#!/usr/bin/env python3
"""Limestone scars on the far fells, along the map's cliffs, as geometry set into the 32 m far hills.

    python Tools/make_far_rock.py --torque-shed ../torque-shed

The 32 m far hills smooth a Dales scar (a 4 to 12 m face of bedded limestone, a shelf of turf above it)
into an even slope. Along each of the map's cliff lines more than 40 m outside the ride's 2 m tiles, this
builds the step back: a turf-capped shelf that rises out of the hillside behind the cliff line, a bedded
face (three beds, each a little set back) dropping from its lip, and a buried foot, so the face shows as
high as the map's slope allows and every edge of it runs into the ground. Heights come from the far
hills exactly as drawn (Tools/make_trees.Land).

Writes Data/Buttertubs/Far/scars_NN.glb, the faces (vertex-coloured for M_Stone: R brightness, G bare
bedded stone, B lichen, A moss; UV0 metres of surface), and shelf_NN.glb, the turf above them, coloured as
the far hills under them are so that M_Land draws them as the same hillside. Run export_buttertubs.py first if the land has changed.
Only numpy is needed.
"""
import argparse, json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from export_buttertubs import write_glb   # noqa: E402
import buttertubs_far as FAR              # noqa: E402
from make_trees import Land, DATA         # noqa: E402

OUT = os.path.join(DATA, "Far")
STEP = 4.0          # along the cliff (m)
MARGIN = 40.0       # stay this far outside the 2 m tiles (the backdrop is sunk under them)
LIMIT = 60000       # vertices per glb


def smooth_noise(n, rng, scale):
    """Smooth random values along a line of n points, correlated over about `scale` points."""
    k = max(int(scale), 1)
    raw = rng.normal(0, 1, n + 4 * k)
    w = np.exp(-0.5 * (np.arange(-2 * k, 2 * k + 1) / k) ** 2); w /= w.sum()
    s = np.convolve(raw, w, mode="same")[2 * k:2 * k + n]
    return s / max(s.std(), 1e-6)


def build(land, cliffs, rng, land_rgb):
    chunks = {"face": [[[], [], [], [], 0]], "shelf": [[[], [], [], [], 0]]}
    total_m = 0.0
    tb = land.back
    bx1 = tb.x0 + (tb.H.shape[1] - 1) * tb.step; bz1 = tb.z0 + (tb.H.shape[0] - 1) * tb.step

    def ok(x, z):
        far = ~land.in_tiles(x + MARGIN, z) & ~land.in_tiles(x - MARGIN, z) & ~land.in_tiles(x, z + MARGIN) & \
              ~land.in_tiles(x, z - MARGIN) & ~land.in_tiles(x, z)
        return far & (x > tb.x0 + 64) & (x < bx1 - 64) & (z > tb.z0 + 64) & (z < bz1 - 64)

    for L in cliffs:
        seg = np.linalg.norm(np.diff(L, axis=0), axis=1)
        s = np.concatenate([[0], np.cumsum(seg)])
        if s[-1] < 12:
            continue
        t = np.arange(0, s[-1], STEP)
        P = np.stack([np.interp(t, s, L[:, 0]), np.interp(t, s, L[:, 1])], 1)
        keep = ok(P[:, 0], P[:, 1])
        idx = np.flatnonzero(keep)
        if len(idx) < 3:
            continue
        for run in np.split(idx, np.flatnonzero(np.diff(idx) > 1) + 1):
            if len(run) < 3:
                continue
            Q = P[run]
            n = len(Q)
            d = np.gradient(Q, axis=0); d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
            nrm = np.stack([-d[:, 1], d[:, 0]], 1)
            # Which side is downhill (the face looks that way), from the ground either side.
            g0 = land(Q[:, 0], Q[:, 1])
            gp = land(Q[:, 0] + nrm[:, 0] * 12, Q[:, 1] + nrm[:, 1] * 12)
            gm = land(Q[:, 0] - nrm[:, 0] * 12, Q[:, 1] - nrm[:, 1] * 12)
            down = np.where((gp - gm).mean() < 0, 1.0, -1.0)
            nrm = nrm * down
            slope = np.clip((np.maximum(gm, gp) - np.minimum(gm, gp)) / 24.0, 0.05, 2.0)
            # The face's height: 2.5 to 5.5 m, varying along it, tapering to nothing at the ends. (The
            # 32 m hills already hold some of the step; 4-11 m on top of it stood up as a knife-edge.)
            hn = smooth_noise(n, rng, 6)
            Hs = np.clip(4.0 + 1.0 * hn, 2.5, 5.5)
            end = np.minimum(np.arange(n), np.arange(n)[::-1]) * STEP
            Hs *= np.clip(end / 25.0, 0.0, 1.0) ** 0.7
            # The shelf: as deep as it takes the slope to fall by the face's height, within reason.
            shelf = np.clip(Hs / slope * 0.6, 1.0, 5.0)
            lip_y = g0 + 0.25
            # Cross-section, from behind (uphill) to in front, as (offset along nrm, height):
            #   back (buried), cliff line, shelf edge (the lip), three beds down the face, foot (buried).
            offs, ys, moss, bright = [], [], [], []
            back = -6.0
            offs.append(np.full(n, back)); ys.append(land(Q[:, 0] + nrm[:, 0] * back, Q[:, 1] + nrm[:, 1] * back) - 0.7)
            moss.append(np.ones(n)); bright.append(np.full(n, 0.45))
            offs.append(np.zeros(n)); ys.append(lip_y); moss.append(np.ones(n)); bright.append(np.full(n, 0.45))
            offs.append(shelf - 0.6); ys.append(lip_y - 0.45 * shelf * slope); moss.append(np.full(n, 0.9)); bright.append(np.full(n, 0.48))
            top = lip_y - 0.45 * shelf * slope - 0.25 * np.minimum(Hs, 1.4)
            offs.append(shelf); ys.append(top); moss.append(np.full(n, 0.35)); bright.append(np.full(n, 0.55))
            bed = [0.0, 0.36, 0.68, 1.0]
            for k in range(1, 4):
                # Each bed a little set back from the one above, its top edge overhanging.
                f = bed[k]
                o = shelf + f * Hs * 0.18 + np.where(k % 2, -0.35, 0.25) + rng.normal(0, 0.15, n)
                yy = top - f * Hs
                offs.append(o - 0.2); ys.append(yy + 0.25); moss.append(np.full(n, 0.12)); bright.append(np.full(n, 0.5 + 0.04 * k))
                offs.append(o); ys.append(yy); moss.append(np.full(n, 0.2 if k < 3 else 0.35)); bright.append(np.full(n, 0.46))
            fo = shelf + Hs * 0.18 + 1.0
            foot_g = land(Q[:, 0] + nrm[:, 0] * fo, Q[:, 1] + nrm[:, 1] * fo)
            ys[-1] = np.minimum(ys[-1], foot_g - 0.4)
            offs.append(np.full(n, fo + 3.0))
            ys.append(land(Q[:, 0] + nrm[:, 0] * (fo + 3), Q[:, 1] + nrm[:, 1] * (fo + 3)) - 0.8)
            moss.append(np.full(n, 0.7)); bright.append(np.full(n, 0.42))
            O_ = np.stack(offs, 1); Y = np.stack(ys, 1); M = np.stack(moss, 1); Bv = np.stack(bright, 1)
            m = O_.shape[1]
            # Roughen the face across and along: bedding joints and blocks.
            jag = rng.normal(0, 0.18, (n, m)); jag[:, :4] = 0; jag[:, -2:] = 0
            Xp = Q[:, None, 0] + nrm[:, None, 0] * (O_ + jag)
            Zp = Q[:, None, 1] + nrm[:, None, 1] * (O_ + jag)
            pos = np.stack([Xp, Y, Zp], -1).reshape(-1, 3)
            # Colour (for M_Stone): R brightness, G bare bedded stone, B lichen, A moss/turf.
            col = np.zeros((n * m, 4))
            col[:, 0] = (Bv * np.exp(rng.normal(0, 0.06, (n, m)))).ravel()
            col[:, 1] = 0.75; col[:, 2] = 0.35; col[:, 3] = M.ravel()
            # UV: metres along the cliff and down the section.
            along = np.arange(n)[:, None] * STEP
            down_m = np.concatenate([np.zeros((n, 1)), np.cumsum(np.hypot(np.diff(O_, axis=1), np.diff(Y, axis=1)), axis=1)], 1)
            uv = np.stack([np.repeat(along, m, 1), down_m], -1).reshape(-1, 2)
            # Two meshes: the turf shelf (rows up to the lip), drawn with the land's own material and
            # colour so it is the hillside; and the face (the lip down), drawn with M_Stone.
            for part, rows in (("shelf", np.arange(0, 4)), ("face", np.arange(3, m))):
                k = len(rows)
                sub = np.arange(n * m).reshape(n, m)[:, rows]
                pp = pos[sub.ravel()]
                cc = col[sub.ravel()].copy()
                if part == "shelf":
                    cc = np.concatenate([land_rgb(pp[:, 0], pp[:, 2]), np.ones((len(pp), 1))], 1)
                i = np.arange(n * k).reshape(n, k)
                a, b, c, e = i[:-1, :-1], i[:-1, 1:], i[1:, :-1], i[1:, 1:]
                tris = np.concatenate([np.stack([a, c, b], -1).reshape(-1, 3), np.stack([b, c, e], -1).reshape(-1, 3)])
                e1 = pp[tris[:, 1]] - pp[tris[:, 0]]; e2 = pp[tris[:, 2]] - pp[tris[:, 0]]
                if np.cross(e1, e2)[:, 1].sum() < 0:
                    tris = tris[:, [0, 2, 1]]
                ch = chunks[part][-1]
                if ch[4] + len(pp) > LIMIT:
                    chunks[part].append([[], [], [], [], 0]); ch = chunks[part][-1]
                ch[0].append(pp); ch[1].append(tris + ch[4]); ch[2].append(cc); ch[3].append(uv[sub.ravel()]); ch[4] += len(pp)
            total_m += n * STEP
    return chunks, total_m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--torque-shed", default=os.path.join(os.path.dirname(REPO), "torque-shed"))
    args = ap.parse_args()
    route = json.load(open(os.path.join(DATA, "route.json")))
    O = route["origin_region_m"]
    root = os.path.join(args.torque_shed, "data", "regions", "dales")
    meta = json.load(open(os.path.join(root, "region.json")))
    land = Land(route)
    cliffs, _ = FAR.features(root, meta, O)
    # The far hills' own colours (linear 0..1), for the shelves.
    from make_foliage import read_glb
    bg = read_glb(os.path.join(DATA, "backdrop.glb"))
    bp = bg["POSITION"]; bc = bg["COLOR_0"][:, :3].astype(np.float64) / 255.0
    nbx = len(np.unique(bp[:, 0]))
    bstep = float(bp[1, 0] - bp[0, 0])

    def land_rgb(x, z):
        i = np.clip(np.round((x - bp[0, 0]) / bstep).astype(int), 0, nbx - 1)
        j = np.clip(np.round((z - bp[0, 2]) / bstep).astype(int), 0, len(bp) // nbx - 1)
        return bc[j * nbx + i]
    chunks, total_m = build(land, cliffs, np.random.default_rng(21), land_rgb)
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".glb") and (f.startswith("scars_") or f.startswith("shelf_")):
            os.remove(os.path.join(OUT, f))
    names = []
    for part, prefix in (("face", "scars"), ("shelf", "shelf")):
        for k, (P, T, C, UV, n) in enumerate(chunks[part]):
            if not n:
                continue
            pos = np.concatenate(P); tris = np.concatenate(T); col = np.concatenate(C); uv = np.concatenate(UV)
            fn = np.cross(pos[tris[:, 1]] - pos[tris[:, 0]], pos[tris[:, 2]] - pos[tris[:, 0]])
            nrm = np.zeros_like(pos)
            for i in range(3):
                np.add.at(nrm, tris[:, i], fn)
            nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
            name = "%s_%02d" % (prefix, k)
            write_glb(os.path.join(OUT, name + ".glb"), pos, tris, nrm,
                      np.clip(np.round(col * 255), 0, 255).astype(np.uint8), name, uv)
            names.append(name)
    print("far scars: %.1f km of cliff in %d meshes" % (total_m / 1000, len(names)))


if __name__ == "__main__":
    main()
