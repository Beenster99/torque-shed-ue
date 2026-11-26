"""Measured check: do our prop models show their back faces? ([caravans, build 175] beenster: "I still see
broken caravans". The prop material is one-sided, so a panel wound inside out is drawn as a hole; half of a
tourer's panels were, from Tools/blender_street_kit.py's _object guessing their way round by signed volume.)

Casts rays at each model from all round (and from above and a little below) and counts the first surfaces
they meet that face away from the ray: those are holes in the game. Reads the glbs only (numpy, no Blender).

    python Tools/check_prop_faces.py                       # every Data/RideUK/Props model
    python Tools/check_prop_faces.py caravan forecourt     # the models whose names hold any of these words
    python Tools/check_prop_faces.py --max 0.5 FILE.glb    # fail above 0.5 % (the default)

Exits 1 if any model shows more back faces than --max percent of what the rays meet.
"""
import json
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PROPS = os.path.join(os.path.dirname(HERE), "Data", "RideUK", "Props")


def load_glb(path):
    """All the glb's triangles: positions (n, 3) and corner indices (m, 3), the mesh nodes' transforms ignored
    (our kits write one node at the origin)."""
    b = open(path, "rb").read()
    jl = struct.unpack("<I", b[12:16])[0]
    js = json.loads(b[20:20 + jl])
    binary = b[20 + jl + 8:]

    def acc(i):
        a = js["accessors"][i]
        bv = js["bufferViews"][a["bufferView"]]
        n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        dt = np.dtype({5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a["componentType"]])
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = bv.get("byteStride", 0) or n * dt.itemsize
        raw = np.frombuffer(binary, np.uint8, (a["count"] - 1) * stride + n * dt.itemsize, off)
        rows = np.lib.stride_tricks.as_strided(raw, (a["count"], n * dt.itemsize), (stride, 1)).copy()
        return rows.view(dt).reshape(a["count"], n)

    P, I = [], []
    base = 0
    for m in js["meshes"]:
        for pr in m["primitives"]:
            p = acc(pr["attributes"]["POSITION"]).astype(np.float64)
            idx = acc(pr["indices"]).reshape(-1, 3).astype(np.int64) if "indices" in pr else np.arange(len(p)).reshape(-1, 3)
            P.append(p); I.append(idx + base)
            base += len(p)
    return np.concatenate(P), np.concatenate(I)


def back_faces(P, I, res=40):
    """(rays that met the model, of which met a back face first). y is up (the glbs' frame)."""
    a, b, c = P[I[:, 0]], P[I[:, 1]], P[I[:, 2]]
    e1, e2 = b - a, c - a
    fn = np.cross(e1, e2)
    lo, hi = P.min(0), P.max(0)
    cen, R = (lo + hi) / 2, float(np.linalg.norm(hi - lo))
    met = back = 0
    for th in np.linspace(0, 2 * np.pi, 8, endpoint=False):
        for el in (-0.3, 0.15, 0.6, 1.3):
            v = np.array([np.cos(th) * np.cos(el), np.sin(el), np.sin(th) * np.cos(el)])
            d = -v / np.linalg.norm(v)
            up = np.array([0.0, 1.0, 0.0]) if abs(d[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
            x = np.cross(d, up); x /= np.linalg.norm(x)
            y = np.cross(x, d)
            g = np.linspace(-R / 2, R / 2, res)
            gx, gy = np.meshgrid(g, g)
            orig = cen - d * R * 2 + gx.reshape(-1, 1) * x + gy.reshape(-1, 1) * y
            pv = np.cross(d, e2)
            det = (e1 * pv).sum(1)
            ok = np.abs(det) > 1e-12
            inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
            facing_away = (fn @ d) > 0
            for s in range(0, len(orig), 128):
                tv = orig[s:s + 128, None, :] - a[None]
                u = (tv * pv[None]).sum(-1) * inv
                q = np.cross(tv, e1[None])
                vv = (q * d).sum(-1) * inv
                t = (q * e2[None]).sum(-1) * inv
                t = np.where(ok & (u >= 0) & (vv >= 0) & (u + vv <= 1) & (t > 1e-6), t, np.inf)
                j = t.argmin(1)
                hit = np.isfinite(t[np.arange(len(j)), j])
                met += int(hit.sum())
                back += int(facing_away[j[hit]].sum())
    return met, back


def main(argv):
    limit = 0.5
    args = list(argv)
    if "--max" in args:
        k = args.index("--max"); limit = float(args[k + 1]); del args[k:k + 2]
    files = [a for a in args if a.endswith(".glb")]
    words = [a for a in args if not a.endswith(".glb")]
    if not files:
        files = sorted(os.path.join(PROPS, f) for f in os.listdir(PROPS)
                       if f.endswith(".glb") and (not words or any(w in f for w in words)))
    failed = []
    for f in files:
        P, I = load_glb(f)
        met, back = back_faces(P, I)
        pct = 100.0 * back / max(met, 1)
        bad = pct > limit
        print("%-48s %6d tris  back faces %5.2f %%%s" % (os.path.basename(f), len(I), pct, "  FAIL" if bad else ""))
        if bad:
            failed.append(os.path.basename(f))
    print("%d models, %d show back faces over %.2f %%" % (len(files), len(failed), limit))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
