"""Build the shed, /Game/Shed/Shed, in the Unreal editor: where the game starts on foot.

A port of Torque Shed's shed (the Godot game's scripts/workshop.gd, with shed_state.gd's fixed
machines): the same rooms, the same places for everything, so it plays as it did. Godot metres
(x east, z towards the roller door, the shed's middle at z 65) become Unreal cm here as
x = 100 gx, y = 100 (gz - 65), z = 100 gy.

In the editor: Tools > Execute Python Script... and pick this file, or start the editor with
-ExecutePythonScript=Tools/ue_build_shed.py. Run Tools/ue_build_level.py first (it imports the
Speed Twin and writes Data/Bike/SpeedTwin/ue_assets.json, which the shed's bikes draw from, and the
Megascans/ambientCG textures this reuses), and python Tools/fetch_textures.py for the shed's own
scanned surfaces (the shed_ roles: floor, plaster, cladding, roof, steel, timber, the paint shop's
walls, the yard; any not there stay plain colours). Safe to run again: it rebuilds the map from scratch.

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
    room built as a shell (the dyno room and the paint shop fitted out: the rig, desk and switches;
    the downdraft spray bay with its hooks under hexagon lights), and the wash bay lean-to on the west wall, open to the
    yard, with the pressure washer on its back wall;
  - the deliveries bay painted on the yard east of the door; fields, lane and fells round it;
  - daylight as Buttertubs; the shed's strip lights over each bay and down the middle; Lumen;
  - a point tagged RideOut in the door and a player start where Torque Shed puts walkers.
The game mode spawns the walker (ATorqueWalker) in any map with an ATorqueShedBike in it.
"""
import json
import math
import os
import sys

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
VOLUMETRIC_FOG = False      # the faint dusty shaft of daylight through the roller door (the fog's volumetric)
SKY_LEAK = 0.02             # Lumen's skylight leaking: keeps the far corners off crushed black (0.04 flattened the AO)
LOCAL_FOG = 0.0             # the dust in the hall's air (a local fog volume's height fog extinction): 0 for none
SUN_SCATTER = 3.0           # how strongly the sun lights that dust (the directional light's volumetric scattering)
SUN_K, STRIP_K = 5000.0, 6000.0   # warm sun, cool daylight-white fluorescents (the white balance sits at 6300 K)
LED_GLOW = 600.0            # the tubes' own glow: a real tube is ~200x a lit wall (6, then 45, read as white boxes)
HEX_GLOW = 250.0            # the paint shop's hex tiles (many of them, so less each)
FOOT_AO = 0.45              # the soft dark at the foot of walls, cabinets and benches (a decal each): 0 for none

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
# The paint mixing bay through the paint shop's east wall (paint_bay.gd ROOM, DOOR_Z, DOOR_HALF,
# DOOR_HEIGHT): its door at y 460. Keep in step with Shed/TorquePaintBay.h and TorqueShedLayout.h.
MIXBAY_X0, MIXBAY_X1, MIXBAY_Y0, MIXBAY_Y1 = 1430.0, 1700.0, -520.0, 520.0
MIXBAY_DOOR_Y, MIXBAY_DOOR_HALF, MIXBAY_DOOR_TOP = 460.0, 42.0, 220.0
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


def ghost_xray_material():
    """M_ShedGhostXRay: the edges of where a held part goes down, seen through whatever is in the way
    (the part in your hands, a bench): M_ShedHighlight drawn without the depth test. Color, Opacity."""
    m = new_material("M_ShedGhostXRay")
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    try_set(m, "disable_depth_test", True)
    col = expr(m, unreal.MaterialExpressionVectorParameter, -600, 0, parameter_name="Color",
               default_value=unreal.LinearColor(0.7, 1.7, 2.0, 1.0))
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    op = expr(m, unreal.MaterialExpressionScalarParameter, -600, 200, parameter_name="Opacity", default_value=0.35)
    mel.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    return finish(m)


def outline_material():
    """M_ShedOutline: a thin, soft line round what you look at, for the game (C++, TorqueLookOutline)
    to put on the walker's camera as a post-process. The game marks what's looked at with custom
    depth and stencil (r.CustomDepth=3); this draws a line just outside that shape: two rings of
    stencil samples (a crisp inner one, a fainter outer one for softness), drawn fainter where
    something in front hides the part's edge, and blended before depth of field (so TSR steadies it):
    orange over dark things, deeper orange over light ones.
    Parameters: Color, DarkColor, Opacity, Width (pixels at 1080p, scaled with the screen),
    Hidden (how much of the line shows through things in front)."""
    m = new_material("M_ShedOutline")
    if not try_set(m, "material_domain", unreal.MaterialDomain.MD_POST_PROCESS):
        raise RuntimeError("M_ShedOutline: can't make a post-process material")
    # Before depth of field, so before TSR (beenster, Nov 9: "highlighter should not shimmer"). After
    # tonemapping it ran on the upscaled frame but read custom depth/stencil at render size, jittered
    # a sub-pixel each frame: the 1-2 px line crawled. Here TSR averages the jitter into a steady,
    # anti-aliased line. The scene colour here is pre-exposed HDR, so Color ~1 still reads the same
    # whatever the shed's exposure.
    loc = None
    for name in ("BL_SCENE_COLOR_BEFORE_DOF", "BL_BEFORE_TRANSLUCENCY"):
        loc = getattr(unreal.BlendableLocation, name, None)
        if loc is not None:
            break
    if loc is None or not try_set(m, "blendable_location", loc):
        raise RuntimeError("M_ShedOutline: can't put it before depth of field (it would shimmer)")
    M = unreal.MaterialProperty

    def link(a, b, pin, out=""):
        if not mel.connect_material_expressions(a, out, b, pin):
            raise RuntimeError("M_ShedOutline: can't connect %s -> %s.%s" % (a.get_name(), b.get_name(), pin))

    def op(cls, x, y, a, b=None, **props):
        e = expr(m, cls, x, y, **props)
        link(a, e, "A")
        if b is not None:
            link(b, e, "B")
        return e

    def num(name, x, y, v):
        return expr(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)

    def const(x, y, v):
        return expr(m, unreal.MaterialExpressionConstant, x, y, r=v)

    def red(x, y, e):
        k = expr(m, unreal.MaterialExpressionComponentMask, x, y, r=True, g=False, b=False, a=False)
        link(e, k, "")
        return k

    uv_pin = [None]

    def scene(tex_id, x, y, uv=None):
        e = expr(m, unreal.MaterialExpressionSceneTexture, x, y, scene_texture_id=tex_id)
        if uv is not None:
            # The pin is "UVs" in the editor (Coordinates in C++): find it once.
            names = [uv_pin[0]] if uv_pin[0] else ["UVs", "Coordinates", "UV"]
            for n in names:
                if mel.connect_material_expressions(uv, "", e, n):
                    uv_pin[0] = n
                    break
            else:
                raise RuntimeError("M_ShedOutline: no UV pin on SceneTexture")
        return red(x + 250, y, e)

    T = unreal.SceneTextureId
    # A pixel step: Width/1080 of the screen's height, the same across (times height/width).
    screen = expr(m, unreal.MaterialExpressionScreenPosition, -3000, 0)
    view = expr(m, unreal.MaterialExpressionViewSize, -3000, 200)
    vw = expr(m, unreal.MaterialExpressionComponentMask, -2800, 150, r=True, g=False, b=False, a=False)
    link(view, vw, "")
    vh = expr(m, unreal.MaterialExpressionComponentMask, -2800, 250, r=False, g=True, b=False, a=False)
    link(view, vh, "")
    width = op(unreal.MaterialExpressionDivide, -2600, 350, num("Width", -2800, 350, 1.7), const(-2800, 420, 1080.0))
    step_x = op(unreal.MaterialExpressionMultiply, -2400, 200, op(unreal.MaterialExpressionDivide, -2600, 200, vh, vw), width)
    step = op(unreal.MaterialExpressionAppendVector, -2200, 250, step_x, width)

    dirs = [(1, 0), (-1, 0), (0, 1), (0, -1), (0.7071, 0.7071), (-0.7071, 0.7071), (0.7071, -0.7071), (-0.7071, -0.7071)]
    rings = []
    depth_min = None
    y = 0
    for r in (1.0, 2.2):
        ring = None
        for i, (dx, dy) in enumerate(dirs):
            off = op(unreal.MaterialExpressionMultiply, -2000, y, step,
                     expr(m, unreal.MaterialExpressionConstant2Vector, -2200, y + 60, r=dx * r, g=dy * r))
            uv = op(unreal.MaterialExpressionAdd, -1800, y, screen, off)
            mark = expr(m, unreal.MaterialExpressionSaturate, -1300, y)
            link(scene(T.PPI_CUSTOM_STENCIL, -1700, y, uv), mark, "")
            ring = mark if ring is None else op(unreal.MaterialExpressionMax, -1100, y, ring, mark)
            if i < 4:   # the part's nearest depth beside this pixel
                d = scene(T.PPI_CUSTOM_DEPTH, -1700, y + 80, uv)
                depth_min = d if depth_min is None else op(unreal.MaterialExpressionMin, -1100, y + 80, depth_min, d)
            y += 180
        rings.append(ring)
    centre = expr(m, unreal.MaterialExpressionSaturate, -1300, y)
    link(scene(T.PPI_CUSTOM_STENCIL, -1700, y), centre, "")
    outside = op(unreal.MaterialExpressionSubtract, -900, y, const(-1100, y + 60, 1.0), centre)
    soft = op(unreal.MaterialExpressionMax, -900, y - 200, rings[0],
              op(unreal.MaterialExpressionMultiply, -1000, y - 140, rings[1], const(-1200, y - 100, 0.45)))
    edge = op(unreal.MaterialExpressionMultiply, -700, y - 100, soft, outside)
    # Hidden: this pixel is nearer than the part's edge beside it (something in front of the part).
    here = scene(T.PPI_SCENE_DEPTH, -1700, y + 200)
    gap = op(unreal.MaterialExpressionDivide, -900, y + 200, op(unreal.MaterialExpressionSubtract, -1000, y + 200, here, depth_min), const(-1100, y + 260, 4.0))
    seen = expr(m, unreal.MaterialExpressionSaturate, -600, y + 200)
    link(op(unreal.MaterialExpressionAdd, -750, y + 200, gap, const(-900, y + 260, 1.0)), seen, "")
    show = expr(m, unreal.MaterialExpressionLinearInterpolate, -450, y + 150)
    link(num("Hidden", -650, y + 100, 0.3), show, "A")
    link(const(-650, y + 120, 1.0), show, "B")
    link(seen, show, "Alpha")
    alpha = op(unreal.MaterialExpressionMultiply, -300, y, op(unreal.MaterialExpressionMultiply, -450, y, edge, show),
               num("Opacity", -500, y + 300, 0.75))
    base = expr(m, unreal.MaterialExpressionSceneTexture, -700, y - 450, scene_texture_id=T.PPI_POST_PROCESS_INPUT0)
    rgb = expr(m, unreal.MaterialExpressionComponentMask, -450, y - 450, r=True, g=True, b=True, a=False)
    link(base, rgb, "")
    # Orange (beenster, Nov 9): Torque Shed's highlight orange over dark things, a deeper orange over
    # light ones (the white walls, a silver tank), so it reads against either. Linear colours now (it
    # blends before the tonemapper): the same oranges as before once tonemapped.
    lum = op(unreal.MaterialExpressionDotProduct, -300, y - 600, rgb,
             expr(m, unreal.MaterialExpressionConstant3Vector, -450, y - 600, constant=unreal.LinearColor(0.3, 0.59, 0.11, 1.0)))
    bright = expr(m, unreal.MaterialExpressionSaturate, -100, y - 600)
    link(op(unreal.MaterialExpressionMultiply, -200, y - 600,
            op(unreal.MaterialExpressionSubtract, -250, y - 650, lum, const(-400, y - 650, 0.3)), const(-400, y - 700, 2.5)), bright, "")
    colour = expr(m, unreal.MaterialExpressionLinearInterpolate, -300, y - 300)
    link(expr(m, unreal.MaterialExpressionVectorParameter, -500, y - 350, parameter_name="Color",
              default_value=unreal.LinearColor(1.2, 0.3, 0.02, 1.0)), colour, "A")
    link(expr(m, unreal.MaterialExpressionVectorParameter, -500, y - 250, parameter_name="DarkColor",
              default_value=unreal.LinearColor(0.85, 0.1, 0.0, 1.0)), colour, "B")
    link(bright, colour, "Alpha")
    out = expr(m, unreal.MaterialExpressionLinearInterpolate, -150, y - 200)
    link(rgb, out, "A")
    link(colour, out, "B")
    link(alpha, out, "Alpha")
    mel.connect_material_property(out, "", M.MP_EMISSIVE_COLOR)
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
    bright = expr(m, unreal.MaterialExpressionScalarParameter, -700, 250, parameter_name="Brightness", default_value=4.5)
    mul = expr(m, unreal.MaterialExpressionMultiply, -300, 0)
    mel.connect_material_expressions(tex, "RGB", mul, "A")
    mel.connect_material_expressions(bright, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish(m)


def badge_material():
    """M_ShedBadge: a tank's badge (Shed/TorqueBikeBadge.cpp, bike_badge.gd's decals), a thin skin
    over the tank's paint wearing its picture: the picture (parameter Badge, white where it covers)
    cut out of it, in its colour (Color), lit as a glossy transfer (Roughness)."""
    m = new_material("M_ShedBadge")
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_MASKED)
    # The picture's soft edge is sampled as sRGB: a linear 0.2 is about half way across it.
    try_set(m, "opacity_mask_clip_value", 0.2)
    tex = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0, parameter_name="Badge")
    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    if white:
        try_set(tex, "texture", white)
    mel.connect_material_property(tex, "R", unreal.MaterialProperty.MP_OPACITY_MASK)
    col = expr(m, unreal.MaterialExpressionVectorParameter, -700, -250, parameter_name="Color",
               default_value=unreal.LinearColor(0.92, 0.92, 0.9, 1.0))
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(m, unreal.MaterialExpressionScalarParameter, -700, 300, parameter_name="Roughness", default_value=0.3)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    return finish(m)


# The masked pattern, in the part's frame in cm (paint_mix.gd masked(), its metres x 100): 1 a centre
# stripe 6 cm wide, 2 twin stripes, 3 a pinstripe round its middle, 4 the top half (two-tone).
MASK_HLSL = """
float3 q = float3(dot(R0, P), dot(R1, P), dot(R2, P)) + Offset;
float aa = 0.15;
float x = abs(q.y);
float y = q.z;
if (Pattern < 0.5) return 0.0;
if (Pattern < 1.5) return 1.0 - smoothstep(3.0 - aa, 3.0 + aa, x);
if (Pattern < 2.5) return (1.0 - smoothstep(4.5 - aa, 4.5 + aa, x)) * smoothstep(2.2 - aa, 2.2 + aa, x);
if (Pattern < 3.5) return 1.0 - smoothstep(0.4 - aa * 0.5, 0.4 + aa * 0.5, abs(y));
return smoothstep(-aa, aa, y);
"""


def effect_paint_material():
    """M_ShedEffectPaint, for the game (C++, TorqueEffectPaint.cpp) to put on a part sprayed with an
    effect paint from the mixing bay (paint_mix.gd's spec), or masked with tape: a clear-coated paint whose colour flips
    with the angle for a chameleon (Color to FlipA to FlipB, by Flip), takes a pearl's sheen at the
    edges (PearlTint, by Pearl), deepens towards the edges for a candy (Depth), and sparkles with
    flakes stuck to the part (FlakeColour, by Flake). Opaque, a handful of maths nodes and one
    texture-based noise: solid paints keep the bike's own materials. The road dirt over it as on the
    bike's own (Tools/ue_bike_dirt.py: Grime and the rest)."""
    m = new_material("M_ShedEffectPaint")
    try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    M = unreal.MaterialProperty

    def vec(name, x, y, rgb):
        return expr(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                    default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def num(name, x, y, v):
        return expr(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)

    def op(cls, x, y, a, b=None, a_out="", b_out="", **props):
        e = expr(m, cls, x, y, **props)
        mel.connect_material_expressions(a, a_out, e, "A")
        if b is not None:
            mel.connect_material_expressions(b, b_out, e, "B")
        return e

    def lerp(x, y, a, b, alpha):
        e = expr(m, unreal.MaterialExpressionLinearInterpolate, x, y)
        mel.connect_material_expressions(a, "", e, "A")
        mel.connect_material_expressions(b, "", e, "B")
        mel.connect_material_expressions(alpha, "", e, "Alpha")
        return e

    def sat(x, y, a):
        e = expr(m, unreal.MaterialExpressionSaturate, x, y)
        mel.connect_material_expressions(a, "", e, "")
        return e

    col = vec("Color", -2000, 0, (0.8, 0.1, 0.1))
    fres = expr(m, unreal.MaterialExpressionFresnel, -2000, 200, exponent=3.0, base_reflect_fraction=0.0)
    # The chameleon: facing you its colour, turning away FlipA, then FlipB at the very edge.
    flip = num("Flip", -2000, 400, 0.0)
    f2 = op(unreal.MaterialExpressionMultiply, -1800, 200, fres, const_b=2.0)
    near = sat(-1600, 200, f2)
    far = sat(-1600, 300, op(unreal.MaterialExpressionSubtract, -1700, 300, f2, const_b=1.0))
    c1 = lerp(-1400, 0, col, vec("FlipA", -1600, 450, (0.1, 0.6, 0.3)), op(unreal.MaterialExpressionMultiply, -1500, 250, near, flip))
    c2 = lerp(-1200, 0, c1, vec("FlipB", -1400, 450, (0.1, 0.3, 0.8)), op(unreal.MaterialExpressionMultiply, -1300, 300, far, flip))
    # The pearl's sheen at the edges.
    pearl = op(unreal.MaterialExpressionMultiply, -1200, 600, fres, num("Pearl", -1400, 650, 0.0))
    c3 = lerp(-1000, 0, c2, vec("PearlTint", -1200, 750, (0.9, 0.9, 1.0)), op(unreal.MaterialExpressionMultiply, -1100, 600, pearl, const_b=0.6))
    # A candy looks deeper where you see through more of it (the edges): its colour times itself there.
    depth = sat(-1000, 900, op(unreal.MaterialExpressionMultiply, -1100, 900, fres, num("Depth", -1300, 950, 0.0)))
    deep = lerp(-900, 800, expr(m, unreal.MaterialExpressionConstant, -1000, 800, r=1.0), c3, depth)
    c4 = op(unreal.MaterialExpressionMultiply, -800, 0, c3, deep)
    # Flakes: specks of the flake colour stuck to the part (its own position, so they move with it).
    noise = expr(m, unreal.MaterialExpressionNoise, -1400, 1100, scale=2.5, levels=1, output_min=0.0, output_max=1.0, turbulence=False)
    try_set(noise, "noise_function", unreal.NoiseFunction.NOISEFUNCTION_SIMPLEX_TEX)
    mel.connect_material_expressions(expr(m, unreal.MaterialExpressionLocalPosition, -1600, 1100), "", noise, "Position")
    flake = num("Flake", -1400, 1300, 0.0)
    cut = op(unreal.MaterialExpressionSubtract, -1200, 1300, expr(m, unreal.MaterialExpressionConstant, -1300, 1250, r=1.0),
             op(unreal.MaterialExpressionMultiply, -1300, 1300, flake, const_b=0.35))
    speck = sat(-900, 1150, op(unreal.MaterialExpressionMultiply, -1000, 1150,
                               op(unreal.MaterialExpressionSubtract, -1100, 1150, noise, cut), const_b=12.0))
    c5 = lerp(-600, 0, c4, vec("FlakeColour", -800, 1300, (0.85, 0.85, 0.88)), op(unreal.MaterialExpressionMultiply, -700, 1150, speck, const_b=0.8))
    # Masking (paint_mix.gd masked()): the colour kept under the tape in a pattern across the part's
    # own frame (x forward, y right, z up, from its middle; the game sets PartRow0..2 and PartOffset
    # per mesh), and the tape itself while it's still on.
    mask = expr(m, unreal.MaterialExpressionCustom, -900, 1800)
    try_set(mask, "code", MASK_HLSL)
    try_set(mask, "output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    ins = []
    for n in ("P", "R0", "R1", "R2", "Offset", "Pattern"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    try_set(mask, "inputs", ins)
    mel.connect_material_expressions(expr(m, unreal.MaterialExpressionLocalPosition, -1200, 1750), "", mask, "P")
    mel.connect_material_expressions(vec("PartRow0", -1200, 1850, (1.0, 0.0, 0.0)), "", mask, "R0")
    mel.connect_material_expressions(vec("PartRow1", -1200, 1950, (0.0, 1.0, 0.0)), "", mask, "R1")
    mel.connect_material_expressions(vec("PartRow2", -1200, 2050, (0.0, 0.0, 1.0)), "", mask, "R2")
    mel.connect_material_expressions(vec("PartOffset", -1200, 2150, (0.0, 0.0, 0.0)), "", mask, "Offset")
    mel.connect_material_expressions(num("Pattern", -1200, 2250, 0.0), "", mask, "Pattern")
    tape = op(unreal.MaterialExpressionMultiply, -700, 1900, mask, num("Tape", -900, 2000, 0.0))
    masked = lerp(-500, 0, c5, vec("MaskColor", -700, 1700, (1.0, 1.0, 1.0)), mask)
    taped = lerp(-450, 0, masked, vec("TapeColor", -700, 2100, (0.18, 0.42, 0.85)), tape)
    metal = lerp(-300, 300, num("Metallic", -600, 300, 0.0), expr(m, unreal.MaterialExpressionConstant, -600, 380, r=1.0), speck)
    metal = lerp(-250, 300, metal, num("MaskMetal", -500, 330, 0.05), mask)
    metal = lerp(-200, 300, metal, expr(m, unreal.MaterialExpressionConstant, -400, 380, r=0.0), tape)
    rough = lerp(-300, 500, num("Roughness", -600, 500, 0.15), expr(m, unreal.MaterialExpressionConstant, -600, 580, r=0.08), speck)
    rough = lerp(-250, 500, rough, num("MaskRough", -500, 530, 0.15), mask)
    rough = lerp(-200, 500, rough, expr(m, unreal.MaterialExpressionConstant, -400, 580, r=0.85), tape)
    # The road dirt over it, as on the bike's own materials (Tools/ue_bike_dirt.py).
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ue_bike_dirt
    look, glow = ue_bike_dirt.add_dirt(m)
    base, metal, rough, emis, _ = ue_bike_dirt.lay_over(m, -150, 2400, look, glow, taped, metal, rough)
    mel.connect_material_property(base, "", M.MP_BASE_COLOR)
    mel.connect_material_property(metal, "", M.MP_METALLIC)
    mel.connect_material_property(rough, "", M.MP_ROUGHNESS)
    mel.connect_material_property(emis, "", M.MP_EMISSIVE_COLOR)
    # Clear coat and its roughness: UE 5.8's Python MaterialProperty has no MP_CUSTOM_DATA0/1, so set them
    # only where the enum has them (unconnected, the clear-coat model defaults to a full coat).
    if hasattr(M, "MP_CUSTOM_DATA0"):
        mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant, -300, 800, r=1.0), "", M.MP_CUSTOM_DATA0)   # clear coat
        mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant, -300, 900, r=0.04), "", M.MP_CUSTOM_DATA1)   # its roughness
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


SHED_TEX = "/Game/Shed/Textures"


def shed_scans():
    """The shed's scanned surfaces (the shed_ roles in Data/Textures/textures.json): a Megascans
    surface already in the project (the role's 'ue' block) first, else the ambientCG scan that
    Tools/fetch_textures.py downloaded, imported here (its OpenGL normal's green flipped).
    {role: (role spec, {map: Texture2D})} for those there are; the rest stay plain colours."""
    root = os.path.join(os.path.abspath(unreal.Paths.project_dir()), "Data", "Textures")
    try:
        spec = json.load(open(os.path.join(root, "textures.json")))
    except Exception as e:
        unreal.log_warning("[Torque] no Data/Textures/textures.json (%s): the shed stays in plain colours" % e)
        return {}
    out = {}
    for role, r in spec["roles"].items():
        if not role.startswith("shed_"):
            continue
        if "ue" in r:
            maps = {k: unreal.load_asset(v) for k, v in r["ue"].items()}
            maps = {k: v for k, v in maps.items() if isinstance(v, unreal.Texture)}
            if "color" in maps:
                out[role] = (r, maps)
                continue
        folder = os.path.join(root, r["id"])
        if not os.path.exists(os.path.join(folder, "color.jpg")):
            unreal.log_warning("[Torque] shed texture %s (%s) not downloaded: python Tools/fetch_textures.py" % (r["id"], role))
            continue
        maps = {}
        for name in ("color", "normal", "rough"):
            f = os.path.join(folder, name + ".jpg")
            if not os.path.exists(f):
                continue
            dest = "%s/%s" % (SHED_TEX, r["id"])
            t = unreal.AssetImportTask()
            t.set_editor_property("filename", f)
            t.set_editor_property("destination_path", dest)
            t.set_editor_property("destination_name", "T_%s_%s" % (r["id"], name))
            t.set_editor_property("automated", True)
            t.set_editor_property("replace_existing", True)
            t.set_editor_property("save", False)
            asset_tools.import_asset_tasks([t])
            tex = unreal.load_asset("%s/T_%s_%s" % (dest, r["id"], name))
            if not tex:
                unreal.log_warning("[Torque] couldn't import " + f)
                continue
            if name != "color":
                tex.set_editor_property("srgb", False)
            if name == "normal":
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
                try_set(tex, "flip_green_channel", True)   # ambientCG's normals are OpenGL's
            unreal.EditorAssetLibrary.save_loaded_asset(tex)
            maps[name] = tex
        if "color" in maps:
            out[role] = (r, maps)
    log("shed scans: " + (", ".join("%s=%s" % (k, v[0]["id"]) for k, v in out.items()) or "none (plain colours)"))
    return out


def scanned(scans, role, name, rgb, rough, metallic=0.0, grain=0.06):
    """A shed surface from its scan, projected from the world's three axes (the basic shapes' UVs
    stretch), tinted so the scan's mean colour comes out as Torque Shed's rgb and its roughness
    scaled to Torque Shed's rough. Without the scan (or if the nodes don't connect): flat()."""
    if role not in scans:
        return flat(name, rgb, rough, metallic=metallic, grain=grain)
    r, maps = scans[role]
    mean = r.get("mean", [0.5, 0.5, 0.5])
    tint = [rgb[i] / max(mean[i], 0.01) for i in range(3)]
    rough_k = rough / max(r.get("rough_mean", 0.5), 0.05)
    size_cm = r.get("tile_m", 1.0) * 100.0
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

        c = project(maps["color"], 0, WAT)
        t = expr(m, unreal.MaterialExpressionConstant3Vector, -1000, -150, constant=unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
        mul = expr(m, unreal.MaterialExpressionMultiply, -600, 0)
        link(c, "XYZ Texture", mul, "A")
        link(t, "", mul, "B")
        mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
        if "rough" in maps:
            rr = project(maps["rough"], 300, WAT)
            mask = expr(m, unreal.MaterialExpressionComponentMask, -700, 300, r=True, g=False, b=False, a=False)
            link(rr, "XYZ Texture", mask, "")
            k = expr(m, unreal.MaterialExpressionConstant, -700, 420, r=rough_k)
            rmul = expr(m, unreal.MaterialExpressionMultiply, -450, 300)
            link(mask, "", rmul, "A")
            link(k, "", rmul, "B")
            sat = expr(m, unreal.MaterialExpressionSaturate, -300, 300)
            link(rmul, "", sat, "")
            mel.connect_material_property(sat, "", unreal.MaterialProperty.MP_ROUGHNESS)
        else:
            mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant, -600, 300, r=rough), "", unreal.MaterialProperty.MP_ROUGHNESS)
        if "normal" in maps:
            n = project(maps["normal"], 600, WAN)
            # The role's normal_k: how much of the scan's relief shows (1 all, 0 flat); a big face
            # shows a strong scan's height as ripples.
            nk = float(r.get("normal_k", 1.0))
            if nk < 0.999:
                up = expr(m, unreal.MaterialExpressionConstant3Vector, -700, 700, constant=unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
                soft = expr(m, unreal.MaterialExpressionLinearInterpolate, -450, 600, const_alpha=max(nk, 0.0))
                link(up, "", soft, "A")
                link(n, "XYZ Texture", soft, "B")
                mel.connect_material_property(soft, "", unreal.MaterialProperty.MP_NORMAL)
            else:
                mel.connect_material_property(n, "XYZ Texture", unreal.MaterialProperty.MP_NORMAL)
        if metallic:
            mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant, -600, 800, r=metallic), "", unreal.MaterialProperty.MP_METALLIC)
        if not ok[0]:
            raise RuntimeError("nodes didn't connect")
        return finish(m)
    except Exception as e:
        unreal.log_warning("[Torque] %s: the scan didn't build (%s); a plain colour instead" % (name, e))
        return flat(name, rgb, rough, metallic=metallic, grain=grain)


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
    scans = shed_scans()   # the photoreal pass: scanned surfaces in Torque Shed's colours
    # The spray mini game's coat, mist and footprint (TorqueSprayJob.cpp, TorqueSprayFx.cpp).
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import ue_build_spray
        ue_build_spray.build()
    except Exception as e:
        unreal.log_error("[Torque] spray materials: %s" % e)
    # (The wash mini game draws no muck of its own: the bike's materials carry the road dirt and its
    # wash mask, Tools/ue_bike_dirt.py.)
    return {
        # Torque Shed's shed (workshop.gd _make_materials): steel cladding, plaster over a dark
        # band inside, an epoxy floor, steel trusses, timber bench tops, hazard yellow.
        "cladding": scanned(scans, "shed_cladding", "M_ShedCladding", (0.30, 0.33, 0.36), 0.45, metallic=0.7, grain=0.06),
        "roof": scanned(scans, "shed_roof", "M_ShedRoof", (0.25, 0.27, 0.29), 0.5, metallic=0.6, grain=0.0),
        "floor": scanned(scans, "shed_floor", "M_ShedEpoxy", (0.36, 0.37, 0.38), 0.3, grain=0.06),
        "wall_in": scanned(scans, "shed_plaster", "M_ShedPlaster", (0.78, 0.76, 0.72), 0.85, grain=0.06),
        "wall_low": scanned(scans, "shed_plaster", "M_ShedWallBand", (0.20, 0.24, 0.27), 0.6, grain=0.0),
        "steel": scanned(scans, "shed_steel", "M_ShedSteel", (0.30, 0.32, 0.35), 0.35, metallic=0.85, grain=0.0),
        "stand": flat("M_ShedStandOrange", (0.85, 0.35, 0.05), 0.4, metallic=0.5),
        "wood": scanned(scans, "shed_timber", "M_ShedTimber", (0.50, 0.36, 0.22), 0.7, grain=0.08),
        "yellow": flat("M_ShedYellow", (0.95, 0.75, 0.05), 0.5),
        "dark": flat("M_ShedDark", (0.06, 0.06, 0.07), 0.5, metallic=0.3),
        "blue": flat("M_ShedBlue", (0.10, 0.30, 0.65), 0.35, metallic=0.6),
        "red": flat("M_ShedRed", (0.70, 0.08, 0.06), 0.35, metallic=0.5),
        "green": flat("M_ShedGreen", (0.20, 0.55, 0.30), 0.45, metallic=0.1),
        "pad": flat("M_ShedWashPad", (0.42, 0.44, 0.45), 0.25, grain=0.1),
        "screen": flat("M_ShedScreen", (0.10, 0.30, 0.50), 0.3, emissive=1.4),
        "highlight": highlight_material(),     # for the game: what's looked at, where a part goes down
        "ghost_xray": ghost_xray_material(),   # for the game: where a part goes down, seen through things
        "outline": outline_material(),         # for the game: the soft line round what's looked at
        "screen_page": screen_page_material(),   # for the game: the screens' pages
        "badge": badge_material(),               # for the game: a tank's badge
        "effect_paint": effect_paint_material(),   # for the game: parts sprayed with pearl, candy, flake, chameleon
        "led": flat("M_ShedLed", (0.9, 0.92, 0.95), 0.5, emissive=LED_GLOW),
        # The paint shop (workshop.gd _build_booth_room, equipment_view.gd _build_booth): white walls,
        # a dark ceiling for the hexagon LED tiles, the spray bay's panels, filter, grate and rails.
        "paint_wall": scanned(scans, "shed_paint_wall", "M_PaintShopWall", (0.90, 0.91, 0.92), 0.35, grain=0.0),
        "paint_ceiling": flat("M_PaintShopCeiling", (0.08, 0.08, 0.09), 0.6, metallic=0.2),
        "hex_led": flat("M_PaintShopHexLed", (0.97, 0.99, 1.0), 0.4, emissive=HEX_GLOW),
        "booth_panel": flat("M_BoothPanel", (0.94, 0.95, 0.96), 0.5, metallic=0.05),   # white enamel (0.88 read light grey)
        "booth_filter": flat("M_BoothFilter", (0.95, 0.60, 0.35), 0.95, grain=0.1),
        "booth_grate": flat("M_BoothGrate", (0.20, 0.21, 0.23), 0.4, metallic=0.8),
        "chrome": flat("M_ShedChrome", (0.88, 0.89, 0.92), 0.15, metallic=0.95),
        # Outside: the yard, the lane and the Dales round it.
        "concrete": scanned(scans, "shed_yard", "M_ShedConcrete", (0.36, 0.35, 0.33), 0.82, grain=0.18),
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
    # Standing on the floor (walls, cabinets, benches, posts): a soft dark at its foot (foot_ao).
    bottom = centre[2] - size[2] * 0.5
    if (abs(bottom) < 1.5 and size[2] >= 30.0 and min(size[0], size[1]) >= 2.0 and rot == (0.0, 0.0, 0.0)
            and not folder.startswith("Outside")):
        FOOTPRINTS.append((label, centre[0], centre[1], size[0], size[1]))
    return a


FOOTPRINTS = []   # (label, x, y, size x, size y) of what stands on the floor, for foot_ao()


FLOOR_PAINT = []   # M_ShedFloorPaint, made on first use


def floor_paint_material():
    """M_ShedFloorPaint: yellow line paint rolled onto the floor (beenster, Nov 9: "should be
    painted on, not like tape"). A decal, so it has no thickness and no edge to catch the light:
    it takes the floor's own bumps (no normal of its own), with a satin paint's roughness, its
    edges just soft, and a little wear where feet and tyres have scuffed it (world-space noise, so
    every line wears differently)."""
    if FLOOR_PAINT:
        return FLOOR_PAINT[0]
    mat = new_material("M_ShedFloorPaint")
    try_set(mat, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    try_set(mat, "material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    uv = expr(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 0)
    centred = expr(mat, unreal.MaterialExpressionConstantBiasScale, -1200, 0, bias=-0.5, scale=2.0)
    mel.connect_material_expressions(uv, "", centred, "")
    ab = expr(mat, unreal.MaterialExpressionAbs, -1050, 0)
    mel.connect_material_expressions(centred, "", ab, "")
    # 1 - |c|^24 on each axis: full paint to just short of the edge, then a quick soft fall-off.
    pw = expr(mat, unreal.MaterialExpressionPower, -900, 0, const_exponent=24.0)
    mel.connect_material_expressions(ab, "", pw, "Base")
    inv = expr(mat, unreal.MaterialExpressionOneMinus, -750, 0)
    mel.connect_material_expressions(pw, "", inv, "")
    mu = expr(mat, unreal.MaterialExpressionComponentMask, -600, -60, r=True, g=False, b=False, a=False)
    mv = expr(mat, unreal.MaterialExpressionComponentMask, -600, 60, r=False, g=True, b=False, a=False)
    mel.connect_material_expressions(inv, "", mu, "")
    mel.connect_material_expressions(inv, "", mv, "")
    edge = expr(mat, unreal.MaterialExpressionMultiply, -450, 0)
    mel.connect_material_expressions(mu, "", edge, "A")
    mel.connect_material_expressions(mv, "", edge, "B")
    # Wear: big soft patches (~40 cm) and fine speckle (~2 cm), from 1 (whole) down to ~0.55.
    wear = None
    for i, (scale, lo) in enumerate(((0.025, 0.72), (0.45, 0.8))):
        n = expr(mat, unreal.MaterialExpressionNoise, -900, 250 + i * 200, scale=scale, quality=1, levels=2,
                 output_min=lo, output_max=1.25, turbulence=False)
        sat = expr(mat, unreal.MaterialExpressionSaturate, -700, 250 + i * 200)
        mel.connect_material_expressions(n, "", sat, "")
        if wear is None:
            wear = sat
        else:
            both = expr(mat, unreal.MaterialExpressionMultiply, -550, 300)
            mel.connect_material_expressions(wear, "", both, "A")
            mel.connect_material_expressions(sat, "", both, "B")
            wear = both
    alpha = expr(mat, unreal.MaterialExpressionMultiply, -300, 100)
    mel.connect_material_expressions(edge, "", alpha, "A")
    mel.connect_material_expressions(wear, "", alpha, "B")
    mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    col = expr(mat, unreal.MaterialExpressionConstant3Vector, -300, -200, constant=unreal.LinearColor(0.78, 0.55, 0.02, 1.0))
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(expr(mat, unreal.MaterialExpressionConstant, -300, -80, r=0.42), "", unreal.MaterialProperty.MP_ROUGHNESS)
    FLOOR_PAINT.append(finish(mat))
    return FLOOR_PAINT[0]


def floor_paint(label, x0, x1, y0, y1, folder, z=0.0):
    """A painted line on the floor between x0..x1, y0..y1 (cm), at floor height z: a decal pointing
    down that only reaches 2 cm either side of the floor, so it paints nothing standing on it."""
    d = actors.spawn_actor_from_class(unreal.DecalActor, unreal.Vector((x0 + x1) / 2, (y0 + y1) / 2, z), unreal.Rotator(0.0, -90.0, 0.0))
    d.set_actor_label(label)
    d.set_folder_path(folder)
    dc = d.get_component_by_class(unreal.DecalComponent)
    dc.set_decal_material(floor_paint_material())
    # Pointing down: the decal's X is down, its Y the world's Y, its Z the world's X (half sizes).
    try_set(dc, "decal_size", unreal.Vector(2.0, (y1 - y0) * 0.5, (x1 - x0) * 0.5))
    try_set(dc, "fade_screen_size", 0.0)   # thin lines would fade out across the shed otherwise
    return d


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
            floor_paint("BayLine_%d_%d" % (i, side > 0), lx - 3, lx + 3, BAY_Y + 40 - 155, BAY_Y + 40 + 155, "Bays")
        label_text("BayNumber_%d" % (i + 1), "BAY %d" % (i + 1), (x, BAY_Y + 175, 0.15), 90.0, 40.0, (242, 204, 51), "Bays", pitch=90.0)
    floor_paint("Walkway", -hx + 150, hx - 150, -143, -137, "Bays")


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
    # The radio's shelf over the bench's back end, and the crew board on the wall above.
    ry = BENCH_Y0 + 60 - 25
    box("Radio_Shelf", -HALF_X, -HALF_X + 34, ry - 30, ry + 30, 158.5, 161.5, m["wood"], folder="Bench", collide=False)
    # (The radio on it is ATorqueShedRadio: it plays, its dial glows, the song shows over it.)
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
    room("PaintShop", 680.0, 1400.0, -520.0, 520.0, m, open_sides=("w", "e"))
    # Its east wall, shared with the paint mixing bay, with the bay's door in it (paint_bay.gd DOOR_Z).
    wall("PaintShop_WallE", "y", 1400.0, 1430.0, -520.0 - WALL, 520.0 + WALL, ROOM_HEIGHT,
         [(MIXBAY_DOOR_Y - MIXBAY_DOOR_HALF, MIXBAY_DOOR_Y + MIXBAY_DOOR_HALF, 0.0, MIXBAY_DOOR_TOP)], m["cladding"])
    room("Dyno", -450.0, 450.0, -1300.0, -580.0, m, open_sides=("s",))


# --- The dyno room (dyno_room.gd): the rolling road behind the shed's back wall -----------------------
# Torque Shed's room frame (Godot metres: x right, y up, z towards the shed) to the shed's (cm): the
# room's back wall z 59.2 is this shed's back wall, y -580.
DYNO_RIG = (0.0, -940.0)             # CENTER (0, 0, 55.6): the rig, the bike on it facing the far wall
DYNO_DESK = (290.0, -1110.0)         # DESK (2.9, 0, 53.9)


def g2ue(x, y, z):
    """A point in Torque Shed's dyno room frame (metres) as the shed's (cm)."""
    return (x * 100.0, (z - 59.2) * 100.0 - 580.0, y * 100.0)


def gbox(label, centre, size, mat, folder="Dyno", **kw):
    """dyno_room.gd's _deco and _solid: a box at a room-frame centre with a room-frame size (metres)."""
    c = g2ue(*centre)
    return shape(CUBE, label, c, (size[0] * 100.0, size[2] * 100.0, size[1] * 100.0), mat, folder=folder, **kw)


def gpaint(label, centre, size, folder="Dyno"):
    """A painted floor line in the dyno room: centre (x, z) and size (x, z) in the room frame (metres)."""
    c = g2ue(centre[0], 0.0, centre[1])
    hx, hy = size[0] * 50.0, size[1] * 50.0
    return floor_paint(label, c[0] - hx, c[0] + hx, c[1] - hy, c[1] + hy, folder, z=c[2])


def gcyl(label, centre, radius, height, mat, axis="y", folder="Dyno", **kw):
    """A cylinder (radius, height in metres) along the room frame's y (up) or z (towards the shed)."""
    c = g2ue(*centre)
    rot = (0.0, 0.0, 0.0) if axis == "y" else (90.0, 0.0, 0.0)
    return shape(CYLINDER, label, c, (radius * 200.0, radius * 200.0, height * 100.0), mat, rot=rot, folder=folder, **kw)


def dyno_room(m):
    cx, cz = 0.0, 55.6
    hz = 3.6
    # The rolling road (_build_rig): a steel plate flush with the floor, hazard edging, the slot the
    # drum shows through under the rear tyre (the drum turns: the game's), the front wheel clamp.
    gbox("Dyno_Plate", (cx, 0.007, cz - 0.05), (1.3, 0.014, 3.3), m["steel"], collide=False)
    for i, side in enumerate((-1.0, 1.0)):
        gbox("Dyno_Edge_%d" % i, (cx + side * 0.7, 0.009, cz - 0.05), (0.08, 0.018, 3.4), m["yellow"], collide=False)
    gbox("Dyno_Slot", (cx, 0.016, cz + 0.71), (0.6, 0.004, 0.62), m["dark"], collide=False)
    clamp_z = cz - 0.72
    for i, side in enumerate((-1.0, 1.0)):
        gbox("Dyno_Clamp_%d" % i, (cx + side * 0.1, 0.2, clamp_z - 0.08), (0.03, 0.36, 0.6), m["blue"], collide=False)
    gbox("Dyno_ClampBase", (cx, 0.03, clamp_z - 0.08), (0.26, 0.04, 0.62), m["blue"], collide=False)
    gbox("Dyno_ClampStop", (cx, 0.2, clamp_z - 0.42), (0.26, 0.4, 0.04), m["blue"], collide=False, rot=(-20.0, 0.0, 0.0))
    # The four strap posts (the straps are the game's, drawn once done up).
    for i, (px, pz) in enumerate(((-0.95, -0.6), (0.95, -0.6), (-0.95, 0.8), (0.95, 0.8))):
        gcyl("Dyno_Post_%d" % i, (cx + px, 0.22, cz + pz), 0.045, 0.44, m["yellow"])
        gcyl("Dyno_PostCap_%d" % i, (cx + px, 0.46, cz + pz), 0.06, 0.05, m["dark"], collide=False)
    # The cooling fan in front of the bike (_build_fan): its base, post and housing (the blades turn: the game's).
    fz = cz - 1.95
    gbox("Dyno_FanBase", (cx, 0.18, fz), (0.7, 0.36, 0.45), m["dark"])
    gbox("Dyno_FanPost", (cx, 0.55, fz), (0.12, 0.4, 0.12), m["steel"], collide=False)
    gcyl("Dyno_FanHousing", (cx, 0.95, fz), 0.48, 0.3, m["blue"], axis="z", collide=False)
    gcyl("Dyno_FanBack", (cx, 0.95, fz - 0.02), 0.44, 0.3, m["dark"], axis="z", collide=False)
    gbox("Dyno_FanGrilleH", (cx, 0.95, fz + 0.17), (0.92, 0.015, 0.015), m["steel"], collide=False)
    gbox("Dyno_FanGrilleV", (cx, 0.95, fz + 0.17), (0.015, 0.92, 0.015), m["steel"], collide=False)
    # The dyno desk (_build_desk): a console with its monitor and the big red run button.
    dx, dz = 2.9, 53.9
    gbox("Dyno_Desk", (dx, 0.4, dz), (1.5, 0.8, 0.7), m["dark"])
    gbox("Dyno_DeskTop", (dx, 0.82, dz), (1.55, 0.04, 0.75), m["wood"], collide=False)
    gbox("Dyno_DeskMonitor", (dx, 1.17, dz - 0.18), (0.9, 0.52, 0.05), m["dark"], collide=False)
    gbox("Dyno_DeskGlass", (dx, 1.17, dz - 0.15), (0.84, 0.46, 0.01), m["screen"], collide=False)
    gbox("Dyno_DeskKeyboard", (dx - 0.1, 0.85, dz + 0.12), (0.5, 0.02, 0.16), m["dark"], collide=False)
    gbox("Dyno_RunButton", (dx + 0.45, 0.86, dz + 0.12), (0.18, 0.06, 0.12), m["red"], collide=False)
    label_text("Dyno_DeskTitle", "DYNO & TUNING", g2ue(dx, 1.30, dz - 0.14), 90.0, 6.0, (240, 160, 60), "Dyno")
    # The big screen's frame on the far wall (_build_screen; the page is the game's).
    gbox("Dyno_ScreenFrame", (cx, 2.25, cz - hz + 0.06), (3.3, 1.9, 0.05), m["dark"], collide=False)
    # Hazard lines round the rig, the walkway mat by the desk, the sign over the door (_build_decor).
    for i, side in enumerate((-1.0, 1.0)):
        gpaint("Dyno_Hazard_%d" % i, (cx + side * 1.45, cz - 0.3), (0.06, 4.2))
    gpaint("Dyno_HazardFront", (cx, cz - 2.4), (2.9, 0.06))
    gbox("Dyno_DeskMat", (dx, 0.006, dz + 0.85), (1.6, 0.012, 0.9), m["dark"], collide=False)
    label_text("Dyno_DoorSign", "DYNO ROOM", (0.0, -HALF_Y + 3.0, DYNO_DOOR_TOP + 45.0), 90.0, 22.0, (40, 32, 26), "Dyno")
    # The light and fan switches by the door (_build_switches): the rockers are the game's to light.
    sw = (-1.3, 1.35, 59.17)
    gbox("Dyno_SwitchPlate", (sw[0] - 0.17, sw[1], sw[2]), (0.52, 0.26, 0.03), m["yellow"], collide=False)
    for i, name in enumerate(("LIGHTS", "FAN")):
        x = sw[0] - 0.05 - i * 0.24
        gbox("Dyno_Switch_%d" % i, (x, sw[1], sw[2] - 0.02), (0.12, 0.16, 0.02), m["wall_in"], collide=False)
        label_text("Dyno_SwitchLabel_%d" % i, name, g2ue(x, sw[1] + 0.17, sw[2] - 0.035), -90.0, 4.0, (40, 32, 26), "Dyno")


def deliveries(m):
    # The deliveries bay on the yard east of the door (Shed.DELIVERY_AREA): painted lines, the
    # floor's front edge, DELIVERIES on the ground.
    x0, x1, y0, y1 = 290.0, 1070.0, 750.0, 1250.0
    for i, (a0, a1, b0, b1) in enumerate(((x0, x0 + 8, y0, y1), (x1 - 8, x1, y0, y1), (x0, x1, y0, y0 + 8), (x0, x1, y1 - 8, y1),
                                          (x0, x1, 1065 - 3, 1065 + 3))):
        floor_paint("Deliveries_Line_%d" % i, a0, a1, b0, b1, "Outside")
    label_text("Deliveries_Mark", "DELIVERIES", ((x0 + x1) / 2, y0 - 45, 0.15), 90.0, 50.0, (217, 178, 51), "Outside", pitch=90.0)


# --- The paint shop (workshop.gd _build_booth_room) and its spray bay (equipment "booth_shop") -----------
# The room: interior x 680 .. 1400, y -520 .. 520 (Torque Shed x 6.8 .. 14.0, z 59.8 .. 70.2), through
# the doorway in the shed's east wall at y 350. The downdraft spray bay (BOOTH_XF: 8 m along y, 4.7 m
# deep, 2.7 m high) stands against the east wall, its open front at x 925 facing the walkway, open
# topped under the room's hexagon lights, with two rails of five hooks (TorqueShedLayout::HookAt:
# x 1270 and 1050, y -300 .. 300 every 150, the hook's tip 138 cm up).
PAINT_X0, PAINT_X1, PAINT_Y0, PAINT_Y1 = 680.0, 1400.0, -520.0, 520.0
BOOTH_X0, BOOTH_X1, BOOTH_HALF_Y, BOOTH_H = 925.0, 1395.0, 400.0, 270.0
BOOTH_RAIL_Z = 235.0
HOOK_X, HOOK_Y, HOOK_TIP = (1270.0, 1050.0), (-300.0, -150.0, 0.0, 150.0, 300.0), 138.0


def paint_shop(m):
    import math
    f = "PaintShop"
    h = ROOM_HEIGHT
    # White inside the cladding, the doorway from the shed left open; a dark ceiling.
    box("Paint_LiningN", PAINT_X0, PAINT_X1, PAINT_Y0, PAINT_Y0 + 1, 0, h, m["paint_wall"], folder=f, collide=False)
    box("Paint_LiningS", PAINT_X0, PAINT_X1, PAINT_Y1 - 1, PAINT_Y1, 0, h, m["paint_wall"], folder=f, collide=False)
    e0, e1 = MIXBAY_DOOR_Y - MIXBAY_DOOR_HALF, MIXBAY_DOOR_Y + MIXBAY_DOOR_HALF
    box("Paint_LiningE_0", PAINT_X1 - 1, PAINT_X1, PAINT_Y0, e0, 0, h, m["paint_wall"], folder=f, collide=False)
    box("Paint_LiningE_1", PAINT_X1 - 1, PAINT_X1, e1, PAINT_Y1, 0, h, m["paint_wall"], folder=f, collide=False)
    box("Paint_LiningE_Top", PAINT_X1 - 1, PAINT_X1, e0, e1, MIXBAY_DOOR_TOP, h, m["paint_wall"], folder=f, collide=False)
    d0, d1 = PAINT_DOOR_Y - SIDE_DOOR_HALF, PAINT_DOOR_Y + SIDE_DOOR_HALF
    box("Paint_LiningW_0", PAINT_X0, PAINT_X0 + 1, PAINT_Y0, d0, 0, h, m["paint_wall"], folder=f, collide=False)
    box("Paint_LiningW_1", PAINT_X0, PAINT_X0 + 1, d1, PAINT_Y1, 0, h, m["paint_wall"], folder=f, collide=False)
    box("Paint_LiningW_Top", PAINT_X0, PAINT_X0 + 1, d0, d1, SIDE_DOOR_TOP, h, m["paint_wall"], folder=f, collide=False)
    box("Paint_Ceiling", PAINT_X0, PAINT_X1, PAINT_Y0, PAINT_Y1, h - 1, h, m["paint_ceiling"], folder=f, collide=False)
    label_text("Paint_Poster", "SPRAY GUN: TOOL 7", (PAINT_X0 + 2, PAINT_DOOR_Y - SIDE_DOOR_HALF - 120, 160.0), 0.0, 16.0, (30, 30, 36), f)
    # The honeycomb of hexagon LED tubes across the ceiling (workshop.gd _hex_ceiling), each shared
    # edge once, in a frame round the lot.
    r = 55.0
    ax0, ay0, aw, ah = PAINT_X0 + 35, PAINT_Y0 + 35, PAINT_X1 - PAINT_X0 - 70, PAINT_Y1 - PAINT_Y0 - 70
    row_h = math.sqrt(3.0) * r
    cols = int(math.floor((aw - 2.0 * r) / (1.5 * r))) + 1
    rows = int(math.floor((ah - row_h) / row_h)) + 1
    span_x = (cols - 1) * 1.5 * r + 2.0 * r
    span_y = rows * row_h + (row_h * 0.5 if cols > 1 else 0.0)
    ox, oy = ax0 + (aw - span_x) * 0.5 + r, ay0 + (ah - span_y) * 0.5 + row_h * 0.5
    edges = {}
    for col in range(cols):
        for row in range(rows):
            cx = ox + col * 1.5 * r
            cy = oy + row * row_h + (row_h * 0.5 if col % 2 == 1 else 0.0)
            for k in range(6):
                a = (cx + r * math.cos(k * math.pi / 3.0), cy + r * math.sin(k * math.pi / 3.0))
                b = (cx + r * math.cos((k + 1) * math.pi / 3.0), cy + r * math.sin((k + 1) * math.pi / 3.0))
                edges[(round((a[0] + b[0]) * 0.5), round((a[1] + b[1]) * 0.5))] = (a, b)
    lo = (ox - r - 12.0, oy - row_h * 0.5 - 12.0)
    hi = (lo[0] + span_x + 24.0, lo[1] + span_y + 24.0)
    segs = list(edges.values()) + [(lo, (hi[0], lo[1])), ((hi[0], lo[1]), hi), (hi, (lo[0], hi[1])), ((lo[0], hi[1]), lo)]
    for i, (a, b) in enumerate(segs):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) * 0.94
        yaw = math.degrees(math.atan2(dy, dx))
        shape(CUBE, "Paint_Hex_%d" % i, ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5, h - 4.0), (length, 3.5, 2.5), m["hex_led"],
              rot=(0.0, 0.0, yaw), folder=f + "/HexLights", collide=False)
    # Real light under the tiles: bright clean white from above (the four spots).
    for i, (px, py) in enumerate(((-180.0, -260.0), (120.0, -260.0), (-180.0, 260.0), (120.0, 260.0))):
        light = actors.spawn_actor_from_class(unreal.SpotLight, unreal.Vector(1040.0 + px, py, h - 12.0), unreal.Rotator(0.0, -90.0, 0.0))
        light.set_actor_label("Paint_Spot_%d" % i)
        light.set_folder_path(f)
        lc = light.get_component_by_class(unreal.SpotLightComponent)
        lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        units = getattr(unreal, "LightUnits", None)
        if units is not None:
            try_set(lc, "intensity_units", units.LUMENS)
        for k, v in (("intensity", 5200.0), ("outer_cone_angle", 75.0), ("inner_cone_angle", 35.0), ("attenuation_radius", 650.0),
                     ("temperature", 6200.0), ("use_temperature", True), ("contact_shadow_length", 0.05)):
            try_set(lc, k, v)
    # The spray bay (equipment_view.gd _build_booth): back and side panels, steel posts at the open
    # front, the filter wall at the back with its extractor grille, the downdraft grate.
    t = 5.0
    box("Booth_Back", BOOTH_X1 - t, BOOTH_X1, -BOOTH_HALF_Y, BOOTH_HALF_Y, 0, BOOTH_H, m["booth_panel"], folder=f + "/Booth")
    for i, side in enumerate((-1.0, 1.0)):
        y0, y1 = sorted((side * BOOTH_HALF_Y, side * (BOOTH_HALF_Y - t)))
        box("Booth_Side_%d" % i, BOOTH_X0, BOOTH_X1, y0, y1, 0, BOOTH_H, m["booth_panel"], folder=f + "/Booth")
        box("Booth_Post_%d" % i, BOOTH_X0 - 3, BOOTH_X0 + 3, y0 - 2, y1 + 2, 0, BOOTH_H, m["steel"], folder=f + "/Booth", collide=False)
    fx = BOOTH_X1 - t - 2.0
    box("Booth_Filter", fx - 3, fx, -320.0, 320.0, 121.5 - 81.0, 121.5 + 81.0, m["booth_filter"], folder=f + "/Booth", collide=False)
    for i in range(6):
        z = 54.0 + i * 27.0
        box("Booth_Grille_%d" % i, fx - 4, fx - 3, -320.0, 320.0, z - 0.6, z + 0.6, m["steel"], folder=f + "/Booth", collide=False)
    box("Booth_Grate", BOOTH_X0 + 5, BOOTH_X1 - 5, -BOOTH_HALF_Y + 5, BOOTH_HALF_Y - 5, 0, 4, m["booth_grate"], folder=f + "/Booth", collide=False)
    for i in range(12):
        x = 1371.5 - i * 38.45
        box("Booth_GrateBar_%d" % i, x - 0.75, x + 0.75, -390.0, 390.0, 4, 5, m["steel"], folder=f + "/Booth", collide=False)
    label_text("Booth_Name", "DOWNDRAFT SPRAY BAY", (BOOTH_X1 - t - 1.0, 0.0, BOOTH_H - 30.0), 180.0, 18.0, (31, 31, 36), f + "/Booth")
    # The hook rails, hung from the ceiling on drop rods, and a hanger for each place's hook.
    for i, rx in enumerate(HOOK_X):
        box("Booth_Rail_%d" % i, rx - 2, rx + 2, -390.0, 390.0, BOOTH_RAIL_Z - 2, BOOTH_RAIL_Z + 2, m["chrome"], folder=f + "/Booth", collide=False)
        for j, side in enumerate((-1.0, 1.0)):
            shape(CYLINDER, "Booth_Rod_%d_%d" % (i, j), (rx, side * 370.0, (BOOTH_RAIL_Z + h) * 0.5), (2.4, 2.4, h - BOOTH_RAIL_Z),
                  m["chrome"], folder=f + "/Booth", collide=False)
        for j, hy in enumerate(HOOK_Y):
            n = i * 5 + j
            box("Booth_Hanger_%d" % n, rx - 3, rx + 3, hy - 2.5, hy + 2.5, BOOTH_RAIL_Z - 3.5, BOOTH_RAIL_Z - 2, m["dark"], folder=f + "/Booth", collide=False)
            # (The wire and its hook are ATorqueBoothHooks': they're raised, lowered and swing.)


def paint_bay(m):
    """The paint mixing bay (paint_bay.gd _build): the mixing bench down the east wall with the scale
    and the shaker on it and the tint rack over it, the tin shelf beside it, the swatch wall on the
    shared wall, the spray-out stand in the daylight booth. ATorquePaintBayRoom draws the tins, the
    bottles, the cards and the scale's reading."""
    f = "PaintBay"
    h = ROOM_HEIGHT
    x0, x1, y0, y1 = MIXBAY_X0, MIXBAY_X1, MIXBAY_Y0, MIXBAY_Y1
    room("PaintBay", x0, x1, y0, y1, m, open_sides=("w",))   # the paint shop's east wall closes it
    # White inside, the door left open; the ceiling dark like the paint shop's.
    d0, d1 = MIXBAY_DOOR_Y - MIXBAY_DOOR_HALF, MIXBAY_DOOR_Y + MIXBAY_DOOR_HALF
    box("MixBay_LiningN", x0, x1, y0, y0 + 1, 0, h, m["paint_wall"], folder=f, collide=False)
    box("MixBay_LiningS", x0, x1, y1 - 1, y1, 0, h, m["paint_wall"], folder=f, collide=False)
    box("MixBay_LiningE", x1 - 1, x1, y0, y1, 0, h, m["paint_wall"], folder=f, collide=False)
    box("MixBay_LiningW_0", x0, x0 + 1, y0, d0, 0, h, m["paint_wall"], folder=f, collide=False)
    box("MixBay_LiningW_1", x0, x0 + 1, d1, y1, 0, h, m["paint_wall"], folder=f, collide=False)
    box("MixBay_LiningW_Top", x0, x0 + 1, d0, d1, MIXBAY_DOOR_TOP, h, m["paint_wall"], folder=f, collide=False)
    box("MixBay_Ceiling", x0, x1, y0, y1, h - 1, h, m["paint_ceiling"], folder=f, collide=False)
    label_text("MixBay_Name", "PAINT MIXING", (x0 + 2.0, 300.0, 285.0), 0.0, 20.0, (30, 30, 36), f)
    # The mixing bench (BENCH, BENCH_TOP): x 1635 .. 1700, y -220 .. 180, a steel top at 92.
    box("MixBay_Bench", 1638.0, x1, -218.0, 178.0, 0, 86, m["wall_low"], folder=f)
    box("MixBay_BenchTop", 1635.0, x1, -220.0, 180.0, 86, 92, m["steel"], folder=f)
    # The scale (SCALE_AT): its base, a chrome pan, the read-out on the front (the room draws the grams).
    box("MixBay_ScaleBase", 1647.0, 1677.0, -55.0, -25.0, 92, 94, m["dark"], folder=f, collide=False)
    shape(CYLINDER, "MixBay_ScalePan", (1662.0, -40.0, 94.5), (18.0, 18.0, 1.0), m["chrome"], folder=f, collide=False)
    # (The read-out hangs on the bench's front edge under the pan, big enough to read from the bench.)
    box("MixBay_ScaleDisplay", 1632.0, 1635.0, -64.0, -16.0, 77, 92, m["dark"], folder=f, collide=False)
    # The shaker (SHAKER_AT): a base plate the tin sits on at 100 and a cage round it.
    box("MixBay_ShakerBase", 1645.0, 1680.0, 105.0, 145.0, 92, 100, m["dark"], folder=f, collide=False)
    for i, (sx, sy) in enumerate(((1646.0, 106.0), (1679.0, 106.0), (1646.0, 144.0), (1679.0, 144.0))):
        box("MixBay_ShakerPost_%d" % i, sx - 1, sx + 1, sy - 1, sy + 1, 100, 124, m["steel"], folder=f, collide=False)
    box("MixBay_ShakerTop", 1645.0, 1680.0, 105.0, 145.0, 124, 126, m["dark"], folder=f, collide=False)
    label_text("MixBay_ShakerName", "SHAKER", (1644.0, 125.0, 96.0), 180.0, 4.0, (220, 220, 220), f)
    # The tint rack over the bench (RACK_X, RACK_Y): two steel shelves of eight bottles, 25 cm apart.
    for i, z in enumerate((132.0, 172.0)):
        box("MixBay_RackShelf_%d" % i, 1674.0, x1, -222.0, -12.0, z - 3, z, m["steel"], folder=f, collide=False)
        box("MixBay_RackLip_%d" % i, 1673.0, 1674.0, -222.0, -12.0, z - 3, z + 2.5, m["steel"], folder=f, collide=False)
    for i, ry in enumerate((-222.0, -12.0)):
        box("MixBay_RackEnd_%d" % i, 1674.0, x1, ry - 1, ry + 1, 126, 196, m["steel"], folder=f, collide=False)
    label_text("MixBay_RackName", "TINTS", (1698.0, -117.0, 205.0), 180.0, 8.0, (30, 30, 36), f)
    # The tin shelf (SHELF_X, SHELF_Y, SHELF_Z): three timber shelves of four tins, uprights each end.
    for i, z in enumerate((45.0, 95.0, 145.0)):
        box("MixBay_TinShelf_%d" % i, 1655.0, x1, 195.0, 325.0, z - 4, z, m["wood"], folder=f)
    for i, sy in enumerate((193.0, 327.0)):
        box("MixBay_TinShelfEnd_%d" % i, 1655.0, x1, sy - 2, sy + 2, 0, 165, m["steel"], folder=f)
    label_text("MixBay_TinShelfName", "TINS", (1653.0, 260.0, 158.0), 180.0, 8.0, (30, 30, 36), f)
    # The swatch wall on the shared wall (SWATCH_X ..): a board for the cards, eight a row, four rows.
    box("MixBay_SwatchBoard", x0 + 1, x0 + 2.5, -405.0, 210.0, 70.0, 262.0, m["wood"], folder=f, collide=False)
    label_text("MixBay_SwatchName", "SWATCHES", (x0 + 3.0, -97.0, 268.0), 0.0, 10.0, (30, 30, 36), f)
    # The daylight booth in the north corner (BOOTH): side panels, a light canopy over the spray-out
    # stand (STAND_AT), the test card in its clip at 140.
    box("MixBay_DaylightSideW", 1458.0, 1460.0, y0, -458.0, 0, 225, m["booth_panel"], folder=f, collide=False)
    box("MixBay_DaylightSideE", 1680.0, 1682.0, y0, -458.0, 0, 225, m["booth_panel"], folder=f, collide=False)
    box("MixBay_DaylightCanopy", 1458.0, 1682.0, y0, -458.0, 222, 226, m["booth_panel"], folder=f, collide=False)
    box("MixBay_DaylightPanel", 1470.0, 1670.0, y0 + 4, -464.0, 220, 222, m["led"], folder=f, collide=False)
    shape(CYLINDER, "MixBay_StandPost", (1500.0, -405.0, 60.0), (4.0, 4.0, 120.0), m["steel"], folder=f, collide=False)
    shape(CYLINDER, "MixBay_StandFoot", (1500.0, -405.0, 1.0), (40.0, 40.0, 2.0), m["steel"], folder=f, collide=False)
    box("MixBay_StandClip", 1497.0, 1503.0, -422.0, -388.0, 117, 121, m["dark"], folder=f, collide=False)
    lights_at = (("MixBay_Daylight", (1570.0, -490.0, 218.0), 6500.0, 2600.0), ("MixBay_Light", (1565.0, 0.0, h - 8.0), 5600.0, 3800.0))
    for name, at, kelvin, lumens in lights_at:
        light = actors.spawn_actor_from_class(unreal.RectLight, unreal.Vector(*at), unreal.Rotator(0.0, -90.0, 0.0))
        light.set_actor_label(name)
        light.set_folder_path(f)
        lc = light.get_component_by_class(unreal.RectLightComponent)
        lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        units = getattr(unreal, "LightUnits", None)
        if units is not None:
            try_set(lc, "intensity_units", units.LUMENS)
        for k, v in (("intensity", lumens), ("source_width", 180.0), ("source_height", 50.0), ("attenuation_radius", 700.0),
                     ("barn_door_angle", 80.0), ("temperature", kelvin), ("use_temperature", True)):
            try_set(lc, k, v)


def lights(m):
    # Workshop.gd _build_lights: a strip light over each bay with a spot under it, and three more
    # strips down the middle and over the door.
    spots = [(x, BAY_Y) for x in BAY_X] + [(-420.0, 150.0), (420.0, 150.0), (0.0, 350.0)]
    for i, (x, y) in enumerate(spots):
        strip = box("Strip_%d" % i, x - 10, x + 10, y - 65, y + 65, HEIGHT - 37.5, HEIGHT - 32.5, m["led"], folder="Lights", collide=False)
        if strip is not None:
            strip.set_editor_property("tags", [unreal.Name("ShedLightStrip")])   # its glow goes off with the lights
        light = actors.spawn_actor_from_class(unreal.RectLight, unreal.Vector(x, y, HEIGHT - 39), unreal.Rotator(0.0, -90.0, 0.0))
        light.set_actor_label("StripLight_%d" % i)
        light.set_folder_path("Lights")
        # The main lights the switch by the roller door works (ATorqueShedKit finds them by tag).
        light.set_editor_property("tags", [unreal.Name("ShedLight")])
        lc = light.get_component_by_class(unreal.RectLightComponent)
        lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        units = getattr(unreal, "LightUnits", None)
        if units is not None:
            try_set(lc, "intensity_units", units.LUMENS)
        for k, v in (("intensity", LED_LUMENS * (1.3 if i < len(BAY_X) else 1.0)), ("source_width", 130.0), ("source_height", 20.0),
                     ("attenuation_radius", 1200.0), ("barn_door_angle", 70.0),
                     ("temperature", STRIP_K), ("use_temperature", True), ("contact_shadow_length", 0.05)):
            try_set(lc, k, v)
    # The wash bay's own strip, and one down the middle of the storage room (it had none).
    strip_light("WashBayLight", (-930.0, 100.0, 290.0), 1400.0, 160.0, 15.0, 700.0, "WashBay")
    box("WashBay_Strip", -937.5, -922.5, 20, 180, 292, 296, m["led"], folder="WashBay", collide=False)
    strip_light("StorageLight", (-1090.0, -690.0, ROOM_HEIGHT - 9.0), 1600.0, 130.0, 20.0, 900.0, "Storage")
    box("Storage_Strip", -1100.0, -1080.0, -755.0, -625.0, ROOM_HEIGHT - 7.5, ROOM_HEIGHT - 2.5, m["led"], folder="Storage", collide=False)


def strip_light(label, at, lumens, width, height, radius, folder, kelvin=None):
    # An LED batten's light, pointing down: a rect light the batten's size, a little contact shadow
    # so things sit on the bench and floor under it.
    light = actors.spawn_actor_from_class(unreal.RectLight, unreal.Vector(*at), unreal.Rotator(0.0, -90.0, 0.0))
    light.set_actor_label(label)
    light.set_folder_path(folder)
    lc = light.get_component_by_class(unreal.RectLightComponent)
    lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    units = getattr(unreal, "LightUnits", None)
    if units is not None:
        try_set(lc, "intensity_units", units.LUMENS)
    for k, v in (("intensity", lumens), ("source_width", width), ("source_height", height),
                 ("attenuation_radius", radius), ("barn_door_angle", 70.0),
                 ("temperature", kelvin or STRIP_K), ("use_temperature", True), ("contact_shadow_length", 0.05)):
        try_set(lc, k, v)
    return lc


def outside(m):
    # Concrete apron in front of the door, the lane off it to the south, the field all round.
    # (The yard reaches past the paint mixing bay, x 1430 .. 1700, its east wall clear of it.)
    box("Forecourt", -1200, 1840, HALF_Y + WALL, HALF_Y + WALL + 900, -10, 0, m["concrete"], folder="Outside")
    box("Yard", -1600, 1840, -1400, HALF_Y + WALL, -11, -1, m["concrete"], folder="Outside")
    box("Lane", -200, 200, HALF_Y + WALL + 900, 30000, -12, -2, m["tarmac"], folder="Outside")
    box("Field", -40000, 40000, -40000, 40000, -30, -8, m["grass"], folder="Outside")
    # Drystone walls round the yard and along the lane.
    def dry(label, x0, x1, y0, y1):
        box(label, x0, x1, y0, y1, -8, 130, m["drystone"], folder="Outside")
    dry("Wall_YardW", -1700, -1640, -1500, HALF_Y + WALL + 900)
    dry("Wall_YardE", 1840, 1900, -1500, HALF_Y + WALL + 900)
    dry("Wall_Back", -1700, 1900, -1560, -1500)
    dry("Wall_LaneW", -280, -220, HALF_Y + WALL + 900, 12000)
    dry("Wall_LaneE", 220, 280, HALF_Y + WALL + 900, 12000)
    dry("Wall_Field1", -9000, -230, 4000, 4060)
    dry("Wall_Field2", 330, 9000, 6500, 6560)
    # Far fells: low domes 8 to 20 km off, hazed blue by the sky's aerial perspective.
    for i, (x, y, w, d, h) in enumerate(((-14000, 9000, 26000, 9000, 2600), (9000, 15000, 30000, 12000, 3400),
                                         (20000, -6000, 14000, 30000, 2400), (-6000, -16000, 34000, 10000, 3000),
                                         (-20000, -2000, 12000, 26000, 2200))):
        shape(SPHERE, "Fell_%d" % i, (x, y, -h * 0.25), (w, d, h * 2.0), m["hill"], folder="Outside", collide=False)


def foot_ao():
    """Ambient occlusion where things meet the floor: Lumen's GI leaves the foot of a wall or a
    cabinet as bright as open floor, so they look pasted down. A soft dark decal round each
    footprint (15 cm out from it, darkest at its edge), projected down onto the floor."""
    if FOOT_AO <= 0.0 or not FOOTPRINTS:
        return
    mat = new_material("M_ShedFootAO")
    # Blend first, then the domain: a decal can't be Opaque (the default), and setting the domain
    # on an opaque material fails to compile ("DeferredDecal ... only ... Translucent").
    try_set(mat, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    try_set(mat, "material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    uv = expr(mat, unreal.MaterialExpressionTextureCoordinate, -1200, 0)
    centred = expr(mat, unreal.MaterialExpressionConstantBiasScale, -1000, 0, bias=-0.5, scale=2.0)
    mel.connect_material_expressions(uv, "", centred, "")
    ab = expr(mat, unreal.MaterialExpressionAbs, -850, 0)
    mel.connect_material_expressions(centred, "", ab, "")
    pw = expr(mat, unreal.MaterialExpressionPower, -700, 0, const_exponent=3.0)
    mel.connect_material_expressions(ab, "", pw, "Base")
    inv = expr(mat, unreal.MaterialExpressionOneMinus, -550, 0)
    mel.connect_material_expressions(pw, "", inv, "")
    mu = expr(mat, unreal.MaterialExpressionComponentMask, -400, -60, r=True, g=False, b=False, a=False)
    mv = expr(mat, unreal.MaterialExpressionComponentMask, -400, 60, r=False, g=True, b=False, a=False)
    mel.connect_material_expressions(inv, "", mu, "")
    mel.connect_material_expressions(inv, "", mv, "")
    both = expr(mat, unreal.MaterialExpressionMultiply, -250, 0)
    mel.connect_material_expressions(mu, "", both, "A")
    mel.connect_material_expressions(mv, "", both, "B")
    strength = expr(mat, unreal.MaterialExpressionMultiply, -100, 0, const_b=FOOT_AO)
    mel.connect_material_expressions(both, "", strength, "A")
    mel.connect_material_property(strength, "", unreal.MaterialProperty.MP_OPACITY)
    black = expr(mat, unreal.MaterialExpressionConstant3Vector, -100, -200, constant=unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
    mel.connect_material_property(black, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mat = finish(mat)
    reach = 15.0
    n = 0
    for label, x, y, sx, sy in FOOTPRINTS[:500]:
        d = actors.spawn_actor_from_class(unreal.DecalActor, unreal.Vector(x, y, 0.0), unreal.Rotator(0.0, -90.0, 0.0))
        d.set_actor_label("FootAO_" + label)
        d.set_folder_path("Lights/FootAO")
        dc = d.get_component_by_class(unreal.DecalComponent)
        dc.set_decal_material(mat)
        # Pointing down: the decal's X is down, its Y the world's Y, its Z the world's X (half sizes).
        try_set(dc, "decal_size", unreal.Vector(12.0, sy * 0.5 + reach, sx * 0.5 + reach))
        n += 1
    log("foot AO: %d decals" % n)


def hall_dust():
    """The dust in the hall's air, for the sun through the roller door to show in: a local fog
    volume over the shed, its fog lying low (volumetric fog draws it lit by the sun and the strips)."""
    cls = getattr(unreal, "LocalFogVolume", None)
    if LOCAL_FOG <= 0.0 or cls is None:
        log("no hall dust (LOCAL_FOG %.3f, LocalFogVolume %s)" % (LOCAL_FOG, cls is not None))
        return
    a = actors.spawn_actor_from_class(cls, unreal.Vector(0.0, 0.0, 200.0), unreal.Rotator(0, 0, 0))
    a.set_actor_label("HallDust")
    a.set_folder_path("Lights")
    # Its sphere sized to the hall (whatever the engine's unit size is): about 8 m across the middle.
    origin, extent = a.get_actor_bounds(False)
    unit = max(extent.x, 1.0)
    a.set_actor_scale3d(unreal.Vector(820.0 / unit, 820.0 / unit, 820.0 / unit))
    fc = a.get_component_by_class(getattr(unreal, "LocalFogVolumeComponent"))
    if fc:
        for k, v in (("radial_fog_extinction", 0.0), ("height_fog_extinction", LOCAL_FOG), ("height_fog_falloff", 250.0),
                     ("height_fog_offset", 0.0), ("fog_phase_g", 0.6), ("fog_albedo", unreal.LinearColor(0.92, 0.9, 0.86, 1.0))):
            try_set(fc, k, v)
    origin, extent = a.get_actor_bounds(False)
    log("hall dust: local fog unit %.0f cm, scaled to 820 (now %.0f x %.0f x %.0f cm), extinction %.2f"
        % (unit, extent.x * 2, extent.y * 2, extent.z * 2, LOCAL_FOG))


def sky():
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 50000), unreal.Rotator(0.0, -30.0, -45.0))
    sun.set_actor_label("Sun")
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    for k, v in (("atmosphere_sun_light", True), ("intensity", SUN_LUX), ("light_source_angle", 0.53),
                 ("light_color", unreal.Color(255, 255, 255, 255)), ("use_temperature", True), ("temperature", SUN_K),
                 ("volumetric_scattering_intensity", SUN_SCATTER)):
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
    try_set(skc, "light_color", unreal.Color(232, 242, 255, 255))   # the sky's fill a touch cool against the warm sun
    fog = actors.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, -500), unreal.Rotator(0, 0, 0))
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    for k, v in (("fog_density", 0.0018), ("fog_height_falloff", 0.15), ("start_distance", 1500.0),
                 ("fog_inscattering_luminance", unreal.LinearColor(0.45, 0.46, 0.47, 1.0))):
        try_set(fc, k, v)
    if VOLUMETRIC_FOG:
        # Dust in the air of a working shed: the sun through the door and the strips over the bays
        # pick it out a little. Start at the eye (the height fog's 1500 cm start is for the haze outside).
        # (bEnableVolumetricFog: "enable_volumetric_fog" to Python, and SetVolumetricFog too, to be sure.)
        try:
            fc.set_volumetric_fog(True)
        except Exception as e:
            unreal.log_warning("[Torque] no set_volumetric_fog: %s" % e)
        for k, v in (("enable_volumetric_fog", True), ("volumetric_fog_scattering_distribution", 0.6),
                     ("volumetric_fog_albedo", unreal.Color(235, 230, 220, 255)), ("volumetric_fog_extinction_scale", 1.0),
                     ("volumetric_fog_start_distance", 0.0), ("volumetric_fog_distance", 6000.0)):
            try_set(fc, k, v)
        try:
            log("volumetric fog on: %s" % fc.get_editor_property("enable_volumetric_fog"))
        except Exception as e:
            unreal.log_warning("[Torque] can't read enable_volumetric_fog back: %s" % e)
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
                 ("bloom_intensity", 0.35), ("vignette_intensity", 0.25), ("film_grain_intensity", 0.06),
                 ("white_temp", 6300.0), ("color_saturation", unreal.Vector4(1.08, 1.08, 1.08, 1.0)),
                 # Finer GI: the sunlit floor patch bouncing warm onto the walls, the junctions occluded.
                 ("lumen_final_gather_quality", 4.0), ("lumen_scene_lighting_quality", 3.0),
                 ("lumen_scene_detail", 4.0), ("lumen_reflection_quality", 2.0),
                 ("lumen_final_gather_lighting_update_speed", 2.0), ("indirect_lighting_intensity", 1.25),
                 ("lumen_skylight_leaking", SKY_LEAK),
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
    dyno_room(m)
    paint_shop(m)
    paint_bay(m)
    deliveries(m)
    lights(m)
    outside(m)
    foot_ao()
    hall_dust()
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

    # The real models over the grey boxes (Tools/ue_import_props.py, Data/Props/swaps.json): the
    # boxes stay as the colliders. Without imported props the boxes simply stay showing.
    sys.path.insert(0, os.path.join(os.path.abspath(unreal.Paths.project_dir()), "Tools"))
    import ue_import_props
    ue_import_props.dress()

    save_fresh_level(MAP, before)
    # Say what the saved level holds for the game's own lookups (tags), so a run's log shows it.
    for a in actors.get_all_level_actors():
        tags = [str(t) for t in a.get_editor_property("tags")]
        if "MenuView" in tags or "RideOut" in tags:
            log("level has %s (%s) at %s, rotation %s" % (a.get_actor_label(), ",".join(tags), a.get_actor_location(), a.get_actor_rotation()))
    log("built %s: Torque Shed's shed, %d bays" % (MAP, len(BAY_X)))


build()
