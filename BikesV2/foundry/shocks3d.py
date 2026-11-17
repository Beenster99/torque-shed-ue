"""Common parts, stage 8: rear shocks, filler caps and number plate hangers (common parts plan, 16 Nov).

The joint (docs/common_parts_joints.md): a shock keeps the model's two eye points and so its eye-to-eye
length; the bike's own eyes, studs, forked brackets and bolts stay, only the shock between them changes. Twin
shocks stand where the bike's stood (outside the arm); a single shock stays on the centre line. A filler cap
keeps the tank's cap point and the bike's six-bolt ring (39.5 mm) on its raised neck, so every cap fits every
tank. A plate hanger is an optional part: a short tail tidy under the tail, or a side mount from the left
axle; it carries the plate, the plate lamp and the rear indicators where they move with it.

Our own designs with real parts as reference only: no maker's name, logo or exact product ships.

    python3 foundry/shocks3d.py shots [keys] [--samples N]   # out/shocks/*.png
    python3 foundry/shocks3d.py check                         # every part on the lead bike of each family
"""
import json
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bars3d as BR  # noqa: E402
import detail3d as D  # noqa: E402
import lamps3d as L  # noqa: E402

B = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "shocks")

SHOCKS = {
    "piggyback": dict(label="Piggyback", fams=("Naked", "Sport", "Cafe", "Adventure", "MX", "Classic"), spring="red",
                      note="Anodised body with its gas reservoir riding alongside on a bridge, two adjuster dials and a threaded preload collar."),
    "coilover": dict(label="Polished coil-over", fams=("Naked", "Sport", "Cafe", "Classic", "Adventure", "Cruiser"), spring="yellow",
                     note="A polished alloy body with deep machined rings, a big spring and a knurled rebound knob at the foot."),
    "shroud": dict(label="Chrome shroud", fams=("Classic", "Cafe", "Cruiser"), spring="chrome",
                   note="The old style: a chrome shroud over the top of the spring, the lower coils showing below it."),
    "air": dict(label="Air ride", fams=("Cruiser", "Classic"), spring="black",
                note="A rubber air bellows in place of the spring, chrome cans top and bottom and an air line to a valve."),
}
SPRINGS = ("red", "yellow", "black", "chrome", "blue", "gold")
CAPS = {
    "aero": dict(label="Aero flush", fams=("Naked", "Sport", "Cafe", "Adventure", "MX"),
                 note="A race-style cap: a flat machined ring on the six bolts and a flush lid with a flip-up key flap."),
    "monza": dict(label="Flip-up", fams=("Cafe", "Classic", "Naked", "Cruiser", "Chopper"),
                  note="A big domed chrome lid on a sprung hinge with a thumb tab, the old racing filler."),
    "screw": dict(label="Knurled screw", fams=("Classic", "Cruiser", "Chopper", "Cafe"),
                  note="A chrome screw-on cap with a knurled edge and a little breather nipple on top."),
    "billet": dict(label="Billet", fams=("Naked", "Sport", "Cafe", "Adventure", "Chopper"),
                   note="A thick black anodised ring cut into scallops, a contrasting lid and exposed bright bolts."),
}
CAP_FINISHES = ("silver", "black", "gold", "red", "blue")
HANGERS = {
    "tidy": dict(label="Tail tidy", fams=("Naked", "Sport", "Cafe", "Adventure"),
                 note="The long plate arm goes: a short alloy bracket bolted under the tail end carries the plate and its lamp."),
    "side": dict(label="Side mount", fams=("Naked", "Cafe", "Classic", "Cruiser", "Chopper"),
                 note="The plate stands upright beside the rear tyre on a bent arm from the left axle, with a hooded lamp on top."),
}


def use(b):
    global B
    B = b
    L.use(b)


def spring_mat(f):
    if f == "chrome":
        return B.mat("chrome")
    col = dict(red=(0.5, 0.02, 0.02), yellow=(0.75, 0.52, 0.02), black=(0.015, 0.015, 0.017), blue=(0.03, 0.1, 0.45), gold=(0.42, 0.27, 0.08))[f]
    return D.surf("sk_spring_" + f, col, 0.35 if f != "gold" else 1.0, 0.3, coat=0.6, wear=0.05)


def anod(f):
    col = dict(silver=(0.62, 0.62, 0.64), black=(0.02, 0.02, 0.022), gold=(0.42, 0.27, 0.08), red=(0.45, 0.025, 0.02), blue=(0.04, 0.12, 0.42))[f]
    return D.surf("sk_" + f, col, 1.0 if f in ("silver", "gold") else 0.85, 0.28, wear=0.05)


def mats():
    return dict(alu=B.mat("alu"), steel=B.mat("steel"), chrome=B.mat("chrome"), rubber=B.mat("rubber"), dark=B.mat("alu_dark"),
                polish=D.surf("sk_polish", (0.8, 0.8, 0.82), 1.0, 0.12, wear=0.05), black=anod("black"), gold=anod("gold"),
                hose=D.surf("hose", (0.015, 0.015, 0.017), 0.0, 0.42))


# ---------------------------------------------------------------- shocks
SHOCK_PARTS = ("shocktop", "shockbody", "shockrod", "shockbump", "shockcollar", "spring")


def shock_ends(model):
    """The shock's two ends as the bike draws them: (top, bottom, radius scale) per shock."""
    from mathutils import Vector
    sk = model.get("shock")
    if not sk:
        return []
    st, sb = sk["top"], sk["bottom"]
    if not sk.get("twin"):
        return [("", Vector((st[0], 0, st[1])), Vector((sb[0], 0, sb[1])), 1.22, 0)]
    y_t, y_b = 150, 150 if sk.get("real") else 194
    return [(str(s), Vector((st[0], s * y_t, st[1])), Vector((sb[0], s * y_b, sb[1])), 1.0, s) for s in (-1, 1)]


def coil(name, a, d, n1, n2, t0, t1, R, wire, turns, mat, prog=0.45):
    pts = []
    for j in range(241):
        v = j / 240
        w = 2 * math.pi * turns * (v + prog * v * v) / (1 + prog)
        q = a + d * (t1 + (t0 - t1) * v) + n1 * (R * math.cos(w)) + n2 * (R * math.sin(w))
        pts.append(tuple(q))
    B.tube(name, pts, wire, mat)


def build_shock(key, tag, a, b, k, s, M, spring="red", body_f="gold", res=("rear", 26)):
    from mathutils import Vector
    L_ = (b - a).length
    d = (b - a) / L_
    n1 = d.cross(Vector((0, 1, 0))).normalized()
    n2 = d.cross(n1)
    at, dt = tuple(a), tuple(d)

    def P_(prof):
        return [(q[0], q[1] * k) for q in prof]
    sm = spring_mat(spring)
    nm = f"sk{tag}_"
    if key == "piggyback":
        body = anod(body_f)
        D.lathe(nm + "head", at, dt, P_([(13, 0), (13, 14), (28, 14), (32, 27), (36, 33), (46, 33), (48, 30), (48, 0)]), body, 36)
        D.lathe(nm + "body", at, dt, P_([(48, 0), (48, 21), (L_ * 0.55, 21), (L_ * 0.55 + 3, 17.5), (L_ * 0.55 + 3, 0)]), M["black"], 32)
        # threaded preload collar and lock ring high on the body
        # the spring only on the lower part (beenster, 17 Nov photo): its seat and lock ring sit on the body
        # below halfway, the coils close-wound at the top and opening out towards the bottom
        ts = L_ * (0.42 if s else 0.55)
        for j, t in enumerate((ts - 15, ts - 7)):
            D.lathe(nm + f"preload{j}", at, dt, P_([(t, 0), (t, 27), (t + 1, 28.5), (t + 6, 28.5), (t + 7, 27), (t + 7, 0)]), body, 36)
        D.lathe(nm + "rod", at, dt, P_([(L_ * 0.55, 0), (L_ * 0.55, 7.5), (L_ - 34, 7.5), (L_ - 34, 0)]), M["chrome"], 20)
        D.lathe(nm + "seat", at, dt, P_([(L_ - 62, 0), (L_ - 62, 31), (L_ - 56, 31), (L_ - 52, 16), (L_ - 34, 14), (L_ - 14, 12.5), (L_ - 14, 0)]), body, 36)
        coil(nm + "spring", a, d, n1, n2, ts + 1, L_ - 62.0, 27 * k, 4.8 * k, 5.5 if L_ > 300 else 4.5, sm, 1.6)
        # the reservoir: a short fat canister on a bridge from the head, behind the body (beenster, 17 Nov:
        # half the length, a little fatter, towards the rear, not outwards)
        # on a single shock the engine's rear cylinder sits just behind it, so there the canister goes to the right
        rear = n1 if n1.x < 0 else -n1
        side = rear if (s or res[0] == "rear") else Vector((0, res[0], 0))
        along = 26 if s else res[1]
        off = side * (48 * k)
        c0 = a + d * along + off
        rl = L_ * 0.19
        D.lathe(nm + "res", tuple(c0), dt, P_([(0, 0), (0, 19), (2, 21), (rl, 21), (rl + 2, 19), (rl + 2, 0)]), M["black"], 32)
        for j, t in enumerate((0, rl - 8)):
            D.lathe(nm + f"rescap{j}", tuple(c0), dt, P_([(t - 1, 0), (t - 1, 21.8), (t + 10, 21.8), (t + 10, 0)]), body, 32)
        D.lathe(nm + "dial0", tuple(c0 + d * (rl + 2)), dt, [(0, 0), (0, 9), (6, 9), (6, 0)], M["gold"] if body_f != "gold" else M["black"], 16)
        B.cone(nm + "bridge", tuple(a + d * (along - 4)), tuple(c0 + d * 22 - side * 10), 11 * k, 11 * k, body if along < 40 else M["black"], 20)
        B.cone(nm + "dial1", tuple(a + d * 38 - side * 33 * k), tuple(a + d * 38 - side * 41 * k), 7.5, 7.5, M["gold"] if body_f != "gold" else M["black"], 16)
        return
    if key == "coilover":
        D.lathe(nm + "head", at, dt, P_([(13, 0), (13, 14), (26, 14), (30, 25), (40, 25), (41, 29), (48, 29), (49, 25), (49, 0)]), M["polish"], 36)
        prof = [(49, 0), (49, 20)]
        t = 60.0
        while t < L_ * 0.48 - 8:
            prof += [(t, 20), (t + 1.5, 18.5), (t + 4.5, 18.5), (t + 6, 20)]
            t += 18
        prof += [(L_ * 0.48, 20), (L_ * 0.48 + 3, 16.5), (L_ * 0.48 + 3, 0)]
        D.lathe(nm + "body", at, dt, P_(prof), M["polish"], 36)
        # the spring covers only the lower part (beenster, 17 Nov): its top seat sits on the body below halfway
        ts = L_ * (0.42 if s else 0.55)
        D.lathe(nm + "preload", at, dt, P_([(ts - 8, 0), (ts - 8, 23), (ts - 6, 27), (ts, 27), (ts, 0)]), M["polish"], 36)
        D.lathe(nm + "rod", at, dt, P_([(L_ * 0.48, 0), (L_ * 0.48, 7.5), (L_ - 34, 7.5), (L_ - 34, 0)]), M["chrome"], 20)
        D.lathe(nm + "seat", at, dt, P_([(L_ - 60, 0), (L_ - 60, 32), (L_ - 54, 32), (L_ - 50, 16), (L_ - 30, 15), (L_ - 14, 13), (L_ - 14, 0)]), M["polish"], 36)
        D.lathe(nm + "knob", tuple(b - d * 24 + n1 * 14 * k), tuple(n1), [(0, 0), (0, 8), (7, 8.5), (8, 0)], M["black"], 18)
        coil(nm + "spring", a, d, n1, n2, ts + 2, L_ - 60.0, 29 * k, 5.6 * k, 5.0 if L_ > 300 else 4.0, sm, 1.2)
        return
    if key == "shroud":
        D.lathe(nm + "head", at, dt, P_([(13, 0), (13, 14), (24, 14), (26, 20), (34, 20), (34, 0)]), M["chrome"], 36)
        D.lathe(nm + "body", at, dt, P_([(34, 0), (34, 19), (L_ * 0.6, 19), (L_ * 0.6 + 3, 15), (L_ * 0.6 + 3, 0)]), M["black"], 32)
        D.lathe(nm + "rod", at, dt, P_([(L_ * 0.6, 0), (L_ * 0.6, 7), (L_ - 30, 7), (L_ - 30, 0)]), M["chrome"], 20)
        sh = L_ * 0.5
        D.lathe(nm + "shroud", at, dt, P_([(22, 0), (22, 22), (26, 31), (34, 33), (sh - 4, 33), (sh, 34.5), (sh + 2, 34.5), (sh + 2, 30), (sh, 30), (sh - 4, 30.5), (34, 30.5), (26, 28), (24, 0)]), M["chrome"], 48)
        D.lathe(nm + "seat", at, dt, P_([(L_ - 56, 0), (L_ - 56, 31), (L_ - 50, 31), (L_ - 46, 17), (L_ - 30, 14), (L_ - 14, 12), (L_ - 14, 0)]), M["chrome"], 36)
        # cam preload: three steps on the seat
        coil(nm + "spring", a, d, n1, n2, sh - 30, L_ - 56.0, 26 * k, 4.6 * k, 5.0 if L_ > 300 else 4.0, sm, 0.0)
        return
    # air ride: chrome can at the top, a rubber bellows in two convolutions, a chrome piston can below, a line to a valve
    D.lathe(nm + "head", at, dt, P_([(13, 0), (13, 14), (26, 14), (30, 30), (40, 34), (L_ * 0.28, 34), (L_ * 0.28 + 3, 31), (L_ * 0.28 + 3, 0)]), M["chrome"], 40)
    t0, t1 = L_ * 0.28, L_ * 0.66
    prof = [(t0, 0), (t0, 30)]
    for j in range(25):
        v = j / 24
        prof.append((t0 + (t1 - t0) * v, 31 + 6 * abs(math.sin(2 * math.pi * v))))
    prof += [(t1, 30), (t1, 0)]
    D.lathe(nm + "bellows", at, dt, P_(prof), M["rubber"], 40, sharp=False)
    D.lathe(nm + "piston", at, dt, P_([(t1 - 6, 0), (t1 - 6, 27), (L_ - 40, 27), (L_ - 34, 22), (L_ - 14, 13), (L_ - 14, 0)]), M["chrome"], 36)
    D.lathe(nm + "rod", at, dt, P_([(L_ * 0.5, 0), (L_ * 0.5, 7), (L_ - 30, 7), (L_ - 30, 0)]), M["chrome"], 20)
    side = Vector((0, s, 0)) if s else n1
    p0 = a + d * 22 + side * 30 * k
    p1 = a + d * 6 + side * 46 * k - n1 * 30
    B.tube(nm + "line", [tuple(p0), tuple(a + d * 14 + side * 42 * k), tuple(p1)], 3.0, M["hose"])
    D.lathe(nm + "fitting", tuple(a + d * 22 + side * 26 * k), tuple(side), [(0, 0), (0, 5), (8, 5), (8, 0)], M["steel"], 12)


# ---------------------------------------------------------------- filler caps
CAP_PARTS = ("capneck", "capring", "caplid", "capbolt", "capkey", "caphinge", "caphingepin")


def build_cap(key, i, o, M, finish="silver"):
    nm = f"skcap{i}_"
    steel, chrome = M["steel"], M["chrome"]
    up = (0, 0, 1)
    X = (o[0], o[1], o[2])

    def at(dx, dy, dz):
        return (o[0] + dx, o[1] + dy, o[2] + dz)

    def ringbolts(r=39.5, z=13.6, rr=2.6, h=1.8, mat=None, socket=True):
        for k in range(6):
            w = math.radians(60 * k + 30)
            D.bolt(f"{nm}bolt{k}", at(r * math.cos(w), r * math.sin(w), z), up, rr, h, mat or steel, socket)
    if key == "aero":
        ring = anod(finish)
        D.lathe(nm + "neck", X, up, [(-10, 0), (-10, 48), (4, 48), (9, 45), (9, 0)], M["dark"], 56)
        D.lathe(nm + "ring", X, up, [(9, 0), (9, 46), (12.5, 46), (13.6, 44.5), (13.6, 32), (12.6, 31), (12.6, 0)], ring, 64)
        D.lathe(nm + "lid", X, up, [(11, 0), (11, 30.5), (13.4, 30.5), (14.2, 29), (14.2, 0)], M["polish"], 56)
        B.prism(nm + "flap", [(o[0] - 6, o[2] + 14.0), (o[0] + 22, o[2] + 14.0), (o[0] + 22, o[2] + 15.6), (o[0] - 6, o[2] + 15.6)], -9, 9, ring, 0.8)
        B.cone(nm + "key", at(14, 0, 13.8), at(14, 0, 14.3), 5, 5, M["rubber"], 20)
        ringbolts()
        for k in range(12):
            w = math.radians(30 * k)
            B.cone(f"{nm}vent{k}", at(23 * math.cos(w), 23 * math.sin(w), 14.0), at(23 * math.cos(w), 23 * math.sin(w), 14.4), 1.6, 1.6, M["rubber"], 8)
        return
    if key == "monza":
        D.lathe(nm + "neck", X, up, [(-10, 0), (-10, 48), (4, 48), (9, 45), (9, 0)], chrome, 56)
        D.lathe(nm + "ring", X, up, [(9, 0), (9, 44), (12, 44), (14, 41.5), (14, 36), (12.5, 35), (12.5, 0)], chrome, 56)
        ringbolts(mat=chrome, socket=False)
        # a big domed lid, hinged at the back, a thumb tab at the front
        D.lathe(nm + "lid", at(0, 0, 0), up, [(12.5, 0), (12.5, 36), (15, 37), (19, 35), (24, 28), (27, 16), (28, 0)], chrome, 64, sharp=False)
        B.prism(nm + "tab", [(o[0] + 34, o[2] + 15), (o[0] + 48, o[2] + 19), (o[0] + 48, o[2] + 22), (o[0] + 32, o[2] + 19)], -9, 9, chrome, 1.0)
        B.prism(nm + "hinge", [(o[0] - 44, o[2] + 12), (o[0] - 32, o[2] + 12), (o[0] - 32, o[2] + 21), (o[0] - 44, o[2] + 21)], -12, 12, chrome, 1.5)
        B.tube(nm + "hingepin", [at(-38, -14, 17), at(-38, 14, 17)], 2.4, steel)
        return
    if key == "screw":
        D.lathe(nm + "neck", X, up, [(-10, 0), (-10, 40), (4, 40), (9, 36), (12, 30), (12, 0)], chrome, 56)
        prof = [(12, 0), (12, 31)]
        for j in range(5):
            prof += [(13 + 2 * j, 31), (14 + 2 * j, 32.4)]
        prof += [(22, 32.4), (25, 30), (28, 22), (29, 12), (29, 0)]
        D.lathe(nm + "cap", X, up, prof, chrome, 72, sharp=False)
        B.cone(nm + "breather", at(0, 0, 29), at(0, 0, 36), 3.5, 2.5, chrome, 16)
        B.cone(nm + "breathertip", at(0, 0, 36), at(0, 0, 38), 1.8, 1.6, M["rubber"], 12)
        for k in range(4):
            w = math.radians(90 * k + 45)
            D.bolt(f"{nm}bolt{k}", at(35 * math.cos(w), 35 * math.sin(w), 9), up, 2.6, 1.8, chrome, False)
        return
    # billet: a thick black ring cut into scallops round the bolts, a coloured lid, bright bolts
    ring = M["black"]
    lid = anod(finish if finish != "black" else "silver")
    D.lathe(nm + "neck", X, up, [(-10, 0), (-10, 48), (4, 48), (9, 45), (9, 0)], M["dark"], 56)
    outline = []
    for j in range(240):
        w = 2 * math.pi * j / 240
        r = 44 + 3.5 * math.cos(6 * (w - math.radians(30)))
        outline.append((r * math.cos(w), r * math.sin(w)))
    D.plan_plate(nm + "ring", [(o[0] + x, o[1] + y) for x, y in outline], o[2] + 9, o[2] + 17, ring, 1.2)
    D.lathe(nm + "lid", X, up, [(15, 0), (15, 30), (18, 30), (20.5, 27.5), (21, 0)], lid, 56)
    D.lathe(nm + "lidcentre", X, up, [(20.5, 0), (20.5, 12), (21.6, 11), (21.6, 0)], M["polish"], 40)
    ringbolts(r=39.5, z=17, rr=3.0, h=2.2, mat=M["polish"])


# ---------------------------------------------------------------- plate hangers
def _verts(ob):
    import numpy as np
    dg = bpy.context.evaluated_depsgraph_get()
    me = ob.evaluated_get(dg).to_mesh()
    v = np.array([tuple(ob.matrix_world @ p.co) for p in me.vertices]) / B.MM
    ob.evaluated_get(dg).to_mesh_clear()
    return v


def _rect(c, u, v, hu, hv):
    """A rectangle in the side view... in 3D: centre c, half sizes along unit vectors u and v."""
    return [tuple(c[i] + u[i] * su * hu + v[i] * sv * hv for i in range(3)) for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1))]


def box(name, c, u, v, w, hu, hv, ht, mat, bevel=0.6):
    """A plate: centre c (3D), its face spanned by unit u and v, thickness along w."""
    verts = []
    for sw in (-1, 1):
        verts += [tuple(c[i] + w[i] * sw * ht + u[i] * su * hu + v[i] * sv * hv for i in range(3)) for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    verts = [B.V(*q) for q in verts]
    faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    ob = B.mesh_obj(name, verts, faces, mat, False, bevel)
    B.bm_fix_normals(ob)
    return ob


def plate_parts():
    plate = next((o for o in bpy.data.objects if o.name.startswith("behind") and o.active_material and o.active_material.name == "plate"), None)
    return plate


def drop_hanger():
    """The bike's own plate, its lamp and the arm that carried them (only the arm behind the axle)."""
    tail_x = None
    for o in list(bpy.data.objects):
        n = o.name
        if n.startswith("cut_"):
            continue
        if n.startswith(("platelamp", "platemount")):
            bpy.data.objects.remove(o, do_unlink=True)
            continue
        if n.startswith("behind"):
            v = _verts(o)
            mt = o.active_material.name if o.active_material else ""
            if mt == "plate" or (mt == "plastic" and v[:, 1].max() - v[:, 1].min() <= 60):
                bpy.data.objects.remove(o, do_unlink=True)
            continue
        if n.startswith("platearm"):
            v = _verts(o)
            if v[:, 0].min() < 0:
                bpy.data.objects.remove(o, do_unlink=True)


def build_hanger(key, model, M, plate_mat, finish="black"):
    import numpy as np
    tyre = bpy.data.objects.get("rear_tyre")
    if tyre is None:
        return False
    tv = _verts(tyre)
    A = ((tv[:, 0].min() + tv[:, 0].max()) / 2, (tv[:, 2].min() + tv[:, 2].max()) / 2)
    Rt = (tv[:, 0].max() - tv[:, 0].min()) / 2
    arm_m = M["chrome"] if finish == "chrome" else M["black"]
    lens = bpy.data.materials.get("redlens") or M["rubber"]
    if key == "tidy":
        tail = bpy.data.objects.get("tail")
        if tail is None:
            return False
        v = _verts(tail)
        xt = v[:, 0].min()
        rear = v[v[:, 0] < xt + 90]
        zu = rear[:, 2].min()
        # the bracket: a bent alloy plate from under the tail end, down and back to the plate's top edge
        tilt = math.radians(14)
        drop_ = 40.0
        for back in range(0, 160, 8):
            top = (xt - 4 - back, zu - drop_)
            u = (math.sin(tilt), 0, -math.cos(tilt))        # down the plate face
            pc = (top[0] + u[0] * 62, 0, top[1] + u[2] * 62)
            corners = [(pc[0] + u[0] * sv * 60, pc[2] + u[2] * sv * 60) for sv in (-1, 1)]
            if all(math.hypot(x - A[0], z - A[1]) > Rt + 30 for x, z in corners):
                break
        f0 = (xt + 70, zu + 1)
        bend = (xt + 6 - back * 0.3, zu - 8)
        for k_, (p0, p1) in enumerate(((f0, bend), (bend, (top[0] + 6, top[1] + 4)))):
            Lb = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
            n = (-(p1[1] - p0[1]) / Lb * 2.5, (p1[0] - p0[0]) / Lb * 2.5)
            B.prism(f"skph_bracket{k_}", [(p0[0] + n[0], p0[1] + n[1]), (p1[0] + n[0], p1[1] + n[1]), (p1[0] - n[0], p1[1] - n[1]), (p0[0] - n[0], p0[1] - n[1])], -42, 42, arm_m, 1.0)
        for y in (-28, 28):
            D.bolt(f"skph_bolt{y}", (xt + 50, y, zu - 1.5), (0, 0, -1), 4.5, 3.5, M["steel"])
        w = (-math.cos(tilt), 0, -math.sin(tilt))            # the plate faces back
        box("skph_plate", pc, (0, 1, 0), u, w, 85, 60, 1.5, plate_mat, 0.8)
        box("skph_back", tuple(pc[i] - w[i] * 3 for i in range(3)), (0, 1, 0), u, w, 87, 62, 1.5, arm_m, 0.6)
        # the lamp: a slim bar over the plate's top edge shining down on it
        lc = (top[0] + 6 + w[0] * 8, 0, top[1] + 6)
        box("skph_lamp", lc, (0, 1, 0), (1, 0, 0), (0, 0, 1), 34, 9, 6, arm_m, 1.5)
        box("skph_lamplens", (lc[0] + w[0] * 9.2, 0, lc[2] - 3), (0, 1, 0), (0, 0, 1), (1, 0, 0), 28, 3, 0.8, lens, 0.3)
        return True
    # side mount: an arm from the left axle end, the plate upright beside the tyre facing back
    ax = (A[0], 0, A[1])
    y0 = 188.0
    yc = 215.0
    pc = (A[0] - 150, yc, A[1] + 95)
    knee = (A[0] - 95, y0 + 22, A[1] + 30)
    B.cone("skph_boss", (ax[0], y0 - 8, ax[2]), (ax[0], y0 + 8, ax[2]), 16, 16, arm_m, 24)
    D.bolt("skph_axlenut", (ax[0], y0 + 8, ax[2]), (0, 1, 0), 11, 7, M["steel"], False)
    B.tube("skph_arm", [(ax[0], y0, ax[2]), (ax[0] - 40, y0 + 6, ax[2] + 8), knee, (pc[0] + 14, yc - 30, pc[2] - 30), (pc[0] + 10, yc - 20, pc[2] - 4)], 7, arm_m)
    w = (-1, 0, 0)
    box("skph_back", (pc[0] + 4, pc[1], pc[2]), (0, 1, 0), (0, 0, 1), w, 78, 56, 2.0, arm_m, 0.8)
    box("skph_plate", (pc[0] - 0.5, pc[1], pc[2]), (0, 1, 0), (0, 0, 1), w, 75, 53, 1.2, plate_mat, 0.6)
    lc = (pc[0] - 4, pc[1], pc[2] + 62)
    D.lathe("skph_lamp", (lc[0] + 14, lc[1], lc[2]), (-1, 0, 0), [(0, 0), (0, 15), (20, 15), (26, 9), (26, 0)], arm_m, 24)
    box("skph_lamplens", (lc[0] + 10, lc[1], lc[2] - 13), (0, 1, 0), (1, 0, 0), (0, 0, 1), 10, 8, 1.0, lens, 0.3)
    return True


def crown(x0, z0, reach=140.0):
    """Where a cap should stand: if the tank's cap point is on a slope (beenster, 17 Nov: not down the slope,
    near the top), the highest point of the tank's centre line within reach of it; otherwise the point itself."""
    tank = next((o for o in bpy.data.objects if o.name.startswith("tank") and o.type == "MESH" and not o.name.startswith("tankbadge")), None)
    if tank is None:
        return x0, z0
    v = _verts(tank)
    v = v[(abs(v[:, 1]) < 12) & (abs(v[:, 0] - x0) < reach)]
    if not len(v):
        return x0, z0
    top = v[v[:, 2].argmax()]
    near = v[abs(v[:, 0] - x0) < 12]
    here = near[:, 2].max() if len(near) else z0
    if top[2] - here < 12:
        return x0, z0
    # stand just behind the very top, where the crown is flat enough
    flat = v[v[:, 2] > top[2] - 4]
    x = float(flat[:, 0].mean())
    near = v[abs(v[:, 0] - x) < 12]
    return x, float(near[:, 2].max()) if len(near) else float(top[2])


# ---------------------------------------------------------------- fitting
def fit(b, model, c, shock=None, spring=None, body=None, cap=None, cap_finish=None, hanger=None, hanger_finish=None):
    use(b)
    M = mats()
    fam = model["family"]
    before = set(bpy.data.objects.keys())
    rep = dict(model=model.get("name", ""), family=fam)
    ends = shock_ends(model)
    if shock and ends and fam in SHOCKS[shock]["fams"]:
        for nm in SHOCK_PARTS:
            L._remove((nm,))
        sp = spring or SHOCKS[shock]["spring"]
        for tag, a, b_, k, s in ends:
            if shock == "piggyback" and not s:
                # a single shock sits between the engine and the subframe: the canister goes to the side with room
                best = None
                for ry in [(sd, al) for al in (26, L_ * 0.3, L_ * 0.45) for sd in ("rear", -1, 1)
                           for L_ in [(b_ - a).length]]:
                    pre = set(bpy.data.objects.keys())
                    build_shock(shock, tag, a, b_, k, s, M, sp, body or "gold", ry)
                    new = [o for o in bpy.data.objects.keys() if o not in pre]
                    hit = sum(v for kk, v in BR.clip_check([n for n in new if n.startswith(("sk_res", "sk_bridge", "sk_dial"))]).items()
                              if not ok_pair(kk))
                    for n in new:
                        bpy.data.objects.remove(bpy.data.objects[n], do_unlink=True)
                    if best is None or hit < best[0]:
                        best = (hit, ry)
                build_shock(shock, tag, a, b_, k, s, M, sp, body or "gold", best[1])
                continue
            build_shock(shock, tag, a, b_, k, s, M, sp, body or "gold")
        rep["shock"], rep["spring"] = shock, sp
    caps = model["body"].get("caps", [])
    if cap and caps and fam in CAPS[cap]["fams"]:
        L._remove(CAP_PARTS)
        for i, cp in enumerate(caps):
            # teardrop tanks only: sport, adventure and MX tanks keep the cap where the tank puts it
            x, z = crown(cp["at"][0], cp["at"][1]) if fam in ("Classic", "Cafe", "Cruiser", "Chopper", "Naked") else (cp["at"][0], cp["at"][1])
            build_cap(cap, i, (x, 0, z - 4), M, cap_finish or "silver")
            if abs(x - cp["at"][0]) > 1:
                rep["cap_moved"] = round(x - cp["at"][0])
        rep["cap"] = cap
    if hanger and fam in HANGERS[hanger]["fams"]:
        old = plate_parts()
        pm = old.active_material if old is not None else None
        if pm is None:
            pm = bpy.data.materials.get("plate") or D.surf("sk_plate", (0.85, 0.7, 0.05), 0.0, 0.35)
        drop_hanger()
        fin = hanger_finish or ("chrome" if fam in ("Cruiser", "Chopper", "Classic") else "black")
        if build_hanger(hanger, model, M, pm, fin):
            rep["hanger"] = hanger
    made = [o for o in bpy.data.objects.keys() if o not in before and not o.startswith("cut_")]
    return rep, made


def built(mn, **kw):
    import build3d
    b3, m, cc, r, made0 = L.built(mn)
    use(build3d)
    rep, made = fit(build3d, m, cc, **kw)
    return build3d, m, cc, rep, made


# ---------------------------------------------------------------- pictures and the check
SHOTS = [
    ("shock", "piggyback", "hartwell_cafe", dict(spring="red", body="gold")),
    ("shock", "coilover", "hartwell_classic", dict(spring="yellow")),
    ("shock", "shroud", "lodestar_cruiser", dict(spring="chrome")),
    ("shock", "air", "lodestar_cruiser", dict()),
    ("cap", "aero", "kazeda_sport", dict(cap_finish="silver")),
    ("cap", "monza", "hartwell_cafe", dict()),
    ("cap", "screw", "lodestar_chopper", dict()),
    ("cap", "billet", "corvano_naked", dict(cap_finish="red")),
    ("hanger", "tidy", "kazeda_sport", dict()),
    ("hanger", "side", "lodestar_chopper", dict()),
]


def shots(which=None, samples=40):
    os.makedirs(OUT, exist_ok=True)
    rep = {}
    for kind, key, mn, other in SHOTS:
        if which and key not in which:
            continue
        kw = dict(other)
        kw[kind] = key
        b3, m, cc, r, made = built(mn, **kw)
        rep[f"{kind}_{key}"] = r
        if kind == "shock":
            c_ = L._centre(("sk1_", "sk_"))
            if not c_:
                print("NO PART", kind, key, mn)
                continue
            ctr, lo, hi = c_
            twin = bool(model_twin(m))
            tgt = (ctr[0], ctr[1], ctr[2])
            loc = BR.vadd(tgt, (420, 900 if not twin else 700, 160))
            lens = 50 if twin else 60
        elif kind == "hanger":
            c_ = L._centre(("skph_plate",))
            if not c_:
                print("NO PART", kind, key, mn)
                continue
            ctr, lo, hi = c_
            tgt = (ctr[0] + 80, ctr[1] * 0.6, ctr[2] - 20)
            loc = BR.vadd(tgt, (-900, 760, 260))
            lens = 45
        else:
            c_ = L._centre(("skcap0_",))
            if not c_:
                print("NO PART", kind, key, mn)
                continue
            ctr, lo, hi = c_
            tgt = ctr
            loc = BR.vadd(tgt, (-240, 270, 300))
            lens = 70
        L.render_at(b3, os.path.join(OUT, f"{kind}_{key}.png"), loc, tgt, lens, res=(1100, 800), samples=samples)
    path = os.path.join(OUT, "shots.json")
    old = json.load(open(path)) if os.path.exists(path) else {}
    old.update(rep)
    json.dump(old, open(path, "w"), indent=1)


def model_twin(m):
    return (m.get("shock") or {}).get("twin")


OK = ("sk | sk", "sk | shockeye", "sk | shockstud", "sk | shocklug", "sk | shockbolt", "sk | shocknut", "sk | shockhead", "sk | shockbridge",
      "sk | shockmount", "skcap | tank", "skcap | skcap", "skcap | badge", "skph | skph", "skph | tail")


def ok_pair(pair):
    a, b_ = [x.strip() for x in pair.split("|")]
    for j in OK:
        ja, jb = [x.strip() for x in j.split("|")]
        if (a.startswith(ja) and b_.startswith(jb)) or (b_.startswith(ja) and a.startswith(jb)):
            return True
    return False


def check(keys=None):
    res = {}
    leads = dict(BR.LEADS)
    leads["Chopper"] = "lodestar_chopper"
    jobs = [("shock", k, h["fams"]) for k, h in SHOCKS.items()] + [("cap", k, h["fams"]) for k, h in CAPS.items()]
    jobs += [("hanger", k, h["fams"]) for k, h in HANGERS.items()]
    for fam, mn in leads.items():
        for kind, key, fams in jobs:
            if fam not in fams or (keys and key not in keys):
                continue
            import build3d
            b3, m, cc, r0, made0 = L.built(mn)
            # the bike's own shock already sits against some parts (engine covers, panels): those parts
            # are judged against how deep the bike's own shock went, not against zero
            own = {}
            for pr, dep in BR.clip_check([o.name for o in bpy.data.objects if o.name.startswith(SHOCK_PARTS)]).items():
                for nm_ in pr.split(" | "):
                    if not nm_.strip().startswith(("shock", "spring")):
                        own[nm_.strip()] = max(own.get(nm_.strip(), 0), dep)
            use(build3d)
            r, made = fit(build3d, m, cc, **{kind: key})
            bad = {}
            for k_, v_ in BR.clip_check(made).items():
                other = [x.strip() for x in k_.split(" | ") if not x.strip().startswith("sk")]
                if ok_pair(k_) or (other and other[0] in own and v_ <= own[other[0]] + 6):
                    continue
                bad[k_] = v_
            res.setdefault(kind, {}).setdefault(key, {})[fam] = dict(model=mn, clips=bad, fitted=kind in r, rep=r)
            print(f"SCHECK {kind:6s} {key:10s} {fam:10s} fitted={kind in r} {bad}", flush=True)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "check.json")
    old = json.load(open(path)) if (keys and os.path.exists(path)) else {}
    for kind, v in res.items():
        for k, per in v.items():
            old.setdefault(kind, {}).setdefault(k, {}).update(per)
    json.dump(old, open(path, "w"), indent=1)


if __name__ == "__main__":
    a = sys.argv[1:]
    cmd = a[0] if a else "shots"
    smp = int(a[a.index("--samples") + 1]) if "--samples" in a else 40
    rest = [x for x in a[1:] if not x.startswith("--") and x != str(smp)]
    if cmd == "shots":
        shots(rest or None, smp)
    elif cmd == "check":
        check(rest or None)
