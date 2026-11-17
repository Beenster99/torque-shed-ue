"""Bold line artwork, second sheet (beenster passed the first, Nov 17): more gothic, death and
garage flash in the same style. Each piece joins the art shelf through flash.art.
"""
import math

from dk import (P, arc_pts, circle, curl, diff, ellipse, grow, inter, line_path, mirror_x, outline, poly, rect, smooth,
                spline, star, stroke, sym_x, taper, union, xform)
from flash import FINE, INK, Parts, U, art, dagger, flames_up, heart, leaf, mirror_pts, rose, skull


# ------------------------------------------------------------------------------------------ helpers


def wheel(cx, cy, r, spokes=12):
    k = Parts()
    k.lay(circle(cx, cy, r))
    k.black(diff(circle(cx, cy, r), circle(cx, cy, r * 0.8)))
    k.ink(diff(circle(cx, cy, r * 0.8), circle(cx, cy, r * 0.72)))
    for i in range(spokes):
        a = math.radians(i * 360 / spokes)
        k.ink(stroke(line_path([(cx + r * 0.2 * math.cos(a), cy + r * 0.2 * math.sin(a)), (cx + r * 0.74 * math.cos(a), cy + r * 0.74 * math.sin(a))]), FINE))
    k.lay(circle(cx, cy, r * 0.22))
    k.black(circle(cx, cy, r * 0.08))
    k.tone(inter(diff(circle(cx, cy, r * 0.72), circle(cx, cy, r * 0.22)), rect(cx, cy - r, cx + r, cy + r)))
    return k


def banner(cx, cy, w, h):
    """A ribbon across, its tails folded behind."""
    k = Parts()
    for sg in (1, -1):
        x0 = cx + sg * w * 0.42
        tail = poly([(x0, cy - h * 0.2), (x0 + sg * w * 0.2, cy - h * 0.2), (x0 + sg * w * 0.15, cy + h * 0.3), (x0 + sg * w * 0.2, cy + h * 0.8), (x0, cy + h * 0.8)])
        k.lay(tail)
        k.tone(tail)
    band = smooth([(cx - w * 0.45, cy - h * 0.5), (cx, cy - h * 0.7), (cx + w * 0.45, cy - h * 0.5), (cx + w * 0.45, cy + h * 0.5), (cx, cy + h * 0.3),
                   (cx - w * 0.45, cy + h * 0.5)], 10)
    k.lay(band)
    return k


def snake_body(pts, w):
    """A snake along pts: a tapered body, belly scales hinted, a head at the end."""
    k = Parts()
    body = taper(spline(pts, 16), w * 0.35, w * 0.8, w, ends="round")
    k.lay(body)
    k.tone(inter(body, taper(spline([(x + w * 0.18, y + w * 0.18) for x, y in pts], 16), w * 0.2, w * 0.5, w * 0.6, ends="round")))
    sp = spline(pts, 16)
    for i in range(4, len(sp) - 4, 6):
        x, y = sp[i]
        k.ink(circle(x, y, w * 0.08))
    hx, hy = pts[-1]
    px, py = pts[-2]
    ang = math.degrees(math.atan2(hy - py, hx - px))
    head = xform(ellipse(0, 0, w * 0.9, w * 0.62), rot=ang, tx=hx, ty=hy)
    k.lay(head)
    a = math.radians(ang)
    ex, ey = hx + math.cos(a) * w * 0.25 - math.sin(a) * w * 0.25, hy + math.sin(a) * w * 0.25 + math.cos(a) * w * 0.25
    k.black(circle(ex, ey, w * 0.12))
    tx, ty = hx + math.cos(a) * w * 0.9, hy + math.sin(a) * w * 0.9
    tongue = [(tx, ty), (tx + math.cos(a) * w * 0.5, ty + math.sin(a) * w * 0.5)]
    k.black(stroke(line_path(tongue), FINE * 0.9))
    for fork in (-25, 25):
        b = a + math.radians(fork)
        k.black(stroke(line_path([tongue[1], (tongue[1][0] + math.cos(b) * w * 0.25, tongue[1][1] + math.sin(b) * w * 0.25)]), FINE * 0.8))
    return k


# ------------------------------------------------------------------------------------- the pieces


@art
def gothic_cross():
    k = Parts()
    arm = 52
    cross = union(rect(256 - arm / 2, 40, 256 + arm / 2, 470, 6), rect(110, 150 - arm / 2, 402, 150 + arm / 2, 6))
    for cx, cy in ((256, 40), (256, 470), (110, 150), (402, 150)):
        cross = union(cross, circle(cx, cy, 38))
    k.lay(cross)
    k.lay(circle(256, 150, 62))
    k.black(diff(circle(256, 150, 46), circle(256, 150, 34)))
    k.tone(inter(cross, rect(256, 0, 512, 512)))
    for cx, cy in ((256, 40), (256, 470), (110, 150), (402, 150)):
        k.black(circle(cx, cy, 12))
    return k.decal("gothic_cross", "Gothic cross", ["gothic", "cross"])


@art
def coffin():
    k = Parts()
    c = poly([(256, 30), (360, 120), (330, 480), (182, 480), (152, 120)])
    k.lay(c)
    k.tone(inter(c, rect(256, 0, 512, 512)))
    k.lay(grow(poly([(256, 70), (328, 132), (304, 440), (208, 440), (184, 132)]), -2), FINE * 1.5, fill=True)
    k.black(union(rect(244, 150, 268, 330, 4), rect(206, 196, 306, 220, 4)))
    return k.decal("coffin", "Coffin", ["death", "coffin", "gothic"])


@art
def tombstone():
    k = Parts()
    stone = union(rect(130, 170, 382, 450, 8), ellipse(256, 175, 126, 120))
    k.lay(stone)
    k.tone(inter(stone, rect(320, 0, 512, 512)))
    k.black(union(rect(244, 200, 268, 340, 4), rect(206, 236, 306, 260, 4)))
    k.ink(stroke(line_path([(160, 120), (180, 160), (170, 200)]), FINE))
    ground = smooth([(70, 450), (180, 420), (330, 425), (440, 450), (440, 480), (70, 480)], 8)
    k.lay(ground)
    k.black(inter(ground, rect(0, 465, 512, 512)))
    return k.decal("tombstone", "Tombstone", ["death", "grave", "gothic"])


@art
def raven():
    k = Parts()
    body = smooth([(120, 300), (200, 240), (300, 220), (370, 180), (420, 170), (440, 190), (410, 215), (360, 260), (320, 320),
                   (240, 350), (160, 340)], 10)
    tail = poly([(140, 320), (40, 380), (60, 340), (30, 330), (120, 296)])
    wing = smooth([(200, 260), (300, 240), (360, 270), (300, 310), (220, 320)], 10)
    k.lay(tail)
    k.lay(body)
    k.lay(wing)
    for i in range(4):
        k.ink(taper([(230 + 30 * i, 290 - 4 * i), (300 + 18 * i, 280 - 2 * i)], FINE, 1, FINE, ends="tip"))
    k.lay(poly([(430, 178), (480, 192), (436, 204)]))
    k.black(circle(412, 186, 8))
    for x in (250, 290):
        k.ink(stroke(line_path([(x, 345), (x - 8, 400)]), FINE * 1.4))
        k.ink(stroke(line_path([(x - 8, 400), (x - 30, 410)]), FINE))
        k.ink(stroke(line_path([(x - 8, 400), (x + 12, 412)]), FINE))
    k.lay(rect(120, 408, 400, 428, 8))
    k.tone(inter(body, rect(0, 280, 512, 512)))
    return k.decal("raven", "Raven", ["bird", "raven", "gothic", "death"])


@art
def snake_coil():
    k = Parts()
    pts = [(120, 420), (250, 440), (380, 410), (400, 350), (300, 320), (160, 330), (140, 270), (220, 230), (340, 230), (360, 180),
           (300, 140), (250, 110)]
    k.merge(snake_body(pts, 46))
    return k.decal("snake_coil", "Coiled snake", ["snake", "tattoo"])


@art
def snake_dagger():
    k = Parts()
    k.merge(dagger(256, 30, 256, 490, 34))
    pts = [(300, 430), (200, 390), (310, 330), (200, 270), (310, 210), (230, 160), (300, 110)]
    k.merge(snake_body(pts, 38))
    return k.decal("snake_dagger", "Snake and dagger", ["snake", "dagger", "tattoo"])


@art
def skull_tophat():
    k = Parts()
    k.merge(skull(256, 300, 150))
    hat = Parts()
    brim = ellipse(256, 168, 190, 30)
    crown = poly([(150, 170), (166, 40), (346, 40), (362, 170)])
    hat.lay(brim)
    hat.lay(crown)
    hat.black(rect(156, 128, 356, 158))
    hat.tone(inter(crown, rect(300, 0, 512, 512)))
    k.merge(hat)
    return k.decal("skull_tophat", "Skull in a top hat", ["skull", "hat", "death", "gothic"])


@art
def skull_helmet():
    k = Parts()
    k.merge(skull(256, 290, 150, jaw=True))
    h = Parts()
    shell = diff(ellipse(256, 200, 190, 170), rect(0, 220, 512, 512))
    h.lay(shell)
    h.tone(inter(shell, rect(320, 0, 512, 512)))
    h.ink(stroke(line_path([(256, 40), (256, 210)]), FINE * 1.2))
    h.black(rect(130, 192, 382, 208))
    h.lay(circle(176, 200, 50))
    h.black(circle(176, 200, 34))
    h.lay(circle(336, 200, 50))
    h.black(circle(336, 200, 34))
    for cx in (176, 336):
        h.ink(stroke(line_path(arc_pts(cx, 200, 24, 200, 260, 10)), FINE))
    k.merge(h)
    return k.decal("skull_helmet", "Skull in helmet and goggles", ["skull", "helmet", "rider", "death"])


@art
def crossbones():
    k = Parts()
    for sg in (1, -1):
        b = Parts()
        L = 380
        shaft = rect(-L / 2 + 30, -18, L / 2 - 30, 18, 12)
        ends = union(*(circle(x, y, 30) for x in (-L / 2 + 20, L / 2 - 20) for y in (-20, 20)))
        bone = union(shaft, ends)
        b.lay(bone)
        b.tone(inter(bone, rect(-L, 0, L, 60)))
        k.merge(b.moved(rot=sg * 40, tx=256, ty=256, cx=0, cy=0))
    return k.decal("crossbones", "Crossbones", ["bones", "death"])


@art
def sparkplug_wings():
    k = Parts()
    pieces = []
    from dk import feather, painter
    for i in range(5):
        s = i / 4
        pieces.append(feather((300 + 14 * s, 250 - 20 * s), -4 - 32 * s, 200 * (1 - 0.4 * s), 46, curve=-0.06))
    fill, line = painter(pieces, INK)
    w = Parts()
    w.fill, w.line = sym_x(fill, 256), sym_x(line, 256)
    k.merge(w)
    p = Parts()
    p.lay(rect(240, 40, 272, 100, 6))
    p.lay(rect(222, 100, 290, 210, 10))
    for y in (120, 145, 170, 195):
        p.ink(stroke(line_path([(222, y), (290, y)]), FINE))
    p.lay(poly([(212, 210), (300, 210), (300, 260), (212, 260)]))
    p.black(rect(212, 228, 300, 242))
    p.lay(rect(232, 260, 280, 330, 6))
    p.lay(rect(250, 330, 262, 380, 3))
    p.black(stroke(line_path([(256, 380), (256, 395), (276, 395)]), FINE * 1.4))
    p.tone(rect(256, 100, 290, 330))
    k.merge(p)
    return k.decal("sparkplug_wings", "Winged spark plug", ["garage", "engine", "wings", "spark plug"])


@art
def flaming_wheel():
    k = Parts()
    fl = Parts()
    licks = []
    for i in range(6):
        y = 170 + 36 * i
        c = [(300, y), (200, y - 20), (120, y + 14), (30, y - 16)]
        licks.append(taper(spline(c, 14), 46, 1, 40, ends="tip"))
    f = union(*licks)
    fl.lay(f)
    fl.tone(inter(f, grow(f, -14)))
    k.merge(fl)
    k.merge(wheel(330, 260, 150))
    return k.decal("flaming_wheel", "Flaming wheel", ["wheel", "flames", "speed"], w=512, h=512)


@art
def nautical_star():
    k = Parts()
    outer = star(256, 256, 230, 92, n=5)
    k.lay(outer)
    for i in range(5):
        a0 = math.radians(-90 + i * 72)
        a1 = math.radians(-90 + i * 72 + 36)
        tip = (256 + 230 * math.cos(a0), 256 + 230 * math.sin(a0))
        valley = (256 + 92 * math.cos(a1), 256 + 92 * math.sin(a1))
        k.black(poly([(256, 256), tip, valley]))
    return k.decal("nautical_star", "Nautical star", ["star", "tattoo", "luck"])


@art
def gem():
    k = Parts()
    outline_pts = [(130, 190), (190, 120), (322, 120), (382, 190), (256, 420)]
    g = poly(outline_pts)
    k.lay(g)
    for a, b in (((130, 190), (382, 190)), ((190, 120), (220, 190)), ((322, 120), (292, 190)), ((256, 120), (220, 190)),
                 ((256, 120), (292, 190)), ((220, 190), (256, 420)), ((292, 190), (256, 420))):
        k.ink(stroke(line_path([a, b]), FINE))
    k.tone(poly([(292, 190), (382, 190), (256, 420)]))
    k.tone(poly([(256, 120), (322, 120), (292, 190)]))
    for x, y, r in ((400, 110, 22), (110, 330, 16)):
        k.ink(star(x, y, r, r * 0.3, n=4, rot=0))
    return k.decal("gem", "Diamond", ["gem", "diamond", "luck"])


@art
def sword_skull():
    k = Parts()
    k.merge(dagger(256, 20, 256, 500, 30))
    k.merge(skull(256, 270, 150))
    return k.decal("sword_skull", "Sword through a skull", ["skull", "sword", "death"])


@art
def thorn_heart():
    k = Parts()
    h = heart(256, 270, 190)
    k.lay(h)
    k.tone(inter(h, rect(256, 0, 512, 512)))
    vine = []
    for i in range(41):
        t = i / 40
        x = 50 + 412 * t
        y = 260 + 40 * math.sin(t * math.pi * 3)
        vine.append((x, y))
    k.lay(taper(vine, 18, 18, 18, ends="round"))
    for i in range(3, 40, 4):
        x, y = vine[i]
        sg = 1 if i % 8 < 4 else -1
        k.lay(poly([(x - 9, y), (x + 9, y), (x + 2, y + sg * 30)]), FINE)
    return k.decal("thorn_heart", "Heart in thorns", ["heart", "thorns", "gothic"])


@art
def ouroboros():
    k = Parts()
    pts = arc_pts(256, 256, 178, 130, 130 + 280, 30)
    k.merge(snake_body(pts, 50))
    return k.decal("ouroboros", "Snake ring", ["snake", "gothic", "circle"])


@art
def hourglass_wings():
    k = Parts()
    from dk import feather, painter
    pieces = []
    for i in range(5):
        s = i / 4
        pieces.append(feather((310 + 12 * s, 250 - 22 * s), -6 - 30 * s, 190 * (1 - 0.4 * s), 44, curve=-0.06))
    fill, line = painter(pieces, INK)
    w = Parts()
    w.fill, w.line = sym_x(fill, 256), sym_x(line, 256)
    k.merge(w)
    g = Parts()
    g.lay(rect(180, 60, 332, 90, 8))
    g.lay(rect(180, 420, 332, 450, 8))
    glass = smooth([(196, 92), (316, 92), (300, 200), (262, 254), (300, 310), (316, 418), (196, 418), (212, 310), (250, 254), (212, 200)], 10)
    g.lay(glass)
    g.black(poly([(224, 150), (288, 150), (262, 240), (250, 240)]))
    g.black(smooth([(220, 410), (256, 350), (292, 410)], 8))
    g.ink(stroke(line_path([(256, 240), (256, 360)]), FINE * 0.8))
    for x in (190, 322):
        g.lay(rect(x - 8, 90, x + 8, 420, 4))
    k.merge(g)
    return k.decal("hourglass_wings", "Winged hourglass", ["time", "wings", "death", "gothic"])


@art
def moon_stars():
    k = Parts()
    moon = diff(circle(240, 256, 190), circle(320, 220, 170))
    k.lay(moon)
    k.tone(inter(moon, rect(0, 300, 512, 512)))
    k.black(ellipse(140, 230, 10, 14))
    k.ink(taper([(110, 300), (140, 312), (170, 300)], FINE * 1.4, FINE, ends="round"))
    for x, y, r in ((380, 140, 40), (440, 260, 26), (360, 380, 32)):
        k.lay(star(x, y, r, r * 0.42, n=5))
    return k.decal("moon_stars", "Moon and stars", ["moon", "stars", "night", "gothic"])


@art
def lantern():
    k = Parts()
    k.lay(diff(circle(256, 60, 34), circle(256, 60, 20)))
    k.lay(poly([(200, 90), (312, 90), (330, 130), (182, 130)]))
    glass = rect(196, 130, 316, 380, 6)
    k.lay(glass)
    k.tone(inter(glass, rect(270, 0, 512, 512)))
    flame = smooth([(256, 190), (284, 260), (272, 310), (256, 320), (240, 310), (228, 260)], 10)
    k.lay(flame)
    k.black(ellipse(256, 290, 10, 22))
    for x in (196, 256, 316):
        k.lay(rect(x - 8, 130, x + 8, 380, 3))
    k.lay(poly([(182, 380), (330, 380), (312, 420), (200, 420)]))
    k.lay(rect(196, 420, 316, 440, 4))
    return k.decal("lantern", "Lantern", ["lantern", "light", "gothic"])


@art
def crown():
    k = Parts()
    pts = [(80, 380), (80, 160), (170, 260), (256, 110), (342, 260), (432, 160), (432, 380)]
    c = poly(pts)
    k.lay(c)
    k.lay(rect(70, 360, 442, 420, 8))
    k.tone(inter(c, rect(256, 0, 512, 512)))
    for x, y in ((80, 150), (256, 98), (432, 150)):
        k.lay(circle(x, y, 22))
    for x in (150, 256, 362):
        k.black(ellipse(x, 390, 20, 14))
    return k.decal("crown", "Crown", ["crown", "king"])
