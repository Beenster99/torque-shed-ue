"""[forecourts] Ride UK's petrol stations, modelled by script in Blender (Everyday Britain kits plan, stage 5,
approved by beenster Nov 26: "rebuild the existing forecourt as a proper model: canopy, pumps, kiosk shop,
price pole, air and water machine, jet wash, forecourt clutter. The ride-in garage unit and filling up stay
as they are"). Our own designs from general knowledge of ordinary UK forecourts: no maker's name, logo or
colour scheme is copied. Each station wears one of a set of made-up liveries (a main colour and an accent,
none a real company's pair) and a plain emblem with no lettering.

The game lays these pieces where World/WorldRideTownLife.cpp already puts each forecourt's parts (the canopy
over its two or three pump islands, the kiosk row behind, the price pole at the way in), so placing, filling
up and the garage unit are unchanged. Pieces, in the forecourt's own frame (x along the road, y out from the
road's edge, away from it, z up, the ground at 0; anything standing runs on 0.6 m below so a slope never shows
its foot):

  canopy_2, canopy_3   the canopy for 2 or 3 islands: deep fascia in the livery, accent stripe, chamfered
                       corners, white soffit with flush light panels, clad columns with livery kick plates
  island               one pump island: raised kerbed island, two dispensers (screens, holsters, hoses),
                       crash hoops, a bin and a screenwash and towel stand
  kiosk_<style>_<bay>  the shop, cut into bays (2.5 m nominal, stretched to the row): glass, door, atm,
                       end; styles "glass" (the modern box) and "brick" (the older village station, tiled roof)
  price_pole           a tall totem: livery panel, emblem, four price rows lit after dark, plinth
  air_water            the air and water machine on its plinth, hose coiled
  jet_wash             a two-bay jet wash: steel frame, roof, side screens, control posts, lances
  clutter              the gas bottle cage, ice chest, log and coal bags, A-board, planters, a rules plate

Every part is tagged with the furthest level of detail it stays in (0 near only, 1 the middle, 2 always), so
the distance versions are made here by hand, never by Unreal's reducer. A part's colour may be a livery slot
(LIVERY_MAIN, LIVERY_ACCENT) that the game fills per station, and lit parts (NIGHT_*) glow after dark only.

    python Tools/blender_forecourt_kit.py --render DIR    # the review sheet in DIR
    python Tools/blender_forecourt_kit.py --export        # Data/RideUK/forecourt_kit.bin and .json for the game

Reviewed by beenster Nov 26 13:48 ("These look great").
"""
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from blender_street_kit import _object, box, lathe, sweep  # noqa: E402

FOOT = -0.6
SWAP_XY = Matrix(((0, 1, 0, 0), (1, 0, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)))   # (a prism's outline (x, z) laid on the y-z plane)

# The game's layout (World/WorldRideTownLife.cpp): keep in step.
ENTRY_LANE = 5.0       # road edge to the canopy's front
CANOPY_DEEP = 9.0
ISLAND_GAP = 6.5
ISLAND_LONG = 4.4
SOFFIT = 5.0           # the canopy's underside above the forecourt
FASCIA = 1.1
KIOSK_DEEP = 6.5
KIOSK_GAP = 1.5        # canopy's back to the kiosk's front
KIOSK_BAY = 2.5

# Livery slots: the game swaps in the station's own colours (render uses the livery picked).
LIVERY_MAIN = "main"
LIVERY_ACCENT = "accent"
# Lit after dark (the game draws these in the square's night mesh).
NIGHT_SOFFIT = "night_soffit"
NIGHT_SHOP = "night_shop"
NIGHT_DIGITS = "night_digits"
NIGHT_SIGN = "night_sign"

# Made-up liveries: (label, main, accent). Checked against the UK's real forecourt schemes so none is one.
LIVERIES = [
    ("plum and silver", (0.07, 0.01, 0.045), (0.55, 0.56, 0.57)),
    ("teal and coral", (0.0, 0.16, 0.15), (0.75, 0.22, 0.13)),
    ("damson and cream", (0.09, 0.012, 0.03), (0.72, 0.62, 0.42)),
    ("slate and copper", (0.022, 0.026, 0.03), (0.5, 0.2, 0.07)),
    ("mustard and charcoal", (0.3, 0.15, 0.015), (0.03, 0.03, 0.035)),
    ("midnight and sky", (0.01, 0.02, 0.07), (0.3, 0.55, 0.8)),
]

# (linear colours)
WHITE = (0.78, 0.78, 0.76)
SOFFIT_WHITE = (0.7, 0.7, 0.69)
CONCRETE = (0.36, 0.35, 0.33)
KERB_YELLOW = (0.75, 0.5, 0.02)
HOOP = (0.65, 0.42, 0.02)
STEEL = (0.32, 0.33, 0.34)
GALV = (0.48, 0.49, 0.48)
DARK = (0.03, 0.03, 0.035)
SCREEN = (0.01, 0.012, 0.015)
GLASS = (0.025, 0.035, 0.045)
FRAME = (0.09, 0.09, 0.1)
RENDER = (0.6, 0.58, 0.54)
PANEL = (0.45, 0.46, 0.47)
BRICK = (0.27, 0.09, 0.05)
TILE = (0.1, 0.05, 0.035)
RUBBER = (0.012, 0.012, 0.012)
PUMP_GREY = (0.6, 0.61, 0.62)
GAS_BLUE = (0.02, 0.1, 0.35)
GAS_RED = (0.4, 0.03, 0.02)
GAS_GREEN = (0.02, 0.2, 0.06)
ICE_WHITE = (0.7, 0.72, 0.74)
LOG_BROWN = (0.2, 0.12, 0.05)
SACK = (0.08, 0.08, 0.08)
PLANT = (0.03, 0.12, 0.02)
LAMP = (2.0, 2.0, 1.9)
SHOP_GLOW = (1.6, 1.4, 1.1)
AMBER = (2.4, 1.3, 0.2)


class Part:
    def __init__(self, obj, surface, rgb, lod):
        self.obj, self.surface, self.rgb, self.lod = obj, surface, rgb, lod


class Piece:
    """One kit piece: its parts, each with the furthest LOD it stays in."""
    def __init__(self, name, label):
        self.name, self.label = name, label
        self.parts = []

    def add(self, obj, surface, rgb, lod=2):
        self.parts.append(Part(obj, surface, rgb, lod))
        return obj

    def b(self, name, x0, x1, y0, y1, z0, z1, surface, rgb, lod=2, bevel=0.0):
        """A box from (x0, y0, z0) to (x1, y1, z1)."""
        c = ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
        return self.add(box(name, c, (abs(x1 - x0), abs(y1 - y0), abs(z1 - z0)), bevel=bevel), surface, rgb, lod)


def prism(name, outline, y0, y1):
    """A slab whose face is the convex outline [(x, z)], from y0 to y1."""
    n = len(outline)
    verts = [(x, y0, z) for x, z in outline] + [(x, y1, z) for x, z in outline]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, 2 * n))]
    faces += [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    return _object(name, verts, faces, 30.0)


def plan_slab(name, ring, z0, z1):
    """A slab whose plan is the convex ring [(x, y)], from z0 up to z1."""
    n = len(ring)
    verts = [(x, y, z0) for x, y in ring] + [(x, y, z1) for x, y in ring]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, 2 * n))]
    faces += [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    return _object(name, verts, faces, 30.0)


def chamfered(x0, y0, x1, y1, c):
    return [(x0 + c, y0), (x1 - c, y0), (x1, y0 + c), (x1, y1 - c), (x1 - c, y1), (x0 + c, y1), (x0, y1 - c), (x0, y0 + c)]


def emblem(P, x, y, z, size, facing, lod=1):
    """The plain made-up emblem: a white drop with a smaller drop in the accent inside it, no lettering.
    facing -1 faces -y (towards the road), +1 faces +y; standing proud of a face at y."""
    def drop(r, n=14):
        pts = []
        for k in range(n):
            a = -math.pi / 2 + 2 * math.pi * k / n
            px, pz = r * math.cos(a), r * math.sin(a)
            if pz > 0:   # (the top half drawn up to a point)
                t = pz / r
                px *= (1.0 - t) ** 1.1
                pz = r * (t * 1.6)
            pts.append((px, pz))
        return pts
    r = size * 0.32
    zc = z - size * 0.12
    outer = [(x + px, zc + pz) for px, pz in drop(r)]
    inner = [(x + px * 0.55 + r * 0.12, zc + pz * 0.55 - r * 0.18) for px, pz in drop(r)]
    t0 = y + (0.0 if facing > 0 else -0.03)
    P.add(prism("emblem", outer, t0, t0 + 0.03), "paint", WHITE, lod)
    t1 = y + (0.03 if facing > 0 else -0.045)
    P.add(prism("emblem_in", inner, t1, t1 + 0.015), "paint", LIVERY_ACCENT, 0)


# --- the canopy --------------------------------------------------------------------------------------------

def canopy(islands):
    long = islands * ISLAND_GAP + 1.5
    P = Piece("canopy_%d" % islands, "Canopy, %d islands (%.1f x %.0f m)" % (islands, long, CANOPY_DEEP))
    x0, x1, y0, y1 = -long / 2, long / 2, ENTRY_LANE, ENTRY_LANE + CANOPY_DEEP
    top = SOFFIT + FASCIA
    c = 0.9
    # The fascia: a deep band all round in the livery, chamfered corners, white capping, accent stripe.
    outer = chamfered(x0, y0, x1, y1, c)
    inner = chamfered(x0 + 0.12, y0 + 0.12, x1 - 0.12, y1 - 0.12, c * 0.9)
    for k in range(8):
        a, b = outer[k], outer[(k + 1) % 8]
        ia, ib = inner[k], inner[(k + 1) % 8]
        ring = [a, b, ib, ia]
        P.add(plan_slab("fascia", ring, SOFFIT, top - 0.12), "paint", LIVERY_MAIN, 2)
        P.add(plan_slab("capping", ring, top - 0.12, top), "metal", WHITE, 2)
        # (the accent stripe, a hair proud, along the band's foot)
        d = Vector((b[0] - a[0], b[1] - a[1], 0.0))
        if d.length < 1e-3:
            continue
        n = Vector((d.y, -d.x, 0.0)).normalized() * 0.015
        sa, sb = (a[0] + n.x, a[1] + n.y), (b[0] + n.x, b[1] + n.y)
        P.add(plan_slab("stripe", [sa, sb, b, a], SOFFIT + 0.12, SOFFIT + 0.3), "paint", LIVERY_ACCENT, 1)
    # The roof deck (shallow, falling to the back) and the soffit.
    P.add(plan_slab("deck", inner, top - 0.35, top - 0.2), "metal", (0.4, 0.41, 0.42), 2)
    P.add(plan_slab("soffit", inner, SOFFIT, SOFFIT + 0.05), "panel", SOFFIT_WHITE, 2)
    # (soffit panel joints as faint lines, then the flush light panels over each island and lane)
    for k in range(1, int(long / 1.5)):
        x = x0 + k * long / int(long / 1.5)
        P.b("joint", x - 0.01, x + 0.01, y0 + 0.2, y1 - 0.2, SOFFIT - 0.006, SOFFIT, "metal", (0.5, 0.5, 0.5), 0)
    for i in range(islands):
        u = (i - (islands - 1) / 2) * ISLAND_GAP
        for v in (y0 + 1.8, (y0 + y1) / 2, y1 - 1.8):
            P.b("light", u - 1.3, u + 1.3, v - 0.4, v + 0.4, SOFFIT - 0.03, SOFFIT, "lamp", NIGHT_SOFFIT, 1)
    # Emblems on the fascia's front and both ends.
    emblem(P, x1 - 1.9, y0 - 0.01, SOFFIT + FASCIA * 0.5, 0.75, -1)
    emblem(P, x0 + 1.9, y0 - 0.01, SOFFIT + FASCIA * 0.5, 0.75, -1)
    # The columns: square clad casings in line with the islands' ends, out of the lanes, livery kick plate.
    rows = 3 if long > 16.0 else 2
    for r in range(rows):
        u = x0 + 1.2 + (long - 2.4) * r / (rows - 1)
        for v in (y0 + 2.0, y1 - 2.0):
            P.b("column", u - 0.2, u + 0.2, v - 0.2, v + 0.2, FOOT, SOFFIT, "panel", (0.62, 0.63, 0.63), 2, bevel=0.03)
            P.b("kick", u - 0.215, u + 0.215, v - 0.215, v + 0.215, 0.0, 1.1, "paint", LIVERY_MAIN, 1)
            P.b("kickband", u - 0.22, u + 0.22, v - 0.22, v + 0.22, 1.1, 1.22, "paint", LIVERY_ACCENT, 0)
    # (the downpipes in two of the back columns: a slim pipe and a shoe)
    for u in (x0 + 1.2, x1 - 1.2):
        P.b("downpipe", u - 0.05, u + 0.05, y1 - 1.75, y1 - 1.68, 0.1, SOFFIT, "metal", (0.5, 0.5, 0.5), 0)
    return P


# --- a pump island -------------------------------------------------------------------------------------------

def dispenser(P, u, v):
    """A modern multi-product dispenser at (u, v): slim cabinet, livery header with a lit band, a screen and a
    nozzle row each side, hoses hung in a loop."""
    w, d = 0.95, 0.5
    P.b("plinth", u - w / 2 - 0.03, u + w / 2 + 0.03, v - d / 2 - 0.03, v + d / 2 + 0.03, 0.16, 0.3, "metal", STEEL, 1)
    P.b("cabinet", u - w / 2, u + w / 2, v - d / 2, v + d / 2, 0.3, 1.75, "panel", PUMP_GREY, 2, bevel=0.02)
    P.b("column", u - 0.12, u + 0.12, v - 0.14, v + 0.14, 1.75, 2.15, "panel", PUMP_GREY, 1)
    P.b("header", u - w / 2 - 0.02, u + w / 2 + 0.02, v - d / 2 - 0.02, v + d / 2 + 0.02, 2.15, 2.5, "paint", LIVERY_MAIN, 2, bevel=0.015)
    P.b("header_band", u - w / 2 - 0.025, u + w / 2 + 0.025, v - d / 2 - 0.025, v + d / 2 + 0.025, 2.2, 2.27, "lamp", NIGHT_SIGN, 1)
    for s in (-1, 1):
        f = v + s * d / 2
        # (screen, card reader, keypad, the grade buttons' strip, the three holsters and their nozzles)
        P.b("screen", u - 0.32, u + 0.32, f - 0.006 if s < 0 else f, f + 0.006 if s > 0 else f, 1.28, 1.62, "glass", SCREEN, 0)
        P.b("reader", u + 0.12, u + 0.3, f - 0.03 if s < 0 else f, f + 0.03 if s > 0 else f, 0.95, 1.18, "metal", DARK, 0)
        P.b("keys", u - 0.3, u + 0.05, f - 0.01 if s < 0 else f, f + 0.01 if s > 0 else f, 1.0, 1.18, "metal", (0.2, 0.2, 0.21), 0)
        P.b("grades", u - 0.4, u + 0.4, f - 0.008 if s < 0 else f, f + 0.008 if s > 0 else f, 0.82, 0.9, "paint", LIVERY_ACCENT, 0)
        for k, col in enumerate(((0.02, 0.25, 0.04), DARK, (0.02, 0.25, 0.04))):
            x = u - 0.3 + k * 0.3
            y = f + s * 0.07
            P.b("holster", x - 0.06, x + 0.06, min(f, y), max(f, y), 0.55, 0.78, "metal", DARK, 0)
            noz = sweep("nozzle", [(x, y, 0.75), (x, y + s * 0.05, 0.62), (x, y + s * 0.09, 0.5)], 0.028, 6)
            P.add(noz, "paint", col, 0)
            # (the hose: up from the nozzle and back into the cabinet's top side, sagging)
            pts = [(x, y + s * 0.02, 0.78), (x, y + s * 0.2, 1.0), (x + 0.02, y + s * 0.26, 1.5), (x + 0.02, f + s * 0.12, 1.85), (x, f + s * 0.02, 1.95)]
            P.add(sweep("hose", pts, 0.017, 6), "rubber", RUBBER, 0)


def island():
    P = Piece("island", "Pump island (%.1f m)" % ISLAND_LONG)
    a0, a1 = -ISLAND_LONG / 2, ISLAND_LONG / 2
    v0, v1 = -0.6, 0.6
    # (the island: a kerbed concrete slab with rounded-off ends, its kerb faces painted yellow at the ends)
    ring = [(a0 + 0.5, v0), (a1 - 0.5, v0), (a1, v0 + 0.35), (a1, v1 - 0.35), (a1 - 0.5, v1), (a0 + 0.5, v1), (a0, v1 - 0.35), (a0, v0 + 0.35)]
    P.add(plan_slab("island", ring, -0.1, 0.16), "concrete", CONCRETE, 2)
    for sx in (-1, 1):
        e = [(sx * (ISLAND_LONG / 2 - 0.5), v0 - 0.01), (sx * (ISLAND_LONG / 2 + 0.01), v0 + 0.34), (sx * (ISLAND_LONG / 2 + 0.01), v1 - 0.34), (sx * (ISLAND_LONG / 2 - 0.5), v1 + 0.01)]
        if sx < 0:
            e = list(reversed(e))
        P.add(plan_slab("kerbpaint", e + [(sx * (ISLAND_LONG / 2 - 0.6), 0.0)], 0.0, 0.17), "paint", KERB_YELLOW, 1)
        # (the crash hoop at each end: a bent galvanised tube painted yellow)
        x = sx * (ISLAND_LONG / 2 - 0.3)
        pts = [(x, -0.38, 0.1), (x, -0.38, 0.75), (x, -0.25, 0.9), (x, 0.25, 0.9), (x, 0.38, 0.75), (x, 0.38, 0.1)]
        P.add(sweep("hoop", pts, 0.045, 8), "paint", HOOP, 1)
    for s in (-1, 1):
        dispenser(P, s * 1.05, 0.0)
    # (a bin and a screenwash and towel stand at one end, in the island's middle)
    P.add(lathe("bin", [(0.2, 0.16), (0.21, 0.9), (0.22, 0.95), (0.0, 0.98)], seg=10, at=(-1.85, 0.0, 0.0)), "paint", (0.08, 0.09, 0.09), 0)
    P.b("towels", 1.75, 2.0, -0.15, 0.15, 0.16, 1.1, "metal", GALV, 0)
    P.b("towelbox", 1.72, 2.03, -0.18, 0.18, 1.1, 1.45, "paint", LIVERY_MAIN, 0)
    P.b("bucket", 1.7, 2.05, -0.2, 0.2, 0.16, 0.45, "paint", (0.04, 0.12, 0.3), 0)
    return P


# --- the kiosk -------------------------------------------------------------------------------------------------

KIOSK_TOP = {"glass": 4.0, "brick": 3.0}


def kiosk_bay(style, kind):
    """One 2.5 m bay of the kiosk's row: front at y = 0 facing the road (-y), back wall at KIOSK_DEEP, roof and
    fascia over; kind 'glass', 'door', 'atm' or 'end' (an end wall, its outer face at x = 0 facing -x)."""
    W = KIOSK_BAY
    D = KIOSK_DEEP
    P = Piece("kiosk_%s_%s" % (style, kind), "Kiosk %s: %s bay" % (style, kind))
    top = KIOSK_TOP[style]
    if kind == "end":
        t = 0.3
        if style == "glass":
            P.b("endwall", 0.0, t, 0.0, D, FOOT, top - 0.6, "render", RENDER, 2)
            P.b("endfascia", -0.12, t, -0.12, D + 0.12, top - 0.6, top, "paint", LIVERY_MAIN, 2)
            P.b("endcap", -0.13, t, -0.13, D + 0.13, top - 0.06, top + 0.02, "metal", WHITE, 1)
            # (a fire exit door, its push bar sign, a wall light)
            P.b("exit", -0.04, 0.0, D - 2.2, D - 1.2, 0.0, 2.1, "paint", (0.25, 0.26, 0.27), 1)
            P.b("lamp", -0.12, 0.0, D - 1.0, D - 0.75, 2.4, 2.55, "lamp", NIGHT_SIGN, 0)
            P.b("plinth", -0.02, t, 0.0, D, FOOT, 0.35, "metal", (0.2, 0.2, 0.21), 1)
        else:
            P.b("endwall", 0.0, t, 0.0, D, FOOT, top, "brick", BRICK, 2)
            # (the gable to the ridge, tiled verge)
            ridge = top + D * 0.5 * math.tan(math.radians(35))
            gable = prism("gable", [(0.0, top), (D, top), (D * 0.5, ridge)], 0.0, t)
            gable.matrix_world = SWAP_XY
            P.add(gable, "brick", BRICK, 2)
            P.b("exit", -0.04, 0.0, D - 2.2, D - 1.2, 0.0, 2.1, "paint", (0.04, 0.09, 0.05), 1)
            P.b("lamp", -0.12, 0.0, D - 1.0, D - 0.75, 2.3, 2.45, "lamp", NIGHT_SIGN, 0)
        return P
    # The back wall and the roof slab over the bay.
    if style == "glass":
        P.b("back", 0.0, W, D - 0.3, D, FOOT, top - 0.6, "render", RENDER, 2)
        P.b("roof", 0.0, W, 0.0, D, top - 0.7, top - 0.5, "roof_flat", (0.2, 0.2, 0.2), 2)
        P.b("fascia", 0.0, W, -0.12, 0.0, top - 0.6, top, "paint", LIVERY_MAIN, 2)
        P.b("fascia_back", 0.0, W, D, D + 0.12, top - 0.6, top, "paint", LIVERY_MAIN, 2)
        P.b("capping", 0.0, W, -0.13, D + 0.13, top - 0.06, top + 0.02, "metal", WHITE, 1)
        P.b("stripe", 0.0, W, -0.135, -0.12, top - 0.55, top - 0.42, "paint", LIVERY_ACCENT, 1)
        P.b("plinth", 0.0, W, 0.0, 0.12, FOOT, 0.35, "metal", (0.2, 0.2, 0.21), 1)
        head = top - 0.6
        # (a canopy lip over the front, the shop's glow behind the glass after dark)
        P.b("lip", 0.0, W, -1.0, 0.0, head - 0.15, head, "metal", (0.55, 0.56, 0.56), 1)
        P.b("lipglow", 0.2, W - 0.2, -0.9, -0.2, head - 0.16, head - 0.15, "lamp", NIGHT_SOFFIT, 0)
        if kind in ("glass", "door", "atm"):
            if kind == "atm":
                P.b("panel", 0.0, W, 0.0, 0.25, 0.35, head, "render", RENDER, 2)
                P.b("atm_surround", 0.7, 1.8, -0.05, 0.0, 0.8, 2.1, "metal", (0.5, 0.51, 0.52), 1)
                P.b("atm_screen", 0.95, 1.55, -0.07, -0.05, 1.35, 1.7, "glass", SCREEN, 0)
                P.b("atm_hood", 0.7, 1.8, -0.3, 0.0, 2.1, 2.2, "metal", (0.5, 0.51, 0.52), 0)
                P.b("atm_keys", 1.05, 1.45, -0.18, -0.05, 1.05, 1.15, "metal", DARK, 0)
            else:
                P.b("glass", 0.0, W, 0.05, 0.08, 0.35, head, "glass", GLASS, 2)
                P.b("glow", 0.1, W - 0.1, 0.12, 0.13, 0.4, head - 0.1, "lamp", NIGHT_SHOP, 1)
                for x in (0.0, W):
                    P.b("mullion", x - 0.04, x + 0.04, 0.0, 0.12, 0.35, head, "metal", FRAME, 1)
                P.b("transom", 0.0, W, 0.0, 0.12, head - 0.08, head, "metal", FRAME, 1)
                P.b("sill", 0.0, W, -0.02, 0.12, 0.3, 0.38, "metal", FRAME, 1)
                if kind == "door":
                    # (sliding doors: a frame round the opening, the leaves, a mat well and a night-pay hatch)
                    P.b("doorhead", 0.15, W - 0.15, -0.03, 0.12, 2.3, 2.45, "metal", FRAME, 1)
                    for x in (0.15, W / 2 - 0.02, W / 2 + 0.02, W - 0.15):
                        P.b("doorstile", x - 0.03, x + 0.03, -0.03, 0.06, 0.0, 2.3, "metal", FRAME, 0)
                    P.b("doorglass", 0.2, W - 0.2, 0.02, 0.04, 0.0, 2.28, "glass", GLASS, 1)
                    P.b("mat", 0.2, W - 0.2, -1.2, 0.0, 0.0, 0.015, "rubber", RUBBER, 0)
                else:
                    # (posters in the glass, a counter's shadow line, a newspaper rack below)
                    P.b("poster", 0.4, 1.1, 0.04, 0.05, 1.1, 1.9, "paint", LIVERY_ACCENT, 0)
                    P.b("poster2", 1.4, 2.1, 0.04, 0.05, 1.3, 1.8, "paint", WHITE, 0)
        return P
    # (brick: a low brick shop under a tiled pitched roof, timber shopfront, a hanging sign bracket)
    ridge = top + D * 0.5 * math.tan(math.radians(35))
    P.b("back", 0.0, W, D - 0.3, D, FOOT, top, "brick", BRICK, 2)
    over = 0.35
    for s0, s1 in ((-over, D * 0.5), (D * 0.5, D + over)):
        z0 = top - over * math.tan(math.radians(35)) if s0 < 0 else ridge
        z1 = ridge if s0 < 0 else top - over * math.tan(math.radians(35))
        y0, y1 = s0, s1
        pts = [(y0, z0), (y1, z1), (y1, z1 + 0.12), (y0, z0 + 0.12)]
        slab = prism("roof", [(y, z) for y, z in pts], 0.0, W)
        slab.matrix_world = SWAP_XY
        P.add(slab, "tile", TILE, 2)
    P.b("ridge", 0.0, W, D * 0.5 - 0.12, D * 0.5 + 0.12, ridge, ridge + 0.18, "tile", (0.08, 0.04, 0.03), 1)
    P.b("fascia", 0.0, W, -over, -over + 0.04, top - 0.45, top - 0.15, "paint", WHITE, 1)
    P.b("gutter", 0.0, W, -over - 0.12, -over, top - 0.35, top - 0.22, "metal", (0.03, 0.03, 0.03), 1)
    head = 2.5
    P.b("signboard", 0.0, W, -0.08, 0.0, head + 0.05, top - 0.05, "paint", LIVERY_MAIN, 2)
    P.b("signline", 0.0, W, -0.09, -0.08, head + 0.12, head + 0.18, "paint", LIVERY_ACCENT, 1)
    if kind == "atm":
        P.b("wall", 0.0, W, 0.0, 0.3, FOOT, head + 0.05, "brick", BRICK, 2)
        P.b("atm_surround", 0.7, 1.8, -0.05, 0.0, 0.8, 2.1, "metal", (0.5, 0.51, 0.52), 1)
        P.b("atm_screen", 0.95, 1.55, -0.07, -0.05, 1.35, 1.7, "glass", SCREEN, 0)
        return P
    P.b("stallriser", 0.0, W, 0.0, 0.3, FOOT, 0.65, "brick", BRICK, 2)
    P.b("glass", 0.0, W, 0.12, 0.15, 0.65, head, "glass", GLASS, 2)
    P.b("glow", 0.1, W - 0.1, 0.18, 0.19, 0.7, head - 0.05, "lamp", NIGHT_SHOP, 1)
    timber = (0.04, 0.09, 0.05)
    for x in (0.05, W / 2, W - 0.05):
        P.b("mullion", x - 0.05, x + 0.05, 0.05, 0.2, 0.65, head, "paint", timber, 1)
    P.b("transom", 0.0, W, 0.05, 0.2, head - 0.1, head + 0.05, "paint", timber, 1)
    P.b("sill", 0.0, W, -0.04, 0.2, 0.6, 0.68, "paint", timber, 1)
    if kind == "door":
        P.b("door", 0.7, 1.8, 0.1, 0.16, 0.0, 2.2, "paint", timber, 1)
        P.b("doorglass", 0.82, 1.68, 0.08, 0.1, 0.9, 2.05, "glass", GLASS, 0)
        P.b("step", 0.6, 1.9, -0.35, 0.1, -0.1, 0.12, "concrete", CONCRETE, 0)
    return P


# --- the price pole, the air and water machine, the jet wash, clutter --------------------------------------------

def price_pole():
    P = Piece("price_pole", "Price pole (6 m)")
    w, d, H = 1.5, 0.36, 6.0
    P.b("plinth", -w / 2 - 0.15, w / 2 + 0.15, -d / 2 - 0.15, d / 2 + 0.15, FOOT, 0.45, "concrete", CONCRETE, 2)
    P.b("body", -w / 2, w / 2, -d / 2, d / 2, 0.45, H, "paint", LIVERY_MAIN, 2, bevel=0.03)
    P.b("cap", -w / 2 - 0.03, w / 2 + 0.03, -d / 2 - 0.03, d / 2 + 0.03, H, H + 0.08, "metal", WHITE, 1)
    P.b("band", -w / 2 - 0.005, w / 2 + 0.005, -d / 2 - 0.005, d / 2 + 0.005, H - 1.75, H - 1.6, "paint", LIVERY_ACCENT, 1)
    for s in (-1, 1):
        f = s * (d / 2 + 0.01)
        emblem(P, 0.0, f, H - 0.85, 0.9, s, 1)
        # (four price rows: a dark panel each, the grade's colour chip, amber digits after dark)
        for k in range(4):
            z = H - 2.25 - k * 0.72
            P.b("panel", -0.62, 0.62, min(f, f + s * 0.02), max(f, f + s * 0.02), z - 0.28, z + 0.28, "glass", SCREEN, 1)
            chip = ((0.02, 0.25, 0.04), DARK, (0.02, 0.15, 0.35), (0.35, 0.04, 0.03))[k]
            P.b("chip", -0.58, -0.38, min(f, f + s * 0.03), max(f, f + s * 0.03), z - 0.2, z + 0.2, "paint", chip, 0)
            P.b("digits", -0.25, 0.55, min(f, f + s * 0.025), max(f, f + s * 0.025), z - 0.17, z + 0.17, "lamp", NIGHT_DIGITS, 0)
        # (the shop's opening-hours plate and a car wash plate low down)
        P.b("plate", -0.62, 0.62, min(f, f + s * 0.02), max(f, f + s * 0.02), 0.75, 1.15, "paint", WHITE, 0)
    return P


def air_water():
    P = Piece("air_water", "Air and water machine")
    P.b("plinth", -0.5, 0.5, -0.4, 0.4, -0.1, 0.12, "concrete", CONCRETE, 1)
    P.b("cabinet", -0.32, 0.32, -0.22, 0.22, 0.12, 1.45, "paint", (0.65, 0.66, 0.66), 2, bevel=0.03)
    P.b("head", -0.34, 0.34, -0.24, 0.24, 1.45, 1.75, "paint", (0.04, 0.18, 0.45), 1, bevel=0.02)
    P.b("screen", -0.15, 0.15, -0.25, -0.22, 1.1, 1.35, "glass", SCREEN, 0)
    P.b("coins", 0.17, 0.27, -0.25, -0.22, 1.0, 1.3, "metal", DARK, 0)
    for s, col in ((-1, (0.6, 0.05, 0.03)), (1, (0.02, 0.15, 0.4))):
        x = s * 0.34
        P.b("hook", x - 0.03 if s < 0 else x, x if s < 0 else x + 0.03, -0.08, 0.08, 0.9, 1.05, "metal", DARK, 0)
        pts = [(x + s * 0.03, 0.0, 1.0)] + [(x + s * (0.1 + 0.12 * math.sin(a)), 0.12 * math.cos(a), 0.65 + 0.3 * math.cos(a)) for a in [k * 0.6 for k in range(11)]]
        P.add(sweep("hose", pts, 0.012, 6), "rubber", col, 0)
    P.add(lathe("post", [(0.07, -0.3), (0.07, 0.95), (0.08, 1.0), (0.0, 1.02)], seg=8, at=(0.75, -0.25, 0.0)), "paint", KERB_YELLOW, 0)
    return P


def jet_wash():
    P = Piece("jet_wash", "Two-bay jet wash (8 x 5.5 m)")
    L, D, H = 8.0, 5.5, 4.2
    P.b("apron", 0.0, L, 0.0, D, -0.05, 0.03, "concrete", (0.3, 0.3, 0.29), 2)
    P.b("drain", 0.3, L - 0.3, D * 0.5 - 0.08, D * 0.5 + 0.08, 0.03, 0.035, "metal", DARK, 0)
    for x in (0.0, L / 2, L):
        for y in (0.3, D - 0.3):
            P.b("post", x - 0.1, x + 0.1, y - 0.1, y + 0.1, FOOT, H, "metal", GALV, 2)
        # (the side screens: translucent sheet panels between the posts, on the outer frames and the middle one)
        P.b("screen", x - 0.03, x + 0.03, 0.4, D - 0.4, 0.6, H - 0.4, "glass", (0.25, 0.3, 0.33), 1)
        P.b("rail", x - 0.06, x + 0.06, 0.3, D - 0.3, H - 0.4, H - 0.25, "metal", GALV, 1)
    P.b("roof", -0.3, L + 0.3, -0.3, D + 0.3, H, H + 0.15, "roof", (0.45, 0.46, 0.47), 2)
    P.b("fascia", -0.32, L + 0.32, -0.32, -0.3, H - 0.45, H + 0.15, "paint", LIVERY_MAIN, 2)
    P.b("fasciastripe", -0.33, L + 0.33, -0.33, -0.32, H - 0.4, H - 0.32, "paint", LIVERY_ACCENT, 1)
    for k, x in enumerate((L * 0.25, L * 0.75)):
        # (each bay's control post at the back, its lance on a boom, the hose hanging)
        P.b("control", x - 0.3, x + 0.3, D - 0.6, D - 0.3, 0.0, 1.6, "paint", (0.65, 0.66, 0.66), 1, bevel=0.02)
        P.b("controlhead", x - 0.32, x + 0.32, D - 0.62, D - 0.28, 1.6, 1.85, "paint", LIVERY_MAIN, 1)
        P.b("boom", x - 0.04, x + 0.04, 0.8, D - 0.4, H - 0.6, H - 0.5, "metal", GALV, 0)
        pts = [(x, 1.8, H - 0.55), (x + 0.15, 1.8, H - 1.6), (x + 0.3, 1.9, 1.2), (x + 0.5, 2.0, 0.9)]
        P.add(sweep("hose", pts, 0.015, 6), "rubber", RUBBER, 0)
        P.add(sweep("lance", [(x + 0.5, 2.0, 0.9), (x + 0.9, 2.05, 0.6)], 0.018, 6), "metal", DARK, 0)
        P.b("lamp", x - 0.5, x + 0.5, 1.5, 2.0, H - 0.03, H, "lamp", NIGHT_SOFFIT, 0)
    return P


def clutter():
    P = Piece("clutter", "Forecourt clutter (by the kiosk)")
    # The gas bottle cage: galvanised mesh, a lid, blue butane and red propane bottles inside, a green patio one.
    x0, x1, y0, y1, h = 0.0, 1.8, 0.0, 0.9, 1.9
    P.b("cagebase", x0, x1, y0, y1, 0.0, 0.08, "metal", GALV, 1)
    P.b("cagetop", x0, x1, y0, y1, h - 0.06, h, "metal", GALV, 1)
    for x, y in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):
        P.b("cagepost", x - 0.03, x + 0.03, y - 0.03, y + 0.03, 0.0, h, "metal", GALV, 1)
    P.b("cagemesh", x0, x1, y0 - 0.005, y0 + 0.005, 0.08, h - 0.06, "metal", (0.35, 0.36, 0.36), 0)
    P.b("cagesign", 0.6, 1.2, y0 - 0.02, y0 - 0.01, 1.4, 1.7, "paint", KERB_YELLOW, 0)
    for k, (bx, col) in enumerate(((0.3, GAS_BLUE), (0.75, GAS_BLUE), (1.2, GAS_RED), (1.55, GAS_GREEN))):
        P.add(lathe("bottle", [(0.15, 0.08), (0.155, 0.75), (0.1, 0.86), (0.05, 0.9), (0.05, 0.98), (0.0, 0.99)], seg=10, at=(bx, 0.45, 0.0)), "paint", col, 1 if k < 2 else 0)
    # The ice chest: a white chest freezer with a lid, a livery band.
    ix = 2.2
    P.b("ice", ix, ix + 1.3, 0.1, 0.85, 0.0, 0.95, "paint", ICE_WHITE, 1, bevel=0.03)
    P.b("icelid", ix - 0.02, ix + 1.32, 0.08, 0.87, 0.95, 1.05, "paint", (0.04, 0.25, 0.55), 1)
    P.b("iceband", ix - 0.005, ix + 1.305, 0.095, 0.1, 0.55, 0.7, "paint", LIVERY_MAIN, 0)
    # Logs and coal bags on a pallet.
    px = 3.9
    P.b("pallet", px, px + 1.2, 0.0, 1.0, 0.0, 0.14, "paint", (0.3, 0.2, 0.1), 1)
    for k in range(6):
        bx = px + 0.05 + (k % 3) * 0.37
        by = 0.08 + (k // 3) * 0.45
        P.b("bag", bx, bx + 0.34, by, by + 0.42, 0.14, 0.62, "paint", SACK if k % 2 else LOG_BROWN, 0, bevel=0.04)
    P.b("bagtop", px + 0.1, px + 1.1, 0.1, 0.9, 0.62, 0.98, "paint", LOG_BROWN, 0, bevel=0.05)
    # The A-board by the door, two planters, a litter bin.
    ax = 5.6
    for s in (-1, 1):
        board = prism("aboard", [(ax, 0.0), (ax + 0.6, 0.0), (ax + 0.6, 0.9), (ax, 0.9)], 0.0, 0.03)
        board.matrix_world = Matrix.Translation((0, 0.35, 0)) @ Matrix.Translation((ax + 0.3, 0, 0)) @ Matrix.Rotation(s * 0.18, 4, "X") @ Matrix.Translation((-ax - 0.3, 0, 0))
        P.add(board, "paint", LIVERY_MAIN if s < 0 else WHITE, 0)
    for k, x in enumerate((6.6, 8.0)):
        P.b("planter", x, x + 0.9, 0.0, 0.5, 0.0, 0.45, "paint", (0.12, 0.12, 0.12), 1)
        P.b("shrub", x + 0.05, x + 0.85, 0.05, 0.45, 0.45, 0.85, "plant", PLANT, 0, bevel=0.12)
    P.add(lathe("bin", [(0.25, 0.0), (0.27, 0.95), (0.29, 1.0), (0.0, 1.05)], seg=10, at=(9.3, 0.3, 0.0)), "paint", (0.04, 0.05, 0.05), 1)
    return P


def rules_plate():
    """The forecourt rules plate and an emergency stop post, by the canopy's back column."""
    P = Piece("rules", "Rules plate and emergency stop")
    P.add(lathe("post", [(0.04, FOOT), (0.04, 2.2), (0.0, 2.22)], seg=6), "metal", GALV, 1)
    P.b("plate", -0.4, 0.4, -0.06, -0.04, 1.4, 2.1, "paint", WHITE, 1)
    P.b("plateband", -0.4, 0.4, -0.065, -0.06, 1.95, 2.1, "paint", (0.5, 0.03, 0.02), 0)
    P.b("stopbox", -0.12, 0.12, -0.1, -0.04, 1.0, 1.25, "paint", KERB_YELLOW, 0)
    P.b("stopbutton", -0.05, 0.05, -0.14, -0.1, 1.07, 1.17, "paint", (0.6, 0.02, 0.02), 0)
    return P


# --- whole stations (for the sheet: laid as the game lays them) ----------------------------------------------

class Station:
    """A whole forecourt from the pieces, as WorldRideTownLife.cpp lays one, for the review sheet."""
    def __init__(self, name, label, islands, style, livery, with_jet=True):
        self.name, self.label = name, label
        self.islands, self.style, self.livery = islands, style, livery
        self.parts = []
        self.with_jet = with_jet


def place(S, P, M, livery):
    """Copies piece P into the station S at matrix M (the piece's own objects are kept for its tri count)."""
    for p in P.parts:
        ob = p.obj.copy()
        ob.data = p.obj.data.copy()
        ob.matrix_world = M @ p.obj.matrix_world
        bpy.context.scene.collection.objects.link(ob)
        S.parts.append(Part(ob, p.surface, p.rgb, p.lod))


def stretch_x(x, sx):
    return Matrix.Translation((x, 0, 0)) @ Matrix.Diagonal((sx, 1, 1, 1))


def assemble(S, kit):
    long = S.islands * ISLAND_GAP + 1.5
    place(S, kit["canopy_%d" % S.islands], Matrix(), S.livery)
    vm = ENTRY_LANE + CANOPY_DEEP / 2
    for k in range(S.islands):
        u = (k - (S.islands - 1) / 2) * ISLAND_GAP
        place(S, kit["island"], Matrix.Translation((u, vm, 0)), S.livery)
    # The kiosk row behind the canopy: the garage (left, as the game's, not drawn here) and the shop.
    kv = ENTRY_LANE + CANOPY_DEEP + KIOSK_GAP
    row = min(long, 15.5)
    k0, k1 = -row / 2 + 7.7, row / 2   # (the garage takes 7.5 m and a gap at the row's left end)
    n = max(2, round((k1 - k0 - 0.6) / KIOSK_BAY))
    bw = (k1 - k0 - 0.6) / n
    kinds = ["glass"] * n
    kinds[n // 2] = "door"
    if n >= 3:
        kinds[-1] = "atm"
    for i, kind in enumerate(kinds):
        x = k0 + 0.3 + i * bw
        place(S, kit["kiosk_%s_%s" % (S.style, kind)], Matrix.Translation((0, kv, 0)) @ stretch_x(x, bw / KIOSK_BAY), S.livery)
    place(S, kit["kiosk_%s_end" % S.style], Matrix.Translation((k0, kv, 0)) @ Matrix.Translation((0.3, 0, 0)) @ Matrix.Diagonal((-1, 1, 1, 1)), S.livery)
    place(S, kit["kiosk_%s_end" % S.style], Matrix.Translation((k1 - 0.3, kv, 0)), S.livery)
    # (the garage unit, a stand-in box in grey: it stays the game's own model)
    S.parts.append(Part(box("garage_standin", (-row / 2 + 3.75, kv + 3.25, 2.25), (7.5, 6.5, 4.5)), "panel", (0.42, 0.43, 0.44), 2))
    S.parts.append(Part(box("garage_door", (-row / 2 + 3.75, kv - 0.01, 1.75), (4.0, 0.05, 3.5)), "shutter", (0.55, 0.56, 0.56), 2))
    place(S, kit["price_pole"], Matrix.Translation((long / 2 + 4.0, 1.6, 0)) @ Matrix.Rotation(math.radians(90), 4, "Z"), S.livery)
    place(S, kit["rules"], Matrix.Translation((-long / 2 + 1.2, ENTRY_LANE + CANOPY_DEEP - 2.0 - 0.25, 0)), S.livery)
    place(S, kit["air_water"], Matrix.Translation((long / 2 + 2.5, ENTRY_LANE + CANOPY_DEEP + 1.0, 0)), S.livery)
    place(S, kit["clutter"], Matrix.Translation((k0 + 0.6, kv - 1.1, 0)) @ Matrix.Diagonal((0.9, 1, 1, 1)), S.livery)
    if S.with_jet:
        place(S, kit["jet_wash"], Matrix.Translation((long / 2 + 2.0, ENTRY_LANE + CANOPY_DEEP + 3.0, 0)), S.livery)
    S.size = (long + 16.0, kv + KIOSK_DEEP + 2.0)


# --- build ---------------------------------------------------------------------------------------------------------

def clear():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for me in list(bpy.data.meshes):
        bpy.data.meshes.remove(me)


def triangle_counts(parts):
    dg = bpy.context.evaluated_depsgraph_get()
    counts = [0, 0, 0]
    for p in parts:
        ev = p.obj.evaluated_get(dg)
        me = ev.to_mesh()
        me.calc_loop_triangles()
        n = len(me.loop_triangles)
        ev.to_mesh_clear()
        for lod in range(p.lod + 1):
            counts[lod] += n
    return counts


def build_kit():
    pieces = [canopy(2), canopy(3), island(), price_pole(), air_water(), jet_wash(), clutter(), rules_plate()]
    for style in ("glass", "brick"):
        for kind in ("glass", "door", "atm", "end"):
            pieces.append(kiosk_bay(style, kind))
    kit = {}
    for P in pieces:
        P.tris = triangle_counts(P.parts)
        kit[P.name] = P
        print("%-20s LOD0 %6d  LOD1 %6d  LOD2 %6d" % (P.name, P.tris[0], P.tris[1], P.tris[2]))
    return kit


STATIONS = [
    ("FC_town_glass", "Town station: 3 islands, glass kiosk, jet wash", 3, "glass", 0, True),
    ("FC_town_glass_b", "Town station: 2 islands, glass kiosk", 2, "glass", 1, False),
    ("FC_village_brick", "Village station: 2 islands, older brick kiosk", 2, "brick", 2, False),
    ("FC_main_road", "Main road station: 3 islands, glass kiosk, jet wash", 3, "glass", 3, True),
    ("FC_village_brick_b", "Village station: 2 islands, brick kiosk", 2, "brick", 4, False),
    ("FC_edge_of_town", "Edge of town: 3 islands, glass kiosk", 3, "glass", 5, True),
]


def build_stations(kit):
    out = []
    for name, label, islands, style, liv, jet in STATIONS:
        S = Station(name, label, islands, style, LIVERIES[liv], jet)
        assemble(S, kit)
        S.tris = triangle_counts(S.parts)
        print("%-20s LOD0 %6d  LOD1 %6d  LOD2 %6d  (%s)" % (name, S.tris[0], S.tris[1], S.tris[2], LIVERIES[liv][0]))
        out.append(S)
    return out


# --- export for the game ------------------------------------------------------------------------------------

SEC_PLAIN, SEC_ROOFING, SEC_BRICK, SEC_RENDER = 0, 2, 3, 4
SLOTS = {LIVERY_MAIN: 1, LIVERY_ACCENT: 2, NIGHT_SOFFIT: 10, NIGHT_SHOP: 11, NIGHT_DIGITS: 12, NIGHT_SIGN: 13}


def part_class(p):
    """The mesh a part goes in (World/WorldRideChurches.h's sections) and its alpha: the roofing's kind on a roof
    (0.5 clay tiles, 1 sheet or felt), dirt at the foot on brick and render, 1 on the plain mesh."""
    s = p.surface
    if s == "tile":
        return SEC_ROOFING, 0.5
    if s in ("roof", "roof_flat"):
        return SEC_ROOFING, 1.0
    if s == "brick":
        return SEC_BRICK, 0.06
    if s in ("render", "concrete"):
        return SEC_RENDER, 0.08
    return SEC_PLAIN, 1.0


def piece_bytes(P):
    import struct
    import numpy as np
    dg = bpy.context.evaluated_depsgraph_get()
    pos, nrm, part_of = [], [], []
    for i, part in enumerate(P.parts):
        ev = part.obj.evaluated_get(dg)
        me = ev.to_mesh()
        me.calc_loop_triangles()
        mw = part.obj.matrix_world
        nw = mw.to_3x3().inverted().transposed()
        flip = mw.to_3x3().determinant() < 0
        co = [mw @ v.co for v in me.vertices]
        cn = [None] * len(me.loops)
        for k, l in enumerate(me.corner_normals):
            n = nw @ l.vector
            cn[k] = n.normalized() if n.length > 1e-9 else n
        for lt in me.loop_triangles:
            vs, ls = list(lt.vertices), list(lt.loops)
            if flip:
                vs, ls = [vs[0], vs[2], vs[1]], [ls[0], ls[2], ls[1]]
            for v, l in zip(vs, ls):
                pos.append(tuple(co[v])); nrm.append(tuple(cn[l])); part_of.append(i)
        ev.to_mesh_clear()
    pos, nrm, part_of = np.array(pos), np.array(nrm), np.array(part_of)
    q = np.round(pos * 100.0).astype(np.int64)
    qn = np.round(nrm * 127.0).astype(np.int64)
    uniq, inv = np.unique(np.concatenate([q, qn, part_of[:, None]], 1), axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    assert len(uniq) < 65536 and np.abs(q).max() < 32767, P.name
    b = bytearray()
    nm = P.name.encode()
    b += struct.pack("<H", len(nm)) + nm
    b += struct.pack("<H", len(P.parts))
    for p in P.parts:
        sec, code = part_class(p)
        slot = SLOTS.get(p.rgb, 0) if isinstance(p.rgb, str) else 0
        rgb = (1.0, 1.0, 1.0) if isinstance(p.rgb, str) else p.rgb
        b += struct.pack("<7B", sec, *[int(round(max(0.0, min(1.0, c)) * 255)) for c in rgb], int(round(code * 255)), p.lod, slot)
    b += struct.pack("<II", len(uniq), len(inv) // 3)
    v = np.zeros(len(uniq), dtype=[("p", "<i2", 3), ("n", "i1", 3), ("k", "u1")])
    v["p"] = uniq[:, 0:3]
    v["n"] = np.clip(uniq[:, 3:6], -127, 127)
    v["k"] = uniq[:, 6]
    b += v.tobytes()
    b += inv.astype("<u2").tobytes()
    return bytes(b)


def export(kit):
    import json
    import struct
    data = os.path.join(os.path.dirname(HERE), "Data", "RideUK")
    blob = b"".join(piece_bytes(P) for P in kit.values())
    with open(os.path.join(data, "forecourt_kit.bin"), "wb") as f:
        f.write(b"TSFK" + struct.pack("<II", 1, len(kit)) + blob)
    about = ("Ride UK's petrol stations (the forecourt kit, everyday kits stage 5; beenster Nov 26: 'These look great'): "
             "our own pieces modelled by Tools/blender_forecourt_kit.py (which writes this file and forecourt_kit.bin). "
             "World/WorldRideForecourtKit.cpp lays them where World/WorldRideTownLife.cpp places each forecourt's parts. "
             "Pieces are in the forecourt's frame: x along the road, y out from its edge, z up, cm. A part's slot: 1 the "
             "livery's main colour, 2 its accent, 10-13 lit after dark only (soffit, shop, price digits, signs).")
    rows = dict(_about=about, liveries=[dict(label=l, main=[round(c, 4) for c in m], accent=[round(c, 4) for c in a]) for l, m, a in LIVERIES],
                styles=dict(glass=0.7, brick=0.3), kiosk_bay=KIOSK_BAY, kiosk_top=KIOSK_TOP,
                pieces={P.name: dict(label=P.label, tris=P.tris) for P in kit.values()})
    with open(os.path.join(data, "forecourt_kit.json"), "w") as f:
        json.dump(rows, f, indent=1)
    print("forecourt kit: %d pieces, %d KB" % (len(kit), (len(blob) + 12) // 1024))


def main():
    args = sys.argv[1:]
    clear()
    kit = build_kit()
    if "--export" in args:
        export(kit)
        return
    stations = build_stations(kit)
    if "--render" in args:
        out = args[args.index("--render") + 1]
        import blender_forecourt_kit_render as R
        R.render_all(kit, stations, out)


if __name__ == "__main__":
    main()
