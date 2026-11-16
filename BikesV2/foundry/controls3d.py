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
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bars3d as BR  # noqa: E402
import detail3d as D  # noqa: E402

B = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "controls")

LEVERS = {
    "standard": dict(label="Billet plate set", note="The signed-off naked set: plate-cut S-curve lever, drilled root, ring end, round fluid cup on a neck."),
    "short": dict(label="Short lever set", note="Stubby two-finger levers ending inboard of the grip end, block reservoir cast into the perch."),
    "folding": dict(label="Folding radial set", note="Adjustable levers whose tips fold back in a spill, radial master cylinders with remote fluid pots on both sides."),
    "classic": dict(label="Classic polished set", note="Round-section polished levers with ball ends, a chrome round pot on the brake perch and a cable clutch."),
    "forged": dict(label="Forged dog-leg set", note="Wide flat forged levers, an angled integral reservoir with a sight glass: modern naked and adventure."),
}
SWITCHES = {
    "modern": dict(label="Modern pod", note="The signed-off chunky two-part housing: lights rocker, indicator slider, horn; red kill rocker and starter."),
    "classic": dict(label="Classic pod", note="A smaller rounded housing with chrome toggle switches and a push button."),
    "minimal": dict(label="Minimal billet pod", note="A slim machined ring round the bar with two small buttons each side."),
}
MIRRORS = {
    "round": dict(label="Round stalk", kind="stalk", note="A round head on a slim stalk from the perch."),
    "oval": dict(label="Oval stalk", kind="stalk", note="The signed-off naked mirror: a wide oval head on a slim stalk."),
    "rect": dict(label="Rectangular stalk", kind="stalk", note="A squared head with rounded corners on a cast aero stem."),
    "teardrop": dict(label="Teardrop stalk", kind="stalk", note="A head that tapers to a point inboard, chrome back: classic and cruiser."),
    "barend_round": dict(label="Bar-end round", kind="barend", note="A round head on a short stem from a clamp on the bar end."),
    "barend_blade": dict(label="Bar-end blade", kind="barend", note="A slim wide blade hung below the bar end: cafe and naked."),
    "fairing_aero": dict(label="Fairing aero", kind="fairing", note="A wide head on an aero arm from the fairing shoulder, with an indicator strip."),
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


def lever_set(fr, s, key, M):
    tag = s
    brake = s == -1
    if key == "standard":
        perch(fr, tag, M, M["satin"])
        piv = plate_lever(fr, tag, M, STD_CL, STD_HOLES + [(192, 2.0, 3.8)], M["satin"])
        B.cone(f"cleveradj{tag}", fr.P(piv[0] + 7 * 1, piv[1] + 1.5, piv[2] + 5.5), fr.P(piv[0] + 7, piv[1] + 1.5, piv[2] + 11), 8.2, 8.2, M["red"], 14)
        if brake:
            B.cone("cmastercyl", fr.P(-70, 20, -4), fr.P(-70, 70, -10), 11.5, 10.5, M["satin"], 24)
            B.cone("cmasterneck", fr.P(-54, 34, 2), fr.P(-54, 34, 20), 5, 5, M["rubber"], 12)
            B.cone("cmasterres", fr.P(-54, 34, 18), fr.P(-54, 34, 46), 17.5, 18.5, M["cup"], 28)
            B.cone("cmasterrescap", fr.P(-54, 34, 46), fr.P(-54, 34, 54), 20, 19.5, M["plastic"], 28)
        else:
            B.cone("cclutchadj", fr.P(-84, 40, -6), fr.P(-90, 66, -10), 6.5, 6.5, M["steel"], 12)
    elif key == "short":
        perch(fr, tag, M, M["satin"])
        cl = [(0, 0, 11.5), (22, 7, 10.5), (44, 13, 9.5), (62, 15, 7.6), (90, 12, 7.0), (118, 7, 6.6)]
        piv = plate_lever(fr, tag, M, cl, [], M["satin"], ring=False)
        # two long slots on the finger section, cut from the top
        lv = bpy.data.objects[f"clever{tag}"]
        D.drill(lv, [(fr.P(piv[0] + t, piv[1] + f, piv[2] - 9), fr.P(piv[0] + t, piv[1] + f, piv[2] + 9), 2.6) for t, f in ((80, 13.5), (86, 13), (92, 12.6), (98, 11.5))], 0)
        if brake:
            # a squared reservoir cast into the top of the master cylinder, with a smoked sight glass facing back
            B.cone("cmastercyl", fr.P(-70, 20, -4), fr.P(-70, 66, -10), 12.5, 11.5, M["satin"], 24)
            box = D.chaikin([(-10, 6), (30, 4), (34, 30), (-6, 34)], 2)
            side_plate("cmasterres", fr, box, -92, -48, M["satin"], 2.5)
            side_plate("cmasterglass", fr, [(f_ * 0.5 - 12, u * 0.45 + 9) for f_, u in box], -86, -54, M["smoke"], 0)
            for da in (-84, -56):
                D.bolt(f"cmasterlid{da}", fr.P(da, 12, 32), fr.dir(0, 0, 1), 3.2, 1.6)
        else:
            B.cone("cclutchadj", fr.P(-84, 40, -6), fr.P(-90, 62, -10), 6.5, 6.5, M["steel"], 12)
    elif key == "folding":
        perch(fr, tag, M, M["satin"])
        piv = plate_lever(fr, tag, M, STD_CL[:-1] + [(176, 2, 4.8)], [], M["satin"], ring=False, fold=118)
        # the span dial: a gold wheel with six numbered flats
        D.lathe(f"cspan{tag}", fr.P(piv[0], piv[1] + 2, piv[2] + 5.5), fr.dir(0, 0, 1), [(0, 0), (0, 9.5), (6, 9.5), (6, 0)], M["gold"], 6)
        # radial master cylinder pointing forward, with a remote fluid pot on a short hose, on both sides
        B.cone(f"cmastercyl{tag}", fr.P(-70, 16, -6), fr.P(-70, 92, -12), 12, 11, M["satin"], 24)
        B.cone(f"cmasterend{tag}", fr.P(-70, 92, -12), fr.P(-70, 98, -12), 9, 7, M["satin"], 24)
        B.smooth_tube(f"cmasterhose{tag}", [fr.P(-62, 40, 8), fr.P(-56, 46, 30), fr.P(-50, 36, 46)], 4.0, M["rubber"])
        B.cone(f"cmasterres{tag}", fr.P(-50, 36, 44), fr.P(-50, 36, 78), 15, 15, M["smoke"], 28)
        B.cone(f"cmasterrescap{tag}", fr.P(-50, 36, 78), fr.P(-50, 36, 86), 17, 16.5, M["gold"], 28)
        side_plate(f"cmasterbrkt{tag}", fr, [(26, 30), (44, 30), (44, 52), (26, 52)], -58, -55, M["satin"], 0.8)
    elif key == "classic":
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
    elif key == "forged":
        perch(fr, tag, M, M["forged"])
        cl = [(0, 0, 12.5), (22, 9, 11.5), (46, 18, 10.5), (64, 20, 9.4), (100, 16, 9.0), (140, 9, 8.6), (172, 5, 8.2)]
        plate_lever(fr, tag, M, cl, [], M["forged"], ring=False, flat=4.5)
        if brake:
            B.cone("cmastercyl", fr.P(-70, 20, -4), fr.P(-70, 70, -10), 12, 11, M["forged"], 24)
            # an angled reservoir cast on the perch, leaning back towards the rider, with a sight glass
            trap = D.chaikin([(-6, 8), (36, 8), (30, 34), (0, 38)], 2)
            side_plate("cmasterres", fr, trap, -90, -50, M["forged"], 2.5)
            side_plate("cmasterglass", fr, [(-6.6, 14), (-6.6, 30), (-6.4, 30), (-6.4, 14)], -84, -56, M["smoke"], 0)
            side_plate("cmasterlid", fr, [(-4, 38), (31, 34), (31, 36.5), (-4, 40.5)], -90, -50, M["dark"], 0.6)
        else:
            B.cone("cclutchadj", fr.P(-84, 40, -6), fr.P(-90, 66, -10), 6.5, 6.5, M["steel"], 12)
    else:
        raise KeyError(key)


# ---------------------------------------------------------------- switch pods
def switch_pod(fr, s, key, M):
    tag = s
    if key == "modern":
        box = D.chaikin([(-26, -21), (22, -23), (25, 18), (-9, 25), (-27, 14)], 2)
        side_plate(f"cswitch{tag}", fr, box, -52, -4, M["plastic"], 3.5)
        side_plate(f"cswitchsplit{tag}", fr, [(-27.6, 0), (-26.6, 0), (-26.6, 2), (-27.6, 2)], -52, -4, M["rubber"], 0)
        if s == 1:
            side_plate("cswlights", fr, [(-31, 6), (-26, 6), (-26, 16), (-32, 15)], -37, -19, M["plastic"], 1)
            side_plate("cswind", fr, [(-32, -6), (-26, -6), (-26, 2), (-32, 2)], -34, -22, M["grey"], 1)
            B.cone("cswhorn", fr.P(-28, -25, -14), fr.P(-28, -31, -15), 5.5, 5, M["plastic"], 16)
        else:
            side_plate("cswkill", fr, [(-32, 5), (-26, 5), (-26, 16), (-33, 14)], -36, -20, M["killred"], 1)
            B.cone("cswstart", fr.P(-28, -25, -10), fr.P(-28, -31, -11), 6, 5.5, M["grey"], 16)
    elif key == "classic":
        # a rounded shell, smaller, with chrome toggles standing out of its back face
        D.lathe(f"cswitch{tag}", fr.P(-46, 0, 0), fr.dir(1, 0, 0), [(0, 11), (0, 19), (3, 22), (33, 22), (36, 19), (36, 11)], M["plastic"], 40, ell=(1.18, 1.0))
        B.cone(f"cswitchscrew{tag}", fr.P(-28, 0, -23), fr.P(-28, 0, -26), 3, 3, M["chrome"], 12)
        for k, (da, du) in enumerate(((-38, 8), (-24, 8), (-31, -8))):
            if k < 2:
                B.cone(f"cswtoggle{tag}{k}", fr.P(da, -24, du), fr.P(da, -38, du + 4), 2.2, 1.6, M["chrome"], 10)
                B.cone(f"cswtogglebase{tag}{k}", fr.P(da, -21, du), fr.P(da, -25, du), 5, 4.5, M["chrome"], 16)
            else:
                B.cone(f"cswbutton{tag}", fr.P(da, -21, du), fr.P(da, -28, du), 6, 5.6, M["killred"] if s == -1 else M["grey"], 16)
    elif key == "minimal":
        # a billet ring with two buttons facing the thumb
        D.lathe(f"cswitch{tag}", fr.P(-34, 0, 0), fr.dir(1, 0, 0), [(0, 11), (0, 16.5), (1.5, 18), (20.5, 18), (22, 16.5), (22, 11)], M["satin"], 40)
        for k, du in enumerate((6, -6)):
            col = (M["killred"] if s == -1 else M["amber"]) if k == 0 else M["grey"]
            B.cone(f"cswbutton{tag}{k}", fr.P(-23, -15, du), fr.P(-23, -21, du * 1.15), 4.4, 4.2, col, 16)
            B.cone(f"cswbuttonring{tag}{k}", fr.P(-23, -14.5, du), fr.P(-23, -17.5, du * 1.05), 5.6, 5.6, M["polished"], 16)
    else:
        raise KeyError(key)


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
    if key == "barend_blade":
        return D.chaikin([(-70, -14), (-74, 12), (60, 22), (84, 6), (80, -16)], 3)
    if key == "fairing_aero":
        return D.chaikin([(-84, -16), (-70, 26), (20, 38), (88, 24), (92, -14), (40, -32), (-50, -30)], 2)
    if key == "fairing_wing":
        return D.chaikin([(-80, -6), (-60, 20), (60, 24), (90, 4), (70, -18), (-50, -16)], 2)
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
    ob = B.mesh_obj(name, verts, faces, back_mat, True, 0, False)
    B.bm_fix_normals(ob)
    gv = [B.V(*(c + n * 0.6 + side * (x * 0.93) + up * (z * 0.9))) for x, z in ol]
    B.mesh_obj(name + "glass", gv, [tuple(range(m))], M["glass"], False)
    # the view this glass gives from the eye: the eye's ray reflected about the glass
    v = (c - Vector(eye)).normalized()
    r = v - 2 * v.dot(n) * n
    return dict(centre=tuple(c), normal=tuple(n), back=tuple(-n), reflect=tuple(r), area=abs(sum(ol[i][0] * ol[(i + 1) % m][1] - ol[(i + 1) % m][0] * ol[i][1] for i in range(m)) / 2))


def mirror(fr, s, key, M, model, lines, hand):
    st = MIRRORS[key]
    eye = eye_point(model, hand)
    tag = s
    if st["kind"] == "stalk":
        root = fr.P(-70, 4, 18)
        mc = Vector(fr.P(60, -2, 150))
        if key == "rect":
            B.cone(f"cmirrorboss{tag}", root, fr.P(-70, 4, 32), 12, 9.5, M["satin"], 16)
            D.strap(f"cmstalk{tag}", [fr.P(-70, 4, 28), fr.P(-58, 16, 88), fr.P(10, 28, 118), fr.P(40, 18, 138)], [(9, 9), (9, 7), (11, 6.5), (16, 7.5)], M["plastic"])
        else:
            B.cone(f"cmirrorboss{tag}", root, fr.P(-70, 4, 28), 8, 7, M["satin"] if key != "teardrop" else M["chrome"], 6)
            stalk = M["chrome"] if key == "teardrop" else M["satin"]
            B.smooth_tube(f"cmstalk{tag}", [fr.P(-70, 4, 26), fr.P(-60, 12, 80), fr.P(18, 20, 120), fr.P(40, 18, 140)], 5.2, stalk)
        info = mirror_head(f"cmirror{tag}", mc, eye, s, key, M, M["chrome"] if key == "teardrop" else M["plastic"])
    elif st["kind"] == "barend":
        g0, d, E, H = lines
        dv = Vector(d)
        e_ = Vector(E) + dv * 4
        D.lathe(f"cmirrorboss{tag}", tuple(e_ - dv * 21), tuple(dv), [(0, 0), (0, 9), (21, 9), (21, 15.5), (24, 17.5), (37, 17.5), (40, 15), (40, 0)], M["dark"], 40)
        if key == "barend_round":
            root = e_ + dv * 12
            mc = root + Vector((-6, 26 * s, 86))
            B.smooth_tube(f"cmstalk{tag}", [tuple(root + Vector((0, 6 * s, 10))), tuple(root + Vector((0, 14 * s, 42))), tuple(mc + Vector((14, -6 * s, -36))), tuple(mc + Vector((14, 0, -4)))], 5.6, M["dark"])
        else:
            # the blade hangs below the bar end on a short drop arm
            root = e_ + dv * 12
            mc = root + Vector((-10, 64 * s, -34))
            D.strap(f"cmstalk{tag}", [tuple(root + Vector((0, 4 * s, -6))), tuple(root + Vector((-4, 26 * s, -22))), tuple(mc + Vector((12, -40 * s, 0)))], [(7, 5), (7, 4.5), (9, 5)], M["dark"])
        info = mirror_head(f"cmirror{tag}", tuple(mc), eye, s, key, M, M["dark"], 18 if key == "barend_blade" else 22)
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
            mc = root + Vector((-34, 150 * s, 58))
            B.cone(f"cmirrorboss{tag}", tuple(root), tuple(root + Vector((0, 12 * s, 3))), 12, 9, M["plastic"], 16)
            D.strap(f"cmstalk{tag}", [tuple(root + Vector((0, 8 * s, 2))), tuple(root + Vector((-8, 48 * s, 22))), tuple(mc + Vector((16, -52 * s, -12))), tuple(mc + Vector((14, -8 * s, -4)))],
                    [(16, 6), (14, 5.5), (15, 5.5), (18, 6)], M["plastic"])
            info = mirror_head(f"cmirror{tag}", tuple(mc), eye, s, key, M, M["plastic"])
            # an indicator strip along the head's front lower edge
            n = Vector(info["normal"])
            side = Vector((0, 0, 1)).cross(n).normalized()
            B.smooth_tube(f"cmirrorind{tag}", [tuple(mc - n * 22 + side * (-60) + Vector((0, 0, -22))), tuple(mc - n * 26 + Vector((0, 0, -26))), tuple(mc - n * 22 + side * 60 + Vector((0, 0, -20)))], 3.2, M["amber"])
        else:
            mc = root + Vector((-14, 96 * s, 22))
            D.strap(f"cmstalk{tag}", [tuple(root + Vector((0, 4 * s, 0))), tuple(root + Vector((-6, 40 * s, 10))), tuple(mc + Vector((10, -30 * s, -4)))], [(26, 6), (22, 5), (18, 5)], M["plastic"])
            info = mirror_head(f"cmirror{tag}", tuple(mc), eye, s, key, M, M["plastic"], 22)
    info["eye"] = eye
    return info


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


def fit(b, model, c, levers="standard", switches="modern", mirrors="oval", lines=None):
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
    info, made = fit(build3d, m, cc, levers, switches, mirrors, lines)
    return build3d, m, cc, grip_lines(m, cc, lines), info, made


def render_at(b3, path, loc, tgt, lens, res=(1300, 900), samples=40):
    sc = bpy.context.scene
    b3.studio(sc)
    sc.cycles.samples = samples
    sc.render.resolution_x, sc.render.resolution_y = res
    BR.shoot(sc, path, loc, tgt, lens)


# what each picture shows: (kind, key, bike, other parts) -> one close-up
SHOTS = [
    ("lever", "standard", "corvano_naked", dict(switches="modern", mirrors="oval")),
    ("lever", "short", "kazeda_naked", dict(switches="minimal", mirrors="rect")),
    ("lever", "folding", "kazeda_sport", dict(switches="modern", mirrors="fairing_aero")),
    ("lever", "classic", "hartwell_classic", dict(switches="classic", mirrors="teardrop")),
    ("lever", "forged", "rheinhart_naked", dict(switches="modern", mirrors="rect")),
    ("switch", "modern", "corvano_naked", dict(levers="standard", mirrors="oval")),
    ("switch", "classic", "hartwell_classic", dict(levers="classic", mirrors="teardrop")),
    ("switch", "minimal", "rheinhart_naked", dict(levers="short", mirrors="barend_blade")),
    ("mirror", "round", "corvano_naked", dict(levers="standard", switches="modern")),
    ("mirror", "oval", "rheinhart_naked", dict(levers="forged", switches="modern")),
    ("mirror", "rect", "corvano_adventure", dict(levers="forged", switches="modern")),
    ("mirror", "teardrop", "lodestar_cruiser", dict(levers="classic", switches="classic")),
    ("mirror", "barend_round", "hartwell_cafe", dict(levers="classic", switches="classic")),
    ("mirror", "barend_blade", "kazeda_naked", dict(levers="short", switches="minimal")),
    ("mirror", "fairing_aero", "kazeda_sport", dict(levers="folding", switches="modern")),
    ("mirror", "fairing_wing", "varga_sport", dict(levers="folding", switches="minimal")),
]


def shots(which=None, samples=40):
    os.makedirs(OUT, exist_ok=True)
    rep = {}
    for kind, key, mn, other in SHOTS:
        if which and key not in which:
            continue
        lv = key if kind == "lever" else other.get("levers", "standard")
        sw = key if kind == "switch" else other.get("switches", "modern")
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
            b3, m, cc, lines, info, made = built(mn, "standard", "modern", key)
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
