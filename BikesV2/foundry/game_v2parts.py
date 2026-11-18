"""Every V2 bike as shop parts (beenster, 18 Nov: "build a bike in a family from the ground up"): Data/BikesV2/parts_v2.json.

game_catalogue.py writes each V2 bike's frame part ("v2_<model>"); this lays the rest of the bike over the catalogue,
one shop part for each slot the model draws: "v2_<model>__<slot>" (its tank, saddle, tail, mudguard, lamps, fork,
wheels, shock, calipers, stand, bars, engine block, cylinder head, gearbox, fuel system, air filter, cooling, exhaust...).

Each part rides as the family's stock part of its slot (its numbers copied, the engine's too, so a V2 bike rides as the
155 whole bikes did) and carries "v2part": which model draws it, and what it fits:
  family  - any frame of its family (the hidden mounting standard, docs/mounting_standard.md)
  layout  - a frame of its family built round the same engine layout (joint rule 3: the engine block, engine guards)
  engine  - the engine block of its own model (cylinder head, gearbox, fuel system, air filter, cooling and exhaust:
            they bolt to that engine's ports and cases), and so a frame that block fits
  shock   - a frame of its family with the same rear end (twin shocks or a mono-shock)
  model   - its own model only (handlebars: the rider's grips are drawn on them)
The game draws a fitted part from its own model's glb, moved onto the bike's frame (Fit/BikeParts FitV2); an empty
slot draws nothing, so a new frame on the ramp is a bare frame.

Each frame's "v2" gains "build" (its own parts, slot -> id: the bike as it comes from the configurator) and, in
"points_mm", "deck" (nose and pitches, for seating a tank, seat or tail) and "engine" (crank, countershaft, cylinders:
where the engine block's cylinder head is cut from it). The old Godot parts of the kinds these replace are retired.

    python3 foundry/game_v2parts.py      # after game_catalogue.py (it calls this itself)
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.dirname(ROOT)

# slot -> (kind, fits, shop name)
SLOTS = {
    "tank": ("tank", "family", "fuel tank"),
    "saddle": ("saddle", "family", "saddle"),
    "tail": ("tail", "family", "tail unit"),
    "side_panels": ("side_panels", "family", "side panels"),
    "front_mudguard": ("front_mudguard", "family", "front mudguard"),
    "fairing": ("fairing", "family", "fairing"),
    "number_boards": ("number_boards", "family", "number boards"),
    "headlight": ("headlight", "family", "headlight"),
    "indicators": ("indicators", "family", "indicators"),
    "mirrors": ("mirrors", "family", "mirrors"),
    "handlebars": ("handlebars", "model", "handlebars"),
    "fork": ("fork", "family", "front fork"),
    "front_wheel": ("wheel", "family", "front wheel"),
    "rear_wheel": ("wheel", "family", "rear wheel"),
    "shock": ("shock", "shock", "rear shock"),
    "side_stand": ("side_stand", "family", "side stand"),
    "front_caliper": ("caliper", "family", "front caliper"),
    "rear_caliper": ("caliper", "family", "rear caliper"),
    "guards": ("guards", "layout", "engine guards"),
    "engine": ("engine", "layout", "engine block"),
    "head": ("head", "engine", "cylinder head"),
    "gearbox": ("gearbox", "engine", "gearbox and side cases"),
    "fuel": ("fuel", "engine", "fuel system"),
    "air_filter": ("air_filter", "engine", "airbox and filter"),
    "cooling": ("cooling", "engine", "cooling"),
    "exhaust": ("exhaust", "engine", "exhaust"),
}
# Slots a bike needs to ride that some models draw nothing in.
HIDDEN_NEEDED = ("shock",)
# The old Godot parts of these kinds are shapes the V2 bikes no longer draw: out of the shop (the common parts stay).
RETIRE_KINDS = ("tank", "saddle", "tail", "side_panels", "front_mudguard", "fairing", "number_boards", "headlight",
                "indicators", "mirrors", "handlebars", "guards", "wheel", "fork", "shock", "engine", "side_stand")
PRICE = {"tank": 260, "saddle": 130, "tail": 110, "side_panels": 60, "front_mudguard": 70, "fairing": 380, "number_boards": 60,
         "headlight": 95, "indicators": 40, "mirrors": 50, "handlebars": 85, "guards": 120, "wheel": 350, "fork": 540,
         "shock": 300, "engine": 2400, "head": 600, "gearbox": 500, "fuel": 300, "air_filter": 60, "cooling": 250,
         "exhaust": 420, "side_stand": 40, "caliper": 220}
DROP = ("name", "description", "styles", "pack", "price", "look", "v2", "common")


def main():
    parts = json.load(open(os.path.join(GAME, "Data", "Parts", "parts.json")))
    layout = json.load(open(os.path.join(GAME, "Data", "Fitment", "layout.json")))
    dst = os.path.join(GAME, "Data", "BikesV2", "parts_v2.json")
    v2 = json.load(open(dst))
    frames = v2["frame"]
    made = {}
    for fid, frame in sorted(frames.items()):
        info = frame["v2"]
        name, family = info["model"], info["family"]
        style = frame["styles"][0]
        model = json.load(open(os.path.join(ROOT, "data", "models", name + ".json")))
        drawn = json.load(open(os.path.join(GAME, "Data", "BikesV2", name + ".json")))
        slots = {s["slot"] for s in drawn["surfaces"].values()}
        eng = model["engine"]
        if any(s["slot"] == "engine" and not s.get("sub") for s in drawn["surfaces"].values()):
            slots.add("head")
            if any(s["slot"] == "engine" and not s.get("sub") and s.get("tag") == "d3_sidecase" for s in drawn["surfaces"].values()):
                slots.add("gearbox")
        pts = info["points_mm"]
        deck = model["deck"]
        pts["deck"] = {"nose": [deck["nose"][0], 0.0, deck["nose"][1]], "tank_pitch": deck["tank_pitch"], "seat_pitch": deck["seat_pitch"],
                       "seat_unit": bool(deck.get("seat_unit"))}
        split = {"crank": [eng["crank"][0], 0.0, eng["crank"][1]], "countershaft": [eng["countershaft"][0], 0.0, eng["countershaft"][1]],
                 "cylinders": [{k: c[k] for k in ("angle", "start", "length", "head", "bore_w")} for c in eng.get("cylinders", [])]}
        if eng.get("boxer"):
            b = eng["boxer"]
            split["boxer"] = {"at": [b["at"][0], 0.0, b["at"][1]], "reach": b["reach"]}
        pts["engine"] = split
        st = pts.get("stats", {})
        ref = layout["reference_builds"][style]
        brand = info["brand"]
        build = {}
        # A slot the bike needs to ride but draws nothing in (the choppers' hidden shock): still its own part.
        for slot in HIDDEN_NEEDED:
            if ref.get(slot) and ref[slot] != "none":
                slots.add(slot)
        for slot in SLOTS:
            if slot not in slots:
                continue
            kind, fits, noun = SLOTS[slot]
            # Every part's numbers are the family's stock part's, the engine's too: a V2 bike rides as the 155 whole
            # bikes did (the physics preset and top speeds are tuned on them); only its shape and name are its own.
            src = ref.get(slot) or ""
            base = dict(parts.get(kind, {}).get(src) or {})
            if not base:   # (the family's stock bike has none: the first of the kind with numbers)
                base = dict(next((v for v in parts.get(kind, {}).values() if isinstance(v, dict) and v.get("mass")), {}))
            price = base.get("price", PRICE.get(kind, 100))
            for k in DROP:
                base.pop(k, None)
            pid = f"{fid}__{slot}"
            desc = {"family": f"Off the {brand} {family}. Bolts to any {family} frame on the hidden mounting standard.",
                    "layout": f"Off the {brand} {family}: for a {family} frame built round a {model.get('engine_label', eng['layout'])}.",
                    "engine": f"Off the {brand} {family}: for the {brand} {family} engine.",
                    "shock": f"Off the {brand} {family}. Fits any {family} frame with {'twin shocks' if st.get('twin_shock') else 'a mono-shock'}.",
                    "model": f"Off the {brand} {family}, and made for it: fits the {brand} {family} frame only."}[fits]
            p = dict(base, name=f"{frame['name']} {noun}", description=desc, styles=[style], pack=style, price=price,
                     color=base.get("color", [0.5, 0.5, 0.5]) if kind not in ("tank", "tail", "fairing", "side_panels", "front_mudguard") else info.get("body", [0.5, 0.5, 0.5]),
                     v2part={"model": name, "frame": fid, "slot": slot, "family": family, "brand": brand, "fits": fits,
                             "layout": eng["layout"], "twin_shock": bool(st.get("twin_shock"))})
            if kind == "engine":
                p["type"] = model.get("engine_label", base.get("type", "")).capitalize()
            made.setdefault(kind, {})[pid] = p
            build[slot] = pid
        info["build"] = build
    for kind, entries in made.items():
        v2.setdefault(kind, {}).update(entries)
    retired = v2.setdefault("_retired", {})
    for kind in RETIRE_KINDS:
        for pid, v in (parts.get(kind) or {}).items():
            if isinstance(v, dict) and pid != "none" and pid not in retired.get(kind, []):
                retired.setdefault(kind, []).append(pid)
    json.dump(v2, open(dst, "w"), indent=1)
    print(sum(len(v) for v in made.values()), "V2 shop parts:", {k: len(v) for k, v in sorted(made.items())})
    print(sum(len(v) for v in retired.values()), "old parts retired")


if __name__ == "__main__":
    main()
