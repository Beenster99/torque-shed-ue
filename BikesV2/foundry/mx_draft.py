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
    """Kazeda rev B (beenster, 14 Nov 22:55: too like the Varga). A different layout of bodywork: one belt of
    colour runs from the shroud tip right back under the seat into a steeply kicked tail, over a big black lower
    shroud with vents; no side panel, so the bare aluminium twin spar and subframe show; a humped seat; a short
    squared front mudguard; silver rims; and the pipe leaves the rear of the leant-back cylinder for a stubby
    silencer tucked high under the tail."""
    m, _ = base()
    m.update(name="Kazeda MX (working title X2)", brand="kazeda", rev="B", engine_label="four-stroke single, cylinder leant back", frame_label="aluminium twin spar frame, long-travel suspension",
             colours={"body": "#1C4FBF", "frame": "#B9BEC4", "rim": "#C8CDD2", "fork": "#d9dbde", "accent": "#D7E23A", "seat": "#101114"})
    e = single(101, 132, 140, 100, dx=-20)
    e["extras"] = [{"pts": [[850, 760, 1], [900, 780], [950, 750], [940, 690], [880, 670, 1], [840, 700, 1]], "cls": "engh", "t": 0.6}]
    m["engine"] = e
    m["radiator"] = {"at": [946, 800], "angle": 66, "len": 170, "w": 34}
    m["exhaust"] = {"headers": [{"pts": [[700, 800], [620, 836], [480, 880], [340, 916]], "w": 42, "front": True}],
                    "cans": [{"a": [340, 916], "b": [70, 1000], "w": 92}]}
    m["frame"] = {"nodes": {"PV": [520, 640], "PB": [524, 372], "D0": [990, 760], "D1": [890, 410], "D2": [760, 345],
                            "S1": [600, 800], "S2": [60, 1010], "S3": [520, 650], "S4": [150, 960]},
                  "tubes": [{"path": ["S1", "S2"], "w": 24, "cls": "sub"}, {"path": ["S3", "S4"], "w": 22, "cls": "sub"}, {"path": ["S3", "S1"], "w": 20, "cls": "sub"},
                            {"path": ["head_b", "D0", "D1", "D2", "PB"], "w": 30, "t": 0.2},
                            {"path": ["head_t", "PV"], "w": 64, "t": 0}, {"path": ["PV", "PB"], "w": 56, "t": 0}], "plates": [], "sub_y": 96}
    b = m["body"]
    # short squared front mudguard, tip lifted; black fork guards
    x0 = min(q[0] for q in b["front_guard"][0]["pts"]) + 190
    b["front_guard"][0].update(t=0.3, pts=[[round(x0 + (q[0] - x0) * 0.78, 1) if q[0] > x0 else q[0], round(q[1] + max(0, q[0] - x0) * 0.16, 1)] + q[2:] for q in b["front_guard"][0]["pts"]])
    b["front_guard"][1]["cls"] = "plastic"
    b["tail"] = [{"pts": [[340, 944, 1], [200, 968], [90, 1000], [-60, 1070], [-220, 1166, 1], [-212, 1146, 1], [-60, 1040], [70, 972], [200, 926], [340, 884, 1]], "cls": "body", "t": 0.3}]
    b["seat"][0]["pts"] = [[930, 1014, 1], [870, 986], [770, 963], [650, 955], [520, 956], [430, 966], [370, 1000], [320, 1024], [200, 1034, 1],
                           [206, 978, 1], [390, 914], [560, 890], [710, 890], [830, 912], [906, 952, 1]]
    b["seat"][1]["pts"] = [[860, 970], [760, 950], [650, 942], [520, 943], [436, 952], [366, 986]]
    b["tank"] = [{"pts": [[886, 960, 1], [916, 1020, 1], [1040, 1066, 1], [1110, 1044, 1], [1266, 904, 1], [1252, 882, 1], [1090, 930, 1], [960, 900, 1], [900, 880, 1]], "cls": "body", "t": 0.15},
                 {"pts": [[1252, 882, 1], [1090, 930, 1], [960, 900, 1], [904, 880, 1], [930, 730, 1], [1010, 690, 1], [1120, 790, 1]], "cls": "plastic", "t": 0.15, "draw_only": True},
                 {"pts": [[1000, 860, 1], [1090, 880, 1], [1070, 820, 1], [1010, 770, 1]], "cls": "rad", "t": 0.2, "draw_only": True},
                 {"pts": [[940, 1016], [1040, 1050], [1110, 1022]], "cls": "hl", "closed": False}]
    b["behind"] = []
    # the belt: the shroud's colour carried back under the seat and up into the tail, with a small number plate
    b["panels"] = [{"pts": [[906, 950, 1], [900, 880, 1], [700, 850, 1], [480, 860, 1], [300, 910, 1], [60, 1010, 1], [50, 1044, 1], [200, 996], [390, 916], [560, 892], [710, 892], [830, 914]], "cls": "body", "t": 0.15},
                   {"pts": [[440, 876, 1], [330, 906, 1], [190, 962, 1], [330, 936, 1], [450, 904, 1]], "cls": "plate", "t": 0.2}]
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


PAINT = [("Orange", "orange plastics", {"body": "#F26A1B"}), ("Blue", "blue plastics", {"body": "#1C4FBF"}), ("Ivory", "pale plastics", {"body": "#E9E6DF"}),
         ("Black", "black plastics", {"body": "#1b1c1f"}), ("Yellow", "yellow plastics", {"body": "#E8C51E"}), ("Green", "green plastics", {"body": "#2E8B3D"}),
         ("Red", "red plastics", {"body": "#C2182B"})]
OWN = {"varga": 0, "kazeda": 1, "corvano": 2}


def with_kit(fn):
    """The MX kit: seats and tanks, mudguards, lamps for enduro and rally use, silencers, paint and extras.
    Every option is built from the bike's own shapes, so each brand keeps its own look."""
    from kit_draft import tail as retail
    m = fn()
    b, rk = m["body"], math.radians(RAKE)
    tx, tz = (round(v, 1) for v in fork_top(m["geometry"], FR, L))

    def along(dist, off=0.0, flag=None):
        return [round(WB - dist * math.sin(rk) + off * math.cos(rk), 1), round(FR + dist * math.cos(rk) + off * math.sin(rk), 1)] + ([1] if flag else [])

    def seat(dz):
        sp = copy.deepcopy(b["seat"][0])
        k = min(range(len(sp["pts"])), key=lambda i: sp["pts"][i][0])          # the top line runs from the nose to the rear end
        for q in sp["pts"][:k + 1]:
            q[1] = round(q[1] + dz * (1.0 if 330 < q[0] < 800 else 0.35), 1)
        return [sp]

    def tank(k):
        tp = copy.deepcopy(b["tank"][0])
        for q in tp["pts"]:
            if q[1] > 1000:
                q[1] = round(1000 + (q[1] - 1000) * k, 1)
        return [tp]
    board = next(x for x in m["front_end"] if x.get("part") == "head")
    plain = {k_: v for k_, v in board.items() if k_ != "part"}

    def lens(d0, d1):
        return {"pts": [along(d0, 90, 1), along(d1, 94, 1), along(d1, 80, 1), along(d0, 76, 1)], "cls": "lens", "t": 0.3}
    mask = {"pts": [along(YK + 2, 60, 1), along(YK + 90, 104), along(L + 96, 92, 1), along(L + 102, 48, 1), along(YK + 6, 44, 1)], "cls": "body", "t": 0.5}

    def cans(scale, dw):
        ex = copy.deepcopy(m["exhaust"])
        c = ex["cans"][-1]
        c["b"] = [round(c["a"][0] + (c["b"][0] - c["a"][0]) * scale, 1), round(c["a"][1] + (c["b"][1] - c["a"][1]) * scale, 1)]
        c["w"] += dw
        return ex
    short, _ = retail(b["tail"], end_dx=90, x0=150)
    long_, f = retail(b["tail"], end_dx=-50, x0=150)
    tip = min((q for q in long_[0]["pts"]), key=lambda q: q[0])
    own = OWN[m["brand"]]
    m["kit"] = {
        "base": {"tank_seat": "Standard", "tail": "Standard", "front": "Number board", "exhaust": "Standard", "paint": PAINT[own][0]},
        "tank_seat": [{"name": "Tall seat", "note": "taller, firmer seat for standing up and long legs", "tank": tank(1.0), "seat": seat(26)},
                      {"name": "Desert tank", "note": "bigger tank standing above the shrouds, with a scooped seat", "tank": tank(1.7), "seat": seat(-16)}],
        "tail": [{"name": "Cut short", "note": "rear mudguard cut short", "tail": short},
                 {"name": "Enduro", "note": "longer mudguard with a tail light", "tail": long_ + [{"pts": [[tip[0] + 6, tip[1] - 30, 1], [tip[0] - 16, tip[1] - 24, 1], [tip[0] - 10, tip[1] - 2, 1], [tip[0] + 12, tip[1] - 8, 1]], "cls": "redlens", "t": 0.3}]}],
        "front": [{"name": "Enduro lamp", "note": "a small headlamp set in the number board", "head": [plain, lens(YK + 70, YK + 150)]},
                  {"name": "Rally mask", "note": "a taller painted mask with two lamps", "head": [mask, lens(YK + 60, YK + 130), lens(YK + 150, YK + 220)]}],
        "exhaust": [{"name": "Short race can", "note": "shorter, fatter silencer", "exhaust": cans(0.66, 12)},
                    {"name": "Long quiet can", "note": "longer silencer for trail riding", "exhaust": cans(1.3, -6)}],
        "paint": [{"name": nm, "note": note, "colours": col} for i, (nm, note, col) in enumerate(PAINT) if i != own],
        "extras": [{"name": "Hand guards", "note": "plastic guards over the levers",
                    "add": {"panels": [{"pts": [[tx - 112, tz + 122, 1], [tx - 44, tz + 128], [tx - 26, tz + 96], [tx - 40, tz + 58, 1], [tx - 66, tz + 64], [tx - 60, tz + 100], [tx - 108, tz + 104, 1]], "cls": "body", "t": 0.6}]}},
                   {"name": "Bash plate", "note": "aluminium plate under the engine",
                    "add": {"panels": [{"pts": [[906, 420, 1], [884, 344], [760, 322], [540, 340, 1], [540, 354, 1], [756, 338], [868, 358], [890, 424, 1]], "cls": "chromef", "t": 0.5}]}}],
    }
    return m


MODELS = {"varga_mx": lambda: with_kit(varga), "kazeda_mx": lambda: with_kit(kazeda), "corvano_mx": lambda: with_kit(corvano)}

if __name__ == "__main__":
    for n in (sys.argv[1:] or MODELS):
        path = os.path.join(ROOT, "data", "models", n + ".json")
        json.dump(MODELS[n](), open(path, "w"), indent=1)
        open(path, "a").write("\n")
        print("drafted", path)
