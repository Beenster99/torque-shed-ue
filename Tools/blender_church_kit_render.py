"""A review sheet of Tools/blender_church_kit.py's designs, rendered in Blender (Cycles): each church from the
south-west and from the south-east, in plain materials with a little stone texture (stage 2 brings the
kit's shared material). Called by blender_church_kit.py --render DIR."""
import math
import os

import bpy
from mathutils import Vector
from PIL import Image, ImageDraw, ImageFont

LOOK = {"stone": (0.88, 0.0, True), "roof": (0.62, 0.0, True), "glass": (0.08, 0.0, False), "timber": (0.6, 0.0, False),
        "steel": (0.45, 0.8, False)}


def _material(cache, part):
    key = (part.surface, tuple(round(c, 4) for c in part.rgb))
    if key in cache:
        return cache[key]
    rough, metal, textured = LOOK.get(part.surface, (0.8, 0.0, False))
    m = bpy.data.materials.new("c%d" % len(cache))
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if textured:
        # (each stone its own shade: Voronoi cells about a block's size, broken by fine noise; mortar darker)
        co = nt.nodes.new("ShaderNodeTexCoord")
        vor = nt.nodes.new("ShaderNodeTexVoronoi")
        vor.inputs["Scale"].default_value = 2.6 if part.surface == "stone" else 5.0
        nt.links.new(co.outputs["Object"], vor.inputs["Vector"])
        edge = nt.nodes.new("ShaderNodeTexVoronoi")
        edge.feature = "DISTANCE_TO_EDGE"
        edge.inputs["Scale"].default_value = vor.inputs["Scale"].default_value
        nt.links.new(co.outputs["Object"], edge.inputs["Vector"])
        noise = nt.nodes.new("ShaderNodeTexNoise")
        noise.inputs["Scale"].default_value = 14.0
        nt.links.new(co.outputs["Object"], noise.inputs["Vector"])
        mix = nt.nodes.new("ShaderNodeMath"); mix.operation = "MULTIPLY_ADD"
        nt.links.new(vor.outputs["Color"], mix.inputs[0])
        mix.inputs[1].default_value = 0.35
        mix.inputs[2].default_value = 0.82
        mortar = nt.nodes.new("ShaderNodeMapRange")
        nt.links.new(edge.outputs["Distance"], mortar.inputs["Value"])
        mortar.inputs["From Min"].default_value = 0.0
        mortar.inputs["From Max"].default_value = 0.05
        mortar.inputs["To Min"].default_value = 0.6
        mortar.inputs["To Max"].default_value = 1.0
        f = nt.nodes.new("ShaderNodeMath"); f.operation = "MULTIPLY"
        nt.links.new(mix.outputs[0], f.inputs[0]); nt.links.new(mortar.outputs[0], f.inputs[1])
        g = nt.nodes.new("ShaderNodeMath"); g.operation = "MULTIPLY_ADD"
        nt.links.new(noise.outputs["Fac"], g.inputs[0]); g.inputs[1].default_value = 0.3; nt.links.new(f.outputs[0], g.inputs[2])
        sc = nt.nodes.new("ShaderNodeVectorMath"); sc.operation = "SCALE"
        sc.inputs[0].default_value = tuple(part.rgb)
        nt.links.new(g.outputs[0], sc.inputs["Scale"])
        nt.links.new(sc.outputs[0], bsdf.inputs["Base Color"])
        bump = nt.nodes.new("ShaderNodeBump")
        bump.inputs["Strength"].default_value = 0.5
        bump.inputs["Distance"].default_value = 0.02
        nt.links.new(mortar.outputs[0], bump.inputs["Height"])
        nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    else:
        bsdf.inputs["Base Color"].default_value = tuple(part.rgb) + (1.0,)
    cache[key] = m
    return m


def _scene(models):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = int(os.environ.get("KIT_SAMPLES", "40"))
    sc.cycles.use_denoising = True
    names = [v.identifier for v in type(sc.view_settings).bl_rna.properties["view_transform"].enum_items]
    sc.view_settings.view_transform = "AgX" if "AgX" in names else "Filmic"
    sc.view_settings.exposure = -2.9
    world = bpy.data.worlds.new("w")
    sc.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    try:
        sky = world.node_tree.nodes.new("ShaderNodeTexSky")
        sky.sun_elevation = math.radians(35)
        sky.sun_rotation = math.radians(200)
        world.node_tree.links.new(sky.outputs["Color"], bg.inputs["Color"])
        bg.inputs["Strength"].default_value = 0.3
    except Exception:
        bg.inputs["Color"].default_value = (0.55, 0.65, 0.8, 1)
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 3.0
    sun.angle = math.radians(1.5)
    so = bpy.data.objects.new("sun", sun)
    so.rotation_euler = (math.radians(55), 0, math.radians(200 - 180 + 90))
    sc.collection.objects.link(so)
    gm = bpy.data.meshes.new("ground")
    gm.from_pydata([(-3000, -3000, 0), (3000, -3000, 0), (3000, 3000, 0), (-3000, 3000, 0)], [], [(0, 1, 2, 3)])
    g = bpy.data.objects.new("ground", gm)
    sc.collection.objects.link(g)
    gmat = bpy.data.materials.new("grass"); gmat.use_nodes = True
    gb = gmat.node_tree.nodes["Principled BSDF"]
    gb.inputs["Base Color"].default_value = (0.06, 0.09, 0.03, 1); gb.inputs["Roughness"].default_value = 0.95
    gm.materials.append(gmat)
    cache = {}
    for M in models:
        for p in M.parts:
            p.obj.data.materials.clear()
            p.obj.data.materials.append(_material(cache, p))
    return sc


def _bounds(M):
    lo = Vector((1e9, 1e9, 1e9)); hi = Vector((-1e9, -1e9, -1e9))
    for p in M.parts:
        for c in p.obj.bound_box:
            w = p.obj.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
    return lo, hi


def _shoot(sc, M, yaw_deg, pitch_deg, size, path, lens=40.0):
    for ob in sc.collection.objects:
        if ob.type == "MESH" and ob.name != "ground":
            ob.hide_render = ob not in [p.obj for p in M.parts]
    lo, hi = _bounds(M)
    lo.z = max(lo.z, 0.0)
    centre = (lo + hi) * 0.5
    centre.z = lo.z + (hi.z - lo.z) * 0.4   # (a tall spire would otherwise push the walls off the bottom)
    radius = (hi - lo).length * 0.5
    cam_data = bpy.data.cameras.new("cam"); cam_data.lens = lens; cam_data.clip_end = 20000.0
    cam = bpy.data.objects.new("cam", cam_data)
    sc.collection.objects.link(cam)
    yaw, pitch = math.radians(yaw_deg), math.radians(pitch_deg)
    dist = radius / math.tan(math.atan(18.0 / lens)) * (1.45 if hi.z - lo.z > 0.8 * max(hi.x - lo.x, hi.y - lo.y) else 1.2)
    cam.location = centre + Vector((math.cos(yaw) * math.cos(pitch), math.sin(yaw) * math.cos(pitch), math.sin(pitch))) * dist
    cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = size
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam, do_unlink=True)


LABELS = {"SM_Church_dales_chapel": "Dales chapel of ease: gritstone, stone slates, bellcote",
          "SM_Church_norman_tower": "Norman village church: squat west tower, round-headed windows",
          "SM_Church_victorian_spire": "Victorian Gothic town church: aisle, clerestory, broach spire",
          "SM_Church_methodist_chapel": "Methodist chapel: pedimented front, two tiers of windows"}


def render_sheet(models, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    sc = _scene(models)
    # (each church at the origin, one at a time; from the south-west and from the south-east)
    size = (760, 560)
    rows = []
    for M in models:
        a = os.path.join(out_dir, M.name + "_sw.png")
        b = os.path.join(out_dir, M.name + "_se.png")
        _shoot(sc, M, 230, 14, size, a)
        _shoot(sc, M, 300, 10, size, b)
        rows.append((M.name, a, b))
    font = None
    for cand in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf"):
        if os.path.exists(cand):
            font = ImageFont.truetype(cand, 20)
    pad, head = 12, 34
    W = 2 * size[0] + 3 * pad
    H = 56 + len(rows) * (size[1] + head + pad)
    sheet = Image.new("RGB", (W, H), (245, 245, 242))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 14), "Ride UK churches, stage 1: the four first designs (Blender renders, plain materials)", fill=(20, 20, 20), font=font)
    y = 56
    for name, a, b in rows:
        d.text((pad, y + 6), LABELS.get(name, name), fill=(30, 30, 30), font=font)
        y += head
        sheet.paste(Image.open(a).convert("RGB"), (pad, y))
        sheet.paste(Image.open(b).convert("RGB"), (2 * pad + size[0], y))
        y += size[1] + pad
    path = os.path.join(out_dir, "churches_stage1.png")
    sheet.save(path)
    print("review sheet:", path)
    return path
