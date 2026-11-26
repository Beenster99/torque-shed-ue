"""Ride UK's farm machinery (beenster, Nov 26: a sheet of sixteen machines "and these" for the farm pack), modelled in
Blender. Our own models from general knowledge of farm machines: no maker's names, badges, model numbers, tyre
brands or lettering. The modern tractor on that sheet is the farm kit's own farm_tractor (Tools/blender_tractor.py,
thread "Build farmyards and village kits"); these are the other fifteen.

The self-propelled machines come in the farm kit's three fleet liveries (Tools/blender_tractor.py's), so a farm's
machines read as one fleet (body / wheel centres / roof): 0 deep teal / silver, 1 burgundy / graphite, 2 slate grey /
cream / white roof. A kind's models run livery 0, 1, 2 (then the next design's 0, 1, 2), so a prop's Variant modulo 3 is its
livery (World/WorldRideFarms.cpp gives a yard's machines its farm's livery). The implements and trailers keep their
own colours (a farm's implements come from all sorts of makers).

Writes Data/RideUK/Props/SM_Prop_<kind>_<n>.glb, each with two distance versions made here by hand
(SM_Prop_<kind>_<n>_LOD1.glb and _LOD2.glb: Tools/ue_build_rideuk.py puts them in as the mesh's LODs, never reduced
by the engine), and sets each kind's height and reach in Data/RideUK/prop_kit.json. Same material and frame as the
street kit (Tools/blender_street_kit.py: M_RideStreetKit, front +x, foot at 0, y up in the file); all the models of a
kind are the same height.

  farm_combine 0..5     a combine harvester: 0..2 with a grain header and its reel, 3..5 with a maize header
  farm_forager 0..2     a self-propelled forage harvester, its spout up and over, a maize header
  farm_sprayer 0..2     a self-propelled high-clearance sprayer, its booms folded along its sides
  farm_tractor4wd 0..2  a big articulated four-wheel-drive tractor on dual wheels
  farm_tractortrack 0..2  a rubber-tracked tractor
  farm_crawler 0..2     a small narrow crawler tractor with a fold-up roll bar (orchards, vineyards, hill farms)
  farm_loader 0..2      a compact telescopic loader with pallet forks
  farm_mower 0..2       a ride-on gang mower with a roll bar and canopy (estates, golf courses, parks)
  farm_buggy 0..2       a farm utility buggy: bench seat, roll cage, tipping load bed
  farm_chaser 0         a big grain chaser trailer on five axles, its unloading auger folded
  farm_tipper 0         a twin-axle tipping trailer with high sides and a sheet over
  farm_baler 0          a big square baler on a tandem axle, a bale on its chute
  farm_drill 0          a seed drill: tyre packer, tines, hopper, disc coulters
  farm_cultivator 0     a wide trailed cultivator opened out for work (10 m)

    python Tools/blender_farm_machines.py                # the models
    python Tools/blender_farm_machines.py --render DIR   # and the review sheet's pictures in DIR
    python Tools/blender_farm_machines.py --only combine,baler --render DIR   # some of them
"""
import math
import os
import sys

import bpy  # noqa: F401  (before mathutils)
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_street_kit as K   # noqa: E402
from blender_street_kit import box, lathe, rod, sweep   # noqa: E402

# (the farm kit's fleet liveries: body, wheel centres, cab roof)
LIVERIES = [   # (Tools/blender_tractor.py's LIVERIES, linear: body, wheel centres, cab roof, stripe)
    ((0.02, 0.15, 0.17), (0.42, 0.43, 0.42), (0.02, 0.15, 0.17), (0.55, 0.55, 0.52)),     # 0 deep teal, silver
    ((0.2, 0.018, 0.035), (0.08, 0.082, 0.086), (0.2, 0.018, 0.035), (0.5, 0.48, 0.44)),  # 1 burgundy, graphite
    ((0.12, 0.13, 0.14), (0.55, 0.5, 0.38), (0.6, 0.6, 0.58), (0.55, 0.5, 0.38)),         # 2 slate grey, cream, white roof
]
TYRE = (0.018, 0.018, 0.02)
DARK = (0.022, 0.023, 0.025)
GRAPHITE = (0.06, 0.062, 0.066)
STEEL = (0.42, 0.42, 0.41)
GLASS = (0.02, 0.025, 0.03)
LAMP = (0.55, 0.55, 0.5)
AMBER = (0.7, 0.32, 0.02)
REDLAMP = (0.45, 0.02, 0.02)
SEAT = (0.03, 0.03, 0.03)
MUSTARD = (0.6, 0.44, 0.04)
TIPPER = (0.62, 0.36, 0.05)
TIP_GREEN = (0.05, 0.17, 0.06)
SHEET = (0.06, 0.2, 0.08)
IMP_RED = (0.42, 0.045, 0.03)
STRAW = (0.48, 0.36, 0.15)
GRAIN = (0.45, 0.33, 0.12)

# kind: (reach in m, casts a shadow, hand-made LODs' screen sizes)
KINDS = {
    "farm_combine": (600, True, [0.15, 0.04]), "farm_forager": (500, True, [0.15, 0.04]),
    "farm_sprayer": (500, True, [0.15, 0.04]), "farm_tractor4wd": (500, True, [0.15, 0.04]),
    "farm_tractortrack": (450, True, [0.15, 0.04]), "farm_crawler": (250, True, [0.12, 0.03]),
    "farm_loader": (250, True, [0.12, 0.03]), "farm_mower": (200, True, [0.12, 0.03]),
    "farm_buggy": (200, True, [0.12, 0.03]), "farm_chaser": (500, True, [0.15, 0.04]),
    "farm_tipper": (400, True, [0.15, 0.04]), "farm_baler": (400, True, [0.15, 0.04]),
    "farm_drill": (350, True, [0.12, 0.03]), "farm_cultivator": (400, True, [0.12, 0.03]),
}


# --- shapes ----------------------------------------------------------------------------------------------

def prism(name, pts, axis, a0, a1, sharp=35.0):
    """A straight prism: pts a convex outline ((x, z) for axis "y", (y, z) for axis "x", (x, y) for axis "z")
    run from a0 to a1 along axis."""
    n = len(pts)
    v = []
    for a in (a0, a1):
        for p, q in pts:
            v.append((p, a, q) if axis == "y" else (a, p, q) if axis == "x" else (p, q, a))
    f = [tuple(reversed(range(n))), tuple(range(n, 2 * n))]
    f += [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    return K._object(name, v, f, sharp)


def hull_of_circles(circles, seg):
    """The convex outline round circles [(x, z, r)] (a track round its wheels), counter-clockwise."""
    pts = []
    for cx, cz, r in circles:
        for k in range(seg):
            a = 2 * math.pi * k / seg
            pts.append((cx + r * math.cos(a), cz + r * math.sin(a)))
    pts = sorted(set((round(x, 5), round(z, 5)) for x, z in pts))

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def tilted(name, centre, size, pitch=0.0, yaw=0.0, roll=0.0, bevel=0.0):
    """A box turned: pitch about y (nose down for +), yaw about z, roll about x."""
    R = Matrix.Rotation(yaw, 4, "Z") @ Matrix.Rotation(pitch, 4, "Y") @ Matrix.Rotation(roll, 4, "X")
    return box(name, centre, size, bevel=bevel, rot=R)


def beam(M, name, a, b, w, h, surface, rgb, dirt=0.3):
    """A square beam from a to b (w across, h up)."""
    a, b = Vector(a), Vector(b)
    d = b - a
    L = d.length
    yaw = math.atan2(d.y, d.x)
    pitch = -math.atan2(d.z, math.hypot(d.x, d.y))
    M.add(tilted(name, (a + b) * 0.5, (L, w, h), pitch, yaw), surface, rgb, dirt)


def wheel(M, x, y, z, r, w, rim, lod, lugs=14, rim_frac=0.55, out=1, dual=False):
    """A tyre on its axle along y, its wheel centre in rim colour on the outer face (out: +1 the left side)."""
    seg = (20 if r > 0.6 else 16) if lod == 0 else (10 if lod == 1 else 6)
    prof = [(0.0, -w / 2), (r * 0.62, -w / 2), (r * 0.9, -w / 2), (r, -w / 2 + w * 0.18), (r, w / 2 - w * 0.18), (r * 0.9, w / 2), (r * 0.62, w / 2), (0.0, w / 2)]
    if lod == 2:
        prof = [(0.0, -w / 2), (r * 0.95, -w / 2), (r, -w / 2 + w * 0.2), (r, w / 2 - w * 0.2), (r * 0.95, w / 2), (0.0, w / 2)]
    ob = lathe("tyre", prof, seg)
    ob.matrix_world = Matrix.Translation((x, y, z)) @ Matrix.Rotation(math.pi / 2, 4, "X")
    M.add(ob, "paint", TYRE, 0.7)
    if lod == 2:
        return
    rr = r * rim_frac
    sides = (-1, 1) if lod == 0 and not dual else (out if not dual or dual == "outer" else -out,)
    for s in sides:
        dish = 0.035 if s == out else 0.01
        h = lathe("rim", [(0.0, 0.0), (rr, 0.0), (rr, 0.02), (rr * 0.5, dish + 0.02), (0.0, dish + 0.03)], 14 if lod == 0 else 8)
        h.matrix_world = Matrix.Translation((x, y + s * (w / 2 - 0.01), z)) @ Matrix.Rotation(-s * math.pi / 2, 4, "X")
        M.add(h, "enamel", rim, 0.45)
    if lod == 0 and lugs:
        # (the tread: chevron bars across the crown)
        for k in range(lugs):
            a = 2 * math.pi * (k + 0.5) / lugs
            for side in (-1, 1):
                c = (x + math.cos(a) * r, y + side * w * 0.24, z + math.sin(a) * r)
                M.add(box("lug", c, (0.05, w * 0.48, min(0.07, r * 0.09)), rot=Matrix.Rotation(-a, 4, "Y") @ Matrix.Rotation(side * 0.5, 4, "X")), "paint", TYRE, 0.7)


def track(M, name, circles, y, w, lod, rim, rollers=()):
    """A rubber (or steel) track round its wheels circles [(x, z, r)], centred at y, w wide; the wheels' faces in rim
    colour, and road rollers (x, z, r) between."""
    outline = hull_of_circles(circles, 20 if lod == 0 else 10 if lod == 1 else 6)
    M.add(prism(name, outline, "y", y - w / 2, y + w / 2, 50.0), "paint", TYRE, 0.7)
    if lod == 2:
        return
    s = 1 if y > 0 else -1
    for cx, cz, r in list(circles) + list(rollers):
        rr = r * 0.82
        h = lathe("trackwheel", [(0.0, 0.0), (rr, 0.0), (rr, 0.02), (rr * 0.45, 0.05), (0.0, 0.06)], 14 if lod == 0 else 8)
        h.matrix_world = Matrix.Translation((cx, y + s * (w / 2 - 0.005), cz)) @ Matrix.Rotation(-s * math.pi / 2, 4, "X")
        M.add(h, "paint", GRAPHITE, 0.6)
        if lod == 0 and r > 0.4:
            c = lathe("trackhub", [(0.0, 0.0), (r * 0.3, 0.0), (r * 0.3, 0.03), (0.0, 0.05)], 10)
            c.matrix_world = Matrix.Translation((cx, y + s * (w / 2 + 0.05), cz)) @ Matrix.Rotation(-s * math.pi / 2, 4, "X")
            M.add(c, "enamel", rim, 0.4)
    if lod == 0:
        # (the lugs on the track's ground run)
        x0 = min(c[0] for c in circles)
        x1 = max(c[0] for c in circles)
        n = int((x1 - x0) / 0.18)
        for k in range(n + 1):
            M.add(box("tlug", (x0 + k * (x1 - x0) / max(n, 1), y, 0.02), (0.06, w * 0.96, 0.04)), "paint", TYRE, 0.8)


def cab(M, x0, x1, hw, z0, z1, liv, lod, beacon=True, lamps=True, posts=DARK, mirrors=True):
    """A glass cab x0..x1 (front x1), hw half wide, z0..z1, its roof over in the livery's roof colour, a beacon."""
    body, rim, roof, stripe = liv
    M.add(box("glass", ((x0 + x1) / 2, 0.0, (z0 + z1) / 2), (x1 - x0, hw * 2, z1 - z0)), "lens", GLASS, 0.0)
    M.add(box("roof", ((x0 + x1) / 2 - 0.03, 0.0, z1 + 0.05), (x1 - x0 + 0.2, hw * 2 + 0.16, 0.1), bevel=0.03 if lod == 0 else 0.0), "enamel", roof, 0.2)
    if lod == 2:
        return z1 + 0.1
    for x in (x0 + 0.04, x1 - 0.04):
        for y in (-hw + 0.03, hw - 0.03):
            M.add(box("post", (x, y, (z0 + z1) / 2), (0.08, 0.08, z1 - z0)), "paint", posts, 0.2)
    M.add(box("cabfloor", ((x0 + x1) / 2, 0.0, z0 + 0.12), (x1 - x0 + 0.02, hw * 2 + 0.02, 0.24)), "enamel", body, 0.35)
    top = z1 + 0.1
    if lod == 0:
        if lamps:
            for y in (-hw + 0.25, -hw + 0.55, hw - 0.55, hw - 0.25):
                M.add(box("worklamp", (x1 + 0.1, y, z1 + 0.02), (0.06, 0.16, 0.1)), "lens", LAMP, 0.0)
            for y in (-hw + 0.4, hw - 0.4):
                M.add(box("worklamp", (x0 - 0.1, y, z1 + 0.02), (0.06, 0.16, 0.1)), "lens", LAMP, 0.0)
        if mirrors:
            for s in (-1, 1):
                M.add(rod("mirrorarm", (x1 - 0.1, s * hw, z1 - 0.15), (x1 - 0.05, s * (hw + 0.4), z1 - 0.25), 0.015, 4), "paint", DARK, 0.2)
                M.add(box("mirror", (x1 - 0.05, s * (hw + 0.42), z1 - 0.4), (0.04, 0.16, 0.26)), "paint", DARK, 0.2)
    if beacon:
        bx, by = x0 + 0.25, hw - 0.2
        if lod == 0:
            b = lathe("beacon", [(0.0, 0.0), (0.07, 0.0), (0.07, 0.11), (0.05, 0.14), (0.0, 0.15)], 10, at=(bx, by, top))
        else:
            b = box("beacon", (bx, by, top + 0.075), (0.12, 0.12, 0.15))
        M.add(b, "lens", AMBER, 0.0)
        top += 0.15
    return top


def lights(M, x, ys, z, lod, size=(0.04, 0.2, 0.12), rgb=LAMP):
    if lod == 0:
        for y in ys:
            M.add(box("lamp", (x, y, z), size), "lens", rgb, 0.0)


def rail(M, pts, r=0.02, lod=0, rgb=DARK):
    if lod == 0:
        M.add(sweep("rail", pts, r, 6), "paint", rgb, 0.3)


# --- the machines ---------------------------------------------------------------------------------------

def combine(name, liv, maize, lod):
    """A combine harvester, 4.0 m to its beacon: big drive wheels under the front, steering wheels behind, the
    grain tank on top behind the cab, the folded unloading auger along its left, the feeder house down to a 7.6 m
    grain header and reel (or a maize header's row points)."""
    body, rim, roof, stripe = liv
    M = K.Model(name)
    for s in (-1, 1):
        wheel(M, 1.15, s * 1.45, 0.93, 0.93, 0.75, rim, lod, 18, out=s)
        wheel(M, -3.35, s * 1.3, 0.6, 0.6, 0.45, rim, lod, 12, out=s)
    bev = 0.05 if lod == 0 else 0.0
    M.add(box("body", (-1.3, 0.0, 1.95), (5.8, 2.5, 2.1), bevel=bev), "enamel", body, 0.45)
    # (the grain tank, flaring out over the body)
    M.add(prism("tank", [(-1.25, 3.0), (1.25, 3.0), (1.55, 3.55), (-1.55, 3.55)], "x", -2.5, 0.55), "enamel", body, 0.35)
    M.add(box("engine", (-3.25, 0.0, 3.12), (1.7, 2.3, 0.26), bevel=bev), "enamel", body, 0.4)
    M.add(tilted("chopper", (-4.35, 0.0, 1.55), (0.6, 2.2, 1.3), pitch=-0.25), "enamel", body, 0.5)
    M.add(box("belly", (-1.3, 0.0, 0.88), (5.4, 1.6, 0.12)), "paint", DARK, 0.6)
    # (the feeder house, sloping down to the header)
    M.add(tilted("feeder", (2.55, 0.0, 1.15), (2.3, 1.35, 0.75), pitch=0.42), "enamel", body, 0.5)
    # (the cab on its platform over the feeder house)
    M.add(box("platform", (1.75, 0.0, 2.88), (1.2, 2.3, 0.12)), "paint", DARK, 0.3)
    top = cab(M, 1.0, 2.4, 0.9, 2.94, 3.78, liv, lod)
    if lod < 2:
        # (the auger folded along the left, its spout forward)
        M.add(rod("auger", (-3.6, 1.45, 3.3), (0.9, 1.6, 3.38), 0.17, 10 if lod == 0 else 6), "enamel", body, 0.35)
        M.add(rod("spout", (0.9, 1.6, 3.38), (1.1, 1.6, 3.0), 0.14, 8 if lod == 0 else 4), "enamel", body, 0.35)
    if lod == 0:
        M.add(box("grille", (-3.25, 1.16, 2.5), (1.3, 0.02, 0.6)), "paint", DARK, 0.3)
        M.add(box("grille", (-3.25, -1.16, 2.5), (1.3, 0.02, 0.6)), "paint", DARK, 0.3)
        for k in range(4):
            M.add(box("panel", (-0.1 - k * 1.05, 1.26, 1.9), (0.02, 0.01, 1.7)), "paint", DARK, 0.4)
        M.add(box("stripe", (-1.3, 1.26, 2.55), (5.6, 0.01, 0.12)), "paint", stripe, 0.2)
        M.add(box("stripe", (-1.3, -1.26, 2.55), (5.6, 0.01, 0.12)), "paint", stripe, 0.2)
        # (the ladder up to the cab, on the left)
        for s in (0.0, 0.42):
            M.add(rod("ladder", (2.2 + s, 1.25, 0.55), (1.85 + s, 1.2, 2.85), 0.02, 4), "paint", DARK, 0.3)
        for k in range(6):
            z = 0.7 + k * 0.38
            t = (z - 0.55) / 2.3
            M.add(box("rung", (2.2 - 0.35 * t + 0.21, 1.22, z), (0.42, 0.18, 0.03)), "paint", DARK, 0.4)
        rail(M, [(1.25, 1.15, 2.95), (1.25, 1.15, 3.85), (2.3, 1.15, 3.85)], lod=lod)
        lights(M, -4.6, (-1.0, 1.0), 2.2, lod, rgb=REDLAMP)
        M.add(rod("exhaust", (-3.8, -0.7, 3.2), (-3.8, -0.7, 3.62), 0.06, 8), "steel", DARK, 0.4)
    # (the header)
    if not maize:
        M.add(prism("header", [(3.25, 0.12), (4.55, 0.1), (4.7, 0.22), (3.55, 1.1), (3.25, 1.12)], "y", -3.8, 3.8), "enamel", body, 0.45)
        M.add(box("cutterbar", (4.68, 0.0, 0.12), (0.12, 7.6, 0.06)), "steel", STEEL, 0.4)
        for s in (-1, 1):
            M.add(prism("endplate", [(3.25, 0.1), (4.75, 0.1), (4.9, 0.2), (3.6, 1.25), (3.25, 1.3)], "y", s * 3.8 - 0.03, s * 3.8 + 0.03), "enamel", body, 0.4)
            M.add(prism("divider", [(4.6, 0.1), (5.35, 0.12), (4.6, 0.55)], "y", s * 3.8 - 0.06, s * 3.8 + 0.06), "enamel", body, 0.4)
        if lod < 2:
            # (the reel: its axle, spiders and bats, held on two arms)
            M.add(rod("reelaxle", (4.2, -3.7, 1.35), (4.2, 3.7, 1.35), 0.05, 8 if lod == 0 else 4), "paint", DARK, 0.3)
            for s in (-1, 1):
                M.add(rod("reelarm", (3.4, s * 3.72, 1.2), (4.2, s * 3.72, 1.35), 0.05, 4), "enamel", body, 0.3)
            nb = 6 if lod == 0 else 4
            for k in range(nb):
                a = 2 * math.pi * k / nb
                bx, bz = 4.2 + 0.55 * math.cos(a), 1.35 + 0.55 * math.sin(a)
                M.add(rod("bat", (bx, -3.65, bz), (bx, 3.65, bz), 0.035 if lod == 0 else 0.05, 4), "paint", (0.5, 0.48, 0.42) if lod == 0 else DARK, 0.3)
                if lod == 0:
                    for y in (-3.6, -1.2, 1.2, 3.6):
                        M.add(rod("spider", (4.2, y, 1.35), (bx, y, bz), 0.02, 4), "paint", DARK, 0.3)
        if lod == 0:
            M.add(rod("auger", (3.65, -3.7, 0.5), (3.65, 3.7, 0.5), 0.25, 12), "enamel", body, 0.4)
    else:
        M.add(box("trough", (3.6, 0.0, 0.6), (0.9, 6.2, 0.9)), "enamel", body, 0.45)
        rows = 8
        for i in range(rows):
            y = (i - (rows - 1) / 2) * 0.76
            pts = [(4.0, 0.12), (5.45, 0.18), (4.05, 0.95)] if lod == 2 else [(4.0, 0.12), (5.0, 0.15), (5.5, 0.22), (4.6, 0.7), (4.05, 0.95)]
            M.add(prism("snout", pts, "y", y - 0.17, y + 0.17), "enamel", body, 0.45)
        for s in (-1, 1):
            M.add(prism("snout", [(4.0, 0.12), (5.5, 0.22), (4.05, 0.95)], "y", s * 3.1 - 0.1, s * 3.1 + 0.1), "enamel", body, 0.45)
        if lod == 0:
            for i in range(rows - 1):
                y = (i - (rows - 2) / 2) * 0.76
                M.add(box("gap", (4.3, y, 0.42), (0.9, 0.36, 0.06)), "paint", DARK, 0.6)
    return M


def forager(name, liv, lod):
    """A self-propelled forage harvester, 4.6 m to its spout: the cab well forward, a big engine behind, the spout
    arching up from behind the cab and forward, a rotary maize header."""
    body, rim, roof, stripe = liv
    M = K.Model(name)
    for s in (-1, 1):
        wheel(M, 0.85, s * 1.38, 0.95, 0.95, 0.75, rim, lod, 18, out=s)
        wheel(M, -2.95, s * 1.22, 0.68, 0.68, 0.55, rim, lod, 14, out=s)
    bev = 0.05 if lod == 0 else 0.0
    M.add(box("body", (-1.4, 0.0, 1.8), (4.8, 2.3, 1.7), bevel=bev), "enamel", body, 0.45)
    M.add(prism("engine", [(-1.1, 2.65), (1.1, 2.65), (0.9, 3.1), (-0.9, 3.1)], "x", -3.8, -1.2), "enamel", body, 0.4)
    M.add(box("belly", (-1.2, 0.0, 0.85), (4.6, 1.5, 0.2)), "paint", DARK, 0.6)
    top = cab(M, -0.2, 1.4, 0.9, 2.62, 3.5, liv, lod)
    M.add(box("cabbase", (0.6, 0.0, 2.3), (1.7, 1.9, 0.65)), "enamel", body, 0.4)
    # (the feed rolls' housing down to the header)
    M.add(tilted("intake", (1.95, 0.0, 1.05), (1.4, 1.2, 0.85), pitch=0.3), "enamel", body, 0.5)
    # (the spout: up from the turret behind the cab, over, its deflector forward)
    sp = [(-0.75, 0.0, 2.65), (-0.8, 0.0, 3.5), (-0.55, 0.0, 4.15), (0.15, 0.0, 4.4), (1.0, 0.0, 4.32), (1.6, 0.0, 4.05)]
    if lod == 2:
        sp = [sp[0], sp[2], sp[4], sp[5]]
    M.add(sweep("spout", sp, 0.2, 4, square=True), "enamel", body, 0.35)
    if lod < 2:
        M.add(lathe("turret", [(0.0, 0.0), (0.45, 0.0), (0.45, 0.3), (0.0, 0.3)], 14 if lod == 0 else 8, at=(-0.75, 0.0, 2.6)), "paint", DARK, 0.4)
        M.add(tilted("deflector", (1.75, 0.0, 3.93), (0.5, 0.42, 0.06), pitch=0.9), "enamel", body, 0.3)
    # (the maize header: a wide drum housing and its row points)
    M.add(box("header", (3.0, 0.0, 0.55), (1.0, 6.0, 0.8)), "enamel", rim, 0.45)
    for i in range(10 if lod < 2 else 5):
        n = 10 if lod < 2 else 5
        y = (i - (n - 1) / 2) * (6.0 / n)
        w = 6.0 / n * 0.55
        M.add(prism("point", [(3.45, 0.15), (4.3, 0.2), (3.5, 0.85)], "y", y - w / 2, y + w / 2), "enamel", rim, 0.45)
    if lod == 0:
        M.add(box("grille", (-2.5, 1.16, 2.2), (2.0, 0.02, 0.8)), "paint", DARK, 0.3)
        M.add(box("grille", (-2.5, -1.16, 2.2), (2.0, 0.02, 0.8)), "paint", DARK, 0.3)
        M.add(box("stripe", (-1.4, 1.16, 2.62), (4.6, 0.01, 0.1)), "paint", stripe, 0.2)
        M.add(box("stripe", (-1.4, -1.16, 2.62), (4.6, 0.01, 0.1)), "paint", stripe, 0.2)
        for s in (0.0, 0.4):
            M.add(rod("ladder", (1.2 + s, 1.2, 0.5), (0.9 + s, 1.15, 2.6), 0.02, 4), "paint", DARK, 0.3)
        for k in range(5):
            M.add(box("rung", (1.25 - 0.06 * k, 1.17, 0.7 + k * 0.4), (0.42, 0.16, 0.03)), "paint", DARK, 0.4)
        lights(M, -3.85, (-0.9, 0.9), 2.2, lod, rgb=REDLAMP)
        M.add(rod("exhaust", (-3.2, -0.6, 3.05), (-3.2, -0.6, 3.5), 0.07, 8), "steel", DARK, 0.4)
    return M


def sprayer(name, liv, lod):
    """A self-propelled high-clearance sprayer, 3.8 m to its beacon: four tall narrow wheels on legs, the tank
    behind the cab, the boom's lattice folded along each side."""
    body, rim, roof, stripe = liv
    M = K.Model(name)
    for x in (1.75, -1.75):
        for s in (-1, 1):
            wheel(M, x, s * 1.12, 0.85, 0.85, 0.32, rim, lod, 14, rim_frac=0.6, out=s)
            M.add(box("leg", (x, s * 0.8, 1.3), (0.22, 0.12, 0.9)), "enamel", body, 0.5)
            M.add(box("hub", (x, s * 0.92, 0.85), (0.3, 0.2, 0.3)), "paint", DARK, 0.5)
    M.add(box("chassis", (0.0, 0.0, 1.75), (5.4, 1.75, 0.3)), "paint", DARK, 0.5)
    bev = 0.08 if lod == 0 else 0.0
    M.add(box("tank", (-1.2, 0.0, 2.55), (2.7, 1.9, 1.3), bevel=bev), "enamel", body, 0.35)
    M.add(box("engine", (-2.9, 0.0, 2.3), (0.9, 1.6, 0.8), bevel=bev), "enamel", body, 0.4)
    M.add(box("cabbase", (1.45, 0.0, 2.05), (1.8, 1.7, 0.3)), "enamel", body, 0.4)
    top = cab(M, 0.6, 2.3, 0.85, 2.2, 3.55, liv, lod)
    # (the folded boom: two rails and their lattice down each side, the centre frame at the back)
    for s in (-1, 1):
        y = s * 1.35
        for z in (2.15, 2.75):
            M.add(rod("boom", (-3.1, y, z), (2.45, y, z), 0.045 if lod < 2 else 0.08, 4), "enamel", rim, 0.4)
        if lod < 2:
            n = 10 if lod == 0 else 4
            for k in range(n):
                x0 = -3.1 + k * 5.55 / n
                x1 = x0 + 5.55 / n
                M.add(rod("lattice", (x0, y, 2.15), (x1, y, 2.75), 0.025, 4), "enamel", rim, 0.4)
                if lod == 0:
                    M.add(rod("upright", (x0, y, 2.15), (x0, y, 2.75), 0.02, 4), "enamel", rim, 0.4)
            M.add(box("cradle", (2.0, s * 1.2, 2.45), (0.12, 0.35, 0.9)), "paint", DARK, 0.4)
    M.add(box("centre", (-3.15, 0.0, 2.1), (0.15, 2.5, 1.0)), "enamel", rim, 0.4)
    if lod == 0:
        M.add(box("lid", (-1.2, 0.0, 3.22), (0.5, 0.5, 0.06)), "paint", DARK, 0.3)
        rail(M, [(-2.4, 0.95, 3.25), (-2.4, 0.95, 3.6), (0.0, 0.95, 3.6), (0.0, 0.95, 3.25)], lod=lod)
        for s in (0.0, 0.35):
            M.add(rod("ladder", (0.25 + s, 1.0, 0.5), (0.25 + s, 0.95, 2.1), 0.02, 4), "paint", DARK, 0.3)
        lights(M, 2.32, (-0.6, 0.6), 2.0, lod)
        lights(M, -3.36, (-1.0, 1.0), 1.85, lod, rgb=REDLAMP)
        M.add(box("induction", (-0.2, -1.05, 2.0), (0.7, 0.3, 0.5)), "paint", DARK, 0.4)
    return M


def tractor4wd(name, liv, lod):
    """A big articulated four-wheel-drive tractor on duals, 3.7 m to its beacon: a long bonnet on the front
    frame, the cab on the rear, the pivot between."""
    body, rim, roof, stripe = liv
    M = K.Model(name)
    for x in (1.6, -1.6):
        for s in (-1, 1):
            wheel(M, x, s * 1.12, 1.0, 1.0, 0.6, rim, lod, 16, out=s, dual="inner")
            wheel(M, x, s * 1.8, 1.0, 1.0, 0.6, rim, lod, 16, out=s, dual="outer")
            if lod < 2:
                M.add(box("axle", (x, s * 0.7, 1.0), (0.35, 0.5, 0.35)), "paint", DARK, 0.5)
    bev = 0.06 if lod == 0 else 0.0
    M.add(box("frontframe", (1.7, 0.0, 1.2), (3.0, 1.1, 0.55)), "paint", DARK, 0.5)
    M.add(prism("bonnet", [(-0.62, 1.4), (0.62, 1.4), (0.6, 2.25), (0.45, 2.42), (-0.45, 2.42), (-0.6, 2.25)], "x", 0.3, 3.2), "enamel", body, 0.35)
    M.add(box("grille", (3.21, 0.0, 1.88), (0.04, 1.1, 0.85)), "paint", DARK, 0.3)
    M.add(box("weights", (3.45, 0.0, 1.15), (0.45, 1.4, 0.5), bevel=bev), "paint", DARK, 0.5)
    M.add(box("rearframe", (-1.6, 0.0, 1.25), (2.8, 1.2, 0.6)), "paint", DARK, 0.5)
    M.add(box("cabbase", (-0.75, 0.0, 1.8), (1.9, 1.7, 0.6)), "enamel", body, 0.4)
    top = cab(M, -1.7, 0.2, 0.9, 2.1, 3.5, liv, lod)
    for s in (-1, 1):
        M.add(box("fender", (-1.6, s * 1.48, 2.15), (2.0, 1.55, 0.06)), "enamel", body, 0.4)
        if lod == 0:
            M.add(box("fenderlip", (-0.6, s * 1.48, 2.0), (0.06, 1.55, 0.3)), "enamel", body, 0.4)
    if lod < 2:
        M.add(box("hitch", (-3.1, 0.0, 0.95), (0.35, 0.9, 0.6)), "paint", DARK, 0.5)
        M.add(rod("exhaust", (0.55, -0.35, 2.4), (0.55, -0.35, 3.4), 0.08, 8 if lod == 0 else 4), "steel", DARK, 0.4)
        M.add(rod("intake", (0.55, 0.35, 2.4), (0.55, 0.35, 3.1), 0.11, 8 if lod == 0 else 4), "enamel", body, 0.4)
    if lod == 0:
        for y in (-0.42, 0.42):
            M.add(box("headlamp", (3.22, y, 2.15), (0.04, 0.26, 0.12)), "lens", LAMP, 0.0)
        M.add(lathe("capexh", [(0.0, 0.0), (0.1, 0.0), (0.1, 0.06), (0.0, 0.06)], 8, at=(0.55, -0.35, 3.4)), "steel", DARK, 0.4)
        for k in range(4):
            M.add(box("step", (-0.4, 1.3, 0.55 + k * 0.36), (0.4, 0.25, 0.03)), "paint", DARK, 0.4)
        M.add(box("stripe", (1.75, 0.63, 2.0), (2.8, 0.01, 0.08)), "paint", stripe, 0.2)
        M.add(box("stripe", (1.75, -0.63, 2.0), (2.8, 0.01, 0.08)), "paint", stripe, 0.2)
        lights(M, -2.75, (-0.7, 0.7), 2.05, lod, rgb=REDLAMP)
    return M


def tractortrack(name, liv, lod):
    """A rubber-tracked tractor, 3.4 m to its beacon: two tracks round a drive wheel behind and an idler in front,
    its road rollers between, a bonnet and cab over."""
    body, rim, roof, stripe = liv
    M = K.Model(name)
    for s in (-1, 1):
        track(M, "track", [(-1.35, 0.8, 0.8), (1.55, 0.55, 0.55)], s * 1.0, 0.62, lod, rim,
              rollers=[(-0.35, 0.3, 0.3), (0.35, 0.3, 0.3)] if lod == 0 else [])
        M.add(box("trackfender", (0.1, s * 1.0, 1.75), (3.6, 0.72, 0.05)), "enamel", body, 0.4)
    bev = 0.06 if lod == 0 else 0.0
    M.add(box("belly", (0.2, 0.0, 1.05), (4.2, 1.1, 0.7)), "paint", DARK, 0.5)
    M.add(prism("bonnet", [(-0.55, 1.35), (0.55, 1.35), (0.55, 2.15), (0.4, 2.32), (-0.4, 2.32), (-0.55, 2.15)], "x", 0.0, 2.7), "enamel", body, 0.35)
    M.add(box("grille", (2.71, 0.0, 1.85), (0.04, 0.95, 0.75)), "paint", DARK, 0.3)
    M.add(box("weights", (2.9, 0.0, 1.05), (0.4, 1.2, 0.55), bevel=bev), "paint", DARK, 0.5)
    M.add(box("cabbase", (-0.75, 0.0, 1.75), (1.8, 1.7, 0.4)), "enamel", body, 0.4)
    top = cab(M, -1.65, 0.15, 0.85, 1.95, 3.15, liv, lod)
    if lod < 2:
        M.add(rod("exhaust", (0.35, -0.3, 2.3), (0.35, -0.3, 3.2), 0.07, 8 if lod == 0 else 4), "steel", DARK, 0.4)
        M.add(box("hitch", (-2.25, 0.0, 0.95), (0.4, 0.9, 0.6)), "paint", DARK, 0.5)
    if lod == 0:
        for y in (-0.38, 0.38):
            M.add(box("headlamp", (2.72, y, 2.08), (0.04, 0.24, 0.12)), "lens", LAMP, 0.0)
        M.add(box("stripe", (1.35, 0.56, 1.9), (2.6, 0.01, 0.08)), "paint", stripe, 0.2)
        M.add(box("stripe", (1.35, -0.56, 1.9), (2.6, 0.01, 0.08)), "paint", stripe, 0.2)
        for k in range(3):
            M.add(box("step", (-0.3, 1.38, 0.95 + k * 0.3), (0.35, 0.2, 0.03)), "paint", DARK, 0.4)
        lights(M, -1.75, (-0.7, 0.7), 1.85, lod, rgb=REDLAMP)
    return M


def crawler(name, liv, lod):
    """A small narrow crawler tractor (orchards, vineyards, steep hill ground), 2.2 m to its roll bar: steel tracks,
    a short bonnet, an open seat, the roll bar up."""
    body, rim, roof, stripe = liv
    M = K.Model(name)
    for s in (-1, 1):
        track(M, "track", [(-0.72, 0.32, 0.32), (0.68, 0.25, 0.25)], s * 0.5, 0.3, lod, DARK,
              rollers=[(-0.15, 0.14, 0.14), (0.25, 0.14, 0.14)] if lod == 0 else [])
        M.add(box("trackfender", (0.0, s * 0.5, 0.72), (1.75, 0.38, 0.04)), "enamel", body, 0.4)
    bev = 0.04 if lod == 0 else 0.0
    M.add(box("belly", (0.0, 0.0, 0.5), (1.7, 0.6, 0.45)), "paint", DARK, 0.5)
    M.add(prism("bonnet", [(-0.38, 0.72), (0.38, 0.72), (0.38, 1.12), (0.28, 1.25), (-0.28, 1.25), (-0.38, 1.12)], "x", 0.05, 1.3), "enamel", body, 0.35)
    M.add(box("grille", (1.31, 0.0, 1.0), (0.03, 0.6, 0.38)), "paint", DARK, 0.3)
    M.add(box("platform", (-0.45, 0.0, 0.82), (0.9, 0.9, 0.08)), "paint", DARK, 0.4)
    M.add(box("seat", (-0.55, 0.0, 1.12), (0.42, 0.45, 0.12), bevel=bev), "paint", SEAT, 0.3)
    M.add(box("seatback", (-0.76, 0.0, 1.35), (0.08, 0.42, 0.38), bevel=bev), "paint", SEAT, 0.3)
    M.add(box("seatbox", (-0.55, 0.0, 0.96), (0.5, 0.55, 0.22)), "enamel", body, 0.4)
    # (the roll bar)
    for s in (-1, 1):
        M.add(rod("rops", (-0.92, s * 0.48, 0.75), (-0.92, s * 0.48, 2.12), 0.045, 8 if lod == 0 else 4), "galv", (0.6, 0.6, 0.58), 0.2)
    M.add(rod("ropstop", (-0.92, -0.48, 2.12), (-0.92, 0.48, 2.12), 0.045, 8 if lod == 0 else 4), "galv", (0.6, 0.6, 0.58), 0.2)
    if lod < 2:
        M.add(rod("wheel", (0.0, 0.0, 1.2), (-0.2, 0.0, 1.45), 0.025, 4), "paint", DARK, 0.3)
        M.add(lathe("steer", [(0.0, 0.0), (0.17, 0.0), (0.17, 0.03), (0.0, 0.03)], 12 if lod == 0 else 6, at=(-0.2, 0.0, 1.45)), "paint", DARK, 0.3)
        M.add(rod("exhaust", (0.9, -0.25, 1.2), (0.9, -0.25, 1.65), 0.035, 6), "steel", DARK, 0.4)
    if lod == 0:
        for y in (-0.2, 0.2):
            M.add(box("headlamp", (1.32, y, 1.13), (0.03, 0.12, 0.07)), "lens", LAMP, 0.0)
        M.add(box("stripe", (0.65, 0.39, 1.0), (1.2, 0.01, 0.05)), "paint", stripe, 0.2)
        M.add(box("stripe", (0.65, -0.39, 1.0), (1.2, 0.01, 0.05)), "paint", stripe, 0.2)
        M.add(box("hitch", (-0.95, 0.0, 0.5), (0.2, 0.5, 0.35)), "paint", DARK, 0.5)
        M.add(lathe("beacon", [(0.0, 0.0), (0.05, 0.0), (0.05, 0.08), (0.0, 0.1)], 8, at=(-0.92, 0.36, 2.16)), "lens", AMBER, 0.0)
    return M


def loader(name, liv, lod):
    """A compact telescopic loader, 2.45 m to its canopy: an articulated machine on four even wheels, a
    roll-over canopy over the seat, the boom along its right side down to a pallet fork."""
    body, rim, roof, stripe = liv
    M = K.Model(name)
    for x in (0.95, -0.95):
        for s in (-1, 1):
            wheel(M, x, s * 0.66, 0.48, 0.48, 0.36, rim, lod, 12, out=s)
    bev = 0.05 if lod == 0 else 0.0
    M.add(box("rear", (-1.1, 0.0, 0.95), (1.5, 1.1, 0.85), bevel=bev), "enamel", body, 0.4)
    M.add(box("front", (0.75, 0.0, 0.7), (1.4, 0.8, 0.45)), "enamel", body, 0.45)
    M.add(box("deck", (-0.35, 0.0, 1.32), (1.3, 1.2, 0.08)), "paint", DARK, 0.4)
    for s in (-1, 1):
        M.add(box("guard", (-0.95, s * 0.66, 1.0), (1.0, 0.42, 0.05)), "enamel", body, 0.4)
        M.add(box("guard", (0.95, s * 0.66, 1.0), (1.0, 0.42, 0.05)), "enamel", body, 0.4)
    # (the canopy on four posts, the seat under)
    for x in (-0.9, 0.2):
        for s in (-1, 1):
            M.add(box("post", (x, s * 0.55, 1.88), (0.07, 0.07, 1.05)), "paint", DARK, 0.3)
    M.add(box("canopy", (-0.35, 0.0, 2.42), (1.35, 1.25, 0.07), bevel=0.02 if lod == 0 else 0.0), "enamel", roof, 0.3)
    M.add(box("seat", (-0.5, 0.0, 1.55), (0.45, 0.48, 0.12)), "paint", SEAT, 0.3)
    M.add(box("seatback", (-0.72, 0.0, 1.8), (0.08, 0.45, 0.45)), "paint", SEAT, 0.3)
    # (the boom: out from its pivot behind the seat, down to the carriage)
    beam(M, "boom", (-0.95, -0.62, 1.95), (1.55, -0.62, 0.95), 0.2, 0.26, "enamel", body)
    if lod < 2:
        beam(M, "boominner", (0.8, -0.62, 1.25), (1.7, -0.62, 0.88), 0.14, 0.18, "paint", rim)
        beam(M, "ram", (-0.4, -0.62, 1.2), (0.6, -0.62, 1.55), 0.08, 0.08, "steel", STEEL)
        M.add(box("crossbar", (1.72, -0.15, 0.85), (0.12, 1.0, 0.12)), "paint", DARK, 0.4)
    M.add(box("carriage", (1.85, 0.0, 0.5), (0.1, 1.05, 0.85)), "paint", DARK, 0.4)
    for s in (-1, 1):
        M.add(box("tine", (2.4, s * 0.36, 0.08), (1.1, 0.11, 0.05)), "steel", STEEL, 0.5)
        M.add(box("tineup", (1.92, s * 0.36, 0.4), (0.05, 0.11, 0.7)), "steel", STEEL, 0.5)
    if lod == 0:
        M.add(box("grille", (-1.86, 0.0, 0.95), (0.03, 0.8, 0.5)), "paint", DARK, 0.3)
        for y in (-0.4, 0.4):
            M.add(box("headlamp", (0.25, y, 2.35), (0.06, 0.12, 0.08)), "lens", LAMP, 0.0)
        M.add(rod("steer", (-0.1, 0.0, 1.35), (-0.25, 0.0, 1.75), 0.025, 4), "paint", DARK, 0.3)
        M.add(lathe("steerwheel", [(0.0, 0.0), (0.16, 0.0), (0.16, 0.03), (0.0, 0.03)], 12, at=(-0.25, 0.0, 1.75)), "paint", DARK, 0.3)
        M.add(lathe("beacon", [(0.0, 0.0), (0.05, 0.0), (0.05, 0.03), (0.0, 0.03)], 8, at=(-0.8, 0.45, 2.455)), "lens", AMBER, 0.0)
        lights(M, -1.86, (-0.4, 0.4), 1.2, lod, rgb=REDLAMP)
    return M


def mower(name, liv, lod):
    """A ride-on gang mower, 2.15 m to its canopy: three cutting units in front, two beside, the seat under a
    roll bar and canopy."""
    body, rim, roof, stripe = liv
    M = K.Model(name)
    for s in (-1, 1):
        wheel(M, 0.4, s * 0.58, 0.42, 0.42, 0.3, rim, lod, 10, out=s)
        wheel(M, -0.9, s * 0.45, 0.32, 0.32, 0.22, rim, lod, 10, out=s)
    bev = 0.04 if lod == 0 else 0.0
    M.add(box("body", (-0.45, 0.0, 0.62), (1.6, 0.8, 0.5), bevel=bev), "enamel", body, 0.4)
    M.add(box("engine", (-0.95, 0.0, 1.0), (0.8, 0.85, 0.35), bevel=bev), "enamel", body, 0.4)
    M.add(box("seat", (-0.35, 0.0, 1.0), (0.45, 0.48, 0.12)), "paint", SEAT, 0.3)
    M.add(box("seatback", (-0.56, 0.0, 1.25), (0.08, 0.46, 0.4)), "paint", SEAT, 0.3)
    M.add(box("console", (0.2, 0.0, 0.95), (0.3, 0.5, 0.25)), "enamel", body, 0.4)
    for s in (-1, 1):
        M.add(rod("rops", (-0.7, s * 0.52, 0.85), (-0.7, s * 0.52, 2.05), 0.04, 8 if lod == 0 else 4), "paint", DARK, 0.2)
    M.add(box("canopy", (-0.35, 0.0, 2.1), (1.55, 1.25, 0.06), bevel=0.02 if lod == 0 else 0.0), "enamel", roof, 0.2)
    # (the cutting units: three in front, one each side; each a reel under its hood, rollers fore and aft)
    units = [(1.25, -0.95), (1.25, 0.0), (1.25, 0.95), (-0.15, -1.15), (-0.15, 1.15)]
    for ux, uy in units:
        M.add(box("hood", (ux, uy, 0.38), (0.55, 0.85, 0.12), bevel=0.02 if lod == 0 else 0.0), "enamel", body, 0.4)
        for e in ((-1, 1) if lod < 2 else ()):
            M.add(box("unitend", (ux, uy + e * 0.42, 0.22), (0.72, 0.03, 0.3)), "enamel", body, 0.5)
        if lod < 2:
            M.add(rod("reel", (ux, uy - 0.4, 0.2), (ux, uy + 0.4, 0.2), 0.14, 8 if lod == 0 else 5), "steel", STEEL, 0.5)
        if lod == 0:
            for dx in (-0.3, 0.3):
                M.add(rod("roller", (ux + dx, uy - 0.4, 0.07), (ux + dx, uy + 0.4, 0.07), 0.065, 6), "steel", STEEL, 0.6)
            beam(M, "arm", (0.3 if ux > 0 else -0.15, uy * 0.35, 0.55), (ux, uy, 0.45), 0.06, 0.06, "paint", DARK)
    if lod == 0:
        M.add(rod("steer", (0.15, 0.0, 1.05), (0.05, 0.0, 1.35), 0.02, 4), "paint", DARK, 0.3)
        M.add(lathe("steerwheel", [(0.0, 0.0), (0.15, 0.0), (0.15, 0.03), (0.0, 0.03)], 12, at=(0.05, 0.0, 1.35)), "paint", DARK, 0.3)
        M.add(lathe("beacon", [(0.0, 0.0), (0.05, 0.0), (0.05, 0.02), (0.0, 0.02)], 8, at=(-0.9, 0.45, 2.13)), "lens", AMBER, 0.0)
        for y in (-0.25, 0.25):
            M.add(box("headlamp", (0.37, y, 1.02), (0.03, 0.1, 0.06)), "lens", LAMP, 0.0)
    return M


def buggy(name, liv, lod):
    """A farm utility buggy, 1.95 m to its cage: a short bonnet behind a guard, a bench seat for two under a
    roll cage, a tipping load bed behind."""
    body, rim, roof, stripe = liv
    M = K.Model(name)
    for x in (1.05, -0.9):
        for s in (-1, 1):
            wheel(M, x, s * 0.62, 0.33, 0.33, 0.26, rim, lod, 10, rim_frac=0.6, out=s)
    bev = 0.04 if lod == 0 else 0.0
    M.add(box("chassis", (0.0, 0.0, 0.42), (3.0, 1.0, 0.18)), "paint", DARK, 0.5)
    M.add(prism("bonnet", [(0.55, 0.5), (1.55, 0.5), (1.55, 0.82), (1.2, 0.98), (0.55, 1.02)], "y", -0.62, 0.62), "enamel", body, 0.35)
    M.add(box("dash", (0.48, 0.0, 0.98), (0.2, 1.2, 0.25)), "paint", DARK, 0.3)
    M.add(box("seatbase", (-0.05, 0.0, 0.62), (0.6, 1.2, 0.3)), "paint", DARK, 0.4)
    M.add(box("seat", (-0.02, 0.0, 0.82), (0.5, 1.15, 0.12), bevel=bev), "paint", SEAT, 0.3)
    M.add(tilted("seatback", (-0.3, 0.0, 1.1), (0.08, 1.15, 0.45), pitch=-0.15, bevel=bev), "paint", SEAT, 0.3)
    for s in (-1, 1):
        M.add(box("guard", (1.05, s * 0.62, 0.72), (0.65, 0.3, 0.04)), "enamel", body, 0.4)
    # (the cage)
    cg = 8 if lod == 0 else 4
    for s in (-1, 1):
        M.add(sweep("cage", [(0.55, s * 0.6, 1.0), (0.3, s * 0.6, 1.88), (-0.45, s * 0.6, 1.88), (-0.45, s * 0.6, 0.55)], 0.035, cg), "paint", DARK, 0.3)
    for x in (0.3, -0.45):
        M.add(rod("cagebar", (x, -0.6, 1.88), (x, 0.6, 1.88), 0.035, cg), "paint", DARK, 0.3)
    M.add(box("roof", (-0.08, 0.0, 1.92), (0.85, 1.25, 0.04)), "enamel", roof, 0.3)
    # (the load bed)
    if lod == 2:
        M.add(box("bed", (-1.0, 0.0, 0.85), (1.2, 1.3, 0.4)), "enamel", body, 0.4)
    else:
        M.add(box("bedfloor", (-1.0, 0.0, 0.7), (1.2, 1.3, 0.08)), "enamel", body, 0.4)
        for s in (-1, 1):
            M.add(box("bedside", (-1.0, s * 0.63, 0.88), (1.2, 0.04, 0.3)), "enamel", body, 0.4)
        M.add(box("bedend", (-1.58, 0.0, 0.88), (0.04, 1.3, 0.3)), "enamel", body, 0.4)
        M.add(box("bedend", (-0.42, 0.0, 0.88), (0.04, 1.3, 0.3)), "enamel", body, 0.4)
    if lod == 0:
        M.add(sweep("bullbar", [(1.55, -0.45, 0.45), (1.7, -0.45, 0.5), (1.7, -0.45, 0.95), (1.55, -0.45, 1.0)], 0.03, 6), "paint", DARK, 0.3)
        M.add(sweep("bullbar", [(1.55, 0.45, 0.45), (1.7, 0.45, 0.5), (1.7, 0.45, 0.95), (1.55, 0.45, 1.0)], 0.03, 6), "paint", DARK, 0.3)
        M.add(rod("bullbar", (1.7, -0.45, 0.75), (1.7, 0.45, 0.75), 0.03, 6), "paint", DARK, 0.3)
        for y in (-0.38, 0.38):
            M.add(box("headlamp", (1.56, y, 0.8), (0.03, 0.2, 0.08)), "lens", LAMP, 0.0)
            M.add(box("taillamp", (-1.61, y + (0.2 if y > 0 else -0.2), 0.85), (0.03, 0.08, 0.1)), "lens", REDLAMP, 0.0)
        M.add(rod("steer", (0.45, 0.25, 0.95), (0.3, 0.25, 1.15), 0.02, 4), "paint", DARK, 0.3)
        M.add(lathe("steerwheel", [(0.0, 0.0), (0.16, 0.0), (0.16, 0.03), (0.0, 0.03)], 12, at=(0.3, 0.25, 1.15)), "paint", DARK, 0.3)
    return M


def chaser(name, lod):
    """A big grain chaser on five axles, 3.4 m: a hopper with straight lower sides that flare out, ribbed outside,
    its unloading auger folded up at the front left, a drawbar to the tractor."""
    M = K.Model(name)
    for k in range(5):
        x = -3.4 + k * 1.15
        for s in (-1, 1):
            wheel(M, x, s * 1.15, 0.56, 0.56, 0.5, MUSTARD, lod, 12 if k % 2 == 0 else 0, out=s)
    M.add(box("chassis", (-1.1, 0.0, 1.0), (7.2, 1.0, 0.3)), "paint", DARK, 0.5)
    for s in (-1, 1):
        M.add(box("mudguard", (-1.1, s * 1.15, 1.17), (6.2, 0.6, 0.05)), "enamel", MUSTARD, 0.5)
    M.add(prism("hopper", [(-1.25, 1.2), (1.25, 1.2), (1.25, 1.9), (1.6, 2.7), (1.6, 3.3), (-1.6, 3.3), (-1.6, 2.7), (-1.25, 1.9)], "x", -4.6, 2.9), "enamel", MUSTARD, 0.45)
    if lod < 2:
        M.add(box("rim", (-0.85, 0.0, 3.33), (7.6, 3.3, 0.08)), "enamel", MUSTARD, 0.4)
        # (the ribs down each side: uprights and a diagonal lattice)
        n = 8 if lod == 0 else 4
        for s in (-1, 1):
            for k in range(n + 1):
                x = -4.55 + k * 7.4 / n
                M.add(sweep("rib", [(x, s * 1.3, 1.2), (x, s * 1.3, 1.9), (x, s * 1.65, 2.7), (x, s * 1.65, 3.3)], 0.04, 4, square=True), "enamel", MUSTARD, 0.5)
                if lod == 0 and k < n:
                    M.add(rod("brace", (x, s * 1.3, 1.25), (x + 7.4 / n, s * 1.3, 1.85), 0.03, 4), "enamel", MUSTARD, 0.5)
                    M.add(rod("brace", (x, s * 1.66, 3.25), (x + 7.4 / n, s * 1.66, 2.75), 0.03, 4), "enamel", MUSTARD, 0.5)
        # (the auger, folded up the front left)
        M.add(rod("auger", (2.9, 1.2, 1.2), (3.2, 1.25, 3.25), 0.22, 10 if lod == 0 else 6), "enamel", MUSTARD, 0.4)
        M.add(rod("auger", (3.2, 1.25, 3.25), (3.25, -0.6, 3.3), 0.17, 10 if lod == 0 else 6), "enamel", MUSTARD, 0.4)
    beam(M, "drawbar", (2.7, 0.0, 1.0), (4.9, 0.0, 0.62), 0.25, 0.3, "enamel", MUSTARD)
    if lod == 0:
        M.add(rod("jack", (3.8, 0.25, 0.0), (3.8, 0.25, 0.8), 0.05, 6), "paint", DARK, 0.5)
        M.add(box("jackfoot", (3.8, 0.25, 0.03), (0.25, 0.25, 0.06)), "paint", DARK, 0.6)
        M.add(box("eye", (5.0, 0.0, 0.6), (0.25, 0.12, 0.12)), "paint", DARK, 0.5)
        lights(M, -4.62, (-1.2, 1.2), 1.4, lod, rgb=REDLAMP)
    return M


def tipper(name, lod):
    """A twin-axle tipping trailer, 2.85 m: high sides with ribs, a tailgate, a sheet rolled over the top, the
    chassis green."""
    M = K.Model(name)
    for x in (-0.95, 0.1):
        for s in (-1, 1):
            wheel(M, x, s * 0.92, 0.52, 0.52, 0.42, TIPPER, lod, 12, out=s)
    M.add(box("chassis", (-0.2, 0.0, 1.0), (5.2, 0.9, 0.22)), "paint", TIP_GREEN, 0.5)
    M.add(box("body", (-0.3, 0.0, 1.95), (5.6, 2.35, 1.6), bevel=0.03 if lod == 0 else 0.0), "enamel", TIPPER, 0.45)
    M.add(box("sheet", (-0.3, 0.0, 2.8), (5.7, 2.45, 0.1)), "paint", SHEET, 0.3)
    for s in (-1, 1):
        M.add(box("mudguard", (-0.42, s * 0.92, 1.2), (2.0, 0.5, 0.04)), "paint", DARK, 0.5)
    beam(M, "drawbar", (2.3, 0.0, 1.0), (4.1, 0.0, 0.6), 0.2, 0.22, "paint", TIP_GREEN)
    if lod < 2:
        for s in (-1, 1):
            for k in range(6):
                x = -2.95 + k * 1.04
                M.add(box("rib", (x, s * 1.185, 1.95), (0.07, 0.04, 1.55)), "enamel", (0.5, 0.29, 0.04), 0.5)
            M.add(box("rail", (-0.3, s * 1.19, 2.33), (5.5, 0.04, 0.06)), "enamel", (0.5, 0.29, 0.04), 0.5)
    if lod == 0:
        for s in (-1, 1):
            for k in range(5):
                M.add(box("board", (-0.3, s * 1.18, 1.3 + k * 0.32), (5.5, 0.01, 0.015)), "paint", (0.4, 0.22, 0.03), 0.4)
            M.add(rod("sheetrope", (-3.0, s * 1.2, 2.72), (2.4, s * 1.2, 2.72), 0.012, 4), "paint", DARK, 0.3)
        M.add(rod("jack", (3.2, 0.2, 0.0), (3.2, 0.2, 0.78), 0.045, 6), "paint", DARK, 0.5)
        M.add(box("jackfoot", (3.2, 0.2, 0.03), (0.22, 0.22, 0.06)), "paint", DARK, 0.6)
        M.add(box("ram", (2.25, 0.0, 1.0), (0.2, 0.2, 0.45)), "steel", STEEL, 0.4)
        M.add(box("hinge", (-3.12, 0.0, 2.7), (0.06, 2.3, 0.08)), "paint", DARK, 0.4)
        lights(M, -3.12, (-1.0, 1.0), 1.1, lod, rgb=REDLAMP)
    return M


def baler(name, lod):
    """A big square baler, 3.05 m to its rails: a long body on a tandem axle, the pickup under the front, the
    flywheel at its front left, a bale coming off the chute behind."""
    M = K.Model(name)
    for x in (-1.7, -0.6):
        for s in (-1, 1):
            wheel(M, x, s * 1.35, 0.55, 0.55, 0.48, GRAPHITE, lod, 12, out=s)
    bev = 0.07 if lod == 0 else 0.0
    M.add(box("body", (-0.8, 0.0, 1.8), (5.6, 2.2, 1.9), bevel=bev), "enamel", IMP_RED, 0.45)
    M.add(box("chamber", (-3.9, 0.0, 1.35), (0.8, 1.3, 1.0)), "enamel", IMP_RED, 0.5)
    M.add(box("belly", (-1.0, 0.0, 0.78), (4.4, 1.6, 0.22)), "paint", DARK, 0.6)
    M.add(box("pickup", (2.25, 0.0, 0.35), (0.9, 2.3, 0.45)), "paint", DARK, 0.6)
    beam(M, "drawbar", (1.9, 0.0, 2.0), (4.4, 0.0, 0.75), 0.3, 0.3, "enamel", IMP_RED)
    M.add(box("chute", (-4.9, 0.0, 0.85), (1.4, 1.25, 0.06)), "paint", DARK, 0.6)
    M.add(box("bale", (-4.9, 0.0, 1.33), (1.3, 1.2, 0.88)), "timber", STRAW, 0.3)
    if lod < 2:
        M.add(lathe("flywheel", [(0.0, 0.0), (0.6, 0.0), (0.6, 0.12), (0.0, 0.12)], 18 if lod == 0 else 8), "paint", DARK, 0.4)
        bpy_ob = M.parts[-1].obj
        bpy_ob.matrix_world = Matrix.Translation((1.75, 1.15, 1.6)) @ Matrix.Rotation(-math.pi / 2, 4, "X")
        M.add(rod("pto", (2.3, 0.0, 1.45), (4.3, 0.0, 0.85), 0.06, 6), "paint", DARK, 0.4)
        # (the rails along the top)
        for s in (-1, 1):
            M.add(rod("toprail", (-3.4, s * 1.0, 3.0), (1.8, s * 1.0, 3.0), 0.025, 4), "paint", DARK, 0.3)
            for x in (-3.3, -1.6, 0.1, 1.7):
                M.add(rod("post", (x, s * 1.0, 2.7), (x, s * 1.0, 3.0), 0.02, 4), "paint", DARK, 0.3)
    if lod == 0:
        for x in (-2.9, -1.9, -0.9, 0.1, 1.1):
            for s in (-1, 1):
                M.add(box("door", (x, s * 1.105, 1.85), (0.02, 0.01, 1.5)), "paint", (0.2, 0.02, 0.015), 0.4)
        for k in range(12):
            M.add(rod("tine", (2.68, -1.05 + k * 0.19, 0.25), (2.85, -1.05 + k * 0.19, 0.05), 0.01, 4), "steel", STEEL, 0.5)
        M.add(box("knotters", (-0.3, 0.0, 2.85), (1.2, 1.6, 0.25)), "paint", DARK, 0.4)
        M.add(rod("jack", (3.4, 0.25, 0.0), (3.4, 0.25, 1.15), 0.05, 6), "paint", DARK, 0.5)
        M.add(box("jackfoot", (3.4, 0.25, 0.03), (0.25, 0.25, 0.06)), "paint", DARK, 0.6)
        lights(M, -4.32, (-0.7, 0.7), 1.0, lod, rgb=REDLAMP)
        M.add(box("stripe", (-0.8, 1.11, 2.55), (5.4, 0.01, 0.08)), "paint", (0.75, 0.74, 0.7), 0.2)
        M.add(box("stripe", (-0.8, -1.11, 2.55), (5.4, 0.01, 0.08)), "paint", (0.75, 0.74, 0.7), 0.2)
    return M


def drill(name, lod):
    """A 4 m seed drill, 2.4 m to its markers: a front tyre packer, a row of tines, the seed hopper with its
    platform, two rows of disc coulters behind, the bout markers folded up at each end."""
    M = K.Model(name)
    n = 8
    for k in range(n):
        y = (k - (n - 1) / 2) * 0.5
        wheel(M, 0.55, y, 0.42, 0.42, 0.36, GRAPHITE, lod if lod == 2 else max(lod, 1), 0, out=1)
    M.add(box("packerframe", (0.55, 0.0, 0.95), (0.2, 4.1, 0.2)), "paint", GRAPHITE, 0.5)
    M.add(box("frame", (-0.4, 0.0, 0.95), (0.2, 4.1, 0.2)), "paint", GRAPHITE, 0.5)
    M.add(box("spine", (0.3, 0.0, 0.95), (2.2, 0.3, 0.2)), "paint", GRAPHITE, 0.5)
    M.add(box("headstock", (1.5, 0.0, 0.85), (0.2, 1.0, 0.9)), "paint", GRAPHITE, 0.5)
    M.add(prism("hopper", [(-1.2, 1.15), (-0.2, 1.15), (0.1, 1.55), (0.1, 2.2), (-1.4, 2.2), (-1.4, 1.55)], "y", -1.9, 1.9), "enamel", IMP_RED, 0.4)
    M.add(box("lid", (-0.65, 0.0, 2.24), (1.55, 3.85, 0.08)), "enamel", (0.3, 0.035, 0.025), 0.3)
    if lod < 2:
        for s in (-1, 1):
            M.add(rod("marker", (-0.4, s * 2.05, 0.9), (-0.45, s * 2.05, 2.35), 0.04, 4), "enamel", IMP_RED, 0.4)
            M.add(rod("hopperleg", (-0.4, s * 1.7, 1.0), (-0.6, s * 1.7, 1.3), 0.06, 4), "paint", GRAPHITE, 0.4)
        nt = 16
        for k in range(nt):
            y = (k - (nt - 1) / 2) * 0.25
            if lod == 0:
                M.add(sweep("tine", [(1.2, y, 0.95), (1.25, y, 0.5), (1.12, y, 0.15), (1.0, y, 0.05)], 0.02, 4), "steel", STEEL, 0.6)
            for row, x in ((0, -1.1), (1, -1.5)):
                yy = y + (0.125 if row else 0.0)
                if abs(yy) > 1.95:
                    continue
                if lod == 0:
                    d = lathe("disc", [(0.0, 0.0), (0.19, 0.0), (0.19, 0.01), (0.0, 0.03)], 10, at=(0, 0, 0))
                    d.matrix_world = Matrix.Translation((x, yy, 0.2)) @ Matrix.Rotation(math.pi / 2, 4, "X")
                    M.add(d, "steel", STEEL, 0.6)
                    M.add(rod("arm", (x + 0.05, yy, 0.25), (x + 0.4 + 0.4 * row, yy, 0.95), 0.02, 4), "enamel", (0.06, 0.17, 0.05), 0.5)
        if lod == 1:
            M.add(box("coulters", (-1.3, 0.0, 0.25), (0.6, 3.9, 0.35)), "steel", STEEL, 0.6)
        M.add(box("coulterbar", (-1.25, 0.0, 0.9), (0.6, 4.0, 0.1)), "paint", GRAPHITE, 0.5)
    if lod == 0:
        M.add(box("platform", (-1.65, 0.0, 1.1), (0.4, 1.6, 0.04)), "paint", GRAPHITE, 0.4)
        rail(M, [(-1.85, -0.8, 1.1), (-1.85, -0.8, 1.9), (-1.85, 0.8, 1.9), (-1.85, 0.8, 1.1)], lod=lod)
        for k in range(3):
            M.add(box("step", (-1.85, 1.0, 0.35 + k * 0.25), (0.3, 0.3, 0.03)), "paint", GRAPHITE, 0.4)
        lights(M, -1.9, (-1.7, 1.7), 1.3, lod, rgb=REDLAMP)
    return M


def cultivator(name, lod):
    """A 10 m trailed cultivator opened out for work, 1.15 m: four rows of tines on a beam frame, a crumbler roller
    behind, the transport wheels in the middle, the drawbar forward."""
    M = K.Model(name)
    for s in (-1, 1):
        wheel(M, -0.6, s * 0.75, 0.45, 0.45, 0.38, GRAPHITE, lod, 10, out=s)
    for x in (1.4, 0.6, -0.2, -1.0):
        M.add(box("beam", (x, 0.0, 0.78), (0.12, 10.0, 0.12)), "enamel", IMP_RED, 0.5)
    for y in (-4.9, -2.5, 0.0, 2.5, 4.9):
        M.add(box("rail", (0.2, y, 0.86), (2.6, 0.12, 0.08)), "enamel", IMP_RED, 0.5)
    beam(M, "drawbar", (1.4, -1.0, 0.82), (3.7, 0.0, 0.6), 0.14, 0.14, "enamel", IMP_RED)
    beam(M, "drawbar", (1.4, 1.0, 0.82), (3.7, 0.0, 0.6), 0.14, 0.14, "enamel", IMP_RED)
    for s in (-1, 1):
        M.add(rod("roller", (-1.75, s * 0.05, 0.22), (-1.75, s * 4.95, 0.22), 0.22, 10 if lod == 0 else 6 if lod == 1 else 4), "steel", GRAPHITE, 0.6)
        M.add(rod("rollerarm", (-1.0, s * 4.9, 0.78), (-1.75, s * 4.9, 0.22), 0.04, 4), "enamel", IMP_RED, 0.5)
        if lod < 2:
            M.add(box("hinge", (0.2, s * 2.5, 1.05), (0.5, 0.2, 0.25)), "enamel", IMP_RED, 0.4)
            M.add(rod("ram", (0.2, s * 0.3, 1.05), (0.2, s * 2.4, 1.1), 0.04, 4), "steel", STEEL, 0.4)
    if lod == 0:
        for r, x in enumerate((1.4, 0.6, -0.2, -1.0)):
            n = 20
            for k in range(n):
                y = -4.75 + (k + (0.5 if r % 2 else 0.0)) * 9.5 / n
                M.add(sweep("tine", [(x, y, 0.72), (x + 0.05, y, 0.4), (x - 0.05, y, 0.12), (x - 0.15, y, 0.04)], 0.018, 4), "steel", STEEL, 0.6)
        lights(M, -2.0, (-4.9, 4.9), 0.6, lod, size=(0.04, 0.12, 0.12), rgb=REDLAMP)
    elif lod == 1:
        for x in (1.4, 0.6, -0.2, -1.0):
            M.add(box("tines", (x - 0.05, 0.0, 0.4), (0.12, 9.6, 0.7)), "steel", STEEL, 0.6)
    return M


# --- the list ------------------------------------------------------------------------------------------

def build(lod, only=None):
    """Every model at lod (0 the model, 1 and 2 its distance versions)."""
    s = "" if lod == 0 else "_LOD%d" % lod
    out = []

    def want(kind):
        return only is None or any(o in kind for o in only)
    fleet = [("farm_forager", forager), ("farm_sprayer", sprayer), ("farm_tractor4wd", tractor4wd),
             ("farm_tractortrack", tractortrack), ("farm_crawler", crawler), ("farm_loader", loader),
             ("farm_mower", mower), ("farm_buggy", buggy)]
    if want("farm_combine"):
        for d, maize in enumerate((False, True)):
            for i, liv in enumerate(LIVERIES):
                out.append(combine("SM_Prop_farm_combine_%d%s" % (d * 3 + i, s), liv, maize, lod))
    for kind, fn in fleet:
        if want(kind):
            for i, liv in enumerate(LIVERIES):
                out.append(fn("SM_Prop_%s_%d%s" % (kind, i, s), liv, lod))
    for kind, fn in (("farm_chaser", chaser), ("farm_tipper", tipper), ("farm_baler", baler), ("farm_drill", drill), ("farm_cultivator", cultivator)):
        if want(kind):
            out.append(fn("SM_Prop_%s_0%s" % (kind, s), lod))
    return out


def render(models, out_dir, lod):
    """The review pictures: every model from its front left corner (LOD 0), and at LOD 1 and 2 livery 0's and the
    implements, for the distance versions' strip; then the sheets."""
    import blender_farm_kit_render as FR
    R = FR.R
    from PIL import Image, ImageDraw
    os.makedirs(out_dir, exist_ok=True)

    class _U:
        def __init__(self, M):
            self.name, self.parts = M.name, M.parts
            for p in self.parts:
                p.lod = 2
    if lod > 0:
        models = [M for M in models if M.name.split("_LOD")[0][-1] == "0"]
    units = [_U(M) for M in models]
    sc = R._scene(units)
    size = (560, 380)
    for U in units:
        R._shoot(sc, U, 32, 14, size, os.path.join(out_dir, U.name + ".png"), lens=50.0)
        if lod == 0 and U.name[-1] == "0":
            R._shoot(sc, U, 212, 14, size, os.path.join(out_dir, U.name + "_back.png"), lens=50.0)
    if lod != 2:
        return
    # (the sheets: all of LOD 0; then each livery 0 model's front, back, LOD 1 and LOD 2 in a row)
    import glob
    font = R._font
    lod0 = sorted(p for p in glob.glob(os.path.join(out_dir, "SM_Prop_*.png")) if "_LOD" not in p and "_back" not in p)
    order = list(KINDS)
    lod0.sort(key=lambda p: (order.index(os.path.basename(p)[8:].rsplit("_", 1)[0]), p))
    for title, name, paths, cols in (
            ("Ride UK farm machinery: every model (our own designs; fleet liveries teal, burgundy, slate)", "farm_machines.png", lod0, 5),
            ("Ride UK farm machinery: front, back and the two hand-made distance versions", "farm_machines_lods.png",
             [q for p in lod0 if p.endswith("_0.png") for q in (p, p[:-4] + "_back.png", p[:-4] + "_LOD1.png", p[:-4] + "_LOD2.png")], 4)):
        pad, head = 8, 24
        rows = (len(paths) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * size[0] + (cols + 1) * pad, 70 + rows * (size[1] + head + pad)), (245, 245, 242))
        d = ImageDraw.Draw(sheet)
        d.text((pad, 18), title, fill=(20, 20, 20), font=font(24))
        for i, p in enumerate(paths):
            x = pad + (i % cols) * (size[0] + pad)
            y = 70 + (i // cols) * (size[1] + head + pad)
            d.text((x, y + 2), os.path.basename(p)[8:-4], fill=(25, 25, 25), font=font(15))
            if os.path.exists(p):
                sheet.paste(Image.open(p).convert("RGB"), (x, y + head))
        sheet.save(os.path.join(out_dir, name))
        print("sheet:", os.path.join(out_dir, name))


OFFSETS = {}


def centre(models):
    """Each model moved so its footprint's middle is at 0 (the yards and fields place things by their middles);
    the distance versions by their model's own shift, so nothing jumps between them."""
    for M in models:
        base = M.name.split("_LOD")[0]
        if base not in OFFSETS:
            lo = Vector((1e9, 1e9, 0.0)); hi = Vector((-1e9, -1e9, 0.0))
            for p in M.parts:
                for c in p.obj.bound_box:
                    w = p.obj.matrix_world @ Vector(c)
                    lo.x, lo.y = min(lo.x, w.x), min(lo.y, w.y)
                    hi.x, hi.y = max(hi.x, w.x), max(hi.y, w.y)
            OFFSETS[base] = (-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, 0.0)
        M.transform(Matrix.Translation(OFFSETS[base]))


def main():
    import blender_farm_props as FP
    args = sys.argv[1:]
    only = args[args.index("--only") + 1].split(",") if "--only" in args else None
    out = {}
    for lod in (0, 1, 2):
        K.clear()
        models = build(lod, only)
        centre(models)
        for M in models:
            nt, y0, y1, x0, x1, z0, z1 = K.write(M)
            out[M.name] = (nt, y0, y1)
            print("%-34s %6d triangles, %.2f..%.2f m up, %.1f x %.1f m" % (M.name, nt, y0, y1, x1 - x0, z1 - z0))
        if "--render" in args:
            render(models, args[args.index("--render") + 1], lod)
    FP.KINDS = KINDS
    FP.write_kinds(out)


if __name__ == "__main__":
    main()
