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
SCOUT_RED = [0.3, 0.012, 0.012]
GRAPHITE = [0.075, 0.078, 0.084]
# The slots a bought bike is built with: those it draws (its own model's json), in this order, plus the gearbox it
# rides with (the frame is the bike itself; brake discs, chain, sprockets, master cylinders and dash ride on the stock
# parts' numbers, as the Multistrada first went in).
ORDER = ["tank", "saddle", "tail", "side_panels", "front_mudguard", "fairing", "headlight", "indicators", "mirrors",
         "handlebars", "fork", "front_wheel", "rear_wheel", "shock", "swingarm", "rearsets", "front_caliper",
         "rear_caliper", "engine", "gearbox", "cooling", "exhaust", "side_stand"]
BIKES = {
    "ducati_multistrada": {
        "style": "adventure", "family": "multistrada", "brand": "Ducati", "name": "Ducati Multistrada V4 S",
        "description": "The Ducati Multistrada V4 S, bought whole: 1,158 cc V4 Granturismo, 170 hp, semi-active suspension. Ridden as it comes.",
        "frame": {"price": 4200, "mass": 11.0, "color": RED, "cda": 0.52},   # (a slipperier screen and fairing than our adventure bikes: about 155 mph)
        "body": RED, "layout": "v4", "twin_shock": False,
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
    },
    # beenster 25 Nov: "I have 3 more bikes for the same treatment" (BikesV2/foundry/bought_export.py makes their drawings)
    "ducati_panigale": {
        "style": "sport", "family": "panigale", "brand": "Ducati", "name": "Ducati Panigale V4 S",
        "description": "The Ducati Panigale V4 S, bought whole: 1,103 cc Desmosedici Stradale V4, 215 hp, single-sided swingarm. Ridden as it comes.",
        "frame": {"price": 5200, "mass": 9.0, "color": RED, "cda": 0.3},
        "body": RED, "layout": "v4", "twin_shock": False,
        "own": {
            # Desmosedici Stradale: 1103 cc, 81 x 53.5 mm, 215 hp at 13,500, 121 N m at 11,250 (made-up close to the real figures)
            "engine": {"layout": "vtwin", "cylinders": 4, "displacement": 1103.0, "bore": 81.0, "stroke": 53.5, "torque_per_cc": 0.11,
                       "peak_torque_rpm": 11250.0, "redline": 14500.0, "idle_rpm": 1300.0, "mass": 64.0, "crank": "twinpulse",
                       "v_angle": 90.0, "type": "Desmosedici Stradale V4", "price": 9500},
            # (its 15/41 gearing folded into the primary over the sport stock 15/45, with a taller top end for its 186 mph)
            "gearbox": {"primary": 1.62, "ratios": [2.714, 2.118, 1.737, 1.524, 1.364, 1.25], "shift_time": 0.04, "shift_feel": "slick",
                        "quickshifter": True, "dry_clutch": False, "type": "Six-speed with quickshifter", "price": 1200},
        },
    },
    "indian_scout": {
        "style": "cruiser", "family": "scout", "brand": "Indian", "name": "Indian Scout 101",
        "description": "The Indian Scout 101, bought whole: 1,250 cc SpeedPlus 60-degree V-twin, 111 hp, belt drive. Ridden as it comes.",
        "frame": {"price": 3600, "mass": 16.0, "color": SCOUT_RED},
        "body": SCOUT_RED, "layout": "vtwin", "twin_shock": True,
        "own": {
            # SpeedPlus: 1250 cc, 104 x 73.6 mm, 111 hp at 7,250, 109 N m at 6,300 (made-up close to the real figures)
            "engine": {"layout": "vtwin", "cylinders": 2, "displacement": 1250.0, "bore": 104.0, "stroke": 73.6, "torque_per_cc": 0.087,
                       "peak_torque_rpm": 6300.0, "redline": 8300.0, "idle_rpm": 1000.0, "mass": 78.0, "crank": "60v",
                       "v_angle": 60.0, "type": "SpeedPlus 60-degree V-twin", "price": 5200},
            "gearbox": {"primary": 1.75, "ratios": [2.77, 1.88, 1.43, 1.18, 1.0, 0.88], "shift_time": 0.1, "shift_feel": "clunky",
                        "quickshifter": False, "dry_clutch": False, "type": "Six-speed", "price": 700},
        },
    },
    "triumph_trident": {
        "style": "naked", "family": "trident", "brand": "Triumph", "name": "Triumph Trident 800",
        "description": "The Triumph Trident 800, bought whole: 798 cc triple, 115 hp, Showa forks. Ridden as it comes.",
        "frame": {"price": 3200, "mass": 11.0, "color": GRAPHITE, "cda": 0.46},   # (about 145 mph, as the real one)
        "body": GRAPHITE, "layout": "triple", "twin_shock": False,
        "own": {
            # 798 cc triple, 78 x 55.7 mm, 115 hp at 10,750, 84 N m at 8,500 (made-up close to the real figures); T-plane 240 degree crank
            "engine": {"layout": "triple", "cylinders": 3, "displacement": 798.0, "bore": 78.0, "stroke": 55.7, "torque_per_cc": 0.105,
                       "peak_torque_rpm": 8500.0, "redline": 11500.0, "idle_rpm": 1200.0, "mass": 55.0, "crank": "240",
                       "type": "800 triple", "price": 4600},
            "gearbox": {"primary": 1.85, "ratios": [2.6, 1.9, 1.55, 1.32, 1.16, 1.04], "shift_time": 0.05, "shift_feel": "slick",
                        "quickshifter": True, "dry_clutch": False, "type": "Six-speed with Shift Assist", "price": 900},
        },
    },
}
NOUN = {"tank": "fuel tank", "saddle": "seat", "tail": "tail unit", "side_panels": "side panels", "front_mudguard": "front mudguard",
        "fairing": "fairing", "headlight": "headlight", "indicators": "indicators", "mirrors": "mirrors", "handlebars": "handlebars",
        "fork": "front fork", "front_wheel": "front wheel", "rear_wheel": "rear wheel", "shock": "rear shock", "swingarm": "swingarm",
        "rearsets": "footrests and controls", "front_caliper": "front calipers", "rear_caliper": "rear caliper", "engine": "engine",
        "gearbox": "gearbox", "final_drive": "sprockets", "side_stand": "side stand", "cooling": "radiator", "exhaust": "exhaust"}
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
        drawn = {v["slot"] for v in info["surfaces"].values()}
        for slot in [x for x in ORDER if x in drawn or x in ("engine", "gearbox")]:
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
                             "layout": b["layout"], "twin_shock": b["twin_shock"], "bought": True})
            if slot in BODY:
                p["color"] = b["body"]
            p.update(b["own"].get(slot, {}))
            v2.setdefault(kind, {})[pid] = p
            build[slot] = pid
            made += 1
        stock["v2"] = {"model": model, "family": b["family"], "brand": b["brand"], "lead": False, "body": b["body"], "points_mm": pt,
                       "build": build, "bought": True}
        v2["frame"][fid] = stock
        made += 1
    json.dump(v2, open(path, "w"), indent=1)
    print(f"{len(BIKES)} bought bike(s), {made} catalogue entries, in {path}")


if __name__ == "__main__":
    main()
