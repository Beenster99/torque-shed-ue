#!/usr/bin/env python3
"""The becks and gills on the ride's own land: the map's streams, as a stony bed with water running down it.

    python Tools/make_becks.py --torque-shed ../torque-shed     (then python Tools/make_foliage.py)

The Godot region's streams.json (OpenStreetMap's becks, simplified to within 4 m) are moved onto the floor
of the gill the 2 m land actually has (the lowest ground within a few metres across, smoothed along), and
built on the land as drawn: a bed of wet stones and gravel a little wider than the water, cobbles and
boulders along it (more at the edges and on steep stretches, where the water has moved them), and the
water itself. Bigger becks (the longer lines on the map, Thwaite Beck) are wider. They stop short of the
road, which crosses them on culverts.

Writes Data/Buttertubs/Becks/:

    bed_NN.glb      the bed, vertex-coloured for M_Stone (R brightness, G bare stone, B lichen, A moss), UV0
                    metres of surface
    SM_BeckStone_N.glb, instances.bin, becks.json
                    the cobbles and boulders: six stone models and where each stone lies (the trees' format),
                    planted by a TorqueFoliage actor, drawn out to 600 m
    water_NN.glb    the water, for M_Beck: UV0 x across (0..1), y metres downstream (the ripples run that way);
                    vertex colour R is how fast it runs (steep stretches are white water)

make_foliage.py keeps its plants off them. Only numpy is needed.
"""
import argparse, json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from export_buttertubs import write_glb   # noqa: E402
import buttertubs_far as FAR              # noqa: E402
import tree_models as TM                  # noqa: E402
from make_trees import Land, DATA, line_dist   # noqa: E402

OUT = os.path.join(DATA, "Becks")
STEP = 1.0           # along the beck (m)
ROAD_GAP = 6.0       # stop this far from the road's edge
N_ROCKS = 6
LIFT = 0.05          # the bed sits this far over the land (it conforms to it; this hides the land's facets)


def smooth(a, sigma):
    k = np.arange(-int(3 * sigma), int(3 * sigma) + 1)
    w = np.exp(-0.5 * (k / sigma) ** 2); w /= w.sum()
    pad = len(k) // 2
    return np.convolve(np.pad(a, pad, mode="edge"), w, mode="valid")


class Out:
    def __init__(self):
        self.chunks = [TM.Mesh()]

    def add(self, P, T, C, UV):
        if self.chunks[-1].n + len(P) > 60000:
            self.chunks.append(TM.Mesh())
        self.chunks[-1].add(P, T, C, UV)

    def write(self, prefix):
        names = []
        for k, m in enumerate(self.chunks):
            if not m.n:
                continue
            P, T, N, C, UV = m.arrays()
            name = "%s_%02d" % (prefix, k)
            write_glb(os.path.join(OUT, name + ".glb"), P, T, N, C, name, UV)
            names.append(name)
        return names


def ribbon(Q, side, half, n_across, y_fn, lift):
    """A strip along points Q (n, 2) with unit side vectors, `half` (n,) wide each side, n_across
    vertices across, each at the land's height plus `lift`. Returns positions (n, n_across, 3), tris."""
    a = np.linspace(-1, 1, n_across)
    X = Q[:, None, 0] + side[:, None, 0] * half[:, None] * a[None]
    Z = Q[:, None, 1] + side[:, None, 1] * half[:, None] * a[None]
    Y = y_fn(X, Z) + lift   # lift: a number, or (1, n_across) to lie flusher at the edges
    P = np.stack([X, Y, Z], -1)
    n = len(Q)
    i = np.arange(n * n_across).reshape(n, n_across)
    p, q, r, s = i[:-1, :-1], i[:-1, 1:], i[1:, :-1], i[1:, 1:]
    T = np.concatenate([np.stack([p, r, q], -1).reshape(-1, 3), np.stack([q, r, s], -1).reshape(-1, 3)])
    PP = P.reshape(-1, 3)
    e1 = PP[T[:, 1]] - PP[T[:, 0]]; e2 = PP[T[:, 2]] - PP[T[:, 0]]
    if np.cross(e1, e2)[:, 1].sum() < 0:
        T = T[:, [0, 2, 1]]
    return P, T


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--torque-shed", default=os.path.join(os.path.dirname(REPO), "torque-shed"))
    args = ap.parse_args()
    route = json.load(open(os.path.join(DATA, "route.json")))
    O = route["origin_region_m"]
    root = os.path.join(args.torque_shed, "data", "regions", "dales")
    meta = json.load(open(os.path.join(root, "region.json")))
    to_ride = FAR.frame(meta, O)
    land = Land(route)
    ground = land.near
    road = np.array(route["line_sim"])[:, [0, 2]]
    half_road = route["road_half_width_m"]
    rng = np.random.default_rng(31)
    bed, water = Out(), Out()
    total = 0.0
    stones = []
    for row in json.load(open(os.path.join(root, "streams.json")))["streams"]:
        lat, lon = row[1], row[2]
        pts = [to_ride(lat, lon)]
        for k in range(3, len(row) - 1, 2):
            lat += row[k]; lon += row[k + 1]
            pts.append(to_ride(lat, lon))
        L = np.array(pts)
        if not land.in_tiles(L[:, 0], L[:, 1]).any():
            continue
        seg = np.linalg.norm(np.diff(L, axis=0), axis=1)
        s = np.concatenate([[0], np.cumsum(seg)])
        length = s[-1]
        t = np.arange(0, length, STEP)
        if len(t) < 6:
            continue
        Q = np.stack([np.interp(t, s, L[:, 0]), np.interp(t, s, L[:, 1])], 1)
        d = np.gradient(Q, axis=0); d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
        side = np.stack([-d[:, 1], d[:, 0]], 1)
        # Onto the gill's floor: the lowest ground within 5 m across, smoothed along the beck.
        offs = np.arange(-5.0, 5.01, 0.5)
        G = np.stack([ground(Q[:, 0] + side[:, 0] * o, Q[:, 1] + side[:, 1] * o) for o in offs], 1)
        shift = smooth(offs[np.argmin(G + 0.02 * np.abs(offs)[None], axis=1)], 5.0)
        Q = Q + side * shift[:, None]
        d = np.gradient(Q, axis=0); d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
        side = np.stack([-d[:, 1], d[:, 0]], 1)
        # Where it runs: on the 2 m tiles (a few metres in from their edge) and clear of the road.
        ok = land.in_tiles(Q[:, 0] + 6, Q[:, 1]) & land.in_tiles(Q[:, 0] - 6, Q[:, 1]) & \
             land.in_tiles(Q[:, 0], Q[:, 1] + 6) & land.in_tiles(Q[:, 0], Q[:, 1] - 6)
        ok &= line_dist(Q[:, 0], Q[:, 1], [road], cell=30.0) > half_road + ROAD_GAP
        idx = np.flatnonzero(ok)
        if len(idx) < 6:
            continue
        w0 = float(np.clip(0.7 + length / 1800.0, 0.7, 3.2))   # water width: longer becks are bigger
        for run in np.split(idx, np.flatnonzero(np.diff(idx) > 1) + 1):
            if len(run) < 6:
                continue
            q, sd = Q[run], side[run]
            n = len(q)
            gy = ground(q[:, 0], q[:, 1])
            steep = np.clip(-np.gradient(smooth(gy, 2.0)) / STEP, 0, 1)   # fall per metre
            wv = w0 * (1 + 0.25 * np.sin(np.arange(n) / rng.uniform(5, 9) + rng.uniform(0, 6))) * (1 - 0.3 * np.clip(steep * 3, 0, 1))
            end = np.minimum(np.arange(n), np.arange(n)[::-1]) * STEP
            taper = np.clip(end / 3.0, 0.2, 1)
            # The bed: wet stone and gravel, a good deal wider than the water, mossy at its edges.
            hb = (wv * 0.9 + 0.5) * taper
            P, T = ribbon(q, sd, hb, 7, ground, LIFT * (0.25 + 0.75 * (1 - np.linspace(-1, 1, 7) ** 2))[None])
            na = P.shape[1]
            across = np.abs(np.linspace(-1, 1, na))[None].repeat(n, 0)
            C = np.zeros((n, na, 4))
            C[..., 0] = 0.28 + 0.1 * across + rng.normal(0, 0.03, (n, na))   # dark, wet
            C[..., 1] = 0.6; C[..., 2] = 0.1
            C[..., 3] = np.clip((across - 0.55) / 0.45, 0, 1) * 0.9            # moss at the edges
            along = np.arange(n)[:, None].repeat(na, 1) * STEP
            UV = np.stack([along, across * hb[:, None]], -1)
            bed.add(P.reshape(-1, 3), T, C.reshape(-1, 4), UV.reshape(-1, 2))
            # The water: a little over the bed, narrower.
            hw = wv * 0.5 * taper
            Pw, Tw = ribbon(q, sd, hw, 3, ground, LIFT + 0.04)
            Cw = np.zeros((n, 3, 4)); Cw[..., 0] = np.clip(steep * 4, 0, 1)[:, None]; Cw[..., 3] = 1
            UVw = np.stack([np.linspace(0, 1, 3)[None].repeat(n, 0), along[:, :3]], -1)
            water.add(Pw.reshape(-1, 3), Tw, Cw.reshape(-1, 4), UVw.reshape(-1, 2))
            # Stones: cobbles all along, boulders now and then, more at the edges and on the steep.
            for k in range(n):
                for _ in range(rng.poisson(1.2 + 3 * steep[k])):
                    a = rng.uniform(-1, 1) * hb[k] * 1.05
                    sz = rng.choice([0.08, 0.14, 0.22, 0.4, 0.7], p=[0.35, 0.3, 0.2, 0.11, 0.04])
                    sz *= rng.uniform(0.75, 1.3)
                    c = q[k] + sd[k] * a + d[run][k] * rng.uniform(-0.5, 0.5)
                    gyc = float(ground(np.array([c[0]]), np.array([c[1]]))[0])
                    stones.append((int(rng.integers(N_ROCKS)), c[0], gyc + LIFT - sz * 0.12, c[1], rng.uniform(0, 2 * math.pi), sz))
            total += n * STEP
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".glb"):
            os.remove(os.path.join(OUT, f))
    names = bed.write("bed") + water.write("water")
    # The stones: a few rock models, planted as instances (by a TorqueFoliage actor, as the trees are).
    rock_names = []
    for k in range(N_ROCKS):
        r = np.random.default_rng(900 + k)
        V, F = TM.lumpy_sphere(np.zeros(3), 0.5, r, 2)
        V = V * np.array([r.uniform(1.0, 1.4), 0.55, 1.0])
        up = np.clip((V[:, 1] + 0.3) / 0.6, 0, 1)
        C = np.zeros((len(V), 4))
        C[:, 0] = 0.34 * r.uniform(0.85, 1.15) * (1 + 0.1 * r.normal(0, 1, len(V)))
        C[:, 1] = r.uniform(0.4, 0.9); C[:, 2] = 0.15; C[:, 3] = up * 0.45
        UV = np.stack([V[:, 0] + V[:, 1], V[:, 2] + V[:, 1]], 1)
        m = TM.Mesh(); m.add(V, F, C, UV)
        P, T, N, Cc, UVv = m.arrays()
        name = "SM_BeckStone_%d" % k
        write_glb(os.path.join(OUT, name + ".glb"), P, T, N, Cc, name, UVv)
        rock_names.append(name)
    st = np.array(stones)
    groups = {}
    for k, name in enumerate(rock_names):
        m = st[:, 0] == k
        groups[name] = (st[m][:, [1, 2, 3]], st[m][:, 4], st[m][:, 5])
    from make_trees import write_instances
    count, nbytes = write_instances(os.path.join(OUT, "instances.bin"), groups, rock_names)
    json.dump({"about": "Becks made by Tools/make_becks.py; instances.bin's species index is into 'species'.",
               "species": [{"name": n, "mesh": n, "count": int((st[:, 0] == k).sum()), "cull_m": 600, "shadow": True}
                           for k, n in enumerate(rock_names)],
               "km": round(total / 1000, 2), "meshes": names},
              open(os.path.join(OUT, "becks.json"), "w"), indent=1)
    print("becks: %.1f km, %d stones, %d meshes: %s" % (total / 1000, len(stones), len(names), names))


if __name__ == "__main__":
    main()
