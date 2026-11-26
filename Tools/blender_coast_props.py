"""Ride UK's coast set (the everyday kits plan, stage 10): beach huts, sea wall parapets, promenade railings and
lifebuoy housings, modelled in Blender. Our own designs, from general knowledge of British seafronts: no names,
numbers, council crests or lettering.

Writes Data/RideUK/Props/SM_Prop_<kind>_<n>.glb, each with two distance versions made here by hand
(SM_Prop_<kind>_<n>_LOD1.glb and _LOD2.glb: Tools/ue_build_rideuk.py puts them in as the mesh's LODs; never reduced
by the engine), and sets each kind's height and reach in Data/RideUK/prop_kit.json. Same material and frame as the
street kit (Tools/blender_street_kit.py: M_RideStreetKit, front +x, foot at 0); all the models of a kind are the same
height. World/WorldRideCoast.cpp lays them from the kits bake's coast things (kits/cROW_COL.json) and the map's
beach hut buildings.

  coast_beachhut 0..5   a timber beach hut 2.7 m to its ridge, its doors at its front (+x): six paint schemes,
                        pitched or with a bargeboard gable, planked sides
  coast_seawall 0       a 4 m length of concrete sea wall parapet along x, 1.0 m high, its coping and joint
  coast_promrail 0..1   a 2.5 m panel of promenade railing along x, 1.05 m: painted cast iron (blue, green)
  coast_lifebuoy 0      a lifebuoy housing on its post, its door to the front (+x)

    python Tools/blender_coast_props.py                # the models
    python Tools/blender_coast_props.py --render DIR   # and the review sheet's pictures in DIR
"""
import math
import os
import sys

import bpy  # noqa: F401  (before mathutils)
from mathutils import Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_street_kit as K   # noqa: E402
from blender_street_kit import box, lathe, rod   # noqa: E402

WHITE = (0.72, 0.72, 0.7)
CONCRETE = (0.4, 0.39, 0.36)
CONCRETE_DARK = (0.3, 0.29, 0.27)
RAIL_BLUE = (0.03, 0.12, 0.3)
RAIL_GREEN = (0.03, 0.15, 0.08)
BUOY_RED = (0.55, 0.04, 0.02)
BUOY_YELLOW = (0.68, 0.48, 0.02)
FELT = (0.04, 0.045, 0.045)
# (each hut: walls, doors and trim, its roof)
HUTS = [
    ((0.06, 0.2, 0.42), WHITE, (0.3, 0.07, 0.05)),       # blue, white trim, red roof
    ((0.62, 0.5, 0.06), WHITE, FELT),                    # yellow
    ((0.5, 0.1, 0.12), WHITE, FELT),                     # red
    ((0.1, 0.35, 0.2), (0.7, 0.66, 0.5), FELT),          # green, cream trim
    ((0.62, 0.62, 0.6), (0.05, 0.16, 0.4), (0.05, 0.16, 0.4)),   # white, blue trim and roof
    ((0.55, 0.3, 0.38), WHITE, FELT),                    # pink
]
HUT_H = 2.7

# kind: (reach in m, casts a shadow, hand-made LODs' screen sizes)
KINDS = {"coast_beachhut": (350, True, [0.15, 0.04]), "coast_seawall": (400, True, [0.12, 0.03]),
         "coast_promrail": (250, False, [0.12, 0.03]), "coast_lifebuoy": (150, True, [0.12, 0.03])}


def beachhut(name, scheme, lod):
    """A hut 2.0 m wide (y) and 2.4 deep (x), walls 2.0 m to the eaves on a low deck, a pitched roof to 2.7 m, double
    doors at the front with its gable over them, planks on its sides."""
    M = K.Model(name)
    wall, trim, roof = HUTS[scheme]
    W, D, E = 2.0, 2.4, 2.0
    deck = 0.25
    M.add(box("deck", (0.35, 0.0, deck / 2), (D + 0.7, W + 0.1, deck)), "timber", (0.24, 0.17, 0.1), 0.5)
    M.add(box("body", (0.0, 0.0, deck + (E - deck) / 2), (D, W, E - deck)), "paint", wall, 0.3)
    # (the gables, front and back, and the roof's two slopes from eaves to ridge)
    rise = HUT_H - E - 0.05
    for sx in (-1, 1):
        verts = [(sx * D / 2, -W / 2, E), (sx * D / 2, W / 2, E), (sx * D / 2, 0.0, E + rise),
                 (sx * (D / 2 - 0.04), -W / 2, E), (sx * (D / 2 - 0.04), W / 2, E), (sx * (D / 2 - 0.04), 0.0, E + rise)]
        M.add(K._object("gable", verts, [(0, 1, 2), (5, 4, 3), (0, 3, 4, 1), (1, 4, 5, 2), (2, 5, 3, 0)], 30.0), "paint", wall, 0.25)
    over = 0.12
    slope = math.atan2(rise, W / 2)
    L = math.hypot(rise, W / 2) + over
    for sy in (-1, 1):
        R = Matrix.Rotation(-sy * slope, 4, "X")
        cy = sy * (W / 4 + over / 2 * math.cos(slope)) * 1.0
        M.add(box("roof", (0.0, cy, E + rise / 2 + 0.04), (D + 0.3, L, 0.05), rot=R), "paint", roof, 0.3)
    if lod < 2:
        # (the bargeboards up the front gable, the doors and their frame)
        for sy in (-1, 1):
            R = Matrix.Rotation(-sy * slope, 4, "X")
            M.add(box("barge", (D / 2 + 0.17, sy * W / 4, E + rise / 2), (0.04, math.hypot(rise, W / 2) + 0.1, 0.14), rot=R), "paint", trim, 0.25)
        M.add(box("doorframe", (D / 2 + 0.01, 0.0, deck + 0.95), (0.03, 1.5, 1.8)), "paint", trim, 0.3)
        M.add(box("doors", (D / 2 + 0.02, 0.0, deck + 0.9), (0.03, 1.36, 1.68)), "paint", wall, 0.3)
        M.add(box("doorgap", (D / 2 + 0.036, 0.0, deck + 0.9), (0.005, 0.02, 1.68)), "paint", (0.02, 0.02, 0.02), 0.2)
        for sy in (-1, 1):
            M.add(box("corner", (D / 2 - 0.02, sy * (W / 2 - 0.02), deck + (E - deck) / 2), (0.08, 0.08, E - deck)), "paint", trim, 0.3)
            M.add(box("corner", (-D / 2 + 0.02, sy * (W / 2 - 0.02), deck + (E - deck) / 2), (0.08, 0.08, E - deck)), "paint", trim, 0.3)
    if lod == 0:
        # (planks: a shadow line every 0.15 m up the sides and back)
        z = deck + 0.15
        while z < E - 0.05:
            for sy in (-1, 1):
                M.add(box("plank", (0.0, sy * (W / 2 + 0.006), z), (D - 0.1, 0.012, 0.025)), "paint", tuple(c * 0.75 for c in wall), 0.3)
            M.add(box("plank", (-D / 2 - 0.006, 0.0, z), (0.012, W - 0.1, 0.025)), "paint", tuple(c * 0.75 for c in wall), 0.3)
            z += 0.15
        M.add(box("hasp", (D / 2 + 0.045, 0.1, deck + 1.0), (0.02, 0.12, 0.05)), "steel", (0.3, 0.3, 0.3), 0.3)
        M.add(box("vent", (D / 2 + 0.01, 0.0, E + rise * 0.4), (0.04, 0.3, 0.12)), "paint", trim, 0.2)
    return M


def seawall(name, lod):
    """A 4 m length of sea wall parapet along x: a concrete wall 0.45 m thick and 1.0 m high, its coping, a joint."""
    M = K.Model(name)
    L = 4.0
    M.add(box("wall", (0.0, 0.0, 0.4), (L, 0.45, 1.3)), "concrete", CONCRETE, 0.55)
    M.add(box("coping", (0.0, 0.0, 1.0 - 0.06), (L, 0.58, 0.12), bevel=0.02 if lod == 0 else 0.0), "concrete", CONCRETE_DARK, 0.5)
    if lod < 2:
        for sy in (-1, 1):
            M.add(box("joint", (L / 2 - 0.01, sy * 0.23, 0.45), (0.02, 0.01, 0.9)), "paint", (0.08, 0.08, 0.08), 0.3)
    if lod == 0:
        for sy in (-1, 1):
            M.add(box("plinth", (0.0, sy * 0.27, 0.12), (L, 0.1, 0.24)), "concrete", CONCRETE, 0.7)
    return M


def promrail(name, rgb, lod):
    """A 2.5 m panel of promenade railing along x, 1.05 m high: a post at +x with its ball top, top and bottom rails,
    bars, and a ring of scrolls under the top rail."""
    M = K.Model(name)
    L, H = 2.5, 1.05
    seg = (12, 6, 4)[lod]
    M.add(lathe("post", [(0.07, -0.1), (0.07, 0.15), (0.045, 0.2), (0.04, H - 0.05), (0.05, H), (0.0, H)], seg, at=(L / 2, 0, 0)), "paint", rgb, 0.3)
    if lod < 2:
        M.add(lathe("ball", [(0.0, H), (0.05, H + 0.04), (0.0, H + 0.09)], seg, at=(L / 2, 0, 0)), "paint", rgb, 0.3)
    else:
        M.add(box("ballbox", (L / 2, 0.0, H + 0.04), (0.08, 0.08, 0.08)), "paint", rgb, 0.3)
    M.add(box("top", (0.0, 0.0, H - 0.03), (L, 0.07, 0.05)), "paint", rgb, 0.3)
    M.add(box("bottom", (0.0, 0.0, 0.12), (L, 0.04, 0.04)), "paint", rgb, 0.3)
    if lod == 2:
        M.add(box("infill", (0.0, 0.0, 0.5), (L, 0.01, 0.7)), "paint", rgb, 0.3)
        return M
    M.add(box("mid", (0.0, 0.0, H - 0.25), (L, 0.03, 0.03)), "paint", rgb, 0.3)
    n = 20 if lod == 0 else 8
    for k in range(n):
        x = -L / 2 + (k + 0.5) * L / n
        M.add(box("bar", (x, 0.0, 0.48), (0.018 if lod == 0 else 0.03, 0.018, 0.72)), "paint", rgb, 0.3)
    if lod == 0:
        for k in range(10):
            x = -L / 2 + (k + 0.5) * L / 10
            ob = lathe("ring", [(0.05, -0.01), (0.06, 0.0), (0.05, 0.01)], 10)
            ob.matrix_world = Matrix.Translation((x, 0.0, H - 0.14)) @ Matrix.Rotation(math.radians(90), 4, "X")
            M.add(ob, "paint", rgb, 0.3)
    return M


def lifebuoy(name, lod):
    """A lifebuoy housing: a red cabinet on a post, its yellow door to the front (+x), the ring inside showing."""
    M = K.Model(name)
    M.add(rod("post", (0.0, 0.0, -0.3), (0.0, 0.0, 0.95), 0.05, (8, 6, 4)[lod]), "paint", BUOY_RED, 0.3)
    M.add(box("case", (0.0, 0.0, 1.35), (0.25, 0.85, 0.85), bevel=0.02 if lod == 0 else 0.0), "enamel", BUOY_RED, 0.3)
    M.add(box("hood", (0.0, 0.0, 1.8), (0.32, 0.92, 0.05)), "enamel", BUOY_RED, 0.3)
    if lod < 2:
        M.add(box("door", (0.13, 0.0, 1.35), (0.02, 0.7, 0.7)), "enamel", BUOY_YELLOW, 0.25)
    if lod == 0:
        ob = lathe("buoy", [(0.2, -0.04), (0.28, -0.05), (0.3, 0.0), (0.28, 0.05), (0.2, 0.04), (0.18, 0.0), (0.2, -0.04)], 16)
        ob.matrix_world = Matrix.Translation((0.15, 0.0, 1.35)) @ Matrix.Rotation(math.radians(90), 4, "Y")
        M.add(ob, "paint", (0.75, 0.42, 0.05), 0.2)
    return M


def build(lod):
    s = ("", "_LOD1", "_LOD2")[lod]
    out = [beachhut("SM_Prop_coast_beachhut_%d%s" % (i, s), i, lod) for i in range(len(HUTS))]
    out += [seawall("SM_Prop_coast_seawall_0" + s, lod),
            promrail("SM_Prop_coast_promrail_0" + s, RAIL_BLUE, lod),
            promrail("SM_Prop_coast_promrail_1" + s, RAIL_GREEN, lod),
            lifebuoy("SM_Prop_coast_lifebuoy_0" + s, lod)]
    return out


def main():
    import blender_farm_props as FP
    args = sys.argv[1:]
    out = {}
    for lod in (0, 1, 2):
        K.clear()
        models = build(lod)
        for M in models:
            nt, y0, y1, x0, x1, z0, z1 = K.write(M)
            out[M.name] = (nt, y0, y1)
            print("%-34s %6d triangles, %.2f..%.2f m up, %.1f x %.1f m" % (M.name, nt, y0, y1, x1 - x0, z1 - z0))
        if lod == 0 and "--render" in args:
            import blender_farm_kit_render as R
            R.render_props(models, args[args.index("--render") + 1],
                           title="Ride UK's coast set (our own models; each with two hand-made distance versions)",
                           sheet_name="coast_props.png")
    FP.KINDS = KINDS
    FP.write_kinds(out)


if __name__ == "__main__":
    main()
