"""The hand tools hanging on the workbench's pegboard (the rack under the crew screen), as their own
prop, pegboard_tools: a graded set of combination spanners, an adjustable spanner, a ratchet and a
socket rail, screwdrivers in a rack, two pairs of pliers, a hammer, all on proper pegboard hooks.
No makers' names or marks anywhere: plain chrome, plain coloured grips.

    python3 Tools/props/make_pegboard_tools.py      # writes Data/Props/Blender/pegboard_tools.glb

Plain Python and numpy (no Blender): every piece is a sweep of a rounded section along a path, a
lathe (a turned profile), or a bevelled slab (an outline extruded with rounded edges), smooth shaded,
UV'd in metres by box projection like the Blender kit's props, one primitive per material key of
Data/Props/materials.json. Deterministic.

Frame: the workbench model's own (make_props.py shed_workbench: metres, x along the bench, y up, +z
out of the wall), so the prop goes in at the workbench's place and turn (swaps.json 'dress'). The
pegboard's face is at z PEG_Z, from x -1.44 to 1.44 and y 1.0 to 1.6; the bench top is at 0.945.
Nothing shares a plane with anything else (Tools/check_zfight.py checks): the tools stand 3 mm off
the board, shafts are thinner than the heads they run into, grips enclose their handles."""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import glb_edit   # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "Data", "Props", "Blender", "pegboard_tools.glb")
PEG_Z = -0.45 + 0.01 + 0.006      # the pegboard's front face (Peg: centre z -D/2 + 0.01, 12 mm thick)
GAP = 0.003                       # tools hang this far off the board

MESH = {}   # material -> [positions list, normals list, index list]


# --- Mesh building -----------------------------------------------------------------------------

def add(mat, pos, idx, normals=None):
    """Adds triangles (pos (n, 3), idx (m, 3) into pos) to a material, smooth normals by default."""
    pos = np.asarray(pos, float)
    idx = np.asarray(idx, np.int64).reshape(-1, 3)
    if normals is None:
        normals = np.zeros_like(pos)
        a, b, c = pos[idx[:, 0]], pos[idx[:, 1]], pos[idx[:, 2]]
        fn = np.cross(b - a, c - a)   # area weighted
        for k in range(3):
            np.add.at(normals, idx[:, k], fn)
    ln = np.linalg.norm(normals, axis=1, keepdims=True)
    normals = normals / np.where(ln < 1e-12, 1.0, ln)
    m = MESH.setdefault(mat, [[], [], []])
    base = sum(len(p) for p in m[0])
    m[0].append(pos)
    m[1].append(normals)
    m[2].append(idx + base)


def grid_faces(rows, cols, closed_cols=True, closed_rows=False):
    """Quads over a rows x cols vertex grid (row-major), as triangles."""
    f = []
    for r in range(rows - 1 + (1 if closed_rows else 0)):
        for c in range(cols - 1 + (1 if closed_cols else 0)):
            a = r * cols + c
            b = r * cols + (c + 1) % cols
            cc = ((r + 1) % rows) * cols + (c + 1) % cols
            d = ((r + 1) % rows) * cols + c
            f += [(a, b, cc), (a, cc, d)]
    return f


def cap(mat, ring, centre, outward):
    """A flat fan closing a ring of points, facing `outward` (hard edge: its own vertices)."""
    ring = np.asarray(ring, float)
    centre = np.asarray(centre, float)
    pts = np.vstack([centre, ring])
    n = len(ring)
    idx = [(0, 1 + i, 1 + (i + 1) % n) for i in range(n)]
    fn = sum(np.cross(pts[b] - pts[a], pts[c] - pts[a]) for a, b, c in idx)
    out = np.asarray(outward, float)
    if np.dot(fn, out) < 0:
        idx = [(a, c, b) for a, b, c in idx]
    add(mat, pts, idx, np.tile(out / np.linalg.norm(out), (len(pts), 1)))


def rounded_rect(w, h, r, n=4):
    """A rounded rectangle section (half sizes w, h, corner radius r) as points, anticlockwise."""
    r = min(r, w * 0.999, h * 0.999)
    pts = []
    for cx, cy, a0 in ((w - r, h - r, 0), (-w + r, h - r, 90), (-w + r, -h + r, 180), (w - r, -h + r, 270)):
        for k in range(n + 1):
            a = math.radians(a0 + 90.0 * k / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def circle(r, n=12):
    return [(r * math.cos(2 * math.pi * k / n), r * math.sin(2 * math.pi * k / n)) for k in range(n)]


def sweep(mat, path, section, normal=(0.0, 0.0, 1.0), closed=False, caps=True):
    """A section swept along a path. path: points; section(t, i) -> 2D points (x across the path in
    the plane, y along `normal`), t the distance fraction along the path. The frame's 'across' is
    normal x tangent, so flat tools lie in the board's plane."""
    path = np.asarray(path, float)
    N = np.asarray(normal, float)
    n = len(path)
    seg = np.linalg.norm(np.diff(path, axis=0), axis=1)
    s = np.concatenate([[0.0], np.cumsum(seg)])
    total = s[-1] + (np.linalg.norm(path[0] - path[-1]) if closed else 0.0)
    rings = []
    for i in range(n):
        if closed:
            tan = path[(i + 1) % n] - path[i - 1]
        else:
            tan = path[min(i + 1, n - 1)] - path[max(i - 1, 0)]
        tan /= np.linalg.norm(tan)
        if abs(np.dot(tan, N)) > 0.95:   # along the normal (a hook into the board): any side will do
            side = np.cross(tan, (0.0, 1.0, 0.0))
            up = np.cross(side, tan)
        else:
            side = np.cross(N, tan)
            side /= np.linalg.norm(side)
            up = np.cross(tan, side)
        sec = section(s[i] / total, i)
        rings.append([path[i] + side * a + up * b for a, b in sec])
    m = len(rings[0])
    pos = np.array([p for r in rings for p in r])
    # Smooth normals from the section's own shape (the section's outward direction in its plane).
    idx = grid_faces(n, m, closed_cols=True, closed_rows=closed)
    add(mat, pos, idx)
    if caps and not closed:
        cap(mat, rings[0], np.mean(rings[0], axis=0), path[0] - path[1])
        cap(mat, rings[-1], np.mean(rings[-1], axis=0), path[-1] - path[-2])


def lathe(mat, axis_origin, axis, profile, segments=32, radial=None, ref=None, caps=(True, True)):
    """A turned shape: profile [(along, radius)] about the axis through axis_origin. radial(theta,
    along, r) -> r reshapes it (flutes, hexes). ref: the direction theta 0 points."""
    o = np.asarray(axis_origin, float)
    a = np.asarray(axis, float)
    a /= np.linalg.norm(a)
    ref = np.asarray(ref if ref is not None else ((0, 0, 1) if abs(a[2]) < 0.9 else (1, 0, 0)), float)
    u = ref - a * np.dot(ref, a)
    u /= np.linalg.norm(u)
    v = np.cross(a, u)
    rows = []
    for (t, r) in profile:
        row = []
        for k in range(segments):
            th = 2 * math.pi * k / segments
            rr = radial(th, t, r) if radial else r
            row.append(o + a * t + (u * math.cos(th) + v * math.sin(th)) * rr)
        rows.append(row)
    pos = np.array([p for row in rows for p in row])
    add(mat, pos, grid_faces(len(rows), segments))
    if caps[0] and profile[0][1] > 1e-6:
        cap(mat, rows[0], o + a * profile[0][0], -a)
    if caps[1] and profile[-1][1] > 1e-6:
        cap(mat, rows[-1], o + a * profile[-1][0], a)


def offset_outline(pts, d):
    """Moves a closed outline (anticlockwise) out by d (in by -d) along its corner bisectors."""
    pts = np.asarray(pts, float)
    n = len(pts)
    out = []
    for i in range(n):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % n]
        e0 = (p1 - p0) / np.linalg.norm(p1 - p0)
        e1 = (p2 - p1) / np.linalg.norm(p2 - p1)
        n0 = np.array((e0[1], -e0[0]))
        n1 = np.array((e1[1], -e1[0]))
        m = n0 + n1
        m /= np.linalg.norm(m)
        out.append(p1 + m * d / max(np.dot(m, n0), 0.3))
    return out


def triangulate(pts):
    """Ear clipping of a simple anticlockwise polygon: index triples."""
    pts = [tuple(p) for p in pts]
    idx = list(range(len(pts)))
    tris = []

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    guard = 0
    while len(idx) > 3 and guard < 10000:
        guard += 1
        for k in range(len(idx)):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            a, b, c = pts[i0], pts[i1], pts[i2]
            if cross(a, b, c) <= 1e-12:
                continue
            if any(cross(a, b, pts[j]) >= 0 and cross(b, c, pts[j]) >= 0 and cross(c, a, pts[j]) >= 0
                   for j in idx if j not in (i0, i1, i2)):
                continue
            tris.append((i0, i1, i2))
            idx.pop(k)
            break
    tris.append(tuple(idx))
    return tris


def slab(mat, outline, z0, thick, bevel, frame=None, steps=3):
    """An outline (2D, anticlockwise, metres) extruded thick along +z from z0 with rounded edges of
    radius bevel; frame (origin, x axis, y axis) places it (default: the board's plane)."""
    o, ex, ey = frame if frame else ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0))
    o, ex, ey = np.asarray(o, float), np.asarray(ex, float), np.asarray(ey, float)
    ez = np.cross(ex, ey)

    def P(p2, z):
        return o + ex * p2[0] + ey * p2[1] + ez * z
    rings = []
    # Bottom face edge round to the top's: a quarter round each side.
    for k in range(steps + 1):
        a = math.radians(-90 + 90.0 * k / steps)
        rings.append((offset_outline(outline, -bevel + bevel * math.cos(a)), z0 + bevel + bevel * math.sin(a)))
    for k in range(steps + 1):
        a = math.radians(90.0 * k / steps)
        rings.append((offset_outline(outline, -bevel + bevel * math.cos(a)), z0 + thick - bevel + bevel * math.sin(a)))
    m = len(outline)
    pos = np.array([P(p, z) for ring, z in rings for p in ring])
    add(mat, pos, grid_faces(len(rings), m))
    top, zt = rings[-1]
    bot, zb = rings[0]
    tris = triangulate(top)
    add(mat, [P(p, zt) for p in top], tris, np.tile(ez, (len(top), 1)))
    add(mat, [P(p, zb) for p in bot], [(a, c, b) for a, b, c in tris], np.tile(-ez, (len(bot), 1)))


def arc(cx, cy, r, a0, a1, n):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * k / n)), cy + r * math.sin(math.radians(a0 + (a1 - a0) * k / n)))
            for k in range(n + 1)]


def rot2(pts, deg, about=(0.0, 0.0)):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [(about[0] + (x - about[0]) * c - (y - about[1]) * s, about[1] + (x - about[0]) * s + (y - about[1]) * c) for x, y in pts]


def clean(pts, eps=1e-5):
    out = []
    for p in pts:
        if not out or math.hypot(p[0] - out[-1][0], p[1] - out[-1][1]) > eps:
            out.append(p)
    if math.hypot(out[0][0] - out[-1][0], out[0][1] - out[-1][1]) <= eps:
        out.pop()
    return out


# --- The pegboard hooks --------------------------------------------------------------------------

WIRE = 0.0022


def hook(x, y, reach=0.055, lift=0.012, mat="chrome"):
    """A J hook: in at the board, straight out reach, a little tip turned up."""
    z0 = PEG_Z - 0.008
    pts = [(x, y, z0), (x, y, PEG_Z + 0.004)]
    for k in range(1, 7):
        pts.append((x, y, PEG_Z + 0.004 + (reach - 0.012) * k / 6))
    zt = PEG_Z + reach - 0.008
    for k in range(1, 7):
        a = math.radians(90.0 * k / 6)
        pts.append((x, y + 0.008 * (1 - math.cos(a)), zt + 0.008 * math.sin(a)))
    pts.append((x, y + 0.008 + lift, zt + 0.008))
    sweep(mat, pts, lambda t, i: circle(WIRE, 10), normal=(0.0, 1.0, 0.0))
    # The bent-over foot that locks it in the board's hole, behind the lip.
    lathe(mat, (x, y - 0.006, PEG_Z + 0.0007), (0, 1, 0), [(0.0, 0.0), (0.0, 0.0035), (0.002, 0.0035), (0.002, 0.0)], segments=12)


def straight_peg(x, y, reach=0.07, mat="chrome"):
    """A straight peg (two of these hold a hammer, a rail or a bracket)."""
    pts = [(x, y, PEG_Z - 0.008)] + [(x, y, PEG_Z + reach * k / 6) for k in range(1, 7)]
    sweep(mat, pts, lambda t, i: circle(WIRE, 10), normal=(0.0, 1.0, 0.0))
    lathe(mat, (x, y, PEG_Z + reach), (0, 0, 1), [(0.0, WIRE), (0.0012, WIRE * 0.8), (0.0018, 0.0)], segments=10, caps=(False, False))


# --- The tools -----------------------------------------------------------------------------------

def spanner(x, top_y, size, mat="chrome"):
    """A combination spanner for a size mm nut, hung by its ring end from a hook at (x, top_y): ring
    up, open jaw down (turned 15 degrees, as they are), the shaft between thinner than the heads."""
    s = size / 1000.0
    L = 0.012 * size + 0.04           # 8 mm: 0.136 m; 19 mm: 0.268 m
    th = 0.0024 + s * 0.17            # head half thickness
    sh = th * 0.72                    # shaft half thickness
    zc = PEG_Z + GAP + th
    r_in = s * 0.62                   # the ring's 12-point hole (round here) clears the nut
    r_out = s * 0.62 + 0.0024 + s * 0.22
    cy = top_y - r_in + WIRE          # the ring sits on the hook's wire
    # Ring end: a rounded section swept round a circle, its plane the board's (closed sweep).
    mid = (r_in + r_out) / 2
    hw = (r_out - r_in) / 2
    loop = [(x + mid * math.cos(2 * math.pi * k / 40), cy + mid * math.sin(2 * math.pi * k / 40), zc) for k in range(40)]
    sweep(mat, loop, lambda t, i: rounded_rect(hw, th, min(hw, th) * 0.6), normal=(0.0, 0.0, 1.0), closed=True)
    # The shaft: from the ring's rim to the jaw's, tapering, a slight waist (an I-beam's flats).
    jy = cy - L
    jaw_r = s * 0.5 + 0.0028 + s * 0.28   # the open end's outside radius
    y0, y1 = cy - r_out * 0.75, jy + jaw_r * 0.7
    w0, w1 = r_out * 0.4, jaw_r * 0.4
    # Rounded long sides: a few points on each, bowed in.
    def side(xa, ya, xb, yb, bow):
        return [(xa + (xb - xa) * k / 6 + bow * math.sin(math.pi * k / 6), ya + (yb - ya) * k / 6) for k in range(7)]
    outline = ccw(side(x + w1, y1, x + w0, y0, -0.0012)[:-1] + side(x - w0, y0, x - w1, y1, 0.0012)[:-1])
    slab(mat, outline, zc - sh, sh * 2, min(sh * 0.8, 0.0012))
    # The open end: a C round the nut's flats, turned 15 degrees, jaws running straight to the tips.
    a_open = -90 - 15.0                        # the opening points down, tilted
    half_gap = s * 0.5 * 1.04
    pts = []
    n = 26
    for k in range(n + 1):
        # From one jaw tip round the back to the other: the path at the jaw's middle radius.
        a = math.radians(a_open + 50 + (360 - 100) * k / n)
        pts.append((math.cos(a), math.sin(a)))
    mid_r = (half_gap + jaw_r) / 2
    hwj = (jaw_r - half_gap) / 2
    path = [(x + mid_r * px, jy + mid_r * py, zc) for px, py in pts]
    sweep(mat, path, lambda t, i: rounded_rect(hwj * (0.75 + 0.25 * math.sin(math.pi * t)), th, min(hwj, th) * 0.6),
          normal=(0.0, 0.0, 1.0))


def _signed_area(pts):
    return 0.5 * sum(pts[i - 1][0] * pts[i][1] - pts[i][0] * pts[i - 1][1] for i in range(len(pts)))


def ccw(pts):
    pts = clean(pts)
    return pts if _signed_area(pts) > 0 else pts[::-1]


def adjustable(x, top_y, mat="chrome"):
    """An adjustable spanner (250 mm) hung by the hole in its handle's end, head down, jaws open."""
    L = 0.25
    th = 0.0065
    zc = PEG_Z + GAP + th
    r_hole = 0.0055
    cy = top_y - r_hole + WIRE
    # The handle's end with its hanging hole: a rounded ring, then the handle tapering into the head.
    loop = [(x + 0.0085 * math.cos(2 * math.pi * k / 32), cy + 0.0085 * math.sin(2 * math.pi * k / 32), zc) for k in range(32)]
    sweep(mat, loop, lambda t, i: rounded_rect(0.003, th * 0.8, 0.002), closed=True)
    hy = cy - L + 0.03                       # the head's centre
    # The handle: slim, flaring into the head, thinner than it.
    side_r = [(x + 0.006 + 0.0025 * (k / 8) ** 2, cy - 0.008 - (cy - 0.008 - hy - 0.012) * k / 8) for k in range(9)]
    handle = ccw(side_r + [(2 * x - px, py) for px, py in side_r[::-1]])
    slab(mat, handle, zc - th * 0.75, th * 1.5, 0.0015)
    # The head: the fixed jaw out to the left at the top, a round back, the throat and the slot
    # the moving jaw slides in; the moving jaw below, 13 mm open, thinner than the head.
    head = ccw([(x - 0.046, hy + 0.004), (x - 0.016, hy + 0.002), (x - 0.012, hy - 0.006), (x - 0.004, hy - 0.016),
                (x + 0.006, hy - 0.024)] + arc(x + 0.004, hy - 0.002, 0.022, -70, 100, 16)[1:] +
               [(x - 0.012, hy + 0.021), (x - 0.03, hy + 0.017), (x - 0.046, hy + 0.012)])
    slab(mat, head, zc - th, th * 2, 0.0018)
    jaw = ccw([(x - 0.046, hy - 0.011), (x - 0.012, hy - 0.011), (x - 0.002, hy - 0.019), (x - 0.004, hy - 0.027),
               (x - 0.03, hy - 0.027), (x - 0.046, hy - 0.019)])
    slab(mat, jaw, zc - th * 0.85, th * 1.7, 0.0016)
    # The knurled worm under the moving jaw (steel).
    lathe("steel", (x - 0.024, hy - 0.031, zc), (1, 0, 0), [(0.0, 0.0), (0.0, 0.0055), (0.02, 0.0055), (0.02, 0.0)], segments=16,
          radial=lambda tt, t, r: r * (1.0 + 0.07 * math.cos(tt * 14)) if r > 0 else r)


def ratchet(x, top_y, mat="chrome"):
    """A 3/8 in drive ratchet (200 mm), hung by its handle's end hole, head down, the square drive
    pointing out of the board, a black grip on the handle."""
    L = 0.2
    th = 0.006
    zc = PEG_Z + GAP + 0.011
    cy = top_y - 0.005 + WIRE
    loop = [(x + 0.008 * math.cos(2 * math.pi * k / 32), cy + 0.008 * math.sin(2 * math.pi * k / 32), zc) for k in range(32)]
    sweep(mat, loop, lambda t, i: rounded_rect(0.0028, th * 0.7, 0.002), closed=True)
    hy = cy - L + 0.02
    # The handle: a flat bar, then the grip moulded round its upper part.
    bar = [(x, cy - 0.0105 + (hy + 0.02 - (cy - 0.0105)) * k / 10, zc) for k in range(11)]
    sweep(mat, bar, lambda t, i: rounded_rect(0.0065 + 0.002 * t, th * 0.7, 0.002))
    grip = [(x, cy - 0.022 - 0.1 * k / 10, zc) for k in range(11)]
    sweep("rubber", grip, lambda t, i: rounded_rect(0.0105, 0.0072 + 0.0008 * math.sin(math.pi * t), 0.0055))
    # The head: a turned disc, its lever on top, the drive square out of the front.
    lathe(mat, (x, hy, zc - 0.009), (0, 0, 1), [(0.0, 0.0), (0.0, 0.016), (0.002, 0.0175), (0.016, 0.0175), (0.018, 0.016), (0.018, 0.0)],
          segments=32)
    lever = ccw([(x - 0.007, hy - 0.004), (x + 0.007, hy - 0.004), (x + 0.004, hy + 0.012), (x - 0.004, hy + 0.012)])
    slab(mat, lever, zc + 0.009, 0.003, 0.0009)
    lathe("steel_dark", (x, hy, zc - 0.012), (0, 0, 1), [(0.0, 0.0), (0.0, 0.0055), (0.004, 0.0055), (0.004, 0.0)], segments=4,
          ref=(1, 1, 0))


def socket_rail(x0, x1, y):
    """A rail of sockets (10..19 mm) on two straight pegs, open ends out, a hex inside each."""
    zc = PEG_Z + 0.006
    rail = ccw([(x0, y - 0.009), (x1, y - 0.009), (x1, y + 0.009), (x0, y + 0.009)])
    slab("plastic_red", rail, PEG_Z + GAP, 0.01, 0.002)
    for i, size in enumerate((10, 11, 12, 13, 14, 15, 17, 19)):
        s = size / 1000.0
        r = s * 0.5 * 1.155 + 0.0028
        x = x0 + 0.018 + i * (x1 - x0 - 0.036) / 7
        z0 = PEG_Z + GAP + 0.01 + 0.0004
        Ls = 0.028
        hexr = s * 0.5 * 1.155

        def hexed(th, t, rr, hr=hexr):
            if abs(rr - hr) > 1e-9:   # only the hole's wall is hexagonal
                return rr
            a = (th + math.pi / 6) % (math.pi / 3) - math.pi / 6
            return hr * math.cos(math.pi / 6) / math.cos(a)
        lathe("chrome", (x, y, z0), (0, 0, 1),
              [(0.0, 0.0), (0.0, r - 0.0012), (0.0008, r), (Ls - 0.0008, r), (Ls, r - 0.0012), (Ls, hexr), (0.006, hexr), (0.006, 0.0)],
              segments=36, radial=hexed)
    for px in (x0 + 0.02, x1 - 0.02):
        straight_peg(px, y - 0.012, reach=0.018)


def screwdriver(x, y_rack, blade_len, handle_len, r_handle, colour, tip="flat"):
    """A screwdriver standing in the rack, tip down: a fluted handle, a ferrule, the shaft, the tip."""
    z = PEG_Z + 0.028
    yb = y_rack + 0.006                    # the bolster rests on the rack
    shaft_r = 0.0024 + r_handle * 0.06
    tip_y = yb - blade_len

    def flat(th, t, r):
        if tip != "flat" or t > 0.012 or r == 0:
            return r
        k = t / 0.012
        return r * (abs(math.cos(th)) * (1 - k) * 1.4 + k) if abs(math.cos(th)) > 0.3 else r * (0.25 + 0.75 * k)
    lathe("chrome", (x, tip_y, z), (0, 1, 0),
          [(0.0, 0.0), (0.0, shaft_r * 0.5), (0.012, shaft_r), (blade_len - 0.002, shaft_r), (blade_len, shaft_r * 1.8),
           (blade_len + 0.006, shaft_r * 1.8), (blade_len + 0.006, 0.0)], segments=16, radial=flat)

    def flutes(th, t, r):
        return r * (1.0 - 0.07 * max(0.0, math.cos(th * 6)) ** 2)
    h0 = yb + 0.004
    lathe(colour, (x, h0, z), (0, 1, 0),
          [(0.0, 0.0), (0.0, r_handle * 0.55), (0.004, r_handle * 0.8), (0.012, r_handle), (handle_len - 0.012, r_handle * 0.96),
           (handle_len - 0.004, r_handle * 0.8), (handle_len, r_handle * 0.4), (handle_len, 0.0)], segments=36, radial=flutes)
    # The ferrule between the bolster and the handle.
    lathe("steel", (x, yb - 0.001, z), (0, 1, 0), [(0.0, 0.0), (0.0, shaft_r * 2.1), (0.0058, shaft_r * 2.1), (0.0058, 0.0)], segments=16)


def screwdriver_rack(x0, x1, y):
    """A steel strip on two straight pegs with a hole for each screwdriver."""
    rack = ccw([(x0, PEG_Z + GAP), (x1, PEG_Z + GAP), (x1, PEG_Z + 0.05), (x0, PEG_Z + 0.05)])
    # (Laid flat: its outline in x and z, extruded down from y + 2 mm to y - 2 mm.)
    slab("steel_dark", rack, -(y + 0.002), 0.004, 0.0012, frame=((0, 0, 0), (1, 0, 0), (0, 0, 1)))
    for px in (x0 + 0.015, x1 - 0.015):
        straight_peg(px, y - 0.0045, reach=0.048)


def pliers(x, top_y, long_nose=False):
    """A pair of pliers hung on a hook between their handles: the pivot just under the hook, the
    jaws pointing down, red dipped grips on the handles."""
    zc = PEG_Z + GAP + 0.0062
    py = top_y - 0.01                       # the pivot
    jaw_len = 0.07 if long_nose else 0.04
    # The two halves cross at the pivot, one in front of the other (each half its own layer).
    for side, dz in ((-1, -0.0031), (1, 0.0031)):
        # Handle: from the pivot up and out, a gentle curve.
        # (The steel ends 4 mm inside the grip's end, not in its plane.)
        path = [(x + side * (0.006 + 0.03 * (k / 10 * 0.97) ** 1.3), py + 0.012 + 0.126 * k / 10, zc + dz) for k in range(11)]
        sweep("steel", path, lambda t, i: rounded_rect(0.0045 - 0.001 * t, 0.003, 0.0018))
        grip = [(x + side * (0.006 + 0.03 * (k / 10) ** 1.3), py + 0.012 + 0.13 * k / 10, zc + dz) for k in range(3, 11)]
        sweep("plastic_red", grip, lambda t, i: rounded_rect(0.0072 + 0.0008 * math.sin(math.pi * t), 0.0052, 0.0045))
        # Jaw: from the pivot down, tapering to the nose, crossing to the other side's.
        path = [(x - side * 0.004 * (1 - k / 8) + side * 0.0005 * (k / 8), py - jaw_len * k / 8, zc + dz) for k in range(9)]
        w0, w1 = (0.0075, 0.0018) if long_nose else (0.0085, 0.0045)
        sweep("steel", path, lambda t, i, w0=w0, w1=w1: rounded_rect(w0 + (w1 - w0) * t, 0.003, 0.0015))
        # The boss round the pivot, each half's own.
        lathe("steel", (x, py, zc + dz - 0.003), (0, 0, 1), [(0.0, 0.0), (0.0, 0.0105), (0.006, 0.0105), (0.006, 0.0)], segments=24)
    lathe("chrome", (x, py, zc - 0.0071), (0, 0, 1), [(0.0, 0.0), (0.0, 0.0042), (0.0142, 0.0042), (0.0142, 0.0)], segments=16)


def hammer(x, y_pegs):
    """A ball-pein hammer standing head up on two straight pegs, its handle hanging between them."""
    zc = PEG_Z + 0.03
    head_y = y_pegs + WIRE + 0.0135
    # The ash handle (oval), its top in the head.
    lathe("wood_top", (x, head_y + 0.0095, zc), (0, -1, 0),
          [(0.0, 0.0), (0.0, 0.009), (0.03, 0.0105), (0.16, 0.0115), (0.26, 0.0135), (0.285, 0.012), (0.292, 0.006), (0.292, 0.0)],
          segments=24, radial=lambda th, t, r: r * (1.0 if abs(math.sin(th)) < 0.01 else 1.0) * (0.72 + 0.28 * abs(math.cos(th))),
          ref=(0, 0, 1))
    # The head across the pegs: the flat face on the left, the ball on the right.
    lathe("steel_dark", (x - 0.055, head_y, zc), (1, 0, 0),
          [(0.0, 0.0), (0.0, 0.0115), (0.003, 0.0135), (0.03, 0.0135), (0.05, 0.011), (0.075, 0.0105), (0.085, 0.012),
           (0.095, 0.0115), (0.103, 0.009), (0.108, 0.005), (0.11, 0.0)], segments=28)
    for px in (x - 0.026, x + 0.026):
        straight_peg(px, y_pegs, reach=0.06)


def torque_wrench(x, top_y, mat="chrome"):
    """A long click torque wrench (450 mm) hung by the ring on its handle's end: the knurled grip,
    the tube with its scale band, the ratchet head at the bottom."""
    zc = PEG_Z + GAP + 0.0145
    cy = top_y - 0.0055 + WIRE
    loop = [(x + 0.0085 * math.cos(2 * math.pi * k / 32), cy + 0.0085 * math.sin(2 * math.pi * k / 32), zc) for k in range(32)]
    sweep(mat, loop, lambda t, i: rounded_rect(0.003, 0.0035, 0.0025), closed=True)
    top = cy - 0.0085
    knurl = lambda th, t, r: r * (1.0 + 0.035 * math.cos(th * 24)) if 0.01 < t < 0.11 else r
    lathe("steel_dark", (x, top, zc), (0, -1, 0),
          [(0.0, 0.0), (0.0, 0.009), (0.004, 0.0135), (0.115, 0.0135), (0.12, 0.0115), (0.12, 0.0)], segments=32, radial=knurl,
          ref=(0, 0, 1))
    lathe(mat, (x, top - 0.118, zc), (0, -1, 0),
          [(0.0, 0.0), (0.0, 0.0108), (0.3, 0.0102), (0.31, 0.007), (0.4, 0.0062), (0.4, 0.0)], segments=28)
    # The scale band: a ring of steel round the tube under the grip.
    lathe("steel", (x, top - 0.125, zc), (0, -1, 0), [(0.0, 0.0), (0.0, 0.0112), (0.035, 0.0112), (0.035, 0.0)], segments=28)
    hy = top - 0.118 - 0.4 - 0.012
    lathe(mat, (x, hy, zc - 0.009), (0, 0, 1), [(0.0, 0.0), (0.0, 0.015), (0.002, 0.0165), (0.016, 0.0165), (0.018, 0.015), (0.018, 0.0)],
          segments=32)
    neck = ccw([(x - 0.0065, hy + 0.008), (x + 0.0065, hy + 0.008), (x + 0.0055, hy + 0.03), (x - 0.0055, hy + 0.03)])
    slab(mat, neck, zc - 0.006, 0.012, 0.002)
    lathe("steel_dark", (x, hy, zc - 0.012), (0, 0, 1), [(0.0, 0.0), (0.0, 0.0055), (0.004, 0.0055), (0.004, 0.0)], segments=4,
          ref=(1, 1, 0))


def hacksaw(x0, x1, y):
    """A hacksaw resting on two straight pegs: the frame's back over the pegs, the blade along the
    bottom, the pistol grip at the right."""
    zc = PEG_Z + GAP + 0.0105          # the grip's half thickness: the grip just clears the board
    top = y + WIRE + 0.0062
    depth = 0.1
    frame = [(x1 - 0.02, top - 0.03 + 0.03 * k / 4, zc) for k in range(5)]
    frame += [(x1 - 0.02 - (x1 - x0 - 0.04) * k / 12, top, zc) for k in range(1, 13)]
    frame += [(x0 + 0.02, top - depth * k / 6, zc) for k in range(1, 7)]
    sweep("paint_blue", frame, lambda t, i: rounded_rect(0.0055, 0.0062, 0.0035))
    blade = ccw([(x0 + 0.012, top - depth - 0.006), (x1 - 0.02, top - depth - 0.006), (x1 - 0.02, top - depth + 0.006),
                 (x0 + 0.012, top - depth + 0.006)])
    slab("steel_dark", blade, zc - 0.0005, 0.001, 0.0003)
    for bx in (x0 + 0.02, x1 - 0.02):
        lathe("chrome", (bx, top - depth, zc - 0.004), (0, 0, 1), [(0.0, 0.0), (0.0, 0.0045), (0.008, 0.0045), (0.008, 0.0)], segments=6,
              ref=(1, 0, 0))
    # The grip: from the frame's back end down to below the blade, rounded.
    grip = ccw([(x1 - 0.026, top - 0.02), (x1 + 0.008, top - 0.012), (x1 + 0.03, top - 0.09), (x1 + 0.03, top - 0.125),
                (x1 + 0.005, top - 0.13), (x1 - 0.006, top - 0.095), (x1 - 0.022, top - 0.05)])
    slab("plastic_black", grip, zc - 0.0105, 0.021, 0.006)
    for px in (x0 + 0.08, x1 - 0.09):
        straight_peg(px, y, reach=0.045)


def hex_keys(x0, y):
    """A set of hex keys (2 to 10 mm) standing in their holder on a peg, short legs out at the top."""
    hz = PEG_Z + GAP
    holder = ccw([(x0, y - 0.012), (x0 + 0.13, y - 0.012), (x0 + 0.13, y + 0.012), (x0, y + 0.012)])
    slab("plastic_red", holder, hz, 0.026, 0.004)
    sizes = (2, 2.5, 3, 4, 5, 6, 8, 10)
    for i, mm in enumerate(sizes):
        a = mm / 2000.0 / math.cos(math.radians(30))
        x = x0 + 0.01 + i * 0.0155
        long_leg = 0.06 + mm * 0.009
        z = hz + 0.013
        r_bend = a * 1.6
        path = [(x, y - long_leg + 0.03, z), (x, y + 0.03, z)]
        for k in range(1, 7):
            th = math.radians(90 * k / 6)
            path.append((x, y + 0.03 + r_bend * math.sin(th), z + r_bend * (1 - math.cos(th))))
        path.append((x, y + 0.03 + r_bend, z + r_bend + 0.012 + mm * 0.0025))
        sweep("steel_dark", path, lambda t, i, a=a: circle(a, 6), normal=(1.0, 0.0, 0.0))
    straight_peg(x0 + 0.065, y - 0.0145, reach=0.024)


def tyre_lever(x, top_y, length):
    """A tyre lever hung by the hole in its end: a flat bar, the spoon end at the bottom curved out."""
    zc = PEG_Z + GAP + 0.003
    cy = top_y - 0.005 + WIRE
    loop = [(x + 0.0095 * math.cos(2 * math.pi * k / 28), cy + 0.0095 * math.sin(2 * math.pi * k / 28), zc) for k in range(28)]
    sweep("chrome", loop, lambda t, i: rounded_rect(0.0045, 0.0028, 0.0018), closed=True)
    path = [(x, cy - 0.013 - (length - 0.06) * k / 10, zc) for k in range(11)]
    end = path[-1]
    for k in range(1, 7):   # the spoon: curving out from the board over its last 5 cm
        path.append((x, end[1] - 0.008 * k, zc + 0.003 * k * k / 6))
    sweep("chrome", path, lambda t, i: rounded_rect(0.0085 + 0.004 * max(0.0, t - 0.75) / 0.25, 0.0025, 0.0016))


def build():
    MESH.clear()
    # Left: the spanner set, 8 to 19 mm, graded along the board on J hooks.
    sizes = (8, 10, 11, 12, 13, 14, 15, 16, 17, 19)
    top = 1.55
    for i, size in enumerate(sizes):
        x = -1.36 + i * 0.062
        hook(x, top, reach=0.035)
        spanner(x, top, size)
    # The adjustable and the ratchet.
    hook(-0.66, top, reach=0.04)
    adjustable(-0.66, top)
    hook(-0.56, top, reach=0.045)
    ratchet(-0.56, top)
    # Middle: the socket rail over the screwdrivers' rack.
    socket_rail(-0.44, -0.12, 1.5)
    screwdriver_rack(-0.44, -0.08, 1.31)
    kinds = ((0.15, 0.10, 0.015, "plastic_red", "flat"), (0.1, 0.095, 0.014, "plastic_yellow", "cross"),
             (0.125, 0.1, 0.015, "plastic_red", "cross"), (0.075, 0.085, 0.013, "plastic_yellow", "flat"),
             (0.15, 0.105, 0.016, "plastic_black", "flat"), (0.1, 0.09, 0.013, "plastic_black", "cross"))
    for i, (blade, handle, r, col, tip) in enumerate(kinds):
        screwdriver(-0.42 + i * 0.055, 1.31, blade, handle, r, col, tip)
    # Right: pliers, long nose pliers, the hammer.
    hook(0.12, 1.42, reach=0.05)
    pliers(0.12, 1.42)
    hook(0.26, 1.42, reach=0.05)
    pliers(0.26, 1.42, long_nose=True)
    hammer(0.5, 1.5)
    # Far right: the torque wrench, the hacksaw over the hex keys, the tyre levers.
    hook(0.7, 1.58, reach=0.05)
    torque_wrench(0.7, 1.58)
    hacksaw(0.84, 1.18, 1.5)
    hex_keys(0.86, 1.2)
    for i, length in enumerate((0.3, 0.25, 0.25)):
        hook(1.27 + i * 0.05, 1.5, reach=0.035)
        tyre_lever(1.27 + i * 0.05, 1.5, length)


def uv_box(pos, nrm):
    """Box projection in metres (UV 1.0 = 1 m), as the Blender kit's."""
    a = np.abs(nrm)
    uv = np.where((a[:, 0] >= a[:, 1]) & (a[:, 0] >= a[:, 2]), 0, np.where(a[:, 1] >= a[:, 2], 1, 2))
    out = np.empty((len(pos), 2))
    out[uv == 0] = pos[uv == 0][:, [2, 1]]
    out[uv == 1] = pos[uv == 1][:, [0, 2]]
    out[uv == 2] = pos[uv == 2][:, [0, 1]]
    return out


def write(path=OUT):
    import json
    spec = json.load(open(os.path.join(ROOT, "Data", "Props", "materials.json")))["materials"]
    j = {"asset": {"generator": "Torque Shed make_pegboard_tools.py", "version": "2.0"}, "scene": 0,
         "scenes": [{"name": "Scene", "nodes": [0]}], "nodes": [{"mesh": 0, "name": "SM_pegboard_tools"}],
         "materials": [], "meshes": [{"name": "SM_pegboard_tools", "primitives": []}], "accessors": [], "bufferViews": []}
    b = bytearray()

    def blob(arr, target=None):
        raw = arr.tobytes()
        while len(b) % 4:
            b.append(0)
        bv = {"buffer": 0, "byteOffset": len(b), "byteLength": len(raw)}
        if target:
            bv["target"] = target
        b.extend(raw)
        j["bufferViews"].append(bv)
        return len(j["bufferViews"]) - 1

    def accessor(arr, kind, ctype, target):
        acc = {"bufferView": blob(arr, target), "componentType": ctype, "count": int(arr.shape[0]), "type": kind}
        if kind == "VEC3" and ctype == 5126 and target == 34962:
            acc["min"] = [float(v) for v in arr.min(0)]
            acc["max"] = [float(v) for v in arr.max(0)]
        j["accessors"].append(acc)
        return len(j["accessors"]) - 1

    for mat in sorted(MESH):
        if mat not in spec:
            raise KeyError("no material %r in Data/Props/materials.json" % mat)
        pos = np.vstack(MESH[mat][0]).astype(np.float32)
        nrm = np.vstack(MESH[mat][1]).astype(np.float32)
        idx = np.vstack(MESH[mat][2]).reshape(-1).astype(np.uint32)
        uv = uv_box(pos, nrm).astype(np.float32)
        m = spec[mat]
        t = m.get("tint", [0.5, 0.5, 0.5])
        j["materials"].append({"doubleSided": True, "name": mat, "pbrMetallicRoughness": {
            "baseColorFactor": [t[0], t[1], t[2], 1.0], "metallicFactor": m.get("metal", 0.0), "roughnessFactor": m.get("rough", 0.5)}})
        j["meshes"][0]["primitives"].append({"attributes": {
            "POSITION": accessor(pos, "VEC3", 5126, 34962), "NORMAL": accessor(nrm, "VEC3", 5126, 34962),
            "TEXCOORD_0": accessor(uv, "VEC2", 5126, 34962)}, "indices": accessor(idx, "SCALAR", 5125, 34963),
            "material": len(j["materials"]) - 1})
    glb_edit.write(path, j, b)
    tris = sum(len(np.vstack(v[2])) for v in MESH.values())
    print("wrote %s: %d triangles, materials %s" % (os.path.relpath(path, ROOT), tris, ", ".join(sorted(MESH))))


if __name__ == "__main__":
    build()
    write()
