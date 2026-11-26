"""The automatic sorter for beenster's bought bikes (beenster 25 Nov, the second batch of 14: "I want exactly the same
treatment, please import them all into the game, include a side stand too").

The first four bought bikes have a hand-made table each (bought_models.py: every object by its name). These 14 come
from the same maker but name their objects all sorts of ways (and three not at all: one object per material), so this
sorts every loose piece by where it is, with the object's name as a hint only:

    tyres        round rubber pieces standing on the ground: their middles are the axles (then they go)
    wheels       what spins with them: pieces centred on an axle, spokes, rim parts
    fork         the two long slanted tubes in line with the front axle give the steering axis; what's on the fork
                 below the lower yoke rides with the wheel (front_hub), above it turns with the bars (steer)
    steer        the bars and all on them; on a naked bike also the headlight, dash, fly screen and front
                 indicators (on a faired bike those stay on the frame)
    swingarm     the arm from the pivot to the rear axle, and what rides on it (chain, hugger, rear caliper)
    shock        the rear damper(s) by name, cut across their length by the exporter
    lights       by their glass and where they are (headlamp, tail, indicators by side, reflectors never lit)
    mirrors      their glass by material, the head round it
    the rest     the frame, by slot from the name (or the paint and where it is)
Small pieces with nothing of their own to say (bolts, decals) take the group and slot of what they're fastened to.

The model's frame is as the first four's: metres, -Y forward, +X the rider's left, Z up, ground at z 0. Everything
here works in the game's (x forward from the rear axle, y left, z up), as numpy arrays.

A model's config (bought_models.AUTO[...]) can say: "faired" (the headlight, fairing, screen and dash are on the
frame), "body" (its main paint material and colour), "boxes" (overrides: pieces whose middle is in a box go to a
given slot and group), "pivot", "hip_band", "drop" (names left out: a rider figure, the model's own stand).
"""
import math
import re
import unicodedata

import bpy  # noqa
import numpy as np
from mathutils import Vector

# ---------------------------------------------------------------------------------------------------------------------
# name hints: (pattern on the cleaned name, kind, slot). First match wins. Kinds:
#   drop, tyre, wheel, disc, sprocket, fork, front_low, swing, shock, spring, shock_or_swing, exhaust, body (on the frame
#   always), front_body (on the bars on a naked bike), steer (on the bars), mirror, decal, bits
HINTS = [
    (r"burro|caballete|^pie$|pata_lateral", "drop", None),
    (r"goma(s)?_?(delantera|trasera|trsera|tracera)?$|neumatico", "tyre", None),
    (r"^(aro|llanta|rueda|rin)|aro_de_llanta|_llanta|ruedas", "wheel", None),
    (r"disco|discos", "disc", None),
    (r"engrane|traccion_cadena|trasmision_cadena|transmision", "sprocket", "final_drive"),
    (r"cadena", "swing", "chain"),
    (r"amortiguador_delantero|suspen[cs]i[oó]n_delantera|ejes_direccion_laterales|junto_ejes|barras", "fork", "fork"),
    (r"direcc?i?on|direcxion|presillas|tapon_eje_direccion|eje_direccion|tornillo_direccion|mecanismo_delantero", "fork", "fork"),
    (r"freno_delantero|freno_frontal|freno_frpntal|pastilla_freno|guardabarros?_delantero|eje_delantero|eje_frontal", "front_low", None),
    (r"soporte_dis[a-z_]*delantero", "front_low", None),
    (r"guarda_cadena|cubierta_cadena|tapa_cadena|cubierta_rueda_trasera|basculante|brazo", "swing", "swingarm"),
    (r"freno_tra[sc]ero|freno_de_atras|calipe_freno_trasero|eje_tra[sc]ero|soporte_dis[a-z_]*tra[sc]ero|soporte_freno_trasero",
     "swing", None),
    (r"resorte", "spring", "shock"),
    (r"amortiguador|pilar_suspension|tornillos_amortiguador", "shock", "shock"),
    (r"su[a-z]*si[oó]m?n?_tra[sc]era|suspensiom|suapension", "shock_or_swing", None),
    (r"escape|silen[a-z]*dor|silecnciador", "exhaust", "exhaust"),
    (r"radiador|rediador", "body", "cooling"),
    (r"motor|filtro|toma_de_aire|palanca_motor", "body", "engine"),
    (r"tanque|deposito|tapon_tanque|cierre_tapon", "body", "tank"),
    (r"a[cs]iento", "body", "saddle"),
    (r"colin|guardabarros?_tra|portamatricula|base_matricula|soporte_chapa|parrilla|maleta|chasis_trasero|cubierta_trasera",
     "body", "tail"),
    (r"(faro|luz|luces|reflector(es)?|indicador(es)?)_?(tra[sc]ero|trasera|tracera)s?|interio_reflectores_traseros|luz_tracera|"
     r"marco_luz_tra", "body", "tail"),
    (r"indicador|intermitente", "front_body", "indicators"),
    (r"faro|luz_delantera|luces_delanteras|reflector_delantero|cristal_luces|^luz$|^luces$", "front_body", "headlight"),
    (r"pantalla|pizarra|tablero|velocimetro", "front_body", "dashboard"),
    (r"carenado|careta|cupula|copula|parabrisas|aleron|carroceria|cubierta_frontal", "front_body", "fairing"),
    (r"espejo|retrovisor", "mirror", "mirrors"),
    (r"estrib|pedal|palanca|sostenedores", "body", "rearsets"),
    (r"manillar|timon|pu[nñ]os|puos|maneta|manilla|controles|boton|swich|frasco|liquido_freno|contrapeso|cubre_manetas|"
     r"protector_pu|cubierta_punos|cierre_frasco", "steer", "handlebars"),
    (r"vinil|grafica|pegatina|logo|decal|dibujo|letras|marca|^kawasaki$", "decal", None),
    (r"tornill|tortmilo|cableado|linea|conducto|cable", "bits", "frame"),
    (r"lateral|cubierta_plastica|cubierta_lateral|cubiertas|plastico_lateral|tapa_lateral", "body", "side_panels"),
    (r"chasis|esqueleto|subchasis", "body", "frame"),
]
LIGHT_MATS = ("m_glass_red", "m_glass_orange", "m_light", "m_chrome_light", "m_chrome_reflector", "m_reflector_chrome")


def clean(name):
    n = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode().lower().strip()
    n = re.sub(r"\.\d+$", "", n)
    return re.sub(r"[\s\-]+", "_", n)


def hint_of(name):
    n = clean(name)
    for pat, kind, slot in HINTS:
        if re.search(pat, n):
            return kind, slot
    return None, None


class AP:
    """One loose piece: its object, the object's name, its hint, its verts in the game's frame."""

    def __init__(self, ob, base):
        self.ob, self.base = ob, base
        me = ob.data
        co = np.empty(len(me.vertices) * 3)
        me.vertices.foreach_get("co", co)
        co = co.reshape(-1, 3)
        mw = np.array(ob.matrix_world)
        self.m = co @ mw[:3, :3].T + mw[:3, 3]
        mi = np.empty(len(me.polygons), dtype=np.int32)
        me.polygons.foreach_get("material_index", mi)
        mats = [m.name.split(".")[0] if m else "" for m in me.materials]
        self.n = len(me.polygons)
        self.mat = mats[np.bincount(mi).argmax()] if len(mi) and mats else ""
        self.kind, self.hslot = hint_of(base)
        self.group = self.slot = self.finish = None
        self.role = ""

    def to_game(self, rear_y):
        m = self.m
        self.g = np.stack([rear_y - m[:, 1], m[:, 0], m[:, 2]], axis=1)
        self.lo, self.hi = self.g.min(0), self.g.max(0)
        self.c = (self.lo + self.hi) / 2
        self.size = self.hi - self.lo
        self.vol = float(np.prod(np.maximum(self.size, 0.005)))

    def axis(self):
        """Its long axis (unit, z up), length along it, how far it spreads off it, and how slim it is."""
        if not hasattr(self, "_ax"):
            g = self.g - self.g.mean(0)
            if len(g) < 3:
                self._ax = (np.array([0, 0, 1.0]), 0.0, 0.0, 1.0)
            else:
                _, s, vt = np.linalg.svd(g, full_matrices=False)
                u = vt[0] if vt[0][2] >= 0 else -vt[0]
                t = g @ u
                off = np.linalg.norm(g - np.outer(t, u), axis=1)
                self._ax = (u, float(t.max() - t.min()), float(off.max()), float(s[1] / max(s[0], 1e-9)))
        return self._ax


def is_rubber(p):
    return "rubber" in p.mat or "tire" in p.mat or p.kind == "tyre"


def seg_dist(px, pz, a, b):
    """Distance in side view (x, z) from each point to the segment a-b."""
    ax, az = a
    bx, bz = b
    vx, vz = bx - ax, bz - az
    t = np.clip(((px - ax) * vx + (pz - az) * vz) / (vx * vx + vz * vz), 0, 1)
    return np.hypot(px - (ax + t * vx), pz - (az + t * vz))


def box_gap(a, b):
    return float(np.max(np.maximum(0, np.maximum(a.lo - b.hi, b.lo - a.hi))))


def sort_pieces(objs, cfg, split_loose, cut_faces, log=print):
    """objs: the model's mesh objects (modifiers applied, unparented). Returns
    (placed: [(ob, base, slot, group, role, finish)], info: axles, tyres, key point sources)."""
    drop = [re.compile(d) for d in cfg.get("drop", [])]
    pieces = []
    for o in objs:
        base = o.name
        if any(d.search(clean(base)) for d in drop) or hint_of(base)[0] == "drop":
            bpy.data.objects.remove(o, do_unlink=True)
            continue
        for ob in split_loose(o):
            if len(ob.data.polygons):
                pieces.append(AP(ob, base))
            else:
                bpy.data.objects.remove(ob, do_unlink=True)
    for p in pieces:
        if p.mat in cfg.get("drop_mats", ()):
            p.kind = "drop"

    # --- the tyres: round rubber standing on the ground (model frame) ---
    def msz(p):
        return p.m.max(0) - p.m.min(0)
    cands = [p for p in pieces if is_rubber(p) and p.kind != "drop" and msz(p)[1] > 0.42 and abs(msz(p)[1] - msz(p)[2]) < 0.06
             and 0.05 < msz(p)[0] < 0.36 and p.m[:, 2].min() < 0.05]
    if len(cands) < 2:
        raise SystemExit(f"tyres not found ({len(cands)})")
    cands.sort(key=lambda p: msz(p)[1] * msz(p)[2], reverse=True)
    two = sorted(cands[:2], key=lambda p: p.m[:, 1].mean())        # front first (-Y forward)
    tyres = {}
    for end, p in zip(("front", "rear"), two):
        lo, hi = p.m.min(0), p.m.max(0)
        cy, cz = (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2
        r = np.hypot(p.m[:, 1] - cy, p.m[:, 2] - cz)
        tyres[end] = {"cy": cy, "cz": cz, "r": (hi[2] - lo[2]) / 2, "w": hi[0] - lo[0], "rim": float(r.min()), "x": (lo[0] + hi[0]) / 2}
        p.kind = "drop"
    rear_y = tyres["rear"]["cy"]
    for p in pieces:
        p.to_game(rear_y)
    F = np.array([rear_y - tyres["front"]["cy"], 0.0, tyres["front"]["cz"]])
    R = np.array([0.0, 0.0, tyres["rear"]["cz"]])
    ax = {"front": F, "rear": R}
    # the rest of each tyre (its lettering, a second half): rubber inside the tyre's ring
    for end, t in tyres.items():
        A = ax[end]
        for p in pieces:
            if p.kind != "drop" and is_rubber(p):
                r = np.hypot(p.g[:, 0] - A[0], p.g[:, 2] - A[2])
                if r.min() > t["rim"] - 0.012 and r.max() < t["r"] + 0.015 and np.abs(p.g[:, 1]).max() < t["w"] / 2 + 0.015:
                    p.kind = "drop"
    for p in pieces:
        if p.kind == "drop":
            bpy.data.objects.remove(p.ob, do_unlink=True)
    pieces = [p for p in pieces if p.kind != "drop"]
    log(f"auto: rear_y {rear_y:.4f} axles F {F.round(3)} R {R.round(3)}; tyres " + ", ".join(f"{k} r {v['r']:.3f} w {v['w']:.3f} rim {v['rim']:.3f}" for k, v in tyres.items()))

    # --- the wheels: what spins ---
    def wheel_end(p):
        for end in ("front", "rear"):
            A, t = ax[end], tyres[end]
            r = np.hypot(p.g[:, 0] - A[0], p.g[:, 2] - A[2])
            inside = r.max() < t["rim"] + 0.02 and np.abs(p.g[:, 1]).max() < t["w"] / 2 + 0.085
            if not inside:
                continue
            spins = abs(p.c[0] - A[0]) < 0.02 and abs(p.c[2] - A[2]) < 0.02 and abs(p.size[0] - p.size[2]) < 0.03 and p.size[0] > 0.035
            u, ln, off, slim = p.axis()
            spoke = r.min() < 0.11 and r.max() > t["rim"] - 0.08 and slim < 0.12 and p.n < 600
            band = r.min() > t["rim"] - 0.035
            if spins or spoke or band or p.kind in ("wheel", "disc"):
                return end
        return None

    for p in pieces:
        if p.kind in ("exhaust",):
            continue
        end = wheel_end(p)
        if end is None and p.kind == "wheel":
            end = "front" if p.c[0] > F[0] / 2 else "rear"
        if end:
            p.group = end + "_wheel"
            if p.kind == "disc" or "disk" in p.mat or "disc" in p.mat:
                p.slot = end + "_disc"
            elif p.kind == "sprocket" or (end == "rear" and p.mat in ("m_steel",) and p.c[1] > 0.06 and p.size[0] > 0.12 and p.kind not in ("wheel",)):
                p.slot = "final_drive"
            else:
                p.slot = end + "_wheel"

    # --- the fork tubes and the steering axis ---
    free = [p for p in pieces if p.group is None]
    tubes = []
    for p in free:
        if p.kind in ("exhaust", "decal", "mirror", "bits", "wheel", "disc", "shock", "spring") or (p.kind == "body" and p.hslot != "frame"):
            continue
        u, ln, off, slim = p.axis()
        if ln < 0.26 or abs(u[1]) > 0.2 or u[0] > 0:
            continue
        tilt = math.degrees(math.atan2(-u[0], u[2]))
        if not 12 < tilt < 38 or not 0.025 < abs(p.c[1]) < 0.2 or not 0.011 < off < 0.07:
            continue
        x_at = p.c[0] + u[0] * (F[2] - p.c[2]) / u[2]
        if abs(x_at - F[0]) < 0.1 and p.c[0] > F[0] - 0.55:
            tubes.append(p)
    if len([t for t in tubes if t.c[1] > 0]) < 1 or len([t for t in tubes if t.c[1] < 0]) < 1:
        raise SystemExit(f"fork tubes not found: {[(t.base, t.c.round(3)) for t in tubes]}")
    longest = max(tubes, key=lambda t: t.axis()[1])
    u0 = longest.axis()[0]
    tubes = [t for t in tubes if math.degrees(math.acos(min(1, abs(float(np.dot(t.axis()[0], u0)))))) < 5]
    d = sum(t.axis()[0] * t.axis()[1] for t in tubes)
    d = np.array([d[0], 0.0, d[2]])
    d /= np.linalg.norm(d)
    tv = np.concatenate([t.g for t in tubes])
    fm = tv.mean(0)
    fl_x = lambda z: fm[0] + d[0] * (z - fm[2]) / d[2]              # the fork line in side view: x at height z
    tube_y = float(np.mean([abs(t.c[1]) for t in tubes]))
    tube_top = float(tv[:, 2].max())
    # the yokes: wide, flat, centred pieces across the tubes
    yokes = [p for p in free if abs(p.c[1]) < 0.02 and p.size[1] > 2 * tube_y - 0.02 and p.size[2] < 0.1 and p.size[0] < 0.25
             and abs(fl_x(p.c[2]) - p.c[0]) < 0.12 and p.c[2] > F[2] + 0.25 and p.c[2] < tube_top + 0.08]
    yokes.sort(key=lambda p: p.c[2])
    if len(yokes) >= 2 and yokes[-1].c[2] - yokes[0].c[2] > 0.08:
        y_low, y_top = float(yokes[0].lo[2]), float(yokes[-1].hi[2])
    elif yokes and yokes[0].c[2] > tube_top - 0.1:
        y_top, y_low = float(yokes[0].hi[2]), float(yokes[0].hi[2]) - 0.22
    else:
        y_top, y_low = tube_top - 0.01, tube_top - 0.23
    y_low = cfg.get("yoke_low", y_low)
    # the steering axis: the stem or head tube if one's found in line, else 30 mm behind the fork line
    stems = []
    for p in free:
        if abs(p.c[1]) < 0.015 and p.size[1] < 0.08 and y_low - 0.06 < p.c[2] < y_top + 0.06:
            u, ln, off, slim = p.axis()
            back = fl_x(p.c[2]) - p.c[0]
            if ln > 0.08 and abs(float(np.dot(u, d))) > 0.99 and 0.0 < back < 0.09 and off < 0.05:
                stems.append(p)
    perp = np.array([-d[2], 0.0, d[0]])                           # (backwards, at right angles to the fork)
    if perp[0] > 0:
        perp = -perp
    if stems:
        sm = np.concatenate([s.g for s in stems]).mean(0)
        sm[1] = 0.0
    else:
        sm = np.array([fl_x(y_top), 0.0, y_top]) - perp * cfg.get("fork_offset", 0.03)
    s_x = lambda z: sm[0] + d[0] * (z - sm[2]) / d[2]              # the steering axis: x at height z
    log(f"auto: fork {len(tubes)} tubes ({sorted({t.base for t in tubes})}), rake {math.degrees(math.atan2(-d[0], d[2])):.1f}, "
        f"yokes {y_low:.3f}..{y_top:.3f}, stem {'found ' + str(sorted({s.base for s in stems})) if stems else 'none'}")
    faired = cfg.get("faired", False)

    # tubes reaching the axle and up into the yokes: cut across, the lower part rides with the wheel
    z_cut = cfg.get("tube_cut", y_low - 0.12)
    along_m = Vector((0.0, -d[0], d[2]))
    at_m = Vector((0.0, rear_y - fl_x(z_cut), z_cut)).dot(along_m)
    new = []
    for t in tubes:
        if t.lo[2] < F[2] + 0.15 and t.hi[2] > y_low:
            up = cut_faces(t.ob, True, along_m, at_m, t.ob.name + "_up")
            t.__dict__.pop("_ax", None)
            t.__init__(t.ob, t.base)
            t.to_game(rear_y)
            t.group, t.slot = "front_hub", "fork"
            if len(up.data.polygons):
                q = AP(up, t.base)
                q.to_game(rear_y)
                q.group, q.slot = "steer", "fork"
                new.append(q)
        else:
            t.group, t.slot = ("front_hub" if t.lo[2] < F[2] + 0.15 else "steer"), "fork"
    pieces += new

    # --- the pivot and the swingarm ---
    tr = tyres["rear"]
    arm_c = []
    for p in pieces:
        if p.group is not None or (p.kind == "body" and p.hslot != "frame") or p.kind in ("exhaust", "steer", "front_body", "mirror", "fork", "front_low", "decal", "spring", "shock"):
            continue
        if "chain" in (p.hslot or "") or p.kind == "sprocket":
            continue
        if -0.1 < p.lo[0] < 0.15 and 0.3 < p.hi[0] < 0.85 and p.size[0] > 0.28 and p.lo[2] > 0.08 and p.hi[2] < R[2] + 0.32 \
                and np.abs(p.g[:, 1]).max() < 0.25:
            arm_c.append(p)
    if "pivot" in cfg:
        pivot = np.array([cfg["pivot"][0], 0.0, cfg["pivot"][1]])
    elif arm_c:
        # (the arm itself holds the axle and is a beam, not a hose or a cable that runs past it)
        def arm_like(p):
            return bool(np.hypot(p.g[:, 0], p.g[:, 2] - R[2]).min() < 0.09 and p.size[2] > 0.05)
        a = max(arm_c, key=lambda p: (p.kind in ("swing", "shock_or_swing"), arm_like(p), p.hi[0]))
        front = a.g[a.g[:, 0] > a.hi[0] - 0.06]
        pivot = np.array([a.hi[0] - 0.035, 0.0, float(front[:, 2].mean())])
    else:
        raise SystemExit("swingarm not found: give a pivot")
    log(f"auto: pivot {pivot.round(3)} from {[(p.base, p.kind, p.mat, p.lo.round(2).tolist(), p.hi.round(2).tolist()) for p in arm_c][:10]}")
    wmax = max(tr["w"] / 2 + 0.09, 0.19)

    def in_swing(p):
        g = p.g
        if g[:, 0].min() < -0.17 or g[:, 0].max() > pivot[0] + 0.07 or np.abs(g[:, 1]).max() > wmax:
            return False
        if g[:, 2].max() > R[2] + tr["r"] + 0.05:
            return False
        dseg = seg_dist(g[:, 0], g[:, 2], (0.0, R[2]), (pivot[0], pivot[2]))
        thr = 0.14 - 0.07 * np.clip(g[:, 0] / pivot[0], 0, 1)          # (narrowing to the pivot: not the rearsets' plates)
        dax = np.hypot(g[:, 0], g[:, 2] - R[2])
        hug = (dax < tr["r"] + 0.06) & (np.abs(g[:, 1]) < tr["w"] / 2 + 0.05)      # (over the tyre: the hugger)
        return bool(np.all((dseg < thr) | hug | (dax < 0.22)))

    # the arm itself (from the axle to the pivot), whatever else it reaches up to (a brace, a linkage mount)
    for p in arm_c:
        if p.hi[0] > pivot[0] - 0.03 and p.group is None:
            p.group = "swingarm"

    # --- each piece's group ---
    tf = tyres["front"]

    def fork_side(p):
        """Behind (+) / ahead (-) of the fork line for every vert."""
        return fl_x(p.g[:, 2]) - p.g[:, 0]

    def in_tube_corridor(p):
        """Along a fork leg all the way (a gaiter, a slider, a brace): (lowest, highest) or None."""
        if np.abs(np.abs(p.g[:, 1]) - tube_y).max() > 0.065:
            return False
        b = fork_side(p)
        return bool(b.max() < 0.075 and b.min() > -0.07 and p.lo[2] > F[2] - 0.08 and p.hi[2] < y_top + 0.06)

    def near_fork_low(p):
        if p.hi[2] > y_low + 0.01 or np.abs(p.g[:, 1]).max() > 0.21:
            return False
        b = fork_side(p)
        if b.max() < 0.13 and b.min() > -0.32:
            return True
        # the mudguard: centred over the tyre
        df = np.hypot(p.g[:, 0] - F[0], p.g[:, 2] - F[2])
        return abs(p.c[1]) < 0.04 and p.lo[2] > F[2] - 0.03 and df.max() < tf["r"] + 0.09 and p.size[1] < tf["w"] + 0.12

    def near_fork_high(p):
        if p.lo[2] < y_low - 0.03 or p.hi[2] > y_top + 0.12 or np.abs(p.g[:, 1]).max() > 0.2:
            return False
        b = fork_side(p)
        return b.max() < 0.075 and b.min() > -0.12

    def bar_box(p):
        """On the bars: high up, a little behind the steering axis (and not the tank's middle)."""
        if p.c[2] < y_top - 0.08 or p.hi[2] > y_top + 0.4 or p.size.max() > 0.9:
            return False
        back = s_x(p.g[:, 2]) - p.g[:, 0]
        ahead = -0.25 if not faired else (-0.12 if (p.c[2] > y_top + 0.05 and abs(p.c[1]) > 0.15) else -0.04)
        if back.max() > 0.45 or back.min() < ahead:      # (on a faired bike, only the mirrors on stalks reach ahead of the axis)
            return False
        return abs(p.c[1]) > 0.08 or p.size[1] > 0.3 or (s_x(p.c[2]) - p.c[0]) < 0.2

    def ahead_high(p):
        """On a naked bike's steering: ahead of the axis, above the lower yoke."""
        return p.lo[2] > y_low - 0.06 and (s_x(p.g[:, 2]) - p.g[:, 0]).max() < 0.1 and p.size.max() < 0.6

    for p in pieces:
        if p.group is not None:
            continue
        k = p.kind
        if k in ("shock", "spring"):
            p.group = "shock" if k == "shock" else "shock_spring"
        elif k == "shock_or_swing":
            u, ln, off, slim = p.axis()
            p.group = "swingarm" if (p.size[0] > 0.3 or in_swing(p)) else "shock"
        elif k == "exhaust" or (k == "body" and p.hslot not in ("rearsets", "frame")):
            p.group = ""
        elif k == "rearsets" or p.hslot == "rearsets":
            p.group = ""
        elif k == "swing":
            if p.hslot == "chain":
                p.group = "swingarm" if p.hi[0] < pivot[0] + 0.13 else ""
            else:
                p.group = "swingarm" if (in_swing(p) or (p.hi[0] < pivot[0] - 0.08 and np.abs(p.g[:, 1]).max() < wmax)) else ""
        elif k == "front_low":
            p.group = "front_hub" if p.hi[2] < y_low + 0.02 else "steer"
        elif k == "fork":
            if in_tube_corridor(p):
                p.group = "front_hub" if p.hi[2] < y_low + 0.02 else "steer"
            elif near_fork_low(p) or (p.hi[2] < y_low + 0.01 and p.lo[2] < F[2] + 0.3):
                p.group = "front_hub"
            elif p.lo[2] > y_low - 0.06:
                p.group = "steer"
            else:
                p.group = ""
        elif k == "steer":
            p.group = "steer" if p.c[2] > y_low - 0.06 else ""
        elif k == "front_body":
            if p.c[0] < s_x(p.c[2]) - 0.25:                           # (a rear one)
                p.group = ""
            elif faired:
                p.group = "steer" if (p.hslot == "handlebars") else ""
            else:
                p.group = "steer" if (p.lo[2] > y_low - 0.08 and p.c[0] > s_x(p.c[2]) - 0.15) else ""
        elif k == "mirror":
            p.group = None                                            # (after: by their glass)
        else:
            if in_tube_corridor(p):
                p.group = "front_hub" if p.hi[2] < y_low + 0.02 else "steer"
            elif near_fork_low(p):
                p.group = "front_hub"
            elif in_swing(p):
                p.group = "swingarm"
            elif near_fork_high(p) or bar_box(p) or (not faired and ahead_high(p)):
                p.group = "steer"
            else:
                p.group = ""

    # --- the mirrors: the glass, its head (what holds it), their stems; on the bars unless on a fairing ---
    glass = [p for p in pieces if p.mat == "m_mirror"]
    for g in glass:
        g.slot, g.role = "mirrors", ""
        heads = [p for p in pieces if p is not g and p.mat != "m_mirror" and box_gap(p, g) < 0.006 and p.size.max() < 0.32
                 and p.kind in ("mirror", None, "bits", "steer", "front_body") and p.size.max() >= g.size.max() * 0.8]
        for h in heads:
            h.slot, h.role = "mirrors", "head"
        # the mirror and its stalk: what it holds on to says whether it turns with the bars
        cluster = [g] + heads + [p for p in pieces if p.kind == "mirror" and p.mat != "m_mirror" and p not in heads
                                 and p.c[1] * g.c[1] > 0 and np.linalg.norm(p.c - g.c) < 0.35]
        for _ in range(3):
            more = [p for p in pieces if p not in cluster and p.size.max() < 0.25 and p.kind in ("mirror", None, "bits")
                    and p.group in (None, "", "steer") and any(box_gap(p, q) < 0.004 for q in cluster)
                    and p.c[2] > y_top - 0.1 and p.c[1] * g.c[1] > 0]
            if not more:
                break
            cluster += more
        touch = [p for p in pieces if p not in cluster and any(box_gap(p, q) < 0.004 for q in cluster)]
        if not faired:
            grp = "steer"
        elif g.c[0] > s_x(g.c[2]) + 0.12:                             # (well out on the fairing: a sports bike's)
            grp = ""
        elif any(p.group == "steer" and p.slot in (None, "handlebars", "fork") for p in touch):
            grp = "steer"
        elif touch:
            grp = "steer" if all(p.group == "steer" for p in touch) else ""
        else:
            grp = "steer" if g.c[0] < s_x(g.c[2]) + 0.03 else ""
        for p in cluster:
            p.group = grp
            if p.kind == "mirror" or p is g or p in heads:
                p.slot = "mirrors"
    for p in pieces:
        if p.kind == "mirror" and p.group is None:
            p.slot = "mirrors"
            p.group = "steer" if (not faired and p.c[2] > y_low) else ""

    # --- small and decal pieces: what they're fastened to ---
    big = [p for p in pieces if p.size.max() >= 0.05 and p.kind not in ("decal", "bits") and p.slot != "mirrors"]
    for p in pieces:
        small = p.size.max() < 0.05
        if p.kind == "decal" or (p.kind == "bits" and p.size.max() < 0.12) or (small and p.kind is None and p.group == ""):
            best = None
            for q in big:
                if q is p:
                    continue
                gap = box_gap(p, q)
                if gap < 0.006 and (best is None or q.vol < best[1]):
                    best = (q, q.vol)
            if best:
                q = best[0]
                if p.kind == "decal" or q.group not in (None, ""):
                    p.group = q.group if q.group is not None else ""
                    if p.kind == "decal" and q.slot:
                        p.slot = q.slot
                    elif p.kind == "decal":
                        p._follow = q
            if p.group is None:
                p.group = ""

    # --- the overrides: boxes in the game's frame ---
    for b in cfg.get("boxes", []):
        lo, hi = np.array(b["lo"]), np.array(b["hi"])
        for p in pieces:
            if np.all(p.c > lo) and np.all(p.c < hi) and (not b.get("name") or re.search(b["name"], clean(p.base))) \
                    and (not b.get("mat") or p.mat == b["mat"]):
                if "group" in b:
                    p.group = b["group"]
                if "slot" in b:
                    p.slot = b["slot"]
                if "finish" in b:
                    p.finish = b["finish"]
                if "kind" in b:
                    p.kind = b["kind"]
    pieces = [p for p in pieces if p.kind != "drop" or bpy.data.objects.remove(p.ob, do_unlink=True)]

    # --- the seat, then each piece's slot ---
    seat = [p for p in pieces if p.hslot == "saddle" or ("leather" in p.mat and p.group == "" and p.size.max() > 0.15)]
    seat_front = max(float(p.hi[0]) for p in seat) if seat else pivot[0] + 0.2
    seat_back = min(float(p.lo[0]) for p in seat) if seat else 0.0
    body_mat = cfg.get("body", {}).get("mat")

    def slot_of(p):
        if p.slot:
            return p.slot
        g = p.group
        if g in ("front_wheel", "rear_wheel"):
            return g
        if g == "front_hub":
            if abs(p.c[1]) < 0.04 and p.lo[2] > F[2]:
                return "front_mudguard"
            if p.hslot:
                return p.hslot
            if p.kind == "front_low" and re.search("freno|calip", clean(p.base)) and p.size.max() < 0.2:
                return "front_caliper"
            return "fork"
        if g == "swingarm":
            if p.hslot:
                return p.hslot
            if p.kind == "swing" and re.search("freno|calip", clean(p.base)) and p.size.max() < 0.2:
                return "rear_caliper"
            return "swingarm"
        if g in ("shock", "shock_top", "shock_bottom", "shock_spring"):
            return "shock"
        if p.hslot:
            return p.hslot
        if g == "steer":
            if p.c[0] > s_x(p.c[2]) + 0.02:
                return "headlight" if p.c[2] < y_top + 0.06 else "fairing"
            return "handlebars" if p.c[2] > y_top - 0.06 else "fork"
        if "leather" in p.mat:
            return "saddle"
        painted = p.mat.startswith("m_paint") and p.mat not in ("m_paint_black_rough",) and p.size.max() > 0.12
        if painted or (body_mat and p.mat == body_mat):
            if p.c[0] > s_x(p.c[2]) - 0.05 or (faired and p.lo[2] < y_low and p.c[0] > pivot[0] + 0.2):
                return "fairing"
            if p.c[2] > 0.7 and seat_front - 0.05 < p.c[0] < s_x(p.c[2]):
                return "tank"
            if p.c[0] < seat_front - 0.25 and p.c[2] > 0.6:
                return "tail"
            return "side_panels"
        return "frame"
    for p in pieces:
        p.slot = slot_of(p)
    for p in pieces:
        if getattr(p, "_follow", None) is not None:
            p.slot = p._follow.slot

    # --- lights: by their glass and where they are ---
    def light_finish(p):
        mat, front = p.mat, p.c[0] > s_x(p.c[2]) - 0.3
        if p.group in ("front_hub", "front_wheel", "rear_wheel", "swingarm"):
            # (the fork legs' and wheels' reflectors: never lit)
            if mat == "m_glass_orange":
                return "orange_reflector", None
            if mat == "m_glass_red":
                return "red_reflector", None
            if mat in LIGHT_MATS:
                return "back_chrome", None
            return None, None
        rear = p.c[0] < seat_back + 0.25
        side = abs(p.c[1]) > 0.07
        ind = p.hslot == "indicators" or p.slot == "indicators"
        if mat == "m_glass_red":
            if ind:
                return "amber", "indicators"
            return ("red_reflector", "tail") if (p.c[2] < 0.6 and p.size.max() < 0.08) else ("tail", "tail")
        if mat == "m_glass_orange":
            if p.c[2] > 0.62 and (front or rear) and side:
                return "amber", "indicators"
            return "orange_reflector", None
        if mat in ("m_light", "m_chrome_light", "m_chrome_reflector", "m_reflector_chrome"):
            if ind or (side and p.size.max() < 0.07 and (p.c[0] > s_x(p.c[2]) + 0.0 or rear) and p.c[2] > 0.62 and abs(p.c[1]) > 0.1):
                return "amber", "indicators"
            if front and p.c[2] > F[2] + 0.2:
                return "lamp", "headlight"
            if rear:
                return "tail", "tail"
        if mat == "m_glass_clear" and ind:
            return "amber", "indicators"
        return None, None
    for p in pieces:
        if p.finish:
            continue
        f, s = light_finish(p)
        if f:
            p.finish = f
            if s and p.slot not in ("mirrors",):
                p.slot = s

    # --- a model with numbered pieces only (no names to go by): its lamps, engine, exhaust and tank by where they are ---
    def have(attr, v):
        return any(getattr(p, attr) == v for p in pieces)
    if not have("finish", "lamp"):
        # (lamps drawn as clear glass over the bulbs: the glass in the nose, below the top yoke, is the headlamp)
        for p in pieces:
            if p.mat == "m_glass_clear" and p.group in ("steer", "") and not p.finish and p.c[0] > s_x(p.c[2]) \
                    and abs(p.c[1]) < 0.12 and p.size.max() < 0.28 and F[2] + 0.2 < p.c[2] < y_top - 0.02:
                p.finish, p.slot = "lamp", "headlight"
    if not have("finish", "amber"):
        # (clear indicator glass: small, out to each side, at the front or behind the seat)
        for p in pieces:
            if p.mat == "m_glass_clear" and p.group in ("steer", "") and not p.finish and p.size.max() < 0.1 \
                    and abs(p.c[1]) > 0.1 and p.c[2] > 0.55 and (p.c[0] > s_x(p.c[2]) - 0.15 or p.c[0] < seat_back + 0.25):
                p.finish, p.slot = "amber", "indicators"
    metal = re.compile("chrome|iron|steel|alu|metal|titan")
    loose = lambda p: p.group == "" and p.slot == "frame" and not p.hslot and not p.finish
    if not have("slot", "exhaust"):
        cands = [p for p in pieces if loose(p) and metal.search(p.mat) and p.size.max() > 0.2 and p.lo[2] > 0.08
                 and p.c[0] < pivot[0] + 0.15 and p.c[2] < 0.95 and (abs(p.c[1]) > 0.05 or p.c[2] < 0.3)]
        if cands:
            can = max(cands, key=lambda p: float(np.prod(p.size)))
            can.slot = "exhaust"
            got, grew = [can], True
            while grew:
                grew = False
                for p in pieces:
                    if loose(p) and p.mat == can.mat and p.c[2] < 0.6 and p.size.max() < 0.6 \
                            and any(box_gap(p, q) < 0.004 for q in got):
                        p.slot = "exhaust"
                        got.append(p)
                        grew = True
            log(f"auto: exhaust by its place: {can.base} {can.mat} and {len(got) - 1} more")
    if not have("slot", "engine"):
        n = 0
        for p in pieces:
            mid = sorted(p.size)[1]
            if loose(p) and pivot[0] + 0.02 < p.c[0] < F[0] - tyres["front"]["r"] - 0.02 and 0.12 < p.c[2] < 0.65 \
                    and abs(p.c[1]) < 0.2 and p.size.max() < 0.48 and mid > 0.06:
                p.slot = "engine"
                n += 1
        log(f"auto: engine by its place: {n} pieces")
    if not have("slot", "tank"):
        tk = [p for p in pieces if p.group == "" and not p.finish and p.c[2] > 0.72 and abs(p.c[1]) < 0.1 and p.size[1] > 0.2
              and seat_front - 0.1 < p.c[0] < s_x(p.c[2]) + 0.05 and "leather" not in p.mat]
        if tk:
            t = max(tk, key=lambda p: float(np.prod(p.size)))
            t.slot = "tank"
            log(f"auto: tank by its place: {t.base} {t.mat}")

    # --- key point sources ---
    fork_pts = [Vector(v) for t in tubes for v in t.g[::max(1, len(t.g) // 400)]]
    stem_pts = [Vector((s_x(y_low), 0.0, y_low)), Vector((s_x(y_top), 0.0, y_top))]
    # the rider's peg: the outermost small piece low down near the pivot, on the left
    pegs = [p for p in pieces if p.c[1] > 0.1 and 0.18 < p.c[2] < 0.52 and pivot[0] - 0.25 < p.c[0] < pivot[0] + 0.32
            and p.size.max() < 0.16 and p.group == "" and (p.hslot == "rearsets" or p.kind is None or "rubber" in p.mat)]
    if cfg.get("peg_name"):
        pegs = [p for p in pegs if re.search(cfg["peg_name"], clean(p.base))] or pegs
    if not pegs:
        raise SystemExit("no footpeg found")
    peg = max(pegs, key=lambda p: (p.hi[1] - abs(p.c[0] - pivot[0] - 0.05) * 0.4))
    grips = [p for p in pieces if p.group == "steer" and p.c[1] > 0.2 and y_top - 0.15 < p.c[2] < y_top + 0.28 and p.size[1] > 0.07
             and p.slot != "mirrors" and p.role != "head" and abs(p.axis()[0][1]) > 0.75
             and ("rubber" in p.mat or p.hslot == "handlebars" or p.mat.startswith("m_plastic"))]
    named_grips = [p for p in grips if re.search(r"pu[nñ]o|puos|goma|grip", clean(p.base)) and not re.search("protector|cubierta", clean(p.base))]
    grip = max(named_grips or grips, key=lambda p: p.hi[1] + (0.05 if "rubber" in p.mat else 0)) if grips else None
    if grip is None:
        raise SystemExit("no grip found")
    caps = [p for p in pieces if re.search(r"tap(a|on)_?(tanque|deposito)|(tanque|deposito)_tapa|cierre_tapon|tapon_tanque", clean(p.base))]
    if not caps:
        tank = [p for p in pieces if p.slot == "tank"] or [p for p in pieces if p.group == "" and p.c[2] > 0.7 and seat_front < p.c[0] < s_x(p.c[2])]
        tv_ = np.concatenate([p.g for p in tank]) if tank else np.array([[seat_front + 0.15, 0, 0.95]])
        mid = tv_[np.abs(tv_[:, 1]) < 0.03]
        mid = mid if len(mid) else tv_
        top = mid[mid[:, 2] > mid[:, 2].max() - 0.01]
        cap_pts = [Vector(top.mean(0))]
    else:
        cap_pts = [Vector(v) for p in caps for v in p.g]
    seat_pts = [Vector(v) for p in seat for v in p.g] if seat else []
    screens = [p for p in pieces if p.slot == "dashboard" or p.hslot == "dashboard" or p.mat == "m_glass_dark"]
    hip_band = cfg.get("hip_band", (seat_front - 0.42, seat_front - 0.06))
    log(f"auto: peg {peg.base} {peg.c.round(3)}, grip {grip.base} {grip.c.round(3)}, seat {seat_back:.3f}..{seat_front:.3f}, "
        f"caps {sorted({p.base for p in caps})}, screens {sorted({p.base for p in screens})}")
    counts = {}
    for p in pieces:
        counts[p.group] = counts.get(p.group, 0) + 1
    log("auto: groups " + ", ".join(f"{k or 'frame'} {v}" for k, v in sorted(counts.items(), key=lambda kv: str(kv[0]))))

    import os
    if os.environ.get("BOUGHT_AUTO_DEBUG"):
        rows = {}
        for p in pieces:
            k = (p.group, p.base, p.slot, p.finish)
            r = rows.setdefault(k, [0, p.lo.copy(), p.hi.copy()])
            r[0] += 1
            r[1], r[2] = np.minimum(r[1], p.lo), np.maximum(r[2], p.hi)
        for (g, b, sl, f), (n, lo, hi) in sorted(rows.items(), key=lambda kv: (str(kv[0][0]), kv[0][1])):
            log(f"  [{g or 'frame'}] {b} -> {sl}{' ' + f if f else ''} x{n}  lo {lo.round(2)} hi {hi.round(2)}")
    if os.environ.get("BOUGHT_AUTO_PIECES"):
        for p in sorted(pieces, key=lambda p: -float(p.size.max())):
            if (p.group == "" and p.size.max() > 0.03) or re.search("glass|light|reflect|chrome", p.mat):
                log(f"  piece {p.base} {p.mat} -> {p.slot} {p.finish or ''} c {p.c.round(2)} size {p.size.round(2)}")
    placed = [(p.ob, p.base, p.slot, p.group, p.role, p.finish) for p in pieces]
    info = {"F": F, "R": R, "rear_y": rear_y, "tyres": tyres, "fork": fork_pts, "stem": stem_pts, "peg": [Vector(v) for v in peg.g],
            "grip": [Vector(v) for v in grip.g], "seat": seat_pts, "cap": cap_pts, "screen": [p.ob for p in screens],
            "hip_band": hip_band, "pivot": Vector(pivot), "steer_top_z": y_top, "steer_bottom_z": y_low}
    return placed, info
