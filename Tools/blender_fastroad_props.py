"""Ride UK's fast road set (the everyday kits plan, stage 8): the steel and concrete crash barriers of dual carriageways
and motorways, their sign gantries, yellow speed cameras, emergency phones and marker posts, modelled in Blender. Our
own designs, from general knowledge of British trunk roads: no makers' names, numbers or lettering.

  fast_barrier 0..1    a 4 m length of steel barrier along x (a corrugated beam on posts every 2 m, at its middle and
                       its +x end): 0 one beam, on the -y side (laid with the road on its -y side: a verge's), 1 a beam
                       each side (a central reservation's)
  fast_concrete 0      a 4 m length of concrete step barrier along x, 0.9 m high
  fast_gantry 0..2     a sign gantry 14, 20 or 27 m across y (two, three or four lanes and the hard shoulder): two
                       lattice legs and a box truss, a blank signal panel over each lane, 7.6 m tall; laid by the
                       nearest span and stretched the last little way to fit (Across)
  fast_camera 0        a yellow speed camera: its box on a grey pole, its lens to the front (+x), 3.4 m
  fast_phone 0         an emergency phone: an orange box under a hood on a short post, its door to +x
  fast_marker 0        a marker post: a white post with a red reflector to its front, 1.1 m

Each with two hand-made distance versions (SM_Prop_<kind>_<n>_LOD1.glb and _LOD2.glb: Tools/ue_build_rideuk.py puts
them in as the mesh's LODs; never reduced by the engine); each kind's height and reach in Data/RideUK/prop_kit.json.
Same material and frame as the street kit (Tools/blender_street_kit.py: M_RideStreetKit, front +x, +y its left, foot
at 0). World/WorldRideFastRoads.cpp lays them.

    python Tools/blender_fastroad_props.py                # the models
    python Tools/blender_fastroad_props.py --render DIR   # and the review sheet's pictures in DIR
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

STEEL = (0.36, 0.37, 0.38)
POST = (0.28, 0.28, 0.29)
CONCRETE = (0.4, 0.39, 0.36)
GANTRY = (0.4, 0.41, 0.42)
YELLOW = (0.7, 0.5, 0.02)
GREY = (0.3, 0.31, 0.32)
ORANGE = (0.7, 0.22, 0.02)
WHITE = (0.75, 0.75, 0.73)
DARK = (0.03, 0.03, 0.035)

# kind: (reach in m, casts a shadow, hand-made LODs' screen sizes)
KINDS = {"fast_barrier": (250, False, [0.1, 0.025]), "fast_concrete": (300, True, [0.1, 0.025]),
         "fast_gantry": (1200, True, [0.2, 0.05]), "fast_camera": (400, True, [0.12, 0.03]),
         "fast_phone": (200, True, [0.12, 0.03]), "fast_marker": (150, False, [0.12, 0.03])}


def wbeam(name, y, side, L, lod):
    """A corrugated beam along x at y, 0.45 to 0.78 m up, its face to side (+1: +y, -1: -y): two ridges at LOD0."""
    n = (6, 2, 1)[lod]
    if n == 1:
        return box(name, (0.0, y + side * 0.01, 0.615), (L, 0.02, 0.33))
    # (a cross-section of ridges and a valley, swept along x)
    prof = [(0.45, 0.0), (0.5, 0.04), (0.56, 0.06), (0.6, 0.02), (0.63, 0.02), (0.67, 0.06), (0.73, 0.04), (0.78, 0.0)]
    if lod == 1:
        prof = [(0.45, 0.0), (0.55, 0.05), (0.615, 0.02), (0.68, 0.05), (0.78, 0.0)]
    verts, faces = [], []
    for x in (-L / 2, L / 2):
        for z, d in prof:
            verts.append((x, y + side * d, z))
    m = len(prof)
    for i in range(m - 1):
        faces.append((i, i + 1, m + i + 1, m + i))
    verts += [(v[0], v[1] - side * 0.005, v[2]) for v in verts]
    k = 2 * m
    for i in range(m - 1):
        faces.append((k + i, k + m + i, k + m + i + 1, k + i + 1))
    return K._object(name, verts, faces, 50.0)


def barrier(name, double, lod):
    M = K.Model(name)
    L = 4.0
    for x in (0.0, L / 2 - 0.04):
        M.add(box("post", (x, 0.0, 0.27), (0.08, 0.12, 0.94)), "galv", POST, 0.5)
        if lod == 0:
            for sy in ((-1, 1) if double else (-1,)):
                M.add(box("block", (x, sy * 0.1, 0.62), (0.06, 0.1, 0.25)), "galv", POST, 0.5)
    for sy in ((-1, 1) if double else (-1,)):
        M.add(wbeam("beam", sy * 0.17, sy, L, lod), "galv", STEEL, 0.35)
        if lod == 0:
            M.add(box("bolt", (L / 2 - 0.04, sy * 0.185, 0.615), (0.08, 0.01, 0.05)), "galv", POST, 0.4)
    if not double and lod < 2:
        M.add(box("blank", (0.0, 0.17, 0.615), (0.01, 0.01, 0.01)), "galv", STEEL, 0.4)
    return M


def concrete(name, lod):
    M = K.Model(name)
    L = 4.0
    # (the step barrier's section: 0.6 at its foot, a step at 0.25 m, 0.24 at its top 0.9 m up)
    sec = [(-0.3, -0.05), (-0.3, 0.075), (-0.2, 0.25), (-0.12, 0.9), (0.12, 0.9), (0.2, 0.25), (0.3, 0.075), (0.3, -0.05)]
    if lod == 2:
        sec = [(-0.3, -0.05), (-0.2, 0.25), (-0.12, 0.9), (0.12, 0.9), (0.2, 0.25), (0.3, -0.05)]
    verts = [(x, y, z) for x in (-L / 2, L / 2) for (y, z) in sec]
    m = len(sec)
    faces = [(i, i + 1, m + i + 1, m + i) for i in range(m - 1)]
    faces += [tuple(range(m - 1, -1, -1)), tuple(range(m, 2 * m))]
    M.add(K._object("body", verts, faces, 20.0), "concrete", CONCRETE, 0.55)
    if lod < 2:
        for sy in (-1, 1):
            M.add(box("joint", (L / 2 - 0.01, sy * 0.16, 0.5), (0.02, 0.01, 0.75)), "concrete", (0.2, 0.2, 0.19), 0.5)
    if lod == 0:
        for sy in (-1, 1):
            M.add(box("stain", (0.0, sy * 0.26, 0.12), (L, 0.01, 0.12), rot=Matrix.Rotation(sy * -0.47, 4, "X")), "concrete", (0.26, 0.25, 0.23), 0.8)
    return M


GANTRY_SPANS = (14.0, 20.0, 27.0)


def gantry(name, span, lod):
    """A sign gantry span m across (along y; laid a little stretched to its carriageway, Across): a lattice leg at each
    end, a box truss between them at 6.4 to 7.6 m, a blank signal panel over each lane's middle facing +x."""
    M = K.Model(name)
    H, T0 = 7.6, 6.4
    half = span / 2
    for sy in (-1, 1):
        y = sy * half
        for dx in (-0.35, 0.35):
            for dy in (-0.3, 0.3):
                M.add(rod("leg", (dx, y + dy, 0.0), (dx, y + dy, H), 0.07 if lod < 2 else 0.1, (8, 6, 4)[lod]), "galv", GANTRY, 0.35)
        if lod < 2:
            for z in [z * 1.2 + 0.6 for z in range(5)]:
                for dy in (-0.3, 0.3):
                    M.add(rod("brace", (-0.35, y + dy, z), (0.35, y + dy, z + 1.2), 0.035, 4), "galv", GANTRY, 0.35)
                for dx in (-0.35, 0.35):
                    M.add(rod("brace", (dx, y - 0.3, z + 0.6), (dx, y + 0.3, z + 1.8), 0.035, 4), "galv", GANTRY, 0.35)
        M.add(box("base", (0.0, y, 0.15), (1.3, 1.1, 0.3)), "concrete", CONCRETE, 0.6)
    # (the truss's chords, top and bottom, front and back; its diagonals; the walkway)
    for dx in (-0.4, 0.4):
        for z in (T0, H - 0.05):
            M.add(box("chord", (dx, 0.0, z), (0.1, span + 0.6, 0.1)), "galv", GANTRY, 0.35)
    if lod < 2:
        n = int(span / (1.2 if lod == 0 else 2.4))
        for i in range(n):
            ya, yb = -half + span * i / n, -half + span * (i + 1) / n
            for dx in (-0.4, 0.4):
                M.add(rod("diag", (dx, ya, T0), (dx, yb, H - 0.05), 0.03, 4), "galv", GANTRY, 0.35)
    M.add(box("walk", (0.65, 0.0, T0 - 0.05), (0.5, span, 0.04)), "galv", GANTRY, 0.35)
    if lod == 0:
        M.add(box("handrail", (0.9, 0.0, T0 + 1.0), (0.04, span, 0.04)), "galv", GANTRY, 0.35)
    lanes = max(2, int((span - 6.0) / 3.65))
    for k in range(lanes):
        y = (k - (lanes - 1) / 2) * 3.65
        M.add(box("panel", (0.48, y, T0 - 0.65), (0.15, 1.2, 1.2)), "paint", DARK, 0.3)
        if lod < 2:
            M.add(box("rim", (0.56, y, T0 - 0.65), (0.01, 1.28, 1.28)), "paint", GREY, 0.3)
        if lod == 0:
            M.add(box("hanger", (0.45, y, T0 - 0.02), (0.06, 0.8, 0.06)), "galv", GANTRY, 0.35)
    return M


def camera(name, lod):
    """A yellow speed camera: a box 0.45 wide, 0.8 tall and 0.6 deep on a grey pole, its dark lens and flash window to
    the front (+x), a sloping roof."""
    M = K.Model(name)
    seg = (10, 6, 4)[lod]
    M.add(rod("pole", (0.0, 0.0, -0.3), (0.0, 0.0, 2.7), 0.06, seg), "galv", GREY, 0.4)
    M.add(box("box", (0.0, 0.0, 2.95), (0.6, 0.45, 0.8), bevel=0.02 if lod == 0 else 0.0), "paint", YELLOW, 0.3)
    M.add(box("roof", (0.0, 0.0, 3.38), (0.7, 0.5, 0.06), rot=Matrix.Rotation(-0.12, 4, "Y")), "paint", YELLOW, 0.3)
    if lod < 2:
        M.add(box("window", (0.305, 0.0, 3.05), (0.01, 0.33, 0.42)), "lens", DARK, 0.1)
        M.add(box("bracket", (0.0, 0.0, 2.52), (0.16, 0.16, 0.08)), "galv", GREY, 0.4)
    if lod == 0:
        M.add(box("flash", (0.305, 0.0, 2.72), (0.01, 0.3, 0.1)), "lens", (0.5, 0.5, 0.5), 0.1)
        for sy in (-1, 1):
            M.add(box("stripe", (0.0, sy * 0.226, 2.95), (0.5, 0.005, 0.05)), "paint", DARK, 0.2)
    return M


def phone(name, lod):
    """An emergency phone: an orange box 0.45 wide and 0.6 tall under a hood, on a short post, its door to +x."""
    M = K.Model(name)
    M.add(box("post", (0.0, 0.0, 0.35), (0.12, 0.12, 0.8)), "galv", GREY, 0.5)
    M.add(box("box", (0.0, 0.0, 1.0), (0.3, 0.45, 0.6), bevel=0.02 if lod == 0 else 0.0), "enamel", ORANGE, 0.3)
    M.add(box("hood", (0.04, 0.0, 1.33), (0.42, 0.55, 0.05)), "enamel", ORANGE, 0.3)
    if lod < 2:
        M.add(box("door", (0.155, 0.0, 0.98), (0.01, 0.36, 0.48)), "enamel", (0.6, 0.18, 0.02), 0.3)
    if lod == 0:
        M.add(box("handle", (0.165, 0.14, 0.98), (0.02, 0.03, 0.1)), "galv", GREY, 0.3)
        M.add(box("plate", (0.162, 0.0, 1.16), (0.004, 0.22, 0.06)), "paint", WHITE, 0.2)
    return M


def marker(name, lod):
    """A marker post: a white post 1.1 m tall, its top bevelled, a black band, a red reflector to its front."""
    M = K.Model(name)
    M.add(box("post", (0.0, 0.0, 0.5), (0.12, 0.12, 1.2)), "paint", WHITE, 0.35)
    if lod < 2:
        M.add(box("band", (0.0, 0.0, 0.9), (0.125, 0.125, 0.15)), "paint", DARK, 0.2)
    if lod == 0:
        M.add(box("reflector", (0.064, 0.0, 0.92), (0.006, 0.05, 0.1)), "lens", (0.55, 0.02, 0.02), 0.1)
        M.add(box("cap", (0.0, 0.0, 1.1), (0.12, 0.12, 0.02), rot=Matrix.Rotation(0.3, 4, "Y")), "paint", WHITE, 0.2)
    return M


def build(lod):
    s = ("", "_LOD1", "_LOD2")[lod]
    return [barrier("SM_Prop_fast_barrier_0" + s, False, lod), barrier("SM_Prop_fast_barrier_1" + s, True, lod),
            concrete("SM_Prop_fast_concrete_0" + s, lod)] + \
           [gantry("SM_Prop_fast_gantry_%d" % v + s, sp, lod) for v, sp in enumerate(GANTRY_SPANS)] + \
           [camera("SM_Prop_fast_camera_0" + s, lod), phone("SM_Prop_fast_phone_0" + s, lod),
            marker("SM_Prop_fast_marker_0" + s, lod)]


def main():
    import blender_farm_props as FP
    import blender_roadside_props as RS
    args = sys.argv[1:]
    out = {}
    for lod in (0, 1, 2):
        K.clear()
        models = build(lod)
        if lod == 0 and "--render" in args:
            RS.render_sheet(models, args[args.index("--render") + 1],
                            "Ride UK's fast road set (our own models; each with two hand-made distance versions)",
                            "fastroad_props.png", cols=4, size=(420, 320))
        for M in models:
            nt, y0, y1, x0, x1, z0, z1 = K.write(M)
            out[M.name] = (nt, y0, y1)
            print("%-34s %6d triangles, %.2f..%.2f m up, %.2f x %.2f m" % (M.name, nt, y0, y1, x1 - x0, z1 - z0))
    FP.KINDS = KINDS
    FP.write_kinds(out)


if __name__ == "__main__":
    main()
