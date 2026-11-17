"""Bike Models V2: the cast and forged wheel library (beenster, 15 Nov).

A wheel is three things: a rim section turned about the axle (flanges, bead seats, drop well, and the ribs
and steps on the side you see), a hub, and a spoke pattern. A spoke pattern is a few bars drawn once in
polar coordinates (how far out, what angle, how wide, how thick) and repeated round the wheel, so a new
style is a short recipe. Every style is built at a front and a rear size.

Our own designs, drawn with real wheels as reference only: no maker's name, logo or exact pattern ships.

    python3 foundry/wheels3d.py                 # every style: out/wheels/<style>.png and out/wheels/sheet.png
    python3 foundry/wheels3d.py sweep7 web10    # only these
"""
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import detail3d as D

B = None


def use(b):
    """Point the library (and the detail helpers) at the build3d module in use."""
    global B
    B = b
    D.B = b


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "wheels")
INCH = 25.4

FINISH = {
    "gloss_black": dict(col=(0.012, 0.012, 0.014), metallic=0.0, rough=0.2, coat=1.0),
    "matt_black": dict(col=(0.022, 0.022, 0.024), metallic=0.0, rough=0.6, coat=0.0),
    "gold": dict(col=(0.62, 0.42, 0.13), metallic=1.0, rough=0.3, coat=0.5),
    "anodised_gold": dict(col=(0.75, 0.46, 0.05), metallic=1.0, rough=0.18, coat=0.2),
    "chrome": dict(col=(0.86, 0.86, 0.88), metallic=1.0, rough=0.07, coat=0.0),
    "steel_brushed": dict(col=(0.56, 0.57, 0.59), metallic=1.0, rough=0.3, coat=0.0),
    "gold_bright": dict(col=(0.95, 0.66, 0.2), metallic=1.0, rough=0.09, coat=0.0),
    "brass": dict(col=(0.78, 0.6, 0.27), metallic=1.0, rough=0.2, coat=0.0),
    "gunmetal": dict(col=(0.12, 0.125, 0.13), metallic=0.9, rough=0.36, coat=0.3),
    "carbon": dict(col=(0.02, 0.02, 0.023), metallic=0.0, rough=0.3, coat=1.0),
    "bronze": dict(col=(0.3, 0.19, 0.09), metallic=1.0, rough=0.34, coat=0.3),
}


def finish(name):
    f = FINISH[name]
    return D.surf("wheel_" + name, f["col"], f["metallic"], f["rough"], coat=f["coat"], wear=0.05)


# A spoke pattern: "n" repeats of "bars". A bar is a list of points (f, angle, w, t):
#   f      0 at the hub, 1 at the rim
#   angle  degrees round the wheel from the bar's own spoke line
#   w      half width in the plane of the wheel, mm
#   t      half thickness along the axle, as a share of the hub's (1) tapering to the rim's (about 0.5)
# "curve" rounds the bar's corners; "inlay" lays a machined bright strip on both faces.
STYLES = {
    "sweep7": dict(label="Seven swept spokes", ref=1, flare=1.5, n=7, finish="gloss_black", rim=1, hub="star", sizes=((17, 3.5), (17, 6.0)), bars=[
        dict(curve=True, pts=[(0, -6, 17, 1.0), (0.3, 4, 13, 0.9), (0.65, 20, 9.5, 0.7), (1, 37, 8, 0.55)])]),
    "y5": dict(label="Five Y spokes", ref=2, n=5, finish="gunmetal", rim=2, hub="flange", sizes=((17, 3.5), (17, 5.5)), bars=[
        dict(pts=[(0, 0, 15, 1.0), (0.42, 0, 11, 0.85)]),
        dict(curve=True, pts=[(0.36, 0, 9, 0.85), (0.5, 3, 8, 0.75), (1, 15, 6.5, 0.5)]),
        dict(curve=True, pts=[(0.36, 0, 9, 0.85), (0.5, -3, 8, 0.75), (1, -15, 6.5, 0.5)])]),
    "hex6": dict(label="Six hex web, machined face", ref=3, n=6, finish="gloss_black", rim=2, hub="flange", sizes=((19, 3.0), (17, 6.0)), bars=[
        dict(inlay=False, pts=[(0, 0, 20, 1.0), (0.38, 0, 15, 0.9)]),
        dict(curve=True, inlay=True, pts=[(0.3, 0, 12, 0.9), (0.5, 8, 9, 0.8), (0.72, 24, 8, 0.65), (1, 26, 9, 0.55)]),
        dict(curve=True, inlay=True, pts=[(0.3, 0, 12, 0.9), (0.5, -8, 9, 0.8), (0.72, -24, 8, 0.65), (1, -26, 9, 0.55)])]),
    "ten": dict(label="Ten spokes in five pairs", ref=4, n=5, finish="matt_black", rim=2, hub="drum", sizes=((17, 3.5), (17, 5.5)), bars=[
        dict(pts=[(0, 9, 8, 1.0), (0.5, 12, 6.5, 0.75), (1, 15, 6, 0.5)]),
        dict(pts=[(0, -9, 8, 1.0), (0.5, -12, 6.5, 0.75), (1, -15, 6, 0.5)])]),
    "turbine10": dict(label="Ten turbine blades", ref=5, flare=1.7, n=10, finish="chrome", rim=0, hub="slim", sizes=((21, 2.5), (18, 5.5)), bars=[
        dict(curve=True, pts=[(0, -10, 12, 1.0), (0.25, 2, 13, 0.9), (0.6, 28, 9, 0.7), (0.85, 48, 6, 0.55), (1, 58, 5, 0.5)])]),
    "five_inlay": dict(label="Five straight spokes, bright inlay", ref=6, n=5, finish="gloss_black", rim=0, hub="slim", sizes=((21, 2.5), (18, 5.5)), bars=[
        dict(inlay=True, pts=[(0, 0, 13, 1.0), (0.5, 0, 19, 0.8), (1, 0, 26, 0.6)])]),
    "sickle5": dict(label="Five sickle spokes", ref=7, flare=1.2, n=5, finish="chrome", rim=0, hub="tall", sizes=((21, 2.5), (18, 5.5)), bars=[
        dict(curve=True, pts=[(0, -14, 20, 1.0), (0.3, 0, 24, 0.9), (0.6, 26, 17, 0.7), (0.85, 48, 10, 0.55), (1, 52, 9, 0.5)])]),
    "twin5": dict(label="Five fine twin spokes", ref=8, wk=1.0, n=5, finish="carbon", rim=1, hub="star", sizes=((17, 3.5), (17, 6.0)), bars=[
        dict(pts=[(0, 14, 5.5, 1.0), (1, 5, 4.5, 0.5)]),
        dict(pts=[(0, -14, 5.5, 1.0), (1, -5, 4.5, 0.5)])]),
    "web10": dict(label="Petal web", ref=9, wk=1.0, n=10, finish="gloss_black", rim=2, hub="slim", sizes=((19, 3.0), (17, 6.0)), bars=[
        dict(pts=[(0, 0, 5, 1.0), (0.4, 0, 4.5, 0.85)]),
        dict(pts=[(0.4, 0, 4.5, 0.85), (0.68, 18, 4.5, 0.7)]),
        dict(pts=[(0.4, 0, 4.5, 0.85), (0.68, -18, 4.5, 0.7)]),
        dict(pts=[(0.68, 18, 4.5, 0.7), (0.86, 18, 4.5, 0.6)]),
        dict(pts=[(0.86, 18, 4.5, 0.6), (1, 12.5, 4.5, 0.5)]),
        dict(pts=[(0.86, 18, 4.5, 0.6), (1, 23.5, 4.5, 0.5)])]),
    "blade7": dict(label="Seven straight blades", ref=10, n=7, finish="carbon", rim=1, hub="star", sizes=((17, 3.5), (17, 6.0)), bars=[
        dict(pts=[(0, 0, 19, 1.0), (0.3, 0, 13, 0.85), (1, 0, 9.5, 0.5)])]),
    "offset5": dict(label="Five offset twin spokes", ref=0, n=5, finish="gloss_black", rim=1, hub="flange", sizes=((17, 3.5), (17, 5.5)), bars=[
        dict(pts=[(0, -30, 7, 1.0), (1, 4, 6, 0.5)]),
        dict(pts=[(0, -8, 7, 1.0), (1, 14, 6, 0.5)])]),
    "split6": dict(label="Six split spokes", ref=0, n=6, finish="bronze", rim=1, hub="flange", sizes=((17, 3.5), (17, 5.5)), bars=[
        dict(pts=[(0, 5, 8, 1.0), (0.5, 4, 6.5, 0.75), (1, 9, 6, 0.5)]),
        dict(pts=[(0, -5, 8, 1.0), (0.5, -4, 6.5, 0.75), (1, -9, 6, 0.5)]),
        dict(pts=[(0.48, 4, 5, 0.5), (0.48, -4, 5, 0.5)])]),
    # billet chopper wheels (beenster, 17 Nov, with reference pictures of cut chrome wheels): broad blades cut from
    # plate, swept, hooked, split and crossed. Our own patterns. A bar marked free=True ends in a point in mid-air on
    # purpose (a hook or barb); every other bar end must be buried in the hub, the rim or another bar: rim_check().
    "billet_reaper5": dict(label="Billet: five hooked blades", ref=0, flare=0.9, wk=1.0, n=5, finish="chrome", rim=0, hub="tall", sizes=((21, 2.15), (18, 5.5)), bars=[
        dict(curve=True, pts=[(0, -20, 22, 0.9), (0.25, -6, 27, 0.75), (0.55, 22, 21, 0.6), (0.8, 46, 14, 0.5), (1, 60, 11, 0.45)]),
        dict(curve=True, free=True, pts=[(0.42, 6, 12, 0.6), (0.56, -4, 9, 0.5), (0.7, -8, 5, 0.42), (0.82, -4, 1.5, 0.36)])]),
    "billet_cyclone9": dict(label="Billet: nine split blades", ref=0, flare=0.8, wk=1.0, n=9, finish="chrome", rim=0, hub="slim", sizes=((21, 2.15), (18, 5.5)), bars=[
        dict(curve=True, pts=[(0, -8, 10, 0.9), (0.3, 6, 10, 0.75), (0.62, 30, 8, 0.6), (0.8, 45, 7, 0.5)]),
        dict(curve=True, pts=[(0.74, 40, 5.5, 0.5), (0.88, 49, 5, 0.47), (1, 53, 5, 0.45)]),
        dict(curve=True, pts=[(0.74, 40, 5.5, 0.5), (0.88, 58, 5, 0.47), (1, 69, 5, 0.45)])]),
    "billet_bloom8": dict(label="Billet: crossed petals", ref=0, flare=0.7, wk=1.0, n=8, finish="chrome", rim=0, hub="flange", sizes=((21, 2.15), (18, 5.5)), bars=[
        dict(curve=True, pts=[(0, 0, 8, 0.9), (0.35, 14, 7.5, 0.72), (0.7, 38, 7, 0.55), (1, 52, 7, 0.45)]),
        dict(curve=True, pts=[(0, 0, 8, 0.9), (0.35, -14, 7.5, 0.72), (0.7, -38, 7, 0.55), (1, -52, 7, 0.45)])]),
    "billet_fang6": dict(label="Billet: six leaf blades with a rib", ref=0, flare=0.6, wk=1.0, n=6, finish="chrome", rim=0, hub="slim", sizes=((21, 2.15), (18, 5.5)), bars=[
        dict(curve=True, pts=[(0, -4, 11, 0.9), (0.3, 4, 23, 0.72), (0.55, 16, 25, 0.6), (0.8, 30, 13, 0.5), (1, 36, 9, 0.45)]),
        dict(curve=True, pts=[(0, -24, 6, 0.9), (0.5, -8, 5, 0.6), (1, 8, 5, 0.45)])]),
    "billet_twist7": dict(label="Billet: seven S blades", ref=0, flare=0.9, wk=1.0, n=7, finish="chrome", rim=0, hub="tall", sizes=((21, 2.15), (18, 5.5)), bars=[
        dict(curve=True, pts=[(0, 10, 13, 0.9), (0.3, -12, 12, 0.75), (0.6, 4, 10, 0.6), (0.85, 30, 8, 0.5), (1, 42, 8, 0.45)])]),
    # wire-spoked wheels (beenster, 16 Nov: five more rims with spoke variations, mainly for MX and adventure bikes).
    # "wire": n spokes laced "cross" times, "gauge" the spoke's radius in mm, a rim section, a hub, and what the
    # hub, spokes and nipples are finished in. "over": the spokes run to the far edge of the rim (tubeless).
    "wire_mx36": dict(label="Motocross: 36 spokes, flat anodised rim", ref=0, finish="anodised_gold", sizes=((21, 1.6), (19, 2.15)),
                      wire=dict(n=36, cross=3, gauge=2.0, rim="flat", hub="spool", hub_finish="matt_black", spoke="steel", nipple="alu")),
    "wire_enduro32": dict(label="Enduro: 32 heavy spokes, billet hub", ref=0, finish="matt_black", sizes=((21, 1.6), (18, 2.15)),
                          wire=dict(n=32, cross=2, gauge=3.0, rim="lip", hub="billet", hub_finish="anodised_gold", spoke="black", nipple="anodised_gold")),
    "wire_cross40": dict(label="Adventure: 40 spokes crossed to the rim edges, tubeless", ref=0, finish="gloss_black", sizes=((19, 3.0), (17, 4.5)),
                         wire=dict(n=40, cross=3, gauge=2.3, rim="cross", hub="cone", hub_finish="gunmetal", spoke="steel", nipple="alu", over=True)),
    "wire_spine32": dict(label="Adventure: centre-rib rim, 16 spoke pairs", ref=0, finish="gunmetal", sizes=((21, 2.15), (18, 4.0)),
                         wire=dict(n=32, cross=1, gauge=2.6, rim="spine", hub="billet", hub_finish="gunmetal", spoke="black", nipple="alu")),
    "wire_rally36": dict(label="Rally: 36 long tangent spokes, deep polished rim", ref=0, finish="chrome", sizes=((21, 2.15), (17, 4.25)),
                         wire=dict(n=36, cross=4, gauge=2.2, rim="lip", hub="cone", hub_finish="chrome", spoke="steel", nipple="alu")),
}

# wire rims: the side you see, from the outer lip in to the middle, as (across from the rim's edge h or the well's
# edge wd, radius below the bead seat R); and where the nipples seat (across, radius below R)
WIRE_RIMS = {
    "flat": (lambda h, wd: [(h + 9, 8), (h + 7, 1), (h + 3, -6), (h - 6, -9), (wd + 6, -12), (wd - 2, -22), (8, -25), (0, -25)], (6, -25)),
    "lip": (lambda h, wd: [(h + 10, 9), (h + 9, 3), (h + 4, -3), (h - 2, -4), (h - 8, -8), (wd + 4, -16), (wd - 6, -28), (0, -31)], (5, -30)),
    "cross": (lambda h, wd: [(h + 10, 8), (h + 11, -2), (h + 7, -13), (h - 5, -15), (h - 11, -10), (wd + 3, -14), (wd - 5, -24), (0, -25)], (None, -13)),
    "spine": (lambda h, wd: [(h + 9, 8), (h + 6, 2), (h + 2, -5), (h - 20, -9), (wd + 3, -14), (wd - 5, -24), (10, -25), (7, -41), (0, -42)], (6, -34)),
}


def wire_wheel(tag, st, R, W, rear, paint):
    """A wire-spoked wheel: its rim, a spool hub with two drilled flanges, and every spoke a rod from its hole in
    the flange to a nipple seated in the rim."""
    wr = st["wire"]
    h = W / 2
    wd = max(13.0, h * 0.42)
    side_f, (ny, nr) = WIRE_RIMS[wr["rim"]]
    tyre = [(-h - 8, R + 12), (-h - 5, R + 14.5), (-h - 1.5, R + 13.5), (-h, R + 9), (-h + 1, R + 2), (-h + 3.5, R), (-h + 17, R - 1),
            (-h + 20, R + 1.2), (-h + 23, R - 1.5), (-wd - 2, R - 7), (-wd + 6, R - 19), (wd - 6, R - 19), (wd + 2, R - 7),
            (h - 23, R - 1.5), (h - 20, R + 1.2), (h - 17, R - 1), (h - 3.5, R), (h - 1, R + 2), (h, R + 9), (h + 1.5, R + 13.5), (h + 5, R + 14.5), (h + 8, R + 12)]
    side = [(y, R + r) for y, r in side_f(h, wd)]
    D.lathe(f"{tag}_rim", (0, 0, 0), (0, 1, 0), tyre + side + [(-y, r) for y, r in side[::-1][1:]] + [tyre[0]], paint, 96, caps=False)
    B.cone(f"{tag}_valve", (R - 26, 6, 0), (R - 46, 16, 0), 3.2, 2.8, B.mat("rubber"), 10)
    hubm = finish(wr.get("hub_finish", st["finish"]))
    steel = B.mat("steel")
    hl = 62 if rear else 52
    r_f, r_b, r_m = {"spool": (44, 30, 23), "billet": (54, 40, 34), "cone": (64, 36, 50)}[wr["hub"]]
    if rear:
        r_f, r_b, r_m = r_f * 1.18, r_b * 1.18, r_m * 1.18
    yf = hl - 15
    prof = [(-hl, 15), (-hl, r_b), (-yf - 7, r_b), (-yf - 4, r_f), (-yf + 4, r_f), (-yf + 8, r_m + (r_f - r_m) * 0.35), (-yf * 0.4, r_m), (yf * 0.4, r_m),
            (yf - 8, r_m + (r_f - r_m) * 0.35), (yf - 4, r_f), (yf + 4, r_f), (yf + 7, r_b), (hl, r_b), (hl, 15)]
    D.lathe(f"{tag}_hub", (0, 0, 0), (0, 1, 0), prof, hubm, 56)
    D.lathe(f"{tag}_bearing", (0, 0, 0), (0, 1, 0), [(-hl - 2, 10), (-hl - 2, 17), (hl + 2, 17), (hl + 2, 10)], steel, 32)
    D.lathe(f"{tag}_bore", (0, 0, 0), (0, 1, 0), [(-hl - 2.4, 0), (-hl - 2.4, 10.5), (hl + 2.4, 10.5), (hl + 2.4, 0)], D.surf("soot", (0.006, 0.006, 0.006), 0.0, 0.95, wear=0.0), 24)
    if wr["hub"] == "billet":      # a machined band round the middle of the barrel
        D.lathe(f"{tag}_hubband", (0, 0, 0), (0, 1, 0), [(-9, r_m - 1), (-9, r_m + 2.5), (9, r_m + 2.5), (9, r_m - 1)], D.surf("wheel_machined", (0.82, 0.83, 0.85), 1.0, 0.16, wear=0.05), 48)
    n, g = wr["n"], wr["gauge"]
    sm = D.surf("spoke_black", (0.02, 0.02, 0.022), 0.6, 0.35, wear=0.0) if wr.get("spoke") == "black" else steel
    nm = finish(wr["nipple"]) if wr.get("nipple") in FINISH else B.mat("alu")
    sdeg = 720.0 / n
    for k in range(n):
        s = 1 if k % 2 == 0 else -1
        lead = (k // 2) % 2 == 0
        phi = 360.0 * (k + 0.5) / n
        ha = math.radians(phi + wr["cross"] * sdeg * (-1 if lead else 1))
        ra = math.radians(phi)
        p0 = ((r_f - 6) * math.cos(ha), s * (yf + (3.5 if lead else -3.5)), (r_f - 6) * math.sin(ha))
        if wr.get("over"):
            rn, yn = R + nr, -s * (h + 1)
        else:
            rn, yn = R + nr, s * ny
        p1 = (rn * math.cos(ra), yn, rn * math.sin(ra))
        L = math.dist(p0, p1)
        u = tuple((p0[j] - p1[j]) / L for j in range(3))
        pe = tuple(p1[j] - u[j] * 3 for j in range(3))      # the nipple's head beds a little way into the rim
        B.tube(f"{tag}_wire{k}", [p0, p1], g, sm, caps=False)
        B.cone(f"{tag}_nipple{k}", pe, tuple(p1[j] + u[j] * (9 + g * 3) for j in range(3)), g * 2.0, g * 1.6, nm, 6)
        hd = tuple(p0[j] for j in range(3))
        B.cone(f"{tag}_wirehead{k}", (hd[0], hd[1] - (2.5 if (s > 0) == lead else -2.5) - 1.5, hd[2]), (hd[0], hd[1] - (2.5 if (s > 0) == lead else -2.5) + 1.5, hd[2]), g * 1.8, g * 1.8, sm, 8)



def rim_profile(R, W, ridges):
    """The rim section as a closed loop [(across, radius)]: tyre side first, then back along the side you see."""
    h = W / 2
    wd = max(13.0, h * 0.42)
    tyre = [(-h - 8, R + 12), (-h - 5, R + 14.5), (-h - 1.5, R + 13.5), (-h, R + 9), (-h + 1, R + 2), (-h + 3.5, R), (-h + 17, R - 1),
            (-h + 20, R + 1.2), (-h + 23, R - 1.5), (-wd - 2, R - 7), (-wd + 6, R - 19), (wd - 6, R - 19), (wd + 2, R - 7),
            (h - 23, R - 1.5), (h - 20, R + 1.2), (h - 17, R - 1), (h - 3.5, R), (h - 1, R + 2), (h, R + 9), (h + 1.5, R + 13.5), (h + 5, R + 14.5), (h + 8, R + 12)]
    side = [(h + 9, R + 8), (h + 6, R + 2), (h + 2, R - 5)]
    if ridges == 1:      # one raised rib on the shoulder
        side += [(h - 8, R - 7.5), (h - 9.5, R - 10.5), (h - 13, R - 10.5), (h - 14.5, R - 8)]
    elif ridges == 2:    # a stepped shoulder with two fine ribs
        side += [(h - 4, R - 6.5), (h - 5, R - 9.5), (h - 8, R - 9.5), (h - 9, R - 7.5), (h - 14, R - 8), (h - 15, R - 11), (h - 18, R - 11), (h - 19, R - 8.5)]
    side += [(h - 23, R - 9), (wd + 3, R - 14), (wd - 5, R - 26), (0, R - 27)]
    back = [(-y, r) for y, r in side[::-1][1:]]
    return tyre + side + back + [tyre[0]]


def bar(name, pts, n, k0, material, r_h, r_r, th_h, curve, yo=0.0, flat=None, wk=1.0, cap=None, flare=0.0):
    """One bar of a spoke pattern, at spoke k0 of n. "flare" spreads the last fifth of the bar into a foot where it
    joins the rim (beenster, 16 Nov: thin swept tips looked as if they only touched it)."""
    if curve:
        for _ in range(3):
            o = [pts[0]]
            for i in range(len(pts) - 1):
                a, b = pts[i], pts[i + 1]
                o += [tuple(0.75 * a[j] + 0.25 * b[j] for j in range(4)), tuple(0.25 * a[j] + 0.75 * b[j] for j in range(4))]
            pts = o + [pts[-1]]
    dense = []
    for i in range(len(pts) - 1):
        a, b = pts[i], pts[i + 1]
        m = 1 if curve else 10
        dense += [tuple(a[j] + (b[j] - a[j]) * q / m for j in range(4)) for q in range(m)]
    dense.append(pts[-1])
    if flare and max(q[0] for q in dense) > 0.95:
        fo = lambda f: max(0.0, (f - 0.76) / 0.24) ** 2
        dense = [(f, ang, w * (1 + flare * fo(f)), t * (1 + 0.45 * min(flare, 1.0) * fo(f))) for f, ang, w, t in dense]
    base = 360.0 * k0 / n
    path = []
    for f, ang, w, t in dense:
        r = r_h + (r_r - r_h) * f
        th = math.radians(base + ang)
        path.append((r * math.cos(th), r * math.sin(th), w * wk, t * th_h))
    verts, faces, m = [], [], 12
    for i, (x, z, w, t) in enumerate(path):
        a, b = path[max(0, i - 1)], path[min(len(path) - 1, i + 1)]
        tx, tz = b[0] - a[0], b[1] - a[1]
        L = math.hypot(tx, tz) or 1.0
        nx, nz = -tz / L, tx / L
        for q in range(m):
            wq = 2 * math.pi * q / m
            cq, sq = math.cos(wq), math.sin(wq)
            ex = math.copysign(abs(cq) ** 0.45, cq) * w
            ey = math.copysign(abs(sq) ** 0.45, sq) * (flat if flat else t)
            vx, vy, vz = x + nx * ex, yo * (t if flat else 1) + ey, z + nz * ex
            if cap:      # the spoke runs right into the rim: each point stops just inside the rim's own inner surface
                rv, rc_ = math.hypot(vx, vz), cap(abs(vy))
                if rv > rc_:
                    vx, vz = vx * rc_ / rv, vz * rc_ / rv
            verts.append(B.V(vx, vy, vz))
    for i in range(len(path) - 1):
        for q in range(m):
            q2 = (q + 1) % m
            faces.append((i * m + q, i * m + q2, (i + 1) * m + q2, (i + 1) * m + q))
    faces += [tuple(range(m - 1, -1, -1)), tuple((len(path) - 1) * m + q for q in range(m))]
    ob = B.mesh_obj(name, verts, faces, material, True, 0, True)
    B.bm_fix_normals(ob)
    md = ob.modifiers.new("bevel", "BEVEL")
    md.width, md.segments, md.limit_method = 1.2 * B.MM, 2, "ANGLE"
    return ob


def wheel(tag, style, dia, width, rear, finish_name=None):
    """Build one wheel about the origin, axle along y. Returns its objects."""
    before = set(bpy.data.objects)
    st = STYLES[style]
    R, W = dia * INCH / 2, width * INCH
    paint = finish(finish_name or st["finish"])
    if st.get("wire"):
        wire_wheel(tag, st, R, W, rear, paint)
        return [o for o in bpy.data.objects if o not in before]
    bright = D.surf("wheel_machined", (0.82, 0.83, 0.85), 1.0, 0.16, wear=0.05)
    steel = B.mat("steel")
    D.lathe(f"{tag}_rim", (0, 0, 0), (0, 1, 0), rim_profile(R, W, st["rim"]), paint, 96, caps=False)
    # valve
    B.cone(f"{tag}_valve", (R - 26, 6, 0), (R - 46, 16, 0), 3.2, 2.8, B.mat("rubber"), 10)
    # hub
    hub = st["hub"]
    hl = (62 if rear else 52) if hub != "tall" else (80 if rear else 66)
    r_body = {"star": 30, "flange": 36, "drum": 58 if rear else 44, "slim": 26, "tall": 30}[hub] * (1.25 if rear and hub != "drum" else 1.0)
    r_fl = r_body + (16 if hub in ("flange", "star") else 6)
    prof = [(-hl, 15), (-hl, r_fl - 6), (-hl + 3, r_fl), (-hl + 9, r_fl), (-hl + 15, r_body + 1), (-10, r_body + (5 if hub == "tall" else 0)), (10, r_body + (5 if hub == "tall" else 0)),
            (hl - 15, r_body + 1), (hl - 9, r_fl), (hl - 3, r_fl), (hl, r_fl - 6), (hl, 15)]
    D.lathe(f"{tag}_hub", (0, 0, 0), (0, 1, 0), prof, paint, 56)
    D.lathe(f"{tag}_bearing", (0, 0, 0), (0, 1, 0), [(-hl - 2, 10), (-hl - 2, 17), (hl + 2, 17), (hl + 2, 10)], steel, 32)
    D.lathe(f"{tag}_bore", (0, 0, 0), (0, 1, 0), [(-hl - 2.4, 0), (-hl - 2.4, 10.5), (hl + 2.4, 10.5), (hl + 2.4, 0)], D.surf("soot", (0.006, 0.006, 0.006), 0.0, 0.95, wear=0.0), 24)
    nb = 5 if hub in ("star", "drum") else 6
    for sy in (-1, 1):
        for k in range(nb):
            w = 2 * math.pi * (k + 0.5) / nb
            D.bolt(f"{tag}_hubbolt{sy}{k}", ((r_fl - 9) * math.cos(w), sy * hl, (r_fl - 9) * math.sin(w)), (0, sy, 0), 5.0, 3.5, steel)
        if hub == "star":      # a star carrier plate behind the bolts
            pts = []
            for k in range(nb * 2):
                w = 2 * math.pi * (k / 2 + 0.5) / nb
                rr = r_fl + 22 if k % 2 == 0 else r_fl - 2
                pts.append((rr * math.cos(w), rr * math.sin(w)))
            y0, y1 = sorted((sy * (hl - 15), sy * (hl - 8)))
            B.prism(f"{tag}_star{sy}", D.chaikin(pts, 1), y0, y1, paint, 1.0)
    # spokes
    # spoke ends meet the rim flush (beenster, 16 Nov): the spokes run on past the rim's inner surface and are
    # trimmed to it, across the well and up the shoulders, so no gap shows from the side at any spoke angle
    r_h, r_r = r_body - 5, R - 8
    h_, wd_ = W / 2, max(13.0, W / 2 * 0.42)

    def cap(y, R=R):
        if y <= wd_ - 5:
            return R - 27 + 3.5 + y / max(1.0, wd_ - 5)
        if y <= wd_ + 3:
            return R - 26 + 3.5 + (y - (wd_ - 5)) / 8.0 * 12
        return R - 14 + 3.5 + min(1.0, (y - wd_ - 3) / max(1.0, h_ - 26 - wd_)) * 5
    th_h = (min(hl - 16, 24) if not rear else min(hl - 16, 30)) * (1.25 if hub == "tall" else 1.0)
    wk = st.get("wk", 1.4) * (1.12 if rear else 1.0)
    for bi, b_ in enumerate(st["bars"]):
        for k in range(st["n"]):
            bar(f"{tag}_spoke{bi}_{k}", list(b_["pts"]), st["n"], k, paint, r_h, r_r, th_h, b_.get("curve", False), wk=wk, cap=cap, flare=st.get("flare", 0.45))
            if b_.get("inlay"):
                for sy in (-1, 1):
                    pts = [(f, a, w * 0.62, t) for f, a, w, t in b_["pts"] if True]
                    pts = [(min(0.93, max(0.08, f)), a, w, t) for f, a, w, t in pts]
                    bar(f"{tag}_inlay{bi}_{k}{sy}", pts, st["n"], k, bright, r_h, r_r, th_h, b_.get("curve", False), yo=sy * 1.0, flat=1.0, wk=wk, cap=lambda y: cap(y) - 4)
    return [o for o in bpy.data.objects if o not in before]


def rim_check(tag, style):
    """Do the spokes actually meet the rim (beenster, 17 Nov)? For every bar of a built wheel: the outer end must
    be buried in the rim (or, for a branch, in another bar) and the inner end in the hub or another bar. A bar marked
    free may end in the air at its outer end. Returns a list of faults; empty means every spoke is joined."""
    from mathutils import Vector
    from mathutils.bvhtree import BVHTree
    st = STYLES[style]
    if st.get("wire"):
        return []
    dg = bpy.context.evaluated_depsgraph_get()

    def tree_of(o):
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        vs = [ev.matrix_world @ v.co for v in me.vertices]
        ps = [tuple(p.vertices) for p in me.polygons]
        ev.to_mesh_clear()
        return vs, BVHTree.FromPolygons(vs, ps, epsilon=0.0)
    objs = {o.name: tree_of(o) for o in bpy.data.objects if o.type == "MESH" and o.name.startswith(tag + "_") and o.name.split("_")[1][:5] in ("rim", "hub", "spoke")}
    cen = Vector(bpy.data.objects[tag + "_rim"].matrix_world.translation)

    def inside(pt, name):
        """is the point inside the solid of that part? counts crossings of a ray along the axle... the rim is a
        closed ring section and the bars closed tubes, so parity works"""
        tr = objs[name][1]
        n_, o_ = 0, pt + Vector((0.00003, -1.0, 0.00002))
        d = Vector((0, 1, 0))
        while True:
            hit = tr.ray_cast(o_, d)
            if hit[0] is None or hit[0].y > pt.y:
                break
            n_ += 1
            o_ = hit[0] + d * 1e-5
        return n_ % 2 == 1
    faults = []
    m = 12
    for name, (vs, _) in objs.items():
        if "_spoke" not in name:
            continue
        bi = int(name.split("_spoke")[1].split("_")[0])
        free = st["bars"][bi].get("free", False)
        o = bpy.data.objects[name]
        raw = [o.matrix_world @ v.co for v in o.data.vertices]
        ends = (("inner", raw[:m]), ("outer", raw[-m:]))
        for which, ring in ends:
            if which == "outer" and free:
                continue
            c_ = sum(ring, Vector()) / m
            # pull the test point a little back along the bar so it is the bar's own core just before its end
            other = sum(raw[m:2 * m] if which == "inner" else raw[-2 * m:-m], Vector()) / m
            probe = c_ + (other - c_) * 0.15
            others = [n for n in objs if n != name]
            near = min(objs[n][1].find_nearest(probe)[3] or 1e9 for n in others)
            buried = any(inside(probe, n) for n in others)
            if not buried and near > 0.0015:
                rr = math.hypot(c_.x - cen.x, c_.z - cen.z) / B.MM
                faults.append(f"{name} {which} end floats {near / B.MM:.1f} mm from the nearest part (radius {rr:.0f} mm)")
    return faults


def fit(b, tag, c, w, tyre_width):
    """Fit a library wheel to a bike: the style named in the model's wheel, at the bike's own rim size,
    centred on the axle c (x, z). The bike keeps its own tyre, discs and valve."""
    use(b)
    objs = wheel(tag, w["style"], w["rim_r"] * 2 / INCH, tyre_width * (0.6 if STYLES[w["style"]].get("wire") else 0.76) / INCH, tag == "rear", w.get("finish"))
    keep = []
    for o in objs:
        if o.name.startswith(tag + "_valve"):
            bpy.data.objects.remove(o, do_unlink=True)
        else:
            keep.append(o)
    objs = keep
    place(objs, (c[0], 0, c[1]), 0)
    return objs


def place(objs, loc, rot_z):
    e = bpy.data.objects.new("pivot", None)
    bpy.context.collection.objects.link(e)
    for o in objs:
        if o.parent is None:
            o.parent = e
    e.location = tuple(v * B.MM for v in loc)
    e.rotation_euler = (0, 0, math.radians(rot_z))


def render(style, samples=40):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    B._mats.clear()
    st = STYLES[style]
    (fd, fw), (rd, rw) = st["sizes"]
    place(wheel("front", style, fd, fw, False), (300, -60, 0), 14)
    place(wheel("rear", style, rd, rw, True), (-290, 120, 0), -38)
    sc = bpy.context.scene
    sc.render.engine, sc.cycles.device, sc.cycles.samples, sc.cycles.use_denoising = "CYCLES", "CPU", samples, True
    sc.render.resolution_x, sc.render.resolution_y = 1280, 760
    w = bpy.data.worlds.new("w")
    sc.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.5, 0.51, 0.53, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.3
    bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 0, -(max(fd, rd) * INCH / 2 + 16) * B.MM))
    fl = bpy.context.object
    fm = bpy.data.materials.new("floor")
    fm.use_nodes = True
    fm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.33, 0.335, 0.345, 1)
    fm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.5
    fl.data.materials.append(fm)
    bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 1.6, 0), rotation=(math.radians(90), 0, 0))
    bpy.context.object.data.materials.append(fm)
    for loc, size, power in (((-1.4, -1.8, 1.6), 2.2, 380), ((1.8, -1.6, 0.9), 2.0, 220), ((0, -0.4, 2.2), 2.5, 180)):
        ld = bpy.data.lights.new("l", "AREA")
        ld.size, ld.energy = size, power
        lo = bpy.data.objects.new("l", ld)
        sc.collection.objects.link(lo)
        lo.location = loc
        from mathutils import Vector
        lo.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    cd = bpy.data.cameras.new("cam")
    cd.lens = 85
    cam = bpy.data.objects.new("cam", cd)
    sc.collection.objects.link(cam)
    from mathutils import Vector
    cam.location = (0.05, -2.9, 0.42)
    cam.rotation_euler = (Vector((0.0, 0, -0.02)) - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.camera = cam
    try:
        sc.view_settings.look = "AgX - Medium High Contrast"
    except Exception:
        pass
    sc.view_settings.exposure = -2.1
    os.makedirs(OUT, exist_ok=True)
    sc.render.filepath = os.path.join(OUT, style + ".png")
    bpy.ops.render.render(write_still=True)


def sheet(styles):
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 30)
    small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 22)
    cols = 3
    rows = (len(styles) + cols - 1) // cols
    tw, th = 960, 570
    im = Image.new("RGB", (cols * tw, rows * (th + 84)), (244, 242, 238))
    dr = ImageDraw.Draw(im)
    for i, s in enumerate(styles):
        st = STYLES[s]
        x, y = (i % cols) * tw, (i // cols) * (th + 84)
        im.paste(Image.open(os.path.join(OUT, s + ".png")).convert("RGB").resize((tw, th)), (x, y))
        (fd, fw), (rd, rw) = st["sizes"]
        dr.text((x + 18, y + th + 8), f"{i + 1}. {st['label']}", fill=(28, 27, 25), font=font)
        dr.text((x + 18, y + th + 46), f"front {fd} x {fw:g} in, rear {rd} x {rw:g} in, shown in {st['finish'].replace('_', ' ')}", fill=(93, 90, 84), font=small)
    im.save(os.path.join(OUT, "sheet.png"))


if __name__ == "__main__":
    import build3d
    use(build3d)
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    names = args or list(STYLES)
    if "--sheet-only" not in sys.argv:
        if "--check" in sys.argv:
            # build every style front and rear and test that each spoke is joined at both ends
            bad = 0
            for s in names:
                for rear in (False, True):
                    bpy.ops.wm.read_factory_settings(use_empty=True)
                    B._mats.clear()
                    (fd, fw), (rd, rw) = STYLES[s]["sizes"]
                    wheel("w", s, rd if rear else fd, rw if rear else fw, rear)
                    f = rim_check("w", s)
                    bad += len(f)
                    print(f"RIMCHECK {s:16s} {'rear ' if rear else 'front'} {'ok' if not f else 'FAULTS ' + str(len(f))}", flush=True)
                    for q in f[:4]:
                        print("   ", q, flush=True)
            print("rim check faults:", bad)
            sys.exit(0)
        for s in names:
            render(s, 16 if "--draft" in sys.argv else 40)
    sheet(list(STYLES) if not args else names)
