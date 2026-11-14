"""Wave C lead models: the Corvano adventure bike and the Lodestar chopper.

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
    on_arm = any(s.get("mount") == "swingarm" for s in b.get("plate_hanger", []))
    for part in ("behind", "tail", "seat", "tank", "panels", "caps", "plate_hanger", "trim"):
        b[part] = [tf(s, arm if part == "plate_hanger" and on_arm else up) for s in b.get(part, [])]
    for s in b["panels"]:          # a hugger drawn round the rear wheel stays on the wheel
        if "arc" in s and abs(s["arc"]["c"][0]) < 1:
            s["arc"].update(c=[0, rear_r], r0=s["arc"]["r0"] + da, r1=s["arc"]["r1"] + da)
    m["deck"]["nose"] = list(up(*m["deck"]["nose"]))
    m["wheels"]["rear"]["tyre_r"] = rear_r


def corvano_adventure():
    """The adventure lead, drawn again (beenster, 14 Nov: start again from a big Italian V4 adventure tourer,
    reference only). Corvano's V-twin and trellis stood up on longer suspension with a 19 inch cast front
    wheel; a pointed nose fairing with a slanted lamp and a tall screen; a big humped tank whose side panels
    sweep down past the radiator; a stepped seat, a short high tail with a rack, a sump guard, hand guards
    and a short low silencer on the swingarm side."""
    base = load("corvano_naked")
    m = copy.deepcopy(base)
    m.pop("kit", None)
    m.update(name="Corvano adventure (working title A1)", family="Adventure", rev="B")
    DZ, FR, RR, WB = 50, 330, 320, 1560
    g0, r0 = base["geometry"], base["wheels"]["front"]["tyre_r"]
    raise_bike(m, DZ, RR)
    m["geometry"] = dict(g0, wheelbase=WB, rake=24.5, trail=105, seat_height=g0["seat_height"] + DZ, hip_x=520, ground_clearance=g0["ground_clearance"] + DZ)
    m["fork"] = dict(base["fork"], length=790, lower_yoke=610, slider_top=400, usd=True)
    m["wheels"]["front"].update(tyre_r=FR, rim_r=241, tread="touring")
    m["wheels"]["rear"].update(tread="touring")
    a = fork_top(g0, r0, base["fork"]["length"])
    tx, tz = fork_top(m["geometry"], FR, m["fork"]["length"])
    dx, dz = tx - a[0], tz - a[1]
    fe = [move(s, dx - 10, dz + 20) for s in base["front_end"] if s.get("part") != "head"]
    grip = next(s for s in fe if s.get("circle") == 20)["at"]
    fe.append({"pts": [[grip[0] + 54, grip[1] + 20, 1], [grip[0] + 70, grip[1] - 28, 1], [grip[0] + 22, grip[1] - 46, 1], [grip[0] + 30, grip[1] - 8, 1]], "cls": "plastic", "t": 0.5})

    def h(pts, cls, t=0.5):
        return {"pts": [[round(tx + p[0], 1), round(tz + p[1], 1)] + list(p[2:]) for p in pts], "cls": cls, "t": t, "part": "head"}
    fe = [h([[-46, 44, 1], [70, 34], [150, -18], [236, -92, 1], [218, -112, 1], [124, -106], [62, -150, 1], [-12, -142, 1], [-52, -40]], "body", 0.6),
          h([[104, -18, 1], [168, -60], [152, -84, 1], [86, -56, 1]], "lens", 0.3),
          h([[-12, 60, 1], [34, 50, 1], [46, 112, 1], [2, 120, 1]], "plastic", 0),
          h([[80, 30, 1], [6, 246, 1], [-10, 240, 1], [52, 22, 1]], "screen", 0)] + fe
    m["front_end"] = fe
    m["front_caliper"] = [[p[0] + WB - g0["wheelbase"], p[1] + FR - r0] + p[2:] for p in base["front_caliper"]]
    b = m["body"]
    b["front_guard"] = [{"arc": {"c": [WB, FR], "r0": FR + 18, "r1": FR + 44, "a0": 70, "a1": 150}, "cls": "body"}] + \
                       [move(s, WB - g0["wheelbase"], FR - r0) for s in base["body"]["front_guard"] if s.get("shroud")]
    # the big tank: longer, with its hump pushed forward, and a side panel sweeping down past the radiator
    tk = b["tank"][0]["pts"]
    x0, zb = min(p[0] for p in tk), min(p[1] for p in tk)
    for s in b["tank"]:
        s["pts"] = [[round(x0 + (p[0] - x0) * 1.15, 1), round(zb + (p[1] - zb) * (1.0 + 0.14 * min(1.0, max(0.0, (p[0] - x0) / 300)) if p[1] > zb + 40 else 1.0), 1)] + p[2:] for p in s["pts"]]
    b["caps"] = [dict(c, at=[round(x0 + (c["at"][0] - x0) * 1.15, 1), round(zb + (c["at"][1] - zb) * 1.12, 1)]) for c in b.get("caps", [])]
    b["panels"] = b["panels"] + [
        {"pts": [[1180, 968, 1], [1240, 900], [1222, 790], [1150, 690], [1076, 640, 1], [996, 690, 1], [1008, 800], [1036, 930, 1]], "cls": "body", "t": 0.6},
        {"pts": [[1176, 866, 1], [1196, 800], [1136, 722, 1], [1094, 762, 1]], "cls": "plastic", "t": 0.5},
        {"pts": [[1014, 402, 1], [966, 296, 1], [724, 264, 1], [612, 292, 1], [700, 314, 1], [930, 342, 1]], "cls": "cast", "t": 0},
        {"tube": 12, "pts": [[340, 986], [150, 1016], [40, 1016]], "cls": "dark"},
        {"tube": 10, "pts": [[140, 1016], [190, 950]], "cls": "dark"}]
    hd = m["exhaust"]["headers"]
    m["exhaust"] = {"side": "left",
                    "headers": [dict(hd[0], pts=hd[0]["pts"][:9] + [[500, 400]]), dict(hd[1], pts=hd[1]["pts"][:3] + [[490, 470], [500, 400]])],
                    "cans": [{"a": [500, 400], "b": [236, 462], "w": 108, "front": True}]}
    b["overall"] = [-RR - 40, WB + FR + 10]
    m["frame_label"] = "steel trellis, engine as part of the frame, long-travel suspension"
    return m


def ring(c, r0, r1, a0, a1, step=10):
    """A mudguard arc as a closed outline (angles from +x, 90 = top, 180 = rear)."""
    ang = [math.radians(a) for a in range(a0, a1 + 1, step)]
    pts = [[round(c[0] + r_ * math.cos(a), 1), round(c[1] + r_ * math.sin(a), 1)] for r_, aa in ((r1, ang), (r0, ang[::-1])) for a in aa]
    n = len(ang)
    for i in (0, n - 1, n, 2 * n - 1):
        pts[i].append(1)
    return pts


def lodestar_chopper():
    """The chopper, drawn from scratch (beenster, 14 Nov: start again; choppers are bespoke and get special
    treatment; the reference is the classic long-fork film chopper, reference only). A rigid frame with no
    rear suspension: backbone, seat post, downtube and two stays each side running straight to the rear axle.
    A very long fork raked right out, a tall skinny front wheel and a fat rear, a small peanut tank laid on
    the steep backbone, tall pulled-back bars on risers, a stepped two-level seat running up a tall back rest
    and sissy bar, forward footrests, and slim pipes that run straight back and kick up behind the axle."""
    from cruiser_draft import lodestar as cruiser, FRONT_R as CF, REAR_R as CR
    c = cruiser()
    FR, RR, WB, L = 335, 330, 1880, 1000
    g = {"wheelbase": WB, "rake": 43.0, "trail": 230, "seat_height": 668, "hip_x": 430, "ground_clearance": 125}
    fork = {"length": L, "lower_yoke": 800, "offset": 40, "slider_top": 400, "usd": False}
    t = fork_top(g, FR, L)
    tx, tz = round(t[0], 1), round(t[1], 1)
    rk = math.radians(g["rake"])
    yoke = (WB - 800 * math.sin(rk), FR + 800 * math.cos(rk))
    # a small lamp carried low on the bottom yoke
    ct = fork_top(c["geometry"], CF, c["fork"]["length"])
    lamp = [s for s in c["front_end"] if s.get("part") == "head" and "pts" in s]
    lp = [p for s in lamp for p in s["pts"]]
    lc = (sum(p[0] for p in lp) / len(lp), sum(p[1] for p in lp) / len(lp))
    target = (yoke[0] + 92, yoke[1] + 6)
    head = [dict(s, pts=[[round(target[0] + (p[0] - lc[0]) * 0.8, 1), round(target[1] + (p[1] - lc[1]) * 0.8, 1)] + p[2:] for p in s["pts"]]) for s in copy.deepcopy(lamp)]
    head.append({"circle": 30, "at": [tx + 34, tz + 26], "cls": "plastic", "part": "head"})
    bars = [{"tube": 26, "pts": [[tx, tz], [tx - 34, tz + 104]], "cls": "steelt"},
            {"tube": 24, "pts": [[tx - 34, tz + 104], [tx - 74, tz + 124], [tx - 150, tz + 84]], "cls": "steelt", "t": 0.5},
            {"tube": 9, "pts": [[tx - 146, tz + 70], [tx - 86, tz + 78]], "cls": "dark"},
            {"circle": 20, "at": [tx - 164, tz + 78], "cls": "cast"},
            {"tube": 8, "pts": [[tx - 120, tz + 100], [tx - 124, tz + 164]], "cls": "dark"},
            {"pts": [[tx - 149, tz + 169], [tx - 124, tz + 194], [tx - 94, tz + 179], [tx - 114, tz + 154]], "cls": "chromef", "mirror": True}]
    # the engine sits a little further back than in the cruiser, with shorter barrels under the steep backbone
    DX = -30

    def back(x, z):
        return x + DX, z
    e = copy.deepcopy(c["engine"])
    e["crank"], e["countershaft"] = list(back(*e["crank"])), list(back(*e["countershaft"]))
    e["case"] = [list(back(p[0], p[1])) + p[2:] for p in e["case"]]
    e["covers"] = [tf(k, back) for k in e["covers"]]
    for cy in e["cylinders"]:
        cy["length"] = 178
    x = c["engine"]["extras"]
    e["extras"] = [tf(dict(x[0], pts=[[p[0], p[1] - 22] + p[2:] for p in x[0]["pts"]]), back),
                   {"pts": [[560, 610, 1], [470, 620], [410, 596], [396, 520, 1], [430, 452], [520, 440, 1], [566, 500, 1]], "cls": "chromef", "t": 0.8}]
    # the tank lies on the backbone: its outline is drawn along the tube and then tipped up with it
    B, slope = [470, 640], None
    O = (640.0, 0.0)
    ht = (tx - 22 * math.sin(rk) + 40 * math.cos(rk), tz - 22 * math.cos(rk) - 40 * math.sin(rk))
    slope = (ht[1] - B[1]) / (ht[0] - B[0])
    O = (640.0, B[1] + slope * (640 - B[0]))
    ang = math.atan(slope)
    eu, en = (math.cos(ang), math.sin(ang)), (-math.sin(ang), math.cos(ang))

    def on_tube(u, v, flag=None):
        return [round(O[0] + eu[0] * u + en[0] * v, 1), round(O[1] + eu[1] * u + en[1] * v, 1)] + ([1] if flag else [])
    # a long teardrop: full and high right behind the headstock, tapering back to a point at the seat (beenster, 14 Nov)
    U = math.hypot(ht[0] - O[0], ht[1] - O[1]) - 46
    tank = [on_tube(4, 6, 1), on_tube(0.16 * U, 40), on_tube(0.4 * U, 86), on_tube(0.64 * U, 124), on_tube(0.83 * U, 134), on_tube(0.95 * U, 110),
            on_tube(U, 54, 1), on_tube(U - 8, -26, 1), on_tube(0.5 * U, -30), on_tube(20, -16, 1)]
    seat = [[668, 762, 1], [600, 706], [500, 676], [404, 682], [336, 716], [304, 784, 1], [220, 802], [90, 798], [22, 784, 1],
            [-34, 700, 1], [60, 694], [200, 642], [290, 612], [400, 620], [560, 632], [662, 700, 1]]
    rest = [[30, 790, 1], [-28, 960], [-80, 1152, 1], [-120, 1140, 1], [-76, 950], [-44, 770], [-34, 720, 1]]
    m = {
        "name": "Lodestar chopper (working title H1)", "brand": "lodestar", "family": "Chopper", "rev": "B",
        "engine_label": "45 degree V-twin", "frame_label": "rigid steel frame, no rear suspension",
        "colours": {"body": "#23408E", "frame": "#B9BDC2", "rim": "#C7CBD0", "fork": "#D5D8DC", "accent": "#D8C9A3", "seat": "#1c1c1e"},
        "deck": {"nose": [640, round(O[1], 1)], "tank_pitch": round(math.degrees(ang), 1), "seat_pitch": 0},
        "geometry": g,
        "wheels": {"front": {"tyre_r": FR, "rim_r": 282, "spokes": 40, "wire": True, "disc": 120, "phase": 0, "tread": "cruiser"},
                   "rear": {"tyre_r": RR, "rim_r": 200, "spokes": 40, "wire": True, "disc": 125, "phase": 15, "tread": "cruiser"}},
        "fork": fork,
        "front_caliper": [[p[0] + WB - c["geometry"]["wheelbase"], p[1] + FR - CF] + p[2:] for p in c["front_caliper"]],
        "engine": e,
        "exhaust": {"headers": [{"pts": [[986, 620], [1012, 540], [1006, 410], [970, 300], [890, 258], [600, 250], [60, 258], [-50, 270]], "w": 46, "front": True},
                                {"pts": [[620, 620], [556, 580], [504, 470], [478, 380], [440, 344], [60, 344], [-30, 352]], "w": 46, "front": True}],
                    "cans": [{"a": [-50, 270], "b": [-200, 384], "w": 56, "front": True}, {"a": [-30, 352], "b": [-176, 470], "w": 56, "front": True}]},
        # no swingarm: only the axle plate, which the four stays run into
        "swingarm": {"pivot": [480, 300], "sprocket_r": 95, "rigid": True, "cls": "chromef",
                     "outline": [[70, RR + 46, 1], [-44, RR + 30, 1], [-58, RR - 30, 1], [60, RR - 46, 1], [110, RR, 1]], "holes": []},
        "frame": {"nodes": {"B": B, "P": [500, 226], "D2": [1000, 226], "AX": [30, RR], "N": [600, 640 + slope * 130]},
                  "tubes": [{"path": ["B", "AX"], "w": 26}, {"path": ["P", "AX"], "w": 26},
                            {"path": ["head_t", "B", "P"], "w": 36, "t": 0.3},
                            {"path": ["head_b", "D2", "P"], "w": 32, "t": 0.4},
                            {"path": ["head_b", "N"], "w": 22, "t": 0}], "plates": [], "sub_y": 96},
        "front_end": head + bars,
        "hangers": [{"tube": 16, "pts": [[1004, 300], [1078, 332]], "cls": "steelt"}, {"circle": 17, "at": [1082, 334], "cls": "bolt"},
                    {"tube": 10, "pts": [[1082, 334], [1114, 388]], "cls": "dark"},
                    {"tube": 16, "pts": [[700, 222], [490, 166]], "cls": "dark"}],
        "rear_caliper": {k: [[p[0], p[1] + RR - CR] + p[2:] for p in v] for k, v in c["rear_caliper"].items()},
        "fit": {"plate_hanger": False},
        "body": {
            "overall": [-RR - 40, WB + FR + 20],
            "behind": [],
            "front_guard": [{"arc": {"c": [WB, FR], "r0": FR + 10, "r1": FR + 22, "a0": 78, "a1": 122}, "cls": "chromef"}],
            "tail": [{"pts": ring([0, RR], RR + 12, RR + 32, 50, 160), "cls": "body", "t": 0.9},
                     {"pts": [[-318, 470, 1], [-342, 482, 1], [-330, 516, 1], [-306, 504, 1]], "cls": "redlens", "t": 0.3}],
            "seat": [{"pts": seat, "cls": "seat"},
                     {"pts": [[590, 692], [500, 664], [404, 670], [344, 700]], "cls": "stitch", "closed": False},
                     {"pts": [[14, 800], [-34, 960], [-84, 1130]], "cls": "stitch", "closed": False}],
            "tank": [{"pts": tank, "cls": "body"},
                     {"pts": [on_tube(0.45 * U, 76), on_tube(0.64 * U, 104), on_tube(0.82 * U, 112), on_tube(0.93 * U, 92)], "cls": "hl", "closed": False}],
            "panels": [{"pts": rest, "cls": "seat", "t": 0.8}, {"tube": 12, "pts": [[-6, RR + 30], [-56, 700]], "cls": "steelt", "front": True}, {"tube": 12, "pts": [[-56, 700], [-128, 1150]], "cls": "steelt", "front": True},
                       {"tube": 12, "pts": [[-128, 1150], [-166, 1336]], "cls": "steelt", "front": True}, {"tube": 12, "pts": [[-166, 1336], [-140, 1352]], "cls": "steelt", "front": True}],
            "caps": [{"at": on_tube(0.76 * U, 130), "r": 13, "cls": "chromef"}],
            "plate_hanger": []},
    }
    return m


MODELS = {"corvano_adventure": corvano_adventure, "lodestar_chopper": lodestar_chopper}

if __name__ == "__main__":
    for n in (sys.argv[1:] or MODELS):
        path = os.path.join(ROOT, "data", "models", n + ".json")
        json.dump(MODELS[n](), open(path, "w"), indent=1)
        open(path, "a").write("\n")
        print("drafted", path)
