"""Builds the shed's own kit as glbs in Blender, one per prop, into Data/Props/Blender/<id>.glb.

    blender -b --factory-startup -P Tools/props/make_props.py                 every Blender prop
    blender -b --factory-startup -P Tools/props/make_props.py -- tyre_changer  just these
    blender -b --factory-startup -P Tools/props/make_props.py -- --list        what there is

These are the machines and fittings no free scan library has at Torque Shed's sizes (a tyre changer,
parts washers, engine stands, repair benches, the shed's shelving whose boards the parts sit on).
Each is drawn to its Torque Shed model's footprint (Equipment.MODELS size, workshop.gd's fixed kit)
and work height, so the places parts go on it still line up; in Torque Shed's local frame (see
kit.py), the numbers read straight across from equipment_view.gd. Deterministic: the same script
always writes the same glbs, so they're committed and the PC never needs Blender.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kit  # noqa: E402
import delivery_layout  # noqa: E402
from kit import box, span, cyl, tube, bent, torus, prism, castor, label, sphere  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "Data", "Props", "Blender")
MODELS = {}


def model(pid, size):
    """Registers a prop builder; `size` is Torque Shed's footprint (width x, height y, depth z), m."""
    def wrap(fn):
        MODELS[pid] = (fn, size)
        return fn
    return wrap


# --- Pieces several props share ------------------------------------------------------------------------

def drawer_bank(name, x0, x1, y0, y1, z, rows, mat, handle="chrome", cols=1):
    """A front of drawers between x0..x1 and y0..y1 at depth z (the front face), each with its pull."""
    gap = 0.006
    h = (y1 - y0) / rows
    w = (x1 - x0) / cols
    for c in range(cols):
        for r in range(rows):
            cx = x0 + w * (c + 0.5)
            cy = y0 + h * (r + 0.5)
            box("%s_%d_%d" % (name, c, r), (cx, cy, z + 0.009), (w - gap, h - gap, 0.018), mat, bevel=0.003)
            tube("%s_%d_%dP" % (name, c, r), bent([(cx - w * 0.3, cy + h * 0.28, z + 0.018), (cx - w * 0.3, cy + h * 0.28, z + 0.035),
                                                   (cx + w * 0.3, cy + h * 0.28, z + 0.035), (cx + w * 0.3, cy + h * 0.28, z + 0.018)], 0.01),
                 0.006, handle)


def square_tube_frame(name, x0, x1, z0, z1, y0, y1, post, mat):
    """Four legs and a top rail ring: a welded steel bench frame."""
    for x in (x0 + post / 2, x1 - post / 2):
        for z in (z0 + post / 2, z1 - post / 2):
            box("%s_Leg%.2f%.2f" % (name, x, z), (x, (y0 + y1) / 2, z), (post, y1 - y0, post), mat, bevel=0.003)
            box("%s_Foot%.2f%.2f" % (name, x, z), (x, 0.006, z), (post + 0.02, 0.012, post + 0.02), "rubber", bevel=0.002)
    for z in (z0 + post / 2, z1 - post / 2):
        box("%s_RailX%.2f" % (name, z), ((x0 + x1) / 2, y1 - post / 2, z), (x1 - x0, post, post), mat, bevel=0.003)
    for x in (x0 + post / 2, x1 - post / 2):
        box("%s_RailZ%.2f" % (name, x), (x, y1 - post / 2, (z0 + z1) / 2), (post, post, z1 - z0), mat, bevel=0.003)


def wood_top(name, centre, size, mat="wood_top"):
    """A laminated hardwood worktop: strips glued side by side, so it reads as butcher block."""
    x, y, z = centre
    w, h, d = size
    n = max(3, int(d / 0.045))
    for i in range(n):
        zz = z - d / 2 + d * (i + 0.5) / n
        box("%s_%d" % (name, i), (x, y, zz), (w, h, d / n - 0.0008), mat, bevel=0.0015)


def vice(name, at, mat="paint_blue"):
    """A bench vice on the top at `at` (its jaws facing +z)."""
    x, y, z = at
    box(name + "_Base", (x, y + 0.02, z - 0.04), (0.15, 0.04, 0.16), mat, bevel=0.006)
    box(name + "_Body", (x, y + 0.08, z - 0.02), (0.1, 0.08, 0.2), mat, bevel=0.008)
    box(name + "_JawFixed", (x, y + 0.14, z - 0.05), (0.15, 0.06, 0.04), mat, bevel=0.005)
    box(name + "_JawMoving", (x, y + 0.14, z + 0.04), (0.15, 0.06, 0.04), mat, bevel=0.005)
    for dz in (-0.03, 0.06):
        box(name + "_Plate%.2f" % dz, (x, y + 0.155, z + dz - 0.0), (0.14, 0.03, 0.006), "steel_dark", bevel=0.001)
    cyl(name + "_Screw", (x, y + 0.11, z + 0.12), 0.011, 0.12, "steel", axis="z")
    cyl(name + "_Bar", (x, y + 0.11, z + 0.18), 0.006, 0.2, "chrome", axis="x")
    for s in (-1, 1):
        sphere(name + "_Knob%d" % s, (x + s * 0.1, y + 0.11, z + 0.18), 0.011, "chrome")


def grinder(name, at, body="paint_black"):
    """A bench grinder at `at` (its motor across x), wheels either end, guards and tool rests."""
    x, y, z = at
    box(name + "_Base", (x, y + 0.015, z), (0.16, 0.03, 0.14), body, bevel=0.005)
    cyl(name + "_Motor", (x, y + 0.1, z), 0.065, 0.16, body, axis="x", bevel=0.01)
    box(name + "_Switch", (x, y + 0.04, z + 0.07), (0.05, 0.02, 0.02), "paint_yellow", bevel=0.002)
    for s in (-1, 1):
        cyl(name + "_Shaft%d" % s, (x + s * 0.1, y + 0.1, z), 0.008, 0.04, "steel", axis="x")
        cyl(name + "_Wheel%d" % s, (x + s * 0.135, y + 0.1, z), 0.075, 0.022, "cast_iron" if s < 0 else "paint_grey", axis="x", bevel=0.002)
        cyl(name + "_Guard%d" % s, (x + s * 0.135, y + 0.1, z - 0.005), 0.085, 0.036, body, axis="x", bevel=0.004)
        box(name + "_Rest%d" % s, (x + s * 0.135, y + 0.05, z + 0.085), (0.04, 0.006, 0.03), "steel_dark", bevel=0.001)
        box(name + "_Shield%d" % s, (x + s * 0.135, y + 0.18, z + 0.07), (0.07, 0.05, 0.003), "glass", bevel=0.001)


def hose(name, points, radius, mat="rubber"):
    tube(name, bent(points, 0.06), radius, mat, sides=10)


# --- The shed's own fixed kit (workshop.gd) ------------------------------------------------------------

@model("shed_workbench", (0.95, 0.95, 4.8))
def shed_workbench():
    """workshop.gd _build_bench: the long bench down the west wall, its front (the drawers) to +x
    in the shed, here +z (the swap turns it). A welded steel frame, three rows of red drawer banks
    in bays, a lower shelf, a butcher-block top and a pegboard back with tools."""
    L, D, top = 4.8, 0.9, 0.945
    wood_top("Top", (0.0, 0.92, 0.0), (L + 0.05, 0.05, 0.95))
    # Turned so its length runs along x here: width L, depth D (front +z).
    for x in (-L / 2 + 0.03, -L / 6, L / 6, L / 2 - 0.03):
        for z in (-D / 2 + 0.03, D / 2 - 0.03):
            box("Leg%.2f%.2f" % (x, z), (x, 0.45, z), (0.05, 0.9, 0.05), "paint_grey", bevel=0.004)
            box("Foot%.2f%.2f" % (x, z), (x, 0.006, z), (0.07, 0.012, 0.07), "rubber", bevel=0.002)
    for z in (-D / 2 + 0.03, D / 2 - 0.03):
        box("Rail%.2f" % z, (0.0, 0.87, z), (L, 0.05, 0.05), "paint_grey", bevel=0.004)
    box("Shelf", (0.0, 0.16, 0.0), (L - 0.05, 0.025, D - 0.06), "paint_board", bevel=0.003)
    box("Back", (0.0, 0.5, -D / 2 + 0.03), (L - 0.06, 0.7, 0.01), "paint_grey", bevel=0.002)
    # Three cabinets of drawers along the front, red as Torque Shed's.
    for i, cx in enumerate((-L / 3, 0.0, L / 3)):
        w = L / 3 - 0.16
        box("Cab%d" % i, (cx, 0.53, 0.02), (w, 0.66, D - 0.1), "paint_red", bevel=0.004)
        drawer_bank("Drawer%d" % i, cx - w / 2 + 0.01, cx + w / 2 - 0.01, 0.21, 0.85, D / 2 - 0.03, 3, "paint_red", cols=2)
    # Pegboard up the wall behind it. Its tools are their own prop, pegboard_tools
    # (Tools/props/make_pegboard_tools.py), placed with the bench.
    box("Peg", (0.0, 1.3, -D / 2 + 0.01), (L * 0.6, 0.6, 0.012), "pegboard", bevel=0.002)
    vice("Vice", (L / 2 - 0.35, 0.945, D / 2 - 0.15))


@model("parts_computer", (0.8, 1.35, 1.35))
def parts_computer():
    """workshop.gd _build_computer: the desk with the monitor, keyboard and mouse, the tower under it.
    Its front (where you stand) is +z here; Torque Shed's desk is 0.75 x 1.3 with the screen across it."""
    W, D = 1.35, 0.8
    box("DeskTop", (0.0, 0.755, 0.0), (W, 0.03, D), "plywood", bevel=0.003)
    # The edge banding wraps the top's front 2 mm proud (flush, it z-fought the top).
    box("DeskEdge", (0.0, 0.755, D / 2 - 0.002), (W + 0.004, 0.034, 0.008), "plastic_black", bevel=0.001)
    for x in (-W / 2 + 0.04, W / 2 - 0.04):
        box("Panel%.1f" % x, (x, 0.37, 0.0), (0.025, 0.74, D - 0.04), "paint_grey", bevel=0.003)
    box("Modesty", (0.0, 0.5, -D / 2 + 0.05), (W - 0.1, 0.4, 0.018), "paint_grey", bevel=0.002)
    # Monitor: a 27" panel on a stand.
    box("MonBase", (0.0, 0.775, -0.1), (0.24, 0.012, 0.18), "plastic_black", bevel=0.004)
    box("MonNeck", (0.0, 0.9, -0.14), (0.05, 0.25, 0.03), "aluminium", bevel=0.006)
    box("MonBack", (0.0, 1.1, -0.12), (0.64, 0.38, 0.035), "plastic_black", bevel=0.008)
    box("MonScreen", (0.0, 1.1, -0.101), (0.6, 0.34, 0.002), "screen", bevel=0.0)
    box("Keyboard", (0.0, 0.775, 0.12), (0.44, 0.014, 0.14), "plastic_black", bevel=0.004)
    for r in range(5):
        box("Keys%d" % r, (0.0, 0.784, 0.07 + r * 0.025), (0.42, 0.005, 0.018), "plastic_grey", bevel=0.001)
    box("Mouse", (0.3, 0.777, 0.12), (0.06, 0.02, 0.1), "plastic_black", bevel=0.009)
    box("Pad", (0.3, 0.771, 0.12), (0.24, 0.003, 0.2), "foam_black", bevel=0.001)
    # The tower on the floor under the desk.
    box("Tower", (W / 2 - 0.2, 0.23, -0.05), (0.2, 0.45, 0.45), "plastic_black", bevel=0.006)
    box("TowerFront", (W / 2 - 0.2, 0.23, 0.176), (0.19, 0.44, 0.003), "steel_dark", bevel=0.0)
    box("TowerLed", (W / 2 - 0.2, 0.42, 0.178), (0.06, 0.004, 0.002), "display_green", bevel=0.0)


def _wheelie_bin(name, body, label_mat="label_white"):
    """Torque Shed's bin: BIN_SIZE 0.52 x 0.9 x 0.6; a tapering body, lid with the hinge and handle
    at the back (+z here is the front, the label side), the wheels at the back."""
    W, H, D = 0.52, 0.9, 0.6
    outline = [(-D / 2 + 0.04, 0.05), (D / 2 - 0.05, 0.05), (D / 2, H - 0.05), (-D / 2, H - 0.05)]
    prism(name + "_Body", outline, W - 0.02, body, axis="x", bevel=0.012)
    box(name + "_Lid", (0.0, H - 0.025, 0.0), (W + 0.02, 0.04, D + 0.04), body, bevel=0.01)
    box(name + "_LidLip", (0.0, H - 0.06, D / 2 + 0.01), (W, 0.04, 0.02), body, bevel=0.005)
    box(name + "_Handle", (0.0, H - 0.07, -D / 2 - 0.035), (W - 0.06, 0.035, 0.035), body, bevel=0.008)
    cyl(name + "_Axle", (0.0, 0.1, -D / 2 + 0.02), 0.012, W + 0.04, "steel", axis="x")
    for s in (-1, 1):
        cyl(name + "_Wheel%d" % s, (s * (W / 2 + 0.005), 0.1, -D / 2 + 0.02), 0.1, 0.05, "rubber", axis="x", bevel=0.01)
        cyl(name + "_Hub%d" % s, (s * (W / 2 + 0.008), 0.1, -D / 2 + 0.02), 0.04, 0.052, "plastic_grey", axis="x")
    box(name + "_Foot", (0.0, 0.03, D / 2 - 0.06), (W - 0.1, 0.06, 0.08), body, bevel=0.01)
    # The printed label (Tools/props/bin_labels.py draws it, materials.json bin_label_<kind>), lying
    # on the sloping front: 5 cm lean over the body's 80 cm.
    lean = math.atan2(0.05, H - 0.1)
    front = D / 2 - 0.05 + 0.05 * (0.58 - 0.05) / (H - 0.1)
    box(name + "_Label", (0.0, 0.58, front + 0.0004 + 0.002 / math.cos(lean)), (W - 0.1, 0.26, 0.004), label_mat,
        bevel=0.001, rot=(math.degrees(lean), 0.0, 0.0))


@model("bin_general", (0.52, 0.9, 0.6))
def bin_general():
    _wheelie_bin("Bin", "paint_bin_black", "bin_label_general")


@model("bin_plastic", (0.52, 0.9, 0.6))
def bin_plastic():
    _wheelie_bin("Bin", "paint_bin_blue", "bin_label_plastic")


@model("bin_metal", (0.52, 0.9, 0.6))
def bin_metal():
    _wheelie_bin("Bin", "paint_bin_green", "bin_label_metal")


def _radio_shelf():
    box("Shelf", (0.0, -0.015, 0.0), (0.6, 0.03, 0.34), "wood_top", bevel=0.003)
    for x in (-0.22, 0.22):
        prism("Bracket%.2f" % x, [(-0.17, -0.03), (-0.17, -0.18), (-0.15, -0.18), (0.1, -0.03)], 0.025, "steel_dark", axis="x", centre=x)


@model("radio_shelf", (0.6, 0.18, 0.34))
def radio_shelf():
    """The radio's shelf and its two brackets on their own (shed_radio below without its radio): what
    a bought radio model (Data/Props/fab.json fab_radio) stands on, in the same frame as shed_radio."""
    _radio_shelf()


@model("shed_radio", (0.6, 0.45, 0.34))
def shed_radio():
    """shed_radio.gd on its shelf (workshop.gd _build_radio): a 0.46 x 0.26 x 0.16 workshop radio,
    grille cloth, tuning dial and knobs, a carry handle, on a wooden shelf with two brackets. The
    shelf is 0.6 long (x here) and 0.34 deep, its top at 0.03 above the origin; the origin is at the
    shelf's top centre at the wall (-z), like the radio's own."""
    _radio_shelf()
    y0 = 0.0
    box("Body", (0.0, y0 + 0.13, 0.02), (0.46, 0.26, 0.16), "plastic_red", bevel=0.02)
    box("Grille", (-0.1, y0 + 0.13, 0.1), (0.2, 0.18, 0.004), "fabric_grille" if False else "paper", bevel=0.001)
    for i in range(5):
        box("Bar%d" % i, (-0.1, y0 + 0.06 + i * 0.035, 0.103), (0.2, 0.006, 0.004), "chrome", bevel=0.0)
    box("Dial", (0.12, y0 + 0.17, 0.101), (0.17, 0.07, 0.004), "paint_cream", bevel=0.001)
    box("DialGlass", (0.12, y0 + 0.17, 0.104), (0.165, 0.065, 0.002), "glass", bevel=0.0)
    for x in (0.07, 0.17):
        cyl("Knob%.2f" % x, (x, y0 + 0.08, 0.11), 0.018, 0.02, "plastic_black", axis="z", bevel=0.003)
    tube("Handle", bent([(-0.18, y0 + 0.26, 0.02), (-0.18, y0 + 0.31, 0.02), (0.18, y0 + 0.31, 0.02), (0.18, y0 + 0.26, 0.02)], 0.03), 0.009, "plastic_black")
    cyl("Aerial", (0.2, y0 + 0.4, -0.04), 0.003, 0.3, "chrome", rot=(0, 0, -15))


@model("whiteboard", (2.47, 1.27, 0.05))
def whiteboard():
    """The crew whiteboard (Equipment 'whiteboard', 2.47 x 1.27): aluminium frame, a pen tray, the
    origin at its middle on the wall (back face at -z 0)."""
    W, H = 2.47, 1.27
    box("Board", (0.0, 0.0, 0.012), (W - 0.04, H - 0.04, 0.012), "whiteboard", bevel=0.0)
    box("Back", (0.0, 0.0, 0.004), (W, H, 0.008), "paint_grey", bevel=0.0)
    for y in (-H / 2 + 0.012, H / 2 - 0.012):
        box("FrameH%.1f" % y, (0.0, y, 0.016), (W, 0.024, 0.024), "aluminium", bevel=0.004)
    for x in (-W / 2 + 0.012, W / 2 - 0.012):
        box("FrameV%.1f" % x, (x, 0.0, 0.016), (0.024, H, 0.024), "aluminium", bevel=0.004)
    for x in (-W / 2, W / 2):
        box("Corner%.1f" % x, (x * 0.995, -H / 2 + 0.012, 0.018), (0.03, 0.03, 0.03), "plastic_grey", bevel=0.006)
    prism("Tray", [(0.0, 0.0), (0.06, 0.0), (0.07, 0.025), (0.06, 0.012), (0.0, 0.012)], W * 0.5, "aluminium", axis="x", centre=0.0, bevel=0.002)
    # (the tray's outline is (z, y) about the bottom edge: move it down)
    tray = kit._pieces[-1]
    tray.location.z -= H / 2 + 0.01
    for i, c in enumerate(("plastic_black", "plastic_red", "paint_blue")):
        cyl("Pen%d" % i, (-0.3 + i * 0.15, -H / 2 - 0.0, 0.05), 0.008, 0.13, c, axis="x", bevel=0.002)


@model("light_switches", (0.14, 0.24, 0.03))
def light_switches():
    """The switch boxes by the roller door (workshop.gd _build_door_and_switches): a yellow
    industrial box for the door with up and down buttons, and a light switch, on the wall."""
    box("DoorBox", (0.0, 0.07, 0.02), (0.12, 0.13, 0.04), "plastic_yellow", bevel=0.006)
    for y, c in ((0.1, "plastic_black"), (0.05, "plastic_black")):
        cyl("Btn%.2f" % y, (0.0, y, 0.045), 0.014, 0.012, c, axis="z", bevel=0.002)
    box("Arrow", (0.0, 0.1, 0.052), (0.01, 0.01, 0.002), "label_white", bevel=0.0)
    box("LightPlate", (0.0, -0.06, 0.006), (0.086, 0.086, 0.012), "plastic_white", bevel=0.003)
    box("Rocker", (0.0, -0.06, 0.014), (0.02, 0.035, 0.008), "plastic_white", bevel=0.002)
    tube("Conduit", [(0.0, 0.135, 0.015), (0.0, 0.6, 0.015)], 0.01, "plastic_grey")


# --- Floor equipment (Equipment.MODELS, drawn as equipment_view.gd) -----------------------------------

def _engine_stand(folding):
    """A red engine stand on castors (equipment_view.gd _build_stand): a T base, a column at x 0.25,
    a turning head with a four-arm mounting plate that the block bolts to (the place at 0.72),
    the crank handle on its gearbox and the grab handle."""
    paint = "paint_blue" if folding else "paint_red"
    reach = 0.25 if folding else 0.32
    leg = 0.6 if folding else 0.8
    box("BaseX", (0.0, 0.05, 0.0), (reach * 2.0, 0.06, 0.07), paint, bevel=0.005)
    box("BaseZ", (0.25, 0.05, 0.0), (0.07, 0.06, leg), paint, bevel=0.005)
    for p in ((-reach + 0.03, 0.0), (0.25, -leg / 2 + 0.04), (0.25, leg / 2 - 0.04)):
        castor("Castor%.2f%.2f" % p, p, 0.03)
    box("Column", (0.25, 0.32, 0.0), (0.08, 0.5, 0.08), paint, bevel=0.005)
    box("Sleeve", (0.25, 0.56, 0.0), (0.095, 0.06, 0.095), paint, bevel=0.004)
    cyl("Pin", (0.25, 0.56, 0.06), 0.008, 0.06, "chrome", axis="z")
    box("Arm", (0.15, 0.62, 0.0), (0.22, 0.06, 0.06), paint, bevel=0.005)
    cyl("Bearing", (0.04, 0.62, 0.0), 0.05, 0.04, "steel", axis="x", bevel=0.004)
    cyl("Plate", (0.01, 0.62, 0.0), 0.06, 0.02, "steel", axis="x", bevel=0.003)
    for i in range(4):
        a = math.radians(45 + 90 * i)
        box("MountArm%d" % i, (0.0, 0.62 + math.sin(a) * 0.08, math.cos(a) * 0.08), (0.02, 0.025, 0.17), "steel", bevel=0.003,
            rot=(-math.degrees(a), 0, 0))
    box("Gearbox", (0.25, 0.62, 0.07), (0.07, 0.07, 0.05), "steel_dark", bevel=0.005)
    tube("Crank", bent([(0.25, 0.62, 0.095), (0.25, 0.62, 0.13), (0.25, 0.53, 0.13), (0.25, 0.53, 0.17)], 0.01), 0.007, "chrome")
    # The steering handle: a post up out of the column top and a T bar across it.
    tube("HandlePost", [(0.25, 0.59, -0.02), (0.25, 0.75, -0.02)], 0.012, paint, sides=10)
    cyl("HandleBar", (0.25, 0.75, -0.02), 0.012, 0.42, "steel", axis="z")
    for s in (-1, 1):
        cyl("Grip%d" % s, (0.25, 0.75, -0.02 + s * 0.17), 0.017, 0.09, "rubber", axis="z", bevel=0.004)
    box("Plate_Label", (0.25 + 0.041, 0.32, 0.0), (0.002, 0.08, 0.05), "label_white", bevel=0.0)


@model("engine_stand", (0.7, 0.9, 0.8))
def engine_stand():
    _engine_stand(False)


@model("engine_stand_folding", (0.5, 0.9, 0.6))
def engine_stand_folding():
    _engine_stand(True)


def _shelving(W, H, D, levels, heavy):
    """Boltless shelving (equipment_view.gd _build_shelf): slotted angle posts, boards at Torque
    Shed's pitch ((H - 0.15) / levels, the first at 0.06), and the label plate on top. Heavy-duty
    racking has orange beams and blue uprights, as pallet racking does."""
    post = 0.06 if heavy else 0.04
    frame = "paint_blue" if heavy else "paint_shelf"
    beam = "paint_orange" if heavy else "paint_shelf"
    pitch = (H - 0.15) / levels
    for x in (-W / 2 + post / 2, W / 2 - post / 2):
        for z in (-D / 2 + post / 2, D / 2 - post / 2):
            # An angle (L section) post, not a solid bar, with its rows of slots.
            sx = 1 if x < 0 else -1
            sz = 1 if z < 0 else -1
            box("PostA%.2f%.2f" % (x, z), (x - sx * post * 0.4, H / 2, z), (post * 0.2, H, post), frame, bevel=0.002)
            box("PostB%.2f%.2f" % (x, z), (x, H / 2, z - sz * post * 0.4), (post, H, post * 0.2), frame, bevel=0.002)
            box("PostFoot%.2f%.2f" % (x, z), (x, 0.004, z), (post + 0.01, 0.008, post + 0.01), "plastic_black", bevel=0.002)
        # Diagonal bracing on the ends of tall/heavy units.
        if heavy or H > 2.2:
            for k in range(3):
                y0, y1 = 0.2 + k * (H - 0.4) / 3, 0.2 + (k + 1) * (H - 0.4) / 3
                tube("Brace%.2f%d" % (x, k), [(x, y0, -D / 2 + post), (x, y1, D / 2 - post)], 0.008, frame, sides=6)
    for level in range(levels + 1):
        y = min(0.06 + level * pitch, H - 0.02)
        box("Board%d" % level, (0.0, y, 0.0), (W - 0.01, 0.018, D - 0.01), "plywood" if heavy else "paint_board", bevel=0.002)
        for z in (-D / 2 + 0.015, D / 2 - 0.015):
            # (28 mm deep: 1 mm inside the posts' faces, not in their plane.)
            box("Beam%d%.2f" % (level, z), (0.0, y - 0.03, z), (W - post * 0.5, 0.05 if heavy else 0.035, 0.028), beam, bevel=0.003)
    box("Plate", (0.0, H - 0.12, D / 2 + 0.005), (0.7, 0.14, 0.008), "label_white", bevel=0.002)
    for x in (-0.33, 0.33):
        cyl("Rivet%.2f" % x, (x, H - 0.12, D / 2 + 0.01), 0.006, 0.004, "chrome", axis="z", verts=10)


@model("shelf_compact", (0.8, 2.0, 0.45))
def shelf_compact():
    _shelving(0.8, 2.0, 0.45, 4, False)


@model("shelf_standard", (1.2, 2.0, 0.5))
def shelf_standard():
    _shelving(1.2, 2.0, 0.5, 4, False)


@model("shelf_tall", (1.2, 2.5, 0.5))
def shelf_tall():
    _shelving(1.2, 2.5, 0.5, 5, False)


@model("shelf_wide", (1.8, 2.0, 0.55))
def shelf_wide():
    _shelving(1.8, 2.0, 0.55, 4, False)


@model("shelf_staff", (1.8, 2.0, 0.55))
def shelf_staff():
    _shelving(1.8, 2.0, 0.55, 4, False)
    pitch = (2.0 - 0.15) / 4
    for level in range(4):
        y = 0.06 + level * pitch + 0.018 - 0.045
        box("BinStrip%d" % level, (0.0, y, 0.55 / 2 + 0.004), (0.62, 0.06, 0.006), "label_yellow", bevel=0.001)


@model("shelf_racking", (2.4, 2.2, 0.6))
def shelf_racking():
    _shelving(2.4, 2.2, 0.6, 4, True)


@model("shelf_delivery", (3.6, 2.2, 0.7))
def shelf_delivery():
    _shelving(3.6, 2.2, 0.7, 4, True)


def _washer(pid):
    """Parts washers (equipment_view.gd _build_washer): a solvent sink on a drum cabinet, its
    lid hinged at the back, the splash back, the flexible tap and the brush. Work height 0.86."""
    sizes = {"washer_compact": (0.7, 1.0, 0.6), "washer_standard": (1.3, 1.6, 0.9), "washer_ultrasonic": (0.9, 1.1, 0.7),
             "washer_cabinet": (1.2, 1.7, 1.0), "washer_industrial": (1.9, 1.7, 1.0)}
    paints = {"washer_compact": "paint_green", "washer_standard": "paint_blue", "washer_ultrasonic": "paint_white",
              "washer_cabinet": "paint_red", "washer_industrial": "paint_grey"}
    W, H, D = sizes[pid]
    body = paints[pid]
    top = 0.8 if pid == "washer_compact" else 0.86
    if pid == "washer_compact":
        for x in (-W * 0.4, W * 0.4):
            for z in (-D * 0.4, D * 0.4):
                box("Leg%.2f%.2f" % (x, z), (x, (top - 0.24) / 2, z), (0.035, top - 0.24, 0.035), "steel", bevel=0.003)
        box("Tank", (0.0, top - 0.12, 0.0), (W, 0.24, D), body, bevel=0.008)
    elif pid == "washer_cabinet":
        # A closed cabinet: the drum, a lid hinged at the back with a window, the turntable inside.
        box("Cabinet", (0.0, (top - 0.04) / 2, 0.0), (W, top - 0.04, D), body, bevel=0.01)
        box("Plinth", (0.0, 0.03, 0.0), (W - 0.04, 0.06, D - 0.04), "steel_dark", bevel=0.004)
        box("Lid", (0.0, top + 0.18, -0.05), (W, 0.36, D - 0.1), body, bevel=0.02)
        box("Window", (0.0, top + 0.37, -0.05), (W * 0.6, 0.004, D * 0.5), "glass", bevel=0.0)
        tube("LidHandle", bent([(-0.3, top + 0.2, D / 2 - 0.05), (-0.3, top + 0.2, D / 2 + 0.0), (0.3, top + 0.2, D / 2 + 0.0), (0.3, top + 0.2, D / 2 - 0.05)], 0.02), 0.012, "chrome")
        box("Panel", (W / 2 - 0.15, top - 0.2, D / 2 + 0.003), (0.2, 0.14, 0.006), "plastic_black", bevel=0.002)
        box("PanelTemp", (W / 2 - 0.15, top - 0.18, D / 2 + 0.007), (0.08, 0.03, 0.002), "display_red", bevel=0.0)
    else:
        # Cabinet below the sink.
        box("Cabinet", (0.0, (top - 0.06) / 2 + 0.03, 0.0), (W - 0.04, top - 0.12, D - 0.04), body, bevel=0.008)
        box("Plinth", (0.0, 0.03, 0.0), (W - 0.06, 0.06, D - 0.06), "steel_dark", bevel=0.004)
        box("Door", (-W * 0.22, (top - 0.06) / 2 + 0.03, D / 2 - 0.01), (W * 0.4, top - 0.2, 0.012), body, bevel=0.003)
        box("DoorHandle", (-W * 0.06, top * 0.55, D / 2 + 0.005), (0.015, 0.12, 0.02), "chrome", bevel=0.004)
        # The sink: a steel tub with a rolled rim.
        box("SinkFloor", (0.0, top - 0.18, 0.0), (W - 0.12, 0.01, D - 0.12), "steel", bevel=0.002)
        for z in (-D / 2 + 0.03, D / 2 - 0.03):
            box("SinkWallZ%.2f" % z, (0.0, top - 0.09, z), (W, 0.18, 0.04), "steel", bevel=0.01)
        for x in (-W / 2 + 0.03, W / 2 - 0.03):
            box("SinkWallX%.2f" % x, (x, top - 0.09, 0.0), (0.04, 0.18, D), "steel", bevel=0.01)
        box("Solvent", (0.0, top - 0.14, 0.0), (W - 0.1, 0.004, D - 0.1), "solvent", bevel=0.0)
        box("Grate", (0.0, top - 0.02, 0.0), (W - 0.12, 0.012, D - 0.12), "steel_dark", bevel=0.001)
        for i in range(int((W - 0.12) / 0.05)):
            box("GrateBar%d" % i, (-W / 2 + 0.085 + i * 0.05, top - 0.012, 0.0), (0.008, 0.012, D - 0.122), "steel", bevel=0.0)
    if pid in ("washer_standard", "washer_industrial", "washer_cabinet"):
        back = min(H - top, 1.1)
        if pid != "washer_cabinet":
            box("Splash", (0.0, top + back / 2, -D / 2 + 0.03), (W, back, 0.04), body, bevel=0.008)
            for x in (-W / 2 + 0.02, W / 2 - 0.02):
                prism("Wing%.2f" % x, [(-D / 2 + 0.05, top), (-D / 2 + 0.05, top + back * 0.6), (-D / 2 + 0.3, top)], 0.02, body, axis="x", centre=x)
            # The lid hinged up against the splash back.
            box("LidUp", (0.0, top + 0.3, -D / 2 + 0.08), (W - 0.04, 0.6, 0.02), body, bevel=0.006)
            cyl("Fusible", (W * 0.3, top + 0.55, -D / 2 + 0.1), 0.012, 0.04, "brass", axis="z")
            # The flexible tap.
            hose("Tap", [(W * 0.25, top + 0.15, -D / 2 + 0.06), (W * 0.25, top + 0.45, -D / 2 + 0.2), (W * 0.2, top + 0.4, -D / 2 + 0.35), (W * 0.18, top + 0.2, -D / 2 + 0.38)], 0.012, "steel")
            cyl("Nozzle", (W * 0.18, top + 0.17, -D / 2 + 0.38), 0.008, 0.06, "brass", axis="y")
            # The brush resting on the grate.
            box("BrushHandle", (-W * 0.2, top + 0.0, 0.05), (0.3, 0.025, 0.035), "plastic_red", bevel=0.008, rot=(0, 20, 0))
            box("BrushHead", (-W * 0.2 - 0.17, top + 0.0, 0.0), (0.06, 0.04, 0.04), "plastic_yellow", bevel=0.004, rot=(0, 20, 0))
        # The pump on the side.
        cyl("Pump", (W / 2 + 0.06, 0.25, -D / 4), 0.06, 0.18, "paint_black", axis="y", bevel=0.01)
        box("PumpBox", (W / 2 + 0.06, 0.38, -D / 4), (0.1, 0.08, 0.1), "plastic_black", bevel=0.006)
        hose("PumpCable", [(W / 2 + 0.06, 0.42, -D / 4), (W / 2 + 0.04, 0.6, -D / 2 + 0.03), (W / 2 - 0.04, top - 0.1, -D / 2 + 0.03)], 0.006, "plastic_black")
    if pid == "washer_ultrasonic":
        box("Display", (W * 0.35, top - 0.2, D / 2 + 0.004), (0.12, 0.08, 0.004), "display_green", bevel=0.0)
        for i in range(3):
            cyl("UBtn%d" % i, (W * 0.35 - 0.05 + i * 0.05, top - 0.3, D / 2 + 0.006), 0.012, 0.008, "plastic_grey", axis="z", bevel=0.002)
        box("Basket", (0.0, top - 0.06, 0.0), (W - 0.2, 0.1, D - 0.2), "steel", bevel=0.004)
        box("ULid", (0.0, top + 0.015, 0.0), (W - 0.04, 0.03, D - 0.04), "plastic_grey", bevel=0.01)
    box("NamePlate", (0.0, top - 0.25, D / 2 + 0.003 - (0.0 if pid != "washer_compact" else 0.0)), (min(0.5, W - 0.3), 0.07, 0.004), "label_white", bevel=0.001)


for _pid, _size in (("washer_compact", (0.7, 1.0, 0.6)), ("washer_standard", (1.3, 1.6, 0.9)), ("washer_ultrasonic", (0.9, 1.1, 0.7)),
                    ("washer_cabinet", (1.2, 1.7, 1.0)), ("washer_industrial", (1.9, 1.7, 1.0))):
    MODELS[_pid] = ((lambda p: (lambda: _washer(p)))(_pid), _size)


def _repair(pid):
    """Repair machines (equipment_view.gd _build_repair): a steel bench (top 0.96) with the vice and
    grinder every one has; a pedestal grinder stand; a bead blast cabinet; the pro benches with a
    pegboard, drawers, a welder and a drill press."""
    sizes = {"repair_compact": (0.8, 1.2, 0.6), "repair_standard": (1.8, 1.2, 0.9), "repair_blaster": (1.1, 1.6, 0.8),
             "repair_pro": (2.2, 1.3, 1.0), "repair_workshop": (2.8, 1.4, 1.0)}
    W, H, D = sizes[pid]
    top = 0.96
    if pid == "repair_compact":
        # A pedestal: column, base plate bolted down, a small top with the grinder and vice.
        cyl("Column", (0.0, top * 0.45, 0.0), 0.05, top * 0.9, "paint_grey", bevel=0.004)
        box("Foot", (0.0, 0.015, 0.0), (0.45, 0.03, 0.45), "paint_grey", bevel=0.008)
        for x in (-0.18, 0.18):
            for z in (-0.18, 0.18):
                cyl("Bolt%.2f%.2f" % (x, z), (x, 0.035, z), 0.01, 0.012, "steel", verts=6)
        box("Top", (0.0, top - 0.015, 0.0), (W, 0.03, D * 0.8), "steel_dark", bevel=0.004)
        grinder("Grinder", (0.12, top, -0.05))
        vice("Vice", (-0.25, top, 0.08))
        return
    if pid == "repair_blaster":
        # A bead blast cabinet: a hopper body on legs, a sloping window, two glove ports.
        for x in (-W / 2 + 0.04, W / 2 - 0.04):
            for z in (-D / 2 + 0.04, D / 2 - 0.04):
                box("Leg%.2f%.2f" % (x, z), (x, 0.3, z), (0.04, 0.6, 0.04), "paint_green", bevel=0.003)
        prism("Hopper", [(-D / 2, 0.6), (D / 2, 0.6), (0.1, 0.35), (-0.1, 0.35)], W - 0.02, "paint_green", axis="x")
        box("Body", (0.0, top - 0.18, 0.0), (W, 0.72, D), "paint_green", bevel=0.01)
        prism("Hood", [(-D / 2, top + 0.18), (D / 2 - 0.25, top + 0.18), (D / 2, top - 0.0), (D / 2, top + 0.0), (-D / 2, top + 0.0)], W, "paint_green", axis="x")
        prism("Window", [(D / 2 - 0.26, top + 0.17), (D / 2 - 0.01, top + 0.01), (D / 2 - 0.0, top + 0.02), (D / 2 - 0.25, top + 0.185)], W * 0.7, "glass", axis="x", bevel=0.0)
        for x in (-0.2, 0.2):
            torus("Port%.1f" % x, (x, top - 0.18, D / 2 + 0.005), 0.075, 0.012, "rubber", axis="z")
            cyl("Glove%.1f" % x, (x, top - 0.18, D / 2 - 0.058), 0.07, 0.12, "rubber", axis="z", bevel=0.02)   # (2 mm proud)
        box("Dust", (W / 2 + 0.1, 0.4, -D / 4), (0.2, 0.8, 0.25), "paint_grey", bevel=0.01)
        hose("Duct", [(W / 2 + 0.1, 0.8, -D / 4), (W / 2 + 0.1, top + 0.1, -D / 4), (W / 2 - 0.05, top + 0.12, -D / 4)], 0.04, "plastic_black")
        box("Pedal", (0.0, 0.03, D / 2 - 0.05), (0.12, 0.04, 0.2), "steel_dark", bevel=0.005)
        return
    # The steel bench: a top plate on a frame, a shelf, the vice at one end and grinder at the other.
    square_tube_frame("Frame", -W / 2, W / 2, -D / 2, D / 2, 0.0, top - 0.02, 0.05, "paint_grey")
    box("Top", (0.0, top - 0.01, 0.0), (W + 0.05, 0.02, D + 0.05), "steel_dark", bevel=0.003)
    box("Shelf", (0.0, 0.18, 0.0), (W - 0.1, 0.02, D - 0.1), "steel_dark", bevel=0.002)
    vice("Vice", (-W / 2 + 0.15, top, D / 4))
    grinder("Grinder", (W / 2 - 0.2, top, -D * 0.2))
    if pid in ("repair_pro", "repair_workshop"):
        box("Peg", (0.0, top + 0.5, -D / 2 + 0.02), (W, 0.8, 0.015), "pegboard", bevel=0.002)
        for i in range(int(W / 0.2)):
            x = -W / 2 + 0.15 + i * 0.2
            box("PegTool%d" % i, (x, top + 0.55 - (i % 3) * 0.05, -D / 2 + 0.04), (0.025, 0.2 + (i % 4) * 0.03, 0.006), "chrome" if i % 2 else "plastic_red", bevel=0.003)
        drawer_bank("Drawers", W / 2 - 0.45, W / 2 - 0.05, 0.24, top - 0.06, D / 2 - 0.02, 4, "paint_red")
        box("DrawerCab", (W / 2 - 0.25, (top - 0.06 + 0.24) / 2, 0.0), (0.4, top - 0.3, D - 0.1), "paint_red", bevel=0.004)
        # The welder: a MIG set on the shelf with its torch lead coiled on top.
        box("Welder", (-W / 2 + 0.35, 0.38, 0.0), (0.3, 0.38, 0.55), "paint_blue", bevel=0.01)
        box("WelderPanel", (-W / 2 + 0.35, 0.42, 0.276), (0.26, 0.2, 0.004), "plastic_black", bevel=0.0)
        for i in range(2):
            cyl("WKnob%d" % i, (-W / 2 + 0.28 + i * 0.12, 0.42, 0.285), 0.02, 0.02, "plastic_grey", axis="z", bevel=0.003)
        torus("WeldLead", (-W / 2 + 0.35, 0.585, 0.0), 0.12, 0.012, "rubber", axis="y")
    if pid == "repair_workshop":
        # A bench drill press at the middle.
        x = 0.1
        box("DrillBase", (x, top + 0.015, -0.1), (0.25, 0.03, 0.35), "cast_iron", bevel=0.006)
        cyl("DrillColumn", (x, top + 0.35, -0.22), 0.03, 0.66, "chrome")
        box("DrillHead", (x, top + 0.6, -0.12), (0.18, 0.2, 0.32), "paint_green", bevel=0.02)
        cyl("DrillMotor", (x, top + 0.62, -0.32), 0.07, 0.16, "paint_green", axis="z", bevel=0.01)
        cyl("DrillQuill", (x, top + 0.45, -0.03), 0.02, 0.1, "chrome")
        cyl("DrillChuck", (x, top + 0.38, -0.03), 0.018, 0.05, "steel", radius2=0.012)
        box("DrillTable", (x, top + 0.25, -0.05), (0.2, 0.02, 0.2), "cast_iron", bevel=0.004)
        for i in range(3):
            a = math.radians(120 * i - 30)
            tube("DrillLever%d" % i, [(x + 0.1, top + 0.55, -0.1), (x + 0.1 + math.cos(a) * 0.15, top + 0.55 + math.sin(a) * 0.15, -0.1)], 0.006, "chrome")
    box("NamePlate", (0.0, top - 0.2, D / 2 + 0.03), (0.45, 0.07, 0.004), "label_white", bevel=0.001)


for _pid, _size in (("repair_compact", (0.8, 1.2, 0.6)), ("repair_standard", (1.8, 1.2, 0.9)), ("repair_blaster", (1.1, 1.6, 0.8)),
                    ("repair_pro", (2.2, 1.3, 1.0)), ("repair_workshop", (2.8, 1.4, 1.0))):
    MODELS[_pid] = ((lambda p: (lambda: _repair(p)))(_pid), _size)


@model("tyre_changer", (0.8, 1.2, 0.9))
def tyre_changer():
    """The tyre changer (equipment_view.gd _build_changer): a pedestal (work height 0.86) with the
    clamping turntable (its place at (0, 0.86, 0.08)), the column at the back with the swing arm and
    mount head, the bead breaker on the side and three pedals at the front."""
    W, H, D = 0.8, 1.2, 0.9
    top = 0.86
    outline = [(-D / 2 + 0.1, 0.05), (D / 2 - 0.1, 0.05), (D / 2 - 0.14, top - 0.06), (-D / 2 + 0.1, top - 0.06)]
    prism("Body", outline, W - 0.1, "paint_navy", axis="x", bevel=0.015)
    box("Base", (0.0, 0.025, 0.0), (W, 0.05, D), "steel_dark", bevel=0.006)
    box("TopPlate", (0.0, top - 0.05, 0.02), (W - 0.12, 0.02, D - 0.26), "steel_dark", bevel=0.004)
    # The turntable and its four clamp jaws in their slots.
    cyl("Turntable", (0.0, top - 0.025, 0.08), 0.3, 0.05, "steel", verts=48, bevel=0.004)
    for i in range(4):
        a = math.radians(90 * i + 45)
        box("Slot%d" % i, (math.cos(a) * 0.17, top + 0.001, 0.08 + math.sin(a) * 0.17), (0.18, 0.003, 0.025), "steel_dark", bevel=0.0, rot=(0, -math.degrees(a), 0))
        box("Jaw%d" % i, (math.cos(a) * 0.2, top + 0.015, 0.08 + math.sin(a) * 0.2), (0.07, 0.03, 0.035), "paint_red", bevel=0.004, rot=(0, -math.degrees(a), 0))
    cyl("Post", (0.0, top + 0.08, 0.08), 0.018, 0.16, "chrome")
    cyl("PostCone", (0.0, top + 0.03, 0.08), 0.04, 0.05, "steel", radius2=0.02)
    # Column at the back, swing arm forward over the table, the mount head on its hex bar.
    back = -D / 2 + 0.08
    box("Column", (0.0, top + 0.2, back), (0.1, H - top + 0.4, 0.1), "paint_navy", bevel=0.008)
    cyl("Pivot", (0.0, H + 0.05, back), 0.06, 0.06, "steel_dark", bevel=0.004)
    box("Arm", (0.0, H + 0.05, back + 0.24), (0.08, 0.08, 0.48), "paint_navy", bevel=0.008)
    box("ArmHead", (0.0, H + 0.05, back + 0.48), (0.1, 0.12, 0.1), "paint_navy", bevel=0.01)
    cyl("Bar", (0.0, H - 0.2, back + 0.48), 0.022, 0.6, "chrome", verts=6)
    box("MountHead", (0.0, H - 0.52, back + 0.48), (0.05, 0.06, 0.09), "plastic_black", bevel=0.008)
    box("Lock", (0.07, H + 0.05, back + 0.48), (0.04, 0.03, 0.03), "paint_red", bevel=0.004)
    # The bead breaker on the side: its arm, the shovel, the rubber pad on the body.
    box("BreakerPost", (W / 2 - 0.04, top * 0.5, 0.0), (0.05, top * 0.6, 0.05), "paint_navy", bevel=0.004)
    box("BreakerArm", (W / 2 + 0.02, top * 0.55, 0.12), (0.04, 0.04, 0.36), "steel", bevel=0.004, rot=(-23, 0, 0))
    box("BreakerShovel", (W / 2 + 0.02, top * 0.4, 0.29), (0.1, 0.12, 0.012), "steel", bevel=0.003)
    box("BreakerPad", (W / 2 - 0.045, top * 0.35, 0.0), (0.012, 0.2, 0.16), "rubber", bevel=0.004)
    # The tyre lever hanging on the column and the air line.
    box("Lever", (0.06, top + 0.25, back + 0.06), (0.02, 0.45, 0.02), "paint_red", bevel=0.005, rot=(0, 0, 9))
    hose("AirLine", [(-0.06, top + 0.6, back - 0.05), (-0.25, top + 0.3, back - 0.05), (-0.3, 0.3, back + 0.0), (-0.3, 0.0, back - 0.1)], 0.007, "plastic_yellow")
    box("Gauge", (-W / 2 + 0.1, top + 0.05, -D / 2 + 0.15), (0.06, 0.06, 0.04), "plastic_black", bevel=0.01)
    cyl("GaugeFace", (-W / 2 + 0.1, top + 0.05, -D / 2 + 0.172), 0.025, 0.004, "paint_white", axis="z")
    for i in range(3):
        box("Pedal%d" % i, (-0.18 + i * 0.18, 0.08, D / 2 - 0.05), (0.1, 0.025, 0.14), "steel_dark", bevel=0.004, rot=(8, 0, 0))
    box("NamePlate", (0.0, top - 0.2, D / 2 - 0.105), (0.32, 0.06, 0.004), "label_white", bevel=0.001)


def _bench(pid):
    """Workbenches (equipment_view.gd _build_bench): a top (work height size.y + 0.01) on folding
    legs, a tool chest on castors, a steel frame, or the pro station with drawers and pegboard."""
    sizes = {"bench_compact": (1.0, 0.9, 0.6), "bench_mobile": (1.2, 0.95, 0.6), "bench_standard": (1.8, 0.95, 0.75),
             "bench_long": (2.4, 0.95, 0.75), "bench_pro": (2.4, 0.95, 0.8)}
    W, H, D = sizes[pid]
    top = H
    if pid == "bench_pro":
        box("Top", (0.0, top - 0.02, 0.0), (W, 0.04, D), "steel", bevel=0.004)
    else:
        wood_top("Top", (0.0, top - 0.025, 0.0), (W, 0.05, D))
    if pid == "bench_compact":
        for x in (-W * 0.4, W * 0.4):
            for s in (-1, 1):
                tube("Leg%.2f%d" % (x, s), [(x, top - 0.06, 0.0), (x, 0.02, s * D * 0.42)], 0.014, "steel")
            tube("Foot%.2f" % x, [(x, 0.015, -D * 0.45), (x, 0.015, D * 0.45)], 0.015, "steel")
            box("Hinge%.2f" % x, (x, top - 0.07, 0.0), (0.05, 0.04, 0.08), "steel_dark", bevel=0.004)
        tube("Brace", [(-W * 0.4, 0.35, 0.0), (W * 0.4, 0.35, 0.0)], 0.01, "steel")
        return
    if pid == "bench_mobile":
        box("Chest", (0.0, (top - 0.05 + 0.12) / 2, 0.0), (W - 0.04, top - 0.17, D - 0.04), "paint_red", bevel=0.006)
        drawer_bank("Drawer", -W / 2 + 0.04, W / 2 - 0.04, 0.14, top - 0.07, D / 2 - 0.02, 5, "paint_red", cols=1)
        for x in (-W / 2 + 0.08, W / 2 - 0.08):
            for z in (-D / 2 + 0.08, D / 2 - 0.08):
                castor("C%.2f%.2f" % (x, z), (x, z), 0.05)
        tube("Handle", bent([(W / 2 - 0.02, top - 0.12, -D / 2 + 0.1), (W / 2 + 0.06, top - 0.12, -D / 2 + 0.1), (W / 2 + 0.06, top - 0.12, D / 2 - 0.1), (W / 2 - 0.02, top - 0.12, D / 2 - 0.1)], 0.03), 0.013, "chrome")
        return
    square_tube_frame("Frame", -W / 2, W / 2, -D / 2, D / 2, 0.0, top - 0.05, 0.05, "paint_grey")
    box("Shelf", (0.0, 0.18, 0.0), (W - 0.1, 0.025, D - 0.1), "paint_board", bevel=0.003)
    if pid == "bench_pro":
        box("Cab", (W * 0.25, (top - 0.05) / 2, 0.0), (W * 0.45, top - 0.1, D - 0.06), "paint_black", bevel=0.005)
        drawer_bank("Drawer", W * 0.25 - W * 0.22, W * 0.25 + W * 0.22, 0.1, top - 0.08, D / 2 - 0.03, 5, "paint_black", cols=2)
        box("Board", (0.0, top + 0.5, -D / 2 + 0.02), (W, 0.85, 0.02), "paint_grey", bevel=0.003)
        for i in range(10):
            x = -W / 2 + 0.15 + i * (W - 0.3) / 9
            box("PTool%d" % i, (x, top + 0.6 - (i % 2) * 0.08, -D / 2 + 0.045), (0.02, 0.18 + (i % 3) * 0.04, 0.006), "chrome", bevel=0.003)
        box("LightBar", (0.0, top + 0.95, -D / 2 + 0.1), (W - 0.1, 0.03, 0.08), "aluminium", bevel=0.004)
        box("LightTube", (0.0, top + 0.934, -D / 2 + 0.1), (W - 0.15, 0.004, 0.06), "led_white", bevel=0.0)
    else:
        vice("Vice", (-W / 2 + 0.2, top, D / 2 - 0.12))


for _pid, _size in (("bench_compact", (1.0, 0.9, 0.6)), ("bench_mobile", (1.2, 0.95, 0.6)), ("bench_standard", (1.8, 0.95, 0.75)),
                    ("bench_long", (2.4, 0.95, 0.75)), ("bench_pro", (2.4, 0.95, 0.8))):
    MODELS[_pid] = ((lambda p: (lambda: _bench(p)))(_pid), _size)


def _trolley(flatbed):
    """Trolleys (equipment_view.gd _build_trolley): a lipped deck on four castors, the push handle at
    the front (+z). The parts trolley has a second shelf; the flatbed is a low plywood platform."""
    W, H, D = (0.9, 0.95, 1.6) if flatbed else (0.62, 0.95, 1.0)
    top = 0.3 if flatbed else 0.78
    frame = "paint_navy"
    if flatbed:
        box("Deck", (0.0, top - 0.02, 0.0), (W, 0.025, D), "plywood", bevel=0.003)
        for x in (-W / 2 + 0.02, W / 2 - 0.02):
            box("Chassis%.2f" % x, (x, top - 0.05, 0.0), (0.04, 0.04, D), frame, bevel=0.004)
    else:
        for y in (top - 0.02, 0.24):
            box("Deck%.2f" % y, (0.0, y, 0.0), (W - 0.004, 0.02, D - 0.004), "paint_grey", bevel=0.003)   # (inside the lips)
            for s in (-1, 1):
                box("LipX%.2f%d" % (y, s), (s * (W / 2 - 0.008), y + 0.02, 0.0), (0.016, 0.04, D), frame, bevel=0.003)
                box("LipZ%.2f%d" % (y, s), (0.0, y + 0.02, s * (D / 2 - 0.008)), (W, 0.04, 0.016), frame, bevel=0.003)
    wr = 0.06 if top > 0.5 else 0.07
    for x in (-W / 2 + 0.06, W / 2 - 0.06):
        for z in (-D / 2 + 0.08, D / 2 - 0.08):
            if top - 0.04 - wr * 2 > 0.04:
                tube("Leg%.2f%.2f" % (x, z), [(x, wr * 2 + 0.03, z), (x, top - 0.02, z)], 0.015, frame, sides=10)
            castor("C%.2f%.2f" % (x, z), (x, z), wr, swivel=z > 0)
    for s in (-1, 1):
        tube("Upright%d" % s, [(s * (W / 2 - 0.03), top, D / 2 - 0.03), (s * (W / 2 - 0.03), H - 0.04, D / 2 - 0.03), (s * (W / 2 - 0.03), H, D / 2 + 0.01)], 0.014, frame, sides=10)
    cyl("Grip", (0.0, H, D / 2 + 0.01), 0.018, W - 0.02, "rubber", axis="x", bevel=0.004)
    box("NamePlate", (0.0, top - 0.02, D / 2 + 0.004), (0.25, 0.04, 0.004), "label_white", bevel=0.001)


@model("trolley_parts", (0.62, 0.95, 1.0))
def trolley_parts():
    _trolley(False)


@model("trolley_flatbed", (0.9, 0.95, 1.6))
def trolley_flatbed():
    _trolley(True)


# --- Seats (Equipment seats, equipment_view.gd _build_seat) ---------------------------------------------

@model("seat_stool", (0.45, 0.7, 0.45))
def seat_stool():
    """A swivel shop stool with a red vinyl top: five-star base on castors, gas column, foot ring."""
    for i in range(5):
        a = 2 * math.pi * i / 5
        tube("Star%d" % i, [(0.0, 0.08, 0.0), (math.cos(a) * 0.2, 0.06, math.sin(a) * 0.2)], 0.016, "chrome", sides=10)
        castor("C%d" % i, (math.cos(a) * 0.2, math.sin(a) * 0.2), 0.025)
    cyl("Hub", (0.0, 0.08, 0.0), 0.04, 0.05, "plastic_black")
    cyl("Column", (0.0, 0.3, 0.0), 0.025, 0.42, "chrome")
    cyl("Gas", (0.0, 0.48, 0.0), 0.032, 0.18, "plastic_black")
    torus("FootRing", (0.0, 0.3, 0.0), 0.17, 0.009, "chrome", axis="y")
    for i in range(3):
        a = 2 * math.pi * i / 3
        tube("RingArm%d" % i, [(0.0, 0.3, 0.0), (math.cos(a) * 0.17, 0.3, math.sin(a) * 0.17)], 0.007, "chrome", sides=8)
    cyl("SeatPan", (0.0, 0.6, 0.0), 0.17, 0.02, "steel_dark")
    cyl("Cushion", (0.0, 0.645, 0.0), 0.19, 0.07, "vinyl_red", verts=40, bevel=0.025)


@model("seat_creeper", (0.5, 0.5, 0.6))
def seat_creeper():
    """A creeper seat: a padded seat on a post over a tool tray, on four castors."""
    box("Tray", (0.0, 0.12, 0.0), (0.44, 0.04, 0.54), "paint_red", bevel=0.006)
    for s in (-1, 1):
        box("TrayLip%d" % s, (s * 0.21, 0.15, 0.0), (0.02, 0.03, 0.54), "paint_red", bevel=0.004)
    for x in (-0.18, 0.18):
        for z in (-0.22, 0.22):
            castor("C%.2f%.2f" % (x, z), (x, z), 0.04)
    cyl("Post", (0.0, 0.3, 0.0), 0.03, 0.3, "chrome")
    cyl("Seat", (0.0, 0.45, 0.0), 0.19, 0.07, "vinyl_red" if False else "plastic_black", verts=40, bevel=0.025)
    for i in range(4):
        box("Socket%d" % i, (-0.15 + i * 0.1, 0.16, 0.18), (0.06, 0.02, 0.06), "steel", bevel=0.004)


@model("seat_chair", (0.5, 0.85, 0.5))
def seat_chair():
    """A canvas camp chair: crossed steel legs, a slung canvas seat and back, arm rests."""
    for s in (-1, 1):
        tube("LegA%d" % s, [(s * 0.22, 0.0, -0.22), (s * 0.22, 0.46, 0.22)], 0.011, "steel_dark", sides=8)
        tube("LegB%d" % s, [(s * 0.22, 0.0, 0.22), (s * 0.22, 0.46, -0.22)], 0.011, "steel_dark", sides=8)
        tube("Back%d" % s, [(s * 0.22, 0.46, -0.22), (s * 0.23, 0.85, -0.28)], 0.011, "steel_dark", sides=8)
        tube("Arm%d" % s, [(s * 0.24, 0.62, -0.24), (s * 0.24, 0.62, 0.2)], 0.018, "canvas_green", sides=8)
    prism("Sling", [(-0.22, 0.46), (0.22, 0.46), (0.2, 0.42), (-0.2, 0.42)], 0.44, "canvas_green", axis="x")
    prism("BackSling", [(-0.24, 0.5), (-0.22, 0.48), (-0.28, 0.82), (-0.3, 0.84)], 0.44, "canvas_green", axis="x")


@model("seat_bench", (1.4, 0.8, 0.5))
def seat_bench():
    """A wooden slat garage bench seat with a slatted back, on steel ends."""
    for x in (-0.62, 0.62):
        prism("End%.2f" % x, [(-0.22, 0.0), (-0.18, 0.0), (-0.18, 0.44), (0.2, 0.44), (0.2, 0.0), (0.24, 0.0), (0.24, 0.47), (-0.25, 0.8), (-0.29, 0.8), (-0.22, 0.47)], 0.04, "cast_iron", axis="x", centre=x)
    for i in range(5):
        z = -0.18 + i * 0.09
        box("Slat%d" % i, (0.0, 0.46, z), (1.4, 0.03, 0.07), "wood_top", bevel=0.006)
    for i in range(3):
        y = 0.56 + i * 0.09
        box("BackSlat%d" % i, (0.0, y, -0.23 - (y - 0.47) * 0.15), (1.4, 0.07, 0.025), "wood_top", bevel=0.006, rot=(-9, 0, 0))


@model("seat_sofa", (1.8, 0.85, 0.85))
def seat_sofa():
    """An old saggy leather sofa: plinth, seat cushions, back cushions, rolled arms, feet."""
    W, H, D = 1.8, 0.85, 0.85
    box("Base", (0.0, 0.22, 0.0), (W - 0.04, 0.24, D - 0.04), "leather_brown", bevel=0.04)
    for i in range(3):
        x = -W / 2 + 0.2 + (W - 0.4) * (i + 0.5) / 3
        box("Cushion%d" % i, (x, 0.42, 0.08), ((W - 0.42) / 3 - 0.01, 0.15, D - 0.25), "leather_brown", bevel=0.05)
        box("BackCushion%d" % i, (x, 0.65, -D / 2 + 0.17), ((W - 0.42) / 3 - 0.01, 0.38, 0.18), "leather_brown", bevel=0.06, rot=(-8, 0, 0))
    box("Back", (0.0, 0.55, -D / 2 + 0.07), (W, 0.6, 0.14), "leather_brown", bevel=0.05)
    for s in (-1, 1):
        box("Arm%d" % s, (s * (W / 2 - 0.1), 0.42, 0.0), (0.2, 0.4, D), "leather_brown", bevel=0.06)
        cyl("ArmRoll%d" % s, (s * (W / 2 - 0.1), 0.62, 0.0), 0.12, D, "leather_brown", axis="z", bevel=0.04)
    for x in (-W / 2 + 0.1, W / 2 - 0.1):
        for z in (-D / 2 + 0.08, D / 2 - 0.08):
            cyl("Foot%.1f%.1f" % (x, z), (x, 0.05, z), 0.03, 0.1, "wood_top", radius2=0.022)


# --- Wall TVs and posters ----------------------------------------------------------------------------

# The frame round a TV's glass, m: the game's page fills the rest (TorqueShedScreens.cpp TvBezel).
TV_BEZEL = 0.011

def _tv(width, height, depth):
    """A modern wall-mounted flat TV, its outside exactly width x height (so the shed's screens keep
    their size) and depth from the wall to the glass. The origin is at its middle on the wall.

    From the front: one sheet of black glass (the 'screen' slot the game draws its channel on, an
    inset TV_BEZEL in from the edge) in a slim dark-metal frame whose edge is chamfered, a slightly
    deeper chin along the bottom with the IR window and the standby light. Behind: the thin panel
    (2.4 cm), the electronics hump over the lower two-thirds with its vent slots and the ports on its
    left, the VESA plate and the tilt bracket's two arms back to a wall rail, so from the side it
    stands off the wall the way a real one does."""
    W, H, D = width, height, depth
    b = TV_BEZEL
    panel = 0.024                     # the panel's own thickness, glass to back sheet
    zf = D                            # the glass's face
    zb = D - panel                    # the panel's back sheet
    # The frame: four chamfered aluminium-look bars round the edge, the glass set 1 mm inside them.
    fd = panel                        # the frame runs the panel's whole thickness
    box("FrameTop", (0.0, H / 2 - b / 2, zf - fd / 2), (W, b, fd), "tv_frame", bevel=0.0025)
    box("FrameBottom", (0.0, -H / 2 + b / 2, zf - fd / 2), (W, b, fd), "tv_frame", bevel=0.0025)
    for s in (-1, 1):
        box("FrameSide%d" % s, (s * (W / 2 - b / 2), 0.0, zf - fd / 2), (b, H - 2 * b + 0.002, fd), "tv_frame", bevel=0.0025)
    # The glass: black, glossy, a hair behind the frame's lip.
    box("Panel", (0.0, 0.0, zf - 0.0012), (W - 2 * b + 0.002, H - 2 * b + 0.002, 0.002), "screen", bevel=0.0)
    box("PanelBack", (0.0, 0.0, zb + 0.004), (W - 0.004, H - 0.004, 0.008), "tv_back", bevel=0.002)
    # The chin's little bar under the glass: IR window in the middle, the standby light to its right.
    box("IrWindow", (0.0, -H / 2 + b / 2, zf + 0.0006), (0.05, b * 0.45, 0.0012), "tv_glass", bevel=0.0)
    box("Led", (0.035, -H / 2 + b / 2, zf + 0.0008), (0.004, 0.0025, 0.0012), "display_red", bevel=0.0)
    # The back: the electronics hump over the lower two-thirds, rounded off, with vent slots across
    # its top and bottom and a recessed bay of ports facing the left.
    hw, hh = W * 0.62, H * 0.58
    hy = -H / 2 + 0.1 + hh / 2
    hump_d = zb - 0.03                # from the bracket's face to the panel's back
    hz = 0.03 + hump_d / 2
    box("Hump", (0.0, hy, hz), (hw, hh, hump_d), "tv_back", bevel=min(0.012, hump_d * 0.45))
    for row, sy in enumerate((1, -1)):
        for i in range(14):
            x = -hw / 2 + 0.08 + (hw - 0.16) * i / 13.0
            box("Vent%d_%d" % (row, i), (x, hy + sy * (hh / 2 + 0.0004), hz), (0.012, 0.002, hump_d * 0.6), "plastic_black", bevel=0.0)
    box("Ports", (-hw / 2 + 0.002, hy, hz), (0.006, 0.14, hump_d * 0.55), "plastic_black", bevel=0.001)
    for i in range(4):
        box("Port%d" % i, (-hw / 2 - 0.0005, hy - 0.05 + i * 0.033, hz), (0.002, 0.012, 0.018), "steel_dark", bevel=0.0)
    # The VESA plate on the hump's back and the bracket: two arms down to the wall rail.
    box("Vesa", (0.0, hy, 0.03 - 0.003), (0.62 if W > 1.5 else W * 0.4, 0.42 if H > 1.0 else H * 0.3, 0.006), "steel_dark", bevel=0.002)
    for s in (-1, 1):
        ax = s * min(0.26, W * 0.17)
        box("Arm%d" % s, (ax, hy, 0.015), (0.04, 0.5 if H > 1.0 else H * 0.36, 0.024), "steel_dark", bevel=0.004)
    box("WallRail", (0.0, hy, 0.005), (min(0.7, W * 0.45), 0.08, 0.01), "steel_dark", bevel=0.003)
    for s in (-1, 1):
        cyl("WallBolt%d" % s, (s * min(0.3, W * 0.2), hy, 0.011), 0.008, 0.004, "steel", axis="z", verts=6, bevel=0.0005)
    # A tidy cable dropping from the ports round the hump's edge to the wall.
    tube("Cable", [(-hw / 2 - 0.01, hy - 0.02, hz), (-hw / 2 - 0.02, hy - 0.08, hz * 0.6), (-hw / 2 - 0.03, -H / 2 + 0.12, 0.004)], 0.004, "rubber", sides=8)


@model("tv_120", (2.66, 1.5, 0.09))
def tv_120():
    _tv(2.66, 1.5, 0.09)


@model("tv_128", (2.83, 1.59, 0.09))
def tv_128():
    _tv(2.83, 1.59, 0.09)


@model("tv_136", (3.01, 1.69, 0.1))
def tv_136():
    _tv(3.01, 1.69, 0.1)


@model("bay_screen", (2.6, 1.46, 0.09))
def bay_screen():
    """The screens built in over the bays (workshop.gd BAY_SCREEN_INNER/OUTER: 2.6 m across)."""
    _tv(2.6, 1.4625, 0.09)


@model("poster_frame", (0.6, 0.85, 0.02))
def poster_frame():
    """A clip frame for an A1 poster (the art is the game's: the 'paper' slot is drawn over): a thin
    aluminium edge, glass, the paper behind it. Scaled by the game for the other sizes."""
    W, H = 0.6, 0.85
    box("Backing", (0.0, 0.0, 0.004), (W, H, 0.006), "plywood", bevel=0.0)
    box("Paper", (0.0, 0.0, 0.0075), (W - 0.01, H - 0.01, 0.001), "paper", bevel=0.0)
    box("Glass", (0.0, 0.0, 0.0095), (W, H, 0.002), "glass", bevel=0.0)
    for y in (-H / 2, H / 2):
        box("EdgeH%.2f" % y, (0.0, y, 0.008), (W + 0.01, 0.01, 0.012), "aluminium", bevel=0.002)
    for x in (-W / 2, W / 2):
        box("EdgeV%.2f" % x, (x, 0.0, 0.008), (0.01, H + 0.01, 0.012), "aluminium", bevel=0.002)


# --- The dyno room (dyno_room.gd), in its own frame: built so yaw 0 lines up --------------------------
# Each is drawn about its own point in the room (Godot metres: the rig about CENTER, the fan about
# CENTER + (0, 0, -1.95), the desk about DESK); the room's +z (towards the shed) is the props' front.
# The drum and the fan's blades turn, so the game draws those (TorqueDynoRoom.cpp): not here.

@model("dyno_rig", (2.0, 0.5, 3.4))
def dyno_rig():
    """dyno_room.gd _build_rig: the chequer plate flush with the floor with its hazard edging, the
    framed slot the drum shows through (DRUM_Z 0.71), the front wheel clamp and four strap posts."""
    box("Plate", (0.0, 0.007, -0.05), (1.3, 0.014, 3.3), "steel", bevel=0.003)
    for i in range(22):   # tread bars, so it reads as chequer plate
        for j in range(8):
            z = -1.65 + 0.15 * i + (0.075 if j % 2 else 0.0)
            if abs(z - 0.71) < 0.36:
                continue
            box("Tread%d_%d" % (i, j), (-0.56 + j * 0.16, 0.0155, z), (0.06, 0.003, 0.012), "steel", bevel=0.001, rot=(0, 45 if (i + j) % 2 else -45, 0))
    for s in (-1, 1):
        box("Edge%d" % s, (s * 0.7, 0.009, -0.05), (0.08, 0.018, 3.4), "paint_yellow", bevel=0.003)
        for k in range(17):   # black hazard stripes over the yellow
            box("Stripe%d_%d" % (s, k), (s * 0.7, 0.0185, -1.65 + k * 0.2), (0.075, 0.002, 0.05), "paint_black", bevel=0.0, rot=(0, 40, 0))
    box("SlotFrame", (0.0, 0.016, 0.71), (0.66, 0.006, 0.68), "steel_dark", bevel=0.002)
    box("Slot", (0.0, 0.018, 0.71), (0.58, 0.004, 0.6), "paint_black", bevel=0.0)
    cz = -0.72
    for s in (-1, 1):
        prism("Cradle%d" % s, [(cz - 0.38, 0.03), (cz + 0.22, 0.03), (cz + 0.22, 0.12), (cz - 0.05, 0.38), (cz - 0.38, 0.38)], 0.03, "paint_blue", axis="x", centre=s * 0.1)
    box("ClampBase", (0.0, 0.03, cz - 0.08), (0.26, 0.04, 0.62), "paint_blue", bevel=0.004)
    box("ClampStop", (0.0, 0.2, cz - 0.42), (0.26, 0.4, 0.04), "paint_blue", bevel=0.004, rot=(-20, 0, 0))
    for k in range(5):
        cyl("ClampRoller%d" % k, (0.0, 0.07, cz + 0.15 - k * 0.07), 0.02, 0.17, "rubber", axis="x")
    cyl("ClampPin", (0.0, 0.3, cz - 0.1), 0.012, 0.3, "chrome", axis="x")
    for i, (px, pz) in enumerate(((-0.95, -0.6), (0.95, -0.6), (-0.95, 0.8), (0.95, 0.8))):
        box("PostPlate%d" % i, (px, 0.006, pz), (0.16, 0.012, 0.16), "steel_dark", bevel=0.002)
        cyl("Post%d" % i, (px, 0.22, pz), 0.045, 0.44, "paint_yellow", bevel=0.004)
        cyl("PostCap%d" % i, (px, 0.46, pz), 0.06, 0.05, "steel_dark", bevel=0.004)
        torus("PostRing%d" % i, (px, 0.44, pz), 0.055, 0.008, "steel", axis="y", verts=16)


@model("dyno_fan", (1.0, 1.45, 0.5))
def dyno_fan():
    """dyno_room.gd _build_fan: the cooling fan on its base, an open drum housing (the blades
    and hub are the game's, turning at (0, 0.95, 0.12)), the motor behind and the grille on the front."""
    box("Base", (0.0, 0.18, 0.0), (0.7, 0.36, 0.45), "paint_black", bevel=0.01)
    for x in (-0.3, 0.3):
        for z in (-0.17, 0.17):
            castor("C%.1f%.2f" % (x, z), (x, z), 0.04)
    box("Post", (0.0, 0.55, 0.0), (0.12, 0.4, 0.12), "steel", bevel=0.006)
    box("Yoke", (0.0, 0.75, 0.0), (1.02, 0.05, 0.08), "steel", bevel=0.006)
    for s in (-1, 1):
        box("YokeArm%d" % s, (s * 0.5, 0.86, 0.0), (0.04, 0.24, 0.08), "steel", bevel=0.005)
        cyl("Knob%d" % s, (s * 0.53, 0.95, 0.0), 0.03, 0.04, "plastic_black", axis="x")
    # The housing: a ring, open front and back, around where the blades turn.
    n = 40
    for k in range(n):
        a = 2 * math.pi * k / n
        box("Shell%d" % k, (math.cos(a) * 0.475, 0.95 + math.sin(a) * 0.475, 0.0), (0.077, 0.012, 0.3), "paint_blue", bevel=0.0, rot=(0, 0, math.degrees(a) + 90))
    torus("LipF", (0.0, 0.95, 0.15), 0.476, 0.012, "paint_blue", axis="z", verts=48)
    torus("LipB", (0.0, 0.95, -0.15), 0.476, 0.012, "paint_blue", axis="z", verts=48)
    cyl("Motor", (0.0, 0.95, -0.12), 0.11, 0.22, "paint_black", axis="z", bevel=0.015)
    for k in range(3):
        a = math.radians(90 + 120 * k)
        tube("Strut%d" % k, [(math.cos(a) * 0.1, 0.95 + math.sin(a) * 0.1, -0.1), (math.cos(a) * 0.47, 0.95 + math.sin(a) * 0.47, -0.12)], 0.008, "steel")
    for r in (0.2, 0.33, 0.46):
        torus("Grille%.2f" % r, (0.0, 0.95, 0.17), r, 0.005, "steel", axis="z", verts=48)
    for a in (0, 45, 90, 135):
        box("Spoke%d" % a, (0.0, 0.95, 0.17), (0.93, 0.008, 0.008), "steel", bevel=0.0, rot=(0, 0, a))
    hose("Cable", [(0.08, 0.95, -0.23), (0.15, 0.6, -0.25), (0.25, 0.1, -0.25), (0.4, 0.008, -0.6)], 0.008, "plastic_black")


@model("dyno_desk", (1.6, 1.45, 0.8))
def dyno_desk():
    """dyno_room.gd _build_desk: the console (1.5 x 0.8 x 0.7), its worktop, the monitor (its
    screen 0.84 x 0.46 just behind the game's glass at z -0.15), keyboard, mouse and the big red
    run button on its yellow pod."""
    box("Console", (0.0, 0.4, 0.0), (1.5, 0.8, 0.7), "paint_grey", bevel=0.008)
    for i, x in enumerate((-0.55, 0.0, 0.55)):
        box("Door%d" % i, (x, 0.42, 0.352), (0.45, 0.66, 0.012), "paint_grey", bevel=0.003)
        box("Pull%d" % i, (x + 0.17, 0.62, 0.364), (0.015, 0.1, 0.015), "chrome", bevel=0.004)
    box("Plinth", (0.0, 0.03, 0.0), (1.46, 0.06, 0.66), "paint_black", bevel=0.003)
    box("Top", (0.0, 0.82, 0.0), (1.55, 0.04, 0.75), "wood_top", bevel=0.004)
    box("Arm", (0.0, 0.92, -0.25), (0.06, 0.16, 0.05), "aluminium", bevel=0.006)
    box("MonBack", (0.0, 1.17, -0.2), (0.9, 0.52, 0.05), "plastic_black", bevel=0.008)
    box("MonScreen", (0.0, 1.17, -0.1755), (0.86, 0.48, 0.002), "screen", bevel=0.0)
    box("Keyboard", (-0.1, 0.85, 0.12), (0.5, 0.02, 0.16), "plastic_black", bevel=0.004)
    for r in range(5):
        box("Keys%d" % r, (-0.1, 0.861, 0.07 + r * 0.025), (0.48, 0.005, 0.018), "plastic_grey", bevel=0.001)
    box("Mouse", (0.22, 0.85, 0.12), (0.06, 0.02, 0.1), "plastic_black", bevel=0.009)
    box("ButtonPod", (0.45, 0.865, 0.12), (0.18, 0.05, 0.12), "plastic_yellow", bevel=0.01)
    cyl("RunButton", (0.45, 0.9, 0.12), 0.04, 0.025, "plastic_red", bevel=0.008)
    box("Logger", (-0.55, 0.88, -0.22), (0.3, 0.08, 0.2), "paint_black", bevel=0.006)
    for k in range(4):
        box("LoggerLed%d" % k, (-0.65 + k * 0.05, 0.89, -0.119), (0.012, 0.008, 0.002), "display_green", bevel=0.0)
    hose("LoggerCable", [(-0.55, 0.88, -0.32), (-0.6, 0.6, -0.4), (-0.7, 0.01, -0.5)], 0.006, "plastic_black")


# --- The wash bay's pressure washer (workshop.gd _build_wash_bay) ---------------------------------------

@model("pressure_washer", (0.9, 1.65, 0.5))
def pressure_washer():
    """A trolley pressure washer against the wash bay's back wall (its front +z out into the bay),
    with the hose reel on the wall to its right (the reel's middle at (0.45, 1.4, -0.1))."""
    W, D = 0.5, 0.4
    box("Body", (0.0, 0.42, 0.0), (W - 0.04, 0.6, D - 0.04), "paint_yellow", bevel=0.03)
    box("Hood", (0.0, 0.75, -0.02), (W - 0.08, 0.08, D - 0.1), "plastic_black", bevel=0.02)
    box("Panel", (0.0, 0.55, D / 2 - 0.015), (0.3, 0.18, 0.01), "plastic_black", bevel=0.004)
    cyl("Dial", (-0.07, 0.56, D / 2 - 0.005), 0.035, 0.02, "plastic_grey", axis="z", bevel=0.004)
    cyl("Gauge", (0.08, 0.56, D / 2 - 0.005), 0.03, 0.015, "paint_white", axis="z", bevel=0.003)
    for s in (-1, 1):
        cyl("Wheel%d" % s, (s * (W / 2 + 0.01), 0.12, -D / 2 + 0.08), 0.12, 0.06, "rubber", axis="x", bevel=0.015)
        cyl("Hub%d" % s, (s * (W / 2 + 0.014), 0.12, -D / 2 + 0.08), 0.05, 0.062, "plastic_grey", axis="x")
        tube("Handle%d" % s, [(s * 0.18, 0.7, -D / 2 + 0.03), (s * 0.18, 0.95, -D / 2 + 0.0)], 0.013, "plastic_black")
    box("Foot", (0.0, 0.04, D / 2 - 0.08), (W - 0.12, 0.08, 0.08), "plastic_black", bevel=0.01)
    cyl("Grip", (0.0, 0.95, -D / 2), 0.016, 0.38, "plastic_black", axis="x")
    # The lance and gun in their holster on the side.
    tube("Lance", [(W / 2 + 0.03, 0.25, 0.08), (W / 2 + 0.03, 0.95, 0.06)], 0.008, "steel")
    box("Gun", (W / 2 + 0.03, 0.2, 0.08), (0.04, 0.16, 0.06), "plastic_yellow", bevel=0.008)
    # The reel on the wall bracket, the hose wound on it and running down to the machine.
    box("ReelPlate", (0.45, 1.4, -0.19), (0.14, 0.3, 0.012), "steel_dark", bevel=0.003)
    cyl("ReelAxle", (0.45, 1.4, -0.12), 0.02, 0.16, "steel", axis="z")
    for z in (-0.16, -0.04):
        cyl("ReelSide%.2f" % z, (0.45, 1.4, z), 0.22, 0.01, "plastic_grey", axis="z", bevel=0.003)
    cyl("HoseWound", (0.45, 1.4, -0.1), 0.16, 0.11, "plastic_black", axis="z", bevel=0.02)
    tube("Crank", bent([(0.45, 1.4, -0.03), (0.45, 1.4, -0.0), (0.45, 1.28, -0.0), (0.45, 1.28, 0.05)], 0.01), 0.007, "chrome")
    hose("Hose", [(0.45, 1.24, -0.1), (0.4, 0.9, -0.05), (0.22, 0.5, 0.15), (0.0, 0.35, D / 2)], 0.009, "plastic_black")


# --- Sandblasting cabinets (equipment_view.gd _build_blaster) -----------------------------------------

def _blaster(pid):
    """The engine blast cabinet: legs, the hopper, a chamber (floor at 0.72, roof at size.y - 0.15)
    with a low front and its glove ports, the sloping glass, the turntable grate, ribs, the filter
    box and duct on the back, and a compressor beside it."""
    W, H, D = {"blaster_cabinet": (1.7, 2.35, 1.7), "blaster_pro": (2.0, 2.45, 1.95)}[pid]
    body = "paint_blue" if pid == "blaster_cabinet" else "paint_red"
    floor_y = 0.72
    top = H - 0.15
    front_top = floor_y + 0.43
    glass_z = D / 2 - 0.04 - (top - front_top) * 0.6
    leg_h = floor_y - 0.04
    for x in (-W * 0.45, W * 0.45):
        for z in (-D * 0.42, D * 0.42):
            box("Leg%.2f%.2f" % (x, z), (x, leg_h / 2, z), (0.06, leg_h, 0.06), "steel", bevel=0.004)
            box("Foot%.2f%.2f" % (x, z), (x, 0.005, z), (0.1, 0.01, 0.1), "steel_dark", bevel=0.002)
    cyl("Hopper", (0.0, floor_y - 0.22, 0.0), W * 0.3, 0.36, body, radius2=W * 0.3, bevel=0.005)
    cyl("HopperCone", (0.0, floor_y - 0.5, 0.0), 0.08, 0.2, body, radius2=W * 0.3)
    cyl("Valve", (0.0, floor_y - 0.65, 0.0), 0.04, 0.08, "steel_dark")
    box("Floor", (0.0, floor_y - 0.04, 0.0), (W, 0.08, D), body, bevel=0.006)
    box("Back", (0.0, (floor_y + top) / 2, -D / 2 + 0.02), (W, top - floor_y, 0.04), body, bevel=0.006)
    for s in (-1, 1):
        box("Side%d" % s, (s * (W / 2 - 0.02), (floor_y + top) / 2, 0.0), (0.04, top - floor_y, D), body, bevel=0.006)
        for k in range(3):
            box("Rib%d%d" % (s, k), (s * (W / 2 + 0.01), (floor_y + top) / 2, (-0.35 + 0.35 * k) * D), (0.02, top - floor_y, 0.05), body, bevel=0.004)
    box("Door", (W / 2 + 0.005, (floor_y + top) / 2 - 0.1, 0.0), (0.012, (top - floor_y) * 0.6, D * 0.5), body, bevel=0.003)
    box("DoorHandle", (W / 2 + 0.025, (floor_y + top) / 2 - 0.1, D * 0.2), (0.02, 0.18, 0.025), "chrome", bevel=0.006)
    box("Front", (0.0, (floor_y + front_top) / 2, D / 2 - 0.02), (W, front_top - floor_y, 0.04), body, bevel=0.006)
    roof_d = glass_z + D / 2
    box("Roof", (0.0, top + 0.02, glass_z - roof_d / 2), (W, 0.04, roof_d), body, bevel=0.006)
    # The glass from the front's top up to the roof, and its frame.
    a = (0.0, front_top, D / 2 - 0.02)
    b = (0.0, top + 0.02, glass_z)
    ang = math.degrees(math.atan2(b[1] - a[1], a[2] - b[2]))
    L = math.hypot(b[1] - a[1], a[2] - b[2])
    mid = (0.0, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2)
    box("Glass", mid, (W - 0.06, 0.008, L - 0.006), "glass", bevel=0.0, rot=(ang, 0, 0))   # (ends inside the bars)
    for s in (-1, 1):
        box("GlassRail%d" % s, (s * (W / 2 - 0.03), mid[1], mid[2]), (0.05, 0.05, L), "paint_black", bevel=0.005, rot=(ang, 0, 0))
    # (3 mm short of the sides' outer faces each end: flush, they z-fought.)
    box("GlassLow", (0.0, front_top, D / 2 - 0.02), (W - 0.006, 0.05, 0.05), "paint_black", bevel=0.005)
    box("GlassHigh", (0.0, top + 0.02, glass_z), (W - 0.006, 0.05, 0.05), "paint_black", bevel=0.005)
    box("Lamp", (0.0, top - 0.01, glass_z - 0.2), (W * 0.6, 0.02, 0.12), "led_white", bevel=0.0)
    port_y = (floor_y + front_top) / 2
    for s in (-1, 1):
        torus("Port%d" % s, (s * 0.3, port_y, D / 2 + 0.005), 0.1, 0.018, "rubber", axis="z")
        cyl("Glove%d" % s, (s * 0.3, port_y - 0.1, D / 2 - 0.2), 0.065, 0.4, "rubber", axis="z", bevel=0.03, rot=(-20, 0, 0))
    grate_r = min(W, D) * 0.36
    turn_z = -D * 0.06
    cyl("Turntable", (0.0, floor_y + 0.015, turn_z), grate_r, 0.03, "steel", verts=48)
    for k in range(6):
        box("Bar%d" % k, (0.0, floor_y + 0.035, turn_z), (grate_r * 2, 0.012, 0.02), "steel_dark", bevel=0.0, rot=(0, 30 * k, 0))
    box("Filter", (W * 0.25, top - 0.35, -D / 2 - 0.12), (0.4, 0.6, 0.2), "paint_black", bevel=0.01)
    cyl("Duct", (W * 0.25, top + 0.12, -D / 2 - 0.12), 0.08, 0.3, "steel")
    box("Pedal", (0.0, 0.03, D / 2 + 0.1), (0.14, 0.04, 0.22), "steel_dark", bevel=0.006)
    hose("PedalLine", [(0.0, 0.05, D / 2), (0.1, 0.3, D / 2 - 0.1), (W / 2 - 0.1, floor_y - 0.3, 0.0)], 0.007, "plastic_black")
    # A compressor tank beside it on the left.
    cyl("Compressor", (-W / 2 - 0.25, 0.35, -D / 2 + 0.35), 0.18, 0.55, "paint_red" if pid == "blaster_cabinet" else "paint_blue", axis="z", bevel=0.05)
    cyl("CompMotor", (-W / 2 - 0.25, 0.6, -D / 2 + 0.25), 0.08, 0.18, "paint_black", axis="x", bevel=0.01)
    for z in (-D / 2 + 0.15, -D / 2 + 0.55):
        box("CompFoot%.2f" % z, (-W / 2 - 0.25, 0.08, z), (0.3, 0.16, 0.04), "steel_dark", bevel=0.004)
    hose("AirHose", [(-W / 2 - 0.25, 0.55, -D / 2 + 0.6), (-W / 2 - 0.1, 0.4, -D / 2 + 0.7), (-W / 2 + 0.02, floor_y - 0.1, -D / 4)], 0.01, "plastic_yellow")


for _pid, _size in (("blaster_cabinet", (1.7, 2.35, 1.7)), ("blaster_pro", (2.0, 2.45, 1.95))):
    MODELS[_pid] = ((lambda p: (lambda: _blaster(p)))(_pid), _size)


# --- Trinkets (trinkets.gd): standing on a surface (origin under them) or on a wall (origin on it) -----

@model("trinket_bobblehead", (0.12, 0.22, 0.12))
def trinket_bobblehead():
    cyl("Base", (0.0, 0.01, 0.0), 0.05, 0.02, "plastic_black", bevel=0.004)
    box("Legs", (0.0, 0.05, 0.0), (0.035, 0.06, 0.025), "leather_brown", bevel=0.008)
    box("Body", (0.0, 0.1, 0.0), (0.045, 0.05, 0.03), "paint_red", bevel=0.01)
    cyl("Spring", (0.0, 0.13, 0.0), 0.006, 0.02, "chrome")
    sphere("Helmet", (0.0, 0.17, 0.0), 0.045, "paint_white")
    box("Visor", (0.0, 0.168, 0.036), (0.06, 0.022, 0.012), "plastic_black", bevel=0.006)
    box("Stripe", (0.0, 0.2, 0.0), (0.012, 0.03, 0.09), "paint_red", bevel=0.004)


@model("trinket_gnome", (0.22, 0.4, 0.22))
def trinket_gnome():
    cyl("Base", (0.0, 0.02, 0.0), 0.1, 0.04, "concrete_grey" if False else "paint_grey", bevel=0.01)
    cyl("Body", (0.0, 0.13, 0.0), 0.085, 0.18, "plastic_black", radius2=0.06, bevel=0.02)
    for s in (-1, 1):
        cyl("Boot%d" % s, (s * 0.04, 0.05, 0.03), 0.03, 0.04, "leather_brown", axis="z", bevel=0.01)
    sphere("Beard", (0.0, 0.24, 0.035), 0.055, "paint_white", scale=(1, 1.2, 0.8))
    sphere("Face", (0.0, 0.27, 0.03), 0.045, "paint_cream")
    sphere("Nose", (0.0, 0.27, 0.075), 0.014, "paint_red")
    sphere("Helmet", (0.0, 0.3, 0.0), 0.06, "paint_red", scale=(1, 0.8, 1))
    box("Goggles", (0.0, 0.3, 0.05), (0.09, 0.02, 0.02), "chrome", bevel=0.006)


@model("trinket_lava", (0.14, 0.42, 0.14))
def trinket_lava():
    cyl("Base", (0.0, 0.06, 0.0), 0.065, 0.12, "aluminium", radius2=0.035, bevel=0.005)
    cyl("Glass", (0.0, 0.25, 0.0), 0.035, 0.26, "glass", radius2=0.055, bevel=0.0)
    cyl("Fluid", (0.0, 0.25, 0.0), 0.032, 0.25, "lava_glow", radius2=0.05, bevel=0.0)
    cyl("Cap", (0.0, 0.4, 0.0), 0.025, 0.04, "aluminium", radius2=0.015, bevel=0.003)
    for k, (y, r) in enumerate(((0.17, 0.02), (0.26, 0.016), (0.33, 0.012))):
        sphere("Blob%d" % k, (0.005 * k, y, 0.0), r, "lava_blob", scale=(1, 1.4, 1))


@model("trinket_clock_digital", (0.52, 0.2, 0.05))
def trinket_clock_digital():
    box("Case", (0.0, 0.0, 0.025), (0.52, 0.2, 0.05), "plastic_black", bevel=0.008)
    box("Face", (0.0, 0.0, 0.0505), (0.48, 0.16, 0.001), "screen", bevel=0.0)
    for k, x in enumerate((-0.17, -0.07, 0.07, 0.17)):
        for seg in ((0, 0.05, 0.05, 0.008), (0, 0, 0.05, 0.008), (0, -0.05, 0.05, 0.008), (-0.028, 0.025, 0.008, 0.045), (0.028, -0.025, 0.008, 0.045)):
            box("Seg%d_%.3f%.3f" % (k, seg[0], seg[1]), (x + seg[0], seg[1], 0.0515), (seg[2], seg[3], 0.001), "display_red", bevel=0.0)
    for y in (0.025, -0.025):
        box("Colon%.3f" % y, (0.0, y, 0.0515), (0.01, 0.01, 0.001), "display_red", bevel=0.0)


@model("trinket_clock_desk", (0.18, 0.09, 0.07))
def trinket_clock_desk():
    prism("Case", [(-0.035, 0.0), (0.035, 0.0), (0.025, 0.09), (-0.03, 0.09)], 0.18, "plastic_black", axis="x", bevel=0.008)
    box("Face", (0.0, 0.045, 0.031), (0.15, 0.05, 0.002), "display_green", bevel=0.0, rot=(-6, 0, 0))
    box("Snooze", (0.0, 0.092, -0.005), (0.08, 0.006, 0.025), "plastic_grey", bevel=0.002)


@model("trinket_trophy", (0.2, 0.4, 0.2))
def trinket_trophy():
    box("Plinth", (0.0, 0.04, 0.0), (0.16, 0.08, 0.16), "wood_top", bevel=0.006)
    box("Plaque", (0.0, 0.04, 0.081), (0.1, 0.035, 0.002), "brass", bevel=0.0)
    cyl("Foot", (0.0, 0.095, 0.0), 0.05, 0.03, "brass", radius2=0.03)
    cyl("Stem", (0.0, 0.15, 0.0), 0.012, 0.08, "brass")
    cyl("Cup", (0.0, 0.27, 0.0), 0.03, 0.16, "brass", radius2=0.075, verts=40, bevel=0.003)
    for s in (-1, 1):
        torus("Handle%d" % s, (s * 0.075, 0.28, 0.0), 0.04, 0.006, "brass", axis="z", verts=24)


@model("trinket_rug", (1.6, 0.012, 1.0))
def trinket_rug():
    n, m = 8, 5
    for i in range(n):
        for j in range(m):
            box("Sq%d_%d" % (i, j), (-0.8 + 0.1 + i * 0.2, 0.006, -0.5 + 0.1 + j * 0.2), (0.2, 0.012, 0.2),
                "rug_white" if (i + j) % 2 else "rug_black", bevel=0.0)


@model("trinket_neon", (0.9, 0.36, 0.06))
def trinket_neon():
    box("Backboard", (0.0, 0.0, 0.006), (0.9, 0.36, 0.012), "glass", bevel=0.003)
    for k, (x, y) in enumerate(((-0.36, 0.1), (0.36, 0.1), (-0.36, -0.1), (0.36, -0.1))):
        cyl("Stand%d" % k, (x, y, 0.025), 0.006, 0.03, "chrome", axis="z")
    # TORQUE SHED as simple stroked neon tubes: a frame round it and two rows of strokes.
    tube("Frame", [(-0.42, -0.15, 0.04), (-0.42, 0.15, 0.04), (0.42, 0.15, 0.04), (0.42, -0.15, 0.04), (-0.42, -0.15, 0.04)], 0.006, "neon_pink", sides=8)
    def letter(strokes, x, y, s=0.05):
        for a, b in strokes:
            tube("L%.3f%.3f%.2f%.2f" % (x, y, a[0], a[1]), [(x + a[0] * s, y + a[1] * s, 0.04), (x + b[0] * s, y + b[1] * s, 0.04)], 0.005, "neon_pink", sides=8)
    L = {"T": [((0, 2), (2, 2)), ((1, 2), (1, 0))], "O": [((0, 0), (0, 2)), ((0, 2), (2, 2)), ((2, 2), (2, 0)), ((2, 0), (0, 0))],
         "R": [((0, 0), (0, 2)), ((0, 2), (2, 2)), ((2, 2), (2, 1)), ((2, 1), (0, 1)), ((0, 1), (2, 0))],
         "Q": [((0, 0), (0, 2)), ((0, 2), (2, 2)), ((2, 2), (2, 0)), ((2, 0), (0, 0)), ((1.2, 0.6), (2.3, -0.3))],
         "U": [((0, 2), (0, 0)), ((0, 0), (2, 0)), ((2, 0), (2, 2))], "E": [((2, 2), (0, 2)), ((0, 2), (0, 0)), ((0, 0), (2, 0)), ((0, 1), (1.5, 1))],
         "S": [((2, 2), (0, 2)), ((0, 2), (0, 1)), ((0, 1), (2, 1)), ((2, 1), (2, 0)), ((2, 0), (0, 0))], "H": [((0, 0), (0, 2)), ((2, 0), (2, 2)), ((0, 1), (2, 1))],
         "D": [((0, 0), (0, 2)), ((0, 2), (1.5, 2)), ((1.5, 2), (2, 1.5)), ((2, 1.5), (2, 0.5)), ((2, 0.5), (1.5, 0)), ((1.5, 0), (0, 0))]}
    for row, (word, y) in enumerate((("TORQUE", 0.02), ("SHED", -0.12))):
        w = len(word) * 0.13
        for k, ch in enumerate(word):
            letter(L[ch], -w / 2 + k * 0.13 + 0.015, y)


# --- The dyno room's ear defenders on their hook, and the petrol pump -----------------------------------

@model("ear_defenders", (0.2, 0.25, 0.12))
def ear_defenders():
    """Ear defenders hanging on their wall hook (dyno_room.gd PHONES_HOOK): origin on the wall."""
    box("HookPlate", (0.0, 0.0, 0.004), (0.04, 0.06, 0.008), "steel_dark", bevel=0.002)
    tube("Hook", bent([(0.0, 0.0, 0.008), (0.0, 0.0, 0.06), (0.0, 0.025, 0.07)], 0.01), 0.004, "steel")
    tube("Band", bent([(-0.075, -0.11, 0.06), (-0.075, -0.01, 0.06), (0.0, 0.01, 0.06), (0.075, -0.01, 0.06), (0.075, -0.11, 0.06)], 0.05), 0.008, "plastic_black")
    for s in (-1, 1):
        cyl("Cup%d" % s, (s * 0.068, -0.14, 0.06), 0.045, 0.04, "plastic_yellow", axis="x", bevel=0.012)
        cyl("Pad%d" % s, (s * 0.044, -0.14, 0.06), 0.04, 0.015, "foam_black", axis="x", bevel=0.006)


@model("petrol_pump", (0.55, 1.75, 0.45))
def petrol_pump():
    """shed_pump_view.gd: a red wall-mounted pump cabinet with a cream face, the lit display up top,
    the empty holster on the right, and the pipe from the tank up out of the ground. Origin on the
    wall at the ground. The nozzle and its hose are the game's (Shed/TorqueShedPump.cpp draws them,
    in the holster or in someone's hand), so they aren't in the model."""
    box("WallPlate", (0.0, 1.05, 0.006), (0.5, 0.95, 0.012), "steel_dark", bevel=0.003)
    box("Case", (0.0, 1.05, 0.13), (0.46, 0.9, 0.24), "paint_red", bevel=0.02)
    box("Face", (0.0, 1.0, 0.252), (0.36, 0.55, 0.004), "paint_cream", bevel=0.002)
    box("Display", (0.0, 1.3, 0.255), (0.3, 0.14, 0.004), "screen", bevel=0.0)
    for k in range(2):
        box("Digits%d" % k, (0.03, 1.33 - k * 0.06, 0.258), (0.2, 0.035, 0.001), "display_green", bevel=0.0)
    box("Badge", (0.0, 0.85, 0.256), (0.16, 0.08, 0.002), "paint_red", bevel=0.0)
    box("Top", (0.0, 1.52, 0.13), (0.48, 0.04, 0.26), "paint_white", bevel=0.01)
    box("Holster", (0.28, 1.0, 0.15), (0.08, 0.2, 0.1), "plastic_black", bevel=0.01)
    box("Bracket", (0.25, 1.0, 0.1), (0.04, 0.06, 0.06), "steel_dark", bevel=0.004)
    tube("Pipe", [(-0.15, 0.0, 0.08), (-0.15, 0.6, 0.08)], 0.025, "steel")
    cyl("PipeCollar", (-0.15, 0.02, 0.08), 0.045, 0.04, "steel_dark")


# --- Hand tools (tool_models.gd): held, forward -z, up +y ------------------------------------------------

@model("tool_brush", (0.04, 0.28, 0.03))
def tool_brush():
    box("Handle", (0.0, -0.075, 0.004), (0.03, 0.16, 0.02), "wood_top", bevel=0.009)
    box("Head", (0.0, 0.055, 0.0), (0.04, 0.12, 0.02), "wood_top", bevel=0.004)
    for i in range(6):
        for j in range(3):
            box("Bristle%d_%d" % (i, j), (-0.012 + j * 0.012, 0.015 + i * 0.016, -0.018), (0.008, 0.012, 0.018), "steel", bevel=0.0)


@model("tool_repair", (0.1, 0.25, 0.14))
def tool_repair():
    """An angle grinder: body down the hand, the gearhead and guarded disc at the top."""
    cyl("Body", (0.0, -0.05, 0.0), 0.028, 0.18, "paint_blue", radius2=0.026, bevel=0.006)
    cyl("Grip", (0.0, -0.095, 0.0), 0.029, 0.075, "rubber", bevel=0.005)
    cyl("Tail", (0.0, -0.15, 0.0), 0.026, 0.03, "paint_blue", radius2=0.012, bevel=0.005)
    cyl("Cable", (0.0, -0.2, 0.0), 0.004, 0.06, "rubber")
    box("Gearhead", (0.0, 0.055, -0.01), (0.05, 0.04, 0.06), "aluminium", bevel=0.01)
    cyl("Disc", (0.0, 0.07, -0.05), 0.058, 0.004, "steel_dark", axis="x")
    cyl("Guard", (-0.004, 0.08, -0.04), 0.062, 0.02, "steel", axis="x", bevel=0.002)
    cyl("SideHandle", (0.045, 0.06, 0.0), 0.012, 0.07, "plastic_black", axis="x", bevel=0.004)


@model("tool_lance", (0.06, 0.2, 0.75))
def tool_lance():
    box("Gun", (0.0, 0.0, -0.02), (0.034, 0.045, 0.12), "plastic_yellow", bevel=0.008)
    cyl("Back", (0.0, 0.0, 0.04), 0.0225, 0.034, "plastic_yellow", axis="x")
    box("Grip", (0.0, -0.07, 0.055), (0.03, 0.12, 0.035), "plastic_black", bevel=0.01, rot=(-18, 0, 0))
    box("Trigger", (0.0, -0.05, 0.012), (0.012, 0.075, 0.012), "plastic_yellow", bevel=0.003, rot=(-14, 0, 0))
    cyl("Lance", (0.0, 0.0, -0.35), 0.006, 0.5, "steel", axis="z")
    cyl("Nozzle", (0.0, 0.0, -0.6), 0.011, 0.04, "plastic_black", axis="z", bevel=0.003)


@model("tool_gun", (0.05, 0.25, 0.15))
def tool_gun():
    """An HVLP spray gun: aluminium body, air cap, trigger, the cup on top (tinted by the game)."""
    by = 0.0
    cyl("Barrel", (0.0, by, -0.045), 0.0135, 0.1, "aluminium", axis="z", radius2=0.0155)
    box("Body", (0.0, by - 0.006, 0.01), (0.024, 0.03, 0.05), "aluminium", bevel=0.005)
    cyl("AirCap", (0.0, by, -0.1), 0.0125, 0.012, "chrome", axis="z")
    for s in (-1, 1):
        box("Horn%d" % s, (s * 0.0125, by, -0.109), (0.006, 0.006, 0.014), "chrome", bevel=0.001)
    box("Handle", (0.0, -0.07, 0.03), (0.022, 0.12, 0.03), "aluminium", bevel=0.008, rot=(-12, 0, 0))
    box("Trigger", (0.0, -0.035, -0.02), (0.008, 0.06, 0.01), "chrome", bevel=0.002, rot=(-10, 0, 0))
    cyl("Cup", (0.0, 0.065, -0.02), 0.03, 0.09, "plastic_white", bevel=0.005)
    cyl("Lid", (0.0, 0.112, -0.02), 0.032, 0.008, "aluminium")
    cyl("Inlet", (0.0, -0.135, 0.04), 0.006, 0.02, "brass")


@model("tool_polisher", (0.08, 0.12, 0.25))
def tool_polisher():
    cyl("Body", (0.0, 0.0, 0.02), 0.034, 0.15, "plastic_red", axis="z", bevel=0.008)
    sphere("Back", (0.0, 0.0, 0.095), 0.032, "plastic_red")
    cyl("Pad", (0.0, 0.0, -0.075), 0.037, 0.05, "foam_black", axis="z", bevel=0.01)
    cyl("Plate", (0.0, 0.0, -0.045), 0.03, 0.01, "plastic_black", axis="z")
    box("Handle", (0.0, 0.055, 0.03), (0.022, 0.02, 0.1), "plastic_black", bevel=0.006)


@model("tool_applicator", (0.06, 0.035, 0.09))
def tool_applicator():
    box("Block", (0.0, 0.0, -0.02), (0.06, 0.03, 0.09), "paint_blue", bevel=0.006)
    box("Cloth", (0.0, -0.018, -0.02), (0.062, 0.006, 0.092), "plastic_white", bevel=0.002)


@model("tool_stopwatch", (0.07, 0.09, 0.02))
def tool_stopwatch():
    cyl("Case", (0.0, 0.0, 0.0), 0.03, 0.016, "chrome", axis="z", bevel=0.004)
    cyl("Face", (0.0, 0.0, 0.0085), 0.026, 0.001, "paint_white", axis="z")
    cyl("Glass", (0.0, 0.0, 0.0095), 0.027, 0.001, "glass", axis="z")
    cyl("Crown", (0.0, 0.036, 0.0), 0.005, 0.012, "chrome")
    torus("Loop", (0.0, 0.046, 0.0), 0.007, 0.0015, "chrome", axis="z", verts=16)
    for s in (-1, 1):
        cyl("Pusher%d" % s, (s * 0.02, 0.026, 0.0), 0.003, 0.008, "chrome", rot=(0, 0, -s * 40))


# --- The paint shop's spray bay (equipment 'booth_shop'; ue_build_shed.py paint_shop()) -----------------

@model("booth_shop", (8.0, 2.7, 4.7))
def booth_shop():
    """The downdraft spray bay's shell to the shed's boxes (BOOTH_X0..X1 925..1395, half width 400,
    height 270): insulated panel walls with aluminium trims and seams, lit side panels, steel posts
    at the open front, the pleated filter wall with its grille at the back, and a walk-on floor grate
    over the extraction pit. The rails, drop rods and hangers are the shed's (they move); nothing
    here comes below the rails' 2.35 m inside the bay, so the hooks swing clear."""
    W, H, D = 8.0, 2.7, 4.7
    t = 0.05
    box("Back", (0.0, H / 2, -D / 2 + t / 2), (W, H, t), "paint_white", bevel=0.004)
    for s in (-1, 1):
        box("Side%d" % s, (s * (W / 2 - t / 2), H / 2, 0.0), (t, H, D), "paint_white", bevel=0.004)
        # Panel seams every 1.17 m and a cap trim along the top.
        for k in range(1, 4):
            box("Seam%d_%d" % (s, k), (s * (W / 2 - t - 0.002), H / 2, -D / 2 + k * D / 4), (0.006, H, 0.02), "aluminium", bevel=0.001)
        box("Cap%d" % s, (s * (W / 2 - t / 2), H + 0.015, 0.0), (t + 0.03, 0.03, D + 0.03), "aluminium", bevel=0.004)
        # Two tall light panels on each side wall, angled in.
        for k, z in enumerate((-1.1, 0.6)):
            box("LightBox%d_%d" % (s, k), (s * (W / 2 - t - 0.06), 1.5, z), (0.12, 1.9, 0.5), "aluminium", bevel=0.01)
            box("LightLens%d_%d" % (s, k), (s * (W / 2 - t - 0.121), 1.5, z), (0.004, 1.84, 0.44), "led_white", bevel=0.0)
        # The front posts, a kick plate.
        box("Post%d" % s, (s * (W / 2 - 0.025), H / 2, D / 2 - 0.03), (0.09, H, 0.06), "steel", bevel=0.006)
        box("Kick%d" % s, (s * (W / 2 - t - 0.002), 0.1, 0.0), (0.004, 0.2, D), "steel", bevel=0.001)
    for k in range(1, 7):
        box("BackSeam%d" % k, (-W / 2 + k * W / 7, H / 2, -D / 2 + t + 0.002), (0.02, H, 0.006), "aluminium", bevel=0.001)
    box("BackCap", (0.0, H + 0.015, -D / 2 + t / 2), (W + 0.03, 0.03, t + 0.03), "aluminium", bevel=0.004)
    # The filter wall: a frame of pleated media and the grille bars over it.
    fz = -D / 2 + t + 0.02
    box("FilterFrame", (0.0, 1.215, fz + 0.01), (6.5, 1.66, 0.03), "steel", bevel=0.004)
    for k in range(64):
        x = -3.2 + (k + 0.5) * 0.1
        box("Pleat%d" % k, (x, 1.215, fz + 0.03), (0.07, 1.6, 0.02), "filter_media", bevel=0.0, rot=(0, 25 if k % 2 else -25, 0))
    for i in range(6):
        box("Grille%d" % i, (0.0, 0.54 + i * 0.27, fz + 0.05), (6.4, 0.012, 0.01), "steel", bevel=0.001)
    for k in range(9):
        box("GrilleV%d" % k, (-3.2 + k * 0.8, 1.215, fz + 0.05), (0.012, 1.6, 0.01), "steel", bevel=0.001)
    box("FilterSign", (2.6, 2.2, fz + 0.01), (0.5, 0.18, 0.004), "label_yellow", bevel=0.0)
    # The floor grate over the pit: a steel frame, the pit's dark beneath and a grid of flat bars.
    box("Pit", (0.0, 0.004, 0.0), (W - 0.1, 0.008, D - 0.1), "paint_black", bevel=0.0)
    box("GrateFrameF", (0.0, 0.02, D / 2 - 0.07), (W - 0.1, 0.04, 0.04), "steel", bevel=0.003)
    box("GrateFrameB", (0.0, 0.02, -D / 2 + 0.07), (W - 0.1, 0.04, 0.04), "steel", bevel=0.003)
    for k in range(13):
        box("GrateBeam%d" % k, (-W / 2 + 0.07 + k * (W - 0.14) / 12, 0.02, 0.0), (0.04, 0.04, D - 0.1), "steel", bevel=0.003)
    for k in range(int((D - 0.2) / 0.03)):
        box("GrateBar%d" % k, (0.0, 0.035, -D / 2 + 0.1 + k * 0.03), (W - 0.14, 0.03, 0.004), "steel_dark", bevel=0.0)


# --- Deliveries: cardboard boxes, wooden crates, the courier's label and the sticky note --------------
# Drawn at a nominal size; the game scales each to the part's box (TorqueShedItems KindSize + 8 cm),
# choosing the variant nearest in shape (Data/Props/deliveries.json) so nothing stretches far. The
# label and the note are separate little meshes the game lays on at their real size, so they never
# stretch at all.

def _cardboard(W, H, D):
    box("Body", (0.0, H / 2, 0.0), (W, H - 0.004, D), "cardboard", bevel=0.006)
    # The two top flaps meeting down the middle, and the bottom's the same.
    for y in (H - 0.002, 0.002):
        box("Seam%.3f" % y, (0.0, y, 0.0), (W - 0.01, 0.003, 0.004), "cardboard_dark", bevel=0.0)
    # Brown parcel tape along the seam and down both ends.
    box("TapeTop", (0.0, H + 0.0005, 0.0), (W + 0.004, 0.002, 0.05), "parcel_tape", bevel=0.0)
    for s in (-1, 1):
        box("TapeEnd%d" % s, (s * (W / 2 + 0.0005), H - 0.06, 0.0), (0.002, 0.12, 0.05), "parcel_tape", bevel=0.0)
    # Printed handling marks on the front: an arrow pair ("this way up") and a glass.
    for k in (-1, 1):
        prism("Arrow%d" % k, [(k * 0.02 - 0.012, 0.0), (k * 0.02 + 0.012, 0.0), (k * 0.02 + 0.012, 0.035), (k * 0.02 + 0.022, 0.035),
                              (k * 0.02, 0.06), (k * 0.02 - 0.022, 0.035), (k * 0.02 - 0.012, 0.035)], 0.001, "ink_black", axis="z",
              centre=D / 2 + 0.0006, bevel=0.0)
        ax, ay, _ = delivery_layout.cardboard(W, H, D)["arrows"]
        kit._pieces[-1].location.x += ax
        kit._pieces[-1].location.z += ay


@model("box_cardboard", (0.45, 0.35, 0.4))
def box_cardboard():
    _cardboard(0.45, 0.35, 0.4)


@model("box_cardboard_long", (0.95, 0.3, 0.32))
def box_cardboard_long():
    _cardboard(0.95, 0.3, 0.32)


@model("box_cardboard_flat", (0.7, 0.25, 0.6))
def box_cardboard_flat():
    _cardboard(0.7, 0.25, 0.6)


def _crate(W, H, D):
    """A nailed pine crate: a board skin on every face, battens round each face's edges, the
    corner posts, a pallet-style skid underneath, nail heads and a stencilled band."""
    skid = 0.08
    for z in (-D / 2 + 0.05, 0.0, D / 2 - 0.05):
        box("Skid%.2f" % z, (0.0, skid / 2, z), (W, skid, 0.07), "crate_wood_dark", bevel=0.004)
    y0 = skid
    h = H - skid
    # Board skins: horizontal boards on the front, back and ends, boards along x on the lid.
    nb = max(3, int(round(h / 0.14)))
    for i in range(nb):
        y = y0 + h * (i + 0.5) / nb
        bh = h / nb - 0.008
        for s in (-1, 1):
            box("FB%d_%d" % (i, s), (0.0, y, s * (D / 2 - 0.009)), (W - 0.004, bh, 0.018), "crate_wood", bevel=0.002)
            box("EB%d_%d" % (i, s), (s * (W / 2 - 0.009), y, 0.0), (0.018, bh, D - 0.04), "crate_wood", bevel=0.002)
    nl = max(3, int(round(D / 0.14)))
    for i in range(nl):
        z = -D / 2 + D * (i + 0.5) / nl
        box("Lid%d" % i, (0.0, H - 0.009, z), (W - 0.004, 0.018, D / nl - 0.008), "crate_wood", bevel=0.002)
    # Battens framing each face.
    bt, bw = 0.022, 0.07
    for s in (-1, 1):
        for y in (y0 + bw / 2, H - bw / 2):
            box("BatH%d%.2f" % (s, y), (0.0, y, s * (D / 2 + bt / 2)), (W, bw, bt), "crate_wood_dark", bevel=0.003)
        for x in (-W / 2 + bw / 2, W / 2 - bw / 2):
            box("BatV%d%.2f" % (s, x), (x, y0 + h / 2, s * (D / 2 + bt / 2)), (bw, h - 2 * bw, bt), "crate_wood_dark", bevel=0.003)
        if W > 0.9:   # a diagonal brace on the long faces
            L = math.hypot(W - 2 * bw, h - 2 * bw)
            ang = math.degrees(math.atan2(h - 2 * bw, W - 2 * bw))
            box("Brace%d" % s, (0.0, y0 + h / 2, s * (D / 2 + bt / 2)), (L, bw * 0.9, bt * 0.9), "crate_wood_dark", bevel=0.003, rot=(0, 0, ang * s))
        for z in (-D / 2 + bw / 2, D / 2 - bw / 2):
            box("BatE%d%.2f" % (s, z), (s * (W / 2 + bt / 2), y0 + h / 2, z), (bt, h, bw), "crate_wood_dark", bevel=0.003)
        box("LidBat%d" % s, (s * (W / 2 - bw / 2), H + bt / 2, 0.0), (bw, bt, D), "crate_wood_dark", bevel=0.003)
    # Nail heads at the batten corners, and a stencilled band on the front.
    for s in (-1, 1):
        for x in (-W / 2 + bw / 2, W / 2 - bw / 2):
            for y in (y0 + bw / 2, H - bw / 2):
                cyl("Nail%d%.2f%.2f" % (s, x, y), (x, y, s * (D / 2 + bt + 0.0005)), 0.004, 0.002, "steel_dark", axis="z", verts=8, bevel=0.0)
    # Printing goes on the boards (flush), in the spots delivery_layout keeps clear of the brace.
    lay = delivery_layout.crate(W, H, D)
    sx, sy, _ = lay["stencil"]
    box("Stencil", (sx, sy, D / 2 + 0.0006), (lay["stencil_w"], 0.04, 0.001), "ink_black", bevel=0.0)
    ax, ay, _ = lay["arrows"]
    for k in range(2):
        prism("Up%d" % k, [(-0.01, 0.0), (0.01, 0.0), (0.01, 0.03), (0.02, 0.03), (0.0, 0.05), (-0.02, 0.03), (-0.01, 0.03)], 0.001, "ink_black",
              axis="z", centre=D / 2 + 0.0006, bevel=0.0)
        kit._pieces[-1].location.x += ax + k * 0.05
        kit._pieces[-1].location.z += ay


@model("crate_wood", (0.75, 0.65, 0.65))
def crate_wood():
    _crate(0.75, 0.65, 0.65)


@model("crate_wood_long", (1.5, 0.55, 0.6))
def crate_wood_long():
    _crate(1.5, 0.55, 0.6)


@model("crate_wood_flat", (1.0, 0.45, 0.85))
def crate_wood_flat():
    _crate(1.0, 0.45, 0.85)


@model("courier_label", (0.16, 0.0, 0.1))
def courier_label():
    """The shipping label (10 x 16 cm), lying face up with its top edge towards -z; its face is one
    quad UV'd 0..1 for the courier atlas (the game picks the row), so it isn't box-UV'd."""
    box("Label", (0.0, 0.0004, 0.0), (0.16, 0.0008, 0.1), "courier_label", bevel=0.0)


@model("sticky_note", (0.076, 0.076, 0.0))
def sticky_note():
    """A 76 mm square sticky note standing on a face (facing +z), its bottom corner lifting a touch."""
    box("Note", (0.0, 0.0, 0.0004), (0.076, 0.07, 0.0008), "sticky_note", bevel=0.0)
    box("Curl", (0.0, -0.0375, 0.0016), (0.076, 0.006, 0.0008), "sticky_note", bevel=0.0, rot=(-25, 0, 0))


# --- Running it --------------------------------------------------------------------------------------

def main():
    import bpy
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if "--list" in args:
        for pid, (_, size) in sorted(MODELS.items()):
            print("%-24s %.2f x %.2f x %.2f" % ((pid,) + tuple(size)))
        return
    spec = json.load(open(os.path.join(ROOT, "Data", "Props", "materials.json")))
    kit.set_materials(spec["materials"])
    os.makedirs(OUT, exist_ok=True)
    want = [a for a in args if not a.startswith("-")] or sorted(MODELS)
    report = {}
    for pid in want:
        fn, size = MODELS[pid]
        kit.reset()
        for m in list(bpy.data.materials):
            bpy.data.materials.remove(m)
        fn()
        obj = kit.finish("SM_" + pid)
        lo, hi = kit.bounds(obj)
        path = os.path.join(OUT, pid + ".glb")
        kit.export(path)
        tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        report[pid] = {"bounds_min": [round(v, 4) for v in lo], "bounds_max": [round(v, 4) for v in hi], "tris": tris,
                       "materials": [s.material.name for s in obj.material_slots]}
        print("[props] %-22s %6d tris  %s .. %s" % (pid, tris, tuple(round(v, 3) for v in lo), tuple(round(v, 3) for v in hi)))
    with open(os.path.join(OUT, "_built.json"), "w") as f:
        old = {}
        try:
            old = json.load(open(os.path.join(OUT, "_built.json")))
        except Exception:
            pass
        old.update(report)
        json.dump(dict(sorted(old.items())), f, indent=1)


main()
