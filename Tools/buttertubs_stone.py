"""Stone-by-stone meshes for the Buttertubs countryside, written as glb by buttertubs_detail.py into
Data/Buttertubs/Props and placed in the level as Nanite instances (ue_build_level.py reads props.json).

    SM_Wall_A..H      2 m of Dales drystone wall: footings, battered faces laid in courses, through
                      stones, hearting in the gaps, and a row of coping stones on edge along the top
    SM_WallEnd_A..B   a wall head: a 1 m length finished at +x with headers tied across
    SM_WallPier       1 m finished at both ends, set on the sharp bends of a wall
    SM_GateStoop      a gate post: a tall limestone upright
    SM_Gate           a galvanised five-bar field gate, 3.4 m, hinged at local x = 0
    SM_Rock_00..11    limestone boulders, about 1 m across, scaled when placed
    SM_Scree_A..B     a spill of small stones about 1.6 m across (A is swapped for the Megascans
                      Small Limestone Rocks Pack in prop_swaps.json where the PC has it)
    SM_Scar_A..D      4 m of limestone scar: beds of blocks split by joints, stepped back up the face
    SM_Barn_N         one field barn per map building: rubble walls with quoins and through stones,
    SM_BarnWood_N       a stone slate roof, a cart door and forking holes (the wood is its own mesh)
    SM_Sheep_A..C     a Swaledale ewe grazing, looking up, and lying down

The local frame is the glb's: metres, x along, y up, z across (z is the face for scars). Ground is
y = 0 and everything goes some way below it, so it sits into uneven ground.

Vertex colour is a set of masks for the materials, not a colour:
    stone:  R brightness (0.5 = the texture as it is), G which of the two stone scans (0 lichened
            limestone, 1 bare), B lichen, A moss
    fleece, metal, wood: RGB the colour (linear), A dirt
UV0 is in metres of surface, each stone with its own offset, so no two stones show the same patch.
"""
import math
import numpy as np


# --- A mesh being built --------------------------------------------------------------------------

class Mesh:
    def __init__(self):
        self.P, self.T, self.C, self.U = [], [], [], []
        self.n = 0

    def add(self, pos, tris, col, uv):
        pos = np.asarray(pos, np.float64).reshape(-1, 3)
        col = np.asarray(col, np.float64)
        if col.ndim == 1:
            col = np.repeat(col[None, :], len(pos), 0)
        self.P.append(pos); self.T.append(np.asarray(tris, np.int64).reshape(-1, 3) + self.n)
        self.C.append(col); self.U.append(np.asarray(uv, np.float64).reshape(-1, 2))
        self.n += len(pos)

    def arrays(self):
        pos = np.concatenate(self.P); tris = np.concatenate(self.T)
        col = np.clip(np.concatenate(self.C), 0, 1); uv = np.concatenate(self.U)
        fn = np.cross(pos[tris[:, 1]] - pos[tris[:, 0]], pos[tris[:, 2]] - pos[tris[:, 0]])
        nrm = np.zeros_like(pos)
        for i in range(3):
            np.add.at(nrm, tris[:, i], fn)
        nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
        return pos, tris, nrm, (col * 255).round().astype(np.uint8), uv

    def write(self, path, write_glb, name):
        pos, tris, nrm, col, uv = self.arrays()
        write_glb(path, pos, tris, nrm, col, name, uv)
        return len(pos), len(tris)


def rot_xyz(ax, ay, az):
    """Rotation about x, then y, then z (radians)."""
    cx, sx, cy, sy, cz, sz = math.cos(ax), math.sin(ax), math.cos(ay), math.sin(ay), math.cos(az), math.sin(az)
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Rz @ Ry @ Rx


# --- Coherent noise: a sum of random waves, cheap and smooth --------------------------------------

class Waves:
    def __init__(self, rng, n=10, freq=(1.0, 6.0), amp=1.0):
        self.d = rng.normal(0, 1, (n, 3)); self.d /= np.linalg.norm(self.d, axis=1, keepdims=True)
        self.f = np.exp(rng.uniform(math.log(freq[0]), math.log(freq[1]), n))
        self.ph = rng.uniform(0, 2 * math.pi, n)
        self.a = amp / self.f / np.sqrt((1.0 / self.f ** 2).sum())   # pinker: big waves stronger

    def __call__(self, p):
        return (np.sin((p @ self.d.T) * self.f * 2 * math.pi + self.ph) * self.a).sum(-1)


# --- One stone ------------------------------------------------------------------------------------

_TOPO = {}


def _topology(nu, nv):
    """A UV sphere's triangles: bottom pole, nu rings of nv, top pole; faces wound outwards."""
    if (nu, nv) in _TOPO:
        return _TOPO[(nu, nv)]
    tris = [[0, 1 + vv, 1 + (vv + 1) % nv] for vv in range(nv)]
    for uu in range(nu - 1):
        for vv in range(nv):
            a = 1 + uu * nv + vv; b = 1 + uu * nv + (vv + 1) % nv
            c = 1 + (uu + 1) * nv + vv; d = 1 + (uu + 1) * nv + (vv + 1) % nv
            tris += [[a, c, b], [b, c, d]]
    last = 1 + nu * nv
    tris += [[last, 1 + (nu - 1) * nv + (vv + 1) % nv, 1 + (nu - 1) * nv + vv] for vv in range(nv)]
    tris = np.array(tris)
    # Check the winding on a unit sphere and turn it outwards if need be.
    P = _unit(nu, nv, 1.0, 1.0)
    fn = np.cross(P[tris[:, 1]] - P[tris[:, 0]], P[tris[:, 2]] - P[tris[:, 0]])
    if (fn * P[tris].mean(1)).sum() < 0:
        tris = tris[:, [0, 2, 1]]
    _TOPO[(nu, nv)] = tris
    return tris


def _unit(nu, nv, e1, e2):
    """Superellipsoid of half size 1: e near 1 is round, small e is boxy."""
    def sp(a, e):
        return np.sign(a) * np.abs(a) ** e
    u = np.linspace(-math.pi / 2, math.pi / 2, nu + 2)[1:-1]
    v = np.linspace(0, 2 * math.pi, nv, endpoint=False) + math.pi / nv
    U, V = np.meshgrid(u, v, indexing="ij")
    x = sp(np.cos(U), e1) * sp(np.cos(V), e2)
    y = sp(np.sin(U), e1)
    z = sp(np.cos(U), e1) * sp(np.sin(V), e2)
    P = np.stack([x, y, z], -1).reshape(-1, 3)
    return np.vstack([[0, -1, 0], P, [0, 1, 0]])


def stone(rng, size, centre=(0, 0, 0), R=None, res=(6, 10), round_=(0.3, 0.35), chops=4, chop_depth=(0.65, 0.95),
          bump=0.015, waves=None):
    """A rough block of half size `size` = (x, y, z): a rounded box, a few corners knocked off by
    flat breaks (limestone splits along planes), and a little roughness. Returns positions,
    triangles and box-projected UVs in metres (each stone at its own place in the texture)."""
    nu, nv = res
    e1 = rng.uniform(*round_); e2 = rng.uniform(*round_)
    P = _unit(nu, nv, e1, e2) * np.asarray(size, np.float64)
    for _ in range(chops):
        n = rng.normal(0, 1, 3); n /= np.linalg.norm(n)
        d = rng.uniform(*chop_depth) * (P @ n).max()
        P -= np.maximum(0.0, P @ n - d)[:, None] * n
    if bump:
        w = waves or Waves(rng, 8, (2.0, 9.0), 1.0)
        dirn = P / np.maximum(np.linalg.norm(P, axis=1, keepdims=True), 1e-9)
        P += dirn * (w(P + rng.uniform(-50, 50, 3)) * bump)[:, None]
    # UVs: project each vertex on the plane square to its normal's strongest axis.
    T = _topology(nu, nv)
    fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    vn = np.zeros_like(P)
    for i in range(3):
        np.add.at(vn, T[:, i], fn)
    ax = np.abs(vn).argmax(1)
    uv = np.where(ax[:, None] == 0, P[:, [2, 1]], np.where(ax[:, None] == 1, P[:, [0, 2]], P[:, [0, 1]]))
    uv = uv + rng.uniform(0, 7, 2)
    if R is not None:
        P = P @ np.asarray(R).T
    P += np.asarray(centre, np.float64)
    return P, T, uv


def box(lo, hi):
    """An axis-aligned box: 8 corners, 12 triangles wound outwards, UVs from x/z and y."""
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    P = np.array([[x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1],
                  [x0, y1, z0], [x1, y1, z0], [x1, y1, z1], [x0, y1, z1]], np.float64)
    T = np.array([[0, 1, 2], [0, 2, 3], [4, 6, 5], [4, 7, 6], [0, 4, 5], [0, 5, 1],
                  [1, 5, 6], [1, 6, 2], [2, 6, 7], [2, 7, 3], [3, 7, 4], [3, 4, 0]])
    return P, T, np.stack([P[:, 0] + P[:, 2], P[:, 1]], 1)


def tube(path, radius, sides=6):
    """A tube along a path of points (n, 3): positions and triangles, open ends capped."""
    path = np.asarray(path, np.float64)
    n = len(path)
    t = np.gradient(path, axis=0); t /= np.linalg.norm(t, axis=1, keepdims=True)
    a = np.cross(t, [0.0, 1.0, 0.0])
    bad = np.linalg.norm(a, axis=1) < 1e-3
    a[bad] = np.cross(t[bad], [1.0, 0.0, 0.0])
    a /= np.linalg.norm(a, axis=1, keepdims=True)
    b = np.cross(t, a)
    r = np.broadcast_to(np.asarray(radius, np.float64), (n,))
    ang = np.linspace(0, 2 * math.pi, sides, endpoint=False)
    P = path[:, None, :] + r[:, None, None] * (np.cos(ang)[None, :, None] * a[:, None, :] + np.sin(ang)[None, :, None] * b[:, None, :])
    P = P.reshape(-1, 3)
    T = []
    for i in range(n - 1):
        for s in range(sides):
            p0 = i * sides + s; p1 = i * sides + (s + 1) % sides
            T += [[p0, p1 + sides, p1], [p0, p0 + sides, p1 + sides]]
    P = np.vstack([P, path[0], path[-1]])
    c0, c1 = len(P) - 2, len(P) - 1
    for s in range(sides):
        T += [[c0, s, (s + 1) % sides], [c1, (n - 1) * sides + (s + 1) % sides, (n - 1) * sides + s]]
    T = np.array(T)
    # Outwards: compare each face normal with the direction from the path.
    fc = P[T].mean(1)
    fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    if (fn * (fc - path.mean(0))).sum() < 0:
        T = T[:, [0, 2, 1]]
    return P, T


def stone_col(rng, n, bright=None, bare=None, lichen=None, moss=0.0):
    """The stone masks (see the module's docstring) for one stone's n vertices."""
    b = 0.44 + rng.normal(0, 0.07) if bright is None else bright
    g = rng.uniform(0, 1) ** 0.7 if bare is None else bare
    l = rng.uniform(0, 0.6) ** 1.5 if lichen is None else lichen
    c = np.zeros((n, 4))
    c[:, 0] = np.clip(b + rng.normal(0, 0.015, n), 0.05, 1); c[:, 1] = g; c[:, 2] = l
    c[:, 3] = moss
    return c


def moss_for(P, top_y, rng, base_moss=0.35, low=0.25):
    """Moss: thick at the foot of a wall and on upward faces near the top, a little everywhere."""
    lowm = np.clip((low - P[:, 1]) / max(low, 1e-3), 0, 1) * 0.9
    return np.clip(lowm + base_moss * rng.uniform(0, 1) ** 3, 0, 1)


# --- Drystone wall ---------------------------------------------------------------------------------

WALL_L = 2.0
WALL_H = 1.12      # top of the faces, under the copes
FOOT_HW = 0.42     # half width at the foot
TOP_HW = 0.23      # half width under the copes
# Course boundaries and their seam shifts are the same in every variant, so neighbouring
# lengths line up course for course; only the stones differ.
COURSES = [0.14, 0.33, 0.50, 0.65, 0.79, 0.92, 1.03, WALL_H]
SHIFT = [0.07, -0.11, 0.13, -0.04, 0.09, -0.13, 0.03, 0.11]


def hw_at(y):
    return FOOT_HW + (TOP_HW - FOOT_HW) * np.clip(y / WALL_H, 0, 1)


def wall_length(M, rng, x0, x1, end_lo=False, end_hi=False, copes=True):
    """Stones of a wall from x0 to x1 (local), faces battered from FOOT_HW to TOP_HW."""
    batter = math.atan((FOOT_HW - TOP_HW) / WALL_H)
    L = x1 - x0
    # Hearting: dark rubble behind the faces, seen through the gaps.
    cp = []
    for y in (-0.3, WALL_H + 0.02):
        h = hw_at(max(y, 0)) - 0.09
        cp += [[x0 + 0.02, y, -h], [x1 - 0.02, y, -h], [x1 - 0.02, y, h], [x0 + 0.02, y, h]]
    cp = np.array(cp)
    T = np.array([[0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7], [0, 1, 5], [0, 5, 4], [1, 2, 6], [1, 6, 5],
                  [2, 3, 7], [2, 7, 6], [3, 0, 4], [3, 4, 7]])
    cc = np.zeros((8, 4)); cc[:, 0] = 0.08; cc[:, 1] = 1.0; cc[:, 3] = 0.0
    M.add(cp, T, cc, cp[:, [0, 1]] + cp[:, [2, 2]])

    def place(cx, cy, cz, sx, sy, sz, R, moss_base, res=(6, 10), bright=None, bare=None, lichen=None):
        P, Tt, uv = stone(rng, (sx, sy, sz), (cx, cy, cz), R, res=res)
        col = stone_col(rng, len(P), bright, bare, lichen)
        col[:, 3] = np.clip(moss_for(P, WALL_H, rng, moss_base) + rng.normal(0, 0.03, len(P)), 0, 1)
        M.add(P, Tt, col, uv)

    # Footings: big stones, a little proud of the face, mostly buried.
    for side in (-1, 1):
        x = x0 - rng.uniform(0, 0.25) if not end_lo else x0
        while x < x1 - 0.1:
            ln = min(rng.uniform(0.45, 0.8), x1 - x)
            h = rng.uniform(0.32, 0.44); d = rng.uniform(0.36, 0.46)
            cx = x + ln / 2
            cz = side * (FOOT_HW + 0.04 - d / 2)
            R = rot_xyz(rng.normal(0, 0.05), rng.normal(0, 0.06), rng.normal(0, 0.04))
            place(cx, 0.14 - h / 2 + rng.uniform(-0.03, 0.03), cz, ln / 2, h / 2, d / 2, R, 0.6)
            x += ln + rng.uniform(0.0, 0.03)

    # Courses of face stones.
    yb = COURSES[0]
    for ci, yt in enumerate(COURSES[1:]):
        ch = yt - yb
        for side in (-1, 1):
            s = SHIFT[ci] * (L / WALL_L)
            # The seams: course ci runs over [x0 + s, x1 + s] so the next length takes over at x1 + s.
            lo = x0 + (0 if end_lo else s); hi = x1 + (0 if end_hi else s)
            if end_hi:
                hi = x1 - (0.26 if ci % 2 == 0 else 0.0)
            if end_lo:
                lo = x0 + (0.26 if ci % 2 == 1 else 0.0)
            x = lo
            while x < hi - 0.04:
                ln = rng.uniform(0.17, 0.46) * (1.25 - 0.4 * yt / WALL_H)
                if hi - (x + ln) < 0.12:
                    ln = hi - x
                gap = rng.uniform(0.004, 0.03)
                h = ch * rng.uniform(0.72, 0.97)
                d = rng.uniform(0.17, 0.3)
                cy = yb + h / 2 + rng.uniform(0, ch - h) * 0.6 + 0.005
                face = hw_at(cy) + rng.normal(0, 0.018)
                cz = side * (face - d / 2)
                R = rot_xyz(-side * batter + rng.normal(0, 0.06), rng.normal(0, 0.06), rng.normal(0, 0.07))
                place(x + ln / 2, cy, cz, ln / 2 - gap / 2, h / 2, d / 2, R, 0.08)
                x += ln
        # Ends: a header across the whole width on every other course, so the head is tied.
        for at_end, xe, sgn in ((end_hi, x1, -1), (end_lo, x0, 1)):
            if not at_end:
                continue
            if (ci % 2 == 0) == (sgn < 0):
                ln = 0.27; h = ch * 0.92
                cy = yb + h / 2 + 0.005
                w = hw_at(cy)
                R = rot_xyz(rng.normal(0, 0.02), rng.normal(0, 0.03), rng.normal(0, 0.03))
                place(xe + sgn * ln / 2, cy, 0.0, ln / 2, h / 2, w + 0.01, R, 0.1, res=(6, 12), bare=rng.uniform(0, 0.4))
        # Through stones half way up, about every 0.9 m, sticking out a hand's width.
        if ci == 2:
            for xt in np.arange(x0 + rng.uniform(0.2, 0.6), x1 - 0.15, 0.9 + rng.uniform(-0.1, 0.1)):
                w = hw_at(yt) + rng.uniform(0.06, 0.12)
                R = rot_xyz(rng.normal(0, 0.03), rng.normal(0, 0.06), rng.normal(0, 0.03))
                place(xt, yt + 0.06, rng.normal(0, 0.03), rng.uniform(0.11, 0.16), 0.065, w, R, 0.25,
                      res=(6, 12), bare=rng.uniform(0, 0.5))
        yb = yt

    if not copes:
        return
    # Copes: flat stones on edge across the top, leaning a little this way and that.
    s = SHIFT[-1] * (L / WALL_L)
    x = x0 + (0 if end_lo else s)
    hi = x1 + (0 if end_hi else s)
    lean = rng.normal(0, 0.12)
    while x < hi - 0.03:
        t = rng.uniform(0.06, 0.15)
        if hi - (x + t) < 0.05:
            t = hi - x
        h = rng.uniform(0.2, 0.36)
        w = TOP_HW + rng.uniform(0.02, 0.07)
        lean = 0.7 * lean + rng.normal(0, 0.08)
        R = rot_xyz(rng.normal(0, 0.05), rng.normal(0, 0.06), lean)
        P, Tt, uv = stone(rng, (t / 2, h / 2, w), (x + t / 2, WALL_H + h / 2 - 0.03, rng.normal(0, 0.02)), R,
                          res=(6, 10), round_=(0.2, 0.35), chops=5, chop_depth=(0.55, 0.9))
        col = stone_col(rng, len(P), lichen=rng.uniform(0.2, 0.9))
        top = np.clip((P[:, 1] - WALL_H - 0.12) / 0.15, 0, 1)
        col[:, 3] = np.clip(0.15 + 0.6 * top * rng.uniform(0.2, 1.0) ** 2 + rng.normal(0, 0.03, len(P)), 0, 1)
        M.add(P, Tt, col, uv)
        x += t + rng.uniform(0.0, 0.015)


def wall_module(rng):
    M = Mesh()
    wall_length(M, rng, -WALL_L / 2, WALL_L / 2)
    return M


def wall_end(rng, length=1.0, both=False):
    M = Mesh()
    wall_length(M, rng, -length / 2, length / 2, end_lo=both, end_hi=True)
    return M


def gate_stoop(rng):
    M = Mesh()
    P, T, uv = stone(rng, (0.17, 0.95, 0.15), (0, 0.55, 0), rot_xyz(0, rng.uniform(0, 3), 0), res=(10, 10),
                     round_=(0.25, 0.35), chops=5, chop_depth=(0.75, 0.95))
    col = stone_col(rng, len(P), lichen=0.6)
    col[:, 3] = np.clip(0.5 - P[:, 1], 0, 1) * 0.8
    M.add(P, T, col, uv)
    return M


def gate(rng):
    """A galvanised field gate: hinge stile at x = 0, latch stile at x = 3.35, five rails and a brace."""
    M = Mesh()
    grey = np.array([0.42, 0.43, 0.42])

    def bar(a, b, r=0.022):
        P, T = tube([a, b], r, 6)
        c = np.zeros((len(P), 4)); c[:, :3] = grey * rng.uniform(0.9, 1.08); c[:, 3] = rng.uniform(0, 0.4)
        M.add(P, T, c, P[:, [0, 1]])

    W, y0, y1 = 3.35, 0.12, 1.15
    bar([0.03, y0 - 0.05, 0], [0.03, y1 + 0.08, 0], 0.03)
    bar([W, y0 - 0.02, 0], [W, y1 + 0.02, 0], 0.025)
    for k in range(5):
        y = y0 + (y1 - y0) * (k / 4) ** 1.25
        bar([0.03, y, 0], [W, y, 0], 0.018 if k else 0.022)
    bar([0.05, y0, 0], [W * 0.62, y1, 0], 0.016)
    bar([W * 0.5, y0, 0], [W * 0.5, y1, 0], 0.014)
    return M


# --- Scree -----------------------------------------------------------------------------------------

def scree(rng):
    """Small limestone stones, 5 to 30 cm, spilled over a patch about 1.6 m across, half sunk."""
    M = Mesh()
    for _ in range(int(rng.integers(14, 22))):
        r = 0.8 * math.sqrt(rng.uniform()); a = rng.uniform(0, 2 * math.pi)
        size = rng.choice([0.05, 0.09, 0.14, 0.2], p=[0.35, 0.35, 0.2, 0.1]) * rng.uniform(0.8, 1.3)
        sx, sy, sz = size * rng.uniform(0.8, 1.3), size * rng.uniform(0.4, 0.7), size * rng.uniform(0.7, 1.1)
        P, T, uv = stone(rng, (sx, sy, sz), (r * math.cos(a), sy * 0.3, r * math.sin(a)),
                         rot_xyz(rng.normal(0, 0.3), rng.uniform(0, 6.3), rng.normal(0, 0.3)),
                         res=(7, 12), round_=(0.2, 0.4), chops=5, bump=0.004)
        col = stone_col(rng, len(P), bright=0.45 + rng.normal(0, 0.06), bare=rng.uniform(0.3, 1.0), lichen=rng.uniform(0, 0.4))
        col[:, 3] = rng.uniform(0, 0.3)
        M.add(P, T, col, uv)
    return M


# --- Boulders and scars ----------------------------------------------------------------------------

def boulder(rng):
    """A limestone boulder about 1 m across, its bottom third meant to be under ground."""
    M = Mesh()
    sx, sy, sz = 0.5 * rng.uniform(0.8, 1.2), 0.5 * rng.uniform(0.45, 0.8), 0.5 * rng.uniform(0.7, 1.1)
    w = Waves(rng, 14, (0.7, 8.0), 1.0)
    P, T, uv = stone(rng, (sx, sy, sz), (0, sy * 0.45, 0), None, res=(26, 40), round_=(0.2, 0.42), chops=10,
                     chop_depth=(0.55, 0.9), bump=0.035, waves=w)
    col = stone_col(rng, len(P), bright=0.4 + rng.normal(0, 0.05), bare=rng.uniform(0, 0.8), lichen=rng.uniform(0.2, 0.8))
    up = (P[:, 1] - P[:, 1].min()) / max(np.ptp(P[:, 1]), 1e-6)
    col[:, 3] = np.clip(0.55 * np.clip(0.35 - up, 0, 1) / 0.35 + 0.3 * np.clip(w(P * 1.7) * 2 + 0.1, 0, 1), 0, 1)
    col[:, 0] += w(P * 3.1) * 0.08
    M.add(P, T, col, uv)
    return M


def scar_module(rng):
    """4 m of limestone scar, 3 m high, its face looking along +z: beds of blocks, thinner and more
    broken towards the top, each bed set back a little from the one below, split by vertical joints,
    the odd block slumped forward or gone. Behind, it runs 3 m into the hill."""
    M = Mesh()
    y = -0.6
    back = 0.0
    W = 4.0
    while y < 3.0:
        bed = rng.uniform(0.35, 0.95) * (1.1 - 0.3 * max(y, 0) / 3.0)
        back += rng.uniform(-0.04, 0.16)
        x = -W / 2 - rng.uniform(0.1, 0.8)
        while x < W / 2 + 0.3:
            ln = rng.uniform(0.7, 2.2)
            joint = rng.uniform(0.01, 0.06)
            if rng.uniform() < 0.04:            # a gap where a block has fallen
                x += ln + joint
                continue
            out = rng.normal(0, 0.06) + (0.25 if rng.uniform() < 0.08 else 0.0)
            depth = 3.0
            cz = -back + out - depth / 2
            R = rot_xyz(rng.normal(0, 0.04), rng.normal(0, 0.05), rng.normal(0, 0.05))
            P, T, uv = stone(rng, (ln / 2 - joint / 2, bed / 2, depth / 2), (x + ln / 2, y + bed / 2, cz), R,
                             res=(10, 18), round_=(0.1, 0.2), chops=7, chop_depth=(0.75, 0.97), bump=0.025)
            col = stone_col(rng, len(P), bright=0.55 + rng.normal(0, 0.06), bare=rng.uniform(0.5, 1.0),
                            lichen=rng.uniform(0.1, 0.6))
            up = np.clip((P[:, 1] - (y + bed * 0.8)) / (bed * 0.3), 0, 1)
            col[:, 3] = np.clip(up * rng.uniform(0.3, 0.9) + (0.5 if y > 2.6 else 0.0), 0, 1)
            M.add(P, T, col, uv)
            x += ln
        y += bed
    return M


# --- Field barn ------------------------------------------------------------------------------------

def barn(rng, w, d, h, depth_below):
    """A Dales field barn, w along local x, d along z (the ridge runs along the longer side), eaves at h
    above the ground at its centre, walls carried depth_below under it. Returns (stone, wood) meshes."""
    S = Mesh(); Wd = Mesh()
    if d > w:   # build it with the ridge along x, then turn it
        S2, W2 = barn(rng, d, w, h, depth_below)
        for m in (S2, W2):
            m.P = [p[:, [2, 1, 0]] * [1, 1, -1] for p in m.P]   # a quarter turn: (x, y, z) -> (z, y, -x)
        return S2, W2
    t_wall = 0.6
    pitch = math.radians(rng.uniform(27, 33))
    ridge = h + (d / 2) * math.tan(pitch)
    y_lo = -depth_below
    # Openings on the front (z = +d/2): a cart door, a forking hole above, a byre door; one on the back.
    door_w = min(2.6, w * 0.3); door_x = rng.uniform(-w * 0.15, w * 0.15)
    openings = {"front": [(door_x - door_w / 2, door_x + door_w / 2, -0.1, min(2.9, h - 0.6), "cart")],
                "back": [], "left": [], "right": []}
    if w > 7:
        bx = door_x + door_w / 2 + 1.5 if door_x < 0 else door_x - door_w / 2 - 2.5
        openings["front"].append((bx, bx + 1.0, -0.1, 1.9, "door"))
    fx = -w / 2 + 1.4 if door_x > 0 else w / 2 - 2.0
    openings["front"].append((fx, fx + 0.6, h - 1.4, h - 0.75, "hole"))
    openings["back"].append((rng.uniform(-w / 4, w / 4), 0, h - 1.5, h - 0.9, "hole"))
    openings["back"][0] = (openings["back"][0][0], openings["back"][0][0] + 0.55, h - 1.5, h - 0.95, "hole")
    if rng.uniform() < 0.6:
        openings["left"].append((-0.3, 0.3, h + 0.2, h + 0.8, "hole"))

    # Interior: a dark box seen through the openings.
    P, T, _ = box((-w / 2 + 0.3, y_lo, -d / 2 + 0.3), (w / 2 - 0.3, h + 0.2, d / 2 - 0.3))
    T = T[:, [0, 2, 1]]   # inward
    c = np.zeros((8, 4)); c[:, 0] = 0.03; c[:, 1] = 1.0
    S.add(P, T, c, P[:, [0, 1]])
    # And a dark solid core behind the face stones.
    P, T, _ = box((-w / 2 + 0.12, y_lo, -d / 2 + 0.12), (w / 2 - 0.12, h, d / 2 - 0.12))
    c = np.zeros((8, 4)); c[:, 0] = 0.07; c[:, 1] = 1.0
    S.add(P, T, c, P[:, [0, 1]])

    faces = {  # name: origin, along, outward, length
        "front": (np.array([-w / 2, 0, d / 2]), np.array([1.0, 0, 0]), np.array([0, 0, 1.0]), w),
        "back": (np.array([w / 2, 0, -d / 2]), np.array([-1.0, 0, 0]), np.array([0, 0, -1.0]), w),
        "left": (np.array([-w / 2, 0, -d / 2]), np.array([0, 0, 1.0]), np.array([-1.0, 0, 0]), d),
        "right": (np.array([w / 2, 0, d / 2]), np.array([0, 0, -1.0]), np.array([1.0, 0, 0]), d),
    }

    def put(o, a, n, s, y, sx, sy, sz, R_local, bright=None, bare=None, moss=None, res=(5, 8)):
        """A stone in face coordinates: s along, y up, sz deep (its face at the wall's face)."""
        R = np.stack([a, [0, 1, 0], n], 1) @ R_local
        c = o + a * s + np.array([0, y, 0]) + n * (-sz + rng.normal(0, 0.01))
        P, T, uv = stone(rng, (sx, sy, sz), c, R, res=res, round_=(0.25, 0.4), chops=3, bump=0.008)
        col = stone_col(rng, len(P), bright, bare)
        col[:, 3] = np.clip((0.4 - P[:, 1]) / 0.6, 0, 1) * 0.8 if moss is None else moss
        S.add(P, T, col, uv)

    def in_opening(name, s0, s1, y0, y1, pad=0.02):
        for (a0, a1, b0, b1, _) in openings[name]:
            if s1 > a0 - pad and s0 < a1 + pad and y1 > b0 - pad and y0 < b1 + pad:
                return True
        return False

    for name, (o, a, n, L) in faces.items():
        gable = name in ("left", "right")
        # Openings are given relative to the face's centre; turn them into distance along the face.
        openings[name] = [(a0 + L / 2, a1 + L / 2, b0, b1, k) for (a0, a1, b0, b1, k) in openings[name]]
        y = y_lo
        course = 0
        while True:
            top_here = h + (ridge - h) * 1.0 if gable else h
            if y >= top_here - 0.02:
                break
            ch = rng.uniform(0.15, 0.26) if y > 0.3 else rng.uniform(0.25, 0.4)
            if y + ch > h and not gable:
                ch = h - y
            s = 0.0
            while s < L - 0.01:
                # Quoins: big dressed stones at the corners, long and short in turn.
                if s < 0.01:
                    ln = 0.55 if course % 2 == 0 else 0.32
                    quoin = True
                else:
                    ln = rng.uniform(0.22, 0.6)
                    quoin = False
                if L - (s + ln) < 0.5:
                    # finish the course against the far corner's quoin
                    q = 0.32 if course % 2 == 0 else 0.55
                    if s + ln > L - q - 0.12:
                        ln = L - q - s
                        if ln < 0.12:
                            ln = L - s
                            quoin = True
                yy = y + ch / 2
                if gable:
                    # under the roof line: the gable's height above the eaves falls off from the middle
                    sc = s + ln / 2
                    lim = h + (ridge - h) * max(0.0, 1 - abs(sc - L / 2) / (L / 2)) - 0.05
                    if yy + ch / 2 > lim:
                        s += ln
                        continue
                if not in_opening(name, s, s + ln, y, y + ch):
                    gap = 0.012 if quoin else rng.uniform(0.006, 0.02)
                    sy = ch / 2 - (0.004 if quoin else rng.uniform(0.004, 0.02))
                    R = rot_xyz(rng.normal(0, 0.025), rng.normal(0, 0.03), rng.normal(0, 0.03))
                    put(o, a, n, s + ln / 2, yy, ln / 2 - gap / 2, sy, 0.17 if not quoin else 0.24, R,
                        bright=0.6 if quoin else None, bare=0.6 if quoin else None, res=(5, 8) if not quoin else (6, 8))
                s += ln
            # Through stones in rows on the long walls.
            if not gable and course in (5, 9):
                for st in np.arange(0.9, L - 0.6, rng.uniform(1.0, 1.4)):
                    if not in_opening(name, st - 0.15, st + 0.15, y, y + 0.1):
                        R = rot_xyz(0, rng.normal(0, 0.03), rng.normal(0, 0.03))
                        put(o, a, n, st, y + ch, 0.15, 0.06, 0.32, R, bare=0.5)
            y += ch
            course += 1
        # Openings: lintels and jambs, and wood in them.
        for (a0, a1, b0, b1, kind) in openings[name]:
            span = a1 - a0
            # lintel
            R = rot_xyz(0, 0, rng.normal(0, 0.01))
            put(o, a, n, (a0 + a1) / 2, b1 + 0.13, span / 2 + 0.25, 0.13, 0.3, R, bright=0.58, bare=0.7, res=(6, 12))
            if kind == "cart":
                # A segmental arch of voussoirs above the lintel line.
                for k in range(9):
                    th = math.pi * (0.15 + 0.7 * k / 8)
                    r = span / 2 + 0.2
                    cs, cy_ = (a0 + a1) / 2 - math.cos(th) * r, b1 + 0.15 + math.sin(th) * r * 0.45
                    put(o, a, n, cs, cy_, 0.12, 0.2, 0.25, rot_xyz(0, 0, -(th - math.pi / 2) * 0.8), bright=0.55, res=(5, 8))
            # Jambs: dressed stones up each side.
            for side_s in (a0 - 0.16, a1 + 0.16):
                yy = max(b0, 0.0)
                k = 0
                while yy < b1 - 0.05:
                    hh = min(0.32, b1 - yy)
                    put(o, a, n, side_s, yy + hh / 2, 0.15 if k % 2 else 0.2, hh / 2 - 0.006, 0.22,
                        rot_xyz(0, 0, rng.normal(0, 0.01)), bright=0.58, bare=0.7, res=(5, 8))
                    yy += hh; k += 1
            # Wood: doors or a shutter, set back in the opening.
            wood = np.array([0.16, 0.11, 0.07]) * rng.uniform(0.7, 1.1)
            plank = 0.18
            ss = a0
            while ss < a1 - 0.01:
                pw = min(plank, a1 - ss)
                lo = o + a * ss + n * (-0.22) + np.array([0, b0, 0])
                hi = o + a * (ss + pw - 0.008) + n * (-0.18) + np.array([0, b1, 0])
                P, T, _ = box(np.minimum(lo, hi), np.maximum(lo, hi))
                c = np.zeros((8, 4)); c[:, :3] = wood * rng.uniform(0.8, 1.15); c[:, 3] = rng.uniform(0.2, 0.7)
                Wd.add(P, T, c, np.stack([P[:, 0] + P[:, 2], P[:, 1]], 1))
                ss += pw

    # Gable copes (tabling) up each verge, and kneelers at the eaves.
    for xg in (-w / 2, w / 2):
        for side in (-1, 1):
            n_k = 8
            for k in range(n_k):
                f0 = k / n_k; f1 = (k + 1) / n_k
                z0 = side * (d / 2 + 0.1) * (1 - f0); z1 = side * (d / 2 + 0.1) * (1 - f1)
                y0 = h + (ridge - h) * f0; y1 = h + (ridge - h) * f1
                c = np.array([xg, (y0 + y1) / 2 + 0.12, (z0 + z1) / 2])
                ang = math.atan2(y1 - y0, abs(z1 - z0))
                R = rot_xyz(side * ang, 0, 0)
                P, T, uv = stone(rng, (t_wall / 2 + 0.04, 0.09, math.hypot(z1 - z0, y1 - y0) / 2 - 0.01), c, R,
                                 res=(5, 8), round_=(0.2, 0.3), chops=2, bump=0.006)
                col = stone_col(rng, len(P), bright=0.5, bare=0.8, lichen=0.7)
                col[:, 3] = 0.45
                S.add(P, T, col, uv)

    # The roof: stone slates in courses, big at the eaves and smaller towards the ridge.
    run = (d / 2 + 0.25) / math.cos(pitch)
    for side in (-1, 1):
        u = 0.0
        row = 0
        while u < run - 0.05:
            course_h = 0.42 - 0.24 * (u / run) + rng.uniform(-0.03, 0.03)
            s = -w / 2 - 0.15 - rng.uniform(0, 0.3)
            while s < w / 2 + 0.15:
                ln = rng.uniform(0.3, 0.65) * (1.0 - 0.3 * u / run)
                ln = min(ln, w / 2 + 0.2 - s)
                if ln < 0.08:
                    break
                um = u + course_h / 2
                zz = side * (d / 2 + 0.25 - um * math.cos(pitch))
                yy = h - 0.25 * math.sin(pitch) + um * math.sin(pitch) + 0.05
                R = rot_xyz(side * (pitch + 0.035), rng.normal(0, 0.02), rng.normal(0, 0.02))
                P, T, uv = stone(rng, (ln / 2 - 0.006, 0.025 + rng.uniform(0, 0.012), course_h * 0.62), (s + ln / 2, yy, zz),
                                 R, res=(4, 8), round_=(0.15, 0.25), chops=2, chop_depth=(0.75, 0.95), bump=0.004)
                col = stone_col(rng, len(P), bright=0.28 + rng.normal(0, 0.06), bare=1.0, lichen=rng.uniform(0.2, 0.9) ** 2)
                col[:, 3] = np.clip(0.25 + 0.5 * (1 - u / run) * rng.uniform(0, 1) ** 2, 0, 1)
                S.add(P, T, col, uv)
                s += ln
            u += course_h * 0.62
            row += 1
    # Ridge stones.
    s = -w / 2 - 0.1
    while s < w / 2 + 0.1:
        ln = min(0.6, w / 2 + 0.15 - s)
        P, T, uv = stone(rng, (ln / 2 - 0.01, 0.12, 0.2), (s + ln / 2, ridge + 0.12, 0), rot_xyz(math.pi / 4, 0, 0),
                         res=(5, 8), round_=(0.2, 0.3), chops=2, bump=0.004)
        col = stone_col(rng, len(P), bright=0.4, bare=1.0, lichen=0.5)
        col[:, 3] = 0.3
        S.add(P, T, col, uv)
        s += ln
    return S, Wd


# --- Sheep -------------------------------------------------------------------------------------------

def sheep(rng, pose):
    """A Swaledale ewe, about 1.3 m nose to tail, facing +x. pose: 'graze', 'look' or 'lie'."""
    M = Mesh()
    fleece = np.array([0.38, 0.355, 0.3])
    lie = pose == "lie"
    body_y = 0.38 if lie else 0.66
    # Body: a lumpy ellipsoid of coarse fleece.
    w = Waves(rng, 18, (2.0, 14.0), 1.0)
    P = _unit(18, 28, 0.85, 0.9) * [0.52, 0.3 if lie else 0.29, 0.29 if lie else 0.27]
    P[:, 0] += 0.06 * (P[:, 1] / 0.3)          # a little higher at the shoulders
    nrm = P / np.maximum(np.linalg.norm(P, axis=1, keepdims=True), 1e-9)
    P += nrm * (np.abs(w(P)) * 0.06)[:, None]
    P[:, 1] += body_y
    if lie:
        P[:, 1] = np.maximum(P[:, 1], 0.03)
    T = _topology(18, 28)
    c = np.zeros((len(P), 4))
    under = np.clip((body_y - P[:, 1]) / 0.25, 0, 1)
    c[:, :3] = fleece * (1 - 0.35 * under)[:, None] * rng.uniform(0.92, 1.05)
    c[:, :3] *= (1 + 0.12 * w(P * 0.5))[:, None]
    c[:, 3] = under * 0.6
    M.add(P, T, c, P[:, [0, 1]])

    # Head: dark, with the Swaledale's white muzzle and eye rings.
    if pose == "graze":
        neck = np.array([0.5, body_y + 0.05, 0]); head = np.array([0.72, 0.18, 0]); tilt = -1.25
    elif lie:
        neck = np.array([0.45, body_y + 0.12, 0]); head = np.array([0.62, 0.48, rng.uniform(-0.1, 0.1)]); tilt = -0.3
    else:
        neck = np.array([0.5, body_y + 0.08, 0]); head = np.array([0.7, 0.95, rng.uniform(-0.12, 0.12)]); tilt = -0.45
    # neck of fleece
    path = np.linspace(neck, head + (neck - head) * 0.2, 6)
    Pn, Tn = tube(path, np.linspace(0.17, 0.1, 6), 10)
    cn = np.zeros((len(Pn), 4)); cn[:, :3] = fleece * 0.95
    M.add(Pn, Tn, cn, Pn[:, [0, 1]])
    R = rot_xyz(0, 0, tilt)
    Ph = _unit(10, 14, 0.8, 0.85) * [0.15, 0.085, 0.075]
    Ph[:, 0] *= 1 + 0.25 * (Ph[:, 0] < 0)            # deeper at the back of the skull
    Ph[:, 1] *= 1 - 0.3 * np.clip(Ph[:, 0] / 0.15, 0, 1)  # tapering to the nose
    loc = Ph @ R.T + head
    ch = np.zeros((len(Ph), 4)); ch[:, :3] = 0.025
    muzzle = (Ph[:, 0] > 0.1)
    eye = (np.abs(Ph[:, 2]) > 0.05) & (Ph[:, 0] > -0.02) & (Ph[:, 0] < 0.04) & (Ph[:, 1] > 0.0)
    ch[muzzle | eye, :3] = 0.55
    M.add(loc, _topology(10, 14), ch, Ph[:, [0, 1]])
    # Ears and curled horns.
    for side in (-1, 1):
        ear = np.array([-0.06, 0.03, side * 0.09])
        Pe, Te = tube(np.array([ear, ear + [-0.04, -0.01, side * 0.08]]) @ R.T + head, [0.022, 0.01], 6)
        ce = np.zeros((len(Pe), 4)); ce[:, :3] = 0.03
        M.add(Pe, Te, ce, Pe[:, [0, 1]])
        a = np.linspace(0, 1.6 * math.pi, 14)
        rad = 0.07 * (1 - 0.45 * a / a[-1])
        hp = np.stack([-0.08 - np.sin(a) * rad, 0.07 + np.cos(a) * rad - 0.07, side * (0.06 + a * 0.012)], 1)
        Ph_, Th_ = tube(hp @ R.T + head, np.linspace(0.022, 0.006, 14), 6)
        chn = np.zeros((len(Ph_), 4)); chn[:, :3] = [0.32, 0.28, 0.2]
        M.add(Ph_, Th_, chn, Ph_[:, [0, 1]])
    # Legs: black and white, thin.
    if not lie:
        for lx in (-0.33, 0.33):
            for lz in (-0.12, 0.12):
                knee = rng.normal(0, 0.02)
                path = np.array([[lx, body_y - 0.12, lz], [lx + knee, 0.25, lz], [lx, 0.0, lz]])
                Pl, Tl = tube(path, [0.045, 0.028, 0.026], 6)
                cl = np.zeros((len(Pl), 4)); cl[:, :3] = 0.04
                cl[Pl[:, 1] > 0.15, :3] = 0.35
                M.add(Pl, Tl, cl, Pl[:, [0, 1]])
    # Tail.
    Pt, Tt = tube(np.array([[-0.5, body_y + 0.05, 0], [-0.6, body_y - 0.15, 0]]), [0.06, 0.04], 6)
    ct = np.zeros((len(Pt), 4)); ct[:, :3] = fleece * 0.85
    M.add(Pt, Tt, ct, Pt[:, [0, 1]])
    return M
