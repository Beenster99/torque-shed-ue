#!/usr/bin/env python3
"""Ride UK's garden planting as our own models: roses, shrubs, flower clumps, low cover, small garden
trees and clipped hedges, as real geometry for Nanite.

    python Tools/make_gardens.py        (writes Data/RideUK/Gardens/SM_Tree_garden_<kind>_<n>.glb)

beenster, Nov 20 2026: "please consider building a small library of bushes, hedges for gardens". The
villages' and towns' gardens (World/WorldRideGardens.cpp) were planted only with scanned Megaplants,
which were dropped the same day; this is the library that takes their place, made the way our trees
are (Tools/tree_models.py): branches grown by space colonisation, opaque little leaves on the twig ends,
flowers as coloured leaves that are the first to fall. Clipped forms (box balls, hedges) are leaves laid
over the clipped shape's face around a dark, solid heart, so they read as dense and neatly cut.

Every model carries the trees' season data, so the gardens follow the ride's date with the trees
(M_Tree): roses, hydrangeas and flowers die back in autumn, laurel, box, privet, holly, heathers and
conifers stay green, and a beech hedge keeps its brown leaves through winter.

Kinds (World/WorldRideGardens.cpp plants them; the game picks one model a plant, one a house's hedge):
    garden_rose     rose bushes, about a metre: red, pink, white, yellow
    garden_shrub    laurel, hydrangea, a flowering shrub, a dwarf conifer, a clipped box ball
    garden_flowers  border clumps: lavender, mixed cottage flowers, red and orange, white and blue
    garden_low      low cover: heather, hebe, a creeping evergreen
    garden_tree     small garden trees: cherry, apple, Japanese maple, a young silver birch
    garden_hedge    1.5 m lengths of clipped front hedge: privet, beech, laurel, box, leylandii

Frames and vertex data as the trees' (Tools/tree_models.py): metres, y up, the foot at y 0 (stems 0.3 m
below); a hedge along x, -0.75 to 0.75, closed at both ends. Tools/ue_build_rideuk.py (garden_kit) imports them.

    python Tools/make_gardens.py --hedges   (only the hedge lengths made again)
"""
import json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tree_models as TM                    # noqa: E402
from export_buttertubs import write_glb     # noqa: E402

OUT = os.path.join(os.path.dirname(HERE), "Data", "RideUK", "Gardens")
HEDGE_LEN = 1.5
END_SHARE = 1.0    # (a clipped hedge's end faces: leaves as dense as its sides')


def shrub_spec(height, crown, leaf, bark, season, stems=5, flowers=None, attract=2500, step=None, sparse=0.2,
               up=0.2, droop=0.04, lean=0.0, jitter=0.14, weep=0.0):
    """A shrub or small tree for tree_models.broadleaf: its numbers scaled to its size."""
    sp = dict(height=height, crown=crown, trunk_r=0.012 if stems > 1 else 0.022, attract=attract,
              step=step or max(0.06, height * 0.045), infl=max(0.25, height * 0.2), kill=max(0.08, height * 0.06),
              up=up, droop=droop, leaf=leaf, bark=bark, lean=lean, sparse=sparse, far_blobs=6, season=season,
              stems=stems, jitter=jitter, near_only=True, lobes=5)
    if weep:
        sp["weep"] = weep
    if flowers:
        sp["flowers"] = flowers
    return sp


# --- Grown plants ----------------------------------------------------------------------------------

ROSE_COLOURS = {"red": [(150, 16, 24), (120, 10, 20)], "pink": [(220, 120, 150), (200, 90, 130)],
                "white": [(235, 232, 220), (225, 225, 210)], "yellow": [(235, 196, 60), (225, 170, 40)]}

GROWN = {
    "garden_rose": [shrub_spec(1.1, (0.15, 0.55, 0.42, 1.2), ("oval", 0.035, 7, (46, 74, 34), 0.08), ((70, 66, 46), 0.08),
                               (1, 0.85, 0.0), stems=5, flowers=("round", 0.04, 3, ROSE_COLOURS[c], 0.55))
                    for c in ("red", "pink", "white", "yellow")],
    "garden_shrub": [
        # laurel: big glossy evergreen leaves
        shrub_spec(2.0, (0.08, 0.5, 0.42, 1.1), ("oval", 0.11, 6, (30, 58, 26), 0.06), ((80, 74, 62), 0.07), (2, 0.0, 0.0), stems=4),
        # hydrangea: mop heads, blue or pink
        shrub_spec(1.3, (0.1, 0.55, 0.5, 1.4), ("oval", 0.08, 5, (62, 96, 44), 0.07), ((96, 84, 66), 0.07), (1, 0.15, 0.0), stems=7,
                   flowers=("round", 0.05, 9, [(90, 120, 200), (200, 110, 160), (150, 140, 210)], 0.45)),
        # a flowering shrub (weigela, forsythia sort): arching, small leaves, a scatter of flowers
        shrub_spec(1.8, (0.1, 0.5, 0.5, 1.0), ("diamond", 0.05, 9, (70, 102, 42), 0.08), ((100, 88, 70), 0.07), (1, 0.4, 0.0),
                   stems=8, weep=0.25, flowers=("round", 0.03, 3, [(230, 150, 170), (240, 210, 70)], 0.3)),
        # a dwarf conifer: a dense green column
        shrub_spec(1.9, (0.05, 0.45, 0.17, 0.6), ("tuft", 0.06, 6, (40, 66, 44), 0.05), ((90, 70, 56), 0.07), (2, 0.0, 0.0),
                   stems=1, sparse=0.0, up=0.45, attract=3000),
    ],
    "garden_flowers": [
        # lavender: grey-green, purple spikes
        shrub_spec(0.55, (0.05, 0.5, 0.6, 1.6), ("lance", 0.03, 6, (110, 128, 104), 0.06), ((96, 90, 76), 0.06), (2, 0.0, 0.0),
                   stems=9, up=0.5, flowers=("lance", 0.03, 5, [(120, 90, 190), (100, 80, 170)], 0.8)),
        # cottage flowers: mixed colours
        shrub_spec(0.6, (0.05, 0.5, 0.6, 1.5), ("oval", 0.04, 6, (70, 110, 46), 0.08), ((80, 100, 50), 0.06), (1, 0.1, 0.0),
                   stems=10, up=0.45, flowers=("round", 0.035, 4, [(230, 80, 120), (250, 200, 60), (240, 240, 230), (160, 100, 210)], 0.85)),
        # red and orange (geraniums, crocosmia)
        shrub_spec(0.5, (0.05, 0.5, 0.6, 1.5), ("palmate", 0.045, 5, (60, 100, 40), 0.08), ((70, 90, 46), 0.06), (1, 0.2, 0.0),
                   stems=9, up=0.45, flowers=("round", 0.035, 4, [(220, 40, 30), (240, 120, 30)], 0.8)),
        # white and blue (daisies, salvia)
        shrub_spec(0.55, (0.05, 0.5, 0.6, 1.5), ("lance", 0.035, 6, (66, 108, 48), 0.08), ((76, 96, 50), 0.06), (1, 0.05, 0.0),
                   stems=10, up=0.5, flowers=("round", 0.035, 4, [(245, 245, 235), (80, 100, 210)], 0.85)),
    ],
    "garden_low": [
        # heather: low, fine, evergreen, a little purple
        shrub_spec(0.35, (0.02, 0.5, 0.9, 2.0), ("lance", 0.02, 8, (64, 82, 46), 0.08), ((80, 66, 52), 0.07), (2, 0.0, 0.0),
                   stems=10, up=0.15, flowers=("lance", 0.018, 6, [(170, 90, 150), (150, 80, 140)], 0.5)),
        # hebe: neat evergreen dome
        shrub_spec(0.45, (0.02, 0.5, 0.75, 1.8), ("oval", 0.03, 9, (66, 104, 60), 0.06), ((90, 80, 62), 0.07), (2, 0.0, 0.0),
                   stems=9, up=0.2),
        # a creeping evergreen (cotoneaster, ivy sort)
        shrub_spec(0.3, (0.02, 0.5, 1.1, 2.4), ("round", 0.03, 8, (36, 64, 30), 0.07), ((70, 58, 48), 0.07), (2, 0.0, 0.0),
                   stems=10, up=0.02, flowers=("round", 0.015, 3, [(190, 30, 30)], 0.2)),
    ],
    "garden_tree": [
        # flowering cherry: red in autumn
        shrub_spec(5.0, (0.32, 0.6, 0.42, 1.5), ("oval", 0.07, 8, (70, 98, 44), 0.08), ((96, 56, 50), 0.07), (1, 0.62, 0.0),
                   stems=1, attract=4000, up=0.1),
        # apple: low, spreading, crooked
        shrub_spec(4.0, (0.3, 0.55, 0.5, 1.6), ("oval", 0.065, 8, (74, 104, 48), 0.08), ((92, 82, 70), 0.07), (1, 0.15, 0.0),
                   stems=1, attract=4000, up=0.02, jitter=0.24, flowers=("round", 0.035, 2, [(190, 40, 30), (150, 170, 50)], 0.15)),
        # Japanese maple: red leaves, a layered spread
        shrub_spec(3.0, (0.18, 0.55, 0.55, 1.7), ("palmate", 0.05, 8, (130, 36, 30), 0.08), ((84, 70, 64), 0.07), (1, 0.7, 0.0),
                   stems=3, attract=3500, up=0.0),
        # a young silver birch
        dict(TM.SPECIES["birch"], height=6.0, attract=3500, near_only=True),
    ],
}


def grown(kind, k, spec):
    near, _, info = TM.broadleaf(spec, 4000 + 91 * k + len(kind), k, len(GROWN[kind]))
    return near, info


# --- Clipped forms ---------------------------------------------------------------------------------

HEDGES = [
    # (name, leaf (shape, size m, sRGB, spread), season, height, width at the top, at the foot)
    ("privet", ("oval", 0.04, (40, 66, 30), 0.07), (2, 0.0, 0.0), 1.15, 0.5, 0.6),
    ("beech", ("oval", 0.05, (74, 104, 40), 0.07), (1, 0.5, 0.88), 1.25, 0.5, 0.6),
    ("laurel", ("oval", 0.09, (30, 58, 26), 0.06), (2, 0.0, 0.0), 1.35, 0.7, 0.8),
    ("box", ("oval", 0.03, (44, 72, 34), 0.06), (2, 0.0, 0.0), 0.75, 0.42, 0.48),
    ("leylandii", ("tuft", 0.045, (34, 58, 42), 0.05), (2, 0.0, 0.0), 1.7, 0.6, 0.75),
]


def clipped(rng, surface, inside, leaf, season, area, core):
    """Leaves laid over a clipped shape's face (surface(n) -> points, outward normals), facing out and
    tilted every way, so it reads as a dense cut face; a dark lumpy heart (core: positions, triangles)
    just inside it so no light shows through; a few stems at the foot."""
    shape, size, col, spread = leaf
    size *= 1.45
    n = int(area / (size * size * 0.2))
    P0, Nrm = surface(n)
    mesh = TM.Mesh()
    base = TM.lin(col)
    sp = {"leaf": (shape, size, 1, col, spread), "season": season}
    if shape == "tuft":
        # (a conifer hedge: little brushes poking out of the face)
        for p, nr in zip(P0[::3], Nrm[::3]):
            TM.leaf_cluster(mesh, p - nr * size * 0.6, nr, sp, rng, 0.3, inside)
    else:
        outline, tris = TM.LEAF_SHAPES[shape]
        allP, allT, allC, allPiv, allR = [], [], [], [], []
        nv = 0
        for p, nr in zip(P0, Nrm):
            d = rng.normal(0, 1, 3); d -= nr * (d @ nr); d /= max(np.linalg.norm(d), 1e-9)
            nn = nr + rng.normal(0, 0.45, 3); nn /= np.linalg.norm(nn)
            ld = d - nn * (d @ nn); ld /= max(np.linalg.norm(ld), 1e-9)
            ls = np.cross(nn, ld)
            ln = size * rng.uniform(0.75, 1.2)
            q = p - nr * rng.uniform(0, size * 1.2) - ld * ln * 0.5
            Pl = q[None] + outline[:, :1] * ld[None] * ln + outline[:, 1:] * ls[None] * ln
            allP.append(Pl); allT.append(np.array(tris) + nv); nv += len(Pl)
            allPiv.append(np.repeat(Pl.mean(0)[None], len(Pl), 0))
            allR.append(np.full(len(Pl), rng.uniform(0.02, 1.0)))
            c = base * np.exp(rng.normal(0, spread, 3)) * rng.uniform(0.85, 1.12)
            allC.append(np.repeat(c[None], len(Pl), 0))
        P = np.concatenate(allP)
        kind, uv1, uv2, uv3 = TM._season_uvs(P, np.concatenate(allPiv), np.concatenate(allR), sp)
        mesh.add(P, np.concatenate(allT), np.concatenate([np.concatenate(allC), inside(P)[:, None]], 1),
                 np.stack([np.full(len(P), kind), np.full(len(P), 0.3)], 1), uv1, uv2, uv3)
    # The heart: dark leaf-coloured, a leaf that never falls (its rank 0) and turns with the rest.
    cP, cT = core
    kind, hue, keep = season
    ccol = base * 0.7 * np.exp(rng.normal(0, 0.05, (len(cP), 3)))
    mesh.add(cP, cT, np.concatenate([ccol, np.full((len(cP), 1), 0.3)], 1), np.stack([np.full(len(cP), float(kind)), np.full(len(cP), 0.05)], 1),
             None, None, np.tile([hue, keep], (len(cP), 1)))
    return mesh


def box_prism(hx, h, w_top, w_foot, inset, nx=10, ny=6, cap=True):
    """A clipped hedge's heart: its section (narrower at the top, rounded shoulders) inset by `inset`,
    as a closed grid along x from -hx to hx."""
    P, T = [], []
    ring = []
    for a in np.linspace(0, 1, 9):
        # (round the section: up one side from the foot, over the top, down the other)
        if a < 0.35:
            t = a / 0.35; ring.append((-(w_foot + (w_top - w_foot) * t) / 2 + inset, 0.05 + (h - 0.12) * t - inset * 0.3))
        elif a < 0.65:
            t = (a - 0.35) / 0.3; ring.append(((-0.5 + t) * (w_top - 2 * inset), h - inset - 0.04 * math.sin(t * math.pi) * -1))
        else:
            t = (a - 0.65) / 0.35; ring.append(((w_top + (w_foot - w_top) * t) / 2 - inset, h - 0.12 - (h - 0.17) * t - inset * 0.3))
    xs = np.linspace(-hx, hx, nx)
    m = len(ring)
    for x in xs:
        for (z, y) in ring:
            P.append([x, y, z])
    for i in range(nx - 1):
        for j in range(m - 1):
            a = i * m + j; b = a + 1; c = a + m; d = c + 1
            T += [[a, b, c], [b, d, c]]
    # ([Nov 21] beenster, build 166 at Gigha: "ALL hedges and walls need ends": the heart closed at both
    # ends too, a fan from the section's middle, so a run's last length shows no hollow inside)
    if cap:
        cz = sum(z for z, _ in ring) / m; cy = sum(y for _, y in ring) / m
        for i, sgn in ((0, -1), (nx - 1, 1)):
            c = len(P); P.append([xs[i], cy, cz])
            for j in range(m - 1):
                a = i * m + j; b = a + 1
                T += [[c, b, a]] if sgn < 0 else [[c, a, b]]
            # (and the foot, from the last ring point back to the first)
            a = i * m + m - 1; b = i * m
            T += [[c, b, a]] if sgn < 0 else [[c, a, b]]
    return np.array(P), np.array(T)


def hedge_length(rng, name, leaf, season, h, w_top, w_foot):
    h *= rng.uniform(0.92, 1.08)
    hx = HEDGE_LEN / 2

    def surface(n):
        # The cut face: two sides (a slight A), the flat top, and ([Nov 21] beenster at Gigha, build 166: the
        # hedges' ends were open, the dark heart showing) both ends clipped square too, as a real clipped
        # hedge's are; where two lengths meet the ends sit inside each other, out of sight.
        side_a = 2 * hx * h; top_a = 2 * hx * w_top; end_a = END_SHARE * h * (w_top + w_foot) / 2
        k_side = int(n * side_a / (2 * side_a + top_a + 2 * end_a))
        k_end = int(n * end_a / (2 * side_a + top_a + 2 * end_a))
        pts, nrm = [], []
        for sgn in (-1, 1):
            x = rng.uniform(-hx, hx, k_side); y = rng.uniform(0.04, h, k_side) ** 1.0
            w = (w_foot + (w_top - w_foot) * (y / h)) / 2
            pts.append(np.stack([x, y, sgn * w], 1))
            nn = np.zeros((k_side, 3)); nn[:, 2] = sgn; nn[:, 1] = 0.08
            nrm.append(nn)
        kt = n - 2 * k_side - 2 * k_end
        x = rng.uniform(-hx, hx, kt); z = rng.uniform(-w_top / 2, w_top / 2, kt)
        y = h + 0.03 * np.sin(x * 3.1 + rng.uniform(0, 6)) - 0.05 * (2 * z / w_top) ** 4
        pts.append(np.stack([x, y, z], 1)); nn = np.zeros((kt, 3)); nn[:, 1] = 1; nrm.append(nn)
        for sgn in (-1, 1):
            # (the end: across the section, kept a leaf in from its edges so nothing pokes out of a joint)
            y = rng.uniform(0.04, h - 0.03, k_end)
            w = (w_foot + (w_top - w_foot) * (y / h)) / 2 - 0.03
            z = rng.uniform(-1, 1, k_end) * w
            pts.append(np.stack([np.full(k_end, sgn * hx), y, z], 1))
            nn = np.zeros((k_end, 3)); nn[:, 0] = sgn; nn[:, 1] = 0.08
            nrm.append(nn)
        P = np.concatenate(pts); Nn = np.concatenate(nrm)
        return P, Nn / np.linalg.norm(Nn, axis=1, keepdims=True)

    def inside(P):
        w = (w_foot + (w_top - w_foot) * np.clip(P[:, 1] / h, 0, 1)) / 2
        face = np.clip(1 - np.minimum(w - np.abs(P[:, 2]), hx - np.abs(P[:, 0])) / 0.2, 0, 1)
        return np.clip(0.2 + 0.5 * face + 0.3 * np.clip(P[:, 1] / h - 0.8, 0, 1) * 5, 0.12, 1.0)

    area = 2 * HEDGE_LEN * h + HEDGE_LEN * w_top + 2 * END_SHARE * h * (w_top + w_foot) / 2
    inset = 0.08 if leaf[1] < 0.06 else 0.14
    core = box_prism(hx - inset, h, w_top, w_foot, inset)   # ([Nov 21] its ends in from the cut end as its sides are)
    mesh = clipped(rng, surface, inside, leaf, season, area, core)
    # A few stems at the foot, seen under it.
    for x in np.arange(-hx + 0.15, hx, 0.35):
        p0 = np.array([x + rng.normal(0, 0.04), -0.3, rng.normal(0, 0.05)])
        p1 = p0 + np.array([rng.normal(0, 0.04), 0.55, rng.normal(0, 0.04)])
        N = np.array([p0, p1]); par = np.array([-1, 0])
        r = np.array([0.025, 0.02])
        TM.tubes(mesh, N, par, r, TM._kids(par), ((70, 60, 48), 0.06), rng, np.zeros(2), lambda P: np.full(len(P), 0.25),
                 min_r=0.005, flare=False)
    return mesh, {"height": round(h, 2), "kind": name}


def box_ball(rng):
    """A clipped box ball, about 0.8 m, on a short stem."""
    R = rng.uniform(0.36, 0.46); cy = R + 0.08

    def surface(n):
        d = rng.normal(0, 1, (n, 3)); d /= np.linalg.norm(d, axis=1, keepdims=True)
        d = d[d[:, 1] > -0.75]
        return np.array([0, cy, 0]) + d * R * (1 + 0.03 * np.sin(d[:, 0:1] * 9)), d

    def inside(P):
        return np.clip(0.25 + 0.6 * np.clip((P[:, 1] - cy) / R + 0.6, 0, 1.2), 0.15, 1.0)

    cP, cT = TM.lumpy_sphere(np.array([0, cy, 0]), R * 0.72, rng, 2)
    return clipped(rng, surface, inside, ("oval", 0.03, (44, 72, 34), 0.06), (2, 0.0, 0.0), 4 * math.pi * R * R * 0.9, (cP, cT))


def main():
    os.makedirs(OUT, exist_ok=True)
    # (--hedges: only the clipped hedge lengths made again, the other plants and gardens.json's rows for them kept)
    only_hedges = "--hedges" in sys.argv
    for f in os.listdir(OUT):
        if f.endswith(".glb") and (not only_hedges or f.startswith("SM_Tree_garden_hedge_")):
            os.remove(os.path.join(OUT, f))
    info = {}
    if only_hedges:
        info = {k: v for k, v in json.load(open(os.path.join(OUT, "gardens.json")))["models"].items() if not k.startswith("SM_Tree_garden_hedge_")}

    def save(name, mesh, extra):
        P, T = mesh.write(os.path.join(OUT, name + ".glb"), name, write_glb)
        info[name] = dict(extra, triangles=int(len(T)), height=round(float(P[:, 1].max()), 2))
        print("%-28s %6d triangles, %.2f m" % (name, len(T), P[:, 1].max()), flush=True)

    for kind, specs in (GROWN.items() if not only_hedges else []):
        for k, spec in enumerate(specs):
            near, meta = grown(kind, k, spec)
            save("SM_Tree_%s_%d" % (kind, k), near, {"kind": kind})
    # The clipped box ball joins the shrubs.
    if not only_hedges:
        rng = np.random.default_rng(77)
        save("SM_Tree_garden_shrub_%d" % len(GROWN["garden_shrub"]), box_ball(rng), {"kind": "garden_shrub", "form": "box ball"})
    for k, (name, leaf, season, h, wt, wf) in enumerate(HEDGES):
        rng = np.random.default_rng(500 + k)
        mesh, meta = hedge_length(rng, name, leaf, season, h, wt, wf)
        save("SM_Tree_garden_hedge_%d" % k, mesh, meta)
    json.dump({"about": "Garden plants made by Tools/make_gardens.py (see its docstring).", "models": info},
              open(os.path.join(OUT, "gardens.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
