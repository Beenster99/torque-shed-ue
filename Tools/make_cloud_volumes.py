"""The new clouds' two 3D noise textures (clouds rebuilt, option A, beenster Nov 16 2026), made once with numpy.

The way Horizon Zero Dawn (Schneider, "The real-time volumetric cloudscapes of Horizon", 2015) and
Hillaire ("Physically based sky, atmosphere and cloud rendering in Frostbite", 2016) build clouds:

  Data/Sky/cloud_shape.png   128 x 128 x 128, tiling every way: the clouds' forms
      R  the cloud's form: Perlin-Worley (billowy Perlin made lumpy by Worley cells) carved by a
         Worley fbm (4, 8 and 16 cells a side), then evened out so that a value v is exceeded by
         1 - v of the volume (the cover the weather asks for is the cover drawn)
      G  Worley fbm at 4 cells a side, B at 8, A at 16 (spare, for later looks)
  Data/Sky/cloud_detail.png  32 x 32 x 32, tiling: only for eroding the edges
      R  Worley fbm at 2, G at 4, B at 8 (summed in the material)

Each is cut into slices laid out in a sheet (shape 16 x 8 slices, detail 8 x 4), every slice with a
one-texel border copied from its opposite edge so sampling between texels never bleeds into the next
slice. The cloud material reads them as pseudo-volumes (two slices sampled and blended), as the
old cloud did with Data/Sky/cloud_noise.png (removed Nov 17).

Run: python Tools/make_cloud_volumes.py  (numpy and Pillow; about a minute)
"""
import os
import numpy as np

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(HERE, "Data", "Sky")
rng = np.random.default_rng(20261116)


def grid(n):
    g = (np.arange(n, dtype=np.float32) + 0.5) / n
    X, Y, Z = np.meshgrid(g, g, g, indexing="ij")
    return np.stack([X, Y, Z], -1)   # 0..1, tiles at 1


def worley(P, cells):
    """1 - distance to the nearest of one random point per cell (cells a side), tiling; about 0..1."""
    pts = rng.random((cells, cells, cells, 3)).astype(np.float32)
    c = np.floor(P * cells).astype(np.int32)
    f = (P * cells - c).astype(np.float32)
    best = np.full(P.shape[:3], 9.0, dtype=np.float32)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                o = np.array([dx, dy, dz], dtype=np.int32)
                n = (c + o) % cells
                fp = pts[n[..., 0], n[..., 1], n[..., 2]] + o
                d = np.sqrt(((fp - f) ** 2).sum(-1))
                np.minimum(best, d, out=best)
    return 1.0 - np.clip(best / 0.95, 0.0, 1.0)


def perlin(P, cells):
    """Gradient noise, tiling, about -1..1."""
    grads = rng.normal(size=(cells, cells, cells, 3)).astype(np.float32)
    grads /= np.linalg.norm(grads, axis=-1, keepdims=True)
    c = np.floor(P * cells).astype(np.int32)
    f = (P * cells - c).astype(np.float32)
    fade = f * f * f * (f * (f * 6 - 15) + 10)
    total = np.zeros(P.shape[:3], dtype=np.float32)
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                o = np.array([dx, dy, dz], dtype=np.int32)
                n = (c + o) % cells
                dot = (grads[n[..., 0], n[..., 1], n[..., 2]] * (f - o)).sum(-1)
                w = (fade[..., 0] if dx else 1 - fade[..., 0]) * (fade[..., 1] if dy else 1 - fade[..., 1]) * (fade[..., 2] if dz else 1 - fade[..., 2])
                total += w * dot
    return total * 1.4


def worley_fbm(P, cells):
    return worley(P, cells) * 0.625 + worley(P, cells * 2) * 0.25 + worley(P, cells * 4) * 0.125


def remap(v, lo, hi, to_lo, to_hi):
    return to_lo + (v - lo) / np.maximum(hi - lo, 1e-5) * (to_hi - to_lo)


def norm(v):
    lo, hi = np.percentile(v, 0.5), np.percentile(v, 99.5)
    return np.clip((v - lo) / max(hi - lo, 1e-6), 0.0, 1.0)


def sheet(vol, cols, rows):
    """A volume (n, n, n, ch) as slices along z in a cols x rows sheet, each with a wrapped border."""
    n = vol.shape[0]
    t = n + 2
    out = np.zeros((rows * t, cols * t, vol.shape[3]), dtype=np.uint8)
    for z in range(n):
        s = vol[:, :, z, :]                       # x, y
        s = np.pad(s, ((1, 1), (1, 1), (0, 0)), mode="wrap")
        r, c = divmod(z, cols)
        out[r * t:(r + 1) * t, c * t:(c + 1) * t, :] = np.transpose(s, (1, 0, 2))   # rows = y
    return out


def save(img, name):
    from PIL import Image
    path = os.path.join(OUT_DIR, name)
    Image.fromarray(img, "RGBA" if img.shape[2] == 4 else "RGB").save(path, optimize=True)
    print("wrote %s (%d x %d)" % (path, img.shape[1], img.shape[0]))
    return path


def make_shape(n=128):
    P = grid(n)
    pfbm = norm(perlin(P, 4) * 0.6 + perlin(P, 8) * 0.3 + perlin(P, 16) * 0.1)
    w4 = norm(worley_fbm(P, 4))
    # Perlin-Worley: the Perlin's low parts dug out by the Worley's cells, keeping its billows.
    pw = norm(np.clip(remap(pfbm, w4 - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0))
    g = w4
    b = norm(worley_fbm(P, 8))
    a = norm(worley_fbm(P, 16))
    lowf = g * 0.625 + b * 0.25 + a * 0.125
    base = np.clip(remap(pw, lowf - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0)
    # (evened out: each value's rank, so a threshold of 1 - cover leaves that cover)
    order = np.argsort(base, axis=None, kind="stable")
    even = np.empty(base.size, dtype=np.float32)
    even[order] = (np.arange(base.size, dtype=np.float32) + 0.5) / base.size
    base = even.reshape(base.shape)
    vol = (np.stack([base, g, b, a], -1) * 255.0 + 0.5).astype(np.uint8)
    return vol


def make_detail(n=32):
    P = grid(n)
    r = norm(worley_fbm(P, 2))
    g = norm(worley_fbm(P, 4))
    b = norm(worley_fbm(P, 8))
    return (np.stack([r, g, b, np.ones_like(r)], -1) * 255.0 + 0.5).astype(np.uint8)


def main():
    shape = make_shape()
    detail = make_detail()
    save(sheet(shape, 16, 8), "cloud_shape.png")
    save(sheet(detail, 8, 4), "cloud_detail.png")


if __name__ == "__main__":
    main()
