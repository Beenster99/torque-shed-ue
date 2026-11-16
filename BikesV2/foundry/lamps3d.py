"""Common parts, stage 3: headlamps, tail lights and indicators (common parts plan, 16 Nov).

The joints are the ones in docs/common_parts_joints.md:
- Headlamp: ears clamped to the fork tubes, the lamp in the bike's lamp box (centre and front face found
  from the bike's own lamp). Faired bikes (sport, adventure) and the MX number board keep their own lamps.
- Tail light: the mount the bike's own lamp stands on, facing straight back (the tail end on a naked, the
  frame loop's bracket on a cafe, the rear mudguard on classics, cruisers and choppers). A sport tail has
  its lamp built in and keeps it.
- Indicators: front ones on the headlamp ears, rear ones on the sides of the tail light, both on M10 stalks.
  Bikes that keep their own lamps keep their indicator places and take ours there.

Every lens is clear, so what is inside shows (beenster, 16 Nov), and every light is a glowing part, so the
night pictures show them lit.

Our own designs with real parts as reference only: no maker's name, logo or exact product ships.

    python3 foundry/lamps3d.py shots [keys] [--samples N]   # out/lamps/*.png: each part on a bike, plus night shots
    python3 foundry/lamps3d.py check                         # every part on the lead bike of each family: clipping
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
OUT = os.path.join(ROOT, "out", "lamps")

HEADLAMPS = {
    "round_big": dict(label="Large round", note="A deep chrome bowl and rolled rim, a silvered reflector and a domed glass: classics, cruisers, choppers."),
    "round_small": dict(label="Small round", note="A compact black bowl with a satin rim and a projector behind clear glass: cafe and naked."),
    "halo": dict(label="Round halo", note="A black LED unit with a ring of light round its edge and two projectors."),
    "twin": dict(label="Twin round", note="Two small round lamps side by side on one bracket."),
    "stacked": dict(label="Stacked twin", note="Two small round lamps one above the other in a single black shroud."),
    "rect": dict(label="Flat rectangle", note="A wide flat unit with rounded corners, an LED strip across the top and two projectors."),
    "hex": dict(label="Angular", note="A six-sided modern unit with light brows over two projectors."),
    "oval": dict(label="Tall oval", note="An upright oval with a light blade down each side and one big projector."),
}
TAILS = {
    "round": dict(label="Round", note="A chrome bowl with a domed red glass, a ringed reflector and a bulb inside."),
    "oval": dict(label="Oval", note="A wide black oval with a clear red lens over a chrome tray of LEDs."),
    "strip": dict(label="LED strip", note="A slim wide strip of light in a black housing."),
    "twin_dot": dict(label="Twin dot", note="Two round LED lamps on a short black bar."),
    "classic": dict(label="Classic unit", note="A rounded red lens with a chrome rim on a chrome bracket, with a plate window underneath."),
    "hanger": dict(label="Hanger lamp", note="A small LED lamp on a short black arm that drops from the tail, plate light beneath."),
}
INDICATORS = {
    "led_short": dict(label="LED, short stalk", note="A slim black pod with a row of amber LEDs behind a clear lens, on a short rubber stalk."),
    "led_long": dict(label="LED, long stalk", note="The same pod on a long rubber stalk, for wide tails and bikes with luggage."),
    "chrome_short": dict(label="Chrome, short stalk", note="An amber glass in a chrome shell with a ribbed reflector and bulb, on a short chrome stalk."),
    "chrome_long": dict(label="Chrome, long stalk", note="The chrome indicator on a long stalk: classics and cruisers."),
    "bullet": dict(label="Bullet", note="A small chrome bullet lying fore and aft with an amber lens at its tip."),
    "flush": dict(label="Flush blade", note="A thin amber blade on a stub, almost flush with its mount."),
}
# which families each kind can go on (the rest keep their own lamps; see the module text)
HEAD_FAMS = ("Naked", "Classic", "Cafe", "Cruiser", "Chopper")
TAIL_FAMS = ("Naked", "Classic", "Cafe", "Cruiser", "Chopper")

OLD_HEAD = ("lamphouse", "lampbezel", "lampdrl", "lampbowl", "lampbar", "projring", "projlens", "projbulb", "maincup", "mainled", "lampstay",
            "lampcover", "lamprim", "lampreflector", "lampshield", "lampbulb", "lampglass", "lampled", "lampproj", "lampear")
OLD_TAIL = ("taillight", "guardlamp", "looplamphouse", "looplamplens", "looplamptray", "looplampcell", "looplampled", "looplampplate", "taillamp")


def use(b):
    global B
    B = b
    D.B = b
    BR.use(b)


def mats():
    lens = D.glass_mat("redglass", (0.9, 0.02, 0.02))
    return dict(
        chrome=B.mat("chrome"), plastic=B.mat("plastic"), rubber=B.mat("rubber"), steel=B.mat("steel"), dark=B.mat("alu_dark"),
        black=D.surf("lp_gloss", (0.01, 0.01, 0.012), 0.3, 0.12, coat=1.0, wear=0.0),
        satin=D.surf("lp_satin", (0.05, 0.05, 0.055), 0.85, 0.3),
        silver=D.surf("lp_silver", (0.9, 0.9, 0.88), 1.0, 0.12, wear=0.0),
        smoke=D.surf("smokechrome", (0.2, 0.2, 0.22), 1.0, 0.1, wear=0.0),
        glass=D.glass_mat(), red=lens, amberglass=D.glass_mat("amberglass", (1.0, 0.36, 0.03)),
        white=D.glow("lp_white", (1.0, 0.97, 0.92), 3.0), drl=D.glow("drl", (0.9, 0.95, 1.0), 4.0),
        dip=D.glow("lampdip", (1.0, 0.97, 0.9), 0.9), tailred=D.glow("tailred", (1.0, 0.03, 0.02), 2.6),
        amber=D.glow("amberled", (1.0, 0.42, 0.02), 1.2), bulb=D.glow("bulb", (1.0, 0.97, 0.9), 1.2),
    )


# ---------------------------------------------------------------- shapes
def circle(r, m=64):
    return [(r * math.cos(2 * math.pi * k / m), r * math.sin(2 * math.pi * k / m)) for k in range(m)]


def loft(name, org, face, outline, stations, mat, close=(True, True), sharp=False):
    """A shell along x through an outline [(y, z)] scaled at stations [(dx along the face direction, scale)]."""
    m = len(outline)
    verts, faces = [], []
    for dx, k in stations:
        verts += [B.V(org[0] + face * dx, org[1] + y * k, org[2] + z * k) for y, z in outline]
    for j in range(len(stations) - 1):
        for i in range(m):
            i2 = (i + 1) % m
            faces.append((j * m + i, j * m + i2, (j + 1) * m + i2, (j + 1) * m + i))
    if close[0]:
        faces.append(tuple(range(m - 1, -1, -1)))
    if close[1]:
        faces.append(tuple((len(stations) - 1) * m + i for i in range(m)))
    ob = B.mesh_obj(name, verts, faces, mat, True, 0, sharp)
    B.bm_fix_normals(ob)
    return ob


def plate(name, org, face, outline, d0, d1, mat, bevel=0.0):
    """A flat plate of an outline [(y, z)] between depths d0 and d1 along the face direction."""
    return loft(name, org, face, outline, [(d0, 1.0), (d1, 1.0)], mat, (True, True), True) if bevel == 0 else \
        D.slab(name, (org[0], org[2]), (face, 0.0), (0.0, 1.0), [(org[1] + y, z) for y, z in outline], d0, d1, mat, bevel)


def unit(name, org, face, ol, depth, M, house, rim, inside, glass_dome=14.0):
    """One lamp: a housing that narrows to the back, a rim, a reflector bowl, the light source(s) and a clear
    domed glass. org is the centre of the front face, ol its outline (centred on 0, 0)."""
    loft(name + "house", org, face, ol, [(-depth, 0.5), (-depth * 0.75, 0.78), (-depth * 0.35, 0.96), (-4, 1.0), (0, 1.0)], house, (True, False))
    loft(name + "rim", org, face, ol, [(-3, 1.035), (2, 1.04), (4.5, 1.0), (4.5, 0.92), (0, 0.9)], rim, (False, False))
    loft(name + "bowl", org, face, ol, [(0.5, 0.9), (-depth * 0.25, 0.72), (-depth * 0.45, 0.42), (-depth * 0.5, 0.15)], M["silver"] if inside != "led" else M["black"], (False, True))
    loft(name + "glass", org, face, ol, [(3, 0.91), (3 + glass_dome * 0.4, 0.82), (3 + glass_dome * 0.75, 0.6), (3 + glass_dome * 0.95, 0.3), (3 + glass_dome, 0.06)], M["glass"], (False, True))


def projector(name, at, face, r, M, lit=True):
    """A projector lens in its ring, as on the signed-off naked lamp."""
    ax = (face, 0, 0)
    D.lathe(name + "ring", at, ax, [(-22, r + 6), (-2, r + 6), (0, r + 3.5), (0, r), (-22, r)], M["satin"], 40, caps=False)
    D.lathe(name + "lens", at, ax, [(-12, r), (-7, r * 0.95), (-2, r * 0.7), (1, r * 0.35), (2, 0)], M["glass"], 40, sharp=False, caps=False)
    D.lathe(name + "back", at, ax, [(-14, 0), (-14, r), (-13, r), (-13, 0)], M["dip"] if lit else M["satin"], 24)


def bulb(name, at, face, M, r=7):
    D.lathe(name, at, (face, 0, 0), [(-16, 0), (-16, r), (-2, r), (1, r * 0.6), (2, 0)], M["dip"], 16)


# ---------------------------------------------------------------- headlamps
def fork_at(c, z):
    """The fork axis x at height z (the fork line runs F + uf * t in the side view)."""
    F, uf = c["F"], c["uf"]
    t = (z - F[1]) / uf[1]
    return F[0] + uf[0] * t


def build_headlamp(key, box, c, M):
    """The lamp in the bike's lamp box: box = (x of the front face, z of the centre). Returns where the front
    indicators go (one base each side) and the parts made."""
    xf, zc = box
    face = 1
    if key == "round_big":
        R, depth, parts = 88.0, 96, [((0, 0), "bulb")]
    elif key == "round_small":
        R, depth, parts = 64.0, 74, [((0, 0), "proj")]
    elif key == "halo":
        R, depth, parts = 82.0, 80, [((0, 30), "proj_s"), ((0, -26), "proj")]
    elif key == "twin":
        R, depth, parts = 54.0, 66, [((-62, 0), "bulb"), ((62, 0), "bulb")]
    elif key == "stacked":
        R, depth, parts = 46.0, 62, [((0, 50), "proj_s"), ((0, -48), "proj_s")]
    else:
        R, depth, parts = None, 70, None
    yk = _bbox(("yoke_low", "yokepinch_low"))
    if yk is not None and yk[0][2] - 10 < zc + (R or 60) and yk[1][2] + 10 > zc - (R or 60):
        # the lower yoke stands right behind the lamp: a shallower bowl, and the lamp forward if still needed
        room = xf - yk[1][0] - 6
        depth = max(40, min(depth, room))
        xf = max(xf, yk[1][0] + 6 + depth)
    # how wide the lamp is, so its back clears the fork tubes
    fx = fork_at(c, zc)
    hit = D.side_hit(("forktube", "forkouter", "fork"), fx, zc, 1)
    rf = max(20.0, (abs(hit[1]) - 105.0) if hit is not None else 26.0)
    half_w = {"twin": 62 + 54, "rect": 96, "hex": 84, "oval": 66}.get(key, R or 80)
    if half_w > 105 - rf - 6:
        xf = max(xf, fx + rf + 8 + depth * 0.55)
    org = (xf, 0.0, zc)
    if key in ("round_big", "round_small", "halo"):
        house = M["chrome"] if key == "round_big" else M["black"]
        rim = M["chrome"] if key != "halo" else M["satin"]
        unit("lph", org, face, circle(R), depth, M, house, rim, "led" if key == "halo" else "bulb")
        if key == "halo":
            D.lathe("lphhalo", (xf + 1, 0, zc), (1, 0, 0), [(0, R * 0.9 - 1), (1.5, R * 0.86), (1.5, R * 0.8), (0, R * 0.78)], M["drl"], 64, caps=False)
    elif key == "twin":
        for s in (-1, 1):
            unit(f"lph{s}", (xf, s * 62.0, zc), face, circle(R), depth, M, M["black"], M["chrome"], "bulb")
        # the bracket between them
        plate("lphbridge", (xf - depth * 0.6, 0, zc - R * 0.2), 1, [(-70, -9), (70, -9), (70, 9), (-70, 9)], -6, 6, M["satin"])
    elif key == "stacked":
        shroud = D.chaikin([(-56, -102), (56, -102), (62, 0), (56, 104), (-56, 104), (-62, 0)], 3)
        loft("lphshroud", org, face, shroud, [(-depth, 0.6), (-depth * 0.6, 0.9), (-4, 1.0), (2, 1.0)], M["black"], (True, False))
        loft("lphshroudface", org, face, shroud, [(2, 1.0), (2, 0.0)], M["black"], (False, False))
        for dz in (50, -48):
            unit(f"lph{dz}", (xf + 2, 0, zc + dz), face, circle(R), 30, M, M["black"], M["satin"], "led", 10)
    elif key == "rect":
        ol = D.sell(96, 58, 1.0, 4.0, 64)
        unit("lph", org, face, ol, depth, M, M["black"], M["satin"], "led", 10)
        plate("lphstrip", (xf + 1, 0, zc + 36), 1, D.sell(78, 6, 1.0, 4.0, 40), 0, 2, M["drl"])
        parts = [((-40, -10), "proj"), ((40, -10), "proj")]
        R = 26.0
    elif key == "hex":
        ol = D.chaikin([(-84, 12), (-56, 62), (56, 62), (84, 12), (56, -58), (-56, -58)], 2)
        unit("lph", org, face, ol, depth, M, M["black"], M["satin"], "led", 12)
        for s in (-1, 1):
            brow = [(s * y, z) for y, z in D.chaikin([(14, 48), (60, 48), (76, 18), (68, 16), (54, 40), (14, 40)], 1)]
            plate(f"lphbrow{s}", (xf + 1, 0, zc), 1, brow if s > 0 else brow[::-1], 0, 2, M["drl"])
        parts = [((-34, -8), "proj"), ((34, -8), "proj")]
        R = 24.0
    elif key == "oval":
        ol = [(66 * math.cos(2 * math.pi * k / 64), 92 * math.sin(2 * math.pi * k / 64)) for k in range(64)]
        unit("lph", org, face, ol, depth, M, M["black"], M["satin"], "led", 14)
        for s in (-1, 1):
            blade = [(s * 50 + 5 * math.cos(2 * math.pi * k / 24), 52 * math.sin(2 * math.pi * k / 24)) for k in range(24)]
            plate(f"lphblade{s}", (xf + 1, 0, zc), 1, blade, 0, 2, M["drl"])
        parts = [((0, 0), "proj_big")]
        R = 34.0
    else:
        raise KeyError(key)
    for (dy, dz), kind in parts:
        at = (xf - 4, dy, zc + dz)
        if kind == "bulb":
            bulb(f"lphbulb{dy}{dz}", (xf - depth * 0.42, dy, zc + dz), 1, M)
        else:
            r = {"proj": 24.0, "proj_s": 17.0, "proj_big": 34.0}[kind] if key not in ("rect", "hex") else 20.0
            projector(f"lphproj{dy}{dz}", at, 1, r, M)
    # the ears: a plate from each side of the housing back to a split clamp on the fork tube
    ear_z = zc
    if yk is not None and yk[0][2] - 30 < ear_z < yk[1][2] + 30:
        # keep the clamps clear of the lower yoke: below it on the fork leg
        ear_z = yk[0][2] - 30
    fx = fork_at(c, ear_z)
    hit = D.side_hit(("forktube", "forkouter", "fork"), fx, ear_z, 1)
    rf = max(20.0, (abs(hit[1]) - 105.0) if hit is not None else rf)
    d = (c["uf"][0], 0, c["uf"][1])
    xb = xf - depth * 0.55
    ea = min(half_w * 0.8, 105 - rf - 6)
    bases = {}
    for s in (-1, 1):
        y0, y1 = sorted((s * (ea - 6), s * (105 - rf - 2)))
        if y1 - y0 < 6:
            y0, y1 = sorted((s * (105 - rf - 8), s * (105 - rf - 2)))
        B.prism(f"lpear{s}", [(xb + 16, zc - 14), (xb + 16, zc + 14), (fx - 4, ear_z + 18), (fx - 4, ear_z - 18)], y0, y1, M["satin"], 1.5)
        D.lathe(f"lpearclamp{s}", (fx, s * 105.0, ear_z), d, [(-15, rf + 0.5), (-15, rf + 5.5), (15, rf + 5.5), (15, rf + 0.5), (-15, rf + 0.5)], M["satin"], 36, caps=False)
        D.bolt(f"lpearbolt{s}", (fx - rf - 5, s * 105.0, ear_z), (-1, 0, 0), 5, 4, M["steel"])
        # the indicator boss stands out of the clamp's side
        bases[s] = (fx, s * (105.0 + rf + 5.5), ear_z + 26)
        D.lathe(f"lpindboss{s}", (fx, s * (105.0 + rf + 1), ear_z + 26), (0, s, 0), [(0, 0), (0, 10), (5, 10), (6, 0)], M["satin"], 20)
        B.prism(f"lpindlug{s}", [(fx - 10, ear_z + 6), (fx + 10, ear_z + 6), (fx + 10, ear_z + 34), (fx - 10, ear_z + 34)], *sorted((s * (105.0 + rf + 0.5), s * (105.0 + rf + 2))), M["satin"], 0.5)
    return bases, (xf, zc)


# ---------------------------------------------------------------- tail lights
def build_tail(key, mount, M):
    """The tail light at the bike's mount: mount = (x of the back face, z, x of the front of the mount, z of
    the mount's foot). Returns the two side points the rear indicators stand on."""
    xb, zc, xm, zf = mount
    face = -1
    org = (xb, 0.0, zc)
    side = {}
    if key == "round":
        R = 34.0
        D.lathe("lptbody", (xb + 50, 0, zc), (-1, 0, 0), [(0, 0), (0, R * 0.45), (16, R * 0.85), (36, R), (48, R), (50, R - 1), (50, 0)], M["chrome"], 48, sharp=False)
        D.lathe("lptrim", (xb, 0, zc), (-1, 0, 0), [(-2, R - 0.5), (-2, R + 1.8), (2, R + 2.2), (5, R + 1), (5, R - 3), (-2, R - 3)], M["chrome"], 48, sharp=False, caps=False)
        D.lathe("lptlens", (xb, 0, zc), (-1, 0, 0), [(0, R - 2.6), (5, R - 3), (10, R - 8), (14, R - 18), (16, 0)], M["red"], 48, sharp=False, caps=False)
        ribs = [(-1, R - 3.2)] + [(-3 - k * 1.3 + (k % 2) * 1.4, (R - 4) * (1 - k / 9)) for k in range(9)]
        D.lathe("lptreflector", (xb, 0, zc), (-1, 0, 0), ribs + [(-14, 0)], M["chrome"], 48)
        D.lathe("lptbulb", (xb, 0, zc), (-1, 0, 0), [(-10, 0), (-10, 6.5), (2, 6.5), (6, 4), (7, 0)], M["tailred"], 20)
        w, depth = R, 50
    elif key in ("oval", "strip"):
        hy, hz = (52.0, 30.0) if key == "oval" else (62.0, 11.0)
        ol = [(hy * math.cos(2 * math.pi * k / 56), hz * math.sin(2 * math.pi * k / 56)) for k in range(56)] if key == "oval" else D.sell(hy, hz, 1.0, 5.0, 56)
        depth = 34 if key == "oval" else 22
        loft("lpthouse", org, face, ol, [(-depth, 0.7), (-depth * 0.5, 0.95), (-2, 1.04), (1, 1.04)], M["black"], (True, False))
        loft("lptlens", org, face, ol, [(1, 1.0), (6, 0.96), (9, 0.8), (10, 0.0)], M["red"], (False, False))
        plate("lpttray", (xb - 0.5, 0, zc), face, [(y * 0.94, z * 0.9) for y, z in ol], -2, 0, M["chrome"])
        if key == "oval":
            for iy in range(-4, 5):
                for iz in (-1, 0, 1):
                    if (iy * 10 / 46) ** 2 + (iz * 14 / 25) ** 2 > 1:
                        continue
                    plate(f"lptled{iy}{iz}", (xb + 0.5, iy * 10.0, zc + iz * 14.0), face, circle(3.0, 10), 0, 1.5, M["tailred"])
        else:
            plate("lptled", (xb + 0.5, 0, zc), face, D.sell(hy - 6, 4.5, 1.0, 5.0, 40), 0, 1.5, M["tailred"])
        w = hy
    elif key == "twin_dot":
        depth = 26
        plate("lptbar", (xb - 18, 0, zc), face, D.sell(52, 7, 1.0, 4.0, 40), -6, 6, M["satin"])
        for s in (-1, 1):
            o = (xb, s * 34.0, zc)
            D.lathe(f"lptpot{s}", (xb + 24, s * 34.0, zc), (-1, 0, 0), [(0, 0), (0, 16), (20, 18), (24, 18), (24, 0)], M["black"], 32)
            D.lathe(f"lptdot{s}", o, (-1, 0, 0), [(0, 16), (4, 14), (6, 8), (6.5, 0)], M["red"], 32, sharp=False, caps=False)
            D.lathe(f"lptdotled{s}", o, (-1, 0, 0), [(-1, 0), (-1, 11), (0.5, 11), (0.5, 0)], M["tailred"], 24)
        w = 52
    elif key == "classic":
        ol = D.chaikin([(-46, -22), (46, -22), (40, 22), (-40, 22)], 3)
        depth = 40
        loft("lpthouse", org, face, ol, [(-depth, 0.8), (-depth * 0.4, 0.98), (-2, 1.0), (0, 1.0)], M["chrome"], (True, False))
        loft("lptrim", org, face, ol, [(-1, 1.06), (3, 1.06), (4, 1.0), (4, 0.94), (0, 0.92)], M["chrome"], (False, False))
        loft("lptlens", org, face, ol, [(1, 0.95), (8, 0.88), (12, 0.6), (13, 0.0)], M["red"], (False, False))
        loft("lptreflector", org, face, ol, [(0, 0.93), (-10, 0.6), (-14, 0.2)], M["chrome"], (False, True))
        D.lathe("lptbulb", (xb - 4, 0, zc), (-1, 0, 0), [(-8, 0), (-8, 6), (2, 6), (5, 0)], M["tailred"], 16)
        plate("lptwindow", (xb - 14, 0, zc - 26), face, [(-22, -4), (22, -4), (22, 4), (-22, 4)], -12, 2, M["bulb"])
        w = 46
    elif key == "hanger":
        # a black arm dropping from the mount, the lamp on its back face, a plate light underneath
        depth = 18
        zl = zc - 34
        B.prism("lptarm", [(xm, zf + 4), (xm, zf - 10), (xb + depth, zl - 8), (xb + depth, zl + 22)], -26, 26, M["black"], 3.0)
        ol = D.sell(40, 12, 1.0, 4.0, 48)
        org = (xb, 0.0, zl)
        loft("lpthouse", org, face, ol, [(-depth, 0.9), (-2, 1.05), (1, 1.05)], M["black"], (True, False))
        loft("lptlens", org, face, ol, [(1, 1.0), (5, 0.94), (6, 0.0)], M["red"], (False, False))
        plate("lptled", (xb + 0.5, 0, zl), face, D.sell(34, 5, 1.0, 5.0, 40), 0, 1.5, M["tailred"])
        plate("lptwindow", (xb + 6, 0, zl - 14), face, [(-18, -3), (18, -3), (18, 3), (-18, 3)], -6, 2, M["bulb"])
        zc = zl
        w = 40
    else:
        raise KeyError(key)
    # a foot from the lamp forward to the mount (not for the hanger, which has its own arm)
    if key != "hanger" and xm - (xb + depth) > 3:
        B.prism("lptfoot", [(xb + depth - 4, zc - 10), (xb + depth - 4, zc + 10), (xm, zf + 10), (xm, zf - 6)], -18, 18, M["satin"] if key not in ("round", "classic") else M["chrome"], 2.0)
    for s in (-1, 1):
        side[s] = (xb + depth * 0.5, s * (w + 1.0), zc)
    return side


# ---------------------------------------------------------------- indicators
def build_indicator(name, key, base, s, face, M):
    """An indicator whose stalk leaves base outwards (y = s), its lens facing along x = face."""
    ax = (0, s, 0)
    stalk = {"led_short": 16, "led_long": 64, "chrome_short": 14, "chrome_long": 60, "bullet": 22, "flush": 4}[key]
    if key.startswith("led"):
        prof = [(0, 0), (0, 11.5), (3, 11.5), (4, 8.6)]
        k = 5
        while k < stalk - 2:
            prof += [(k, 8.6), (k + 1, 10.2), (k + 3, 10.2), (k + 4, 8.4)]
            k += 6
        prof += [(stalk, 7.0), (stalk, 0)]
        D.lathe(name + "_stalk", base, ax, prof, M["rubber"], 24)
        o = stalk
        D.lathe(name + "_pod", base, ax, [(o, 0), (o, 4.5), (o + 4, 8.2), (o + 12, 10.2), (o + 50, 10.2), (o + 61, 8.4), (o + 68, 4), (o + 69, 0)], M["plastic"], 28, sharp=False, ell=(0.74, 1.0))
        D.lathe(name + "_lens", (base[0] + face * 3.4, base[1], base[2]), ax, [(o + 8, 0), (o + 8, 7.6), (o + 14, 9.2), (o + 50, 9.2), (o + 60, 7.6), (o + 66, 3.5), (o + 67, 0)], M["glass"], 28, sharp=False, ell=(0.7, 1.0))
        for j in range(6):
            y = base[1] + s * (o + 17 + 7.4 * j)
            B.cone(name + f"_led{j}", (base[0] + face * 7.0, y, base[2]), (base[0] + face * 9.2, y, base[2]), 3.1, 2.4, M["amber"], 12)
    elif key.startswith("chrome"):
        D.lathe(name + "_stalk", base, ax, [(0, 0), (0, 9), (4, 9), (6, 6), (stalk, 6), (stalk, 0)], M["chrome"], 20)
        o = stalk - 4
        D.lathe(name + "_pod", base, ax, [(o, 0), (o + 1, 9), (o + 6, 16), (o + 16, 19.5), (o + 46, 19.5), (o + 56, 16), (o + 61, 9), (o + 62, 0)], M["chrome"], 32, sharp=False, ell=(0.8, 1.0))
        D.lathe(name + "_lens", (base[0] + face * 7, base[1], base[2]), ax, [(o + 3, 0), (o + 4, 8), (o + 8, 14.5), (o + 17, 17.6), (o + 45, 17.6), (o + 54, 14.5), (o + 58, 8), (o + 59, 0)], M["amberglass"], 32, sharp=False, ell=(0.74, 1.0))
        ribs = [(o + 6, 0), (o + 6, 7)] + [(o + 8 + j * 4, 13.6 if j % 2 else 11.2) for j in range(12)] + [(o + 56, 7), (o + 56, 0)]
        D.lathe(name + "_reflector", (base[0] + face * 5, base[1], base[2]), ax, ribs, M["chrome"], 32, ell=(0.7, 1.0))
        B.cone(name + "_bulb", (base[0] + face * 13, base[1] + s * (o + 20), base[2]), (base[0] + face * 13, base[1] + s * (o + 42), base[2]), 5, 5, M["amber"], 12)
    elif key == "bullet":
        D.lathe(name + "_stalk", base, ax, [(0, 0), (0, 8), (stalk, 6), (stalk, 0)], M["chrome"], 20)
        c_ = (base[0], base[1] + s * (stalk + 10), base[2])
        D.lathe(name + "_pod", (c_[0] - face * 34, c_[1], c_[2]), (face, 0, 0), [(0, 0), (2, 6), (8, 11), (20, 13), (40, 13), (44, 12), (44, 0)], M["chrome"], 32, sharp=False)
        D.lathe(name + "_lens", (c_[0] + face * 10, c_[1], c_[2]), (face, 0, 0), [(-1, 12), (3, 11), (7, 7), (9, 0)], M["amberglass"], 32, sharp=False, caps=False)
        D.lathe(name + "_bulb", (c_[0] + face * 10, c_[1], c_[2]), (face, 0, 0), [(-8, 0), (-8, 5), (2, 5), (4, 0)], M["amber"], 16)
    elif key == "flush":
        D.lathe(name + "_stalk", base, ax, [(0, 0), (0, 10), (stalk, 10), (stalk, 0)], M["plastic"], 20)
        org = (base[0], base[1] + s * stalk, base[2])
        blade = [(s * y, z) for y, z in D.chaikin([(0, -6), (54, -4), (60, 0), (54, 4), (0, 6)], 2)]
        plate(name + "_pod", org, face, blade if s > 0 else blade[::-1], -6, 0, M["plastic"])
        plate(name + "_lens", org, face, [(y * 0.94, z * 0.8) for y, z in (blade if s > 0 else blade[::-1])], 0, 1.2, M["amber"])
    else:
        raise KeyError(key)


# ---------------------------------------------------------------- fitting
def _bbox(prefixes):
    import numpy as np
    dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for ob in bpy.data.objects:
        if ob.type in ("MESH", "CURVE") and ob.name.startswith(prefixes):
            me = ob.evaluated_get(dg).to_mesh()
            pts += [tuple(ob.matrix_world @ v.co) for v in me.vertices]
            ob.evaluated_get(dg).to_mesh_clear()
    if not pts:
        return None
    v = np.array(pts) / B.MM
    return v.min(0), v.max(0)


def _remove(prefixes, keep=()):
    for ob in list(bpy.data.objects):
        if ob.name.startswith(prefixes) and not ob.name.startswith(keep):
            bpy.data.objects.remove(ob, do_unlink=True)


def fit(b, model, c, head=None, tail=None, ind=None):
    """Swap the bike's lamps for ours. head/tail/ind are keys (None keeps the bike's own). Returns a report
    and the names of the parts made."""
    use(b)
    M = mats()
    fam = model["family"]
    before = set(bpy.data.objects.keys())
    rep = dict(model=model.get("name", ""), family=fam)
    front_bases = rear_bases = None
    # where the bike's own indicators stand, for bikes that keep their lamps
    own_ind = {}
    for tag in ("f", "r"):
        for s in (-1, 1):
            bb = _bbox((f"ind_{tag}{s}_stalk",)) or _bbox((f"ind_{tag}{s}_",))
            if bb is not None:
                lo, hi = bb
                own_ind[(tag, s)] = ((lo[0] + hi[0]) / 2, s * min(abs(lo[1]), abs(hi[1])), (lo[2] + hi[2]) / 2)
    if head and fam in HEAD_FAMS:
        bb = _bbox(OLD_HEAD)
        if bb is not None:
            lo, hi = bb
            _remove(OLD_HEAD, keep=("lampunit",))     # fairing lamp units (Bikes V2) stay
            front_bases, at = build_headlamp(head, (hi[0] - 18, (lo[2] + hi[2]) / 2), c, M)
            rep["head"] = dict(key=head, face_x=round(at[0]), z=round(at[1]), own_face_x=round(hi[0]))
    if tail and fam in TAIL_FAMS and not (tail == "hanger" and fam in ("Classic", "Cruiser", "Chopper")):
        bb = _bbox(OLD_TAIL)
        if bb is not None:
            lo, hi = bb
            _remove(OLD_TAIL)
            zc = (lo[2] + hi[2]) / 2
            # the mount: a guard lamp's body stands on the guard, its front end is the mount; others mount at the back face
            guard = fam in ("Classic", "Cruiser", "Chopper")
            mount = (lo[0], zc, min(hi[0], lo[0] + 70) if guard else lo[0] + 30, zc)
            if fam == "Cafe":
                mount = (lo[0], zc, hi[0] + 4, zc)
            rear_bases = build_tail(tail, mount, M)
            rep["tail"] = dict(key=tail, back_x=round(lo[0]), z=round(zc))
    if ind:
        for tag, face in (("f", 1), ("r", -1)):
            if fam == "Sport":
                continue      # flush in the nose and grown out of the tail housing: the sport keeps its own
            bases = front_bases if tag == "f" else rear_bases
            if bases is None:
                if not all((tag, s) in own_ind for s in (-1, 1)):
                    continue
                bases = {s: own_ind[(tag, s)] for s in (-1, 1)}
            _remove((f"ind_{tag}-1_", f"ind_{tag}1_"))
            for s in (-1, 1):
                b_ = bases[s]
                # stand on the outside of whatever is there (tail, tail housing, number board, yoke, guard)
                for dz in (-12, 0, 12):
                    hit = D.side_hit(("tail", "advtail", "numberboard", "yoke", "guard", "rearguard", "lpt"), b_[0], b_[2] + dz, s)
                    if hit is not None and abs(hit[1]) + 1 > abs(b_[1]):
                        b_ = (b_[0], s * (abs(hit[1]) + 1), b_[2])
                build_indicator(f"lpind_{tag}{s}", ind, b_, s, face, M)
        rep["ind"] = ind
    made = [o for o in bpy.data.objects.keys() if o not in before]
    return rep, made


def built(mn, head=None, tail=None, ind=None):
    import build3d
    use(build3d)
    BR.fresh()
    m = BR.models()[mn]
    build3d.DETAIL["on"] = True
    build3d.build(m)
    top, F, uf = BR.model_top(m)
    rake = math.radians(m["geometry"]["rake"])
    cc = dict(F=F, uf=uf, nb=(-math.cos(rake), -math.sin(rake)), fk=m["fork"], top=top)
    use(build3d)
    rep, made = fit(build3d, m, cc, head, tail, ind)
    return build3d, m, cc, rep, made


def night(sc):
    """Night: the sky down to a glimmer, the studio lights off, and each lamp throwing its own light."""
    w = sc.world
    if w and w.node_tree:
        bg = w.node_tree.nodes.get("Background")
        if bg:
            bg.inputs[1].default_value = 0.015
    for ob in list(bpy.data.objects):
        if ob.type == "LIGHT":
            ob.data.energy *= 0.025
    for ob in bpy.data.objects:
        if ob.type != "MESH" or not ob.data.materials:
            continue
        nm = ob.data.materials[0].name
        if nm in ("d3_lp_white", "d3_drl", "d3_lampdip", "d3_tailred", "d3_amberled", "d3_bulb"):
            ob.data.materials[0].node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = 14.0


def lamp_light(at, aim, col, energy, size=60):
    bpy.ops.object.light_add(type="SPOT", location=B.V(*at))
    L = bpy.context.object
    L.data.energy, L.data.color, L.data.spot_size, L.data.spot_blend = energy, col, math.radians(size), 0.6
    L.rotation_euler = (Vector(aim) - Vector(at)).to_track_quat("-Z", "Y").to_euler()
    return L


def render_at(b3, path, loc, tgt, lens, res=(1300, 900), samples=40, dark=False, beams=None):
    sc = bpy.context.scene
    b3.studio(sc)
    if dark:
        night(sc)
        for at, aim, col, en, sz in beams or []:
            lamp_light(at, aim, col, en, sz)
    sc.cycles.samples = samples
    sc.render.resolution_x, sc.render.resolution_y = res
    BR.shoot(sc, path, loc, tgt, lens)


# what each picture shows: (kind, key, bike, other parts)
SHOTS = [
    ("head", "round_big", "hartwell_classic", dict(ind="chrome_short", tail="classic")),
    ("head", "round_small", "hartwell_cafe", dict(ind="chrome_short", tail="oval")),
    ("head", "halo", "corvano_naked", dict(ind="led_short", tail="strip")),
    ("head", "twin", "rheinhart_naked", dict(ind="led_short", tail="twin_dot")),
    ("head", "stacked", "kazeda_naked", dict(ind="flush", tail="hanger")),
    ("head", "rect", "varga_naked", dict(ind="led_short", tail="strip")),
    ("head", "hex", "hartwell_naked", dict(ind="flush", tail="oval")),
    ("head", "oval", "lodestar_chopper", dict(ind="bullet", tail="round")),
    ("tail", "round", "lodestar_cruiser", dict(head="round_big", ind="bullet")),
    ("tail", "oval", "hartwell_cafe", dict(head="round_small", ind="chrome_short")),
    ("tail", "strip", "corvano_naked", dict(head="halo", ind="led_short")),
    ("tail", "twin_dot", "rheinhart_naked", dict(head="twin", ind="led_short")),
    ("tail", "classic", "hartwell_classic", dict(head="round_big", ind="chrome_long")),
    ("tail", "hanger", "kazeda_naked", dict(head="stacked", ind="flush")),
    ("ind", "led_short", "corvano_naked", dict(head="halo", tail="strip")),
    ("ind", "led_long", "corvano_adventure", dict()),
    ("ind", "chrome_short", "hartwell_cafe", dict(head="round_small", tail="oval")),
    ("ind", "chrome_long", "lodestar_cruiser", dict(head="round_big", tail="classic")),
    ("ind", "bullet", "lodestar_chopper", dict(head="oval", tail="round")),
    ("ind", "flush", "kazeda_naked", dict(head="stacked", tail="hanger")),
]
NIGHT = [("corvano_naked", dict(head="halo", tail="strip", ind="led_short")), ("hartwell_classic", dict(head="round_big", tail="classic", ind="chrome_short"))]


def _centre(prefix):
    bb = _bbox(prefix)
    if bb is None:
        return None
    lo, hi = bb
    return tuple((lo + hi) / 2), tuple(lo), tuple(hi)


def shots(which=None, samples=40):
    os.makedirs(OUT, exist_ok=True)
    rep = {}
    for kind, key, mn, other in SHOTS:
        if which and key not in which:
            continue
        kw = dict(other)
        kw[kind] = key
        b3, m, cc, r, made = built(mn, kw.get("head"), kw.get("tail"), kw.get("ind"))
        rep[f"{kind}_{key}"] = r
        if kind == "head":
            c_ = _centre(("lph",))
            if not c_:
                print("NO LAMP", mn, key)
                continue
            tgt = (c_[0][0] - 20, 0, c_[0][2] - 10)
            render_at(b3, os.path.join(OUT, f"head_{key}.png"), BR.vadd(tgt, (620, -420, 160)), tgt, 50, samples=samples)
        elif kind == "tail":
            c_ = _centre(("lpt",))
            if not c_:
                print("NO TAIL", mn, key)
                continue
            tgt = (c_[0][0] + 20, 0, c_[0][2] - 10)
            render_at(b3, os.path.join(OUT, f"tail_{key}.png"), BR.vadd(tgt, (-560, -400, 180)), tgt, 50, samples=samples)
        else:
            front = "lpind_f" if _centre(("lpind_f",)) else None
            c_ = _centre((front or "lpind_r",))
            if not c_:
                print("NO IND", mn, key)
                continue
            tgt = (c_[2][0], c_[1][1], c_[0][2])
            tgt = (c_[0][0], c_[1][1] + 30, c_[0][2])
            render_at(b3, os.path.join(OUT, f"ind_{key}.png"), BR.vadd(tgt, (300, -260, 110) if front else (-300, -260, 110)), tgt, 50, samples=samples)
    for i, (mn, kw) in enumerate(NIGHT):
        if which and f"night{i}" not in which and "night" not in which:
            continue
        b3, m, cc, r, made = built(mn, kw["head"], kw["tail"], kw["ind"])
        h = _centre(("lph",))
        t = _centre(("lpt",))
        beams = []
        if h:
            at = (h[2][0] + 10, 0, h[0][2])
            beams.append((at, (at[0] + 2400, 0, 0), (1.0, 0.95, 0.88), 30000, 50))
        if t:
            at = (t[1][0] - 10, 0, t[0][2])
            beams.append((at, (at[0] - 2000, 0, 0), (1.0, 0.05, 0.03), 400, 120))
        tgt = (600, 0, 700)
        render_at(b3, os.path.join(OUT, f"night_{mn}.png"), (3300, -2100, 1300), tgt, 40, samples=samples, dark=True, beams=beams)
        render_at(b3, os.path.join(OUT, f"night_{mn}_rear.png"), (-2600, -1700, 1200), (400, 0, 650), 40, samples=samples, dark=True, beams=beams)
    path = os.path.join(OUT, "shots.json")
    old = json.load(open(path)) if os.path.exists(path) else {}
    old.update(rep)
    json.dump(old, open(path, "w"), indent=1)


OK = ("lpear | fork", "lpearclamp | fork", "lpearbolt | lpearclamp", "lpindboss | lpearclamp", "lpindlug | lpearclamp", "lpear | lpearclamp", "lpear | lph",
      "lpind_ | lpindboss", "lpind_ | lpindlug", "lph | lph", "lpt | lpt", "lpind_ | lpt", "lpt | rearguard", "lpt | guard", "lpt | tail", "lpt | frameloop",
      "lpt | looplampbracket", "lpind_ | lpind_", "lpear | lpindlug", "lpear | lpindboss", "lphbridge | lph")


def ok_pair(pair):
    a, b_ = [x.strip() for x in pair.split("|")]
    for j in OK:
        ja, jb = [x.strip() for x in j.split("|")]
        if (a.startswith(ja) and b_.startswith(jb)) or (b_.startswith(ja) and a.startswith(jb)):
            return True
    return False


def check():
    """Every headlamp, tail light and indicator on the lead bike of each family it suits: what clips."""
    res = {}
    leads = dict(BR.LEADS)
    leads["Chopper"] = "lodestar_chopper"
    for fam, mn in leads.items():
        for key in HEADLAMPS:
            if fam not in HEAD_FAMS:
                break
            b3, m, cc, r, made = built(mn, head=key)
            bad = {k: v for k, v in BR.clip_check(made).items() if not ok_pair(k)}
            res.setdefault("head", {}).setdefault(key, {})[fam] = dict(model=mn, clips=bad, fitted="head" in r, rep=r.get("head"))
            print(f"HCHECK {key:12s} {fam:10s} {bad}", flush=True)
        for key in TAILS:
            if fam not in TAIL_FAMS:
                break
            if key == "hanger" and fam in ("Classic", "Cruiser", "Chopper"):
                continue
            b3, m, cc, r, made = built(mn, tail=key, ind="led_short")
            bad = {k: v for k, v in BR.clip_check(made).items() if not ok_pair(k)}
            res.setdefault("tail", {}).setdefault(key, {})[fam] = dict(model=mn, clips=bad, fitted="tail" in r)
            print(f"TCHECK {key:12s} {fam:10s} {bad}", flush=True)
        for key in INDICATORS:
            b3, m, cc, r, made = built(mn, head="round_small" if fam in HEAD_FAMS else None, tail="round" if fam in TAIL_FAMS else None, ind=key)
            bad = {k: v for k, v in BR.clip_check(made).items() if not ok_pair(k)}
            res.setdefault("ind", {}).setdefault(key, {})[fam] = dict(model=mn, clips=bad, fitted=any(x.startswith("lpind") for x in made))
            print(f"ICHECK {key:12s} {fam:10s} {bad}", flush=True)
    os.makedirs(OUT, exist_ok=True)
    json.dump(res, open(os.path.join(OUT, "check.json"), "w"), indent=1)


if __name__ == "__main__":
    a = sys.argv[1:]
    cmd = a[0] if a else "shots"
    smp = int(a[a.index("--samples") + 1]) if "--samples" in a else 40
    rest = [x for x in a[1:] if not x.startswith("--") and x != str(smp)]
    if cmd == "shots":
        shots(rest or None, smp)
    elif cmd == "check":
        check()
