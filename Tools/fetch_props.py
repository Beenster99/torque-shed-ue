#!/usr/bin/env python3
"""Downloads the shed's free scanned props and prop materials from Poly Haven (all CC0) into
build/props (not kept in git; run once on the PC that builds the shed, then Tools/ue_import_props.py).

    python Tools/fetch_props.py              every Poly Haven model in Data/Props/props.json, and the
                                             scanned maps Data/Props/materials.json's 'scans' name
    python Tools/fetch_props.py --res 4k     sharper (and about 4x bigger)
    python Tools/fetch_props.py --list       what it would fetch, and how big, without fetching

Models land in build/props/<id>/<id>.gltf with their .bin and textures/ beside them (as Poly Haven
lays them out); material scans in build/props/_scans/<id>/ as color.jpg, normal.jpg (OpenGL) and
rough.jpg. Already-fetched files are skipped, so it's safe to run again.
"""
import argparse, json, os, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "build", "props")
UA = {"User-Agent": "TorqueShedProto-prop-fetch/1.0"}
SCAN_MAPS = {"Diffuse": "color.jpg", "nor_gl": "normal.jpg", "Rough": "rough.jpg"}


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=180) as r:
        return r.read()


def files(asset):
    return json.loads(get("https://api.polyhaven.com/files/" + asset))


def save(url, path):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return 0
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = get(url)
    with open(path + ".part", "wb") as f:
        f.write(data)
    os.replace(path + ".part", path)
    return len(data)


def model_plan(asset, res):
    g = files(asset)["gltf"]
    r = g.get(res) or g.get("2k") or g[sorted(g)[0]]
    main = r["gltf"]
    plan = [(main["url"], os.path.join(OUT, asset, asset + ".gltf"), main["size"])]
    for rel, inc in main.get("include", {}).items():
        plan.append((inc["url"], os.path.join(OUT, asset, rel), inc["size"]))
    return plan


def scan_plan(asset, res):
    f = files(asset)
    plan = []
    for key, name in SCAN_MAPS.items():
        by = f.get(key, {})
        r = by.get(res) or by.get("2k")
        if r and "jpg" in r:
            plan.append((r["jpg"]["url"], os.path.join(OUT, "_scans", asset, name), r["jpg"]["size"]))
    return plan


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--res", default="2k")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()
    props = json.load(open(os.path.join(ROOT, "Data", "Props", "props.json")))
    mats = json.load(open(os.path.join(ROOT, "Data", "Props", "materials.json")))
    models = sorted({p["polyhaven"] for p in props["props"].values() if "polyhaven" in p})
    # Scans kept in git (Data/Props/Scans/<id>/, drawn by a tool: brushed_metal) aren't Poly Haven's.
    # The bases' scans and any a material names for itself (the yard kit's: materials.json 'scan').
    wanted = set(mats["scans"].values()) | {m["scan"] for m in mats["materials"].values() if m.get("scan")}
    scans = sorted(s for s in wanted if not os.path.isdir(os.path.join(ROOT, "Data", "Props", "Scans", s)))
    total, got, failed = 0, 0, []
    for kind, assets, planner in (("model", models, model_plan), ("scan", scans, scan_plan)):
        for a in assets:
            try:
                plan = planner(a, args.res)
            except Exception as e:
                print("  %s %s: lookup FAILED (%s)" % (kind, a, e))
                failed.append(a)
                continue
            size = sum(p[2] for p in plan)
            total += size
            if args.list:
                print("%-6s %-32s %6.1f MB" % (kind, a, size / 1e6))
                continue
            try:
                n = sum(save(url, path) for url, path, _ in plan)
                got += n
                print("%-6s %-32s %s" % (kind, a, "%.1f MB" % (n / 1e6) if n else "already here"))
            except Exception as e:
                print("  %s %s: FAILED (%s)" % (kind, a, e))
                failed.append(a)
    print("%d models, %d scans, %.0f MB in all%s" % (len(models), len(scans), total / 1e6,
                                                     "" if args.list else ", %.0f MB fetched now into %s" % (got / 1e6, OUT)))
    if failed:
        print("not fetched:", ", ".join(failed))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
