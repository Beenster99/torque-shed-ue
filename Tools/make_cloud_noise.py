"""The clouds' 3D noise (skies plan stage 2): Data/Sky/cloud_noise.png, made once with numpy.

A 64 x 64 x 64 block of noise that tiles every way, cut into 64 slices laid out 8 x 8 in one picture
(each slice with a one-texel border copied from its opposite edge, so sampling between texels never
bleeds into the next slice: 8 x 66 = 528 texels square). M_RideCloud (Tools/ue_build_level.py) reads it
as a pseudo-volume: two slices sampled and blended, the way Epic's PseudoVolumeTexture does.
  R  Perlin-Worley: billowy cloud shapes (Perlin made lumpy by Worley cells), the large forms
  G  Worley at twice the rate: the cauliflower detail that erodes the edges
  B  Worley at four times: finer still, for wisps
Run: python Tools/make_cloud_noise.py  (needs numpy and Pillow or imageio)
"""
import os

import numpy as np

N, TILES, PAD = 64, 8, 1
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "Sky", "cloud_noise.png")
rng = np.random.default_rng(20261113)
g = (np.arange(N) + 0.5) / N
X, Y, Z = np.meshgrid(g, g, g, indexing="ij")
P = np.stack([X, Y, Z], -1)   # 0..1, tiles at 1


def worley(cells):
    """1 - distance to the nearest of one random point per cell (cells a side), tiling."""
    pts = rng.random((cells, cells, cells, 3))
    c = np.floor(P * cells).astype(int)
    f = P * cells - c
    best = np.full(P.shape[:3], 9.0)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                o = np.array([dx, dy, dz])
                n = (c + o) % cells
                fp = pts[n[..., 0], n[..., 1], n[..., 2]] + o
                d = np.sqrt(((fp - f) ** 2).sum(-1))
                best = np.minimum(best, d)
    return 1.0 - np.clip(best / 0.9, 0, 1)


def perlin(cells):
    """Gradient noise, tiling, about -1..1."""
    grads = rng.normal(size=(cells, cells, cells, 3))
    grads /= np.linalg.norm(grads, axis=-1, keepdims=True)
    c = np.floor(P * cells).astype(int)
    f = P * cells - c
    fade = f * f * f * (f * (f * 6 - 15) + 10)
    total = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                o = np.array([dx, dy, dz])
                n = (c + o) % cells
                gr = grads[n[..., 0], n[..., 1], n[..., 2]]
                dot = (gr * (f - o)).sum(-1)
                w = (fade[..., 0] if dx else 1 - fade[..., 0]) * (fade[..., 1] if dy else 1 - fade[..., 1]) * (fade[..., 2] if dz else 1 - fade[..., 2])
                total = total + w * dot
    return total * 1.4


def fbm(fn, base, octaves, gain=0.5):
    v, a, s = 0.0, 1.0, 0.0
    for i in range(octaves):
        v = v + a * fn(base * 2 ** i)
        s += a
        a *= gain
    return v / s


def norm(v):
    lo, hi = np.percentile(v, 1), np.percentile(v, 99)
    return np.clip((v - lo) / (hi - lo), 0, 1)


def main():
    per = norm(fbm(perlin, 4, 4))
    wor = norm(fbm(worley, 4, 3))
    # Perlin-Worley (Schneider): the Perlin remapped so the Worley cells carve it into billows.
    r = norm(np.clip((per - (wor - 1.0)) / (1.0 - (wor - 1.0)), 0, 1) * 0.5 + per * 0.5 * wor)
    gch = norm(fbm(worley, 8, 3))
    bch = norm(fbm(worley, 16, 2))
    vol = np.stack([r, gch, bch], -1)   # x, y, z, channel
    side = TILES * (N + 2 * PAD)
    img = np.zeros((side, side, 3), np.float32)
    for z in range(N):
        tx, ty = z % TILES, z // TILES
        sl = vol[:, :, z, :].transpose(1, 0, 2)   # rows y, columns x
        padded = np.pad(sl, ((PAD, PAD), (PAD, PAD), (0, 0)), mode="wrap")
        y0, x0 = ty * (N + 2 * PAD), tx * (N + 2 * PAD)
        img[y0:y0 + N + 2 * PAD, x0:x0 + N + 2 * PAD] = padded
    data = (img * 255 + 0.5).astype(np.uint8)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    try:
        from PIL import Image
        Image.fromarray(data, "RGB").save(OUT, optimize=True)
    except ImportError:
        import imageio
        imageio.imwrite(OUT, data)
    print("wrote %s (%d x %d)" % (OUT, side, side))


if __name__ == "__main__":
    main()
