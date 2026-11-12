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
"""
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
                           ("M_RideStone", lambda: L.stone_material(tex, roles)),
                           ("M_RideMasonry", lambda: L.stone_material(tex, roles, "M_Masonry", "masonry", "masonry", broad_moss=False))):
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


def build():
    L = level_lib()
    land_material(L)
    road_material(L)
    scanned = scanned_materials(L)
    before = L.begin_fresh_level(MAP)
    L.sky_and_camera(reuse_cloud_shadow=True)
    # (the bike is put down at the ride's start by the game; a level still wants a start of its own)
    L.spawn(unreal.PlayerStart, (0, 0, 100000.0))
    L.save_fresh_level(MAP, before)
    log("built %s and %s/M_RideLand, M_RideRoad%s" % (MAP, MAT_DIR, "".join(", " + m for m in scanned)))


build()
