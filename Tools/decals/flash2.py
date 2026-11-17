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


# =============================================================================== third sheet


def winged(cx, cy, root_dx, span, wd, n=5):
    from dk import feather, painter
    pieces = []
    for i in range(n):
        s = i / (n - 1)
        pieces.append(feather((cx + root_dx + 12 * s, cy - 20 * s), -6 - 30 * s, span * (1 - 0.4 * s), wd, curve=-0.06))
    fill, line = painter(pieces, INK)
    w = Parts()
    w.fill, w.line = sym_x(fill, cx), sym_x(line, cx)
    return w


@art
def lightning_skull():
    k = Parts()
    bolt = poly([(380, 10), (130, 280), (250, 280), (110, 502), (410, 220), (300, 220), (470, 10)])
    k.lay(bolt)
    k.tone(inter(bolt, rect(270, 0, 512, 512)))
    k.merge(skull(270, 260, 115))
    return k.decal("lightning_skull", "Skull and lightning", ["skull", "lightning", "death"])


@art
def skeleton_key():
    k = Parts()
    bow = union(circle(130, 256, 80))
    k.lay(bow)
    k.black(circle(130, 256, 44))
    k.black(union(*(circle(130 + 62 * math.cos(math.radians(a)), 256 + 62 * math.sin(math.radians(a)), 9) for a in range(0, 360, 45))))
    shaft = rect(200, 238, 450, 274, 8)
    k.lay(shaft)
    k.lay(rect(200, 226, 222, 286, 4))
    bit = poly([(400, 274), (450, 274), (450, 360), (430, 360), (430, 330), (412, 330), (412, 360), (400, 360)])
    k.lay(bit)
    k.tone(inter(union(shaft, bit), rect(0, 256, 512, 512)))
    return k.decal("skeleton_key", "Skeleton key", ["key", "gothic"])


@art
def padlock_heart():
    k = Parts()
    k.lay(diff(ellipse(256, 150, 90, 100), ellipse(256, 150, 58, 70), rect(150, 170, 362, 300)))
    h = heart(256, 300, 170)
    k.lay(h)
    k.tone(inter(h, rect(256, 0, 512, 512)))
    k.black(union(circle(256, 300, 22), poly([(246, 300), (266, 300), (274, 370), (238, 370)])))
    return k.decal("padlock_heart", "Padlocked heart", ["heart", "lock", "love"])


@art
def scythe():
    k = Parts()
    k.lay(taper(spline([(330, 490), (300, 300), (280, 120), (290, 40)], 12), 22, 22, 22, ends="round"))
    blade = smooth([(285, 70), (180, 40), (70, 80), (30, 160), (110, 110), (200, 100), (282, 118)], 10)
    k.lay(blade)
    k.tone(inter(blade, rect(0, 0, 512, 92)))
    k.lay(rect(268, 330, 312, 346, 6))
    return k.decal("scythe", "Scythe", ["scythe", "death", "reaper"])


@art
def candle():
    k = Parts()
    body = smooth([(196, 200), (316, 200), (318, 460), (194, 460)], 6)
    k.lay(rect(196, 190, 316, 460, 6))
    for x, L in ((214, 60), (262, 110), (300, 40)):
        drip = union(rect(x - 12, 190, x + 12, 190 + L, 12), circle(x, 190 + L, 13))
        k.tone(drip)
    k.lay(rect(150, 455, 362, 485, 10))
    k.ink(stroke(line_path([(256, 190), (256, 160)]), FINE * 1.3))
    flame = smooth([(256, 60), (286, 130), (276, 162), (256, 170), (236, 162), (226, 130)], 10)
    k.lay(flame)
    k.black(ellipse(256, 145, 9, 18))
    return k.decal("candle", "Candle", ["candle", "gothic", "light"])


@art
def death_moth():
    k = Parts()
    for sg in (1, -1):
        upper = smooth([(256, 230), (256 + sg * 60, 150), (256 + sg * 200, 110), (256 + sg * 230, 170), (256 + sg * 150, 260)], 10)
        lower = smooth([(256, 260), (256 + sg * 140, 270), (256 + sg * 180, 340), (256 + sg * 110, 380), (256 + sg * 40, 320)], 10)
        k.lay(lower)
        k.tone(inter(lower, grow(lower, -18)))
        k.lay(upper)
        k.tone(inter(upper, ellipse(256 + sg * 190, 150, 40, 30)))
        k.black(circle(256 + sg * 150, 180, 14))
    body = ellipse(256, 270, 34, 110)
    k.lay(body)
    k.lay(circle(256, 160, 30))
    # the skull mark on its back
    k.black(union(circle(244, 240, 8), circle(268, 240, 8), rect(246, 262, 266, 268, 2)))
    for sg in (1, -1):
        k.ink(taper(spline([(256 + sg * 10, 135), (256 + sg * 40, 80), (256 + sg * 80, 70)], 10), FINE, 1, FINE, ends="tip"))
    return k.decal("death_moth", "Death's-head moth", ["moth", "death", "gothic"], w=512, h=512)


@art
def cog_skull():
    k = Parts()
    teeth = []
    for i in range(12):
        a = i * 30
        teeth.append(xform(rect(-22, -230, 22, -180, 4), rot=a, tx=256, ty=256, cx=0, cy=0))
    cog = diff(union(circle(256, 256, 200), *teeth), circle(256, 256, 160))
    k.lay(cog)
    k.tone(inter(cog, rect(256, 0, 512, 512)))
    k.merge(skull(256, 262, 115))
    return k.decal("cog_skull", "Skull in a cog", ["skull", "cog", "garage", "engine"])


@art
def piston_wings():
    k = Parts()
    k.merge(winged(256, 240, 60, 210, 46))
    p = Parts()
    crown = rect(196, 120, 316, 250, 12)
    p.lay(crown)
    for y in (140, 160, 180):
        p.ink(stroke(line_path([(196, y), (316, y)]), FINE))
    p.black(circle(256, 215, 14))
    p.lay(rect(240, 250, 272, 400, 6))
    p.lay(circle(256, 410, 36))
    p.black(circle(256, 410, 16))
    p.tone(inter(union(crown, rect(240, 250, 272, 400)), rect(256, 0, 512, 512)))
    k.merge(p)
    return k.decal("piston_wings", "Winged piston", ["piston", "engine", "wings", "garage"])


@art
def chain_ring():
    k = Parts()
    n = 14
    for i in range(n):
        a = i * 360 / n
        cx = 256 + 180 * math.cos(math.radians(a))
        cy = 256 + 180 * math.sin(math.radians(a))
        link = diff(ellipse(0, 0, 46, 24), ellipse(0, 0, 30, 10))
        if i % 2:
            link = diff(ellipse(0, 0, 46, 20), ellipse(0, 0, 32, 8))
        k.lay(xform(link, rot=a + 90, tx=cx, ty=cy, cx=0, cy=0))
    return k.decal("chain_ring", "Chain ring", ["chain", "garage"])


@art
def shield_swords():
    k = Parts()
    for sg in (1, -1):
        k.merge(dagger(256 - sg * 200, 60, 256 + sg * 180, 470, 26))
    sh = smooth([(150, 110), (256, 90), (362, 110), (362, 260), (320, 360), (256, 420), (192, 360), (150, 260)], 10)
    k.lay(sh)
    k.tone(inter(sh, rect(256, 0, 512, 512)))
    k.lay(grow(sh, -26), FINE, fill=True)
    k.black(union(rect(244, 140, 268, 370, 4), rect(180, 210, 332, 234, 4)))
    return k.decal("shield_swords", "Shield and swords", ["shield", "sword", "gothic"])


@art
def heart_banner():
    k = Parts()
    h = heart(256, 250, 200)
    k.lay(h)
    k.tone(inter(h, rect(256, 0, 512, 512)))
    k.merge(banner(256, 280, 480, 80))
    return k.decal("heart_banner", "Heart and banner", ["heart", "banner", "lettering"])


@art
def rose_banner():
    k = Parts()
    k.merge(rose(256, 200, 140))
    k.merge(banner(256, 380, 480, 80))
    return k.decal("rose_banner", "Rose and banner", ["rose", "banner", "lettering"])


@art
def skull_crown():
    k = Parts()
    k.merge(skull(256, 310, 140))
    c = Parts()
    pts = [(140, 200), (140, 70), (198, 140), (256, 40), (314, 140), (372, 70), (372, 200)]
    cr = poly(pts)
    c.lay(cr)
    c.tone(inter(cr, rect(256, 0, 512, 512)))
    for x, y in ((140, 62), (256, 32), (372, 62)):
        c.lay(circle(x, y, 16))
    c.black(union(*(ellipse(x, 176, 14, 10) for x in (180, 256, 332))))
    k.merge(c)
    return k.decal("skull_crown", "Crowned skull", ["skull", "crown", "death"])


@art
def wave():
    k = Parts()
    body = smooth([(30, 450), (60, 300), (160, 160), (300, 100), (420, 130), (470, 210), (420, 260), (360, 220), (330, 260), (360, 330),
                   (440, 380), (480, 450)], 10)
    k.lay(body)
    k.tone(inter(body, rect(0, 330, 512, 512)))
    k.ink(taper(curl(400, 200, 50, turns=1.1, sign=-1, start=0.3), INK, 1, ends="tip"))
    for i in range(4):
        k.ink(taper(spline([(80 + 40 * i, 440), (120 + 40 * i, 330), (200 + 40 * i, 230)], 10), FINE, 1, FINE, ends="tip"))
    for x, y, r in ((470, 150, 14), (490, 190, 9), (440, 110, 11)):
        k.lay(circle(x, y, r))
    return k.decal("wave", "Great wave", ["wave", "sea", "tattoo"])


@art
def rosary():
    k = Parts()
    for i in range(22):
        a = math.radians(-60 + i * 300 / 21)
        k.lay(circle(256 + 150 * math.cos(a) * 0.9, 190 + 150 * math.sin(a), 13))
    k.lay(rect(244, 330, 268, 480, 4))
    k.lay(rect(196, 370, 316, 394, 4))
    k.lay(circle(256, 330, 18))
    return k.decal("rosary", "Rosary", ["rosary", "cross", "gothic"])


@art
def scorpion():
    k = Parts()
    for sg in (1, -1):
        for dy in (230, 265, 300, 335):
            leg = [(256 + sg * 40, dy), (256 + sg * 100, dy - 14), (256 + sg * 130, dy + 34)]
            k.lay(taper(leg, 14, 7, 12, ends="round"))
        arm = [(256 + sg * 30, 180), (256 + sg * 90, 150), (256 + sg * 110, 90)]
        k.lay(taper(spline(arm, 10), 22, 18, 22, ends="round"))
        claw = diff(ellipse(256 + sg * 115, 60, 34, 46), poly([(256 + sg * 115, 60), (256 + sg * 100, 10), (256 + sg * 130, 10)]))
        k.lay(claw)
    tail = [(256, 360), (262, 440), (330, 480), (410, 440), (430, 350), (390, 270)]
    t = taper(spline(tail, 14), 44, 22, 36, ends="round")
    k.lay(t)
    sp = spline(tail, 14)
    for i in range(6, len(sp) - 2, 8):
        x, y = sp[i]
        k.ink(circle(x, y, 4))
    k.lay(poly([(398, 284), (340, 250), (380, 236)]))
    segs = [(256, 330, 50, 36), (256, 270, 54, 40), (256, 210, 48, 34)]
    for x, y, rx, ry in segs:
        k.lay(ellipse(x, y, rx, ry))
    k.black(union(circle(244, 186, 6), circle(268, 186, 6)))
    k.tone(inter(union(*(ellipse(x, y, rx, ry) for x, y, rx, ry in segs)), rect(256, 0, 512, 512)))
    return k.decal("scorpion", "Scorpion", ["scorpion", "tattoo", "death"])


@art
def spider():
    k = Parts()
    for sg in (1, -1):
        for i, (ax, ay, bx, by) in enumerate(((60, -40, 140, -120), (70, -10, 170, -40), (70, 20, 170, 80), (60, 50, 130, 160))):
            leg = [(256 + sg * 20, 250 + ay * 0.3), (256 + sg * ax, 250 + ay), (256 + sg * bx, 250 + by)]
            k.lay(taper(leg, 16, 6, 14, ends="round"))
    k.lay(ellipse(256, 320, 70, 90))
    k.lay(ellipse(256, 210, 44, 40))
    k.black(poly([(256, 270), (280, 310), (256, 330), (232, 310)]))
    k.black(poly([(256, 340), (276, 370), (236, 370)]))
    k.tone(inter(ellipse(256, 320, 70, 90), rect(256, 0, 512, 512)))
    k.ink(stroke(line_path([(256, 30), (256, 170)]), FINE * 0.8))
    return k.decal("spider", "Spider", ["spider", "gothic", "tattoo"])


@art
def cobra():
    k = Parts()
    hood = smooth([(256, 60), (330, 110), (360, 220), (310, 330), (256, 360), (202, 330), (152, 220), (182, 110)], 10)
    body = taper(spline([(256, 340), (300, 420), (200, 470), (120, 440)], 14), 80, 20, 70, ends="round")
    k.lay(body)
    k.lay(hood)
    k.tone(inter(hood, grow(hood, -30)))
    k.ink(outline(smooth([(256, 150), (290, 190), (256, 230), (222, 190)], 8), FINE))
    head = smooth([(256, 70), (290, 90), (296, 130), (256, 150), (216, 130), (222, 90)], 8)
    k.lay(head)
    k.black(union(ellipse(236, 106, 7, 10), ellipse(276, 106, 7, 10)))
    for i in range(5):
        y = 240 + i * 22
        k.ink(stroke(line_path([(232 + i * 2, y), (280 - i * 2, y)]), FINE * 0.8))
    return k.decal("cobra", "Cobra", ["snake", "cobra", "tattoo"])


@art
def wrench_screwdriver():
    k = Parts()
    w = Parts()
    L = 400
    handle = rect(-L / 2 + 40, -20, L / 2 - 40, 20, 16)
    head = diff(circle(L / 2 - 20, 0, 58), poly([(L / 2 - 20, -22), (L / 2 + 60, -34), (L / 2 + 60, 34), (L / 2 - 20, 22)]))
    ring = diff(circle(-L / 2 + 20, 0, 54), circle(-L / 2 + 20, 0, 26))
    body = union(handle, head, ring)
    w.lay(body)
    w.tone(inter(body, rect(-L, 0, L, 80)))
    k.merge(w.moved(rot=45, tx=256, ty=256, cx=0, cy=0))
    s = Parts()
    grip = rect(-200, -30, -40, 30, 20)
    s.lay(grip)
    for x in (-170, -130, -90):
        s.ink(stroke(line_path([(x, -30), (x, 30)]), FINE))
    s.lay(rect(-40, -10, 170, 10, 4))
    s.lay(poly([(170, -10), (200, -4), (200, 4), (170, 10)]))
    s.tone(inter(grip, rect(-200, 0, 0, 30)))
    k.merge(s.moved(rot=-45, tx=256, ty=256, cx=0, cy=0))
    return k.decal("wrench_screwdriver", "Spanner and screwdriver", ["tools", "garage", "spanner"])


@art
def fuel_can():
    k = Parts()
    can = rect(130, 120, 382, 470, 18)
    k.lay(can)
    k.tone(inter(can, rect(300, 0, 512, 512)))
    k.lay(rect(150, 80, 260, 130, 8))
    k.black(rect(176, 96, 234, 116, 4))
    k.lay(rect(290, 70, 340, 130, 6))
    k.ink(outline(poly([(170, 180), (342, 180), (342, 430), (170, 430)]), FINE * 1.4))
    k.ink(stroke(line_path([(170, 180), (342, 430)]), FINE * 1.4))
    k.ink(stroke(line_path([(342, 180), (170, 430)]), FINE * 1.4))
    return k.decal("fuel_can", "Fuel can", ["fuel", "garage"])
