"""Smoke test for the cloud 3D step: build a wheel-like torus in Blender, render it with Cycles on CPU.

Usage: python3 foundry/render_smoke.py out/smoke.png
"""
import sys
import time

import bpy

out = sys.argv[1] if len(sys.argv) > 1 else "out/smoke.png"
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
bpy.ops.mesh.primitive_torus_add(major_radius=0.26, minor_radius=0.055, major_segments=96, minor_segments=24, rotation=(1.5708, 0, 0), location=(0, 0, 0.315))
tyre = bpy.context.object
m = bpy.data.materials.new("rubber")
m.use_nodes = True
b = m.node_tree.nodes["Principled BSDF"]
b.inputs["Base Color"].default_value = (0.02, 0.02, 0.02, 1)
b.inputs["Roughness"].default_value = 0.7
tyre.data.materials.append(m)
bpy.ops.object.shade_smooth()
bpy.ops.mesh.primitive_plane_add(size=6)
bpy.ops.object.light_add(type="AREA", location=(1.5, -2, 2.5))
bpy.context.object.data.energy = 600
bpy.context.object.data.size = 2
bpy.ops.object.camera_add(location=(0.0, -2.2, 0.5), rotation=(1.48, 0, 0))
sc.camera = bpy.context.object
sc.world = bpy.data.worlds.new("w")
sc.world.use_nodes = True
sc.world.node_tree.nodes["Background"].inputs[1].default_value = 0.6
sc.render.engine = "CYCLES"
sc.cycles.device = "CPU"
sc.cycles.samples = 48
sc.render.resolution_x, sc.render.resolution_y = 640, 360
sc.render.filepath = out
t = time.time()
bpy.ops.render.render(write_still=True)
print("rendered in %.1fs -> %s" % (time.time() - t, out))
