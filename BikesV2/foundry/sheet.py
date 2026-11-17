"""Designer sketch sheets for the portfolio: a loose pencil study of one bike on a single sheet, in
the manner of a designer's working drawing (beenster's reference, 17 Nov: refs/portfolio_ref_copilot_sketch_sheet.png).
A big three-quarter view, a small side elevation with its proportions, detail studies and handwritten notes.
Everything is drawn from the real model's cut-out renders (BIKE_SKETCH=1), so it is exactly the bike.

    python3 foundry/sheet.py corvano_cafe        -> out/sheets/corvano_cafe.jpg
"""
import json
import math
import os
import sys
import numpy as np
from PIL import Image, ImageFilter, ImageChops, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 3072, 2048
FONT = os.path.join(ROOT, "foundry", "fonts", "ArchitectsDaughter.ttf")
GRAPHITE = np.array((52, 52, 58), dtype="float32")


def smooth_noise(shape, cell, seed):
    rng = np.random.default_rng(seed)
    small = rng.random((max(2, shape[0] // cell + 2), max(2, shape[1] // cell + 2))).astype("float32")
    im = Image.fromarray((small * 255).astype("uint8")).resize((shape[1], shape[0]), Image.BICUBIC)
    return np.asarray(im, dtype="float32") / 255 - 0.5


def wobble(layer, amp, cell, seed):
    """The same drawing, redrawn by a slightly unsteady hand."""
    h, w = layer.shape
    dx, dy = smooth_noise((h, w), cell, seed) * 2 * amp, smooth_noise((h, w), cell, seed + 1) * 2 * amp
    yy, xx = np.mgrid[0:h, 0:w]
    return layer[np.clip((yy + dy).astype("int32"), 0, h - 1), np.clip((xx + dx).astype("int32"), 0, w - 1)]


def streaks(shape, angle, length, seed):
    """Pencil strokes all leaning one way: 0..1, bright where a stroke is."""
    h, w = shape
    d = int(math.hypot(h, w)) + 4
    rng = np.random.default_rng(seed)
    n = rng.random((d, d)).astype("float32")
    c = np.cumsum(n, axis=1)
    n = (c[:, length:] - c[:, :-length]) / length
    n = np.pad(n, ((0, 0), (0, d - n.shape[1])), mode="edge")
    n = (n - n.mean()) / (n.std() + 1e-6)
    im = Image.fromarray(np.clip(n * 60 + 128, 0, 255).astype("uint8")).rotate(angle, Image.BICUBIC)
    a = np.asarray(im, dtype="float32")[(d - h) // 2:(d - h) // 2 + h, (d - w) // 2:(d - w) // 2 + w]
    return (a - 128) / 60


def pencil(cut, size, weight=1.0, shade=1.0, seed=1):
    """A cut-out render drawn in pencil: darkness 0..1 at the given size (w, h)."""
    im = cut.resize(size, Image.LANCZOS)
    alpha = im.getchannel("A")
    a = np.asarray(alpha, dtype="float32") / 255
    rgb = np.asarray(im.convert("RGB"), dtype="float32")
    lum = (rgb @ np.array((0.3, 0.59, 0.11), dtype="float32")) / 255
    lum = lum ** 0.55                                             # open up the blacks so tyres keep their detail
    g = np.clip((lum * a + (1 - a)) * 255, 0, 255)
    grey = Image.fromarray(g.astype("uint8"))
    inv_blur = np.asarray(ImageChops.invert(grey).filter(ImageFilter.GaussianBlur(1.8)), dtype="float32")
    dodge = np.clip(g * 255.0 / np.maximum(255.0 - inv_blur, 1.0), 0, 255)
    fine = np.clip((238.0 - dodge) / 50.0, 0, 1)
    # form edges: where the surface turns or one part ends against another
    sm = np.asarray(grey.filter(ImageFilter.GaussianBlur(1.6)), dtype="float32") / 255
    gx, gy = np.zeros_like(sm), np.zeros_like(sm)
    gx[:, 1:-1], gy[1:-1, :] = sm[:, 2:] - sm[:, :-2], sm[2:, :] - sm[:-2, :]
    grad = np.hypot(gx, gy)
    form = np.clip((grad - 0.035) / 0.07, 0, 1)
    form = np.asarray(Image.fromarray((form * 255).astype("uint8")).filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(0.9)), dtype="float32") / 255
    edge = np.asarray(alpha.filter(ImageFilter.FIND_EDGES).filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(1.0)), dtype="float32") / 255
    line = np.clip(fine * 0.55 + np.clip(form * 1.6, 0, 1) * 0.75 + np.clip(edge * 2.2, 0, 1) * 0.9, 0, 1)
    # three passes of an unsteady hand: the lines never quite retrace each other
    out = 1 - (1 - line * 0.85) * (1 - wobble(line, 2.4, 90, seed) * 0.42) * (1 - wobble(line, 5.0, 150, seed + 7) * 0.26)
    # hatching only in the real darks, crossed in the deepest; the lights stay bare paper
    tone = np.asarray(grey.filter(ImageFilter.GaussianBlur(4)), dtype="float32") / 255
    dark = np.clip((1 - tone - 0.3) / 0.7, 0, 1) * a
    h1, h2 = streaks(a.shape, 38, 46, seed + 20), streaks(a.shape, -24, 40, seed + 30)
    on = np.clip(dark * 8, 0, 1)
    ht = (np.clip((h1 - (1.25 - dark * 2.6)) * 0.9, 0, 1) * 0.42 + np.clip((h2 - (2.4 - dark * 2.9)) * 0.9, 0, 1) * 0.38) * on
    out = 1 - (1 - out) * (1 - ht * shade)
    return np.clip(out * weight, 0, 1), a


class Sheet:
    def __init__(self, seed=5):
        self.d = np.zeros((H, W), dtype="float32")            # pencil darkness
        self.aux = Image.new("L", (W, H), 0)                  # construction lines, arrows and lettering
        self.dr = ImageDraw.Draw(self.aux)
        self.rng = np.random.default_rng(seed)
        self.solid = np.zeros((H, W), dtype="float32")        # where a drawing covers the construction lines

    def place(self, dk, xy):
        x, y = xy
        h, w = dk.shape
        self.d[y:y + h, x:x + w] = 1 - (1 - self.d[y:y + h, x:x + w]) * (1 - dk)

    def line(self, p, q, v=70, over=40, n=2, width=2):
        """A ruled construction line that overshoots both ends, gone over more than once."""
        dx, dy = q[0] - p[0], q[1] - p[1]
        L = math.hypot(dx, dy) or 1
        ux, uy = dx / L, dy / L
        for _ in range(n):
            j = self.rng.normal(0, 2.0, 4)
            o0, o1 = over * (0.5 + self.rng.random()), over * (0.5 + self.rng.random())
            self.dr.line([(p[0] - ux * o0 + j[0], p[1] - uy * o0 + j[1]), (q[0] + ux * o1 + j[2], q[1] + uy * o1 + j[3])], fill=v, width=width)

    def circle(self, c, r, v=60, n=2):
        for _ in range(n):
            j = self.rng.normal(0, 3.0, 3)
            rr = r + j[2]
            self.dr.ellipse([c[0] - rr + j[0], c[1] - rr + j[1], c[0] + rr + j[0], c[1] + rr + j[1]], outline=v, width=2)

    def curve(self, pts, v=60, width=2):
        """A sweeping freehand curve through the points (Catmull-Rom)."""
        out = []
        P = [pts[0]] + list(pts) + [pts[-1]]
        for i in range(1, len(P) - 2):
            for t in np.linspace(0, 1, 40):
                t2, t3 = t * t, t * t * t
                out.append(tuple(0.5 * ((2 * P[i][k]) + (-P[i - 1][k] + P[i + 1][k]) * t + (2 * P[i - 1][k] - 5 * P[i][k] + 4 * P[i + 1][k] - P[i + 2][k]) * t2 + (-P[i - 1][k] + 3 * P[i][k] - 3 * P[i + 1][k] + P[i + 2][k]) * t3) for k in (0, 1)))
        self.dr.line(out, fill=v, width=width)
        return out

    def arrow(self, pts, v=150):
        c = self.curve(pts, v, 3)
        (x1, y1), (x0, y0) = c[-1], c[-8]
        ang = math.atan2(y1 - y0, x1 - x0)
        for s in (-0.45, 0.45):
            self.dr.line([(x1, y1), (x1 - 34 * math.cos(ang + s), y1 - 34 * math.sin(ang + s))], fill=v, width=3)

    def text(self, xy, s, size=58, v=185, slant=-4):
        f = ImageFont.truetype(FONT, size)
        box = self.dr.multiline_textbbox((0, 0), s, font=f, spacing=size * 0.3)
        t = Image.new("L", (int(box[2]) + 30, int(box[3]) + 30), 0)
        ImageDraw.Draw(t).multiline_text((10, 10), s, font=f, fill=v, spacing=size * 0.3)
        t = t.rotate(slant, Image.BICUBIC, expand=True)
        self.aux.paste(ImageChops.lighter(self.aux.crop((xy[0], xy[1], xy[0] + t.width, xy[1] + t.height)), t), xy)

    def render(self, out):
        aux = np.asarray(self.aux.filter(ImageFilter.GaussianBlur(0.8)), dtype="float32") / 255
        aux = np.maximum(wobble(aux, 2.0, 120, 91), wobble(aux, 3.2, 200, 97) * 0.45) * (1 - self.solid * 0.85)
        grain = 0.72 + 0.5 * np.clip(smooth_noise((H, W), 3, 11) + 0.5, 0, 1)        # the tooth of the paper breaks the line up
        d = np.clip((1 - (1 - self.d) * (1 - aux)) * grain, 0, 1)[..., None]
        rng = np.random.default_rng(3)
        paper = np.array((244, 240, 231), dtype="float32")[None, None, :] + (smooth_noise((H, W), 2, 5) * 10 + smooth_noise((H, W), 400, 6) * 8)[..., None]
        res = paper * (1 - d * 0.9) + GRAPHITE * d * 0.9
        im = Image.fromarray(np.clip(res, 0, 255).astype("uint8")).resize((W * 5 // 8, H * 5 // 8), Image.LANCZOS)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        im.save(out, quality=90)
        return out


def crop_to(cut, pad=30):
    b = cut.getchannel("A").getbbox()
    return cut.crop((max(0, b[0] - pad), max(0, b[1] - pad), min(cut.width, b[2] + pad), min(cut.height, b[3] + pad)))


def sheet(name, details=None, notes=None, words=None):
    m = json.load(open(os.path.join(ROOT, "data", "models", name + ".json")))
    src = os.path.join(ROOT, "out", "sketch_src")
    side_tag = "side_long" if m["family"].lower() == "chopper" else "side"
    main = Image.open(os.path.join(src, f"{name}_def_shot_front34.png")).convert("RGBA")
    side = Image.open(os.path.join(src, f"{name}_def_shot_{side_tag}.png")).convert("RGBA")
    S = Sheet()
    # flow lines and a scribbled ground under the big view, drawn first so the bike sits on them
    S.curve([(380, 1560), (1100, 1380), (1900, 1700), (2950, 1500)], 70)
    S.curve([(700, 1900), (1500, 1620), (2300, 1880), (3040, 1650)], 62)
    S.curve([(900, 420), (1500, 250), (2200, 330), (2700, 620)], 52)
    # the big three-quarter view
    mc = crop_to(main)
    k = min(1900 / mc.width, 1560 / mc.height)
    msz = (int(mc.width * k), int(mc.height * k))
    dk, al_ = pencil(mc, msz, shade=1.3, seed=2)
    mx, my = 800 + (1900 - msz[0]) // 2, 1960 - msz[1]
    S.place(dk, (mx, my))
    S.solid[my:my + msz[1], mx:mx + msz[0]] = al_
    for i in range(7):                                                     # ground shadow, quick strokes
        y = 1962 + i * 9
        S.line((mx + 180 + i * 50, y), (mx + msz[0] - 160 - i * 30, y - 8), 60 - i * 5, 60, 1, 3)
    # the side elevation with its proportions
    sc = crop_to(side, 8)
    sw = 860
    ssz = (sw, int(sc.height * sw / sc.width))
    dk, _ = pencil(sc, ssz, weight=0.8, shade=0.55, seed=4)
    sx, sy = 150, 250
    S.place(dk, (sx, sy))
    g, rf, rr = m["geometry"], m["wheels"]["front"]["tyre_r"], m["wheels"]["rear"]["tyre_r"]
    ground = sy + ssz[1] - 8 * sw / sc.width
    # the tyres are the widest thing at axle height, so the scale comes from the widest row of the cut-out
    al = np.asarray(sc.getchannel("A").resize(ssz)) > 40
    rows = [np.flatnonzero(r) for r in al[int(ssz[1] * 0.6):]]
    x_l, x_r = min(r[0] for r in rows if len(r)), max(r[-1] for r in rows if len(r))
    px = (x_r - x_l) / (g["wheelbase"] + rf + rr)
    cr, cf = (sx + x_l + rr * px, ground - rr * px), (sx + x_r - rf * px, ground - rf * px)
    S.line((sx - 30, ground), (sx + sw + 30, ground), 80, 50)
    for c, r in ((cr, rr * px), (cf, rf * px)):
        S.circle(c, r + 5, 62)
        S.line((c[0], c[1] - r - 30), (c[0], c[1] + r + 12), 62, 20, 1)
        S.line((c[0] - r - 20, c[1]), (c[0] + r + 20, c[1]), 62, 20, 1)
    rk = math.radians(g["rake"])
    S.line(cf, (cf[0] - math.sin(rk) * 420 * px * 2, cf[1] - math.cos(rk) * 420 * px * 2), 62, 60, 1)     # the steering axis
    yd = sy - 60
    S.line((cr[0], yd), (cf[0], yd), 110, 0, 1)
    for x in (cr[0], cf[0]):
        S.line((x, yd - 22), (x, cr[1] - rr * px - 40), 70, 10, 1)
        S.dr.line([(x - 9, yd + 9), (x + 9, yd - 9)], fill=130, width=3)
    S.text((int((cr[0] + cf[0]) / 2) - 190, int(yd) - 84), f"WHEELBASE {g['wheelbase']}", 46, 170, 0)
    S.text((sx + 10, int(ground) + 40), f"RAKE {g['rake']}°   TRAIL {g['trail']}   SEAT {g['seat_height']}", 40, 150, 0)
    # detail studies, each ringed and named
    for (tag, box, at, w, label, lab_at, arr) in details or []:
        srcim = main if tag == "front34" else side
        c = srcim.crop(box)
        dsz = (w, int(c.height * w / c.width))
        dk, _ = pencil(c, dsz, weight=0.95, shade=0.8, seed=9)
        yy, xx = np.mgrid[0:dsz[1], 0:dsz[0]]                                       # the study fades out at its edges
        fade = np.clip(1.25 - np.hypot((xx - dsz[0] / 2) / (dsz[0] / 2), (yy - dsz[1] / 2) / (dsz[1] / 2)), 0, 1) ** 0.8
        S.place(dk * fade, at)
        S.text(lab_at, label, 50, 185)
        S.arrow(arr)
    for (xy, s, size) in notes or []:
        S.text(xy, s, size, 185)
    if words:
        S.text((110, 1560), "\n".join(words), 60, 190)
        S.line((110, 1560 + 96 * len(words) + 40), (520, 1560 + 96 * len(words) + 28), 150, 10, 2, 3)
    return S.render(os.path.join(ROOT, "out", "sheets", name + ".jpg"))


SHEETS = {
    "corvano_cafe": dict(
        details=[("front34", (1500, 560, 2080, 1060), (2330, 110), 600, "SIGNATURE\nLAMP", (2700, 640), [(2860, 640), (2790, 560), (2700, 470)]),
                 ("side", (380, 520, 1250, 1040), (2330, 800), 640, "HUMP TAIL", (2640, 1190), [(2760, 1180), (2720, 1120), (2650, 1060)])],
        notes=[((1180, 90), "CORVANO  CAFE", 96), ((1190, 215), "90° V-twin  ·  steel trellis, engine as part of the frame", 44),
               ((2560, 1830), "FLOW\nLINES", 46)],
        words=["LEAN", "RED", "MECHANICAL"]),
}

if __name__ == "__main__":
    for n in (sys.argv[1:] or SHEETS):
        print(sheet(n, **SHEETS.get(n, {})))
