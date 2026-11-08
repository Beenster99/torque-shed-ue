"""Stone-by-stone meshes for the Buttertubs countryside, written as glb by buttertubs_detail.py into
Data/Buttertubs/Props and placed in the level as Nanite instances (ue_build_level.py reads props.json).

    SM_Wall_A..H      2 m of Dales drystone wall: footings, battered faces laid in courses, through
                      stones, hearting in the gaps, and a row of coping stones on edge along the top
    SM_WallFar        the same 2 m as one light shell (about 300 triangles) with a toothed cope line,
                      for walls in the distance when ue_build_level.py uses LODs instead of Nanite
    SM_WallEnd_A..D   a wall head: a 1 m length finished at +x with headers tied across, tapering down
    SM_GateStoop      a gate post: a tall limestone upright
    SM_Gate           a galvanised five-bar field gate, 3.4 m, hinged at local x = 0
    SM_Rock_00..11    limestone boulders, about 1 m across, scaled when placed
    SM_Scree_A..B     a spill of small stones about 1.6 m across (A is swapped for the Megascans
                      Small Limestone Rocks Pack in prop_swaps.json where the PC has it)
    SM_Scar_A..D      4 m of limestone scar: beds of blocks split by joints, stepped back up the face
    SM_Bld_N_*        the map's buildings as light shells: two-storey Swaledale houses (in terraces
                      where their footprints meet, some rendered or limewashed, porches, front garden
                      walls), farms, field barns, sheds, the church, chapel, school and pub; rubble
                      walls (masonry texture) with dressed quoins and surrounds, stone or blue slate
                      roofs, gable chimney stacks, sash or mullioned windows, painted doors; one mesh
                      per material (_Masonry, _Render, _Stone, _Wood, _Glass). barn() is the older
                      stone-by-stone field barn.
    SM_Sheep_A..C     a Swaledale ewe grazing, looking up, and lying down

The local frame is the glb's: metres, x along, y up, z across (z is the face for scars). Ground is
y = 0 and everything goes some way below it, so it sits into uneven ground.

Vertex colour is a set of masks for the materials, not a colour:
    stone:  R brightness (0.5 = the texture as it is), G which of the two stone scans (0 lichened
            limestone, 1 bare), B lichen, A moss
    fleece, metal, wood, glass, render: RGB the colour (linear), A dirt
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

    def two_sided(self):
        """Every triangle also wound the other way (with its own vertices, so its own normals): for
        the thin wall shells of the houses, which are seen from inside through windows and gaps."""
        off = 0
        for k in range(len(self.P)):
            P, T, C, U = self.P[k], self.T[k], self.C[k], self.U[k]
            base = off; off += len(P)
            self.P.append(P.copy()); self.C.append(C.copy()); self.U.append(U.copy())
            self.T.append((T - base)[:, [0, 2, 1]] + self.n)
            self.n += len(P)
        return self

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
            # The head is battered like the faces (it leans back as it rises) and laid as a cheek
            # end: a header right across the wall, then a pair of long stretchers, in turn.
            inset = 0.07 * yb / WALL_H
            if (ci % 2 == 0) == (sgn < 0):
                ln = rng.uniform(0.3, 0.4); h = ch * 0.94
                cy = yb + h / 2 + 0.004
                w = hw_at(cy)
                R = rot_xyz(rng.normal(0, 0.015), rng.normal(0, 0.02), rng.normal(0, 0.02))
                P, Tt, uv = stone(rng, (ln / 2, h / 2, w + 0.015), (xe + sgn * (inset + ln / 2), cy, 0.0), R, res=(6, 12),
                                  round_=(0.12, 0.2), chops=3, chop_depth=(0.85, 0.97))
                col = stone_col(rng, len(P), bare=rng.uniform(0.2, 0.7))
                col[:, 3] = 0.06
                M.add(P, Tt, col, uv)
            else:
                for side in (-1, 1):
                    ln = rng.uniform(0.5, 0.65); h = ch * 0.92
                    cy = yb + h / 2 + 0.004
                    w = hw_at(cy) / 2
                    R = rot_xyz(-side * batter, rng.normal(0, 0.02), rng.normal(0, 0.02))
                    P, Tt, uv = stone(rng, (ln / 2, h / 2, w - 0.005), (xe + sgn * (inset + ln / 2), cy, side * w), R, res=(6, 12),
                                      round_=(0.12, 0.2), chops=3, chop_depth=(0.85, 0.97))
                    col = stone_col(rng, len(P), bare=rng.uniform(0.2, 0.7))
                    col[:, 3] = 0.06
                    M.add(P, Tt, col, uv)
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
    x = x0 + (0.03 if end_lo else s)
    hi = x1 + (-0.03 if end_hi else s)
    # A through stone near the head, tying it, sticking out a hand's width each side.
    for at_end, xe, sgn in ((end_hi, x1, -1), (end_lo, x0, 1)):
        if not at_end:
            continue
        P, Tt, uv = stone(rng, (0.13, 0.065, hw_at(0.55) + 0.1), (xe + sgn * 0.42, 0.56, 0.0),
                          rot_xyz(0, rng.normal(0, 0.05), 0), res=(6, 12))
        col = stone_col(rng, len(P)); col[:, 3] = 0.15
        M.add(P, Tt, col, uv)
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


def wall_far(rng):
    """2 m of wall as one shell: battered faces, footings, and a band of copes with a ragged top
    (on edge, so the skyline of a wall reads right from far off). Same masks and UVs as the stones."""
    M = Mesh()
    xs = np.linspace(-WALL_L / 2, WALL_L / 2, 9)
    # Faces: (side, up) profile swept along x.
    prof = [(-FOOT_HW - 0.03, -0.3), (-FOOT_HW - 0.02, 0.12), (-TOP_HW, WALL_H), (TOP_HW, WALL_H),
            (FOOT_HW + 0.02, 0.12), (FOOT_HW + 0.03, -0.3)]
    n, m = len(xs), len(prof)
    P = np.zeros((n, m, 3))
    for j, (sd, up) in enumerate(prof):
        P[:, j, 0] = xs; P[:, j, 1] = up + (rng.normal(0, 0.02, n) if 0 < up < WALL_H else 0)
        P[:, j, 2] = sd + (rng.normal(0, 0.012, n) if 0 < up < WALL_H else 0)
    idx = np.arange(n * m).reshape(n, m)
    T = []
    for j in range(m - 1):
        a, b, c, d = idx[:-1, j], idx[:-1, j + 1], idx[1:, j], idx[1:, j + 1]
        T += [np.stack([a, b, c], 1), np.stack([b, d, c], 1)]
    T = np.concatenate(T)
    P = P.reshape(-1, 3)
    fc = P[T].mean(1); fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    out = fc.copy(); out[:, 0] = 0; out[:, 1] -= 0.4
    flip = (fn * out).sum(1) < 0
    T[flip] = T[flip][:, [0, 2, 1]]
    col = np.zeros((len(P), 4)); col[:, 0] = 0.3; col[:, 1] = 1.0; col[:, 2] = 0.15   # darker: no gaps to shade it
    col[:, 3] = np.clip((0.25 - P[:, 1]) / 0.4, 0, 1) * 0.8
    M.add(P, T, col, np.stack([P[:, 0] * 1.7, P[:, 1] * 1.7 + np.abs(P[:, 2])], 1))
    # Copes: a box per run of a few stones, tops at their own heights.
    x = -WALL_L / 2
    while x < WALL_L / 2 - 0.02:
        t = min(rng.uniform(0.18, 0.4), WALL_L / 2 - x)
        h = rng.uniform(0.2, 0.34)
        w = TOP_HW + rng.uniform(0.02, 0.06)
        Pb, Tb, uvb = box((x, WALL_H - 0.04, -w), (x + t - 0.01, WALL_H + h, w))
        cb = np.zeros((8, 4)); cb[:, 0] = 0.36 + rng.normal(0, 0.05); cb[:, 1] = rng.uniform(0.3, 1); cb[:, 2] = 0.3
        cb[:, 3] = np.where(Pb[:, 1] > WALL_H + 0.1, 0.35, 0.15)
        M.add(Pb, Tb, cb, uvb + rng.uniform(0, 5, 2))
        x += t
    return M


def wall_module(rng):
    M = Mesh()
    wall_length(M, rng, -WALL_L / 2, WALL_L / 2)
    return M


def wall_end(rng, length=1.0, both=False):
    """A wall head: the last metre finished at +x, and tapered: the wall comes down about a third
    towards its end (its copes following), as Dales wall ends are, rather than standing square."""
    M = Mesh()
    wall_length(M, rng, -length / 2, length / 2, end_lo=both, end_hi=True)
    for P in M.P:
        t = np.clip((P[:, 0] + length / 2) / length, 0, 1) ** 1.3
        f = 1.0 - 0.33 * t
        up = P[:, 1] > 0.15
        P[up, 1] = 0.15 + (P[up, 1] - 0.15) * f[up]
        # and narrower to the end, the faces leaning in a little more
        P[:, 2] *= 1.0 - 0.12 * t
    return M


def gate_stoop(rng):
    M = Mesh()
    P, T, uv = stone(rng, (0.19, 0.95, 0.16), (0, 0.55, 0), rot_xyz(rng.normal(0, 0.02), rng.normal(0, 0.05), rng.normal(0, 0.02)),
                     res=(10, 12), round_=(0.1, 0.16), chops=3, chop_depth=(0.85, 0.97))   # a squared limestone post
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


# --- Houses, farms and barns as light shells -----------------------------------------------------------

def obox(c, h, R=None, uv_off=(0.0, 0.0)):
    """A box with flat faces (24 vertices): centre c, half sizes h, rotation R. UVs in metres."""
    hx, hy, hz = h
    faces = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
    P, T, U = [], [], []
    hv = np.array([hx, hy, hz])
    for n, u, v in faces:
        n, u, v = np.array(n, float), np.array(u, float), np.array(v, float)
        base = len(P)
        for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = n * hv + u * hv * a + v * hv * b
            P.append(p)
            U.append([(p * u).sum() + uv_off[0], (p * v).sum() + uv_off[1]])
        T += [[base, base + 1, base + 2], [base, base + 2, base + 3]]
    P = np.array(P)
    T = np.array(T)
    # outward winding check per face
    fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    fc = P[T].mean(1)
    bad = (fn * fc).sum(1) < 0
    T[bad] = T[bad][:, [0, 2, 1]]
    if R is not None:
        P = P @ np.asarray(R).T
    return P + np.asarray(c, float), T, np.array(U)


def quad(M, a, b, c, d, col, uv):
    """A quad a-b-c-d (counter-clockwise seen from its front)."""
    M.add(np.array([a, b, c, d], float), [[0, 1, 2], [0, 2, 3]], col, uv)


def _cells(L, y0, y1, holes, x0=0.0, cuts=()):
    """Rectangles covering [x0, L] x [y0, y1] minus the holes (s0, s1, b0, b1), also split at the
    heights in cuts (so vertex colour can change there)."""
    holes = [h for h in holes if h[1] > x0 and h[0] < L]
    xs = sorted({x0, L} | {min(max(h[0], x0), L) for h in holes} | {min(max(h[1], x0), L) for h in holes})
    ys = sorted({y0, y1} | {h[2] for h in holes if y0 < h[2] < y1} | {h[3] for h in holes if y0 < h[3] < y1}
                | {c for c in cuts if y0 < c < y1})
    out = []
    for i in range(len(xs) - 1):
        for j in range(len(ys) - 1):
            if xs[i + 1] - xs[i] < 1e-4 or ys[j + 1] - ys[j] < 1e-4:
                continue
            cx, cy = (xs[i] + xs[i + 1]) / 2, (ys[j] + ys[j + 1]) / 2
            if any(h[0] < cx < h[1] and h[2] < cy < h[3] for h in holes):
                continue
            out.append((xs[i], xs[i + 1], ys[j], ys[j + 1]))
    return out


# Lime render and limewash on some village houses (linear RGB), and the paint on doors and frames.
RENDERS = [(0.72, 0.68, 0.58), (0.68, 0.62, 0.48), (0.6, 0.57, 0.5), (0.74, 0.7, 0.6), (0.64, 0.58, 0.47)]   # warm off-whites and creams
DOOR_PAINT = [(0.02, 0.06, 0.03), (0.03, 0.04, 0.09), (0.01, 0.01, 0.012), (0.12, 0.02, 0.02), (0.3, 0.3, 0.28),
              (0.2, 0.12, 0.05), (0.05, 0.1, 0.12), (0.25, 0.22, 0.12)]


def house_front(rng, kind, w, h, layout=None):
    """Openings (s0, s1, b0, b1, kind) on a house front w long with eaves h: the door and windows in
    bays, the upper row over the lower one on two storeys. layout: 'double' (door in the middle,
    windows either side), 'cottage' (door at one end) or 'single' (a door and a window)."""
    two = h >= 4.4
    if layout is None:
        layout = "single" if w < 4.6 else ("cottage" if w < 7.0 or rng.uniform() < 0.3 else "double")
    mull = kind not in ("pub", "lodge") and rng.uniform() < 0.22          # older houses: two-light mullioned windows
    ww = rng.uniform(1.4, 1.8) if mull else rng.uniform(0.85, 1.12)
    sill = rng.uniform(0.75, 1.0)
    head = sill + (rng.uniform(1.0, 1.15) if mull else rng.uniform(1.25, 1.45))
    up_head = h - rng.uniform(0.28, 0.45)
    up_h = rng.uniform(0.95, 1.1) if mull else rng.uniform(1.0, 1.25)
    if not two:
        up_head = None
    wk = "mull" if mull else "window"
    if layout == "single":
        door_left = rng.uniform() < 0.5
        bays = [("door", 0.3 + 0.5) if door_left else ("door", w - 0.8)]
        wx = w * 0.62 if door_left else w * 0.38
        bays.append(("win", wx))
    else:
        if layout == "double":
            nb = 7 if w > 17 else 5 if w > 11.5 else 3
        else:
            nb = 3 if w > 6.4 else 2
        edge = rng.uniform(0.9, 1.3)
        xs = np.linspace(edge, w - edge, nb) if nb > 1 else np.array([w / 2])
        if layout == "double":
            door = nb // 2
        else:
            door = 0 if rng.uniform() < 0.5 else nb - 1
        bays = [("door" if i == door else "win", x) for i, x in enumerate(xs)]
    out = []
    dw = rng.uniform(0.95, 1.05)
    for k, x in bays:
        if k == "door":
            out.append((x - dw / 2, x + dw / 2, 0.0, 2.05, "door"))
            if up_head and (layout == "double" or rng.uniform() < 0.6):
                sw = min(ww, 0.9) if not mull else 0.7      # over the door: a smaller window (the landing)
                out.append((x - sw / 2, x + sw / 2, up_head - up_h * 0.85, up_head, "window"))
        else:
            out.append((x - ww / 2, x + ww / 2, sill, head, wk))
            if up_head:
                out.append((x - ww / 2, x + ww / 2, up_head - up_h, up_head, wk))
            if h >= 7.0:     # three storeys: a middle row
                mid_top = (head + up_head - up_h) / 2 + up_h / 2 + 0.1
                out.append((x - ww / 2, x + ww / 2, mid_top - up_h - 0.1, mid_top, wk))
    return out, layout


def building(rng, kind, w, d, h, plinth, opt=None):
    """A Swaledale building as light shells: rubble walls (masonry texture) with openings cut in them,
    dressed quoins, lintels, sills and jambs, a stone (or blue) slate roof, gable copes and kneelers,
    chimney stacks on the gables, sash or mullioned windows and painted doors; a cart door under an
    arch on barns.
    kind: 'house', 'pub', 'lodge' (three storeys), 'farm' (a house with its barn under one roof), 'barn', 'shed', 'church',
    'chapel' (gable to the street, at +x) or 'school'. w along x (the ridge), d across, the front
    is +z; eaves h above ground at the centre, walls carried plinth below it.
    opt (all optional):
        party   (h_left, h_right): eaves of the house next door at -x / +x (a terrace), else None
        render  an RGB colour: the walls are rendered or limewashed, not bare stone
        roof    'stone' (stone slates in diminishing courses) or 'welsh' (blue slate)
        porch   a gabled stone porch over the front door
        layout  'double', 'cottage' or 'single' (see house_front)
        garden  (f, drop): a low front wall f metres out from the front, its ground drop metres
                below this one's, with a gap and a flagged path to the door
        sign    a hanging sign by the door (the pub)
    Returns {material: Mesh} for 'masonry', 'render', 'stone', 'wood' and 'glass'."""
    opt = dict(opt or {})
    if d > w:
        out = building(rng, kind, d, w, h, plinth, opt)
        for m in out.values():
            m.P = [p[:, [2, 1, 0]] * [1, 1, -1] for p in m.P]
        return out
    Wl, Rd, St, Wd, Gl = Mesh(), Mesh(), Mesh(), Mesh(), Mesh()
    house = kind in ("house", "farm", "pub", "lodge")
    party = opt.get("party") or (None, None)
    render = opt.get("render")
    roof = opt.get("roof", "stone")
    big = kind in ("church", "chapel", "school")
    pitch = math.radians(rng.uniform(30, 35) if house else rng.uniform(36, 40) if big else rng.uniform(26, 31))
    ridge = h + (d / 2) * math.tan(pitch)
    bright = 0.5 + rng.normal(0, 0.06)
    door_col = np.array(DOOR_PAINT[int(rng.integers(len(DOOR_PAINT)))])
    frame_col = np.array([0.62, 0.62, 0.6]) if rng.uniform() < 0.75 else door_col
    rcol = np.array(render) * rng.uniform(0.95, 1.04) if render is not None else None

    def own(side):
        """Whether this house draws the gable/party end at side (0 = -x, 1 = +x): its own gable, or a
        party wall it is taller at (or the left one of an even pair, so nothing is drawn twice)."""
        hn = party[side]
        return hn is None or h > hn + 0.05 or (abs(h - hn) <= 0.05 and side == 0)

    def mcol(n, y):
        c = np.zeros((n, 4)); c[:, 0] = bright + rng.normal(0, 0.02); c[:, 2] = 0.05
        c[:, 3] = np.clip((0.5 - np.asarray(y)) / 0.8, 0, 1) * 0.4
        return c

    def rendcol(n, y):
        c = np.zeros((n, 4)); c[:, :3] = rcol
        c[:, 3] = np.clip((0.7 - np.asarray(y)) / 0.9, 0, 1) * 0.55 + 0.04
        return c

    def wall_quad(p, ys, uv):
        if rcol is not None:
            quad(Rd, *p, rendcol(4, ys), uv)
        else:
            quad(Wl, *p, mcol(4, ys), uv)

    def wall_box(c, hs, R, ylo):
        P, T, U = obox(c, hs, R, rng.uniform(0, 5, 2))
        if rcol is not None:
            Rd.add(P, T, rendcol(len(P), P[:, 1]), U)
        else:
            Wl.add(P, T, mcol(len(P), P[:, 1]), U)

    def scol(n, b=0.55, bare=0.8, moss=0.06):
        c = np.zeros((n, 4)); c[:, 0] = b + rng.normal(0, 0.03); c[:, 1] = bare; c[:, 2] = 0.3; c[:, 3] = moss
        return c

    # Openings on the front (+z), the back, and the gable ends.
    house_len = w if kind in ("house", "pub", "lodge") else (min(9.0, w * 0.45) if kind == "farm" else 0.0)
    front, back, left, right = [], [], [], []
    layout = None
    if house_len > 0:
        front, layout = house_front(rng, kind, house_len, h, opt.get("layout"))
        nbk = max(1, int(house_len / 3.5))
        for i in range(nbk):
            cx = (i + 0.5) * house_len / nbk + rng.uniform(-0.4, 0.4)
            if h >= 4.4:
                back.append((cx - 0.4, cx + 0.4, h - 1.5, h - 0.5, "window"))
            back.append((cx - 0.45, cx + 0.45, 1.0, 2.1, "window") if i or nbk == 1 and rng.uniform() < 0.5
                        else (cx - 0.45, cx + 0.45, 0.0, 2.0, "door"))
        if party[0] is None and rng.uniform() < 0.35 and h >= 4.4:
            left.append((d / 2 - 0.3, d / 2 + 0.3, h + 0.2, h + 0.95, "window"))
    if kind in ("farm", "barn"):
        b0 = house_len + 0.6
        b1 = w - 0.6
        if b1 - b0 > 3.2:
            cx = (b0 + b1) / 2 + rng.uniform(-0.3, 0.3) * (b1 - b0 - 3.2)
            front.append((cx - 1.3, cx + 1.3, 0.0, min(3.0, h - 0.5), "cart"))
            if b1 - b0 > 7:
                for fx in (b0 + 0.8, b1 - 1.4):
                    front.append((fx, fx + 0.6, h - 1.3, h - 0.7, "hole"))
            back.append((cx - 0.3, cx + 0.3, h - 1.3, h - 0.8, "hole"))
            if b1 - b0 > 6:
                front.append((b0 + 0.3, b0 + 1.2, 0.0, 1.9, "door"))
            # Small square muck holes and slit vents low down, as on Swaledale cow houses.
            for sx in np.arange(b0 + 0.6, b1 - 0.4, 2.4):
                if not any(s0 - 0.5 < sx < s1 + 0.5 for (s0, s1, *_r) in front):
                    front.append((sx - 0.2, sx + 0.2, 1.2, 1.6, "hole"))
        elif b1 - b0 > 1.2:
            cx = (b0 + b1) / 2
            front.append((cx - 0.5, cx + 0.5, 0.0, 2.0, "door"))
        if kind == "barn" and rng.uniform() < 0.6:
            left.append((d / 2 - 0.35, d / 2 + 0.35, h - 0.3, h + 0.5, "hole"))   # the forking hole in the gable
    if kind == "shed":
        front.append((w / 2 - 0.5, w / 2 + 0.5, 0.0, min(2.0, h - 0.3), "door"))
    if kind == "church":
        n = max(3, int(w / 4.5))
        for i in range(n):
            cx = (i + 0.5) * w / n + 0.4
            if i == 0:
                front.append((cx - 0.55, cx + 0.55, 0.0, 2.4, "door"))
                opt["porch_x"] = cx
            else:
                front.append((cx - 0.35, cx + 0.35, 1.3, 3.7, "tall"))
            back.append((w - cx - 0.35, w - cx + 0.35, 1.3, 3.7, "tall"))
        right.append((d / 2 - 0.9, d / 2 + 0.9, 1.6, h + 1.2, "tall"))    # the east window
        left.append((d / 2 - 0.3, d / 2 + 0.3, h - 1.0, h + 0.4, "tall"))
    if kind == "chapel":
        # The gable at +x is the front: a door between two tall windows, a small one above.
        right += [(d / 2 - 0.6, d / 2 + 0.6, 0.0, 2.5, "door"),
                  (d * 0.18 - 0.45, d * 0.18 + 0.45, 1.2, 3.6, "tall"), (d * 0.82 - 0.45, d * 0.82 + 0.45, 1.2, 3.6, "tall"),
                  (d / 2 - 0.4, d / 2 + 0.4, h - 0.2, h + 0.8, "window")]
        n = max(2, int(w / 3.6))
        for i in range(n):
            cx = (i + 0.5) * w / n
            front.append((cx - 0.5, cx + 0.5, 1.2, 3.8, "tall"))
            back.append((cx - 0.5, cx + 0.5, 1.2, 3.8, "tall"))
    if kind == "school":
        n = max(3, int(w / 3.2))
        for i in range(n):
            cx = (i + 0.5) * w / n
            if i == n - 1:
                front.append((cx - 0.5, cx + 0.5, 0.0, 2.2, "door"))
                opt["porch_x"] = cx
            else:
                front.append((cx - 0.6, cx + 0.6, 1.0, 3.0, "tall"))
            back.append((cx - 0.6, cx + 0.6, 1.0, 3.0, "tall"))

    faces = {  # origin (s = 0 corner at ground), along, outward, length, openings
        "front": (np.array([-w / 2, 0, d / 2]), np.array([1.0, 0, 0]), np.array([0, 0, 1.0]), w, front),
        "back": (np.array([w / 2, 0, -d / 2]), np.array([-1.0, 0, 0]), np.array([0, 0, -1.0]), w,
                 [(w - b, w - a, y0, y1, k) for (a, b, y0, y1, k) in back]),
        "left": (np.array([-w / 2, 0, -d / 2]), np.array([0, 0, 1.0]), np.array([-1.0, 0, 0]), d, left),
        "right": (np.array([w / 2, 0, d / 2]), np.array([0, 0, -1.0]), np.array([1.0, 0, 0]), d,
                  [(d - b, d - a, y0, y1, k) for (a, b, y0, y1, k) in right]),
    }
    up = np.array([0, 1.0, 0])
    door_spots = []
    for name, (o, a, n, L, holes) in faces.items():
        side = 0 if name == "left" else 1 if name == "right" else None
        y_from = -plinth
        if side is not None and party[side] is not None:
            if not own(side):
                continue                   # the house next door carries the party wall
            holes = []                     # a party wall, the whole of it (footprints rarely match exactly)
        R = np.stack([a, up, n], 1)
        # The wall: rectangles round the openings, and reveals in them.
        hl = [(s0, s1, b0, b1) for (s0, s1, b0, b1, _) in holes]
        if y_from is not None:
            top = h + 0.07 if side is None else h      # long walls run up under the slates: no slit at the eaves
            for (s0, s1, y0, y1) in _cells(L, y_from, top, hl, cuts=(0.0, 0.7)):   # the foot's dirt and moss stay at the foot
                p = [o + a * s0 + up * y0, o + a * s1 + up * y0, o + a * s1 + up * y1, o + a * s0 + up * y1]
                wall_quad(p, [y0, y0, y1, y1], [[s0, y0], [s1, y0], [s1, y1], [s0, y1]])
        # Gable: masonry triangle above the eaves on the ends, cut round a gable window if any.
        if side is not None:
            A = o + up * h; B = o + a * L + up * h; C = o + a * (L / 2) + up * ridge
            gh = [hh for hh in holes if hh[3] > h]
            if not gh:
                p3 = np.array([A, B, C])
                if rcol is not None:
                    Rd.add(p3, [[0, 1, 2]], rendcol(3, [h, h, ridge]), [[0, h], [L, h], [L / 2, ridge]])
                else:
                    Wl.add(p3, [[0, 1, 2]], mcol(3, [h, h, ridge]), [[0, h], [L, h], [L / 2, ridge]])
            else:
                # Split the triangle into a band round the window and the triangle above it.
                s0, s1, b0, b1, _k = gh[0]
                top = b1 + 0.05
                half = (ridge - top) / math.tan(pitch)
                sa, sb = L / 2 - half, L / 2 + half
                for (x0, x1, y0, y1) in _cells(L, h, top, [(s0, s1, b0, b1)]):
                    # clip to the gable slope: trapezoid cells (corners pulled in under the verges)
                    def lim(s, y):
                        m = (y - h) / math.tan(pitch)
                        return min(max(s, m), L - m)
                    p = [o + a * lim(x0, y0) + up * y0, o + a * lim(x1, y0) + up * y0,
                         o + a * lim(x1, y1) + up * y1, o + a * lim(x0, y1) + up * y1]
                    wall_quad(p, [y0, y0, y1, y1], [[x0, y0], [x1, y0], [x1, y1], [x0, y1]])
                p3 = np.array([o + a * sa + up * top, o + a * sb + up * top, C])
                if rcol is not None:
                    Rd.add(p3, [[0, 1, 2]], rendcol(3, [top, top, ridge]), [[sa, top], [sb, top], [L / 2, ridge]])
                else:
                    Wl.add(p3, [[0, 1, 2]], mcol(3, [top, top, ridge]), [[sa, top], [sb, top], [L / 2, ridge]])
        for (s0, s1, b0, b1, k) in holes:
            dep = 0.22 if k != "cart" else 0.35
            for (pa, pb) in (((s0, b0), (s0, b1)), ((s1, b1), (s1, b0)), ((s1, b1), (s0, b1)), ((s0, b0), (s1, b0))):
                A = o + a * pa[0] + up * pa[1]; B = o + a * pb[0] + up * pb[1]
                wall_quad([A, B, B - n * dep, A - n * dep], [pa[1], pb[1], pb[1], pa[1]],
                          [[0, pa[1]], [0, pb[1]], [dep, pb[1]], [dep, pa[1]]])
            mid = o + a * ((s0 + s1) / 2)
            if k == "door" and name == "front":
                door_spots.append((s0 + s1) / 2 - w / 2)
            # Dressed surrounds: lintel, sill, jambs (cart doors get an arch of voussoirs; tall windows
            # a pointed or round head of voussoirs).
            if k == "cart":
                r = (s1 - s0) / 2 + 0.15
                for i in range(11):
                    th = math.pi * (0.06 + 0.88 * i / 10)
                    c = mid + a * (-math.cos(th) * r) + up * (b1 - 0.25 + math.sin(th) * r * 0.42 + 0.25) - n * 0.27
                    Rv = R @ rot_xyz(0, 0, th - math.pi / 2)
                    P, T, U = obox(c, (0.11, 0.2, 0.3), Rv, rng.uniform(0, 5, 2))
                    St.add(P, T, scol(len(P), 0.6), U)
            elif k == "tall":
                r = (s1 - s0) / 2 + 0.12
                for i in range(7):
                    th = math.pi * (0.05 + 0.9 * i / 6)
                    c = mid + a * (-math.cos(th) * r) + up * (b1 + math.sin(th) * r * 0.55) - n * 0.24
                    Rv = R @ rot_xyz(0, 0, th - math.pi / 2)
                    P, T, U = obox(c, (0.09, 0.13, 0.26), Rv, rng.uniform(0, 5, 2))
                    St.add(P, T, scol(len(P), 0.62), U)
                P, T, U = obox(mid + up * (b0 - 0.06) - n * 0.22, ((s1 - s0) / 2 + 0.1, 0.06, 0.3), R, rng.uniform(0, 5, 2))
                St.add(P, T, scol(len(P), 0.62, moss=0.12), U)
                # fill the arch head over the opening (masonry tympanum stands in for glass up there)
                q0 = o + a * s0 + up * b1; q1 = o + a * s1 + up * b1
                Wl.add(np.array([q0 - n * dep, q1 - n * dep, mid + up * (b1 + r * 0.5) - n * dep]), [[0, 1, 2]],
                       mcol(3, [b1, b1, b1]), [[0, 0], [1, 0], [0.5, 0.5]])
            else:
                if k in ("window", "mull") and rcol is not None and rng.uniform() < 0.5:
                    # painted surrounds on a rendered house
                    pc = np.array([0.03, 0.03, 0.03]) if rng.uniform() < 0.6 else np.array([0.55, 0.55, 0.52])
                    for (cc, hs) in (((mid + up * (b1 + 0.08)), ((s1 - s0) / 2 + 0.1, 0.08)),
                                     ((mid + up * (b0 - 0.05)), ((s1 - s0) / 2 + 0.1, 0.05)),
                                     ((o + a * (s0 - 0.06) + up * (b0 + b1) / 2), (0.06, (b1 - b0) / 2)),
                                     ((o + a * (s1 + 0.06) + up * (b0 + b1) / 2), (0.06, (b1 - b0) / 2))):
                        P, T, U = obox(cc + n * 0.012, (hs[0], hs[1], 0.012), R)
                        c = np.zeros((len(P), 4)); c[:, :3] = pc; c[:, 3] = 0.2
                        Wd.add(P, T, c, U)
                else:
                    P, T, U = obox(mid + up * (b1 + 0.11) - n * 0.28, ((s1 - s0) / 2 + 0.18, 0.11, 0.3), R, rng.uniform(0, 5, 2))
                    St.add(P, T, scol(len(P), 0.6), U)
                    if k in ("window", "mull"):
                        P, T, U = obox(mid + up * (b0 - 0.05) - n * 0.24, ((s1 - s0) / 2 + 0.1, 0.05, 0.3), R, rng.uniform(0, 5, 2))
                        St.add(P, T, scol(len(P), 0.62, moss=0.12), U)
                    if k != "hole" or b1 - b0 > 0.5:
                        for sj in (s0 - 0.09, s1 + 0.09):
                            yy = max(b0, -0.2)
                            while yy < b1 - 0.05:
                                hh = min(0.3, b1 - yy)
                                ext = 0.12 if int(yy / 0.3) % 2 else 0.06
                                P, T, U = obox(o + a * sj + up * (yy + hh / 2) - n * 0.235, (0.09 + ext * 0.3, hh / 2 - 0.005, 0.25), R, rng.uniform(0, 5, 2))
                                St.add(P, T, scol(len(P), 0.6), U)
                                yy += hh
            # What fills the opening, set back in it.
            back_p = lambda s, y, k_=0.0: o + a * s + up * y - n * (dep - 0.02 - k_)
            if k in ("window", "mull", "tall"):
                g = [back_p(s0, b0), back_p(s1, b0), back_p(s1, b1), back_p(s0, b1)]
                gc = np.zeros((4, 4)); gc[:, :3] = 0.02
                quad(Gl, *g, gc, [[0, 0], [1, 0], [1, 1], [0, 1]])
                cm = (s0 + s1) / 2
                if k == "window":      # a pair of sashes, each of two (or six) panes
                    bars = [(s0 + 0.03, b0, s0 + 0.07, b1), (s1 - 0.07, b0, s1 - 0.03, b1), (s0, b1 - 0.07, s1, b1 - 0.03),
                            (s0, b0 + 0.03, s1, b0 + 0.08), (cm - 0.02, b0, cm + 0.02, b1),
                            (s0, (b0 + b1) / 2 - 0.03, s1, (b0 + b1) / 2 + 0.03)]
                    if (s1 - s0) > 0.95 and rng.uniform() < 0.4:
                        for t in (1 / 3, 2 / 3):
                            sx = s0 + (s1 - s0) * t
                            bars.append((sx - 0.012, b0, sx + 0.012, b1))
                    fcol = frame_col
                elif k == "mull":      # casements either side of a stone mullion
                    P, T, U = obox(o + a * cm + up * ((b0 + b1) / 2) - n * 0.2, (0.08, (b1 - b0) / 2, 0.2), R, rng.uniform(0, 5, 2))
                    St.add(P, T, scol(len(P), 0.6), U)
                    bars = [(s0 + 0.02, b0, s0 + 0.06, b1), (s1 - 0.06, b0, s1 - 0.02, b1), (s0, b1 - 0.06, s1, b1 - 0.02),
                            (s0, b0 + 0.02, s1, b0 + 0.06), (s0, (b0 + b1) / 2 - 0.02, s1, (b0 + b1) / 2 + 0.02)]
                    fcol = door_col if rng.uniform() < 0.5 else frame_col
                else:                  # tall: a light frame and glazing bars
                    bars = [(cm - 0.02, b0, cm + 0.02, b1)] + [(s0, y - 0.02, s1, y + 0.02) for y in np.linspace(b0, b1, 5)[1:-1]]
                    fcol = np.array([0.5, 0.5, 0.48])
                for (x0, y0, x1, y1) in bars:
                    P, T, U = obox(o + a * ((x0 + x1) / 2) + up * ((y0 + y1) / 2) - n * (dep - 0.06),
                                   ((x1 - x0) / 2, (y1 - y0) / 2, 0.03), R)
                    c = np.zeros((len(P), 4)); c[:, :3] = fcol; c[:, 3] = 0.15
                    Wd.add(P, T, c, U)
            elif k in ("door", "cart", "hole"):
                col = door_col if (k == "door" and (house or big)) else np.array([0.13, 0.09, 0.06]) * rng.uniform(0.7, 1.1)
                planks = max(2, int((s1 - s0) / 0.16))
                pw = (s1 - s0) / planks
                for i in range(planks):
                    P, T, U = obox(o + a * (s0 + (i + 0.5) * pw) + up * ((max(b0, 0) + b1) / 2) - n * (dep - 0.04),
                                   (pw / 2 - 0.004, (b1 - max(b0, 0)) / 2, 0.025), R, (rng.uniform(0, 3), 0))
                    c = np.zeros((len(P), 4)); c[:, :3] = col * rng.uniform(0.85, 1.12); c[:, 3] = rng.uniform(0.2, 0.6)
                    Wd.add(P, T, c, U)
                if k == "door" and house and rng.uniform() < 0.5:   # a fanlight over the door
                    P, T, U = obox(o + a * ((s0 + s1) / 2) + up * (b1 - 0.2) - n * (dep - 0.07), ((s1 - s0) / 2, 0.18, 0.02), R)
                    c = np.zeros((len(P), 4)); c[:, :3] = 0.02
                    Gl.add(P, T, c, U)

        # Quoins up the corners, long and short in turn, both faces; none on a rendered house, and
        # none where the house next door carries on.
        if rcol is None and not (side is not None and party[side] is not None):
            yy = -plinth
            kq = 0
            while yy < h - 0.05:
                hh = min(0.3, h - yy)
                for end in (0, 1):
                    if name == "front" and party[end] is not None or name == "back" and party[1 - end] is not None:
                        continue
                    ln = 0.5 if (kq + end) % 2 == 0 else 0.3
                    sc = ln / 2 if end == 0 else L - ln / 2
                    P, T, U = obox(o + a * sc + up * (yy + hh / 2) - n * 0.185, (ln / 2, hh / 2 - 0.006, 0.2), R, rng.uniform(0, 5, 2))
                    St.add(P, T, scol(len(P), 0.6, moss=0.25 if yy < 0.4 else 0.05), U)
                yy += hh; kq += 1
        # Through stones in a row on the long walls of barns.
        if kind in ("barn",) and name in ("front", "back"):
            for st in np.arange(0.9, L - 0.6, 1.2):
                if not any(s0 - 0.2 < st < s1 + 0.2 and b0 - 0.2 < h * 0.55 < b1 + 0.2 for (s0, s1, b0, b1, _) in holes):
                    P, T, U = obox(o + a * st + up * (h * 0.55) + n * 0.02, (0.13, 0.06, 0.1), R, rng.uniform(0, 5, 2))
                    St.add(P, T, scol(len(P), 0.55, moss=0.4), U)
        # A plain eaves band (gutter shadow) on rendered houses.
        if rcol is not None and name in ("front", "back"):
            P, T, U = obox(o + a * (L / 2) + up * (h - 0.06) + n * 0.06, (L / 2, 0.06, 0.06), R)
            c = np.zeros((len(P), 4)); c[:, :3] = 0.02; c[:, 3] = 0.3
            Wd.add(P, T, c, U)

    # Roof: courses of stone slates, wide at the eaves and narrower to the ridge; or blue slate.
    ext = [0.25 if party[0] is None else 0.0, 0.25 if party[1] is None else 0.0]
    xa, xb = -w / 2 - ext[0], w / 2 + ext[1]
    run = (d / 2 + 0.3) / math.cos(pitch)
    welsh = roof == "welsh"
    rb = (0.2 if welsh else 0.3) + rng.normal(0, 0.03)
    for sd in (-1, 1):
        u = 0.0
        while u < run - 0.02:
            if welsh:
                ch = 0.24
            else:
                ch = max(0.16, 0.42 - 0.24 * u / run) * rng.uniform(0.9, 1.1)
            seg = min(ch * 1.6, run - u + 0.05)
            um = u + seg / 2
            zz = sd * (d / 2 + 0.3 - um * math.cos(pitch))
            yy = h - 0.3 * math.sin(pitch) + um * math.sin(pitch) + 0.06
            R = rot_xyz(sd * (pitch + (0.015 if welsh else 0.03)), 0, 0)
            P, T, U = obox(((xa + xb) / 2, yy, zz), ((xb - xa) / 2, 0.012 if welsh else 0.022, seg / 2), R, (rng.uniform(0, 5), u))
            moss = 0.03 if welsh else np.clip(0.06 + 0.22 * (1 - u / run) ** 2, 0, 1)   # moss low on the eaves, not a green roof
            c = scol(len(P), rb + rng.normal(0, 0.015 if welsh else 0.03), 1.0, moss)
            if welsh:
                c[:, 2] = 0.02
            St.add(P, T, c, U)
            u += ch
    for s in np.arange(xa + 0.05, xb - 0.05, 0.55):
        sl = min(0.275, (xb - s) / 2)
        P, T, U = obox((s + sl, ridge + 0.1, 0), (sl, 0.11, 0.11), rot_xyz(math.pi / 4, 0, 0), rng.uniform(0, 5, 2))
        St.add(P, T, scol(len(P), 0.4 if not welsh else 0.25, 1.0, 0.12 if not welsh else 0.05), U)
    # Gable copes up the verges, kneelers at the eaves (only where this house owns the gable).
    for side, xg in ((0, -w / 2), (1, w / 2)):
        if party[side] is not None and not own(side):
            continue
        if party[side] is not None and abs(h - party[side]) <= 0.05:
            continue    # an even party wall: the roof runs straight on
        if rcol is not None and party[side] is None and rng.uniform() < 0.5:
            continue    # plain verges on some rendered houses
        for sd in (-1, 1):
            z0, z1 = sd * (d / 2 + 0.12), 0.0
            ln = math.hypot(z1 - z0, ridge - h)
            R = rot_xyz(sd * pitch, 0, 0)
            P, T, U = obox((xg, (h + ridge) / 2 + 0.16, (z0 + z1) / 2), (0.36, 0.09, ln / 2), R, rng.uniform(0, 5, 2))
            St.add(P, T, scol(len(P), 0.5, 0.8, 0.35), U)
            if party[side] is None:
                P, T, U = obox((xg, h - 0.05, sd * (d / 2 - 0.05)), (0.36, 0.2, 0.26), None, rng.uniform(0, 5, 2))
                St.add(P, T, scol(len(P), 0.55), U)
    # Chimney stacks on the gables (straddling a party wall), and at the house end of a farm.
    if house:
        spots = []
        for side, xg in ((0, -w / 2), (1, w / 2)):
            if kind == "farm" and side == 1:
                spots.append((-w / 2 + house_len, False))
            elif party[side] is None:
                if side == 0 or rng.uniform() < 0.8:
                    spots.append((xg + (0.35 if side == 0 else -0.35), False))
            elif own(side):
                spots.append((xg, True))
        sh = rng.uniform(0.8, 1.25)
        sw = rng.uniform(0.28, 0.36)
        for sx, wide in spots:
            hz = 0.45 if not wide else 0.6
            P, T, U = obox((sx, ridge + sh / 2 - 0.2, 0), (sw if not wide else 0.4, sh / 2 + 0.2, hz), None, rng.uniform(0, 5, 2))
            if rcol is not None and rng.uniform() < 0.5:
                Rd.add(P, T, rendcol(len(P), P[:, 1]), U)
            else:
                St.add(P, T, scol(len(P), 0.5, 0.4, 0.1), U)
            P, T, U = obox((sx, ridge + sh + 0.06, 0), ((sw if not wide else 0.4) + 0.06, 0.06, hz + 0.07), None, rng.uniform(0, 5, 2))
            St.add(P, T, scol(len(P), 0.55, 0.8, 0.2), U)
            npots = int(rng.integers(0, 3)) + (1 if wide else 0)
            for zp in np.linspace(-hz + 0.2, hz - 0.2, npots) if npots > 1 else ([0.0] if npots else []):
                Pp, Tp = tube([[sx, ridge + sh + 0.1, zp], [sx, ridge + sh + 0.1 + rng.uniform(0.25, 0.45), zp]], rng.uniform(0.08, 0.11), 8)
                c = np.zeros((len(Pp), 4)); c[:, 0] = 0.32 + rng.uniform(0, 0.1); c[:, 1] = 1.0
                St.add(Pp, Tp, c, Pp[:, [0, 1]])
    # Bellcote on the west gable of a church or school.
    if kind in ("church", "school"):
        P, T, U = obox((-w / 2, ridge + 0.6, 0), (0.25, 0.75, 0.5), None, rng.uniform(0, 5, 2))
        Wl.add(P, T, mcol(len(P), P[:, 1]), U)
        P, T, U = obox((-w / 2, ridge + 1.4, 0), (0.3, 0.08, 0.58), None, rng.uniform(0, 5, 2))
        St.add(P, T, scol(len(P), 0.55), U)
    # A gabled stone porch over the front door.
    px = opt.get("porch_x") if opt.get("porch_ok", True) else None
    if px is None and opt.get("porch") and door_spots:
        px = door_spots[0]
    elif px is not None:
        px = px - w / 2
    porch_depth = 0.0
    if px is not None:
        pw, pd, pe = 2.0, rng.uniform(1.1, 1.5), 2.45
        porch_depth = pd
        z0 = d / 2
        pp = math.radians(40)
        pr = pe + (pw / 2) * math.tan(pp)
        Rz = np.eye(3)
        for sx in (px - pw / 2 + 0.11, px + pw / 2 - 0.11):
            wall_box((sx, (pe - plinth) / 2, z0 + pd / 2), (0.11, (pe + plinth) / 2, pd / 2), Rz, -plinth)
        for sx in (px - pw / 2 + 0.25, px + pw / 2 - 0.25):   # piers either side of the doorway
            wall_box((sx, (2.15 - plinth) / 2, z0 + pd - 0.11), (0.25, (2.15 + plinth) / 2, 0.11), Rz, -plinth)
        wall_box((px, (2.15 + pe) / 2, z0 + pd - 0.11), (pw / 2, (pe - 2.15) / 2, 0.11), Rz, 2.15)
        tri = np.array([[px - pw / 2, pe, z0 + pd], [px + pw / 2, pe, z0 + pd], [px, pr, z0 + pd]])
        if rcol is not None:
            Rd.add(tri, [[0, 1, 2]], rendcol(3, [pe, pe, pr]), [[0, pe], [pw, pe], [pw / 2, pr]])
        else:
            Wl.add(tri, [[0, 1, 2]], mcol(3, [pe, pe, pr]), [[0, pe], [pw, pe], [pw / 2, pr]])
        # roof slabs along z, sloping either way across x
        sl = (pw / 2 + 0.2) / math.cos(pp)
        for sd in (-1, 1):
            c = (px + sd * (pw / 4 + 0.05), pe + (pw / 4) * math.tan(pp) + 0.05, z0 + pd / 2 + 0.05)
            P, T, U = obox(c, (sl / 2, 0.03, pd / 2 + 0.15), rot_xyz(0, 0, -sd * pp), rng.uniform(0, 5, 2))
            cc = scol(len(P), rb, 1.0, 0.04 if welsh else 0.12)
            St.add(P, T, cc, U)
        P, T, U = obox((px, pr + 0.08, z0 + pd / 2 + 0.05), (0.1, 0.08, pd / 2 + 0.15), rot_xyz(0, 0, 0), rng.uniform(0, 5, 2))
        St.add(P, T, scol(len(P), 0.45, 1.0, 0.2), U)
        # its door, set in the porch front
        nplank = 6
        for i in range(nplank):
            P, T, U = obox((px - 0.45 + (i + 0.5) * 0.15, 1.05, z0 + pd - 0.14), (0.074, 1.05, 0.025), None, (rng.uniform(0, 3), 0))
            c = np.zeros((len(P), 4)); c[:, :3] = door_col * rng.uniform(0.9, 1.1); c[:, 3] = 0.3
            Wd.add(P, T, c, U)
    # The pub's sign on a bracket by the door.
    if opt.get("sign") and door_spots:
        sx = door_spots[0] + 1.3
        P, T, U = obox((sx, 3.1, d / 2 + 0.45), (0.03, 0.03, 0.45), None)
        c = np.zeros((len(P), 4)); c[:, :3] = 0.02; Wd.add(P, T, c, U)
        P, T, U = obox((sx, 2.6, d / 2 + 0.5), (0.025, 0.4, 0.33), None)
        c = np.zeros((len(P), 4)); c[:, :3] = (0.12, 0.03, 0.02); c[:, 3] = 0.2; Wd.add(P, T, c, U)
    # A low front wall with flat copes, a gap and gate piers at the path, and a flagged path.
    gd = opt.get("garden")
    if gd is not None and gd[0] > porch_depth + 1.0:
        f, drop = gd
        zf = d / 2 + f
        gy = -drop
        gx = door_spots[0] if door_spots else (px if px is not None else 0.0)
        gap = (gx - 0.55, gx + 0.55)
        x0 = -w / 2 if party[0] is not None else -w / 2 - 0.15
        x1 = w / 2 if party[1] is not None else w / 2 + 0.15
        wh = rng.uniform(0.7, 0.95)
        runs = [(x0, gap[0]), (gap[1], x1)] if x0 < gap[0] < gap[1] < x1 else [(x0, x1)]
        for (ra, rb_) in runs:
            if rb_ - ra < 0.2:
                continue
            P, T, U = obox(((ra + rb_) / 2, gy + (wh - 0.6) / 2, zf), ((rb_ - ra) / 2, (wh + 0.6) / 2, 0.22), None, rng.uniform(0, 5, 2))
            Wl.add(P, T, mcol(len(P), P[:, 1] - gy), U)
            for s in np.arange(ra, rb_ - 0.05, 0.6):
                e = min(s + 0.6, rb_)
                P, T, U = obox(((s + e) / 2, gy + wh + 0.06, zf), ((e - s) / 2 - 0.01, 0.07, 0.27), None, rng.uniform(0, 5, 2))
                St.add(P, T, scol(len(P), 0.58, 0.8, 0.4), U)
        if len(runs) == 2:
            for sx in gap:
                P, T, U = obox((sx, gy + (wh + 0.35 - 0.6) / 2, zf), (0.2, (wh + 0.35 + 0.6) / 2, 0.24), None, rng.uniform(0, 5, 2))
                St.add(P, T, scol(len(P), 0.6, 0.8, 0.3), U)
            # flags from the door (or porch) out to the gate
            za, zb = d / 2 + porch_depth + 0.02, zf - 0.22
            nz = max(1, int((zb - za) / 0.7))
            for i in range(nz):
                q0 = za + (zb - za) * i / nz; q1 = za + (zb - za) * (i + 1) / nz
                yf = 0.03 - drop * ((q0 + q1) / 2 - d / 2) / f
                P, T, U = obox((gx, yf, (q0 + q1) / 2), (0.45, 0.05, (q1 - q0) / 2 - 0.015), None, rng.uniform(0, 5, 2))
                St.add(P, T, scol(len(P), 0.58, 0.9, 0.2), U)
        # side walls back to the house where it ends (not between terraced neighbours)
        for side, xs_ in ((0, x0), (1, x1)):
            if party[side] is not None:
                continue
            P, T, U = obox((xs_, gy + (wh - 0.6) / 2, (d / 2 + zf) / 2), (0.2, (wh + 0.6) / 2 + max(0.0, drop) / 2, (zf - d / 2) / 2), None, rng.uniform(0, 5, 2))
            Wl.add(P, T, mcol(len(P), P[:, 1] - gy), U)
    return {"masonry": Wl.two_sided(), "render": Rd.two_sided(), "stone": St, "wood": Wd, "glass": Gl.two_sided()}


def far_building(w, d, h, rot, x, y_lo, y, z, kind):
    """Far off, a building is a box and a roof (positions in the ride frame), stone grey and slate
    dark: (positions, triangles, colours 0..1)."""
    pitch = math.radians(32 if kind in ("house", "farm") else 28)
    if d > w:
        w, d = d, w
        rot += math.pi / 2
    ridge = h + (d / 2) * math.tan(pitch)
    c, s = math.cos(rot), math.sin(rot)
    def at(u, yy, v):
        return [x + u * c - v * s, yy, z + u * s + v * c]
    P = [at(u, yy, v) for yy in (y_lo, y + h) for (u, v) in ((-w / 2, -d / 2), (w / 2, -d / 2), (w / 2, d / 2), (-w / 2, d / 2))]
    T = [[0, 4, 1], [1, 4, 5], [1, 5, 2], [2, 5, 6], [2, 6, 3], [3, 6, 7], [3, 7, 0], [0, 7, 4]]
    col = [[0.066, 0.066, 0.059]] * 4 + [[0.053, 0.055, 0.049]] * 4
    e = 0.3
    R = [at(-w / 2 - e, y + h - 0.1, -d / 2 - e), at(w / 2 + e, y + h - 0.1, -d / 2 - e), at(w / 2 + e, y + h - 0.1, d / 2 + e),
         at(-w / 2 - e, y + h - 0.1, d / 2 + e), at(-w / 2 - e, y + ridge, 0), at(w / 2 + e, y + ridge, 0)]
    n = len(P)
    P += R
    T += [[n + 0, n + 1, n + 5], [n + 0, n + 5, n + 4], [n + 2, n + 3, n + 4], [n + 2, n + 4, n + 5]]
    col += [[0.03, 0.032, 0.034]] * 6
    # gable triangles
    n = len(P)
    P += [at(-w / 2, y + h, -d / 2), at(-w / 2, y + h, d / 2), at(-w / 2, y + ridge, 0),
          at(w / 2, y + h, -d / 2), at(w / 2, y + h, d / 2), at(w / 2, y + ridge, 0)]
    T += [[n, n + 2, n + 1], [n + 3, n + 4, n + 5]]
    col += [[0.062, 0.062, 0.056]] * 6
    return np.array(P), np.array(T), np.array(col)


# --- Sheep -------------------------------------------------------------------------------------------

def sheep(rng, pose, fleece=(0.38, 0.355, 0.3)):
    """A Swaledale ewe, about 1.3 m nose to tail, facing +x. pose: 'graze', 'look' or 'lie'. A lamb
    is the same at about 0.55 scale with a cleaner, whiter fleece."""
    M = Mesh()
    fleece = np.array(fleece)
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


# --- Street furniture ------------------------------------------------------------------------------
# Each returns {material: Mesh}: 'wood' and 'metal' are vertex-coloured paint and steel (RGB colour,
# A dirt), 'glass' panes, 'stone' as everywhere. Local frame: ground y = 0, the front is +z.

def _paint(M, P, T, U, rgb, dirt=0.15):
    c = np.zeros((len(P), 4)); c[:, :3] = rgb; c[:, 3] = dirt
    M.add(P, T, c, U)


def _pole(M, x0, y0, y1, r, rgb, z0=0.0, dirt=0.2):
    P, T = tube([[x0, y0, z0], [x0, y1, z0]], r, 8)
    _paint(M, P, T, P[:, [0, 1]] + P[:, [2, 2]], rgb, dirt)


RED = (0.42, 0.008, 0.006)        # telephone and post box red, linear


def phone_box(rng):
    """A K6 telephone kiosk: 0.9 m square, 2.5 m tall, red cast iron, glazed in rows of small panes on
    three sides and the door, a domed roof and a white crown panel each side."""
    Wd, Gl = Mesh(), Mesh()
    hw, H = 0.45, 2.36
    for sx in (-1, 1):
        for sz in (-1, 1):
            P, T, U = obox((sx * (hw - 0.05), H / 2, sz * (hw - 0.05)), (0.06, H / 2, 0.06))
            _paint(Wd, P, T, U, RED)
    P, T, U = obox((0, 0.06, 0), (hw + 0.03, 0.08, hw + 0.03)); _paint(Wd, P, T, U, RED, 0.5)
    P, T, U = obox((0, H - 0.12, 0), (hw + 0.02, 0.14, hw + 0.02)); _paint(Wd, P, T, U, RED)
    for k in range(4):        # the dome, in steps
        r = hw + 0.03 - k * 0.1
        P, T, U = obox((0, H + 0.04 + k * 0.05, 0), (r, 0.03, r)); _paint(Wd, P, T, U, RED)
    for ang in range(4):      # each face: crown panel, glazing bars, glass
        R = rot_xyz(0, ang * math.pi / 2, 0)
        P, T, U = obox(np.array([0, H - 0.12, hw + 0.025]) @ R.T, (0.28, 0.07, 0.005), R); _paint(Wd, P, T, U, (0.75, 0.75, 0.72), 0.05)
        if ang == 2:
            P, T, U = obox(np.array([0, 1.2, hw - 0.02]) @ R.T, (hw - 0.1, 1.05, 0.02), R); _paint(Wd, P, T, U, RED)   # the back: solid
            continue
        g = np.array([[-hw + 0.1, 0.25, hw - 0.03], [hw - 0.1, 0.25, hw - 0.03], [hw - 0.1, H - 0.28, hw - 0.03], [-hw + 0.1, H - 0.28, hw - 0.03]]) @ R.T
        quad(Gl, *g, np.tile([0.03, 0.03, 0.03, 0.0], (4, 1)), [[0, 0], [1, 0], [1, 1], [0, 1]])
        for x in np.linspace(-hw + 0.1, hw - 0.1, 4):
            P, T, U = obox(np.array([x, (H - 0.03) / 2, hw - 0.025]) @ R.T, (0.018, (H - 0.53) / 2, 0.02), R); _paint(Wd, P, T, U, RED)
        for y in np.linspace(0.25, H - 0.28, 9):
            P, T, U = obox(np.array([0, y, hw - 0.025]) @ R.T, (hw - 0.1, 0.016, 0.02), R); _paint(Wd, P, T, U, RED)
    return {"wood": Wd.two_sided(), "glass": Gl.two_sided()}


def post_box(rng):
    """A pillar box: a red cylinder 1.6 m tall on a black base, a cap with a rim, the slot."""
    Wd = Mesh()
    _pole(Wd, 0, -0.2, 0.12, 0.25, (0.01, 0.01, 0.01), dirt=0.6)
    _pole(Wd, 0, 0.1, 1.42, 0.23, RED)
    _pole(Wd, 0, 1.42, 1.48, 0.26, RED)
    P, T = tube([[0, 1.48, 0], [0, 1.56, 0], [0, 1.6, 0]], [0.23, 0.18, 0.06], 12)
    _paint(Wd, P, T, P[:, [0, 1]], RED)
    P, T, U = obox((0, 1.2, 0.225), (0.13, 0.02, 0.02)); _paint(Wd, P, T, U, (0.01, 0.01, 0.01))
    P, T, U = obox((0, 0.85, 0.226), (0.07, 0.09, 0.01)); _paint(Wd, P, T, U, (0.7, 0.7, 0.68), 0.05)   # the times plate
    return {"wood": Wd}


def bench(rng):
    """A park bench: five wooden slats on two black cast ends, 1.8 m long, facing +z."""
    Wd, Mt = Mesh(), Mesh()
    wood = np.array([0.16, 0.09, 0.045]) * rng.uniform(0.8, 1.2)
    for i, (y, z, ang) in enumerate(((0.45, 0.12, 0.0), (0.45, 0.0, 0.0), (0.45, -0.12, 0.0), (0.62, -0.22, 1.35), (0.8, -0.25, 1.35))):
        P, T, U = obox((0, y, z), (0.9, 0.02, 0.05), rot_xyz(ang, 0, 0), (rng.uniform(0, 3), 0))
        _paint(Wd, P, T, U, wood * rng.uniform(0.9, 1.1), rng.uniform(0.2, 0.5))
    for x in (-0.78, 0.78):
        for (a, b) in (((x, 0.0, 0.15), (x, 0.43, 0.12)), ((x, 0.0, -0.2), (x, 0.43, -0.15)), ((x, 0.43, 0.15), (x, 0.43, -0.2)),
                       ((x, 0.43, -0.2), (x, 0.85, -0.27)), ((x, 0.62, 0.15), (x, 0.6, -0.1))):
            P, T = tube([a, b], 0.022, 6); _paint(Mt, P, T, P[:, [0, 1]], (0.015, 0.015, 0.015), 0.3)
    return {"wood": Wd, "metal": Mt}


def road_sign(rng, kind, text=""):
    """A sign on grey posts: 'village' (the place name in black capitals on a white plate with a
    black border) or 'junction' (a red-bordered triangle, side road ahead)."""
    Wd, Mt = Mesh(), Mesh()
    if kind == "village":
        th = 0.12
        hw = max(0.42, text_width(text, th) / 2 + 0.1)
        for px_ in (-hw + 0.12, hw - 0.12):
            _pole(Mt, px_, -0.3, 2.0, 0.035, (0.25, 0.25, 0.25), dirt=0.3)
        P, T, U = obox((0, 1.72, 0.04), (hw + 0.035, 0.17, 0.012)); _paint(Wd, P, T, U, (0.02, 0.02, 0.02), 0.1)
        P, T, U = obox((0, 1.72, 0.054), (hw, 0.135, 0.004)); _paint(Wd, P, T, U, (0.78, 0.78, 0.76), 0.05)
        lettering(Wd, text, (0, 1.72, 0.058), (1, 0, 0), (0, 1, 0), (0, 0, 1), th, (0.015, 0.015, 0.015))
    else:
        _pole(Mt, 0.0, -0.3, 2.1, 0.035, (0.25, 0.25, 0.25), dirt=0.3)
        tri = lambda r, zz: np.array([[0, 1.6 + r * 1.15, zz], [-r, 1.6 - r * 0.58, zz], [r, 1.6 - r * 0.58, zz]])
        for r, zz, col in ((0.42, 0.045, RED), (0.31, 0.05, (0.8, 0.8, 0.78))):
            A = tri(r, zz)
            Wd.add(A, [[0, 1, 2]], np.tile(list(col) + [0.05], (3, 1)), A[:, [0, 1]])
        P, T, U = obox((0, 1.55, 0.052), (0.025, 0.18, 0.002)); _paint(Wd, P, T, U, (0.02, 0.02, 0.02), 0.0)
        P, T, U = obox((0.08, 1.58, 0.052), (0.09, 0.022, 0.002)); _paint(Wd, P, T, U, (0.02, 0.02, 0.02), 0.0)
        Wd.two_sided()
    return {"wood": Wd, "metal": Mt}


def finger_post(rng, arms):
    """A Dales finger post: a white post 2.4 m with a finial, and an arm for each (angle, text) in arms
    (angle in radians about the vertical, 0 = +x), white with the place and miles in black on both
    faces."""
    Wd = Mesh()
    P, T, U = obox((0, 1.05, 0), (0.055, 1.35, 0.055)); _paint(Wd, P, T, U, (0.72, 0.72, 0.7), 0.35)
    P, T, U = obox((0, 2.44, 0), (0.08, 0.04, 0.08)); _paint(Wd, P, T, U, (0.02, 0.02, 0.02), 0.2)
    P, T = tube([[0, 2.48, 0], [0, 2.6, 0]], [0.05, 0.01], 8); _paint(Wd, P, T, P[:, [0, 1]], (0.02, 0.02, 0.02))
    for i, (a, text) in enumerate(arms):
        R = rot_xyz(0, -a, 0)
        y = 2.12 - i * 0.2
        th = 0.065                          # 65 mm capitals, as on the Dales' cast finger posts
        L = max(0.5, text_width(text, th) + 0.18)
        dirv = np.array([1.0, 0, 0]) @ R.T; nrm = np.array([0, 0, 1.0]) @ R.T
        c = np.array([0.06 + L / 2, y, 0]) @ R.T
        P, T, U = obox(c, (L / 2, 0.075, 0.022), R); _paint(Wd, P, T, U, (0.74, 0.74, 0.72), 0.3)
        P, T, U = obox(np.array([0.06 + L + 0.03, y, 0]) @ R.T, (0.04, 0.045, 0.018), R)   # the pointed tip
        _paint(Wd, P, T, U, (0.74, 0.74, 0.72), 0.3)
        for sgn in (1, -1):
            lettering(Wd, text, c + nrm * sgn * 0.022, dirv * sgn, (0, 1, 0), nrm * sgn, th, (0.015, 0.015, 0.015))
    return {"wood": Wd}


def churn_stand(rng):
    """A milk churn stand: a stone slab on two stone legs at lorry height, with two old churns."""
    St, Mt = Mesh(), Mesh()
    for x in (-0.5, 0.5):
        P, T, uv = stone(rng, (0.16, 0.6, 0.25), (x, 0.4, 0), None, res=(6, 10), round_=(0.12, 0.2), chops=2)
        c = stone_col(rng, len(P)); c[:, 3] = 0.3; St.add(P, T, c, uv)
    P, T, uv = stone(rng, (0.75, 0.07, 0.38), (0, 1.06, 0), None, res=(6, 10), round_=(0.12, 0.2), chops=3)
    c = stone_col(rng, len(P)); c[:, 3] = 0.4; St.add(P, T, c, uv)
    for x in (-0.3, 0.25):
        P, T = tube([[x, 1.13, 0], [x, 1.55, 0], [x, 1.68, 0], [x, 1.76, 0]], [0.17, 0.17, 0.09, 0.1], 12)
        _paint(Mt, P, T, P[:, [0, 1]], (0.35, 0.36, 0.35), 0.6)
    return {"stone": St, "metal": Mt}


def wheelie_bin(rng, rgb):
    """A wheelie bin, 0.58 by 0.74 by 1.07 m, lid hinged at the back (-z)."""
    Wd = Mesh()
    P, T, U = obox((0, 0.52, 0), (0.28, 0.5, 0.35)); _paint(Wd, P, T, U, rgb, 0.3)
    P, T, U = obox((0, 1.04, 0.02), (0.3, 0.025, 0.38)); _paint(Wd, P, T, U, np.array(rgb) * 0.8, 0.2)
    for x in (-0.24, 0.24):
        P, T = tube([[x, 0.1, -0.36], [x, 0.1, -0.3]], 0.1, 8); _paint(Wd, P, T, P[:, [0, 1]], (0.01, 0.01, 0.01), 0.5)
    return {"wood": Wd}


# --- Lettering -------------------------------------------------------------------------------------
# Capitals and figures drawn as strokes (straight runs and smooth arcs, round joins), in the manner of
# the road signs' lettering, built as geometry on a sign's face (no textures to manage). Glyphs are in
# units of a tenth of the cap height, y up; each entry is (advance width, [polylines]).


def _arc(cx, cy, rx, ry, a0, a1, n=None):
    n = n or max(4, int(abs(a1 - a0) / 12))
    t = np.radians(np.linspace(a0, a1, n + 1))
    return [(cx + rx * math.cos(u), cy + ry * math.sin(u)) for u in t]


FONT = {
    "A": (6.4, [[(0, 0), (3.2, 10), (6.4, 0)], [(1.2, 3.4), (5.2, 3.4)]]),
    "B": (5.8, [[(0, 5), (0, 10), (3.2, 10)] + _arc(3.2, 7.5, 2.3, 2.5, 90, -90) + [(0, 5), (3.4, 5)]
                + _arc(3.4, 2.5, 2.4, 2.5, 90, -90) + [(0, 0), (0, 5)]]),
    "C": (6.0, [_arc(3.3, 5, 3.3, 5, 48, 312, 26)]),
    "D": (6.2, [[(0, 0), (0, 10), (2.4, 10)] + _arc(2.4, 5, 3.8, 5, 90, -90, 20) + [(0, 0)]]),
    "E": (5.0, [[(5.0, 10), (0, 10), (0, 0), (5.0, 0)], [(0, 5.1), (4.2, 5.1)]]),
    "F": (4.9, [[(4.9, 10), (0, 10), (0, 0)], [(0, 5.1), (4.2, 5.1)]]),
    "G": (6.4, [_arc(3.3, 5, 3.3, 5, 48, 335, 26) + [(6.4, 4.4), (3.8, 4.4)]]),
    "H": (6.0, [[(0, 0), (0, 10)], [(6, 0), (6, 10)], [(0, 5.1), (6, 5.1)]]),
    "I": (0.0, [[(0, 0), (0, 10)]]),
    "J": (4.2, [[(4.2, 10), (4.2, 3.0)] + _arc(2.1, 3.0, 2.1, 3.0, 0, -180)]),
    "K": (5.8, [[(0, 0), (0, 10)], [(5.6, 10), (0, 3.4)], [(1.9, 5.6), (5.8, 0)]]),
    "L": (4.8, [[(0, 10), (0, 0), (4.8, 0)]]),
    "M": (7.4, [[(0, 0), (0, 10), (3.7, 2.2), (7.4, 10), (7.4, 0)]]),
    "N": (6.0, [[(0, 0), (0, 10), (6, 0), (6, 10)]]),
    "O": (6.8, [_arc(3.4, 5, 3.4, 5, 0, 360, 30)]),
    "P": (5.6, [[(0, 0), (0, 10), (3.0, 10)] + _arc(3.0, 7.4, 2.6, 2.6, 90, -90) + [(0, 4.8)]]),
    "Q": (6.8, [_arc(3.4, 5, 3.4, 5, 0, 360, 30), [(4.0, 2.4), (6.9, -0.6)]]),
    "R": (5.8, [[(0, 0), (0, 10), (3.0, 10)] + _arc(3.0, 7.4, 2.6, 2.6, 90, -90) + [(0, 4.8)], [(2.8, 4.8), (5.8, 0)]]),
    "S": (5.8, [_arc(2.9, 7.5, 2.7, 2.5, 25, 270) + _arc(2.9, 2.5, 2.9, 2.5, 90, -155)]),
    "T": (6.0, [[(0, 10), (6, 10)], [(3, 10), (3, 0)]]),
    "U": (6.0, [[(0, 10), (0, 3.2)] + _arc(3, 3.2, 3, 3.2, 180, 360) + [(6, 10)]]),
    "V": (6.4, [[(0, 10), (3.2, 0), (6.4, 10)]]),
    "W": (9.0, [[(0, 10), (2.2, 0), (4.5, 8.6), (6.8, 0), (9.0, 10)]]),
    "X": (6.0, [[(0, 10), (6, 0)], [(0, 0), (6, 10)]]),
    "Y": (6.2, [[(0, 10), (3.1, 5), (6.2, 10)], [(3.1, 5), (3.1, 0)]]),
    "Z": (5.8, [[(0, 10), (5.8, 10), (0, 0), (5.8, 0)]]),
    "0": (5.6, [_arc(2.8, 5, 2.8, 5, 0, 360, 30)]),
    "1": (2.8, [[(0, 7.8), (2.8, 10), (2.8, 0)]]),
    "2": (5.8, [_arc(2.9, 7.1, 2.8, 2.9, 165, -35) + [(0, 0), (5.8, 0)]]),
    "3": (5.8, [[(0.4, 10), (5.4, 10), (2.4, 6.2)] + _arc(2.8, 3.1, 3.0, 3.1, 95, -150)]),
    "4": (6.2, [[(4.6, 0), (4.6, 10), (0, 3), (6.2, 3)]]),
    "5": (5.8, [[(5.4, 10), (0.7, 10), (0.4, 5.6)] + _arc(2.8, 3.2, 3.0, 3.2, 135, -150)]),
    "6": (5.8, [[(4.6, 10), (0.6, 4.4)], _arc(2.9, 3.0, 2.9, 3.0, 0, 360, 24)]),
    "7": (5.8, [[(0, 10), (5.8, 10), (1.8, 0)]]),
    "8": (5.8, [_arc(2.9, 7.6, 2.5, 2.4, 0, 360, 22), _arc(2.9, 2.6, 2.9, 2.6, 0, 360, 24)]),
    "9": (5.8, [_arc(2.9, 7.0, 2.9, 3.0, 0, 360, 24), [(5.2, 5.6), (1.2, 0)]]),
    "-": (4.0, [[(0.3, 4.4), (3.7, 4.4)]]),
    " ": (2.6, []),
}
STROKE = 1.6         # stroke width, in tenths of the cap height
GAP = 1.9            # space between letters


def text_width(text, height):
    u = height / 10.0
    w = sum(FONT.get(ch, FONT[" "])[0] + STROKE + GAP for ch in text.upper()) - GAP
    return max(0.0, w) * u


def lettering(M, text, centre, right, up, normal, height, rgb):
    """Capitals `height` tall centred on `centre`, on the plane (right, up), set 2 mm out along normal."""
    right, up, normal = (np.asarray(v, float) for v in (right, up, normal))
    u = height / 10.0
    hw = STROKE * u / 2
    c0 = np.asarray(centre, float) + normal * 0.002
    flip = np.cross(right, up) @ normal < 0
    P, T = [], []

    def at(x, y):
        return c0 + right * x + up * y

    def poly(pts):                     # a convex polygon (counter-clockwise in the sign's plane)
        n0 = len(P)
        P.extend(pts)
        for k in range(1, len(pts) - 1):
            T.append([n0, n0 + k + 1, n0 + k] if flip else [n0, n0 + k, n0 + k + 1])

    x = -text_width(text, height) / 2 + hw
    for ch in text.upper():
        adv, lines = FONT.get(ch, FONT[" "])
        for line in lines:
            q = [(x + px_ * u, -height / 2 + py * u) for px_, py in line]
            for (x0, y0), (x1, y1) in zip(q[:-1], q[1:]):
                d = np.array([x1 - x0, y1 - y0]); L = float(np.hypot(*d))
                if L < 1e-6:
                    continue
                nx, ny = -d[1] / L * hw, d[0] / L * hw
                poly([at(x0 - nx, y0 - ny), at(x1 - nx, y1 - ny), at(x1 + nx, y1 + ny), at(x0 + nx, y0 + ny)])
            for px_, py in q:          # round joins and ends
                poly([at(px_ + hw * math.cos(t), py + hw * math.sin(t)) for t in np.linspace(0, 2 * math.pi, 10, endpoint=False)])
        x += (adv + STROKE + GAP) * u
    if P:
        P = np.array(P); n = len(P)
        col = np.tile(list(rgb) + [0.05], (n, 1))
        uv = np.stack([P @ right, P @ up], 1)
        M.add(P, np.array(T), col, uv)


# --- Vehicles and farm things ------------------------------------------------------------------------
# 'paint' is vertex-coloured gloss (car paint, bale wrap): RGB the colour, A dirt. Local +x is forward.

def _wheel(M, x, z, r, w, rgb=(0.012, 0.012, 0.012), hub=(0.2, 0.2, 0.2), Mh=None):
    side = 1 if z > 0 else -1
    P, T = tube([[x, r, z - side * w / 2], [x, r, z + side * w / 2]], r, 14)
    _paint(M, P, T, P[:, [0, 1]], rgb, 0.5)
    P, T = tube([[x, r, z + side * (w / 2 - 0.01)], [x, r, z + side * (w / 2 + 0.005)]], r * 0.55, 10)
    _paint(Mh if Mh is not None else M, P, T, P[:, [0, 1]], hub, 0.3)


def car(rng, rgb):
    """A small family hatchback, 4.0 by 1.75 by 1.45 m, in one paint colour: body, a glazed cabin
    stepped in from the sides, bumpers, lights and four wheels."""
    Pt, Bk, Gl = Mesh(), Mesh(), Mesh()
    L, W = 4.0, 1.75
    P, T, U = obox((0, 0.62, 0), (L / 2, 0.3, W / 2)); _paint(Pt, P, T, U, rgb, 0.15)
    P, T, U = obox((0.95, 0.88, 0), (0.75, 0.04, W / 2 - 0.04), rot_xyz(0, 0, -0.12)); _paint(Pt, P, T, U, rgb, 0.1)   # bonnet
    # cabin: pillars and roof in paint, glass between
    cx0, cx1, y0, y1 = -1.75, 0.55, 0.92, 1.42
    P, T, U = obox(((cx0 + cx1) / 2 - 0.05, y1, 0), ((cx1 - cx0) / 2 - 0.1, 0.03, W / 2 - 0.12)); _paint(Pt, P, T, U, rgb, 0.1)
    for x in (cx0 + 0.06, -0.55, cx1 - 0.06):
        for z in (-1, 1):
            P, T, U = obox((x, (y0 + y1) / 2, z * (W / 2 - 0.12)), (0.05, (y1 - y0) / 2, 0.03)); _paint(Pt, P, T, U, rgb, 0.1)
    g = lambda a, b, c, d: quad(Gl, a, b, c, d, np.tile([0.02, 0.025, 0.03, 0.0], (4, 1)), [[0, 0], [1, 0], [1, 1], [0, 1]])
    for z in (-1, 1):
        zz = z * (W / 2 - 0.11)
        g([cx0, y0, zz], [cx1, y0, zz], [cx1 - 0.15, y1 - 0.02, zz], [cx0 + 0.08, y1 - 0.02, zz])
    g([cx1, y0, -W / 2 + 0.13], [cx1, y0, W / 2 - 0.13], [cx1 - 0.15, y1 - 0.02, W / 2 - 0.13], [cx1 - 0.15, y1 - 0.02, -W / 2 + 0.13])
    g([cx0, y0, W / 2 - 0.13], [cx0, y0, -W / 2 + 0.13], [cx0 + 0.08, y1 - 0.02, -W / 2 + 0.13], [cx0 + 0.08, y1 - 0.02, W / 2 - 0.13])
    Gl.two_sided()
    for x in (L / 2, -L / 2):     # bumpers and lights
        P, T, U = obox((x, 0.42, 0), (0.06, 0.1, W / 2 - 0.02)); _paint(Bk, P, T, U, (0.02, 0.02, 0.02), 0.3)
        for z in (-1, 1):
            P, T, U = obox((x + np.sign(x) * 0.005, 0.72, z * 0.62), (0.02, 0.06, 0.13))
            _paint(Bk, P, T, U, (0.8, 0.8, 0.75) if x > 0 else (0.35, 0.01, 0.01), 0.05)
    for x in (1.28, -1.3):
        for z in (-0.72, 0.72):
            _wheel(Bk, x, z, 0.31, 0.2)
    return {"paint": Pt, "wood": Bk, "glass": Gl}


def utility_4x4(rng, rgb):
    """A boxy farm 4x4 (a working pick-up-sized estate), 4.0 by 1.8 by 2.0 m: flat panels, a flat
    bonnet, upright screen, a spare wheel on the back door, high ground clearance, mud on it."""
    Pt, Bk, Gl = Mesh(), Mesh(), Mesh()
    L, W = 4.0, 1.8
    P, T, U = obox((0, 0.85, 0), (L / 2, 0.38, W / 2)); _paint(Pt, P, T, U, rgb, 0.45)
    P, T, U = obox((-0.45, 1.65, 0), (1.5, 0.42, W / 2 - 0.02)); _paint(Pt, P, T, U, rgb, 0.3)
    P, T, U = obox((-0.45, 2.08, 0), (1.52, 0.02, W / 2)); _paint(Pt, P, T, U, (0.75, 0.75, 0.72), 0.3)   # white roof
    g = lambda a, b, c, d: quad(Gl, a, b, c, d, np.tile([0.02, 0.025, 0.03, 0.0], (4, 1)), [[0, 0], [1, 0], [1, 1], [0, 1]])
    for z in (-1, 1):
        zz = z * (W / 2 - 0.015)
        for x0, x1 in ((0.25, 0.95), (-0.6, 0.15), (-1.85, -0.7)):
            g([x0, 1.4, zz], [x1, 1.4, zz], [x1, 1.92, zz], [x0, 1.92, zz])
    g([1.06, 1.38, -0.8], [1.06, 1.38, 0.8], [1.06, 1.95, 0.8], [1.06, 1.95, -0.8])
    Gl.two_sided()
    P, T, U = obox((L / 2 + 0.03, 0.62, 0), (0.06, 0.1, W / 2)); _paint(Bk, P, T, U, (0.03, 0.03, 0.03), 0.6)
    P, T, U = obox((L / 2, 0.98, 0), (0.02, 0.18, 0.6)); _paint(Bk, P, T, U, (0.04, 0.04, 0.04), 0.4)   # grille
    for z in (-0.62, 0.62):
        P, T, U = obox((L / 2 + 0.01, 0.98, z), (0.02, 0.08, 0.08)); _paint(Bk, P, T, U, (0.8, 0.8, 0.75), 0.1)
    P, T, U = obox((-L / 2 - 0.03, 0.55, 0), (0.06, 0.08, W / 2)); _paint(Bk, P, T, U, (0.03, 0.03, 0.03), 0.6)
    P, T = tube([[-L / 2 - 0.02, 1.25, 0], [-L / 2 - 0.26, 1.25, 0]], 0.37, 14); _paint(Bk, P, T, P[:, [1, 2]], (0.012, 0.012, 0.012), 0.5)
    for x in (1.25, -1.25):
        for z in (-0.78, 0.78):
            _wheel(Bk, x, z, 0.38, 0.25, hub=(0.3, 0.3, 0.3))
    return {"paint": Pt, "wood": Bk, "glass": Gl}


def quad_bike(rng):
    """A farm quad: red body and mudguards, black seat, racks front and back, four fat tyres."""
    Pt, Bk = Mesh(), Mesh()
    red = (0.35, 0.02, 0.01)
    P, T, U = obox((0, 0.62, 0), (0.8, 0.12, 0.42)); _paint(Pt, P, T, U, red, 0.5)
    for x in (0.6, -0.6):
        P, T, U = obox((x, 0.78, 0), (0.3, 0.04, 0.52)); _paint(Pt, P, T, U, red, 0.6)
    P, T, U = obox((-0.15, 0.86, 0), (0.38, 0.07, 0.18)); _paint(Bk, P, T, U, (0.015, 0.015, 0.015), 0.4)
    P, T = tube([[0.45, 1.05, -0.38], [0.48, 1.08, 0.38]], 0.018, 6); _paint(Bk, P, T, P[:, [0, 1]], (0.02, 0.02, 0.02), 0.3)
    P, T = tube([[0.35, 0.7, 0], [0.45, 1.06, 0]], 0.03, 6); _paint(Bk, P, T, P[:, [0, 1]], (0.02, 0.02, 0.02), 0.3)
    for x in (0.65, -0.68):
        for z in (-0.3, -0.1, 0.1, 0.3):
            P, T = tube([[x - 0.25, 0.86, z], [x + 0.25, 0.86, z]], 0.012, 6); _paint(Bk, P, T, P[:, [0, 1]], (0.03, 0.03, 0.03), 0.4)
    for x in (0.6, -0.6):
        for z in (-0.48, 0.48):
            _wheel(Bk, x, z, 0.27, 0.24)
    return {"paint": Pt, "wood": Bk}


def round_bales(rng, n):
    """n round bales (1.2 m wide, 1.3 m across) in black or green wrap, in a row along +x, a few
    stood on end."""
    Pt = Mesh()
    wrap = (0.01, 0.012, 0.01) if rng.uniform() < 0.7 else (0.02, 0.06, 0.02)
    for i in range(n):
        x = i * 1.32
        if rng.uniform() < 0.25:
            P, T = tube([[x, 0.0, 0], [x, 1.2, 0]], 0.64, 16)
        else:
            P, T = tube([[x, 0.64, -0.6], [x, 0.64, 0.6]], 0.64, 16)
        _paint(Pt, P, T, P[:, [0, 1]] + P[:, [2, 2]], np.array(wrap) * rng.uniform(0.9, 1.15), rng.uniform(0.15, 0.45))
    return {"paint": Pt}


# --- Birds, gardens and the road's edge --------------------------------------------------------------

def corvid(rng, pose):
    """A jackdaw or crow at rest, about 0.35 m long, facing +x, its feet at y = 0: 'perch' (upright)
    or 'peck' (head down). All black, with the jackdaw's grey nape now and then."""
    Wd = Mesh()
    blk = np.array([0.012, 0.012, 0.014])
    up = pose == "perch"
    P = _unit(8, 12, 0.9, 0.9) * [0.13, 0.07, 0.065]
    R = rot_xyz(0, 0, 0.35 if up else -0.15)
    P = P @ R.T + [0, 0.12, 0]
    _paint(Wd, P, _topology(8, 12), P[:, [0, 1]], blk, 0.05)
    hc = np.array([0.12, 0.2, 0]) if up else np.array([0.15, 0.06, 0])
    P = _unit(6, 8, 1, 1) * 0.04 + hc
    _paint(Wd, P, _topology(6, 8), P[:, [0, 1]], blk * (1.0 if rng.uniform() < 0.5 else 4.0), 0.05)
    P, T = tube([hc + [0.03, 0, 0], hc + [0.08, -0.01 if up else -0.03, 0]], [0.012, 0.002], 5)
    _paint(Wd, P, T, P[:, [0, 1]], blk, 0.0)
    P, T, U = obox((-0.16, 0.12 if up else 0.15, 0), (0.07, 0.008, 0.03), rot_xyz(0, 0, 0.5 if up else 0.1))
    _paint(Wd, P, T, U, blk, 0.0)
    for z in (-0.02, 0.02):
        P, T = tube([[0.01, 0.07, z], [0.02, 0.0, z]], 0.005, 4); _paint(Wd, P, T, P[:, [0, 1]], blk, 0.0)
    return {"wood": Wd}


def washing_line(rng, length=4.5):
    """Two posts and a line with washing pegged out along it (sheets, shirts and towels as thin
    panels hanging a little askew), the line along +x."""
    Wd = Mesh()
    for x in (0, length):
        P, T, U = obox((x, 0.9, 0), (0.04, 1.0, 0.04)); _paint(Wd, P, T, U, (0.13, 0.1, 0.07), 0.4)
    P, T = tube([[0, 1.82, 0], [length / 2, 1.72, 0], [length, 1.82, 0]], 0.004, 4)
    _paint(Wd, P, T, P[:, [0, 1]], (0.5, 0.5, 0.5), 0.1)
    cols = [(0.75, 0.75, 0.72), (0.08, 0.15, 0.4), (0.5, 0.08, 0.06), (0.62, 0.55, 0.25), (0.3, 0.45, 0.6), (0.7, 0.7, 0.68)]
    x = 0.3
    while x < length - 0.5:
        wdt = rng.uniform(0.35, 1.1); hgt = rng.uniform(0.35, 0.9)
        if x + wdt > length - 0.2:
            break
        ytop = 1.82 - 0.1 * (1 - ((x + wdt / 2) - length / 2) ** 2 / (length / 2) ** 2)
        P, T, U = obox((x + wdt / 2, ytop - hgt / 2, 0), (wdt / 2, hgt / 2, 0.006), rot_xyz(rng.normal(0, 0.08), 0, rng.normal(0, 0.03)))
        _paint(Wd, P, T, U, cols[int(rng.integers(len(cols)))], 0.05)
        x += wdt + rng.uniform(0.08, 0.3)
    return {"wood": Wd}


def log_pile(rng, length=2.0, height=1.0):
    """Split logs stacked against a wall: rows of log ends facing +z, about 0.4 m deep, along +x."""
    Wd = Mesh()
    y = 0.0
    while y < height:
        x = 0.06
        while x < length - 0.06:
            r = rng.uniform(0.05, 0.09)
            P, T = tube([[x + r, y + r, -0.2], [x + r, y + r, 0.2 + rng.uniform(-0.03, 0.03)]], r, 6)
            c = np.zeros((len(P), 4)); c[:, :3] = np.array([0.2, 0.13, 0.07]) * rng.uniform(0.7, 1.2); c[:, 3] = 0.3
            c[P[:, 2] > 0.15, :3] = np.array([0.35, 0.27, 0.16]) * rng.uniform(0.8, 1.1)   # the pale cut ends
            Wd.add(P, T, c, P[:, [0, 1]])
            x += 2 * r + 0.01
        y += 0.13
    return {"wood": Wd}


def picnic_table(rng):
    """A pub's wooden picnic table with its benches, 1.8 m long."""
    Wd = Mesh()
    wood = np.array([0.2, 0.13, 0.07])
    P, T, U = obox((0, 0.74, 0), (0.9, 0.025, 0.36)); _paint(Wd, P, T, U, wood, 0.3)
    for z in (-0.62, 0.62):
        P, T, U = obox((0, 0.45, z), (0.9, 0.022, 0.13)); _paint(Wd, P, T, U, wood, 0.35)
    for x in (-0.7, 0.7):
        for sz in (-1, 1):
            P, T = tube([[x, 0.0, sz * 0.7], [x, 0.74, sz * 0.1]], 0.035, 5); _paint(Wd, P, T, P[:, [0, 1]], wood * 0.8, 0.4)
        P, T, U = obox((x, 0.42, 0), (0.035, 0.03, 0.78)); _paint(Wd, P, T, U, wood * 0.8, 0.4)
    return {"wood": Wd}


def gravel_wash(rng):
    """Grit and small stones washed onto the road's edge after rain: a low fan about 1.6 m along
    and 0.6 m out, thickest at the verge (-z) and thinning onto the tarmac."""
    St = Mesh()
    for i in range(int(rng.integers(110, 160))):
        u = rng.uniform(-0.8, 0.8); v = abs(rng.normal(0, 0.22)) * (1 - abs(u) / 1.0)
        sz = rng.uniform(0.012, 0.035) * (1.2 - v)
        P, T, uv = stone(rng, (sz * 1.3, sz * 0.6, sz), (u, sz * 0.3, -0.3 + v), rot_xyz(0, rng.uniform(0, 3), 0), res=(4, 6),
                         round_=(0.3, 0.4), chops=1)
        c = stone_col(rng, len(P), bright=rng.uniform(0.4, 0.6)); c[:, 3] = 0.0
        St.add(P, T, c, uv)
    return {"stone": St}


def droppings(rng):
    """Sheep droppings on the fell road: a few dark pellets in a loose scatter."""
    Wd = Mesh()
    for _ in range(int(rng.integers(8, 18))):
        P = _unit(4, 6, 1, 1) * rng.uniform(0.006, 0.01) + [rng.normal(0, 0.06), 0.006, rng.normal(0, 0.06)]
        _paint(Wd, P, _topology(4, 6), P[:, [0, 2]], (0.02, 0.016, 0.01), 0.2)
    return {"wood": Wd}
