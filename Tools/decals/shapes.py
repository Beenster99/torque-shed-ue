"""Stripes and shapes shelf: about forty decals drawn by script (Tools/decals/make_decals.py).

Each function returns a dk.Decal. Long ones are 1024 x 256 (or 1024 x 384), the rest 512 square.
Colour 1 is the fill, colour 2 the line: a two-colour shape has both, a one-colour shape one.
"""
import math

from dk import (Decal, P, feather, painter, arc_pts, bez, circle, curl, diff, ellipse, grow, inter, line_path, mirror_x, outline, poly,
                quad, rect, smooth, spline, star, stroke, sym_x, taper, union, xform)

SHELF = "shapes"
ALL = []


def shape(fn):
    ALL.append(fn)
    return fn


# ----------------------------------------------------------------------------------- pinstripes


def _scroll_end(x, y, sign, size):
    """A pinstripe's classic curled end at (x, y), curling up and back."""
    c = curl(x + sign * size * 0.15, y - size * 0.55, size * 0.55, turns=1.1, sign=-sign, start=math.pi / 2, shrink=0.8)
    return c


@shape
def pin_single():
    W, H = 1024, 192
    y = H * 0.6
    body = [(120, y), (904, y)]
    left = _scroll_end(120, y, -1, 90)
    right = [(904, y)] + _scroll_end(904, y, 1, 90)
    pts = left[::-1] + body + right[1:]
    return Decal("pin_single", SHELF, "Pinstripe, curled ends", ["pinstripe", "line"], W, H,
                 fill=taper(pts, 9, 9, 15, ends="point"))


@shape
def pin_double():
    W, H = 1024, 192
    a = taper([(60, 70), (964, 70)], 18, 18, 18, ends="point")
    b = taper([(140, 122), (884, 122)], 10, 10, 10, ends="point")
    return Decal("pin_double", SHELF, "Double pinstripe", ["pinstripe", "line", "two colour"], W, H, fill=a, line=b, colours=2)


@shape
def pin_spear():
    W, H = 1024, 160
    pts = spline([(40, 80), (300, 70), (700, 70), (984, 80)], 24)
    top = taper(pts, 1, 1, 26, ends="point")
    line = taper(spline([(80, 80), (512, 80), (944, 80)], 24), 1, 1, 5, ends="point")
    return Decal("pin_spear", SHELF, "Spear stripe", ["pinstripe", "spear", "two colour"], W, H, fill=diff(top, grow(line, 3)), line=line, colours=2)


@shape
def pin_flourish():
    """A symmetric pinstriper's flourish: long lines, teardrops and curls (built up, no copy)."""
    W, H = 1024, 512
    cx = W / 2
    strokes = []
    # long sweeping arms
    strokes.append(taper(spline([(cx, 260), (cx + 120, 210), (cx + 300, 210), (cx + 470, 250)], 20), 3, 2, 11))
    strokes.append(taper(spline([(cx, 280), (cx + 140, 330), (cx + 320, 330), (cx + 440, 300)], 20), 3, 2, 9))
    # curls at the arm ends
    strokes.append(taper(curl(cx + 420, 220, 40, turns=1.0, sign=-1, start=0.4), 9, 2, 6))
    strokes.append(taper(curl(cx + 400, 330, 30, turns=0.9, sign=1, start=-0.4), 7, 2, 5))
    # teardrops near the centre
    strokes.append(taper(spline([(cx + 30, 240), (cx + 70, 150), (cx + 40, 90)], 20), 2, 2, 22))
    strokes.append(taper(spline([(cx + 30, 300), (cx + 80, 380), (cx + 50, 440)], 20), 2, 2, 18))
    strokes.append(taper(spline([(cx + 80, 270), (cx + 200, 270)], 20), 2, 2, 14))
    half = union(*strokes)
    centre = smooth([(cx, 235), (cx + 22, 270), (cx, 305), (cx - 22, 270)], 10)
    return Decal("pin_flourish", SHELF, "Pinstripe flourish", ["pinstripe", "flourish", "line"], W, H,
                 fill=union(sym_x(half, cx), centre))


@shape
def pin_eye():
    W, H = 1024, 384
    cx, cy = W / 2, H / 2
    up = spline([(80, cy), (cx, 40), (W - 80, cy)], 30)
    dn = spline([(80, cy), (cx, H - 40), (W - 80, cy)], 30)
    l = union(taper(up, 2, 2, 12), taper(dn, 2, 2, 12), circle(cx, cy, 34))
    inner = union(taper(spline([(200, cy), (cx, 110), (W - 200, cy)], 30), 2, 2, 7),
                  taper(spline([(200, cy), (cx, H - 110), (W - 200, cy)], 30), 2, 2, 7))
    return Decal("pin_eye", SHELF, "Pinstripe eye", ["pinstripe", "flourish", "two colour"], W, H, fill=l, line=inner, colours=2)


# ------------------------------------------------------------------------------------- stripes


@shape
def stripes_twin():
    W, H = 1024, 256
    a = rect(0, 30, W, 100)
    b = rect(0, 156, W, 226)
    pin = union(rect(0, 112, W, 120), rect(0, 136, W, 144))
    return Decal("stripes_twin", SHELF, "Twin racing stripes", ["stripe", "racing", "two colour"], W, H, fill=union(a, b), line=pin, colours=2)


@shape
def stripes_triple():
    W, H = 1024, 256
    a = union(rect(0, 20, W, 76), rect(0, 100, W, 156), rect(0, 180, W, 236))
    return Decal("stripes_triple", SHELF, "Three stripes", ["stripe", "racing"], W, H, fill=a)


@shape
def stripe_center():
    W, H = 1024, 256
    a = rect(0, 64, W, 192)
    b = union(rect(0, 40, W, 52), rect(0, 204, W, 216))
    return Decal("stripe_centre", SHELF, "Centre stripe with pin lines", ["stripe", "racing", "two colour"], W, H, fill=a, line=b, colours=2)


@shape
def stripe_speed():
    W, H = 1024, 256
    out = []
    for i, (y, w, x0) in enumerate([(70, 46, 60), (138, 30, 220), (192, 18, 380)]):
        out.append(poly([(x0, y - w / 2), (W - 20, y - w / 2 + 6), (W - 20, y + w / 2 - 6), (x0 + 60, y + w / 2)]))
    return Decal("stripe_speed", SHELF, "Speed lines", ["stripe", "speed"], W, H, fill=union(*out))


@shape
def stripe_hockey():
    W, H = 1024, 384
    a = poly([(0, 60), (760, 60), (900, 200), (980, 330), (900, 330), (760, 150), (0, 150)])
    a = union(diff(a, P()), circle(940, 330, 0.1))
    b = stroke(line_path([(0, 172), (740, 172), (870, 330)]), 10, cap="butt")
    return Decal("stripe_hockey", SHELF, "Hockey stick stripe", ["stripe", "two colour"], W, H, fill=a, line=b, colours=2)


@shape
def stripe_zigzag():
    W, H = 1024, 256
    pts = [(x, 70 if i % 2 == 0 else 186) for i, x in enumerate(range(0, 1025, 128))]
    return Decal("stripe_zigzag", SHELF, "Zigzag stripe", ["stripe", "zigzag"], W, H, fill=stroke(line_path(pts), 40, cap="butt", join="miter"))


@shape
def stripe_hazard():
    W, H = 1024, 192
    band = rect(0, 16, W, 176)
    bars = union(*[poly([(x, 16), (x + 70, 16), (x - 90, 176), (x - 160, 176)]) for x in range(0, 1300, 140)])
    return Decal("stripe_hazard", SHELF, "Hazard stripes", ["stripe", "two colour"], W, H, fill=diff(band, bars), line=inter(band, bars), colours=2)


@shape
def stripe_chevrons():
    W, H = 1024, 256
    out = []
    for x in range(40, 1000, 150):
        out.append(poly([(x, 30), (x + 60, 30), (x + 150, 128), (x + 60, 226), (x, 226), (x + 90, 128)]))
    return Decal("stripe_chevrons", SHELF, "Chevrons", ["stripe", "arrow"], W, H, fill=union(*out))


@shape
def stripe_dots():
    W, H = 1024, 256
    out = []
    for i in range(16):
        x = 40 + i * 62
        r = 46 * (1 - i / 17) ** 1.2 + 3
        out.append(circle(x, 128, r))
    return Decal("stripe_dots", SHELF, "Dot fade", ["stripe", "halftone"], W, H, fill=union(*out))


@shape
def chequers_strip():
    W, H = 1024, 256
    s = 64
    out = []
    for r in range(4):
        for c in range(16):
            if (r + c) % 2 == 0:
                out.append(rect(c * s, r * s, c * s + s, r * s + s))
    return Decal("chequers_strip", SHELF, "Chequered strip", ["chequers", "racing"], W, H, fill=union(*out))


@shape
def chequered_flag():
    W, H = 512, 512
    out = []
    n, s = 6, 50

    def warp(x, y):
        return (60 + x + 0, 70 + y + 24 * math.sin(x / 300 * 2 * math.pi))
    for r in range(n):
        for c in range(n + 2):
            if (r + c) % 2 == 0:
                pts = [warp(c * s, r * s), warp(c * s + s, r * s), warp(c * s + s, r * s + s), warp(c * s, r * s + s)]
                out.append(poly(pts))
    flag = union(*out)
    edge = outline(poly([warp(x, 0) for x in range(0, 401, 10)] + [warp(x, 300) for x in range(400, -1, -10)]), 8)
    pole = rect(40, 50, 58, 470, 6)
    return Decal("chequered_flag", SHELF, "Chequered flag", ["chequers", "racing", "flag", "two colour"], W, H,
                 fill=flag, line=union(edge, pole), colours=2)


# --------------------------------------------------------------------------- scallops and flames


def _scallops(n, W, H):
    # long pointed tongues from the left edge, spread top to bottom
    out = []
    for i in range(n):
        y = H * (i + 0.5) / n
        h = H / n * 0.9
        L = W * (0.95 - 0.12 * abs(i - (n - 1) / 2))
        pts = spline([(0, y - h / 2), (L * 0.35, y - h * 0.38), (L * 0.75, y - h * 0.15), (L, y),
                      (L * 0.75, y + h * 0.15), (L * 0.35, y + h * 0.38), (0, y + h / 2)], 14)
        out.append(poly(pts + [(0, y + h / 2)]))
    return union(*out)


@shape
def scallops_3():
    W, H = 1024, 384
    f = _scallops(3, W, H)
    return Decal("scallops_3", SHELF, "Scallops, three", ["scallops", "two colour"], W, H, fill=f, line=outline(f, 8), colours=2)


@shape
def scallops_5():
    W, H = 1024, 384
    f = _scallops(5, W, H)
    return Decal("scallops_5", SHELF, "Scallops, five", ["scallops", "two colour"], W, H, fill=f, line=outline(f, 7), colours=2)


def _lick(x0, y0, length, h, wav, curlup=True):
    """One flame lick from (x0, y0) running right: fat at the root, an S wave, a hooked tip."""
    sg = -1 if curlup else 1
    c = [(x0, y0), (x0 + length * 0.3, y0 - wav * 0.6), (x0 + length * 0.62, y0 + wav), (x0 + length * 0.88, y0 + wav * 0.2),
         (x0 + length * 0.97, y0 + wav * 0.2 + sg * h * 0.35), (x0 + length * 0.92, y0 + wav * 0.2 + sg * h * 0.55)]
    return taper(spline(c, 16), h, 1, h * 0.55, ends="tip")


@shape
def flames_classic():
    W, H = 1024, 384
    licks = []
    specs = [(80, 640, 96, 18, True), (140, 860, 104, -16, False), (196, 960, 110, 20, True), (252, 780, 104, -18, False),
             (308, 560, 92, 14, False)]
    for y, L, h, wav, up in specs:
        licks.append(_lick(0, y, L, h, wav, up))
    root = rect(0, 30, 90, 354, 0)
    f = union(root, *licks)
    return Decal("flames_classic", SHELF, "Hot rod flames", ["flames", "hot rod", "two colour"], W, H, fill=f, line=outline(f, 9), colours=2)


@shape
def flames_tribal():
    W, H = 1024, 384
    out = []
    for i, (y, L, sgn) in enumerate([(90, 900, 1), (190, 1000, -1), (290, 820, 1)]):
        a = spline([(0, y - 50), (L * 0.4, y - 40 - 30 * sgn), (L * 0.75, y - 10 * sgn), (L, y + 30 * sgn)], 20)
        b = spline([(L, y + 30 * sgn), (L * 0.7, y + 18), (L * 0.35, y + 46), (0, y + 50)], 20)
        out.append(poly(a + b[1:]))
        # a barb off each
        bx = L * 0.45
        out.append(poly([(bx, y - 20 * sgn), (bx + 200, y - 95 * sgn), (bx + 120, y - 10 * sgn)]))
    return Decal("flames_tribal", SHELF, "Tribal flames", ["flames", "tribal"], W, H, fill=union(*out))


@shape
def flames_ghost():
    W, H = 1024, 384
    licks = []
    for y, L, h, wav, up in [(100, 720, 100, 18, True), (192, 960, 110, -16, False), (284, 640, 100, 16, False)]:
        licks.append(_lick(0, y, L, h, wav, up))
    root = rect(-20, 40, 60, 344)
    f = union(root, *licks)
    line = diff(outline(f, 7), rect(-30, 0, 6, H))
    return Decal("flames_ghost", SHELF, "Ghost flames (outline only)", ["flames", "outline"], W, H, line=line, colours=1)


# ---------------------------------------------------------------------------------- lightning


@shape
def bolt_single():
    W, H = 512, 512
    f = poly([(300, 20), (150, 270), (250, 270), (170, 492), (380, 210), (275, 210), (360, 20)])
    return Decal("bolt_single", SHELF, "Lightning bolt", ["lightning", "two colour"], W, H, fill=f, line=outline(f, 10), colours=2)


@shape
def bolt_long():
    W, H = 1024, 256
    pts = [(10, 160), (330, 70), (360, 120), (640, 40), (660, 110), (1014, 30), (690, 160), (670, 120), (380, 200), (350, 150)]
    f = poly(pts)
    return Decal("bolt_long", SHELF, "Long lightning", ["lightning", "stripe", "two colour"], W, H, fill=f, line=outline(f, 8), colours=2)


# ------------------------------------------------------------------------------------ tribal


@shape
def tribal_blade():
    W, H = 1024, 384
    cx = W / 2
    a = poly(spline([(cx, 190), (cx + 120, 120), (cx + 330, 90), (cx + 500, 40)], 16)
             + spline([(cx + 500, 40), (cx + 380, 140), (cx + 200, 190), (cx + 80, 230)], 16))
    b = poly(spline([(cx + 60, 230), (cx + 200, 260), (cx + 330, 330), (cx + 420, 370)], 16)
             + spline([(cx + 420, 370), (cx + 260, 330), (cx + 120, 300), (cx + 30, 260)], 16))
    half = union(a, b)
    centre = poly([(cx, 120), (cx + 40, 200), (cx, 300), (cx - 40, 200)])
    return Decal("tribal_blade", SHELF, "Tribal blades", ["tribal"], W, H, fill=union(sym_x(half, cx), centre))


@shape
def tribal_swirl():
    W, H = 512, 512
    out = []
    for k in range(3):
        c = curl(256, 256, 220, turns=0.55, sign=1, start=k * 2 * math.pi / 3, shrink=0.85, n=60)
        out.append(taper(c, 58, 4, 40, ends="round"))
        out.append(poly([c[20], (c[20][0] + 60 * math.cos(k * 2.1 + 1), c[20][1] + 60 * math.sin(k * 2.1 + 1)), c[26]]))
    return Decal("tribal_swirl", SHELF, "Tribal swirl", ["tribal", "swirl"], W, H, fill=union(*out, circle(256, 256, 40)))


@shape
def tribal_spike():
    W, H = 1024, 256
    out = [poly(spline([(0, 100), (500, 110), (1014, 128)], 20) + spline([(1014, 128), (500, 150), (0, 160)], 20))]
    for x, s in [(200, -1), (380, 1), (560, -1), (720, 1)]:
        base = 128 + s * 22
        out.append(poly([(x, base), (x + 160, base + s * 90), (x + 70, base)]))
    return Decal("tribal_spike", SHELF, "Spiked stripe", ["tribal", "stripe"], W, H, fill=union(*out))


# ------------------------------------------------------------------------------- panels & plates


@shape
def panel_rounded():
    W, H = 1024, 384
    f = rect(30, 40, 994, 344, 70)
    l = outline(rect(56, 66, 968, 318, 50), 8)
    return Decal("panel_rounded", SHELF, "Tank panel with pinstripe", ["panel", "two colour"], W, H, fill=f, line=l, colours=2)


@shape
def panel_teardrop():
    W, H = 1024, 384
    pts = spline([(40, 200), (220, 60), (640, 50), (990, 150), (990, 250), (640, 334), (220, 330)], 18, closed=True)
    f = poly(pts)
    return Decal("panel_teardrop", SHELF, "Teardrop panel", ["panel", "two colour"], W, H, fill=f, line=outline(grow(f, -26), 7), colours=2)


@shape
def panel_coffin():
    W, H = 1024, 384
    f = poly([(40, 150), (160, 40), (980, 90), (980, 294), (160, 344), (40, 234)])
    return Decal("panel_coffin", SHELF, "Coffin panel", ["panel", "two colour"], W, H, fill=f, line=outline(grow(f, -22), 8), colours=2)


@shape
def roundel():
    W, H = 512, 512
    f = circle(256, 256, 236)
    l = diff(circle(256, 256, 236), circle(256, 256, 206))
    return Decal("roundel", SHELF, "Number roundel", ["number", "racing", "two colour"], W, H, fill=diff(f, l), line=l, colours=2)


@shape
def race_oval():
    W, H = 512, 384
    f = ellipse(256, 192, 240, 176)
    l = diff(f, ellipse(256, 192, 214, 150))
    return Decal("race_oval", SHELF, "Number oval", ["number", "racing", "two colour"], W, H, fill=diff(f, l), line=l, colours=2)


@shape
def race_plate():
    W, H = 512, 384
    f = rect(16, 16, 496, 368, 44)
    l = diff(f, rect(40, 40, 472, 344, 26))
    return Decal("race_plate", SHELF, "Number plate", ["number", "racing", "two colour"], W, H, fill=diff(f, l), line=l, colours=2)


@shape
def shield():
    W, H = 512, 512
    f = poly(spline([(40, 50), (256, 30), (472, 50), (460, 260), (256, 490), (52, 260)], 14, closed=True))
    return Decal("shield", SHELF, "Shield", ["shield", "badge", "two colour"], W, H, fill=f, line=outline(grow(f, -22), 10), colours=2)


# -------------------------------------------------------------------------------- wings, banners


def _wing_pieces(cx, cy, span, h, n=6):
    """The right wing (from cx outwards), back to front: long flight feathers hanging from a
    curved arm (the longest at the outside), then the arm's coverts over their roots."""
    pieces = []
    arm = lambda s: (cx + span * (0.05 + 0.80 * s), cy - h * 0.30 * math.sin(s * math.pi * 0.85) + h * 0.05)
    for i in range(n - 1, -1, -1):
        s = i / (n - 1)
        root = arm(s)
        pieces.append(feather(root, 80 - 58 * s, h * (0.50 + 0.45 * s), span * 0.21, curve=0.05))
    top = []
    for k in range(n):
        s = k / (n - 1)
        x, y = arm(s)
        top.append((x, y))
    cov = taper(spline(top, 12), h * 0.28, h * 0.10, h * 0.30, ends="round")
    pieces.append(cov)
    return pieces


def _wing_layers(cx, cy, span, h, n, mid):
    f, l = painter(_wing_pieces(cx, cy, span, h, n), 7)
    return sym_x(f, mid), sym_x(l, mid)


@shape
def wings_spread():
    W, H = 1024, 384
    cx = W / 2
    f, l = _wing_layers(cx + 40, 120, 430, 280, 7, cx)
    body = ellipse(cx, 190, 64, 120)
    return Decal("wings_spread", SHELF, "Spread wings (plain)", ["wings", "badge", "two colour"], W, H,
                 fill=union(f, body), line=union(l, outline(body, 8)), colours=2).fitted()


@shape
def wings_badge():
    W, H = 1024, 256
    cx = W / 2
    f, l = _wing_layers(cx + 80, 100, 360, 170, 6, cx)
    ring = diff(circle(cx, 128, 110), circle(cx, 128, 84))
    return Decal("wings_badge", SHELF, "Winged roundel", ["wings", "badge", "number", "two colour"], W, H,
                 fill=union(diff(f, circle(cx, 128, 110)), ring), line=union(diff(l, circle(cx, 128, 104)), outline(circle(cx, 128, 110), 7), outline(circle(cx, 128, 84), 7)), colours=2).fitted(6)


@shape
def banner_straight():
    W, H = 1024, 256
    body = rect(120, 50, 904, 170)
    tails = union(poly([(0, 90), (150, 90), (150, 210), (0, 210), (60, 150)]), poly([(1024, 90), (874, 90), (874, 210), (1024, 210), (964, 150)]))
    folds = union(poly([(120, 170), (150, 210), (150, 170)]), poly([(904, 170), (874, 210), (874, 170)]))
    f = union(body, tails)
    return Decal("banner_straight", SHELF, "Banner for lettering", ["banner", "lettering", "two colour"], W, H, fill=f,
                 line=union(outline(f, 8), folds), shade=union(tails), colours=2)


@shape
def banner_curved():
    W, H = 1024, 384
    top = spline([(110, 140), (512, 40), (914, 140)], 30)
    bot = spline([(110, 260), (512, 160), (914, 260)], 30)
    body = poly(top + bot[::-1])
    lt = poly([(0, 210), (150, 170), (170, 300), (20, 330), (70, 270)])
    rt = mirror_x(lt, 512)
    f = union(body, lt, rt)
    return Decal("banner_curved", SHELF, "Curved banner", ["banner", "lettering", "two colour"], W, H, fill=f,
                 line=outline(f, 8), shade=union(lt, rt), colours=2)


# ------------------------------------------------------------------------------------ symbols


@shape
def star_five():
    W, H = 512, 512
    f = star(256, 270, 240, 96)
    return Decal("star_five", SHELF, "Star", ["star", "two colour"], W, H, fill=f, line=outline(grow(f, -20), 8), colours=2)


@shape
def star_nautical():
    W, H = 512, 512
    cx, cy, R, r = 256, 262, 236, 92
    halves_a, halves_b = [], []
    for i in range(5):
        a = math.radians(-90 + i * 72)
        a0 = math.radians(-90 + i * 72 - 36)
        a1 = math.radians(-90 + i * 72 + 36)
        tip = (cx + R * math.cos(a), cy + R * math.sin(a))
        l = (cx + r * math.cos(a0), cy + r * math.sin(a0))
        rr = (cx + r * math.cos(a1), cy + r * math.sin(a1))
        halves_a.append(poly([(cx, cy), l, tip]))
        halves_b.append(poly([(cx, cy), tip, rr]))
    full = union(*halves_a, *halves_b)
    return Decal("star_nautical", SHELF, "Nautical star", ["star", "tattoo", "two colour"], W, H,
                 fill=union(*halves_a), line=union(outline(full, 9), union(*halves_b)), colours=2)


@shape
def sunburst():
    W, H = 512, 512
    out = []
    for i in range(16):
        a = math.radians(i * 22.5)
        out.append(poly([(256, 256), (256 + 250 * math.cos(a - 0.09), 256 + 250 * math.sin(a - 0.09)),
                         (256 + 250 * math.cos(a + 0.09), 256 + 250 * math.sin(a + 0.09))]))
    return Decal("sunburst", SHELF, "Sunburst", ["rays"], W, H, fill=diff(union(*out), circle(256, 256, 70)))


@shape
def target():
    W, H = 512, 512
    f = union(diff(circle(256, 256, 240), circle(256, 256, 170)), circle(256, 256, 100))
    l = diff(circle(256, 256, 170), circle(256, 256, 100))
    return Decal("target", SHELF, "Target roundel", ["roundel", "mod", "two colour"], W, H, fill=f, line=l, colours=2)


@shape
def diamonds_strip():
    W, H = 1024, 192
    out = [poly([(x, 96), (x + 50, 20), (x + 100, 96), (x + 50, 172)]) for x in range(12, 1000, 112)]
    return Decal("diamonds_strip", SHELF, "Diamond chain", ["stripe", "diamond"], W, H, fill=union(*out),
                 line=stroke(line_path([(0, 96), (W, 96)]), 6, cap="butt"), colours=2)


@shape
def hex_strip():
    W, H = 1024, 256
    out = []
    r = 46
    for i in range(12):
        for j in range(2):
            x = 50 + i * 80 + (40 if j else 0)
            y = 80 + j * 70
            hx = poly([(x + r * math.cos(math.radians(60 * k + 30)), y + r * math.sin(math.radians(60 * k + 30))) for k in range(6)])
            out.append(outline(hx, 8))
    return Decal("hex_strip", SHELF, "Honeycomb strip", ["stripe", "hex"], W, H, fill=union(*out))


ALL_NAMES = [f.__name__ for f in ALL]
