"""A review sheet of Tools/blender_street_kit.py's lamp posts and telegraph poles, rendered in Blender
(Cycles) with materials made to match M_RideStreetKit: each surface's roughness, metal and texture from
Tools/make_street_kit_textures.py's table and textures. Called by blender_street_kit.py --render DIR."""
import math
import os

import bpy
import numpy as np
from mathutils import Matrix, Vector
from PIL import Image, ImageDraw, ImageFont


def _material(cache, part, tex, surfaces):
    key = (part.surface, tuple(round(c, 4) for c in part.rgb), round(part.dirt, 2))
    if key in cache:
        return cache[key]
    s = next(x for x in surfaces if x[0] == part.surface)
    _, rough, metal, spec, grain, spangle, pits, weather, nstr = s
    m = bpy.data.materials.new("kit_%d" % len(cache))
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    coord = nt.nodes.new("ShaderNodeTexCoord")
    img = nt.nodes.new("ShaderNodeTexImage")
    img.image = bpy.data.images.load(os.path.join(tex, "detail.png"), check_existing=True)
    img.image.colorspace_settings.name = "Non-Color"
    img.projection = "BOX"
    img.projection_blend = 0.3
    nt.links.new(coord.outputs["Object"], img.inputs["Vector"])
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(img.outputs["Color"], sep.inputs["Color"])
    # (colour: the part's, darkened by dirt, varied by its surface's grain / spangle / pits and the weathering)
    def math_node(op, a, b):
        n = nt.nodes.new("ShaderNodeMath"); n.operation = op
        for i, v in enumerate((a, b)):
            if isinstance(v, (int, float)):
                n.inputs[i].default_value = v
            else:
                nt.links.new(v, n.inputs[i])
        return n.outputs[0]
    shade = 1.0 - 0.45 * part.dirt
    f = math_node("MULTIPLY", math_node("SUBTRACT", sep.outputs["Red"], 0.5), 0.7 * grain)
    f = math_node("ADD", f, math_node("MULTIPLY", math_node("SUBTRACT", sep.outputs["Green"], 0.5), 0.5 * spangle))
    f = math_node("ADD", f, math_node("MULTIPLY", math_node("SUBTRACT", sep.outputs["Blue"], 0.5), 0.6 * pits))
    f = math_node("ADD", f, math_node("MULTIPLY", math_node("SUBTRACT", img.outputs["Alpha"], 0.5), 0.5 * weather))
    f = math_node("ADD", f, shade)
    mix = nt.nodes.new("ShaderNodeVectorMath"); mix.operation = "SCALE"
    mix.inputs[0].default_value = tuple(part.rgb)
    nt.links.new(f, mix.inputs["Scale"])
    nt.links.new(mix.outputs[0], bsdf.inputs["Base Color"])
    r = math_node("ADD", math_node("MULTIPLY", math_node("SUBTRACT", sep.outputs["Green"], 0.5), 0.25 * spangle), rough + 0.25 * part.dirt)
    nt.links.new(r, bsdf.inputs["Roughness"])
    bsdf.inputs["Metallic"].default_value = metal * (1.0 - 0.5 * part.dirt)
    if nstr > 0.3:
        bump = nt.nodes.new("ShaderNodeBump")
        bump.inputs["Strength"].default_value = 0.35 * nstr
        bump.inputs["Distance"].default_value = 0.003
        nt.links.new(sep.outputs["Red"] if grain else sep.outputs["Blue"], bump.inputs["Height"])
        nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    cache[key] = m
    return m


def _scene(models, tex, surfaces):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = int(os.environ.get("KIT_SAMPLES", "48"))
    sc.cycles.use_denoising = True
    sc.view_settings.view_transform = "AgX" if "AgX" in [v.identifier for v in type(sc.view_settings).bl_rna.properties["view_transform"].enum_items] else "Filmic"
    sc.view_settings.exposure = -1.6
    world = bpy.data.worlds.new("w")
    sc.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    try:
        sky = world.node_tree.nodes.new("ShaderNodeTexSky")
        sky.sun_elevation = math.radians(32)
        sky.sun_rotation = math.radians(215)
        world.node_tree.links.new(sky.outputs["Color"], bg.inputs["Color"])
        bg.inputs["Strength"].default_value = 0.25
    except Exception:
        bg.inputs["Color"].default_value = (0.55, 0.65, 0.8, 1)
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 3.2
    sun.angle = math.radians(1.5)
    so = bpy.data.objects.new("sun", sun)
    so.rotation_euler = (math.radians(58), 0, math.radians(215 - 180))
    sc.collection.objects.link(so)
    gm = bpy.data.meshes.new("ground")
    gm.from_pydata([(-60, -60, 0), (60, -60, 0), (60, 60, 0), (-60, 60, 0)], [], [(0, 1, 2, 3)])
    g = bpy.data.objects.new("ground", gm)
    sc.collection.objects.link(g)
    gmat = bpy.data.materials.new("grass"); gmat.use_nodes = True
    gb = gmat.node_tree.nodes["Principled BSDF"]
    gb.inputs["Base Color"].default_value = (0.07, 0.1, 0.035, 1); gb.inputs["Roughness"].default_value = 0.95
    gm.materials.append(gmat)
    cache = {}
    for M in models:
        for p in M.parts:
            p.obj.data.materials.clear()
            p.obj.data.materials.append(_material(cache, p, tex, surfaces))
    return sc


def _shoot(sc, objs_visible, target, distance, yaw_deg, pitch_deg, lens, size, path):
    for ob in sc.collection.objects:
        if ob.type == "MESH" and ob.name != "ground":
            ob.hide_render = ob not in objs_visible
    cam_data = bpy.data.cameras.new("cam"); cam_data.lens = lens
    cam = bpy.data.objects.new("cam", cam_data)
    sc.collection.objects.link(cam)
    t = Vector(target)
    yaw, pitch = math.radians(yaw_deg), math.radians(pitch_deg)
    cam.location = t + Vector((math.cos(yaw) * math.cos(pitch), math.sin(yaw) * math.cos(pitch), math.sin(pitch))) * distance
    cam.rotation_euler = (t - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = size
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam, do_unlink=True)


def render_sheet(models, out_dir, tex, surfaces):
    os.makedirs(out_dir, exist_ok=True)
    sc = _scene(models, tex, surfaces)
    by = {M.name: M for M in models}
    objs = lambda name: [p.obj for p in by[name].parts]
    # (each model stands at the origin: shown one at a time)
    shots = [
        ("SM_Prop_street_lamp_0", "LED lamp, galvanised column", (0.5, 0, 3.1), 11.0, 35, 6, 50, (420, 760)),
        ("SM_Prop_street_lamp_1", "Sodium lamp, concrete column", (0.5, 0, 3.1), 11.0, 35, 6, 50, (420, 760)),
        ("SM_Prop_power_pole_0", "Telegraph pole", (0, 0, 4.6), 16.0, 30, 6, 50, (420, 760)),
        ("SM_Prop_power_pole_1", "With stay wire", (0, -1.4, 4.4), 17.0, 20, 6, 50, (420, 760)),
        ("SM_Prop_power_pole_2", "With transformer", (-0.2, 0, 4.6), 16.0, 200, 6, 50, (420, 760)),
        ("SM_Prop_street_lamp_0", "LED lantern", (1.0, 0, 5.85), 2.2, 30, -12, 50, (420, 300)),
        ("SM_Prop_street_lamp_1", "Sodium lantern", (1.0, 0, 5.85), 2.4, 30, -12, 50, (420, 300)),
        ("SM_Prop_power_pole_0", "Pole head", (0, 0, 8.7), 2.8, 30, -8, 50, (420, 300)),
        ("SM_Prop_power_pole_2", "Transformer", (-0.3, 0, 6.6), 4.2, 210, -6, 50, (420, 300)),
        ("SM_Prop_power_pole_1", "Steps and stay guard", (0, -1.0, 1.8), 4.5, 15, 4, 50, (420, 300)),
    ]
    files = []
    for i, (name, label, target, dist, yaw, pitch, lens, size) in enumerate(shots):
        f = os.path.join(out_dir, "shot_%02d.png" % i)
        _shoot(sc, objs(name), target, dist, yaw, pitch, lens, size, f)
        files.append((f, label))
    # (the sheet: the five whole models in a row, the five close-ups under them)
    font = None
    for cand in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf"):
        if os.path.exists(cand):
            font = ImageFont.truetype(cand, 18)
    pad = 10
    W = 5 * 420 + 6 * pad
    H = 760 + 300 + 3 * pad + 2 * 28 + 50
    sheet = Image.new("RGB", (W, H), (245, 245, 242))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 12), "Ride UK street kit: new lamp posts and telegraph poles (Blender renders, one shared material)", fill=(20, 20, 20), font=font)
    for i, (f, label) in enumerate(files):
        im = Image.open(f).convert("RGB")
        col = i % 5
        y = 50 if i < 5 else 50 + 760 + 28 + pad
        x = pad + col * (420 + pad)
        sheet.paste(im, (x, y))
        d.text((x, y + im.size[1] + 4), label, fill=(30, 30, 30), font=font)
    path = os.path.join(out_dir, "street_kit_review.png")
    sheet.save(path)
    print("review sheet:", path)
    return path


def render_signal_sheet(models, out_dir, tex, surfaces):
    """([Nov 19] the traffic signal) its own sheet: whole, the head close, from the side and lit (the lit
    face here is a stand-in for the game's own, M_RideSignal on the clock)."""
    if not models:
        return None
    os.makedirs(out_dir, exist_ok=True)
    sc = _scene(models, tex, surfaces)
    M = models[0]
    parts = [p.obj for p in M.parts]
    # (a lit red face, render only: an emissive disc just in front of the red lens)
    import bmesh
    me = bpy.data.meshes.new("lit")
    bm = bmesh.new()
    bmesh.ops.create_circle(bm, cap_ends=True, radius=0.098, segments=24)
    bm.to_mesh(me); bm.free()
    lit = bpy.data.objects.new("lit", me)
    sc.collection.objects.link(lit)
    lit.matrix_world = Matrix.Translation((0.29, 0, 2.8 + 0.32)) @ Matrix.Rotation(math.pi / 2, 4, "Y")
    mat = bpy.data.materials.new("lit_red"); mat.use_nodes = True
    nt = mat.node_tree; nt.nodes.clear()
    em = nt.nodes.new("ShaderNodeEmission"); em.inputs[0].default_value = (1.0, 0.04, 0.01, 1); em.inputs[1].default_value = 12.0
    out = nt.nodes.new("ShaderNodeOutputMaterial"); nt.links.new(em.outputs[0], out.inputs[0])
    me.materials.append(mat)
    shots = [
        ("Traffic signal, as the traffic sees it", (0.2, 0, 1.9), 7.5, 18, 6, 50, (420, 760), False),
        ("Lit red, from the kerb", (0.2, 0, 1.9), 7.5, 40, 4, 50, (420, 760), True),
        ("From the side: hoods and clamps", (0.1, 0, 2.8), 2.8, 80, 6, 50, (420, 760), True),
        ("The head close", (0.25, 0, 2.8), 2.4, 25, -6, 50, (420, 760), True),
    ]
    files = []
    for i, (label, target, dist, yaw, pitch, lens, size, with_lit) in enumerate(shots):
        f = os.path.join(out_dir, "signal_%02d.png" % i)
        _shoot(sc, parts + ([lit] if with_lit else []), target, dist, yaw, pitch, lens, size, f)
        files.append((f, label))
    font = None
    for cand in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf"):
        if os.path.exists(cand):
            font = ImageFont.truetype(cand, 18)
    pad = 10
    W = 4 * 420 + 5 * pad
    H = 760 + 2 * pad + 28 + 50
    sheet = Image.new("RGB", (W, H), (245, 245, 242))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 12), "Ride UK street kit: new traffic signal (Blender renders; the lit face is the game's own)", fill=(20, 20, 20), font=font)
    for i, (f, label) in enumerate(files):
        im = Image.open(f).convert("RGB")
        x = pad + i * (420 + pad)
        sheet.paste(im, (x, 50))
        d.text((x, 50 + im.size[1] + 4), label, fill=(30, 30, 30), font=font)
    path = os.path.join(out_dir, "traffic_signal_review.png")
    sheet.save(path)
    print("review sheet:", path)
    return path
