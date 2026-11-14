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
        kit["front"].append({"name": nm, "note": note, "head": fn(m)})
    for nm, note, fn in spec["exhaust"]:
        ex = fn(copy.deepcopy(m["exhaust"]["headers"]))
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
