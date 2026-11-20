"""The ferries plan's stage 2 review sheet: the small ferry (Tools/props/make_ferry.py) tied up at a slipway
with its near ramp down and the far one raised, seen from six sides, on its deck, and the shore kit at the
slipway's head, in daylight with the scanned materials as M_PropMaster draws them (Tools/props/render_real.py's
build). For looking at the models without the PC; the slipway and the water here are stand-ins for the game's
(World/TorqueFerry.cpp lays the real slipway on the real shore).

    SCANS=<folder of <scan>/{color,rough,normal}.jpg> python3 Tools/props/render_ferry_sheet.py -- OUT.png
"""
import math
import os
import sys

import bpy  # noqa: F401  (first: bmesh and mathutils come with it)
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(ROOT, "Data", "Props", "Blender")
out = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else os.path.join(ROOT, "build", "ferry_sheet.png")
TILE = int(os.environ.get("TILE", "640"))

# render_real.py's scan materials, without running it.
_src = open(os.path.join(HERE, "render_real.py")).read()
_src = _src[:_src.rindex("\nmain()")]
_ns = {"__file__": os.path.join(HERE, "render_real.py")}
_argv = sys.argv
sys.argv = ["x", "--", out]
exec(compile(_src, "render_real.py", "exec"), _ns)
sys.argv = _argv
build = _ns["build"]
SPEC = _ns["SPEC"]


def B(x, y, z):
    """Torque Shed's frame (x right, y up, +z front) to Blender's."""
    return Vector((x, -z, y))


def load(pid, at, rot=(0.0, 0.0, 0.0)):
    bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, pid + ".glb"))
    objs = list(bpy.context.selected_objects)
    for o in objs:
        if o.parent is None:
            o.location = at
            o.rotation_mode = "XYZ"
            o.rotation_euler = tuple(math.radians(a) for a in rot)
        if o.type == "MESH":
            for slot in o.material_slots:
                if slot.material and not slot.material.get("built"):
                    build(slot.material, slot.material.name.split(".")[0])
                    slot.material["built"] = 1
    return objs


def plain(name, colour, rough, metal=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*colour, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    return m


def slab(name, corners, mat):
    """A quad from four Torque Shed points, thickened down 0.3 m."""
    me = bpy.data.meshes.new(name)
    vs = [B(*c) for c in corners] + [B(c[0], c[1] - 0.3, c[2]) for c in corners]
    me.from_pydata(vs, [], [(0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)])
    me.update()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    o.data.materials.append(mat)
    bpy.context.view_layer.objects.active = o
    # UVs in metres (the scans tile at their size).
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(me)
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        for lp in f.loops:
            p = lp.vert.co
            lp[uv].uv = (p.x, p.y)
    bm.to_mesh(me)
    bm.free()
    return o


def scene():
    sc, cam = _ns["scene"]()
    # No floor: water, a shore and a slipway instead.
    for o in list(sc.objects):
        if o.type == "MESH":
            bpy.data.objects.remove(o, do_unlink=True)
    sc.render.resolution_x = TILE
    sc.render.resolution_y = int(TILE * 0.66)
    # The sea loch: dark, glossy, a little ruffled.
    bpy.ops.mesh.primitive_plane_add(size=600, location=(0, 0, 0))
    water = bpy.context.active_object
    wm = plain("water", (0.012, 0.03, 0.035), 0.06)
    nt = wm.node_tree
    bump = nt.nodes.new("ShaderNodeBump")
    nz = nt.nodes.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 90.0
    nz.inputs["Detail"].default_value = 6.0
    bump.inputs["Strength"].default_value = 0.12
    nt.links.new(nz.outputs[0], bump.inputs["Height"])
    nt.links.new(bump.outputs[0], nt.nodes["Principled BSDF"].inputs["Normal"])
    water.data.materials.append(wm)
    # The shore: rough grass rising from the water's edge at z 17 (the slipway's foot side) up the bank.
    grass = plain("grass", (0.07, 0.11, 0.035), 0.9)
    shingle = plain("shingle", (0.17, 0.15, 0.12), 0.8)
    slab("Beach", [(-60, -0.6, 15.0), (60, -0.6, 15.0), (60, 1.2, 24.0), (-60, 1.2, 24.0)], shingle)
    slab("Bank", [(-60, 1.2, 24.0), (60, 1.2, 24.0), (60, 4.0, 70.0), (-60, 4.0, 70.0)], grass)
    # The slipway: concrete at 1 in 8 from under the water up onto the bank, weed on its lowest part, the
    # approach road on from its head.
    slab("Slip", [(-3.2, -1.2, 9.0), (3.2, -1.2, 9.0), (3.2, 2.7, 40.0), (-3.2, 2.7, 40.0)], bpy.data.materials.get("slip_concrete") or _mat("slip_concrete"))
    slab("Weed", [(-3.21, -1.18, 9.0), (3.21, -1.18, 9.0), (3.21, 0.32, 21.0), (-3.21, 0.32, 21.0)], _mat("slip_weed"))
    road = plain("road", (0.05, 0.05, 0.05), 0.75)
    slab("Road", [(-3.2, 2.71, 40.0), (3.2, 2.71, 40.0), (3.2, 4.0, 70.0), (-3.2, 4.0, 70.0)], road)
    return sc, cam


def _mat(key):
    m = bpy.data.materials.new(key)
    m.use_nodes = True
    build(m, key)
    return m


def main():
    sc, cam = scene()
    # The boat tied up with its bow (+z) to the slipway, its bow ramp down onto the concrete, the stern's raised.
    load("ferry_small", B(0, 0, 0))
    load("ferry_ramp", B(0, 1.0, 12.6), rot=(13.0, 0, 0))
    load("ferry_ramp", B(0, 1.0, -12.6), rot=(-72.0, 0, 180.0))
    # The shore kit at the slipway's head: the barrier across the lane, the hut and shelter beside it, the sign.
    load("ferry_barrier", B(3.6, 2.7, 37.0), rot=(0, 0, 0))
    load("ferry_barrier_arm", B(3.6, 3.7, 37.0), rot=(0, 0, 180))
    load("ferry_hut", B(7.0, 2.9, 40.0), rot=(0, 0, -90))
    load("ferry_shelter", B(-6.5, 2.9, 42.0), rot=(0, 0, 90))
    load("ferry_sign", B(-4.2, 3.6, 58.0), rot=(0, 0, 180))
    sun = [o for o in sc.objects if o.type == "LIGHT"][0]
    sun.rotation_euler = (math.radians(50), 0, math.radians(200))
    shots = [
        ("bow three-quarter", B(26, 12, 34), B(0, 2.0, 2)),
        ("starboard side (tower)", B(42, 5, 0), B(0, 2.2, 0)),
        ("port side", B(-42, 5, -2), B(0, 2.2, 0)),
        ("stern, raised ramp", B(-18, 4, -32), B(0, 2.4, -6)),
        ("from above", B(0.1, 45, 0), B(0, 0, 0)),
        ("on the deck, by the bike bay", B(-1.0, 2.65, -9.0), B(1.6, 1.6, 6.0)),
        ("riding down the slipway", B(0.6, 4.6, 46.0), B(0, 1.4, 12.0)),
        ("the slipway's head", B(-1.0, 4.4, 30.0), B(2.5, 3.2, 44.0)),
    ]
    tmp = os.path.join(os.path.dirname(os.path.abspath(out)), "_ferry_tiles")
    os.makedirs(tmp, exist_ok=True)
    paths = []
    for i, (name, eye, look) in enumerate(shots):
        cam.location = eye
        cam.rotation_euler = (look - eye).to_track_quat("-Z", "Y").to_euler()
        cam.data.lens = 22 if "deck" in name or "head" in name else 32
        if "above" in name:
            cam.data.lens = 40
        p = os.path.join(tmp, "%d.png" % i)
        sc.render.filepath = p
        bpy.ops.render.render(write_still=True)
        paths.append(p)
        print("shot", i, name, flush=True)
    W, H = sc.render.resolution_x, sc.render.resolution_y
    cols, rows = 2, (len(paths) + 1) // 2
    sheet = bpy.data.images.new("sheet", W * cols, H * rows)
    px = [0.0] * (W * cols * H * rows * 4)
    for i, p in enumerate(paths):
        im = bpy.data.images.load(p)
        src = list(im.pixels)
        cx, cy = i % cols, rows - 1 - i // cols
        for y in range(H):
            row = (cy * H + y) * W * cols + cx * W
            px[row * 4:(row + W) * 4] = src[y * W * 4:(y + 1) * W * 4]
    sheet.pixels = px
    sheet.filepath_raw = out
    sheet.file_format = "PNG"
    sheet.save()
    print("wrote", out)


main()
