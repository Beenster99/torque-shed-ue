"""Common parts: side stands for the V2 bikes (beenster's build 154 test, 17 Nov: "we also need to add side stands").

Split agreed with Bike Models V2 (17 Nov): this file models and fits the stand; build3d calls fit(), exports the
stand as its own moving group "side_stand" and writes the pivot data below into the bike json, and the game swings
the stand down about that pivot when the bike is parked (TorqueShedBike::ApplyPose / TorqueBikePawn::PoseStands).

The stand stands on the left (rider's left, +y), on a lug under the engine a little ahead of the swingarm pivot,
where the old game bikes had theirs (16 cm ahead of the swingarm pivot). It is drawn FOLDED, tucked back under the
chain line; down, it is swung about one axis through its pivot until the foot meets the floor with the bike leaned
LEAN degrees to the left, the lean the game parks at. The foot pad is drawn flat on the floor in that pose.

Objects: sidestand_* swing with the stand (leg, foot, boss, tang, spring); ssmount_* is the lug on the bike and
does not move (map it to the frame).

    fit(b, model, c, style=None) -> (rep, made)
        rep["stand"] = dict(pivot=[x, y, z] mm, axis=[unit], angle_down_deg, folded_dir, down_dir, foot_down=[mm],
                            length_mm, lean_deg) in the bike frame: x forward from the rear axle line, y to the
                            rider's left, z up from the ground. Swinging the sidestand_* objects by angle_down_deg
                            about axis through pivot (right-hand rule) takes them from folded to down.

    python3 foundry/sidestand3d.py check [models]          # folded and down on each family's lead (or the models)
    python3 foundry/sidestand3d.py shots [--samples N]      # out/sidestand/*.png
"""
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bars3d as BR  # noqa: E402
import detail3d as D  # noqa: E402
import lamps3d as L  # noqa: E402

B = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "sidestand")
LEAN = 12.0           # degrees the bike leans onto its stand when parked (the game's default "lean")
MX_PEG = (600, 404)

# per family: leg radius at the pivot / at the foot, foot pad radius and shape, finish, tang
STYLES = {
    "sport": dict(label="Forged sport", r0=11.0, r1=8.0, pad=24.0, pad_k=(1.0, 0.8), fin="black", tang="rubber"),
    "street": dict(label="Pressed street", r0=12.0, r1=9.0, pad=27.0, pad_k=(1.0, 0.85), fin="black", tang="steel"),
    "classic": dict(label="Chrome classic", r0=12.0, r1=9.5, pad=26.0, pad_k=(1.0, 1.0), fin="chrome", tang="steel", dog=True),
    "adventure": dict(label="Wide-foot adventure", r0=13.0, r1=10.0, pad=40.0, pad_k=(1.25, 0.9), fin="black", tang="steel"),
    "mx": dict(label="Slim off-road", r0=10.0, r1=7.5, pad=20.0, pad_k=(1.0, 0.8), fin="silver", tang=None),
}
FAMILY_STYLE = {"Sport": "sport", "Naked": "street", "Cafe": "street", "Classic": "classic", "Cruiser": "classic",
                "Chopper": "classic", "Adventure": "adventure", "MX": "mx"}

# where the lug is meant to sit inside the bike (it bolts to the engine cases or the bottom rail)
OK = ("ssmount | crankcase", "ssmount | sidecase", "ssmount | frame", "ssmount | sidestand", "ssmount | altcover",
      "ssmount | casebolt", "ssmount | sump", "ssmount | rail", "ssmount | cradle", "ssmount | downtube", "ssmount | engine")
SKIP_INNER = ("chain", "sprocket", "peg", "gear", "swingarm", "rear_", "front_", "header", "can", "brake", "heel",
              "hanger", "sidestand", "ssmount", "shock", "cut_", "svc_")


def use(b):
    global B
    B = b
    L.use(b)


def mats():
    return dict(black=D.surf("ss_black", (0.025, 0.025, 0.028), 0.8, 0.42, wear=0.08), chrome=B.mat("chrome"),
                silver=D.surf("ss_silver", (0.62, 0.63, 0.64), 1.0, 0.3, wear=0.05), steel=B.mat("steel"),
                rubber=B.mat("rubber"), spring=D.surf("ss_spring", (0.05, 0.05, 0.055), 0.9, 0.35, wear=0.0))


def _v(p):
    return Vector((float(p[0]), float(p[1]), float(p[2])))


def _verts(dg, ob):
    ev = ob.evaluated_get(dg)
    me = ev.to_mesh()
    mw = ev.matrix_world
    out = [mw @ v.co for v in me.vertices]
    ev.to_mesh_clear()
    return out


def case_y(x, z):
    """How far out the bike's engine or frame stands at (x, z) on the left: the lug reaches in to there."""
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    best = None
    for ob in bpy.data.objects:
        if ob.type not in ("MESH", "CURVE") or ob.name.startswith(SKIP_INNER):
            continue
        lo = [min(c[i] for c in ob.bound_box) for i in range(3)]
        if ob.type == "MESH" and ob.matrix_world == Matrix.Identity(4) and lo[2] * 1000 > z + 80:
            continue
        for w in _verts(dg, ob):
            X, Y, Z = w.x * 1000, w.y * 1000, w.z * 1000
            if abs(X - x) < 35 and z - 25 < Z < z + 60 and 15 < Y < 200:
                best = Y if best is None else max(best, Y)
    return best


def ground_normal():
    """The floor's up, in the bike's frame, with the bike leaned LEAN degrees to its left (+y)."""
    a = math.radians(LEAN)
    return Vector((0.0, -math.sin(a), math.cos(a)))


def leg_length(P, d):
    """How long the leg must be for its end to touch the floor with the bike leaned (pivot P, leg direction d)."""
    n = ground_normal()
    return -P.dot(n) / d.dot(n)


def plan(model, dx=120.0, dz=95.0, rise=6.0, fwd=0.30, out=0.34):
    """The stand's pivot, swing axis and its folded and down directions for a bike."""
    sp = model["swingarm"]["pivot"]
    pegs = [tuple(s_["at"]) for s_ in model.get("hangers", []) if s_.get("circle")] or [MX_PEG]
    x = sp[0] + dx
    z = min(sp[1], min(p[1] for p in pegs)) - dz
    z = max(z, model["geometry"].get("ground_clearance", 150) + 25)
    yc = case_y(x, z)
    y = (yc if yc is not None else 120.0) + 22.0
    P = Vector((x, y, z))
    down = Vector((fwd, out, -1.0)).normalized()
    r = math.radians(rise)
    folded = Vector((-math.cos(r), 0.06, math.sin(r))).normalized()
    axis = folded.cross(down).normalized()
    ang = math.degrees(folded.angle(down))
    return dict(P=P, down=down, folded=folded, axis=axis, angle=ang, case_y=yc, L=leg_length(P, down))


def build_down(st, nm, pl, M, fam):
    """The stand as it stands on the floor (down pose)."""
    P, d, a = pl["P"], pl["down"], pl["axis"]
    if a.y < 0:
        a = -a
    Lg = pl["L"]
    fin = M[st["fin"]]
    n = ground_normal()
    foot = P + d * Lg
    pad_t = 6.0
    # the leg: tapered round section; the classic one has a dog-leg kick just above the foot
    if st.get("dog"):
        k = foot - d * 70 + Vector((22, 0, 0))
        D.lathe(nm + "leg", tuple(P), tuple(d), [(0, 0), (0, st["r0"]), ((k - P).length * 0.98, (st["r0"] + st["r1"]) / 2), ((k - P).length, 0)], fin, 28)
        dk = (foot + n * pad_t - k).normalized()
        D.lathe(nm + "legtoe", tuple(k), tuple(dk), [(-6, 0), (-6, (st["r0"] + st["r1"]) / 2), ((foot + n * pad_t - k).length, st["r1"]),
                                                    ((foot + n * pad_t - k).length, 0)], fin, 28)
        D.lathe(nm + "legknee", tuple(k - dk * 8), tuple(dk), [(0, 0), (0, (st["r0"] + st["r1"]) / 2 + 1.5), (12, (st["r0"] + st["r1"]) / 2 + 1.5), (12, 0)], fin, 28)
    else:
        D.lathe(nm + "leg", tuple(P), tuple(d), [(0, 0), (0, st["r0"]), (Lg * 0.55, st["r0"] * 0.92), (Lg - pad_t, st["r1"]), (Lg - pad_t, 0)],
                fin, 28, ell=(1.0, 0.72))
    # the foot pad, flat on the floor
    D.lathe(nm + "foot", tuple(foot), tuple(n), [(0, 0), (0, st["pad"] - 2), (1.5, st["pad"]), (pad_t - 1.5, st["pad"]), (pad_t, st["pad"] - 2), (pad_t, 0)],
            fin, 36, ell=st["pad_k"])
    D.lathe(nm + "footboss", tuple(foot + n * (pad_t - 1)), tuple(n), [(0, 0), (0, st["r1"] + 5), (8, st["r1"] + 1.5), (8, 0)], fin, 28)
    # the pivot boss round the pin, the pin's head on the outside and a nut inside
    D.lathe(nm + "boss", tuple(P), tuple(a), [(-15, 0), (-15, 14), (-13, 15.5), (13, 15.5), (15, 14), (15, 0)], fin, 32)
    D.bolt(nm + "pin", tuple(P + a * 15), tuple(a), 9.0, 5.0, M["steel"], False)
    # the tang the rider's boot flicks it down with: out from the leg near the pivot, forward when folded
    if st.get("tang"):
        t0 = P + d * 34
        td = (a * 0.85 + d.cross(a).normalized() * -0.35).normalized()
        B.cone(nm + "tang", tuple(t0), tuple(t0 + td * 48), 6.5, 5.0, fin, 16)
        tip = M["rubber"] if st["tang"] == "rubber" else fin
        B.cone(nm + "tangtip", tuple(t0 + td * 44), tuple(t0 + td * 62), 8.0, 7.0, tip, 20)
    # the return spring: a tight coil from a tab on the leg to the hook on the boss collar
    s0 = P + d * 58 + a * -6
    s1 = P + a * -11 + d.cross(a).normalized() * 22
    ax = (s1 - s0)
    Ls = ax.length
    ax.normalize()
    u = ax.cross(Vector((0, 0, 1)))
    u = (u if u.length > 1e-3 else ax.cross(Vector((1, 0, 0)))).normalized()
    v = ax.cross(u)
    pts = []
    turns = 9
    for j in range(181):
        t = j / 180
        w = 2 * math.pi * turns * t
        q = s0 + ax * (Ls * t) + u * (4.2 * math.cos(w)) + v * (4.2 * math.sin(w))
        pts.append(tuple(q))
    B.tube(nm + "spring", pts, 1.1, M["spring"])
    return foot


def build_mount(nm, pl, M, fam):
    """The lug on the bike the pin goes through: from the inner face of the boss in to the engine case or rail."""
    P, a = pl["P"], pl["axis"]
    if a.y < 0:
        a = -a
    yin = (pl["case_y"] - 6.0) if pl["case_y"] is not None else P.y - 40
    inner = P - a * 15
    q = Vector((P.x + 6, yin, P.z + 14))
    B.cone(nm + "lug", tuple(inner + a * 1.0), tuple(q), 15.0, 17.0, M["black"] if fam != "Chopper" else M["chrome"], 28)
    D.bolt(nm + "nut", tuple(inner - a * 0.5), tuple(-a), 8.0, 6.0, M["steel"], False)


def swing(names, pl, deg):
    """Turn the stand's swinging parts by deg about its pivot axis."""
    P = pl["P"] * B.MM
    a = pl["axis"]
    R = Matrix.Translation(P) @ Matrix.Rotation(math.radians(deg), 4, a) @ Matrix.Translation(-P)
    for n in names:
        ob = bpy.data.objects.get(n)
        if ob is not None:
            ob.matrix_world = R @ ob.matrix_world
    bpy.context.view_layer.update()


def _ok(pair):
    a, b_ = [x.strip() for x in pair.split("|")]
    for j in OK:
        ja, jb = [x.strip() for x in j.split("|")]
        if (a.startswith(ja) and b_.startswith(jb)) or (b_.startswith(ja) and a.startswith(jb)):
            return True
    return a.startswith("sidestand") and b_.startswith("sidestand")


def _clash(made):
    return {k: v for k, v in BR.clip_check(made).items() if not _ok(k)}


def _remove(names):
    for n in names:
        ob = bpy.data.objects.get(n)
        if ob is not None:
            bpy.data.objects.remove(ob, do_unlink=True)


def _one(model, st, M, fam, pl):
    before = set(bpy.data.objects.keys())
    build_down(st, "sidestand_", pl, M, fam)
    sw = [o for o in bpy.data.objects.keys() if o not in before]
    build_mount("ssmount_", pl, M, fam)
    made = [o for o in bpy.data.objects.keys() if o not in before]
    down = _clash(made)
    swing(sw, pl, -pl["angle"])            # drawn folded
    folded = _clash(made)
    return made, sw, down, folded


def fit(b, model, c=None, style=None):
    """Fit a side stand to a built bike. Tries a few pivot places and keeps the one that touches least, folded
    and down. Returns (rep, made); rep["stand"] is the data the game swings it with."""
    use(b)
    M = mats()
    fam = model["family"]
    st = STYLES[style or FAMILY_STYLE.get(fam, "street")]
    rep = dict(model=model.get("name", ""), family=fam, style=style or FAMILY_STYLE.get(fam, "street"))
    best = None
    for dx, dz, rise in ((120, 95, 6), (150, 95, 6), (90, 95, 8), (120, 75, 4), (150, 115, 8), (180, 95, 6), (60, 95, 10)):
        pl = plan(model, dx, dz, rise)
        if not 150 < pl["L"] < 420:
            continue
        made, sw, down, folded = _one(model, st, M, fam, pl)
        score = sum(down.values()) + sum(folded.values())
        if best is None or score < best[0]:
            if best is not None:
                _remove(best[2])
            best = (score, pl, made, down, folded)
        else:
            _remove(made)
        if score == 0:
            break
    if best is None:
        rep["fitted"] = False
        return rep, []
    score, pl, made, down, folded = best
    a = pl["axis"] if pl["axis"].y >= 0 else -pl["axis"]
    ang = pl["angle"] if pl["axis"].y >= 0 else -pl["angle"]
    foot = pl["P"] + pl["down"] * pl["L"]
    rep.update(fitted=True, clash_down=down, clash_folded=folded, stand=dict(
        pivot=[round(v, 1) for v in pl["P"]], axis=[round(v, 4) for v in a], angle_down_deg=round(ang, 2),
        folded_dir=[round(v, 4) for v in pl["folded"]], down_dir=[round(v, 4) for v in pl["down"]],
        foot_down=[round(v, 1) for v in foot], length_mm=round(pl["L"], 1), lean_deg=LEAN,
        swing=[n for n in made if n.startswith("sidestand_")], fixed=[n for n in made if n.startswith("ssmount_")]))
    return rep, made


def built(mn, style=None):
    import build3d
    b3, m, cc, r, made0 = L.built(mn)
    use(build3d)
    rep, made = fit(build3d, m, cc, style)
    return build3d, m, cc, rep, made


# ---------------------------------------------------------------- the check and pictures
def leads():
    ld = dict(BR.LEADS)
    ld["Chopper"] = "lodestar_chopper"
    return ld


def check(names=None):
    res = {}
    todo = names or list(leads().values())
    for mn in todo:
        b3, m, cc, rep, made = built(mn)
        res[mn] = rep
        s = rep.get("stand", {})
        print(f"SSCHECK {mn:20s} {rep['family']:9s} fitted={rep.get('fitted')} L={s.get('length_mm')} pivot={s.get('pivot')} "
              f"swing={s.get('angle_down_deg')} down={rep.get('clash_down')} folded={rep.get('clash_folded')}", flush=True)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "check.json")
    old = json.load(open(path)) if (names and os.path.exists(path)) else {}
    old.update(res)
    json.dump(old, open(path, "w"), indent=1, default=list)
    return res


def shots(names=None, samples=40):
    """Per lead bike: the stand folded (low from the left) and down (the bike leaned on it, from the left front)."""
    os.makedirs(OUT, exist_ok=True)
    for fam, mn in leads().items():
        if names and mn not in names and fam not in names:
            continue
        for pose in ("folded", "down"):
            b3, m, cc, rep, made = built(mn)
            if not rep.get("fitted"):
                break
            s = rep["stand"]
            P = Vector(s["pivot"])
            sc = bpy.context.scene
            if pose == "down":
                swing(s["swing"], dict(P=P, axis=Vector(s["axis"])), s["angle_down_deg"])
                root = bpy.data.objects.new("lean_root", None)
                sc.collection.objects.link(root)
                for ob in list(bpy.data.objects):
                    if ob.parent is None and ob is not root and ob.type in ("MESH", "CURVE", "EMPTY"):
                        ob.parent = root
                # leaned to its left (+y) about the line the tyres touch the floor along
                root.rotation_euler = (math.radians(-LEAN), 0, 0)
                bpy.context.view_layer.update()
            b3.studio(sc)
            sc.cycles.samples = samples
            sc.render.resolution_x, sc.render.resolution_y = 1000, 700
            if pose == "folded":
                BR.shoot(sc, os.path.join(OUT, f"{mn}_folded.png"), (P.x + 420, P.y + 950, P.z + 300), (P.x - 70, P.y, P.z - 10), 38)
            else:
                f = Vector(s["foot_down"])
                c_ = math.cos(math.radians(LEAN))
                fy, fz = f.y * c_ + f.z * math.sin(math.radians(LEAN)), f.z * c_ - f.y * math.sin(math.radians(LEAN))
                BR.shoot(sc, os.path.join(OUT, f"{mn}_down.png"), (f.x + 1100, fy + 1700, 700), (f.x - 150, fy - 60, 300), 36)
                print(f"SSFOOT {mn} foot height after lean {fz:.1f} mm", flush=True)


if __name__ == "__main__":
    a = sys.argv[1:]
    cmd = a[0] if a else "check"
    smp = int(a[a.index("--samples") + 1]) if "--samples" in a else 40
    rest = [x for x in a[1:] if not x.startswith("--") and x != str(smp)]
    if cmd == "check":
        check(rest or None)
    elif cmd == "shots":
        shots(rest or None, smp)
