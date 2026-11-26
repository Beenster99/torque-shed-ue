#!/usr/bin/env python3
"""[everyday kits] Finds examples of a kind of thing near a road, from the store files Tools/kits_count.py downloaded,
for pictures: prints "lat,lon road_m" for the first few.

    python3 Tools/kits_find.py --store DIR --what feature:bus_stop [--cells 577_711 ...] [--near 5,40] [--n 5]
                               [--classes primary,secondary]

--what is folder:kind (feature, ground, places, poi, bld) or kits:kind (kits/ files, once baked)."""
import argparse
import io
import json
import math
import os
import sys
import zipfile


def frame_of(cell):
    row, col = (int(v) for v in cell.split("_"))
    return row * 0.25 - 90.0 + 0.125, col * 0.25 - 180.0 + 0.125


def roads_of(store, cell, classes):
    z = zipfile.ZipFile(os.path.join(store, "cell_%s.zip" % cell))
    r = json.loads(z.read("roads.json"))
    reg = json.loads(z.read("region.json"))
    nodes, segs = r["nodes"], []
    for e in r["edges"]:
        if classes and r["classes"][e["c"]] not in classes:
            continue
        pts = [nodes[e["a"]]] + [e["p"][i:i + 2] for i in range(0, len(e["p"]), 2)] + [nodes[e["b"]]]
        segs += list(zip(pts, pts[1:]))
    b = reg["bounds"]
    return segs, (b[2] / 0.125, b[3] / 0.125)


def dist_seg(p, a, b):
    ax, az = a
    bx, bz = b
    dx, dz = bx - ax, bz - az
    L = dx * dx + dz * dz
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((p[0] - ax) * dx + (p[1] - az) * dz) / L))
    return math.hypot(p[0] - ax - t * dx, p[1] - az - t * dz)


def things(store, cell, what):
    folder, kind = what.split(":", 1)
    word = None
    if ":" in kind:
        kind, word = kind.split(":", 1)
    out = []
    if folder == "road":
        # (a road of that class: the middle of each of its pieces)
        z = zipfile.ZipFile(os.path.join(store, "cell_%s.zip" % cell))
        reg = json.loads(z.read("region.json"))
        r = json.loads(z.read("roads.json"))
        f, b = reg["frame"], reg["bounds"]
        mlon, mlat = b[2] / 0.125, b[3] / 0.125
        for e in r["edges"]:
            if r["classes"][e["c"]] == kind and len(e["p"]) >= 4:
                x, zz = e["p"][len(e["p"]) // 4 * 2], e["p"][len(e["p"]) // 4 * 2 + 1]
                out.append((f["lat"] - zz / mlat, f["lon"] + x / mlon, ""))
        return out
    if folder == "poi":
        z = zipfile.ZipFile(os.path.join(store, "cell_%s.zip" % cell))
        reg = json.loads(z.read("region.json"))
        f = reg["frame"]
        b = reg["bounds"]
        mlon, mlat = b[2] / 0.125, b[3] / 0.125
        for p in json.loads(z.read("pois.json")):
            if p["kind"] == kind:
                out.append((f["lat"] - p["z"] / mlat, f["lon"] + p["x"] / mlon, p.get("name", "")))
        return out
    if folder == "bld":
        z = zipfile.ZipFile(os.path.join(store, "bld_%s.zip" % cell))
        words = json.loads(z.read("buildings.json"))["words"]
        for n in z.namelist():
            if n.startswith("t"):
                for row in json.loads(z.read(n)):
                    if 0 <= row[3] < len(words) and words[row[3]] == kind:
                        out.append((row[9] / 1e6, row[10] / 1e6, ""))
        return out
    path = os.path.join(store, "%s_%s.json" % (folder, cell))
    if not os.path.exists(path):
        return out
    d = json.load(open(path))
    kinds = d["kinds"]
    if folder == "places":
        for row in d["places"]:
            if kinds[row[0]] == kind and (word is None or row[7] == word):
                out.append((row[3] / 1e5, row[4] / 1e5, row[1]))
        return out
    words = d.get("words", [])
    for key in ("points", "areas", "lines"):
        for row in d.get(key, []):
            if kinds[row[0]] != kind:
                continue
            if folder == "kits":
                if word is not None and words[row[1]] != word:
                    continue
                i = 5 if key == "lines" else 3
                out.append((row[i] / 1e5, row[i + 1] / 1e5, words[row[1]]))
                continue
            # (feature/ and ground/ rows: kind, then a few fields, then lat, lon in 1e-5 or 1e-6 degrees: the first
            # pair of big numbers)
            first = next((i for i, v in enumerate(row) if i > 0 and isinstance(v, int) and abs(v) > 100000), None)
            if first is None or first + 1 >= len(row):
                continue
            lat, lon = row[first], row[first + 1]
            s = 1e5 if abs(lat) < 2e7 else 1e6
            if key == "areas":
                # (an outline: its middle, from the steps after its first point)
                pts, a, o = [(lat, lon)], lat, lon
                rest = [v for v in row[first + 2:] if isinstance(v, int)]
                for j in range(0, len(rest) - 1, 2):
                    a, o = a + rest[j], o + rest[j + 1]
                    pts.append((a, o))
                lat, lon = sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)
            out.append((lat / s, lon / s, ""))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--store", required=True)
    ap.add_argument("--what", required=True)
    ap.add_argument("--cells", nargs="*", default=["577_711"])
    ap.add_argument("--near", default="4,40")
    ap.add_argument("--n", type=int, default=5)
    ap.add_argument("--classes", default="")
    a = ap.parse_args(argv)
    lo, hi = (float(v) for v in a.near.split(","))
    classes = set(c for c in a.classes.split(",") if c)
    found = 0
    for cell in a.cells:
        segs, (mlon, mlat) = roads_of(a.store, cell, classes)
        flat, flon = frame_of(cell)
        grid = {}
        for s in segs:
            k = (int(s[0][0] // 100), int(s[0][1] // 100))
            grid.setdefault(k, []).append(s)
        for lat, lon, name in things(a.store, cell, a.what):
            x, z = (lon - flon) * mlon, -(lat - flat) * mlat
            k = (int(x // 100), int(z // 100))
            d = min((dist_seg((x, z), s[0], s[1]) for dx in (-1, 0, 1) for dz in (-1, 0, 1) for s in grid.get((k[0] + dx, k[1] + dz), ())), default=1e9)
            if lo <= d <= hi:
                print("%.5f,%.5f %.0f %s" % (lat, lon, d, name))
                found += 1
                if found >= a.n:
                    return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
