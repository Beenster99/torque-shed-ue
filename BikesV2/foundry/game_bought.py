"""The bought bikes in the game's catalogue (beenster, 25 Nov): Data/BikesV2/parts_v2.json.

A bought bike is a whole bike from a 3D model beenster bought, ridden as is: no construction, no mixing. It is a V2
frame part "v2_<model>" like our own bikes (so the game draws, steers, spins and suspends it from its glb), with:
  - a family of its own ("multistrada"), so no other family's parts bolt to it and its own fit nothing else;
  - its own parts in every slot it draws ("fits": "model"), carrying its own numbers where the real bike's are known
    (engine, gearbox, gearing), the rest copied from its style's stock part as every V2 bike's are;
  - "bought": true on the frame's "v2", which the tests read (its drawing isn't in git: Tools/bought_bikes.py).
The drawing (Data/BikesV2/<model>.glb) is never committed: BikesV2/foundry/bought_multistrada.py makes it from the
bought .blend on beenster's PC, and Tools/bought_bikes.py puts it in the build. Without it the game draws the bike
from its catalogue parts (BikeParts::SetV2Source falls back), which is the co-op stand-in.

    python3 BikesV2/foundry/game_bought.py      # after game_catalogue.py (which calls this itself)
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.dirname(ROOT)
DATA = os.path.join(GAME, "Data", "BikesV2")

RED = [0.55, 0.012, 0.01]
BIKES = {
    "ducati_multistrada": {
        "style": "adventure", "family": "multistrada", "brand": "Ducati", "name": "Ducati Multistrada V4 S",
        "description": "The Ducati Multistrada V4 S, bought whole: 1,158 cc V4 Granturismo, 170 hp, semi-active suspension. Ridden as it comes.",
        "frame": {"price": 4200, "mass": 11.0, "color": RED, "cda": 0.52},   # (a slipperier screen and fairing than our adventure bikes: about 155 mph)
        # its own numbers over the stock part's (slot -> fields); slots not here keep the stock part's numbers
        "own": {
            # V4 Granturismo: 1158 cc, 83 x 53.5 mm, 170 hp at 10,500, 125 N m at 8,750 (made-up close to the real figures);
            # the twin-pulse firing order (0, 90, 290, 380 degrees) is its sound (TorqueAudio/EngineSynth "twinpulse")
            "engine": {"layout": "vtwin", "cylinders": 4, "displacement": 1158.0, "bore": 83.0, "stroke": 53.5, "torque_per_cc": 0.108,
                       "peak_torque_rpm": 8750.0, "redline": 11500.0, "idle_rpm": 1300.0, "mass": 66.0, "crank": "twinpulse",
                       "v_angle": 90.0, "type": "V4 Granturismo", "price": 6800},
            # (its 15/43 gearing folded into the primary: the stock 16/40 sprockets stay, as the shop's bikes all come with them)
            "gearbox": {"primary": 2.064, "ratios": [2.6, 1.94, 1.57, 1.33, 1.15, 1.0], "shift_time": 0.05, "shift_feel": "slick",
                        "quickshifter": True, "dry_clutch": False, "type": "Six-speed with quickshifter", "price": 900},
        },
        # slots it is built with: what it draws, plus the gearbox and gearing it rides with
        "slots": ["tank", "saddle", "tail", "side_panels", "front_mudguard", "fairing", "headlight", "indicators", "mirrors",
                  "handlebars", "fork", "front_wheel", "rear_wheel", "shock", "swingarm", "rearsets", "front_caliper",
                  "rear_caliper", "engine", "gearbox", "cooling", "exhaust"],
    },
}
NOUN = {"tank": "fuel tank", "saddle": "seat", "tail": "tail unit", "side_panels": "side panels", "front_mudguard": "front mudguard",
        "fairing": "fairing", "headlight": "headlight", "indicators": "indicators", "mirrors": "mirrors", "handlebars": "handlebars",
        "fork": "front fork", "front_wheel": "front wheel", "rear_wheel": "rear wheel", "shock": "rear shock", "swingarm": "swingarm",
        "rearsets": "footrests and controls", "front_caliper": "front calipers", "rear_caliper": "rear caliper", "engine": "engine",
        "gearbox": "gearbox", "final_drive": "sprockets", "cooling": "radiator", "exhaust": "exhaust"}
KIND = {"front_wheel": "wheel", "rear_wheel": "wheel", "front_caliper": "caliper", "rear_caliper": "caliper"}
BODY = ("tank", "tail", "fairing", "side_panels", "front_mudguard")
DROP = ("name", "description", "styles", "pack", "price", "look", "v2", "common", "v2part")


def main():
    parts = json.load(open(os.path.join(GAME, "Data", "Parts", "parts.json")))
    layout = json.load(open(os.path.join(GAME, "Data", "Fitment", "layout.json")))
    path = os.path.join(DATA, "parts_v2.json")
    v2 = json.load(open(path))
    # (again from clean: a bought bike's entries go before they're made anew)
    for kind, entries in v2.items():
        if isinstance(entries, dict) and kind != "_retired":
            for pid in [p for p in entries if any(p == "v2_" + m or p.startswith("v2_" + m + "__") for m in BIKES)]:
                del entries[pid]
    made = 0
    for model, b in BIKES.items():
        info = json.load(open(os.path.join(DATA, model + ".json")))
        style, fid = b["style"], "v2_" + model
        ref = layout["reference_builds"][style]
        stock = dict(parts["frame"][ref["frame"]])
        pt = info["points_mm"]
        fa, ra, pv = pt["front_axle"], pt["rear_axle"], pt["swing_pivot"]
        dx, dz = fa[0] - ra[0], fa[2] - ra[2]
        ln = (dx * dx + dz * dz) ** 0.5
        px, pz = pv[0] - ra[0], pv[2] - ra[2]
        import math
        stock["geo"] = dict(stock.get("geo", {}), pivot_y=round((-dz * px + dx * pz) / ln / 1000.0, 4), arm=round((dx * px + dz * pz) / ln / 1000.0, 4))
        stock.update(b["frame"])
        stock.update(name=b["name"], description=b["description"], wheelbase=round(ln / 1000.0, 4),
                     rake_deg=round(pt["stats"]["rake_deg"] - math.degrees(math.atan2(dz, dx)), 3), seat_height=pt["stats"]["seat_height"] / 1000.0,
                     styles=[style], type=b["name"])
        build = {}
        for slot in b["slots"]:
            kind = KIND.get(slot, slot)
            base = dict(parts.get(kind, {}).get(ref.get(slot) or "") or {})
            if not base:
                base = dict(next((v for v in parts.get(kind, {}).values() if isinstance(v, dict) and v.get("mass")), {}))
            for k in DROP:
                base.pop(k, None)
            pid = f"{fid}__{slot}"
            p = dict(base, name=f"{b['name']} {NOUN[slot]}", description=f"Off the {b['name']}, and made for it: fits the {b['name']} only.",
                     styles=[style], pack=style, price=base.get("price", 200),
                     v2part={"model": model, "frame": fid, "slot": slot, "family": b["family"], "brand": b["brand"], "fits": "model",
                             "layout": "v4", "twin_shock": False, "bought": True})
            if slot in BODY:
                p["color"] = RED
            p.update(b["own"].get(slot, {}))
            v2.setdefault(kind, {})[pid] = p
            build[slot] = pid
            made += 1
        stock["v2"] = {"model": model, "family": b["family"], "brand": b["brand"], "lead": False, "body": RED, "points_mm": pt,
                       "build": build, "bought": True}
        v2["frame"][fid] = stock
        made += 1
    json.dump(v2, open(path, "w"), indent=1)
    print(f"{len(BIKES)} bought bike(s), {made} catalogue entries, in {path}")


if __name__ == "__main__":
    main()
