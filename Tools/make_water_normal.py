"""Ride UK's water ripples (build 154: beenster, "can we move to a proper water animation, as it does look
like a lot of static tiles for the surface"): Data/Textures/Water/normal.png, a tiling normal map of
wind-driven water made once here from a wave spectrum (an ocean-style FFT: many small waves of every
length from 2 m down to a few centimetres, mostly with the wind, a few across it), so it has no blobs
or seams. M_RideWater (Tools/ue_build_rideuk.py water_material) draws three samples of it at different
sizes, each drifting its own way, and the eye reads moving water rather than a pattern.

    python Tools/make_water_normal.py
"""
import os
import numpy as np
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "Textures", "Water")
SIZE = 512          # pixels
PATCH = 8.0         # metres the map covers (UV 1 = 8 m in M_RideWater)


def heights(seed=11, wind=(1.0, 0.35), speed=2.2):
    rng = np.random.default_rng(seed)
    k = np.fft.fftfreq(SIZE, d=PATCH / SIZE) * 2 * np.pi
    kx, ky = np.meshgrid(k, k)
    kk = np.hypot(kx, ky)
    kk[0, 0] = 1e-6
    w = np.array(wind) / np.hypot(*wind)
    Lw = speed * speed / 9.81
    cos = (kx * w[0] + ky * w[1]) / kk
    # Phillips spectrum, most of it with the wind, a little across, the very short waves damped
    P = np.exp(-1.0 / (kk * Lw) ** 2) / kk ** 4 * (0.08 + 0.92 * cos ** 4) * np.exp(-(kk * 0.05) ** 2)
    # ([real water, beenster on 170: "we never seem to get rid of the tiling on the water surface"] the
    # spectrum put most of the slope in waves as long as the map itself: 38% of it at one or two waves
    # across the 8 m, a few big diamonds that came round again every repeat, which is what the eye picks
    # out as tiles however the samples were turned and bent. Now nothing longer than a sixth of the map
    # (1.3 m) is in it, each repeat holds dozens of small waves and no big shape, and the long waves come
    # from the material's own slow noise, which doesn't repeat for 80-160 m.)
    P *= 1.0 - np.exp(-(kk / (2 * np.pi / (PATCH / 6.0))) ** 6)
    P[0, 0] = 0
    h = np.fft.ifft2((rng.normal(size=kk.shape) + 1j * rng.normal(size=kk.shape)) * np.sqrt(P)).real
    return h


def main():
    os.makedirs(OUT, exist_ok=True)
    h = heights()
    # slopes by spectral derivative would be exact; finite differences on a tiling grid are as good here
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) / (2 * PATCH / SIZE)
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) / (2 * PATCH / SIZE)
    s = 0.55 / np.percentile(np.hypot(dx, dy), 95)    # (the steepest twentieth about 29 degrees)
    n = np.stack([-dx * s, -dy * s, np.ones_like(h)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    img = ((n * 0.5 + 0.5) * 255).round().clip(0, 255).astype(np.uint8)
    Image.fromarray(img, "RGB").save(os.path.join(OUT, "normal.png"))
    print("wrote %s (%d px over %.0f m)" % (os.path.join(OUT, "normal.png"), SIZE, PATCH))


if __name__ == "__main__":
    main()
