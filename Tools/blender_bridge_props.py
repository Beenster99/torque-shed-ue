"""Ride UK's bridge set (the everyday kits plan, stage 7): what holds a road's bridge up and lines its edges, modelled in
Blender. Our own designs, from general knowledge of British bridges: no names, plates or lettering.

The road build draws a bridge's deck (its tarmac, its slab's faces and underside) and, on a country road, its walls
(the dry stone wall kit's). These stand under and along it (World/WorldRideBridgeKit.cpp lays them from the road's
deck rows, Out.Props of the road's piece):

  bridge_arch 0..5     a stone arch bridge's body between its deck and the ground, one arch: its two spandrel faces
                       with the arch ring and keystone, a string course, the barrel under, its piers at its ends.
                       Made 1 m each way and laid stretched to the span along x, across the deck along y and down to
                       the ground (Scale), several side by side for a longer bridge. Six walling stones (the wall
                       kit's styles: Dales limestone, Cotswold, slate, gritstone, granite, sandstone).
  bridge_pier 0..1     a concrete pier, 1 m tall and laid scaled to the deck's underside: 0 two round columns under a
                       crosshead (over land and roads), 1 a wall pier with rounded ends (in water)
  bridge_abutment 0    a concrete abutment, 1 m: its face to +x, its bearing shelf, wing walls back from either side
  bridge_rail 0        a modern bridge's parapet, a 2.5 m length along x: a concrete plinth, steel posts at its ends,
                       three rails and bars (a wide road's, a dual carriageway's or a motorway's bridge)

Each with two hand-made distance versions (SM_Prop_<kind>_<n>_LOD1.glb and _LOD2.glb: Tools/ue_build_rideuk.py puts
them in as the mesh's LODs; never reduced by the engine); each kind's height and reach in Data/RideUK/prop_kit.json.
Same material and frame as the street kit (Tools/blender_street_kit.py: M_RideStreetKit, front +x, foot at 0).

    python Tools/blender_bridge_props.py                # the models
    python Tools/blender_bridge_props.py --render DIR   # and the review sheet's pictures in DIR
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

# (the wall kit's walling, in its order: World/WorldRide.cpp WallStyle)
STONES = [
    (0.34, 0.33, 0.3),    # 0 Dales limestone
    (0.48, 0.39, 0.25),   # 1 Cotswold
    (0.2, 0.21, 0.22),    # 2 slate
    (0.33, 0.28, 0.22),   # 3 gritstone
    (0.3, 0.29, 0.28),    # 4 granite
    (0.4, 0.27, 0.19),    # 5 sandstone
]
CONCRETE = (0.42, 0.41, 0.38)
CONCRETE_DARK = (0.3, 0.3, 0.28)
RAIL = (0.32, 0.34, 0.35)

# kind: (reach in m, casts a shadow, hand-made LODs' screen sizes)
KINDS = {"bridge_arch": (1200, True, [0.2, 0.05]), "bridge_pier": (1000, True, [0.15, 0.04]),
         "bridge_abutment": (800, True, [0.15, 0.04]), "bridge_rail": (300, False, [0.12, 0.03])}

# The arch's shape in its 1 m piece: the opening's half width, its springing and crown heights.
ARCH_HALF, ARCH_SPRING, ARCH_CROWN = 0.36, 0.2, 0.8


def shade(rgb, f):
    return tuple(min(1.0, c * f) for c in rgb)


def arch_z(x):
    u = min(1.0, abs(x) / ARCH_HALF)
    return ARCH_SPRING + (ARCH_CROWN - ARCH_SPRING) * math.sqrt(max(0.0, 1.0 - u * u))


def arch(name, style, lod):
    """A stone arch bridge's 1 m piece (x along the span from -0.5 to 0.5, y across from -0.5 to 0.5, z from the
    ground at 0 to the deck's underside at 1): the two spandrel faces round the opening, the barrel under the arch, the
    opening's inner faces at its piers, the arch ring of voussoirs proud of each face, the keystone and a string
    course; at a distance the faces and the barrel alone."""
    M = K.Model(name)
    stone = STONES[style]
    n = (24, 12, 6)[lod]
    xs = [-ARCH_HALF + 2 * ARCH_HALF * i / n for i in range(n + 1)]
    for sy in (-1, 1):
        y = sy * 0.5
        # (the face, in courses: each a band across the face, a little lighter or darker, round the opening)
        courses = (12, 5, 1)[lod]
        verts, faces, cols = [], [], []
        for c in range(courses):
            z0, z1 = c / courses, (c + 1) / courses
            # (the band's outline round the opening: from -0.5 to the opening, then over the arch, on to 0.5)
            pts = [-0.5, -ARCH_HALF] + xs[1:-1] + [ARCH_HALF, 0.5]
            for i in range(len(pts) - 1):
                xa, xb = pts[i], pts[i + 1]
                ba = 0.0 if abs(xa) >= ARCH_HALF - 1e-9 and (i == 0 or i == len(pts) - 2) else arch_z(xa)
                bb = 0.0 if abs(xb) >= ARCH_HALF - 1e-9 and (i == 0 or i == len(pts) - 2) else arch_z(xb)
                if i == 0:
                    ba = bb = 0.0
                if i == len(pts) - 2:
                    ba = bb = 0.0
                la, lb = max(z0, ba), max(z0, bb)
                if la >= z1 and lb >= z1:
                    continue
                k = len(verts)
                verts += [(xa, y, min(la, z1)), (xb, y, min(lb, z1)), (xb, y, z1), (xa, y, z1)]
                faces.append((k, k + 1, k + 2, k + 3) if sy < 0 else (k, k + 3, k + 2, k + 1))
        ob = K._object("face", verts, faces, 80.0)
        # (_object turns a lone sheet to face its signed volume's way: set it outward by hand)
        me = ob.data
        for poly in me.polygons:
            if (poly.normal.y > 0) != (sy > 0):
                poly.flip()
        me.update()
        M.add(ob, "concrete", stone, 0.45)
        if lod < 2:
            # (lighter and darker courses: thin bands just proud of the face)
            for c in range(1, (12, 5)[lod], 2):
                z0, z1 = c / (12, 5)[lod], (c + 1) / (12, 5)[lod]
                for xa, xb in ((-0.5, -ARCH_HALF - 0.08), (ARCH_HALF + 0.08, 0.5)):
                    M.add(box("course", ((xa + xb) / 2, y + sy * 0.002, (z0 + z1) / 2), (xb - xa, 0.004, z1 - z0)), "concrete", shade(stone, 0.78), 0.5)
                if z0 > ARCH_CROWN + 0.04:
                    M.add(box("course", (0.0, y + sy * 0.002, (z0 + z1) / 2), (2 * ARCH_HALF + 0.16, 0.004, z1 - z0)), "concrete", shade(stone, 0.78), 0.5)
            # (the arch ring: voussoirs round the opening, proud of the face; the keystone bigger)
            vn = (17, 9)[lod]
            for i in range(vn):
                t0, t1 = i / vn, (i + 1) / vn
                a0, a1 = math.pi * t0, math.pi * t1
                ring = []
                for a in (a0, a1):
                    cx, cz = -math.cos(a) * ARCH_HALF, ARCH_SPRING + (ARCH_CROWN - ARCH_SPRING) * math.sin(a)
                    ox, oz = -math.cos(a) * (ARCH_HALF + 0.07), ARCH_SPRING + (ARCH_CROWN - ARCH_SPRING + 0.07) * math.sin(a)
                    ring.append(((cx, cz), (ox, oz)))
                (i0, o0), (i1, o1) = ring
                key = i == vn // 2
                if key:
                    o0 = (o0[0], o0[1] + 0.03)
                    o1 = (o1[0], o1[1] + 0.03)
                d = 0.012 if key else 0.008
                v = [(i0[0], y, i0[1]), (i1[0], y, i1[1]), (o1[0], y, o1[1]), (o0[0], y, o0[1])]
                v += [(p[0], y + sy * d, p[2]) for p in v]
                vo = K._object("voussoir", v, [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)], 30.0)
                M.add(vo, "concrete", shade(stone, 1.12 if i % 2 else 1.04), 0.4)
            # (the string course near the top, along the whole face)
            M.add(box("string", (0.0, y + sy * 0.008, 0.9), (1.0, 0.016, 0.035)), "concrete", shade(stone, 1.1), 0.4)
    # (the barrel: the arch's underside across from face to face; the piers' inner faces below the springing)
    verts, faces = [], []
    for i, x in enumerate(xs):
        z = arch_z(x)
        verts += [(x, -0.5, z), (x, 0.5, z)]
        if i > 0:
            k = 2 * i
            faces.append((k - 2, k, k + 1, k - 1))
    ob = K._object("barrel", verts, faces, 80.0)
    for poly in ob.data.polygons:
        if poly.normal.z > 0:
            poly.flip()
    ob.data.update()
    M.add(ob, "concrete", shade(stone, 0.7), 0.7)
    for sx in (-1, 1):
        x = sx * ARCH_HALF
        v = [(x, -0.5, 0.0), (x, 0.5, 0.0), (x, 0.5, ARCH_SPRING), (x, -0.5, ARCH_SPRING)]
        ob = K._object("jamb", v, [(0, 1, 2, 3)], 80.0)
        for poly in ob.data.polygons:
            if (poly.normal.x > 0) != (sx < 0):
                poly.flip()
        ob.data.update()
        M.add(ob, "concrete", shade(stone, 0.8), 0.6)
        # (its ends: the piece's end faces, where the next piece or the bank meets it)
        v = [(sx * 0.5, -0.5, 0.0), (sx * 0.5, 0.5, 0.0), (sx * 0.5, 0.5, 1.0), (sx * 0.5, -0.5, 1.0)]
        ob = K._object("end", v, [(0, 1, 2, 3)], 80.0)
        for poly in ob.data.polygons:
            if (poly.normal.x > 0) != (sx > 0):
                poly.flip()
        ob.data.update()
        M.add(ob, "concrete", shade(stone, 0.9), 0.5)
    # (the piers' undersides at the ground, facing down)
    for sx in (-1, 1):
        xa, xb = sorted((sx * 0.5, sx * ARCH_HALF))
        ob = K._object("foot", [(xa, -0.5, 0.0), (xb, -0.5, 0.0), (xb, 0.5, 0.0), (xa, 0.5, 0.0)], [(0, 1, 2, 3)], 80.0)
        for poly in ob.data.polygons:
            if poly.normal.z > 0:
                poly.flip()
        ob.data.update()
        M.add(ob, "concrete", shade(stone, 0.6), 0.8)
    # (its top under the deck, facing up: closes the piece, so nothing seen from above, past or without the deck, is
    # the faces' backs: Tools/check_prop_faces.py)
    ob = K._object("top", [(-0.5, -0.5, 1.0), (0.5, -0.5, 1.0), (0.5, 0.5, 1.0), (-0.5, 0.5, 1.0)], [(0, 1, 2, 3)], 80.0)
    for poly in ob.data.polygons:
        if poly.normal.z < 0:
            poly.flip()
    ob.data.update()
    M.add(ob, "concrete", shade(stone, 0.9), 0.5)
    return M


def pier(name, kind, lod):
    """A concrete pier 1 m tall (laid scaled to the deck's underside): x along the road 0.12 m, y across 0.8 m, both
    stretched where it is laid. 0: two round columns under a crosshead; 1: a wall pier with rounded ends."""
    M = K.Model(name)
    seg = (16, 10, 6)[lod]
    if kind == 0:
        for sy in (-1, 1):
            ob = lathe("column", [(0.06, -0.05), (0.06, 0.84), (0.0, 0.84)], seg)
            ob.matrix_world = Matrix.Translation((0.0, sy * 0.25, 0.0))
            M.add(ob, "concrete", CONCRETE, 0.5)
        M.add(box("crosshead", (0.0, 0.0, 0.92), (0.16, 0.8, 0.16), bevel=0.01 if lod == 0 else 0.0), "concrete", CONCRETE, 0.4)
        if lod == 0:
            for sy in (-0.3, -0.1, 0.1, 0.3):
                M.add(box("bearing", (0.0, sy, 1.0 - 0.005), (0.1, 0.05, 0.01)), "steel", (0.2, 0.2, 0.2), 0.4)
            for sy in (-1, 1):
                M.add(box("stain", (0.061, sy * 0.25, 0.6), (0.002, 0.03, 0.4)), "concrete", CONCRETE_DARK, 0.6)
    else:
        M.add(box("wall", (0.0, 0.0, 0.47), (0.12, 0.6, 1.04)), "concrete", CONCRETE, 0.5)
        for sy in (-1, 1):
            ob = lathe("nose", [(0.06, -0.05), (0.06, 0.99), (0.0, 0.99)], seg)
            ob.matrix_world = Matrix.Translation((0.0, sy * 0.3, 0.0))
            M.add(ob, "concrete", CONCRETE, 0.5)
        if lod < 2:
            M.add(box("cap", (0.0, 0.0, 0.97), (0.15, 0.8, 0.06)), "concrete", shade(CONCRETE, 1.1), 0.4)
        if lod == 0:
            M.add(box("tide", (0.0, 0.0, 0.08), (0.125, 0.605, 0.12)), "concrete", CONCRETE_DARK, 0.8)
    return M


def abutment(name, lod):
    """A concrete abutment 1 m tall: its face to +x across y from -0.5 to 0.5, a bearing shelf at its top, wing walls
    going back from either side at 45 degrees, a footing."""
    M = K.Model(name)
    M.add(box("wall", (-0.06, 0.0, 0.5), (0.12, 1.0, 1.0)), "concrete", CONCRETE, 0.5)
    for sy in (-1, 1):
        v = [(0.0, sy * 0.5, 0.0), (0.0, sy * 0.5, 1.0), (-0.35, sy * 0.85, 0.2), (-0.35, sy * 0.85, 0.0)]
        v += [(p[0] - 0.05, p[1] - sy * 0.04, p[2]) for p in v]
        ob = K._object("wing", v, [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)], 30.0)
        M.add(ob, "concrete", shade(CONCRETE, 0.95), 0.5)
    if lod < 2:
        M.add(box("shelf", (0.03, 0.0, 0.9), (0.06, 0.96, 0.03)), "concrete", shade(CONCRETE, 1.1), 0.4)
        M.add(box("footing", (0.05, 0.0, 0.02), (0.1, 1.04, 0.06)), "concrete", CONCRETE_DARK, 0.7)
    if lod == 0:
        for y in (-0.36, -0.12, 0.12, 0.36):
            M.add(box("bearing", (0.03, y, 0.92), (0.05, 0.06, 0.02)), "steel", (0.2, 0.2, 0.2), 0.4)
        for y in (-0.3, 0.0, 0.3):
            M.add(box("weep", (0.001, y, 0.25), (0.004, 0.03, 0.03)), "paint", (0.05, 0.05, 0.05), 0.4)
    return M


def rail(name, lod):
    """A 2.5 m length of modern bridge parapet along x, 1.25 m tall: a concrete plinth 0.4 m, a steel post at its +x
    end, top, middle and lower rails and (close by) vertical bars."""
    M = K.Model(name)
    L = 2.5
    M.add(box("plinth", (0.0, 0.0, 0.2), (L, 0.35, 0.4), bevel=0.02 if lod == 0 else 0.0), "concrete", CONCRETE, 0.5)
    M.add(box("post", (L / 2 - 0.05, 0.0, 0.82), (0.1, 0.08, 0.86)), "galv", RAIL, 0.3)
    M.add(box("top", (0.0, 0.0, 1.22), (L, 0.12, 0.06)), "galv", RAIL, 0.3)
    if lod == 2:
        M.add(box("infill", (0.0, 0.0, 0.8), (L, 0.01, 0.75)), "galv", shade(RAIL, 0.8), 0.3)
        return M
    for z in (0.62, 0.95):
        M.add(box("rail", (0.0, 0.0, z), (L, 0.07, 0.05)), "galv", RAIL, 0.3)
    if lod == 0:
        n = 22
        for k in range(n):
            x = -L / 2 + (k + 0.5) * L / n
            M.add(box("bar", (x, 0.0, 0.79), (0.02, 0.02, 0.8)), "galv", RAIL, 0.3)
        M.add(box("base", (L / 2 - 0.05, 0.0, 0.41), (0.2, 0.16, 0.02)), "galv", RAIL, 0.3)
        M.add(box("drip", (0.0, 0.18, 0.05), (L, 0.02, 0.03)), "concrete", CONCRETE_DARK, 0.6)
    else:
        for k in range(8):
            x = -L / 2 + (k + 0.5) * L / 8
            M.add(box("bar", (x, 0.0, 0.79), (0.03, 0.03, 0.8)), "galv", RAIL, 0.3)
    return M


def build(lod):
    s = ("", "_LOD1", "_LOD2")[lod]
    out = [arch("SM_Prop_bridge_arch_%d%s" % (i, s), i, lod) for i in range(len(STONES))]
    out += [pier("SM_Prop_bridge_pier_%d%s" % (i, s), i, lod) for i in range(2)]
    out += [abutment("SM_Prop_bridge_abutment_0" + s, lod), rail("SM_Prop_bridge_rail_0" + s, lod)]
    return out


def main():
    import blender_farm_props as FP
    import blender_roadside_props as RS
    args = sys.argv[1:]
    out = {}
    for lod in (0, 1, 2):
        K.clear()
        models = build(lod)
        for M in models:
            nt, y0, y1, x0, x1, z0, z1 = K.write(M)
            out[M.name] = (nt, y0, y1)
            print("%-34s %6d triangles, %.2f..%.2f m up, %.2f x %.2f m" % (M.name, nt, y0, y1, x1 - x0, z1 - z0))
        if lod == 0 and "--render" in args:
            RS.render_sheet(models, args[args.index("--render") + 1],
                            "Ride UK's bridge set (our own models, made 1 m and laid stretched to each bridge; two hand-made distance versions)",
                            "bridge_props.png", cols=5, size=(420, 320))
    FP.KINDS = KINDS
    FP.write_kinds(out)


if __name__ == "__main__":
    main()
