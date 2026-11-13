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


def packing(model, tmp="/tmp"):
    """Share of empty space inside the bike's middle, between its lowest and highest drawn part.

    beenster, 13 Nov: "the large void space beneath the tank and saddle ... should not exist".
    Rasterises the side elevation (needs node + Playwright, see foundry/shot.js) and, for each
    column between the swingarm pivot and the steering head, counts unfilled pixels between the
    lowest and highest drawn pixel. Returns a fraction 0..1, or None if it could not be drawn.
    """
    import subprocess
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import elevation
    from PIL import Image
    m = dict(model, dimensions=False)
    svg = elevation.draw(m)
    html = os.path.join(tmp, "v2_pack.html")
    png = os.path.join(tmp, "v2_pack.png")
    css = elevation.CSS + ".elev .paper{fill:#fff}.elev .ground{stroke:none}.elev .castin,.elev .hole{fill:#888}"
    open(html, "w").write("<html><head><style>body{margin:0}" + css + "</style></head><body>" + svg + "</body></html>")
    try:
        subprocess.run(["node", os.path.join(os.path.dirname(os.path.abspath(__file__)), "shot.js"), "file://" + html, png, "1600", "1000"],
                       check=True, timeout=90, cwd=tmp, capture_output=True)
    except Exception:
        return None
    im = Image.open(png).convert("L")
    px = im.load()
    g = model["geometry"]
    x_hi = g["wheelbase"] - model["wheels"]["front"]["tyre_r"] - 30
    x_lo = model["swingarm"]["pivot"][0]
    empty = total = 0
    for X in range(int(elevation.X(x_hi)), int(elevation.X(x_lo))):
        col = [px[X, Y] < 250 for Y in range(int(elevation.Y(1150)), int(elevation.Y(60)))]
        if True not in col:
            continue
        a, b = col.index(True), len(col) - 1 - col[::-1].index(True)
        total += b - a + 1
        empty += col[a:b + 1].count(False)
    return empty / total if total else None


PACKING_MAX = 0.12


if __name__ == "__main__":
    fams = json.load(open(os.path.join(ROOT, "data", "families.json")))["families"]
    bad = 0
    for path in sorted(glob.glob(os.path.join(ROOT, "data", "models", "*.json"))):
        m = json.load(open(path))
        print(m["name"])
        res = check(m, fams)
        pk = packing(m)
        if pk is not None:
            res.append((pk <= PACKING_MAX, f"empty space inside the bike {pk * 100:.0f}% (at most {PACKING_MAX * 100:.0f}%)"))
        for ok, msg in res:
            bad += not ok
            print("  ", "ok  " if ok else "FAIL", msg)
    sys.exit(1 if bad else 0)
