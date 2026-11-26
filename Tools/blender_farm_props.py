"""Ride UK's farmyards (the everyday kits plan, stage 4): the yard's pieces, modelled in Blender. Our own models,
from general knowledge of British farms: no maker's names, badges, colours schemes or lettering (the tractors are
made-up machines in made-up liveries).

Writes Data/RideUK/Props/SM_Prop_<kind>_<n>.glb, each with a distance version made here by hand,
SM_Prop_<kind>_<n>_LOD1.glb (never reduced by the engine: Tools/ue_build_rideuk.py puts it in as the mesh's LOD 1),
and sets each kind's height and reach in Data/RideUK/prop_kit.json. Same material and frame as the street kit
(Tools/blender_street_kit.py: M_RideStreetKit, front +x, foot at 0, y up in the file); all the models of a kind are
the same height (the game scales a kind's models to its height). World/WorldRideFarms.cpp lays them in the map's
farmyards.

  farm_bales 0..2     a pyramid of round straw bales; a stack of big square bales; a pyramid of wrapped black bales
  farm_bale 0..2      one round bale: straw, wrapped black, wrapped green
  farm_clamp 0..1     a 6 m length of silage clamp (concrete panel walls, black sheet, tyres), and its open end;
                      the game stretches it along the clamp
  farm_tank 0..1      a slurry store: a galvanised steel ring tank with its ladder; a concrete panel tank
  farm_feedbin 0..1   one galvanised hopper feed bin; two side by side
  farm_trough 0..1    a galvanised water trough on legs; a concrete one
  farm_tractor 0..2   a made-up four-wheel-drive tractor with a cab: green, red, blue, a front loader on one
  farm_trailer 0..1   a grain trailer, high sides; a flat bale trailer
  farm_diesel 0       a bunded diesel tank with its pump
  farm_muck 0..1      a muck heap; a heap of field stones and rubble

    python Tools/blender_farm_props.py                # the models
    python Tools/blender_farm_props.py --render DIR   # and the review sheet's pictures in DIR
"""
import json
import math
import os
import random
import sys

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_street_kit as K   # noqa: E402
from blender_street_kit import box, lathe, rod, sweep   # noqa: E402

STRAW = (0.48, 0.36, 0.15)
STRAW_END = (0.4, 0.3, 0.13)
WRAP_BLACK = (0.012, 0.012, 0.013)
WRAP_GREEN = (0.03, 0.08, 0.035)
CONCRETE = (0.36, 0.35, 0.32)
CONCRETE_OLD = (0.27, 0.27, 0.24)
SHEET_BLACK = (0.015, 0.015, 0.016)
TYRE = (0.018, 0.018, 0.02)
SILAGE = (0.12, 0.1, 0.03)
GALV = (0.46, 0.47, 0.46)
GALV_DULL = (0.33, 0.34, 0.33)
TANK_GREEN = (0.04, 0.08, 0.05)
GREEN = (0.05, 0.16, 0.05)
RED = (0.32, 0.03, 0.02)
BLUE = (0.02, 0.06, 0.2)
GREY_DARK = (0.04, 0.042, 0.045)
HUB_YELLOW = (0.55, 0.38, 0.02)
HUB_GREY = (0.3, 0.3, 0.3)
GLASS = (0.02, 0.025, 0.03)
MUCK = (0.06, 0.045, 0.025)
MUCK_STRAW = (0.2, 0.15, 0.06)
STONES = (0.3, 0.29, 0.26)
WATER = (0.03, 0.04, 0.04)

# kind: (reach in m, casts a shadow)
KINDS = {"farm_bales": (450, True), "farm_bale": (220, True), "farm_clamp": (400, True), "farm_tank": (700, True),
         "farm_feedbin": (700, True), "farm_trough": (120, False), "farm_tractor": (300, True), "farm_trailer": (300, True),
         "farm_diesel": (150, True), "farm_muck": (300, True)}


def M_(name):
    return K.Model(name)


def round_bale(M, x, y, z, d=1.2, w=1.2, rgb=STRAW, wrapped=False, axis="y", lod=0, rot=0.0):
    """A round bale lying on its side, its axis along y (or x), its foot at z."""
    seg = 16 if lod == 0 else 8
    r = d / 2
    prof = [(0.0, -w / 2), (r * 0.92, -w / 2), (r, -w / 2 + 0.06), (r, w / 2 - 0.06), (r * 0.92, w / 2), (0.0, w / 2)] if lod == 0 else \
        [(0.0, -w / 2), (r, -w / 2), (r, w / 2), (0.0, w / 2)]
    ob = lathe("bale", prof, seg)
    R = Matrix.Rotation(math.pi / 2, 4, "X") if axis == "y" else Matrix.Rotation(math.pi / 2, 4, "Y")
    ob.matrix_world = Matrix.Translation((x, y, z + r)) @ Matrix.Rotation(rot, 4, "Z") @ R
    surf = "paint" if wrapped else "timber"
    M.add(ob, surf, rgb, 0.15 if wrapped else 0.35)
    return ob


def square_bale(M, x, y, z, size=(2.4, 1.2, 0.9), rgb=STRAW, lod=0, rot=0.0):
    ob = box("sqbale", (x, y, z + size[2] / 2), size, rot, bevel=0.04 if lod == 0 else 0.0)
    M.add(ob, "timber", rgb, 0.35)
    if lod == 0:
        # (the twines round it)
        for k in (-0.3, 0.3):
            dx = k * size[0]
            M.add(box("twine", (x + dx * math.cos(rot), y + dx * math.sin(rot), z + size[2] / 2), (0.02, size[1] + 0.02, size[2] + 0.02), rot), "paint", (0.35, 0.1, 0.02), 0.2)


def bales_pyramid(name, rgb, wrapped, lod):
    """Round bales in a pyramid three high (4 + 3 + 2) along y, two rows deep at the base."""
    M = M_(name)
    d, w = 1.5, 1.2
    rows = [(4, 0.0), (3, d * 0.866), (2, d * 1.732)]
    for depth in ((-0.62, 0.62) if lod == 0 else (0.0,)):
        for n, z in rows:
            for k in range(n):
                x = (k - (n - 1) / 2) * d
                if lod == 0:
                    round_bale(M, x, depth, z, d, w, rgb, wrapped, axis="y", lod=lod)
                else:
                    round_bale(M, x, depth, z, d, w * 2.05, rgb, wrapped, axis="y", lod=lod)
    return M


def bales_square(name, lod):
    M = M_(name)
    random.seed(4)
    for r in range(5):
        for c in range(3):
            for k in range(2 if lod == 0 else 1):
                x = (c - 1) * 2.42 + (random.uniform(-0.05, 0.05) if lod == 0 else 0.0)
                y = (k - 0.5) * 1.22 if lod == 0 else 0.0
                sz = (2.4, 1.2, 0.81) if lod == 0 else (2.4, 2.44, 0.81)
                square_bale(M, x, y, r * 0.82, sz, tuple(v * (0.9 + 0.1 * ((r + c) % 2)) for v in STRAW), lod)
    return M


def bale_one(name, rgb, wrapped, lod):
    M = M_(name)
    round_bale(M, 0.0, 0.0, 0.0, 1.5, 1.2, rgb, wrapped, axis="y", lod=lod)
    return M


def clamp(name, open_end, lod):
    """A 6 m length of a silage clamp 12 m wide: precast panel walls each side (2.5 m, leaning out), the
    silage under a black sheet weighted with tyres; open_end shows the cut face and the yard in front."""
    M = M_(name)
    L, Wd, H = 6.0, 12.0, 2.5
    for s in (-1, 1):
        y = s * Wd / 2
        ob = box("wall", (0, 0, 0), (1, 1, 1))
        ob.matrix_world = Matrix.Translation((0.0, y + s * 0.15, 0.0)) @ Matrix.Rotation(-s * 0.1, 4, "X") @ Matrix.Translation((0.0, 0.0, H / 2)) @ Matrix.Diagonal((L, 0.3, H, 1.0))
        M.add(ob, "concrete", CONCRETE_OLD, 0.5)
        if lod == 0:
            for k in range(3):
                x = -L / 2 + 1.0 + k * 2.0
                M.add(box("rib", (x, y + s * 0.45, 0.9), (0.25, 0.5, 1.8), 0.0), "concrete", CONCRETE_OLD, 0.5)
    # (the silage's top: a low hump, sheeted, from wall to wall)
    n = 8 if lod == 0 else 3
    top = []
    for i in range(n + 1):
        t = i / n
        yy = -Wd / 2 + 0.1 + (Wd - 0.2) * t
        top.append((yy, H - 0.2 + 0.9 * math.sin(math.pi * t)))
    bot = [(y, 0.0) for y, _ in top]
    from blender_warehouse_kit import strip
    M.add(strip("silage", top, bot, -L / 2, L / 2), "paint", SHEET_BLACK, 0.3)
    if lod == 0:
        random.seed(7)
        for k in range(14):
            yy = random.uniform(-Wd / 2 + 1.0, Wd / 2 - 1.0)
            x = random.uniform(-L / 2 + 0.6, L / 2 - 0.6)
            t = (yy + Wd / 2) / Wd
            z = H - 0.2 + 0.9 * math.sin(math.pi * t)
            ob = lathe("tyre", [(0.2, 0.0), (0.38, 0.0), (0.4, 0.1), (0.38, 0.2), (0.2, 0.2)], 10)
            ob.matrix_world = Matrix.Translation((x, yy, z - 0.02)) @ Matrix.Rotation(random.uniform(-0.25, 0.25), 4, "X")
            M.add(ob, "paint", TYRE, 0.4)
    if open_end:
        # (the cut face at +x: dark green-brown silage, the sheet folded back over it)
        prof = [(-Wd / 2 + 0.1, 0.0), (Wd / 2 - 0.1, 0.0)] + [(y, z) for y, z in reversed(top)]
        from blender_warehouse_kit import Wall
        w = Wall((L / 2, -Wd / 2), (0, 1), Wd, H)
        w.face(M_wrap(M), "face", [(0.1, 0.0), (Wd - 0.1, 0.0)] + [(y + Wd / 2, z - 0.15) for y, z in reversed(top)], -0.05, 0.0, "timber", SILAGE, 1)
        M.add(box("apron", (L / 2 + 3.0, 0.0, 0.03), (6.0, Wd, 0.06)), "concrete", CONCRETE, 0.6)
    return M


class _Wrap:
    """Lets the warehouse kit's Wall add into a prop model."""
    def __init__(self, M):
        self.M = M

    def add(self, obj, surface, rgb, lod=2):
        s = {"timber": "timber", "concrete": "concrete"}.get(surface, "paint")
        self.M.add(obj, s, rgb, 0.4)
        return obj


def M_wrap(M):
    return _Wrap(M)


def tank(name, concrete, lod):
    """A slurry store 16 m across, 4 m high: galvanised sheet rings and a ladder; or precast concrete panels."""
    M = M_(name)
    R, H = 8.0, 4.0
    seg = 40 if lod == 0 else 16
    prof = [(R - 0.15, 0.0), (R, 0.0), (R, H), (R - 0.15, H)]
    ob = lathe("tankwall", prof + [(R - 0.15, 0.0)], seg)
    M.add(ob, "concrete" if concrete else "galv", CONCRETE if concrete else GALV_DULL, 0.6)
    ob = lathe("slurry", [(0.0, H - 0.6), (R - 0.15, H - 0.6), (R - 0.15, H - 0.55), (0.0, H - 0.55)], seg)
    M.add(ob, "paint", (0.05, 0.045, 0.025), 0.3)
    if lod == 0:
        if not concrete:
            for k in range(1, 4):   # (the ring seams)
                M.add(lathe("seam", [(R + 0.01, k - 0.03), (R + 0.03, k - 0.03), (R + 0.03, k + 0.03), (R + 0.01, k + 0.03)], seg), "galv", GALV, 0.5)
        else:
            for k in range(24):
                a = 2 * math.pi * k / 24
                M.add(box("buttress", (math.cos(a) * (R + 0.15), math.sin(a) * (R + 0.15), H / 2 - 0.4), (0.35, 0.3, H - 0.8), a), "concrete", CONCRETE_OLD, 0.6)
        # (a caged ladder up to a platform)
        x = R + 0.35
        for y in (-0.25, 0.25):
            M.add(rod("stile", (x, y, 0.0), (x, y, H + 1.0), 0.03), "galv", GALV, 0.3)
        for k in range(int(H / 0.3)):
            M.add(rod("rung", (x, -0.25, 0.3 + k * 0.3), (x, 0.25, 0.3 + k * 0.3), 0.015), "galv", GALV, 0.3)
        M.add(box("platform", (R - 0.2, 0.0, H + 0.05), (1.2, 1.0, 0.06)), "galv", GALV, 0.3)
        M.add(rod("rail", (R - 0.8, -0.5, H + 1.0), (R - 0.8, 0.5, H + 1.0), 0.025), "galv", GALV, 0.3)
    return M


def feedbin(name, twin, lod):
    """A galvanised hopper feed bin, 7 m: legs, a cone, a corrugated drum, a cone roof with its lid; and a pair."""
    M = M_(name)
    seg = 16 if lod == 0 else 8
    for x in ((-1.6, 1.6) if twin else (0.0,)):
        prof = [(0.12, 1.6), (1.35, 3.0), (1.35, 6.3), (0.25, 6.95), (0.0, 7.0)]
        M.add(lathe("bin", [(0.0, 1.6)] + prof, seg, at=(x, 0.0, 0.0)), "galv", GALV, 0.3)
        if lod == 0:
            for k in range(4):
                a = math.pi / 4 + k * math.pi / 2
                M.add(rod("leg", (x + math.cos(a) * 1.3, math.sin(a) * 1.3, 0.0), (x + math.cos(a) * 1.25, math.sin(a) * 1.25, 3.05), 0.05, 6), "galv", GALV_DULL, 0.5)
            for z in (3.6, 4.4, 5.2, 6.0):
                M.add(lathe("band", [(1.36, z - 0.03), (1.38, z - 0.03), (1.38, z + 0.03), (1.36, z + 0.03)], seg, at=(x, 0.0, 0.0)), "galv", GALV_DULL, 0.3)
            M.add(rod("auger", (x, 0.0, 1.5), (x + 3.0, 0.0, 0.9), 0.07, 6), "galv", GALV_DULL, 0.3)
            for y in (-0.22, 0.22):
                M.add(rod("ladder", (x - 1.4, y, 3.0), (x - 1.4, y, 6.8), 0.025), "galv", GALV, 0.3)
        else:
            M.add(box("legs", (x, 0.0, 1.5), (2.0, 2.0, 3.0)), "galv", GALV_DULL, 0.5)
    return M


def trough(name, concrete, lod):
    M = M_(name)
    if concrete:
        M.add(box("trough", (0.0, 0.0, 0.3), (2.4, 0.8, 0.6), bevel=0.04 if lod == 0 else 0.0), "concrete", CONCRETE, 0.7)
    else:
        M.add(box("trough", (0.0, 0.0, 0.35), (2.0, 0.6, 0.5), bevel=0.06 if lod == 0 else 0.0), "galv", GALV, 0.5)
        if lod == 0:
            for x in (-0.85, 0.85):
                for y in (-0.25, 0.25):
                    M.add(rod("leg", (x, y, 0.0), (x, y, 0.15), 0.025), "galv", GALV_DULL, 0.6)
    M.add(box("water", (0.0, 0.0, 0.56), (1.9 if not concrete else 2.2, 0.5 if not concrete else 0.6, 0.02)), "glaze", WATER, 0.0)
    return M


def wheel(M, x, y, z, r, w, hub, lod, axis_y=True):
    seg = 18 if lod == 0 else 8
    prof = [(0.0, -w / 2), (r * 0.6, -w / 2), (r * 0.92, -w / 2), (r, -w / 2 + w * 0.15), (r, w / 2 - w * 0.15), (r * 0.92, w / 2), (0.0, w / 2)]
    ob = lathe("tyre", prof, seg)
    ob.matrix_world = Matrix.Translation((x, y, z)) @ Matrix.Rotation(math.pi / 2, 4, "X")
    M.add(ob, "paint", TYRE, 0.6)
    if lod == 0:
        for s in (-1, 1):
            h = lathe("hub", [(0.0, 0.0), (r * 0.62, 0.0), (r * 0.62, 0.03), (0.0, 0.03)], 12)
            h.matrix_world = Matrix.Translation((x, y + s * (w / 2 + 0.002), z)) @ Matrix.Rotation(-s * math.pi / 2, 4, "X")
            M.add(h, "paint", hub, 0.4)
        # (lugs on the tread: a few dark ribs)
        for k in range(14):
            a = 2 * math.pi * k / 14
            M.add(box("lug", (x + math.cos(a) * r, y, z + math.sin(a) * r), (0.06, w * 0.9, 0.05), 0.0,
                      rot=Matrix.Rotation(-a, 4, "Y")), "paint", TYRE, 0.6)


def tractor(name, rgb, loader, lod):
    """A made-up four-wheel-drive tractor, 2.95 m to the cab roof: bonnet, cab, big rear wheels."""
    M = M_(name)
    # (front +x)
    wheel(M, -1.05, 0.95, 0.85, 0.85, 0.6, HUB_YELLOW if rgb != GREEN else HUB_GREY, lod)
    wheel(M, -1.05, -0.95, 0.85, 0.85, 0.6, HUB_YELLOW if rgb != GREEN else HUB_GREY, lod)
    wheel(M, 1.55, 0.85, 0.6, 0.6, 0.45, HUB_YELLOW if rgb != GREEN else HUB_GREY, lod)
    wheel(M, 1.55, -0.85, 0.6, 0.6, 0.45, HUB_YELLOW if rgb != GREEN else HUB_GREY, lod)
    M.add(box("chassis", (0.3, 0.0, 0.75), (3.6, 0.7, 0.5)), "paint", GREY_DARK, 0.5)
    M.add(box("bonnet", (1.25, 0.0, 1.35), (2.0, 0.85, 0.75), bevel=0.06 if lod == 0 else 0.0), "enamel", rgb, 0.2)
    M.add(box("cabbase", (-0.85, 0.0, 1.3), (1.6, 1.3, 0.6)), "enamel", rgb, 0.3)
    # (the cab: four posts, glass all round, a roof)
    if lod == 0:
        for x in (-1.6, -0.1):
            for y in (-0.62, 0.62):
                M.add(box("post", (x, y, 2.15), (0.07, 0.07, 1.6)), "paint", GREY_DARK, 0.2)
        M.add(box("glass", (-0.85, 0.0, 2.15), (1.45, 1.2, 1.55)), "lens", GLASS, 0.0)
        M.add(box("fender", (-1.05, 0.95, 1.75), (1.6, 0.62, 0.08)), "enamel", rgb, 0.3)
        M.add(box("fender", (-1.05, -0.95, 1.75), (1.6, 0.62, 0.08)), "enamel", rgb, 0.3)
        M.add(rod("exhaust", (0.55, 0.3, 1.6), (0.55, 0.3, 2.9), 0.05, 8), "steel", GREY_DARK, 0.3)
        M.add(box("grille", (2.26, 0.0, 1.3), (0.04, 0.7, 0.55)), "paint", GREY_DARK, 0.3)
        for y in (-0.3, 0.3):
            M.add(box("lamp", (2.27, y, 1.55), (0.03, 0.18, 0.1)), "lens", (0.5, 0.5, 0.45), 0.0)
    else:
        M.add(box("glass", (-0.85, 0.0, 2.15), (1.5, 1.3, 1.55)), "lens", GLASS, 0.0)
    M.add(box("roof", (-0.85, 0.0, 2.92), (1.75, 1.45, 0.06)), "enamel", rgb if rgb != GREEN else (0.3, 0.3, 0.28), 0.2)
    if loader:
        for y in (-0.7, 0.7):
            M.add(sweep("boom", [(-0.4, y, 1.7), (0.9, y, 1.9), (2.4, y, 1.2), (2.9, y, 0.5)], 0.08, 6, square=True), "enamel", rgb, 0.4)
        M.add(box("bucket", (3.1, 0.0, 0.4), (0.5, 2.0, 0.6)), "enamel", GREY_DARK, 0.6)
    return M


def trailer(name, bales, lod):
    """A twin-axle grain trailer with high sides (red), or a flat bale trailer loaded with square bales."""
    M = M_(name)
    for x in (-0.6, 0.6):
        for y in (-0.95, 0.95):
            wheel(M, x - 0.8, y, 0.5, 0.5, 0.4, HUB_GREY, lod)
    M.add(box("drawbar", (3.0, 0.0, 0.65), (2.2, 0.2, 0.15)), "paint", GREY_DARK, 0.4)
    if bales:
        M.add(box("bed", (-0.6, 0.0, 1.05), (6.5, 2.5, 0.15)), "timber", (0.12, 0.08, 0.05), 0.5)
        for r in range(1):
            for c in range(3):
                for k in range(2):
                    square_bale(M, -2.6 + c * 2.0 + (0.0 if lod == 0 else 0.0), (k - 0.5) * 1.22, 1.12 + r * 0.9, (1.95, 1.2, 0.88) if lod == 0 else (1.95, 1.2, 0.88), STRAW, lod)
        M.add(box("headboard", (2.6, 0.0, 1.6), (0.08, 2.5, 1.1)), "paint", RED, 0.4)
    else:
        M.add(box("body", (-0.6, 0.0, 1.55), (6.0, 2.4, 1.15), bevel=0.03 if lod == 0 else 0.0), "enamel", RED, 0.4)
        if lod == 0:
            for x in (-3.0, -1.5, 0.0, 1.5):
                for y in (-1.21, 1.21):
                    M.add(box("rib", (x + 0.6 - 0.6, y, 1.55), (0.06, 0.03, 1.1)), "enamel", (0.25, 0.02, 0.015), 0.4)
            M.add(box("grain", (-0.6, 0.0, 2.08), (5.8, 2.2, 0.06)), "timber", (0.4, 0.3, 0.12), 0.2)
    return M


def diesel(name, lod):
    M = M_(name)
    M.add(box("bund", (0.0, 0.0, 0.85), (2.2, 1.3, 1.7), bevel=0.05 if lod == 0 else 0.0), "enamel", TANK_GREEN, 0.4)
    if lod == 0:
        M.add(box("cabinet", (1.15, 0.0, 1.1), (0.12, 0.6, 0.7)), "enamel", (0.1, 0.1, 0.1), 0.4)
        M.add(sweep("hose", [(1.2, 0.2, 1.0), (1.4, 0.3, 0.5), (1.3, 0.1, 0.3), (1.22, -0.1, 1.2)], 0.025, 6), "paint", TYRE, 0.4)
        M.add(rod("vent", (-0.8, 0.4, 1.7), (-0.8, 0.4, 1.8), 0.04), "steel", GALV_DULL, 0.3)
    return M


def muck(name, stones, lod):
    """A heap: a rough mound, lower and lumpier for field stones."""
    M = M_(name)
    random.seed(11 if stones else 3)
    seg = 14 if lod == 0 else 7
    rings = 5 if lod == 0 else 3
    verts, faces = [], []
    L, Wd, H = (6.0, 4.0, 1.8) if not stones else (5.0, 3.5, 1.8)
    for i in range(rings + 1):
        t = i / rings
        z = H * math.sin(t * math.pi / 2) if not stones else H * t ** 0.8
        sc = math.cos(t * math.pi / 2) if i < rings else 0.0
        for k in range(seg):
            a = 2 * math.pi * k / seg
            j = random.uniform(0.85, 1.15) if 0 < i < rings else 1.0
            verts.append((math.cos(a) * L / 2 * sc * j, math.sin(a) * Wd / 2 * sc * j, z if i < rings else H))
    for i in range(rings):
        for k in range(seg):
            a, b = i * seg + k, i * seg + (k + 1) % seg
            faces.append((a, b, b + seg, a + seg))
    faces.append(tuple(reversed(range(seg))))
    ob = K._object("heap", verts, faces, 60.0)
    M.add(ob, "concrete" if stones else "timber", STONES if stones else MUCK, 0.6)
    if lod == 0 and not stones:
        for k in range(10):
            a = random.uniform(0, 2 * math.pi)
            r = random.uniform(0.2, 0.8)
            M.add(box("straw", (math.cos(a) * L / 2 * r * 0.8, math.sin(a) * Wd / 2 * r * 0.8, H * (1 - r * r) * 0.9 + 0.05), (0.6, 0.35, 0.08), a), "timber", MUCK_STRAW, 0.5)
    return M


def build(lod):
    """Every model at lod (0 the model, 1 its distance version)."""
    s = "" if lod == 0 else "_LOD1"
    return [
        bales_pyramid("SM_Prop_farm_bales_0" + s, STRAW, False, lod),
        bales_square("SM_Prop_farm_bales_1" + s, lod),
        bales_pyramid("SM_Prop_farm_bales_2" + s, WRAP_BLACK, True, lod),
        bale_one("SM_Prop_farm_bale_0" + s, STRAW, False, lod),
        bale_one("SM_Prop_farm_bale_1" + s, WRAP_BLACK, True, lod),
        bale_one("SM_Prop_farm_bale_2" + s, WRAP_GREEN, True, lod),
        clamp("SM_Prop_farm_clamp_0" + s, False, lod),
        clamp("SM_Prop_farm_clamp_1" + s, True, lod),
        tank("SM_Prop_farm_tank_0" + s, False, lod),
        tank("SM_Prop_farm_tank_1" + s, True, lod),
        feedbin("SM_Prop_farm_feedbin_0" + s, False, lod),
        feedbin("SM_Prop_farm_feedbin_1" + s, True, lod),
        trough("SM_Prop_farm_trough_0" + s, False, lod),
        trough("SM_Prop_farm_trough_1" + s, True, lod),
        tractor("SM_Prop_farm_tractor_0" + s, GREEN, False, lod),
        tractor("SM_Prop_farm_tractor_1" + s, RED, False, lod),
        tractor("SM_Prop_farm_tractor_2" + s, BLUE, True, lod),
        trailer("SM_Prop_farm_trailer_0" + s, False, lod),
        trailer("SM_Prop_farm_trailer_1" + s, True, lod),
        diesel("SM_Prop_farm_diesel_0" + s, lod),
        muck("SM_Prop_farm_muck_0" + s, False, lod),
        muck("SM_Prop_farm_muck_1" + s, True, lod),
    ]


def main():
    args = sys.argv[1:]
    out = {}
    for lod in (0, 1):
        K.clear()
        models = build(lod)
        for M in models:
            nt, y0, y1, x0, x1, z0, z1 = K.write(M)
            out[M.name] = (nt, y0, y1)
            print("%-28s %5d triangles, %.2f..%.2f m up, %.1f x %.1f m" % (M.name, nt, y0, y1, x1 - x0, z1 - z0))
        if lod == 0 and "--render" in args:
            render(models, args[args.index("--render") + 1])
    # (each kind's height: its models' tallest; they must agree, or they'd be scaled to fit)
    kinds = {}
    for name, (nt, y0, y1) in out.items():
        if name.endswith("_LOD1"):
            continue
        kind = name[len("SM_Prop_"):].rsplit("_", 1)[0]
        kinds.setdefault(kind, []).append(y1 - y0)
    kit = json.load(open(K.KIT))
    lines = open(K.KIT).read().split("\n")
    for kind, hs in kinds.items():
        if max(hs) - min(hs) > 0.06:
            print("note: %s models differ in height %s: scaled to %.2f" % (kind, [round(h, 2) for h in hs], max(hs)))
        reach, shadow = KINDS[kind]
        row = {"words": [], "reach": reach, "height": round(float(max(hs)), 3), "shadow": shadow, "credit": "", "hand_lods": [0.12]}
        head = '  "%s": ' % kind
        for i, ln in enumerate(lines):
            if ln.startswith(head):
                comma = ln.rstrip().endswith(",")
                lines[i] = head + json.dumps(row) + ("," if comma else "")
                break
        else:
            at = max(i for i, ln in enumerate(lines) if ln.startswith('  "') and ln.rstrip().endswith("},"))   # (after a whole one-line row)
            lines.insert(at + 1, head + json.dumps(row) + ",")
    open(K.KIT, "w").write("\n".join(lines))
    json.load(open(K.KIT))


def render(models, out_dir):
    """Each piece alone, for the farm kit's review sheet (Tools/blender_farm_kit.py --render puts them on it)."""
    import blender_farm_kit_render as R
    R.render_props(models, out_dir)


if __name__ == "__main__":
    main()
