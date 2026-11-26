#!/usr/bin/env python3
"""[everyday kits stage 2] Counts what the world store already holds for the everyday kits, over every UK cell
(the cells with a roadx file), by rough region: scenery kinds (feature/), land use (ground/), places worth riding
to (places/), the map's stops (the cell zip's pois.json), building classes (bld/), and road km by class. With
--kits, the kits/ bake too (the new map data pass, torque-shed-world scripts/kits_bake.py).

    python3 Tools/kits_count.py --store DIR [--jobs 8] [--out counts.json] [--kits]

Downloads into DIR (kept, so a second run is quick). Writes counts.json: {"regions": {region: {key: n}}, "cells": n}.
Keys: "feature:<kind>" (points, areas and lines each count one; "+km" for lines' length), "ground:<kind>[:<word>]",
"places:<kind>", "poi:<kind>", "bld:<class>", "road_km:<class>", "kits:<kind>"."""
import argparse
import collections
import concurrent.futures as cf
import io
import json
import math
import os
import sys
import time
import urllib.request
import zipfile

RAW = "https://raw.githubusercontent.com/Beenster99/torque-shed-world/main/"


def region_of(lat, lon):
    """A rough region of the UK from a point (boxes, not borders: good enough for counts)."""
    if lon < -5.45 and 54.0 <= lat <= 55.35:
        return "Northern Ireland"
    if lat >= 55.8 or (lat >= 55.05 and lon < -2.4) or (lat >= 55.3 and lon < -2.0):
        return "Scotland"
    if lon < -2.75 and 51.35 <= lat <= 53.45 and not (lat < 51.6 and lon > -3.0):
        return "Wales"
    if lat >= 54.0:
        return "North East and Cumbria" if lon < -1.8 or lat >= 54.45 else "Yorkshire"
    if lat >= 53.3:
        return "North West" if lon < -2.0 else "Yorkshire"
    if lat >= 52.35:
        return "East of England" if lon > 0.0 else "Midlands"
    if lat >= 51.25 and lon > -0.6:
        return "London and South East" if lat < 51.95 or lon < 0.3 else "East of England"
    if lon < -2.2:
        return "South West"
    return "South"


def cell_mid(cell):
    row, col = (int(v) for v in cell.split("_"))
    return row * 0.25 - 90.0 + 0.125, col * 0.25 - 180.0 + 0.125


def get(url, path, tries=6):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        with open(path, "rb") as f:
            return f.read()
    for t in range(tries):
        try:
            with urllib.request.urlopen(url, timeout=120) as r:
                body = r.read()
            tmp = path + ".part"
            with open(tmp, "wb") as f:
                f.write(body)
            os.replace(tmp, path)
            return body
        except Exception as e:  # noqa: BLE001
            if getattr(e, "code", 0) == 404:
                return None
            time.sleep(2 + 3 * t)
    return None


def count_cell(cell, index, store, kits):
    out = collections.Counter()
    os.makedirs(store, exist_ok=True)
    # scenery kinds
    for folder, key in (("feature", "features"), ("ground", "grounds"), ("places", "places")):
        if cell not in index.get(key, {}):
            continue
        body = get(RAW + "%s/c%s.json" % (folder, cell), os.path.join(store, "%s_%s.json" % (folder, cell)))
        if not body:
            out["missing:" + folder] += 1
            continue
        d = json.loads(body)
        kinds = d.get("kinds", [])
        if folder == "feature":
            for row in d.get("points", []):
                out["feature:" + kinds[row[0]]] += 1
            for row in d.get("areas", []):
                out["feature:" + kinds[row[0]]] += 1
            for row in d.get("lines", []):
                out["feature:" + kinds[row[0]]] += 1
        elif folder == "ground":
            words = d.get("words", [])
            for row in d.get("areas", []):
                k = kinds[row[0]]
                w = words[row[2]] if k in ("pitch", "car_park") and isinstance(row[2], int) and 0 <= row[2] < len(words) else ""
                out["ground:" + k + (":" + w if w else "")] += 1
            for row in d.get("points", []):
                out["ground:" + kinds[row[0]]] += 1
            for row in d.get("lines", []):
                out["ground:" + kinds[row[0]]] += 1
        else:
            for row in d.get("places", []):
                out["places:" + kinds[row[0]] + (":" + str(row[7]) if row[7] else "")] += 1
    # the cell's own zip: stops and roads
    e = index["cells"].get(cell)
    if e and e.get("url"):
        body = get(e["url"], os.path.join(store, "cell_%s.zip" % cell))
        if body:
            z = zipfile.ZipFile(io.BytesIO(body))
            names = z.namelist()
            if "pois.json" in names:
                for p in json.loads(z.read("pois.json")):
                    out["poi:" + p.get("kind", "?")] += 1
            if "roads.json" in names:
                r = json.loads(z.read("roads.json"))
                nodes, classes = r["nodes"], r["classes"]
                for ed in r["edges"]:
                    pts = [nodes[ed["a"]]] + [ed["p"][i:i + 2] for i in range(0, len(ed["p"]), 2)] + [nodes[ed["b"]]]
                    m = sum(math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]) for i in range(1, len(pts)))
                    out["road_km:" + classes[ed["c"]]] += m / 1000.0
        else:
            out["missing:cell"] += 1
    b = index.get("buildings", {}).get(cell)
    if b and b.get("url"):
        body = get(b["url"], os.path.join(store, "bld_%s.zip" % cell))
        if body:
            z = zipfile.ZipFile(io.BytesIO(body))
            head = json.loads(z.read("buildings.json"))
            words = head.get("words", [])
            for n in z.namelist():
                if n.startswith("t") and n.endswith(".json"):
                    for row in json.loads(z.read(n)):
                        w = row[3] if len(row) > 3 else -1
                        out["bld:" + (words[w] if isinstance(w, int) and 0 <= w < len(words) and words[w] else "(none)")] += 1
        else:
            out["missing:bld"] += 1
    if kits and cell in index.get("kits", {}):
        body = get(RAW + "kits/c%s.json" % cell, os.path.join(store, "kits_%s.json" % cell))
        if body:
            d = json.loads(body)
            kinds = d.get("kinds", [])
            for row in d.get("points", []) + d.get("areas", []) + d.get("lines", []):
                out["kits:" + kinds[row[0]]] += 1
            for k, v in d.get("km", {}).items():
                out["kits_km:" + k] += v
    return cell, out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--store", required=True)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--out", default="counts.json")
    ap.add_argument("--kits", action="store_true")
    ap.add_argument("--cells", nargs="*", default=[])
    args = ap.parse_args(argv)
    os.makedirs(args.store, exist_ok=True)
    index = json.loads(get(RAW + "index.json", os.path.join(args.store, "index_%d.json" % int(time.time() // 3600))))
    cells = args.cells or sorted(index["roadx"].keys())
    regions = collections.defaultdict(collections.Counter)
    per_cell = {}
    with cf.ThreadPoolExecutor(args.jobs) as ex:
        futs = [ex.submit(count_cell, c, index, args.store, args.kits) for c in cells]
        for n, f in enumerate(cf.as_completed(futs), 1):
            cell, c = f.result()
            per_cell[cell] = c
            regions[region_of(*cell_mid(cell))].update(c)
            regions["UK"].update(c)
            if n % 50 == 0:
                print("%d of %d cells" % (n, len(cells)), flush=True)
    with open(args.out, "w") as f:
        json.dump({"cells": len(cells), "release": index.get("release"),
                   "regions": {r: dict(sorted(c.items())) for r, c in regions.items()},
                   "per_cell": {k: dict(v) for k, v in sorted(per_cell.items())}}, f, indent=0)
    print("wrote %s: %d cells" % (args.out, len(cells)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
