"""Build the grey-box shed, /Game/Shed/Shed, in the Unreal editor: where the game starts on foot.

In the editor: Tools > Execute Python Script... and pick this file, or start the editor with
-ExecutePythonScript=Tools/ue_build_shed.py. Run Tools/ue_build_level.py first (it imports the
Speed Twin and writes Data/Bike/SpeedTwin/ue_assets.json, which the shed's bike draws from, and the
Megascans/ambientCG textures this reuses). Safe to run again: it rebuilds the map from scratch.

What it makes (Unreal cm: x east, y south, z up; the floor at z 0):
  - a Dales stone workshop 12 x 8 m inside (x -600..600, y -400..400), 60 cm rubble walls, a pitched
    stone-slate roof (eaves 3.4 m, ridge 5.2 m along x) with crow-stepped gables, three tie beams;
  - a 4 m wide, 3 m high roller door in the south wall (rolled up), a side door, five small windows;
  - a concrete floor running out onto the forecourt, a lane, fields, drystone walls and far fells;
  - inside: the Speed Twin on a rear paddock stand (ATorqueShedBike) facing the door, the workbench
    along the north wall with its pegboard and vice and the parts bench (ATorquePartsBench) on its top
    (the last few parts taken off), the parts store (ATorquePartsStore, every spare by slot) north-east,
    parts shelving on the west wall, a roll cab, a tyre stack and a bin;
  - daylight: the same mid-afternoon south-west sun and sky as Buttertubs (at 1/16 of real, the camera
    exposed to match), through the door and windows; six LED battens on the beams; Lumen;
  - a point tagged RideOut in the door (stand there and press F to ride out) and a player start.
The game mode spawns the walker (ATorqueWalker) in any map with an ATorqueShedBike in it.
"""
import math
import os

import unreal

MAP = "/Game/Shed/Shed"
MAT_DIR = "/Game/Shed/Materials"

# The knobs most likely to want tuning after a look on the PC.
SUN_LUX = 4700.0            # as Buttertubs: 1/16 of a real 75,000 lux afternoon
LED_LUMENS = 1800.0         # each of six battens (a real 5 ft batten is ~6000 lm; scaled with the sun, a bit brighter)
EXPOSURE_MIN = 5.0          # EV100 floor of the auto exposure: lower lets the inside brighten more
#                             (4.0 in round 1 blew the windows out to pure white on the PC)
EXPOSURE_BIAS = 0.2         # was 0.6
HIGHLIGHTS = 0.35           # local exposure: how much contrast the bright windows and doorway keep (1 = none taken out)
EXPOSURE_MAX = 11.5

HALF_X, HALF_Y = 600.0, 400.0   # inside half sizes
WALL = 60.0
EAVES, RIDGE = 340.0, 520.0
DOOR_X0, DOOR_X1, DOOR_TOP = -150.0, 250.0, 300.0
BIKE_AT = ((DOOR_X0 + DOOR_X1) * 0.5, -20.0)

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

CUBE = unreal.load_asset("/Engine/BasicShapes/Cube")
CYLINDER = unreal.load_asset("/Engine/BasicShapes/Cylinder")
SPHERE = unreal.load_asset("/Engine/BasicShapes/Sphere")


def log(msg):
    unreal.log("[Torque] " + msg)


def try_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:
        unreal.log_warning("[Torque] couldn't set %s: %s" % (prop, e))
        return False


# --- Materials --------------------------------------------------------------------------------------

def new_material(name):
    path = MAT_DIR + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    return asset_tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())


def expr(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        try_set(e, k, v)
    return e


def finish(m):
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def flat(name, rgb, rough, metallic=0.0, emissive=0.0, grain=0.0):
    """One colour, with a little large-scale noise in it (grain) so big flat faces aren't dead flat."""
    m = new_material(name)
    col = expr(m, unreal.MaterialExpressionConstant3Vector, -600, 0, constant=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    if grain:
        n = expr(m, unreal.MaterialExpressionNoise, -800, 150, scale=0.004, output_min=1.0 - grain, output_max=1.0 + grain, levels=4)
        mul = expr(m, unreal.MaterialExpressionMultiply, -300, 0)
        mel.connect_material_expressions(col, "", mul, "A")
        mel.connect_material_expressions(n, "", mul, "B")
        col = mul
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant, -600, 300, r=rough), "", unreal.MaterialProperty.MP_ROUGHNESS)
    if metallic:
        mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant, -600, 400, r=metallic), "", unreal.MaterialProperty.MP_METALLIC)
    if emissive:
        e = expr(m, unreal.MaterialExpressionConstant3Vector, -600, 500,
                 constant=unreal.LinearColor(rgb[0] * emissive, rgb[1] * emissive, rgb[2] * emissive, 1.0))
        mel.connect_material_property(e, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish(m)


def first_texture(paths):
    for p in paths:
        if p and unreal.EditorAssetLibrary.does_asset_exist(p):
            t = unreal.load_asset(p)
            if isinstance(t, unreal.Texture):
                return t
    return None


WAT = "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture"
WAN = "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedNormal"


def world_aligned(name, maps, size_cm, tint=(1.0, 1.0, 1.0), fallback=None):
    """A scanned surface projected from the world's three axes (the basic shapes' UVs stretch over
    each face, whatever its size), tile size_cm. Falls back to a flat colour when the textures
    aren't in the project or the nodes don't connect in this engine version."""
    def plain():
        return flat(name, fallback[0], fallback[1], grain=0.12) if fallback else None
    colour = first_texture(maps.get("color", []))
    if colour is None:
        return plain()
    try:
        m = new_material(name)
        ok = [True]

        def link(a, a_out, b, b_in):
            ok[0] = bool(mel.connect_material_expressions(a, a_out, b, b_in)) and ok[0]

        def project(tex, y, fn_path):
            obj = expr(m, unreal.MaterialExpressionTextureObject, -1300, y, texture=tex)
            call = expr(m, unreal.MaterialExpressionMaterialFunctionCall, -1000, y)
            call.set_editor_property("material_function", unreal.load_asset(fn_path))
            size = expr(m, unreal.MaterialExpressionConstant3Vector, -1300, y + 120, constant=unreal.LinearColor(size_cm, size_cm, size_cm, 1.0))
            link(obj, "", call, "TextureObject")
            link(size, "", call, "TextureSize")
            return call

        c = project(colour, 0, WAT)
        t = expr(m, unreal.MaterialExpressionConstant3Vector, -1000, -150, constant=unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
        mul = expr(m, unreal.MaterialExpressionMultiply, -600, 0)
        link(c, "XYZ Texture", mul, "A")
        link(t, "", mul, "B")
        mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
        rough = first_texture(maps.get("rough", []))
        if rough is not None:
            r = project(rough, 300, WAT)
            mask = expr(m, unreal.MaterialExpressionComponentMask, -600, 300, r=True, g=False, b=False, a=False)
            link(r, "XYZ Texture", mask, "")
            mel.connect_material_property(mask, "", unreal.MaterialProperty.MP_ROUGHNESS)
        else:
            mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant, -600, 300, r=0.85), "", unreal.MaterialProperty.MP_ROUGHNESS)
        normal = first_texture(maps.get("normal", []))
        if normal is not None:
            n = project(normal, 600, WAN)
            mel.connect_material_property(n, "XYZ Texture", unreal.MaterialProperty.MP_NORMAL)
        if not ok[0]:
            raise RuntimeError("nodes didn't connect")
        return finish(m)
    except Exception as e:
        unreal.log_warning("[Torque] %s: world-aligned textures failed (%s); flat colour instead" % (name, e))
        return plain()


def megascans(folder, name, kinds=("BaseColor", "Roughness", "Normal")):
    base = "/Game/Fab/Megascans/Surfaces/%s/%s_2K_" % (folder, folder)
    return {"color": [base + kinds[0]], "rough": [base + kinds[1]], "normal": [base + kinds[2]]}


def ambientcg(asset_id):
    base = "/Game/Textures/%s/T_%s_" % (asset_id, asset_id)
    return {"color": [base + "color"], "rough": [base + "rough"], "normal": [base + "normal"]}


def materials():
    rock = megascans("Layered_Rock_vl0fdhdo", "rock")
    stone_maps = {k: ambientcg("Bricks096")[k] + rock[k] for k in rock}   # the masonry scan, else layered rock
    grass = megascans("Wet_Grass_Ground_sjzkdjba", "grass")
    tarmac = megascans("Asphalt_Road_tigkfb3o", "asphalt")
    return {
        # Gritstone/limestone rubble walls, a little warm, darker for the slate roof.
        "stone": world_aligned("M_ShedStone", stone_maps, 180.0, (0.95, 0.92, 0.86), ((0.42, 0.40, 0.36), 0.88)),
        "slate": world_aligned("M_ShedSlate", rock, 150.0, (0.45, 0.45, 0.47), ((0.16, 0.16, 0.17), 0.7)),
        "grass": world_aligned("M_ShedField", grass, 300.0, (1.0, 1.0, 1.0), ((0.17, 0.26, 0.08), 0.95)),
        "tarmac": world_aligned("M_ShedLane", tarmac, 250.0, (1.0, 1.0, 1.0), ((0.09, 0.09, 0.09), 0.8)),
        "concrete": flat("M_ShedConcrete", (0.36, 0.35, 0.33), 0.82, grain=0.18),
        "wood": flat("M_ShedTimber", (0.30, 0.20, 0.12), 0.75, grain=0.1),
        "bench": flat("M_ShedBenchTop", (0.42, 0.30, 0.18), 0.6, grain=0.1),
        "pegboard": flat("M_ShedPegboard", (0.55, 0.45, 0.32), 0.85),
        "steel": flat("M_ShedSteel", (0.55, 0.56, 0.58), 0.4, metallic=1.0),
        "galv": flat("M_ShedGalv", (0.62, 0.63, 0.64), 0.5, metallic=1.0),
        "red": flat("M_ShedRollCab", (0.45, 0.03, 0.02), 0.35),
        "black": flat("M_ShedRubber", (0.025, 0.025, 0.025), 0.9),
        "card": flat("M_ShedCardboard", (0.45, 0.32, 0.18), 0.9, grain=0.1),
        "door": flat("M_ShedDoor", (0.12, 0.22, 0.16), 0.55),          # a green-painted side door
        # The battens' opal diffusers: bright enough to read as lit, not a white block (round 1's 40 was).
        "led": flat("M_ShedLed", (0.9, 0.88, 0.84), 0.5, emissive=6.0),
        "bucket": flat("M_ShedBucket", (0.03, 0.12, 0.35), 0.45),
        "sponge": flat("M_ShedSponge", (0.75, 0.55, 0.05), 0.95),
        "hill": flat("M_ShedFell", (0.20, 0.24, 0.12), 0.95, grain=0.25),
        "drystone": world_aligned("M_ShedDrystone", rock, 120.0, (0.9, 0.9, 0.88), ((0.38, 0.37, 0.34), 0.9)),
    }


# --- Placing shapes ---------------------------------------------------------------------------------

def shape(mesh, label, centre, size, mat, rot=(0.0, 0.0, 0.0), folder="Shed", collide=True):
    """A basic shape (100 cm, centred) scaled to size cm. rot is (roll, pitch, yaw)."""
    a = actors.spawn_actor_from_object(mesh, unreal.Vector(*centre), unreal.Rotator(*rot))
    a.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    a.set_actor_label(label)
    a.set_folder_path(folder)
    c = a.get_component_by_class(unreal.StaticMeshComponent)
    c.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    if mat:
        c.set_material(0, mat)
    if not collide:
        c.set_collision_profile_name("NoCollision")
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    return a


def box(label, x0, x1, y0, y1, z0, z1, mat, **kw):
    return shape(CUBE, label, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), (x1 - x0, y1 - y0, z1 - z0), mat, **kw)


def wall(label, along, fixed0, fixed1, a0, a1, height, openings, mat):
    """A wall along x (along="x") or y between a0 and a1, fixed0..fixed1 thick, with openings
    [(start, end, sill, top)] cut out: piers between them, sills under and lintels over."""
    def piece(n, s0, s1, z0, z1):
        if s1 - s0 < 1.0 or z1 - z0 < 1.0:
            return
        if along == "x":
            box("%s_%d" % (label, n), s0, s1, fixed0, fixed1, z0, z1, mat)
        else:
            box("%s_%d" % (label, n), fixed0, fixed1, s0, s1, z0, z1, mat)
    n = 0
    at = a0
    for (s0, s1, sill, top) in sorted(openings):
        piece(n, at, s0, 0.0, height); n += 1
        piece(n, s0, s1, 0.0, sill); n += 1
        piece(n, s0, s1, top, height); n += 1
        at = s1
    piece(n, at, a1, 0.0, height)


def gable(label, x0, x1, mat):
    """A crow-stepped gable on an end wall: three courses up to the ridge, each as wide as the roof
    is at its foot, so the steps stand proud of the slates and leave no gaps for light."""
    out = HALF_Y + WALL
    steps = 3
    rise = (RIDGE - EAVES) / steps
    for k in range(steps):
        hw = out * (1.0 - k / float(steps)) + 10.0
        box("%s_%d" % (label, k), x0, x1, -hw, hw, EAVES + k * rise, EAVES + (k + 1) * rise + 12.0, mat)


def roof(mat, wood):
    out_y = HALF_Y + WALL + 35.0          # eaves overhang
    x_len = 2 * (HALF_X + WALL) + 50.0
    run, rise = out_y, RIDGE - EAVES + (35.0 * (RIDGE - EAVES) / (HALF_Y + WALL))
    slope = math.hypot(run, rise)
    pitch = math.degrees(math.atan2(rise, run))
    eave_z = RIDGE - rise
    for side, sign in (("N", -1.0), ("S", 1.0)):
        cy = sign * run / 2.0
        cz = (eave_z + RIDGE) / 2.0 + 10.0
        # Positive roll tips +y down: the south slope falls to +y, the north one rises to it.
        shape(CUBE, "Roof_" + side, (0.0, cy, cz), (x_len, slope + 12.0, 18.0), mat, rot=(sign * pitch, 0.0, 0.0), folder="Roof")
    shape(CUBE, "Ridge", (0.0, 0.0, RIDGE + 14.0), (x_len, 30.0, 14.0), mat, folder="Roof")
    # The wall heads packed up to the slates on the inside, so no strip of sky shows along the eaves.
    for side, sign in (("N", -1.0), ("S", 1.0)):
        y0, y1 = sorted((sign * (HALF_Y - 5.0), sign * (HALF_Y + 30.0)))
        box("WallHead_" + side, -HALF_X, HALF_X, y0, y1, EAVES - 1.0, EAVES + 26.0, mat, folder="Roof")
    for i, x in enumerate((-300.0, 0.0, 300.0)):
        box("TieBeam_%d" % i, x - 10, x + 10, -HALF_Y - 5, HALF_Y + 5, EAVES - 30, EAVES - 5, wood, folder="Roof")


# --- The rooms' contents ----------------------------------------------------------------------------

def workbench(m):
    x0, x1 = -520.0, -100.0
    y0, y1 = -HALF_Y, -HALF_Y + 75.0
    top = 92.0
    box("Bench_Top", x0, x1, y0, y1, top - 6, top, m["bench"], folder="Bench")
    box("Bench_Shelf", x0 + 5, x1 - 5, y0 + 5, y1 - 5, 18, 22, m["wood"], folder="Bench")
    for i, (x, y) in enumerate(((x0 + 6, y0 + 6), (x1 - 6, y0 + 6), (x0 + 6, y1 - 6), (x1 - 6, y1 - 6), ((x0 + x1) / 2, y1 - 6))):
        box("Bench_Leg_%d" % i, x - 4, x + 4, y - 4, y + 4, 0, top - 6, m["wood"], folder="Bench")
    box("Pegboard", x0 + 10, x1 - 10, -HALF_Y, -HALF_Y + 2, 105, 215, m["pegboard"], folder="Bench")
    # Tools on the pegboard: spanners, a hammer, screwdrivers (grey box).
    for i in range(10):
        x = x0 + 40 + i * 32
        box("Tool_%d" % i, x - 2, x + 2, -HALF_Y + 2, -HALF_Y + 5, 140 + (i % 3) * 18, 175 + (i % 3) * 18, m["steel"], folder="Bench", collide=False)
    box("Vice_Body", x1 - 40, x1 - 15, y1 - 20, y1 + 5, top, top + 15, m["steel"], folder="Bench")
    box("Vice_Jaw", x1 - 40, x1 - 15, y1 + 5, y1 + 12, top + 3, top + 15, m["steel"], folder="Bench")
    bench = actors.spawn_actor_from_class(unreal.load_class(None, "/Script/TorqueShedProto.TorquePartsBench"),
                                          unreal.Vector((x0 + x1) / 2 - 20, (y0 + y1) / 2, top), unreal.Rotator(0, 0, 0))
    if bench:
        bench.set_actor_label("PartsBench")
        bench.set_folder_path("Bench")
        try_set(bench, "length", x1 - x0 - 80.0)   # the vice's end left clear
    else:
        unreal.log_warning("[Torque] no TorquePartsBench class: build the C++ first")


def shelving(m, label, y0, y1):
    x0, x1 = -HALF_X, -HALF_X + 50.0
    for i, (x, y) in enumerate(((x0 + 3, y0 + 3), (x1 - 3, y0 + 3), (x0 + 3, y1 - 3), (x1 - 3, y1 - 3))):
        box("%s_Post_%d" % (label, i), x - 2.5, x + 2.5, y - 2.5, y + 2.5, 0, 200, m["galv"], folder="Shelves")
    for k, z in enumerate((15.0, 60.0, 105.0, 150.0, 195.0)):
        box("%s_Shelf_%d" % (label, k), x0, x1, y0, y1, z - 2, z, m["galv"], folder="Shelves")
        if k == 4:
            break
        # Boxes of parts, and the odd loose part, along each shelf.
        y = y0 + 6
        n = 0
        while True:
            w = 28.0 + ((k * 7 + n * 13) % 4) * 9.0
            h = 18.0 + ((k * 5 + n * 11) % 3) * 9.0
            if y + w > y1 - 6:
                break
            if (k + n) % 5 != 3:
                box("%s_Box_%d_%d" % (label, k, n), x0 + 6, x1 - 8, y, y + w, z, z + h, m["card"], folder="Shelves", collide=False)
            y += w + 5.0
            n += 1


def furniture(m):
    # The roll cab by the bench's end, a tyre stack and a bin by the door, a drip tray under the bike.
    box("RollCab", -80, 30, -HALF_Y + 2, -HALF_Y + 52, 8, 105, m["red"], folder="Kit")
    box("RollCab_Top", -78, 28, -HALF_Y + 4, -HALF_Y + 48, 105, 150, m["red"], folder="Kit")
    for i in range(4):
        shape(CYLINDER, "RollCab_Wheel_%d" % i, (-70 + (i % 2) * 90, -HALF_Y + 10 + (i // 2) * 34, 4), (8, 8, 4), m["black"],
              rot=(90.0, 0.0, 0.0), folder="Kit", collide=False)
    for i in range(4):
        shape(CYLINDER, "Tyre_%d" % i, (-HALF_X + 50, 100.0, 9 + i * 18), (62, 62, 17), m["black"], folder="Kit")
    shape(CYLINDER, "Bin", (DOOR_X0 - 70, HALF_Y - 40, 45), (55, 55, 90), m["black"], folder="Kit")
    box("DripTray", BIKE_AT[0] - 30, BIKE_AT[0] + 30, BIKE_AT[1] - 20, BIKE_AT[1] + 20, 0, 1.5, m["black"], folder="Kit", collide=False)
    # A wash bucket and sponge behind the bike (hold G / Square on the bike to wash it).
    shape(CYLINDER, "Bucket", (BIKE_AT[0] + 80, BIKE_AT[1] - 130, 14), (30, 30, 28), m["bucket"], folder="Kit")
    box("Sponge", BIKE_AT[0] + 72, BIKE_AT[0] + 88, BIKE_AT[1] - 136, BIKE_AT[1] - 124, 28, 34, m["sponge"], folder="Kit", collide=False)
    # The paint rack on the east wall between the window and the side door (ATorquePaintRack: its
    # colour cards; E picks one, E on bodywork paints it), facing into the room.
    rack_cls = unreal.load_class(None, "/Script/TorqueShedProto.TorquePaintRack")
    if rack_cls:
        r = actors.spawn_actor_from_class(rack_cls, unreal.Vector(HALF_X - 2.0, 10.0, 150.0), unreal.Rotator(0.0, 0.0, 180.0))
        r.set_actor_label("PaintRack")
        r.set_folder_path("Kit")
    else:
        unreal.log_warning("[Torque] no TorquePaintRack class: build the C++ first")
    # The parts store along the north wall east of the window (ATorquePartsStore: a labelled bay for
    # every slot with that slot's spares; E fits one). Its foot's middle against the wall, facing in.
    store_cls = unreal.load_class(None, "/Script/TorqueShedProto.TorquePartsStore")
    if store_cls:
        st = actors.spawn_actor_from_class(store_cls, unreal.Vector(440.0, -HALF_Y + 23.0, 0.0), unreal.Rotator(0.0, 0.0, 90.0))
        st.set_actor_label("PartsStore")
        st.set_folder_path("Kit")
    else:
        unreal.log_warning("[Torque] no TorquePartsStore class: build the C++ first")
    # The side door, shut, in the east wall.
    box("SideDoor", HALF_X + 20, HALF_X + 26, 100, 190, 0, 205, m["door"], folder="Walls")


def leds(m):
    for i, x in enumerate((-300.0, 0.0, 300.0)):
        for j, y in enumerate((-170.0, 170.0)):
            # A grey steel batten body hung on two chains from the beam, its diffuser underneath.
            box("Batten_%d_%d" % (i, j), x - 6, x + 6, y - 76, y + 76, EAVES - 40, EAVES - 34, m["steel"], folder="Lights", collide=False)
            box("Batten_%d_%d_Diffuser" % (i, j), x - 4, x + 4, y - 73, y + 73, EAVES - 42, EAVES - 40, m["led"], folder="Lights", collide=False)
            for k, cy in enumerate((y - 60, y + 60)):
                box("Batten_%d_%d_Chain%d" % (i, j, k), x - 0.6, x + 0.6, cy - 0.6, cy + 0.6, EAVES - 34, EAVES - 30, m["galv"], folder="Lights", collide=False)
            light = actors.spawn_actor_from_class(unreal.RectLight, unreal.Vector(x, y, EAVES - 43), unreal.Rotator(0.0, -90.0, 0.0))
            light.set_actor_label("LED_%d_%d" % (i, j))
            light.set_folder_path("Lights")
            lc = light.get_component_by_class(unreal.RectLightComponent)
            lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
            units = getattr(unreal, "LightUnits", None)
            if units is not None:
                try_set(lc, "intensity_units", units.LUMENS)
            for k, v in (("intensity", LED_LUMENS), ("source_width", 150.0), ("source_height", 12.0),
                         ("attenuation_radius", 1400.0), ("barn_door_angle", 88.0),
                         ("temperature", 5000.0), ("use_temperature", True)):
                try_set(lc, k, v)


def outside(m):
    # Concrete apron in front of the door, the lane off it to the south, the field all round.
    box("Forecourt", -450, 650, HALF_Y + WALL, HALF_Y + WALL + 900, -10, 0, m["concrete"], folder="Outside")
    box("Lane", -150, 250, HALF_Y + WALL + 900, 30000, -12, -2, m["tarmac"], folder="Outside")
    box("Field", -40000, 40000, -40000, 40000, -30, -8, m["grass"], folder="Outside")
    # Drystone walls round the yard and along the lane.
    def dry(label, x0, x1, y0, y1):
        box(label, x0, x1, y0, y1, -8, 130, m["drystone"], folder="Outside")
    dry("Wall_YardW", -1100, -1040, -900, HALF_Y + WALL + 900)
    dry("Wall_YardE", 1240, 1300, -900, HALF_Y + WALL + 900)
    dry("Wall_Back", -1100, 1300, -960, -900)
    dry("Wall_LaneW", -230, -170, HALF_Y + WALL + 900, 12000)
    dry("Wall_LaneE", 270, 330, HALF_Y + WALL + 900, 12000)
    dry("Wall_Field1", -9000, -230, 4000, 4060)
    dry("Wall_Field2", 330, 9000, 6500, 6560)
    # Far fells: low domes 8 to 20 km off, hazed blue by the sky's aerial perspective.
    for i, (x, y, w, d, h) in enumerate(((-14000, 9000, 26000, 9000, 2600), (9000, 15000, 30000, 12000, 3400),
                                         (20000, -6000, 14000, 30000, 2400), (-6000, -16000, 34000, 10000, 3000),
                                         (-20000, -2000, 12000, 26000, 2200))):
        shape(SPHERE, "Fell_%d" % i, (x, y, -h * 0.25), (w, d, h * 2.0), m["hill"], folder="Outside", collide=False)


def sky():
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 50000), unreal.Rotator(0.0, -30.0, -45.0))
    sun.set_actor_label("Sun")
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    for k, v in (("atmosphere_sun_light", True), ("intensity", SUN_LUX), ("light_source_angle", 0.53),
                 ("light_color", unreal.Color(255, 247, 235, 255))):
        try_set(sc, k, v)
    atm = actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, -100), unreal.Rotator(0, 0, 0))
    ac = atm.get_component_by_class(unreal.SkyAtmosphereComponent)
    for k, v in (("mie_scattering_scale", 0.004), ("rayleigh_scattering_scale", 0.045),
                 ("aerial_pespective_view_distance_scale", 0.6)):   # sic: the engine spells it so
        try_set(ac, k, v)
    skl = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 2000), unreal.Rotator(0, 0, 0))
    skc = skl.get_component_by_class(unreal.SkyLightComponent)
    skc.set_editor_property("real_time_capture", True)
    skc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    fog = actors.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, -500), unreal.Rotator(0, 0, 0))
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    for k, v in (("fog_density", 0.0018), ("fog_height_falloff", 0.15), ("start_distance", 1500.0),
                 ("fog_inscattering_luminance", unreal.LinearColor(0.45, 0.46, 0.47, 1.0))):
        try_set(fc, k, v)
    try:
        actors.spawn_actor_from_class(unreal.VolumetricCloud, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    except Exception as e:
        unreal.log_warning("[Torque] no volumetric cloud: %s" % e)

    ppv = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    ppv.set_editor_property("unbound", True)
    pp = ppv.get_editor_property("settings")

    def enum(path):
        try:
            v = unreal
            for part in path.split("."):
                v = getattr(v, part)
            return v
        except AttributeError:
            unreal.log_warning("[Torque] no " + path)
            return None
    # The eye opening up indoors and stopping down at the door: a wide auto exposure range, slower
    # to adapt than on the bike, and local exposure pulling the bright windows and doorway down so
    # the fields outside still read instead of clipping white.
    for k, v in (("auto_exposure_method", enum("AutoExposureMethod.AEM_HISTOGRAM")),
                 ("auto_exposure_min_brightness", EXPOSURE_MIN), ("auto_exposure_max_brightness", EXPOSURE_MAX),
                 ("auto_exposure_bias", EXPOSURE_BIAS), ("auto_exposure_speed_up", 1.5), ("auto_exposure_speed_down", 1.0),
                 ("local_exposure_highlight_contrast_scale", HIGHLIGHTS), ("local_exposure_shadow_contrast_scale", 0.7),
                 ("auto_exposure_low_percent", 70.0), ("auto_exposure_high_percent", 97.0),   # the windows count, so it stops down a little for them
                 ("film_shoulder", 0.4),   # a gentler roll-off into white than the default 0.26
                 ("bloom_intensity", 0.2), ("vignette_intensity", 0.25), ("film_grain_intensity", 0.06),
                 ("white_temp", 6300.0), ("color_saturation", unreal.Vector4(1.08, 1.08, 1.08, 1.0)),
                 ("lumen_final_gather_quality", 2.0), ("lumen_scene_lighting_quality", 2.0),
                 ("lumen_scene_detail", 2.0), ("lumen_reflection_quality", 2.0),
                 ("lumen_ray_lighting_mode", enum("LumenRayLightingModeOverride.HIT_LIGHTING_FOR_REFLECTIONS"))):
        if v is not None and try_set(pp, k, v):
            try_set(pp, "override_" + k, True)
    ppv.set_editor_property("settings", pp)


# --- Replacing a map in place (shared with ue_build_level.py / ue_build_shed.py; keep the copies alike) ---
# Build into a blank unsaved world and save it over the map's path. Deleting the old map first doesn't
# work when it was the editor's start-up map: delete_asset says yes but the .umap stays on disk, and
# new_level(MAP) then refuses ("An asset already exists at this location").

def umap_file(map_path):
    return os.path.abspath(os.path.join(unreal.Paths.project_content_dir(), map_path[len("/Game/"):] + ".umap"))


def begin_fresh_level(map_path):
    """An empty unsaved world to build map_path in; returns the .umap's old modified time (or None)."""
    f = umap_file(map_path)
    before = os.path.getmtime(f) if os.path.exists(f) else None
    if not unreal.EditorLoadingAndSavingUtils.new_blank_map(False):
        raise RuntimeError("couldn't open a blank map to build %s in" % map_path)
    unreal.SystemLibrary.collect_garbage()   # let the old map's world go before saving over it
    log("building %s in a blank world (old .umap %s)" % (map_path, "from %.0f" % before if before else "absent"))
    return before


def save_fresh_level(map_path, before):
    """Save the world being built over map_path, and check the .umap really was written."""
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not world:
        raise RuntimeError("no editor world to save as %s" % map_path)
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
        raise RuntimeError("couldn't save the level as %s" % map_path)
    f = umap_file(map_path)
    if not os.path.exists(f):
        raise RuntimeError("%s isn't on disk after saving (%s)" % (map_path, f))
    after = os.path.getmtime(f)
    if before is not None and after <= before:
        raise RuntimeError("%s wasn't rewritten (%s still from %.0f)" % (map_path, f, before))
    log("saved %s (%s)" % (map_path, f))


def build():
    m = materials()
    before = begin_fresh_level(MAP)

    # Floor: the slab runs under the walls and out of the door.
    box("Floor", -HALF_X - WALL, HALF_X + WALL, -HALF_Y - WALL, HALF_Y + WALL, -12, 0, m["concrete"], folder="Walls")
    # Walls: south with the roller door and a window, north with two windows, west with one, east with
    # the side door and one. Ends run full width; the long walls sit between them.
    s = m["stone"]
    wall("Wall_S", "x", HALF_Y, HALF_Y + WALL, -HALF_X, HALF_X, EAVES,
         [(DOOR_X0, DOOR_X1, 0.0, DOOR_TOP), (-480.0, -370.0, 110.0, 215.0), (380.0, 480.0, 110.0, 215.0)], s)
    wall("Wall_N", "x", -HALF_Y - WALL, -HALF_Y, -HALF_X, HALF_X, EAVES,
         [(-380.0, -270.0, 120.0, 220.0), (150.0, 260.0, 120.0, 220.0)], s)
    wall("Wall_W", "y", -HALF_X - WALL, -HALF_X, -HALF_Y - WALL, HALF_Y + WALL, EAVES, [(-60.0, 60.0, 120.0, 220.0)], s)
    wall("Wall_E", "y", HALF_X, HALF_X + WALL, -HALF_Y - WALL, HALF_Y + WALL, EAVES,
         [(100.0, 190.0, 0.0, 210.0), (-250.0, -150.0, 120.0, 220.0)], s)
    gable("Gable_W", -HALF_X - WALL, -HALF_X, s)
    gable("Gable_E", HALF_X, HALF_X + WALL, s)
    roof(m["slate"], m["wood"])
    # The roller door, rolled up: its drum in a box over the opening, and the guide rails.
    shape(CYLINDER, "RollerDoor_Drum", ((DOOR_X0 + DOOR_X1) / 2, HALF_Y - 28, DOOR_TOP + 22), (44, 44, DOOR_X1 - DOOR_X0 + 20),
          m["galv"], rot=(0.0, 90.0, 0.0), folder="Door", collide=False)
    for i, x in enumerate((DOOR_X0 - 6, DOOR_X1 + 6)):
        box("RollerDoor_Rail_%d" % i, x - 4, x + 4, HALF_Y - 8, HALF_Y, 0, DOOR_TOP + 10, m["galv"], folder="Door")
    ride = actors.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector((DOOR_X0 + DOOR_X1) / 2, HALF_Y + 30, 0), unreal.Rotator(0, 0, 0))
    ride.set_actor_label("RideOut")
    ride.set_folder_path("Door")
    ride.set_editor_property("tags", [unreal.Name("RideOut")])

    workbench(m)
    shelving(m, "ShelfA", -350.0, -150.0)
    shelving(m, "ShelfB", 160.0, 360.0)
    furniture(m)
    leds(m)
    outside(m)
    sky()

    # The bike, nose to the door.
    bike_cls = unreal.load_class(None, "/Script/TorqueShedProto.TorqueShedBike")
    if bike_cls:
        b = actors.spawn_actor_from_class(bike_cls, unreal.Vector(BIKE_AT[0], BIKE_AT[1], 0.0), unreal.Rotator(0.0, 0.0, 90.0))
        b.set_actor_label("SpeedTwin")
    else:
        unreal.log_warning("[Torque] no TorqueShedBike class: build the C++ first")

    # Start by the door looking in at the bike.
    start = (DOOR_X1 + 60.0, HALF_Y - 90.0)
    yaw = math.degrees(math.atan2(BIKE_AT[1] - start[1], BIKE_AT[0] - start[0]))
    # Where the start menu looks from (ATorqueMenuPawn): inside the door at eye height, looking in at
    # the bike, below the battens and beams.
    view = (DOOR_X1 + 80.0, HALF_Y - 60.0, 165.0)
    to = (BIKE_AT[0] - view[0], BIKE_AT[1] - view[1], 70.0 - view[2])
    v = actors.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(*view), unreal.Rotator(
        0.0, math.degrees(math.atan2(to[2], math.hypot(to[0], to[1]))), math.degrees(math.atan2(to[1], to[0]))))
    v.set_actor_label("MenuView")
    v.set_editor_property("tags", [unreal.Name("MenuView")])
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(start[0], start[1], 100.0), unreal.Rotator(0.0, 0.0, yaw))

    save_fresh_level(MAP, before)
    # Say what the saved level holds for the game's own lookups (tags), so a run's log shows it.
    for a in actors.get_all_level_actors():
        tags = [str(t) for t in a.get_editor_property("tags")]
        if "MenuView" in tags or "RideOut" in tags:
            log("level has %s (%s) at %s, rotation %s" % (a.get_actor_label(), ",".join(tags), a.get_actor_location(), a.get_actor_rotation()))
    log("built %s: walk round the bike, E to swap a part, F at the door to ride out" % MAP)


build()
