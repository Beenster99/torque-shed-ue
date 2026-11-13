"""The high cloud's sheets (skies plan stage 3): Data/Sky/high_cloud.png, made once with numpy.

Cirrus is thin and nine kilometres up, so it isn't drawn as a volume: it is a textured sheet over the
sky (M_RideHighCloud, Tools/ue_build_level.py). Four tiling sheets, one per channel of a 512 x 512
picture, all noise shaped in the frequency domain (so they tile by construction):
  R  wisps: fibres drawn out along one way and bent by a slow warp (cirrus, mares' tails)
  G  veil: a smooth, even sheet with soft thicker patches (cirrostratus)
  B  ripples: small cells in rows (cirrocumulus, the mackerel sky)
  A  detail: finer fibres, to break up the others close to the sun
Run: python Tools/make_high_cloud.py  (needs numpy and Pillow)
"""
import os

import numpy as np

N = 512
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "Sky", "high_cloud.png")
rng = np.random.default_rng(20261113)
k = np.fft.fftfreq(N) * N   # cycles across the tile
KX, KY = np.meshgrid(k, k, indexing="xy")


def shaped(power, stretch=1.0, angle=0.0):
    """Tiling noise whose spectrum is power(f), f the frequency with x stretched (fibres along x)."""
    c, s = np.cos(angle), np.sin(angle)
    u = KX * c + KY * s
    v = -KX * s + KY * c
    f = np.sqrt((u * stretch) ** 2 + v ** 2)
    f[0, 0] = 1.0
    amp = power(f)
    amp[0, 0] = 0.0
    phase = np.exp(2j * np.pi * rng.random((N, N)))
    out = np.real(np.fft.ifft2(amp * phase))
    return (out - out.mean()) / (out.std() + 1e-9)


def warp(img, dx, dy):
    """img sampled at (x + dx, y + dy) texels, wrapping (bilinear)."""
    y, x = np.mgrid[0:N, 0:N].astype(np.float64)
    sx, sy = (x + dx) % N, (y + dy) % N
    x0, y0 = np.floor(sx).astype(int), np.floor(sy).astype(int)
    fx, fy = sx - x0, sy - y0
    x1, y1 = (x0 + 1) % N, (y0 + 1) % N
    return (img[y0, x0] * (1 - fx) * (1 - fy) + img[y0, x1] * fx * (1 - fy)
            + img[y1, x0] * (1 - fx) * fy + img[y1, x1] * fx * fy)


def norm(a, lo=2.0, hi=98.0):
    p0, p1 = np.percentile(a, lo), np.percentile(a, hi)
    return np.clip((a - p0) / (p1 - p0 + 1e-9), 0.0, 1.0)


# R: wisps. Fibres along x (a spectrum squeezed in x), in streaky bunches (a slow mask), bent by a warp.
fibres = shaped(lambda f: f ** -1.6 * (f > 1.5), stretch=7.0)
bunches = norm(shaped(lambda f: f ** -2.5 * (f < 12)), 30, 99)
wx = shaped(lambda f: f ** -3.0 * (f < 6)) * 18.0
wy = shaped(lambda f: f ** -3.0 * (f < 6)) * 30.0
wisps = warp(fibres, wx, wy) * 0.6 + 0.4
wisps = norm(np.maximum(wisps, 0.0) * bunches ** 1.5)

# G: veil. Mostly even, with soft thicker and thinner patches.
veil = norm(shaped(lambda f: f ** -2.2 * (f > 0.5)))
veil = 0.55 + 0.45 * veil

# B: ripples. Rows of small cells (a wave across the rows, broken along them), in patches.
y, x = np.mgrid[0:N, 0:N] / N
rows = 0.5 + 0.5 * np.cos(2 * np.pi * (24 * y + 0.4 * shaped(lambda f: f ** -3.0 * (f < 8))))
cells = norm(shaped(lambda f: np.exp(-((f - 40.0) / 10.0) ** 2), stretch=0.6))
patches = norm(shaped(lambda f: f ** -2.5 * (f < 10)), 35, 99)
ripples = norm(rows * cells * patches)

# A: detail fibres, finer and at an angle to the wisps.
detail = norm(shaped(lambda f: f ** -1.2 * (f > 4), stretch=4.0, angle=0.4))

img = (np.stack([wisps, veil, ripples, detail], -1) * 255.0 + 0.5).astype(np.uint8)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
from PIL import Image  # noqa: E402

Image.fromarray(img, "RGBA").save(OUT)
print("wrote %s (%d x %d, wisps / veil / ripples / detail)" % (OUT, N, N))
