"""The countryside's detail for the Buttertubs ride, written by export_buttertubs.py beside the land:
drystone walls and fences where the map has them, limestone scars along its cliffs, field barns,
the cattle grids, snow poles beside the road, boulders and scree, tufts of rush and moor grass by
the road, and sheep. All of it baked into a few vertex-coloured meshes (detail_NN.glb, and grass_NN.glb
for the grass, which is drawn only near the camera; under 65k
vertices each) that the level script places with no collision: the wheels only see the land and
the road.
"""
import json, math, os
import numpy as np


def srgb(c):
    """sRGB 0..255 to linear 0..255 (glTF COLOR_0 is linear)."""
    return np.power(np.asarray(c, np.float64) / 255.0, 2.2) * 255.0


class Batch:
    """Triangles with colours, split into meshes small enough for 16-bit indices."""
    LIMIT = 60000

    def __init__(self):
        self.chunks = [[[], [], [], 0]]   # positions, triangles, colours, vertex count

    def add(self, pos, tris, col):
        pos = np.asarray(pos, np.float64).reshape(-1, 3)
        tris = np.asarray(tris, np.int64).reshape(-1, 3)
        col = np.asarray(col, np.float64)
        if col.ndim == 1:
            col = np.repeat(col[None, :3], len(pos), 0)
        if self.chunks[-1][3] + len(pos) > self.LIMIT:
            self.chunks.append([[], [], [], 0])
        c = self.chunks[-1]
        c[0].append(pos); c[1].append(tris + c[3]); c[2].append(col[:, :3]); c[3] += len(pos)

    def flush(self):
        if self.chunks[-1][3]:
            self.chunks.append([[], [], [], 0])

    def write(self, out, write_glb, prefix="detail"):
        for f in os.listdir(out):
            if f.startswith(prefix + "_") and f.endswith(".glb"):
                os.remove(os.path.join(out, f))
        names = []
        for k, (P, T, C, n) in enumerate(self.chunks):
            if n == 0:
                continue
            pos = np.concatenate(P); tris = np.concatenate(T); col = np.concatenate(C)
            fn = np.cross(pos[tris[:, 1]] - pos[tris[:, 0]], pos[tris[:, 2]] - pos[tris[:, 0]])
            nrm = np.zeros_like(pos)
            for i in range(3):
                np.add.at(nrm, tris[:, i], fn)
            nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
            rgba = np.concatenate([np.clip(col, 0, 255), np.full((len(pos), 1), 255.0)], 1).round().astype(np.uint8)
            name = "%s_%02d" % (prefix, k)
            write_glb(os.path.join(out, name + ".glb"), pos, tris, nrm, rgba, name)
            names.append(name)
        return names


def prism(path_pts, profile, closed=False):
    """A cross-section (list of (side, up) offsets) swept along points on the ground: path_pts is
    (n, 3) with each point's ground height in y. Returns positions and triangles."""
    P = np.asarray(path_pts, np.float64)
    n = len(P)
    d = np.gradient(P[:, [0, 2]], axis=0)
    d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
    side = np.stack([-d[:, 1], d[:, 0]], 1)          # horizontal, square to the line
    prof = np.asarray(profile, np.float64)
    m = len(prof)
    pos = np.zeros((n, m, 3))
    pos[:, :, 0] = P[:, None, 0] + side[:, None, 0] * prof[None, :, 0]
    pos[:, :, 2] = P[:, None, 2] + side[:, None, 1] * prof[None, :, 0]
    pos[:, :, 1] = P[:, None, 1] + prof[None, :, 1]
    i = np.arange(n * m).reshape(n, m)
    cols = m if closed else m - 1
    tris = []
    for c in range(cols):
        a = i[:-1, c]; b = i[:-1, (c + 1) % m]; e = i[1:, c]; f = i[1:, (c + 1) % m]
        tris.append(np.stack([a, e, b], 1)); tris.append(np.stack([b, e, f], 1))
    return pos, np.concatenate(tris)


def resample_line(pts, step):
    pts = np.asarray(pts, np.float64)
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    s = np.concatenate([[0], np.cumsum(seg)])
    if s[-1] < step:
        return pts
    t = np.arange(0, s[-1], step)
    return np.stack([np.interp(t, s, pts[:, 0]), np.interp(t, s, pts[:, 1])], 1)


def build(region_root, meta, O, H, xs, zs, best, line, tiles, road_half, out, write_glb, seed=7):
    rng = np.random.default_rng(seed)
    step = xs[1] - xs[0]

    def ground(x, z):
        x = np.asarray(x, np.float64); z = np.asarray(z, np.float64)
        fx = np.clip((x - xs[0]) / step, 0, len(xs) - 1.001); fz = np.clip((z - zs[0]) / step, 0, len(zs) - 1.001)
        i = fx.astype(int); j = fz.astype(int); u = fx - i; v = fz - j
        return (H[j, i] * (1 - u) * (1 - v) + H[j, i + 1] * u * (1 - v) + H[j + 1, i] * (1 - u) * v + H[j + 1, i + 1] * u * v)

    def road_dist(x, z):
        i = np.clip(np.round((np.asarray(x) - xs[0]) / step).astype(int), 0, len(xs) - 1)
        j = np.clip(np.round((np.asarray(z) - zs[0]) / step).astype(int), 0, len(zs) - 1)
        return best[j, i]

    rects = np.array([[t["min"][0], t["max"][0], t["min"][2], t["max"][2]] for t in tiles])

    def near(x, z, pad=0.0):
        x = np.asarray(x)[..., None]; z = np.asarray(z)[..., None]
        return ((x >= rects[:, 0] + pad) & (x <= rects[:, 1] - pad) & (z >= rects[:, 2] + pad) & (z <= rects[:, 3] - pad)).any(-1)

    slope = np.hypot(*np.gradient(H, step))

    def slope_at(x, z):
        i = np.clip(np.round((np.asarray(x) - xs[0]) / step).astype(int), 0, len(xs) - 1)
        j = np.clip(np.round((np.asarray(z) - zs[0]) / step).astype(int), 0, len(zs) - 1)
        return slope[j, i]

    # The map's frame: features are in 1e-5 degrees.
    fl, fo = meta["frame"]["lat"], meta["frame"]["lon"]
    p = math.radians(fl)
    m_lat = 111132.92 - 559.82 * math.cos(2 * p) + 1.175 * math.cos(4 * p)
    m_lon = 111412.84 * math.cos(p) - 93.5 * math.cos(3 * p) + 0.118 * math.cos(5 * p)

    def to_ride(lat, lon):
        return (lon * 1e-5 - fo) * m_lon - O[0], -(lat * 1e-5 - fl) * m_lat - O[2]

    feats = json.load(open(os.path.join(region_root, "features.json")))
    kinds = feats["kinds"]
    lines = {}
    for row in feats["lines"]:
        k = kinds[row[0]]
        if k not in ("wall", "fence", "cliff", "retaining_wall", "hedge"):
            continue
        lat, lon = row[6], row[7]
        pts = [to_ride(lat, lon)]
        for i in range(8, len(row) - 1, 2):
            lat += row[i]; lon += row[i + 1]
            pts.append(to_ride(lat, lon))
        pts = np.array(pts)
        if near(pts[:, 0], pts[:, 1]).any():
            lines.setdefault(k, []).append(pts)
    points = {}
    for row in feats["points"]:
        k = kinds[row[0]]
        x, z = to_ride(row[1], row[2])
        if near(x, z):
            points.setdefault(k, []).append((x, z))

    B = Batch()
    counts = {}

    # Drystone walls: about 1.3 m of limestone, battered in from a 0.75 m foot, rough along the
    # top, pale grey with moss low down. They stop short of the tarmac.
    wall_m = 0.0
    for kind in ("wall", "retaining_wall"):
        for pts in lines.get(kind, []):
            r = resample_line(pts, 0.8)
            keep = near(r[:, 0], r[:, 1]) & (road_dist(r[:, 0], r[:, 1]) > road_half + 0.9)
            # Runs of kept samples.
            idx = np.flatnonzero(keep)
            if len(idx) < 2:
                continue
            breaks = np.flatnonzero(np.diff(idx) > 1)
            for run in np.split(idx, breaks + 1):
                if len(run) < 2:
                    continue
                q = r[run]
                g = ground(q[:, 0], q[:, 1])
                n = len(q)
                hgt = 1.25 + rng.normal(0, 0.05, n) + 0.08 * np.sin(np.arange(n) * 0.7)
                top_j = rng.uniform(-0.04, 0.04, n)
                P3 = np.stack([q[:, 0], g, q[:, 1]], 1)
                prof_n = 8
                pos = np.zeros((n, prof_n, 3)); P3d = P3
                d = np.gradient(q, axis=0); d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
                sd = np.stack([-d[:, 1], d[:, 0]], 1)
                # (side, up) per sample: foot, batter, coping stones, batter, foot.
                prof = np.stack([
                    np.stack([np.full(n, -0.38), np.full(n, -0.2)], 1),
                    np.stack([np.full(n, -0.36), np.full(n, 0.35)], 1),
                    np.stack([np.full(n, -0.24), hgt - 0.12], 1),
                    np.stack([np.full(n, -0.16) + top_j, hgt + 0.05], 1),
                    np.stack([np.full(n, 0.16) + top_j, hgt + 0.05], 1),
                    np.stack([np.full(n, 0.24), hgt - 0.12], 1),
                    np.stack([np.full(n, 0.36), np.full(n, 0.35)], 1),
                    np.stack([np.full(n, 0.38), np.full(n, -0.2)], 1)], 1)
                pos[:, :, 0] = P3d[:, None, 0] + sd[:, None, 0] * prof[:, :, 0]
                pos[:, :, 2] = P3d[:, None, 2] + sd[:, None, 1] * prof[:, :, 0]
                pos[:, :, 1] = P3d[:, None, 1] + prof[:, :, 1]
                ii = np.arange(n * prof_n).reshape(n, prof_n)
                tris = []
                for c in range(prof_n - 1):
                    a = ii[:-1, c]; b = ii[:-1, c + 1]; e = ii[1:, c]; f = ii[1:, c + 1]
                    tris.append(np.stack([a, e, b], 1)); tris.append(np.stack([b, e, f], 1))
                tris = np.concatenate(tris)
                # Colour: stone by stone (each sample its own), mossy at the foot.
                stone = srgb(np.clip(np.array([158, 155, 146])[None] + rng.normal(0, 14, (n, 1)) + rng.normal(0, 4, (n, 3)), 60, 220))
                moss = srgb([96, 102, 70])
                col = np.repeat(stone[:, None, :], prof_n, 1)
                low = np.array([1, 0.55, 0, 0, 0, 0, 0.55, 1])[None, :, None]
                col = col * (1 - low * 0.7) + moss[None, None, :] * low * 0.7
                B.add(pos.reshape(-1, 3), tris, col.reshape(-1, 3))
                wall_m += float(np.linalg.norm(np.diff(q, axis=0), axis=1).sum())
    counts["wall_m"] = round(wall_m)

    # Fences: weathered posts every 3 m and three strands of wire.
    fence_m = 0.0
    post_col = srgb([92, 78, 60]); wire_col = srgb([70, 70, 72])
    for pts in lines.get("fence", []):
        r = resample_line(pts, 3.0)
        keep = near(r[:, 0], r[:, 1]) & (road_dist(r[:, 0], r[:, 1]) > road_half + 0.9)
        idx = np.flatnonzero(keep)
        if len(idx) < 2:
            continue
        for run in np.split(idx, np.flatnonzero(np.diff(idx) > 1) + 1):
            if len(run) < 2:
                continue
            q = r[run]; g = ground(q[:, 0], q[:, 1])
            for (x, z), y in zip(q, g):
                a = rng.uniform(0, math.pi)
                c, s = math.cos(a) * 0.05, math.sin(a) * 0.05
                base = np.array([[x - c + s, y - 0.1, z - s - c], [x + c + s, y - 0.1, z + s - c],
                                 [x + c - s, y - 0.1, z + s + c], [x - c - s, y - 0.1, z - s + c]])
                top = base + [0, 1.25 + rng.uniform(-0.05, 0.05), 0]
                pos = np.vstack([base, top])
                tris = [[0, 4, 1], [1, 4, 5], [1, 5, 2], [2, 5, 6], [2, 6, 3], [3, 6, 7], [3, 7, 0], [0, 7, 4], [4, 7, 5], [5, 7, 6]]
                B.add(pos, tris, post_col)
            P3 = np.stack([q[:, 0], g, q[:, 1]], 1)
            for h in (0.45, 0.75, 1.05):
                pos, tris = prism(P3, [(0.0, h - 0.008), (0.0, h + 0.008)])
                B.add(pos, tris, wire_col)
            fence_m += float(np.linalg.norm(np.diff(q, axis=0), axis=1).sum())
    counts["fence_m"] = round(fence_m)

    # Limestone scars along the map's cliffs: a broken pale face dropping from the cliff top.
    scar_m = 0.0
    for pts in lines.get("cliff", []):
        r = resample_line(pts, 1.0)
        r = r[near(r[:, 0], r[:, 1]) & (road_dist(r[:, 0], r[:, 1]) > road_half + 3)]
        if len(r) < 2:
            continue
        n = len(r)
        d = np.gradient(r, axis=0); d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
        sd = np.stack([-d[:, 1], d[:, 0]], 1)
        # The face drops on the downhill side.
        down = np.sign(ground(r[:, 0] + sd[:, 0] * 3, r[:, 1] + sd[:, 1] * 3) - ground(r[:, 0] - sd[:, 0] * 3, r[:, 1] - sd[:, 1] * 3))
        sd *= -np.where(down == 0, 1, down)[:, None]
        hgt = 2.0 + 2.5 * np.abs(np.sin(np.arange(n) * 0.13)) + rng.uniform(0, 0.8, n)
        rows = []
        for k, (off, up) in enumerate([(-0.3, 0.3), (0.4, -0.25), (0.9, -0.55), (1.6, -0.85), (2.4, -1.05)]):
            x = r[:, 0] + sd[:, 0] * (off + rng.uniform(-0.15, 0.15, n))
            z = r[:, 1] + sd[:, 1] * (off + rng.uniform(-0.15, 0.15, n))
            y = ground(r[:, 0], r[:, 1]) + up * hgt
            if k == 4:
                y = np.minimum(y, ground(x, z) - 0.2)
            rows.append(np.stack([x, y, z], 1))
        pos = np.stack(rows, 1)   # n x 5
        ii = np.arange(n * 5).reshape(n, 5)
        tris = []
        for c in range(4):
            a = ii[:-1, c]; b = ii[:-1, c + 1]; e = ii[1:, c]; f = ii[1:, c + 1]
            tris.append(np.stack([a, e, b], 1)); tris.append(np.stack([b, e, f], 1))
        col = srgb(np.clip(np.array([182, 178, 168])[None, None] + rng.normal(0, 12, (n, 5, 1)), 80, 230))
        col[:, 0] = srgb([104, 108, 78])   # turf on the lip
        B.add(pos.reshape(-1, 3), np.concatenate(tris), col.reshape(-1, 3))
        scar_m += n
    counts["scar_m"] = scar_m

    # Field barns (the map's buildings here): rubble stone walls, a stone slate roof.
    barns = 0
    for row in json.load(open(os.path.join(region_root, "buildings.json"))):
        x, z = row[0] - O[0], row[1] - O[2]
        if not near(x, z) or road_dist(x, z) < road_half + 2:
            continue
        w, dpt, rot, h = row[2], row[3], row[4], row[5]
        w = max(w, 4.0); dpt = max(dpt, 4.0); h = min(max(h * 0.75, 3.0), 6.0)
        c, s = math.cos(rot), math.sin(rot)
        def at(u, v, y):
            return [x + u * c - v * s, y, z + u * s + v * c]
        g = float(ground(x, z))
        lo = min(float(ground(*at(u, v, 0)[0::2])) for u in (-w / 2, w / 2) for v in (-dpt / 2, dpt / 2)) - 0.3
        corners = [(-w / 2, -dpt / 2), (w / 2, -dpt / 2), (w / 2, dpt / 2), (-w / 2, dpt / 2)]
        pos = [at(u, v, lo) for u, v in corners] + [at(u, v, g + h) for u, v in corners]
        tris = [[0, 4, 1], [1, 4, 5], [1, 5, 2], [2, 5, 6], [2, 6, 3], [3, 6, 7], [3, 7, 0], [0, 7, 4]]
        B.add(pos, tris, srgb([150, 144, 132]))
        ridge = g + h + w * 0.32
        e = 0.35
        rp = [at(-w / 2 - e, -dpt / 2 - e, g + h - 0.1), at(w / 2 + e, -dpt / 2 - e, g + h - 0.1),
              at(w / 2 + e, dpt / 2 + e, g + h - 0.1), at(-w / 2 - e, dpt / 2 + e, g + h - 0.1),
              at(0, -dpt / 2 - e, ridge), at(0, dpt / 2 + e, ridge)]
        B.add(rp, [[0, 4, 3], [3, 4, 5], [1, 2, 4], [2, 5, 4]], srgb([62, 60, 58]))
        gable = [at(-w / 2, -dpt / 2, g + h), at(w / 2, -dpt / 2, g + h), at(0, -dpt / 2, ridge - 0.05),
                 at(-w / 2, dpt / 2, g + h), at(w / 2, dpt / 2, g + h), at(0, dpt / 2, ridge - 0.05)]
        B.add(gable, [[0, 2, 1], [3, 4, 5]], srgb([150, 144, 132]))
        barns += 1
    counts["barns"] = barns

    # Cattle grids where the map has them on the road: steel bars across the tarmac.
    grids = 0
    for gx, gz in points.get("cattle_grid", []):
        dd = np.hypot(line[:, 0] - gx, line[:, 2] - gz)
        k = int(np.argmin(dd))
        if dd[k] > 25 or k + 1 >= len(line):
            continue
        a, b = line[k], line[min(k + 1, len(line) - 1)]
        f = np.array([b[0] - a[0], b[2] - a[2]]); f /= np.linalg.norm(f)
        sdv = np.array([-f[1], f[0]])
        for i in range(-6, 7):
            cx, cz = a[0] + f[0] * i * 0.16, a[2] + f[1] * i * 0.16
            y = a[1] + 0.012
            p0 = np.array([cx, y, cz]); pts = []
            for side in (-1, 1):
                e = np.array([sdv[0], 0, sdv[1]]) * side * (road_half + 0.1)
                fw = np.array([f[0], 0, f[1]]) * 0.03
                pts += [p0 + e - fw, p0 + e + fw]
            pts = np.array(pts); top = pts + [0, 0.035, 0]
            pos = np.vstack([pts, top])   # 0,1 left; 2,3 right
            tris = [[0, 4, 2], [2, 4, 6], [1, 3, 5], [3, 7, 5], [4, 5, 6], [5, 7, 6], [0, 1, 4], [1, 5, 4], [2, 6, 3], [3, 6, 7]]
            B.add(pos, tris, srgb([60, 62, 64]))
        grids += 1
    counts["cattle_grids"] = grids

    # Snow poles: black and white posts beside the road every 50 m or so, alternating sides,
    # as on the high moor roads.
    poles = 0
    seg = np.linalg.norm(np.diff(line[:, [0, 2]], axis=0), axis=1)
    s_along = np.concatenate([[0], np.cumsum(seg)])
    for k_s, sp in enumerate(np.arange(15.0, s_along[-1] - 5, 48.0)):
        k = int(np.searchsorted(s_along, sp))
        k = min(max(k, 1), len(line) - 2)
        f = line[k + 1, [0, 2]] - line[k - 1, [0, 2]]; f /= np.linalg.norm(f)
        sdv = np.array([-f[1], f[0]]) * (1 if k_s % 2 == 0 else -1)
        x, z = line[k, 0] + sdv[0] * (road_half + 1.3), line[k, 2] + sdv[1] * (road_half + 1.3)
        y = float(ground(x, z))
        seg_n = 6
        ring = np.array([[math.cos(a) * 0.045, math.sin(a) * 0.045] for a in np.linspace(0, 2 * math.pi, seg_n, endpoint=False)])
        hs = [-0.2, 0.9, 1.2, 1.5, 1.8]
        cols_band = [srgb([235, 235, 230]), srgb([20, 20, 20]), srgb([235, 235, 230]), srgb([20, 20, 20])]
        for bi in range(4):
            pos = [[x + rr[0], y + hs[bi], z + rr[1]] for rr in ring] + [[x + rr[0], y + hs[bi + 1], z + rr[1]] for rr in ring]
            tris = []
            for c in range(seg_n):
                a_, b_ = c, (c + 1) % seg_n
                tris += [[a_, seg_n + a_, b_], [b_, seg_n + a_, seg_n + b_]]
            if bi == 3:
                pos.append([x, y + hs[4] + 0.03, z])
                tris += [[seg_n + c, len(pos) - 1, seg_n + (c + 1) % seg_n] for c in range(seg_n)]
            B.add(pos, tris, cols_band[bi])
        poles += 1
    counts["snow_poles"] = poles

    # Boulders: more on steep ground (scree) and under the scars, a few out on the moor.
    def rock(cx, cz, r):
        u = np.linspace(0, math.pi, 6)[1:-1]; v = np.linspace(0, 2 * math.pi, 8, endpoint=False)
        U, V = np.meshgrid(u, v, indexing="ij")
        d = np.stack([np.sin(U) * np.cos(V), np.cos(U), np.sin(U) * np.sin(V)], -1).reshape(-1, 3)
        d = np.vstack([[0, 1, 0], d, [0, -1, 0]])
        rad = r * (1 + rng.uniform(-0.25, 0.25, len(d)))
        sc = np.array([1.0, rng.uniform(0.45, 0.8), rng.uniform(0.7, 1.2)])
        pts = d * rad[:, None] * sc
        a = rng.uniform(0, math.pi * 2); c, s = math.cos(a), math.sin(a)
        pts = pts @ np.array([[c, 0, -s], [0, 1, 0], [s, 0, c]]).T
        y = float(ground(cx, cz)) - r * sc[1] * 0.35
        pos = pts + [cx, y, cz]
        nu, nv = 4, 8
        tris = []
        for vv in range(nv):
            tris.append([0, 1 + (vv + 1) % nv, 1 + vv])
        for uu in range(nu - 1):
            for vv in range(nv):
                a_ = 1 + uu * nv + vv; b_ = 1 + uu * nv + (vv + 1) % nv
                c_ = 1 + (uu + 1) * nv + vv; d_ = 1 + (uu + 1) * nv + (vv + 1) % nv
                tris += [[a_, b_, c_], [b_, d_, c_]]
        last = len(pos) - 1
        for vv in range(nv):
            tris.append([last, 1 + (nu - 1) * nv + vv, 1 + (nu - 1) * nv + (vv + 1) % nv])
        grey = rng.uniform(120, 175)
        col = srgb(np.clip(np.array([grey, grey * 0.98, grey * 0.93])[None] + rng.normal(0, 8, (len(pos), 1)), 50, 230))
        B.add(pos, tris, col)

    cand_x = rng.uniform(xs[0], xs[-1], 60000); cand_z = rng.uniform(zs[0], zs[-1], 60000)
    ok = near(cand_x, cand_z, pad=1.0) & (road_dist(cand_x, cand_z) > road_half + 2.5)
    sl = slope_at(cand_x, cand_z)
    keep_p = np.clip((sl - 0.35) * 0.5, 0.004, 0.25)
    pick = ok & (rng.uniform(0, 1, len(cand_x)) < keep_p)
    nb = 0
    for x, z in zip(cand_x[pick][:2600], cand_z[pick][:2600]):
        rock(x, z, rng.choice([0.25, 0.4, 0.6, 0.9, 1.4], p=[0.35, 0.3, 0.2, 0.1, 0.05]))
        nb += 1
    counts["boulders"] = nb

    # Rush and moor grass tufts by the road: a few thin blades each, straw to green.
    tufts = 0
    nt = 26000
    # Mostly close to the road, thinning out to 70 m.
    k = np.sort(rng.integers(0, len(line), nt))   # in order along the road, so each mesh is one stretch
    f = np.gradient(line[:, [0, 2]], axis=0); f /= np.linalg.norm(f, axis=1, keepdims=True)
    sdv = np.stack([-f[:, 1], f[:, 0]], 1)
    off = (road_half + 0.6 + rng.exponential(14.0, nt)) * rng.choice([-1, 1], nt)
    tx = line[k, 0] + sdv[k, 0] * off + f[k, 0] * rng.uniform(-1, 1, nt)
    tz = line[k, 2] + sdv[k, 1] * off + f[k, 1] * rng.uniform(-1, 1, nt)
    ok = near(tx, tz, pad=0.5) & (road_dist(tx, tz) > road_half + 0.5) & (np.abs(off) < 70) & (slope_at(tx, tz) < 0.9)
    tx, tz = tx[ok], tz[ok]
    ty = ground(tx, tz)
    blades = 5
    n = len(tx)
    ang = rng.uniform(0, 2 * math.pi, (n, blades))
    lean = rng.uniform(0.08, 0.25, (n, blades))
    hgt = rng.uniform(0.22, 0.55, (n, 1)) * rng.uniform(0.7, 1.0, (n, blades))
    w = 0.025
    base = np.stack([tx, ty - 0.02, tz], 1)[:, None, :]
    dirv = np.stack([np.cos(ang), np.zeros_like(ang), np.sin(ang)], -1)
    perp = np.stack([-np.sin(ang), np.zeros_like(ang), np.cos(ang)], -1)
    p0 = base + perp * w; p1 = base - perp * w
    p2 = base + dirv * (lean * hgt)[..., None] + np.array([0, 1, 0]) * hgt[..., None]
    pos = np.stack([p0, p1, p2], 2).reshape(n, blades * 3, 3)
    green = np.array([78, 92, 44]); straw = np.array([150, 136, 84])
    mix = rng.uniform(0, 1, (n, 1))
    tcol = green * (1 - mix) + straw * mix
    tcol = np.repeat(tcol[:, None, :], blades * 3, 1)
    tcol[:, 2::3] *= 1.15            # lighter tips
    tcol[:, 0::3] *= 0.7; tcol[:, 1::3] *= 0.7   # darker at the base
    tcol = srgb(np.clip(tcol, 0, 255))
    per = blades * 3
    tri_one = np.arange(per).reshape(blades, 3)
    G = Batch()                       # grass in its own meshes (grass_NN), drawn only near the camera
    for start in range(0, n, 3000):
        sl_ = slice(start, min(start + 3000, n))
        m_ = sl_.stop - sl_.start
        tris = (tri_one[None] + (np.arange(m_) * per)[:, None, None]).reshape(-1, 3)
        G.add(pos[sl_].reshape(-1, 3), tris, tcol[sl_].reshape(-1, 3))
    tufts = n
    counts["tufts"] = tufts

    # Sheep: Swaledales, white fleece, black faces, grazing on the gentler slopes.
    sheep = 0
    cand_x = rng.uniform(xs[0], xs[-1], 20000); cand_z = rng.uniform(zs[0], zs[-1], 20000)
    rd = road_dist(cand_x, cand_z)
    ok = near(cand_x, cand_z, pad=2) & (rd > road_half + 6) & (slope_at(cand_x, cand_z) < 0.35)
    for x, z in list(zip(cand_x[ok], cand_z[ok]))[:70]:
        yaw = rng.uniform(0, 2 * math.pi); c, s = math.cos(yaw), math.sin(yaw)
        y0 = float(ground(x, z))

        def blob(cx, cy, cz, sx, sy, sz, col, segs=8):
            u = np.linspace(0, math.pi, 6)[1:-1]; v = np.linspace(0, 2 * math.pi, segs, endpoint=False)
            U, V = np.meshgrid(u, v, indexing="ij")
            d = np.stack([np.sin(U) * np.cos(V) * sx, np.cos(U) * sy, np.sin(U) * np.sin(V) * sz], -1).reshape(-1, 3)
            d = np.vstack([[0, sy, 0], d, [0, -sy, 0]])
            lx, lz = d[:, 0] + cx, d[:, 2] + cz
            pos = np.stack([x + lx * c - lz * s, y0 + cy + d[:, 1], z + lx * s + lz * c], 1)
            nu, nv = 4, segs
            tris = [[0, 1 + (vv + 1) % nv, 1 + vv] for vv in range(nv)]
            for uu in range(nu - 1):
                for vv in range(nv):
                    a_ = 1 + uu * nv + vv; b_ = 1 + uu * nv + (vv + 1) % nv
                    c_ = 1 + (uu + 1) * nv + vv; d_ = 1 + (uu + 1) * nv + (vv + 1) % nv
                    tris += [[a_, b_, c_], [b_, d_, c_]]
            last = len(pos) - 1
            tris += [[last, 1 + (nu - 1) * nv + vv, 1 + (nu - 1) * nv + (vv + 1) % nv] for vv in range(nv)]
            B.add(pos, tris, srgb(col))

        fleece = np.clip(np.array([214, 208, 192]) + rng.normal(0, 8), 150, 240)
        blob(0, 0.62, 0, 0.5, 0.28, 0.27, fleece)
        grazing = rng.uniform() < 0.6
        blob(0.52, 0.45 if grazing else 0.75, 0, 0.14, 0.11, 0.09, [28, 26, 24], 6)
        for lx in (-0.28, 0.3):
            for lz in (-0.13, 0.13):
                blob(lx, 0.2, lz, 0.035, 0.22, 0.035, [30, 28, 26], 5)
        sheep += 1
    counts["sheep"] = sheep

    names = B.write(out, write_glb) + G.write(out, write_glb, "grass")
    counts["meshes"] = len(names)
    return counts
