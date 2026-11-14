"""Wave D: the MX bikes, drawn from scratch as whole bikes (no road bike stood on stilts).

A light single in a tall frame on 300 mm of suspension travel: a high front mudguard under the lower yoke,
a number board, a short tank under radiator shrouds that reach forward beside the fork, a long flat seat
that climbs onto the tank, side number panels, a high rear mudguard, and no lamp, mirrors or plate.
Each brand has its own frame and engine so the three do not share an outline:
Varga a steel central tube frame with an upright four-stroke, Kazeda an aluminium twin spar with the
cylinder leant back, Corvano a trellis with a two-stroke and its fat expansion pipe.

    python3 foundry/mx_draft.py            # (re)write the models
"""
import copy
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sport_draft import ROOT, load  # noqa: E402
from classic_draft import fork_top  # noqa: E402
from cruiser_draft import shifted_engine  # noqa: E402

FR, RR, WB, RAKE, L, YK = 353, 340, 1480, 27.0, 900, 715


def base():
    """Everything the three share: wheels, fork, stance, rider triangle, seat line and mudguards."""
    d = load("varga_naked")
    rk = math.radians(RAKE)
    g = {"wheelbase": WB, "rake": RAKE, "trail": 118, "seat_height": 955, "hip_x": 610, "ground_clearance": 335}
    t = fork_top(g, FR, L)
    tx, tz = round(t[0], 1), round(t[1], 1)
    yk = (WB - YK * math.sin(rk), FR + YK * math.cos(rk))

    def along(dist, off=0.0):          # a point on the fork line, dist up from the axle, off ahead of it
        return [round(WB - dist * math.sin(rk) + off * math.cos(rk), 1), round(FR + dist * math.cos(rk) + off * math.sin(rk), 1)]
    board = {"pts": [along(YK + 2, 60) + [1], along(YK + 80, 84), along(L + 40, 70) + [1], along(L + 44, 48) + [1], along(YK + 6, 44) + [1]], "cls": "plate", "t": 0.5, "part": "head"}
    bars = [{"tube": 26, "pts": [[tx, tz], [tx - 10, tz + 44]], "cls": "steelt"},
            {"tube": 24, "pts": [[tx - 10, tz + 44], [tx - 26, tz + 88], [tx - 70, tz + 90]], "cls": "steelt", "t": 0.4},
            {"pts": [[tx - 44, tz + 66, 1], [tx + 8, tz + 62, 1], [tx + 6, tz + 84, 1], [tx - 40, tz + 90, 1]], "cls": "plastic", "t": 0.6},
            {"tube": 9, "pts": [[tx - 70, tz + 74], [tx - 6, tz + 80]], "cls": "dark"},
            {"circle": 20, "at": [tx - 84, tz + 90], "cls": "cast"}]
    guard = [{"pts": [[yk[0] - 84, yk[1] - 84, 1], [yk[0] - 36, yk[1] - 30], [yk[0] + 50, yk[1] + 4], [yk[0] + 190, yk[1] + 18], [yk[0] + 350, yk[1] + 6],
                      [yk[0] + 500, yk[1] - 36, 1], [yk[0] + 496, yk[1] - 50, 1], [yk[0] + 350, yk[1] - 20], [yk[0] + 190, yk[1] - 8],
                      [yk[0] + 60, yk[1] - 22], [yk[0] - 14, yk[1] - 54], [yk[0] - 66, yk[1] - 98, 1]], "cls": "body", "t": 0.8},
             {"pts": [along(40, 34) + [1], along(330, 40), along(360, 22) + [1], along(350, -4) + [1], along(60, 6) + [1]], "cls": "body", "t": 0.5}]
    seat = [[930, 1014, 1], [870, 986], [770, 963], [650, 955], [500, 957], [350, 965], [200, 976, 1],
            [206, 940, 1], [390, 906], [560, 890], [710, 890], [830, 912], [906, 952, 1]]
    tail = [{"pts": [[340, 944, 1], [200, 966], [50, 984], [-120, 1000], [-262, 1014, 1], [-262, 998, 1], [-120, 976], [40, 954], [200, 924], [340, 884, 1]], "cls": "body", "t": 0.8}]
    m = {
        "family": "MX", "rev": "A",
        "deck": {"nose": [900, 985], "tank_pitch": -12, "seat_pitch": 2},
        "geometry": g,
        "wheels": {"front": {"tyre_r": FR, "rim_r": 280, "spokes": 36, "wire": True, "disc": 130, "phase": 0, "tread": "knobbly"},
                   "rear": {"tyre_r": RR, "rim_r": 254, "spokes": 36, "wire": True, "disc": 120, "phase": 15, "tread": "knobbly"}},
        "fork": {"length": L, "lower_yoke": YK, "offset": 30, "slider_top": 420, "usd": True},
        "front_caliper": [[p[0] + WB - d["geometry"]["wheelbase"], p[1] + FR - 300] + p[2:] for p in d["front_caliper"]],
        "rear_caliper": {k: [[p[0], p[1] + RR - 316] + p[2:] for p in v] for k, v in d["rear_caliper"].items()},
        "swingarm": {"pivot": [500, 470], "sprocket_r": 110, "cls": "alu",
                     "outline": [[545, 512, 1], [400, 500], [200, 440], [40, 388], [-44, 366], [-52, 314], [30, 300], [250, 362], [420, 418], [545, 430, 1]],
                     "holes": [[[400, 478, 1], [290, 462, 1], [180, 420, 1], [300, 396, 1], [400, 424, 1]]]},
        "shock": {"top": [556, 790], "bottom": [470, 452]},
        "front_end": [board] + bars,
        "hangers": [{"pts": [[520, 470, 1], [560, 440], [600, 404, 1], [560, 392, 1], [520, 420, 1]], "cls": "cast"},
                    {"tube": 14, "pts": [[540, 404], [612, 404]], "cls": "dark"},
                    {"tube": 10, "pts": [[600, 420], [690, 440], [700, 470]], "cls": "dark", "t": 0.3}],
        "fit": {"plate_hanger": False},
        "body": {"overall": [-RR - 20, WB + FR + 20], "behind": [], "front_guard": guard, "tail": tail,
                 "seat": [{"pts": seat, "cls": "seat"}, {"pts": [[860, 970], [760, 950], [650, 942], [500, 944], [350, 952], [230, 960]], "cls": "stitch", "closed": False}],
                 "tank": [], "panels": [], "caps": [{"at": [966, 1036], "r": 15, "cls": "plastic"}], "plate_hanger": []},
    }
    return m, (tx, tz)


def single(angle, bore, length, head, k=0.78, dx=-50):
    e = shifted_engine("varga_naked", dx, 80, k)
    e["layout"] = "single"
    e["cylinders"] = [{"angle": angle, "bore_w": bore, "start": 92, "length": length, "head": head, "fins": False}]
    return e


def side_panel(pts, oval):
    """A side number panel in the bike's colour with a pale number plate on it."""
    return [{"pts": pts, "cls": "body", "t": 0.5}, {"pts": oval, "cls": "plate", "t": 0.9}]


def varga():
    """The lead: a steel central tube frame painted in the bike's colour, an upright four-stroke single, flat
    sharp shrouds, and the pipe looped round the cylinder then run high to a short silencer under the panel."""
    m, _ = base()
    m.update(name="Varga MX (working title X1)", brand="varga", engine_label="four-stroke single", frame_label="steel central tube frame, long-travel suspension",
             colours={"body": "#F26A1B", "frame": "#F26A1B", "rim": "#1b1c1f", "fork": "#d9dbde", "accent": "#F4F4F2", "seat": "#1c1c1e"})
    e = single(80, 136, 150, 104)
    e["extras"] = [{"pts": [[650, 720, 1], [596, 730], [560, 700], [566, 650], [620, 640, 1], [668, 672, 1]], "cls": "engh", "t": 0.6}]
    m["engine"] = e
    m["radiator"] = {"at": [940, 790], "angle": 66, "len": 180, "w": 34}
    m["exhaust"] = {"headers": [{"pts": [[850, 770], [900, 756], [922, 700], [896, 650], [820, 664], [680, 740], [540, 800], [440, 842]], "w": 40, "front": True}],
                    "cans": [{"a": [440, 842], "b": [150, 906], "w": 84, "front": True}]}
    m["frame"] = {"nodes": {"A": [770, 880], "PV": [512, 610], "PB": [520, 372], "D0": [990, 760], "D1": [880, 410], "D2": [760, 345],
                            "S1": [600, 850], "S2": [130, 946], "S3": [520, 640], "S4": [210, 900]},
                  "tubes": [{"path": ["S1", "S2"], "w": 22, "cls": "sub"}, {"path": ["S3", "S4"], "w": 20, "cls": "sub"},
                            {"path": ["head_t", "A", "PV", "PB"], "w": 38, "t": 0.5},
                            {"path": ["head_b", "D0", "D1", "D2", "PB"], "w": 30, "t": 0.5}], "plates": [], "sub_y": 96}
    b = m["body"]
    # tank and shroud as one unit: up from the seat nose to the filler, forward to a flat sharp tip beside the fork,
    # back along a straight lower edge over the radiator
    b["tank"] = [{"pts": [[886, 960, 1], [920, 1016], [990, 1050], [1060, 1066, 1], [1150, 1010, 1], [1262, 900, 1], [1236, 880, 1], [1090, 800, 1], [1000, 716, 1],
                          [930, 720, 1], [896, 800, 1]], "cls": "body", "t": 0.4},
                 {"pts": [[1236, 892], [1110, 916], [980, 900]], "cls": "bodyline", "closed": False},
                 {"pts": [[960, 1020], [1040, 1042], [1130, 1000]], "cls": "hl", "closed": False}]
    b["behind"] = [{"pts": [[590, 880, 1], [360, 904], [400, 730], [520, 700, 1]], "cls": "plastic fender", "t": 0.6, "slim": True}]
    b["panels"] = side_panel([[600, 896, 1], [380, 912], [170, 936, 1], [250, 800, 1], [400, 700, 1], [540, 690, 1], [610, 780, 1]],
                             [[540, 870, 1], [330, 892, 1], [380, 780, 1], [530, 760, 1]])
    return m


def kazeda():
    """Kazeda: an aluminium twin spar frame left bare, the cylinder leant back with the pipe leaving the rear of
    the head and running straight to the silencer, layered angular shrouds in two colours and a raised sharp tail."""
    m, _ = base()
    m.update(name="Kazeda MX (working title X2)", brand="kazeda", engine_label="four-stroke single, cylinder leant back", frame_label="aluminium twin spar frame, long-travel suspension",
             colours={"body": "#1C4FBF", "frame": "#B9BEC4", "rim": "#1b1c1f", "fork": "#d9dbde", "accent": "#D7E23A", "seat": "#101114"})
    e = single(101, 132, 140, 100, dx=-20)
    e["extras"] = [{"pts": [[850, 760, 1], [900, 780], [950, 750], [940, 690], [880, 670, 1], [840, 700, 1]], "cls": "engh", "t": 0.6}]
    m["engine"] = e
    m["radiator"] = {"at": [946, 800], "angle": 66, "len": 170, "w": 34}
    m["exhaust"] = {"headers": [{"pts": [[700, 800], [640, 826], [540, 852], [450, 872]], "w": 42, "front": True}],
                    "cans": [{"a": [450, 872], "b": [130, 936], "w": 90, "front": True}]}
    m["frame"] = {"nodes": {"PV": [520, 640], "PB": [524, 372], "D0": [990, 760], "D1": [890, 410], "D2": [760, 345],
                            "S1": [600, 800], "S2": [100, 990], "S3": [520, 650], "S4": [190, 930]},
                  "tubes": [{"path": ["S1", "S2"], "w": 22, "cls": "sub"}, {"path": ["S3", "S4"], "w": 20, "cls": "sub"},
                            {"path": ["head_b", "D0", "D1", "D2", "PB"], "w": 30, "t": 0.2},
                            {"path": ["head_t", "PV"], "w": 64, "t": 0}, {"path": ["PV", "PB"], "w": 56, "t": 0}], "plates": [], "sub_y": 96}
    b = m["body"]
    b["tail"] = [{"pts": [[340, 944, 1], [200, 972], [50, 1004], [-110, 1040], [-250, 1076, 1], [-246, 1058, 1], [-110, 1012], [40, 968], [200, 926], [340, 884, 1]], "cls": "body", "t": 0.5}]
    b["seat"][0]["pts"] = [[930, 1014, 1], [870, 986], [770, 963], [650, 955], [500, 958], [350, 972], [200, 996, 1],
                           [206, 950, 1], [390, 906], [560, 890], [710, 890], [830, 912], [906, 952, 1]]
    b["seat"][1]["pts"] = [[860, 970], [760, 950], [650, 942], [500, 945], [350, 958], [230, 976]]
    b["tank"] = [{"pts": [[886, 960, 1], [916, 1020, 1], [1050, 1070, 1], [1120, 1040, 1], [1270, 930, 1], [1250, 906, 1], [1130, 880, 1], [1040, 760, 1], [960, 740, 1], [900, 800, 1]], "cls": "body", "t": 0.2},
                 {"pts": [[1262, 924, 1], [1246, 904, 1], [1126, 876, 1], [1036, 756, 1], [1004, 748, 1], [1090, 884, 1]], "cls": "plastic", "t": 0.2, "draw_only": True},
                 {"pts": [[940, 1016], [1040, 1050], [1110, 1022]], "cls": "hl", "closed": False}]
    b["behind"] = [{"pts": [[590, 880, 1], [360, 904], [400, 730], [520, 700, 1]], "cls": "plastic fender", "t": 0.6, "slim": True}]
    b["panels"] = side_panel([[620, 900, 1], [400, 916, 1], [120, 1000, 1], [300, 800, 1], [470, 720, 1], [600, 760, 1]],
                             [[560, 880, 1], [390, 894, 1], [330, 830, 1], [480, 770, 1], [570, 800, 1]])
    return m


def corvano():
    """Corvano: a slim two-stroke in a red trellis frame, its fat expansion pipe curling down the front of the
    engine, pale rounded plastics, a red seat and a short stubby tail."""
    m, _ = base()
    m.update(name="Corvano MX (working title X3)", brand="corvano", engine_label="two-stroke single", frame_label="steel trellis frame, long-travel suspension",
             colours={"body": "#E9E6DF", "frame": "#B3122A", "rim": "#B08D57", "fork": "#d9dbde", "accent": "#B3122A", "seat": "#B3122A"})
    e = single(78, 118, 104, 74, k=0.7, dx=-60)
    e["extras"] = [{"pts": [[690, 680, 1], [630, 700], [590, 676], [596, 630], [650, 616, 1], [700, 640, 1]], "cls": "engh", "t": 0.6}]
    m["engine"] = e
    m["radiator"] = {"at": [946, 800], "angle": 66, "len": 160, "w": 30}
    m["exhaust"] = {"headers": [{"pts": [[810, 640], [860, 626], [884, 580], [862, 520], [800, 500], [700, 540], [600, 640], [520, 760]], "w": 86, "front": True}],
                    "cans": [{"a": [520, 760], "b": [300, 880], "w": 44, "front": True}, {"a": [300, 880], "b": [140, 930], "w": 66, "front": True}]}
    m["frame"] = {"nodes": {"A": [840, 940], "B": [650, 770], "PV": [512, 610], "PB": [520, 372], "C": [984, 790], "D": [780, 660], "D1": [884, 410], "D2": [760, 345],
                            "S1": [600, 800], "S2": [150, 946], "S3": [520, 640], "S4": [230, 896]},
                  "tubes": [{"path": ["S1", "S2"], "w": 20, "cls": "sub"}, {"path": ["S3", "S4"], "w": 18, "cls": "sub"}, {"path": ["S3", "S1"], "w": 18, "cls": "sub"},
                            {"path": ["A", "C"], "w": 20}, {"path": ["A", "D"], "w": 20}, {"path": ["B", "D"], "w": 20}, {"path": ["C", "D"], "w": 20},
                            {"path": ["head_t", "A", "B", "PV", "PB"], "w": 26, "t": 0.2},
                            {"path": ["D", "PV"], "w": 24},
                            {"path": ["head_b", "C", "D1", "D2", "PB"], "w": 26, "t": 0.3}], "plates": [], "sub_y": 96}
    b = m["body"]
    b["tail"] = [{"pts": [[340, 944, 1], [200, 966], [60, 980], [-80, 990], [-190, 992, 1], [-196, 974, 1], [-80, 966], [50, 952], [200, 924], [340, 884, 1]], "cls": "body", "t": 0.9}]
    b["tank"] = [{"pts": [[886, 960, 1], [916, 1012], [980, 1046], [1056, 1056], [1130, 1010], [1210, 930], [1236, 880], [1200, 856], [1100, 820], [1020, 760], [960, 750], [906, 800]], "cls": "body", "t": 0.9},
                 {"pts": [[1000, 800], [1080, 850], [1190, 880]], "cls": "bodyline", "closed": False},
                 {"pts": [[950, 1016], [1040, 1034], [1110, 996]], "cls": "hl", "closed": False}]
    b["behind"] = [{"pts": [[590, 880, 1], [380, 900], [420, 740], [520, 710, 1]], "cls": "plastic fender", "t": 0.6, "slim": True}]
    b["panels"] = side_panel([[610, 896, 1], [420, 916], [230, 930], [250, 850], [340, 760], [480, 720], [590, 760]],
                             [[550, 872], [400, 888], [330, 870], [370, 800], [480, 764], [550, 800]])
    return m


MODELS = {"varga_mx": varga, "kazeda_mx": kazeda, "corvano_mx": corvano}

if __name__ == "__main__":
    for n in (sys.argv[1:] or MODELS):
        path = os.path.join(ROOT, "data", "models", n + ".json")
        json.dump(MODELS[n](), open(path, "w"), indent=1)
        open(path, "a").write("\n")
        print("drafted", path)
