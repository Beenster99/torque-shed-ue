"""The 3D step: builds a whole bike in Blender from a model description file and renders it.

Reads the same data/models/<model>.json as the drawing step. Blender axes: x forward,
y to the bike's left, z up, metres. The side render uses an orthographic camera
framed exactly like the elevation drawing, so the two can be overlaid.

Usage: python3 foundry/build3d.py corvano_naked [--glb] [--samples 48]
       python3 foundry/build3d.py corvano_naked --game   (writes Data/BikesV2/ for the game, no render)
  -> out/renders/<model>_side.png, <model>_q.png, and optionally out/glb/<model>.glb
"""
import json
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shapes  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MM = 0.001

# Half-width tables along each body part, u = 0 at the rear end and 1 at the front end.
# n is the roundness of the cross-section (2 = ellipse, higher = boxier); bottom narrows the underside.
WIDTHS = {
    "tank": {"w": [[0, 95], [0.25, 150], [0.6, 180], [0.85, 150], [1, 105]], "n": 2.7, "bottom": 0.62},
    "seat": {"w": [[0, 105], [0.35, 140], [0.7, 150], [1, 85]], "n": 3.6, "bottom": 0.85},
    "tail": {"w": [[0, 55], [0.4, 105], [1, 125]], "n": 3.0, "bottom": 0.8},
}

LIB = json.load(open(os.path.join(ROOT, "data", "materials.json")))
FINISH = {"name": "gloss"}  # paint finish of the bike being built; build() sets it
_mats = {}


def hex_rgb(h):
    h = h.lstrip("#")
    return tuple(((int(h[i:i + 2], 16) / 255) ** 2.2) for i in (0, 2, 4))


def mat(name, col=None, finish=None):
    """A surface from data/materials.json. Slot surfaces are named tag__slot so the game can recolour them."""
    spec = dict(LIB["surfaces"][name]) if name in LIB["surfaces"] else {"col": [0.2, 0.205, 0.21], "rough": 0.85}
    if spec.get("finish"):
        finish = finish or FINISH["name"]
        spec.update(LIB["finishes"][finish])
    key = name + str(col) + str(finish)
    if key in _mats:
        return _mats[key]
    m = bpy.data.materials.new(name + ("__" + spec["slot"] if spec.get("slot") else ""))
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    c = tuple(col or spec.get("col", (0.5, 0.5, 0.5)))
    b.inputs["Base Color"].default_value = (*c, 1)
    b.inputs["Metallic"].default_value = spec.get("metallic", 0.0)
    b.inputs["Roughness"].default_value = spec.get("rough", 0.5)
    if spec.get("coat"):
        b.inputs["Coat Weight"].default_value = spec["coat"]
        b.inputs["Coat Roughness"].default_value = 0.04
    if spec.get("sheen"):
        b.inputs["Sheen Weight"].default_value = spec["sheen"]
    if spec.get("alpha"):
        b.inputs["Alpha"].default_value = spec["alpha"]
    if spec.get("emit"):
        b.inputs["Emission Color"].default_value = (*c, 1)
        b.inputs["Emission Strength"].default_value = spec["emit"]
    if spec.get("bump"):
        size_mm, depth = spec["bump"]
        tc = nt.nodes.new("ShaderNodeTexCoord")
        nz = nt.nodes.new("ShaderNodeTexNoise")
        nz.inputs["Scale"].default_value = 1.0 / (size_mm * MM)
        nz.inputs["Detail"].default_value = 3.0
        bp = nt.nodes.new("ShaderNodeBump")
        bp.inputs["Strength"].default_value = depth
        bp.inputs["Distance"].default_value = size_mm * MM * 0.3
        nt.links.new(tc.outputs["Object"], nz.inputs["Vector"])
        nt.links.new(nz.outputs["Fac"], bp.inputs["Height"])
        nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    if DETAIL["on"] and not spec.get("alpha") and not spec.get("emit"):
        # slight wear: no surface on a used bike has one even gloss, so break the roughness up a little
        tc = nt.nodes.new("ShaderNodeTexCoord")
        nz = nt.nodes.new("ShaderNodeTexNoise")
        nz.inputs["Scale"].default_value, nz.inputs["Detail"].default_value = 9.0, 6.0
        mr = nt.nodes.new("ShaderNodeMapRange")
        r0 = spec.get("rough", 0.5)
        mr.inputs["From Min"].default_value, mr.inputs["From Max"].default_value = 0.3, 0.75
        mr.inputs["To Min"].default_value, mr.inputs["To Max"].default_value = max(0.02, r0 * 0.75), min(1.0, r0 * 1.3 + 0.06)
        nt.links.new(tc.outputs["Object"], nz.inputs["Vector"])
        nt.links.new(nz.outputs["Fac"], mr.inputs["Value"])
        nt.links.new(mr.outputs["Result"], b.inputs["Roughness"])
    _mats[key] = m
    return m


GAME_THIN = [float(v) for v in os.environ.get("BIKE_GAME_THIN", "2.0,0.3,0.6,0.5").split(",")]


GAME_STAND = {}      # the fitted side stand's pivot and swing, for the game (points_mm "stand")


def export_game(name, model, model_file=None):
    """Writes the bike as the game reads it: Data/BikesV2/<name>.glb (one mesh, one primitive per
    surface, the reader in Source/TorqueShedProto/Fit/PartMesh) and <name>.json. Every surface is one
    game slot, one moving group and one finish (game_parts.classify), named s000.. in the glb; the json
    gives each its slot, group ("node"), look and paint slot, and the bike's key points."""
    import re
    import detail3d
    import game_parts
    try:
        import game_common
    except ImportError:
        game_common = None
    dst = os.path.join(os.path.dirname(ROOT), "Data", "BikesV2")
    os.makedirs(dst, exist_ok=True)
    obs = [o for o in bpy.context.scene.objects if o.type in ("MESH", "CURVE")]
    bpy.ops.object.select_all(action="DESELECT")
    for o in obs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = obs[0]
    bpy.ops.object.convert(target="MESH")
    bpy.ops.object.make_single_user(object=True, obdata=True, material=False)
    obs = [o for o in bpy.context.selected_objects if o.type == "MESH"]
    # What the game makes itself is left out: the tyres (its own tread sections bent round the drawn wheels, as
    # sharp as the catalogue's: Fit/BikeParts V2Tyres) and the clocks' working pieces (its numbered scale, needle
    # and lit screen: V2Gauges).
    gauges = []
    for nm, gz in sorted(detail3d.GAUGES.items()):
        if bpy.data.objects.get(nm):
            cl = game_parts.classify(nm, model)
            gauges.append(dict(gz, node=cl[1] if cl else ""))
    own = re.compile(r"^(front_tyre|rear_tyre|clocktick|clockneedle|dashrev)")
    for o in [o for o in obs if own.match(o.name)]:
        obs.remove(o)
        bpy.data.objects.remove(o, do_unlink=True)
    tagged, looks, unknown = {}, {}, set()
    # the Definition build is drawn for close-up pictures (543,000 triangles on the naked lead); the game
    # gets every piece thinned to what reads at riding and shed distance
    hist = [0] * 5
    for o in obs:
        n = sum(len(p.vertices) - 2 for p in o.data.polygons)
        hist[min(4, 0 if n < 100 else 1 if n < 240 else 2 if n < 800 else 3 if n < 2500 else 4)] += n
        if n < 100 or "tyre" in o.name or "rotor" in o.name:      # (a drilled disc is a flat face full of holes: left exactly as drawn)
            continue
        bpy.context.view_layer.objects.active = o
        if any(m and m.name.startswith(("paint", "accent")) for m in o.data.materials):
            # paintwork shows every dent in its reflections: only halved, never flattened
            if n > 2500:
                md = o.modifiers.new("thin", "DECIMATE")
                md.ratio = GAME_THIN[3]
                bpy.ops.object.modifier_apply(modifier=md.name)
            continue
        # first what costs nothing to look at: faces that lie in one plane or along a straight run become one
        # (a drilled disc, a plate, a straight tube keep their exact shape)
        md = o.modifiers.new("flat", "DECIMATE")
        md.decimate_type = "DISSOLVE"
        md.angle_limit = math.radians(GAME_THIN[0])
        md.delimit = {"MATERIAL", "SHARP"}
        bpy.ops.object.modifier_apply(modifier=md.name)
        n2 = sum(len(p.vertices) - 2 for p in o.data.polygons)
        ratio = GAME_THIN[1] if n2 > 2500 else GAME_THIN[2] if n2 > 800 else 1.0
        if ratio < 1.0:
            md = o.modifiers.new("thin", "DECIMATE")
            md.ratio = ratio
            bpy.ops.object.modifier_apply(modifier=md.name)
    bpy.context.view_layer.update()
    for o in obs:
        r = game_parts.classify(o.name, model)
        if not r:
            unknown.add(game_parts.stem(o.name))
            r = ("frame", "")
        xs = [(o.matrix_world @ __import__("mathutils").Vector(q)).x / MM for q in o.bound_box]
        r = game_parts.placed(o.name, r[0], r[1], min(xs), max(xs), model)
        sk_ = model.get("shock")
        if r == ("shock", "") and sk_ and not model.get("swingarm", {}).get("rigid"):
            # the shock works in the game (beenster: shocks that compress): its pieces by which end they belong to
            ws = [o.matrix_world @ __import__("mathutils").Vector(q) for q in o.bound_box]
            cx, cz = sum(w.x for w in ws) / 8 / MM, sum(w.z for w in ws) / 8 / MM
            ax, az = sk_["bottom"][0] - sk_["top"][0], sk_["bottom"][1] - sk_["top"][1]
            r = ("shock", game_parts.shock_node(o.name, ((cx - sk_["top"][0]) * ax + (cz - sk_["top"][1]) * az) / (ax * ax + az * az)))
        # the piece of its slot a common part can take the place of ("grip", "silencer", ...: common parts'
        # foundry/game_common.py sub_of; "" for the rest of the slot)
        part_sub = (game_common.sub_of(game_parts.stem(o.name), o) or "") if game_common else ""
        for i, m in enumerate(o.data.materials):
            if m is None:
                continue
            key = (r[0], r[1], part_sub, m.name)
            if key not in tagged:
                c = m.copy()
                c.name = f"s{len(tagged):03d}"
                tagged[key] = c
                tag = m.name.split(".")[0]
                spec = LIB["surfaces"].get(tag.split("__")[0], {})
                b = m.node_tree.nodes["Principled BSDF"]
                ue = spec.get("ue")
                colour = [round(v, 4) for v in b.inputs["Base Color"].default_value[:3]]
                metal, rough = round(b.inputs["Metallic"].default_value, 3), round(b.inputs["Roughness"].default_value, 3)
                # the game's lights find their lenses by these names (TorqueLights: Surf:lens_white / amber / red)
                lens = "lens_white" if ue == "lens_clear" and r[0] in ("headlight", "fog_lights") else ue if ue in ("lens_red", "lens_amber") else ""
                tint = spec.get("ue_tint", True)
                if ue is None or tag.split("__")[0] == "screen":
                    # the pipeline's own material: the game finish that suits it (build 154: glass came out as
                    # solid steel, grips white, dials and hoses brushed metal)
                    ue, lens, colour = game_parts.look(tag.split("__")[0], r[0], colour, metal, rough)
                    tint = colour is not None
                look = {"slot": r[0], "node": r[1], "surface": tag, "tag": lens or tag.split("__")[0], "ue": ue}
                if part_sub:
                    look["sub"] = part_sub
                if spec.get("slot"):
                    look["paint"] = spec["slot"]
                if tint:
                    look["color"], look["metallic"], look["roughness"] = colour, metal, rough
                looks[c.name] = look
            o.data.materials[i] = tagged[key]
    pts = game_parts.points(model)
    bpy.context.view_layer.update()
    for key, obname in (("peg", "peg01"), ("grip", "grip1")):      # the rider's left peg and grip as built
        o = bpy.data.objects.get(obname)
        if o:
            bb = [o.matrix_world @ __import__("mathutils").Vector(q) for q in o.bound_box]
            pts[key] = [round((min(getattr(q, a) for q in bb) + max(getattr(q, a) for q in bb)) / 2 / MM, 1) for a in "xyz"]
    # the filler cap's middle (the common parts' caps go on here)
    capq = [o.matrix_world @ __import__("mathutils").Vector(q) for o in obs if re.match(r"cap(bolt|lid|neck|ring|hinge)", o.name) for q in o.bound_box]
    if capq:
        pts["tank_cap"] = [round((min(getattr(q, a) for q in capq) + max(getattr(q, a) for q in capq)) / 2 / MM, 1) for a in "xyz"]
    if not gauges:
        # a bike drawn with no clocks (the MX bikes): the game gets a small cased display above the top yoke
        ft = pts["fork_top"]
        gauges = [{"what": "screen", "at": [round(ft[0] + 58, 1), 0.0, round(ft[2] + 31, 1)], "axis": [-0.75054, 0.0, 0.66083],
                   "up": [0.66083, 0.0, 0.75054], "w": 62.0, "h": 38.0, "node": "steer", "case": True}]
    pts["gauges"] = gauges
    if GAME_STAND:
        pts["stand"] = dict(GAME_STAND)
    # [lights] A DRL ring is its own lamp (on with the ignition; beenster Nov 17) when the headlamp also has a
    # dipped lens; a lamp that is only a ring stays the headlamp, or dipped beam would have nothing to light.
    heads = [v for v in looks.values() if v["slot"] == "headlight" and v["tag"] == "lens_white"]
    if any(v["surface"] != "d3_drl" for v in heads):
        for v in heads:
            if v["surface"] == "d3_drl":
                v["tag"] = "lens_drl"
    name = model_file or name
    bpy.context.view_layer.objects.active = obs[0]
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = ob.data.name = name
    # the joined bike must sit at the world origin with nothing above it: a parent left on the first object
    # (a wheel hung on its axle) put a 1.4 m shift in the file that the game does not read
    mw = ob.matrix_world.copy()
    ob.parent = None
    ob.matrix_world = mw
    bpy.context.view_layer.update()
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    assert ob.matrix_world.translation.length < 1e-6, "game export: the bike is not at the origin"
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    bpy.ops.export_scene.gltf(filepath=os.path.join(dst, name + ".glb"), use_selection=True, export_apply=True, export_yup=True,
                              export_texcoords=False, export_normals=True, export_materials="EXPORT", export_cameras=False, export_lights=False)
    g = model["geometry"]
    info = {"name": model["name"], "brand": model["brand"], "family": model["family"], "rev": model.get("rev"),
            "wheelbase_mm": g["wheelbase"], "seat_height_mm": g["seat_height"], "format": 2, "triangles": tris,
            "points_mm": pts, "pieces": sorted({f"{v['slot']}|{v['node']}" for v in looks.values()}),
            "surfaces": looks}
    json.dump(info, open(os.path.join(dst, name + ".json"), "w"), indent=1)
    print("triangles before thinning, by piece size (<100, <240, <800, <2500, more):", hist)
    print(f"game export {name}: {len(looks)} surfaces, {len(info['pieces'])} pieces, {tris} triangles, no rule for: {sorted(unknown) or 'none'}")
    return dst


def swatches(samples=32):
    """Renders every surface and paint finish as a ball on one sheet: out/renders/materials.png."""
    names = [("paint", f) for f in LIB["finishes"]] + [(n, None) for n in LIB["surfaces"] if n != "paint"]
    cols = 6
    demo = {"body": "#B3122A", "frame": "#A8884F", "rim": "#2b2d31", "accent": "#D7E23A"}
    for i, (n, f) in enumerate(names):
        x, z = -(i % cols) * 0.3, -(i // cols) * 0.3
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.12, segments=64, ring_count=32, location=(x, 0, z))
        ob = bpy.context.object
        bpy.ops.object.shade_smooth()
        slot = LIB["surfaces"][n].get("slot")
        ob.data.materials.append(mat(n, hex_rgb(demo[slot]) if slot else None, f))
    sc = bpy.context.scene
    rows = (len(names) + cols - 1) // cols
    w = bpy.data.worlds.new("w")
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.75, 0.77, 0.8, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.5
    sc.world = w
    cx, cz = -(cols - 1) * 0.15, -(rows - 1) * 0.15
    for loc, energy in (((cx + 1.0, 2.5, cz + 1.5), 120), ((cx - 1.5, 2.0, cz - 0.5), 40)):
        bpy.ops.object.light_add(type="AREA", location=loc)
        L = bpy.context.object
        L.data.energy, L.data.size = energy, 1.5
        L.rotation_euler = _look(loc, (cx, 0, cz))
    cam = bpy.data.cameras.new("sw")
    cam.type, cam.ortho_scale = "ORTHO", cols * 0.3
    ob = bpy.data.objects.new("sw", cam)
    sc.collection.objects.link(ob)
    ob.location = (cx, 4, cz)
    ob.rotation_euler = _look(ob.location, (cx, 0, cz))
    sc.camera = ob
    sc.render.engine, sc.cycles.device, sc.cycles.samples, sc.cycles.use_denoising = "CYCLES", "CPU", samples, True
    sc.render.film_transparent = True
    sc.render.resolution_x, sc.render.resolution_y = 1500, 250 * rows
    out = os.path.join(ROOT, "out", "renders", "materials.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sc.render.filepath = out
    bpy.ops.render.render(write_still=True)
    return [(n, f) for n, f in names], cols


def V(x, y, z):
    return (x * MM, y * MM, z * MM)


def mesh_obj(name, verts, faces, material, smooth=True, bevel=0.0, sharp=True):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    me.materials.append(material)
    if smooth:
        me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
        try:
            if sharp:
                me.set_sharp_from_angle(angle=math.radians(38))
        except Exception:
            pass
    if bevel:
        md = ob.modifiers.new("bevel", "BEVEL")
        md.width, md.segments, md.limit_method = bevel * MM, 3, "ANGLE"
    return ob


def tube(name, pts, radius, material, cyclic=False, caps=True):
    """A round tube along 3D points (mm)."""
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth, cu.bevel_resolution, cu.use_fill_caps = radius * MM, 6, caps
    sp = cu.splines.new("POLY")
    sp.points.add(len(pts) - 1)
    for p, q in zip(sp.points, pts):
        p.co = (*V(*q), 1)
    sp.use_cyclic_u = cyclic
    ob = bpy.data.objects.new(name, cu)
    bpy.context.collection.objects.link(ob)
    cu.materials.append(material)
    return ob


def smooth_tube(name, pts, radius, material):
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth, cu.bevel_resolution, cu.use_fill_caps = radius * MM, 6, True
    sp = cu.splines.new("NURBS")
    sp.points.add(len(pts) - 1)
    for p, q in zip(sp.points, pts):
        p.co = (*V(*q), 1)
    sp.use_endpoint_u, sp.order_u = True, 3
    cu.resolution_u = 10
    ob = bpy.data.objects.new(name, cu)
    bpy.context.collection.objects.link(ob)
    cu.materials.append(material)
    return ob


def prism(name, poly, y0, y1, material, bevel=2.0):
    """Extrude a side-view polygon (x, z in mm) between two y planes."""
    n = len(poly)
    verts = [V(p[0], y0, p[1]) for p in poly] + [V(p[0], y1, p[1]) for p in poly]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, 2 * n))]
    faces += [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    return mesh_obj(name, verts, faces, material, True, bevel)


def pillow(name, poly, y0, y1, material, inset=14.0, zone=None, both=True, steps=5):
    """A prism with rounded shoulders instead of flat sides: the outline is drawn in towards its
    middle by up to inset mm over the last zone mm before each end (only the y1 end if not both),
    on an elliptical curve. Makes cast and moulded parts read as shaped, not sawn from sheet."""
    xs, zs = [q[0] for q in poly], [q[1] for q in poly]
    cx, cz = (min(xs) + max(xs)) / 2, (min(zs) + max(zs)) / 2
    wx, wz = max(xs) - min(xs), max(zs) - min(zs)
    inset = min(inset, 0.3 * min(wx, wz))
    span = abs(y1 - y0)
    zone = min(zone if zone else inset * 1.6, span / (2.0 if both else 1.0))
    sgn = 1 if y1 > y0 else -1
    layers = []   # (y, inset there)
    if both:
        for k in range(steps, 0, -1):
            t = k / steps
            layers.append((y0 + sgn * zone * (1 - math.sin(t * math.pi / 2)) , inset * (1 - math.cos(t * math.pi / 2))))
    layers.append((y0 + (sgn * zone if both else 0), 0.0))
    for k in range(0, steps + 1):
        t = k / steps
        layers.append((y1 - sgn * zone * (1 - math.sin(t * math.pi / 2)), inset * (1 - math.cos(t * math.pi / 2))))
    n, verts, faces = len(poly), [], []
    for y, d in layers:
        fx, fz = 1 - 2 * d / max(wx, 1.0), 1 - 2 * d / max(wz, 1.0)
        verts += [V(cx + (q[0] - cx) * fx, y, cz + (q[1] - cz) * fz) for q in poly]
    for li in range(len(layers) - 1):
        a, b = li * n, (li + 1) * n
        faces += [(a + i, a + (i + 1) % n, b + (i + 1) % n, b + i) for i in range(n)]
    faces += [tuple(range(n - 1, -1, -1)), tuple((len(layers) - 1) * n + i for i in range(n))]
    ob = mesh_obj(name, verts, faces, material, True, 0)
    ob.data.polygons.foreach_set("use_smooth", [True] * len(ob.data.polygons))
    try:
        ob.data.set_sharp_from_angle(angle=math.radians(50))
    except Exception:
        pass
    bm_fix_normals(ob)
    return ob


def _inside(poly, x, z):
    hit = False
    for a, b in zip(poly, poly[1:] + poly[:1]):
        if (a[1] > z) != (b[1] > z) and x < a[0] + (b[0] - a[0]) * (z - a[1]) / (b[1] - a[1]):
            hit = not hit
    return hit


def skin(name, poly, f, side, material, stations=70, rows=16, thick=7.0, holes=()):
    """A thin shell whose side view is the outline poly and whose surface sits at y = side * f(x, z):
    a fairing side, drawn as one flat outline, that wraps out round the engine and closes in again
    towards the nose and keel. Built as an even grid between the outline's top and bottom lines."""
    xs = [q[0] for q in poly]
    x0, x1 = min(xs), max(xs)
    cols = []
    for i in range(stations + 1):
        x = x0 + (x1 - x0) * (0.004 + 0.992 * i / stations)
        tb = shapes.top_bottom(poly, x)
        if tb:
            cols.append((x, tb[0], tb[1]))
    for _ in range(2):          # ease small steps in the edge lines
        prev = cols[:]
        cols = [prev[0]] + [(prev[k][0], 0.25 * prev[k - 1][1] + 0.5 * prev[k][1] + 0.25 * prev[k + 1][1],
                             0.25 * prev[k - 1][2] + 0.5 * prev[k][2] + 0.25 * prev[k + 1][2]) for k in range(1, len(prev) - 1)] + [prev[-1]]
    verts, faces = [], []
    for x, zt, zb in cols:
        for r in range(rows + 1):
            z = zb + (zt - zb) * r / rows
            verts.append(V(x, side * f(x, z), z))
    n = rows + 1
    for c in range(len(cols) - 1):
        for r in range(rows):
            a = c * n + r
            faces.append((a, a + 1, a + n + 1, a + n) if side > 0 else (a, a + n, a + n + 1, a + 1))
    ob = mesh_obj(name, verts, faces, material, True, 0, sharp=False)
    sol = ob.modifiers.new("shell", "SOLIDIFY")
    sol.thickness, sol.offset = thick * MM, -1
    if holes:      # openings cut clean through the shell: vents, air channels, engine reveals
        bpy.context.view_layer.objects.active = ob
        bpy.ops.object.modifier_apply(modifier=sol.name)
        for n_, h in enumerate(holes):
            cutter = prism(f"{name}cut{n_}", list(h), side * 30, side * 460, material, 0)
            bm_fix_normals(cutter)      # outlines are drawn either way round
            bo = ob.modifiers.new(f"cut{n_}", "BOOLEAN")
            bo.operation, bo.object, bo.solver = "DIFFERENCE", cutter, "EXACT"
            bpy.context.view_layer.objects.active = ob
            bpy.ops.object.modifier_apply(modifier=bo.name)
            bpy.data.objects.remove(cutter, do_unlink=True)
    return ob


def bm_fix_normals(ob):
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(ob.data)
    bm.free()


def revolve(name, profile, centre, material, segs=72, a0=0.0, a1=360.0, bevel=0.0):
    """Revolve a closed profile of (radius, y) in mm about the wheel axis through centre (x, z)."""
    full = abs(a1 - a0) >= 359.9
    k = segs if full else segs + 1
    verts, faces = [], []
    for i in range(k):
        a = math.radians(a0 + (a1 - a0) * i / segs)
        for r, y in profile:
            verts.append(V(centre[0] + r * math.cos(a), y, centre[1] + r * math.sin(a)))
    m = len(profile)
    for i in range(segs):
        i2 = (i + 1) % k if full else i + 1
        for j in range(m):
            j2 = (j + 1) % m
            faces.append((i * m + j, i2 * m + j, i2 * m + j2, i * m + j2))
    if not full:
        faces += [tuple(range(m - 1, -1, -1)), tuple((k - 1) * m + j for j in range(m))]
    return mesh_obj(name, verts, faces, material, True, bevel)


def cone(name, p0, p1, r0, r1, material, segs=28):
    """A round taper between two 3D points (mm), radius r0 at p0 and r1 at p1, ends closed."""
    from mathutils import Vector
    a, b = Vector(p0), Vector(p1)
    ax = (b - a).normalized()
    side = ax.cross(Vector((0, 0, 1)))
    if side.length < 1e-4:
        side = ax.cross(Vector((0, 1, 0)))
    side.normalize()
    up = side.cross(ax)
    verts = []
    for c, r in ((a, r0), (b, r1)):
        for k in range(segs):
            t = 2 * math.pi * k / segs
            q = c + (side * math.cos(t) + up * math.sin(t)) * r
            verts.append(V(q.x, q.y, q.z))
    faces = [(k, (k + 1) % segs, segs + (k + 1) % segs, segs + k) for k in range(segs)]
    faces += [tuple(range(segs - 1, -1, -1)), tuple(range(segs, 2 * segs))]
    ob = mesh_obj(name, verts, faces, material, True, 0)
    bm_fix_normals(ob)
    return ob


def unit(a_deg):
    return (math.cos(math.radians(a_deg)), math.sin(math.radians(a_deg)))


def add(p, q, k=1.0):
    return (p[0] + q[0] * k, p[1] + q[1] * k)


TYRE_LOD = {"lod": "lod0"}   # the game export uses lod1 to keep the file light


def game_tyre(tag, c, w, hw):
    """The tyre built from the game's own tread section (Data/Parts/Redrawn/tyre/<tread>.glb, drawn from
    real tyres), bent round the hoop exactly as Fit/BikeParts.cpp does it. False if there is no section."""
    import struct
    tread = {"knobbly": "knobs"}.get(w.get("tread"), w.get("tread"))      # the game's name for the MX tread
    path = os.path.join(os.path.dirname(ROOT), "Data", "Parts", "Redrawn", "tyre", f"{tread}.glb")
    if not tread or not os.path.exists(path):
        return False
    raw = open(path, "rb").read()
    n = struct.unpack("<I", raw[12:16])[0]
    j = json.loads(raw[20:20 + n])
    blob = raw[20 + n + 8:]
    node = next(x for x in j["nodes"] if x["name"] == TYRE_LOD["lod"])
    prim = j["meshes"][node["mesh"]]["primitives"][0]

    def acc(i, fmt, size):
        a = j["accessors"][i]
        bv = j["bufferViews"][a["bufferView"]]
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = bv.get("byteStride", size)
        return [struct.unpack_from(fmt, blob, off + k * stride) for k in range(a["count"])]
    pos = acc(prim["attributes"]["POSITION"], "<3f", 12)
    idx = [v[0] for v in acc(prim["indices"], "<H", 2)]
    sections = {"sport": 10, "touring": 12, "sipes": 12, "cruiser": 16, "trail": 14, "blocks": 24}.get(tread, 32)
    rt, rr = w["tyre_r"], w["rim_r"]
    bead, base, span, step = min(hw * 0.8 - 4, hw * 0.9), rr - 6, max(rt - rr, 20) + 6, 2 * math.pi / sections
    verts, faces = [], []
    # a front tyre is fitted the other way round to a rear: its grooves run from the centre outwards and DOWN on
    # the face you see from the front, where a rear's lead with the point of the V (beenster's reference, 16 Nov)
    flip = tag == "front" and tread not in ("knobs", "blocks", "studs")
    for k in range(sections):
        o = len(verts)
        for x, y, z in pos:
            a_ = (k - z if flip else k + z) * step
            r_ = base + y * span
            lat = x * hw
            if y < 0.434 and abs(x) >= 0.9:
                lat = math.copysign(bead + (hw - bead) * min(1.0, max(0.0, (abs(x) - 0.9) / 0.1)), x)
            verts.append(V(c[0] + r_ * math.cos(a_), lat, c[1] + r_ * math.sin(a_)))
        faces += [(o + idx[t], o + idx[t + 1], o + idx[t + 2]) for t in range(0, len(idx), 3)]
    ob = mesh_obj(tag + "_tyre", verts, faces, mat("rubber"), True)
    bm_fix_normals(ob)
    return True


def is_belly(ex, i):
    """A sport exhaust's first "can" low under the engine is a collector box on the centre line."""
    cans = ex.get("cans", [])
    return i == 0 and len(cans) > 1 and not cans[0].get("nip") and cans[0]["a"][1] < 300 and cans[0]["w"] >= 130


def arm_sides(model):
    """Which sides the swingarm has a leg on (beenster, 16 Nov): two on every bike, but a chain-drive sport or naked
    bike that was drawn with a one-legged arm keeps it, on the chain side, and carries its rear brake there too."""
    sw = model["swingarm"]
    one = model["family"].lower() in ("sport", "naked") and sw.get("cls") not in ("alu", "chromef") and not sw.get("shaft") and not sw.get("rigid")
    return (1,) if one else (1, -1)


def boxer_y(eng, y):
    """Across-the-bike position on a boxer's cylinder: the drawing's 110..372 scaled to the model's reach."""
    return 110 + (y - 110) * (eng["boxer"].get("reach", 298) - 110) / 262.0


def inline_cyls(model, i, wide):
    """Which cylinders of an in-line engine feed header pipe i: all of them into one pipe, or with two drawn
    pipes on a four the right-hand pair into the first and the left-hand pair into the second (4 into 2)."""
    ys = inline_ys(model["engine"], wide)
    hs = [h for h in model["exhaust"]["headers"] if not h.get("link")]
    if len(hs) == 2 and len(ys) == 4:
        ys = sorted(ys)
        return ys[:2] if i == 0 else ys[2:]
    return ys if i == 0 else []


def inline_ys(eng, wide):
    """Where the cylinders of an in-line engine stand across the bike (mm from the centre line)."""
    n = {"inline4": 4, "triple": 3}.get(eng["layout"], 0)
    pitch = 2 * (wide / 2 - 44) / n if n else 0
    return [(k - (n - 1) / 2) * pitch for k in range(n)]


def can_y(ex, i, under):
    """Where a silencer sits across the bike at its two ends (mm from the centre line)."""
    if is_belly(ex, i):
        return (0, 0)
    if ex.get("can_y"):
        return tuple(ex["can_y"])      # the model places its own silencer (a slim one tucked in over the swingarm)
    if is_belly(ex, 0):
        return (128, 172)       # a stubby outlet angled out from the box, ahead of the rear tyre
    return (58, 58) if under else (218, 228)


def wheel(tag, c, w, width, rim_col, discs):
    rt, rr, hw = w["tyre_r"], w["rim_r"], width / 2
    prof = [(rr + 4, hw * 0.78), (rr + 30, hw * 0.98), (rt - 42, hw), (rt - 16, hw * 0.78), (rt - 3, hw * 0.4), (rt, 0),
            (rt - 3, -hw * 0.4), (rt - 16, -hw * 0.78), (rt - 42, -hw), (rr + 30, -hw * 0.98), (rr + 4, -hw * 0.78), (rr - 4, 0)]
    if not game_tyre(tag, c, w, hw):
        revolve(tag + "_tyre", prof, c, mat("rubber"))
    for kv in filter(None, os.environ.get("BIKE_BESPOKE", "").split(",")):
        if kv.split("=")[0] == "wheels":          # a trial fit: BIKE_BESPOKE="wheels=billet_reaper5:chrome"
            st_ = kv.split("=")[1].split(":")
            w = dict(w, style=st_[0], **({"finish": st_[1]} if len(st_) > 1 else {}))
    if DETAIL["on"] and w.get("style"):
        # a wheel from the library (wheels3d.py) at this bike's rim size; the bike keeps its tyre and discs
        import wheels3d
        wheels3d.fit(sys.modules[__name__], tag, c, w, width)
        return wheel_discs(tag, c, w, discs)
    rw = hw * 0.8
    rprof = [(rr + 8, rw), (rr + 8, rw - 8), (rr - 8, rw - 14), (rr - 22, 0), (rr - 8, -rw + 14), (rr + 8, -rw + 8), (rr + 8, -rw), (rr - 26, -rw + 6), (rr - 34, 0), (rr - 26, rw - 6)]
    revolve(tag + "_rim", rprof, c, mat("rim", rim_col))
    n = w.get("spokes", 5)
    if w.get("wire"):
        verts, faces = [], []
        for k in range(36):
            a_, b_ = math.radians(k * 10), math.radians(k * 10 + (28 if k % 2 else -28))
            yy = (hw * 0.45) * (1 if (k // 2) % 2 else -1)
            base = len(verts)
            for da in (-1.3, 1.3):
                verts.append(V(c[0] + 46 * math.cos(a_) - da * math.sin(a_), yy, c[1] + 46 * math.sin(a_) + da * math.cos(a_)))
                verts.append(V(c[0] + (rr - 22) * math.cos(b_) - da * math.sin(b_), yy * 0.15, c[1] + (rr - 22) * math.sin(b_) + da * math.cos(b_)))
            faces.append((base, base + 1, base + 3, base + 2))
        if DETAIL["on"] and w.get("spoke_fat"):
            # heavy-gauge round spokes (beenster, MX round 1): each one a real rod from the hub flange to a nipple at the rim
            sr = float(w["spoke_fat"])
            for k in range(36):
                a_, b_ = math.radians(k * 10), math.radians(k * 10 + (28 if k % 2 else -28))
                yy = (hw * 0.45) * (1 if (k // 2) % 2 else -1)
                p0 = (c[0] + 46 * math.cos(a_), yy, c[1] + 46 * math.sin(a_))
                p1 = (c[0] + (rr - 24) * math.cos(b_), yy * 0.15, c[1] + (rr - 24) * math.sin(b_))
                tube(f"{tag}_wire{k}", [p0, p1], sr, mat("steel"), caps=False)
                pn = tuple(p1[j] + (p0[j] - p1[j]) * 0.07 for j in range(3))
                cone(f"{tag}_nipple{k}", p1, pn, sr * 2.1, sr * 1.7, mat("alu"), 6)
        else:
            mesh_obj(tag + "_wires", verts, faces, mat("steel"), smooth=False)
        n = 0
    elif w.get("straight"):
        sw_ = w.get("spoke_w", 4)
        for k in range(n):
            a0 = 360 / n * k + w.get("phase", 12)
            quad = [add(c, unit(a0 - sw_ * 2.2), 50), add(c, unit(a0 - sw_ * 0.5), rr - 24), add(c, unit(a0 + sw_ * 0.5), rr - 24), add(c, unit(a0 + sw_ * 2.2), 50)]
            pillow(f"{tag}_spoke{k}", quad, -16, 16, mat("rim", rim_col), 5, 12)
        n = 0
    for k in range(n):
        a0 = 360 / n * k + w.get("phase", 12)
        for side in (-1, 1):
            a_h, a_r = a0 + side * 16, a0 + side * 9 + 20
            quad = [add(c, unit(a_h - 7), 50), add(c, unit(a_r - 3.2), rr - 24), add(c, unit(a_r + 3.2), rr - 24), add(c, unit(a_h + 7), 50)]
            pillow(f"{tag}_spoke{k}{side}", quad, -15, 15, mat("rim", rim_col), 5, 12)
    revolve(tag + "_hub", [(18, hw * 0.7), (56, hw * 0.55), (56, -hw * 0.55), (18, -hw * 0.7)], c, mat("rim", rim_col), 32)
    wheel_discs(tag, c, w, discs)


def wheel_discs(tag, c, w, discs):
    d = w.get("disc")
    for y in discs if d else []:
        revolve(f"{tag}_disc{y}", [(d, y + 2.5), (d, y - 2.5), (d - 36, y - 2.5), (d - 36, y + 2.5)], c, mat("steel"), 64)
        # drilled holes: dark dots on both faces, two staggered rings
        verts, faces = [], []
        for ring_r, n_, ph in ((d - 11, 30, 0.0), (d - 24, 30, 0.5)):
            for k in range(n_):
                a_ = 2 * math.pi * (k + ph) / n_
                hx, hz = c[0] + ring_r * math.cos(a_), c[1] + ring_r * math.sin(a_)
                for yy in (y + 2.8, y - 2.8):
                    base = len(verts)
                    verts += [V(hx + 3.6 * math.cos(t * math.pi / 4), yy, hz + 3.6 * math.sin(t * math.pi / 4)) for t in range(8)]
                    faces.append(tuple(range(base, base + 8)))
        mesh_obj(f"{tag}_discholes{y}", verts, faces, mat("plastic"), smooth=False)
        revolve(f"{tag}_carrier{y}", [(d - 34, y + 3), (d - 34, y - 3), (d - 72, y - 3), (d - 72, y + 3)], c, mat("alu_dark"), 48)


def loft(name, outline, spec, material, stations=96, ring=44, smooth_lines=True):
    """A body part with real width: superellipse sections between the outline's top and bottom lines."""
    xs = [p[0] for p in outline]
    x0, x1 = min(xs), max(xs)
    rows = []
    # sample the outline and the width table, then smooth all three lines along the part so that a
    # corner in the drawing or a kink in the table never shows as a band across the surface
    samp = []
    for i in range(stations):
        u = (i + 0.5) / stations
        tb = shapes.top_bottom(outline, x0 + (x1 - x0) * u)
        if tb and tb[0] - tb[1] >= 2:
            samp.append([u, tb[0], tb[1], shapes.lerp_table(spec["w"], u)])
    for k_ in range(30):
        prev = [r_[:] for r_ in samp]
        for i in range(1, len(samp) - 1):
            # the width line is smoothed much harder than the drawn top and bottom lines, so the
            # plan view is one fair curve with no corner where two rows of the width table meet
            for q in ((1, 2, 3) if smooth_lines and k_ < 24 else (3,)):
                samp[i][q] = 0.25 * prev[i - 1][q] + 0.5 * prev[i][q] + 0.25 * prev[i + 1][q]
    for u, zt, zb, w in samp:
        x = x0 + (x1 - x0) * u
        tb = (zt, zb)
        zc, h = (tb[0] + tb[1]) / 2, (tb[0] - tb[1]) / 2
        edge = min(1.0, min(u, 1 - u) / 0.1)      # round the two ends off as part of an ellipse
        if not (spec.get("blunt_rear") and u < 0.5):      # blunt_rear: the rear end is cut off square, for a lamp let into it
            w *= 0.3 + 0.7 * math.sqrt(1.0 - (1.0 - edge) ** 2)
        row = []
        for j in range(ring):
            th = 2 * math.pi * j / ring
            cs, sn = math.cos(th), math.sin(th)
            e = 2.0 / (spec["nb"] if sn < 0 and spec.get("nb") else spec["n"])      # nb: a boxier underside
            y = w * math.copysign(abs(cs) ** e, cs)
            z = zc + h * math.copysign(abs(sn) ** e, sn)
            if sn < 0:
                y *= 1 - (1 - spec["bottom"]) * abs(sn)
                if spec.get("base"):
                    # the base flares out to a given half-width (a tank's skirt, sitting on the frame spars)
                    tgt = spec["base"](x) * max(0.0, min(1.0, (u - 0.03) / 0.22)) * max(0.0, min(1.0, (1 - u) / 0.1)) ** 0.5
                    if tgt > w:
                        y *= 1 + (tgt / w - 1) * min(1.0, abs(sn) / 0.6) ** 1.5
            kc = spec.get("knee")
            if kc and 0.08 < sn < 0.72 and kc[0] < u < kc[1]:
                # a knee recess let into the flank: two clean edges along the side of the tank
                fall = math.sin(math.pi * (u - kc[0]) / (kc[1] - kc[0])) ** 0.6
                y *= 1 - kc[2] * fall * math.sin(math.pi * (sn - 0.08) / 0.64) ** 0.7
            row.append(V(x, y, z))
        rows.append(row)
    verts = [v for r in rows for v in r]
    faces = []
    for i in range(len(rows) - 1):
        for j in range(ring):
            j2 = (j + 1) % ring
            faces.append((i * ring + j, i * ring + j2, (i + 1) * ring + j2, (i + 1) * ring + j))
    faces.append(tuple(range(ring - 1, -1, -1)))
    faces.append(tuple((len(rows) - 1) * ring + j for j in range(ring)))
    ob = mesh_obj(name, verts, faces, material, True, 0, sharp=False)
    md = ob.modifiers.new("sub", "SUBSURF")
    md.levels = md.render_levels = 1
    return ob


def arc_band(name, a, hw, material):
    """A mudguard: a thin crowned shell that wraps over the tyre and a little way down its shoulders.
    The same section runs its whole length: the ends are a clean edge, rounded in plan, and do not
    curl down towards the tyre (beenster, 14 Nov)."""
    lo, hi, n, segs = a["r0"] - 14, a["r1"] + 4, 9, 44
    c = tuple(a["c"])
    span = abs(a["a1"] - a["a0"])
    tip = min(0.2, math.degrees(hw * 0.55 / hi) / span)     # share of the arc taken by each rounded corner
    verts, faces, m = [], [], 2 * n
    for i in range(segs + 1):
        f = i / segs
        e = min(f, 1 - f) / tip
        k = 0.72 + 0.28 * math.sqrt(max(0.0, 1 - (1 - min(1.0, e)) ** 2))   # plan outline only
        ang = math.radians(a["a0"] + (a["a1"] - a["a0"]) * f)
        outer = [(lo + (hi - lo) * (math.cos(math.radians(u)) ** 0.7), hw * k * math.sin(math.radians(u))) for u in (-88 + 176 * q / (n - 1) for q in range(n))]
        inner = [(r - 3.5, y * 0.96) for r, y in outer]
        for r, y in outer[::-1] + inner:
            verts.append(V(c[0] + r * math.cos(ang), y, c[1] + r * math.sin(ang)))
    for i in range(segs):
        for q in range(m):
            q2 = (q + 1) % m
            faces.append((i * m + q, (i + 1) * m + q, (i + 1) * m + q2, i * m + q2))
    faces += [tuple(range(m - 1, -1, -1)), tuple(segs * m + q for q in range(m))]
    ob = mesh_obj(name, verts, faces, material, True, 0)
    bm_fix_normals(ob)
    return ob


def guard_blade(name, a, blade, hw, material, tight=False):
    """A sport front mudguard and its fork blades as ONE moulding (beenster, 14 Nov): the crowned shell
    over the tyre, with its two side edges drawn down into the blades that run to the axle. How far
    down the side goes at each point round the wheel is read off the drawn blade outline, then faired
    so the blade grows out of the guard in a curve."""
    lo, hi, n, segs, K = a["r0"] - 14, a["r1"] + 4, 25 if DETAIL["on"] else 9, 80, 12
    if tight and DETAIL["on"]:
        hi = tight + 11      # the crown a few mm off the tread, the edges following the tyre's shoulder down (beenster, sport round 2)
    c = tuple(a["c"])

    def mould(u, f):
        """Definition pass (reference: front mudguard moulding): a raised spine down the middle that is
        tallest over the fork and dies away to the tips, and a pressed shoulder line each side of it."""
        if not DETAIL["on"]:
            return 0.0
        along = math.sin(math.pi * min(1.0, max(0.0, f))) ** 0.5
        spine = 7.5 * math.exp(-(u / 15.0) ** 2) * along
        return 0.0 * spine      # plain smooth shell: the spine and pressed shoulder read as a lip (beenster, 15 Nov)

    def depth(deg):
        th, best = math.radians(deg), (hi if DETAIL["on"] else lo - 8)     # Definition pass: no skirt at all away from the blades
        for rho in range(int(hi), 24, -3):
            if DETAIL["on"]:
                break      # Definition pass: the shell has no skirt; the fork shrouds are their own shape (detail3d.fork_shrouds)
            if _inside(blade, c[0] + rho * math.cos(th), c[1] + rho * math.sin(th)):
                best = min(best, rho)
        return best
    rin = [depth(a["a0"] + (a["a1"] - a["a0"]) * i / segs) for i in range(segs + 1)]
    for _ in range(5):
        rin = [rin[0]] + [0.25 * rin[i - 1] + 0.5 * rin[i] + 0.25 * rin[i + 1] for i in range(1, segs)] + [rin[segs]]
    span = abs(a["a1"] - a["a0"])
    tip = min(0.2, math.degrees(hw * 0.55 / hi) / span)
    verts, faces = [], []
    for i in range(segs + 1):
        f = i / segs
        k = 0.72 + 0.28 * math.sqrt(max(0.0, 1 - (1 - min(1.0, min(f, 1 - f) / tip)) ** 2))
        if DETAIL["on"]:
            # the front nose is rounded off in plan, a half oval and not a square-cut end (beenster, 16 Nov: ALL front mudguards)
            ff = f if math.cos(math.radians(a["a0"])) > math.cos(math.radians(a["a1"])) else 1 - f
            nose_f = math.degrees(hw * 1.15 / hi) / span
            if ff < nose_f:
                k = max(0.06, math.sqrt(max(0.0, 1 - (1 - ff / nose_f) ** 2)))
        ang = math.radians(a["a0"] + (a["a1"] - a["a0"]) * f)
        # Definition pass: the edges of the shell just end, following the tyre; they do not curl in to a lip
        edge = hi - ((8 + 20 * min(1.0, min(f, 1 - f) / 0.3)) if tight else (5 + 12 * min(1.0, min(f, 1 - f) / 0.3))) if DETAIL["on"] else lo     # slim towards both tips
        if DETAIL["on"]:
            c70 = math.cos(math.radians(70))
            crown = [(edge + (hi - edge) * (math.cos(math.radians(u)) - c70) / (1 - c70), hw * k * math.sin(math.radians(u)) / math.sin(math.radians(70))) for u in (-70 + 140 * q / (n - 1) for q in range(n))]
        else:
            crown = [(lo + (hi - lo) * (math.cos(math.radians(u)) ** 0.7) + mould(u, f), hw * k * math.sin(math.radians(u))) for u in (-88 + 176 * q / (n - 1) for q in range(n))]
        ye = crown[-1][1]
        skirt = []
        for q in range(1, K + 1):
            rho = edge + (min(rin[i], edge) - edge) * q / K
            t_ = min(1.0, (edge - rho) / 95.0)
            skirt.append((rho, ye + (144 - ye) * t_ * t_ * (3 - 2 * t_)))
        sec = [(r, -y) for r, y in skirt[::-1]] + crown + skirt
        for r, y in sec:
            verts.append(V(c[0] + r * math.cos(ang), y, c[1] + r * math.sin(ang)))
    m = n + 2 * K
    for i in range(segs):
        for q in range(m - 1):
            faces.append((i * m + q, (i + 1) * m + q, (i + 1) * m + q + 1, i * m + q + 1))
    ob = mesh_obj(name, verts, faces, material, True, 0, sharp=False)
    sol = ob.modifiers.new("shell", "SOLIDIFY")
    sol.thickness, sol.offset = 4 * MM, 1
    return ob


def build(model):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    _mats.clear()
    FINISH["name"] = model.get("finish") or LIB["brand_finish"].get(model.get("brand"), "gloss")
    g, wh, col = model["geometry"], model["wheels"], model["colours"]
    body_c, frame_c, rim_c = hex_rgb(col["body"]), hex_rgb(col["frame"]), hex_rgb(col.get("rim", "#2b2d31"))
    paint, framem = mat("paint", body_c), mat("frame", frame_c)
    if model["frame"].get("chrome"):
        framem = mat("chrome")       # a show chopper's frame is plated (beenster, chopper round 1)
    wb, rake = g["wheelbase"], math.radians(g["rake"])
    A, F = (0, wh["rear"]["tyre_r"]), (wb, wh["front"]["tyre_r"])
    uf, nb = (-math.sin(rake), math.cos(rake)), (-math.cos(rake), -math.sin(rake))
    fk = model["fork"]
    top = add(F, uf, fk["length"])
    head_t, head_b = add(add(F, uf, fk["length"] - 22), nb, fk["offset"]), add(add(F, uf, fk["lower_yoke"] + 18), nb, fk["offset"])

    # wheels
    wheel("front", F, wh["front"], wh["front"].get("width", 120), rim_c, (-78, 78))
    wheel("rear", A, wh["rear"], wh["rear"].get("width", 180), rim_c, (-82,))

    # fork legs, yokes, axle
    forkm = mat("gold") if col.get("fork", "").lower() == "#b8975a" else mat("alu_dark")
    stan = add(F, uf, fk["slider_top"])
    for s in (-1, 1):
        y = s * 105
        tube(f"stanchion{s}", [(add(F, uf, 40)[0], y, add(F, uf, 40)[1]), (stan[0], y, stan[1])], 21.5, mat("chrome"))
        tube(f"forktube{s}", [(stan[0], y, stan[1]), (top[0], y, top[1])], 29, forkm)
        foot = add(F, uf, -30)
        tube(f"forkfoot{s}", [(foot[0], y, foot[1]), (add(F, uf, 62)[0], y, add(F, uf, 62)[1])], 31, mat("alu_dark"))
    tube("axle_f", [(F[0], -125, F[1]), (F[0], 125, F[1])], 12, mat("steel"))
    for at, th in ((fk["length"] - 22, 22), (fk["lower_yoke"], 26)):
        c = add(F, uf, at)
        a, b = add(c, nb, -40), add(c, nb, fk["offset"] + 42)
        n2 = (uf[0] * th, uf[1] * th)
        prism(f"yoke{at}", [add(a, n2, 1), add(b, n2, 1), add(b, n2, -1), add(a, n2, -1)], -135, 135, mat("alu_dark"), 6)
    tube("head", [(head_t[0], 0, head_t[1]), (head_b[0], 0, head_b[1])], 31, framem)
    cal = model.get("front_caliper")
    if cal:
        cp = shapes.dense(cal, True, 0.8)
        for s in (-1, 1):
            pillow(f"caliper{s}", cp, s * 62, s * 104, mat("gold") if forkm.name.startswith("gold") else mat("alu_dark"), 8, 14)

    rc = model.get("rear_caliper")
    if rc:
        # on the disc side of the rear wheel, on a carrier plate from the axle
        pillow("rearcaliper", shapes.dense(rc["body"], True, 0.8), -106, -60, mat("gold") if forkm.name.startswith("gold") else mat("alu_dark"), 8, 12)
        prism("rearcarrier", [tuple(q[:2]) for q in rc["arm"]], -100, -92, mat("alu_dark"), 2)

    # frame tubes: half-width grows from the head back to the pivot
    fr = model["frame"]
    nodes = {k: tuple(v) for k, v in fr["nodes"].items()}
    nodes.update(head_t=head_t, head_b=head_b)

    def half(p):
        u = max(0.0, min(1.0, (head_t[0] - p[0]) / max(1.0, head_t[0] - 600)))
        if DETAIL["on"]:
            # a real tube frame opens out quickly behind the steering head, so its sides pass outside the
            # airbox and the cylinder heads instead of through them
            return 30 + 110 * (1 - (1 - u) ** 2)
        return 22 + u * 118

    eng = model["engine"]
    wide = 420 if eng["layout"] in ("inline4", "triple") else 300
    # the seat rails run back inside the tail: they close in towards its end and are joined there by
    # a cross tube, so nothing is left as an open tube end
    sub_x = [nodes[n][0] for tb in fr["tubes"] if tb.get("cls") == "sub" for n in tb["path"]]
    x_end = min(sub_x) if sub_x else 0.0
    sub_w = fr.get("sub_y", 110)

    def sub_half(p):
        if p[0] >= 500 and "sub_y" not in fr:
            return half(p)
        u = max(0.0, min(1.0, (430 - p[0]) / max(1.0, 430 - x_end)))
        return sub_w + (50 - sub_w) * u

    frame_runs = [("head", [(head_t[0], 0, head_t[1]), (head_b[0], 0, head_b[1])], 31)]   # every tube as built, for the weld beads
    SUB_OUT = 132      # Definition pass: the seat rails stay outside the side panels as far as the pillion hanger
    for i, tb in enumerate(fr["tubes"]):
        pts = [nodes[n] for n in tb["path"]]
        sub = tb.get("cls") == "sub"
        for s in (-1, 1):
            p3 = [(p[0], s * (sub_half(p) if sub else half(p)), p[1]) for p in pts]
            if sub and DETAIL["on"] and fr.get("sub_show"):
                # the seat frame runs back in full view under the saddle, outside a slim tail (beenster,
                # adventure round 2), closing in a little to its end
                p3 = [(p[0], s * (half(p) if p[0] >= 500 else SUB_OUT - 28 * max(0.0, min(1.0, (430 - p[0]) / max(1.0, 430 - x_end)))), p[1]) for p in pts]
                tube(f"frame{i}{s}", p3, tb["w"] / 2, framem)
                frame_runs.append((f"frame{i}{s}", p3, tb["w"] / 2))
                continue
            if sub and DETAIL["on"] and "sub_y" not in fr:
                # the visible rails run back outside the panels and stop at the hanger node; from there the
                # rails carry on hidden inside the tail, so no tube cuts through the bodywork (beenster, round 4)
                z3 = nodes.get("S3", (0, 770))[1]
                first = [(p[0], s * (half(p) if p[0] >= 500 else (SUB_OUT if p[1] < z3 + 30 else 92)), p[1]) for p in pts[:2]]
                tube(f"frame{i}{s}", first, tb["w"] / 2, framem)
                frame_runs.append((f"frame{i}{s}", first, tb["w"] / 2))
                if len(pts) > 2:
                    tube(f"frame{i}{s}in", [(p[0], s * min(60, sub_half(p)), p[1]) for p in pts[1:]], tb["w"] / 2, framem)
                continue
            tube(f"frame{i}{s}", p3, tb["w"] / 2, framem)
            frame_runs.append((f"frame{i}{s}", p3, tb["w"] / 2))
        if sub:
            e_ = pts[-1]
            ye_ = SUB_OUT - 28 * max(0.0, min(1.0, (430 - e_[0]) / max(1.0, 430 - x_end))) if DETAIL["on"] and fr.get("sub_show") else sub_half(e_)
            tube(f"framecross{i}", [(e_[0], -ye_, e_[1]), (e_[0], ye_, e_[1])], tb["w"] / 2, framem)
    for i, pl in enumerate(fr.get("plates", [])):
        pp = shapes.dense(pl["pts"], True, pl.get("t", 0.6))
        xh = max(q[0] for q in pp)

        def spar_y(x, z, xh=xh):
            # a spar starts at the steering head and passes outside the cylinder head, never through it
            t = max(0.0, min(1.0, (xh - x) / 300.0))
            t = t * t * (3 - 2 * t)
            return 78 + (wide / 2 + 12 - 78) * t
        for s in (-1, 1):
            skin(f"spar{i}{s}", pp, spar_y, s, framem, thick=36.0)
    # engine
    crank = tuple(eng["crank"])
    pillow("crankcase", shapes.dense(eng["case"], True, 0.9), -wide / 2 + 10, wide / 2 - 10, mat("engine"), 30, 55)
    if eng["layout"] == "boxer":
        # a cylinder out each side: finned barrel, head and a valve cover whose end-on outline is eng["boxer_cover"]
        bc = eng["boxer"]
        cx_, cz_ = bc["at"]
        # bc["reach"] is how far the valve cover stands out from the centre line (mm)
        kb = (bc.get("reach", 298) - 110) / 262.0

        def by(y):
            return 110 + (y - 110) * kb
        for s in (-1, 1):
            cone(f"boxerbarrel{s}", (cx_, s * 110, cz_), (cx_, s * by(250), cz_), bc["r"], bc["r"], mat("engine"), 36)
            k = 122
            while k < 246:
                cone(f"boxerfin{s}{k}", (cx_, s * by(k), cz_), (cx_, s * (by(k) + 4), cz_), bc["r"] + 14, bc["r"] + 14, mat("engine"), 36)
                k += 18
            pillow(f"boxerhead{s}", shapes.dense(bc["cover"], True, 0.6), s * by(250), s * by(318), mat("engine"), 10, 18, both=False)
            pillow(f"boxercover{s}", [((q[0] - cx_) * 0.9 + cx_, (q[1] - cz_) * 0.86 + cz_) for q in shapes.dense(bc["cover"], True, 0.6)], s * by(316), s * by(372), mat("engine_dark"), 14, 30, both=False)
    for i, cy in enumerate(eng["cylinders"]):
        u = unit(cy["angle"])
        base, end = add(crank, u, cy["start"]), add(crank, u, cy["start"] + cy["length"])
        headend = add(end, u, cy["head"])
        if eng["layout"] in ("inline4", "triple"):
            n = (-u[1], u[0])
            hwc = cy["bore_w"] / 2
            pillow(f"block{i}", [add(base, n, hwc), add(end, n, hwc), add(end, n, -hwc), add(base, n, -hwc)], -wide / 2 + 30, wide / 2 - 30, mat("engine"), 8, 14)
            hwc += 13
            pillow(f"cylhead{i}", [add(end, n, hwc), add(headend, n, hwc), add(headend, n, -hwc), add(end, n, -hwc)], -wide / 2 + 25, wide / 2 - 25, mat("engine"), 10, 18)
            cov = add(headend, u, 30)
            pillow(f"cover{i}", [add(headend, n, hwc - 8), add(cov, n, hwc - 18), add(cov, n, -hwc + 18), add(headend, n, -hwc + 8)], -wide / 2 + 35, wide / 2 - 35, mat("engine_dark"), 10, 24)
        else:
            r = cy["bore_w"] / 2
            tube(f"barrel{i}", [(base[0], 0, base[1]), (end[0], 0, end[1])], r - 14, mat("engine"))
            if cy.get("fins", True):
                k = 14
                while k < cy["length"] - 6:
                    q0, q1 = add(base, u, k), add(base, u, k + 3)
                    tube(f"fin{i}_{k}", [(q0[0], 0, q0[1]), (q1[0], 0, q1[1])], r + 4, mat("engine"))
                    k += 13
            tube(f"cylhead{i}", [(end[0], 0, end[1]), (headend[0], 0, headend[1])], r + 12, mat("engine"))
            cov = add(headend, u, 28)
            tube(f"cover{i}", [(headend[0], 0, headend[1]), (cov[0], 0, cov[1])], r - 6, mat("engine_dark"))
    for i, cv in enumerate(eng.get("covers", [])):
        c = tuple(cv["c"])
        for sg in (1, -1):
            y0 = sg * (wide / 2 - 30)
            cone(f"sidecover{i}{sg}", (c[0], y0, c[1]), (c[0], y0 + sg * 34, c[1]), cv["r"], cv["r"] - 6, mat("engine_dark"), 48)
            cone(f"sidedome{i}{sg}", (c[0], y0 + sg * 34, c[1]), (c[0], y0 + sg * 50, c[1]), cv["r"] * 0.72, cv["r"] * 0.55, mat("engine"), 48)
            nb_ = cv.get("bolts", 6)
            for k in range(nb_):
                q = add(c, unit(k * 360 / nb_ + 15), cv["r"] * 0.86)
                cone(f"bolt{i}{sg}{k}", (q[0], y0 + sg * 30, q[1]), (q[0], y0 + sg * 40, q[1]), 6, 6, mat("steel"), 6)
        cs_ = tuple(eng["countershaft"])
        cone("sprocketcover", (cs_[0], wide / 2 - 40, cs_[1]), (cs_[0], wide / 2 + 6, cs_[1]), 52, 44, mat("engine_dark"), 32)
    for i, shp in enumerate(eng.get("extras", [])):
        hwa = 112
        if DETAIL["on"]:      # the airbox stays inside the frame rails
            hwa = max(44, min(112, min(half((x_, 0)) for x_ in (min(q[0] for q in shp["pts"]), max(q[0] for q in shp["pts"]))) + 14))
            hwa = min(hwa, half((max(q[0] for q in shp["pts"]) - 40, 0)) - 18) if hwa > 60 else hwa
        pillow(f"airbox{i}", shapes.dense(shp["pts"], True, shp.get("t", 0.9)), -hwa, hwa, mat("plastic"), min(26, hwa * 0.3), 45)
        if shp.get("cover_y") and DETAIL["on"]:
            # a side cover each side that stands outside the frame and hides it (beenster, rollout round 1)
            for s in (-1, 1):
                ya_, yb_ = sorted((s * shp["cover_y"][0], s * shp["cover_y"][1]))
                pillow(f"framecover{i}{s}", shapes.dense(shp["pts"], True, shp.get("t", 0.9)), ya_, yb_, mat("plastic"), 6, 14, both=False)
    r = model.get("radiator")
    if r:
        u = unit(r["angle"])
        n = (-u[1], u[0])
        a, b, hwc = tuple(r["at"]), add(tuple(r["at"]), u, r["len"]), r["w"] / 2
        prism("radiator", [add(a, n, hwc), add(b, n, hwc), add(b, n, -hwc), add(a, n, -hwc)], -150, 150, mat("plastic"), 4)
        # the core: a finned face front and back between the plastic end tanks, with a filler neck
        if n[0] < 0:
            n = (-n[0], -n[1])
        a2, b2 = add(a, u, 16), add(b, u, -16)
        prism("radcore", [add(a2, n, hwc + 3), add(b2, n, hwc + 3), add(b2, n, -hwc - 3), add(a2, n, -hwc - 3)], -128, 128, mat("alu_dark"), 1)
        k_ = 10.0
        while k_ < r["len"] - 40:
            for sg in (1, -1):
                q = add(add(a2, u, k_), n, sg * (hwc + 3.5))
                tube(f"radfin{int(k_)}{sg}", [(q[0], -126, q[1]), (q[0], 126, q[1])], 2.2, mat("alu"), caps=False)
            k_ += 11

    # exhaust on the right side
    ex = model["exhaust"]
    # ("side": "both" is one high silencer each side: the first header runs to the left one, the rest to the right)
    # "left" puts everything on the left, the swingarm side of a single-sided arm, so the open side
    # of the rear wheel stays on show; "under" carries two silencers side by side under the tail
    both, left, under = (ex.get("side") == k_ for k_ in ("both", "left", "under"))
    for i, hp in enumerate(ex["headers"]):
        pts = shapes.dense(hp["pts"], False, 1.0)[::3] + [tuple(hp["pts"][-1][:2])]
        sg = 1 if left or ((both or under) and i == 0) else -1
        out_y = 58 if under else 214      # side silencers sit outside the swingarm
        if is_belly(ex, 0):
            out_y = 0                     # the pipes gather into a box under the engine, on the centre line
        if ex.get("join") and ex.get("cans") and not is_belly(ex, 0):
            # the pipe arrives dead in line with its silencer's mouth (beenster, cruiser round 1)
            out_y = can_y(ex, min(i, len(ex["cans"]) - 1), under)[0]
        yy = [sg * (40 + (out_y - 40) * (j / (len(pts) - 1)) ** 1.5) for j in range(len(pts))]
        if ex.get("join"):
            yy = [sg * (40 + (out_y - 40) * (lambda t: t * t * (3 - 2 * t))(min(1.0, j / (0.8 * (len(pts) - 1))))) for j in range(len(pts))]
        if left:
            # stay tucked under the engine, then move out behind the sump and ahead of the swingarm,
            # so the pipe passes outside the arm and never through it
            def out(x):
                t = max(0.0, min(1.0, (700 - x) / 130.0))
                return t * t * (3 - 2 * t)
            yy = [sg * (40 + (out_y - 40) * out(p[0])) for p in pts]
        if hp.get("front"):
            # down the side of the engine: out past the cases straight away, then back to the silencer
            side_y = wide / 2 + 34
            yy = [sg * (60 + (side_y - 60) * min(1.0, j / (0.18 * (len(pts) - 1))) ** 0.7) for j in range(len(pts))]
            if hp.get("ease"):      # leaves the port dead in line with it, then swings out (beenster, classic round 2: no kink at the start)
                def ease(j):
                    t = min(1.0, j / (0.34 * (len(pts) - 1)))
                    return t * t * (3 - 2 * t)
                yy = [sg * (60 + (side_y - 60) * ease(j)) for j in range(len(pts))]
            yy = [sg * (abs(y) + (out_y - side_y) * max(0.0, j / (len(pts) - 1) - 0.75) / 0.25) for j, y in enumerate(yy)]
        if hp.get("ys"):
            # the model gives the pipe's own line across the bike, point for point (pipes that gather at the
            # front and run under the engine, a link pipe out to the silencer)
            ctl, n_c, yy = hp["pts"], len(hp["pts"]), []
            for p in pts:
                k_ = min(range(n_c - 1), key=lambda k: abs(math.hypot(p[0] - ctl[k][0], p[1] - ctl[k][1]) + math.hypot(p[0] - ctl[k + 1][0], p[1] - ctl[k + 1][1]) - math.hypot(ctl[k + 1][0] - ctl[k][0], ctl[k + 1][1] - ctl[k][1])))
                L_k = math.hypot(ctl[k_ + 1][0] - ctl[k_][0], ctl[k_ + 1][1] - ctl[k_][1]) or 1.0
                t_k = max(0.0, min(1.0, math.hypot(p[0] - ctl[k_][0], p[1] - ctl[k_][1]) / L_k))
                yy.append(hp["ys"][k_] + (hp["ys"][k_ + 1] - hp["ys"][k_]) * t_k)
            for _ in range(4):
                yy = [yy[0]] + [0.25 * yy[j - 1] + 0.5 * yy[j] + 0.25 * yy[j + 1] for j in range(1, len(yy) - 1)] + [yy[-1]]
        if DETAIL["on"] and eng["layout"] == "boxer" and not hp.get("link") and not hp.get("ys"):
            # a flat twin: each pipe starts under its own cylinder head. The left one swings in to the drawn line;
            # the right one drops the same way, then crosses in front of the sump and joins the left one
            port_y = boxer_y(eng, 284)
            sm_ = lambda t: t * t * (3 - 2 * t)
            j1 = max(3, int(0.3 * (len(pts) - 1)))
            lefty = [port_y + (abs(yy[j]) - port_y) * sm_(min(1.0, j / j1)) for j in range(len(pts))]
            rad = hp.get("w", 44) / 2 * 0.8
            if i == 0:
                smooth_tube(f"header{i}", [(p[0], sg * y, p[1]) for p, y in zip(pts, lefty)], rad, mat("exhaust"))
            else:
                j2 = min(range(len(pts)), key=lambda j: math.hypot(pts[j][0] - hp["pts"][2][0], pts[j][1] - hp["pts"][2][1]))
                j3 = min(len(pts) - 1, j2 + max(2, int(0.1 * (len(pts) - 1))))
                x2, z2 = pts[j2]
                path = [(pts[j][0], -sg * lefty[j], pts[j][1]) for j in range(j2 + 1)]
                ya, yb = -sg * lefty[j2], sg * lefty[j3]
                path += [(x2 - 14, ya + (yb - ya) * 0.16, z2 - 10), (x2 - 18, ya + (yb - ya) * 0.5, z2 - 12), ((x2 - 18 + pts[j3][0]) / 2, ya + (yb - ya) * 0.86, (z2 - 12 + pts[j3][1]) / 2), (pts[j3][0], yb, pts[j3][1])]
                smooth_tube(f"header{i}", path, rad, mat("exhaust"))
                if len(ex.get("cans", [])) > 1:
                    c1_ = ex["cans"][1]["a"]
                    jb = max(j3 + 1, len(pts) - 1 - max(2, int(0.16 * (len(pts) - 1))))
                    yc_ = sg * can_y(ex, 1, under)[0]
                    smooth_tube("headerbranch", [(pts[jb][0], sg * lefty[jb], pts[jb][1]), (pts[jb + 1][0], sg * lefty[jb + 1], pts[jb + 1][1] + 6), ((pts[-1][0] + c1_[0]) / 2 + 6, yc_, (pts[jb + 1][1] + c1_[1]) / 2 + 10), (c1_[0], yc_, c1_[1])], rad, mat("exhaust"))
                cone(f"headermerge{i}", (pts[j3][0], sg * lefty[j3], pts[j3][1]), (pts[min(len(pts) - 1, j3 + 1)][0], sg * lefty[min(len(pts) - 1, j3 + 1)], pts[min(len(pts) - 1, j3 + 1)][1]), rad * 1.25, rad, mat("exhaust"), 28)
            continue
        cyl_ys = inline_cyls(model, i, wide) if DETAIL["on"] and not hp.get("link") else []
        if cyl_ys:
            # an in-line engine: one pipe from every cylinder across the front of the head, gathering into the
            # drawn pipe a little under half way along it
            j0 = max(3, int(0.42 * (len(pts) - 1)))
            sm_ = lambda t: t * t * (3 - 2 * t)
            for k, yk in enumerate(cyl_ys):
                smooth_tube(f"header{i}_{k}", [(pts[j][0], yk + (yy[j] - yk) * sm_(j / j0), pts[j][1]) for j in range(j0 + 1)], hp.get("w", 44) / 2 * 0.8 * 0.6, mat("exhaust"))
            cone(f"headermerge{i}", (pts[j0 - 1][0], yy[j0 - 1], pts[j0 - 1][1]), (pts[j0][0], yy[j0], pts[j0][1]), hp.get("w", 44) / 2 * 0.8 * 1.25, hp.get("w", 44) / 2 * 0.8, mat("exhaust"), 28)
            smooth_tube(f"header{i}", [(p[0], y, p[1]) for p, y in zip(pts[j0:], yy[j0:])], hp.get("w", 44) / 2 * 0.8, mat("exhaust"))
            continue
        smooth_tube(f"header{i}", [(p[0], y, p[1]) for p, y in zip(pts, yy)], hp.get("w", 44) / 2 * 0.8, mat("exhaust"))
    if ex.get("collector"):
        # where the pipes meet under the engine: a flat box across the bike
        cb = ex["collector"]
        pillow("collector", [(cb["x"][0], cb["z"][0]), (cb["x"][0], cb["z"][1]), (cb["x"][1], cb["z"][1]), (cb["x"][1], cb["z"][0])], -cb["y"], cb["y"], mat("exhaust"), 9, 14)
    for i, can in enumerate(ex.get("cans", [])):
        for sg in ((1, -1) if both or under else (1,) if left else (-1,)):
            y0c, y1c = can_y(ex, i, under)
            a3, b3 = (can["a"][0], sg * y0c, can["a"][1]), (can["b"][0], sg * y1c, can["b"][1])
            rc = can["w"] / 2 * 0.86
            if is_belly(ex, i):
                # the collector box under the engine: a flat rounded box across the bike, not a round can
                (ax_, az_), (bx_, bz_), h_ = can["a"], can["b"], can["w"] / 2 * 0.8
                pillow(f"can{i}{sg}", [(ax_ + 14, az_ - h_ * 0.8), (ax_ + 14, az_ + h_ * 0.8), (bx_, bz_ + h_), (bx_, bz_ - h_)], -112, 112, mat("exhaust"), 26, 60)
                continue
            if i == 1 and is_belly(ex, 0) and DETAIL["on"]:
                # the outlet pipe from the collector box round to the stub silencer, so the two are one system
                bx_ = ex["cans"][0]["b"]
                smooth_tube(f"canlink{sg}", [(bx_[0] + 30, sg * 84, bx_[1]), (bx_[0] - 6, sg * 100, bx_[1] + 2), (a3[0] + (b3[0] - a3[0]) * 0.02, a3[1] + (b3[1] - a3[1]) * 0.02 - sg * 6, a3[2] + (b3[2] - a3[2]) * 0.02), tuple(a3[k] + (b3[k] - a3[k]) * 0.2 for k in range(3))], can["w"] / 2 * 0.5, mat("exhaust"))

            def at(t, a3=a3, b3=b3):
                return tuple(a3[k] + (b3[k] - a3[k]) * t for k in range(3))
            if can.get("nip"):      # a classic silencer: chrome, drawn in to a small outlet
                cone(f"caninlet{i}{sg}", at(-0.02), at(0.1), rc * 0.5, rc, mat("chrome"), 36)
                cone(f"can{i}{sg}", at(0.1), at(0.7), rc, rc, mat("chrome"), 36)
                cone(f"cannip{i}{sg}", at(0.7), at(0.96), rc, rc * 0.52, mat("chrome"), 36)
                cone(f"canend{i}{sg}", at(0.96), at(1.0), rc * 0.52, rc * 0.5, mat("chrome"), 36)
                cone(f"canbore{i}{sg}", at(0.995), at(1.006), rc * 0.36, rc * 0.36, mat("rubber"), 24)
                continue
            cone(f"can{i}{sg}", at(0.14), at(0.9), rc, rc, mat("exhaust_black" if can.get("dark") else "exhaust"), 36)
            cone(f"canband{i}{sg}", at(0.2), at(0.24), rc + 2.5, rc + 2.5, mat("steel"), 36)
            cone(f"canend{i}{sg}", at(0.9), at(1.0), rc + 2, rc * 0.9, mat("exhaust_black"), 36)
            cone(f"canbore{i}{sg}", at(0.995), at(1.012), rc * 0.6, rc * 0.6, mat("rubber"), 24)

    # swingarm, sprocket, chain, shock
    sw = model["swingarm"]
    so = shapes.dense(sw["outline"], True, 0.9)
    swm = mat("alu") if sw.get("cls") == "alu" else mat("alu_dark")
    sides = arm_sides(model)
    # across the bike, from the middle out: tyre (to 90), a gap, the chain (CHAIN_Y), a gap, the arm.
    # The chain never touches the tyre and never passes through the swingarm (beenster, 14 Nov).
    CHAIN_Y, ARM_IN, ARM_OUT = 108, 126, 174
    if sw.get("rigid"):
        # a rigid frame: the drawn shape is the axle plate welded between the frame's two rear tubes, one each side
        sides = (1, -1)
        swm = mat("chrome") if sw.get("cls") == "chromef" else framem
        for s in sides:
            prism(f"axleplate{s}", so, s * (ARM_IN + 4) if s > 0 else s * (ARM_IN + 16), s * (ARM_IN + 16) if s > 0 else s * (ARM_IN + 4), swm, 2.0)
    else:
        for s in sides:
            pillow(f"swingarm{s}", so, s * ARM_IN, s * ARM_OUT, swm, *((4, 7) if sw.get("square") else (14, 20)))      # square: a box section with only its edges eased
    piv = tuple(sw["pivot"])
    if not sw.get("rigid"):
        tube("pivot", [(piv[0], -ARM_OUT, piv[1]), (piv[0], ARM_OUT, piv[1])], 14, mat("steel"))
    tube("axle_r", [(A[0], -ARM_OUT + 6 if len(sides) == 2 else -60, A[1]), (A[0], ARM_OUT + 6, A[1])], 14, mat("steel"))
    if sw.get("shaft"):
        cone("finaldrive", (A[0], 92, A[1]), (A[0], ARM_IN + 10, A[1]), 78, 70, swm, 40)
        cone("finaldrivecap", (A[0], ARM_OUT - 4, A[1]), (A[0], ARM_OUT + 8, A[1]), 62, 50, mat("alu_dark"), 40)
    else:
        revolve("sprocket", [(sw["sprocket_r"], CHAIN_Y + 3), (sw["sprocket_r"], CHAIN_Y - 3), (40, CHAIN_Y - 3), (40, CHAIN_Y + 3)], A, mat("steel"), 48)
        cs = tuple(eng["countershaft"])
        d = math.hypot(A[0] - cs[0], A[1] - cs[1])
        th = math.atan2(A[1] - cs[1], A[0] - cs[0])
        al = math.acos((36 - sw["sprocket_r"]) / d)
        loop = []
        for sgn in (1, -1):
            v = (math.cos(th + sgn * al), math.sin(th + sgn * al))
            pair = [add(cs, v, 36), add(A, v, sw["sprocket_r"])]
            loop += pair if sgn == 1 else pair[::-1]
        R_ = sw["sprocket_r"]
        line = []
        for k in range(25):                      # round the back of the rear sprocket
            t_ = th + al - 2 * al * k / 24
            line.append(((A[0] + R_ * math.cos(t_), A[1] + R_ * math.sin(t_)), (math.cos(t_), math.sin(t_))))
        for k in range(13):                      # round the front of the gearbox sprocket
            t_ = th - al - (2 * math.pi - 2 * al) * k / 12
            line.append(((cs[0] + 36 * math.cos(t_), cs[1] + 36 * math.sin(t_)), (math.cos(t_), math.sin(t_))))
        verts, faces, m_ = [], [], len(line)
        for (px, pz), (nx_, nz_) in line:
            for dy, dr in ((CHAIN_Y - 9, 5.5), (CHAIN_Y + 9, 5.5), (CHAIN_Y + 9, -5.5), (CHAIN_Y - 9, -5.5)):
                verts.append(V(px + nx_ * dr, dy, pz + nz_ * dr))
        for k in range(m_):
            k2 = (k + 1) % m_
            for e in range(4):
                faces.append((4 * k + e, 4 * k + (e + 1) % 4, 4 * k2 + (e + 1) % 4, 4 * k2 + e))
        mesh_obj("chain", verts, faces, mat("steel"), smooth=False)
        # side plates: a dark stripe along the middle of each run so the links read
        for sgn in (1, -1):
            v = (math.cos(th + sgn * al), math.sin(th + sgn * al))
            p0, p1 = add(cs, v, 36), add(A, v, R_)
            nl = int(math.hypot(p1[0] - p0[0], p1[1] - p0[1]) / 15.875)
            verts, faces = [], []
            for k in range(nl):
                f0, f1 = (k + 0.2) / nl, (k + 0.8) / nl
                for yy in (CHAIN_Y - 9.4, CHAIN_Y + 9.4):
                    base = len(verts)
                    for f_, dr in ((f0, 3.2), (f1, 3.2), (f1, -3.2), (f0, -3.2)):
                        verts.append(V(p0[0] + (p1[0] - p0[0]) * f_ + v[0] * dr, yy, p0[1] + (p1[1] - p0[1]) * f_ + v[1] * dr))
                    faces.append((base, base + 1, base + 2, base + 3))
            mesh_obj(f"chainlinks{sgn}", verts, faces, mat("alu_dark"), smooth=False)
    for sk in ([model["shock"]] if model.get("shock") else []):      # a rigid (hardtail) frame has no shock
        sb, st_ = sk["bottom"], sk["top"]
        L = math.hypot(sb[0] - st_[0], sb[1] - st_[1])
        us = ((sb[0] - st_[0]) / L, (sb[1] - st_[1]) / L)
        ns = (-us[1], us[0])
        springm = mat("accent", hex_rgb(col.get("accent", col["body"])))
        if sk.get("twin"):
            # a pair of shocks: one each side, outside the swingarm at the bottom and on a stud from the
            # seat rail at the top
            y_t, y_b, rail = 150, ARM_OUT + 20, fr.get("sub_y", 84)
            for s in (-1, 1):
                tube(f"shock{s}", [(st_[0], s * y_t, st_[1]), (sb[0], s * y_b, sb[1])], 12, mat("alu_dark"))
                tube(f"shockstud_t{s}", [(st_[0], s * (rail - 10), st_[1]), (st_[0], s * (y_t + 16), st_[1])], 9, framem)
                tube(f"shockstud_b{s}", [(sb[0], s * (ARM_OUT - 14), sb[1]), (sb[0], s * (y_b + 16), sb[1])], 9, swm)
                cone(f"shockeye_t{s}", (st_[0], s * (y_t - 12), st_[1]), (st_[0], s * (y_t + 12), st_[1]), 18, 18, mat("alu_dark"), 24)
                cone(f"shockeye_b{s}", (sb[0], s * (y_b - 12), sb[1]), (sb[0], s * (y_b + 12), sb[1]), 18, 18, mat("alu_dark"), 24)
                coil = []
                for i in range(121):
                    t, a = 40 + (L - 110) * i / 120, 2 * math.pi * 10 * i / 120
                    q = add(add(tuple(st_), us, t), ns, 21 * math.cos(a))
                    coil.append((q[0], s * (y_t + (y_b - y_t) * t / L) + 21 * math.sin(a), q[1]))
                tube(f"spring{s}", coil, 4.5, springm)
        else:
            tube("shock", [(st_[0], 0, st_[1]), (sb[0], 0, sb[1])], 16, mat("alu_dark"))
            # the shock is held at both ends: a bridge across the swingarm at the bottom (out to the one arm
            # on a single-sided swingarm), and a cross tube between the frame sides at the top
            ya, yb = (-ARM_IN, ARM_IN) if len(sides) == 2 else (-34, ARM_IN + 4)
            tube("shockbridge", [(sb[0], ya, sb[1]), (sb[0], yb, sb[1])], 20, swm)
            cone("shockeye_b", (sb[0], -26, sb[1]), (sb[0], 26, sb[1]), 27, 27, mat("alu_dark"), 24)
            tube("shockmount_t", [(st_[0], -70, st_[1]), (st_[0], 70, st_[1])], 12, framem)
            cone("shockeye_t", (st_[0], -24, st_[1]), (st_[0], 24, st_[1]), 24, 24, mat("alu_dark"), 24)
            coil = []
            for i in range(121):
                t, a = 40 + (L - 110) * i / 120, 2 * math.pi * 9 * i / 120
                q = add(add(tuple(st_), us, t), ns, 27 * math.cos(a))
                coil.append((q[0], 27 * math.sin(a), q[1]))
            tube("spring", coil, 5, springm)

    # bodywork
    trellis = len([t for t in fr["tubes"] if t.get("cls") != "sub"]) >= 6
    body = model["body"]
    for part in ("tank", "seat", "tail"):
        shp = body[part][0]
        outline = shapes.dense(shp["pts"], True, shp.get("t", 1.0))
        spec = model.get("widths", {}).get(part, WIDTHS[part])
        if DETAIL["on"] and part == "tank" and model["family"].lower() in ("naked", "cafe", "classic", "sport"):
            spec = dict(spec, knee=(0.04, 0.6, 0.16))
            if model["family"].lower() == "sport":
                # the tank's base is flat and full width, so it sits down on the frame spars as it does in
                # the drawing instead of curving up away from them (beenster, sport round 2)
                spec = dict(spec, nb=7.0, bottom=0.96)
                if fr.get("plates"):
                    xh_ = max(q[0] for q in fr["plates"][0]["pts"])
                    sm_ = lambda t: max(0.0, min(1.0, t)) ** 2 * (3 - 2 * max(0.0, min(1.0, t)))
                    spec["base"] = lambda x: (78 + (wide / 2 + 12 - 78) * sm_((xh_ - x) / 300.0) - 8) / 0.88      # the skirt's corner lands in the spar's own thickness
        if DETAIL["on"] and part == "tail" and trellis and "tail" not in model.get("widths", {}):
            spec = dict(spec, w=[[0, 55], [0.4, 98], [1, 104]])      # the tail's front tucks in behind the side panels
        loft(part, outline, spec, (mat("seat", hex_rgb(col["seat"])) if col.get("seat") else mat("seat")) if part == "seat" else paint)
    for i, shp in enumerate(body.get("tail", [])[1:]):
        if shp["cls"] == "redlens":
            prism(f"taillight{i}", [tuple(p[:2]) for p in shp["pts"]], -45, 45, mat("redlens"), 2)
        elif shp["cls"] == "seat" and DETAIL["on"]:
            # a back rest pad standing on the sissy bar
            pillow(f"seatback{i}", shapes.dense(shp["pts"], True, shp.get("t", 1.0)), -88, 88, mat("seat", hex_rgb(col["seat"])) if col.get("seat") else mat("seat"), 16, 30)
    sissy = [shp for shp in body.get("tail", [])[1:] if shp.get("tube")] if DETAIL["on"] else []
    if sissy:
        # the sissy bar: one polished hoop, up from the axle plates either side of the mudguard, closing in
        # behind the back rest and bent over across the stop_
        sp_ = [tuple(sissy[0]["pts"][0])] + [tuple(shp["pts"][1]) for shp in sissy]
        if len(sp_) > 3 and sp_[-1][0] > sp_[-2][0]:
            sp_ = sp_[:-1]                # the drawn hook at the stop_ is the hoop's bend seen side on
        z0_, z1_ = sp_[0][1], sp_[-1][1]
        show_ = model.get("detail", {}).get("sissy")
        show_ = [show_, model.get("detail", {}).get("sissy_metal", "chrome")] if show_ in ("chain", "scroll", "filigree") else None
        for kv in filter(None, os.environ.get("BIKE_BESPOKE", "").split(",")):
            if kv.split("=")[0] == "sissy":
                show_ = (kv.split("=")[1].split(":") + ["chrome"])[:2]
        ornate = model.get("detail", {}).get("sissy") == "ornate" or bool(show_)
        ys_ = [128 - 40 * min(1.0, (q[1] - z0_) / max(1.0, 700 - z0_)) - (6 if ornate else 30) * max(0.0, (q[1] - 700) / max(1.0, z1_ - 700)) for q in sp_]
        sleft = [(q[0], y, q[1]) for q, y in zip(sp_, ys_)]
        xt, zt, yt = sp_[-1][0], sp_[-1][1], ys_[-1]
        chrome_ = mat("chrome")
        f_ = lambda z: next(((a[0] + (b[0] - a[0]) * (z - a[2]) / (b[2] - a[2]), a[1] + (b[1] - a[1]) * (z - a[2]) / (b[2] - a[2])) for a, b in zip(sleft, sleft[1:]) if a[2] <= z <= b[2]), (sleft[-1][0], sleft[-1][1]))
        if show_:
            # the show bars to order (beenster, Nov 17): chain, scrollwork, filigree, in any of the four metals
            import chopper_parts3d
            chopper_parts3d.use(sys.modules[__name__])
            chopper_parts3d.sissy_bar(show_[0], show_[1], sleft, sissy[0]["tube"] / 2)
            chrome_ = chopper_parts3d.metal(show_[1])
            straps = (sp_[-2][1] - 150, sp_[-2][1] - 20)
        elif ornate:
            # a show sissy bar (beenster, chopper round 1: "elaborately designed"): the two uprights close in a pointed
            # arch with a spear finial, a centre bar with collars, and wrought scrolls between the bars above the pad
            ux_, uz_ = sp_[-1][0] - sp_[-2][0], sp_[-1][1] - sp_[-2][1]
            ul_ = math.hypot(ux_, uz_)
            ux_, uz_ = ux_ / ul_, uz_ / ul_
            up_ = lambda q, d: (q[0] + ux_ * d, q[1], q[2] + uz_ * d)
            apex = up_((xt, 0.0, zt), 112)
            for sgn in (1, -1):
                side_ = sleft + [up_((xt, yt * 0.97, zt), 30), up_((xt, yt * 0.72, zt), 66), up_((xt, yt * 0.3, zt), 96), apex]
                smooth_tube(f"sissybar{sgn}", [(q[0], sgn * q[1], q[2]) for q in side_], sissy[0]["tube"] / 2, chrome_)
            cone("sissyfinial0", up_(apex, -6), up_(apex, 4), 9, 9, chrome_, 20)
            cone("sissyfinial1", up_(apex, 4), up_(apex, 30), 4, 15, chrome_, 4)
            cone("sissyfinial2", up_(apex, 30), up_(apex, 92), 15, 0.6, chrome_, 4)
            zpad = sp_[-2][1] + 26
            xb_, yb_ = f_(zpad)
            base = (xb_, 0.0, zpad)
            tube("sissycentre", [base, up_(apex, -4)], 4.5, chrome_)
            for k, d_ in enumerate((0.0, 62.0, 124.0)):
                q = up_(base, d_)
                cone(f"sissycollar{k}a", up_(q, -7), q, 4.5, 9, chrome_, 16)
                cone(f"sissycollar{k}b", q, up_(q, 7), 9, 4.5, chrome_, 16)
            for sgn in (1, -1):
                for k, (d_, R_, turn) in enumerate(((36.0, 0.5, 1), (104.0, 0.42, -1))):
                    xu, yu = f_(zpad + d_ * uz_)
                    R_ = (yu - 10) * R_
                    cen = up_((xb_, yu - 5 - R_, zpad), d_)
                    pts_ = []
                    for j in range(25):
                        t_ = j / 24
                        w_, rad = turn * t_ * 2 * math.pi * 1.35, R_ * (1 - 0.72 * t_)
                        q = up_((cen[0], cen[1] + rad * math.cos(w_), cen[2]), rad * math.sin(w_))
                        pts_.append((q[0], sgn * q[1], q[2]))
                    smooth_tube(f"sissyscroll{k}{sgn}", pts_, 3.6, chrome_)
            straps = (sp_[-2][1] - 150, sp_[-2][1] - 20, zpad)
        elif model.get("detail", {}).get("sissy") == "blade":
            # an angular bar: square shoulders with a spike on each, and a cross brace above the pad
            top_ = zt + 46
            for sgn in (1, -1):
                tube(f"sissybar{sgn}", [(q[0], sgn * q[1], q[2]) for q in sleft + [(xt - 5, yt, top_)]], sissy[0]["tube"] / 2, chrome_)
                cone(f"sissyspike{sgn}a", (xt - 5, sgn * yt, top_ - 4), (xt - 5, sgn * yt, top_ + 8), 10, 10, chrome_, 6)
                cone(f"sissyspike{sgn}b", (xt - 5, sgn * yt, top_ + 8), (xt - 7, sgn * yt, top_ + 70), 10, 0.6, chrome_, 6)
            tube("sissytop", [(xt - 5, -yt, top_), (xt - 5, yt, top_)], sissy[0]["tube"] / 2, chrome_)
            za_, zb_ = sp_[-2][1] + 30, top_ - 14
            (xa_, ya_), (xb2_, yb2_) = f_(za_), f_(min(zb_, zt))
            for sgn in (1, -1):
                tube(f"sissybrace{sgn}", [(xa_, sgn * ya_, za_), (xb2_, -sgn * yb2_, zb_)], 4.5, chrome_)
            cone("sissyboss", ((xa_ + xb2_) / 2 - 5, 0.0, (za_ + zb_) / 2), ((xa_ + xb2_) / 2 + 5, 0.0, (za_ + zb_) / 2), 16, 16, chrome_, 6)
            straps = (sp_[-2][1] - 150, sp_[-2][1] - 20)
        else:
            stop_ = [(xt - 4, yt * 0.86, zt + 26), (xt - 6, yt * 0.5, zt + 44), (xt - 7, 0.0, zt + 50)]
            shalf = sleft + stop_
            smooth_tube("sissybar", shalf + [(q[0], -q[1], q[2]) for q in shalf[-2::-1]], sissy[0]["tube"] / 2, chrome_)
            straps = (sp_[-2][1] - 150, sp_[-2][1] - 20)
            if model.get("detail", {}).get("sissy") == "ladder":
                # a traditional ladder bar: evenly spaced rungs up to the bend, a ball at each end of the top rung
                n_r = 4
                straps += tuple(sp_[-2][1] + 34 + (zt - sp_[-2][1] - 44) * k / (n_r - 1) for k in range(n_r))
                xx_, yy_ = f_(straps[-1])
                for sgn in (1, -1):
                    cone(f"sissyball{sgn}", (xx_, sgn * (yy_ + 4), straps[-1]), (xx_, sgn * (yy_ + 22), straps[-1]), 11, 11, chrome_, 16)
        for k, zz in enumerate(straps):
            # flat cross straps: the back rest bolts to the lower two
            xx, yy = f_(zz)
            tube(f"sissystrap{k}", [(xx, -yy, zz), (xx, yy, zz)], 5, chrome_)
    sport_bike = model["family"].lower() == "sport"
    fair_wrap = {}
    for i, shp in enumerate(body.get("front_guard", []) + body.get("panels", [])):
        if shp.get("draw_only") or (shp.get("adv") and DETAIL["on"]):      # a detail of the drawing only, such as a vent printed on a fairing
            continue                                                         # ("adv": built as shaped shells by the Definition pass)
        m_ = paint if shp["cls"] == "body" else mat("chrome") if shp["cls"] == "chromef" else mat("plastic")
        if shp["cls"] == "plate":
            m_ = mat("paint", hex_rgb(col.get("accent", "#F4F4F2")))      # a number board in the bike's accent colour
        blade_ = next((v for v in body.get("front_guard", []) if v.get("shroud")), None) if sport_bike or DETAIL["on"] else None
        if "arc" in shp and blade_ and shp in body.get("front_guard", []):
            guard_blade(f"guard{i}", shp["arc"], [tuple(q[:2]) for q in shapes.dense(blade_["pts"], True, blade_.get("t", 1.0))], 72, m_, tight=model["wheels"]["front"]["tyre_r"] if model["family"].lower() in ("sport", "cafe") else False)
        elif shp.get("shroud") and (sport_bike or DETAIL["on"]):
            pass                          # part of the one-piece mudguard above
        elif "arc" in shp:
            arc_band(f"guard{i}", shp["arc"], 72 if shp["arc"]["c"][0] > 100 else 100, m_)
        elif shp in body.get("front_guard", []) and max(q[0] for q in shp["pts"]) - min(q[0] for q in shp["pts"]) > 300:
            # a high motocross mudguard: ONE blade on the centre line under the bottom yoke (beenster, MX round 1)
            pillow(f"guard{i}", shapes.dense(shp["pts"], True, shp.get("t", 1.0)), -84, 84, m_, 10, 60)
        else:
            pp = shapes.dense(shp["pts"], True, shp.get("t", 1.0))
            low = max(p[1] for p in pp) < 400
            if shp.get("shroud"):
                # the fork shroud: a shell round the front of each lower fork leg, joined to the mudguard
                for s in (-1, 1):
                    pillow(f"shroud{i}{s}", pp, s * 84, s * (104 if sport_bike else 128), m_, 8 if sport_bike else 14, 22)
            elif shp.get("fairing"):
                # a fairing side: outside the engine over the cases, closing in to the nose and the keel
                xf = max(q[0] for q in pp)
                sport = model["family"].lower() == "sport"

                # the marks drawn on a fairing side (vents, slots) are cut through it in 3D
                cut_shapes = [v for v in body.get("panels", []) if v.get("draw_only") and v["cls"] == "plastic"] if sport else []
                cuts = [[tuple(q[:2]) for q in shapes.dense(v["pts"], True, v.get("t", 1.0))] for v in cut_shapes]

                def lead(z, pp=pp):
                    """The fairing's leading edge at height z."""
                    xs_ = [a[0] + (b_[0] - a[0]) * (z - a[1]) / (b_[1] - a[1]) for a, b_ in zip(pp, pp[1:] + pp[:1])
                           if (a[1] - z) * (b_[1] - z) <= 0 and a[1] != b_[1]]
                    return max(xs_) if xs_ else xf

                z_lo, z_hi = min(q[1] for q in pp), max(q[1] for q in pp)
                tab = [lead(z_lo + (z_hi - z_lo) * n_ / 80.0) for n_ in range(81)]
                for _ in range(6):        # one fair leading edge, so the turned-in lip has no steps
                    tab = [tab[0]] + [0.25 * tab[n_ - 1] + 0.5 * tab[n_] + 0.25 * tab[n_ + 1] for n_ in range(1, 80)] + [tab[80]]

                def lead_s(z):
                    u = max(0.0, min(79.999, (z - z_lo) / (z_hi - z_lo) * 80.0))
                    return tab[int(u)] + (tab[int(u) + 1] - tab[int(u)]) * (u - int(u))

                def wrap(x, z, xf=xf):
                    k = 0.86 + 0.14 * max(0.0, min(1.0, (z - 190) / 170.0))
                    if sport:
                        # taut flat panels, not a blown shell: a flat cheek opening out from the nose to a
                        # hard shoulder line, a flat flank behind it, a flat V keel below, and the top
                        # leaning in under the tank; the leading edge is turned in tight round the front
                        y = 150 + 104 * max(0.0, min(1.0, (1330 - x) / 290.0))
                        if x < 1040:
                            y -= 26 * min(1.0, (1040 - x) / 420.0)
                        if z < 430:
                            u_ = max(0.0, min(1.0, (z - 140) / 290.0))      # the keel pinches in to the wheel's width in a curve
                            y = 96 + (y + 26 * min(1.0, max(0.0, 1040 - x) / 420.0) - 96) * math.sqrt(1 - (1 - u_) ** 2)
                        elif z > 690:
                            y *= 1 - 0.16 * min(1.0, (z - 690) / 170.0)
                            # the handlebar cutaway: under the bars the top edge turns outward in a small
                            # lip, a wing that throws air off the rider's hands
                            fl = max(0.0, min(1.0, (x - 960) / 80.0)) * max(0.0, min(1.0, (1250 - x) / 90.0))
                            y += 22 * fl * max(0.0, min(1.0, (z - 745) / 55.0)) ** 2
                        d = max(0.0, min(1.0, (lead_s(z) - x) / 70.0))
                        y = 122 + (max(y, 122) - 122) * math.sqrt(1 - (1 - d) ** 2) if y > 122 else y
                        if DETAIL["on"] and z < 500:
                            # the belly tapers in to a prow where the two sides meet behind the front tyre
                            # (beenster, sport round 1): full width 280 mm back, drawn in to the middle at the front
                            d2 = max(0.0, min(1.0, (lead_s(z) - x) / 280.0))
                            low = max(0.0, min(1.0, (500 - z) / 90.0))
                            y *= 1 - low * 0.66 * (1 - d2 * d2 * (3 - 2 * d2))
                        return y
                    if DETAIL["on"] and model["family"].lower() == "adventure" and any(v.get("cowl") and v.get("w") for v in body.get("panels", [])):
                        # an adventure tank shroud: flush with the nose's flank where the two meet, swelling out
                        # over the radiator, leaning back in to the rider's knee, its lower spear and its top
                        # edge both tucked in, so it reads as a shaped shell and not a flat board
                        y = 153 + 44 * math.sin(math.pi * max(0.0, min(1.0, (1191 - x) / 340.0)))
                        lo_ = max(0.0, min(1.0, (z - 340) / 330.0))
                        y = 122 + (y - 122) * math.sqrt(1 - (1 - lo_) ** 2)
                        return y - 16 * max(0.0, min(1.0, (z - 925) / 90.0)) ** 2
                    t = max(0.0, min(1.0, (xf - x) / 260.0))
                    t = t * t * (3 - 2 * t)
                    return (104 + (wide / 2 + 52 - 104) * t) * k
                fair_wrap["f"] = wrap
                for s in (-1, 1):
                    skin(f"fairing{i}{s}", pp, wrap, s, m_, stations=130 if sport else 70, rows=(60 if DETAIL["on"] else 30) if sport else 16, holes=cuts)
                if sport and DETAIL["on"]:
                    # behind the nose flare the fairing's top edge turns in to meet the cowl: a shelf that closes
                    # the slot you could see down between the two (beenster's yellow mark, sport round 2)
                    for s in (-1, 1):
                        vs, fs, xs_ = [], [], [1092 + 8 * n_ for n_ in range(27)]
                        for x_ in xs_:
                            zt_ = shapes.top_bottom(pp, x_)[0]
                            e_ = min(1.0, (x_ - 1092) / 40.0)
                            vs += [V(x_, s * (wrap(x_, zt_ - 3) - 1), zt_ - 3), V(x_, s * (wrap(x_, zt_ - 3) - 1 - e_ * (wrap(x_, zt_ - 3) - 139)), zt_ - 3 - 10 * e_)]
                        for n_ in range(len(xs_) - 1):
                            fs.append((2 * n_, 2 * n_ + 1, 2 * n_ + 3, 2 * n_ + 2))
                        mesh_obj(f"fairingshelf{i}{s}", vs, fs, m_, True, 0, sharp=False)
                # the two sides join behind the front tyre in a curve: a chin bridge, low in the middle and
                # rising to each leading edge, set back in the middle to clear the tyre on full compression
                zs_ = [z_lo + 6 + n_ * 4 for n_ in range(int((min(340, z_hi) - z_lo - 6) / 4))]
                z_n = max(zs_, key=lead_s) if zs_ else None
                if sport and z_n and z_lo < 260 and lead_s(z_n) > 1040:
                    zb, NT, NZ = max(z_lo + 10, z_n - 46), 16, 8
                    edge_ = None
                    if DETAIL["on"]:
                        zb, NZ = z_lo + 3, 22
                        # the real front edge of the built fairing side, read off its mesh, so the bridge meets it exactly
                        fv = [(v.co.x / MM, abs(v.co.y) / MM, v.co.z / MM) for v in bpy.data.objects[f"fairing{i}1"].data.vertices if v.co.z / MM < 480]
                        edge_ = []
                        for n_ in range(int((470 - z_lo) / 4) + 1):
                            band = [v for v in fv if abs(v[2] - (z_lo + 4 * n_)) <= 6]
                            edge_.append(list(max(band)[:2]) if band else None)
                        known = [e for e in edge_ if e]
                        edge_ = [e or known[0] for e in edge_]
                        for _ in range(10):
                            edge_ = [edge_[0]] + [[0.25 * a_[k] + 0.5 * b_[k] + 0.25 * c_[k] for k in (0, 1)] for a_, b_, c_ in zip(edge_, edge_[1:], edge_[2:])] + [edge_[-1]]
                    vs, fs = [], []
                    for back in (0, 1):
                        for a_ in range(NT + 1):
                            t_ = -1 + 2 * a_ / NT
                            zt = z_n + 22 + 130 * t_ * t_
                            if DETAIL["on"]:
                                # the sides really join (beenster, sport round 2): one closed prow from the keel
                                # up to the radiator's air opening, a little higher at each side than in the middle
                                zt = min(z_hi - 10, 408 + 46 * t_ * t_)
                            for b_ in range(NZ + 1):
                                z_ = zb + (zt - zb) * b_ / NZ
                                if DETAIL["on"]:      # the bridge laps round the front edge of each fairing side, so the cut edges do not show as a slit
                                    u_e = max(0.0, min(len(edge_) - 1.001, (z_ - z_lo) / 4.0))
                                    xe_, yw_ = (edge_[int(u_e)][k] + (edge_[int(u_e) + 1][k] - edge_[int(u_e)][k]) * (u_e - int(u_e)) for k in (0, 1))
                                    # a shallow dish, not a deep slot: set back by under half its own half-width
                                    vs.append(V(xe_ + 2.0 - min(46.0, 0.4 * yw_) * (1 - t_ * t_) - 5 * back, t_ * (yw_ + 1.5 - 4 * back), z_))
                                    continue
                                vs.append(V(lead_s(z_) - 6 - 44 * (1 - t_ * t_) - 4 * back, t_ * (wrap(lead_s(z_) - 4, z_) - 2), z_))
                    n1 = (NT + 1) * (NZ + 1)
                    for a_ in range(NT):
                        for b_ in range(NZ):
                            q0 = a_ * (NZ + 1) + b_
                            fs.append((q0, q0 + NZ + 1, q0 + NZ + 2, q0 + 1))
                            fs.append((n1 + q0, n1 + q0 + 1, n1 + q0 + NZ + 2, n1 + q0 + NZ + 1))
                    for a_ in range(NT):
                        for b_ in (0, NZ):
                            q0 = a_ * (NZ + 1) + b_
                            fs.append((q0, q0 + NZ + 1, n1 + q0 + NZ + 1, n1 + q0))
                    for b_ in range(NZ):
                        for a_ in (0, NT):
                            q0 = a_ * (NZ + 1) + b_
                            fs.append((q0, q0 + 1, n1 + q0 + 1, n1 + q0))
                    bm_fix_normals(mesh_obj(f"chin{i}", vs, fs, m_, True, 0, sharp=False))
                    for n_, (cut, shp_) in enumerate(zip(cuts, cut_shapes)):
                        if not shp_.get("reveal"):      # an air channel: a dark duct wall set back inside the opening
                            skin(f"duct{i}{n_}{s}", cut, lambda x, z: wrap(x, z) - 26, s, mat("plastic"), stations=30, rows=12, thick=3.0)
            elif shp.get("ear"):
                # the nose's flared sides, ahead of the grips and the cutaway the bars swing through
                # (beenster's red line, 14 Nov)
                def earf(x, z, w_=shp["w"]):
                    # one surface with the nose: it leaves the snout and widens in a straight line in
                    # plan to the cutaway edge, a kite in front view with nothing standing above the nose
                    t_ = max(0.0, min(1.0, (1500 + g["wheelbase"] - 1440 - x) / 240.0))
                    y = 96 + (shapes.lerp_table(w_, z) - 96) * t_
                    if DETAIL["on"] and fair_wrap.get("f"):
                        # the flare's bottom edge lies on the fairing side below it, so no gap opens between
                        # the nose and the fairing (beenster's yellow mark, sport round 2)
                        tb_ = shapes.top_bottom(pp, x)
                        if tb_:
                            k_ = max(0.0, min(1.0, 1 - (z - tb_[1]) / 75.0)) * max(0.0, min(1.0, (815 - tb_[1]) / 20.0))      # along the shared lower edge only
                            y += (fair_wrap["f"](x, tb_[1] - 4) + 1.5 - y) * k_ * k_ * (3 - 2 * k_)
                    return y
                if DETAIL["on"] and fair_wrap.get("f"):
                    pp = [(q[0], q[1] - 9 * max(0.0, min(1.0, (805 - q[1]) / 20.0))) + tuple(q[2:]) for q in pp]      # lapped over the fairing's top edge
                for s in (-1, 1):
                    skin(f"ear{i}{s}", pp, earf, s, m_, stations=50, rows=18, thick=5.0)
            elif shp.get("cowl"):
                if shp.get("w"):
                    # a nose with its own plan shape (adventure): full width round the fork tops, drawn in to the beak
                    loft(f"cowl{i}", pp, {"w": shp["w"], "n": shp.get("n", 3.4), "bottom": shp.get("bottom", 1.0), "blunt_rear": True}, m_)
                elif model["family"].lower() == "sport":
                    # a sport nose: full width at the bars, drawn in to a point over the front wheel
                    loft(f"cowl{i}", pp, {"w": [[0, 150], [0.5, 156], [0.8, 146], [1, 104]], "n": 4.6, "bottom": 1.0}, m_)
                else:
                    pillow(f"cowl{i}", pp, -112, 112, m_, 34, 70)
            elif shp.get("winglet"):
                # an aerofoil: a wing growing out of the fairing beside the nose, thick at the root,
                # tapering and drooping a little to a thin tip at the fairing's full width
                ring_ = [tuple(q[:2]) for q in pp[::max(1, len(pp) // 28)]]
                cx_ = sum(q[0] for q in ring_) / len(ring_)
                cz_ = sum(q[1] for q in ring_) / len(ring_)
                secs = [(120, 1.0, 1.5, 0), (190, 0.92, 1.1, -8), (255, 0.8, 0.7, -20), (312, 0.62, 0.3, -32)]
                if DETAIL["on"]:
                    # the tip turns down into an end plate (beenster, sport round 1, from the reference front view)
                    secs = [(120, 1.0, 1.4, 0), (190, 0.94, 1.0, -7), (255, 0.86, 0.62, -18), (288, 0.8, 0.45, -27), (302, 0.76, 0.38, -38), (309, 0.7, 0.32, -52), (311, 0.6, 0.26, -66)]
                for s in (-1, 1):
                    vs, fs = [], []
                    for y_, kx, kz, dz in secs:
                        vs += [V(cx_ + (q[0] - cx_) * kx - (1 - kx) * 40, s * y_, cz_ + (q[1] - cz_) * kz + dz) for q in ring_]
                    n_ = len(ring_)
                    for a_ in range(len(secs) - 1):
                        for b_ in range(n_):
                            q0, q1 = a_ * n_ + b_, a_ * n_ + (b_ + 1) % n_
                            fs.append((q0, q1, q1 + n_, q0 + n_))
                    fs.append(tuple(range(n_)))
                    fs.append(tuple((len(secs) - 1) * n_ + b_ for b_ in range(n_)))
                    bm_fix_normals(mesh_obj(f"winglet{i}{s}", vs, fs, m_, True, 0, sharp=False))
            elif shp["cls"] == "screen":
                pillow(f"pscreen{i}", pp, -96, 96, mat("screen"), 30, 70)
            elif low:
                pillow(f"belly{i}", pp, -125, 125, m_, 22, 50)
            else:
                for s in (-1, 1):
                    # inside a tube frame the panels sit within the tubes, so the frame never cuts through them
                    y_a, y_b = (86, 120) if trellis else (128, 166)
                    if shp.get("out_r") and s == -1:
                        # the right-hand panel stands further out, with room behind it for the silencer (beenster, MX round 2)
                        y_a, y_b = y_a + shp["out_r"], y_b + shp["out_r"]
                    if shp["cls"] == "plate":
                        y_a, y_b = y_b - 6, y_b + 5      # a number board stands proud of the panel it is on
                    pillow(f"panel{i}{s}", pp, s * y_a, s * y_b, m_, 4 if shp["cls"] == "plate" else 18, 30, both=False)
    hanger = body.get("plate_hanger", []) if model.get("fit", {}).get("plate_hanger", True) else []
    for i, shp in enumerate(hanger):
        if shp["cls"] == "redlens":
            prism(f"platelamp{i}", [tuple(q[:2]) for q in shp["pts"]], -30, 30, mat("redlens"), 2)
    for i, shp in enumerate(body.get("behind", []) + [h for h in hanger if h["cls"] != "redlens"]):
        hw = 85 if shp["cls"] == "plate" else (95 if "fender" in shp["cls"] else 25)
        if shp.get("tube"):
            # an arm from the swingarm, round the outside of the tyre and in to the plate
            q = [tuple(v) for v in shp["pts"]]
            ys = [150, 128, 118, 60][:len(q)] + [60] * max(0, len(q) - 4)
            smooth_tube(f"platearm{i}", [(v[0], y, v[1]) for v, y in zip(q, ys)], shp["tube"] / 2 * 0.8, mat("alu_dark"))
            continue
        blade_ = next((v for v in body.get("front_guard", []) if v.get("shroud")), None) if sport_bike or DETAIL["on"] else None
        if "arc" in shp and blade_ and shp in body.get("front_guard", []):
            guard_blade(f"guard{i}", shp["arc"], [tuple(q[:2]) for q in shapes.dense(blade_["pts"], True, blade_.get("t", 1.0))], 72, m_)
        elif shp.get("shroud") and (sport_bike or DETAIL["on"]):
            pass                          # part of the one-piece mudguard above
        elif "arc" in shp:
            arc_band(f"platearm{i}", shp["arc"], 60, mat("plastic"))
            continue
        pp = shapes.dense(shp["pts"], True, shp.get("t", 1.0))
        (prism if shp["cls"] == "plate" else pillow)(f"behind{i}", pp, -hw, hw, mat("plate") if shp["cls"] == "plate" else mat("plastic"), 2 if shp["cls"] == "plate" else 10)
    for c in body.get("caps", []):
        tube("filler", [(c["at"][0], 0, c["at"][1] - 20), (c["at"][0], 0, c["at"][1] - 4)], 38, mat("alu_dark"))

    # front end: lamp, dash, bars, mirrors
    grip = next((tuple(s_["at"]) for s_ in model.get("front_end", []) if s_.get("circle") and s_.get("part") != "head"), None)
    head = [s_ for s_ in model.get("front_end", []) if s_.get("part") == "head"]
    round_lamp = any(s_.get("round") for s_ in head)
    if round_lamp:
        # a round lamp: bowl, chrome rim and domed glass, turned about the lamp's own axis
        bowl = next(shapes.dense(s_["pts"], True, s_.get("t", 1.0)) for s_ in head if s_.get("round"))
        x0, x1 = min(q[0] for q in bowl), max(q[0] for q in bowl)
        zc = (min(q[1] for q in bowl) + max(q[1] for q in bowl)) / 2
        R, L = (max(q[1] for q in bowl) - min(q[1] for q in bowl)) / 2, x1 - x0
        twin = any(s_.get("twin") for s_ in head)
        for yl in ((-R - 6, R + 6) if twin else (0,)):
            cone(f"lampbowl0{yl}", (x0, yl, zc), (x0 + 0.3 * L, yl, zc), R * 0.35, R * 0.8, mat("plastic"), 40)
            cone(f"lampbowl1{yl}", (x0 + 0.3 * L, yl, zc), (x0 + 0.7 * L, yl, zc), R * 0.8, R, mat("plastic"), 40)
            cone(f"lampbowl2{yl}", (x0 + 0.7 * L, yl, zc), (x1, yl, zc), R, R, mat("plastic"), 40)
            cone(f"lamprim{yl}", (x1 - 6, yl, zc), (x1 + 8, yl, zc), R + 5, R + 3, mat("chrome"), 40)
            cone(f"lampglass0{yl}", (x1 + 6, yl, zc), (x1 + 16, yl, zc), R, R * 0.75, mat("lens"), 40)
            cone(f"lampglass1{yl}", (x1 + 16, yl, zc), (x1 + 22, yl, zc), R * 0.75, R * 0.3, mat("lens"), 40)
    for i, shp in enumerate(model.get("front_end", [])):
        if shp.get("tube"):
            continue
        if shp.get("circle"):
            if shp.get("part") == "head" and shp["cls"] == "plastic":
                # a round clock: a pair of cups side by side, faces square to the rider
                d_, r_, c_ = (-0.45, 0.89), shp["circle"], shp["at"]
                for s in ((-1, 1) if not i % 2 else (-1, 1)):
                    yc = s * 46
                    cone(f"clock{i}{s}", (c_[0] - d_[0] * 22, yc, c_[1] - d_[1] * 22), (c_[0] + d_[0] * 20, yc, c_[1] + d_[1] * 20), r_ * 0.8, r_ * 1.25, mat("plastic"), 28)
                    cone(f"clockrim{i}{s}", (c_[0] + d_[0] * 20, yc, c_[1] + d_[1] * 20), (c_[0] + d_[0] * 24, yc, c_[1] + d_[1] * 24), r_ * 1.3, r_ * 1.3, mat("chrome"), 28)
            continue
        pp = shapes.dense(shp["pts"], True, shp.get("t", 1.0))
        if round_lamp and shp.get("part") == "head" and (shp.get("round") or shp["cls"] in ("lens", "chromef")):
            continue
        if shp["cls"] == "lens":
            if model["family"].lower() == "sport":      # slim lamps let into the point of the nose
                cx_ = sum(q[0] for q in pp) / len(pp)
                cz_ = sum(q[1] for q in pp) / len(pp)
                for s in (-1, 1):                     # a slim lamp each side, in a dark surround
                    pillow(f"lens{i}{s}", [(q[0] + 9, q[1] - 2) for q in pp], s * 36, s * 90, mat("lens"), 5, 14)
                    pillow(f"lensrim{i}{s}", [(cx_ + (q[0] - cx_) * 1.3 - 8, cz_ + (q[1] - cz_) * 1.3 - 2) for q in pp], s * 24, s * 104, mat("plastic"), 5, 14)
            else:
                pillow(f"lens{i}", [(q[0] + 16, q[1] - 3) for q in pp], -82, 82, mat("lens"), 6, 26)
        elif shp["cls"] == "screen":
            pillow(f"screen{i}", pp, -105, 105, mat("screen"), 30, 70)
        elif shp["cls"] == "plate":      # the front number board: a flat plate across the fork legs
            pillow(f"numberboard{i}", pp, -112, 112, mat("paint", hex_rgb(col.get("accent", "#F4F4F2"))), 5, 34)
        elif shp.get("bar_end"):
            cx_, cz_ = sum(p[0] for p in pp) / len(pp), sum(p[1] for p in pp) / len(pp)
            for s in (-1, 1):
                tube(f"mstub{s}", [(cx_, s * 378, cz_), (cx_, s * 400, cz_)], 6, mat("alu_dark"))
                cone(f"mirror{i}{s}", (cx_ + 4, s * 440, cz_ + 4), (cx_ - 6, s * 440, cz_ + 2), 44, 40, mat("plastic"), 28)
        elif shp.get("mirror") or max(p[1] for p in pp) > 1120:
            for s in (-1, 1):
                pillow(f"mirror{i}{s}", pp, s * 330, s * 430, mat("plastic"), 7, 16)
                cx = sum(p[0] for p in pp) / len(pp)
                cz = min(p[1] for p in pp)
                if grip:
                    tube(f"mstalk{s}", [(grip[0] + 10, s * 300, grip[1] + 5), (cx, s * 370, cz + 5)], 5, mat("plastic"))
        else:
            pillow(f"front{i}", pp, -90, 90, paint if shp["cls"] == "body" else mat("plastic"), 20, 50)
    if grip:
        bars_ = [s_ for s_ in model.get("front_end", []) if s_.get("part") != "head"]
        clip = any(s_.get("clipon") for s_ in bars_)
        rz = next((tuple(s_["pts"][1]) for s_ in bars_ if s_.get("tube") and s_["cls"] == "cast"), (top[0] - 28, top[1] + 62))
        for s in (-1, 1):
            if clip:
                # clip-ons: a clamp round each fork leg and a short bar straight out from it
                tube(f"clamp{s}", [(rz[0] + 20, s * 96, rz[1] - 16), (rz[0] + 20, s * 96, rz[1] + 14)], 36, mat("alu_dark"))
                # the last stretch runs dead straight along the grip's own centre line (the grip sat 11 degrees off the bar)
                on_ = lambda y: (grip[0] - 6 * (y - 258) / 126, s * y, grip[1] - 4 * (y - 258) / 126)
                smooth_tube(f"bar{s}", [(rz[0] + 14, s * 110, rz[1]), (grip[0] + 9, s * 186, grip[1] + 2.5), on_(232), on_(258), on_(320), on_(384)], 11.5, mat("alu_dark"))
            else:
                tube(f"riser{s}", [(top[0], s * 45, top[1]), (rz[0], s * 45, rz[1])], 15, mat("alu_dark"))
            tube(f"grip{s}", [(grip[0], s * 255, grip[1]), (grip[0] - 6, s * 380, grip[1] - 4)], 17, mat("rubber"))
            tube(f"lever{s}", [(grip[0] + 30, s * 262, grip[1] - 10), (grip[0] + 62, s * 300, grip[1] - 22), (grip[0] + 55, s * 395, grip[1] - 24)], 5, mat("alu_dark"))
        if not clip:
            smooth_tube("bar", [(grip[0] - 4, -380, grip[1] - 3), (grip[0], -240, grip[1]), (rz[0], -70, rz[1]), (rz[0], 70, rz[1]), (grip[0], 240, grip[1]), (grip[0] - 4, 380, grip[1] - 3)], 11.5, mat("alu_dark"))

    # hoses and cables: front brake lines to both calipers, clutch and throttle cables down behind the head
    if grip:
        blk = mat("rubber")
        mc = (grip[0] + 22, grip[1] - 6)
        for s in (-1, 1):
            calp = add(F, (-0.6, 0.35), 95)
            smooth_tube(f"brakehose{s}", [(mc[0], -250, mc[1]), (mc[0] + 70, -150, mc[1] - 60), (head_t[0] + 70, s * 60, head_t[1] - 120),
                                          (add(F, uf, 330)[0] - 40, s * 96, add(F, uf, 330)[1]), (calp[0], s * 92, calp[1])], 4.5, blk)
        smooth_tube("clutchcable", [(mc[0], 250, mc[1]), (mc[0] + 80, 150, mc[1] - 50), (head_t[0] + 30, 60, head_t[1] - 80),
                                    (head_b[0] - 90, 110, head_b[1] - 40), (crank[0] - 60, wide / 2 - 30, crank[1] + 110)], 3.5, blk)
        smooth_tube("throttlecable", [(mc[0], -255, mc[1] + 8), (mc[0] + 90, -140, mc[1] - 30), (head_t[0] + 20, -50, head_t[1] - 60),
                                      (head_b[0] - 120, -70, head_b[1] + 20)], 3.5, blk)
        # indicators either side of the lamp
        for s in (-1, 1):
            ix, iz = head_b[0] + 60, head_b[1] + 40
            tube(f"indstalk{s}", [(ix, s * 90, iz), (ix, s * 150, iz)], 5, mat("plastic"))
            cone(f"indicator{s}", (ix - 14, s * 165, iz), (ix + 18, s * 165, iz), 13, 9, mat("amberlens"), 16)
    tl = next((shp for shp in body.get("tail", []) if shp.get("cls") == "redlens"), None)
    if tl:
        tx = sum(q[0] for q in tl["pts"]) / len(tl["pts"])
        tz = min(q[1] for q in tl["pts"]) - 25
        for s in (-1, 1):
            tube(f"rindstalk{s}", [(tx + 30, s * 60, tz), (tx + 30, s * 125, tz)], 5, mat("plastic"))
            cone(f"rindicator{s}", (tx + 44, s * 138, tz), (tx + 12, s * 138, tz), 13, 9, mat("amberlens"), 16)

    # footrests and stand
    pegs = [tuple(s["at"]) for s in model.get("hangers", []) if s.get("circle")]
    for i, shp in enumerate(model.get("hangers", [])):
        if shp.get("pts") and not shp.get("tube"):
            pp = shapes.dense(shp["pts"], True, shp.get("t", 0.8))
            for s in (-1, 1):
                pillow(f"hanger{i}{s}", pp, s * 163, s * 179, mat("alu_dark"), 4, 5)
    for i, p in enumerate(pegs):
        for s in (-1, 1):
            tube(f"peg{i}{s}", [(p[0], s * 170, p[1]), (p[0], s * 255, p[1])], 12, mat("alu"))
    if len(pegs) > 1:
        for s in (-1, 1):
            n3 = nodes.get("S3", (470, 780))
            tube(f"pillionhanger{s}", [(n3[0], s * 112, n3[1]), (pegs[1][0], s * 168, pegs[1][1])], 8, mat("alu_dark"))
    if DETAIL["on"]:
        definition(model, locals())
        import detail3d
        detail3d.run(sys.modules[__name__], model, locals())
        # bespoke chopper parts to order: model detail.bespoke {"pegs": [design, metal], "covers": [design, metal]},
        # or for a trial fit BIKE_BESPOKE="pegs=torpedo:gold,covers=spoked:gold"
        bs = dict(model.get("detail", {}).get("bespoke", {}))
        for kv in filter(None, os.environ.get("BIKE_BESPOKE", "").split(",")):
            bs[kv.split("=")[0]] = kv.split("=")[1].split(":")
        if bs:
            import chopper_parts3d
            chopper_parts3d.use(sys.modules[__name__])
            if "pegs" in bs:
                chopper_parts3d.fit_pegs(model, *bs["pegs"])
            if "covers" in bs:
                chopper_parts3d.fit_covers(model, *bs["covers"])
            if "lamps" in bs:
                chopper_parts3d.fit_lamps(model, *bs["lamps"])
            if "engine" in bs:
                chopper_parts3d.fit_engine(model, *bs["engine"])
        lr = model.get("detail", {}).get("lock_relief")
        if lr:
            import lock_relief
            lock_relief.relieve(sys.modules[__name__], model, lr["deg"] if isinstance(lr, dict) else lr, tuple(lr.get("parts", ("tank",))) if isinstance(lr, dict) else ("tank",))
    return dict(A=A, F=F, wb=wb)


DETAIL = {"on": False}      # --definition: the Definition pass parts, on top of the outline build


def sphere(name, at, r, material):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=r * MM, location=V(*at), segments=20, ring_count=12)
    ob = bpy.context.object
    ob.name = name
    ob.data.materials.append(material)
    bpy.ops.object.shade_smooth()
    return ob


def definition(model, c):
    """Definition pass (plan doc, 14 Nov): the connective and close-look detail a real bike has, added on top of
    the outline build without moving any part, so every fit and clearance check still holds. Hoses and cables,
    cast lugs at the frame joints, plug caps and leads, fork seals, lamp bezel, filler cap bolts, seat seams."""
    F, uf, nb, fk, top = c["F"], c["uf"], c["nb"], c["fk"], c["top"]
    eng, crank, wide, nodes, half, fr, framem = c["eng"], c["crank"], c["wide"], c["nodes"], c["half"], c["fr"], c["framem"]
    body, deck = model["body"], model.get("deck", {"nose": [700, 850]})
    nose = deck["nose"]
    hose, steel, dark = mat("rubber"), mat("steel"), mat("plastic")

    def P(q, y):
        return (q[0], y, q[1])
    # frame joints: a smooth cast lug wherever main tubes meet
    seen = set()
    for tb in fr["tubes"]:
        if tb.get("cls") == "sub":
            continue
        for n_ in tb["path"]:
            if n_ in seen or n_ in ("head_t", "head_b"):
                continue
            seen.add(n_)
            for sg in (-1, 1):
                sphere(f"lug_{n_}{sg}", P(nodes[n_], sg * half(nodes[n_])), tb.get("w", 26) / 2 + 3.5, framem)
    # fork seals and pinch bolts
    stan = add(F, uf, fk["slider_top"])
    for sg in (-1, 1):
        y = sg * 105
        tube(f"forkseal{sg}", [P(add(stan, uf, -12), y), P(add(stan, uf, 4), y)], 31.5, dark)
        for d_ in (-14, 22):
            q = add(add(F, uf, d_), nb, -30)
            cone(f"pinch{sg}{d_}", P(q, y - 12), P(q, y + 12), 5, 5, steel, 8)
        for at in (fk["length"] - 22, fk["lower_yoke"]):
            q = add(add(F, uf, at), nb, -36)
            cone(f"yokebolt{sg}{at}", P(q, y - 10), P(q, y + 10), 6, 6, steel, 8)
    # brake hoses: front caliper up the fork leg to the right bar, rear caliper along the swingarm
    cal = model.get("front_caliper")
    grip = next((s_["at"] for s_ in model["front_end"] if s_.get("circle") == 20), [top[0] - 60, top[1] + 60])
    if cal:
        cc = (sum(q[0] for q in cal) / len(cal), sum(q[1] for q in cal) / len(cal))
        smooth_tube("brakehose_f", [P(cc, -92), P(add(cc, uf, 90), -128), P(add(add(F, uf, fk["slider_top"]), nb, -40), -132),
                                    P(add(add(F, uf, fk["lower_yoke"]), nb, -70), -96), P(add(top, nb, -70), -70), (grip[0] + 70, -150, grip[1] - 6), (grip[0] + 30, -190, grip[1] - 4)], 5, hose)
        cone("brakeres", (grip[0] + 26, -196, grip[1] + 2), (grip[0] + 26, -196, grip[1] + 24), 17, 17, dark, 20)
    rc = model.get("rear_caliper")
    piv = model["swingarm"]["pivot"]
    if rc:
        cc = (sum(q[0] for q in rc["body"]) / len(rc["body"]), sum(q[1] for q in rc["body"]) / len(rc["body"]))
        smooth_tube("brakehose_r", [P(cc, -100), (cc[0] + 90, -118, cc[1] + 40), (piv[0] - 120, -128, piv[1] + 30), (piv[0] + 30, -150, piv[1] + 70), (piv[0] + 40, -168, piv[1] + 130)], 5, hose)
        cone("rearres", (piv[0] + 40, -168, piv[1] + 124), (piv[0] + 40, -168, piv[1] + 170), 15, 15, dark, 16)
    # clutch and throttle cables from the bars down past the headstock
    cs_ = eng["countershaft"]
    smooth_tube("clutchcable", [(grip[0] + 30, 190, grip[1] - 4), (grip[0] + 80, 150, grip[1] - 10), P(add(top, nb, -60), 60), P(add(add(F, uf, fk["lower_yoke"]), nb, 40), 70),
                                (crank[0] + 90, wide / 2 - 20, crank[1] + 190), (cs_[0] + 60, wide / 2 + 2, cs_[1] + 70)], 4, hose)
    for k in (0, 1):
        smooth_tube(f"throttle{k}", [(grip[0] + 20, -186, grip[1] - 14 - 8 * k), (grip[0] + 90, -140, grip[1] - 20), P(add(top, nb, -56), -40 - 14 * k),
                                     P(add(add(F, uf, fk["lower_yoke"] + 40), nb, 50), -60), (nose[0] + 170, -70, nose[1] - 30)], 3.5, hose)
    # radiator hoses with clips
    r = model.get("radiator")
    if r and eng["cylinders"]:
        u = unit(r["angle"])
        a, b = tuple(r["at"]), add(tuple(r["at"]), u, r["len"])
        cy = eng["cylinders"][0]
        uc = unit(cy["angle"])
        headend = add(crank, uc, cy["start"] + cy["length"] + cy["head"] * 0.5)
        runs = (("radhose_t", [P(add(b, u, -18), 118), (b[0] - 50, 132, b[1] + 6), ((b[0] + headend[0]) / 2, 124, (b[1] + headend[1]) / 2 + 30), (headend[0] + 20, 84, headend[1] + 10), P(headend, 60)]),
                ("radhose_b", [P(add(a, u, 18), -118), (a[0] - 40, -128, a[1] - 20), (crank[0] + 150, -wide / 2 + 10, crank[1] + 60), (crank[0] + 110, -wide / 2 + 36, crank[1] + 30)]))
        for nm, pts in runs:
            smooth_tube(nm, pts, 13, hose)
            for e_ in (0, -1):
                d0, d1 = pts[e_], pts[1 if e_ == 0 else -2]
                k_ = 0.25
                cone(nm + f"clip{e_}", tuple(d0[i] + (d1[i] - d0[i]) * k_ for i in range(3)), tuple(d0[i] + (d1[i] - d0[i]) * (k_ + 0.12) for i in range(3)), 15, 15, steel, 16)
    # plug caps and leads on separate cylinders
    if eng["layout"] not in ("inline4", "triple"):
        for i, cy in enumerate(eng["cylinders"]):
            uc = unit(cy["angle"])
            q = add(crank, uc, cy["start"] + cy["length"] + cy["head"] * 0.55)
            rr_ = cy["bore_w"] / 2 + 10
            cone(f"plugcap{i}", P(q, rr_ - 4), P(q, rr_ + 34), 9, 7, dark, 12)
            smooth_tube(f"pluglead{i}", [P(q, rr_ + 30), (q[0] - 10, rr_ + 46, q[1] + 30), (nose[0] + 120, 86, nose[1] - 50), (nose[0] + 90, 50, nose[1] - 24)], 3.5, hose)
    # lamp bezel behind a flat lens
    for i, shp in enumerate(model["front_end"]):
        if shp.get("cls") == "lens" and "pts" in shp and not shp.get("round") and model["family"].lower() != "sport":
            pp = shapes.dense(shp["pts"], True, shp.get("t", 0.8))
            cx_, cz_ = sum(q[0] for q in pp) / len(pp), sum(q[1] for q in pp) / len(pp)
            pillow(f"bezel{i}", [(cx_ + (q[0] - cx_) * 1.12 + 11, cz_ + (q[1] - cz_) * 1.16 - 3) for q in pp], -90, 90, mat("chrome"), 5, 14)
    # filler cap: a ring of small bolts
    for i, cp in enumerate(body.get("caps", [])):
        for k in range(6):
            a_ = math.radians(60 * k + 30)
            x_, y_ = cp["at"][0] + cp["r"] * 0.74 * math.cos(a_), cp["r"] * 0.74 * math.sin(a_)
            cone(f"capbolt{i}{k}", (x_, y_, cp["at"][1] + 1), (x_, y_, cp["at"][1] + 6.5), 2.6, 2.6, steel, 8)
    # seat seams: a raised stitched seam along each shoulder of the seat
    st = next((s_ for s_ in body.get("seat", []) if s_.get("cls") == "seat"), None)
    if st:
        outline = shapes.dense(st["pts"], True, st.get("t", 1.0))
        xs = [q[0] for q in outline]
        x0, x1, spec = min(xs), max(xs), WIDTHS["seat"]
        for sg in (-1, 1):
            pts = []
            for k in range(8, 93, 4):
                u = k / 100
                tb = shapes.top_bottom(outline, x0 + (x1 - x0) * u)
                if not tb:
                    continue
                w = shapes.lerp_table(spec["w"], u) * (0.3 + 0.7 * math.sqrt(1.0 - (1.0 - min(1.0, min(u, 1 - u) / 0.1)) ** 2))
                zc, h = (tb[0] + tb[1]) / 2, (tb[0] - tb[1]) / 2
                f_ = 0.7
                pts.append((x0 + (x1 - x0) * u, sg * w * f_, zc + h * (1 - f_ ** spec["n"]) ** (1 / spec["n"]) + 0.5))
            if len(pts) > 3:
                smooth_tube(f"seam{sg}", pts, 2.4, mat("seat"))
    # crankcase: a ring of bolts round each side, a filler cap and a sight glass
    case = shapes.dense(eng["case"], True, 0.9)
    ccx, ccz = sum(q[0] for q in case) / len(case), sum(q[1] for q in case) / len(case)
    step = max(1, len(case) // 14)
    for sg in (-1, 1):
        y0 = sg * (wide / 2 - 26)
        for k, q in enumerate(case[::step]):
            d_ = math.hypot(q[0] - ccx, q[1] - ccz) or 1.0
            b_ = (q[0] - (q[0] - ccx) / d_ * 40, q[1] - (q[1] - ccz) / d_ * 40)
            cone(f"casebolt{sg}{k}", P(b_, y0), P(b_, y0 + sg * 9), 5.5, 5.5, steel, 6)
    cone("oilcap", (crank[0] - 150, wide / 2 - 30, crank[1] + 70), (crank[0] - 150, wide / 2 - 4, crank[1] + 70), 17, 15, mat("alu"), 12)
    cone("sightglass", (crank[0] - 120, -wide / 2 + 24, crank[1] - 70), (crank[0] - 120, -wide / 2 + 14, crank[1] - 70), 13, 13, mat("lens"), 20)
    # axle nuts and the swingarm pivot caps: hexagons, not bare tube ends
    for sg in (-1, 1):
        cone(f"axlenut_f{sg}", (F[0], sg * 124, F[1]), (F[0], sg * 138, F[1]), 15, 15, steel, 6)
        cone(f"axlenut_r{sg}", (c["A"][0], sg * 150, c["A"][1]), (c["A"][0], sg * 164, c["A"][1]), 17, 17, steel, 6)
        cone(f"pivotcap{sg}", (piv[0], sg * 176, piv[1]), (piv[0], sg * 186, piv[1]), 15, 13, mat("alu"), 6)
    # bodywork: a shut line along the waist of the tail and a round badge on each side of the tank
    for part, f_y in (("tail", 1.0), ("tank", 1.0)):
        shp = next((s_ for s_ in body.get(part, []) if s_.get("cls") == "body" and "pts" in s_), None)
        if not shp:
            continue
        outline = shapes.dense(shp["pts"], True, shp.get("t", 1.0))
        xs = [q[0] for q in outline]
        x0, x1, spec = min(xs), max(xs), WIDTHS[part]

        def side(u, dz=0.0):
            tb = shapes.top_bottom(outline, x0 + (x1 - x0) * u)
            w = shapes.lerp_table(spec["w"], u) * (0.3 + 0.7 * math.sqrt(1.0 - (1.0 - min(1.0, min(u, 1 - u) / 0.1)) ** 2))
            return (x0 + (x1 - x0) * u, w, (tb[0] + tb[1]) / 2 + dz) if tb else None
        for sg in (-1, 1):
            if part == "tail":
                continue      # beenster, 17 Nov: "weird piping along the side of the tail, please remove"
                pts = [side(k / 100) for k in range(14, 90, 6)]
                pts = [(q[0], sg * (q[1] - 0.5), q[2]) for q in pts if q]
                if len(pts) > 3:
                    smooth_tube(f"shutline{sg}", pts, 1.6, dark)
            else:
                q = side(0.56, 14)
                if q:
                    cone(f"badge{sg}", (q[0], sg * (q[1] - 9), q[2]), (q[0], sg * (q[1] + 1.5), q[2]), 24, 22, mat("chrome"), 32)
                    cone(f"badgein{sg}", (q[0], sg * (q[1] + 1.5), q[2]), (q[0], sg * (q[1] + 2.5), q[2]), 18, 17, mat("paint", c["body_c"]), 32)

    # real lamp: clear glass over a bright reflector with two projector eyes, instead of a white slab
    glass = bpy.data.materials.new("lampglass")
    glass.use_nodes = True
    gb = glass.node_tree.nodes["Principled BSDF"]
    gb.inputs["Transmission Weight"].default_value, gb.inputs["Roughness"].default_value, gb.inputs["IOR"].default_value = 1.0, 0.03, 1.45
    for i, shp in enumerate(model["front_end"]):
        if shp.get("cls") != "lens" or "pts" not in shp or shp.get("round"):
            continue
        hit = [o for o in bpy.data.objects if o.name.startswith(f"lens{i}") and o.type == "MESH"]
        if not hit:
            continue
        for o in hit:
            o.data.materials[0] = glass
        pp = shapes.dense(shp["pts"], True, shp.get("t", 0.8))
        cx_, cz_ = sum(q[0] for q in pp) / len(pp), sum(q[1] for q in pp) / len(pp)
        pillow(f"reflector{i}", [(cx_ + (q[0] - cx_) * 0.9 + 2, cz_ + (q[1] - cz_) * 0.9 - 1) for q in pp], -74, 74, mat("chrome"), 3, 10)
        for sg in (-1, 1):
            cone(f"eye{i}{sg}", (cx_ - 2, sg * 38, cz_), (cx_ + 13, sg * 38, cz_), 21, 19, mat("chrome"), 28)
            cone(f"eyeglass{i}{sg}", (cx_ + 13, sg * 38, cz_), (cx_ + 15.5, sg * 38, cz_), 17, 12, mat("lens"), 28)
    # engine castings: a ribbed ring on each side cover, cooling ribs under the sump, an oil filter and a starter motor
    alu = mat("alu")
    for k, cv in enumerate(eng.get("covers", [])):
        cc_, r_ = cv["c"], cv["r"]
        for sg in (-1, 1):
            y0 = sg * (wide / 2 + 2)
            cone(f"coverring{k}{sg}", P(cc_, y0), P(cc_, y0 + sg * 7), r_ * 0.62, r_ * 0.56, alu, 40)
            cone(f"coverboss{k}{sg}", P(cc_, y0 + sg * 7), P(cc_, y0 + sg * 13), r_ * 0.26, r_ * 0.2, alu, 24)
            for j in range(6):
                a_ = math.radians(60 * j)
                p0 = (cc_[0] + r_ * 0.28 * math.cos(a_), cc_[1] + r_ * 0.28 * math.sin(a_))
                p1 = (cc_[0] + r_ * 0.56 * math.cos(a_), cc_[1] + r_ * 0.56 * math.sin(a_))
                cone(f"coverrib{k}{sg}{j}", P(p0, y0 + sg * 8), P(p1, y0 + sg * 6), 3.2, 3.2, alu, 8)
    zb = min(q[1] for q in case)
    xb = [q[0] for q in case if q[1] < zb + 30]
    if xb:
        xa, xz = min(xb) + 30, max(xb) - 30
        for j in range(7):
            x_ = xa + (xz - xa) * j / 6
            prism(f"sumprib{j}", [(x_ - 3, zb - 9), (x_ + 3, zb - 9), (x_ + 3, zb + 12), (x_ - 3, zb + 12)], -wide / 2 + 46, wide / 2 - 46, alu, 1.0)
        xf = max(q[0] for q in case if q[1] < zb + 90)
        cone("oilfilter", (xf - 34, -40, zb + 52), (xf + 34, -40, zb + 44), 33, 33, dark, 24)
        cone("oilfilterend", (xf + 34, -40, zb + 44), (xf + 41, -40, zb + 43), 33, 22, dark, 24)
    # the starter motor: on a single-shock bike whose shock passes it on the centre line, a short motor
    # on the left of the shock (a full-width one would run straight through the spring)
    st_y0, sh_ = -wide / 2 + 40, model.get("shock", {})
    if "top" in sh_ and not sh_.get("twin"):
        a_, b_, p_ = sh_["top"], sh_["bottom"], (cs_[0] - 40, cs_[1] + 96)
        t_ = max(0.0, min(1.0, ((p_[0] - b_[0]) * (a_[0] - b_[0]) + (p_[1] - b_[1]) * (a_[1] - b_[1])) / ((a_[0] - b_[0]) ** 2 + (a_[1] - b_[1]) ** 2)))
        if math.hypot(p_[0] - b_[0] - t_ * (a_[0] - b_[0]), p_[1] - b_[1] - t_ * (a_[1] - b_[1])) < 82:
            st_y0 = 56 if wide / 2 - 96 - 56 >= 50 else None
    sx_, sz_ = cs_[0] - 40, cs_[1] + 96
    if st_y0 is None:       # a narrow engine has no room on the left: the motor goes to the right of the shock
        cone("starter", (sx_, -wide / 2 + 40, sz_), (sx_, -46, sz_), 30, 30, dark, 24)
        cone("starterend", (sx_, -wide / 2 + 40, sz_), (sx_, -wide / 2 + 28, sz_), 30, 20, steel, 24)
    else:
        cone("starter", (sx_, st_y0, sz_), (sx_, wide / 2 - 96, sz_), 30, 30, dark, 24)
        cone("starterend", (sx_, wide / 2 - 96, sz_), (sx_, wide / 2 - 84, sz_), 30, 20, steel, 24)
    # seat base: a moulded pan under the foam, so the seat reads as a pad on a base and not one soft lump
    if st:
        outline = shapes.dense(st["pts"], True, st.get("t", 1.0))
        xs = [q[0] for q in outline]
        x0, x1, spec = min(xs), max(xs), WIDTHS["seat"]
        low = []
        for k in range(10, 91, 5):
            tb = shapes.top_bottom(outline, x0 + (x1 - x0) * k / 100)
            if tb:
                low.append((x0 + (x1 - x0) * k / 100, tb[1]))
        if len(low) > 3:
            wmin = min(shapes.lerp_table(spec["w"], k / 100) for k in range(15, 86, 5))
            pillow("seatpan", [(q[0], q[1] - 5) for q in low] + [(q[0], q[1] + 13) for q in reversed(low)], -wmin * 0.93, wmin * 0.93, dark, 3, 8)


def studio(scene):
    bpy.ops.mesh.primitive_plane_add(size=40, location=(0.7, 0, 0))
    bpy.context.object.data.materials.append(mat("floor"))
    w = bpy.data.worlds.new("w")
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.75, 0.77, 0.8, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.3
    scene.world = w
    for loc, energy, size in (((0.7, 3.5, 3.2), 170, 4.0), ((3.5, 2.0, 1.6), 70, 3.0), ((-2.2, 2.5, 1.8), 70, 3.0), ((0.7, -3.0, 2.5), 60, 4.0)):
        bpy.ops.object.light_add(type="AREA", location=loc)
        L = bpy.context.object
        L.data.energy, L.data.size, L.data.shape = energy, size, "DISK"
        L.rotation_euler = _look(loc, (0.7, 0, 0.6))
    if DETAIL["on"]:
        # outdoors: sky light and a tarmac floor, so the bike is judged in the kind of light the game has
        try:
            nt = w.node_tree
            sky = nt.nodes.new("ShaderNodeTexSky")
            for k_, v_ in (("sun_elevation", math.radians(38)), ("sun_rotation", math.radians(235)), ("sun_intensity", 0.35)):
                if hasattr(sky, k_):
                    setattr(sky, k_, v_)
            nt.links.new(sky.outputs[0], nt.nodes["Background"].inputs[0])
            nt.nodes["Background"].inputs[1].default_value = 0.06
        except Exception as ex:      # no sky texture in this Blender: keep the plain studio world
            print("sky skipped:", ex)
        for k_, v_ in (("look", "AgX - Medium High Contrast"), ("exposure", -0.55)):
            try:
                setattr(scene.view_settings, k_, v_)
            except Exception as ex:
                print("view setting skipped:", ex)
        fm = bpy.data.materials.new("tarmac")
        fm.use_nodes = True
        fb = fm.node_tree.nodes["Principled BSDF"]
        fb.inputs["Base Color"].default_value = (0.035, 0.035, 0.038, 1)
        fb.inputs["Roughness"].default_value = 0.86
        nz, bp = fm.node_tree.nodes.new("ShaderNodeTexNoise"), fm.node_tree.nodes.new("ShaderNodeBump")
        nz.inputs["Scale"].default_value, nz.inputs["Detail"].default_value = 260.0, 8.0
        bp.inputs["Strength"].default_value, bp.inputs["Distance"].default_value = 0.6, 0.004
        fm.node_tree.links.new(nz.outputs["Fac"], bp.inputs["Height"])
        fm.node_tree.links.new(bp.outputs["Normal"], fb.inputs["Normal"])
        floor = next(o for o in scene.objects if o.type == "MESH" and o.data.materials and o.data.materials[0].name.startswith("floor"))
        floor.data.materials[0] = fm
        # long softboxes overhead and to the side, so paint, chrome and glass have something real to reflect
        for loc, size, energy in (((0.7, 0.0, 3.6), (5.0, 0.7), 70), ((0.7, 3.2, 1.4), (4.2, 0.45), 36), ((3.8, -0.6, 1.2), (0.5, 2.4), 18)):
            bpy.ops.object.light_add(type="AREA", location=loc)
            L = bpy.context.object
            L.data.shape, L.data.size, L.data.size_y, L.data.energy = "RECTANGLE", size[0], size[1], energy
            L.rotation_euler = _look(loc, (0.7, 0, 0.6))
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.use_denoising = True
    scene.render.film_transparent = False


def _look(loc, target):
    from mathutils import Vector
    d = Vector(target) - Vector(loc)
    return d.to_track_quat("-Z", "Y").to_euler()


def render(name, samples=48, views=False):
    sc = bpy.context.scene
    studio(sc)
    sc.cycles.samples = samples
    out = os.path.join(ROOT, "out", "renders")
    os.makedirs(out, exist_ok=True)
    # side view, framed exactly like foundry/elevation.py (W 1600, H 1000, S 0.56, OX 1275, OY 850)
    cam = bpy.data.cameras.new("side")
    cam.type, cam.ortho_scale = "ORTHO", 1600 / 0.56 * MM
    ob = bpy.data.objects.new("side", cam)
    sc.collection.objects.link(ob)
    cx, cz = (1275 - 800) / 0.56 * MM, (850 - 500) / 0.56 * MM
    ob.location = (cx, 6.0, cz)
    ob.rotation_euler = _look((cx, 6.0, cz), (cx, 0, cz))
    sc.camera = ob
    sc.render.resolution_x, sc.render.resolution_y = 1600, 1000
    sc.render.filepath = os.path.join(out, name + "_side.png")
    bpy.ops.render.render(write_still=True)
    # three-quarter view from the front left
    cam2 = bpy.data.cameras.new("q")
    cam2.lens = 70
    ob2 = bpy.data.objects.new("q", cam2)
    sc.collection.objects.link(ob2)
    ob2.location = (4.3, 3.6, 1.45)
    ob2.rotation_euler = _look(ob2.location, (0.72, 0, 0.55))
    sc.camera = ob2
    sc.render.filepath = os.path.join(out, name + "_q.png")
    bpy.ops.render.render(write_still=True)
    if views:
        # top and front views, straight on, at the same scale as the side view (1 px = 1/0.56 mm)
        for tag, loc, rot, res in (("top", (cx, 0, 6.0), (0, 0, math.pi), (1600, 620)),
                                   ("front", (6.0, 0, cz), None, (620, 1000))):
            c3 = bpy.data.cameras.new(tag)
            c3.type, c3.ortho_scale = "ORTHO", max(res) / 0.56 * MM
            o3 = bpy.data.objects.new(tag, c3)
            sc.collection.objects.link(o3)
            o3.location = loc
            o3.rotation_euler = rot or _look(loc, (0, 0, cz))
            sc.camera = o3
            sc.render.resolution_x, sc.render.resolution_y = res
            sc.render.filepath = os.path.join(out, f"{name}_{tag}.png")
            # from straight above, each studio lamp shows in the paint as its own bright patch, which
            # reads as bands across a tank: light the top view with even light from the whole sky instead
            lamps = [o for o in sc.objects if o.type == "LIGHT"]
            if tag == "top":
                for o in lamps:
                    o.hide_render = True
                sc.world.node_tree.nodes["Background"].inputs[1].default_value = 1.15
            bpy.ops.render.render(write_still=True)
            if tag == "top":
                sc.world.node_tree.nodes["Background"].inputs[1].default_value = 0.3
                for o in lamps:
                    o.hide_render = False
    return out


if __name__ == "__main__":
    name = sys.argv[1]
    if name == "--swatches":
        bpy.ops.wm.read_factory_settings(use_empty=True)
        print(swatches())
        sys.exit(0)
    model = json.load(open(os.path.join(ROOT, "data", "models", name + ".json")))
    # --kit tank_seat,tail,front,exhaust[,extra+extra]  and  --donor <model> [--donor-kit ...]: kit and mixed builds
    import mix

    def kit_args(flag):
        if flag not in sys.argv:
            return None
        v = sys.argv[sys.argv.index(flag) + 1].split(",")
        return [int(x) for x in v[:4]] + [[int(x) for x in v[4].split("+")] if len(v) > 4 and v[4] else []]
    if "--pool" in sys.argv:   # --pool bars,mirrors,lamps,wheels,silencers
        pa = [int(x) for x in sys.argv[sys.argv.index("--pool") + 1].split(",")]
        model = mix.apply_pool(model, *pa)
        name = f"{name}_pool{''.join(str(x) for x in pa)}"
    if "--game" in sys.argv:
        TYRE_LOD["lod"] = "lod1"
    ka = kit_args("--kit")
    if "--donor" in sys.argv:
        dn = sys.argv[sys.argv.index("--donor") + 1]
        donor = json.load(open(os.path.join(ROOT, "data", "models", dn + ".json")))
        dk = kit_args("--donor-kit")
        model = mix.mix(mix.apply_kit(model, *ka) if ka else model, mix.apply_kit(donor, *dk) if dk else donor)
        name = f"{name}+{dn}"
    elif ka:
        model = mix.apply_kit(model, *ka)
        name = f"{name}_kit{''.join(str(x) for x in ka[:4])}{''.join('x' + str(x) for x in ka[4])}"
    if "--definition" in sys.argv:
        DETAIL["on"] = True
        if "--indicators" in sys.argv:
            import detail3d
            detail3d.VARIANT["indicators"] = sys.argv[sys.argv.index("--indicators") + 1]
        name += "_def"
    build(model)
    if os.environ.get("BIKE_DUMP"):      # a list of every built part with its surface and where it is, for checking
        bpy.context.view_layer.update()
        with open(os.environ["BIKE_DUMP"], "w") as f_:
            for ob in sorted(bpy.data.objects, key=lambda o: o.name):
                bb = [ob.matrix_world @ __import__("mathutils").Vector(q) for q in ob.bound_box]
                mt = ob.data.materials[0].name if getattr(ob.data, "materials", None) and len(ob.data.materials) else "-"
                f_.write(f"{ob.name}\t{mt}\t" + " ".join(f"{min(getattr(q, a) for q in bb) / MM:.0f}..{max(getattr(q, a) for q in bb) / MM:.0f}" for a in "xyz") + "\n")
        if os.environ.get("BIKE_DUMP_ONLY"):
            sys.exit(0)
    if "--glb" in sys.argv:
        os.makedirs(os.path.join(ROOT, "out", "glb"), exist_ok=True)
        bpy.ops.export_scene.gltf(filepath=os.path.join(ROOT, "out", "glb", name + ".glb"), export_apply=True)
    if "--game" in sys.argv:
        # the side stand (common parts' sidestand3d: drawn folded, the game swings it down on its pivot)
        import sidestand3d
        rep_, _made = sidestand3d.fit(sys.modules[__name__], model)
        if rep_.get("fitted"):
            GAME_STAND.update({k: [round(float(q), 4) for q in rep_["stand"][k]] for k in ("pivot", "axis", "folded_dir", "down_dir")})
            GAME_STAND["angle_down_deg"] = round(float(rep_["stand"]["angle_down_deg"]), 2)
        else:
            print("NO SIDE STAND FITTED on", name)
        # (BIKE_GAME_NAME: the files' name, for builds that run side by side on one model: game_kits.py bespoke)
        print("game files in", export_game(os.environ.get("BIKE_GAME_NAME") or name, model, sys.argv[1] if not ("--kit" in sys.argv or "--donor" in sys.argv or "--pool" in sys.argv) else None))
        sys.exit(0)
    if "--clip" in sys.argv or "--clip-accept" in sys.argv:      # part-against-part clipping check, no pictures
        import clip3d
        clip3d.run(name, "--clip-accept" in sys.argv)
        sys.exit(0)
    samples = int(sys.argv[sys.argv.index("--samples") + 1]) if "--samples" in sys.argv else 48
    if "--shots" in sys.argv:      # close-up pictures of single areas, for checking the Definition pass
        import detail3d
        print("shots in", detail3d.shots(sys.modules[__name__], name, sys.argv[sys.argv.index("--shots") + 1].split(","), samples))
        sys.exit(0)
    print("renders in", render(name, samples, "--views" in sys.argv))
