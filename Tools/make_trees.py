#!/usr/bin/env python3
"""The trees of the Buttertubs ride and the dales around it, every one where the map says a tree is.

    python Tools/make_trees.py --torque-shed ../torque-shed                (fetches the canopy map once, ~300 MB)
    python Tools/make_trees.py --torque-shed ../torque-shed --models-only  (just the tree meshes)

Where the trees are comes from a canopy height map: Meta and the World Resources Institute's 1 m map of
how tall the trees are (from satellite pictures and lidar; CC BY 4.0,
https://registry.opendata.aws/dataforgood-fb-forests/), the same map Torque Shed's own trees come from
(tools/tree_bake.py in the Godot game). Its tiles are cached in --chm-cache. It lines up with the map's
becks to a metre or two: the ash and alder along every gill, the sycamores around Thwaite's fields and
farms, the lone hawthorns on the fellside and the spruce plantations, all at their real heights.

    1. The map is sampled onto a 2 m grid over the far hills (12 km square, the backdrop's), keeping the
       tallest canopy in each square.
    2. Trees: the crowns' tops (local peaks, by size) first, then the rest of the canopy filled with
       trees spaced by their crowns' size, so a wood is as dense as a wood and a lone tree is one tree.
    3. Species, from what the map knows: the Godot region's leaf type (OpenStreetMap's, where mapped),
       how near a beck it stands (the region's streams), how big and even the stand is (plantations are
       big, blocky and all one height), its height and how high on the fell it grows.
    4. Each tree stands on the land exactly as drawn (the 2 m tiles near the ride, the 32 m backdrop
       further out), its foot sunk into the slope.

Trees more than FAR_SPLIT from the ride (so never seen closer than that) use the far version of their
model, a few hundred triangles; the rest are the full Nanite models. Nothing is culled with distance,
so nothing pops in: Nanite draws the far ones as a handful of clusters each.

Writes Data/Buttertubs/Trees/:

    SM_Tree_<species>_<n>.glb      the tree models (Tools/tree_models.py), metres, y up, base at the origin
    SM_TreeFar_<species>_<n>.glb   their far versions
    instances.bin                  every tree, in the format of Data/Buttertubs/Foliage/instances.bin,
                                   read by an ATorqueFoliage actor at BeginPlay (Tools/ue_build_level.py)
    trees.json                     the species list (instances.bin's species index), counts and credits

Only numpy is needed (and network access the first time, for the canopy tiles).
"""
import argparse, json, math, os, struct, sys, time, urllib.request, zlib
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from export_buttertubs import write_glb   # noqa: E402
import tree_models as TM                  # noqa: E402

DATA = os.path.join(REPO, "Data", "Buttertubs")
OUT = os.path.join(DATA, "Trees")

CHM_URL = "https://dataforgood-fb-data.s3.amazonaws.com/forests/v1/alsgedi_global_v6_float/chm/%s.tif"
CHM_ZOOM = 9
WORLD_M = 40075016.685578488
STEP = 2.0            # the canopy grid (m)
HALF = 6000.0         # half size of the area with trees (the backdrop's)
TREE_MIN = 3.0        # canopy this tall (m) or more is a tree's
FAR_SPLIT = 1500.0    # trees further than this from the ride use their far model
CREDIT = ("Trees: canopy heights from Meta and World Resources Institute, High Resolution Canopy Height Maps "
          "(2024), CC BY 4.0, https://registry.opendata.aws/dataforgood-fb-forests/")

# The models: species and how many variants of each.
VARIANTS = {"ash": 4, "sycamore": 4, "alder": 3, "birch": 2, "hawthorn": 3, "spruce": 2}


# --- Canopy map ------------------------------------------------------------------------------------

def quadkey(tx, ty, zoom=CHM_ZOOM):
    out = ""
    for i in range(zoom, 0, -1):
        m = 1 << (i - 1)
        out += str((1 if tx & m else 0) + (2 if ty & m else 0))
    return out


class Tiff:
    """A striped, deflated, byte-a-pixel BigTIFF (as the canopy tiles are), read a row at a time."""

    def __init__(self, path):
        self.f = open(path, "rb")
        h = self.f.read(16)
        assert h[:4] == b"II+\x00", "not a little-endian BigTIFF"
        self.f.seek(struct.unpack_from("<Q", h, 8)[0])
        cnt = struct.unpack("<Q", self.f.read(8))[0]
        ifd = self.f.read(cnt * 20)
        types = {3: "H", 4: "I", 16: "Q"}
        tags = {}
        for k in range(cnt):
            tag, typ = struct.unpack_from("<HH", ifd, k * 20)
            n = struct.unpack_from("<Q", ifd, k * 20 + 4)[0]
            if typ not in types:
                continue
            fmt = "<%d%s" % (n, types[typ])
            nb = struct.calcsize(fmt)
            if nb <= 8:
                vals = struct.unpack_from(fmt, ifd, k * 20 + 12)
            else:
                here = self.f.tell()
                self.f.seek(struct.unpack_from("<Q", ifd, k * 20 + 12)[0])
                vals = struct.unpack(fmt, self.f.read(nb))
                self.f.seek(here)
            tags[tag] = vals
        self.width = tags[256][0]
        self.rps = tags.get(278, (1,))[0]
        self.offsets, self.counts = tags[273], tags[279]
        self.pred = tags.get(317, (1,))[0]
        assert tags.get(259, (8,))[0] in (8, 32946) and tags.get(258, (8,))[0] == 8
        self._strip = (-1, None)

    def row(self, y):
        k = y // self.rps
        if self._strip[0] != k:
            self.f.seek(self.offsets[k])
            a = np.frombuffer(zlib.decompress(self.f.read(self.counts[k])), np.uint8).reshape(-1, self.width)
            if self.pred == 2:
                a = np.cumsum(a, axis=1, dtype=np.uint8)
            self._strip = (k, a)
        return self._strip[1][y - k * self.rps]


def chm_tile(cache, tx, ty):
    key = quadkey(tx, ty)
    path = os.path.join(cache, key + ".tif")
    if not os.path.exists(path):
        os.makedirs(cache, exist_ok=True)
        print("fetching canopy tile %s (about 150 MB)" % key, flush=True)
        req = urllib.request.Request(CHM_URL % key, headers={"User-Agent": "TorqueShed make_trees"})
        with urllib.request.urlopen(req, timeout=900) as r, open(path + ".part", "wb") as f:
            while True:
                b = r.read(1 << 20)
                if not b:
                    break
                f.write(b)
        os.replace(path + ".part", path)
    return Tiff(path)


def sample_canopy(cache, xs, zs, to_geo):
    """The canopy's height (m) on the ride-frame grid xs by zs: the tallest pixel in each square."""
    lat, _ = to_geo(np.zeros(len(zs)), zs)
    _, lon = to_geo(xs, np.zeros(len(xs)))
    size = WORLD_M / (1 << CHM_ZOOM)
    px = size / 65536
    mx = (lon + 180) / 360 * WORLD_M
    s = np.sin(np.radians(lat))
    my = (0.5 - np.log((1 + s) / (1 - s)) / (4 * np.pi)) * WORLD_M
    gx, gy = mx / px, my / px
    cell = STEP / (px * math.cos(math.radians(float(lat.mean()))))
    txs = sorted(set((np.floor(gx / 65536)).astype(int).tolist()))
    tys = sorted(set((np.floor(gy / 65536)).astype(int).tolist()))
    tiles = {(tx, ty): chm_tile(cache, tx, ty) for tx in txs for ty in tys}
    base = txs[0] * 65536
    a = np.floor(gx - cell / 2).astype(np.int64) - base
    out = np.zeros((len(zs), len(xs)), np.uint8)
    for j in range(len(zs)):
        y0, y1 = int(gy[j] - cell / 2), int(gy[j] + cell / 2) + 1
        R = None
        for y in range(y0, y1):
            ty = y // 65536
            row = np.concatenate([tiles[(tx, ty)].row(y - ty * 65536) for tx in txs])
            R = row if R is None else np.maximum(R, row)
        out[j] = np.maximum.reduceat(R[:a[-1] + int(math.ceil(cell))], a)
    # The edge squares: clear them, so the filters below (which wrap round) see no canopy there.
    out[:4] = 0; out[-4:] = 0; out[:, :4] = 0; out[:, -4:] = 0
    return out


# --- The land as drawn -----------------------------------------------------------------------------

def read_glb_positions(path):
    b = open(path, "rb").read()
    jl = struct.unpack("<I", b[12:16])[0]
    js = json.loads(b[20:20 + jl]); bin_ = b[20 + jl + 8:]
    a = js["accessors"][js["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
    v = js["bufferViews"][a["bufferView"]]
    return np.frombuffer(bin_[v["byteOffset"]:v["byteOffset"] + v["byteLength"]], np.float32)[:a["count"] * 3].reshape(-1, 3)


class Grid:
    """A height grid drawn as export_buttertubs.grid_mesh draws it (each cell split from its (0, 0)
    corner to its (1, 1) corner), sampled exactly."""

    def __init__(self, H, x0, z0, step):
        self.H, self.x0, self.z0, self.step = H, x0, z0, step

    @classmethod
    def from_glb(cls, path):
        P = read_glb_positions(path).astype(np.float64)
        xs = np.unique(np.round(P[:, 0], 3)); zs = np.unique(np.round(P[:, 2], 3))
        H = P[:, 1].reshape(len(zs), len(xs))
        return cls(H, xs[0], zs[0], xs[1] - xs[0])

    def inside(self, x, z):
        nz, nx = self.H.shape
        return ((x >= self.x0) & (x <= self.x0 + (nx - 1) * self.step) &
                (z >= self.z0) & (z <= self.z0 + (nz - 1) * self.step))

    def __call__(self, x, z):
        nz, nx = self.H.shape
        fx = np.clip((np.asarray(x) - self.x0) / self.step, 0, nx - 1.0001)
        fz = np.clip((np.asarray(z) - self.z0) / self.step, 0, nz - 1.0001)
        i = fx.astype(int); j = fz.astype(int); u = fx - i; v = fz - j
        a = self.H[j, i]; b = self.H[j, i + 1]; c = self.H[j + 1, i]; d = self.H[j + 1, i + 1]
        # Triangles (a, d, b) where u >= v and (a, c, d) where u < v.
        return np.where(u >= v, a + u * (b - a) + v * (d - b), a + v * (c - a) + u * (d - c))


class Land:
    """The ground as the player sees it: the 2 m tiles near the ride, the 32 m backdrop elsewhere."""

    def __init__(self, route):
        t = route["tiles"]
        step = route["step_m"]
        x0 = min(r["min"][0] for r in t); z0 = min(r["min"][2] for r in t)
        x1 = max(r["max"][0] for r in t); z1 = max(r["max"][2] for r in t)
        nx = int(round((x1 - x0) / step)) + 1; nz = int(round((z1 - z0) / step)) + 1
        H = np.full((nz, nx), np.nan)
        self.rects = []
        for r in t:
            g = Grid.from_glb(os.path.join(DATA, r["name"] + ".glb"))
            i0 = int(round((g.x0 - x0) / step)); j0 = int(round((g.z0 - z0) / step))
            H[j0:j0 + g.H.shape[0], i0:i0 + g.H.shape[1]] = g.H
            self.rects.append((r["min"][0], r["max"][0], r["min"][2], r["max"][2]))
        self.rects = np.array(self.rects)
        self.near = Grid(np.where(np.isnan(H), -1e4, H), x0, z0, step)
        self.back = Grid.from_glb(os.path.join(DATA, "backdrop.glb"))

    def in_tiles(self, x, z):
        x = np.asarray(x)[..., None]; z = np.asarray(z)[..., None]
        R = self.rects
        return ((x >= R[:, 0]) & (x <= R[:, 1]) & (z >= R[:, 2]) & (z <= R[:, 3])).any(-1)

    def __call__(self, x, z):
        x = np.asarray(x, np.float64); z = np.asarray(z, np.float64)
        out = self.back(x, z)
        m = self.in_tiles(x, z)
        if np.any(m):
            out = np.where(m, self.near(x, z), out)
        return out


# --- Finding the trees -----------------------------------------------------------------------------

def maxfilt(a, r):
    out = a.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if (dx or dy) and dx * dx + dy * dy <= r * r + r:
                out = np.maximum(out, np.roll(np.roll(a, dy, 0), dx, 1))
    return out


def blur(a):
    a = a.astype(np.float32)
    a = (np.roll(a, 1, 0) + 2 * a + np.roll(a, -1, 0)) / 4
    return (np.roll(a, 1, 1) + 2 * a + np.roll(a, -1, 1)) / 4


def crown_r(h):
    return np.clip(1.0 + 0.16 * h, 1.4, 6.5)


def find_trees(C, x0, z0, rng):
    """(x, z, height) of each tree: crown tops first (tallest first), then the canopy filled, each new
    tree kept only where it doesn't crowd one already there."""
    S = blur(C) + rng.uniform(0, 0.01, C.shape).astype(np.float32)
    peaks = []
    for lo, hi, r in ((TREE_MIN, 6, 1), (6, 10, 2), (10, 16, 2), (16, 99, 3)):
        m = maxfilt(S, r)
        j, i = np.nonzero((S >= m - 1e-6) & (S >= lo) & (S < hi))
        peaks.append(np.stack([j, i, S[j, i]], 1))
    P = np.concatenate(peaks)
    P = P[np.argsort(-P[:, 2])]
    Cf = C.astype(np.float32)
    HL = maxfilt(Cf, 2)
    j, i = np.nonzero((Cf >= TREE_MIN) & (Cf >= 0.6 * HL))   # not the sloping edge of a bigger crown
    hl = HL[j, i]
    order = np.argsort(-(hl + rng.uniform(0, 2.5, len(hl))))
    cand = np.concatenate([P, np.stack([j[order], i[order], hl[order]], 1)])
    G = 8.0
    grid = {}
    out = []
    jit = rng.uniform(-1, 1, (len(cand), 2))
    # How close each tree may stand to its neighbours varies, so stands have tight groups and gaps,
    # not a lattice; strung-out rows along becks and walls break up the same way.
    gap = rng.uniform(0.55, 1.0, len(cand)) ** 0.8
    for k, (jj, ii, h) in enumerate(cand):
        x = x0 + ii * STEP + jit[k, 0]; z = z0 + jj * STEP + jit[k, 1]
        r = float(crown_r(h))
        gx, gz = int(x // G), int(z // G)
        ok = True
        for a in range(gx - 2, gx + 3):
            for b in range(gz - 2, gz + 3):
                for (ox, oz, orr) in grid.get((a, b), ()):
                    if (ox - x) ** 2 + (oz - z) ** 2 < ((r + orr) * gap[k]) ** 2:
                        ok = False
                        break
                if not ok:
                    break
            if not ok:
                break
        if ok:
            grid.setdefault((gx, gz), []).append((x, z, r))
            out.append((x, z, float(h)))
    return np.array(out)


def stands(C):
    """Label the canopy's connected stands (on the 2 m grid): (labels, sizes in m^2, how uneven the
    canopy is inside the stand, its mean height there, the share of the stand that is inside)."""
    mask = C >= TREE_MIN
    lab = np.zeros(C.shape, np.int32)
    nz, nx = C.shape
    # Union-find over runs, row by row (numpy only, no scipy).
    parent = [0]

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    prev = np.zeros(nx, np.int32)
    for j in range(nz):
        row = mask[j]
        cur = np.zeros(nx, np.int32)
        if row.any():
            starts = np.flatnonzero(row & ~np.concatenate([[False], row[:-1]]))
            ends = np.flatnonzero(row & ~np.concatenate([row[1:], [False]]))
            for s, e in zip(starts, ends):
                above = prev[max(s - 1, 0):min(e + 2, nx)]
                ids = set(find(int(a)) for a in above[above > 0])
                if ids:
                    root = min(ids)
                    for o in ids:
                        parent[o] = root
                else:
                    parent.append(len(parent)); root = len(parent) - 1
                cur[s:e + 1] = root
        lab[j] = cur
        prev = cur
    roots = np.array([find(a) for a in range(len(parent))], np.int32)
    lab = roots[lab]
    n = len(parent)
    size = np.bincount(lab.ravel(), minlength=n).astype(np.float64) * STEP * STEP
    size[0] = 0
    # The stand's inside (more than 8 m from its edge): how much of it there is, and how even its
    # canopy is there (a plantation is all one height; a wild wood is not).
    inner = mask.copy()
    for dy in range(-4, 5):
        for dx in range(-4, 5):
            if dx * dx + dy * dy <= 16:
                inner &= np.roll(np.roll(mask, dy, 0), dx, 1)
    L = lab.ravel(); w = inner.ravel().astype(np.float64); h = C.ravel().astype(np.float64)
    ic = np.bincount(L, w, minlength=n)
    mu = np.bincount(L, h * w, minlength=n) / np.maximum(ic, 1)
    sd = np.sqrt(np.maximum(np.bincount(L, h * h * w, minlength=n) / np.maximum(ic, 1) - mu ** 2, 0))
    frac = ic / np.maximum(size / (STEP * STEP), 1)
    return lab, size, sd / np.maximum(mu, 1e-6), mu, frac


def line_dist(px, pz, lines, cell=50.0):
    """Distance from each point to the nearest of the polylines (m), up to about 200 m."""
    seg = []
    for L in lines:
        if len(L) >= 2:
            seg.append(np.concatenate([L[:-1], L[1:]], 1))
    if not seg:
        return np.full(len(px), 1e9)
    S = np.concatenate(seg)
    out = np.full(len(px), 1e9)
    # Bucket segments by cell for speed.
    buckets = {}
    for k, (ax, az, bx, bz) in enumerate(S):
        for cx in range(int(min(ax, bx) // cell) - 1, int(max(ax, bx) // cell) + 2):
            for cz in range(int(min(az, bz) // cell) - 1, int(max(az, bz) // cell) + 2):
                buckets.setdefault((cx, cz), []).append(k)
    keys = list(zip((px // cell).astype(int), (pz // cell).astype(int)))
    groups = {}
    for i, k in enumerate(keys):
        groups.setdefault(k, []).append(i)
    for k, idx in groups.items():
        ss = buckets.get(k)
        if not ss:
            continue
        idx = np.array(idx); Q = S[ss]
        ax, az, bx, bz = Q[:, 0], Q[:, 1], Q[:, 2], Q[:, 3]
        dx, dz = bx - ax, bz - az
        L2 = np.maximum(dx * dx + dz * dz, 1e-9)
        X = px[idx][:, None]; Z = pz[idx][:, None]
        t = np.clip(((X - ax) * dx + (Z - az) * dz) / L2, 0, 1)
        d = np.hypot(X - (ax + t * dx), Z - (az + t * dz)).min(1)
        out[idx] = np.minimum(out[idx], d)
    return out


def species_for(T, C, lab, size, spread, mean_h, frac, x0, z0, leaf_at, beck_d, elev, rng):
    """A species for each tree, from what the map says about where it stands."""
    j = np.clip(np.round((T[:, 1] - z0) / STEP).astype(int), 0, C.shape[0] - 1)
    i = np.clip(np.round((T[:, 0] - x0) / STEP).astype(int), 0, C.shape[1] - 1)
    st = lab[j, i]
    area = size[st]; even = spread[st]; sh = mean_h[st]
    h = T[:, 2]
    leaf = leaf_at(T[:, 0], T[:, 1])
    u = rng.uniform(0, 1, len(T))
    out = np.empty(len(T), object)
    # Plantations: needle-leaved where OpenStreetMap says so; otherwise big, solid stands (over a
    # hectare, mostly more than 8 m from their edge, not a strip along a beck) whose canopy is all one
    # height. How even it must be is a sliding scale, decided once per stand so a wood is all one kind.
    p_con = np.clip((0.30 - even) / 0.07, 0, 1) * (area > 10000) * (frac[st] > 0.5) * (sh > 7)
    stand_u = (np.sin(st * 12.9898) * 43758.5453) % 1.0
    planted = (leaf == 2) | ((leaf == 0) & (stand_u < p_con))
    mixed = (leaf == 3) & (u < 0.5)
    con = planted | mixed
    small = h < 6.5
    lone = area < 300
    by_beck = beck_d < 14
    high = elev > 430
    for k in range(len(T)):
        if con[k]:
            out[k] = "spruce"; continue
        v = u[k]
        if small[k] and (lone[k] or high[k]) and not by_beck[k]:
            out[k] = "hawthorn" if v < 0.9 else "birch"
        elif by_beck[k]:
            # Gill woodland: ash and alder by the water, birch higher up, the odd sycamore and hawthorn.
            if high[k]:
                out[k] = "birch" if v < 0.18 else "ash" if v < 0.55 else "alder" if v < 0.8 else "hawthorn"
            else:
                out[k] = "alder" if v < 0.35 else "ash" if v < 0.78 else "sycamore"
        else:
            # Field, farm and village trees: sycamore and ash.
            if small[k] and v < 0.3:
                out[k] = "hawthorn"
            else:
                out[k] = "sycamore" if v < 0.52 else "ash"
    return out


# --- Writing ---------------------------------------------------------------------------------------

def write_instances(path, groups, names):
    """instances.bin as Data/Buttertubs/Foliage's (see make_foliage.write_instances), one chunk per
    model per 512 m square."""
    chunks = []
    total = 0
    for si, name in enumerate(names):
        if name not in groups:
            continue
        pos, yaw, scale = groups[name]
        ue = np.stack([pos[:, 0] * 100, pos[:, 2] * 100, pos[:, 1] * 100], 1)
        tx = np.floor(pos[:, 0] / 512.0).astype(int); tz = np.floor(pos[:, 2] / 512.0).astype(int)
        for key in sorted(set(zip(tx.tolist(), tz.tolist()))):
            m = (tx == key[0]) & (tz == key[1])
            p = ue[m]
            o = np.floor(p.min(0)) - 1
            q = np.round(p - o).astype(np.int64)
            assert q.max() < 65536, "square too tall for 16 bits"
            yw = (np.round(yaw[m] / (2 * math.pi) * 256).astype(int) % 256).astype(np.uint8)
            sc = np.clip(np.round(scale[m] * 128), 1, 255).astype(np.uint8)
            rec = np.zeros(len(p), dtype=[("x", "<u2"), ("y", "<u2"), ("z", "<u2"), ("yaw", "u1"), ("s", "u1")])
            rec["x"], rec["y"], rec["z"], rec["yaw"], rec["s"] = q[:, 0], q[:, 1], q[:, 2], yw, sc
            chunks.append(struct.pack("<HHfffI", si, 0, *o.tolist(), len(p)) + rec.tobytes())
            total += len(p)
    raw = b"TSF1" + struct.pack("<I", len(chunks)) + b"".join(chunks)
    comp = zlib.compress(raw, 9)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"TSFZ", len(raw), len(comp)) + comp)
    return total, len(comp)


def build_models():
    os.makedirs(OUT, exist_ok=True)
    info = {}
    for sp, n in VARIANTS.items():
        for k in range(n):
            t0 = time.time()
            near, far, meta = TM.make(sp, 1000 + 37 * k + len(sp))
            for tag, m in (("SM_Tree", near), ("SM_TreeFar", far)):
                P, T, N, C, UV = m.arrays()
                name = "%s_%s_%d" % (tag, sp, k)
                write_glb(os.path.join(OUT, name + ".glb"), P, T, N, C, name, UV)
                info[name] = {"species": sp, "height": round(float(P[:, 1].max()), 2), "triangles": int(len(T)),
                              "radius": round(float(np.percentile(np.linalg.norm(P[P[:, 1] > 1][:, [0, 2]], axis=1), 98)), 2)}
            print("%-10s %d: %6d triangles near, %5d far, %.0f m, %.1f s" % (
                sp, k, info["SM_Tree_%s_%d" % (sp, k)]["triangles"], info["SM_TreeFar_%s_%d" % (sp, k)]["triangles"],
                info["SM_Tree_%s_%d" % (sp, k)]["height"], time.time() - t0), flush=True)
    return info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--torque-shed", default=os.path.join(os.path.dirname(REPO), "torque-shed"),
                    help="a checkout of the Godot game (for data/regions/dales)")
    ap.add_argument("--chm-cache", default=os.path.join(REPO, "build", "canopy"),
                    help="where the canopy tiles are kept (about 300 MB)")
    ap.add_argument("--models-only", action="store_true")
    ap.add_argument("--placement-only", action="store_true", help="keep the models already in Data/Buttertubs/Trees")
    args = ap.parse_args()
    t_start = time.time()

    if args.placement_only:
        info = json.load(open(os.path.join(OUT, "trees.json")))["models"]
    else:
        info = build_models()
    if args.models_only:
        json.dump({"models": info}, open(os.path.join(OUT, "trees.json"), "w"), indent=1)
        return

    route = json.load(open(os.path.join(DATA, "route.json")))
    O = route["origin_region_m"]
    root = os.path.join(args.torque_shed, "data", "regions", "dales")
    meta = json.load(open(os.path.join(root, "region.json")))
    fl, fo = meta["frame"]["lat"], meta["frame"]["lon"]
    p = math.radians(fl)
    m_lat = 111132.92 - 559.82 * math.cos(2 * p) + 1.175 * math.cos(4 * p)
    m_lon = 111412.84 * math.cos(p) - 93.5 * math.cos(3 * p) + 0.118 * math.cos(5 * p)

    def to_geo(x, z):
        return fl - (np.asarray(z) + O[2]) / m_lat, fo + (np.asarray(x) + O[0]) / m_lon

    def to_ride(lat, lon):   # 1e-5 degrees
        return (lon * 1e-5 - fo) * m_lon - O[0], -(lat * 1e-5 - fl) * m_lat - O[2]

    land = Land(route)
    t = route["tiles"]
    c = np.array([(min(r["min"][0] for r in t) + max(r["max"][0] for r in t)) / 2,
                  (min(r["min"][2] for r in t) + max(r["max"][2] for r in t)) / 2])
    # Keep inside the backdrop (its own centre and size), less a cell.
    bx0, bz0 = land.back.x0, land.back.z0
    bx1 = bx0 + (land.back.H.shape[1] - 1) * land.back.step; bz1 = bz0 + (land.back.H.shape[0] - 1) * land.back.step
    xs = np.arange(max(c[0] - HALF, bx0 + 40), min(c[0] + HALF, bx1 - 40), STEP)
    zs = np.arange(max(c[1] - HALF, bz0 + 40), min(c[1] + HALF, bz1 - 40), STEP)
    print("canopy grid %d x %d at %.0f m" % (len(xs), len(zs), STEP), flush=True)
    C = sample_canopy(args.chm_cache, xs, zs, to_geo)
    print("canopy: %.2f km2 of trees, %.0f s" % ((C >= TREE_MIN).sum() * STEP * STEP / 1e6, time.time() - t_start), flush=True)
    rng = np.random.default_rng(5)
    T = find_trees(C, xs[0], zs[0], rng)
    print("trees: %d, %.0f s" % (len(T), time.time() - t_start), flush=True)
    lab, size, spread, mean_h, frac = stands(C)

    # Leaf type (OpenStreetMap's, by way of the Godot region's trees.json), where mapped.
    import base64
    cells = []
    for row in json.load(open(os.path.join(root, "trees.json")))["cells"]:
        if not row.get("leaf"):
            continue
        r_, c_ = (int(v) for v in row["cell"].split("_"))
        R, Cc = row["rows"], row["cols"]
        lf = np.frombuffer(zlib.decompress(base64.b64decode(row["leaf"])), np.uint8).reshape(R, Cc)
        cells.append((r_ * 0.25 - 90.0, c_ * 0.25 - 180.0, R, Cc, lf))

    def leaf_at(x, z):
        lat, lon = to_geo(x, z)
        out = np.zeros(len(x), np.uint8)
        for s, w, R, Cc, lf in cells:
            m = (lat >= s) & (lat < s + 0.25) & (lon >= w) & (lon < w + 0.25)
            if m.any():
                rr = np.clip(((s + 0.25 - lat[m]) / 0.25 * R).astype(int), 0, R - 1)
                cc = np.clip(((lon[m] - w) / 0.25 * Cc).astype(int), 0, Cc - 1)
                out[m] = lf[rr, cc]
        return out

    # Becks.
    becks = []
    for row in json.load(open(os.path.join(root, "streams.json")))["streams"]:
        lat, lon = row[1], row[2]
        pts = [to_ride(lat, lon)]
        for k in range(3, len(row) - 1, 2):
            lat += row[k]; lon += row[k + 1]
            pts.append(to_ride(lat, lon))
        becks.append(np.array(pts))
    beck_d = line_dist(T[:, 0], T[:, 1], becks)

    ground = land(T[:, 0], T[:, 1])
    elev = ground + O[1]
    sp = species_for(T, C, lab, size, spread, mean_h, frac, xs[0], zs[0], leaf_at, beck_d, elev, rng)

    # Keep trunks off the tarmac and out of buildings.
    line = np.array(route.get("road_line_sim") or route["line_sim"])   # the whole ribbon, run-outs and all
    half = route["road_half_width_m"]
    road_d = line_dist(T[:, 0], T[:, 1], [line[:, [0, 2]]])
    keep = road_d > half + 1.2
    farm = []
    for row in json.load(open(os.path.join(root, "buildings.json"))):
        bx, bz = row[0] - O[0], row[1] - O[2]
        # As big as the walls work builds them (buttertubs_detail.place_buildings), and a metre round.
        w, d, rot = max(row[2], 5.0), max(row[3], 4.5), row[4]
        if abs(bx - c[0]) > HALF + 50 or abs(bz - c[1]) > HALF + 50:
            continue
        farm.append((bx, bz))
        dx, dz = T[:, 0] - bx, T[:, 1] - bz
        cs, sn = math.cos(rot), math.sin(rot)
        u = dx * cs + dz * sn; v = -dx * sn + dz * cs
        keep &= ~((np.abs(u) < w / 2 + 1.0) & (np.abs(v) < d / 2 + 1.0))
    T, sp, ground, beck_d = T[keep], sp[keep], ground[keep], beck_d[keep]
    # Farms and houses have their sycamores (planted as shelter round Dales farmsteads).
    if farm:
        F = np.array(farm)
        near_farm = np.zeros(len(T), bool)
        for k in range(0, len(F), 200):
            dd = np.hypot(T[:, None, 0] - F[None, k:k + 200, 0], T[:, None, 1] - F[None, k:k + 200, 1]).min(1)
            near_farm |= dd < 45
        broad = (sp != "spruce") & near_farm & (beck_d > 10) & (rng.uniform(0, 1, len(T)) < 0.7)
        sp[broad] = "sycamore"
    far = line_dist(T[:, 0], T[:, 1], [line[:, [0, 2]]], cell=400.0) > FAR_SPLIT

    # Place them: a variant each, scaled to the map's height, the foot sunk below the lowest ground
    # under the trunk so no root shows on a slope.
    groups = {}
    yaw_all = rng.uniform(0, 2 * math.pi, len(T))
    var_u = rng.uniform(0, 1, len(T))
    hjit = rng.uniform(0.85, 1.1, len(T))   # the map's 2 m squares round the heights; un-round them
    counts = {}
    for k in range(len(T)):
        s = sp[k]
        n = VARIANTS[s]
        vi = min(int(var_u[k] * n), n - 1)
        name = "%s_%s_%d" % ("SM_TreeFar" if far[k] else "SM_Tree", s, vi)
        mh = info[name.replace("SM_TreeFar", "SM_Tree")]["height"]
        sc = min(max(T[k, 2] * hjit[k] / mh, 0.25), 1.98)
        counts[s] = counts.get(s, 0) + 1
        yaw = yaw_all[k]
        if s == "hawthorn":   # leaning away from the prevailing south westerly
            yaw = -math.pi / 4 + rng.normal(0, 0.35)
        groups.setdefault(name, []).append((k, yaw, sc))
    out = {}
    for name, rows in groups.items():
        idx = np.array([r[0] for r in rows]); yaw = np.array([r[1] for r in rows]); sc = np.array([r[2] for r in rows])
        x, z = T[idx, 0], T[idx, 1]
        tr = 0.45 * sc + 0.2
        lo = np.min([land(x + dx * tr, z + dz * tr) for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1), (0, 0))], axis=0)
        y = lo - 0.12 * sc
        out[name] = (np.stack([x, y, z], 1), yaw, sc)
    names = sorted(info.keys())
    total, nbytes = write_instances(os.path.join(OUT, "instances.bin"), out, names)
    spec = {
        "about": "Trees made by Tools/make_trees.py; see its docstring. instances.bin's species index is into 'species'.",
        "species": [{"name": n, "mesh": n, "count": int(len(out[n][0])) if n in out else 0,
                     "far": n.startswith("SM_TreeFar"), "cull_m": 30000, "shadow": True} for n in names],
        "models": info,
        "counts": {**counts, "total": int(total), "far": int(far.sum()), "near": int((~far).sum())},
        "far_split_m": FAR_SPLIT,
        "credits": [CREDIT],
    }
    json.dump(spec, open(os.path.join(OUT, "trees.json"), "w"), indent=1)
    print("placed %d trees (%d near, %d far): %s; instances.bin %.1f MB; %.0f s" % (
        total, (~far).sum(), far.sum(), counts, nbytes / 1e6, time.time() - t_start))


if __name__ == "__main__":
    main()
