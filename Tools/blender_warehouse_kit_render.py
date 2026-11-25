"""Review renders of Tools/blender_warehouse_kit.py's units (Cycles, CPU): each unit from the front corner and
the back corner, plus close views of the loading bays, a LOD strip, on a concrete yard in daylight. Plain
procedural materials (profiled sheeting, brick, glass, roller shutters) stand in for the game's."""
import math
import os

import bpy
from mathutils import Vector
from PIL import Image, ImageDraw, ImageFont

# surface: roughness, metallic, pattern (kind, axis, period m, bump strength)
LOOK = {
    "clad": (0.45, 0.25, ("wave", "z", 0.19, 1.0)),
    "clad_v": (0.45, 0.25, ("wave", "h", 0.26, 1.0)),
    "roof": (0.5, 0.3, ("wave", "x", 0.33, 1.0)),
    "roof_flat": (0.8, 0.0, None),
    "tile": (0.7, 0.0, ("wave", "z", 0.22, 0.4)),
    "shutter": (0.4, 0.6, ("wave", "z", 0.08, 0.5)),
    "panel_door": (0.45, 0.2, ("wave", "z", 0.6, 0.25)),
    "louvre": (0.5, 0.4, ("wave", "z", 0.1, 0.8)),
    "brick": (0.85, 0.0, ("brick", None, None, 0.3)),
    "solar": (0.15, 0.2, ("solar", None, None, 0.0)),
    "glass": (0.04, 0.0, None),
    "rooflight": (0.6, 0.0, ("wave", "x", 0.33, 0.3)),
    "frame": (0.4, 0.5, None), "metal": (0.4, 0.4, None), "paint": (0.45, 0.0, None),
    "rubber": (0.8, 0.0, None), "concrete": (0.9, 0.0, ("noise", None, None, 0.1)),
    "sign": (0.5, 0.0, None), "lamp": (0.2, 0.0, None),
}


def _pos_vector(nt, axis):
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(geo.outputs["Position"], sep.inputs[0])
    if axis in ("x", "z", None):
        return geo.outputs["Position"], sep
    add = nt.nodes.new("ShaderNodeMath"); add.operation = "ADD"
    nt.links.new(sep.outputs[0], add.inputs[0]); nt.links.new(sep.outputs[1], add.inputs[1])
    com = nt.nodes.new("ShaderNodeCombineXYZ")
    nt.links.new(add.outputs[0], com.inputs[0]); nt.links.new(sep.outputs[2], com.inputs[2])
    return com.outputs[0], sep


def _material(cache, part):
    key = (part.surface, tuple(round(c, 4) for c in part.rgb))
    if key in cache:
        return cache[key]
    rough, metal, pat = LOOK.get(part.surface, (0.7, 0.0, None))
    m = bpy.data.materials.new("m%d" % len(cache))
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    rgb = tuple(part.rgb) + (1.0,)
    bsdf.inputs["Base Color"].default_value = rgb
    if part.surface == "glass":
        bsdf.inputs["Coat Weight"].default_value = 0.6
    if pat:
        kind, axis, period, strength = pat
        if kind == "wave":
            vec, _ = _pos_vector(nt, axis if axis != "h" else "h")
            wave = nt.nodes.new("ShaderNodeTexWave")
            wave.wave_type = "BANDS"
            wave.bands_direction = "Z" if axis == "z" else "X"
            wave.wave_profile = "SAW" if part.surface in ("clad", "clad_v", "roof", "rooflight") else "SIN"
            wave.inputs["Scale"].default_value = 2 * math.pi / (20.0 * period)
            wave.inputs["Distortion"].default_value = 0.0
            wave.inputs["Detail"].default_value = 0.0
            nt.links.new(vec, wave.inputs["Vector"])
            bump = nt.nodes.new("ShaderNodeBump")
            bump.inputs["Strength"].default_value = strength
            bump.inputs["Distance"].default_value = 0.02
            nt.links.new(wave.outputs["Fac"], bump.inputs["Height"])
            nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
        elif kind in ("brick", "solar"):
            vec, sep = _pos_vector(nt, "h")
            if kind == "solar":
                com = nt.nodes.new("ShaderNodeCombineXYZ")
                nt.links.new(sep.outputs[0], com.inputs[0]); nt.links.new(sep.outputs[1], com.inputs[1])
                vec = com.outputs[0]
            else:
                com = nt.nodes.new("ShaderNodeCombineXYZ")
                add = vec.node.inputs[0].links[0].from_node
                nt.links.new(add.outputs[0], com.inputs[0]); nt.links.new(sep.outputs[2], com.inputs[1])
                vec = com.outputs[0]
            br = nt.nodes.new("ShaderNodeTexBrick")
            nt.links.new(vec, br.inputs["Vector"])
            br.inputs["Scale"].default_value = 1.0
            if kind == "brick":
                br.inputs["Brick Width"].default_value = 0.225
                br.inputs["Row Height"].default_value = 0.075
                br.inputs["Mortar Size"].default_value = 0.01
                br.inputs["Color1"].default_value = rgb
                br.inputs["Color2"].default_value = tuple(c * 0.72 for c in part.rgb) + (1.0,)
                br.inputs["Mortar"].default_value = (0.3, 0.29, 0.27, 1)
            else:
                br.offset = 0.0
                br.inputs["Brick Width"].default_value = 1.05
                br.inputs["Row Height"].default_value = 1.75
                br.inputs["Mortar Size"].default_value = 0.025
                br.inputs["Color1"].default_value = rgb
                br.inputs["Color2"].default_value = tuple(c * 1.3 for c in part.rgb) + (1.0,)
                br.inputs["Mortar"].default_value = (0.4, 0.42, 0.44, 1)
            nt.links.new(br.outputs["Color"], bsdf.inputs["Base Color"])
            if kind == "brick":
                bump = nt.nodes.new("ShaderNodeBump")
                bump.inputs["Strength"].default_value = strength
                nt.links.new(br.outputs["Fac"], bump.inputs["Height"])
                nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
        elif kind == "noise":
            geo = nt.nodes.new("ShaderNodeNewGeometry")
            no = nt.nodes.new("ShaderNodeTexNoise")
            no.inputs["Scale"].default_value = 3.0
            nt.links.new(geo.outputs["Position"], no.inputs["Vector"])
            mul = nt.nodes.new("ShaderNodeMix"); mul.data_type = "RGBA"; mul.blend_type = "MULTIPLY"
            mul.inputs["Factor"].default_value = 0.35
            mul.inputs[6].default_value = rgb
            nt.links.new(no.outputs["Color"], mul.inputs[7])
            nt.links.new(mul.outputs[2], bsdf.inputs["Base Color"])
    cache[key] = m
    return m


def _flat(name, x0, y0, x1, y1, z, rgb, rough=0.9):
    me = bpy.data.meshes.new(name)
    me.from_pydata([(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    mat = bpy.data.materials.new(name); mat.use_nodes = True
    b = mat.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = rgb + (1,); b.inputs["Roughness"].default_value = rough
    me.materials.append(mat)
    return ob


def _scene(units):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = int(os.environ.get("KIT_SAMPLES", "32"))
    sc.cycles.use_denoising = True
    names = [v.identifier for v in type(sc.view_settings).bl_rna.properties["view_transform"].enum_items]
    sc.view_settings.view_transform = "AgX" if "AgX" in names else "Filmic"
    sc.view_settings.exposure = float(os.environ.get("KIT_EXPOSURE", "-2.6"))
    world = bpy.data.worlds.new("w")
    sc.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    sky = world.node_tree.nodes.new("ShaderNodeTexSky")
    try:
        sky.sky_type = "MULTIPLE_SCATTERING"
    except Exception:
        pass
    sky.sun_elevation = math.radians(32)
    sky.sun_rotation = math.radians(215)
    world.node_tree.links.new(sky.outputs["Color"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = 0.35
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 3.2
    sun.angle = math.radians(1.0)
    so = bpy.data.objects.new("sun", sun)
    # (the sun from the south-west, low enough to cast the canopies' and shelters' shadows on the walls)
    d = Vector((math.cos(math.radians(235)), math.sin(math.radians(235)), math.tan(math.radians(32)))).normalized()
    so.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    sc.collection.objects.link(so)
    _flat("grass", -3000, -3000, 3000, 3000, -0.02, (0.05, 0.075, 0.025), 0.95)
    cache = {}
    for U in units:
        for p in U.parts:
            p.obj.data.materials.clear()
            p.obj.data.materials.append(_material(cache, p))
    return sc


def _bounds(U):
    lo = Vector((1e9, 1e9, 1e9)); hi = Vector((-1e9, -1e9, -1e9))
    for p in U.parts:
        for c in p.obj.bound_box:
            w = p.obj.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
    return lo, hi


def _show(sc, U, lod=0):
    mine = {p.obj.name for p in U.parts if p.lod >= lod}
    for ob in sc.collection.objects:
        if ob.type == "MESH" and ob.name not in ("grass",) and not ob.name.startswith("yard"):
            ob.hide_render = ob.name not in mine
        elif ob.name.startswith("yard"):
            ob.hide_render = ob.name != "yard_" + U.name


def _shoot(sc, U, yaw_deg, pitch_deg, size, path, lens=35.0, zoom=1.0, focus=None, lod=0):
    _show(sc, U, lod)
    lo, hi = _bounds(U)
    lo.z = max(lo.z, 0.0)
    centre = (lo + hi) * 0.5
    centre.z = lo.z + (hi.z - lo.z) * 0.35
    radius = (hi - lo).length * 0.5
    if focus is not None:
        centre = Vector(focus)
        radius = zoom
    cam_data = bpy.data.cameras.new("cam"); cam_data.lens = lens; cam_data.clip_end = 20000.0
    cam = bpy.data.objects.new("cam", cam_data)
    sc.collection.objects.link(cam)
    yaw, pitch = math.radians(yaw_deg), math.radians(pitch_deg)
    dist = radius / math.tan(math.atan(18.0 / lens)) * (1.0 if focus is not None else 1.12)
    cam.location = centre + Vector((math.cos(yaw) * math.cos(pitch), math.sin(yaw) * math.cos(pitch), math.sin(pitch))) * dist
    cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = size
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam, do_unlink=True)


def _font(n):
    for cand in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf"):
        if os.path.exists(cand):
            return ImageFont.truetype(cand, n)
    return ImageFont.load_default()


def render_sheet(units, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    sc = _scene(units)
    for U in units:   # (a concrete yard round each unit; the units all sit at the origin, shown one at a time)
        L, W = U.spec["L"], U.spec["W"]
        _flat("yard_" + U.name, -25, -30, L + 15, W + 15, -0.01, (0.32, 0.32, 0.3), 0.85)
    only = os.environ.get("KIT_ONLY")
    size = (800, 480)
    rows = []
    for U in units:
        if only and U.name not in only.split(","):
            continue
        a = os.path.join(out_dir, U.name + "_front.png")
        b = os.path.join(out_dir, U.name + "_back.png")
        _shoot(sc, U, 236, 10, size, a)
        _shoot(sc, U, 42, 13, size, b)
        rows.append((U, a, b))
    font, small = _font(22), _font(17)
    pad, head = 12, 34
    W = 2 * size[0] + 3 * pad
    H = 60 + len(rows) * (size[1] + head + pad)
    sheet = Image.new("RGB", (W, H), (245, 245, 242))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 16), "Ride UK industrial estates: the warehouse kit's first eight units (Blender renders, our own designs)", fill=(20, 20, 20), font=font)
    y = 60
    for i, (U, a, b) in enumerate(rows):
        t = U.tris
        d.text((pad, y + 7), "%d. %s   (%d x %d m;  LOD0 %s / LOD1 %s / LOD2 %s triangles)" % (
            i + 1, U.label, U.spec["L"], U.spec["W"], f"{t[0]:,}", f"{t[1]:,}", f"{t[2]:,}"), fill=(30, 30, 30), font=small)
        y += head
        sheet.paste(Image.open(a).convert("RGB"), (pad, y))
        sheet.paste(Image.open(b).convert("RGB"), (2 * pad + size[0], y))
        y += size[1] + pad
    path = os.path.join(out_dir, "warehouse_kit_sheet.png")
    sheet.save(path)
    print("sheet:", path)
    return path


def render_details(units, out_dir):
    """Close views (loading bays, entrance, terrace) and one unit at LOD0/1/2."""
    os.makedirs(out_dir, exist_ok=True)
    sc = bpy.context.scene
    by = {U.name: U for U in units}
    shots = []
    size = (800, 480)
    plan = [("WH_blue_depot", 255, 10, (60, 0, 4), 16, "Depot loading bays: dock shelters, levellers, bumpers, stairs, bollards"),
            ("WH_big_box", 250, 8, (22, 0, 6), 20, "Big-box office corner: glazed entrance block, brick plinth, ribbon windows"),
            ("WH_trade_terrace", 255, 10, (24, 0, 3), 15, "Trade terrace: roller doors, glazed counters, blank sign boards"),
            ("WH_office_annex", 215, 12, (-5, 5, 4), 16, "Office annex: buff brick, punched windows, low pitched roof")]
    for name, yaw, pitch, focus, zoom, label in plan:
        U = by[name]
        p = os.path.join(out_dir, name + "_close.png")
        _shoot(sc, U, yaw, pitch, size, p, lens=35, zoom=zoom, focus=focus)
        shots.append((label, p))
    U = by["WH_big_box"]
    for lod in range(3):
        p = os.path.join(out_dir, "lod%d.png" % lod)
        _shoot(sc, U, 238, 16, size, p, lod=lod)
        shots.append(("Big-box shed at LOD%d: %s triangles" % (lod, f"{U.tris[lod]:,}"), p))
    font, small = _font(22), _font(17)
    pad, head, cols = 12, 30, 2
    rows = (len(shots) + cols - 1) // cols
    W = cols * size[0] + (cols + 1) * pad
    H = 60 + rows * (size[1] + head + pad)
    sheet = Image.new("RGB", (W, H), (245, 245, 242))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 16), "Warehouse kit: close views and levels of detail", fill=(20, 20, 20), font=font)
    for i, (label, p) in enumerate(shots):
        x = pad + (i % cols) * (size[0] + pad)
        y = 60 + (i // cols) * (size[1] + head + pad)
        d.text((x, y + 5), label, fill=(30, 30, 30), font=small)
        sheet.paste(Image.open(p).convert("RGB"), (x, y + head))
    path = os.path.join(out_dir, "warehouse_kit_details.png")
    sheet.save(path)
    print("details:", path)
    return path
