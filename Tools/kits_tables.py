#!/usr/bin/env python3
"""[everyday kits stage 2] The map data pass's tables for the kit threads, from the kits/ bake files that
Tools/kits_count.py --kits downloaded (STORE/kits_<cell>.json): everything counted by region, the roadside stops'
details, bridges by what they carry, length, structure, material, layer and what they cross, and the fast roads'
km. Writes Markdown.

    python3 Tools/kits_tables.py --store DIR --out TABLES.md
"""
import argparse
import collections
import glob
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kits_count import region_of  # noqa: E402

UNDER = [(1, "water"), (2, "railway"), (4, "road"), (8, "path")]
LENGTHS = [(10, "under 10 m"), (25, "10-25 m"), (50, "25-50 m"), (100, "50-100 m"), (250, "100-250 m"), (1e9, "over 250 m")]


def line_pts(row):
    lat, lon = row[5] / 1e5, row[6] / 1e5
    pts = [(lat, lon)]
    d = row[7:]
    for i in range(0, len(d) - 1, 2):
        lat += d[i] / 1e5
        lon += d[i + 1] / 1e5
        pts.append((lat, lon))
    return pts


def metres(pts):
    m = 0.0
    for (a0, o0), (a1, o1) in zip(pts, pts[1:]):
        m += math.hypot((a1 - a0) * 111320.0, (o1 - o0) * 111320.0 * math.cos(math.radians((a0 + a1) / 2)))
    return m


def table(rows, head):
    out = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--store", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    count = collections.defaultdict(collections.Counter)      # region -> kind -> n
    words = collections.defaultdict(collections.Counter)      # kind -> word -> n
    km = collections.defaultdict(collections.Counter)         # region -> road class -> km
    bridges = collections.defaultdict(collections.Counter)    # carries -> facet -> n
    cells = 0
    for f in sorted(glob.glob(os.path.join(a.store, "kits_*.json"))):
        d = json.load(open(f))
        cells += 1
        K, W = d["kinds"], d["words"]
        r0, c0 = (int(v) for v in d["cell"].split("_"))
        cell_region = region_of(r0 * 0.25 - 90 + 0.125, c0 * 0.25 - 180 + 0.125)
        for p in d["points"]:
            k = K[p[0]]
            count[region_of(p[3] / 1e5, p[4] / 1e5)][k] += 1
            words[k][W[p[1]] or "-"] += 1
        for p in d["areas"]:
            k = K[p[0]]
            count[region_of(p[3] / 1e5, p[4] / 1e5)][k] += 1
            words[k][W[p[1]] or "-"] += 1
        for row in d["lines"]:
            k = K[row[0]]
            pts = line_pts(row)
            reg = region_of(*pts[0])
            count[reg][k] += 1
            if k.startswith("bridge_"):
                carries = k[len("bridge_"):]
                m = metres(pts)
                b = bridges[carries]
                b["all"] += 1
                b["len:" + next(n for lim, n in LENGTHS if m < lim)] += 1
                b["structure:" + (W[row[1]] or "not mapped")] += 1
                b["material:" + (W[row[2]] or "not mapped")] += 1
                b["layer:%d" % row[3]] += 1
                under = [n for bit, n in UNDER if row[4] & bit]
                b["crosses:" + (" + ".join(under) if under else "nothing mapped")] += 1
            else:
                words[k][W[row[1]] or "-"] += 1
        for cls, v in d.get("km", {}).items():
            km[cell_region][cls] += v
    regions = sorted(r for r in count if r != "UK")
    kinds = sorted({k for r in count.values() for k in r})
    md = ["# Everyday kits, stage 2: what the map holds (%d cells)" % cells, ""]
    md.append("Counts from OpenStreetMap through the kits bake (torque-shed-world scripts/kits_bake.py). Regions are rough boxes, not borders.")
    md.append("")
    md.append("## Everything, by region")
    md.append("")
    md.append(table([[k] + [count[r][k] for r in regions] + [sum(count[r][k] for r in regions)] for k in kinds], ["kind"] + regions + ["UK"]))
    md.append("")
    md.append("## What the map says about each kind (its main word, top 8)")
    md.append("")
    md.append(table([[k, ", ".join("%s %d" % (w, n) for w, n in words[k].most_common(8))] for k in sorted(words)], ["kind", "words"]))
    md.append("")
    md.append("## Bridges")
    md.append("")
    for carries in sorted(bridges):
        b = bridges[carries]
        md.append("### Carrying a %s (%d)" % (carries, b["all"]))
        md.append("")
        for facet in ("len", "crosses", "structure", "material", "layer"):
            items = sorted(((k.split(":", 1)[1], n) for k, n in b.items() if k.startswith(facet + ":")), key=lambda t: -t[1])[:10]
            md.append("- **%s**: %s" % (facet, ", ".join("%s %d" % (k, n) for k, n in items)))
        md.append("")
    md.append("## Road km by region")
    md.append("")
    classes = ["motorway", "motorway_link", "trunk", "trunk_link", "primary", "secondary", "tertiary", "unclassified", "residential"]
    md.append(table([[r] + [round(km[r][c]) for c in classes] for r in sorted(km)], ["region"] + classes))
    md.append("")
    open(a.out, "w").write("\n".join(md))
    print("wrote", a.out)


if __name__ == "__main__":
    main(sys.argv[1:])
