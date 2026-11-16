"""Draws a TFT unit from SimTest/dash_dump's triangles, as a simple shaded picture from the rider's side
(no engine or Blender needed): python Tools/dash_pictures.py <out.png> <unit.txt>[=<screen.png>] ...
A screen picture (from Tools/tft_sheets.js or any PNG) is laid on the unit's lit face."""
import sys
import numpy as np
from PIL import Image

COL = {"plastic": (0.055, 0.057, 0.062), "black_metal": (0.035, 0.035, 0.04), "glass": (0.012, 0.013, 0.016), "rubber": (0.02, 0.02, 0.02),
       "polished_alloy": (0.75, 0.76, 0.78), "lens_white": (0.2, 0.75, 0.3), "lens_amber": (0.9, 0.65, 0.1), "lens_red": (0.85, 0.12, 0.08)}


def render(path, shot, size=560):
    lines = open(path).read().strip().split("\n")
    eye = np.array([float(v) for v in lines[0].split()[1:4]])
    tris = []
    for ln in lines[1:]:
        f = ln.split()
        tris.append((f[0], np.array([float(v) for v in f[1:10]]).reshape(3, 3), np.array([float(v) for v in f[10:19]]).reshape(3, 3)))
    pts = np.concatenate([t[1] for t in tris])
    mid = (pts.min(0) + pts.max(0)) * 0.5
    reach = np.linalg.norm(pts.max(0) - pts.min(0)) * 0.5
    screen = [t for t in tris if t[0].endswith(":screen")]
    spts = np.concatenate([t[1] for t in screen])
    # The camera: near the rider's eye line, a little to one side, pulled in close.
    fwd = mid - eye
    fwd /= np.linalg.norm(fwd)
    side = np.cross(fwd, [0, 1, 0]); side /= np.linalg.norm(side)
    up = np.cross(side, fwd)
    cam = mid - fwd * reach * 3.4 + side * reach * 1.1 + up * reach * 0.5
    fwd = mid - cam; fwd /= np.linalg.norm(fwd)
    side = np.cross(fwd, [0, 1, 0]); side /= np.linalg.norm(side)
    up = np.cross(side, fwd)
    focal = size * 1.35
    def proj(p):
        d = p - cam
        z = d @ fwd
        return np.array([size / 2 + focal * (d @ side) / z, size / 2 - focal * (d @ up) / z, z])
    # The screen's own axes, for its picture.
    sc = spts.mean(0)
    sn = np.cross(screen[0][1][1] - screen[0][1][0], screen[0][1][2] - screen[0][1][0]); sn /= np.linalg.norm(sn)
    sx = np.array([0.0, 0.0, 1.0])
    sy = np.cross(sx, sn); sy /= np.linalg.norm(sy)
    if sy[1] < 0: sy = -sy
    half_w = np.abs((spts - sc) @ sx).max(); half_h = np.abs((spts - sc) @ sy).max()
    tex = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32) / 255.0 if shot else None
    img = np.zeros((size, size, 3), np.float32) + np.array([0.11, 0.115, 0.13], np.float32)
    zb = np.full((size, size), 1e9, np.float32)
    light = np.array([-0.35, 0.8, 0.3]); light /= np.linalg.norm(light)
    for tag, p, n in tris:
        q = np.array([proj(v) for v in p])
        x0, x1 = int(max(0, np.floor(q[:, 0].min()))), int(min(size - 1, np.ceil(q[:, 0].max())))
        y0, y1 = int(max(0, np.floor(q[:, 1].min()))), int(min(size - 1, np.ceil(q[:, 1].max())))
        if x1 < x0 or y1 < y0: continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        det = (q[1, 1] - q[2, 1]) * (q[0, 0] - q[2, 0]) + (q[2, 0] - q[1, 0]) * (q[0, 1] - q[2, 1])
        if abs(det) < 1e-9: continue
        a = ((q[1, 1] - q[2, 1]) * (xs - q[2, 0]) + (q[2, 0] - q[1, 0]) * (ys - q[2, 1])) / det
        b = ((q[2, 1] - q[0, 1]) * (xs - q[2, 0]) + (q[0, 0] - q[2, 0]) * (ys - q[2, 1])) / det
        c = 1 - a - b
        inside = (a >= 0) & (b >= 0) & (c >= 0)
        z = a * q[0, 2] + b * q[1, 2] + c * q[2, 2]
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        hit = inside & (z < sub)
        if not hit.any(): continue
        nrm = a[..., None] * n[0] + b[..., None] * n[1] + c[..., None] * n[2]
        nrm /= np.maximum(np.linalg.norm(nrm, axis=-1, keepdims=True), 1e-6)
        pos = a[..., None] * p[0] + b[..., None] * p[1] + c[..., None] * p[2]
        base = np.array(COL.get(tag.split(":")[0].split("__")[0], (0.3, 0.3, 0.3)))
        lam = np.clip(nrm @ light, 0, 1)[..., None]
        view = cam - pos; view /= np.linalg.norm(view, axis=-1, keepdims=True)
        half = view + light; half /= np.linalg.norm(half, axis=-1, keepdims=True)
        spec = np.clip((nrm * half).sum(-1), 0, 1)[..., None] ** 40
        col = base * (0.35 + 0.9 * lam) + spec * (0.5 if "glass" in tag or "alloy" in tag else 0.12)
        if tag.endswith(":screen") and tex is not None:
            u = ((pos - sc) @ sx) / (2 * half_w) + 0.5
            v = 0.5 - ((pos - sc) @ sy) / (2 * half_h)
            tx = np.clip((u * tex.shape[1]).astype(int), 0, tex.shape[1] - 1)
            ty = np.clip((v * tex.shape[0]).astype(int), 0, tex.shape[0] - 1)
            col = tex[ty, tx] * 1.0 + spec * 0.15
        elif tag.startswith("lens"):
            col = np.broadcast_to(base, pos.shape)
        tgt = img[y0:y1 + 1, x0:x1 + 1]
        tgt[hit] = np.clip(col, 0, 1)[hit]
        sub[hit] = z[hit]
    return Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))


def main():
    out, items = sys.argv[1], sys.argv[2:]
    shots = [render(*(it.split("=") if "=" in it else (it, None))) for it in items]
    sheet = Image.new("RGB", (sum(s.width for s in shots) + 12 * (len(shots) + 1), shots[0].height + 24), (27, 29, 33))
    x = 12
    for s in shots:
        sheet.paste(s, (x, 12)); x += s.width + 12
    sheet.save(out)


if __name__ == "__main__":
    main()
