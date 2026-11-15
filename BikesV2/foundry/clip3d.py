"""Part-against-part clipping check for the 3D build (Definition pass).

    python3 foundry/build3d.py <model> --definition --clip            list what clips, against the baseline
    python3 foundry/build3d.py <model> --definition --clip-accept     add this bike's overlaps to the baseline

Every visible part is grouped by its name (front_spoke3, front_spoke4 -> front_spoke). For each pair of groups
whose surfaces cross, the check measures how deep one goes into the other, in mm. Parts are meant to overlap at
joints, so the measure alone says little: the judgement comes from the baseline (data/clip_baseline.json), which
holds the overlaps of bikes that have been looked over and signed off. A pair is reported when it is not in the
baseline, or goes deeper than the baseline allows.
"""
import json
import os
import re

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(ROOT, "data", "clip_baseline.json")
SLACK = 8.0        # mm deeper than the baseline before a known pair is reported
NEW_MIN = 6.0      # mm: a pair the baseline has never seen is reported from this depth
CAP = 80.0         # mm: beyond this the "inside" test is unreliable (open sheets), so depths are capped
SAMPLE = 3000      # vertices tested per part and pair


def stem(name):
    s = re.sub(r"\.\d+$", "", name)
    s = re.sub(r"[-+]?\d+(\.\d+)?", "", s)
    s = re.sub(r"_+", "_", s).strip("_")
    return s or name


def parts():
    dg = bpy.context.evaluated_depsgraph_get()
    out = []
    for o in bpy.data.objects:
        if o.type != "MESH" or o.hide_render:
            continue
        try:
            if o.hide_get():
                continue
        except RuntimeError:
            continue
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        mw = ev.matrix_world
        vs = [mw @ v.co for v in me.vertices]
        polys = [tuple(p.vertices) for p in me.polygons]
        ev.to_mesh_clear()
        if len(vs) < 4 or not polys:
            continue
        lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
        hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
        out.append(dict(name=o.name, stem=stem(o.name), vs=vs, lo=lo, hi=hi, tree=BVHTree.FromPolygons(vs, polys, epsilon=0.0)))
    return out


def depth(a, b):
    """How deep (mm) the vertices of a sit inside b, and how many do."""
    step = max(1, len(a["vs"]) // SAMPLE)
    worst, n = 0.0, 0
    for v in a["vs"][::step]:
        if not all(b["lo"][i] <= v[i] <= b["hi"][i] for i in range(3)):
            continue
        loc, nor, _, dist = b["tree"].find_nearest(v)
        if loc is None or (v - loc).dot(nor) >= 0:
            continue
        # a point truly inside a closed part sees the part in every direction
        if any(b["tree"].ray_cast(v, Vector(d))[0] is None for d in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))):
            continue
        n += 1
        worst = max(worst, min(CAP, dist * 1000.0))
    return worst, n


def measure():
    P = parts()
    found = {}
    for i, a in enumerate(P):
        for b in P[i + 1:]:
            if a["stem"] == b["stem"]:
                continue
            if any(a["lo"][k] > b["hi"][k] or b["lo"][k] > a["hi"][k] for k in range(3)):
                continue
            if not a["tree"].overlap(b["tree"]):
                continue
            d1, n1 = depth(a, b)
            d2, n2 = depth(b, a)
            d = max(d1, d2)
            if d <= 0:
                continue
            key = " | ".join(sorted((a["stem"], b["stem"])))
            cur = found.get(key)
            if not cur or d > cur["depth"]:
                found[key] = dict(depth=round(d, 1), points=n1 + n2, parts=[a["name"], b["name"]])
    return found


def baseline():
    return json.load(open(BASE)) if os.path.exists(BASE) else {"about": "Overlaps (mm) between part groups on bikes that have been looked over and signed off. Written by foundry/clip3d.py.", "bikes": [], "pairs": {}}


def run(name, accept=False):
    found = measure()
    base = baseline()
    os.makedirs(os.path.join(ROOT, "out", "clip"), exist_ok=True)
    if accept:
        for k, v in found.items():
            base["pairs"][k] = max(base["pairs"].get(k, 0.0), v["depth"])
        if name not in base["bikes"]:
            base["bikes"].append(name)
        base["pairs"] = dict(sorted(base["pairs"].items()))
        json.dump(base, open(BASE, "w"), indent=1)
        print(f"clip baseline: {len(found)} overlapping pairs on {name} accepted, {len(base['pairs'])} pairs known")
        return []
    bad = []
    for k, v in found.items():
        known = base["pairs"].get(k)
        if known is None and v["depth"] >= NEW_MIN:
            bad.append(dict(pair=k, why="new", **v))
        elif known is not None and v["depth"] > known + SLACK:
            bad.append(dict(pair=k, why=f"deeper than {known:g}", **v))
    bad.sort(key=lambda r: -r["depth"])
    json.dump(dict(model=name, pairs=len(found), reported=bad), open(os.path.join(ROOT, "out", "clip", name + ".json"), "w"), indent=1)
    for r in bad:
        print(f"CLIP {r['depth']:5.1f} mm  {r['pair']}  ({r['why']}; {r['parts'][0]} / {r['parts'][1]}; {r['points']} points)")
    print(f"clip check {name}: {len(found)} overlapping pairs, {len(bad)} reported")
    return bad
