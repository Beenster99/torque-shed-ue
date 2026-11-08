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
        v = "ABCD"[k % 4]
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


def roadside_walls(region_root, O, line, lines, village, road_half, near, ground_abs, rng):
    """Walls along the valley road where the map has none of its own: down in the dale the B6270
    runs between walls (the map's field walls all stop at its edge), out on the moor the road is open
    (and has its cattle grids). Kept back from the houses and their gardens, broken where another road
    or lane joins, and with a field gate (shut) now and then. Returns (lines, gate spots)."""
    r = json.load(open(os.path.join(region_root, "roads.json")))
    N, E = r["nodes"], r["edges"]
    xz = line[:, [0, 2]]
    # Lanes and tracks that join the ride (any road that comes near it but isn't it).
    joins, junctions = [], []
    for e in E:
        pp = e["p"]
        pts = np.array([N[e["a"]]] + [(pp[i], pp[i + 1]) for i in range(0, len(pp), 2)] + [N[e["b"]]], float) - [O[0], O[2]]
        if not near(pts[:, 0], pts[:, 1]).any():
            continue
        q = resample_line(pts, 2.0)
        d = np.min(np.hypot(q[:, None, 0] - xz[None, ::4, 0], q[:, None, 1] - xz[None, ::4, 1]), 1)
        off = q[(d > road_half + 3) & (d < 40)]
        if len(off) and (d > 6).mean() > 0.3:
            joins.append(off)
            k = int(np.argmin(d))
            if e.get("c", 8) < 8 and d[k] < road_half + 12 and near(q[k:k + 1, 0], q[k:k + 1, 1]).all() \
                    and all(np.hypot(*(q[k] - j[0])) > 50 for j in junctions):   # roads and lanes, not tracks
                far_k = min(len(q) - 1, k + 6) if (k == 0 or d[min(len(q) - 1, k + 6)] > d[max(0, k - 6)]) else max(0, k - 6)
                junctions.append((q[k], q[far_k] - q[k], e.get("c", 8)))
    joins = np.concatenate(joins) if joins else np.zeros((0, 2))
    walls = np.concatenate([resample_line(p, 2.0) for p in lines.get("wall", []) + lines.get("retaining_wall", [])]) \
        if lines.get("wall") else np.zeros((0, 2))
    houses = np.array(village["buildings"], float).reshape(-1, 2)
    gardens = village["gardens"]

    def far_from(pts, P, dmin):
        if not len(P):
            return np.ones(len(pts), bool)
        out = np.ones(len(pts), bool)
        for i in range(0, len(pts), 512):
            c = pts[i:i + 512]
            out[i:i + 512] = np.min(np.hypot(c[:, None, 0] - P[None, :, 0], c[:, None, 1] - P[None, :, 1]), 1) > dmin
        return out

    d = np.gradient(xz, axis=0); d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
    nrm = np.stack([-d[:, 1], d[:, 0]], 1)
    low = ground_abs(xz[:, 0], xz[:, 1]) < 385.0   # the enclosed dale (to Angram and Keld), not the open moor
    out, spots = [], []
    for side in (-1, 1):
        P = xz + nrm * side * (road_half + 2.4)
        keep = low & near(P[:, 0], P[:, 1])
        keep &= far_from(P, joins, 7.0) & far_from(P, walls, 3.0) & far_from(P, houses, 13.0)
        for gx, gz, gw, gf, gr in gardens:
            c, s_ = math.cos(gr), math.sin(gr)
            u = (P[:, 0] - gx) * c + (P[:, 1] - gz) * s_; v = -(P[:, 0] - gx) * s_ + (P[:, 1] - gz) * c
            keep &= ~((np.abs(u) < gw / 2 + 3) & (np.abs(v) < gf / 2 + 3))
        idx = np.flatnonzero(keep)
        for run in np.split(idx, np.flatnonzero(np.diff(idx) > 1) + 1):
            if len(run) < 4:
                continue
            q = P[run]
            out.append(q)
            L = len(run) * 2.0
            for t in np.arange(rng.uniform(40, 120), L - 20, rng.uniform(130, 260)):
                spots.append(tuple(q[int(t / 2.0)]))
    return out, spots, junctions


def place_street(props, region_root, O, line, village, junctions, ground, road_half, rng, counts):
    """A start on the villages' street furniture: in each village the ride passes through, a K6
    telephone box, a pillar box and a bench or two on the verge, and its name sign on the left at
    each way in; a finger post and a side-road sign at each lane that joins the ride; by farms near
    the road, a milk churn stand and a couple of wheelie bins. All on the verge, clear of the
    buildings."""
    # Places (ride frame) for the names on the signs. Kirkby Stephen is off the map's region: it is
    # where the road over from Keld goes, 17 km north west.
    places = []
    try:
        for p in json.load(open(os.path.join(region_root, "places.json"))):
            places.append((p["name"], p["x"] - O[0], p["z"] - O[2]))
    except (OSError, ValueError):
        pass
    places.append(("Kirkby Stephen", -19893.0 - O[0], -14790.0 - O[2]))
    PRIORITY = ["Hawes", "Muker", "Keld", "Kirkby Stephen", "Reeth", "Gunnerside", "Thwaite", "Angram"]

    def name_at(x, z, within):
        best = min(places, key=lambda p: math.hypot(p[1] - x, p[2] - z), default=None)
        return best[0] if best and math.hypot(best[1] - x, best[2] - z) < within else ""

    def destination(x, z, ang):
        """The place a road leaving (x, z) at angle ang (0 = +x) is signed to, and its miles."""
        cands = []
        for nm, px_, pz_ in places:
            d = math.hypot(px_ - x, pz_ - z)
            if d < 700:
                continue
            off = abs((math.atan2(pz_ - z, px_ - x) - ang + math.pi) % (2 * math.pi) - math.pi)
            if off < math.radians(55):
                cands.append((PRIORITY.index(nm) if nm in PRIORITY else 99, d, nm))
        if not cands:
            return None
        pr, d, nm = min(cands)
        return "%s %d" % (nm.upper(), max(1, round(d * 1.25 / 1609.0)))

    xz = line[:, [0, 2]]
    seg = np.linalg.norm(np.diff(xz, axis=0), axis=1); s_al = np.concatenate([[0], np.cumsum(seg)])
    fwd = np.gradient(xz, axis=0); fwd /= np.maximum(np.linalg.norm(fwd, axis=1, keepdims=True), 1e-9)
    left = np.stack([fwd[:, 1], -fwd[:, 0]], 1)        # x east, z south: the left of the way you ride
    bld = village.get("bld", [])
    B = np.array([[b[0], b[1]] for b in bld]).reshape(-1, 2)
    R = np.array([max(b[2], b[3]) / 2 + 2.0 for b in bld])
    made = {}
    placed = []
    n = {"phone_boxes": 0, "post_boxes": 0, "benches": 0, "village_signs": 0, "finger_posts": 0,
         "junction_signs": 0, "churn_stands": 0, "bins": 0}

    def parts(name, make):
        if name not in made:
            names = []
            for mat, M in make().items():
                if M.n:
                    nm = "SM_%s_%s" % (name, mat.capitalize())
                    props.meshes[nm] = M; props.mats[nm] = mat
                    names.append(nm)
            made[name] = names
        return made[name]

    def free(x, z, r=1.2):
        if not len(B):
            return True
        return bool((np.hypot(B[:, 0] - x, B[:, 1] - z) > R + r).all())

    def put(name, make, x, z, face, yaw_extra=0.0):
        """face: the direction (x, z) the thing's front (+z) looks."""
        a = math.atan2(-face[0], face[1]) + yaw_extra
        placed.append((x, z))
        for nm in parts(name, make):
            props.add(nm, x, float(ground(x, z)) - 0.02, z, a)

    def verge(k, side, off):
        sd = left[k] * side
        return xz[k] + sd * (road_half + off), -sd   # position, facing the road

    def at_s(sv):
        return int(np.clip(np.searchsorted(s_al, sv), 1, len(xz) - 2))

    for vx, vz, vr in village["villages"]:
        inside = np.flatnonzero(np.hypot(xz[:, 0] - vx, xz[:, 1] - vz) < vr)
        if len(inside) < 10:
            continue
        s0, s1 = s_al[inside[0]], s_al[inside[-1]]
        # name signs on the left at each way in
        vname = name_at(vx, vz, vr + 250).upper()
        for k, side_dir in ((at_s(s0 - 15), 1), (at_s(s1 + 15), -1)):
            p, f = verge(k, 1 if side_dir > 0 else -1, 1.3)
            if vname and free(*p, 0.5):
                put("SignVillage_" + vname.replace(" ", ""), lambda: ST.road_sign(np.random.default_rng(700), "village", vname),
                    p[0], p[1], fwd[k] * -side_dir)
                n["village_signs"] += 1
        plan = [(0.32, "PhoneBox", lambda: ST.phone_box(np.random.default_rng(710)), 1.7, "phone_boxes"),
                (0.55, "PostBox", lambda: ST.post_box(np.random.default_rng(711)), 1.3, "post_boxes"),
                (0.2, "Bench", lambda: ST.bench(np.random.default_rng(712)), 1.8, "benches"),
                (0.74, "Bench", lambda: ST.bench(np.random.default_rng(712)), 1.8, "benches")]
        for t, name, make, off, key in plan:
            for tries in range(8):
                k = at_s(s0 + (s1 - s0) * (t + tries * 0.03 * (-1) ** tries))
                side = 1 if (tries + int(t * 10)) % 2 == 0 else -1
                p, f = verge(k, side, off)
                if free(*p, 0.8):
                    put(name, make, p[0], p[1], f)
                    n[key] += 1
                    break
    # Lanes joining the ride: a finger post on the corner, a side-road sign before it.
    for jp, jdir, jc in junctions:
        d = np.hypot(xz[:, 0] - jp[0], xz[:, 1] - jp[1]); k = int(np.argmin(d))
        side = 1 if (jp - xz[k]) @ left[k] > 0 else -1
        p = xz[k] + left[k] * side * (road_half + 1.6) + fwd[k] * 4.5
        lane = jdir / max(np.linalg.norm(jdir), 1e-9)
        # The arms: either way along the ride (signed by where the road is heading a kilometre on)
        # and up the lane. Angles here are in the ground plane, 0 = +x (east), z south.
        arms = []
        for dirn in (1, -1):
            ka = at_s(s_al[k] + dirn * 1200)
            v = xz[ka] - xz[k]
            arms.append(math.atan2(v[1], v[0]))
        arms.append(math.atan2(lane[1], lane[0]))
        arms = [(a_, destination(p[0], p[1], a_)) for a_ in arms]
        arms = [(a_, t_) for a_, t_ in arms if t_]
        if arms and free(*p, 0.5):
            for nm in parts("FingerPost_%d" % n["finger_posts"], lambda: ST.finger_post(np.random.default_rng(720), arms)):
                props.add(nm, p[0], float(ground(*p)) - 0.1, p[1], 0.0)
            n["finger_posts"] += 1
        for dirn in ((1, -1) if jc <= 5 else ()):   # the triangle warns riders from either way, on their left; not for lanes
            kk = at_s(s_al[k] - dirn * 70)
            p, f = verge(kk, dirn, 1.3)
            if free(*p, 0.5):
                put("SignJunction", lambda: ST.road_sign(np.random.default_rng(701), "junction"), p[0], p[1], -fwd[kk] * dirn)
                n["junction_signs"] += 1
    # Farms near the road: a churn stand at the road end, bins beside it.
    bins = [(0.02, 0.07, 0.03), (0.03, 0.03, 0.03), (0.02, 0.03, 0.08)]
    for b in bld:
        if b[5] != "farm" or n["churn_stands"] >= 8:
            continue
        d = np.hypot(xz[:, 0] - b[0], xz[:, 1] - b[1]); k = int(np.argmin(d))
        if d[k] > 45:
            continue
        side = 1 if (np.array(b[:2]) - xz[k]) @ left[k] > 0 else -1
        p, f = verge(k, side, 1.6)
        if not free(*p, 0.8):
            continue
        put("ChurnStand", lambda: ST.churn_stand(np.random.default_rng(730)), p[0], p[1], f)
        n["churn_stands"] += 1
        for j in range(2):
            q = p + fwd[k] * (1.3 + 0.7 * j)
            ci = int(rng.integers(len(bins)))
            put("Bin_%d" % ci, lambda ci=ci: ST.wheelie_bin(np.random.default_rng(740 + ci), bins[ci]), q[0], q[1], f, rng.normal(0, 0.1))
            n["bins"] += 1
    # Life: cars parked on the verge in front of village houses, a 4x4 in each farmyard near the
    # ride, a quad bike at some, and wrapped bales stacked by the barns, all by the map's buildings.
    gardens = village.get("gardens", [])

    def off_gardens(x, z, pad=1.2):
        for gx, gz, gw, gf, gr in gardens:
            c, s_ = math.cos(gr), math.sin(gr)
            u = (x - gx) * c + (z - gz) * s_; v = -(x - gx) * s_ + (z - gz) * c
            if abs(u) < gw / 2 + pad and abs(v) < gf / 2 + pad:
                return False
        return True

    def yard_free(x, z, r):
        """Clear of every building's footprint (as a rectangle, grown by r)."""
        for bx_, bz_, bw, bd, br, _k in bld:
            c, s_ = math.cos(br), math.sin(br)
            u = (x - bx_) * c + (z - bz_) * s_; v = -(x - bx_) * s_ + (z - bz_) * c
            if abs(u) < bw / 2 + r and abs(v) < bd / 2 + r:
                return False
        return off_gardens(x, z, r)

    def sit(name, make, x, z, a, length):
        """On the ground, nose up or down with the slope along it."""
        c, s_ = math.cos(a), math.sin(a)
        yf = float(ground(x + c * length / 2, z + s_ * length / 2)); yb = float(ground(x - c * length / 2, z - s_ * length / 2))
        pitch = max(-0.25, min(0.25, math.atan2(yf - yb, length)))
        for nm in parts(name, make):
            props.add(nm, x, (yf + yb) / 2 - 0.02, z, a, pitch)

    colours = [(0.25, 0.25, 0.26), (0.02, 0.03, 0.08), (0.3, 0.02, 0.015), (0.55, 0.55, 0.55), (0.02, 0.02, 0.02),
               (0.05, 0.12, 0.05), (0.4, 0.38, 0.32)]
    n.update(cars=0, farm_4x4s=0, quad_bikes=0, bale_stacks=0)
    jpts = np.array([j[0] for j in junctions]).reshape(-1, 2)
    for vx, vz, vr in village["villages"]:
        inside = np.flatnonzero(np.hypot(xz[:, 0] - vx, xz[:, 1] - vz) < vr)
        if len(inside) < 10:
            continue
        parked = 0
        for k in inside[rng.permutation(len(inside))]:
            if parked >= 3:
                break
            if not len(B) or np.hypot(B[:, 0] - xz[k, 0], B[:, 1] - xz[k, 1]).min() > 16:
                continue                      # park where there are houses
            side = 1 if rng.uniform() < 0.5 else -1
            p, f = verge(k, side, 1.25)
            if len(jpts) and np.hypot(jpts[:, 0] - p[0], jpts[:, 1] - p[1]).min() < 12:
                continue
            ok = all(yard_free(*(p + fwd[k] * t), 1.0) for t in (-2.2, 0, 2.2)) and \
                all(math.hypot(p[0] - q[0], p[1] - q[1]) > 3.5 for q in placed)
            if ok and all(math.hypot(p[0] - q[0], p[1] - q[1]) > 8 for q in n.setdefault("_cars", [])):
                ci = int(rng.integers(len(colours)))
                a = math.atan2(fwd[k][1], fwd[k][0]) + (0 if side < 0 else math.pi)   # facing the way of the traffic on its side
                sit("Car_%d" % ci, lambda ci=ci: ST.car(np.random.default_rng(750 + ci), colours[ci]), p[0], p[1], a, 2.6)
                n["_cars"].append(p); parked += 1; n["cars"] += 1
    n.pop("_cars", None)
    for i, b in enumerate(bld):
        x, z, w, d_, rot, kind = b
        dmin = float(np.hypot(xz[:, 0] - x, xz[:, 1] - z).min())
        c, s_ = math.cos(rot), math.sin(rot)
        if kind == "farm" and dmin < 160 and n["farm_4x4s"] < 10:
            # in the yard in front of it, alongside
            p = np.array([x, z]) + np.array([-s_, c]) * (d_ / 2 + 3.2) + np.array([c, s_]) * rng.uniform(-w / 4, w / 4)
            if yard_free(*p, 1.4) and float(np.hypot(xz[:, 0] - p[0], xz[:, 1] - p[1]).min()) > road_half + 3.5:
                ci = int(rng.integers(3))
                cols = [(0.03, 0.07, 0.04), (0.08, 0.08, 0.08), (0.02, 0.04, 0.09)]
                sit("Farm4x4_%d" % ci, lambda ci=ci: ST.utility_4x4(np.random.default_rng(760 + ci), cols[ci]),
                    p[0], p[1], rot + (math.pi if rng.uniform() < 0.5 else 0.0) + rng.normal(0, 0.12), 2.5)
                n["farm_4x4s"] += 1
                if rng.uniform() < 0.5:
                    q = p + np.array([c, s_]) * 3.4
                    if yard_free(*q, 0.9):
                        sit("QuadBike", lambda: ST.quad_bike(np.random.default_rng(770)), q[0], q[1], rot + rng.normal(0, 0.5), 1.2)
                        n["quad_bikes"] += 1
        if kind in ("barn", "farm") and dmin < 350 and i % 3 == 0:
            # bales along the barn's gable end
            nb_ = 5                       # the mesh holds five
            end = 1 if rng.uniform() < 0.5 else -1
            p = np.array([x, z]) + np.array([c, s_]) * end * (w / 2 + 1.4) + np.array([-s_, c]) * (-(nb_ - 1) * 0.66)
            ends = [p + np.array([-s_, c]) * t for t in (0.0, (nb_ - 1) * 1.32)]
            if all(yard_free(*e_, 0.8) for e_ in ends) and float(np.hypot(xz[:, 0] - p[0], xz[:, 1] - p[1]).min()) > road_half + 3:
                k_ = int(rng.integers(3))
                for nm in parts("Bales_%d" % k_, lambda k_=k_: ST.round_bales(np.random.default_rng(780 + k_), 5)):
                    props.add(nm, p[0], float(ground(*p)) - 0.05, p[1], rot + math.pi / 2)
                n["bale_stacks"] += 1
    counts.update(n)


def place_walls(props, lines, gates, ground, near, road_dist, road_half, rng, counts):
    """Drystone walls along the map's wall lines in 2 m lengths, each tilted to the ground under it,
    stopped short of the tarmac with a wall head, a pier on each sharp bend, and a gate between two
    stoops wherever the map has a gate on the wall."""
    gates = np.array(gates, np.float64).reshape(-1, 2)
    wall_m = 0.0; n_gates = 0; n_ends = 0; n_stoops = 0
    road_gates = np.array(lines.get("roadside_gates", []), np.float64).reshape(-1, 2)
    for kind in ("wall", "retaining_wall", "roadside"):
        for pts in lines.get(kind, []):
            r = resample_line(pts, 0.25)
            if len(r) < 4:
                continue
            keep = near(r[:, 0], r[:, 1]) & (road_dist(r[:, 0], r[:, 1]) > road_half + 1.5)   # heads a good metre off the tarmac
            s_all, _ = _arc(r)
            # Gates: the map's, and (the map has few here) one now and then where a wall comes near
            # the road, as a field's way in, and the odd one out in the fields.
            spots = list(gates) if kind != "roadside" else list(road_gates)
            gate_zone = np.zeros(len(r), bool)
            rd = road_dist(r[:, 0], r[:, 1])
            last = -1e9
            for k in (rng.permutation(len(r))[: len(r) // 20] if kind != "roadside" else []):
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
                gate_zone |= np.abs(s_all - s_all[k]) <= 1.9
                k0 = int(np.searchsorted(s_all, s_all[k] - 1.75)); k1 = int(np.searchsorted(s_all, s_all[k] + 1.75))
                k1 = min(k1, len(r) - 1)
                # Never a gate in or swinging over the road (a wall line that meets the tarmac).
                if float(np.min(rd[k0:k1 + 1])) < road_half + 4.0 and kind != "roadside":
                    continue
                d = r[k1] - r[k0]; d /= max(np.linalg.norm(d), 1e-9)
                a = math.atan2(d[1], d[0])
                for kk in (k0, k1):
                    props.add(props.mesh("SM_GateStoop_%s" % "AB"[kk == k1], lambda kk=kk: ST.gate_stoop(np.random.default_rng(400 + (kk == k1)))),
                              r[kk, 0], float(ground(*r[kk])) - 0.25, r[kk, 1], a + rng.normal(0, 0.06))   # squared to the wall
                hinge = r[k0] + d * 0.12
                swing = 0.0 if rng.uniform() < 0.7 or kind == "roadside" else -rng.uniform(0.4, 1.6)   # shut by the road
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
                # Heads (a 1 m piece finished at its outer end) where the wall stops, not where it
                # runs off the near land: the far layer carries it on from there.
                head_lo = total >= 3.0 and (run[0] == 0 or on_tile[run[0] - 1])
                head_hi = total >= 3.0 and (run[-1] == len(r) - 1 or on_tile[run[-1] + 1])
                if total < 2.5 or (head_lo and head_hi and total < 5.0):
                    continue   # a stub that short, headed at both ends, reads as a dropped pillar, not a wall
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
                            # fill the wedge on the outside of the bend with a short length of plain wall
                            mid = math.atan2(math.sin(a) + math.sin(prev), math.cos(a) + math.cos(prev))
                            props.add(props.wall(int(rng.integers(len(WALL_VARIANTS)))), A[0], float(ground(*A)) - 0.03, A[1],
                                      mid, 0.0, 0.0, (0.45, 1.0, 1.0))
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
                        # A gap in the wall (not the wall's own end, nor a gate, which has its stoops):
                        # a stoop stands at the head, as at a gateway.
                        nb = run[0] - 1 if t1 == 0.0 else run[-1] + 1
                        if 0 <= nb < len(r) and not gate_zone[max(0, nb - 8):nb + 9].any():
                            sp = B + d * 0.22
                            props.add(props.mesh("SM_GateStoop_%s" % "AB"[n_ends % 2], lambda kk=n_ends % 2: ST.gate_stoop(np.random.default_rng(400 + kk))),
                                      sp[0], float(ground(*sp)) - 0.25, sp[1], a + rng.normal(0, 0.06))
                            n_stoops += 1
                wall_m += total
    counts["wall_m"] = round(wall_m); counts["gates"] = n_gates; counts["wall_heads"] = n_ends; counts["gap_stoops"] = n_stoops


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
    return "house", w, dpt, (5.1 if w * dpt >= 24 else 3.9)   # two storeys, as the near houses


def place_buildings(props, region_root, O, ground, near, road_dist, road_half, rng, counts, line, to_ride=None):
    """The map's buildings on the near land, as Swaledale buildings (ST.building): in the villages,
    two-storey houses facing the street, in terraces where their footprints meet (a long footprint
    split into houses of its own), some rendered or limewashed, with porches, front garden walls and
    blue or stone slate roofs; the church, the chapel or institute, the school and the pub from the
    map's tags and points of interest; farms (house and barn under one roof) and, standing alone in
    the meadows, field barns. Houses each have their own mesh; barns and sheds of about the same
    shape share one. Returns the village circles and front gardens (for village.json)."""
    rows = []
    for row in json.load(open(os.path.join(region_root, "buildings.json"))):
        x, z = row[0] - O[0], row[1] - O[2]
        if not near(x, z) or road_dist(x, z) < road_half + 2:
            continue
        w, dpt, rot = max(row[2], 3.5), max(row[3], 3.0), row[4]
        if dpt > w:
            w, dpt, rot = dpt, w, rot + math.pi / 2
        mk = BUILDING_KINDS[int(row[6])] if int(row[6]) < len(BUILDING_KINDS) else "house"
        rows.append({"x": x, "z": z, "w": w, "d": dpt, "rot": rot, "mk": mk, "mh": row[5]})
    if not rows:
        counts["buildings"] = 0
        return {"villages": [], "gardens": []}
    XY = np.array([[b["x"], b["z"]] for b in rows])
    D = np.hypot(XY[:, None, 0] - XY[None, :, 0], XY[:, None, 1] - XY[None, :, 1])

    # Points of interest: the pub, the church.
    pubs, churches, lodges = [], [], []
    for f, test, out in (("pois.json", lambda p: p.get("kind") == "pub", pubs),
                         ("landmarks.json", lambda p: p.get("kind") == "worship", churches),
                         ("pois.json", lambda p: p.get("kind") in ("inn", "hostel") and
                          any(w in p.get("name", "") for w in ("Lodge", "Hostel", "Hotel")), lodges)):
        try:
            for p in json.load(open(os.path.join(region_root, f))):
                if test(p):
                    out.append((p["x"] - O[0], p["z"] - O[2]))
        except (OSError, ValueError):
            pass

    def nearest(pts, x, z):
        return min((math.hypot(px - x, pz - z) for px, pz in pts), default=1e9)

    # What each one is.
    for i, b in enumerate(rows):
        area = b["w"] * b["d"]
        b["village"] = int((D[i] < 70).sum()) - 1 >= 5
        alone = (np.delete(D[i], i) > 35).all()
        mk = b["mk"]
        if mk == "worship" or nearest(churches, b["x"], b["z"]) < 12 and area > 60:
            k = "church" if area > 160 or b["w"] > 2.2 * b["d"] else "chapel"
        elif nearest(lodges, b["x"], b["z"]) < 22 and area > 80:
            k = "lodge"              # a big three-storey house: an inn or the old youth hostel
        elif mk == "civic":
            k = "chapel" if area < 70 else "school"
        elif nearest(pubs, b["x"], b["z"]) < 14 and area > 40:
            k = "pub"
        elif mk == "shed" or area < 14:
            k = "shed"
        elif mk == "barn" or (alone and not b["village"] and area < 110):
            k = "barn"               # the field barns of the meadows
        elif b["village"]:
            k = "farm" if area > 110 and b["d"] > 10 else "house"
        else:
            k = "farm" if area > 110 or b["w"] > 15 else "house"
        b["kind"] = k

    # Long village footprints are rows of houses: split them.
    units = []
    for b in rows:
        if b["kind"] == "house" and b["village"] and b["w"] > 9.5:
            n = max(2, int(round(b["w"] / rng.uniform(5.0, 6.8))))
            wts = rng.uniform(0.8, 1.2, n); wts *= b["w"] / wts.sum()
            c, s = math.cos(b["rot"]), math.sin(b["rot"])
            u0 = -b["w"] / 2
            grp = []
            for wu in wts:
                um = u0 + wu / 2
                nb = dict(b); nb.update(x=b["x"] + um * c, z=b["z"] + um * s, w=float(wu))
                grp.append(nb); u0 += wu
            units += grp
        else:
            units.append(b)
    rows = units

    def to_road(x, z):
        dd = np.hypot(line[:, 0] - x, line[:, 2] - z)
        k = int(np.argmin(dd))
        return np.array([line[k, 0] - x, line[k, 2] - z]), float(dd[k])

    # Heights, and the house's style.
    for b in rows:
        k = b["kind"]
        if k == "house":
            b["h"] = 3.9 if b["w"] * b["d"] < 24 else 5.0 + rng.uniform(-0.25, 0.5)
        elif k == "pub":
            b["h"] = 5.9 + rng.uniform(0, 0.3)
        elif k == "lodge":
            b["h"] = 7.8 + rng.uniform(0, 0.4)
        elif k == "farm":
            b["h"] = 5.1 + rng.uniform(-0.2, 0.4)
        elif k == "barn":
            b["h"] = float(np.clip(b["mh"] * 0.62, 3.6, 4.8)) if b["mk"] == "barn" else rng.uniform(3.8, 4.6)
        elif k == "shed":
            b["h"] = 2.6
        elif k == "church":
            b["h"] = 5.2
        elif k == "chapel":
            b["h"] = 6.2 if b["mh"] > 7 else 5.4
        else:
            b["h"] = 4.6
        b["h"] = round(b["h"], 1)

    # Terraces: houses whose footprints meet end to end, parallel. Their fronts face the street
    # together, and they share party walls.
    homes = [i for i, b in enumerate(rows) if b["kind"] in ("house", "pub", "farm", "lodge")]
    parent = list(range(len(rows)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]; i = parent[i]
        return i
    pairs = []
    for a_ in homes:
        for b_ in homes:
            if b_ <= a_:
                continue
            A, B = rows[a_], rows[b_]
            dp = np.array([B["x"] - A["x"], B["z"] - A["z"]])
            if np.hypot(*dp) > 30 or abs(math.sin(B["rot"] - A["rot"])) > 0.22:
                continue
            c, s = math.cos(A["rot"]), math.sin(A["rot"])
            u = dp @ [c, s]; v = dp @ [-s, c]
            if abs(v) < 0.35 * min(A["d"], B["d"]) + 0.6 and abs(abs(u) - (A["w"] + B["w"]) / 2) < 1.2:
                pairs.append((a_, b_))
                parent[find(a_)] = find(b_)
    groups = {}
    for i in range(len(rows)):
        groups.setdefault(find(i), []).append(i)
    for g in groups.values():
        cx = np.mean([rows[i]["x"] for i in g]); cz = np.mean([rows[i]["z"] for i in g])
        tr, _dd = to_road(cx, cz)
        for i in g:
            b = rows[i]
            if b["kind"] == "chapel":       # its gable end (+x) to the street
                f = np.array([math.cos(b["rot"]), math.sin(b["rot"])])
            else:
                f = np.array([-math.sin(b["rot"]), math.cos(b["rot"])])
            if f @ tr < 0:
                b["rot"] += math.pi
            if len(g) > 1:                  # line the row up on the first house's angle
                r0 = rows[g[0]]["rot"]
                b["rot"] = r0 + round((b["rot"] - r0) / math.pi) * math.pi
    for a_, b_ in pairs:   # some rows run level, some step up and down the street
        if rng.uniform() < 0.55:
            rows[b_]["h"] = rows[a_]["h"]
    for b in rows:
        b["party"] = [None, None]
    for a_, b_ in pairs:
        A, B = rows[a_], rows[b_]
        for P_, Q_ in ((A, B), (B, A)):
            c, s = math.cos(P_["rot"]), math.sin(P_["rot"])
            u = (Q_["x"] - P_["x"]) * c + (Q_["z"] - P_["z"]) * s
            P_["party"][1 if u > 0 else 0] = Q_["h"]
        # close the gap (or overlap) between them: each takes half
        c, s = math.cos(A["rot"]), math.sin(A["rot"])
        u = (B["x"] - A["x"]) * c + (B["z"] - A["z"]) * s
        gap = abs(u) - (A["w"] + B["w"]) / 2
        sg = 1 if u > 0 else -1
        for P_, dirn in ((A, sg), (B, -sg)):
            P_["w"] += gap / 2
            P_["x"] += dirn * c * gap / 4; P_["z"] += dirn * s * gap / 4

    # Nothing on the road: every footprint's outline (corners and every 1.5 m along its edges) must
    # stand road_half + 1.5 m off the centre line, or the building is left out (and a terraced
    # neighbour finishes its own end instead).
    A_ = line[:-1][:, [0, 2]]; AB = line[1:][:, [0, 2]] - A_; AB2 = np.maximum((AB ** 2).sum(1), 1e-9)

    def line_dist(pts):
        """Exact distance from each point to the road's centre line (the 2 m grid lookup is up to a
        metre and a half out, enough to let a corner onto the tarmac)."""
        out = np.empty(len(pts))
        for i in range(0, len(pts), 256):
            q = pts[i:i + 256]
            t = np.clip((((q[:, None, :] - A_[None]) * AB[None]).sum(2)) / AB2[None], 0, 1)
            cp = A_[None] + t[..., None] * AB[None]
            out[i:i + 256] = np.sqrt(((q[:, None, :] - cp) ** 2).sum(2)).min(1)
        return out

    def outline(b, front=0.0, pad=0.4):
        """Points round what is built: the footprint grown by the eaves and verges (pad), and on the
        front side by `front` more (a porch, a garden wall)."""
        c, s_ = math.cos(b["rot"]), math.sin(b["rot"])
        u_ = b["w"] / 2 + pad; v0_, v1_ = -(b["d"] / 2 + pad), b["d"] / 2 + pad + front
        pts = []
        for (ua, va), (ub, vb) in (((-u_, v0_), (u_, v0_)), ((u_, v0_), (u_, v1_)), ((u_, v1_), (-u_, v1_)), ((-u_, v1_), (-u_, v0_))):
            for t in np.linspace(0, 1, max(2, int(math.hypot(ub - ua, vb - va) / 1.0) + 2)):
                u = ua + (ub - ua) * t; v = va + (vb - va) * t
                pts.append((b["x"] + u * c - v * s_, b["z"] + u * s_ + v * c))
        return np.array(pts)

    def clear(b, front=0.0):
        if b["kind"] == "chapel":        # its front is the +x gable: grow that end instead
            bb = dict(b); bb["rot"] = b["rot"] - math.pi / 2; bb["w"], bb["d"] = b["d"], b["w"]
            b = bb
        return bool((line_dist(outline(b, front)) > road_half + 1.0).all())
    dropped = set()
    for g in groups.values():
        if all(clear(rows[i]) for i in g):
            continue
        # Set the house (or the whole row) back from the road a little: the map's outlines are a
        # metre or two out at times. What still won't clear is left out.
        cx = np.mean([rows[i]["x"] for i in g]); cz = np.mean([rows[i]["z"] for i in g])
        tr, _dd = to_road(cx, cz)
        away = -tr / max(np.linalg.norm(tr), 1e-9)
        start = [(rows[i]["x"], rows[i]["z"]) for i in g]
        for k_ in np.arange(0.5, 3.01, 0.5):
            for i, (x0, z0) in zip(g, start):
                rows[i]["x"], rows[i]["z"] = x0 + away[0] * k_, z0 + away[1] * k_
            if all(clear(rows[i]) for i in g):
                break
        else:
            for i, (x0, z0) in zip(g, start):
                rows[i]["x"], rows[i]["z"] = x0, z0
            dropped |= {i for i in g if not clear(rows[i])}
    for a_, b_ in pairs:
        for P_, Q_, q in ((rows[a_], rows[b_], b_), (rows[b_], rows[a_], a_)):
            if q in dropped:
                c, s_ = math.cos(P_["rot"]), math.sin(P_["rot"])
                u = (Q_["x"] - P_["x"]) * c + (Q_["z"] - P_["z"]) * s_
                P_["party"][1 if u > 0 else 0] = None
    pairs = [(a_, b_) for a_, b_ in pairs if a_ not in dropped and b_ not in dropped]
    counts["buildings_off_road"] = len(dropped)

    # Make them.
    shapes = {}
    gardens = []
    n_house = n = 0
    for i, b in enumerate(rows):
        if i in dropped:
            continue
        x, z, w, dpt, rot, kind, h = b["x"], b["z"], b["w"], b["d"], b["rot"], b["kind"], b["h"]
        c, s = math.cos(rot), math.sin(rot)
        g = float(ground(x, z))
        lo = min(float(ground(x + u * c - v * s, z + u * s + v * c)) for u in (-w / 2, w / 2) for v in (-dpt / 2, dpt / 2))
        plinth = math.ceil((g - lo + 0.4) * 2) / 2
        if kind in ("barn", "shed"):
            key = (kind, round(w * 2) / 2, round(dpt * 2) / 2, round(h, 1), plinth)
            seed = 900 + len(shapes)
            opt = {"roof": "welsh" if kind == "shed" and rng.uniform() < 0.3 else "stone"}
        else:
            key = ("one", i)
            seed = 5000 + i
            party = tuple(b["party"])
            vil = b["village"]
            opt = {"party": party,
                   "roof": "welsh" if rng.uniform() < (0.35 if kind in ("house", "pub", "chapel", "school") else 0.1) else "stone",
                   "porch": (kind in ("pub", "lodge") or (kind in ("house", "farm") and rng.uniform() < 0.35)) and clear(b, 1.8),
                   "porch_ok": clear(b, 1.8),
                   "sign": kind == "pub"}
            if kind in ("house", "pub", "chapel", "lodge") and rng.uniform() < (0.24 if vil else 0.1):
                opt["render"] = ST.RENDERS[int(rng.integers(len(ST.RENDERS)))]
            if kind in ("pub", "lodge"):
                opt["layout"] = "double"
            # A front garden behind a low wall, where there's room between the house and the road.
            if kind in ("house", "pub", "lodge"):
                fdir = np.array([-s, c])
                fx, fz = x + fdir[0] * dpt / 2, z + fdir[1] * dpt / 2
                tr, dd = to_road(fx, fz)
                room = float(road_dist(fx, fz)) - road_half
                if tr @ fdir > 0.6 * dd and 2.6 < room < 22:
                    f = min(room - 1.1, rng.uniform(3.0, 7.5))
                    while f > 1.8 and not clear(b, f + 0.35):   # the garden wall's whole length off the road
                        f -= 0.5
                if tr @ fdir > 0.6 * dd and 2.6 < room < 22 and f > 1.8:
                    gx, gz = fx + fdir[0] * f, fz + fdir[1] * f
                    opt["garden"] = (round(f, 2), round(g - float(ground(gx, gz)), 2))
                    gardens.append([round(x + fdir[0] * (dpt + f) / 2, 2), round(z + fdir[1] * (dpt + f) / 2, 2),
                                    round(w, 2), round(f, 2), round(rot, 4)])
        if key not in shapes:
            k = len(shapes)
            parts = ST.building(np.random.default_rng(seed), kind, w, dpt, h, plinth, opt)
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
        n_house += kind in ("house", "pub", "lodge")
    # The villages: where the houses stand close.
    rows = [b for i, b in enumerate(rows) if i not in dropped]
    vil = [b for b in rows if b["village"]]
    villages = []
    for b in vil:
        for v in villages:
            if math.hypot(b["x"] - v[0], b["z"] - v[1]) < v[2] + 60:
                pts = v[3] + [(b["x"], b["z"])]
                cx, cz = np.mean(pts, 0)
                r = max(math.hypot(px - cx, pz - cz) for px, pz in pts) + 25
                v[:] = [float(cx), float(cz), float(r), pts]
                break
        else:
            villages.append([b["x"], b["z"], 30.0, [(b["x"], b["z"])]])
    merged = True
    while merged:                     # join villages that grew into each other
        merged = False
        for a_ in range(len(villages)):
            for b_ in range(a_ + 1, len(villages)):
                A, B = villages[a_], villages[b_]
                if math.hypot(A[0] - B[0], A[1] - B[1]) < A[2] + B[2]:
                    pts = A[3] + B[3]
                    cx, cz = np.mean(pts, 0)
                    villages[a_] = [float(cx), float(cz), max(math.hypot(px - cx, pz - cz) for px, pz in pts) + 25, pts]
                    del villages[b_]
                    merged = True
                    break
            if merged:
                break
    counts["buildings"] = n
    counts["houses"] = n_house
    counts["building_shapes"] = len(shapes)
    counts["kinds"] = {k: sum(1 for b in rows if b["kind"] == k) for k in sorted({b["kind"] for b in rows})}
    counts["terrace_party_walls"] = len(pairs)
    counts["front_gardens"] = len(gardens)
    return {"villages": [[round(v[0], 1), round(v[1], 1), round(v[2], 1)] for v in villages],
            "gardens": gardens, "buildings": [[round(b["x"], 1), round(b["z"], 1)] for b in rows],
            "bld": [(b["x"], b["z"], b["w"], b["d"], b["rot"], b["kind"]) for b in rows]}


FAR_REACH = 7000.0   # far walls and buildings out to here from the road (in practice the backdrop's edge,
                     # about 6 km: from the summit you see Wensleydale's walls and Hawes, and Muker)


def place_far(far, region_root, O, near, road_line, to_ride, feats, out, write_glb, counts, reach=FAR_REACH):
    """Beyond the near land, out to `reach` from the road: the map's walls (folds and field walls
    alike) and buildings as light, vertex-coloured shapes on the far hills (the backdrop's own 32 m
    surface, so they sit exactly on it), merged into 1 km cells (far_X_Z.glb). One mesh each, no LODs,
    no cull distance: nothing pops. Coloured about the fell's own value, so they read as texture on
    the land, not as lines drawn on it; the haze does the rest."""
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

    # About the fell's own value (darker and a little greener than the near stone), with little contrast,
    # so the haze doesn't turn them into pale chalk lines; the sides a touch darker than the top.
    side_c = np.array([0.05, 0.054, 0.046]); top_c = np.array([0.058, 0.064, 0.05])
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
            C = np.repeat(np.stack([side_c, top_c, top_c, side_c])[None], len(q), 0)
            # The side away from the sun (south west, as the level's light) a touch darker again.
            lit = sd @ np.array([-0.7071, 0.7071])
            C[:, 0] *= np.where(lit > 0, 0.82, 1.0)[:, None]; C[:, 3] *= np.where(lit < 0, 0.82, 1.0)[:, None]
            C = C.reshape(-1, 3)
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
    village = place_buildings(props, region_root, O, ground, near, road_dist, road_half, prng, counts, line, to_ride)
    lines["roadside"], lines["roadside_gates"], junctions = roadside_walls(region_root, O, line, lines, village, road_half, near,
                                                                lambda x, z: ground(x, z) + O[1], prng)
    place_walls(props, lines, points.get("gate", []), ground, near, road_dist, road_half, prng, counts)
    counts["roadside_wall_runs"] = len(lines["roadside"])
    place_street(props, region_root, O, line, village, junctions, ground, road_half, prng, counts)

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
    with open(os.path.join(out, "village.json"), "w") as f:
        json.dump({"about": "Sim frame metres (x east, z south). villages: [x, z, radius] where the houses stand close "
                            "(street verges, not moor; no snow poles). gardens: front gardens behind their low walls "
                            "[centre x, centre z, width along the house, depth, yaw (radians, width along (cos, sin))]. "
                            "buildings: centres of the near buildings.",
                   "villages": village["villages"], "gardens": village["gardens"], "buildings": village["buildings"]},
                  f, separators=(",", ":"))
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
        # Only out on the moor road: none in the villages, by houses, or down on the valley floor.
        if line[k, 1] + O[1] < 330.0 or any(math.hypot(x - vx, z - vz) < vr + 40 for vx, vz, vr in village["villages"]) \
                or any(math.hypot(x - bx_, z - bz_) < 60 for bx_, bz_ in village["buildings"]):
            continue
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
