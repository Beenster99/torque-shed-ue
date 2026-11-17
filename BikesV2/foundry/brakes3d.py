"""Common parts, stage 7: brake discs, calipers, chains and rear sprockets (common parts plan, 16 Nov).

The joint (docs/common_parts_joints.md): a disc keeps the game's own sizes, the outer radius of the bike's rotor
and the inner edge of its braking ring, on the bike's own hub flange at the bike's own offset. A front caliper
sits where the bike's caliper sat, on the same two radial ears 104 mm apart bolted to the lugs cast on the fork
foot (the lugs and the hose union stay the bike's, so the brake lines still meet it). A rear caliper keeps its
place on the carrier. The chain is the bike's own 5/8 inch chain along its own chain line, only its plates
change finish; a rear sprocket keeps the bike's tooth count, centre and offset.

Each part is found from the bike's own parts (rotor, caliper, sprocket names), so it follows whatever side the
bike puts them on, single-sided arms included.

beenster's references (17 Nov, /mnt/project-files/common_parts/refs/brake_ref_*.png) set the styles: wave rotors
on black spoked carriers, round drilled rotors on gold lattice carriers, one-piece discs with teardrop windows
and a gold centre, a star carrier driving the rotor through notches, a radial caliper whose face follows the
pistons, a flat-faced monobloc and a gold two-piece. Our own designs: no maker's name, logo or exact product.

    python3 foundry/brakes3d.py shots [keys] [--samples N]   # out/brakes/*.png
    python3 foundry/brakes3d.py check                         # every part on the lead bike of each family
"""
import json
import math
import os
import re
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bars3d as BR  # noqa: E402
import detail3d as D  # noqa: E402
import lamps3d as L  # noqa: E402

B = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "brakes")
PITCH = 15.875
ALL = ("Naked", "Sport", "Cafe", "Classic", "Adventure", "MX", "Cruiser", "Chopper")

DISCS = {
    "wave": dict(label="Wave rotor, black spokes", fams=("Naked", "Sport", "Cafe", "Adventure", "MX"),
                 note="A wave-edged drilled rotor floating on a black six-spoke carrier."),
    "lattice": dict(label="Gold lattice floater", fams=("Naked", "Sport", "Cafe", "Classic", "Adventure", "Cruiser", "Chopper"),
                    note="A round cross-drilled rotor on a gold carrier cut into a truss of triangles."),
    "onepiece": dict(label="One-piece teardrop", fams=("Naked", "Cafe", "Classic", "Adventure", "MX", "Cruiser", "Chopper"),
                     note="One steel disc with curved slots, teardrop windows in the web and a gold centre."),
    "star": dict(label="Star drive", fams=("Naked", "Sport", "Cafe", "Adventure"),
                 note="A brushed five-arm star whose arm tips drive the rotor through notches in its inner edge."),
}
CALIPERS = {
    "lobed": dict(label="Lobed radial", fams=("Naked", "Sport", "Cafe", "Adventure"), finish="gold",
                  note="Four-piston radial mount; the face swells over each piston."),
    "monobloc": dict(label="Flat monobloc", fams=("Naked", "Sport", "Cafe", "Adventure", "MX"), finish="black",
                     note="Machined from one block: flat faces, sharp chamfers, a bolted pad pin on top."),
    "twopiece": dict(label="Gold two-piece", fams=("Classic", "Cafe", "Cruiser", "Chopper"), finish="gold",
                     note="Two rounded halves held by four exposed bolts, pots domed on the face."),
    "compact": dict(label="Compact twin-pot", fams=("MX", "Classic", "Adventure", "Cruiser", "Chopper"), finish="silver",
                    note="A small one-pot-a-side caliper; also the rear caliper for every front style."),
}
FINISHES = ("gold", "red", "black", "silver", "blue")
CHAINS = {
    "steel": dict(label="Plain steel", note="Bright steel plates, the everyday chain."),
    "gold": dict(label="Gold", note="Gold plates inside and out."),
    "black_gold": dict(label="Black and gold", note="Black outer plates over gold inner plates."),
    "blue": dict(label="Blue", note="Blue outer plates over steel inner plates."),
}
SPROCKETS = {
    "steel": dict(label="Steel", note="Plain steel with round lightening holes."),
    "alu": dict(label="Alloy spoke", note="Hard-anodised alloy cut back to five curved spokes."),
    "twotone": dict(label="Two-tone", note="A steel tooth ring riveted round an alloy centre."),
}


def use(b):
    global B
    B = b
    L.use(b)


def finish_mat(f):
    col = dict(gold=(0.42, 0.27, 0.08), red=(0.45, 0.025, 0.02), black=(0.02, 0.02, 0.022), silver=(0.62, 0.62, 0.64), blue=(0.04, 0.12, 0.42))[f]
    met = dict(gold=1.0, red=0.8, black=0.85, silver=1.0, blue=0.8)[f]
    rough = dict(gold=0.28, red=0.3, black=0.32, silver=0.3, blue=0.3)[f]
    return D.surf("bk_" + f, col, met, rough, wear=0.05)


def mats():
    return dict(rotor=D.surf("bk_rotor", (0.5, 0.5, 0.51), 1.0, 0.3, wear=0.35), steel=B.mat("steel"), rubber=B.mat("rubber"),
                black=finish_mat("black"), gold=finish_mat("gold"), alu=D.brushed(), dark=B.mat("alu_dark"),
                pad=D.surf("bk_pad", (0.06, 0.055, 0.05), 0.0, 0.8))


# ---------------------------------------------------------------- flat shapes with holes
def shape(name, loops, y0, y1, mat, bevel=0.35):
    """A flat part in the side view: loops of (x, z) points in mm, the first the outline and the rest holes
    (any number, any shape), extruded between two y planes with a small rounded edge."""
    MM = B.MM
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions, cu.fill_mode = "2D", "BOTH"
    half = abs(y1 - y0) / 2
    bevel = min(bevel, half * 0.45)
    cu.extrude = (half - bevel) * MM
    cu.bevel_depth, cu.bevel_resolution = bevel * MM, 1
    for loop in loops:
        sp = cu.splines.new("POLY")
        sp.points.add(len(loop) - 1)
        for i, (x, z) in enumerate(loop):
            sp.points[i].co = (x * MM, z * MM, 0.0, 1.0)
        sp.use_cyclic_u = True
    tmp = bpy.data.objects.new(name + "_cu", cu)
    bpy.context.collection.objects.link(tmp)
    tmp.location = (0.0, (y0 + y1) / 2 * MM, 0.0)
    tmp.rotation_euler = (math.pi / 2, 0.0, 0.0)
    bpy.context.view_layer.update()
    me = bpy.data.meshes.new_from_object(tmp.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    mw = tmp.matrix_world.copy()
    bpy.data.objects.remove(tmp, do_unlink=True)
    bpy.data.curves.remove(cu)
    me.transform(mw)
    me.materials.clear()
    me.materials.append(mat)
    try:
        me.shade_smooth()
        me.set_sharp_from_angle(angle=math.radians(35))
    except Exception:
        pass
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    return ob


def circle(c, r, n=None):
    n = n or max(10, min(96, int(r * 1.6)))
    return [(c[0] + r * math.cos(2 * math.pi * k / n), c[1] + r * math.sin(2 * math.pi * k / n)) for k in range(n)]


def polar(c, fn, n=360):
    return [(c[0] + fn(2 * math.pi * k / n) * math.cos(2 * math.pi * k / n), c[1] + fn(2 * math.pi * k / n) * math.sin(2 * math.pi * k / n)) for k in range(n)]


def march(c, inside, rmax, n=360, step=0.25):
    """The outline of a star-shaped region round c: along each ray the last point inside."""
    out = []
    for k in range(n):
        t = 2 * math.pi * k / n
        ct, st = math.cos(t), math.sin(t)
        r = rmax
        while r > 0 and not inside(r * ct, r * st):
            r -= step
        out.append((c[0] + r * ct, c[1] + r * st))
    return out


def sector(c, r1, r2, t0, t1, w1, w2, n=14):
    """A window between two radii and two spokes: the spokes' half widths w1 at r1 and w2 at r2 (mm)."""
    a0, a1 = t0 + w2 / r2, t1 - w2 / r2
    b0, b1 = t0 + w1 / r1, t1 - w1 / r1
    pts = [(c[0] + r2 * math.cos(a0 + (a1 - a0) * k / n), c[1] + r2 * math.sin(a0 + (a1 - a0) * k / n)) for k in range(n + 1)]
    pts += [(c[0] + r1 * math.cos(b1 - (b1 - b0) * k / n), c[1] + r1 * math.sin(b1 - (b1 - b0) * k / n)) for k in range(n + 1)]
    return D.chaikin(pts, 2)


def inset_tri(tri, w):
    (ax, az), (bx, bz), (cx, cz) = tri
    la, lb, lc = math.hypot(bx - cx, bz - cz), math.hypot(ax - cx, az - cz), math.hypot(ax - bx, az - bz)
    p = la + lb + lc
    ix, iz = (la * ax + lb * bx + lc * cx) / p, (la * az + lb * bz + lc * cz) / p
    area = abs((bx - ax) * (cz - az) - (cx - ax) * (bz - az)) / 2
    k = max(0.05, 1 - w / (2 * area / p))
    return D.chaikin([(ix + (x - ix) * k, iz + (z - iz) * k) for x, z in tri], 2)


def drilled(c, rin, d, rows, n, hr, twist=0.0):
    """Cross-drilled holes over the braking band, rows stepping round so they sweep across the pad."""
    w = d - rin
    out = []
    for k in range(n):
        for j in range(rows):
            r = rin + w * (0.17 + 0.66 * j / max(1, rows - 1)) if rows > 1 else rin + w * 0.5
            t = 2 * math.pi * k / n + twist * j
            out.append(circle((c[0] + r * math.cos(t), c[1] + r * math.sin(t)), hr, 12))
    return out


# ---------------------------------------------------------------- discs
def find_disc(where):
    """The bike's rotor: its centre (x, z), outer and inner radii and y, from the mesh."""
    out = []
    for ob in bpy.data.objects:
        m = re.fullmatch(where + r"_rotor(-?\d+)", ob.name)
        if not m or ob.type != "MESH":
            continue
        y = int(m.group(1))
        used = sorted({i for pg in ob.data.polygons for i in pg.vertices})
        vs = [ob.matrix_world @ ob.data.vertices[i].co for i in used]
        xs, zs = [v.x / B.MM for v in vs], [v.z / B.MM for v in vs]
        c = ((min(xs) + max(xs)) / 2, (min(zs) + max(zs)) / 2)
        rr = [math.hypot(x - c[0], z - c[1]) for x, z in zip(xs, zs)]
        out.append(dict(c=c, d=max(rr), rin=min(rr), y=y))
    return out


DISC_PARTS = ("rotor", "carhub", "frame", "tab", "ear", "lug", "strut", "hubbolt", "button", "buttonin", "carout")


def drop_disc(where, y):
    for p in DISC_PARTS:
        L._remove((f"{where}_{p}{y}",))


def build_disc(key, tag, c, d, rin, y, M):
    s = 1 if y >= 0 else -1
    P = D.P
    r_out = rin - 3.0
    rh = max(26.0, min(0.44 * r_out, 62.0))
    ya, yb = y - 2.4, y + 2.4
    k = d / 160.0
    hr = max(2.0, 2.6 * (d - rin) / 40)
    bolts = 6 if rh > 30 else 5
    if key == "onepiece":
        # one steel disc from the hub to the edge: curved slots across the pad band, teardrops in the web
        holes = [circle(c, rh * 0.55, 48)]
        ns = 12 if d > 130 else 9
        for j in range(ns):
            t0 = 2 * math.pi * j / ns
            pts = []
            for e in range(9):
                f = e / 8
                r = rin + (d - rin) * (0.22 + 0.58 * f)
                t = t0 + 0.32 * f
                pts.append((r, t))
            wsl = 1.15 * k
            side1 = [(c[0] + r * math.cos(t) - wsl * math.sin(t + 0.6), c[1] + r * math.sin(t) + wsl * math.cos(t + 0.6)) for r, t in pts]
            side2 = [(c[0] + r * math.cos(t) + wsl * math.sin(t + 0.6), c[1] + r * math.sin(t) - wsl * math.cos(t + 0.6)) for r, t in pts]
            holes.append(D.chaikin(side1 + side2[::-1], 1))
        for j in range(ns):
            for f in (0.3, 0.5, 0.7):
                r = rin + (d - rin) * (0.22 + 0.58 * f)
                t = 2 * math.pi * (j + 0.5) / ns + 0.32 * f
                holes.append(circle((c[0] + r * math.cos(t), c[1] + r * math.sin(t)), hr * 0.9, 12))
        nt = 6 if d > 130 else 5
        r_big, r_tip = rh + (rin - rh) * 0.62, rh + 6
        for j in range(nt):
            t = 2 * math.pi * (j + 0.5) / nt
            rb = min((rin - rh) * 0.3, 2 * math.pi * r_big / nt * 0.3)
            cb = (c[0] + r_big * math.cos(t), c[1] + r_big * math.sin(t))
            tip = (c[0] + r_tip * math.cos(t), c[1] + r_tip * math.sin(t))
            dd = r_big - r_tip
            th = math.acos(min(0.95, rb / dd))
            phi = t + math.pi
            pts = [tip] + [(cb[0] + rb * math.cos(phi + th + (2 * math.pi - 2 * th) * e / 24), cb[1] + rb * math.sin(phi + th + (2 * math.pi - 2 * th) * e / 24)) for e in range(25)]
            holes.append(D.chaikin(pts, 1))
        shape(f"bk{tag}_disc{y}", [circle(c, d, 160)] + holes, y - 2.5, y + 2.5, M["rotor"], 0.4)
        # the gold centre: a machined ring over the hub flange carrying the bolts
        shape(f"bk{tag}_centre{y}", [circle(c, rh + 2, 64), circle(c, rh * 0.55, 48)], y + s * 2.3, y + s * 4.8, M["gold"], 0.6)
        for j in range(bolts):
            q = B.add(c, B.unit(360.0 / bolts * j + 30), rh * 0.8)
            D.bolt(f"bk{tag}_bolt{y}{j}", (q[0], y + s * 4.8, q[1]), (0, s, 0), min(5.0, rh * 0.1), 2.5, M["steel"])
        return
    # a floating rotor: a ring from rin to d, drawn with its own edge and drilling
    if key == "wave":
        nw = max(14, round(2 * math.pi * d / 36))
        outer = polar(c, lambda t: d - 2.6 * (1 - math.cos(nw * t)), nw * 12)
        holes = [circle(c, rin, 128)] + drilled(c, rin, d, max(3, int((d - rin) / 13)), nw, hr, 2 * math.pi / nw / 2)
    elif key == "lattice":
        outer = circle(c, d, 160)
        holes = [circle(c, rin, 128)] + drilled(c, rin, d, max(3, int((d - rin) / 13)), round(2 * math.pi * d / 26), hr * 0.9, 0.07)
    else:   # star: notches in the ring's inner edge take the arm tips
        na = 5
        hw = rin * 0.11

        def ring_hole(x, z):
            r = math.hypot(x, z)
            if r < rin:
                return True
            t = math.atan2(z, x)
            for j in range(na):
                da = t - 2 * math.pi * j / na
                lat, lon = r * math.sin(da), r * math.cos(da)
                if abs(lat) < hw + 0.6 and lon < rin + 4.6:
                    return True
            return False
        outer = circle(c, d, 160)
        holes = [march(c, ring_hole, rin + 6, 360)] + drilled(c, rin + 3, d, max(3, int((d - rin) / 13)), round(2 * math.pi * d / 28), hr, 0.07)
    shape(f"bk{tag}_rotor{y}", [outer] + holes, y - 2.5, y + 2.5, M["rotor"], 0.4)
    hub = circle(c, rh * 0.55, 48)
    if key == "wave":
        n_sp = 6
        car = [circle(c, rin - 0.6, 128), hub]
        w_sp = max(6.0, rin * 0.085)
        for j in range(n_sp):
            t0, t1 = 2 * math.pi * j / n_sp + 0.15, 2 * math.pi * (j + 1) / n_sp + 0.15
            car.append(sector(c, rh + 4, rin - 9, t0, t1, w_sp * 1.4, w_sp))
        shape(f"bk{tag}_carrier{y}", car, ya, yb, M["black"], 0.6)
        nb = 2 * n_sp
    elif key == "lattice":
        nb = 12 if d > 130 else 10
        nh = nb // 2
        r_o, r_i = rin - 4.5, rh + 3
        O = [B.add(c, B.unit(360.0 / nb * j), r_o) for j in range(nb)]
        I_ = [B.add(c, B.unit(360.0 / nb * (2 * h + 1)), r_i) for h in range(nh)]
        car = [circle(c, rin - 0.6, 128), hub]
        ws = max(2.4, rin * 0.028)
        for h in range(nh):
            for tri in ((I_[h], O[(2 * h) % nb], O[(2 * h + 1) % nb]), (I_[h], O[(2 * h + 1) % nb], O[(2 * h + 2) % nb]),
                        (I_[h], O[(2 * h + 2) % nb], I_[(h + 1) % nh])):
                car.append(inset_tri(tri, ws))
        shape(f"bk{tag}_carrier{y}", car, ya, yb, M["gold"], 0.5)
    else:
        nb = 0

        def star(x, z):
            r = math.hypot(x, z)
            if r < rh + 8:
                return True
            t = math.atan2(z, x)
            for j in range(5):
                da = t - 2 * math.pi * j / 5
                lat, lon = r * math.sin(da), r * math.cos(da)
                wid = hw + (rin - lon) * 0.12 if lon < rin else hw
                if abs(lat) < wid and lon < rin + 4 and lon > 0:
                    return True
            return False
        car = [march(c, star, rin + 6, 360), hub]
        for j in range(5):
            t = 2 * math.pi * j / 5
            r0, r1 = rh + 10, rin - 8
            u, v = (math.cos(t), math.sin(t)), (-math.sin(t), math.cos(t))
            wsl = hw * 0.42
            pts = [(c[0] + u[0] * r + v[0] * sg * wsl * (0.7 + 0.3 * (r - r0) / (r1 - r0)), c[1] + u[1] * r + v[1] * sg * wsl * (0.7 + 0.3 * (r - r0) / (r1 - r0)))
                   for sg, rs in ((1, (r0, r1)), (-1, (r1, r0))) for r in rs]
            car.append(D.chaikin(pts, 3))
        shape(f"bk{tag}_carrier{y}", car, ya - 0.6, yb + 0.6, M["alu"], 0.7)
        for j in range(5):
            q = B.add(c, B.unit(72.0 * j), rin + 1.0)
            B.cone(f"bk{tag}_clip{y}{j}", (q[0], y - 3.6, q[1]), (q[0], y + 3.6, q[1]), hw * 0.55, hw * 0.55, M["steel"], 16)
    # the hub ring that bolts over the bike's flange, and the floating buttons
    B.revolve(f"bk{tag}_carhub{y}", [(rh, yb + 1), (rh, ya - 1), (rh * 0.55, ya - 1), (rh * 0.55, yb + 1)], c, M["black"] if key == "wave" else M["gold"] if key == "lattice" else M["alu"], 48)
    for j in range(bolts):
        q = B.add(c, B.unit(360.0 / bolts * j + 30), rh * 0.78)
        D.bolt(f"bk{tag}_hubbolt{y}{j}", (q[0], y + s * 3.4, q[1]), (0, s, 0), min(5.0, rh * 0.1), 4.0, M["steel"])
    wb = max(5.0, rin * 0.095) * 0.95
    for j in range(nb):
        q = B.add(c, B.unit(360.0 / nb * j + (360.0 / nb / 2 if key == "wave" else 0)), rin + 0.5)
        B.cone(f"bk{tag}_button{y}{j}", (q[0], y - 4.6, q[1]), (q[0], y + 4.6, q[1]), wb, wb, M["gold"] if key == "wave" else M["steel"], 20)
        B.cone(f"bk{tag}_buttonin{y}{j}", (q[0], y - 4.9, q[1]), (q[0], y + 4.9, q[1]), wb * 0.42, wb * 0.42, M["rubber"], 12)


# ---------------------------------------------------------------- calipers
def caliper_frame(cc, c, uf=None):
    L_ = math.hypot(cc[0] - c[0], cc[1] - c[1])
    er = ((cc[0] - c[0]) / L_, (cc[1] - c[1]) / L_)
    ea = (-er[1], er[0])
    if uf and ea[0] * uf[0] + ea[1] * uf[1] < 0:
        ea = (-ea[0], -ea[1])
    return er, ea


def lobed_outline():
    pots = (((-27, -8), 25.0), ((27, -8), 23.0))

    def inside(a, r):
        if -50 <= a <= 50 and -14 <= r <= 40:
            # rounded top corners
            if abs(a) > 40 and r > 30:
                return math.hypot(abs(a) - 40, r - 30) < 10
            return True
        return any(math.hypot(a - p[0], r - p[1]) < R for p, R in pots)
    return [(x, z) for x, z in march((0, 6), lambda a, r: inside(a, r + 6), 70, 180, 0.25)]


def build_caliper(key, tag, cc, er, ea, y, s, k, M, body, ears=True, fork_lug=True):
    P = D.P
    add = B.add

    def Q(a, r):
        return (cc[0] + (ea[0] * a + er[0] * r) * k, cc[1] + (ea[1] * a + er[1] * r) * k)
    steel = M["steel"]
    yo0, yo1, yi0, yi1 = y + s * 6.5, y + s * 37 * k, y - s * 6.5, y - s * 33 * k
    if key == "lobed":
        half, inset = lobed_outline(), 6
    elif key == "monobloc":
        half = D.chaikin([(-62, -38), (62, -38), (62, 40), (-62, 40)], 2)
        half = [(a, r) for a, r in half]
        inset = 2.5
    elif key == "twopiece":
        half = [(62 * math.cos(t) * (1 - 0.08 * math.sin(t)), 1 + 40 * math.sin(t)) for t in [2 * math.pi * j / 48 for j in range(48)]]
        inset = 11
    else:
        half = [(46 * math.cos(t), 3 + 37 * math.sin(t)) for t in [2 * math.pi * j / 40 for j in range(40)]]
        inset = 9
    poly = [Q(a, r) for a, r in half]
    B.pillow(f"{tag}_out", poly, min(yo0, yo1), max(yo0, yo1), body, inset, inset + 4)
    B.pillow(f"{tag}_in", poly, min(yi0, yi1), max(yi0, yi1), body, inset, inset + 4)
    lo, hi = min(yi1, yo1), max(yi1, yo1)
    aw = 44 if key == "compact" else 60
    for n, (a0, a1) in enumerate(((-aw, -22), (22, aw))):
        B.prism(f"{tag}_bridge{n}", [Q(a0, 28), Q(a1, 28), Q(a1 * 0.85 if a1 > 0 else a1, 40), Q(a0 if a0 > 0 else a0 * 0.85, 40)], lo + 1, hi - 1, body, 3 if key != "monobloc" else 1.2)
    pw = 30 if key == "compact" else 42
    for sg in (-1, 1):
        B.prism(f"{tag}_pad{sg}", [Q(-pw, -26), Q(pw, -26), Q(pw, 24), Q(-pw, 24)], y + sg * 3.2, y + sg * 6.4, M["pad"], 0.5)
        B.prism(f"{tag}_padback{sg}", [Q(-pw - 4, -28), Q(pw + 4, -28), Q(pw + 4, 32), Q(-pw - 4, 32)], y + sg * 6.4, y + sg * 8.5, steel, 0.5)
    # the pad pin across the top window
    q = Q(0, 33)
    B.tube(f"{tag}_pin", [P(q, lo - 3), P(q, hi + 3)], 2.6 * k, steel)
    if key == "monobloc":
        D.bolt(f"{tag}_pinbolt", P(q, hi + 3) if s > 0 else P(q, lo - 3), (0, s, 0), 5.0 * k, 4, steel, False)
    pots = {"lobed": ((-27, -8, 18.5), (27, -8, 16.5)), "monobloc": ((-27, -6, 17.5), (27, -6, 15.5)),
            "twopiece": ((-27, -6, 18.0), (27, -6, 16.0)), "compact": ((0, -4, 21.0),)}[key]
    for sg, yf in ((s, yo1), (-s, yi1)):
        for n, (a, r, rr) in enumerate(pots):
            q = Q(a, r)
            if key == "lobed":
                B.cone(f"{tag}_dome{sg}{n}", P(q, yf - sg * 1), P(q, yf + sg * 2.4), rr * k * 1.05, rr * k * 0.78, body, 32)
            elif key in ("twopiece", "compact"):
                B.cone(f"{tag}_dome{sg}{n}", P(q, yf - sg * 1), P(q, yf + sg * 4.0), rr * k, rr * k * 0.7, body, 32)
                B.cone(f"{tag}_domecap{sg}{n}", P(q, yf + sg * 4.0), P(q, yf + sg * 4.8), rr * k * 0.42, rr * k * 0.38, steel, 16)
            else:
                # a machined ring marks each piston on the flat face
                B.cone(f"{tag}_ring{sg}{n}", P(q, yf - sg * 0.5), P(q, yf + sg * 0.6), rr * k * 0.95, rr * k * 0.95, steel, 32)
                B.cone(f"{tag}_ringin{sg}{n}", P(q, yf - sg * 0.4), P(q, yf + sg * 0.75), rr * k * 0.85, rr * k * 0.85, body, 32)
    if key == "twopiece":
        # four through bolts hold the halves: heads outside, nuts inside
        for n, (a, r) in enumerate(((-50, -24), (50, -24), (-46, 24), (46, 24))):
            q = Q(a, r)
            D.bolt(f"{tag}_thru{n}", P(q, yo1), (0, s, 0), 5.5 * k, 4.5, steel, False)
            D.bolt(f"{tag}_thrunut{n}", P(q, yi1), (0, -s, 0), 5.5 * k, 4.5, steel, False)
    if key == "monobloc":
        for n, a in enumerate((-46, 46)):
            q = Q(a, 26)
            D.bolt(f"{tag}_plug{n}", P(q, yo1), (0, s, 0), 4.0 * k, 1.5, steel, True)
    e3 = (er[0], 0, er[1])
    q = Q(48 if key != "compact" else 32, 36)
    B.cone(f"{tag}_bleedboss", P(q, yo1 - s * 12 * k), P(add(q, er, 7), yo1 - s * 12 * k), 6, 5, body, 12)
    B.cone(f"{tag}_bleed", P(add(q, er, 7), yo1 - s * 12 * k), P(add(q, er, 19), yo1 - s * 12 * k), 3.2, 2.2, steel, 10)
    B.cone(f"{tag}_bleedcap", P(add(q, er, 15), yo1 - s * 12 * k), P(add(q, er, 21), yo1 - s * 12 * k), 3.6, 3.4, M["rubber"], 10)
    if ears:
        ym = y + s * 16
        for n, a in enumerate((-52, 52)):
            e0, e1 = Q(a, -52), Q(a, -6)
            B.cone(f"{tag}_ear{n}", P(e0, ym), P(e1, ym), 12.5, 12.5, body, 24)
            D.bolt(f"{tag}_earbolt{n}", P(e1, ym), e3, 8.5, 7, steel, True)
            B.cone(f"{tag}_earwasher{n}", P(e1, ym), P(add(e1, er, 1.2), ym), 10.5, 10.5, steel, 20)


def front_caliper_place(model, c, d):
    F, uf, nb = c["F"], c["uf"], c["nb"]
    return B.add(B.add(F, uf, 75), nb, math.sqrt(max(1.0, (d - 25) ** 2 - 75 ** 2)))


def rear_caliper_place(model, A, d):
    rc = model.get("rear_caliper")
    if not rc:
        return None
    cc = (sum(q[0] for q in rc["body"]) / len(rc["body"]), sum(q[1] for q in rc["body"]) / len(rc["body"]))
    L_ = math.hypot(cc[0] - A[0], cc[1] - A[1])
    return (A[0] + (cc[0] - A[0]) * (d - 14) / L_, A[1] + (cc[1] - A[1]) * (d - 14) / L_)


def drop_caliper(prefix):
    keep = (prefix + "lug", prefix + "banjo")
    L._remove((prefix,), keep=keep)


# ---------------------------------------------------------------- chain and sprocket
def chain_finish(key, M):
    gold = D.surf("chaingold", (0.72, 0.5, 0.16), 1.0, 0.3, wear=0.25)
    plate = D.surf("chainplate", (0.16, 0.16, 0.17), 1.0, 0.38, wear=0.3)
    bright = D.surf("bk_chainsteel", (0.55, 0.55, 0.56), 1.0, 0.28, wear=0.3)
    blue = D.surf("bk_chainblue", (0.03, 0.1, 0.38), 0.85, 0.3, wear=0.2)
    black = D.surf("bk_chainblack", (0.02, 0.02, 0.022), 0.6, 0.35, wear=0.25)
    outer, inner = dict(steel=(bright, bright), gold=(gold, gold), black_gold=(black, gold), blue=(blue, bright))[key]
    for nm, m in (("chainouter", outer), ("chaininner", inner), ("chainrollers", plate)):
        ob = bpy.data.objects.get(nm)
        if ob is not None:
            ob.data.materials.clear()
            ob.data.materials.append(m)


def teeth(cen, R, n):
    out = []
    for k in range(n):
        for da, dr in ((-0.5, -5.2), (-0.28, -4.6), (-0.12, 1.5), (0.0, 3.6), (0.12, 1.5), (0.28, -4.6)):
            w = 2 * math.pi * (k + 0.5 + da) / n
            out.append((cen[0] + (R + dr) * math.cos(w), cen[1] + (R + dr) * math.sin(w)))
    return out


def build_sprocket(key, model, M, finish="black"):
    ob = bpy.data.objects.get("sprocket")
    sw = model["swingarm"]
    if ob is None or "sprocket_r" not in sw:
        return False
    vs = [ob.matrix_world @ v.co for v in ob.data.vertices]
    xs, ys, zs = [v.x / B.MM for v in vs], [v.y / B.MM for v in vs], [v.z / B.MM for v in vs]
    cen = ((min(xs) + max(xs)) / 2, (min(zs) + max(zs)) / 2)
    Y = (min(ys) + max(ys)) / 2
    for o in [o for o in bpy.data.objects if o.name == "sprocket" or o.name.startswith("cut_sprocket_")]:
        bpy.data.objects.remove(o, do_unlink=True)
    n = max(30, round(2 * math.pi * sw["sprocket_r"] / PITCH))
    R = PITCH / (2 * math.sin(math.pi / n))
    ya, yb = Y - 3.2, Y + 3.2
    if key == "steel":
        holes = []
        for j in range(6):
            for dw, rad_, r_ in ((0.0, R * 0.68, R * 0.14), (0.5, R * 0.56, R * 0.07)):
                holes.append(circle(B.add(cen, B.unit(60.0 * (j + dw)), rad_), r_, 24))
        shape("bksprocket", [teeth(cen, R, n)] + holes, ya, yb, D.surf("bk_sprsteel", (0.36, 0.36, 0.37), 1.0, 0.36, wear=0.3), 0.3)
    elif key == "alu":
        holes = []
        for j in range(5):
            t0 = 2 * math.pi * j / 5
            pts = []
            r1, r2 = R * 0.45, R - 17
            for e in range(13):
                f = e / 12
                r = r1 + (r2 - r1) * f
                pts.append((r, t0 + 0.13 + 0.35 * f))
            for e in range(13):
                f = 1 - e / 12
                r = r1 + (r2 - r1) * f
                pts.append((r, t0 + 2 * math.pi / 5 - 0.13 - 0.05 * f + 0.35 * f * 0.4))
            holes.append(D.chaikin([(cen[0] + r * math.cos(t), cen[1] + r * math.sin(t)) for r, t in pts], 2))
        shape("bksprocket", [teeth(cen, R, n)] + holes, ya, yb, finish_mat(finish), 0.3)
    else:
        rs = R - 20
        shape("bksprocket", [teeth(cen, R, n), circle(cen, rs, 120)], ya, yb, D.surf("bk_sprsteel", (0.36, 0.36, 0.37), 1.0, 0.36, wear=0.3), 0.3)
        holes = []
        for j in range(6):
            t = math.radians(60.0 * j + 30)
            holes.append(circle((cen[0] + rs * 0.66 * math.cos(t), cen[1] + rs * 0.66 * math.sin(t)), rs * 0.2, 32))
        shape("bksprocketcentre", [circle(cen, rs - 0.3, 120)] + holes, ya - 0.8, yb + 0.8, finish_mat(finish), 0.6)
        for j in range(12):
            q = B.add(cen, B.unit(30.0 * j + 15), rs)
            B.cone(f"bksprocketrivet{j}", (q[0], ya - 1.6, q[1]), (q[0], yb + 1.6, q[1]), 3.0, 3.0, M["steel"], 12)
    return True


# ---------------------------------------------------------------- fitting
def fit(b, model, c, disc=None, cal=None, finish=None, chain=None, sprocket=None, spr_finish=None):
    use(b)
    M = mats()
    fam = model["family"]
    before = set(bpy.data.objects.keys())
    rep = dict(model=model.get("name", ""), family=fam)
    found = {w: find_disc(w) for w in ("front", "rear")}
    if disc and fam in DISCS[disc]["fams"]:
        got = []
        for where in ("front", "rear"):
            for r in found[where]:
                drop_disc(where, r["y"])
                build_disc(disc, where[0], r["c"], r["d"], r["rin"], r["y"], M)
                got.append((where, r["y"], round(r["d"]), round(r["rin"])))
        rep["disc"] = disc
        rep["discs"] = got
    if cal and fam in CALIPERS[cal]["fams"]:
        f = finish or CALIPERS[cal]["finish"]
        body = finish_mat(f)
        for r in found["front"]:
            s = 1 if r["y"] >= 0 else -1
            pre = f"caliper{s}_"
            if bpy.data.objects.get(pre + "out") is None:
                continue
            drop_caliper(pre)
            cc = front_caliper_place(model, c, r["d"])
            er, ea = caliper_frame(cc, r["c"], c["uf"])
            build_caliper(cal, f"bkcal{s}", cc, er, ea, r["y"], s, 1.0, M, body, True)
        for r in found["rear"]:
            if bpy.data.objects.get("rearcaliper_out") is None:
                continue
            cc = rear_caliper_place(model, r["c"], r["d"])
            if cc is None:
                continue
            drop_caliper("rearcaliper_")
            s = 1 if r["y"] >= 0 else -1
            er, ea = caliper_frame(cc, r["c"])
            build_caliper("compact", "bkrcal", cc, er, ea, r["y"], s, 0.6, M, body, False)
        rep["caliper"] = cal
        rep["finish"] = f
    if chain and bpy.data.objects.get("chainouter") is not None:
        chain_finish(chain, M)
        rep["chain"] = chain
    if sprocket and build_sprocket(sprocket, model, M, spr_finish or "black"):
        rep["sprocket"] = sprocket
        rep["spr_finish"] = spr_finish or "black"
    made = [o for o in bpy.data.objects.keys() if o not in before and not o.startswith("cut_")]
    return rep, made


def built(mn, **kw):
    import build3d
    b3, m, cc, r, made0 = L.built(mn)
    use(build3d)
    rep, made = fit(build3d, m, cc, **kw)
    return build3d, m, cc, rep, made


# ---------------------------------------------------------------- pictures and the check
SHOTS = [
    ("disc", "wave", "kazeda_sport", dict(cal="monobloc", finish="black")),
    ("disc", "lattice", "corvano_naked", dict(cal="lobed", finish="gold")),
    ("disc", "onepiece", "hartwell_classic", dict(cal="twopiece", finish="gold")),
    ("disc", "star", "corvano_naked", dict(cal="lobed", finish="red")),
    ("cal", "lobed", "corvano_naked", dict(disc="lattice", finish="gold")),
    ("cal", "monobloc", "kazeda_sport", dict(disc="wave", finish="black")),
    ("cal", "twopiece", "lodestar_cruiser", dict(disc="lattice", finish="gold")),
    ("cal", "compact", "varga_mx", dict(disc="wave", finish="silver")),
    ("drive", "steel", "corvano_naked", dict(chain="steel", sprocket="steel")),
    ("drive", "gold", "corvano_naked", dict(chain="gold", sprocket="alu", spr_finish="gold")),
    ("drive", "black_gold", "kazeda_sport", dict(chain="black_gold", sprocket="twotone", spr_finish="black")),
    ("drive", "blue", "corvano_adventure", dict(chain="blue", sprocket="alu", spr_finish="blue")),
]


def shots(which=None, samples=40):
    os.makedirs(OUT, exist_ok=True)
    rep = {}
    for kind, key, mn, other in SHOTS:
        if which and key not in which:
            continue
        kw = dict(other)
        if kind != "drive":
            kw[kind] = key
        b3, m, cc, r, made = built(mn, **kw)
        rep[f"{kind}_{key}"] = r
        if kind == "drive":
            c_ = L._centre(("bksprocket",))
        else:
            c_ = L._centre(("bkf_",)) or L._centre(("bkcal",))
        if not c_:
            print("NO PART", kind, key, mn)
            continue
        ctr, lo, hi = c_
        if kind == "drive":
            tgt = (ctr[0] + 10, 108, ctr[2] + 30)
            loc = BR.vadd(tgt, (330, 640, 420))
        elif kind == "cal":
            ys = [o for o in bpy.data.objects if o.name.startswith("bkcal1_out") or o.name.startswith("bkcal-1_out")]
            cb = L._centre(("bkcal1_", "bkcal-1_"))
            tgt = cb[0] if cb else ctr
            side = 1 if tgt[1] > 0 else -1
            loc = BR.vadd(tgt, (260, side * 520, 140))
        else:
            fb = [r_ for r_ in find_disc("front")]
            tgt = (ctr[0], 78, ctr[2])
            loc = BR.vadd(tgt, (200, 820, 120))
        L.render_at(b3, os.path.join(OUT, f"{kind}_{key}.png"), loc, tgt, 50, res=(1100, 800), samples=samples)
    path = os.path.join(OUT, "shots.json")
    old = json.load(open(path)) if os.path.exists(path) else {}
    old.update(rep)
    json.dump(old, open(path, "w"), indent=1)


OK = ("bk | bk", "bkf | front_flange", "bkr | rear_flange", "bkf | caliper", "bkcal | caliper", "bkcal | bkf", "bkrcal | bkr", "bkrcal | rearcaliper",
      "bkf | front_hub", "bkr | rear_hub", "bkf | axle", "bkr | axle", "bksprocket | chain", "bksprocket | sprocketnut", "bksprocket | rear_hub",
      "bkcal | forkbottom", "bkcal | axleboss", "bkf | forkbottom", "bkf | axleboss")


def ok_pair(pair):
    a, b_ = [x.strip() for x in pair.split("|")]
    for j in OK:
        ja, jb = [x.strip() for x in j.split("|")]
        if (a.startswith(ja) and b_.startswith(jb)) or (b_.startswith(ja) and a.startswith(jb)):
            return True
    return False


def check(keys=None):
    res = {}
    leads = dict(BR.LEADS)
    leads["Chopper"] = "lodestar_chopper"
    jobs = [("disc", k, dict(disc=k), h["fams"]) for k, h in DISCS.items()]
    jobs += [("cal", k, dict(cal=k), h["fams"]) for k, h in CALIPERS.items()]
    jobs += [("sprocket", k, dict(sprocket=k, chain="gold"), ALL) for k in SPROCKETS]
    for fam, mn in leads.items():
        for kind, key, kw, fams in jobs:
            if fam not in fams or (keys and key not in keys):
                continue
            b3, m, cc, r, made = built(mn, **kw)
            bad = {k: v for k, v in BR.clip_check(made).items() if not ok_pair(k)}
            fitted = (kind if kind != "cal" else "caliper") in r
            res.setdefault(kind, {}).setdefault(key, {})[fam] = dict(model=mn, clips=bad, fitted=fitted, rep=r)
            print(f"BCHECK {kind:8s} {key:9s} {fam:10s} fitted={fitted} {bad}", flush=True)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "check.json")
    old = json.load(open(path)) if (keys and os.path.exists(path)) else {}
    for kind, v in res.items():
        for k, per in v.items():
            old.setdefault(kind, {}).setdefault(k, {}).update(per)
    json.dump(old, open(path, "w"), indent=1)


if __name__ == "__main__":
    a = sys.argv[1:]
    cmd = a[0] if a else "shots"
    smp = int(a[a.index("--samples") + 1]) if "--samples" in a else 40
    rest = [x for x in a[1:] if not x.startswith("--") and x != str(smp)]
    if cmd == "shots":
        shots(rest or None, smp)
    elif cmd == "check":
        check(rest or None)
