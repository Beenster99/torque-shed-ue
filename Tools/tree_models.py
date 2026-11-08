"""The Dales' trees as real geometry for Nanite: ash, sycamore, alder, birch, hawthorn and Sitka spruce.

Used by Tools/make_trees.py. Each tree is grown by space colonisation (the branches grow towards points
scattered through the species' crown shape, so they fill it the way real ones do), its limbs made as
tubes thickened by the pipe model, and its leaves as small opaque polygons (no alpha cut-outs, which
Nanite draws slowly), clustered on the twig ends. A far version of each tree is a few lumpy masses of
leaves on its trunk and main limbs, for trees that are only ever seen from more than a kilometre away.

Frames: metres, x east, y up, z south (as glTF and the rest of Data/Buttertubs). The tree's base is at
the origin and its trunk goes 1.2 m below it, so it can be sunk into a slope without its foot showing.

Vertex colour RGB is linear colour, A is how much sky the spot sees (dark inside the crown). UV x is 1
on leaves and 0 on bark; UV y is how free the spot is to move in the wind (0 at the trunk, 1 at the
outer twigs), which M_Tree turns into sway and flutter. UV1 is the bark texture's (around and along the
branch, in metres; BARK_M per repeat around), 0 on the leaves.
"""
import math
import numpy as np


def lin(rgb):
    return np.power(np.asarray(rgb, np.float64) / 255.0, 2.2)


class Mesh:
    def __init__(self):
        self.P, self.C, self.UV, self.UV1, self.T = [], [], [], [], []
        self.n = 0

    def add(self, pos, tris, col, uv, uv1=None):
        pos = np.asarray(pos, np.float64).reshape(-1, 3)
        col = np.asarray(col, np.float64)
        if col.ndim == 1:
            col = np.repeat(col[None], len(pos), 0)
        uv = np.asarray(uv, np.float64)
        if uv.ndim == 1:
            uv = np.repeat(uv[None], len(pos), 0)
        self.P.append(pos); self.C.append(col); self.UV.append(uv)
        self.UV1.append(np.zeros((len(pos), 2)) if uv1 is None else np.asarray(uv1, np.float64).reshape(-1, 2))
        self.T.append(np.asarray(tris, np.int64).reshape(-1, 3) + self.n)
        self.n += len(pos)

    def uv1(self):
        """The second UV set: the bark's texture coordinates (around, along in metres); 0 elsewhere."""
        return np.concatenate(self.UV1)

    def arrays(self, weld=False):
        """Positions, triangles, normals, RGBA bytes and UV0. weld: vertices at the same place on the
        bark (UV0 x 0) share one normal, so the seam where its texture wraps doesn't show."""
        P = np.concatenate(self.P); T = np.concatenate(self.T)
        C = np.concatenate(self.C); UV = np.concatenate(self.UV)
        fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
        N = np.zeros_like(P)
        for k in range(3):
            np.add.at(N, T[:, k], fn)
        if weld:
            bark = np.flatnonzero(UV[:, 0] == 0)
            key = np.round(P[bark] * 1e4).astype(np.int64)
            _, inv = np.unique(key, axis=0, return_inverse=True)
            S = np.zeros((inv.max() + 1, 3))
            np.add.at(S, inv.ravel(), N[bark])
            N[bark] = S[inv.ravel()]
        N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
        rgba = np.clip(np.round(C * 255), 0, 255).astype(np.uint8)
        return P, T, N, rgba, UV


# --- Species ---------------------------------------------------------------------------------------

# height: the model's height (m); instances scale it. crown: the crown's base and widest point as a
# share of the height, its radius as a share of the height, and its shape (1 an ellipsoid, above 1
# flatter-topped, below 1 more pointed). leaf: shape, size (m), leaves per twig end, colour (sRGB) and
# its spread. bark: colour and its spread. lean: how far the crown leans downwind (to the north east,
# the mesh's +x) as a share of the height.
SPECIES = {
    "ash": dict(height=15.0, crown=(0.2, 0.55, 0.42, 1.3), trunk_r=0.022, attract=9000, step=0.3, infl=1.7, kill=0.45,
                up=0.10, droop=0.06, leaf=("pinnate", 0.09, 4, (104, 128, 58), 0.09), bark=((88, 86, 78), 0.07),
                lean=0.02, sparse=0.55, far_blobs=12, leaf_depth=2),
    "sycamore": dict(height=15.0, crown=(0.22, 0.55, 0.42, 1.4), trunk_r=0.026, attract=8000, step=0.3, infl=1.6,
                     kill=0.45, up=0.05, droop=0.02, leaf=("palmate", 0.095, 10, (66, 92, 38), 0.08),
                     bark=((80, 72, 62), 0.08), lean=0.02, sparse=0.8, far_blobs=12),
    "alder": dict(height=11.0, crown=(0.2, 0.45, 0.3, 0.85), trunk_r=0.02, attract=6500, step=0.26, infl=1.4, kill=0.38,
                  up=0.16, droop=0.02, leaf=("round", 0.065, 12, (54, 76, 34), 0.07), bark=((56, 50, 44), 0.08),
                  lean=0.0, sparse=0.4, far_blobs=10),
    "birch": dict(height=12.0, crown=(0.28, 0.5, 0.25, 0.9), trunk_r=0.016, attract=6000, step=0.26, infl=1.4, kill=0.36,
                  up=0.14, droop=0.12, leaf=("diamond", 0.045, 15, (108, 132, 54), 0.08), bark=((168, 162, 152), 0.06),
                  lean=0.0, sparse=0.3, far_blobs=10),
    "hawthorn": dict(height=5.0, crown=(0.14, 0.5, 0.5, 1.6), trunk_r=0.024, attract=5000, step=0.13, infl=0.7, kill=0.2,
                     up=0.02, droop=0.0, leaf=("diamond", 0.042, 15, (60, 82, 36), 0.08), bark=((62, 54, 46), 0.08),
                     lean=0.16, sparse=0.5, far_blobs=8),
    "spruce": dict(height=18.0, trunk_r=0.017, leaf=(None, 0.0, 0, (36, 56, 40), 0.06), bark=((70, 56, 46), 0.08),
                   far_blobs=0),
}


def envelope(sp, rng, n):
    """n attraction points in the crown: a union of several clumps (one per big limb), of different
    sizes, around the crown's centre and some hanging low on one side, so the outline is ragged and
    lopsided as a real field tree's is, not a ball on a stick. Mostly near each clump's surface,
    where the leaves are."""
    h = sp["height"]
    c0, cw, cr, flat = sp["crown"]
    y0, ym = c0 * h, cw * h
    R = cr * h
    hv = max(h - y0, 1e-6) / 2           # the crown's half height
    k = sp.get("lobes", 7)
    cen = np.array([0.0, y0 + 0.42 * (h - y0), 0.0])
    clumps = []
    for i in range(k):
        a = rng.uniform(0, 2 * np.pi)
        up = rng.uniform(-0.95, 0.9)
        d = np.array([math.cos(a) * math.sqrt(1 - up * up), up, math.sin(a) * math.sqrt(1 - up * up)])
        off = d * np.array([R, hv, R]) * rng.uniform(0.35, 0.7)
        rad = rng.uniform(0.35, 0.62) * np.array([R, hv / flat, R]) * rng.uniform(0.85, 1.15, 3)
        clumps.append((cen + off, rad))
    clumps.append((cen + np.array([0, hv * 0.35, 0]), np.array([R, hv, R]) * 0.45))   # the top
    clumps.append((cen, np.array([R, hv, R]) * 0.5))                                 # the middle
    # The downwind lean: the crown sits a little to +x, more so higher up.
    pts = []
    vols = np.array([np.prod(r) for _, r in clumps])
    counts = np.maximum((n * 1.2 * vols / vols.sum()).astype(int), 20)
    for (c, r), m in zip(clumps, counts):
        q = rng.normal(0, 1, (m, 3)); q /= np.linalg.norm(q, axis=1, keepdims=True)
        q *= rng.uniform(0, 1, (m, 1)) ** 0.25           # mostly near the surface
        P = c + q * r
        pts.append(P)
    P = np.concatenate(pts)
    P = P[P[:, 1] > y0 * 0.7]
    P[:, 0] += sp["lean"] * h * np.clip((P[:, 1] - y0) / max(h - y0, 1e-6), 0, 1)
    P = P[rng.permutation(len(P))[:n]]
    # Gaps: a few holes right through, so the sky shows through the crown in places.
    for _ in range(int(sp["sparse"] * 5)):
        c = P[rng.integers(len(P))]
        P = P[np.linalg.norm(P - c, axis=1) > rng.uniform(0.2, 0.38) * R]
    return P


def colonise(sp, rng):
    """Space colonisation from the top of a trunk: nodes (n, 3) and their parents (-1 for the root)."""
    h = sp["height"]
    A = envelope(sp, rng, int(sp["attract"] * 1.3))
    step, infl, kill = sp["step"] * h / sp["height"], sp["infl"], sp["kill"]
    # The trunk: straight-ish up to the crown's base, then the colonisation takes over.
    c0 = sp["crown"][0] * h
    nodes = [np.array([0.0, -1.2, 0.0])]
    parent = [-1]
    bend = rng.normal(0, 0.02, 2)
    y = -1.2
    while y < c0:
        y = min(y + step, c0)
        f = (y + 1.2) / (c0 + 1.2)
        nodes.append(np.array([bend[0] * f * h * 0.1 + sp["lean"] * h * 0.2 * f ** 2, y, bend[1] * f * h * 0.1]))
        parent.append(len(nodes) - 2)
    nodes = list(nodes)
    N = np.array(nodes)
    best_d = np.full(len(A), np.inf); best_i = np.full(len(A), -1)

    def update(new_idx):
        nonlocal best_d, best_i
        if not len(new_idx):
            return
        Q = N[new_idx]
        d = np.linalg.norm(A[:, None, :] - Q[None, :, :], axis=2)
        k = np.argmin(d, axis=1); dk = d[np.arange(len(A)), k]
        m = dk < best_d
        best_d[m] = dk[m]; best_i[m] = np.asarray(new_idx)[k[m]]

    update(list(range(len(N))))
    alive = np.ones(len(A), bool)
    for it in range(400):
        alive &= best_d > kill
        act = alive & (best_d < infl)
        if not act.any():
            # Nothing in reach: grow the nearest node towards the nearest attraction point.
            if not alive.any():
                break
            ai = np.flatnonzero(alive)[np.argmin(best_d[alive])]
            if best_d[ai] > infl * 4:
                break
            act = np.zeros(len(A), bool); act[ai] = True
        idx = best_i[act]
        dirs = A[act] - N[idx]
        dirs /= np.maximum(np.linalg.norm(dirs, axis=1, keepdims=True), 1e-9)
        uniq, inv = np.unique(idx, return_inverse=True)
        acc = np.zeros((len(uniq), 3))
        np.add.at(acc, inv, dirs)
        acc /= np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-9)
        acc[:, 1] += sp["up"] - sp["droop"] * (N[uniq, 1] / h)
        acc += rng.normal(0, 0.12, acc.shape)
        acc /= np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-9)
        new = N[uniq] + acc * step
        start = len(N)
        N = np.vstack([N, new])
        parent.extend(uniq.tolist())
        update(list(range(start, len(N))))
    return N, np.array(parent)


def radii(N, parent, tip_r, trunk_r_base):
    """Pipe model: r^2.4 of a branch is the sum of its children's, from tip_r at the twig ends."""
    n = len(N)
    kids = [[] for _ in range(n)]
    for i in range(1, n):
        kids[parent[i]].append(i)
    order = np.argsort(-np.array([depth for depth in _depths(parent)]))
    r = np.zeros(n)
    e = 2.4
    for i in order:
        if not kids[i]:
            r[i] = tip_r
        else:
            r[i] = sum(r[k] ** e for k in kids[i]) ** (1 / e)
    # Scale so the trunk at its base is the species' size, keeping the twigs at tip_r.
    if r[0] > tip_r:
        r = tip_r + (r - tip_r) * (trunk_r_base - tip_r) / (r[0] - tip_r)
    return r, kids


def _depths(parent):
    d = np.zeros(len(parent), int)
    for i in range(1, len(parent)):
        d[i] = d[parent[i]] + 1
    return d


def ring_frame(d):
    d = d / max(np.linalg.norm(d), 1e-9)
    a = np.array([1.0, 0, 0]) if abs(d[0]) < 0.9 else np.array([0, 0, 1.0])
    u = np.cross(d, a); u /= np.linalg.norm(u)
    v = np.cross(d, u)
    return u, v


BARK_M = 0.7   # metres one repeat of the bark texture covers (Data/Textures/textures.json "bark" tile_m)


def tubes(mesh, N, parent, r, kids, bark, rng, wind, ao_fn, min_r=0.006, flare=True):
    """Bark: a ring of vertices at each node, joined to its parent's ring. Sides by thickness. UV1 is
    the bark texture's: around the branch (a whole number of repeats, so it wraps without a seam) and
    along it, in metres from the foot of the tree."""
    n = len(N)
    base_col, spread = bark
    col0 = lin(base_col)
    # One bark colour and one ring phase for the whole tree: a fresh colour and a turned ring on
    # every segment made the trunk read as a stack of bands (beenster: "very low poly").
    cv_tree = col0 * np.exp(rng.normal(0, spread, 3))
    phase = rng.uniform(0, 1)
    # Root flare: buttresses where the roots leave the trunk, a few per tree, spreading at the ground.
    lobes = int(rng.integers(4, 7)); lobe_ph = rng.uniform(0, 6.3)
    along = np.zeros(n)
    for i in range(1, n):
        along[i] = along[parent[i]] + np.linalg.norm(N[i] - N[parent[i]])
    for i in range(1, n):
        if r[i] < min_r:
            continue
        p = parent[i]
        sides = 24 if r[i] > 0.12 else 18 if r[i] > 0.05 else 10 if r[i] > 0.02 else 6 if r[i] > 0.01 else 3
        d = N[i] - N[p]
        u, v = ring_frame(d)
        ang = np.arange(sides + 1) / sides * 2 * np.pi + phase   # the last vertex repeats the first: the seam
        reps = max(1, int(round(2 * np.pi * r[i] / BARK_M)))
        rings = []
        for q, rr, yy in ((N[p], max(r[p] if p else r[i], r[i]) if kids[p] and len(kids[p]) == 1 else r[i] * 1.04, N[p][1]),
                          (N[i], r[i], N[i][1])):
            rad = np.full(sides + 1, rr)
            if flare and q[1] < 1.4 and rr > 0.08:
                f = np.clip((1.4 - q[1]) / 1.4, 0, 1.3)
                rad = rr * (1 + 0.85 * f ** 2.2 * (1 + 0.5 * np.cos(ang * lobes + lobe_ph)))
            rings.append(q[None, :] + np.cos(ang)[:, None] * u[None, :] * rad[:, None] + np.sin(ang)[:, None] * v[None, :] * rad[:, None])
        P = np.vstack(rings)
        k = np.arange(sides)
        m = sides + 1
        a, b, c, e = k, k + 1, k + m, k + 1 + m
        T = np.concatenate([np.stack([a, c, b], 1), np.stack([b, c, e], 1)])
        uv1 = np.concatenate([np.stack([np.arange(m) / sides * reps * BARK_M, np.full(m, along[p])], 1),
                              np.stack([np.arange(m) / sides * reps * BARK_M, np.full(m, along[i])], 1)])
        # Colour: the species' bark, varied, greened by algae low down on the wet (south west) side.
        cv = cv_tree * np.exp(rng.normal(0, spread * 0.15, 3))
        C = np.repeat(cv[None], len(P), 0)
        low = np.clip(1 - P[:, 1] / 3.0, 0, 1)
        sw = np.clip(-(P[:, 0] - N[i][0]) + (P[:, 2] - N[i][2]), 0, None) / max(r[i], 1e-3)
        alg = np.clip(low * (0.35 + 0.4 * np.clip(sw, 0, 1)), 0, 0.7)
        C = C * (1 - alg[:, None]) + lin((70, 92, 44))[None] * alg[:, None]
        C = C * np.clip(1 - 0.5 * np.clip(-P[:, 1], 0, 1), 0.4, 1)[:, None]   # soil-dark below ground
        if base_col[0] > 140:   # birch: black marks on the white bark, black at the foot
            C = C * np.where(rng.uniform(0, 1, (len(P), 1)) < 0.22, 0.3, 1.0)
            C = C * np.clip(0.35 + P[:, 1:2] / 1.5, 0.35, 1)
        ao = ao_fn(P)
        w = np.repeat([wind[p], wind[i]], m)
        mesh.add(P, T, np.concatenate([C, ao[:, None]], 1), np.stack([np.zeros(len(P)), w], 1), uv1)


LEAF_SHAPES = {
    # 2D outlines (along, across) of one leaf, base at (0, 0), length 1; triangles as a fan from vertex 0.
    "diamond": (np.array([[0, 0], [0.45, 0.32], [1, 0], [0.45, -0.32]]), [[0, 1, 2], [0, 2, 3]]),
    "round": (np.array([[0, 0], [0.3, 0.38], [0.75, 0.42], [1, 0], [0.75, -0.42], [0.3, -0.38]]),
              [[0, 1, 2], [0, 2, 3], [0, 3, 4], [0, 4, 5]]),
    "palmate": (np.array([[0, 0], [0.2, 0.62], [0.55, 0.45], [0.8, 0.5], [1, 0], [0.8, -0.5], [0.55, -0.45], [0.2, -0.62]]),
                [[0, 1, 2], [0, 2, 3], [0, 3, 4], [0, 4, 5], [0, 5, 6], [0, 6, 7]]),
    "leaflet": (np.array([[0, 0], [0.5, 0.16], [1, 0], [0.5, -0.16]]), [[0, 1, 2], [0, 2, 3]]),
}


def leaf_cluster(mesh, at, out, sp, rng, wind_w, ao_fn, scale=1.0):
    """The leaves at one twig end: `at` the twig's end, `out` the way it points."""
    shape, size, count, col, spread = sp["leaf"]
    size *= scale
    base = lin(col)
    allP, allT, allC, allW = [], [], [], []
    nv = 0
    if shape == "pinnate":
        # Ash: compound leaves, each a stalk of 4 pairs of leaflets and one at the tip.
        outline, tris = LEAF_SHAPES["leaflet"]
        for _ in range(count):
            d = out + rng.normal(0, 0.6, 3); d[1] += 0.2
            d /= np.linalg.norm(d)
            u, v = ring_frame(d)
            side = u * math.cos(rng.uniform(0, 6.3)) + v * math.sin(rng.uniform(0, 6.3))
            side -= d * (side @ d); side /= max(np.linalg.norm(side), 1e-9)
            normal = np.cross(d, side)
            if normal[1] < 0:
                normal = -normal
            L = size * rng.uniform(3.2, 4.2)   # the whole compound leaf
            for k in range(4):
                t = 0.3 + k * 0.22
                stem = at + d * L * t + np.array([0, -0.06, 0]) * L * t * t
                for sgn in ((1, -1) if k < 3 else (0,)):
                    ld = d * (0.35 if sgn else 1) + side * sgn
                    ld /= np.linalg.norm(ld)
                    ls = np.cross(normal, ld)
                    ln = size * rng.uniform(0.8, 1.15)
                    P = stem[None] + outline[:, :1] * ld[None] * ln + outline[:, 1:] * ls[None] * ln
                    P[[1, 3]] += normal * ln * 0.05   # a slight fold along the midrib
                    allP.append(P); allT.append(np.array(tris) + nv); nv += len(P)
                    c = base * np.exp(rng.normal(0, spread, 3)) * rng.uniform(0.85, 1.12)
                    allC.append(np.repeat(c[None], len(P), 0))
    else:
        outline, tris = LEAF_SHAPES[shape]
        for _ in range(count):
            d = out + rng.normal(0, 0.8, 3); d[1] += 0.3
            d /= np.linalg.norm(d)
            # Leaves face mostly up and outwards, on a short stalk off the twig.
            nrm = np.array([0, 1.0, 0]) * 0.7 + out * 0.5 + rng.normal(0, 0.35, 3)
            nrm /= np.linalg.norm(nrm)
            ld = d - nrm * (d @ nrm); ld /= max(np.linalg.norm(ld), 1e-9)
            ls = np.cross(nrm, ld)
            ln = size * rng.uniform(0.75, 1.25)
            st = at + rng.normal(0, size * 2.2, 3) + d * size * 0.8
            P = st[None] + outline[:, :1] * ld[None] * ln + outline[:, 1:] * ls[None] * ln
            mid = (outline[:, 1] == 0)
            P[~mid] -= nrm * ln * 0.07   # cupped a little
            allP.append(P); allT.append(np.array(tris) + nv); nv += len(P)
            c = base * np.exp(rng.normal(0, spread, 3)) * rng.uniform(0.85, 1.12)
            allC.append(np.repeat(c[None], len(P), 0))
    P = np.concatenate(allP); C = np.concatenate(allC)
    ao = ao_fn(P)
    mesh.add(P, np.concatenate(allT), np.concatenate([C, ao[:, None]], 1),
             np.stack([np.ones(len(P)), np.full(len(P), wind_w)], 1))
    return P


def crown_ao(leaf_pts, h):
    """How much sky a point sees: from how deep it is in the crown (distance from the crown's centre
    against the crown's extent that way) and how high it is."""
    c = leaf_pts.mean(0)
    ext = np.percentile(np.linalg.norm(leaf_pts - c, axis=1), 92)

    def ao(P):
        d = np.linalg.norm((P - c) * np.array([1, 0.8, 1]), axis=1) / max(ext, 1e-6)
        up = np.clip((P[:, 1] - c[1]) / max(ext, 1e-6), -1, 1)
        return np.clip(0.22 + 0.68 * np.clip(d, 0, 1) ** 1.6 + 0.12 * up, 0.12, 1.0)
    return ao


def broadleaf(name, seed):
    """One broadleaf tree: (near mesh, far mesh, info)."""
    rng = np.random.default_rng(seed)
    # Each variant its own shape: crown lower or higher, wider or narrower, flatter or rounder,
    # leaning one way, more or fewer big limbs (lobes), so a row of them isn't one tree repeated.
    sp = dict(SPECIES[name])
    c0, cw, cr, flat = sp["crown"]
    k0 = rng.uniform(0.4, 1.1)
    sp["crown"] = (c0 * k0, min(cw * rng.uniform(0.85, 1.1), 0.75), cr * rng.uniform(0.8, 1.25), flat * rng.uniform(0.75, 1.3))
    sp["lean"] = sp["lean"] + rng.uniform(0.0, 0.07)
    sp["lobes"] = int(rng.integers(4, 10))
    sp["sparse"] = sp["sparse"] * rng.uniform(0.8, 1.6)
    h = sp["height"]
    N, parent = colonise(sp, rng)
    r, kids = radii(N, parent, 0.004, sp["trunk_r"] * h)
    # Wind: 0 at the trunk, 1 at the outer twigs (by path length from the crown's base).
    plen = np.zeros(len(N))
    for i in range(1, len(N)):
        plen[i] = plen[parent[i]] + np.linalg.norm(N[i] - N[parent[i]])
    c0 = sp["crown"][0] * h + 1.2
    wind = np.clip((plen - c0) / max(plen.max() - c0, 1e-6), 0, 1) ** 1.3
    # Twig ends: the leaves go there, and along the last few nodes before them.
    tips = [i for i in range(1, len(N)) if not kids[i]]
    leaf_nodes = set()
    for t in tips:
        k = t
        for _ in range(sp.get("leaf_depth", 3)):
            leaf_nodes.add(k)
            k = parent[k]
            if k <= 0 or r[k] > 0.02:
                break
    leaf_nodes = sorted(leaf_nodes)
    # AO from where the leaves will be (the nodes, as a stand-in).
    ao_fn = crown_ao(N[leaf_nodes], h)
    near = Mesh()
    leafP = []
    for i in leaf_nodes:
        d = N[i] - N[parent[i]]
        d /= max(np.linalg.norm(d), 1e-9)
        out = N[i] - np.array([0, N[i][1], 0]); out[1] = 0
        out = out / max(np.linalg.norm(out), 1e-9) * 0.6 + d * 0.4
        leafP.append(leaf_cluster(near, N[i], out, sp, rng, min(1.0, wind[i] + 0.15), ao_fn))
    tubes(near, N, parent, r, kids, sp["bark"], rng, wind, ao_fn)
    far = far_tree(sp, rng, N, parent, r, kids, np.concatenate(leafP), ao_fn, wind)
    return near, far, {"nodes": int(len(N)), "tips": len(tips), "height": h}


def lumpy_sphere(c, rad, rng, subdiv=2):
    """An icosphere, its radius roughened, as (positions, triangles)."""
    t = (1 + 5 ** 0.5) / 2
    V = np.array([[-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0], [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t],
                  [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1]], np.float64)
    F = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6],
         [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7],
         [9, 8, 1]]
    V = list(V / np.linalg.norm(V[0]))
    for _ in range(subdiv):
        mid = {}
        nf = []
        for a, b, c_ in F:
            ids = []
            for x, y in ((a, b), (b, c_), (c_, a)):
                key = (min(x, y), max(x, y))
                if key not in mid:
                    m = (V[x] + V[y]) / 2
                    V.append(m / np.linalg.norm(m)); mid[key] = len(V) - 1
                ids.append(mid[key])
            ab, bc, ca = ids
            nf += [[a, ab, ca], [b, bc, ab], [c_, ca, bc], [ab, bc, ca]]
        F = nf
    V = np.array(V)
    bump = np.ones(len(V))
    for _ in range(4):
        dirn = rng.normal(0, 1, 3); dirn /= np.linalg.norm(dirn)
        bump += 0.12 * np.cos((V @ dirn) * rng.uniform(4, 9) + rng.uniform(0, 6.3))
    return c + V * (rad * bump)[:, None], np.array(F)


def kmeans(P, k, rng, iters=12):
    C = P[rng.choice(len(P), k, replace=False)]
    for _ in range(iters):
        lab = np.argmin(((P[:, None] - C[None]) ** 2).sum(-1), 1)
        for j in range(k):
            m = lab == j
            if m.any():
                C[j] = P[m].mean(0)
    return C, lab


def far_tree(sp, rng, N, parent, r, kids, leaves, ao_fn, wind):
    """The far version: the trunk and main limbs as thin tubes, the leaves as a dozen lumpy masses."""
    far = Mesh()
    k = sp["far_blobs"]
    sub = leaves[rng.choice(len(leaves), min(len(leaves), 6000), replace=False)]
    C, lab = kmeans(sub, k, rng)
    base = lin(sp["leaf"][3])
    for j in range(k):
        m = sub[lab == j]
        if len(m) < 8:
            continue
        ext = np.percentile(np.linalg.norm(m - C[j], axis=1), 80)
        P, T = lumpy_sphere(C[j], ext * 0.95, rng, 1)
        ao = ao_fn(P) * 0.8
        # Darker and greyer than a single leaf: a crown seen from afar is mostly its own shade.
        col = base * np.exp(rng.normal(0, 0.05, 3))
        col = (col * 0.7 + col.mean() * 0.3) * 0.62
        far.add(P, T, np.concatenate([np.repeat(col[None], len(P), 0), ao[:, None]], 1),
                np.stack([np.ones(len(P)), np.full(len(P), 0.5)], 1))
    tubes(far, N, parent, r, kids, sp["bark"], rng, wind * 0, ao_fn, min_r=0.06, flare=False)
    return far


# --- Sitka spruce (the plantations) ----------------------------------------------------------------

def spruce(seed):
    """A plantation spruce: a straight trunk, whorls of drooping branches (dead and short low down in a
    close-grown stand), each covered in shoots of needles made as little dark bottle brushes."""
    sp = SPECIES["spruce"]
    rng = np.random.default_rng(seed)
    h = sp["height"]
    near = Mesh()
    far = Mesh()
    live_base = h * rng.uniform(0.35, 0.5)   # close-grown: the lower trunk is bare
    R_max = h * 0.16
    bark0 = lin(sp["bark"][0])
    needle = lin(sp["leaf"][3])

    def ao_fn(P):
        y = np.clip((P[:, 1] - live_base) / (h - live_base), 0, 1)
        rr = np.linalg.norm(P[:, [0, 2]], axis=1)
        R = R_max * (1 - y) + 0.2
        return np.clip(0.18 + 0.55 * np.clip(rr / R, 0, 1) ** 1.2 + 0.3 * y, 0.12, 1)

    # Trunk.
    ys = np.linspace(-1.2, h, 24)
    rt = sp["trunk_r"] * h * np.clip(1 - (ys + 1.2) / (h + 1.2), 0.03, 1) ** 0.9
    sides = 12
    ang = np.arange(sides + 1) / sides * 2 * np.pi   # the last repeats the first: the bark's seam
    P = np.concatenate([np.stack([np.cos(ang) * rr, np.full(sides + 1, y), np.sin(ang) * rr], 1) for y, rr in zip(ys, rt)])
    T = []
    m1 = sides + 1
    for k in range(len(ys) - 1):
        for s in range(sides):
            a, b = k * m1 + s, k * m1 + s + 1
            T += [[a, a + m1, b], [b, a + m1, b + m1]]
    reps = max(1, int(round(2 * np.pi * rt[0] / BARK_M)))
    uv1 = np.concatenate([np.stack([np.arange(m1) / sides * reps * BARK_M, np.full(m1, y + 1.2)], 1) for y in ys])
    C = np.repeat(bark0[None], len(P), 0) * np.exp(rng.normal(0, 0.05, (len(P), 1)))
    for m in (near, far):
        m.add(P, T, np.concatenate([C, ao_fn(P)[:, None]], 1), np.stack([np.zeros(len(P)), np.zeros(len(P))], 1), uv1)
    # Whorls every ~0.5 m (a year's growth), 5 branches each, plus a few between.
    y = 1.0
    shoots = []
    while y < h - 0.3:
        t = (y - live_base) / (h - live_base)
        live = t > 0
        L = (R_max * (1 - t) ** 0.95 + 0.15) if live else rng.uniform(0.3, 0.9)
        nb = 5 if live else 3
        a0 = rng.uniform(0, 2 * np.pi)
        for b in range(nb):
            a = a0 + b * 2 * np.pi / nb + rng.normal(0, 0.25)
            dirn = np.array([math.cos(a), 0, math.sin(a)])
            # Branch: out and up a little, drooping at the end (more so low down).
            n_seg = 6
            pts = []
            for s in range(n_seg + 1):
                f = s / n_seg
                drop = (0.15 + 0.25 * (1 - t)) * L * f * f - 0.12 * L * f
                pts.append(dirn * L * f + np.array([0, y - drop, 0]))
            pts = np.array(pts)
            rb = 0.04 * (L / R_max) + 0.008
            # The branch as a thin 3 sided tube.
            for s in range(n_seg):
                p0, p1 = pts[s], pts[s + 1]
                u, v = ring_frame(p1 - p0)
                r0 = rb * (1 - s / n_seg) + 0.004; r1 = rb * (1 - (s + 1) / n_seg) + 0.004
                ring = lambda p, rr: p[None] + np.array([u * math.cos(q) + v * math.sin(q) for q in (0, 2.1, 4.2)]) * rr
                Pq = np.vstack([ring(p0, r0), ring(p1, r1)])
                Tq = [[0, 3, 1], [1, 3, 4], [1, 4, 2], [2, 4, 5], [2, 5, 0], [0, 5, 3]]
                col = bark0 * (0.8 if live else 0.65)
                near.add(Pq, Tq, np.concatenate([np.repeat(col[None], 6, 0), ao_fn(Pq)[:, None]], 1),
                         np.stack([np.zeros(6), np.full(6, 0.6 * (s + 1) / n_seg * (1 if live else 0.2))], 1))
            if live:
                # Shoots along the outer two thirds of the branch, either side and hanging below it.
                for s in range(1, n_seg + 1):
                    for _ in range(6):
                        p = pts[s] + rng.normal(0, 0.1, 3)
                        sd = np.cross(dirn, [0, 1, 0]) * rng.choice([-1, 1]) + dirn * 0.6 + np.array([0, rng.uniform(-0.9, 0.1), 0])
                        sd /= np.linalg.norm(sd)
                        shoots.append((p, sd, rng.uniform(0.3, 0.55) * (0.6 + 0.4 * L / R_max), (s / n_seg) * 0.8 + 0.2))
            y_br = y
        y += rng.uniform(0.42, 0.6)
    # The leader and top shoots.
    for k in range(10):
        p = np.array([0, h - 0.2 - k * 0.15, 0]) + rng.normal(0, 0.03, 3)
        sd = np.array([rng.normal(0, 0.4), 1.0, rng.normal(0, 0.4)]); sd /= np.linalg.norm(sd)
        shoots.append((p, sd, 0.25, 1.0))
    # Shoots as bottle brushes: a stalk with 3 rows of spiky needles (a jagged triangular prism).
    allP, allT, allC, allW = [], [], [], []
    nv = 0
    for p, d, L, w in shoots:
        u, v = ring_frame(d)
        nseg = 3
        Pk = []
        for s in range(nseg + 1):
            q = p + d * L * s / nseg
            rr = 0.075 * (1 - 0.6 * s / nseg)
            for k, a in enumerate((0.0, 2.1, 4.2)):
                jag = rr * (1.0 if (s + k) % 2 == 0 else 0.55)
                Pk.append(q + (u * math.cos(a) + v * math.sin(a)) * jag)
        Pk.append(p + d * L * 1.08)
        Pk = np.array(Pk)
        Tk = []
        for s in range(nseg):
            for k in range(3):
                a, b = s * 3 + k, s * 3 + (k + 1) % 3
                Tk += [[a, a + 3, b], [b, a + 3, b + 3]]
        tip = len(Pk) - 1
        for k in range(3):
            Tk.append([nseg * 3 + k, tip, nseg * 3 + (k + 1) % 3])
        allP.append(Pk); allT.append(np.array(Tk) + nv); nv += len(Pk)
        c = needle * np.exp(rng.normal(0, 0.06, 3)) * rng.uniform(0.85, 1.1)
        allC.append(np.repeat(c[None], len(Pk), 0)); allW.append(np.full(len(Pk), w))
    P = np.concatenate(allP)
    near.add(P, np.concatenate(allT), np.concatenate([np.concatenate(allC), ao_fn(P)[:, None]], 1),
             np.stack([np.ones(len(P)), np.concatenate(allW)], 1))
    # Far: a jagged cone of needles over the live crown, in tiers.
    tiers = 7
    for k in range(tiers):
        y0 = live_base + (h - live_base) * k / tiers
        y1 = live_base + (h - live_base) * (k + 1.6) / tiers
        R0 = R_max * (1 - (y0 - live_base) / (h - live_base)) + 0.2
        segs = 9
        a = np.arange(segs) / segs * 2 * np.pi + rng.uniform(0, 1)
        ring = np.stack([np.cos(a) * R0 * rng.uniform(0.85, 1.1, segs), np.full(segs, y0 - 0.25 * R0),
                         np.sin(a) * R0 * rng.uniform(0.85, 1.1, segs)], 1)
        Pt = np.vstack([ring, [[0, min(y1, h), 0]], [[0, y0 + 0.1, 0]]])
        Tt = [[s, segs, (s + 1) % segs] for s in range(segs)] + [[s, (s + 1) % segs, segs + 1] for s in range(segs)]
        c = needle * np.exp(rng.normal(0, 0.04, 3)) * 0.8
        far.add(Pt, Tt, np.concatenate([np.repeat(c[None], len(Pt), 0), ao_fn(Pt)[:, None] * 0.9], 1),
                np.stack([np.ones(len(Pt)), np.full(len(Pt), 0.3)], 1))
    return near, far, {"shoots": len(shoots), "height": h}


def make(name, seed):
    return spruce(seed) if name == "spruce" else broadleaf(name, seed)
