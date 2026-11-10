#!/usr/bin/env python3
"""Makes the 'scans' the shed's painted machinery wears (Data/Props/materials.json 'scans') in
place of Poly Haven's metal_plate, which is a diamond tread plate:
  brushed_metal  (base 'brushed', the blue steel): fine streaks all one way, as a sanded and
                 lacquered steel panel shows under its paint;
  painted_metal  (base 'painted'): smooth enamel on sheet steel, a faint orange peel and the odd
                 rub mark, no pattern.
Poly Haven has neither, so they're drawn here and kept in git, in the same three maps a fetched
scan has (Tools/fetch_props.py):

    python Tools/props/make_metal_scans.py      -> Data/Props/Scans/<id>/{color,rough,normal}.jpg

The maps tile (the streaks are filtered in Fourier space, so they wrap), a metre across at the
props' Tile 1. M_PropMaster reads the colour's light and shade at x2 gain and the roughness at x2,
so both sit about 0.5 on average: the material's own Tint and Rough stay what they say.
"""
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SCANS = os.path.join(ROOT, "Data", "Props", "Scans")
N = 1024


def band(rng, along, across):
    """Tileable noise stretched along x: a long, thin Gaussian filter on white noise, in Fourier space."""
    f = np.fft.fft2(rng.standard_normal((N, N)))
    ky = np.fft.fftfreq(N)[:, None]
    kx = np.fft.fftfreq(N)[None, :]
    g = np.exp(-0.5 * ((kx * along) ** 2 + (ky * across) ** 2))
    out = np.real(np.fft.ifft2(f * g))
    return out / (out.std() + 1e-9)


def normal_map(dx, dy, k):
    """OpenGL normal (+y up the map) from a height's slopes, scaled by k."""
    nx, ny, nz = -dx * k, -dy * k, np.ones_like(dx)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.stack([nx / ln, ny / ln, nz / ln], axis=-1) * 0.5 + 0.5


def save(sid, colour, rough, nrm):
    out = os.path.join(SCANS, sid)
    os.makedirs(out, exist_ok=True)

    def one(a, name):
        img = np.clip(a * 255.0 + 0.5, 0, 255).astype(np.uint8)
        Image.fromarray(img).save(os.path.join(out, name), quality=92)

    grey = np.clip(colour, 0.0, 1.0)
    one(np.stack([grey] * 3, axis=-1), "color.jpg")
    one(np.clip(rough, 0.0, 1.0), "rough.jpg")
    one(nrm, "normal.jpg")
    print(sid, "maps in", out)


def slopes(h):
    return (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5, (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5


def brushed():
    rng = np.random.default_rng(7)
    # Streaks: the fine scratches (long and a pixel or two across), a few deeper ones, and a soft
    # cloud so a big panel isn't perfectly even.
    fine = band(rng, 160.0, 0.6)
    deep = band(rng, 400.0, 1.2)
    deep = np.where(deep > 1.8, deep - 1.8, 0.0)
    cloud = band(rng, 30.0, 30.0)
    colour = 0.5 + 0.05 * fine - 0.05 * deep + 0.01 * cloud
    rough = 0.5 + 0.07 * fine + 0.08 * deep + 0.03 * cloud
    # The slope across the streaks only; along them it's flat.
    _, dy = slopes(fine + 1.5 * deep)
    save("brushed_metal", colour, rough, normal_map(np.zeros_like(dy), dy, 0.04))


def painted():
    rng = np.random.default_rng(11)
    # Enamel: orange peel (small soft bumps, the same every way), a broad cloud where the coat's a
    # touch thicker or more worn, and a few faint rub marks where hands and boots go.
    peel = band(rng, 6.0, 6.0)
    cloud = band(rng, 60.0, 60.0)
    rub = band(rng, 120.0, 4.0)
    rub = np.where(rub > 2.0, rub - 2.0, 0.0)
    colour = 0.5 + 0.012 * cloud + 0.006 * peel + 0.03 * rub
    rough = 0.5 + 0.03 * cloud + 0.02 * peel - 0.06 * rub
    dx, dy = slopes(peel)
    save("painted_metal", colour, rough, normal_map(dx, dy, 0.08))


def main():
    brushed()
    painted()


if __name__ == "__main__":
    main()
