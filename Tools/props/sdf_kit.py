"""A small signed-distance modelling kit for detailed hard-surface props without Blender: shapes are
distance functions (numpy, millimetres), unions and cuts blend with fillets, and each part is meshed
with marching cubes, its normals taken from the field's gradient (smooth over curves, still tight on
small fillets), decimated, given box-projected UVs in metres and written out.

Used by Tools/props/make_hand_tools.py (the tools in your hands). Needs numpy, scikit-image and
fast_simplification (pip install scikit-image fast_simplification).

Frame: whatever the caller uses; make_hand_tools.py writes Unreal's (x forward, y right, z up).
"""
import json
import math
import struct
import sys

sys.setrecursionlimit(20000)

import numpy as np

# --- Fields --------------------------------------------------------------------------------------------


def _v(x):
    return np.asarray(x, dtype=np.float64)


def rot_matrix(axis, deg):
    axis = _v(axis) / np.linalg.norm(axis)
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    x, y, z = axis
    return np.array([[c + x * x * (1 - c), x * y * (1 - c) - z * s, x * z * (1 - c) + y * s],
                     [y * x * (1 - c) + z * s, c + y * y * (1 - c), y * z * (1 - c) - x * s],
                     [z * x * (1 - c) - y * s, z * y * (1 - c) + x * s, c + z * z * (1 - c)]])


def frame(f, at=(0, 0, 0), R=None):
    """f placed at `at`, turned by R (its own axes into the parent's): f(R^T (p - at))."""
    at = _v(at)
    if R is None:
        return lambda p: f(p - at)
    Rt = _v(R).T
    return lambda p: f((p - at) @ Rt.T)


def turned(f, axis, deg, at=(0, 0, 0)):
    return frame(f, at, rot_matrix(axis, deg))


def sphere(r):
    return lambda p: np.linalg.norm(p, axis=1) - r


def box(half, round_=0.0):
    """A box of half-sizes `half`, its edges rounded by round_."""
    h = _v(half) - round_
    def f(p):
        q = np.abs(p) - h
        return np.linalg.norm(np.maximum(q, 0.0), axis=1) + np.minimum(np.max(q, axis=1), 0.0) - round_
    return f


def segment(a, b, r):
    """A capsule from a to b, radius r (r may be a pair: r at a, r at b, a cone with round ends)."""
    a, b = _v(a), _v(b)
    ra, rb = (r, r) if np.isscalar(r) else r
    ab = b - a
    L2 = float(ab @ ab)
    def f(p):
        t = np.clip(((p - a) @ ab) / L2, 0.0, 1.0)
        return np.linalg.norm(p - (a + t[:, None] * ab), axis=1) - (ra + (rb - ra) * t)
    return f


def cylinder_z(r, h0, h1, round_=0.0):
    """A cylinder round z from h0 to h1, its rims rounded by round_."""
    mid, half = (h0 + h1) * 0.5, (h1 - h0) * 0.5
    def f(p):
        d = np.stack([np.hypot(p[:, 0], p[:, 1]) - (r - round_), np.abs(p[:, 2] - mid) - (half - round_)], axis=1)
        return np.linalg.norm(np.maximum(d, 0.0), axis=1) + np.minimum(np.max(d, axis=1), 0.0) - round_
    return f


def torus_z(R, r):
    return lambda p: np.hypot(np.hypot(p[:, 0], p[:, 1]) - R, p[:, 2]) - r


def polygon2(pts):
    """The signed distance to a closed 2D polygon (negative inside), as a function of (N, 2)."""
    P = _v(pts)
    def f(q):
        d = np.full(len(q), np.inf)
        s = np.ones(len(q))
        n = len(P)
        for i in range(n):
            a, b = P[i], P[(i + 1) % n]
            e = b - a
            w = q - a
            t = np.clip((w @ e) / (e @ e), 0.0, 1.0)
            dd = w - t[:, None] * e
            d = np.minimum(d, np.einsum("ij,ij->i", dd, dd))
            c1 = q[:, 1] >= a[1]
            c2 = q[:, 1] < b[1]
            c3 = e[0] * w[:, 1] > e[1] * w[:, 0]
            flip = (c1 & c2 & c3) | (~c1 & ~c2 & ~c3)
            s = np.where(flip, -s, s)
        return s * np.sqrt(d)
    return f


def circle2(r, at=(0, 0)):
    at = _v(at)
    return lambda q: np.linalg.norm(q - at, axis=1) - r


def lathe(profile2, axis="z"):
    """A 2D field in (radius, height) turned round z."""
    def f(p):
        q = np.stack([np.hypot(p[:, 0], p[:, 1]), p[:, 2]], axis=1)
        return profile2(q)
    return f


def extrude(f2, h0, h1, round_=0.0):
    """A 2D field in (x, y) pushed along z from h0 to h1, its edges rounded."""
    mid, half = (h0 + h1) * 0.5, (h1 - h0) * 0.5
    def f(p):
        d = np.stack([f2(p[:, :2]) + round_, np.abs(p[:, 2] - mid) - (half - round_)], axis=1)
        return np.linalg.norm(np.maximum(d, 0.0), axis=1) + np.minimum(np.max(d, axis=1), 0.0) - round_
    return f


def union(*fs):
    return lambda p: np.min(np.stack([f(p) for f in fs]), axis=0)


def smooth_union(k, *fs):
    def f(p):
        d = fs[0](p)
        for g in fs[1:]:
            e = g(p)
            h = np.clip(0.5 + 0.5 * (e - d) / k, 0.0, 1.0)
            d = e * (1 - h) + d * h - k * h * (1 - h)
        return d
    return f


def cut(f, *gs, k=0.0):
    """f with gs cut away (a k-mm fillet on the cut's edges)."""
    def h(p):
        d = f(p)
        for g in gs:
            e = -g(p)
            if k <= 0:
                d = np.maximum(d, e)
            else:
                t = np.clip(0.5 - 0.5 * (e - d) / k, 0.0, 1.0)
                d = e * (1 - t) + d * t + k * t * (1 - t)
        return d
    return h


def intersect(f, g):
    return lambda p: np.maximum(f(p), g(p))


def shell(f, t):
    return lambda p: np.abs(f(p)) - t


def offset(f, d):
    return lambda p: f(p) - d


# --- Meshing -------------------------------------------------------------------------------------------


def mesh_field(f, lo, hi, step, target_tris=None, chunk=400000):
    """Marching cubes over [lo, hi] at `step` mm; returns (verts, normals, tris), normals from the
    field's gradient, decimated to target_tris when given."""
    from skimage import measure
    lo, hi = _v(lo) - 2 * step, _v(hi) + 2 * step
    n = np.ceil((hi - lo) / step).astype(int) + 1
    xs = [lo[i] + np.arange(n[i]) * step for i in range(3)]
    grid = np.stack(np.meshgrid(*xs, indexing="ij"), axis=-1).reshape(-1, 3)
    vals = np.empty(len(grid))
    for s in range(0, len(grid), chunk):
        vals[s:s + chunk] = f(grid[s:s + chunk])
    vol = vals.reshape(n)
    if vol.min() > 0 or vol.max() < 0:
        return np.zeros((0, 3)), np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64)
    v, t, _, _ = measure.marching_cubes(vol, 0.0, spacing=(step, step, step))
    v = v + lo
    if target_tris and len(t) > target_tris:
        import fast_simplification
        v, t = fast_simplification.simplify(v.astype(np.float32), t.astype(np.int32), target_count=int(target_tris))
        v = v.astype(np.float64)
    # Pull each vertex onto the surface (a couple of Newton steps) and take its normal from the gradient.
    for _ in range(2):
        d, g = _value_grad(f, v, step * 0.25)
        gn = g / np.maximum(np.linalg.norm(g, axis=1, keepdims=True), 1e-9)
        v = v - gn * np.clip(d, -step, step)[:, None]
    _, g = _value_grad(f, v, step * 0.25)
    nrm = g / np.maximum(np.linalg.norm(g, axis=1, keepdims=True), 1e-9)
    # Wind each triangle to face along its normals.
    t = np.asarray(t, dtype=np.int64)
    fn = np.cross(v[t[:, 1]] - v[t[:, 0]], v[t[:, 2]] - v[t[:, 0]])
    flip = np.einsum("ij,ij->i", fn, nrm[t[:, 0]] + nrm[t[:, 1]] + nrm[t[:, 2]]) < 0
    t[flip] = t[flip][:, [0, 2, 1]]
    return v, nrm, t


def revolve(f2, rmax, z0, z1, step=0.05, segs=96, tol=0.01, splits=None):
    """A surface of revolution round z from a 2D field in (radius, height), meshed exactly (no
    marching cubes): its outline traced at `step`, thinned to `tol`, swept round in `segs`, normals
    from the field's gradient. splits: [(z, name), ...] cut the outline at heights into parts named
    by the band they're in (returns {name: (v, n, t)}); else {None: (v, n, t)}."""
    from skimage import measure
    rs = np.arange(-rmax - 2 * step, rmax + 2 * step, step)
    zs = np.arange(z0 - 2 * step, z1 + 2 * step, step)
    R, Z = np.meshgrid(rs, zs, indexing="ij")
    vals = f2(np.stack([np.abs(R.ravel()), Z.ravel()], axis=1)).reshape(R.shape)
    runs = []
    for c in measure.find_contours(vals, 0.0):
        pts = np.stack([rs[0] + c[:, 0] * step, zs[0] + c[:, 1] * step], axis=1)
        closed = np.allclose(pts[0], pts[-1])
        if (pts[:, 0] < 0).any():
            # Start on the axis's far side and keep the runs this side of it, ends on the axis.
            if closed:
                k = int(np.argmin(pts[:, 0]))
                pts = np.concatenate([pts[k:-1], pts[:k + 1]])
            pos = pts[:, 0] >= 0
            i = 0
            while i < len(pts):
                if not pos[i]:
                    i += 1
                    continue
                j = i
                while j < len(pts) and pos[j]:
                    j += 1
                run = pts[i:j]
                if i > 0:
                    a, b = pts[i - 1], pts[i]
                    t = a[0] / (a[0] - b[0])
                    run = np.concatenate([[[0.0, a[1] + (b[1] - a[1]) * t]], run])
                if j < len(pts):
                    a, b = pts[j - 1], pts[j]
                    t = a[0] / (a[0] - b[0])
                    run = np.concatenate([run, [[0.0, a[1] + (b[1] - a[1]) * t]]])
                runs.append(run)
                i = j
        else:
            runs.append(pts)
    def pull(q):
        for _ in range(3):
            d = f2(q)
            h = step * 0.25
            g = np.stack([(f2(q + [h, 0]) - f2(q - [h, 0])) / (2 * h), (f2(q + [0, h]) - f2(q - [0, h])) / (2 * h)], axis=1)
            g /= np.maximum(np.linalg.norm(g, axis=1, keepdims=True), 1e-9)
            q = q - g * d[:, None]
        return q, g
    out = {}
    names = sorted(splits or [], key=lambda x: x[0])
    def band_of(z):
        name = names[0][1] if names else None
        for zz, nm in names:
            if z >= zz:
                name = nm
        return name
    for run in runs:
        on_axis = abs(run[0, 0]) < 1e-6 and abs(run[-1, 0]) < 1e-6
        run, _ = pull(run)
        if on_axis:
            run[0, 0] = run[-1, 0] = 0.0
        run = _thin(run, tol)
        # Cut it where it crosses a split height (the band's name by its middle).
        pieces = [run]
        for zz, _nm in names[1:]:
            nxt = []
            for pc in pieces:
                k = 0
                cur = [pc[0]]
                for a, b in zip(pc[:-1], pc[1:]):
                    if (a[1] - zz) * (b[1] - zz) < 0:
                        t = (zz - a[1]) / (b[1] - a[1])
                        m = a + (b - a) * t
                        cur.append(m)
                        nxt.append(np.array(cur))
                        cur = [m]
                    cur.append(b)
                nxt.append(np.array(cur))
            pieces = nxt
        for pc in pieces:
            if len(pc) < 2:
                continue
            _, g = pull(pc.copy())
            name = band_of(float(np.mean(pc[:, 1])))
            v, n, t = _sweep(pc, g, segs)
            V, N, T = out.get(name, (np.zeros((0, 3)), np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64)))
            out[name] = (np.concatenate([V, v]), np.concatenate([N, n]), np.concatenate([T, t + len(V)]))
    return out


def _thin(pts, tol):
    """Douglas-Peucker."""
    if len(pts) < 3:
        return pts
    a, b = pts[0], pts[-1]
    ab = b - a
    L = np.linalg.norm(ab)
    if L < 1e-12:
        d = np.linalg.norm(pts - a, axis=1)
    else:
        w = pts - a
        d = np.abs(ab[0] * w[:, 1] - ab[1] * w[:, 0]) / L
    i = int(np.argmax(d))
    if d[i] <= tol:
        return np.array([a, b])
    left, right = _thin(pts[:i + 1], tol), _thin(pts[i:], tol)
    return np.concatenate([left[:-1], right])


def _sweep(pc, g, segs):
    a = np.linspace(0, 2 * math.pi, segs, endpoint=False)
    c, s = np.cos(a), np.sin(a)
    V = np.stack([pc[:, 0][:, None] * c[None, :], pc[:, 0][:, None] * s[None, :], np.repeat(pc[:, 1][:, None], segs, 1)], axis=2).reshape(-1, 3)
    N = np.stack([g[:, 0][:, None] * c[None, :], g[:, 0][:, None] * s[None, :], np.repeat(g[:, 1][:, None], segs, 1)], axis=2).reshape(-1, 3)
    T = []
    for i in range(len(pc) - 1):
        for k in range(segs):
            a0, a1 = i * segs + k, i * segs + (k + 1) % segs
            b0, b1 = a0 + segs, a1 + segs
            T += [[a0, a1, b1], [a0, b1, b0]]
    T = np.array(T, dtype=np.int64)
    fn = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
    good = np.linalg.norm(fn, axis=1) > 1e-12
    T = T[good]
    fn = fn[good]
    flip = np.einsum("ij,ij->i", fn, N[T[:, 0]] + N[T[:, 1]] + N[T[:, 2]]) < 0
    T[flip] = T[flip][:, [0, 2, 1]]
    return V, N, T


def _value_grad(f, v, h):
    d = f(v)
    g = np.zeros_like(v)
    for i in range(3):
        e = np.zeros(3)
        e[i] = h
        g[:, i] = (f(v + e) - f(v - e)) / (2 * h)
    return d, g


def box_uv(v, n, scale=0.001):
    """UVs by the normal's main axis (metres, for the props' tiled scans)."""
    a = np.argmax(np.abs(n), axis=1)
    u = np.where(a == 0, v[:, 1], v[:, 0])
    w = np.where(a == 2, v[:, 1], v[:, 2])
    return np.stack([u, w], axis=1) * scale


# --- A model: its parts by material --------------------------------------------------------------------


class Model:
    def __init__(self, name):
        self.name = name
        self.slots = {}   # material -> list of (v, n, t)

    def add(self, material, f, lo, hi, step=0.3, tris=None):
        v, n, t = mesh_field(f, lo, hi, step, tris)
        if len(t):
            self.slots.setdefault(material, []).append((v, n, t))
        return len(t)

    def add_revolve(self, material, f2, rmax, z0, z1, R=None, at=(0, 0, 0), splits=None, **kw):
        """A surface of revolution (sdf_kit.revolve) round local z, placed by R and at; with splits,
        material maps each band's name to a slot."""
        parts = revolve(f2, rmax, z0, z1, splits=splits, **kw)
        R = np.eye(3) if R is None else np.asarray(R, float)
        for name, (v, n, t) in parts.items():
            slot = material if name is None else name
            self.add_mesh(slot, v @ R.T + np.asarray(at, float), n @ R.T, t)

    def add_mesh(self, material, v, n, t):
        self.slots.setdefault(material, []).append((_v(v), _v(n), np.asarray(t, dtype=np.int64)))

    def merged(self):
        out = {}
        for m, parts in self.slots.items():
            V, N, T, base = [], [], [], 0
            for v, n, t in parts:
                V.append(v); N.append(n); T.append(t + base); base += len(v)
            out[m] = (np.concatenate(V), np.concatenate(N), np.concatenate(T))
        return out

    def tri_count(self):
        return sum(len(t) for parts in self.slots.values() for _, _, t in parts)

    def bounds(self):
        allv = np.concatenate([v for parts in self.slots.values() for v, _, _ in parts])
        return allv.min(axis=0), allv.max(axis=0)

    def write_tmesh(self, path, unit=0.1):
        """The game's runtime format (TorqueToolModels.cpp reads it): little-endian
        'TMSH', u32 version 1, u32 slots; per slot: u32 name bytes, name (utf-8), u32 verts, u32 indices,
        f32 xyz per vert (cm), f32 normal per vert, f32 uv per vert, u32 indices."""
        m = self.merged()
        with open(path, "wb") as fh:
            fh.write(b"TMSH" + struct.pack("<II", 1, len(m)))
            for name in sorted(m):
                v, n, t = m[name]
                uv = box_uv(v, n)
                nb = name.encode("utf-8")
                fh.write(struct.pack("<I", len(nb)) + nb)
                fh.write(struct.pack("<II", len(v), t.size))
                fh.write((v * unit).astype("<f4").tobytes())
                fh.write(n.astype("<f4").tobytes())
                fh.write(uv.astype("<f4").tobytes())
                fh.write(t.astype("<u4").tobytes())

    def write_glb(self, path, colours, unit=0.001):
        """A preview glb (glTF's frame: Unreal's x forward -> -z, y right -> x, z up -> y; metres)."""
        m = self.merged()
        bin_ = bytearray()
        views, accs, prims, mats = [], [], [], []

        def put(arr, target):
            nonlocal bin_
            while len(bin_) % 4:
                bin_ += b"\0"
            off = len(bin_)
            bin_ += arr.tobytes()
            views.append({"buffer": 0, "byteOffset": off, "byteLength": arr.nbytes, "target": target})
            return len(views) - 1

        for i, name in enumerate(sorted(m)):
            v, n, t = m[name]
            gv = np.stack([v[:, 1], v[:, 2], -v[:, 0]], axis=1) * unit
            gn = np.stack([n[:, 1], n[:, 2], -n[:, 0]], axis=1)
            gt = t[:, [0, 2, 1]]   # the frame change is a mirror in numbers (Unreal's is left-handed), so rewind
            pv = put(gv.astype("<f4"), 34962)
            accs.append({"bufferView": pv, "componentType": 5126, "count": len(gv), "type": "VEC3",
                         "min": gv.min(axis=0).tolist(), "max": gv.max(axis=0).tolist()})
            pn = put(gn.astype("<f4"), 34962)
            accs.append({"bufferView": pn, "componentType": 5126, "count": len(gn), "type": "VEC3"})
            pi = put(gt.astype("<u4").ravel(), 34963)
            accs.append({"bufferView": pi, "componentType": 5125, "count": int(gt.size), "type": "SCALAR"})
            c = colours.get(name, {"colour": [0.8, 0.0, 0.8], "metal": 0.0, "rough": 0.5})
            mats.append({"name": name, "pbrMetallicRoughness": {"baseColorFactor": list(c["colour"]) + [1.0],
                         "metallicFactor": c.get("metal", 0.0), "roughnessFactor": c.get("rough", 0.5)}})
            prims.append({"attributes": {"POSITION": len(accs) - 3, "NORMAL": len(accs) - 2}, "indices": len(accs) - 1, "material": i})
        gltf = {"asset": {"version": "2.0", "generator": "Torque Shed sdf_kit"}, "scene": 0,
                "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": self.name}],
                "meshes": [{"primitives": prims}], "materials": mats, "accessors": accs,
                "bufferViews": views, "buffers": [{"byteLength": len(bin_)}]}
        js = json.dumps(gltf).encode()
        js += b" " * ((4 - len(js) % 4) % 4)
        while len(bin_) % 4:
            bin_ += b"\0"
        with open(path, "wb") as fh:
            fh.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_)))
            fh.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
            fh.write(struct.pack("<II", len(bin_), 0x004E4942) + bytes(bin_))
