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
    if "boxer" in e:
        e["boxer"] = dict(e["boxer"], at=list(up(*e["boxer"]["at"])), cover=[list(up(q[0], q[1])) + q[2:] for q in e["boxer"]["cover"]])
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


def adventure(brand="corvano", wire=False):
    """The adventure lead, drawn again (beenster, 14 Nov: start again from a big Italian V4 adventure tourer,
    reference only). Corvano's V-twin and trellis stood up on longer suspension with a 19 inch cast front
    wheel. Shapes from beenster's overdrawing: a long distinctive beak under a tall screen, a three-quarter
    fairing, a long flatter tank, a long rear section comfortable for two with a rack, hand guards and one
    large silencer swept up the swingarm side.

    The other brands' adventure bikes are first drafts by rule: the brand's own engine, frame, wheels, bars
    and colours under the same approved nose, fairing, tank, seat and tail (not styled per brand yet)."""
    base = load(brand + "_naked")
    m = copy.deepcopy(base)
    m.pop("kit", None)
    m.update(name=brand.title() + " adventure (working title A1)", family="Adventure", rev="D" if brand == "corvano" else "A")
    FR, RR, WB = 330, 320, 1560
    g0, r0 = base["geometry"], base["wheels"]["front"]["tyre_r"]
    DZ = max(922 - base["deck"]["nose"][1], 200 - g0["ground_clearance"])     # the deck comes up to the family's line, the sump to its clearance
    raise_bike(m, DZ, RR)
    m["geometry"] = dict(g0, wheelbase=WB, rake=24.5, trail=105, seat_height=870, hip_x=520, ground_clearance=g0["ground_clearance"] + DZ)
    m["deck"] = {"nose": [690, 922], "tank_pitch": 3, "seat_pitch": 3.5}      # one shared deck: the tank, seat and tail are the same on every brand
    m["fork"] = dict(base["fork"], length=790, lower_yoke=610, slider_top=400, usd=True)
    m["wheels"]["front"].update(tyre_r=FR, rim_r=241, tread="touring")
    m["wheels"]["rear"].update(tread="touring")
    if wire:
        for w in m["wheels"].values():
            w.update(wire=True, spokes=36)
    a = fork_top(g0, r0, base["fork"]["length"])
    tx, tz = fork_top(m["geometry"], FR, m["fork"]["length"])
    dx, dz = tx - a[0], tz - a[1]
    fe = [move(s, dx - 10, dz + 20) for s in base["front_end"] if s.get("part") != "head"]
    grip = next(s for s in fe if s.get("circle") == 20)["at"]
    fe.append({"pts": [[grip[0] + 54, grip[1] + 20, 1], [grip[0] + 70, grip[1] - 28, 1], [grip[0] + 22, grip[1] - 46, 1], [grip[0] + 30, grip[1] - 8, 1]], "cls": "plastic", "t": 0.5})

    def h(pts, cls, t=0.5):
        return {"pts": pts, "cls": cls, "t": t, "part": "head"}
    # the lamp, clocks and tall screen; the beak itself is one piece with the fairing (below)
    fe = [h([[1222, 1118, 1], [1268, 1110, 1], [1280, 1166, 1], [1236, 1174, 1]], "plastic", 0),
          h([[1436, 1090, 1], [1206, 1350, 1], [1226, 1310, 1], [1306, 1140, 1]], "screen", 0)] + fe
    m["front_end"] = fe
    m["front_caliper"] = [[p[0] + WB - g0["wheelbase"], p[1] + FR - r0] + p[2:] for p in base["front_caliper"]]
    b = m["body"]
    b["front_guard"] = [{"arc": {"c": [WB, FR], "r0": FR + 18, "r1": FR + 44, "a0": 70, "a1": 150}, "cls": "body"}] + \
                       [move(s, WB - g0["wheelbase"], FR - r0) for s in base["body"]["front_guard"] if s.get("shroud")]
    # a long, flatter tank falling to the seat
    b["tank"] = [{"pts": [[1172, 1010, 1], [1136, 1076], [1050, 1104], [940, 1100], [830, 1066], [740, 1000], [690, 950, 1], [690, 902, 1], [1166, 918, 1], [1188, 960]], "cls": "body"},
                 {"pts": [[1110, 1052], [1040, 1076], [940, 1072], [850, 1044]], "cls": "hl", "closed": False}]
    b["caps"] = [{"at": [1000, 1100], "r": 15, "cls": "plastic"}]
    # the long rear: a stepped seat for two on a tail that runs back past the rear axle to a kicked-up tip
    b["seat"] = [{"pts": [[718, 962, 1], [640, 914], [560, 892], [470, 892], [400, 908], [350, 942], [300, 964], [180, 982], [40, 998], [-70, 1006, 1],
                          [-60, 960, 1], [60, 950], [200, 930], [330, 900], [440, 852], [560, 842], [704, 884, 1]], "cls": "seat"},
                 {"pts": [[640, 902], [560, 880], [470, 880], [408, 894]], "cls": "stitch", "closed": False},
                 {"pts": [[340, 934], [300, 952], [180, 970], [40, 986]], "cls": "stitch", "closed": False}]
    b["tail"] = [{"pts": [[470, 862, 1], [330, 904], [200, 934], [60, 954], [-60, 966], [-182, 1042, 1], [-192, 1012, 1], [-110, 930], [20, 890], [200, 850], [340, 800], [470, 790, 1]], "cls": "body", "t": 0.7},
                 {"pts": [[-176, 1034, 1], [-196, 1024, 1], [-186, 1004, 1], [-166, 1012, 1]], "cls": "redlens", "t": 0}]
    b["behind"] = [{"pts": [[470, 790, 1], [340, 800], [200, 850], [20, 890], [-110, 930, 1], [-100, 900, 1], [30, 858], [200, 814], [340, 768], [466, 762, 1]], "cls": "plastic fender", "t": 0.8, "slim": True}]
    n = m["frame"]["nodes"]
    n.update({k: v for k, v in (("S2", [-50, 950]), ("S4", [60, 900])) if k in n})
    b["plate_hanger"] = [{"tube": 18, "pts": [[-120, 936], [-236, 800], [-286, 700]], "cls": "plastic", "t": 0.6, "front": True},
                         {"pts": [[-278, 724, 1], [-294, 732, 1], [-330, 642, 1], [-314, 634, 1]], "cls": "plate", "t": 0},
                         {"pts": [[-272, 742, 1], [-262, 736, 1], [-280, 700, 1], [-290, 706, 1]], "cls": "redlens", "t": 0}]
    # beak and three-quarter fairing are ONE continuous piece (beenster's second overdrawing, 14 Nov): from the
    # beak tip the lower edge sweeps back and down in a single line past the radiator to a point low beside
    # the engine; the top steps up to the screen; the rear edge is cut back round the front cylinder. The
    # beak's centre line stays 45 mm clear of the tyre with the fork fully compressed.
    b["panels"] = b["panels"] + [
        {"pts": [[1374, 1114, 1], [1250, 1124, 1], [1196, 1100], [1191, 1012, 1], [941, 972, 1], [888, 762, 1], [993, 669], [947, 577, 1], [993, 524],
                 [1059, 419], [1020, 340, 1], [1086, 432], [1151, 538], [1178, 617], [1204, 690], [1270, 790], [1323, 860], [1402, 884], [1520, 888],
                 [1575, 878, 1], [1570, 894, 1], [1504, 975, 1], [1494, 990, 1], [1420, 1090, 1], [1410, 1104, 1]], "cls": "body", "t": 0.35, "fairing": True},
        {"pts": [[1420, 1040, 1], [1476, 972], [1456, 952, 1], [1392, 1008, 1]], "cls": "lens", "t": 0.3},
        {"pts": [[1120, 800, 1], [1140, 720], [1090, 640, 1], [1050, 690, 1]], "cls": "plastic", "t": 0.5},
        {"tube": 12, "pts": [[260, 992], [60, 1032], [-150, 1056]], "cls": "dark"},
        {"tube": 10, "pts": [[40, 1034], [70, 990]], "cls": "dark"}, {"tube": 10, "pts": [[-120, 1052], [-100, 1000]], "cls": "dark"}]
    # one large silencer, swept up along the swingarm side
    hd = m["exhaust"]["headers"]
    if brand == "corvano":
        heads = [dict(hd[0], pts=hd[0]["pts"][:8] + [[560, 372]]), dict(hd[1], pts=hd[1]["pts"][:3] + [[520, 480], [560, 372]])]
    else:               # each pipe keeps its own run down and under the engine, then turns up into the silencer
        heads = []
        for x in hd:
            k = next((i for i, q in enumerate(x["pts"]) if i > 2 and q[0] < 640), len(x["pts"]))
            heads.append(dict(x, pts=x["pts"][:k] + [[560, 372]]))
    m["exhaust"] = {"side": "left", "headers": heads, "cans": [{"a": [560, 372], "b": [50, 640], "w": 136, "front": True}]}
    b["overall"] = [-RR - 40, WB + FR + 10]
    m["frame_label"] = base["frame_label"].split(", engine as")[0] + ", long-travel suspension"
    return m


def ring(c, r0, r1, a0, a1, step=10):
    """A mudguard arc as a closed outline (angles from +x, 90 = top, 180 = rear)."""
    ang = [math.radians(a) for a in range(a0, a1 + 1, step)]
    pts = [[round(c[0] + r_ * math.cos(a), 1), round(c[1] + r_ * math.sin(a), 1)] for r_, aa in ((r1, ang), (r0, ang[::-1])) for a in aa]
    n = len(ang)
    for i in (0, n - 1, n, 2 * n - 1):
        pts[i].append(1)
    return pts


# ---- the chopper library: every tank, seat, sissy bar and pipe is drawn once and offered on all three bikes ----
def _teardrop(T, U):
    return [T(4, 6, 1), T(0.16 * U, 40), T(0.4 * U, 86), T(0.64 * U, 124), T(0.83 * U, 134), T(0.95 * U, 110), T(U, 54, 1), T(U - 8, -26, 1), T(0.5 * U, -30), T(20, -16, 1)]


def _bullet(T, U):
    V = 0.9 * U
    return [T(4, 6, 1), T(0.12 * V, 26), T(0.26 * V, 84), T(0.5 * V, 124), T(0.76 * V, 122), T(0.94 * V, 84), T(V, 30), T(V - 10, -26, 1), T(0.5 * V, -34), T(20, -16, 1)]


def _coffin(T, U):
    return [T(4, 6, 1), T(30, 76, 1), T(0.8 * U, 106, 1), T(U, 52, 1), T(U - 10, -26, 1), T(20, -16, 1)]


def _peanut(T, U):
    return [T(4, 6, 1), T(0.14 * U, 30), T(0.3 * U, 86), T(0.48 * U, 110), T(0.64 * U, 98), T(0.75 * U, 54), T(0.77 * U, 6), T(0.74 * U, -26, 1), T(0.4 * U, -32), T(20, -16, 1)]


UNDER = [[-34, 700, 1], [60, 694], [200, 642], [290, 612], [400, 620], [560, 632]]
KING = [[668, 762, 1], [600, 706], [500, 676], [404, 682], [336, 716], [304, 784, 1], [220, 802], [90, 798], [22, 784, 1]] + UNDER + [[662, 700, 1]]
SADDLE = [[670, 746, 1], [596, 702], [500, 680], [410, 682], [350, 698], [318, 722, 1], [296, 752, 1], [210, 764], [90, 760], [24, 744, 1]] + UNDER + [[658, 696, 1]]
BANANA = [[668, 742, 1], [600, 700], [500, 672], [400, 676], [300, 704], [190, 752], [90, 794], [36, 806], [4, 790, 1]] + UNDER + [[662, 700, 1]]
SOLO = [[668, 748, 1], [600, 704], [500, 678], [400, 682], [330, 708], [290, 728], [230, 706], [170, 690, 1], [176, 656, 1]] + UNDER[2:] + [[662, 700, 1]]
PAIRS = [("Teardrop and king seat", "long teardrop tank up to the headstock, stepped king and queen seat", _teardrop, KING),
         ("Bullet and saddle", "fat round tank, thin flat saddle with a pillion pad", _bullet, SADDLE),
         ("Coffin and banana", "flat-sided coffin tank, banana seat swept up the rear guard", _coffin, BANANA),
         ("Peanut and solo", "small tank set back with bare backbone to the headstock, solo saddle", _peanut, SOLO)]


def chop_tails(RR):
    guard = [{"pts": ring([0, RR], RR + 12, RR + 32, 50, 160), "cls": "body", "t": 0.9},
             {"pts": [[-318, 470, 1], [-342, 482, 1], [-330, 516, 1], [-306, 504, 1]], "cls": "redlens", "t": 0.3}]

    def bar(*pts, **kw):
        return dict({"tube": 12, "pts": [list(q) for q in pts], "cls": "steelt", "front": True}, **kw)
    tall = [bar((-6, RR + 30), (-56, 700)), bar((-56, 700), (-128, 1150)), bar((-128, 1150), (-166, 1336)), bar((-166, 1336), (-140, 1352))]
    return [("Tall sissy bar and back rest", "tall bar with a padded back rest",
             guard + [{"pts": [[30, 790, 1], [-28, 960], [-80, 1152, 1], [-120, 1140, 1], [-76, 950], [-44, 770], [-34, 720, 1]], "cls": "seat", "t": 0.8}] + tall),
            ("Short loop and pad", "short sissy loop with a small pad",
             guard + [{"pts": [[24, 746, 1], [-22, 850], [-52, 944, 1], [-88, 934, 1], [-66, 850], [-44, 770], [-34, 720, 1]], "cls": "seat", "t": 0.8},
                      bar((-6, RR + 30), (-56, 700)), bar((-56, 700), (-96, 960)), bar((-96, 960), (-74, 990), (-50, 960), t=0.5)]),
            ("Bare guard", "no sissy bar, just the guard and its stay", guard + [bar((-6, RR + 30), (-40, 700))]),
            ("Tall bare bar", "tall chrome bar with no back rest", guard + tall + [bar((-90, 916), (-40, 930), (-78, 990), t=0.5)])]


def low_end(h):
    """A header cut where its sweep down from the engine reaches the low run back."""
    k = next((i for i, q in enumerate(h["pts"]) if q[1] < 360 and q[0] < 700), len(h["pts"]) - 1)
    return dict(h, pts=copy.deepcopy(h["pts"][:k + 1]))


def chop_pipes(headers):
    hs = [low_end(h) for h in headers]

    def end(h):
        return list(h["pts"][-1][:2])

    def run(h, x, dz=0):
        return dict(h, pts=h["pts"] + [[x, end(h)[1] + dz]])
    up = [run(h, -50 + 20 * i, 8) for i, h in enumerate(hs)]
    mg = [run(h, 220 + 40 * i) for i, h in enumerate(hs)]
    return {"Upswept slim pipes": ("slim pipes that kick up behind the axle", {"headers": up, "cans": [{"a": end(h), "b": [end(h)[0] - 150, end(h)[1] + 114], "w": 56, "front": True} for h in up]}),
            "Megaphones": ("upswept megaphones", {"headers": mg, "cans": [{"a": end(h), "b": [-230 + 30 * i, end(h)[1] + 110], "w": 72, "front": True} for i, h in enumerate(mg)]}),
            "Drag pipes": ("short straight pipes cut off under the seat", {"headers": hs, "cans": [{"a": end(h), "b": [end(h)[0] - 420, end(h)[1] + 4], "w": 58, "front": True} for h in hs]}),
            "Fishtails": ("long low silencers with nipped ends", {"headers": hs, "cans": [{"a": end(h), "b": [-220 + 30 * i, end(h)[1] + 16], "w": 72, "front": True, "nip": True} for i, h in enumerate(hs)]})}


def bars_pull(tx, tz):
    return [{"tube": 26, "pts": [[tx, tz], [tx - 34, tz + 104]], "cls": "steelt"},
            {"tube": 24, "pts": [[tx - 34, tz + 104], [tx - 74, tz + 124], [tx - 150, tz + 84]], "cls": "steelt", "t": 0.5},
            {"tube": 9, "pts": [[tx - 146, tz + 70], [tx - 86, tz + 78]], "cls": "dark"},
            {"circle": 20, "at": [tx - 164, tz + 78], "cls": "cast"},
            {"tube": 8, "pts": [[tx - 120, tz + 100], [tx - 124, tz + 164]], "cls": "dark"},
            {"pts": [[tx - 149, tz + 169], [tx - 124, tz + 194], [tx - 94, tz + 179], [tx - 114, tz + 154]], "cls": "chromef", "mirror": True}]


def bars_ape(tx, tz):
    return [
            {"tube": 26, "pts": [[tx, tz], [tx - 30, tz + 60]], "cls": "steelt"},
            {"tube": 24, "pts": [[tx - 30, tz + 60], [tx - 70, tz + 212], [tx - 170, tz + 196]], "cls": "steelt", "t": 0.35},
            {"tube": 9, "pts": [[tx - 164, tz + 182], [tx - 102, tz + 190]], "cls": "dark"},
            {"circle": 20, "at": [tx - 184, tz + 192], "cls": "cast"},
            {"tube": 8, "pts": [[tx - 134, tz + 206], [tx - 138, tz + 226]], "cls": "dark"},
            {"circle": 34, "at": [tx - 140, tz + 240], "cls": "chromef", "mirror": True}]


def bars_drag(tx, tz):
    return [
            {"tube": 30, "pts": [[tx, tz], [tx - 96, tz + 92]], "cls": "steelt"},
            {"tube": 24, "pts": [[tx - 96, tz + 92], [tx - 150, tz + 84]], "cls": "steelt"},
            {"circle": 22, "at": [tx - 96, tz + 92], "cls": "bolt"},
            {"tube": 9, "pts": [[tx - 150, tz + 68], [tx - 90, tz + 74]], "cls": "dark"},
            {"circle": 20, "at": [tx - 164, tz + 82], "cls": "cast"},
            {"tube": 8, "pts": [[tx - 126, tz + 96], [tx - 132, tz + 150]], "cls": "dark"},
            {"pts": [[tx - 160, tz + 150, 1], [tx - 160, tz + 180, 1], [tx - 104, tz + 180, 1], [tx - 104, tz + 150, 1]], "cls": "chromef", "mirror": True}]


def bars_z(tx, tz):
    return [{"tube": 26, "pts": [[tx, tz], [tx - 26, tz + 50]], "cls": "steelt"},
            {"tube": 24, "pts": [[tx - 26, tz + 50], [tx - 60, tz + 150]], "cls": "steelt"},
            {"tube": 24, "pts": [[tx - 60, tz + 150], [tx - 160, tz + 136]], "cls": "steelt"},
            {"circle": 14, "at": [tx - 60, tz + 150], "cls": "steelt"},
            {"tube": 9, "pts": [[tx - 156, tz + 122], [tx - 96, tz + 130]], "cls": "dark"},
            {"circle": 20, "at": [tx - 174, tz + 134], "cls": "cast"},
            {"tube": 8, "pts": [[tx - 126, tz + 146], [tx - 130, tz + 196]], "cls": "dark"},
            {"pts": [[tx - 156, tz + 196, 1], [tx - 156, tz + 224, 1], [tx - 104, tz + 224, 1], [tx - 104, tz + 196, 1]], "cls": "chromef", "mirror": True}]


BARS = [("Pull-backs", "tall bars pulled back on risers", bars_pull), ("Ape hangers", "high ape hangers", bars_ape),
        ("Drag bars", "short flat bars on a tall riser", bars_drag), ("Z-bars", "angular bars with sharp corners", bars_z)]
PAINT = [("Midnight blue", "blue with a bright bare frame", {"body": "#23408E", "frame": "#B9BDC2"}),
         ("Oxblood", "deep red with a black frame", {"body": "#6B1A22", "frame": "#17181b"}),
         ("Burnt orange", "orange with a black frame", {"body": "#C8571B", "frame": "#15161a"}),
         ("Gloss black", "black with a bright bare frame", {"body": "#141518", "frame": "#B9BDC2"}),
         ("Gold", "gold with a black frame", {"body": "#B8892B", "frame": "#17181b"}),
         ("Sea green", "green with a cream frame", {"body": "#1F6F6B", "frame": "#D9D2BE"})]
CHOP_BASE = {"lodestar": (0, 0, "Upswept slim pipes", 0), "hartwell": (1, 1, "Megaphone each side", 1), "kazeda": (2, 2, "Stacked straight pipes", 2)}
CHOP = {"lodestar": (1880, 43.0, 1000, 800, 230), "hartwell": (1760, 38.0, 900, 720, 200), "kazeda": (1900, 45.0, 1030, 830, 250)}


def chopper(brand="lodestar"):
    """The chopper, drawn from scratch (beenster, 14 Nov: start again; choppers are bespoke and get special
    treatment; the reference is the classic long-fork film chopper, reference only). A rigid frame with no
    rear suspension: backbone, seat post, downtube and two stays each side running straight to the rear axle.
    A very long fork raked right out, a tall skinny front wheel and a fat rear, a small peanut tank laid on
    the steep backbone, tall pulled-back bars on risers, a stepped two-level seat running up a tall back rest
    and sissy bar, forward footrests, and slim pipes that run straight back and kick up behind the axle."""
    from cruiser_draft import lodestar as cruiser, FRONT_R as CF, REAR_R as CR
    c = cruiser()
    WB, RAKE, L, YK, TRAIL = CHOP[brand]
    FR, RR = 335, 330
    g = {"wheelbase": WB, "rake": RAKE, "trail": TRAIL, "seat_height": 668, "hip_x": 430, "ground_clearance": 125}
    fork = {"length": L, "lower_yoke": YK, "offset": 40, "slider_top": 400, "usd": False}
    t = fork_top(g, FR, L)
    tx, tz = round(t[0], 1), round(t[1], 1)
    rk = math.radians(g["rake"])
    yoke = (WB - YK * math.sin(rk), FR + YK * math.cos(rk))
    # a small lamp carried low on the bottom yoke
    ct = fork_top(c["geometry"], CF, c["fork"]["length"])
    lamp = [s for s in c["front_end"] if s.get("part") == "head" and "pts" in s]
    lp = [p for s in lamp for p in s["pts"]]
    lc = (sum(p[0] for p in lp) / len(lp), sum(p[1] for p in lp) / len(lp))
    target = (yoke[0] + 92, yoke[1] + 6)
    head = [dict(s, pts=[[round(target[0] + (p[0] - lc[0]) * 0.8, 1), round(target[1] + (p[1] - lc[1]) * 0.8, 1)] + p[2:] for p in s["pts"]]) for s in copy.deepcopy(lamp)]
    head.append({"circle": 30, "at": [tx + 34, tz + 26], "cls": "plastic", "part": "head"})
    bars = BARS[CHOP_BASE[brand][3]][2](tx, tz)
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
    tank, seat = _teardrop(on_tube, U), copy.deepcopy(KING)
    tails = chop_tails(RR)
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
            "tail": copy.deepcopy(tails[CHOP_BASE[brand][1]][2]),
            "seat": [{"pts": seat, "cls": "seat"},
                     {"pts": [[590, 692], [500, 664], [404, 670], [344, 700]], "cls": "stitch", "closed": False},
                     {"pts": [[14, 800], [-34, 960], [-84, 1130]], "cls": "stitch", "closed": False}],
            "tank": [{"pts": tank, "cls": "body"},
                     {"pts": [on_tube(0.45 * U, 76), on_tube(0.64 * U, 104), on_tube(0.82 * U, 112), on_tube(0.93 * U, 92)], "cls": "hl", "closed": False}],
            "panels": [],
            "caps": [{"at": on_tube(0.76 * U, 130), "r": 13, "cls": "chromef"}],
            "plate_hanger": []},
    }
    if brand != "lodestar":
        STYLES[brand](m, tx, tz, on_tube, U, RR, e)
    # the bespoke kit: everything in the chopper library that this bike does not already wear
    bp, bt, bx, bb = CHOP_BASE[brand]

    def lamp_at(tg, k):
        return [dict(s_, pts=[[round(tg[0] + (q[0] - lc[0]) * k, 1), round(tg[1] + (q[1] - lc[1]) * k, 1)] + q[2:] for q in s_["pts"]]) for s_ in copy.deepcopy(lamp)]
    dial = {"circle": 30, "at": [tx + 34, tz + 26], "cls": "plastic"}
    fx, fz = math.sin(rk), -math.cos(rk)          # down the fork
    pipes = chop_pipes(m["exhaust"]["headers"])
    roll = [{"pts": ring([yoke[0] + 64 - 150 * fx, yoke[1] + 30 - 150 * fz], 2, 44, 0, 360, 30), "cls": "seat", "t": 0.9},
            {"tube": 6, "pts": [[yoke[0] + 64 - 150 * fx, yoke[1] + 76 - 150 * fz], [yoke[0] + 64 - 150 * fx, yoke[1] - 16 - 150 * fz]], "cls": "chromef", "front": True}]
    from kit_draft import BAG
    m["kit"] = {
        "base": {"tank_seat": PAIRS[bp][0], "tail": tails[bt][0], "front": "Low lamp", "exhaust": bx, "bars": BARS[bb][0], "paint": PAINT[bb][0]},
        "tank_seat": [{"name": nm, "note": note, "tank": [{"pts": fn(on_tube, U), "cls": "body"}], "seat": [{"pts": copy.deepcopy(st), "cls": "seat"}]}
                      for i, (nm, note, fn, st) in enumerate(PAIRS) if i != bp],
        "tail": [{"name": nm, "note": note, "tail": copy.deepcopy(shp)} for i, (nm, note, shp) in enumerate(tails) if i != bt],
        "front": [{"name": "High lamp", "note": "the lamp up at the top yoke", "head": lamp_at((yoke[0] + 92 - 150 * fx, yoke[1] + 6 - 150 * fz), 0.8) + [dial]},
                  {"name": "Twin lamps", "note": "two small lamps one above the other", "head": lamp_at((yoke[0] + 86 - 48 * fx, yoke[1] + 6 - 48 * fz), 0.5) + lamp_at((yoke[0] + 86 + 48 * fx, yoke[1] + 6 + 48 * fz), 0.5) + [dial]}],
        "exhaust": [{"name": nm, "note": note, "exhaust": ex} for nm, (note, ex) in pipes.items() if nm != bx and not (brand == "hartwell" and nm == "Megaphones")][:3],
        "bars": [{"name": nm, "note": note, "bars": fn(tx, tz)} for i, (nm, note, fn) in enumerate(BARS) if i != bb],
        "paint": [{"name": nm, "note": note, "colours": col} for i, (nm, note, col) in enumerate(PAINT) if i != bb],
        "extras": [{"name": "Saddlebag", "note": "leather bag on the rear stays", "add": {"panels": copy.deepcopy(BAG)}},
                   {"name": "Tool roll", "note": "leather roll strapped across the fork", "add": {"panels": roll}}],
    }
    return m


def hartwell_chop(m, tx, tz, on_tube, U, RR, lo):
    """Hartwell's chopper, drawn by hand: the British take. An upright finned parallel twin standing tall in the
    rigid frame, a shorter fork with less rake, a small round peanut tank, tall ape hanger bars, a thin flat
    saddle with a separate pillion pad on the rear guard, a short sissy loop and one upswept megaphone each side."""
    from cruiser_draft import shifted_engine
    m.update(name="Hartwell chopper (working title H2)", brand="hartwell", rev="A", engine_label="air-cooled look parallel twin")
    m["colours"] = {"body": "#6B1A22", "frame": "#17181b", "rim": "#C7CBD0", "fork": "#D5D8DC", "accent": "#E8DDBE", "seat": "#5a3a26"}
    e = shifted_engine("hartwell_naked", 0, -18)
    for cy in e["cylinders"]:
        cy["fins"] = True
    x = e["extras"]
    e["extras"] = [x[1], x[3], lo["extras"][1],
                   {"pts": [[690, 700, 1], [650, 690], [630, 650], [650, 610], [690, 600, 1], [712, 650]], "cls": "chromef", "t": 0.7}]
    m["engine"] = e
    m["exhaust"] = {"headers": [{"pts": [[985, 582], [1000, 500], [1006, 410], [984, 322], [924, 288], [600, 282], [220, 286]], "w": 44, "front": True}],
                    "cans": [{"a": [220, 286], "b": [-230, 400], "w": 72, "front": True}]}
    b = m["body"]
    tank = _bullet(on_tube, U)
    b["tank"] = [{"pts": tank, "cls": "body"},
                 {"pts": [on_tube(0.4 * U, 96), on_tube(0.58 * U, 106), on_tube(0.76 * U, 92)], "cls": "hl", "closed": False},
                 {"pts": [on_tube(0.2 * U, 22), on_tube(0.5 * U, 34), on_tube(0.84 * U, 22)], "cls": "stitch", "closed": False}]
    b["caps"] = [{"at": on_tube(0.6 * U, 124), "r": 13, "cls": "chromef"}]
    seat = copy.deepcopy(SADDLE)
    b["seat"] = [{"pts": seat, "cls": "seat"},
                 {"pts": [[590, 688], [500, 668], [410, 670], [352, 686]], "cls": "stitch", "closed": False},
                 {"pts": [[280, 744], [200, 752], [90, 748], [36, 736]], "cls": "stitch", "closed": False}]


def kazeda_chop(m, tx, tz, on_tube, U, RR, lo):
    """Kazeda's chopper, drawn by hand: the long, low digger. A wide inline four across the frame, the longest
    fork of the three at the flattest angle, a flat-sided coffin tank, short drag bars on a tall riser, a low
    banana seat that sweeps up the rear guard with no sissy bar, and stacked straight pipes."""
    from cruiser_draft import shifted_engine
    m.update(name="Kazeda chopper (working title H3)", brand="kazeda", rev="A", engine_label="air-cooled look inline four")
    m["colours"] = {"body": "#C8571B", "frame": "#15161a", "rim": "#C7CBD0", "fork": "#D5D8DC", "accent": "#F0C24A", "seat": "#1c1c1e"}
    e = shifted_engine("kazeda_naked", 20, -18)
    for cy in e["cylinders"]:
        cy["fins"] = True
        cy["length"] = 178
    e["extras"] = [{"pts": [[790, 690, 1], [700, 716], [640, 700], [620, 640], [650, 596], [730, 590, 1], [800, 620, 1]], "cls": "chromef", "t": 0.7}, lo["extras"][1],
                   {"pts": [[560, 610, 1], [470, 620], [410, 596], [396, 520, 1], [430, 452], [520, 440, 1], [566, 500, 1]], "cls": "plastic", "t": 0.8}]
    m["engine"] = e
    m["exhaust"] = {"headers": [{"pts": [[1016, 650], [1040, 540], [1046, 430], [1016, 316], [946, 262], [780, 244], [660, 246]], "w": 50, "front": True}],
                    "cans": [{"a": [660, 240], "b": [10, 252], "w": 58, "front": True}, {"a": [640, 300], "b": [70, 322], "w": 58, "front": True}]}
    b = m["body"]
    tank = _coffin(on_tube, U)
    b["tank"] = [{"pts": tank, "cls": "body"},
                 {"pts": [on_tube(84, 56), on_tube(0.78 * U, 82), on_tube(0.94 * U, 46)], "cls": "hl", "closed": False},
                 {"pts": [on_tube(60, 14), on_tube(U - 30, 4)], "cls": "stitch", "closed": False}]
    b["caps"] = [{"at": on_tube(0.6 * U, 96), "r": 13, "cls": "chromef"}]
    seat = copy.deepcopy(BANANA)
    b["seat"] = [{"pts": seat, "cls": "seat"},
                 {"pts": [[596, 686], [500, 660], [400, 664], [300, 692], [190, 740], [70, 790]], "cls": "stitch", "closed": False}]


STYLES = {"hartwell": hartwell_chop, "kazeda": kazeda_chop}


MODELS = {"corvano_adventure": adventure, "hartwell_adventure": lambda: adventure("hartwell", wire=True), "kazeda_adventure": lambda: adventure("kazeda"),
          "varga_adventure": lambda: adventure("varga", wire=True), "rheinhart_adventure": lambda: adventure("rheinhart", wire=True),
          "lodestar_chopper": chopper, "hartwell_chopper": lambda: chopper("hartwell"), "kazeda_chopper": lambda: chopper("kazeda")}

if __name__ == "__main__":
    for n in (sys.argv[1:] or MODELS):
        path = os.path.join(ROOT, "data", "models", n + ".json")
        json.dump(MODELS[n](), open(path, "w"), indent=1)
        open(path, "a").write("\n")
        print("drafted", path)
