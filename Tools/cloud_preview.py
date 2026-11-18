"""A test renderer for the rebuilt clouds (option A, Nov 16 2026): pictures of the new cloud material's
shapes before they go into the game, in the cloud with numpy (no Unreal).

It runs the SAME density function as the game's cloud material (CLOUD_V2_HLSL in Tools/ue_build_level.py,
mirrored line for line in density() below) on the same noise textures (Data/Sky/cloud_shape.png and
cloud_detail.png, from Tools/make_cloud_volumes.py), with the sky's own figures for a preset sky
(from SimTest's sky dump). The light is a simple stand-in for Unreal's (sun with two-lobe phase and
three octaves of bounced light, a sky ambient, a plain sky behind): it shows the clouds' forms and
how they sit in the sky, not the game's exact colours.

    python Tools/cloud_preview.py <scene.json> <out.png> [--width 960] [--steps 96]
scene.json: {"sun": deg, "sun_bearing": deg, "look_bearing": deg, "pitch": deg, "fov": deg,
             "base": m, "top": m, "low": {...}, "mid": {...}, "dull": 0..1, "looks": {v2 figures}}
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKY = os.path.join(HERE, "Data", "Sky")


def load_volume(name, n, cols, rows, chans):
    img = np.asarray(Image.open(os.path.join(SKY, name)), dtype=np.float32) / 255.0
    t = n + 2
    vol = np.zeros((n, n, n, chans), dtype=np.float32)
    for z in range(n):
        r, c = divmod(z, cols)
        s = img[r * t + 1:r * t + 1 + n, c * t + 1:c * t + 1 + n, :chans]   # rows y, cols x
        vol[:, :, z, :] = np.transpose(s, (1, 0, 2))
    return vol


def trilinear(vol, w):
    """vol (n,n,n,c) sampled at w (...,3) in 0..1 repeats, wrapping, as the pseudo-volume does."""
    n = vol.shape[0]
    p = np.mod(w, 1.0) * n - 0.5
    i0 = np.floor(p).astype(np.int32)
    f = p - i0
    i0 %= n
    i1 = (i0 + 1) % n
    out = 0.0
    for dx, ix in ((0, i0[..., 0]), (1, i1[..., 0])):
        wx = f[..., 0] if dx else 1 - f[..., 0]
        for dy, iy in ((0, i0[..., 1]), (1, i1[..., 1])):
            wy = f[..., 1] if dy else 1 - f[..., 1]
            for dz, iz in ((0, i0[..., 2]), (1, i1[..., 2])):
                wz = f[..., 2] if dz else 1 - f[..., 2]
                out = out + vol[ix, iy, iz] * (wx * wy * wz)[..., None]
    return out


def sat(x):
    return np.clip(x, 0.0, 1.0)


def remap(v, lo, hi, a, b):
    return a + (v - lo) / np.maximum(hi - lo, 1e-4) * (b - a)


def smooth(e0, e1, x):
    t = sat((x - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)


def looks_from_file():
    """The figures as the game has them: Data/Sky/sky_looks.json's clouds_v2 group and the cloud's scattering."""
    d = json.load(open(os.path.join(SKY, "sky_looks.json")))
    L = {k: v for k, v in d.get("clouds_v2", {}).items() if not k.startswith("_")}
    for k in ("cloud_ms_contribution", "cloud_ms_occlusion", "cloud_ms_eccentricity", "cloud_phase_g", "cloud_phase_g2", "cloud_phase_blend"):
        L[k] = d["cloud"][k]
    return L


class Clouds:
    def __init__(self, scene):
        self.s = scene
        self.shape = load_volume("cloud_shape.png", 128, 16, 8, 4)
        self.detail = load_volume("cloud_detail.png", 32, 8, 4, 3)
        L = looks_from_file()
        L.update(scene.get("looks", {}))
        scene["looks"] = L
        self.L = L
        lo, mi = scene["low"], scene["mid"]
        # (as ATorqueSky sets MPC_Sky: each layer's shape size by its lumps, its detail size)
        self.scale = [L["v2_shape_m"] * (1.3 - 0.6 * lo["lumps"]), L["v2_mid_shape_m"] * (1.3 - 0.6 * mi["lumps"])]
        # (V2DetailSize: a deep layer's detail grows by sqrt(depth / v2_detail_deep_m))
        depth = max(scene["top"] - scene["base"], 0.0)
        grow = lambda P: max(np.sqrt(max(depth * (P["top"] - P["bottom"]), 0.0) / L.get("v2_detail_deep_m", 3000.0)), 1.0)
        self.dscale = [L["v2_detail_m"] * grow(lo), L["v2_mid_detail_m"] * grow(mi)]
        self.ext = L["v2_extinction"]

    def density(self, x, y, alt, shape_only=False):
        """Extinction per metre at points (metres east, north, above sea level): CLOUD_V2_HLSL."""
        s, L = self.s, self.L
        base_m, top_m = s["base"], s["top"]
        norm = (alt - base_m) / max(top_m - base_m, 1.0)
        best = np.zeros_like(x)
        dist_km = np.sqrt(x * x + y * y + (alt - self.s.get("eye_asl", 252.0)) ** 2) / 1000.0
        broad = trilinear(self.shape[..., :1], np.stack([x / L["v2_broad_m"], y / L["v2_broad_m"], np.zeros_like(x)], -1))[..., 0]
        for ly, key in ((0, "low"), (1, "mid")):
            P = s[key]
            cv = P["cover"]
            if cv <= 0.001:
                continue
            h = (norm - P["bottom"]) / max(P["top"] - P["bottom"], 1e-3)
            inside = (h >= 0.0) & (h <= 1.0)
            if not inside.any():
                continue
            # The weather's own patches: cover a little more and less across tens of km.
            cvl = sat(cv + (broad - 0.5) * L["v2_patchy"] * 2.0 * min(cv, 1.0 - cv))
            # The height profile: heaped (a flat base, rounded tops narrowing up) .. a flat sheet.
            heaped = sat(h / 0.03) * (1.0 - 0.9 * smooth(0.15, 1.0, h))
            deck = sat(h / 0.12) * sat((1.0 - h) / 0.3)
            profile = heaped + (deck - heaped) * P["shape"]
            sc = self.scale[ly]
            w = np.stack([x / sc, y / sc, alt / sc], -1)
            base = trilinear(self.shape[..., :1], w)[..., 0]
            # (the same forms a third the size: billows on the big shapes)
            w3 = np.stack([x / (sc * 0.31) + 0.37, y / (sc * 0.31) + 0.71, alt / (sc * 0.31)], -1)
            base = base * (1.0 - L["v2_billow"]) + trilinear(self.shape[..., :1], w3)[..., 0] * L["v2_billow"]
            bc = sat(remap(base * profile, 1.0 - cvl, 1.0, 0.0, 1.0))
            # (a closed sky: cloud everywhere in the sheet, lumpy, no holes)
            cf = L.get("v2_closed_from", 0.85)
            bc = np.maximum(bc, profile * (0.45 + 0.55 * base) * smooth(cf, max(L.get("v2_closed_full", 0.92), cf + 0.01), cvl))
            if not shape_only:
                ds = self.dscale[ly]
                d = trilinear(self.detail, np.stack([x / ds, y / ds, alt / ds], -1))
                df = d[..., 0] * 0.625 + d[..., 1] * 0.25 + d[..., 2] * 0.125
                mod = df + ((1.0 - df) - df) * sat(h * 4.0)
                erode = (L["v2_erode"] + L["v2_erode_ragged"] * P["ragged"]) * (1.0 - 0.5 * sat(dist_km / max(L.get("v2_detail_fade_km", 10.0), 0.1)))
                bc = sat(remap(bc, mod * erode, 1.0, 0.0, 1.0))
            # (crisp edges: a cumulus is dense right to its skin, not a fog thinning over a kilometre)
            bc = sat(bc * (L["v2_edge_sharp"] + (1.0 - L["v2_edge_sharp"]) * sat(dist_km / max(L.get("v2_detail_fade_km", 10.0), 0.1))))
            thin = 1.0 + (L.get("v2_deck_thin", 1.0) - 1.0) * P["shape"]
            best = np.maximum(best, np.where(inside, bc * P["density"] * thin, 0.0))
        return best * self.ext


def hg(cos, g):
    return (1 - g * g) / (4 * math.pi * np.power(np.maximum(1 + g * g - 2 * g * cos, 1e-4), 1.5))


def render(scene, width=960, steps=96):
    height = width * 9 // 16
    C = Clouds(scene)
    L = scene["looks"]
    sun_e, sun_b = math.radians(scene["sun"]), math.radians(scene.get("sun_bearing", 180.0))
    sun = np.array([math.cos(sun_e) * math.sin(sun_b), math.cos(sun_e) * math.cos(sun_b), math.sin(sun_e)])
    look_b, pitch = math.radians(scene.get("look_bearing", 200.0)), math.radians(scene.get("pitch", 12.0))
    fov = math.radians(scene.get("fov", 90.0))
    fwd = np.array([math.cos(pitch) * math.sin(look_b), math.cos(pitch) * math.cos(look_b), math.sin(pitch)])
    right = np.array([math.cos(look_b), -math.sin(look_b), 0.0])
    up = np.cross(right, fwd)
    tx = math.tan(fov / 2)
    u = (np.arange(width) + 0.5) / width * 2 - 1
    v = 1 - (np.arange(height) + 0.5) / height * 2
    U, V = np.meshgrid(u * tx, v * tx * height / width)
    D = fwd[None, None] + U[..., None] * right + V[..., None] * up
    D /= np.linalg.norm(D, axis=-1, keepdims=True)
    cam = np.array([0.0, 0.0, scene.get("eye_asl", 252.0)])
    # Light: the sun's colour by its height; the sky's ambient; the plain sky behind.
    warm = sat(1.0 - scene["sun"] / 25.0)
    sun_col = np.array([1.0, 0.96 - 0.25 * warm, 0.9 - 0.45 * warm]) * (1.0 - 0.75 * scene.get("dull", 0.0))
    sky_top = np.array([0.18, 0.32, 0.62]) * (1 - scene.get("dull", 0.0)) + np.array([0.42, 0.44, 0.47]) * scene.get("dull", 0.0)
    sky_hor = np.array([0.62, 0.70, 0.80]) * (1 - scene.get("dull", 0.0)) + np.array([0.58, 0.59, 0.60]) * scene.get("dull", 0.0)
    el = sat(D[..., 2])
    sky = sky_hor + (sky_top - sky_hor) * np.power(el, 0.5)[..., None]
    cs = (D * sun).sum(-1)
    sky = sky + sun_col * (0.25 * np.power(sat(cs), 40) + 4.0 * np.power(sat(cs), 3000))[..., None]
    ground = np.array([0.16, 0.2, 0.12])
    img = np.where((D[..., 2] > 0)[..., None], sky, ground)
    m = D[..., 2] > 0.004
    d = D[m]
    t0 = np.maximum((scene["base"] - cam[2]) / d[:, 2], 0.0)
    t1 = np.minimum((scene["top"] - cam[2]) / d[:, 2], 45000.0)
    live = t1 > t0
    trans = np.ones(len(d))
    light = np.zeros((len(d), 3))
    amb_sky = sky_top * 1.2 + 0.25
    amb_ground = np.array([0.10, 0.10, 0.10]) + ground * 0.4
    ms_a, ms_b, ms_c = L["cloud_ms_contribution"], L["cloud_ms_occlusion"], L["cloud_ms_eccentricity"]
    g1, g2, blend = L["cloud_phase_g"], L["cloud_phase_g2"], L["cloud_phase_blend"]
    cos_sun = d @ sun
    dt = np.where(live, (t1 - t0) / steps, 0.0)
    jitter = np.random.default_rng(1).random(len(d))   # (the game jitters each frame and blends them)
    for i in range(steps):
        tt = t0 + (i + jitter) * dt
        p = cam + d * tt[:, None]
        on = live & (trans > 0.01)
        if not on.any():
            break
        sig = np.zeros(len(d))
        sig[on] = C.density(p[on, 0], p[on, 1], p[on, 2])
        hit = sig > 1e-5
        if hit.any():
            q = p[hit]
            tau = np.zeros(hit.sum())
            dist = 0.0
            for step in (60.0, 120.0, 240.0, 480.0, 960.0):
                dist += step * 0.5
                qq = q + sun * dist
                tau += C.density(qq[:, 0], qq[:, 1], qq[:, 2], shape_only=True) * step
                dist += step * 0.5
            cs_h = cos_sun[hit]
            scat = 0.0
            for o in range(3):
                g_a, g_b = g1 * ms_c ** o, g2 * ms_c ** o
                ph = hg(cs_h, g_a) * (1 - blend) + hg(cs_h, g_b) * blend
                scat = scat + (ms_a ** o) * ph * np.exp(-tau * (ms_b ** o))
            scat = scat * 4 * math.pi * 2.2   # (sun's brightness against the sky's, in this stand-in)
            hfrac = sat((q[:, 2] - scene["base"]) / max(scene["top"] - scene["base"], 1.0))
            amb = amb_ground + (amb_sky - amb_ground) * hfrac[:, None]
            s_h = sig[hit]
            step_t = np.exp(-s_h * dt[hit])
            contrib = (scat[:, None] * sun_col + amb * 0.6) * (1 - step_t)[:, None]
            idx = np.where(hit)[0]
            light[idx] += trans[idx, None] * contrib
            trans[idx] *= step_t
    # Air between: far cloud fades into the horizon's haze.
    haze = sat(1.0 - np.exp(-np.where(live, t0, 60000.0) / 30000.0))
    bg = img[m]
    col = light + bg * trans[:, None]
    col = col * (1 - haze[:, None]) + bg * haze[:, None]
    img[m] = col
    # (exposure and a filmic curve)
    x = img * scene.get("exposure", 1.0)
    x = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)
    x = np.power(sat(x), 1 / 2.2)
    return (x * 255 + 0.5).astype(np.uint8)


def main():
    args = sys.argv[1:]
    width = int(args[args.index("--width") + 1]) if "--width" in args else 960
    steps = int(args[args.index("--steps") + 1]) if "--steps" in args else 96
    scene = json.load(open(args[0]))
    Image.fromarray(render(scene, width, steps)).save(args[1])
    print("wrote", args[1])


if __name__ == "__main__":
    main()
