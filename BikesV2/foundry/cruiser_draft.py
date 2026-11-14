"""Cruisers (wave B). The Lodestar cruiser is the lead model and the first Lodestar: a new 45 degree
V-twin in a steel double cradle, long and low, with forward footrests, a teardrop tank, deep
mudguards, pulled-back bars and staggered pipes. It is drawn here by hand.

    python3 foundry/cruiser_draft.py            # (re)write the cruisers
"""
import copy
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sport_draft import ROOT, load  # noqa: E402
from classic_draft import move, fork_top  # noqa: E402

FRONT_R, REAR_R = 330, 325


def lodestar():
    h = load("hartwell_naked")          # the round lamp, clocks and calipers are the range's shared pieces
    g = {"wheelbase": 1640, "rake": 31.0, "trail": 140, "seat_height": 690, "hip_x": 440, "ground_clearance": 125}
    fork = {"length": 760, "lower_yoke": 560, "offset": 40, "slider_top": 340, "usd": False}
    top = fork_top(g, FRONT_R, fork["length"])
    h_top = fork_top(h["geometry"], h["wheels"]["front"]["tyre_r"], h["fork"]["length"])
    head = [s for s in h["front_end"] if s.get("part") == "head"]
    lamp = [move(s, top[0] + 118 - h_top[0] - 99, top[1] - 112 - h_top[1] + 122) for s in head if "pts" in s]
    clocks = [move(s, top[0] + 58 - h_top[0] - 80, top[1] + 22 - h_top[1] - 21) for s in head if "circle" in s]
    tx, tz = round(top[0], 1), round(top[1], 1)
    bars = [{"tube": 30, "pts": [[tx, tz], [tx - 12, tz + 60]], "cls": "cast"},
            {"tube": 24, "pts": [[tx - 12, tz + 60], [tx - 95, tz + 84], [tx - 185, tz + 52]], "cls": "steelt", "t": 0.6},
            {"tube": 9, "pts": [[tx - 180, tz + 38], [tx - 118, tz + 30]], "cls": "dark"},
            {"circle": 20, "at": [tx - 198, tz + 48], "cls": "cast"},
            {"tube": 8, "pts": [[tx - 172, tz + 58], [tx - 180, tz + 138]], "cls": "dark"},
            {"pts": [[tx - 205, tz + 143], [tx - 180, tz + 168], [tx - 150, tz + 153], [tx - 170, tz + 128]], "cls": "plastic", "mirror": True}]
    m = {
        "name": "Lodestar cruiser (working title R1)", "brand": "lodestar", "family": "Cruiser", "rev": "A",
        "engine_label": "45 degree V-twin", "frame_label": "steel double cradle",
        "colours": {"body": "#7A1F1F", "frame": "#15161a", "rim": "#C7CBD0", "fork": "#C7CBD0", "accent": "#D8C9A3", "seat": "#1c1c1e"},
        "deck": {"nose": [600, 736], "tank_pitch": 5, "seat_pitch": 0},
        "geometry": g,
        "wheels": {"front": {"tyre_r": FRONT_R, "rim_r": 208, "spokes": 40, "wire": True, "disc": 150, "phase": 0, "tread": "cruiser"},
                   "rear": {"tyre_r": REAR_R, "rim_r": 205, "spokes": 40, "wire": True, "disc": 125, "phase": 15, "tread": "cruiser"}},
        "fork": fork,
        "front_caliper": [[p[0] + g["wheelbase"] - h["geometry"]["wheelbase"], p[1] + FRONT_R - 300] + p[2:] for p in h["front_caliper"]],
        "engine": {
            "layout": "vtwin45", "crank": [845, 372], "countershaft": [610, 384],
            "case": [[996, 396], [984, 296], [915, 244], [710, 236], [600, 262], [530, 320], [500, 392], [512, 468], [570, 514], [680, 530], [845, 508], [955, 492]],
            "cylinders": [{"angle": 67.5, "bore_w": 164, "start": 118, "length": 204, "head": 80, "fins": True},
                          {"angle": 112.5, "bore_w": 164, "start": 118, "length": 204, "head": 80, "fins": True}],
            "covers": [{"c": [845, 372], "r": 114, "bolts": 8}, {"c": [628, 386], "r": 86, "bolts": 6}],
            "extras": [{"pts": [[782, 588, 1], [814, 548], [876, 548], [908, 588, 1], [908, 648], [876, 684], [814, 684], [782, 648]], "cls": "chromef", "t": 0.9},
                       {"pts": [[1020, 796, 1], [1110, 806], [1200, 780, 1], [1160, 705], [1060, 690, 1], [1000, 735, 1]], "cls": "plastic", "t": 0.7},
                       {"pts": [[1250, 700, 1], [1180, 585], [1108, 462, 1], [1004, 500, 1], [1040, 624], [1112, 734, 1]], "cls": "engh", "t": 0.5},
                       {"pts": [[640, 752, 1], [720, 760], [760, 720, 1], [700, 668], [620, 690, 1]], "cls": "plastic", "t": 0.7},
                       {"pts": [[610, 660, 1], [480, 668], [402, 640], [384, 560, 1], [430, 468], [540, 452, 1], [612, 520, 1]], "cls": "plastic", "t": 0.7}]},
        "exhaust": {"headers": [{"pts": [[1016, 640], [1042, 545], [1040, 410], [1002, 300], [920, 262], [660, 258], [500, 256]], "w": 48},
                                {"pts": [[650, 640], [585, 590], [530, 470], [480, 372], [440, 348]], "w": 48}],
                    "cans": [{"a": [500, 258], "b": [-150, 270], "w": 86, "front": True}, {"a": [440, 348], "b": [-190, 364], "w": 86, "front": True}]},
        "swingarm": {"pivot": [500, 400], "sprocket_r": 95,
                     "outline": [[540, 440, 1], [420, 440], [200, 392], [40, 366], [-40, 350], [-46, 306], [30, 290], [220, 320], [420, 360], [540, 362, 1]], "holes": []},
        "shock": {"top": [330, 618], "bottom": [48, 352], "twin": True},
        "frame": {"nodes": {"B": [690, 760], "Q": [520, 600], "P": [500, 400], "D1": [1086, 446], "D2": [1022, 222], "R": [560, 222], "S2": [60, 600], "S3": [300, 640]},
                  "tubes": [{"path": ["Q", "S3", "S2"], "w": 24, "cls": "sub"},
                            {"path": ["head_t", "B", "Q", "P"], "w": 36, "t": 0.4},
                            {"path": ["head_b", "D1", "D2", "R", "P"], "w": 32, "t": 0.5}], "plates": [], "sub_y": 96},
        "front_end": lamp + clocks + bars,
        "hangers": [{"tube": 16, "pts": [[1044, 300], [1098, 338]], "cls": "cast"}, {"circle": 17, "at": [1102, 340], "cls": "bolt"},
                    {"tube": 10, "pts": [[1102, 340], [1136, 392]], "cls": "dark"},
                    {"tube": 16, "pts": [[720, 222], [500, 168]], "cls": "dark"}],
        "rear_caliper": {k: [[p[0], p[1] + REAR_R - 316] + p[2:] for p in v] for k, v in h["rear_caliper"].items()},
        "fit": {"plate_hanger": False},
        "body": {
            "overall": [-REAR_R - 40, g["wheelbase"] + FRONT_R + 40],
            "behind": [],
            "front_guard": [{"arc": {"c": [g["wheelbase"], FRONT_R], "r0": FRONT_R + 14, "r1": FRONT_R + 34, "a0": 58, "a1": 140}, "cls": "body"}],
            "tail": [{"pts": [[500, 676, 1], [330, 672], [236, 690, 1], [230, 656, 1], [330, 640], [500, 640, 1]], "cls": "body", "t": 0.7}],
            "seat": [{"pts": [[626, 762, 1], [570, 734], [490, 712], [400, 708], [310, 722], [250, 758, 1], [238, 716, 1], [300, 676], [420, 654], [560, 662], [634, 718, 1]], "cls": "seat"},
                     {"pts": [[570, 724], [490, 702], [400, 698], [330, 710]], "cls": "stitch", "closed": False}],
            "tank": [{"pts": [[1092, 858, 1], [1060, 928], [985, 968], [875, 970], [768, 938], [682, 874], [620, 798, 1], [604, 738, 1], [1070, 778, 1], [1100, 820]], "cls": "body"},
                     {"pts": [[1040, 910], [972, 940], [880, 942], [790, 914]], "cls": "hl", "closed": False}],
            "panels": [{"arc": {"c": [0, REAR_R], "r0": REAR_R + 14, "r1": REAR_R + 36, "a0": 46, "a1": 132}, "cls": "body"},
                       {"pts": [[-238, 606, 1], [-270, 584, 1], [-256, 560, 1], [-224, 582, 1]], "cls": "redlens", "t": 0.4}],
            "caps": [{"at": [895, 970], "r": 16, "cls": "chromef"}],
            "plate_hanger": []},
    }
    return m


MODELS = {"lodestar_cruiser": lodestar}

if __name__ == "__main__":
    for n in (sys.argv[1:] or MODELS):
        path = os.path.join(ROOT, "data", "models", n + ".json")
        json.dump(MODELS[n](), open(path, "w"), indent=1)
        open(path, "a").write("\n")
        print("cruiser drafted in", path)
