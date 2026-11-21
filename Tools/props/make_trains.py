"""The train family for Ride UK (trains plan stage 3, beenster Nov 20 2026: trains on every line to a made-up timetable,
https://claude.ai/artifact/S6xACsnVDrMdQ5EG9psUoB). Our own designs in invented operators' colours: no real company's
name, logo or livery. Built in Blender into Data/Props/Blender/<id>.glb like the shed props, so the PC imports them with
the rest (Tools/ue_import_props.py) and never needs Blender:

    blender -b --factory-startup -P Tools/props/make_trains.py                    all of them
    python3 Tools/props/make_trains.py -- train_dmu_car                            just these (bpy from pip)

Each vehicle in Torque Shed's frame (metres, x right, y up, +z its front), its origin on the rails' top under its middle,
its numbers the game's (World/WorldTrains.cpp Specs: length over the buffers, bogie centres, width, height):

    train_dmu_car        a rural diesel unit's car: a cab at its front (+z), two pairs of doors a side, a roof with
                         its exhaust and radiator; the game runs two (or four) back to back
    train_emu_cab        an electric commuter unit's driving car: a flatter, wider cab, three doors a side
    train_emu_mid        its middle car, the pantograph on its roof
    train_ic_power       the intercity set's power car: a long streamlined nose, no side doors
    train_ic_coach       the intercity set's coach: long windows, end doors
    train_freight_loco   a freight diesel: a full-width body with a cab each end and the engine room between
    train_wagon_box      a container flat with two boxes on it
    train_wagon_hopper   a covered stone hopper
    train_wagon_tank     a tank wagon
    train_steam_tank     a small six-coupled tank engine (heritage lines)
    train_steam_big      a larger express engine; its tender is train_steam_tender
    train_coach_old      an old corridor coach for the heritage lines
    train_diesel_old     an old diesel locomotive with a cab at each end (heritage and engineers' trains)

The liveries are material slots the game recolours (World/TorqueTrains.cpp): train_livery_a the body's main colour,
train_livery_b its band; the cab's lamps are train_lamp_head (white, lit at the front) and train_lamp_tail (red, lit at
the back); the side windows train_window (lit after dark). Deterministic.
"""
import json
import math
import os
import sys

import bpy  # noqa: E402,F401

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kit  # noqa: E402
from kit import box, span, cyl, prism  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "Data", "Props", "Blender")
MODELS = {}

# The game's numbers (World/WorldTrains.cpp Specs): length, bogie centre from the middle, width, height.
SPECS = {
    "train_dmu_car": (23.0, 8.0, 2.75, 3.80),
    "train_emu_cab": (20.4, 7.0, 2.80, 3.78),
    "train_emu_mid": (20.0, 7.0, 2.80, 3.78),
    "train_ic_power": (20.0, 6.5, 2.75, 3.85),
    "train_ic_coach": (23.0, 8.0, 2.75, 3.85),
    "train_freight_loco": (21.3, 7.0, 2.65, 3.90),
    "train_wagon_box": (19.6, 6.5, 2.60, 3.70),
    "train_wagon_hopper": (15.0, 4.8, 2.80, 3.70),
    "train_wagon_tank": (15.5, 4.8, 2.70, 3.80),
    "train_steam_tank": (10.0, 2.6, 2.60, 3.90),
    "train_steam_big": (13.5, 4.0, 2.75, 4.00),
    "train_steam_tender": (8.0, 2.3, 2.70, 3.60),
    "train_coach_old": (19.5, 6.4, 2.75, 3.80),
    "train_diesel_old": (18.0, 5.8, 2.70, 3.85),
}
GAUGE = 1.435
FLOOR = 1.10          # a modern body's underside over the rails
WHEEL_R = 0.42


def model(pid):
    def wrap(fn):
        MODELS[pid] = fn
        return fn
    return wrap


# --- Shared parts ------------------------------------------------------------------------------------------------------

import bmesh  # noqa: E402
from mathutils import Vector  # noqa: E402


def _faces(name, faces, mat):
    """One object from faces given as lists of Torque Shed points (already wound outward)."""
    bm = bmesh.new()
    cache = {}

    def v(p):
        key = (round(p[0], 5), round(p[1], 5), round(p[2], 5))
        if key not in cache:
            cache[key] = bm.verts.new(kit.B(p))
        return cache[key]

    for f in faces:
        vs = []
        for q in f:
            x = v(q)
            if x not in vs:
                vs.append(x)
        if len(vs) >= 3:
            try:
                bm.faces.new(vs)
            except ValueError:
                pass
    obj = kit._new(name, bm)
    return kit._finish(obj, mat, 0.0)


def _outward(face, inside):
    a, b, c = Vector(face[0]), Vector(face[1]), Vector(face[2])
    n = (b - a).cross(c - a)
    if n.length < 1e-10 and len(face) > 3:
        n = (c - a).cross(Vector(face[3]) - a)
    m = sum((Vector(q) for q in face), Vector()) / len(face)
    return list(reversed(face)) if n.dot(m - Vector(inside)) < 0 else face


def _centre(r):
    n = len(r)
    return [sum(q[i] for q in r) / n for i in range(3)]


def loft(name, rings, mat_of, cap_back=None, cap_front=None):
    """A skin through closed rings of (x, y, z) points (all the same count), back (-z) to front (+z). mat_of(k, j): the
    material of the quad between rings k, k+1 and points j, j+1 (None: left open); the end caps' materials."""
    groups = {}
    n = len(rings[0])
    for k in range(len(rings) - 1):
        ca, cb = _centre(rings[k]), _centre(rings[k + 1])
        inside = [(ca[i] + cb[i]) / 2 for i in range(3)]
        for j in range(n):
            m = mat_of(k, j)
            if not m:
                continue
            f = [rings[k][j], rings[k][(j + 1) % n], rings[k + 1][(j + 1) % n], rings[k + 1][j]]
            groups.setdefault(m, []).append(_outward(f, inside))
    if cap_back:
        c = _centre(rings[0])
        groups.setdefault(cap_back, []).append(_outward(list(rings[0]), (c[0], c[1], c[2] + 1.0)))
    if cap_front:
        c = _centre(rings[-1])
        groups.setdefault(cap_front, []).append(_outward(list(rings[-1]), (c[0], c[1], c[2] - 1.0)))
    for m, fs in groups.items():
        _faces("%s_%s" % (name, m), fs, m)


NR = 12   # points over a roof's arc


def ring(z, hw, ybot, ywin, yeave, ytop, nr=NR, tuck=0.07, p=2.4):
    """A body's cross-section at z (a closed loop): from the right foot up the side (its foot tucked in, a line at
    the windows' foot), over the roof to the left foot. Points 0..4 right (foot, 0.3 up, ywin - 0.07, ywin, eaves),
    5.. the roof's arc, then the left side mirrored. Quads: 0,1 lower right; 2 a thin row under the windows; 3 the
    window row; 4..nr+3 the roof; nr+4 window row left; nr+5 thin row; nr+6, nr+7 lower left; nr+8 the floor."""
    ytop = max(ytop, ybot + 0.45)
    yeave = min(yeave, ytop - 0.1)
    ywin = min(ywin, yeave - 0.06)
    ylo = min(ybot + 0.3, ywin - 0.1)
    right = [(hw - tuck, ybot), (hw, ylo), (hw, ywin - 0.07), (hw, ywin), (hw, yeave)]
    arc = []
    for i in range(1, nr):
        a = math.pi * i / nr
        c, s = math.cos(a), math.sin(a)
        arc.append((hw * math.copysign(abs(c) ** (2 / p), c), yeave + (ytop - yeave) * abs(s) ** (2 / p)))
    left = [(-x, y) for (x, y) in reversed(right)]
    return [(x, y, z) for (x, y) in right + arc + left]


def rows(nr=NR):
    """The quad rows of a ring: (lower, thin, window, roof, floor)."""
    n = nr + 9
    return ({0, 1, n - 3, n - 2}, {2, n - 4}, {3, n - 5}, set(range(4, n - 5)), {n - 1})


def _inside(pts, x, y):
    inside = False
    n = len(pts)
    for i in range(n):
        x1, y1 = pts[i][0], pts[i][1]
        x2, y2 = pts[(i + 1) % n][0], pts[(i + 1) % n][1]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1 + 1e-12) + x1:
            inside = not inside
    return inside


def surface_z(rings, x, y):
    """The front-most z where (x, y) is still inside the rings (on the skin there), by the rings and between them."""
    best = None
    for k in range(len(rings)):
        if _inside(rings[k], x, y):
            best = k
    if best is None:
        return None
    if best == len(rings) - 1:
        return rings[-1][0][2]
    # (between ring best, inside, and best + 1, outside: halve towards the skin)
    za, zb = rings[best][0][2], rings[best + 1][0][2]
    ra, rb = rings[best], rings[best + 1]
    lo, hi = 0.0, 1.0
    for _ in range(14):
        t = (lo + hi) / 2
        r = [(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t) for a, b in zip(ra, rb)]
        if _inside(r, x, y):
            lo = t
        else:
            hi = t
    return za + (zb - za) * lo


def wiper(name, rings, x, y0, y1, out=0.025):
    pts = []
    for k in range(6):
        y = y0 + (y1 - y0) * k / 5
        z = surface_z(rings, x + 0.06 * k / 5, y)
        if z is not None:
            pts.append((x + 0.06 * k / 5, y, z + out))
    if len(pts) >= 2:
        kit.tube(name, pts, 0.012, "steel_dark", sides=6)


def lamp_cluster(name, rings, x, y, f=1, head=0.075, tail=0.05, gap=0.2, housing=True):
    """A lamp cluster on a cab's face at (x, y): a black housing, a white head lamp inboard and a red tail outboard."""
    sx = 1 if x > 0 else -1
    for k, (dx, r, mat) in enumerate(((0.0, head, "train_lamp_head"), (sx * gap, tail, "train_lamp_tail"))):
        z = surface_z(rings, x + dx, y) if rings else None
        if z is None:
            continue
        cyl("%s_%d" % (name, k), (x + dx, y, z + f * 0.01), r, 0.05, mat, axis="z", verts=16, bevel=0.0)
        if housing:
            cyl("%s_%dH" % (name, k), (x + dx, y, z - f * 0.01), r + 0.03, 0.05, "paint_black", axis="z", verts=16, bevel=0.0)


def bogie(name, zc, wb=2.6, r=WHEEL_R, frame="train_bogie", skirt=None):
    """A two-axle bogie centred at zc: its side frames dipped between the axles, axleboxes on coil springs, dampers,
    brake discs, the bolster, four wheels."""
    for sx in (-1, 1):
        x = sx * 1.0
        prof = [(zc - wb / 2 - 0.55, 0.62), (zc - wb / 2 - 0.55, 0.88), (zc - wb / 2 + 0.35, 0.92), (zc - 0.35, 0.72),
                (zc + 0.35, 0.72), (zc + wb / 2 - 0.35, 0.92), (zc + wb / 2 + 0.55, 0.88), (zc + wb / 2 + 0.55, 0.62),
                (zc + wb / 2 - 0.3, 0.6), (zc + 0.3, 0.45), (zc - 0.3, 0.45), (zc - wb / 2 + 0.3, 0.6)]
        prism("%s_Side%+d" % (name, sx), prof, 0.16, frame, axis="x", centre=x, bevel=0.015)
        for dz in (-wb / 2, wb / 2):
            span("%s_Box%+d%+.1f" % (name, sx, dz), (x - 0.12, r - 0.14, zc + dz - 0.16), (x + 0.12, r + 0.12, zc + dz + 0.16), frame, bevel=0.02)
            cyl("%s_Coil%+d%+.1f" % (name, sx, dz), (x, r + 0.25, zc + dz), 0.09, 0.22, "train_spring", verts=10)
        # (the yaw damper along the frame, the secondary air spring on the bolster)
        kit.tube("%s_Damp%+d" % (name, sx), [(x + sx * 0.14, 0.78, zc - wb / 2 + 0.1), (x + sx * 0.14, 0.82, zc - 0.25)], 0.045, "steel_dark", sides=8)
        cyl("%s_Air%+d" % (name, sx), (x - sx * 0.1, 0.98, zc), 0.2, 0.18, "rubber", verts=16)
    span(name + "_Bolster", (-1.1, 0.72, zc - 0.3), (1.1, 0.9, zc + 0.3), frame, bevel=0.02)
    span(name + "_Trans", (-1.0, 0.6, zc - 0.12), (1.0, 0.72, zc + 0.12), frame, bevel=0.01)
    for dz in (-wb / 2, wb / 2):
        for sx in (-1, 1):
            cyl("%s_W%+d%+.1f" % (name, sx, dz), (sx * (GAUGE / 2 + 0.03), r, zc + dz), r, 0.13, "train_wheel", axis="x", verts=24)
            cyl("%s_D%+d%+.1f" % (name, sx, dz), (sx * (GAUGE / 2 - 0.25), r, zc + dz), r * 0.72, 0.05, "steel_dark", axis="x", verts=20)
        cyl("%s_Axle%+.1f" % (name, dz), (0.0, r, zc + dz), 0.08, GAUGE + 0.3, "train_bogie", axis="x", verts=10)
    if skirt:
        for sx in (-1, 1):
            span("%s_Skirt%+d" % (name, sx), (sx * 1.3 - 0.02, 0.62, zc - wb / 2 - 0.45), (sx * 1.3 + 0.02, 1.12, zc + wb / 2 + 0.45), skirt, bevel=0.01)


def underframe(name, length, w, y1=FLOOR, gap=None):
    """Equipment cases under the floor between the bogies: various sizes, ribbed, their brackets."""
    g = gap if gap is not None else length * 0.28
    cases = [(-g, -g * 0.62, 0.45, 0.62), (-g * 0.56, -g * 0.2, 0.55, 0.8), (-g * 0.12, g * 0.18, 0.5, 0.7),
             (g * 0.25, g * 0.55, 0.62, 0.55), (g * 0.62, g, 0.5, 0.75)]
    for k, (z0, z1, y0, hw) in enumerate(cases):
        mat = "train_under" if k % 2 == 0 else "train_grille"
        span("%s_%d" % (name, k), (-w * hw / 2, y0, z0), (w * hw / 2, y1, z1), mat, bevel=0.02)
        for sx in (-1, 1):
            for zz in (z0 + 0.15, z1 - 0.15):
                span("%s_%d_Br%+d%.1f" % (name, k, sx, zz), (sx * w * hw / 2 - 0.03, y1 - 0.25, zz - 0.04), (sx * w * hw / 2 + 0.03, y1, zz + 0.04), "train_bogie", bevel=0.0)
    span(name + "_Sole", (-w / 2 + 0.12, y1 - 0.12, -length / 2 + 0.6), (w / 2 - 0.12, y1, length / 2 - 0.6), "train_under", bevel=0.01)


def side_window(name, x_side, z0, z1, y0, y1, mat="train_window", frame="rubber", sx=1):
    """A flush window with its black rubber surround, on the side at x_side (sx: which side)."""
    span(name + "_F", (x_side - sx * 0.012, y0 - 0.05, z0 - 0.05), (x_side + sx * 0.016, y1 + 0.05, z1 + 0.05), frame, bevel=0.03)
    span(name, (x_side, y0, z0), (x_side + sx * 0.022, y1, z1), mat, bevel=0.025)


def window_band(name, w, z0, z1, y0, y1, count, gap=0.25, mat="train_window"):
    """A row of framed side windows on both sides from z0 to z1."""
    pitch = (z1 - z0) / count
    for sx in (-1, 1):
        for k in range(count):
            za, zb = z0 + k * pitch + gap / 2, z0 + (k + 1) * pitch - gap / 2
            side_window("%s_%+d_%d" % (name, sx, k), sx * (w / 2), za, zb, y0, y1, mat, sx=sx)


def doors(name, w, zs, y0, y1, width=1.3, mat="train_livery_b", pair=True):
    """Sliding plug doors on both sides at each z: a contrasting pair of leaves, a window in each, the seal line
    between them, the open button, the step under them."""
    for sx in (-1, 1):
        x = sx * (w / 2)
        for k, z in enumerate(zs):
            span("%s_%+d_%d" % (name, sx, k), (x - 0.015, y0, z - width / 2), (x + 0.015 + 0.004 * sx, y1, z + width / 2), mat, bevel=0.01)
            leaves = [(z - width / 2, z), (z, z + width / 2)] if pair else [(z - width / 2, z + width / 2)]
            for j, (za, zb) in enumerate(leaves):
                side_window("%s_%+d_%d_%d" % (name, sx, k, j), x + 0.02 * sx, za + 0.12, zb - 0.12, y0 + 1.0, y1 - 0.3, sx=sx)
            if pair:
                span("%s_%+d_%dS" % (name, sx, k), (x - 0.02 + 0.02 * sx, y0 + 0.02, z - 0.012), (x + 0.025 * sx + 0.02, y1 - 0.02, z + 0.012), "rubber", bevel=0.0)
            cyl("%s_%+d_%dB" % (name, sx, k), (x + 0.03 * sx, y0 + 1.05, z + width / 2 + 0.12), 0.035, 0.03, "train_lamp_green", axis="x", verts=10, bevel=0.0)
            span("%s_%+d_%dStep" % (name, sx, k), (x - 0.1 if sx > 0 else x - 0.12, y0 - 0.12, z - width / 2), (x + 0.12 if sx > 0 else x + 0.1, y0 - 0.06, z + width / 2), "train_under", bevel=0.01)


def roof_kit(name, z0, z1, H, w):
    """The roof's fittings: air-conditioning pods, vents and a rain strip."""
    for k, (za, zb) in enumerate(((z0, z0 + 2.6), (z1 - 2.6, z1))):
        prism("%s_Pod%d" % (name, k), [(-0.75, H - 0.05), (0.75, H - 0.05), (0.62, H + 0.28), (-0.62, H + 0.28)], zb - za, "train_roof", axis="z", centre=(za + zb) / 2, bevel=0.06)
        for j in range(4):
            span("%s_Pod%d_G%d" % (name, k, j), (-0.5, H + 0.28, za + 0.3 + j * 0.55), (0.5, H + 0.29, za + 0.7 + j * 0.55), "train_grille", bevel=0.0)
    for j in range(int((z1 - z0 - 6.0) / 2.5)):
        z = z0 + 3.6 + j * 2.5
        cyl("%s_Vent%d" % (name, j), (0.45 * (1 if j % 2 else -1), H + 0.02, z), 0.12, 0.08, "train_grille", verts=12)
    for sx in (-1, 1):
        span("%s_Gutter%+d" % (name, sx), (sx * (w / 2 - 0.18) - 0.02, H - 0.42, z0), (sx * (w / 2 - 0.18) + 0.02, H - 0.39, z1), "train_roof", bevel=0.0)


def gangway(name, z, f, y0=FLOOR + 0.15, y1=3.1, open_end=True):
    """A gangway's bellows at an inner end (z, facing f)."""
    for k in range(5):
        span("%s_%d" % (name, k), (-0.62 + 0.02 * (k % 2), y0 + 0.02 * (k % 2), z + f * (0.03 + 0.06 * k) - 0.03), (0.62 - 0.02 * (k % 2), y1 - 0.02 * (k % 2), z + f * (0.03 + 0.06 * k) + 0.03), "rubber", bevel=0.01)


def lamps(name, z, w, y=1.45, spread=0.55):
    """A plain end's lamps facing +z (or -z with z < 0): white head lamps inboard, red tail lamps outboard."""
    f = 1 if z > 0 else -1
    for sx in (-1, 1):
        cyl("%s_Head%+d" % (name, sx), (sx * spread, y, z + f * 0.01), 0.11, 0.06, "train_lamp_head", axis="z", verts=16, bevel=0.0)
        cyl("%s_Tail%+d" % (name, sx), (sx * (w / 2 - 0.25), y, z + f * 0.01), 0.07, 0.06, "train_lamp_tail", axis="z", verts=12, bevel=0.0)


def buffers(name, z, y=1.05, sep=1.75, beam="train_bogie"):
    f = 1 if z > 0 else -1
    span(name + "_Beam", (-1.35, 0.8, z - f * 0.3 - 0.12), (1.35, 1.3, z - f * 0.3 + 0.12), beam, bevel=0.02)
    for sx in (-1, 1):
        cyl("%s_B%+d" % (name, sx), (sx * sep / 2, y, z - f * 0.25), 0.1, 0.35, "train_bogie", axis="z", verts=14)
        cyl("%s_S%+d" % (name, sx), (sx * sep / 2, y, z - f * 0.1), 0.07, 0.15, "steel", axis="z", verts=12)
        cyl("%s_H%+d" % (name, sx), (sx * sep / 2, y, z - f * 0.02), 0.21, 0.05, "steel_dark", axis="z", verts=20)
        # (the brake and air pipes hanging beside the hook)
        kit.tube("%s_Pipe%+d" % (name, sx), [(sx * 0.45, 0.95, z - f * 0.42), (sx * 0.45, 0.75, z - f * 0.2), (sx * 0.4, 0.62, z - f * 0.05)], 0.03, "rubber", sides=6)
        cyl("%s_Cock%+d" % (name, sx), (sx * 0.45, 0.6, z - f * 0.05), 0.04, 0.06, "train_buffer_red" if sx > 0 else "train_warning", axis="z", verts=8)
    span(name + "_Hook", (-0.07, y - 0.08, z - f * 0.3 - 0.12), (0.07, y + 0.08, z - f * 0.05 + 0.02), "steel_dark", bevel=0.01)
    kit.tube(name + "_Chain", [(-0.06, y - 0.05, z - f * 0.06), (0.0, y - 0.25, z - f * 0.02), (0.06, y - 0.05, z - f * 0.06)], 0.025, "steel_dark", sides=6)


def auto_coupler(name, z, f, y=0.95):
    """A unit's automatic coupler under the cab: its shank, head and hatch."""
    span(name + "_Shank", (-0.12, y - 0.1, z - f * 0.6), (0.12, y + 0.1, z + f * 0.05), "steel_dark", bevel=0.02)
    span(name + "_Head", (-0.32, y - 0.22, z + f * 0.02 - 0.1), (0.32, y + 0.22, z + f * 0.02 + 0.1), "steel_dark", bevel=0.04)
    span(name + "_Face", (-0.28, y - 0.18, z + f * 0.12 - 0.005), (0.28, y + 0.18, z + f * 0.12 + 0.005), "train_buffer_red", bevel=0.0)


def pantograph(name, z, top):
    span(name + "_Base", (-0.7, top, z - 0.9), (0.7, top + 0.12, z + 0.9), "train_bogie", bevel=0.02)
    for sx in (-1, 1):
        for dz in (-0.7, 0.7):
            cyl("%s_Ins%+d%+.1f" % (name, sx, dz), (sx * 0.55, top + 0.2, z + dz), 0.07, 0.28, "train_insulator", verts=10)
            for j in range(3):
                cyl("%s_Ins%+d%+.1f_%d" % (name, sx, dz, j), (sx * 0.55, top + 0.12 + 0.08 * j, z + dz), 0.1, 0.02, "train_insulator", verts=10)
    span(name + "_Frame", (-0.65, top + 0.32, z - 0.85), (0.65, top + 0.4, z + 0.85), "steel_dark", bevel=0.01)
    for sx in (-1, 1):
        kit.tube("%s_Arm1%+d" % (name, sx), [(sx * 0.35, top + 0.4, z - 0.6), (sx * 0.08, top + 1.15, z + 0.35)], 0.035, "steel_dark", sides=8)
        kit.tube("%s_Arm2%+d" % (name, sx), [(sx * 0.08, top + 1.15, z + 0.35), (sx * 0.3, top + 1.9, z - 0.25)], 0.028, "steel_dark", sides=8)
    cyl(name + "_Knee", (0.0, top + 1.15, z + 0.35), 0.05, 0.3, "steel_dark", axis="x", verts=10)
    cyl(name + "_Spring", (0.0, top + 0.6, z - 0.55), 0.08, 0.45, "train_spring", axis="z", verts=10)
    for dz in (-0.12, 0.12):
        span("%s_Head%+.2f" % (name, dz), (-0.9, top + 1.92, z - 0.25 + dz - 0.035), (0.9, top + 1.98, z - 0.25 + dz + 0.035), "steel", bevel=0.01)
        for sx in (-1, 1):
            kit.tube("%s_Horn%+d%+.2f" % (name, sx, dz), [(sx * 0.9, top + 1.95, z - 0.25 + dz), (sx * 1.02, top + 1.88, z - 0.25 + dz), (sx * 1.06, top + 1.8, z - 0.25 + dz)], 0.015, "steel", sides=6)


def handrail(name, pts):
    kit.tube(name, pts, 0.016, "steel", sides=8)


def section(w, y0, eaves, top, n=7, tumble=0.06):
    """An old coach's cross-section (x, y): sides from y0 up to the eaves, a curved roof."""
    hw = w / 2
    pts = [(-hw + tumble, y0), (hw - tumble, y0), (hw, y0 + 0.5), (hw, eaves)]
    for k in range(1, n):
        a = math.pi / 2 * k / n
        pts.append((hw * math.cos(a) * 0.92 + 0.08 * hw * (1 - k / n), eaves + (top - eaves) * math.sin(a)))
    pts.append((0.0, top))
    left = [(-x, y) for (x, y) in reversed(pts[3:-1])]
    return pts + left + [(-hw, eaves), (-hw, y0 + 0.5)]


def shell(name, w, z0, z1, y0, eaves, top, mat):
    prism(name, section(w, y0, eaves, top), z1 - z0, mat, axis="z", centre=(z0 + z1) / 2, bevel=0.02)


def lerp(a, b, t):
    return a + (b - a) * t


def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


# --- The units --------------------------------------------------------------------------------------------------------

def body_rings(hz0, hz1, W, H, ywin=1.95, yeave=3.0):
    return [ring(hz0, W / 2, FLOOR, ywin, yeave, H), ring(hz1, W / 2, FLOOR, ywin, yeave, H)]


def unit_nose(W, H, z0, length, emu):
    """A unit's cab: rings from the body's end (z0) forward. The electric unit's is a rounded wedge with a wrapround
    screen; the diesel unit's a flat end raked a little at the top, its screens and the yellow end on its face."""
    rs = []
    N = 14
    for k in range(N + 1):
        t = k / N
        z = z0 + length * (1 - (1 - t) ** 1.6) if emu else z0 + length * t
        if emu:
            hw = W / 2 - 0.42 * t ** 2.4
            ytop = lerp(H, FLOOR + 1.0, smooth((t - 0.25) / 0.75) ** 1.3)
            ybot = FLOOR - 0.1 * t
            ywin = lerp(1.95, FLOOR + 0.95, smooth((t - 0.3) / 0.6))
            yeave = 3.0
        else:
            hw = W / 2 - 0.16 * smooth((t - 0.55) / 0.45) ** 1.5
            ytop = H - 0.28 * smooth((t - 0.5) / 0.5) ** 1.2
            ybot = FLOOR - 0.12 * t
            ywin = 1.95
            yeave = 3.0
        rs.append(ring(z, hw, ybot, ywin, yeave, ytop))
    return rs


def unit_car(pid, cab, pan=False, emu=False):
    L, bc, W, H = SPECS[pid]
    hz = L / 2
    nose = 2.4 if emu else 0.55
    z1 = hz - nose if cab else hz - 0.05
    z0 = -hz + 0.05
    lower, thin, win, roof, floor = rows()
    body = body_rings(z0, z1, W, H)
    loft("Body", body, lambda k, j: "train_under" if j in floor else "train_roof" if j in roof and 6 <= j <= NR + 2 else "train_livery_a",
         cap_back="train_livery_a", cap_front=None if cab else "train_livery_a")
    # (the band along its windows' foot)
    for sx in (-1, 1):
        span("Band%+d" % sx, (sx * (W / 2 - 0.006), 1.68, z0 + 0.1), (sx * (W / 2 + 0.006 + 0.004), 1.82, z1 - 0.05), "train_livery_b", bevel=0.0)
    if cab:
        rs = [body[-1]] + unit_nose(W, H, z1, nose, emu)[1:]
        n = len(rs[0])
        if emu:
            # (the wrapround screen in the middle of the nose, a black mask round it, the yellow lower face)
            def m(k, j):
                if j in floor:
                    return "train_under"
                if j in win | roof:
                    if 5 <= k <= 11:
                        return "train_cab_glass"
                    if k in (4, 12, 13):
                        return "paint_black"
                    return "train_livery_a"
                if j in thin and 4 <= k <= 13:
                    return "paint_black"
                return "train_warning" if k >= 10 else "train_livery_a"
            loft("Nose", rs, m, cap_front="train_warning")
            zf = rs[-1][0][2]
            for sx in (-1, 1):
                lamp_cluster("Lamp%+d" % sx, rs, sx * 0.72, FLOOR + 0.55, gap=0.22)
                wiper("Wiper%+d" % sx, rs, sx * 0.45 - 0.03, FLOOR + 1.35, FLOOR + 2.0)
            zs = surface_z(rs, 0.0, H - 0.25)
            if zs is not None:
                span("Dest", (-0.55, H - 0.36, zs - 0.06), (0.55, H - 0.2, zs + 0.01), "train_dest", bevel=0.0)
            auto_coupler("Coupler", zf, 1)
            span("Plough", (-1.1, 0.25, zf - 0.35), (1.1, 0.85, zf - 0.15), "train_under", bevel=0.03)
        else:
            def m(k, j):
                if j in floor:
                    return "train_under"
                if k >= len(rs) - 5:
                    return "train_warning" if j not in roof or k >= len(rs) - 3 else "train_livery_a"
                return "train_roof" if j in roof and 6 <= j <= NR + 2 else "train_livery_a"
            loft("Nose", rs, m, cap_front="train_warning")
            zf = rs[-1][0][2]
            # (two screens in black surrounds, the number box and its headcode panel between, wipers, lamps low)
            for sx in (-1, 1):
                x0, x1 = (0.08, 1.12) if sx > 0 else (-1.12, -0.08)
                span("ScreenF%+d" % sx, (x0 - 0.06, 1.98, zf - 0.02), (x1 + 0.06, 2.92, zf + 0.02), "paint_black", bevel=0.04)
                span("Screen%+d" % sx, (x0, 2.04, zf), (x1, 2.86, zf + 0.03), "train_cab_glass", bevel=0.03)
                kit.tube("Wiper%+d" % sx, [((x0 + x1) / 2, 2.08, zf + 0.04), ((x0 + x1) / 2 + 0.25, 2.65, zf + 0.04)], 0.012, "steel_dark", sides=6)
                for j, (dx, r, mat) in enumerate(((0.0, 0.085, "train_lamp_head"), (0.24, 0.055, "train_lamp_tail"))):
                    cyl("Lamp%+d_%d" % (sx, j), (sx * (0.72 + dx), 1.55, zf + 0.02), r, 0.05, mat, axis="z", verts=16, bevel=0.0)
                    cyl("Lamp%+d_%dH" % (sx, j), (sx * (0.72 + dx), 1.55, zf), r + 0.03, 0.05, "paint_black", axis="z", verts=16, bevel=0.0)
                handrail("Grab%+d" % sx, [(sx * 1.25, 1.75, zf + 0.0), (sx * 1.25, 1.75, zf + 0.08), (sx * 1.25, 2.75, zf + 0.08), (sx * 1.25, 2.75, zf + 0.0)])
            span("NumBox", (-0.42, 1.68, zf - 0.02), (0.42, 1.9, zf + 0.03), "paint_black", bevel=0.01)
            span("NumText", (-0.36, 1.72, zf + 0.02), (0.36, 1.86, zf + 0.035), "train_dest", bevel=0.0)
            span("Horns", (-0.25, 1.2, zf - 0.02), (0.25, 1.38, zf + 0.04), "train_grille", bevel=0.01)
            auto_coupler("Coupler", zf, 1)
            span("Valance", (-1.25, 0.55, zf - 0.6), (1.25, FLOOR, zf - 0.05), "train_under", bevel=0.03)
        # (the cab's side windows and doors)
        for sx in (-1, 1):
            zc = z1 - 0.7 if not emu else z1 - 0.15
            side_window("CabWin%+d" % sx, sx * W / 2, zc - 0.55, zc + 0.15, 2.0, 2.78, "train_cab_glass", sx=sx)
            span("CabDoor%+d" % sx, (sx * (W / 2 - 0.012), FLOOR + 0.15, zc - 1.45), (sx * (W / 2 + 0.012 + 0.004), 2.9, zc - 0.75), "train_livery_b", bevel=0.01)
            side_window("CabDoorW%+d" % sx, sx * W / 2 + 0.015 * sx, zc - 1.35, zc - 0.85, 2.05, 2.7, "train_cab_glass", sx=sx)
            handrail("CabRail%+d" % sx, [(sx * W / 2, 1.5, zc - 1.52), (sx * (W / 2 + 0.06), 1.5, zc - 1.52), (sx * (W / 2 + 0.06), 2.6, zc - 1.52), (sx * W / 2, 2.6, zc - 1.52)])
    else:
        gangway("GangF", hz - 0.05, 1)
    gangway("GangR", -hz + 0.05, -1)
    # (doors at the thirds or quarters, windows between)
    cab_room = (2.3 if not emu else 0.9) if cab else 0.0
    dz = [-L * 0.3, L * 0.3 - cab_room] if not emu else [-L * 0.33, -L * 0.02, L * 0.3 - cab_room]
    doors("Door", W, dz, FLOOR + 0.15, 3.05, width=1.3 if not emu else 1.5)
    edges = [z0 + 0.8] + sum([[d - 0.95, d + 0.95] for d in dz], []) + [(z1 - (2.4 if not emu else 1.3)) if cab else z1 - 0.8]
    for k in range(0, len(edges), 2):
        za, zb = edges[k], edges[k + 1]
        n = max(1, int(round((zb - za) / 2.1)))
        if zb - za > 1.0:
            window_band("Win%d" % k, W, za, zb, 1.95, 2.78, n, gap=0.2)
    bogie("BogieF", bc, skirt=None)
    bogie("BogieR", -bc)
    underframe("Under", L, W, gap=bc - 1.8)
    roof_kit("Roof", z0 + 1.5, z1 - 1.5, H, W)
    if pan:
        pantograph("Pan", -L * 0.12, H + 0.1)
        span("PanWell", (-0.9, H - 0.05, -L * 0.12 - 1.4), (0.9, H + 0.1, -L * 0.12 + 1.4), "train_roof", bevel=0.03)
        for sx in (-1, 1):
            kit.tube("PanBus%+d" % sx, [(sx * 0.2, H + 0.25, -L * 0.12 + 0.8), (sx * 0.2, H + 0.25, -L * 0.12 + 3.5)], 0.03, "steel", sides=6)
    if not emu:
        # (a diesel unit's roof exhaust stacks and the radiator grille under the floor)
        for sx in (-1, 1):
            cyl("Exhaust%+d" % sx, (sx * 0.5, H + 0.12, L * 0.05), 0.07, 0.35, "steel_dark", verts=10)
        span("Radiator", (-1.0, 0.55, -L * 0.12), (1.0, 1.05, L * 0.02), "train_grille", bevel=0.01)


@model("train_dmu_car")
def train_dmu_car():
    unit_car("train_dmu_car", cab=True)


@model("train_emu_cab")
def train_emu_cab():
    unit_car("train_emu_cab", cab=True, emu=True)


@model("train_emu_mid")
def train_emu_mid():
    unit_car("train_emu_mid", cab=False, pan=True, emu=True)


# --- The intercity set ------------------------------------------------------------------------------------------------

@model("train_ic_power")
def train_ic_power():
    """The power car: a long smooth nose dropping to a low snout, a black wrapround screen across it, a sweep of the
    band colour under the screen to the snout, the lamps set in the snout's cheeks."""
    L, bc, W, H = SPECS["train_ic_power"]
    hz = L / 2
    nose = 6.6
    zb = hz - nose
    lower, thin, win, roof, floor = rows()
    body = body_rings(-hz + 0.05, zb, W, H, yeave=3.05)
    loft("Body", body, lambda k, j: "train_under" if j in floor else "train_roof" if j in roof and 6 <= j <= NR + 2 else "train_livery_a", cap_back="train_livery_a")
    rs = []
    N = 26
    for k in range(N + 1):
        t = k / N
        z = zb + nose * (1 - (1 - t) ** 1.35)
        hw = W / 2 * (1 - 0.22 * t ** 2.0)
        if t > 0.82:
            hw *= max(0.28, math.sqrt(max(0.0, 1 - ((t - 0.82) / 0.18) ** 2)))
        ytip = FLOOR + 0.72
        ytop = ytip + (H - ytip) * math.cos(math.pi / 2 * t ** 1.15)
        ybot = FLOOR - 0.15 + 0.32 * t ** 3
        ywin = lerp(2.15, FLOOR + 0.6, smooth((t - 0.2) / 0.55))
        if t > 0.92:
            ytop = max(ytop, ybot + 0.5)
        rs.append(ring(z, hw, ybot, ywin, 3.05, ytop))
    rs[0] = body[-1]

    def m(k, j):
        t = k / N
        if j in floor:
            return "train_under"
        if j in win | roof:
            if 0.27 <= t < 0.6:
                return "train_cab_glass"
            if 0.6 <= t < 0.66 or 0.23 <= t < 0.27:
                return "paint_black"
            if 0.08 <= t < 0.2 and j in win:
                return "train_cab_glass"
            return "train_livery_a"
        if j in thin:
            return "paint_black" if 0.2 <= t < 0.66 else "train_livery_b" if t >= 0.66 else "train_livery_a"
        # (the band's sweep: low along the body, rising up the nose's cheeks)
        return "train_livery_b" if t >= 0.35 else "train_livery_a"
    loft("Nose", rs, m, cap_front="train_livery_b")
    zf = rs[-1][0][2]
    for sx in (-1, 1):
        lamp_cluster("Lamp%+d" % sx, rs, sx * 0.55, FLOOR + 0.4, gap=0.18)
        wiper("Wiper%+d" % sx, rs, sx * 0.4, FLOOR + 2.05, FLOOR + 2.45)
        # (the band along the body under the windows, meeting the nose's sweep)
        span("Band%+d" % sx, (sx * (W / 2 - 0.006), 1.42, -hz + 0.1), (sx * (W / 2 + 0.006 + 0.004), 1.62, zb), "train_livery_b", bevel=0.0)
        # (the engine room's grilles, the cab door behind the screen)
        for k in range(3):
            z = -hz + 2.0 + k * 3.0
            span("GrilleF%+d_%d" % (sx, k), (sx * (W / 2 - 0.02), 2.05, z - 0.05), (sx * (W / 2 + 0.02 + 0.004), 2.95, z + 2.45), "rubber", bevel=0.01)
            span("Grille%+d_%d" % (sx, k), (sx * (W / 2 - 0.02 + 0.006), 2.1, z), (sx * (W / 2 + 0.026 + 0.004), 2.9, z + 2.4), "train_grille", bevel=0.0)
        span("CabDoor%+d" % sx, (sx * (W / 2 - 0.012), FLOOR + 0.15, zb - 1.6), (sx * (W / 2 + 0.012 + 0.004), 2.95, zb - 0.8), "train_livery_a", bevel=0.01)
        side_window("CabDoorW%+d" % sx, sx * W / 2 + 0.015 * sx, zb - 1.5, zb - 0.9, 2.1, 2.75, "train_cab_glass", sx=sx)
        handrail("CabRail%+d" % sx, [(sx * W / 2, 1.5, zb - 1.68), (sx * (W / 2 + 0.06), 1.5, zb - 1.68), (sx * (W / 2 + 0.06), 2.6, zb - 1.68), (sx * W / 2, 2.6, zb - 1.68)])
    zs = surface_z(rs, 0.0, FLOOR + 0.55)
    if zs is not None:
        span("Hatch", (-0.35, FLOOR + 0.3, zs - 0.08), (0.35, FLOOR + 0.62, zs + 0.012), "paint_black", bevel=0.02)
    gangway("GangR", -hz + 0.05, -1)
    roof_kit("Roof", -hz + 1.0, zb - 0.5, H, W)
    cyl("Exhaust", (0.0, H + 0.1, -hz + 5.0), 0.1, 0.25, "steel_dark", verts=10)
    bogie("BogieF", bc, wb=2.6, skirt="train_livery_a")
    bogie("BogieR", -bc, wb=2.6, skirt="train_livery_a")
    underframe("Under", L, W, gap=bc - 1.8)
    span("Plough", (-1.0, 0.3, zf - 1.3), (1.0, 0.75, zf - 0.6), "train_under", bevel=0.04)


@model("train_ic_coach")
def train_ic_coach():
    L, bc, W, H = SPECS["train_ic_coach"]
    hz = L / 2
    lower, thin, win, roof, floor = rows()
    loft("Body", body_rings(-hz + 0.05, hz - 0.05, W, H, yeave=3.05),
         lambda k, j: "train_under" if j in floor else "train_roof" if j in roof and 6 <= j <= NR + 2 else "train_livery_a",
         cap_back="train_livery_a", cap_front="train_livery_a")
    for sx in (-1, 1):
        span("Band%+d" % sx, (sx * (W / 2 - 0.006), 1.42, -hz + 0.1), (sx * (W / 2 + 0.006 + 0.004), 1.62, hz - 0.1), "train_livery_b", bevel=0.0)
    doors("Door", W, [-hz + 1.3, hz - 1.3], FLOOR + 0.15, 3.05, width=0.95, mat="train_livery_a", pair=False)
    window_band("Win", W, -hz + 2.3, hz - 2.3, 1.95, 2.8, 8, gap=0.4)
    for z, f in ((-hz + 0.05, -1), (hz - 0.05, 1)):
        gangway("Gang%+d" % f, z, f)
    roof_kit("Roof", -hz + 1.2, hz - 1.2, H, W)
    bogie("BogieF", bc, skirt="train_livery_a")
    bogie("BogieR", -bc, skirt="train_livery_a")
    underframe("Under", L, W, gap=bc - 1.8)


# --- Freight ----------------------------------------------------------------------------------------------------------

def loco_cab_rings(W, H, z0, length, f):
    """A modern loco's cab end: the body carried to its face, the big screen raked back, the corners rounded."""
    rs = []
    N = 10
    for k in range(N + 1):
        t = k / N
        z = z0 + f * length * t
        hw = W / 2 - 0.12 * smooth((t - 0.5) / 0.5) ** 1.5
        ytop = H - (H - 2.45) * smooth((t - 0.35) / 0.65) ** 0.9
        rs.append(ring(z, hw, FLOOR, 2.3, 3.2, ytop, tuck=0.03))
    return rs if f > 0 else list(reversed(rs))


@model("train_freight_loco")
def train_freight_loco():
    L, bc, W, H = SPECS["train_freight_loco"]
    hz = L / 2
    lower, thin, win, roof, floor = rows()
    zc = hz - 1.4
    body = [ring(-zc, W / 2, FLOOR, 2.3, 3.2, H, tuck=0.03), ring(zc, W / 2, FLOOR, 2.3, 3.2, H, tuck=0.03)]
    loft("Body", body, lambda k, j: "train_under" if j in floor else "train_roof" if j in roof and 5 <= j <= NR + 3 else "train_livery_a")
    for f in (1, -1):
        rs = loco_cab_rings(W, H, f * zc, 1.25, f)
        N = len(rs) - 1

        def m(k, j, f=f, N=N):
            kk = k if f > 0 else N - 1 - k   # (counted from the body towards the face)
            if j in floor:
                return "train_under"
            if j in win | roof and kk >= 4:
                return "train_cab_glass" if kk <= 8 and (j in roof or kk >= 6) else "paint_black"
            if j in thin and kk >= 4:
                return "paint_black"
            return "train_livery_b" if kk >= 7 else "train_livery_a"
        loft("Cab%+d" % f, rs, m, cap_front="train_livery_b" if f > 0 else None, cap_back="train_livery_b" if f < 0 else None)
        zf = f * (zc + 1.25)
        for sx in (-1, 1):
            lamp_cluster("Lamp%+d%+d" % (f, sx), rs, sx * 0.78, FLOOR + 1.0, f=f, gap=0.24)
            wiper("Wiper%+d%+d" % (f, sx), rs, sx * 0.45, 2.55, 3.15)
            # (a cab door each side with its window and grab rails, the cab's side window)
            zd = f * (zc - 0.4)
            span("CabDoor%+d%+d" % (f, sx), (sx * (W / 2 - 0.01), FLOOR + 0.1, zd - 0.42), (sx * (W / 2 + 0.012 + 0.004), 3.15, zd + 0.42), "train_livery_a", bevel=0.01)
            side_window("CabDoorW%+d%+d" % (f, sx), sx * W / 2 + 0.015 * sx, zd - 0.3, zd + 0.3, 2.35, 2.95, "train_cab_glass", sx=sx)
            for dz in (-0.5, 0.5):
                handrail("Grab%+d%+d%+.1f" % (f, sx, dz), [(sx * W / 2, 1.3, zd + dz), (sx * (W / 2 + 0.07), 1.3, zd + dz), (sx * (W / 2 + 0.07), 2.9, zd + dz), (sx * W / 2, 2.9, zd + dz)])
            for j in range(3):
                span("Step%+d%+d_%d" % (f, sx, j), (sx * (W / 2 - 0.05) - 0.18, 0.45 + j * 0.3, zd - 0.3), (sx * (W / 2 - 0.05) + 0.18, 0.49 + j * 0.3, zd + 0.3), "train_bogie", bevel=0.0)
        buffers("Buf%+d" % f, f * hz)
        zp = f * (hz - 0.15)
        prism("Plough%+d" % f, [(zp - f * 0.5, 0.12), (zp, 0.12), (zp - f * 0.05, 0.75), (zp - f * 0.5, 0.75)], 2.4, "train_under", axis="x", bevel=0.03)
        span("Lettering%+d" % f, (-0.6, 1.35, zf - 0.008 * f - 0.006), (0.6, 1.55, zf + 0.008 * f + 0.006), "train_livery_a", bevel=0.0)
    for sx in (-1, 1):
        span("Band%+d" % sx, (sx * (W / 2 - 0.006), 1.35, -zc), (sx * (W / 2 + 0.006 + 0.004), 1.5, zc), "train_livery_b", bevel=0.0)
        span("Band2%+d" % sx, (sx * (W / 2 - 0.006), 3.12, -zc), (sx * (W / 2 + 0.006 + 0.004), 3.2, zc), "train_livery_b", bevel=0.0)
        for k in range(5):
            z = -zc + 1.5 + k * 2.6
            span("GrilleF%+d_%d" % (sx, k), (sx * (W / 2 - 0.02), 2.05, z - 0.05), (sx * (W / 2 + 0.02 + 0.004), 3.0, z + 1.55), "rubber", bevel=0.01)
            span("Grille%+d_%d" % (sx, k), (sx * (W / 2 - 0.02 + 0.006), 2.1, z), (sx * (W / 2 + 0.026 + 0.004), 2.95, z + 1.5), "train_grille", bevel=0.0)
        # (the body's panel lines, the solebar)
        for k in range(6):
            z = -zc + 0.4 + k * (2 * zc - 0.8) / 5
            span("Seam%+d_%d" % (sx, k), (sx * (W / 2 - 0.006), FLOOR + 0.1, z - 0.008), (sx * (W / 2 + 0.008 + 0.002), 3.15, z + 0.008), "train_grille", bevel=0.0)
        span("Sole%+d" % sx, (sx * (W / 2 - 0.05) - 0.06, FLOOR - 0.2, -hz + 0.3), (sx * (W / 2 - 0.05) + 0.06, FLOOR + 0.02, hz - 0.3), "train_bogie", bevel=0.01)
    # (the roof: fans, the silencer, hatches, a pantograph well, the horns)
    for k in range(3):
        cyl("Fan%d" % k, (0.0, H + 0.02, -3.6 + k * 2.2), 0.62, 0.08, "train_grille", verts=24)
        cyl("FanRim%d" % k, (0.0, H + 0.04, -3.6 + k * 2.2), 0.68, 0.04, "train_roof", verts=24)
    span("Silencer", (-0.45, H - 0.05, 0.4), (0.45, H + 0.35, 2.6), "steel_dark", bevel=0.05)
    cyl("Exhaust", (0.0, H + 0.45, 1.5), 0.15, 0.2, "steel_dark", verts=12)
    for k in range(4):
        span("Hatch%d" % k, (-0.95, H - 0.02, 3.0 + k * 0.9), (0.95, H + 0.03, 3.8 + k * 0.9), "train_roof", bevel=0.02)
    for f in (1, -1):
        for sx in (-1, 1):
            cyl("Horn%+d%+d" % (f, sx), (sx * 0.3, H + 0.08, f * (zc - 0.3)), 0.07, 0.35, "steel_dark", axis="z", verts=10, radius2=0.11)
    # (three-axle bogies with their springs and dampers, the fuel tank and air tanks between)
    for zc2 in (bc, -bc):
        for sx in (-1, 1):
            prof = [(zc2 - 2.55, 0.55), (zc2 - 2.55, 0.95), (zc2 + 2.55, 0.95), (zc2 + 2.55, 0.55), (zc2 + 1.4, 0.48), (zc2 - 1.4, 0.48)]
            prism("Frame%+.0f%+d" % (zc2, sx), prof, 0.18, "train_bogie", axis="x", centre=sx * 1.05, bevel=0.02)
            for dz in (-1.9, 0.0, 1.9):
                cyl("W%+.0f%+.1f%+d" % (zc2, dz, sx), (sx * (GAUGE / 2 + 0.03), 0.53, zc2 + dz), 0.53, 0.13, "train_wheel", axis="x", verts=24)
                span("Box%+.0f%+.1f%+d" % (zc2, dz, sx), (sx * 1.05 - 0.13, 0.4, zc2 + dz - 0.18), (sx * 1.05 + 0.13, 0.68, zc2 + dz + 0.18), "train_bogie", bevel=0.02)
                cyl("Coil%+.0f%+.1f%+d" % (zc2, dz, sx), (sx * 1.05, 0.82, zc2 + dz), 0.1, 0.25, "train_spring", verts=10)
            kit.tube("Damp%+.0f%+d" % (zc2, sx), [(sx * 1.2, 0.6, zc2 - 1.0), (sx * 1.2, 1.0, zc2 - 0.6)], 0.045, "steel_dark", sides=8)
            span("Sand%+.0f%+d" % (zc2, sx), (sx * 1.15 - 0.12, 0.7, zc2 + 2.6), (sx * 1.15 + 0.12, 1.05, zc2 + 2.85), "train_bogie", bevel=0.02)
    span("Tank", (-1.15, 0.45, -3.4), (1.15, FLOOR - 0.05, 3.4), "train_under", bevel=0.1)
    for sx in (-1, 1):
        span("TankBand%+d" % sx, (sx * 1.15 - 0.01, 0.6, -3.4), (sx * 1.15 + 0.01, 0.8, 3.4), "train_livery_b", bevel=0.0)
        cyl("Filler%+d" % sx, (sx * 1.16, 0.95, 2.6), 0.08, 0.05, "steel_dark", axis="x", verts=10)
    for sx in (-1, 1):
        cyl("AirTank%+d" % sx, (sx * 0.6, 0.7, -4.2), 0.22, 1.0, "train_under", axis="z", verts=14)


def flat_deck(pid):
    L, bc, W, H = SPECS[pid]
    hz = L / 2
    span("Deck", (-W / 2 + 0.15, 1.0, -hz + 0.4), (W / 2 - 0.15, 1.25, hz - 0.4), "train_wagon_frame", bevel=0.02)
    span("Spine", (-0.3, 0.8, -hz + 0.6), (0.3, 1.05, hz - 0.6), "train_wagon_frame", bevel=0.02)
    for z in (hz, -hz):
        buffers("Buf%+d" % (1 if z > 0 else -1), z)
    bogie("BogieF", bc, wb=1.8, r=0.42, frame="train_wagon_frame")
    bogie("BogieR", -bc, wb=1.8, r=0.42, frame="train_wagon_frame")


@model("train_wagon_box")
def train_wagon_box():
    L, bc, W, H = SPECS["train_wagon_box"]
    flat_deck("train_wagon_box")
    # (two containers, our own colours, their ribs and doors)
    for k, (z0, z1, mat) in enumerate(((-9.2, -0.2, "train_container_1"), (0.2, 9.2, "train_container_2"))):
        span("Box%d" % k, (-1.22, 1.27, z0), (1.22, 1.27 + 2.6, z1), mat, bevel=0.03)
        for j in range(int((z1 - z0) / 0.6)):
            for sx in (-1, 1):
                span("Rib%d_%d%+d" % (k, j, sx), (sx * 1.22 - 0.02, 1.32, z0 + 0.3 + j * 0.6), (sx * 1.22 + 0.02, 3.82, z0 + 0.36 + j * 0.6), mat, bevel=0.0)
        span("Doors%d" % k, (-1.18, 1.3, z1 - 0.02), (1.18, 3.84, z1 + 0.02), "train_wagon_frame", bevel=0.0)
        for sx in (-1, 1):
            for zz in (z0, z1):
                for yy in (1.27, 3.77):
                    span("Corner%d%+d%.0f%.0f" % (k, sx, zz, yy), (sx * 1.22 - 0.09, yy, zz - 0.09), (sx * 1.22 + 0.09, yy + 0.1, zz + 0.09), "train_wagon_frame", bevel=0.0)
            for j in range(4):
                span("Bar%d%+d_%d" % (k, sx, j), (sx * 0.25 * (j + 0.5) - 0.02, 1.35, z1 + 0.02), (sx * 0.25 * (j + 0.5) + 0.02, 3.75, z1 + 0.06), "steel_dark", bevel=0.0)


@model("train_wagon_hopper")
def train_wagon_hopper():
    L, bc, W, H = SPECS["train_wagon_hopper"]
    hz = L / 2
    flat_deck("train_wagon_hopper")
    # (the hopper: slab sides, sloped ends down to the doors, a hood over it)
    prof = [(-hz + 0.6, 3.3), (-hz + 0.6, 2.2), (-hz + 3.0, 1.0), (hz - 3.0, 1.0), (hz - 0.6, 2.2), (hz - 0.6, 3.3)]
    prism("Hopper", prof, W - 0.1, "train_hopper", axis="x", bevel=0.03)
    prism("Hood", [(-1.35, 3.3), (1.35, 3.3), (0.9, H), (-0.9, H)], L - 1.4, "train_hopper", axis="z", bevel=0.03)
    for z in (-1.2, 1.2):
        prism("Chute%+.0f" % z, [(-0.9, 1.0), (0.9, 1.0), (0.45, 0.6), (-0.45, 0.6)], 0.8, "train_hopper", axis="z", centre=z, bevel=0.02)
    for sx in (-1, 1):
        kit.torus("BrakeWheel%+d" % sx, (sx * 1.25, 1.0, -hz + 1.0), 0.25, 0.02, "steel_dark", axis="x", verts=20)
        handrail("HoodRail%+d" % sx, [(sx * 1.0, 3.45, -hz + 0.9), (sx * 1.0, 3.45, hz - 0.9)])
    for k in range(6):
        z = -hz + 1.5 + k * (L - 3.0) / 5
        for sx in (-1, 1):
            span("Post%d%+d" % (k, sx), (sx * (W / 2 - 0.05) - 0.04, 1.2, z - 0.06), (sx * (W / 2 - 0.05) + 0.04, 3.3, z + 0.06), "train_wagon_frame", bevel=0.0)


@model("train_wagon_tank")
def train_wagon_tank():
    L, bc, W, H = SPECS["train_wagon_tank"]
    hz = L / 2
    flat_deck("train_wagon_tank")
    cyl("Tank", (0.0, 2.45, 0.0), 1.3, L - 1.8, "train_tank", axis="z", verts=32, bevel=0.05)
    for z in (-(L - 1.8) / 2, (L - 1.8) / 2):
        cyl("End%+.0f" % z, (0.0, 2.45, z), 1.26, 0.2, "train_tank", axis="z", verts=32, bevel=0.06)
    cyl("Dome", (0.0, 3.75, 0.0), 0.4, 0.2, "train_tank", verts=20)
    span("Walk", (-0.5, 3.72, -1.2), (0.5, 3.76, 1.2), "train_wagon_frame", bevel=0.0)
    for z in (-3.5, 3.5):
        kit.torus("Band%+.0f" % z, (0.0, 2.45, z), 1.31, 0.03, "train_wagon_frame", axis="z", verts=32)
    for sx in (-1, 1):
        handrail("Ladder%+d" % sx, [(sx * 1.33, 1.25, -0.25), (sx * 1.33, 3.0, -0.25)])
        handrail("Ladder%+db" % sx, [(sx * 1.33, 1.25, 0.25), (sx * 1.33, 3.0, 0.25)])
        for j in range(6):
            handrail("Rung%+d_%d" % (sx, j), [(sx * 1.33, 1.4 + j * 0.3, -0.25), (sx * 1.33, 1.4 + j * 0.3, 0.25)])
        handrail("WalkRail%+d" % sx, [(sx * 0.5, 3.76, -1.2), (sx * 0.5, 4.1, -1.2), (sx * 0.5, 4.1, 1.2), (sx * 0.5, 3.76, 1.2)])
        span("Valve%+d" % sx, (sx * 0.4 - 0.12, 1.0, -0.15), (sx * 0.4 + 0.12, 1.15, 0.15), "steel_dark", bevel=0.02)
        span("Hazard%+d" % sx, (sx * 1.31 - 0.01, 2.6, 2.0), (sx * 1.31 + 0.01, 3.0, 2.6), "train_warning", bevel=0.0)


# --- Heritage stock ---------------------------------------------------------------------------------------------------

def driving_wheels(name, zs, r, frame_y=0.9):
    for k, z in enumerate(zs):
        for sx in (-1, 1):
            cyl("%s_%d%+d" % (name, k, sx), (sx * (GAUGE / 2 + 0.05), r, z), r, 0.12, "train_livery_a", axis="x", verts=28)
            cyl("%s_%d%+dT" % (name, k, sx), (sx * (GAUGE / 2 + 0.11), r, z), r * 0.92, 0.02, "train_wheel", axis="x", verts=28)
            cyl("%s_%d%+dH" % (name, k, sx), (sx * (GAUGE / 2 + 0.13), r, z), 0.12, 0.05, "steel_dark", axis="x", verts=12)
    # (the coupling rods along them)
    for sx in (-1, 1):
        span("%s_Rod%+d" % (name, sx), (sx * (GAUGE / 2 + 0.17) - 0.02, r - 0.05 + 0.25, zs[0] - 0.1), (sx * (GAUGE / 2 + 0.17) + 0.02, r + 0.05 + 0.25, zs[-1] + 0.1), "train_rod", bevel=0.01)


def steam_engine(pid, tank):
    L, bc, W, H = SPECS[pid]
    hz = L / 2
    r = 0.7 if tank else 0.95
    # (the frames, the footplate, the buffer beams)
    span("Frames", (-0.65, 0.35, -hz + 0.5), (0.65, 1.3, hz - 0.5), "paint_black", bevel=0.02)
    span("Footplate", (-W / 2 + 0.05, 1.3, -hz + 0.3), (W / 2 - 0.05, 1.4, hz - 0.3), "train_livery_a", bevel=0.01)
    span("BeamF", (-W / 2 + 0.1, 0.85, hz - 0.35), (W / 2 - 0.1, 1.35, hz - 0.2), "train_buffer_red", bevel=0.01)
    span("BeamR", (-W / 2 + 0.1, 0.85, -hz + 0.2), (W / 2 - 0.1, 1.35, -hz + 0.35), "train_buffer_red" if tank else "paint_black", bevel=0.01)
    buffers("BufF", hz)
    if tank:
        buffers("BufR", -hz)
    # (the boiler and smokebox, its chimney and domes)
    bz0, bz1 = -hz + (2.6 if tank else 3.4), hz - 1.4
    cyl("Boiler", (0.0, 2.35, (bz0 + bz1) / 2), 0.78 if tank else 0.85, bz1 - bz0, "train_livery_a", axis="z", verts=32, bevel=0.01)
    cyl("Smokebox", (0.0, 2.35, bz1 + 0.45), 0.82 if tank else 0.9, 0.9, "paint_black", axis="z", verts=32, bevel=0.02)
    cyl("SmokeDoor", (0.0, 2.35, bz1 + 0.92), 0.7, 0.06, "paint_black", axis="z", verts=32, bevel=0.02)
    cyl("Chimney", (0.0, 3.55 if tank else 3.6, bz1 + 0.45), 0.24, 0.6 if tank else 0.55, "paint_black", verts=20, bevel=0.01)
    cyl("ChimneyLip", (0.0, H - 0.04, bz1 + 0.45), 0.3, 0.08, "paint_black", verts=20, bevel=0.01)
    cyl("Dome", (0.0, 3.25, (bz0 + bz1) / 2 + 0.3), 0.32, 0.35, "train_brass", verts=20, bevel=0.06)
    cyl("Safety", (0.0, 3.25, bz0 + 0.5), 0.15, 0.25, "train_brass", verts=14, bevel=0.03)
    for k in range(3):
        z = bz0 + 0.4 + k * (bz1 - bz0 - 0.8) / 2
        kit.torus("Band%d" % k, (0.0, 2.35, z), 0.79 if tank else 0.86, 0.02, "train_brass", axis="z", verts=32)
    # (the cylinders each side at the front)
    for sx in (-1, 1):
        cyl("Cyl%+d" % sx, (sx * 0.95, 1.0, hz - 1.3), 0.3, 1.0, "train_livery_a", axis="z", verts=20, bevel=0.02)
    if tank:
        # (the side tanks along the boiler, the bunker behind the cab)
        for sx in (-1, 1):
            span("SideTank%+d" % sx, (sx * 0.8 if sx > 0 else -W / 2 + 0.05, 1.4, bz0 + 0.3), (W / 2 - 0.05 if sx > 0 else -0.8, 2.75, bz1 - 0.3), "train_livery_a", bevel=0.03)
            span("TankBand%+d" % sx, (sx * (W / 2 - 0.05) - 0.01, 2.0, bz0 + 0.5), (sx * (W / 2 - 0.05) + 0.01, 2.05, bz1 - 0.5), "train_livery_b", bevel=0.0)
        span("Bunker", (-W / 2 + 0.05, 1.4, -hz + 0.35), (W / 2 - 0.05, 2.7, -hz + 1.2), "train_livery_a", bevel=0.03)
    # (the cab: sides, a roof, spectacle windows)
    cz0, cz1 = (-hz + 1.2, bz0) if tank else (-hz + 0.3, bz0)
    for sx in (-1, 1):
        span("CabSide%+d" % sx, (sx * (W / 2 - 0.08) - 0.04, 1.4, cz0), (sx * (W / 2 - 0.08) + 0.04, 3.05, cz1), "train_livery_a", bevel=0.01)
        span("CabBand%+d" % sx, (sx * (W / 2 - 0.08) - 0.05, 2.0, cz0 + 0.1), (sx * (W / 2 - 0.08) + 0.05, 2.05, cz1 - 0.1), "train_livery_b", bevel=0.0)
    span("CabFront", (-W / 2 + 0.05, 1.4, cz1 - 0.05), (W / 2 - 0.05, 3.3, cz1 + 0.05), "train_livery_a", bevel=0.01)
    for sx in (-1, 1):
        cyl("Spectacle%+d" % sx, (sx * 0.85, 2.95, cz1 + 0.07), 0.18, 0.03, "train_cab_glass", axis="z", verts=16, bevel=0.0)
    prism("CabRoof", [(-W / 2, 3.3), (W / 2, 3.3), (W / 2 - 0.3, H - 0.25), (-W / 2 + 0.3, H - 0.25)], cz1 - cz0 + 0.3, "paint_black", axis="z", centre=(cz0 + cz1) / 2, bevel=0.02)
    # (the wheels: six coupled, a leading bogie on the big one)
    if tank:
        driving_wheels("Drive", [hz - 3.0, hz - 5.0, hz - 7.0], r)
    else:
        driving_wheels("Drive", [hz - 4.4, hz - 6.6, hz - 8.8], r)
        bogie("Pony", hz - 1.6, wb=1.6, r=0.45, frame="paint_black")
    # (the lamps: on the buffer beam's top at the front; a red one at the back)
    lamps("LampF", hz - 0.15, W - 0.2, y=1.6, spread=0.9)
    lamps("LampR", -hz + 0.15, W - 0.2, y=1.6, spread=0.9)
    # (the fittings: whistle and its pipe, sandboxes, handrails along the boiler, the smokebox's number plate and door
    # handles, lamp irons, cab side windows and handrails, connecting rods to the middle wheels, the injector pipes)
    cyl("Whistle", (0.0, 3.55, bz0 + 0.15), 0.05, 0.3, "train_brass", verts=10, radius2=0.07)
    cyl("Whistle2", (0.18, 3.5, bz0 + 0.15), 0.04, 0.22, "train_brass", verts=10)
    for sx in (-1, 1):
        handrail("BoilerRail%+d" % sx, [(sx * 0.62, 2.85, bz0 + 0.1), (sx * 0.62, 2.85, bz1 + 0.6)])
        handrail("CabRail%+d" % sx, [(sx * (W / 2 - 0.02), 1.5, cz1 - 0.15), (sx * (W / 2 + 0.04), 1.5, cz1 - 0.15), (sx * (W / 2 + 0.04), 2.9, cz1 - 0.15), (sx * (W / 2 - 0.02), 2.9, cz1 - 0.15)])
        span("Sandbox%+d" % sx, (sx * 0.75 - 0.15, 1.4, hz - 2.4), (sx * 0.75 + 0.15, 1.75, hz - 2.0), "train_livery_a", bevel=0.03)
        side_window("CabSideW%+d" % sx, sx * (W / 2 - 0.04), cz0 + 0.35 if not tank else cz0 + 0.25, cz1 - 0.35, 2.35, 2.85, "train_cab_glass", sx=sx)
        kit.tube("Injector%+d" % sx, [(sx * 0.7, 1.45, cz1 - 0.1), (sx * 0.86, 1.7, bz0 + 0.6), (sx * 0.86, 1.6, bz0 + 1.5), (sx * 0.65, 1.4, bz0 + 2.0)], 0.035, "train_brass", sides=8)
        span("ConRod%+d" % sx, (sx * (GAUGE / 2 + 0.23) - 0.025, r + 0.12, hz - 1.3), (sx * (GAUGE / 2 + 0.23) + 0.025, r + 0.22, (hz - 4.0 if tank else hz - 5.4)), "train_rod", bevel=0.01)
        cyl("Crosshead%+d" % sx, (sx * (GAUGE / 2 + 0.23), 1.0, hz - 1.95), 0.08, 0.25, "train_rod", axis="z", verts=10)
    cyl("NumPlate", (0.0, 2.85, bz1 + 0.95), 0.16, 0.02, "train_brass", axis="z", verts=16, bevel=0.0)
    for k, ang in enumerate((-35, 35)):
        span("DoorDart%d" % k, (-0.03, 2.33, bz1 + 0.94), (0.03, 2.37, bz1 + 1.02), "steel", bevel=0.0)
    for x in (-0.8, 0.0, 0.8):
        span("Iron%+.1f" % x, (x - 0.03, 1.4, hz - 0.3), (x + 0.03, 1.58, hz - 0.22), "paint_black", bevel=0.0)
    span("TopIron", (-0.03, 3.3, bz1 + 0.94), (0.03, 3.45, bz1 + 1.0), "paint_black", bevel=0.0)


@model("train_steam_tank")
def train_steam_tank():
    steam_engine("train_steam_tank", tank=True)


@model("train_steam_big")
def train_steam_big():
    steam_engine("train_steam_big", tank=False)


@model("train_steam_tender")
def train_steam_tender():
    L, bc, W, H = SPECS["train_steam_tender"]
    hz = L / 2
    span("Frames", (-0.65, 0.35, -hz + 0.4), (0.65, 1.3, hz - 0.4), "paint_black", bevel=0.02)
    span("Tank", (-W / 2 + 0.05, 1.3, -hz + 0.3), (W / 2 - 0.05, 3.0, hz - 0.5), "train_livery_a", bevel=0.04)
    for sx in (-1, 1):
        span("Band%+d" % sx, (sx * (W / 2 - 0.05) - 0.01, 2.0, -hz + 0.5), (sx * (W / 2 - 0.05) + 0.01, 2.05, hz - 0.7), "train_livery_b", bevel=0.0)
    span("Coal", (-W / 2 + 0.25, 3.0, -0.5), (W / 2 - 0.25, H, hz - 0.6), "train_coal", bevel=0.1)
    span("Bulkhead", (-W / 2 + 0.05, 3.0, -0.6), (W / 2 - 0.05, H - 0.1, -0.5), "train_livery_a", bevel=0.01)
    buffers("BufR", -hz)
    lamps("LampR", -hz + 0.15, W - 0.2, y=1.6, spread=0.9)
    driving_wheels("Wheel", [-2.4, 0.0, 2.4], 0.55)


@model("train_coach_old")
def train_coach_old():
    L, bc, W, H = SPECS["train_coach_old"]
    hz = L / 2
    shell("Lower", W, -hz + 0.3, hz - 0.3, 1.05, 2.05, 2.06, "train_livery_a")
    shell("Upper", W, -hz + 0.3, hz - 0.3, 2.05, 3.1, H - 0.05, "train_livery_b")
    span("Roof", (-W / 2 + 0.2, H - 0.12, -hz + 0.3), (W / 2 - 0.2, H, hz - 0.3), "train_roof_old", bevel=0.05)
    window_band("Win", W, -hz + 1.0, hz - 1.0, 2.15, 2.85, 8, gap=0.35)
    for sx in (-1, 1):
        for z in (-hz + 0.7, hz - 0.7):
            span("Door%+d%+.0f" % (sx, z), (sx * W / 2 - 0.02, 1.15, z - 0.35), (sx * W / 2 + 0.02, 3.0, z + 0.35), "train_livery_a", bevel=0.0)
            span("Handle%+d%+.0f" % (sx, z), (sx * W / 2 - 0.05, 1.9, z + 0.18), (sx * W / 2 + 0.05, 1.95, z + 0.28), "train_brass", bevel=0.0)
    for z in (hz, -hz):
        f = 1 if z > 0 else -1
        span("Gangway%+d" % f, (-0.5, 1.2, z - f * 0.35), (0.5, 3.0, z - f * 0.25 + f * 0.1), "rubber", bevel=0.03)
        buffers("Buf%+d" % f, z)
    bogie("BogieF", bc, wb=2.6, frame="paint_black")
    bogie("BogieR", -bc, wb=2.6, frame="paint_black")
    for k in range(2):
        span("Battery%d" % k, (-1.0, 0.55, -3.0 + k * 3.5), (1.0, 1.0, -1.2 + k * 3.5), "paint_black", bevel=0.02)
    # (roof vents, the footboards along the bottom, door ventilators, the end steps and the tail lamp's iron)
    for j in range(8):
        z = -hz + 1.6 + j * (L - 3.2) / 7
        cyl("Vent%d" % j, (0.0, H + 0.04, z), 0.11, 0.32, "train_roof_old", axis="z", verts=12, bevel=0.02)
    for sx in (-1, 1):
        span("Footboard%+d" % sx, (sx * (W / 2 + 0.05) - 0.12, 0.98, -hz + 0.6), (sx * (W / 2 + 0.05) + 0.12, 1.02, hz - 0.6), "paint_black", bevel=0.0)
        for z in (-hz + 0.7, hz - 0.7):
            side_window("Droplight%+d%+.0f" % (sx, z), sx * (W / 2 + 0.02), z - 0.22, z + 0.22, 2.3, 2.85, sx=sx)
            span("DoorVent%+d%+.0f" % (sx, z), (sx * (W / 2 + 0.03) - 0.01, 2.92, z - 0.25), (sx * (W / 2 + 0.03) + 0.01, 2.98, z + 0.25), "train_brass", bevel=0.0)
        for j in range(3):
            span("BodyLine%+d_%d" % (sx, j), (sx * (W / 2) - 0.006, 1.2 + j * 0.003, -hz + 0.35), (sx * (W / 2) + 0.006 * sx, 1.22 + j * 0.003, hz - 0.35), "train_brass" if j == 1 else "paint_black", bevel=0.0)
    lamps("LampR", -hz - 0.05, W, y=1.7, spread=1.2)


@model("train_diesel_old")
def train_diesel_old():
    """An old diesel with a cab each end: a flat face with two screens, the headcode box between them under the roof's
    dome, a yellow end, horns on the roof, louvres along the sides."""
    L, bc, W, H = SPECS["train_diesel_old"]
    hz = L / 2
    lower, thin, win, roof, floor = rows()
    zc = hz - 0.9
    body = [ring(-zc, W / 2, 1.2, 2.35, 3.15, H, tuck=0.05), ring(zc, W / 2, 1.2, 2.35, 3.15, H, tuck=0.05)]
    loft("Body", body, lambda k, j: "train_under" if j in floor else "train_roof" if j in roof and 6 <= j <= NR + 2 else "train_livery_a")
    for f in (1, -1):
        rs = []
        N = 8
        for k in range(N + 1):
            t = k / N
            z = f * (zc + 0.6 * t)
            hw = W / 2 - 0.1 * smooth((t - 0.4) / 0.6)
            ytop = H - (H - 3.15) * smooth((t - 0.2) / 0.8) ** 1.4
            rs.append(ring(z, hw, 1.2, 2.35, 3.15, ytop, tuck=0.05))
        if f < 0:
            rs = list(reversed(rs))

        def m(k, j, f=f, N=N):
            kk = k if f > 0 else N - 1 - k
            if j in floor:
                return "paint_black"
            if j in roof:
                return "train_roof" if 6 <= j <= NR + 2 and kk < 5 else "train_livery_a"
            return "train_warning" if kk >= 6 else "train_livery_a"
        loft("Cab%+d" % f, rs, m, cap_front="train_warning" if f > 0 else None, cap_back="train_warning" if f < 0 else None)
        zf = f * (zc + 0.6)
        o = f * 0.012
        # (the two screens and their frames, the headcode box, the small marker lamps, the lamp irons)
        for sx in (-1, 1):
            x0, x1 = (0.12, 1.1) if sx > 0 else (-1.1, -0.12)
            span("ScreenF%+d%+d" % (f, sx), (x0 - 0.06, 2.45, zf - o), (x1 + 0.06, 3.12, zf + 2 * o), "paint_black", bevel=0.03)
            span("Screen%+d%+d" % (f, sx), (x0, 2.5, zf), (x1, 3.07, zf + 2.6 * o), "train_cab_glass", bevel=0.02)
            kit.tube("Wiper%+d%+d" % (f, sx), [((x0 + x1) / 2 - 0.2, 2.55, zf + 3 * o), ((x0 + x1) / 2 + 0.1, 3.0, zf + 3 * o)], 0.012, "steel_dark", sides=6)
            cyl("Lamp%+d%+dH" % (f, sx), (sx * 0.85, 1.75, zf + o), 0.09, 0.05, "train_lamp_head", axis="z", verts=16, bevel=0.0)
            cyl("Lamp%+d%+dT" % (f, sx), (sx * 1.1, 1.75, zf + o), 0.06, 0.05, "train_lamp_tail", axis="z", verts=12, bevel=0.0)
            cyl("Lamp%+d%+dR" % (f, sx), (sx * 0.85, 1.75, zf), 0.12, 0.05, "paint_black", axis="z", verts=16, bevel=0.0)
            handrail("Grab%+d%+d" % (f, sx), [(sx * 1.3, 1.5, zf), (sx * 1.3, 1.5, zf + 4 * o), (sx * 1.3, 2.4, zf + 4 * o), (sx * 1.3, 2.4, zf)])
        span("HeadBox%+d" % f, (-0.55, 2.0, zf - o), (0.55, 2.35, zf + 3 * o), "paint_black", bevel=0.02)
        span("HeadText%+d" % f, (-0.48, 2.05, zf + 2.5 * o), (0.48, 2.3, zf + 3.3 * o), "train_headcode", bevel=0.0)
        for sx in (-1, 1):
            cyl("Horn%+d%+d" % (f, sx), (sx * 0.28, H + 0.05, f * (zc + 0.1)), 0.06, 0.4, "steel_dark", axis="z", verts=10, radius2=0.1)
        span("Lamp%+dIron" % f, (-0.05, 1.45, zf - o), (0.05, 1.6, zf + 6 * o), "steel_dark", bevel=0.0)
        buffers("Buf%+d" % f, f * hz, beam="paint_black")
        # (the cab's side window, the door and its handrails)
        for sx in (-1, 1):
            side_window("CabWin%+d%+d" % (f, sx), sx * W / 2, f * (zc - 0.45) - 0.35, f * (zc - 0.45) + 0.35, 2.4, 3.0, "train_cab_glass", sx=sx)
            zd = f * (zc - 1.45)
            span("CabDoor%+d%+d" % (f, sx), (sx * (W / 2 - 0.01), 1.3, zd - 0.38), (sx * (W / 2 + 0.014), 3.1, zd + 0.38), "train_livery_a", bevel=0.01)
            side_window("CabDoorW%+d%+d" % (f, sx), sx * (W / 2 + 0.015), zd - 0.25, zd + 0.25, 2.45, 2.95, "train_cab_glass", sx=sx)
            for dz in (-0.45, 0.45):
                handrail("Rail%+d%+d%+.2f" % (f, sx, dz), [(sx * W / 2, 1.5, zd + dz), (sx * (W / 2 + 0.06), 1.5, zd + dz), (sx * (W / 2 + 0.06), 2.9, zd + dz), (sx * W / 2, 2.9, zd + dz)])
    for sx in (-1, 1):
        span("Band%+d" % sx, (sx * (W / 2 - 0.006), 1.62, -zc), (sx * (W / 2 + 0.01), 1.76, zc), "train_livery_b", bevel=0.0)
        for k in range(6):
            z = -zc + 2.2 + k * 2.05
            span("LouvreF%+d_%d" % (sx, k), (sx * (W / 2 - 0.02), 2.25, z - 0.05), (sx * (W / 2 + 0.024), 3.0, z + 1.45), "paint_black", bevel=0.01)
            for j in range(8):
                span("Louvre%+d_%d_%d" % (sx, k, j), (sx * (W / 2 - 0.01), 2.3 + j * 0.085, z), (sx * (W / 2 + 0.04), 2.33 + j * 0.085, z + 1.4), "train_livery_a", bevel=0.0)
        span("Sole%+d" % sx, (sx * (W / 2 - 0.06) - 0.06, 1.0, -hz + 0.3), (sx * (W / 2 - 0.06) + 0.06, 1.22, hz - 0.3), "paint_black", bevel=0.01)
    for zc2 in (bc, -bc):
        for sx in (-1, 1):
            prof = [(zc2 - 2.3, 0.55), (zc2 - 2.3, 0.95), (zc2 + 2.3, 0.95), (zc2 + 2.3, 0.55), (zc2 + 1.0, 0.45), (zc2 - 1.0, 0.45)]
            prism("Frame%+.0f%+d" % (zc2, sx), prof, 0.16, "paint_black", axis="x", centre=sx * 1.05, bevel=0.02)
            for dz in (-1.7, 0.0, 1.7):
                cyl("W%+.0f%+.1f%+d" % (zc2, dz, sx), (sx * (GAUGE / 2 + 0.03), 0.5, zc2 + dz), 0.5, 0.13, "train_wheel", axis="x", verts=24)
                span("Box%+.0f%+.1f%+d" % (zc2, dz, sx), (sx * 1.05 - 0.13, 0.38, zc2 + dz - 0.17), (sx * 1.05 + 0.13, 0.66, zc2 + dz + 0.17), "paint_black", bevel=0.02)
                for j in range(4):
                    span("Leaf%+.0f%+.1f%+d_%d" % (zc2, dz, sx, j), (sx * 1.05 - 0.1, 0.7 + j * 0.05, zc2 + dz - 0.55 + j * 0.08), (sx * 1.05 + 0.1, 0.73 + j * 0.05, zc2 + dz + 0.55 - j * 0.08), "train_spring", bevel=0.0)
    span("Tank", (-1.1, 0.55, -2.2), (1.1, 1.2, 2.2), "paint_black", bevel=0.06)
    for sx in (-1, 1):
        cyl("Battery%+d" % sx, (sx * 0.7, 0.8, 3.0), 0.25, 1.1, "paint_black", axis="z", verts=14)


def main():
    import bpy  # noqa: F401
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    spec = json.load(open(os.path.join(ROOT, "Data", "Props", "materials.json")))
    kit.set_materials(spec["materials"])
    want = [a for a in args if not a.startswith("-")] or sorted(MODELS)
    os.makedirs(OUT, exist_ok=True)
    for pid in want:
        kit.reset()
        MODELS[pid]()
        obj = kit.finish("SM_" + pid)
        lo, hi = kit.bounds(obj)
        path = os.path.join(OUT, pid + ".glb")
        kit.export(path)
        print("%s: %d faces, %.2f x %.2f x %.2f m -> %s" % (pid, len(obj.data.polygons), hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], path))


if __name__ == "__main__":
    main()
