#!/usr/bin/env python3
"""Draws Data/Props/Textures/courier_labels.png: the shipping labels of Torque Shed's made-up
delivery firms (bike_model.gd COURIERS, no real companies), one 1024 x 640 label per row, 8 rows.
The game picks a box's row (its 'Courier' parameter, from the item id as courier_for does) and
writes what's in the box and the tracking number into the blank strip (Data/Props/deliveries.json
'label_text'). Needs Pillow; deterministic.

    python3 Tools/props/make_labels.py
"""
import json, os, random
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPEC = json.load(open(os.path.join(ROOT, "Data", "Props", "deliveries.json")))
W, H = 1024, 640
FONTS = ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
         "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"]


def font(i, size):
    return ImageFont.truetype(FONTS[i], size)


def c255(c):
    return tuple(int(round(max(0.0, min(1.0, v)) * 255)) for v in c)


def mark(d, kind, x, y, s, ink):
    if kind == "disc":
        d.ellipse([x - s, y - s, x + s, y + s], fill=ink)
        d.ellipse([x - s * 0.45, y - s * 0.45, x + s * 0.45, y + s * 0.45], fill=None, outline=None)
    elif kind == "diamond":
        d.polygon([(x, y - s), (x + s, y), (x, y + s), (x - s, y)], fill=ink)
    elif kind == "chevron":
        for k in (0, 1):
            ox = x - s * 0.6 + k * s * 0.8
            d.polygon([(ox, y - s), (ox + s * 0.5, y - s), (ox + s * 1.1, y), (ox + s * 0.5, y + s), (ox, y + s), (ox + s * 0.6, y)], fill=ink)
    else:
        for k in range(3):
            w = s * 2 * (1.0 - k * 0.25)
            d.rectangle([x - s + k * s * 0.25, y - s + k * s * 0.7, x - s + k * s * 0.25 + w, y - s + k * s * 0.7 + s * 0.38], fill=ink)


def main():
    couriers = SPEC["couriers"]
    atlas = Image.new("RGB", (W, H * len(couriers)), (242, 240, 230))
    for row, c in enumerate(couriers):
        img = Image.new("RGB", (W, H), (242, 240, 230))
        d = ImageDraw.Draw(img)
        bg, ink = c255(c["bg"]), c255(c["ink"])
        band = 190
        d.rectangle([0, 0, W, band], fill=bg)
        mark(d, c["mark"], 110, band // 2, 62, ink)
        name = c["name"]
        size = 96
        while d.textlength(name, font=font(0, size)) > W - 250 and size > 40:
            size -= 4
        d.text((205, band // 2), name, font=font(0, size), fill=ink, anchor="lm")
        d.text((W - 24, band - 14), c.get("tagline", ""), font=font(1, 26), fill=ink, anchor="rs")
        # Delivery address block and the service box.
        d.text((30, band + 30), "DELIVER TO", font=font(0, 26), fill=(40, 40, 40))
        for k, line in enumerate(SPEC["address"]):
            d.text((30, band + 66 + k * 38), line, font=font(1, 34), fill=(20, 20, 20))
        svc = c.get("service", "NEXT DAY")
        d.rectangle([W - 300, band + 26, W - 26, band + 150], outline=(20, 20, 20), width=6)
        d.text((W - 163, band + 88), svc, font=font(0, 40), fill=(20, 20, 20), anchor="mm")
        # The blank strip the game writes in: what's in the box and the tracking number.
        y0 = SPEC["label_text"]["strip_v"][0] * H
        d.line([(30, y0), (W - 30, y0)], fill=(60, 60, 60), width=3)
        d.text((30, y0 + 8), "CONTENTS / TRACKING", font=font(1, 22), fill=(110, 110, 110))
        # A barcode down the right of the strip, the same for every box of a firm.
        rng = random.Random(name)
        x = W - 330
        while x < W - 30:
            w = rng.choice((3, 3, 5, 8))
            d.rectangle([x, y0 + 40, x + w, H - 30], fill=(15, 15, 15))
            x += w + rng.choice((3, 4, 6))
        d.rectangle([0, 0, W - 1, H - 1], outline=(200, 198, 188), width=4)
        atlas.paste(img, (0, row * H))
    out = os.path.join(ROOT, "Data", "Props", "Textures", "courier_labels.png")
    atlas.save(out, optimize=True)
    print("wrote", out, atlas.size)


if __name__ == "__main__":
    main()
