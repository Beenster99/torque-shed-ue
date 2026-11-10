#!/usr/bin/env python3
"""The picture sheets: every build's pictures before and after the solver, packed one sheet a sweep,
and the facts of each build for the page that shows them (page.html).

  make_sheets.py builds.json <folder of fit_trial grid output> <out folder>
"""
import glob, json, os, sys
from PIL import Image

builds = json.load(open(sys.argv[1]))["builds"]
src, out = sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
results = {}
for path in glob.glob(os.path.join(src, "results_*.jsonl")):
    for line in open(path):
        line = line.strip()
        if line:
            try:
                r = json.loads(line)
            except ValueError:
                continue
            results[r["n"]] = r

TW, TH, COLS = 330, 330, 8
TITLES = {"0_designed": "The 14 designed bikes", "1_engine": "Engine swaps", "2_tank": "Tank swaps", "3_frame": "Frame swaps", "4_fork": "Fork swaps",
          "5_bars": "Handlebar swaps", "6_seat": "Seat swaps", "7_tail": "Tail swaps", "8_exhaust": "Exhaust swaps", "9_fairing": "Fairing swaps",
          "10_swingarm": "Swingarm swaps", "11_engine_x_tank_chopper": "Engine and tank on the chopper", "11_engine_x_tank_cafe": "Engine and tank on the cafe racer",
          "11_engine_x_tank_sport": "Engine and tank on the sports bike", "11_engine_x_tank_adventure": "Engine and tank on the adventure bike",
          "12_top_half": "Top halves swapped between styles", "13_random": "Random bikes, every slot free"}
sweeps = []
for n, b in enumerate(builds):
    if b["sweep"] not in [s["key"] for s in sweeps]:
        sweeps.append({"key": b["sweep"], "title": TITLES.get(b["sweep"], b["sweep"]), "builds": []})
    r = results.get(n)
    if r is None:
        continue
    sw = [s for s in sweeps if s["key"] == b["sweep"]][0]
    wb, wa = r["wrong_before"], r["wrong_after"]
    status = "fine" if not r["before"] else "mended" if not r["after"] else "partly" if wa < wb * 0.7 else "worse" if wa > wb * 1.05 else "stuck"
    sw["builds"].append({"n": n, "row": b["row"], "col": b["col"], "status": status, "wb": round(wb), "wa": round(wa), "tier": r["tier"],
                         "before": [[f["a"], f["b"], round(f["amount"] * (1000 if not f["b"] else 1))] for f in r["before"]],
                         "after": [[f["a"], f["b"], round(f["amount"] * (1000 if not f["b"] else 1))] for f in r["after"]], "did": r["did"]})

for si, sw in enumerate(sweeps):
    tiles = []
    for b in sw["builds"]:
        b["a"] = len(tiles)
        tiles.append(os.path.join(src, "%05d_a.ppm" % b["n"]))
        if b["status"] != "fine":
            b["b"] = len(tiles)
            tiles.append(os.path.join(src, "%05d_b.ppm" % b["n"]))
    # (a sheet stays under about 12 MB: 240 tiles each)
    sw["sheets"] = []
    for part in range(0, max(1, len(tiles)), 240):
        chunk = tiles[part:part + 240]
        rows = max(1, (len(chunk) + COLS - 1) // COLS)
        sheet = Image.new("RGB", (COLS * TW, rows * TH), (236, 236, 236))
        for i, t in enumerate(chunk):
            try:
                im = Image.open(t).resize((TW, TH), Image.LANCZOS)
            except Exception:
                continue
            sheet.paste(im, ((i % COLS) * TW, (i // COLS) * TH))
        name = "sheet_%02d_%d.jpg" % (si, part // 240)
        sheet.save(os.path.join(out, name), quality=70, optimize=True)
        sw["sheets"].append({"file": name, "rows": rows})
json.dump({"cols": COLS, "per": 240, "sweeps": sweeps, "total": len(builds), "done": len(results)}, open(os.path.join(out, "data.json"), "w"), separators=(",", ":"))
tot = {}
for sw in sweeps:
    for b in sw["builds"]:
        tot[b["status"]] = tot.get(b["status"], 0) + 1
print(len(results), "of", len(builds), tot)
