"""Ride UK's farm pack: three tractors from beenster's reference pictures (Nov 26), used for proportions and the kind
of detail only. Our own designs and liveries: no maker's name, badge, colour scheme, model number or tyre brand; the
number plates are blank.

  farm_tractor 0..2           a modern mid-size four-wheel-drive tractor with a cab, front weight and linkage
  farm_tractor_utility 0..2   a smaller utility tractor: narrow bonnet, glassy cab, upright exhaust at the cab corner
  farm_tractor_compact 0..2   a compact tractor: roll bar, open seat, a front loader with its bucket on the ground

Close up (beenster Nov 26: "given the amazing images I shared ... why are ours so low poly?") each is modelled in
detail, about 30,000 to 60,000 triangles: bevelled and rounded panels, deep chevron-lugged tyres, dished rims with
their bolt circles, slatted grilles and louvres, lamp clusters, hoses, handrails, steps, the cab's inside. The two
distance versions are made here by hand and stay light (_LOD1 a few thousand, _LOD2 about a thousand: Tools/
ue_build_rideuk.py puts them in as the mesh's LODs; never reduced by the engine).

Each in three liveries, one fleet across the three: deep teal with silver wheels, burgundy with graphite wheels, slate
grey with cream wheels and a white roof. All the models of a kind are the same height. Front +x, foot at 0, as the
street kit's props (Tools/blender_street_kit.py: M_RideStreetKit). Writes Data/RideUK/Props/SM_Prop_<kind>_<n>.glb and
the kinds' rows in Data/RideUK/prop_kit.json. World/WorldRideFarms.cpp parks them in the map's farmyards. Every
piece is closed or a sheet seen from one side only, so Tools/check_prop_faces.py finds no faces wound inside out.

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
from blender_street_kit import box, lathe, rod, sweep, bezier   # noqa: E402

TYRE = (0.018, 0.018, 0.02)
BLACK = (0.022, 0.023, 0.025)
GRAPHITE = (0.06, 0.062, 0.066)
GLASS = (0.02, 0.025, 0.03)
LAMP = (0.55, 0.55, 0.52)
CHROME = (0.5, 0.5, 0.5)
AMBER = (0.6, 0.22, 0.02)
RED_LENS = (0.45, 0.02, 0.02)
STEEL = (0.35, 0.35, 0.35)
PLATE = (0.7, 0.7, 0.68)
SEAT = (0.03, 0.03, 0.035)
HOSE = (0.015, 0.015, 0.017)
COUPLER = (0.45, 0.3, 0.02)
BLUECAP = (0.02, 0.08, 0.3)

# (each livery: body, wheels, the roof and the stripe)
LIVERIES = [
    ((0.02, 0.15, 0.17), (0.42, 0.43, 0.42), (0.02, 0.15, 0.17), (0.55, 0.55, 0.52)),   # deep teal, silver wheels
    ((0.2, 0.018, 0.035), (0.08, 0.082, 0.086), (0.2, 0.018, 0.035), (0.5, 0.48, 0.44)),   # burgundy, graphite wheels
    ((0.12, 0.13, 0.14), (0.55, 0.5, 0.38), (0.6, 0.6, 0.58), (0.55, 0.5, 0.38)),         # slate grey, cream wheels, white roof
]

# The wheels: (x, track half, radius, width); the centre is its radius up.
REAR = (-1.15, 0.95, 0.93, 0.6)
FRONT = (1.65, 0.92, 0.72, 0.5)


# --- shapes ---------------------------------------------------------------------------------------------------

def rbox(name, centre, size, r=0.03, seg=3, rot=None, rot_z=0.0):
    """A box with its edges rounded (radius r, seg steps; none when seg is 0)."""
    ob = box(name, centre, size, rot_z=rot_z, rot=rot)
    if seg > 0 and r > 0.0:
        m = ob.modifiers.new("bevel", "BEVEL")
        m.width = min(r, min(size) * 0.45)
        m.segments = seg
        m.limit_method = "ANGLE"
        m.harden_normals = True
    return ob


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


def section(hy, z0, z1, rt, rb, n):
    """A rounded-rectangle cross-section [(y, z)]: half width hy, from z0 to z1, top corners of radius rt and
    bottom ones of rb, n steps a corner (n 0: square corners)."""
    if n == 0:
        return [(hy, z0), (hy, z1), (-hy, z1), (-hy, z0)]
    pts = []
    for cy, cz, r, a0, a1 in ((hy - rb, z0 + rb, rb, -90, 0), (hy - rt, z1 - rt, rt, 0, 90),
                              (-hy + rt, z1 - rt, rt, 90, 180), (-hy + rb, z0 + rb, rb, 180, 270)):
        for i in range(n + 1):
            a = math.radians(a0 + (a1 - a0) * i / n)
            pts.append((cy + r * math.cos(a), cz + r * math.sin(a)))
    return pts


def loft_round(name, sections, n=4, sharp=40.0):
    """A closed solid through rounded sections [(x, half width, z bottom, z top, top radius, bottom radius)], front
    to back, capped at both ends."""
    rings = [[(x, y, z) for y, z in section(hy, z0, z1, rt, rb, n)] for x, hy, z0, z1, rt, rb in sections]
    m = len(rings[0])
    verts = [v for ring in rings for v in ring]
    faces = []
    for i in range(len(rings) - 1):
        a, b = i * m, (i + 1) * m
        for k in range(m):
            k1 = (k + 1) % m
            faces.append((a + k, a + k1, b + k1, b + k))
    faces.append(tuple(range(m)))
    last = (len(rings) - 1) * m
    faces.append(tuple(reversed(range(last, last + m))))
    return K._object(name, verts, faces, sharp)


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
        faces += [(b0 + 2, b1 + 2, b1 + 3, b0 + 3), (b0 + 0, b0 + 1, b1 + 1, b1 + 0),
                  (b0 + 0, b1 + 0, b1 + 2, b0 + 2), (b0 + 1, b0 + 3, b1 + 3, b1 + 1)]
    last = n * 4
    faces += [(0, 2, 3, 1), (last + 0, last + 1, last + 3, last + 2)]
    return K._object(name, verts, faces, 40.0)


def wing(M, name, x, zc, R, a0, a1, y0, y1, rgb, lod, surface="enamel", lip=True):
    """A wheel's wing: the curved plate and, close up, a rolled lip along both its edges."""
    n = (40, 12, 5)[lod]
    M.add(arc_band(name, x, zc, R, a0, a1, y0, y1, 0.035, n), surface, rgb, 0.3)
    if lod == 0 and lip:
        for yy in (y0, y1):
            pts = [(x + (R + 0.018) * math.cos(a0 + (a1 - a0) * i / 24), yy, zc + (R + 0.018) * math.sin(a0 + (a1 - a0) * i / 24)) for i in range(25)]
            M.add(sweep(name + "lip", pts, 0.022, 8), surface, rgb, 0.3)


def curve(p0, p1, p2, p3, n=10):
    return bezier(Vector(p0), Vector(p1), Vector(p2), Vector(p3), n)


def hose(M, pts, r, lod, rgb=HOSE):
    """A hose along a smooth curve (four control points)."""
    if lod == 2:
        return
    M.add(sweep("hose", curve(*pts, n=(12, 4)[lod]), r, (8, 4)[lod]), "paint", rgb, 0.4)


def lamp(M, at, facing, size, lod, lens=LAMP, body=BLACK):
    """A lamp: a rounded housing with its lens on the face toward facing (+1 front, -1 back)."""
    x, y, z = at
    d, w, h = size
    M.add(rbox("lamphousing", (x - facing * d * 0.2, y, z), (d, w, h), r=min(w, h) * 0.25, seg=(2, 0, 0)[lod]), "paint", body, 0.3)
    M.add(rbox("lens", (x + facing * d * 0.32, y, z), (d * 0.1, w * 0.84, h * 0.8), r=min(w, h) * 0.2, seg=(2, 0, 0)[lod]), "lens", lens, 0.05)
    if lod == 0:
        for k in (-1, 1):
            M.add(lathe("reflector", [(0.0, 0.0), (min(w, h) * 0.3, 0.0), (min(w, h) * 0.32, 0.008), (0.0, 0.01)], 12,
                        at=(0.0, 0.0, 0.0)), "steel", CHROME, 0.05).matrix_world = (
                Matrix.Translation((x + facing * d * 0.38, y + k * w * 0.22, z)) @ Matrix.Rotation(facing * math.pi / 2, 4, "Y"))


def beacon(M, at, lod, r=0.075):
    """An amber beacon on its base: a ribbed dome."""
    if lod == 2:
        return
    x, y, z = at
    M.add(lathe("beaconbase", [(0.0, 0.0), (r * 1.05, 0.0), (r * 1.05, 0.035), (0.0, 0.035)], (16, 8)[lod], at=(x, y, z)), "paint", BLACK, 0.3)
    prof = [(0.0, 0.035), (r, 0.035), (r * 0.98, 0.07), (r * 1.02, 0.075), (r * 0.96, 0.11), (r * 1.0, 0.115), (r * 0.85, 0.15), (r * 0.5, 0.18), (0.0, 0.19)]
    M.add(lathe("beacon", prof if lod == 0 else [(0.0, 0.035), (r, 0.035), (r * 0.9, 0.15), (0.0, 0.19)], (20, 8)[lod], at=(x, y, z)), "lens", AMBER, 0.05)


def mirror(M, base, side, lod, reach=0.42, drop=0.3):
    """A mirror on a bent arm out from base: the main head and, close up, a small wide-angle one under it."""
    if lod == 2:
        return
    x, y, z = base
    tip = (x + 0.08, y + side * reach, z + 0.05)
    M.add(sweep("mirrorarm", curve((x, y, z), (x + 0.03, y + side * reach * 0.6, z + 0.12), (x + 0.07, y + side * reach * 0.95, z + 0.1), tip, n=(8, 3)[lod]),
                0.016, (8, 4)[lod]), "paint", BLACK, 0.3)
    M.add(rod("mirrorstem", tip, (tip[0], tip[1], tip[2] - drop + 0.12), 0.014, (8, 4)[lod]), "paint", BLACK, 0.3)
    M.add(rbox("mirror", (tip[0], tip[1] + side * 0.02, tip[2] - drop * 0.55), (0.05, 0.2, 0.3), r=0.02, seg=(3, 0)[lod]), "paint", BLACK, 0.3)
    M.add(rbox("mirrorglass", (tip[0] - 0.027, tip[1] + side * 0.02, tip[2] - drop * 0.55), (0.006, 0.17, 0.26), r=0.015, seg=(2, 0)[lod]), "steel", CHROME, 0.05)
    if lod == 0:
        M.add(rbox("widemirror", (tip[0], tip[1] + side * 0.02, tip[2] - drop - 0.08), (0.05, 0.16, 0.13), r=0.02, seg=3), "paint", BLACK, 0.3)
        M.add(rbox("wideglass", (tip[0] - 0.027, tip[1] + side * 0.02, tip[2] - drop - 0.08), (0.006, 0.13, 0.1), r=0.012, seg=2), "steel", CHROME, 0.05)


def steering(M, at, tilt_deg, r, lod):
    """The steering wheel: its rim, three spokes, the boss and column."""
    if lod == 2:
        return
    x, y, z = at
    T = Matrix.Translation((x, y, z)) @ Matrix.Rotation(math.radians(tilt_deg), 4, "Y")
    ring = ring_lathe("wheelrim", [(r - 0.014, -0.012), (r, -0.016), (r + 0.014, 0.0), (r, 0.016), (r - 0.014, 0.012)], (32, 12)[lod])
    ring.matrix_world = T
    M.add(ring, "paint", SEAT, 0.2)
    if lod == 0:
        boss = lathe("boss", [(0.0, -0.03), (0.05, -0.03), (0.05, 0.02), (0.0, 0.03)], 12)
        boss.matrix_world = T
        M.add(boss, "paint", SEAT, 0.2)
        for k in range(3):
            a = math.radians(90 + 120 * k)
            sp = sweep("spoke", [(0.04 * math.cos(a), 0.04 * math.sin(a), 0.0), ((r - 0.01) * math.cos(a), (r - 0.01) * math.sin(a), 0.0)], 0.011, 6)
            sp.matrix_world = T
            M.add(sp, "paint", SEAT, 0.2)


def seat(M, x, z, lod, w=0.5, back=0.62, rgb=SEAT, tilt=-8.0):
    """A sprung seat: cushion, back and, close up, the armrests and the suspension under it."""
    seg = (3, 0, 0)[lod]
    M.add(rbox("seat", (x, 0.0, z), (0.5, w, 0.14), r=0.05, seg=seg), "paint", rgb, 0.2)
    M.add(rbox("seatback", (x - 0.24, 0.0, z + back / 2 + 0.04), (0.13, w * 0.96, back), r=0.05, seg=seg,
               rot=Matrix.Rotation(math.radians(tilt), 4, "Y")), "paint", rgb, 0.2)
    if lod == 0:
        M.add(rbox("seatbase", (x - 0.02, 0.0, z - 0.17), (0.38, 0.36, 0.2), r=0.03, seg=2), "paint", GRAPHITE, 0.3)
        M.add(rbox("headrest", (x - 0.3, 0.0, z + back + 0.12), (0.1, w * 0.6, 0.18), r=0.04, seg=3), "paint", rgb, 0.2)


def tread_step(M, x, y, z, w, d, lod):
    """A step: a plate with raised treads."""
    M.add(rbox("step", (x, y, z), (w, d, 0.03), r=0.008, seg=(1, 0, 0)[lod]), "steel", GRAPHITE, 0.5)
    if lod == 0:
        for k in range(4):
            M.add(box("tread", (x - w * 0.375 + k * w / 4, y, z + 0.018), (0.02, d * 0.9, 0.008)), "steel", GRAPHITE, 0.5)


# --- the wheels ------------------------------------------------------------------------------------------------

def wheel(M, spec, side, rim_rgb, lod, lugs_per_m=31.0):
    """A tractor wheel: the tyre (rounded shoulders and sidewalls, deep chevron lugs that wrap over the shoulder), the
    dished rim with its flange and air holes, the hub, its bolt circle and the valve."""
    x, half, R, w = spec
    y = side * half
    zc = R
    hl = 0.065 * min(1.0, R / 0.93) + 0.01        # (the lugs' depth)
    Rb = R - hl                                  # (the tread's floor)
    Rr = R * 0.6                                  # (the rim)
    seg = (72, 24, 12)[lod]
    hw = w / 2
    T = Matrix.Translation((x, y, zc)) @ Matrix.Rotation(-side * math.pi / 2, 4, "X")   # (local z: out from the tractor)
    if lod == 0:
        prof = [(Rr, -hw + 0.04), (Rr + 0.03, -hw + 0.01), (Rr + (Rb - Rr) * 0.35, -hw - 0.005), (Rr + (Rb - Rr) * 0.7, -hw + 0.0),
                (Rb - 0.05, -hw + 0.02), (Rb - 0.015, -hw + 0.05), (Rb, -hw + 0.09), (Rb, hw - 0.09), (Rb - 0.015, hw - 0.05),
                (Rb - 0.05, hw - 0.02), (Rr + (Rb - Rr) * 0.7, hw), (Rr + (Rb - Rr) * 0.35, hw + 0.005), (Rr + 0.03, hw - 0.01), (Rr, hw - 0.04)]
    else:
        prof = [(Rr, -hw + 0.04), (Rb - 0.06, -hw), (R if lod == 2 else Rb, -hw + 0.08), (R if lod == 2 else Rb, hw - 0.08), (Rb - 0.06, hw), (Rr, hw - 0.04)]
    ob = ring_lathe("tyre", prof, seg)
    ob.matrix_world = T
    M.add(ob, "paint", TYRE, 0.55)
    # (the rim: its flanges into the tyre's beads, the well, then the disc dished in from the outer face)
    if lod == 0:
        f = min(1.0, w / 0.6)   # (a narrow wheel's rim and dish in proportion, so its outline never crosses itself)
        rp = [(Rr + 0.02 * f, -hw + 0.035 * f), (Rr - 0.005 * f, -hw + 0.05 * f), (Rr - 0.035 * f, -hw + 0.09 * f), (Rr - 0.035 * f, hw - 0.1 * f),
              (Rr - 0.005 * f, hw - 0.055 * f), (Rr + 0.022 * f, hw - 0.035 * f), (Rr + 0.02 * f, hw - 0.02 * f), (Rr - 0.045 * f, hw - 0.04 * f),
              (Rr * 0.9 - 0.02 * f, hw - 0.1 * f), (Rr * 0.78, hw - 0.14 * f), (Rr * 0.6, hw - 0.15 * f), (Rr * 0.56, hw - 0.12 * f), (Rr * 0.52, hw - 0.12 * f),
              (Rr * 0.48, hw - 0.15 * f), (Rr * 0.38, hw - 0.17 * f), (0.0, hw - 0.17 * f)]
    else:
        rp = [(Rr + 0.01, -hw + 0.04), (Rr + 0.01, hw - 0.03), (Rr * 0.8, hw - 0.13), (Rr * 0.4, hw - 0.17), (0.0, hw - 0.17)]
    rim = lathe("rim", rp, (64, 20, 10)[lod])
    rim.matrix_world = T
    M.add(rim, "enamel", rim_rgb, 0.35)
    if lod == 2:
        return
    hub = lathe("hub", [(0.0, hw - 0.2), (Rr * 0.3, hw - 0.2), (Rr * 0.3, hw - 0.14), (Rr * 0.22, hw - 0.1), (Rr * 0.14, hw - 0.07), (0.0, hw - 0.06)], (24, 8)[lod])
    hub.matrix_world = T
    M.add(hub, "enamel", rim_rgb, 0.4)
    if lod == 0:
        cap = lathe("hubcap", [(0.0, hw - 0.075), (Rr * 0.12, hw - 0.075), (Rr * 0.1, hw - 0.04), (0.0, hw - 0.035)], 16)
        cap.matrix_world = T
        M.add(cap, "paint", GRAPHITE, 0.4)
        nb = 10 if R > 0.6 else 8
        for k in range(nb):
            a = 2 * math.pi * k / nb
            r = Rr * 0.34
            nut = lathe("nut", [(0.0, 0.0), (0.028, 0.0), (0.028, 0.022), (0.02, 0.03), (0.0, 0.032)], 6, at=(r * math.cos(a), r * math.sin(a), hw - 0.165))
            nut.matrix_world = T @ nut.matrix_world
            M.add(nut, "steel", STEEL, 0.3)
        # (the disc's air holes: dark ovals in the dish)
        for k in range(8):
            a = 2 * math.pi * (k + 0.5) / 8
            r = Rr * 0.69
            hole = lathe("airhole", [(0.0, 0.0), (0.06 * Rr / 0.56, 0.0), (0.055 * Rr / 0.56, 0.006), (0.0, 0.006)], 12)
            hole.matrix_world = T @ Matrix.Translation((r * math.cos(a), r * math.sin(a), hw - 0.145)) @ Matrix.Rotation(-0.35, 4, Vector((-math.sin(a), math.cos(a), 0.0)))
            M.add(hole, "paint", BLACK, 0.5)
        valve = rod("valve", (Rr * 0.92, 0.0, hw - 0.06), (Rr * 0.92, 0.0, hw - 0.01), 0.008, 6)
        valve.matrix_world = T @ valve.matrix_world
        M.add(valve, "steel", STEEL, 0.3)
    # (the lugs: chevrons round the tread, each half from the middle out over the shoulder, deep and tapered)
    n = max(12, int(round(lugs_per_m * R))) if lod == 0 else max(8, int(round(lugs_per_m * R * 0.45)))
    st = (6, 2)[lod]
    for k in range(n):
        a0 = 2 * math.pi * k / n
        for h in (-1, 1):
            verts, faces = [], []
            for i in range(st + 1):
                s = i / st
                yo = h * (0.03 + s * (hw - 0.03 + 0.03))
                a = a0 + (0.42 * w / R) * s + (math.pi / n) * (h > 0)
                over = max(0.0, abs(yo) - (hw - 0.07))
                rb = Rb - 0.01 - over * 1.4
                top = rb + 0.01 + hl * (1.0 - 0.35 * over / 0.06)
                lw = (0.07 if R > 0.6 else 0.05) * (1.0 + 0.15 * s)
                for rr, half_w in ((rb, lw * 0.5), (rb, -lw * 0.5), (top, -lw * 0.38), (top, lw * 0.38)):
                    aa = a + half_w / rr
                    verts.append((rr * math.cos(aa), rr * math.sin(aa), yo))
            for i in range(st):
                b0, b1 = i * 4, (i + 1) * 4
                for q in range(4):
                    q1 = (q + 1) % 4
                    faces.append((b0 + q, b0 + q1, b1 + q1, b1 + q))
            faces.append((0, 3, 2, 1))
            e = st * 4
            faces.append((e, e + 1, e + 2, e + 3))
            ob = K._object("lug", verts, faces, 40.0)
            ob.matrix_world = T
            M.add(ob, "paint", TYRE, 0.55)


# --- the big tractor --------------------------------------------------------------------------------------------

def grille(M, x, hy, z0, z1, slats, lod, frame_rgb=BLACK):
    """A slatted grille facing +x: a dark back, a frame round it and its slats."""
    M.add(box("grilleback", (x - 0.03, 0.0, (z0 + z1) / 2), (0.03, 2 * hy, z1 - z0)), "paint", BLACK, 0.3)
    if lod == 2:
        return
    M.add(rbox("grilleframe_t", (x, 0.0, z1), (0.05, 2 * hy + 0.04, 0.04), r=0.012, seg=(2, 0)[lod]), "paint", frame_rgb, 0.3)
    M.add(rbox("grilleframe_b", (x, 0.0, z0), (0.05, 2 * hy + 0.04, 0.04), r=0.012, seg=(2, 0)[lod]), "paint", frame_rgb, 0.3)
    for s in (-1, 1):
        M.add(rbox("grilleframe_s", (x, s * hy, (z0 + z1) / 2), (0.05, 0.04, z1 - z0), r=0.012, seg=(2, 0)[lod]), "paint", frame_rgb, 0.3)
    k_n = slats if lod == 0 else max(3, slats // 3)
    for k in range(k_n):
        z = z0 + (z1 - z0) * (k + 0.5) / k_n
        M.add(box("slat", (x - 0.005, 0.0, z), (0.035, 2 * hy - 0.02, (z1 - z0) / k_n * 0.45), rot=Matrix.Rotation(math.radians(-25), 4, "Y")), "paint", GRAPHITE, 0.3)


def louvres(M, x0, x1, y, z0, z1, n, lod, rgb=BLACK):
    """A panel of louvres on a side, each slat standing a little proud."""
    if lod == 2:
        return
    s = 1 if y > 0 else -1
    M.add(box("louvrepanel", ((x0 + x1) / 2, y, (z0 + z1) / 2), (x1 - x0, 0.012, z1 - z0)), "paint", BLACK, 0.3)
    if lod == 0:
        for k in range(n):
            z = z0 + (z1 - z0) * (k + 0.5) / n
            M.add(box("louvre", ((x0 + x1) / 2, y + s * 0.01, z), (x1 - x0 - 0.02, 0.012, (z1 - z0) / n * 0.5), rot=Matrix.Rotation(math.radians(s * 30), 4, "X")), "paint", rgb, 0.3)


def cab(M, cx0, cx1, cy, cz0, cz1, body, roof, lod, door_x=None, roof_over=0.12, beacons=2, front_lights=4, steps_z=(0.42, 0.72, 1.02)):
    """A modern cab: glass all round with rounded corners, slim posts, a door with its handle and hinges, the roof
    with its visor, lights and beacons, the handrails and steps up to the door, and the seat, wheel and console."""
    sg = (3, 1, 0)[lod]
    xm = (cx0 + cx1) / 2
    L = cx1 - cx0
    zm = (cz0 + cz1) / 2
    M.add(rbox("cabfloor", (xm, 0.0, cz0 - 0.08), (L + 0.08, 2 * cy, 0.2), r=0.04, seg=sg), "enamel", body, 0.4)
    M.add(rbox("glass", (xm, 0.0, zm), (L - 0.03, 2 * cy - 0.03, cz1 - cz0), r=0.16, seg=(4, 2, 0)[lod]), "lens", GLASS, 0.0)
    for x in (cx0 + 0.04, cx1 - 0.04):
        for s in (-1, 1):
            M.add(rbox("post", (x, s * (cy - 0.03), zm), (0.08, 0.08, cz1 - cz0), r=0.025, seg=sg), "paint", BLACK, 0.25)
    M.add(rbox("lowband", (xm, 0.0, cz0 + 0.04), (L + 0.02, 2 * cy + 0.02, 0.08), r=0.02, seg=sg), "paint", BLACK, 0.3)
    if lod < 2:
        dx = door_x if door_x is not None else xm + 0.2
        M.add(rbox("doorpost", (dx, cy, zm), (0.06, 0.06, cz1 - cz0), r=0.02, seg=sg), "paint", BLACK, 0.25)
        M.add(rbox("topband", (xm, 0.0, cz1 - 0.02), (L + 0.02, 2 * cy + 0.02, 0.05), r=0.015, seg=sg), "paint", BLACK, 0.3)
    if lod == 0:
        # (the door: its handle, two hinges, a seal line; the rear window's frame)
        M.add(rbox("doorhandle", (dx - 0.06, cy + 0.02, zm - 0.05), (0.04, 0.03, 0.18), r=0.01, seg=2), "paint", BLACK, 0.3)
        for z in (cz0 + 0.35, cz1 - 0.35):
            M.add(rbox("hinge", (cx0 + 0.12, cy + 0.02, z), (0.07, 0.03, 0.06), r=0.01, seg=2), "paint", BLACK, 0.3)
        M.add(box("doorseal", ((cx0 + dx) / 2, cy + 0.003, zm + 0.15), (dx - cx0 - 0.12, 0.006, 0.012)), "paint", BLACK, 0.3)
        M.add(box("wiperpark", (cx1 + 0.002, 0.0, cz0 + 0.12), (0.012, 2 * cy - 0.2, 0.02)), "paint", BLACK, 0.3)
        for k in range(2):
            yb = -0.35 + k * 0.6
            arm = sweep("wiperarm", [(cx1 + 0.01, yb, cz0 + 0.1), (cx1 + 0.015, yb + 0.12, cz0 + 0.6)], 0.01, 4, square=True)
            M.add(arm, "paint", BLACK, 0.2)
            M.add(box("wiperblade", (cx1 + 0.02, yb + 0.13, cz0 + 0.62), (0.015, 0.02, 0.62), rot=Matrix.Rotation(math.radians(-12), 4, "X")), "paint", BLACK, 0.2)
        M.add(rbox("rearwiper", (cx0 - 0.01, 0.0, cz1 - 0.4), (0.015, 0.02, 0.5), r=0.005, seg=1), "paint", BLACK, 0.2)
    # (the roof: a rounded lid with a visor over the windscreen, the lights in its front and back)
    rz0, rz1 = cz1 + 0.02, cz1 + 0.2
    if lod < 2:
        M.add(loft_round("roof", [(cx1 + roof_over, cy + 0.04, rz0 + 0.02, rz1 - 0.04, 0.08, 0.02),
                                  (cx1 + roof_over - 0.06, cy + 0.1, rz0, rz1, 0.1, 0.02),
                                  (cx0 - 0.05, cy + 0.1, rz0, rz1, 0.1, 0.02),
                                  (cx0 - 0.1, cy + 0.06, rz0 + 0.02, rz1 - 0.03, 0.08, 0.02)], n=(4, 1)[lod]), "enamel", roof, 0.2)
        M.add(rbox("rooflip", (xm + roof_over / 2, 0.0, rz0 - 0.005), (L + roof_over + 0.12, 2 * cy + 0.2, 0.035), r=0.012, seg=(2, 0)[lod]), "paint", BLACK, 0.3)
    else:
        M.add(box("roof", (xm + roof_over / 2, 0.0, (rz0 + rz1) / 2), (L + roof_over + 0.1, 2 * cy + 0.2, rz1 - rz0)), "enamel", roof, 0.2)
    if lod == 0:
        for k in range(3):
            M.add(rbox("roofvent", (xm - 0.25 + k * 0.25, 0.0, rz1 + 0.01), (0.16, 0.5, 0.03), r=0.01, seg=2), "paint", BLACK, 0.3)
    if lod < 2:
        ys = [(-0.6, -0.4, 0.4, 0.6), (-0.5, 0.5)][front_lights < 4][: front_lights]
        for yy in ys:
            lamp(M, (cx1 + roof_over - 0.04, yy * cy / 0.78, rz0 + 0.06), 1, (0.06, 0.13, 0.08), lod)
        for yy in (-0.5, 0.5):
            lamp(M, (cx0 - 0.08, yy * cy / 0.78, rz0 + 0.06), -1, (0.06, 0.13, 0.08), lod)
        for s in ((-1, 1) if beacons == 2 else (-1,)):
            beacon(M, (cx0 + 0.15, s * (cy - 0.06), rz1), lod)
            M.add(rod("aerial", (cx0 + 0.3, 0.35, rz1), (cx0 + 0.3, 0.35, rz1 + 0.5), 0.005, 4), "paint", BLACK, 0.2) if (lod == 0 and s == -1) else None
        for s in (-1, 1):
            mirror(M, (cx1 - 0.02, s * cy, cz1 - 0.25), s, lod)
    # (the handrails and steps up to the door, on the right)
    if lod < 2:
        sx = dx - 0.1
        for k, z in enumerate(steps_z):
            tread_step(M, sx, cy + 0.06, z, 0.42, 0.24, lod)
        for x in (sx - 0.22, sx + 0.22):
            M.add(rbox("stepside", (x, cy + 0.07, (steps_z[0] + steps_z[-1]) / 2), (0.03, 0.22, steps_z[-1] - steps_z[0] + 0.1), r=0.01, seg=(1, 0)[lod]), "paint", BLACK, 0.5)
        for x in (cx0 + 0.07, dx + 0.07):
            M.add(sweep("handrail", [(x, cy + 0.02, cz0 + 0.15), (x, cy + 0.08, cz0 + 0.2), (x, cy + 0.08, cz0 + 0.95), (x, cy + 0.02, cz0 + 1.0)], 0.014, (8, 4)[lod]), "steel", GRAPHITE, 0.3)
    # (inside: the seat, the wheel and its column, the dashboard pod, the armrest console and joystick)
    if lod == 0:
        seat(M, cx0 + L * 0.42, cz0 + 0.38, lod)
        steering(M, (cx1 - 0.48, 0.0, cz0 + 0.78), -38, 0.2, lod)
        M.add(rod("column", (cx1 - 0.3, 0.0, cz0 + 0.2), (cx1 - 0.46, 0.0, cz0 + 0.74), 0.035, 10), "paint", SEAT, 0.2)
        M.add(rbox("pod", (cx1 - 0.36, 0.0, cz0 + 0.55), (0.16, 0.36, 0.2), r=0.04, seg=2, rot=Matrix.Rotation(math.radians(-30), 4, "Y")), "paint", SEAT, 0.2)
        M.add(rbox("console", (cx0 + L * 0.42 + 0.05, -0.33, cz0 + 0.45), (0.55, 0.16, 0.12), r=0.03, seg=2), "paint", SEAT, 0.2)
        M.add(rod("joystick", (cx0 + L * 0.42 + 0.2, -0.33, cz0 + 0.5), (cx0 + L * 0.42 + 0.22, -0.33, cz0 + 0.62), 0.02, 8), "paint", SEAT, 0.2)
        for yy in (0.1, -0.1):
            M.add(rbox("pedal", (cx1 - 0.18, yy, cz0 + 0.1), (0.04, 0.08, 0.12), r=0.01, seg=1, rot=Matrix.Rotation(math.radians(-30), 4, "Y")), "paint", SEAT, 0.2)


def rear_linkage(M, xb, zb, spread, body, lod, scale=1.0):
    """The three-point linkage behind xb: lower links, lift arms and rods, the top link, the stabilisers, PTO and its
    guard, the drawbar and hitch, and the spool valves with their couplers and hoses."""
    s_ = scale
    if lod == 2:
        return
    sq = lod == 0
    for s in (-1, 1):
        y = s * spread
        M.add(sweep("lowerlink", [(xb, y * 0.85, zb - 0.3 * s_), (xb - 0.65 * s_, y, zb - 0.45 * s_)], 0.045 * s_, 4, square=True), "paint", GRAPHITE, 0.5)
        if sq:
            M.add(lathe("balljoint", [(0.0, -0.03 * s_), (0.05 * s_, -0.03 * s_), (0.05 * s_, 0.03 * s_), (0.0, 0.03 * s_)], 12, at=(xb - 0.67 * s_, y, zb - 0.47 * s_)), "paint", GRAPHITE, 0.5)
            M.add(sweep("stabiliser", [(xb + 0.1, y * 0.6, zb - 0.32 * s_), (xb - 0.4 * s_, y * 0.95, zb - 0.38 * s_)], 0.022 * s_, 6), "steel", STEEL, 0.5)
        M.add(sweep("liftarm", [(xb, y * 0.75, zb + 0.32 * s_), (xb - 0.4 * s_, y * 0.82, zb + 0.24 * s_)], 0.04 * s_, 4, square=True), "enamel", body, 0.5)
        M.add(rod("liftrod", (xb - 0.38 * s_, y * 0.85, zb + 0.22 * s_), (xb - 0.42 * s_, y * 0.92, zb - 0.38 * s_), 0.022 * s_, (8, 4)[lod]), "steel", STEEL, 0.5)
        if sq:
            M.add(lathe("liftram", [(0.0, 0.0), (0.045 * s_, 0.0), (0.045 * s_, 0.35 * s_), (0.0, 0.36 * s_)], 12, at=(xb + 0.05, y * 0.65, zb - 0.05 * s_)), "paint", GRAPHITE, 0.5)
    M.add(sweep("toplink", [(xb, 0.0, zb + 0.25 * s_), (xb - 0.6 * s_, 0.0, zb + 0.15 * s_)], 0.035 * s_, (10, 6)[lod]), "paint", GRAPHITE, 0.5)
    if sq:
        M.add(lathe("toplinkbarrel", [(0.0, 0.0), (0.05 * s_, 0.0), (0.05 * s_, 0.22 * s_), (0.0, 0.22 * s_)], 12, at=(0.0, 0.0, 0.0)), "paint", GRAPHITE, 0.5).matrix_world = (
            Matrix.Translation((xb - 0.2 * s_, 0.0, zb + 0.22 * s_)) @ Matrix.Rotation(math.radians(-80), 4, "Y"))
    # (the PTO shaft under its guard)
    pto = lathe("pto", [(0.0, 0.0), (0.035 * s_, 0.0), (0.035 * s_, 0.14 * s_), (0.025 * s_, 0.16 * s_), (0.0, 0.16 * s_)], (12, 6)[lod])
    pto.matrix_world = Matrix.Translation((xb - 0.02, 0.0, zb - 0.15 * s_)) @ Matrix.Rotation(-math.pi / 2, 4, "Y")
    M.add(pto, "steel", STEEL, 0.4)
    M.add(arc_band("ptoguard", xb - 0.1 * s_, zb - 0.15 * s_, 0.1 * s_, 0.0, math.pi, -0.13 * s_, 0.13 * s_, 0.01, (10, 4)[lod]), "enamel", body, 0.4)
    # (the drawbar and the pick-up hitch's hook)
    M.add(rbox("drawbar", (xb - 0.35 * s_, 0.0, zb - 0.62 * s_), (0.6 * s_, 0.18 * s_, 0.07 * s_), r=0.015, seg=(2, 0)[lod]), "paint", GRAPHITE, 0.5)
    M.add(rbox("hitchframe", (xb - 0.12 * s_, 0.0, zb - 0.45 * s_), (0.15 * s_, 0.5 * s_, 0.25 * s_), r=0.02, seg=(2, 0)[lod]), "paint", GRAPHITE, 0.5)
    if sq:
        M.add(rod("drawpin", (xb - 0.55 * s_, 0.0, zb - 0.7 * s_), (xb - 0.55 * s_, 0.0, zb - 0.5 * s_), 0.02 * s_, 8), "steel", STEEL, 0.4)
        # (the spool valves: four pairs of couplers with their dust caps, and two hoses hanging)
        for k in range(4):
            yy = (-0.24 + k * 0.16) * s_
            for dz in (0.0, 0.07 * s_):
                M.add(rod("coupler", (xb + 0.02, yy, zb + 0.5 * s_ + dz), (xb - 0.06, yy, zb + 0.5 * s_ + dz), 0.018 * s_, 8), "paint", COUPLER if k % 2 else RED_LENS, 0.3)
        M.add(rbox("valveblock", (xb + 0.06, 0.0, zb + 0.53 * s_), (0.08, 0.7 * s_, 0.16 * s_), r=0.015, seg=2), "paint", GRAPHITE, 0.4)
        for yy in (-0.24, -0.08):
            hose(M, [(xb - 0.06, yy * s_, zb + 0.5 * s_), (xb - 0.3 * s_, yy * s_, zb + 0.48 * s_), (xb - 0.3 * s_, (yy - 0.1) * s_, zb + 0.1 * s_), (xb - 0.15 * s_, (yy - 0.12) * s_, zb - 0.1 * s_)], 0.013 * s_, lod)


def tractor(name, livery, lod):
    M = K.Model(name)
    body, rim, roof, stripe = LIVERIES[livery]
    sg = (3, 1, 0)[lod]
    xr, hr, Rr_, wr = REAR
    xf, hf, Rf, wf = FRONT
    # --- wheels and wings
    for side in (-1, 1):
        wheel(M, REAR, side, rim, lod)
        wheel(M, FRONT, side, rim, lod)
        y0, y1 = sorted((side * (hr - wr / 2 - 0.06), side * (hr + wr / 2 + 0.06)))
        wing(M, "rearwing", xr, Rr_, Rr_ + 0.08, math.radians(8), math.radians(172), y0, y1, body, lod)
        y0, y1 = sorted((side * (hf - wf / 2 - 0.04), side * (hf + wf / 2 + 0.04)))
        wing(M, "frontwing", xf, Rf, Rf + 0.1, math.radians(25), math.radians(150), y0, y1, BLACK, lod, surface="paint")
        if lod < 2:
            # (the rear wing's lamps: the tail cluster at its back, an amber flasher on its top corner, a reflector)
            lamp(M, (xr - Rr_ - 0.07, side * hr, Rr_ + 0.35), -1, (0.07, 0.24, 0.15), lod, lens=RED_LENS)
            lamp(M, (xr - 0.2, side * (hr + wr / 2 - 0.05), Rr_ * 2 + 0.17), 1, (0.12, 0.1, 0.07), lod, lens=AMBER)
            M.add(rbox("frontstay", (xf - 0.05, side * (hf - wf / 2 - 0.1), Rf + 0.35), (0.08, 0.2, 0.35), r=0.02, seg=sg), "paint", BLACK, 0.4)
            lamp(M, (xf + 0.25, side * (hf + 0.05), Rf + 0.62), 1, (0.08, 0.12, 0.07), lod, lens=AMBER)
            if lod == 0:
                M.add(rbox("wingstay", (xr - 0.35, side * (hr - wr / 2 - 0.02), Rr_ + 0.55), (0.05, 0.05, 0.5), r=0.012, seg=2), "paint", BLACK, 0.4)
                M.add(rbox("reflector", (xr - Rr_ - 0.1, side * hr, Rr_ + 0.18), (0.01, 0.12, 0.05), r=0.01, seg=1), "lens", RED_LENS, 0.05)
    # --- the axles, the engine block under the bonnet, its hoses
    M.add(rbox("sump", (1.25, 0.0, 0.78), (2.4, 0.62, 0.5), r=0.05, seg=sg), "paint", GRAPHITE, 0.5)
    M.add(rbox("axle", (xf, 0.0, Rf), (0.3, 2 * hf - 0.3, 0.22), r=0.05, seg=sg), "paint", GRAPHITE, 0.5)
    M.add(rbox("rearaxle", (xr, 0.0, Rr_), (0.4, 2 * hr - 0.4, 0.34), r=0.08, seg=sg), "paint", GRAPHITE, 0.5)
    M.add(rbox("transmission", (-0.9, 0.0, 0.95), (1.9, 0.8, 0.7), r=0.08, seg=sg), "enamel", body, 0.45)
    if lod == 0:
        for s in (-1, 1):
            M.add(lathe("finaldrive", [(0.0, 0.0), (0.26, 0.0), (0.26, 0.12), (0.2, 0.2), (0.0, 0.2)], 24), "paint", GRAPHITE, 0.5).matrix_world = (
                Matrix.Translation((xr, s * (hr - wr / 2 - 0.22), Rr_)) @ Matrix.Rotation(-s * math.pi / 2, 4, "X"))
            M.add(lathe("knuckle", [(0.0, 0.0), (0.13, 0.0), (0.13, 0.1), (0.09, 0.16), (0.0, 0.16)], 16), "paint", GRAPHITE, 0.5).matrix_world = (
                Matrix.Translation((xf, s * (hf - wf / 2 - 0.12), Rf)) @ Matrix.Rotation(-s * math.pi / 2, 4, "X"))
        M.add(rod("trackrod", (xf - 0.18, -hf + 0.35, Rf - 0.05), (xf - 0.18, hf - 0.35, Rf - 0.05), 0.022, 8), "steel", STEEL, 0.5)
        M.add(lathe("propshaft", [(0.0, 0.0), (0.045, 0.0), (0.045, 2.1), (0.0, 2.1)], 10), "steel", STEEL, 0.5).matrix_world = (
            Matrix.Translation((-0.4, 0.0, 0.5)) @ Matrix.Rotation(math.pi / 2, 4, "Y"))
        # (the engine's flank under the bonnet: block, filters, the turbo's pipes, the radiator's hoses)
        M.add(rbox("block", (1.15, 0.0, 1.15), (1.5, 0.5, 0.42), r=0.04, seg=2), "paint", GRAPHITE, 0.5)
        M.add(lathe("oilfilter", [(0.0, 0.0), (0.06, 0.0), (0.06, 0.2), (0.05, 0.22), (0.0, 0.22)], 14, at=(1.0, -0.33, 0.98)), "paint", (0.2, 0.2, 0.2), 0.4)
        M.add(lathe("fuelfilter", [(0.0, 0.0), (0.045, 0.0), (0.045, 0.16), (0.0, 0.17)], 12, at=(0.7, -0.33, 1.0)), "paint", (0.25, 0.25, 0.25), 0.4)
        hose(M, [(1.9, 0.2, 1.25), (1.6, 0.35, 1.2), (1.3, 0.35, 1.35), (1.0, 0.25, 1.38)], 0.03, lod)
        hose(M, [(1.9, -0.2, 0.95), (1.7, -0.33, 0.9), (1.4, -0.33, 1.0), (1.2, -0.3, 1.1)], 0.028, lod)
    # --- the bonnet: rounded all over, sloping to the nose; the nose cone, the grille and lamp clusters
    M.add(loft_round("bonnet", [(2.5, 0.4, 1.0, 1.66, 0.14, 0.03), (2.42, 0.45, 0.98, 1.74, 0.18, 0.03), (1.9, 0.48, 0.98, 1.8, 0.2, 0.03),
                                (1.0, 0.5, 0.98, 1.85, 0.2, 0.03), (0.05, 0.52, 0.98, 1.9, 0.2, 0.03)], n=(5, 2, 0)[lod]), "enamel", body, 0.25)
    M.add(rbox("bonnetfoot", (1.3, 0.0, 0.96), (2.3, 0.94, 0.14), r=0.03, seg=sg), "paint", BLACK, 0.4)
    grille(M, 2.53, 0.34, 1.06, 1.5, 14, lod)
    for s in (-1, 1):
        lamp(M, (2.47, s * 0.27, 1.6), 1, (0.08, 0.2, 0.11), lod)
        if lod < 2:
            lamp(M, (2.36, s * 0.43, 1.68), 1, (0.08, 0.1, 0.08), lod)
    if lod < 2:
        for s in (-1, 1):
            M.add(rbox("stripe", (1.25, s * 0.505, 1.66), (1.9, 0.012, 0.045), r=0.005, seg=(1, 0)[lod]), "enamel", stripe, 0.2)
            louvres(M, 1.85, 2.3, s * 0.48, 1.12, 1.55, 6, lod)
            louvres(M, 0.6, 1.2, s * 0.515, 1.2, 1.6, 7, lod, rgb=body)
            M.add(rbox("lampbrow", (2.44, s * 0.27, 1.69), (0.08, 0.26, 0.035), r=0.012, seg=(2, 0)[lod]), "enamel", body, 0.2)
    # (the front weight block, built of slabs, on its frame; the front linkage)
    M.add(rbox("weightframe", (2.62, 0.0, 0.8), (0.25, 0.6, 0.44), r=0.03, seg=sg), "paint", GRAPHITE, 0.5)
    nsl = (8, 3, 1)[lod]
    for k in range(nsl):
        M.add(rbox("weight", (2.78, 0.0, 0.62 + 0.36 * (k + 0.5) / nsl), (0.36, 0.72, 0.36 / nsl - 0.012), r=0.02, seg=sg), "enamel", body, 0.5)
    if lod < 2:
        M.add(rbox("weightface", (2.95, 0.0, 0.8), (0.02, 0.36, 0.2), r=0.01, seg=(1, 0)[lod]), "enamel", stripe, 0.4)
        for s in (-1, 1):
            M.add(sweep("frontlink", [(2.5, s * 0.3, 0.6), (2.98, s * 0.36, 0.45)], 0.04, 4, square=True), "enamel", body, 0.5)
        M.add(rbox("tow", (2.75, 0.0, 0.5), (0.16, 0.18, 0.08), r=0.02, seg=(2, 0)[lod]), "paint", GRAPHITE, 0.5)
    # --- the cab
    cab(M, -1.95, -0.15, 0.78, 1.32, 2.92, body, roof, lod, door_x=-0.85)
    # (the exhaust beside the cab's front post with its heat shield, the air pre-cleaner on the other)
    cx1 = -0.15
    M.add(rod("exhaust", (cx1 + 0.12, -0.6, 1.55), (cx1 + 0.12, -0.6, 2.85), 0.07, (16, 8, 4)[lod]), "steel", STEEL, 0.4)
    if lod < 2:
        M.add(sweep("exhausttip", curve((cx1 + 0.12, -0.6, 2.84), (cx1 + 0.12, -0.6, 2.96), (cx1 + 0.16, -0.6, 3.0), (cx1 + 0.28, -0.6, 3.02), n=(8, 3)[lod]), 0.072, (16, 6)[lod]), "steel", STEEL, 0.4)
        hs = [(0.09, 1.9)] + [(0.095 if k % 2 == 0 else 0.088, 1.92 + k * 0.045) for k in range(15)] + [(0.09, 2.6)] if lod == 0 else [(0.09, 1.9), (0.095, 1.92), (0.095, 2.58), (0.09, 2.6)]
        M.add(lathe("heatshield", hs, (24, 8)[lod], at=(cx1 + 0.12, -0.6, 0.0)), "paint", BLACK, 0.4)
        M.add(rbox("airbox", (cx1 + 0.12, 0.6, 1.62), (0.3, 0.3, 0.6), r=0.06, seg=sg), "paint", BLACK, 0.4)
    if lod == 0:
        M.add(lathe("precleaner", [(0.0, 0.0), (0.09, 0.0), (0.1, 0.06), (0.1, 0.18), (0.06, 0.22), (0.0, 0.22)], 16, at=(cx1 + 0.12, 0.6, 1.92)), "paint", BLACK, 0.4)
        hose(M, [(cx1 + 0.12, 0.6, 1.32), (cx1 + 0.3, 0.5, 1.25), (0.5, 0.35, 1.5), (0.8, 0.2, 1.5)], 0.06, lod)
    # (the fuel tank under the door with its cap, the AdBlue tank and its blue cap, the battery box, the toolbox)
    M.add(rbox("fueltank", (-0.45, 0.62, 0.8), (0.75, 0.3, 0.5), r=0.08, seg=(4, 1, 0)[lod]), "paint", BLACK, 0.45)
    if lod < 2:
        M.add(rbox("adblue", (-0.45, -0.62, 0.8), (0.5, 0.26, 0.42), r=0.06, seg=sg), "paint", BLACK, 0.45)
        M.add(rbox("battery", (0.35, 0.58, 0.72), (0.38, 0.22, 0.3), r=0.03, seg=sg), "paint", BLACK, 0.45)
    if lod == 0:
        M.add(lathe("fuelcap", [(0.0, 0.0), (0.05, 0.0), (0.05, 0.04), (0.0, 0.045)], 14, at=(-0.2, 0.62, 1.05)), "paint", GRAPHITE, 0.3)
        M.add(lathe("bluecap", [(0.0, 0.0), (0.04, 0.0), (0.04, 0.035), (0.0, 0.04)], 12, at=(-0.3, -0.62, 1.01)), "paint", BLUECAP, 0.3)
        M.add(rbox("toolbox", (-1.7, -0.75, 1.05), (0.4, 0.14, 0.25), r=0.02, seg=2), "paint", BLACK, 0.45)
    # --- the back: the housing, the linkage, the plate and its lamp
    M.add(rbox("rearhousing", (-1.85, 0.0, 0.95), (0.4, 0.9, 0.7), r=0.06, seg=sg), "enamel", body, 0.5)
    rear_linkage(M, -2.0, 0.95, 0.42, body, lod)
    if lod < 2:
        M.add(rbox("plate", (-1.99, 0.55, 1.42), (0.02, 0.32, 0.17), r=0.01, seg=(1, 0)[lod]), "paint", PLATE, 0.2)
        M.add(rbox("platebracket", (-1.97, 0.55, 1.42), (0.03, 0.36, 0.21), r=0.01, seg=(1, 0)[lod]), "paint", BLACK, 0.3)
    return M


# --- the smaller utility tractor (beenster's second set of pictures): a plain workhorse of about 75 hp, a narrow
# rounded bonnet, a tall glassy cab with thin posts, the exhaust standing up beside the cab's front corner
U_REAR = (-0.95, 0.86, 0.78, 0.48)
U_FRONT = (1.45, 0.8, 0.53, 0.36)


def utility(name, livery, lod):
    M = K.Model(name)
    body, rim, roof, stripe = LIVERIES[livery]
    sg = (3, 1, 0)[lod]
    xr, hr, Rr_, wr = U_REAR
    xf, hf, Rf, wf = U_FRONT
    for side in (-1, 1):
        wheel(M, U_REAR, side, rim, lod)
        wheel(M, U_FRONT, side, rim, lod)
        y0, y1 = sorted((side * (hr - wr / 2 - 0.05), side * (hr + wr / 2 + 0.05)))
        wing(M, "rearwing", xr, Rr_, Rr_ + 0.07, math.radians(10), math.radians(170), y0, y1, body, lod)
        y0, y1 = sorted((side * (hf - wf / 2 - 0.03), side * (hf + wf / 2 + 0.03)))
        wing(M, "frontwing", xf, Rf, Rf + 0.08, math.radians(30), math.radians(150), y0, y1, BLACK, lod, surface="paint")
        if lod < 2:
            lamp(M, (xr - Rr_ - 0.05, side * hr, Rr_ + 0.25), -1, (0.06, 0.2, 0.13), lod, lens=RED_LENS)
            M.add(rbox("frontstay", (xf - 0.03, side * (hf - wf / 2 - 0.07), Rf + 0.28), (0.06, 0.14, 0.3), r=0.015, seg=sg), "paint", BLACK, 0.4)
            lamp(M, (xr - 0.1, side * (hr + wr / 2 - 0.04), Rr_ * 2 + 0.13), 1, (0.1, 0.08, 0.06), lod, lens=AMBER)
            lamp(M, (xf + 0.2, side * (hf + 0.04), Rf + 0.5), 1, (0.07, 0.1, 0.06), lod, lens=AMBER)
    # (the axles and engine; the narrow, round-shouldered bonnet sloping down to the nose)
    M.add(rbox("sump", (0.95, 0.0, 0.62), (2.0, 0.5, 0.42), r=0.04, seg=sg), "paint", GRAPHITE, 0.5)
    M.add(rbox("axle", (xf, 0.0, Rf), (0.24, 2 * hf - 0.26, 0.18), r=0.04, seg=sg), "paint", GRAPHITE, 0.5)
    M.add(rbox("rearaxle", (xr, 0.0, Rr_), (0.32, 2 * hr - 0.36, 0.28), r=0.06, seg=sg), "paint", GRAPHITE, 0.5)
    M.add(rbox("transmission", (-0.65, 0.0, 0.8), (1.4, 0.66, 0.56), r=0.06, seg=sg), "enamel", body, 0.45)
    if lod == 0:
        for s in (-1, 1):
            M.add(lathe("finaldrive", [(0.0, 0.0), (0.22, 0.0), (0.22, 0.1), (0.16, 0.16), (0.0, 0.16)], 24), "paint", GRAPHITE, 0.5).matrix_world = (
                Matrix.Translation((xr, s * (hr - wr / 2 - 0.18), Rr_)) @ Matrix.Rotation(-s * math.pi / 2, 4, "X"))
            M.add(lathe("knuckle", [(0.0, 0.0), (0.1, 0.0), (0.1, 0.08), (0.07, 0.12), (0.0, 0.12)], 16), "paint", GRAPHITE, 0.5).matrix_world = (
                Matrix.Translation((xf, s * (hf - wf / 2 - 0.1), Rf)) @ Matrix.Rotation(-s * math.pi / 2, 4, "X"))
        M.add(rod("trackrod", (xf - 0.14, -hf + 0.28, Rf - 0.04), (xf - 0.14, hf - 0.28, Rf - 0.04), 0.018, 8), "steel", STEEL, 0.5)
        M.add(rbox("block", (0.95, 0.0, 0.98), (1.3, 0.42, 0.36), r=0.04, seg=2), "paint", GRAPHITE, 0.5)
        M.add(lathe("oilfilter", [(0.0, 0.0), (0.05, 0.0), (0.05, 0.17), (0.04, 0.19), (0.0, 0.19)], 14, at=(0.8, -0.28, 0.82)), "paint", (0.2, 0.2, 0.2), 0.4)
        hose(M, [(1.6, 0.15, 1.05), (1.35, 0.3, 1.0), (1.1, 0.3, 1.12), (0.9, 0.2, 1.15)], 0.026, lod)
    M.add(loft_round("bonnet", [(2.07, 0.33, 0.84, 1.33, 0.13, 0.03), (2.0, 0.38, 0.83, 1.42, 0.17, 0.03), (1.5, 0.41, 0.83, 1.5, 0.2, 0.03),
                                (0.7, 0.43, 0.83, 1.54, 0.2, 0.03), (0.05, 0.44, 0.83, 1.57, 0.2, 0.03)], n=(5, 2, 0)[lod]), "enamel", body, 0.25)
    M.add(rbox("bonnetfoot", (1.05, 0.0, 0.8), (1.95, 0.8, 0.12), r=0.03, seg=sg), "paint", BLACK, 0.4)
    grille(M, 2.1, 0.27, 0.9, 1.3, 11, lod)
    for s in (-1, 1):
        lamp(M, (2.04, s * 0.29, 1.24), 1, (0.08, 0.13, 0.17), lod)
    if lod < 2:
        for s in (-1, 1):
            M.add(rbox("stripe", (1.1, s * 0.43, 1.4), (1.6, 0.012, 0.04), r=0.005, seg=(1, 0)[lod]), "enamel", stripe, 0.2)
            louvres(M, 1.5, 1.95, s * 0.405, 0.95, 1.25, 5, lod)
            louvres(M, 0.45, 0.95, s * 0.44, 1.05, 1.4, 6, lod, rgb=body)
    M.add(rbox("fronthitch", (2.18, 0.0, 0.6), (0.3, 0.5, 0.22), r=0.03, seg=sg), "paint", GRAPHITE, 0.5)
    if lod < 2:
        M.add(rbox("bumper", (2.25, 0.0, 0.78), (0.08, 0.7, 0.1), r=0.02, seg=sg), "paint", BLACK, 0.4)
    # --- the cab: more glass than frame
    cx0, cx1, cy, cz0, cz1 = -1.55, -0.12, 0.62, 1.12, 2.55
    cab(M, cx0, cx1, cy, cz0, cz1, body, roof, lod, door_x=-0.6, roof_over=0.16, beacons=1, front_lights=2, steps_z=(0.38, 0.7))
    # (the exhaust standing up at the cab's front corner with its guard, the air intake on the other)
    ey = cy + 0.1
    M.add(rod("exhaust", (cx1 + 0.06, ey, 1.4), (cx1 + 0.06, ey, 2.72), 0.055, (16, 8, 4)[lod]), "steel", STEEL, 0.4)
    if lod < 2:
        M.add(lathe("exhaustguard", [(0.075, 1.8), (0.08, 1.82), (0.08, 2.28), (0.075, 2.3)], (20, 6)[lod], at=(cx1 + 0.06, ey, 0.0)), "paint", BLACK, 0.4)
        M.add(rbox("exhaustbracket", (cx1 + 0.02, cy + 0.05, 2.0), (0.04, 0.1, 0.05), r=0.01, seg=(1, 0)[lod]), "paint", BLACK, 0.4)
        M.add(sweep("exhaustpipe", curve((cx1 + 0.3, 0.38, 1.25), (cx1 + 0.15, 0.5, 1.28), (cx1 + 0.06, ey, 1.32), (cx1 + 0.06, ey, 1.42), n=(8, 3)[lod]), 0.05, (12, 6)[lod]), "steel", STEEL, 0.4)
        M.add(rod("intake", (cx1 + 0.06, -ey, 1.4), (cx1 + 0.06, -ey, 2.15), 0.05, (16, 6)[lod]), "paint", BLACK, 0.4)
    if lod == 0:
        M.add(lathe("rainc", [(0.0, 0.0), (0.06, 0.0), (0.07, 0.04), (0.07, 0.1), (0.0, 0.12)], 14, at=(cx1 + 0.06, -ey, 2.15)), "paint", BLACK, 0.4)
        M.add(sweep("exhausttip", curve((cx1 + 0.06, ey, 2.7), (cx1 + 0.06, ey, 2.78), (cx1 + 0.09, ey, 2.8), (cx1 + 0.16, ey, 2.8), n=6), 0.056, 12), "steel", STEEL, 0.4)
    M.add(rbox("fueltank", (-0.35, -0.5, 0.68), (0.6, 0.26, 0.42), r=0.07, seg=(4, 1, 0)[lod]), "paint", BLACK, 0.45)
    if lod == 0:
        M.add(lathe("fuelcap", [(0.0, 0.0), (0.045, 0.0), (0.045, 0.035), (0.0, 0.04)], 12, at=(-0.2, -0.5, 0.89)), "paint", GRAPHITE, 0.3)
        M.add(rbox("battery", (0.3, 0.5, 0.62), (0.32, 0.18, 0.26), r=0.03, seg=2), "paint", BLACK, 0.45)
    M.add(rbox("rearhousing", (-1.45, 0.0, 0.8), (0.35, 0.74, 0.56), r=0.05, seg=sg), "enamel", body, 0.5)
    rear_linkage(M, -1.6, 0.8, 0.34, body, lod, scale=0.82)
    if lod < 2:
        M.add(rbox("plate", (-1.64, -0.42, 1.18), (0.02, 0.28, 0.15), r=0.01, seg=(1, 0)[lod]), "paint", PLATE, 0.2)
    return M


# --- the compact tractor (beenster's third set): a little four-wheel-drive with a fold-up roll bar instead of a cab,
# an open seat between the rear wings and a front loader with its bucket set down on the ground
C_REAR = (-0.55, 0.6, 0.5, 0.34)
C_FRONT = (0.95, 0.55, 0.33, 0.22)


def compact(name, livery, lod):
    M = K.Model(name)
    body, rim, roof, stripe = LIVERIES[livery]
    sg = (3, 1, 0)[lod]
    xr, hr, Rr_, wr = C_REAR
    xf, hf, Rf, wf = C_FRONT
    for side in (-1, 1):
        wheel(M, C_REAR, side, rim, lod, lugs_per_m=36.0)
        wheel(M, C_FRONT, side, rim, lod, lugs_per_m=40.0)
        # (the rear wings: an arc over the tyre with a flat top the seat sits between, a handgrip on each)
        y0, y1 = sorted((side * (hr - wr / 2 - 0.04), side * (hr + wr / 2 + 0.04)))
        wing(M, "rearwing", xr, Rr_, Rr_ + 0.06, math.radians(5), math.radians(175), y0, y1, body, lod)
        M.add(rbox("wingtop", (xr, side * (hr - wr / 2 - 0.08), Rr_ * 2 + 0.06), (0.75, 0.12, 0.03), r=0.01, seg=(2, 0, 0)[lod]), "enamel", body, 0.3)
        if lod < 2:
            lamp(M, (xr - 0.14, side * (hr - 0.02), Rr_ * 2 + 0.14), -1, (0.05, 0.1, 0.08), lod, lens=RED_LENS)
            M.add(sweep("grip", [(xr + 0.15, side * (hr - wr / 2 - 0.12), Rr_ * 2 + 0.08), (xr + 0.17, side * (hr - wr / 2 - 0.12), Rr_ * 2 + 0.2),
                                 (xr + 0.35, side * (hr - wr / 2 - 0.12), Rr_ * 2 + 0.2), (xr + 0.37, side * (hr - wr / 2 - 0.12), Rr_ * 2 + 0.08)], 0.012, (8, 4)[lod]), "paint", BLACK, 0.3)
        # (the roll bar's legs up from behind the wings, their feet and the amber lamps on them)
        M.add(rbox("rollleg", (xr - 0.32, side * 0.47, 1.45), (0.07, 0.09, 1.0), r=0.02, seg=sg), "paint", BLACK, 0.3)
        if lod < 2:
            M.add(rbox("rollfoot", (xr - 0.32, side * 0.47, 0.92), (0.14, 0.14, 0.06), r=0.015, seg=sg), "paint", BLACK, 0.3)
            lamp(M, (xr - 0.27, side * 0.55, 1.42), -1, (0.05, 0.08, 0.12), lod, lens=AMBER)
            if lod == 0:
                M.add(rod("foldpin", (xr - 0.32, side * 0.42, 1.75), (xr - 0.32, side * 0.53, 1.75), 0.015, 8), "steel", STEEL, 0.3)
                for z in (0.96, 0.96):
                    M.add(lathe("footbolt", [(0.0, 0.0), (0.018, 0.0), (0.018, 0.015), (0.0, 0.02)], 6, at=(xr - 0.36, side * 0.47, z)), "steel", STEEL, 0.3)
    pts = [(xr - 0.32, -0.47, 1.9)]
    arc = [(xr - 0.32, -0.47 + 0.14 * (1 - math.cos(math.radians(t))), 2.06 + 0.14 * math.sin(math.radians(t))) for t in range(0, 91, (10, 30, 45)[lod])]
    arc += [(xr - 0.32, 0.47 - 0.14 * (1 - math.cos(math.radians(t))), 2.06 + 0.14 * math.sin(math.radians(t))) for t in range(90, -1, -(10, 30, 45)[lod])]
    M.add(sweep("rolltop", [pts[0]] + arc + [(xr - 0.32, 0.47, 1.9)], 0.04, (12, 6, 4)[lod]), "paint", BLACK, 0.3)
    # (the chassis, the axles, the footplate with its treads)
    M.add(rbox("sump", (0.6, 0.0, 0.45), (1.2, 0.34, 0.3), r=0.03, seg=sg), "paint", GRAPHITE, 0.5)
    M.add(rbox("axle", (xf, 0.0, Rf), (0.16, 2 * hf - 0.2, 0.13), r=0.03, seg=sg), "paint", GRAPHITE, 0.5)
    M.add(rbox("rearaxle", (xr, 0.0, Rr_), (0.26, 2 * hr - 0.28, 0.22), r=0.05, seg=sg), "paint", GRAPHITE, 0.5)
    M.add(rbox("transmission", (-0.35, 0.0, 0.6), (0.9, 0.42, 0.42), r=0.05, seg=sg), "enamel", body, 0.45)
    M.add(rbox("footplate", (0.0, 0.0, 0.48), (0.55, 1.02, 0.04), r=0.01, seg=(1, 0, 0)[lod]), "paint", BLACK, 0.5)
    if lod == 0:
        for k in range(6):
            M.add(box("platetread", (-0.2 + k * 0.08, 0.0, 0.505), (0.02, 0.98, 0.01)), "paint", GRAPHITE, 0.5)
        for s in (-1, 1):
            M.add(lathe("finaldrive", [(0.0, 0.0), (0.15, 0.0), (0.15, 0.07), (0.1, 0.11), (0.0, 0.11)], 20), "paint", GRAPHITE, 0.5).matrix_world = (
                Matrix.Translation((xr, s * (hr - wr / 2 - 0.12), Rr_)) @ Matrix.Rotation(-s * math.pi / 2, 4, "X"))
        M.add(rod("trackrod", (xf - 0.1, -hf + 0.2, Rf - 0.03), (xf - 0.1, hf - 0.2, Rf - 0.03), 0.014, 8), "steel", STEEL, 0.5)
        M.add(lathe("fuelcap", [(0.0, 0.0), (0.04, 0.0), (0.04, 0.03), (0.0, 0.035)], 12, at=(0.3, 0.0, 1.13)), "paint", GRAPHITE, 0.3)
        for yy in (0.15, -0.15):
            M.add(rbox("pedal", (0.12, yy, 0.6), (0.04, 0.08, 0.12), r=0.01, seg=1, rot=Matrix.Rotation(math.radians(-30), 4, "Y")), "paint", SEAT, 0.2)
        M.add(rod("lever", (-0.25, 0.22, 0.95), (-0.18, 0.26, 1.2), 0.012, 6), "paint", BLACK, 0.2)
        M.add(lathe("knob", [(0.0, 0.0), (0.02, 0.0), (0.022, 0.02), (0.0, 0.04)], 10, at=(-0.18, 0.26, 1.2)), "paint", BLACK, 0.2)
    # (the bonnet: short, sloping down to a wide nose; the grille, the lamps, the louvres)
    M.add(loft_round("bonnet", [(1.43, 0.28, 0.58, 0.92, 0.1, 0.02), (1.38, 0.31, 0.57, 0.99, 0.12, 0.02), (1.1, 0.33, 0.56, 1.06, 0.13, 0.02),
                                (0.6, 0.34, 0.56, 1.12, 0.13, 0.02), (0.22, 0.34, 0.56, 1.16, 0.13, 0.02)], n=(5, 2, 0)[lod]), "enamel", body, 0.25)
    grille(M, 1.45, 0.22, 0.64, 0.86, 8, lod)
    M.add(rbox("dash", (0.18, 0.0, 1.12), (0.14, 0.5, 0.22), r=0.04, seg=sg), "enamel", body, 0.3)
    for s in (-1, 1):
        lamp(M, (1.39, s * 0.2, 0.96), 1, (0.06, 0.12, 0.06), lod)
        if lod < 2:
            M.add(rbox("stripe", (0.8, s * 0.335, 1.04), (1.0, 0.012, 0.03), r=0.005, seg=(1, 0)[lod]), "enamel", stripe, 0.2)
            louvres(M, 0.35, 0.75, s * 0.338, 0.65, 0.95, 6, lod)
    # (the seat between the wings, the wheel)
    seat(M, -0.5, 1.06, lod, w=0.44, back=0.42, tilt=-12.0)
    if lod < 2:
        M.add(rbox("seatbase2", (-0.5, 0.0, 0.92), (0.36, 0.3, 0.2), r=0.03, seg=sg), "enamel", body, 0.4)
        M.add(rod("column", (0.16, 0.0, 1.1), (0.04, 0.0, 1.38), 0.03, (10, 6)[lod]), "paint", SEAT, 0.2)
        steering(M, (0.02, 0.0, 1.4), -40, 0.165, lod)
        M.add(rbox("hitchplate", (xr - 0.55, 0.0, 0.42), (0.1, 0.5, 0.2), r=0.02, seg=sg), "paint", GRAPHITE, 0.5)
    rear_linkage(M, xr - 0.28, 0.6, 0.24, body, lod, scale=0.55)
    # --- the front loader: a tower each side of the bonnet with its bracing, the arms down to the bucket on the
    # ground, the rams and their hoses, the crossbar, the parking stand
    tx, tz = 0.42, 1.12          # (the arm's pivot on the tower)
    bx, bz = 2.05, 0.32          # (the bucket's pivot)
    for s in (-1, 1):
        y = s * 0.47
        M.add(rbox("tower", (tx, y, 0.78), (0.2, 0.07, 0.76), r=0.02, seg=sg), "enamel", body, 0.4)
        M.add(rbox("towerbrace", (tx + 0.02, s * 0.4, 0.55), (0.3, 0.12, 0.08), r=0.015, seg=sg), "enamel", body, 0.4)
        arm = [(tx, y, tz), (1.25, y, 1.05), (1.78, y, 0.68), (bx, y, bz)]
        M.add(sweep("arm", curve(arm[0], (1.0, y, 1.18), (1.65, y, 0.9), arm[3], n=(14, 5, 3)[lod]), 0.06, 4, square=True), "enamel", body, 0.4)
        if lod < 2:
            M.add(lathe("ram", [(0.0, 0.0), (0.035, 0.0), (0.035, 0.55), (0.0, 0.56)], (12, 6)[lod]), "paint", GRAPHITE, 0.4).matrix_world = (
                Matrix.Translation((tx - 0.02, y + s * 0.07, 0.55)) @ Matrix.Rotation(math.atan2(1.15 - 0.02 - tx, 0.95 - 0.55), 4, "Y"))
            M.add(sweep("rod", [(tx + 0.36, y + s * 0.07, 0.75), (1.2, y + s * 0.07, 0.98)], 0.018, (8, 4)[lod]), "steel", STEEL, 0.3)
            M.add(sweep("tiltlink", [(1.6, y, 0.9), (1.85, y, 0.74), (2.05, y, 0.62)], 0.025, 4, square=True), "enamel", body, 0.4)
            M.add(lathe("tiltram", [(0.0, 0.0), (0.028, 0.0), (0.028, 0.35), (0.0, 0.36)], (10, 6)[lod]), "paint", GRAPHITE, 0.4).matrix_world = (
                Matrix.Translation((1.2, y - s * 0.07, 1.06)) @ Matrix.Rotation(math.radians(122), 4, "Y"))
        if lod == 0:
            for p in arm[:1] + [(1.6, y, 0.9)]:
                M.add(rod("pin", (p[0], y - 0.06, p[2]), (p[0], y + 0.06, p[2]), 0.022, 10), "steel", STEEL, 0.3)
            hose(M, [(tx, y - s * 0.06, 0.4), (tx + 0.15, y - s * 0.1, 0.9), (0.9, y - s * 0.08, 1.2), (1.4, y - s * 0.07, 1.0)], 0.012, lod)
            hose(M, [(tx + 0.03, y - s * 0.08, 0.4), (tx + 0.2, y - s * 0.12, 0.85), (0.95, y - s * 0.1, 1.15), (1.45, y - s * 0.09, 0.96)], 0.012, lod)
            M.add(rod("stand", (1.3, y, 0.95), (1.45, y, 0.02), 0.02, 8), "paint", GRAPHITE, 0.4)
    M.add(rbox("crossbar", (1.5, 0.0, 0.88), (0.1, 0.94, 0.08), r=0.02, seg=sg), "enamel", body, 0.4)
    if lod == 0:
        M.add(rbox("valve", (0.3, 0.32, 1.0), (0.14, 0.1, 0.12), r=0.015, seg=2), "paint", GRAPHITE, 0.4)
        M.add(rod("loaderlever", (0.3, 0.34, 1.06), (0.2, 0.36, 1.3), 0.012, 6), "paint", BLACK, 0.2)
    # (the bucket: a curved back, a floor, two side cheeks, the cutting edge with its bolts and the top lip)
    bw = 1.45
    if lod == 0:
        # (the shell: a plate bent through the floor, the heel and up the back, one closed slab a bend)
        pts2 = [(bx + 0.62, 0.025), (bx + 0.3, 0.025), (bx + 0.1, 0.05), (bx - 0.005, 0.16), (bx - 0.01, 0.3), (bx + 0.02, 0.45), (bx + 0.1, 0.56)]
        for (xa, za), (xb, zb) in zip(pts2, pts2[1:]):
            L = math.hypot(xb - xa, zb - za)
            ang = math.atan2(zb - za, xb - xa)
            M.add(box("bucketshell", ((xa + xb) / 2, 0.0, (za + zb) / 2), (L + 0.02, bw - 0.04, 0.03), rot=Matrix.Rotation(-ang, 4, "Y")), "enamel", body, 0.5)
    else:
        M.add(box("bucketback", (bx + 0.02, 0.0, 0.3), (0.04, bw, 0.5), rot=Matrix.Rotation(math.radians(12), 4, "Y")), "enamel", body, 0.5)
        M.add(box("bucketfloor", (bx + 0.3, 0.0, 0.04), (0.6, bw, 0.035)), "enamel", body, 0.5)
    M.add(box("edge", (bx + 0.62, 0.0, 0.025), (0.08, bw, 0.03)), "steel", STEEL, 0.4)
    for s in (-1, 1):
        cheek = K._object("cheek", [(bx - 0.03, -0.015, 0.02), (bx + 0.66, -0.015, 0.0), (bx + 0.12, -0.015, 0.58),
                                    (bx - 0.03, 0.015, 0.02), (bx + 0.66, 0.015, 0.0), (bx + 0.12, 0.015, 0.58)],
                          [(0, 2, 1), (3, 4, 5), (0, 1, 4, 3), (1, 2, 5, 4), (2, 0, 3, 5)], 30.0)
        cheek.matrix_world = Matrix.Translation((0.0, s * (bw / 2 - 0.015), 0.0))
        M.add(cheek, "enamel", body, 0.5)
    if lod < 2:
        M.add(rbox("bucketlip", (bx + 0.12, 0.0, 0.57), (0.08, bw, 0.04), r=0.015, seg=sg), "enamel", body, 0.5)
        for s in (-1, 1):
            M.add(rbox("bucketear", (bx + 0.04, s * 0.47, 0.42), (0.12, 0.05, 0.3), r=0.015, seg=sg), "enamel", body, 0.5)
    if lod == 0:
        for k in range(9):
            M.add(lathe("edgebolt", [(0.0, 0.0), (0.014, 0.0), (0.014, 0.01), (0.0, 0.012)], 6, at=(bx + 0.6, -bw / 2 + 0.1 + k * (bw - 0.2) / 8, 0.04)), "steel", STEEL, 0.3)
        for k in range(3):
            M.add(box("wearstrip", (bx + 0.35, -0.5 + k * 0.5, 0.045), (0.5, 0.06, 0.012)), "steel", STEEL, 0.4)
    return M


KINDS3 = {"farm_tractor": tractor, "farm_tractor_utility": utility, "farm_tractor_compact": compact}


def build(lod, kind="farm_tractor"):
    s = ("", "_LOD1", "_LOD2")[lod]
    return [KINDS3[kind]("SM_Prop_%s_%d%s" % (kind, i, s), i, lod) for i in range(len(LIVERIES))]


def main():
    import blender_farm_props as FP
    args = sys.argv[1:]
    only = [a for a in args if a in KINDS3]
    out = {}
    for kind in (only or KINDS3):
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
    FP.KINDS = {k: v for k, v in FP.KINDS.items() if not only or k in only}
    FP.write_kinds(out)


if __name__ == "__main__":
    main()
