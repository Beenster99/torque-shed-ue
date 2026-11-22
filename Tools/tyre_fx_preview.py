"""Pictures of the tyre smoke and the darkies (Weather/TorqueTyreFx.cpp) in the cloud, with numpy (no Unreal).

It runs the same emission, drift, growth and fading as ATorqueTyreFx (EmitSmoke, SimulatePuffs,
DrawPuffs: mirrored below, Effects quality 4 so 300 puffs) and draws each puff with M_TyreSmoke's shape
(SMOKE_HLSL in Tools/ue_build_spray.py), back to front, over a plain road and sky; the light is flat
(no sun or shadow), so it shows how much smoke there is and where it hangs, not the game's lighting.

    python Tools/tyre_fx_preview.py <out.png>

Three views of one burnout (rear tyre spinning at 20 m/s on the spot for 6 s, a light crosswind):
from the side 9 m off; from the chase camera 4 m behind (inside the bank, with the veil); and from
above after the burnout, a locked-wheel skid and a wheelspin pull-away, with the darkies laid.
"""
import math
import sys

import numpy as np
from PIL import Image, ImageDraw

RNG = np.random.default_rng(7)
DT = 1.0 / 60.0
CAP = 300
SMOKE = np.array([0.80, 0.80, 0.82])


def frand(a, b):
    return a + (b - a) * RNG.random()


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


class Puff:
    __slots__ = ("pos", "vel", "age", "life", "s0", "s1", "alpha", "seed", "drag")


def burnout(seconds, wind, radius=0.30):
    """The smoke of a burnout, in metres (x ahead of the bike, y left, z up), rear tyre's patch at 0.

    As EmitSmoke after build 160: a ball carried round the back of the wheel with the tread, and a fan
    flung off the back of it that slows and swells into the bank behind."""
    fwd, up, side = np.array([1.0, 0, 0]), np.array([0, 0, 1.0]), np.array([0, 1.0, 0])
    axle = up * radius
    puffs, heat, debt = [], 0.0, 0.0
    slide, road, half = 20.0, 0.0, 0.08
    tread = slide - road
    spin_for = 0.0
    for _ in range(int(seconds / DT)):
        spin_for += DT
        ring = 1.0 - float(smoothstep(1.0, 3.0, spin_for)) * 0.75
        heat = min(heat + slide / 6.0 * DT, 1.0)
        hot = float(smoothstep(0.03, 0.5, heat))
        debt = min(debt + 45.0 * hot * min(slide / 12.0, 1.6) * 1.6 * DT, 6.0)
        while debt >= 1.0:
            debt -= 1.0
            p = Puff()
            p.seed = RNG.random()
            across = frand(-1, 1) * half
            pick = RNG.random()
            if pick < ring and (spin_for < 1.0 or pick < ring * 0.5):
                # [build 168] the ring round the tread a burnout starts with
                th = math.radians(frand(0.0, 360.0))
                out = radius + frand(0.0, 0.06)
                radial = -up * math.cos(th) - fwd * math.sin(th)
                p.pos = axle + radial * out + side * across * 0.7
                rnd = -fwd * math.cos(th) + up * math.sin(th)
                p.vel = rnd * tread * frand(0.02, 0.05) + radial * frand(0.05, 0.2) + side * frand(-0.15, 0.15)
                p.life, p.s0, p.s1 = frand(0.5, 1.0), frand(0.08, 0.14), frand(0.25, 0.45)
                p.alpha, p.drag = frand(0.45, 0.65), 5.0
            elif RNG.random() < 0.45:
                th = math.radians(frand(0.0, 200.0))
                out = radius + frand(0.02, 0.14)
                p.pos = axle - up * out * math.cos(th) - fwd * out * math.sin(th) + side * across * 1.4
                rnd = -fwd * math.cos(th) + up * math.sin(th)
                p.vel = rnd * tread * frand(0.08, 0.16) + side * frand(-0.4, 0.4)
                p.life, p.s0, p.s1 = frand(1.2, 2.5), frand(0.25, 0.40), frand(1.2, 2.2)
                p.alpha, p.drag = frand(0.40, 0.60) * (0.6 + 0.4 * hot), 3.5
            else:
                th = math.radians(frand(10.0, 70.0))
                p.pos = axle - up * radius * math.cos(th) - fwd * radius * math.sin(th) + side * across
                fl = min(tread, 30.0) * frand(0.14, 0.32)
                p.vel = -fwd * fl + up * fl * frand(0.0, 0.22) + side * fl * frand(-0.38, 0.38) + wind * 0.3
                p.life, p.s0, p.s1 = frand(9.0, 15.0), frand(0.30, 0.50), frand(4.0, 8.0)
                p.alpha, p.drag = frand(0.30, 0.50) * (0.6 + 0.4 * hot), 1.4
            p.age = 0.0
            if len(puffs) < CAP:
                puffs.append(p)
            else:
                old = max(range(len(puffs)), key=lambda k: puffs[k].age / puffs[k].life)
                puffs[old] = p
        drift = wind + np.array([0, 0, 0.15])
        keep = []
        for p in puffs:
            p.age += DT
            if p.age >= p.life:
                continue
            p.vel = p.vel + (drift - p.vel) * (1.0 - math.exp(-p.drag * DT))
            p.pos = p.pos + p.vel * DT
            keep.append(p)
        puffs = keep
    return puffs


def puff_look(p):
    f = p.age / p.life
    size = p.s0 + (p.s1 - p.s0) * (1.0 - (1.0 - f) ** 3)
    a = p.alpha * min(p.age / min(0.3, 0.15 * p.life), 1.0) * (1.0 - float(smoothstep(0.55, 1.0, f))) * (1.0 + (0.55 - 1.0) * f)
    return size, a


def shape(u, v, seed):
    """SMOKE_HLSL (Soft 1)."""
    qx, qy = u * 2 - 1, v * 2 - 1
    d = np.sqrt(qx * qx + qy * qy)
    ang = np.arctan2(qy, qx)
    lump = 0.10 * np.sin(ang * 3 + seed * 31) + 0.07 * np.sin(ang * 5 + seed * 17) + 0.04 * np.sin(ang * 9 + seed * 5)
    r = d * (1 - lump)
    puff = np.clip(1 - r, 0, 1) ** 1.35
    billow = 0.72 + 0.28 * np.sin(qx * 4.5 + seed * 13) * np.sin(qy * 3.7 + seed * 7)
    return puff * billow


def scene(w, h, eye, target, fov, puffs, label):
    fwd = target - eye
    fwd /= np.linalg.norm(fwd)
    right = np.cross(fwd, [0, 0, 1.0])
    right /= np.linalg.norm(right)
    upv = np.cross(right, fwd)
    f = 0.5 * w / math.tan(math.radians(fov) / 2)
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    dx = (xs - w / 2) / f
    dy = -(ys - h / 2) / f
    dirs = fwd[None, None, :] + dx[..., None] * right[None, None, :] + dy[..., None] * upv[None, None, :]
    dirs /= np.linalg.norm(dirs, axis=2, keepdims=True)
    # Sky and road (a grey tarmac plane at z 0, a white edge line at y = 3.5 m).
    img = np.zeros((h, w, 3), np.float32)
    sky = 0.55 + 0.35 * np.clip(dirs[..., 2], 0, 1)[..., None]
    img[:] = sky * np.array([0.62, 0.74, 0.92])
    down = dirs[..., 2] < -1e-4
    t = np.where(down, -eye[2] / np.where(down, dirs[..., 2], -1), 0)
    gx, gy = eye[0] + dirs[..., 0] * t, eye[1] + dirs[..., 1] * t
    road = down & (np.abs(gy) < 3.6)
    img[down] = np.array([0.33, 0.45, 0.22])
    img[road] = np.array([0.22, 0.22, 0.23])
    img[road & (np.abs(np.abs(gy) - 3.45) < 0.06)] = np.array([0.85, 0.85, 0.82])
    dist_far = np.where(down, t, 1e9)
    # The bike: a dark box over the patch (x -0.3 .. 1.2, |y| < 0.35, z 0 .. 1.1), for scale.
    # (Drawn by projecting its corners and filling their screen box: enough to see where it is.)
    close = np.linalg.norm(eye[:2] - np.array([-0.3, 0.0])) < 4.0
    corners = np.array([[x, y, z] for x in (-0.3, 1.2) for y in (-0.35, 0.35) for z in (0.0, 1.1)])
    rel = corners - eye
    zc = rel @ fwd
    if np.all(zc > 0.1):
        sx = w / 2 + f * (rel @ right) / zc
        sy = h / 2 - f * (rel @ upv) / zc
        x0, x1, y0, y1 = int(max(sx.min(), 0)), int(min(sx.max(), w)), int(max(sy.min(), 0)), int(min(sy.max(), h))
        img[y0:y1, x0:x1] = np.array([0.08, 0.08, 0.09]) if not close else img[y0:y1, x0:x1]
        if not close:
            dist_far[y0:y1, x0:x1] = np.minimum(dist_far[y0:y1, x0:x1], np.linalg.norm(rel.mean(axis=0)))
    if close:
        # (the back tyre's outline, 30 cm round its axle, for scale)
        for k in range(180):
            th = k * 2 * math.pi / 180
            q = np.array([-0.0 - 0.30 * math.sin(th), 0.0, 0.30 - 0.30 * math.cos(th)]) - eye
            zq = q @ fwd
            if zq > 0.1:
                sx, sy = int(w / 2 + f * (q @ right) / zq), int(h / 2 - f * (q @ upv) / zq)
                if 0 <= sx < w - 2 and 0 <= sy < h - 2:
                    img[sy:sy + 3, sx:sx + 3] = np.array([0.05, 0.05, 0.05])
    # The puffs, back to front, faded out where the camera is inside them; their thickness there is the veil.
    order = sorted(puffs, key=lambda p: -np.sum((p.pos - eye) ** 2))
    thick = 0.0
    for p in order:
        size, a = puff_look(p)
        rel = p.pos - eye
        d = float(np.linalg.norm(rel))
        rad = size * 0.5
        if d < rad:
            k = 1 - (d / rad) ** 2
            thick += a * k * k
        a *= float(smoothstep(rad * 0.35, rad, d))
        z = float(rel @ fwd)
        if a < 0.002 or z < 0.2:
            continue
        cx, cy = w / 2 + f * float(rel @ right) / z, h / 2 - f * float(rel @ upv) / z
        pr = f * rad / z
        x0, x1 = int(max(cx - pr, 0)), int(min(cx + pr, w))
        y0, y1 = int(max(cy - pr, 0)), int(min(cy + pr, h))
        if x1 <= x0 or y1 <= y0:
            continue
        u = (xs[y0:y1, x0:x1] - (cx - pr)) / (2 * pr)
        v = (ys[y0:y1, x0:x1] - (cy - pr)) / (2 * pr)
        op = np.clip(shape(u, v, p.seed) * a, 0, 1)
        # (depth fade: 60 cm where it meets the road or the bike)
        op *= np.clip((dist_far[y0:y1, x0:x1] - z) / 0.6, 0, 1)
        img[y0:y1, x0:x1] = img[y0:y1, x0:x1] * (1 - op[..., None]) + SMOKE * op[..., None]
    veil = min(1 - math.exp(-1.6 * thick), 0.93)
    img = img * (1 - veil) + SMOKE * veil
    out = Image.fromarray((np.clip(img, 0, 1) ** (1 / 1.4) * 255).astype(np.uint8))
    ImageDraw.Draw(out).text((10, 8), "%s   (%d puffs, veil %.2f)" % (label, len(puffs), veil), fill=(255, 255, 255))
    return out


def darkies(w, h):
    """From above: a burnout's patch, a locked rear wheel's skid and a wheelspin pull-away (LayMark's sums)."""
    img = Image.new("RGB", (w, h), (56, 56, 59))
    px = w / 24.0   # 24 m across
    dr = ImageDraw.Draw(img, "RGBA")
    dr.line([(0, h * 0.12), (w, h * 0.12)], fill=(215, 215, 205), width=3)
    dr.line([(0, h * 0.88), (w, h * 0.88)], fill=(215, 215, 205), width=3)

    def strip(x0, x1, y, opacity, half=0.08):
        a = int(255 * opacity)
        dr.rectangle([x0 * px, (y - half) * px, x1 * px, (y + half) * px], fill=(4, 4, 4, a))

    # Burnout: 6 s spinning at 20 m/s on the spot: laid 120 m of tread on a 0.25 m patch: full dark (0.9).
    strip(2.0, 2.6, 3.0, 0.9)
    # Locked rear wheel from 15 m/s stopping at 7 m/s/s: about 16 m of 0.55 (laid = slid).
    strip(5.0, 21.0, 5.0, 0.55)
    # Wheelspin pulling away: slide falling from 8 m/s to 1.2 m/s over 6 m; segments of 1.2 m.
    x, v, slide = 4.0, 1.0, 8.0
    while slide > 1.2 and x < 22:
        seg = 1.2
        laid = slide * seg / max(v, 0.5)
        strip(x, x + seg + 0.05, 7.0, min(0.6 * laid / seg, 0.9))
        x += seg
        v += 2.5
        slide *= 0.7
    # A donut: the back tyre round the front on a 1.45 m circle, a piece every 10 deg (was 40: an octagon).
    cx, cy, rr = 17.0, 2.6, 1.45
    for k in range(36):
        a0, a1 = math.radians(k * 10.0), math.radians(k * 10.0 + 10.0)
        p0 = (cx + rr * math.cos(a0), cy + 0.9 + rr * math.sin(a0) * 0.6)
        p1 = (cx + rr * math.cos(a1), cy + 0.9 + rr * math.sin(a1) * 0.6)
        dr.line([(p0[0] * px, p0[1] * px), (p1[0] * px, p1[1] * px)], fill=(4, 4, 4, 200), width=int(0.16 * px))
    dr.text((10, 8), "darkies from above (24 m): burnout patch / locked-rear skid from 34 mph / wheelspin pulling away / donut",
            fill=(255, 255, 255))
    return img


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "tyre_fx.png"
    wind = np.array([0.0, -1.2, 0.0])   # a light breeze from the bike's left (m/s, near the ground)
    puffs = burnout(6.0, wind)
    w, h = 640, 360
    side = scene(w, h, np.array([-2.0, 9.0, 1.4]), np.array([-2.0, 0.0, 1.0]), 70, puffs, "6 s burnout, from the side 9 m off")
    chase = scene(w, h, np.array([-9.0, 0.6, 1.6]), np.array([0.0, 0.0, 0.9]), 75, puffs, "a mate standing 9 m behind")
    behind = scene(w, h, np.array([-4.0, 0.0, 1.6]), np.array([2.0, 0.0, 0.9]), 75, puffs, "from the chase camera, 4 m behind")
    sheet = Image.new("RGB", (w * 2, h * 2), (0, 0, 0))
    sheet.paste(side, (0, 0))
    sheet.paste(behind, (w, 0))
    early = burnout(0.8, wind)
    ring = scene(w, h, np.array([0.0, 2.2, 0.4]), np.array([0.0, 0.0, 0.3]), 55, early, "0.8 s into the burnout, close by the back wheel")
    sheet.paste(ring, (0, h))
    sheet.paste(darkies(w, h), (w, h))
    sheet.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main()
