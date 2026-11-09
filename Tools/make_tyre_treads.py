"""Finer tyre treads: the tread sections the game bends round every tyre, redrawn with real depth.

    python Tools/make_tyre_treads.py && python Tools/import_blender_parts.py --redrawn

Torque Shed's sections (blender/parts/tyre/treads.py in the Godot repo) press their road grooves
into a carcass only six rows long and ten points across the crown, 2.5 mm deep, so on a bike in
Unreal the tread barely shows; and an MX tyre has four big knobs in each 1/32 of the way round.
beenster's build 101: "all tyres (except slicks) need tread, and knobbles for the MX tyres".

These are the same patterns (treads.py's groove() and knob layout, the same carcass profile and the
same units, so BlenderParts.tyre_hoop and Fit/BikeParts.cpp bend them round exactly as before),
drawn finer: 16 rows along and 48 points across the crown, grooves 4.5 mm deep with steep walls,
and off-road knobs as a real MX tyre has them (seven a section in two staggered rows, shoulder
knobs leaning out with the carcass, each standing square off it). The slick stays Torque Shed's.

Writes Data/Parts/Redrawn/tyre/<tread>.glb in the parts agreement (glTF frame: x across -1..1, y
from the bead 0 to the crown 1, z 0..1 along 1/32 of the way round; nodes lod0..lod2; materials
named for their surface tags), which Tools/import_blender_parts.py --redrawn turns into
Data/Parts/Blender/tyre/<tread>*.glb and the index. Deterministic: run it again, same bytes.
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
DEEP = 0.045               # a road groove, in tyre heights (4.5 mm on the reference 0.1 m tyre)

# Per LOD: rows along the section, points across the crown, points up each wall.
ROAD_DETAIL = [{"along": 16, "crown": 48, "wall": 6}, {"along": 8, "crown": 20, "wall": 3}, {"along": 1, "crown": 2, "wall": 1}]
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


def groove(tread, u, x):
    """treads.py groove(): how deep a road tread's groove is at u along and x across (at DEEP)."""
    ax = abs(x)
    deep = DEEP
    if ax > 0.75:
        return 0.0
    if tread == "rib":
        return deep if min(ax, abs(ax - 0.45)) < 0.08 else 0.0
    if tread == "touring":
        if abs(ax - 0.3) < 0.08:
            return deep
        return deep * 0.6 if 0.45 < ax < 0.9 and abs((u * 2.0 + ax) % 1.0 - 0.5) < 0.12 else 0.0
    if tread == "sport":
        return deep if 0.15 < ax < 0.9 and abs((u * 2.0 + ax * 0.8) % 1.0 - 0.5) < 0.14 else 0.0
    if tread == "cruiser":
        if ax < 0.07:
            return deep
        return deep if 0.2 < ax < 0.85 and abs((u * 2.0 - ax * 0.6) % 1.0 - 0.5) < 0.13 else 0.0
    if tread == "trail":
        return deep if (u * 3.0) % 1.0 < 0.2 or abs(ax - 0.35) < 0.07 else 0.0
    if tread == "sipes":
        return deep * 0.6 if (u * 3.0) % 1.0 < 0.18 and ax < 0.9 else 0.0
    return 0.0


def groove_soft(tread, u, x, du, dx):
    """The groove's depth averaged over a grid cell (4 x 4 samples), so its walls fall steeply over
    one cell instead of sloping across a whole one."""
    s = 0.0
    for a in (-0.375, -0.125, 0.125, 0.375):
        for b in (-0.375, -0.125, 0.125, 0.375):
            s += groove(tread, (u + a * du) % 1.0, x + b * dx)
    return s / 16.0


def fine_crown(tread, d):
    """The road profile with the crown resampled evenly across (so grooves are as fine everywhere)."""
    pts, on = profile(tread, d)
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


def carcass(tread, lod, mesh):
    road = tread not in OFFROAD
    d = (ROAD_DETAIL if road else OFF_DETAIL)[lod]
    pts, on_crown = profile(tread, d)
    rows = d["along"]
    cols = len(pts)
    grid = np.zeros((rows + 1, cols, 3))
    for i in range(rows + 1):
        u = i / rows
        for j, ((x, y), crown) in enumerate(zip(pts, on_crown)):
            dy = 0.0
            if road and crown and lod < 2:
                dx = abs(pts[min(j + 1, cols - 1)][0] - pts[max(j - 1, 0)][0]) * 0.5
                dy = groove_soft(tread, u, x, 1.0 / rows, max(dx, 1e-3))
            grid[i, j] = (x, y - dy, u)
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
    """(x across, u along, width, length) of each knob in a section: two staggered rows, as an MX
    tyre's (a 50/50's blocks bigger, flatter and fewer)."""
    if tread == "blocks":
        spots = [(0.24, 0.25, 0.44, 0.38), (-0.24, 0.25, 0.44, 0.38), (0.0, 0.75, 0.4, 0.38),
                 (0.66, 0.75, 0.36, 0.34), (-0.66, 0.75, 0.36, 0.34), (0.86, 0.25, 0.22, 0.3), (-0.86, 0.25, 0.22, 0.3)]
    else:
        spots = [(0.3, 0.25, 0.34, 0.26), (-0.3, 0.25, 0.34, 0.26), (0.88, 0.25, 0.22, 0.24), (-0.88, 0.25, 0.22, 0.24),
                 (0.0, 0.75, 0.4, 0.26), (0.62, 0.75, 0.3, 0.26), (-0.62, 0.75, 0.3, 0.26)]
    if lod == 1:
        spots = [s for s in spots if abs(s[0]) < 0.7]
    return spots if lod < 2 else []


def section(tread, lod):
    rubber, steel = Mesh(), Mesh()
    carcass(tread, lod, rubber)
    if tread in OFFROAD:
        h = 0.1 if tread == "blocks" else 0.19
        for x, u, w, length in knob_layout(tread, lod):
            top = knob(rubber, tread, x, u, w, length, h)
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
