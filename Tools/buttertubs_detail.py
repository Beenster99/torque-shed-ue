"""The countryside's detail for the Buttertubs ride, written by export_buttertubs.py beside the land.

Stone-by-stone things are Nanite meshes placed as instances (Tools/buttertubs_stone.py makes them):
drystone walls along the map's wall lines, with wall heads where they stop, piers on sharp bends, and
field gates between stone stoops; limestone scars along its cliffs with scree below; boulders; a field
barn for each of the map's buildings; and flocks of Swaledale sheep. The meshes go in Props/*.glb and
the instances in props.json (Unreal cm and degrees), which ue_build_level.py turns into instanced
static meshes.

The rest is baked into a few vertex-coloured meshes (detail_NN.glb, under 65k vertices each): wire
fences, the cattle grids and snow poles. The grass and other plants are Tools/make_foliage.py's.
Nothing here has collision: the wheels only see the land and the road.
"""
import json, math, os
import numpy as np

import buttertubs_stone as ST


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


# --- Stone props: meshes made once, placed as instances ---------------------------------------------

WALL_VARIANTS = "ABCDEFGH"
ROCKS = 12
SCARS = "ABCD"


class Props:
    """The prop meshes (built on first use, each from its own seed so it's the same every run) and
    their instances. An instance is Unreal location (cm), rotation (pitch, yaw, roll in degrees) and
    scale along the mesh's own Unreal axes. The glb frame (x, y up, z) is Unreal (x, z, y) as for the
    land, so a mesh turned by angle a about the vertical (its +x along (cos a, sin a) in x east, z south)
    has Unreal yaw a."""

    MATERIAL = {"SM_Gate.": "metal", "SM_Sheep": "fleece", "SM_BarnWood": "wood"}   # a trailing . means the whole name

    def __init__(self):
        self.meshes = {}
        self.inst = {}
        self.lod1 = {}
        self.mats = {}   # mesh: its material, where the name doesn't say   # mesh: a light stand-in for the distance, if the level uses LODs rather than Nanite

    def mesh(self, name, make):
        if name not in self.meshes:
            self.meshes[name] = make()
        return name

    def add(self, name, x, y, z, yaw=0.0, pitch=0.0, roll=0.0, scale=(1.0, 1.0, 1.0)):
        sx, sy, sz = scale
        self.inst.setdefault(name, []).append([round(x * 100, 1), round(z * 100, 1), round(y * 100, 1),
                                               round(math.degrees(pitch), 2), round(math.degrees(yaw), 2),
                                               round(math.degrees(roll), 2),
                                               round(sx, 3), round(sz, 3), round(sy, 3)])

    def wall(self, k):
        v = WALL_VARIANTS[k % len(WALL_VARIANTS)]
        self.mesh("SM_WallFar", lambda: ST.wall_far(np.random.default_rng(99)))
        self.lod1["SM_Wall_" + v] = "SM_WallFar"
        return self.mesh("SM_Wall_" + v, lambda: ST.wall_module(np.random.default_rng(100 + ord(v))))

    def wall_end(self, k):
        v = "AB"[k % 2]
        return self.mesh("SM_WallEnd_" + v, lambda: ST.wall_end(np.random.default_rng(200 + ord(v))))

    def rock(self, x, y, z, size, rng, tilt=0.15):
        k = int(rng.integers(ROCKS))
        name = self.mesh("SM_Rock_%02d" % k, lambda: ST.boulder(np.random.default_rng(300 + k)))
        s = size * rng.uniform(0.85, 1.15, 3)
        self.add(name, x, y - 0.12 * s[1], z, rng.uniform(0, 2 * math.pi), rng.normal(0, tilt), rng.normal(0, tilt), s)

    def write(self, out, write_glb):
        folder = os.path.join(out, "Props")
        os.makedirs(folder, exist_ok=True)
        for f in os.listdir(folder):
            if f.endswith(".glb"):
                os.remove(os.path.join(folder, f))
        info = {}
        verts = 0
        for name, M in sorted(self.meshes.items()):
            nv, nt = M.write(os.path.join(folder, name + ".glb"), write_glb, name)
            verts += nv
            mat = self.mats.get(name, "stone")
            for prefix, m in self.MATERIAL.items():
                if (name == prefix[:-1]) if prefix.endswith(".") else name.startswith(prefix):
                    mat = m
            info[name] = {"material": mat, "triangles": nt}
            if name in self.lod1:
                info[name]["lod1"] = self.lod1[name]
            if name.startswith("SM_Scree"):
                info[name]["shadow"] = False   # small stones: their shadows aren't worth the shadow-map pages
        js = {"about": "Instances of the meshes in Props/ (Tools/buttertubs_detail.py). Each is Unreal "
                       "[x, y, z] cm, [pitch, yaw, roll] degrees, [sx, sy, sz] along the mesh's Unreal axes.",
              "meshes": info, "instances": {k: v for k, v in sorted(self.inst.items())}}
        with open(os.path.join(out, "props.json"), "w") as f:
            json.dump(js, f, separators=(",", ":"))
        return {"prop_meshes": len(self.meshes), "prop_instances": sum(len(v) for v in self.inst.values()),
                "prop_vertices": verts}


def _arc(q):
    s = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(q, axis=0), axis=1))])
    return s, lambda t: np.stack([np.interp(t, s, q[:, 0]), np.interp(t, s, q[:, 1])], -1)


def place_walls(props, lines, gates, ground, near, road_dist, road_half, rng, counts):
    """Drystone walls along the map's wall lines in 2 m lengths, each tilted to the ground under it,
    stopped short of the tarmac with a wall head, a pier on each sharp bend, and a gate between two
    stoops wherever the map has a gate on the wall."""
    gates = np.array(gates, np.float64).reshape(-1, 2)
    wall_m = 0.0; n_gates = 0; n_ends = 0
    for kind in ("wall", "retaining_wall"):
        for pts in lines.get(kind, []):
            r = resample_line(pts, 0.25)
            if len(r) < 4:
                continue
            keep = near(r[:, 0], r[:, 1]) & (road_dist(r[:, 0], r[:, 1]) > road_half + 1.5)   # heads a good metre off the tarmac
            s_all, _ = _arc(r)
            # Gates: the map's, and (the map has few here) one now and then where a wall comes near
            # the road, as a field's way in, and the odd one out in the fields.
            spots = list(gates)
            rd = road_dist(r[:, 0], r[:, 1])
            last = -1e9
            for k in rng.permutation(len(r))[: len(r) // 20]:
                if not keep[k] or abs(s_all[k] - last) < 150:
                    continue
                if (rd[k] < 25 and rng.uniform() < 0.3) or rng.uniform() < 0.012:
                    spots.append(tuple(r[k])); last = s_all[k]
            for gx, gz in spots:
                dd = np.hypot(r[:, 0] - gx, r[:, 1] - gz)
                k = int(np.argmin(dd))
                if dd[k] > 2.5 or not keep[k] or s_all[k] < 2.0 or s_all[-1] - s_all[k] < 2.0:
                    continue
                keep &= np.abs(s_all - s_all[k]) > 1.85
                k0 = int(np.searchsorted(s_all, s_all[k] - 1.75)); k1 = int(np.searchsorted(s_all, s_all[k] + 1.75))
                k1 = min(k1, len(r) - 1)
                # Never a gate in or swinging over the road (a wall line that meets the tarmac).
                if float(np.min(rd[k0:k1 + 1])) < road_half + 4.0:
                    continue
                d = r[k1] - r[k0]; d /= max(np.linalg.norm(d), 1e-9)
                a = math.atan2(d[1], d[0])
                for kk in (k0, k1):
                    props.add(props.mesh("SM_GateStoop_%s" % "AB"[kk == k1], lambda kk=kk: ST.gate_stoop(np.random.default_rng(400 + (kk == k1)))),
                              r[kk, 0], float(ground(*r[kk])) - 0.25, r[kk, 1], rng.uniform(0, 2 * math.pi))
                hinge = r[k0] + d * 0.12
                swing = 0.0 if rng.uniform() < 0.7 else -rng.uniform(0.4, 1.6)
                props.add(props.mesh("SM_Gate", lambda: ST.gate(np.random.default_rng(410))),
                          hinge[0], float(ground(*hinge)) - 0.03, hinge[1], a + swing)
                n_gates += 1
            idx = np.flatnonzero(keep)
            if len(idx) < 4:
                continue
            on_tile = near(r[:, 0], r[:, 1])
            for run in np.split(idx, np.flatnonzero(np.diff(idx) > 1) + 1):
                q = r[run]
                s, at = _arc(q)
                total = s[-1]
                if total < 2.5:
                    continue   # a stub that short reads as a dropped pillar, not a wall
                # Heads (a 1 m piece finished at its outer end) where the wall stops, not where it
                # runs off the near land: the far layer carries it on from there.
                head_lo = total >= 3.0 and (run[0] == 0 or on_tile[run[0] - 1])
                head_hi = total >= 3.0 and (run[-1] == len(r) - 1 or on_tile[run[-1] + 1])
                lo, hi = (0.95 if head_lo else 0.0), (total - 0.95 if head_hi else total)
                n = max(1, int(round((hi - lo) / ST.WALL_L)))
                ts = np.linspace(lo, hi, n + 1)
                P = at(ts)
                prev = None
                for i in range(n):
                    A, B = P[i], P[i + 1]
                    d = B - A; c = np.linalg.norm(d)
                    if c < 0.3:
                        continue
                    d /= c
                    a = math.atan2(d[1], d[0])
                    yA, yB = float(ground(*A)), float(ground(*B))
                    pitch = max(-0.45, min(0.45, math.atan2(yB - yA, c)))
                    m = (A + B) / 2
                    flip = rng.uniform() < 0.5        # walls look alike from both sides: turn half of them round
                    props.add(props.wall(int(rng.integers(len(WALL_VARIANTS)))), m[0], (yA + yB) / 2 - 0.03, m[1],
                              a + (math.pi if flip else 0.0), -pitch if flip else pitch, 0.0,
                              (c / ST.WALL_L * 1.02, rng.uniform(0.92, 1.06), 1.0))
                    if prev is not None:
                        turn = abs((a - prev + math.pi) % (2 * math.pi) - math.pi)
                        if turn > math.radians(28):
                            mid = math.atan2(math.sin(a) + math.sin(prev), math.cos(a) + math.cos(prev))
                            props.add(props.mesh("SM_WallPier", lambda: ST.wall_end(np.random.default_rng(250), 1.0, True)),
                                      A[0], float(ground(*A)) - 0.03, A[1], mid, 0.0, 0.0, (0.9, 1.02, 1.05))
                    prev = a
                for want, t0, t1 in ((head_lo, 0.95, 0.0), (head_hi, total - 0.95, total)):
                        if not want:
                            continue
                        A, B = at(np.array([t0, t1]))
                        d = B - A; c = max(np.linalg.norm(d), 1e-6); d /= c
                        a = math.atan2(d[1], d[0]); m = (A + B) / 2
                        yA, yB = float(ground(*A)), float(ground(*B))
                        pitch = max(-0.45, min(0.45, math.atan2(yB - yA, c)))
                        props.add(props.wall_end(n_ends), m[0], (yA + yB) / 2 - 0.03, m[1], a, pitch, 0.0,
                                  (c / 1.0 * 1.02, 1.0, 1.0))
                        n_ends += 1
                wall_m += total
    counts["wall_m"] = round(wall_m); counts["gates"] = n_gates; counts["wall_heads"] = n_ends


def place_scars(props, cliffs, ground, near, road_dist, road_half, rng, counts):
    """Limestone scars along the map's cliffs: 4 m pieces of bedded face every 3.4 m, set into the
    slope on its downhill side so the face shows from the cliff's top edge to the ground in front of
    it, and scree spilling below."""
    scar_m = 0.0; pieces = 0
    for pts in cliffs:
        r = resample_line(pts, 3.4)
        r = r[near(r[:, 0], r[:, 1]) & (road_dist(r[:, 0], r[:, 1]) > road_half + 4)]
        if len(r) < 1:
            continue
        d = np.gradient(r, axis=0) if len(r) > 1 else np.array([[1.0, 0.0]])
        d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
        for (x, z), (dx, dz) in zip(r, d):
            side = np.array([-dz, dx])
            g0 = float(ground(x + side[0] * 3, z + side[1] * 3)); g1 = float(ground(x - side[0] * 3, z - side[1] * 3))
            down = side if g0 < g1 else -side              # downhill, square to the cliff
            # Out from the cliff line until there's a face of 1.6 m or so to show (the 4 m map has
            # smoothed the cliff into a slope), at most 4 m.
            top = float(ground(x, z)) + 0.35
            for fwd in (2.2, 2.8, 3.4, 4.0):
                fx, fz = x + down[0] * fwd, z + down[1] * fwd
                foot = float(ground(fx, fz))
                if top - foot > 1.6:
                    break
            hgt = max(1.2, top - foot + 0.7) * rng.uniform(0.9, 1.25)
            # The piece's face looks along its +z: +x along the cliff with +z downhill.
            a = math.atan2(-down[0], down[1])
            k = int(rng.integers(len(SCARS)))
            name = props.mesh("SM_Scar_" + SCARS[k], lambda k=k: ST.scar_module(np.random.default_rng(500 + k)))
            sy = hgt / 3.0
            props.add(name, fx, top - 3.0 * sy, fz, a, 0.0, rng.normal(0, 0.03), (rng.uniform(0.9, 1.15), sy, 1.0))
            pieces += 1
            # Scree: spills of small stones below the face, and a few bigger ones further down.
            for _ in range(int(rng.integers(1, 3))):
                o = rng.exponential(1.5) + 0.8
                l = rng.uniform(-1.8, 1.8)
                px = fx + down[0] * o + dx * l; pz = fz + down[1] * o + dz * l
                if road_dist(px, pz) > road_half + 2:
                    k = int(rng.integers(2))
                    name = props.mesh("SM_Scree_" + "AB"[k], lambda k=k: ST.scree(np.random.default_rng(800 + k)))
                    sc = rng.uniform(0.8, 1.3)
                    props.add(name, px, float(ground(px, pz)) - 0.03, pz, rng.uniform(0, 2 * math.pi), 0.0, 0.0, (sc, sc, sc))
            for _ in range(int(rng.integers(1, 5))):
                o = rng.exponential(3.0) + 0.6
                l = rng.uniform(-2.0, 2.0)
                px = fx + down[0] * o + dx * l; pz = fz + down[1] * o + dz * l
                if road_dist(px, pz) > road_half + 2:
                    props.rock(px, float(ground(px, pz)), pz, rng.choice([0.15, 0.25, 0.4, 0.6], p=[0.4, 0.3, 0.2, 0.1]), rng, 0.5)
            scar_m += 3.4
    counts["scar_m"] = round(scar_m); counts["scar_pieces"] = pieces


BUILDING_KINDS = ["house", "shed", "barn", "shop", "block", "industrial", "worship", "civic"]


def building_shape(row):
    """Kind, footprint and eaves height for a map building (buildings.json [x, z, w, d, yaw, h, kind, knot])."""
    w, dpt, h = max(row[2], 3.5), max(row[3], 3.0), row[5]
    k = BUILDING_KINDS[int(row[6])] if int(row[6]) < len(BUILDING_KINDS) else "house"
    if k == "shed" or w * dpt < 14:
        return "shed", w, dpt, 2.6
    if k == "barn":
        return "barn", max(w, 5.0), max(dpt, 4.5), min(max(h * 0.62, 3.2), 5.0)
    if w * dpt > 110 or max(w, dpt) > 15:
        return "farm", w, dpt, min(max(h * 0.9, 4.8), 6.0)
    return "house", w, dpt, (min(max(h * 0.9, 4.6), 6.0) if w * dpt >= 40 else 3.2)


def place_buildings(props, region_root, O, ground, near, road_dist, road_half, rng, counts):
    """The map's buildings on the near land: Swaledale houses, farms (house and barn under one roof),
    field barns and sheds, as light shells (ST.building), sized to their footprints. Buildings of about
    the same shape share a mesh."""
    n = 0
    shapes = {}
    for row in json.load(open(os.path.join(region_root, "buildings.json"))):
        x, z = row[0] - O[0], row[1] - O[2]
        if not near(x, z) or road_dist(x, z) < road_half + 2:
            continue
        kind, w, dpt, h = building_shape(row)
        rot = row[4]
        c, s = math.cos(rot), math.sin(rot)
        g = float(ground(x, z))
        lo = min(float(ground(x + u * c - v * s, z + u * s + v * c)) for u in (-w / 2, w / 2) for v in (-dpt / 2, dpt / 2))
        key = (kind, round(w * 2) / 2, round(dpt * 2) / 2, round(h, 1), math.ceil((g - lo + 0.4) * 2) / 2)
        if key not in shapes:
            k = len(shapes)
            parts = ST.building(np.random.default_rng(900 + k), kind, key[1], key[2], key[3], key[4])
            names = []
            for mat, M in parts.items():
                if M.n:
                    nm = "SM_Bld_%03d_%s" % (k, mat.capitalize())
                    props.meshes[nm] = M
                    props.mats[nm] = mat
                    names.append(nm)
            shapes[key] = names
        for nm in shapes[key]:
            props.add(nm, x, g, z, rot)
        n += 1
    counts["buildings"] = n
    counts["building_shapes"] = len(shapes)


def place_far(far, region_root, O, near, road_line, to_ride, feats, out, write_glb, counts, reach=3000.0):
    """Beyond the near land, out to `reach` from the road: the map's walls (folds and field walls
    alike) and buildings as light, vertex-coloured shapes on the far hills (the backdrop's own 32 m
    surface, so they sit exactly on it), merged into 1 km cells (far_X_Z.glb). One mesh each, no LODs,
    no cull distance: nothing pops. Stone grey a little darker than the near walls, as they read at
    distance; the haze does the rest."""
    Bh, bx, bz = far
    bs = bx[1] - bx[0]

    def fground(x, z):
        """The backdrop's height, on its own triangles (each cell split from (0, 0) to (1, 1))."""
        x = np.asarray(x, float); z = np.asarray(z, float)
        fx = np.clip((x - bx[0]) / bs, 0, len(bx) - 1.001); fz = np.clip((z - bz[0]) / bs, 0, len(bz) - 1.001)
        i = fx.astype(int); j = fz.astype(int); u = fx - i; v = fz - j
        h00 = Bh[j, i]; h10 = Bh[j, i + 1]; h01 = Bh[j + 1, i]; h11 = Bh[j + 1, i + 1]
        return np.where(u >= v, h00 + u * (h10 - h00) + v * (h11 - h10), h00 + v * (h01 - h00) + u * (h11 - h01))

    lo = road_line[:, [0, 2]].min(0) - reach; hi = road_line[:, [0, 2]].max(0) + reach
    lo = np.maximum(lo, [bx[0] + bs, bz[0] + bs]); hi = np.minimum(hi, [bx[-1] - bs, bz[-1] - bs])
    # Distance from the road on a coarse grid, to keep to `reach`.
    gx = np.arange(lo[0], hi[0] + 64, 64.0); gz = np.arange(lo[1], hi[1] + 64, 64.0)
    GX, GZ = np.meshgrid(gx, gz)
    dist = np.full(GX.shape, np.inf)
    for p in road_line[::10]:
        dist = np.minimum(dist, np.hypot(GX - p[0], GZ - p[2]))

    def within(x, z):
        i = np.clip(np.round((np.asarray(x) - gx[0]) / 64).astype(int), 0, len(gx) - 1)
        j = np.clip(np.round((np.asarray(z) - gz[0]) / 64).astype(int), 0, len(gz) - 1)
        inside = (np.asarray(x) >= lo[0]) & (np.asarray(x) <= hi[0]) & (np.asarray(z) >= lo[1]) & (np.asarray(z) <= hi[1])
        return inside & (dist[j, i] < reach)

    cells = {}

    def cell(x, z):
        key = (int(math.floor(x / 1000.0)), int(math.floor(z / 1000.0)))
        if key not in cells:
            cells[key] = [[], [], [], 0]
        return cells[key]

    def add(cx, cz, P, T, C):
        c = cell(cx, cz)
        c[0].append(P); c[1].append(T + c[3]); c[2].append(C); c[3] += len(P)

    side_c = np.array([0.15, 0.148, 0.135]); top_c = np.array([0.12, 0.13, 0.1])
    kinds = feats["kinds"]
    wall_m = 0.0
    for row in feats["lines"]:
        if kinds[row[0]] not in ("wall", "retaining_wall"):
            continue
        lat, lon = row[6], row[7]
        pts = [to_ride(lat, lon)]
        for i in range(8, len(row) - 1, 2):
            lat += row[i]; lon += row[i + 1]
            pts.append(to_ride(lat, lon))
        pts = np.array(pts)
        if not within(pts[:, 0], pts[:, 1]).any():
            continue
        r = resample_line(pts, 6.0)
        if len(r) < 2:
            continue
        keep = within(r[:, 0], r[:, 1]) & ~near(r[:, 0], r[:, 1])
        idx = np.flatnonzero(keep)
        if len(idx) < 2:
            continue
        for run in np.split(idx, np.flatnonzero(np.diff(idx) > 1) + 1):
            # Reach back one sample onto the near land so the far wall meets the near one.
            run = np.arange(max(run[0] - 1, 0), min(run[-1] + 2, len(r)))
            q = r[run]
            if len(q) < 2:
                continue
            g = fground(q[:, 0], q[:, 1])
            d = np.gradient(q, axis=0); d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
            sd = np.stack([-d[:, 1], d[:, 0]], 1)
            prof = [(-0.45, -0.7), (-0.3, 1.4), (0.3, 1.4), (0.45, -0.7)]
            P = np.zeros((len(q), 4, 3))
            for j, (o, y) in enumerate(prof):
                P[:, j, 0] = q[:, 0] + sd[:, 0] * o; P[:, j, 2] = q[:, 1] + sd[:, 1] * o; P[:, j, 1] = g + y
            ii = np.arange(len(q) * 4).reshape(len(q), 4)
            T = []
            for j in range(3):
                a_, b_, c_, d_ = ii[:-1, j], ii[:-1, j + 1], ii[1:, j], ii[1:, j + 1]
                T += [np.stack([a_, c_, b_], 1), np.stack([b_, c_, d_], 1)]
            T = np.concatenate(T)
            P = P.reshape(-1, 3)
            # Outwards: away from the wall's own centre line.
            fc = P[T].mean(1); fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
            ctr = np.repeat(np.stack([q[:, 0], g + 0.2, q[:, 1]], 1), 4, 0)[T[:, 0]]
            flip = (fn * (fc - ctr)).sum(1) < 0
            T[flip] = T[flip][:, [0, 2, 1]]
            C = np.repeat(np.stack([side_c, top_c, top_c, side_c])[None], len(q), 0).reshape(-1, 3)
            C = C * np.random.default_rng(len(q)).uniform(0.85, 1.15, (len(q), 1)).repeat(4, 0)
            m = q.mean(0)
            add(m[0], m[1], P, T, C)
            wall_m += float(np.linalg.norm(np.diff(q, axis=0), axis=1).sum())
    nb = 0
    for row in json.load(open(os.path.join(region_root, "buildings.json"))):
        x, z = row[0] - O[0], row[1] - O[2]
        if near(x, z) or not within(x, z):
            continue
        kind, w, dpt, h = building_shape(row)
        rot = row[4]; c, s = math.cos(rot), math.sin(rot)
        y = float(fground(x, z))
        ylo = min(float(fground(x + u * c - v * s, z + u * s + v * c)) for u in (-w / 2, w / 2) for v in (-dpt / 2, dpt / 2)) - 0.5
        P, T, C = ST.far_building(w, dpt, h, rot, x, ylo, y, z, kind)
        add(x, z, P, T, C)
        nb += 1
    for f in os.listdir(out):
        if f.startswith("far_") and f.endswith(".glb"):
            os.remove(os.path.join(out, f))
    tris = 0
    for (kx, kz), (P, T, C, n) in sorted(cells.items()):
        P = np.concatenate(P); T = np.concatenate(T); C = np.concatenate(C)
        fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
        nrm = np.zeros_like(P)
        for i in range(3):
            np.add.at(nrm, T[:, i], fn)
        nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
        rgba = np.concatenate([np.clip(C, 0, 1), np.ones((len(P), 1))], 1)
        name = "far_%d_%d" % (kx, kz)
        write_glb(os.path.join(out, name + ".glb"), P, T, nrm, (rgba * 255).round().astype(np.uint8), name)
        tris += len(T)
    counts["far_wall_m"] = round(wall_m); counts["far_buildings"] = nb; counts["far_cells"] = len(cells)
    counts["far_triangles"] = tris


def place_sheep(props, ground, near, road_dist, road_half, slope_at, xs, zs, rng, counts):
    """Flocks of Swaledales on the gentler ground: most grazing, some looking up, a few lying down."""
    poses = [("SM_Sheep_A", "graze"), ("SM_Sheep_B", "graze"), ("SM_Sheep_C", "look"), ("SM_Sheep_D", "lie")]
    n = 0
    tries = 0
    while n < 70 and tries < 4000:
        tries += 1
        cx, cz = rng.uniform(xs[0], xs[-1]), rng.uniform(zs[0], zs[-1])
        if not (near(cx, cz, 4) and road_dist(cx, cz) > road_half + 10 and slope_at(cx, cz) < 0.3):
            continue
        heading = rng.uniform(0, 2 * math.pi)
        for _ in range(int(rng.integers(3, 10))):
            x = cx + rng.normal(0, 7); z = cz + rng.normal(0, 7)
            if not (near(x, z, 2) and road_dist(x, z) > road_half + 6 and slope_at(x, z) < 0.4):
                continue
            k = int(rng.choice(4, p=[0.3, 0.3, 0.25, 0.15]))
            name, pose = poses[k]
            name = props.mesh(name, lambda k=k, pose=pose: ST.sheep(np.random.default_rng(700 + k), pose))
            a = heading + rng.normal(0, 0.8)
            fx, fz = math.cos(a), math.sin(a)
            pitch = math.atan2(float(ground(x + fx * 0.5, z + fz * 0.5)) - float(ground(x - fx * 0.5, z - fz * 0.5)), 1.0)
            sc = rng.uniform(0.92, 1.08)
            props.add(name, x, float(ground(x, z)) - 0.02, z, a, pitch, 0.0, (sc, sc, sc))
            n += 1
    counts["sheep"] = n


def build(region_root, meta, O, H, xs, zs, best, line, tiles, road_half, out, write_glb, seed=7, far=None):
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

    props = Props()
    prng = np.random.default_rng(seed + 1000)   # the stone props have their own stream
    place_walls(props, lines, points.get("gate", []), ground, near, road_dist, road_half, prng, counts)

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

    place_scars(props, lines.get("cliff", []), ground, near, road_dist, road_half, prng, counts)
    place_buildings(props, region_root, O, ground, near, road_dist, road_half, prng, counts)
    if far is not None:
        place_far(far, region_root, O, near, line, to_ride, feats, out, write_glb, counts)

    # Cattle grids where the map has them on the road: a dark pit across the tarmac with galvanised
    # bars over it, following the road's crown and standing a little proud of it as real ones do,
    # and a low concrete kerb at each end. Their places go in route.json for the rumble.
    grids = 0
    grid_points = []
    crown = 0.05   # export_buttertubs.CROWN
    across = np.linspace(-1.0, 1.0, 11)

    def surface(cx, cz, f, sdv, base_y, lift, half_w):
        """Points across the road at (cx, cz), on the crowned surface plus lift."""
        w = across * half_w
        y = base_y + crown * (1 - np.clip(w / road_half, -1, 1) ** 2) + lift
        return np.stack([cx + sdv[0] * w, y, cz + sdv[1] * w], 1)

    def strip(c0, c1, thick, col):
        """A slab between two rows of points (front and back edges), thick metres deep below them."""
        n = len(c0)
        top = np.vstack([c0, c1]); bot = top - [0, thick, 0]
        pos = np.vstack([top, bot])
        tris = []
        for k in range(n - 1):
            a0, a1, b0, b1 = k, k + 1, n + k, n + k + 1   # top: front row a, back row b
            tris += [[a0, b0, a1], [a1, b0, b1]]
            o = 2 * n
            tris += [[a0, a1, o + a0], [a1, o + a1, o + a0]]                 # front face
            tris += [[b0, o + b0, b1], [b1, o + b0, o + b1]]                 # back face
        B.add(pos, tris, col)

    for gx, gz in points.get("cattle_grid", []):
        dd = np.hypot(line[:, 0] - gx, line[:, 2] - gz)
        k = int(np.argmin(dd))
        if dd[k] > 25 or k + 1 >= len(line):
            continue
        a, b = line[k], line[min(k + 1, len(line) - 1)]
        f = np.array([b[0] - a[0], b[2] - a[2]]); f /= np.linalg.norm(f)
        sdv = np.array([-f[1], f[0]])
        half_w = road_half + 0.15
        # The pit: near-black, just over the tarmac so it shows between the bars.
        c0 = surface(a[0] - f[0] * 1.15, a[2] - f[1] * 1.15, f, sdv, a[1], 0.004, half_w)
        c1 = surface(a[0] + f[0] * 1.15, a[2] + f[1] * 1.15, f, sdv, a[1], 0.004, half_w)
        strip(c0, c1, 0.002, srgb([6, 6, 6]))
        # Bars: 14 rails, 5 cm wide with 11 cm gaps, 2.5 cm proud of the road, weathered galvanised
        # steel (darker than new, so the band reads as stripes over a dark pit rather than a pale slab).
        for i in range(-7, 7):
            m = (i + 0.5) * 0.16
            c0 = surface(a[0] + f[0] * (m - 0.025), a[2] + f[1] * (m - 0.025), f, sdv, a[1], 0.025, half_w)
            c1 = surface(a[0] + f[0] * (m + 0.025), a[2] + f[1] * (m + 0.025), f, sdv, a[1], 0.025, half_w)
            shade = 78 + 10 * ((i * 7) % 3)
            strip(c0, c1, 0.03, srgb([shade, shade + 2, shade - 2]))
        # Concrete kerbs either side of the pit, beyond the road edge.
        for side in (-1, 1):
            kx = a[0] + sdv[0] * side * (half_w + 0.2); kz = a[2] + sdv[1] * side * (half_w + 0.2)
            ky = ground(np.array([kx]), np.array([kz]))[0]
            pts = []
            for u in (-1.25, 1.25):
                for v in (-0.2, 0.2):
                    pts.append([kx + f[0] * u + sdv[0] * v, ky - 0.1, kz + f[1] * u + sdv[1] * v])
            pts = np.array(pts); top = pts + [0, 0.35, 0]
            pos = np.vstack([pts, top])
            tris = [[0, 4, 1], [1, 4, 5], [1, 5, 3], [3, 5, 7], [3, 7, 2], [2, 7, 6], [2, 6, 0], [0, 6, 4], [4, 6, 5], [5, 6, 7]]
            B.add(pos, tris, srgb([150, 148, 140]))
        grid_points.append([round(float(a[0]), 2), round(float(a[1]), 2), round(float(a[2]), 2), int(k)])
        grids += 1
    counts["grid_points"] = grid_points
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

    # Boulders: more on steep ground, a few out on the moor.
    cand_x = rng.uniform(xs[0], xs[-1], 60000); cand_z = rng.uniform(zs[0], zs[-1], 60000)
    ok = near(cand_x, cand_z, pad=1.0) & (road_dist(cand_x, cand_z) > road_half + 2.5)
    sl = slope_at(cand_x, cand_z)
    keep_p = np.clip((sl - 0.35) * 0.5, 0.004, 0.25)
    pick = ok & (rng.uniform(0, 1, len(cand_x)) < keep_p)
    nb = 0
    for x, z in zip(cand_x[pick][:2600], cand_z[pick][:2600]):
        size = rng.choice([0.25, 0.4, 0.6, 0.9, 1.4], p=[0.35, 0.3, 0.2, 0.1, 0.05]) * rng.uniform(0.8, 1.25)
        props.rock(x, float(ground(x, z)), z, size, prng)
        nb += 1
    counts["boulders"] = nb

    place_sheep(props, ground, near, road_dist, road_half, slope_at, xs, zs, prng, counts)

    names = B.write(out, write_glb)
    for f in os.listdir(out):   # the old stick tufts, now replaced by the foliage
        if f.startswith("grass_") and f.endswith(".glb"):
            os.remove(os.path.join(out, f))
    counts["meshes"] = len(names)
    counts.update(props.write(out, write_glb))
    return counts
