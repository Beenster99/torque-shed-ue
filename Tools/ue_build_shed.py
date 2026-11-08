"""Build the shed, /Game/Shed/Shed, in the Unreal editor: where the game starts on foot.

A port of Torque Shed's shed (the Godot game's scripts/workshop.gd, with shed_state.gd's fixed
machines): the same rooms, the same places for everything, so it plays as it did. Godot metres
(x east, z towards the roller door, the shed's middle at z 65) become Unreal cm here as
x = 100 gx, y = 100 (gz - 65), z = 100 gy.

In the editor: Tools > Execute Python Script... and pick this file, or start the editor with
-ExecutePythonScript=Tools/ue_build_shed.py. Run Tools/ue_build_level.py first (it imports the
Speed Twin and writes Data/Bike/SpeedTwin/ue_assets.json, which the shed's bikes draw from, and the
Megascans/ambientCG textures this reuses). Safe to run again: it rebuilds the map from scratch.

What it makes (Unreal cm: x east, y south (towards the roller door), z up; the floor at z 0):
  - the shed: 13 x 11 m inside (x -650..650, y -550..550), 4.5 m to the roof, steel-clad walls
    (plaster above a dark band inside), a flat roof on five steel trusses, an epoxy floor;
  - a 4.6 m wide, 3.2 m high roller door in the front wall (+y, rolled up) with yellow posts and
    the TORQUE SHED sign over it; the light switches to its right;
  - four bike bays along the back wall (ATorqueShedBike, Bay 0-3, x -480/-160/160/480), noses to
    the door, each in a painted box with BAY n in front and its screen on the wall above; a yellow
    walkway line in front of the bays;
  - the workbench down the west wall (parts go down anywhere on it) with the radio shelf over its
    end and the crew board above it; the tyre changer past its end;
  - the parts computer on the east wall, the engine stand by the bays, the parts washer and the
    repair bench against the front wall, the three bins left of the roller door;
  - doorways to the storage room (west), the paint shop (east) and the dyno room (back wall), each
    room built as an empty shell for now, and the wash bay lean-to on the west wall, open to the
    yard, with the pressure washer on its back wall;
  - the deliveries bay painted on the yard east of the door; fields, lane and fells round it;
  - daylight as Buttertubs; the shed's strip lights over each bay and down the middle; Lumen;
  - a point tagged RideOut in the door and a player start where Torque Shed puts walkers.
The game mode spawns the walker (ATorqueWalker) in any map with an ATorqueShedBike in it.
"""
import math
import os

import unreal

MAP = "/Game/Shed/Shed"
MAT_DIR = "/Game/Shed/Materials"

# The knobs most likely to want tuning after a look on the PC.
SUN_LUX = 4700.0            # as Buttertubs: 1/16 of a real 75,000 lux afternoon
LED_LUMENS = 1800.0         # each strip light (a real 5 ft batten is ~6000 lm; scaled with the sun, a bit brighter)
EXPOSURE_MIN = 5.0          # EV100 floor of the auto exposure: lower lets the inside brighten more
#                             (4.0 in round 1 blew the windows out to pure white on the PC)
EXPOSURE_BIAS = 0.2         # was 0.6
HIGHLIGHTS = 0.35           # local exposure: how much contrast the bright windows and doorway keep (1 = none taken out)
EXPOSURE_MAX = 11.5

# The shed (workshop.gd): interior half sizes, height, wall thickness.
HALF_X, HALF_Y = 650.0, 550.0
HEIGHT = 450.0
WALL = 30.0
EAVES = HEIGHT                       # (the lights hang from the trusses at this height)
DOOR_HALF, DOOR_TOP = 230.0, 320.0   # the roller door, in the middle of the front (+y) wall
DOOR_X0, DOOR_X1 = -DOOR_HALF, DOOR_HALF
DYNO_DOOR_HALF, DYNO_DOOR_TOP = 85.0, 250.0         # through the back wall to the dyno room
SIDE_DOOR_HALF, SIDE_DOOR_TOP = 65.0, 240.0          # through the side walls
STORE_DOOR_Y, PAINT_DOOR_Y = -450.0, 350.0           # west to the storage room, east to the paint shop
ROOM_HEIGHT = 340.0
# The four bays (Workshop.bay_transform): along the back wall, 3.2 m apart, noses to the door.
BAY_X = (-480.0, -160.0, 160.0, 480.0)
BAY_Y = -350.0
BAY_SCREEN_X = (-504.0, -232.0, 232.0, 504.0)        # their screens on the back wall, clear of the dyno doorway
BIKE_AT = (0.0, BAY_Y)               # what the menu view and the player start look at
# Workbench spots bench_0..3 run down the west wall at y -120, 0, 120, 240; the bench 0.6 m past each end.
BENCH_X, BENCH_Y0, BENCH_Y1, BENCH_TOP = -600.0, -180.0, 300.0, 95.0

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


def highlight_material():
    """M_ShedHighlight, for the game (C++) to lay over what you look at and to draw where a held part
    goes down: Torque Shed's soft orange glow (bike_model.gd highlight) and white footprint. Unlit
    and see-through; Color and Opacity are parameters."""
    m = new_material("M_ShedHighlight")
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    col = expr(m, unreal.MaterialExpressionVectorParameter, -600, 0, parameter_name="Color",
               default_value=unreal.LinearColor(1.0, 0.6, 0.15, 1.0))
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    op = expr(m, unreal.MaterialExpressionScalarParameter, -600, 200, parameter_name="Opacity", default_value=0.3)
    mel.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    return finish(m)


def screen_page_material():
    """M_ShedScreenPage: a screen showing a page the game draws (C++ canvas render targets: the bay
    screens, Torque Shed's TVs). Unlit, the page's texture as its glow (parameter Page), Brightness."""
    m = new_material("M_ShedScreenPage")
    try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    tex = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0, parameter_name="Page")
    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    if white:
        try_set(tex, "texture", white)
    bright = expr(m, unreal.MaterialExpressionScalarParameter, -700, 250, parameter_name="Brightness", default_value=1.6)
    mul = expr(m, unreal.MaterialExpressionMultiply, -300, 0)
    mel.connect_material_expressions(tex, "RGB", mul, "A")
    mel.connect_material_expressions(bright, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
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
    grass = megascans("Wet_Grass_Ground_sjzkdjba", "grass")
    tarmac = megascans("Asphalt_Road_tigkfb3o", "asphalt")
    return {
        # Torque Shed's shed (workshop.gd _make_materials): steel cladding, plaster over a dark
        # band inside, an epoxy floor, steel trusses, timber bench tops, hazard yellow.
        "cladding": flat("M_ShedCladding", (0.30, 0.33, 0.36), 0.45, metallic=0.7, grain=0.06),
        "roof": flat("M_ShedRoof", (0.25, 0.27, 0.29), 0.5, metallic=0.6),
        "floor": flat("M_ShedEpoxy", (0.36, 0.37, 0.38), 0.3, grain=0.06),
        "wall_in": flat("M_ShedPlaster", (0.78, 0.76, 0.72), 0.85, grain=0.06),
        "wall_low": flat("M_ShedWallBand", (0.20, 0.24, 0.27), 0.6),
        "steel": flat("M_ShedSteel", (0.30, 0.32, 0.35), 0.35, metallic=0.85),
        "stand": flat("M_ShedStandOrange", (0.85, 0.35, 0.05), 0.4, metallic=0.5),
        "wood": flat("M_ShedTimber", (0.50, 0.36, 0.22), 0.7, grain=0.08),
        "yellow": flat("M_ShedYellow", (0.95, 0.75, 0.05), 0.5),
        "dark": flat("M_ShedDark", (0.06, 0.06, 0.07), 0.5, metallic=0.3),
        "blue": flat("M_ShedBlue", (0.10, 0.30, 0.65), 0.35, metallic=0.6),
        "red": flat("M_ShedRed", (0.70, 0.08, 0.06), 0.35, metallic=0.5),
        "green": flat("M_ShedGreen", (0.20, 0.55, 0.30), 0.45, metallic=0.1),
        "pad": flat("M_ShedWashPad", (0.42, 0.44, 0.45), 0.25, grain=0.1),
        "screen": flat("M_ShedScreen", (0.10, 0.30, 0.50), 0.3, emissive=1.4),
        "highlight": highlight_material(),     # for the game: what's looked at, where a part goes down
        "screen_page": screen_page_material(),   # for the game: the screens' pages
        "led": flat("M_ShedLed", (0.9, 0.88, 0.84), 0.5, emissive=6.0),
        # Outside: the yard, the lane and the Dales round it.
        "concrete": flat("M_ShedConcrete", (0.36, 0.35, 0.33), 0.82, grain=0.18),
        "grass": world_aligned("M_ShedField", grass, 300.0, (1.0, 1.0, 1.0), ((0.17, 0.26, 0.08), 0.95)),
        "tarmac": world_aligned("M_ShedLane", tarmac, 250.0, (1.0, 1.0, 1.0), ((0.09, 0.09, 0.09), 0.8)),
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


def room(label, x0, x1, y0, y1, m, open_sides=()):
    """An empty room off the shed (for now): floor, walls on the sides not in open_sides
    ("n", "s", "w", "e"; the shed's own wall closes one), and a roof."""
    h = ROOM_HEIGHT
    box(label + "_Floor", x0, x1, y0, y1, -12, 0, m["floor"], folder=label)
    if "n" not in open_sides:
        box(label + "_WallN", x0 - WALL, x1 + WALL, y0 - WALL, y0, 0, h, m["cladding"], folder=label)
    if "s" not in open_sides:
        box(label + "_WallS", x0 - WALL, x1 + WALL, y1, y1 + WALL, 0, h, m["cladding"], folder=label)
    if "w" not in open_sides:
        box(label + "_WallW", x0 - WALL, x0, y0, y1, 0, h, m["cladding"], folder=label)
    if "e" not in open_sides:
        box(label + "_WallE", x1, x1 + WALL, y0, y1, 0, h, m["cladding"], folder=label)
    box(label + "_Roof", x0 - WALL, x1 + WALL, y0 - WALL, y1 + WALL, h, h + 20, m["roof"], folder=label)


def label_text(name, text, at, yaw, size, colour, folder, pitch=0.0):
    """Words on a wall or the floor (a TextRenderActor: it faces +x, turned by yaw; pitch 90 lays
    it flat). Decoration only: never stops the build."""
    try:
        t = actors.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(*at), unreal.Rotator(0.0, pitch, yaw))
        t.set_actor_label(name)
        t.set_folder_path(folder)
        tr = t.get_editor_property("text_render")
        tr.set_editor_property("text", text)
        tr.set_editor_property("world_size", size)
        tr.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
        tr.set_editor_property("vertical_alignment", unreal.VerticalTextAligment.EVRTA_TEXT_CENTER)
        tr.set_editor_property("text_render_color", unreal.Color(r=colour[0], g=colour[1], b=colour[2], a=255))
    except Exception as e:
        unreal.log_warning("[Torque] label %s: %s" % (name, e))


# --- The shed (workshop.gd _build_shell) ------------------------------------------------------------

def shell(m):
    c, w = m["cladding"], WALL
    hx, hy, h = HALF_X, HALF_Y, HEIGHT
    # Floor: the epoxy slab, running under the walls (the yard is outside()).
    box("Floor", -hx - w, hx + w, -hy - w, hy + w, -12, 0, m["floor"], folder="Walls")
    # Back wall, with the doorway to the dyno room; side walls with theirs; the front either side
    # of the roller door and over it.
    wall("Wall_Back", "x", -hy - w, -hy, -hx - w, hx + w, h, [(-DYNO_DOOR_HALF, DYNO_DOOR_HALF, 0.0, DYNO_DOOR_TOP)], c)
    wall("Wall_W", "y", -hx - w, -hx, -hy, hy, h, [(STORE_DOOR_Y - SIDE_DOOR_HALF, STORE_DOOR_Y + SIDE_DOOR_HALF, 0.0, SIDE_DOOR_TOP)], c)
    wall("Wall_E", "y", hx, hx + w, -hy, hy, h, [(PAINT_DOOR_Y - SIDE_DOOR_HALF, PAINT_DOOR_Y + SIDE_DOOR_HALF, 0.0, SIDE_DOOR_TOP)], c)
    wall("Wall_Front", "x", hy, hy + w, -hx - w, hx + w, h, [(DOOR_X0, DOOR_X1, 0.0, DOOR_TOP)], c)
    # Inside faces: painted plaster above a dark band 1.2 m high, 2 cm proud of the cladding.
    def lining(label, along, face, inward, a0, a1, gaps):
        f0, f1 = sorted((face, face + inward * 2.0))
        def panel(name, s0, s1, z0, z1, mat):
            if s1 - s0 < 1.0 or z1 - z0 < 1.0:
                return
            if along == "x":
                box(name, s0, s1, f0, f1, z0, z1, mat, folder="Walls", collide=False)
            else:
                box(name, f0, f1, s0, s1, z0, z1, mat, folder="Walls", collide=False)
        at = a0
        for n, (g0, g1, top) in enumerate(sorted(gaps) + [(a1, a1, h)]):
            panel("%s_%d_Low" % (label, n), at, g0, 0.0, 120.0, m["wall_low"])
            panel("%s_%d_Up" % (label, n), at, g0, 120.0, h, m["wall_in"])
            panel("%s_%d_Over" % (label, n), g0, g1, top, h, m["wall_in"])
            at = g1
    lining("Lining_Back", "x", -hy, 1.0, -hx, hx, [(-DYNO_DOOR_HALF, DYNO_DOOR_HALF, DYNO_DOOR_TOP)])
    lining("Lining_Front", "x", hy, -1.0, -hx, hx, [(DOOR_X0, DOOR_X1, DOOR_TOP)])
    lining("Lining_W", "y", -hx, 1.0, -hy, hy, [(STORE_DOOR_Y - SIDE_DOOR_HALF, STORE_DOOR_Y + SIDE_DOOR_HALF, SIDE_DOOR_TOP)])
    lining("Lining_E", "y", hx, -1.0, -hy, hy, [(PAINT_DOOR_Y - SIDE_DOOR_HALF, PAINT_DOOR_Y + SIDE_DOOR_HALF, SIDE_DOOR_TOP)])
    # Hazard-yellow frames round the side doorways and posts either side of the roller door.
    for name, x, y in (("Storage", -hx - w / 2, STORE_DOOR_Y), ("Paint", hx + w / 2, PAINT_DOOR_Y)):
        for end in (-1.0, 1.0):
            yy = y + end * (SIDE_DOOR_HALF + 4.5)
            box("DoorFrame_%s_%d" % (name, end > 0), x - w / 2 - 3, x + w / 2 + 3, yy - 5.5, yy + 5.5, 0, SIDE_DOOR_TOP, m["yellow"], folder="Walls", collide=False)
        box("DoorFrame_%s_Top" % name, x - w / 2 - 3, x + w / 2 + 3, y - SIDE_DOOR_HALF - 10, y + SIDE_DOOR_HALF + 10,
            SIDE_DOOR_TOP, SIDE_DOOR_TOP + 11, m["yellow"], folder="Walls", collide=False)
        sx = -hx + 3 if x < 0 else hx - 3
        label_text("DoorSign_" + name, "STORAGE" if x < 0 else "PAINT SHOP", (sx, y, SIDE_DOOR_TOP + 35), 0.0 if x < 0 else 180.0,
                   22.0, (20, 20, 20), "Walls")
    for side in (-1.0, 1.0):
        px = side * (DOOR_HALF + 7.5)
        box("DoorPost_%d" % (side > 0), px - 8.5, px + 8.5, hy - 2, hy + w + 6, 0, DOOR_TOP, m["yellow"], folder="Door", collide=False)
    # Roof: a flat steel roof on five trusses.
    box("Roof", -hx - 50, hx + 50, -hy - 50, hy + 50, h, h + 30, m["roof"], folder="Roof")
    for i in range(5):
        y = -hy + 80 + i * (2 * hy - 160) / 4.0
        box("Truss_%d" % i, -hx, hx, y - 5, y + 5, h - 29, h - 11, m["steel"], folder="Roof", collide=False)
    # The roller door rolled up into its drum, a dark bar under it, the sign outside.
    shape(CYLINDER, "RollerDoor_Drum", (0.0, hy - 38, DOOR_TOP + 34), (60, 60, DOOR_HALF * 2 + 30), m["cladding"],
          rot=(0.0, 90.0, 0.0), folder="Door", collide=False)
    box("RollerDoor_Bar", DOOR_X0, DOOR_X1, hy - 13, hy - 3, DOOR_TOP - 2, DOOR_TOP + 6, m["dark"], folder="Door", collide=False)
    label_text("FrontSign", "TORQUE SHED", (0.0, hy + w + 6, DOOR_TOP + 60), 90.0, 70.0, (255, 216, 89), "Door")
    # Painted bay boxes, BAY n in front of each, and the walkway line in front of the bays.
    for i, x in enumerate(BAY_X):
        for side in (-1.0, 1.0):
            lx = x + side * 150.0
            box("BayLine_%d_%d" % (i, side > 0), lx - 3, lx + 3, BAY_Y + 40 - 155, BAY_Y + 40 + 155, 0, 0.4, m["yellow"], folder="Bays", collide=False)
        label_text("BayNumber_%d" % (i + 1), "BAY %d" % (i + 1), (x, BAY_Y + 175, 1.2), 90.0, 40.0, (242, 204, 51), "Bays", pitch=90.0)
    box("Walkway", -hx + 150, hx - 150, -143, -137, 0, 0.4, m["yellow"], folder="Bays", collide=False)


def bays(m):
    # Each bay's bike on its paddock stand (the stand is the bike actor's own), and its screen on
    # the back wall above it (grey box until the screens show the bike's details).
    bike_cls = unreal.load_class(None, "/Script/TorqueShedProto.TorqueShedBike")
    if not bike_cls:
        unreal.log_warning("[Torque] no TorqueShedBike class: build the C++ first")
    for i, x in enumerate(BAY_X):
        if bike_cls:
            b = actors.spawn_actor_from_class(bike_cls, unreal.Vector(x, BAY_Y, 0.0), unreal.Rotator(0.0, 0.0, 90.0))
            b.set_actor_label("Bike_Bay%d" % (i + 1))
            b.set_folder_path("Bays")
            try_set(b, "bay", i)
        sx = BAY_SCREEN_X[i]
        box("BayScreen_%d" % (i + 1), sx - 130, sx + 130, -HALF_Y, -HALF_Y + 6, 245 - 73, 245 + 73, m["dark"], folder="Bays", collide=False)
        box("BayScreen_%d_Glass" % (i + 1), sx - 124, sx + 124, -HALF_Y + 6, -HALF_Y + 7, 245 - 67, 245 + 67, m["screen"], folder="Bays", collide=False)
    # Every part that isn't on a bike (on the benches and machines, on the floor, boxed in the
    # deliveries bay, in someone's hands) is drawn by one ATorqueShedItems (the walker makes one if
    # it's missing).
    items_cls = unreal.load_class(None, "/Script/TorqueShedProto.TorqueShedItems")
    if items_cls:
        it = actors.spawn_actor_from_class(items_cls, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
        it.set_actor_label("ShedItems")
        it.set_folder_path("Bays")
    else:
        unreal.log_warning("[Torque] no TorqueShedItems class: build the C++ first")


# --- What's in it (workshop.gd _build_bench, _build_computer, _build_bin, _build_radio; the fixed
# machines from shed_state.gd start_new) ---------------------------------------------------------------

def workbench(m):
    x0, x1 = BENCH_X - 47.5, BENCH_X + 47.5
    y0, y1 = BENCH_Y0, BENCH_Y1
    box("Bench_Body", BENCH_X - 45, BENCH_X + 45, y0, y1, 0, 94.5, m["steel"], folder="Bench")
    box("Bench_Top", x0, x1, y0 - 2.5, y1 + 2.5, 89, BENCH_TOP, m["wood"], folder="Bench")
    for i in range(3):   # red drawer fronts along its face
        box("Bench_Drawers_%d" % i, BENCH_X + 45, BENCH_X + 47, y0 + 10, y1 - 10, 15 + i * 25, 35 + i * 25, m["red"], folder="Bench", collide=False)
    label_text("Bench_Poster", "WORKBENCH", (-HALF_X + 2, BENCH_Y1 - 40, 132), 0.0, 14.0, (30, 30, 30), "Bench")
    # The radio on its shelf over the bench's back end, and the crew board on the wall above.
    ry = BENCH_Y0 + 60 - 25
    box("Radio_Shelf", -HALF_X, -HALF_X + 34, ry - 30, ry + 30, 158.5, 161.5, m["wood"], folder="Bench", collide=False)
    box("Radio", -HALF_X + 6, -HALF_X + 30, ry - 18, ry + 18, 161.5, 178, m["dark"], folder="Bench", collide=False)
    box("CrewBoard", -HALF_X, -HALF_X + 6, 60 - 150, 60 + 150, 255 - 84, 255 + 84, m["dark"], folder="Bench", collide=False)
    box("CrewBoard_Glass", -HALF_X + 6, -HALF_X + 7, 60 - 144, 60 + 144, 255 - 78, 255 + 78, m["screen"], folder="Bench", collide=False)
    # The tyre changer, against the west wall just past the bench's end.
    box("TyreChanger_Base", -HALF_X + 16, -HALF_X + 76, 325, 385, 0, 70, m["red"], folder="Bench")
    shape(CYLINDER, "TyreChanger_Table", (-HALF_X + 46, 355, 74), (60, 60, 6), m["steel"], folder="Bench", collide=False)
    box("TyreChanger_Column", -HALF_X + 16, -HALF_X + 26, 350, 360, 70, 150, m["steel"], folder="Bench", collide=False)


def machines(m):
    hx, hy = HALF_X, HALF_Y
    # The parts computer on the east wall: desk, monitor, keyboard ("PARTS: order anything").
    px, py = hx - 45, 190
    box("PC_Desk", px - 37.5, px + 37.5, py - 65, py + 65, 0, 74, m["wood"], folder="Computer")
    box("PC_DeskTop", px - 40, px + 40, py - 67.5, py + 67.5, 74, 78, m["dark"], folder="Computer", collide=False)
    box("PC_Monitor", px + 9.5, px + 14.5, py - 34, py + 34, 89, 131, m["dark"], folder="Computer", collide=False)
    box("PC_Screen", px + 8.5, px + 9.5, py - 31, py + 31, 92, 128, m["screen"], folder="Computer", collide=False)
    box("PC_Keyboard", px - 24, px - 6, py - 22.5, py + 22.5, 78, 80, m["dark"], folder="Computer", collide=False)
    label_text("PC_Title", "PARTS", (px + 8, py, 122), 180.0, 9.0, (178, 230, 255), "Computer")
    # The engine stand by the east bay; the parts washer and repair bench against the front wall.
    sx, sy = hx - 90, -40
    box("EngineStand_Base", sx - 35, sx + 35, sy - 40, sy + 40, 0, 8, m["stand"], folder="Machines")
    box("EngineStand_Post", sx - 5, sx + 5, sy - 5, sy + 5, 8, 72, m["stand"], folder="Machines")
    box("EngineStand_Head", sx - 20, sx + 5, sy - 15, sy + 15, 62, 82, m["steel"], folder="Machines", collide=False)
    wx, wy = hx - 105, hy - 60
    box("PartsWasher", wx - 65, wx + 65, wy - 45, wy + 45, 0, 95, m["blue"], folder="Machines")
    box("PartsWasher_Lid", wx - 65, wx + 65, wy + 20, wy + 45, 95, 160, m["blue"], folder="Machines", collide=False)
    rx, ry = 340.0, hy - 60
    box("RepairBench", rx - 90, rx + 90, ry - 45, ry + 45, 0, 90, m["steel"], folder="Machines")
    box("RepairBench_Top", rx - 90, rx + 90, ry - 45, ry + 45, 90, 94, m["dark"], folder="Machines", collide=False)
    box("RepairBench_Grinder", rx + 40, rx + 70, ry - 10, ry + 20, 94, 120, m["red"], folder="Machines", collide=False)
    # The bins left of the roller door: general waste, plastic and metal recycling.
    for name, x, col in (("General", -275.0, "dark"), ("Plastic", -333.0, "blue"), ("Metal", -391.0, "green")):
        y = hy - 33
        box("Bin_" + name, x - 26, x + 26, y - 30, y + 30, 0, 88, m[col], folder="Bins")
        box("Bin_%s_Lid" % name, x - 28, x + 28, y - 34, y + 32, 88, 92, m[col], folder="Bins", collide=False)
    # The light switches right of the door, inside.
    box("Switches", DOOR_HALF + 48, DOOR_HALF + 62, hy - 5, hy - 2, 123, 147, m["yellow"], folder="Door", collide=False)


def wash_bay(m):
    # The lean-to on the west wall, open to the yard at the front (+y): back and outer walls,
    # tiles to 2 m, a roof, a yellow post and fascia, and the pressure washer on the back wall.
    cx, cy, hx, hy, h = -930.0, 100.0, 240.0, 340.0, 300.0
    box("WashBay_Pad", cx - hx, cx + hx, cy - hy, cy + hy, -12, 0.6, m["pad"], folder="WashBay")
    box("WashBay_Back", cx - hx - 20, cx + hx, cy - hy - 20, cy - hy, 0, h, m["cladding"], folder="WashBay")
    box("WashBay_Outer", cx - hx - 20, cx - hx, cy - hy, cy + hy + 20, 0, h, m["cladding"], folder="WashBay")
    box("WashBay_Roof", cx - hx - 40, cx + hx, cy - hy - 40, cy + hy + 40, h, h + 12, m["roof"], folder="WashBay")
    box("WashBay_Post", cx - hx + 4, cx - hx + 16, cy + hy - 16, cy + hy - 4, 0, h, m["yellow"], folder="WashBay")
    box("WashBay_Fascia", cx - hx, cx + hx, cy + hy - 10, cy + hy, h - 20, h, m["yellow"], folder="WashBay", collide=False)
    label_text("WashBay_Sign", "WASH BAY", (cx, cy + hy + 2, h - 10), 90.0, 22.0, (20, 20, 20), "WashBay")
    ux, uy = -1130.0, -180.0
    box("PressureWasher", ux - 25, ux + 25, uy - 20, uy + 20, 0, 90, m["yellow"], folder="WashBay")
    shape(CYLINDER, "PressureWasher_Reel", (ux + 45, uy - 10, 140), (44, 44, 12), m["dark"], rot=(90.0, 0.0, 0.0), folder="WashBay", collide=False)
    box("WashBay_Drain", -930 - 21, -930 + 21, 100 - 21, 100 + 21, 0.6, 1.2, m["dark"], folder="WashBay", collide=False)


def rooms(m):
    # The rooms off the shed, as empty shells for now: storage (west, behind the wash bay), the
    # paint shop (east) and the dyno room (through the back wall).
    room("Storage", -1500.0, -680.0, -1100.0, -280.0, m, open_sides=("e",))   # the shed's west wall closes it...
    box("Storage_WallE", -680.0, -650.0, -1100.0, -HALF_Y - WALL, 0, ROOM_HEIGHT, m["cladding"], folder="Storage")   # ...and this, behind it
    room("PaintShop", 680.0, 1400.0, -520.0, 520.0, m, open_sides=("w",))
    room("Dyno", -450.0, 450.0, -1300.0, -580.0, m, open_sides=("s",))


def deliveries(m):
    # The deliveries bay on the yard east of the door (Shed.DELIVERY_AREA): painted lines, the
    # floor's front edge, DELIVERIES on the ground.
    x0, x1, y0, y1 = 290.0, 1070.0, 750.0, 1250.0
    for i, (a0, a1, b0, b1) in enumerate(((x0, x0 + 8, y0, y1), (x1 - 8, x1, y0, y1), (x0, x1, y0, y0 + 8), (x0, x1, y1 - 8, y1),
                                          (x0, x1, 1065 - 3, 1065 + 3))):
        box("Deliveries_Line_%d" % i, a0, a1, b0, b1, 0, 0.6, m["yellow"], folder="Outside", collide=False)
    label_text("Deliveries_Mark", "DELIVERIES", ((x0 + x1) / 2, y0 - 45, 1.0), 90.0, 50.0, (217, 178, 51), "Outside", pitch=90.0)


def lights(m):
    # Workshop.gd _build_lights: a strip light over each bay with a spot under it, and three more
    # strips down the middle and over the door.
    spots = [(x, BAY_Y) for x in BAY_X] + [(-420.0, 150.0), (420.0, 150.0), (0.0, 350.0)]
    for i, (x, y) in enumerate(spots):
        box("Strip_%d" % i, x - 10, x + 10, y - 65, y + 65, HEIGHT - 37.5, HEIGHT - 32.5, m["led"], folder="Lights", collide=False)
        light = actors.spawn_actor_from_class(unreal.RectLight, unreal.Vector(x, y, HEIGHT - 39), unreal.Rotator(0.0, -90.0, 0.0))
        light.set_actor_label("StripLight_%d" % i)
        light.set_folder_path("Lights")
        lc = light.get_component_by_class(unreal.RectLightComponent)
        lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        units = getattr(unreal, "LightUnits", None)
        if units is not None:
            try_set(lc, "intensity_units", units.LUMENS)
        for k, v in (("intensity", LED_LUMENS * (1.3 if i < len(BAY_X) else 1.0)), ("source_width", 130.0), ("source_height", 20.0),
                     ("attenuation_radius", 1200.0), ("barn_door_angle", 70.0),
                     ("temperature", 4800.0), ("use_temperature", True)):
            try_set(lc, k, v)
    # The wash bay's own strip.
    light = actors.spawn_actor_from_class(unreal.RectLight, unreal.Vector(-930.0, 100.0, 290.0), unreal.Rotator(0.0, -90.0, 0.0))
    light.set_actor_label("WashBayLight")
    light.set_folder_path("WashBay")
    box("WashBay_Strip", -937.5, -922.5, 20, 180, 292, 296, m["led"], folder="WashBay", collide=False)


def outside(m):
    # Concrete apron in front of the door, the lane off it to the south, the field all round.
    box("Forecourt", -1200, 1500, HALF_Y + WALL, HALF_Y + WALL + 900, -10, 0, m["concrete"], folder="Outside")
    box("Yard", -1600, 1500, -1400, HALF_Y + WALL, -11, -1, m["concrete"], folder="Outside")
    box("Lane", -200, 200, HALF_Y + WALL + 900, 30000, -12, -2, m["tarmac"], folder="Outside")
    box("Field", -40000, 40000, -40000, 40000, -30, -8, m["grass"], folder="Outside")
    # Drystone walls round the yard and along the lane.
    def dry(label, x0, x1, y0, y1):
        box(label, x0, x1, y0, y1, -8, 130, m["drystone"], folder="Outside")
    dry("Wall_YardW", -1700, -1640, -1500, HALF_Y + WALL + 900)
    dry("Wall_YardE", 1540, 1600, -1500, HALF_Y + WALL + 900)
    dry("Wall_Back", -1700, 1600, -1560, -1500)
    dry("Wall_LaneW", -280, -220, HALF_Y + WALL + 900, 12000)
    dry("Wall_LaneE", 220, 280, HALF_Y + WALL + 900, 12000)
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

    shell(m)
    bays(m)
    workbench(m)
    machines(m)
    wash_bay(m)
    rooms(m)
    deliveries(m)
    lights(m)
    outside(m)
    sky()
    ride = actors.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0.0, HALF_Y + 30, 0), unreal.Rotator(0, 0, 0))
    ride.set_actor_label("RideOut")
    ride.set_folder_path("Door")
    ride.set_editor_property("tags", [unreal.Name("RideOut")])

    # Walkers arrive where Torque Shed puts them (Workshop.walker_spawn): in a row 2.6 m in front
    # of the middle, facing the bays.
    for i in range(4):
        actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-150.0 + i * 100.0, 260.0, 100.0), unreal.Rotator(0.0, 0.0, -90.0))
    # Where the start menu looks from (ATorqueMenuPawn): inside the door at eye height, looking in at
    # the bays, below the trusses.
    view = (320.0, 470.0, 170.0)
    to = (BIKE_AT[0] - view[0], BIKE_AT[1] - view[1], 70.0 - view[2])
    v = actors.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(*view), unreal.Rotator(
        0.0, math.degrees(math.atan2(to[2], math.hypot(to[0], to[1]))), math.degrees(math.atan2(to[1], to[0]))))
    v.set_actor_label("MenuView")
    v.set_editor_property("tags", [unreal.Name("MenuView")])

    save_fresh_level(MAP, before)
    # Say what the saved level holds for the game's own lookups (tags), so a run's log shows it.
    for a in actors.get_all_level_actors():
        tags = [str(t) for t in a.get_editor_property("tags")]
        if "MenuView" in tags or "RideOut" in tags:
            log("level has %s (%s) at %s, rotation %s" % (a.get_actor_label(), ",".join(tags), a.get_actor_location(), a.get_actor_rotation()))
    log("built %s: Torque Shed's shed, %d bays" % (MAP, len(BAY_X)))


build()
