"""The small vehicle ferry and its shore kit (ferries plan stage 2, beenster Nov 19 2026: "Each ferry should
feel small (for a couple of cars and a couple of bikes). Good ferry models and nice sounds would be great").
Built in Blender into Data/Props/Blender/<id>.glb like the shed props (Tools/props/make_props.py), so the PC
imports them with the rest (Tools/ue_import_props.py) and never needs Blender:

    blender -b --factory-startup -P Tools/props/make_ferry.py                 all of them
    blender -b --factory-startup -P Tools/props/make_ferry.py -- ferry_small  just these
    python3 Tools/props/make_ferry.py -- ...                                   the same with bpy from pip

The kit, in Torque Shed's frame (metres, x right, y up, +z the front), each with its origin where the game
hangs it (World/TorqueFerry.cpp reads the same numbers, FerryModel):

    ferry_small         the boat: a double-ended open-deck car ferry, 26 m by 9 m, drive-through (a ramp at
                        each end, the bow is +z), its wheelhouse on a tower on the starboard (+x) side with
                        an outside stair up to a small upper deck. Origin on the waterline under the middle
                        of the car deck; the deck's top at y 1.0. Room for two cars in the port lane and a
                        marked bay for bikes beside the tower. Our own livery: a dark blue hull, red below
                        the waterline, a white tower, a teal band; the operator ("Kyle & Sound") and the
                        boat's name are made up.
    ferry_ramp          one end's ramp (the boat has two): ridged steel, 6.6 m wide and 4.6 m long, hinged
                        about its origin (the deck's end, at the deck's top), lying along +z; its toe flap
                        at the far end. The game swings it up about x.
    ferry_hut           the ticket hut at the head of the slipway: 3 by 2.4 m, rendered block, a slate roof,
                        a window to the queue lane (+z faces the lane).
    ferry_shelter       a waiting shelter: steel frame, glass sides, a bench (+z open side).
    ferry_sign          the approach sign: a blue board on two posts, FERRY over a little boat and an arrow.
    ferry_barrier       the barrier at the slipway's head: the post and its signal head; the arm is
                        ferry_barrier_arm (pivot at its origin, lying along +x), which the game swings up.

Every piece is bevelled and box-UV'd in metres by kit.finish, so the scanned materials (Data/Props/materials.json:
ferry_*, slip_*) tile at their true size. Deterministic: the same script writes the same glbs.
"""
import json
import math
import os
import sys

import bpy  # noqa: E402,F401  (first: with bpy from pip, bmesh and mathutils come with it)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kit  # noqa: E402
from kit import box, span, cyl, tube, torus, prism, sphere  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "Data", "Props", "Blender")
MODELS = {}

# --- The boat's numbers (World/TorqueFerry.cpp's FerryModel has the same) ------------------------------
LEN = 26.0          # overall (m), z -13 .. 13
BEAM = 9.0          # x -4.5 .. 4.5
DECK = 1.0          # the car deck's top over the waterline
KEEL = -1.5         # the hull's bottom
HALF_DECK = 12.6    # the deck runs z -12.6 .. 12.6; each ramp hinges at its end
BULWARK = 2.05      # the solid sides' top (1.05 m over the deck)
SIDE_IN = 4.3       # the bulwarks' inner face (x)
TOWER = (2.45, 4.5, -4.6, 4.6, 3.45)   # the side tower: x from, x to, z from, z to, its roof (the upper deck) y
HOUSE = (2.65, 4.35, -2.0, 2.0, 5.75)  # the wheelhouse on it: x from, x to, z from, z to, roof y
STAIR = (2.6, 3.5, -8.4, -4.6)         # the outside stair up to the upper deck: x from, x to, z foot, z top
LANE_CARS = -2.95                      # the middle of the cars' lane (x)
BIKE_BAY = (0.75, 2.3, 4.9, 10.6)      # the marked bay for bikes: x from, x to, z from, z to
RAMP_W, RAMP_L = 6.6, 4.6


def model(pid):
    def wrap(fn):
        MODELS[pid] = fn
        return fn
    return wrap


def text(name, body, at, size, mat, rot=(90, 0, 0), extrude=0.004, align="CENTER"):
    """Letters standing on a face: at in Torque Shed's frame; rot (Blender Euler degrees)."""
    import bpy
    bpy.ops.object.text_add(location=kit.B(at), rotation=tuple(math.radians(a) for a in rot))
    t = bpy.context.object
    t.data.body = body
    t.data.align_x = align
    t.data.align_y = "CENTER"
    t.data.size = size
    t.data.extrude = extrude
    bpy.ops.object.convert(target="MESH")
    o = bpy.context.object
    o.name = name
    kit._finish(o, mat, 0.0)
    return o


def rail(name, pts, height, posts_every=1.5, mat="ferry_white", r=0.025):
    """A handrail along pts (x, z) at deck height pts' y: a top rail, a mid rail and posts."""
    for k, (lo, hi) in enumerate(zip(pts[:-1], pts[1:])):
        x0, y0, z0 = lo
        x1, y1, z1 = hi
        tube("%s_Top%d" % (name, k), [(x0, y0 + height, z0), (x1, y1 + height, z1)], r, mat, sides=10)
        tube("%s_Mid%d" % (name, k), [(x0, y0 + height * 0.5, z0), (x1, y1 + height * 0.5, z1)], r * 0.7, mat, sides=8)
        seg = math.hypot(x1 - x0, z1 - z0)
        n = max(1, int(round(seg / posts_every)))
        for i in range(n + (1 if k == len(pts) - 2 else 0)):
            t = i / n
            x, y, z = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, z0 + (z1 - z0) * t
            cyl("%s_Post%d_%d" % (name, k, i), (x, y + height * 0.5, z), r * 0.9, height, mat, verts=8, bevel=0.0)


def lifebelt(name, at, facing_x):
    """A lifebelt on its bracket, hung on a face looking along x (facing_x +1 or -1)."""
    x, y, z = at
    torus(name, (x, y, z), 0.3, 0.075, "cone_orange", axis="x", verts=24)
    for k in range(4):
        a = math.pi * 0.25 + k * math.pi * 0.5
        box("%s_Tape%d" % (name, k), (x + 0.0, y + math.sin(a) * 0.3, z + math.cos(a) * 0.3), (0.17, 0.06, 0.06), "paint_white", bevel=0.0,
            rot=(math.degrees(a), 0, 0))
    box(name + "_Hook", (x - facing_x * 0.06, y + 0.33, z), (0.06, 0.08, 0.05), "steel_dark", bevel=0.01)


def bollard(name, x, z, y=DECK):
    cyl(name + "_Base", (x, y + 0.03, z), 0.28, 0.06, "paint_black", verts=16, bevel=0.01)
    for dz in (-0.18, 0.18):
        cyl(name + "_Post%+.0f" % (dz * 10), (x, y + 0.28, z + dz), 0.11, 0.48, "paint_black", verts=16, bevel=0.02)
        cyl(name + "_Cap%+.0f" % (dz * 10), (x, y + 0.53, z + dz), 0.14, 0.04, "paint_black", verts=16, bevel=0.01)


def rope_coil(name, x, z, y=DECK):
    for k in range(4):
        torus("%s_%d" % (name, k), (x, y + 0.03 + k * 0.045, z), 0.32 - k * 0.025, 0.022, "twine", verts=20)


def window_row(name, x_face, facing, y0, y1, z0, z1, count, mat="glass", frame="steel_dark"):
    """A row of windows on a face at x = x_face looking along +x (facing 1) or -x (-1)."""
    w = (z1 - z0) / count
    for k in range(count):
        zc = z0 + w * (k + 0.5)
        box("%s_%d" % (name, k), (x_face + facing * 0.012, (y0 + y1) / 2, zc), (0.02, y1 - y0, w - 0.12), mat, bevel=0.0)
        box("%s_%dF" % (name, k), (x_face + facing * 0.006, (y0 + y1) / 2, zc), (0.03, y1 - y0 + 0.08, w - 0.04), frame, bevel=0.004)


@model("ferry_small")
def ferry_small():
    # --- The hull: its side profile pushed across its beam, raked up at both ends like a landing craft.
    # (its top just under the deck plate's, so the tread plate is what shows)
    top = DECK - 0.05
    prof = [(-HALF_DECK - 0.25, top), (-HALF_DECK - 0.25, 0.25), (-10.8, KEEL), (10.8, KEEL), (HALF_DECK + 0.25, 0.25), (HALF_DECK + 0.25, top)]
    prism("Hull", prof, BEAM, "ferry_hull", axis="x", bevel=0.12)
    # Below the waterline: the antifouling's red, a hair proud of the hull so it shows at the waterline.
    under = [(-HALF_DECK - 0.27, 0.06), (-HALF_DECK - 0.27, 0.0), (-11.6, -0.5), (-10.8, KEEL - 0.02), (10.8, KEEL - 0.02), (11.6, -0.5),
             (HALF_DECK + 0.27, 0.0), (HALF_DECK + 0.27, 0.06)]
    prism("Antifoul", under, BEAM + 0.03, "ferry_antifoul", axis="x", bevel=0.1)
    # The rubbing strakes and the teal band along each side.
    for s in (-1, 1):
        x = s * (BEAM / 2 + 0.06)
        span("Strake%+d" % s, (x - 0.08, 0.72, -HALF_DECK), (x + 0.08, 0.88, HALF_DECK), "ferry_fender", bevel=0.05)
        span("Band%+d" % s, (s * BEAM / 2 - 0.02, 0.35, -HALF_DECK + 0.3), (s * BEAM / 2 + 0.02, 0.55, HALF_DECK - 0.3), "ferry_teal", bevel=0.0)
        # Tyre fenders hung over the side by the ends.
        for k, z in enumerate((-10.5, -7.0, 7.0, 10.5)):
            torus("Tyre%+d_%d" % (s, k), (s * (BEAM / 2 + 0.22), 0.45, z), 0.32, 0.13, "ferry_fender", axis="x", verts=20)
    # The boat's name and its home port on the bow quarters and the stern, the operator on the tower.
    for s in (-1, 1):
        for zc in (-8.5, 8.5):
            text("Name%+d%+.0f" % (s, zc), "SKERRY BELLE", (s * (BEAM / 2 + 0.005), 0.78 + 0.0, zc), 0.34, "ferry_white",
                 rot=(90, 0, 90 if s > 0 else -90), extrude=0.003)

    # --- The car deck: green non-slip tread plate, the lanes' lines, the bike bay.
    span("Deck", (-SIDE_IN - 0.05, DECK - 0.04, -HALF_DECK - 0.05), (SIDE_IN + 0.05, DECK, HALF_DECK + 0.05), "ferry_deck", bevel=0.0)
    for x in (-1.75, 0.55):
        span("Lane%+.1f" % x, (x - 0.05, DECK, -HALF_DECK + 0.6), (x + 0.05, DECK + 0.004, HALF_DECK - 0.6), "paint_yellow", bevel=0.0)
    x0, x1, z0, z1 = BIKE_BAY
    for nm, lo, hi in (("BayA", (x0, DECK, z0), (x1, DECK + 0.004, z0 + 0.1)), ("BayB", (x0, DECK, z1 - 0.1), (x1, DECK + 0.004, z1)),
                       ("BayC", (x0, DECK, z0), (x0 + 0.1, DECK + 0.004, z1))):
        span(nm, lo, hi, "paint_white", bevel=0.0)
    for k in range(1, 3):
        z = z0 + (z1 - z0) * k / 3.0
        span("BayDiv%d" % k, (x0, DECK, z - 0.04), (x1, DECK + 0.004, z + 0.04), "paint_white", bevel=0.0)
    text("BayWord", "MOTORCYCLES", ((x0 + x1) / 2 - 0.15, DECK + 0.006, (z0 + z1) / 2), 0.32, "paint_white", rot=(0, 0, 90), extrude=0.001)
    # Lashing points let into the deck, a grid of them.
    for xi in (-3.6, -2.3, -0.6, 1.5):
        for zi in range(-5, 6):
            cyl("Lash%+.1f_%d" % (xi, zi), (xi, DECK + 0.006, zi * 2.2), 0.06, 0.012, "steel_dark", verts=10, bevel=0.0)
    # The wash ports along the deck edge (where the sea runs off) and a little rust under each.
    for s in (-1, 1):
        for k in range(-4, 5):
            z = k * 2.6
            span("Port%+d_%d" % (s, k), (s * SIDE_IN - 0.06, DECK, z - 0.25), (s * SIDE_IN + 0.06, DECK + 0.12, z + 0.25), "paint_black", bevel=0.0)
            span("Streak%+d_%d" % (s, k), (s * (BEAM / 2 + 0.003) - 0.004, 0.4, z - 0.08), (s * (BEAM / 2 + 0.003) + 0.004, DECK - 0.02, z + 0.08),
                 "yard_rust", bevel=0.0)

    # --- The bulwarks: solid steel sides to waist height, the port one whole, the starboard one either side of
    # the tower; their capping rail; the gates at the ends are the ramps.
    for s, segs in ((-1, [(-HALF_DECK, HALF_DECK)]), (1, [(-HALF_DECK, STAIR[2] - 0.2), (TOWER[3], HALF_DECK)])):
        for k, (za, zb) in enumerate(segs):
            span("Bulwark%+d_%d" % (s, k), (s * SIDE_IN if s > 0 else -BEAM / 2, DECK, za), (BEAM / 2 if s > 0 else -SIDE_IN, BULWARK, zb), "ferry_hull", bevel=0.03)
            span("Cap%+d_%d" % (s, k), (s * SIDE_IN - (0.0 if s > 0 else 0.25), BULWARK, za), (s * SIDE_IN + (0.25 if s > 0 else 0.0), BULWARK + 0.06, zb),
                 "ferry_white", bevel=0.02)
            # Its inner face painted light, with stiffeners every metre.
            span("Inner%+d_%d" % (s, k), (s * SIDE_IN - 0.01, DECK, za), (s * SIDE_IN + 0.01, BULWARK, zb), "ferry_white", bevel=0.0)
            n = int((zb - za) / 1.2)
            for i in range(1, n):
                z = za + (zb - za) * i / n
                span("Stiff%+d_%d_%d" % (s, k, i), (s * SIDE_IN - s * 0.09 - 0.02, DECK, z - 0.04), (s * SIDE_IN - s * 0.09 + 0.02, BULWARK - 0.05, z + 0.04),
                     "ferry_white", bevel=0.0)
    # Lifebelts on the port bulwark's inside, bollards and rope coils at the four corners.
    for k, z in enumerate((-8.0, 0.0, 8.0)):
        lifebelt("Belt%d" % k, (-SIDE_IN + 0.12, 1.58, z), 1)
    for sx in (-1, 1):
        for sz in (-1, 1):
            x = sx * 3.55
            z = sz * 11.5
            if sx > 0 and sz < 0:
                z = -10.9
            bollard("Bollard%+d%+d" % (sx, sz), x, z)
            rope_coil("Coil%+d%+d" % (sx, sz), x, z - sz * 0.9)

    # --- The ramps' gantries at each end: the hydraulic rams' posts on the bulwark ends, a lamp on each.
    for sz in (-1, 1):
        zh = sz * (HALF_DECK - 0.15)
        for sx in (-1, 1):
            x = sx * (SIDE_IN + 0.12)
            span("RamPost%+d%+d" % (sx, sz), (x - 0.14, DECK, zh - 0.14), (x + 0.14, 3.0, zh + 0.14), "ferry_white", bevel=0.02)
            cyl("Ram%+d%+d" % (sx, sz), (x, 2.4, zh + sz * 0.35), 0.075, 1.6, "galvanised", verts=12, rot=(sz * -55, 0, 0))
            cyl("RamRod%+d%+d" % (sx, sz), (x, 2.15, zh + sz * 0.9), 0.04, 1.2, "chrome", verts=10, rot=(sz * -55, 0, 0))
        span("Gantry%+d" % sz, (-SIDE_IN - 0.26, 3.0, zh - 0.12), (SIDE_IN + 0.26, 3.22, zh + 0.12), "ferry_white", bevel=0.02)
        for sx in (-1, 1):
            box("Flood%+d%+d" % (sx, sz), (sx * 2.2, 2.92, zh), (0.45, 0.12, 0.22), "steel_dark", bevel=0.01)
            box("FloodLens%+d%+d" % (sx, sz), (sx * 2.2, 2.85, zh), (0.4, 0.02, 0.18), "lamp_lens", bevel=0.0)
        # The traffic light for the cars, red and green, facing out over the ramp.
        span("SignalBox%+d" % sz, (-0.25, 3.22, zh - 0.08), (0.25, 3.62, zh + 0.08), "paint_black", bevel=0.02)
        cyl("SignalRed%+d" % sz, (-0.11, 3.42, zh + sz * 0.09), 0.08, 0.03, "red_lens", axis="z", verts=16, bevel=0.0)
        cyl("SignalGreen%+d" % sz, (0.11, 3.42, zh + sz * 0.09), 0.08, 0.03, "ferry_green_lens", axis="z", verts=16, bevel=0.0)

    # --- The side tower: white, the lounge's windows, a door, the outside stair, the upper deck and the
    # wheelhouse on it.
    tx0, tx1, tz0, tz1, ty = TOWER
    span("Tower", (tx0, DECK, tz0), (tx1, ty, tz1), "ferry_white", bevel=0.06)
    window_row("LoungeIn", tx0, -1, 2.0, 2.75, tz0 + 0.6, tz1 - 0.6, 4)
    window_row("LoungeOut", tx1, 1, 2.0, 2.75, tz0 + 0.4, tz1 - 0.4, 5)
    span("Door", (tx0 - 0.02, DECK, 2.2), (tx0 + 0.01, 3.0, 3.1), "steel_dark", bevel=0.01)
    box("DoorPort", (tx0 - 0.03, 2.55, 2.65), (0.02, 0.32, 0.32), "glass", bevel=0.0)
    text("Operator", "KYLE & SOUND", (tx0 - 0.015, 3.15, 0.0), 0.26, "ferry_teal", rot=(90, 0, -90), extrude=0.003)
    text("OperatorOut", "KYLE & SOUND", (tx1 + 0.015, 3.15, 0.0), 0.26, "ferry_teal", rot=(90, 0, 90), extrude=0.003)
    # The teal band round the tower's top.
    span("TowerBand", (tx0 - 0.01, ty - 0.25, tz0 - 0.01), (tx1 + 0.01, ty - 0.08, tz1 + 0.01), "ferry_teal", bevel=0.0)
    # The upper deck (the tower's roof, a little wider), its rail.
    ux0 = tx0 - 0.15
    span("UpperDeck", (ux0, ty, tz0 - 0.3), (tx1 + 0.05, ty + 0.08, tz1 + 0.3), "ferry_deck", bevel=0.02)
    yr = ty + 0.08
    rail("UpperRail", [(ux0 + 0.05, yr, STAIR[3] + 0.0), (ux0 + 0.05, yr, tz1 + 0.25), (tx1, yr, tz1 + 0.25), (tx1, yr, tz0 - 0.25),
                       (STAIR[1] + 0.05, yr, tz0 - 0.25)], 1.05)
    # The stair: treads up a stringer each side, a handrail.
    sx0, sx1, sz0, sz1 = STAIR
    steps = 12
    for k in range(steps):
        t = (k + 0.5) / steps
        y = DECK + (ty - DECK) * (k + 1) / steps
        z = sz0 + (sz1 - sz0) * t
        span("Tread%d" % k, (sx0, y - 0.04, z - 0.15), (sx1, y, z + 0.15), "ferry_deck", bevel=0.005)
    for x in (sx0 - 0.03, sx1 + 0.03):
        tube("Stringer%.1f" % x, [(x, DECK, sz0), (x, ty, sz1)], 0.045, "ferry_white", sides=8)
    rail("StairRail", [(sx0 - 0.05, DECK, sz0), (sx0 - 0.05, ty, sz1)], 0.95, posts_every=1.6)
    # The wheelhouse: windows all round, a dark roof with the mast, the radar and the lights.
    hx0, hx1, hz0, hz1, hy = HOUSE
    span("HouseLow", (hx0, yr, hz0), (hx1, yr + 0.95, hz1), "ferry_white", bevel=0.03)
    span("HouseHigh", (hx0, yr + 1.75, hz0), (hx1, hy, hz1), "ferry_white", bevel=0.03)
    for k, (x, z) in enumerate(((hx0, hz0), (hx1, hz0), (hx0, hz1), (hx1, hz1))):
        span("Pillar%d" % k, (x - 0.06, yr + 0.9, z - 0.06), (x + 0.06, yr + 1.8, z + 0.06), "ferry_white", bevel=0.01)
    for nm, lo, hi in (("GlassIn", (hx0 - 0.01, yr + 0.95, hz0 + 0.06), (hx0 + 0.01, yr + 1.75, hz1 - 0.06)),
                       ("GlassOut", (hx1 - 0.01, yr + 0.95, hz0 + 0.06), (hx1 + 0.01, yr + 1.75, hz1 - 0.06)),
                       ("GlassFwd", (hx0 + 0.06, yr + 0.95, hz1 - 0.01), (hx1 - 0.06, yr + 1.75, hz1 + 0.01)),
                       ("GlassAft", (hx0 + 0.06, yr + 0.95, hz0 - 0.01), (hx1 - 0.06, yr + 1.75, hz0 + 0.01))):
        span(nm, lo, hi, "glass", bevel=0.0)
    # Inside: the console, the wheel, two seats (seen through the glass).
    for sz in (-1, 1):
        span("Console%+d" % sz, (hx0 + 0.2, yr, sz * (hz1 - 0.45) - 0.25), (hx1 - 0.2, yr + 1.0, sz * (hz1 - 0.45) + 0.25), "steel_dark", bevel=0.02)
        cyl("Wheel%+d" % sz, ((hx0 + hx1) / 2, yr + 1.15, sz * (hz1 - 0.75)), 0.2, 0.03, "paint_black", axis="z", verts=20, bevel=0.0)
        box("Seat%+d" % sz, ((hx0 + hx1) / 2, yr + 0.5, sz * (hz1 - 1.3)), (0.5, 0.12, 0.5), "foam_black", bevel=0.03)
    span("HouseRoof", (hx0 - 0.15, hy, hz0 - 0.15), (hx1 + 0.15, hy + 0.1, hz1 + 0.15), "paint_grey", bevel=0.02)
    span("Visor", (hx0 - 0.3, hy - 0.05, hz0 - 0.3), (hx1 + 0.3, hy + 0.02, hz1 + 0.3), "ferry_white", bevel=0.01)
    mx = (hx0 + hx1) / 2
    cyl("Mast", (mx, hy + 1.2, 0.0), 0.06, 2.4, "ferry_white", verts=12)
    span("Yard", (mx - 0.7, hy + 1.6, -0.04), (mx + 0.7, hy + 1.66, 0.04), "ferry_white", bevel=0.005)
    box("RadarBase", (mx, hy + 2.45, 0.0), (0.22, 0.14, 0.22), "steel_dark", bevel=0.02)
    box("Radar", (mx, hy + 2.56, 0.0), (0.1, 0.07, 1.4), "plastic_white", bevel=0.02, rot=(0, 25, 0))
    sphere("MastLight", (mx, hy + 2.42, 0.0), 0.07, "lamp_lens")
    sphere("PortLight", (mx - 0.68, hy + 1.75, 0.0), 0.07, "red_lens")
    sphere("StbdLight", (mx + 0.68, hy + 1.75, 0.0), 0.07, "ferry_green_lens")
    cyl("Horn", (mx + 0.25, hy + 0.35, 0.6), 0.07, 0.4, "ferry_white", axis="z", verts=12, radius2=0.12)
    # Twin exhaust stacks at the tower's outboard corners, black tops, the teal band.
    for sz in (-1, 1):
        x, z = tx1 - 0.35, sz * (tz1 - 0.55)
        cyl("Stack%+d" % sz, (x, yr + 1.0, z), 0.2, 2.0, "ferry_white", verts=16)
        cyl("StackBand%+d" % sz, (x, yr + 1.55, z), 0.205, 0.3, "ferry_teal", verts=16, bevel=0.0)
        cyl("StackTop%+d" % sz, (x, yr + 2.05, z), 0.21, 0.12, "paint_black", verts=16, bevel=0.0)
    # Life rafts in their white canisters on the upper deck's outboard rail, lifebelts on the tower ends.
    for k, z in enumerate((-3.4, 3.4)):
        cyl("Raft%d" % k, (tx1 - 0.6, yr + 0.35, z), 0.3, 1.1, "plastic_white", axis="z", verts=16, bevel=0.06)
        span("RaftCradle%d" % k, (tx1 - 0.95, yr, z - 0.4), (tx1 - 0.25, yr + 0.1, z + 0.4), "galvanised", bevel=0.01)
    lifebelt("BeltTowerA", (tx0 - 0.02, 2.4, tz0 + 0.5), -1)
    lifebelt("BeltTowerB", (tx0 - 0.02, 2.4, tz1 - 0.5), -1)


@model("ferry_ramp")
def ferry_ramp():
    """One end's ramp, lying flat along +z from its hinge at the origin (the deck's top at the deck's end)."""
    w, l = RAMP_W, RAMP_L
    span("Plate", (-w / 2, -0.28, 0.0), (w / 2, 0.0, l - 0.6), "ferry_ramp", bevel=0.02)
    # The toe flap: a short plate angled down to meet the slipway.
    box("Toe", (0.0, -0.1, l - 0.32), (w - 0.1, 0.05, 0.62), "ferry_ramp", bevel=0.01, rot=(-12, 0, 0))
    # Ridges across it every 0.3 m, for the tyres' grip on wet steel.
    k = 0
    z = 0.25
    while z < l - 0.7:
        span("Ridge%d" % k, (-w / 2 + 0.15, 0.0, z - 0.02), (w / 2 - 0.15, 0.03, z + 0.02), "ferry_ramp", bevel=0.005)
        z += 0.3
        k += 1
    # Its edges: low kerbs with yellow and black chevrons, the hinge's knuckles.
    for s in (-1, 1):
        span("Kerb%+d" % s, (s * w / 2 - (0.16 if s > 0 else 0.0), 0.0, 0.0), (s * w / 2 + (0.0 if s > 0 else 0.16), 0.16, l - 0.6), "paint_yellow", bevel=0.02)
        span("Lug%+d" % s, (s * (w / 2 - 0.2) - 0.1, -0.4, 0.4), (s * (w / 2 - 0.2) + 0.1, 0.0, 0.9), "galvanised", bevel=0.01)
    for k in range(6):
        x = -w / 2 + 0.6 + k * (w - 1.2) / 5
        cyl("Knuckle%d" % k, (x, -0.14, 0.0), 0.14, 0.5, "steel_dark", axis="x", verts=12, bevel=0.01)
    for k in range(5):
        x = -w / 2 + 0.9 + k * (w - 1.8) / 4
        span("Chevron%d" % k, (x - 0.25, 0.031, l - 0.95), (x + 0.25, 0.034, l - 0.75), "paint_black" if k % 2 else "paint_yellow", bevel=0.0)


@model("ferry_hut")
def ferry_hut():
    """The ticket hut: rendered block on a plinth, a slate roof, a window and a hatch to the lane (+z)."""
    W, D, H = 3.0, 2.4, 2.5
    span("Plinth", (-W / 2 - 0.05, -0.6, -D / 2 - 0.05), (W / 2 + 0.05, 0.15, D / 2 + 0.05), "slip_concrete", bevel=0.01)
    span("Walls", (-W / 2, 0.15, -D / 2), (W / 2, H, D / 2), "ferry_white", bevel=0.02)
    span("Window", (-0.95, 1.0, D / 2 - 0.01), (0.55, 1.9, D / 2 + 0.03), "glass", bevel=0.0)
    span("WindowFrame", (-1.0, 0.95, D / 2), (0.6, 1.95, D / 2 + 0.02), "ferry_teal", bevel=0.01)
    span("Sill", (-1.05, 0.9, D / 2), (0.65, 0.97, D / 2 + 0.12), "slip_concrete", bevel=0.01)
    span("Door", (W / 2 - 0.01, 0.15, -0.45), (W / 2 + 0.02, 2.05, 0.45), "ferry_teal", bevel=0.01)
    span("DoorGlass", (W / 2 + 0.02, 1.35, -0.25), (W / 2 + 0.03, 1.85, 0.25), "glass", bevel=0.0)
    # A shallow pitched roof of slate, its ridge along x.
    for s in (-1, 1):
        box("Roof%+d" % s, (0.0, H + 0.28, s * (D / 4 + 0.08)), (W + 0.4, 0.06, D / 2 + 0.38), "stone", bevel=0.01,
            rot=(s * 22, 0, 0))
    span("Ridge", (-W / 2 - 0.2, H + 0.5, -0.07), (W / 2 + 0.2, H + 0.58, 0.07), "paint_grey", bevel=0.02)
    for s in (-1, 1):
        prism("Gable%+d" % s, [(-D / 2, H), (D / 2, H), (0.0, H + 0.48)], 0.02, "ferry_white", axis="x", centre=s * W / 2, bevel=0.0)
    span("Board", (-1.1, 2.1, D / 2 + 0.02), (1.1, 2.42, D / 2 + 0.05), "ferry_sign_blue", bevel=0.01)
    text("BoardWord", "TICKETS", (0.0, 2.26, D / 2 + 0.055), 0.2, "paint_white", rot=(90, 0, 0), extrude=0.002)


@model("ferry_shelter")
def ferry_shelter():
    """A waiting shelter: a galvanised frame, glass back and ends, a bench, a flat roof (+z open)."""
    W, D, H = 3.2, 1.4, 2.3
    for x in (-W / 2, W / 2):
        for z in (-D / 2, D / 2):
            span("Post%+.0f%+.0f" % (x, z), (x - 0.04, 0.0, z - 0.04), (x + 0.04, H, z + 0.04), "galvanised", bevel=0.01)
    span("Back", (-W / 2, 0.15, -D / 2 - 0.01), (W / 2, H - 0.1, -D / 2 + 0.01), "glass", bevel=0.0)
    for x in (-W / 2, W / 2):
        span("End%+.0f" % x, (x - 0.01, 0.15, -D / 2), (x + 0.01, H - 0.1, D / 2 - 0.2), "glass", bevel=0.0)
    span("Roof", (-W / 2 - 0.1, H, -D / 2 - 0.15), (W / 2 + 0.1, H + 0.1, D / 2 + 0.15), "galvanised", bevel=0.02)
    span("Fascia", (-W / 2 - 0.1, H - 0.18, D / 2 + 0.1), (W / 2 + 0.1, H + 0.1, D / 2 + 0.15), "ferry_teal", bevel=0.01)
    span("Bench", (-W / 2 + 0.2, 0.45, -D / 2 + 0.05), (W / 2 - 0.2, 0.5, -D / 2 + 0.4), "galvanised", bevel=0.01)
    for x in (-1.0, 1.0):
        span("BenchLeg%+.0f" % x, (x - 0.03, 0.0, -D / 2 + 0.15), (x + 0.03, 0.45, -D / 2 + 0.3), "galvanised", bevel=0.005)
    span("Timetable", (-0.4, 1.1, -D / 2 + 0.015), (0.4, 1.7, -D / 2 + 0.03), "paper", bevel=0.0)


@model("ferry_sign")
def ferry_sign():
    """The approach sign: a blue board on two posts, FERRY over a little boat and an arrow (+z its face)."""
    W, H0, H1 = 1.9, 1.3, 2.6
    for x in (-0.7, 0.7):
        cyl("Post%+.1f" % x, (x, H1 / 2, -0.05), 0.04, H1, "galvanised", verts=12)
    span("Board", (-W / 2, H0, -0.02), (W / 2, H1, 0.0), "ferry_sign_blue", bevel=0.01)
    span("Edge", (-W / 2 + 0.04, H0 + 0.04, 0.0), (W / 2 - 0.04, H1 - 0.04, 0.004), "paint_white", bevel=0.0)
    span("Field", (-W / 2 + 0.07, H0 + 0.07, 0.004), (W / 2 - 0.07, H1 - 0.07, 0.008), "ferry_sign_blue", bevel=0.0)
    text("Word", "FERRY", (0.0, H1 - 0.32, 0.01), 0.36, "paint_white", rot=(90, 0, 0), extrude=0.002)
    # The boat: a hull and a cabin, white on the blue; an arrow under it.
    prism("BoatHull", [(-0.45, H0 + 0.55), (0.45, H0 + 0.55), (0.35, H0 + 0.42), (-0.35, H0 + 0.42)], 0.006, "paint_white", axis="z", centre=0.011, bevel=0.0)
    span("BoatCabin", (0.05, H0 + 0.55, 0.008), (0.3, H0 + 0.75, 0.014), "paint_white", bevel=0.0)
    prism("Arrow", [(-0.5, H0 + 0.22), (0.25, H0 + 0.22), (0.25, H0 + 0.14), (0.5, H0 + 0.27), (0.25, H0 + 0.4), (0.25, H0 + 0.32), (-0.5, H0 + 0.32)],
          0.006, "paint_white", axis="z", centre=0.011, bevel=0.0)


@model("ferry_chain")
def ferry_chain():
    """[stage 7] A chain ferry's chain where it leaves the boat: from the origin out along +z and down into the water
    (9 m, falling 1 in 2.4), links of 20 mm bar 160 mm long, each turned a quarter to the last (the boat has four)."""
    n = 56
    step = 0.16
    for k in range(n):
        z = k * step
        y = -z / 2.4
        if k % 2 == 0:
            span("Link%d" % k, (-0.07, y - 0.012, z), (0.07, y + 0.012, z + step + 0.03), "steel_dark", bevel=0.006)
        else:
            span("Link%d" % k, (-0.012, y - 0.07, z), (0.012, y + 0.07, z + step + 0.03), "steel_dark", bevel=0.006)
    # The guide wheel's housing it leaves the deck's end by.
    span("Guide", (-0.32, -0.45, -0.3), (0.32, 0.25, 0.3), "ferry_teal", bevel=0.03)
    cyl("Wheel", (0.0, -0.1, 0.0), 0.28, 0.2, "steel_dark", axis="x", verts=20, bevel=0.01)


@model("ferry_turntable")
def ferry_turntable():
    """[stage 7] A turntable ferry's turning deck (Glenelg's): a steel disc 6.8 m across over the car deck, its rim
    painted, its centre pivot plate (+y up; its top 12 mm over the deck)."""
    R = 3.4
    cyl("Disc", (0.0, 0.006, 0.0), R, 0.012, "ferry_deck", axis="y", verts=64, bevel=0.0)
    cyl("RimPaint", (0.0, 0.0125, 0.0), R - 0.05, 0.002, "paint_yellow", axis="y", verts=64, bevel=0.0)
    cyl("RimInner", (0.0, 0.0135, 0.0), R - 0.2, 0.002, "ferry_deck", axis="y", verts=64, bevel=0.0)
    cyl("Pivot", (0.0, 0.016, 0.0), 0.6, 0.008, "steel_dark", axis="y", verts=32, bevel=0.002)


@model("ferry_queue")
def ferry_queue():
    """[stage 5] The queue sign up the approach: a white board on one post, FERRY QUEUE and a little boat, black (+z its face)."""
    W, H0, H1 = 1.0, 1.4, 2.3
    cyl("Post", (0.0, H1 / 2, -0.05), 0.04, H1, "galvanised", verts=12)
    span("Board", (-W / 2, H0, -0.02), (W / 2, H1, 0.0), "paint_white", bevel=0.01)
    span("Edge", (-W / 2 + 0.03, H0 + 0.03, 0.0), (W / 2 - 0.03, H1 - 0.03, 0.003), "paint_black", bevel=0.0)
    span("Field", (-W / 2 + 0.06, H0 + 0.06, 0.003), (W / 2 - 0.06, H1 - 0.06, 0.006), "paint_white", bevel=0.0)
    text("Word", "FERRY", (0.0, H1 - 0.24, 0.008), 0.2, "paint_black", rot=(90, 0, 0), extrude=0.002)
    text("Word2", "QUEUE", (0.0, H1 - 0.48, 0.008), 0.2, "paint_black", rot=(90, 0, 0), extrude=0.002)
    prism("BoatHull", [(-0.25, H0 + 0.3), (0.25, H0 + 0.3), (0.19, H0 + 0.2), (-0.19, H0 + 0.2)], 0.004, "paint_black", axis="z", centre=0.009, bevel=0.0)
    span("BoatCabin", (0.03, H0 + 0.3, 0.006), (0.17, H0 + 0.42, 0.011), "paint_black", bevel=0.0)


@model("ferry_barrier")
def ferry_barrier():
    """The barrier's post (its pivot 1.0 m up, the arm reaching along +x) and a red and green signal over it."""
    span("Housing", (-0.2, 0.0, -0.2), (0.2, 1.1, 0.2), "paint_white", bevel=0.02)
    span("HousingBand", (-0.205, 0.75, -0.205), (0.205, 0.9, 0.205), "paint_red", bevel=0.0)
    span("Foot", (-0.3, -0.1, -0.3), (0.3, 0.02, 0.3), "slip_concrete", bevel=0.01)
    cyl("SignalPost", (-0.12, 1.8, 0.0), 0.04, 1.4, "galvanised", verts=12)
    span("SignalBox", (-0.25, 2.25, -0.09), (0.01, 2.85, 0.09), "paint_black", bevel=0.02)
    cyl("SignalRed", (-0.12, 2.68, 0.1), 0.075, 0.03, "red_lens", axis="z", verts=16, bevel=0.0)
    cyl("SignalGreen", (-0.12, 2.42, 0.1), 0.075, 0.03, "ferry_green_lens", axis="z", verts=16, bevel=0.0)


@model("ferry_barrier_arm")
def ferry_barrier_arm():
    """The arm, 5 m, from its pivot at the origin along +x: red and white, a little skirt."""
    n = 10
    for k in range(n):
        x0 = 0.1 + k * 0.5
        span("Arm%d" % k, (x0, -0.05, -0.04), (x0 + 0.5, 0.05, 0.04), "paint_red" if k % 2 == 0 else "paint_white", bevel=0.01)
    cyl("Hub", (0.0, 0.0, 0.0), 0.12, 0.12, "steel_dark", axis="z", verts=16, bevel=0.01)
    span("Counter", (-0.6, -0.08, -0.05), (-0.1, 0.08, 0.05), "steel_dark", bevel=0.01)


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
