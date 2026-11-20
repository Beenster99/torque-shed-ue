#!/usr/bin/env python3
"""Ride UK's hedgerows: lengths of laid and trimmed field hedge, as real geometry for Nanite.

    python Tools/make_hedges.py            (writes Data/RideUK/Hedges/SM_Tree_hedge_<n>.glb)

beenster, build 148: the hedges were flat green blocks that "look like something from roblox". A
British field hedge is a row of shrubs planted close (hawthorn mostly, with blackthorn, hazel, field
maple and holly mixed in), cut by a flail each year into a rough A shape about 1.5-2 m tall, twiggy
and full of small leaves at its face, darker and open inside, with shoots sticking up out of the cut
and here and there a gap at the foot. Each model is one 4 m length of it, grown as Tools/tree_models.py
grows its trees (space colonisation from a stem every half metre or so, the leaves clustered on the
twig ends, opaque little polygons that Nanite draws quickly), so World/TorqueRideWorld can lay them
end to end along the map's hedge lines (RidePlan::PlantsTile, kind "hedge"), each a little over 4 m
apart's worth of overlap so no seam shows.

Frames as the trees': metres, x along the hedge (-2.1 to 2.1), y up, z across; the foot at y 0 with
its stems 0.4 m below so it sits into a slope. Vertex colour and UVs as the trees' (M_Tree): RGB
linear colour, A how much sky the spot sees; UV x 1 on leaves, 0 on wood; UV y how free to sway.

Tools/ue_build_rideuk.py (hedge_kit) imports them with the trees' material, or uses a scanned hedge
(Data/RideUK/scanned_trees.json "hedge" words) when one has been added from Fab.
"""
import json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tree_models as TM                    # noqa: E402
from export_buttertubs import write_glb     # noqa: E402

OUT = os.path.join(os.path.dirname(HERE), "Data", "RideUK", "Hedges")
LENGTH = 4.2          # the model's length (m); planted every 3.9 m
VARIANTS = 4

# The hedge's shrubs (leaves drawn ~1.5x life size, so a light hedge still reads as solid): leaf (shape, size m, leaves per twig end, sRGB colour, spread), bark, share.
# ([Nov 20] and each shrub's season, as the trees' (Tools/tree_models.py): hawthorn and blackthorn turn
# red-brown and fall, hazel and field maple yellow; holly, now in the mix, stays green all winter.)
SHRUBS = {
    "hawthorn": (("diamond", 0.05, 13, (58, 80, 34), 0.09), ((62, 54, 46), 0.08), 0.5, (1, 0.6, 0.0)),
    "blackthorn": (("diamond", 0.045, 12, (44, 60, 30), 0.08), ((40, 36, 34), 0.08), 0.18, (1, 0.85, 0.0)),
    "hazel": (("round", 0.07, 8, (72, 94, 40), 0.08), ((96, 76, 58), 0.08), 0.14, (1, 0.05, 0.0)),
    "maple": (("palmate", 0.065, 6, (66, 90, 36), 0.09), ((84, 76, 64), 0.08), 0.1, (1, 0.1, 0.0)),
    "holly": (("holly", 0.05, 8, (26, 46, 22), 0.06), ((112, 110, 100), 0.05), 0.08, (2, 0.0, 0.0)),
}


def envelope(rng, height, width, n):
    """n attraction points through the hedge: mostly in the outer 25 cm of its cut face (where the
    leaves are), an A-shaped section narrowing to a rounded top, the top's line lumpy along the
    length, some shoots standing up out of it, and a thin, gappy foot."""
    pts = []
    xs = rng.uniform(-LENGTH / 2, LENGTH / 2, n * 3)
    # The top's line: a few soft lumps along the length, and its height varies across the cut.
    ph = rng.uniform(0, 6.3, 3)
    top = height * (1 + 0.05 * np.sin(xs * 1.3 + ph[0]) + 0.04 * np.sin(xs * 2.9 + ph[1]) + 0.025 * np.sin(xs * 6.1 + ph[2]))
    t = rng.uniform(0, 1, n * 3) ** 0.8
    y = 0.12 + t * (top - 0.12)
    # Half width at height y: wide in the middle, narrowing towards the foot a little and the top a lot.
    hw = width / 2 * (1 - 0.32 * (y / top) ** 2) * (0.72 + 0.28 * np.clip(y / 0.6, 0, 1))
    side = np.where(rng.uniform(0, 1, n * 3) < 0.5, -1.0, 1.0)
    depth = np.abs(rng.normal(0, 0.16, n * 3))
    on_top = rng.uniform(0, 1, n * 3) < 0.28
    z = side * np.clip(hw - depth, 0, None)
    z = np.where(on_top, rng.uniform(-1, 1, n * 3) * hw * 0.8, z)
    y = np.where(on_top, top - depth * 0.8, y)
    P = np.stack([xs, y, z], 1)
    # Gaps at the foot where the stems show (the hedge isn't a wall down to the grass).
    keep = ~((P[:, 1] < 0.45) & (rng.uniform(0, 1, len(P)) < 0.55))
    P = P[keep]
    # Shoots: a few whippy stems a third to two thirds of a metre up out of the cut.
    shoots = []
    for _ in range(int(rng.integers(2, 6))):
        x0 = rng.uniform(-LENGTH / 2 + 0.2, LENGTH / 2 - 0.2)
        z0 = rng.uniform(-0.25, 0.25) * width
        up = rng.uniform(0.25, 0.5)
        for k in range(10):
            s = k / 9
            shoots.append([x0 + rng.normal(0, 0.03), height + s * up, z0 + rng.normal(0, 0.03) + s * rng.uniform(-0.15, 0.15)])
    P = np.vstack([P[rng.permutation(len(P))[:n]], np.array(shoots)])
    return P


def colonise(rng, A, stems, step=0.06, infl=0.4, kill=0.075):
    """Space colonisation from several stems at once (one root node each, 0.4 m underground, a short
    straight piece to the surface): nodes and parents."""
    nodes, parent = [], []
    for (x, z) in stems:
        nodes.append(np.array([x, -0.4, z])); parent.append(-1)
        nodes.append(np.array([x + rng.normal(0, 0.02), 0.1, z + rng.normal(0, 0.02)])); parent.append(len(nodes) - 2)
    N = np.array(nodes)
    best_d = np.full(len(A), np.inf); best_i = np.full(len(A), -1)

    def update(new_idx):
        Q = N[new_idx]
        d = np.linalg.norm(A[:, None, :] - Q[None, :, :], axis=2)
        k = np.argmin(d, axis=1); dk = d[np.arange(len(A)), k]
        m = dk < best_d
        best_d[m] = dk[m]; best_i[m] = np.asarray(new_idx)[k[m]]

    update([i for i in range(len(N)) if parent[i] >= 0])
    alive = np.ones(len(A), bool)
    for _ in range(260):
        alive &= best_d > kill
        act = alive & (best_d < infl)
        if not act.any():
            if not alive.any():
                break
            ai = np.flatnonzero(alive)[np.argmin(best_d[alive])]
            if best_d[ai] > infl * 3:
                break
            act = np.zeros(len(A), bool); act[ai] = True
        idx = best_i[act]
        dirs = A[act] - N[idx]
        dirs /= np.maximum(np.linalg.norm(dirs, axis=1, keepdims=True), 1e-9)
        uniq, inv = np.unique(idx, return_inverse=True)
        acc = np.zeros((len(uniq), 3))
        np.add.at(acc, inv, dirs)
        acc /= np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-9)
        acc[:, 1] += 0.18
        acc += rng.normal(0, 0.18, acc.shape)
        acc /= np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-9)
        new = N[uniq] + acc * step
        start = len(N)
        N = np.vstack([N, new])
        parent.extend(uniq.tolist())
        update(list(range(start, len(N))))
    return N, np.array(parent)


def core(mesh, rng, height, width, ao_fn):
    nx, ny = 22, 7
    xs = np.linspace(-LENGTH / 2 + 0.05, LENGTH / 2 - 0.05, nx)
    ts = np.linspace(0, 1, ny)
    rows = []
    for t in ts:
        ring = []
        for x in xs:
            y = 0.35 + t * (height * 0.86 - 0.35)
            hw = max(0.05, width / 2 * (1 - 0.32 * (y / height) ** 2) - rng.uniform(0.2, 0.32))
            ring.append((x, y, hw))
        rows.append(ring)
    P, Tr = [], []
    # Two sides and a top: a grid on each side (z = +-hw), joined over the top.
    for sgn in (-1.0, 1.0):
        base = len(P)
        for j, ring in enumerate(rows):
            for (x, y, hw) in ring:
                P.append([x + rng.normal(0, 0.03), y + rng.normal(0, 0.04), sgn * hw])
        for j in range(ny - 1):
            for i in range(nx - 1):
                a = base + j * nx + i; b = a + 1; c = a + nx; d = c + 1
                Tr += [[a, c, b], [b, c, d]] if sgn > 0 else [[a, b, c], [b, d, c]]
    top = len(P) // 2
    for i in range(nx - 1):
        a = (ny - 1) * nx + i; b = a + 1; c = top + a; d = c + 1
        Tr += [[a, b, c], [b, d, c]]
    P = np.array(P)
    col = TM.lin((40, 56, 26)) * np.exp(rng.normal(0, 0.06, (len(P), 3)))
    ao = np.clip(ao_fn(P) * 0.55, 0.08, 1)
    # (its season: a hawthorn's turn, and it never falls, so in winter the hedge is a dense brown heart of twigs)
    mesh.add(P, np.array(Tr), np.concatenate([col, ao[:, None]], 1), np.stack([np.ones(len(P)), np.full(len(P), 0.05)], 1),
             None, None, np.tile([0.6, 0.0], (len(P), 1)))


def hedge(seed):
    rng = np.random.default_rng(seed)
    height = rng.uniform(1.55, 1.9)
    width = rng.uniform(1.15, 1.5)
    A = envelope(rng, height, width, 7000)
    # Stems: planted in a staggered double row, 30-50 cm apart, a few missing.
    stems = []
    x = -LENGTH / 2 + rng.uniform(0.05, 0.3)
    row = 0
    while x < LENGTH / 2:
        if rng.uniform() > 0.12:
            stems.append((x, (0.14 if row % 2 else -0.14) * width + rng.normal(0, 0.04)))
        x += rng.uniform(0.3, 0.5); row += 1
    N, parent = colonise(rng, A, stems)
    r, kids = TM.radii(N, parent, 0.003, 0.022)
    # (TM.radii scales by the first stem alone: bring the thickest of them all to 3 cm)
    r = 0.003 + (r - 0.003) * 0.027 / max(r.max() - 0.003, 1e-6)
    # Which shrub each stem is: runs of one kind along the hedge, as they were planted and spread.
    names = list(SHRUBS)
    shares = np.array([SHRUBS[k][2] for k in names])
    kind_of_root = {}
    current = rng.choice(len(names), p=shares / shares.sum())
    for i in range(len(N)):
        if parent[i] < 0:
            if rng.uniform() < 0.35:
                current = rng.choice(len(names), p=shares / shares.sum())
            kind_of_root[i] = current
    root = np.zeros(len(N), int)
    for i in range(len(N)):
        root[i] = i if parent[i] < 0 else root[parent[i]]
    kind = np.array([kind_of_root[root[i]] for i in range(len(N))])
    # Wind: stiff at the foot, free at the outer twigs and the shoots.
    wind = np.clip(N[:, 1] / (height + 0.5), 0, 1) ** 1.5 * 0.6 + 0.1

    # How much sky a point sees: deep inside the hedge it's dark, the top and the face are lit.
    def ao_fn(P):
        y = np.clip(P[:, 1] / height, 0, 1.4)
        hw = width / 2 * (1 - 0.32 * np.clip(y, 0, 1) ** 2)
        face = np.clip(1 - (hw - np.abs(P[:, 2])) / 0.35, 0, 1)
        return np.clip(0.18 + 0.55 * face ** 1.5 + 0.3 * np.clip(y - 0.6, 0, 1), 0.12, 1.0)

    mesh = TM.Mesh()
    tips = [i for i in range(len(N)) if parent[i] >= 0 and not kids[i]]
    leaf_nodes = set()
    for t in tips:
        k = t
        for _ in range(3):
            leaf_nodes.add(k)
            k = parent[k]
            if k < 0 or parent[k] < 0 or r[k] > 0.012:
                break
    for i in sorted(leaf_nodes):
        if N[i][1] < 0.15:
            continue
        name = names[kind[i]]
        sp = {"leaf": SHRUBS[name][0], "season": SHRUBS[name][3]}
        d = N[i] - N[parent[i]]
        d /= max(np.linalg.norm(d), 1e-9)
        out = np.array([0.0, 0.5 if N[i][1] > height * 0.85 else 0.0, np.sign(N[i][2]) or 1.0])
        out = out / np.linalg.norm(out) * 0.6 + d * 0.4
        # (fewer leaves deep in, where no light gets)
        if abs(N[i][2]) < width * 0.22 and N[i][1] < height * 0.8 and rng.uniform() < 0.6:
            continue
        TM.leaf_cluster(mesh, N[i], out, sp, rng, float(min(1.0, wind[i] + 0.2)), ao_fn)
    # The hedge's heart: a dark, lumpy core 20-30 cm in from the cut face, so it reads as a solid,
    # dense hedge (light gets through a real one only at the foot), not a row of see-through bushes.
    core(mesh, rng, height, width, ao_fn)
    # The wood: each shrub kind's bark on its own stems.
    for kk, name in enumerate(names):
        sel = kind == kk
        if not sel.any():
            continue
        rr = np.where(sel & (parent >= 0), r, 0.0)
        TM.tubes(mesh, N, parent, rr, kids, SHRUBS[name][1], rng, wind, ao_fn, min_r=0.007, flare=False)
    return mesh, {"height": round(height, 2), "width": round(width, 2), "stems": len(stems), "nodes": int(len(N))}


def main():
    os.makedirs(OUT, exist_ok=True)
    info = {}
    for k in range(VARIANTS):
        mesh, meta = hedge(5100 + 71 * k)
        name = "SM_Tree_hedge_%d" % k
        P, T = mesh.write(os.path.join(OUT, name + ".glb"), name, write_glb)
        meta["triangles"] = int(len(T))
        info[name] = meta
        print(name, meta, flush=True)
    json.dump({"_about": "Tools/make_hedges.py: Ride UK's hedge lengths (4.2 m, x along, planted every 3.9 m).", "models": info},
              open(os.path.join(OUT, "hedges.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
