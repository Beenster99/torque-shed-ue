"""Common parts, stage 2: lever sets, switch pods and mirrors (common parts plan, 16 Nov).

Everything here is built in the grip's own frame, so it sits right on any bar from bars3d or on a bike's
own bar: a = along the bar outwards from the inner end of the grip (the perch is at a = -82 to -58, the
switch pod at -52 to -4), f = forward, square to the bar, u = up. The joints are the ones agreed in
docs/common_parts_joints.md: perch clamp at y = +-176 to +-200, pivot 48 mm ahead of the bar, switch
pods at +-206 to +-254, an M10 boss on top of each perch for stalk mirrors, the bar end for bar-end
mirrors, and the fairing shoulder (found on the bike) for fairing mirrors.

Every mirror is aimed by script: its glass is turned so that from the rider's eye it shows the road
straight behind, a little outboard (beenster: mirrors must be usable).

Our own designs with real parts as reference only: no maker's name, logo or exact product ships.

    python3 foundry/controls3d.py shots              # out/controls/*.png: each lever set, switch pod and mirror on a bike
    python3 foundry/controls3d.py check              # mirror view and clipping, every mirror on the lead bike of each family
"""
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bars3d as BR  # noqa: E402
import detail3d as D  # noqa: E402

B = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "controls")

LEVERS = {
    "standard": dict(label="Billet adjustable set", note="Levers that drop from the pivot and run straight along the grip, red span dials, round fluid cup on a neck."),
    "short": dict(label="Short lever set", note="Two-finger levers that stop halfway along the grip, block reservoir with a sight glass cast into the perch."),
    "folding": dict(label="Folding radial set", note="Adjustable levers whose tips fold back in a spill, radial master cylinders with remote fluid pots on both sides."),
    "classic": dict(label="Classic polished set", note="Round-section polished levers with ball ends, a chrome round pot on the brake perch and a cable clutch."),
    "forged": dict(label="Forged set", note="Deeper forged levers in the same line, no dial, an angled reservoir with a sight glass: naked and adventure."),
}
SWITCHES = {   # buttons only (beenster, 16 Nov), with a red kill switch on the right on every set
    "dpad": dict(label="D-pad pod", note="A four-way pad with a centre key and four keys round it, horn and hazard buttons below, indicator under the thumb."),
    "cruise": dict(label="Cruise pod", note="An angled pod with a column of mode keys, set up and down keys, reset, horn, indicator and hazard."),
    "compact": dict(label="Compact pod", note="A small pod with a four-way pad, horn and indicator buttons: classics and minimal bikes."),
}
MIRRORS = {
    "round": dict(label="Round stalk", kind="stalk", note="A round head on a slim stalk from the perch."),
    "oval": dict(label="Oval stalk", kind="stalk", note="The signed-off naked mirror: a wide oval head on a slim stalk."),
    "rect": dict(label="Rectangular stalk", kind="stalk", note="A squared head with rounded corners on a cast aero stem."),
    "teardrop": dict(label="Teardrop stalk", kind="stalk", note="A head that tapers to a point inboard, chrome back: classic and cruiser."),
    "barend_round": dict(label="Bar-end round", kind="barend", note="A round head on a short stem from a clamp on the bar end."),
    "barend_teardrop": dict(label="Bar-end teardrop", kind="barend", note="The teardrop head (M4) on a short chrome arm from the bar end: cafe, classic and naked."),
    "fairing_aero": dict(label="Fairing aero", kind="fairing", note="A wide head on an aero arm from the fairing shoulder, plain back."),
    "fairing_wing": dict(label="Fairing wing", kind="fairing", note="A low flat head almost on the fairing, on a short wing-shaped boss."),
}


def use(b):
    global B
    B = b
    D.B = b
    BR.use(b)


# ---------------------------------------------------------------- the grip frame
class Frame:
    def __init__(self, g0, d, s):
        self.o, self.s = Vector(g0), s
        self.a = Vector(d).normalized()
        up = Vector((0, 0, 1))
        self.u = (up - self.a * self.a.dot(up)).normalized()
        X = Vector((1, 0, 0))
        f = X - self.a * self.a.dot(X) - self.u * self.u.dot(X)
        self.f = f.normalized()

    def P(self, a, f, u):
        q = self.o + self.a * a + self.f * f + self.u * u
        return (q.x, q.y, q.z)

    def dir(self, a, f, u):
        q = self.a * a + self.f * f + self.u * u
        q.normalize()
        return (q.x, q.y, q.z)


def plate(name, fr, outline, u0, u1, material, bevel=0.0):
    """A plate cut to an outline in the bar's plane (a, f), between two heights u0..u1 of the frame."""
    n = len(outline)
    verts = [B.V(*fr.P(a, f, u0)) for a, f in outline] + [B.V(*fr.P(a, f, u1)) for a, f in outline]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, 2 * n))] + [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    ob = B.mesh_obj(name, verts, faces, material, True, bevel)
    B.bm_fix_normals(ob)
    return ob


def side_plate(name, fr, outline, a0, a1, material, bevel=0.0):
    """A block cut to a side outline (f, u) between two places along the bar a0..a1."""
    n = len(outline)
    verts = [B.V(*fr.P(a0, f, u)) for f, u in outline] + [B.V(*fr.P(a1, f, u)) for f, u in outline]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, 2 * n))] + [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    ob = B.mesh_obj(name, verts, faces, material, True, bevel)
    B.bm_fix_normals(ob)
    return ob


def smooth(cl, n=3):
    for _ in range(n):
        o_ = [cl[0]]
        for i_ in range(len(cl) - 1):
            o_ += [tuple(0.75 * cl[i_][k] + 0.25 * cl[i_ + 1][k] for k in range(len(cl[0]))), tuple(0.25 * cl[i_][k] + 0.75 * cl[i_ + 1][k] for k in range(len(cl[0])))]
        cl = o_ + [cl[-1]]
    return cl


def outline_of(cl):
    """Left and right edges of a plate lever from its centre line [(t along, f across, half width)]."""
    lft, rgt = [], []
    for i_, (t_, f_, w_) in enumerate(cl):
        a_, b_ = cl[max(0, i_ - 1)], cl[min(len(cl) - 1, i_ + 1)]
        dt, df = b_[0] - a_[0], b_[1] - a_[1]
        Ld = math.hypot(dt, df)
        lft.append((t_ - df / Ld * w_, f_ + dt / Ld * w_))
        rgt.append((t_ + df / Ld * w_, f_ - dt / Ld * w_))
    return lft, rgt


# ---------------------------------------------------------------- materials
def mats():
    return dict(
        satin=D.surf("leverblack", (0.035, 0.035, 0.04), 0.85, 0.36),
        steel=B.mat("steel"), plastic=B.mat("plastic"), rubber=B.mat("rubber"), dark=B.mat("alu_dark"),
        polished=D.surf("ctl_polished", (0.82, 0.82, 0.84), 1.0, 0.1, wear=0.04),
        chrome=B.mat("chrome"),
        red=D.surf("anored", (0.5, 0.025, 0.03), 1.0, 0.32, wear=0.05),
        gold=D.surf("ctl_gold", (0.72, 0.46, 0.08), 1.0, 0.24, wear=0.04),
        forged=D.surf("ctl_forged", (0.05, 0.05, 0.055), 0.6, 0.55, bump=(0.4, 0.15)),
        cup=D.surf("rescup", (0.55, 0.5, 0.36), 0, 0.25, wear=0.0),
        smoke=D.glass_mat("ctl_smoke", (0.35, 0.3, 0.25)),
        grey=D.surf("swgrey", (0.12, 0.12, 0.13), 0, 0.5),
        killred=D.surf("killred", (0.5, 0.02, 0.02), 0, 0.45),
        glass=D.surf("mirrorglass", (0.9, 0.92, 0.95), 1.0, 0.01, wear=0.0),
        mark=D.surf("ctl_mark", (0.85, 0.86, 0.88), 0.0, 0.5, wear=0.0),
        amber=B.mat("amberlens"),
    )


# ---------------------------------------------------------------- levers and master cylinders
def perch(fr, tag, M, body, bolts=True, wide=24):
    """The lever perch: a split clamp round the bar with the pivot boss out in front."""
    a0, a1 = -82, -82 + wide
    po = D.chaikin([(-17, -17), (22, -18), (56, -22), (60, -8), (30, 4), (14, 18), (-12, 18), (-19, 0)], 2)
    side_plate(f"cperch{tag}", fr, po, a0, a1, body, 2.0)
    if bolts:
        for du in (-11, 11):
            D.bolt(f"cperchbolt{tag}{du}", fr.P((a0 + a1) / 2, -18.5, du), fr.dir(0, -1, 0), 4.2, 3.5)
    return (a0 + a1) / 2


def plate_lever(fr, tag, M, cl, holes, body, ring=True, fold=None, flat=5.5):
    """A lever cut from plate, lying in the bar's plane under the pivot (like detail3d's signed-off lever)."""
    piv = (-70, 48, -14)
    cl = smooth(cl)
    lft, rgt = outline_of(cl)
    te, fe = cl[-1][0] + 6, cl[-1][1] + 0.5
    root = [(12.5 * math.cos(math.radians(w)), 12.5 * math.sin(math.radians(w))) for w in range(-100, -261, -20)]
    if ring:
        tip = [(te + 8.6 * math.cos(math.radians(w)), fe + 8.6 * math.sin(math.radians(w))) for w in range(140, -141, -20)]
    else:
        r_ = cl[-1][2]
        tip = [(cl[-1][0] + r_ * math.cos(math.radians(w)), cl[-1][1] + r_ * math.sin(math.radians(w))) for w in range(90, -91, -20)]
    if fold:
        # the lever in two: the root half up to the fold pin, and the tip on its own pin, turned back a little
        k = next(i for i, c in enumerate(cl) if c[0] >= fold)
        l1 = lft[2:k] + [(cl[k][0] + 4, cl[k][1] + cl[k][2]), (cl[k][0] + 4, cl[k][1] - cl[k][2])] + rgt[2:k][::-1] + root
        plate(f"clever{tag}", fr, [(piv[0] + t_, piv[1] + f_) for t_, f_ in l1], piv[2] - flat, piv[2] + flat, body, 0.6)
        l2 = [(cl[k][0] - 2, cl[k][1] + cl[k][2])] + lft[k:-1] + tip + rgt[k:-1][::-1] + [(cl[k][0] - 2, cl[k][1] - cl[k][2])]
        ang = math.radians(-6)
        cx, cf = cl[k][0], cl[k][1]
        rot = [(cx + (t_ - cx) * math.cos(ang) - (f_ - cf) * math.sin(ang), cf + (t_ - cx) * math.sin(ang) + (f_ - cf) * math.cos(ang)) for t_, f_ in l2]
        plate(f"clevertip{tag}", fr, [(piv[0] + t_, piv[1] + f_) for t_, f_ in rot], piv[2] - flat + 1, piv[2] + flat - 1, body, 0.6)
        D.bolt(f"cfoldpin{tag}", fr.P(piv[0] + cx, piv[1] + cf, piv[2] + flat), fr.dir(0, 0, 1), 3.4, 1.6, M["steel"], False)
    else:
        tf = lft[2:-1] + tip + rgt[2:-1][::-1] + root
        lev = plate(f"clever{tag}", fr, [(piv[0] + t_, piv[1] + f_) for t_, f_ in tf], piv[2] - flat, piv[2] + flat, body, 0.6)
        if holes:
            D.drill(lev, [(fr.P(piv[0] + t_, piv[1] + f_, piv[2] - 9), fr.P(piv[0] + t_, piv[1] + f_, piv[2] + 9), r_) for t_, f_, r_ in holes], 0)
    D.bolt(f"cpivot{tag}", fr.P(piv[0], piv[1], piv[2] + flat + 3), fr.dir(0, 0, 1), 6, 3.5, M["steel"], False)
    return piv


def round_lever(fr, tag, M, body):
    """A classic lever: a round-section blade, polished, curving to a ball end."""
    piv = (-70, 48, -14)
    pts = [(0, 0), (20, 6), (46, 14), (70, 16), (110, 12), (150, 6), (178, 3)]
    pts = smooth([(t, f) for t, f in pts], 3)
    P = [fr.P(piv[0] + t, piv[1] + f, piv[2]) for t, f in pts]
    R = [8.5 - 4.0 * min(1, i / (len(P) * 0.35)) for i in range(len(P))]
    BR.sweep(f"clever{tag}", P, R, body, 20)
    D.lathe(f"cleverball{tag}", fr.P(piv[0] + 178, piv[1] + 3, piv[2]), fr.dir(1, 0, 0), [(0, 0), (0, 5), (3, 7.6), (8, 7.6), (11, 5), (12.5, 0)], body, 24)
    D.lathe(f"cpivotboss{tag}", fr.P(piv[0], piv[1], piv[2] - 9), fr.dir(0, 0, 1), [(0, 0), (0, 12), (18, 12), (18, 0)], body, 28)
    D.bolt(f"cpivot{tag}", fr.P(piv[0], piv[1], piv[2] + 9), fr.dir(0, 0, 1), 5.5, 3, M["chrome"], False)
    return piv


STD_CL = [(0, 0, 11.5), (24, 7, 10.5), (50, 14, 9.5), (68, 16.5, 6.6), (92, 14.5, 5.6), (128, 8.5, 5.2), (162, 3, 4.9), (186, 1.5, 4.7)]
STD_HOLES = [(22, 6.6, 4.4), (37, 10.6, 3.7), (50.5, 13.8, 3.0)]


def blade_sweep(name, fr, pts, secs, material, m=16):
    """A lever blade along points (a, f, u) of the grip frame: secs gives (half thickness fore and aft,
    half height) at each point. The section stands upright, so from in front the blade shows its face."""
    pts, secs = smooth(pts, 2), smooth(secs, 2)
    verts, faces = [], []
    for i, ((a, f, u), (ht, hh)) in enumerate(zip(pts, secs)):
        pa, pb = pts[max(0, i - 1)], pts[min(len(pts) - 1, i + 1)]
        da, df = pb[0] - pa[0], pb[1] - pa[1]
        L_ = math.hypot(da, df) or 1.0
        na, nf = -df / L_, da / L_            # square to the blade in the bar's plane
        for k in range(m):
            w = 2 * math.pi * k / m
            cw, sw = math.cos(w), math.sin(w)
            ex = math.copysign(abs(cw) ** 0.55, cw) * ht
            ez = math.copysign(abs(sw) ** 0.55, sw) * hh
            verts.append(B.V(*fr.P(a + na * ex, f + nf * ex, u + ez)))
    for i in range(len(pts) - 1):
        for k in range(m):
            k2 = (k + 1) % m
            faces.append((i * m + k, i * m + k2, (i + 1) * m + k2, (i + 1) * m + k))
    faces += [tuple(range(m - 1, -1, -1)), tuple((len(pts) - 1) * m + k for k in range(m))]
    ob = B.mesh_obj(name, verts, faces, material, True, 0, False)
    B.bm_fix_normals(ob)
    return ob


# A lever as it really sits (beenster's photo, 16 Nov): the pivot block stands ahead of the perch at bar
# height; the lever leaves it with a short knuckle, drops and turns in towards the grip, then runs straight
# along the grip, a finger's gap in front of it and a little below its middle, and ends in a small kick.
# Points are (a along the bar, f forward, u up) from the inner end of the grip.
# Seen from above (beenster's plan view, 16 Nov) the lever is not straight: from the pivot it runs out and
# away from the grip, swings back in to a hump nearest the grip where the fingers pull, then eases away
# again to a small ball at the tip.
LEVER_PATH = [(-66, 46, -2), (-50, 54, -6), (-34, 60, -12), (-18, 63, -16), (0, 56, -18), (20, 43, -17), (36, 39, -17), (60, 43, -16),
              (90, 52, -15), (116, 59, -12)]
LEVER_SEC = [(5.5, 10.5), (5.0, 10.0), (4.8, 9.2), (4.5, 8.6), (4.2, 8.0), (4.0, 7.8), (3.8, 7.4), (3.6, 7.0), (3.5, 6.6), (3.4, 6.0)]


def pivot_block(fr, tag, M, body, dial=None):
    """The lever's root round the pivot bolt, and its span dial on top."""
    D.lathe(f"cleverroot{tag}", fr.P(-70, 48, -13), fr.dir(0, 0, 1), [(0, 0), (0, 11), (1, 12.5), (21, 12.5), (22, 11), (22, 0)], body, 28)
    D.bolt(f"cpivot{tag}", fr.P(-70, 48, 9), fr.dir(0, 0, 1), 5.5, 3, M["steel"], False)
    D.bolt(f"cpivotnut{tag}", fr.P(-70, 48, -13), fr.dir(0, 0, -1), 6, 3.5, M["steel"], False)
    if dial:
        # the span adjuster: a knurled wheel with numbered flats, between the root and the blade
        D.lathe(f"cleveradj{tag}", fr.P(-58, 52, -10), fr.dir(1, 0.25, 0), [(0, 0), (0, 9.5), (8, 9.5), (8, 0)], dial, 6)


def profile_lever(fr, tag, M, body, cut=None, fold=None, scale=1.0, groove=True):
    path = [p for p in LEVER_PATH if cut is None or p[0] <= cut]
    sec = [(a * scale, b * scale) for a, b in LEVER_SEC[:len(path)]]
    if cut is not None and path[-1][0] < cut:
        last = path[-1]
        path.append((cut, last[1] - 1, last[2] + 4))
        sec.append((sec[-1][0], sec[-1][1] * 0.9))
    if fold:
        k = next(i for i, p in enumerate(path) if p[0] >= fold)
        p0 = path[:k] + [(fold, path[k][1], path[k][2])]
        blade_sweep(f"clever{tag}", fr, p0, sec[:k] + [sec[k]], body)
        # the tip on its own pin, folded back a few degrees, with a gap at the joint
        p1 = [(fold + 3, path[k][1] - 0.5, path[k][2])] + [(a, f - (a - fold) * 0.08, u) for a, f, u in path[k:]]
        blade_sweep(f"clevertip{tag}", fr, p1, [sec[k]] + sec[k:], body)
        D.bolt(f"cfoldpin{tag}", fr.P(fold + 1, path[k][1], path[k][2] + sec[k][1]), fr.dir(0, 0, 1), 3.0, 1.4, M["steel"], False)
    else:
        blade_sweep(f"clever{tag}", fr, path, sec, body)
    if cut is None:
        # the ball at the tip
        a, f, u = path[-1]
        B.sphere(f"clevertip_ball{tag}", fr.P(a + 3, f + (2 if not fold else 2 - (a - fold) * 0.08), u), sec[-1][1] * 1.05, body)
    if groove:
        # a shallow groove along the front face of the finger section (a lighter machined line), square to the blade
        g = []
        for i, (a, f, u) in enumerate(path):
            if a < 0:
                continue
            pa, pb = path[max(0, i - 1)], path[min(len(path) - 1, i + 1)]
            da, df = pb[0] - pa[0], pb[1] - pa[1]
            L_ = math.hypot(da, df) or 1.0
            g.append((a - df / L_ * sec[i][0] * 0.97, f + da / L_ * sec[i][0] * 0.97, u))
        if len(g) > 1:
            blade_sweep(f"clevergroove{tag}", fr, g, [(0.3, 1.6 * scale)] * len(g), M["polished"], 8)


def master(fr, kind, M, body):
    """The brake master cylinder on the right perch."""
    if kind == "cup":
        B.cone("cmastercyl", fr.P(-70, 20, -4), fr.P(-70, 70, -10), 11.5, 10.5, body, 24)
        B.cone("cmasterneck", fr.P(-54, 34, 2), fr.P(-54, 34, 20), 5, 5, M["rubber"], 12)
        B.cone("cmasterres", fr.P(-54, 34, 18), fr.P(-54, 34, 46), 17.5, 18.5, M["cup"], 28)
        B.cone("cmasterrescap", fr.P(-54, 34, 46), fr.P(-54, 34, 54), 20, 19.5, M["plastic"], 28)
    elif kind == "block":
        B.cone("cmastercyl", fr.P(-70, 20, -4), fr.P(-70, 66, -10), 12.5, 11.5, body, 24)
        box = D.chaikin([(-10, 6), (30, 4), (34, 30), (-6, 34)], 2)
        side_plate("cmasterres", fr, box, -92, -48, body, 2.5)
        side_plate("cmasterglass", fr, [(f_ * 0.5 - 12, u * 0.45 + 9) for f_, u in box], -86, -54, M["smoke"], 0)
        for da in (-84, -56):
            D.bolt(f"cmasterlid{da}", fr.P(da, 12, 32), fr.dir(0, 0, 1), 3.2, 1.6)
    elif kind == "angled":
        B.cone("cmastercyl", fr.P(-70, 20, -4), fr.P(-70, 70, -10), 12, 11, body, 24)
        trap = D.chaikin([(-6, 8), (36, 8), (30, 34), (0, 38)], 2)
        side_plate("cmasterres", fr, trap, -90, -50, body, 2.5)
        side_plate("cmasterglass", fr, [(-6.6, 14), (-6.6, 30), (-6.4, 30), (-6.4, 14)], -84, -56, M["smoke"], 0)
        side_plate("cmasterlid", fr, [(-4, 38), (31, 34), (31, 36.5), (-4, 40.5)], -90, -50, M["dark"], 0.6)


def radial(fr, tag, M, body):
    """A radial master cylinder pointing forward, with a remote fluid pot on a short hose."""
    B.cone(f"cmastercyl{tag}", fr.P(-70, 16, -6), fr.P(-70, 92, -12), 12, 11, body, 24)
    B.cone(f"cmasterend{tag}", fr.P(-70, 92, -12), fr.P(-70, 98, -12), 9, 7, body, 24)
    B.smooth_tube(f"cmasterhose{tag}", [fr.P(-62, 40, 8), fr.P(-56, 46, 30), fr.P(-50, 36, 46)], 4.0, M["rubber"])
    B.cone(f"cmasterres{tag}", fr.P(-50, 36, 44), fr.P(-50, 36, 78), 15, 15, M["smoke"], 28)
    B.cone(f"cmasterrescap{tag}", fr.P(-50, 36, 78), fr.P(-50, 36, 86), 17, 16.5, M["gold"], 28)
    side_plate(f"cmasterbrkt{tag}", fr, [(26, 30), (44, 30), (44, 52), (26, 52)], -58, -55, body, 0.8)


def lever_set(fr, s, key, M):
    tag = s
    brake = s == -1
    if key == "classic":
        perch(fr, tag, M, M["polished"], wide=22)
        round_lever(fr, tag, M, M["polished"])
        if brake:
            B.cone("cmastercyl", fr.P(-70, 18, -4), fr.P(-70, 60, -8), 11, 10, M["polished"], 28)
            # the round pot: a short chrome drum standing on the perch with a domed, screwed lid
            D.lathe("cmasterres", fr.P(-60, 22, 10), fr.dir(0, 0, 1), [(0, 0), (0, 19), (2, 21), (22, 21), (24, 19.5), (24, 0)], M["chrome"], 40)
            D.lathe("cmasterrescap", fr.P(-60, 22, 34), fr.dir(0, 0, 1), [(0, 0), (0, 20.5), (2, 20.5), (5, 17), (7, 9), (7.5, 0)], M["chrome"], 40)
            for k in range(4):
                w = math.radians(45 + 90 * k)
                D.bolt(f"cmasterlid{k}", fr.P(-60 + 15 * math.cos(w), 22 + 15 * math.sin(w), 35.5), fr.dir(0, 0, 1), 2.4, 1.2, M["chrome"], False)
        else:
            B.cone("cclutchadj", fr.P(-84, 38, -6), fr.P(-88, 64, -10), 6.0, 6.0, M["chrome"], 12)
            B.cone("cclutchadjlock", fr.P(-85, 44, -7), fr.P(-86, 50, -8), 8.5, 8.5, M["chrome"], 6)
        return
    body = M["forged"] if key == "forged" else M["satin"]
    perch(fr, tag, M, body)
    if key == "standard":
        pivot_block(fr, tag, M, body, M["red"])
        profile_lever(fr, tag, M, body)
    elif key == "short":
        pivot_block(fr, tag, M, body, M["red"])
        profile_lever(fr, tag, M, body, cut=62)
    elif key == "folding":
        pivot_block(fr, tag, M, body, M["gold"])
        profile_lever(fr, tag, M, body, fold=56)
    elif key == "forged":
        pivot_block(fr, tag, M, body)
        profile_lever(fr, tag, M, body, scale=1.25, groove=False)
    else:
        raise KeyError(key)
    if key == "folding":
        radial(fr, tag, M, body)
    elif brake:
        master(fr, {"standard": "cup", "short": "block", "forged": "angled"}[key], M, body)
    else:
        B.cone("cclutchadj", fr.P(-84, 40, -6), fr.P(-90, 66, -10), 6.5, 6.5, M["steel"], 12)


# ---------------------------------------------------------------- switch pods
def face_plate(name, fr, outline, f0, f1, material, bevel=0.0):
    """A button or block cut to an outline (a, u) on the rider's face of a pod, between f0 and f1."""
    n = len(outline)
    verts = [B.V(*fr.P(a, f0, u)) for a, u in outline] + [B.V(*fr.P(a, f1, u)) for a, u in outline]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, 2 * n))] + [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    ob = B.mesh_obj(name, verts, faces, material, True, bevel)
    B.bm_fix_normals(ob)
    return ob


def rr(a, u, w, h, r=1.6, n=4):
    """A rounded rectangle centred at (a, u)."""
    pts = []
    for cx, cu, a0 in ((w / 2 - r, h / 2 - r, 0), (-w / 2 + r, h / 2 - r, 90), (-w / 2 + r, -h / 2 + r, 180), (w / 2 - r, -h / 2 + r, 270)):
        for k in range(n + 1):
            t = math.radians(a0 + 90 * k / n)
            pts.append((a + cx + r * math.cos(t), u + cu + r * math.sin(t)))
    return pts


def tri(a, u, size, ang):
    """A rounded triangle button pointing at ang degrees in the (a, u) plane."""
    pts = []
    for k in range(3):
        t = math.radians(ang + 120 * k)
        pts.append((a + size * math.cos(t), u + size * math.sin(t)))
    return D.chaikin(pts, 2)


def button(fr, name, outline, face, M, col, h=3.2, mark=None):
    """A button standing proud of the pod face (face = the pod's f there), with an optional light mark on it."""
    face_plate(name, fr, outline, face - h, face + 0.5, col, 0.6)
    if mark:
        face_plate(name + "mk", fr, mark, face - h - 0.25, face - h + 0.1, M["mark"], 0)


def pod_shell(fr, tag, M, a0, a1, prof):
    """The pod's housing (beenster, 16 Nov: less cube-like). prof gives its rear (rider's) face f, its
    bottom and top and its front; the face stays flat for the buttons and everything else is rounded:
    a full curve round the front and rounded ends along the bar."""
    fr_, ub = prof[0]
    ut = max(u for f, u in prof)
    ff = max(f for f, u in prof)
    um, fb = (ut + ub) / 2, fr_ + 9
    pts = [(fr_, ub + 4), (fr_, ut - 4)]
    for k in range(17):
        t = math.pi / 2 - math.pi * k / 16
        c_, s_ = math.cos(t), math.sin(t)
        pts.append((fb + (ff - fb) * math.copysign(abs(c_) ** 0.75, c_), um + (ut - um) * math.copysign(abs(s_) ** 0.75, s_)))
    prof = D.chaikin(pts, 2)
    cf, cu = (fr_ + ff) / 2, um
    # sections along the bar: full size in the middle, rounded in over the last 8 mm at each end
    r = 8.0
    secs = []
    for k in range(7):
        t = k / 6
        secs.append((a0 + r * (1 - math.cos(t * math.pi / 2)), 0.78 + 0.22 * math.sin(t * math.pi / 2)))
    secs = secs + [(a1 - (a - a0), k_) for a, k_ in reversed(secs)]
    m = len(prof)
    verts, faces = [], []
    for a, k_ in secs:
        verts += [B.V(*fr.P(a, cf + (f - cf) * k_, cu + (u - cu) * k_)) for f, u in prof]
    for i in range(len(secs) - 1):
        for j in range(m):
            j2 = (j + 1) % m
            faces.append((i * m + j, i * m + j2, (i + 1) * m + j2, (i + 1) * m + j))
    faces += [tuple(range(m - 1, -1, -1)), tuple((len(secs) - 1) * m + j for j in range(m))]
    ob = B.mesh_obj(f"cswitch{tag}", verts, faces, M["plastic"], True, 0, False)
    B.bm_fix_normals(ob)
    side_plate(f"cswitchsplit{tag}", fr, [(fr_ - 0.6, -0.6), (fr_ + 0.4, -0.6), (fr_ + 0.4, 0.6), (fr_ - 0.6, 0.6)], a0 + 8, a1 - 8, M["rubber"], 0)


def right_pod(fr, tag, M, a0=-48, a1=-6):
    """The right-hand pod on every set: a red kill switch on top, the starter button under it, and a mode
    button (beenster, 16 Nov: always a red kill switch on the right)."""
    face = -28
    pod_shell(fr, tag, M, a0, a1, [(-28, -24), (20, -24), (24, 16), (6, 26), (-28, 20)])
    mid = (a0 + a1) / 2
    # the kill switch: a red rocker across the top rear edge, with a white stripe
    side_plate("cswkill", fr, [(-30, 8), (-20, 8), (-17, 24), (-27, 26)], mid - 10, mid + 10, M["killred"], 1)
    face_plate("cswkillmk", fr, [(mid - 7, 20), (mid + 7, 20), (mid + 7, 21.5), (mid - 7, 21.5)], -27.3, -27.0, M["mark"], 0)
    # the starter: a big grey button with a ring
    button(fr, "cswstart", rr(mid, -6, 20, 16, 4), face, M, M["grey"], 3.6, rr(mid, -6, 8, 2.2, 0.8, 2))
    button(fr, "cswmode", rr(mid, -18, 14, 6, 2), face, M, M["dark"], 2.4)


def left_pod(fr, key, M):
    tag = 1
    if key == "dpad":
        # (photo 2) a tall pod with a four-way pad and a centre key in a 3 x 3 grid, horn and hazard below
        a0, a1, face = -60, -4, -32
        pod_shell(fr, tag, M, a0, a1, [(-32, -34), (22, -30), (26, 18), (8, 32), (-32, 30)])
        c, cu = -32.0, 4.0
        button(fr, "cswpad_up", tri(c, cu + 12, 6.5, 90), face, M, M["dark"], 3.4, tri(c, cu + 12, 2.2, 90))
        button(fr, "cswpad_dn", tri(c, cu - 12, 6.5, -90), face, M, M["dark"], 3.4, tri(c, cu - 12, 2.2, -90))
        button(fr, "cswpad_lt", tri(c - 13, cu, 6.0, 180), face, M, M["dark"], 3.4, tri(c - 13, cu, 2.0, 180))
        button(fr, "cswpad_rt", tri(c + 13, cu, 6.0, 0), face, M, M["dark"], 3.4, tri(c + 13, cu, 2.0, 0))
        button(fr, "cswpad_ok", rr(c, cu, 6, 8, 1.4), face, M, M["dark"], 2.6, rr(c, cu, 1.2, 4.5, 0.4, 1))
        for k, (da, du) in enumerate(((-15, 18), (15, 18), (-15, -10), (15, -10))):
            button(fr, f"cswkey{k}", rr(c + da, cu + du, 10, 8, 2), face, M, M["dark"], 2.8, rr(c + da, cu + du, 4, 1.4, 0.4, 1))
        button(fr, "cswhorn", rr(c - 13, -22, 14, 9, 2.5), face, M, M["grey"], 3.0)
        button(fr, "cswhazard", [(c + 13 + 6 * math.cos(2 * math.pi * k / 20), -22 + 6 * math.sin(2 * math.pi * k / 20)) for k in range(20)], face, M, M["killred"], 2.6, tri(c + 13, -22, 3, 90))
        # the indicator switch: a grey thumb button under the pod, pushed left or right
        side_plate("cswind", fr, [(-34, -34), (-22, -34), (-24, -40), (-34, -40)], c - 9, c + 9, M["grey"], 1)
    elif key == "cruise":
        # (photo 3) an angled pod with a column of mode keys, a small up/down rocker pair, horn and indicator
        # buttons along the bottom and a hazard button
        a0, a1, face = -62, -4, -30
        pod_shell(fr, tag, M, a0, a1, [(-30, -32), (22, -30), (26, 16), (10, 30), (-30, 26)])
        c = -33.0
        button(fr, "cswkey0", D.chaikin([(c - 20, 12), (c - 6, 18), (c - 6, 8), (c - 20, 4)], 1), face, M, M["grey"], 3.0, tri(c - 16, 11, 1.6, 90))
        button(fr, "cswkey1", D.chaikin([(c - 4, 18), (c + 8, 14), (c + 8, 6), (c - 4, 8)], 1), face, M, M["grey"], 3.0, tri(c + 4, 12, 1.6, 0))
        button(fr, "cswkey2", D.chaikin([(c - 20, 2), (c - 6, 6), (c - 6, -6), (c - 18, -8)], 1), face, M, M["grey"], 3.0, tri(c - 12, -1, 1.6, -90))
        button(fr, "cswset_up", rr(c + 18, 18, 12, 9, 2), face, M, M["grey"], 3.0, tri(c + 18, 18, 2, 90))
        button(fr, "cswset_mid", rr(c + 18, 7, 10, 8, 2), face, M, M["grey"], 2.6, rr(c + 18, 7, 3.5, 3.5, 1.5, 2))
        button(fr, "cswset_dn", rr(c + 18, -4, 12, 9, 2), face, M, M["grey"], 3.0, tri(c + 18, -4, 2, -90))
        button(fr, "cswreset", rr(c + 2, -12, 14, 7, 2.5), face, M, M["grey"], 2.6)
        button(fr, "cswind", rr(c - 6, -21, 12, 6, 2), face, M, M["grey"], 3.6)
        button(fr, "cswhorn", rr(c - 6, -28, 14, 5, 2), face, M, M["grey"], 3.0)
        button(fr, "cswhazard", rr(c + 16, -22, 10, 10, 2.5), face, M, M["dark"], 3.0, tri(c + 16, -22, 3, 90))
        face_plate("cswhazardmk", fr, tri(c + 16, -22, 2.4, 90), face - 3.4, face - 3.1, M["killred"], 0)
    elif key == "compact":
        # a small pod for classics and minimal bikes: a four-way pad, horn and indicator buttons
        a0, a1, face = -50, -6, -26
        pod_shell(fr, tag, M, a0, a1, [(-26, -24), (18, -24), (22, 14), (6, 26), (-26, 24)])
        c, cu = -28.0, 6.0
        for nm, (da, du, ang) in (("up", (0, 9, 90)), ("dn", (0, -9, -90)), ("lt", (-9, 0, 180)), ("rt", (9, 0, 0))):
            button(fr, f"cswpad_{nm}", tri(c + da, cu + du, 4.6, ang), face, M, M["dark"], 3.0, tri(c + da, cu + du, 1.6, ang))
        button(fr, "cswpad_ok", [(c + 3.4 * math.cos(2 * math.pi * k / 16), cu + 3.4 * math.sin(2 * math.pi * k / 16)) for k in range(16)], face, M, M["dark"], 2.4)
        button(fr, "cswhorn", rr(c - 9, -14, 12, 7, 2), face, M, M["grey"], 3.0)
        button(fr, "cswind", rr(c + 9, -14, 12, 7, 2), face, M, M["grey"], 3.4, [(c + 5, -14), (c + 7, -15.5), (c + 7, -12.5)])
    else:
        raise KeyError(key)


def switch_pod(fr, s, key, M):
    if s == -1:
        right_pod(fr, -1, M)
    else:
        left_pod(fr, key, M)


# ---------------------------------------------------------------- mirrors
def eye_point(model, hand):
    """Where the rider's eyes are: behind and above the hands, by the family's seat (a fixed guess per family,
    from the rider pose pictures: upright families sit taller and further back)."""
    fam = model["family"].lower()
    back, up = {"sport": (420, 330), "cafe": (440, 360), "cruiser": (470, 520), "adventure": (440, 470), "mx": (380, 440)}.get(fam, (440, 430))
    return (hand[0] - back, 0.0, hand[2] + up)


def head_outline(key, s):
    if key in ("round", "barend_round"):
        R = 48 if key == "round" else 40
        return [(R * math.cos(2 * math.pi * k / 48), R * math.sin(2 * math.pi * k / 48)) for k in range(48)]
    if key == "oval":
        return D.chaikin([(-86, -10), (-64, 30), (10, 44), (76, 32), (86, -4), (58, -36), (-52, -40)], 3)
    if key == "rect":
        return D.chaikin([(-78, -34), (-80, 30), (80, 36), (82, -30)], 3)
    if key == "teardrop":
        return D.chaikin([(-92, 4), (-40, 34), (30, 44), (70, 26), (78, -10), (40, -38), (-30, -24)], 3)
    if key == "barend_teardrop":
        return [(x * 0.78, z * 0.78) for x, z in head_outline("teardrop", s)]
    if key == "fairing_aero":
        return D.chaikin([(-84, -16), (-70, 26), (20, 38), (88, 24), (92, -14), (40, -32), (-50, -30)], 2)
    if key == "fairing_wing":
        return D.chaikin([(x * 1.2, z * 1.25) for x, z in [(-80, -6), (-60, 20), (60, 24), (90, 4), (70, -18), (-50, -16)]], 2)
    raise KeyError(key)


def mirror_head(name, centre, eye, s, key, M, back_mat, depth=26.0):
    """A mirror head at centre, its glass turned to show the road behind the rider from the eye point."""
    c = Vector(centre)
    to_eye = (Vector(eye) - c).normalized()
    behind = Vector((-1.0, 0.12 * s, 0.0)).normalized()        # the view we want: straight back, a little outboard
    n = (to_eye + behind).normalized()                         # the glass faces between the two
    side = Vector((0, 0, 1)).cross(n).normalized()
    up = n.cross(side).normalized()
    if side.y * s < 0:
        side = -side
    ol = head_outline(key, s)
    ol = [(x * s if key not in ("round", "barend_round") else x, z) for x, z in ol]
    back = [(1.0, 0), (1.0, depth * 0.3), (0.92, depth * 0.66), (0.7, depth * 0.95), (0.35, depth * 1.1)]
    verts, faces, m = [], [], len(ol)
    for k, dx in back:
        verts += [B.V(*(c - n * dx + side * (x * k) + up * (z * k))) for x, z in ol]
    for j in range(len(back) - 1):
        for i in range(m):
            i2 = (i + 1) % m
            faces.append((j * m + i, j * m + i2, (j + 1) * m + i2, (j + 1) * m + i))
    faces += [tuple(range(m - 1, -1, -1)), tuple((len(back) - 1) * m + i for i in range(m))]
    # the head and the glass are surfaces of their own, a side at a time (the game finds and turns each one)
    def own(mat, what):
        c_ = mat.copy()
        c_.name = f"{mat.name.split('.')[0].split('__')[0]}__{what}_{'l' if s > 0 else 'r'}"
        return c_
    ob = B.mesh_obj(name, verts, faces, own(back_mat, "mirror_head"), True, 0, False)
    B.bm_fix_normals(ob)
    gv = [B.V(*(c + n * 0.6 + side * (x * 0.93) + up * (z * 0.9))) for x, z in ol]
    B.mesh_obj(name + "glass", gv, [tuple(range(m))], own(M["glass"], "mirror_glass"), False)
    # the view this glass gives from the eye: the eye's ray reflected about the glass
    v = (c - Vector(eye)).normalized()
    r = v - 2 * v.dot(n) * n
    return dict(centre=tuple(c), normal=tuple(n), back=tuple(-n), reflect=tuple(r), reach=max(math.hypot(x, z) for x, z in ol), area=abs(sum(ol[i][0] * ol[(i + 1) % m][1] - ol[(i + 1) % m][0] * ol[i][1] for i in range(m)) / 2))


def ball_path(first, mc, bk, reach=60.0):
    """A stem's path: its first points from the root, then round behind the head and straight in to the ball joint
    D.BALL mm behind the middle of the glass (bk: the unit direction out of the back of the head; reach: how far
    the glass spreads from its middle). The game turns the head up to 20 degrees on that ball, which swings the
    glass's edge back: under the glass the stem keeps deep enough behind it to stay clear (0.42 mm deeper for
    each mm out from the ball, tan 22.5 degrees), so it comes round the back of the head like a real arm."""
    mc = Vector(mc)
    d = Vector(first[-1]) - mc
    d -= bk * d.dot(bk)                # across the back of the head, towards where the stem comes from
    far = d.length
    u = d / far if far > 1e-6 else Vector((0, 0, 0))
    edge = min(far, reach + 10.0)      # where the stem comes in under the glass's edge
    deep = lambda r: max(44.0, D.BALL + r * 0.42 + 16.0)
    inner = mc + bk * deep(edge * 0.45) + u * (edge * 0.45)
    if Vector(first[-1]).dot(bk) - mc.dot(bk) < deep(edge) - 6.0 or far > edge + 20.0:
        outer = mc + bk * deep(edge) + u * edge
    else:
        outer = (Vector(first[-1]) + inner) / 2          # (already deep and close: nothing to go round)
    return [tuple(q) for q in first] + [tuple(outer), tuple(inner), tuple(mc + bk * 44 + u * min(far, 4.0)), tuple(mc + bk * D.BALL)]


def W5(secs):
    """A strap's sections for a ball_path from two first points: one more section for the point that goes round."""
    return secs[:2] + [tuple((a + b) / 2 for a, b in zip(secs[1], secs[2]))] + secs[2:]


def mirror(fr, s, key, M, model, lines, hand):
    st = MIRRORS[key]
    eye = eye_point(model, hand)
    tag = s
    # Every stem ends on the head's ball joint, D.BALL mm behind the middle of the glass, coming in from behind the
    # head (ball_path): the game turns each head on that ball so the rider's own eyes see the road behind
    # (beenster, build 166), and a stem that came in from the side came out through the glass.
    if st["kind"] == "stalk":
        root = fr.P(-70, 4, 18)
        mc = Vector(fr.P(60, -2, 150))
        info = mirror_head(f"cmirror{tag}", mc, eye, s, key, M, M["chrome"] if key == "teardrop" else M["plastic"])
        bk = Vector(info["back"])
        if key == "rect":
            B.cone(f"cmirrorboss{tag}", root, fr.P(-70, 4, 32), 12, 9.5, M["satin"], 16)
            D.strap(f"cmstalk{tag}", ball_path([fr.P(-70, 4, 28), fr.P(-58, 16, 88)], mc, bk, info["reach"]), W5([(9, 9), (9, 7), (10, 6.5), (9, 6), (7, 5.5)]), M["plastic"])
        else:
            B.cone(f"cmirrorboss{tag}", root, fr.P(-70, 4, 28), 8, 7, M["satin"] if key != "teardrop" else M["chrome"], 6)
            stalk = M["chrome"] if key == "teardrop" else M["satin"]
            B.smooth_tube(f"cmstalk{tag}", ball_path([fr.P(-70, 4, 26), fr.P(-60, 12, 80)], mc, bk, info["reach"]), 5.2, stalk)
    elif st["kind"] == "barend":
        g0, d, E, H = lines
        dv = Vector(d)
        e_ = Vector(E) + dv * 4
        D.lathe(f"cmirrorboss{tag}", tuple(e_ - dv * 21), tuple(dv), [(0, 0), (0, 9), (21, 9), (21, 15.5), (24, 17.5), (37, 17.5), (40, 15), (40, 0)], M["dark"], 40)
        root = e_ + dv * 12
        if key == "barend_round":
            mc = root + Vector((-6, 26 * s, 86))
            info = mirror_head(f"cmirror{tag}", tuple(mc), eye, s, key, M, M["dark"], 22)
            B.smooth_tube(f"cmstalk{tag}", ball_path([tuple(root + Vector((0, 6 * s, 10))), tuple(root + Vector((0, 14 * s, 42)))], mc, Vector(info["back"]), info["reach"]), 5.6, M["dark"])
        else:
            # the teardrop head on a chrome arm that rises out and up from the bar end
            mc = root + Vector((-6, 44 * s, 80))
            info = mirror_head(f"cmirror{tag}", tuple(mc), eye, s, key, M, M["chrome"], 22)
            B.smooth_tube(f"cmstalk{tag}", ball_path([tuple(root + Vector((0, 6 * s, 10))), tuple(root + Vector((0, 18 * s, 40)))], mc, Vector(info["back"]), info["reach"]), 5.2, M["chrome"])
    else:
        cowl = next((v for v in model["body"].get("panels", []) if v.get("cowl")), None)
        if not cowl:
            return None
        cx0, cx1 = min(q[0] for q in cowl["pts"]), max(q[0] for q in cowl["pts"])
        cz1 = max(q[1] for q in cowl["pts"])
        hit = None
        for zz in (cz1 - 26, cz1 - 40, cz1 - 60):
            hit = D.side_hit(("cowl", "ear"), cx0 + (cx1 - cx0) * 0.42, zz, s)
            if hit:
                break
        if not hit:
            return None
        root = Vector((hit[0], hit[1] - s * 6, hit[2]))
        if key == "fairing_aero":
            mc = root + Vector((-52, 150 * s, 58))
            B.cone(f"cmirrorboss{tag}", tuple(root), tuple(root + Vector((0, 12 * s, 3))), 12, 9, M["plastic"], 16)
            info = mirror_head(f"cmirror{tag}", tuple(mc), eye, s, key, M, M["plastic"])
            D.strap(f"cmstalk{tag}", ball_path([tuple(root + Vector((0, 8 * s, 2))), tuple(root + Vector((-6, 46 * s, 18)))], mc, Vector(info["back"]), info["reach"]),
                    W5([(16, 6), (14, 5.5), (11, 5.5), (9, 5.5), (7, 5.5)]), M["plastic"])
        else:
            mc = root + Vector((-62, 122 * s, 26))
            info = mirror_head(f"cmirror{tag}", tuple(mc), eye, s, key, M, M["plastic"], 22)
            D.strap(f"cmstalk{tag}", ball_path([tuple(root + Vector((0, 4 * s, 0))), tuple(root + Vector((10, 24 * s, 5)))], mc, Vector(info["back"]), info["reach"]),
                    W5([(20, 6), (12, 5), (8, 5), (7, 5), (7, 5)]), M["plastic"])
    behind_glass(tag, info)
    info["eye"] = eye
    return info


def behind_glass(tag, info):
    """Nothing of the arm or its boss may stand nearer the rider than 3 mm behind the glass, anywhere under it
    (beenster, build 166: arms poking through the mirror heads; the wing mirror's wide arm came 24 mm through)."""
    glass = bpy.data.objects.get(f"cmirror{tag}glass")
    if not glass:
        return
    mc, n = Vector(info["centre"]) * B.MM, Vector(info["normal"]).normalized()
    reach = max((glass.matrix_world @ v.co - mc).length for v in glass.data.vertices) + 0.004
    for nm in (f"cmstalk{tag}", f"cmirrorboss{tag}"):
        ob = D.mesh_now(nm)
        if not ob:
            continue
        inv = ob.matrix_world.inverted()
        for vt in ob.data.vertices:
            w = ob.matrix_world @ vt.co
            dep = (w - mc).dot(n)
            if dep > -0.003 and ((w - mc) - n * dep).length < reach:
                vt.co = inv @ (w - n * (dep + 0.003))
        ob.data.update()


# ---------------------------------------------------------------- fitting
OLD = ("perch", "levpivot", "lever", "cut_lever", "mastercyl", "masterneck", "brakeres", "clutchadj", "switch", "swlights", "swind", "swhorn",
       "swkill", "swstart", "mirror", "mstalk")


def grip_lines(model, c, lines=None):
    """The grip line each side: from bars3d.fit when a library bar is on, else the bike's own."""
    if lines:
        return lines
    g = c["grip"]
    out = {}
    for s in (-1, 1):
        g0 = (g[0], s * 258.0, g[1])
        d = BR.vnorm((-6, s * 126.0, -4))
        out[s] = (g0, d, BR.vadd(g0, d, BR.GRIP_L), BR.vadd(g0, d, BR.HAND_IN))
    return out


def own_bar_lines(model, c):
    """The grip line each side read off the bike's OWN bar where the grip sits, not a guessed direction.
    Some bikes' own grips do not follow their bar (kazeda_sport: the bar sweeps back 6 degrees more than
    the grip, so its end shows through the grip; beenster, 16 Nov). Where they differ, the bike's grip and
    bar end are turned onto the bar, and the controls follow the same line."""
    import numpy as np
    guess = grip_lines(model, c)
    dg = bpy.context.evaluated_depsgraph_get()
    out, turned = {}, {}
    for s in (-1, 1):
        pts = []
        for nm in (f"bar{s}", "bar"):
            ob = bpy.data.objects.get(nm)
            if not ob:
                continue
            me = ob.evaluated_get(dg).to_mesh()
            pts += [tuple(ob.matrix_world @ v.co) for v in me.vertices]
            ob.evaluated_get(dg).to_mesh_clear()
        v = np.array(pts) / B.MM if pts else np.zeros((0, 3))
        cen = []
        for yy in range(262, 382, 8):
            sel = v[np.abs(v[:, 1] * s - yy) < 4] if len(v) else []
            if len(sel) > 6:
                cen.append(sel.mean(0))
        if len(cen) < 6:
            out[s] = guess[s]
            continue
        cen = np.array(cen)
        mid = cen.mean(0)
        d = np.linalg.svd(cen - mid)[2][0]
        d = d if d[1] * s > 0 else -d
        g0 = mid + d * ((s * 258.0 - mid[1]) / d[1])
        g0, d = tuple(float(x) for x in g0), tuple(float(x) for x in d)
        out[s] = (g0, d, BR.vadd(g0, d, BR.GRIP_L), BR.vadd(g0, d, BR.HAND_IN))
        # turn the bike's own grip and bar end onto that line
        od = Vector(guess[s][1])
        ang = math.degrees(od.angle(Vector(d)))
        turned[s] = round(ang, 1)
        if ang > 1.0:
            q = od.rotation_difference(Vector(d))
            m = (Matrix.Translation(Vector(g0) * B.MM) @ q.to_matrix().to_4x4() @ Matrix.Translation(-Vector(guess[s][0]) * B.MM))
            for ob in bpy.data.objects:
                if ob.name in (f"grip{s}", f"barend{s}") or ob.name.startswith(f"barendbolt{s}"):
                    ob.matrix_world = m @ ob.matrix_world
    print("OWN BAR", model.get("name", ""), turned, flush=True)
    return out


def fit(b, model, c, levers="standard", switches="dpad", mirrors="oval", lines=None):
    use(b)
    lines = grip_lines(model, c, lines)
    for ob in list(bpy.data.objects):
        if ob.name.startswith(OLD):
            bpy.data.objects.remove(ob, do_unlink=True)
    if MIRRORS[mirrors]["kind"] == "barend":
        for ob in list(bpy.data.objects):
            if ob.name.startswith(("barend", "barend_lib")):
                bpy.data.objects.remove(ob, do_unlink=True)
    M = mats()
    info = {}
    before = set(bpy.data.objects.keys())
    for s in (-1, 1):
        fr = Frame(lines[s][0], lines[s][1], s)
        lever_set(fr, s, levers, M)
        switch_pod(fr, s, switches, M)
        info[s] = mirror(fr, s, mirrors, M, model, lines[s], lines[s][3])
    made = [o for o in bpy.data.objects.keys() if o not in before]
    # can the rider see each glass? a ray from the eye to the middle of the glass must meet the mirror first
    dg = bpy.context.evaluated_depsgraph_get()
    for s, i in info.items():
        if not i:
            continue
        e, c_ = Vector(i["eye"]) * B.MM, Vector(i["centre"]) * B.MM
        hit, loc, nor, idx, ob, mw = bpy.context.scene.ray_cast(dg, e, (c_ - e).normalized(), distance=(c_ - e).length + 0.01)
        i["seen"] = bool(hit and ob and ob.name.startswith("cmirror"))
        i["blocked_by"] = None if i["seen"] else (ob.name if ob else "nothing")
    return info, made


def mirror_report(info):
    """How usable a mirror is: where its view points (degrees back, up/down, outwards), and the glass size."""
    out = {}
    for s, i in info.items():
        if not i:
            continue
        r = Vector(i["reflect"])
        back = math.degrees(math.atan2(math.hypot(r.y, r.z), -r.x)) if r.x < 0 else 180.0
        down = math.degrees(math.asin(max(-1, min(1, r.z))))
        out_ = math.degrees(math.atan2(r.y * s, -r.x))
        out[s] = dict(off_axis=round(back, 1), up=round(down, 1), outboard=round(out_, 1), glass_cm2=round(i["area"] / 100.0, 1),
                      seen=i.get("seen"), blocked_by=i.get("blocked_by"))
    return out


# ---------------------------------------------------------------- pictures and checks
def built(mn, levers, switches, mirrors, bar=None, grip="knurl", end="weighted"):
    import build3d
    use(build3d)
    BR.fresh()
    m = BR.models()[mn]
    build3d.DETAIL["on"] = True
    build3d.build(m)
    top, F, uf = BR.model_top(m)
    rake = math.radians(m["geometry"]["rake"])
    cc = dict(F=F, uf=uf, nb=(-math.cos(rake), -math.sin(rake)), fk=m["fork"], top=top, grip=next(s_["at"] for s_ in m["front_end"] if s_.get("circle") and s_.get("part") != "head"))
    lines = None
    if bar:
        lines, _ = BR.fit(build3d, m, cc, bar, grip, end)
    else:
        use(build3d)
        lines = own_bar_lines(m, cc)
    info, made = fit(build3d, m, cc, levers, switches, mirrors, lines)
    return build3d, m, cc, lines, info, made


def render_at(b3, path, loc, tgt, lens, res=(1300, 900), samples=40):
    sc = bpy.context.scene
    b3.studio(sc)
    sc.cycles.samples = samples
    sc.render.resolution_x, sc.render.resolution_y = res
    BR.shoot(sc, path, loc, tgt, lens)


# what each picture shows: (kind, key, bike, other parts) -> one close-up
SHOTS = [
    ("lever", "standard", "corvano_naked", dict(switches="dpad", mirrors="oval")),
    ("lever", "short", "kazeda_naked", dict(switches="cruise", mirrors="rect")),
    ("lever", "folding", "kazeda_sport", dict(switches="dpad", mirrors="fairing_aero")),
    ("lever", "classic", "hartwell_classic", dict(switches="compact", mirrors="teardrop")),
    ("lever", "forged", "rheinhart_naked", dict(switches="dpad", mirrors="rect")),
    ("switch", "dpad", "corvano_adventure", dict(levers="forged", mirrors="rect")),
    ("switch", "compact", "hartwell_classic", dict(levers="classic", mirrors="teardrop")),
    ("switch", "cruise", "lodestar_cruiser", dict(levers="standard", mirrors="teardrop")),
    ("mirror", "round", "corvano_naked", dict(levers="standard", switches="dpad")),
    ("mirror", "oval", "rheinhart_naked", dict(levers="forged", switches="dpad")),
    ("mirror", "rect", "corvano_adventure", dict(levers="forged", switches="dpad")),
    ("mirror", "teardrop", "lodestar_cruiser", dict(levers="classic", switches="compact")),
    ("mirror", "barend_round", "hartwell_cafe", dict(levers="classic", switches="compact")),
    ("mirror", "barend_teardrop", "kazeda_naked", dict(levers="short", switches="cruise")),
    ("mirror", "fairing_aero", "kazeda_sport", dict(levers="folding", switches="dpad")),
    ("mirror", "fairing_wing", "varga_sport", dict(levers="folding", switches="cruise")),
]


def shots(which=None, samples=40):
    os.makedirs(OUT, exist_ok=True)
    rep = {}
    for kind, key, mn, other in SHOTS:
        if which and key not in which:
            continue
        lv = key if kind == "lever" else other.get("levers", "standard")
        sw = key if kind == "switch" else other.get("switches", "dpad")
        mi = key if kind == "mirror" else other.get("mirrors", "oval")
        b3, m, cc, lines, info, made = built(mn, lv, sw, mi)
        rep[f"{kind}_{key}"] = dict(model=mn, mirror=mirror_report(info))
        H = lines[-1][3]
        if kind == "lever":
            # the brake side, close, from ahead, outside and above
            fr = Frame(lines[-1][0], lines[-1][1], -1)
            tgt = fr.P(-10, 30, -10)
            render_at(b3, os.path.join(OUT, f"lever_{key}.png"), BR.vadd(tgt, (300, -330, 250)), tgt, 50, samples=samples)
        elif kind == "switch":
            fr = Frame(lines[1][0], lines[1][1], 1)
            tgt = fr.P(-28, -16, 0)
            # the clutch side, from behind and above, where the thumb is
            render_at(b3, os.path.join(OUT, f"switch_{key}.png"), BR.vadd(tgt, (-210, 150, 160)), tgt, 60, samples=samples)
        else:
            tgt = (H[0] + 40, 0, H[2] + 40)
            render_at(b3, os.path.join(OUT, f"mirror_{key}.png"), BR.vadd(tgt, (900, -760, 420)), tgt, 36, samples=samples)
            # and what the rider sees: from the eye towards the left mirror
            if key in ("oval", "fairing_aero", "barend_round"):
                eye = info[1]["eye"] if info.get(1) else info[-1]["eye"]
                render_at(b3, os.path.join(OUT, f"mirror_{key}_eye.png"), eye, (H[0] + 30, 0, H[2] + 60), 24, samples=samples)
    path = os.path.join(OUT, "shots.json")
    old = json.load(open(path)) if os.path.exists(path) else {}
    old.update(rep)
    json.dump(old, open(path, "w"), indent=1)


def check():
    """Every mirror on the lead bike of each family: its view and whether anything new clips."""
    res = {}
    for fam, mn in BR.LEADS.items():
        for key, st in MIRRORS.items():
            if st["kind"] == "fairing" and fam != "Sport":
                continue
            b3, m, cc, lines, info, made = built(mn, "standard", "dpad", key)
            cl = BR.clip_check(made)
            bad = {k: v for k, v in cl.items() if not ok_pair(k)}
            res.setdefault(key, {})[fam] = dict(model=mn, mirror=mirror_report(info), clips=bad, fitted=bool(info.get(1)))
            print(f"MCHECK {key:14s} {fam:10s} {res[key][fam]}", flush=True)
    for key in LEVERS:
        for sw in SWITCHES:
            b3, m, cc, lines, info, made = built("corvano_naked", key, sw, "oval")
            cl = BR.clip_check(made)
            bad = {k: v for k, v in cl.items() if not ok_pair(k)}
            res.setdefault("sets", {})[f"{key}+{sw}"] = bad
            print(f"SCHECK {key}+{sw} {bad}", flush=True)
    os.makedirs(OUT, exist_ok=True)
    json.dump(res, open(os.path.join(OUT, "check.json"), "w"), indent=1)


OK = ("cperch | bar", "cperch | cmaster", "cperchbolt | cperch", "clever | cperch", "cpivot | clever", "cpivot | cperch", "cmaster | cperch", "cmaster | cmaster",
      "cswitch | bar", "csw | cswitch", "cmirrorboss | cperch", "cmstalk | cmirror", "cmstalk | cmirrorboss", "cmirror | cmirror", "cmirrorboss | bar",
      "cmirrorboss | barend", "cmirrorboss | grip", "cmstalk | cowl", "cmirrorboss | cowl", "cmstalk | ear", "cleveradj | clever", "cspan | clever",
      "cfoldpin | clever", "clevertip | clever", "cleverball | clever", "cpivotboss | clever", "cpivotboss | cperch", "cclutchadj | cperch",
      "cswitchscrew | cswitch", "cswtoggle | cswitch", "cswbutton | cswitch", "cmasterlid | cmaster", "cmasterglass | cmaster", "cperch | bclip",
      "cswitch | bclip", "cmasterhose | cmaster", "cmasterbrkt | cmaster", "cswitchsplit | cswitch", "cswbuttonring | cswitch", "cswbutton | cswbuttonring",
      "cmirrorboss | bar_lib", "cperch | bar_lib", "cswitch | bar_lib", "cswtogglebase | cswitch", "cswtoggle | cswtogglebase", "cperch | throttle",
      "cmaster | brakeline", "cclutchadj | clutchcable", "cperch | clutchcable", "cmirrorind | cmirror", "cmstalk | cmirrorind", "cmaster | throttle")


def ok_pair(pair):
    a, b_ = [x.strip() for x in pair.split("|")]
    for j in OK:
        ja, jb = [x.strip() for x in j.split("|")]
        if (a.startswith(ja) and b_.startswith(jb)) or (b_.startswith(ja) and a.startswith(jb)):
            return True
    return False


if __name__ == "__main__":
    a = sys.argv[1:]
    cmd = a[0] if a else "shots"
    smp = int(a[a.index("--samples") + 1]) if "--samples" in a else 40
    rest = [x for x in a[1:] if not x.startswith("--") and x != str(smp)]
    if cmd == "shots":
        shots(rest or None, smp)
    elif cmd == "check":
        check()
