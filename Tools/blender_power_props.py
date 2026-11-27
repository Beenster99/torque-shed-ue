"""Ride UK's power and skyline set (the everyday kits plan, stage 9): the pylons, masts, wind turbines, water towers
and substations the map draws, modelled in Blender in place of World/WorldRideSkyline.cpp's code-made shapes near
the road (farther off the code's plain shapes stay). Our own designs, from general knowledge of British power lines
and masts: no operator's names, logos or lettering (the danger and number plates are blank shapes).

Writes Data/RideUK/Props/SM_Prop_<kind>_<n>.glb, each with two distance versions made here by hand,
SM_Prop_<kind>_<n>_LOD1.glb and _LOD2.glb (Tools/ue_build_rideuk.py puts them in as the mesh's LODs 1 and 2;
never reduced by the engine), and sets each kind's height and reach in Data/RideUK/prop_kit.json. Same material
and frame as the street kit (Tools/blender_street_kit.py: M_RideStreetKit, front +x, foot at 0); all the models of
a kind are the same height, and the game scales each one by the map's height over the model's (RideProp::Scale).

  power_pylon 0         a lattice tower 26 m: three cross-arms a side across its line (+x along the line),
                        insulator strings 2 m long whose ends are where the code's wires meet them (at 0.8,
                        0.677 and 0.554 of its height, 5.5, 6.82 and 5.5 m out: WorldRideSkyline.cpp)
  power_mast 0..1       a three-legged lattice mast 30 m (to its lightning rod): with dishes; with panels
                        and a cabin at its foot
  power_monopole 0..1   a phone mast 18 m: a tapered pole with a head frame of panels; a slim shrouded pole
  power_turbine 0       a wind turbine's tower and nacelle, its hub 45 m up and 3.6 m out in front (+x)
  power_rotor 0         its rotor: three blades 20 m long and the hub, turning about y (laid by the game at
                        the hub, its Pitch the blades' angle)
  power_watertower 0..1 a concrete water tower 18 m: a drum on eight legs; a cone on a central shaft
  power_kiosk 0..1      a substation kiosk 1.7 m: green moulded doors; green steel louvred doors
  power_transformer 0   a substation transformer: its tank, radiator banks, conservator and bushings
  power_gantry 0        a substation gantry 7 m: two lattice posts, a beam, isolators under it
  power_palisade 0      a 2.75 m panel of steel palisade fence 2.4 m high with its post at +x

    python Tools/blender_power_props.py                # the models
    python Tools/blender_power_props.py --render DIR   # and the review sheet's pictures in DIR
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
import blender_farm_props as FP   # noqa: E402  (ring_lathe)

GALV = (0.3, 0.315, 0.3)
GALV_DARK = (0.22, 0.23, 0.22)
INSUL = (0.16, 0.17, 0.18)          # (glass and composite strings, a dark grey-green)
PORCELAIN = (0.36, 0.2, 0.12)
CONCRETE = (0.36, 0.35, 0.32)
CONCRETE_DARK = (0.27, 0.26, 0.24)
KIOSK_GREEN = (0.03, 0.09, 0.05)
TANK_GREY = (0.2, 0.22, 0.21)
WHITE = (0.66, 0.67, 0.66)
TURBINE = (0.6, 0.61, 0.6)
PANEL = (0.5, 0.5, 0.49)
DISH = (0.58, 0.58, 0.56)
CABIN = (0.24, 0.26, 0.24)
BLACK = (0.02, 0.02, 0.02)
PLATE_RED = (0.42, 0.03, 0.02)
PLATE_YELLOW = (0.62, 0.45, 0.02)

# The pylon's size and where its wires meet it (WorldRideSkyline.cpp: the wires 0.8 of its height up, 11 m spread,
# a level down every 3.2 m; all scaled by height over 26).
PYLON_H = 26.0
WIRE_AT = [(22.8, 5.5, 20.8), (19.6, 6.82, 17.6), (16.4, 5.5, 14.4)]   # (arm height, reach, string's end)
TURBINE_HUB = 45.0
TURBINE_NOSE = 3.6

# kind: (reach in m, casts a shadow, hand-made LODs' screen sizes)
KINDS = {"power_pylon": (7500, True, [0.2, 0.05]), "power_mast": (7500, True, [0.2, 0.05]),
         "power_monopole": (5000, True, [0.2, 0.05]), "power_turbine": (7500, True, [0.2, 0.05]),
         "power_rotor": (7500, True, [0.2, 0.05]), "power_watertower": (7500, True, [0.2, 0.05]),
         "power_kiosk": (300, True, [0.12, 0.03]), "power_transformer": (800, True, [0.12, 0.03]),
         "power_gantry": (1500, True, [0.12, 0.03]), "power_palisade": (400, False, [0.12, 0.03])}


def bar(M, a, b, r, rgb=GALV, lod=0, surface="galv", dirt=0.25):
    """A steel member: an angle's square bar near, fewer sides off."""
    return M.add(sweep("m", [a, b], r, 4, square=True), surface, rgb, dirt)


def tube(M, a, b, r, rgb, lod, surface="galv", dirt=0.25):
    return M.add(rod("t", a, b, r, (10, 6, 4)[lod]), surface, rgb, dirt)


def string(M, top, length, lod, horizontal=None):
    """An insulator string hanging from top: its sheds (discs), a yoke at the foot."""
    x, y, z = top
    if lod == 2:
        M.add(rod("ins", (x, y, z), (x, y, z - length), 0.09, 4), "glaze", INSUL, 0.1)
        return
    n = 12 if lod == 0 else 4
    prof = [(0.03, 0.0)]
    for k in range(n):
        z0 = 0.15 + (length - 0.35) * k / n
        prof += [(0.03, z0), (0.13, z0 + 0.04), (0.04, z0 + 0.08)]
    prof += [(0.03, length - 0.12), (0.0, length - 0.1)]
    ob = lathe("string", [(r, length - zz) for r, zz in prof][::-1], 10 if lod == 0 else 6)
    ob.matrix_world = Matrix.Translation((x, y, z - length))
    M.add(ob, "glaze", INSUL, 0.1)
    M.add(box("yoke", (x, y, z - length + 0.02), (0.12, 0.3, 0.06)), "galv", GALV_DARK, 0.3)


# --- the pylon -------------------------------------------------------------------------------------------

def pylon_half(z):
    """The body's half width at z: splayed legs to a waist at 15 m, near straight above."""
    if z <= 15.0:
        return 3.3 + (0.9 - 3.3) * (z / 15.0) ** 0.85
    return 0.9 + (0.62 - 0.9) * (z - 15.0) / 9.0


def pylon(name, lod):
    """A lattice tower 26 m, its line along x, cross-arms along y."""
    M = K.Model(name)
    levels = [0.0, 3.0, 5.8, 8.4, 10.8, 13.0, 15.0, 16.4, 18.0, 19.6, 21.2, 22.8, 24.0]
    if lod == 2:
        levels = [0.0, 8.4, 15.0, 24.0]
    elif lod == 1:
        levels = [0.0, 5.8, 10.8, 15.0, 19.6, 24.0]
    corners = [(-1, -1), (1, -1), (1, 1), (-1, 1)]

    def C(k, z, dz=0.0):
        sx, sy = corners[k % 4]
        h = pylon_half(z)
        return Vector((sx * h, sy * h, z + dz))
    leg_r = (0.11, 0.12, 0.16)[lod]
    for i in range(len(levels) - 1):
        z0, z1 = levels[i], levels[i + 1]
        for k in range(4):
            bar(M, C(k, z0, -0.4 if i == 0 else 0.0), C(k, z1), leg_r, lod=lod)
            if lod == 2:
                continue
            # (each face braced: a cross in each panel, a horizontal at each level)
            bar(M, C(k, z0), C(k + 1, z1), 0.045 if lod == 0 else 0.07, lod=lod)
            bar(M, C(k + 1, z0), C(k, z1), 0.045 if lod == 0 else 0.07, lod=lod)
            if i > 0:
                bar(M, C(k, z0), C(k + 1, z0), 0.05 if lod == 0 else 0.07, lod=lod)
            if lod == 0 and z1 - z0 > 2.0:
                # (the redundants: a mid-height strut across the panel)
                zm = (z0 + z1) / 2
                bar(M, C(k, zm), C(k + 1, zm), 0.035, lod=lod)
    # (the peak: the earth wire's)
    top = Vector((0.0, 0.0, PYLON_H))
    for k in range(4):
        bar(M, C(k, 24.0), top + Vector((0.0, 0.0, -0.15)), leg_r * 0.8, lod=lod)
    M.add(box("peakcap", (0.0, 0.0, PYLON_H - 0.1), (0.3, 0.3, 0.2)), "galv", GALV_DARK, 0.3)
    # (the cross-arms: a horizontal truss each side, its two lower chords from the body's two corners to the tip,
    # an upper chord from higher on the body)
    for za, reach, zend in WIRE_AT:
        for s in (-1, 1):
            tip = Vector((0.0, s * reach, za))
            hw = pylon_half(za)
            a0, a1 = Vector((-hw, s * hw, za)), Vector((hw, s * hw, za))
            up = Vector((0.0, s * hw, za + 1.3))
            if lod == 2:
                M.add(box("arm", (0.0, s * (hw + reach) / 2, za + 0.3), (0.6, reach - hw, 0.6)), "galv", GALV, 0.25)
            else:
                bar(M, a0, tip, 0.08, lod=lod)
                bar(M, a1, tip, 0.08, lod=lod)
                bar(M, up + Vector((-hw * 0.9, 0, 0)), tip + Vector((0, 0, 0.12)), 0.06, lod=lod)
                bar(M, up + Vector((hw * 0.9, 0, 0)), tip + Vector((0, 0, 0.12)), 0.06, lod=lod)
                if lod == 0:
                    for t in (0.33, 0.66):
                        p = a0.lerp(tip, t)
                        q = a1.lerp(tip, t)
                        u = (up + Vector((-hw * 0.9, 0, 0))).lerp(tip, t)
                        bar(M, p, q, 0.035, lod=lod)
                        bar(M, p, u, 0.035, lod=lod)
                        bar(M, q, u, 0.035, lod=lod)
            string(M, (0.0, s * reach, za), za - zend, lod)
    if lod == 0:
        # (the anti-climbing guard 4 m up: a band of barbed spikes round the legs, and a plate on one leg)
        z = 4.2
        for k in range(4):
            a, b = C(k, z), C(k + 1, z)
            bar(M, a, b, 0.05, lod=lod)
            for t in [i / 8 for i in range(1, 8)]:
                p = a.lerp(b, t)
                out = Vector((p.x, p.y, 0.0)).normalized() * 0.35
                bar(M, p, p + out + Vector((0, 0, 0.25)), 0.012, rgb=GALV_DARK, lod=lod)
        p = C(0, 3.2)
        M.add(box("danger", (p.x - 0.25, p.y - 0.25, 3.2), (0.02, 0.32, 0.4), rot_z=math.radians(45)), "enamel", PLATE_RED, 0.25)
        M.add(box("number", (p.x - 0.25, p.y - 0.25, 2.6), (0.02, 0.32, 0.2), rot_z=math.radians(45)), "enamel", PLATE_YELLOW, 0.25)
        # (the concrete muffs the legs stand in)
        for k in range(4):
            q = C(k, 0.0)
            M.add(box("muff", (q.x, q.y, 0.1), (0.7, 0.7, 0.5), bevel=0.03), "concrete", CONCRETE, 0.6)
    return M


# --- masts -----------------------------------------------------------------------------------------------

def mast(name, panels, lod):
    """A three-legged lattice mast, 27 m of lattice and a 3 m lightning rod (30 m), dishes or panels up top."""
    M = K.Model(name)
    H = 27.0
    R0, R1 = 1.5, 0.55
    rings = (9, 5, 3)[lod]
    pts = []
    for r in range(rings + 1):
        z = H * r / rings
        rr = R0 + (R1 - R0) * r / rings
        pts.append([Vector((rr * math.cos(2.0944 * k), rr * math.sin(2.0944 * k), z - (0.4 if r == 0 else 0.0))) for k in range(3)])
    for r in range(rings):
        for k in range(3):
            tube(M, pts[r][k], pts[r + 1][k], (0.08, 0.09, 0.12)[lod], GALV, lod)
            if lod < 2:
                bar(M, pts[r][k], pts[r + 1][(k + 1) % 3], 0.03 if lod == 0 else 0.05, lod=lod)
                if lod == 0:
                    bar(M, pts[r][(k + 1) % 3], pts[r + 1][k], 0.03, lod=lod)
                bar(M, pts[r + 1][k], pts[r + 1][(k + 1) % 3], 0.035 if lod == 0 else 0.05, lod=lod)
    tube(M, (0, 0, H - 0.5), (0, 0, H + 3.0), 0.05, GALV, max(lod, 1))
    if lod == 0:
        # (the ladder up one face and its feeder cables)
        for z in [0.5 + 0.4 * i for i in range(int((H - 1.0) / 0.4))]:
            t = z / H
            rr = R0 + (R1 - R0) * t
            c = Vector((rr * 0.5, 0.0, z))
            M.add(box("rung", (c.x, c.y, z), (0.03, 0.4, 0.03)), "galv", GALV_DARK, 0.3)
        M.add(box("cables", ((R0 + R1) * 0.25 + 0.05, 0.25, H / 2), (0.08, 0.08, H)), "paint", BLACK, 0.2)
    if panels:
        # (three sectors of two panel antennas on a ring of stand-offs, a second set lower)
        for zc in (H - 1.8, H - 5.5):
            for k in range(3):
                a = 2.0944 * k + 1.047
                for d in (-0.35, 0.35):
                    cx, cy = math.cos(a) * 1.15 - math.sin(a) * d, math.sin(a) * 1.15 + math.cos(a) * d
                    M.add(box("panel", (cx, cy, zc), (0.12, 0.3, 2.0 if zc > H - 3 else 1.4), rot_z=a, bevel=0.02 if lod == 0 else 0.0), "enamel", PANEL, 0.15)
                if lod < 2:
                    bar(M, (math.cos(a) * 0.55, math.sin(a) * 0.55, zc + 0.6), (math.cos(a) * 1.05, math.sin(a) * 1.05, zc + 0.6), 0.04, lod=lod)
        # (the equipment cabin at its foot)
        M.add(box("cabin", (3.4, 0.0, 1.3), (2.4, 3.2, 2.6), bevel=0.04 if lod == 0 else 0.0), "enamel", CABIN, 0.35)
        if lod < 2:
            M.add(box("cabinroof", (3.4, 0.0, 2.65), (2.6, 3.4, 0.1)), "enamel", (0.12, 0.13, 0.12), 0.3)
            M.add(box("tray", (1.8, 0.0, 2.4), (1.2, 0.3, 0.08)), "galv", GALV_DARK, 0.3)
    else:
        # (microwave dishes, each on its stand-off, at three heights and ways)
        for zc, a, r in ((H - 2.2, 0.4, 0.6), (H - 4.6, 2.6, 0.9), (H - 8.0, 4.4, 0.6), (H - 11.5, 1.6, 0.45)):
            rr = R0 + (R1 - R0) * zc / H
            d = Vector((math.cos(a), math.sin(a), 0.0))
            c = d * (rr + 0.6)
            ob = lathe("dish", [(0.0, -0.05), (r * 0.6, 0.0), (r, 0.12), (r, 0.3), (r * 0.94, 0.32), (0.0, 0.32)], (20, 10, 6)[lod])
            ob.matrix_world = Matrix.Translation((c.x, c.y, zc)) @ d.to_track_quat("Z", "Y").to_matrix().to_4x4()
            M.add(ob, "paint", DISH, 0.15)
            if lod < 2:
                bar(M, d * rr * 0.8 + Vector((0, 0, zc)), c + Vector((0, 0, zc)), 0.04, lod=lod)
        # (a few panels up top too)
        for k in range(3):
            a = 2.0944 * k
            M.add(box("panel", (math.cos(a) * 0.8, math.sin(a) * 0.8, H - 0.9), (0.1, 0.26, 1.4), rot_z=a), "enamel", PANEL, 0.15)
    for k in range(3):
        q = pts[0][k]
        M.add(box("base", (q.x, q.y, 0.15), (0.8, 0.8, 0.5)), "concrete", CONCRETE, 0.6)
    return M


def monopole(name, shrouded, lod):
    """A phone mast 18 m: a tapered steel pole; a head frame of panels, or a slim pole with a shroud."""
    M = K.Model(name)
    H = 18.0
    seg = (16, 10, 6)[lod]
    if shrouded:
        M.add(lathe("pole", [(0.3, -0.3), (0.28, 0.0), (0.2, H - 4.2)], seg), "galv", GALV, 0.3)
        M.add(lathe("shroud", [(0.0, H - 4.4), (0.2, H - 4.3), (0.42, H - 4.0), (0.42, H - 0.3), (0.3, H - 0.05), (0.0, H)], seg), "enamel", (0.42, 0.44, 0.43), 0.2)
        if lod < 2:
            M.add(lathe("band", [(0.43, H - 2.2), (0.43, H - 2.1)], seg), "paint", (0.3, 0.32, 0.31), 0.2)
        M.add(box("cabinet", (0.9, 0.0, 0.8), (0.6, 1.4, 1.6), bevel=0.03 if lod == 0 else 0.0), "enamel", (0.32, 0.34, 0.33), 0.35)
        M.add(box("cabinet2", (0.9, 1.6, 0.7), (0.6, 1.0, 1.4), bevel=0.03 if lod == 0 else 0.0), "enamel", (0.32, 0.34, 0.33), 0.35)
        return M
    M.add(lathe("pole", [(0.46, -0.3), (0.44, 0.0), (0.26, H - 0.8), (0.0, H - 0.75)], seg), "galv", GALV, 0.3)
    # (the head frame: a triangle of booms, two panels to each side, short and long)
    zh = H - 1.8
    for k in range(3):
        a = 2.0944 * k + 0.5236
        e = Vector((math.cos(a), math.sin(a), 0.0))
        t = Vector((-e.y, e.x, 0.0))
        if lod < 2:
            bar(M, e * 0.25 + Vector((0, 0, zh + 0.8)), e * 1.1 + Vector((0, 0, zh + 0.8)), 0.05, lod=lod)
            bar(M, e * 0.25 + Vector((0, 0, zh - 0.8)), e * 1.1 + Vector((0, 0, zh - 0.8)), 0.05, lod=lod)
        for d, l in ((-0.38, 2.4), (0.38, 1.6)):
            c = e * 1.2 + t * d
            M.add(box("panel", (c.x, c.y, zh), (0.12, 0.3, l), rot_z=a, bevel=0.02 if lod == 0 else 0.0), "enamel", PANEL, 0.15)
        if lod == 0:
            c = e * 1.2 + Vector((0, 0, zh - 1.6))
            M.add(box("rru", (c.x, c.y, c.z), (0.18, 0.3, 0.45), rot_z=a), "enamel", (0.32, 0.33, 0.33), 0.2)
    if lod < 2:
        tube(M, (0, 0, zh + 0.8), (0, 0, H), 0.05, GALV, 1)
    else:
        M.add(rod("tip", (0, 0, zh + 0.8), (0, 0, H), 0.06, 4), "galv", GALV, 0.3)
    # (the cabinets at its foot, the steel fence of the compound left to the map)
    M.add(box("cabinet", (1.2, 0.0, 0.8), (0.7, 1.6, 1.6), bevel=0.03 if lod == 0 else 0.0), "enamel", (0.32, 0.34, 0.33), 0.35)
    M.add(box("plinth", (1.2, 0.0, 0.03), (1.4, 2.0, 0.12)), "concrete", CONCRETE, 0.6)
    return M


# --- the wind turbine ------------------------------------------------------------------------------------

def turbine(name, lod):
    """The tower (tapered, white), the nacelle on top facing +x, the hub's foot 3.6 m out at 45 m."""
    M = K.Model(name)
    seg = (28, 14, 8)[lod]
    M.add(lathe("tower", [(1.65, -0.5), (1.6, 0.0), (1.2, 22.0), (0.95, TURBINE_HUB - 1.4), (0.0, TURBINE_HUB - 1.35)], seg), "enamel", TURBINE, 0.2)
    if lod < 2:
        M.add(lathe("door", [(1.62, 0.3), (1.62, 2.4)], seg, phase=0.0), "enamel", (0.5, 0.51, 0.5), 0.3)
        M.add(box("doorstep", (1.9, 0.0, 0.2), (0.8, 1.4, 0.4)), "concrete", CONCRETE, 0.6)
    # (the nacelle: a long box with rounded ends, its back a little higher)
    ob = box("nacelle", (-0.4, 0.0, TURBINE_HUB + 0.1), (6.6, 2.6, 2.8), bevel=0.4 if lod == 0 else 0.0)
    M.add(ob, "enamel", TURBINE, 0.15)
    if lod < 2:
        M.add(box("cooler", (-3.3, 0.0, TURBINE_HUB + 1.7), (0.6, 2.0, 0.6)), "enamel", (0.5, 0.51, 0.5), 0.2)
        M.add(rod("mast", (-2.8, 0.0, TURBINE_HUB + 1.5), (-2.8, 0.0, TURBINE_HUB + 2.3), 0.03, 4), "galv", GALV, 0.2)
    return M


def rotor(name, lod):
    """The rotor about its hub at the origin, turning about y: three blades 20 m in the x-z plane, one up."""
    M = K.Model(name)
    seg = (20, 10, 6)[lod]
    ob = lathe("spinner", [(0.0, -1.4), (0.8, -1.1), (1.25, -0.4), (1.3, 0.2), (1.2, 0.8), (0.0, 1.0)], seg)
    ob.matrix_world = Matrix.Rotation(math.radians(-90), 4, "X")
    M.add(ob, "enamel", TURBINE, 0.1)
    blade = 20.0
    # (each blade a flat aerofoil: widest 1.6 m a fifth of the way out, a sliver at the tip, thicker at the root)
    span = [0.0, 0.08, 0.2, 0.45, 0.75, 1.0] if lod == 0 else [0.0, 0.2, 1.0]
    chord = {0.0: 0.7, 0.08: 1.3, 0.2: 1.6, 0.45: 1.2, 0.75: 0.8, 1.0: 0.25}
    thick = {0.0: 0.55, 0.08: 0.45, 0.2: 0.3, 0.45: 0.18, 0.75: 0.1, 1.0: 0.05}
    for k in range(3):
        a = math.radians(90 + 120 * k)
        R = Matrix.Rotation(-a + math.pi / 2, 4, "Y")
        verts, faces = [], []
        for i, t in enumerate(span):
            r = 1.0 + (blade - 1.0) * t
            c, h = chord[t], thick[t]
            # (the leading edge ahead, the trailing edge back; a slight twist)
            for x, y in ((-c * 0.3, 0.0), (0.0, h * 0.5), (c * 0.7, 0.0), (0.0, -h * 0.5)):
                verts.append(R @ Vector((x, y, r)))
        n = 4
        for i in range(len(span) - 1):
            for j in range(n):
                a0, a1 = i * n + j, i * n + (j + 1) % n
                faces.append((a0, a1, a1 + n, a0 + n))
        faces.append(tuple(reversed(range(n))))
        last = (len(span) - 1) * n
        faces.append(tuple(range(last, last + n)))
        ob = K._object("blade", verts, faces, 40.0)
        M.add(ob, "enamel", TURBINE, 0.12)
    return M


# --- water towers ----------------------------------------------------------------------------------------

def watertower(name, cone, lod):
    """A concrete water tower 18 m, its tank 4 m in radius: a drum on eight legs and ring beams; or a cone on a
    central shaft."""
    M = K.Model(name)
    H, R = 18.0, 4.0
    seg = (32, 16, 8)[lod]
    if cone:
        M.add(lathe("shaft", [(1.7, -0.3), (1.6, 0.0), (1.5, 10.5)], seg), "concrete", CONCRETE, 0.5)
        M.add(lathe("bowl", [(1.5, 10.4), (2.4, 11.6), (R, 14.0), (R, 17.4), (R - 0.15, 17.6), (2.0, 17.9), (0.0, H)], seg), "concrete", CONCRETE, 0.4)
        if lod < 2:
            M.add(lathe("lip", [(R + 0.08, 14.0), (R + 0.08, 14.35)], seg), "concrete", CONCRETE_DARK, 0.4)
            M.add(box("door", (1.58, 0.0, 1.1), (0.1, 1.0, 2.2)), "paint", (0.06, 0.09, 0.07), 0.3)
        if lod == 0:
            for k in range(12):
                a = 2 * math.pi * k / 12
                M.add(box("rib", (math.cos(a) * R, math.sin(a) * R, 15.7), (0.12, 0.25, 3.4), rot_z=a), "concrete", CONCRETE, 0.4)
        return M
    legs = 8
    tank0 = 12.0
    for k in range(legs):
        a = 2 * math.pi * k / legs
        p0 = Vector((math.cos(a) * (R - 0.2), math.sin(a) * (R - 0.2), -0.3))
        p1 = Vector((math.cos(a) * (R - 0.4), math.sin(a) * (R - 0.4), tank0))
        M.add(box("leg", ((p0 + p1) / 2), (0.5, 0.5, tank0 + 0.3), rot_z=a), "concrete", CONCRETE, 0.5)
    if lod < 2:
        for zb in (4.4, 8.4):
            M.add(FP.ring_lathe("ring", [(R - 0.15, zb), (R - 0.15, zb + 0.5), (R - 0.65, zb + 0.5), (R - 0.65, zb)][::-1], seg), "concrete", CONCRETE_DARK, 0.5)
        M.add(lathe("stair", [(0.9, -0.3), (0.9, tank0)], (12, 8)[lod]), "concrete", CONCRETE, 0.5)
    M.add(lathe("tank", [(R - 0.5, tank0 - 0.4), (R + 0.1, tank0), (R + 0.1, H - 0.6), (R - 0.3, H - 0.3), (0.0, H)], seg), "concrete", CONCRETE, 0.4)
    if lod < 2:
        M.add(lathe("band", [(R + 0.16, H - 1.6), (R + 0.16, H - 1.2)], seg), "concrete", CONCRETE_DARK, 0.4)
    if lod == 0:
        for k in range(16):
            a = 2 * math.pi * (k + 0.5) / 16
            M.add(box("pilaster", (math.cos(a) * (R + 0.12), math.sin(a) * (R + 0.12), (tank0 + H - 1.6) / 2), (0.1, 0.3, H - 1.6 - tank0), rot_z=a), "concrete", CONCRETE, 0.4)
    return M


# --- substations -----------------------------------------------------------------------------------------

def kiosk(name, steel, lod):
    """A substation kiosk 1.7 m: a green box 1.6 x 1.2 m on a plinth, double doors at its front (+x)."""
    M = K.Model(name)
    W, D = (1.8, 1.3) if steel else (1.6, 1.2)
    M.add(box("plinth", (0.0, 0.0, 0.08), (D + 0.3, W + 0.3, 0.16)), "concrete", CONCRETE, 0.6)
    M.add(box("body", (0.0, 0.0, 0.16 + 1.42 / 2), (D, W, 1.42), bevel=0.02 if lod == 0 else 0.0), "enamel", KIOSK_GREEN, 0.3)
    M.add(box("roof", (0.0, 0.0, 1.64), (D + 0.12, W + 0.12, 0.12), bevel=0.03 if lod == 0 else 0.0), "enamel", KIOSK_GREEN, 0.25)
    if lod < 2:
        M.add(box("seam", (D / 2 + 0.003, 0.0, 0.88), (0.01, 0.02, 1.3)), "paint", BLACK, 0.2)
        M.add(box("plate", (D / 2 + 0.006, W * 0.25, 1.3), (0.01, 0.22, 0.16)), "enamel", PLATE_YELLOW, 0.2)
    if lod == 0:
        for s in (-1, 1):
            M.add(box("handle", (D / 2 + 0.02, s * 0.1, 0.9), (0.03, 0.03, 0.15)), "steel", GALV, 0.2)
        if steel:
            for side in (-1, 1):
                for k in range(6):
                    M.add(box("louvre", (D / 2 + 0.01, side * W * 0.25, 0.4 + k * 0.16), (0.02, W * 0.4, 0.04)), "paint", (0.02, 0.06, 0.03), 0.25)
        else:
            for k in range(4):
                M.add(box("vent", (0.0, -W / 2 - 0.005, 1.2 + k * 0.06), (D * 0.5, 0.01, 0.025)), "paint", BLACK, 0.2)
    return M


def transformer(name, lod):
    """A substation transformer: a tank 3 x 2 m and 2.6 m high on a plinth, radiator banks each side (y), a
    conservator along its top, three tall bushings and three short."""
    M = K.Model(name)
    M.add(box("plinth", (0.0, 0.0, 0.15), (3.6, 4.6, 0.3)), "concrete", CONCRETE, 0.6)
    M.add(box("tank", (0.0, 0.0, 0.3 + 1.3), (3.0, 2.0, 2.6), bevel=0.04 if lod == 0 else 0.0), "enamel", TANK_GREY, 0.35)
    M.add(box("lid", (0.0, 0.0, 2.95), (3.1, 2.1, 0.1)), "enamel", TANK_GREY, 0.3)
    fins = (14, 5, 1)[lod]
    for s in (-1, 1):
        if lod == 2:
            M.add(box("bank", (0.0, s * 1.6, 1.5), (2.6, 1.0, 2.0)), "enamel", TANK_GREY, 0.35)
            continue
        for k in range(fins):
            x = -1.3 + 2.6 * (k + 0.5) / fins
            M.add(box("fin", (x, s * 1.6, 1.5), (0.05 if lod == 0 else 0.3, 1.0, 2.0)), "enamel", TANK_GREY, 0.35)
        M.add(box("header", (0.0, s * 1.6, 2.55), (2.7, 0.12, 0.12)), "enamel", TANK_GREY, 0.3)
    # (the conservator: a drum on two legs above one end)
    ob = lathe("cons", [(0.0, -1.0), (0.38, -0.98), (0.4, -0.9), (0.4, 0.9), (0.38, 0.98), (0.0, 1.0)], (16, 8, 6)[lod])
    ob.matrix_world = Matrix.Translation((-1.1, 0.0, 3.6)) @ Matrix.Rotation(math.radians(90), 4, "X")
    M.add(ob, "enamel", TANK_GREY, 0.3)
    if lod < 2:
        for y in (-0.6, 0.6):
            M.add(box("cleg", (-1.1, y, 3.15), (0.08, 0.08, 0.4)), "enamel", TANK_GREY, 0.3)
    for k, y in enumerate((-0.6, 0.0, 0.6)):
        prof = [(0.12, 0.0)] + [(r, 0.15 + i * 0.07) for i in range(14) for r in ((0.15, 0.1)[i % 2],)] + [(0.06, 1.2), (0.0, 1.25)]
        M.add(lathe("hv", prof if lod == 0 else [(0.13, 0.0), (0.09, 1.2), (0.0, 1.25)], (10, 6, 4)[lod], at=(0.6, y, 3.0)), "glaze", PORCELAIN, 0.15)
        if lod < 2:
            M.add(lathe("lv", [(0.09, 0.0), (0.07, 0.45), (0.0, 0.5)], (8, 5)[lod], at=(1.25, y * 0.8, 3.0)), "glaze", PORCELAIN, 0.15)
    if lod == 0:
        M.add(box("plate", (1.505, 0.6, 1.9), (0.01, 0.3, 0.2)), "enamel", PLATE_YELLOW, 0.2)
        M.add(box("box", (1.55, -0.5, 1.2), (0.12, 0.5, 0.6)), "enamel", TANK_GREY, 0.3)
    return M


def gantry(name, lod):
    """A gantry 7 m: two square lattice posts 6 m apart along y, a lattice beam across, three isolators under it."""
    M = K.Model(name)
    H, Y, w = 7.0, 3.0, 0.25
    levels = [0.0, 1.75, 3.5, 5.25, 6.6] if lod == 0 else [0.0, 6.6]
    for s in (-1, 1):
        cs = [Vector((dx * w, s * Y + dy * w, 0.0)) for dx, dy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        if lod == 2:
            M.add(box("post", (0.0, s * Y, 3.3), (0.5, 0.5, 6.9)), "galv", GALV, 0.3)
            continue
        for k in range(4):
            a, b = cs[k], cs[(k + 1) % 4]
            bar(M, a + Vector((0, 0, -0.3)), a + Vector((0, 0, 6.6)), 0.05, lod=lod)
            for i in range(len(levels) - 1):
                z0, z1 = levels[i], levels[i + 1]
                bar(M, a + Vector((0, 0, z0)), b + Vector((0, 0, z1)), 0.02 if lod == 0 else 0.03, lod=lod)
        M.add(box("foot", (0.0, s * Y, 0.1), (0.9, 0.9, 0.4)), "concrete", CONCRETE, 0.6)
    # (the beam: a box truss across the posts' heads)
    if lod == 2:
        M.add(box("beam", (0.0, 0.0, 6.75), (0.5, 2 * Y + 0.5, 0.5)), "galv", GALV, 0.3)
    else:
        for dx in (-w, w):
            for z in (6.5, 7.0):
                bar(M, (dx, -Y - w, z), (dx, Y + w, z), 0.04, lod=lod)
        if lod == 0:
            n = 10
            for i in range(n):
                y0, y1 = -Y + 2 * Y * i / n, -Y + 2 * Y * (i + 1) / n
                for dx in (-w, w):
                    bar(M, (dx, y0, 6.5), (dx, y1, 7.0), 0.02, lod=lod)
    # (three isolators: a post insulator each end, its blade between, hanging off the beam on a frame)
    for y in (-1.6, 0.0, 1.6):
        if lod == 2:
            M.add(rod("iso", (0.0, y, 6.4), (0.0, y, 5.2), 0.1, 4), "glaze", PORCELAIN, 0.15)
            continue
        prof = [(0.1, 0.0)] + [(r, 0.1 + i * 0.07) for i in range(12) for r in ((0.13, 0.08)[i % 2],)] + [(0.06, 1.0)]
        M.add(lathe("iso", prof if lod == 0 else [(0.1, 0.0), (0.08, 1.0)], (10, 6)[lod], at=(0.0, y, 5.4)), "glaze", PORCELAIN, 0.15)
        M.add(box("hanger", (0.0, y, 6.45), (0.12, 0.12, 0.1)), "galv", GALV_DARK, 0.3)
        tube(M, (0.0, y, 5.4), (0.0, y, 6.4), 0.03, GALV_DARK, lod)
    return M


def palisade(name, lod):
    """A 2.75 m panel of steel palisade 2.4 m high along x: W-section pales with triple-pointed heads on two rails,
    its post at +x (the game lays them end to end round a substation)."""
    M = K.Model(name)
    L, H = 2.75, 2.4
    M.add(box("post", (L / 2, 0.0, (H + 0.1) / 2 - 0.1), (0.1, 0.1, H + 0.1)), "galv", GALV, 0.35)
    for z in (0.35, 2.0):
        M.add(box("rail", (0.0, 0.03, z), (L, 0.05, 0.05)), "galv", GALV, 0.35)
    n = 13
    if lod == 2:
        M.add(box("screen", (0.0, -0.01, H / 2 - 0.1), (L, 0.02, H - 0.2)), "galv", GALV, 0.4)
        return M
    for k in range(n):
        x = -L / 2 + (k + 0.5) * L / n
        M.add(box("pale", (x, 0.0, (H - 0.2) / 2), (0.07, 0.02, H - 0.2)), "galv", GALV, 0.4)
        if lod == 0:
            # (its head: three points)
            for dx in (-0.025, 0.0, 0.025):
                verts = [(x + dx - 0.012, -0.01, H - 0.2), (x + dx + 0.012, -0.01, H - 0.2), (x + dx, -0.01, H - 0.02 - abs(dx) * 2.0),
                         (x + dx - 0.012, 0.01, H - 0.2), (x + dx + 0.012, 0.01, H - 0.2), (x + dx, 0.01, H - 0.02 - abs(dx) * 2.0)]
                faces = [(0, 2, 1), (3, 4, 5), (0, 1, 4, 3), (1, 2, 5, 4), (2, 0, 3, 5)]
                M.add(K._object("point", verts, faces, 30.0), "galv", GALV, 0.4)
        else:
            M.add(box("tip", (x, 0.0, H - 0.1), (0.04, 0.02, 0.2)), "galv", GALV, 0.4)
    return M


def build(lod):
    """Every model at lod (0 the model, 1 and 2 its distance versions)."""
    s = ("", "_LOD1", "_LOD2")[lod]
    return [
        pylon("SM_Prop_power_pylon_0" + s, lod),
        mast("SM_Prop_power_mast_0" + s, False, lod),
        mast("SM_Prop_power_mast_1" + s, True, lod),
        monopole("SM_Prop_power_monopole_0" + s, False, lod),
        monopole("SM_Prop_power_monopole_1" + s, True, lod),
        turbine("SM_Prop_power_turbine_0" + s, lod),
        rotor("SM_Prop_power_rotor_0" + s, lod),
        watertower("SM_Prop_power_watertower_0" + s, False, lod),
        watertower("SM_Prop_power_watertower_1" + s, True, lod),
        kiosk("SM_Prop_power_kiosk_0" + s, False, lod),
        kiosk("SM_Prop_power_kiosk_1" + s, True, lod),
        transformer("SM_Prop_power_transformer_0" + s, lod),
        gantry("SM_Prop_power_gantry_0" + s, lod),
        palisade("SM_Prop_power_palisade_0" + s, lod),
    ]


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
                           title="Ride UK's power and skyline set (our own models; each with two hand-made distance versions)",
                           sheet_name="power_props.png")
    FP.KINDS = KINDS
    FP.write_kinds(out)


if __name__ == "__main__":
    main()
