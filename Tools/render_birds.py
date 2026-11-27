"""Review pictures of Tools/make_ride_birds.py's models (Blender's Python module, Cycles on the CPU): each
species in each pose, product-studio lit on a plain backdrop, and one contact sheet per species.

    python Tools/make_ride_birds.py --render DIR
    python Tools/render_birds.py MODELS_DIR OUT_DIR [species,species] [--lod N] [--view three|side|top|under]
"""
import math
import os
import sys

import bpy
from mathutils import Vector


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def studio(scene):
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 24
    try:
        scene.cycles.use_denoising = True
    except Exception:
        pass
    scene.render.resolution_x, scene.render.resolution_y = 640, 480
    scene.render.film_transparent = False
    world = bpy.data.worlds.new("w")
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1)
    bg.inputs[1].default_value = 0.9
    for loc, energy, size in (((2.0, -2.0, 2.5), 300.0, 2.0), ((-2.5, -1.0, 1.0), 120.0, 3.0), ((0.0, 2.5, -1.5), 90.0, 3.0)):
        d = bpy.data.lights.new("l", "AREA")
        d.energy = energy
        d.size = size
        o = bpy.data.objects.new("l", d)
        scene.collection.objects.link(o)
        o.location = loc
        o.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()


def vertex_material():
    m = bpy.data.materials.new("bird")
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    a = nt.nodes.new("ShaderNodeVertexColor")
    nt.links.new(a.outputs["Color"], b.inputs["Base Color"])
    # roughness from the alpha (surface): feathers 1 -> 0.8, bare 0.55 -> 0.45, eye 0.15 -> 0.1
    mr = nt.nodes.new("ShaderNodeMapRange")
    nt.links.new(a.outputs["Alpha"], mr.inputs[0])
    mr.inputs[3].default_value = 0.05
    mr.inputs[4].default_value = 0.82
    nt.links.new(mr.outputs[0], b.inputs["Roughness"])
    try:
        b.inputs["Sheen Weight"].default_value = 0.3
    except Exception:
        pass
    return m


def load(path, mat):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    obs = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    for o in obs:
        o.data.materials.clear()
        o.data.materials.append(mat)
    return obs


def frame(scene, obs, view):
    lo = Vector((1e9, 1e9, 1e9)); hi = -lo
    for o in obs:
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
    centre = (lo + hi) * 0.5
    size = max((hi - lo).length, 0.05)
    cam = bpy.data.cameras.new("c")
    cam.lens = 70
    co = bpy.data.objects.new("c", cam)
    scene.collection.objects.link(co)
    d = {"three": Vector((0.9, -1.3, 0.55)), "side": Vector((0.0, -1.0, 0.05)), "top": Vector((0.05, -0.15, 1.0)),
         "under": Vector((0.3, -0.5, -1.0)), "front": Vector((1.0, -0.2, 0.15))}[view].normalized()
    co.location = centre + d * size * 2.3
    co.rotation_euler = (centre - co.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = co


def render_one(path, out, view="three"):
    clear()
    scene = bpy.context.scene
    studio(scene)
    obs = load(path, vertex_material())
    frame(scene, obs, view)
    scene.render.filepath = out
    bpy.ops.render.render(write_still=True)


def sheet(images, out, cols=4):
    from PIL import Image, ImageDraw
    ims = [(Image.open(p), label) for p, label in images if os.path.exists(p)]
    if not ims:
        return
    w, h = ims[0][0].size
    rows = (len(ims) + cols - 1) // cols
    S = Image.new("RGB", (w * cols, h * rows), (40, 40, 40))
    dr = ImageDraw.Draw(S)
    for i, (im, label) in enumerate(ims):
        x, y = (i % cols) * w, (i // cols) * h
        S.paste(im.convert("RGB"), (x, y))
        dr.text((x + 8, y + 6), label, fill=(255, 255, 255))
    S.save(out, quality=88)


def main(models, out_dir, only=None, lod=0, views=("three",)):
    import json
    os.makedirs(out_dir, exist_ok=True)
    spec = json.load(open(os.path.join(models, "birds.json")))["species"]
    for name, info in spec.items():
        if only and name not in only:
            continue
        shots = []
        for pose in info["poses"]:
            for view in views:
                suffix = "" if lod == 0 else "_LOD%d" % lod
                src = os.path.join(models, "SM_Bird_%s_%s%s.glb" % (name, pose, suffix))
                png = os.path.join(out_dir, "%s_%s_%s%s.png" % (name, pose, view, suffix))
                render_one(src, png, view)
                shots.append((png, "%s %s %s%s" % (name, pose, view, suffix)))
        sheet(shots, os.path.join(out_dir, "%s_sheet%s.jpg" % (name, "" if lod == 0 else "_lod%d" % lod)))


if __name__ == "__main__":
    a = sys.argv[1:]
    lod = int(a[a.index("--lod") + 1]) if "--lod" in a else 0
    views = tuple(a[a.index("--view") + 1].split(",")) if "--view" in a else ("three",)
    pos = [x for i, x in enumerate(a) if not x.startswith("--") and (i == 0 or not a[i - 1].startswith("--"))]
    main(pos[0], pos[1], pos[2].split(",") if len(pos) > 2 else None, lod, views)
