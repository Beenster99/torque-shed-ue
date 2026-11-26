"""Ride UK's industrial estates: a modular kit of UK warehouse, distribution and trade units, modelled by
script in Blender (beenster Nov 25: "could you make blender models out of this image?", a store preview of
a commercial warehouse pack, then a big blue banded distribution depot). Our own original designs in that
general style, from general knowledge of UK portal-frame sheds; no model, company, name or logo is copied.

A unit is a rectangle L long (x) by W deep (y), its front elevation at y = 0 facing -y, sized to whatever
outline the map gives. Every wall is cut into structural bays (about 7.5 m on the long sides, 6 m on the
gables, as a portal frame is) and each bay is filled from a short pattern string, so the same design fits any
size of building:

    "OO E R D* R"   two office bays, a glazed entrance, a roller door, as many docks as fit, a roller door

  .  plain bay (a downpipe on its edge)       R  level-access roller door (bollards, a light)
  D  dock-leveller loading bay (shelter, bumpers, light; on a dock-level unit, a pit face below)
  P  personnel door (canopy, light, a louvre; on a dock-level unit, steel stairs up to it)
  O  office bay: brick to the first floor, ribbon windows on two floors
  E  glazed entrance block (curtain wall, doors, a blank sign band)
  T  trade-counter unit of a terrace (roller door, glazed counter, door, blank sign board, party flashing)
  S  retail shopfront (full-height glazing, canopy, a blank sign panel on the parapet)
  G  supermarket glazing (tall glass and the canopy, no sign)
  M  supermarket entrance lobby (a glazed box under a deep canopy, the store's name on top)
  U  a shop unit of a retail terrace (brick piers, glazed front, its own fascia and name)
  W  high-level ribbon windows                H  a big hangar door
  L  high louvres and wall lights only
  *  repeats the letter before it to fill the bays left over

Roofs: "curve" (the shallow barrel of the newer sheds), "pitch" (a low duo pitch), "multi" (several spans
behind the walls, as a big depot has) or "flat" (behind a parapet, the retail park unit). Rooflights run
across the slopes and solar arrays can cover them. Optional: a two-storey office annex on a gable, banded
cladding, a dock-level floor (a white concrete base all round at 1.2 m).

Every piece is tagged with the furthest LOD it stays in (0 = close detail only, 1 = middle, 2 = the far
shell), so each unit reports LOD0/1/2 triangle counts. Frame: x along the front, y into the building, z up,
the ground at z = 0; walls run 0.6 m below the ground so they never stand proud of a slope.

    python Tools/blender_warehouse_kit.py --render DIR     # the review sheet of the designs in DIR

Tools/blender_warehouse_kit_export.py cuts the units into pieces for the game (World/WorldRideWarehouses.cpp).
"""
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from blender_street_kit import _object, box  # noqa: E402

FOOT = -0.6
DOCK_FLOOR = 1.2

# Colours (linear). Cladding colours are the usual plastisol greys, blues and greens of UK sheds.
GOOSEWING = (0.42, 0.44, 0.45)
MERLIN = (0.16, 0.17, 0.18)
ALUMINIUM = (0.52, 0.53, 0.53)
OLIVE = (0.12, 0.15, 0.1)
ANTHRACITE = (0.035, 0.038, 0.042)
NAVY = (0.012, 0.03, 0.08)
BLUE_BANDS = [(0.012, 0.035, 0.1), (0.02, 0.06, 0.16), (0.04, 0.12, 0.27), (0.1, 0.24, 0.45), (0.25, 0.45, 0.65)]
BLUE_TRIM = (0.02, 0.06, 0.25)
GREEN_TRIM = (0.02, 0.12, 0.06)
BRICK_RED = (0.25, 0.085, 0.05)
BRICK_BUFF = (0.42, 0.28, 0.15)
BLOCK = (0.36, 0.35, 0.32)
CONCRETE_WHITE = (0.62, 0.62, 0.6)
ROOF_GREY = (0.33, 0.345, 0.355)
ROOFLIGHT = (0.5, 0.52, 0.5)
GLASS = (0.02, 0.03, 0.04)
FRAME_GREY = (0.08, 0.085, 0.09)
SHUTTER = (0.55, 0.56, 0.56)
DOOR_GREY = (0.3, 0.31, 0.32)
YELLOW = (0.8, 0.5, 0.0)
RUBBER = (0.012, 0.012, 0.012)
STEEL = (0.25, 0.26, 0.27)
SOLAR = (0.01, 0.015, 0.04)
SIGN_WHITE = (0.75, 0.75, 0.73)
LAMP = (0.9, 0.9, 0.85)


# Where the made-up names go: every sign board's face as it is made (its middle, the way it faces, its width
# and height, and what kind of name it carries), for the export to hand the game (World/WorldRideWarehouses.cpp
# writes the names there). Emptied with the scene.
SIGNS = []


def sign(U, w, name, s0, s1, z0, z1, out0, out1, kind, lod=1, rgb=SIGN_WHITE):
    """A blank sign board on the wall, its face kept for a name of the given kind (see SIGNS)."""
    ob = w.box(U, name, s0, s1, z0, z1, out0, out1, "sign", rgb, lod)
    SIGNS.append((w.at((s0 + s1) * 0.5, out1, (z0 + z1) * 0.5), Vector(w.n), abs(s1 - s0), abs(z1 - z0), kind, U))
    return ob


class Part:
    def __init__(self, obj, surface, rgb, lod):
        self.obj, self.surface, self.rgb, self.lod = obj, surface, rgb, lod


class Unit:
    """One assembled building: its parts, each with the furthest LOD it stays in."""
    def __init__(self, name, label):
        self.name, self.label = name, label
        self.parts = []

    def add(self, obj, surface, rgb, lod=2):
        self.parts.append(Part(obj, surface, rgb, lod))
        return obj


# --- shapes ----------------------------------------------------------------------------------------------

def solid(name, rings):
    """A closed solid from two or more matching rings of points (a loft): caps on the first and last ring."""
    n = len(rings[0])
    verts = [tuple(p) for r in rings for p in r]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range((len(rings) - 1) * n, len(rings) * n))]
    for k in range(len(rings) - 1):
        a, b = k * n, (k + 1) * n
        faces += [(a + i, a + (i + 1) % n, b + (i + 1) % n, b + i) for i in range(n)]
    return _object(name, verts, faces, 30.0)


def strip(name, top, bottom, x0, x1):
    """A slab swept along x from x0 to x1 between a top curve and a bottom curve [(y, z)] (the roofs)."""
    n = len(top)
    ring = lambda x: [(x, y, z) for y, z in top] + [(x, y, z) for y, z in reversed(bottom)]
    verts = ring(x0) + ring(x1)
    m = 2 * n
    faces = []
    for i in range(m):
        j = (i + 1) % m
        faces.append((i, j, m + j, m + i))
    for i in range(n - 1):   # (end caps as quads between top and bottom: the slab is thin and bends)
        a, b = i, i + 1
        c, d = m - 1 - (i + 1), m - 1 - i
        faces.append((d, c, b, a))
        faces.append((m + a, m + b, m + c, m + d))
    return _object(name, verts, faces, 25.0)


class Wall:
    """An elevation: a straight wall from `origin` along unit vector u for `length` metres, facing n."""
    def __init__(self, origin, u, length, top):
        self.o = Vector((origin[0], origin[1], 0.0))
        self.u = Vector((u[0], u[1], 0.0)).normalized()
        self.n = Vector((self.u.y, -self.u.x, 0.0))   # (outward: the right of the walking direction)
        self.length = length
        self.top = top          # wall height, or a function of s for a gable
        self.rot = math.atan2(self.u.y, self.u.x)

    def at(self, s, out, z):
        return self.o + self.u * s + self.n * out + Vector((0, 0, z))

    def box(self, U, name, s0, s1, z0, z1, out0, out1, surface, rgb, lod=2):
        """A box on the wall from s0..s1 along it, z0..z1 up, out0..out1 out from its face."""
        c = self.at((s0 + s1) * 0.5, (out0 + out1) * 0.5, (z0 + z1) * 0.5)
        ob = box(name, c, (abs(s1 - s0), abs(out1 - out0), abs(z1 - z0)), self.rot)
        return U.add(ob, surface, rgb, lod)

    def face(self, U, name, outline, out0, out1, surface, rgb, lod=2):
        """A slab whose face is a convex outline [(s, z)] on the wall, out0..out1 out from it."""
        rings = [[tuple(self.at(s, t, z)) for s, z in outline] for t in (out0, out1)]
        return U.add(solid(name, rings), surface, rgb, lod)


# --- roofs -----------------------------------------------------------------------------------------------

def roof_z(spec, y):
    W, H = spec["W"], spec["H"]
    kind = spec["roof"]
    if kind == "curve":
        r = spec.get("rise", W * 0.065)
        R = (W * W / 4 + r * r) / (2 * r)
        d = y - W / 2
        return H + math.sqrt(max(R * R - d * d, 0.0)) - (R - r)
    if kind == "pitch":
        return H + math.tan(math.radians(spec.get("pitch", 6.0))) * (W / 2 - abs(y - W / 2))
    if kind == "multi":
        n = spec.get("spans", 3)
        span = W / n
        k = min(max(y, 0.0), W - 1e-6) % span
        return H - 0.4 + math.tan(math.radians(spec.get("pitch", 8.0))) * (span / 2 - abs(k - span / 2))
    return H - 0.5   # flat, behind the parapet


def roof_samples(spec, y0, y1, n):
    pts = [y0 + (y1 - y0) * i / n for i in range(n + 1)]
    if spec["roof"] in ("pitch", "multi"):   # (keep the ridges and valleys sharp)
        W = spec["W"]
        spans = spec.get("spans", 3) if spec["roof"] == "multi" else 1
        for k in range(2 * spans + 1):
            y = W * k / (2 * spans)
            if y0 < y < y1:
                pts.append(y)
        pts = sorted(set(round(p, 4) for p in pts))
    return pts


def build_roof(U, spec):
    W, L = spec["W"], spec["L"]
    kind = spec["roof"]
    over = 0.0 if kind in ("flat", "multi") else 0.35
    x0, x1 = -over, L + over
    if kind == "flat":
        U.add(box("roof", (L / 2, W / 2, spec["H"] - 0.6), (L, W, 0.2)), "roof_flat", ROOF_GREY, 2)
        return
    ys = roof_samples(spec, -over, W + over, 24 if kind == "curve" else 2)
    top = [(y, roof_z(spec, y)) for y in ys]
    bot = [(y, z - 0.22) for y, z in top]
    U.add(strip("roof", top, bot, x0, x1), "roof", spec.get("roof_rgb", ROOF_GREY), 2)
    if kind != "multi":
        # (the gutter and fascia along each eave, in the trim colour)
        trim = spec.get("trim", GOOSEWING)
        for y, s in ((-over, -1), (W + over, 1)):
            z = roof_z(spec, y)
            U.add(box("gutter", (L / 2, y + s * 0.1, z - 0.2), (L + 2 * over, 0.22, 0.32)), "metal", trim, 1)
    # Rooflights: strips across the slopes in alternate bays (about 10% of the roof, as the regs ask).
    bays = max(1, round(L / spec.get("bay", 7.5)))
    bw = L / bays
    lights = spec.get("rooflights", True)
    if lights:
        bands = []
        if kind == "curve":
            bands = [(W * 0.12, W * 0.42), (W * 0.58, W * 0.88)]
        elif kind == "pitch":
            bands = [(W * 0.1, W * 0.4), (W * 0.6, W * 0.9)]
        else:
            n = spec.get("spans", 3)
            sp = W / n
            for k in range(n):
                bands += [(k * sp + sp * 0.12, k * sp + sp * 0.4), (k * sp + sp * 0.6, k * sp + sp * 0.88)]
        solar = spec.get("solar")
        for b in range(bays):
            if b % 2 == 0 or b == 0 or b == bays - 1:
                continue
            for y0, y1 in bands:
                if solar and solar[0] <= (y0 + y1) / (2 * W) <= solar[1]:
                    continue
                xa, xb = b * bw + bw * 0.3, b * bw + bw * 0.7
                ys = roof_samples(spec, y0, y1, 6 if kind == "curve" else 1)
                t = [(y, roof_z(spec, y) + 0.06) for y in ys]
                bt = [(y, z - 0.07) for y, z in t]
                U.add(strip("rooflight", t, bt, xa, xb), "rooflight", ROOFLIGHT, 1)
    # Solar: arrays over a share of the roof (fractions of the depth), in rows down the slope.
    solar = spec.get("solar")
    if solar:
        ya, yb = W * solar[0], W * solar[1]
        xa, xb = bw * 0.6, L - bw * 0.6
        ys = roof_samples(spec, ya, yb, 10 if kind == "curve" else 1)
        if kind == "pitch":
            ys = [y for y in ys if True]
        t = [(y, roof_z(spec, y) + 0.12) for y in ys]
        bt = [(y, z - 0.05) for y, z in t]
        U.add(strip("solar", t, bt, xa, xb), "solar", SOLAR, 2)


# --- bay pieces ------------------------------------------------------------------------------------------

def light(U, w, s, z):
    w.box(U, "lamp", s - 0.2, s + 0.2, z, z + 0.12, 0.0, 0.3, "frame", FRAME_GREY, 0)
    w.box(U, "lens", s - 0.16, s + 0.16, z - 0.01, z + 0.01, 0.04, 0.27, "lamp", LAMP, 0)


def bollards(U, w, s0, s1):
    for s in (s0 - 0.45, s1 + 0.45):
        U.add(box("bollard", w.at(s, 0.6, 0.6), (0.17, 0.17, 1.2)), "paint", YELLOW, 0)


def roller_door(U, w, s, width, height, floor):
    a, b = s - width / 2, s + width / 2
    w.box(U, "shutter", a, b, floor, floor + height, 0.0, 0.05, "shutter", SHUTTER, 2)
    w.box(U, "coil", a - 0.15, b + 0.15, floor + height, floor + height + 0.6, 0.0, 0.5, "metal", ALUMINIUM, 1)
    for x in (a - 0.15, b):
        w.box(U, "guide", x, x + 0.15, floor, floor + height, 0.0, 0.14, "metal", ALUMINIUM, 1)
    w.box(U, "bottom", a, b, floor, floor + 0.12, 0.05, 0.08, "paint", YELLOW, 0)
    light(U, w, s, floor + height + 1.0)


def dock_face(U, w, a, b, spec):
    """A dock-level unit's pit face below a door: dark concrete with the leveller's lip and two bumpers."""
    w.box(U, "pitface", a, b, FOOT, DOCK_FLOOR, 0.0, 0.08, "concrete", (0.2, 0.2, 0.19), 1)
    w.box(U, "lip", a + 0.3, b - 0.3, DOCK_FLOOR - 0.12, DOCK_FLOOR, 0.0, 0.35, "metal", STEEL, 0)
    for x in (a + 0.25, b - 0.55):
        w.box(U, "bumper", x, x + 0.3, DOCK_FLOOR - 0.6, DOCK_FLOOR - 0.05, 0.0, 0.18, "rubber", RUBBER, 0)


def dock_door(U, w, s, spec):
    floor = DOCK_FLOOR if spec.get("dock_floor") else 1.2
    width, height = 3.0, 3.0
    a, b = s - width / 2, s + width / 2
    if not spec.get("dock_floor"):
        dock_face(U, w, a - 0.3, b + 0.3, spec)
    else:
        dock_face(U, w, a - 0.2, b + 0.2, spec)
    w.box(U, "dockdoor", a, b, floor, floor + height, 0.0, 0.05, "panel_door", (0.62, 0.63, 0.63), 2)
    for k in range(2):
        w.box(U, "vision", s - 0.35 + k * 0.5, s - 0.15 + k * 0.5, floor + 1.4, floor + 1.7, 0.05, 0.06, "glass", GLASS, 0)
    # (the shelter: a grey frame and black curtains standing out from the wall)
    sh = 0.85
    col = spec.get("shelter", (0.35, 0.36, 0.37))
    w.box(U, "shelter_top", a - 0.55, b + 0.55, floor + height, floor + height + 0.75, 0.0, sh, "metal", col, 1)
    for x0, x1 in ((a - 0.55, a - 0.05), (b + 0.05, b + 0.55)):
        w.box(U, "shelter_side", x0, x1, floor, floor + height, 0.0, sh, "metal", col, 1)
        w.box(U, "curtain", x0 + 0.1, x1 - 0.1, floor + 0.1, floor + height, sh, sh + 0.04, "rubber", RUBBER, 0)
    w.box(U, "curtain_top", a, b, floor + height + 0.05, floor + height + 0.6, sh, sh + 0.04, "rubber", RUBBER, 0)
    for x in (a - 0.4, b + 0.25):   # (the yellow guide posts each side)
        w.box(U, "guidepost", x, x + 0.15, floor + 0.3, floor + 2.2, sh, sh + 0.06, "paint", YELLOW, 0)
    light(U, w, b + 1.0, floor + height + 0.3)
    w.box(U, "bayplate", s - 0.25, s + 0.25, floor + height + 1.6, floor + height + 2.1, 0.0, 0.03, "sign", SIGN_WHITE, 0)


def stairs(U, w, s, top, side=1):
    """Galvanised steel stairs up the wall to a door at `top`, with a landing and handrails."""
    run = top * 1.1
    land = 1.2
    sa = s + side * (land / 2)
    w.box(U, "landing", s - land / 2, s + land / 2, top - 0.08, top, 0.0, 1.1, "metal", STEEL, 0)
    steps = max(3, int(top / 0.18))
    for k in range(steps):
        z = top - (k + 1) * top / steps
        x = sa + side * (k + 0.5) * run / steps
        w.box(U, "step", x - run / steps / 2, x + run / steps / 2, z - 0.04, z, 0.1, 1.0, "metal", STEEL, 0)
    for out in (0.05, 1.05):
        # (stringer and rail, as sloping boxes: built as faces on the wall plane offset outward)
        p = [(sa, top), (sa + side * run, 0.0), (sa + side * run, 0.2), (sa, top + 0.2)]
        if side < 0:
            p = list(reversed(p))
        w.face(U, "stringer", p, out, out + 0.06, "metal", STEEL, 0)
        r = [(sa, top + 0.95), (sa + side * run, 0.95), (sa + side * run, 1.0), (sa, top + 1.0)]
        if side < 0:
            r = list(reversed(r))
        w.face(U, "rail", r, out, out + 0.05, "metal", STEEL, 0)
    w.box(U, "landrail", s - land / 2, s + land / 2, top + 0.95, top + 1.0, 1.05, 1.1, "metal", STEEL, 0)


def personnel_door(U, w, s, spec, z=0.0, colour=None):
    col = colour or spec.get("door_rgb", spec.get("trim", DOOR_GREY))
    w.box(U, "pdoor", s - 0.5, s + 0.5, z, z + 2.15, 0.0, 0.05, "paint", col, 1)
    w.box(U, "canopy", s - 0.7, s + 0.7, z + 2.35, z + 2.45, 0.0, 0.8, "metal", ALUMINIUM, 0)
    light(U, w, s + 0.9, z + 2.6)


def louvre(U, w, s, z, width=1.6, h=1.0):
    w.box(U, "louvre", s - width / 2, s + width / 2, z, z + h, 0.0, 0.08, "louvre", (0.12, 0.125, 0.13), 1)


def ribbon(U, w, a, b, z0, z1, mull=1.25, lod_glass=2):
    w.box(U, "glass", a, b, z0, z1, -0.02, 0.03, "glass", GLASS, lod_glass)
    t = 0.07
    for z in (z0, z1 - t):
        w.box(U, "transom", a, b, z, z + t, 0.0, 0.07, "frame", FRAME_GREY, 0)
    n = max(1, round((b - a) / mull))
    for k in range(n + 1):
        x = a + (b - a) * k / n
        w.box(U, "mullion", x - t / 2, x + t / 2, z0, z1, 0.0, 0.07, "frame", FRAME_GREY, 0)
    w.box(U, "sill", a - 0.05, b + 0.05, z0 - 0.06, z0, 0.0, 0.1, "metal", ALUMINIUM, 0)


def curtain(U, w, a, b, z0, z1, out0, out1, grid=(1.5, 1.4)):
    """A glazed curtain wall from a..b, z0..z1, standing out0..out1 from the wall face."""
    w.box(U, "cwglass", a, b, z0, z1, out1 - 0.06, out1 - 0.02, "glass", GLASS, 2)
    t = 0.09
    n = max(1, round((b - a) / grid[0]))
    for k in range(n + 1):
        x = a + (b - a) * k / n
        w.box(U, "cwmull", x - t / 2, x + t / 2, z0, z1, out1 - 0.06, out1 + 0.05, "frame", FRAME_GREY, 0)
    m = max(1, round((z1 - z0) / grid[1]))
    for k in range(m + 1):
        z = z0 + (z1 - z0) * k / m
        w.box(U, "cwtran", a, b, z - t / 2, z + t / 2, out1 - 0.06, out1 + 0.05, "frame", FRAME_GREY, 0)


def entrance(U, w, a, b, spec):
    """A glazed entrance block standing proud of the front, up past the eaves, with a blank sign band."""
    H = spec["H"]
    top = H + 1.2
    proj = 1.6
    trim = spec.get("trim", MERLIN)
    w.box(U, "ent_shell", a, b, FOOT, top, 0.0, proj, "clad_v", trim, 2)
    curtain(U, w, a + 0.5, b - 0.5, 0.0, top - 1.6, 0.0, proj, (1.5, top / 6))
    w.box(U, "ent_band", a, b, top - 1.5, top, proj, proj + 0.04, "clad", trim, 1)
    sign(U, w, "ent_sign", (a + b) / 2 - 2.0, (a + b) / 2 + 2.0, top - 1.25, top - 0.25, proj + 0.04, proj + 0.08, "firm")
    s = (a + b) / 2
    w.box(U, "ent_door", s - 1.0, s + 1.0, 0.0, 2.4, proj, proj + 0.02, "glass", (0.04, 0.05, 0.06), 0)
    w.box(U, "ent_canopy", s - 2.0, s + 2.0, 2.9, 3.1, proj, proj + 2.0, "metal", ALUMINIUM, 1)
    # (the block's own sides: side walls in the trim colour, glazed down the side next to the door)
    side = Wall(tuple(w.at(b, proj, 0)), tuple(-w.n), proj, top)
    curtain(U, side, 0.2, proj - 0.2, 0.0, top - 1.6, 0.0, 0.03, (0.8, top / 6))


def trade_counter(U, w, a, b, spec, first, last):
    H = spec["H"]
    bw = b - a
    rd = min(4.0, bw * 0.45)
    roller_door(U, w, a + 0.6 + rd / 2, rd, min(4.5, H - 1.6), 0.0)
    ca, cb = a + rd + 1.4, b - 0.6
    curtain(U, w, ca, cb, 0.0, min(3.6, H - 2.2), 0.0, 0.08, (1.2, 1.2))
    w.box(U, "counterdoor", cb - 1.6, cb - 0.4, 0.0, 2.3, 0.08, 0.1, "frame", FRAME_GREY, 0)
    sign(U, w, "signboard", ca - 0.2, cb + 0.2, min(3.9, H - 1.9), min(4.9, H - 0.9), 0.0, 0.06, "trade")
    if not last:
        w.box(U, "party", b - 0.12, b + 0.12, FOOT, H, 0.0, 0.1, "metal", spec.get("trim", MERLIN), 1)


def shopfront(U, w, a, b, spec):
    H = spec["H"]
    curtain(U, w, a + 0.3, b - 0.3, 0.0, 4.6, 0.0, 0.1, (1.6, 2.3))
    w.box(U, "canopy", a, b, 4.8, 5.1, 0.0, 2.2, "metal", spec.get("trim", MERLIN), 1)
    for x in (a + 0.3, b - 0.5):
        w.box(U, "col", x, x + 0.2, 0.0, 4.8, 2.0, 2.2, "metal", ALUMINIUM, 0)
    s = (a + b) / 2
    sign(U, w, "shopsign", s - (b - a) * 0.35, s + (b - a) * 0.35, H - 2.4, H - 0.6, 0.0, 0.1, "shop")


def glazing(U, w, a, b, spec):
    """A supermarket's glazed front either side of its lobby: tall glass, the canopy run on, no sign."""
    curtain(U, w, a + 0.2, b - 0.2, 0.0, 4.6, 0.0, 0.1, (1.6, 2.3))
    w.box(U, "canopy", a, b, 4.8, 5.1, 0.0, 3.0, "metal", spec.get("trim", MERLIN), 1)
    w.box(U, "col", a + 0.1, a + 0.3, 0.0, 4.8, 2.8, 3.0, "metal", ALUMINIUM, 0)


def lobby(U, w, a, b, spec):
    """A supermarket's entrance lobby: a glazed box standing out under a deep canopy, up past the parapet,
    with the store's name across its top."""
    H = spec["H"] + spec.get("parapet", 1.0)
    top = H + 1.8
    proj = 3.5
    trim = spec.get("trim", MERLIN)
    w.box(U, "lob_shell", a, b, FOOT, top, 0.0, proj, "clad", spec.get("lobby_rgb", trim), 2)
    curtain(U, w, a + 0.4, b - 0.4, 0.0, 5.2, 0.0, proj, (1.5, 1.3))
    w.box(U, "lob_band", a, b, 5.4, top, proj, proj + 0.04, "clad", trim, 1)
    sign(U, w, "lob_sign", a + 0.6, b - 0.6, top - 2.6, top - 0.5, proj + 0.04, proj + 0.1, "store")
    s = (a + b) / 2
    for k in (-1, 1):   # (two pairs of sliding doors)
        w.box(U, "lob_door", s + k * 1.6 - 1.0, s + k * 1.6 + 1.0, 0.0, 2.5, proj, proj + 0.02, "glass", (0.04, 0.05, 0.06), 0)
    w.box(U, "lob_canopy", a - 1.0, b + 1.0, 5.2, 5.5, 0.0, proj + 3.5, "metal", trim, 1)
    for x in (a - 0.6, b + 0.4):
        w.box(U, "lob_col", x, x + 0.25, 0.0, 5.2, proj + 3.1, proj + 3.35, "metal", ALUMINIUM, 0)
    side = Wall(tuple(w.at(b, proj, 0)), tuple(-w.n), proj, top)
    curtain(U, side, 0.3, proj - 0.3, 0.0, 5.2, 0.0, 0.03, (1.2, 1.3))
    side = Wall(tuple(w.at(a, 0.0, 0)), tuple(w.n), proj, top)
    curtain(U, side, 0.3, proj - 0.3, 0.0, 5.2, 0.0, 0.03, (1.2, 1.3))


def unit_front(U, w, a, b, spec, last):
    """One shop of a retail terrace: brick piers each side, a glazed front, its own fascia board and name."""
    H = spec["H"]
    brick = spec.get("brick_rgb", BRICK_RED)
    w.box(U, "pier", a, a + 0.6, FOOT, H + spec.get("parapet", 1.0) - 0.15, 0.0, 0.45, "brick", brick, 2)
    if last:
        w.box(U, "pier", b - 0.6, b, FOOT, H + spec.get("parapet", 1.0) - 0.15, 0.0, 0.45, "brick", brick, 2)
    curtain(U, w, a + 0.6, b - (0.6 if last else 0.0), 0.0, 4.3, 0.0, 0.08, (1.8, 2.15))
    w.box(U, "fascia", a + 0.6, b - (0.6 if last else 0.0), 4.5, 6.3, 0.0, 0.35, "clad", spec.get("trim", MERLIN), 1)
    s = (a + b) / 2
    sign(U, w, "unitsign", s - (b - a) * 0.36, s + (b - a) * 0.36, 4.75, 6.05, 0.35, 0.4, "shop")
    w.box(U, "unitdoor", s - 1.0, s + 1.0, 0.0, 2.4, 0.08, 0.1, "frame", FRAME_GREY, 0)


def office_bay(U, w, a, b, spec, tall=False):
    H = spec["H"]
    floor = DOCK_FLOOR if spec.get("dock_floor") else 0.0
    z1 = floor + 0.9
    ribbon(U, w, a + 0.4, b - 0.4, z1, z1 + 1.5)
    z2 = floor + 4.3
    if z2 + 1.6 < H - 0.6:
        ribbon(U, w, a + 0.4, b - 0.4, z2, z2 + 1.5)


# --- the unit --------------------------------------------------------------------------------------------

def expand(pattern, n):
    """A bay pattern ("OO E D* R") filled out to n bays."""
    toks = [t for t in pattern.replace(" ", "")]
    out, star = [], None
    for i, t in enumerate(toks):
        if t == "*":
            star = len(out) - 1
        else:
            out.append(t)
    if star is not None:
        while len(out) < n:
            out.insert(star, out[star])
        while len(out) > n and out.count(out[star]) > 1:
            out.pop(star)
    out = (out + ["."] * n)[:n]
    return out


def cladding(U, w, a, b, z0, z1, spec, lod=2):
    """The wall's sheeting between z0 and z1: one colour, or the unit's colour bands bottom to top."""
    bands = spec.get("bands")
    surf = spec.get("clad_surface", "clad")
    if not bands:
        w.box(U, "clad", a, b, z0, z1, -0.15, 0.0, surf, spec.get("clad_rgb", GOOSEWING), lod)
        top = spec.get("top_band")
        if top and z1 - z0 > 3.0:
            w.box(U, "topband", a, b, z1 - top[0], z1, 0.0, 0.015, surf, top[1], lod)
        return
    edges = spec.get("band_edges")
    n = len(bands)
    for k, rgb in enumerate(bands):
        e0 = z0 + (z1 - z0) * (edges[k] if edges else k / n)
        e1 = z0 + (z1 - z0) * (edges[k + 1] if edges else (k + 1) / n)
        w.box(U, "band", a, b, e0, e1, -0.15, 0.0, surf, rgb, lod)


def plinth_of(spec, letter):
    if spec.get("dock_floor"):
        return DOCK_FLOOR, "concrete", CONCRETE_WHITE
    if letter in "OE" and spec.get("office_brick"):
        return 3.2, "brick", spec.get("brick_rgb", BRICK_RED)
    kind = spec.get("plinth", "brick")
    if kind == "none":
        return None
    return spec.get("plinth_h", 2.1), kind, spec.get("brick_rgb", BRICK_RED) if kind == "brick" else BLOCK


def build_wall(U, spec, w, pattern, bay, is_long):
    n = max(1, round(w.length / bay))
    bw = w.length / n
    letters = expand(pattern, n)
    top = w.top
    for i, t in enumerate(letters):
        a, b = i * bw, (i + 1) * bw
        # (the sheeting: a plain rectangle on the long walls, the gable's outline on the ends)
        p = plinth_of(spec, t)
        z0 = p[0] if p else FOOT
        if callable(top):
            k = 4 if spec["roof"] == "curve" else 1
            ss = [a + (b - a) * j / k for j in range(k + 1)]
            if spec["roof"] in ("pitch", "multi"):
                ss = sorted(set(ss + [s for s in _gable_breaks(spec, w) if a < s < b]))
            surf = spec.get("clad_surface", "clad")
            bands = spec.get("bands")
            if bands:
                base = spec["H"] + (spec.get("parapet", 0.0) if spec["roof"] in ("flat", "multi") else 0.0)
                cladding(U, w, a, b, z0, base, spec)
                if any(top(s) > base + 0.05 for s in ss):
                    outline = [(a, base - 0.01), (b, base - 0.01)] + [(s, max(top(s), base)) for s in reversed(ss)]
                    w.face(U, "gable_top", outline, -0.15, 0.0, surf, bands[-1], 2)
            else:
                outline = [(a, z0), (b, z0)] + [(s, top(s)) for s in reversed(ss)]
                w.face(U, "gable", outline, -0.15, 0.0, surf, spec.get("clad_rgb", GOOSEWING), 2)
        else:
            cladding(U, w, a, b, z0, top, spec)
        bay_features(U, spec, w, t, a, b, i, n, is_long, p)


def bay_features(U, spec, w, t, a, b, i, n, is_long, p, every_pipe=False):
    """A bay's plinth and what stands on it: doors, docks, windows, lights (see the pattern letters)."""
    bw = b - a
    if p:
        w.box(U, "plinth", a, b, FOOT, p[0], 0.0, 0.04, p[1], p[2], 2)
    floor = DOCK_FLOOR if spec.get("dock_floor") else 0.0
    H = spec["H"]
    s = (a + b) / 2
    if t == "R":
        if spec.get("dock_floor"):
            dock_face(U, w, s - 2.4, s + 2.4, spec)
        roller_door(U, w, s, min(4.8, bw * 0.65), min(5.0, H - floor - 2.0), floor)
        if not spec.get("dock_floor"):
            bollards(U, w, s - min(4.8, bw * 0.65) / 2, s + min(4.8, bw * 0.65) / 2)
    elif t == "D":
        dock_door(U, w, s, spec)
    elif t == "P":
        personnel_door(U, w, s, spec, z=floor)
        if spec.get("dock_floor"):
            stairs(U, w, s, DOCK_FLOOR, side=1)
        louvre(U, w, s, H - 3.0)
    elif t == "O":
        office_bay(U, w, a, b, spec)
    elif t == "E":
        entrance(U, w, a, b, spec)
    elif t == "T":
        trade_counter(U, w, a, b, spec, i == 0, i == n - 1)
    elif t == "S":
        shopfront(U, w, a, b, spec)
    elif t == "G":
        glazing(U, w, a, b, spec)
    elif t == "M":
        lobby(U, w, a, b, spec)
    elif t == "U":
        unit_front(U, w, a, b, spec, i == n - 1)
    elif t == "W":
        ribbon(U, w, a + 0.6, b - 0.6, H - 2.6, H - 1.4, lod_glass=1)
    elif t == "H":
        roller_door(U, w, s, bw * 0.85, H - 1.0, 0.0)
    elif t == "L":
        louvre(U, w, s, H - 3.2, 2.2, 1.2)
    if is_long and t in ".DRL" and spec.get("lights", True):
        light(U, w, s - bw * 0.3 if t != "." else s, H - 1.2)
    if is_long and t in "." and (every_pipe or i % 2 == 0) and spec["roof"] in ("curve", "pitch"):
        w.box(U, "downpipe", b - 0.08, b + 0.08, 0.0, H - 0.1, 0.0, 0.14, "metal", spec.get("trim", GOOSEWING), 0)


def _gable_breaks(spec, w):
    W = spec["W"]
    spans = spec.get("spans", 3) if spec["roof"] == "multi" else 1
    return [W * k / (2 * spans) for k in range(2 * spans + 1)]


def corners(U, spec, walls):
    trim = spec.get("trim", GOOSEWING)
    H = spec["H"] + (spec.get("parapet", 0.0) if spec["roof"] in ("flat", "multi") else 0.0)
    for w in walls:
        z0 = DOCK_FLOOR if spec.get("dock_floor") else spec.get("plinth_h", 2.1) if spec.get("plinth", "brick") != "none" else FOOT
        w.box(U, "corner", -0.02, 0.32, z0, H, 0.0, 0.03, "metal", trim, 1)
        w.box(U, "corner", w.length - 0.32, w.length + 0.02, z0, H, 0.0, 0.03, "metal", trim, 1)
    if spec["roof"] in ("flat", "multi"):
        for w in walls:
            w.box(U, "coping", -0.1, w.length + 0.1, H - 0.02, H + 0.12, -0.3, 0.05, "metal", trim, 1)


def annex(U, spec):
    """A two-storey office block on the left gable: brick, punched windows, its own low pitched roof."""
    a = spec["annex"]
    W = spec["W"]
    depth, width, h = a.get("depth", 10.0), W * a.get("share", 0.6), a.get("height", 7.4)
    brick = a.get("brick", spec.get("brick_rgb", BRICK_BUFF))
    walls = [Wall((-depth, 0.0), (1, 0), depth, h),          # front
             Wall((-depth, width), (0, -1), width, h),        # outer end
             Wall((0.0, width), (-1, 0), depth, h)]          # back
    upper = a.get("upper")   # (cladding above the first floor, or all brick)
    for k, w in enumerate(walls):
        if upper:
            w.box(U, "an_brick", 0.0, w.length, FOOT, 3.6, -0.3, 0.0, "brick", brick, 2)
            w.box(U, "an_clad", 0.0, w.length, 3.6, h, -0.3, 0.0, "clad", upper, 2)
        else:
            w.box(U, "an_brick", 0.0, w.length, FOOT, h, -0.3, 0.0, "brick", brick, 2)
        n = max(1, int(w.length / 2.8))
        for i in range(n):
            s = w.length * (i + 0.5) / n
            if k == 0 and i == n // 2:
                personnel_door(U, w, s, spec, colour=FRAME_GREY)
                w.box(U, "an_glassdoor", s - 0.6, s + 0.6, 0.0, 2.3, 0.0, 0.04, "glass", GLASS, 1)
            else:
                ribbon(U, w, s - 0.8, s + 0.8, 0.9, 2.3, 0.8, 1)
            ribbon(U, w, s - 0.8, s + 0.8, 4.4, 5.8, 0.8, 1)
    # (its roof: a low duo pitch with the ridge along y, hipped back into the main gable)
    pitch = math.radians(a.get("pitch", 15.0))
    o = 0.3
    rise = math.tan(pitch) * (depth / 2 + o)
    xs = [(-depth - o, h - 0.1), (-depth / 2, h - 0.1 + rise), (0.0, h - 0.1 + rise * o / (depth / 2 + o))]
    # (strip sweeps along x; turned a quarter about z it sweeps along y, so its profile is laid out in -x)
    top = [(-x, z) for x, z in xs]
    bot = [(y, z - 0.2) for y, z in top]
    ob = strip("an_roof", top, bot, -o, width + o)
    ob.matrix_world = Matrix.Rotation(math.pi / 2, 4, "Z")
    U.add(ob, "tile", a.get("roof_rgb", (0.06, 0.065, 0.07)), 2)
    # (the gable infill under its roof at the front and back)
    for w, sgn in ((walls[0], 1), (walls[2], -1)):
        L = w.length
        tri = [(0.0, h), (L, h), (L / 2, h + rise * depth / 2 / (depth / 2 + o) - 0.15)]
        w.face(U, "an_gable", tri, -0.3, 0.0, "brick" if not upper else "clad", brick if not upper else upper, 2)


def unit(name, label, spec):
    """Assemble one unit from its spec."""
    U = Unit(name, label)
    W, L, H = spec["W"], spec["L"], spec["H"]
    roof = spec["roof"]
    par = spec.get("parapet", 1.0) if roof in ("flat", "multi") else 0.0
    if roof in ("flat", "multi"):
        gtop = H + par
    else:
        gtop = None
    gable = (lambda s, back=False: gtop) if gtop else None
    front = Wall((0, 0), (1, 0), L, H + par)
    back = Wall((L, W), (-1, 0), L, H + par)
    if roof == "multi":
        gt = lambda y: max(gtop, roof_z(spec, y) - 0.2)
    elif gtop:
        gt = lambda y: gtop
    else:
        gt = lambda y: roof_z(spec, y) - 0.2
    right = Wall((L, 0), (0, 1), W, lambda s: gt(s))
    left = Wall((0, W), (0, -1), W, lambda s: gt(W - s))
    bay = spec.get("bay", 7.5)
    gbay = spec.get("gable_bay", 6.0)
    if roof == "multi":   # (one slope per gable bay, so every piece of the gable is convex)
        gbay = W / (2 * spec.get("spans", 3))
    build_wall(U, spec, front, spec.get("front", ".*"), bay, True)
    build_wall(U, spec, back, spec.get("back", ".*"), bay, True)
    build_wall(U, spec, right, spec.get("right", ".*"), gbay, False)
    build_wall(U, spec, left, spec.get("left", ".*"), gbay, False)
    corners(U, spec, [front, back, right, left])
    build_roof(U, spec)
    if spec.get("annex"):
        annex(U, spec)
    if spec.get("dock_floor"):
        # (the floor slab's edge reads as the white base all round: covered by each bay's plinth)
        pass
    return U


# --- the designs -----------------------------------------------------------------------------------------

DESIGNS = [
    ("WH_big_box", "Big-box distribution shed: barrel roof, 14 docks, glazed office corner",
     dict(L=150, W=70, H=13.5, roof="curve", rise=4.2, front="OO E D* R R", back="P L* P", right=". R R .", left="O O O O",
          trim=BLUE_TRIM, top_band=(2.4, MERLIN), plinth="brick", office_brick=True, shelter=MERLIN)),
    ("WH_blue_depot", "Banded-cladding depot: dock-level floor, multi-span roof, stairs, office end",
     dict(L=120, W=60, H=14.0, roof="multi", spans=4, pitch=10, parapet=0.0, dock_floor=True,
          bands=BLUE_BANDS, band_edges=[0, 0.38, 0.56, 0.72, 0.86, 1.0], front="O O P D D D D D P D* R P", back=". L*",
          right="P R R R P", left="O O O O O", trim=(0.012, 0.03, 0.09), door_rgb=YELLOW, shelter=(0.45, 0.46, 0.47))),
    ("WH_trade_terrace", "Trade-counter terrace: six units, roller doors, glazed counters, pitched roof",
     dict(L=72, W=24, H=7.5, roof="pitch", pitch=7, bay=12, front="T*", back="P*", right="P .", left=". P",
          clad_rgb=ALUMINIUM, trim=GREEN_TRIM, plinth="block", plinth_h=1.8, top_band=(1.2, (0.05, 0.09, 0.07)))),
    ("WH_office_annex", "Pitched warehouse with blue trim and a two-storey brick office annex",
     dict(L=80, W=40, H=9.0, roof="pitch", pitch=9, front="P R R . . R .", back=". .*", right="R . R . R .", left="R . . . . .",
          clad_rgb=GOOSEWING, trim=BLUE_TRIM, door_rgb=BLUE_TRIM, plinth="brick", brick_rgb=BRICK_BUFF, plinth_h=2.4,
          annex=dict(depth=11, share=0.65, height=7.6, brick=BRICK_BUFF, pitch=14))),
    ("WH_solar_curve", "Barrel-roof unit with solar arrays, roller doors down the side",
     dict(L=100, W=45, H=11.0, roof="curve", rise=3.0, solar=(0.1, 0.9), front="O E R R R R R R O", back=". *",
          right=". R .", left="O O W O", trim=MERLIN, top_band=(1.8, MERLIN), plinth="brick", office_brick=True)),
    ("WH_workshop", "Small workshop unit: vertical cladding, one roller door, office windows",
     dict(L=24, W=15, H=6.0, roof="pitch", pitch=8, bay=6, front="O P R .", back=". *", right="R .", left=". .",
          clad_surface="clad_v", clad_rgb=OLIVE, trim=(0.04, 0.05, 0.035), plinth="block", plinth_h=1.2, lights=True)),
    ("WH_retail_park", "Retail park unit: flat roof behind a parapet, glazed shopfront and canopy",
     dict(L=60, W=45, H=9.5, roof="flat", parapet=1.5, bay=10, front="S*", back="P D . D P", right=". . . . . . .", left="P . . . . .",
          clad_rgb=(0.55, 0.56, 0.55), trim=ANTHRACITE, plinth="block", plinth_h=0.9, top_band=(3.0, ANTHRACITE))),
    # (stage 3, Nov 26: the retail park's other two, a supermarket and a terrace of shop units; made-up names)
    ("WH_supermarket", "Supermarket: flat roof, glazed front, an entrance lobby under a deep canopy, its name on top",
     dict(L=100, W=70, H=8.0, roof="flat", parapet=2.0, bay=10, front="P G M G* .", back="P D D . . D P", right="R . . . .", left=". . . P",
          clad_rgb=(0.6, 0.61, 0.6), trim=(0.06, 0.065, 0.07), lobby_rgb=(0.22, 0.23, 0.24), plinth="brick", brick_rgb=BRICK_BUFF,
          plinth_h=1.2, top_band=(2.6, (0.22, 0.23, 0.24)))),
    ("WH_retail_row", "Retail park terrace: shop units between brick piers, each with its own fascia and name",
     dict(L=90, W=36, H=7.5, roof="flat", parapet=1.4, bay=15, front="U*", back="P . D . P", right="P . .", left=". . P",
          clad_rgb=(0.48, 0.49, 0.49), trim=(0.05, 0.055, 0.06), plinth="brick", brick_rgb=BRICK_RED, plinth_h=1.0)),
    ("WH_hangar", "Hangar-style store: barrel roof, big doors in the gable, high windows",
     dict(L=60, W=36, H=10.0, roof="curve", rise=3.6, front=". P W W P .", back=". W W .", right="H", left="P . .",
          gable_bay=36, clad_rgb=MERLIN, trim=ANTHRACITE, plinth="block", plinth_h=1.5, rooflights=True)),
]


def clear():
    SIGNS.clear()
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for me in list(bpy.data.meshes):
        bpy.data.meshes.remove(me)


def triangle_counts(U):
    dg = bpy.context.evaluated_depsgraph_get()
    counts = [0, 0, 0]
    for p in U.parts:
        ev = p.obj.evaluated_get(dg)
        me = ev.to_mesh()
        me.calc_loop_triangles()
        n = len(me.loop_triangles)
        ev.to_mesh_clear()
        for lod in range(p.lod + 1):
            counts[lod] += n
    return counts


def build_all():
    clear()
    units = []
    for name, label, spec in DESIGNS:
        U = unit(name, label, spec)
        U.spec = spec
        U.tris = triangle_counts(U)
        units.append(U)
        print("%-18s LOD0 %6d  LOD1 %6d  LOD2 %6d  (%dx%d m)" % (name, U.tris[0], U.tris[1], U.tris[2], spec["L"], spec["W"]))
    return units


def main():
    args = sys.argv[1:]
    units = build_all()
    if "--render" in args:
        out = args[args.index("--render") + 1]
        import blender_warehouse_kit_render as R
        R.render_sheet(units, out)
        if not os.environ.get("KIT_ONLY"):
            R.render_details(units, out)


if __name__ == "__main__":
    main()
