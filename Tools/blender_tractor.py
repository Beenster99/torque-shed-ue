"""Ride UK's farm pack: three tractors from beenster's reference pictures (Nov 26), used for proportions and the kind
of detail only. Our own designs and liveries: no maker's name, badge, colour scheme, model number or tyre brand; the
number plates are blank.

  farm_tractor 0..2           a modern mid-size four-wheel-drive tractor with a cab, front weight and linkage
  farm_tractor_utility 0..2   a smaller utility tractor: narrow bonnet, glassy cab, upright exhaust at the cab corner
  farm_tractor_compact 0..2   a compact tractor: roll bar, open seat, a front loader with its bucket on the ground

Each in three liveries, one fleet across the three: deep teal with silver wheels, burgundy with graphite wheels, slate
grey with cream wheels and a white roof. All the models of a kind are the same height. Front +x, foot at 0, as the
street kit's props (Tools/blender_street_kit.py: M_RideStreetKit). Each with two distance versions made here by hand
(_LOD1, _LOD2: Tools/ue_build_rideuk.py puts them in as the mesh's LODs; never reduced by the engine). Writes
Data/RideUK/Props/SM_Prop_<kind>_<n>.glb and the kinds' rows in Data/RideUK/prop_kit.json. World/WorldRideFarms.cpp
parks them in the map's farmyards.

    python Tools/blender_tractor.py                # the models
    python Tools/blender_tractor.py --render DIR   # and the review sheet's pictures in DIR
"""
import math
import os
import sys

import bpy  # noqa: F401  (before mathutils)
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_street_kit as K   # noqa: E402
from blender_street_kit import box, lathe, rod, sweep   # noqa: E402

TYRE = (0.018, 0.018, 0.02)
BLACK = (0.022, 0.023, 0.025)
GRAPHITE = (0.06, 0.062, 0.066)
GLASS = (0.02, 0.025, 0.03)
LAMP = (0.55, 0.55, 0.52)
AMBER = (0.6, 0.22, 0.02)
RED_LENS = (0.45, 0.02, 0.02)
STEEL = (0.35, 0.35, 0.35)
PLATE = (0.7, 0.7, 0.68)
SEAT = (0.03, 0.03, 0.035)

# (each livery: body, wheels, the roof and the stripe)
LIVERIES = [
    ((0.02, 0.15, 0.17), (0.42, 0.43, 0.42), (0.02, 0.15, 0.17), (0.55, 0.55, 0.52)),   # deep teal, silver wheels
    ((0.2, 0.018, 0.035), (0.08, 0.082, 0.086), (0.2, 0.018, 0.035), (0.5, 0.48, 0.44)),   # burgundy, graphite wheels
    ((0.12, 0.13, 0.14), (0.55, 0.5, 0.38), (0.6, 0.6, 0.58), (0.55, 0.5, 0.38)),         # slate grey, cream wheels, white roof
]

# The wheels: (x, track half, radius, width); the rear's centre is its radius up.
REAR = (-1.15, 0.95, 0.93, 0.6)
FRONT = (1.65, 0.92, 0.72, 0.5)


def ring_lathe(name, prof, seg, phase=0.0):
    """A closed profile [(radius, z)] turned about z: a tyre's body, open in the middle."""
    verts, faces = [], []
    n = len(prof)
    for r, z in prof:
        for k in range(seg):
            a = phase + 2 * math.pi * k / seg
            verts.append((r * math.cos(a), r * math.sin(a), z))
    for i in range(n):
        j = (i + 1) % n
        for k in range(seg):
            k1 = (k + 1) % seg
            faces.append((i * seg + k, i * seg + k1, j * seg + k1, j * seg + k))
    return K._object(name, verts, faces, 40.0)


def wheel(M, spec, side, rim_rgb, lod):
    """A tractor wheel: the tyre (rounded shoulders, chevron lugs), its dished rim, the hub and its studs."""
    x, half, R, w = spec
    y = side * half
    zc = R
    Rr = R * 0.62
    seg = (36, 18, 10)[lod]
    prof = [(Rr, -w / 2 + 0.06), (R - 0.14, -w / 2), (R - 0.04, -w / 2 + 0.05), (R, -w / 2 + 0.14), (R, w / 2 - 0.14),
            (R - 0.04, w / 2 - 0.05), (R - 0.14, w / 2), (Rr, w / 2 - 0.06)]
    ob = ring_lathe("tyre", prof, seg)
    ob.matrix_world = Matrix.Translation((x, y, zc)) @ Matrix.Rotation(-side * math.pi / 2, 4, "X")
    M.add(ob, "paint", TYRE, 0.55)
    # (the rim: dished in from the tyre's outer face; the centre disc, the hub and its studs)
    out = w / 2 - 0.08
    rim = lathe("rim", [(Rr + 0.01, -w + 0.2), (Rr + 0.01, 0.0), (Rr - 0.04, 0.0), (Rr * 0.93, -0.12), (Rr * 0.45, -0.14),
                        (Rr * 0.4, -0.1), (0.0, -0.1)], (32, 16, 8)[lod])
    rim.matrix_world = Matrix.Translation((x, y + side * out, zc)) @ Matrix.Rotation(-side * math.pi / 2, 4, "X")
    M.add(rim, "enamel", rim_rgb, 0.35)
    if lod < 2:
        hub = lathe("hub", [(0.0, -0.1), (Rr * 0.3, -0.1), (Rr * 0.28, 0.02), (Rr * 0.18, 0.06), (0.0, 0.07)], (16, 8)[lod])
        hub.matrix_world = Matrix.Translation((x, y + side * out, zc)) @ Matrix.Rotation(-side * math.pi / 2, 4, "X")
        M.add(hub, "enamel", rim_rgb, 0.4)
    if lod == 0:
        for k in range(10):
            a = 2 * math.pi * k / 10
            r = Rr * 0.36
            M.add(rod("stud", (x + r * math.cos(a), y + side * (out - 0.1), zc + r * math.sin(a)),
                      (x + r * math.cos(a), y + side * (out - 0.04), zc + r * math.sin(a)), 0.022, 6), "steel", STEEL, 0.3)
    # (the lugs: chevrons round the tread, each half running back from the middle to a shoulder)
    if lod == 2:
        return
    n = (26 if R > 0.8 else 22 if R > 0.6 else 18) if lod == 0 else 12
    g = min(1.0, max(0.55, R / 0.93))   # (a small tyre's lugs smaller)
    u = Vector((0.0, 1.0, 0.0))
    for k in range(n):
        a = 2 * math.pi * (k + 0.5 * (side > 0)) / n
        nrm = Vector((math.cos(a), 0.0, math.sin(a)))
        t = Vector((-math.sin(a), 0.0, math.cos(a)))
        for h in (-1, 1):
            d = (u * h + t * 0.75).normalized()
            L = w * 0.62
            c = Vector((x, y, zc)) + nrm * (R - 0.012 * g) + d * (L / 2)
            e = nrm.cross(d)
            rot = Matrix(((d.x, e.x, nrm.x), (d.y, e.y, nrm.y), (d.z, e.z, nrm.z))).to_4x4()
            M.add(box("lug", c, (L, (0.085 if lod == 0 else 0.11) * g, 0.07 * g), rot=rot), "paint", TYRE, 0.55)


def arc_band(name, x, zc, R, a0, a1, y0, y1, thick, n):
    """A curved plate over a wheel: from angle a0 to a1 (radians, from +x round over the top) at radius R, across
    y0..y1, thick m deep."""
    verts, faces = [], []
    for i in range(n + 1):
        a = a0 + (a1 - a0) * i / n
        for r in (R, R + thick):
            for yy in (y0, y1):
                verts.append((x + r * math.cos(a), yy, zc + r * math.sin(a)))
    for i in range(n):
        b0, b1 = i * 4, (i + 1) * 4
        # (corners: 0 inner y0, 1 inner y1, 2 outer y0, 3 outer y1)
        faces += [(b0 + 2, b1 + 2, b1 + 3, b0 + 3), (b0 + 0, b0 + 1, b1 + 1, b1 + 0),
                  (b0 + 0, b1 + 0, b1 + 2, b0 + 2), (b0 + 1, b0 + 3, b1 + 3, b1 + 1)]
    last = n * 4
    faces += [(0, 2, 3, 1), (last + 0, last + 1, last + 3, last + 2)]
    return K._object(name, verts, faces, 40.0)


def loft(name, sections, bevel=0.0):
    """A solid from two or more rectangular sections [(x, y half, z bottom, z top)], front to back."""
    verts, faces = [], []
    for x, hy, z0, z1 in sections:
        verts += [(x, -hy, z0), (x, hy, z0), (x, hy, z1), (x, -hy, z1)]
    for i in range(len(sections) - 1):
        a, b = i * 4, (i + 1) * 4
        for k in range(4):
            k1 = (k + 1) % 4
            faces.append((a + k, b + k, b + k1, a + k1))
    faces.append((0, 1, 2, 3))
    last = (len(sections) - 1) * 4
    faces.append((last + 3, last + 2, last + 1, last + 0))
    ob = K._object(name, verts, faces, 30.0)
    if bevel > 0:
        m = ob.modifiers.new("bevel", "BEVEL")
        m.width = bevel
        m.segments = 3
        m.limit_method = "ANGLE"
    return ob


def tractor(name, livery, lod):
    M = K.Model(name)
    body, rim, roof, stripe = LIVERIES[livery]
    # --- wheels and wings
    for side in (-1, 1):
        wheel(M, REAR, side, rim, lod)
        wheel(M, FRONT, side, rim, lod)
        xr, hr, Rr_, wr = REAR
        y0, y1 = sorted((side * (hr - wr / 2 - 0.06), side * (hr + wr / 2 + 0.06)))
        M.add(arc_band("rearwing", xr, Rr_, Rr_ + 0.08, math.radians(8), math.radians(172), y0, y1, 0.04, (16, 8, 4)[lod]), "enamel", body, 0.3)
        xf, hf, Rf, wf = FRONT
        y0, y1 = sorted((side * (hf - wf / 2 - 0.04), side * (hf + wf / 2 + 0.04)))
        M.add(arc_band("frontwing", xf, Rf, Rf + 0.1, math.radians(25), math.radians(150), y0, y1, 0.03, (12, 6, 3)[lod]), "paint", BLACK, 0.35)
        if lod < 2:
            # (the rear wing's lamps: red at its back, an amber flasher on its top corner, a work light)
            M.add(box("taillamp", (xr - Rr_ - 0.05, side * hr, Rr_ + 0.35), (0.06, 0.22, 0.14)), "lens", RED_LENS, 0.1)
            M.add(box("flasher", (xr - 0.2, side * (hr + wr / 2 - 0.05), Rr_ * 2 + 0.16), (0.12, 0.1, 0.07)), "lens", AMBER, 0.1)
            M.add(box("frontstay", (xf - 0.05, side * (hf - wf / 2 - 0.1), Rf + 0.35), (0.08, 0.2, 0.35)), "paint", BLACK, 0.4)
    # --- the engine, the bonnet and its nose
    M.add(box("sump", (1.25, 0.0, 0.78), (2.4, 0.62, 0.5)), "paint", GRAPHITE, 0.5)
    M.add(box("axle", (FRONT[0], 0.0, FRONT[2]), (0.3, 2 * FRONT[1] - 0.3, 0.22)), "paint", GRAPHITE, 0.5)
    M.add(box("rearaxle", (REAR[0], 0.0, REAR[2]), (0.4, 2 * REAR[1] - 0.4, 0.34)), "paint", GRAPHITE, 0.5)
    M.add(box("transmission", (-0.9, 0.0, 0.95), (1.9, 0.8, 0.7)), "enamel", body, 0.45)
    M.add(loft("bonnet", [(2.48, 0.43, 1.02, 1.7), (1.6, 0.48, 1.02, 1.8), (0.05, 0.52, 1.02, 1.88)], bevel=0.08 if lod == 0 else 0.0), "enamel", body, 0.25)
    M.add(box("bonnetfoot", (1.3, 0.0, 0.96), (2.3, 0.94, 0.16)), "paint", BLACK, 0.4)
    M.add(box("grille", (2.5, 0.0, 1.3), (0.04, 0.74, 0.62)), "paint", BLACK, 0.3)
    for s in (-1, 1):
        M.add(box("headlamp", (2.47, s * 0.22, 1.6), (0.06, 0.26, 0.11)), "lens", LAMP, 0.0)
    if lod < 2:
        for s in (-1, 1):
            M.add(box("stripe", (1.25, s * 0.5, 1.68), (1.9, 0.01, 0.04)), "enamel", stripe, 0.2)
            M.add(box("sidepanel", (2.1, s * 0.45, 1.3), (0.7, 0.02, 0.5)), "paint", BLACK, 0.3)
    if lod == 0:
        for s in (-1, 1):
            for k in range(3):
                M.add(box("vent", (0.95 + k * 0.12, s * 0.515, 1.42), (0.05, 0.012, 0.32)), "paint", BLACK, 0.3)
            M.add(box("lampbrow", (2.44, s * 0.22, 1.68), (0.06, 0.28, 0.03)), "enamel", body, 0.2)
        for k in range(8):
            M.add(box("grillebar", (2.525, 0.0, 1.05 + k * 0.065), (0.01, 0.7, 0.018)), "paint", GRAPHITE, 0.3)
    # (the front weight and linkage)
    M.add(box("frontweight", (2.7, 0.0, 0.78), (0.4, 0.66, 0.42), bevel=0.03 if lod == 0 else 0.0), "enamel", body, 0.5)
    if lod < 2:
        M.add(box("weightface", (2.91, 0.0, 0.78), (0.02, 0.4, 0.26)), "enamel", stripe, 0.4)
        for s in (-1, 1):
            M.add(sweep("frontlink", [(2.5, s * 0.3, 0.6), (2.95, s * 0.36, 0.45)], 0.04, 4, square=True), "enamel", body, 0.5)
    # --- the cab
    cx0, cx1, cy, cz0, cz1 = -1.95, -0.15, 0.78, 1.32, 2.92
    M.add(box("cabfloor", ((cx0 + cx1) / 2, 0.0, 1.22), (cx1 - cx0 + 0.1, 2 * cy, 0.22)), "enamel", body, 0.4)
    M.add(box("glass", ((cx0 + cx1) / 2, 0.0, (cz0 + cz1) / 2), (cx1 - cx0 - 0.04, 2 * cy - 0.04, cz1 - cz0)), "lens", GLASS, 0.0)
    for x in (cx0, cx1):
        for s in (-1, 1):
            M.add(box("post", (x, s * cy, (cz0 + cz1) / 2), (0.08, 0.08, cz1 - cz0)), "paint", BLACK, 0.25)
    if lod < 2:
        M.add(box("doorpost", (-0.85, cy, (cz0 + cz1) / 2), (0.06, 0.06, cz1 - cz0)), "paint", BLACK, 0.25)
        M.add(box("lowband", ((cx0 + cx1) / 2, 0.0, cz0 + 0.04), (cx1 - cx0 + 0.04, 2 * cy + 0.04, 0.08)), "paint", BLACK, 0.3)
    M.add(box("roof", ((cx0 + cx1) / 2 - 0.02, 0.0, cz1 + 0.09), (cx1 - cx0 + 0.24, 2 * cy + 0.2, 0.18), bevel=0.05 if lod == 0 else 0.0), "enamel", roof, 0.2)
    M.add(box("rooflip", ((cx0 + cx1) / 2 - 0.02, 0.0, cz1 - 0.01), (cx1 - cx0 + 0.2, 2 * cy + 0.16, 0.04)), "paint", BLACK, 0.3)
    if lod < 2:
        for x, k in ((cx1 + 0.13, 1), (cx0 - 0.13, -1)):
            for yy in (-0.6, -0.4, 0.4, 0.6):
                M.add(box("worklight", (x, yy, cz1 + 0.06), (0.04, 0.12, 0.09)), "lens", LAMP, 0.0)
        # (the beacons on their stalks, the mirrors on their arms, the exhaust)
        for s in (-1, 1):
            M.add(rod("beaconstalk", (cx0 + 0.1, s * (cy + 0.02), cz1 + 0.18), (cx0 + 0.1, s * (cy + 0.02), cz1 + 0.3), 0.025, 6), "paint", BLACK, 0.3)
            M.add(lathe("beacon", [(0.0, 0.0), (0.075, 0.0), (0.07, 0.12), (0.04, 0.17), (0.0, 0.18)], (12, 6)[lod],
                        at=(cx0 + 0.1, s * (cy + 0.02), cz1 + 0.3)), "lens", AMBER, 0.05)
            M.add(sweep("mirrorarm", [(cx1, s * cy, 2.6), (cx1 + 0.05, s * (cy + 0.35), 2.68), (cx1 + 0.08, s * (cy + 0.45), 2.6)], 0.018, 5), "paint", BLACK, 0.3)
            M.add(box("mirror", (cx1 + 0.08, s * (cy + 0.47), 2.42), (0.05, 0.2, 0.32), bevel=0.01 if lod == 0 else 0.0), "paint", BLACK, 0.3)
    M.add(rod("exhaust", (cx1 + 0.12, -0.6, 1.55), (cx1 + 0.12, -0.6, 2.85), 0.07, (12, 8, 4)[lod]), "paint", GRAPHITE, 0.4)
    if lod < 2:
        M.add(sweep("exhausttip", [(cx1 + 0.12, -0.6, 2.84), (cx1 + 0.14, -0.6, 2.98), (cx1 + 0.25, -0.6, 3.02)], 0.072, (12, 6)[lod]), "paint", GRAPHITE, 0.4)
        M.add(box("airbox", (cx1 + 0.12, 0.6, 1.55), (0.3, 0.3, 0.5)), "paint", BLACK, 0.4)
    # (the steps up to the door, the fuel tank under it)
    M.add(box("fueltank", (-0.45, 0.62, 0.8), (0.75, 0.3, 0.5), bevel=0.04 if lod == 0 else 0.0), "paint", BLACK, 0.45)
    if lod < 2:
        for k, z in enumerate((0.42, 0.72, 1.02)):
            M.add(box("step", (-0.95, cy + 0.04, z), (0.42, 0.24, 0.04)), "steel", GRAPHITE, 0.5)
        for x in (-1.17, -0.73):
            M.add(box("stepside", (x, cy + 0.05, 0.72), (0.04, 0.22, 0.75)), "enamel", body, 0.5)
    if lod == 0:
        # (inside: the seat, the wheel and its column, the console)
        M.add(box("seat", (-1.35, 0.0, 1.62), (0.48, 0.5, 0.14)), "paint", SEAT, 0.2)
        M.add(box("seatback", (-1.6, 0.0, 1.95), (0.12, 0.48, 0.6)), "paint", SEAT, 0.2)
        M.add(rod("column", (-0.45, 0.0, 1.35), (-0.62, 0.0, 1.95), 0.04, 8), "paint", SEAT, 0.2)
        ring = lathe("wheelring", [(0.18, -0.015), (0.2, 0.0), (0.18, 0.015)], 16)
        ring.matrix_world = Matrix.Translation((-0.64, 0.0, 1.98)) @ Matrix.Rotation(math.radians(-35), 4, "Y")
        M.add(ring, "paint", SEAT, 0.2)
        M.add(box("console", (-1.1, -0.45, 1.6), (0.6, 0.2, 0.25)), "paint", SEAT, 0.2)
        for k in range(2):
            M.add(box("wiper", (cx1 + 0.03, -0.2 + k * 0.4, 2.15), (0.01, 0.02, 0.7), rot=Matrix.Rotation(math.radians(20 - 40 * k), 4, "X")), "paint", BLACK, 0.2)
    # --- the back: the linkage, the drawbar, the plate
    M.add(box("rearhousing", (-1.85, 0.0, 0.95), (0.4, 0.9, 0.7)), "enamel", body, 0.5)
    if lod < 2:
        for s in (-1, 1):
            M.add(sweep("lowerlink", [(-1.9, s * 0.35, 0.65), (-2.55, s * 0.45, 0.5)], 0.045, 4, square=True), "enamel", body, 0.5)
            M.add(sweep("liftarm", [(-1.9, s * 0.3, 1.25), (-2.3, s * 0.35, 1.15), (-2.3, s * 0.42, 0.62)], 0.035, 4, square=True), "enamel", body, 0.5)
        M.add(sweep("toplink", [(-1.95, 0.0, 1.2), (-2.5, 0.0, 1.1)], 0.035, 6), "paint", GRAPHITE, 0.5)
        M.add(box("drawbar", (-2.2, 0.0, 0.45), (0.5, 0.2, 0.08)), "paint", GRAPHITE, 0.5)
        M.add(box("plate", (-1.97, 0.55, 1.42), (0.02, 0.32, 0.17)), "paint", PLATE, 0.2)
        M.add(rod("pto", (-2.05, 0.0, 0.75), (-2.2, 0.0, 0.75), 0.04, 8), "steel", STEEL, 0.4)
    return M


# --- the smaller utility tractor (beenster's second set of pictures): a plain workhorse of about 75 hp, a narrow
# rounded bonnet, a tall glassy cab with thin posts, the exhaust standing up beside the cab's front corner
U_REAR = (-0.95, 0.86, 0.78, 0.48)
U_FRONT = (1.45, 0.8, 0.53, 0.36)


def utility(name, livery, lod):
    M = K.Model(name)
    body, rim, roof, stripe = LIVERIES[livery]
    xr, hr, Rr_, wr = U_REAR
    xf, hf, Rf, wf = U_FRONT
    for side in (-1, 1):
        wheel(M, U_REAR, side, rim, lod)
        wheel(M, U_FRONT, side, rim, lod)
        y0, y1 = sorted((side * (hr - wr / 2 - 0.05), side * (hr + wr / 2 + 0.05)))
        M.add(arc_band("rearwing", xr, Rr_, Rr_ + 0.07, math.radians(10), math.radians(170), y0, y1, 0.035, (16, 8, 4)[lod]), "enamel", body, 0.3)
        y0, y1 = sorted((side * (hf - wf / 2 - 0.03), side * (hf + wf / 2 + 0.03)))
        M.add(arc_band("frontwing", xf, Rf, Rf + 0.08, math.radians(30), math.radians(150), y0, y1, 0.025, (10, 6, 3)[lod]), "paint", BLACK, 0.35)
        if lod < 2:
            M.add(box("taillamp", (xr - Rr_ - 0.03, side * hr, Rr_ + 0.25), (0.06, 0.18, 0.12)), "lens", RED_LENS, 0.1)
            M.add(box("frontstay", (xf - 0.03, side * (hf - wf / 2 - 0.07), Rf + 0.28), (0.06, 0.14, 0.3)), "paint", BLACK, 0.4)
            M.add(box("flasher", (xr - 0.1, side * (hr + wr / 2 - 0.04), Rr_ * 2 + 0.12), (0.1, 0.08, 0.06)), "lens", AMBER, 0.1)
    # (the engine and its bonnet: narrow and round-shouldered, sloping down to the nose)
    M.add(box("sump", (0.95, 0.0, 0.62), (2.0, 0.5, 0.42)), "paint", GRAPHITE, 0.5)
    M.add(box("axle", (xf, 0.0, Rf), (0.24, 2 * hf - 0.26, 0.18)), "paint", GRAPHITE, 0.5)
    M.add(box("rearaxle", (xr, 0.0, Rr_), (0.32, 2 * hr - 0.36, 0.28)), "paint", GRAPHITE, 0.5)
    M.add(box("transmission", (-0.65, 0.0, 0.8), (1.4, 0.66, 0.56)), "enamel", body, 0.45)
    M.add(loft("bonnet", [(2.05, 0.36, 0.84, 1.36), (1.5, 0.41, 0.84, 1.5), (0.05, 0.44, 0.84, 1.56)], bevel=0.09 if lod == 0 else 0.0), "enamel", body, 0.25)
    M.add(box("bonnetfoot", (1.05, 0.0, 0.8), (1.95, 0.8, 0.12)), "paint", BLACK, 0.4)
    M.add(box("grille", (2.07, 0.0, 1.1), (0.04, 0.6, 0.48)), "paint", BLACK, 0.3)
    for s in (-1, 1):
        M.add(box("headlamp", (2.02, s * 0.27, 1.26), (0.08, 0.14, 0.16)), "lens", LAMP, 0.0)
    if lod < 2:
        for s in (-1, 1):
            M.add(box("stripe", (1.1, s * 0.425, 1.4), (1.6, 0.01, 0.035)), "enamel", stripe, 0.2)
            M.add(box("sidepanel", (1.75, s * 0.385, 1.08), (0.5, 0.02, 0.36)), "paint", BLACK, 0.3)
    if lod == 0:
        for k in range(6):
            M.add(box("grillebar", (2.095, 0.0, 0.92 + k * 0.07), (0.01, 0.56, 0.018)), "paint", GRAPHITE, 0.3)
    # (a small front hitch where the big one has its weight)
    M.add(box("fronthitch", (2.18, 0.0, 0.6), (0.3, 0.5, 0.22)), "paint", GRAPHITE, 0.5)
    # --- the cab: more glass than frame
    cx0, cx1, cy, cz0, cz1 = -1.55, -0.12, 0.62, 1.12, 2.55
    M.add(box("cabfloor", ((cx0 + cx1) / 2, 0.0, 1.03), (cx1 - cx0 + 0.08, 2 * cy, 0.2)), "enamel", body, 0.4)
    M.add(box("glass", ((cx0 + cx1) / 2, 0.0, (cz0 + cz1) / 2), (cx1 - cx0 - 0.03, 2 * cy - 0.03, cz1 - cz0)), "lens", GLASS, 0.0)
    for x in (cx0, cx1):
        for s in (-1, 1):
            M.add(box("post", (x, s * cy, (cz0 + cz1) / 2), (0.06, 0.06, cz1 - cz0)), "paint", BLACK, 0.25)
    M.add(box("roof", ((cx0 + cx1) / 2, 0.0, cz1 + 0.07), (cx1 - cx0 + 0.3, 2 * cy + 0.18, 0.14), bevel=0.05 if lod == 0 else 0.0), "enamel", roof, 0.2)
    if lod < 2:
        M.add(box("lowband", ((cx0 + cx1) / 2, 0.0, cz0 + 0.03), (cx1 - cx0 + 0.03, 2 * cy + 0.03, 0.06)), "paint", BLACK, 0.3)
        M.add(box("rooflip", ((cx0 + cx1) / 2, 0.0, cz1 - 0.005), (cx1 - cx0 + 0.26, 2 * cy + 0.14, 0.03)), "paint", BLACK, 0.3)
        for s in (-1, 1):
            M.add(box("worklight", (cx1 + 0.16, s * 0.45, cz1 + 0.05), (0.04, 0.12, 0.08)), "lens", LAMP, 0.0)
            M.add(sweep("mirrorarm", [(cx1, s * cy, 2.25), (cx1 + 0.04, s * (cy + 0.32), 2.36), (cx1 + 0.06, s * (cy + 0.4), 2.28)], 0.016, 5), "paint", BLACK, 0.3)
            M.add(box("mirror", (cx1 + 0.06, s * (cy + 0.42), 2.1), (0.04, 0.17, 0.28)), "paint", BLACK, 0.3)
        M.add(rod("beaconstalk", (cx0 + 0.1, -(cy - 0.05), cz1 + 0.14), (cx0 + 0.1, -(cy - 0.05), cz1 + 0.24), 0.022, 6), "paint", BLACK, 0.3)
        M.add(lathe("beacon", [(0.0, 0.0), (0.065, 0.0), (0.06, 0.1), (0.035, 0.15), (0.0, 0.16)], (12, 6)[lod],
                    at=(cx0 + 0.1, -(cy - 0.05), cz1 + 0.24)), "lens", AMBER, 0.05)
    # (the exhaust standing up at the cab's front corner, the air intake on the other)
    ey = cy + 0.1
    M.add(rod("exhaust", (cx1 + 0.06, ey, 1.4), (cx1 + 0.06, ey, 2.72), 0.055, (10, 8, 4)[lod]), "steel", STEEL, 0.4)
    if lod < 2:
        M.add(rod("exhaustguard", (cx1 + 0.06, ey, 1.8), (cx1 + 0.06, ey, 2.3), 0.075, (12, 6)[lod]), "paint", BLACK, 0.4)
        M.add(box("exhaustbracket", (cx1 + 0.02, cy + 0.05, 2.0), (0.04, 0.1, 0.05)), "paint", BLACK, 0.4)
        M.add(sweep("exhaustpipe", [(cx1 + 0.2, 0.4, 1.3), (cx1 + 0.06, ey, 1.35), (cx1 + 0.06, ey, 1.42)], 0.05, 6), "steel", STEEL, 0.4)
        M.add(rod("intake", (cx1 + 0.06, -ey, 1.4), (cx1 + 0.06, -ey, 2.15), 0.05, (10, 6)[lod]), "paint", BLACK, 0.4)
    M.add(box("fueltank", (-0.35, -0.5, 0.68), (0.6, 0.26, 0.42), bevel=0.04 if lod == 0 else 0.0), "paint", BLACK, 0.45)
    if lod < 2:
        for z in (0.38, 0.7):
            M.add(box("step", (-0.7, cy + 0.03, z), (0.36, 0.2, 0.035)), "steel", GRAPHITE, 0.5)
        for x in (-0.88, -0.52):
            M.add(box("stepside", (x, cy + 0.04, 0.62), (0.035, 0.18, 0.62)), "paint", BLACK, 0.5)
        M.add(box("rearhousing", (-1.45, 0.0, 0.8), (0.35, 0.74, 0.56)), "enamel", body, 0.5)
        for s in (-1, 1):
            M.add(sweep("lowerlink", [(-1.5, s * 0.3, 0.52), (-2.05, s * 0.38, 0.4)], 0.038, 4, square=True), "paint", GRAPHITE, 0.5)
            M.add(sweep("liftarm", [(-1.5, s * 0.26, 1.05), (-1.82, s * 0.3, 0.96), (-1.82, s * 0.36, 0.48)], 0.03, 4, square=True), "paint", GRAPHITE, 0.5)
        M.add(sweep("toplink", [(-1.55, 0.0, 1.02), (-2.0, 0.0, 0.92)], 0.03, 6), "paint", GRAPHITE, 0.5)
        M.add(box("drawbar", (-1.75, 0.0, 0.38), (0.4, 0.16, 0.07)), "paint", GRAPHITE, 0.5)
    if lod == 0:
        M.add(box("seat", (-1.05, 0.0, 1.38), (0.42, 0.44, 0.12)), "paint", SEAT, 0.2)
        M.add(box("seatback", (-1.27, 0.0, 1.66), (0.1, 0.42, 0.5)), "paint", SEAT, 0.2)
        M.add(rod("column", (-0.35, 0.0, 1.15), (-0.5, 0.0, 1.68), 0.035, 8), "paint", SEAT, 0.2)
        ring = lathe("wheelring", [(0.17, -0.014), (0.185, 0.0), (0.17, 0.014)], 16)
        ring.matrix_world = Matrix.Translation((-0.52, 0.0, 1.7)) @ Matrix.Rotation(math.radians(-35), 4, "Y")
        M.add(ring, "paint", SEAT, 0.2)
    return M


# --- the compact tractor (beenster's third set): a little four-wheel-drive with a fold-up roll bar instead of a cab,
# an open seat between the rear wings and a front loader with its bucket set down on the ground
C_REAR = (-0.55, 0.6, 0.5, 0.34)
C_FRONT = (0.95, 0.55, 0.33, 0.22)


def compact(name, livery, lod):
    M = K.Model(name)
    body, rim, roof, stripe = LIVERIES[livery]
    xr, hr, Rr_, wr = C_REAR
    xf, hf, Rf, wf = C_FRONT
    for side in (-1, 1):
        wheel(M, C_REAR, side, rim, lod)
        wheel(M, C_FRONT, side, rim, lod)
        # (the rear wings: an arc over the tyre with a flat top the seat sits between)
        y0, y1 = sorted((side * (hr - wr / 2 - 0.04), side * (hr + wr / 2 + 0.04)))
        M.add(arc_band("rearwing", xr, Rr_, Rr_ + 0.06, math.radians(5), math.radians(175), y0, y1, 0.03, (14, 8, 4)[lod]), "enamel", body, 0.3)
        M.add(box("wingtop", (xr, side * (hr - wr / 2 - 0.08), Rr_ * 2 + 0.06), (0.75, 0.12, 0.03)), "enamel", body, 0.3)
        if lod < 2:
            M.add(box("taillamp", (xr - 0.12, side * (hr - 0.02), Rr_ * 2 + 0.1), (0.05, 0.1, 0.08)), "lens", RED_LENS, 0.1)
        # (the roll bar's legs, up from the back of the wings)
        M.add(box("rollleg", (xr - 0.32, side * 0.47, 1.45), (0.07, 0.09, 1.0)), "paint", BLACK, 0.3)
        if lod < 2:
            M.add(box("rollfoot", (xr - 0.32, side * 0.47, 0.92), (0.14, 0.14, 0.06)), "paint", BLACK, 0.3)
            M.add(box("barlamp", (xr - 0.3, side * 0.55, 1.42), (0.05, 0.08, 0.12)), "lens", AMBER, 0.1)
    M.add(sweep("rolltop", [(xr - 0.32, -0.47, 1.9), (xr - 0.32, -0.47, 2.1), (xr - 0.32, -0.33, 2.2), (xr - 0.32, 0.33, 2.2),
                            (xr - 0.32, 0.47, 2.1), (xr - 0.32, 0.47, 1.9)], 0.04, (8, 6, 4)[lod], square=(lod == 2)), "paint", BLACK, 0.3)
    # (the chassis, the axles, the footplate)
    M.add(box("sump", (0.6, 0.0, 0.45), (1.2, 0.34, 0.3)), "paint", GRAPHITE, 0.5)
    M.add(box("axle", (xf, 0.0, Rf), (0.16, 2 * hf - 0.2, 0.13)), "paint", GRAPHITE, 0.5)
    M.add(box("rearaxle", (xr, 0.0, Rr_), (0.26, 2 * hr - 0.28, 0.22)), "paint", GRAPHITE, 0.5)
    M.add(box("transmission", (-0.35, 0.0, 0.6), (0.9, 0.42, 0.42)), "enamel", body, 0.45)
    M.add(box("footplate", (0.0, 0.0, 0.48), (0.55, 1.02, 0.04)), "paint", BLACK, 0.5)
    # (the bonnet: short, sloping down to a wide nose)
    M.add(loft("bonnet", [(1.42, 0.3, 0.58, 0.94), (1.1, 0.33, 0.56, 1.06), (0.22, 0.34, 0.56, 1.16)], bevel=0.06 if lod == 0 else 0.0), "enamel", body, 0.25)
    M.add(box("grille", (1.43, 0.0, 0.76), (0.03, 0.48, 0.3)), "paint", BLACK, 0.3)
    M.add(box("dash", (0.18, 0.0, 1.12), (0.14, 0.5, 0.22)), "enamel", body, 0.3)
    for s in (-1, 1):
        M.add(box("headlamp", (1.4, s * 0.19, 0.95), (0.06, 0.13, 0.06)), "lens", LAMP, 0.0)
        if lod < 2:
            M.add(box("stripe", (0.8, s * 0.335, 1.04), (1.0, 0.01, 0.028)), "enamel", stripe, 0.2)
            M.add(box("sidevent", (0.55, s * 0.345, 0.8), (0.4, 0.015, 0.28)), "paint", BLACK, 0.3)
    # (the seat between the wings, the wheel)
    M.add(box("seat", (-0.5, 0.0, 1.06), (0.42, 0.44, 0.1)), "paint", SEAT, 0.2)
    M.add(box("seatback", (-0.7, 0.0, 1.3), (0.1, 0.42, 0.42), rot=Matrix.Rotation(math.radians(-10), 4, "Y")), "paint", SEAT, 0.2)
    if lod < 2:
        M.add(box("seatbase", (-0.5, 0.0, 0.92), (0.36, 0.3, 0.2)), "enamel", body, 0.4)
        M.add(rod("column", (0.16, 0.0, 1.1), (0.04, 0.0, 1.38), 0.03, 8), "paint", SEAT, 0.2)
        ring = lathe("wheelring", [(0.15, -0.012), (0.165, 0.0), (0.15, 0.012)], (16, 8)[lod])
        ring.matrix_world = Matrix.Translation((0.02, 0.0, 1.4)) @ Matrix.Rotation(math.radians(-40), 4, "Y")
        M.add(ring, "paint", SEAT, 0.2)
        M.add(box("hitchplate", (xr - 0.55, 0.0, 0.42), (0.1, 0.5, 0.2)), "paint", GRAPHITE, 0.5)
        for s in (-1, 1):
            M.add(sweep("lowerlink", [(xr - 0.3, s * 0.22, 0.38), (xr - 0.75, s * 0.28, 0.3)], 0.028, 4, square=True), "paint", GRAPHITE, 0.5)
    # --- the front loader: a tower each side of the bonnet, the arms down to the bucket on the ground
    tx, tz = 0.42, 1.12          # (the arm's pivot on the tower)
    bx, bz = 2.05, 0.32          # (the bucket's pivot)
    for s in (-1, 1):
        y = s * 0.47
        M.add(box("tower", (tx, y, 0.78), (0.2, 0.07, 0.76)), "enamel", body, 0.4)
        # (the arm: up from the tower, the knee over the nose, down to the bucket)
        M.add(sweep("arm", [(tx, y, tz), (1.25, y, 1.05), (1.78, y, 0.68), (bx, y, bz)], 0.06, 4, square=True), "enamel", body, 0.4)
        if lod < 2:
            M.add(sweep("ram", [(tx - 0.02, y + s * 0.06, 0.55), (1.15, y + s * 0.06, 0.95)], 0.03, 6), "paint", GRAPHITE, 0.4)
            M.add(sweep("rod", [(1.15, y + s * 0.06, 0.95), (1.3, y + s * 0.06, 1.0)], 0.018, 6), "steel", STEEL, 0.3)
            M.add(sweep("tiltlink", [(1.78, y, 0.82), (2.05, y, 0.62)], 0.025, 4, square=True), "enamel", body, 0.4)
    M.add(box("crossbar", (1.5, 0.0, 0.88), (0.1, 0.94, 0.08)), "enamel", body, 0.4)
    # (the bucket: a back plate, a floor, two side cheeks and a cutting edge, open to the front)
    bw = 1.45
    M.add(box("bucketback", (bx + 0.02, 0.0, 0.3), (0.04, bw, 0.5), rot=Matrix.Rotation(math.radians(12), 4, "Y")), "enamel", body, 0.5)
    M.add(box("bucketfloor", (bx + 0.3, 0.0, 0.04), (0.6, bw, 0.035)), "enamel", body, 0.5)
    M.add(box("edge", (bx + 0.6, 0.0, 0.025), (0.06, bw, 0.03)), "steel", STEEL, 0.4)
    for s in (-1, 1):
        cheek = K._object("cheek", [(bx - 0.03, 0.0, 0.06), (bx + 0.62, 0.0, 0.02), (bx + 0.08, 0.0, 0.55)], [(0, 1, 2)], 30.0)
        if lod < 2:
            sol = cheek.modifiers.new("thick", "SOLIDIFY")
            sol.thickness = 0.03
        cheek.matrix_world = Matrix.Translation((0.0, s * (bw / 2 - 0.015), 0.0))
        M.add(cheek, "enamel", body, 0.5)
    if lod < 2:
        M.add(box("bucketlip", (bx + 0.12, 0.0, 0.56), (0.08, bw, 0.04)), "enamel", body, 0.5)
    return M


KINDS3 = {"farm_tractor": tractor, "farm_tractor_utility": utility, "farm_tractor_compact": compact}


def build(lod, kind="farm_tractor"):
    s = ("", "_LOD1", "_LOD2")[lod]
    return [KINDS3[kind]("SM_Prop_%s_%d%s" % (kind, i, s), i, lod) for i in range(len(LIVERIES))]


def main():
    import blender_farm_props as FP
    args = sys.argv[1:]
    out = {}
    for kind in KINDS3:
        for lod in (0, 1, 2):
            K.clear()
            models = build(lod, kind)
            for M in models:
                nt, y0, y1, x0, x1, z0, z1 = K.write(M)
                out[M.name] = (nt, y0, y1)
                print("%-38s %6d triangles, %.2f..%.2f m up, %.1f x %.1f m" % (M.name, nt, y0, y1, x1 - x0, z1 - z0))
            if lod == 0 and "--render" in args:
                import blender_farm_kit_render as R
                R.render_props(models, args[args.index("--render") + 1],
                               title="Ride UK's farm pack: %s (our own design and liveries; two hand-made distance versions)" % kind.replace("_", " "),
                               sheet_name=kind + ".png")
    # (reach m, shadows, and the hand-made distance versions' screen sizes)
    FP.KINDS = {"farm_tractor": (300, True, [0.2, 0.05]), "farm_tractor_utility": (260, True, [0.2, 0.05]),
                "farm_tractor_compact": (200, True, [0.2, 0.05])}
    FP.write_kinds(out)


if __name__ == "__main__":
    main()
