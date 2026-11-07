"""Road blemishes for Buttertubs' tarmac: repair patches, bitumen-sealed cracks, potholes, wheel-track
polish and oil drips, baked into two textures the road material samples by the road ribbon's UV
(u 0..1 across the 5 m carriageway, v = metres along / 20).

Data/Textures/RoadBlemish/mask.png   R patch tint (0.5 none, above lighter old tarmac, below fresh dark patch)
                                     G bitumen seal (cracks and patch seams)   B pothole   A wheel-track polish/oil
Data/Textures/RoadBlemish/normal.png tangent-space normal from the matching height (patch lips, pothole dips,
                                     seal ridges)
The texture covers 5 m x 204.8 m (256 x 8192 px, about 2 x 2.5 cm a pixel). Numpy only."""
import os, sys
import numpy as np
from PIL import Image

W, H = 256, 8192
WIDTH_M, LEN_M = 5.0, 204.8
rng = np.random.default_rng(int(sys.argv[1]) if len(sys.argv) > 1 else 7)
x = (np.arange(W) + 0.5) / W * WIDTH_M            # metres across, 0 at the left edge
y = (np.arange(H) + 0.5) / H * LEN_M              # metres along
X, Y = np.meshgrid(x, y)


def smooth_noise(cells_x, cells_y, seed):
    """Tileable value noise (bicubic-ish) with cells_x x cells_y lattice."""
    r = np.random.default_rng(seed).random((cells_y, cells_x))
    gx = X / WIDTH_M * cells_x; gy = Y / LEN_M * cells_y
    ix = np.floor(gx).astype(int); iy = np.floor(gy).astype(int)
    fx = gx - ix; fy = gy - iy
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy)
    a = r[iy % cells_y, ix % cells_x]; b = r[iy % cells_y, (ix + 1) % cells_x]
    c = r[(iy + 1) % cells_y, ix % cells_x]; d = r[(iy + 1) % cells_y, (ix + 1) % cells_x]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def fbm(base_x, base_y, seed, octaves=4):
    s = np.zeros_like(X); amp = 1.0; tot = 0.0
    for o in range(octaves):
        s += amp * smooth_noise(base_x * 2 ** o, base_y * 2 ** o, seed + o); tot += amp; amp *= 0.5
    return s / tot


def wrap_dy(cy):
    d = Y - cy
    return (d + LEN_M / 2) % LEN_M - LEN_M / 2


patch = np.full((H, W), 0.5, np.float32)
seal = np.zeros((H, W), np.float32)
pot = np.zeros((H, W), np.float32)
track = np.zeros((H, W), np.float32)
height = np.zeros((H, W), np.float32)    # millimetres

wob = fbm(4, 160, 11) - 0.5               # wobbles patch edges

# Repair patches: rough rectangles, often one lane, sometimes the full width; old (lighter) or new (dark).
for _ in range(16):
    cy = rng.uniform(0, LEN_M); ln = rng.uniform(1.2, 9.0)
    if rng.random() < 0.35:
        x0, x1 = rng.uniform(0.0, 0.3), rng.uniform(4.7, 5.0)
    else:
        lane = rng.integers(2)
        x0 = lane * 2.5 + rng.uniform(0.1, 0.9); x1 = min(5.0, x0 + rng.uniform(0.6, 2.2))
    dy = wrap_dy(cy)
    e = 0.06 * wob
    inside = (np.abs(dy) < ln / 2 + e) & (X > x0 + e) & (X < x1 + e)
    fresh = rng.random() < 0.5
    tone = rng.uniform(0.15, 0.32) if fresh else rng.uniform(0.62, 0.8)
    patch = np.where(inside, tone + 0.06 * (fbm(6, 300, 30 + _) - 0.5), patch)
    height += inside * (1.5 if fresh else -1.0)
    # sealed seam around the patch
    edge = np.minimum(np.minimum(np.abs(np.abs(dy) - ln / 2 - e), np.abs(X - x0 - e)), np.abs(X - x1 - e))
    ring = (edge < 0.05) & (np.abs(dy) < ln / 2 + 0.06) & (X > x0 - 0.06) & (X < x1 + 0.06)
    seal = np.maximum(seal, ring * rng.uniform(0.6, 1.0))

# Crack sealing: meandering longitudinal and transverse bitumen lines.
for _ in range(40):
    if rng.random() < 0.6:   # longitudinal, often near the centre joint or wheel paths
        cx = rng.choice([2.5, 1.0, 4.0, rng.uniform(0.3, 4.7)]); cy = rng.uniform(0, LEN_M); ln = rng.uniform(2, 14)
        dy = wrap_dy(cy)
        path = cx + 0.12 * np.sin(dy * rng.uniform(0.6, 1.8) + rng.uniform(0, 6)) + 0.05 * wob
        w = rng.uniform(0.012, 0.035)
        m = (np.abs(X - path) < w) & (np.abs(dy) < ln / 2)
    else:                    # transverse
        cy = rng.uniform(0, LEN_M); xa, xb = sorted(rng.uniform(0, 5, 2)); xb = max(xb, xa + 0.8)
        path = cy + 0.08 * np.sin(X * rng.uniform(1, 3)) + 0.04 * wob
        dy = (Y - path + LEN_M / 2) % LEN_M - LEN_M / 2
        w = rng.uniform(0.012, 0.03)
        m = (np.abs(dy) < w) & (X > xa) & (X < xb)
    seal = np.maximum(seal, m * rng.uniform(0.5, 1.0))
    height += m * 0.8

# Potholes: a few, mostly near the edges and in the wheel paths, ragged rims.
for _ in range(9):
    cx = rng.choice([rng.uniform(0.2, 0.7), rng.uniform(4.3, 4.8), rng.uniform(0.8, 4.2)])
    cy = rng.uniform(0, LEN_M); rx = rng.uniform(0.08, 0.3); ry = rx * rng.uniform(0.8, 1.6)
    d = np.sqrt(((X - cx) / rx) ** 2 + (wrap_dy(cy) / ry) ** 2) + 0.35 * wob
    hole = np.clip((1.0 - d) * 6, 0, 1)
    pot = np.maximum(pot, hole)
    height -= hole * rng.uniform(15, 40)
    patch = np.where((d > 0.9) & (d < 1.6), np.minimum(patch, 0.4), patch)   # broken dark ring of fretting

# Wheel-track polish (two paths per lane) and oil drips down the lane centres.
for c in (0.7, 1.8, 3.2, 4.3):
    track = np.maximum(track, np.exp(-((X - c) / 0.28) ** 2) * (0.35 + 0.3 * fbm(3, 120, 70)))
for c in (1.25, 3.75):
    drip = np.exp(-((X - c) / 0.35) ** 2) * np.clip((fbm(8, 900, 90) - 0.55) * 4, 0, 1)
    track = np.maximum(track, drip)

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Data", "Textures", "RoadBlemish")
os.makedirs(out, exist_ok=True)
rgba = np.stack([patch, np.clip(seal, 0, 1), pot, np.clip(track, 0, 1)], -1)
Image.fromarray((np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(os.path.join(out, "mask.png"))

# Normal from height (mm); pixel sizes differ across and along.
px, py = WIDTH_M / W * 1000, LEN_M / H * 1000
dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) / (2 * px)
dyy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) / (2 * py)
n = np.stack([-dx * 3, -dyy * 3, np.ones_like(dx)], -1)
n /= np.linalg.norm(n, axis=-1, keepdims=True)
Image.fromarray(((n * 0.5 + 0.5) * 255 + 0.5).astype(np.uint8), "RGB").save(os.path.join(out, "normal.png"))
print("road blemish", W, "x", H, "patches/seals/potholes written to", os.path.normpath(out))
