"""Britain's trees as real geometry for Nanite, all our own (no scans): ash, sycamore, oak, beech, alder,
willow, birch, rowan, hawthorn, hazel and holly, and the conifers Scots pine, Sitka spruce and larch.

Used by Tools/make_trees.py (the trees), Tools/make_hedges.py (field hedges) and Tools/make_gardens.py
(garden shrubs and hedges). Each broadleaf is grown by space colonisation (the branches grow towards
points scattered through the species' crown shape, so they fill it the way real ones do), its limbs
made as tubes thickened by the pipe model, and its leaves as small opaque polygons (no alpha cut-outs,
which Nanite draws slowly), clustered on the twig ends. A far version of each tree is a few lumpy
masses of leaves on its trunk and main limbs, for trees seen from more than about 200 m.

[Nov 20 2026] beenster: "the original trees you created had a sort of corkscrew trunk with gaps in,
please make sure you can make them look solid". The bark was made one segment at a time, each with a
ring of its own turned to suit that segment alone, so the rings twisted from one segment to the next
(the corkscrew) and two segments meeting at a bend didn't share a ring (the gaps). Now each limb is one
tube: its nodes smoothed, one ring per node turned along the limb by parallel transport (no twist),
shared by the segments either side of it (no gaps); a branch starts inside its parent limb, and a limb
ends in a point, never an open end. And the seasons: every leaf carries what the material (M_Tree)
needs to turn it in autumn and drop it in winter, evergreens left as they are.

Frames: metres, x east, y up, z south (as glTF and the rest of Data/Buttertubs). The tree's base is at
the origin and its trunk goes 1.2 m below it, so it can be sunk into a slope without its foot showing.

Vertex data (M_Tree reads it all):
    COLOR_0     RGB linear colour, A how much sky the spot sees (dark inside the crown)
    TEXCOORD_0  x what it is: 0 bark, 1 a deciduous leaf, 2 an evergreen leaf; y how free the spot is
                to move in the wind (0 at the trunk, 1 at the outer twigs)
    TEXCOORD_1  bark: the bark texture's (around and along the branch, in metres; BARK_M a repeat).
                leaves: x, y of the way from the vertex to its leaf's middle (metres, glTF frame)
    TEXCOORD_2  leaves: z of that way; and the leaf's turn to fall (0 first .. 1 last; -1 on a far
                model's leaf mass, which greys in winter instead of falling)
    TEXCOORD_3  leaves: the autumn colour (0 yellow, 1/3 orange, 2/3 red, 1 brown) and the share of
                dead leaves the tree holds through winter (oak and beech hold some)
"""
import math
import numpy as np


def lin(rgb):
    return np.power(np.asarray(rgb, np.float64) / 255.0, 2.2)


class Mesh:
    def __init__(self):
        self.P, self.C, self.UV, self.UV1, self.UV2, self.UV3, self.T = [], [], [], [], [], [], []
        self.n = 0

    def add(self, pos, tris, col, uv, uv1=None, uv2=None, uv3=None):
        pos = np.asarray(pos, np.float64).reshape(-1, 3)
        col = np.asarray(col, np.float64)
        if col.ndim == 1:
            col = np.repeat(col[None], len(pos), 0)
        uv = np.asarray(uv, np.float64)
        if uv.ndim == 1:
            uv = np.repeat(uv[None], len(pos), 0)
        z = lambda a: np.zeros((len(pos), 2)) if a is None else np.broadcast_to(np.asarray(a, np.float64), (len(pos), 2)).copy()
        self.P.append(pos); self.C.append(col); self.UV.append(uv)
        self.UV1.append(z(uv1)); self.UV2.append(z(uv2)); self.UV3.append(z(uv3))
        self.T.append(np.asarray(tris, np.int64).reshape(-1, 3) + self.n)
        self.n += len(pos)

    def uv1(self):
        return np.concatenate(self.UV1)

    def uv2(self):
        return np.concatenate(self.UV2)

    def uv3(self):
        return np.concatenate(self.UV3)

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
        if weld and len(bark):
            key = np.round(P[bark] * 1e4).astype(np.int64)
            _, inv = np.unique(key, axis=0, return_inverse=True)
            S = np.zeros((inv.max() + 1, 3))
            np.add.at(S, inv.ravel(), N[bark])
            N[bark] = S[inv.ravel()]
        N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
        rgba = np.clip(np.round(C * 255), 0, 255).astype(np.uint8)
        return P, T, N, rgba, UV

    def season_cut(self, upto):
        """[build 167] This mesh with the leaves that have fallen by a leaf drop of upto taken out (a
        deciduous leaf, UV0 x 1, whose turn to fall, UV2 y, is in (0, min(upto, 1 - its keep, UV3 y)]):
        the game draws it in place of the full one while that many leaves are down, as Nanite's
        simplified clusters, past a few tens of metres, can't drop a leaf by moving it. None when no
        leaf falls."""
        out = Mesh()
        cut = False
        base = 0
        for P, T, C, UV, U1, U2, U3 in zip(self.P, self.T, self.C, self.UV, self.UV1, self.UV2, self.UV3):
            t = T - base
            base += len(P)
            gone = (np.abs(UV[:, 0] - 1.0) < 0.5) & (U2[:, 1] > 0) & (U2[:, 1] <= np.minimum(upto, 1.0 - U3[:, 1]))
            keep_t = ~gone[t].all(axis=1)
            if not keep_t.all():
                cut = True
            t = t[keep_t]
            if not len(t):
                continue
            used = np.unique(t)
            remap = np.full(len(P), -1, np.int64); remap[used] = np.arange(len(used))
            out.add(P[used], remap[t], C[used], UV[used], U1[used], U2[used], U3[used])
        return out if cut and out.n else None

    def write(self, path, name, write_glb):
        P, T, N, C, UV = self.arrays(weld=True)
        write_glb(path, P, T, N, C, name, UV, self.uv1(), self.uv2(), self.uv3())
        return P, T


# --- Species ---------------------------------------------------------------------------------------

# height: the model's height (m); instances scale it. crown: the crown's base and widest point as a
# share of the height, its radius as a share of the height, and its shape (1 an ellipsoid, above 1
# flatter-topped, below 1 more pointed). leaf: shape, size (m), leaves per twig end, colour (sRGB) and
# its spread. bark: colour and its spread. lean: how far the crown leans downwind (to the north east,
# the mesh's +x) as a share of the height. season: (1 deciduous or 2 evergreen, its autumn colour 0
# yellow .. 1 brown, the share of dead leaves it holds through winter). stems: how many stems from the
# ground (hazel and willow grow as stools). jitter: how crooked its limbs (oak's are). weep: how far its
# outer shoots hang (willow, birch). bark_top: (colour, from what share of the height) for the Scots
# pine's orange upper trunk.
SPECIES = {
    "ash": dict(height=15.0, crown=(0.2, 0.55, 0.42, 1.3), trunk_r=0.022, attract=9000, step=0.3, infl=1.7, kill=0.45,
                up=0.10, droop=0.06, leaf=("pinnate", 0.09, 4, (104, 128, 58), 0.09), bark=((88, 86, 78), 0.07),
                lean=0.02, sparse=0.55, far_blobs=12, leaf_depth=2, season=(1, 0.05, 0.0)),
    "sycamore": dict(height=15.0, crown=(0.22, 0.55, 0.42, 1.4), trunk_r=0.026, attract=8000, step=0.3, infl=1.6,
                     kill=0.45, up=0.05, droop=0.02, leaf=("palmate", 0.095, 10, (66, 92, 38), 0.08),
                     bark=((80, 72, 62), 0.08), lean=0.02, sparse=0.8, far_blobs=12, season=(1, 0.3, 0.0)),
    "oak": dict(height=16.0, crown=(0.17, 0.5, 0.55, 1.5), trunk_r=0.03, attract=9000, step=0.3, infl=1.8, kill=0.45,
                up=0.0, droop=0.0, jitter=0.22, leaf=("oak", 0.085, 5, (60, 84, 34), 0.08), bark=((80, 74, 64), 0.07),
                lean=0.03, sparse=0.35, far_blobs=14, lobes=9, season=(1, 0.9, 0.06)),
    "beech": dict(height=18.0, crown=(0.24, 0.55, 0.45, 1.2), trunk_r=0.026, attract=9000, step=0.3, infl=1.7, kill=0.42,
                  up=0.08, droop=0.08, leaf=("oval", 0.07, 8, (76, 104, 40), 0.07), bark=((132, 132, 126), 0.04),
                  lean=0.02, sparse=0.25, far_blobs=12, season=(1, 0.5, 0.12)),
    "alder": dict(height=11.0, crown=(0.2, 0.45, 0.3, 0.85), trunk_r=0.02, attract=6500, step=0.26, infl=1.4, kill=0.38,
                  up=0.16, droop=0.02, leaf=("round", 0.065, 12, (54, 76, 34), 0.07), bark=((56, 50, 44), 0.08),
                  lean=0.0, sparse=0.4, far_blobs=10, season=(1, 1.0, 0.0)),
    "willow": dict(height=12.0, crown=(0.14, 0.5, 0.5, 1.0), trunk_r=0.03, attract=7000, step=0.28, infl=1.5, kill=0.4,
                   up=0.14, droop=0.1, weep=0.45, stems=2, leaf=("lance", 0.075, 12, (104, 128, 82), 0.08),
                   bark=((86, 80, 70), 0.08), lean=0.06, sparse=0.4, far_blobs=10, season=(1, 0.08, 0.0)),
    "birch": dict(height=12.0, crown=(0.28, 0.5, 0.25, 0.9), trunk_r=0.016, attract=6000, step=0.26, infl=1.4, kill=0.36,
                  up=0.14, droop=0.12, weep=0.18, leaf=("diamond", 0.045, 15, (108, 132, 54), 0.08),
                  bark=((168, 162, 152), 0.06), lean=0.0, sparse=0.3, far_blobs=10, season=(1, 0.0, 0.0)),
    "rowan": dict(height=8.0, crown=(0.3, 0.55, 0.3, 1.0), trunk_r=0.02, attract=5000, step=0.22, infl=1.2, kill=0.32,
                  up=0.18, droop=0.04, leaf=("pinnate", 0.055, 5, (82, 110, 46), 0.08), bark=((112, 106, 98), 0.06),
                  lean=0.02, sparse=0.45, far_blobs=8, leaf_depth=2, season=(1, 0.67, 0.0)),
    "hawthorn": dict(height=5.0, crown=(0.14, 0.5, 0.5, 1.6), trunk_r=0.024, attract=5000, step=0.13, infl=0.7, kill=0.2,
                     up=0.02, droop=0.0, jitter=0.18, leaf=("diamond", 0.042, 15, (60, 82, 36), 0.08),
                     bark=((62, 54, 46), 0.08), lean=0.16, sparse=0.5, far_blobs=8, season=(1, 0.6, 0.0)),
    "hazel": dict(height=6.0, crown=(0.06, 0.5, 0.45, 1.1), trunk_r=0.011, attract=5000, step=0.18, infl=1.0, kill=0.28,
                  up=0.24, droop=0.06, stems=7, leaf=("round", 0.07, 8, (74, 98, 40), 0.08), bark=((104, 82, 62), 0.07),
                  lean=0.03, sparse=0.35, far_blobs=8, season=(1, 0.05, 0.0)),
    "holly": dict(height=7.0, crown=(0.08, 0.38, 0.27, 0.7), trunk_r=0.02, attract=6000, step=0.18, infl=1.0, kill=0.26,
                  up=0.12, droop=0.02, leaf=("holly", 0.06, 9, (26, 46, 22), 0.06), bark=((112, 110, 100), 0.05),
                  lean=0.0, sparse=0.1, far_blobs=8, season=(2, 0.0, 0.0)),
    "pine": dict(height=18.0, crown=(0.55, 0.8, 0.3, 1.6), trunk_r=0.021, attract=7000, step=0.35, infl=1.9, kill=0.5,
                 up=0.04, droop=0.03, jitter=0.16, leaf=("tuft", 0.11, 8, (46, 70, 44), 0.06), bark=((96, 74, 60), 0.07),
                 bark_top=((150, 84, 50), 0.55), high_crown=True, lean=0.05, sparse=0.7, far_blobs=8, leaf_depth=2, lobes=5,
                 season=(2, 0.0, 0.0)),
    "spruce": dict(height=18.0, trunk_r=0.017, leaf=(None, 0.0, 0, (36, 56, 40), 0.06), bark=((70, 56, 46), 0.08),
                   far_blobs=0, season=(2, 0.0, 0.0), droop=1.0, needle_r=0.075),
    "larch": dict(height=17.0, trunk_r=0.016, leaf=(None, 0.0, 0, (96, 136, 58), 0.07), bark=((92, 70, 58), 0.08),
                  far_blobs=0, season=(1, 0.12, 0.0), droop=0.4, needle_r=0.07, open=0.8),
}

# The autumn colours the material blends between (linear albedo; M_Tree's season node has the same).
AUTUMN = [(0.62, 0.48, 0.06), (0.55, 0.30, 0.05), (0.55, 0.12, 0.03), (0.30, 0.17, 0.04)]


def season_of(sp):
    return sp.get("season", (1, 0.6, 0.0))


def envelope(sp, rng, n):
    """n attraction points in the crown: a union of several clumps (one per big limb), of different
    sizes, around the crown's centre and some hanging low on one side, so the outline is ragged and
    lopsided as a real field tree's is, not a ball on a stick. Mostly near each clump's surface,
    where the leaves are."""
    h = sp["height"]
    c0, cw, cr, flat = sp["crown"]
    y0 = c0 * h
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
    pts = []
    vols = np.array([np.prod(r) for _, r in clumps])
    counts = np.maximum((n * 1.2 * vols / vols.sum()).astype(int), 20)
    for (c, r), m in zip(clumps, counts):
        q = rng.normal(0, 1, (m, 3)); q /= np.linalg.norm(q, axis=1, keepdims=True)
        q *= rng.uniform(0, 1, (m, 1)) ** 0.25           # mostly near the surface
        pts.append(c + q * r)
    P = np.concatenate(pts)
    P = P[P[:, 1] > y0 * 0.7]
    # A weeping crown: its outer points hang, more so the further out.
    if sp.get("weep", 0) > 0:
        rr = np.linalg.norm(P[:, [0, 2]], axis=1) / max(R, 1e-6)
        P[:, 1] -= sp["weep"] * hv * np.clip(rr - 0.4, 0, None) ** 1.5
        P = P[P[:, 1] > 0.4]
    # The downwind lean: the crown sits a little to +x, more so higher up.
    P[:, 0] += sp["lean"] * h * np.clip((P[:, 1] - y0) / max(h - y0, 1e-6), 0, 1)
    P = P[rng.permutation(len(P))[:n]]
    # Gaps: a few holes right through, so the sky shows through the crown in places.
    for _ in range(int(sp["sparse"] * 5)):
        c = P[rng.integers(len(P))]
        P = P[np.linalg.norm(P - c, axis=1) > rng.uniform(0.2, 0.38) * R]
    return P


# [Nov 20, build 166] beenster: "The trees look a bit to similar, a trunk with all branches popping out.
# They should have large branches coming out along their length". The crown used to grow from the top of
# a bare stem, so every limb left one point. Now the trunk runs on up into the crown as a leader and the
# great limbs leave it one above another, each kind its own way, before the twigs fill the crown around
# them: (leader: how far up the trunk runs, of the height; limbs: (fewest, most); angle: from the vertical,
# degrees, lowest limb first, top limb last; reach: how far out a limb goes, of the crown's radius; rise:
# how much a limb curves up towards its end; low: the share of the crown's depth limbs start in, from its base).
FORMS = {
    # oak: a short trunk, then a few great limbs spreading wide and low, rising at their ends
    "oak": dict(leader=0.55, limbs=(5, 7), angle=(75, 55), reach=0.7, rise=0.35, low=0.8),
    # ash: upswept limbs from a long trunk, curving up at the tips
    "ash": dict(leader=0.7, limbs=(5, 7), angle=(55, 30), reach=0.65, rise=0.35, low=0.85),
    # beech: a tall smooth trunk dividing into steep forks
    "beech": dict(leader=0.65, limbs=(4, 6), angle=(42, 22), reach=0.7, rise=0.25, low=0.8),
    # sycamore: a broad dome on stout limbs at middling angles
    "sycamore": dict(leader=0.6, limbs=(5, 7), angle=(65, 38), reach=0.8, rise=0.3, low=0.8),
    # alder, birch: a leader to near the top, many lighter limbs along it
    "alder": dict(leader=0.88, limbs=(7, 10), angle=(70, 45), reach=0.6, rise=0.2, low=0.9),
    "birch": dict(leader=0.9, limbs=(6, 9), angle=(55, 35), reach=0.55, rise=0.15, low=0.9),
    "rowan": dict(leader=0.7, limbs=(4, 6), angle=(50, 30), reach=0.65, rise=0.3, low=0.85),
    "hawthorn": dict(leader=0.5, limbs=(4, 6), angle=(70, 45), reach=0.75, rise=0.2, low=0.8),
    "holly": dict(leader=0.9, limbs=(7, 10), angle=(70, 50), reach=0.55, rise=0.15, low=0.95),
    # Scots pine: a long bare stem, a few near-level limbs in the high crown
    "pine": dict(leader=0.9, limbs=(4, 6), angle=(80, 60), reach=0.7, rise=0.2, low=0.9),
}


def scaffold(sp, rng, nodes, parent, last, c0):
    """The leader and the great limbs along it (FORMS): nodes and parents added in place; the leader's
    top node returned."""
    f = sp["form"]
    h = sp["height"]
    R = sp["crown"][2] * h
    step = sp["step"]
    top = max(c0 + step * 3, f["leader"] * h)
    # the leader, wandering a little
    trunk = [(nodes[last][1], last)]
    wob = rng.normal(0, 0.03, 2)
    y = nodes[last][1]
    while y < top - 1e-6:
        y = min(y + step, top)
        q = nodes[last].copy(); q[1] = y
        q[0] += wob[0] * step; q[2] += wob[1] * step
        nodes.append(q); parent.append(last); last = len(nodes) - 1
        trunk.append((y, last))
    lo, hi = f["limbs"]
    n = int(rng.integers(lo, hi + 1))
    span = (top - c0) * f["low"]
    az0 = rng.uniform(0, 2 * np.pi)
    for i in range(n):
        t = (i + rng.uniform(0.15, 0.85)) / n
        ly = c0 + span * t
        at = min(trunk, key=lambda e: abs(e[0] - ly))[1]
        a = math.radians(f["angle"][0] + (f["angle"][1] - f["angle"][0]) * t + rng.normal(0, 6))
        az = az0 + i * 2.39996 + rng.normal(0, 0.35)     # (the golden angle round the trunk)
        d = np.array([math.sin(a) * math.cos(az), math.cos(a), math.sin(a) * math.sin(az)])
        # (the lowest limbs reach furthest out; the top ones are short)
        L = f["reach"] * R * (1.15 - 0.45 * t) * rng.uniform(0.85, 1.15) / max(math.sin(a), 0.45)
        L = min(L, (h - nodes[at][1]) / max(math.cos(a), 0.2) * 0.9)
        p_, q = at, nodes[at].copy()
        base = nodes[at].copy()
        for j in range(max(2, int(L / step))):
            d = d + np.array([0.0, f["rise"] * step / max(L, 1e-6) * 2.0, 0.0]) + rng.normal(0, 0.09, 3)
            d /= np.linalg.norm(d)
            q = q + d * step
            # (a limb stays well inside the crown: the twigs grow on out from it)
            if j > 2 and (np.hypot(q[0] - base[0], q[2] - base[2]) > 0.7 * R or q[1] > 0.88 * h):
                break
            nodes.append(q.copy()); parent.append(p_); p_ = len(nodes) - 1
    return last


def colonise(sp, rng):
    """Space colonisation from the top of the stem(s): nodes (n, 3) and their parents (-1 for the root).
    One stem (a trunk) or several from the same root (a stool: hazel, willow)."""
    h = sp["height"]
    A = envelope(sp, rng, int(sp["attract"] * 1.3))
    step, infl, kill = sp["step"], sp["infl"], sp["kill"]
    jitter = sp.get("jitter", 0.12)
    c0 = sp["crown"][0] * h
    nodes = [np.array([0.0, -1.2, 0.0])]
    parent = [-1]
    stems = int(sp.get("stems", 1))
    top = max(c0, min(0.8, 0.2 * h) if stems > 1 else c0)
    for s in range(stems):
        # Each stem leans out from the stool a little (a lone trunk barely), and bends gently.
        a = rng.uniform(0, 2 * np.pi) if stems > 1 else 0.0
        splay = rng.uniform(0.12, 0.3) if stems > 1 else 0.0
        bend = rng.normal(0, 0.02, 2)
        y = -1.2
        last = 0
        while y < top - 1e-6:
            y = min(y + step, top)
            f = (y + 1.2) / (top + 1.2)
            out = splay * max(y, 0.0) * np.array([math.cos(a), math.sin(a)])
            nodes.append(np.array([bend[0] * f * h * 0.1 + sp["lean"] * h * 0.2 * f ** 2 + out[0], y,
                                   bend[1] * f * h * 0.1 + out[1]]))
            parent.append(last)
            last = len(nodes) - 1
            if stems > 1 and y < 0:
                # (the stems share the stool below ground)
                pass
    if stems == 1 and sp.get("form"):
        last = scaffold(sp, rng, nodes, parent, last, c0)
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

    # (only the stems' tops above the crown's base grow at first: a bare trunk below it)
    update([i for i in range(len(N)) if N[i][1] >= c0 * 0.8 or (stems > 1 and N[i][1] > 0.5)])
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
        np.add.at(acc, inv.ravel(), dirs)
        acc /= np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-9)
        acc[:, 1] += sp["up"] - sp["droop"] * (N[uniq, 1] / h)
        acc += rng.normal(0, jitter, acc.shape)
        acc /= np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-9)
        new = N[uniq] + acc * step
        start = len(N)
        N = np.vstack([N, new])
        parent.extend(uniq.tolist())
        update(list(range(start, len(N))))
    return N, np.array(parent)


def _kids(parent):
    kids = [[] for _ in range(len(parent))]
    for i in range(len(parent)):
        if parent[i] >= 0:
            kids[parent[i]].append(i)
    return kids


def radii(N, parent, tip_r, trunk_r_base):
    """Pipe model: r^2.4 of a branch is the sum of its children's, from tip_r at the twig ends."""
    n = len(N)
    kids = _kids(parent)
    order = np.argsort(-_depths(parent))
    r = np.zeros(n)
    e = 2.2
    for i in order:
        if not kids[i]:
            r[i] = tip_r
        else:
            r[i] = sum(r[k] ** e for k in kids[i]) ** (1 / e)
    # Scale so the trunk at its base is the species' size, keeping the twigs at tip_r: by a power of the
    # radius (r / tip_r) ** g, not a straight stretch, which made every middling branch a fat sausage
    # ending in a blunt stub (one of beenster's "corkscrew trunk with gaps" trees' faults).
    r0 = r.max()
    if r0 > tip_r * 1.01 and trunk_r_base > tip_r:
        g = math.log(trunk_r_base / tip_r) / math.log(r0 / tip_r)
        r = tip_r * (r / tip_r) ** g
    return r, kids


def _depths(parent):
    d = np.zeros(len(parent), int)
    for i in range(len(parent)):
        if parent[i] >= 0:
            d[i] = d[parent[i]] + 1
    return d


def primary_child(kids, r):
    return np.array([max(k, key=lambda j: r[j]) if k else -1 for k in kids])


def smooth_limbs(N, parent, kids, r, iters=6):
    """Each limb's nodes pulled towards the line through their neighbours along it, the thick ones
    most, so a trunk and its big limbs run smooth instead of zigzagging node to node (one cause of the
    corkscrew look). The roots and the twig ends stay put."""
    N = N.copy()
    prim = primary_child(kids, r)
    idx = np.array([i for i in range(len(N)) if parent[i] >= 0 and prim[i] >= 0])
    if not len(idx):
        return N
    par = parent[idx]; nxt = prim[idx]
    w = np.clip(r[idx] / 0.03, 0.25, 1.0) * 0.5
    for _ in range(iters):
        mid = 0.5 * (N[par] + N[nxt])
        N[idx] += (mid - N[idx]) * w[:, None]
    return N


def smooth_radii(N, parent, kids, r):
    """[build 168] beenster on 167: "the trunks of the trees need to narrow much more smoothly with
    height - I could see 'shoulders' in their profiles". The pipe model narrows a trunk in a step
    wherever a big limb leaves it. Along each limb (the trunk first, then each limb before its own
    limbs) each step down is spread into a ramp below it, longer the thicker the limb, kept narrowing
    upwards, and a side limb starts no thicker than 85% of what it leaves."""
    r = r.copy()
    prim = primary_child(kids, r)
    starts = [i for i in range(len(N)) if parent[i] < 0 or prim[parent[i]] != i]
    depth = _depths(parent)
    starts.sort(key=lambda i: depth[i])
    for s0 in starts:
        run = [s0]
        while prim[run[-1]] >= 0:
            run.append(prim[run[-1]])
        if len(run) < 3:
            continue
        R = r[run]
        L = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(N[run], axis=0), axis=1))])
        # (each step down becomes a ramp: going back down the limb from its end, the radius may grow
        # by at most a rate, in log per metre, that is gentler the thicker the limb, so the trunk starts
        # narrowing well below where a big limb leaves it instead of stepping in at that point)
        # and the same the other way, the radius narrowing at most at that rate going up, so a step
        # is spread half below and half above where the limb leaves (the trunk keeps its girth)
        k = np.clip(0.08 / np.maximum(R, 0.02), 0.1, 3.0)
        dn = R.copy()
        for a in range(len(run) - 2, -1, -1):
            dn[a] = min(R[a], dn[a + 1] * math.exp(k[a] * (L[a + 1] - L[a])))
        up = R.copy()
        for a in range(1, len(run)):
            up[a] = max(R[a], up[a - 1] * math.exp(-k[a] * (L[a] - L[a - 1])))
        sm = np.sqrt(dn * up)
        sm[-1] = R[-1]
        if parent[s0] >= 0:
            sm = np.minimum(sm, r[parent[s0]] * 0.85)
        sm = np.minimum.accumulate(sm)    # (never thickening on the way out)
        r[run] = sm
    return r


def ring_frame(d):
    d = d / max(np.linalg.norm(d), 1e-9)
    a = np.array([1.0, 0, 0]) if abs(d[0]) < 0.9 else np.array([0, 0, 1.0])
    u = np.cross(d, a); u /= np.linalg.norm(u)
    v = np.cross(d, u)
    return u, v


def transported_frames(P):
    """Tangents and ring axes (u, v) along a polyline, carried from each point to the next by parallel
    transport, so the rings don't twist along a limb."""
    n = len(P)
    T = np.zeros((n, 3))
    for k in range(n):
        a = P[max(k - 1, 0)]; b = P[min(k + 1, n - 1)]
        d = b - a
        T[k] = d / max(np.linalg.norm(d), 1e-9)
    U = np.zeros((n, 3)); V = np.zeros((n, 3))
    U[0], V[0] = ring_frame(T[0])
    for k in range(1, n):
        u = U[k - 1] - T[k] * (T[k] @ U[k - 1])
        nu = np.linalg.norm(u)
        if nu < 1e-6:
            u, _ = ring_frame(T[k]); nu = 1.0
        U[k] = u / nu
        V[k] = np.cross(T[k], U[k])
    return T, U, V


BARK_M = 0.7   # metres one repeat of the bark texture covers (Data/Textures/textures.json "bark" tile_m)


def sides_for(r):
    return 20 if r > 0.2 else 14 if r > 0.08 else 9 if r > 0.03 else 6 if r > 0.012 else 4


def limbs(parent, kids, r):
    """The tree as limbs: each a run of nodes following the thickest child, starting at a root or at
    the node it branches from. (nodes along it, the radius at each)"""
    prim = primary_child(kids, r)
    starts = [i for i in range(len(parent)) if parent[i] < 0]
    starts += [k for i in range(len(parent)) for k in kids[i] if k != prim[i]]
    out = []
    for s in starts:
        seq = [] if parent[s] < 0 else [parent[s]]
        k = s
        while k >= 0:
            seq.append(k)
            k = prim[k]
        rad = [r[s] if j == 0 and parent[s] >= 0 else r[seq[j]] for j in range(len(seq))]
        out.append((seq, rad))
    return out


def tubes(mesh, N, parent, r, kids, bark, rng, wind, ao_fn, min_r=0.006, flare=True, bark_top=None, coarse=1):
    """Bark: each limb one tube, a ring of vertices at each of its nodes (turned along the limb by
    parallel transport) joined to the next; fewer sides as it thins, the thinner tube starting a node
    back inside the thicker so no gap shows; a side limb starting at the middle of its parent limb, and
    every limb ending in a point (coarse: a far model's, with 1/coarse the sides). UV1 is the bark texture's: around the limb (a whole number of
    repeats, so it wraps without a seam) and along it, in metres from the foot of the tree."""
    n = len(N)
    base_col, spread = bark
    col0 = lin(base_col)
    # One bark colour and one ring phase for the whole tree: a fresh colour and a turned ring on
    # every segment made the trunk read as a stack of bands (beenster: "very low poly").
    cv_tree = col0 * np.exp(rng.normal(0, spread, 3))
    # Root flare: buttresses where the roots leave the trunk, a few per tree, spreading at the ground.
    lobes = int(rng.integers(4, 7)); lobe_ph = rng.uniform(0, 6.3)
    along = np.zeros(n)
    for i in range(n):
        if parent[i] >= 0:
            along[i] = along[parent[i]] + np.linalg.norm(N[i] - N[parent[i]])
    h_top = max(N[:, 1].max(), 1.0)
    for seq, rad in limbs(parent, kids, r):
        # Cut the limb where it gets thinner than min_r (it ends in a point there).
        keep = 0
        while keep < len(seq) and rad[keep] >= min_r:
            keep += 1
        if keep < 2:
            continue
        seq = seq[:keep]; rad = np.array(rad[:keep])
        P = N[seq]
        T, U, V = transported_frames(P)
        # Runs of one side count: a new, thinner run starts one node back, inside the end of the last.
        runs = []
        s0 = 0
        side_n = lambda rr: max(3, sides_for(rr) // coarse)
        cur = side_n(rad[0])
        for j in range(1, len(seq)):
            sj = side_n(rad[j])
            if sj < cur and j - 1 > s0:
                runs.append((s0, j, cur))
                s0 = j - 1; cur = sj
        runs.append((s0, len(seq) - 1, cur))
        for ri, (a0, a1, sides) in enumerate(runs):
            last = ri == len(runs) - 1
            ang = np.arange(sides + 1) / sides * 2 * np.pi   # the last vertex repeats the first: the seam
            reps = max(1, int(round(2 * np.pi * rad[a0] / BARK_M)))
            rings, uvs, ws = [], [], []
            for j in range(a0, a1 + 1):
                rr = np.full(sides + 1, rad[j])
                q = P[j]
                if flare and parent[seq[0]] < 0 and q[1] < 1.4 and rad[j] > 0.08:
                    f = np.clip((1.4 - q[1]) / 1.4, 0, 1.3)
                    rr = rad[j] * (1 + 0.85 * f ** 2.2 * (1 + 0.5 * np.cos(ang * lobes + lobe_ph)))
                rings.append(q[None] + np.cos(ang)[:, None] * U[j][None] * rr[:, None] + np.sin(ang)[:, None] * V[j][None] * rr[:, None])
                uvs.append(np.stack([np.arange(sides + 1) / sides * reps * BARK_M, np.full(sides + 1, along[seq[j]])], 1))
                ws.append(np.full(sides + 1, wind[seq[j]]))
            if last:
                # The point the limb ends in, half a radius on.
                tip = P[-1] + T[-1] * max(rad[-1] * 2.0, 0.01)
                rings.append(np.repeat(tip[None], sides + 1, 0))
                uvs.append(np.stack([np.arange(sides + 1) / sides * reps * BARK_M, np.full(sides + 1, along[seq[-1]] + 0.02)], 1))
                ws.append(np.full(sides + 1, wind[seq[-1]]))
            Pr = np.vstack(rings)
            m = sides + 1
            k = np.arange(sides)
            tri = []
            for row in range(len(rings) - 1):
                a, b, c, e = k + row * m, k + 1 + row * m, k + (row + 1) * m, k + 1 + (row + 1) * m
                tri.append(np.stack([a, c, b], 1)); tri.append(np.stack([b, c, e], 1))
            Tr = np.concatenate(tri)
            # Colour: the species' bark, greened by algae low down on the wet (south west) side.
            C = np.repeat(cv_tree[None], len(Pr), 0)
            if bark_top is not None:
                # (the Scots pine: grey-brown and plated low down, orange from part way up)
                tc, frac = bark_top
                t = np.clip((Pr[:, 1] / h_top - frac) / 0.15, 0, 1)
                C = C * (1 - t[:, None]) + lin(tc)[None] * t[:, None]
            low = np.clip(1 - Pr[:, 1] / 3.0, 0, 1)
            cx = np.repeat(np.vstack([P[a0:a1 + 1]] + ([P[-1:]] if last else [])), m, 0)
            sw = np.clip(-(Pr[:, 0] - cx[:, 0]) + (Pr[:, 2] - cx[:, 2]), 0, None) / max(rad[a0], 1e-3)
            alg = np.clip(low * (0.35 + 0.4 * np.clip(sw, 0, 1)), 0, 0.7)
            C = C * (1 - alg[:, None]) + lin((70, 92, 44))[None] * alg[:, None]
            C = C * np.clip(1 - 0.5 * np.clip(-Pr[:, 1], 0, 1), 0.4, 1)[:, None]   # soil-dark below ground
            if base_col[0] > 140:   # birch: black marks on the white bark, black at the foot
                C = C * np.where(rng.uniform(0, 1, (len(Pr), 1)) < 0.22, 0.3, 1.0)
                C = C * np.clip(0.35 + Pr[:, 1:2] / 1.5, 0.35, 1)
            ao = ao_fn(Pr)
            mesh.add(Pr, Tr, np.concatenate([C, ao[:, None]], 1), np.stack([np.zeros(len(Pr)), np.concatenate(ws)], 1),
                     np.concatenate(uvs))


def _outline(pts, centre=False):
    """A leaf's outline (along, across), base first, going round, and its triangles: fanned from the
    base, or (centre: lobed and spiky outlines, which a fan from the base would overlap) from a middle
    vertex added at the end."""
    pts = np.asarray(pts, np.float64)
    m = len(pts)
    if not centre:
        return pts, [[0, k, k + 1] for k in range(1, m - 1)]
    tris = [[m, (k + 1) % m, k] for k in range(m)]
    return np.vstack([pts, [[0.48, 0.0]]]), tris


def _mirror(half):
    """A symmetric outline from its left side (base to tip, across > 0)."""
    half = [tuple(p) for p in half]
    right = [(x, -y) for x, y in reversed(half[1:-1])]
    return half + right


LEAF_SHAPES = {
    # 2D outlines (along, across) of one leaf, base at (0, 0), length 1.
    "diamond": _outline(_mirror([(0, 0), (0.45, 0.32), (1, 0)])),
    "round": _outline(_mirror([(0, 0), (0.3, 0.38), (0.75, 0.42), (1, 0)])),
    "palmate": _outline(_mirror([(0, 0), (0.2, 0.62), (0.55, 0.45), (0.8, 0.5), (1, 0)])),
    "leaflet": _outline(_mirror([(0, 0), (0.5, 0.16), (1, 0)])),
    # (an oak's: round lobes along each side)
    "oak": _outline(_mirror([(0, 0), (0.3, 0.26), (0.62, 0.22), (0.82, 0.28), (1, 0)])),
    # (a beech's: a neat oval, a little pointed)
    "oval": _outline(_mirror([(0, 0), (0.3, 0.3), (0.75, 0.24), (1, 0)])),
    # (a willow's: long and narrow)
    "lance": _outline(_mirror([(0, 0), (0.2, 0.08), (0.6, 0.1), (1, 0)])),
    # (a holly's: wavy, with spines)
    "holly": _outline(_mirror([(0, 0), (0.22, 0.34), (0.36, 0.2), (0.55, 0.36), (0.72, 0.18), (0.85, 0.28), (1, 0)]), True),
}


def _season_uvs(P, pivots, rank, sp):
    """Each leaf's season data (see the docstring): the way to its middle, its turn to fall, the
    species' autumn colour and how much it holds through winter."""
    kind, hue, keep = season_of(sp)
    off = pivots - P
    uv1 = off[:, :2]
    uv2 = np.stack([off[:, 2], rank], 1)
    uv3 = np.tile([hue, keep], (len(P), 1))
    return float(kind), uv1, uv2, uv3


def leaf_cluster(mesh, at, out, sp, rng, wind_w, ao_fn, scale=1.0):
    """The leaves at one twig end: `at` the twig's end, `out` the way it points."""
    shape, size, count, col, spread = sp["leaf"]
    size *= scale
    base = lin(col)
    allP, allT, allC, allPiv = [], [], [], []
    nv = 0
    # (when this clump's leaves fall: a little apart leaf by leaf, mostly the clump as one)
    clump_rank = rng.uniform(0.02, 1.0)
    ranks = []
    if shape == "pinnate":
        # Ash and rowan: compound leaves, each a stalk of 4 pairs of leaflets and one at the tip.
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
            lr = np.clip(clump_rank + rng.normal(0, 0.12), 0.01, 1.0)
            for k in range(4):
                t = 0.3 + k * 0.22
                stem = at + d * L * t + np.array([0, -0.06, 0]) * L * t * t
                for sgn in ((1, -1) if k < 3 else (0,)):
                    ld = d * (0.35 if sgn else 1) + side * sgn
                    ld /= np.linalg.norm(ld)
                    ls = np.cross(normal, ld)
                    ln = size * rng.uniform(0.8, 1.15)
                    P = stem[None] + outline[:, :1] * ld[None] * ln + outline[:, 1:] * ls[None] * ln
                    P[1:-1:2] += normal * ln * 0.04   # a slight fold along the midrib
                    allP.append(P); allT.append(np.array(tris) + nv); nv += len(P)
                    allPiv.append(np.repeat(P.mean(0)[None], len(P), 0)); ranks.append(np.full(len(P), lr))
                    c = base * np.exp(rng.normal(0, spread, 3)) * rng.uniform(0.85, 1.12)
                    allC.append(np.repeat(c[None], len(P), 0))
    elif shape == "tuft":
        # Scots pine: the needles of a shoot's end as a little upturned brush, its tip pointing out
        # and up (bluish green, in tufts with bare twig between).
        for _ in range(count):
            d = out * 0.8 + np.array([0, 0.6, 0]) + rng.normal(0, 0.35, 3)
            d /= np.linalg.norm(d)
            u, v = ring_frame(d)
            L = size * rng.uniform(1.6, 2.4)
            p = at + rng.normal(0, size * 0.8, 3)
            nseg = 2
            Pk = []
            for s in range(nseg + 1):
                q = p + d * L * s / nseg
                rr = size * 0.55 * (1 - 0.5 * s / nseg)
                for k, a in enumerate((0.0, 1.57, 3.14, 4.71)):
                    jag = rr * (1.0 if (s + k) % 2 == 0 else 0.6)
                    Pk.append(q + (u * math.cos(a) + v * math.sin(a)) * jag)
            Pk.append(p + d * L * 1.15)
            Pk = np.array(Pk)
            Tk = []
            for s in range(nseg):
                for k in range(4):
                    a, b = s * 4 + k, s * 4 + (k + 1) % 4
                    Tk += [[a, a + 4, b], [b, a + 4, b + 4]]
            tip = len(Pk) - 1
            for k in range(4):
                Tk.append([nseg * 4 + k, tip, nseg * 4 + (k + 1) % 4])
            allP.append(Pk); allT.append(np.array(Tk) + nv); nv += len(Pk)
            allPiv.append(np.repeat(Pk.mean(0)[None], len(Pk), 0)); ranks.append(np.full(len(Pk), clump_rank))
            c = base * np.exp(rng.normal(0, spread, 3)) * rng.uniform(0.85, 1.1)
            allC.append(np.repeat(c[None], len(Pk), 0))
    else:
        outline, tris = LEAF_SHAPES[shape]
        mid = np.abs(outline[:, 1]) < 1e-9
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
            P[~mid] -= nrm * ln * 0.07   # cupped a little
            allP.append(P); allT.append(np.array(tris) + nv); nv += len(P)
            allPiv.append(np.repeat(P.mean(0)[None], len(P), 0))
            ranks.append(np.full(len(P), np.clip(clump_rank + rng.normal(0, 0.12), 0.01, 1.0)))
            c = base * np.exp(rng.normal(0, spread, 3)) * rng.uniform(0.85, 1.12)
            allC.append(np.repeat(c[None], len(P), 0))
    P = np.concatenate(allP); C = np.concatenate(allC)
    ao = ao_fn(P)
    kind, uv1, uv2, uv3 = _season_uvs(P, np.concatenate(allPiv), np.concatenate(ranks), sp)
    mesh.add(P, np.concatenate(allT), np.concatenate([C, ao[:, None]], 1),
             np.stack([np.full(len(P), kind), np.full(len(P), wind_w)], 1), uv1, uv2, uv3)
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


LEAF_SIZE, LEAF_COUNT = 1.45, 1.4


# [Nov 20, build 166] beenster: "none of them look very large or mature (an oak or ash tree is huge)". A
# veteran of each big broadleaf: the lone field or hedgerow tree of 22-28 m, its crown low and as broad as
# it is tall, the trunk a metre and more across, a few great limbs. Coarser steps and bigger leaf clusters
# keep it inside a big young tree's triangles. (multiplies the species' own numbers; crown: start, width, radius, flatness)
VETERAN = {"oak": dict(height=1.5, crown=(0.55, 1.0, 0.78, 1.15), trunk_r=1.35, attract=1.3, step=1.6, leaf=1.65, count=0.8, sparse=0.8),
           "ash": dict(height=1.55, crown=(0.75, 1.0, 0.95, 1.05), trunk_r=1.3, attract=1.25, step=1.6, leaf=1.65, count=0.8, sparse=1.0),
           "beech": dict(height=1.45, crown=(0.6, 1.0, 1.0, 1.1), trunk_r=1.3, attract=1.25, step=1.6, leaf=1.65, count=0.8, sparse=1.0),
           "sycamore": dict(height=1.5, crown=(0.65, 1.0, 1.0, 1.1), trunk_r=1.3, attract=1.25, step=1.6, leaf=1.65, count=0.8, sparse=1.0)}


def veteran_spec(name):
    """The species' numbers grown on to a veteran (VETERAN)."""
    sp = dict(SPECIES[name]); v = VETERAN[name]
    sp["height"] = sp["height"] * v["height"]
    c0, cw, cr, flat = sp["crown"]; m = v["crown"]
    sp["crown"] = (c0 * m[0], cw * m[1], cr * m[2], flat * m[3])
    sp["trunk_r"] = sp["trunk_r"] * v["trunk_r"]
    sp["attract"] = int(sp["attract"] * v["attract"])
    sp["step"] = sp["step"] * v["step"]
    sp["infl"] = sp["infl"] * v["step"]
    sp["kill"] = sp["kill"] * v["step"]
    sp["sparse"] = sp["sparse"] * v["sparse"]
    shape, size, count, col, spread = sp["leaf"]
    sp["leaf"] = (shape, size * v["leaf"], max(2, int(round(count * v.get("count", 1.0)))), col, spread * v["leaf"])
    sp["far_blobs"] = int(sp.get("far_blobs", 12) * 1.3)
    sp["veteran"] = True
    sp["kind"] = name
    return sp


def vary(name, rng, k=None, n=5):
    """A variant's own shape: its age (young: slim, high-crowned, upright; old: broad, low-limbed,
    thick-trunked), its crown lower or higher, wider or narrower, flatter or rounder, leaning one way,
    more or fewer big limbs, so a row of them isn't one tree repeated."""
    sp = dict(SPECIES[name]) if isinstance(name, str) else dict(name)
    age = (0.55 + 0.75 * k / max(n - 1, 1)) if k is not None else rng.uniform(0.55, 1.3)
    age = float(np.clip(age + rng.normal(0, 0.08), 0.5, 1.35))
    if sp.get("veteran"):
        age = 1.35 + rng.uniform(0.0, 0.1)
    c0, cw, cr, flat = sp["crown"]
    lift = rng.uniform(0.9, 1.1) if sp.get("high_crown") else rng.uniform(0.6, 1.15) * (1.35 - 0.4 * age)
    sp["crown"] = (float(np.clip(c0 * lift, 0.03, 0.75)),
                   min(cw * rng.uniform(0.85, 1.1), 0.8),
                   cr * rng.uniform(0.82, 1.18) * (0.62 + 0.38 * age),
                   flat * rng.uniform(0.75, 1.3))
    sp["trunk_r"] = sp["trunk_r"] * (0.75 + 0.3 * age)
    sp["up"] = sp["up"] + 0.08 * (1 - age)
    sp["jitter"] = sp.get("jitter", 0.12) * (0.8 + 0.3 * age)
    sp["lean"] = sp["lean"] + rng.uniform(0.0, 0.07)
    sp["lobes"] = int(rng.integers(max(3, sp.get("lobes", 7) - 3), sp.get("lobes", 7) + 3))
    sp["sparse"] = sp["sparse"] * rng.uniform(0.8, 1.5)
    if "stems" in sp:
        sp["stems"] = max(1, int(round(sp["stems"] * rng.uniform(0.6, 1.3))))
    sp["attract"] = int(sp["attract"] * (0.75 + 0.3 * age))
    sp["age"] = age
    kind = name if isinstance(name, str) else sp.get("kind")
    if kind in FORMS and "form" not in sp:
        sp["form"] = dict(FORMS[kind])
        if age < 0.8:   # (a young tree: its limbs fewer and steeper)
            sp["form"]["limbs"] = (max(2, FORMS[kind]["limbs"][0] - 2), max(3, FORMS[kind]["limbs"][1] - 2))
            sp["form"]["angle"] = tuple(a * 0.8 for a in FORMS[kind]["angle"])
    # (the leaves a little over life size and more of them, so a crown reads as a mass of leaves and not
    # a tangle of twigs: life-sized, a model with Nanite's budget of leaves is mostly holes)
    shape, size, count, col, spread = sp["leaf"]
    sp["leaf"] = (shape, size * LEAF_SIZE, int(round(count * LEAF_COUNT)), col, spread)
    return sp


def broadleaf(name, seed, k=None, n=5):
    """One broadleaf tree (or the Scots pine, grown the same way): (near mesh, far mesh, info)."""
    rng = np.random.default_rng(seed)
    sp = vary(name, rng, k, n)
    h = sp["height"]
    N, parent = colonise(sp, rng)
    r, kids = radii(N, parent, 0.004, sp["trunk_r"] * h)
    N = smooth_limbs(N, parent, kids, r)
    r = smooth_radii(N, parent, kids, r)
    # Wind: 0 at the trunk, 1 at the outer twigs (by path length from the crown's base).
    plen = np.zeros(len(N))
    for i in range(len(N)):
        if parent[i] >= 0:
            plen[i] = plen[parent[i]] + np.linalg.norm(N[i] - N[parent[i]])
    c0 = sp["crown"][0] * h + 1.2
    wind = np.clip((plen - c0) / max(plen.max() - c0, 1e-6), 0, 1) ** 1.3
    # Twig ends: the leaves go there, and along the last few nodes before them.
    tips = [i for i in range(len(N)) if parent[i] >= 0 and not kids[i]]
    leaf_nodes = set()
    for t in tips:
        q = t
        for _ in range(sp.get("leaf_depth", 3)):
            leaf_nodes.add(q)
            q = parent[q]
            if q <= 0 or r[q] > 0.02:
                break
    leaf_nodes = sorted(leaf_nodes)
    ao_fn = crown_ao(N[leaf_nodes], h)
    near = Mesh()
    leafP = []
    for i in leaf_nodes:
        d = N[i] - N[parent[i]]
        d /= max(np.linalg.norm(d), 1e-9)
        out = N[i] - np.array([0, N[i][1], 0]); out[1] = 0
        out = out / max(np.linalg.norm(out), 1e-9) * 0.6 + d * 0.4
        if sp.get("weep", 0) > 0.3:
            out = out + np.array([0, -0.5, 0])
        leafP.append(leaf_cluster(near, N[i], out, sp, rng, min(1.0, wind[i] + 0.15), ao_fn))
    if sp.get("flowers"):
        # (a garden plant's flowers on some of its twig ends: Tools/make_gardens.py; first to fall)
        shape, size, count, colours, share = sp["flowers"]
        fsp = dict(sp, leaf=(shape, size, count, colours[0], 0.06))
        for i in leaf_nodes:
            if not kids[i] and rng.uniform() < share:
                fsp["leaf"] = (shape, size, count, colours[int(rng.integers(len(colours)))], 0.06)
                fl = Mesh()
                out = N[i] - N[parent[i]]; out = out / max(np.linalg.norm(out), 1e-9)
                leaf_cluster(fl, N[i] + out * size, out, fsp, rng, min(1.0, wind[i] + 0.2), lambda P: np.clip(ao_fn(P) + 0.2, 0, 1))
                for q in range(len(fl.P)):
                    fl.UV2[q][:, 1] = np.minimum(fl.UV2[q][:, 1], rng.uniform(0.01, 0.3))
                    near.add(fl.P[q], fl.T[q] - sum(len(x) for x in fl.P[:q]), fl.C[q], fl.UV[q], fl.UV1[q], fl.UV2[q], fl.UV3[q])
    tubes(near, N, parent, r, kids, sp["bark"], rng, wind, ao_fn, bark_top=sp.get("bark_top"))
    if sp.get("near_only"):
        return near, None, {"nodes": int(len(N)), "tips": len(tips), "height": h, "age": round(sp["age"], 2)}
    far = far_tree(sp, rng, N, parent, r, kids, np.concatenate(leafP), ao_fn, wind)
    return near, far, {"nodes": int(len(N)), "tips": len(tips), "height": h, "age": round(sp["age"], 2)}


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
    """The far version: the trunk and main limbs as thin tubes, and the leaves as a few hundred broad
    ragged leaf cards where the leaves are ([build 167] beenster: "please please please get rid of the
    bubble trees everywhere forever": the lumpy spheres these were read as bubbles on sticks). Each card
    takes its turn to fall as the leaves do, so the bare model (SEASON_CUTS) is the limbs alone."""
    far = Mesh()
    h = sp["height"]
    kind, hue, keep = season_of(sp)
    base = lin(sp["leaf"][3])
    R = max(sp["crown"][2] * h, 1.0)
    n = int(np.clip(len(leaves) / 5, 300, 700))
    at = leaves[rng.choice(len(leaves), min(len(leaves), n), replace=False)]
    centre = np.median(leaves, axis=0)
    size = math.sqrt(1.3 * math.pi * R * R / (0.62 * len(at)))
    # (a card: a ragged eight-sided leaf mass, fanned from its middle)
    ang = np.linspace(0, 2 * np.pi, 9)[:-1]
    for p in at:
        out = p - centre; out[1] *= 0.6
        out = out / max(np.linalg.norm(out), 1e-6)
        nrm = out * 0.7 + rng.normal(0, 0.45, 3); nrm[1] += 0.35
        nrm /= max(np.linalg.norm(nrm), 1e-6)
        u, v = ring_frame(nrm)
        s_ = size * rng.uniform(0.7, 1.3)
        rad = s_ * 0.5 * rng.uniform(0.55, 1.0, 8)
        P = np.vstack([p, p + (np.cos(ang)[:, None] * u + np.sin(ang)[:, None] * v) * rad[:, None]])
        T = np.array([[0, 1 + k, 1 + (k + 1) % 8] for k in range(8)])
        ao = ao_fn(P) * 0.85
        col = base * np.exp(rng.normal(0, 0.06, 3))
        col = (col * 0.8 + col.mean() * 0.2) * 0.85
        rank = float(np.clip(rng.uniform(0.02, 1.0), 0.01, 1.0))
        far.add(P, T, np.concatenate([np.repeat(col[None], len(P), 0), ao[:, None]], 1),
                np.stack([np.full(len(P), float(kind)), np.full(len(P), 0.5)], 1),
                None, np.tile([0.0, rank], (len(P), 1)), np.tile([hue, keep], (len(P), 1)))
    tubes(far, N, parent, r, kids, sp["bark"], rng, wind * 0, ao_fn, min_r=min(0.04, sp["trunk_r"] * h * 0.3), flare=False,
          bark_top=sp.get("bark_top"), coarse=2)
    return far


# --- Conifers with whorls: Sitka spruce (the plantations) and larch ---------------------------------

def spruce(seed, name="spruce", k=None, n=5):
    """A plantation spruce or larch: a straight trunk, whorls of branches (dead and short low down in a
    close-grown stand), each covered in shoots of needles made as little bottle brushes. A larch's
    branches are fewer, upswept and more open, its needles light green, gold in autumn and gone in
    winter."""
    sp = SPECIES[name]
    rng = np.random.default_rng(seed)
    age = (0.6 + 0.6 * k / max(n - 1, 1)) if k is not None else rng.uniform(0.6, 1.2)
    h = sp["height"]
    larch = name == "larch"
    near = Mesh()
    far = Mesh()
    live_base = h * rng.uniform(0.3, 0.5) * (1.25 - 0.4 * age)   # close-grown: the lower trunk is bare
    R_max = h * (0.13 + 0.06 * age) * (1.1 if larch else 1.0)
    bark0 = lin(sp["bark"][0])
    needle = lin(sp["leaf"][3])
    kind, hue, keep = season_of(sp)

    def ao_fn(P):
        y = np.clip((P[:, 1] - live_base) / (h - live_base), 0, 1)
        rr = np.linalg.norm(P[:, [0, 2]], axis=1)
        R = R_max * (1 - y) + 0.2
        return np.clip(0.18 + 0.55 * np.clip(rr / R, 0, 1) ** 1.2 + 0.3 * y, 0.12, 1)

    # Trunk.
    ys = np.linspace(-1.2, h, 24)
    trunk_r = sp["trunk_r"] * (0.8 + 0.3 * age)
    rt = trunk_r * h * np.clip(1 - (ys + 1.2) / (h + 1.2), 0.03, 1) ** 0.9
    sides = 12
    ang = np.arange(sides + 1) / sides * 2 * np.pi   # the last repeats the first: the bark's seam
    P = np.concatenate([np.stack([np.cos(ang) * rr, np.full(sides + 1, y), np.sin(ang) * rr], 1) for y, rr in zip(ys, rt)])
    P = np.vstack([P, [[0, h + 0.15, 0]]])
    T = []
    m1 = sides + 1
    for kk in range(len(ys) - 1):
        for s in range(sides):
            a, b = kk * m1 + s, kk * m1 + s + 1
            T += [[a, a + m1, b], [b, a + m1, b + m1]]
    top = len(P) - 1
    for s in range(sides):
        a = (len(ys) - 1) * m1 + s
        T.append([a, top, a + 1])
    reps = max(1, int(round(2 * np.pi * rt[0] / BARK_M)))
    uv1 = np.concatenate([np.stack([np.arange(m1) / sides * reps * BARK_M, np.full(m1, y + 1.2)], 1) for y in ys] + [[[0, h + 1.35]]])
    C = np.repeat(bark0[None], len(P), 0) * np.exp(rng.normal(0, 0.05, (len(P), 1)))
    for m in (near, far):
        m.add(P, T, np.concatenate([C, ao_fn(P)[:, None]], 1), np.stack([np.zeros(len(P)), np.zeros(len(P))], 1), uv1)
    # Whorls every ~0.5 m (a year's growth), 5 branches each (a larch's 4, further apart).
    y = 1.0
    shoots = []
    while y < h - 0.3:
        t = (y - live_base) / (h - live_base)
        live = t > 0
        L = (R_max * (1 - t) ** 0.95 + 0.15) if live else rng.uniform(0.3, 0.9)
        nb = (4 if larch else 5) if live else 3
        a0 = rng.uniform(0, 2 * np.pi)
        for b in range(nb):
            a = a0 + b * 2 * np.pi / nb + rng.normal(0, 0.25)
            dirn = np.array([math.cos(a), 0, math.sin(a)])
            # Branch: out and up a little, drooping at the end (more so low down); a larch's sweep up.
            n_seg = 6
            pts = []
            for s in range(n_seg + 1):
                f = s / n_seg
                drop = sp["droop"] * ((0.15 + 0.25 * (1 - t)) * L * f * f - 0.12 * L * f)
                if larch:
                    drop -= 0.18 * L * f * f
                pts.append(dirn * L * f + np.array([0, y - drop, 0]))
            pts = np.array(pts)
            rb = 0.04 * (L / R_max) + 0.008
            # The branch as one 4 sided tube, its rings carried along it (no twist), ending in a point.
            Tg, U, V = transported_frames(pts)
            rings = []
            for s in range(n_seg + 1):
                rr = rb * (1 - s / n_seg) + 0.004
                rings.append(pts[s][None] + np.array([U[s] * math.cos(q) + V[s] * math.sin(q) for q in (0, 1.57, 3.14, 4.71)]) * rr)
            rings.append(np.repeat((pts[-1] + Tg[-1] * 0.03)[None], 4, 0))
            Pq = np.vstack(rings)
            Tq = []
            for s in range(len(rings) - 1):
                for q in range(4):
                    a_, b_ = s * 4 + q, s * 4 + (q + 1) % 4
                    Tq += [[a_, a_ + 4, b_], [b_, a_ + 4, b_ + 4]]
            col = bark0 * (0.8 if live else 0.65)
            wv = np.repeat(np.concatenate([np.arange(n_seg + 1) / n_seg, [1.0]]) * 0.6 * (1 if live else 0.2), 4)
            near.add(Pq, Tq, np.concatenate([np.repeat(col[None], len(Pq), 0), ao_fn(Pq)[:, None]], 1),
                     np.stack([np.zeros(len(Pq)), wv], 1))
            if live:
                # Shoots along the outer two thirds of the branch, either side and hanging below it
                # (a larch's sparser, and tufted along its twigs).
                per = 6
                for s in range(1, n_seg + 1):
                    for _ in range(per):
                        p = pts[s] + rng.normal(0, 0.1, 3)
                        sd = np.cross(dirn, [0, 1, 0]) * rng.choice([-1, 1]) + dirn * 0.6 + np.array([0, rng.uniform(-0.9, 0.1) * sp["droop"] + (0.25 if larch else 0), 0])
                        sd /= np.linalg.norm(sd)
                        shoots.append((p, sd, rng.uniform(0.3, 0.55) * (0.6 + 0.4 * L / R_max), (s / n_seg) * 0.8 + 0.2))
        y += rng.uniform(0.42, 0.6) * (1.25 if larch else 1.0)
    # The leader and top shoots.
    for kk in range(10):
        p = np.array([0, h - 0.2 - kk * 0.15, 0]) + rng.normal(0, 0.03, 3)
        sd = np.array([rng.normal(0, 0.4), 1.0, rng.normal(0, 0.4)]); sd /= np.linalg.norm(sd)
        shoots.append((p, sd, 0.25, 1.0))
    # Shoots as bottle brushes: a stalk with 3 rows of spiky needles (a jagged triangular prism).
    allP, allT, allC, allW, allPiv, allR = [], [], [], [], [], []
    nv = 0
    nr = sp.get("needle_r", 0.075)
    for p, d, L, w in shoots:
        u, v = ring_frame(d)
        nseg = 3
        Pk = []
        for s in range(nseg + 1):
            q = p + d * L * s / nseg
            rr = nr * (1 - 0.6 * s / nseg)
            for kk, a in enumerate((0.0, 2.1, 4.2)):
                jag = rr * (1.0 if (s + kk) % 2 == 0 else 0.55)
                Pk.append(q + (u * math.cos(a) + v * math.sin(a)) * jag)
        Pk.append(p + d * L * 1.08)
        Pk = np.array(Pk)
        Tk = []
        for s in range(nseg):
            for kk in range(3):
                a, b = s * 3 + kk, s * 3 + (kk + 1) % 3
                Tk += [[a, a + 3, b], [b, a + 3, b + 3]]
        tip = len(Pk) - 1
        for kk in range(3):
            Tk.append([nseg * 3 + kk, tip, nseg * 3 + (kk + 1) % 3])
        allP.append(Pk); allT.append(np.array(Tk) + nv); nv += len(Pk)
        c = needle * np.exp(rng.normal(0, 0.06, 3)) * rng.uniform(0.85, 1.1)
        allC.append(np.repeat(c[None], len(Pk), 0)); allW.append(np.full(len(Pk), w))
        allPiv.append(np.repeat(Pk.mean(0)[None], len(Pk), 0)); allR.append(np.full(len(Pk), rng.uniform(0.02, 1.0)))
    P = np.concatenate(allP)
    off = np.concatenate(allPiv) - P
    near.add(P, np.concatenate(allT), np.concatenate([np.concatenate(allC), ao_fn(P)[:, None]], 1),
             np.stack([np.full(len(P), float(kind)), np.concatenate(allW)], 1),
             off[:, :2], np.stack([off[:, 2], np.concatenate(allR)], 1), np.tile([hue, keep], (len(P), 1)))
    # Far: a jagged cone of needles over the live crown, in tiers (a larch's more open).
    tiers = 7
    for kk in range(tiers):
        y0 = live_base + (h - live_base) * kk / tiers
        y1 = live_base + (h - live_base) * (kk + 1.6) / tiers
        R0 = R_max * (1 - (y0 - live_base) / (h - live_base)) + 0.2
        segs = 9
        a = np.arange(segs) / segs * 2 * np.pi + rng.uniform(0, 1)
        ring = np.stack([np.cos(a) * R0 * rng.uniform(0.85, 1.1, segs), np.full(segs, y0 - 0.25 * R0),
                         np.sin(a) * R0 * rng.uniform(0.85, 1.1, segs)], 1)
        if larch:
            ring *= np.array([sp.get("open", 1.0), 1, sp.get("open", 1.0)])
        Pt = np.vstack([ring, [[0, min(y1, h), 0]], [[0, y0 + 0.1, 0]]])
        Tt = [[s, segs, (s + 1) % segs] for s in range(segs)] + [[s, (s + 1) % segs, segs + 1] for s in range(segs)]
        c = needle * np.exp(rng.normal(0, 0.04, 3)) * 0.8
        far.add(Pt, Tt, np.concatenate([np.repeat(c[None], len(Pt), 0), ao_fn(Pt)[:, None] * 0.9], 1),
                np.stack([np.full(len(Pt), float(kind)), np.full(len(Pt), 0.3)], 1),
                None, np.tile([0.0, -1.0], (len(Pt), 1)), np.tile([hue, keep], (len(Pt), 1)))
    return near, far, {"shoots": len(shoots), "height": h, "age": round(age, 2)}


def make(name, seed, k=None, n=5, veteran=False):
    if veteran:
        return broadleaf(veteran_spec(name), seed, n - 1, n)
    if name in ("spruce", "larch"):
        return spruce(seed, name, k, n)
    return broadleaf(name, seed, k, n)
