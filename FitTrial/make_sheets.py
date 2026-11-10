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

# --- Round 2: realism rules. What a real bike of any type keeps to, and each type's own proportions. -----
# The limits are judgement, set from real bikes, NOT from the 14 designed bikes: several of those break them.
shape_dir = sys.argv[4] if len(sys.argv) > 4 else None
shape = {}
if shape_dir:
    for path in glob.glob(os.path.join(shape_dir, "shape_*.jsonl")):
        for line in open(path):
            try:
                r = json.loads(line)
                shape[r["n"]] = r
            except ValueError:
                pass
LOW_GUARD = {"classic", "cafe", "bobber", "chopper", "cruiser", "scrambler", "tourer"}
RULES = ["Chain runs through the swingarm", "Rear guard touches or sits far off the tyre", "Seat not carried by a subframe", "Tail cuts through the seat or hangs",
         "Empty space over the engine", "Fairing does not meet the bike", "Proportions outside the type", "Parts from another type"]


def broken(r, ref):
    """The rules a build breaks: [rule index, detail]."""
    m, out = r["m"], []
    g = lambda k, d=0.0: m.get(k, d)
    if g("chain_in_swingarm") > 0.03 or g("chain_in_rear_tyre") > 0.02 or g("chain_in_frame") > 0.03:
        out.append([0, "%d%% of the chain is inside the swingarm" % round(100 * g("chain_in_swingarm"))])
    if "rear_guard_clear_min" in m:
        if m["rear_guard_clear_min"] < 0.015 or g("tail_in_rear_tyre") > 0.01:
            out.append([1, "guard %d mm off the tread at its closest" % round(1000 * m["rear_guard_clear_min"])])
        elif r["style"] in LOW_GUARD and m["rear_guard_clear_mid"] > 0.14:
            out.append([1, "guard rides %d mm above the tyre" % round(1000 * m["rear_guard_clear_mid"])])
    if "seat_carried" in m and (m["seat_carried"] < 0.85 or g("seat_gap") > 0.03 or g("seat_hang") > 0.013):
        out.append([2, "%d%% of the seat has structure under it; mean gap %d mm" % (round(100 * m["seat_carried"]), round(1000 * g("seat_gap")))])
    if g("tail_in_saddle") > 0.08 or g("saddle_in_tail") > 0.08 or g("tail_hang") > 0.013 or g("saddle_in_rear_tyre") > 0.01:
        out.append([3, "%d%% of the tail is inside the seat" % round(100 * max(g("tail_in_saddle"), g("saddle_in_tail"))) if g("tail_hang") <= 0.013 else "tail hangs %d mm clear" % round(1000 * g("tail_hang"))])
    if "engine_headroom" in m and (m["engine_headroom"] > 0.10 or m["engine_headroom"] > ref["m"].get("engine_headroom", 0.08) + 0.04 or g("engine_bay_fill", 1) < 0.72):
        out.append([4, "%d mm of air over the engine on average; it fills %d%% of its bay" % (round(1000 * m["engine_headroom"]), round(100 * g("engine_bay_fill", 1)))])
    if "fairing_hang" in m:
        bad = [k for k in ("fairing_in_front_tyre", "fairing_in_fork", "fairing_in_tank", "tank_in_fairing", "engine_in_fairing", "fairing_in_engine", "fairing_in_frame", "handlebars_in_fairing") if g(k) > 0.02]
        if m["fairing_hang"] > 0.013 or g("fairing_to_tank") > 0.04 or bad:
            out.append([5, ("hangs %d mm clear" % round(1000 * m["fairing_hang"])) if m["fairing_hang"] > 0.013 else ("cuts through: " + ", ".join(b.replace("fairing_in_", "").replace("_in_fairing", "").replace("_", " ") for b in bad)) if bad else "%d mm from the tank" % round(1000 * g("fairing_to_tank"))])
    off = []
    for k, name, tol in (("tank_len", "tank length", 0.18), ("seat_len", "seat length", 0.22), ("bars_top", "bar height", 0.15), ("tail_rear", "tail overhang", 0.25), ("engine_len", "engine length", 0.30), ("tank_top", "tank height", 0.12)):
        if k in m and k in ref["m"] and abs(ref["m"][k]) > 1e-6 and abs(m[k] - ref["m"][k]) > tol * abs(ref["m"][k]):
            off.append("%s %+d%%" % (name, round(100 * (m[k] - ref["m"][k]) / abs(ref["m"][k]))))
    if off:
        out.append([6, ", ".join(off) + " against the designed " + r["style"]])
    if r.get("misfit"):
        out.append([7, ", ".join(x.replace("_", " ") for x in r["misfit"])])
    return out


style_ref = {shape[n]["style"]: shape[n] for n in range(14) if n in shape}
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
                         "after": [[f["a"], f["b"], round(f["amount"] * (1000 if not f["b"] else 1))] for f in r["after"]], "did": r["did"],
                         "style": shape[n]["style"] if n in shape else "", "rules": broken(shape[n], style_ref.get(shape[n]["style"], shape[n])) if n in shape else None})

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
json.dump({"rules": RULES, "cols": COLS, "per": 240, "sweeps": sweeps, "total": len(builds), "done": len(results)}, open(os.path.join(out, "data.json"), "w"), separators=(",", ":"))
tot = {}
for sw in sweeps:
    for b in sw["builds"]:
        tot[b["status"]] = tot.get(b["status"], 0) + 1
print(len(results), "of", len(builds), tot)

if shape:
    print("rule: designed bikes breaking it / all builds breaking it")
    for i, name in enumerate(RULES):
        refs = [shape[n]["style"] for n in range(14) if n in shape and any(b[0] == i for b in broken(shape[n], style_ref[shape[n]["style"]]))]
        alln = sum(1 for n, r in shape.items() if any(b[0] == i for b in broken(r, style_ref.get(r["style"], r))))
        print("  %-46s %2d (%s)  %d of %d" % (name, len(refs), ", ".join(refs), alln, len(shape)))
    clean = sum(1 for n, r in shape.items() if not broken(r, style_ref.get(r["style"], r)))
    print("  builds breaking none:", clean)
