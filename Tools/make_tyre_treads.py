"""Finer tyre treads: the tread sections the game bends round every tyre, redrawn with real depth.

    python Tools/make_tyre_treads.py && python Tools/import_blender_parts.py --redrawn

Torque Shed's sections (blender/parts/tyre/treads.py in the Godot repo) press their road grooves
into a carcass only six rows long and ten points across the crown, 2.5 mm deep, so on a bike in
Unreal the tread barely shows; and an MX tyre has four big knobs in each 1/32 of the way round.
beenster's build 101: "all tyres (except slicks) need tread, and knobbles for the MX tyres".

These keep Torque Shed's carcass profile and units (so BlenderParts.tyre_hoop and
Fit/BikeParts.cpp bend them round as before), with the patterns drawn from beenster's reference
pictures of real tyres (a sport tyre's long swept grooves, thin where they start by the middle and
widest halfway out; a sport-touring tyre's swept grooves with branching sipes; an adventure tyre's
blocks; an MX tyre's staggered knobs with split middle blocks and tie bars; a 50/50's big chunky
blocks). A road tread's section is longer than Torque Shed's 1/32 of the way round (SECTIONS, the
same numbers as BikeParts.cpp's TyreSectionsFor), so one groove sweeps from the middle to the
shoulder. Grooves are 6 mm deep. At lod0 each groove is cut along its drawn outline with sharp lips
and drafted walls (cut_tread: a constrained triangulation, so the edges follow the curve exactly and
stay crisp up close); lod1 presses them into an even grid. Reference only: no maker's pattern is
traced, and no names or logos go on the tyres. The slick stays Torque Shed's.

Writes Data/Parts/Redrawn/tyre/<tread>.glb in the parts agreement (glTF frame: x across -1..1, y
from the bead 0 to the crown 1, z 0..1 along 1/32 of the way round; nodes lod0..lod2; materials
named for their surface tags), which Tools/import_blender_parts.py --redrawn turns into
Data/Parts/Blender/tyre/<tread>*.glb and the index. Deterministic: run it again, same bytes.
Needs numpy, shapely and triangle (pip install shapely triangle).
"""
import json
import math
import os
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Data", "Parts", "Redrawn", "tyre")

TREADS = ["rib", "touring", "sport", "cruiser", "trail", "sipes", "knobs", "blocks", "studs"]
OFFROAD = ["knobs", "blocks", "studs"]
CENTRE_GROOVE = ["rib", "cruiser"]
CENTRE_EDGE = 0.09
BULGE = 0.434
DEEP = 0.06                # a road groove, in tyre heights (6 mm on the reference 0.1 m tyre, a new tyre's depth)

# Per LOD: rows along the section, points across the crown, points up each wall.
ROAD_DETAIL = [{"cell": 0.036, "crown": 54, "wall": 6}, {"cell": 0.12, "crown": 16, "wall": 3}, {"cell": 99.0, "crown": 4, "wall": 1}]
OFF_DETAIL = [{"along": 6, "crown": 24, "wall": 5}, {"along": 2, "crown": 8, "wall": 2}, {"along": 1, "crown": 2, "wall": 1}]

RUBBER = {"name": "rubber", "doubleSided": True,
          "pbrMetallicRoughness": {"baseColorFactor": [0.02, 0.02, 0.02, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.85}}
STEEL = {"name": "raw_steel", "doubleSided": True,
         "pbrMetallicRoughness": {"baseColorFactor": [0.45, 0.45, 0.46, 1.0], "metallicFactor": 1.0, "roughnessFactor": 0.4}}


def top_of(tread):
    return 1.0 - (0.19 if tread in ("knobs", "studs") else 0.1 if tread == "blocks" else 0.0)


def profile(tread, d):
    """treads.py profile(): the carcass from the left bead over the crown to the right, (across, up),
    and which points are on the crown."""
    sq = 2.8 if tread in OFFROAD else 2.0
    top = top_of(tread)
    side = []
    for i in range(d["wall"]):
        t = i / d["wall"]
        side.append((0.9 + 0.1 * math.sin(t * math.pi * 0.5), BULGE * (1.0 - math.cos(t * math.pi * 0.5))))
    crown = []
    n = d["crown"] // 2
    for i in range(n + 1):
        th = i / n * math.pi * 0.5
        crown.append((math.cos(th) ** (2.0 / sq), BULGE + (top - BULGE) * math.sin(th) ** (2.0 / sq)))
    if tread in CENTRE_GROOVE and d["crown"] >= 10:
        th = math.acos(CENTRE_EDGE ** (sq / 2.0))
        crown.insert(len(crown) - 1, (CENTRE_EDGE, BULGE + (top - BULGE) * math.sin(th) ** (2.0 / sq)))
        crown.sort(key=lambda p: -p[0])
    half = side + crown
    pts = [(-x, y) for x, y in half[:-1]] + list(reversed(half))
    on_crown = [abs(x) < 0.97 and y > BULGE + 0.05 for x, y in pts]
    return pts, on_crown


# How many sections go round a tyre of each tread (Fit/BikeParts.cpp TyreSectionsFor: the same
# numbers; Torque Shed's 32 for the rest). A road tread's section is long enough for one sweep of
# groove from the middle to the shoulder each side, as the real tyres have them.
SECTIONS = {"sport": 10, "touring": 12, "sipes": 12, "cruiser": 16, "trail": 14, "blocks": 24}


def along_of(tread):
    """A section's length in half widths, at a usual road tyre (0.31 m radius, 170 mm wide)."""
    return 2.0 * math.pi * 0.31 / SECTIONS.get(tread, 32) / 0.085


# The road patterns, drawn from beenster's reference pictures (a sport tyre's long swept grooves,
# a sport-touring tyre's swept grooves with branching sipes, an adventure tyre's blocks) and the
# makers' published descriptions. Reference only: no names or logos go on the tyres.
# Each groove: points (x across in half widths, u along the section) and its width at each point
# (half widths, either side of the line), on the right half; the left half is the right mirrored and
# half a section on, as the real ones alternate. Depth as a fraction of DEEP.
GROOVES = {
    # Sport: one long swept groove a side, thin where it starts by the middle, widest halfway out,
    # running out before the shoulder; a short dash further round by the shoulder. Slick shoulders.
    "sport": [([(0.1, 0.0), (0.2, 0.1), (0.36, 0.26), (0.53, 0.47), (0.66, 0.7), (0.74, 0.86)],
               [0.008, 0.03, 0.045, 0.045, 0.032, 0.008], 1.0),
              ([(0.58, 0.04), (0.66, 0.17), (0.72, 0.27)], [0.008, 0.026, 0.008], 0.8)],
    # Sport-touring: a long groove from the very middle sweeping out to the shoulder, a sipe
    # branching back off it, a shorter groove outboard, and a little sipe in the middle.
    "touring": [([(0.0, 0.0), (0.12, 0.07), (0.3, 0.2), (0.5, 0.39), (0.68, 0.6), (0.8, 0.78)],
                 [0.008, 0.03, 0.04, 0.04, 0.03, 0.008], 1.0),
                ([(0.42, 0.31), (0.56, 0.22), (0.7, 0.17)], [0.012, 0.014, 0.006], 0.7),
                ([(0.32, 0.62), (0.47, 0.78), (0.62, 0.98)], [0.008, 0.032, 0.01], 1.0),
                ([(0.08, 0.42), (0.18, 0.56)], [0.006, 0.006], 0.6)],
    # Rain: the sport-touring cut with more sipes across it.
    "sipes": [([(0.0, 0.0), (0.12, 0.07), (0.3, 0.2), (0.5, 0.39), (0.68, 0.6), (0.8, 0.78)],
               [0.008, 0.03, 0.04, 0.04, 0.03, 0.008], 1.0),
              ([(0.32, 0.62), (0.47, 0.78), (0.62, 0.98)], [0.008, 0.032, 0.01], 1.0),
              ([(0.42, 0.31), (0.56, 0.22), (0.72, 0.15)], [0.012, 0.014, 0.006], 0.7),
              ([(0.2, 0.32), (0.3, 0.46)], [0.007, 0.007], 0.6), ([(0.55, 0.5), (0.68, 0.42)], [0.007, 0.007], 0.6),
              ([(0.08, 0.6), (0.2, 0.76)], [0.007, 0.007], 0.6)],
    # Cruiser: a groove straight round the middle and chevrons either side of it.
    "cruiser": [([(0.0, -0.02), (0.0, 1.02)], [0.035, 0.035], 1.0),
                ([(0.1, 0.02), (0.36, 0.2), (0.72, 0.34)], [0.01, 0.042, 0.01], 1.0),
                ([(0.1, 0.52), (0.36, 0.7), (0.72, 0.84)], [0.01, 0.042, 0.01], 1.0)],
    # Adventure (90/10 road-biased, beenster's blocky adventure reference): blocks in three
    # rows, cut by a zigzag groove round the tyre each side and slanting cross grooves.
    "trail": [([(0.3, -0.02), (0.38, 0.24), (0.29, 0.5), (0.38, 0.74), (0.29, 1.0)], [0.04] * 5, 1.4),
              ([(-0.02, 0.02), (0.15, 0.08), (0.31, 0.14)], [0.035, 0.04, 0.035], 1.4),
              ([(0.35, 0.36), (0.6, 0.44), (0.84, 0.5)], [0.035, 0.045, 0.02], 1.4),
              ([(0.35, 0.86), (0.6, 0.94), (0.84, 1.0)], [0.035, 0.045, 0.02], 1.4),
              ([(0.6, 0.62), (0.68, 0.7)], [0.012, 0.012], 0.6)],
}


# How much wider than drawn above each tread's grooves are cut (beenster's references: bold grooves).
WIDEN = {"sport": 1.8, "touring": 1.6, "sipes": 1.4, "cruiser": 1.5, "trail": 1.8}


def densify(points, widths, step=0.01):
    """A groove's line as a Catmull-Rom curve through its points, every step, with its width."""
    P = [np.array(p, float) for p in points]
    out, wout = [], []
    n = len(P)
    for i in range(n - 1):
        p0, p1, p2, p3 = P[max(i - 1, 0)], P[i], P[i + 1], P[min(i + 2, n - 1)]
        k = max(2, int(np.linalg.norm(p2 - p1) / step))
        for j in range(k):
            t = j / k
            t2, t3 = t * t, t * t * t
            out.append(0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (3 * p1 - p0 - 3 * p2 + p3) * t3))
            wout.append(widths[i] + (widths[i + 1] - widths[i]) * t)
    out.append(P[-1]); wout.append(widths[-1])
    return np.array(out), np.array(wout)


def edge_field(tread, X, U):
    """Over points (X across, U along): how far each is outside the nearest groove's edge (half
    widths; negative inside it), and how deep that groove is (a fraction of DEEP)."""
    along = along_of(tread)
    E = np.full(X.shape, 9.0)
    F = np.ones(X.shape)
    if tread == "rib":
        for c in (0.0, 0.3, 0.58):
            for sd in ((1.0,) if c == 0.0 else (1.0, -1.0)):
                Uu = U if sd > 0 else (U + 0.5) % 1.0
                zig = 0.022 * (1.0 - np.abs((Uu * 4.0) % 2.0 - 1.0) * 2.0) if c else 0.0
                E = np.minimum(E, np.abs(X - sd * (c + zig)) - 0.045)
        return np.where(np.abs(X) > 0.88, 9.0, E), F
    for side in (1.0, -1.0):
        Xs = (X * side).reshape(-1, 1)
        Us = (U if side > 0 else (U + 0.5) % 1.0).reshape(-1, 1)
        for points, widths, deep in GROOVES[tread]:
            line, w = densify(points, [x * WIDEN.get(tread, 1.0) for x in widths], step=0.004)
            for wrap in (-1.0, 0.0, 1.0):
                dist = np.sqrt((Xs - line[None, :, 0]) ** 2 + ((Us - line[None, :, 1] - wrap) * along) ** 2) - w[None, :]
                e = dist.min(axis=1).reshape(X.shape)
                F = np.where(e < E, deep, F)
                E = np.minimum(E, e)
    return np.where(np.abs(X) > 0.88, 9.0, E), F


def snap_to_edges(tread, X, U, cell):
    """Grid points within half a cell of a groove's edge moved onto it (so the edge runs smooth, not
    stepped along the grid), and each point's depth: none outside a groove or on its edge, the
    groove's full depth inside it."""
    along = along_of(tread)
    E, F = edge_field(tread, X, U)
    h = cell * 0.05
    Ex = (edge_field(tread, X + h, U)[0] - edge_field(tread, X - h, U)[0]) / (2 * h)
    Eu = (edge_field(tread, X, U + h / along)[0] - edge_field(tread, X, U - h / along)[0]) / (2 * h)   # (per half width along)
    g2 = np.maximum(Ex ** 2 + Eu ** 2, 1e-9)
    near = (np.abs(E) < cell * 0.5) & (E < 5.0)
    Xn = np.where(near, X - E * Ex / g2, X)
    Un = np.where(near, U - E * Eu / g2 / along, U)
    D = np.where(near, 0.0, np.where(E < 0.0, F * DEEP, 0.0))
    return Xn, Un, D


def road_profile(tread, lod):
    """The road carcass from bead to bead: Torque Shed's walls, and its crown sampled evenly across
    (cells about `cell` half widths square at lod0), with which points are on the crown."""
    d = ROAD_DETAIL[lod]
    sq = 2.0
    top = top_of(tread)
    side = []
    for i in range(d["wall"]):
        t = i / d["wall"]
        side.append((0.9 + 0.1 * math.sin(t * math.pi * 0.5), BULGE * (1.0 - math.cos(t * math.pi * 0.5))))
    n = d["crown"]
    crown = []
    for i in range(n + 1):
        x = 1.0 - 2.0 * i / n
        crown.append((x, BULGE + (top - BULGE) * max(0.0, 1.0 - abs(x) ** sq) ** (1.0 / sq)))
    pts = [(-x, y) for x, y in side] + list(reversed([(x, y) for x, y in crown])) + list(reversed(side))
    pts = [(-x, y) for x, y in side] + [(x, y) for x, y in reversed(crown)][1:-1] + [(x, y) for x, y in reversed(side)]
    pts = [p for p in pts]
    on = [abs(x) < 0.97 and y > BULGE + 0.05 for x, y in pts]
    return pts, on


class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.idx = [], [], [], []

    def add(self, p, n, uv):
        self.pos.append(p); self.nrm.append(n); self.uv.append(uv)
        return len(self.pos) - 1

    def tri(self, a, b, c):
        self.idx += [a, b, c]


def outward(tri_pts):
    a, b, c = tri_pts
    n = np.cross(b - a, c - a)
    m = (a + b + c) / 3.0
    return float(np.dot(n, np.array([m[0], m[1] - 0.3, 0.0]))) >= 0.0


# --- the road treads at lod0: grooves cut with crisp edges --------------------------------------
#
# beenster, build 128: "the tread on the tyres looks really low res". Pressing the grooves into an
# even grid (as lod1 still does) steps every groove edge along the grid and smooths each wall into a
# slope a cell wide, which up close reads as a saw-toothed, blurry cut. So lod0 is cut properly: each
# groove's outline is drawn as a polygon (the same lines and widths as edge_field), its floor is
# that outline drawn in by the wall's draft, and the crown is triangulated with both outlines as
# fixed edges (a constrained Delaunay triangulation), so the groove's lip and foot run exactly along
# the drawn curve. The land and the groove floor are lit as the smooth carcass; each wall has its own
# normals, so the lip is a clean, sharp edge.
#
# Needs shapely and triangle (pip install shapely triangle).

XB = 0.96                  # the crown's cut region, across (the grooves stop at 0.88)
DRAFT = 0.012              # how far a groove's floor is drawn in from its lip (half widths, about 1 mm)
CROWN_STEP = 0.035         # the crown's sampling across along the section ends (half widths)
ROW_STEP = 0.05            # the sidewalls' rows along the section (half widths)
MAX_AREA = 0.0012          # the largest triangle on the land and the floors (square half widths)


def groove_strokes(tread):
    """Each groove's outline in (x across, s along in half widths) over the section and its
    neighbours either side (so the cut runs on across the section's ends), with its depth."""
    from shapely.geometry import LineString, Point, box
    from shapely.ops import unary_union
    along = along_of(tread)
    out = []
    if tread == "rib":
        u = np.linspace(-1.0, 2.0, 49)        # (the zigzags' corners every 1/16)
        for c in (0.0, 0.3, 0.58):
            for sd in ((1.0,) if c == 0.0 else (1.0, -1.0)):
                uu = u if sd > 0 else (u + 0.5) % 1.0
                zig = 0.022 * (1.0 - np.abs((uu * 4.0) % 2.0 - 1.0) * 2.0) if c else 0.0 * u
                line = LineString(np.stack([sd * (c + zig), u * along], 1))
                out.append((line.buffer(0.045, cap_style="flat", join_style="mitre", quad_segs=16), 1.0))
    else:
        for side in (1.0, -1.0):
            for points, widths, deep in GROOVES[tread]:
                line, w = densify(points, [x * WIDEN.get(tread, 1.0) for x in widths], step=0.012)
                for wrap in (-1.0, 0.0, 1.0):
                    xs = side * line[:, 0]
                    ss = (line[:, 1] + (0.0 if side > 0 else 0.5) + wrap) * along
                    discs = [Point(x, s_).buffer(r, quad_segs=6) for x, s_, r in zip(xs, ss, w)]
                    hulls = [discs[i].union(discs[i + 1]).convex_hull for i in range(len(discs) - 1)]
                    out.append((unary_union(hulls), deep))
    keep = box(-0.88, -along, 0.88, 2.0 * along)
    return [(g.intersection(keep), d) for g, d in out]


def crown_height(tread, x):
    top = top_of(tread)
    return BULGE + (top - BULGE) * np.sqrt(np.maximum(0.0, 1.0 - np.abs(x) ** 2.0))


def crown_normal(tread, x):
    """The smooth carcass's normal at x across (glb frame: x across, y up, z along)."""
    h = 1e-4
    slope = (crown_height(tread, x + h) - crown_height(tread, x - h)) / (2 * h)
    n = np.stack([-slope, np.ones_like(slope), np.zeros_like(slope)], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def cut_tread(tread, mesh):
    import shapely
    import triangle
    from shapely.geometry import LineString, MultiLineString, box
    from shapely.ops import unary_union
    along = along_of(tread)
    strokes = groove_strokes(tread)
    G_ext = unary_union([g for g, _ in strokes])
    F_ext = G_ext.buffer(-DRAFT, quad_segs=6)
    rect = box(-XB, 0.0, XB, along)
    G = G_ext.intersection(rect)
    Fl = F_ext.intersection(rect)
    shapely.prepare(G_ext); shapely.prepare(F_ext)

    def region(x, s):   # 2 floor, 1 wall, 0 land
        f = shapely.contains_xy(F_ext, x, s)
        g = shapely.contains_xy(G_ext, x, s)
        return np.where(f, 2, np.where(g, 1, 0))

    # The section's ends: crown samples across (none inside a wall, whose ends are its lip and foot),
    # the same at both ends so the sections meet without cracks.
    xs = np.linspace(-XB, XB, int(round(2 * XB / CROWN_STEP)) + 1)
    on_end = [x for x in xs if region(np.array([x]), np.array([0.0]))[0] != 1 and region(np.array([x]), np.array([along]))[0] != 1]
    rows = max(2, int(math.ceil(along / ROW_STEP)))
    ss = np.linspace(0.0, along, rows + 1)
    edge = LineString([(x, 0.0) for x in on_end] + [(XB, s_) for s_ in ss] + [(x, along) for x in reversed(on_end)]
                      + [(-XB, s_) for s_ in reversed(ss)] + [(on_end[0], 0.0)])
    lines = [edge]
    for geom in (G, Fl):
        for poly in getattr(geom, "geoms", [geom]):
            if poly.is_empty or poly.geom_type != "Polygon":
                continue
            lines.append(poly.exterior)
            lines += list(poly.interiors)
    noded = unary_union(MultiLineString([list(l.coords) for l in lines]))
    keys, verts, segs = {}, [], set()

    def vid(p):
        k = (round(p[0], 9), round(p[1], 9))
        if k not in keys:
            keys[k] = len(verts)
            verts.append(k)
        return keys[k]

    for ln in getattr(noded, "geoms", [noded]):
        c = list(ln.coords)
        for a, b in zip(c[:-1], c[1:]):
            i, j = vid(a), vid(b)
            if i != j:
                segs.add((min(i, j), max(i, j)))
    # Both ends carry the same points across (a lip met at one end is met at the other).
    V = np.array(verts)
    end0 = sorted(V[np.abs(V[:, 1]) < 1e-9, 0])
    end1 = sorted(V[np.abs(V[:, 1] - along) < 1e-9, 0])
    if len(end0) != len(end1) or (end0 and np.max(np.abs(np.array(end0) - np.array(end1))) > 1e-4):
        raise SystemExit("%s: the section's ends don't match (%d / %d points)" % (tread, len(end0), len(end1)))
    # (A hair apart at most, from the arithmetic: the far end's points put exactly on the near end's.)
    at1 = np.where(np.abs(V[:, 1] - along) < 1e-9)[0]
    V[at1[np.argsort(V[at1, 0])], 0] = end0
    t = triangle.triangulate({"vertices": V, "segments": np.array(sorted(segs))}, "pYYa%g" % MAX_AREA)
    P2, T = t["vertices"], t["triangles"]
    # Depths: the land none, a floor its groove's full depth, a wall from its lip (none) to its foot.
    _, Fdeep = edge_field(tread, P2[:, 0], P2[:, 1] / along)
    full = Fdeep * DEEP
    pts2 = shapely.points(P2)
    # (The outlines run on past the section's ends, so a point on an end is on a lip only if it is.)
    dG = shapely.distance(G_ext.boundary, pts2)
    dF = shapely.distance(F_ext.boundary, pts2) if not F_ext.is_empty else np.full(len(P2), 9.0)
    wall_depth = np.where(dG < 1e-7, 0.0, np.where(dF < 1e-7, full, full * dG / np.maximum(dG + dF, 1e-12)))
    # Each triangle's surface, from its corners where they say (a corner off both outlines is on the
    # land, a floor or inside a wall), else from its middle: so no sliver by an outline is lifted
    # to the wrong height and opens a crack.
    lip, foot = dG < 1e-7, dF < 1e-7
    inner = region(P2[:, 0], P2[:, 1])
    cen = P2[T].mean(axis=1)
    reg = region(cen[:, 0], cen[:, 1])
    free = ~(lip | foot)
    for r in (1, 2, 0):
        reg = np.where((free[T] & (inner[T] == r)).any(axis=1), r, reg)
    reg = np.where(reg == 0, np.where((foot[T]).any(axis=1) & ~(free[T] & (inner[T] == 0)).any(axis=1), np.where(lip[T].any(axis=1), 1, 2), 0), reg)
    reg = np.where((reg == 2) & lip[T].any(axis=1) & ~(free[T] & (inner[T] == 2)).any(axis=1), 1, reg)

    def lift(ids, depth):
        x, s_ = P2[ids, 0], P2[ids, 1]
        return np.stack([x, crown_height(tread, x) - depth, s_ / along], 1)

    def seam_key(p):
        return (round(p[0], 7), round(p[2] % 1.0, 7) if abs(p[2] - 1.0) < 1e-9 else round(p[2], 7))

    for r in (0, 2, 1):
        tris = T[reg == r]
        if not len(tris):
            continue
        ids = np.unique(tris)
        depth = np.zeros(len(ids)) if r == 0 else full[ids] if r == 2 else wall_depth[ids]
        pos = lift(ids, depth)
        local = {int(k): n for n, k in enumerate(ids)}
        tl = np.vectorize(local.get)(tris)
        # Face the triangles out of the tyre.
        a, b, c = pos[tl[:, 0]], pos[tl[:, 1]], pos[tl[:, 2]]
        fn = np.cross(b - a, c - a)
        up = crown_normal(tread, (a[:, 0] + b[:, 0] + c[:, 0]) / 3.0)
        flip = np.einsum("ij,ij->i", fn, up) < 0.0
        tl[flip] = tl[flip][:, [0, 2, 1]]
        fn[flip] = -fn[flip]
        if r == 1:
            # A wall's normals: its faces' (by area), joined across the section's ends.
            nrm = np.zeros_like(pos)
            for k in range(3):
                np.add.at(nrm, tl[:, k], fn)
            twins = {}
            for n, p in enumerate(pos):
                twins.setdefault(seam_key(p), []).append(n)
            for group in twins.values():
                if len(group) > 1:
                    nrm[group] = nrm[group].sum(axis=0)
            nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
        else:
            nrm = crown_normal(tread, pos[:, 0])
        base = len(mesh.pos)
        for p, n in zip(pos, nrm):
            mesh.add(tuple(float(v) for v in p), tuple(float(v) for v in n), (float(p[0]) * 0.05, float(p[2]) * 0.07))
        for tri in tl:
            mesh.tri(*(base + int(k) for k in tri))
    # The sidewalls: Torque Shed's walls from the bead up to the cut crown's edge, in rows.
    pts, _ = road_profile(tread, 0)
    for sd in (1.0, -1.0):
        # (From the bead up: the wall's points, then the crown's outside the cut region.)
        prof = [(x, y) for x, y in pts if x * sd > 0 and (y < BULGE or x * sd > XB + 1e-6)]
        prof.sort(key=lambda p: p[1])
        prof.append((sd * XB, float(crown_height(tread, XB))))
        prof = np.array(prof)
        nrm2 = []
        for i in range(len(prof)):
            if i == len(prof) - 1:
                nrm2.append(crown_normal(tread, np.array(sd * XB)))
                continue
            tg = prof[min(i + 1, len(prof) - 1)] - prof[max(i - 1, 0)]
            n = np.array([tg[1], -tg[0], 0.0]) * sd
            if n[0] * sd < 0.0 and n[1] < 0.0:
                n = -n
            nrm2.append(n / np.linalg.norm(n))
        base = len(mesh.pos)
        for s_ in ss:
            for (x, y), n in zip(prof, nrm2):
                mesh.add((float(x), float(y), float(s_ / along)), tuple(float(v) for v in n), (float(x) * 0.05, float(s_ / along) * 0.07))
        cols = len(prof)
        for i in range(rows):
            for j in range(cols - 1):
                a, b, c, e = base + i * cols + j, base + (i + 1) * cols + j, base + (i + 1) * cols + j + 1, base + i * cols + j + 1
                P = [np.array(mesh.pos[k]) for k in (a, b, c)]
                if outward(P):
                    mesh.tri(a, b, c); mesh.tri(a, c, e)
                else:
                    mesh.tri(a, c, b); mesh.tri(a, e, c)


def carcass(tread, lod, mesh):
    road = tread not in OFFROAD
    if road and lod == 0:
        return cut_tread(tread, mesh)
    d = (ROAD_DETAIL if road else OFF_DETAIL)[lod]
    if road:
        pts, on_crown = road_profile(tread, lod)
        rows = max(1, int(round(along_of(tread) / d["cell"])))
    else:
        pts, on_crown = profile(tread, d)
        rows = d["along"]
    cols = len(pts)
    grid = np.zeros((rows + 1, cols, 3))
    xs = np.array([p[0] for p in pts]); ys = np.array([p[1] for p in pts]); crown = np.array(on_crown)
    for i in range(rows + 1):
        grid[i, :, 0] = xs
        grid[i, :, 1] = ys
        grid[i, :, 2] = i / rows
    if road and lod < 2:
        X = np.repeat(xs[None, :], rows + 1, 0)
        U = np.repeat((np.arange(rows + 1) / rows)[:, None], cols, 1)
        Xn, Un, D = snap_to_edges(tread, X, U, d["cell"])
        on = np.repeat(crown[None, :], rows + 1, 0)
        # (The ends of the section stay where they are along, so the sections still meet.)
        ends = np.zeros_like(on); ends[0, :] = True; ends[-1, :] = True
        Xn = np.where(on, np.clip(Xn, -0.96, 0.96), X)
        Un = np.where(on & ~ends, np.clip(Un, 0.0, 1.0), U)
        top = top_of(tread)
        Yn = BULGE + (top - BULGE) * np.maximum(0.0, 1.0 - np.abs(Xn) ** 2.0) ** 0.5
        grid[:, :, 0] = Xn
        grid[:, :, 2] = Un
        grid[:, :, 1] = np.where(on, Yn - D, grid[:, :, 1])
    # Smooth normals off the grid (central differences), square round the tyre at the section ends.
    nrm = np.zeros_like(grid)
    for i in range(rows + 1):
        for j in range(cols):
            ta = grid[min(i + 1, rows), j] - grid[max(i - 1, 0), j]
            tb = grid[i, min(j + 1, cols - 1)] - grid[i, max(j - 1, 0)]
            n = np.cross(tb, ta)
            p = grid[i, j]
            if np.dot(n, [p[0], p[1] - 0.3, 0.0]) < 0.0:
                n = -n
            if i == 0 or i == rows:
                n[2] = 0.0
            ln = np.linalg.norm(n)
            nrm[i, j] = n / ln if ln > 1e-9 else (0.0, 1.0, 0.0)
    base = len(mesh.pos)
    for i in range(rows + 1):
        for j in range(cols):
            p = grid[i, j]
            mesh.add(p, nrm[i, j], (p[0] * 0.05, p[2] * 0.07))
    for i in range(rows):
        for j in range(cols - 1):
            a, b, c, e = base + i * cols + j, base + (i + 1) * cols + j, base + (i + 1) * cols + j + 1, base + i * cols + j + 1
            P = [np.array(mesh.pos[k]) for k in (a, b, c)]
            if outward(P):
                mesh.tri(a, b, c); mesh.tri(a, c, e)
            else:
                mesh.tri(a, c, b); mesh.tri(a, e, c)


def crown_point(tread, x):
    """Where the carcass is at x across, and its outward normal there (in the x-y plane)."""
    pts, _ = profile(tread, OFF_DETAIL[0])
    pts = sorted([p for p in pts if p[1] > 0.3], key=lambda p: p[0])
    xs = np.array([p[0] for p in pts]); ys = np.array([p[1] for p in pts])
    y = float(np.interp(x, xs, ys))
    h = 0.02
    slope = (float(np.interp(x + h, xs, ys)) - float(np.interp(x - h, xs, ys))) / (2 * h)
    n = np.array([-slope, 1.0]); n /= np.linalg.norm(n)
    return np.array([x, y]), n


def flat_quad(mesh, q, ref_out):
    """A flat-shaded quad (four corners), facing away from ref_out's opposite."""
    q = [np.asarray(v, float) for v in q]
    n = np.cross(q[1] - q[0], q[2] - q[0])
    if np.dot(n, ref_out) < 0.0:
        q = [q[0], q[3], q[2], q[1]]
        n = -n
    n = n / max(np.linalg.norm(n), 1e-12)
    ids = [mesh.add(v, n, (v[0] * 0.05, v[2] * 0.07)) for v in q]
    mesh.tri(ids[0], ids[1], ids[2]); mesh.tri(ids[0], ids[2], ids[3])


def knob(mesh, tread, x, u, w, length, h):
    """A knob w across and length along, standing h square off the carcass at (x, u), its sides
    drafted in a little (as a moulded knob's are), its foot sunk in the rubber."""
    p, n = crown_point(tread, x)
    side = np.array([n[1], -n[0]])                       # across the carcass there
    up3 = np.array([n[0], n[1], 0.0]); side3 = np.array([side[0], side[1], 0.0]); along3 = np.array([0.0, 0.0, 1.0])
    foot = np.array([p[0], p[1], u]) - up3 * 0.02
    top = np.array([p[0], p[1], u]) + up3 * h
    corners = ((-1, -1), (1, -1), (1, 1), (-1, 1))
    lo = [foot + side3 * sx * w * 0.5 + along3 * sy * length * 0.5 for sx, sy in corners]
    hi = [top + side3 * sx * w * 0.42 + along3 * sy * length * 0.42 for sx, sy in corners]
    centre = (foot + top) * 0.5
    flat_quad(mesh, hi, up3)
    for i in range(4):
        k = (i + 1) % 4
        mid = (lo[i] + lo[k] + hi[i] + hi[k]) / 4.0
        flat_quad(mesh, [lo[i], lo[k], hi[k], hi[i]], mid - centre)
    return top


def stud(mesh, at, up3):
    """A tungsten stud in a knob's top: a little hexagonal pin."""
    r, sides = 0.035, 6
    a = np.array([1.0, 0.0, 0.0]) if abs(up3[0]) < 0.9 else np.array([0.0, 1.0, 0.0])
    s1 = np.cross(up3, a); s1 /= np.linalg.norm(s1); s2 = np.cross(up3, s1)
    lo = at - up3 * 0.02; hi = at + up3 * 0.03
    ring = [(math.cos(2 * math.pi * k / sides), math.sin(2 * math.pi * k / sides)) for k in range(sides)]
    for k in range(sides):
        c0, c1 = ring[k], ring[(k + 1) % sides]
        q = [lo + (s1 * c0[0] + s2 * c0[1]) * r, lo + (s1 * c1[0] + s2 * c1[1]) * r,
             hi + (s1 * c1[0] + s2 * c1[1]) * r, hi + (s1 * c0[0] + s2 * c0[1]) * r]
        flat_quad(mesh, q, s1 * (c0[0] + c1[0]) + s2 * (c0[1] + c1[1]))
    cap = [hi + (s1 * c[0] + s2 * c[1]) * r for c in ring]
    n = up3 / np.linalg.norm(up3)
    ids = [mesh.add(v, n, (v[0] * 0.05, v[2] * 0.07)) for v in cap]
    for k in range(1, sides - 1):
        P = [cap[0], cap[k], cap[k + 1]]
        if np.dot(np.cross(P[1] - P[0], P[2] - P[0]), n) >= 0:
            mesh.tri(ids[0], ids[k], ids[k + 1])
        else:
            mesh.tri(ids[0], ids[k + 1], ids[k])


def knob_layout(tread, lod):
    """(x across, u along, width, length, height scale) of each knob in a section. Modelled on how
    real knobblies are laid out (reference only): an MX tyre's knobs in staggered rows, more of them
    on the shoulders, the middle ones split in two (a block within a block), and low tie bars from
    the middle knobs to the outer ones; a 50/50 adventure tyre's bigger, flatter blocks in offset
    rows, the shoulder blocks staggered."""
    if tread == "blocks":
        # (A 50/50's blocks: big and chunky, narrow gaps between, the rows offset.)
        spots = [(0.2, 0.22, 0.36, 0.42, 1.0), (-0.2, 0.22, 0.36, 0.42, 1.0),
                 (0.6, 0.3, 0.36, 0.4, 1.0), (-0.6, 0.3, 0.36, 0.4, 1.0),
                 (0.0, 0.72, 0.4, 0.4, 1.0), (0.4, 0.75, 0.32, 0.38, 1.0), (-0.4, 0.75, 0.32, 0.38, 1.0),
                 (0.8, 0.74, 0.28, 0.36, 0.9), (-0.8, 0.74, 0.28, 0.36, 0.9), (0.92, 0.26, 0.18, 0.3, 0.8), (-0.92, 0.26, 0.18, 0.3, 0.8)]
    else:
        spots = [(0.11, 0.25, 0.17, 0.24, 1.0), (-0.11, 0.25, 0.17, 0.24, 1.0),   # the split middle block
                 (0.47, 0.22, 0.24, 0.22, 1.0), (-0.47, 0.22, 0.24, 0.22, 1.0),
                 (0.88, 0.25, 0.2, 0.22, 0.95), (-0.88, 0.25, 0.2, 0.22, 0.95),
                 (0.25, 0.74, 0.26, 0.22, 1.0), (-0.25, 0.74, 0.26, 0.22, 1.0),
                 (0.68, 0.76, 0.22, 0.2, 0.95), (-0.68, 0.76, 0.22, 0.2, 0.95)]
    if lod == 1:
        spots = [s for s in spots if abs(s[0]) < 0.6]
    return spots if lod < 2 else []


def tie_bars(tread, lod):
    """Low ribs joining an MX tyre's middle knobs to the outer ones (x0, x1, u, width along)."""
    if tread == "blocks" or lod > 0:
        return []
    return [(0.11, 0.47, 0.235, 0.08), (-0.11, -0.47, 0.235, 0.08), (0.25, 0.68, 0.75, 0.08), (-0.25, -0.68, 0.75, 0.08)]


def section(tread, lod):
    rubber, steel = Mesh(), Mesh()
    carcass(tread, lod, rubber)
    if tread in OFFROAD:
        h = 0.1 if tread == "blocks" else 0.19
        for x0, x1, u, length in tie_bars(tread, lod):
            # (A tie bar: a long low knob between the two, a third of a knob's height.)
            knob(rubber, tread, (x0 + x1) * 0.5, u, abs(x1 - x0), length, h * 0.33)
        for x, u, w, length, hs in knob_layout(tread, lod):
            top = knob(rubber, tread, x, u, w, length, h * hs)
            if tread == "studs" and lod == 0:
                _, n = crown_point(tread, x)
                stud(steel, top, np.array([n[0], n[1], 0.0]))
    return [(m, mat) for m, mat in ((rubber, 0), (steel, 1)) if m.idx]


# --- glb ---------------------------------------------------------------------------------------

def pad4(b, fill=b"\0"):
    return b + fill * ((4 - len(b) % 4) % 4)


def write_glb(path, tread, lods):
    blob = bytearray()
    accessors, views, meshes = [], [], []

    def add(arr, comp, typ, target, minmax=False):
        data = arr.tobytes()
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(pad4(data))
        a = {"bufferView": len(views) - 1, "componentType": comp, "count": int(arr.shape[0]),
             "type": {1: "SCALAR", 2: "VEC2", 3: "VEC3"}[1 if arr.ndim == 1 else arr.shape[1]]}
        if minmax:
            a["min"] = [round(float(v), 6) for v in arr.min(axis=0)]
            a["max"] = [round(float(v), 6) for v in arr.max(axis=0)]
        accessors.append(a)
        return len(accessors) - 1

    for k, prims in enumerate(lods):
        out = []
        for m, mat in prims:
            pos = np.round(np.array(m.pos, np.float64), 6).astype("<f4")
            nrm = np.round(np.array(m.nrm, np.float64), 6).astype("<f4")
            uv = np.round(np.array(m.uv, np.float64), 6).astype("<f4")
            idx = np.array(m.idx, "<u4" if len(m.pos) > 65535 else "<u2")
            out.append({"attributes": {"POSITION": add(pos, 5126, None, 34962, True), "NORMAL": add(nrm, 5126, None, 34962),
                                       "TEXCOORD_0": add(uv, 5126, None, 34962)},
                        "indices": add(idx, 5125 if idx.dtype == np.dtype("<u4") else 5123, None, 34963), "material": mat})
        meshes.append({"name": "lod%d" % k, "primitives": out})
    j = {"asset": {"version": "2.0", "generator": "Tools/make_tyre_treads.py"},
         "scene": 0, "scenes": [{"nodes": [0]}],
         "nodes": [{"name": "part", "children": [1, 2, 3]}] + [{"name": "lod%d" % k, "mesh": k} for k in range(3)],
         "meshes": meshes, "materials": [RUBBER, STEEL], "accessors": accessors, "bufferViews": views,
         "buffers": [{"byteLength": len(blob)}]}
    js = pad4(json.dumps(j, separators=(",", ":"), sort_keys=True).encode(), b" ")
    body = struct.pack("<I4s", len(js), b"JSON") + js + struct.pack("<I4s", len(blob), b"BIN\0") + bytes(blob)
    data = struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body
    os.makedirs(os.path.dirname(path), exist_ok=True)
    old = open(path, "rb").read() if os.path.exists(path) else None
    if old != data:
        with open(path, "wb") as f:
            f.write(data)
    return [sum(len(m.idx) // 3 for m, _ in p) for p in lods]


def main():
    for tread in TREADS:
        lods = [section(tread, k) for k in range(3)]
        tris = write_glb(os.path.join(OUT, tread + ".glb"), tread, lods)
        print("%-8s %s triangles a section (lod0, lod1, lod2)" % (tread, tris))


if __name__ == "__main__":
    main()
