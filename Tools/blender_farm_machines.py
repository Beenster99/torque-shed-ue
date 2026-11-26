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
from blender_street_kit import lathe, rod, sweep   # noqa: E402


def box(name, centre, size, rot_z=0.0, bevel=0.0, rot=None, seg=3):
    """The street kit's box, its bevels rounder (seg segments)."""
    ob = K.box(name, centre, size, rot_z, bevel, rot)
    if bevel > 0:
        ob.modifiers["bevel"].segments = seg
    return ob

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
    """A tyre on its axle along y, its wheel centre in rim colour on the outer face (out: +1 the left side).
    Close up (wheel_hd): a rounded carcass, deep chevron lugs all round, a dished rim with its bolt circle."""
    if lod == 0:
        return wheel_hd(M, x, y, z, r, w, rim, lugs > 0, rim_frac, out, dual)
    seg = 16 if lod == 1 else 7
    prof = [(0.0, -w / 2), (r * 0.62, -w / 2), (r * 0.9, -w / 2), (r, -w / 2 + w * 0.18), (r, w / 2 - w * 0.18), (r * 0.9, w / 2), (r * 0.62, w / 2), (0.0, w / 2)]
    if lod == 2:
        prof = [(0.0, -w / 2), (r * 0.95, -w / 2), (r, -w / 2 + w * 0.2), (r, w / 2 - w * 0.2), (r * 0.95, w / 2), (0.0, w / 2)]
    ob = lathe("tyre", prof, seg)
    ob.matrix_world = Matrix.Translation((x, y, z)) @ Matrix.Rotation(math.pi / 2, 4, "X")
    M.add(ob, "paint", TYRE, 0.7)
    if lod == 2:
        return
    rr = r * rim_frac
    s = out if not dual or dual == "outer" else -out
    h = lathe("rim", [(0.0, 0.0), (rr, 0.0), (rr, 0.02), (rr * 0.5, 0.055), (0.0, 0.065)], 10)
    h.matrix_world = Matrix.Translation((x, y + s * (w / 2 - 0.01), z)) @ Matrix.Rotation(-s * math.pi / 2, 4, "X")
    M.add(h, "enamel", rim, 0.45)


def _on_axle(ob, x, y, z, s):
    """Stand a lathe (made about z, its +z outwards) on an axle along y at (x, y, z), facing side s."""
    ob.matrix_world = Matrix.Translation((x, y, z)) @ Matrix.Rotation(-s * math.pi / 2, 4, "X")
    return ob


def wheel_hd(M, x, y, z, r, w, rim, lugs=True, rim_frac=0.55, out=1, dual=False):
    """The close-up wheel: the tyre's carcass turned with rounded shoulders and a sidewall bulge, the tread's
    chevron lugs (deep bars running in from each shoulder, staggered), the rim's well and dished centre, its
    hub, the bolt circle and the valve."""
    seg = 40 if r > 0.6 else 32
    h = w / 2
    # (the carcass: bead, sidewall bulge, shoulder, crown; the crown sits under the lugs)
    rc = r - (0.045 if lugs else 0.0)
    prof = [(r * rim_frac * 0.98, -h + 0.03), (r * (rim_frac + 0.08), -h), (r * 0.8, -h - 0.015), (rc * 0.95, -h + 0.005),
            (rc * 0.99, -h + w * 0.04), (rc, -h + w * 0.09), (rc, h - w * 0.09), (rc * 0.99, h - w * 0.04), (rc * 0.95, h - 0.005),
            (r * 0.8, h + 0.015), (r * (rim_frac + 0.08), h), (r * rim_frac * 0.98, h - 0.03)]
    M.add(_on_axle(lathe("tyre", prof, seg), x, y, z, 1), "paint", TYRE, 0.7)
    if lugs:
        n = max(16, int(round(r * 26)))
        d = 0.045
        for k in range(n):
            for side in (-1, 1):
                a = 2 * math.pi * (k + (0.5 if side > 0 else 0.0)) / n
                # (a lug from the shoulder in towards the crown's middle, raked; its outer face a touch narrower)
                ca, sa = math.cos(a), math.sin(a)
                L = w * 0.5
                pts = [(-0.05, -0.01), (0.05, -0.01), (0.04, d), (-0.04, d)]
                lug = prism("lug", pts, "y", -L / 2, L / 2, 40.0)
                lug.matrix_world = (Matrix.Translation((x + ca * rc, y + side * w * 0.24, z + sa * rc))
                                    @ Matrix.Rotation(-a + math.pi / 2, 4, "Y") @ Matrix.Rotation(side * 0.38, 4, "Z"))
                M.add(lug, "paint", TYRE, 0.75)
    # (the rims: the outer face dished with the hub and bolts; the inner face a plain disc)
    rr = r * rim_frac
    faces = (out,) if dual == "outer" else (-out,) if dual == "inner" else (out, -out)
    for s in faces:
        outer = s == out
        if outer:
            pr = [(0.0, -0.04), (rr, -0.04), (rr, 0.015), (rr * 0.95, 0.03), (rr * 0.9, 0.0), (rr * 0.62, -0.02), (rr * 0.5, 0.03),
                  (rr * 0.32, 0.05), (rr * 0.3, 0.09), (rr * 0.18, 0.1), (rr * 0.12, 0.16), (0.0, 0.17)]
            M.add(_on_axle(lathe("rim", pr, 32), x, y + s * (w / 2 - 0.06), z, s), "enamel", rim, 0.45)
            nb = 10 if r > 0.6 else 8
            for b in range(nb):
                t = 2 * math.pi * b / nb
                bx, bz = x + math.cos(t) * rr * 0.24, z + math.sin(t) * rr * 0.24
                bolt = lathe("bolt", [(0.0, 0.0), (0.022, 0.0), (0.022, 0.03), (0.012, 0.045), (0.0, 0.045)], 6)
                M.add(_on_axle(bolt, bx, y + s * (w / 2 - 0.06 + 0.09), bz, s), "steel", STEEL, 0.5)
            # (the windows pressed in the dish, dark)
            for b in range(8):
                t = 2 * math.pi * (b + 0.5) / 8
                hole = lathe("window", [(0.0, 0.0), (rr * 0.07, 0.0), (rr * 0.07, 0.006), (0.0, 0.006)], 8)
                M.add(_on_axle(hole, x + math.cos(t) * rr * 0.62, y + s * (w / 2 - 0.06 + 0.022), z + math.sin(t) * rr * 0.62, s), "paint", DARK, 0.6)
            valve = rod("valve", (x + rr * 0.85, y + s * (w / 2 - 0.04), z + 0.02), (x + rr * 0.85, y + s * (w / 2 + 0.01), z + 0.03), 0.008, 5)
            M.add(valve, "steel", STEEL, 0.4)
        else:
            M.add(_on_axle(lathe("rim", [(0.0, 0.0), (rr, 0.0), (rr, 0.02), (rr * 0.35, 0.05), (0.0, 0.06)], 24), x, y + s * (w / 2 - 0.04), z, s),
                  "enamel", rim, 0.5)


def track(M, name, circles, y, w, lod, rim, rollers=()):
    """A rubber (or steel) track round its wheels circles [(x, z, r)], centred at y, w wide; the wheels' faces in rim
    colour, and road rollers (x, z, r) between. Close up: the belt with its guide ribs and lugs all the way round,
    the wheels spoked and bolted."""
    outline = hull_of_circles(circles, 36 if lod == 0 else 12 if lod == 1 else 6)
    if lod == 0:
        # (the belt as a ring: its outer run and the inner face held in by the belt's thickness)
        inner = hull_of_circles([(cx, cz, r - 0.07) for cx, cz, r in circles], 36)
        M.add(prism(name, outline, "y", y - w / 2, y + w / 2, 50.0), "paint", TYRE, 0.7)
        M.add(prism("guide", inner, "y", y - 0.05, y + 0.05, 50.0), "paint", TYRE, 0.7)
    else:
        M.add(prism(name, outline, "y", y - w / 2, y + w / 2, 50.0), "paint", TYRE, 0.7)
    if lod == 2:
        return
    s = 1 if y > 0 else -1
    for cx, cz, r in list(circles) + list(rollers):
        rr = r * 0.84
        if lod == 0:
            pr = [(0.0, 0.0), (rr, 0.0), (rr, 0.025), (rr * 0.9, 0.035), (rr * 0.55, 0.03), (rr * 0.45, 0.06), (rr * 0.25, 0.075), (0.0, 0.08)]
            M.add(_on_axle(lathe("trackwheel", pr, 28), cx, y + s * (w / 2 - 0.01), cz, s), "paint", GRAPHITE, 0.6)
            if r > 0.4:
                M.add(_on_axle(lathe("trackhub", [(0.0, 0.0), (r * 0.3, 0.0), (r * 0.3, 0.04), (r * 0.2, 0.08), (0.0, 0.09)], 16),
                               cx, y + s * (w / 2 + 0.06), cz, s), "enamel", rim, 0.4)
                for b in range(8):
                    t = 2 * math.pi * b / 8
                    M.add(_on_axle(lathe("bolt", [(0.0, 0.0), (0.02, 0.0), (0.02, 0.035), (0.0, 0.035)], 6),
                                   cx + math.cos(t) * r * 0.22, y + s * (w / 2 + 0.1), cz + math.sin(t) * r * 0.22, s), "steel", STEEL, 0.4)
        else:
            M.add(_on_axle(lathe("trackwheel", [(0.0, 0.0), (rr, 0.0), (rr, 0.02), (rr * 0.45, 0.05), (0.0, 0.06)], 10),
                           cx, y + s * (w / 2 - 0.005), cz, s), "paint", GRAPHITE, 0.6)
    if lod == 0:
        # (the lugs all round the belt's outside, chevrons)
        per = 0.2
        L = 0.0
        loop = outline + outline[:1]
        segs = [(Vector((a[0], 0, a[1])), Vector((b[0], 0, b[1]))) for a, b in zip(loop, loop[1:])]
        total = sum((b - a).length for a, b in segs)
        n = int(total / per)
        k = 0
        acc = 0.0
        for a, b in segs:
            sl = (b - a).length
            while k < n and acc + sl >= k * per:
                t = (k * per - acc) / max(sl, 1e-9)
                p = a + (b - a) * t
                tang = (b - a).normalized()
                ang = math.atan2(-tang.z, tang.x)
                for side in (-1, 1):
                    lug = box("tlug", (p.x, y + side * w * 0.24, p.z), (0.07, w * 0.44, 0.07),
                              rot=Matrix.Rotation(ang, 4, "Y") @ Matrix.Rotation(side * 0.35, 4, "Z"))
                    M.add(lug, "paint", TYRE, 0.8)
                k += 1
            acc += sl


def cab(M, x0, x1, hw, z0, z1, liv, lod, beacon=True, lamps=True, posts=DARK, mirrors=True):
    """A glass cab x0..x1 (front x1), hw half wide, z0..z1, its roof over in the livery's roof colour, a beacon.
    Close up: rounded corner pillars, the glass in its panes, a door with its handle and hinges, wipers, a roof
    with a curved edge and visor, the light bar, mirrors on arms, the steps up."""
    body, rim, roof, stripe = liv
    if lod > 0:
        M.add(box("glass", ((x0 + x1) / 2, 0.0, (z0 + z1) / 2), (x1 - x0, hw * 2, z1 - z0)), "lens", GLASS, 0.0)
        M.add(box("roof", ((x0 + x1) / 2 - 0.03, 0.0, z1 + 0.05), (x1 - x0 + 0.2, hw * 2 + 0.16, 0.1)), "enamel", roof, 0.2)
        if lod == 1:
            for x in (x0 + 0.04, x1 - 0.04):
                for y in (-hw + 0.03, hw - 0.03):
                    M.add(box("post", (x, y, (z0 + z1) / 2), (0.08, 0.08, z1 - z0)), "paint", posts, 0.2)
            M.add(box("cabfloor", ((x0 + x1) / 2, 0.0, z0 + 0.12), (x1 - x0 + 0.02, hw * 2 + 0.02, 0.24)), "enamel", body, 0.35)
        top = z1 + 0.1
        if beacon:
            M.add(box("beacon", (x0 + 0.25, hw - 0.2, top + 0.075), (0.12, 0.12, 0.15)), "lens", AMBER, 0.0)
            top += 0.15
        return top
    L, H = x1 - x0, z1 - z0
    xm = (x0 + x1) / 2
    # (the glass: one rounded block slightly inside the frame, the front raked forward at the top a little)
    g = box("glass", (xm, 0.0, (z0 + z1) / 2 + 0.05), (L - 0.04, hw * 2 - 0.04, H - 0.1), bevel=0.09)
    M.add(g, "lens", GLASS, 0.0)
    # (the lower panels in the body colour, rounded)
    M.add(box("cabfloor", (xm, 0.0, z0 + 0.16), (L + 0.04, hw * 2 + 0.04, 0.32), bevel=0.05), "enamel", body, 0.35)
    # (the pillars: round tubes at the corners, the B pillars at the door's back)
    for x in (x0 + 0.03, x1 - 0.03, x0 + L * 0.38):
        for yy in (-hw + 0.02, hw - 0.02):
            M.add(sweep("pillar", [(x, yy, z0 + 0.3), (x, yy, z1 - 0.02)], 0.042, 10), "paint", posts, 0.2)
    # (glazing bars: the waist rail and the top rail round the glass)
    for zz in (z0 + 0.34, z1 - 0.04):
        M.add(sweep("rail", [(x0 + 0.03, -hw + 0.02, zz), (x1 - 0.03, -hw + 0.02, zz), (x1 - 0.03, hw - 0.02, zz), (x0 + 0.03, hw - 0.02, zz), (x0 + 0.03, -hw + 0.02, zz)], 0.022, 6, cap=False), "paint", posts, 0.2)
    # (the door on the left: its seal line, handle and hinges)
    dx0, dx1 = x0 + L * 0.38, x1 - 0.06
    M.add(box("doorline", ((dx0 + dx1) / 2, hw + 0.006, z0 + 0.34), (dx1 - dx0, 0.012, 0.02)), "paint", DARK, 0.3)
    M.add(box("handle", (dx0 + 0.08, hw + 0.03, (z0 + z1) / 2), (0.03, 0.03, 0.2), bevel=0.01), "steel", STEEL, 0.3)
    for zz in (z0 + 0.6, z1 - 0.3):
        M.add(sweep("hinge", [(dx1, hw, zz), (dx1, hw + 0.03, zz)], 0.02, 6), "paint", DARK, 0.3)
    # (the wipers on the windscreen)
    for yy in (-hw * 0.4, hw * 0.4):
        M.add(sweep("wiper", [(x1 + 0.01, yy, z0 + 0.42), (x1 + 0.01, yy + 0.15, z0 + 0.95)], 0.01, 4), "paint", DARK, 0.3)
    # (the roof: a rounded shell over the edges, the visor over the windscreen, a hatch)
    M.add(box("roof", (xm - 0.03, 0.0, z1 + 0.06), (L + 0.24, hw * 2 + 0.2, 0.14), bevel=0.06), "enamel", roof, 0.2)
    M.add(box("roofcap", (xm - 0.06, 0.0, z1 + 0.15), (L * 0.75, hw * 1.5, 0.06), bevel=0.03), "enamel", roof, 0.2)
    M.add(tilted("visor", (x1 + 0.12, 0.0, z1 - 0.02), (0.22, hw * 2 - 0.05, 0.02), pitch=0.25), "lens", GLASS, 0.0)
    top = z1 + 0.18
    if lamps:
        for yy in (-hw + 0.22, -hw + 0.5, hw - 0.5, hw - 0.22):
            lamp(M, x1 + 0.12, yy, z1 + 0.06, 0.06, face=(1, 0, 0))
        for yy in (-hw + 0.35, hw - 0.35):
            lamp(M, x0 - 0.12, yy, z1 + 0.06, 0.06, face=(-1, 0, 0))
    if mirrors:
        for s in (-1, 1):
            M.add(sweep("mirrorarm", [(x1 - 0.05, s * hw, z1 - 0.1), (x1 + 0.05, s * (hw + 0.25), z1 - 0.05), (x1 + 0.02, s * (hw + 0.42), z1 - 0.2)], 0.014, 6), "paint", DARK, 0.2)
            M.add(box("mirror", (x1 + 0.02, s * (hw + 0.44), z1 - 0.38), (0.05, 0.2, 0.3), bevel=0.02), "paint", DARK, 0.2)
            M.add(box("mirrorglass", (x1 - 0.005, s * (hw + 0.44), z1 - 0.38), (0.006, 0.17, 0.26)), "lens", (0.3, 0.32, 0.34), 0.0)
    if beacon:
        bx, by = x0 + 0.25, hw - 0.2
        M.add(lathe("beaconbase", [(0.0, 0.0), (0.075, 0.0), (0.075, 0.025), (0.0, 0.03)], 16, at=(bx, by, top - 0.03)), "paint", DARK, 0.2)
        M.add(lathe("beacon", [(0.0, 0.0), (0.065, 0.0), (0.065, 0.1), (0.05, 0.13), (0.0, 0.14)], 16, at=(bx, by, top - 0.005)), "lens", AMBER, 0.0)
        top += 0.135
    return top


def lamp(M, x, y, z, r, face=(1, 0, 0), rgb=LAMP):
    """A round lamp: its housing, chrome bezel and lens, facing face (+x forward, -x back, or (0, +-1, 0) sideways)."""
    pr = [(0.0, -0.06), (r * 0.9, -0.06), (r, -0.02), (r, 0.0), (r * 1.05, 0.005), (r * 0.85, 0.01), (r * 0.8, 0.02), (0.0, 0.025)]
    ob = lathe("lamp", pr, 16)
    f = Vector(face).normalized()
    R = Vector((0, 0, 1)).rotation_difference(f).to_matrix().to_4x4()
    ob.matrix_world = Matrix.Translation((x, y, z)) @ R
    M.add(ob, "lens", rgb, 0.0)


def lights(M, x, ys, z, lod, size=(0.04, 0.2, 0.12), rgb=LAMP):
    """Lamps across a face at x (the front if x > 0, else the back): close up a pair of round lamps in a bezel each."""
    if lod == 0:
        face = (1, 0, 0) if x > 0 else (-1, 0, 0)
        for y in ys:
            M.add(box("lampbox", (x - face[0] * 0.03, y, z), (0.06, size[1] + 0.03, size[2] + 0.03), bevel=0.015), "paint", DARK, 0.3)
            for dy in (-size[1] * 0.25, size[1] * 0.25):
                lamp(M, x, y + dy, z, min(size[2], size[1]) * 0.42, face=face, rgb=rgb)


LOFT_N = [28]   # (the lofts' points round: fewer for the distance version)


def loft(name, sections, n=28, flat_bottom=True):
    """A smooth shell lofted through cross-sections along x: each (x, half width, bottom z, top z, roundness) a
    rounded rectangle (a superellipse of power roundness: 2 an ellipse, 6 nearly square), so bonnets, tanks and
    bodies come out as pressed panels with soft crowns, not boxes. flat_bottom keeps the underside flat."""
    v, f = [], []
    for x, hw, zb, zt, pw in sections:
        zc, hh = (zb + zt) / 2, (zt - zb) / 2
        for k in range(n):
            a = 2 * math.pi * k / n
            c, si = math.cos(a), math.sin(a)
            y = hw * math.copysign(abs(c) ** (2 / pw), c)
            z = zc + hh * math.copysign(abs(si) ** (2 / pw), si)
            if flat_bottom and si < 0:
                z = zc + hh * math.copysign(abs(si) ** (2 / max(pw, 8)), si)
            v.append((x, y, z))
    m = len(sections)
    for i in range(m - 1):
        for k in range(n):
            a, b = i * n + k, i * n + (k + 1) % n
            f.append((a, b, b + n, a + n))
    f.append(tuple(reversed(range(n))))
    f.append(tuple(range((m - 1) * n, m * n)))
    return K._object(name, v, f, 50.0)


def fender(M, x, y, z, r, w, rgb, a0=0.15, a1=2.6, n=14, t=0.035):
    """A curved mudguard over a wheel at (x, y, z) of radius r: a strip w wide bent round from angle a0 to a1
    (0 forward, pi/2 the top)."""
    pts = [(x + r * math.cos(a0 + (a1 - a0) * k / n), z + r * math.sin(a0 + (a1 - a0) * k / n)) for k in range(n + 1)]
    v, f = [], []
    for px, pz in pts:
        dx, dz = px - x, pz - z
        L = math.hypot(dx, dz)
        for yy in (y - w / 2, y + w / 2):
            v.append((px, yy, pz))
            v.append((px + dx / L * t, yy, pz + dz / L * t))
    for i in range(n):
        a = i * 4
        b = a + 4
        f += [(a + 1, a + 3, b + 3, b + 1), (a, b, b + 2, a + 2), (a, a + 1, b + 1, b), (a + 2, b + 2, b + 3, a + 3)]
    f += [(0, 2, 3, 1), (n * 4, n * 4 + 1, n * 4 + 3, n * 4 + 2)]
    M.add(K._object("fender", v, f, 40.0), "enamel", rgb, 0.4)


def rail(M, pts, r=0.02, lod=0, rgb=DARK):
    if lod == 0:
        M.add(sweep("rail", pts, r, 8), "paint", rgb, 0.3)


def grille(M, x, y, z, w, h, face=1, slats=None, rgb=DARK):
    """A slatted grille on a face across x (face +1 forward) or, with face 0, on a side wall at y (w along x)."""
    n = slats or max(4, int(h / 0.045))
    if face:
        M.add(box("grilleback", (x - face * 0.02, y, z), (0.02, w, h)), "paint", (0.01, 0.01, 0.012), 0.4)
        for k in range(n):
            M.add(box("slat", (x, y, z - h / 2 + (k + 0.5) * h / n), (0.03, w, h / n * 0.45)), "paint", rgb, 0.4)
    else:
        sy = 1 if y > 0 else -1
        M.add(box("grilleback", (x, y - sy * 0.01, z), (w, 0.01, h)), "paint", (0.01, 0.01, 0.012), 0.4)
        for k in range(n):
            M.add(box("slat", (x, y + sy * 0.005, z - h / 2 + (k + 0.5) * h / n), (w, 0.015, h / n * 0.45)), "paint", rgb, 0.4)


def hose(M, pts, r=0.016):
    M.add(sweep("hose", pts, r, 6), "paint", (0.012, 0.012, 0.014), 0.5)


def steps(M, x, y, z0, z1, n=4, w=0.42, rake=0.0):
    """A ladder of n treads from z0 to z1 at (x, y), its two stringers; rake runs it in x as it climbs."""
    s = 1 if y > 0 else -1
    for e in (-w / 2, w / 2):
        M.add(sweep("stringer", [(x + e, y, z0), (x + e + rake, y, z1)], 0.018, 6), "paint", DARK, 0.4)
    for k in range(n):
        t = (k + 0.5) / n
        zz = z0 + (z1 - z0) * t
        M.add(box("tread", (x + rake * t, y - s * 0.06, zz), (w, 0.18, 0.025)), "galv", (0.35, 0.35, 0.34), 0.5)
        if True:
            for i in range(3):
                M.add(box("treadhole", (x + rake * t - w / 3 + i * w / 3, y - s * 0.06, zz + 0.013), (0.06, 0.12, 0.004)), "paint", DARK, 0.5)


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
    M.add(loft("body", [(-4.2, 1.18, 0.9, 2.95, 7), (-4.05, 1.25, 0.9, 3.0, 8), (1.45, 1.25, 0.9, 3.0, 8), (1.6, 1.2, 0.95, 2.95, 7)], LOFT_N[0]) if lod <= 1
          else box("body", (-1.3, 0.0, 1.95), (5.8, 2.5, 2.1)), "enamel", body, 0.45)
    # (the grain tank, flaring out over the body)
    M.add(prism("tank", [(-1.25, 3.0), (1.25, 3.0), (1.55, 3.55), (-1.55, 3.55)], "x", -2.5, 0.55), "enamel", body, 0.35)
    M.add(loft("engine", [(-4.1, 1.05, 2.95, 3.22, 4), (-3.2, 1.15, 2.95, 3.3, 4), (-2.4, 1.15, 2.95, 3.3, 4)], LOFT_N[0]) if lod <= 1
          else box("engine", (-3.25, 0.0, 3.12), (1.7, 2.3, 0.26)), "enamel", body, 0.4)
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
    M.add(loft("body", [(-3.8, 1.08, 0.95, 2.6, 6), (-3.65, 1.15, 0.95, 2.65, 7), (0.85, 1.15, 0.95, 2.65, 7), (1.0, 1.1, 1.0, 2.6, 6)], LOFT_N[0]) if lod <= 1
          else box("body", (-1.4, 0.0, 1.8), (4.8, 2.3, 1.7)), "enamel", body, 0.45)
    M.add(loft("engine", [(-3.85, 0.95, 2.55, 3.0, 3), (-3.6, 1.1, 2.55, 3.15, 3.5), (-1.4, 1.1, 2.55, 3.15, 3.5), (-1.15, 1.0, 2.55, 3.05, 3)], LOFT_N[0]) if lod <= 1
          else prism("engine", [(-1.1, 2.65), (1.1, 2.65), (0.9, 3.1), (-0.9, 3.1)], "x", -3.8, -1.2), "enamel", body, 0.4)
    M.add(box("belly", (-1.2, 0.0, 0.85), (4.6, 1.5, 0.2)), "paint", DARK, 0.6)
    top = cab(M, -0.2, 1.4, 0.9, 2.62, 3.5, liv, lod)
    M.add(loft("cabbase", [(-0.25, 0.95, 1.98, 2.66, 6), (1.3, 0.95, 1.98, 2.66, 6), (1.45, 0.9, 2.0, 2.6, 5)], LOFT_N[0]) if lod <= 1
          else box("cabbase", (0.6, 0.0, 2.3), (1.7, 1.9, 0.65)), "enamel", body, 0.4)
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
    M.add(loft("tank", [(-2.55, 0.8, 1.95, 3.0, 2.6), (-2.45, 0.95, 1.9, 3.18, 3), (0.05, 0.95, 1.9, 3.18, 3), (0.15, 0.82, 1.95, 3.02, 2.6)], LOFT_N[0] + 4, False) if lod <= 1
          else box("tank", (-1.2, 0.0, 2.55), (2.7, 1.9, 1.3)), "enamel", body, 0.35)
    M.add(loft("engine", [(-3.35, 0.7, 1.9, 2.6, 4), (-3.2, 0.8, 1.9, 2.7, 5), (-2.45, 0.8, 1.9, 2.7, 5)], LOFT_N[0]) if lod <= 1
          else box("engine", (-2.9, 0.0, 2.3), (0.9, 1.6, 0.8)), "enamel", body, 0.4)
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
    M.add(loft("bonnet", [(0.3, 0.62, 1.4, 2.42, 5), (2.5, 0.62, 1.4, 2.42, 5), (3.05, 0.6, 1.42, 2.36, 4.5), (3.2, 0.55, 1.48, 2.26, 4)], LOFT_N[0]) if lod <= 1
          else prism("bonnet", [(-0.62, 1.4), (0.62, 1.4), (0.6, 2.25), (0.45, 2.42), (-0.45, 2.42), (-0.6, 2.25)], "x", 0.3, 3.2), "enamel", body, 0.35)
    M.add(box("grille", (3.21, 0.0, 1.88), (0.04, 1.1, 0.85)), "paint", DARK, 0.3)
    M.add(box("weights", (3.45, 0.0, 1.15), (0.45, 1.4, 0.5), bevel=bev), "paint", DARK, 0.5)
    M.add(box("rearframe", (-1.6, 0.0, 1.25), (2.8, 1.2, 0.6)), "paint", DARK, 0.5)
    M.add(box("cabbase", (-0.75, 0.0, 1.8), (1.9, 1.7, 0.6)), "enamel", body, 0.4)
    top = cab(M, -1.7, 0.2, 0.9, 2.1, 3.5, liv, lod)
    for s in (-1, 1):
        if lod == 0:
            fender(M, -1.6, s * 1.47, 1.0, 1.12, 1.6, body, 0.35, 2.8)
        else:
            M.add(box("fender", (-1.6, s * 1.48, 2.15), (2.0, 1.55, 0.06)), "enamel", body, 0.4)

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
    M.add(loft("bonnet", [(0.0, 0.56, 1.35, 2.32, 5), (2.1, 0.56, 1.35, 2.32, 5), (2.58, 0.54, 1.38, 2.26, 4.5), (2.72, 0.5, 1.43, 2.18, 4)], LOFT_N[0]) if lod <= 1
          else prism("bonnet", [(-0.55, 1.35), (0.55, 1.35), (0.55, 2.15), (0.4, 2.32), (-0.4, 2.32), (-0.55, 2.15)], "x", 0.0, 2.7), "enamel", body, 0.35)
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
    M.add(loft("bonnet", [(0.05, 0.38, 0.72, 1.25, 5), (1.0, 0.38, 0.72, 1.25, 5), (1.24, 0.36, 0.74, 1.2, 4.5), (1.31, 0.33, 0.78, 1.14, 4)], LOFT_N[0]) if lod <= 1
          else prism("bonnet", [(-0.38, 0.72), (0.38, 0.72), (0.38, 1.12), (0.28, 1.25), (-0.28, 1.25), (-0.38, 1.12)], "x", 0.05, 1.3), "enamel", body, 0.35)
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
    M.add(loft("rear", [(-1.86, 0.5, 0.55, 1.25, 4), (-1.75, 0.55, 0.52, 1.36, 5), (-0.5, 0.55, 0.52, 1.38, 5), (-0.35, 0.52, 0.55, 1.3, 4)], LOFT_N[0]) if lod <= 1
          else box("rear", (-1.1, 0.0, 0.95), (1.5, 1.1, 0.85)), "enamel", body, 0.4)
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
    M.add(loft("body", [(-1.25, 0.36, 0.38, 0.85, 4), (-1.15, 0.4, 0.37, 0.87, 5), (0.25, 0.4, 0.37, 0.87, 5), (0.35, 0.36, 0.4, 0.82, 4)], LOFT_N[0]) if lod <= 1
          else box("body", (-0.45, 0.0, 0.62), (1.6, 0.8, 0.5)), "enamel", body, 0.4)
    M.add(loft("engine", [(-1.36, 0.38, 0.84, 1.12, 3), (-1.25, 0.43, 0.83, 1.18, 3.5), (-0.6, 0.43, 0.83, 1.18, 3.5), (-0.55, 0.4, 0.84, 1.12, 3)], LOFT_N[0]) if lod <= 1
          else box("engine", (-0.95, 0.0, 1.0), (0.8, 0.85, 0.35)), "enamel", body, 0.4)
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
    M.add(loft("bonnet", [(0.55, 0.6, 0.5, 1.02, 5), (1.1, 0.6, 0.5, 0.98, 5), (1.45, 0.58, 0.5, 0.86, 4), (1.57, 0.54, 0.52, 0.78, 3.5)], LOFT_N[0]) if lod <= 1
          else prism("bonnet", [(0.55, 0.5), (1.55, 0.5), (1.55, 0.82), (1.2, 0.98), (0.55, 1.02)], "y", -0.62, 0.62), "enamel", body, 0.35)
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
    M.add(loft("body", [(-3.6, 1.05, 0.85, 2.7, 6), (-3.45, 1.1, 0.85, 2.75, 7), (1.85, 1.1, 0.85, 2.75, 7), (2.0, 1.05, 0.9, 2.7, 6)], LOFT_N[0]) if lod <= 1
          else box("body", (-0.8, 0.0, 1.8), (5.6, 2.2, 1.9)), "enamel", IMP_RED, 0.45)
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



# --- the close-up details (LOD 0 only): what makes a machine read as a machine up close ----------------------

CHROME = (0.62, 0.62, 0.6)


def ram(M, a, b, r=0.045, rgb=GRAPHITE):
    """A hydraulic ram from a to b: its barrel two thirds of the way, the bright rod out of it, the eyes."""
    a, b = Vector(a), Vector(b)
    m = a + (b - a) * 0.62
    M.add(sweep("rambarrel", [a, m], r, 10), "paint", rgb, 0.4)
    M.add(sweep("ramrod", [m, b], r * 0.5, 8), "steel", CHROME, 0.2)
    for e in (a, b):
        M.add(sweep("rameye", [e + Vector((0, -r, 0)), e + Vector((0, r, 0))], r * 0.8, 8), "paint", rgb, 0.4)


def linkage(M, x, z, w, liv):
    """A tractor's three-point linkage at its back (x the axle housing's back face): two lower links, the top
    link, lift arms and rods, the drawbar and the PTO's guard."""
    body = liv[0]
    for s in (-1, 1):
        M.add(sweep("lowerlink", [(x, s * w * 0.3, z - 0.25), (x - 0.85, s * w * 0.42, z - 0.45)], 0.035, 8), "paint", DARK, 0.5)
        M.add(lathe("balljoint", [(0.0, -0.04), (0.06, -0.04), (0.06, 0.04), (0.0, 0.04)], 10, at=(x - 0.88, s * w * 0.42, z - 0.45)), "steel", STEEL, 0.5)
        M.add(sweep("liftarm", [(x + 0.1, s * w * 0.28, z + 0.45), (x - 0.35, s * w * 0.3, z + 0.35)], 0.04, 8), "paint", DARK, 0.5)
        M.add(sweep("liftrod", [(x - 0.33, s * w * 0.3, z + 0.33), (x - 0.5, s * w * 0.36, z - 0.33)], 0.022, 6), "paint", DARK, 0.5)
        ram(M, (x + 0.05, s * w * 0.36, z - 0.2), (x - 0.25, s * w * 0.32, z + 0.38), 0.04)
    M.add(sweep("toplink", [(x + 0.02, 0.0, z + 0.3), (x - 0.7, 0.0, z + 0.15)], 0.032, 8), "paint", DARK, 0.5)
    M.add(box("drawbar", (x - 0.35, 0.0, z - 0.5), (0.7, 0.12, 0.06), bevel=0.01), "steel", STEEL, 0.5)
    M.add(lathe("ptoguard", [(0.0, 0.0), (0.13, 0.0), (0.13, 0.12), (0.0, 0.14)], 14, at=(0, 0, 0)), "enamel", body, 0.4)
    M.parts[-1].obj.matrix_world = Matrix.Translation((x + 0.02, 0.0, z - 0.15)) @ Matrix.Rotation(-math.pi / 2, 4, "Y")
    for k, dy in enumerate((-0.25, -0.12, 0.12, 0.25)):
        M.add(lathe("coupler", [(0.0, 0.0), (0.03, 0.0), (0.03, 0.06), (0.0, 0.06)], 8), "paint", (0.3, 0.02, 0.02) if k % 2 else (0.02, 0.06, 0.3), 0.3)
        M.parts[-1].obj.matrix_world = Matrix.Translation((x + 0.03, dy, z + 0.12)) @ Matrix.Rotation(-math.pi / 2, 4, "Y")


def handrail(M, pts):
    M.add(sweep("handrail", pts, 0.02, 8, cap=True), "galv", (0.45, 0.45, 0.44), 0.4)


def more_combine(M, liv, maize):
    body, rim, roof, stripe = liv
    for s in (-1, 1):
        grille(M, -3.25, s * 1.17, 2.5, 1.3, 0.6, face=0)
        # (side panels' louvres and latches, the hoses down to the header)
        for k in range(4):
            M.add(box("latch", (-0.5 - k * 1.05, s * 1.27, 2.35), (0.08, 0.02, 0.04)), "steel", STEEL, 0.3)
        hose(M, [(1.6, s * 0.6, 1.55), (2.4, s * 0.72, 1.35), (3.3, s * 0.75, 0.95), (3.5, s * 0.8, 0.8)], 0.02)
        hose(M, [(1.6, s * 0.55, 1.5), (2.4, s * 0.67, 1.3), (3.3, s * 0.7, 0.9), (3.5, s * 0.75, 0.75)], 0.016)
        ram(M, (1.3, s * 0.5, 0.95), (2.6, s * 0.55, 1.12), 0.05)
        # (mudflaps behind the drive wheels, the steering axle's rams)
        M.add(box("mudflap", (0.1, s * 1.45, 0.55), (0.02, 0.7, 0.5)), "paint", TYRE, 0.6)
        ram(M, (-3.0, s * 0.3, 0.6), (-3.2, s * 0.95, 0.6), 0.035)
    # (the platform's handrails round the cab, the engine deck's ladder and rails)
    handrail(M, [(1.2, -1.15, 2.94), (1.2, -1.15, 3.9), (2.35, -1.15, 3.9), (2.35, -1.15, 2.94)])
    handrail(M, [(2.4, -1.1, 2.94), (2.4, -1.1, 3.5), (2.4, 1.1, 3.5), (2.4, 1.1, 2.94)])
    steps(M, -4.1, -1.2, 1.2, 3.0, 5, 0.4, rake=0.15)
    handrail(M, [(-4.1, -1.2, 3.25), (-4.1, -1.2, 3.75), (-2.4, -1.2, 3.75), (-2.4, -1.2, 3.25)])
    # (the grain tank's folding covers, its sight window and the tank lamps)
    for k in range(5):
        M.add(box("tankrib", (-2.3 + k * 0.62, 0.0, 3.57), (0.05, 3.05, 0.04)), "enamel", body, 0.3)
    M.add(box("sightglass", (-1.0, 1.52, 3.25), (0.5, 0.02, 0.22), bevel=0.01), "lens", GLASS, 0.0)
    lights(M, 2.45, (-0.85, 0.85), 2.85, 0, size=(0.04, 0.22, 0.12))
    # (the straw spreader's hood and its discs under the back)
    for s in (-1, 1):
        M.add(lathe("spreader", [(0.0, 0.0), (0.38, 0.0), (0.38, 0.05), (0.1, 0.12), (0.0, 0.12)], 20, at=(-4.45, s * 0.5, 0.95)), "paint", DARK, 0.6)
    M.add(lathe("exhaustcap", [(0.0, 0.0), (0.075, 0.0), (0.085, 0.06), (0.0, 0.06)], 12, at=(-3.8, -0.7, 3.62)), "steel", DARK, 0.5)
    M.add(lathe("aircleaner", [(0.0, 0.0), (0.18, 0.0), (0.18, 0.35), (0.12, 0.42), (0.0, 0.42)], 18, at=(-3.6, 0.6, 3.25)), "paint", DARK, 0.4)
    if not maize:
        # (the header: its skids, the knife's guards, the reel's tines, the side drives' shields)
        for k in range(48):
            y = -3.75 + k * 7.5 / 47
            M.add(box("guard", (4.76, y, 0.12), (0.12, 0.03, 0.03)), "steel", STEEL, 0.5)
        for k in range(6):
            y = -3.0 + k * 1.2
            M.add(box("skid", (4.0, y, 0.08), (1.2, 0.12, 0.04), bevel=0.01), "steel", STEEL, 0.6)
        for s in (-1, 1):
            M.add(box("driveshield", (3.6, s * 3.88, 0.7), (0.9, 0.08, 0.7), bevel=0.03), "enamel", body, 0.4)
            ram(M, (3.4, s * 3.72, 0.95), (3.9, s * 3.72, 1.32), 0.035)
        for k in range(6):
            a = 2 * math.pi * k / 6
            bx, bz = 4.2 + 0.55 * math.cos(a), 1.35 + 0.55 * math.sin(a)
            for i in range(24):
                y = -3.55 + i * 7.1 / 23
                M.add(sweep("reeltine", [(bx, y, bz), (bx + 0.05, y, bz - 0.2)], 0.007, 4), "paint", (0.5, 0.48, 0.42), 0.4)


def more_forager(M, liv):
    body, rim, roof, stripe = liv
    for s in (-1, 1):
        grille(M, -2.5, s * 1.17, 2.2, 2.0, 0.8, face=0)
        hose(M, [(1.4, s * 0.55, 1.5), (2.2, s * 0.62, 1.25), (2.8, s * 0.7, 0.95)], 0.02)
        ram(M, (1.1, s * 0.45, 0.9), (2.1, s * 0.5, 1.05), 0.05)
        M.add(box("mudflap", (-0.2, s * 1.38, 0.55), (0.02, 0.7, 0.5)), "paint", TYRE, 0.6)
    # (rings round the spout, its lamp, the turret's ram)
    sp = [(-0.75, 0.0, 2.65), (-0.8, 0.0, 3.5), (-0.55, 0.0, 4.15), (0.15, 0.0, 4.4), (1.0, 0.0, 4.32), (1.6, 0.0, 4.05)]
    for (ax, ay, az), (bx, by, bz) in zip(sp, sp[1:]):
        M.add(box("spoutflange", ((ax + bx) / 2, 0.0, (az + bz) / 2), (0.06, 0.5, 0.5), rot=Matrix.Rotation(math.atan2(bx - ax, bz - az), 4, "Y")), "paint", DARK, 0.4)
    lamp(M, 1.3, 0.25, 4.25, 0.05)
    ram(M, (-0.6, 0.3, 2.9), (-0.45, 0.25, 3.9), 0.04)
    handrail(M, [(-0.25, -0.95, 2.65), (-0.25, -0.95, 3.4), (1.45, -0.95, 3.4)])
    lights(M, 1.42, (-0.75, 0.75), 2.55, 0, size=(0.04, 0.22, 0.12))
    M.add(lathe("aircleaner", [(0.0, 0.0), (0.2, 0.0), (0.2, 0.3), (0.0, 0.32)], 18, at=(-3.0, 0.55, 3.1)), "paint", DARK, 0.4)
    M.add(box("counterweight", (-3.95, 0.0, 1.2), (0.3, 1.8, 0.7), bevel=0.06), "paint", DARK, 0.5)


def more_sprayer(M, liv):
    body, rim, roof, stripe = liv
    for s in (-1, 1):
        y = s * 1.35
        # (the nozzle bodies hanging under the boom's lower rail, and the feed pipe along it)
        for k in range(22):
            x = -3.0 + k * 0.25
            M.add(sweep("nozzle", [(x, y, 2.12), (x, y, 2.0)], 0.012, 5), "paint", (0.4, 0.04, 0.02) if k % 2 else (0.05, 0.05, 0.25), 0.3)
        M.add(sweep("boompipe", [(-3.05, y + s * 0.04, 2.18), (2.4, y + s * 0.04, 2.18)], 0.015, 6), "steel", STEEL, 0.3)
        hose(M, [(-3.1, s * 0.6, 2.1), (-3.0, s * 1.0, 2.2), (-2.9, y, 2.3)], 0.022)
        for x in (1.75, -1.75):
            hose(M, [(x, s * 0.8, 1.75), (x + 0.05, s * 0.85, 1.3), (x, s * 0.9, 1.0)], 0.018)
    # (the tank's sight gauge, lid hinges, the chemical induction bowl and the clean water tank)
    M.add(box("gauge", (-0.6, 0.96, 2.6), (0.04, 0.02, 0.9)), "lens", (0.3, 0.32, 0.3), 0.0)
    for k in range(9):
        M.add(box("gaugemark", (-0.6, 0.975, 2.2 + k * 0.1), (0.06, 0.004, 0.008)), "paint", DARK, 0.2)
    M.add(lathe("induction", [(0.0, 0.0), (0.06, 0.0), (0.25, 0.3), (0.26, 0.34), (0.0, 0.34)], 20, at=(-0.2, -1.05, 2.25)), "paint", DARK, 0.4)
    M.add(box("cleanwater", (-2.85, 0.0, 2.85), (0.6, 1.4, 0.3), bevel=0.08), "paint", (0.5, 0.5, 0.48), 0.3)
    handrail(M, [(-2.4, -0.95, 3.25), (-2.4, -0.95, 3.6), (0.0, -0.95, 3.6), (0.0, -0.95, 3.25)])
    lights(M, 2.32, (-0.6, 0.6), 2.0, 0, size=(0.04, 0.22, 0.12))


def more_tractor4wd(M, liv):
    body, rim, roof, stripe = liv
    grille(M, 3.23, 0.0, 1.88, 1.0, 0.8)
    lights(M, 3.22, (-0.42, 0.42), 2.17, 0, size=(0.04, 0.24, 0.12))
    linkage(M, -2.95, 1.15, 1.3, liv)
    # (the pivot's steering rams and the hoses across it)
    for s in (-1, 1):
        ram(M, (0.55, s * 0.45, 1.15), (-0.4, s * 0.5, 1.2), 0.05)
        hose(M, [(0.6, s * 0.2, 1.45), (0.2, s * 0.3, 1.25), (-0.3, s * 0.2, 1.45)], 0.02)
        for k in range(4):
            M.add(box("louvre", (1.0 + k * 0.4, s * 0.615, 1.95), (0.3, 0.02, 0.05)), "paint", DARK, 0.3)
    steps(M, -0.35, 1.3, 0.45, 1.8, 4, 0.42)
    handrail(M, [(-0.15, 0.98, 1.85), (-0.15, 0.98, 3.1)])
    M.add(box("toolbox", (-1.6, -0.75, 1.65), (0.6, 0.25, 0.3), bevel=0.03), "paint", DARK, 0.4)
    M.add(box("battery", (-1.6, 0.75, 1.65), (0.6, 0.25, 0.3), bevel=0.03), "paint", DARK, 0.4)


def more_tractortrack(M, liv):
    body, rim, roof, stripe = liv
    grille(M, 2.73, 0.0, 1.85, 0.9, 0.72)
    lights(M, 2.72, (-0.38, 0.38), 2.1, 0, size=(0.04, 0.22, 0.12))
    linkage(M, -2.1, 1.05, 1.2, liv)
    for k in range(6):
        M.add(box("weightplate", (2.75 + 0.0, -0.55 + k * 0.22, 1.05), (0.42, 0.18, 0.5), bevel=0.02), "paint", GRAPHITE, 0.5)
    for s in (-1, 1):
        for k in range(4):
            M.add(box("louvre", (0.5 + k * 0.4, s * 0.56, 1.95), (0.3, 0.02, 0.05)), "paint", DARK, 0.3)
        M.add(box("trackframe", (0.1, s * 1.0, 0.55), (2.6, 0.3, 0.25), bevel=0.04), "paint", GRAPHITE, 0.5)
    steps(M, -0.3, 1.4, 0.85, 1.75, 3, 0.36)
    handrail(M, [(-0.1, 0.92, 1.85), (-0.1, 0.92, 2.9)])


def more_crawler(M, liv):
    body, rim, roof, stripe = liv
    grille(M, 1.32, 0.0, 1.0, 0.56, 0.34)
    lamp(M, 1.33, -0.2, 1.15, 0.04)
    lamp(M, 1.33, 0.2, 1.15, 0.04)
    for s in (-1, 1):
        M.add(sweep("lever", [(-0.35, s * 0.2, 0.86), (-0.3, s * 0.22, 1.3)], 0.012, 5), "paint", DARK, 0.3)
        M.add(lathe("knob", [(0.0, 0.0), (0.025, 0.0), (0.025, 0.04), (0.0, 0.05)], 8, at=(-0.3, s * 0.22, 1.3)), "paint", (0.4, 0.03, 0.02), 0.2)
        M.add(box("armrest", (-0.55, s * 0.26, 1.25), (0.35, 0.05, 0.05), bevel=0.015), "paint", SEAT, 0.3)
    for k in range(4):
        M.add(box("louvre", (0.4 + k * 0.2, 0.39, 1.0), (0.12, 0.02, 0.03)), "paint", DARK, 0.3)
    linkage(M, -0.95, 0.55, 0.7, liv)


def more_loader(M, liv):
    body, rim, roof, stripe = liv
    # (hoses along the boom to the carriage, the carriage's back rest, the steering pivot's ram)
    for dz in (0.0, 0.05):
        hose(M, [(-0.9, -0.48, 1.9 + dz), (0.4, -0.5, 1.45 + dz), (1.5, -0.5, 1.02 + dz), (1.8, -0.3, 0.9 + dz)], 0.016)
    for k in range(5):
        M.add(sweep("backrest", [(1.88, -0.45 + k * 0.225, 0.9), (1.88, -0.45 + k * 0.225, 1.45)], 0.015, 5), "paint", DARK, 0.4)
    M.add(sweep("backresttop", [(1.88, -0.47, 1.45), (1.88, 0.47, 1.45)], 0.02, 6), "paint", DARK, 0.4)
    ram(M, (-0.2, 0.3, 0.6), (0.4, 0.35, 0.6), 0.04)
    grille(M, -1.87, 0.0, 0.95, 0.75, 0.45, face=-1)
    lights(M, -1.86, (-0.42, 0.42), 1.25, 0, size=(0.04, 0.12, 0.1), rgb=REDLAMP)
    steps(M, -0.1, 0.62, 0.35, 1.25, 2, 0.32)
    for s in (-1, 1):
        M.add(sweep("mirrorarm", [(0.2, s * 0.58, 2.2), (0.3, s * 0.8, 2.15)], 0.012, 5), "paint", DARK, 0.2)
        M.add(box("mirror", (0.3, s * 0.82, 2.0), (0.04, 0.14, 0.22), bevel=0.015), "paint", DARK, 0.2)


def more_mower(M, liv):
    body, rim, roof, stripe = liv
    units = [(1.25, -0.95), (1.25, 0.0), (1.25, 0.95), (-0.15, -1.15), (-0.15, 1.15)]
    for ux, uy in units:
        # (each reel a cage of helical blades round its shaft)
        for b in range(6):
            pts = []
            for i in range(7):
                t = i / 6
                a = 2 * math.pi * b / 6 + t * 1.2
                pts.append((ux + 0.13 * math.cos(a), uy - 0.38 + t * 0.76, 0.2 + 0.13 * math.sin(a)))
            M.add(sweep("blade", pts, 0.01, 4), "steel", STEEL, 0.4)
        hose(M, [(0.0 if ux > 0 else -0.3, uy * 0.3, 0.6), (ux - 0.2, uy * 0.9, 0.55), (ux, uy, 0.46)], 0.014)
    grille(M, -1.36, 0.0, 1.0, 0.7, 0.3, face=-1)
    M.add(box("fueltank", (-0.95, 0.38, 0.65), (0.5, 0.15, 0.3), bevel=0.04), "paint", DARK, 0.4)
    M.add(lathe("fuelcap", [(0.0, 0.0), (0.04, 0.0), (0.04, 0.03), (0.0, 0.03)], 10, at=(-0.95, 0.38, 0.8)), "paint", (0.5, 0.4, 0.04), 0.3)
    lamp(M, 0.38, -0.25, 1.02, 0.04)
    lamp(M, 0.38, 0.25, 1.02, 0.04)


def more_buggy(M, liv):
    body, rim, roof, stripe = liv
    for s in (-1, 1):
        # (wheel arches over the front wheels, the rear wings, the mirrors on the cage)
        arch = [(1.05 + 0.45 * math.cos(a), s * 0.62, 0.33 + 0.45 * math.sin(a)) for a in [math.pi * k / 10 for k in range(11)]]
        M.add(sweep("arch", arch, 0.04, 6, square=True), "paint", DARK, 0.4)
        M.add(box("wing", (-0.9, s * 0.66, 0.78), (0.8, 0.1, 0.06), bevel=0.02), "paint", DARK, 0.4)
        M.add(box("mirror", (0.62, s * 0.72, 1.15), (0.04, 0.12, 0.09), bevel=0.015), "paint", DARK, 0.2)
        M.add(box("seatbelt", (-0.24, s * 0.35, 1.0), (0.02, 0.04, 0.4)), "paint", (0.05, 0.05, 0.05), 0.3)
    for y in (-0.38, 0.38):
        lamp(M, 1.56, y, 0.8, 0.07)
    M.add(box("dashpanel", (0.5, 0.25, 1.12), (0.1, 0.4, 0.06), bevel=0.02), "paint", (0.01, 0.01, 0.01), 0.2)
    M.add(box("hitch", (-1.68, 0.0, 0.45), (0.2, 0.1, 0.08), bevel=0.01), "steel", STEEL, 0.5)
    M.add(lathe("towball", [(0.0, 0.0), (0.025, 0.0), (0.025, 0.04), (0.025, 0.06), (0.0, 0.08)], 10, at=(-1.75, 0.0, 0.49)), "steel", STEEL, 0.5)
    for k in range(6):
        M.add(box("bedrib", (-1.5 + k * 0.2, 0.0, 0.745), (0.03, 1.25, 0.012)), "enamel", body, 0.4)


def more_chaser(M):
    for s in (-1, 1):
        hose(M, [(2.9, s * 0.15, 1.05), (3.8, s * 0.12, 0.85), (4.7, s * 0.08, 0.75), (5.0, s * 0.2, 0.9)], 0.018)
    steps(M, 2.95, -1.3, 0.5, 3.2, 7, 0.38)
    handrail(M, [(2.95, -1.25, 3.3), (2.95, -1.25, 3.75), (2.4, -1.25, 3.75)])
    M.add(box("window", (2.92, -0.5, 2.6), (0.02, 0.6, 0.35), bevel=0.01), "lens", GLASS, 0.0)
    # (the auger's flights showing at its spout, its fold hinge and ram)
    for k in range(8):
        a = k * 0.8
        M.add(box("flight", (3.25 + 0.1 * math.cos(a), -0.6 + 0.1 * math.sin(a), 3.3), (0.03, 0.18, 0.18), rot=Matrix.Rotation(a, 4, "X")), "galv", (0.4, 0.4, 0.38), 0.5)
    ram(M, (2.95, 1.0, 1.4), (3.15, 1.2, 2.9), 0.05)
    lights(M, -4.62, (-1.2, 1.2), 1.4, 0, size=(0.04, 0.2, 0.12), rgb=REDLAMP)
    for x in (-4.2, -1.1, 2.0):
        for s in (-1, 1):
            M.add(box("reflector", (x, s * 1.62, 2.0), (0.12, 0.01, 0.06)), "lens", (0.6, 0.3, 0.02), 0.0)


def more_tipper(M):
    ram(M, (2.25, 0.0, 0.95), (2.25, 0.0, 1.6), 0.09, GRAPHITE)
    for s in (-1, 1):
        hose(M, [(2.4, s * 0.1, 1.0), (3.3, s * 0.1, 0.8), (4.1, s * 0.12, 0.7)], 0.016)
        for z in (1.45, 2.4):
            M.add(box("latch", (-3.13, s * 1.0, z), (0.06, 0.12, 0.1), bevel=0.01), "steel", STEEL, 0.4)
        M.add(box("bracket", (-0.42, s * 0.92, 1.1), (0.06, 0.06, 0.2)), "paint", DARK, 0.4)
    lights(M, -3.12, (-1.0, 1.0), 1.1, 0, size=(0.04, 0.2, 0.12), rgb=REDLAMP)


def more_baler(M):
    for k in range(30):
        y = -1.05 + k * 2.1 / 29
        M.add(sweep("pickuptine", [(2.68, y, 0.3), (2.85, y, 0.12), (2.95, y, 0.06)], 0.008, 4), "steel", STEEL, 0.5)
    for x in (-2.9, -1.9, -0.9, 0.1, 1.1):
        for s in (-1, 1):
            M.add(box("doorhandle", (x + 0.35, s * 1.12, 1.85), (0.03, 0.03, 0.15), bevel=0.008), "steel", STEEL, 0.3)
    M.add(lathe("ptoguard", [(0.0, 0.0), (0.09, 0.0), (0.12, 1.6), (0.0, 1.62)], 12), "paint", (0.55, 0.42, 0.04), 0.3)
    M.parts[-1].obj.matrix_world = Matrix.Translation((2.4, 0.0, 1.4)) @ Matrix.Rotation(math.radians(110), 4, "Y")
    for k in range(5):
        M.add(sweep("roller", [(-4.3 - k * 0.25, -0.6, 0.9), (-4.3 - k * 0.25, 0.6, 0.9)], 0.035, 8), "steel", STEEL, 0.5)
    for s in (-1, 1):
        hose(M, [(1.9, s * 0.2, 2.0), (3.2, s * 0.15, 1.4), (4.3, s * 0.12, 0.95)], 0.016)
    for k in range(4):
        M.add(box("twinebox", (-1.4 + k * 0.5, 1.2, 1.25), (0.4, 0.08, 0.6), bevel=0.02), "paint", (0.2, 0.02, 0.015), 0.4)


def more_drill(M):
    # (the seed tubes from the hopper's metering down to every coulter)
    for k in range(16):
        y = (k - 7.5) * 0.25
        for row, x in ((0, -1.1), (1, -1.5)):
            yy = y + (0.125 if row else 0.0)
            if abs(yy) > 1.95:
                continue
            M.add(sweep("seedtube", [(-0.9, yy * 0.95, 1.15), (-1.0 - 0.2 * row, yy, 0.8), (x + 0.05, yy, 0.35)], 0.012, 5), "paint", (0.08, 0.08, 0.08), 0.4)
    for s in (-1, 1):
        M.add(sweep("markerdisc", [(-0.45, s * 2.05, 2.35), (-0.45, s * 2.12, 2.35)], 0.22, 14), "steel", STEEL, 0.5)
        ram(M, (-0.3, s * 1.85, 1.0), (-0.42, s * 2.0, 1.8), 0.03)
    M.add(lathe("fan", [(0.0, 0.0), (0.22, 0.0), (0.22, 0.2), (0.0, 0.22)], 18, at=(1.25, 0.0, 1.05)), "paint", (0.4, 0.04, 0.03), 0.4)
    M.add(sweep("airpipe", [(1.25, 0.0, 1.25), (0.6, 0.0, 1.5), (0.1, 0.0, 1.55)], 0.08, 10), "paint", (0.08, 0.08, 0.08), 0.4)
    hose(M, [(1.5, 0.2, 1.2), (2.0, 0.25, 0.9)], 0.016)


def more_cultivator(M):
    for s in (-1, 1):
        hose(M, [(3.6, s * 0.08, 0.65), (1.6, s * 0.5, 0.95), (0.2, s * 1.0, 1.15), (0.2, s * 2.3, 1.15)], 0.016)
        ram(M, (0.2, s * 0.35, 1.05), (0.2, s * 2.35, 1.12), 0.045)
        # (the levelling boards behind the tines)
        M.add(box("leveller", (-1.45, s * 2.5, 0.3), (0.05, 4.9, 0.18), bevel=0.01), "steel", STEEL, 0.5)
        ram(M, (-0.6, s * 0.75, 0.8), (-0.6, s * 0.75, 0.45), 0.04)


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
                M = combine("SM_Prop_farm_combine_%d%s" % (d * 3 + i, s), liv, maize, lod)
                if lod == 0:
                    more_combine(M, liv, maize)
                out.append(M)
    for kind, fn in fleet:
        if want(kind):
            for i, liv in enumerate(LIVERIES):
                M = fn("SM_Prop_%s_%d%s" % (kind, i, s), liv, lod)
                if lod == 0:
                    globals()["more_" + kind[5:]](M, liv)
                out.append(M)
    for kind, fn in (("farm_chaser", chaser), ("farm_tipper", tipper), ("farm_baler", baler), ("farm_drill", drill), ("farm_cultivator", cultivator)):
        if want(kind):
            M = fn("SM_Prop_%s_0%s" % (kind, s), lod)
            if lod == 0:
                globals()["more_" + kind[5:]](M)
            out.append(M)
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
    size = tuple(int(v) for v in os.environ.get("FARM_SHOT", "560,380").split(","))
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


def write(M):
    """The street kit's write, its corners shared where they are the same (position, normal, colour and UV), so
    the detailed models stay small on disk and in memory."""
    import numpy as np
    pos, nrm, col, uv = K.triangles(M)
    key = np.concatenate([np.round(pos, 4), np.round(nrm, 3), col.astype(np.float64), np.round(uv, 4)], 1)
    uniq, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    tris = inv.reshape(-1).astype(np.int64).reshape(-1, 3)
    K.write_glb(os.path.join(K.OUT, M.name + ".glb"), pos[first], tris, nrm[first], col[first], M.name, uv[first])
    return len(tris), pos[:, 1].min(), pos[:, 1].max(), pos[:, 0].min(), pos[:, 0].max(), pos[:, 2].min(), pos[:, 2].max()


OFFSETS = {}
SOFT_SKIP = ("tyre", "rim", "lug", "tlug", "bolt", "window", "valve", "hose", "slat", "grilleback", "treadhole", "lamp", "glass",
             "track", "guide", "trackwheel", "trackhub", "beacon", "stripe", "doorline", "panel", "board", "tine", "disc")


def soften(models):
    """Close up nothing is a hard box: every plain block and prism gets its edges rounded (a bevel in three steps, a
    fifth of its thinnest side at most), so the panels read as pressed and folded steel."""
    for M in models:
        for p in M.parts:
            ob = p.obj
            if ob.modifiers or ob.name.split(".")[0] in SOFT_SKIP or len(ob.data.vertices) > 64:
                continue
            d = sorted(ob.dimensions)
            w = min(0.045, d[0] * 0.22, d[1] * 0.12)
            if w < 0.004:
                continue
            m = ob.modifiers.new("bevel", "BEVEL")
            m.width = w
            m.segments = 3
            m.limit_method = "ANGLE"
            m.angle_limit = math.radians(40)
            m.harden_normals = True


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
        LOFT_N[0] = 28 if lod == 0 else 12
        models = build(lod, only)
        if lod == 0:
            soften(models)
        centre(models)
        for M in models:
            nt, y0, y1, x0, x1, z0, z1 = write(M)
            out[M.name] = (nt, y0, y1)
            print("%-34s %6d triangles, %.2f..%.2f m up, %.1f x %.1f m" % (M.name, nt, y0, y1, x1 - x0, z1 - z0))
        if "--render" in args:
            render(models, args[args.index("--render") + 1], lod)
    FP.KINDS = KINDS
    FP.write_kinds(out)


if __name__ == "__main__":
    main()
