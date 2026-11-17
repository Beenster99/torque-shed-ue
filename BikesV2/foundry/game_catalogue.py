"""The V2 bikes as the game's parts catalogue sees them (switch-over, stage 1): Data/BikesV2/parts_v2.json.

Each V2 bike is a frame part "v2_<model>" laid over the game's catalogue (Data/Parts/parts.json stays the
Godot game's own). It starts as its style's stock frame, takes the model's own wheelbase, rake, seat height
and colour, and carries "v2": the model's name, its key points and sizes (written by build3d.py --game into
Data/BikesV2/<model>.json). The game draws a bike whose frame has "v2" from Data/BikesV2/<model>.glb.

    python3 foundry/game_catalogue.py          # after exporting the bikes with build3d.py <model> --definition --game
"""
import glob
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.dirname(ROOT)
STYLE = {"cafe": "cafe", "classic": "classic", "sport": "sport", "adventure": "adventure", "mx": "mx", "naked": "naked",
         "chopper": "chopper", "cruiser": "cruiser"}
# the family leads: the bike a style starts as in the shop and in the robot tests
LEADS = {"corvano_naked", "kazeda_sport", "hartwell_cafe", "hartwell_classic", "lodestar_cruiser", "corvano_adventure",
         "lodestar_chopper", "varga_mx"}
SHOWN = {"hartwell": "BSX"}      # the brand's shown name (beenster, 16 Nov); the id stays hartwell


def lin(h):
    def c(v):
        v /= 255.0
        return round(v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4, 4)
    h = h.lstrip("#")
    return [c(int(h[i:i + 2], 16)) for i in (0, 2, 4)]


def main():
    parts = json.load(open(os.path.join(GAME, "Data", "Parts", "parts.json")))
    layout = json.load(open(os.path.join(GAME, "Data", "Fitment", "layout.json")))
    out, missing = {}, []
    for f in sorted(glob.glob(os.path.join(ROOT, "data", "models", "*.json"))):
        name = os.path.basename(f)[:-5]
        m = json.load(open(f))
        info_file = os.path.join(GAME, "Data", "BikesV2", name + ".json")
        info = json.load(open(info_file)) if os.path.exists(info_file) else {}
        if info.get("format") != 2:
            missing.append(name)
            continue
        style = STYLE[m["family"].lower()]
        stock = dict(parts["frame"][layout["reference_builds"][style]["frame"]])
        g = m["geometry"]
        brand = SHOWN.get(m["brand"].lower(), m["brand"].title())
        # the swingarm pivot as the riding physics wants it: its height over the axle line and how far
        # ahead of the rear axle it is, with the bike tipped so that both axles are level (Fit/BikeLayout V2Frame)
        pt = info["points_mm"]
        fa, ra, pv = pt["front_axle"], pt["rear_axle"], pt["swing_pivot"]
        dx, dz = fa[0] - ra[0], fa[2] - ra[2]
        ln = math.hypot(dx, dz)
        px, pz = pv[0] - ra[0], pv[2] - ra[2]
        geo = dict(stock.get("geo", {}), pivot_y=round((-dz * px + dx * pz) / ln / 1000.0, 4), arm=round((dx * px + dz * pz) / ln / 1000.0, 4))
        stock["geo"] = geo
        stock.update(name=f"{brand} {m['family']}", description=f"The {brand} {m['family'].lower()}: {m.get('engine_label', '')}, {m.get('frame_label', '')}.".replace(": , ", ": "),
                     wheelbase=round(ln / 1000.0, 4), rake_deg=round(g["rake"] - math.degrees(math.atan2(dz, dx)), 3), seat_height=g["seat_height"] / 1000.0,
                     color=lin(m["colours"]["frame"]), styles=[style], type=f"{brand} {m['family']}",
                     v2={"model": name, "family": m["family"].lower(), "brand": brand, "lead": name in LEADS, "body": lin(m["colours"]["body"]),
                         "points_mm": info["points_mm"]})
        out["v2_" + name] = stock
    dst = os.path.join(GAME, "Data", "BikesV2", "parts_v2.json")
    json.dump({"frame": out}, open(dst, "w"), indent=1)
    print(f"{len(out)} V2 bikes in {dst}; not exported yet: {missing or 'none'}")


if __name__ == "__main__":
    main()
