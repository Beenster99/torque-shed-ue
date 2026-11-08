#!/usr/bin/env python3
"""The becks and gills on the ride's own land: the map's streams, as a stony bed with water running down it.

    python Tools/make_becks.py --torque-shed ../torque-shed     (then python Tools/make_foliage.py)

The Godot region's streams.json (OpenStreetMap's becks, simplified to within 4 m) are moved onto the floor
of the gill the 2 m land actually has (the lowest ground within a few metres across, smoothed along), and
built on the land as drawn: a bed of wet stones and gravel a little wider than the water, cobbles and
boulders along it (more at the edges and on steep stretches, where the water has moved them), and the
water itself. Bigger becks (the longer lines on the map, Thwaite Beck) are wider. The road crosses them
on culverts: a headwall either side, with a parapet and upright coping stones, the bed running in under
it and the water stopping a metre and a half short of the tarmac. Where the map stops a beck short of the
road (its culvert isn't mapped) it is carried on to it, and past it when nothing on the map carries it on.

Writes Data/Buttertubs/Becks/:

    bed_NN.glb      the bed, vertex-coloured for M_Stone (R brightness, G bare stone, B lichen, A moss), UV0
                    metres of surface
    SM_BeckStone_N.glb, instances.bin, becks.json
                    the cobbles and boulders: six stone models and where each stone lies (the trees' format),
                    planted by a TorqueFoliage actor, drawn out to 600 m
    wall_00.glb     the culverts' headwalls, for M_Masonry; cope_00.glb their coping stones, for M_Stone
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
BED_GAP = 0.7        # the bed runs in under the culvert's headwall, this far from the road's edge
WATER_GAP = 1.6      # the water stops this far short of it
WALL_IN = 0.35       # the headwall's inner face, from the road's edge (m)
WALL_T = 0.45        # its thickness at the top
BATTER = 0.15        # and how much thicker at its foot (the outer face leans in)
PARAPET = 0.75       # its top over the road, under the coping
CROSS_JOIN = 12.0    # becks meeting the road within this distance (along it) share one culvert
END_REACH = 12.0     # a beck the map stops this close to the road is carried on to it
BEYOND = 9.0         # and this far past its edge when nothing carries it on beyond
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


def cross2(a, b):
    return float(a[0] * b[1] - a[1] * b[0])


def box(m, c, t, u, n, size, col, uv_scale=1.0):
    """A box centred at c with unit axes t (length), u (up), n (across) and full sizes (lt, lu, ln),
    each face with its own vertices (flat shading)."""
    h = np.asarray(size) / 2
    axes = [np.asarray(t), np.asarray(u), np.asarray(n)]
    for k in range(3):
        for sgn in (-1, 1):
            a, b = axes[(k + 1) % 3], axes[(k + 2) % 3]
            ha, hb_ = h[(k + 1) % 3], h[(k + 2) % 3]
            f = c + axes[k] * h[k] * sgn
            P = np.array([f - a * ha - b * hb_, f + a * ha - b * hb_, f + a * ha + b * hb_, f - a * ha + b * hb_])
            T = np.array([[0, 1, 2], [0, 2, 3]]) if sgn > 0 else np.array([[0, 2, 1], [0, 3, 2]])
            UV = np.array([[0, 0], [2 * ha, 0], [2 * ha, 2 * hb_], [0, 2 * hb_]]) * uv_scale
            m.add(P, T, col, UV)


def strip(m, A, B, col, v0=0.0):
    """A quad strip between point rows A and B (n, 3); UV u along A, v from A to B, in metres."""
    n = len(A)
    P = np.concatenate([A, B])
    i = np.arange(n - 1)
    T = np.concatenate([np.stack([i, i + n, i + 1], 1), np.stack([i + 1, i + n, i + n + 1], 1)])
    along = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(A, axis=0), axis=1))])
    v = np.linalg.norm(B - A, axis=1)
    UV = np.concatenate([np.stack([along, np.full(n, v0)], 1), np.stack([along, v0 + v], 1)])
    m.add(P, T, col, UV)


class Road:
    """The ride's road line: points by distance along it, and its height."""
    def __init__(self, route):
        self.L = np.array(route["line_sim"], float)
        self.s = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(self.L[:, [0, 2]], axis=0), axis=1))])
        self.xz = self.L[:, [0, 2]]
        self.half = route["road_half_width_m"]

    def at(self, s):
        s = np.asarray(s, float)
        p = np.stack([np.interp(s, self.s, self.L[:, k]) for k in range(3)], -1)
        q = np.stack([np.interp(s + 0.5, self.s, self.L[:, k]) for k in range(3)], -1) - \
            np.stack([np.interp(s - 0.5, self.s, self.L[:, k]) for k in range(3)], -1)
        t = q / np.maximum(np.linalg.norm(q, axis=-1, keepdims=True), 1e-9)
        return p, t

    def nearest(self, x, z):
        i = int(np.argmin((self.xz[:, 0] - x) ** 2 + (self.xz[:, 1] - z) ** 2))
        best = (1e18, 0.0)
        for j in (i - 1, i):
            if j < 0 or j + 1 >= len(self.xz):
                continue
            a, b = self.xz[j], self.xz[j + 1]
            ab = b - a
            f = float(np.clip(np.dot([x - a[0], z - a[1]], ab) / max(np.dot(ab, ab), 1e-12), 0, 1))
            p = a + ab * f
            d = (p[0] - x) ** 2 + (p[1] - z) ** 2
            if d < best[0]:
                best = (d, self.s[j] + f * (self.s[j + 1] - self.s[j]))
        return best[1]


def read_streams(root, to_ride):
    out = []
    for row in json.load(open(os.path.join(root, "streams.json")))["streams"]:
        lat, lon = row[1], row[2]
        pts = [to_ride(lat, lon)]
        for k in range(3, len(row) - 1, 2):
            lat += row[k]; lon += row[k + 1]
            pts.append(to_ride(lat, lon))
        out.append([row[0], np.array(pts, float)])
    return out


def densify(L, step):
    seg = np.linalg.norm(np.diff(L, axis=0), axis=1)
    s = np.concatenate([[0], np.cumsum(seg)])
    t = np.arange(0, s[-1], step)
    return np.stack([np.interp(t, s, L[:, 0]), np.interp(t, s, L[:, 1])], 1), s[-1]


def find_crossings(streams, road, land):
    """Where the becks pass under the road. A beck the map crosses keeps its line; a beck the map stops
    short of the road (the culvert isn't mapped) is carried on to it, joining a crossing already found
    nearby, and on beyond it when nothing on the far side carries it on. Returns the crossings as
    [distance along the road, the beck's width (m), the beck's direction (x, z)]."""
    cross = []

    def near_crossing(s):
        for c in cross:
            if abs(c[0] - s) < CROSS_JOIN:
                return c
        return None

    def width(kind, length):
        return 9.0 if kind == 0 else float(np.clip(0.7 + length / 1800.0, 0.7, 3.2))

    # The becks the map takes across the road.
    for kind, L in streams:
        Q, length = densify(L, 1.0)
        if len(Q) < 2 or not land.in_tiles(Q[:, 0], Q[:, 1]).any():
            continue
        d = line_dist(Q[:, 0], Q[:, 1], [road.xz], cell=30.0)
        for i in np.flatnonzero((d[:-1] < 1.0) & (d[:-1] <= d[1:]) & (np.r_[True, d[1:-1] <= d[:-2]])):
            if not land.in_tiles(Q[i:i + 1, 0], Q[i:i + 1, 1])[0]:
                continue
            s = road.nearest(*Q[i])
            dirn = Q[min(i + 2, len(Q) - 1)] - Q[max(i - 2, 0)]
            c = near_crossing(s)
            if c is None:
                cross.append([s, width(kind, length), dirn / max(np.linalg.norm(dirn), 1e-9), 0])
            else:
                c[1] = max(c[1], width(kind, length))
    # The becks the map stops short of it.
    pending = []
    for k, (kind, L) in enumerate(streams):
        if len(L) < 2:
            continue
        for end in (0, -1):
            p = L[end]
            if not land.in_tiles(p[None, 0], p[None, 1])[0]:
                continue
            dd = float(line_dist(p[None, 0], p[None, 1], [road.xz], cell=30.0)[0])
            if not (1.0 <= dd < END_REACH):
                continue
            # Only a beck heading for the road: one that runs along beside it (Cliff Beck at 1 km) isn't.
            Q, _ = densify(L if end == -1 else L[::-1], 1.0)
            back = Q[-16:-15] if len(Q) > 16 else Q[:1]
            if float(line_dist(back[:, 0], back[:, 1], [road.xz], cell=30.0)[0]) - dd < 3.0:
                continue
            s = road.nearest(*p)
            c = near_crossing(s)
            length = float(np.linalg.norm(np.diff(L, axis=0), axis=1).sum())
            if c is None:
                rp, _ = road.at(s)
                dirn = rp[[0, 2]] - p
                c = [s, width(kind, length), dirn / max(np.linalg.norm(dirn), 1e-9), 0]
                cross.append(c)
            else:
                c[1] = max(c[1], width(kind, length))
            target = road.at(c[0])[0][[0, 2]]
            side = np.sign(cross2(road.at(c[0])[1][[0, 2]], p - target)) or 1.0
            c[3] = c[3] | (1 if side > 0 else 2)
            pending.append((k, end, c, side))
    for k, end, c, side in pending:
        kind, L = streams[k]
        rp, rt = road.at(c[0])
        target = rp[[0, 2]]
        add = [target]
        if c[3] != 3:
            # Nothing on the far side: carry the beck on down past the road, square to it.
            n = np.array([-rt[2], rt[0]]); n /= max(np.linalg.norm(n), 1e-9)
            if np.sign(cross2(rt[[0, 2]], n)) == side:
                n = -n
            add.append(target + n * (road.half + BEYOND))
            c[3] = 3
        add = np.array(add)
        streams[k][1] = np.concatenate([L, add]) if end == -1 else np.concatenate([add[::-1], L])
    cross.sort(key=lambda c: c[0])
    return cross


def read_waterfalls(root, to_ride):
    """The map's waterfalls (features.json points of kind 'waterfall'): [(name, (x, z))] in the ride frame."""
    f = json.load(open(os.path.join(root, "features.json")))
    kinds, names = f["kinds"], f["names"]
    out = []
    for p in f["points"]:
        if kinds[p[0]] == "waterfall":
            out.append((names[p[6]] if p[6] else "", np.array(to_ride(p[1], p[2]), float)))
    return out


def cascade(q, sd, dq, gy, hb, hw, rng, rock, water):
    """A waterfall where the map has one: the beck's steep stretches nearby (on the 2 m land, the falls
    are smoothed into a slope) rebuilt as limestone steps across the bed, each a shelf of rock with
    still water on it that pours over its lip as a white sheet to the next. Returns which points of the
    beck it covers (its own water and stones are left off there)."""
    n = len(q)
    covered = np.zeros(n, bool)
    g = smooth(gy, 1.5)
    slope = -np.gradient(g) / STEP
    steep = slope > 0.12
    for a, b in ((r[0], r[-1] + 1) for r in np.split(np.flatnonzero(steep), np.flatnonzero(np.diff(np.flatnonzero(steep)) > 1) + 1) if len(r)):
        a, b = max(a - 1, 0), min(b + 1, n - 1)
        fall = g[a] - g[b]
        if fall < 0.8 or b - a < 2:
            continue
        steps = int(np.clip(round(fall / 1.3), 1, 6))
        # The lips: where the floor passes each step's height (a ragged share of the fall each).
        cuts = np.sort(rng.uniform(0.2, 0.8, steps - 1)) if steps > 1 else np.array([])
        levels = g[a] - fall * np.concatenate([[0], cuts, [1]])
        lips = [a] + [int(a + np.argmax(g[a:b + 1] <= lv)) for lv in levels[1:-1]] + [b]
        covered[a:b + 1] = True
        for k in range(steps):
            j0, j1 = lips[k], max(lips[k + 1], lips[k] + 1)
            top, below = levels[k], levels[k + 1]
            idx = np.arange(max(j0 - 1, 0), j1 + 1)
            m = len(idx)
            # The shelf: a trapezoid across the bed (top as wide as the bed, sloping into the banks),
            # its top a little rough, run along the beck to its lip, where its face drops to the next.
            across = np.linspace(-1, 1, 7)
            rough_top = top - 0.06 + rng.normal(0, 0.04, (m, 7))
            wt = hb[idx][:, None] * 1.05
            base = np.minimum(gy[idx], below)[:, None] - 0.8
            Pt = np.stack([q[idx, 0, None] + sd[idx, 0, None] * wt * across, rough_top,
                           q[idx, 1, None] + sd[idx, 1, None] * wt * across], -1)
            Pb = np.stack([q[idx, 0, None] + sd[idx, 0, None] * wt * 1.5 * across, np.repeat(base, 7, 1),
                           q[idx, 1, None] + sd[idx, 1, None] * wt * 1.5 * across], -1)
            # The lip: ragged, a little forward here and back there.
            jag = rng.uniform(-0.35, 0.35, 7)
            Pt[-1, :, [0, 2]] += (dq[idx[-1]][:, None] * jag[None]) * 0.8
            col = np.array([0.42, 1.0, 0.25, 0.15])
            # Top (rows across), the two sloping sides, and the lip's face.
            ii = np.arange(m * 7).reshape(m, 7)
            pa, pb, pc, pd = ii[:-1, :-1], ii[:-1, 1:], ii[1:, :-1], ii[1:, 1:]
            T = np.concatenate([np.stack([pa, pc, pb], -1).reshape(-1, 3), np.stack([pb, pc, pd], -1).reshape(-1, 3)])
            PP = Pt.reshape(-1, 3)
            if np.cross(PP[T[0, 1]] - PP[T[0, 0]], PP[T[0, 2]] - PP[T[0, 0]])[1] < 0:
                T = T[:, [0, 2, 1]]
            along = np.arange(m)[:, None].repeat(7, 1) * STEP
            rock.add(PP, T, col, np.stack([along, (across[None] * wt)], -1).reshape(-1, 2))
            for e in (0, -1):
                A, B = Pt[:, e], Pb[:, e]
                if e == 0:
                    A, B = B, A
                strip(rock, A, B, col)
            A, B = Pt[-1], Pb[-1]
            strip(rock, B, A, col) if np.dot(np.cross(A[1] - A[0], B[0] - A[0]), np.r_[dq[idx[-1]][0], 0, dq[idx[-1]][1]]) > 0 \
                else strip(rock, A, B, col)
            # The water on the shelf: still, then quickening to the lip, where it pours over.
            hwk = hw[idx] * 0.95
            Xw = q[idx, None, 0] + sd[idx, None, 0] * hwk[:, None] * np.linspace(-1, 1, 3)[None]
            Zw = q[idx, None, 1] + sd[idx, None, 1] * hwk[:, None] * np.linspace(-1, 1, 3)[None]
            Yw = np.full_like(Xw, top + 0.03)
            Pw = np.stack([Xw, Yw, Zw], -1)
            ii = np.arange(m * 3).reshape(m, 3)
            pa, pb, pc, pd = ii[:-1, :-1], ii[:-1, 1:], ii[1:, :-1], ii[1:, 1:]
            Tw = np.concatenate([np.stack([pa, pc, pb], -1).reshape(-1, 3), np.stack([pb, pc, pd], -1).reshape(-1, 3)])
            PW = Pw.reshape(-1, 3)
            if np.cross(PW[Tw[0, 1]] - PW[Tw[0, 0]], PW[Tw[0, 2]] - PW[Tw[0, 0]])[1] < 0:
                Tw = Tw[:, [0, 2, 1]]
            dist_lip = (m - 1 - np.arange(m)) * STEP
            dist_top = np.arange(m) * STEP
            speed = np.clip(np.maximum(1 - dist_lip / 2.0, 1 - dist_top / 2.5) , 0.15, 1.0)   # foam below the last fall too
            Cw = np.zeros((m, 3, 4)); Cw[..., 0] = speed[:, None]; Cw[..., 3] = 1
            UVw = np.stack([np.linspace(0, 1, 3)[None].repeat(m, 0), dist_top[:, None].repeat(3, 1)], -1)
            water.add(PW, Tw, Cw.reshape(-1, 4), UVw.reshape(-1, 2))
            # The sheet: over the lip, out a little and down to the pool below.
            drop = top - below + 0.1
            t = np.linspace(0, 1, 7)
            lip = Pw[-1]
            fwd = np.r_[dq[idx[-1]][0], 0.0, dq[idx[-1]][1]]
            rows = [lip + fwd[None] * (0.45 * math.sqrt(drop) * math.sqrt(tt) + 0.05) - np.array([0, drop * tt, 0])[None]
                    for tt in t]
            Ps = np.array(rows)          # (7, 3, 3)
            ii = np.arange(7 * 3).reshape(7, 3)
            pa, pb, pc, pd = ii[:-1, :-1], ii[:-1, 1:], ii[1:, :-1], ii[1:, 1:]
            Ts = np.concatenate([np.stack([pa, pc, pb], -1).reshape(-1, 3), np.stack([pb, pc, pd], -1).reshape(-1, 3)])
            PS = Ps.reshape(-1, 3)
            nrm = np.cross(PS[Ts[0, 1]] - PS[Ts[0, 0]], PS[Ts[0, 2]] - PS[Ts[0, 0]])
            if np.dot(nrm, fwd) < 0:
                Ts = Ts[:, [0, 2, 1]]
            Cs = np.zeros((7, 3, 4)); Cs[..., 0] = 1.0; Cs[..., 3] = 1
            # UV y runs 4x the metres, so the ripples fall faster than they flow.
            UVs = np.stack([np.linspace(0, 1, 3)[None].repeat(7, 0), (dist_top[-1] + 4 * drop * t)[:, None].repeat(3, 1)], -1)
            water.add(PS, Ts, Cs.reshape(-1, 4), UVs.reshape(-1, 2))
    return covered


def headwalls(cross, road, ground, rng):
    """At each crossing, a culvert headwall either side of the road: a battered masonry wall from the
    beck's bed to a parapet a little under a metre over the road, capped with upright coping stones
    (Dales style), with the culvert's dark mouth in its outer face where the beck runs."""
    wall, cope = TM.Mesh(), TM.Mesh()
    for s0, w, dirn, _ in cross:
        _, rt = road.at(s0)
        sin = abs(float(cross2(rt[[0, 2]] / max(np.linalg.norm(rt[[0, 2]]), 1e-9), dirn)))
        hb = w * 1.15 * 0.9 + 0.5
        length = float(np.clip(2 * hb / max(sin, 0.5) + 2.6, 4.5, 10.0))
        u = np.linspace(s0 - length / 2, s0 + length / 2, int(length / 0.5) + 1)
        P, T = road.at(u)
        n = np.stack([-T[:, 2], np.zeros(len(u)), T[:, 0]], 1)
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
        up = np.array([0.0, 1.0, 0.0])
        for sgn in (-1.0, 1.0):
            N = n * sgn
            inner = P + N * (road.half + WALL_IN)
            top = P[:, 1] + PARAPET
            gi = ground(inner[:, 0], inner[:, 2])
            go = ground(*(P + N * (road.half + WALL_IN + WALL_T + 0.15))[:, [0, 2]].T)
            base = np.minimum(gi, go) - 0.4
            ib, it = inner.copy(), inner.copy()
            ib[:, 1] = base; it[:, 1] = top
            ob = P + N * (road.half + WALL_IN + WALL_T + BATTER); ob[:, 1] = base
            ot = P + N * (road.half + WALL_IN + WALL_T); ot[:, 1] = top
            col = np.array([0.5, 0.5, 0.3, 0.12])
            strip(wall, ib, it, col)          # faces the road
            strip(wall, ot, ob, col)          # faces the beck
            strip(wall, it, ot, col)          # top, under the coping
            for e, flip in ((0, False), (-1, True)):
                A = np.array([ib[e], ob[e]]); B = np.array([it[e], ot[e]])
                if flip:
                    A, B = A[::-1], B[::-1]
                strip(wall, A, B, col)
            # The culvert's mouth, where the beck meets the wall: a dark arch just proud of the face.
            hw = w * 0.5 + 0.25
            mc = road.at(s0)[0] + N[len(u) // 2] * (road.half + WALL_IN + WALL_T + BATTER * 0.7 + 0.02)
            gb = float(ground(np.array([mc[0]]), np.array([mc[2]]))[0]) - 0.1
            tdir = T[len(u) // 2] * np.array([1, 0, 1]); tdir /= max(np.linalg.norm(tdir), 1e-9)
            ang = np.linspace(0, math.pi, 13)
            rise = min(0.65, hw * 0.8)
            arc = [mc + tdir * (hw * math.cos(a)) + up * (gb + 0.25 + rise * math.sin(a) - mc[1]) for a in ang]
            pts = np.array([mc + tdir * hw + up * (gb - mc[1])] + arc + [mc - tdir * hw + up * (gb - mc[1])])
            cen = mc + up * (gb + 0.3 - mc[1])
            Pm = np.concatenate([cen[None], pts])
            Tm = np.array([[0, i + 1, i + 2] for i in range(len(pts) - 1)])
            if np.dot(np.cross(Pm[Tm[0, 1]] - Pm[0], Pm[Tm[0, 2]] - Pm[0]), N[len(u) // 2]) < 0:
                Tm = Tm[:, [0, 2, 1]]
            wall.add(Pm, Tm, np.array([0.04, 0.5, 0.0, 0.6]), np.zeros((len(Pm), 2)))
            # The coping: upright stones along the top, each a little different.
            a = 0.0
            while a < length - 0.08:
                th = rng.uniform(0.09, 0.2)
                a += th / 2
                p, t = road.at(np.array([u[0] + a]))
                p, t = p[0], t[0]
                nn = np.array([-t[2], 0, t[0]]) * sgn; nn /= max(np.linalg.norm(nn), 1e-9)
                hgt = rng.uniform(0.2, 0.34)
                tilt = rng.normal(0, 0.06)
                uu = up * math.cos(tilt) + t * math.sin(tilt)
                c = p + nn * (road.half + WALL_IN + WALL_T / 2) + up * (PARAPET + hgt / 2 - 0.04)
                box(cope, c, t, uu, nn, (th, hgt, WALL_T + rng.uniform(0.0, 0.12)),
                    np.array([rng.uniform(0.42, 0.56), rng.uniform(0, 1), rng.uniform(0.3, 0.7), rng.uniform(0.1, 0.45)]))
                a += th / 2 + rng.uniform(0.005, 0.03)
    return wall, cope


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
    rd = Road(route)
    road = rd.xz
    half_road = rd.half
    rng = np.random.default_rng(31)
    streams = read_streams(root, to_ride)
    cross = find_crossings(streams, rd, land)
    falls = read_waterfalls(root, to_ride)
    rock = TM.Mesh()
    fell = []
    bed, water = Out(), Out()
    total = 0.0
    stones = []
    for kind, L in streams:
        if not land.in_tiles(L[:, 0], L[:, 1]).any():
            continue
        Q, length = densify(L, STEP)
        if len(Q) < 6:
            continue
        if length < 30 and line_dist(Q[:, 0], Q[:, 1], [road], cell=30.0).min() < 1.0:
            continue   # the culvert itself, mapped as a short way: the becks either side are carried to it
        d = np.gradient(Q, axis=0); d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
        side = np.stack([-d[:, 1], d[:, 0]], 1)
        # Onto the gill's floor: the lowest ground within 5 m across, smoothed along the beck (but not
        # near the road, where it must meet its culvert).
        offs = np.arange(-5.0, 5.01, 0.5) * (2.0 if kind == 0 else 1.0)
        G = np.stack([ground(Q[:, 0] + side[:, 0] * o, Q[:, 1] + side[:, 1] * o) for o in offs], 1)
        shift = smooth(offs[np.argmin(G + 0.02 * np.abs(offs)[None], axis=1)], 5.0)
        dr = line_dist(Q[:, 0], Q[:, 1], [road], cell=30.0)
        shift *= np.clip((dr - half_road - 2.0) / 8.0, 0, 1)
        Q = Q + side * shift[:, None]
        d = np.gradient(Q, axis=0); d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
        side = np.stack([-d[:, 1], d[:, 0]], 1)
        # Where it runs: on the 2 m tiles (a few metres in from their edge) and clear of the road: the
        # bed runs in under the headwall, the water stops a little short of it.
        ok = land.in_tiles(Q[:, 0] + 6, Q[:, 1]) & land.in_tiles(Q[:, 0] - 6, Q[:, 1]) & \
             land.in_tiles(Q[:, 0], Q[:, 1] + 6) & land.in_tiles(Q[:, 0], Q[:, 1] - 6)
        dr = line_dist(Q[:, 0], Q[:, 1], [road], cell=30.0)
        ok &= dr > half_road + BED_GAP
        idx = np.flatnonzero(ok)
        if len(idx) < 4:
            continue
        # Water width: longer becks are bigger; the map's rivers (the Swale) far bigger.
        w0 = 9.0 if kind == 0 else float(np.clip(0.7 + length / 1800.0, 0.7, 3.2))
        for run in np.split(idx, np.flatnonzero(np.diff(idx) > 1) + 1):
            if len(run) < 4:
                continue
            q, sd, dq = Q[run], side[run], d[run]
            # Carry an end at the road on in a straight line until it is under the headwall (the
            # steps along the beck stop up to a metre short of it).
            if dr[run[-1]] < half_road + BED_GAP + 2.5 and len(run) > 1:
                rate = max(dr[run[-2]] - dr[run[-1]], 0.2)
                extra = float(np.clip((dr[run[-1]] - half_road - BED_GAP + 0.3) / rate, 0, 3))
                q = np.concatenate([q, q[-1:] + dq[-1:] * extra * STEP]); sd = np.concatenate([sd, sd[-1:]])
                dq = np.concatenate([dq, dq[-1:]])
            if dr[run[0]] < half_road + BED_GAP + 2.5 and len(run) > 1:
                rate = max(dr[run[1]] - dr[run[0]], 0.2)
                extra = float(np.clip((dr[run[0]] - half_road - BED_GAP + 0.3) / rate, 0, 3))
                q = np.concatenate([q[:1] - dq[:1] * extra * STEP, q]); sd = np.concatenate([sd[:1], sd])
                dq = np.concatenate([dq[:1], dq])
            drq = line_dist(q[:, 0], q[:, 1], [road], cell=30.0)
            n = len(q)
            gy = ground(q[:, 0], q[:, 1])
            steep = np.clip(-np.gradient(smooth(gy, 2.0)) / STEP, 0, 1)   # fall per metre
            wv = w0 * (1 + 0.25 * np.sin(np.arange(n) / rng.uniform(5, 9) + rng.uniform(0, 6))) * (1 - 0.3 * np.clip(steep * 3, 0, 1))
            # Taper where the beck starts or leaves the tiles; full width up to its culvert.
            at_road = drq < half_road + BED_GAP + 2.5
            e0 = np.arange(n) * STEP if not at_road[0] else np.full(n, 1e9)
            e1 = np.arange(n)[::-1] * STEP if not at_road[-1] else np.full(n, 1e9)
            taper = np.clip(np.minimum(e0, e1) / 3.0, 0.2, 1)
            # The bed: wet stone and gravel, a good deal wider than the water, mossy at its edges.
            hb = (wv * 0.9 + 0.5) * taper
            # A waterfall the map marks on this stretch: its steps, shelves and pouring water.
            covered = np.zeros(n, bool)
            for wname, wp in falls:
                dw = np.hypot(q[:, 0] - wp[0], q[:, 1] - wp[1])
                iw = int(np.argmin(dw))
                if dw[iw] < 8.0:
                    lo, hi = max(iw - 40, 0), min(iw + 41, n)
                    c = cascade(q[lo:hi], sd[lo:hi], dq[lo:hi], gy[lo:hi], hb[lo:hi], wv[lo:hi] * 0.5 * taper[lo:hi],
                                rng, rock, water)
                    if c.any():
                        covered[lo:hi] |= c
                        fell.append(wname or "unnamed")
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
            # The water: a little over the bed, narrower, stopping short of the road.
            wet = np.flatnonzero((drq > half_road + WATER_GAP) & ~covered)
            for wr in np.split(wet, np.flatnonzero(np.diff(wet) > 1) + 1):
                if len(wr) < 3:
                    continue
                hw = wv[wr] * 0.5 * taper[wr]
                Pw, Tw = ribbon(q[wr], sd[wr], hw, 3, ground, LIFT + 0.04)
                m = len(wr)
                Cw = np.zeros((m, 3, 4)); Cw[..., 0] = np.clip(steep[wr] * 4, 0, 1)[:, None]; Cw[..., 3] = 1
                UVw = np.stack([np.linspace(0, 1, 3)[None].repeat(m, 0), along[wr, :3]], -1)
                water.add(Pw.reshape(-1, 3), Tw, Cw.reshape(-1, 4), UVw.reshape(-1, 2))
            # Stones: cobbles all along, boulders now and then, more at the edges and on the steep.
            for k in range(n):
                if covered[k]:
                    continue
                for _ in range(rng.poisson(1.2 + 3 * steep[k])):
                    a = rng.uniform(-1, 1) * hb[k] * 1.05
                    sz = rng.choice([0.08, 0.14, 0.22, 0.4, 0.7], p=[0.35, 0.3, 0.2, 0.11, 0.04])
                    sz *= rng.uniform(0.75, 1.3)
                    c = q[k] + sd[k] * a + dq[k] * rng.uniform(-0.5, 0.5)
                    gyc = float(ground(np.array([c[0]]), np.array([c[1]]))[0])
                    stones.append((int(rng.integers(N_ROCKS)), c[0], gyc + LIFT - sz * 0.12, c[1], rng.uniform(0, 2 * math.pi), sz))
            total += n * STEP
    # No stones in the headwalls.
    st = np.array(stones)
    keep = line_dist(st[:, 1], st[:, 3], [road], cell=30.0) > half_road + BED_GAP + 0.5 + st[:, 5] * 0.5
    stones = [r for r, k in zip(stones, keep) if k]
    wall, cope = headwalls(cross, rd, lambda x, z: ground(np.asarray(x, float), np.asarray(z, float)), rng)
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".glb"):
            os.remove(os.path.join(OUT, f))
    names = bed.write("bed") + water.write("water")
    for m, name in ((wall, "wall_00"), (cope, "cope_00"), (rock, "falls_00")):
        if m.n:
            P, T, N, C, UV = m.arrays()
            write_glb(os.path.join(OUT, name + ".glb"), P, T, N, C, name, UV)
            names.append(name)
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
    json.dump({"about": "Becks made by Tools/make_becks.py; instances.bin's species index is into 'species'. "
                        "wall_ meshes are the culverts' headwalls (M_Masonry), cope_ their coping stones (M_Stone).",
               "species": [{"name": n, "mesh": n, "count": int((st[:, 0] == k).sum()), "cull_m": 600, "shadow": True}
                           for k, n in enumerate(rock_names)],
               "km": round(total / 1000, 2), "meshes": names,
               "crossings_km": [round(c[0] / 1000, 2) for c in cross], "waterfalls": fell},
              open(os.path.join(OUT, "becks.json"), "w"), indent=1)
    print("becks: %.1f km, %d stones, %d meshes: %s" % (total / 1000, len(stones), len(names), names))
    print("culverts at km", ", ".join("%.2f" % (c[0] / 1000) for c in cross))
    print("waterfalls:", ", ".join(fell))


if __name__ == "__main__":
    main()
