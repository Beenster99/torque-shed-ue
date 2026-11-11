"""Build the level the real world's roads are ridden in (Ride UK stage 2): /Game/Maps/RideUK.

Run inside the Unreal editor, after Tools/ue_build_level.py has built the Buttertubs ride (it borrows
that script's sun, sky, air and camera, and the ride's cloud shadows as they are):

    UnrealEditor-Cmd.exe TorqueShedProto.uproject -ExecutePythonScript=Tools/ue_build_rideuk.py -unattended

The level has no ground of its own: only the sky, the lights and a player start. The game lays the
ground and the road out round the rider as they go (World/TorqueRideWorld), from the ride the world
subsystem planned, and draws them with the two plain materials made here, /Game/RideUK/Materials'
M_RideLand (moor grass, mottled) and M_RideRoad (the road's vertex colours: tarmac, verge, paint).
The land's cover, walls, trees and buildings are later stages.
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
    """Moor grass: two greens in broad patches, mottled close up, matte."""
    mel = L.mel
    m = L.new_material("M_RideLand", MAT_DIR)
    dark = L.const3(m, (0.055, 0.075, 0.03), -900, 0)
    pale = L.const3(m, (0.12, 0.135, 0.05), -900, 200)
    patch = L.noise(m, 0.0006, 0.0, 1.0, -900, 400)
    mix = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -600, 100)
    mel.connect_material_expressions(dark, "", mix, "A")
    mel.connect_material_expressions(pale, "", mix, "B")
    mel.connect_material_expressions(patch, "", mix, "Alpha")
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


def build():
    L = level_lib()
    land_material(L)
    road_material(L)
    before = L.begin_fresh_level(MAP)
    L.sky_and_camera(reuse_cloud_shadow=True)
    # (the bike is put down at the ride's start by the game; a level still wants a start of its own)
    L.spawn(unreal.PlayerStart, (0, 0, 100000.0))
    L.save_fresh_level(MAP, before)
    log("built %s and %s/M_RideLand, M_RideRoad" % (MAP, MAT_DIR))


build()
