"""The tools in your hands, modelled in detail (no Blender: Tools/props/sdf_kit.py's distance fields,
meshed with marching cubes): the combination spanner, the wire brush, the angle grinder, the aerosol
can, the gravity-feed spray gun and the pressure washer's gun and lance. They replace
TorqueToolModels' primitives (Torque Shed's tool_models.gd shapes), same tools, same jobs.

    python Tools/props/make_hand_tools.py [--glb DIR] [ids...]

Writes Data/Tools/<id>.tmesh (the game reads them at run time, no editor import) and
Data/Tools/tools.json: each tool's material slots (the shed props' materials, Data/Props/materials.json
keys, as MI_Prop_<key>; "paint" is tinted the spray colour), where the work comes off it (nozzle),
how you hold it in first person (hold) and where each hand grips it (grips: the bar through the
fist, pinky to index, the way the knuckles point, the bar's radius, fingers on a trigger).

Frame: Unreal's, millimetres here and centimetres in the files: x forward (away from you), y right,
z up, the right hand's grip at the origin. No makers' names, logos or markings on any of them.
"""
import argparse
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sdf_kit import (Model, box, circle2, cut, cylinder_z, extrude, frame, intersect, lathe, offset,  # noqa: E402
                     polygon2, rot_matrix, segment, smooth_union, sphere, torus_z, turned, union)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "Data", "Tools")

# Local axes for a 2D outline drawn in (y, z) and pushed along x, and the like.
R_YZ = np.array([[0, 0, 1], [1, 0, 0], [0, 1, 0]], dtype=float)   # u->y, v->z, w->x
R_X = np.array([[0, 0, 1], [0, 1, 0], [-1, 0, 0]], dtype=float)   # local z -> x (lathe round x)
R_Y = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], dtype=float)   # local z -> y (lathe round y)


def along(axis):
    """A turn taking local z onto axis."""
    a = np.asarray(axis, float)
    a = a / np.linalg.norm(a)
    z = np.array([0.0, 0.0, 1.0])
    c = float(z @ a)
    if c > 0.9999:
        return np.eye(3)
    if c < -0.9999:
        return rot_matrix((1, 0, 0), 180)
    ax = np.cross(z, a)
    return rot_matrix(ax, math.degrees(math.acos(c)))


def hexagon2(apothem, turn_deg=0.0):
    """A regular hexagon (2D), flats `apothem` from its middle."""
    k = np.array([-0.866025404, 0.5])
    kz = 0.577350269
    c, s = math.cos(math.radians(turn_deg)), math.sin(math.radians(turn_deg))
    def f(q):
        q = np.stack([c * q[:, 0] + s * q[:, 1], -s * q[:, 0] + c * q[:, 1]], axis=1)
        q = np.abs(q)
        d = np.minimum(q @ k, 0.0)
        q = q - 2.0 * d[:, None] * k[None, :]
        cx = np.clip(q[:, 0], -kz * apothem, kz * apothem)
        r = np.stack([q[:, 0] - cx, q[:, 1] - apothem], axis=1)
        return np.linalg.norm(r, axis=1) * np.sign(r[:, 1])
    return f


def cut2(f, *gs):
    """A 2D outline with others cut out of it."""
    def h(q):
        d = f(q)
        for g in gs:
            d = np.maximum(d, -g(q))
        return d
    return h


def poly2(pts, round_=0.0):
    f = polygon2(pts)
    return (lambda q: f(q) - round_) if round_ else f


def grooves2(r, zs, w):
    """Round grooves in a lathe outline: circles of radius w at (r, z)."""
    return [circle2(w, (r, z)) for z in zs]


def union2(*fs):
    return lambda q: np.min(np.stack([f(q) for f in fs]), axis=0)


def box2(cx, cy, hx, hy, turn_deg=0.0, round_=0.0):
    c, s = math.cos(math.radians(turn_deg)), math.sin(math.radians(turn_deg))
    def f(q):
        x, y = q[:, 0] - cx, q[:, 1] - cy
        u, v = c * x + s * y, -s * x + c * y
        dx, dy = np.abs(u) - (hx - round_), np.abs(v) - (hy - round_)
        return np.hypot(np.maximum(dx, 0), np.maximum(dy, 0)) + np.minimum(np.maximum(dx, dy), 0) - round_
    return f


def bar(a, b, half_w, half_t, round_, wdir):
    """A rounded bar from a to b, half_w wide along wdir (made square to the bar), half_t thick."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    L = np.linalg.norm(b - a)
    zx = (b - a) / L
    w = np.asarray(wdir, float)
    w = w - zx * (w @ zx)
    w /= np.linalg.norm(w)
    t = np.cross(zx, w)
    R = np.stack([w, t, zx], axis=1)   # local x = w, y = t, z = along
    return frame(box((half_w, half_t, L * 0.5), round_), (a + b) * 0.5, R)


def ring_slots(r_in, r_out, z0, z1, count, width, phase=0.0):
    """Radial slots round z (vents): boxes pointing out from the axis."""
    parts = []
    for i in range(count):
        a = 2 * math.pi * i / count + phase
        d = np.array([math.cos(a), math.sin(a), 0.0])
        mid = d * (r_in + r_out) * 0.5 + np.array([0, 0, (z0 + z1) * 0.5])
        R = np.stack([d, np.array([-d[1], d[0], 0.0]), np.array([0, 0, 1.0])], axis=1)
        parts.append(frame(box(((r_out - r_in) * 0.5, width * 0.5, (z1 - z0) * 0.5), min(width * 0.45, 0.8)), mid, R))
    return union(*parts)


def lathe_poly(pts, round_=0.0):
    """A solid of revolution round local z from an outline in (radius, height), its edges rounded."""
    f2 = polygon2(pts)
    return offset(lathe(f2), round_) if round_ else lathe(f2)


def wires(model, material, rng, roots, direction, length, spread, r_wire=0.17, per=14, tuft_r=1.6):
    """A tuft of steel wires at each root, springing out along direction and splaying a little."""
    direction = np.asarray(direction, float)
    direction /= np.linalg.norm(direction)
    u = np.cross(direction, [0.0, 0.0, 1.0] if abs(direction[2]) < 0.9 else [1.0, 0.0, 0.0])
    u /= np.linalg.norm(u)
    w = np.cross(direction, u)
    V, N, T = [], [], []
    sides = 5
    for root in roots:
        for _ in range(per):
            a, rr = rng.uniform(0, 2 * math.pi), tuft_r * math.sqrt(rng.uniform(0, 1))
            off = (u * math.cos(a) + w * math.sin(a)) * rr
            p0 = np.asarray(root, float) + off - direction * 2.0
            L = length + rng.uniform(-1.6, 1.6)
            tip = p0 + direction * L + off * spread + (u * rng.normal() + w * rng.normal()) * 0.35
            ax = tip - p0
            ax /= np.linalg.norm(ax)
            a1 = np.cross(ax, u)
            a1 /= np.linalg.norm(a1)
            a2 = np.cross(ax, a1)
            base = len(V)
            for k in range(sides):
                t = 2 * math.pi * k / sides
                n = a1 * math.cos(t) + a2 * math.sin(t)
                V += [p0 + n * r_wire, tip + n * r_wire * 0.8]
                N += [n, n]
            for k in range(sides):
                i0, i1 = base + 2 * k, base + 2 * ((k + 1) % sides)
                T += [[i0, i1, i1 + 1], [i0, i1 + 1, i0 + 1]]
            c = len(V)
            V.append(tip + ax * r_wire * 0.5)
            N.append(ax)
            for k in range(sides):
                T.append([base + 2 * k + 1, base + 2 * ((k + 1) % sides) + 1, c])
    V, N, T = np.array(V), np.array(N), np.array(T, dtype=np.int64)
    fn = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
    flip = np.einsum("ij,ij->i", fn, N[T[:, 0]] + N[T[:, 1]] + N[T[:, 2]]) < 0
    T[flip] = T[flip][:, [0, 2, 1]]
    model.add_mesh(material, V, N, T)


# --- The tools ---------------------------------------------------------------------------------------


def spanner():
    """A 13 mm combination spanner, 170 mm long: the 12-point ring up (offset 15 degrees), the open
    jaw down at 15 degrees, an I-beam shank, satin chrome. Held like a hammer round the shank."""
    m = Model("tool_wrench")
    # The shank's outline in (y, z), widening into both heads.
    side = [(-62, 8.6), (-40, 7.4), (-10, 6.8), (20, 6.3), (44, 6.6), (56, 8.2)]
    outline = [(w, z) for z, w in side] + [(-w, z) for z, w in reversed(side)]
    shank = frame(extrude(polygon2(outline), -2.9, 2.9, 1.1), (0, 0, 0), R_YZ)
    # I-beam: a shallow channel down each face.
    inner = [(z, w - 2.4) for z, w in side[1:-1]]
    ch = [(w, z) for z, w in inner] + [(-w, z) for z, w in reversed(inner)]
    chan = union(frame(extrude(polygon2(ch), 2.15, 6.0, 0.0), (0, 0, 0), R_YZ),
                 frame(extrude(polygon2(ch), -6.0, -2.15, 0.0), (0, 0, 0), R_YZ))
    shank = cut(shank, chan, k=0.9)
    # The ring end: offset 15 degrees out of the shank's plane, a 12-point hole for a 13 mm nut.
    ring_c = np.array([0.0, 0.0, 64.0])
    tilt = rot_matrix((0, 1, 0), -15)
    ring_out = frame(extrude(circle2(12.2), -4.1, 4.1, 1.3), ring_c, tilt @ R_YZ)
    twelve = union2(hexagon2(6.55), hexagon2(6.55, 30.0))
    ring_hole = frame(extrude(twelve, -9.0, 9.0, 0.0), ring_c, tilt @ R_YZ)
    # A chamfer into the hole from each face.
    sink = union(lathe_poly([(0, -5.0), (8.3, -5.0), (6.0, -2.9), (0, -2.9)]), lathe_poly([(0, 2.9), (6.0, 2.9), (8.3, 5.0), (0, 5.0)]))
    ring = cut(ring_out, ring_hole, frame(sink, ring_c, tilt @ R_X), k=0.35)
    # The open end: a rounded head with the jaw cut through it at 15 degrees.
    jaw_c = np.array([0.0, 0.0, -80.0])
    head2 = union2(circle2(15.6), box2(0, 10.0, 9.0, 10.0, 0.0, 4.0))
    jaw2 = union2(box2(0, -14.0, 6.6, 14.0, 0.0, 0.0), circle2(6.6))
    jaw_turn = rot_matrix((1, 0, 0), 15)
    head = frame(extrude(head2, -3.3, 3.3, 1.1), jaw_c, R_YZ)
    jaw = frame(extrude(jaw2, -9, 9, 0.0), jaw_c, jaw_turn @ R_YZ)
    head = cut(head, jaw, k=0.5)
    whole = smooth_union(3.5, shank, ring, head)
    m.add("chrome", whole, (-12, -18, -97), (12, 18, 78), step=0.28, tris=36000)
    return m, {
        "nozzle": [0.0, 0.0, 64.0],   # the ring's middle: where it meets the bolt
        "grips": {"right": {"at": [0, 0, -18], "axis": [0, 0, 1], "aim": [1, 0, 0], "radius": 4.6}},
        "hold": {"at": [33, 16, -15], "x": [0.94, 0.12, -0.32], "z": [0.32, -0.16, 0.93]},
    }


def brush():
    """A wire brush: a shaped beech handle, a hanging hole, and four rows of steel-wire tufts set in
    the stock, bristles forward."""
    m = Model("tool_brush")
    rz = np.array([[-98, 10.5], [-92, 13.0], [-70, 14.2], [-40, 14.6], [-5, 13.4], [22, 11.4], [36, 11.6], [48, 13.6]])
    def handle(p):
        r = np.interp(p[:, 2], rz[:, 0], rz[:, 1])
        q = np.hypot(p[:, 0] / 0.74, p[:, 1]) - r
        ends = np.maximum(-98 - p[:, 2], p[:, 2] - 52)
        d = np.stack([q, ends], axis=1)
        return np.linalg.norm(np.maximum(d + 3.0, 0), axis=1) + np.minimum(np.max(d + 3.0, axis=1), 0) - 3.0
    stock = box((7.0, 13.5, 66.0), 3.2)
    stock = frame(stock, (1.0, 0, 112.0))
    wood = smooth_union(6.0, handle, stock)
    hole = frame(cylinder_z(4.2, -30, 30, 0), (0, 0, -84), R_X)
    wood = cut(wood, hole, k=0.8)
    # Drilled holes for the tufts, on the stock's face.
    rows, cols = [-8.6, -2.9, 2.9, 8.6], [52 + i * 8.2 for i in range(15)]
    roots = []
    for i, y in enumerate(rows):
        for z in cols:
            zz = z + (4.1 if i % 2 else 0.0)
            if zz > 172:
                continue
            roots.append((8.0, y, zz))
    m.add("wood_top", wood, (-12, -16, -101), (12, 16, 182), step=0.3, tris=40000)
    wires(m, "wire", np.random.default_rng(7), roots, (1, 0, 0), 25.0, 0.45)
    return m, {
        "nozzle": [33.0, 0.0, 112.0],   # the bristles' tips, mid-head
        "grips": {"right": {"at": [0, 0, -45], "axis": [0, 0, 1], "aim": [1, 0, 0], "radius": 13.5}},
        "hold": {"at": [33, 16, -17], "x": [0.86, 0.1, -0.5], "z": [0.5, -0.12, 0.86]},
    }


def grinder():
    """A 115 mm angle grinder: the motor housing upright in your hand (a rubber grip, vents, a slide
    switch on the back), the cast gear head on top, the side handle out to the left, the guard
    behind the disc and the depressed-centre disc facing forward."""
    m = Model("tool_repair")
    # The housing as an exact surface of revolution (smooth reflections), the vents as dark slots
    # set into it, and the switch's seat on the back (-x).
    house2 = poly2([(0, -128), (19, -127), (24, -122), (27.5, -112), (29.0, -70), (29.8, 0), (31.0, 48), (31.5, 60), (0, 60)], 0.6)
    m.add_revolve("body", house2, 33, -131, 63, segs=160)
    def vent_ring(z0, z1, count, phase, r_of):
        slots = []
        for i in range(count):
            t = 2 * math.pi * i / count + phase
            d = np.array([math.cos(t), math.sin(t), 0.0])
            r = r_of((z0 + z1) * 0.5) - 0.9
            slots.append(bar(d * r + (0, 0, z0), d * r + (0, 0, z1), 1.1, 1.3, 1.0, np.cross(d, (0, 0, 1))))
        return union(*slots)
    rz = np.array([[-128, 0], [-122, 24], [-112, 27.5], [-70, 29.0], [0, 29.8], [48, 31.0], [60, 31.5]])
    r_of = lambda z: float(np.interp(z, rz[:, 0], rz[:, 1])) + 0.6   # the housing's outer surface
    vents = union(vent_ring(-111, -101, 14, 0.0, r_of), vent_ring(40, 52, 16, 0.1, r_of))
    m.add("plastic_black", vents, (-34, -34, -114), (34, 34, 55), step=0.2, tris=16000)
    seat = intersect(bar((-29.5, 0, -18), (-29.5, 0, 22), 9.0, 3.0, 2.4, (0, 1, 0)), lambda p: -(np.hypot(p[:, 0], p[:, 1]) - 26.0))
    m.add("body", seat, (-34, -11, -22), (-25, 11, 26), step=0.2, tris=6000)
    # Rubber over the grip, with ribs.
    grip2 = cut2(poly2([(26, -98), (30.2, -98), (31.2, -94), (31.6, -26), (30.4, -22), (26, -22)], 0.4),
                 *grooves2(32.3, np.arange(-88, -30, 6.5), 1.4))
    m.add_revolve("rubber", grip2, 34, -100, -20, segs=128)
    # The slide switch and its lock-off.
    m.add("plastic_black", bar((-32.4, 0, -12), (-32.4, 0, 14), 6.0, 1.8, 1.2, (0, 1, 0)), (-36, -8, -16), (-28, 8, 18), step=0.2, tris=2000)
    m.add("plastic_red", bar((-34.0, 0, 6), (-34.0, 0, 13), 4.6, 1.6, 1.0, (0, 1, 0)), (-37, -6, 3), (-31, 6, 16), step=0.2, tris=1200)
    # Rear cap, strain relief and cable.
    relief = segment((0, 0, -126), (0, 0, -172), (8.5, 5.6))
    relief = cut(relief, union(*[frame(torus_z(7.6 - (z + 130) * 0.03, 0.9), (0, 0, z)) for z in np.arange(-134, -168, -5.0)]), k=0.3)
    m.add("rubber", union(relief, segment((0, 0, -170), (0, -6, -215), 4.2)), (-10, -14, -222), (10, 10, -122), step=0.28, tris=5000)
    # The gear head: a cast block, the spindle boss forward, the side handle's bosses, ribs.
    gc = np.array([0.0, 0.0, 92.0])
    block = frame(box((25.0, 26.0, 24.0), 7.0), gc + (1, 0, -8))
    boss = frame(cylinder_z(21.0, 0, 34, 3.0), gc, R_X)
    sides = frame(cylinder_z(9.5, -32, 32, 2.0), gc + (4, 0, 0), R_Y)
    neck = lathe_poly([(0, 56), (31.5, 56), (29, 72), (0, 72)], 1.0)
    head = smooth_union(4.0, block, boss, sides, neck)
    head = cut(head, union(*[bar((-26, y, 70), (-26, y, 112), 1.0, 3.0, 0.8, (0, 1, 0)) for y in (-14, -7, 0, 7, 14)]), k=0.4)
    m.add("aluminium", head, (-30, -36, 54), (38, 36, 120), step=0.3, tris=22000)
    m.add("plastic_black", frame(cylinder_z(4.8, 0, 6, 1.0), gc + (-25, 0, 18), R_X), (-33, -6, 104), (-23, 6, 116), step=0.2, tris=800)
    # Side handle out to the left.
    sh2 = cut2(poly2([(0, 0), (8.5, 0), (9.5, 12), (12.5, 30), (13.5, 70), (12.8, 98), (10.0, 104), (0, 105)], 0.8),
               *grooves2(14.6, np.arange(36, 96, 6.0), 1.5))
    m.add_revolve("rubber", sh2, 16, -2, 108, R=rot_matrix((1, 0, 0), 90), at=gc + (4, -32, 0), segs=64)
    # Spindle, flange nut, disc and its label.
    m.add("steel_dark", frame(cylinder_z(7.0, 30, 40, 0.8), gc, R_X), (28, -8, 84), (42, 8, 100), step=0.2, tris=1500)
    disc2 = poly2([(11.0, 40.5), (21.5, 40.5), (27.0, 37.0), (57.5, 37.0), (57.5, 43.4), (27.6, 43.4), (22.2, 46.8), (11.0, 46.8)], 0.35)
    m.add_revolve("abrasive", disc2, 59, 35, 49, R=R_X, at=gc, segs=160)
    label2 = poly2([(22.6, 47.0), (26.9, 43.6), (41.0, 43.6), (41.0, 43.95), (27.2, 43.95), (22.9, 47.35)])
    m.add_revolve("label", label2, 43, 42, 49, R=R_X, at=gc, segs=160, step=0.03)
    nut = cut(frame(cylinder_z(13.5, 46.8, 51.5, 0.8), gc, R_X),
              *[frame(cylinder_z(1.6, 45, 53, 0), gc + (0, y, 0), R_X) for y in (-8, 8)], frame(cylinder_z(5.5, 45, 53, 0), gc, R_X), k=0.3)
    m.add("chrome", nut, (44, -15, 76), (54, 15, 108), step=0.18, tris=4000)
    # The guard: a dished plate behind the disc's lower half, a rim round it and its clamp band.
    def lower(p, extra=0.0):
        return p[:, 2] - (gc[2] + 6.0 + extra)   # below a line just above the spindle
    plate = lathe_poly([(14.0, 31.0), (62.0, 33.5), (62.0, 35.0), (14.0, 32.5)])
    rim = lathe_poly([(61.0, 33.0), (62.6, 33.0), (62.6, 47.5), (61.0, 47.5)], 0.2)
    guard = intersect(union(frame(plate, gc, R_X), frame(rim, gc, R_X)), lower)
    band = cut(frame(cylinder_z(24.0, 20, 31, 1.0), gc, R_X), frame(cylinder_z(21.2, 10, 40, 0), gc, R_X))
    m.add("steel", union(guard, band), (18, -64, 26), (50, 64, 102), step=0.25, tris=16000)
    return m, {
        "nozzle": [44.0, 0.0, 149.0],   # the disc's exposed top edge, where it cuts
        "grips": {"right": {"at": [0, 0, -50], "axis": [0, 0, 1], "aim": [1, 0, 0], "radius": 30.5},
                  "left": {"at": [4, -100, 92], "axis": [0, 1, 0], "aim": [1, 0, 0], "radius": 13.0}},
        "hold": {"at": [33, 14, -20], "x": [0.96, 0.08, -0.26], "z": [0.26, -0.1, 0.96]},
    }


def spray_can():
    """A 400 ml aerosol: a seamed tinplate can in the paint's colour with a matt band and a colour
    swatch, domed top, valve cup and a spray head with its nozzle forward."""
    m = Model("tool_spray")
    can2 = poly2([(0, -66), (24, -70), (29.5, -77), (31.6, -78.2), (32.6, -76.5), (32.6, 97.5), (31.2, 101),
                  (27, 108), (19, 113.5), (14.6, 115.5), (14.4, 117.6), (16.6, 118.4), (16.8, 121.6), (13.4, 122.8), (0, 122.8)], 0.4)
    beads = [circle2(1.3, (31.9, -75.6)), circle2(1.2, (31.5, 99.4))]
    can2 = union2(can2, *beads)
    # Tinplate ends, the can in the paint's colour, a matt band round its middle with a stripe of the
    # colour on it (the colour you're spraying, no maker's print).
    m.add_revolve(None, can2, 35, -81, 125, segs=128, splits=[(-1e9, "tinplate"), (-73.5, "paint"), (-40, "band"), (22, "paint"),
                                                              (30, "band"), (48, "paint"), (98.6, "tinplate")])
    # Spray head: a stem, a round cap with a finger dish, the nozzle facing forward.
    head = lathe_poly([(0, 122), (5.0, 122), (5.0, 125.5), (11.8, 126), (12.2, 139.2), (10.5, 141.0), (0, 140.0)], 0.7)
    dish = frame(sphere(30.0), (0, 0, 169.2))
    head = cut(head, dish, k=0.6)
    tip = frame(cylinder_z(2.6, 0, 16.4, 0.5), (0, 0, 134), R_X)
    head = smooth_union(1.5, head, tip)
    head = cut(head, frame(cylinder_z(0.65, 10, 20, 0), (0, 0, 134), R_X))
    m.add("plastic_black", head, (-14, -14, 120), (18, 14, 143), step=0.18, tris=9000)
    return m, {
        "nozzle": [17.0, 0.0, 134.0],
        "grips": {"right": {"at": [0, 0, 0], "axis": [0, 0, 1], "aim": [1, 0, 0], "radius": 32.6, "index": "top"}},
        "hold": {"at": [32, 15, -16], "x": [0.99, -0.05, -0.12], "z": [0.12, 0.0, 0.99]},
    }


def paint_gun():
    """An HVLP gravity-feed spray gun: forged body, air cap with its two horns, fluid and fan
    adjusters, a two-finger trigger, the raked handle with an air inlet under it, and the cup on
    top in the paint's colour."""
    m = Model("tool_gun")
    a = np.array([0.259, 0.0, 0.966])   # the handle's line, raked back 15 degrees
    aim = np.array([0.966, 0.0, -0.259])
    def handle(p):
        s = p @ a
        q = p - s[:, None] * a[None, :]
        u = q @ aim
        w = q[:, 1]
        hw = np.interp(s, [-55, -40, 0, 40, 55], [12.0, 13.4, 14.2, 13.4, 12.6])
        hd = np.interp(s, [-55, -40, 0, 40, 55], [15.0, 17.0, 18.0, 17.0, 16.0])
        r = np.hypot(u / hd, w / hw) * np.minimum(hw, hd) - np.minimum(hw, hd)
        ends = np.maximum(-56 - s, s - 60)
        d = np.stack([r, ends], axis=1)
        return np.linalg.norm(np.maximum(d + 2.5, 0), axis=1) + np.minimum(np.max(d + 2.5, axis=1), 0) - 2.5
    grooves = union(*[frame(sphere(5.5), a * s + aim * 19.5) for s in (-26, -6, 14)])
    handle_g = cut(handle, grooves, k=1.2)
    body = smooth_union(4.0, frame(box((52.0, 12.5, 15.0), 6.0), (22, 0, 60)),
                        frame(cylinder_z(14.0, -40, 92, 3.0), (0, 0, 58), R_X),
                        frame(cylinder_z(9.0, -18, 18, 2.0), (36, 0, 74)))
    gun = smooth_union(6.0, handle_g, body)
    m.add("aluminium", gun, (-52, -20, -62), (98, 20, 94), step=0.3, tris=60000)
    # Air cap with its horns, fluid tip.
    cap = smooth_union(1.5, frame(cylinder_z(15.5, 92, 102, 2.0), (0, 0, 58), R_X),
                       *[bar((100, y * 13.5, 58), (111, y * 10.5, 58), 3.4, 5.5, 2.4, (0, 0, 1)) for y in (-1, 1)])
    cap = cut(cap, frame(cylinder_z(5.0, 98, 110, 0), (0, 0, 58), R_X), k=0.5)
    m.add("chrome", cap, (88, -20, 50), (114, 20, 66), step=0.2, tris=8000)
    m.add("brass", frame(lathe_poly([(0, 95), (4.6, 95), (4.2, 104), (1.6, 107.5), (0, 107.5)], 0.3), (0, 0, 58), R_X), (93, -6, 52), (109, 6, 64), step=0.15, tris=2000)
    # Adjusters at the back: fluid (knurled knob), fan (on top), air (under the handle).
    knurl = lambda r: union(*[bar((r * math.cos(t), r * math.sin(t), -1), (r * math.cos(t), r * math.sin(t), 13), 0.9, 0.9, 0.6, (math.cos(t), math.sin(t), 0))
                              for t in np.linspace(0, 2 * math.pi, 18, endpoint=False)])
    fluid = frame(cut(cylinder_z(9.5, 0, 12, 1.5), knurl(9.8), k=0.4), (0, 0, 0), R_X)
    m.add("plastic_black", frame(fluid, (-56, 0, 58)), (-58, -12, 45), (-42, 12, 71), step=0.18, tris=5000)
    m.add("chrome", frame(cylinder_z(3.2, -44, -38, 0.5), (0, 0, 58), R_X), (-46, -5, 53), (-36, 5, 63), step=0.18, tris=800)
    fan = frame(cut(cylinder_z(7.5, 0, 10, 1.2), knurl(7.8), k=0.3), (0, 0, 0), R_X)
    m.add("plastic_black", frame(fan, (-46, 0, 78)), (-48, -10, 68), (-34, 10, 88), step=0.18, tris=4000)
    m.add("chrome", segment((-38, 0, 78), (-30, 0, 76), 2.4), (-41, -4, 72), (-27, 4, 82), step=0.18, tris=600)
    # Trigger: two fingers wide, hung from a pivot at the body's front.
    trig = union(bar((24, 0, 48), (33, 0, 30), 9.5, 2.4, 1.8, (0, 1, 0)), bar((33, 0, 30), (34, 0, 6), 9.5, 2.4, 1.8, (0, 1, 0)),
                 bar((34, 0, 6), (30, 0, -6), 9.5, 2.4, 1.8, (0, 1, 0)))
    m.add("chrome", union(trig, frame(cylinder_z(3.4, -11, 11, 0.8), (24, 0, 49), R_Y)), (18, -13, -10), (40, 13, 54), step=0.22, tris=6000)
    # Air inlet fitting and a stub of hose.
    base = a * -60
    m.add("brass", union(frame(cylinder_z(7.5, -14, 0, 1.0), base, along(a)), frame(cylinder_z(5.0, -26, -14, 0.8), base, along(a))),
          base - 30 - 12, base + 12, step=0.2, tris=4000)
    hose_a = base - a * 24
    m.add("hose", segment(hose_a, hose_a + np.array([-34, 0, -40]), 6.8), np.minimum(hose_a, hose_a + (-34, 0, -40)) - 9,
          np.maximum(hose_a, hose_a + (-34, 0, -40)) + 9, step=0.3, tris=3000)
    # The cup: a chrome neck, the cup in the paint's colour (translucent plastic), a steel lid.
    neck = frame(cylinder_z(7.0, 70, 92, 1.0), (36, 0, 0))
    m.add("chrome", neck, (27, -9, 68), (45, 9, 94), step=0.2, tris=2000)
    cup2 = poly2([(0, 88), (8.5, 88), (30, 100), (37.5, 112), (39.0, 176), (0, 176)], 1.2)
    m.add_revolve("cup", cup2, 41, 85, 178, at=(36, 0, 0), segs=128)
    lid2 = union2(poly2([(0, 174), (40.6, 174), (41.2, 177), (40.0, 182), (8.0, 184), (0, 184)], 0.8),
                  poly2([(0, 182), (2.0, 182), (2.0, 195), (0, 196)], 0.6))
    m.add_revolve("steel", lid2, 43, 171, 198, at=(36, 0, 0), segs=128)
    return m, {
        "nozzle": [112.0, 0.0, 58.0],
        "grips": {"right": {"at": [0, 0, 0], "axis": a.tolist(), "aim": aim.tolist(), "radius": 15.5, "trigger": 2}},
        "hold": {"at": [31, 14, -22], "x": [0.99, -0.1, 0.0], "z": [0.0, 0.0, 1.0]},
    }


def lance():
    """A pressure washer gun and lance: the pistol grip with its long trigger and lock, a quick
    coupling, the stainless lance with a ribbed hand grip, and an adjustable nozzle head."""
    m = Model("tool_lance")
    a = np.array([0.309, 0.0, 0.951])   # the grip's line, raked 18 degrees
    aim = np.array([0.951, 0.0, -0.309])
    def grip(p):
        s = p @ a
        q = p - s[:, None] * a[None, :]
        u, w = q @ aim, q[:, 1]
        hw = np.interp(s, [-58, -30, 10, 50], [14.0, 15.0, 15.0, 15.5])
        hd = np.interp(s, [-58, -30, 10, 50], [19.0, 21.0, 20.0, 20.0])
        r = np.hypot(u / hd, w / hw) * np.minimum(hw, hd) - np.minimum(hw, hd)
        ends = np.maximum(-60 - s, s - 62)
        d = np.stack([r, ends], axis=1)
        return np.linalg.norm(np.maximum(d + 3.0, 0), axis=1) + np.minimum(np.max(d + 3.0, axis=1), 0) - 3.0
    housing = smooth_union(6.0, frame(box((62.0, 16.5, 17.0), 8.0), (40, 0, 62)), grip)
    housing = cut(housing, frame(box((40.0, 30.0, 14.0), 6.0), (36, 0, 20)), k=3.0)   # room for the fingers
    guard = bar((-6, 0, -50), (34, 0, -44), 8.0, 3.2, 2.6, (0, 1, 0))
    guard = union(guard, bar((34, 0, -44), (58, 0, 40), 8.0, 3.2, 2.6, (0, 1, 0)))
    body = smooth_union(3.0, housing, guard)
    m.add("plastic_grey", body, (-40, -22, -66), (106, 22, 84), step=0.3, tris=50000)
    # Rubber over the grip's back and sides.
    over = intersect(offset(grip, 0.9), lambda p: (p @ aim) - 4.0)
    m.add("rubber", over, (-40, -20, -66), (40, 20, 70), step=0.32, tris=12000)
    # The trigger: a long lever in front of the grip, pulled by the whole hand; its orange lock.
    trig = union(bar((30, 0, 44), (36, 0, 8), 8.6, 3.8, 2.6, (0, 1, 0)), bar((36, 0, 8), (24, 0, -34), 8.6, 3.8, 2.6, (0, 1, 0)))
    m.add("plastic_black", trig, (16, -12, -40), (44, 12, 50), step=0.26, tris=7000)
    m.add("plastic_orange", bar((4, 15.8, 52), (16, 15.8, 52), 4.0, 1.4, 1.0, (0, 0, 1)), (0, 12, 46), (20, 19, 58), step=0.2, tris=1500)
    # Quick coupling and the lance.
    lz = 62.0
    at_l = (0, 0, lz)
    coupling2 = cut2(poly2([(0, 96), (12.5, 96), (13.5, 99), (13.5, 112), (11.0, 116), (7.0, 120), (0, 120)], 0.5),
                     *grooves2(14.3, (101, 104, 107, 110), 1.0))
    m.add_revolve("chrome", coupling2, 15, 94, 122, R=R_X, at=at_l, segs=64)
    m.add_revolve("steel", poly2([(0, 118), (5.2, 118), (5.2, 548), (0, 548)], 0.0), 7, 116, 550, R=R_X, at=at_l, segs=40, step=0.08)
    sleeve2 = cut2(poly2([(0, 112), (8.0, 112), (13.5, 118), (14.0, 200), (12.5, 206), (6.0, 210), (0, 210)], 0.8),
                   *grooves2(15.0, np.arange(124, 200, 6.0), 1.6))
    m.add_revolve("rubber", sleeve2, 16, 110, 212, R=R_X, at=at_l, segs=64)
    # Nozzle head: a rubber bumper, an orange turning ring (ribbed to grip), the brass orifice.
    m.add_revolve("rubber", poly2([(0, 540), (6.0, 540), (11.0, 548), (12.5, 566), (11.5, 572), (0, 572)], 0.8), 14, 538, 574, R=R_X, at=at_l, segs=64)
    ring = lathe_poly([(0, 571), (12.4, 571), (13.0, 574), (12.0, 582), (6.0, 584), (0, 584)], 0.6)
    ring = cut(ring, union(*[bar((13.2 * math.cos(t), 13.2 * math.sin(t), 572), (13.2 * math.cos(t), 13.2 * math.sin(t), 582), 1.0, 1.0, 0.6, (math.cos(t), math.sin(t), 0))
                             for t in np.linspace(0, 2 * math.pi, 16, endpoint=False)]), k=0.3)
    m.add("plastic_orange", frame(ring, at_l, R_X), (569, -15, lz - 15), (586, 15, lz + 15), step=0.15, tris=9000)
    m.add_revolve("brass", poly2([(0, 582), (2.6, 582), (2.6, 586.5), (0.7, 587), (0.7, 585.5), (0, 585.5)], 0.3), 4, 580, 589, R=R_X, at=at_l, segs=32)
    # Swivel and a stub of the high-pressure hose from the grip's foot.
    foot = a * -62
    m.add("chrome", frame(lathe_poly([(0, -12), (9.0, -12), (9.5, 0), (0, 0)], 0.6), foot, along(a)), foot - 16, foot + 16, step=0.2, tris=3000)
    hose_a = foot - a * 12
    hose_b = hose_a + np.array([-20, 0, -60])
    m.add("hose", segment(hose_a, hose_b, 5.6), np.minimum(hose_a, hose_b) - 8, np.maximum(hose_a, hose_b) + 8, step=0.3, tris=3000)
    return m, {
        "nozzle": [588.0, 0.0, lz],
        "grips": {"right": {"at": [0, 0, 0], "axis": a.tolist(), "aim": aim.tolist(), "radius": 17.0, "trigger": 4},
                  "left": {"at": [140, 0, lz], "axis": [1, 0, 0], "aim": [0, 1, 0], "radius": 14.0}},
        "hold": {"at": [22, 6, -26], "x": [0.9, -0.42, -0.1], "z": [0.1, 0.0, 1.0]},
    }


TOOLS = {
    "wrench": spanner, "brush": brush, "repair": grinder, "spray": spray_can, "gun": paint_gun, "lance": lance,
}

# Material slots: a shed prop material (MI_Prop_<prop>) with its scans tiled `tile` times a metre (the
# tools are small), else a plain finish. "paint": tinted the spray colour; "cup": the gun's cup, the
# paint colour seen through translucent plastic.
MATERIALS = {
    "chrome": {"prop": "chrome", "tile": 14.0, "colour": [0.86, 0.87, 0.9], "metal": 1.0, "rough": 0.14},
    "steel": {"prop": "steel", "tile": 10.0, "colour": [0.6, 0.6, 0.62], "metal": 1.0, "rough": 0.36},
    "steel_dark": {"prop": "steel_dark", "tile": 10.0, "colour": [0.2, 0.2, 0.21], "metal": 1.0, "rough": 0.45},
    "aluminium": {"prop": "aluminium", "tile": 10.0, "colour": [0.78, 0.78, 0.79], "metal": 1.0, "rough": 0.34},
    "brass": {"prop": "brass", "tile": 12.0, "colour": [0.8, 0.6, 0.27], "metal": 1.0, "rough": 0.28},
    "tinplate": {"prop": "steel", "tile": 10.0, "colour": [0.75, 0.75, 0.76], "metal": 1.0, "rough": 0.22},
    "wire": {"colour": [0.66, 0.64, 0.6], "metal": 1.0, "rough": 0.42},
    "wood_top": {"prop": "wood_top", "tile": 7.0, "colour": [0.56, 0.36, 0.19], "metal": 0.0, "rough": 0.5},
    "rubber": {"colour": [0.028, 0.028, 0.03], "metal": 0.0, "rough": 0.82},
    "hose": {"colour": [0.03, 0.03, 0.035], "metal": 0.0, "rough": 0.55},
    "plastic_black": {"prop": "plastic_black", "tile": 10.0, "colour": [0.025, 0.025, 0.028], "metal": 0.0, "rough": 0.42},
    "plastic_grey": {"prop": "plastic_grey", "tile": 10.0, "colour": [0.22, 0.23, 0.24], "metal": 0.0, "rough": 0.48},
    "plastic_red": {"prop": "plastic_red", "tile": 10.0, "colour": [0.6, 0.04, 0.03], "metal": 0.0, "rough": 0.4},
    "plastic_orange": {"colour": [0.9, 0.3, 0.02], "metal": 0.0, "rough": 0.4},
    "body": {"prop": "paint_blue", "tile": 8.0, "colour": [0.06, 0.3, 0.56], "metal": 0.0, "rough": 0.38},
    "abrasive": {"colour": [0.16, 0.155, 0.15], "metal": 0.05, "rough": 0.95},
    "label": {"colour": [0.62, 0.06, 0.04], "metal": 0.0, "rough": 0.55},
    "band": {"colour": [0.03, 0.03, 0.033], "metal": 0.2, "rough": 0.6},
    "paint": {"paint": True, "colour": [0.9, 0.75, 0.1], "metal": 0.35, "rough": 0.24},
    "cup": {"paint": True, "colour": [0.9, 0.75, 0.1], "metal": 0.0, "rough": 0.18},
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ids", nargs="*")
    ap.add_argument("--glb", help="also write preview glbs here")
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    spec_path = os.path.join(OUT, "tools.json")
    spec = json.load(open(spec_path)) if os.path.exists(spec_path) else {}
    spec["about"] = ("The tools in your hands (Tools/props/make_hand_tools.py): mesh file, material slots, nozzle, the "
                     "first-person hold (cm in the camera's frame: x ahead, y right, z up; the tool's x and z axes) and each "
                     "hand's grip (cm in the tool's frame: the bar through the fist, pinky to index, the way the knuckles "
                     "point, its radius; trigger = fingers on a trigger, index 'top' = the index on a spray head).")
    spec.setdefault("tools", {})
    for tid in args.ids or TOOLS:
        model, info = TOOLS[tid]()
        model.write_tmesh(os.path.join(OUT, model.name + ".tmesh"))
        if args.glb:
            os.makedirs(args.glb, exist_ok=True)
            model.write_glb(os.path.join(args.glb, model.name + ".glb"), MATERIALS)
        cm = lambda v: [round(float(x) * 0.1, 3) for x in v]
        grips = {}
        for side, g in info["grips"].items():
            gg = dict(g)
            gg["at"] = cm(g["at"])
            gg["radius"] = round(g["radius"] * 0.1, 3)
            gg["axis"] = [round(float(x), 4) for x in g["axis"]]
            gg["aim"] = [round(float(x), 4) for x in g["aim"]]
            grips[side] = gg
        lo, hi = model.bounds()
        spec["tools"][tid] = {
            "mesh": model.name + ".tmesh",
            "slots": {k: MATERIALS[k] for k in sorted(model.slots)},
            "nozzle": cm(info["nozzle"]),
            "hold": info["hold"],
            "grips": grips,
            "tris": model.tri_count(),
        }
        print("%-7s %6d tris, %d slots, %.0f x %.0f x %.0f mm" % (tid, model.tri_count(), len(model.slots), *(hi - lo)))
    with open(spec_path, "w") as fh:
        json.dump(spec, fh, indent=1)


if __name__ == "__main__":
    main()
