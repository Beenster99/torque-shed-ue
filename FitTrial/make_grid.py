#!/usr/bin/env python3
"""The trial's builds: a systematic grid, so the solver is seen in every kind of situation.

Each sweep takes the 14 designed bikes (one per style) as its rows and changes one thing along its
columns, with every kind of that thing the catalogue has; then whole top halves are moved between
styles, and last come seeded random bikes with every slot drawn freely. Reads the game's data,
writes builds.json. Changes nothing.
"""
import json, random, sys, os

root = sys.argv[1]
out = sys.argv[2]
parts = json.load(open(os.path.join(root, "Data/Parts/parts.json")))
layout = json.load(open(os.path.join(root, "Data/Fitment/layout.json")))
styles = layout["styles"]
refs = layout["reference_builds"]
slot_kind = layout["slot_kind"]
slots = layout["slots"]
builds = []


def add(sweep, row, col, build, note=""):
    builds.append({"id": "%s__%s__%s" % (sweep, row, col), "sweep": sweep, "row": row, "col": col, "note": note, "parts": dict(build)})


def one_per(kind, key):
    """One part of each sort (by key), the first the catalogue lists."""
    seen, picked = set(), []
    for pid, p in parts[kind].items():
        k = key(pid, p)
        if k is None or k in seen:
            continue
        seen.add(k)
        picked.append(pid)
    return picked


def look(field):
    return lambda pid, p: (p.get("look") or {}).get(field, p.get("style"))


# 0. The designed bikes as they are: the solver should leave these alone.
for s in styles:
    add("0_designed", s, "as designed", refs[s])

# 1. Engines: the smallest and the biggest of each layout.
engines = []
by_layout = {}
for pid, p in parts["engine"].items():
    by_layout.setdefault(p["layout"], []).append((p.get("displacement", 0), pid))
for lay, lst in by_layout.items():
    lst.sort()
    engines.append(lst[0][1])
    if lst[-1][1] != lst[0][1]:
        engines.append(lst[-1][1])
# 2. Tanks: one of every shape.
tanks = one_per("tank", look("shape"))
# (the redrawn tanks come in families of five: the first and third of each)
tanks = [t for t in tanks if not (t.rsplit("_", 1)[-1] in ("2", "4", "5") and t.rsplit("_", 1)[0] in ("bobber", "adventure", "sports_tourer", "chopper", "cruiser"))]
# 3. Frames: one of every type.
frames = one_per("frame", lambda pid, p: p.get("type"))
# 4. Forks, bars, seats, tails, exhausts, fairings, swingarms.
forks = one_per("fork", lambda pid, p: p.get("type")) + ["chopper_extended_tube", "long_travel_mx"]
bars = one_per("handlebars", lambda pid, p: p.get("style")) + ["chopper_tall_apes", "cruiser_beach_bar", "clubman_bar", "bobber_z_bar"]
seats = one_per("saddle", lambda pid, p: (p.get("look") or {}).get("shape", p.get("style")))
tails = one_per("tail", lambda pid, p: (p.get("look") or {}).get("cowl") or (p.get("look") or {}).get("lamp") or p.get("style"))
exhausts = one_per("exhaust", lambda pid, p: (p.get("look") or {}).get("can", p.get("style")))
fairings = list(parts["fairing"].keys())
swingarms = one_per("swingarm", lambda pid, p: p.get("type").split(",")[0].replace(" black", ""))

sweeps = [("1_engine", "engine", engines), ("2_tank", "tank", tanks), ("3_frame", "frame", frames), ("4_fork", "fork", forks),
          ("5_bars", "handlebars", bars), ("6_seat", "saddle", seats), ("7_tail", "tail", tails), ("8_exhaust", "exhaust", exhausts),
          ("9_fairing", "fairing", fairings), ("10_swingarm", "swingarm", swingarms)]
for sweep, slot, ids in sweeps:
    seen = []
    for i in ids:
        if i not in seen:
            seen.append(i)
    for s in styles:
        for pid in seen:
            if refs[s].get(slot) == pid:
                continue
            b = dict(refs[s])
            b[slot] = pid
            add(sweep, s, pid, b)

# 11. Two things at once, the awkward pairs: the biggest and smallest engines under every tank shape on three frames.
for s in ["chopper", "cafe", "sport", "adventure"]:
    for eng in ["inline_1200", "boxer_1200", "vtwin_1800", "single_250"]:
        for t in tanks:
            b = dict(refs[s])
            b["engine"] = eng
            b["tank"] = t
            add("11_engine_x_tank_" + s, eng, t, b)

# 12. Top halves moved between styles: the tank, seat, tail, bars, fairing, headlight and mudguards of one
# on the frame, forks, wheels and engine of another.
top = ["tank", "saddle", "tail", "handlebars", "fairing", "headlight", "front_mudguard", "side_panels", "mirrors", "dashboard", "indicators", "exhaust"]
for chassis in styles:
    for body in styles:
        if chassis == body:
            continue
        b = dict(refs[chassis])
        for sl in top:
            b[sl] = refs[body].get(sl, "none")
        add("12_top_half", chassis + " chassis", body + " top", b)

# 13. Anything goes: every slot drawn freely from the catalogue (seeded, so the same every run).
rng = random.Random(1911)
optional = set(layout.get("optional_slots", []))
for n in range(160):
    b = {}
    for sl in slots:
        kind = slot_kind.get(sl, sl)
        ids = list(parts.get(kind, {}).keys())
        if not ids:
            b[sl] = "none"
            continue
        if sl in optional and rng.random() < 0.6:
            b[sl] = "none"
        else:
            b[sl] = rng.choice(ids)
    add("13_random", "row %d" % (n // 10 + 1), "random %03d" % (n + 1), b)

json.dump({"builds": builds}, open(out, "w"))
count = {}
for b in builds:
    count[b["sweep"]] = count.get(b["sweep"], 0) + 1
print(len(builds), "builds")
for k, v in count.items():
    print(" ", k, v)
print("engines", engines); print("tanks", tanks); print("frames", frames); print("forks", forks); print("bars", bars)
print("seats", seats); print("tails", tails); print("exhausts", exhausts); print("swingarms", swingarms)
