"""Bold line artwork shelf: tattoo flash in the old style (a strong outline, flat fills, a little
shading), drawn by script from plain geometry (Tools/decals/make_decals.py). Two colours: colour 1
the fill, colour 2 the ink; the shading is a darker tone of colour 1.

Every piece is built from simple parts (skull, rose, blade, heart, wing...) in a 512 unit square,
y down. The first sheet is twenty pieces, the skulls and roses first, to prove the style.
"""
import math

from dk import (Decal, P, arc_pts, circle, curl, diff, ellipse, feather, grow, inter, line_path, mirror_x, outline,
                painter, poly, rect, smooth, spline, star, stroke, sym_x, taper, union, xform)

SHELF = "art"
ALL = []
INK = 9.0     # the main outline at 512
FINE = 4.5    # inner detail lines


def art(fn):
    ALL.append(fn)
    return fn


class Parts:
    """Fill, ink and shade gathered up, with pieces laid back to front."""

    def __init__(self):
        self.fill, self.line, self.shade = P(), P(), P()
        self.solid = P()   # parts filled with ink (eye sockets, a dagger's grip)

    def lay(self, shape, ink=INK, fill=True):
        """Lays a shape over what's there: it hides what it covers."""
        cover = shape
        self.line = diff(self.line, cover)
        self.shade = diff(self.shade, cover)
        self.solid = diff(self.solid, cover)
        if fill:
            self.fill = union(self.fill, shape)
        else:
            self.fill = diff(self.fill, cover)
        self.line = union(self.line, outline(shape, ink))
        return self

    def ink(self, shape):
        self.line = union(self.line, shape)
        return self

    def black(self, shape):
        self.solid = union(self.solid, shape)
        return self

    def tone(self, shape):
        self.shade = union(self.shade, shape)
        return self

    def merge(self, other):
        """Lays all of another set of parts over this one."""
        whole = union(other.fill, other.line, other.solid)
        self.fill = union(diff(self.fill, whole), other.fill)
        self.line = union(diff(self.line, whole), other.line)
        self.shade = union(diff(self.shade, whole), other.shade)
        self.solid = union(diff(self.solid, whole), other.solid)
        return self

    def moved(self, s=1.0, rot=0.0, tx=0.0, ty=0.0, cx=256, cy=256):
        q = Parts()
        for k in ("fill", "line", "shade", "solid"):
            setattr(q, k, xform(getattr(self, k), sx=s, sy=s, rot=rot, tx=tx, ty=ty, cx=cx, cy=cy))
        return q

    def flipped(self, cx=256):
        q = Parts()
        for k in ("fill", "line", "shade", "solid"):
            setattr(q, k, mirror_x(getattr(self, k), cx))
        return q

    def decal(self, id, name, tags, w=512, h=512, margin=14):
        line = union(self.line, self.solid)
        fill = self.fill
        d = Decal(id, SHELF, name, tags, w, h, fill=fill, line=line, shade=inter(self.shade, fill), colours=2)
        return d.fitted(margin)


def U(pts, cx, cy, s):
    """Unit points (-1..1) to pixels."""
    return [(cx + x * s, cy + y * s) for x, y in pts]


def mirror_pts(pts):
    return pts + [(-x, y) for x, y in reversed(pts)]


# ------------------------------------------------------------------------------------- skull


def skull(cx=256, cy=256, s=180, jaw=True, grin=True):
    """A front-on flash skull: dome, deep sockets, an upside-down heart nose, cheekbones, teeth."""
    k = Parts()
    half = [(0, -1.12), (0.42, -1.07), (0.78, -0.82), (0.97, -0.40), (0.98, -0.02), (0.90, 0.26), (0.80, 0.36),
            (0.70, 0.52), (0.56, 0.62)]
    if jaw:
        half += [(0.58, 0.80), (0.48, 1.02), (0.25, 1.13), (0.0, 1.16)]
    else:
        half += [(0.45, 0.82), (0.0, 0.86)]
    pts = half + [(-x, y) for x, y in reversed(half[:-1])]
    head = poly(spline(U(pts, cx, cy, s), 10, closed=True))
    k.lay(head)
    # sockets: big, the outer corner lower, a heavy brow
    sock = [(0.10, -0.12), (0.30, -0.24), (0.58, -0.22), (0.72, -0.04), (0.68, 0.17), (0.48, 0.26), (0.24, 0.22), (0.10, 0.08)]
    for sg in (1, -1):
        sp = poly(spline(U([(x * sg, y) for x, y in sock], cx, cy, s), 10, closed=True))
        k.black(sp)
        k.tone(diff(grow(sp, s * 0.10), sp))
        # a glint of light in each socket
    # nose
    nose = [(0, 0.24), (0.07, 0.30), (0.13, 0.46), (0.07, 0.50), (0, 0.44)]
    k.black(poly(spline(U(mirror_pts(nose), cx, cy, s), 6, closed=True)))
    # cheekbones and temples
    for sg in (1, -1):
        k.ink(taper(spline(U([(0.86 * sg, 0.24), (0.66 * sg, 0.34), (0.50 * sg, 0.36)], cx, cy, s), 12), FINE * 1.6, 1, FINE * 1.8, ends="point"))
        k.ink(taper(spline(U([(0.84 * sg, -0.50), (0.80 * sg, -0.22), (0.86 * sg, 0.02)], cx, cy, s), 12), FINE * 1.4, 1, FINE * 1.6, ends="point"))
        k.tone(poly(spline(U([(0.98 * sg, -0.40), (0.82 * sg, -0.30), (0.80 * sg, 0.05), (0.90 * sg, 0.26), (0.98 * sg, -0.02)], cx, cy, s), 8, closed=True)))
        k.tone(poly(spline(U([(0.80 * sg, 0.36), (0.62 * sg, 0.38), (0.56 * sg, 0.62), (0.70 * sg, 0.52)], cx, cy, s), 8, closed=True)))
    # a crack on the dome
    k.ink(taper(U([(-0.30, -1.08), (-0.22, -0.88), (-0.30, -0.74), (-0.18, -0.60)], cx, cy, s), FINE * 1.3, 1, FINE * 1.3, ends="point"))
    # teeth: upper row, and the lower on the jaw
    top, mid, bot = 0.58, 0.76, 0.94
    xs = [-0.40, -0.27, -0.135, 0.0, 0.135, 0.27, 0.40]
    rows = [(top, mid)] + ([(mid, bot)] if jaw else [])
    for y0, y1 in rows:
        band = poly(spline(U([(-0.44, y0), (0, y0 - 0.03), (0.44, y0), (0.42, y1), (0, y1 + 0.03), (-0.42, y1)], cx, cy, s), 6, closed=True))
        k.lay(band, FINE * 1.6)
        for x in xs[1:-1]:
            k.ink(taper(U([(x, y0 + 0.02), (x * 1.02, y1 - 0.02)], cx, cy, s), FINE, FINE, FINE * 1.2, ends="round"))
    if jaw:
        for sg in (1, -1):
            k.ink(taper(spline(U([(0.56 * sg, 0.62), (0.55 * sg, 0.78), (0.44 * sg, 0.92)], cx, cy, s), 10), FINE * 1.5, 1, FINE * 1.5, ends="point"))
    return k


# -------------------------------------------------------------------------------------- rose


def rose(cx=256, cy=256, s=150, leaves=True, rot=0.0):
    """A flash rose seen from a little above: back petals, a cupped bowl, a rolled centre."""
    k = Parts()
    # leaves behind the flower
    if leaves:
        for ang, L in ((205, 1.55), (-20, 1.45), (110, 1.25)):
            a = math.radians(ang + rot)
            root = (cx + math.cos(a) * s * 0.55, cy + math.sin(a) * s * 0.55)
            lf = leaf(root, ang + rot, s * L * 0.75, s * 0.42)
            k.merge(lf)
    petals = []
    # back petals (the wide outer ring)
    for i, (ang, r) in enumerate([(-150, 0.95), (-90, 1.0), (-30, 0.95), (30, 0.92), (150, 0.92)]):
        a = math.radians(ang + rot)
        px, py = cx + math.cos(a) * s * 0.42, cy + math.sin(a) * s * 0.36
        p = poly(spline(U([(-0.62, 0.15), (-0.55, -0.35), (-0.2, -0.62), (0.2, -0.62), (0.55, -0.35), (0.62, 0.15), (0, 0.35)], 0, 0, s * r), 8, closed=True))
        p = xform(p, rot=ang + 90 + rot, tx=px, ty=py)
        petals.append(p)
    # the bowl: a cupped front petal
    bowl = poly(spline(U([(-0.78, -0.05), (-0.55, 0.45), (0, 0.66), (0.55, 0.45), (0.78, -0.05), (0.30, 0.12), (-0.30, 0.12)], cx, cy + s * 0.12, s), 10, closed=True))
    # two side wraps
    lw = poly(spline(U([(-0.80, -0.30), (-0.70, 0.25), (-0.30, 0.40), (-0.18, 0.05), (-0.42, -0.30)], cx, cy, s), 8, closed=True))
    rw = mirror_x(lw, cx)
    # the centre: a rolled cup
    cup = ellipse(cx, cy - s * 0.10, s * 0.40, s * 0.26)
    for p in petals:
        k.lay(p)
    k.lay(cup)
    k.lay(lw)
    k.lay(rw)
    k.lay(bowl)
    # rolled centre lines
    sp = curl(cx, cy - s * 0.12, s * 0.30, turns=1.35, sign=1, start=math.pi * 0.9, shrink=0.75)
    k.ink(taper([(x, cy - s * 0.12 + (y - cy + s * 0.12) * 0.62) for x, y in sp], INK * 0.9, 1, INK, ends="point"))
    # shading in the throat and under the bowl's lip
    k.tone(ellipse(cx, cy - s * 0.06, s * 0.30, s * 0.14))
    k.tone(inter(bowl, ellipse(cx, cy + s * 0.12, s * 0.70, s * 0.22)))
    for p in petals:
        k.tone(inter(p, grow(cup, s * 0.10)))
    # petal edge flicks
    for sg in (1, -1):
        k.ink(taper(spline(U([(0.55 * sg, 0.40), (0.35 * sg, 0.52), (0.15 * sg, 0.55)], cx, cy + s * 0.12, s), 8), FINE, 1, FINE * 1.4))
    return k


def leaf(root, ang, L, wd):
    k = Parts()
    a = math.radians(ang)
    dx, dy = math.cos(a), math.sin(a)
    nx, ny = -dy, dx
    pts = []
    for i in range(21):
        t = i / 20
        w = wd / 2 * math.sin(t * math.pi) ** 0.8
        pts.append((root[0] + dx * L * t + nx * w, root[1] + dy * L * t + ny * w))
    for i in range(20, -1, -1):
        t = i / 20
        w = wd / 2 * math.sin(t * math.pi) ** 0.8
        pts.append((root[0] + dx * L * t - nx * w * 0.9, root[1] + dy * L * t - ny * w * 0.9))
    shape = poly(pts)
    k.lay(shape)
    k.ink(taper([(root[0] + dx * L * 0.05, root[1] + dy * L * 0.05), (root[0] + dx * L * 0.85, root[1] + dy * L * 0.85)], FINE * 1.5, 1, FINE * 1.2, ends="tip"))
    half = poly([(root[0] + dx * L * t - nx * wd / 2 * math.sin(t * math.pi) ** 0.8 * 0.9, root[1] + dy * L * t - ny * wd / 2 * math.sin(t * math.pi) ** 0.8 * 0.9) for t in [i / 20 for i in range(21)]]
                + [(root[0] + dx * L, root[1] + dy * L), root])
    k.tone(half)
    return k


# ------------------------------------------------------------------------------------- blades


def dagger(x0, y0, x1, y1, w):
    """A dagger from its pommel (x0, y0) to its point (x1, y1): grip and guard in ink, the blade
    filled with a shaded half and a centre line."""
    k = Parts()
    L = math.dist((x0, y0), (x1, y1))
    ang = math.degrees(math.atan2(y1 - y0, x1 - x0))

    def place(path):
        return xform(path, rot=ang, tx=x0, ty=y0)
    g = 0.24 * L
    blade = poly([(g + 0.02 * L, -w * 0.5), (L * 0.80, -w * 0.42), (L, 0), (L * 0.80, w * 0.42), (g + 0.02 * L, w * 0.5)])
    guard = poly(spline([(g - w * 0.25, -w * 1.5), (g + w * 0.30, -w * 1.25), (g + w * 0.30, w * 1.25), (g - w * 0.25, w * 1.5),
                         (g - w * 0.05, 0)], 6, closed=True))
    grip = rect(w * 0.6, -w * 0.33, g, w * 0.33, w * 0.15)
    pommel = circle(w * 0.40, 0, w * 0.48)
    k.lay(place(blade))
    k.tone(place(poly([(g + 0.02 * L, 0), (L * 0.80, 0), (L, 0), (L * 0.80, w * 0.42), (g + 0.02 * L, w * 0.5)])))
    k.ink(place(taper([(g + w * 0.4, 0), (L * 0.82, 0)], FINE * 1.3, 1, FINE * 1.3, ends="tip")))
    k.lay(place(grip))
    k.black(place(diff(grip, P())))
    for t in (0.35, 0.55, 0.75):
        k.ink(place(stroke(line_path([(g * t + w * 0.2, -w * 0.33), (g * t + w * 0.45, w * 0.33)]), FINE * 0.8)))
    k.lay(place(guard))
    k.lay(place(pommel))
    k.tone(place(diff(pommel, circle(w * 0.25, -w * 0.15, w * 0.30))))
    k.tone(place(inter(guard, rect(g - w, 0, g + w, w * 2))))
    return k


# ------------------------------------------------------------------------------------- hearts


def heart(cx, cy, s):
    pts = [(0, -0.55), (0.30, -0.95), (0.82, -0.88), (1.0, -0.40), (0.78, 0.15), (0.30, 0.60), (0, 0.95)]
    return poly(spline(U(mirror_pts(pts), cx, cy, s), 10, closed=True))


def flames_up(cx, base_y, width, height, n=5, seed=0):
    """A crown of flames rising from a base line."""
    out = []
    for i in range(n):
        t = (i + 0.5) / n
        x = cx - width / 2 + width * t
        hgt = height * (0.6 + 0.4 * math.sin(t * math.pi)) * (0.85 + 0.15 * ((i * 7 + seed) % 3) / 2)
        lean = (t - 0.5) * 0.5
        sgn = 1 if i % 2 else -1
        c = [(x, base_y), (x + lean * 40 - sgn * 10, base_y - hgt * 0.45), (x + lean * 80 + sgn * 18, base_y - hgt * 0.8),
             (x + lean * 90 + sgn * 6, base_y - hgt)]
        out.append(taper(spline(c, 12), width / n * 1.25, 1, width / n * 1.0, ends="tip"))
    return union(*out)


# =================================================================================== the pieces


@art
def skull_front():
    k = skull()
    return k.decal("skull_front", "Skull", ["skull", "death"])


@art
def skull_roses():
    k = Parts()
    k.merge(rose(120, 380, 95, rot=-10))
    k.merge(rose(392, 380, 95, rot=10))
    k.merge(skull(256, 230, 150))
    return k.decal("skull_roses", "Skull with roses", ["skull", "rose", "death"])


@art
def skull_wings():
    k = Parts()
    pieces = []
    cx, cy, span, h = 256 + 120, 210, 330, 300
    arm = lambda s: (cx + span * (0.02 + 0.85 * s), cy - h * 0.38 * math.sin(s * math.pi * 0.85) + h * 0.05)
    n = 7
    for i in range(n - 1, -1, -1):
        s = i / (n - 1)
        pieces.append(feather(arm(s), 82 - 58 * s, h * (0.55 + 0.45 * s), span * 0.20, curve=0.05))
    cov = taper(spline([arm(i / 6) for i in range(7)], 12), h * 0.30, h * 0.10, h * 0.32, ends="round")
    pieces.append(cov)
    fill, line = painter(pieces, INK)
    wing = Parts()
    wing.fill, wing.line = sym_x(fill, 256), sym_x(line, 256)
    # feather shafts
    for i in range(n):
        s = i / (n - 1)
        r = arm(s)
        a = math.radians(82 - 58 * s)
        L = h * (0.55 + 0.45 * s)
        wing.ink(sym_x(taper([(r[0] + math.cos(a) * L * 0.25, r[1] + math.sin(a) * L * 0.25), (r[0] + math.cos(a) * L * 0.8, r[1] + math.sin(a) * L * 0.8)], FINE, 1, FINE, ends="tip"), 256))
    wing.tone(sym_x(inter(cov, rect(0, 0, 1024, 1024)), 256))
    k.merge(wing)
    k.merge(skull(256, 250, 165))
    return k.decal("skull_wings", "Winged skull", ["skull", "wings", "death"], w=1024, h=512)


@art
def skull_pistons():
    k = Parts()
    for sg in (1, -1):
        p = Parts()
        L = 420
        # rod and piston crown, crossed behind the skull
        rod = rect(-L / 2, -16, L / 2 - 80, 16, 8)
        crown = rect(L / 2 - 110, -58, L / 2, 58, 10)
        eye = circle(-L / 2, 0, 40)
        p.lay(rod)
        p.lay(eye)
        p.black(circle(-L / 2, 0, 18))
        p.lay(crown)
        for x in (L / 2 - 92, L / 2 - 74, L / 2 - 56):
            p.ink(stroke(line_path([(x, -58), (x, 58)]), FINE))
        p.black(circle(L / 2 - 30, 0, 12))
        p.tone(rect(L / 2 - 110, 0, L / 2, 58))
        p.tone(rect(-L / 2 + 40, 0, L / 2 - 110, 16))
        k.merge(p.moved(rot=sg * 40, tx=256, ty=280, cx=0, cy=0))
    k.merge(skull(256, 240, 135))
    return k.decal("skull_pistons", "Skull and crossed pistons", ["skull", "piston", "engine", "death"])


@art
def skull_flaming():
    k = Parts()
    f = union(flames_up(256, 300, 300, 290, n=7), ellipse(256, 290, 150, 60))
    k.lay(f)
    k.tone(inter(f, rect(0, 200, 512, 360)))
    k.merge(skull(256, 320, 140))
    return k.decal("skull_flaming", "Flaming skull", ["skull", "flames", "death"])


@art
def skull_sugar():
    k = skull(256, 256, 190)
    # flowers round the sockets and a web on the brow, in ink
    for sg in (1, -1):
        cx, cy = 256 + sg * 76, 256
        for i in range(8):
            a = i * math.pi / 4
            k.ink(circle(cx + math.cos(a) * 92, cy + math.sin(a) * 74, 9))
    k.ink(outline(heart(256, 140, 28), FINE * 1.2))
    for i in range(5):
        a = math.radians(200 + i * 35)
        k.ink(taper([(256 + math.cos(a) * 60, 130 + math.sin(a) * 40), (256 + math.cos(a) * 120, 130 + math.sin(a) * 75)], FINE, 1, FINE * 1.2))
    k.ink(stroke(line_path([(140, 330), (372, 330)]), FINE))
    for x in range(160, 360, 24):
        k.ink(stroke(line_path([(x, 320), (x, 340)]), FINE * 0.8))
    return k.decal("skull_sugar", "Sugar skull", ["skull", "flowers"])


@art
def rose_single():
    k = rose(256, 230, 170)
    stem = taper(spline([(256, 360), (240, 420), (262, 500)], 12), 16, 10, 16, ends="round")
    s = Parts()
    s.lay(stem)
    s.tone(stem)
    s.merge(leaf((248, 430), 160, 120, 60))
    s.merge(k)
    return s.decal("rose", "Rose", ["rose", "flower"])


@art
def rose_dagger():
    k = Parts()
    k.merge(dagger(256, 20, 256, 500, 34))
    k.merge(rose(256, 250, 120))
    return k.decal("rose_dagger", "Rose and dagger", ["rose", "dagger"])


@art
def dagger_heart():
    k = Parts()
    h = heart(256, 270, 190)
    hp = Parts()
    hp.lay(h)
    hp.tone(diff(h, heart(236, 250, 175)))
    d = dagger(-20, -60, 430, 490, 32)
    # the blade goes in behind the heart's front and comes out below
    k.merge(d)
    k.merge(hp)
    tip = Parts()
    blade_tip = inter(union(d.fill), diff(rect(330, 330, 512, 512), grow(h, 2)))
    tip.lay(blade_tip, fill=True)
    k.merge(tip)
    # a highlight on the heart, and drops
    k.black(ellipse(200, 190, 26, 14, rot=-30))
    for x, y, r in ((370, 470, 13), (392, 505 - 20, 9)):
        drop = poly(spline([(x, y - r * 2.4), (x + r, y), (x, y + r), (x - r, y)], 6, closed=True))
        k.lay(drop)
    return k.decal("dagger_heart", "Dagger through a heart", ["heart", "dagger"])


@art
def sacred_heart():
    k = Parts()
    f = flames_up(256, 150, 120, 120, n=3)
    k.lay(f)
    k.tone(inter(f, rect(0, 70, 512, 150)))
    h = heart(256, 290, 170)
    k.lay(h)
    k.tone(diff(h, heart(236, 270, 155)))
    # band of thorns round it
    band = taper(spline([(110, 280), (256, 320), (402, 280)], 20), 24, 24, 26, ends="round")
    k.lay(band, FINE * 1.6)
    for i in range(10):
        t = (i + 0.5) / 10
        x = 110 + 292 * t
        y = 280 + 40 * math.sin(t * math.pi)
        k.ink(poly([(x - 6, y), (x + 4, y - 26 if i % 2 else y + 26), (x + 8, y)]))
    k.black(ellipse(196, 220, 22, 12, rot=-30))
    return k.decal("sacred_heart", "Sacred heart", ["heart", "flames"])


@art
def swallow():
    k = Parts()
    # the body and head
    body = poly(spline([(150, 250), (210, 210), (300, 215), (390, 255), (430, 290), (380, 300), (300, 300), (220, 300)], 10, closed=True))
    head = circle(155, 245, 46)
    beak = poly([(108, 240), (80, 252), (112, 258)])
    tail = union(poly(spline([(390, 260), (470, 300), (500, 360)], 12) + spline([(500, 360), (460, 320), (400, 300)], 12)),
                 poly(spline([(395, 290), (480, 350), (490, 420)], 12) + spline([(490, 420), (450, 360), (380, 305)], 12)))
    wing_up = [feather((250 + i * 22, 225), -100 + i * 9, 230 - i * 22, 52) for i in range(4, -1, -1)]
    wing_dn = [feather((250 + i * 18, 280), 60 + i * 8, 180 - i * 20, 46) for i in range(3, -1, -1)]
    f, l = painter(wing_dn, INK)
    k.fill, k.line = f, l
    k.lay(tail)
    k.tone(tail)
    k.lay(body)
    k.tone(inter(body, rect(0, 268, 512, 400)))
    k.lay(beak)
    k.lay(head)
    k.tone(inter(head, rect(0, 255, 512, 400)))
    wf, wl = painter(wing_up, INK)
    w = Parts()
    w.fill, w.line = wf, wl
    k.merge(w)
    k.black(circle(145, 238, 9))
    k.ink(taper(spline([(130, 268), (170, 280), (210, 270)], 10), FINE, 1, FINE * 1.4))
    return k.decal("swallow", "Swallow", ["bird", "swallow", "sailor"])


@art
def reaper():
    k = Parts()
    # the scythe behind him
    pole = taper([(140, 500), (360, 40)], 16, 16, 16, ends="round")
    blade = poly(spline([(345, 70), (300, 40), (200, 30), (90, 70), (40, 130), (120, 95), (230, 80), (330, 95)], 12, closed=True))
    k.lay(pole)
    k.lay(blade)
    k.tone(inter(blade, rect(0, 70, 512, 200)))
    # the robe
    robe = poly(spline([(256, 120), (330, 170), (360, 280), (400, 470), (330, 500), (256, 490), (180, 500), (112, 470), (152, 280), (182, 170)], 10, closed=True))
    hood = poly(spline([(256, 70), (322, 100), (346, 180), (330, 250), (256, 270), (182, 250), (166, 180), (190, 100)], 10, closed=True))
    k.lay(robe)
    for x in (210, 256, 300):
        k.ink(taper(spline([(x, 300), (x + (x - 256) * 0.2, 400), (x + (x - 256) * 0.3, 490)], 10), FINE * 1.4, 1, FINE * 1.6))
    k.tone(inter(robe, union(rect(0, 0, 200, 512), rect(320, 0, 512, 512))))
    k.lay(hood)
    k.tone(diff(hood, ellipse(256, 200, 62, 70)))
    # the face in the hood's shadow: a small skull
    face = Parts()
    face.black(ellipse(256, 195, 66, 74))
    k.merge(face)
    k.merge(skull(256, 198, 46))
    # bony hand on the pole
    hand = poly(spline([(225, 300), (262, 290), (280, 312), (262, 330), (226, 324)], 8, closed=True))
    k.lay(hand)
    for y in (300, 312, 324):
        k.ink(stroke(line_path([(245, y), (278, y + 2)]), FINE * 0.8))
    return k.decal("reaper", "Grim reaper with scythe", ["reaper", "death", "scythe"])


@art
def spider_web():
    k = Parts()
    cx, cy = 256, 230
    for i in range(9):
        a = math.radians(-180 + i * 22.5)
        k.ink(taper([(cx, cy), (cx + math.cos(a) * 240, cy + math.sin(a) * 240)], FINE * 1.2, FINE * 0.8, FINE * 1.2, ends="round"))
    for r in (60, 110, 160, 210):
        pts = []
        for i in range(9):
            a = math.radians(-180 + i * 22.5)
            pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
        segs = []
        for a0, a1 in zip(pts, pts[1:]):
            mid = ((a0[0] + a1[0]) / 2, (a0[1] + a1[1]) / 2)
            mid = (mid[0] + (cx - mid[0]) * 0.12, mid[1] + (cy - mid[1]) * 0.12)
            segs.append(taper(spline([a0, mid, a1], 8), FINE, FINE, FINE, ends="round"))
        k.ink(union(*segs))
    # the spider hanging
    sx, sy = 330, 360
    k.ink(stroke(line_path([(sx, cy), (sx, sy - 30)]), FINE * 0.8))
    for sg in (1, -1):
        for j, (ax, ay) in enumerate(((60, -50), (70, -10), (66, 30), (50, 64))):
            k.ink(taper(spline([(sx + sg * 12, sy + 6 * j - 4), (sx + sg * ax * 0.7, sy + ay - 20), (sx + sg * ax, sy + ay)], 8), FINE * 2, 1, FINE * 2))
    k.lay(ellipse(sx, sy + 20, 26, 34))
    k.lay(circle(sx, sy - 18, 16))
    k.black(poly([(sx, sy + 6), (sx + 10, sy + 20), (sx, sy + 34), (sx - 10, sy + 20)]))
    return k.decal("spider_web", "Spider and web", ["spider", "web"])


@art
def bat():
    k = Parts()
    half = [(0, -0.20), (0.10, -0.42), (0.14, -0.26), (0.40, -0.40), (0.75, -0.55), (1.0, -0.35), (0.92, -0.05), (0.80, 0.12),
            (0.70, 0.05), (0.58, 0.20), (0.44, 0.10), (0.30, 0.25), (0.15, 0.10), (0.08, 0.40), (0, 0.45)]
    pts = half + [(-x, y) for x, y in reversed(half[:-1])]
    b = poly(U(pts, 256, 256, 240))
    k.lay(b, INK)
    for sg in (1, -1):
        for tip in ((0.80, 0.12), (0.58, 0.20), (0.30, 0.25)):
            k.ink(taper(spline(U([(0.16 * sg, -0.20), ((tip[0] * 0.6) * sg, (tip[1] - 0.2) * 0.6), (tip[0] * sg, tip[1])], 256, 256, 240), 8), FINE * 1.5, 1, FINE * 1.5))
        k.tone(poly(U([(0.16 * sg, -0.18), (0.75 * sg, -0.55), (1.0 * sg, -0.35), (0.92 * sg, -0.05), (0.16 * sg, 0.0)], 256, 256, 240)))
        k.black(circle(256 + sg * 14, 200, 6))
    return k.decal("bat", "Bat", ["bat", "night"])


@art
def dice():
    k = Parts()

    def die(cx, cy, s, rot, pips_top, pips_side, pips_front):
        d = Parts()
        top = poly([(0, -1), (0.9, -0.5), (0, 0), (-0.9, -0.5)])
        left = poly([(-0.9, -0.5), (0, 0), (0, 1.05), (-0.9, 0.55)])
        right = poly([(0.9, -0.5), (0, 0), (0, 1.05), (0.9, 0.55)])
        tr = lambda p: xform(p, sx=s, sy=s, rot=rot, tx=cx, ty=cy, cx=0, cy=0)
        d.lay(tr(top))
        d.lay(tr(left))
        d.lay(tr(right))
        d.tone(tr(right))

        def pips(face, pts):
            for u, v in pts:
                if face == "top":
                    x, y = (u - v) * 0.45, -0.5 + (u + v) * 0.25
                elif face == "left":
                    x, y = -0.9 + 0.9 * u, -0.5 + 0.5 * u + 1.05 * v
                else:
                    x, y = 0.9 * u, 0.5 * u * -1 + 1.05 * v + 0.0
                    x, y = 0.9 * (1 - u), -0.5 + 0.5 * u + 1.05 * v
                d.black(tr(ellipse(x, y, 0.09, 0.07)))
        layout = {1: [(0.5, 0.5)], 2: [(0.25, 0.25), (0.75, 0.75)], 3: [(0.25, 0.25), (0.5, 0.5), (0.75, 0.75)],
                  4: [(0.25, 0.25), (0.75, 0.25), (0.25, 0.75), (0.75, 0.75)],
                  5: [(0.25, 0.25), (0.75, 0.25), (0.5, 0.5), (0.25, 0.75), (0.75, 0.75)],
                  6: [(0.25, 0.2), (0.25, 0.5), (0.25, 0.8), (0.75, 0.2), (0.75, 0.5), (0.75, 0.8)]}
        pips("top", [(u, v) for u, v in layout[pips_top]])
        pips("left", layout[pips_side])
        pips("right", layout[pips_front])
        return d
    k.merge(die(180, 250, 130, -12, 1, 6, 3))
    k.merge(die(350, 300, 120, 14, 5, 2, 4))
    return k.decal("dice", "Dice", ["dice", "luck"])


@art
def ace_spades():
    k = Parts()
    for i, (rot, dx) in enumerate(((-14, -60), (0, 0), (14, 60))):
        card = rect(-110, -160, 110, 160, 18)
        c = Parts()
        c.lay(card)
        if i == 2:
            sp = union(xform(heart(0, 0, 70), sy=-1, cx=0, cy=0), poly([(-8, 40), (8, 40), (30, 90), (-30, 90)]))
            sp = xform(sp, ty=-15, cx=0, cy=0)
            c.black(sp)
            c.black(xform(sp, sx=0.25, sy=0.25, tx=-80, ty=-115, cx=0, cy=0))
        else:
            c.tone(inter(card, rect(-110, -160, -90, 160)))
        k.merge(c.moved(rot=rot, tx=256 + dx * 1.3, ty=280, cx=0, cy=0))
    return k.decal("ace_spades", "Ace of spades", ["cards", "luck", "spades"])


@art
def eight_ball():
    k = Parts()
    b = circle(256, 256, 220)
    k.lay(b)
    k.black(diff(b, circle(236, 226, 100)))
    k.black(diff(b, P()) if False else P())
    k.fill = b
    win = circle(236, 226, 96)
    k.lay(win, INK)
    k.tone(diff(b, circle(206, 196, 190)))
    # the eight
    k.black(union(diff(ellipse(236, 196, 38, 30), ellipse(236, 196, 18, 13)), diff(ellipse(236, 258, 46, 36), ellipse(236, 258, 24, 18))))
    k.ink(ellipse(150, 150, 30, 16, rot=-40))
    return k.decal("eight_ball", "Eight ball", ["luck", "ball"])


@art
def horseshoe():
    k = Parts()
    outer = diff(circle(256, 230, 200), rect(0, 330, 512, 512))
    shoe = union(diff(circle(256, 230, 200), circle(256, 230, 120)), diff(rect(56, 230, 136, 450, 20), P()), rect(376, 230, 456, 450, 20))
    shoe = diff(shoe, rect(136, 230, 376, 512))
    k.lay(shoe)
    k.tone(inter(shoe, rect(0, 230, 256, 512)))
    for i in range(8):
        a = math.radians(-180 + i * 180 / 7 + (0 if i < 4 else 0))
        x, y = 256 + math.cos(a) * 160, 230 + math.sin(a) * 160
        k.black(rect(x - 9, y - 9, x + 9, y + 9, 3))
    for x in (96, 416):
        k.black(rect(x - 9, 360, x + 9, 378, 3))
    # sparkle stars
    for x, y, r in ((256, 60, 30), (430, 110, 20), (80, 110, 20)):
        k.black(star(x, y, r, r * 0.25, 4, rot=0))
    return k.decal("horseshoe", "Lucky horseshoe", ["luck", "horseshoe"])


@art
def anchor():
    k = Parts()
    shank = rect(240, 90, 272, 420, 8)
    stock = rect(150, 120, 362, 150, 12)
    ring = diff(circle(256, 70, 44), circle(256, 70, 24))
    arms = taper(arc_pts(256, 290, 180, 20, 160, 40), 40, 40, 34, ends="round")
    flukes = union(poly([(420, 330), (470, 270), (440, 380)]), poly([(92, 330), (42, 270), (72, 380)]))
    for p in (ring, shank, arms, flukes, stock):
        k.lay(p)
    k.tone(inter(union(shank, arms, stock), rect(256, 0, 512, 512)))
    # rope round it
    rope = taper(spline([(256, 115), (330, 170), (220, 230), (300, 300), (240, 380), (330, 440), (410, 400)], 14), 14, 10, 14, ends="round")
    k.lay(rope, FINE * 1.4)
    for i in range(30):
        pts = spline([(256, 115), (330, 170), (220, 230), (300, 300), (240, 380), (330, 440), (410, 400)], 14)
        j = int(i / 30 * (len(pts) - 2))
        a, b = pts[j], pts[j + 1]
        dx, dy = b[0] - a[0], b[1] - a[1]
        d = math.hypot(dx, dy) or 1
        nx, ny = -dy / d * 7, dx / d * 7
        k.ink(stroke(line_path([(a[0] - nx + dx / d * 3, a[1] - ny + dy / d * 3), (a[0] + nx - dx / d * 3, a[1] + ny - dy / d * 3)]), 2.5))
    return k.decal("anchor", "Anchor and rope", ["anchor", "sailor"])


@art
def spanners():
    k = Parts()

    def spanner(rot):
        s = Parts()
        L = 400
        handle = rect(-L / 2 + 40, -20, L / 2 - 40, 20, 16)
        head_o = diff(circle(L / 2 - 20, 0, 58), poly([(L / 2 - 20, -22), (L / 2 + 60, -34), (L / 2 + 60, 34), (L / 2 - 20, 22)]))
        ring = diff(circle(-L / 2 + 20, 0, 54), circle(-L / 2 + 20, 0, 26))
        body = union(handle, head_o, ring)
        s.lay(body)
        s.tone(inter(body, rect(-L, 0, L, 80)))
        s.ink(outline(rect(-L / 2 + 90, -8, L / 2 - 90, 8, 8), FINE))
        return s.moved(rot=rot, tx=256, ty=256, cx=0, cy=0)
    k.merge(spanner(45))
    k.merge(spanner(-45))
    return k.decal("spanners", "Crossed spanners", ["tools", "spanner", "garage"])


ALL_NAMES = [f.__name__ for f in ALL]
