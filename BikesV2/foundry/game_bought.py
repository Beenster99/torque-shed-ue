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
                       "v_angle": 90.0, "type": "V4 Granturismo", "price": 6800,
                       # [Bike V3 Sounds] beenster's own recording of their Multistrada (Data/Audio/Bikes/multistrada)
                       "sound": "multistrada"},
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
                       "v_angle": 90.0, "type": "Desmosedici Stradale V4", "price": 9500,
                       # [Bike V3 Sounds] beenster's own recording of the Panigale on tour: its tickover (Data/Audio/Bikes/panigale)
                       "sound": "panigale"},
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
                       "v_angle": 60.0, "type": "SpeedPlus 60-degree V-twin", "price": 5200,
                       # [Bike V3 Sounds] beenster's own recording of the Scout on tour (Data/Audio/Bikes/scout)
                       "sound": "scout"},
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
                       "type": "800 triple", "price": 4600,
                       # [Bike V3 Sounds] the made triple's tone matched by ear to a real Trident 800 (measured, no audio used)
                       "tone_high": 3.0, "tone_mid": -3.0},
            "gearbox": {"primary": 1.85, "ratios": [2.6, 1.9, 1.55, 1.32, 1.16, 1.04], "shift_time": 0.05, "shift_feel": "slick",
                        "quickshifter": True, "dry_clutch": False, "type": "Six-speed with Shift Assist", "price": 900},
        },
    },
    # beenster 25 Nov, the second batch: "Lots of Kawasaki bikes and others. I want exactly the same treatment"
    # (bought_export.py with bought_auto.py makes their drawings). Engines: made-up close to the real figures.
}


def _eng(layout, cyl, cc, bore, stroke, tpc, peak, red, idle, mass, crank, kind, price, v_angle=None):
    e = {"layout": layout, "cylinders": cyl, "displacement": cc, "bore": bore, "stroke": stroke, "torque_per_cc": tpc,
         "peak_torque_rpm": peak, "redline": red, "idle_rpm": idle, "mass": mass, "crank": crank, "type": kind, "price": price}
    if v_angle:
        e["v_angle"] = v_angle
    return e


def _box(primary, ratios, qs, kind, price, shift=0.06):
    return {"primary": primary, "ratios": ratios, "shift_time": 0.045 if qs else shift, "shift_feel": "slick" if qs else "smooth",
            "quickshifter": qs, "dry_clutch": False, "type": kind, "price": price}


def _bike(style, family, brand, name, desc, price, mass, body, layout, eng, box, cda=None, twin=False, frame_color=None):
    f = {"price": price, "mass": mass, "color": frame_color or body}
    if cda:
        f["cda"] = cda
    return {"style": style, "family": family, "brand": brand, "name": name, "description": desc + " Ridden as it comes.",
            "frame": f, "body": body, "layout": layout, "twin_shock": twin, "own": {"engine": eng, "gearbox": box}}


K_TWIN6 = [2.92, 2.06, 1.6, 1.36, 1.18, 1.04]
BIKES.update({
    "ducati_monster": _bike("naked", "monster", "Ducati", "Ducati Monster 890",
        "The Ducati Monster, bought whole: 890 cc V2, 111 hp, trellis-free frame and a 179 kg dry weight.", 3400, 9.5, RED, "vtwin90",
        _eng("vtwin", 2, 890.0, 96.0, 61.5, 0.1024, 7250.0, 10750.0, 1300.0, 52.0, "90v", "890 V2", 4400, 90.0),
        _box(1.78, [2.69, 1.95, 1.58, 1.37, 1.22, 1.11], True, "Six-speed with quickshifter", 900), cda=0.46),
    "honda_cb1000gt": _bike("adventure", "cb1000gt", "Honda", "Honda CB1000 GT",
        "The Honda CB1000 GT, bought whole: 1,084 cc inline four, 150 hp, a sports tourer with a tall screen.", 4300, 12.0, [0.025, 0.026, 0.03], "inline4",
        # ([Bike V3 Sounds] beenster's own recording of the CB1000 GT on tour: Data/Audio/Bikes/cb1000gt)
        dict(_eng("inline", 4, 1084.0, 81.0, 52.6, 0.094, 8750.0, 12000.0, 1200.0, 62.0, "flat", "1000 inline four", 5600), sound="cb1000gt"),
        _box(1.98, [2.6, 1.95, 1.58, 1.35, 1.19, 1.07], True, "Six-speed with quickshifter", 900), cda=0.5),
    "kawasaki_eliminator": _bike("cruiser", "eliminator", "Kawasaki", "Kawasaki Eliminator 500",
        "The Kawasaki Eliminator 500, bought whole: 451 cc parallel twin, 45 hp, a low-slung cruiser.", 1900, 13.0, [0.02, 0.02, 0.022], "parallel_twin",
        _eng("parallel", 2, 451.0, 70.0, 58.6, 0.0945, 6000.0, 10500.0, 1200.0, 36.0, "180", "451 parallel twin", 1900),
        _box(2.67, K_TWIN6, False, "Six-speed", 500), twin=True),
    "kawasaki_kle500": _bike("adventure", "kle", "Kawasaki", "Kawasaki KLE 500",
        "The Kawasaki KLE 500, bought whole: 451 cc parallel twin, 45 hp, 21-inch front wheel.", 2100, 11.0, [0.8, 0.8, 0.8], "parallel_twin",
        _eng("parallel", 2, 451.0, 70.0, 58.6, 0.095, 6000.0, 10000.0, 1200.0, 36.0, "180", "451 parallel twin", 1900),
        _box(2.59, K_TWIN6, False, "Six-speed", 500)),
    "kawasaki_klr650": _bike("adventure", "klr", "Kawasaki", "Kawasaki KLR 650",
        "The Kawasaki KLR 650, bought whole: 652 cc single, 40 hp, the go-anywhere workhorse.", 2000, 13.0, [0.424, 0.428, 0.027], "single",
        _eng("single", 1, 652.0, 100.0, 83.0, 0.08, 4500.0, 7500.0, 1200.0, 44.0, "single", "652 single", 1800),
        _box(2.43, [2.33, 1.63, 1.27, 1.04, 0.88], False, "Five-speed", 450, shift=0.09), cda=0.55),
    "kawasaki_ninja7": _bike("sport", "ninja7", "Kawasaki", "Kawasaki Ninja 7 Hybrid",
        "The Kawasaki Ninja 7 Hybrid, bought whole: 451 cc twin with an electric boost, 69 hp all in.", 2900, 10.0, [0.62, 0.63, 0.64], "parallel_twin",
        _eng("parallel", 2, 451.0, 70.0, 58.6, 0.13, 5000.0, 10500.0, 1200.0, 48.0, "180", "451 twin hybrid", 3600),
        _box(1.89, K_TWIN6, True, "Six-speed with electric shift", 900), cda=0.36),
    "kawasaki_z650s": _bike("naked", "z650", "Kawasaki", "Kawasaki Z650 S",
        "The Kawasaki Z650 S, bought whole: 649 cc parallel twin, 68 hp, light and easy.", 2300, 9.0, [0.014, 0.315, 0.039], "parallel_twin",
        _eng("parallel", 2, 649.0, 83.0, 60.0, 0.0986, 6700.0, 10500.0, 1200.0, 42.0, "180", "649 parallel twin", 2400),
        _box(2.34, [2.44, 1.78, 1.44, 1.24, 1.09, 0.96], False, "Six-speed", 500), cda=0.48),
    "kawasaki_z900": _bike("naked", "z900", "Kawasaki", "Kawasaki Z900",
        "The Kawasaki Z900, bought whole: 948 cc inline four, 123 hp, red trellis frame.", 3000, 10.5, [0.16, 0.165, 0.17], "inline4",
        _eng("inline", 4, 948.0, 73.4, 56.0, 0.104, 7700.0, 11500.0, 1200.0, 58.0, "flat", "948 inline four", 4200),
        _box(1.93, [2.69, 1.95, 1.6, 1.39, 1.22, 1.08], False, "Six-speed", 600), cda=0.46),
    "kawasaki_z1100": _bike("naked", "z1100", "Kawasaki", "Kawasaki Z1100",
        "The Kawasaki Z1100, bought whole: 1,099 cc inline four, 134 hp, the big supernaked.", 3500, 11.0, [0.02, 0.021, 0.024], "inline4",
        _eng("inline", 4, 1099.0, 77.0, 59.0, 0.103, 7600.0, 10500.0, 1150.0, 64.0, "flat", "1099 inline four", 4800),
        _box(1.79, [2.6, 1.95, 1.6, 1.37, 1.2, 1.06], True, "Six-speed with quickshifter", 800), cda=0.46),
    "kawasaki_zh2": _bike("naked", "zh2", "Kawasaki", "Kawasaki Z H2",
        "The Kawasaki Z H2, bought whole: 998 cc supercharged inline four, 200 hp.", 5000, 11.0, [0.03, 0.031, 0.034], "inline4",
        _eng("inline", 4, 998.0, 76.0, 55.0, 0.137, 8500.0, 12000.0, 1200.0, 70.0, "flat", "998 supercharged inline four", 8500),
        _box(1.85, [2.6, 1.95, 1.6, 1.37, 1.2, 1.06], True, "Six-speed with quickshifter", 1000), cda=0.46),
    "kawasaki_zx4rr": _bike("sport", "zx4rr", "Kawasaki", "Kawasaki ZX-4RR",
        "The Kawasaki ZX-4RR, bought whole: 399 cc inline four screaming to 16,000 rpm, 77 hp.", 3000, 8.5, [0.16, 0.6, 0.02], "inline4",
        _eng("inline", 4, 399.0, 57.0, 39.1, 0.098, 13000.0, 16000.0, 1500.0, 40.0, "flat", "399 inline four", 4000),
        _box(2.8, [2.92, 2.06, 1.62, 1.36, 1.18, 1.07], True, "Six-speed with quickshifter", 800), cda=0.32),
    "kawasaki_zx6r": _bike("sport", "zx6r", "Kawasaki", "Kawasaki ZX-6R",
        "The Kawasaki ZX-6R, bought whole: 636 cc inline four, 124 hp, a supersport.", 3800, 8.5, [0.12, 0.55, 0.02], "inline4",
        # ([Bike V3 Sounds] beenster's own recording on tour: Data/Audio/Bikes/zx6r)
        dict(_eng("inline", 4, 636.0, 67.0, 45.1, 0.108, 11000.0, 16000.0, 1400.0, 50.0, "flat", "636 inline four", 5200), sound="zx6r"),
        _box(2.13, [2.92, 2.06, 1.62, 1.36, 1.18, 1.07], True, "Six-speed with quickshifter", 900), cda=0.31),
    "ktm_1390sa": _bike("adventure", "1390sa", "KTM", "KTM 1390 Super Adventure S",
        "The KTM 1390 Super Adventure S, bought whole: 1,350 cc 75-degree V-twin, 173 hp.", 4800, 12.0, [0.835, 0.192, 0.0], "vtwin",
        # ([Bike V3 Sounds] beenster's own recording on tour, its tickover: Data/Audio/Bikes/ktm1390)
        dict(_eng("vtwin", 2, 1350.0, 110.0, 71.0, 0.107, 7000.0, 10200.0, 1300.0, 66.0, "75v", "LC8 75-degree V-twin", 7200, 75.0), sound="ktm1390"),
        _box(1.9, [2.69, 1.95, 1.55, 1.3, 1.1, 0.95], True, "Six-speed with quickshifter", 1000), cda=0.5),
    "yamaha_tracer9": _bike("adventure", "tracer9", "Yamaha", "Yamaha Tracer 9 GT",
        "The Yamaha Tracer 9 GT, bought whole: 890 cc CP3 triple, 117 hp, cruise control and panniers.", 3700, 12.0, [0.045, 0.047, 0.05], "triple",
        # ([Bike V3 Sounds] beenster's own recording on tour, its tickover: Data/Audio/Bikes/tracer9)
        dict(_eng("triple", 3, 890.0, 78.0, 62.1, 0.1045, 7000.0, 11000.0, 1250.0, 58.0, "240", "CP3 890 triple", 4900), sound="tracer9"),
        _box(2.07, [2.67, 1.95, 1.6, 1.36, 1.17, 1.04], True, "Six-speed with quickshifter", 900), cda=0.52),
})
# (the adventure style's own fork is an enduro one: soft and long. The big road-going adventure and touring bikes have
# firmer road forks: beenster on the CB1000 GT in 174, "too much fork dive on braking". The KLE and KLR keep the enduro one.)
ROAD_FORK = {"spring": 30000.0, "bump": 2400.0, "rebound": 3400.0, "travel": 0.13, "style": "usd"}
TALL_FORK = {"spring": 25000.0, "bump": 2100.0, "rebound": 3100.0, "travel": 0.17, "style": "usd"}
for _m, _f in (("honda_cb1000gt", ROAD_FORK), ("yamaha_tracer9", ROAD_FORK), ("ktm_1390sa", TALL_FORK), ("ducati_multistrada", TALL_FORK)):
    if _m in BIKES:
        BIKES[_m]["own"].setdefault("fork", dict(_f))


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
        # (its rear shock and swingarm always come with it, drawn as their own pieces or not: beenster in 174, "fit the rear
        # suspension automatically when buying from the shop", and on the KLR 650 "footpegs and rearsets" too)
        for slot in [x for x in ORDER if x in drawn or x in ("engine", "gearbox", "shock", "swingarm", "rearsets")]:
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
