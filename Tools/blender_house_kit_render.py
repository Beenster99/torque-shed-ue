"""Review renders of Tools/blender_house_kit.py's houses (Cycles, CPU): each house from its front corner, plus a
back view and a close look, on a lawn in daylight. Procedural materials stand in for the game's (brick and tile
courses, render, glass)."""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_house_kit as K  # noqa: E402


def _clear():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for me in list(bpy.data.meshes):
        bpy.data.meshes.remove(me)
    for m in list(bpy.data.materials):
        bpy.data.materials.remove(m)


def _mat(role, rgb, code):
    m = bpy.data.materials.new(role)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = tuple(rgb) + (1.0,)
    bsdf.inputs["Roughness"].default_value = 0.85
    tc = nt.nodes.new("ShaderNodeTexCoord")
    if role in ("brick", "brick2", "soldier", "plinth") or (role in ("pot",)):
        br = nt.nodes.new("ShaderNodeTexBrick")
        br.inputs["Scale"].default_value = 1.0
        br.inputs["Brick Width"].default_value = 0.225 if role != "soldier" else 0.075
        br.inputs["Row Height"].default_value = 0.075 if role != "soldier" else 0.225
        br.inputs["Mortar Size"].default_value = 0.01
        br.inputs["Color1"].default_value = tuple(c * 1.15 for c in rgb) + (1,)
        br.inputs["Color2"].default_value = tuple(c * 0.75 for c in rgb) + (1,)
        br.inputs["Mortar"].default_value = (0.16, 0.15, 0.135, 1)
        mp = nt.nodes.new("ShaderNodeMapping")
        # (object coords: the brick runs along x and y; the box mapping by normal)
        nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
        img = nt.nodes.new("ShaderNodeTexImage") if False else None
        sep = nt.nodes.new("ShaderNodeSeparateXYZ")
        nt.links.new(tc.outputs["Object"], sep.inputs[0])
        add = nt.nodes.new("ShaderNodeMath"); add.operation = "ADD"
        nt.links.new(sep.outputs[0], add.inputs[0]); nt.links.new(sep.outputs[1], add.inputs[1])
        com = nt.nodes.new("ShaderNodeCombineXYZ")
        nt.links.new(add.outputs[0], com.inputs[0]); nt.links.new(sep.outputs[2], com.inputs[1])
        nt.links.new(com.outputs[0], br.inputs["Vector"])
        nt.links.new(br.outputs["Color"], bsdf.inputs["Base Color"])
        bump = nt.nodes.new("ShaderNodeBump")
        bump.inputs["Strength"].default_value = 0.4
        nt.links.new(br.outputs["Fac"], bump.inputs["Height"])
        nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    elif role in ("tile", "boards") or (role == "ridge"):
        wave = nt.nodes.new("ShaderNodeTexWave")
        wave.wave_type = "BANDS"
        wave.bands_direction = "Z"
        wave.wave_profile = "SAW"
        period = 0.22 if code > 0.3 else 0.18
        if role == "boards":
            period = 0.15
        wave.inputs["Scale"].default_value = 2 * math.pi / (20.0 * period)
        wave.inputs["Distortion"].default_value = 0.0
        wave.inputs["Detail"].default_value = 0.0
        nt.links.new(tc.outputs["Object"], wave.inputs["Vector"])
        bump = nt.nodes.new("ShaderNodeBump")
        bump.inputs["Strength"].default_value = 0.8
        nt.links.new(wave.outputs["Fac"], bump.inputs["Height"])
        nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
        if role == "tile" and code > 0.3:   # (interlocking tiles: their rolls across the slope too)
            w2 = nt.nodes.new("ShaderNodeTexWave")
            w2.wave_type = "BANDS"; w2.bands_direction = "X"; w2.wave_profile = "SIN"
            w2.inputs["Scale"].default_value = 2 * math.pi / (20.0 * 0.3)
            nt.links.new(tc.outputs["Object"], w2.inputs["Vector"])
            b2 = nt.nodes.new("ShaderNodeBump"); b2.inputs["Strength"].default_value = 0.3
            nt.links.new(w2.outputs["Fac"], b2.inputs["Height"])
            nt.links.new(bump.outputs["Normal"], b2.inputs["Normal"])
            nt.links.new(b2.outputs["Normal"], bsdf.inputs["Normal"])
    elif role in ("glass", "glass_lit"):
        bsdf.inputs["Base Color"].default_value = (0.01, 0.012, 0.015, 1)
        bsdf.inputs["Roughness"].default_value = 0.03
        bsdf.inputs["Coat Weight"].default_value = 0.8
    elif role in ("frame", "trim", "door", "garage", "gutter", "post"):
        bsdf.inputs["Roughness"].default_value = 0.6
    elif role in ("metal", "solar_frame"):
        bsdf.inputs["Metallic"].default_value = 0.9
        bsdf.inputs["Roughness"].default_value = 0.3
    elif role == "solar":
        bsdf.inputs["Roughness"].default_value = 0.15
    elif role in ("lead",):
        bsdf.inputs["Roughness"].default_value = 0.6
    return m


def _objects(spec, H, at=(0, 0, 0), lod=0, cw=0):
    colours = spec["colourways"][cw]
    obs = []
    for (role, plod), (P, N) in H.parts.items():
        if plod < lod or not P:
            continue
        p = np.asarray(P, float).reshape(-1, 3) + np.asarray(at)
        n = len(p) // 3
        me = bpy.data.meshes.new(role)
        me.from_pydata([tuple(v) for v in p], [], [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(n)])
        me.update()
        # (Blender's frame: our y into the house is Blender's y; looking from -y)
        nrm = np.asarray(N, float).reshape(-1, 3)
        try:
            me.normals_split_custom_set([tuple(v) for v in nrm])
        except Exception:
            pass
        ob = bpy.data.objects.new(spec["name"] + ":" + role, me)
        bpy.context.scene.collection.objects.link(ob)
        rgb = K.role_rgb(role, colours)
        code = K.TILE_CODE[spec.get("roofing", "tile")] if role in ("tile", "ridge") else 1.0
        ob.data.materials.append(_mat(role, rgb, code))
        obs.append(ob)
    return obs


def _scene(samples=24):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.view_settings.view_transform = "AgX"
    sc.view_settings.look = "AgX - Medium High Contrast"
    sc.view_settings.exposure = -1.6
    world = bpy.data.worlds.new("w") if not bpy.data.worlds else bpy.data.worlds[0]
    sc.world = world
    world.use_nodes = True
    nt = world.node_tree
    for nd in list(nt.nodes):
        if nd.type != "OUTPUT_WORLD":
            nt.nodes.remove(nd)
    sky = nt.nodes.new("ShaderNodeTexSky")
    try:
        sky.sky_type = "NISHITA"
    except Exception:
        pass
    sky.sun_elevation = math.radians(38)
    sky.sun_rotation = math.radians(210)
    bg = nt.nodes.new("ShaderNodeBackground")
    bg.inputs["Strength"].default_value = 0.25
    nt.links.new(sky.outputs[0], bg.inputs[0])
    nt.links.new(bg.outputs[0], nt.nodes["World Output"].inputs[0])
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 3.2
    sun.angle = math.radians(1.5)
    so = bpy.data.objects.new("sun", sun)
    so.rotation_euler = (math.radians(52), 0, math.radians(-35))
    sc.collection.objects.link(so)


def _ground(size=60):
    me = bpy.data.meshes.new("ground")
    me.from_pydata([(-size, -size, 0), (size, -size, 0), (size, size, 0), (-size, size, 0)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new("ground", me)
    bpy.context.scene.collection.objects.link(ob)
    m = bpy.data.materials.new("lawn")
    m.use_nodes = True
    m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.06, 0.1, 0.035, 1)
    m.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.95
    ob.data.materials.append(m)


def _camera(target, dist, yaw_deg, pitch_deg, lens=40):
    cam = bpy.data.cameras.new("cam")
    cam.lens = lens
    ob = bpy.data.objects.new("cam", cam)
    bpy.context.scene.collection.objects.link(ob)
    yaw, pitch = math.radians(yaw_deg), math.radians(pitch_deg)
    t = Vector(target)
    pos = t + Vector((math.sin(yaw) * math.cos(pitch), -math.cos(yaw) * math.cos(pitch), math.sin(pitch))) * dist
    ob.location = pos
    ob.rotation_euler = (t - pos).to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.camera = ob
    return ob


def shot(spec, H, path, yaw=-35, pitch=12, dist=None, res=(1000, 720), lod=0, cw=0, target=None, lens=40, samples=24):
    _clear()
    _scene(samples)
    _ground()
    x0, x1, y0, y1 = K.footprint(H)
    _objects(spec, H, lod=lod, cw=cw)
    size = max(x1 - x0, y1 - y0)
    if target is None:
        target = ((x0 + x1) / 2, (y0 + y1) / 2, 3.2)
    _camera(target, dist or size * 2.1 + 6, yaw, pitch, lens)
    sc = bpy.context.scene
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)


def render(built, out_dir):
    from PIL import Image, ImageDraw, ImageFont
    os.makedirs(out_dir, exist_ok=True)
    tiles = []
    for spec, H in built:
        for cw in range(len(spec["colourways"]))[:1]:
            p = os.path.join(out_dir, "%s_front.png" % spec["name"])
            shot(spec, H, p, yaw=-32, pitch=10, cw=cw)
            tiles.append((p, spec))
            p2 = os.path.join(out_dir, "%s_back.png" % spec["name"])
            shot(spec, H, p2, yaw=150, pitch=14, cw=min(1, len(spec["colourways"]) - 1), res=(500, 360), samples=12)
    # the sheet: front views with labels
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 22)
    except Exception:
        font = ImageFont.load_default()
    cols = 3
    tw, th = 1000, 720
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * tw, rows * (th + 40)), (240, 240, 238))
    d = ImageDraw.Draw(sheet)
    for i, (p, spec) in enumerate(tiles):
        im = Image.open(p).convert("RGB")
        x, y = (i % cols) * tw, (i // cols) * (th + 40)
        sheet.paste(im, (x, y))
        bp = os.path.join(out_dir, "%s_back.png" % spec["name"])
        if os.path.exists(bp):
            b = Image.open(bp).convert("RGB").resize((300, 216))
            sheet.paste(b, (x + tw - 306, y + 6))
        d.text((x + 12, y + th + 6), "%s  (%s)" % (spec["label"], spec["set"]), fill=(20, 20, 20), font=font)
    sheet.save(os.path.join(out_dir, "house_kit_sheet.jpg"), quality=88)
