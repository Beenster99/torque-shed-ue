"""The street kit's one shared material (Chat with Fable, approved by beenster Nov 18: "please build them
yourself in blender and use suitable materials to improve their look"): the lamp posts and telegraph
poles that Tools/blender_street_kit.py builds all draw with M_RideStreetKit (Tools/ue_build_rideuk.py
street_kit_material), which tells its surfaces apart by the vertex alpha: the surface's number in SURFACES
(0..7) and the dirt on it, in bytes 32 * number + 2 + 28 * dirt (the material reads the number as
floor(alpha * 255 / 32) and the dirt from what is left).

This writes the three small textures that material reads, to Data/Textures/StreetKit:

  detail.png  512 px tiling over 1 m: R the timber's grain and checks (streaks up the pole), G the
              galvanising's spangle (cells of brighter and duller zinc), B concrete's aggregate and pits,
              A broad weathering (streaks and blotches).
  normal.png  512 px tiling over 1 m: two normal maps in one, RG the timber's checks and grain, BA the
              concrete's pits (each x, y in 0..1; z made up in the material).
  lut.png     8 x 2 px, a column per surface: row 0 roughness, metallic, specular, grain amount;
              row 1 spangle amount, pits amount, weathering amount, normal strength.

    python Tools/make_street_kit_textures.py
"""
import os

import numpy as np
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "Textures", "StreetKit")
SIZE = 512

# Each surface: (name, roughness, metallic, specular, grain, spangle, pits, weathering, normal strength).
SURFACES = [
    ("paint",     0.55, 0.0, 0.5, 0.0, 0.0, 0.15, 0.35, 0.2),   # 0 painted metal, plastic (signs, boxes)
    ("timber",    0.78, 0.0, 0.35, 1.0, 0.0, 0.0, 0.6, 1.0),    # 1 creosoted softwood: poles, cross arms
    ("galv",      0.42, 1.0, 0.5, 0.0, 1.0, 0.0, 0.45, 0.15),   # 2 galvanised steel: columns, brackets, steps
    ("concrete",  0.9, 0.0, 0.45, 0.0, 0.0, 1.0, 0.7, 0.8),     # 3 concrete: the old lamp columns
    ("glaze",     0.12, 0.0, 0.6, 0.0, 0.0, 0.0, 0.2, 0.0),     # 4 glazed porcelain: insulators
    ("lens",      0.06, 0.0, 0.6, 0.0, 0.0, 0.0, 0.15, 0.0),    # 5 glass and acrylic: lantern lenses, bowls
    ("enamel",    0.32, 0.0, 0.5, 0.0, 0.0, 0.0, 0.3, 0.0),     # 6 powder-coated or enamelled lantern bodies
    ("steel",     0.6, 0.85, 0.5, 0.0, 0.35, 0.1, 0.8, 0.25),   # 7 bare and weathered steel: stay wire, bolts
]
SURFACE = {s[0]: i for i, s in enumerate(SURFACES)}


def alpha_of(surface, dirt):
    """The vertex alpha (0..1, a whole byte) for a surface by name and its dirt (0..1)."""
    return round(SURFACE[surface] * 32 + 2 + 28 * float(np.clip(dirt, 0.0, 1.0))) / 255.0


def _noise(rng, shape, falloff, stretch=(1.0, 1.0)):
    """Tiling noise: random phases under a power-law spectrum, stretched along v and u by `stretch`."""
    h, w = shape
    ky = np.fft.fftfreq(h)[:, None] * h * stretch[0]
    kx = np.fft.fftfreq(w)[None, :] * w * stretch[1]
    k = np.hypot(kx, ky)
    k[0, 0] = 1.0
    amp = 1.0 / k ** falloff
    amp[0, 0] = 0.0
    f = (rng.normal(size=shape) + 1j * rng.normal(size=shape)) * amp
    n = np.fft.ifft2(f).real
    n -= n.min()
    return n / max(n.max(), 1e-9)


def _cells(rng, n, size):
    """Tiling Voronoi cells: each pixel's cell value (0..1) and distance to its nearest edge (pixels)."""
    pts = rng.random((n, 2)) * size
    val = rng.random(n)
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    best = np.full((size, size), 1e9, np.float32)
    second = np.full((size, size), 1e9, np.float32)
    cell = np.zeros((size, size), np.float32)
    for (px, py), v in zip(pts, val):
        dx = np.abs(xx - px); dx = np.minimum(dx, size - dx)
        dy = np.abs(yy - py); dy = np.minimum(dy, size - dy)
        d = np.hypot(dx, dy)
        closer = d < best
        second = np.where(closer, best, np.minimum(second, d))
        cell = np.where(closer, v, cell)
        best = np.where(closer, d, best)
    return cell, (second - best) * 0.5


def _normal_xy(height, strength):
    """x, y (0..1) of the normal of a tiling height field (pixels), `strength` its slope scale."""
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 0.5 * strength
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 0.5 * strength
    n = np.stack([-dx, dy, np.ones_like(dx)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n[..., 0] * 0.5 + 0.5, n[..., 1] * 0.5 + 0.5


def textures(seed=7):
    rng = np.random.default_rng(seed)
    S = (SIZE, SIZE)
    # Timber: long streaks up the pole (v is up), a few dark checks (drying cracks) along them.
    grain = 0.65 * _noise(rng, S, 1.6, (1.0, 0.06)) + 0.35 * _noise(rng, S, 1.2, (1.0, 0.15))
    checks = np.zeros(S, np.float32)
    for _ in range(9):
        x = rng.integers(0, SIZE); y = rng.integers(0, SIZE); length = rng.integers(60, 260)
        wob = np.cumsum(rng.normal(0, 0.35, length))
        for i in range(length):
            xi = int(x + wob[i]) % SIZE
            w = 1.0 - abs(i / length - 0.5) * 2.0
            checks[(y + i) % SIZE, xi] = max(checks[(y + i) % SIZE, xi], w)
            checks[(y + i) % SIZE, (xi + 1) % SIZE] = max(checks[(y + i) % SIZE, (xi + 1) % SIZE], w * 0.5)
    r = np.clip(grain * 0.85 + 0.15 - checks * 0.6, 0, 1)
    # Galvanising: spangle cells, each its own shade, a little mottle over them.
    cell, _ = _cells(rng, 360, SIZE)
    g = np.clip(0.35 + cell * 0.45 + (_noise(rng, S, 1.4) - 0.5) * 0.3, 0, 1)
    # Concrete: aggregate speckle and small pits.
    speck = _noise(rng, S, 0.6)
    pits = (rng.random(S) < 0.004).astype(np.float32)
    pit_field = np.zeros(S, np.float32)
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            pit_field = np.maximum(pit_field, np.roll(np.roll(pits, oy, 0), ox, 1) * (1.0 if ox == oy == 0 else 0.5))
    b = np.clip(0.25 + speck * 0.6 - pit_field * 0.45, 0, 1)
    # Weathering: broad blotches and rain streaks down.
    a = np.clip(0.55 * _noise(rng, S, 1.8) + 0.45 * _noise(rng, S, 1.5, (1.0, 0.2)), 0, 1)
    detail = np.stack([r, g, b, a], -1)
    # Normals: timber's checks cut in and grain ridges; concrete's pits.
    th = grain * 6.0 - checks * 10.0
    tx, ty = _normal_xy(th, 1.0)
    ch = speck * 3.0 - pit_field * 6.0
    cx, cy = _normal_xy(ch, 1.0)
    normal = np.stack([tx, ty, cx, cy], -1)
    lut = np.zeros((2, 8, 4))
    for i, s in enumerate(SURFACES):
        lut[0, i] = s[1:5]
        lut[1, i] = s[5:9]
    return detail, normal, lut


def main():
    os.makedirs(OUT, exist_ok=True)
    detail, normal, lut = textures()
    to8 = lambda x: (np.clip(x, 0, 1) * 255).round().astype(np.uint8)
    Image.fromarray(to8(detail), "RGBA").save(os.path.join(OUT, "detail.png"))
    Image.fromarray(to8(normal), "RGBA").save(os.path.join(OUT, "normal.png"))
    Image.fromarray(to8(lut), "RGBA").save(os.path.join(OUT, "lut.png"))
    print("street kit textures in", OUT)


if __name__ == "__main__":
    main()
