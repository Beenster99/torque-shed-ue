"""Stage 9 check picture: reads every exported part GLB back in (the files load), lays them out by group with a small
red/green/blue axis cross at each file's origin (the mount point), and renders one sheet: out/parts_game/sheet.png.

    python3 foundry/parts_sheet.py [--samples N]
"""
import json
import math
import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "out", "parts_game")


def axis_cross(at, size=0.06):
    for i, col in enumerate(((1, 0.05, 0.05, 1), (0.05, 0.8, 0.05, 1), (0.1, 0.2, 1, 1))):
        m = bpy.data.materials.new(f"ax{i}")
        m.use_nodes = True
        b = m.node_tree.nodes["Principled BSDF"]
        b.inputs["Base Color"].default_value = col
        b.inputs["Emission Color"].default_value = col
        b.inputs["Emission Strength"].default_value = 2.0
        rot = [(0, math.pi / 2, 0), (-math.pi / 2, 0, 0), (0, 0, 0)][i]
        off = [(size / 2, 0, 0), (0, size / 2, 0), (0, 0, size / 2)][i]
        bpy.ops.mesh.primitive_cylinder_add(radius=0.004, depth=size, location=tuple(at[k] + off[k] for k in range(3)), rotation=rot)
        bpy.context.object.data.materials.append(m)


def main(samples=24):
    man = json.load(open(os.path.join(OUT, "parts_manifest.json")))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    parts = [p for p in man["parts"] if p.get("file")]
    cols, pitch = 12, 0.5
    rows = (len(parts) + cols - 1) // cols
    loaded = 0
    for i, p in enumerate(parts):
        c, r = i % cols, i // cols
        pre = set(bpy.data.objects.keys())
        bpy.ops.import_scene.gltf(filepath=os.path.join(OUT, p["file"]))
        new = [bpy.data.objects[n] for n in bpy.data.objects.keys() if n not in pre]
        at = (c * pitch, 0.0, -r * pitch)
        s = min(1.5, 0.36 / max(p["size_mm"]) * 1000)
        for ob in new:
            if ob.parent is None:
                ob.location = at
                ob.scale = (s, s, s)
                # turned about its own origin so the left-right width shows: a three-quarter view from the rear right, from above
                ob.rotation_mode = "XYZ"
                ob.rotation_euler = (math.radians(25), 0, math.radians(55))
        axis_cross(at, 0.05)
        bpy.ops.object.text_add(location=(at[0] - 0.22, -0.3, at[2] - 0.23))
        t = bpy.context.object
        t.data.body = p["id"]
        t.data.size = 0.028
        t.rotation_euler = (math.pi / 2, 0, 0)
        loaded += 1
    sc = bpy.context.scene
    w = bpy.data.worlds.new("w")
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.8, 0.82, 0.85, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.9
    sc.world = w
    cx, cz = (cols - 1) * pitch / 2, -(rows - 1) * pitch / 2
    cam = bpy.data.cameras.new("c")
    cam.type, cam.ortho_scale = "ORTHO", cols * pitch + 0.2
    ob = bpy.data.objects.new("c", cam)
    sc.collection.objects.link(ob)
    ob.location = (cx, -6, cz)
    ob.rotation_euler = (math.radians(90), 0, 0)
    sc.camera = ob
    bpy.ops.object.light_add(type="SUN", rotation=(math.radians(50), math.radians(10), math.radians(-30)))
    bpy.context.object.data.energy = 3
    sc.render.engine, sc.cycles.device, sc.cycles.samples, sc.cycles.use_denoising = "CYCLES", "CPU", samples, True
    sc.render.resolution_x = 2400
    sc.render.resolution_y = int(2400 * (rows * pitch + 0.2) / (cols * pitch + 0.2))
    sc.render.filepath = os.path.join(OUT, "sheet.png")
    bpy.ops.render.render(write_still=True)
    print(f"SHEET {loaded} parts read back in from {len(parts)} files", flush=True)


if __name__ == "__main__":
    a = sys.argv[1:]
    main(int(a[a.index("--samples") + 1]) if "--samples" in a else 24)
