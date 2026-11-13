"""Automatic checks on model description files. Grows with the pipeline.

So far: the model sits inside its family envelope, and the engine and radiator
keep clear of the front tyre with the fork fully compressed (beenster, 13 Nov:
"the Corvano engine is too close to the front tyre").

Usage: python3 foundry/checks.py            -> checks every model, exit 1 on a failure
"""
import glob
import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def box_pts(c, ang, start, length, width):
    u = (math.cos(math.radians(ang)), math.sin(math.radians(ang)))
    n = (-u[1], u[0])
    return [(c[0] + u[0] * d + n[0] * s, c[1] + u[1] * d + n[1] * s) for d in (start, start + length) for s in (-width / 2, width / 2)]


def check(model, fams):
    fam = fams[model["family"].lower()]
    g, out = model["geometry"], []

    def rng(name, v, r):
        ok = r[0] <= v <= r[1]
        out.append((ok, f"{name} {v:g} in {r[0]:g} to {r[1]:g}"))

    rng("wheelbase", g["wheelbase"], fam["wheelbase"])
    rng("rake", g["rake"], fam["rake"])
    rng("seat height", g["seat_height"], fam["seat_height"])
    rng("ground clearance", g["ground_clearance"], fam["ground_clearance"])
    rng("hip position", g["hip_x"], fam["rider"]["hip_x"])
    # front tyre against engine and radiator, fork fully compressed
    rake, travel = math.radians(g["rake"]), fam["fork_travel"][1]
    rt = model["wheels"]["front"]["tyre_r"]
    c = (g["wheelbase"] - math.sin(rake) * travel, rt + math.cos(rake) * travel)
    pts, e = [], model["engine"]
    for cy in e["cylinders"]:
        pts += box_pts(e["crank"], cy["angle"], cy["start"], cy["length"] + cy["head"] + 30, cy["bore_w"] + 26)
    pts += [tuple(p[:2]) for p in e["case"]]
    r = model.get("radiator")
    if r:
        pts += box_pts(r["at"], r["angle"], 0, r["len"], r["w"])
    gap = min(math.hypot(p[0] - c[0], p[1] - c[1]) for p in pts) - rt
    need = fam["clearance"]["front_tyre_min"]
    out.append((gap >= need, f"front tyre to engine or radiator at full compression {gap:.0f} mm (needs {need})"))
    return out


if __name__ == "__main__":
    fams = json.load(open(os.path.join(ROOT, "data", "families.json")))["families"]
    bad = 0
    for path in sorted(glob.glob(os.path.join(ROOT, "data", "models", "*.json"))):
        m = json.load(open(path))
        print(m["name"])
        for ok, msg in check(m, fams):
            bad += not ok
            print("  ", "ok  " if ok else "FAIL", msg)
    sys.exit(1 if bad else 0)
