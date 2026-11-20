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

def section(w, y0, eaves, top, n=7, tumble=0.06):
    """A body's cross-section (x, y): sides from y0 up to the eaves (tucked in a little at the foot), a curved roof."""
    hw = w / 2
    pts = [(-hw + tumble, y0), (hw - tumble, y0), (hw, y0 + 0.5), (hw, eaves)]
    for k in range(1, n):
        a = math.pi / 2 * k / n
        pts.append((hw * math.cos(a) * 0.92 + 0.08 * hw * (1 - k / n), eaves + (top - eaves) * math.sin(a)))
    pts.append((0.0, top))
    left = [(-x, y) for (x, y) in reversed(pts[3:-1])]
    return pts + left[:-0 or None] + [(-hw, eaves), (-hw, y0 + 0.5)]


def shell(name, w, z0, z1, y0, eaves, top, mat):
    prism(name, section(w, y0, eaves, top), z1 - z0, mat, axis="z", centre=(z0 + z1) / 2, bevel=0.02)


def bogie(name, zc, wb=2.6, r=WHEEL_R, frame="train_bogie"):
    """A two-axle bogie centred at zc: its frame, springs and four wheels."""
    span(name + "_Frame", (-1.0, 0.45, zc - wb / 2 - 0.5), (1.0, 0.75, zc + wb / 2 + 0.5), frame, bevel=0.02)
    span(name + "_Bolster", (-1.1, 0.7, zc - 0.35), (1.1, 0.95, zc + 0.35), frame, bevel=0.02)
    for dz in (-wb / 2, wb / 2):
        for sx in (-1, 1):
            cyl("%s_W%+d%+.1f" % (name, sx, dz), (sx * (GAUGE / 2 + 0.03), r, zc + dz), r, 0.13, "train_wheel", axis="x", verts=20)
            cyl("%s_Box%+d%+.1f" % (name, sx, dz), (sx * 1.02, r, zc + dz), 0.12, 0.2, frame, axis="x", verts=10)
        cyl("%s_Axle%+.1f" % (name, dz), (0.0, r, zc + dz), 0.08, GAUGE + 0.3, "train_bogie", axis="x", verts=10)
    for sx in (-1, 1):
        cyl("%s_Spring%+d" % (name, sx), (sx * 0.85, 0.83, zc), 0.12, 0.25, "train_spring", verts=10)


def underframe(name, length, w, y0=0.55, y1=FLOOR, gap=None):
    """Equipment boxes under the floor between the bogies (gap: their reach from the middle)."""
    g = gap if gap is not None else length * 0.28
    for k, (z0, z1) in enumerate(((-g, -g * 0.35), (-g * 0.25, g * 0.3), (g * 0.4, g))):
        span("%s_%d" % (name, k), (-w * 0.38, y0 + 0.05 * k, z0), (w * 0.38, y1, z1), "train_under", bevel=0.02)


def window_band(name, w, z0, z1, y0, y1, count, gap=0.25, mat="train_window"):
    """A row of side windows on both sides from z0 to z1 (flush glass a hair proud of the body)."""
    pitch = (z1 - z0) / count
    for sx in (-1, 1):
        for k in range(count):
            za, zb = z0 + k * pitch + gap / 2, z0 + (k + 1) * pitch - gap / 2
            span("%s_%+d_%d" % (name, sx, k), (sx * (w / 2) - 0.012, y0, za), (sx * (w / 2) + 0.012, y1, zb), mat, bevel=0.0)


def doors(name, w, zs, y0, y1, width=1.3, mat="train_livery_b"):
    """Sliding doors (a contrasting panel with a window) on both sides at each z."""
    for sx in (-1, 1):
        for k, z in enumerate(zs):
            span("%s_%+d_%d" % (name, sx, k), (sx * (w / 2) - 0.015, y0, z - width / 2), (sx * (w / 2) + 0.015, y1, z + width / 2), mat, bevel=0.0)
            span("%s_%+d_%dW" % (name, sx, k), (sx * (w / 2) - 0.02, y0 + 0.95, z - width / 2 + 0.15), (sx * (w / 2) + 0.02, y1 - 0.25, z + width / 2 - 0.15), "train_window", bevel=0.0)


def lamps(name, z, w, y=1.45, spread=0.55):
    """A cab end's lamps facing +z (or -z with z < 0): white head lamps inboard, red tail lamps outboard."""
    f = 1 if z > 0 else -1
    for sx in (-1, 1):
        cyl("%s_Head%+d" % (name, sx), (sx * spread, y, z + f * 0.01), 0.11, 0.06, "train_lamp_head", axis="z", verts=16, bevel=0.0)
        cyl("%s_Tail%+d" % (name, sx), (sx * (w / 2 - 0.25), y, z + f * 0.01), 0.07, 0.06, "train_lamp_tail", axis="z", verts=12, bevel=0.0)


def buffers(name, z, y=1.05, sep=1.75):
    f = 1 if z > 0 else -1
    span(name + "_Beam", (-1.35, 0.85, z - f * 0.3 - 0.1), (1.35, 1.25, z - f * 0.3 + 0.1), "train_bogie", bevel=0.02)
    for sx in (-1, 1):
        cyl("%s_B%+d" % (name, sx), (sx * sep / 2, y, z - f * 0.2), 0.09, 0.4, "train_bogie", axis="z", verts=12)
        cyl("%s_H%+d" % (name, sx), (sx * sep / 2, y, z - f * 0.02), 0.2, 0.04, "steel_dark", axis="z", verts=16)
    span(name + "_Hook", (-0.08, y - 0.06, z - f * 0.25 - 0.1), (0.08, y + 0.06, z - f * 0.25 + 0.1), "steel_dark", bevel=0.01)


def unit_cab(name, w, zc, y0, top, nose=2.2, rake=0.9, mat="train_livery_a"):
    """A unit's cab end at zc (its front face): a raked nose (profile across the width), its windscreen and lamps."""
    f = 1 if zc > 0 else -1
    zb = zc - f * nose
    prof = [(zb, y0), (zc, y0), (zc, y0 + 0.9), (zc - f * rake * 0.35, y0 + 1.6), (zc - f * rake, top - 0.35), (zb, top)]
    if f < 0:
        prof = [(z, y) for (z, y) in prof]
    prism(name + "_Nose", prof, w - 0.04, mat, axis="x", bevel=0.05)
    # (the windscreen on the raked face, the yellow warning panel under it)
    zs0, ys0 = zc - f * rake * 0.35, y0 + 1.6
    zs1, ys1 = zc - f * rake, top - 0.4
    mid = ((zs0 + zs1) / 2, (ys0 + ys1) / 2)
    ang = math.degrees(math.atan2(zs1 - zs0, ys1 - ys0))
    box(name + "_Screen", (0.0, mid[1], mid[0] + f * 0.02), (w - 0.5, math.hypot(zs1 - zs0, ys1 - ys0) - 0.1, 0.03), "train_cab_glass", bevel=0.0, rot=(ang, 0, 0))
    span(name + "_Warn", (-(w / 2) + 0.12, y0 + 0.25, zc - f * 0.01 - 0.01), ((w / 2) - 0.12, y0 + 0.88, zc + f * 0.01 + 0.0), "train_warning", bevel=0.0)
    lamps(name, zc + f * 0.01, w, y=y0 + 0.55)
    span(name + "_Coupler", (-0.2, 0.85, zc - f * 0.1), (0.2, 1.15, zc + f * 0.25), "steel_dark", bevel=0.02)


def pantograph(name, z, top):
    span(name + "_Base", (-0.6, top, z - 0.8), (0.6, top + 0.15, z + 0.8), "train_bogie", bevel=0.02)
    for sx in (-1, 1):
        cyl("%s_Ins%+d" % (name, sx), (sx * 0.45, top + 0.25, z), 0.06, 0.3, "train_insulator", verts=10)
    kit.tube(name + "_Arm1", [(0.0, top + 0.2, z - 0.7), (0.0, top + 0.85, z + 0.2)], 0.035, "train_bogie", sides=8)
    kit.tube(name + "_Arm2", [(0.0, top + 0.85, z + 0.2), (0.0, top + 1.45, z - 0.3)], 0.03, "train_bogie", sides=8)
    span(name + "_Head", (-0.85, top + 1.42, z - 0.4), (0.85, top + 1.5, z - 0.2), "steel_dark", bevel=0.01)


# --- The units --------------------------------------------------------------------------------------------------------

def unit_car(pid, cab, pan=False, emu=False):
    L, bc, W, H = SPECS[pid]
    hz = L / 2
    eaves = 3.0
    nose = 2.3 if not emu else 1.6
    z1 = hz - nose if cab else hz
    shell("Body", W, -hz, z1, FLOOR, eaves, H, "train_livery_a")
    # (the band along its windows, the skirt)
    for sx in (-1, 1):
        span("Band%+d" % sx, (sx * W / 2 - 0.01, 1.62, -hz + 0.05), (sx * W / 2 + 0.01, 1.78, z1), "train_livery_b", bevel=0.0)
    span("Roof", (-W / 2 + 0.35, H - 0.04, -hz + 0.1), (W / 2 - 0.35, H + 0.01, z1 - 0.1), "train_roof", bevel=0.0)
    if cab:
        unit_cab("Cab", W, hz, FLOOR, H, nose=nose, rake=0.9 if not emu else 0.5)
    else:
        span("Gangway", (-0.6, FLOOR + 0.2, hz - 0.05), (0.6, 3.05, hz + 0.12), "rubber", bevel=0.03)
    span("Gangway0", (-0.6, FLOOR + 0.2, -hz - 0.12), (0.6, 3.05, -hz + 0.05), "rubber", bevel=0.03)
    # (doors at the thirds or quarters, windows between)
    dz = [-L * 0.3, L * 0.3 - (1.0 if cab else 0.0)] if not emu else [-L * 0.33, -L * 0.02, L * 0.3 - (0.8 if cab else 0.0)]
    doors("Door", W, dz, FLOOR + 0.15, 3.05, width=1.3 if not emu else 1.5)
    edges = [-hz + 0.8] + sum([[d - 0.85, d + 0.85] for d in dz], []) + [z1 - 0.5]
    for k in range(0, len(edges), 2):
        za, zb = edges[k], edges[k + 1]
        n = max(1, int(round((zb - za) / 2.2)))
        if zb - za > 1.0:
            window_band("Win%d" % k, W, za, zb, 1.95, 2.75, n, gap=0.18)
    bogie("BogieF", bc)
    bogie("BogieR", -bc)
    underframe("Under", L, W, gap=bc - 1.8)
    if pan:
        pantograph("Pan", -L * 0.1, H)
    if not emu:
        # (a diesel unit's roof exhaust and its radiator grille under the floor)
        cyl("Exhaust", (0.55, H + 0.15, L * 0.05), 0.07, 0.4, "steel_dark", verts=10)
        span("Radiator", (-0.9, 0.6, -L * 0.12), (0.9, 1.05, L * 0.02), "train_grille", bevel=0.01)


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
    L, bc, W, H = SPECS["train_ic_power"]
    hz = L / 2
    nose = 5.5
    shell("Body", W, -hz, hz - nose, FLOOR, 3.05, H, "train_livery_a")
    # (the long nose: its profile pushed across the width, the windscreen high on it)
    zb = hz - nose
    prof = [(zb, FLOOR), (hz, FLOOR), (hz, FLOOR + 0.55), (hz - 0.6, FLOOR + 1.15), (hz - 2.0, FLOOR + 1.75), (hz - 3.6, H - 0.25), (zb, H)]
    prism("Nose", prof, W - 0.06, "train_livery_a", axis="x", bevel=0.08)
    box("Screen", (0.0, FLOOR + 2.05, hz - 2.8), (W - 0.7, 1.05, 0.03), "train_cab_glass", bevel=0.0, rot=(-62, 0, 0))
    span("Warn", (-0.9, FLOOR + 0.15, hz - 0.02), (0.9, FLOOR + 0.5, hz + 0.01), "train_warning", bevel=0.0)
    lamps("Lamp", hz - 0.17, W - 0.4, y=FLOOR + 0.75, spread=0.6)
    for sx in (-1, 1):
        span("Band%+d" % sx, (sx * W / 2 - 0.01, 1.55, -hz + 0.1), (sx * W / 2 + 0.01, 1.85, zb), "train_livery_b", bevel=0.0)
        # (the engine room's grilles and the cab's side window)
        for k in range(3):
            span("Grille%+d_%d" % (sx, k), (sx * W / 2 - 0.015, 2.2, -hz + 2.0 + k * 3.2), (sx * W / 2 + 0.015, 3.0, -hz + 4.6 + k * 3.2), "train_grille", bevel=0.0)
        span("CabWin%+d" % sx, (sx * W / 2 - 0.012, 2.05, zb - 1.3), (sx * W / 2 + 0.012, 2.8, zb - 0.2), "train_cab_glass", bevel=0.0)
    span("Gangway0", (-0.6, FLOOR + 0.2, -hz - 0.1), (0.6, 3.0, -hz + 0.05), "rubber", bevel=0.03)
    span("Roof", (-W / 2 + 0.4, H - 0.04, -hz + 0.2), (W / 2 - 0.4, H + 0.02, zb), "train_roof", bevel=0.0)
    bogie("BogieF", bc, wb=2.6)
    bogie("BogieR", -bc, wb=2.6)
    underframe("Under", L, W, gap=bc - 1.8)


@model("train_ic_coach")
def train_ic_coach():
    L, bc, W, H = SPECS["train_ic_coach"]
    hz = L / 2
    shell("Body", W, -hz, hz, FLOOR, 3.05, H, "train_livery_a")
    for sx in (-1, 1):
        span("Band%+d" % sx, (sx * W / 2 - 0.01, 1.55, -hz + 0.05), (sx * W / 2 + 0.01, 1.85, hz - 0.05), "train_livery_b", bevel=0.0)
    doors("Door", W, [-hz + 1.2, hz - 1.2], FLOOR + 0.15, 3.05, width=0.9)
    window_band("Win", W, -hz + 2.1, hz - 2.1, 1.95, 2.8, 8, gap=0.3)
    span("Roof", (-W / 2 + 0.4, H - 0.04, -hz + 0.1), (W / 2 - 0.4, H + 0.01, hz - 0.1), "train_roof", bevel=0.0)
    for z in (-hz, hz):
        f = 1 if z > 0 else -1
        span("Gangway%+d" % f, (-0.6, FLOOR + 0.2, z - 0.06), (0.6, 3.05, z + 0.06), "rubber", bevel=0.03)
    bogie("BogieF", bc)
    bogie("BogieR", -bc)
    underframe("Under", L, W, gap=bc - 1.8)


# --- Freight ----------------------------------------------------------------------------------------------------------

@model("train_freight_loco")
def train_freight_loco():
    L, bc, W, H = SPECS["train_freight_loco"]
    hz = L / 2
    shell("Body", W, -hz + 1.0, hz - 1.0, FLOOR, 3.15, H, "train_livery_a")
    for z in (hz, -hz):
        f = 1 if z > 0 else -1
        # (a cab at each end: a flat front a little raked, its two windows, the lamps, the buffers)
        prof = [(z - f * 1.2, FLOOR), (z - f * 0.05, FLOOR), (z - f * 0.05, FLOOR + 1.3), (z - f * 0.35, H - 0.3), (z - f * 1.2, H)]
        prism("Cab%+d" % f, prof, W - 0.04, "train_livery_a", axis="x", bevel=0.05)
        for sx in (-1, 1):
            box("Screen%+d%+d" % (f, sx), (sx * 0.55, FLOOR + 1.9, z - f * 0.2), (0.85, 0.7, 0.03), "train_cab_glass", bevel=0.0, rot=(f * -12, 0, 0))
        span("Warn%+d" % f, (-W / 2 + 0.1, FLOOR + 0.2, z - f * 0.06 - 0.01), (W / 2 - 0.1, FLOOR + 1.2, z - f * 0.06 + 0.01), "train_warning", bevel=0.0)
        lamps("Lamp%+d" % f, z - f * 0.07, W, y=FLOOR + 0.7)
        buffers("Buf%+d" % f, z)
    for sx in (-1, 1):
        span("Band%+d" % sx, (sx * W / 2 - 0.01, 1.4, -hz + 1.0), (sx * W / 2 + 0.01, 1.75, hz - 1.0), "train_livery_b", bevel=0.0)
        for k in range(4):
            span("Grille%+d_%d" % (sx, k), (sx * W / 2 - 0.015, 2.1, -hz + 3.0 + k * 3.8), (sx * W / 2 + 0.015, 3.0, -hz + 5.6 + k * 3.8), "train_grille", bevel=0.0)
        span("CabDoor%+d" % sx, (sx * W / 2 - 0.015, FLOOR + 0.2, hz - 2.2), (sx * W / 2 + 0.015, 3.1, hz - 1.4), "train_livery_b", bevel=0.0)
    for k in range(3):
        cyl("Fan%d" % k, (0.0, H + 0.02, -3.0 + k * 2.4), 0.55, 0.06, "train_grille", verts=20)
    cyl("Exhaust", (0.0, H + 0.12, 2.5), 0.12, 0.25, "steel_dark", verts=12)
    # (three-axle bogies, the fuel tank between)
    for zc in (bc, -bc):
        span("Frame%+.0f" % zc, (-1.05, 0.45, zc - 2.4), (1.05, 0.8, zc + 2.4), "train_bogie", bevel=0.03)
        for dz in (-1.9, 0.0, 1.9):
            for sx in (-1, 1):
                cyl("W%+.0f%+.1f%+d" % (zc, dz, sx), (sx * (GAUGE / 2 + 0.03), 0.53, zc + dz), 0.53, 0.13, "train_wheel", axis="x", verts=20)
    span("Tank", (-1.1, 0.5, -3.6), (1.1, FLOOR, 3.6), "train_under", bevel=0.08)


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


@model("train_wagon_hopper")
def train_wagon_hopper():
    L, bc, W, H = SPECS["train_wagon_hopper"]
    hz = L / 2
    flat_deck("train_wagon_hopper")
    # (the hopper: slab sides, sloped ends down to the doors, a hood over it)
    prof = [(-hz + 0.6, 3.3), (-hz + 0.6, 2.2), (-hz + 3.0, 1.0), (hz - 3.0, 1.0), (hz - 0.6, 2.2), (hz - 0.6, 3.3)]
    prism("Hopper", prof, W - 0.1, "train_hopper", axis="x", bevel=0.03)
    prism("Hood", [(-1.35, 3.3), (1.35, 3.3), (0.9, H), (-0.9, H)], L - 1.4, "train_hopper", axis="z", bevel=0.03)
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
    lamps("LampR", -hz - 0.05, W, y=1.7, spread=1.2)


@model("train_diesel_old")
def train_diesel_old():
    L, bc, W, H = SPECS["train_diesel_old"]
    hz = L / 2
    shell("Body", W, -hz + 0.6, hz - 0.6, 1.2, 3.1, H, "train_livery_a")
    for z in (hz, -hz):
        f = 1 if z > 0 else -1
        prof = [(z - f * 2.0, 1.2), (z - f * 0.3, 1.2), (z - f * 0.3, 2.4), (z - f * 0.7, H - 0.2), (z - f * 2.0, H)]
        prism("Nose%+d" % f, prof, W - 0.06, "train_livery_a", axis="x", bevel=0.08)
        for sx in (-1, 1):
            box("Screen%+d%+d" % (f, sx), (sx * 0.55, 2.85, z - f * 0.5), (0.8, 0.55, 0.03), "train_cab_glass", bevel=0.0, rot=(f * -28, 0, 0))
        span("Panel%+d" % f, (-0.8, 1.5, z - f * 0.3 - 0.01), (0.8, 2.35, z - f * 0.3 + 0.01), "train_warning", bevel=0.0)
        lamps("Lamp%+d" % f, z - f * 0.31, W, y=1.9, spread=0.7)
        buffers("Buf%+d" % f, z)
    for sx in (-1, 1):
        span("Band%+d" % sx, (sx * W / 2 - 0.01, 1.7, -hz + 0.6), (sx * W / 2 + 0.01, 1.85, hz - 0.6), "train_livery_b", bevel=0.0)
        for k in range(5):
            span("Louvre%+d_%d" % (sx, k), (sx * W / 2 - 0.015, 2.3, -hz + 3.2 + k * 2.4), (sx * W / 2 + 0.015, 2.95, -hz + 4.6 + k * 2.4), "train_grille", bevel=0.0)
    for zc in (bc, -bc):
        span("Frame%+.0f" % zc, (-1.05, 0.45, zc - 2.2), (1.05, 0.8, zc + 2.2), "paint_black", bevel=0.03)
        for dz in (-1.7, 0.0, 1.7):
            for sx in (-1, 1):
                cyl("W%+.0f%+.1f%+d" % (zc, dz, sx), (sx * (GAUGE / 2 + 0.03), 0.5, zc + dz), 0.5, 0.13, "train_wheel", axis="x", verts=20)
    span("Tank", (-1.1, 0.55, -2.2), (1.1, 1.2, 2.2), "paint_black", bevel=0.06)


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
