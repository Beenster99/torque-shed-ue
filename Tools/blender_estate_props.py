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
from blender_street_kit import _object, box, lathe as street_lathe  # noqa: E402

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

def trailer(body, colour):
    """A 13.6 m semi-trailer standing on its landing legs, rear at +x (the end backed to a dock): close by, the
    detailed trailer of Tools/blender_lorry_models.py (the services lorry park's, so both kits share one model);
    further off, its plain shapes just inside it."""
    import blender_lorry_models as LM
    U = K.Unit("trailer", "")
    L, W = LM.TRAILER_LEN, 2.55
    floor, top = LM.FLOOR, LM.TOP
    add_box(U, "body", -L / 2 + 0.03, L / 2 - 0.03, -W / 2 + 0.03, W / 2 - 0.03, floor + 0.03, top - 0.03, colour, 2, "trailer")
    add_box(U, "chassis", -L / 2 + 0.6, L / 2 - 0.4, -0.5, 0.5, 0.98, floor + 0.03, CHASSIS, 1)
    for x in (5.05, 3.74, 2.43):
        for y in (-1.06, 1.06):
            U.add(cyl("tyre", (x, y, LM.R_TYRE), LM.R_TYRE - 0.03, 0.3), "rubber", TYRE, 1)
    for y in (-0.78, 0.78):
        add_box(U, "leg", -4.4 - 0.05, -4.4 + 0.05, y - 0.05, y + 0.05, 0.08, floor - 0.12, CHASSIS, 1)
    if body == "reefer":
        add_box(U, "fridge", -L / 2 - 0.45, -L / 2, -0.92, 0.92, 2.48, 3.82, (0.7, 0.71, 0.7), 1)
    M = LM.trailer_model("trailer", body, colour, 0)
    for p in M.parts:
        U.add(p.obj, "rubber" if p.rgb == LM.TYRE else ("metal" if p.surface == "steel" else p.surface), p.rgb, 0)
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


# --- detail helpers (beenster Nov 26: "Being light doesn't need to cost detail") ---------------------------
#
# A prop is drawn two ways: further off as its simple shapes (parts tagged 1 or 2), close by as those plus its
# detail (parts tagged 0), which the game drops a short way off. So each simple shape sits just inside the
# detailed one that stands for it close by (a few cm in), and only the detail carries the small things.

INSET = 0.025


def turned(name, profile, at, axis="z", seg=16, sharp=35.0):
    """A solid turned about an axis through at: profile [(radius, along)] (along from at on the axis)."""
    ob = street_lathe(name, profile, seg=seg, sharp_deg=sharp)
    R = {"z": Matrix.Identity(4), "x": Matrix.Rotation(math.pi / 2, 4, "Y"), "y": Matrix.Rotation(-math.pi / 2, 4, "X")}[axis]
    ob.matrix_world = Matrix.Translation(Vector(at)) @ R
    return ob


def bbox(U, name, x0, x1, y0, y1, z0, z1, rgb, lod=0, surface="paint", bevel=0.01, rot=None):
    """A box with bevelled edges."""
    b = min(bevel, (x1 - x0) * 0.3, (y1 - y0) * 0.3, (z1 - z0) * 0.3)
    return U.add(box(name, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), (x1 - x0, y1 - y0, z1 - z0), bevel=b, rot=rot), surface, rgb, lod)


def obox(U, name, centre, size, rot, rgb, lod=0, surface="paint", bevel=0.0):
    """A box of size (x, y, z) about its centre, turned by the matrix rot."""
    return U.add(box(name, centre, size, bevel=bevel, rot=rot), surface, rgb, lod)


def tube(name, pts, r, n=8):
    """A round bar along a polyline, its rings carried round the bends without twisting."""
    pts = [Vector(p) for p in pts]
    rings, u = [], None
    for i, p in enumerate(pts):
        if i == 0:
            t = pts[1] - pts[0]
        elif i == len(pts) - 1:
            t = pts[-1] - pts[-2]
        else:
            t = (pts[i + 1] - p).normalized() + (p - pts[i - 1]).normalized()
        t.normalize()
        if u is None:
            ref = Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))
            u = t.cross(ref).normalized()
        else:
            u = (u - t * u.dot(t)).normalized()
        v = t.cross(u)
        rings.append([tuple(p + (u * math.cos(a) + v * math.sin(a)) * r) for a in [2 * math.pi * (k + 0.5) / n for k in range(n)]])
    return K.solid(name, rings)


def bar(U, name, pts, r, rgb, lod=0, surface="metal", n=8):
    return U.add(tube(name, pts, r, n), surface, rgb, lod)


def extrude(name, outline, axis, a0, a1):
    """A slab of a (possibly concave) outline [(u, v)] drawn across the other two axes, from a0 to a1 along axis."""
    put = {"x": lambda u, v, t: (t, u, v), "y": lambda u, v, t: (u, t, v), "z": lambda u, v, t: (u, v, t)}[axis]
    return K.solid(name, [[put(u, v, t) for u, v in outline] for t in (a0, a1)])


def rrect(hx, hy, r, z, seg=3, cx=0.0, cy=0.0):
    """A ring round a rectangle of half sizes (hx, hy) with corners of radius r, at height z."""
    out = []
    for qx, qy, a0 in ((1, 1, 0.0), (-1, 1, 90.0), (-1, -1, 180.0), (1, -1, 270.0)):
        ox, oy = cx + qx * (hx - r), cy + qy * (hy - r)
        for k in range(seg + 1):
            a = math.radians(a0 + 90.0 * k / seg)
            out.append((ox + r * math.cos(a), oy + r * math.sin(a), z))
    return out


def bolt(U, at, axis, r=0.012, rgb=GALV, lod=0, length=0.012):
    """A hex bolt head standing out along axis ('x', '-x', 'y', '-y', 'z')."""
    sign = -1.0 if axis.startswith("-") else 1.0
    ax = axis[-1]
    c = Vector(at)
    off = Vector({"x": (1, 0, 0), "y": (0, 1, 0), "z": (0, 0, 1)}[ax]) * sign * length * 0.5
    U.add(turned("bolt", [(r, -length / 2), (r, length / 2), (r * 0.6, length / 2 + r * 0.3)], c + off, ax, seg=6, sharp=20.0), "metal", rgb, lod)


def rock(name, at, size, seed):
    """A lump of broken brick or concrete: a jittered loft of three rings."""
    import random
    g = random.Random(seed)
    sx, sy, sz = size
    rings = []
    for z, s in ((0.0, 0.8), (0.5, 1.0), (1.0, 0.55)):
        ring = []
        for k in range(6):
            a = 2 * math.pi * k / 6 + g.uniform(-0.25, 0.25)
            q = s * g.uniform(0.75, 1.15)
            ring.append((at[0] + math.cos(a) * sx * 0.5 * q, at[1] + math.sin(a) * sy * 0.5 * q, at[2] + z * sz * g.uniform(0.85, 1.1)))
        rings.append(ring)
    return K.solid(name, rings)


# --- yard things -------------------------------------------------------------------------------------------

def skip(rgb):
    """A builders' skip: a folded steel body with sloping ends, its lip and stiffening ribs, lifting trunnions,
    chain plates, skids, hazard stripes on its ends, and builders' rubble in it."""
    U = K.Unit("skip", "")
    dark = tuple(c * 0.55 for c in rgb)
    zb, zt = 0.12, 1.2
    ring = lambda hx, hy, z: [(-hx, -hy, z), (hx, -hy, z), (hx, hy, z), (-hx, hy, z)]
    # (far off: the plain tapered body and a heap in it, just inside the detailed shell)
    U.add(K.solid("skip", [ring(1.3 - INSET, 0.75 - INSET, zb + 0.01), ring(1.85 - INSET, 0.9 - INSET, zt - 0.02)]), "paint", rgb, 2)
    U.add(K.solid("rubble", [ring(1.7, 0.8, 1.0), ring(0.9, 0.4, 1.38)]), "concrete", (0.25, 0.22, 0.19), 1)
    # (close by: the shell, its outer wall, the rolled lip, the inner wall down to the floor)
    hx = lambda z: 1.3 + (1.85 - 1.3) * (z - zb) / (zt - zb)
    hy = lambda z: 0.75 + (0.9 - 0.75) * (z - zb) / (zt - zb)
    rings = [rrect(hx(zb), hy(zb), 0.05, zb), rrect(hx(zt), hy(zt), 0.06, zt),
             rrect(hx(zt) + 0.05, hy(zt) + 0.05, 0.08, zt + 0.02), rrect(hx(zt) + 0.05, hy(zt) + 0.05, 0.08, zt + 0.1),
             rrect(hx(zt) - 0.03, hy(zt) - 0.03, 0.05, zt + 0.1), rrect(hx(zb + 0.1) - 0.03, hy(zb + 0.1) - 0.03, 0.04, zb + 0.1)]
    U.add(K.solid("shell", rings), "paint", rgb, 0)
    # (the ribs: up the long sides and the sloping ends)
    ang_y = math.atan2(0.15, zt - zb)
    for x in (-0.9, -0.3, 0.3, 0.9):
        for side in (-1, 1):
            zc = (zb + zt) / 2
            y = side * (hy(zc) + 0.025)
            obox(U, "rib", (x, y, zc), (0.1, 0.05, zt - zb - 0.05), Matrix.Rotation(-side * ang_y, 4, "X"), rgb, 0, bevel=0.01)
    ang_x = math.atan2(0.55, zt - zb)
    for y in (-0.45, 0.45):
        for side in (-1, 1):
            zc = (zb + zt) / 2
            obox(U, "endrib", (side * (hx(zc) + 0.025), y, zc), (0.05, 0.1, (zt - zb) / math.cos(ang_x) - 0.05), Matrix.Rotation(side * ang_x, 4, "Y"), rgb, 0, bevel=0.01)
        for side in (-1, 1):   # (hazard stripes on the ends)
            zc = 0.75
            for k in range(5):
                yy = -0.6 + k * 0.3
                ob = obox(U, "stripe", (side * (hx(zc) + 0.008), yy, zc), (0.01, 0.12, 0.35),
                          Matrix.Rotation(side * ang_x, 4, "Y") @ Matrix.Rotation(0.6, 4, "X"), K.YELLOW if k % 2 else CHASSIS, 0)
    # (lifting trunnions and their bosses, a chain plate at each corner)
    for x in (-1.45, 1.45):
        for side in (-1, 1):
            y = side * (hy(0.95) + 0.02)
            U.add(turned("boss", [(0.11, -0.01 * side), (0.11, 0.03 * side), (0.0001, 0.03 * side)], (x, y, 0.95), "y", seg=14), "paint", dark, 0)
            U.add(turned("trunnion", [(0.045, 0.0), (0.045, 0.14 * side), (0.07, 0.14 * side), (0.07, 0.17 * side), (0.0001, 0.17 * side)], (x, y, 0.95), "y", seg=12), "metal", CHASSIS, 0)
    for x in (-1.6, 1.6):
        for side in (-1, 1):
            bbox(U, "chainplate", x - 0.04, x + 0.04, side * (hy(1.12) + 0.01) - 0.012, side * (hy(1.12) + 0.01) + 0.012, 1.02, 1.2, dark, 0, bevel=0.005)
    # (skids under it, and their cross pieces)
    for x in (-0.75, 0.75):
        bbox(U, "skid", x - 0.07, x + 0.07, -0.8, 0.8, 0.0, 0.13, CHASSIS, 1, "metal", bevel=0.01)
    for y in (-0.55, 0.0, 0.55):
        bbox(U, "cross", -1.2, 1.2, y - 0.04, y + 0.04, 0.04, 0.12, CHASSIS, 0, "metal", bevel=0.008)
    # (the rubble: broken brick and concrete, a few boards, a torn bag, a length of pipe)
    import random
    g = random.Random(7)
    for k in range(34):
        x, y = g.uniform(-1.45, 1.45), g.uniform(-0.62, 0.62)
        z = 1.0 + 0.36 * (1.0 - max(abs(x) / 1.6, abs(y) / 0.75)) + g.uniform(-0.03, 0.05)
        s = g.uniform(0.12, 0.32)
        col = g.choice([(0.25, 0.22, 0.19), (0.32, 0.12, 0.07), (0.4, 0.39, 0.36), (0.2, 0.2, 0.2)])
        U.add(rock("rock", (x, y, z), (s, s * g.uniform(0.6, 1.0), s * 0.6), 100 + k), "concrete", col, 0)
    for k in range(4):
        x, y = g.uniform(-1.0, 1.0), g.uniform(-0.4, 0.4)
        obox(U, "board", (x, y, 1.32 + 0.03 * k), (1.6, 0.15, 0.025), Matrix.Rotation(g.uniform(-0.6, 0.6), 4, "Z") @ Matrix.Rotation(g.uniform(-0.15, 0.15), 4, "Y"), WOOD, 0, "wood")
    obox(U, "bag", (0.6, -0.25, 1.38), (0.55, 0.4, 0.22), Matrix.Rotation(0.4, 4, "Z"), (0.75, 0.75, 0.72), 0, "rubber", bevel=0.08)
    bar(U, "pipe", [(-1.3, 0.35, 1.32), (0.4, 0.55, 1.42)], 0.05, (0.45, 0.45, 0.47), 0, "metal", n=10)
    return U


def pallet(U, x, y, z, turn=0.0, wood=WOOD, lod=0):
    """One 1.2 x 1.0 m pallet: three bottom boards, nine blocks, three stringer boards, five top boards."""
    R = Matrix.Rotation(turn, 4, "Z")
    c = Vector((x, y, z))
    put = lambda lx, ly, lz: tuple(c + (R @ Vector((lx, ly, lz))))
    shade = lambda k: tuple(min(1.0, v * (0.85 + 0.05 * (k % 4))) for v in wood)
    k = 0
    for ly in (-0.43, 0.0, 0.43):   # (bottom boards along it)
        obox(U, "pb", put(0.0, ly, 0.011), (1.2, 0.145 if ly == 0.0 else 0.1, 0.022), R, shade(k), lod, "wood"); k += 1
    for lx in (-0.53, 0.0, 0.53):
        for ly in (-0.43, 0.0, 0.43):   # (the blocks)
            obox(U, "pk", put(lx, ly, 0.061), (0.145, 0.1 if ly else 0.145, 0.078), R, WOOD_DARK if (k % 3 == 0) else shade(k + 1), lod, "wood"); k += 1
    for lx in (-0.53, 0.0, 0.53):   # (stringer boards across)
        obox(U, "ps", put(lx, 0.0, 0.111), (0.145, 1.0, 0.022), R, shade(k), lod, "wood"); k += 1
    for ly in (-0.43, -0.22, 0.0, 0.22, 0.43):   # (top boards)
        obox(U, "pt", put(0.0, ly, 0.133), (1.2, 0.145 if ly in (-0.43, 0.0, 0.43) else 0.1, 0.022), R, shade(k), lod, "wood"); k += 1


def pallets():
    """Stacks of wooden pallets, a little out of true as a forklift leaves them, one leaning, one lying."""
    U = K.Unit("pallets", "")
    import random
    g = random.Random(3)
    h = 0.144
    for x, y, n in ((-1.3, 0.0, 9), (0.0, -0.1, 5), (1.3, 0.05, 12), (2.3, -0.6, 1)):
        # (far off: each stack as one block, a little inside its pallets, the colour of old pallet wood)
        add_box(U, "stack", x - 0.6 + 0.04, x + 0.6 - 0.04, y - 0.5 + 0.04, y + 0.5 - 0.04, 0.0, n * h - 0.01, (0.25, 0.16, 0.08), 1, "wood")
        for k in range(n):
            pallet(U, x + g.uniform(-0.02, 0.02), y + g.uniform(-0.02, 0.02), k * h, g.uniform(-0.025, 0.025))
    # (one leant against the tall stack)
    R = Matrix.Rotation(-1.35, 4, "X")
    U2 = K.Unit("lean", "")
    pallet(U2, 0.0, 0.0, 0.0)
    for p in U2.parts:
        p.obj.matrix_world = Matrix.Translation(Vector((1.3, -0.62, 0.0))) @ R @ p.obj.matrix_world
        U.parts.append(p)
    return U


def corrugated(name, x0, x1, z0, z1, y, pitch, depth, face=-1):
    """A corrugated steel sheet across x from x0 to x1, up from z0 to z1, its ridges standing out depth from
    y (towards face): trapezoidal ribs as on a shipping container."""
    n = max(1, int(round((x1 - x0) / pitch)))
    p = (x1 - x0) / n
    t = 0.003
    out, back = [], []
    for k in range(n):
        a = x0 + k * p
        for fx, d in ((0.0, 0.0), (0.18, 0.0), (0.32, depth), (0.68, depth), (0.82, 0.0)):
            out.append((a + fx * p, y + face * d))
    out.append((x1, y))
    back = [(u, v - face * t) for u, v in reversed(out)]
    return extrude(name, out + back, "z", z0, z1)


def container(rgb):
    """A 20 ft shipping container: corrugated sides, front and roof, corner posts and castings, top and bottom
    rails, forklift pockets, and its two rear doors with locking bars, cams, handles and hinges."""
    U = K.Unit("container", "")
    L, W, H = 6.06, 2.44, 2.59
    dark = tuple(c * 0.6 for c in rgb)
    rail = tuple(c * 0.8 for c in rgb)
    # (far off: the box, just inside its sheets)
    add_box(U, "box", -L / 2 + 0.07, L / 2 - 0.07, -W / 2 + 0.07, W / 2 - 0.07, 0.02, H - 0.07, rgb, 2)
    for y in (-W / 2 - 0.005, W / 2 - 0.025):
        add_box(U, "toprail", -L / 2, L / 2, y, y + 0.03, H - 0.15, H, dark, 1)
        add_box(U, "botrail", -L / 2, L / 2, y, y + 0.03, 0.0, 0.15, dark, 1)
    # (close by: the sheets)
    for side in (-1, 1):
        U.add(corrugated("side", -L / 2 + 0.16, L / 2 - 0.16, 0.16, H - 0.12, side * (W / 2 - 0.04), 0.278, 0.036, side), "paint", rgb, 0)
    front = corrugated("front", -W / 2 + 0.16, W / 2 - 0.16, 0.16, H - 0.12, 0.0, 0.278, 0.036, -1)
    front.matrix_world = Matrix.Translation(Vector((-L / 2 + 0.04, 0.0, 0.0))) @ Matrix.Rotation(-math.pi / 2, 4, "Z")
    U.add(front, "paint", rgb, 0)
    roof = corrugated("roof", -L / 2 + 0.1, L / 2 - 0.1, -W / 2 + 0.06, W / 2 - 0.06, 0.0, 0.5, 0.02, 1)
    roof.matrix_world = Matrix.Translation(Vector((0.0, 0.0, H - 0.04))) @ Matrix.Rotation(math.pi / 2, 4, "X")
    U.add(roof, "paint", rgb, 0)
    # (the frame: top and bottom side rails, end rails, corner posts, corner castings with their holes)
    for side in (-1, 1):
        ya, yb = sorted((side * W / 2, side * (W / 2 - 0.07)))
        bbox(U, "tsr", -L / 2 + 0.1, L / 2 - 0.1, ya, yb, H - 0.12, H - 0.02, rail, 0, bevel=0.008)
        bbox(U, "bsr", -L / 2 + 0.1, L / 2 - 0.1, ya, yb, 0.02, 0.17, rail, 0, bevel=0.008)
        for x in (-1.04, 1.04):   # (forklift pockets)
            bbox(U, "pocket", x - 0.2, x + 0.2, side * W / 2 - 0.006, side * W / 2 + 0.006, 0.04, 0.14, CHASSIS, 0)
    for x in (-L / 2, L / 2 - 0.16):
        for y in (-W / 2, W / 2 - 0.16):
            bbox(U, "post", x + 0.01, x + 0.15, y + 0.01, y + 0.15, 0.12, H - 0.12, rail, 0, bevel=0.012)
            for z in (0.0, H - 0.12):
                bbox(U, "casting", x, x + 0.16, y, y + 0.16, z, z + 0.12, (0.08, 0.08, 0.08), 0, bevel=0.006)
                cx = x + 0.08
                sy = -1 if y < 0 else 1
                bbox(U, "hole", cx - 0.04, cx + 0.04, y + (0.16 if sy > 0 else 0.0) - 0.004, y + (0.16 if sy > 0 else 0.0) + 0.004, z + 0.03, z + 0.09, (0.01, 0.01, 0.01), 0)
    for x, bot in ((-L / 2 + 0.02, True), (L / 2 - 0.12, True)):
        bbox(U, "endrail", x, x + 0.1, -W / 2 + 0.15, W / 2 - 0.15, 0.02, 0.22, rail, 0, bevel=0.008)
        bbox(U, "endtop", x, x + 0.1, -W / 2 + 0.15, W / 2 - 0.15, H - 0.13, H - 0.02, rail, 0, bevel=0.008)
    # (the doors at +x: two leaves of upright ribs, their seals, four locking bars with cams and handles, hinges)
    xd = L / 2 - 0.02
    for y0, y1 in ((-W / 2 + 0.16, -0.01), (0.01, W / 2 - 0.16)):
        bbox(U, "leaf", xd - 0.03, xd, y0, y1, 0.22, H - 0.13, rgb, 0, bevel=0.01)
        for k in range(4):
            yy = y0 + (y1 - y0) * (k + 0.5) / 4
            bbox(U, "doorrib", xd, xd + 0.025, yy - 0.08, yy + 0.08, 0.32, H - 0.24, rgb, 0, bevel=0.008)
        bbox(U, "seal", xd - 0.005, xd + 0.004, y0 - 0.01, y0 + 0.01, 0.22, H - 0.13, CHASSIS, 0)
        for yb in (y0 + (y1 - y0) * 0.22, y0 + (y1 - y0) * 0.72):
            bar(U, "lockbar", [(xd + 0.06, yb, 0.12), (xd + 0.06, yb, H - 0.06)], 0.018, GALV, 0, n=8)
            for z in (0.2, 0.75, 1.75, H - 0.2):
                bbox(U, "guide", xd + 0.02, xd + 0.09, yb - 0.035, yb + 0.035, z - 0.03, z + 0.03, GALV, 0, "metal", bevel=0.005)
            for z in (0.14, H - 0.08):
                bbox(U, "cam", xd + 0.03, xd + 0.1, yb - 0.04, yb + 0.04, z - 0.03, z + 0.03, GALV, 0, "metal", bevel=0.006)
            bar(U, "handle", [(xd + 0.06, yb, 1.15), (xd + 0.1, yb, 1.15), (xd + 0.1, yb + 0.32, 1.15)], 0.014, GALV, 0, n=8)
            bbox(U, "keeper", xd + 0.03, xd + 0.09, yb + 0.28, yb + 0.36, 1.1, 1.2, GALV, 0, "metal", bevel=0.005)
        hy = y0 + 0.02 if y0 < 0 else y1 - 0.02
        for z in (0.45, 1.05, 1.65, H - 0.4):
            U.add(turned("hinge", [(0.022, -0.06), (0.022, 0.06)], (xd + 0.03, hy, z), "z", seg=8), "metal", GALV, 0)
    bbox(U, "plate", xd + 0.025, xd + 0.032, 0.25, 0.55, 1.6, 1.8, (0.55, 0.56, 0.56), 0, "metal")
    return U


def gas_cage():
    """A galvanised cage of gas bottles: angle posts and base plates, welded mesh on its doors and ends, a sloping
    lid, a padlocked hasp, bottles with their shoulders, valves and guards, a chain across, and warning plates."""
    U = K.Unit("gascage", "")
    L, D, H = 2.4, 1.0, 2.0
    # (far off: posts, lid, back sheet, rails and bottles, as before)
    for x in (-L / 2, L / 2 - 0.05):
        for y in (-D / 2, D / 2 - 0.05):
            add_box(U, "post", x + 0.005, x + 0.045, y + 0.005, y + 0.045, 0.0, H, GALV, 2)
    U.add(prism("lid", [(-L / 2 - 0.05, H), (L / 2 + 0.05, H), (L / 2 + 0.05, H + 0.05), (-L / 2 - 0.05, H + 0.05)],
                -D / 2 - 0.1, D / 2 + 0.1), "metal", GALV, 2)
    add_box(U, "back", -L / 2, L / 2, D / 2 - 0.03, D / 2, 0.1, H - 0.05, (0.2, 0.21, 0.21), 1)
    for z in (0.1, H - 0.05):
        for y in (-D / 2, D / 2 - 0.04):
            add_box(U, "rail", -L / 2, L / 2, y, y + 0.04, z - 0.03, z + 0.03, GALV, 1)
    # (close by: angle-iron posts, base plates and bolts)
    for x, sx in ((-L / 2, 1), (L / 2, -1)):
        for y, sy in ((-D / 2, 1), (D / 2, -1)):
            U.add(extrude("angle", [(x, y), (x + sx * 0.05, y), (x + sx * 0.05, y + sy * 0.005), (x + sx * 0.005, y + sy * 0.005),
                                     (x + sx * 0.005, y + sy * 0.05), (x, y + sy * 0.05)], "z", 0.0, H), "metal", GALV, 0)
            bbox(U, "foot", x + sx * 0.08 - 0.06, x + sx * 0.08 + 0.06, y + sy * 0.08 - 0.06, y + sy * 0.08 + 0.06, 0.0, 0.008, GALV, 0, "metal")
            for k in (-1, 1):
                bolt(U, (x + sx * 0.08 + k * 0.035, y + sy * 0.08, 0.008), "z", 0.01, GALV)
    # (the welded mesh: on the two front doors and both ends, 100 mm squares)
    def mesh(u0, u1, z0, z1, plane, at, step=0.1):
        n = max(1, int(round((u1 - u0) / step)))
        for k in range(n + 1):
            u = u0 + (u1 - u0) * k / n
            if plane == "front":
                bbox(U, "mv", u - 0.003, u + 0.003, at - 0.003, at + 0.003, z0, z1, GALV, 0, "metal", bevel=0.0)
            else:
                bbox(U, "mv", at - 0.003, at + 0.003, u - 0.003, u + 0.003, z0, z1, GALV, 0, "metal", bevel=0.0)
        m = max(1, int(round((z1 - z0) / step)))
        for k in range(m + 1):
            z = z0 + (z1 - z0) * k / m
            if plane == "front":
                bbox(U, "mh", u0, u1, at - 0.0045, at + 0.0045, z - 0.003, z + 0.003, GALV, 0, "metal", bevel=0.0)
            else:
                bbox(U, "mh", at - 0.0045, at + 0.0045, u0, u1, z - 0.003, z + 0.003, GALV, 0, "metal", bevel=0.0)
    for x0, x1 in ((-L / 2 + 0.06, -0.02), (0.02, L / 2 - 0.06)):   # (the doors, framed)
        mesh(x0 + 0.03, x1 - 0.03, 0.15, H - 0.1, "front", -D / 2 - 0.01)
        for xx in (x0, x1 - 0.03):
            bbox(U, "dframe", xx, xx + 0.03, -D / 2 - 0.025, -D / 2 + 0.005, 0.12, H - 0.07, GALV, 0, "metal", bevel=0.004)
        for z in (0.12, H - 0.1):
            bbox(U, "dframe", x0, x1, -D / 2 - 0.025, -D / 2 + 0.005, z, z + 0.03, GALV, 0, "metal", bevel=0.004)
        hx = x0 + 0.015 if x0 < 0 else x1 - 0.015
        for z in (0.4, H - 0.4):
            U.add(turned("hinge", [(0.014, -0.05), (0.014, 0.05)], (hx + (-0.03 if x0 < 0 else 0.03), -D / 2 - 0.02, z), "z", seg=8), "metal", GALV, 0)
    for x in (-L / 2 - 0.006, L / 2 + 0.006):
        mesh(-D / 2 + 0.06, D / 2 - 0.06, 0.15, H - 0.1, "end", x)
    # (the hasp and its padlock)
    bbox(U, "hasp", -0.06, 0.06, -D / 2 - 0.04, -D / 2 - 0.025, 1.0, 1.08, GALV, 0, "metal", bevel=0.004)
    bbox(U, "padlock", -0.03, 0.03, -D / 2 - 0.065, -D / 2 - 0.04, 0.9, 0.97, (0.55, 0.42, 0.12), 0, "metal", bevel=0.006)
    U.add(turned("shackle", [(0.006, 0.0), (0.006, 0.05)], (-0.018, -D / 2 - 0.052, 0.97), "z", seg=6), "metal", CHROME, 0)
    U.add(turned("shackle", [(0.006, 0.0), (0.006, 0.05)], (0.018, -D / 2 - 0.052, 0.97), "z", seg=6), "metal", CHROME, 0)
    bar(U, "shtop", [(-0.018, -D / 2 - 0.052, 1.02), (-0.012, -D / 2 - 0.052, 1.04), (0.012, -D / 2 - 0.052, 1.04), (0.018, -D / 2 - 0.052, 1.02)], 0.006, CHROME, 0, n=6)
    # (the bottles: two rows, tall and short; foot ring, body, shoulder, neck, valve and its guard)
    kinds = [((0.45, 0.12, 0.05), 0.15, 1.3), ((0.05, 0.2, 0.08), 0.15, 1.3), ((0.12, 0.12, 0.13), 0.115, 1.45), ((0.5, 0.36, 0.05), 0.15, 1.0)]
    k = 0
    for y in (0.22, -0.12):
        for x in (-0.95, -0.6, -0.25, 0.1, 0.45, 0.8):
            rgb, r, h = kinds[(k * 7 + int(y * 10)) % 4]
            k += 1
            U.add(cyl("bottle", (x, y, 0.06 + (h - 0.12) / 2), r - 0.015, h - 0.12, "z", 8), "paint", rgb, 1)
            prof = [(r * 0.85, 0.0), (r * 0.9, 0.06), (r, 0.08), (r, h - r * 0.9), (r * 0.8, h - r * 0.45), (r * 0.45, h - r * 0.1),
                    (0.025, h - 0.02), (0.025, h + 0.04), (0.0001, h + 0.04)]
            U.add(turned("bottle", prof, (x, y, 0.0), "z", seg=18), "paint", rgb, 0)
            U.add(turned("valve", [(0.02, h + 0.04), (0.03, h + 0.06), (0.03, h + 0.1), (0.012, h + 0.12), (0.0001, h + 0.12)], (x, y, 0.0), "z", seg=8), "metal", (0.55, 0.42, 0.12), 0)
            U.add(turned("guard", [(0.06, h - 0.02), (0.065, h + 0.12), (0.055, h + 0.12), (0.05, h - 0.02)], (x, y, 0.0), "z", seg=12), "metal", rgb, 0)
    bar(U, "chain", [(-L / 2 + 0.03, 0.05, 1.0), (-0.3, -0.27, 1.02), (0.3, -0.27, 1.02), (L / 2 - 0.03, 0.05, 1.0)], 0.008, GALV, 0, n=6)
    # (warning plates: a yellow triangle with a black edge on each door)
    for cx in (-0.6, 0.6):
        U.add(extrude("warnb", [(cx - 0.17, 1.3), (cx + 0.17, 1.3), (cx, 1.6)], "y", -D / 2 - 0.035, -D / 2 - 0.03), "sign", CHASSIS, 0)
        U.add(extrude("warn", [(cx - 0.14, 1.32), (cx + 0.14, 1.32), (cx, 1.565)], "y", -D / 2 - 0.04, -D / 2 - 0.035), "sign", K.YELLOW, 0)
        U.add(extrude("flame", [(cx - 0.04, 1.36), (cx + 0.04, 1.36), (cx + 0.03, 1.42), (cx + 0.005, 1.47), (cx - 0.035, 1.42)], "y", -D / 2 - 0.044, -D / 2 - 0.04), "sign", CHASSIS, 0)
    return U


def w_pale(U, name, x, z0, top, rgb, lod=0, y=0.0):
    """A W-section palisade pale at x, from z0 to top, its triple-pointed head; front at -y."""
    w, d = 0.068, 0.03
    t = 0.004
    xs = [x - w / 2, x - w / 4, x, x + w / 4, x + w / 2]
    ys = [y - d, y, y - d, y, y - d]
    front = list(zip(xs, ys))
    back = [(u, v + t) for u, v in reversed(front)]
    U.add(extrude(name, front + back, "z", z0, top - 0.1), "pale", rgb, lod)
    for k, (u, v) in enumerate(((xs[0], ys[0]), (xs[2], ys[2]), (xs[4], ys[4]))):
        half = 0.016
        tip = 0.1 if k == 1 else 0.075
        U.add(extrude(name + "tip", [(u - half, top - 0.1), (u + half, top - 0.1), (u, top - 0.1 + tip)], "y", v, v + 0.035), "pale", rgb, lod)


def palisade(rgb):
    """A 2.75 m run of steel palisade fencing: I-section posts with caps, angle rails, W-section pales with
    triple-pointed heads, each bolted to both rails."""
    U = K.Unit("palisade", "")
    L, H = 2.75, 2.4
    # (far off: posts, rails and flat pales, behind the pales' backs close by)
    for x in (-L / 2, L / 2 - 0.1):
        add_box(U, "post", x + 0.01, x + 0.09, 0.03, 0.11, -0.3, H - 0.06, rgb, 2)
    for z in (0.35, H - 0.45):
        add_box(U, "rail", -L / 2, L / 2, 0.012, 0.05, z + 0.005, z + 0.045, rgb, 2)
    n = 11
    for k in range(n):
        x = -L / 2 + L * (k + 0.5) / n
        w = 0.07
        U.add(prism("pale", [(x - w / 2, -0.05), (x + w / 2, -0.05), (x + w / 2, H - 0.12), (x, H), (x - w / 2, H - 0.12)],
                    0.004, 0.009), "pale", rgb, 1)
    # (close by)
    for x in (-L / 2 + 0.05, L / 2 - 0.05):
        U.add(extrude("ipost", [(x - 0.05, 0.02), (x + 0.05, 0.02), (x + 0.05, 0.03), (x + 0.004, 0.03), (x + 0.004, 0.11), (x + 0.05, 0.11),
                                 (x + 0.05, 0.12), (x - 0.05, 0.12), (x - 0.05, 0.11), (x - 0.004, 0.11), (x - 0.004, 0.03), (x - 0.05, 0.03)], "z", -0.3, H - 0.05), "metal", rgb, 0)
        bbox(U, "cap", x - 0.055, x + 0.055, 0.015, 0.125, H - 0.05, H - 0.03, rgb, 0, bevel=0.005)
    for z in (0.35, H - 0.45):
        U.add(extrude("arail", [(0.01, z), (0.016, z), (0.016, z + 0.044), (0.06, z + 0.044), (0.06, z + 0.05), (0.01, z + 0.05)], "x", -L / 2, L / 2), "metal", rgb, 0)
    for k in range(n):
        x = -L / 2 + L * (k + 0.5) / n
        w_pale(U, "wpale", x, -0.05, H, rgb, 0, y=0.004)
        for z in (0.375, H - 0.425):
            bolt(U, (x, -0.026, z), "-y", 0.009, GALV)
    return U


def gate(rgb):
    """A 6 m sliding palisade gate: a box-section frame with its brace, W pales, wheels on a track, a guide post
    with its rollers and a catch post with its lock box."""
    U = K.Unit("gate", "")
    L, H = 6.0, 2.4
    add_box(U, "frameb", -L / 2, L / 2, 0.005, 0.075, 0.155, 0.295, rgb, 2)
    add_box(U, "framet", -L / 2, L / 2, 0.005, 0.075, H - 0.495, H - 0.405, rgb, 2)
    add_box(U, "diag", -0.055, 0.055, 0.005, 0.075, 0.3, H - 0.5, rgb, 2)
    for x in (-L / 2 - 0.15, L / 2):
        add_box(U, "gpost", x + 0.005, x + 0.145, -0.045, 0.145, -0.3, H + 0.095, rgb, 2)
    n = 22
    for k in range(n):
        x = -L / 2 + L * (k + 0.5) / n
        w = 0.07
        U.add(prism("pale", [(x - w / 2, 0.15), (x + w / 2, 0.15), (x + w / 2, H - 0.12), (x, H), (x - w / 2, H - 0.12)],
                    0.004, 0.009), "pale", rgb, 1)
    # (close by: the frame bevelled, the brace, end stiles)
    bbox(U, "frameb", -L / 2, L / 2, 0.0, 0.08, 0.15, 0.3, rgb, 0, bevel=0.008)
    bbox(U, "framet", -L / 2, L / 2, 0.0, 0.08, H - 0.5, H - 0.4, rgb, 0, bevel=0.008)
    ang = math.atan2(H - 0.8, L - 0.2)
    obox(U, "brace", (0.0, 0.04, (0.3 + H - 0.5) / 2), (math.hypot(L - 0.2, H - 0.8), 0.06, 0.06), Matrix.Rotation(-ang, 4, "Y"), rgb, 0, bevel=0.006)
    for x in (-L / 2, L / 2 - 0.08):
        bbox(U, "stile", x, x + 0.08, 0.0, 0.08, 0.15, H - 0.4, rgb, 0, bevel=0.008)
    for k in range(n):
        x = -L / 2 + L * (k + 0.5) / n
        w_pale(U, "wpale", x, 0.15, H, rgb, 0, y=0.004)
        for z in (0.225, H - 0.45):
            bolt(U, (x, -0.026, z), "-y", 0.009, GALV)
    # (its wheels running on the track, the track, the posts' caps, guide rollers and the lock box)
    for x in (-L / 2 + 0.6, L / 2 - 0.6):
        U.add(turned("wheel", [(0.075, -0.02), (0.08, -0.012), (0.08, 0.012), (0.075, 0.02)], (x, 0.04, 0.08), "y", seg=16), "metal", GALV, 0)
        bbox(U, "fork", x - 0.1, x + 0.1, -0.005, 0.085, 0.13, 0.16, GALV, 0, "metal", bevel=0.004)
    U.add(extrude("track", [(0.01, 0.0), (0.07, 0.0), (0.04, 0.03)], "x", -L / 2 - 1.0, L / 2), "metal", GALV, 0)
    bbox(U, "trackbase", -L / 2 - 1.0, L / 2, -0.03, 0.11, -0.02, 0.0, GALV, 0, "metal")
    for x in (-L / 2 - 0.15, L / 2):
        bbox(U, "pcap", x - 0.01, x + 0.16, -0.055, 0.155, H + 0.1, H + 0.12, rgb, 0, bevel=0.005)
        bbox(U, "gpost", x, x + 0.15, -0.05, 0.15, -0.3, H + 0.1, rgb, 0, bevel=0.01)
    for y in (-0.03, 0.11):
        U.add(turned("roller", [(0.03, -0.04), (0.035, -0.03), (0.035, 0.03), (0.03, 0.04)], (-L / 2 - 0.08, y, H - 0.45), "z", seg=12), "rubber", TYRE, 0)
    bbox(U, "lockbox", L / 2 - 0.02, L / 2 + 0.01, -0.06, 0.14, 1.0, 1.25, rgb, 0, bevel=0.01)
    bbox(U, "lock", L / 2 - 0.12, L / 2 - 0.02, -0.03, 0.11, 1.05, 1.2, GALV, 0, "metal", bevel=0.01)
    return U


def estate_board():
    """The estate's name board at its entrance: two capped posts on footings, a framed navy panel, its name
    header and its units' name strips, fixing bolts."""
    U = K.Unit("board", "")
    L = 3.2
    for x in (-L / 2 + 0.2, L / 2 - 0.35):
        add_box(U, "bpost", x + 0.01, x + 0.14, 0.01, 0.14, -0.3, 3.19, (0.6, 0.61, 0.61), 2)
        bbox(U, "bpost", x, x + 0.15, 0.0, 0.15, -0.3, 3.2, (0.6, 0.61, 0.61), 0, "metal", bevel=0.012)
        bbox(U, "bcap", x - 0.01, x + 0.16, -0.01, 0.16, 3.2, 3.24, (0.12, 0.13, 0.14), 0, "metal", bevel=0.01)
        bbox(U, "footing", x - 0.1, x + 0.25, -0.1, 0.25, -0.1, 0.06, (0.45, 0.44, 0.41), 0, "concrete", bevel=0.02)
    add_box(U, "panel", -L / 2, L / 2, -0.06, 0.0, 1.0, 3.0, NAVY, 2)
    for x0, x1, z0, z1 in ((-L / 2 - 0.03, L / 2 + 0.03, 2.98, 3.03), (-L / 2 - 0.03, L / 2 + 0.03, 0.97, 1.02),
                           (-L / 2 - 0.03, -L / 2 + 0.02, 0.97, 3.03), (L / 2 - 0.02, L / 2 + 0.03, 0.97, 3.03)):
        bbox(U, "bframe", x0, x1, -0.075, 0.005, z0, z1, (0.55, 0.56, 0.57), 0, "metal", bevel=0.01)
    add_box(U, "head", -L / 2 + 0.05, L / 2 - 0.05, -0.07, -0.06, 2.72, 2.96, K.SIGN_WHITE, 1)
    for k in range(5):   # (its units' names, on raised strips)
        z = 1.15 + k * 0.29
        bbox(U, "unitstrip", -L / 2 + 0.12, L / 2 - 0.12, -0.075, -0.06, z, z + 0.22, (0.03, 0.05, 0.12), 0, bevel=0.004)
        add_box(U, "unitline", -L / 2 + 0.25, -L / 2 + 0.25 + (2.2 if k % 2 else 1.7), -0.08, -0.075, z + 0.065, z + 0.155, K.SIGN_WHITE, 0)
        add_box(U, "unitno", L / 2 - 0.45, L / 2 - 0.2, -0.08, -0.075, z + 0.065, z + 0.155, K.YELLOW, 0)
    for x in (-L / 2 + 0.27, L / 2 - 0.27):
        for z in (1.08, 2.9):
            bolt(U, (x, -0.075, z), "-y", 0.012, CHROME)
    K.SIGNS.append((Vector((0.0, -0.07, 2.84)), Vector((0.0, -1.0, 0.0)), L - 0.4, 0.22, "estate", U))
    return U


def trolley(U, x, y, lod=1):
    """One shopping trolley, its nose at -x: wire basket, handle with its grip, child seat flap, the chassis and
    four castors."""
    lo, hi = 0.42, 0.95   # (the basket's bottom and rim)
    def rim(z, s, fl=0.0):
        return [(x - 0.42 * s + fl, y - 0.25 * s, z), (x + 0.42, y - 0.27, z), (x + 0.42, y + 0.27, z), (x - 0.42 * s + fl, y + 0.25 * s, z), (x - 0.42 * s + fl, y - 0.25 * s, z)]
    bar(U, "trim", rim(hi, 1.0), 0.008, CHROME, lod, n=6)
    bar(U, "trim", rim(lo, 0.8, 0.12), 0.006, CHROME, lod, n=6)
    for k in range(9):   # (the side wires)
        f = k / 8
        for side in (-1, 1):
            xa = x - 0.42 + 0.84 * f
            xb = x - 0.42 * 0.8 + 0.12 + (0.42 + 0.42 * 0.8 - 0.12) * f
            bar(U, "wire", [(xa, y + side * (0.25 + 0.02 * f), hi), (xb, y + side * (0.2 + 0.07 * f), lo)], 0.003, CHROME, lod, n=4)
    for k in range(5):
        f = (k + 0.5) / 5
        bar(U, "wire", [(x - 0.42 + 0.0, y - 0.25 + 0.5 * f, hi), (x - 0.42 * 0.8 + 0.12, y - 0.2 + 0.4 * f, lo)], 0.003, CHROME, lod, n=4)
    bar(U, "handle", [(x + 0.42, y - 0.27, hi), (x + 0.55, y - 0.27, 1.02), (x + 0.55, y + 0.27, 1.02), (x + 0.42, y + 0.27, hi)], 0.012, CHROME, lod, n=6)
    bar(U, "grip", [(x + 0.55, y - 0.24, 1.02), (x + 0.55, y + 0.24, 1.02)], 0.02, (0.35, 0.05, 0.03), lod, "rubber", n=8)
    bbox(U, "seat", x + 0.32, x + 0.4, y - 0.22, y + 0.22, hi - 0.25, hi - 0.02, (0.35, 0.05, 0.03), lod, "rubber", bevel=0.01)
    for side in (-1, 1):   # (the chassis)
        bar(U, "leg", [(x + 0.42, y + side * 0.25, hi), (x + 0.36, y + side * 0.22, 0.12)], 0.01, CHROME, lod, n=6)
        bar(U, "base", [(x - 0.4, y + side * 0.16, 0.12), (x + 0.36, y + side * 0.22, 0.12)], 0.01, CHROME, lod, n=6)
    bbox(U, "tray", x - 0.3, x + 0.3, y - 0.18, y + 0.18, 0.17, 0.19, CHROME, lod, "metal")
    for xx, yy in ((x - 0.38, y - 0.16), (x - 0.38, y + 0.16), (x + 0.34, y - 0.22), (x + 0.34, y + 0.22)):
        U.add(turned("castor", [(0.05, -0.012), (0.055, -0.006), (0.055, 0.006), (0.05, 0.012)], (xx, yy, 0.055), "y", seg=10), "rubber", (0.08, 0.08, 0.09), 0)
        bbox(U, "fork", xx - 0.02, xx + 0.02, yy - 0.018, yy + 0.018, 0.06, 0.12, CHROME, 0, "metal")


def trolley_shelter():
    """A supermarket car park's trolley shelter: round posts on base plates, a curved roof with its gutter,
    framed glazing at the back and ends, and two lines of nested trolleys."""
    U = K.Unit("trolleys", "")
    L, D, H = 5.0, 2.2, 2.3
    frame = (0.12, 0.13, 0.14)
    for x in (-L / 2, L / 2 - 0.08):
        for y in (-D / 2, D / 2 - 0.08):
            add_box(U, "tpost", x + 0.015, x + 0.065, y + 0.015, y + 0.065, 0.0, H + (0.15 if y > 0 else 0.0) - 0.02, frame, 2)
            hp = H + (0.15 if y > 0 else 0.0)
            U.add(turned("tpost", [(0.04, 0.0), (0.04, hp)], (x + 0.04, y + 0.04, 0.0), "z", seg=12), "metal", frame, 0)
            bbox(U, "tplate", x - 0.04, x + 0.12, y - 0.04, y + 0.12, 0.0, 0.012, frame, 0, "metal")
            for kx in (-1, 1):
                for ky in (-1, 1):
                    bolt(U, (x + 0.04 + kx * 0.06, y + 0.04 + ky * 0.06, 0.012), "z", 0.008, GALV)
    U.add(_roof(L, D, H), "metal", frame, 2)
    # (the curved roof sheet over the flat one, its gutter at the front)
    top = [(-D / 2 - 0.2 + (D + 0.3) * k / 10, H + 0.07 + 0.15 * k / 10 + 0.09 * math.sin(math.pi * k / 10)) for k in range(11)]
    bot = [(y, z - 0.012) for y, z in top]
    U.add(K.strip("croof", top, bot, -L / 2 - 0.12, L / 2 + 0.12), "metal", (0.5, 0.52, 0.53), 0)
    U.add(turned("gutter", [(0.05, 0.0), (0.05, L + 0.24)], (-L / 2 - 0.12, -D / 2 - 0.2, H + 0.04), "x", seg=10), "metal", frame, 0)
    for x in (-L / 2 + 0.04, 0.0, L / 2 - 0.04):
        bar(U, "rafter", [(x, -D / 2 - 0.15, H + 0.04), (x, D / 2 + 0.05, H + 0.2)], 0.025, frame, 0, n=8)
    add_box(U, "tback", -L / 2, L / 2, D / 2 - 0.05, D / 2 - 0.03, 0.3, H, (0.05, 0.07, 0.08), 1, "glass")
    for x in (-L / 2 + 0.02, L / 2 - 0.04):
        add_box(U, "tend", x, x + 0.02, -D / 2 + 0.3, D / 2 - 0.05, 0.3, H, (0.05, 0.07, 0.08), 1, "glass")
    for k in range(5):   # (the glazing's frames and clips)
        x = -L / 2 + L * k / 4
        bbox(U, "gframe", x - 0.025, x + 0.025, D / 2 - 0.065, D / 2 - 0.015, 0.28, H, frame, 0, "metal", bevel=0.005)
    for z in (0.28, H - 0.03):
        bbox(U, "gframe", -L / 2, L / 2, D / 2 - 0.065, D / 2 - 0.015, z, z + 0.04, frame, 0, "metal", bevel=0.005)
    for x in (-L / 2 + 0.03, L / 2 - 0.03):
        for z in (0.28, H - 0.03):
            bbox(U, "gframe", x - 0.025, x + 0.025, -D / 2 + 0.28, D / 2 - 0.04, z, z + 0.04, frame, 0, "metal", bevel=0.005)
    bar(U, "guide", [(-L / 2 + 0.3, 0.0, 0.75), (L / 2 - 0.3, 0.0, 0.75)], 0.03, CHROME, 0, n=10)
    for y in (-0.45, 0.45):   # (two lines of nested trolleys, 0.3 m apart)
        # (close by only: a block standing in for them further off showed through the wires)
        for k in range(7):
            trolley(U, -L / 2 + 0.75 + 0.3 * k, y, 0)
    return U


def _roof(L, D, H):
    ring = lambda z0: [(-L / 2 - 0.1, -D / 2 - 0.2, z0), (L / 2 + 0.1, -D / 2 - 0.2, z0), (L / 2 + 0.1, D / 2 + 0.1, z0 + 0.15), (-L / 2 - 0.1, D / 2 + 0.1, z0 + 0.15)]
    return K.solid("troof", [ring(H), ring(H + 0.06)])


def yard_lamp():
    """An 8 m yard lighting column: base flange and bolts, an octagonal tapered shaft, its door, a bracket arm,
    and a flat LED floodlight head with cooling fins."""
    U = K.Unit("lamp", "")
    U.add(K.solid("column", [[(0.1 * math.cos(a), 0.1 * math.sin(a), -0.3) for a in [k * math.pi / 3 for k in range(6)]],
                             [(0.055 * math.cos(a), 0.055 * math.sin(a), 8.0) for a in [k * math.pi / 3 for k in range(6)]]]), "metal", GALV, 2)
    add_box(U, "head", -0.28, 0.28, -1.48, -0.82, 7.77, 7.93, (0.12, 0.13, 0.14), 1)
    add_box(U, "arm", -0.04, 0.04, -0.9, 0.0, 7.86, 7.94, GALV, 1)
    # (close by)
    oct_ = lambda r, z: [(r * math.cos(a), r * math.sin(a), z) for a in [(k + 0.5) * math.pi / 4 for k in range(8)]]
    U.add(K.solid("shaft", [oct_(0.115, -0.3), oct_(0.115, 0.4), oct_(0.065, 8.0)]), "metal", GALV, 0)
    bbox(U, "flange", -0.2, 0.2, -0.2, 0.2, 0.0, 0.025, GALV, 0, "metal", bevel=0.006)
    for kx in (-1, 1):
        for ky in (-1, 1):
            U.add(turned("stud", [(0.012, 0.0), (0.012, 0.07)], (kx * 0.15, ky * 0.15, 0.025), "z", seg=6), "metal", GALV, 0)
            U.add(turned("nut", [(0.02, 0.025), (0.02, 0.045)], (kx * 0.15, ky * 0.15, 0.0), "z", seg=6, sharp=20.0), "metal", GALV, 0)
    bbox(U, "door", -0.045, 0.045, -0.122, -0.1, 0.6, 1.05, GALV, 0, "metal", bevel=0.006)
    bolt(U, (0.0, -0.122, 0.98), "-y", 0.01, GALV)
    bar(U, "arm", [(0.0, -0.03, 7.6), (0.0, -0.35, 7.9), (0.0, -0.95, 7.95)], 0.035, GALV, 0, n=10)
    bbox(U, "yoke", -0.3, 0.3, -1.0, -0.94, 7.8, 7.98, GALV, 0, "metal", bevel=0.008)
    bbox(U, "head", -0.3, 0.3, -1.5, -0.8, 7.85, 7.95, (0.12, 0.13, 0.14), 0, "metal", bevel=0.015)
    for k in range(9):   # (cooling fins on top)
        x = -0.26 + 0.065 * k
        bbox(U, "fin", x - 0.006, x + 0.006, -1.46, -0.84, 7.95, 7.99, (0.12, 0.13, 0.14), 0, "metal")
    bbox(U, "lensframe", -0.29, 0.29, -1.49, -0.81, 7.75, 7.78, (0.1, 0.1, 0.11), 0, "metal", bevel=0.01)
    for i in range(4):
        for j in range(3):
            bbox(U, "led", -0.22 + 0.15 * i - 0.05, -0.22 + 0.15 * i + 0.05, -1.4 + 0.2 * j - 0.07, -1.4 + 0.2 * j + 0.07, 7.735, 7.75, K.LAMP, 0, "lamp", bevel=0.004)
    bar(U, "cable", [(0.0, -0.12, 7.88), (0.0, -0.4, 7.82), (0.0, -0.9, 7.86)], 0.008, CHASSIS, 0, "rubber", n=6)
    return U


def bin_1100(U, x, rgb):
    """A 1100 litre four-wheeled bin: tapered ribbed body, rolled rim, domed split lid with hinges, handles and
    castors."""
    zb, zt = 0.2, 1.2
    # (far off: the body and lid, inside the detailed ones)
    add_box(U, "bin", x - 0.6, x + 0.6, -0.48, 0.48, 0.17, 1.22, rgb, 2)
    U.add(prism("lid", [(x - 0.64, 1.23), (x + 0.64, 1.23), (x + 0.64, 1.38), (x - 0.64, 1.28)], -0.53, 0.53), "paint", rgb, 1)
    # (close by)
    U.add(K.solid("body", [rrect(0.56, 0.44, 0.06, zb, cx=x), rrect(0.63, 0.51, 0.08, zt, cx=x), rrect(0.66, 0.54, 0.09, zt + 0.02, cx=x),
                           rrect(0.66, 0.54, 0.09, zt + 0.06, cx=x), rrect(0.6, 0.48, 0.07, zt + 0.06, cx=x), rrect(0.55, 0.43, 0.05, zb + 0.05, cx=x)]), "paint", rgb, 0)
    for k in range(6):   # (ribs down the front and back)
        xx = x - 0.45 + 0.18 * k
        for side in (-1, 1):
            ang = math.atan2(0.07, zt - zb)
            obox(U, "rib", (xx, side * 0.495, (zb + zt) / 2), (0.04, 0.03, zt - zb - 0.1), Matrix.Rotation(-side * ang, 4, "X"), rgb, 0, bevel=0.008)
    for k in range(2):   # (the two lid halves, domed, a lip at the front)
        x0 = x - 0.65 + 0.65 * k
        top = [(-0.56 + 1.12 * i / 8, zt + 0.08 + 0.06 * math.sin(math.pi * i / 8) + 0.06 * i / 8) for i in range(9)]
        ring_top = [(y, z) for y, z in top]
        lid = K.strip("lid", ring_top, [(y, z - 0.025) for y, z in ring_top], x0 + 0.01, x0 + 0.64)
        U.add(lid, "paint", rgb, 0)
        bbox(U, "lip", x0 + 0.05, x0 + 0.6, -0.6, -0.56, zt + 0.02, zt + 0.1, rgb, 0, bevel=0.01)
        for hx in (x0 + 0.12, x0 + 0.52):
            U.add(turned("hinge", [(0.018, -0.03), (0.018, 0.03)], (hx, 0.57, zt + 0.12), "x", seg=8), "paint", tuple(c * 0.6 for c in rgb), 0)
    for side in (-1, 1):   # (side handles and the push bar)
        bar(U, "handle", [(x + side * 0.64, -0.25, 1.05), (x + side * 0.7, -0.2, 1.05), (x + side * 0.7, 0.2, 1.05), (x + side * 0.64, 0.25, 1.05)], 0.016, rgb, 0, "paint", n=8)
    bar(U, "pushbar", [(x - 0.45, 0.55, 1.1), (x + 0.45, 0.55, 1.1)], 0.018, rgb, 0, "paint", n=8)
    for xx in (x - 0.45, x + 0.45):
        for y in (-0.34, 0.34):
            bbox(U, "fork", xx - 0.04, xx + 0.04, y - 0.03, y + 0.03, 0.1, 0.2, GALV, 0, "metal", bevel=0.005)
            U.add(turned("castor", [(0.07, -0.022), (0.08, -0.012), (0.08, 0.012), (0.07, 0.022)], (xx, y, 0.08), "y", seg=14), "rubber", TYRE, 0)
            U.add(turned("castorhub", [(0.03, -0.024), (0.03, 0.024)], (xx, y, 0.08), "y", seg=8), "metal", GALV, 0)
    bbox(U, "pedal", x - 0.08, x + 0.08, -0.48, -0.4, 0.12, 0.16, (0.5, 0.05, 0.03), 0, bevel=0.006)


def bins():
    """Two big four-wheeled bins with their lids."""
    U = K.Unit("bins", "")
    bin_1100(U, -0.7, (0.04, 0.06, 0.05))
    bin_1100(U, 0.7, (0.12, 0.12, 0.13))
    return U


# Every prop: its name in the kit, what it is, and how to make it. (The simple panel vans, van(), are out
# (Nov 26): beenster turned down our own code-built cars as too low quality for the game, so the yards wait for
# a van of the detailed standard; DressYard leaves the roller doors clear without one.)
PROPS = [
    ("trailer_white", "Curtain-sided trailer, white", lambda: trailer("curtain", (0.62, 0.63, 0.62))),
    ("trailer_navy", "Curtain-sided trailer, navy", lambda: trailer("curtain", (0.02, 0.035, 0.09))),
    ("trailer_grey", "Curtain-sided trailer, grey", lambda: trailer("curtain", (0.13, 0.135, 0.14))),
    ("trailer_fridge", "Fridge box trailer", lambda: trailer("reefer", (0.68, 0.69, 0.68))),
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
