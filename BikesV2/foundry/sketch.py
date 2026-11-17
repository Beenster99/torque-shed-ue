"""Artist-sketch versions of the bike renders, for the designer portfolio (beenster, 14 Nov).

    BIKE_SKETCH=1 python3 foundry/build3d.py <model> --definition --samples 16 --shots side,front34
    python3 foundry/sketch.py <model>_def_shot_side [pencil|marker|blueprint]     -> out/sketch/<name>_<style>.jpg

The source is a cut-out of the bike alone (out/sketch_src). Lines are found in the picture itself
(brightness edges and the silhouette), so every style keeps the model's real proportions.
"""
import os
import sys
import numpy as np
from PIL import Image, ImageFilter, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STYLES = ("pencil", "marker", "blueprint")


def _paper(size, base, grain, seed=3):
    rng = np.random.default_rng(seed)
    n = Image.fromarray((rng.random((size[1] // 2, size[0] // 2)) * 255).astype("uint8")).resize(size, Image.BICUBIC).filter(ImageFilter.GaussianBlur(1.2))
    n = (np.asarray(n, dtype="float32") / 255 - 0.5) * grain
    return np.clip(np.array(base, dtype="float32")[None, None, :] + n[..., None], 0, 255)


def _lines(grey, alpha, fine=1.2, bold=3.0):
    """0..1 line darkness: fine detail lines from the dodge of the picture against its own blur, plus a
    heavier outline round the silhouette, like a designer's weighted outline."""
    g = np.asarray(grey, dtype="float32")
    inv_blur = np.asarray(ImageChops.invert(grey).filter(ImageFilter.GaussianBlur(fine * 2.2)), dtype="float32")
    dodge = np.clip(g * 255.0 / np.maximum(255.0 - inv_blur, 1.0), 0, 255)
    fine_l = np.clip((250.0 - dodge) / 70.0, 0, 1) ** 0.8
    edge = np.asarray(alpha.filter(ImageFilter.FIND_EDGES).filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(bold * 0.45)), dtype="float32") / 255
    return np.clip(fine_l + np.clip(edge * 2.2, 0, 1), 0, 1)


def sketch(src, style="pencil", out=None):
    im = Image.open(src).convert("RGBA")
    w, h = im.size
    alpha = im.getchannel("A")
    a = np.asarray(alpha, dtype="float32")[..., None] / 255
    rgb = np.asarray(im.convert("RGB"), dtype="float32")
    white = rgb * a + 255 * (1 - a)
    grey = Image.fromarray(white.astype("uint8")).convert("L")
    ln = _lines(grey, alpha)[..., None]
    if style == "pencil":
        paper = _paper((w, h), (246, 242, 232), 9)
        tone = np.asarray(grey.filter(ImageFilter.GaussianBlur(5)), dtype="float32")[..., None]
        shade = np.clip((150 - tone) / 150, 0, 1) * 0.42 * a                # soft graphite shading in the darks
        hatch = (((np.add.outer(np.arange(h), np.arange(w)) % 9) < 3)[..., None]) * 0.5 + 0.5
        res = paper * (1 - shade * hatch) * (1 - ln * 0.86) + np.array((38, 38, 44), dtype="float32") * ln * 0.86
    elif style == "marker":
        paper = _paper((w, h), (250, 248, 243), 6)
        soft = np.asarray(Image.fromarray(white.astype("uint8")).filter(ImageFilter.MedianFilter(9)).filter(ImageFilter.GaussianBlur(2)), dtype="float32")
        q = np.round(soft / 36) * 36                                         # flat marker tones
        wash = q * 0.72 + 255 * 0.28
        res = paper * (1 - a) + (wash * 0.9 + paper * 0.1) * a
        res = res * (1 - ln * 0.92) + np.array((20, 20, 24), dtype="float32") * ln * 0.92
    else:
        paper = _paper((w, h), (24, 62, 110), 7)
        gridm = ((np.arange(h)[:, None] % 100 < 2) | (np.arange(w)[None, :] % 100 < 2))[..., None]
        paper = paper + gridm * 16
        tone = np.asarray(grey.filter(ImageFilter.GaussianBlur(4)), dtype="float32")[..., None]
        fill = np.clip((200 - tone) / 200, 0, 1) * 0.16 * a
        res = paper * (1 - fill) + 255 * fill * 0.25
        res = res * (1 - ln * 0.9) + np.array((232, 240, 250), dtype="float32") * ln * 0.9
    res = Image.fromarray(np.clip(res, 0, 255).astype("uint8")).resize((w // 2, h // 2), Image.LANCZOS)
    out = out or os.path.join(ROOT, "out", "sketch", os.path.basename(src)[:-4] + f"_{style}.jpg")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    res.save(out, quality=90)
    return out


if __name__ == "__main__":
    name = sys.argv[1]
    for st in (sys.argv[2:] or STYLES):
        print(sketch(os.path.join(ROOT, "out", "sketch_src", name + ".png"), st))
