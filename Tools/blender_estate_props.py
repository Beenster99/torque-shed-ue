"""What stands round Ride UK's industrial estates and retail parks (stage 3 of the everyday kits plan, Nov 26):
lorry trailers backed up to the docks, vans by the roller doors, skips, pallets, shipping containers, gas
bottle cages, palisade fencing and its sliding gate, the estate's name board, a supermarket's trolley shelters,
yard lamps and big wheelie bins. Our own simple models of the everyday things, modelled by script; no maker's
shape, livery, name or logo.

Each prop is a Unit of parts (Tools/blender_warehouse_kit.py's), every part tagged with the furthest level of
detail it stays in: 0 close by only, 1 the middle distance too, 2 always. The game lays props near (its middle
and near squares only), so a prop is its plain shapes at 1 and 2 and its small details at 0.

Frame: x along the prop's length, centred on 0; y across it, its front (the side a reader sees: the board's
face, the fence's road side) towards -y; z up from the ground at 0. Tools/blender_warehouse_kit_export.py
writes them into the kit as "prop/<name>", with their sizes.

    python Tools/blender_estate_props.py --render DIR     # the props' review sheet
"""
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_warehouse_kit as K  # noqa: E402
from blender_street_kit import _object, box  # noqa: E402

TYRE = (0.018, 0.018, 0.02)
HUB = (0.3, 0.31, 0.32)
CHASSIS = (0.03, 0.032, 0.035)
WOOD = (0.34, 0.22, 0.11)
WOOD_DARK = (0.12, 0.075, 0.04)
GALV = (0.34, 0.35, 0.35)
PALISADE_GREEN = (0.025, 0.09, 0.04)
TAIL = (0.4, 0.02, 0.01)
AMBER = (0.7, 0.3, 0.0)
NAVY = (0.012, 0.025, 0.07)
CHROME = (0.45, 0.46, 0.47)


def cyl(name, centre, r, length, axis="y", n=8):
    """A low-sided cylinder of radius r and the given length about its centre, its axis along x, y or z."""
    c = Vector(centre)
    rings = []
    for t in (-length / 2, length / 2):
        ring = []
        for k in range(n):
            a = 2 * math.pi * (k + 0.5) / n
            u, v = r * math.cos(a), r * math.sin(a)
            if axis == "x":
                ring.append(tuple(c + Vector((t, u, v))))
            elif axis == "y":
                ring.append(tuple(c + Vector((u, t, v))))
            else:
                ring.append(tuple(c + Vector((u, v, t))))
        rings.append(ring)
    return K.solid(name, rings)


def prism(name, outline, y0, y1):
    """A slab whose face is a convex outline [(x, z)], from y0 to y1 across."""
    return K.solid(name, [[(x, y, z) for x, z in outline] for y in (y0, y1)])


def add_box(U, name, x0, x1, y0, y1, z0, z1, rgb, lod, surface="paint"):
    return U.add(box(name, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), (x1 - x0, y1 - y0, z1 - z0)), surface, rgb, lod)


def wheel(U, x, y, r, width, lod=1):
    U.add(cyl("tyre", (x, y, r), r, width), "rubber", TYRE, lod)
    side = 1 if y > 0 else -1
    U.add(cyl("hub", (x, y + side * width / 2, r), r * 0.55, 0.02, n=6), "metal", HUB, 0)


# --- vehicles ----------------------------------------------------------------------------------------------

def trailer(body_rgb, kind="curtain"):
    """A 13.6 m semi-trailer standing on its landing legs: rear at +x (the end backed to a dock), nose at -x."""
    U = K.Unit("trailer", "")
    L, W = 13.6, 2.55
    x0, x1 = -L / 2, L / 2
    floor, top = 1.3, 4.0
    add_box(U, "body", x0, x1, -W / 2, W / 2, floor, top, body_rgb, 2, "trailer")
    add_box(U, "chassis", x0 + 0.6, x1 - 0.3, -0.5, 0.5, 0.95, floor, CHASSIS, 1)
    add_box(U, "bed", x0, x1, -W / 2, W / 2, floor - 0.15, floor, CHASSIS, 1)
    for x in (x1 - 1.0, x1 - 2.3, x1 - 3.6):   # (three axles of twin wheels)
        for y in (-W / 2 + 0.3, W / 2 - 0.3):
            wheel(U, x, y, 0.48, 0.58)
    for y in (-0.85, 0.85):   # (landing legs and their feet)
        add_box(U, "leg", x0 + 2.4, x0 + 2.55, y - 0.08, y + 0.08, 0.1, floor - 0.1, GALV, 0)
        add_box(U, "foot", x0 + 2.25, x0 + 2.7, y - 0.15, y + 0.15, 0.0, 0.1, GALV, 0)
    for y in (-W / 2, W / 2 - 0.05):   # (side guards)
        add_box(U, "guard", x0 + 3.5, x1 - 4.4, y, y + 0.05, 0.55, 0.85, (0.5, 0.5, 0.5), 0)
    add_box(U, "underrun", x1 - 0.2, x1, -W / 2 + 0.2, W / 2 - 0.2, 0.45, 0.65, CHASSIS, 0)
    for y in (-W / 2 + 0.1, W / 2 - 0.35):
        add_box(U, "tail", x1, x1 + 0.03, y, y + 0.25, 0.75, 0.95, TAIL, 0)
    # (the rear doors: frame and the line between them; a curtain-sider's straps and rails, a fridge's unit)
    add_box(U, "rearframe", x1, x1 + 0.04, -W / 2, W / 2, top - 0.25, top, (0.45, 0.46, 0.47), 0)
    add_box(U, "doorline", x1, x1 + 0.03, -0.02, 0.02, floor, top - 0.25, CHASSIS, 0)
    for y in (-W / 2 - 0.02, W / 2 - 0.02):
        if kind == "curtain":
            add_box(U, "toprail", x0, x1, y, y + 0.04, top - 0.25, top, (0.45, 0.46, 0.47), 1)
            add_box(U, "botrail", x0, x1, y, y + 0.04, floor, floor + 0.2, (0.45, 0.46, 0.47), 1)
            n = 9
            for k in range(n):
                x = x0 + 0.5 + (L - 1.0) * k / (n - 1)
                add_box(U, "strap", x - 0.03, x + 0.03, y - 0.005, y + 0.045, floor + 0.2, top - 0.25, CHASSIS, 0)
        else:
            add_box(U, "skirt", x0, x1, y, y + 0.04, floor, floor + 0.35, (0.5, 0.51, 0.51), 0)
    if kind == "fridge":
        add_box(U, "fridge", x0 - 0.55, x0, -0.9, 0.9, floor + 0.9, top - 0.3, (0.6, 0.61, 0.61), 1)
        add_box(U, "grille", x0 - 0.56, x0 - 0.54, -0.7, 0.7, floor + 1.2, top - 0.7, CHASSIS, 0)
    return U


def van(body_rgb):
    """A panel van, its nose at -x."""
    U = K.Unit("van", "")
    W = 2.0
    prof = [(2.75, 0.35), (2.75, 2.5), (-1.2, 2.5), (-2.05, 1.35), (-2.7, 1.05), (-2.78, 0.35)]
    U.add(prism("vanbody", prof, -W / 2, W / 2), "paint", body_rgb, 2)
    # (glass: the windscreen and the cab's side windows)
    U.add(prism("screen", [(-1.25, 2.38), (-2.0, 1.42), (-1.95, 1.4), (-1.2, 2.36)], -W / 2 + 0.1, W / 2 - 0.1), "glass", K.GLASS, 1)
    for y in (-W / 2 - 0.01, W / 2 - 0.01):
        U.add(prism("sidewin", [(-0.55, 2.3), (-1.15, 2.3), (-1.75, 1.45), (-0.55, 1.45)], y, y + 0.02), "glass", K.GLASS, 1)
        add_box(U, "mirror", -1.8, -1.7, y - 0.2 if y < 0 else y, (y + 0.22) if y > 0 else y, 1.5, 1.8, CHASSIS, 0)
        add_box(U, "rub", -2.6, 2.7, y - 0.01, y + 0.03, 0.55, 0.65, CHASSIS, 0)
        add_box(U, "doorcut", 0.1, 0.13, y - 0.005, y + 0.025, 0.6, 2.3, CHASSIS, 0)
    for x in (-1.75, 1.85):
        for y in (-W / 2 + 0.18, W / 2 - 0.18):
            wheel(U, x, y, 0.36, 0.28)
    add_box(U, "bumper", -2.85, -2.6, -W / 2 + 0.05, W / 2 - 0.05, 0.3, 0.6, CHASSIS, 1)
    add_box(U, "rbumper", 2.7, 2.85, -W / 2 + 0.05, W / 2 - 0.05, 0.3, 0.55, CHASSIS, 1)
    for y in (-0.8, 0.55):
        add_box(U, "lamp", -2.79, -2.72, y, y + 0.25, 0.85, 1.0, K.LAMP, 0)
        add_box(U, "tail", 2.75, 2.78, y, y + 0.25, 1.3, 1.8, TAIL, 0)
    add_box(U, "reardoor", 2.75, 2.77, -0.01, 0.01, 0.6, 2.35, CHASSIS, 0)
    return U


# --- yard things -------------------------------------------------------------------------------------------

def skip(rgb):
    """A builders' skip, sloping ends, a little rubble in it."""
    U = K.Unit("skip", "")
    b, t = (1.3, 0.75), (1.85, 0.9)
    ring = lambda hx, hy, z: [(-hx, -hy, z), (hx, -hy, z), (hx, hy, z), (-hx, hy, z)]
    U.add(K.solid("skip", [ring(*b, 0.12), ring(*t, 1.2)]), "paint", rgb, 2)
    U.add(K.solid("rubble", [ring(1.7, 0.8, 1.15), ring(0.9, 0.4, 1.45)]), "concrete", (0.25, 0.22, 0.19), 1)
    add_box(U, "rimF", -1.9, 1.9, -0.95, -0.85, 1.1, 1.25, rgb, 0)
    add_box(U, "rimB", -1.9, 1.9, 0.85, 0.95, 1.1, 1.25, rgb, 0)
    for x in (-0.7, 0.7):
        add_box(U, "skid", x - 0.08, x + 0.08, -0.7, 0.7, 0.0, 0.14, CHASSIS, 0)
    for x in (-1.5, 1.5):
        for y in (-0.93, 0.83):
            add_box(U, "lug", x - 0.08, x + 0.08, y, y + 0.1, 0.8, 1.0, CHASSIS, 0)
    return U


def pallets():
    """Three stacks of wooden pallets and one lying by them."""
    U = K.Unit("pallets", "")
    h = 0.144
    for x, y, n in ((-1.3, 0.0, 9), (0.0, -0.1, 5), (1.3, 0.05, 12), (2.3, -0.6, 1)):
        add_box(U, "stack", x - 0.6, x + 0.6, y - 0.5, y + 0.5, 0.0, n * h, WOOD, 2)
        for k in range(n):   # (the dark gap of each pallet's blocks, along its front and back)
            z = k * h + 0.03
            add_box(U, "gap", x - 0.6, x + 0.6, y - 0.505, y - 0.495, z, z + 0.07, WOOD_DARK, 0)
    return U


def container(rgb):
    """A 20 ft shipping container, ribbed sides, doors at +x."""
    U = K.Unit("container", "")
    L, W, H = 6.06, 2.44, 2.59
    add_box(U, "box", -L / 2, L / 2, -W / 2, W / 2, 0.0, H, rgb, 2)
    dark = tuple(c * 0.6 for c in rgb)
    for y in (-W / 2 - 0.03, W / 2):
        n = 12
        for k in range(n):
            x = -L / 2 + 0.3 + (L - 0.6) * (k + 0.5) / n
            add_box(U, "rib", x - 0.1, x + 0.1, y, y + 0.03, 0.15, H - 0.15, dark, 0)
        add_box(U, "toprail", -L / 2, L / 2, y, y + 0.03, H - 0.15, H, dark, 1)
        add_box(U, "botrail", -L / 2, L / 2, y, y + 0.03, 0.0, 0.15, dark, 1)
    for y in (-0.7, -0.35, 0.35, 0.7):   # (the door bars)
        add_box(U, "bar", L / 2, L / 2 + 0.05, y - 0.03, y + 0.03, 0.15, H - 0.15, GALV, 0)
    add_box(U, "doorgap", L / 2, L / 2 + 0.02, -0.02, 0.02, 0.15, H - 0.15, CHASSIS, 0)
    for x in (-L / 2, L / 2 - 0.18):
        for y in (-W / 2, W / 2 - 0.18):
            for z in (0.0, H - 0.12):
                add_box(U, "casting", x, x + 0.18, y, y + 0.18, z, z + 0.12, (0.1, 0.1, 0.1), 0)
    return U


def gas_cage():
    """A galvanised mesh cage of gas bottles, its sloping lid, a warning plate."""
    U = K.Unit("gascage", "")
    L, D, H = 2.4, 1.0, 2.0
    for x in (-L / 2, L / 2 - 0.05):
        for y in (-D / 2, D / 2 - 0.05):
            add_box(U, "post", x, x + 0.05, y, y + 0.05, 0.0, H, GALV, 2)
    U.add(prism("lid", [(-L / 2 - 0.05, H), (L / 2 + 0.05, H), (L / 2 + 0.05, H + 0.05), (-L / 2 - 0.05, H + 0.05)],
                -D / 2 - 0.1, D / 2 + 0.1), "metal", GALV, 2)
    for z in (0.1, H - 0.05):
        for y in (-D / 2, D / 2 - 0.04):
            add_box(U, "rail", -L / 2, L / 2, y, y + 0.04, z - 0.03, z + 0.03, GALV, 1)
    for k in range(1, 12):   # (the mesh, as bars on its front and ends)
        x = -L / 2 + L * k / 12
        add_box(U, "mesh", x - 0.01, x + 0.01, -D / 2 - 0.01, -D / 2 + 0.01, 0.1, H - 0.05, GALV, 0)
    for k in range(1, 8):
        z = H * k / 8
        add_box(U, "meshh", -L / 2, L / 2, -D / 2 - 0.01, -D / 2 + 0.01, z - 0.01, z + 0.01, GALV, 0)
    add_box(U, "back", -L / 2, L / 2, D / 2 - 0.03, D / 2, 0.1, H - 0.05, (0.2, 0.21, 0.21), 1)
    for k, x in enumerate((-0.85, -0.5, -0.15, 0.2, 0.55, 0.9)):
        rgb = (0.45, 0.12, 0.05) if k % 3 else (0.05, 0.2, 0.08)
        U.add(cyl("bottle", (x, 0.1, 0.7), 0.15, 1.4, "z", 8), "paint", rgb, 1)
    add_box(U, "warning", -0.2, 0.2, -D / 2 - 0.03, -D / 2 - 0.01, 1.3, 1.65, K.YELLOW, 0)
    return U


def palisade(rgb):
    """A 2.75 m run of steel palisade fencing: two posts, two rails, pales with pointed tops."""
    U = K.Unit("palisade", "")
    L, H = 2.75, 2.4
    for x in (-L / 2, L / 2 - 0.1):
        add_box(U, "post", x, x + 0.1, 0.02, 0.12, -0.3, H - 0.05, rgb, 2)
    for z in (0.35, H - 0.45):
        add_box(U, "rail", -L / 2, L / 2, 0.0, 0.05, z, z + 0.05, rgb, 2)
    n = 11
    for k in range(n):
        x = -L / 2 + L * (k + 0.5) / n
        w = 0.07
        U.add(prism("pale", [(x - w / 2, -0.05), (x + w / 2, -0.05), (x + w / 2, H - 0.12), (x, H), (x - w / 2, H - 0.12)],
                    -0.02, 0.0), "pale", rgb, 1)
    return U


def gate(rgb):
    """A 6 m sliding palisade gate on its track, a post each end."""
    U = K.Unit("gate", "")
    L, H = 6.0, 2.4
    add_box(U, "frameb", -L / 2, L / 2, 0.0, 0.08, 0.15, 0.3, rgb, 2)
    add_box(U, "framet", -L / 2, L / 2, 0.0, 0.08, H - 0.5, H - 0.4, rgb, 2)
    add_box(U, "diag", -0.06, 0.06, 0.0, 0.08, 0.3, H - 0.5, rgb, 2)
    for x in (-L / 2 - 0.15, L / 2):
        add_box(U, "gpost", x, x + 0.15, -0.05, 0.15, -0.3, H + 0.1, rgb, 2)
    n = 22
    for k in range(n):
        x = -L / 2 + L * (k + 0.5) / n
        w = 0.07
        U.add(prism("pale", [(x - w / 2, 0.15), (x + w / 2, 0.15), (x + w / 2, H - 0.12), (x, H), (x - w / 2, H - 0.12)],
                    -0.02, 0.0), "pale", rgb, 1)
    for x in (-L / 2 + 0.6, L / 2 - 0.6):
        U.add(cyl("roller", (x, 0.04, 0.08), 0.08, 0.06), "metal", GALV, 0)
    add_box(U, "track", -L / 2 - 1.0, L / 2, -0.05, 0.13, -0.05, 0.02, GALV, 0)
    return U


def estate_board():
    """The estate's name board at its entrance: two posts, a navy panel, its name and a list of its units."""
    U = K.Unit("board", "")
    L = 3.2
    for x in (-L / 2 + 0.2, L / 2 - 0.35):
        add_box(U, "bpost", x, x + 0.15, 0.0, 0.15, -0.3, 3.2, (0.6, 0.61, 0.61), 2)
    add_box(U, "panel", -L / 2, L / 2, -0.06, 0.0, 1.0, 3.0, NAVY, 2)
    add_box(U, "head", -L / 2, L / 2, -0.07, -0.06, 2.78, 3.0, K.SIGN_WHITE, 1)
    for k in range(5):   # (its units' names, as white strips)
        z = 1.2 + k * 0.26
        add_box(U, "unitline", -L / 2 + 0.25, -L / 2 + 0.25 + (2.2 if k % 2 else 1.7), -0.07, -0.06, z, z + 0.09, K.SIGN_WHITE, 0)
    K.SIGNS.append((Vector((0.0, -0.07, 2.42)), Vector((0.0, -1.0, 0.0)), L - 0.4, 0.52, "estate", U))
    return U


def trolley_shelter():
    """A supermarket car park's trolley shelter: posts, a sloping roof, glazed back and ends, trolleys in it."""
    U = K.Unit("trolleys", "")
    L, D, H = 5.0, 2.2, 2.3
    for x in (-L / 2, L / 2 - 0.08):
        for y in (-D / 2, D / 2 - 0.08):
            add_box(U, "tpost", x, x + 0.08, y, y + 0.08, 0.0, H + (0.15 if y > 0 else 0.0), (0.12, 0.13, 0.14), 2)
    U.add(_roof(L, D, H), "metal", (0.12, 0.13, 0.14), 2)
    add_box(U, "tback", -L / 2, L / 2, D / 2 - 0.05, D / 2 - 0.03, 0.3, H, (0.05, 0.07, 0.08), 1, "glass")
    for x in (-L / 2 + 0.02, L / 2 - 0.04):
        add_box(U, "tend", x, x + 0.02, -D / 2 + 0.3, D / 2 - 0.05, 0.3, H, (0.05, 0.07, 0.08), 1, "glass")
    for y in (-0.45, 0.45):   # (two lines of nested trolleys)
        add_box(U, "trolley", -L / 2 + 0.4, L / 2 - 0.6, y - 0.28, y + 0.28, 0.35, 1.0, CHROME, 1)
        add_box(U, "handle", L / 2 - 0.6, L / 2 - 0.5, y - 0.3, y + 0.3, 0.95, 1.05, (0.3, 0.05, 0.03), 0)
        add_box(U, "base", -L / 2 + 0.4, L / 2 - 0.6, y - 0.25, y + 0.25, 0.1, 0.2, CHROME, 0)
    return U


def _roof(L, D, H):
    ring = lambda z0: [(-L / 2 - 0.1, -D / 2 - 0.2, z0), (L / 2 + 0.1, -D / 2 - 0.2, z0), (L / 2 + 0.1, D / 2 + 0.1, z0 + 0.15), (-L / 2 - 0.1, D / 2 + 0.1, z0 + 0.15)]
    return K.solid("troof", [ring(H), ring(H + 0.06)])


def yard_lamp():
    """An 8 m yard lighting column with a flat floodlight head."""
    U = K.Unit("lamp", "")
    U.add(K.solid("column", [[(0.11 * math.cos(a), 0.11 * math.sin(a), -0.3) for a in [k * math.pi / 3 for k in range(6)]],
                             [(0.06 * math.cos(a), 0.06 * math.sin(a), 8.0) for a in [k * math.pi / 3 for k in range(6)]]]), "metal", GALV, 2)
    add_box(U, "arm", -0.05, 0.05, -0.9, 0.0, 7.85, 7.95, GALV, 1)
    add_box(U, "head", -0.3, 0.3, -1.5, -0.8, 7.75, 7.95, (0.12, 0.13, 0.14), 1)
    add_box(U, "lens", -0.27, 0.27, -1.47, -0.83, 7.74, 7.75, K.LAMP, 0)
    add_box(U, "door", -0.05, 0.05, -0.13, -0.1, 0.6, 1.0, (0.2, 0.2, 0.2), 0)
    return U


def bins():
    """Two big four-wheeled bins with their lids."""
    U = K.Unit("bins", "")
    for x, rgb in ((-0.7, (0.04, 0.06, 0.05)), (0.7, (0.12, 0.12, 0.13))):
        add_box(U, "bin", x - 0.62, x + 0.62, -0.5, 0.5, 0.15, 1.25, rgb, 2)
        U.add(prism("lid", [(x - 0.66, 1.25), (x + 0.66, 1.25), (x + 0.66, 1.4), (x - 0.66, 1.3)], -0.55, 0.55), "paint", rgb, 1)
        for xx in (x - 0.5, x + 0.5):
            for y in (-0.38, 0.38):
                U.add(cyl("castor", (xx, y, 0.08), 0.08, 0.05), "rubber", TYRE, 0)
    return U


# Every prop: its name in the kit, what it is, and how to make it.
PROPS = [
    ("trailer_white", "Curtain-sided trailer, white", lambda: trailer((0.62, 0.63, 0.62))),
    ("trailer_navy", "Curtain-sided trailer, navy", lambda: trailer((0.02, 0.035, 0.09))),
    ("trailer_grey", "Curtain-sided trailer, grey", lambda: trailer((0.13, 0.135, 0.14))),
    ("trailer_fridge", "Fridge box trailer", lambda: trailer((0.68, 0.69, 0.68), "fridge")),
    ("van_white", "Panel van, white", lambda: van((0.62, 0.63, 0.62))),
    ("van_silver", "Panel van, silver", lambda: van((0.36, 0.37, 0.38))),
    ("van_blue", "Panel van, dark blue", lambda: van((0.02, 0.05, 0.14))),
    ("skip_yellow", "Builders' skip, yellow", lambda: skip((0.7, 0.42, 0.0))),
    ("skip_green", "Builders' skip, green", lambda: skip((0.04, 0.16, 0.07))),
    ("pallets", "Stacks of pallets", pallets),
    ("container_red", "20 ft container, rust red", lambda: container((0.22, 0.05, 0.03))),
    ("container_blue", "20 ft container, blue", lambda: container((0.02, 0.07, 0.2))),
    ("container_green", "20 ft container, green", lambda: container((0.04, 0.13, 0.07))),
    ("gascage", "Gas bottle cage", gas_cage),
    ("palisade_green", "Palisade fence, green, 2.75 m", lambda: palisade(PALISADE_GREEN)),
    ("palisade_grey", "Palisade fence, galvanised, 2.75 m", lambda: palisade(GALV)),
    ("gate_green", "Sliding gate, green, 6 m", lambda: gate(PALISADE_GREEN)),
    ("gate_grey", "Sliding gate, galvanised, 6 m", lambda: gate(GALV)),
    ("estate_board", "Estate name board", estate_board),
    ("trolley_shelter", "Trolley shelter", trolley_shelter),
    ("yard_lamp", "Yard lighting column", yard_lamp),
    ("bins", "Pair of big bins", bins),
]


def build_props():
    """Every prop, each set out in a row for the sheet: a list of (name, label, Unit, LOD triangle counts)."""
    K.clear()
    out = []
    for name, label, make in PROPS:
        U = make()
        U.name, U.label = name, label
        U.tris = K.triangle_counts(U)
        out.append(U)
        print("%-16s LOD0 %5d  LOD1 %5d" % (name, U.tris[0], U.tris[1]))
    return out


def main():
    args = sys.argv[1:]
    units = build_props()
    if "--render" in args:
        import blender_warehouse_kit_render as R
        R.render_props(units, args[args.index("--render") + 1])


if __name__ == "__main__":
    main()
