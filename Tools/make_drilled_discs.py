"""Drilled brake discs: more cross-drilled and perforated discs for the catalogue.

    python Tools/make_drilled_discs.py && python Tools/parts_overlay.py && python Tools/import_blender_parts.py --redrawn

beenster's build 101: "pretty much ALL brake discs are drilled these days, please make sure we have
selections of discs which are perforated". Torque Shed has one drilled disc (280 mm). This adds
six, drawn as Torque Shed draws its discs (blender/parts/disc/discs.py: a steel rotor on a carrier,
holes cut right through, a floating rotor held on its carrier by buttons, five carrier bolts), with
the holes round rather than stepped (the grid points round each hole sit on its circle):

    drilled_260            260 mm one-piece drilled (classic and cafe fronts)
    drilled_floating_300   300 mm floating, three staggered rows (road, naked, tourer)
    drilled_floating_320   320 mm floating, three staggered rows (sport, race)
    drilled_wave_270       270 mm wave edge, drilled (MX, enduro, supermoto)
    drilled_220_rear       220 mm drilled rear
    drilled_245_rear       245 mm drilled rear, black carrier

and redraws Torque Shed's own drilled, floating and wave discs (REDRAW), whose holes came out solid.

It writes each disc's glb to Data/Parts/Redrawn/disc/<id>.glb (the parts agreement: glTF frame, the
disc's face square to x, the origin its middle; nodes lod0..lod2) and its catalogue entry to
Data/Parts/Redrawn/parts_overlay.json ("add" > "disc"), with the reference bikes' rear discs for the
modern styles set to a drilled one ("reference"). Deterministic.
"""
import json
import math
import os
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Data", "Parts", "Redrawn", "disc")
OVERLAY = os.path.join(REPO, "Data", "Parts", "Redrawn", "parts_overlay.json")

STEEL = [0.7, 0.71, 0.73]
# rows of [how far out, how many, hole radius (of the disc's radius), offset] (discs.py HOLES, the drilled ones)
THREE_ROWS = [[0.7, 24, 0.03, 0.0], [0.8, 24, 0.03, 0.5], [0.9, 24, 0.03, 0.0]]
DISCS = {
    "drilled_260": {"name": "260 mm drilled disc", "description": "One-piece rotor, cross-drilled all round. A classic upgrade.",
                    "mass": 1.0, "type": "Drilled", "diameter": 260, "style": "drilled", "color": STEEL,
                    "_holes": THREE_ROWS, "_carrier": None},
    "drilled_floating_300": {"name": "300 mm drilled floating disc", "description": "Drilled steel rotor floating on a black carrier. What most road bikes wear now.",
                             "mass": 1.2, "type": "Floating, drilled", "diameter": 300, "style": "floating", "color": STEEL,
                             "_holes": THREE_ROWS, "_carrier": "black_metal", "_buttons": 10},
    "drilled_floating_320": {"name": "320 mm drilled floating disc", "description": "Big drilled rotor on an anodised carrier, for sport and race bikes.",
                             "mass": 1.35, "type": "Floating, drilled", "diameter": 320, "style": "floating", "color": STEEL,
                             "_holes": [[0.69, 30, 0.026, 0.0], [0.79, 30, 0.026, 0.5], [0.89, 30, 0.026, 0.0]], "_carrier": "anodised", "_buttons": 12},
    "drilled_wave_270": {"name": "270 mm drilled wave disc", "description": "Petal edge and drilled face: sheds mud and water, runs cool.",
                         "mass": 0.9, "type": "Wave, drilled", "diameter": 270, "style": "wave", "color": STEEL,
                         "_holes": [[0.74, 18, 0.03, 0.5], [0.86, 18, 0.03, 0.0]], "_carrier": None, "_wave": True},
    "drilled_220_rear": {"name": "220 mm drilled rear disc", "description": "Light drilled rear rotor.",
                         "mass": 0.7, "type": "Drilled", "diameter": 220, "style": "drilled", "color": STEEL,
                         "_holes": [[0.74, 20, 0.032, 0.0], [0.86, 20, 0.032, 0.5]], "_carrier": None},
    "drilled_245_rear": {"name": "245 mm drilled rear disc", "description": "Drilled rear rotor on a black carrier.",
                         "mass": 0.85, "type": "Drilled", "diameter": 245, "style": "drilled", "color": STEEL,
                         "_holes": [[0.74, 22, 0.03, 0.0], [0.86, 22, 0.03, 0.5]], "_carrier": "black_metal"},
}
# Torque Shed's own drilled and floating discs, redrawn here too: their holes are cells of a coarse
# grid in discs.py, too coarse for the holes, so they came out solid. Same discs, same catalogue
# entries (Godot's), only the glbs.
REDRAW = {
    "drilled_280": {"diameter": 280, "_holes": [[0.7, 24, 0.032, 0.0], [0.8, 24, 0.032, 0.5], [0.9, 24, 0.032, 0.0]], "_carrier": "black_metal"},
    "floating_310": {"diameter": 310, "_holes": [[0.74, 20, 0.03, 0.0], [0.86, 20, 0.03, 0.5]], "_carrier": "anodised", "_buttons": 10},
    "floating_330_race": {"diameter": 330, "_holes": [[0.74, 20, 0.03, 0.0], [0.86, 20, 0.03, 0.5]], "_carrier": "anodised", "_buttons": 10},
    "wave_270": {"diameter": 270, "_holes": [[0.78, 12, 0.034, 0.5]], "_carrier": None, "_wave": True},
    "light_wave_260": {"diameter": 260, "_holes": [[0.78, 12, 0.034, 0.5]], "_carrier": None, "_wave": True},
}
# The reference bikes' rear discs: modern styles get a drilled one (the classics keep their plain disc).
REAR_REFERENCE = {"adventure": "drilled_245_rear", "roader": "drilled_245_rear", "tourer": "drilled_245_rear",
                  "sport": "drilled_220_rear", "race": "drilled_220_rear", "naked": "drilled_220_rear",
                  "supermoto": "drilled_220_rear", "scrambler": "drilled_220_rear", "mx": "drilled_220_rear"}

MATS = {
    "raw_steel": {"baseColorFactor": [0.45, 0.45, 0.46, 1.0], "metallicFactor": 1.0, "roughnessFactor": 0.4},
    "black_metal": {"baseColorFactor": [0.035, 0.035, 0.04, 1.0], "metallicFactor": 0.5, "roughnessFactor": 0.55},
    "anodised": {"baseColorFactor": [0.1, 0.25, 0.7, 1.0], "metallicFactor": 0.9, "roughnessFactor": 0.3},
}
DETAIL = [{"segs": 240, "rows": 16, "holes": True}, {"segs": 64, "rows": 4, "holes": False}, {"segs": 16, "rows": 1, "holes": False}]


class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.idx = [], [], []

    def quad(self, q, n):
        """A flat quad in Blender's frame (x across the disc, y and z its face)."""
        q = [np.asarray(v, float) for v in q]
        f = np.cross(q[1] - q[0], q[2] - q[0])
        if np.dot(f, n) < 0.0:
            q = [q[0], q[3], q[2], q[1]]
        n = np.asarray(n, float) / max(np.linalg.norm(n), 1e-12)
        b = len(self.pos)
        self.pos += q
        self.nrm += [n] * 4
        self.idx += [b, b + 1, b + 2, b, b + 2, b + 3]


def hole_centres(holes):
    out = []
    for at, n, hr, off in holes:
        for k in range(n):
            c = (k + off) * 2.0 * math.pi / n
            out.append((at * math.cos(c), at * math.sin(c), hr * 1.1))
    return out


def plate(mesh, r, r0, r1, thick, segs, rows, solid, edge=None, holes=()):
    """discs.py plate(): a flat ring from r0 to r1 of the radius, thick across x, a cell of its grid
    cut through wherever solid() is False, its outer edge at edge(angle). The grid points round a
    hole are moved onto its circle, so the holes come out round."""
    grid = np.zeros((rows + 1, segs, 2))
    cell = (r1 - r0) / rows
    for i in range(rows + 1):
        f = r0 + (r1 - r0) * i / rows
        for j in range(segs):
            a = 2.0 * math.pi * j / segs
            e = edge(a) if (edge and i == rows) else 1.0
            p = np.array([f * e * math.cos(a), f * e * math.sin(a)])
            for hx, hy, hr in holes:
                d = p - (hx, hy)
                dl = float(np.linalg.norm(d))
                if dl > 1e-9 and abs(dl - hr) < cell * 0.75 and 0 < i < rows:
                    p = np.array([hx, hy]) + d / dl * hr
                    break
            grid[i, j] = p * r
    keep = set()
    for i in range(rows):
        for j in range(segs):
            fm = r0 + (r1 - r0) * (i + 0.5) / rows
            am = 2.0 * math.pi * (j + 0.5) / segs
            if solid(fm, am):
                keep.add((i, j))
    for i, j in sorted(keep):
        k = (j + 1) % segs
        for side in (-1.0, 1.0):
            q = [(side * thick * 0.5, *grid[a, b]) for a, b in ((i, j), (i, k), (i + 1, k), (i + 1, j))]
            mesh.quad(q, (side, 0.0, 0.0))
        # Walls on every edge with no cell beyond it: the rim, the bore and each hole's sides.
        for (a, b), nb in ((((i, j), (i, k)), (i - 1, j)), (((i + 1, k), (i + 1, j)), (i + 1, j)),
                           (((i, k), (i + 1, k)), (i, k)), (((i + 1, j), (i, j)), (i, (j - 1) % segs))):
            if nb in keep and 0 <= nb[0] < rows:
                continue
            pa, pb = grid[a], grid[b]
            mid = (pa + pb) * 0.5
            cen = (grid[i, j] + grid[i, k] + grid[i + 1, k] + grid[i + 1, j]) * 0.25
            out = mid - cen
            q = [(thick * 0.5, *pa), (-thick * 0.5, *pa), (-thick * 0.5, *pb), (thick * 0.5, *pb)]
            mesh.quad(q, (0.0, out[0], out[1]))


def cylinder(mesh, x0, x1, cy, cz, rad, sides):
    """A round pin along x (a floating disc's button), capped at x1."""
    ring = [(math.cos(2 * math.pi * k / sides), math.sin(2 * math.pi * k / sides)) for k in range(sides)]
    for k in range(sides):
        c0, c1 = ring[k], ring[(k + 1) % sides]
        q = [(x0, cy + c0[0] * rad, cz + c0[1] * rad), (x0, cy + c1[0] * rad, cz + c1[1] * rad),
             (x1, cy + c1[0] * rad, cz + c1[1] * rad), (x1, cy + c0[0] * rad, cz + c0[1] * rad)]
        mesh.quad(q, (0.0, c0[0] + c1[0], c0[1] + c1[1]))
    for k in range(1, sides - 1, 2):
        idx = [0, k, k + 1, min(k + 2, sides - 1)]
        q = [(x1, cy + ring[m][0] * rad, cz + ring[m][1] * rad) for m in idx]
        mesh.quad(q, (1.0, 0.0, 0.0))


def disc(spec, lod):
    d = DETAIL[lod]
    r = spec["diameter"] / 2000.0
    holes = hole_centres(spec["_holes"]) if d["holes"] else []

    def window(f, a):
        return 0.27 < f < 0.53 and abs((a * 5.0 + math.pi) % (2.0 * math.pi) - math.pi) < 1.6

    def drilled(f, a):
        for hx, hy, hr in holes:
            if math.hypot(f * math.cos(a) - hx, f * math.sin(a) - hy) < hr:
                return True
        return False

    edge = (lambda a: 1.0 - 0.06 * (0.5 + 0.5 * math.cos(a * 12.0))) if spec.get("_wave") else None
    meshes = {"raw_steel": Mesh()}
    segs, rows = d["segs"], d["rows"]
    carrier = spec.get("_carrier")
    if carrier and lod < 2:
        meshes[carrier] = Mesh()
        plate(meshes["raw_steel"], r, 0.6, 1.0, 0.006, segs, max(1, rows // 2) if not d["holes"] else rows, lambda f, a: not drilled(f, a), edge, holes)
        plate(meshes[carrier], r, 0.14, 0.62, 0.008, max(16, segs // 2), max(1, rows // 4), lambda f, a: not window(f, a))
    else:
        if d["holes"]:
            plate(meshes["raw_steel"], r, 0.6, 1.0, 0.006, segs, rows, lambda f, a: not drilled(f, a), edge, holes)
            plate(meshes["raw_steel"], r, 0.14, 0.6, 0.006, max(16, segs // 2), max(1, rows // 4), lambda f, a: not window(f, a))
        else:
            plate(meshes["raw_steel"], r, 0.14, 1.0, 0.006, segs, rows, lambda f, a: lod == 2 or not window(f, a), edge)
    if lod < 2:
        for k in range(spec.get("_buttons", 0)):
            a = 2.0 * math.pi * k / spec["_buttons"]
            cylinder(meshes["raw_steel"], 0.002, 0.007, math.cos(a) * r * 0.61, math.sin(a) * r * 0.61, 0.0045, 8 if lod == 0 else 4)
        for k in range(5):
            a = 2.0 * math.pi * k / 5
            cylinder(meshes["raw_steel"], 0.001, 0.005, math.cos(a) * 0.045, math.sin(a) * 0.045, 0.006, 6)
    return [(m, tag) for tag, m in meshes.items() if m.idx]


def pad4(b, fill=b"\0"):
    return b + fill * ((4 - len(b) % 4) % 4)


def write_glb(path, lods):
    blob = bytearray()
    accessors, views, meshes = [], [], []
    tags = sorted({tag for prims in lods for _, tag in prims}, key=lambda t: (t != "raw_steel", t))

    def add(arr, comp, target, minmax=False):
        data = arr.tobytes()
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(pad4(data))
        a = {"bufferView": len(views) - 1, "componentType": comp, "count": int(arr.shape[0]),
             "type": "SCALAR" if arr.ndim == 1 else {2: "VEC2", 3: "VEC3"}[arr.shape[1]]}
        if minmax:
            a["min"] = [round(float(v), 6) for v in arr.min(axis=0)]
            a["max"] = [round(float(v), 6) for v in arr.max(axis=0)]
        accessors.append(a)
        return len(accessors) - 1

    for k, prims in enumerate(lods):
        out = []
        for m, tag in prims:
            # Blender's frame to glTF's: (x, y, z) -> (x, z, -y).
            P = np.array(m.pos, np.float64); N = np.array(m.nrm, np.float64)
            pos = np.round(np.stack([P[:, 0], P[:, 2], -P[:, 1]], 1), 6).astype("<f4")
            nrm = np.round(np.stack([N[:, 0], N[:, 2], -N[:, 1]], 1), 6).astype("<f4")
            uv = np.round(np.stack([P[:, 1], P[:, 2]], 1), 6).astype("<f4")   # 1 unit = 1 m, across the face
            idx = np.array(m.idx, "<u4" if len(m.pos) > 65535 else "<u2")
            out.append({"attributes": {"POSITION": add(pos, 5126, 34962, True), "NORMAL": add(nrm, 5126, 34962), "TEXCOORD_0": add(uv, 5126, 34962)},
                        "indices": add(idx, 5125 if idx.dtype == np.dtype("<u4") else 5123, 34963), "material": tags.index(tag)})
        meshes.append({"name": "lod%d" % k, "primitives": out})
    j = {"asset": {"version": "2.0", "generator": "Tools/make_drilled_discs.py"}, "scene": 0, "scenes": [{"nodes": [0]}],
         "nodes": [{"name": "part", "children": [1, 2, 3]}] + [{"name": "lod%d" % k, "mesh": k} for k in range(3)],
         "meshes": meshes, "materials": [{"name": t, "pbrMetallicRoughness": MATS[t]} for t in tags],
         "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(blob)}]}
    js = pad4(json.dumps(j, separators=(",", ":"), sort_keys=True).encode(), b" ")
    body = struct.pack("<I4s", len(js), b"JSON") + js + struct.pack("<I4s", len(blob), b"BIN\0") + bytes(blob)
    data = struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if not os.path.exists(path) or open(path, "rb").read() != data:
        open(path, "wb").write(data)
    return [sum(len(m.idx) // 3 for m, _ in p) for p in lods]


def main():
    for pid, spec in DISCS.items():
        tris = write_glb(os.path.join(OUT, pid + ".glb"), [disc(spec, k) for k in range(3)])
        print("%-22s %s triangles (lod0, lod1, lod2)" % (pid, tris))
    for pid, spec in REDRAW.items():
        tris = write_glb(os.path.join(OUT, pid + ".glb"), [disc(spec, k) for k in range(3)])
        print("%-22s %s triangles (lod0, lod1, lod2), redrawn" % (pid, tris))
    ov = json.load(open(OVERLAY, encoding="utf-8"))
    ov.setdefault("add", {})["disc"] = {pid: {k: v for k, v in spec.items() if not k.startswith("_")} for pid, spec in DISCS.items()}
    for style, pid in REAR_REFERENCE.items():
        ov.setdefault("reference", {}).setdefault(style, {})["rear_disc"] = pid
    text = json.dumps(ov, indent=1, ensure_ascii=False) + "\n"
    if open(OVERLAY, encoding="utf-8").read() != text:
        open(OVERLAY, "w", encoding="utf-8").write(text)


if __name__ == "__main__":
    main()
