"""Common parts into the game, the data half: the shop entries and part files the game reads for the approved
common parts (beenster 17 Nov: "bring all of the common items into the game now").

Reads out/parts_game (parts_export.py: one GLB per part, origin at its mount point on its lead bike) and writes

    Data/BikesV2/common/<group>/<key>.glb + .json      the part files (copied as they are)
    Data/BikesV2/common_parts.json                      catalogue entries, in the same shape as parts_v2.json

Each entry is a normal catalogue part of its category (stats copied from a stock part of that category, so the
game rides it as it would that part) with "styles" = the V2 families it fits, plus a "common" block the game's
V2 drawing reads:

    "common": {"sub": "grip", "pieces": [{"file": "grip/knurl.glb", "anchor": "steer_top",
               "offset_mm": [dx, dy, dz], "node": "steer", "sub": "grip"}]}

A bar also gives grip_offset_mm (its grip point from steer_top): the game moves the pieces on the bar (grips,
ends, levers, switches, stalk mirrors; the bike's own or common) by the change in grip point. The game takes the
bike's own pieces of that sub away and puts each piece's file at the bike's anchor point
(points_mm) plus offset_mm, in that moving node. Discs, calipers and indicators carry a front and a rear piece.

    python3 foundry/common_catalogue.py [--data <repo Data dir>]     # default: <repo>/Data
"""
import json
import math
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REPO = os.path.dirname(ROOT)
SRC = os.path.join(ROOT, "out", "parts_game")
sys.path.insert(0, HERE)
import game_common as G  # noqa: E402

# which game slot (catalogue category) each parts group goes in, and the stock part whose stats it rides with
CATEGORY = {
    "bar": "handlebars", "high_bar": "handlebars", "grip": "grips", "bar_end": "bar_ends", "levers": "levers",
    "switches": "switchgear", "mirrors": "mirrors", "headlamp": "headlight", "clocks": "dashboard",
    "indicators": "indicators", "tail_light": "tail_light", "plate_hanger": "plate_hanger", "silencer": "silencers",
    "pegs": "rearsets", "heel_plates": "heel_plates", "foot_controls": "foot_controls", "disc": "disc", "caliper": "caliper",
    "sprocket": "sprocket", "shock": "shock", "filler_cap": "filler_cap", "side_stand": "side_stand",
}
NEW_SLOTS = ("grips", "bar_ends", "levers", "switchgear", "tail_light", "plate_hanger", "silencers", "heel_plates",
             "foot_controls", "sprocket", "filler_cap")
BASE = {
    ("bar", "street"): "flat_bar", ("bar", "lowrise"): "naked_low_bar", ("bar", "highrise"): "roader_risers",
    ("bar", "mx"): "mx_tall", ("bar", "clipon_low"): "sport_low_clip", ("bar", "clipon_raised"): "sport_raised_clip",
    ("bar", "drag"): "drag_bar", ("bar", "touring"): "touring_bend_bar", ("bar", "tracker"): "scrambler_tracker_bar",
    ("bar", "ace"): "cafe_ace_bar", ("high_bar", "ape"): "ape_hangers", ("high_bar", "ape_tall"): "chopper_tall_apes",
    ("high_bar", "zbar"): "bobber_z_bar", ("high_bar", "beach"): "cruiser_beach_bar",
    "mirrors": "round_mirrors", ("mirrors", "fairing_aero"): "fairing_mirrors", ("mirrors", "fairing_wing"): "fairing_mirrors",
    ("mirrors", "barend_round"): "bar_end_mirrors", ("mirrors", "barend_teardrop"): "bar_end_mirrors",
    "headlamp": "round_7", ("headlamp", "twin"): "twin_round", ("headlamp", "stacked"): "chopper_stacked",
    ("headlamp", "halo"): "naked_round_led", ("headlamp", "rect"): "square_chrome", ("headlamp", "hex"): "naked_round_led",
    "clocks": "analog_speedo", ("clocks", "twin_round"): "twin_clocks", ("clocks", "round_screen"): "tft_compact",
    ("clocks", "wide_screen"): "tft_naked", ("clocks", "tall_screen"): "tft_touring", ("clocks", "bar_unit"): "lcd_basic",
    "indicators": "mini_led", ("indicators", "chrome_short"): "stalk_amber", ("indicators", "chrome_long"): "stalk_amber",
    ("indicators", "bullet"): "bullet_chrome", ("indicators", "flush"): "flush_led",
    "pegs": "road_pegs", ("pegs", "knurled"): "alloy_rearsets", ("pegs", "race"): "race_ti_rearsets", ("pegs", "cleat"): "mx_cleats",
    ("pegs", "board"): "floorboards", ("pegs", "bullet"): "chopper_highway_pegs",
    "disc": "drilled_floating_300", ("disc", "wave"): "drilled_wave_270",
    "caliper": "four_piston_axial", ("caliper", "lobed"): "four_piston_radial", ("caliper", "monobloc"): "four_piston_radial",
    ("caliper", "compact"): "twin_piston",
    "shock": "sport_mono", ("shock", "piggyback"): "sport_mono", ("shock", "coilover"): "twin_shock_gas",
    ("shock", "shroud"): "twin_shock", ("shock", "air"): "enduro_mono",
    "side_stand": "stamped_side",
}
# a light stock-like entry for the new cosmetic slots
NEW_BASE = dict(mass=0.3, color=[0.08, 0.08, 0.09])
PRICE = {"bar": 85, "high_bar": 160, "grip": 25, "bar_end": 30, "levers": 120, "switches": 90, "mirrors": 60, "headlamp": 140,
         "clocks": 160, "indicators": 55, "tail_light": 60, "plate_hanger": 95, "silencer": 320, "pegs": 110, "heel_plates": 80,
         "foot_controls": 120, "disc": 210, "caliper": 260, "sprocket": 70, "shock": 420, "filler_cap": 75, "side_stand": 60}
MATERIAL = {"grips": "rubber", "silencers": "metal", "tail_light": "plastic", "switchgear": "plastic"}
ANCHOR = {"bar_clamp": "steer_top", "top_yoke": "steer_top", "front_axle": "front_axle", "rear_axle": "rear_axle",
          "rider_peg": "peg", "shock_top": "shock_top", "can_inlet": "swing_pivot", "cap_base": "tank_cap",
          "part_centre": "hip"}
NODE = {"steering": "steer", "front_wheel": "front_wheel", "rear_wheel": "rear_wheel", "swingarm": "swingarm", "shock": "",
        "frame": ""}
GROUP_SUB = {"bar": "bar", "high_bar": "bar", "grip": "grip", "bar_end": "bar_end", "levers": "levers", "switches": "switches",
             "mirrors": "mirrors", "headlamp": "headlamp", "clocks": "clocks", "indicators_front": "ind_front",
             "indicators_rear": "ind_rear", "tail_light": "tail_light", "plate_hanger": "plate_hanger", "silencer": "silencer",
             "pegs": "pegs", "heel_plates": "heel_plates", "foot_controls": "foot_controls", "disc_front": "disc_front",
             "disc_rear": "disc_rear", "caliper_front": "caliper_front", "caliper_rear": "caliper_rear", "sprocket": "sprocket",
             "shock": "shock", "filler_cap": "filler_cap"}
# groups exported in two halves that make one shop part
PAIRS = {"indicators": ("indicators_front", "indicators_rear"), "disc": ("disc_front", "disc_rear"),
         "caliper": ("caliper_front", "caliper_rear")}


def points(model):
    """The lead bike's named points (as game_parts.points writes them in points_mm), mm."""
    g, wh, fk, sw = model["geometry"], model["wheels"], model["fork"], model["swingarm"]
    rake = math.radians(g["rake"])
    F, A = (g["wheelbase"], wh["front"]["tyre_r"]), (0.0, wh["rear"]["tyre_r"])
    uf, nb = (-math.sin(rake), math.cos(rake)), (-math.cos(rake), -math.sin(rake))
    at = lambda d, o=0.0: [F[0] + uf[0] * d + nb[0] * o, 0.0, F[1] + uf[1] * d + nb[1] * o]
    out = {"front_axle": [F[0], 0.0, F[1]], "rear_axle": [0.0, 0.0, A[1]], "steer_top": at(fk["length"] - 22, fk["offset"]),
           "swing_pivot": [sw["pivot"][0], 0.0, sw["pivot"][1]], "hip": [g["hip_x"], 0.0, g["seat_height"]]}
    peg = next((h["at"] for h in model.get("hangers", []) if h.get("circle")), None) or (600, 404)
    out["peg"] = [peg[0], 0.0, peg[1]]
    sh = model.get("shock") or {}
    if sh.get("top"):
        out["shock_top"] = [sh["top"][0], 0.0, sh["top"][1]]
    return out


def model(mn):
    return json.load(open(os.path.join(ROOT, "data", "models", mn + ".json")))


# parts that sit on the bar by the grips: placed from the bike's grip point (points_mm "grip", taken on the centre
# line), so they follow each bike's own bar; a common bar moves that point (its "grip_offset_mm" from steer_top)
GRIP_GROUPS = ("grip", "bar_end", "levers", "switches", "mirrors")


def lead_grip(lead):
    import bars3d as BR
    h = BR.model_hand(lead)
    return [h[0], 0.0, h[2]]


def bar_grip(lead, key):
    """Where a library bar puts the grip point on the lead bike, from its steer_top (mm, centre line)."""
    import bars3d as BR
    import highbars3d  # noqa: F401  registers the high bars
    h = BR.hand_point(lead, key)
    st = points(lead)["steer_top"]
    return [round(h[0] - st[0], 1), 0.0, round(h[2] - st[2], 1)], round(abs(h[1]), 1)


def piece(p, lead):
    """One file of a part: where it goes on any V2 bike (an anchor point and the offset from it on the lead bike)."""
    pts = points(lead)
    pts["grip"] = lead_grip(lead)
    anchor = ANCHOR[p["mount"]]
    if p["group"] in GRIP_GROUPS and not p["key"].startswith("fairing"):
        anchor = "grip"
        if p["group"] in ("grip", "bar_end"):
            # parts_export fits grips and ends on the library street bar: their grip point is that bar's
            import bars3d as BR
            h = BR.hand_point(lead, "street")
            pts["grip"] = [h[0], 0.0, h[2]]
    fallback = None
    if anchor == "tank_cap":
        anchor, fallback = "tank_cap", "hip"
    # the cap's own origin is the cap neck centre, which is what tank_cap names: no offset from it
    base = p["mount_mm"] if anchor == "tank_cap" else pts[anchor]
    off = [round(p["mount_mm"][i] - base[i], 1) for i in range(3)]
    out = dict(file=f"{p['group']}/{p['key']}.glb", looks=f"{p['group']}/{p['key']}.json", anchor=anchor, offset_mm=off,
               node=NODE[p["moves_with"]], sub=GROUP_SUB[p["group"]], mount_mm=p["mount_mm"])
    if fallback:
        out["fallback"] = dict(anchor=fallback, offset_mm=[round(p["mount_mm"][i] - pts[fallback][i], 1) for i in range(3)])
    return out


def entry(group, key, name, fits, finishes, pieces, stock):
    cat = CATEGORY[group]
    b = BASE.get((group, key)) or BASE.get(group)
    e = dict(stock[cat][b]) if b and cat in stock and b in stock[cat] else dict(NEW_BASE)
    fams = ", ".join(f.lower() for f in fits)
    e.update(name=name, description=f"{name}. A common part, fitted to suit {fams} bikes.", styles=[f.lower() for f in fits],
             price=PRICE.get(group, 80), common=dict(group=group, key=key, sub=pieces[0]["sub"], pieces=pieces, finishes=finishes))
    if cat in MATERIAL:
        e["material"] = MATERIAL[cat]
    e.pop("v2", None)
    return cat, f"v2c_{group}_{key}", e


def main(data):
    man = json.load(open(os.path.join(SRC, "parts_manifest.json")))
    stock = json.load(open(os.path.join(data, "Parts", "parts.json")))
    leads = {}
    by = {}
    for p in man["parts"]:
        if not p.get("file"):
            continue
        by[(p["group"], p["key"])] = p
    cats = {}
    done = set()
    for (g, k), p in by.items():
        if (g, k) in done:
            continue
        pair = next((pg for pg, halves in PAIRS.items() if g in halves), None)
        if pair:
            parts = [by[(h, k)] for h in PAIRS[pair] if (h, k) in by]
            done.update((h, k) for h in PAIRS[pair])
            grp = pair
        else:
            parts = [p]
            done.add((g, k))
            grp = g
        pcs = []
        for q in parts:
            lead = leads.setdefault(q["fitted_on"], model(q["fitted_on"]))
            pcs.append(piece(q, lead))
        cat, pid, e = entry(grp, k, p["name"], p["fits"], p["finishes"], pcs, stock)
        if grp in ("bar", "high_bar"):
            off, gy = bar_grip(leads[p["fitted_on"]], k)
            e["common"]["grip_offset_mm"], e["common"]["grip_y_mm"] = off, gy
            e["common"]["moves"] = ["grip", "bar_end", "levers", "switches", "mirrors"]
        cats.setdefault(cat, {})[pid] = e
    # the side stand: the bike's own stand, drawn by the bike (sidestand3d), so its entry carries no file
    cats.setdefault("side_stand", {})["v2c_side_stand_v2"] = dict(stock["side_stand"]["stamped_side"], name="Side stand",
        description="The bike's own side stand: pressed or forged steel, sized so it holds the bike at its lean.",
        styles=["naked", "sport", "cafe", "classic", "adventure", "mx", "cruiser", "chopper"], price=60,
        common=dict(group="side_stand", key="v2", sub="side_stand", pieces=[]))
    dst = os.path.join(data, "BikesV2", "common")
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    for (g, k), p in by.items():
        os.makedirs(os.path.join(dst, g), exist_ok=True)
        for ext in (".glb", ".json"):
            shutil.copy(os.path.join(SRC, g, k + ext), os.path.join(dst, g, k + ext))
    out = dict(about="Common bike parts for the V2 bikes (foundry/common_catalogue.py). Each part rides as its stock "
                     "part of the same category; 'common' says which pieces of the bike it replaces (sub) and where "
                     "its files go (anchor in the bike's points_mm + offset_mm, moving node).",
               new_slots=list(NEW_SLOTS), **cats)
    json.dump(out, open(os.path.join(data, "BikesV2", "common_parts.json"), "w"), indent=1)
    n = sum(len(v) for v in cats.values())
    size = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(dst) for f in fs)
    print(f"COMMON {n} shop parts in {len(cats)} categories, {size / 1e6:.1f} MB of files -> {dst}")


if __name__ == "__main__":
    a = sys.argv[1:]
    main(a[a.index("--data") + 1] if "--data" in a else os.path.join(REPO, "Data"))
