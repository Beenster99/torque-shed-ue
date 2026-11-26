"""Ride UK's farmyards (the everyday kits plan, stage 4): a kit of UK farm buildings, modelled by script in Blender,
our own designs from general knowledge of British farms. No model, maker, name or logo is copied.

A building is a rectangle L long (x) by W deep (y), its front at y = 0 facing -y, sized to whatever outline the map
gives, exactly as the warehouse kit's units are (Tools/blender_warehouse_kit.py, whose shape helpers this uses), so the
game lays both with the same code (World/WorldRideWarehouses.cpp's BuildWarehouse). Walls are cut into bays (a farm
portal frame's 4.8 or 6 m; a stone barn's 4 m) and each bay filled from a short pattern:

  .  plain bay: the wall's plinth and its cladding to the eaves
  A  open bay: no wall, the portal frame's stanchions only (a Dutch barn, an implement shed's front)
  B  open above a precast concrete panel wall (a grain or straw store's push wall)
  F  feed barrier: a block plinth, the tombstone feed rail, open, Yorkshire boarding at the top (a cattle shed)
  Y  Yorkshire boarding over a block plinth (a livestock shed's ventilated wall)
  G  a pair of big sliding sheeted doors on a top track
  R  a roller door        P  a personnel door
  C  a cart arch (stone and brick barns): a dressed stone or brick arch, dark boarded doors set back
  K  tall double boarded doors in a brick or boarded barn
  V  slit vents in rows (stone and brick barns)       D  a small boarded byre door
  O  an owl hole and a pitching door high in a gable (old barns)
  *  repeats the letter before it to fill the bays left over

Walls: "sheet" (box-profile steel, the usual juniper green or grey), "boarding" (Yorkshire boarding, open joints),
"weatherboard" (black horizontal boards, the eastern counties), "stone", "brick", "tin" (corrugated iron, rusty).
Roofs: "pitch" (fibre cement or steel sheet with rooflights; or slate, stone slates or clay tiles on old barns),
"curve" (a Dutch barn's) or "mono" (a lean-to). Parts are tagged with the furthest LOD they stay in (0 near detail,
1 middle, 2 the far shell), as the warehouse kit's.

    python Tools/blender_farm_kit.py --render DIR      # the review sheet of the designs and the yard pieces in DIR
"""
import math
import os
import sys

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_warehouse_kit as K  # noqa: E402
from blender_street_kit import box, lathe, rod  # noqa: E402

FOOT = K.FOOT
Wall, Unit, strip, solid = K.Wall, K.Unit, K.strip, K.solid

# Colours (linear).
JUNIPER = (0.035, 0.075, 0.05)        # box-profile green, the commonest farm sheeting
SLATE_BLUE = (0.07, 0.085, 0.1)
FIBRE_CEMENT = (0.3, 0.31, 0.3)
FIBRE_AGED = (0.22, 0.23, 0.2)
ANTHRACITE = (0.04, 0.042, 0.045)
BOARDS = (0.17, 0.11, 0.065)          # Yorkshire boarding, weathered spruce
BOARDS_NEW = (0.3, 0.2, 0.11)
BLACK_BOARD = (0.018, 0.018, 0.02)
BLOCK = (0.34, 0.335, 0.31)
PANEL = (0.4, 0.39, 0.36)
STEEL_GALV = (0.3, 0.31, 0.31)
STEEL_GREEN = (0.03, 0.07, 0.04)
STEEL_BLUE = (0.02, 0.04, 0.09)
RUST = (0.16, 0.055, 0.02)
TIN_GREY = (0.22, 0.22, 0.21)
DARK = (0.012, 0.011, 0.01)
DOOR_TIMBER = (0.075, 0.05, 0.03)
DOOR_GREEN = (0.03, 0.06, 0.035)
DOOR_RED = (0.14, 0.03, 0.02)
LIMESTONE = (0.42, 0.41, 0.37)        # (the preview's: the game colours stone by the place's own)
SANDSTONE = (0.4, 0.3, 0.2)
GRITSTONE = (0.24, 0.21, 0.17)
BRICK_RED = (0.27, 0.09, 0.05)
BRICK_SOFT = (0.33, 0.14, 0.08)
SLATE = (0.06, 0.065, 0.075)
STONE_SLATE = (0.2, 0.19, 0.16)
CLAY_TILE = (0.3, 0.1, 0.05)
PANTILE = (0.33, 0.12, 0.055)
STRAW = (0.5, 0.38, 0.17)
HAY = (0.38, 0.33, 0.14)


def roof_z(spec, y):
    """The roof's height y into the building (the warehouse kit's, and a lean-to's single slope)."""
    if spec["roof"] == "mono":
        return spec["H"] + math.tan(math.radians(spec.get("pitch", 8.0))) * (spec["W"] - y)
    return K.roof_z(spec, y)


def roof_samples(spec, y0, y1, n):
    if spec["roof"] == "mono":
        return [y0 + (y1 - y0) * i / max(1, n) for i in range(max(1, n) + 1)]
    return K.roof_samples(spec, y0, y1, n)


# --- the walls -------------------------------------------------------------------------------------------

def plinth(spec, letter):
    """(top, surface, colour) of the bay's lower wall, or None."""
    if letter in "A":
        return None
    if letter == "B":
        return (spec.get("panel_h", 2.4), "concrete", PANEL)
    if spec.get("wall") in ("stone", "brick"):
        return None
    if letter in "FY":
        return (1.2 if letter == "F" else spec.get("plinth_h", 2.0), "block", BLOCK)
    k = spec.get("plinth", "block")
    if k == "none":
        return None
    return (spec.get("plinth_h", 1.8), "concrete" if k == "panel" else "block", PANEL if k == "panel" else BLOCK)


def wall_surface(spec):
    w = spec.get("wall", "sheet")
    return {"sheet": "clad", "boarding": "boarding", "weatherboard": "weatherboard", "stone": "stone", "brick": "brick", "tin": "tin"}[w]


def wall_rgb(spec):
    return spec.get("wall_rgb", {"sheet": JUNIPER, "boarding": BOARDS, "weatherboard": BLACK_BOARD, "stone": LIMESTONE,
                                 "brick": BRICK_RED, "tin": RUST}[spec.get("wall", "sheet")])


def thick(spec):
    return 0.5 if spec.get("wall") == "stone" else 0.3 if spec.get("wall") == "brick" else 0.12


def sheet(U, w, a, b, z0, z1, spec, lod=2, letter="."):
    """The wall's cladding (or masonry) between z0 and z1 on the bay a..b."""
    if z1 <= z0 + 0.02:
        return
    surf, rgb = wall_surface(spec), wall_rgb(spec)
    if letter in "FY" and spec.get("wall") not in ("stone", "brick"):
        surf, rgb = "boarding", spec.get("board_rgb", BOARDS)
    w.box(U, "wall", a, b, z0, z1, -thick(spec), 0.0, surf, rgb, lod)
    if surf == "boarding" and lod >= 1:
        # (the open joints: dark slots behind the boards, read from close)
        n = max(2, int((b - a) / 0.17))
        for k in range(1, n, 3):
            s = a + (b - a) * k / n
            w.box(U, "joint", s - 0.012, s + 0.012, z0 + 0.05, z1 - 0.05, 0.0, 0.006, "plain", DARK, 0)


def gable_outline(spec, w, a, b, z0):
    top = w.top
    k = 4 if spec["roof"] == "curve" else 1
    ss = [a + (b - a) * j / k for j in range(k + 1)]
    if spec["roof"] == "pitch":
        mid = spec["W"] / 2
        if a < mid < b:
            ss.append(mid)
        ss = sorted(set(ss))
    return [(a, z0), (b, z0)] + [(s, top(s)) for s in reversed(ss)]


def stanchion(U, w, s, top, spec, lod=1):
    col = spec.get("frame_rgb", STEEL_GALV)
    w.box(U, "stanchion", s - 0.13, s + 0.13, FOOT, top, -0.35, -0.05, "metal", col, lod)
    w.box(U, "padstone", s - 0.3, s + 0.3, FOOT, 0.15, -0.5, 0.1, "concrete", PANEL, 0)


def eaves_beam(U, w, a, b, top, spec):
    w.box(U, "eaves_beam", a, b, top - 0.45, top - 0.05, -0.3, -0.05, "metal", spec.get("frame_rgb", STEEL_GALV), 1)


def boarded_doors(U, w, a, b, z0, z1, rgb, out=0.0, lod=1):
    w.box(U, "doors", a, b, z0, z1, out - 0.05, out, "timber", rgb, 2 if lod >= 1 else lod)
    mid = (a + b) / 2
    w.box(U, "meet", mid - 0.03, mid + 0.03, z0, z1, out, out + 0.02, "plain", DARK, 0)
    for z in (z0 + 0.4, (z0 + z1) / 2, z1 - 0.4):
        w.box(U, "ledge", a + 0.1, b - 0.1, z - 0.06, z + 0.06, out, out + 0.03, "timber", tuple(c * 0.8 for c in rgb), 0)


def arch(U, w, a, b, z0, rise, spring, spec, name="arch"):
    """A segmental arch opening a..b springing at z0 + spring, its ring of voussoirs proud of the wall."""
    surf = "stone" if spec.get("wall") == "stone" else "brick"
    col = spec.get("dress_rgb", tuple(min(1.0, c * 1.12) for c in wall_rgb(spec)))
    s0, s1 = a, b
    r = (s1 - s0) / 2
    zc = z0 + spring
    n = 7
    for k in range(n):
        t0, t1 = math.pi * k / n, math.pi * (k + 1) / n
        p = [(s0 + r - math.cos(t0) * r, zc + math.sin(t0) * rise), (s0 + r - math.cos(t1) * r, zc + math.sin(t1) * rise),
             (s0 + r - math.cos(t1) * (r + 0.45), zc + math.sin(t1) * (rise + 0.45)), (s0 + r - math.cos(t0) * (r + 0.45), zc + math.sin(t0) * (rise + 0.45))]
        w.face(U, name, p, 0.0, 0.05, surf, col, 1)
    # (the opening: dark doors set back, filling the arch's square part and its head)
    boarded_doors(U, w, s0 + 0.05, s1 - 0.05, 0.0, zc, spec.get("door_rgb", DOOR_TIMBER), out=-thick(spec) + 0.1, lod=1)
    head = [(s0 + r - math.cos(math.pi * k / 8) * r, zc + math.sin(math.pi * k / 8) * rise) for k in range(9)]
    w.face(U, "archhead", [(s0, zc)] + head[1:-1] + [(s1, zc)], -thick(spec) + 0.05, -thick(spec) + 0.1, "timber", spec.get("door_rgb", DOOR_TIMBER), 1)


def vents(U, w, a, b, H, spec):
    """Rows of narrow slit vents, dark recesses with dressed edges."""
    rows = [1.6, 3.0] if H > 4.5 else [1.8]
    for z in rows:
        for s in (a + (b - a) * 0.3, a + (b - a) * 0.7):
            w.box(U, "slit", s - 0.07, s + 0.07, z, z + 0.75, -0.03, 0.002, "plain", DARK, 0)


def bay(U, spec, w, t, a, b, i, n, is_long, top_z, every_pipe=False):
    """One bay of wall: its plinth, its cladding and what the letter stands for."""
    H = spec["H"]
    s = (a + b) / 2
    bw = b - a
    gable = callable(top_z)
    p = plinth(spec, t)
    masonry = spec.get("wall") in ("stone", "brick")
    z0 = p[0] if p else FOOT
    if p:
        pt = max(0.15, thick(spec))
        pa, pb = (max(a, pt) if i == 0 else a, min(b, w.length - pt) if i == n - 1 else b) if gable else (a, b)
        w.box(U, "plinth", pa, pb, FOOT, p[0], -pt, 0.0, p[1], p[2], 2)
    # (the cladding: to the eaves on a long wall; on a gable its outline up to the roof)
    open_to = None
    if t == "A":
        open_to = FOOT
    elif t == "B":
        open_to = p[0]
    elif t == "F":
        open_to = p[0]
    if open_to is None:
        if gable:
            # (a gable stops at the long walls' inner faces, so no faces lie on top of each other at the corners)
            ga, gb = max(a, thick(spec) if i == 0 else a), min(b, w.length - thick(spec) if i == n - 1 else b)
            w.face(U, "gable", gable_outline(spec, w, ga, gb, z0), -thick(spec), 0.0, wall_surface(spec) if t not in "FY" or masonry else "boarding",
                   wall_rgb(spec) if t not in "FY" or masonry else spec.get("board_rgb", BOARDS), 2)
        elif t == "C" and masonry:
            # (the wall round a cart arch: either side, over the head, and the spandrels in vertical slivers)
            wdt = min(3.6, bw * 0.8)
            s0, s1 = s - wdt / 2, s + wdt / 2
            zc, rise = min(2.9, H - 1.4), 0.9
            sheet(U, w, a, s0, z0, top_z, spec, 2, t)
            sheet(U, w, s1, b, z0, top_z, spec, 2, t)
            sheet(U, w, s0, s1, zc + rise, top_z, spec, 2, t)
            r = wdt / 2
            m = 8
            cv = [(s0 + r - math.cos(math.pi * k / m) * r, zc + math.sin(math.pi * k / m) * rise) for k in range(m + 1)]
            for k in range(m):
                (x0, q0), (x1, q1) = cv[k], cv[k + 1]
                if zc + rise - min(q0, q1) < 0.01:
                    continue
                w.face(U, "spandrel", [(x0, q0), (x1, q1), (x1, zc + rise), (x0, zc + rise)], -thick(spec), 0.0,
                       wall_surface(spec), wall_rgb(spec), 2)
            w.box(U, "reveal", s0, s1, 0.0, 0.02, -thick(spec), 0.0, "concrete", PANEL, 1)
        else:
            sheet(U, w, a, b, z0, top_z, spec, 2, t)
    else:
        # (an open bay: the stanchion at its end, the eaves beam over it; a feed barrier's boarding high up)
        top = top_z(s) if gable else top_z
        if t == "F":
            sheet(U, w, a, b, H - 1.2, top_z if not gable else H, spec, 2, "Y")
            rail = spec.get("frame_rgb", STEEL_GALV)
            w.box(U, "feedrail", a + 0.1, b - 0.1, 1.15, 1.25, 0.05, 0.12, "metal", rail, 1)
            w.box(U, "feedrail", a + 0.1, b - 0.1, 1.95, 2.05, 0.05, 0.12, "metal", rail, 1)
            for k in range(int(bw / 0.6)):
                x = a + 0.3 + k * 0.6
                w.box(U, "tombstone", x - 0.03, x + 0.03, 1.2, 2.0, 0.07, 0.11, "metal", rail, 0)
            w.box(U, "trough", a, b, FOOT, 0.55, 0.0, 0.9, "concrete", PANEL, 1)
        else:
            if not gable:
                eaves_beam(U, w, a, b, H, spec)
            if gable and spec.get("gable_open_sheet"):
                # (an open gable still carries its sheeting above the eaves)
                pass
        stanchion(U, w, b if i < n - 1 or not gable else b - 0.15, top if not gable else top_z(b), spec)
        if i == 0:
            stanchion(U, w, a + 0.15, top if not gable else top_z(a), spec)
        if t == "B":
            w.box(U, "panelcap", a, b, p[0], p[0] + 0.08, -0.25, 0.02, "concrete", PANEL, 0)
        if spec.get("fill") and t in "AB":
            fill(U, w, a, b, spec)
    # (the letter's own things)
    if t == "G":
        d = min(bw * 0.9, 5.5)
        h = min(H - 0.6, 4.8)
        col = spec.get("door_rgb", wall_rgb(spec) if spec.get("wall") == "sheet" else STEEL_GREEN)
        w.box(U, "slider", s - d / 2, s, 0.0, h, 0.0, 0.08, "clad_v", col, 2)
        w.box(U, "slider", s, s + d / 2, 0.0, h, 0.08, 0.16, "clad_v", col, 2)
        w.box(U, "track", s - d / 2 - 0.4, s + d / 2 + 0.4, h + 0.05, h + 0.25, 0.0, 0.2, "metal", STEEL_GALV, 1)
    elif t == "R":
        K.roller_door(U, w, s, min(4.5, bw * 0.75), min(4.5, H - 1.0), 0.0)
    elif t == "P":
        w.box(U, "pdoor", s - 0.45, s + 0.45, 0.0, 2.05, 0.0, 0.05, "paint", spec.get("door_rgb", STEEL_GREEN), 1)
    elif t == "C":
        wdt = min(3.6, bw * 0.8)
        arch(U, w, s - wdt / 2, s + wdt / 2, 0.0, 0.9, min(2.9, H - 1.4), spec)
    elif t == "K":
        wdt = min(4.2, bw * 0.85)
        h = min(H - 0.3, 4.6)
        boarded_doors(U, w, s - wdt / 2, s + wdt / 2, 0.0, h, spec.get("door_rgb", BLACK_BOARD if spec.get("wall") == "weatherboard" else DOOR_TIMBER), out=0.02, lod=2)
        if masonry:
            w.box(U, "lintel", s - wdt / 2 - 0.2, s + wdt / 2 + 0.2, h, h + 0.3, 0.0, 0.06, wall_surface(spec), spec.get("dress_rgb", wall_rgb(spec)), 1)
    elif t == "V":
        vents(U, w, a, b, H, spec)
    elif t == "D":
        boarded_doors(U, w, s - 0.55, s + 0.55, 0.0, 1.95, spec.get("door_rgb", DOOR_TIMBER), out=0.02, lod=1)
        if masonry:
            w.box(U, "lintel", s - 0.75, s + 0.75, 1.95, 2.2, 0.0, 0.05, wall_surface(spec), spec.get("dress_rgb", wall_rgb(spec)), 0)
    elif t == "O":
        top = top_z(s) if gable else top_z
        z = min(H + 0.4, top - 1.2) if gable else H - 1.6
        w.box(U, "pitchdoor", s - 0.55, s + 0.55, z - 1.2, z, 0.0, 0.04, "timber", spec.get("door_rgb", DOOR_TIMBER), 1)
        if gable and top - z > 1.5:
            w.box(U, "owlhole", s - 0.12, s + 0.12, z + 0.6, z + 0.95, -0.02, 0.002, "plain", DARK, 0)
    # (gutters' downpipes on sheeted sheds now and then)
    if is_long and t == "." and spec.get("wall") in ("sheet", "boarding") and (every_pipe or i % 3 == 1):
        w.box(U, "downpipe", b - 0.07, b + 0.07, 0.0, H - 0.1, 0.0, 0.12, "metal", spec.get("trim", STEEL_GALV), 0)


def fill(U, w, a, b, spec):
    """What an open bay shows: a stack of big square straw bales, or round ones, or the dark inside."""
    what = spec["fill"]
    depth = max(1.0, min(spec["W"] * 0.55 - 1.0, 8.0))   # (shallow enough for the narrowest outline it is stretched to)
    if what in ("straw", "hay"):
        col = STRAW if what == "straw" else HAY
        rows = int(min(spec["H"] - 0.4, 5.6) / 0.9)
        for r in range(rows):
            w.box(U, "bales", a + 0.25, b - 0.25, r * 0.9, r * 0.9 + 0.88, -0.6 - depth, -0.6, "straw", tuple(c * (0.92 + 0.08 * (r % 2)) for c in col), 2 if r == 0 else 1)
    else:
        w.box(U, "inside", a + 0.15, b - 0.15, 0.0, spec["H"] - 0.3, -0.6 - depth, -0.6, "plain", (0.03, 0.028, 0.025), 2)


# --- roofs -------------------------------------------------------------------------------------------------

def roof_surface(spec):
    return "tile" if spec.get("roof_kind") in ("slate", "stone_slate", "clay", "pantile") else "roof"


def roof_rgb(spec):
    return spec.get("roof_rgb", {"slate": SLATE, "stone_slate": STONE_SLATE, "clay": CLAY_TILE, "pantile": PANTILE,
                                 "fibre": FIBRE_CEMENT, "sheet": JUNIPER, "tin": RUST}.get(spec.get("roof_kind", "fibre"), FIBRE_CEMENT))


def over(spec):
    return 0.15 if spec.get("wall") in ("stone", "brick") else 0.35


def build_roof(U, spec, x0, x1, lit=False, end=False):
    """The roof right across from x0 to x1 (along the building): its sheet or slates, gutters, rooflights."""
    W = spec["W"]
    o = over(spec)
    ys = roof_samples(spec, -o, W + o, 24 if spec["roof"] == "curve" else 2)
    top = [(y, roof_z(spec, y)) for y in ys]
    th = 0.12 if roof_surface(spec) == "roof" else 0.2
    bot = [(y, z - th) for y, z in top]
    U.add(strip("roof", top, bot, x0, x1), roof_surface(spec), roof_rgb(spec), 2)
    if spec["roof"] == "pitch" and roof_surface(spec) == "tile" and not end:
        # (a ridge of half-round tiles or stone)
        rz = roof_z(spec, W / 2)
        U.add(box("ridge", ((x0 + x1) / 2, W / 2, rz + 0.02), (x1 - x0, 0.32, 0.2)), "tile", tuple(c * 0.8 for c in roof_rgb(spec)), 1)
    if spec.get("gutters", True) and spec["roof"] != "curve":
        for y, sgn in ((-o, -1), (W + o, 1)):
            if spec["roof"] == "mono" and sgn < 0:
                continue
            z = roof_z(spec, y)
            U.add(box("gutter", ((x0 + x1) / 2, y + sgn * 0.08, z - 0.15), (x1 - x0, 0.16, 0.16)), "metal", spec.get("trim", STEEL_GALV), 1)
    if lit and not end and spec.get("rooflights", True) and roof_surface(spec) == "roof":
        bw = x1 - x0
        if spec["roof"] == "curve":
            bands = [(W * 0.2, W * 0.4), (W * 0.6, W * 0.8)]
        elif spec["roof"] == "mono":
            bands = [(W * 0.2, W * 0.5)]
        else:
            bands = [(W * 0.12, W * 0.38), (W * 0.62, W * 0.88)]
        for y0, y1 in bands:
            ys2 = roof_samples(spec, y0, y1, 4 if spec["roof"] == "curve" else 1)
            t = [(y, roof_z(spec, y) + 0.04) for y in ys2]
            bt = [(y, z - 0.05) for y, z in t]
            U.add(strip("rooflight", t, bt, x0 + bw * 0.35, x0 + bw * 0.65), "rooflight", (0.42, 0.44, 0.4), 1)
    if spec.get("open_ridge") and spec["roof"] == "pitch" and not end:
        rz = roof_z(spec, W / 2)
        U.add(box("ridgecap", ((x0 + x1) / 2, W / 2, rz + 0.35), (x1 - x0, 0.9, 0.06)), "roof", roof_rgb(spec), 1)
        for y in (W / 2 - 0.4, W / 2 + 0.4):
            U.add(box("upstand", ((x0 + x1) / 2, y, rz + 0.17), (x1 - x0, 0.05, 0.36)), "metal", STEEL_GALV, 0)


# --- a whole building (the review sheet's; the export cuts the same into pieces) --------------------------

def gable_top_fn(spec):
    o = 0.0
    return lambda y: roof_z(spec, min(max(y, o), spec["W"])) - 0.15


def walls_of(spec):
    W, L, H = spec["W"], spec["L"], spec["H"]
    gt = gable_top_fn(spec)
    front = Wall((0, 0), (1, 0), L, H if spec["roof"] != "mono" else roof_z(spec, 0.0) - 0.15)
    back = Wall((L, W), (-1, 0), L, H)
    right = Wall((L, 0), (0, 1), W, lambda s: gt(s))
    left = Wall((0, W), (0, -1), W, lambda s: gt(W - s))
    return front, back, right, left


def build_wall(U, spec, w, pattern, bay_m, is_long):
    n = max(1, round(w.length / bay_m))
    bw = w.length / n
    letters = K.expand(pattern, n)
    for i, t in enumerate(letters):
        bay(U, spec, w, t, i * bw, (i + 1) * bw, i, n, is_long, w.top)


def unit(name, label, spec):
    U = Unit(name, label)
    front, back, right, left = walls_of(spec)
    bay_m = spec.get("bay", 6.0)
    gbay = spec.get("gable_bay", spec["W"] / max(1, round(spec["W"] / 6.0)))
    build_wall(U, spec, front, spec.get("front", ".*"), bay_m, True)
    build_wall(U, spec, back, spec.get("back", ".*"), bay_m, True)
    build_wall(U, spec, right, spec.get("right", ".*"), gbay, False)
    build_wall(U, spec, left, spec.get("left", ".*"), gbay, False)
    o = over(spec) if spec["roof"] != "mono" else 0.2
    n = max(1, round(spec["L"] / bay_m))
    for k in range(n):
        build_roof(U, spec, spec["L"] * k / n, spec["L"] * (k + 1) / n, lit=(k % 2 == 1 and k not in (0, n - 1)))
    build_roof(U, spec, -o, 0.0, end=True)
    build_roof(U, spec, spec["L"], spec["L"] + o, end=True)
    if spec.get("wall") in ("sheet", "boarding", "tin"):
        trim = spec.get("trim", STEEL_GALV)
        for w in (front, back, right, left):
            z0 = plinth(spec, ".")[0] if plinth(spec, ".") else FOOT
            top = w.top if not callable(w.top) else w.top(0.0)
            w.box(U, "corner", -0.02, 0.25, z0, top, 0.0, 0.03, "metal", trim, 1)
    return U


# --- the designs -------------------------------------------------------------------------------------------
# Which buildings each suits ("classes": the game's farm classes; "short" the outline's short side, "area" m2;
# "regions" where it is built, from the place's look: north, dales, midlands, east, south, west, scotland, wales).

DESIGNS = [
    ("FM_portal_store", "Steel portal general store: panels, juniper sheeting, sliding doors in the gable",
     dict(L=36, W=18, H=6.0, roof="pitch", pitch=15, bay=6.0, gable_bay=6.0, wall="sheet", plinth="panel", plinth_h=2.4,
          front=". . G . . .", back=".*", right=". G .", left=". . .", roof_kind="fibre", door_rgb=JUNIPER),
     dict(classes=["farm_shed", "farm_store"], short=[11, 30], area=[150, 2500], weight=1.4)),
    ("FM_grey_store", "Grain or machinery store: grey box-profile, roller doors, rooflights",
     dict(L=30, W=20, H=7.0, roof="pitch", pitch=12, bay=6.0, wall="sheet", wall_rgb=(0.3, 0.31, 0.31), plinth="panel", plinth_h=3.0,
          front=". R . R .", back=".*", right=". P .", left=". . .", roof_kind="fibre", trim=(0.12, 0.12, 0.12)),
     dict(classes=["farm_shed", "farm_store"], short=[12, 32], area=[200, 3000], weight=0.8)),
    ("FM_cattle_shed", "Cattle shed: feed barrier along the front, Yorkshire boarding, open ridge",
     dict(L=30, W=24, H=4.8, roof="pitch", pitch=15, bay=4.8, wall="boarding", plinth="block", plinth_h=2.0,
          front="F*", back="Y*", right="Y G Y", left="Y Y Y Y", roof_kind="fibre", open_ridge=True, frame_rgb=STEEL_GALV),
     dict(classes=["farm_livestock", "farm_shed"], short=[10, 36], area=[150, 3000], weight=1.0)),
    ("FM_open_implement", "Open-fronted implement shed: steel frame, sheeted back and ends",
     dict(L=30, W=12, H=5.0, roof="pitch", pitch=12, bay=6.0, wall="sheet", plinth="block", plinth_h=1.5,
          front="A*", back=".*", right=". .", left=". .", roof_kind="sheet", roof_rgb=JUNIPER, fill="dark"),
     dict(classes=["farm_shed", "farm_store"], short=[6, 16], area=[60, 900], weight=0.9, ratio=1.6)),
    ("FM_straw_barn", "Straw barn: push walls, open above, big bales stacked inside",
     dict(L=30, W=18, H=7.0, roof="pitch", pitch=12, bay=6.0, wall="sheet", panel_h=2.4,
          front="B*", back=".*", right="B B B", left="B B B", roof_kind="fibre", fill="straw"),
     dict(classes=["farm_store"], short=[10, 26], area=[150, 2000], weight=0.7)),
    ("FM_dutch_barn", "Dutch barn: curved roof on steel stanchions, open on every side, hay inside",
     dict(L=24, W=10, H=5.5, roof="curve", rise=2.2, bay=4.8, gable_bay=10, wall="tin", wall_rgb=RUST,
          front="A*", back="A*", right="A", left="A", roof_kind="tin", roof_rgb=(0.14, 0.06, 0.03), fill="hay", gutters=False,
          frame_rgb=(0.08, 0.06, 0.05), rooflights=False, gable_open=True),
     dict(classes=["farm_store", "farm_shed"], short=[6, 14], area=[50, 600], weight=0.6)),
    ("FM_lean_to", "Low open-fronted shed: a shallow roof, tin sheeted, three open bays",
     dict(L=18, W=8, H=3.4, roof="pitch", pitch=8, bay=4.5, wall="tin", wall_rgb=TIN_GREY,
          front="A A . A", back=".*", right=".", left=".", roof_kind="tin", roof_rgb=(0.2, 0.2, 0.19), fill="dark", rooflights=False),
     dict(classes=["farm_shed", "farm_small"], short=[3, 9], area=[15, 250], weight=1.0)),
    ("FM_dales_barn", "Dales field barn: limestone, stone slate roof, cart arch, slit vents, byre doors",
     dict(L=14, W=7.5, H=4.0, roof="pitch", pitch=32, bay=3.5, gable_bay=7.5, wall="stone", wall_rgb=LIMESTONE,
          dress_rgb=(0.48, 0.47, 0.43), front="D C V D", back="V*", right="O", left=".", roof_kind="stone_slate", gutters=False),
     dict(classes=["barn_stone", "farm_old"], short=[4, 12], area=[25, 400], weight=1.0, regions=["dales", "north", "scotland", "wales"])),
    ("FM_bank_barn", "Northern bank barn: gritstone, slate roof, big cart arch and small doors",
     dict(L=22, W=9, H=5.0, roof="pitch", pitch=30, bay=4.4, gable_bay=9, wall="stone", wall_rgb=GRITSTONE,
          dress_rgb=(0.3, 0.27, 0.22), front="V D C D V", back="V . V . V", right="O", left="D", roof_kind="slate", gutters=False),
     dict(classes=["barn_stone", "farm_old"], short=[7.5, 14], area=[80, 700], weight=1.0, regions=["north", "dales", "west", "wales", "scotland"])),
    ("FM_brick_barn", "Threshing barn: red brick, clay tiles, tall double doors, diamond vents",
     dict(L=24, W=9, H=5.5, roof="pitch", pitch=40, bay=4.8, gable_bay=9, wall="brick", wall_rgb=BRICK_SOFT,
          front="V . K . V", back="V . K . V", right="O", left="O", roof_kind="clay", gutters=False),
     dict(classes=["barn_brick", "farm_old"], short=[6, 14], area=[80, 700], weight=1.0, regions=["midlands", "east", "south"])),
    ("FM_black_barn", "Eastern black barn: tarred weatherboarding on a brick plinth, corrugated roof",
     dict(L=24, W=10, H=5.0, roof="pitch", pitch=35, bay=4.8, gable_bay=10, wall="weatherboard", plinth="block", plinth_h=0.8,
          front=". . K . .", back=".*", right="O", left=".", roof_kind="tin", roof_rgb=(0.2, 0.07, 0.035), rooflights=False, gutters=False),
     dict(classes=["barn_boarded", "farm_old"], short=[6, 14], area=[60, 700], weight=1.0, regions=["east", "south"])),
    ("FM_tin_shed", "Old corrugated iron shed: rusty, a pair of boarded doors",
     dict(L=10, W=6, H=3.2, roof="pitch", pitch=18, bay=5.0, gable_bay=6, wall="tin", plinth="none",
          front=". .", back=".*", right="D", left=".", roof_kind="tin", rooflights=False),
     dict(classes=["farm_small", "farm_old"], short=[3, 8], area=[12, 120], weight=1.0)),
]


def build_all():
    K.clear()
    units = []
    for name, label, spec, suits in DESIGNS:
        U = unit(name, label, spec)
        U.spec = spec
        U.suits = suits
        U.tris = K.triangle_counts(U)
        units.append(U)
        print("%-18s LOD0 %6d  LOD1 %6d  LOD2 %6d  (%gx%g m)" % (name, U.tris[0], U.tris[1], U.tris[2], spec["L"], spec["W"]))
    return units


def main():
    args = sys.argv[1:]
    units = build_all()
    if "--render" in args:
        out = args[args.index("--render") + 1]
        import blender_farm_kit_render as R
        R.render_sheet(units, out)


if __name__ == "__main__":
    main()
