"""Cut the prototype's 3 km of Buttertubs Pass out of Torque Shed's Dales region and write it for Unreal.

    python Tools/export_buttertubs.py --torque-shed ../torque-shed

Reads the Godot game's region data (data/regions/dales: road graph, 4 m and 32 m height tiles, land
cover) and writes Data/Buttertubs/:

    route.json            the ride: origin, spawn, road centre line (sim metres and Unreal cm), tile list
    terrain_X_Z.glb       2 m terrain tiles, 256 m square, the road's bed cut into them, cover as vertex colour
    backdrop.glb          the hills around, 32 m, out to about 6 km, sunk a little under the near tiles
    road.glb              the tarmac ribbon, 5 m wide with a crown, its own mesh so it gets its own material
    sim_heights.txt/.f32  the same 2 m height grid for the standalone sim test (SimTest)
    sim_road.u8, sim_line.txt

Frames. Everything is relative to an origin at the start of the ride. The sim and the glb files use Torque
Shed's region frame: metres, x east, y up, z south (right handed, a valid glTF frame). Unreal is x east,
y south, z up in centimetres, so a sim point (x, y, z) is Unreal (100 x, 100 z, 100 y).

Only numpy is needed. The output is deterministic, so running it again gives the same files.
"""
import argparse, heapq, json, math, os, struct, sys, zlib
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

START_NEAR = (-10400.0, 2229.0)   # Simonstone end of the pass (region frame, metres)
END_NEAR = (-8317.0, -4366.0)     # Thwaite end
CUT = (4000.0, 8250.0)            # metres along the pass: the summit plateau and the drop into Thwaite
MARGIN = 300.0                    # detailed land either side of the ride
STEP = 2.0                        # detailed grid spacing
TILE = 256.0                      # detailed tile size
BACKDROP = 6000.0                 # half size of the far hills
HORIZON = 25000.0                 # half size of the horizon ring beyond them (128 m)
ROAD_HALF = 2.5                   # half width of the tarmac
BED_HALF = 3.2                    # flat bed under the road (a little wider than it)
BLEND = 14.0                      # where the cut or fill has blended back into the hillside
CROWN = 0.05                      # road centre above its edges
SMOOTH = 12.0                     # sigma (m) of the smoothing of the road's height along its length

COVER_RGB = {  # Godot cover names to a base colour (sRGB 0..255; written linear, as glTF wants)
    "grass": (92, 112, 52), "forest": (46, 66, 32), "crop": (110, 120, 60), "shrub": (86, 90, 48),
    "barren": (120, 112, 92), "urban": (110, 105, 100), "sand": (170, 150, 110), "wetland": (88, 92, 58),
    "water": (50, 64, 76), "rock": (128, 126, 120),
}


# --- Region data ----------------------------------------------------------------------------------

class Region:
    def __init__(self, root):
        self.root = os.path.join(root, "data", "regions", "dales")
        sys.path.insert(0, os.path.join(root, "tools"))
        meta = json.load(open(os.path.join(self.root, "region.json")))
        self.meta = meta
        self.levels = {l["name"]: l for l in meta["levels"]}
        self.cover_names = meta["cover"]
        self._tiles = {}

    def _tile(self, level, tx, tz, cover=False):
        key = (level, tx, tz, cover)
        if key not in self._tiles:
            l = self.levels[level]
            p = os.path.join(self.root, l["dir"], "%d_%d.%s" % (tx, tz, "covz" if cover else "u16z"))
            if not os.path.exists(p):
                self._tiles[key] = None
            else:
                raw = open(p, "rb").read()
                n = l["n"]
                if cover:
                    self._tiles[key] = np.frombuffer(zlib.decompress(raw), np.uint8).reshape(n, n)
                else:
                    dz = np.frombuffer(zlib.decompress(raw), dtype="<u2").reshape(n, n).astype(np.int64)
                    dx = np.cumsum(dz, axis=0) & 0xFFFF
                    q = (np.cumsum(dx, axis=1) & 0xFFFF).astype(np.float64)
                    self._tiles[key] = l["min"] + q * l["scale"]
        return self._tiles[key]

    def mosaic(self, level, ix0, iz0, ix1, iz1, cover=False):
        """Samples [iz0..iz1] x [ix0..ix1] (inclusive) of a level, NaN (or 255) where there is no tile."""
        n = self.levels[level]["n"]
        out = np.full((iz1 - iz0 + 1, ix1 - ix0 + 1), 255 if cover else np.nan,
                      np.uint8 if cover else np.float64)
        for tz in range(iz0 // n, iz1 // n + 1):
            for tx in range(ix0 // n, ix1 // n + 1):
                t = self._tile(level, tx, tz, cover)
                if t is None:
                    continue
                ax0, az0 = max(ix0, tx * n), max(iz0, tz * n)
                ax1, az1 = min(ix1, tx * n + n - 1), min(iz1, tz * n + n - 1)
                out[az0 - iz0:az1 - iz0 + 1, ax0 - ix0:ax1 - ix0 + 1] = \
                    t[az0 - tz * n:az1 - tz * n + 1, ax0 - tx * n:ax1 - tx * n + 1]
        return out

    def heights(self, xs, zs):
        """Bilinear heights on the grid xs (columns) by zs (rows), fine tiles first, coarse to fill."""
        out = np.full((len(zs), len(xs)), np.nan)
        for name in ("fine", "coarse"):
            s = self.levels[name]["spacing"]
            fx, fz = xs / s, zs / s
            ix, iz = np.floor(fx).astype(int), np.floor(fz).astype(int)
            m = self.mosaic(name, ix.min(), iz.min(), ix.max() + 1, iz.max() + 1)
            cx, cz = ix - ix.min(), iz - iz.min()
            tx, tz = (fx - ix)[None, :], (fz - iz)[:, None]
            a = m[np.ix_(cz, cx)]; b = m[np.ix_(cz, cx + 1)]
            c = m[np.ix_(cz + 1, cx)]; d = m[np.ix_(cz + 1, cx + 1)]
            h = (a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz
            hole = np.isnan(out)
            out[hole] = h[hole]
        return out

    def cover(self, xs, zs):
        s = self.levels["fine"]["spacing"]
        ix, iz = np.round(xs / s).astype(int), np.round(zs / s).astype(int)
        m = self.mosaic("fine", ix.min(), iz.min(), ix.max(), iz.max(), cover=True)
        return m[np.ix_(iz - iz.min(), ix - ix.min())]


def pass_line(region):
    """The road centre line from Simonstone to Thwaite, by shortest path over the road graph."""
    r = json.load(open(os.path.join(region.root, "roads.json")))
    N, E = r["nodes"], r["edges"]

    def poly(e):
        pp = e["p"]
        return [tuple(N[e["a"]])] + [(pp[i], pp[i + 1]) for i in range(0, len(pp), 2)] + [tuple(N[e["b"]])]

    adj = {}
    for i, e in enumerate(E):
        p = poly(e)
        l = sum(math.dist(p[k], p[k + 1]) for k in range(len(p) - 1))
        adj.setdefault(e["a"], []).append((e["b"], l, i))
        adj.setdefault(e["b"], []).append((e["a"], l, i))

    def near(x, z):
        return min(range(len(N)), key=lambda i: (N[i][0] - x) ** 2 + (N[i][1] - z) ** 2)

    a, b = near(*START_NEAR), near(*END_NEAR)
    dist, prev, q = {a: 0.0}, {}, [(0.0, a)]
    while q:
        c, u = heapq.heappop(q)
        if u == b:
            break
        if c > dist[u]:
            continue
        for v, l, i in adj.get(u, []):
            if c + l < dist.get(v, 1e18):
                dist[v] = c + l; prev[v] = (u, i); heapq.heappush(q, (c + l, v))
    path, u = [], b
    while u != a:
        pu, i = prev[u]; path.append((pu, i)); u = pu
    pts = []
    for pu, i in reversed(path):
        p = poly(E[i])
        if E[i]["a"] != pu:
            p = p[::-1]
        pts += p if not pts else p[1:]
    return np.array(pts, np.float64)


def resample(pts, step):
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    s = np.concatenate([[0.0], np.cumsum(seg)])
    ss = np.arange(0.0, s[-1], step)
    return np.stack([np.interp(ss, s, pts[:, 0]), np.interp(ss, s, pts[:, 1])], 1), ss


def smooth_corners(xz, passes=3):
    """A few passes of a gentle Laplacian so the map's straight pieces and kinks become curves."""
    p = xz.copy()
    for _ in range(passes):
        p[1:-1] = p[1:-1] * 0.5 + (p[:-2] + p[2:]) * 0.25
    return p


# --- Meshes ---------------------------------------------------------------------------------------

ROCK_RGB = (118, 114, 104)


def land_colour(cov, names, slope):
    """Per-vertex colour: the cover's, turning to scree and rock on slopes steeper than about 35
    degrees, as linear values in 0..255 (glTF COLOR_0 is linear; Unreal's VertexColor gives it back)."""
    rgb = np.zeros(cov.shape + (3,), np.float64)
    rgb[...] = COVER_RGB["grass"]
    for ci, name in enumerate(names):
        rgb[cov == ci] = COVER_RGB.get(name, COVER_RGB["grass"])
    rock = np.clip((slope - 0.6) / 0.4, 0, 1)[..., None]   # tan 31 deg .. tan 45 deg
    rgb = rgb * (1 - rock) + np.array(ROCK_RGB) * rock
    return (np.power(rgb / 255.0, 2.2) * 255.0).round().astype(np.uint8)


def far_heights(region, xs, zs):
    """Heights on a grid that may reach past the region's data: beyond it, the edge's heights carried
    outwards, roughened with long, low hills and falling slowly away, so the far horizon is fells."""
    H = region.heights(xs, zs)
    b = region.meta.get("bounds")
    if b is None or not np.isnan(H).any():
        return np.where(np.isnan(H), np.nanmin(H), H)
    x0, z0, x1, z1 = b[0] + 64, b[1] + 64, b[2] - 64, b[3] - 64
    cx = np.clip(xs, x0, x1); cz = np.clip(zs, z0, z1)
    E = region.heights(cx, cz)
    E = np.where(np.isnan(E), np.nanmean(H), E)
    X, Z = np.meshgrid(xs, zs)
    d = np.maximum(np.maximum(x0 - X, X - x1), np.maximum(z0 - Z, Z - z1)).clip(0)
    rng = np.random.default_rng(11)
    hills = np.zeros_like(X)
    for wl, amp in ((9000, 70), (5200, 50), (3100, 35), (1700, 18), (900, 8)):
        for _ in range(2):
            a = rng.uniform(0, np.pi); ph = rng.uniform(0, 2 * np.pi)
            hills += amp * 0.5 * np.sin((X * np.cos(a) + Z * np.sin(a)) * 2 * np.pi / wl + ph)
    ext = E + hills * np.clip(d / 2500.0, 0, 1) - 0.012 * d
    return np.where(np.isnan(H), ext, H)


def write_glb(path, pos, idx, nrm=None, col=None, name="mesh", uv=None, uv1=None):
    """A minimal glTF 2.0 binary: one mesh, one primitive, float positions/normals, uint8 RGBA colour."""
    pos = np.ascontiguousarray(pos, np.float32)
    small = len(pos) < 65536
    idx = np.ascontiguousarray(idx, np.uint16 if small else np.uint32).ravel()
    blobs, views, accs, attrs = [], [], [], {}
    off = 0

    def add(arr, target, comp, typ, count, norm=False, minmax=False):
        nonlocal off
        b = arr.tobytes()
        pad = (-len(b)) % 4
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(b), "target": target})
        a = {"bufferView": len(views) - 1, "componentType": comp, "count": count, "type": typ}
        if norm:
            a["normalized"] = True
        if minmax:
            a["min"] = arr.min(0).tolist(); a["max"] = arr.max(0).tolist()
        accs.append(a)
        blobs.append(b + b"\0" * pad); off += len(b) + pad
        return len(accs) - 1

    attrs["POSITION"] = add(pos, 34962, 5126, "VEC3", len(pos), minmax=True)
    if nrm is not None:
        attrs["NORMAL"] = add(np.ascontiguousarray(nrm, np.float32), 34962, 5126, "VEC3", len(pos))
    if uv is not None:
        attrs["TEXCOORD_0"] = add(np.ascontiguousarray(uv, np.float32), 34962, 5126, "VEC2", len(pos))
    if uv1 is not None:
        attrs["TEXCOORD_1"] = add(np.ascontiguousarray(uv1, np.float32), 34962, 5126, "VEC2", len(pos))
    if col is not None:
        attrs["COLOR_0"] = add(np.ascontiguousarray(col, np.uint8), 34962, 5121, "VEC4", len(pos), norm=True)
    ia = add(idx, 34963, 5123 if small else 5125, "SCALAR", len(idx))
    gl = {
        "asset": {"version": "2.0", "generator": "torque-shed-ue export_buttertubs.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": [{"attributes": attrs, "indices": ia, "mode": 4}]}],
        "buffers": [{"byteLength": off}], "bufferViews": views, "accessors": accs,
    }
    js = json.dumps(gl, separators=(",", ":")).encode()
    js += b" " * ((-len(js)) % 4)
    bin_ = b"".join(blobs)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack("<II", len(bin_), 0x004E4942)); f.write(bin_)


def grid_mesh(H, x0, z0, step, col=None):
    """Triangles of a height grid, each cell split from (0,0) to (1,1) as the sim's field has it.
    Winding is counter-clockwise seen from above (+y), glTF's front face."""
    nz, nx = H.shape
    xs = x0 + np.arange(nx) * step; zs = z0 + np.arange(nz) * step
    X, Z = np.meshgrid(xs, zs)
    pos = np.stack([X, H, Z], -1).reshape(-1, 3)
    gx = np.gradient(H, step, axis=1); gz = np.gradient(H, step, axis=0)
    n = np.stack([-gx, np.ones_like(H), -gz], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    i = np.arange(nz * nx).reshape(nz, nx)
    a, b, c, d = i[:-1, :-1], i[:-1, 1:], i[1:, :-1], i[1:, 1:]
    # Seen from +y with x right and z down the page, (a, d, b) and (a, c, d) are counter-clockwise.
    tris = np.concatenate([np.stack([a, d, b], -1).reshape(-1, 3), np.stack([a, c, d], -1).reshape(-1, 3)])
    uv = np.stack([(X - xs[0]) / (xs[-1] - xs[0]), (Z - zs[0]) / (zs[-1] - zs[0])], -1).reshape(-1, 2)
    return pos, tris, n.reshape(-1, 3), (None if col is None else col.reshape(-1, 4)), uv


# --- Main -----------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--torque-shed", default=os.path.join(os.path.dirname(REPO), "torque-shed"),
                    help="a checkout of the Godot game (for data/regions/dales)")
    ap.add_argument("--out", default=os.path.join(REPO, "Data", "Buttertubs"))
    ap.add_argument("--no-glb", action="store_true", help="only the sim test's files")
    args = ap.parse_args()
    region = Region(args.torque_shed)
    os.makedirs(args.out, exist_ok=True)

    # The ride: resample the pass every 2 m, round its corners a little, cut the 3 km.
    full = pass_line(region)
    xz, s = resample(full, STEP)
    xz = smooth_corners(xz)
    keep = (s >= CUT[0] - 60.0) & (s <= CUT[1] + 60.0)   # a little road beyond each end
    xz, s = xz[keep], s[keep] - CUT[0]
    print("pass %.0f m, ride %.0f m (%d points)" % (resample(full, 1.0)[1][-1], CUT[1] - CUT[0], len(s)))

    # The road's height: the land along its line, smoothed along its length (the 4 m map is lumpy,
    # a road is not), so cut and fill come out as a real road's would.
    ground = np.array([region.heights(np.array([p[0]]), np.array([p[1]]))[0, 0] for p in xz])
    k = np.arange(-int(3 * SMOOTH / STEP), int(3 * SMOOTH / STEP) + 1) * STEP
    w = np.exp(-0.5 * (k / SMOOTH) ** 2); w /= w.sum()
    pad = len(k) // 2
    road_h = np.convolve(np.pad(ground, pad, mode="edge"), w, mode="valid")

    # The origin: the start of the ride, whole metres.
    i0 = int(np.argmin(np.abs(s)))
    O = np.array([round(xz[i0, 0]), round(road_h[i0]), round(xz[i0, 1])], np.float64)
    line = np.stack([xz[:, 0] - O[0], road_h - O[1], xz[:, 1] - O[2]], 1)

    # The detailed grid, on whole multiples of the tile size so tiles line up.
    lo = np.floor((line[:, [0, 2]].min(0) - MARGIN) / TILE) * TILE
    hi = np.ceil((line[:, [0, 2]].max(0) + MARGIN) / TILE) * TILE
    xs = np.arange(lo[0], hi[0] + STEP / 2, STEP); zs = np.arange(lo[1], hi[1] + STEP / 2, STEP)
    H = region.heights(xs + O[0], zs + O[2]) - O[1]
    if np.isnan(H).any():
        raise SystemExit("height data missing inside the ride's area")
    cov = region.cover(xs + O[0], zs + O[2])

    # Cut the road's bed into the land: within BED_HALF the land is the road's height (a hair under,
    # so the ribbon sits on it), blending back to the hillside by BLEND.
    best = np.full(H.shape, 1e9); bed = np.zeros(H.shape)
    for k in range(len(line) - 1):
        a, b = line[k], line[k + 1]
        ab = b[[0, 2]] - a[[0, 2]]; L2 = ab @ ab
        cx0 = int((min(a[0], b[0]) - BLEND - lo[0]) // STEP); cx1 = int((max(a[0], b[0]) + BLEND - lo[0]) // STEP) + 2
        cz0 = int((min(a[2], b[2]) - BLEND - lo[1]) // STEP); cz1 = int((max(a[2], b[2]) + BLEND - lo[1]) // STEP) + 2
        cx0, cz0 = max(cx0, 0), max(cz0, 0)
        gx, gz = np.meshgrid(xs[cx0:cx1], zs[cz0:cz1])
        t = np.clip(((gx - a[0]) * ab[0] + (gz - a[2]) * ab[1]) / L2, 0, 1)
        d = np.hypot(gx - (a[0] + t * ab[0]), gz - (a[2] + t * ab[1]))
        hh = a[1] + t * (b[1] - a[1])
        sub = best[cz0:cz1, cx0:cx1]; m = d < sub
        sub[m] = d[m]; bed[cz0:cz1, cx0:cx1][m] = hh[m]
    # Distance to the road everywhere (coarsely), to keep only tiles near the ride.
    lx, lz = line[::10, 0], line[::10, 2]
    dist_road = np.full(H.shape, 1e9)
    for k in range(0, len(lx), 50):
        dx = xs[None, :, None] - lx[None, None, k:k + 50]
        dz = zs[:, None, None] - lz[None, None, k:k + 50]
        dist_road = np.minimum(dist_road, np.sqrt(dx * dx + dz * dz).min(-1))
    near = best < BLEND
    u = np.clip((best - BED_HALF) / (BLEND - BED_HALF), 0, 1)
    u = u * u * (3 - 2 * u)
    # 6 cm under the ribbon, so the land never shows through the tarmac between grid points.
    H = np.where(near, bed - 0.06 + (H - bed + 0.06) * u, H)
    on_road = best <= ROAD_HALF

    # Vertex colour: the cover's colour, alpha 255 on open land fading to 0 on the verge and road bed
    # (the material blends to verge grass and gravel by it).
    slope = np.hypot(*np.gradient(H, STEP))
    rgb = land_colour(cov, region.cover_names, slope)
    alpha = (np.clip((best - ROAD_HALF) / 4.0, 0, 1) * 255).astype(np.uint8)
    col = np.concatenate([rgb, alpha[..., None]], -1)

    # The sim test's copy.
    with open(os.path.join(args.out, "sim_heights.txt"), "w") as f:
        f.write("%d %d %.3f %.3f %.3f\n" % (len(xs), len(zs), xs[0], zs[0], STEP))
    H.astype("<f4").tofile(os.path.join(args.out, "sim_heights.f32"))
    on_road.astype(np.uint8).tofile(os.path.join(args.out, "sim_road.u8"))
    ride = (s >= 0) & (s <= CUT[1] - CUT[0])
    with open(os.path.join(args.out, "sim_line.txt"), "w") as f:
        for p in line[ride]:
            f.write("%.3f %.3f %.3f\n" % tuple(p))

    tiles = []
    if not args.no_glb:
        per = int(TILE / STEP)
        for tz in range(0, len(zs) - 1, per):
            for tx in range(0, len(xs) - 1, per):
                sl = (slice(tz, tz + per + 1), slice(tx, tx + per + 1))
                if dist_road[sl].min() > MARGIN:
                    continue   # the backdrop covers it
                pos, tris, nrm, c, uv = grid_mesh(H[sl], xs[tx], zs[tz], STEP, col[sl])
                name = "terrain_%d_%d" % (int(xs[tx] // TILE), int(zs[tz] // TILE))
                write_glb(os.path.join(args.out, name + ".glb"), pos, tris, nrm, c, name, uv)
                tiles.append({"name": name, "min": [xs[tx], float(pos[:, 1].min()), zs[tz]],
                              "max": [xs[min(tx + per, len(xs) - 1)], float(pos[:, 1].max()),
                                      zs[min(tz + per, len(zs) - 1)]]})

        # The far hills, 32 m, centred on the ride, sunk under the near tiles so they never show through.
        c = (lo + hi) / 2
        bs = 32.0
        bx = np.arange(c[0] - BACKDROP, c[0] + BACKDROP + 1, bs); bz = np.arange(c[1] - BACKDROP, c[1] + BACKDROP + 1, bs)
        B = far_heights(region, bx + O[0], bz + O[2]) - O[1]
        BX, BZ = np.meshgrid(bx, bz)
        # Which backdrop cells the near tiles cover: a vertex all of whose cells are covered is
        # inside the near land (tiles side by side count together, not one at a time).
        cov_cell = np.zeros((len(bz) + 1, len(bx) + 1), bool)   # cell (j, i) spans vertices j-1..j, i-1..i
        cx = np.concatenate([[bx[0] - bs / 2], (bx[:-1] + bx[1:]) / 2, [bx[-1] + bs / 2]])
        cz = np.concatenate([[bz[0] - bs / 2], (bz[:-1] + bz[1:]) / 2, [bz[-1] + bs / 2]])
        CX, CZ = np.meshgrid(cx, cz)
        def in_tiles(X, Z):
            m = np.zeros(X.shape, bool)
            for t in tiles:
                (x0, _, z0), (x1, _, z1) = t["min"], t["max"]
                m |= (X >= x0 - 0.01) & (X <= x1 + 0.01) & (Z >= z0 - 0.01) & (Z <= z1 + 0.01)
            return m
        cov_cell[:] = True
        any_cell = np.zeros(cov_cell.shape, bool)   # some of the cell is near land
        for ox in (-0.5, -0.25, 0.0, 0.25, 0.5):   # a cell is covered when all of it is
            for oz in (-0.5, -0.25, 0.0, 0.25, 0.5):
                m = in_tiles(CX + ox * bs, CZ + oz * bs)
                cov_cell &= m; any_cell |= m
        c4 = [cov_cell[:-1, :-1], cov_cell[:-1, 1:], cov_cell[1:, :-1], cov_cell[1:, 1:]]
        inside = c4[0] & c4[1] & c4[2] & c4[3]
        a4 = [any_cell[:-1, :-1], any_cell[:-1, 1:], any_cell[1:, :-1], any_cell[1:, 1:]]
        edge = (a4[0] | a4[1] | a4[2] | a4[3]) & ~inside
        # The lowest near land in each covered cell: a backdrop vertex at or under the lowest of its
        # cells can't come up through the near land anywhere in them (the deep road cuts included).
        cell_min = np.full(cov_cell.shape, np.inf)
        for j, i in zip(*np.nonzero(any_cell)):
            ix0 = max(int(np.floor((cx[i] - bs / 2 - xs[0]) / STEP)), 0); ix1 = int(np.ceil((cx[i] + bs / 2 - xs[0]) / STEP)) + 1
            iz0 = max(int(np.floor((cz[j] - bs / 2 - zs[0]) / STEP)), 0); iz1 = int(np.ceil((cz[j] + bs / 2 - zs[0]) / STEP)) + 1
            sub = H[iz0:iz1, ix0:ix1]
            if sub.size:
                cell_min[j, i] = sub.min()
        m4 = np.minimum(np.minimum(cell_min[:-1, :-1], cell_min[:-1, 1:]), np.minimum(cell_min[1:, :-1], cell_min[1:, 1:]))
        # Under the near land inside, and a little at its edges so the seam hides in the grass.
        B = np.where(inside, np.minimum(B - 3.0, m4 - 1.0), np.where(edge, np.minimum(B - 0.6, m4 - 0.3), B))
        bcol = np.zeros(B.shape + (4,), np.uint8); bcol[..., 3] = 255
        bcov = region.cover(bx + O[0], bz + O[2])
        bcol[..., :3] = land_colour(bcov, region.cover_names, np.hypot(*np.gradient(B, bs)))
        pos, tris, nrm, cc, uv = grid_mesh(B, bx[0], bz[0], bs, bcol)
        write_glb(os.path.join(args.out, "backdrop.glb"), pos, tris, nrm, cc, "backdrop", uv)

        # The horizon: 128 m out to 25 km, so the land doesn't stop at the region's edge (it read as
        # sea). Sunk well under the far hills where they cover it, and a little at their edge.
        hs = 128.0
        hx = np.arange(c[0] - HORIZON, c[0] + HORIZON + 1, hs); hz = np.arange(c[1] - HORIZON, c[1] + HORIZON + 1, hs)
        R = far_heights(region, hx + O[0], hz + O[2]) - O[1]
        HX, HZ = np.meshgrid(hx, hz)
        inner = np.maximum(np.abs(HX - c[0]), np.abs(HZ - c[1]))
        R = np.where(inner < BACKDROP - hs, R - 150.0, np.where(inner < BACKDROP + hs, R - 6.0, R))
        hcol = np.zeros(R.shape + (4,), np.uint8); hcol[..., 3] = 255
        hcov = region.cover(hx + O[0], hz + O[2])
        hcol[..., :3] = land_colour(hcov, region.cover_names, np.hypot(*np.gradient(R, hs)))
        pos, tris, nrm, cc, uv = grid_mesh(R, hx[0], hz[0], hs, hcol)
        write_glb(os.path.join(args.out, "horizon.glb"), pos, tris, nrm, cc, "horizon", uv)

        # The tarmac: five vertices across (edge, mid, centre, mid, edge), crowned, every 2 m.
        d = np.gradient(line[:, [0, 2]], axis=0)
        d /= np.linalg.norm(d, axis=1, keepdims=True)
        right = np.stack([-d[:, 1], d[:, 0]], 1)   # x east, z south: forward (dx, dz) has right (-dz, dx)
        across = np.array([-1.0, -0.5, 0.0, 0.5, 1.0])
        lift = CROWN * (1 - across ** 2)
        P = np.zeros((len(line), 5, 3))
        for j, a in enumerate(across):
            P[:, j, 0] = line[:, 0] + right[:, 0] * a * ROAD_HALF
            P[:, j, 2] = line[:, 2] + right[:, 1] * a * ROAD_HALF
            P[:, j, 1] = line[:, 1] + lift[j]
        pos = P.reshape(-1, 3)
        i = np.arange(len(line) * 5).reshape(len(line), 5)
        a_, b_, c_, d_ = i[:-1, :-1], i[:-1, 1:], i[1:, :-1], i[1:, 1:]
        # Along the road is +row, across to the right is +column; seen from above that's clockwise
        # for (a, b, d), so the front faces are (a, d, b) and (a, c, d) reversed: (a, b, c), (b, d, c).
        t1 = np.stack([a_, c_, b_], -1).reshape(-1, 3); t2 = np.stack([b_, c_, d_], -1).reshape(-1, 3)
        tris = np.concatenate([t1, t2])
        e1 = pos[tris[:, 1]] - pos[tris[:, 0]]; e2 = pos[tris[:, 2]] - pos[tris[:, 0]]
        if np.cross(e1, e2)[:, 1].mean() < 0:
            tris = tris[:, [0, 2, 1]]
        nrm = np.zeros_like(pos)
        fn = np.cross(pos[tris[:, 1]] - pos[tris[:, 0]], pos[tris[:, 2]] - pos[tris[:, 0]])
        for kk in range(3):
            np.add.at(nrm, tris[:, kk], fn)
        nrm /= np.linalg.norm(nrm, axis=1, keepdims=True)
        rc = np.zeros((len(pos), 4), np.uint8)
        rc[:, 0] = np.repeat((np.abs(across) * 255).astype(np.uint8)[None, :], len(line), 0).ravel()  # R: 0 centre .. 255 edge
        rc[:, 1] = 0; rc[:, 2] = 0; rc[:, 3] = 255
        # UV: U across (0 left edge .. 1 right edge), V along in units of 20 m (full precision UVs in UE).
        ruv = np.stack([np.tile((across + 1) / 2, len(line)), np.repeat(s / 20.0, 5)], -1)
        write_glb(os.path.join(args.out, "road.glb"), pos, tris, nrm, rc, "road", ruv)

        # Walls, fences, barns, rocks, grass and sheep along the ride.
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import buttertubs_detail
        counts = buttertubs_detail.build(region.root, region.meta, O, H, xs, zs, best, line, tiles, ROAD_HALF,
                                         args.out, write_glb)
        grid_points = counts.pop("grid_points", [])
        print("detail", counts)

    # The ride's description for Unreal and the sim.
    def ue(p):  # sim metres (x east, y up, z south) to Unreal cm (x east, y south, z up)
        return [round(p[0] * 100, 1), round(p[2] * 100, 1), round(p[1] * 100, 1)]

    first = line[ride][0]; ahead = line[ride][5]
    heading = math.atan2(-(ahead[0] - first[0]), -(ahead[2] - first[2]))  # BikeSim.Place: 0 = -z, + turns left
    lat0, lon0 = region.meta["frame"]["lat"], region.meta["frame"]["lon"]
    lat = lat0 - O[2] / 111320.0
    lon = lon0 + O[0] / (111320.0 * math.cos(math.radians(lat0)))
    info = {
        "name": "Buttertubs Pass, summit to the drop towards Thwaite",
        "length_m": CUT[1] - CUT[0],
        "origin_region_m": O.tolist(), "origin_geo": [round(float(lat), 6), round(float(lon), 6)],
        "frames": "sim/glb: metres, x east, y up, z south. Unreal: cm, x east, y south, z up = (100x, 100z, 100y)",
        "spawn": {"sim": (first + np.array([0, 0.35, 0])).tolist(), "heading": heading,
                  "ue": ue(first + np.array([0, 0.35, 0]))},
        "road_half_width_m": ROAD_HALF,
        "line_sim": [[round(v, 2) for v in p] for p in line[ride][::5]],
        "tiles": tiles, "tile_size_m": TILE, "step_m": STEP,
        # Cattle grids on the ride: sim position (x, y, z) and the index of the nearest line point.
        "cattle_grids": [{"sim": g[:3], "line_index": g[3]} for g in grid_points] if not args.no_glb else [],
        "credits": region.meta.get("credits", []),
    }
    json.dump(info, open(os.path.join(args.out, "route.json"), "w"), indent=1)
    drop = line[ride][0, 1] - line[ride][-1, 1]
    print("origin", O.tolist(), "geo", info["origin_geo"], "grid %dx%d" % (len(xs), len(zs)),
          "tiles", len(tiles), "drop %.0f m" % drop)


if __name__ == "__main__":
    main()
