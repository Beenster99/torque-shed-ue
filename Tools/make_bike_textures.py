#!/usr/bin/env python3
"""Make the Speed Twin's tileable detail textures into Data/Bike/Textures: for each set, a normal
map (<name>_n.png, DirectX green as Unreal wants) and a detail map (<name>_d.png: red is a roughness
offset about 0.5, green a shade the colour is multiplied by). They're procedural, made with numpy,
so there's nothing to download; the level script imports them and the finishes in
Tools/make_speedtwin.py pick a set each.

    python Tools/make_bike_textures.py

Sets: flat, flake (metallic paint), brushed (streaks along V), machined (streaks along U: concentric
on a disc), cast (sand-cast alloy), powder (powder coat), rubber, knurl (grips), leather (seat) and
tyre (one tread repeat, bead to bead, wrapped 24 times round each tyre). Decals (decal_<name>_a.png:
coverage, for a masked material) cover the side-view boxes in make_speedtwin.DECALS: the tank's
"1200" and pinstripes, measured off the drone profile, and "SPEED TWIN" on the side panels.
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "Data", "Bike", "Textures")
rng = np.random.default_rng(1200)


def band_noise(h, w, lo, hi, sx=1.0, sy=1.0):
    """Tileable noise with spatial frequencies between lo and hi (cycles per tile), stretched by sx, sy
    (sx > 1 makes features longer along x). Normalised to zero mean, unit spread."""
    fy = np.fft.fftfreq(h)[:, None] * h * sy
    fx = np.fft.fftfreq(w)[None, :] * w * sx
    f = np.sqrt(fx ** 2 + fy ** 2)
    filt = np.exp(-0.5 * ((np.log(np.maximum(f, 1e-6)) - np.log(np.sqrt(lo * hi))) / (0.5 * np.log(hi / lo) + 0.3)) ** 2)
    filt[0, 0] = 0
    n = np.real(np.fft.ifft2(np.fft.fft2(rng.standard_normal((h, w))) * filt))
    return (n - n.mean()) / (n.std() + 1e-12)


def normal_map(height, strength):
    """Normals from a tileable height field (in pixels' worth of slope times strength), DirectX style."""
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 0.5 * strength
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 0.5 * strength
    n = np.stack([-dx, dy, np.ones_like(height)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return np.round((n * 0.5 + 0.5) * 255).astype(np.uint8)


def save(name, height, strength, rough, shade):
    os.makedirs(OUT, exist_ok=True)
    Image.fromarray(normal_map(height, strength)).save(os.path.join(OUT, name + "_n.png"), optimize=True)
    d = np.zeros(height.shape + (3,), np.uint8)
    d[..., 0] = np.round(np.clip(0.5 + rough, 0, 1) * 255)
    d[..., 1] = np.round(np.clip(shade, 0, 1) * 255)
    Image.fromarray(d).save(os.path.join(OUT, name + "_d.png"), optimize=True)
    print(name, height.shape)


def worley(h, w, n_pts):
    """Tileable distance to the nearest of n random points (F1) and to the second (F2), in pixels."""
    pts = rng.random((n_pts, 2)) * [h, w]
    yy, xx = np.mgrid[0:h, 0:w]
    f1 = np.full((h, w), 1e9); f2 = np.full((h, w), 1e9)
    for py, px in pts:
        dy = np.abs(yy - py); dy = np.minimum(dy, h - dy)
        dx = np.abs(xx - px); dx = np.minimum(dx, w - dx)
        d = np.sqrt(dx * dx + dy * dy)
        f2 = np.where(d < f1, f1, np.minimum(f2, d))
        f1 = np.minimum(f1, d)
    return f1, f2


def wrap_dist_to_segments(X, Y, pts, period_x):
    """Distance (in the same units) from each (X, Y) to a polyline, wrapping in X."""
    best = np.full(X.shape, 1e9); along = np.zeros(X.shape)
    total = sum(np.hypot(*(np.subtract(pts[i + 1], pts[i]))) for i in range(len(pts) - 1))
    acc = 0.0
    for i in range(len(pts) - 1):
        a = np.array(pts[i], float); b = np.array(pts[i + 1], float)
        ab = b - a; L2 = ab @ ab; L = np.sqrt(L2)
        for shift in (-period_x, 0.0, period_x):
            px = X - (a[0] + shift); py = Y - a[1]
            t = np.clip((px * ab[0] + py * ab[1]) / L2, 0, 1)
            d = np.hypot(px - t * ab[0], py - t * ab[1])
            closer = d < best
            best = np.where(closer, d, best)
            along = np.where(closer, (acc + t * L) / total, along)
        acc += L
    return best, along


FONT = "/usr/share/fonts/opentype/inter/Inter-ExtraBold.otf"


def decal(name, box, w, h, draw_fn):
    """Draw a decal in metres (side view, x forward, y up) at 4x and filter down. The image is two
    w x h halves side by side: the right side's art, then the left side's (draw_fn gets left=True),
    which the left side sees mirrored."""
    from PIL import ImageDraw
    x0, x1, y0, y1 = box
    k = 4
    halves = []
    for left in (False, True):
        im = Image.new("L", (w * k, h * k), 0)
        d = ImageDraw.Draw(im)

        def px(x, y):
            return ((x - x0) / (x1 - x0) * w * k, (y1 - y) / (y1 - y0) * h * k)
        draw_fn(d, px, im, left)
        halves.append(im.resize((w, h), Image.LANCZOS))
    im = Image.new("L", (2 * w, h), 0)
    im.paste(halves[0], (0, 0)); im.paste(halves[1], (w, 0))
    os.makedirs(OUT, exist_ok=True)
    im.save(os.path.join(OUT, "decal_%s_a.png" % name), optimize=True)
    print("decal", name, (w, h))


def text_at(im, px, text, x0, x1, ya, yb, slant=0.22, fill=255, mirror=False):
    """Italic text filling the side-view box x0..x1, ya (baseline)..yb (cap height)."""
    from PIL import ImageFont, ImageDraw
    (ax, ay), (bx, by) = px(x0, yb), px(x1, ya)
    W, H = int(bx - ax), int(by - ay)
    font = ImageFont.truetype(FONT, 200)
    l, t, r, b = font.getbbox(text)
    t_im = Image.new("L", (r - l + 60, b - t + 20), 0)
    ImageDraw.Draw(t_im).text((30 - l, 10 - t), text, font=font, fill=fill)
    sw, sh = t_im.size
    # Shear for the italic, then fit to the box.
    t_im = t_im.transform((sw + int(sh * slant), sh), Image.AFFINE, (1, -slant, sh * slant, 0, 1, 0), Image.BICUBIC)
    t_im = t_im.crop(t_im.getbbox()).resize((max(W, 1), max(H, 1)), Image.LANCZOS)
    if mirror:
        t_im = t_im.transpose(Image.FLIP_LEFT_RIGHT)
    im.paste(255, (int(ax), int(ay)), t_im)


def decals():
    import sys
    sys.path.insert(0, HERE)
    from make_speedtwin import DECALS

    def tank(d, px, im, left):
        # Two parallel lines falling towards the front: a bold upper one broken by the "1200" and a
        # finer one under it. Ends are cut square at the back and swept at the front.
        def band(y0, slope, half, xa, xb, cut=0.012):
            pts = [(xa, y0 + slope * xa + half), (xb, y0 + slope * xb + half),
                   (xb - cut, y0 + slope * (xb - cut) - half), (xa, y0 + slope * xa - half)]
            d.polygon([px(x, y) for x, y in pts], fill=255)
        band(0.4735, -0.117, 0.0022, -0.106, -0.042)
        band(0.4735, -0.117, 0.0022, 0.075, 0.368)
        band(0.4659, -0.117, 0.0012, -0.102, 0.360)
        text_at(im, px, "1200", -0.038, 0.072, 0.4745, 0.498, mirror=left)

    def panel(d, px, im, left):
        text_at(im, px, "SPEED TWIN", -0.385, -0.228, 0.392, 0.408, slant=0.25, mirror=left)

    # Drilled brake rotor: one repeat (u) of two staggered holes, v round the disc's section: the
    # inner face 0..0.45 (bore to rim), the rim, the outer face 0.5..0.95 (rim to bore), the bore.
    from PIL import ImageDraw
    W, H, k = 128, 512, 4
    im = Image.new("L", (W * k, H * k), 255)
    d = ImageDraw.Draw(im)
    r = 15.4 * k
    for u, f in ((0.25, 0.33), (0.75, 0.68)):
        for v in (f * 0.45, 0.5 + (1 - f) * 0.45):
            for du in (-1, 0, 1):                 # wrap across the tile's edges
                cx, cy = (u + du) * W * k, v * H * k
                d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=0)
    im.resize((W, H), Image.LANCZOS).save(os.path.join(OUT, "disc_holes_a.png"), optimize=True)
    print("disc holes", (W, H))
    decal("tank", DECALS["tank"], 2048, 448, tank)
    decal("panel", DECALS["panel"], 1024, 512, panel)


def main():
    flat = np.zeros((4, 4))
    save("flat", flat, 1.0, flat, flat + 1.0)

    # Metallic flake: tiny cells of slightly different brightness and gloss under the lacquer.
    s = 512
    cells = rng.random((s // 2, s // 2))
    cells = np.kron(cells, np.ones((2, 2)))
    save("flake", np.zeros((s, s)), 1.0, (cells - 0.5) * 0.3, 0.86 + 0.14 * cells)

    # Brushed: fine streaks along V; machined: the same along U.
    streak = band_noise(s, s, 20, 200, sx=1.0, sy=40.0) * 0.7 + band_noise(s, s, 60, 250, sx=1.0, sy=12.0) * 0.3
    slow = band_noise(s, s, 2, 8)
    save("brushed", streak, 0.25, streak * 0.08 + slow * 0.04, 0.95 + 0.04 * np.tanh(streak) + 0.01 * slow)
    save("machined", streak.T, 0.25, streak.T * 0.08 + slow.T * 0.04, 0.95 + 0.04 * np.tanh(streak.T))

    # Sand-cast alloy: grain at a couple of scales and a few soft patches.
    grain = band_noise(s, s, 80, 250) * 0.7 + band_noise(s, s, 20, 60) * 0.3
    patch = band_noise(s, s, 2, 6)
    save("cast", grain, 0.35, grain * 0.06 + patch * 0.06, 0.93 + 0.03 * grain + 0.03 * patch)

    # Powder coat: a soft orange peel.
    peel = band_noise(s, s, 25, 50)
    save("powder", peel, 0.12, peel * 0.04, 0.97 + 0.02 * peel)

    # Rubber: fine stipple.
    stip = band_noise(s, s, 120, 250) * 0.8 + band_noise(s, s, 30, 60) * 0.2
    save("rubber", stip, 0.3, stip * 0.05, 0.94 + 0.04 * stip)

    # Knurled grips: raised diamonds, 16 a tile each way.
    yy, xx = np.mgrid[0:s, 0:s] / s * 2 * np.pi * 16
    kn = -np.abs(np.sin((xx + yy) / 2)) - np.abs(np.sin((xx - yy) / 2))
    kn = kn - kn.mean()
    save("knurl", kn, 6.0, -kn * 0.1, 0.9 + 0.08 * (kn - kn.min()) / (kn.max() - kn.min()))

    # Leather: pebble grain (cells) with soft creases.
    f1, f2 = worley(s, s, 900)
    edge = np.clip((f2 - f1) / 4.0, 0, 1)          # 0 in the creases between pebbles
    pebble = edge ** 0.6 + 0.15 * band_noise(s, s, 60, 200)
    crease = band_noise(s, s, 3, 10, sx=4.0, sy=1.0)
    hgt = pebble * 1.0 - 0.6 * np.clip(np.abs(crease) < 0.08, 0, 1)
    save("leather", hgt - hgt.mean(), 1.4, (1 - edge) * 0.12 - 0.04, 0.9 + 0.1 * edge)

    # Tyre: one repeat of tread, u round (about 78 mm of a 600 mm tyre), v across from bead to bead
    # (about 280 mm of arc). The crown is slick; long swept grooves sit on each shoulder, alternating
    # sides; the sidewalls are smoother with a couple of moulding ribs.
    H, W = 1024, 512
    PX, PY = 78.0, 280.0                                  # millimetres the tile covers
    Y, X = np.mgrid[0:H, 0:W]
    X = (X + 0.5) / W * PX; Y = (Y + 0.5) / H * PY
    hgt = np.zeros((H, W)); dark = np.zeros((H, W))
    grooves = [
        [(4.0, 82.0), (22.0, 92.0), (44.0, 104.0), (62.0, 118.0)],           # one shoulder
        [(43.0, 198.0), (61.0, 188.0), (83.0, 176.0), (101.0, 162.0)],       # the other, half a repeat on
        [(30.0, 70.0), (44.0, 78.0)],                                        # short kicks near the edge
        [(69.0, 210.0), (83.0, 202.0)],
    ]
    for g in grooves:
        d, t = wrap_dist_to_segments(X, Y, g, PX)
        width = 2.2 * np.clip(np.sin(np.pi * np.clip(t, 0, 1)) * 1.6, 0.25, 1.0)   # tapered ends
        inside = np.clip((width - d) / 0.6, 0, 1)
        hgt -= inside * 3.0; dark = np.maximum(dark, inside)
    v = Y / PY
    side = (v < 0.2) | (v > 0.8)
    rib = np.zeros_like(v)
    for vc in (0.12, 0.88, 0.205, 0.795):
        rib += np.exp(-((v - vc) / 0.0025) ** 2)
    hgt += rib * 0.8 + band_noise(H, W, 150, 400) * 0.06
    rough = np.where(side, -0.12, 0.05) + dark * 0.1 + band_noise(H, W, 10, 40) * 0.03
    shade = np.where(side, 0.82, 1.0) - dark * 0.35 + 0.03 * band_noise(H, W, 40, 120)
    save("tyre", hgt, 1.6, rough, shade)
    decals()


if __name__ == "__main__":
    main()
