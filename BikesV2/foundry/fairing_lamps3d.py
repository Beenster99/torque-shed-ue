"""Bike Models V2: headlight units for faired bikes (beenster, 16 Nov: "separate parts we design which should
poke through the existing fairings ... the lenses and lights are intricate and detailed").

A unit is its own part behind an opening cut in the fairing: a gloss bezel round the opening, a dark housing
bucket, a chrome reflector with flutes, projector lamps (a glass lens in a shroud, some with a halo ring), a
running-light guide in its carrier, rows of LED cells, and a clear outer lens standing a little proud of the paint.
Each brand has its own unit: our own designs, with real lamps as reference only.

    fairing_lamps3d.fit(detail3d, model, c)      # called from detail3d.nose_lamps when the model names a unit
"""
import math

import bmesh
import bpy

# A unit, drawn square-on from the front for the rider's left side (y from the centre line, z up; z is lifted to the
# bike's own nose height when fitted). "proj": (y, z, lens radius, halo ring). "drl": the running light's line.
# "cells": LED cells (y, z, half size). "flute": the reflector's rib pitch in mm (0 = smooth facets).
UNITS = {
    # a long eye slanting up and out, edged along its top by the running light, two projectors
    "slant": dict(ky=0.9, outline=[(24, 768), (70, 763), (126, 789), (154, 822), (139, 831), (84, 813), (28, 801)],
                  drl=[(32, 796), (84, 808), (138, 826)], proj=[(54, 782, 13.5, False), (90, 794, 11.5, False)], cells=[(118, 808, 4.5), (130, 815, 4.5)], flute=0),
    # a wedge that starts low by the centre line and rises outwards, the running light a heavy brow, the outer projector haloed
    "vface": dict(ky=0.9, outline=[(12, 763), (46, 760), (112, 789), (152, 827), (133, 830), (70, 808), (16, 783)],
                  drl=[(20, 779), (70, 802), (132, 825)], proj=[(42, 772, 10.5, False), (80, 788, 13, True)], cells=[(112, 803, 4.5)], flute=5),
    # one big haloed projector in a deep fluted reflector, a short running-light bar along the bottom
    "halo": dict(ky=0.86, outline=[(26, 761), (98, 760), (142, 783), (148, 819), (98, 828), (34, 808)],
                 drl=[(36, 768), (96, 767)], proj=[(70, 793, 19, True)], cells=[(118, 792, 5.5), (128, 806, 5.5), (112, 812, 5.5)], flute=6),
    # a tall wedge close to the centre line, two projectors stacked, the running light up the outer edge
    "stack": dict(outline=[(14, 754), (56, 758), (78, 828), (54, 838), (18, 814)],
                  drl=[(52, 766), (69, 822)], proj=[(34, 777, 13, False), (42, 808, 12, True)], cells=[(26, 794, 4), (58, 800, 4)], flute=4),
}


def _inside(p, poly):
    x, y, c = p[0], p[1], False
    for i in range(len(poly)):
        (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % len(poly)]
        if (y0 > y) != (y1 > y) and x < x0 + (x1 - x0) * (y - y0) / (y1 - y0):
            c = not c
    return c


def _edge_dist(p, poly):
    best = 1e9
    for i in range(len(poly)):
        (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % len(poly)]
        dx, dy = x1 - x0, y1 - y0
        t = max(0.0, min(1.0, ((p[0] - x0) * dx + (p[1] - y0) * dy) / (dx * dx + dy * dy or 1.0)))
        best = min(best, math.hypot(p[0] - x0 - t * dx, p[1] - y0 - t * dy))
    return best


def cut(D, prefixes, poly, xmin):
    """Open the fairing: every face of the named parts that lies inside the outline (seen from the front, and no
    deeper than the lamp) goes, after the faces round the outline have been cut finer so the edge follows it."""
    MM = D.B.MM
    ys, zs = [q[0] for q in poly], [q[1] for q in poly]
    box = (min(ys) - 14, max(ys) + 14, min(zs) - 14, max(zs) + 14)
    for o in bpy.data.objects:
        if o.type != "MESH" or not o.name.startswith(tuple(prefixes)):
            continue
        mw = o.matrix_world
        bm = bmesh.new()
        bm.from_mesh(o.data)

        def near(f):
            co = [mw @ v.co for v in f.verts]
            return (max(q.y for q in co) / MM > box[0] and min(q.y for q in co) / MM < box[1] and max(q.z for q in co) / MM > box[2]
                    and min(q.z for q in co) / MM < box[3] and max(q.x for q in co) / MM > xmin - 70)
        fs = [f for f in bm.faces if near(f)]
        if fs:
            bmesh.ops.triangulate(bm, faces=fs)
        for _ in range(8):
            es = list({e for f in bm.faces if near(f) for e in f.edges if e.calc_length() / MM > 4.0})
            if not es:
                break
            bmesh.ops.subdivide_edges(bm, edges=es, cuts=1)
            bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 4])
        gone = []
        for f in bm.faces:
            cc = mw @ f.calc_center_median()
            if cc.x / MM > xmin - 70 and _inside((cc.y / MM, cc.z / MM), poly):
                gone.append(f)
        if gone:
            cs = [mw @ f.calc_center_median() for f in gone]
            if False: print("lamp cut", o.name, len(gone), [round(min(getattr(q, a) for q in cs) / MM) for a in "xyz"], [round(max(getattr(q, a) for q in cs) / MM) for a in "xyz"], "poly y", round(min(ys)), round(max(ys)))
            bmesh.ops.delete(bm, geom=gone, context="FACES")
            bm.to_mesh(o.data)
            o.data.update()
        bm.free()


def _hull(pts):
    pts = sorted(set((round(a, 1), round(b, 1)) for a, b in pts))
    cross = lambda o, a, b: (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, up = [], []
    for q in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], q) <= 0:
            lo.pop()
        lo.append(q)
    for q in reversed(pts):
        while len(up) >= 2 and cross(up[-2], up[-1], q) <= 0:
            up.pop()
        up.append(q)
    return lo[:-1] + up[:-1]


def adventure(D, model, adv, prefixes=("advmask",)):
    """An adventure bike's lamp unit, from the brand's own face in its "adv" block: the eye and running light give
    the opening, the projector points the lamps. A face drawn from the centre line is one unit across the bike."""
    if model.get("detail", {}).get("lamp_unit") == "off" or not adv.get("eye"):
        return False
    eye = [tuple(q) for q in adv["eye"]]
    dp = [tuple(q) for q in adv.get("drl", [])]
    h_ = len(dp) // 2
    line = [((dp[i][0] + dp[-1 - i][0]) / 2, (dp[i][1] + dp[-1 - i][1]) / 2) for i in range(h_)] if h_ >= 2 else []
    pts = eye + dp
    central = min(q[0] for q in pts) < 4
    ey, ez = sum(q[0] for q in eye) / len(eye), sum(q[1] for q in eye) / len(eye)
    if central:
        pts = pts + [(-y, z) for y, z in pts]
        ey = 0.0
    hull = _hull(pts)
    # beenster, Nov 16: the adventure lamps should all be bigger. Grown about the eye's centre, as far as the mask allows
    import bpy
    ky_, kz_ = adv.get("unit_scale", (1.5, 1.55))
    bb = [o.matrix_world @ D.Vector(q) for o in bpy.data.objects if o.name.startswith(prefixes) for q in o.bound_box]
    ymax = max(q.y for q in bb) / D.B.MM - 14 if bb else 1e9
    z0_, z1_ = (min(q.z for q in bb) / D.B.MM + 6, max(q.z for q in bb) / D.B.MM - 8) if bb else (-1e9, 1e9)
    ymin = -ymax if central else 7.0

    def grow(q, pad=0.0):
        y, z = ey + (q[0] - ey) * ky_, ez + (q[1] - ez) * kz_
        return (min(max(y, ymin + pad), ymax - pad), min(max(z, z0_ + pad), z1_ - pad))
    hull = [grow(q) for q in hull[::-1]]
    pr = 11.5 * min(ky_, kz_) * 0.9
    proj = [grow((py, pz), pr + 9) + (pr, n_ == 0) for n_, (py, pz) in enumerate(adv.get("proj", []))]
    line = [grow(q, 5) for q in line]
    # the opening always takes in its own projectors
    ring_ = [(py + (r + 9) * math.cos(a * math.pi / 6), pz + (r + 9) * math.sin(a * math.pi / 6)) for py, pz, r, _ in proj for a in range(12)]
    hull = _hull(hull + ring_)[::-1]
    if central:
        proj = proj + [(-py, pz, r, h) for py, pz, r, h in proj if py > 1]
        line = line + [(-y, z) for y, z in line[::-1] if y > 1] if line else line
    spec = dict(outline=hull, proj=proj, drl=line, cells=[], flute=5)
    made = [unit(D, f"lampunit{s}", s, spec, 0.0, prefixes, depth=52.0) for s in ((1,) if central else (-1, 1))]
    return all(made)


def unit(D, name, s, spec, dz, prefixes=("cowl",), depth=64.0):
    """Build one lamp unit on side s (1 = rider's left) and open the fairing for it. Returns False if the fairing
    has no surface there."""
    B = D.B
    trees = D._trees(prefixes)
    raw = [(s * y * spec.get("ky", 1.0), z + dz) for y, z in spec["outline"]]
    # crisp corners, eased once, then points every few mm so the edge can follow the fairing's curve
    eased, poly = D.chaikin(raw, 2), []
    for i in range(len(eased)):
        (y0, z0), (y1, z1) = eased[i], eased[(i + 1) % len(eased)]
        n_ = max(1, int(math.hypot(y1 - y0, z1 - z0) / 5))
        poly += [(y0 + (y1 - y0) * q / n_, z0 + (z1 - z0) * q / n_) for q in range(n_)]
    cy, cz = sum(q[0] for q in poly) / len(poly), sum(q[1] for q in poly) / len(poly)
    memo = {}

    def X(y, z):
        """the fairing's own surface at (y, z); just off its edge, the nearest surface found towards the middle"""
        key = (round(y, 1), round(z, 1))
        if key not in memo:
            v = None
            for k in (1.0, 0.9, 0.75, 0.5, 0.25, 0.0):
                v = D._cast(trees, cy + (y - cy) * k, cz + (z - cz) * k, "x", 1)
                if v is not None:
                    break
            memo[key] = v
        return memo[key]
    if X(cy, cz) is None:
        return False
    ring = lambda k: [(cy + (y - cy) * k, cz + (z - cz) * k) for y, z in poly]
    xs = [X(y, z) for y, z in poly]
    xb = min(xs) - depth
    m = len(poly)
    cut(D, prefixes, ring(0.985), min(xs))
    gloss = D.surf("lampsurround", (0.012, 0.012, 0.014), 0.0, 0.12, coat=1.0, wear=0.0)
    satin = D.surf("lamphousing", (0.02, 0.02, 0.022), 0.0, 0.5, wear=0.0)
    chrome = D.surf("lampchrome", (0.9, 0.9, 0.92), 1.0, 0.05, wear=0.0)

    def band(nm, rings, material, close=False):
        """a skin through rings of m points [(x, y, z)]"""
        verts = [B.V(*q) for r in rings for q in r]
        faces = [(r * m + i, r * m + (i + 1) % m, (r + 1) * m + (i + 1) % m, (r + 1) * m + i) for r in range(len(rings) - 1) for i in range(m)]
        if close:
            faces.append(tuple((len(rings) - 1) * m + i for i in range(m)))
        ob = B.mesh_obj(nm, verts, faces, material, True, 0, False)
        B.bm_fix_normals(ob)
        return ob
    on = lambda k, lift: [(X(y, z) + lift, y, z) for y, z in ring(k)]
    # the bezel: a gloss frame lapped over the cut edge of the paint, turning in to a short throat
    band(f"{name}_bezel", [on(1.075, 0.6), on(1.05, 2.4), on(0.97, 2.6), on(0.93, 1.0), on(0.9, -14.0)], gloss)
    # the housing: a dark bucket from the opening back to a closed rear wall
    band(f"{name}_housing", [on(1.0, -3.0), [(xb + 10, cy + (y - cy) * 0.92, cz + (z - cz) * 0.92) for y, z in poly], [(xb, cy + (y - cy) * 0.7, cz + (z - cz) * 0.7) for y, z in poly]], satin, close=True)
    # the reflector: a chrome bowl that deepens away from the rim, ribbed with flutes or stepped in facets
    inner = ring(0.9)
    step = 2.5
    ys_, zs_ = [q[0] for q in inner], [q[1] for q in inner]
    ny, nz = int((max(ys_) - min(ys_)) / step) + 2, int((max(zs_) - min(zs_)) / step) + 2
    idx, verts, faces = {}, [], []
    fl = spec.get("flute", 0)
    for iy in range(ny):
        for iz in range(nz):
            y, z = min(ys_) + iy * step, min(zs_) + iz * step
            if not _inside((y, z), inner):
                continue
            d = _edge_dist((y, z), inner)
            bowl = min(depth - 22, d * 1.5)
            if fl:
                rib = abs(((y / fl) % 1.0) - 0.5) * 2 * 2.4            # upright flutes, as a pressed reflector has
            else:
                rib = (int(d / 7) % 2) * 2.0 + (int((y + z) / 9) % 2) * 1.2     # stepped facets
            idx[(iy, iz)] = len(verts)
            verts.append(B.V(X(y, z) - 9 - bowl + rib, y, z))
    for (iy, iz), a in idx.items():
        if (iy + 1, iz) in idx and (iy, iz + 1) in idx and (iy + 1, iz + 1) in idx:
            faces.append((a, idx[(iy + 1, iz)], idx[(iy + 1, iz + 1)], idx[(iy, iz + 1)]))
    B.mesh_obj(f"{name}_reflector", verts, faces, chrome, False, 0, False)
    band(f"{name}_reflectorrim", [on(0.93, -8.0), on(0.86, -11.0)], chrome)
    # projectors: a glass lens in a bright shroud on a dark barrel, a cut-off shield behind it, some with a halo ring
    glass = D.glass_mat("lampglassblue", (0.82, 0.9, 1.0))
    for n_, (py, pz, r, halo) in enumerate(spec.get("proj", [])):
        py, pz = s * py * spec.get("ky", 1.0), pz + dz
        o = (X(py, pz) - 16 - r * 0.9 - 34, py, pz)
        ax = (1, 0, 0)
        D.lathe(f"{name}_projbarrel{n_}", o, ax, [(0, r + 6), (22, r + 6), (22, r + 2), (0, r + 2)], satin, 40)
        D.lathe(f"{name}_projshroud{n_}", o, ax, [(22, r + 7.5), (31, r + 7.5), (35, r + 5.5), (36, r + 2.5), (35, r + 0.6), (22, r + 0.6)], chrome, 48, sharp=False)
        D.lathe(f"{name}_projshield{n_}", o, ax, [(14, 0), (14, r + 1), (16, r + 1), (16, 0)], satin, 32)
        D.lathe(f"{name}_projlens{n_}", o, ax, [(27, 0), (27, r), (30, r), (33, r * 0.93), (36.5, r * 0.74), (39, r * 0.46), (40.2, 0)], glass, 48, sharp=False)
        D.lathe(f"{name}_projbulb{n_}", o, ax, [(17, 0), (17, 4), (21, 4), (23, 2), (23, 0)], D.glow("lampdip", (1.0, 0.97, 0.9), 1.2), 16)
        if halo:
            D.lathe(f"{name}_halo{n_}", o, ax, [(35.2, r + 3), (35.2, r + 6.6), (37.4, r + 6.6), (37.4, r + 3)], D.glow("drl", (0.9, 0.95, 1.0), 4.0), 64, caps=False)
            D.lathe(f"{name}_halocarrier{n_}", o, ax, [(33, r + 7), (33, r + 9), (37, r + 9), (37, r + 7)], gloss, 64, caps=False)
    # the running light: a clear guide glowing along its length, ribbed with fine prisms, in a gloss carrier
    drl = [(s * y * spec.get("ky", 1.0), z + dz) for y, z in spec.get("drl", [])]
    if len(drl) >= 2:
        dense = []
        for i in range(len(drl) - 1):
            (y0, z0), (y1, z1) = drl[i], drl[i + 1]
            n_ = max(2, int(math.hypot(y1 - y0, z1 - z0) / 4))
            dense += [(y0 + (y1 - y0) * q / n_, z0 + (z1 - z0) * q / n_) for q in range(n_)]
        dense.append(drl[-1])
        B.smooth_tube(f"{name}_drl", [(X(y, z) - 7, y, z) for y, z in dense], 3.0, D.glow("drl", (0.9, 0.95, 1.0), 4.0))
        B.smooth_tube(f"{name}_drlcarrier", [(X(y, z) - 10.5, y, z) for y, z in dense], 5.2, gloss)
        for i, (y, z) in enumerate(dense[1:-1:2]):
            B.cone(f"{name}_drlprism{i}", (X(y, z) - 7, y, z - 3.4), (X(y, z) - 7, y, z + 3.4), 3.5, 3.5, D.glass_mat(), 6)
    # LED cells: a small square emitter at the bottom of its own chrome cup
    for n_, (ly, lz, hw) in enumerate(spec.get("cells", [])):
        ly, lz = s * ly * spec.get("ky", 1.0), lz + dz
        x0 = X(ly, lz) - 12
        ob = B.cone(f"{name}_cellcup{n_}", (x0 - 16, ly, lz), (x0, ly, lz), hw * 0.45, hw * 1.4, chrome, 4)
        B.cone(f"{name}_cellled{n_}", (x0 - 15.5, ly, lz), (x0 - 14, ly, lz), hw * 0.5, hw * 0.5, D.glow("lampdip", (1.0, 0.97, 0.9), 1.2), 4)
    # the outer lens: clear, following the fairing and standing a little proud of it
    rings = [[(X(y, z) + 2.2 + 5.0 * (1 - k * k), y, z) for y, z in ring(k * 0.95)] for k in (1.0, 0.8, 0.55, 0.3)]
    verts = [B.V(*q) for r in rings for q in r] + [B.V(X(cy, cz) + 7.2, cy, cz)]
    faces = [(r * m + i, r * m + (i + 1) % m, (r + 1) * m + (i + 1) % m, (r + 1) * m + i) for r in range(3) for i in range(m)]
    faces += [(3 * m + i, 3 * m + (i + 1) % m, 4 * m) for i in range(m)]
    ob = B.mesh_obj(f"{name}_lens", verts, faces, D.glass_mat(), True, 0, False)
    B.bm_fix_normals(ob)
    return True


def fit(D, model, c, dz=0.0, prefixes=("cowl",)):
    """Fit the unit the model names (detail.lamp_unit) on both sides. Returns True if it was built."""
    spec = UNITS.get(model.get("detail", {}).get("lamp_unit"))
    if not spec:
        return False
    made = [unit(D, f"lampunit{s}", s, spec, dz, prefixes) for s in (-1, 1)]
    print("lamp units:", made)
    return all(made)
