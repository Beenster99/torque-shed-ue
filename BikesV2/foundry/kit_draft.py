"""First-draft kits for a model, drawn from the model's own lines.

A kit is the set of outline-changing choices a model offers (tank and seat pairs, tails, front
ends, exhausts, optional extras). This script derives a first draft from the base drawing by rule,
so that every variant stays a variation of the brand's own shape (beenster, 13 Nov: tanks never
bulbous, tails are variations of one good swept line, no tail hump). The result is written into
the model file's "kit" section, where it can then be edited by hand like any other drawing.

    python3 foundry/kit_draft.py            # (re)write the kits listed in KITS below
"""
import copy
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shapes  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
X0 = 400          # seats and tails only change behind this line, so the rider's seat stays put


def _pts(shp, f):
    out = copy.deepcopy(shp)
    out["pts"] = [[round(v, 1) for v in f(p[0], p[1])] + p[2:] for p in shp["pts"]]
    return out


def tank(shp, low=1.0, front=1.0, rear=1.0, peak_dx=0):
    """Scale the tank's top line about its flat underside; the underside and the rear nose stay."""
    zb = 900

    def f(x, z):
        if z < 930:
            return x, z
        k = low * (front if x > 900 else rear if x < 800 else (front + rear) / 2)
        return x + (peak_dx if x > 900 else 0), zb + (z - zb) * k
    return _pts(shp, f)


def tank_fit(shp, low=1.0, front=1.0, rear=1.0):
    """Scale a tank's top line about its own underside, whatever the underside's height or pitch
    (classics sit low and flat, cruisers climb to the headstock). The rear nose stays for the seat."""
    outline = shapes.dense(shp["pts"], True, shp.get("t", 1.0))
    xs = [p[0] for p in outline]
    xa, xb = min(xs), max(xs)

    def f(x, z):
        tb = shapes.top_bottom(outline, min(max(x, xa + 2), xb - 2))
        if not tb or x < xa + 45 or z < (tb[0] + tb[1]) / 2:
            return x, z
        u = (x - xa) / (xb - xa)
        k = low * (front if u > 0.6 else rear if u < 0.36 else (front + rear) / 2)
        return x, tb[1] + (z - tb[1]) * k
    return _pts(shp, f)


def seat(shp, flatten=0.0, step=0.0, top_only=False):
    """Flatten the seat's rise, or step the pillion pad up. Only the top line moves."""
    outline = shapes.dense(shp["pts"], True, shp.get("t", 1.0))
    def upper(p):
        tb = shapes.top_bottom(outline, p[0])
        return tb and p[1] >= (tb[0] + tb[1]) / 2
    tops = [p[1] for p in shp["pts"] if 430 < p[0] < 620 and (not top_only or upper(p))]
    zref = min(tops) if tops else 860

    def f(x, z):
        tb = shapes.top_bottom(outline, x)
        if not tb or z < (tb[0] + tb[1]) / 2 or x > 640:
            return x, z
        if x < 600:
            z = zref + (z - zref) * (1 - flatten)
        if x < 470:
            z += step * min(1.0, (470 - x) / 40)
        return x, z
    return _pts(shp, f)


def tail(shapes_, end_dx=0.0, tip_dz=0.0, x0=X0):
    """Stretch or shorten the tail behind x0 and lift or drop its tip: the same swept line, retimed.
    (x0 further back leaves the part a seat rests on untouched, for tails with little room to spare.)"""
    xs = [p[0] for s in shapes_ for p in s["pts"]]
    x_end = min(xs)
    k = (x0 - (x_end + end_dx)) / (x0 - x_end)

    def f(x, z):
        if x >= x0:
            return x, z
        u = (x0 - x) / (x0 - x_end)
        return x0 - (x0 - x) * k, z + tip_dz * u * u
    return [_pts(s, f) for s in shapes_], f


def head_box(model):
    pp = [p for s in model["front_end"] if s.get("part") == "head" and "pts" in s for p in s["pts"]]
    return min(p[0] for p in pp), max(p[0] for p in pp), min(p[1] for p in pp), max(p[1] for p in pp)


def clean_head(model):
    return [{k: v for k, v in s.items() if k != "part"} for s in copy.deepcopy(model["front_end"]) if s.get("part") == "head"]


def flyscreen(model):
    x0, x1, z0, z1 = head_box(model)
    # the screen springs from the top of the lamp glass and leans back over the clocks
    zl = max(p[1] for s in model["front_end"] if s.get("part") == "head" and s.get("cls") == "lens" for p in s["pts"])
    top = max(z1, zl + 60) + 70
    scr = [[x1 - 14, zl - 6, 1], [x0 + 2, top, 1], [x0 - 10, top - 6, 1], [x1 - 34, zl - 12, 1]]
    return clean_head(model) + [{"pts": scr, "cls": "screen", "t": 0}]


def nacelle(model, lift=0):
    """A painted shell round the lamp, in the bike's colour."""
    x0, x1, z0, z1 = head_box(model)
    z0 += lift          # a taller front tyre needs the shell's chin held up
    zc = (z0 + z1) / 2
    shell = [[x0 - 26, z1 + 14, 1], [x1 - 30, z1 + 26], [x1 - 2, z1 - 10], [x1 + 2, z0 + 12], [x1 - 28, z0 - 6], [x0 - 12, z0 - 2, 1]]
    lens = [[x1 - 2, z1 - 14, 1], [x1 + 8, zc + 22], [x1 + 8, zc - 22], [x1, z0 + 14, 1]]
    keep = [s for s in clean_head(model) if s.get("circle")]
    return [{"pts": [[round(v, 1) for v in p[:2]] + p[2:] for p in shell], "cls": "body", "t": 0.6},
            {"pts": [[round(v, 1) for v in p[:2]] + p[2:] for p in lens], "cls": "lens", "t": 0.4}] + keep


def _end(hd):
    return [hd["pts"][-1][0], hd["pts"][-1][1]]


def classic_bobbed(m):
    t = copy.deepcopy(m["body"]["tail"])
    p = t[0]["pts"]
    t[0]["pts"] = p[:6] + [[-262, 632, 1], [-246, 614, 1]] + p[12:]
    return t, [{"pts": [[-248, 634, 1], [-260, 620, 1], [-334, 560, 1], [-322, 574, 1]], "cls": "plate", "t": 0}]


def classic_ducktail(m):
    t = copy.deepcopy(m["body"]["tail"])
    p = t[0]["pts"]
    t[0]["pts"] = p[:7] + [[-340, 568], [-398, 520, 1], [-384, 502, 1], [-318, 548]] + p[11:]
    return t, [{"pts": [[-384, 508, 1], [-400, 514, 1], [-432, 424, 1], [-416, 418, 1]], "cls": "plate", "t": 0}]


def cruiser_valanced(m):
    r = m["wheels"]["rear"]["tyre_r"]
    import math
    ang = [math.radians(a) for a in range(120, 171, 10)]
    ring = [[round(r_ * math.cos(a), 1), round(r + r_ * math.sin(a), 1)] for r_, aa in ((r + 36, ang), (r + 14, ang[::-1])) for a in aa]
    for i in (0, 5, 6, 11):
        ring[i].append(1)
    return copy.deepcopy(m["body"]["tail"]) + [{"pts": ring, "cls": "body", "t": 0.9}], None


def cruiser_pillion(m):
    pad = {"pts": [[230, 640, 1], [120, 668], [0, 688], [-80, 678, 1], [-90, 700, 1], [0, 722], [120, 706], [236, 690, 1]], "cls": "seat", "t": 0.7}
    return copy.deepcopy(m["body"]["tail"]) + [pad], None


RACK = [{"tube": 10, "pts": [[-60, 792], [-200, 792], [-214, 760]], "cls": "chromef", "front": True},
        {"tube": 8, "pts": [[-120, 792], [-150, 716]], "cls": "chromef", "front": True}]
SISSY = [{"tube": 12, "pts": [[190, 640], [150, 880], [118, 884], [96, 668]], "cls": "chromef", "front": True},
         {"pts": [[168, 860, 1], [112, 866, 1], [104, 790, 1], [156, 784, 1]], "cls": "seat", "t": 0.5}]
BAG = [{"pts": [[170, 610, 1], [-110, 610, 1], [-130, 470], [-90, 404, 1], [120, 404, 1], [176, 470]], "cls": "seat", "t": 0.5},
       {"tube": 8, "pts": [[90, 610], [86, 500]], "cls": "chromef", "front": True}, {"tube": 8, "pts": [[-30, 610], [-34, 500]], "cls": "chromef", "front": True}]


def classic_kit(brand):
    return {
        "base": {"tank_seat": "Roadster", "tail": "Full guard", "front": "Peaked lamp", "exhaust": "Flat pipe"},
        "tank_seat": [("Flat track", "lower tank with a flat one-level bench", dict(low=0.9, fit=True), dict(flatten=0.85, top_only=True)),
                      ("Grand tour", "fuller tank with a deeper saddle", dict(front=1.04, rear=0.97, fit=True), dict(step=8))],
        "tail": [("Bobbed", "mudguard cut short over the tyre", classic_bobbed), ("Ducktail", "mudguard tip flicked out", classic_ducktail)],
        "front": [("Flyscreen", "round lamp with a small tinted screen", flyscreen), ("Nacelle", "lamp set in a painted shell", lambda m: nacelle(m, 16))],
        "exhaust": [("Upswept", "silencer kicked up along the tail", lambda h: {"headers": h, "cans": [{"a": _end(x), "b": [-180 + 20 * i, _end(x)[1] + 150 + 60 * i], "w": 80, "front": True, "nip": True} for i, x in enumerate(h)]}),
                    ("Megaphone", "short open megaphone", lambda h: {"headers": h, "cans": [{"a": _end(x), "b": [_end(x)[0] - 470, _end(x)[1] + 28], "w": 96, "front": True} for x in h]})],
        "extras": [{"name": "Rack", "note": "chrome luggage rack behind the saddle", "add": {"panels": RACK}},
                   {"name": "Sump plate", "note": "painted plate under the engine", "add": {"panels": [PAN]}}],
    }


def cruiser_kit(brand):
    return {
        "base": {"tank_seat": "Solo", "tail": "Bobbed", "front": "Round lamp", "exhaust": "Standard"},
        "tank_seat": [("Peanut", "slimmer, lower tank with a flatter seat", dict(low=0.88, fit=True), dict(flatten=0.5, top_only=True)),
                      ("King", "fuller tank and a seat with a raised bolster", dict(front=1.05, rear=0.97, fit=True), dict(step=10))],
        "tail": [("Valanced", "long mudguard wrapped down the back of the tyre", cruiser_valanced), ("Two-up", "pillion pad on the mudguard", cruiser_pillion)],
        "front": [("Flyscreen", "round lamp with a small tinted screen", flyscreen), ("Nacelle", "lamp set in a painted shell", nacelle)],
        "exhaust": [("Shotgun", "short straight pipes", lambda h: {"headers": h, "cans": [{"a": _end(x), "b": [_end(x)[0] - 400, _end(x)[1] + 6], "w": 78, "front": True} for x in h]}),
                    ("Fishtail", "long silencers with nipped ends", lambda h: {"headers": h, "cans": [{"a": _end(x), "b": [-240 + 30 * i, _end(x)[1] + 10], "w": 84, "front": True, "nip": True} for i, x in enumerate(h)]})],
        "extras": [{"name": "Sissy bar", "note": "chrome hoop and back pad behind the seat", "add": {"panels": SISSY}},
                   {"name": "Saddlebag", "note": "leather bag over the rear wheel", "add": {"panels": BAG}}],
    }


PAN = {"pts": [[990, 334, 1], [928, 204, 1], [660, 178, 1], [596, 214, 1], [700, 252, 1], [900, 292, 1]], "cls": "body", "t": 0}
PADS = {"pts": [[748, 978, 1], [842, 1002], [872, 948], [800, 918, 1]], "cls": "plastic", "t": 0.5}

KITS = {
    "hartwell_naked": {
        "base": {"tank_seat": "Dales", "tail": "Mile", "front": "Lantern", "exhaust": "Stubby"},
        "tank_seat": [("Sprint", "lower, leaner tank with a flatter bench", dict(low=0.88), dict(flatten=0.5)),
                      ("Tourist", "fuller shoulders with a raised pillion pad", dict(front=1.05, rear=0.94, peak_dx=8), dict(step=14))],
        "tail": [("Bobtail", "cut shorter", dict(end_dx=45)), ("Longtail", "longer, with a lifted tip", dict(end_dx=-45, tip_dz=14))],
        "front": [("Flyscreen", "round lamp with a small tinted screen", flyscreen), ("Nacelle", "lamp set in a painted shell", nacelle)],
        "exhaust": [("Upswept", "carried high along the tail", lambda h: {"headers": [dict(h[0], pts=h[0]["pts"] + [[480, 415], [462, 480], [452, 550]])],
                                                                    "cans": [{"a": [452, 550], "b": [170, 690], "w": 84, "front": True}]}),
                    ("Peashooter", "one long slim silencer, low", lambda h: {"headers": h, "cans": [{"a": [530, 318], "b": [130, 394], "w": 80, "front": True}]})],
        "extras": [{"name": "Sump plate", "note": "painted plate under the engine", "add": {"panels": [PAN]}},
                   {"name": "Knee pads", "note": "rubber pads on the tank", "add": {"tank": [PADS]}}],
    },
    "varga_naked": {
        "base": {"tank_seat": "Iskra", "tail": "Farok", "front": "Maszk", "exhaust": "Magas"},
        "tank_seat": [("Vihar", "lower, flatter tank", dict(low=0.9), dict()),
                      ("Szikra", "taller tank with the peak pushed forward", dict(front=1.05, rear=0.92, peak_dx=10), dict())],
        "tail": [("Kurta", "cut shorter", dict(end_dx=38, tip_dz=-22, x0=95)), ("Hosszu", "longer and kicked higher", dict(end_dx=-40, tip_dz=16, x0=185))],
        "front": [("Pajzs", "mask with a tinted screen", flyscreen), ("Burok", "mask wrapped in a painted shell", nacelle)],
        "exhaust": [("Alacsony", "short dark silencer under the engine", lambda h: {"headers": [dict(h[0], pts=h[0]["pts"][:-2])],
                                                                                "cans": [{"a": [530, 322], "b": [345, 350], "w": 100, "front": True, "dark": True}]}),
                    ("Dupla", "two slim silencers stacked high", lambda h: {"headers": h, "cans": [{"a": [470, 540], "b": [250, 640], "w": 62, "front": True},
                                                                                              {"a": [462, 604], "b": [240, 708], "w": 62, "front": True}]})],
        "extras": [{"name": "Has", "note": "painted belly pan", "add": {"panels": [PAN]}},
                   {"name": "Egyes", "note": "painted single seat cowl", "auto": "seat_cowl"}],
    },
    "rheinhart_naked": {
        "base": {"tank_seat": "Strecke", "tail": "Heck", "front": "Doppel", "exhaust": "Zwei"},
        "tank_seat": [("Kurve", "lower tank with a flatter bench", dict(low=0.89), dict(flatten=0.5)),
                      ("Reise", "fuller tank with a raised pillion pad", dict(front=1.04, rear=0.95), dict(step=12))],
        "tail": [("Kurzheck", "cut shorter", dict(end_dx=40)), ("Langheck", "longer, with a lifted tip", dict(end_dx=-45, tip_dz=12))],
        "front": [("Scheibe", "twin lamps with a tinted screen", flyscreen), ("Maske", "twin lamps in a painted shell", nacelle)],
        "exhaust": [("Hoch", "two silencers carried high under the seat", lambda h: {"side": "under", "headers": [dict(x, pts=x["pts"] + [[455, 560], [420, 660], [380, 716]]) for x in h],
                                                                                   "cans": [{"a": [380, 716], "b": [110, 806], "w": 72, "front": True}]}),
                    ("Einzel", "one fat silencer on the swingarm side", lambda h: {"side": "left", "headers": h[:1], "cans": [{"a": [470, 470], "b": [185, 560], "w": 100, "front": True}]})],
        "extras": [{"name": "Schutz", "note": "painted sump guard", "add": {"panels": [PAN]}},
                   {"name": "Kniekissen", "note": "rubber pads on the tank", "add": {"tank": [PADS]}}],
    },
}

for _b in ("hartwell", "corvano", "kazeda", "rheinhart", "lodestar"):
    KITS[_b + "_classic"] = classic_kit(_b)
    KITS[_b + "_cruiser"] = cruiser_kit(_b)
del KITS["hartwell_cruiser"]


def adventure_kit(brand):
    """Adventure kits are about travel: screens, silencers and luggage on the same approved body."""
    clocks = {"pts": [[1222, 1118, 1], [1268, 1110, 1], [1280, 1166, 1], [1236, 1174, 1]], "cls": "plastic", "t": 0}

    def screen(pts):
        return lambda m: [copy.deepcopy(clocks), {"pts": pts, "cls": "screen", "t": 0}]
    return {
        "base": {"tank_seat": "Touring", "tail": "Standard", "front": "Tall screen", "exhaust": "Large silencer"},
        "tank_seat": [("Rally", "lower, slimmer tank and a flatter one-piece seat", dict(low=0.9, fit=True), dict(flatten=0.25, top_only=True)),
                      ("Long range", "fuller tank and a deeper stepped pillion seat", dict(front=1.06, fit=True), dict(step=10))],
        "tail": [("Kicked", "tail tip lifted higher", dict(tip_dz=28)), ("Flat", "lower, flatter tail tip", dict(tip_dz=-26))],
        "front": [("Rally screen", "short screen cut low for standing up", screen([[1436, 1090, 1], [1290, 1250, 1], [1306, 1216, 1], [1340, 1126, 1]])),
                  ("Touring screen", "taller screen with a lip at the top", screen([[1436, 1090, 1], [1186, 1404, 1], [1172, 1436, 1], [1200, 1340], [1300, 1140, 1]]))],
        "exhaust": [("Slim high", "slimmer silencer carried high", lambda h: {"headers": h, "cans": [{"a": [560, 372], "b": [80, 610], "w": 100, "front": True}]}),
                    ("Low slung", "silencer kept low beside the wheel, clear of the luggage", lambda h: {"headers": h, "cans": [{"a": [560, 372], "b": [90, 470], "w": 124, "front": True}]})],
        "extras": [{"name": "Panniers", "note": "hard cases either side of the tail",
                    "add": {"panels": [{"pts": [[130, 900, 1], [-180, 920, 1], [-204, 896], [-198, 610], [-176, 580, 1], [110, 566, 1], [138, 596]], "cls": "plastic", "t": 0.3},
                                       {"pts": [[132, 820], [-200, 838]], "cls": "hl", "closed": False}]}},
                   {"name": "Top box", "note": "hard case on the rack",
                    "add": {"panels": [{"pts": [[-10, 1060, 1], [0, 1240], [-20, 1262, 1], [-210, 1262, 1], [-232, 1240], [-224, 1078, 1]], "cls": "plastic", "t": 0.3},
                                       {"pts": [[-2, 1196], [-230, 1196]], "cls": "hl", "closed": False}]}},
                   {"name": "Crash bars", "note": "steel bars round the fairing and engine",
                    "add": {"panels": [{"tube": 14, "pts": [[1130, 900], [1236, 780], [1200, 600], [1070, 500]], "cls": "steelt", "front": True, "t": 0.5},
                                       {"tube": 12, "pts": [[1216, 690], [1100, 690]], "cls": "steelt", "front": True}]}}],
    }


for _b in ("corvano", "hartwell", "kazeda", "varga", "rheinhart"):
    KITS[_b + "_adventure"] = adventure_kit(_b)


def sport_screen(dx, dz, mid=(0, 0)):
    """The same Perspex screen with its top edge moved: taller for a tucked-in rider, or cut low."""
    def f(m):
        sc = copy.deepcopy(next(v for v in m["body"]["panels"] if v.get("cls") == "screen"))
        hi = max(sc["pts"], key=lambda q: q[1])
        lo = max(sc["pts"], key=lambda q: q[0])
        md = max((q for q in sc["pts"] if q is not hi and q is not lo), key=lambda q: q[0] + q[1])
        for q in sc["pts"]:
            if q is hi or (abs(q[0] - hi[0]) < 40 and abs(q[1] - hi[1]) < 20):
                q[0], q[1] = q[0] + dx, q[1] + dz
        md[0], md[1] = md[0] + mid[0], md[1] + mid[1]
        return {"screen": sc}
    return f


def _can(a, b, k=1.0, **kw):
    return dict({"a": a, "b": [round(a[0] + (b[0] - a[0]) * k, 1), round(a[1] + (b[1] - a[1]) * k, 1)], "front": True}, **kw)


SPORT_EXHAUSTS = {
    # the Kazeda keeps its pipes and collector box inside the belly of the fairing (beenster, 17 Nov);
    # only the silencer that pokes out behind the fairing changes
    "kazeda": [("Slip-on", "longer round silencer poking out behind the fairing",
                lambda h, base=None: dict(copy.deepcopy(base), cans=[base["cans"][0], _can([520, 250], [395, 288], 2.2, w=90)])),
               ("Shorty", "short dark stub of a silencer",
                lambda h, base=None: dict(copy.deepcopy(base), cans=[base["cans"][0], _can([520, 250], [395, 288], 0.8, w=112, dark=True)]))],
    "hartwell": [("Long can", "long slim silencer, low",
                  lambda h: {"headers": h, "cans": [_can([530, 322], [190, 398], w=86)], "join": True}),
                 ("High can", "silencer kicked up towards the tail",
                  lambda h: {"headers": [dict(h[0], pts=h[0]["pts"] + [[472, 346]])], "cans": [_can([472, 346], [236, 474], w=90)], "join": True})],
    "varga": [("Stubby", "short dark silencer",
               lambda h: {"headers": h, "cans": [_can([470, 535], [240, 575], 0.62, w=96, dark=True)], "join": True}),
              ("High", "slimmer silencer kicked up under the tail",
               lambda h: {"headers": h, "cans": [_can([470, 535], [212, 640], w=76)], "join": True})],
    "corvano": [("Short", "short fat silencer tucked under the seat",
                 lambda h: {"side": "under", "headers": h, "cans": [_can([345, 712], [50, 806], 0.66, w=82)], "join": True}),
                ("Long", "long slim silencer out to the tail tip",
                 lambda h: {"side": "under", "headers": h, "cans": [_can([345, 712], [50, 806], 1.2, w=64)], "join": True})],
}


def sport_kit(brand):
    return {
        "base": {"tank_seat": "Road", "tail": "Standard", "front": "Road screen", "exhaust": "Standard"},
        "tank_seat": [("Track", "lower tank to tuck in behind, with a flatter seat", dict(low=0.93, fit=True), dict(flatten=0.5, top_only=True)),
                      ("Endurance", "fuller tank with a raised pillion pad", dict(front=1.04, rear=0.97, fit=True), dict(step=10, top_only=True))],
        "tail": [("Short tail", "tail tip cut shorter", dict(end_dx=48, x0=95)), ("Kicked tail", "longer tip, kicked up", dict(end_dx=-30, tip_dz=20, x0=95))],
        "front": [("Race bubble", "taller double-bubble screen", sport_screen(-22, 46, (-4, 14))),
                  ("Low screen", "screen cut low", sport_screen(24, -34, (2, -6)))],
        "exhaust": SPORT_EXHAUSTS[brand],
        "extras": [{"name": "Seat cowl", "note": "painted cowl over the pillion seat", "auto": "seat_cowl"},
                   {"name": "Tail tidy", "note": "number plate arm taken off", "fit": {"plate_hanger": False}}],
    }


def cafe_hump(k, dx=0):
    """The cafe tail's hump made taller or flatter; its underside on the frame loop stays."""
    def f(m):
        t = copy.deepcopy(m["body"]["tail"])
        outline = shapes.dense(t[0]["pts"], True, t[0].get("t", 1.0))
        x_end = min(p[0] for p in outline)
        for p in t[0]["pts"]:
            tb = shapes.top_bottom(outline, min(max(p[0], x_end + 2), 290))
            if p[0] < 300 and tb and p[1] > (tb[0] + tb[1]) / 2:
                p[1] = round(tb[1] + (p[1] - tb[1]) * k, 1)
                p[0] = round(p[0] + dx * (300 - p[0]) / (300 - x_end), 1)
        return t, None
    return f


def cafe_exhaust(short):
    def f(h, base=None):
        cans = []
        for c in base["cans"]:
            c = copy.deepcopy(c)
            if short:
                c = dict(_can(c["a"], c["b"], 0.58, w=min(c["w"] + 12, 100), dark=True))
            elif c.get("mega"):
                c.pop("mega")
                c = dict(c, b=_can(c["a"], c["b"], 0.8)["b"], w=c["w"] + 10, nip=True)
            else:
                c.pop("nip", None)
                c["mega"] = round(c["w"] * 1.55)
            cans.append(c)
        return {"headers": h, "cans": cans, "join": True}
    return f


BOARD = [{"pts": [[452, 742, 1], [470, 776], [520, 786], [566, 770], [580, 728, 1], [562, 690], [512, 680], [466, 700]], "cls": "plate", "t": 0.9, "front": True}]


def cafe_kit(brand, mega):
    return {
        "base": {"tank_seat": "Clubman", "tail": "Hump", "front": "Round lamp", "exhaust": "Megaphone" if mega else "Standard"},
        "tank_seat": [("Sprint", "lower, leaner tank with a flatter seat", dict(low=0.92, fit=True), dict(flatten=0.5, top_only=True)),
                      ("Endurance", "fuller tank with a deeper seat", dict(front=1.05, rear=0.97, fit=True), dict(step=8, top_only=True))],
        "tail": [("Flat hump", "seat hump cut low", cafe_hump(0.6)), ("Bullet hump", "taller, longer racing hump", cafe_hump(1.22, -24))],
        "front": [("Flyscreen", "round lamp with a small tinted screen", flyscreen), ("Square lamp", "squared LED lamp in a black housing", nacelle) if brand == "hartwell" else ("Nacelle", "lamp set in a painted shell", nacelle)],
        "exhaust": [("Shorty", "short dark silencer", cafe_exhaust(True)),
                    ("Reverse cone", "plain silencer with a nipped end", cafe_exhaust(False)) if mega else ("Megaphone", "flared open megaphone", cafe_exhaust(False))],
        "extras": [{"name": "Number board", "note": "oval race number board on the side", "add": {"panels": BOARD}},
                   {"name": "Sump plate", "note": "painted plate under the engine", "add": {"panels": [PAN]}}],
    }


for _b in ("kazeda", "corvano", "hartwell", "varga"):
    KITS[_b + "_sport"] = sport_kit(_b)
for _b in ("hartwell", "corvano", "kazeda", "rheinhart", "lodestar"):
    _m = json.load(open(os.path.join(ROOT, "data", "models", _b + "_cafe.json")))
    KITS[_b + "_cafe"] = cafe_kit(_b, any(c.get("mega") for c in _m["exhaust"]["cans"]))


def draft(name):
    path = os.path.join(ROOT, "data", "models", name + ".json")
    m = json.load(open(path))
    spec, b = KITS[name], m["body"]
    kit = {"base": spec["base"], "tank_seat": [], "tail": [], "front": [], "exhaust": [], "extras": copy.deepcopy(spec["extras"])}
    for nm, note, tk, st in spec["tank_seat"]:
        tk = dict(tk)
        fn_t = tank_fit if tk.pop("fit", False) else tank
        kit["tank_seat"].append({"name": nm, "note": note, "tank": [fn_t(b["tank"][0], **tk)], "seat": [seat(b["seat"][0], **st)]})
    tray = next((s for s in b.get("behind", []) if "fender" in s.get("cls", "")), None)
    for nm, note, tl in spec["tail"]:
        if callable(tl):
            shp, plate = tl(m)
            kit["tail"].append(dict({"name": nm, "note": note, "tail": shp}, **({"plate": plate} if plate else {})))
            continue
        shp, f = tail(b["tail"], **tl)
        v = {"name": nm, "note": note, "tail": shp}
        if tray:
            v["tray"] = [[round(q, 1) for q in f(p[0], p[1])] + p[2:] for p in tray["pts"]]
        kit["tail"].append(v)
    for nm, note, fn in spec["front"]:
        r = fn(m)
        kit["front"].append(dict({"name": nm, "note": note}, **(r if isinstance(r, dict) else {"head": r})))
    for nm, note, fn in spec["exhaust"]:
        hd = copy.deepcopy(m["exhaust"]["headers"])
        ex = fn(hd, base=m["exhaust"]) if fn.__code__.co_argcount > 1 else fn(hd)
        if "side" in m["exhaust"]:
            ex.setdefault("side", m["exhaust"]["side"])
        kit["exhaust"].append({"name": nm, "note": note, "exhaust": ex})
    m["kit"] = kit
    json.dump(m, open(path, "w"), indent=1)
    open(path, "a").write("\n")
    return path


if __name__ == "__main__":
    for n in (sys.argv[1:] or KITS):
        print("kit drafted in", draft(n))
