"""The trains plan's stage 3 review sheet: every train family (Tools/props/make_trains.py's models) made up as the
game makes them up (World/WorldTrains.cpp's consists, World/TorqueTrains.cpp's livery and lamp overrides) on a
plain stretch of track, in daylight, the other liveries side by side, and one train after dark (lit windows, white
lamps at its front, red at its back). For looking at the models without the PC; the track here is a stand-in.

    [SCANS=<folder of <scan>/{color,rough,normal}.jpg>] python3 Tools/props/render_train_sheet.py -- OUT.png
"""
import math
import os
import sys

import bpy  # noqa: F401
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(ROOT, "Data", "Props", "Blender")
out = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else os.path.join(ROOT, "build", "train_sheet.png")
TILE = int(os.environ.get("TILE", "640"))
ONLY = os.environ.get("ONLY")

_src = open(os.path.join(HERE, "render_real.py")).read()
_src = _src[:_src.rindex("\nmain()")]
_ns = {"__file__": os.path.join(HERE, "render_real.py")}
_argv = sys.argv
sys.argv = ["x", "--", out]
exec(compile(_src, "render_real.py", "exec"), _ns)
sys.argv = _argv
build = _ns["build"]

LEN = {"dmu_car": 23.0, "emu_cab": 20.4, "emu_mid": 20.0, "ic_power": 20.0, "ic_coach": 23.0, "freight_loco": 21.3,
       "wagon_box": 19.6, "wagon_hopper": 15.0, "wagon_tank": 15.5, "steam_tank": 10.0, "steam_big": 13.5,
       "steam_tender": 8.0, "coach_old": 19.5, "diesel_old": 18.0}
FAMILY = {"dmu_car": 0, "emu_cab": 1, "emu_mid": 1, "ic_power": 2, "ic_coach": 2, "freight_loco": 3, "steam_tank": 4,
          "steam_big": 4, "steam_tender": 4, "coach_old": 4, "diesel_old": 4}
GAP = 1.0


def B(x, y, z):
    """Torque Shed's frame (x right, y up, +z front) to Blender's."""
    return Vector((x, -z, y))


_mats = {}


def mat(key):
    if key not in _mats:
        m = bpy.data.materials.new(key)
        m.use_nodes = True
        build(m, key)
        _mats[key] = m
    return _mats[key]


def plain(name, colour, rough, metal=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*colour, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    return m


def box(name, lo, hi, m):
    bpy.ops.mesh.primitive_cube_add(size=1)
    o = bpy.context.active_object
    o.name = name
    c, s = (Vector(lo) + Vector(hi)) * 0.5, Vector(hi) - Vector(lo)
    o.location = c
    o.scale = tuple(abs(v) for v in s)
    o.data.materials.append(m)
    return o


def track(x, z0, z1):
    """Ballast, sleepers and rails along Torque Shed z from z0 to z1 at x; the rails' tops at y 0."""
    ballast = plain("ballast", (0.16, 0.15, 0.14), 0.95)
    sleeper = plain("sleeper", (0.12, 0.11, 0.1), 0.85)
    rail = plain("rail", (0.32, 0.3, 0.28), 0.35, 1.0)
    lo, hi = B(x - 1.9, -0.6, z1), B(x + 1.9, -0.17, z0)
    box("ballast", lo, hi, ballast)
    z = z0
    while z < z1:
        a, b = B(x - 1.3, -0.27, z + 0.13), B(x + 1.3, -0.15, z - 0.13)
        box("sleeper", (a.x, b.y, a.z), (b.x, a.y, b.z), sleeper)
        z += 0.65
    for side in (-1, 1):
        xr = x + side * 0.7175
        a, b = B(xr - 0.035, -0.15, z1), B(xr + 0.035, 0.0, z0)
        box("rail", a, b, rail)


def vehicle(pid, x, z, flip, livery, back, night, dark=False):
    """One vehicle with its middle at (x, 0, z), its front +z (flip: -z), with the game's material overrides."""
    bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, "train_" + pid + ".glb"))
    objs = list(bpy.context.selected_objects)
    fam = FAMILY.get(pid, -1)
    for o in objs:
        if o.parent is None:
            o.location = B(x, 0.0, z)
            o.rotation_mode = "XYZ"
            o.rotation_euler = (0, 0, math.pi if flip else 0.0)
        if o.type != "MESH":
            continue
        for slot in o.material_slots:
            if not slot.material:
                continue
            key = slot.material.name.split(".")[0]
            if fam >= 0 and key in ("train_livery_a", "train_livery_b"):
                key = "train_liv_%d_%d_%s" % (fam, livery, key[-1])
            elif key == "train_lamp_head" and (back or dark):
                key = "train_lamp_off"
            elif key == "train_lamp_tail" and (not back or dark):
                key = "train_lamp_off"
            elif key == "train_window" and night:
                key = "train_window_lit"
            slot.material = mat(key)


def consist(parts, x, livery=0, night=False):
    """parts: (model, flip) front first; the front of the train at z 0, the rest behind (-z). Returns its length."""
    z = 0.0
    n = len(parts)
    for i, (pid, flip) in enumerate(parts):
        L = LEN[pid]
        mid = z - L * 0.5
        # As the game: a vehicle run back to front shows its red lamps, one mid-train none.
        vehicle(pid, x, mid, flip, livery, back=flip, night=night, dark=0 < i < n - 1)
        z -= L + GAP
    return -z


SHOTS = [
    ("Diesel unit, 2 cars", lambda: consist([("dmu_car", False), ("dmu_car", True)], 0), B(5.5, 2.6, 7.5), B(0, 2.0, -4)),
    ("Electric unit, 4 cars", lambda: consist([("emu_cab", False), ("emu_mid", False), ("emu_mid", False), ("emu_cab", True)], 0), B(5.5, 2.7, 8.0), B(0, 2.1, -4)),
    ("Intercity, power car leading", lambda: consist([("ic_power", False), ("ic_coach", False), ("ic_coach", False), ("ic_coach", False)], 0), B(6.0, 2.5, 7.5), B(0, 1.9, -5)),
    ("Intercity, power car at the back", lambda: consist([("ic_power", False), ("ic_coach", False), ("ic_coach", False), ("ic_power", True)], 0), B(-9, 3.0, -100), B(0, 2.0, -70)),
    ("Freight loco, box, hopper, tank wagons", lambda: consist([("freight_loco", False), ("wagon_box", False), ("wagon_hopper", False), ("wagon_tank", False)], 0), B(6.0, 2.8, 7.5), B(0, 2.2, -5)),
    ("Steam tank engine, old coaches", lambda: consist([("steam_tank", False), ("coach_old", False), ("coach_old", False)], 0, livery=0), B(8, 2.6, 10), B(0, 1.8, -8)),
    ("Steam express engine and tender", lambda: consist([("steam_big", False), ("steam_tender", False), ("coach_old", False)], 0, livery=1), B(-8, 2.6, 9), B(0, 1.8, -9)),
    ("Heritage diesel, old coaches", lambda: consist([("diesel_old", False), ("coach_old", False)], 0, livery=2), B(5.5, 2.4, 7.0), B(0, 2.2, -4)),
    ("Other liveries: unit, electric, intercity, freight", None, B(22, 7, 14), B(0, 1.5, -8)),
    ("Electric unit after dark", None, B(9, 2.6, 11), B(0, 1.8, -12)),
]


def liveries():
    xs = [-12, -6, 0, 6, 12]
    rows = [([("dmu_car", False), ("dmu_car", True)], 1), ([("dmu_car", False), ("dmu_car", True)], 2),
            ([("emu_cab", False), ("emu_mid", False)], 1), ([("emu_cab", False), ("emu_mid", False)], 2),
            ([("ic_power", False), ("ic_coach", False)], 1)]
    for x, (parts, liv) in zip(xs, rows):
        track(x, -60, 8)
        consist(parts, x, livery=liv)
    track(18, -60, 8)
    consist([("freight_loco", False), ("wagon_box", False)], 18, livery=1)


def main():
    tmp = os.path.join(os.path.dirname(os.path.abspath(out)), "_train_tiles")
    os.makedirs(tmp, exist_ok=True)
    paths, names = [], []
    for i, (name, make, eye, look) in enumerate(SHOTS):
        if ONLY and str(i) not in ONLY.split(","):
            continue
        _mats.clear()
        sc, cam = _ns["scene"]()
        for o in list(sc.objects):
            if o.type == "MESH":
                bpy.data.objects.remove(o, do_unlink=True)
        sc.render.resolution_x = TILE
        sc.render.resolution_y = int(TILE * 0.62)
        grass = plain("grass", (0.07, 0.1, 0.035), 0.9)
        bpy.ops.mesh.primitive_plane_add(size=400, location=(0, 0, -0.62))
        bpy.context.active_object.data.materials.append(grass)
        night = "dark" in name
        if make:
            track(0, -110, 20)
            make()
        elif "liveries" in name:
            liveries()
        else:
            track(0, -110, 20)
            consist([("emu_cab", False), ("emu_mid", False), ("emu_mid", False), ("emu_cab", True)], 0, night=True)
        sun = [o for o in sc.objects if o.type == "LIGHT"][0]
        sun.rotation_euler = (math.radians(52), 0, math.radians(215))
        if night:
            sun.data.energy = 0.02
            sc.world.node_tree.nodes["Background"].inputs[0].default_value = (0.02, 0.03, 0.06, 1)
            sc.world.node_tree.nodes["Background"].inputs[1].default_value = 0.15
        cam.location = eye
        cam.rotation_euler = (look - eye).to_track_quat("-Z", "Y").to_euler()
        cam.data.lens = 30
        p = os.path.join(tmp, "%d.png" % i)
        sc.render.filepath = p
        bpy.ops.render.render(write_still=True)
        paths.append(p)
        names.append(name)
        print("shot", i, name, flush=True)
    W, H = TILE, int(TILE * 0.62)
    cols = 2
    rows = (len(paths) + cols - 1) // cols
    sheet = bpy.data.images.new("sheet", W * cols, H * rows)
    px = [0.0] * (W * cols * H * rows * 4)
    for i, p in enumerate(paths):
        src = list(bpy.data.images.load(p).pixels)
        cx, cy = i % cols, rows - 1 - i // cols
        for y in range(H):
            row = (cy * H + y) * W * cols + cx * W
            px[row * 4:(row + W) * 4] = src[y * W * 4:(y + 1) * W * 4]
    sheet.pixels = px
    sheet.filepath_raw = out
    sheet.file_format = "PNG"
    sheet.save()
    print("wrote", out, "|".join(names))


main()
