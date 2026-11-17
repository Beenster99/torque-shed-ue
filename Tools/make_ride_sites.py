"""The famous places' own models (places plan stage 6): Data/RideUK/Sites/<id>.glb, made once here and kept
in the repo, read by the game at start (World/WorldRideSites.cpp) and drawn by PlacesTile in place of the
place's recipe where its site note in Data/RideUK/landmarks_uk.json names one ("model": "<id>").

    python Tools/make_ride_sites.py            (all of them)
    python Tools/make_ride_sites.py stonehenge  (just those named)

Each is made from what is openly known of the place: its outline and height on OpenStreetMap, its facts on
Wikipedia and Wikidata, and openly licensed photographs looked at while writing it (none ship). The frame
is the ride's: metres, x east, y up, z south, the place's middle at (0, 0, 0) on its ground (y = 0), and
everything goes some way below the ground so it sits into a slope.

Every part is one node of the glb, drawn with the ride's own material its material is named for:
    stone     the scanned stone of the walls and the stone circles (RGBA masks: R brightness, 0.5 the scan
              as it is; G which scan, 0 lichened limestone, 1 bare; B lichen; A moss)
    masonry   the scanned masonry of the houses and barns (the same masks)
    render    limewash and paint (RGB the colour, linear; A dirt)
    roof      roofs (RGB the colour; A the roofing: 0 slate, 0.25 stone slates, 0.5 clay tiles)
    paint     anything else, coloured (RGB linear; A 1)
A node named with "#drop" stands on the ground under its own middle (each stone of a circle on a slope);
the rest stand together on the lowest ground under them. "#far" marks what is drawn from far off too (a
tall place with none marked is drawn whole from far off). UVs are metres of surface.
"""
import json
import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import buttertubs_stone as ST   # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "RideUK", "Sites")


# --- A site being built: parts by name, each one layer -----------------------------------------------

class Site:
    def __init__(self, name):
        self.name = name
        self.parts = {}    # node name -> (layer, ST.Mesh)

    def mesh(self, node, layer):
        if node not in self.parts:
            self.parts[node] = (layer, ST.Mesh())
        return self.parts[node][1]


def masks(n, bright=0.5, bare=1.0, lichen=0.2, moss=0.1, rng=None, jitter=0.03):
    c = np.zeros((n, 4))
    c[:, 0] = bright; c[:, 1] = bare; c[:, 2] = lichen; c[:, 3] = moss
    if rng is not None:
        c[:, 0] = np.clip(c[:, 0] + rng.normal(0, jitter, n), 0.05, 1)
    return c


def foot_moss(P, base, low=0.6, top=0.0):
    """Moss thick at the foot (below y = low) and thinning up, base a little everywhere."""
    return np.clip(base + np.clip((low - P[:, 1]) / max(low, 1e-3), 0, 1) * 0.6 + top, 0, 1)


def colour(n, rgb, a=1.0):
    c = np.zeros((n, 4)); c[:, :3] = rgb; c[:, 3] = a
    return c


# --- Shapes (all wound outwards, counter-clockwise seen from outside) ---------------------------------

def _outward(P, T, centre):
    fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    fc = P[T].mean(1)
    bad = (fn * (fc - centre)).sum(1) < 0
    T = T.copy()
    T[bad] = T[bad][:, [0, 2, 1]]
    return T


def ngon(n, r, turn=None):
    a0 = math.pi / n if turn is None else turn
    return [(r * math.cos(a0 + 2 * math.pi * k / n), r * math.sin(a0 + 2 * math.pi * k / n)) for k in range(n)]


def prism(M, ring, y0, y1, cols, top_scale=1.0, cap_top=True, cap_bottom=False, shift=(0.0, 0.0), uv_off=0.0):
    """An upright prism over a convex ring of (x, z) (about its own middle), y0 to y1, its top ring
    top_scale times the foot (a taper), flat faces. cols: a function n -> RGBA rows (or rows for 1)."""
    ring = np.asarray(ring, float)
    mid = ring.mean(0)
    top = mid + (ring - mid) * top_scale + np.asarray(shift)
    n = len(ring)
    P, T, U = [], [], []
    per = 0.0
    for k in range(n):
        a, b = ring[k], ring[(k + 1) % n]
        c, d = top[(k + 1) % n], top[k]
        L = float(np.hypot(*(b - a)))
        base = len(P)
        P += [[a[0], y0, a[1]], [b[0], y0, b[1]], [c[0], y1, c[1]], [d[0], y1, d[1]]]
        U += [[per + uv_off, -y0], [per + L + uv_off, -y0], [per + L + uv_off, -y1], [per + uv_off, -y1]]
        T += [[base, base + 1, base + 2], [base, base + 2, base + 3]]
        per += L
    for cap, y, rr in ((cap_top, y1, top), (cap_bottom, y0, ring)):
        if not cap:
            continue
        base = len(P)
        for q in rr:
            P.append([q[0], y, q[1]]); U.append([q[0], q[1]])
        for k in range(1, n - 1):
            T.append([base, base + k, base + k + 1])
    P = np.array(P); T = np.array(T); U = np.array(U)
    T = _outward(P, T, np.array([mid[0], 0.5 * (y0 + y1), mid[1]]))
    # (caps: the centre test above can't tell for a flat cap; set them by which cap they are)
    if cap_top or cap_bottom:
        fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
        flat = np.abs(fn[:, 1]) > 0.999 * np.linalg.norm(fn, axis=1)
        yc = P[T].mean(1)[:, 1]
        want_up = yc > 0.5 * (y0 + y1)
        wrong = flat & ((fn[:, 1] > 0) != want_up)
        T[wrong] = T[wrong][:, [0, 2, 1]]
    M.add(P, T, cols(len(P)) if callable(cols) else colour(len(P), cols[:3], cols[3] if len(cols) > 3 else 1.0), U)
    return P


def block(M, c, h, cols, yaw=0.0, R=None):
    """A box: centre c, half sizes h, turned yaw (radians, clockwise seen from above) or by R."""
    if R is None:
        cy, sy = math.cos(yaw), math.sin(yaw)
        R = np.array([[cy, 0, -sy], [0, 1, 0], [sy, 0, cy]])
    P, T, U = ST.obox(c, h, R)
    M.add(P, T, cols(len(P)) if callable(cols) else colour(len(P), cols[:3], cols[3] if len(cols) > 3 else 1.0), U)
    return P


def hexa(M, b4, t4, cols):
    """A six-faced solid from its foot's four corners (x, y, z) and its top's, flat faces."""
    Q = np.array(list(b4) + list(t4), float)
    faces = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    P, T, U = [], [], []
    mid = Q.mean(0)
    for f in faces:
        base = len(P)
        q = Q[list(f)]
        e = q[1] - q[0]; e /= max(np.linalg.norm(e), 1e-9)
        for v in q:
            P.append(v); U.append([float((v - q[0]) @ e), float(-v[1])])
        T += [[base, base + 1, base + 2], [base, base + 2, base + 3]]
    P = np.array(P); T = _outward(P, np.array(T), mid)
    M.add(P, T, cols(len(P)) if callable(cols) else colour(len(P), cols[:3], cols[3] if len(cols) > 3 else 1.0), np.array(U))


def frame_at(x, z, yaw):
    """(along, across) about (x, z) facing along yaw (radians clockwise from east, seen from above)."""
    c, s = math.cos(yaw), math.sin(yaw)
    return lambda a, b: (x + c * a - s * b, z + s * a + c * b)


def standing_stone(rng, M, x, z, w, h, t, yaw, bury=0.6, lean=(0.0, 0.0), taper=0.8, boxy=(0.3, 0.4), res=(9, 14),
                   chops=4, bright=None, bare=None, lichen=None, moss=0.15, flutes=0, flute_depth=0.0, bump=0.02):
    """A rough stone w wide, t thick and h above the ground, its foot bury below, along yaw, leaning by
    lean (m at the top, along and across), narrower at the top by taper."""
    tall = h + bury
    P, T, U = ST.stone(rng, (w * 0.5, tall * 0.5, t * 0.5), (0, 0, 0), None, res, boxy, chops, (0.7, 0.95), bump)
    yy = (P[:, 1] + tall * 0.5) / tall          # 0 foot .. 1 top
    k = 1.0 + (taper - 1.0) * np.clip(yy, 0, 1) ** 1.3
    P[:, 0] *= k; P[:, 2] *= k
    if flutes:
        ang = np.arctan2(P[:, 2] / max(t, 1e-3), P[:, 0] / max(w, 1e-3))
        groove = (0.5 + 0.5 * np.cos(ang * flutes + rng.uniform(0, 6.28))) ** 1.5
        ramp = np.clip((yy - 0.66) / 0.34, 0, 1) ** 1.1
        rad = np.hypot(P[:, 0], P[:, 2])
        cut = flute_depth * groove * ramp
        s = np.maximum(rad - cut, 0.05 * rad) / np.maximum(rad, 1e-6)
        P[:, 0] *= s; P[:, 2] *= s
        P[:, 1] -= 0.12 * groove * ramp * tall * 0.05    # (the top's ridges worn lower in the grooves)
    P[:, 1] += tall * 0.5 - bury
    P[:, 0] += lean[0] * np.clip(P[:, 1] / max(h, 1e-3), 0, None)
    P[:, 2] += lean[1] * np.clip(P[:, 1] / max(h, 1e-3), 0, None)
    c, s = math.cos(yaw), math.sin(yaw)
    X = P[:, 0] * c - P[:, 2] * s; Z = P[:, 0] * s + P[:, 2] * c
    P = np.stack([X + x, P[:, 1], Z + z], 1)
    col = ST.stone_col(rng, len(P), bright, bare, lichen, 0.0)
    col[:, 3] = foot_moss(P, moss, 0.5)
    M.add(P, T, col, U)
    return P


def lying_stone(rng, M, x, z, l, w, t, yaw, sink=0.25, **kw):
    """A fallen stone l long, w wide and t thick lying along yaw, sunk sink into the turf."""
    P, T, U = ST.stone(rng, (l * 0.5, t * 0.5, w * 0.5), (0, 0, 0), None, kw.get("res", (8, 14)), kw.get("boxy", (0.3, 0.4)), 4, (0.7, 0.95), 0.02)
    P[:, 1] += t * 0.5 - sink
    tilt = rng.uniform(-0.06, 0.06)
    P[:, 1] += P[:, 0] * tilt
    c, s = math.cos(yaw), math.sin(yaw)
    X = P[:, 0] * c - P[:, 2] * s; Z = P[:, 0] * s + P[:, 2] * c
    P = np.stack([X + x, P[:, 1], Z + z], 1)
    col = ST.stone_col(rng, len(P), kw.get("bright"), kw.get("bare"), kw.get("lichen"), 0.0)
    col[:, 3] = foot_moss(P, kw.get("moss", 0.25), 0.4)
    M.add(P, T, col, U)


def panel(M, at, normal, right, w, h, rgb, proud=0.04, depth=0.08, arch=False):
    """A window or door seen as a dark recess: a thin plate w by h standing proud of a wall face at
    'at' (its foot's middle), facing 'normal', with 'right' along the wall. An arched head if arch."""
    at, n, r = np.asarray(at, float), np.asarray(normal, float), np.asarray(right, float)
    up = np.array([0.0, 1.0, 0.0])
    pts = [(-w / 2, 0), (w / 2, 0)]
    if arch:
        sp = h - w / 2
        pts += [(w / 2 * math.cos(a), sp + w / 2 * math.sin(a)) for a in np.linspace(0, math.pi, 7)]
    else:
        pts += [(w / 2, h), (-w / 2, h)]
    ring = [at + r * a + up * b for a, b in pts]
    P, T, U = [], [], []
    for side in (proud, proud - depth):
        base = len(P)
        for q in ring:
            P.append(q + n * side); U.append([float((q - at) @ r), float(-(q - at)[1])])
        for k in range(1, len(ring) - 1):
            T.append([base, base + k, base + k + 1])
    P = np.array(P); T = np.array(T)
    fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    bad = (fn @ n) < 0
    T[bad] = T[bad][:, [0, 2, 1]]
    T = T[: len(T) // 2]        # (only the front plate: the back is in the wall)
    M.add(P, T, colour(len(P), rgb, 1.0), np.array(U))


def ring_panels(M, ring, y, w, h, rgb, every=1, arch=False, skip=(), proud=0.03):
    """A window on each face of a convex ring (x, z) at height y."""
    ring = np.asarray(ring, float)
    mid = ring.mean(0)
    n = len(ring)
    for k in range(0, n, every):
        if k in skip:
            continue
        a, b = ring[k], ring[(k + 1) % n]
        m = 0.5 * (a + b)
        d = b - a; d /= np.linalg.norm(d)
        nn = np.array([d[1], -d[0]])
        if (m - mid) @ nn < 0:
            nn = -nn
        panel(M, [m[0], y, m[1]], [nn[0], 0, nn[1]], [d[0], 0, d[1]], w, h, rgb, proud=proud, arch=arch)


def merlons(M, a, b, y, cols, w=0.9, gap=0.7, h=0.8, t=0.55, out=None):
    """Battlements along a wall top from a to b (x, z): merlons w long with gaps between."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    L = float(np.linalg.norm(b - a))
    d = (b - a) / max(L, 1e-6)
    yaw = math.atan2(d[1], d[0])
    n = max(1, int((L + gap) // (w + gap)))
    pad = (L - (n * w + (n - 1) * gap)) * 0.5
    for k in range(n):
        u = pad + k * (w + gap) + w * 0.5
        c = a + d * u
        block(M, (c[0], y + h * 0.5, c[1]), (w * 0.5, h * 0.5, t * 0.5), cols, yaw)


# --- Writing ------------------------------------------------------------------------------------------

LAYERS = ("stone", "masonry", "render", "roof", "brick", "glow", "paint")


def write_site(path, site):
    """A glb of the site's parts: one node, mesh and primitive each, its material its layer."""
    blobs, views, accs, meshes, nodes = [], [], [], [], []
    mats = []
    off = 0

    def add(arr, target, comp, typ, norm=False, minmax=False):
        nonlocal off
        b = arr.tobytes()
        pad = (-len(b)) % 4
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(b), "target": target})
        a = {"bufferView": len(views) - 1, "componentType": comp, "count": int(arr.shape[0]), "type": typ}
        if norm:
            a["normalized"] = True
        if minmax:
            a["min"] = arr.min(0).tolist(); a["max"] = arr.max(0).tolist()
        accs.append(a)
        blobs.append(b + b"\0" * pad); off += len(b) + pad
        return len(accs) - 1

    tris_all = 0
    for name in sorted(site.parts):
        layer, M = site.parts[name]
        if not M.P:
            continue
        pos, tris, nrm, col, uv = M.arrays()
        tris_all += len(tris)
        if layer not in mats:
            mats.append(layer)
        attrs = {"POSITION": add(np.ascontiguousarray(pos, np.float32), 34962, 5126, "VEC3", minmax=True),
                 "NORMAL": add(np.ascontiguousarray(nrm, np.float32), 34962, 5126, "VEC3"),
                 "TEXCOORD_0": add(np.ascontiguousarray(uv, np.float32), 34962, 5126, "VEC2"),
                 "COLOR_0": add(np.ascontiguousarray(col, np.uint8), 34962, 5121, "VEC4", norm=True)}
        ia = add(np.ascontiguousarray(tris, np.uint32).ravel(), 34963, 5125, "SCALAR")
        meshes.append({"name": name, "primitives": [{"attributes": attrs, "indices": ia, "material": mats.index(layer), "mode": 4}]})
        nodes.append({"mesh": len(meshes) - 1, "name": name})
    gl = {"asset": {"version": "2.0", "generator": "torque-shed-ue Tools/make_ride_sites.py"},
          "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes, "meshes": meshes,
          "materials": [{"name": m, "pbrMetallicRoughness": {"roughnessFactor": 0.9, "metallicFactor": 0.0}} for m in mats],
          "buffers": [{"byteLength": off}], "bufferViews": views, "accessors": accs}
    js = json.dumps(gl, separators=(",", ":")).encode()
    js += b" " * ((-len(js)) % 4)
    bin_ = b"".join(blobs)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack("<II", len(bin_), 0x004E4942)); f.write(bin_)
    return len(nodes), tris_all


# --- The places -----------------------------------------------------------------------------------

def az(deg):
    """A compass bearing (degrees, 0 north, clockwise) as the ride's angle (radians from east towards south)."""
    return math.radians(deg - 90.0)


def polar(deg, r):
    a = az(deg)
    return r * math.cos(a), r * math.sin(a)


SARSEN = dict(bright=0.56, bare=0.85, lichen=0.45)


def stonehenge():
    """Stonehenge as it stands: the sarsen circle (17 of its 30 uprights up, six lintels on them), the
    horseshoe of five trilithons opening to the midsummer sunrise (three whole, the Great Trilithon with
    one upright, one with one), the bluestone circle and horseshoe, the Altar Stone under the fallen
    lintel, the Slaughter Stone, the Heel Stone and the two Station Stones. The axis points north east
    (bearing 50 degrees). Each stone on the ground under it; the circle's lintels go with its uprights."""
    rng = np.random.default_rng(1)
    S = Site("stonehenge")
    AXIS = 50.0
    # The sarsen circle: stones numbered clockwise from the entrance (1 and 30 either side of the axis).
    ring = S.mesh("sarsen_circle#drop#far", "stone")
    R = 15.6
    standing = {1, 2, 3, 4, 5, 6, 7, 10, 11, 16, 21, 22, 23, 27, 28, 29, 30}
    stumps = {8, 9, 12, 14, 15, 19, 25, 26}
    lintels = [(1, 2), (2, 3), (5, 6), (6, 7), (22, 23), (30, 1)]
    tops = {}
    for k in range(1, 31):
        b = AXIS + (k - 0.5) * 12.0
        x, z = polar(b, R)
        yaw = az(b) + math.pi / 2       # (its face to the middle)
        if k in standing:
            h = 4.1 + rng.normal(0, 0.08)
            w = 1.05 if k == 11 else 2.1 + rng.normal(0, 0.1)
            standing_stone(rng, ring, x, z, w, h, 1.1, yaw, bury=1.2, taper=0.88, boxy=(0.12, 0.18), res=(10, 16), chops=2, bump=0.03,
                           lean=(rng.normal(0, 0.04), rng.normal(0, 0.06)), **SARSEN, moss=0.08)
            tops[k] = (x, z, h)
        elif k in stumps:
            M = S.mesh("sarsen_%02d#drop" % k, "stone")
            if rng.random() < 0.5:
                standing_stone(rng, M, x, z, 1.6, rng.uniform(0.4, 1.2), 1.0, yaw, bury=0.4, taper=0.9, boxy=(0.3, 0.4), **SARSEN)
            else:
                ox, oz = polar(b + rng.uniform(-6, 6), R + rng.uniform(-2.5, 2.5))
                lying_stone(rng, M, ox, oz, rng.uniform(2.5, 4.0), 1.8, 0.9, az(b) + rng.uniform(-1, 1), sink=0.3, boxy=(0.15, 0.2), **SARSEN)
    for a, b in lintels:
        xa, za, ha = tops[a]; xb, zb, hb = tops[b]
        L = math.hypot(xb - xa, zb - za)
        yaw = math.atan2(zb - za, xb - xa)
        y = min(ha, hb) + 0.38
        P, T, U = ST.stone(rng, ((L + 1.2) * 0.5, 0.4, 0.5), (0, 0, 0), None, (6, 12), (0.1, 0.15), 1, (0.8, 0.95), 0.015)
        # (curved to the ring, as they are: tongue and groove, dressed on a curve)
        P[:, 2] += (P[:, 0] ** 2) / (2 * R) * -1.0
        c, s = math.cos(yaw), math.sin(yaw)
        mx, mz = (xa + xb) * 0.5, (za + zb) * 0.5
        # (the curve towards the middle: z local positive is across, flip so it bows outward)
        Pw = np.stack([P[:, 0] * c - P[:, 2] * s + mx, P[:, 1] + y, P[:, 0] * s + P[:, 2] * c + mz], 1)
        ring.add(Pw, T, ST.stone_col(rng, len(Pw), 0.58, 0.85, 0.5, 0.05), U)
    # The trilithons: horseshoe opening north east, the Great Trilithon at the far (south west) end.
    tri = [(51, AXIS + 75, 7.3, 6.0, "whole"), (53, AXIS + 125, 6.8, 6.3, "whole"), (55, AXIS + 180, 6.2, 7.3, "great"),
           (57, AXIS + 235, 6.8, 6.3, "whole"), (59, AXIS + 285, 7.3, 6.6, "one")]
    for n, b, r, h, how in tri:
        M = S.mesh("trilithon_%d#drop#far" % n, "stone")
        cx, cz = polar(b, r)
        yaw = az(b) + math.pi / 2
        f = frame_at(cx, cz, yaw)
        ups = [f(-1.35, 0.0), f(1.35, 0.0)]
        if how == "whole":
            for (ux, uz) in ups:
                standing_stone(rng, M, ux, uz, 2.3, h, 1.3, yaw, bury=1.5, taper=0.86, boxy=(0.12, 0.16), res=(10, 16), chops=2, bump=0.03, **SARSEN, moss=0.06)
            P, T, U = ST.stone(rng, (2.4, 0.55, 0.6), (0, 0, 0), None, (6, 12), (0.1, 0.15), 1, (0.8, 0.95), 0.015)
            c, s = math.cos(yaw), math.sin(yaw)
            Pw = np.stack([P[:, 0] * c - P[:, 2] * s + cx, P[:, 1] + h + 0.5, P[:, 0] * s + P[:, 2] * c + cz], 1)
            M.add(Pw, T, ST.stone_col(rng, len(Pw), 0.58, 0.85, 0.5, 0.04), U)
        elif how == "great":
            # (stone 56 stands, leaning a little, its tenon on top; 55 lies broken in two; lintel 156 fallen)
            ux, uz = ups[1]
            standing_stone(rng, M, ux, uz, 2.4, h, 1.4, yaw, bury=2.4, taper=0.84, boxy=(0.12, 0.16), res=(10, 16), chops=2, lean=(0.0, 0.25), **SARSEN, moss=0.05)
            ox, oz = f(-2.0, 2.2)
            lying_stone(rng, M, ox, oz, 3.6, 2.2, 1.1, yaw + 1.4, sink=0.35, boxy=(0.12, 0.16), **SARSEN)
            ox, oz = f(-1.0, 5.4)
            lying_stone(rng, M, ox, oz, 3.4, 2.2, 1.0, yaw + 1.2, sink=0.35, boxy=(0.12, 0.16), **SARSEN)
            ox, oz = f(0.6, 3.6)
            lying_stone(rng, M, ox, oz, 4.6, 1.2, 1.0, yaw + 0.25, sink=0.2, boxy=(0.12, 0.16), **SARSEN)
        else:
            ux, uz = ups[1]
            standing_stone(rng, M, ux, uz, 2.3, h, 1.3, yaw, bury=1.5, taper=0.86, boxy=(0.12, 0.16), res=(10, 16), chops=2, **SARSEN, moss=0.06)
            ox, oz = f(-1.8, 2.4)
            lying_stone(rng, M, ox, oz, 5.5, 2.2, 1.1, yaw + 1.3, sink=0.3, boxy=(0.12, 0.16), **SARSEN)
            ox, oz = f(0.2, 4.0)
            lying_stone(rng, M, ox, oz, 4.4, 1.2, 1.0, yaw + 0.3, sink=0.25, boxy=(0.12, 0.16), **SARSEN)
    # The Altar Stone, flat under the Great Trilithon's lintel.
    M = S.mesh("altar_stone#drop", "stone")
    ax_, az_ = polar(AXIS + 180, 4.6)
    lying_stone(rng, M, ax_, az_, 4.9, 1.0, 0.5, az(AXIS + 90), sink=0.2, bright=0.42, bare=0.3, lichen=0.3)
    # The bluestones: smaller, darker, a broken circle inside the sarsens and a horseshoe inside the trilithons.
    blue = dict(bright=0.36, bare=0.2, lichen=0.35)
    for k in range(34):
        b = AXIS + 6 + k * (360.0 / 34) + rng.normal(0, 2.0)
        if rng.random() < 0.45:
            continue
        x, z = polar(b, 11.8 + rng.normal(0, 0.4))
        M = S.mesh("bluestone_c%02d#drop" % k, "stone")
        if rng.random() < 0.25:
            lying_stone(rng, M, x, z, rng.uniform(1.2, 2.2), 0.7, 0.5, az(b) + rng.uniform(-0.6, 0.6), sink=0.25, **blue)
        else:
            standing_stone(rng, M, x, z, rng.uniform(0.6, 1.0), rng.uniform(0.5, 1.7), 0.55, az(b) + math.pi / 2, bury=0.5, taper=0.75,
                           lean=(rng.normal(0, 0.15), rng.normal(0, 0.15)), res=(7, 10), **blue)
    for k in range(13):
        b = AXIS + 70 + k * (220.0 / 12)
        r = 5.2 + 1.2 * abs(math.cos(math.radians((k - 6) * 15)))
        x, z = polar(b, r)
        M = S.mesh("bluestone_h%02d#drop" % k, "stone")
        if k in (2, 5, 9):
            continue
        standing_stone(rng, M, x, z, 0.65, rng.uniform(1.6, 2.4), 0.5, az(b) + math.pi / 2, bury=0.6, taper=0.6, boxy=(0.2, 0.3), res=(7, 10), **blue)
    # Out along the axis: the Slaughter Stone lying at the entrance, the Heel Stone leaning, the Station Stones.
    M = S.mesh("slaughter_stone#drop", "stone")
    x, z = polar(AXIS - 3, 31.0)
    lying_stone(rng, M, x, z, 6.4, 2.1, 0.8, az(AXIS) + 0.2, sink=0.3, boxy=(0.2, 0.3), **SARSEN)
    M = S.mesh("heel_stone#drop#far", "stone")
    x, z = polar(AXIS + 1.5, 77.0)
    standing_stone(rng, M, x, z, 2.4, 4.7, 2.1, az(AXIS) + math.pi / 2, bury=1.2, taper=0.7, boxy=(0.4, 0.5), res=(10, 16), chops=5,
                   lean=(math.cos(az(AXIS + 180)) * 0.9, math.sin(az(AXIS + 180)) * 0.9), bright=0.5, bare=0.6, lichen=0.6, moss=0.15)
    for k, b in ((91, AXIS + 72), (93, AXIS + 252)):
        M = S.mesh("station_%d#drop" % k, "stone")
        x, z = polar(b, 40.0)
        if k == 91:
            lying_stone(rng, M, x, z, 2.7, 1.1, 0.6, az(b), sink=0.2, **SARSEN)
        else:
            standing_stone(rng, M, x, z, 1.1, 1.2, 0.7, az(b), bury=0.4, taper=0.8, **SARSEN)
    return S


LAKELAND = dict(bright=0.45, bare=0.35, lichen=0.55)


def castlerigg():
    """Castlerigg: 38 stones in a flattened ring about 32.6 m by 29.5 m (flat side to the north east),
    its entrance between two tall stones at the north, its tallest (2.3 m) in the south east, and the
    'sanctuary', a rectangle of ten low stones against the ring's inside on the east."""
    rng = np.random.default_rng(2)
    S = Site("castlerigg")
    n = 38
    for k in range(n):
        b = (k + 0.5) * 360.0 / n + rng.normal(0, 1.5)
        a = math.radians(b)
        # (flattened: a 16.3 by 14.75 m ellipse, its north east quarter drawn in)
        rx, rz = 14.75, 16.3
        x, z = rx * math.sin(a), -rz * math.cos(a)
        if 15 < b < 95:
            pull = math.sin(math.radians((b - 15) * 180 / 80)) * 1.6
            x -= pull * math.sin(a); z += pull * math.cos(a)
        M = S.mesh("stone_%02d#drop" % k, "stone")
        if abs(((b + 180) % 360) - 180) < 6.5:
            continue    # (the gap of the entrance at the north)
        h = rng.uniform(0.9, 1.6)
        w = rng.uniform(0.8, 1.6)
        if abs(b - 360.0 / n * 1) < 6 or abs(b - (360 - 360.0 / n)) < 6:
            h, w = 1.7, 1.5           # (the stones of the entrance)
        if 125 < b < 140:
            h, w = 2.3, 1.3           # (the tallest, south east)
        if rng.random() < 0.12:
            lying_stone(rng, M, x, z, h + 0.3, w, 0.6, az(b) + math.pi / 2 + rng.uniform(-0.5, 0.5), sink=0.2, **LAKELAND)
            continue
        standing_stone(rng, M, x, z, w, h, rng.uniform(0.5, 0.9), az(b) + math.pi / 2 + rng.normal(0, 0.25), bury=0.5, taper=0.7,
                       lean=(rng.normal(0, 0.1), rng.normal(0, 0.1)), boxy=(0.35, 0.5), res=(8, 12), chops=5, **LAKELAND, moss=0.25)
    # The sanctuary: ten low stones in a rectangle about 7.5 by 4 m on the east inside.
    M = S.mesh("sanctuary#drop", "stone")
    for k in range(10):
        if k < 4:
            x, z = 6.0 + 4.0, -3.75 + k * 2.5
        elif k < 7:
            x, z = 6.0 + (k - 4) * 1.3, -3.75
        else:
            x, z = 6.0 + (k - 7) * 1.3, 3.75
        standing_stone(rng, M, x, z, rng.uniform(0.5, 0.9), rng.uniform(0.3, 0.7), 0.4, rng.uniform(0, 3.1), bury=0.25, taper=0.8, res=(7, 10), **LAKELAND, moss=0.3)
    return S


GRIT = dict(bright=0.42, bare=0.75, lichen=0.4)


def rudston():
    """The Rudston Monolith: one gritstone slab 7.6 m tall, 1.75 m wide and under a metre thick, narrowing
    to its top, in the churchyard; a smaller stone near by."""
    rng = np.random.default_rng(3)
    S = Site("rudston")
    M = S.mesh("monolith#far", "stone")
    standing_stone(rng, M, 0.0, 0.0, 1.75, 7.6, 0.85, az(170), bury=1.5, taper=0.8, boxy=(0.45, 0.55), res=(14, 20), chops=3, bump=0.06,
                   lean=(0.05, -0.08), bright=0.4, bare=0.9, lichen=0.55, moss=0.12)
    M = S.mesh("cist_stone#drop", "stone")
    standing_stone(rng, M, -3.4, 4.5, 0.9, 0.7, 0.6, 0.6, bury=0.3, taper=0.8, **GRIT)
    return S


def devils_arrows():
    """The Devil's Arrows: three gritstone monoliths in a line running north north west (6.9, 6.7 and
    5.5 m), their tops deeply grooved by the weather, the middle stone at (0, 0)."""
    rng = np.random.default_rng(4)
    S = Site("devils_arrows")
    stones = [("south", 54.0, 100.0, 6.9, 1.9, 1.1), ("middle", 0.0, 0.0, 6.7, 1.7, 1.2), ("north", -26.0, -55.7, 5.5, 1.8, 1.1)]
    for name, x, z, h, w, t in stones:
        M = S.mesh("arrow_%s#drop#far" % name, "stone")
        standing_stone(rng, M, x, z, w, h, t, az(155) + rng.normal(0, 0.15), bury=1.6, taper=0.72, boxy=(0.3, 0.4), res=(14, 22), chops=3, bump=0.04,
                       flutes=11, flute_depth=0.16, lean=(rng.normal(0, 0.08), rng.normal(0, 0.08)), bright=0.38, bare=0.8, lichen=0.6, moss=0.15)
    return S


SOOT = dict(bright=0.3, bare=1.0, lichen=0.05, moss=0.05)
DARK = (0.006, 0.006, 0.007)


def mas(rng, **kw):
    d = dict(SOOT); d.update(kw)
    return lambda n: masks(n, d["bright"], d["bare"], d["lichen"], d["moss"], rng, 0.025)


def wainhouse():
    """Wainhouse Tower, Halifax (1875): 84 m. An octagonal plinth 8 m across, the tapering octagonal shaft
    with string courses and slit windows up its stair, then the ornate top: a corbelled gallery with its
    balustrade at about 66 m, an arcaded octagonal belvedere, a second gallery, an open lantern of columns
    and the dome with its finial. Halifax sandstone, blackened by a century of mill smoke."""
    rng = np.random.default_rng(5)
    S = Site("wainhouse")
    M = S.mesh("tower#far", "masonry")
    W = S.mesh("windows", "paint")
    c = mas(rng)
    lite = mas(rng, bright=0.36)
    # Plinth: 0 to 7.5 m, with a moulded top, and its door on the south.
    oct8 = ngon(8, 4.25, math.pi / 8)
    prism(M, oct8, -4.0, 6.8, c)
    prism(M, ngon(8, 4.45, math.pi / 8), 6.8, 7.4, lite)
    panel(W, [0.0, 0.0, 3.95], [0, 0, 1], [1, 0, 0], 1.3, 2.8, DARK, arch=True)
    ring_panels(W, oct8, 3.6, 0.5, 1.4, DARK, every=2, skip=(2,), arch=True)
    # Shaft: 7.4 to 60 m, tapering 3.55 -> 2.85, string courses every 9 m.
    y = 7.4
    r0, r1, top = 3.55, 2.85, 60.0
    levels = [7.4, 16.0, 25.0, 34.0, 43.0, 52.0, 60.0]
    for k in range(len(levels) - 1):
        ya, yb = levels[k], levels[k + 1]
        ra = r0 + (r1 - r0) * (ya - y) / (top - y)
        rb = r0 + (r1 - r0) * (yb - y) / (top - y)
        prism(M, ngon(8, ra, math.pi / 8), ya, yb - 0.35, c, top_scale=(rb / ra), cap_top=False)
        prism(M, ngon(8, rb + 0.18, math.pi / 8), yb - 0.35, yb, lite, cap_top=False)
        # (slits up the stair, turning round the faces)
        ring = ngon(8, (ra + rb) * 0.5, math.pi / 8)
        a, b = np.array(ring[k % 8]), np.array(ring[(k + 1) % 8])
        m = (a + b) * 0.5; d = (b - a) / np.linalg.norm(b - a); nrm = np.array([d[1], -d[0]])
        if m @ nrm < 0:
            nrm = -nrm
        panel(W, [m[0], (ya + yb) * 0.5 - 0.8, m[1]], [nrm[0], 0, nrm[1]], [d[0], 0, d[1]], 0.35, 1.6, DARK)
    # Corbelling out to the first gallery: 60 to 65.5 m.
    prism(M, ngon(8, 2.85, math.pi / 8), 60.0, 63.0, c, top_scale=3.3 / 2.85, cap_top=False)
    for k in range(8):
        a = math.pi / 8 + 2 * math.pi * k / 8
        for j in range(3):
            block(M, (math.cos(a) * (3.3 + 0.15 * j), 63.2 + 0.5 * j, math.sin(a) * (3.3 + 0.15 * j)), (0.35, 0.25, 0.35), lite, a)
    prism(M, ngon(8, 4.25, math.pi / 8), 64.6, 65.5, lite, cap_bottom=True)
    # Balustrade round the gallery.
    gal = ngon(8, 4.1, math.pi / 8)
    for k in range(8):
        a, b = np.array(gal[k]), np.array(gal[(k + 1) % 8])
        L = np.linalg.norm(b - a); d = (b - a) / L
        for j in range(7):
            p = a + d * (L * (j + 0.5) / 7)
            block(M, (p[0], 65.95, p[1]), (0.08, 0.45, 0.08), lite)
        block(M, ((a + b)[0] * 0.5, 66.5, (a + b)[1] * 0.5), (L * 0.5 + 0.1, 0.1, 0.16), lite, math.atan2(d[1], d[0]))
        block(M, (a[0], 66.0, a[1]), (0.22, 0.6, 0.22), lite)
    # Belvedere: 65.5 to 72 m, octagon with arched openings.
    bel = ngon(8, 2.9, math.pi / 8)
    prism(M, bel, 65.5, 72.0, c, cap_top=False)
    ring_panels(W, bel, 67.0, 1.3, 3.6, DARK, arch=True)
    prism(M, ngon(8, 3.55, math.pi / 8), 72.0, 72.7, lite, cap_bottom=True)
    gal2 = ngon(8, 3.4, math.pi / 8)
    for k in range(8):
        a, b = np.array(gal2[k]), np.array(gal2[(k + 1) % 8])
        L = np.linalg.norm(b - a); d = (b - a) / L
        for j in range(5):
            p = a + d * (L * (j + 0.5) / 5)
            block(M, (p[0], 73.1, p[1]), (0.07, 0.4, 0.07), lite)
        block(M, ((a + b)[0] * 0.5, 73.55, (a + b)[1] * 0.5), (L * 0.5 + 0.08, 0.08, 0.13), lite, math.atan2(d[1], d[0]))
        block(M, (a[0], 73.2, a[1]), (0.18, 0.5, 0.18), lite)
    # Lantern: eight columns 72.7 to 78.5 m round an inner drum, an entablature, the dome and finial.
    prism(M, ngon(8, 1.25, math.pi / 8), 72.7, 78.6, c, cap_top=False)
    for k in range(8):
        a = math.pi / 8 + 2 * math.pi * k / 8
        cx, cz = math.cos(a) * 2.05, math.sin(a) * 2.05
        prism(M, [(cx + q[0], cz + q[1]) for q in ngon(8, 0.2, 0.0)], 72.7, 78.2, lite, cap_top=False)
        block(M, (cx, 78.35, cz), (0.32, 0.15, 0.32), lite, a)
    prism(M, ngon(8, 2.45, math.pi / 8), 78.5, 79.4, lite, cap_bottom=True)
    # Dome: rings of an octagon closing to a point (an ogee, taller than it is wide).
    yb, rb = 79.4, 2.25
    steps = 8
    for k in range(steps):
        t0, t1 = k / steps, (k + 1) / steps
        r_a = rb * (1.0 - t0 ** 1.4) * (1.0 + 0.12 * math.sin(t0 * math.pi)); r_b = rb * (1.0 - t1 ** 1.4) * (1.0 + 0.12 * math.sin(t1 * math.pi))
        y_a = yb + 3.4 * t0; y_b = yb + 3.4 * t1
        if r_b < 0.15:
            r_b = 0.15
        prism(M, ngon(8, r_a, math.pi / 8), y_a, y_b, lite, top_scale=r_b / r_a, cap_top=(k == steps - 1))
    prism(M, ngon(8, 0.12, math.pi / 8), 82.6, 84.0, lite, top_scale=0.3)
    return S


def hardy():
    """The Hardy Monument (1844) on Black Down: 22 m, an octagonal tower tapering like a telescope from a
    foot that flares out like a bell, a rib up each corner, a corbelled cornice and a plain parapet on
    top; its door on the south east. Weathered brown local stone."""
    rng = np.random.default_rng(6)
    S = Site("hardy")
    M = S.mesh("tower#far", "masonry")
    W = S.mesh("windows", "paint")
    c = mas(rng, bright=0.34, bare=1.0, lichen=0.25, moss=0.15)
    lite = mas(rng, bright=0.4, bare=1.0, lichen=0.3, moss=0.1)
    r0, r1 = 3.55, 2.45
    # (the foot flares out like a bell: steep at first, then nearly flat to the ground)
    prism(M, ngon(8, 5.4, math.pi / 8), -3.0, 0.4, lite, top_scale=5.25 / 5.4)
    flare = [(0.4, 5.25), (1.4, 4.75), (2.6, 4.2), (3.8, 3.85), (4.6, 3.62)]
    for (ya, ra), (yb, rb) in zip(flare, flare[1:]):
        prism(M, ngon(8, ra, math.pi / 8), ya, yb, c, top_scale=rb / ra, cap_top=False)
    prism(M, ngon(8, 3.62, math.pi / 8), 4.6, 19.0, c, top_scale=r1 / 3.62, cap_top=False)
    # Corner ribs: a thin buttress up each corner, tapering with the tower.
    for k in range(8):
        a = math.pi / 8 + 2 * math.pi * k / 8
        ca, sa = math.cos(a), math.sin(a)
        def rib(r, y, w=0.36, out=0.34):
            # (a square rib on the corner: its inner corner in the tower, standing out by out)
            return [(ca * (r - 0.1) - sa * w, y, sa * (r - 0.1) + ca * w), (ca * (r + out) - sa * w, y, sa * (r + out) + ca * w),
                    (ca * (r + out) + sa * w, y, sa * (r + out) - ca * w), (ca * (r - 0.1) + sa * w, y, sa * (r - 0.1) - ca * w)]
        hexa(M, rib(4.7, 1.4, 0.42, 0.3), rib(r1, 19.0, 0.3, 0.24), c)
    # Cornice, parapet and lantern.
    prism(M, ngon(8, r1 + 0.2, math.pi / 8), 19.0, 19.9, c, top_scale=(r1 + 0.6) / (r1 + 0.2), cap_bottom=True, cap_top=False)   # (the cornice, corbelled out)
    prism(M, ngon(8, r1 + 0.6, math.pi / 8), 19.9, 20.5, lite, cap_bottom=True)
    prism(M, ngon(8, r1 + 0.35, math.pi / 8), 20.5, 21.6, c)                    # (the parapet)
    prism(M, ngon(8, r1 + 0.45, math.pi / 8), 21.6, 22.0, lite, top_scale=0.92)
    # Door (south east face) and small windows up the stair.
    ring = ngon(8, 3.75, math.pi / 8)
    face = 1     # (the faces run from east; 1 is south east)
    a, b = np.array(ring[face]), np.array(ring[face + 1])
    m = (a + b) * 0.5; d = (b - a) / np.linalg.norm(b - a); nrm = np.array([d[1], -d[0]])
    if m @ nrm < 0:
        nrm = -nrm
    panel(W, [m[0], 0.4, m[1]], [nrm[0], 0, nrm[1]], [d[0], 0, d[1]], 1.1, 2.4, DARK, arch=True, proud=0.5)
    for k, y in enumerate((7.0, 11.5, 15.5)):
        rr = 3.62 + (r1 - 3.62) * (y - 4.6) / 14.4
        rg = ngon(8, rr, math.pi / 8)
        f = (face + 2 + 3 * k) % 8
        a, b = np.array(rg[f]), np.array(rg[(f + 1) % 8])
        m = (a + b) * 0.5; d = (b - a) / np.linalg.norm(b - a); nrm = np.array([d[1], -d[0]])
        if m @ nrm < 0:
            nrm = -nrm
        panel(W, [m[0], y, m[1]], [nrm[0], 0, nrm[1]], [d[0], 0, d[1]], 0.45, 1.2, DARK)
    # The railing round it.
    R = S.mesh("railing", "paint")
    for k in range(48):
        a = 2 * math.pi * k / 48
        block(R, (math.cos(a) * 7.0, 0.55, math.sin(a) * 7.0), (0.03, 0.6, 0.03), (0.02, 0.02, 0.02, 1.0))
    for y in (0.25, 1.05):
        for k in range(24):
            a0 = 2 * math.pi * k / 24; a1 = 2 * math.pi * (k + 1) / 24
            p0 = np.array([math.cos(a0), math.sin(a0)]) * 7.0; p1 = np.array([math.cos(a1), math.sin(a1)]) * 7.0
            m = (p0 + p1) * 0.5; d = p1 - p0
            block(R, (m[0], y, m[1]), (np.linalg.norm(d) * 0.5, 0.025, 0.025), (0.02, 0.02, 0.02, 1.0), math.atan2(d[1], d[0]))
    return S


def stoodley():
    """Stoodley Pike (1856), 37 m on the moor above Todmorden: a square pedestal with a battered foot and
    a dark entrance on the north, the balcony round it at about 12 m behind a parapet, then a tall
    tapering obelisk ending in a short pyramid. Millstone grit, weathered dark."""
    rng = np.random.default_rng(7)
    S = Site("stoodley")
    M = S.mesh("pike#far", "masonry")
    W = S.mesh("windows", "paint")
    c = mas(rng, bright=0.31, bare=1.0, lichen=0.3, moss=0.15)
    lite = mas(rng, bright=0.36, bare=1.0, lichen=0.3, moss=0.1)
    sq = lambda h: [(-h, -h), (h, -h), (h, h), (-h, h)]
    prism(M, sq(4.5), -3.0, 1.2, c, top_scale=4.1 / 4.5)                 # battered foot
    prism(M, sq(4.1), 1.2, 11.2, c, top_scale=3.6 / 4.1)                  # the lower stage, tapering
    prism(M, sq(4.2), 11.2, 11.7, lite, cap_bottom=True)                  # the balcony's ledge
    prism(M, [(-4.2, -4.2), (4.2, -4.2), (4.2, -3.95), (-4.2, -3.95)], 11.7, 12.6, c)   # its parapet, 4 sides
    prism(M, [(-4.2, 3.95), (4.2, 3.95), (4.2, 4.2), (-4.2, 4.2)], 11.7, 12.6, c)
    prism(M, [(-4.2, -3.95), (-3.95, -3.95), (-3.95, 3.95), (-4.2, 3.95)], 11.7, 12.6, c)
    prism(M, [(3.95, -3.95), (4.2, -3.95), (4.2, 3.95), (3.95, 3.95)], 11.7, 12.6, c)
    prism(M, sq(3.2), 11.7, 13.6, c)                                      # base of the obelisk
    prism(M, sq(3.35), 13.6, 14.0, lite)
    prism(M, sq(3.0), 14.0, 34.6, c, top_scale=1.55 / 3.0, cap_top=False)    # the obelisk
    prism(M, sq(1.75), 34.6, 35.0, lite)
    # Pyramid cap.
    P = np.array([[-1.6, 35.0, -1.6], [1.6, 35.0, -1.6], [1.6, 35.0, 1.6], [-1.6, 35.0, 1.6], [0, 37.0, 0]], float)
    T = np.array([[0, 1, 4], [1, 2, 4], [2, 3, 4], [3, 0, 4]])
    T = _outward(P, T, np.array([0, 35.5, 0]))
    M.add(P, T, lite(len(P)), P[:, [0, 1]] + P[:, [2, 2]])
    # The doorway on the north, and its lettered panel over it (a dark plate).
    panel(W, [0.0, 0.0, -4.45], [0, 0, -1], [-1, 0, 0], 1.4, 2.6, DARK, arch=True, proud=0.1)
    panel(W, [0.0, 9.5, -3.7], [0, 0, -1], [-1, 0, 0], 0.9, 0.5, DARK, proud=0.06)
    panel(W, [0.0, 11.9, -3.2], [0, 0, -1], [-1, 0, 0], 0.9, 1.4, DARK, arch=True)
    for nrm, rgt in (((1, 0, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, -1)), ((0, 0, 1), (1, 0, 0))):
        panel(W, [nrm[0] * 3.9, 5.5, nrm[2] * 3.9], nrm, rgt, 0.5, 1.1, DARK, proud=0.08)
    return S


SANDSTONE = dict(bright=0.52, bare=1.0, lichen=0.35, moss=0.12)


def cook():
    """Captain Cook's Monument (1827) on Easby Moor: a 15.5 m sandstone obelisk rising straight from a
    low stepped base, its inscribed tablet on the west face."""
    rng = np.random.default_rng(8)
    S = Site("cook")
    M = S.mesh("obelisk#far", "masonry")
    W = S.mesh("tablet", "paint")
    c = mas(rng, **SANDSTONE)
    lite = mas(rng, bright=0.58, bare=1.0, lichen=0.3, moss=0.08)
    sq = lambda h: [(-h, -h), (h, -h), (h, h), (-h, h)]
    prism(M, sq(1.75), -2.0, 0.3, lite)
    prism(M, sq(1.55), 0.3, 0.75, lite)
    prism(M, sq(1.35), 0.75, 14.4, c, top_scale=0.62 / 1.35, cap_top=False)
    P = np.array([[-0.62, 14.4, -0.62], [0.62, 14.4, -0.62], [0.62, 14.4, 0.62], [-0.62, 14.4, 0.62], [0, 15.5, 0]], float)
    T = _outward(P, np.array([[0, 1, 4], [1, 2, 4], [2, 3, 4], [3, 0, 4]]), np.array([0, 14.6, 0]))
    M.add(P, T, lite(len(P)), P[:, [0, 1]] + P[:, [2, 2]])
    panel(W, [-1.32, 2.2, 0.0], [-1, 0, 0], [0, 0, 1], 1.1, 0.9, (0.12, 0.1, 0.08), proud=0.03)
    return S


WHITE = (0.75, 0.75, 0.72)
RED_BAND = (0.36, 0.02, 0.015)
SLATE = (0.06, 0.065, 0.075)


def souter():
    """Souter Lighthouse (1871), the first built for electric light: a 23 m tower, white with a broad red
    band, its gallery and red lantern, rising out of the square engine house; the keepers' cottages
    round a yard to the west, whitewashed under grey slate."""
    rng = np.random.default_rng(9)
    S = Site("souter")
    Wh = S.mesh("tower#far", "render")
    Rf = S.mesh("roofs", "roof")
    Pt = S.mesh("lantern", "paint")
    Wn = S.mesh("windows", "paint")
    dirt = 0.25
    # The tower, round, 0 to 18.5 m; the band from 7 to 12.5 m.
    for (ya, yb, rgb) in ((-2.0, 0.6, (0.02, 0.05, 0.035)), (0.6, 7.0, WHITE), (7.0, 12.5, RED_BAND), (12.5, 18.4, WHITE)):
        ra = 3.3 - 0.45 * max(ya, 0) / 18.4; rb = 3.3 - 0.45 * yb / 18.4
        prism(Wh, ngon(20, ra, 0.0), ya, yb, (rgb[0], rgb[1], rgb[2], dirt), top_scale=rb / ra, cap_top=(yb > 18))
    # Gallery and railing, the lantern's glazed drum and its black dome.
    prism(Pt, ngon(20, 3.35, 0.0), 18.4, 18.75, (0.03, 0.03, 0.03, 1.0), cap_bottom=True)
    for k in range(32):
        a = 2 * math.pi * k / 32
        block(Pt, (math.cos(a) * 3.25, 19.3, math.sin(a) * 3.25), (0.025, 0.55, 0.025), (0.02, 0.02, 0.02, 1.0))
    prism(Pt, ngon(20, 3.27, 0.0), 19.8, 19.9, (0.02, 0.02, 0.02, 1.0), cap_top=False)
    prism(Wh, ngon(16, 1.95, 0.0), 18.75, 19.5, (RED_BAND[0], RED_BAND[1], RED_BAND[2], 0.2))
    prism(Pt, ngon(16, 1.9, 0.0), 19.5, 21.4, (0.03, 0.04, 0.045, 1.0))
    for k in range(16):
        a = 2 * math.pi * k / 16
        block(Pt, (math.cos(a) * 1.92, 20.45, math.sin(a) * 1.92), (0.04, 0.95, 0.04), (RED_BAND[0], RED_BAND[1], RED_BAND[2], 1.0), a)
    for k in range(5):
        t0, t1 = k / 5, (k + 1) / 5
        ra = 2.05 * math.cos(t0 * math.pi / 2) + 0.05; rb = 2.05 * math.cos(t1 * math.pi / 2) + 0.05
        prism(Pt, ngon(16, ra, 0.0), 21.4 + 1.1 * math.sin(t0 * math.pi / 2), 21.4 + 1.1 * math.sin(t1 * math.pi / 2), (RED_BAND[0], RED_BAND[1], RED_BAND[2], 1.0), top_scale=rb / ra)
    prism(Pt, ngon(8, 0.12, 0.0), 22.5, 23.0, (0.02, 0.02, 0.02, 1.0))
    # Windows up the tower, on the sea side and the land side.
    for y in (3.5, 9.0, 14.5):
        rr = 3.3 - 0.45 * y / 18.4
        for a in (0.0, math.pi):
            n = np.array([math.cos(a), 0, math.sin(a)]); r = np.array([-math.sin(a), 0, math.cos(a)])
            panel(Wn, n * rr + np.array([0, y, 0]), n, r, 0.6, 1.2, DARK, proud=0.06)
    # The engine house the tower rises from (square, two storeys, flat roofed with a parapet), and the
    # cottages: ranges round a yard to the west, with pitched slate roofs and chimneys.
    def house(x0, x1, z0, z1, eaves, ridge_along_x, chimneys=2):
        prism(Wh, [(x0, z0), (x1, z0), (x1, z1), (x0, z1)], -2.0, eaves, (WHITE[0], WHITE[1], WHITE[2], dirt), cap_top=False)
        cx, cz = 0.5 * (x0 + x1), 0.5 * (z0 + z1)
        if ridge_along_x:
            half = 0.5 * (z1 - z0) + 0.3
            rise = half * 0.75
            P = np.array([[x0 - 0.3, eaves, z0 - 0.3], [x1 + 0.3, eaves, z0 - 0.3], [x1 + 0.3, eaves + rise, cz], [x0 - 0.3, eaves + rise, cz],
                          [x0 - 0.3, eaves, z1 + 0.3], [x1 + 0.3, eaves, z1 + 0.3]], float)
            T = np.array([[0, 2, 1], [0, 3, 2], [4, 5, 2], [4, 2, 3]])
            gab = [[[x0, eaves, z0], [x0, eaves, z1], [x0, eaves + rise - 0.2, cz]], [[x1, eaves, z0], [x1, eaves, z1], [x1, eaves + rise - 0.2, cz]]]
        else:
            half = 0.5 * (x1 - x0) + 0.3
            rise = half * 0.75
            P = np.array([[x0 - 0.3, eaves, z0 - 0.3], [x0 - 0.3, eaves, z1 + 0.3], [cx, eaves + rise, z1 + 0.3], [cx, eaves + rise, z0 - 0.3],
                          [x1 + 0.3, eaves, z0 - 0.3], [x1 + 0.3, eaves, z1 + 0.3]], float)
            T = np.array([[0, 2, 1], [0, 3, 2], [4, 5, 2], [4, 2, 3]])
            gab = [[[x0, eaves, z0], [x1, eaves, z0], [cx, eaves + rise - 0.2, z0]], [[x0, eaves, z1], [x1, eaves, z1], [cx, eaves + rise - 0.2, z1]]]
        T = _outward(P, T, np.array([cx, eaves - 1.0, cz]))
        Rf.add(P, T, colour(len(P), SLATE, 0.0), np.stack([P[:, 0] + P[:, 2], P[:, 1] * 1.25], 1))
        for g in gab:
            G = np.array(g, float)
            Tg = _outward(G, np.array([[0, 1, 2]]), np.array([cx, eaves, cz]))
            Wh.add(G, Tg, colour(3, WHITE, dirt), G[:, [0, 1]] + G[:, [2, 2]])
        for k in range(chimneys):
            t = (k + 0.5) / chimneys
            px = x0 + (x1 - x0) * t if ridge_along_x else cx
            pz = cz if ridge_along_x else z0 + (z1 - z0) * t
            block(Wh, (px, eaves + rise + 0.5, pz), (0.45, 0.9, 0.35), (0.05, 0.05, 0.05, 0.4))
        # windows: along both long sides, ground and first floor where tall enough
        floors = [0.9] + ([3.7] if eaves > 5 else [])
        if ridge_along_x:
            for xw in np.arange(x0 + 1.8, x1 - 1.0, 3.2):
                for y in floors:
                    panel(Wn, [xw, y, z1], [0, 0, 1], [1, 0, 0], 0.9, 1.5, DARK)
                    panel(Wn, [xw, y, z0], [0, 0, -1], [-1, 0, 0], 0.9, 1.5, DARK)
        else:
            for zw in np.arange(z0 + 1.8, z1 - 1.0, 3.2):
                for y in floors:
                    panel(Wn, [x1, y, zw], [1, 0, 0], [0, 0, -1], 0.9, 1.5, DARK)
                    panel(Wn, [x0, y, zw], [-1, 0, 0], [0, 0, 1], 0.9, 1.5, DARK)

    # engine house, west of the tower, flat roofed
    prism(Wh, [(-9.5, -4.5), (-2.0, -4.5), (-2.0, 4.5), (-9.5, 4.5)], -2.0, 6.5, (WHITE[0], WHITE[1], WHITE[2], dirt))
    for ring in ([(-9.6, -4.6), (-1.9, -4.6), (-1.9, -4.25), (-9.6, -4.25)], [(-9.6, 4.25), (-1.9, 4.25), (-1.9, 4.6), (-9.6, 4.6)],
                 [(-9.6, -4.25), (-9.25, -4.25), (-9.25, 4.25), (-9.6, 4.25)]):
        prism(Wh, ring, 6.5, 7.1, (WHITE[0], WHITE[1], WHITE[2], 0.3))
    for zw in (-2.5, 0.0, 2.5):
        panel(Wn, [-9.5, 2.2, zw], [-1, 0, 0], [0, 0, 1], 1.0, 2.2, DARK, arch=True)
    for xw in (-7.5, -4.5):
        panel(Wn, [xw, 2.2, 4.5], [0, 0, 1], [1, 0, 0], 1.0, 2.2, DARK, arch=True)
        panel(Wn, [xw, 2.2, -4.5], [0, 0, -1], [-1, 0, 0], 1.0, 2.2, DARK, arch=True)
    # the ranges round the yard (yard about x -30..-12, z -12..14)
    house(-34.0, -11.0, -18.0, -12.0, 5.6, True, 3)     # north range, two storey
    house(-34.0, -11.0, 14.0, 20.0, 5.6, True, 3)       # south range
    house(-36.0, -30.0, -12.0, 14.0, 3.4, False, 2)     # west range, one storey
    house(-11.0, -9.5, -12.0, -4.5, 3.0, False, 1)      # link
    house(-11.0, -9.5, 4.5, 14.0, 3.0, False, 1)
    # the yard wall's gate piers and the low wall to the sea
    for x in (-30.0, -12.0):
        block(Wh, (x, 0.7, 20.0 + 3.0), (0.3, 1.3, 0.3), (WHITE[0], WHITE[1], WHITE[2], 0.4))
    return S


LIMESTONE = dict(bright=0.46, bare=0.3, lichen=0.45, moss=0.12)


def bolton():
    """Bolton Castle, Wensleydale (1399): a quadrangle about 57 by 42 m round a courtyard, four ranges
    some 15 m high, a great tower at each corner rising to about 28 m and a smaller turret in the middle
    of the north and south sides; the north east tower fell in 1761 and stands as a broken shell. Grey
    rubble limestone, small windows low down, larger ones above, battlements on top. The long side runs
    east by north (the map's outline); the model is made along x and turned by the site note."""
    rng = np.random.default_rng(10)
    S = Site("bolton")
    M = S.mesh("castle#far", "masonry")
    W = S.mesh("windows", "paint")
    c = mas(rng, **LIMESTONE)
    lite = mas(rng, bright=0.5, bare=0.25, lichen=0.5, moss=0.1)
    X, Z = 21.0, 15.0          # half the ranges' outer size (towers stand out past it: 57 by 42 m in all)
    D = 7.0                    # range depth
    H = 15.5                   # range height
    TH = 27.0                  # tower height
    TX, TZ = 6.5, 5.5          # towers' half sizes
    FOUND = -6.0

    def wall(x0, x1, z0, z1, y0, y1, cols=c):
        prism(M, [(x0, z0), (x1, z0), (x1, z1), (x0, z1)], y0, y1, cols)

    def battle_box(x0, x1, z0, z1, y):
        for a, b in (((x0, z0), (x1, z0)), ((x1, z0), (x1, z1)), ((x1, z1), (x0, z1)), ((x0, z1), (x0, z0))):
            merlons(M, a, b, y, lite, w=1.0, gap=0.75, h=0.9, t=0.6)
        prism(M, [(x0 - 0.25, z0 - 0.25), (x1 + 0.25, z0 - 0.25), (x1 + 0.25, z1 + 0.25), (x0 - 0.25, z1 + 0.25)], y - 0.45, y, lite, cap_top=False, cap_bottom=True)

    def windows_on(x0, x1, z, normal, ys, step=6.0, w=0.8, h=1.4, arch=False):
        for x in np.arange(x0 + step * 0.5, x1, step):
            for y in ys:
                panel(W, [x, y, z], [0, 0, normal], [normal, 0, 0], w, h, DARK, arch=arch)

    def windows_on_x(z0, z1, x, normal, ys, step=6.0, w=0.8, h=1.4):
        for zz in np.arange(z0 + step * 0.5, z1, step):
            for y in ys:
                panel(W, [x, y, zz], [normal, 0, 0], [0, 0, -normal], w, h, DARK)

    # The four ranges.
    wall(-X, X, -Z, -Z + D, FOUND, H)          # north
    wall(-X, X, Z - D, Z, FOUND, H)            # south
    wall(-X, -X + D, -Z + D, Z - D, FOUND, H)  # west
    wall(X - D, X, -Z + D, Z - D, FOUND, H)    # east
    battle_box(-X, X - TX * 2, -Z, -Z + D, H)  # (the north range's battlements stop at the ruined tower)
    battle_box(-X, X, Z - D, Z, H)
    battle_box(-X, -X + D, -Z, Z, H)
    battle_box(X - D, X, -Z + D * 1.5, Z, H)
    # The corner towers: three whole, the north east a ruin.
    for sx, sz in ((-1, -1), (-1, 1), (1, 1)):
        cx, cz = sx * (X + 1.0), sz * (Z + 0.5)
        wall(cx - TX, cx + TX, cz - TZ, cz + TZ, FOUND, TH)
        battle_box(cx - TX, cx + TX, cz - TZ, cz + TZ, TH)
        # a stair turret on its corner standing higher
        tx, tz = cx + sx * (TX - 1.6), cz + sz * (TZ - 1.6)
        wall(tx - 1.6, tx + 1.6, tz - 1.6, tz + 1.6, TH - 1.0, TH + 3.2)
        battle_box(tx - 1.6, tx + 1.6, tz - 1.6, tz + 1.6, TH + 3.2)
    # North east tower: two broken walls standing to different heights, the rest low rubble.
    cx, cz = X + 1.0, -(Z + 0.5)
    wall(cx - TX, cx + TX, cz - TZ, cz + TZ, FOUND, 3.0)
    wall(cx - TX, cx - TX + 1.6, cz - TZ, cz + TZ, 3.0, 22.0)            # west wall standing tall
    wall(cx - TX, cx - TX + 1.6, cz - TZ, cz - 1.0, 22.0, 25.0)
    wall(cx - TX, cx + 2.0, cz + TZ - 1.6, cz + TZ, 3.0, 17.0)           # south wall, broken off
    wall(cx - TX, cx - 2.5, cz + TZ - 1.6, cz + TZ, 17.0, 20.5)
    rr = np.random.default_rng(11)
    for k in range(12):
        bx, bz = cx + rr.uniform(-5, 5), cz + rr.uniform(-4, 4)
        block(M, (bx, 3.0 + rr.uniform(0, 1.2), bz), (rr.uniform(0.6, 1.6), rr.uniform(0.4, 1.0), rr.uniform(0.6, 1.4)), c, rr.uniform(0, 3))
    # The ruined tower's inside faces seen through the gap: dark window holes in the standing walls.
    for y in (6.0, 11.0, 16.0):
        panel(W, [cx - TX + 1.62, y, cz], [1, 0, 0], [0, 0, -1], 1.0, 1.8, DARK)
    # The middle turrets on the north and south faces.
    for sz in (-1, 1):
        cz2 = sz * (Z + 1.5)
        wall(-3.0, 3.0, cz2 - 2.0, cz2 + 2.0, FOUND, H + 4.0)
        battle_box(-3.0, 3.0, cz2 - 2.0, cz2 + 2.0, H + 4.0)
    # Windows: small low down, larger and paired higher, on every outer face; the towers' too.
    windows_on(-X + 8, X - 8, Z, 1, (4.0,), step=7.0, w=0.5, h=1.0)
    windows_on(-X + 8, X - 8, Z, 1, (8.5, 12.0), step=5.0, w=1.0, h=1.8)
    windows_on(-X + 8, X - 16, -Z, -1, (4.0,), step=7.0, w=0.5, h=1.0)
    windows_on(-X + 8, X - 16, -Z, -1, (8.5, 12.0), step=5.0, w=1.0, h=1.8)
    windows_on_x(-Z + 7, Z - 7, -X, -1, (5.0, 9.0, 12.5), step=5.0, w=0.9, h=1.6)
    windows_on_x(-Z + 9, Z - 7, X, 1, (5.0, 9.0, 12.5), step=5.0, w=0.9, h=1.6)
    for sx, sz in ((-1, -1), (-1, 1), (1, 1)):
        cx, cz = sx * (X + 1.0), sz * (Z + 0.5)
        for y in (6.0, 11.0, 16.0, 21.0):
            panel(W, [cx, y, cz + sz * TZ], [0, 0, sz], [sz, 0, 0], 0.9, 1.6, DARK)
            panel(W, [cx + sx * TX, y, cz], [sx, 0, 0], [0, 0, -sx], 0.9, 1.6, DARK)
    # The gate into the courtyard (east range, north end) and the courtyard's inner faces' windows.
    panel(W, [X, 0.0, -4.0], [1, 0, 0], [0, 0, -1], 3.0, 4.2, DARK, arch=True)
    for zz in np.arange(-Z + D + 3, Z - D - 1, 5.0):
        panel(W, [-X + D, 7.0, zz], [1, 0, 0], [0, 0, -1], 1.0, 1.8, DARK)
        panel(W, [X - D, 7.0, zz], [-1, 0, 0], [0, 0, 1], 1.0, 1.8, DARK)
    for xx in np.arange(-X + D + 3, X - D - 1, 5.0):
        panel(W, [xx, 7.0, -Z + D], [0, 0, 1], [1, 0, 0], 1.0, 1.8, DARK)
        panel(W, [xx, 7.0, Z - D], [0, 0, -1], [-1, 0, 0], 1.0, 1.8, DARK)
    return S


# --- [towns stage 7] The cities' landmarks: the first of the towns plan's level C ------------------------

def rod_between(M, a, b, r, rgb, sides=6):
    """A round rod from a to b (x, y, z), r thick, coloured rgb (the paint layer)."""
    P, T = ST.tube([list(a), list(b)], r, sides)
    U = np.stack([P[:, 0] + P[:, 2], P[:, 1]], 1)
    M.add(P, T, colour(len(P), rgb, 1.0), U)


CORTEN = (0.16, 0.06, 0.025)


def angel():
    """The Angel of the North, Gateshead (Antony Gormley, 1998): a figure of weathering steel 20 m tall on a
    concrete foot sunk 20 m into the hill, its wings 54 m across held straight out and tilted 3.5 degrees
    forward, each a hollow shell braced by ribs that show as horizontal fins down its length. It stands on
    a grassy mound by the A1 and faces south south west, over the road (+z here, turned by the note)."""
    S = Site("angel")
    M = S.mesh("figure#far", "paint")
    c = CORTEN
    # (the plinth: a low concrete drum in the mound)
    prism(M, ngon(16, 3.2), -3.0, 0.6, (0.32, 0.31, 0.29))
    # (the body: legs together, a long tapering form ribbed every 0.5 m, front to back thinner)
    levels = [(0.6, 1.0, 0.9), (4.0, 1.25, 1.0), (8.0, 1.55, 1.15), (11.0, 1.75, 1.2), (13.5, 2.1, 1.35),
              (15.5, 2.05, 1.3), (17.2, 1.4, 1.0), (17.8, 0.55, 0.6)]
    for (y0, w0, d0), (y1, w1, d1) in zip(levels, levels[1:]):
        hexa(M, [(-w0 / 2, y0, -d0 / 2), (w0 / 2, y0, -d0 / 2), (w0 / 2, y0, d0 / 2), (-w0 / 2, y0, d0 / 2)],
             [(-w1 / 2, y1, -d1 / 2), (w1 / 2, y1, -d1 / 2), (w1 / 2, y1, d1 / 2), (-w1 / 2, y1, d1 / 2)], c)
    for y in np.arange(1.0, 17.0, 0.5):
        w = np.interp(y, [l[0] for l in levels], [l[1] for l in levels]) + 0.12
        d = np.interp(y, [l[0] for l in levels], [l[2] for l in levels]) + 0.12
        prism(M, [(-w / 2, -d / 2), (w / 2, -d / 2), (w / 2, d / 2), (-w / 2, d / 2)], y, y + 0.06, tuple(np.array(c) * 0.8))
    # (the head: a smooth rounded block, no face)
    hexa(M, [(-0.45, 17.8, -0.4), (0.45, 17.8, -0.4), (0.45, 17.8, 0.45), (-0.45, 17.8, 0.45)],
         [(-0.4, 20.0, -0.35), (0.4, 20.0, -0.35), (0.4, 20.0, 0.4), (-0.4, 20.0, 0.4)], c)
    # (the wings: from the shoulders out 26 m each way, 4 m deep at the body narrowing to 2.2 m, 0.5 m
    # thick, the whole tilted forward (towards +z) 3.5 degrees, ribs across at 0.6 m)
    tilt = math.tan(math.radians(3.5))
    for sx in (-1, 1):
        x0, x1 = sx * 0.9, sx * 27.0
        def at(x, y, z):
            return (x, y, z + (y - 13.0) * tilt)
        top0, bot0, top1, bot1 = 16.0, 12.0, 15.3, 13.1
        hexa(M, [at(x0, bot0, -0.3), at(x1, bot1, -0.25), at(x1, bot1, 0.25), at(x0, bot0, 0.3)],
             [at(x0, top0, -0.3), at(x1, top1, -0.25), at(x1, top1, 0.25), at(x0, top0, 0.3)], c)
        for x in np.arange(1.5, 26.6, 0.6):
            xx = sx * x
            f = (x - 0.9) / 26.1
            yb, yt = bot0 + (bot1 - bot0) * f, top0 + (top1 - top0) * f
            hexa(M, [at(xx - 0.04, yb - 0.05, -0.36), at(xx + 0.04, yb - 0.05, -0.36), at(xx + 0.04, yb - 0.05, 0.36), at(xx - 0.04, yb - 0.05, 0.36)],
                 [at(xx - 0.04, yt + 0.05, -0.36), at(xx + 0.04, yt + 0.05, -0.36), at(xx + 0.04, yt + 0.05, 0.36), at(xx - 0.04, yt + 0.05, 0.36)],
                 tuple(np.array(c) * 0.85))
    return S


TOWER_RED = (0.11, 0.02, 0.012)
TERRACOTTA = (0.30, 0.10, 0.05)


def blackpool():
    """Blackpool Tower (1894): an open lattice of steel painted red, 158 m to its flagpole, rising from the
    roof of the Tower Buildings (red brick and terracotta, about 37 m high round the tower's four legs), its
    legs 31 m apart where they leave the roof, curving in to a 9 m square under the observation decks at
    about 117 m, a glazed deck, a further taper to the crow's nest and the flagpole. Built along x, the
    promenade front to +z."""
    S = Site("blackpool")
    B = S.mesh("buildings#far", "brick")
    W = S.mesh("windows", "paint")
    L = S.mesh("lattice#far", "paint")
    G = S.mesh("lights", "glow")
    # (the Tower Buildings: a block 64 by 54 m, 25 m to its cornice, the tower's base rising to 37 m)
    brick = (0.24, 0.07, 0.045)
    prism(B, [(-32, -27), (32, -27), (32, 27), (-32, 27)], -4.0, 25.0, brick)
    prism(B, [(-33, -28), (33, -28), (33, 28), (-33, 28)], 25.0, 26.2, TERRACOTTA)
    prism(B, [(-18, -18), (18, -18), (18, 18), (-18, 18)], 26.2, 37.0, brick)
    prism(B, [(-19, -19), (19, -19), (19, 19), (-19, 19)], 37.0, 38.0, TERRACOTTA)
    for sx in (-1, 1):
        for sz in (-1, 1):   # (corner turrets of the base)
            prism(B, ngon(8, 2.2), 26.2, 41.0, brick, shift=(0, 0))
            B.P[-1][:, 0] += sx * 17.0; B.P[-1][:, 2] += sz * 17.0
    for face, (n, r) in {"s": ((0, 0, 1), (1, 0, 0)), "n": ((0, 0, -1), (-1, 0, 0))}.items():
        z = 27.0 if face == "s" else -27.0
        for x in np.arange(-29.0, 29.5, 3.6):
            for y in (2.5, 8.0, 13.5, 19.0):
                panel(W, [x, y, z], n, r, 1.5, 3.2 if y > 2.5 else 4.0, DARK, arch=y > 15)
    for x0, n, r in ((32.0, (1, 0, 0), (0, 0, -1)), (-32.0, (-1, 0, 0), (0, 0, 1))):
        for zz in np.arange(-24.0, 24.5, 3.6):
            for y in (8.0, 13.5, 19.0):
                panel(W, [x0, y, zz], n, r, 1.5, 3.2, DARK)
    # (the lattice: four legs curving in, braced with Xs on every face, horizontal girders between)
    def half(y):
        if y <= 117.0:
            t = (y - 37.0) / 80.0
            return 15.5 - 11.0 * (1 - (1 - t) ** 1.6)
        return 4.5 - 2.5 * (y - 117.0) / 23.0
    ys = list(np.arange(37.0, 117.1, 6.0)) + [117.0, 123.0, 129.0, 135.0, 140.0]
    ys = sorted(set(round(y, 2) for y in ys))
    corners = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
    for k in range(len(ys) - 1):
        ya, yb = ys[k], ys[k + 1]
        ha, hb = half(ya), half(yb)
        r = 0.55 if ya < 80 else 0.35
        for sx, sz in corners:
            rod_between(L, (sx * ha, ya, sz * ha), (sx * hb, yb, sz * hb), r, TOWER_RED, 6)
        for i in range(4):
            (ax, az_), (bx, bz) = corners[i], corners[(i + 1) % 4]
            rod_between(L, (ax * ha, ya, az_ * ha), (bx * hb, yb, bz * hb), 0.14, TOWER_RED, 4)
            rod_between(L, (bx * ha, ya, bz * ha), (ax * hb, yb, az_ * hb), 0.14, TOWER_RED, 4)
            rod_between(L, (ax * hb, yb, az_ * hb), (bx * hb, yb, bz * hb), 0.22, TOWER_RED, 4)
    # (the decks: a glazed box at 117 to 123 m, a balcony round it, the cupola and the crow's nest, the pole)
    prism(L, [(-5.2, -5.2), (5.2, -5.2), (5.2, 5.2), (-5.2, 5.2)], 116.4, 117.0, (0.6, 0.58, 0.52))
    prism(L, [(-4.5, -4.5), (4.5, -4.5), (4.5, 4.5), (-4.5, 4.5)], 117.0, 123.0, (0.05, 0.06, 0.07))
    prism(L, [(-4.7, -4.7), (4.7, -4.7), (4.7, 4.7), (-4.7, 4.7)], 123.0, 124.0, TOWER_RED)
    prism(L, ngon(8, 2.3), 140.0, 143.0, TOWER_RED)
    prism(L, ngon(8, 2.6), 143.0, 144.0, (0.6, 0.58, 0.52))
    prism(L, ngon(8, 2.0), 144.0, 147.0, TOWER_RED, top_scale=0.4)
    rod_between(L, (0, 147.0, 0), (0, 158.0, 0), 0.12, (0.6, 0.6, 0.58), 6)
    # (after dark: the decks lit, and lights up the legs, as the tower is famous for)
    for zz, n in ((4.55, 1), (-4.55, -1)):
        prism(G, [(-4.3, zz - 0.02 * n), (4.3, zz - 0.02 * n), (4.3, zz), (-4.3, zz)], 117.4, 122.6, (0.55, 0.42, 0.25))
    for k in range(len(ys) - 1):
        for sx, sz in corners:
            h = half(ys[k])
            block(G, (sx * h, ys[k] + 0.6, sz * h), (0.35, 0.35, 0.35), (0.6, 0.35, 0.2))
    return S


PORTLAND_PALE = dict(bright=0.62, bare=0.2, lichen=0.15, moss=0.04)


def liver():
    """The Royal Liver Building, Liverpool (1911): one of the first great reinforced concrete buildings,
    faced in grey granite; ten storeys, about 90 m along the river front by 50 m deep, 50 m to its main
    cornice, with a clock tower rising from each end of the river front to the Liver Birds, 98 m up. Each
    tower steps in over two stages with columns, carries four clock faces 7.6 m across (the largest in
    Britain) and a domed, ribbed cupola with the bird on top. Built along x, the river front to +z
    (turned by the note)."""
    rng = np.random.default_rng(31)
    S = Site("liver")
    M = S.mesh("block#far", "masonry")
    W = S.mesh("windows", "paint")
    G = S.mesh("lights", "glow")
    R = S.mesh("domes", "roof")
    c = mas(rng, **PORTLAND_PALE)
    dark = mas(rng, bright=0.42, bare=0.2, lichen=0.1, moss=0.03)
    X, Z = 45.0, 25.0
    prism(M, [(-X, -Z), (X, -Z), (X, Z), (-X, Z)], -4.0, 50.0, c)
    prism(M, [(-X - 0.8, -Z - 0.8), (X + 0.8, -Z - 0.8), (X + 0.8, Z + 0.8), (-X - 0.8, Z + 0.8)], 50.0, 51.5, dark)
    prism(M, [(-X, -Z), (X, -Z), (X, Z), (-X, Z)], 51.5, 54.0, c)
    prism(M, [(-X - 0.5, -Z - 0.5), (X + 0.5, -Z - 0.5), (X + 0.5, Z + 0.5), (-X - 0.5, Z + 0.5)], 6.0, 7.0, dark)
    # (the windows: ten storeys of them on every face, a giant order of arched ones at the foot)
    for z, n, r in ((Z, (0, 0, 1), (1, 0, 0)), (-Z, (0, 0, -1), (-1, 0, 0))):
        for x in np.arange(-X + 2.5, X - 1.0, 3.2):
            panel(W, [x, 1.0, z], n, r, 1.8, 4.5, DARK, arch=True)
            for y in np.arange(8.5, 48.0, 4.2):
                panel(W, [x, y, z], n, r, 1.5, 2.4, DARK)
    for x0, n, r in ((X, (1, 0, 0), (0, 0, -1)), (-X, (-1, 0, 0), (0, 0, 1))):
        for zz in np.arange(-Z + 2.5, Z - 1.0, 3.2):
            for y in np.arange(8.5, 48.0, 4.2):
                panel(W, [x0, y, zz], n, r, 1.5, 2.4, DARK)
    # (the two clock towers on the river front's ends: x = +/-30 by the front, z near +Z)
    for tx in (-30.0, 30.0):
        tz = Z - 9.0
        def sq(h):
            return [(tx - h, tz - h), (tx + h, tz - h), (tx + h, tz + h), (tx - h, tz + h)]
        prism(M, sq(9.0), 54.0, 63.0, c)
        prism(M, sq(9.4), 63.0, 64.0, dark)
        # (the clock stage, its four faces)
        prism(M, sq(7.5), 64.0, 74.0, c)
        for (nx, nz) in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            cx, cz = tx + nx * 7.55, tz + nz * 7.55
            ring = [(cx + (math.cos(a) * 3.8 if nz else 0.0), 69.0 + math.sin(a) * 3.8, cz + (math.cos(a) * 3.8 if nx else 0.0)) for a in np.linspace(0, 2 * math.pi, 25)[:-1]]
            P = np.array([[cx, 69.0, cz]] + [list(q) for q in ring])
            T = np.array([[0, 1 + k, 1 + (k + 1) % 24] for k in range(24)])
            T = _outward(P, T, np.array([tx, 69.0, tz]))
            W.add(P, T, colour(len(P), (0.55, 0.55, 0.52)), P[:, [0, 1]])
            G.add(P + np.array([nx, 0, nz]) * 0.03, T, colour(len(P), (0.55, 0.5, 0.35)), P[:, [0, 1]])
        prism(M, sq(7.9), 74.0, 75.0, dark)
        # (the colonnaded stage and the cupola)
        prism(M, ngon(8, 5.4, math.pi / 8), 75.0, 82.0, c)
        M.P[-1][:, 0] += tx; M.P[-1][:, 2] += tz
        for k in range(8):
            a = math.pi / 8 + k * math.pi / 4
            prism(M, ngon(6, 0.5), 75.0, 82.0, dark)
            M.P[-1][:, 0] += tx + math.cos(a) * 6.6; M.P[-1][:, 2] += tz + math.sin(a) * 6.6
        prism(M, ngon(8, 6.9, math.pi / 8), 82.0, 83.0, dark)
        M.P[-1][:, 0] += tx; M.P[-1][:, 2] += tz
        for k in range(8):   # (the dome in rings, ribbed slate-green)
            y0, y1 = 83.0 + k * 1.2, 83.0 + (k + 1) * 1.2
            r0, r1 = 6.0 * math.cos(math.radians(k * 11)), 6.0 * math.cos(math.radians((k + 1) * 11))
            prism(R, ngon(16, r0), y0, y1, (0.06, 0.13, 0.11, 1.0), top_scale=r1 / r0)
            R.P[-1][:, 0] += tx; R.P[-1][:, 2] += tz
        # (the bird: a cormorant-like form in green copper, its wings half raised)
        bird = (0.09, 0.2, 0.16)
        Bd = S.mesh("birds#far", "paint")
        hexa(Bd, [(tx - 0.5, 92.6, tz - 1.2), (tx + 0.5, 92.6, tz - 1.2), (tx + 0.5, 92.6, tz + 1.2), (tx - 0.5, 92.6, tz + 1.2)],
             [(tx - 0.4, 96.0, tz - 0.6), (tx + 0.4, 96.0, tz - 0.6), (tx + 0.4, 96.0, tz + 0.6), (tx - 0.4, 96.0, tz + 0.6)], bird)
        block(Bd, (tx, 96.7, tz - 0.9), (0.25, 0.45, 0.35), bird)
        for sx in (-1, 1):
            hexa(Bd, [(tx + sx * 0.4, 94.5, tz - 0.4), (tx + sx * 2.6, 96.5, tz - 0.6), (tx + sx * 2.6, 96.5, tz + 0.2), (tx + sx * 0.4, 94.5, tz + 0.6)],
                 [(tx + sx * 0.4, 95.0, tz - 0.4), (tx + sx * 2.6, 97.8, tz - 0.6), (tx + sx * 2.6, 97.8, tz + 0.2), (tx + sx * 0.4, 95.0, tz + 0.6)], bird)
        prism(M, ngon(8, 1.3), 91.8, 92.6, dark)
        M.P[-1][:, 0] += tx; M.P[-1][:, 2] += tz
    return S


MAGNESIAN = dict(bright=0.66, bare=0.35, lichen=0.2, moss=0.05)
LEAD = (0.11, 0.12, 0.13, 1.0)


def minster():
    """York Minster (1220 to 1472): the largest Gothic cathedral north of the Alps, in cream Magnesian
    limestone. 158 m long west to east; the nave and choir 30 m high to their vaults, under lead roofs to
    about 39 m, with aisles either side (32 m across in all); the transepts 76 m north to south; the
    central tower 20 m square and 71 m high; the two west towers 60 m with their pinnacles. Buttresses
    and pinnacles down every side, the great windows: the Great East Window (23.7 by 9.5 m), the west
    window, the Five Sisters in the north transept and the Rose Window in the south. Built along x, east to
    +x; the map's axis runs about 16 degrees north of east (turned by the note)."""
    rng = np.random.default_rng(47)
    S = Site("minster")
    c = mas(rng, **MAGNESIAN)
    lite = mas(rng, bright=0.74, bare=0.35, lichen=0.15, moss=0.03)

    def section(name, x0, x1, z0, z1, aisle, wall_h, ridge, ax="x"):
        """A main vessel from x0..x1 (or z), its aisles 'aisle' wide each side, clerestory to wall_h, its
        lead roof to ridge; buttresses and pinnacles along the aisles, windows in the bays."""
        M = S.mesh(name + "#far", "masonry")
        R = S.mesh(name + "_roof#far", "roof")
        W = S.mesh(name + "_windows", "paint")
        if ax == "x":
            prism(M, [(x0, z0), (x1, z0), (x1, z1), (x0, z1)], -3.0, wall_h, c)
            prism(M, [(x0, z0 - aisle), (x1, z0 - aisle), (x1, z0), (x0, z0)], -3.0, wall_h * 0.5, c)
            prism(M, [(x0, z1), (x1, z1), (x1, z1 + aisle), (x0, z1 + aisle)], -3.0, wall_h * 0.5, c)
            mid = 0.5 * (z0 + z1)
            hexa(R, [(x0, wall_h, z0 - 0.3), (x1, wall_h, z0 - 0.3), (x1, wall_h, z1 + 0.3), (x0, wall_h, z1 + 0.3)],
                 [(x0, ridge, mid - 0.05), (x1, ridge, mid - 0.05), (x1, ridge, mid + 0.05), (x0, ridge, mid + 0.05)], LEAD)
            for za, zb in ((z0 - aisle, z0), (z1, z1 + aisle)):
                hexa(R, [(x0, wall_h * 0.5, za), (x1, wall_h * 0.5, za), (x1, wall_h * 0.5, zb), (x0, wall_h * 0.5, zb)],
                     [(x0, wall_h * 0.5 + (0.0 if za < z0 else 3.0), za), (x1, wall_h * 0.5 + (0.0 if za < z0 else 3.0), za),
                      (x1, wall_h * 0.5 + (3.0 if za < z0 else 0.0), zb), (x0, wall_h * 0.5 + (3.0 if za < z0 else 0.0), zb)], LEAD)
            for x in np.arange(x0 + 3.0, x1 - 1.0, 7.2):
                for zf, n in ((z0 - aisle, -1), (z1 + aisle, 1)):
                    block(M, (x, wall_h * 0.25, zf + n * 0.7), (0.7, wall_h * 0.27, 0.7), c)
                    prism(M, ngon(4, 0.6), wall_h * 0.52, wall_h * 0.52 + 3.5, lite, top_scale=0.1)
                    M.P[-1][:, 0] += x; M.P[-1][:, 2] += zf + n * 0.7
                    prism(M, ngon(4, 0.55), wall_h + 0.5, wall_h + 4.0, lite, top_scale=0.1)
                    M.P[-1][:, 0] += x; M.P[-1][:, 2] += (z0 if n < 0 else z1) + n * 0.4
                    panel(W, [x + 3.6, 2.5, zf], (0, 0, n), (n, 0, 0), 3.2, wall_h * 0.5 - 5.0, DARK, arch=True)
                    panel(W, [x + 3.6, wall_h * 0.58, z0 if n < 0 else z1], (0, 0, n), (n, 0, 0), 4.0, wall_h * 0.36, DARK, arch=True)
        else:
            prism(M, [(x0, z0), (x1, z0), (x1, z1), (x0, z1)], -3.0, wall_h, c)
            prism(M, [(x0 - aisle, z0), (x0, z0), (x0, z1), (x0 - aisle, z1)], -3.0, wall_h * 0.5, c)
            prism(M, [(x1, z0), (x1 + aisle, z0), (x1 + aisle, z1), (x1, z1)], -3.0, wall_h * 0.5, c)
            mid = 0.5 * (x0 + x1)
            hexa(R, [(x0 - 0.3, wall_h, z0), (x0 - 0.3, wall_h, z1), (x1 + 0.3, wall_h, z1), (x1 + 0.3, wall_h, z0)],
                 [(mid - 0.05, ridge, z0), (mid - 0.05, ridge, z1), (mid + 0.05, ridge, z1), (mid + 0.05, ridge, z0)], LEAD)
            for zz in np.arange(z0 + 3.0, z1 - 1.0, 7.2):
                for xf, n in ((x0 - aisle, -1), (x1 + aisle, 1)):
                    block(M, (xf + n * 0.7, wall_h * 0.25, zz), (0.7, wall_h * 0.27, 0.7), c)
                    panel(W, [xf, 2.5, zz + 3.6], (n, 0, 0), (0, 0, -n), 3.2, wall_h * 0.5 - 5.0, DARK, arch=True)
                    panel(W, [x0 if n < 0 else x1, wall_h * 0.58, zz + 3.6], (n, 0, 0), (0, 0, -n), 4.0, wall_h * 0.36, DARK, arch=True)
        return M, W

    # (x: west front at -79, east end at +79; the crossing at x = -5 to 15 (20 m square), z -10..10)
    Mn, Wn = section("nave", -73.0, -5.0, -8.0, 8.0, 8.0, 30.0, 39.0)
    Mc, Wc = section("choir", 15.0, 79.0, -8.0, 8.0, 8.0, 30.0, 39.0)
    Mt, Wt = section("north_transept", -5.0, 15.0, -38.0, -18.0, 6.0, 28.0, 36.0, ax="z")
    Ms, Ws = section("south_transept", -5.0, 15.0, 18.0, 38.0, 6.0, 28.0, 36.0, ax="z")
    # (the transepts' inner parts to the crossing)
    prism(Mt, [(-5.0, -18.0), (15.0, -18.0), (15.0, -8.0), (-5.0, -8.0)], -3.0, 28.0, c)
    prism(Ms, [(-5.0, 8.0), (15.0, 8.0), (15.0, 18.0), (-5.0, 18.0)], -3.0, 28.0, c)
    # (the central tower: 20 m square, to 71 m, two tiers of tall paired lights, a pierced parapet)
    T = S.mesh("tower#far", "masonry")
    TW = S.mesh("tower_windows", "paint")
    prism(T, [(-5.0, -10.0), (15.0, -10.0), (15.0, 10.0), (-5.0, 10.0)], -3.0, 68.0, c)
    prism(T, [(-5.5, -10.5), (15.5, -10.5), (15.5, 10.5), (-5.5, 10.5)], 68.0, 71.0, lite)
    for (px, pz) in ((-5.0, -10.0), (15.0, -10.0), (15.0, 10.0), (-5.0, 10.0)):
        prism(T, ngon(4, 1.2), 40.0, 73.5, lite, top_scale=0.6)
        T.P[-1][:, 0] += px; T.P[-1][:, 2] += pz
    for off in (-4.5, 4.5):
        for (nx, nz, ax_) in ((0, 1, "x"), (0, -1, "x"), (1, 0, "z"), (-1, 0, "z")):
            if ax_ == "x":
                at = [5.0 + off, 46.0, 10.0 * nz]; r = (nz, 0, 0)
            else:
                at = [5.0 + 10.0 * nx, 46.0, off * nx]; r = (0, 0, -nx)
            panel(TW, at, (nx, 0, nz), r, 2.6, 18.0, DARK, arch=True)
    # (the west front: two towers 60 m, the great west window between, three doors)
    for tz in (-12.0, 12.0):
        sq = [(-79.0, tz - 6.0), (-67.0, tz - 6.0), (-67.0, tz + 6.0), (-79.0, tz + 6.0)]
        prism(T, sq, -3.0, 56.0, c)
        prism(T, [(-79.4, tz - 6.4), (-66.6, tz - 6.4), (-66.6, tz + 6.4), (-79.4, tz + 6.4)], 56.0, 58.0, lite)
        for (px, pz) in ((-79.0, tz - 6.0), (-67.0, tz - 6.0), (-67.0, tz + 6.0), (-79.0, tz + 6.0)):
            prism(T, ngon(4, 1.0), 50.0, 64.0, lite, top_scale=0.15)
            T.P[-1][:, 0] += px; T.P[-1][:, 2] += pz
        panel(TW, [-79.0, 34.0, tz], (-1, 0, 0), (0, 0, 1), 3.4, 14.0, DARK, arch=True)
        panel(TW, [-79.0, 0.0, tz], (-1, 0, 0), (0, 0, 1), 3.0, 7.0, DARK, arch=True)
    prism(T, [(-79.0, -6.0), (-73.0, -6.0), (-73.0, 6.0), (-79.0, 6.0)], -3.0, 36.0, c)
    hexa(T, [(-79.0, 36.0, -6.0), (-73.0, 36.0, -6.0), (-73.0, 36.0, 6.0), (-79.0, 36.0, 6.0)],
         [(-79.0, 44.0, -0.3), (-73.0, 44.0, -0.3), (-73.0, 44.0, 0.3), (-79.0, 44.0, 0.3)], lite)
    panel(TW, [-79.0, 13.0, 0.0], (-1, 0, 0), (0, 0, 1), 9.0, 18.0, DARK, arch=True)
    panel(TW, [-79.0, 0.0, 0.0], (-1, 0, 0), (0, 0, 1), 4.0, 8.0, DARK, arch=True)
    # (the Great East Window, the Five Sisters (north), the Rose Window (south))
    panel(Wc, [79.0, 8.0, 0.0], (1, 0, 0), (0, 0, -1), 9.5, 23.7, DARK, arch=True)
    for k in range(5):
        panel(Wt, [-2.0 + k * 3.5, 4.0, -38.0], (0, 0, -1), (-1, 0, 0), 1.6, 16.0, DARK, arch=True)
    ring = [(5.0 + 4.0 * math.cos(a), 26.0 + 4.0 * math.sin(a), 38.05) for a in np.linspace(0, 2 * math.pi, 25)[:-1]]
    P = np.array([[5.0, 26.0, 38.05]] + [list(q) for q in ring])
    Tt = np.array([[0, 1 + k, 1 + (k + 1) % 24] for k in range(24)])
    Ws.add(P, _outward(P, Tt, np.array([5.0, 26.0, 0.0])), colour(len(P), DARK), P[:, [0, 1]])
    for k in range(4):
        panel(Ws, [-1.5 + k * 4.3, 4.0, 38.0], (0, 0, 1), (1, 0, 0), 2.2, 12.0, DARK, arch=True)
    # (the east end's corner turrets)
    for pz in (-16.0, 16.0):
        prism(Mc, ngon(8, 1.6), -3.0, 44.0, lite, top_scale=0.7)
        Mc.P[-1][:, 0] += 79.0; Mc.P[-1][:, 2] += pz
    return S


SITES = {
    "stonehenge": stonehenge, "castlerigg": castlerigg, "rudston": rudston, "devils_arrows": devils_arrows,
    "wainhouse": wainhouse, "hardy": hardy, "stoodley": stoodley, "cook": cook, "souter": souter, "bolton": bolton,
    "angel": angel, "blackpool": blackpool, "liver": liver, "minster": minster,
}


def main(names):
    os.makedirs(OUT, exist_ok=True)
    for name in names or sorted(SITES):
        S = SITES[name]()
        nodes, tris = write_site(os.path.join(OUT, name + ".glb"), S)
        print("%-14s %3d parts, %6d triangles" % (name, nodes, tris))


if __name__ == "__main__":
    main(sys.argv[1:])
