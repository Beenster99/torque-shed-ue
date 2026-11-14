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


def seat(shp, flatten=0.0, step=0.0):
    """Flatten the seat's rise, or step the pillion pad up. Only the top line moves."""
    outline = shapes.dense(shp["pts"], True, shp.get("t", 1.0))
    tops = [p[1] for p in shp["pts"] if 430 < p[0] < 620]
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


def nacelle(model):
    """A painted shell round the lamp, in the bike's colour."""
    x0, x1, z0, z1 = head_box(model)
    zc = (z0 + z1) / 2
    shell = [[x0 - 26, z1 + 14, 1], [x1 - 30, z1 + 26], [x1 - 2, z1 - 10], [x1 + 2, z0 + 12], [x1 - 26, z0 - 12], [x0 - 12, z0 - 4, 1]]
    lens = [[x1 - 2, z1 - 14, 1], [x1 + 8, zc + 22], [x1 + 8, zc - 22], [x1, z0 + 14, 1]]
    keep = [s for s in clean_head(model) if s.get("circle")]
    return [{"pts": [[round(v, 1) for v in p[:2]] + p[2:] for p in shell], "cls": "body", "t": 0.6},
            {"pts": [[round(v, 1) for v in p[:2]] + p[2:] for p in lens], "cls": "lens", "t": 0.4}] + keep


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
        "exhaust": [("Hoch", "a silencer carried high each side", lambda h: {"side": "both", "headers": [dict(x, pts=x["pts"] + [[455, 540], [440, 610]]) for x in h],
                                                                           "cans": [{"a": [440, 610], "b": [170, 756], "w": 78, "front": True}]}),
                    ("Einzel", "one fat silencer on the right", lambda h: {"headers": h[:1], "cans": [{"a": [470, 470], "b": [185, 560], "w": 100, "front": True}]})],
        "extras": [{"name": "Schutz", "note": "painted sump guard", "add": {"panels": [PAN]}},
                   {"name": "Kniekissen", "note": "rubber pads on the tank", "add": {"tank": [PADS]}}],
    },
}


def draft(name):
    path = os.path.join(ROOT, "data", "models", name + ".json")
    m = json.load(open(path))
    spec, b = KITS[name], m["body"]
    kit = {"base": spec["base"], "tank_seat": [], "tail": [], "front": [], "exhaust": [], "extras": copy.deepcopy(spec["extras"])}
    for nm, note, tk, st in spec["tank_seat"]:
        kit["tank_seat"].append({"name": nm, "note": note, "tank": [tank(b["tank"][0], **tk)], "seat": [seat(b["seat"][0], **st)]})
    tray = next((s for s in b.get("behind", []) if "fender" in s.get("cls", "")), None)
    for nm, note, tl in spec["tail"]:
        shp, f = tail(b["tail"], **tl)
        v = {"name": nm, "note": note, "tail": shp}
        if tray:
            v["tray"] = [[round(q, 1) for q in f(p[0], p[1])] + p[2:] for p in tray["pts"]]
        kit["tail"].append(v)
    for nm, note, fn in spec["front"]:
        kit["front"].append({"name": nm, "note": note, "head": fn(m)})
    for nm, note, fn in spec["exhaust"]:
        kit["exhaust"].append({"name": nm, "note": note, "exhaust": fn(copy.deepcopy(m["exhaust"]["headers"]))})
    m["kit"] = kit
    json.dump(m, open(path, "w"), indent=1)
    open(path, "a").write("\n")
    return path


if __name__ == "__main__":
    for n in (sys.argv[1:] or KITS):
        print("kit drafted in", draft(n))
