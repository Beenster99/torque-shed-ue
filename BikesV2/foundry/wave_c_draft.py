"""Wave C lead models: the Varga adventure bike and the Lodestar chopper.

The adventure bike stands the Varga naked's parallel twin and trellis on long-travel suspension:
everything sprung rises together, the swingarm drops to a bigger rear wheel, and a long fork carries
a 21 inch front wheel under a high beak, a tall screen and a bigger tank with shrouds down to the
radiator. The chopper is the Lodestar cruiser's V-twin and cradle behind a long raked fork, with a
small peanut tank high on a bare backbone, tall bars, a sissy bar and kicked-up pipes.

    python3 foundry/wave_c_draft.py            # (re)write both models
"""
import copy
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sport_draft import ROOT, load  # noqa: E402
from classic_draft import move, fork_top  # noqa: E402


def tf(o, f):
    """A copy of a shape (or can, cover, shock) with every point passed through f(x, z)."""
    o = copy.deepcopy(o)

    def pt(p):
        x, z = f(p[0], p[1])
        return [round(x, 1), round(z, 1)] + list(p[2:])
    for k in ("pts",):
        if k in o:
            o[k] = [pt(p) for p in o[k]]
    for k in ("at", "a", "b", "c", "top", "bottom"):
        if k in o and isinstance(o[k], list):
            o[k] = pt(o[k])
    if "arc" in o:
        o["arc"]["c"] = pt(o["arc"]["c"])
    return o


def raise_bike(m, dz, rear_r):
    """Lift everything sprung by dz and fit a rear wheel of radius rear_r: the swingarm, shock foot
    and rear caliper follow the axle, blending along the arm from the pivot."""
    r0 = m["wheels"]["rear"]["tyre_r"]
    da, px = rear_r - r0, m["swingarm"]["pivot"][0]

    def up(x, z):
        return x, z + dz

    def arm(x, z):
        return x, z + da + (dz - da) * min(1.0, max(0.0, x / px))
    e = m["engine"]
    e["crank"], e["countershaft"] = list(up(*e["crank"])), list(up(*e["countershaft"]))
    e["case"] = [list(up(p[0], p[1])) + p[2:] for p in e["case"]]
    e["covers"] = [tf(c, up) for c in e["covers"]]
    e["extras"] = [tf(s, up) for s in e["extras"]]
    if m.get("radiator"):
        m["radiator"] = tf(m["radiator"], up)
    ex = m["exhaust"]
    ex["headers"] = [tf(h, up) for h in ex["headers"]]
    ex["cans"] = [tf(c, up) for c in ex.get("cans", [])]
    sw = m["swingarm"]
    sw["pivot"] = list(up(*sw["pivot"]))
    sw["outline"] = [list(arm(p[0], p[1])) + p[2:] for p in sw["outline"]]
    sw["holes"] = [[list(arm(p[0], p[1])) + p[2:] for p in h] for h in sw.get("holes", [])]
    m["shock"] = dict(m["shock"], top=list(up(*m["shock"]["top"])), bottom=list(arm(*m["shock"]["bottom"])))
    fr = m["frame"]
    fr["nodes"] = {k: list(up(*v)) for k, v in fr["nodes"].items()}
    fr["plates"] = [tf(p, up) for p in fr.get("plates", [])]
    m["hangers"] = [tf(s, up) for s in m["hangers"]]
    m["rear_caliper"] = {k: [[p[0], p[1] + da] + p[2:] for p in v] for k, v in m["rear_caliper"].items()}
    b = m["body"]
    for part in ("behind", "tail", "seat", "tank", "panels", "caps", "plate_hanger", "trim"):
        b[part] = [tf(s, up) for s in b.get(part, [])]
    m["deck"]["nose"] = list(up(*m["deck"]["nose"]))
    m["wheels"]["rear"]["tyre_r"] = rear_r


def varga_adventure():
    base = load("varga_naked")
    m = copy.deepcopy(base)
    m.pop("kit", None)
    m.update(name="Varga adventure (working title A1)", family="Adventure", rev="A")
    DZ, FR, RR, WB = 45, 345, 335, 1540
    g0 = base["geometry"]
    raise_bike(m, DZ, RR)
    m["geometry"] = dict(g0, wheelbase=WB, rake=26.0, trail=112, seat_height=g0["seat_height"] + DZ, ground_clearance=g0["ground_clearance"] + DZ)
    m["fork"] = dict(base["fork"], length=800, lower_yoke=620, slider_top=410, usd=True)
    m["wheels"]["front"].update(tyre_r=FR, rim_r=272, wire=True, spokes=36, tread="knobbly")
    m["wheels"]["rear"].update(rim_r=232, wire=True, spokes=36, tread="knobbly")
    m["colours"] = dict(m["colours"], rim="#C9A227")
    a = fork_top(g0, base["wheels"]["front"]["tyre_r"], base["fork"]["length"])
    t = fork_top(m["geometry"], FR, m["fork"]["length"])
    dx, dz = t[0] - a[0], t[1] - a[1]
    # the lamp sits high in a nose fairing, clear of the tyre with the long fork fully compressed;
    # the clocks ride behind the screen, the bars a little higher than the naked's
    fe = []
    for s in base["front_end"]:
        if s.get("part") == "head":
            lens_or_shell = "pts" in s and min(p[1] for p in s["pts"]) < a[1] - 20
            fe.append(move(s, dx - 10, dz + (75 if lens_or_shell else 45)))
        else:
            fe.append(move(s, dx - 8, dz + 22))
    grip = next(s for s in fe if s.get("circle") == 20)["at"]
    fe.append({"pts": [[grip[0] + 52, grip[1] + 20, 1], [grip[0] + 66, grip[1] - 30, 1], [grip[0] + 22, grip[1] - 46, 1], [grip[0] + 30, grip[1] - 8, 1]], "cls": "plastic", "t": 0.5})
    tx, tz = t
    fe.append({"pts": [[tx + 78, tz + 42, 1], [tx + 10, tz + 232, 1], [tx - 4, tz + 226, 1], [tx + 54, tz + 30, 1]], "cls": "screen", "t": 0, "part": "head"})
    m["front_end"] = fe
    m["front_caliper"] = [[p[0] + WB - g0["wheelbase"], p[1] + FR - base["wheels"]["front"]["tyre_r"]] + p[2:] for p in base["front_caliper"]]
    b = m["body"]
    # a short low guard on the wheel and the high beak under the lamp
    b["front_guard"] = [{"arc": {"c": [WB, FR], "r0": FR + 14, "r1": FR + 32, "a0": 84, "a1": 140}, "cls": "plastic"},
                        {"pts": [[tx + 36, tz - 136, 1], [tx + 112, tz - 132], [tx + 250, tz - 150, 1], [tx + 246, tz - 166, 1], [tx + 112, tz - 170], [tx + 48, tz - 184, 1]], "cls": "body", "t": 0.6}]
    # the bigger tank: longer and taller than the naked's, with a shroud down each side to the radiator
    tk = b["tank"][0]["pts"]
    x0 = min(p[0] for p in tk)
    zb = min(p[1] for p in tk)
    for s in b["tank"]:
        s["pts"] = [[round(x0 + (p[0] - x0) * 1.13, 1), round(zb + (p[1] - zb) * (1.1 if p[1] > zb + 40 else 1.0), 1)] + p[2:] for p in s["pts"]]
    b["caps"] = [dict(c, at=[round(x0 + (c["at"][0] - x0) * 1.13, 1), round(zb + (c["at"][1] - zb) * 1.1, 1)]) for c in b.get("caps", [])]
    b["panels"] = b.get("panels", []) + [
        {"pts": [[1168, 938, 1], [1120, 870], [1092, 760], [1060, 690, 1], [985, 716, 1], [1000, 840], [1010, 930, 1]], "cls": "body", "t": 0.6},
        {"pts": [[1010, 392, 1], [968, 300, 1], [720, 272, 1], [620, 300, 1], [700, 318, 1], [930, 346, 1]], "cls": "cast", "t": 0},
        {"tube": 12, "pts": [[330, 930], [160, 990], [60, 990]], "cls": "dark", "front": True},
        {"tube": 10, "pts": [[150, 990], [200, 920]], "cls": "dark", "front": True}]
    m["deck"]["tank_pitch"] = 0
    b["overall"] = [-RR - 40, WB + FR + 10]
    m["engine_label"], m["frame_label"] = "parallel twin", "bright steel trellis, long-travel suspension"
    return m


def lodestar_chopper():
    from cruiser_draft import lodestar as cruiser, FRONT_R, REAR_R
    c = cruiser()
    m = copy.deepcopy(c)
    m.update(name="Lodestar chopper (working title H1)", family="Chopper", rev="A")
    WB = 1800
    g = dict(c["geometry"], wheelbase=WB, rake=40.0, trail=190)
    m["geometry"] = g
    m["fork"] = {"length": 880, "lower_yoke": 650, "offset": 40, "slider_top": 360, "usd": False}
    a = fork_top(c["geometry"], FRONT_R, c["fork"]["length"])
    t = fork_top(g, FRONT_R, m["fork"]["length"])
    dx, dz = t[0] - a[0], t[1] - a[1]
    tx, tz = round(t[0], 1), round(t[1], 1)
    head = [move(s, dx, dz) for s in c["front_end"] if s.get("part") == "head"]
    # tall pull-back bars
    bars = [{"tube": 30, "pts": [[tx, tz], [tx - 16, tz + 60]], "cls": "cast"},
            {"tube": 24, "pts": [[tx - 16, tz + 60], [tx - 20, tz + 250], [tx - 70, tz + 286], [tx - 170, tz + 262]], "cls": "steelt", "t": 0.5},
            {"tube": 9, "pts": [[tx - 164, tz + 248], [tx - 102, tz + 244]], "cls": "dark"},
            {"circle": 20, "at": [tx - 184, tz + 258], "cls": "cast"},
            {"tube": 8, "pts": [[tx - 150, tz + 270], [tx - 156, tz + 340]], "cls": "dark"},
            {"pts": [[tx - 181, tz + 345], [tx - 156, tz + 370], [tx - 126, tz + 355], [tx - 146, tz + 330]], "cls": "plastic", "mirror": True}]
    m["front_end"] = head + bars
    m["wheels"]["front"].update(rim_r=268, spokes=40)        # a tall skinny 21 inch front
    m["wheels"]["rear"].update(rim_r=196)                    # a fat rear tyre on a small rim
    m["front_caliper"] = [[p[0] + WB - c["geometry"]["wheelbase"]] + p[1:] for p in c["front_caliper"]]
    x = m["engine"]["extras"]
    m["engine"]["extras"] = [x[0], x[3], x[4]]
    m["exhaust"] = {"headers": c["exhaust"]["headers"],
                    "cans": [{"a": [500, 258], "b": [-130, 338], "w": 76, "front": True}, {"a": [440, 348], "b": [-160, 452], "w": 76, "front": True}]}
    m["deck"] = {"nose": [600, 736], "tank_pitch": 12, "seat_pitch": 0}
    b = m["body"]
    b["front_guard"] = [{"arc": {"c": [WB, FRONT_R], "r0": FRONT_R + 12, "r1": FRONT_R + 26, "a0": 72, "a1": 118}, "cls": "body"}]
    # a small peanut tank high on the backbone, which runs bare from the tank to the headstock
    b["tank"] = [{"pts": [[926, 880, 1], [890, 930], [814, 944], [728, 918], [660, 860], [620, 800, 1], [606, 740, 1], [912, 804, 1], [936, 838]], "cls": "body"},
                 {"pts": [[878, 904], [814, 916], [738, 894]], "cls": "hl", "closed": False}]
    b["caps"] = [{"at": [820, 942], "r": 14, "cls": "chromef"}]
    b["panels"] = b["panels"] + [{"tube": 12, "pts": [[190, 640], [140, 932]], "cls": "chromef", "front": True}, {"tube": 12, "pts": [[96, 668], [112, 934]], "cls": "chromef", "front": True},
                                 {"tube": 12, "pts": [[140, 932], [112, 934]], "cls": "chromef", "front": True},
                                 {"pts": [[166, 900, 1], [106, 906, 1], [100, 800, 1], [156, 794, 1]], "cls": "seat", "t": 0.5}]
    b["overall"] = [-REAR_R - 40, WB + FRONT_R + 40]
    m["colours"] = dict(m["colours"], body="#0F5A5E")
    m["frame_label"] = "stretched steel double cradle"
    return m


MODELS = {"varga_adventure": varga_adventure, "lodestar_chopper": lodestar_chopper}

if __name__ == "__main__":
    for n in (sys.argv[1:] or MODELS):
        path = os.path.join(ROOT, "data", "models", n + ".json")
        json.dump(MODELS[n](), open(path, "w"), indent=1)
        open(path, "a").write("\n")
        print("drafted", path)
