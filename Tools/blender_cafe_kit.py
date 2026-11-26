"""Ride UK's roadside cafes (beenster Nov 26: "Please can you build about 20 different cafes, from small to large. I also
want to be able to sit at tables outside them"), modelled in Blender. Our own designs from general knowledge of British
roadside cafes, tea rooms and transport cafes: no chains' names, logos or liveries. The game sets each cafe's made-up
name on its fascia in the sign alphabet (World/WorldRideCafes.cpp).

  cafe_building 0..19   the cafes, front (door, fascia, terrace side) to +x, centred on their box, feet at 0 with a
                        0.5 m footing below ground (the model's own: no drop), smallest to largest:
                          0 timber tea hut           1 green park kiosk          2 container coffee bar
                          3 log cabin snack bar      4 curved-roof kiosk         5 railway carriage cafe
                          6 Dales stone tea room     7 thatched cottage tea room 8 1950s brick transport cafe
                          9 timber-clad modern cafe 10 glass garden cafe        11 black barn cafe
                         12 tile-hung greasy spoon  13 seaside chalet cafe      14 octagonal pavilion
                         15 biker cafe with veranda 16 chrome diner             17 stone coaching roadhouse
                         18 truck stop cafe         19 white 1930s roadhouse
  cafe_chair 0..3       outdoor chairs (sitter faces +x, seat 0.46 m up at the origin): black metal bistro, slatted
                        timber, green metal, white rattan weave
  cafe_table 0..2       outdoor tables 0.74 m: round black bistro, square timber, round white
  cafe_picnic 0..1      a picnic bench set along y (benches at x = +-0.62, seats 0.45 m): timber, green metal frame
  cafe_parasol 0..3     a parasol over a table: cream, bottle green, red, navy
  cafe_aboard 0..1      a chalk A-board by the door

Every model has two hand-made distance versions (_LOD1, _LOD2). The models of a kind can differ in height: the game
draws each at its own size (Data/RideUK/cafe_kit.json lists every design's size, height, fascia and door; the game
sets the prop's scale from it). Same material and frame as the street kit (Tools/blender_street_kit.py).

    python Tools/blender_cafe_kit.py                # the models, cafe_kit.json, the prop kit rows
    python Tools/blender_cafe_kit.py --render DIR   # and the review sheets in DIR
"""
import json
import math
import os
import sys

import bpy  # noqa: F401  (before mathutils)
from mathutils import Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_street_kit as K   # noqa: E402
from blender_street_kit import box, lathe, rod, sweep   # noqa: E402

FOOT = 0.5   # (the footing below ground: the kind's drop)

# (linear colours)
GLASS = (0.04, 0.055, 0.065)
FRAME_W = (0.72, 0.72, 0.7)
FRAME_D = (0.06, 0.065, 0.07)
CONCRETE = (0.36, 0.35, 0.32)
SLATE = (0.07, 0.075, 0.085)
TILE_RED = (0.3, 0.09, 0.05)
FELT = (0.04, 0.04, 0.045)
THATCH = (0.36, 0.27, 0.13)
STONE = (0.36, 0.33, 0.27)
STONE_DARK = (0.24, 0.22, 0.19)
BRICK = (0.33, 0.12, 0.07)
BRICK_YEL = (0.5, 0.38, 0.22)
RENDER_W = (0.78, 0.77, 0.72)
CREAM = (0.72, 0.64, 0.46)
TIMBER = (0.33, 0.2, 0.1)
TIMBER_PALE = (0.5, 0.36, 0.2)
LOGS = (0.28, 0.16, 0.07)
BLACKBOARD = (0.025, 0.025, 0.028)
GREEN = (0.03, 0.15, 0.07)
GREEN_PALE = (0.25, 0.42, 0.3)
RED = (0.42, 0.04, 0.03)
NAVY = (0.02, 0.04, 0.13)
TEAL = (0.03, 0.22, 0.22)
STEEL = (0.45, 0.45, 0.44)
CHROME = (0.7, 0.7, 0.7)
WHITE = (0.75, 0.75, 0.72)
DARK = (0.03, 0.03, 0.035)
CHALK = (0.6, 0.6, 0.58)

# kind: (reach m, casts a shadow, hand-made LODs' screen sizes)
KINDS = {"cafe_building": (1200, True, [0.25, 0.06]), "cafe_chair": (120, False, [0.1, 0.03]),
         "cafe_table": (150, True, [0.1, 0.03]), "cafe_picnic": (180, True, [0.12, 0.03]),
         "cafe_parasol": (300, True, [0.12, 0.03]), "cafe_aboard": (120, False, [0.1, 0.03])}


# --- pieces ----------------------------------------------------------------------------------------------------------

def prism(name, pts, y0, y1):
    """A solid from the polygon pts [(x, z)] (anticlockwise seen from -y) swept from y0 to y1."""
    n = len(pts)
    verts = [(x, y0, z) for x, z in pts] + [(x, y1, z) for x, z in pts]
    faces = [tuple(range(n))[::-1], tuple(range(n, 2 * n))]
    faces += [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    return K._object(name, verts, faces, 30.0)


def prism_x(name, pts, x0, x1):
    """A solid from the polygon pts [(y, z)] swept along x from x0 to x1."""
    n = len(pts)
    verts = [(x0, y, z) for y, z in pts] + [(x1, y, z) for y, z in pts]
    faces = [tuple(range(n))[::-1], tuple(range(n, 2 * n))]
    faces += [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    return K._object(name, verts, faces, 30.0)


GUTTER = (0.05, 0.05, 0.055)
DETAIL = [0]   # (the lod being built: the roofs' gutters, ridges and bargeboards are the near model's alone)


def gutters_y(M, D, W, ze, xe, length, pipes=True):
    """Half-round gutters along y at x = +-xe, their top at ze, and a downpipe at each end of each to the ground."""
    if DETAIL[0] > 0:
        return
    for s in (-1, 1):
        M.add(box("gutter", (s * (xe + 0.06), 0.0, ze - 0.06), (0.12, length, 0.1)), "paint", GUTTER, 0.3)
        if pipes:
            for e in (-1, 1):
                y = e * (W / 2 - 0.12)
                M.add(box("downpipe", (s * (D / 2 + 0.06), y, (ze - 0.1) / 2), (0.07, 0.07, ze - 0.1)), "paint", GUTTER, 0.3)
                M.add(box("swan", (s * (D / 2 + xe) / 2 + s * 0.06, y, ze - 0.12), (abs(xe - D / 2) + 0.07, 0.07, 0.07)), "paint", GUTTER, 0.3)


def gutters_x(M, D, W, ze, ye, length, pipes=True):
    if DETAIL[0] > 0:
        return
    for s in (-1, 1):
        M.add(box("gutter", (0.0, s * (ye + 0.06), ze - 0.06), (length, 0.12, 0.1)), "paint", GUTTER, 0.3)
        if pipes:
            for e in (-1, 1):
                x = e * (D / 2 - 0.12)
                M.add(box("downpipe", (x, s * (W / 2 + 0.06), (ze - 0.1) / 2), (0.07, 0.07, ze - 0.1)), "paint", GUTTER, 0.3)
                M.add(box("swan", (x, s * (W / 2 + ye) / 2 + s * 0.06, ze - 0.12), (0.07, abs(ye - W / 2) + 0.07, 0.07)), "paint", GUTTER, 0.3)


def footing(M, D, W):
    M.add(box("footing", (0.0, 0.0, (0.2 - FOOT) / 2), (D + 0.1, W + 0.1, 0.2 + FOOT)), "concrete", CONCRETE, 0.5)


def gable_roof(M, D, W, z0, rise, over, surface, rgb, ridge_y=True, thick=0.18):
    """A pitched roof on a D x W box at eaves height z0: the ridge along y (the slopes to +x and -x, gables at the sides)
    or along x (the gable to the front). Returns the ridge's height."""
    if ridge_y:
        h = D / 2 + over
        pts = [(-h, z0 - over * rise / (D / 2)), (h, z0 - over * rise / (D / 2)), (h, z0 - over * rise / (D / 2) + thick),
               (0.0, z0 + rise + thick), (-h, z0 - over * rise / (D / 2) + thick)]
        M.add(prism("roof", pts, -W / 2 - over, W / 2 + over), surface, rgb, 0.3)
        M.add(prism("gable", [(-D / 2, z0), (D / 2, z0), (0.0, z0 + rise)], -W / 2 + 0.01, W / 2 - 0.01), "paint", (0.3, 0.3, 0.3), 0.3)
        if DETAIL[0] == 0 and over > 0.05:
            ze, zt = z0 - over * rise / (D / 2) + thick, z0 + rise + thick
            # the ridge's capping, the bargeboards up each gable, gutters and downpipes
            M.add(box("ridge", (0.0, 0.0, zt + 0.04), (0.26, W + 2 * over, 0.1)), "concrete", tuple(c * 0.8 for c in rgb), 0.4)
            for e in (-1, 1):
                y = e * (W / 2 + over)
                M.add(prism("barge", [(-h - 0.02, ze - 0.24), (0.0, zt - 0.24), (h + 0.02, ze - 0.24), (h + 0.02, ze + 0.02), (0.0, zt + 0.02), (-h - 0.02, ze + 0.02)],
                            y - 0.025 * (1 - e), y + 0.025 * (1 + e)), "paint", FRAME_W if surface != "timber" else TIMBER, 0.3)
            gutters_y(M, D, W, ze - thick, h, W + 2 * over)
    else:
        h = W / 2 + over
        pts = [(-h, z0 - over * rise / (W / 2)), (-h, z0 - over * rise / (W / 2) + thick), (0.0, z0 + rise + thick),
               (h, z0 - over * rise / (W / 2) + thick), (h, z0 - over * rise / (W / 2))]
        M.add(prism_x("roof", pts, -D / 2 - over, D / 2 + over), surface, rgb, 0.3)
        if DETAIL[0] == 0 and over > 0.05:
            ze, zt = z0 - over * rise / (W / 2) + thick, z0 + rise + thick
            M.add(box("ridge", (0.0, 0.0, zt + 0.04), (D + 2 * over, 0.26, 0.1)), "concrete", tuple(c * 0.8 for c in rgb), 0.4)
            x = D / 2 + over
            M.add(prism_x("barge", [(-h - 0.02, ze - 0.24), (-h - 0.02, ze + 0.02), (0.0, zt + 0.02), (h + 0.02, ze + 0.02), (h + 0.02, ze - 0.24), (0.0, zt - 0.24)],
                          x, x + 0.05), "paint", FRAME_W if surface != "timber" else TIMBER, 0.3)
            gutters_x(M, D, W, ze - thick, h, D + 2 * over)
    return z0 + rise + thick


def gable_fill(M, D, W, z0, rise, surface, rgb, ridge_y=True):
    """The gable walls' triangles in the walls' own finish."""
    if ridge_y:
        M.add(prism("gablewall", [(-D / 2, z0), (D / 2, z0), (0.0, z0 + rise)], -W / 2, W / 2), surface, rgb, 0.3)
    else:
        M.add(prism_x("gablewall", [(-W / 2, z0), (0.0, z0 + rise), (W / 2, z0)], -D / 2, D / 2), surface, rgb, 0.3)


def hip_roof(M, D, W, z0, rise, over, surface, rgb):
    hx, hy = D / 2 + over, W / 2 + over
    r = min(hx, hy) * 0.9
    verts = [(-hx, -hy, z0), (hx, -hy, z0), (hx, hy, z0), (-hx, hy, z0)]
    if hy >= hx:
        verts += [(0.0, -hy + r, z0 + rise), (0.0, hy - r, z0 + rise)]
        faces = [(0, 3, 2, 1), (0, 1, 4), (1, 2, 5, 4), (2, 3, 5), (3, 0, 4, 5)]
    else:
        verts += [(-hx + r, 0.0, z0 + rise), (hx - r, 0.0, z0 + rise)]
        faces = [(0, 3, 2, 1), (0, 1, 5, 4), (1, 2, 5), (2, 3, 4, 5), (3, 0, 4)]
    M.add(K._object("roof", verts, faces, 20.0), surface, rgb, 0.3)
    if DETAIL[0] == 0 and over > 0.05:
        M.add(box("fasciaboard", (0.0, 0.0, z0 - 0.08), (2 * hx, 2 * hy, 0.16)), "paint", FRAME_W, 0.3)
        gutters_y(M, D, W, z0 - 0.02, hx, 2 * hy)
        gutters_x(M, D, W, z0 - 0.02, hy, 2 * hx, pipes=False)
    return z0 + rise


def mono_roof(M, D, W, z_front, z_back, over, surface, rgb, thick=0.2):
    """A single slope, high at the back (-x) or the front, over its whole box."""
    xf, xb = D / 2 + over, -D / 2 - over
    k = (z_front - z_back) / D
    zf, zb = z_front + k * over, z_back - k * over
    M.add(prism("roof", [(xb, zb), (xf, zf), (xf, zf + thick), (xb, zb + thick)], -W / 2 - over, W / 2 + over), surface, rgb, 0.3)
    M.add(prism("monofill", [(-D / 2, 0.0), (D / 2, 0.0), (D / 2, z_front), (-D / 2, z_back)], -W / 2 + 0.01, W / 2 - 0.01), "paint", (0.3, 0.3, 0.3), 0.3)
    if DETAIL[0] == 0 and over > 0.05:
        lo = 1 if zf < zb else -1
        zl = min(zf, zb)
        M.add(box("gutter", (lo * (D / 2 + over + 0.06), 0.0, zl - 0.04), (0.12, W + 2 * over, 0.1)), "paint", GUTTER, 0.3)
        for e in (-1, 1):
            M.add(box("downpipe", (lo * (D / 2 + 0.06), e * (W / 2 - 0.12), zl / 2), (0.07, 0.07, zl)), "paint", GUTTER, 0.3)
    return max(zf, zb) + thick


def flat_roof(M, D, W, z0, parapet, rgb, cap_rgb=None):
    M.add(box("roofdeck", (0.0, 0.0, z0 + 0.1), (D - 0.1, W - 0.1, 0.2)), "paint", FELT, 0.6)
    if parapet > 0:
        M.add(box("parapetF", (D / 2 - 0.12, 0.0, z0 + parapet / 2), (0.24, W, parapet)), "paint", rgb, 0.3)
        M.add(box("parapetB", (-D / 2 + 0.12, 0.0, z0 + parapet / 2), (0.24, W, parapet)), "paint", rgb, 0.3)
        for s in (-1, 1):
            M.add(box("parapetS", (0.0, s * (W / 2 - 0.12), z0 + parapet / 2), (D, 0.24, parapet)), "paint", rgb, 0.3)
        if cap_rgb:
            M.add(box("coping", (0.0, 0.0, z0 + parapet + 0.03), (D + 0.06, W + 0.06, 0.06)), "concrete", cap_rgb, 0.3)
    if DETAIL[0] == 0:
        M.add(box("drip", (0.0, 0.0, z0 - 0.05), (D + 0.08, W + 0.08, 0.1)), "concrete", CONCRETE, 0.4)   # (the roof's edge trim)
        for e in (-1, 1):
            M.add(box("hopper", (-D / 2 - 0.08, e * (W / 2 - 0.25), z0 - 0.2), (0.16, 0.2, 0.2)), "paint", GUTTER, 0.3)
            M.add(box("downpipe", (-D / 2 - 0.06, e * (W / 2 - 0.25), (z0 - 0.3) / 2), (0.07, 0.07, z0 - 0.3)), "paint", GUTTER, 0.3)
    return z0 + max(parapet, 0.2) + 0.06


def window(M, lod, face, u, w, z0, h, frame=FRAME_W, bars=2, sill=True, D=0.0, W=0.0, inset=0.0):
    """A window on a face of a D x W box: face 'f' (+x), 'b' (-x), 'l' (+y), 'r' (-y); u along that face."""
    def put(name, du, dz, sx, sz, dn, thick, surface, rgb, dirt=0.25):
        if face in "fb":
            s = 1 if face == "f" else -1
            M.add(box(name, (s * (D / 2 + dn), u + du, dz), (thick, sx, sz)), surface, rgb, dirt)
        else:
            s = 1 if face == "l" else -1
            M.add(box(name, (u + du, s * (W / 2 + dn), dz), (sx, thick, sz)), surface, rgb, dirt)
    put("glass", 0.0, z0 + h / 2, w, h, 0.005 - inset, 0.03, "lens", GLASS, 0.05)
    if lod == 0:
        t = 0.07
        put("frame", 0.0, z0 + h - t / 2, w + 2 * t, t, 0.02, 0.06, "paint", frame)
        put("frame", 0.0, z0 + t / 2, w + 2 * t, t, 0.02, 0.06, "paint", frame)
        for su in (-1, 1):
            put("frame", su * (w / 2 + t / 2), z0 + h / 2, t, h, 0.02, 0.06, "paint", frame)
        for k in range(1, bars):
            put("bar", -w / 2 + k * w / bars, z0 + h / 2, 0.045, h, 0.025, 0.04, "paint", frame)
        if h > 0.9:
            put("transom", 0.0, z0 + h * 0.62, w, 0.06, 0.03, 0.05, "paint", frame)   # (the opening lights' rail)
        put("reveal", 0.0, z0 + h + 0.005, w, 0.03, -0.04, 0.1, "paint", (0.12, 0.12, 0.12))   # (the shadowed head of its opening)
        put("head", 0.0, z0 + h + 0.12, w + 0.3, 0.1, 0.035, 0.07, "concrete", (0.55, 0.54, 0.5))
        if sill:
            put("sill", 0.0, z0 - 0.04, w + 0.24, 0.08, 0.06, 0.14, "concrete", (0.55, 0.54, 0.5))
    elif lod == 1:
        put("frame", 0.0, z0 + h / 2, w + 0.1, h + 0.1, 0.0, 0.02, "paint", frame)


def door(M, lod, u, w, h, rgb, D, glazed=True, face="f", W=0.0):
    s = 1 if face == "f" else -1
    if face in "fb":
        M.add(box("door", (s * (D / 2 + 0.02), u, h / 2 + 0.15), (0.05, w, h)), "paint", rgb, 0.3)
        if glazed and lod < 2:
            M.add(box("doorglass", (s * (D / 2 + 0.05), u, h * 0.62 + 0.15), (0.02, w * 0.7, h * 0.55)), "lens", GLASS, 0.05)
        if lod == 0:
            # its frame round it, raised panels below (all over, unglazed), a handle and a letter plate
            for e in (-1, 1):
                M.add(box("doorframe", (s * (D / 2 + 0.04), u + e * (w / 2 + 0.05), h / 2 + 0.15), (0.06, 0.1, h + 0.1)), "paint", FRAME_W, 0.3)
            M.add(box("doorframe", (s * (D / 2 + 0.04), u, h + 0.2), (0.06, w + 0.2, 0.1)), "paint", FRAME_W, 0.3)
            rows = [(0.35, 0.55)] if glazed else [(0.35, 0.75), (1.25, 0.75)]
            for z, ph in rows:
                for e in ((-1, 1) if w > 0.7 else (0,)):
                    M.add(box("panel", (s * (D / 2 + 0.05), u + e * w * 0.22, z + ph / 2), (0.02, w * (0.34 if e else 0.7), ph)), "paint", tuple(c * 0.85 for c in rgb), 0.3)
            M.add(box("handle", (s * (D / 2 + 0.07), u + s * (w / 2 - 0.1), 1.15), (0.05, 0.03, 0.16)), "steel", (0.55, 0.5, 0.4), 0.2)
            M.add(box("step", (s * (D / 2 + 0.3), u, 0.075), (0.6, w + 0.5, 0.15)), "concrete", CONCRETE, 0.5)
    else:
        sy = 1 if face == "l" else -1
        M.add(box("door", (u, sy * (W / 2 + 0.02), h / 2 + 0.15), (w, 0.05, h)), "paint", rgb, 0.3)


def fascia(M, D, z, w, h, rgb, proud=0.08):
    M.add(box("fascia", (D / 2 + proud, 0.0, z), (0.1, w, h)), "paint", rgb, 0.2)


def awning(M, lod, D, u, w, z, depth, rgb, stripe=None):
    """A sloping canvas awning over the front at height z (its top), depth out."""
    pts = [(D / 2, z), (D / 2 + depth, z - 0.5), (D / 2 + depth, z - 0.75), (D / 2 + depth - 0.04, z - 0.75), (D / 2 + depth - 0.04, z - 0.53), (D / 2, z - 0.04)]
    if stripe and lod < 2:
        n = max(2, int(w / 0.5))
        for k in range(n):
            y0 = u - w / 2 + k * w / n
            M.add(prism("awning", pts, y0, y0 + w / n), "paint", rgb if k % 2 == 0 else stripe, 0.3)
    else:
        M.add(prism("awning", pts, u - w / 2, u + w / 2), "paint", rgb, 0.3)


def chimney(M, x, y, z0, top, rgb, surface="concrete"):
    M.add(box("chimney", (x, y, (z0 + top) / 2), (0.6, 0.9, top - z0)), surface, rgb, 0.4)
    M.add(box("pot", (x, y, top + 0.15), (0.25, 0.25, 0.3)), "concrete", TILE_RED, 0.4)
    return top + 0.3


def posts_veranda(M, lod, D, W, x_out, z, rgb, roof_rgb, n=4, roof_surface="paint"):
    """A veranda the width W in front: posts at x_out, a lean-to roof from the wall at z + 0.3 down to z."""
    d = x_out - D / 2
    pts = [(D / 2, z + 0.45), (x_out + 0.3, z), (x_out + 0.3, z + 0.12), (D / 2, z + 0.57)]
    M.add(prism("veranda", pts, -W / 2, W / 2), roof_surface, roof_rgb, 0.3)
    if lod < 2:
        for k in range(n):
            y = -W / 2 + 0.2 + k * (W - 0.4) / (n - 1)
            M.add(box("post", (x_out, y, z / 2), (0.14, 0.14, z)), "timber" if rgb == TIMBER else "paint", rgb, 0.3)
        M.add(box("deck", (D / 2 + d / 2, 0.0, 0.08), (d + 0.2, W, 0.16)), "timber", TIMBER_PALE, 0.5)


def walls(M, D, W, H, surface, rgb, dirt=0.3):
    M.add(box("walls", (0.0, 0.0, H / 2), (D, W, H)), surface, rgb, dirt)


def side_windows(M, lod, D, W, z0, h, w, n, frame=FRAME_W, faces="lr", bars=2):
    for f in faces:
        for k in range(n):
            u = -D / 2 + (k + 0.5) * D / n
            window(M, lod, f, u, w, z0, h, frame, bars, True, D, W)


def front_windows(M, lod, D, W, us, w, z0, h, frame=FRAME_W, bars=2):
    for u in us:
        window(M, lod, "f", u, w, z0, h, frame, bars, True, D, W)


# --- the cafes -------------------------------------------------------------------------------------------------------
# Each returns its model and its facts: deep D (x), wide W (y), top (m over the ground), the fascia (its middle's
# height, width, the cap height for the name, the name's colour) and the door (y).

def tea_hut(name, lod):
    M = K.Model(name)
    D, W, H = 2.6, 3.6, 2.3
    footing(M, D, W)
    walls(M, D, W, H, "timber", (0.2, 0.3, 0.22))
    if lod < 2:
        for k in range(1, 12):
            M.add(box("board", (D / 2 + 0.01, 0.0, k * 0.2), (0.02, W, 0.025)), "timber", (0.16, 0.24, 0.17), 0.3)
    top = gable_roof(M, D, W, H, 0.9, 0.3, "paint", FELT, ridge_y=True)
    # (the serving hatch: an opening with a flap propped up over it, a counter)
    M.add(box("hatch", (D / 2 + 0.01, -0.3, 1.45), (0.03, 1.8, 0.8)), "paint", DARK, 0.2)
    M.add(box("flap", (D / 2 + 0.38, -0.3, 1.98), (0.75, 1.9, 0.04), rot=Matrix.Rotation(math.radians(-22), 4, "Y")), "timber", (0.16, 0.24, 0.17), 0.3)
    M.add(box("counter", (D / 2 + 0.15, -0.3, 1.02), (0.32, 1.9, 0.05)), "timber", TIMBER_PALE, 0.3)
    door(M, lod, 1.2, 0.8, 1.95, (0.16, 0.24, 0.17), D, glazed=False)
    if lod < 2:
        M.add(rod("flue", (-0.6, 1.2, H + 0.3), (-0.6, 1.2, H + 1.4), 0.07, 8), "steel", STEEL, 0.5)
    fascia(M, D, H - 0.2, 2.2, 0.32, CREAM)
    return M, dict(D=D, W=W, top=max(top, H + 1.4), fz=H - 0.2, fw=2.0, cap=0.2, ink=(0.05, 0.12, 0.07), door=1.2)


def park_kiosk(name, lod):
    M = K.Model(name)
    D, W, H = 3.0, 3.0, 2.6
    footing(M, D, W)
    walls(M, D, W, H, "paint", GREEN)
    if lod < 2:
        for s in (-1, 1):
            for t in (-1, 1):
                M.add(box("pilaster", (s * D / 2, t * W / 2, H / 2), (0.16, 0.16, H)), "paint", (0.02, 0.1, 0.05), 0.3)
    # (a hipped, almost pyramid roof with a finial)
    top = hip_roof(M, D, W, H + 0.25, 1.3, 0.45, "paint", (0.03, 0.12, 0.06))
    M.add(box("eaves", (0.0, 0.0, H + 0.12), (D + 0.9, W + 0.9, 0.25)), "paint", CREAM, 0.3)
    if lod < 2:
        M.add(lathe("finial", [(0.08, 0.0), (0.1, 0.25), (0.0, 0.55)], 8, at=(0.0, 0.0, top - 0.05)), "paint", (0.5, 0.4, 0.12), 0.2)
    for f, u in (("f", 0.0), ("l", 0.0), ("r", 0.0)):
        window(M, lod, f, u, 1.9, 1.0, 1.1, CREAM, 3, False, D, W)
    M.add(box("counter", (D / 2 + 0.12, 0.0, 0.98), (0.25, 2.1, 0.05)), "paint", CREAM, 0.3)
    door(M, lod, 0.0, 0.8, 2.0, (0.02, 0.1, 0.05), D, face="b")
    fascia(M, D, H - 0.12, 2.6, 0.26, CREAM, proud=0.1)
    return M, dict(D=D, W=W, top=top + 0.5, fz=H - 0.12, fw=2.4, cap=0.17, ink=(0.03, 0.12, 0.06), door=-99.0)


def container_bar(name, lod):
    M = K.Model(name)
    D, W, H = 2.44, 6.06, 2.59
    footing(M, D, W)
    walls(M, D, W, H, "galv", (0.05, 0.2, 0.26))
    if lod < 2:
        step = 0.28 if lod == 0 else 0.56
        k = 0
        while -W / 2 + k * step < W / 2:
            for s in (-1, 1):
                M.add(box("rib", (s * (D / 2 + 0.015), -W / 2 + k * step + 0.07, H / 2), (0.03, 0.08, H - 0.2)), "galv", (0.04, 0.17, 0.22), 0.4)
            k += 1
        for s in (-1, 1):
            M.add(box("rail", (0.0, s * (W / 2 - 0.05), H - 0.08), (D + 0.04, 0.12, 0.16)), "galv", (0.04, 0.15, 0.2), 0.4)
    # (the big hatch: its side cut away, a flap up as an awning, a counter)
    M.add(box("hatch", (D / 2 + 0.03, 0.2, 1.55), (0.04, 3.6, 1.0)), "paint", DARK, 0.2)
    M.add(box("flap", (D / 2 + 0.55, 0.2, 2.25), (1.1, 3.7, 0.05), rot=Matrix.Rotation(math.radians(-12), 4, "Y")), "galv", (0.05, 0.2, 0.26), 0.3)
    M.add(box("counter", (D / 2 + 0.2, 0.2, 1.05), (0.4, 3.7, 0.06)), "timber", TIMBER_PALE, 0.3)
    M.add(box("roof", (0.0, 0.0, H + 0.03), (D, W, 0.06)), "galv", (0.05, 0.2, 0.26), 0.4)
    door(M, lod, -2.4, 0.8, 2.0, (0.04, 0.15, 0.2), D)
    fascia(M, D, H - 0.18, 3.6, 0.3, (0.75, 0.6, 0.1))
    return M, dict(D=D, W=W, top=H + 0.06, fz=H - 0.18, fw=3.3, cap=0.18, ink=(0.03, 0.03, 0.03), door=-2.4)


def log_cabin(name, lod):
    M = K.Model(name)
    D, W, H = 4.2, 6.4, 2.5
    footing(M, D, W)
    walls(M, D - 0.1, W - 0.1, H, "timber", LOGS)
    if lod < 2:
        n = 11 if lod == 0 else 6
        for k in range(n):
            z = 0.12 + k * (H - 0.1) / n
            for s in (-1, 1):
                M.add(rod("log", (s * D / 2, -W / 2 - 0.15, z), (s * D / 2, W / 2 + 0.15, z), 0.11, 6), "timber", LOGS, 0.4)
                M.add(rod("log", (-D / 2 - 0.15, s * W / 2, z + 0.1), (D / 2 + 0.15, s * W / 2, z + 0.1), 0.11, 6), "timber", LOGS, 0.4)
    gable_fill(M, D, W, H, 1.2, "timber", LOGS)
    top = gable_roof(M, D, W, H, 1.2, 0.5, "paint", (0.12, 0.06, 0.04))
    front_windows(M, lod, D, W, [-1.8, 1.8], 1.3, 0.95, 1.0, TIMBER_PALE)
    door(M, lod, 0.0, 0.9, 2.0, TIMBER_PALE, D)
    posts_veranda(M, lod, D, W, D / 2 + 1.6, 2.3, TIMBER, (0.12, 0.06, 0.04), 3)
    fascia(M, D, 2.25, 2.6, 0.34, TIMBER_PALE, proud=0.15)
    return M, dict(D=D, W=W, top=top, fz=2.25, fw=2.4, cap=0.22, ink=(0.08, 0.04, 0.02), door=0.0, fx=D / 2 + 0.21, front=D / 2 + 1.9)


def curved_kiosk(name, lod):
    M = K.Model(name)
    D, W, H = 3.2, 5.0, 2.5
    footing(M, D, W)
    walls(M, D, W, H, "paint", (0.62, 0.5, 0.36))
    n = (12, 6, 3)[lod]
    pts = []
    for i in range(n + 1):
        t = i / n
        x = -D / 2 - 0.4 + t * (D + 0.8)
        pts.append((x, H + 0.55 * (1 - (2 * t - 1) ** 2)))
    pts += [(D / 2 + 0.4, H - 0.12), (-D / 2 - 0.4, H - 0.12)]
    pts = [pts[-1]] + pts[:-1]
    M.add(prism("roof", pts, -W / 2 - 0.3, W / 2 + 0.3), "galv", (0.25, 0.27, 0.27), 0.3)
    window(M, lod, "f", -0.4, 3.2, 0.95, 1.2, FRAME_D, 4, False, D, W)
    M.add(box("counter", (D / 2 + 0.15, -0.4, 0.95), (0.3, 3.4, 0.05)), "paint", WHITE, 0.3)
    awning(M, lod, D, -0.4, 3.6, H - 0.05, 0.9, RED, WHITE)
    door(M, lod, 2.0, 0.75, 2.0, FRAME_D, D)
    fascia(M, D, H + 0.22, 3.0, 0.3, WHITE, proud=0.42)
    return M, dict(D=D, W=W, top=H + 0.55, fz=H + 0.22, fw=2.8, cap=0.18, ink=RED, door=2.0, fx=D / 2 + 0.48, front=D / 2 + 0.9)


def carriage(name, lod):
    M = K.Model(name)
    D, W, H = 2.9, 14.0, 3.1
    footing(M, D, W)
    # (an old railway carriage on a short length of track: the body raised, its roof curved, its panels' doors)
    M.add(box("body", (0.0, 0.0, 0.9 + (H - 0.9) / 2), (D, W, H - 0.9)), "paint", (0.25, 0.06, 0.04), 0.3)
    M.add(box("frame", (0.0, 0.0, 0.7), (D - 0.4, W - 0.6, 0.4)), "paint", DARK, 0.5)
    n = (12, 6, 3)[lod]
    pts = [(-D / 2 - 0.05, H - 0.1)]
    for i in range(n + 1):
        t = i / n
        y = -D / 2 - 0.05 + t * (D + 0.1)
        pts.append((y, H + 0.35 * (1 - (2 * t - 1) ** 2)))
    pts.append((D / 2 + 0.05, H - 0.1))
    M.add(prism("roof", pts, -W / 2 - 0.1, W / 2 + 0.1), "paint", (0.3, 0.3, 0.29), 0.4)
    M.add(box("cream", (0.0, 0.0, 2.55), (D + 0.02, W, 0.9)), "paint", CREAM, 0.3)
    for k in range(7):
        u = -W / 2 + 1.0 + k * (W - 2.0) / 6
        window(M, lod, "f", u, 1.2, 2.15, 0.75, (0.25, 0.06, 0.04), 1, False, D, W)
        window(M, lod, "b", u, 1.2, 2.15, 0.75, (0.25, 0.06, 0.04), 1, False, D, W)
    if lod < 2:
        for y in (-W / 2 + 2.0, W / 2 - 2.0):
            for s in (-1, 1):
                for x in (-0.6, 0.6):
                    ob = lathe("wheel", [(0.0, -0.06), (0.45, -0.06), (0.45, 0.06), (0.0, 0.06)], (14, 8, 5)[lod])
                    ob.matrix_world = Matrix.Translation((s * 0.75, y + x, 0.45)) @ Matrix.Rotation(math.radians(90), 4, "Y")
                    M.add(ob, "steel", (0.08, 0.08, 0.08), 0.6)
        for s in (-1, 1):
            M.add(box("rail", (s * 0.72, 0.0, 0.05), (0.08, W + 2.0, 0.1)), "steel", STEEL, 0.6)
        for k in range(12):
            M.add(box("sleeper", (0.0, -W / 2 - 0.5 + k * (W + 1.0) / 11, 0.03), (2.4, 0.25, 0.06)), "timber", (0.15, 0.1, 0.06), 0.6)
    # (steps up to the door)
    door(M, lod, 0.0, 0.8, 1.9, (0.25, 0.06, 0.04), D, glazed=True)
    M.add(box("steps", (D / 2 + 0.5, 0.0, 0.45), (0.9, 1.2, 0.9)), "timber", TIMBER, 0.5)
    fascia(M, D, 1.55, 5.0, 0.36, CREAM, proud=0.05)
    return M, dict(D=D, W=W, top=H + 0.35, fz=1.55, fw=4.6, cap=0.22, ink=(0.25, 0.06, 0.04), door=0.0)


def dales_tea_room(name, lod):
    M = K.Model(name)
    D, W, H = 7.0, 10.0, 4.6
    footing(M, D, W)
    walls(M, D, W, H, "concrete", STONE)
    if lod == 0:
        for s in (-1, 1):
            for t in (-1, 1):
                for k in range(8):
                    M.add(box("quoin", (s * (D / 2 + 0.01), t * (W / 2 - 0.25 - (0.12 if k % 2 else 0.0)), 0.3 + k * 0.55), (0.04, 0.5 + (0.24 if k % 2 else 0.0), 0.35)), "concrete", (0.46, 0.43, 0.36), 0.3)
    gable_fill(M, D, W, H, 2.4, "concrete", STONE)
    top = gable_roof(M, D, W, H, 2.4, 0.3, "concrete", SLATE)
    top = max(top, chimney(M, 0.0, W / 2 - 0.5, H + 1.0, top + 0.7, STONE_DARK))
    front_windows(M, lod, D, W, [-3.2, 3.2], 1.6, 0.9, 1.5, FRAME_W, 3)
    front_windows(M, lod, D, W, [-3.0, 0.0, 3.0], 1.0, 3.0, 1.0, FRAME_W, 2)
    door(M, lod, 0.0, 1.0, 2.1, GREEN, D)
    side_windows(M, lod, D, W, 1.0, 1.2, 1.1, 2)
    fascia(M, D, 2.55, 3.4, 0.4, GREEN)
    return M, dict(D=D, W=W, top=top, fz=2.55, fw=3.2, cap=0.26, ink=CREAM, door=0.0)


def thatched(name, lod):
    M = K.Model(name)
    D, W, H = 6.0, 9.0, 2.6
    footing(M, D, W)
    walls(M, D, W, H, "concrete", RENDER_W, 0.2)
    gable_fill(M, D, W, H, 2.6, "concrete", RENDER_W)
    # (the thatch: thick and rounded at the eaves, a ridge roll)
    pts = [(-D / 2 - 0.6, H - 0.35), (D / 2 + 0.6, H - 0.35), (D / 2 + 0.6, H + 0.05), (0.0, H + 2.95), (-D / 2 - 0.6, H + 0.05)]
    M.add(prism("thatch", pts, -W / 2 - 0.5, W / 2 + 0.5), "timber", THATCH, 0.5)
    if lod < 2:
        M.add(rod("ridge", (0.0, -W / 2 - 0.4, H + 2.95), (0.0, W / 2 + 0.4, H + 2.95), 0.22, 8), "timber", (0.3, 0.22, 0.1), 0.5)
    top = chimney(M, -1.0, -W / 2 + 0.6, H + 1.5, H + 3.6, BRICK, "concrete")
    front_windows(M, lod, D, W, [-2.8, 2.8], 1.4, 0.8, 1.1, BLACKBOARD, 3)
    # (a porch over the door)
    door(M, lod, 0.0, 0.9, 1.95, (0.12, 0.2, 0.32), D)
    M.add(prism("porch", [(D / 2, 2.2), (D / 2 + 1.1, 2.2), (D / 2, 2.8)], -0.9, 0.9), "timber", THATCH, 0.5)
    if lod < 2:
        for s in (-1, 1):
            M.add(box("porchpost", (D / 2 + 1.0, s * 0.8, 1.1), (0.1, 0.1, 2.2)), "timber", TIMBER, 0.4)
    fascia(M, D, 2.0, 1.6, 0.0001, RENDER_W, proud=0.0)
    if lod < 2:
        M.add(box("signboard", (D / 2 + 0.06, -2.8, 2.15), (0.06, 2.0, 0.36)), "paint", (0.12, 0.2, 0.32), 0.3)
    return M, dict(D=D, W=W, top=top, fz=2.15, fw=1.8, cap=0.2, ink=CREAM, door=0.0, fy=-2.8, fx=D / 2 + 0.1)


def transport_cafe(name, lod):
    M = K.Model(name)
    D, W, H = 8.0, 12.0, 3.6
    footing(M, D, W)
    walls(M, D, W, H, "concrete", BRICK)
    top = flat_roof(M, D, W, H, 0.6, BRICK, (0.5, 0.48, 0.44))
    M.add(box("band", (D / 2 + 0.02, 0.0, H + 0.0), (0.06, W, 0.9)), "paint", (0.7, 0.68, 0.6), 0.3)
    front_windows(M, lod, D, W, [-3.6, 3.6], 3.6, 0.85, 1.8, FRAME_W, 3)
    door(M, lod, 0.0, 1.4, 2.1, FRAME_W, D)
    side_windows(M, lod, D, W, 1.1, 1.3, 1.6, 3)
    if lod < 2:
        M.add(box("extract", (-2.4, 3.6, H + 0.9), (0.6, 0.6, 0.6)), "galv", STEEL, 0.5)
        M.add(rod("extractpipe", (-2.4, 3.6, H + 1.2), (-2.4, 3.6, H + 2.0), 0.15, 8), "galv", STEEL, 0.5)
    fascia(M, D, H + 0.05, 8.0, 0.7, RED, proud=0.08)
    return M, dict(D=D, W=W, top=max(top, H + 2.0), fz=H + 0.05, fw=7.6, cap=0.44, ink=WHITE, door=0.0)


def timber_modern(name, lod):
    M = K.Model(name)
    D, W = 8.0, 10.0
    footing(M, D, W)
    zf, zb = 4.6, 3.2
    M.add(prism("walls", [(-D / 2, 0.0), (D / 2, 0.0), (D / 2, zf), (-D / 2, zb)], -W / 2, W / 2), "timber", (0.3, 0.2, 0.12), 0.3)
    if lod < 2:
        step = 0.15 if lod == 0 else 0.4
        k = 0
        while -W / 2 + k * step < W / 2:
            M.add(box("batten", (-D / 2 - 0.015, -W / 2 + k * step, zb / 2), (0.03, 0.05, zb)), "timber", (0.22, 0.14, 0.08), 0.4)
            k += 1
    top = mono_roof(M, D, W, zf, zb, 0.4, "galv", (0.12, 0.13, 0.14))
    window(M, lod, "f", -1.0, 7.0, 0.2, 3.6, FRAME_D, 5, False, D, W)
    door(M, lod, 3.6, 1.2, 2.2, FRAME_D, D)
    side_windows(M, lod, D, W, 1.0, 1.4, 2.2, 1, FRAME_D)
    fascia(M, D, zf - 0.3, 5.0, 0.4, (0.2, 0.13, 0.07), proud=0.05)
    return M, dict(D=D, W=W, top=top, fz=zf - 0.3, fw=4.6, cap=0.26, ink=(0.8, 0.75, 0.6), door=3.6)


def glass_garden(name, lod):
    M = K.Model(name)
    D, W, H = 10.0, 14.0, 3.2
    footing(M, D, W)
    M.add(box("dwarfwall", (0.0, 0.0, 0.3), (D, W, 0.6)), "concrete", BRICK, 0.3)
    M.add(box("back", (-D / 2 + 0.15, 0.0, H / 2), (0.3, W, H)), "concrete", BRICK, 0.3)
    M.add(box("glassbox", (0.15, 0.0, 0.6 + (H - 0.6) / 2), (D - 0.3, W - 0.06, H - 0.6)), "lens", GLASS, 0.05)
    if lod < 2:
        n = 14 if lod == 0 else 7
        for k in range(n + 1):
            y = -W / 2 + k * W / n
            M.add(box("mullion", (D / 2, y, 0.6 + (H - 0.6) / 2), (0.08, 0.08, H - 0.6)), "paint", WHITE, 0.2)
        for k in range(1, 10 if lod == 0 else 5):
            x = -D / 2 + k * D / (10 if lod == 0 else 5)
            for s in (-1, 1):
                M.add(box("mullion", (x, s * W / 2, 0.6 + (H - 0.6) / 2), (0.08, 0.08, H - 0.6)), "paint", WHITE, 0.2)
    # (a glazed pitched roof: the ridge along y, white glazing bars)
    rise = 1.8
    pts = [(-D / 2 - 0.1, H), (D / 2 + 0.1, H), (D / 2 + 0.1, H + 0.08), (0.0, H + rise + 0.08), (-D / 2 - 0.1, H + 0.08)]
    M.add(prism("roof", pts, -W / 2 - 0.1, W / 2 + 0.1), "lens", (0.12, 0.15, 0.16), 0.05)
    gable_fill(M, D, W, H, rise, "lens", GLASS)
    if lod < 2:
        for k in range(0, 15, 1 if lod == 0 else 2):
            y = -W / 2 + k * W / 14
            for s in (-1, 1):
                ob = box("rafter", (s * D / 4, y, H + rise / 2 + 0.1), (math.hypot(D / 2, rise) + 0.1, 0.06, 0.08), rot=Matrix.Rotation(s * math.atan2(rise, D / 2), 4, "Y"))
                M.add(ob, "paint", WHITE, 0.2)
        M.add(box("ridge", (0.0, 0.0, H + rise + 0.12), (0.2, W + 0.2, 0.12)), "paint", WHITE, 0.2)
    M.add(box("eaves", (0.0, 0.0, H - 0.1), (D + 0.3, W + 0.3, 0.2)), "paint", WHITE, 0.2)
    door(M, lod, 0.0, 1.8, 2.2, WHITE, D)
    fascia(M, D, H - 0.1, 5.0, 0.4, GREEN, proud=0.2)
    return M, dict(D=D, W=W, top=H + rise + 0.2, fz=H - 0.1, fw=4.6, cap=0.26, ink=CREAM, door=0.0)


def black_barn(name, lod):
    M = K.Model(name)
    D, W, H = 8.0, 14.0, 3.4
    footing(M, D, W)
    M.add(box("plinth", (0.0, 0.0, 0.3), (D + 0.06, W + 0.06, 0.6)), "concrete", BRICK, 0.4)
    walls(M, D, W, H, "timber", (0.025, 0.025, 0.025))
    if lod < 2:
        step = 0.22 if lod == 0 else 0.44
        for s in (-1, 1):
            k = 1
            while 0.6 + k * step < H:
                M.add(box("weather", (s * (D / 2 + 0.015), 0.0, 0.6 + k * step), (0.03, W, 0.03)), "timber", (0.015, 0.015, 0.015), 0.4)
                k += 1
    gable_fill(M, D, W, H, 3.6, "timber", (0.025, 0.025, 0.025))
    top = gable_roof(M, D, W, H, 3.6, 0.35, "paint", (0.18, 0.06, 0.04))
    # (the cart doors' opening glazed full height, a gabled porch over it)
    window(M, lod, "f", 0.0, 3.6, 0.6, 2.8, FRAME_D, 3, False, D, W)
    M.add(prism("porch", [(D / 2, H + 0.2), (D / 2 + 2.0, H + 0.2), (D / 2 + 2.0, H + 0.4), (D / 2, H + 0.4)], -2.6, 2.6), "paint", (0.18, 0.06, 0.04), 0.3)
    if lod < 2:
        for s in (-1, 1):
            M.add(box("porchpost", (D / 2 + 1.9, s * 2.4, (H + 0.2) / 2), (0.2, 0.2, H + 0.2)), "timber", TIMBER, 0.4)
    front_windows(M, lod, D, W, [-5.0, 5.0], 1.6, 1.1, 1.3, WHITE, 3)
    side_windows(M, lod, D, W, 1.1, 1.3, 1.3, 2, WHITE)
    door(M, lod, -2.5, 0.9, 2.0, (0.025, 0.025, 0.025), D, glazed=False)
    fascia(M, D, H + 0.6, 4.0, 0.0001, (0.025, 0.025, 0.025), proud=2.1)
    if lod < 2:
        M.add(box("signboard", (D / 2 + 2.1, 0.0, H + 0.75), (0.06, 4.4, 0.6)), "timber", CREAM, 0.3)
    return M, dict(D=D, W=W, top=top, fz=H + 0.75, fw=4.0, cap=0.32, ink=(0.03, 0.03, 0.03), door=0.0, fx=D / 2 + 2.14, front=D / 2 + 2.0)


def greasy_spoon(name, lod):
    M = K.Model(name)
    D, W, H = 7.0, 11.0, 3.0
    footing(M, D, W)
    walls(M, D, W, H, "concrete", BRICK_YEL)
    if lod < 2:
        step = 0.16 if lod == 0 else 0.32
        k = 0
        while 1.6 + k * step < H:
            for s in (-1, 1):
                M.add(box("hung", (s * (D / 2 + 0.02), 0.0, 1.6 + k * step), (0.04, W + 0.04, 0.03)), "concrete", TILE_RED, 0.3)
                M.add(box("hung", (0.0, s * (W / 2 + 0.02), 1.6 + k * step), (D + 0.04, 0.04, 0.03)), "concrete", TILE_RED, 0.3)
            k += 1
    top = hip_roof(M, D, W, H, 2.2, 0.35, "concrete", TILE_RED)
    front_windows(M, lod, D, W, [-3.3, 3.3], 3.0, 0.8, 1.5, FRAME_W, 3)
    door(M, lod, 0.0, 1.0, 2.0, FRAME_W, D)
    awning(M, lod, D, 0.0, 10.0, H - 0.05, 1.1, (0.05, 0.12, 0.3), WHITE)
    side_windows(M, lod, D, W, 1.0, 1.1, 1.3, 2)
    fascia(M, D, 2.55, 6.0, 0.0001, BRICK_YEL, proud=1.2)
    if lod < 2:
        M.add(box("signboard", (D / 2 + 0.06, 0.0, H + 0.35), (0.08, 5.6, 0.6)), "paint", WHITE, 0.3)
    return M, dict(D=D, W=W, top=top, fz=H + 0.35, fw=5.2, cap=0.36, ink=(0.05, 0.12, 0.3), door=0.0, fx=D / 2 + 0.11, front=D / 2 + 1.1)


def seaside_chalet(name, lod):
    M = K.Model(name)
    D, W, H = 6.0, 10.0, 3.0
    footing(M, D, W)
    walls(M, D, W, H, "timber", (0.5, 0.62, 0.62))
    if lod < 2:
        step = 0.18 if lod == 0 else 0.36
        k = 1
        while k * step < H:
            for s in (-1, 1):
                M.add(box("board", (s * (D / 2 + 0.012), 0.0, k * step), (0.025, W, 0.025)), "timber", (0.42, 0.53, 0.53), 0.3)
            k += 1
    gable_fill(M, D, W, H, 1.8, "timber", (0.5, 0.62, 0.62), ridge_y=False)
    top = gable_roof(M, D, W, H, 1.8, 0.4, "paint", (0.12, 0.2, 0.32), ridge_y=False)
    if lod < 2:
        M.add(prism_x("bargeF", [(-W / 2 - 0.4, H - 0.15), (0.0, H + 1.85), (W / 2 + 0.4, H - 0.15), (W / 2 + 0.25, H - 0.15), (0.0, H + 1.65), (-W / 2 - 0.25, H - 0.15)], D / 2 + 0.35, D / 2 + 0.45), "paint", WHITE, 0.2)
    front_windows(M, lod, D, W, [-3.0, 3.0], 2.2, 0.9, 1.4, WHITE, 3)
    door(M, lod, 0.0, 1.6, 2.1, WHITE, D)
    posts_veranda(M, lod, D, W, D / 2 + 2.0, 2.5, WHITE, (0.12, 0.2, 0.32), 5)
    fascia(M, D, H + 0.9, 3.6, 0.5, WHITE, proud=0.48)
    return M, dict(D=D, W=W, top=top, fz=H + 0.9, fw=3.2, cap=0.3, ink=(0.12, 0.2, 0.32), door=0.0, fx=D / 2 + 0.54, front=D / 2 + 2.3)


def octagon(name, lod):
    M = K.Model(name)
    R, H = 5.0, 3.2
    footing(M, 2 * R * 0.93, 2 * R * 0.93)
    ring = [(R * math.cos(math.radians(22.5 + 45 * k)), R * math.sin(math.radians(22.5 + 45 * k))) for k in range(8)]
    verts = [(x, y, 0.0) for x, y in ring] + [(x, y, H) for x, y in ring]
    faces = [tuple(range(8))[::-1], tuple(range(8, 16))] + [(k, (k + 1) % 8, 8 + (k + 1) % 8, 8 + k) for k in range(8)]
    M.add(K._object("walls", verts, faces, 30.0), "concrete", RENDER_W, 0.2)
    # (glazed on every face but the back; the roof an eight-sided cone with a lantern)
    for k in range(8):
        if k in (3, 4):
            continue
        a0, a1 = math.radians(22.5 + 45 * k), math.radians(22.5 + 45 * (k + 1))
        mx, my = R * 0.5 * (math.cos(a0) + math.cos(a1)), R * 0.5 * (math.sin(a0) + math.sin(a1))
        ang = math.atan2(my, mx)
        side = 2 * R * math.sin(math.radians(22.5))
        ob = box("glass", (mx * 1.002, my * 1.002, 0.5 + 1.1), (0.04, side * 0.8, 2.2), rot_z=ang)
        M.add(ob, "lens", GLASS, 0.05)
        if lod < 2:
            for t in (-1, 0, 1):
                M.add(box("bar", (mx * 1.006 - math.sin(ang) * t * side * 0.4, my * 1.006 + math.cos(ang) * t * side * 0.4, 1.6), (0.06, 0.06, 2.2), rot_z=ang), "paint", (0.1, 0.25, 0.2), 0.2)
    rv = [(1.12 * x, 1.12 * y, H) for x, y in ring] + [(0.0, 0.0, H + 2.6)]
    rf = [tuple(range(8))[::-1]] + [(k, (k + 1) % 8, 8) for k in range(8)]
    M.add(K._object("roof", rv, rf, 20.0), "galv", (0.12, 0.3, 0.26), 0.3)
    M.add(box("lantern", (0.0, 0.0, H + 2.6), (0.8, 0.8, 0.6)), "paint", WHITE, 0.2)
    M.add(lathe("cap", [(0.55, 0.0), (0.0, 0.5)], 8, at=(0.0, 0.0, H + 2.9)), "galv", (0.12, 0.3, 0.26), 0.3)
    M.add(box("door", (R * 0.925 + 0.03, 0.0, 1.1), (0.05, 1.5, 2.2)), "paint", (0.1, 0.25, 0.2), 0.3)
    fascia(M, R * 0.925 * 2, H - 0.25, 3.0, 0.36, (0.1, 0.25, 0.2), proud=0.06)
    return M, dict(D=2 * R * 0.93, W=2 * R * 0.93, top=H + 3.4, fz=H - 0.25, fw=2.8, cap=0.22, ink=CREAM, door=0.0)


def biker_cafe(name, lod):
    M = K.Model(name)
    D, W, H = 11.0, 22.0, 3.4
    footing(M, D, W)
    walls(M, D, W, H, "concrete", (0.6, 0.58, 0.52), 0.35)
    M.add(box("plinth", (0.0, 0.0, 0.4), (D + 0.06, W + 0.06, 0.8)), "concrete", BRICK, 0.4)
    gable_fill(M, D, W, H, 2.4, "concrete", (0.6, 0.58, 0.52))
    top = gable_roof(M, D, W, H, 2.4, 0.4, "galv", (0.1, 0.11, 0.12))
    front_windows(M, lod, D, W, [-8.0, -4.5, 4.5, 8.0], 2.6, 0.9, 1.7, DARK, 3)
    door(M, lod, 0.0, 1.8, 2.2, DARK, D)
    window(M, lod, "f", 0.0, 4.0, 2.5, 0.6, DARK, 4, False, D, W)
    # (a long veranda the length of the front on black posts, and the pull-in's bike bay in front)
    posts_veranda(M, lod, D, W, D / 2 + 3.2, 2.7, DARK, (0.1, 0.11, 0.12), 7, "galv")
    side_windows(M, lod, D, W, 1.0, 1.3, 1.8, 3, DARK)
    if lod < 2:
        M.add(box("extract", (-3.0, 7.0, H + 1.2), (0.7, 0.7, 0.7)), "galv", STEEL, 0.5)
    fascia(M, D, H + 1.1, 9.0, 0.9, DARK, proud=0.05)
    return M, dict(D=D, W=W, top=top, fz=H + 1.1, fw=8.6, cap=0.6, ink=(0.8, 0.62, 0.15), door=0.0, front=D / 2 + 3.5)


def diner(name, lod):
    M = K.Model(name)
    D, W, H = 7.0, 18.0, 3.4
    footing(M, D, W)
    # (rounded ends: a stadium in plan, a half drum at each end)
    seg = (16, 8, 4)[lod]
    r = D / 2
    ring = []
    for k in range(seg + 1):
        a = math.pi * k / seg
        ring.append((r * math.cos(a), W / 2 - r + r * math.sin(a)))
    for k in range(seg + 1):
        a = math.pi + math.pi * k / seg
        ring.append((r * math.cos(a), -W / 2 + r + r * math.sin(a)))
    n = len(ring)

    def drum(nm, z0, z1, scale, surface, rgb, dirt=0.25):
        v = [(x * scale, y * (1 + (scale - 1) * r / (W / 2)), z0) for x, y in ring] + [(x * scale, y * (1 + (scale - 1) * r / (W / 2)), z1) for x, y in ring]
        f = [tuple(range(n))[::-1], tuple(range(n, 2 * n))] + [(k, (k + 1) % n, n + (k + 1) % n, n + k) for k in range(n)]
        M.add(K._object(nm, v, f, 40.0), surface, rgb, dirt)
    drum("body", 0.0, H, 1.0, "paint", (0.75, 0.73, 0.68))
    drum("band", 0.25, 0.85, 1.01, "paint", (0.45, 0.03, 0.03))
    drum("stripe", 1.0, 1.06, 1.012, "galv", CHROME)
    drum("stripe", 2.85, 2.91, 1.012, "galv", CHROME)
    drum("roof", H, H + 0.35, 1.04, "galv", CHROME)
    window(M, lod, "f", -4.0, 5.0, 1.15, 1.6, CHROME, 6, False, D, W)
    window(M, lod, "f", 4.0, 5.0, 1.15, 1.6, CHROME, 6, False, D, W)
    window(M, lod, "b", 0.0, 8.0, 1.15, 1.6, CHROME, 8, False, D, W)
    door(M, lod, 0.0, 1.6, 2.2, CHROME, D)
    M.add(box("doorcanopy", (D / 2 + 0.6, 0.0, 2.55), (1.2, 2.6, 0.15)), "galv", CHROME, 0.2)
    # (the sign on the roof: a panel on two legs)
    M.add(box("roofsign", (D / 2 - 0.6, 0.0, H + 1.15), (0.2, 7.0, 1.2)), "paint", (0.45, 0.03, 0.03), 0.2)
    if lod < 2:
        for s in (-1, 1):
            M.add(box("signleg", (D / 2 - 0.6, s * 3.0, H + 0.45), (0.12, 0.12, 0.3)), "galv", CHROME, 0.2)
    fascia(M, D, H + 1.15, 6.6, 0.0001, (0.45, 0.03, 0.03), proud=-0.48)
    return M, dict(D=D, W=W, top=H + 1.75, fz=H + 1.15, fw=6.4, cap=0.7, ink=(0.85, 0.82, 0.7), door=0.0, fx=D / 2 - 0.48)


def coaching_inn(name, lod):
    M = K.Model(name)
    D, W, H = 9.0, 18.0, 5.6
    footing(M, D, W)
    walls(M, D, W, H, "concrete", STONE)
    gable_fill(M, D, W, H, 2.9, "concrete", STONE)
    top = gable_roof(M, D, W, H, 2.9, 0.3, "concrete", SLATE)
    for y in (-W / 2 + 0.6, W / 2 - 0.6):
        top = max(top, chimney(M, -0.5, y, H + 1.0, H + 3.6, STONE_DARK))
    front_windows(M, lod, D, W, [-7.0, -4.0, 4.0, 7.0], 1.4, 0.9, 1.6, FRAME_W, 3)
    front_windows(M, lod, D, W, [-7.0, -4.0, -1.0, 1.0, 4.0, 7.0], 1.2, 3.4, 1.3, FRAME_W, 2)
    door(M, lod, 0.0, 1.3, 2.3, (0.2, 0.05, 0.04), D)
    M.add(prism("doorhood", [(D / 2, 2.6), (D / 2 + 0.7, 2.6), (D / 2, 3.0)], -1.0, 1.0), "concrete", SLATE, 0.3)
    side_windows(M, lod, D, W, 1.0, 1.4, 1.2, 2)
    fascia(M, D, 2.95 + 0.0, 0.0001, 0.0001, STONE, proud=0.0)
    if lod < 2:
        M.add(box("signboard", (D / 2 + 0.05, 0.0, 3.0), (0.06, 6.0, 0.5)), "paint", (0.07, 0.12, 0.08), 0.3)
    return M, dict(D=D, W=W, top=top, fz=3.0, fw=5.6, cap=0.32, ink=(0.75, 0.62, 0.3), door=0.0, fx=D / 2 + 0.09)


def truck_stop(name, lod):
    M = K.Model(name)
    D, W, H = 14.0, 22.0, 4.6
    footing(M, D, W)
    walls(M, D, W, H, "galv", (0.45, 0.47, 0.47))
    M.add(box("plinth", (0.0, 0.0, 0.6), (D + 0.06, W + 0.06, 1.2)), "concrete", BRICK, 0.4)
    if lod < 2:
        step = 0.3 if lod == 0 else 0.9
        k = 0
        while -W / 2 + k * step < W / 2:
            for s in (-1, 1):
                M.add(box("rib", (s * (D / 2 + 0.02), -W / 2 + k * step, 1.2 + (H - 1.2) / 2), (0.04, 0.05, H - 1.2)), "galv", (0.38, 0.4, 0.4), 0.4)
            k += 1
    gable_fill(M, D, W, H, 1.4, "galv", (0.45, 0.47, 0.47))
    top = gable_roof(M, D, W, H, 1.4, 0.3, "galv", (0.15, 0.2, 0.25))
    front_windows(M, lod, D, W, [-8.0, -4.0, 4.0, 8.0], 3.0, 1.2, 1.6, FRAME_W, 3)
    door(M, lod, 0.0, 2.0, 2.3, FRAME_W, D)
    M.add(box("entrance", (D / 2 + 1.0, 0.0, 2.9), (2.0, 4.0, 0.2)), "galv", (0.15, 0.2, 0.25), 0.3)
    if lod < 2:
        for s in (-1, 1):
            M.add(box("col", (D / 2 + 1.8, s * 1.8, 1.4), (0.15, 0.15, 2.8)), "galv", STEEL, 0.3)
        M.add(box("backdoor", (-D / 2 - 0.02, 5.0, 1.5), (0.04, 3.0, 3.0)), "galv", STEEL, 0.4)
    fascia(M, D, H - 0.6, 12.0, 1.0, (0.06, 0.22, 0.12), proud=0.1)
    return M, dict(D=D, W=W, top=top, fz=H - 0.6, fw=11.4, cap=0.66, ink=(0.85, 0.82, 0.7), door=0.0)


def deco_roadhouse(name, lod):
    M = K.Model(name)
    D, W, H = 11.0, 19.0, 4.0
    footing(M, D, W)
    R = 3.0
    # (the walls, the front +y corner rounded: a drum of radius R there with bands of glazing)
    walls(M, D, W - R, H, "concrete", RENDER_W, 0.25)
    M.parts[-1].obj.matrix_world = Matrix.Translation((0.0, -R / 2, H / 2))
    walls(M, D - R, R + 0.01, H, "concrete", RENDER_W, 0.25)
    M.parts[-1].obj.matrix_world = Matrix.Translation((-R / 2, W / 2 - R / 2, H / 2))
    seg = (12, 6, 3)[lod]
    M.add(lathe("drum", [(R, 0.0), (R, H)], seg * 4, at=(D / 2 - R, W / 2 - R, 0.0)), "concrete", RENDER_W, 0.25)
    for z in (1.1, 2.6):
        M.add(lathe("drumglass", [(R + 0.01, z), (R + 0.01, z + 0.9)], seg * 4, at=(D / 2 - R, W / 2 - R, 0.0)), "lens", GLASS, 0.05)
    top = flat_roof(M, D, W, H, 0.5, RENDER_W)
    M.add(box("tower", (D / 2 - 1.2, -2.0, (H + 2.6) / 2), (2.4, 3.2, H + 2.6)), "concrete", RENDER_W, 0.25)
    if lod < 2:
        for k in range(3):
            M.add(box("fin", (D / 2 + 0.02, -2.0 - 0.8 + k * 0.8, H + 1.4), (0.08, 0.12, 2.0)), "paint", (0.05, 0.2, 0.15), 0.2)
        for z in (1.8, 2.3, 2.6):
            M.add(box("speedline", (D / 2 + 0.02, -6.5, z), (0.04, 5.0, 0.06)), "paint", (0.05, 0.2, 0.15), 0.2)
    front_windows(M, lod, D, W, [-6.5, 2.4], 3.0, 1.0, 1.6, (0.05, 0.2, 0.15), 4)
    door(M, lod, -2.0, 1.6, 2.4, (0.05, 0.2, 0.15), D)
    side_windows(M, lod, D, W, 1.0, 1.6, 1.8, 3, (0.05, 0.2, 0.15), faces="r")
    return M, dict(D=D, W=W, top=max(top, H + 2.6), fz=H - 0.4, fw=6.0, cap=0.5, ink=(0.05, 0.2, 0.15), door=-2.0, fy=-6.5)


DESIGNS = [tea_hut, park_kiosk, container_bar, log_cabin, curved_kiosk, carriage, dales_tea_room, thatched, transport_cafe,
           timber_modern, glass_garden, black_barn, greasy_spoon, seaside_chalet, octagon, biker_cafe, diner, coaching_inn,
           truck_stop, deco_roadhouse]
# (what sets out in front of each: 0 picnic benches, 1 bistro tables, 2 timber tables; and how many sets at most)
TERRACE = [(0, 2), (1, 2), (1, 3), (0, 3), (1, 3), (0, 4), (2, 4), (2, 3), (0, 5), (1, 5), (1, 6), (0, 6), (1, 4), (1, 5),
           (2, 6), (0, 8), (1, 5), (2, 6), (0, 8), (1, 6)]
# (the kinds of place each suits: 0 a kiosk or hut where the map has no building, 1 a small cafe, 2 a cafe, 3 a large one)
SIZE_CLASS = [0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 1, 2, 3, 3, 3, 3, 3]


# --- the terrace things ----------------------------------------------------------------------------------------------

def chair(name, v, lod):
    M = K.Model(name)
    seat, back = 0.46, 0.85
    sides = (8, 6, 4)[lod]
    col = [DARK, TIMBER_PALE, GREEN, WHITE][v]
    surf = "timber" if v == 1 else "paint"
    # (the seat 0.42 square, centred 0.03 back of the origin; the back behind it at -x)
    if v == 1:
        if lod < 2:
            for k in range(5):
                M.add(box("slat", (0.17 - k * 0.085, 0.0, seat - 0.02), (0.07, 0.44, 0.03)), surf, col, 0.3)
        else:
            M.add(box("seat", (0.0, 0.0, seat - 0.02), (0.42, 0.44, 0.03)), surf, col, 0.3)
    elif v == 3:
        M.add(box("seat", (0.0, 0.0, seat - 0.04), (0.44, 0.46, 0.08), bevel=0.02 if lod == 0 else 0.0), surf, col, 0.3)
    else:
        M.add(lathe("seat", [(0.0, -0.015), (0.21, -0.015), (0.22, 0.0), (0.21, 0.015), (0.0, 0.015)], (16, 10, 6)[lod], at=(0.0, 0.0, seat - 0.015)), surf, col, 0.3)
    for sx in (-1, 1):
        for sy in (-1, 1):
            a = (sx * 0.18, sy * 0.18, seat - 0.03)
            b = (sx * 0.21, sy * 0.21, 0.0)
            if v == 1:
                M.add(box("leg", ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, seat / 2 - 0.02), (0.04, 0.04, seat - 0.02)), surf, col, 0.3)
            elif v == 3:
                M.add(box("leg", (sx * 0.19, sy * 0.19, seat / 2 - 0.04), (0.06, 0.06, seat - 0.08)), surf, col, 0.3)
            elif lod < 2:
                M.add(rod("leg", a, b, 0.012, sides), surf, col, 0.3)
            else:
                M.add(box("leg", ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, seat / 2), (0.025, 0.025, seat)), surf, col, 0.3)
    # (the back)
    if v == 1:
        for sy in (-1, 1):
            M.add(box("upright", (-0.21, sy * 0.19, (seat + back) / 2), (0.04, 0.04, back - seat)), surf, col, 0.3)
        for k in range(3 if lod < 2 else 1):
            M.add(box("backslat", (-0.21, 0.0, back - 0.05 - k * 0.11), (0.025, 0.42, 0.08)), surf, col, 0.3)
    elif v == 3:
        # a moulded garden armchair: the back raked, slots across it, arms on to the front legs
        bk = box("back", (-0.21, 0.0, (seat + back) / 2), (0.04, 0.46, back - seat), bevel=0.015 if lod == 0 else 0.0,
                 rot=Matrix.Rotation(math.radians(-10), 4, "Y"))
        M.add(bk, surf, col, 0.3)
        if lod < 2:
            for e in (-1, 1):
                M.add(box("arm", (-0.02, e * 0.25, seat + 0.2), (0.42, 0.06, 0.03), bevel=0.01 if lod == 0 else 0.0), surf, col, 0.3)
                M.add(box("armpost", (0.17, e * 0.25, seat + 0.1), (0.04, 0.05, 0.2)), surf, col, 0.3)
        if lod == 0:
            for k in range(4):
                M.add(box("slot", (-0.2 + 0.0, -0.12 + k * 0.08, seat + 0.24), (0.05, 0.025, 0.2), rot=Matrix.Rotation(math.radians(-10), 4, "Y")),
                      "paint", tuple(c * 0.55 for c in col), 0.3)
    else:
        if lod < 2:
            pts = [(-0.2, -0.19 + 0.38 * t / 6, back - 0.03 + 0.03 * math.sin(math.pi * t / 6)) for t in range(7)]
            M.add(sweep("backrail", [(-0.19, -0.19, seat)] + pts + [(-0.19, 0.19, seat)], 0.012, sides, True), surf, col, 0.3)
            if lod == 0:
                for k in range(3):
                    M.add(rod("backbar", (-0.2, -0.12 + k * 0.12, seat + 0.05), (-0.21, -0.12 + k * 0.12, back - 0.04), 0.008, 5), surf, col, 0.3)
        else:
            M.add(box("back", (-0.2, 0.0, (seat + back) / 2), (0.02, 0.38, back - seat)), surf, col, 0.3)
    return M


def table(name, v, lod):
    M = K.Model(name)
    h = 0.74
    col = [DARK, TIMBER_PALE, WHITE][v]
    surf = "timber" if v == 1 else "paint"
    if v == 1:
        # a garden table: a slatted top with its breadboard ends, an apron under it, square legs
        bev = 0.005 if lod == 0 else 0.0
        if lod < 2:
            for k in range(5):
                M.add(box("slat", (-0.3 + k * 0.15, 0.0, h - 0.02), (0.135, 0.68, 0.035), bevel=bev), surf, col, 0.35)
            for e in (-1, 1):
                M.add(box("breadboard", (0.0, e * 0.37, h - 0.02), (0.8, 0.06, 0.035), bevel=bev), surf, col, 0.35)
            for e in (-1, 1):
                M.add(box("apron", (e * 0.33, 0.0, h - 0.09), (0.03, 0.62, 0.09)), surf, col, 0.35)
                M.add(box("apron", (0.0, e * 0.33, h - 0.09), (0.62, 0.03, 0.09)), surf, col, 0.35)
        else:
            M.add(box("top", (0.0, 0.0, h - 0.02), (0.8, 0.8, 0.04)), surf, col, 0.3)
        for sx in (-1, 1):
            for sy in (-1, 1):
                M.add(box("leg", (sx * 0.33, sy * 0.33, (h - 0.04) / 2), (0.06, 0.06, h - 0.04), bevel=bev), surf, col, 0.35)
    else:
        r = 0.35 if v == 0 else 0.4
        seg = (20, 12, 8)[lod]
        M.add(lathe("top", [(0.0, -0.02), (r, -0.02), (r, 0.02), (0.0, 0.02)], seg, at=(0.0, 0.0, h - 0.02)), surf, col, 0.3)
        M.add(lathe("stem", [(0.025, 0.04), (0.025, h - 0.04)], (8, 6, 4)[lod]), surf, col, 0.3)
        if lod < 2:
            for k in range(3):
                a = 2 * math.pi * k / 3
                M.add(rod("foot", (0.0, 0.0, 0.05), (0.28 * math.cos(a), 0.28 * math.sin(a), 0.0), 0.015, (6, 4, 4)[lod]), surf, col, 0.3)
        else:
            M.add(lathe("base", [(0.0, 0.0), (0.22, 0.0), (0.22, 0.03), (0.0, 0.03)], 6), surf, col, 0.3)
    return M


def picnic(name, v, lod):
    """A picnic bench set (beenster's pictures, Nov 26: the A-frame pub bench in dark weathered timber): the table 1.8 m
    along y, six planks 0.75 wide, 0.76 tall; two-plank benches at x = +-0.62, 0.45 tall, on bearers through A-frame
    legs, with braces from the bearers to the middle of the top."""
    M = K.Model(name)
    L = 1.8
    col = (0.075, 0.068, 0.06) if v == 0 else (0.12, 0.09, 0.065)   # (weathered near black; or old brown)
    dirt = 0.75
    bev = 0.006 if lod == 0 else 0.0

    def plank(nm, c, size):
        M.add(box(nm, c, size, bevel=bev), "timber", col, dirt)

    if lod < 2:
        for k in range(6):
            plank("topslat", (-0.3125 + k * 0.125, 0.0, 0.74), (0.115, L, 0.04))
        for s in (-1, 1):
            for k in range(2):
                plank("seatslat", (s * (0.555 + k * 0.13), 0.0, 0.43), (0.12, L, 0.04))
    else:
        plank("top", (0.0, 0.0, 0.74), (0.75, L, 0.04))
        for s in (-1, 1):
            plank("seat", (s * 0.62, 0.0, 0.43), (0.25, L, 0.04))
    ends = (-L / 2 + 0.22, L / 2 - 0.22)
    for y in ends:
        out = 1.0 if y > 0 else -1.0
        # the A-frame: two legs from under the top's edges out past the benches' inner planks to the ground
        top_x, foot_x, top_z = 0.27, 0.70, 0.72
        ln = math.hypot(foot_x - top_x, top_z)
        ang = math.atan2(foot_x - top_x, top_z)
        for s in (-1, 1):
            M.add(box("aleg", (s * (top_x + foot_x) / 2, y, top_z / 2), (0.09, 0.045, ln), bevel=bev,
                      rot=Matrix.Rotation(-s * ang, 4, "Y")), "timber", col, dirt)   # (top in, foot out)
        # the cleat under the top and the bench bearer, each with its ends cut back, on the legs' outer face
        y0, y1 = (y + 0.0225, y + 0.0675) if out > 0 else (y - 0.0675, y - 0.0225)
        M.add(prism("cleat", [(-0.37, 0.72), (-0.30, 0.65), (0.30, 0.65), (0.37, 0.72)], y0, y1), "timber", col, dirt)
        M.add(prism("bearer", [(-0.84, 0.41), (-0.76, 0.32), (0.76, 0.32), (0.84, 0.41)], y0, y1), "timber", col, dirt)
        if lod < 2:
            # a brace from the bearer's middle up to the middle of the top (the pictures' inner diagonals)
            a = (0.0, y - out * 0.03, 0.36)
            b = (0.0, y - out * 0.55, 0.715)
            dy, dz = b[1] - a[1], b[2] - a[2]
            M.add(box("brace", ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2), (0.07, 0.045, math.hypot(dy, dz)), bevel=bev,
                      rot=Matrix.Rotation(math.atan2(-dy, dz), 4, "X")), "timber", col, dirt)
    if lod < 2:
        # the rail under the top's middle the braces meet
        plank("rail", (0.0, 0.0, 0.69), (0.07, L - 0.44 - 2 * 0.53, 0.05))
    return M


def parasol(name, v, lod):
    M = K.Model(name)
    col = [CREAM, (0.04, 0.17, 0.08), (0.45, 0.05, 0.04), NAVY][v]
    seg = (16, 8, 6)[lod]
    M.add(lathe("pole", [(0.02, 0.0), (0.02, 2.36)], (6, 4, 4)[lod]), "paint", WHITE, 0.3)
    M.add(lathe("canopy", [(0.0, 1.95), (1.3, 2.0), (1.32, 2.06), (0.06, 2.4), (0.0, 2.42)], seg, phase=math.pi / seg), "paint", col, 0.3)
    if lod < 2:
        M.add(lathe("valance", [(1.3, 1.86), (1.31, 1.86), (1.31, 2.02), (1.3, 2.02)], seg, phase=math.pi / seg), "paint", col, 0.3)
        M.add(lathe("finial", [(0.0, 2.4), (0.04, 2.42), (0.0, 2.48)], 6), "paint", WHITE, 0.3)
        # its base: a heavy round weight, or a cross of feet
        if v % 2 == 0:
            M.add(lathe("base", [(0.0, 0.0), (0.24, 0.0), (0.24, 0.06), (0.06, 0.1), (0.0, 0.1)], (16, 8)[lod]), "concrete", CONCRETE, 0.5)
        else:
            for k in range(2):
                M.add(box("foot", (0.0, 0.0, 0.03), (0.75, 0.06, 0.06), rot_z=k * math.pi / 2), "paint", GUTTER, 0.4)
    if lod == 0:
        # the ribs under the canvas, their stretchers down to the runner, and the crank
        for k in range(8):
            a = 2 * math.pi * (k + 0.5) / 16 * 2
            c, sn = math.cos(a), math.sin(a)
            M.add(rod("rib", (0.04 * c, 0.04 * sn, 2.36), (1.28 * c, 1.28 * sn, 1.98), 0.008, 4), "paint", WHITE, 0.3)
            M.add(rod("stretcher", (0.04 * c, 0.04 * sn, 1.62), (0.65 * c, 0.65 * sn, 2.16), 0.006, 4), "paint", WHITE, 0.3)
        M.add(lathe("runner", [(0.035, 1.58), (0.035, 1.68)], 8), "paint", WHITE, 0.3)
        M.add(box("crank", (0.06, 0.0, 1.2), (0.1, 0.03, 0.12)), "paint", GUTTER, 0.3)
    return M


def aboard(name, v, lod):
    M = K.Model(name)
    h, w = 1.0, 0.6
    ang = math.radians(12)
    col = TIMBER if v == 0 else (0.04, 0.12, 0.08)
    if lod == 0:
        M.add(rod("hinge", (0.0, -w / 2, h * math.cos(ang) - 0.01), (0.0, w / 2, h * math.cos(ang) - 0.01), 0.012, 6), "steel", (0.3, 0.3, 0.3), 0.3)
        for e in (-1, 1):
            z = 0.35
            M.add(rod("stay", (-(h - z) * math.sin(ang), e * (w / 2 - 0.05), z * math.cos(ang)),
                      ((h - z) * math.sin(ang), e * (w / 2 - 0.05), z * math.cos(ang)), 0.005, 4), "steel", (0.3, 0.3, 0.3), 0.3)
    for s in (-1, 1):
        R = Matrix.Rotation(-s * ang, 4, "Y")
        M.add(box("panel", (s * h / 2 * math.sin(ang), 0.0, h / 2 * math.cos(ang)), (0.03, w, h), rot=R), "timber" if v == 0 else "paint", col, 0.3)
        M.add(box("board", (s * (h / 2 * math.sin(ang) + 0.017), 0.0, h / 2 * math.cos(ang) + 0.04), (0.005, w - 0.08, h - 0.24), rot=R), "paint", BLACKBOARD, 0.1)
        if lod == 0:
            # its frame round the blackboard, the hinge along the top and the stay between the legs
            for e in (-1, 1):
                M.add(box("rail", (s * (h / 2 * math.sin(ang) + 0.02), e * (w / 2 - 0.03), h / 2 * math.cos(ang)), (0.012, 0.06, h), rot=R),
                      "timber" if v == 0 else "paint", tuple(c * 0.8 for c in col), 0.3)
            M.add(box("handhole", (s * ((h - 0.08) * math.sin(ang) + 0.016), 0.0, (h - 0.08) * math.cos(ang)), (0.01, 0.14, 0.04), rot=R), "paint", DARK, 0.3)
            for k in range(5):
                z = 0.3 + k * 0.11
                wl = (0.32, 0.4, 0.22, 0.36, 0.28)[k]
                M.add(box("chalk", (s * (z * math.tan(ang) + 0.022), -0.18 + wl / 2, z), (0.003, wl, 0.025), rot=R), "paint", CHALK, 0.1)
    return M


def build(lod):
    DETAIL[0] = lod
    s = ("", "_LOD1", "_LOD2")[lod]
    out, facts = [], []
    for i, f in enumerate(DESIGNS):
        M, fx = f("SM_Prop_cafe_building_%d%s" % (i, s), lod)
        out.append(M)
        facts.append(fx)
    out += [chair("SM_Prop_cafe_chair_%d%s" % (v, s), v, lod) for v in range(4)]
    out += [table("SM_Prop_cafe_table_%d%s" % (v, s), v, lod) for v in range(3)]
    out += [picnic("SM_Prop_cafe_picnic_%d%s" % (v, s), v, lod) for v in range(2)]
    out += [parasol("SM_Prop_cafe_parasol_%d%s" % (v, s), v, lod) for v in range(4)]
    out += [aboard("SM_Prop_cafe_aboard_%d%s" % (v, s), v, lod) for v in range(2)]
    return out, facts


def write_kit(out, facts):
    """The prop kit's rows (each kind's tallest model's height: the game scales each model back to its own) and
    Data/RideUK/cafe_kit.json (each design's facts and its model's own height, foot to top, for the game's scale)."""
    tall = {}
    for name, (nt, y0, y1) in out.items():
        if name.endswith("_LOD1") or name.endswith("_LOD2"):
            continue
        kind = name[len("SM_Prop_"):].rsplit("_", 1)[0]
        tall[kind] = max(tall.get(kind, 0.0), y1 - y0)
    lines = open(K.KIT).read().split("\n")
    for kind, (reach, shadow, hand) in KINDS.items():
        row = {"words": [], "reach": reach, "height": round(tall[kind], 3), "shadow": shadow, "credit": "", "hand_lods": hand}
        head = '  "%s": ' % kind
        for i, ln in enumerate(lines):
            if ln.startswith(head):
                comma = ln.rstrip().endswith(",")
                lines[i] = head + json.dumps(row) + ("," if comma else "")
                break
        else:
            at = max(i for i, ln in enumerate(lines) if ln.startswith('  "') and ln.rstrip().endswith("},"))
            lines.insert(at + 1, head + json.dumps(row) + ",")
    open(K.KIT, "w").write("\n".join(lines))
    json.load(open(K.KIT))
    kit = {"_about": "Ride UK's cafes (Tools/blender_cafe_kit.py): per design (cafe_building's variant) its box deep (x, to the "
                     "front) and wide (y), m; own: its model's height foot to top (m, with the 0.5 m footing), the game's scale "
                     "is own over the kind's height; fz/fw/cap/ink: the fascia's middle height, width, letters' cap height and "
                     "colour; fx, fy: the name's place out from the middle and along (fx absent: the box's front); door: along "
                     "y (-99: at the back); front: the terrace starts this far out (absent: D/2); terrace: 0 picnic benches, 1 "
                     "bistro tables, 2 timber tables, and how many sets at most; size: 0 kiosk, 1 small, 2 cafe, 3 large.",
           "kind_height": round(tall["cafe_building"], 3), "foot": FOOT, "designs": []}
    for i, fx in enumerate(facts):
        name = "SM_Prop_cafe_building_%d" % i
        nt, y0, y1 = out[name]
        d = {"id": i, "name": DESIGNS[i].__name__, "D": round(fx["D"], 3), "W": round(fx["W"], 3), "own": round(y1 - y0, 3),
             "fz": round(fx["fz"], 3), "fw": round(fx["fw"], 3), "cap": round(fx["cap"], 3), "ink": [round(c, 3) for c in fx["ink"]],
             "door": round(fx["door"], 3), "terrace": list(TERRACE[i]), "size": SIZE_CLASS[i], "tris": nt}
        d["fx"] = round(fx.get("fx", fx["D"] / 2 + 0.14), 3)
        d["fy"] = round(fx.get("fy", 0.0), 3)
        d["front"] = round(fx.get("front", fx["D"] / 2), 3)
        kit["designs"].append(d)
    path = os.path.join(os.path.dirname(K.KIT), "cafe_kit.json")
    with open(path, "w") as f:
        json.dump(kit, f, indent=1)
    print("wrote", path)
    # (the same as a table the game compiles in: World/WorldRideCafeKit.inc)
    inc = os.path.join(os.path.dirname(HERE), "Source", "TorqueShedProto", "World", "WorldRideCafeKit.inc")
    rows = []
    for d in kit["designs"]:
        rows.append("\t{%s, %s, %s, %s, %s, %s, %s, {%sf, %sf, %sf}, %s, %s, %s, %d, %d, %d},   // %d %s" % (
            d["D"], d["W"], d["own"], d["fx"], d["fy"], d["fz"], d["fw"], d["ink"][0], d["ink"][1], d["ink"][2], d["cap"], d["door"],
            d["front"], d["terrace"][0], d["terrace"][1], d["size"], d["id"], d["name"]))
    with open(inc, "w") as f:
        f.write("// Made by Tools/blender_cafe_kit.py (do not edit): the cafe designs (cafe_building's variants) as modelled.\n"
                "// D, W, own height (with the footing), fascia x, y, z, width, ink, cap, door y, terrace front, terrace kind,\n"
                "// most sets, size class.\n")
        f.write("static constexpr double CafeKindHeight = %s;\n" % kit["kind_height"])
        f.write("static const CafeDesign CafeDesigns[] = {\n" + "\n".join(rows) + "\n};\n")
    print("wrote", inc)


def main():
    import blender_roadside_props as RS
    args = sys.argv[1:]
    out = {}
    facts0 = None
    for lod in (0, 1, 2):
        K.clear()
        models, facts = build(lod)
        if lod == 0:
            facts0 = facts
        if "--render" in args:
            d = args[args.index("--render") + 1]
            if lod == 0:
                RS.render_sheet(models[:20], d, "Ride UK's cafes: twenty of our own designs, smallest to largest (front to +x)", "cafe_buildings.png", cols=4, size=(480, 340))
                RS.render_sheet(models[20:], d, "Cafe terrace things: chairs, tables, picnic benches, parasols, A-boards", "cafe_terrace.png", cols=5, size=(300, 260))
            elif "--lods" in args:
                RS.render_sheet(models[:20], d, "Cafes, distance version %d" % lod, "cafe_buildings_lod%d.png" % lod, cols=4, size=(360, 250))
        for M in models:
            nt, y0, y1, x0, x1, z0, z1 = K.write(M)
            out[M.name] = (nt, y0, y1)
            print("%-34s %6d triangles, %.2f..%.2f m up, %.2f x %.2f m" % (M.name, nt, y0, y1, x1 - x0, z1 - z0))
    write_kit(out, facts0)


if __name__ == "__main__":
    main()
