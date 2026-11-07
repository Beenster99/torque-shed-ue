#!/usr/bin/env python3
"""Moorland ground cover for the Buttertubs ride: the plants, modelled as real geometry for Nanite, and
where each one grows, worked out from the land already in Data/Buttertubs.

    python Tools/make_foliage.py              plants and placement (about a minute)
    python Tools/make_foliage.py --density 0.6  fewer plants, if the frame rate needs it

Writes Data/Buttertubs/Foliage/:

    <species>.glb     one plant (or patch) each, metres, y up, its base at the origin. Blades, stems and
                      twigs are real triangles (no alpha cut-outs), which is what Nanite draws best.
                      Vertex colour RGB is the plant's colour (linear), A is how much sky the spot sees
                      (dark down in the clump, light at the tips). UV x is a random number per blade,
                      UV y is how far the wind moves that vertex (its height in metres, less for stiff
                      plants), which M_Foliage turns into sway.
    instances.bin     where every plant goes, in Unreal's frame, read by ATorqueFoliage at BeginPlay.
    foliage.json      the species: cull distances, shadows, counts, and the format of instances.bin.

What grows where (the summit of Buttertubs is acid grassland and blanket bog at about 500 m):
    sward       short grazed grass and herbs, in metre patches, carpeting the ground near the road
    tuft        fine bent and fescue tufts, the green of the verges and the grazed ground
    tussock     purple moor grass: tall, straw-tipped tussocks, in big patches on the open moor
    matgrass    mat grass: stiff, pale, upright, on the drier ground
    far         big, coarse clumps of the same grasses with fewer, wider blades: drawn out to 150 m,
                so the hillside keeps its texture after the fine grass has been culled
    rush        soft rush: dark green tussocks of round stems in wet flushes and hollows
    heather     ling: woody little bushes, mostly on the shrub (heath) cover and drier slopes
    moss        sphagnum and moss cushions in the wettest hollows
    bracken     fronds on the steeper, drier slopes of the drop towards Thwaite

Placement uses: the land's height (slope and wetness, from the terrain tiles), the map's cover (from the
tiles' vertex colour), the distance from the road (route.json), large soft noise fields so the plants
grow in patches, and the countryside detail (detail_NN.glb) so nothing grows through walls, barns,
rocks or sheep. The output is deterministic. Only numpy is needed.

Frames: the glb files are metres, x east, y up, z south. Unreal is (100x, 100z, 100y) in centimetres.
"""
import argparse, json, math, os, struct, sys, zlib
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from export_buttertubs import write_glb, COVER_RGB   # noqa: E402

DATA = os.path.join(REPO, "Data", "Buttertubs")
OUT = os.path.join(DATA, "Foliage")


def lin(rgb):
    """sRGB 0..255 to linear 0..1."""
    return np.power(np.asarray(rgb, np.float64) / 255.0, 2.2)


# --- Building meshes -------------------------------------------------------------------------------

class Mesh:
    def __init__(self):
        self.P, self.N, self.UV, self.C, self.T = [], [], [], [], []
        self.n = 0

    def add(self, pos, nrm, uv, col, tris):
        pos = np.asarray(pos, np.float64).reshape(-1, 3)
        self.P.append(pos); self.N.append(np.asarray(nrm, np.float64).reshape(-1, 3))
        self.UV.append(np.asarray(uv, np.float64).reshape(-1, 2)); self.C.append(np.asarray(col, np.float64).reshape(-1, 4))
        self.T.append(np.asarray(tris, np.int64).reshape(-1, 3) + self.n)
        self.n += len(pos)

    def write(self, path, name):
        P = np.concatenate(self.P); N = np.concatenate(self.N); UV = np.concatenate(self.UV)
        C = np.concatenate(self.C); T = np.concatenate(self.T)
        N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)
        rgba = (np.clip(C, 0, 1) * 255).round().astype(np.uint8)
        write_glb(path, P, T, N, rgba, name, uv=UV)
        return len(T)


def normalize(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-12)


def ribbons(M, base, azim, height, width, lean, bend, c_base, c_tip, ao_base, segs, flex=1.0,
            taper=0.8, round_=0.35, up_normal=0.45, rng=None, twist=None):
    """Grass blades as tapered, curving ribbons, all at once. Each blade starts at `base` (n, 3), leans
    `lean` radians from upright towards azimuth `azim` and bends a further `bend` by its tip."""
    n = len(base)
    t = np.linspace(0, 1, segs + 1)                                  # along the blade
    ang = lean[:, None] + bend[:, None] * t[None, :] ** 1.6           # from upright
    dh = (height / segs)[:, None] * np.ones_like(t)[None, :]
    dh[:, 0] = 0
    horiz = np.cumsum(np.sin(ang) * dh, 1); vert = np.cumsum(np.cos(ang) * dh, 1)
    d = np.stack([np.cos(azim), np.zeros(n), np.sin(azim)], -1)       # leaning direction, horizontal
    side = np.stack([-np.sin(azim), np.zeros(n), np.cos(azim)], -1)
    if twist is not None:                                              # turn the blade about its axis a little
        side = normalize(side + d * twist[:, None])
    centre = base[:, None, :] + d[:, None, :] * horiz[..., None] + np.array([0, 1.0, 0]) * vert[..., None]
    w = width[:, None] * np.maximum(1 - t[None, :], 0.0) ** taper * 0.5
    w[:, -1] = width * 0.02
    left = centre - side[:, None, :] * w[..., None]; right = centre + side[:, None, :] * w[..., None]
    pos = np.stack([left, right], 2)                                   # (n, segs+1, 2, 3)
    tang = np.stack([d[:, None, 0] * np.sin(ang), np.cos(ang), d[:, None, 2] * np.sin(ang)], -1)
    face = normalize(np.cross(tang, side[:, None, :]))
    # Face each blade towards the sky (its middle's normal pointing up); the winding follows, so a
    # two-sided material flips the normal on the right side.
    flip = face[:, segs // 2, 1] < 0
    face[flip] *= -1
    face = normalize(face + np.array([0, up_normal, 0]))
    nl = normalize(face - side[:, None, :] * round_); nr = normalize(face + side[:, None, :] * round_)
    nrm = np.stack([nl, nr], 2)
    tt = np.broadcast_to(t[None, :, None], (n, segs + 1, 2))
    col = c_base[:, None, None, :] * (1 - tt[..., None] ** 0.9) + c_tip[:, None, None, :] * tt[..., None] ** 0.9
    ao = ao_base[:, None, None] + (1 - ao_base[:, None, None]) * tt ** 0.6
    rgba = np.concatenate([col, ao[..., None]], -1)
    rid = (rng.uniform(0, 1, n) if rng is not None else np.zeros(n))
    sway = pos[..., 1] - base[:, None, None, 1]
    uv = np.stack([np.broadcast_to(rid[:, None, None], (n, segs + 1, 2)), sway * flex], -1)
    i = np.arange((segs + 1) * 2).reshape(segs + 1, 2)
    a, b, c, e = i[:-1, 0], i[:-1, 1], i[1:, 0], i[1:, 1]
    tri1 = np.concatenate([np.stack([a, c, b], 1), np.stack([b, c, e], 1)])
    per = (segs + 1) * 2
    tris = tri1[None] + (np.arange(n) * per)[:, None, None]
    tris[flip] = tris[flip][:, :, ::-1]
    M.add(pos.reshape(-1, 3), nrm.reshape(-1, 3), uv.reshape(-1, 2), rgba.reshape(-1, 4), tris.reshape(-1, 3))


def tubes(M, pts, radius, col, ao, sides=3, flex=None, rng=None):
    """Stems and twigs: thin tubes along polylines. pts (n, k, 3), radius (n, k), col (n, k, 3), ao (n, k)."""
    n, k, _ = pts.shape
    tang = np.gradient(pts, axis=1)
    tang = normalize(tang)
    ref = np.where(np.abs(tang[..., 1:2]) > 0.9, np.array([1.0, 0, 0]), np.array([0, 1.0, 0]))
    u = normalize(np.cross(tang, ref)); v = np.cross(tang, u)
    a = np.linspace(0, 2 * math.pi, sides, endpoint=False)
    ring = np.cos(a)[None, None, :, None] * u[:, :, None, :] + np.sin(a)[None, None, :, None] * v[:, :, None, :]
    pos = pts[:, :, None, :] + ring * radius[..., None, None]
    nrm = normalize(ring + np.array([0, 0.3, 0]))
    rgba = np.concatenate([np.broadcast_to(col[:, :, None, :], (n, k, sides, 3)),
                           np.broadcast_to(ao[:, :, None, None], (n, k, sides, 1))], -1)
    sway = (pos[..., 1] - pts[:, :1, None, 1]) * (1.0 if flex is None else flex[:, None, None])
    rid = rng.uniform(0, 1, n) if rng is not None else np.zeros(n)
    uv = np.stack([np.broadcast_to(rid[:, None, None], (n, k, sides)), sway], -1)
    i = np.arange(k * sides).reshape(k, sides)
    tri = []
    for s in range(sides):
        s2 = (s + 1) % sides
        a_, b_, c_, d_ = i[:-1, s], i[:-1, s2], i[1:, s], i[1:, s2]
        tri += [np.stack([a_, b_, c_], 1), np.stack([b_, d_, c_], 1)]   # facing out
    tri = np.concatenate(tri)
    per = k * sides
    tris = (tri[None] + (np.arange(n) * per)[:, None, None]).reshape(-1, 3)
    M.add(pos.reshape(-1, 3), nrm.reshape(-1, 3), uv.reshape(-1, 2), rgba.reshape(-1, 4), tris)


def leaves(M, centre, direction, length, width, col, ao, sway, rng):
    """Tiny leaves or shoots as two crossed diamonds (heather's scale leaves, seed heads, petals)."""
    n = len(centre)
    d = normalize(direction)
    ref = np.where(np.abs(d[:, 1:2]) > 0.9, np.array([1.0, 0, 0]), np.array([0, 1.0, 0]))
    s1 = normalize(np.cross(d, ref)); s2 = np.cross(d, s1)
    P, N = [], []
    for s in (s1, s2):
        p = np.stack([centre - d * length[:, None] * 0.5, centre + s * width[:, None] * 0.5,
                      centre + d * length[:, None] * 0.5, centre - s * width[:, None] * 0.5], 1)
        nn = normalize(np.cross(d, s))
        nn = normalize(nn + np.array([0, 0.8, 0]))
        P.append(p); N.append(np.repeat(nn[:, None], 4, 1))
    pos = np.concatenate(P, 1); nrm = np.concatenate(N, 1)
    rgba = np.concatenate([np.repeat(col[:, None], 8, 1), np.repeat(ao[:, None, None], 8, 1)], -1)
    rid = rng.uniform(0, 1, n)
    uv = np.stack([np.repeat(rid[:, None], 8, 1), np.repeat(sway[:, None], 8, 1)], -1)
    q = np.array([[0, 2, 1], [0, 3, 2], [4, 6, 5], [4, 7, 6]])   # front faces along the normals
    tris = (q[None] + (np.arange(n) * 8)[:, None, None]).reshape(-1, 3)
    M.add(pos.reshape(-1, 3), nrm.reshape(-1, 3), uv.reshape(-1, 2), rgba.reshape(-1, 4), tris)


def disk(rng, n, r):
    a = rng.uniform(0, 2 * math.pi, n); rr = r * np.sqrt(rng.uniform(0, 1, n))
    return np.stack([np.cos(a) * rr, np.zeros(n), np.sin(a) * rr], -1), a, rr


def mixcol(rng, n, cols, weights, jitter=0.08):
    k = rng.choice(len(cols), n, p=np.asarray(weights) / np.sum(weights))
    c = lin(np.asarray(cols, np.float64)[k])
    return c * rng.uniform(1 - jitter, 1 + jitter, (n, 1)) * rng.uniform(0.95, 1.05, (n, 3))


# Late summer on the moor: green below, straw and dead leaves through it.
GREEN = (84, 104, 42); GREEN_DK = (58, 78, 32); YELLOW = (128, 126, 56); STRAW = (176, 156, 98)
DEAD = (126, 104, 70); PALE = (150, 146, 96)


def grass_clump(seed, n, r, h, w, lean_max, bend, cols, weights, tip_cols, tip_weights, segs=5, ao=0.25):
    rng = np.random.default_rng(seed)
    M = Mesh()
    base, a, rr = disk(rng, n, r)
    out = a + rng.normal(0, 0.5, n)                       # mostly outwards from the clump's middle
    lean = (rr / r) * lean_max * rng.uniform(0.5, 1.1, n) + rng.uniform(0.0, 0.15, n)
    height = rng.uniform(*h, n) * (1.05 - 0.3 * rr / r)   # the middle a little taller
    cb = mixcol(rng, n, cols, weights)
    ct = mixcol(rng, n, tip_cols, tip_weights)
    ct = cb * 0.4 + ct * 0.6
    ribbons(M, base, out, height, rng.uniform(*w, n), lean, rng.uniform(*bend, n), cb * 0.8, ct,
            np.full(n, ao) + 0.25 * rr / r, segs, rng=rng, twist=rng.normal(0, 0.4, n))
    return M


def sward(seed, flowers=0):
    """A metre and a bit of short grazed turf: many short blades, a few tiny flowers."""
    rng = np.random.default_rng(seed)
    M = Mesh()
    n = 800
    base = np.stack([rng.uniform(-0.6, 0.6, n), np.zeros(n), rng.uniform(-0.6, 0.6, n)], -1)
    # Thin the corners so neighbouring patches blend instead of tiling as squares.
    keep = np.hypot(base[:, 0], base[:, 2]) < 0.6 + rng.uniform(-0.1, 0.12, n)
    base = base[keep]; n = len(base)
    cb = mixcol(rng, n, [GREEN, GREEN_DK, YELLOW, DEAD], [5, 3, 2, 1])
    ct = mixcol(rng, n, [GREEN, YELLOW, PALE], [4, 2, 1])
    ribbons(M, base, rng.uniform(0, 2 * math.pi, n), rng.uniform(0.05, 0.2, n) * rng.uniform(0.6, 1.0, n), rng.uniform(0.003, 0.006, n),
            rng.uniform(0.05, 0.5, n), rng.uniform(0.1, 0.9, n), cb * 0.75, ct, np.full(n, 0.35), 3, rng=rng)
    if flowers:
        # Tormentil (yellow) and eyebright (white) on short stalks.
        fb, _, _ = disk(rng, flowers, 0.55)
        hgt = rng.uniform(0.05, 0.12, flowers)
        stalk = np.stack([fb, fb + np.array([0, 1.0, 0]) * hgt[:, None] * 0.5, fb + np.array([0, 1.0, 0]) * hgt[:, None]], 1)
        stalk[:, 1:, 0] += rng.normal(0, 0.01, (flowers, 2))
        tubes(M, stalk, np.full((flowers, 3), 0.0012), np.broadcast_to(lin(GREEN), (flowers, 3, 3)) * 1.0,
              np.full((flowers, 3), 0.6), sides=3, rng=rng)
        petal = np.where(rng.uniform(0, 1, flowers)[:, None] < 0.6, lin((235, 200, 40)), lin((235, 235, 225)))
        for k in range(4):
            ang = k * math.pi / 2 + rng.uniform(0, 1, flowers)
            dirv = np.stack([np.cos(ang), np.full(flowers, 0.15), np.sin(ang)], -1)
            leaves(M, stalk[:, -1] + normalize(dirv) * 0.004, dirv, np.full(flowers, 0.009), np.full(flowers, 0.006),
                   petal, np.full(flowers, 1.0), hgt, rng)
    return M


def carpet(seed, radius, per_m2, h, w, cols, weights, tips, tip_w, segs=3, flowers=0, tall=0.0):
    """A round mat of grass to lay edge to edge: hundreds of blades per square metre, so the ground
    reads as a continuous sward instead of tufts on bare soil. Thinner and shorter at its rim, so
    overlapping mats merge without a seam. Laid tilted to the slope (see `ALIGN`)."""
    rng = np.random.default_rng(seed)
    M = Mesh()
    n = int(math.pi * radius ** 2 * per_m2)
    base, a, rr = disk(rng, n, radius)
    rim = np.clip((radius - rr) / (0.3 * radius), 0, 1)          # 0 at the edge, 1 inside
    keep = rng.uniform(0, 1, n) < 0.35 + 0.65 * rim
    base, rr, rim = base[keep], rr[keep], rim[keep]; n = len(base)
    # Little clusters: blade height varies over a few centimetres, as real turf does.
    clump = np.sin(base[:, 0] * 23 + seed) * np.sin(base[:, 2] * 19 - seed) * 0.5 + 0.5
    height = rng.uniform(*h, n) * (0.55 + 0.45 * clump) * (0.6 + 0.4 * rim)
    # On the moor, the odd taller, finer stem of wavy hair grass above the mat.
    tl = rng.uniform(0, 1, n) < tall
    height[tl] = rng.uniform(0.25, 0.42, tl.sum())
    cb = mixcol(rng, n, cols, weights)
    ct = mixcol(rng, n, tips, tip_w)
    ct = cb * 0.5 + ct * 0.5          # tips only a little paler than the blade, so the mat matches the land
    ribbons(M, base, rng.uniform(0, 2 * math.pi, n), height, rng.uniform(*w, n), rng.uniform(0.05, 0.55, n),
            rng.uniform(0.2, 1.1, n), cb * 0.7, ct, np.full(n, 0.3), segs, rng=rng, twist=rng.normal(0, 0.4, n))
    if flowers:
        fb, _, _ = disk(rng, flowers, radius * 0.9)
        hgt = rng.uniform(0.08, 0.2, flowers)
        stalk = np.stack([fb, fb + np.array([0, 1.0, 0]) * hgt[:, None] * 0.5, fb + np.array([0, 1.0, 0]) * hgt[:, None]], 1)
        stalk[:, 1:, 0] += rng.normal(0, 0.01, (flowers, 2))
        tubes(M, stalk, np.full((flowers, 3), 0.0012), np.broadcast_to(lin(GREEN), (flowers, 3, 3)) * 1.0,
              np.full((flowers, 3), 0.6), sides=3, rng=rng)
        petal = np.where(rng.uniform(0, 1, flowers)[:, None] < 0.6, lin((235, 200, 40)), lin((235, 235, 225)))
        for k in range(4):
            ang = k * math.pi / 2 + rng.uniform(0, 1, flowers)
            dirv = np.stack([np.cos(ang), np.full(flowers, 0.15), np.sin(ang)], -1)
            leaves(M, stalk[:, -1] + normalize(dirv) * 0.004, dirv, np.full(flowers, 0.009), np.full(flowers, 0.006),
                   petal, np.full(flowers, 1.0), hgt, rng)
    return M


def rush(seed):
    """Soft rush: a dense tussock of smooth round stems, the seed heads bursting from one side near the
    top, last year's stems tan and falling outwards."""
    rng = np.random.default_rng(seed)
    M = Mesh()
    n = int(rng.uniform(70, 100))
    r = rng.uniform(0.08, 0.14)
    base, a, rr = disk(rng, n, r)
    dead = rng.uniform(0, 1, n) < 0.18
    lean = (rr / r) * rng.uniform(0.1, 0.5, n) + np.where(dead, rng.uniform(0.6, 1.3, n), 0.03)
    bend = np.where(dead, rng.uniform(0.3, 0.8, n), rng.uniform(0.02, 0.2, n))
    h = rng.uniform(0.55, 1.0, n) * np.where(dead, 0.8, 1.0)
    k = 6
    t = np.linspace(0, 1, k)
    ang = lean[:, None] + bend[:, None] * t[None] ** 1.5
    dh = (h / (k - 1))[:, None] * np.ones(k)[None]; dh[:, 0] = 0
    out = a + rng.normal(0, 0.3, n)
    dvec = np.stack([np.cos(out), np.zeros(n), np.sin(out)], -1)
    pts = (base[:, None] + dvec[:, None] * np.cumsum(np.sin(ang) * dh, 1)[..., None]
           + np.array([0, 1.0, 0]) * np.cumsum(np.cos(ang) * dh, 1)[..., None])
    radius = np.outer(rng.uniform(0.0013, 0.002, n), np.maximum(1 - t ** 3, 0.08))
    live = mixcol(rng, n, [(66, 90, 42), (78, 100, 46), (88, 104, 54)], [3, 3, 1], 0.06)
    tipc = np.where(rng.uniform(0, 1, n)[:, None] < 0.5, lin((120, 104, 70)), live)
    deadc = mixcol(rng, n, [STRAW, DEAD, (104, 86, 60)], [2, 2, 1])
    c0 = np.where(dead[:, None], deadc * 0.8, live * 0.75)
    c1 = np.where(dead[:, None], deadc, live)
    col = c0[:, None] * (1 - t[None, :, None]) + c1[:, None] * t[None, :, None]
    col[:, -1] = np.where(dead[:, None], deadc, tipc)
    ao = 0.25 + 0.75 * t[None] ** 0.5 * np.ones((n, 1))
    tubes(M, pts, radius, col, ao, sides=3, rng=rng)
    # Seed heads: brown clusters on about half the live stems, 70 to 85 % of the way up.
    hs = np.where(~dead & (rng.uniform(0, 1, n) < 0.5))[0]
    for i in hs:
        f = rng.uniform(0.68, 0.85)
        p = np.array([np.interp(f, t, pts[i, :, j]) for j in range(3)])
        m = int(rng.integers(6, 12))
        d = rng.normal(0, 1, (m, 3)); d[:, 1] = np.abs(d[:, 1]) * 0.5 + 0.2
        side = np.array([math.cos(out[i] + 1.3), 0, math.sin(out[i] + 1.3)])
        cen = p + side * 0.012 + normalize(d) * rng.uniform(0.004, 0.02, (m, 1))
        leaves(M, cen, d, np.full(m, rng.uniform(0.006, 0.01)), np.full(m, 0.004),
               lin((112, 86, 54))[None] * rng.uniform(0.8, 1.1, (m, 1)), np.full(m, 0.8),
               np.full(m, (cen[0, 1]) * 1.0), rng)
    return M


# Heather in late summer: dark olive, brown, and a purple-brown cast from the old flower spikes.
HEATHER_COLS = [(50, 58, 32), (60, 62, 36), (72, 60, 42), (82, 62, 60), (58, 46, 36)]
HEATHER_W = [4, 3, 2, 2, 1]


def heather_twigs(seed, flowering=0.05):
    """Ling up close (by the road): a low, airy woody bush. Wandering dark stems from the root, side
    twigs, and the tiny scale leaves as shoots along the outer half of each twig, dark brown-green
    with a purple-brown cast; a few flower spikes. About 7k triangles."""
    rng = np.random.default_rng(seed)
    M = Mesh()
    R = rng.uniform(0.2, 0.34); H = rng.uniform(0.2, 0.38)
    stems = []
    nmain = int(rng.integers(9, 15))
    for s in range(nmain):
        a = rng.uniform(0, 2 * math.pi)
        p = np.array([math.cos(a) * 0.03, 0.0, math.sin(a) * 0.03])
        d = np.array([math.cos(a) * rng.uniform(0.5, 1.1), 1.0, math.sin(a) * rng.uniform(0.5, 1.1)])
        pts = [p]
        for k in range(6):
            d = normalize(d + rng.normal(0, 0.35, 3) * np.array([1, 0.3, 1]))
            p = p + d * (math.hypot(R, H) / 6)
            p[1] = min(p[1], H * rng.uniform(0.85, 1.05))
            pts.append(p.copy())
        trunk = np.array(pts)
        stems.append(trunk)
        # Side twigs from the upper two thirds.
        for k in range(2, 6):
            for _ in range(int(rng.integers(1, 4))):
                b0 = trunk[k]
                d2 = normalize(d + rng.normal(0, 0.8, 3) * np.array([1, 0.2, 1]) + np.array([0, 0.5, 0]))
                tw = [b0]
                for j in range(3):
                    d2 = normalize(d2 + rng.normal(0, 0.3, 3))
                    tw.append(tw[-1] + d2 * rng.uniform(0.03, 0.06))
                stems.append(np.array(tw))
    # Tubes need equal lengths: resample every stem to 5 points.
    rs = []
    for s in stems:
        L = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(s, axis=0), axis=1))])
        tt = np.linspace(0, L[-1], 5)
        rs.append(np.stack([np.interp(tt, L, s[:, j]) for j in range(3)], 1))
    S = np.array(rs)
    ns = len(S)
    radius = np.outer(np.where(np.arange(ns) < nmain, 0.004, 0.0018), np.linspace(1, 0.4, 5))
    wood = lin((62, 48, 38))
    col = np.broadcast_to(wood, (ns, 5, 3)) * rng.uniform(0.8, 1.15, (ns, 1, 1))
    ao = 0.2 + 0.6 * (S[..., 1] / H).clip(0, 1)
    flex = 0.35
    tubes(M, S, radius, col, ao, sides=3, flex=np.full(ns, flex), rng=rng)
    # Shoots along the outer half of every stem and twig.
    cen, dirs, cols, aos = [], [], [], []
    leaf_cols = HEATHER_COLS
    for s in S:
        L = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(s, axis=0), axis=1))])
        m = int(L[-1] / 0.011) + 3
        u = np.sort(rng.uniform(0.35, 1.0, m)) * L[-1]
        p = np.stack([np.interp(u, L, s[:, j]) for j in range(3)], 1)
        dd = np.gradient(p, axis=0) if m > 1 else np.array([[0, 1.0, 0]])
        dd = normalize(normalize(dd) + rng.normal(0, 0.6, (m, 3)) + np.array([0, 0.6, 0]))
        cen.append(p + rng.normal(0, 0.006, (m, 3))); dirs.append(dd)
        c = mixcol(rng, m, leaf_cols, HEATHER_W, 0.1)
        flower = (rng.uniform(0, 1, m) < flowering) & (u / L[-1] > 0.7)
        c[flower] = lin((128, 82, 116)) * rng.uniform(0.85, 1.1, (flower.sum(), 1))
        cols.append(c); aos.append(0.3 + 0.7 * np.clip(p[:, 1] / H, 0, 1))
    cen = np.concatenate(cen); dirs = np.concatenate(dirs); cols = np.concatenate(cols); aos = np.concatenate(aos)
    m = len(cen)
    leaves(M, cen, dirs, rng.uniform(0.03, 0.05, m), rng.uniform(0.011, 0.018, m), cols, aos, cen[:, 1] * flex, rng)
    return M


def heather(seed, flowering=0.06):
    """Ling, cheaply: a lumpy, solid dome of foliage (opaque, so no overdraw, and Nanite simplifies it
    well), its colour speckled dark olive, brown and the odd purple; a few hundred shoots standing out
    of its surface to break the outline; and a few woody stems at the foot. About 3k triangles."""
    rng = np.random.default_rng(seed)
    M = Mesh()
    R = rng.uniform(0.22, 0.34); H = rng.uniform(0.18, 0.32)
    nr, na = 7, 26
    rr = np.linspace(0, 1, nr)[:, None] * np.ones(na)[None]
    aa = np.linspace(0, 2 * math.pi, na, endpoint=False)[None] * np.ones(nr)[:, None]
    lobes = 1 + 0.16 * np.sin(aa * 3 + rng.uniform(0, 6)) + 0.1 * np.sin(aa * 5 + rng.uniform(0, 6))
    bump = rng.normal(0, 0.12, rr.shape)
    x = np.cos(aa) * rr * R * lobes; z = np.sin(aa) * rr * R * lobes
    y = H * (np.clip(1 - rr ** 2.2, 0, 1) ** 0.6) * (1 + bump * (rr > 0)) - 0.02 * rr
    y[-1] = -0.02                                         # the rim tucks into the ground
    pos = np.stack([x, y, z], -1).reshape(-1, 3)
    i = np.arange(nr * na).reshape(nr, na)
    tris = []
    for k in range(na):
        k2 = (k + 1) % na
        tris += [np.stack([i[:-1, k], i[:-1, k2], i[1:, k]], 1), np.stack([i[:-1, k2], i[1:, k2], i[1:, k]], 1)]
    tris = np.concatenate(tris)
    fn = np.cross(pos[tris[:, 1]] - pos[tris[:, 0]], pos[tris[:, 2]] - pos[tris[:, 0]])
    nrm = np.zeros_like(pos)
    for k in range(3):
        np.add.at(nrm, tris[:, k], fn)
    nrm = normalize(np.where(nrm[:, 1:2] < 0, -nrm, nrm)) + np.array([0, 0.4, 0])
    leaf_cols = HEATHER_COLS
    c = mixcol(rng, len(pos), [(46, 52, 30), (54, 50, 34), (60, 48, 44)], [3, 2, 1], 0.05)
    ao = 0.35 + 0.6 * np.clip(pos[:, 1] / H, 0, 1)
    M.add(pos, nrm, np.stack([rng.uniform(0, 1, len(pos)), pos[:, 1] * 0.3], 1), np.concatenate([c, ao[:, None]], 1), tris)
    # Shoots out of the surface.
    m = 700
    u = np.sqrt(rng.uniform(0, 0.95, m)); a = rng.uniform(0, 2 * math.pi, m)
    lob = 1 + 0.16 * np.sin(a * 3)
    cen = np.stack([np.cos(a) * u * R * lob, H * (np.clip(1 - u ** 2.2, 0, 1) ** 0.6), np.sin(a) * u * R * lob], 1)
    out = normalize(np.stack([np.cos(a) * u, np.full(m, 0.8), np.sin(a) * u], 1) + rng.normal(0, 0.35, (m, 3)))
    cen = cen + out * 0.015
    cc = mixcol(rng, m, leaf_cols, HEATHER_W, 0.1)
    fl = rng.uniform(0, 1, m) < flowering
    cc[fl] = lin((128, 82, 116)) * rng.uniform(0.85, 1.1, (fl.sum(), 1))
    leaves(M, cen, out, rng.uniform(0.02, 0.032, m), rng.uniform(0.006, 0.01, m), cc,
           0.5 + 0.5 * np.clip(cen[:, 1] / H, 0, 1), cen[:, 1] * 0.35, rng)
    # A few woody stems at the foot.
    ns = 7
    a = rng.uniform(0, 2 * math.pi, ns)
    t = np.linspace(0, 1, 4)
    st = np.stack([np.cos(a)[:, None] * (0.03 + R * 0.75 * t), H * 0.45 * np.sin(t * 1.4)[None] * np.ones((ns, 1)),
                   np.sin(a)[:, None] * (0.03 + R * 0.75 * t)], -1)
    tubes(M, st, np.full((ns, 4), 0.004), np.broadcast_to(lin((88, 68, 50)), (ns, 4, 3)), np.full((ns, 4), 0.35),
          sides=3, flex=np.full(ns, 0.1), rng=rng)
    return M


def moss(seed):
    """A low cushion of sphagnum and moss: a bumpy dome, green, yellow and the odd red-brown."""
    rng = np.random.default_rng(seed)
    M = Mesh()
    R = rng.uniform(0.35, 0.6); H = rng.uniform(0.03, 0.07)
    nr, na = 8, 28
    rr = np.linspace(0, 1, nr)[:, None] * np.ones(na)[None]
    aa = np.linspace(0, 2 * math.pi, na, endpoint=False)[None] * np.ones(nr)[:, None]
    edge = 1 + 0.18 * np.sin(aa * 3 + rng.uniform(0, 6)) + 0.1 * np.sin(aa * 7 + rng.uniform(0, 6))
    x = np.cos(aa) * rr * R * edge; z = np.sin(aa) * rr * R * edge
    y = H * np.sqrt(np.clip(1 - rr ** 2, 0, 1)) + rng.normal(0, 0.006, rr.shape) - 0.01 * rr
    pos = np.stack([x, y, z], -1).reshape(-1, 3)
    i = np.arange(nr * na).reshape(nr, na)
    tris = []
    for a in range(na):
        b = (a + 1) % na
        tris += [np.stack([i[:-1, a], i[:-1, b], i[1:, a]], 1), np.stack([i[:-1, b], i[1:, b], i[1:, a]], 1)]
    tris = np.concatenate(tris)
    # Smooth vertex normals.
    fn = np.cross(pos[tris[:, 1]] - pos[tris[:, 0]], pos[tris[:, 2]] - pos[tris[:, 0]])
    nrm = np.zeros_like(pos)
    for k in range(3):
        np.add.at(nrm, tris[:, k], fn)
    nrm = np.where(nrm[:, 1:2] < 0, -nrm, nrm)
    c = mixcol(rng, len(pos), [(96, 112, 46), (118, 124, 56), (78, 96, 40), (120, 80, 52)], [4, 3, 3, 1], 0.12)
    rgba = np.concatenate([c, np.full((len(pos), 1), 0.9)], 1)
    uv = np.stack([rng.uniform(0, 1, len(pos)), np.zeros(len(pos))], 1)
    M.add(pos, nrm, uv, rgba, tris)
    # Fine moss fronds poking up from it.
    n = 260
    b, _, rr = disk(rng, n, R * 0.9)
    b[:, 1] = H * np.sqrt(np.clip(1 - (rr / R) ** 2, 0, 1)) - 0.005
    cc = mixcol(rng, n, [(120, 138, 52), (90, 112, 42)], [1, 1])
    ribbons(M, b, rng.uniform(0, 6.3, n), rng.uniform(0.015, 0.035, n), rng.uniform(0.004, 0.007, n),
            rng.uniform(0, 0.4, n), rng.uniform(0, 0.5, n), cc * 0.8, cc * 1.1, np.full(n, 0.7), 2, flex=0.3, rng=rng)
    return M


def bracken(seed, autumn=0.25):
    """A small stand of bracken: each frond a stalk rising and arching, its blade of paired pinnae held
    nearly flat. Late summer green, some fronds turning."""
    rng = np.random.default_rng(seed)
    M = Mesh()
    nf = int(rng.integers(4, 8))
    for f in range(nf):
        a = rng.uniform(0, 2 * math.pi)
        base = np.array([rng.normal(0, 0.15), 0, rng.normal(0, 0.15)])
        H = rng.uniform(0.45, 0.85)
        d = np.array([math.cos(a), 0, math.sin(a)])
        turn = rng.uniform(0, 1) < autumn
        green = lin((84, 102, 42)) * rng.uniform(0.85, 1.1)
        colf = lin((150, 92, 42)) if turn else green
        # Stalk: up, then arching over along d.
        k = 7
        t = np.linspace(0, 1, k)
        ang = 0.1 + 1.25 * t ** 2
        L = H * 1.3
        seg = L / (k - 1)
        horiz = np.concatenate([[0], np.cumsum(np.sin(ang[1:]) * seg)]); vert = np.concatenate([[0], np.cumsum(np.cos(ang[1:]) * seg)])
        rach = base + d[None] * horiz[:, None] + np.array([0, 1.0, 0]) * vert[:, None]
        tubes(M, rach[None], np.linspace(0.004, 0.0015, k)[None], np.broadcast_to(colf * 0.7, (1, k, 3)),
              np.linspace(0.3, 0.9, k)[None], sides=3, rng=rng)
        # Pinnae along the upper 60 % of the stalk, both sides, longest in the middle.
        npin = 20
        for j in range(npin):
            u = 0.4 + 0.58 * j / (npin - 1)
            p = np.array([np.interp(u, t, rach[:, q]) for q in range(3)])
            tang = normalize(np.array([np.interp(min(u + 0.02, 1), t, rach[:, q]) for q in range(3)]) - p)
            side = normalize(np.cross(tang, np.array([0, 1.0, 0])))
            length = 0.22 * math.sin(math.pi * (0.15 + 0.85 * (1 - (u - 0.4) / 0.6))) + 0.03
            for sgn in (-1, 1):
                ribbons(M, p[None], np.array([math.atan2((side * sgn)[2], (side * sgn)[0])]), np.array([length]),
                        np.array([length * 0.38]), np.array([1.35]), np.array([0.3]),
                        (colf * 0.85)[None], (colf * 1.05)[None], np.array([0.6]), 4, taper=0.6,
                        up_normal=1.2, round_=0.0, rng=rng)
    return M


SPECIES = {
    # name: (builder, Unreal cull distance in metres, casts shadows, cull spread: thinning out to that
    # many times the cull distance instead of stopping at one line)
    # The carpet: mats laid edge to edge so the ground is grass, not soil between tufts.
    "carpet_green": (lambda: carpet(101, 0.95, 1700, (0.05, 0.15), (0.004, 0.0075), [GREEN, GREEN_DK, YELLOW, DEAD],
                                    [5, 4, 1, 1], [GREEN, YELLOW, PALE], [5, 1, 1], segs=2, flowers=10), 36, False, 1.4),
    "carpet_moor": (lambda: carpet(102, 0.95, 1600, (0.06, 0.18), (0.0035, 0.0065), [GREEN_DK, (78, 84, 44), DEAD, STRAW],
                                   [4, 3, 2, 1], [PALE, STRAW, YELLOW, GREEN], [3, 2, 2, 2], segs=2, tall=0.07), 36, False, 1.4),
    "carpet_far_green": (lambda: carpet(111, 2.0, 120, (0.08, 0.2), (0.01, 0.02), [GREEN, GREEN_DK, YELLOW],
                                        [5, 3, 2], [GREEN, YELLOW, PALE], [4, 2, 1], segs=2), 130, False, 1.0),
    "carpet_far_moor": (lambda: carpet(112, 2.0, 120, (0.09, 0.22), (0.01, 0.02), [GREEN, YELLOW, DEAD],
                                       [4, 3, 2], [STRAW, PALE, YELLOW], [3, 2, 2], segs=2), 130, False, 1.0),
    "tuft_a": (lambda: grass_clump(21, 150, 0.08, (0.16, 0.38), (0.003, 0.005), 0.7, (0.3, 1.0),
                                   [GREEN, GREEN_DK, YELLOW], [5, 3, 2], [GREEN, YELLOW, PALE], [3, 2, 1], segs=3), 40, False, 1.6),
    "tuft_b": (lambda: grass_clump(22, 130, 0.07, (0.14, 0.32), (0.003, 0.005), 0.8, (0.4, 1.2),
                                   [GREEN, YELLOW, DEAD], [5, 2, 1], [GREEN, PALE, STRAW], [3, 2, 1], segs=3), 40, False, 1.6),
    "tussock_a": (lambda: grass_clump(31, 170, 0.1, (0.35, 0.7), (0.004, 0.007), 0.8, (0.4, 1.3),
                                      [GREEN, YELLOW, DEAD, STRAW], [4, 3, 2, 2], [STRAW, PALE, YELLOW], [4, 2, 1], segs=4, ao=0.18), 55, True, 1.6),
    "tussock_b": (lambda: grass_clump(32, 150, 0.09, (0.3, 0.6), (0.004, 0.007), 0.9, (0.6, 1.5),
                                      [GREEN, GREEN_DK, DEAD], [4, 3, 3], [STRAW, YELLOW, DEAD], [3, 2, 2], segs=4, ao=0.18), 55, True, 1.6),
    "matgrass": (lambda: grass_clump(41, 130, 0.05, (0.15, 0.32), (0.002, 0.0035), 0.35, (0.05, 0.4),
                                     [PALE, YELLOW, GREEN], [3, 2, 2], [PALE, STRAW], [3, 2], segs=3), 40, False, 1.6),
    "far_a": (lambda: grass_clump(51, 40, 0.16, (0.35, 0.6), (0.012, 0.02), 0.9, (0.4, 1.1),
                                  [GREEN, YELLOW, DEAD], [5, 3, 1], [YELLOW, STRAW, PALE], [3, 2, 1], segs=3, ao=0.3), 150, False, 1.0),
    "far_b": (lambda: grass_clump(52, 36, 0.14, (0.3, 0.55), (0.012, 0.02), 1.0, (0.5, 1.2),
                                  [GREEN, GREEN_DK, YELLOW], [4, 3, 2], [STRAW, YELLOW, GREEN], [3, 2, 2], segs=3, ao=0.3), 150, False, 1.0),
    "rush_a": (lambda: rush(61), 80, True, 1.3),
    "rush_b": (lambda: rush(62), 80, True, 1.3),
    "heather_twig_a": (lambda: heather_twigs(73), 34, True, 1.3),
    "heather_twig_b": (lambda: heather_twigs(74, flowering=0.06), 34, True, 1.3),
    "heather_a": (lambda: heather(71), 70, False, 1.4),
    "heather_b": (lambda: heather(72, flowering=0.1), 70, False, 1.4),
    "moss": (lambda: moss(81), 25, False, 1.4),
    "bracken": (lambda: bracken(91), 90, True, 1.3),
}


# Laid tilted to the ground (mats); everything else grows straight up.
ALIGN = {"carpet_green", "carpet_moor", "carpet_far_green", "carpet_far_moor", "moss"}
# Drawn only beyond this many metres (the fine carpet covers nearer), in chunks this size so the
# minimum draw distance works chunk by chunk. Everything else: 256 m chunks, drawn from 0.
MIN_DRAW = {"carpet_far_green": (30.0, 32.0), "carpet_far_moor": (30.0, 32.0)}
# How much of the ground's colour each species takes (1 = the full blend; heather keeps its own).
GROUND_BLEND = {"heather": 0.15, "carpet": 0.7}


# --- The land --------------------------------------------------------------------------------------

def read_glb(path):
    b = open(path, "rb").read()
    jl = struct.unpack("<I", b[12:16])[0]
    js = json.loads(b[20:20 + jl]); bin_ = b[20 + jl + 8:]
    prim = js["meshes"][0]["primitives"][0]

    def acc(i):
        a = js["accessors"][i]; v = js["bufferViews"][a["bufferView"]]
        dt = {5126: np.float32, 5121: np.uint8, 5123: np.uint16, 5125: np.uint32}[a["componentType"]]
        n = {"VEC3": 3, "VEC4": 4, "VEC2": 2, "SCALAR": 1}[a["type"]]
        return np.frombuffer(bin_[v["byteOffset"]:v["byteOffset"] + v["byteLength"]], dt)[:a["count"] * n].reshape(-1, n)
    out = {k: acc(i) for k, i in prim["attributes"].items()}
    out["indices"] = acc(prim["indices"]).reshape(-1, 3)
    return out


class Land:
    """The terrain tiles stitched back into one 2 m grid: height, cover, slope, distance to the road."""

    def __init__(self, route):
        self.step = route["step_m"]
        tiles = route["tiles"]
        x0 = min(t["min"][0] for t in tiles); z0 = min(t["min"][2] for t in tiles)
        x1 = max(t["max"][0] for t in tiles); z1 = max(t["max"][2] for t in tiles)
        self.x0, self.z0 = x0, z0
        nx = int(round((x1 - x0) / self.step)) + 1; nz = int(round((z1 - z0) / self.step)) + 1
        self.H = np.full((nz, nx), np.nan); self.rgb = np.zeros((nz, nx, 3))
        self.tiles = tiles
        for t in tiles:
            g = read_glb(os.path.join(DATA, t["name"] + ".glb"))
            P = g["POSITION"].astype(np.float64)
            i = np.round((P[:, 0] - x0) / self.step).astype(int); j = np.round((P[:, 2] - z0) / self.step).astype(int)
            self.H[j, i] = P[:, 1]
            self.rgb[j, i] = g["COLOR_0"][:, :3]
        self.have = ~np.isnan(self.H)
        Hf = np.where(self.have, self.H, np.nanmean(self.H))
        self.Hf = Hf
        gz, gx = np.gradient(Hf, self.step)
        self.gx, self.gz = gx, gz
        self.slope = np.hypot(gx, gz)
        # Cover: nearest of the export's cover colours; rockiness: how far it was blended to scree.
        names = list(COVER_RGB)
        ref = np.power(np.array([COVER_RGB[k] for k in names], np.float64) / 255.0, 2.2) * 255
        d = ((self.rgb[..., None, :] - ref[None, None]) ** 2).sum(-1)
        self.cover = np.array(names)[d.argmin(-1)]
        rock = np.power(np.array((118, 114, 104)) / 255.0, 2.2) * 255
        self.rock = np.clip(1 - np.sqrt(((self.rgb - rock) ** 2).sum(-1)) / 12.0, 0, 1)
        self.rock = np.maximum(self.rock, np.clip((self.slope - 0.6) / 0.4, 0, 1))
        # Wetness: flat ground in hollows (smoothed height above its neighbourhood is negative).
        sm = blur(Hf, 4); big = blur(Hf, 20)
        hollow = np.clip((big - sm) / 1.5, -1, 1)
        flat = np.clip(1 - self.slope / 0.18, 0, 1)
        self.wet = np.clip(0.55 * flat + 0.6 * hollow, 0, 1)

    def cell(self, x, z):
        i = np.clip(np.round((x - self.x0) / self.step).astype(int), 0, self.H.shape[1] - 1)
        j = np.clip(np.round((z - self.z0) / self.step).astype(int), 0, self.H.shape[0] - 1)
        return j, i

    def height(self, x, z):
        """The terrain mesh's own surface: tiles are split into triangles, so interpolate on them."""
        fx = np.clip((x - self.x0) / self.step, 0, self.H.shape[1] - 1.001)
        fz = np.clip((z - self.z0) / self.step, 0, self.H.shape[0] - 1.001)
        i = fx.astype(int); j = fz.astype(int); u = fx - i; v = fz - j
        H = self.Hf
        h00, h10, h01, h11 = H[j, i], H[j, i + 1], H[j + 1, i], H[j + 1, i + 1]
        # Both diagonals bracket the true surface closely; bilinear is within a centimetre or two.
        return h00 * (1 - u) * (1 - v) + h10 * u * (1 - v) + h01 * (1 - u) * v + h11 * u * v

    def has(self, x, z):
        j, i = self.cell(x, z)
        inside = (x >= self.x0) & (z >= self.z0) & (x <= self.x0 + (self.H.shape[1] - 1) * self.step) & \
                 (z <= self.z0 + (self.H.shape[0] - 1) * self.step)
        return inside & self.have[j, i]

    def tile_of(self, x, z):
        return (np.floor(x / 256.0).astype(int), np.floor(z / 256.0).astype(int))


def blur(a, sigma_cells):
    """Gaussian blur by separable convolution (numpy only)."""
    r = int(3 * sigma_cells)
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma_cells) ** 2); k /= k.sum()
    p = np.pad(a, r, mode="edge")
    p = np.apply_along_axis(lambda m: np.convolve(m, k, mode="valid"), 1, p)
    p = np.apply_along_axis(lambda m: np.convolve(m, k, mode="valid"), 0, p)
    return p


def road_distance(x, z, line):
    """Horizontal distance from points to the road's centre line (segments), in metres."""
    best = np.full(len(x), 1e9)
    A = line[:-1][:, [0, 2]]; B = line[1:][:, [0, 2]]
    for s in range(0, len(A), 16):
        a = A[s:s + 16]; b = B[s:s + 16]
        ab = b - a; L2 = (ab ** 2).sum(1)
        px = x[:, None] - a[None, :, 0]; pz = z[:, None] - a[None, :, 1]
        t = np.clip((px * ab[None, :, 0] + pz * ab[None, :, 1]) / L2[None], 0, 1)
        d = np.hypot(px - t * ab[None, :, 0], pz - t * ab[None, :, 1])
        best = np.minimum(best, d.min(1))
    return best


class Noise:
    """Smooth value noise from a few random grids: big soft patches for where each plant grows."""

    def __init__(self, seed):
        self.rng = np.random.default_rng(seed)
        self.grids = {}

    def __call__(self, x, z, cell, key=0):
        if (cell, key) not in self.grids:
            self.grids[(cell, key)] = self.rng.uniform(0, 1, (512, 512))
        g = self.grids[(cell, key)]
        fx = x / cell + 256; fz = z / cell + 256
        i = np.floor(fx).astype(int) % 511; j = np.floor(fz).astype(int) % 511
        u = fx - np.floor(fx); v = fz - np.floor(fz)
        u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v)
        return (g[j, i] * (1 - u) * (1 - v) + g[j, i + 1] * u * (1 - v) + g[j + 1, i] * (1 - u) * v + g[j + 1, i + 1] * u * v)

    def fbm(self, x, z, cell, key=0):
        return (self(x, z, cell, key) * 0.6 + self(x, z, cell / 2.7, key + 1) * 0.3 + self(x, z, cell / 7.3, key + 2) * 0.1)


def blocked_mask(land):
    """1 m cells where the countryside detail stands above the ground (walls, barns, rocks, sheep) or
    the becks run (Data/Buttertubs/Becks)."""
    step = 1.0
    nx = int((land.H.shape[1] - 1) * land.step / step) + 1; nz = int((land.H.shape[0] - 1) * land.step / step) + 1
    m = np.zeros((nz, nx), bool)
    becks = os.path.join(DATA, "Becks")   # the becks' beds and culvert walls (Tools/make_becks.py): nothing grows on them
    files = [os.path.join(DATA, f) for f in sorted(os.listdir(DATA)) if f.startswith("detail_") and f.endswith(".glb")]
    if os.path.isdir(becks):
        files += [os.path.join(becks, f) for f in sorted(os.listdir(becks)) if f.startswith(("bed_", "wall_")) and f.endswith(".glb")]
    for path in files:
        g = read_glb(path)
        P = g["POSITION"].astype(np.float64); T = g["indices"].astype(np.int64)
        up = P[:, 1] - land.height(P[:, 0], P[:, 2]) > (0.12 if "Becks" not in path else -1.0)
        tri = T[up[T].any(1)]
        # Points over each raised triangle, so long faces leave no gaps.
        w = np.random.default_rng(1).dirichlet((1, 1, 1), 12)
        pts = np.concatenate([P[tri[:, 0]] * a + P[tri[:, 1]] * b + P[tri[:, 2]] * c for a, b, c in w]
                             + [P[tri[:, k]] for k in range(3)])
        i = np.round((pts[:, 0] - land.x0) / step).astype(int); j = np.round((pts[:, 2] - land.z0) / step).astype(int)
        ok = (i >= 0) & (j >= 0) & (i < nx) & (j < nz)
        m[j[ok], i[ok]] = True
    # Grow by a cell: plants may touch a wall's foot but not stand in it.
    g = m.copy()
    g[1:] |= m[:-1]; g[:-1] |= m[1:]; g[:, 1:] |= m[:, :-1]; g[:, :-1] |= m[:, 1:]
    return g, step


# --- Where things grow -----------------------------------------------------------------------------

def scatter(rng, land, spacing, near_road, line, road_half, jitter=1.0, min_d=0.0):
    """Jittered grid points over the land within `near_road` metres of the road's edge."""
    x0, z0 = land.x0, land.z0
    x1 = x0 + (land.H.shape[1] - 1) * land.step; z1 = z0 + (land.H.shape[0] - 1) * land.step
    # Only the band along the road: work in blocks so memory stays small.
    lx, lz = line[:, 0], line[:, 2]
    out_x, out_z, out_d = [], [], []
    B = 128.0
    for bx in np.arange(x0, x1, B):
        for bz in np.arange(z0, z1, B):
            # Skip blocks far from the road.
            dmin = np.hypot(np.clip(lx, bx, bx + B) - lx, np.clip(lz, bz, bz + B) - lz).min()
            if dmin > near_road + road_half + 1:
                continue
            gx = np.arange(bx, min(bx + B, x1), spacing); gz = np.arange(bz, min(bz + B, z1), spacing)
            X, Z = np.meshgrid(gx, gz)
            if jitter < 1.0:   # a staggered (hex-like) grid with a little jitter: mats meet without gaps
                X = X + (np.arange(len(gz))[:, None] % 2) * spacing * 0.5
            X = X.ravel() + rng.uniform(0, spacing, X.size) * jitter; Z = Z.ravel() + rng.uniform(0, spacing, Z.size) * jitter
            keep = land.has(X, Z)
            X, Z = X[keep], Z[keep]
            if not len(X):
                continue
            d = road_distance(X, Z, line) - road_half
            keep = (d > 0) & (d < near_road)
            out_x.append(X[keep]); out_z.append(Z[keep]); out_d.append(d[keep])
    if not out_x:
        return np.zeros(0), np.zeros(0), np.zeros(0)
    return np.concatenate(out_x), np.concatenate(out_z), np.concatenate(out_d)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def plan(land, line, road_half, density, rng):
    """Every plant: species name -> (x, y, z in metres, yaw radians, scale)."""
    noise = Noise(5)
    blocked, bstep = blocked_mask(land)

    def fields(x, z):
        j, i = land.cell(x, z)
        f = {"slope": land.slope[j, i], "wet": land.wet[j, i], "rock": land.rock[j, i], "cover": land.cover[j, i],
             "y": land.Hf[j, i]}
        bi = np.clip(np.round((x - land.x0) / bstep).astype(int), 0, blocked.shape[1] - 1)
        bj = np.clip(np.round((z - land.z0) / bstep).astype(int), 0, blocked.shape[0] - 1)
        f["blocked"] = blocked[bj, bi]
        # Patches: the open moor's mix changes over tens of metres.
        f["molinia"] = noise.fbm(x, z, 45.0, 0)          # purple moor grass stands
        f["heath"] = noise.fbm(x, z, 30.0, 10)           # heather patches
        f["dry"] = noise.fbm(x, z, 60.0, 20)             # drier knolls (mat grass)
        f["fine"] = noise(x, z, 3.0, 30)                 # small-scale gaps and clusters
        f["wetn"] = np.clip(f["wet"] + (noise.fbm(x, z, 18.0, 40) - 0.5) * 0.6, 0, 1)
        return f

    def ok(f):
        return (~f["blocked"]) & (f["rock"] < 0.6) & (f["slope"] < 1.1)

    out = {}

    def put(name, x, z, scale, sink=0.03):
        y = land.height(x, z)
        # Sink a little more on slopes, so the downhill side of the base doesn't float.
        y = y - sink - np.clip(land.slope[land.cell(x, z)], 0, 1) * 0.08 * scale
        yaw = rng.uniform(0, 2 * math.pi, len(x))
        if name in ALIGN:
            j, i = land.cell(x, z)
            nrm = normalize(np.stack([-land.gx[j, i], np.ones(len(x)), -land.gz[j, i]], 1))
            y = land.height(x, z) - sink              # tilted, the mat needs no extra sinking
        else:
            nrm = np.tile([0.0, 1.0, 0.0], (len(x), 1))
        prev = out.get(name)
        cur = (np.stack([x, y, z], 1), yaw, scale, nrm)
        if prev is not None:
            cur = tuple(np.concatenate([a_, b_]) for a_, b_ in zip(prev, cur))
        out[name] = cur

    def keep(p):
        return rng.uniform(0, 1, len(p)) < np.clip(p * density, 0, 1)

    # The carpet: grass mats laid edge to edge on a staggered grid, so the ground near the road is a
    # continuous sward. Fine mats (36 m, thinning to 50 m) within 48 m of the road; coarse mats with
    # wider blades everywhere to 140 m, drawn only beyond about 30 m (their own small chunks, so
    # Unreal's minimum draw distance can drop the near ones). Greener on the verge and the grazed
    # ground, tighter and darker (mat grass, wavy hair grass) on the open moor.
    for spacing, reach, names, mind in ((1.45, 48.0, ("carpet_green", "carpet_moor"), 1.1),
                                        (3.0, 140.0, ("carpet_far_green", "carpet_far_moor"), 2.1)):
        x, z, d = scatter(rng, land, spacing, reach, line, road_half, jitter=0.25)
        f = fields(x, z)
        k = (ok(f) | (f["rock"] < 0.8)) & ~f["blocked"] & (d > mind)
        x, z, d = x[k], z[k], d[k]
        f = {kk: v[k] for kk, v in f.items()}
        moor = (smoothstep(0.4, 0.6, f["molinia"] + 0.3 * f["dry"] - 0.15) * (d > 4) + (f["cover"] == "shrub") * 0.3) \
            > rng.uniform(0, 1, len(x))
        sc = rng.uniform(0.9, 1.15, len(x))
        put(names[1], x[moor], z[moor], sc[moor], 0.02); put(names[0], x[~moor], z[~moor], sc[~moor], 0.02)
    # ...and a closer row along the verge, so the grass runs right up to the tarmac's edge.
    x, z, d = scatter(rng, land, 0.85, 1.5, line, road_half, jitter=0.2)
    f = fields(x, z)
    k = ~f["blocked"] & (d > 0.75)
    put("carpet_green", x[k], z[k], rng.uniform(0.75, 0.9, k.sum()), 0.02)

    # Grass clumps: the main cover, within 50 m of the road.
    x, z, d = scatter(rng, land, 0.6, 58.0, line, road_half)
    f = fields(x, z)
    base = ok(f) * (1.0 - 0.6 * smoothstep(12, 58, d)) * (0.6 + 0.4 * f["fine"])
    base = base * (1 - 0.7 * smoothstep(0.55, 0.85, f["wetn"]))           # rushes take the wet ground
    heath = (f["cover"] == "shrub") * smoothstep(0.6, 0.72, f["heath"]) * (f["wetn"] < 0.6)
    base = base * (1 - 0.75 * heath)
    k = keep(base)
    x, z, d = x[k], z[k], d[k]
    f = {kk: v[k] for kk, v in f.items()}
    verge = d < 2.5
    mol = smoothstep(0.45, 0.65, f["molinia"]) * (d > 3)
    mat = smoothstep(0.55, 0.7, f["dry"]) * (1 - mol) * (d > 2)
    u = rng.uniform(0, 1, len(x))
    kind = np.where(u < mol, 1, np.where(u < mol + mat, 2, 0))
    kind[verge] = 0
    scale = rng.uniform(0.75, 1.3, len(x)) * np.where(verge, 0.7, 1.0)      # the verge is cut and grazed
    ab = rng.uniform(0, 1, len(x)) < 0.5
    for name, m in (("tuft_a", (kind == 0) & ab), ("tuft_b", (kind == 0) & ~ab), ("tussock_a", (kind == 1) & ab),
                    ("tussock_b", (kind == 1) & ~ab), ("matgrass", kind == 2)):
        put(name, x[m], z[m], scale[m])

    # Coarse clumps for the middle distance, everywhere to 160 m (including near: they fill under the fine grass).
    x, z, d = scatter(rng, land, 2.1, 140.0, line, road_half)
    f = fields(x, z)
    p = ok(f) * (d > 0.8) * (1 - 0.8 * smoothstep(0.6, 0.85, f["wetn"])) * 0.85
    k = keep(p)
    ab = rng.uniform(0, 1, k.sum()) < 0.5
    sc = rng.uniform(0.8, 1.3, k.sum())
    put("far_a", x[k][ab], z[k][ab], sc[ab]); put("far_b", x[k][~ab], z[k][~ab], sc[~ab])

    # Soft rush in the wet ground, and a few scattered clumps anywhere (as in every Dales pasture).
    x, z, d = scatter(rng, land, 1.4, 95.0, line, road_half)
    f = fields(x, z)
    p = ok(f) * (d > 1.2) * (smoothstep(0.5, 0.8, f["wetn"]) * 0.9 + 0.02)
    k = keep(p)
    ab = rng.uniform(0, 1, k.sum()) < 0.5
    sc = rng.uniform(0.75, 1.25, k.sum())
    put("rush_a", x[k][ab], z[k][ab], sc[ab], 0.05); put("rush_b", x[k][~ab], z[k][~ab], sc[~ab], 0.05)

    # Heather in patches on the heath cover (most of the summit is mapped as shrub), drier ground.
    x, z, d = scatter(rng, land, 0.85, 75.0, line, road_half)
    f = fields(x, z)
    heath = ((f["cover"] == "shrub") * 1.0 + (f["cover"] == "grass") * 0.25) * smoothstep(0.6, 0.7, f["heath"])
    p = ok(f) * (d > 1.5) * heath * (1 - smoothstep(0.5, 0.75, f["wetn"]))
    k = keep(p)
    ab = rng.uniform(0, 1, k.sum()) < 0.6
    sc = rng.uniform(0.75, 1.3, k.sum())
    # Real twiggy bushes where the rider sees them (within 22 m of the road), the cheap mounds beyond.
    near = d[k] < 22
    for name, m in (("heather_twig_a", near & ab), ("heather_twig_b", near & ~ab),
                    ("heather_a", ~near & ab), ("heather_b", ~near & ~ab)):
        put(name, x[k][m], z[k][m], sc[m], 0.04)

    # Moss cushions in the wettest spots near the road.
    x, z, d = scatter(rng, land, 1.5, 30.0, line, road_half)
    f = fields(x, z)
    p = ok(f) * (d > 1.0) * smoothstep(0.65, 0.9, f["wetn"]) * 0.8
    k = keep(p)
    put("moss", x[k], z[k], rng.uniform(0.7, 1.4, k.sum()), 0.02)

    # Bracken on the steeper, drier slopes below the summit (the drop towards Thwaite).
    x, z, d = scatter(rng, land, 1.1, 105.0, line, road_half)
    f = fields(x, z)
    low = smoothstep(-40, -90, f["y"])
    p = ok(f) * (d > 2) * low * smoothstep(0.25, 0.45, f["slope"]) * (1 - smoothstep(0.4, 0.6, f["wetn"])) \
        * smoothstep(0.4, 0.6, noise.fbm(x, z, 35.0, 50)) * 0.9
    k = keep(p)
    put("bracken", x[k], z[k], rng.uniform(0.8, 1.25, k.sum()), 0.03)
    return out


# --- Writing ---------------------------------------------------------------------------------------

MAGIC = b"TSF2"


def write_instances(path, out, species_order):
    """instances.bin: zlib of a little-endian stream.

        "TSF2", uint32 chunk count, then per chunk:
            uint16 species index (into foliage.json's species list), uint16 0,
            float32 origin x, y, z (Unreal cm), uint32 count,
            count x (uint16 dx, uint16 dy, uint16 dz in cm from the origin; uint8 yaw (x 360/256 deg);
                     uint8 scale (x 1/128); int8 nx, ny: the ground normal's Unreal x and y (x 1/127),
                     0, 0 for an upright plant)
    Chunks are one species in one 256 m tile (smaller for species with a minimum draw distance), so
    each Unreal component's bounds stay small.
    """
    chunks = []
    total = 0
    for si, name in enumerate(species_order):
        if name not in out:
            continue
        pos, yaw, scale, nrm = out[name]
        if not len(pos):
            continue
        ue = np.stack([pos[:, 0] * 100, pos[:, 2] * 100, pos[:, 1] * 100], 1)
        cs = MIN_DRAW.get(name, (0.0, 256.0))[1]
        tx = np.floor(pos[:, 0] / cs).astype(int); tz = np.floor(pos[:, 2] / cs).astype(int)
        for key in sorted(set(zip(tx.tolist(), tz.tolist()))):
            m = (tx == key[0]) & (tz == key[1])
            p = ue[m]
            o = np.floor(p.min(0)) - 1
            q = np.round(p - o).astype(np.int64)
            assert q.max() < 65536, "tile too tall for 16 bits"
            yw = (np.round(yaw[m] / (2 * math.pi) * 256).astype(int) % 256).astype(np.uint8)
            sc = np.clip(np.round(scale[m] * 128), 1, 255).astype(np.uint8)
            rec = np.zeros(len(p), dtype=[("x", "<u2"), ("y", "<u2"), ("z", "<u2"), ("yaw", "u1"), ("s", "u1"),
                                          ("nx", "i1"), ("ny", "i1")])
            # The ground's normal in Unreal's frame (x east, y south), x and y; z follows.
            nq = np.clip(np.round(nrm[m] * 127), -127, 127).astype(np.int8)
            rec["x"], rec["y"], rec["z"], rec["yaw"], rec["s"] = q[:, 0], q[:, 1], q[:, 2], yw, sc
            rec["nx"], rec["ny"] = nq[:, 0], nq[:, 2]
            chunks.append(struct.pack("<HHfffI", si, 0, *o.tolist(), len(p)) + rec.tobytes())
            total += len(p)
    raw = MAGIC + struct.pack("<I", len(chunks)) + b"".join(chunks)
    comp = zlib.compress(raw, 9)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"TSFZ", len(raw), len(comp)) + comp)
    return total, len(comp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--density", type=float, default=1.0, help="multiplies how many plants are placed")
    ap.add_argument("--meshes-only", action="store_true")
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    route = json.load(open(os.path.join(DATA, "route.json")))
    info = {"about": "Moorland ground cover made by Tools/make_foliage.py; see its docstring.", "species": []}
    names = list(SPECIES)
    for name in names:
        build, cull, shadow, spread = SPECIES[name]
        tris = build().write(os.path.join(OUT, name + ".glb"), name)
        info["species"].append({"name": name, "cull_m": cull, "cull_spread": spread, "shadow": shadow, "triangles": tris,
                                 "ground_blend": next((v for k, v in GROUND_BLEND.items() if name.startswith(k)), 1.0),
                                 "min_draw_m": MIN_DRAW.get(name, (0.0, 256.0))[0]})
        print("%-10s %6d triangles" % (name, tris))
    if args.meshes_only:
        return
    line = np.array(route.get("road_line_sim", route["line_sim"]), np.float64)   # the whole ribbon, run-outs too
    land = Land(route)
    out = plan(land, line, route["road_half_width_m"], args.density, np.random.default_rng(3))
    total, size = write_instances(os.path.join(OUT, "instances.bin"), out, names)
    for s in info["species"]:
        s["count"] = int(len(out[s["name"]][0])) if s["name"] in out else 0
        print("%-10s %8d placed" % (s["name"], s["count"]))
    info["total"] = total
    info["density"] = args.density
    with open(os.path.join(OUT, "foliage.json"), "w") as f:
        json.dump(info, f, indent=1)
    print("%d plants, instances.bin %.1f MB" % (total, size / 1e6))


if __name__ == "__main__":
    main()
