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
    _mats[key] = m
    return m


def export_game(name, model):
    """Writes the bike as the game reads it: Data/BikesV2/<name>.glb (one mesh, one primitive per
    surface, the reader in Source/TorqueShedProto/Fit/PartMesh) and <name>.json (each surface's look)."""
    dst = os.path.join(os.path.dirname(ROOT), "Data", "BikesV2")
    os.makedirs(dst, exist_ok=True)
    obs = [o for o in bpy.context.scene.objects if o.type in ("MESH", "CURVE")]
    bpy.ops.object.select_all(action="DESELECT")
    for o in obs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = obs[0]
    bpy.ops.object.convert(target="MESH")
    obs = [o for o in bpy.context.selected_objects if o.type == "MESH"]
    bpy.context.view_layer.objects.active = obs[0]
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = ob.data.name = name
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(dst, name + ".glb"), use_selection=True, export_apply=True, export_yup=True,
                              export_texcoords=True, export_normals=True, export_materials="EXPORT", export_cameras=False, export_lights=False)
    looks = {}
    for m in ob.data.materials:
        tag = m.name.split(".")[0]
        spec = LIB["surfaces"].get(tag.split("__")[0], {})
        b = m.node_tree.nodes["Principled BSDF"]
        look = {"ue": spec.get("ue", "cat_brushed_steel")}
        if spec.get("ue_tint", True):
            look["color"] = [round(v, 4) for v in b.inputs["Base Color"].default_value[:3]]
            look["metallic"] = round(b.inputs["Metallic"].default_value, 3)
            look["roughness"] = round(b.inputs["Roughness"].default_value, 3)
        looks[m.name] = look
    g = model["geometry"]
    info = {"name": model["name"], "brand": model["brand"], "family": model["family"], "rev": model.get("rev"),
            "wheelbase_mm": g["wheelbase"], "seat_height_mm": g["seat_height"], "surfaces": looks}
    json.dump(info, open(os.path.join(dst, name + ".json"), "w"), indent=1)
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


def mesh_obj(name, verts, faces, material, smooth=True, bevel=0.0):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    me.materials.append(material)
    if smooth:
        me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
        try:
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


def wheel(tag, c, w, width, rim_col, discs):
    rt, rr, hw = w["tyre_r"], w["rim_r"], width / 2
    prof = [(rr + 4, hw * 0.78), (rr + 30, hw * 0.98), (rt - 42, hw), (rt - 16, hw * 0.78), (rt - 3, hw * 0.4), (rt, 0),
            (rt - 3, -hw * 0.4), (rt - 16, -hw * 0.78), (rt - 42, -hw), (rr + 30, -hw * 0.98), (rr + 4, -hw * 0.78), (rr - 4, 0)]
    revolve(tag + "_tyre", prof, c, mat("rubber"))
    rw = hw * 0.8
    rprof = [(rr + 8, rw), (rr + 8, rw - 8), (rr - 8, rw - 14), (rr - 22, 0), (rr - 8, -rw + 14), (rr + 8, -rw + 8), (rr + 8, -rw), (rr - 26, -rw + 6), (rr - 34, 0), (rr - 26, rw - 6)]
    revolve(tag + "_rim", rprof, c, mat("rim", rim_col))
    n = w.get("spokes", 5)
    for k in range(n):
        a0 = 360 / n * k + w.get("phase", 12)
        for side in (-1, 1):
            a_h, a_r = a0 + side * 16, a0 + side * 9 + 20
            quad = [add(c, unit(a_h - 7), 50), add(c, unit(a_r - 3.2), rr - 24), add(c, unit(a_r + 3.2), rr - 24), add(c, unit(a_h + 7), 50)]
            pillow(f"{tag}_spoke{k}{side}", quad, -15, 15, mat("rim", rim_col), 5, 12)
    revolve(tag + "_hub", [(18, hw * 0.7), (56, hw * 0.55), (56, -hw * 0.55), (18, -hw * 0.7)], c, mat("rim", rim_col), 32)
    d = w.get("disc")
    for y in discs if d else []:
        revolve(f"{tag}_disc{y}", [(d, y + 2.5), (d, y - 2.5), (d - 36, y - 2.5), (d - 36, y + 2.5)], c, mat("steel"), 64)
        revolve(f"{tag}_carrier{y}", [(d - 34, y + 3), (d - 34, y - 3), (d - 72, y - 3), (d - 72, y + 3)], c, mat("alu_dark"), 48)


def loft(name, outline, spec, material, stations=56, ring=30):
    """A body part with real width: superellipse sections between the outline's top and bottom lines."""
    xs = [p[0] for p in outline]
    x0, x1 = min(xs), max(xs)
    rows = []
    for i in range(stations):
        u = (i + 0.5) / stations
        x = x0 + (x1 - x0) * u
        tb = shapes.top_bottom(outline, x)
        if not tb or tb[0] - tb[1] < 2:
            continue
        zc, h, w = (tb[0] + tb[1]) / 2, (tb[0] - tb[1]) / 2, shapes.lerp_table(spec["w"], u)
        edge = min(u, 1 - u) * stations          # round the two ends off
        w *= min(1.0, 0.35 + 0.65 * math.sqrt(min(1.0, edge / 3.0)))
        row = []
        for j in range(ring):
            th = 2 * math.pi * j / ring
            cs, sn = math.cos(th), math.sin(th)
            e = 2.0 / spec["n"]
            y = w * math.copysign(abs(cs) ** e, cs)
            z = zc + h * math.copysign(abs(sn) ** e, sn)
            if sn < 0:
                y *= 1 - (1 - spec["bottom"]) * abs(sn)
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
    ob = mesh_obj(name, verts, faces, material, True, 0)
    md = ob.modifiers.new("sub", "SUBSURF")
    md.levels = md.render_levels = 1
    return ob


def arc_band(name, a, hw, material):
    prof = [(a["r0"], hw), (a["r1"], hw * 0.7), (a["r1"] + 6, 0), (a["r1"], -hw * 0.7), (a["r0"], -hw), (a["r0"] - 4, 0)]
    return revolve(name, prof, tuple(a["c"]), material, 36, a["a0"], a["a1"])


def build(model):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    _mats.clear()
    FINISH["name"] = model.get("finish") or LIB["brand_finish"].get(model.get("brand"), "gloss")
    g, wh, col = model["geometry"], model["wheels"], model["colours"]
    body_c, frame_c, rim_c = hex_rgb(col["body"]), hex_rgb(col["frame"]), hex_rgb(col.get("rim", "#2b2d31"))
    paint, framem = mat("paint", body_c), mat("frame", frame_c)
    wb, rake = g["wheelbase"], math.radians(g["rake"])
    A, F = (0, wh["rear"]["tyre_r"]), (wb, wh["front"]["tyre_r"])
    uf, nb = (-math.sin(rake), math.cos(rake)), (-math.cos(rake), -math.sin(rake))
    fk = model["fork"]
    top = add(F, uf, fk["length"])
    head_t, head_b = add(add(F, uf, fk["length"] - 22), nb, fk["offset"]), add(add(F, uf, fk["lower_yoke"] + 18), nb, fk["offset"])

    # wheels
    wheel("front", F, wh["front"], 120, rim_c, (-78, 78))
    wheel("rear", A, wh["rear"], 180, rim_c, (-82,))

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

    # frame tubes: half-width grows from the head back to the pivot
    fr = model["frame"]
    nodes = {k: tuple(v) for k, v in fr["nodes"].items()}
    nodes.update(head_t=head_t, head_b=head_b)

    def half(p):
        u = (head_t[0] - p[0]) / max(1.0, head_t[0] - 600)
        return 22 + max(0.0, min(1.0, u)) * 118

    for i, tb in enumerate(fr["tubes"]):
        pts = [nodes[n] for n in tb["path"]]
        sub = tb.get("cls") == "sub"
        for s in (-1, 1):
            p3 = [(p[0], s * (110 if sub and p[0] < 500 else half(p)), p[1]) for p in pts]
            tube(f"frame{i}{s}", p3, tb["w"] / 2, framem)
    for i, pl in enumerate(fr.get("plates", [])):
        pp = shapes.dense(pl["pts"], True, pl.get("t", 0.6))
        for s in (-1, 1):
            pillow(f"spar{i}{s}", pp, s * 112, s * 158, framem, 16, 22, both=False)
    # engine
    eng = model["engine"]
    crank = tuple(eng["crank"])
    wide = 420 if eng["layout"] in ("inline4", "triple") else 300
    pillow("crankcase", shapes.dense(eng["case"], True, 0.9), -wide / 2 + 10, wide / 2 - 10, mat("engine"), 30, 55)
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
        pillow(f"airbox{i}", shapes.dense(shp["pts"], True, shp.get("t", 0.9)), -112, 112, mat("plastic"), 26, 45)
    r = model.get("radiator")
    if r:
        u = unit(r["angle"])
        n = (-u[1], u[0])
        a, b, hwc = tuple(r["at"]), add(tuple(r["at"]), u, r["len"]), r["w"] / 2
        prism("radiator", [add(a, n, hwc), add(b, n, hwc), add(b, n, -hwc), add(a, n, -hwc)], -150, 150, mat("plastic"), 4)

    # exhaust on the right side
    ex = model["exhaust"]
    # ("side": "both" is one high silencer each side: the first header runs to the left one, the rest to the right)
    both = ex.get("side") == "both"
    for i, hp in enumerate(ex["headers"]):
        pts = shapes.dense(hp["pts"], False, 1.0)[::3] + [tuple(hp["pts"][-1][:2])]
        sg = 1 if both and i == 0 else -1
        out_y = 190 if both else 150
        yy = [sg * (40 + (out_y - 40) * (j / (len(pts) - 1)) ** 1.5) for j in range(len(pts))]
        smooth_tube(f"header{i}", [(p[0], y, p[1]) for p, y in zip(pts, yy)], hp.get("w", 44) / 2 * 0.8, mat("exhaust"))
    for i, can in enumerate(ex.get("cans", [])):
        for sg in ((1, -1) if both else (-1,)):
            y0c, y1c = (190, 205) if both else (165, 175)
            a3, b3 = (can["a"][0], sg * y0c, can["a"][1]), (can["b"][0], sg * y1c, can["b"][1])
            rc = can["w"] / 2 * 0.86

            def at(t, a3=a3, b3=b3):
                return tuple(a3[k] + (b3[k] - a3[k]) * t for k in range(3))
            cone(f"caninlet{i}{sg}", at(-0.06), at(0.14), rc * 0.42, rc, mat("exhaust"), 36)
            cone(f"can{i}{sg}", at(0.14), at(0.9), rc, rc, mat("exhaust"), 36)
            cone(f"canband{i}{sg}", at(0.2), at(0.24), rc + 2.5, rc + 2.5, mat("steel"), 36)
            cone(f"canend{i}{sg}", at(0.9), at(1.0), rc + 2, rc * 0.9, mat("exhaust_black"), 36)
            cone(f"canbore{i}{sg}", at(0.995), at(1.012), rc * 0.6, rc * 0.6, mat("rubber"), 24)

    # swingarm, sprocket, chain, shock
    sw = model["swingarm"]
    so = shapes.dense(sw["outline"], True, 0.9)
    swm = mat("alu") if sw.get("cls") == "alu" else mat("alu_dark")
    sides = (1, -1) if sw.get("cls") == "alu" else (1,)
    for s in sides:
        pillow(f"swingarm{s}", so, s * 112 if len(sides) == 2 else 70, s * 160 if len(sides) == 2 else 152, swm, 14, 20)
    piv = tuple(sw["pivot"])
    tube("pivot", [(piv[0], -160, piv[1]), (piv[0], 160, piv[1])], 14, mat("steel"))
    tube("axle_r", [(A[0], -150, A[1]), (A[0], 165, A[1])], 14, mat("steel"))
    revolve("sprocket", [(sw["sprocket_r"], 100), (sw["sprocket_r"], 94), (40, 94), (40, 100)], A, mat("steel"), 48)
    cs = tuple(eng["countershaft"])
    d = math.hypot(A[0] - cs[0], A[1] - cs[1])
    th = math.atan2(A[1] - cs[1], A[0] - cs[0])
    al = math.acos((36 - sw["sprocket_r"]) / d)
    loop = []
    for sgn in (1, -1):
        v = (math.cos(th + sgn * al), math.sin(th + sgn * al))
        pair = [add(cs, v, 36), add(A, v, sw["sprocket_r"])]
        loop += pair if sgn == 1 else pair[::-1]
    tube("chain", [(p[0], 97, p[1]) for p in loop], 7, mat("steel"), cyclic=True)
    sk = model["shock"]
    tube("shock", [(sk["top"][0], 0, sk["top"][1]), (sk["bottom"][0], 0, sk["bottom"][1])], 16, mat("alu_dark"))
    L = math.hypot(sk["bottom"][0] - sk["top"][0], sk["bottom"][1] - sk["top"][1])
    us = ((sk["bottom"][0] - sk["top"][0]) / L, (sk["bottom"][1] - sk["top"][1]) / L)
    ns = (-us[1], us[0])
    coil = []
    for i in range(121):
        t, a = 40 + (L - 110) * i / 120, 2 * math.pi * 9 * i / 120
        q = add(add(tuple(sk["top"]), us, t), ns, 27 * math.cos(a))
        coil.append((q[0], 27 * math.sin(a), q[1]))
    tube("spring", coil, 5, mat("accent", hex_rgb(col.get("accent", col["body"]))))

    # bodywork
    body = model["body"]
    for part in ("tank", "seat", "tail"):
        shp = body[part][0]
        outline = shapes.dense(shp["pts"], True, shp.get("t", 1.0))
        spec = model.get("widths", {}).get(part, WIDTHS[part])
        loft(part, outline, spec, mat("seat") if part == "seat" else paint)
    for i, shp in enumerate(body.get("tail", [])[1:]):
        if shp["cls"] == "redlens":
            prism(f"taillight{i}", [tuple(p[:2]) for p in shp["pts"]], -45, 45, mat("redlens"), 2)
    for i, shp in enumerate(body.get("front_guard", []) + body.get("panels", [])):
        m_ = paint if shp["cls"] == "body" else mat("plastic")
        if "arc" in shp:
            arc_band(f"guard{i}", shp["arc"], 72 if shp["arc"]["c"][0] > 100 else 100, m_)
        else:
            pp = shapes.dense(shp["pts"], True, shp.get("t", 1.0))
            low = max(p[1] for p in pp) < 400
            if shp.get("shroud"):
                # the fork shroud: a shell round the front of each lower fork leg, joined to the mudguard
                for s in (-1, 1):
                    pillow(f"shroud{i}{s}", pp, s * 72, s * 140, m_, 10, 24)
            elif shp.get("cowl"):
                pillow(f"cowl{i}", pp, -112, 112, m_, 34, 70)
            elif low:
                pillow(f"belly{i}", pp, -125, 125, m_, 22, 50)
            else:
                for s in (-1, 1):
                    pillow(f"panel{i}{s}", pp, s * 128, s * 166, m_, 18, 30, both=False)
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
        if "arc" in shp:
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
        cone("lampbowl0", (x0, 0, zc), (x0 + 0.3 * L, 0, zc), R * 0.35, R * 0.8, mat("plastic"), 40)
        cone("lampbowl1", (x0 + 0.3 * L, 0, zc), (x0 + 0.7 * L, 0, zc), R * 0.8, R, mat("plastic"), 40)
        cone("lampbowl2", (x0 + 0.7 * L, 0, zc), (x1, 0, zc), R, R, mat("plastic"), 40)
        cone("lamprim", (x1 - 6, 0, zc), (x1 + 8, 0, zc), R + 5, R + 3, mat("chrome"), 40)
        cone("lampglass0", (x1 + 6, 0, zc), (x1 + 16, 0, zc), R, R * 0.75, mat("lens"), 40)
        cone("lampglass1", (x1 + 16, 0, zc), (x1 + 22, 0, zc), R * 0.75, R * 0.3, mat("lens"), 40)
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
        if round_lamp and shp.get("part") == "head":
            continue
        if shp["cls"] == "lens":
            pillow(f"lens{i}", [(q[0] + 7, q[1]) for q in pp], -74, 74, mat("lens"), 8, 30)
        elif shp["cls"] == "screen":
            pillow(f"screen{i}", pp, -105, 105, mat("screen"), 30, 70)
        elif max(p[1] for p in pp) > 1120:
            for s in (-1, 1):
                pillow(f"mirror{i}{s}", pp, s * 330, s * 430, mat("plastic"), 7, 16)
                cx = sum(p[0] for p in pp) / len(pp)
                cz = min(p[1] for p in pp)
                if grip:
                    tube(f"mstalk{s}", [(grip[0] + 10, s * 300, grip[1] + 5), (cx, s * 370, cz + 5)], 5, mat("plastic"))
        else:
            pillow(f"front{i}", pp, -90, 90, paint if shp["cls"] == "body" else mat("plastic"), 20, 50)
    if grip:
        for s in (-1, 1):
            tube(f"riser{s}", [(top[0], s * 45, top[1]), (top[0] - 28, s * 45, top[1] + 62)], 15, mat("alu_dark"))
            tube(f"grip{s}", [(grip[0], s * 255, grip[1]), (grip[0] - 6, s * 380, grip[1] - 4)], 17, mat("rubber"))
            tube(f"lever{s}", [(grip[0] + 30, s * 262, grip[1] - 10), (grip[0] + 62, s * 300, grip[1] - 22), (grip[0] + 55, s * 395, grip[1] - 24)], 5, mat("alu_dark"))
        smooth_tube("bar", [(grip[0] - 4, -380, grip[1] - 3), (grip[0], -240, grip[1]), (top[0] - 28, -70, top[1] + 62), (top[0] - 28, 70, top[1] + 62), (grip[0], 240, grip[1]), (grip[0] - 4, 380, grip[1] - 3)], 11.5, mat("alu_dark"))

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
    return dict(A=A, F=F, wb=wb)


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
        L.data.energy, L.data.size = energy, size
        L.rotation_euler = _look(loc, (0.7, 0, 0.6))
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.use_denoising = True
    scene.render.film_transparent = False


def _look(loc, target):
    from mathutils import Vector
    d = Vector(target) - Vector(loc)
    return d.to_track_quat("-Z", "Y").to_euler()


def render(name, samples=48):
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
    build(model)
    if "--glb" in sys.argv:
        os.makedirs(os.path.join(ROOT, "out", "glb"), exist_ok=True)
        bpy.ops.export_scene.gltf(filepath=os.path.join(ROOT, "out", "glb", name + ".glb"), export_apply=True)
    if "--game" in sys.argv:
        print("game files in", export_game(name, model))
        sys.exit(0)
    samples = int(sys.argv[sys.argv.index("--samples") + 1]) if "--samples" in sys.argv else 48
    print("renders in", render(name, samples))
