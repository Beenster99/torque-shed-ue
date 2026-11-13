"""Build the level the real world's roads are ridden in (Ride UK stage 2): /Game/Maps/RideUK.

Run inside the Unreal editor, after Tools/ue_build_level.py has built the Buttertubs ride (it borrows
that script's sun, sky, air and camera, and the ride's cloud shadows as they are):

    UnrealEditor-Cmd.exe TorqueShedProto.uproject -ExecutePythonScript=Tools/ue_build_rideuk.py -unattended

The level has no ground of its own: only the sky, the lights and a player start. The game lays the
ground and the road out round the rider as they go (World/TorqueRideWorld), from the ride the world
subsystem planned, and draws them with the materials made here in /Game/RideUK/Materials: the plain
M_RideLand (moor grass, mottled) and M_RideRoad (the road's vertex colours: tarmac, verge, paint, and
hedges, roofs and stand-in trees), and Buttertubs' own scanned ground and stone made again under
names of their own (M_RideLandScan, M_RideStone, M_RideMasonry; the game uses them when they are here).
Its grass and trees are Buttertubs' own too: the plant kit (place_kit) copies the Buttertubs level's
species, so run this after ue_build_level.py has built that level.
"""
import json
import os
import sys

import unreal

MAP = "/Game/Maps/RideUK"
MAT_DIR = "/Game/RideUK/Materials"


def log(msg):
    unreal.log("[Torque] ride UK: " + msg)


def level_lib():
    """Tools/ue_build_level.py as a module, without it building the Buttertubs ride."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    sys.torque_build_level_as_library = True
    try:
        import importlib
        if "ue_build_level" in sys.modules:
            L = importlib.reload(sys.modules["ue_build_level"])
        else:
            import ue_build_level as L
    finally:
        sys.torque_build_level_as_library = False
    return L


def land_material(L):
    """The ground's own colour from its vertices (what covers it: World/WorldRide's LandColour), darker
    and paler in broad patches, mottled close up, matte."""
    mel = L.mel
    m = L.new_material("M_RideLand", MAT_DIR)
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -900, 0)
    patch = L.noise(m, 0.0006, 0.72, 1.25, -900, 400)
    mix = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -600, 100)
    mel.connect_material_expressions(vc, "", mix, "A")   # the unnamed output is RGB
    mel.connect_material_expressions(patch, "", mix, "B")
    mid = L.noise(m, 0.01, 0.8, 1.15, -900, 600)
    mul1 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -400, 200)
    mel.connect_material_expressions(mix, "", mul1, "A")
    mel.connect_material_expressions(mid, "", mul1, "B")
    fine = L.noise(m, 0.15, 0.85, 1.1, -900, 800)
    mul2 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 300)
    mel.connect_material_expressions(mul1, "", mul2, "A")
    mel.connect_material_expressions(fine, "", mul2, "B")
    mel.connect_material_property(mul2, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(L.const1(m, 0.95, -250, 500), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(L.const1(m, 0.2, -250, 650), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def road_material(L):
    """The road's own colours from its vertices (tarmac, the verge's gravel, the paint: World/WorldRide's
    RoadPiece), a little uneven in tone."""
    mel = L.mel
    m = L.new_material("M_RideRoad", MAT_DIR)
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -900, 0)
    n = L.noise(m, 0.05, 0.8, 1.2, -900, 200)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -650, 100)
    mel.connect_material_expressions(vc, "", mul, "A")   # the unnamed output is RGB
    mel.connect_material_expressions(n, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(L.const1(m, 0.8, -400, 300), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def road_material_scanned(L, tex, roles):
    """Build 141 (beenster: "the road surface more like the Buttertubs demo"): M_RideRoad in Buttertubs'
    scanned tarmac (M_Road: the scan with its brown taken out to the Dales' light, cool grey, slow patches
    of older and newer tarmac, darker and glossy when wet). The road mesh's vertex colour still says what
    each strip is (World/WorldRide's RoadPiece): only the tarmac's own dark grey takes the scan; the paint, verge, kerbs, bridge stone and tunnel portals keep their colours."""
    g = L.Graph(L.new_material("M_RideRoad", MAT_DIR))
    road = g.layer(tex["asphalt"], roles["asphalt"]["tile_m"])
    flat = g.node(unreal.MaterialExpressionDesaturation, -600)
    g.link(road[0], flat)
    g.link(g.const(0.9), flat, "Fraction")
    road[0] = g.op(unreal.MaterialExpressionMultiply, flat, g.const((0.96, 0.99, 1.06)))
    road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], L.noise(g.m, 0.002, 0.6, 0.95, -1200, g.y + 200))
    g.y += 140
    # Patches of a fresher, darker surface dressing and paler, older tarmac, tens of metres long.
    patch = g.mask_noise(0.0012, -2.0, 3.0)
    road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], g.lerp(g.const(1.08), g.const(0.8), patch))
    # Which strips are tarmac: the vertex colour within a hair of the tarmac's own (0.022, 0.022, 0.025),
    # so nothing else drawn in this material (dark glass, black paint) takes the scan.
    vc = g.node(unreal.MaterialExpressionVertexColor)
    d = g.node(unreal.MaterialExpressionDistance, -600)
    g.link(vc, d, "A")
    g.link(g.const((0.022, 0.022, 0.025)), d, "B")
    tar = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
               g.op(unreal.MaterialExpressionSubtract, g.const(0.012), d), const_b=166.0))   # 1 within 0.006 .. 0 by 0.012 (8-bit vertex colours)
    own = [g.op(unreal.MaterialExpressionMultiply, vc, L.noise(g.m, 0.05, 0.8, 1.2, -1200, g.y + 200)),
           g.const((0.0, 0.0, 1.0)), g.const(0.8)]
    g.y += 140
    road = g.mix(own, road, tar)
    # Wet: the tarmac dark and glossy (as Buttertubs' road); dry keeps a floor so there's no white sheen.
    wet = L.wetness(g)
    road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], g.lerp(g.const(1.0), g.lerp(g.const(0.75), g.const(0.5), tar), wet))
    road[2] = g.op(unreal.MaterialExpressionMax, road[2], g.lerp(g.const(0.6), g.const(0.12), wet))
    return g.finish(road, specular=0.4)


def lamp_material(L):
    """A tunnel lamp's glowing face (World/WorldRide's RoadPiece: RideMesh::Glow, its vertex colour the
    light's): unlit, bright enough to read as a lamp under Ride UK's daytime exposure and at night."""
    mel = L.mel
    m = L.new_material("M_RideLamp", MAT_DIR)
    L.try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -600, 0)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -350, 0)
    mel.connect_material_expressions(vc, "", mul, "A")   # the unnamed output is RGB
    mel.connect_material_expressions(L.const1(m, 150.0, -600, 200), "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def night_material(L):
    """What lights up after dark (Ride UK stage 7: RideMesh::Night): lit windows and street lamps' heads
    (vertex alpha 0) and cat's eyes (alpha 1), their vertex colour the light's. By day dark glass, or a
    pale stud; as MPC_Weather's Night rises they glow, the cat's eyes only near by (they shine back the
    headlight, which reaches a couple of hundred metres: lit only in the beam, never glowing by themselves)."""
    g = L.Graph(L.new_material("M_RideNight", MAT_DIR))
    vc = g.node(unreal.MaterialExpressionVertexColor)
    alpha = g.op(unreal.MaterialExpressionMultiply, vc, g.const(1.0), a_out="A")   # (the vertex colour's A output)
    rgb = vc                                                                        # (its unnamed output is RGB)
    # (by day: glass 4% of its colour, a stud half)
    base = g.op(unreal.MaterialExpressionMultiply, rgb, g.lerp(g.const(0.04), g.const(0.5), alpha))
    night = L.weather_param(g, "Night")
    # (a cat's eye or a red edge stud is a retroreflector, not a light: it shines only back along the
    # headlight's beam, which the camera rides just behind. So it is lit only in the beam, the middle
    # of the view a little below the horizon, brightest near and gone by about 120 m, and dark otherwise
    # (beenster, build 141: "should only reflect back our light, not glow in the dark"). A lit window
    # or lamp keeps its glow.)
    depth = g.node(unreal.MaterialExpressionPixelDepth)
    near = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionSubtract, g.const(1.2), g.op(unreal.MaterialExpressionDivide, depth, g.const(10000.0))))
    near = g.op(unreal.MaterialExpressionMultiply, near, near)
    beam = g.const(1.0)
    try:
        sp = g.node(unreal.MaterialExpressionScreenPosition)
        c2 = lambda a, b: g.node(unreal.MaterialExpressionConstant2Vector, r=a, g=b)   # (two numbers: ViewportUV is two)
        off = g.op(unreal.MaterialExpressionSubtract, sp, c2(0.5, 0.6), a_out="ViewportUV")
        off = g.op(unreal.MaterialExpressionMultiply, off, c2(1.5, 2.6))
        dist = g.op(unreal.MaterialExpressionDotProduct, off, off)
        beam = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionSubtract, g.const(1.0), dist))
    except Exception as e:
        unreal.log_warning("[Torque] ride UK: cat's eyes beam not made (%s); they fade by distance only" % e)
    fade = g.lerp(g.const(1.0), g.op(unreal.MaterialExpressionMultiply, near, beam), alpha)
    glow = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, rgb, night), g.op(unreal.MaterialExpressionMultiply, fade, g.const(60.0)))
    L.mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    L.mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    L.mel.connect_material_property(g.const(0.25), "", unreal.MaterialProperty.MP_ROUGHNESS)
    L.mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    return g.m


def water_material(L):
    """The ride's water (Ride UK stage 5: World/WorldRideScenery's WaterTile): the sea, lakes and
    rivers, their colour the vertex colour (dark: deep sea blue-grey, peaty lake, brown river), glossy,
    with slow ripples drifting across it (UV0 is the world's metres over 8). Opaque: open water at a
    distance shows the sky in it, not its bed."""
    g = L.Graph(L.new_material("M_RideWater"))
    vc = g.node(unreal.MaterialExpressionVertexColor)
    nt = L.noise_texture()
    if nt is not None:
        uv = g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=0.9, v_tiling=0.9)
        p1 = g.node(unreal.MaterialExpressionPanner, speed_x=0.012, speed_y=0.02); g.link(uv, p1, "Coordinate")
        uv2 = g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=2.3, v_tiling=2.3)
        p2 = g.node(unreal.MaterialExpressionPanner, speed_x=-0.025, speed_y=0.01); g.link(uv2, p2, "Coordinate")
        n = g.op(unreal.MaterialExpressionAdd, g.sample(nt, p1, "linear"), g.sample(nt, p2, "linear"))
        n = g.op(unreal.MaterialExpressionSubtract, n, const_b=1.0)
        n = g.op(unreal.MaterialExpressionMultiply, n, g.const((0.35, 0.35, 0.0)))
        normal = g.op(unreal.MaterialExpressionAdd, n, g.const((0.0, 0.0, 1.0)))
    else:
        normal = g.const((0.0, 0.0, 1.0))
    base = g.op(unreal.MaterialExpressionMultiply, vc, g.const(1.0))
    mel = L.mel
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(g.const(0.05), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(g.const(0.5), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
    mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    return g.m


def _coursed(g, L, u_m, v_m, stagger=0.5):
    """Running courses on UV0 (metres): units u_m along, v_m up, every other course offset by
    `stagger` of a unit. Returns (along-unit fraction, up-course fraction, the unit's own 0..1
    variation from the noise texture or None)."""
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    u = g.node(unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False); g.link(uv, u)
    v = g.node(unreal.MaterialExpressionComponentMask, r=False, g=True, b=False, a=False); g.link(uv, v)
    div = lambda a, b: g.op(unreal.MaterialExpressionDivide, a, const_b=float(b)) if isinstance(b, (int, float)) else g.op(unreal.MaterialExpressionDivide, a, b)
    rows = div(v, v_m)
    row = g.op(unreal.MaterialExpressionFloor, rows)
    fv = g.op(unreal.MaterialExpressionFrac, rows)
    units = g.op(unreal.MaterialExpressionAdd, div(u, u_m),
                 g.op(unreal.MaterialExpressionMultiply, row, const_b=stagger))
    fu = g.op(unreal.MaterialExpressionFrac, units)
    var = None
    nt = L.noise_texture()
    if nt is not None:
        # (one value per unit: the noise read at a point fixed by the unit's column and course)
        cell = g.op(unreal.MaterialExpressionAppendVector,
                    g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionFloor, units), const_b=0.1731),
                    g.op(unreal.MaterialExpressionMultiply, row, const_b=0.2917))
        var = g.sample(nt, cell, "linear")
    return fu, fv, var


def roof_material(L):
    """[OSM stage 3] The streamed world's roofs (World/WorldRideBuildings: RideMesh::Roofing), drawn
    without any texture: the vertex colour the roof's colour; the vertex alpha the roofing, 0 slate,
    0.25 stone slates, 0.5 clay tiles (courses of those three, each slate or tile a little different,
    a shadow under each course's edge), 0.75 thatch (streaks down the slope), 1 sheet or felt
    (corrugations). UV0 is metres along the eaves and up the slope."""
    g = L.Graph(L.new_material("M_RideRoof"))
    vc = g.node(unreal.MaterialExpressionVertexColor)
    k = g.op(unreal.MaterialExpressionMultiply, vc, const_b=1.0, a_out="A")
    sat = lambda e: g.op(unreal.MaterialExpressionSaturate, e)
    # (course height: slate 0.23 m, stone slates 0.36, tiles 0.2; units 1.3 times as wide)
    to_stone = sat(g.op(unreal.MaterialExpressionMultiply, k, const_b=4.0))
    to_tile = sat(g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, k, const_b=0.25), const_b=4.0))
    h = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, to_stone, const_b=0.13),
             g.op(unreal.MaterialExpressionMultiply, to_tile, const_b=-0.16))
    h = g.op(unreal.MaterialExpressionAdd, h, const_b=0.23)
    w = g.op(unreal.MaterialExpressionMultiply, h, const_b=1.3)
    fu, fv, var = _coursed(g, L, w, h)
    edge = g.lerp(g.const(0.62), g.const(1.0), sat(g.op(unreal.MaterialExpressionDivide, fv, const_b=0.14)))
    joint = g.op(unreal.MaterialExpressionMin, fu, g.op(unreal.MaterialExpressionOneMinus, fu))
    joint = g.lerp(g.const(0.78), g.const(1.0), sat(g.op(unreal.MaterialExpressionDivide, joint, const_b=0.035)))
    shade = g.op(unreal.MaterialExpressionMultiply, edge, joint)
    if var is not None:
        shade = g.op(unreal.MaterialExpressionMultiply, shade,
                     g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, var, const_b=0.3, a_out="R"), const_b=0.85))
    # (courses on slate, stone slate and tile only; thatch streaked; sheet corrugated)
    coursed = g.op(unreal.MaterialExpressionOneMinus, sat(g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, k, const_b=0.5), const_b=4.0)))
    sheet = sat(g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, k, const_b=0.875), const_b=8.0))
    thatch = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionOneMinus, coursed), g.op(unreal.MaterialExpressionOneMinus, sheet))
    other = g.const(1.0)
    nt = L.noise_texture()
    if nt is not None:
        streak = g.sample(nt, g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=3.0, v_tiling=0.12), "linear")
        streak = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, streak, const_b=0.35, a_out="R"), const_b=0.8)
        other = g.lerp(other, streak, thatch)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    u = g.node(unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False); g.link(uv, u)
    wave = g.node(unreal.MaterialExpressionSine, period=0.076); g.link(u, wave)
    wave = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, wave, const_b=0.12), const_b=0.92)
    other = g.lerp(other, wave, sheet)
    shade = g.lerp(other, shade, coursed)
    col = g.op(unreal.MaterialExpressionMultiply, vc, shade)
    col = L.far_dull(g, col)
    rough = g.lerp(g.const(0.88), g.const(0.62), g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionOneMinus, to_stone), coursed))   # (slate the glossiest)
    return g.finish([col, g.const((0.0, 0.0, 1.0)), rough], specular=0.4)


def brick_material(L):
    """[OSM stage 3] The streamed world's brick walls (RideMesh::Brickwork), without any texture:
    the vertex colour the brick's, in stretcher bond (215 by 65 mm bricks, 10 mm joints), each
    brick a little lighter or darker, pale mortar, dirt and green-brown splash at the foot from the
    vertex alpha as M_RideRender has. UV0 is metres along the wall and down it."""
    g = L.Graph(L.new_material("M_RideBrick"))
    vc = g.node(unreal.MaterialExpressionVertexColor)
    col = g.op(unreal.MaterialExpressionMultiply, vc, g.const(1.0))
    fu, fv, var = _coursed(g, L, 0.225, 0.075)
    sat = lambda e: g.op(unreal.MaterialExpressionSaturate, e)
    if var is not None:
        col = g.op(unreal.MaterialExpressionMultiply, col,
                   g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, var, const_b=0.4, a_out="R"), const_b=0.8))
    brick = g.op(unreal.MaterialExpressionMultiply, sat(g.op(unreal.MaterialExpressionDivide, fu, const_b=0.045)),
                 sat(g.op(unreal.MaterialExpressionDivide, fv, const_b=0.13)))
    mortar = g.op(unreal.MaterialExpressionMultiply, vc, g.const((1.5, 1.6, 1.8)))
    mortar = g.op(unreal.MaterialExpressionMin, mortar, g.const((0.42, 0.4, 0.36)))
    col = g.lerp(mortar, col, brick)
    dirt = g.op(unreal.MaterialExpressionMultiply, col, g.const((0.42, 0.42, 0.33)))
    col = g.lerp(col, dirt, vc, alpha_out="A")
    col = L.far_dull(g, col)
    rough = g.lerp(g.const(0.95), g.const(0.85), brick)
    return g.finish([col, g.const((0.0, 0.0, 1.0)), rough], specular=0.35)


def scanned_materials(L):
    """The hand-built ride's own scanned looks, made again here under /Game/RideUK/Materials so the
    streamed world is drawn as Buttertubs is (Ride UK stage 3): M_RideLandScan (Buttertubs' M_Land:
    grass, moor grass, peat, rock on steep ground, tinted by the vertex colour the ground's cover gives
    it, World/WorldRide's LandColour, which matches that colour's meaning), M_RideStone (M_Stone, the
    dry stone walls) and M_RideMasonry (M_Masonry, stone houses and barns). Their vertex colours are
    the stone's masks (R brightness, G which scan, B lichen, A moss) and UV0 metres of surface, as
    World/WorldRideScenery makes them. Built from the same textures as Buttertubs (Data/Textures)."""
    tex, roles = L.import_textures()
    made = []
    original = L.new_material
    try:
        for want, make in (("M_RideLandScan", lambda: L.land_material_scanned(tex, roles) if "grass" in tex else None),
                           ("M_RideStone", lambda: L.stone_material(tex, roles, tint_param=True)),
                           ("M_RideMasonry", lambda: L.stone_material(tex, roles, "M_Masonry", "masonry", "masonry", broad_moss=False, tint_param=True)),
                           # (stage 5: rendered and limewashed houses, the vertex colour their wash)
                           ("M_RideRender", lambda: L.render_material(tex, roles, "M_RideRender")),
                           # (stage 5: the sea, lakes and rivers)
                           ("M_RideWater", lambda: water_material(L)),
                           # (stage 7: lit windows, street lamps and cat's eyes after dark)
                           ("M_RideNight", lambda: night_material(L)),
                           # (OSM stage 3: roofs coursed by their roofing, and brick walls in bond)
                           ("M_RideRoof", lambda: roof_material(L)),
                           ("M_RideBrick", lambda: brick_material(L)),
                           # (build 141: the road in Buttertubs' scanned tarmac; the plain one stays without the scan)
                           ("M_RideRoad", lambda: road_material_scanned(L, tex, roles) if "asphalt" in tex else None)):
            # (the level script's makers name their own material in its own folder: put it here, under our name)
            L.new_material = lambda name, folder=None, _want=want: original(_want, MAT_DIR)
            try:
                if make() is not None:
                    made.append(want)
            except Exception as e:
                unreal.log_warning("[Torque] ride UK: couldn't make %s (%s); the game draws it plain" % (want, e))
    finally:
        L.new_material = original
    if "grass" not in tex:
        # (a stale scanned ground would otherwise be used with textures that are gone)
        path = MAT_DIR + "/M_RideLandScan"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
        unreal.log_warning("[Torque] ride UK: no grass texture (python Tools/fetch_textures.py); the ground stays plain")
    return made


SPECIES_FIELDS = ("name", "cull_distance", "cast_shadow", "scale_multiplier", "native_mesh", "keep", "cull_spread", "min_draw_distance")


def read_kit(L):
    """Buttertubs' plants and trees as its level has them (its TorqueFoliage actors "Foliage" and
    "Trees"): {label: {"frame": (yaw, mirror, scale), "species": [{field: value, "mesh": path, ...}]}},
    asset paths rather than objects, as the level is let go before the ride's is built."""
    if not hasattr(unreal, "TorqueFoliage"):
        unreal.log_warning("[Torque] ride UK: the TorqueFoliage class isn't built; no grass or trees")
        return {}
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.load_level(L.MAP):
        unreal.log_warning("[Torque] ride UK: couldn't open %s for its plants; no grass or trees" % L.MAP)
        return {}
    path = lambda o: o.get_path_name() if o else ""
    kit = {}
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        label = a.get_actor_label()
        if not isinstance(a, unreal.TorqueFoliage) or label not in ("Foliage", "Trees"):
            continue
        rows = []
        for e in a.get_editor_property("species"):
            r = {k: e.get_editor_property(k) for k in SPECIES_FIELDS}
            r["name"] = str(r["name"])
            r["mesh"] = path(e.get_editor_property("mesh"))
            r["material"] = path(e.get_editor_property("material"))
            r["variants"] = [path(m) for m in e.get_editor_property("variants")]
            r["variant_materials"] = [path(m) for m in e.get_editor_property("variant_materials")]
            rows.append(r)
        kit[label] = {"frame": (a.get_editor_property("frame_yaw"), a.get_editor_property("frame_mirror"), a.get_editor_property("frame_scale")),
                      "species": rows}
    log("Buttertubs' plants: %s" % ", ".join("%s %d species" % (k, len(v["species"])) for k, v in kit.items()))
    return kit


def grass_materials(L, kit):
    """The grass's materials without Buttertubs' ground-colour virtual texture (the ride has none: the
    plants would sample black): M_RideFoliage for our own plants, and the scanned plants' instances
    copied onto M_RideFoliageScan, the scanned master made afresh with no ground blend. Changes the
    kit's "Foliage" rows in place."""
    rows = kit.get("Foliage", {}).get("species", [])
    if not rows:
        return
    original = L.new_material
    try:
        L.new_material = lambda name, folder=None: original("M_RideFoliage", MAT_DIR)
        plain = L.foliage_material(None, 1.0)
        defaults = {}
        for r in rows:
            for p in r["variants"]:
                m = unreal.load_asset(p)
                if isinstance(m, unreal.StaticMesh):
                    for k, v in L.scan_textures(m).items():
                        defaults.setdefault(k, v)
        master = None
        if "BaseColor" in defaults:
            L.new_material = lambda name, folder=None: original("M_RideFoliageScan", MAT_DIR)
            master = L.scan_foliage_master(defaults, None, 1.0)
    finally:
        L.new_material = original
    plain_path = plain.get_path_name() if plain else ""
    copied = {}
    for r in rows:
        if r["material"] or not r["variants"]:
            r["material"] = plain_path
        mats = []
        for p in r["variant_materials"]:
            if not p or master is None:
                mats.append(p if master is None else "")
                continue
            if p not in copied:
                src = p.split(".")[0]
                dst = MAT_DIR + "/Ride" + src.rsplit("/", 1)[-1]
                if unreal.EditorAssetLibrary.does_asset_exist(dst):
                    unreal.EditorAssetLibrary.delete_asset(dst)
                mi = unreal.EditorAssetLibrary.duplicate_asset(src, dst)
                if mi:
                    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, master)
                    unreal.EditorAssetLibrary.save_loaded_asset(mi)
                copied[p] = mi.get_path_name() if mi else ""
            mats.append(copied[p])
        r["variant_materials"] = mats
    log("grass materials: M_RideFoliage%s, %d scanned instances" % (", M_RideFoliageScan" if master else "", len(copied)))


def scanned_trees(L, kit):
    """Scanned trees (Data/RideUK/scanned_trees.json: Quixel Megaplants once imported on the PC) in
    the kit's "Trees" in place of Buttertubs' own models of the same kind, as species named
    SM_Tree_<kind>_scan (native meshes with their own materials). Changes the kit in place."""
    f = os.path.join(os.path.dirname(L.data_dir()), "RideUK", "scanned_trees.json")
    if not os.path.exists(f) or "Trees" not in kit:
        return
    spec = json.load(open(f))
    norm = lambda t: "".join(ch for ch in t.lower() if ch.isalnum())
    skip = [norm(w) for w in spec.get("skip", [])]
    most = int(spec.get("most", 6))
    meshes = []
    for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path("/Game", recursive=True):
        if str(a.asset_class_path.asset_name) != "StaticMesh":
            continue
        pkg = str(a.package_name)
        if pkg.startswith("/Game/Buttertubs/") or pkg.startswith("/Game/RideUK/"):
            continue
        meshes.append((norm(pkg), "%s.%s" % (pkg, a.asset_name)))
    rows = kit["Trees"]["species"]
    template = dict(rows[0]) if rows else {"cull_distance": 3000000.0, "cast_shadow": True}
    found = {}
    for kind, words in spec["kinds"].items():
        keys = [norm(w) for w in words]
        got = sorted(p for n, p in meshes if any(k in n for k in keys) and not any(w in n for w in skip))[:most]
        if not got:
            continue
        found[kind] = got
        # (ours of this kind go: the scanned ones take every tree of it, near and far)
        rows[:] = [r for r in rows if not (r["name"].startswith("SM_Tree_%s_" % kind) or r["name"].startswith("SM_TreeFar_%s_" % kind))]
        r = dict(template)
        r.update({"name": "SM_Tree_%s_scan" % kind, "mesh": "", "material": "", "variants": got, "variant_materials": [],
                  "native_mesh": True, "scale_multiplier": 1.0, "keep": 1.0, "cull_spread": 1.0, "min_draw_distance": 0.0, "cast_shadow": True})
        rows.append(r)
    log("scanned trees: %s" % (", ".join("%s %d (%s)" % (k, len(v), v[0].rsplit("/", 1)[-1]) for k, v in found.items()) or
                               "none found (import the Megaplants on the PC; Data/RideUK/scanned_trees.json)"))


def place_kit(kit):
    """The ride's plant kit: two TorqueFoliage actors that plant nothing themselves (Density 0), tagged
    RideGrass and RideTrees, with Buttertubs' species; World/TorqueRideWorld lays them out as it goes."""
    load = lambda p: unreal.load_asset(p) if p else None
    for label, tag in (("Foliage", "RideGrass"), ("Trees", "RideTrees")):
        k = kit.get(label)
        if not k:
            continue
        species = []
        for r in k["species"]:
            e = unreal.TorqueFoliageSpecies()
            for f in SPECIES_FIELDS:
                e.set_editor_property(f, r[f])
            e.set_editor_property("mesh", load(r["mesh"]))
            e.set_editor_property("material", load(r["material"]))
            e.set_editor_property("variants", [m for m in (load(p) for p in r["variants"]) if m])
            e.set_editor_property("variant_materials", [load(p) for p in r["variant_materials"]][:len(r["variants"])])
            species.append(e)
        a = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.TorqueFoliage, unreal.Vector(0, 0, 0))
        a.set_actor_label(tag)
        a.set_editor_property("tags", [unreal.Name(tag)])
        a.set_editor_property("instances_file", "")
        a.set_editor_property("density", 0.0)
        a.set_editor_property("species", species)
        a.set_editor_property("frame_yaw", float(k["frame"][0]))
        a.set_editor_property("frame_mirror", float(k["frame"][1]))
        a.set_editor_property("frame_scale", float(k["frame"][2]))
        log("%s: %d species" % (tag, len(species)))


ANIMAL_DEST = "/Game/RideUK/Animals"


def animal_kit(L):
    """The livestock (Ride UK stage 6): Data/RideUK/Animals/*.glb (Tools/make_ride_animals.py) imported
    to /Game/RideUK/Animals, no collision, and listed in a TorqueFoliage actor tagged RideAnimals that
    plants nothing itself; World/TorqueRideWorld puts them in the fields as it goes. The sheep wear
    M_RideFleece (vertex colour, grazing heads nodding as Buttertubs' do), the cattle M_RideHide.
    Returns a short note for the log."""
    if not hasattr(unreal, "TorqueFoliage"):
        return "no animals (TorqueFoliage isn't built)"
    folder = os.path.join(os.path.dirname(L.data_dir()), "RideUK", "Animals")
    if not os.path.isdir(folder) or not any(f.endswith(".glb") for f in os.listdir(folder)):
        return "no animals (python Tools/make_ride_animals.py)"
    meshes = L.import_glbs(folder, ANIMAL_DEST)
    sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for mesh in meshes.values():
        try:
            sme.remove_collisions(mesh)
        except Exception:
            pass
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    original = L.new_material
    try:
        L.new_material = lambda name, folder=None: original(name, MAT_DIR)
        fleece = L.flat_material("M_RideFleece", 0.95, fine=0.4, wpo=L.graze_wpo)
        hide = L.flat_material("M_RideHide", 0.8, fine=0.25)
    finally:
        L.new_material = original
    first = sorted(meshes)[0]
    frame = L.find_frame(meshes[first], os.path.join(folder, first + ".glb"))
    species = []
    for name, mesh in sorted(meshes.items()):
        e = unreal.TorqueFoliageSpecies()
        e.set_editor_property("name", name)
        e.set_editor_property("mesh", mesh)
        e.set_editor_property("material", fleece if ("_ewe_" in name or "_lamb_" in name) else hide)
        for k, v in (("cast_shadow", True), ("scale_multiplier", 1.0), ("cull_distance", 40000.0)):
            try:
                e.set_editor_property(k, v)
            except Exception:
                pass
        species.append(e)
    a = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.TorqueFoliage, unreal.Vector(0, 0, 0))
    a.set_actor_label("RideAnimals")
    a.set_editor_property("tags", [unreal.Name("RideAnimals")])
    a.set_editor_property("instances_file", "")
    a.set_editor_property("density", 0.0)
    a.set_editor_property("species", species)
    a.set_editor_property("frame_yaw", float(frame[0]))
    a.set_editor_property("frame_mirror", float(frame[1]))
    a.set_editor_property("frame_scale", float(frame[2]))
    return "RideAnimals: %d models" % len(species)


PROP_DEST = "/Game/RideUK/Props"


def prop_kit(L):
    """The things by the road (OSM stage 2): Data/RideUK/Props/*.glb (Tools/make_ride_props.py)
    imported to /Game/RideUK/Props, no collision, and any static mesh under /Game whose path holds one
    of a kind's words in Data/RideUK/prop_kit.json (Fab models imported on the PC; a scan of a real,
    named place only when the file's use_real_scans is true), which take that kind's place. All listed
    in a TorqueFoliage actor tagged RideProps that plants nothing itself, each as a species named
    SM_Prop_<kind>_<n>; World/TorqueRideWorld puts them by the road as it goes, each kind scaled to its
    height in life. Returns a short note for the log."""
    if not hasattr(unreal, "TorqueFoliage"):
        return "no props (TorqueFoliage isn't built)"
    ride = os.path.join(os.path.dirname(L.data_dir()), "RideUK")
    spec_file = os.path.join(ride, "prop_kit.json")
    if not os.path.exists(spec_file):
        return "no props (Data/RideUK/prop_kit.json is missing)"
    spec = json.load(open(spec_file))
    norm = lambda t: "".join(ch for ch in t.lower() if ch.isalnum())
    skip = [norm(w) for w in spec.get("skip", [])]
    most = int(spec.get("most", 4))
    real = bool(spec.get("use_real_scans", False))
    # Ours, made by Tools/make_ride_props.py.
    folder = os.path.join(ride, "Props")
    ours = {}
    if os.path.isdir(folder) and any(f.endswith(".glb") for f in os.listdir(folder)):
        ours = L.import_glbs(folder, PROP_DEST)
        sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        for mesh in ours.values():
            try:
                sme.remove_collisions(mesh)
            except Exception:
                pass
            unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    # Others' (Fab), by the kit's words.
    meshes = []
    for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path("/Game", recursive=True):
        if str(a.asset_class_path.asset_name) != "StaticMesh":
            continue
        pkg = str(a.package_name)
        if pkg.startswith("/Game/Buttertubs/") or pkg.startswith("/Game/RideUK/"):
            continue
        meshes.append((norm(pkg), "%s.%s" % (pkg, a.asset_name)))
    found = {}
    for kind, row in spec["kinds"].items():
        keys = [norm(w) for w in row.get("words", []) + (row.get("real_words", []) if real else []) if norm(w)]
        got = sorted(p for n, p in meshes if any(k in n for k in keys) and not any(w in n for w in skip))[:most] if keys else []
        if got:
            found[kind] = got
    original = L.new_material
    try:
        L.new_material = lambda name, folder=None: original(name, MAT_DIR)
        paint = L.flat_material("M_RideProp", 0.7, fine=0.25)
    finally:
        L.new_material = original
    frame = (0.0, 1.0, 1.0)
    if ours:
        first = sorted(ours)[0]
        frame = L.find_frame(ours[first], os.path.join(folder, first + ".glb"))
    species = []

    def add(name, mesh, native, row):
        e = unreal.TorqueFoliageSpecies()
        e.set_editor_property("name", name)
        e.set_editor_property("mesh", mesh)
        if not native:
            e.set_editor_property("material", paint)
        for k, v in (("cast_shadow", bool(row.get("shadow", False))), ("scale_multiplier", 1.0),
                     ("cull_distance", float(row.get("reach", 400)) * 100.0), ("native_mesh", native)):
            try:
                e.set_editor_property(k, v)
            except Exception:
                pass
        species.append(e)

    for kind, row in spec["kinds"].items():
        if kind in found:
            for n, p in enumerate(found[kind]):
                m = unreal.load_asset(p)
                if m:
                    add("SM_Prop_%s_fab%d" % (kind, n), m, True, row)
        else:
            for name, mesh in sorted(ours.items()):
                if name.startswith("SM_Prop_%s_" % kind):
                    add(name, mesh, False, row)
    a = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.TorqueFoliage, unreal.Vector(0, 0, 0))
    a.set_actor_label("RideProps")
    a.set_editor_property("tags", [unreal.Name("RideProps")])
    a.set_editor_property("instances_file", "")
    a.set_editor_property("density", 0.0)
    a.set_editor_property("species", species)
    a.set_editor_property("frame_yaw", float(frame[0]))
    a.set_editor_property("frame_mirror", float(frame[1]))
    a.set_editor_property("frame_scale", float(frame[2]))
    kinds = {str(e.get_editor_property("name")).rsplit("_", 1)[0][len("SM_Prop_"):] for e in species}
    none = [k for k in spec["kinds"] if k not in kinds]
    return "RideProps: %d models (%s from Fab: %s), %d kinds with none (code-made): %s" % (
        len(species), len(found), ", ".join("%s %s" % (k, v[0].rsplit("/", 1)[-1]) for k, v in found.items()) or "none",
        len(none), ", ".join(none) or "none")


def build():
    L = level_lib()
    land_material(L)
    road_material(L)
    lamp_material(L)
    scanned = scanned_materials(L)
    kit = read_kit(L)
    grass_materials(L, kit)
    scanned_trees(L, kit)
    before = L.begin_fresh_level(MAP)
    L.sky_and_camera(reuse_cloud_shadow=True)
    # (the bike is put down at the ride's start by the game; a level still wants a start of its own)
    L.spawn(unreal.PlayerStart, (0, 0, 100000.0))
    place_kit(kit)
    log(animal_kit(L))
    log(prop_kit(L))
    L.save_fresh_level(MAP, before)
    log("built %s and %s/M_RideLand, M_RideRoad%s" % (MAP, MAT_DIR, "".join(", " + m for m in scanned)))


build()
