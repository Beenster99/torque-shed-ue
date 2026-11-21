"""Ride UK's water detail textures (real water plan, Nov 21 2026; beenster: "make the water finally look
like water"), made once here, tiling, into Data/Textures/Water beside make_water_normal.py's normal.png:

  foam.png       the white of the water: lacy foam (cells' edges, broken up), R the foam, 4 m across. The
                 water material shows it at the shore (lapping, surf), on whitecaps and in white water.
  rain.png       raindrops landing on still water, 1 m across: R the distance from the nearest drop's
                 centre (0 there .. 1 at 6 cm out), G when it lands (0..1 of the cycle), B and A which
                 way its centre lies (x, y as 0..1). The material grows a ring out from each in turn.
  caustics.png   the sun's light netted on the bed under shallow water, 3 m across (the land's material).

    python Tools/make_water_fx.py
"""
import os
import numpy as np
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "Textures", "Water")


def worley(size, cells, seed, power=1):
    """Distances to the nearest and second nearest of cells x cells jittered points, tiling."""
    rng = np.random.default_rng(seed)
    pts = np.stack(np.meshgrid(np.arange(cells), np.arange(cells), indexing="ij"), -1) + rng.random((cells, cells, 2))
    pts = pts.reshape(-1, 2) / cells
    y, x = np.mgrid[0:size, 0:size] / size
    d1 = np.full((size, size), 9.0)
    d2 = np.full((size, size), 9.0)
    near = np.zeros((size, size), int)
    for i, (px, py) in enumerate(pts):
        dx = np.abs(x - px); dx = np.minimum(dx, 1 - dx)
        dy = np.abs(y - py); dy = np.minimum(dy, 1 - dy)
        d = np.hypot(dx, dy)
        closer = d < d1
        d2 = np.where(closer, d1, np.minimum(d2, d))
        near = np.where(closer, i, near)
        d1 = np.where(closer, d, d1)
    return d1, d2, near, pts


def smooth_noise(size, scale, seed):
    """Tiling value noise (scale cells across), bicubic-ish through FFT low pass."""
    rng = np.random.default_rng(seed)
    f = np.fft.fft2(rng.normal(size=(size, size)))
    k = np.fft.fftfreq(size) * size
    kx, ky = np.meshgrid(k, k)
    f *= np.exp(-(np.hypot(kx, ky) / scale) ** 2)
    n = np.fft.ifft2(f).real
    return (n - n.min()) / (n.max() - n.min())


def foam(size=512):
    d1, d2, _, _ = worley(size, 14, 3)
    edge = np.clip(1 - (d2 - d1) * 14 * 2.2, 0, 1) ** 1.5            # thin bright filaments round bubbles
    e1, e2, _, _ = worley(size, 37, 5)
    fine = np.clip(1 - (e2 - e1) * 37 * 2.5, 0, 1)
    breakup = smooth_noise(size, 9, 7)
    f = np.clip(edge * 0.8 + fine * 0.45, 0, 1) * np.clip(breakup * 1.6 - 0.15, 0, 1)
    f = np.clip(f + np.clip(breakup - 0.6, 0, 1) * 1.2, 0, 1)        # dense patches where the noise is high
    return (f * 255).astype(np.uint8)


def rain(size=256, drops=10):
    d1, _, near, pts = worley(size, drops, 11)
    rng = np.random.default_rng(13)
    phase = rng.random(len(pts))
    y, x = np.mgrid[0:size, 0:size] / size
    px, py = pts[near, 0], pts[near, 1]
    dx = (px - x + 0.5) % 1 - 0.5
    dy = (py - y + 0.5) % 1 - 0.5
    ln = np.maximum(np.hypot(dx, dy), 1e-6)
    r = np.clip(d1 * drops / 0.6, 0, 1)                                # (0 .. 1 out to 0.6 of a cell)
    img = np.stack([r, phase[near], dx / ln * 0.5 + 0.5, dy / ln * 0.5 + 0.5], -1)
    return (img * 255).round().astype(np.uint8)


def caustics(size=512):
    out = np.zeros((size, size))
    for seed, cells in ((21, 9), (23, 13)):
        d1, d2, _, _ = worley(size, cells, seed)
        out += np.clip(1 - (d2 - d1) * cells * 3.0, 0, 1) ** 2.2
    out = np.clip(out / out.max() * 1.3, 0, 1)
    return (out * 255).astype(np.uint8)


def main():
    os.makedirs(OUT, exist_ok=True)
    Image.fromarray(foam(), "L").save(os.path.join(OUT, "foam.png"))
    Image.fromarray(rain(), "RGBA").save(os.path.join(OUT, "rain.png"))
    Image.fromarray(caustics(), "L").save(os.path.join(OUT, "caustics.png"))
    print("wrote foam.png, rain.png, caustics.png in %s" % OUT)


if __name__ == "__main__":
    main()
