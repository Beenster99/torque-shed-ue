#!/usr/bin/env python3
"""Makes the brushed-metal 'scan' the shed's painted machinery wears (Data/Props/materials.json
'scans': brushed_metal) in place of Poly Haven's tread plate: fine streaks all one way, as a
sanded and lacquered steel panel shows under its paint. Poly Haven has no brushed metal, so it's
drawn here and kept in git, in the same three maps a fetched scan has (Tools/fetch_props.py):

    python Tools/props/make_brushed_scan.py      -> Data/Props/Scans/brushed_metal/{color,rough,normal}.jpg

The maps tile (the streaks are filtered in Fourier space, so they wrap), a metre across at the
props' Tile 1. M_PropMaster reads the colour's light and shade at x2 gain and the roughness at x2,
so both sit about 0.5 on average: the material's own Tint and Rough stay what they say.
"""
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "Data", "Props", "Scans", "brushed_metal")
N = 1024


def band(rng, along, across):
    """Tileable noise stretched along x: a long, thin Gaussian filter on white noise, in Fourier space."""
    f = np.fft.fft2(rng.standard_normal((N, N)))
    ky = np.fft.fftfreq(N)[:, None]
    kx = np.fft.fftfreq(N)[None, :]
    g = np.exp(-0.5 * ((kx * along) ** 2 + (ky * across) ** 2))
    out = np.real(np.fft.ifft2(f * g))
    return out / (out.std() + 1e-9)


def main():
    rng = np.random.default_rng(7)
    # Streaks: the fine scratches (long and a pixel or two across), a few deeper ones, and a soft
    # cloud so a big panel isn't perfectly even.
    fine = band(rng, 160.0, 0.6)
    deep = band(rng, 400.0, 1.2)
    deep = np.where(deep > 1.8, deep - 1.8, 0.0)
    cloud = band(rng, 30.0, 30.0)
    h = fine + 1.5 * deep
    colour = 0.5 + 0.05 * fine - 0.05 * deep + 0.01 * cloud
    rough = 0.5 + 0.07 * fine + 0.08 * deep + 0.03 * cloud
    # Normal (OpenGL, +y up the map): the slope across the streaks only; along them it's flat.
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5
    nx = np.zeros_like(dy)
    ny = -dy * 0.04
    nz = np.ones_like(dy)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    nrm = np.stack([nx / ln, ny / ln, nz / ln], axis=-1) * 0.5 + 0.5
    os.makedirs(OUT, exist_ok=True)

    def save(a, name):
        img = np.clip(a * 255.0 + 0.5, 0, 255).astype(np.uint8)
        Image.fromarray(img).save(os.path.join(OUT, name), quality=92)

    grey = np.clip(colour, 0.0, 1.0)
    save(np.stack([grey] * 3, axis=-1), "color.jpg")
    save(np.clip(rough, 0.0, 1.0), "rough.jpg")
    save(nrm, "normal.jpg")
    print("brushed metal maps in", OUT)


if __name__ == "__main__":
    main()
