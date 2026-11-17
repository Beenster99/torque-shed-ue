"""Review sheets for the decal library: each shelf flat in two colour schemes and on a tank, and
the lettering faces spelling a name and a number. PNGs, for beenster to mark up."""
import math
import os

import numpy as np
import skia
from PIL import Image, ImageDraw, ImageFont

import dk
import fonts

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LABEL_FONT = os.path.join(ROOT, "Data", "Fonts", "Badge", "OpenSans-SemiBold.ttf")

SCHEMES = [
    ((0.78, 0.10, 0.07), (0.04, 0.04, 0.04)),    # red and black
    ((0.93, 0.74, 0.25), (0.98, 0.97, 0.94)),    # gold and white (shown on a dark tank)
]


def label_font(px):
    return ImageFont.truetype(LABEL_FONT, px)


def to_img(rgba):
    return Image.fromarray((np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")


def fit(img, w, h):
    s = min(w / img.width, h / img.height)
    return img.resize((max(1, int(img.width * s)), max(1, int(img.height * s))), Image.LANCZOS)


# ------------------------------------------------------------------------------------ tank mock


def tank_base(W, H, paint, gloss=True):
    """A tank seen from the side, shaded as a rounded body (a 2D mock: the real check is a build)."""
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    # outline: a teardrop tank, nose left
    u = xx / W
    top = H * (0.18 + 0.10 * (u - 0.45) ** 2 * 4 - 0.04 * np.sin(u * math.pi))
    bot = H * (0.86 - 0.20 * u ** 2)
    left = W * 0.04
    right = W * 0.97
    inside = (yy > top) & (yy < bot) & (xx > left) & (xx < right)
    # soften the nose and tail
    nose = ((xx - W * 0.12) / (W * 0.09)) ** 2 + ((yy - (top + bot) / 2) / ((bot - top) / 2)) ** 2
    inside &= (xx > W * 0.12) | (nose < 1)
    tail = ((xx - W * 0.90) / (W * 0.08)) ** 2 + ((yy - (top + bot) / 2) / ((bot - top) / 2)) ** 2
    inside &= (xx < W * 0.90) | (tail < 1)
    v = np.clip((yy - top) / np.maximum(bot - top, 1), 0, 1)
    # the curve of the side, top light, belly dark
    shade = 0.55 + 0.6 * np.sin(np.clip(v, 0, 1) * math.pi * 0.9 + 0.25) - 0.25 * v
    col = np.array(paint, np.float32)[None, None, :] * shade[..., None]
    if gloss:
        hl = np.exp(-((v - 0.22) / 0.06) ** 2) * np.exp(-((u - 0.45) / 0.35) ** 2)
        col = col + 0.55 * hl[..., None]
    a = inside.astype(np.float32)
    return np.concatenate([np.clip(col, 0, 1), a[..., None]], -1), (top, bot, v)


def on_tank(masks, c1, c2, paint, W=520, H=300, box=(0.18, 0.30, 0.66, 0.42), filled=True):
    base, (top, bot, v) = tank_base(W, H, paint)
    rgba = dk.compose(masks, c1, c2, filled=filled)
    img = to_img(rgba)
    bw, bh = int(W * box[2]), int(H * box[3])
    img = fit(img, bw, bh)
    x0 = int(W * box[0] + (bw - img.width) / 2)
    y0 = int(H * box[1] + (bh - img.height) / 2)
    dec = np.zeros((H, W, 4), np.float32)
    arr = np.asarray(img).astype(np.float32) / 255
    dec[y0:y0 + img.height, x0:x0 + img.width] = arr[: H - y0, : W - x0]
    # the decal takes the tank's light: darker on the belly, the gloss on top of it
    yy = np.arange(H, dtype=np.float32)[:, None]
    light = (0.55 + 0.6 * np.sin(np.clip(v, 0, 1) * math.pi * 0.9 + 0.25) - 0.25 * v)[..., None]
    da = dec[..., 3:4] * base[..., 3:4]
    col = base[..., :3] * (1 - da) + np.clip(dec[..., :3] * light, 0, 1) * da
    hl = np.exp(-((v - 0.22) / 0.06) ** 2)[..., None] * 0.35
    col = np.clip(col + hl * da, 0, 1)
    return to_img(np.concatenate([col, base[..., 3:4]], -1))


# ------------------------------------------------------------------------------------- sheets


def shelf_sheet(decals, path, title, cols=4, cell=(520, 640), schemes=None, dark2=True):
    schemes = schemes or SCHEMES
    rows = math.ceil(len(decals) / cols)
    CW, CH = cell
    head = 90
    sheet = Image.new("RGB", (cols * CW, head + rows * CH), (238, 235, 228))
    d = ImageDraw.Draw(sheet)
    d.text((24, 20), title, fill=(20, 20, 20), font=label_font(40))
    for i, dc in enumerate(decals):
        cx, cy = (i % cols) * CW, head + (i // cols) * CH
        d.rectangle([cx + 8, cy + 8, cx + CW - 8, cy + CH - 8], fill=(250, 249, 245), outline=(205, 200, 190))
        m = dc.masks(1.0)
        # flat, red/black on white
        a = fit(to_img(dk.compose(m, *schemes[0])), CW - 60, 150)
        sheet.paste(a, (cx + (CW - a.width) // 2, cy + 22 + (150 - a.height) // 2), a)
        # flat, gold/white on near-black
        d.rectangle([cx + 24, cy + 190, cx + CW - 24, cy + 340], fill=(22, 22, 24) if dark2 else (250, 249, 245))
        b = fit(to_img(dk.compose(m, *schemes[1])), CW - 80, 130)
        sheet.paste(b, (cx + (CW - b.width) // 2, cy + 200 + (130 - b.height) // 2), b)
        # on a tank
        t = on_tank(m, (0.95, 0.80, 0.30), (0.05, 0.05, 0.05), (0.08, 0.10, 0.22) if i % 2 else (0.45, 0.05, 0.05))
        t = fit(t, CW - 60, 200)
        sheet.paste(t, (cx + (CW - t.width) // 2, cy + 350), t)
        d.text((cx + 22, cy + CH - 76), f"{i + 1}. {dc.name}", fill=(20, 20, 20), font=label_font(24))
        d.text((cx + 22, cy + CH - 44), dc.id + ("   2 colours" if dc.colours == 2 else "   1 colour"), fill=(110, 105, 95), font=label_font(18))
    sheet.save(path, optimize=True)
    print("sheet", path)


def text_path(font_file, text, px, arch=0.0):
    """Text as a skia path, px tall, optionally on an arch (arch: the bend, 0 straight, 1 a lot)."""
    tf = skia.Typeface.MakeFromFile(font_file)
    font = skia.Font(tf, px)
    glyphs = font.textToGlyphs(text)
    widths = font.getWidths(glyphs)
    total = sum(widths)
    path = skia.Path()
    x = 0.0
    R = total / (arch * math.pi) if arch > 0 else None
    for g, w in zip(glyphs, widths):
        gp = font.getPath(g)
        if gp is None:
            x += w
            continue
        if R is None:
            gp.offset(x, 0)
        else:
            mid = x + w / 2 - total / 2
            ang = mid / R
            m = skia.Matrix()
            m.setTranslate(R * math.sin(ang), -R * math.cos(ang) + R)
            m.preRotate(math.degrees(ang))
            m.preTranslate(-w / 2, 0)
            gp.transform(m)
        path.addPath(gp)
        x += w
    return path


def lettering_sheet(path, words=("Reaper", "13")):
    """Each face spelling a word and a number, and the options: arch, outline, shadow."""
    faces = fonts.FONTS
    cols = 2
    CW, CH = 1040, 300
    head = 90
    rows = math.ceil(len(faces) / cols)
    extra = 520
    sheet = Image.new("RGB", (cols * CW, head + rows * CH + extra), (238, 235, 228))
    d = ImageDraw.Draw(sheet)
    d.text((24, 20), "Decal library: lettering (you type the words; each word is one decal)", fill=(20, 20, 20), font=label_font(36))
    for i, f in enumerate(faces):
        cx, cy = (i % cols) * CW, head + (i // cols) * CH
        d.rectangle([cx + 8, cy + 8, cx + CW - 8, cy + CH - 8], fill=(250, 249, 245), outline=(205, 200, 190))
        file = os.path.join(ROOT, "Data", "Fonts", f["file"])
        word = words[0].upper() if f["caps"] else words[0]
        img = render_text(file, f"{word}  {words[1]}", 150, (0.06, 0.06, 0.06), None)
        img = fit(img, CW - 80, 190)
        sheet.paste(img, (cx + 40, cy + 24 + (190 - img.height) // 2), img)
        tag = "new" if f["file"].startswith("Decal/") else "from the tank badges"
        d.text((cx + 24, cy + CH - 64), f"{i + 1}. {f['name']}  ({f['style']}, {tag})", fill=(20, 20, 20), font=label_font(24))
    # options
    oy = head + rows * CH + 20
    d.text((24, oy), "Options for any lettering: straight or arched, outline, drop shadow, two colours", fill=(20, 20, 20), font=label_font(28))
    rock = os.path.join(ROOT, "Data", "Fonts", "Decal", "NewRocker-Regular.ttf")
    frak = os.path.join(ROOT, "Data", "Fonts", "Decal", "UnifrakturCook-Bold.ttf")
    plate = os.path.join(ROOT, "Data", "Fonts", "Decal", "Bungee-Regular.ttf")
    samples = [
        render_text(frak, "Death Rider", 140, (0.85, 0.68, 0.22), None, arch=0.35),
        render_text(rock, "Ride Free", 140, (0.85, 0.12, 0.08), (0.02, 0.02, 0.02), outline_px=10),
        render_text(plate, "77", 220, (0.97, 0.97, 0.97), (0.80, 0.10, 0.07), shadow=(12, 14)),
        render_text(frak, "Hell Bound", 130, (0.97, 0.97, 0.97), (0.70, 0.08, 0.06), outline_px=8, shadow=(8, 8), arch=0.25),
    ]
    x = 24
    for k, s in enumerate(samples):
        s = fit(s, 500, 380)
        bg = (22, 22, 24) if k in (0, 2, 3) else (250, 249, 245)
        d.rectangle([x, oy + 60, x + 510, oy + 470], fill=bg)
        sheet.paste(s, (x + 5 + (500 - s.width) // 2, oy + 70 + (380 - s.height) // 2), s)
        x += 520
    sheet.save(path, optimize=True)
    print("sheet", path)


def render_text(font_file, text, px, c1, c2, arch=0.0, outline_px=0, shadow=None):
    p = text_path(font_file, text, px, arch)
    b = p.computeTightBounds()
    pad = int(px * 0.25) + outline_px + (max(shadow) if shadow else 0)
    W, H = int(b.width() + 2 * pad), int(b.height() + 2 * pad)
    p.offset(-b.left() + pad, -b.top() + pad)
    fillp = p
    linep = None
    if outline_px:
        linep = dk.diff(dk.stroke(p, outline_px * 2), p)
    if shadow:
        sh = skia.Path(dk.union(p, linep) if linep else p)
        sh.offset(*shadow)
        sh = dk.diff(sh, dk.union(p, linep) if linep else p)
        linep = dk.union(linep, sh) if linep else sh
    dc = dk.Decal("t", "lettering", "t", [], W, H, fill=fillp, line=linep)
    return to_img(dk.compose(dc.masks(), c1, c2 or (0, 0, 0)))


def make_all(shelves, out):
    os.makedirs(out, exist_ok=True)
    lettering_sheet(os.path.join(out, "sheet_lettering.png"))
    if "shapes" in shelves:
        shelf_sheet(shelves["shapes"], os.path.join(out, "sheet_stripes_shapes.png"), "Decal library: stripes and shapes (flat in two colour schemes, then on a tank)")
    if "art" in shelves:
        shelf_sheet(shelves["art"], os.path.join(out, "sheet_artwork.png"), "Decal library: tattoo flash, first 20 (bone and black, red and black, then gold and black on a tank)",
                    schemes=[((0.93, 0.88, 0.76), (0.03, 0.03, 0.03)), ((0.78, 0.10, 0.07), (0.03, 0.03, 0.03))], dark2=False)
