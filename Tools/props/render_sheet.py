"""A contact sheet of built props, to look them over without the PC: each glb rendered on its own
from the front three-quarter, tiled into one image.

    blender -b --factory-startup -P Tools/props/render_sheet.py -- OUT.png [id ...]
"""
import math
import os
import sys

import bpy
from mathutils import Vector

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "Data", "Props", "Blender")
args = sys.argv[sys.argv.index("--") + 1:]
out = args[0]
ids = args[1:] or sorted(f[:-4] for f in os.listdir(SRC) if f.endswith(".glb"))
TILE = 360


def scene_setup():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 24
    sc.cycles.use_denoising = True
    sc.render.resolution_x = TILE
    sc.render.resolution_y = TILE
    w = bpy.data.worlds.new("w")
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.57, 0.6, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.6
    sc.world = w
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.5
    sun.rotation_euler = (math.radians(50), math.radians(10), math.radians(35))
    sc.collection.objects.link(sun)
    bpy.ops.mesh.primitive_plane_add(size=40)
    fl = bpy.context.active_object
    m = bpy.data.materials.new("floor")
    m.use_nodes = True
    m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.35, 0.35, 0.34, 1)
    fl.data.materials.append(m)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    return sc, cam


tiles = []
for pid in ids:
    sc, cam = scene_setup()
    bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, pid + ".glb"))
    objs = [o for o in sc.objects if o.type == "MESH" and o.name != "Plane"]
    lo = Vector((min(min((o.matrix_world @ Vector(c))[k] for c in o.bound_box) for o in objs) for k in range(3)))
    hi = Vector((max(max((o.matrix_world @ Vector(c))[k] for c in o.bound_box) for o in objs) for k in range(3)))
    if lo.z > 0.05 or lo.z < -0.05:   # wall props: stand them on the floor for the picture
        for o in objs:
            o.location.z -= lo.z
        hi.z -= lo.z
        lo.z = 0
    c = (lo + hi) / 2
    r = (hi - lo).length / 2
    d = Vector((0.75, -1.0, 0.55)).normalized()   # front (-y in Blender) three-quarter, from above
    cam.data.lens = 50
    cam.location = c + d * r * 2.6
    cam.rotation_euler = (c - cam.location).to_track_quat("-Z", "Y").to_euler()
    path = os.path.join(os.path.dirname(out), "_tile_%s.png" % pid)
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    tiles.append((pid, path))

# Tile them with the names under each.
cols = min(6, len(tiles))
rows = (len(tiles) + cols - 1) // cols
sheet = bpy.data.images.new("sheet", cols * TILE, rows * TILE)
px = [0.0] * (cols * TILE * rows * TILE * 4)
for i, (pid, path) in enumerate(tiles):
    img = bpy.data.images.load(path)
    p = list(img.pixels)
    cx, cy = i % cols, rows - 1 - i // cols
    for y in range(TILE):
        row = (cy * TILE + y) * cols * TILE + cx * TILE
        px[row * 4:(row + TILE) * 4] = p[y * TILE * 4:(y + 1) * TILE * 4]
    os.remove(path)
sheet.pixels = px
sheet.filepath_raw = out
sheet.file_format = "PNG"
sheet.save()
print("[sheet]", out, " ".join(t[0] for t in tiles))
