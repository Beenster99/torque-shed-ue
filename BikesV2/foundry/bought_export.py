"""beenster's other bought bikes (CGTrader, royalty free, Blender 4.2; beenster 25 Nov: "I have 3 more bikes for the same
treatment"): the Indian Scout 101, the Ducati Panigale V4 and the Triumph Trident 800, turned into the game's V2 format
the same way bought_multistrada.py turns the Multistrada (which keeps its own script; this is the same steps, with each
model's object table in bought_models.py):

    <out>/<model>.glb            the drawing (one mesh, a primitive per surface s000..): NEVER committed (gitignored)
    <out>/<model>.json           its slots, moving groups, finishes and key points (no geometry): committed as
                                 Data/BikesV2/<model>.json

    pip install bpy   (Blender 4.2+ as a module; the cloud has it)
    python3 -I BikesV2/foundry/bought_export.py <model> "<...>/<file>.blend" --out <dir>
    (<model>: indian_scout / ducati_panigale / triumph_trident, the keys of bought_models.MODELS)

Each model's frame: metres, -Y forward, +X the rider's left, Z up, ground at z 0 (all three, as the Multistrada). The
game's: x forward from the rear axle, y to the rider's left, z up (millimetres in the json, metres in the glb, y-up).
Every object is put in a game slot and a moving group by its (Spanish) name. An object with a rule (a function in the
table) is split into its loose pieces first and each piece placed by where it is. The tyres are left out: the game bends
its own tread round the drawn rims, to the sizes in points_mm.stats. A mono-shock drawn as one damper (group "shock") is
cut across its length into the top (frame) and bottom (swingarm) ends the game slides together.
"""
import json
import math
import os
import sys

import bpy  # noqa
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import bought_models  # noqa: E402

SIDE_TAGS = ("lens_amber", "d3_mirrorglass", "mirror_head")


class Piece:
    """One loose piece (or a whole object), in the model's frame."""

    def __init__(self, ob, base):
        self.ob, self.base = ob, base
        ws = [ob.matrix_world @ v.co for v in ob.data.vertices] or [Vector()]
        self.lo = Vector([min(w[i] for w in ws) for i in range(3)])
        self.hi = Vector([max(w[i] for w in ws) for i in range(3)])
        self.c = (self.lo + self.hi) / 2
        self.size = self.hi - self.lo
        self.n = len(ob.data.polygons)


def bbox(ob):
    ws = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    return Vector([min(w[i] for w in ws) for i in range(3)]), Vector([max(w[i] for w in ws) for i in range(3)])


def select_only(obs):
    bpy.ops.object.select_all(action="DESELECT")
    for o in obs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = obs[0]


def apply_all(ob):
    """Bake the object's modifiers (the Panigale's are a smoothing node group) and unparent it, keeping where it is."""
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
    mw = ob.matrix_world.copy()
    ob.modifiers.clear()
    old = ob.data
    ob.data = me
    if old.users == 0:
        bpy.data.meshes.remove(old)
    ob.parent = None
    ob.matrix_world = mw


def split_loose(ob):
    select_only([ob])
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.separate(type="LOOSE")
    bpy.ops.object.mode_set(mode="OBJECT")
    return list(bpy.context.selected_objects)


def cut_faces(ob, keep_far, along, at, name):
    """Split ob's faces at `at` along the direction `along` (model frame): the far side becomes a new object."""
    import bmesh
    mw = ob.matrix_world
    far = lambda f: (mw @ f.calc_center_median()).dot(along) > at
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm2 = bm.copy()
    bmesh.ops.delete(bm2, geom=[f for f in bm2.faces if not far(f)], context="FACES")
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if far(f)], context="FACES")
    me2 = bpy.data.meshes.new(name)
    bm2.to_mesh(me2)
    bm.to_mesh(ob.data)
    for m in ob.data.materials:
        me2.materials.append(m)
    o2 = bpy.data.objects.new(name, me2)
    o2.matrix_world = mw.copy()
    bpy.context.scene.collection.objects.link(o2)
    return o2


def main():
    # (under blender.exe -b -P this.py -- <model> <blend> ..., only what comes after "--" is ours)
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    out = args[args.index("--out") + 1] if "--out" in args else os.path.join(GAME, "build", "bought")
    model, blend = args[0], args[1]
    cfg = bought_models.MODELS[model]
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=blend)
    auto = cfg.get("auto", False)
    if not auto:
        table = cfg["objects"]
        names = {o.name for o in bpy.data.objects if o.type == "MESH"}
        unknown = sorted(names - set(table))
        missing = sorted(n for n in table if n not in names)
        if unknown or missing:
            raise SystemExit(f"{model}: model changed: no rule for {unknown}, not found {missing}")
    shown = set(bpy.context.view_layer.objects)
    if auto:
        # (unparented where they are first: some hang off a scaled empty)
        for o in [o for o in bpy.data.objects if o.type == "MESH"]:
            apply_all(o)
    for o in list(bpy.data.objects):
        if o.type != "MESH" or (auto and (o not in shown or o.hide_render)):     # (an auto model's hidden spares go too)
            bpy.data.objects.remove(o, do_unlink=True)
    if not auto:
        for o in list(bpy.data.objects):
            apply_all(o)

    finish = dict(bought_models.FINISH, **cfg.get("finish", {}))
    named = bought_models.NAMED
    ainfo = None
    if auto:
        # paint, plastic and leather in the model's own colours (bought_models.auto_finish)
        own = {}
        for m in bpy.data.materials:
            b = m.node_tree.nodes.get("Principled BSDF") if m.use_nodes and m.node_tree else None
            if b and not b.inputs["Base Color"].is_linked:
                own[m.name.split(".")[0]] = (tuple(b.inputs["Base Color"].default_value)[:3], b.inputs["Metallic"].default_value)
        for mn in {m.name.split(".")[0] for o in bpy.data.objects if o.type == "MESH" for m in o.data.materials if m}:
            finish[mn] = bought_models.auto_finish(mn, own.get(mn), cfg)
        # (bought_auto.py sorts every loose piece by where it is: these models' names aren't to be relied on)
        import bought_auto
        raw, ainfo = bought_auto.sort_pieces([o for o in bpy.data.objects if o.type == "MESH"], cfg, split_loose, cut_faces)
        rear_y = ainfo["rear_y"]
        Fg, Rg = ainfo["F"], ainfo["R"]
        F = Vector((0, rear_y - Fg[0], Fg[2]))
        R = Vector((0, rear_y - Rg[0], Rg[2]))
        to_game = lambda v: Vector((rear_y - v.y, v.x, v.z))
        tyre = {end: (t["r"], t["w"]) for end, t in ainfo["tyres"].items()}
        placed = [(Piece(ob, base), slot, group, role or "", fin) for ob, base, slot, group, role, fin in raw]
    else:
        # the axles (the rims' middles), the tyres' sizes (then the tyres go)
        fr_rim, rr_rim = cfg["rims"]
        fl, fh = bbox(bpy.data.objects[fr_rim])
        rl, rh = bbox(bpy.data.objects[rr_rim])
        F = Vector((0, (fl.y + fh.y) / 2, (fl.z + fh.z) / 2))
        R = Vector((0, (rl.y + rh.y) / 2, (rl.z + rh.z) / 2))
        rear_y = R.y
        to_game = lambda v: Vector((rear_y - v.y, v.x, v.z))
        tyre = {}
        for end, nm in zip(("front", "rear"), cfg["tyres"]):
            lo, hi = bbox(bpy.data.objects[nm])
            tyre[end] = ((hi.z - lo.z) / 2, hi.x - lo.x)
            bpy.data.objects.remove(bpy.data.objects[nm], do_unlink=True)
    ctx = {"F": F, "R": R}

    def tag_of(base, mat):
        f = cfg.get("obj_finish", {}).get(base)
        f = named[f] if isinstance(f, str) else f
        return (f or finish[mat.split(".")[0]])["tag"]

    if not auto:
        # split what holds pieces of more than one group, side or finish; place every piece
        # (an object a key point is measured from a few of its pieces of is split too)
        measured = {sp[0] for sp in (cfg[k] for k in ("fork_tubes", "stem", "peg", "grip", "screen", "cap", "seat")) if isinstance(sp, tuple)}
        placed = []      # (piece, slot, group, role, finish name or None)
        for o in list(bpy.data.objects):
            base = o.name
            rule = table[base]
            if rule is None:
                continue
            sided = any(tag_of(base, m.name) in SIDE_TAGS for m in o.data.materials if m)
            sided = sided or (not callable(rule) and len(rule) > 2 and rule[2] == "head")
            obs = split_loose(o) if (callable(rule) or sided or base in measured) else [o]
            for ob in obs:
                p = Piece(ob, base)
                r = rule(p, ctx) if callable(rule) else rule
                r = tuple(r) + ("", None)[len(r) - 2:] if len(r) < 4 else tuple(r)
                placed.append((p, r[0], r[1], r[2] or "", r[3]))

    # the mono-shock drawn as one damper: cut across its length into its two ends
    sh = [x for x in placed if x[2] == "shock"]
    if sh:
        pts = [p.ob.matrix_world @ v.co for p, *_ in sh for v in p.ob.data.vertices]
        mid = sum(pts, Vector()) / len(pts)
        syy = sum((v.y - mid.y) ** 2 for v in pts); szz = sum((v.z - mid.z) ** 2 for v in pts)
        syz = sum((v.y - mid.y) * (v.z - mid.z) for v in pts)
        ang = 0.5 * math.atan2(2 * syz, syy - szz)
        ax = Vector((0, math.cos(ang), math.sin(ang)))
        if ax.z > 0:
            ax = -ax        # top to bottom
        proj = [v.dot(ax) for v in pts]
        cut = min(proj) + (max(proj) - min(proj)) * cfg.get("shock_cut", 0.55)
        rest = [x for x in placed if x[2] != "shock"]
        for p, slot, _g, role, fin in sh:
            far = cut_faces(p.ob, True, ax, cut, p.ob.name + "_rod")
            rest.append((Piece(p.ob, p.base), slot, "shock_top", role, fin))
            rest.append((Piece(far, p.base), slot, "shock_bottom", role, fin))
        placed = rest
    placed = [x for x in placed if len(x[0].ob.data.polygons)]

    # key points, from the pieces as drawn (game frame, metres)
    def gv(pieces):
        return [to_game(p.ob.matrix_world @ v.co) for p in pieces for v in p.ob.data.vertices]

    def pick(spec):
        nm, flt = spec if isinstance(spec, tuple) else (spec, None)
        return [p for p, *_ in placed if p.base == nm and (flt is None or flt(p))]

    def src(key, ckey=None):
        """A key point's verts (game frame): measured by the sorter, or from the table's pieces."""
        return list(ainfo[key]) if auto else gv(pick(cfg[ckey or key]))
    fork = src("fork", "fork_tubes")
    fmid = sum(fork, Vector()) / len(fork)
    sxx = sum((p.x - fmid.x) ** 2 for p in fork); szz = sum((p.z - fmid.z) ** 2 for p in fork)
    sxz = sum((p.x - fmid.x) * (p.z - fmid.z) for p in fork)
    ang = 0.5 * math.atan2(2 * sxz, sxx - szz)
    d = Vector((math.cos(ang), 0, math.sin(ang)))
    if d.z < 0:
        d = -d
    if abs(d.x) > abs(d.z):          # (the long axis came out across: the other one)
        d = Vector((-d.z, 0, d.x)) if d.x > 0 else Vector((d.z, 0, -d.x))
    rake = math.degrees(math.atan2(-d.x, d.z))
    stem = src("stem")
    smid = sum(stem, Vector()) / len(stem)
    smid.y = 0.0
    on_axis = lambda z: smid + d * ((z - smid.z) / d.z)
    st_top = cfg.get("steer_top_z") or (ainfo["steer_top_z"] if auto else max(v.z for v in stem))
    st_bot = cfg.get("steer_bottom_z") or (ainfo["steer_bottom_z"] if auto else min(v.z for v in stem))
    steer_top, steer_bottom = on_axis(st_top), on_axis(st_bot)
    front_axle, rear_axle = to_game(F), to_game(R)
    trail = on_axis(0.0).x - front_axle.x
    fork_line = lambda z: Vector((fmid.x + d.x * (z - fmid.z) / d.z, 0, z))
    fork_top = fork_line(max(v.z for v in fork))
    fork_len = (fork_top - front_axle).length

    if auto:
        pivot = Vector(ainfo["pivot"])
    elif "pivot" in cfg:
        pivot = to_game(Vector((0, cfg["pivot"][0], cfg["pivot"][1])))
    else:
        arm = gv([p for p, s, g, *_ in placed if g == "swingarm" and s == "swingarm"])
        front_end = [v for v in arm if v.x > max(v.x for v in arm) - 0.06]
        pivot = Vector((max(v.x for v in arm) - 0.035, 0, sum(v.z for v in front_end) / len(front_end)))
    # shock ends: along the damper, top (frame) and bottom (swingarm)
    tops = gv([p for p, s, g, *_ in placed if g == "shock_top"])
    bots = gv([p for p, s, g, *_ in placed if g == "shock_bottom"])
    if tops and bots:
        sa = (sum(bots, Vector()) / len(bots) - sum(tops, Vector()) / len(tops))
        sa.y = 0
        sa.normalize()
        allsh = tops + bots + gv([p for p, s, g, *_ in placed if g == "shock_spring"])
        pr = [v.dot(sa) for v in allsh]
        t_end = [v for v, q in zip(allsh, pr) if q < min(pr) + 0.012]
        b_end = [v for v, q in zip(allsh, pr) if q > max(pr) - 0.012]
        sh_top, sh_bot = sum(t_end, Vector()) / len(t_end), sum(b_end, Vector()) / len(b_end)
    else:
        springs = gv([p for p, s, g, *_ in placed if g == "shock_spring"])
        if springs:
            # (only the spring drawn as the shock's own piece: its two ends)
            smid = sum(springs, Vector()) / len(springs)
            far = max(springs, key=lambda v: (v - smid).length)
            sa = far - smid
            sa.y = 0
            sa.normalize()
            if sa.z < 0:
                sa = -sa
            pr = [v.dot(sa) for v in springs]
            sh_top = sum([v for v, q in zip(springs, pr) if q > max(pr) - 0.012], Vector()) / len([q for q in pr if q > max(pr) - 0.012])
            sh_bot = sum([v for v, q in zip(springs, pr) if q < min(pr) + 0.012], Vector()) / len([q for q in pr if q < min(pr) + 0.012])
            sh_bot = sh_bot - sa * ((sh_top - sh_bot).length * 0.6)    # (the damper's body and eye reach on past the spring)
        else:
            # (no shock found to move: none for the game to work; it rides on the stats' travel all the same)
            print("bought export: no rear shock found: no shock points")
            sh_top = sh_bot = None

    left = lambda vs: [v for v in vs if v.y > 0]
    avg = lambda vs: sum(vs, Vector()) / len(vs)
    peg = avg(left(src("peg")))
    grip = avg(left(src("grip")))
    seat = src("seat")
    h0, h1 = cfg.get("hip_band") or ainfo["hip_band"]
    rider_seat = [v for v in seat if abs(v.y) < 0.04 and h0 < v.x < h1]
    hip_v = min(rider_seat, key=lambda v: v.z)
    seat_top = max((v for v in seat if abs(v.y) < 0.04 and abs(v.x - hip_v.x) < 0.04), key=lambda v: v.z, default=hip_v)
    cap = avg(src("cap"))
    body = gv([p for p, s, g, *_ in placed if g == "" and s not in ("exhaust",)])
    clearance = min(v.z for v in body)

    # the dash's screen: the biggest flat face it has looking back (and not down) at the rider, all its coplanar polygons
    faces = []
    for p in ([Piece(o, o.name) for o in ainfo["screen"]] if auto else pick(cfg["screen"])):
        m3 = p.ob.matrix_world.to_3x3()
        for poly in p.ob.data.polygons:
            nn = (m3 @ poly.normal).normalized()
            if -nn.y < -0.3 and nn.z > -0.2:
                g = Vector((-nn.y, nn.x, nn.z))
                vs = [to_game(p.ob.matrix_world @ p.ob.data.vertices[i].co) for i in poly.vertices]
                faces.append((g, poly.area, vs))

    def plane_of(f0):
        return [f for f in faces if f[0].dot(f0[0]) > 0.985 and abs((f[2][0] - f0[2][0]).dot(f0[0])) < 0.003]
    if faces:
        best = max(faces, key=lambda f: sum(x[1] for x in plane_of(f)) if f[1] > 1e-5 else 0)
        flat = plane_of(best)
        n_g = Vector(best[0])
    else:
        # (no screen found: a 5-inch one just above the top yoke, looking back up at the rider)
        print("bought export: no dash screen found: one placed above the yoke")
        n_g = Vector((-0.55, 0, 0.83)).normalized()
        u_ = Vector((n_g.z, 0, -n_g.x))
        u_ = u_ if u_.z > 0 else -u_
        o_ = steer_top + Vector((-0.07, 0, 0.1))
        flat = [(n_g, 1, [o_ + Vector((0, sy * 0.055, 0)) + u_ * (sz * 0.035) for sy in (-1, 1) for sz in (-1, 1)])]
        best = flat[0]
    n_g.y = 0
    n_g.normalize()
    up = Vector((-n_g.z, 0, n_g.x))
    if up.z < 0:
        up = -up
    face = [v for f in flat for v in f[2]]
    ys = [v.y for v in face]
    hs = [v.dot(up) for v in face]
    span_w, span_h = max(ys) - min(ys), max(hs) - min(hs)
    c0 = avg(face)
    sc = c0 + Vector((0, (max(ys) + min(ys)) / 2 - c0.y, 0)) + up * ((max(hs) + min(hs)) / 2 - c0.dot(up))

    mm = lambda v: [round(v.x * 1000, 1), round(v.y * 1000, 1), round(v.z * 1000, 1)]
    st = dict(cfg["stats"])
    pts = {
        "front_axle": mm(front_axle), "rear_axle": mm(rear_axle),
        "steer_top": mm(steer_top), "steer_bottom": mm(steer_bottom), "fork_top": mm(fork_top),
        "fork_dir": [round(d.x, 5), 0.0, round(d.z, 5)], "swing_pivot": mm(pivot), "rigid": False,
        "hip": [round(hip_v.x * 1000, 1), 0.0, round(seat_top.z * 1000, 1)],
        "stats": dict({"wheelbase": round((front_axle.x - rear_axle.x) * 1000), "rake_deg": round(rake, 2), "trail": round(trail * 1000),
                       "seat_height": round(seat_top.z * 1000), "ground_clearance": max(100, round(clearance * 1000)),
                       "fork_length": round(fork_len * 1000),
                       "front_radius": round(tyre["front"][0] * 1000), "rear_radius": round(tyre["rear"][0] * 1000),
                       "shaft": False}, **st),
        "peg": mm(peg), "grip": mm(grip),
        "tank_cap": mm(cap),
        "gauges": [{"what": "screen", "at": mm(sc), "axis": [round(n_g.x, 5), 0.0, round(n_g.z, 5)], "up": [round(up.x, 5), 0.0, round(up.z, 5)],
                    "w": round(span_w * 1000 * cfg.get("screen_fill", (0.86, 0.82))[0], 1),
                    "h": round(span_h * 1000 * cfg.get("screen_fill", (0.86, 0.82))[1], 1), "node": cfg.get("screen_node", "" if (not auto or cfg.get("faired")) else "steer")}],
    }

    if sh_top is not None:
        pts["shock_top"] = [round(sh_top.x * 1000, 1), 0.0, round(sh_top.z * 1000, 1)]
        pts["shock_bottom"] = [round(sh_bot.x * 1000, 1), 0.0, round(sh_bot.z * 1000, 1)]

    # surfaces: one per slot + group + finish (+ side, for what the game lights or aims a side at a time)
    tagged, looks = {}, {}
    for p, slot, node, role, fin in placed:
        ob = p.ob
        if not ob.data.materials or all(m is None for m in ob.data.materials):
            ob.data.materials.clear()
            ob.data.materials.append(bpy.data.materials.get("m_iron") or bpy.data.materials.new("m_iron"))   # (drawn with none)
        for i, m in enumerate(ob.data.materials):
            if m is None:
                continue
            f = fin or cfg.get("obj_finish", {}).get(p.base)
            f = dict(named[f] if isinstance(f, str) else (f or finish[m.name.split(".")[0]]))
            lens = f["tag"]
            if role == "head":
                lens = "mirror_head"
            side = ("L" if p.c.x > 0 else "R") if lens in SIDE_TAGS else ""
            key = (slot, node, f["surface"], lens, side)
            if key not in tagged:
                c = bpy.data.materials.new(f"s{len(tagged):03d}")
                tagged[key] = c
                look = {"slot": slot, "node": node, "surface": f["surface"], "tag": lens, "ue": f["ue"]}
                if f.get("paint"):
                    look["paint"] = f["paint"]
                if slot == "mirrors":
                    look["sub"] = "mirrors"
                if slot == "dashboard":
                    look["sub"] = "clocks"
                if "color" in f:
                    look["color"], look["metallic"], look["roughness"] = f["color"], f.get("metallic", 0.0), f.get("roughness", 0.5)
                looks[c.name] = look
            ob.data.materials[i] = tagged[key]

    # join, put in the game's frame, export as build3d does (y up, metres, normals, no UVs)
    obs = [p.ob for p, *_ in placed]
    for o in bpy.data.objects:
        if o not in obs:
            bpy.data.objects.remove(o, do_unlink=True)
    select_only(obs)
    bpy.ops.object.make_single_user(object=True, obdata=True, material=False)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = ob.data.name = model
    for a in list(ob.data.color_attributes):         # (some models carry vertex colours: the game's finishes are the colour)
        ob.data.color_attributes.remove(a)
    ob.matrix_world = Matrix.Translation((rear_y, 0, 0)) @ Matrix.Rotation(math.radians(90), 4, "Z")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # (it comes with a side stand: bought_stand.py)
    import bought_stand

    def new_material(look):
        c = bpy.data.materials.new(f"s{len(looks):03d}")
        looks[c.name] = look
        return c
    ob = bought_stand.add_stand(ob, cfg["style"], pts, looks, new_material)
    ob.name = ob.data.name = model
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    glb = os.path.join(out, model + ".glb")
    bpy.ops.export_scene.gltf(filepath=glb, use_selection=True, export_apply=True, export_yup=True, export_texcoords=False,
                              export_normals=True, export_materials="EXPORT", export_cameras=False, export_lights=False)
    info = {"name": cfg["name"], "brand": cfg["brand"], "family": cfg["family_name"], "rev": "bought",
            "bought": {"from": "CGTrader, royalty free (beenster's own copy)", "source": os.path.basename(blend),
                       "note": "the glb is never committed: Tools/bought_bikes.py puts it in the packaged build"},
            "wheelbase_mm": pts["stats"]["wheelbase"], "seat_height_mm": pts["stats"]["seat_height"], "format": 2, "triangles": tris,
            "points_mm": pts, "pieces": sorted({f"{v['slot']}|{v['node']}" for v in looks.values()}), "surfaces": looks}
    jpath = os.path.join(out, model + ".json")
    json.dump(info, open(jpath, "w"), indent=1)
    print(f"bought export {model}: {len(looks)} surfaces, {len(info['pieces'])} pieces, {tris} triangles -> {glb}")
    print(json.dumps({k: v for k, v in pts.items() if k != "gauges"}))
    print("gauges", pts["gauges"])


if __name__ == "__main__":
    main()
