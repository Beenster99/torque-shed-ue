"""[caravans] Ride UK's caravans, modelled in Blender (beenster Nov 20: "maybe we can build a few decent caravan
models in blender and add them"). Our own models: no makers' names, no badges, no lettering.

Writes Data/RideUK/Props/SM_Prop_<kind>_<n>.glb and sets each kind's height in Data/RideUK/prop_kit.json to the
model's own, as Tools/blender_street_kit.py does (same material, M_RideStreetKit; same frame: front +x, foot at 0,
y up in the file). World/WorldRideTownLife.cpp (RidePlan::TownLifeProps) puts them on the campsites' pitches:

  touring_caravan 0   a single-axle tourer, white, grey stripes: the sloped front window, the A-frame and hitch,
                      the jockey wheel, the gas locker, corner steadies down, a roof light
  touring_caravan 1   a twin-axle tourer, a longer body, a silver-grey skirt band and bronze stripes
  static_caravan 0    a classic holiday static: cream, a shallow pitched roof, the lounge's wrap-round end
                      windows, a trim stripe, skirting round its blocks, timber steps to the door
  static_caravan 1    a modern static: two-tone (charcoal below, pale grey above), big windows, patio doors
                      to a small deck of steps
  static_caravan 2    a lodge: timber-clad, a steeper dark roof with deep eaves, glazed doors on to a deck
                      along its side with a rail

A kind's models are all the same height (the game scales a model's bounds to the kit's height), so each kind's
roof line is shared. Every model is length along x: a tourer's A-frame at +x (the code-made caravan's), a
static's door side to +y (the game's -z). Lean on triangles: the game draws each as instances drawn whole (no
reduced LODs: build 166 showed UE's reducer cutting holes in them).

Needs Blender's Python module (pip install bpy):

    python Tools/blender_caravan_kit.py               # the models
    python Tools/blender_caravan_kit.py --render DIR  # and a review sheet in DIR
"""
import json
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_street_kit as K   # noqa: E402  (its geometry helpers, writer and kit file)

# (linear colours)
VAN_WHITE = (0.74, 0.74, 0.72)
CREAM = (0.68, 0.62, 0.48)
PALE_GREY = (0.58, 0.6, 0.6)
CHARCOAL = (0.07, 0.075, 0.08)
SILVER = (0.42, 0.43, 0.44)
STRIPE_GREY = (0.16, 0.17, 0.19)
STRIPE_RED = (0.36, 0.05, 0.04)
STRIPE_BRONZE = (0.28, 0.17, 0.08)
STRIPE_GREEN = (0.08, 0.2, 0.12)
TRIM = (0.05, 0.05, 0.055)          # the black plastics: arches, bumpers, window frames
GLASS = (0.025, 0.028, 0.03)        # tinted acrylic and double glazing
ROOF_LIGHT = (0.32, 0.33, 0.32)
TYRE = (0.02, 0.02, 0.022)
HUB = (0.45, 0.46, 0.46)
GALV = (0.48, 0.49, 0.48)
RED_LENS = (0.3, 0.015, 0.01)
AMBER_LENS = (0.35, 0.12, 0.01)
PLATE_WHITE = (0.72, 0.72, 0.7)
PLASTIC_GREY = (0.2, 0.21, 0.22)
SKIRT = (0.34, 0.33, 0.3)
ROOF_GREY = (0.12, 0.125, 0.13)
LODGE_ROOF = (0.05, 0.055, 0.06)
CEDAR = (0.2, 0.12, 0.065)
DECK = (0.17, 0.12, 0.08)
FRAME_GREY = (0.11, 0.115, 0.12)
BLOCK = (0.4, 0.39, 0.36)

TOUR_TOP = 2.6        # a tourer's roof (the roof light stands on it): the same for both, so neither is scaled
TOUR_FLOOR = 0.42     # its body's underside
STATIC_TOP = 3.42     # a static's ridge
STATIC_FLOOR = 0.62   # its floor's underside (on its blocks)


# --- geometry ---------------------------------------------------------------------------------------------

def rounded(poly, radii, seg=4):
    """A closed polygon [(a, b)] with each corner rounded by its radius (0 keeps it sharp)."""
    out = []
    n = len(poly)
    for i in range(n):
        p = np.array(poly[i], float)
        r = radii[i] if isinstance(radii, (list, tuple)) else radii
        if r <= 0:
            out.append(tuple(p))
            continue
        a = np.array(poly[i - 1], float); b = np.array(poly[(i + 1) % n], float)
        da = (a - p) / np.linalg.norm(a - p); db = (b - p) / np.linalg.norm(b - p)
        half = math.acos(float(np.clip(np.dot(da, db), -1, 1))) / 2
        t = r / math.tan(half)
        t = min(t, np.linalg.norm(a - p) * 0.45, np.linalg.norm(b - p) * 0.45)
        s, e = p + da * t, p + db * t
        # (a quadratic curve corner: from s through near p to e)
        for k in range(seg + 1):
            u = k / seg
            q = (1 - u) ** 2 * s + 2 * (1 - u) * u * p + u * u * e
            out.append(tuple(q))
    return out


def prism(name, profile, axis, lo, hi, bevel=0.0, sharp_deg=35.0):
    """A solid of a closed profile drawn in the plane across `axis` ('x' or 'y'), from lo to hi along it.
    The profile's points are (along the other horizontal, z) for axis 'x': (y, z); for 'y': (x, z)."""
    n = len(profile)
    verts = []
    for w in (lo, hi):
        for a, z in profile:
            verts.append((w, a, z) if axis == "x" else (a, w, z))
    faces = [tuple(range(n)), tuple(range(2 * n - 1, n - 1, -1))]
    faces += [(k, (k + 1) % n, n + (k + 1) % n, n + k) for k in range(n)]
    ob = K._object(name, verts, faces, sharp_deg)
    if bevel > 0:
        m = ob.modifiers.new("bevel", "BEVEL")
        m.width = bevel
        m.segments = 2
        m.limit_method = "ANGLE"
        m.angle_limit = math.radians(50)
        m.harden_normals = True
    return ob


def panel(name, side, w0, w1, z0, z1, at, thick=0.012, r=0.0, axis="y"):
    """A flat panel on a wall: a window, a door, a stripe. axis 'y' is a wall facing +-y (the panel spans x
    from w0 to w1); 'x' a wall facing +-x (spans y). side +1 / -1 is the way it faces, at the wall's place."""
    prof = rounded([(w0, z0), (w1, z0), (w1, z1), (w0, z1)], r, 3) if r > 0 else [(w0, z0), (w1, z0), (w1, z1), (w0, z1)]
    lo, hi = (at, at + side * thick) if side > 0 else (at + side * thick, at)
    return prism(name, prof, "x" if axis == "x" else "y", lo, hi)


def wheel(M, x, y, r=0.3, w=0.17, side=1):
    """A caravan's road wheel turned to stand on the ground at (x, y): its tyre and its silver steel hub."""
    tyre = K.lathe("tyre", [(r * 0.62, -w / 2), (r * 0.93, -w / 2), (r, -w / 2 + 0.03), (r, w / 2 - 0.03), (r * 0.93, w / 2), (r * 0.62, w / 2)], 18)
    tyre.matrix_world = Matrix.Translation((x, y, r)) @ Matrix.Rotation(math.pi / 2, 4, "X")
    M.add(tyre, "paint", TYRE, 0.5)
    hub = K.lathe("hub", [(r * 0.63, -w / 2 + 0.01), (r * 0.63, w / 2 - 0.02), (r * 0.3, w / 2 - 0.005), (0.0, w / 2 - 0.01)], 14)
    hub.matrix_world = Matrix.Translation((x, y, r)) @ Matrix.Rotation(-side * math.pi / 2, 4, "X")
    M.add(hub, "steel", HUB, 0.3)


# --- the touring caravans ---------------------------------------------------------------------------------

def tourer(name, length, axles, stripes, band=None, seed=1):
    """A touring caravan: the body a rounded side profile across its 2.26 m width, the front sloping back to a
    wide window, the door on the nearside (+y) behind the axle; on its chassis, wheels in black arches,
    the A-frame to a hitch at +x with the jockey wheel, the gas locker lid, four corner steadies down."""
    M = K.Model(name)
    W = 2.26
    L = length
    xr, xf = -L / 2, L / 2
    zb, zt = TOUR_FLOOR, TOUR_TOP
    # (the side profile: rear wall, the roof, the curved front cap down its window to the locker and the floor)
    prof = rounded([(xr, zb), (xf - 0.12, zb), (xf, zb + 0.32), (xf, zb + 0.95), (xf - 0.42, zt - 0.06), (xf - 0.75, zt),
                    (xr + 0.2, zt), (xr, zt - 0.18)], [0.04, 0.08, 0.1, 0.25, 0.25, 0.25, 0.18, 0.06], 5)
    M.add(prism("body", prof, "y", -W / 2, W / 2, bevel=0.05), "paint", VAN_WHITE, 0.15)
    # (the front window: a band across the sloping cap, inset from its sides)
    a, b = np.array((xf, zb + 1.02)), np.array((xf - 0.38, zt - 0.14))
    d = (b - a) / np.linalg.norm(b - a)
    nrm = np.array((d[1], -d[0]))   # (out of the slope)
    if nrm[0] < 0:
        nrm = -nrm
    win = [tuple(a + nrm * 0.004), tuple(b + nrm * 0.004), tuple(b + nrm * 0.02), tuple(a + nrm * 0.02)]
    M.add(prism("frontwin", win, "y", -W / 2 + 0.22, W / 2 - 0.22), "lens", GLASS, 0.05)
    # (the gas locker lid on the front, below the window)
    M.add(panel("locker", 1, -0.55, 0.55, zb + 0.4, zb + 0.82, xf + 0.003, 0.014, 0.05, axis="x"), "enamel", PLASTIC_GREY, 0.35)
    # (the side windows: tinted acrylic in black frames; the lounge's at the front, the kitchen's, the end
    # bedroom's at the rear; on the nearside the door between)
    wins = [(xf - 1.45, xf - 0.62), (xr + 0.35, xr + 1.3)]
    if L > 6.0:
        wins.append((-0.6, 0.35))
    for s in (1, -1):
        for w0, w1 in wins:
            M.add(panel("winframe", s, w0 - 0.04, w1 + 0.04, zb + 1.06, zb + 1.74, s * W / 2, 0.01, 0.07), "enamel", TRIM, 0.1)
            M.add(panel("win", s, w0, w1, zb + 1.1, zb + 1.7, s * (W / 2 + 0.01), 0.008, 0.05), "lens", GLASS, 0.05)
        if s < 0 and L <= 6.0:   # (the offside's kitchen window; the long one has its middle window there)
            M.add(panel("winframe", s, -0.19, 0.64, zb + 1.06, zb + 1.74, s * W / 2, 0.01, 0.07), "enamel", TRIM, 0.1)
            M.add(panel("win", s, -0.15, 0.6, zb + 1.1, zb + 1.7, s * (W / 2 + 0.01), 0.008, 0.05), "lens", GLASS, 0.05)
    # (the door: its outline, a window in its top, a handle)
    dx0 = xr + 1.45
    M.add(panel("doorline", 1, dx0 - 0.02, dx0 + 0.66, zb + 0.06, zt - 0.28, W / 2, 0.006, 0.05), "enamel", TRIM, 0.1)
    M.add(panel("door", 1, dx0, dx0 + 0.64, zb + 0.08, zt - 0.3, W / 2 + 0.006, 0.006, 0.04), "paint", VAN_WHITE, 0.15)
    M.add(panel("doorwin", 1, dx0 + 0.1, dx0 + 0.54, zb + 1.2, zt - 0.45, W / 2 + 0.012, 0.006, 0.05), "lens", GLASS, 0.05)
    M.add(K.box("handle", (dx0 + 0.56, W / 2 + 0.02, zb + 1.0), (0.12, 0.025, 0.035)), "steel", HUB, 0.2)
    # (the stripes, the length of each side; the skirt band where it has one)
    for s in (1, -1):
        y = s * W / 2
        if band:
            M.add(panel("band", s, xr + 0.03, xf - 0.14, zb + 0.06, zb + 0.5, y, 0.004), "paint", band, 0.25)
        for z0, z1, rgb in stripes:
            M.add(panel("stripe", s, xr + 0.03, xf - 0.18, zb + z0, zb + z1, y + s * (0.005 if band else 0.0), 0.004), "paint", rgb, 0.15)
    # (the axles' wheels in their arches)
    xs = [-0.25] if axles == 1 else [-0.62, 0.2]
    for xa in xs:
        for s in (1, -1):
            lo, hi = (W / 2 - 0.005, W / 2 + 0.012) if s > 0 else (-W / 2 - 0.012, -W / 2 + 0.005)
            arch = prism("arch", rounded([(xa - 0.42, zb - 0.02), (xa + 0.42, zb - 0.02), (xa + 0.42, zb + 0.16), (xa, zb + 0.34), (xa - 0.42, zb + 0.16)], [0, 0, 0.15, 0.3, 0.15], 4), "y", lo, hi)
            M.add(arch, "enamel", TRIM, 0.4)
            wheel(M, xa, s * (W / 2 - 0.13), side=s)
    # (the chassis rails and the A-frame to the hitch, the jockey wheel down, the breakaway cable)
    for y in (-0.62, 0.62):
        M.add(K.box("rail", ((xr + xf) / 2 + 0.2, y, zb - 0.07), (L - 0.4, 0.05, 0.14)), "galv", GALV, 0.45)
    hitch = Vector((xf + 1.12, 0, 0.44))
    for y in (-0.62, 0.62):
        M.add(K.sweep("aframe", [Vector((xf - 0.3, y, zb - 0.06)), hitch + Vector((-0.12, 0, 0))], 0.05, 6, square=True), "galv", GALV, 0.45)
    M.add(K.box("hitch", (hitch.x + 0.02, 0, hitch.z), (0.32, 0.12, 0.13), bevel=0.02), "paint", TRIM, 0.4)
    M.add(K.sweep("handle", [hitch + Vector((-0.08, 0, 0.06)), hitch + Vector((-0.02, 0, 0.18)), hitch + Vector((0.18, 0, 0.12))], 0.018, 6), "paint", TRIM, 0.3)
    jx = xf + 0.65
    M.add(K.lathe("jockey", [(0.035, 0.12), (0.035, 0.78), (0.045, 0.8), (0.0, 0.82)], 10, at=(jx, 0.2, 0.0)), "galv", GALV, 0.4)
    jw = K.lathe("jwheel", [(0.0, -0.03), (0.1, -0.03), (0.1, 0.03), (0.0, 0.03)], 12)
    jw.matrix_world = Matrix.Translation((jx, 0.2, 0.1)) @ Matrix.Rotation(math.pi / 2, 4, "X")
    M.add(jw, "paint", TYRE, 0.5)
    M.add(K.box("jhandle", (jx, 0.2, 0.84), (0.2, 0.03, 0.03)), "paint", TRIM, 0.3)
    # (the corner steadies, wound down to the grass)
    for x in (xr + 0.25, xf - 0.4):
        for y in (-0.62, 0.62):
            M.add(K.sweep("steady", [Vector((x, y, zb - 0.1)), Vector((x + (0.3 if x < 0 else -0.3), y, 0.04))], 0.025, 4, square=True), "galv", GALV, 0.5)
            M.add(K.box("pad", (x + (0.3 if x < 0 else -0.3), y, 0.02), (0.14, 0.12, 0.04)), "galv", GALV, 0.7)
    # (the rear: its window, the light clusters either side, the bumper and a blank plate)
    M.add(panel("rearwin", -1, -0.7, 0.7, zb + 1.2, zb + 1.75, xr, 0.01, 0.08, axis="x"), "lens", GLASS, 0.05)
    M.add(K.box("bumper", (xr - 0.03, 0, zb + 0.12), (0.08, W - 0.1, 0.16), bevel=0.02), "enamel", TRIM, 0.35)
    for s in (1, -1):
        M.add(panel("taillight", -1, s * 0.78 - 0.18, s * 0.78 + 0.18, zb + 0.32, zb + 0.62, xr, 0.02, 0.03, axis="x"), "lens", RED_LENS, 0.1)
        M.add(panel("indicator", -1, s * 0.78 - 0.18, s * 0.78 + 0.18, zb + 0.62, zb + 0.72, xr, 0.02, 0.02, axis="x"), "lens", AMBER_LENS, 0.1)
        M.add(panel("marker", s, xf - 0.35, xf - 0.25, zt - 0.2, zt - 0.14, s * W / 2, 0.015, 0.02), "lens", AMBER_LENS, 0.1)
    M.add(panel("plate", -1, -0.26, 0.26, zb + 0.36, zb + 0.47, xr, 0.012, 0.0, axis="x"), "paint", (0.7, 0.55, 0.02), 0.2)
    # (on the roof: the roof light over the lounge and two vents)
    rl = prism("rooflight", rounded([(-0.38, zt - 0.02), (0.38, zt - 0.02), (0.3, zt + 0.12), (-0.3, zt + 0.12)], [0, 0, 0.06, 0.06], 3), "x", xf - 1.6, xf - 0.85)
    M.add(rl, "lens", ROOF_LIGHT, 0.2)
    for x in (xr + 0.8, -0.2):
        M.add(K.box("vent", (x, 0, zt + 0.03), (0.36, 0.36, 0.08), bevel=0.02), "enamel", PLASTIC_GREY, 0.3)
    return M


# --- the statics ------------------------------------------------------------------------------------------

def static_shell(M, L, W, top_body, ridge, body_rgb, roof_rgb, eave=0.12, cedar=False):
    """A static's body box on its blocks with skirting round them, and its pitched roof (gable ends to x)."""
    zf = STATIC_FLOOR
    M.add(K.box("body", (0, 0, (zf + top_body) / 2), (L, W, top_body - zf), bevel=0.03), "timber" if cedar else "paint", body_rgb, 0.2)
    # (the skirting, set in a little, and the blocks behind it)
    M.add(K.box("skirt", (0, 0, (zf + 0.02) / 2), (L - 0.12, W - 0.12, zf + 0.02)), "paint", SKIRT, 0.55)
    # (the roof: a gable prism along x, its eaves out over the walls; the gables filled in the body's colour)
    prof = [(-W / 2 - eave, top_body - 0.04), (W / 2 + eave, top_body - 0.04), (W / 2 + eave, top_body), (0.0, ridge), (-W / 2 - eave, top_body)]
    M.add(prism("roof", prof, "x", -L / 2 - eave * 0.6, L / 2 + eave * 0.6), "concrete", roof_rgb, 0.35)   # (matt: felt, coated steel)
    gable = [(-W / 2, top_body - 0.05), (W / 2, top_body - 0.05), (0.0, ridge - 0.05)]
    M.add(prism("gables", gable, "x", -L / 2, L / 2), "timber" if cedar else "paint", body_rgb, 0.2)
    # (the gutters along the eaves)
    for s in (1, -1):
        M.add(K.box("gutter", (0, s * (W / 2 + eave - 0.03), top_body - 0.06), (L + eave, 0.07, 0.07)), "enamel", TRIM if not cedar else FRAME_GREY, 0.3)


def window(M, s, x0, x1, z0, z1, wall, frame=PLATE_WHITE, axis="y"):
    """A double-glazed window: its frame, the glass a little proud of it, a mullion across the wide ones."""
    M.add(panel("frame", s, x0 - 0.05, x1 + 0.05, z0 - 0.05, z1 + 0.05, wall, 0.025, 0.0, axis), "enamel", frame, 0.15)
    M.add(panel("glass", s, x0, x1, z0, z1, wall + s * 0.025, 0.006, 0.0, axis), "lens", GLASS, 0.05)
    if x1 - x0 > 1.3:
        xm = (x0 + x1) / 2
        M.add(panel("mullion", s, xm - 0.03, xm + 0.03, z0, z1, wall + s * 0.03, 0.01, 0.0, axis), "enamel", frame, 0.15)


def steps(M, x, y_wall, s, rows=3, width=1.0, rgb=DECK, rail=True):
    """Timber steps from a door's sill down to the grass, out from the wall at y_wall the way s, a handrail."""
    rise = STATIC_FLOOR / rows
    for r in range(rows):
        d = 0.3 * (rows - r)
        M.add(K.box("step", (x, y_wall + s * d / 2, rise * (r + 0.5)), (width, d, rise)), "timber", rgb, 0.45)
    if rail:
        top = Vector((x + width / 2, y_wall + s * 0.05, STATIC_FLOOR + 0.9))
        foot = Vector((x + width / 2, y_wall + s * (0.3 * rows + 0.05), 0.95))
        M.add(K.sweep("rail", [foot, top], 0.022, 6), "galv", GALV, 0.3)
        M.add(K.rod("post", foot, Vector((foot.x, foot.y, 0.0)), 0.022), "galv", GALV, 0.4)


def static_classic():
    """The classic holiday static: 11 m by 3.7 m, cream, a shallow roof, the lounge's wrap-round windows at -x,
    a green trim stripe, white uPVC windows and door, timber steps."""
    M = K.Model("SM_Prop_static_caravan_0")
    L, W, top = 11.0, 3.7, 3.08
    static_shell(M, L, W, top, STATIC_TOP, CREAM, ROOF_GREY)
    zf = STATIC_FLOOR
    for s in (1, -1):
        M.add(panel("stripe", s, -L / 2 + 0.03, L / 2 - 0.03, zf + 0.62, zf + 0.72, s * W / 2, 0.006), "paint", STRIPE_GREEN, 0.2)
        M.add(panel("stripe2", s, -L / 2 + 0.03, L / 2 - 0.03, top - 0.25, top - 0.2, s * W / 2, 0.006), "paint", STRIPE_GREEN, 0.2)
        # (the lounge's corner window, round the end; the kitchen's; the bedrooms')
        window(M, s, -L / 2 + 0.25, -L / 2 + 1.6, zf + 0.95, zf + 2.0, s * W / 2)
        for x0, x1 in ((-2.4, -1.5), (1.4, 2.1), (3.4, 4.6)):
            if s > 0 and x0 == -2.4:
                continue
            window(M, s, x0, x1, zf + 1.25, zf + 2.0, s * W / 2)
    window(M, -1, -1.5, 1.5, zf + 0.95, zf + 2.0, -L / 2, axis="x")
    window(M, 1, -0.6, 0.6, zf + 1.3, zf + 2.0, L / 2, axis="x")
    # (the door on the +y side, halfway along, and its steps)
    M.add(panel("door", 1, -2.3, -1.45, 0.02 + zf, zf + 2.05, W / 2, 0.03), "enamel", PLATE_WHITE, 0.2)
    M.add(panel("doorglass", 1, -2.15, -1.6, zf + 1.2, zf + 1.9, W / 2 + 0.03, 0.006), "lens", GLASS, 0.05)
    steps(M, -1.875, W / 2, 1, 3, 1.0, DECK)
    # (the gas bottles' box at the far end, and the meter)
    M.add(K.box("gasbox", (L / 2 - 1.0, W / 2 + 0.3, 0.55), (0.8, 0.5, 1.1)), "enamel", PLASTIC_GREY, 0.5)
    return M


def static_modern():
    """A modern static: 12 m by 3.9 m, charcoal below a pale grey upper, a grey roof, big windows in dark grey
    frames, patio doors to a deck of two steps, a window wrapping the lounge end."""
    M = K.Model("SM_Prop_static_caravan_1")
    L, W, top = 12.0, 3.9, 3.0
    static_shell(M, L, W, top, STATIC_TOP, PALE_GREY, ROOF_GREY)
    zf = STATIC_FLOOR
    for s in (1, -1):
        M.add(panel("lower", s, -L / 2 + 0.03, L / 2 - 0.03, zf + 0.02, zf + 0.85, s * W / 2, 0.008), "paint", CHARCOAL, 0.3)
        for x0, x1 in ((-L / 2 + 0.3, -L / 2 + 2.3), (1.0, 1.8), (3.6, 5.0)):
            window(M, s, x0, x1, zf + 0.95, zf + 2.05, s * W / 2 + 0.008, FRAME_GREY)
    window(M, -1, -1.6, 1.6, zf + 0.6, zf + 2.05, -L / 2, FRAME_GREY, axis="x")
    window(M, 1, -0.7, 0.7, zf + 1.2, zf + 2.0, L / 2, FRAME_GREY, axis="x")
    # (the patio doors on +y and the deck before them)
    M.add(panel("patio", 1, -2.9, -1.3, zf + 0.04, zf + 2.1, W / 2 + 0.008, 0.03), "enamel", FRAME_GREY, 0.2)
    for x0, x1 in ((-2.85, -2.12), (-2.08, -1.35)):
        M.add(panel("patioglass", 1, x0, x1, zf + 0.1, zf + 2.04, W / 2 + 0.038, 0.006), "lens", GLASS, 0.05)
    M.add(K.box("deck", (-2.1, W / 2 + 0.6, zf - 0.08), (2.6, 1.2, 0.08)), "timber", DECK, 0.35)
    for x in (-3.3, -0.9):
        M.add(K.box("deckleg", (x, W / 2 + 1.1, (zf - 0.12) / 2), (0.1, 0.1, zf - 0.12)), "timber", DECK, 0.6)
    M.add(K.box("step", (-2.1, W / 2 + 1.35, zf * 0.33), (1.2, 0.3, zf * 0.66)), "timber", DECK, 0.5)
    M.add(K.box("gasbox", (L / 2 - 1.0, W / 2 + 0.3, 0.55), (0.8, 0.5, 1.1)), "enamel", PLASTIC_GREY, 0.5)
    return M


def static_lodge():
    """A lodge: 12 m by 4 m, timber clad in boards, a steeper near-black roof with deep eaves, tall glazing at the
    -x end, glazed double doors on to a deck the length of its +y side behind a rail."""
    M = K.Model("SM_Prop_static_caravan_2")
    L, W, top = 12.0, 4.0, 2.78
    static_shell(M, L, W, top, STATIC_TOP, CEDAR, LODGE_ROOF, eave=0.35, cedar=True)
    zf = STATIC_FLOOR
    # (the boards: shadow lines every 15 cm up the walls)
    for s in (1, -1):
        for k in range(1, 14):
            z = zf + k * 0.15
            M.add(panel("board", s, -L / 2 + 0.03, L / 2 - 0.03, z, z + 0.012, s * W / 2 - s * 0.004, 0.006), "timber", (0.1, 0.06, 0.035), 0.3)
    for k in range(1, 12):
        z = zf + k * 0.15
        for s in (1, -1):
            M.add(panel("board", s, -W / 2 + 0.03, W / 2 - 0.03, z, z + 0.012, s * L / 2 - s * 0.004, 0.006, axis="x"), "timber", (0.1, 0.06, 0.035), 0.3)
    # (the windows in dark grey aluminium; the end wall mostly glass)
    window(M, -1, -1.7, 1.7, zf + 0.2, zf + 2.05, -L / 2 - 0.004, FRAME_GREY, axis="x")
    window(M, 1, -0.6, 0.6, zf + 1.1, zf + 1.9, L / 2 + 0.004, FRAME_GREY, axis="x")
    for x0, x1 in ((-L / 2 + 0.5, -L / 2 + 2.0), (1.6, 2.4), (3.8, 5.0)):
        window(M, -1, x0, x1, zf + 0.9, zf + 2.0, -W / 2 - 0.004, FRAME_GREY)
    window(M, 1, 2.2, 3.0, zf + 1.1, zf + 2.0, W / 2 + 0.004, FRAME_GREY)
    window(M, 1, -L / 2 + 0.5, -L / 2 + 1.7, zf + 0.9, zf + 2.0, W / 2 + 0.004, FRAME_GREY)
    M.add(panel("doors", 1, -2.6, -0.8, zf + 0.04, zf + 2.15, W / 2 + 0.004, 0.035), "enamel", FRAME_GREY, 0.2)
    for x0, x1 in ((-2.55, -1.73), (-1.67, -0.85)):
        M.add(panel("doorglass", 1, x0, x1, zf + 0.1, zf + 2.09, W / 2 + 0.04, 0.006), "lens", GLASS, 0.05)
    # (the deck: boards on joists the length of the side, a rail round it, steps down at its -x end)
    dw = 1.7
    M.add(K.box("deck", (-0.5, W / 2 + dw / 2, zf - 0.05), (L - 2.0, dw, 0.07)), "timber", DECK, 0.35)
    M.add(K.box("deckface", (-0.5, W / 2 + dw - 0.02, zf / 2 - 0.05), (L - 2.0, 0.04, zf - 0.1)), "timber", DECK, 0.5)
    x0, x1 = -0.5 - (L - 2.0) / 2, -0.5 + (L - 2.0) / 2
    yr = W / 2 + dw - 0.05
    for a, b in (((x0 + 1.2, yr), (x1, yr)), ((x1, yr), (x1, W / 2))):
        A, B = Vector((a[0], a[1], zf + 0.95)), Vector((b[0], b[1], zf + 0.95))
        M.add(K.sweep("handrail", [A, B], 0.035, 4, square=True), "timber", DECK, 0.3)
        Am, Bm = Vector((a[0], a[1], zf + 0.45)), Vector((b[0], b[1], zf + 0.45))
        M.add(K.sweep("midrail", [Am, Bm], 0.025, 4, square=True), "timber", DECK, 0.35)
        n = max(2, int((B - A).length / 1.4))
        for k in range(n + 1):
            P = A + (B - A) * (k / n)
            M.add(K.box("post", (P.x, P.y, (zf + 0.95) / 2), (0.09, 0.09, zf + 0.95)), "timber", DECK, 0.45)
    for r in range(3):
        d = 0.3 * (3 - r)
        M.add(K.box("step", (x0 + 0.6, W / 2 + dw + d / 2 - 0.02, STATIC_FLOOR / 3 * (r + 0.5)), (1.0, d, STATIC_FLOOR / 3)), "timber", DECK, 0.5)
    return M


def build():
    K.clear()
    return [tourer("SM_Prop_touring_caravan_0", 5.0, 1, [(0.55, 0.62, STRIPE_GREY), (0.66, 0.69, STRIPE_RED)]),
            tourer("SM_Prop_touring_caravan_1", 6.6, 2, [(0.56, 0.64, STRIPE_BRONZE), (0.68, 0.7, STRIPE_BRONZE)], band=SILVER, seed=2),
            static_classic(), static_modern(), static_lodge()]


REACH = {"touring_caravan": 450, "static_caravan": 650}
SHADOW = {"touring_caravan": True, "static_caravan": True}


def main():
    models = build()
    heights = {}
    for M in models:
        nt, y0, y1, x0, x1, z0, z1 = K.write(M)
        kind = M.name[len("SM_Prop_"):].rsplit("_", 1)[0]
        heights.setdefault(kind, []).append((y0, y1))
        print("%s: %d triangles, %.3f..%.3f m up, x %.2f..%.2f, z %.2f..%.2f" % (M.name, nt, y0, y1, x0, x1, z0, z1))
    lines = open(K.KIT).read().split("\n")
    for kind, spans in heights.items():
        tall = [y1 - y0 for y0, y1 in spans]
        if max(tall) - min(tall) > 0.02:
            raise SystemExit("%s: its models are of different heights (%s): they would be scaled to fit" % (kind, tall))
        if min(y0 for y0, _ in spans) < -0.01:
            raise SystemExit("%s: a model reaches below its foot" % kind)
        # (lods: Tools/ue_build_rideuk.py prop_kit makes the model's LODs on import, at these screen sizes)
        # (no "lods": build 166 showed UE's reduced LODs of these cut holes in them; drawn whole, they are small)
        row = {"words": [], "reach": REACH[kind], "height": round(float(max(tall)), 3), "shadow": SHADOW[kind], "credit": ""}
        head = '  "%s": ' % kind
        for i, ln in enumerate(lines):
            if ln.startswith(head):
                comma = ln.rstrip().endswith(",")
                lines[i] = head + json.dumps(row) + ("," if comma else "")
                break
        else:
            at = next(i for i, ln in enumerate(lines) if ln.startswith('  "grandstand": '))
            lines.insert(at + 1, head + json.dumps(row) + ",")
    open(K.KIT, "w").write("\n".join(lines))
    json.load(open(K.KIT))
    return models


def render_sheet(models, out_dir):
    from blender_street_kit_render import _scene, _shoot
    from PIL import Image, ImageDraw, ImageFont
    from make_street_kit_textures import SURFACES
    os.makedirs(out_dir, exist_ok=True)
    _scene(models, K.TEX, SURFACES)
    sc = bpy.context.scene
    by = {M.name: M for M in models}
    shots = [
        ("SM_Prop_touring_caravan_0", "Tourer, single axle (front)", (0.6, 0, 1.3), 10.0, 40, 12, 50),
        ("SM_Prop_touring_caravan_0", "Tourer, single axle (rear)", (0, 0, 1.3), 10.0, 220, 12, 50),
        ("SM_Prop_touring_caravan_1", "Tourer, twin axle (door side)", (0.4, 0, 1.3), 12.0, 70, 10, 50),
        ("SM_Prop_touring_caravan_1", "Tourer, twin axle (offside)", (0.4, 0, 1.3), 12.0, -60, 14, 50),
        ("SM_Prop_static_caravan_0", "Static, classic", (0, 0.5, 1.7), 20.0, 55, 12, 50),
        ("SM_Prop_static_caravan_1", "Static, modern", (0, 0.5, 1.7), 20.0, 60, 12, 50),
        ("SM_Prop_static_caravan_2", "Lodge", (0, 0.8, 1.7), 21.0, 55, 14, 50),
        ("SM_Prop_static_caravan_2", "Lodge (glazed end)", (-2, 0, 1.7), 20.0, 200, 10, 50),
    ]
    files = []
    for i, (name, label, target, dist, yaw, pitch, lens) in enumerate(shots):
        f = os.path.join(out_dir, "caravan_%02d.png" % i)
        _shoot(sc, [p.obj for p in by[name].parts], target, dist, yaw, pitch, lens, (560, 400), f)
        files.append((f, label))
    font = None
    for cand in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf"):
        if os.path.exists(cand):
            font = ImageFont.truetype(cand, 18)
    pad, cols, w, h = 10, 4, 560, 400
    W = cols * w + (cols + 1) * pad
    H = 50 + 2 * (h + 28 + pad)
    sheet = Image.new("RGB", (W, H), (245, 245, 242))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 12), "Ride UK caravans (Blender renders, the street kit's material; no makers' names or badges)", fill=(20, 20, 20), font=font)
    for i, (f, label) in enumerate(files):
        im = Image.open(f).convert("RGB")
        x = pad + (i % cols) * (w + pad)
        y = 50 + (i // cols) * (h + 28 + pad)
        sheet.paste(im, (x, y))
        d.text((x, y + h + 2), label, fill=(30, 30, 30), font=font)
    path = os.path.join(out_dir, "caravan_kit_review.png")
    sheet.save(path)
    print("review sheet:", path)
    return path


if __name__ == "__main__":
    ms = main()
    if "--render" in sys.argv:
        render_sheet(ms, sys.argv[sys.argv.index("--render") + 1])
