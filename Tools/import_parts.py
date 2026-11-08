"""Copies the Godot game's parts catalogue (Beenster99/torque-shed, data/parts.json) into this repo
at Data/Parts/parts.json, the file the Unreal game reads at run time (Fit/PartsDb, TorqueFit).

The Godot JSON stays the source of truth: this script copies it unchanged (no reformatting, so a
diff against the Godot repo is empty), checks it is the shape the loader expects, and writes
Data/Parts/source.json saying which commit it came from. Run it whenever the catalogue changes:

    python Tools/import_parts.py --godot ../torque-shed

It needs no Unreal: the loader is plain C++ (Source/TorqueShedProto/Fit) shared with FitTest.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Data", "Parts")

# Fields every part of every kind has; anything else is per kind and read by name.
REQUIRED = ("name", "description", "color")
# Kinds the fitment system maps to model parts and slots (the rest load but nothing fits them yet).
FITTED_KINDS = ("frame", "fork", "shock", "wheel", "tyre", "tank", "saddle", "handlebars", "engine",
                "exhaust", "headlight", "tail", "mirrors", "front_mudguard", "indicators", "side_panels",
                "rearsets", "swingarm")


def check(cat):
    problems = []
    if not isinstance(cat, dict):
        return ["the top level is not an object of kinds"]
    for kind, parts in cat.items():
        if not isinstance(parts, dict):
            problems.append("%s: not an object of parts" % kind)
            continue
        for pid, p in parts.items():
            if not isinstance(p, dict):
                problems.append("%s/%s: not an object" % (kind, pid))
                continue
            for f in REQUIRED:
                if f not in p:
                    problems.append("%s/%s: no %s" % (kind, pid, f))
    for kind in FITTED_KINDS:
        if kind not in cat:
            problems.append("no %s kind (the fitment system expects it)" % kind)
    return problems


def godot_commit(godot):
    try:
        return subprocess.check_output(["git", "-C", godot, "rev-parse", "HEAD"], text=True).strip()
    except Exception:
        return "unknown"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--godot", default=os.path.join(os.path.dirname(REPO), "torque-shed"),
                    help="a checkout of Beenster99/torque-shed")
    args = ap.parse_args()
    src = os.path.join(args.godot, "data", "parts.json")
    if not os.path.isfile(src):
        sys.exit("no catalogue at %s (pass --godot <checkout of Beenster99/torque-shed>)" % src)
    raw = open(src, "rb").read()
    cat = json.loads(raw)
    problems = check(cat)
    for p in problems:
        print("problem:", p)
    if problems:
        sys.exit("not imported: fix the catalogue first")
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "parts.json"), "wb") as f:
        f.write(raw)
    info = {
        "source": "Beenster99/torque-shed data/parts.json",
        "commit": godot_commit(args.godot),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "kinds": len(cat),
        "parts": sum(len(v) for v in cat.values()),
        "note": "Copied unchanged by Tools/import_parts.py; edit the Godot repo, not this copy.",
    }
    json.dump(info, open(os.path.join(OUT, "source.json"), "w"), indent=1)
    print("%d kinds, %d parts from %s" % (info["kinds"], info["parts"], info["commit"][:10]))


if __name__ == "__main__":
    main()
