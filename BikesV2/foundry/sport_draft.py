"""First-draft sport bikes: a brand's naked frame and engine under the sport family's bodywork.

The sport rules (beenster, 14 Nov) are the same for every brand: a blunt rounded nose low over the
front wheel, a fairing that curves round the front and joins the nose, aerofoils out of the fairing,
a full-depth front mudguard blade, a low tank, a flat pillion seat and a long low-rake tail. So the
first draft of a brand's sport bike takes those shapes from the approved Kazeda sport, moves the front
of them to the brand's own wheelbase, and keeps the brand's frame, engine, exhaust and colours. The
result is a model file like any other and is then restyled by hand (or by the STYLE table below).

    python3 foundry/sport_draft.py            # (re)write the sport bikes listed in DRAFTS
"""
import copy
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPLIT = 1100       # bodywork ahead of this line moves with the front wheel


def load(n):
    return json.load(open(os.path.join(ROOT, "data", "models", n + ".json")))


def shift(shp, dx, split=None):
    out = copy.deepcopy(shp)
    if "pts" in out:
        out["pts"] = [[round(p[0] + (dx if split is None or p[0] > split else 0), 1)] + p[1:] for p in out["pts"]]
    if "at" in out:
        out["at"] = [out["at"][0] + dx, out["at"][1]]
    if "arc" in out:
        out["arc"]["c"] = [out["arc"]["c"][0] + dx, out["arc"]["c"][1]]
    return out


def draft(name, spec):
    base, k = load(spec["base"]), load("kazeda_sport")
    m = copy.deepcopy(base)
    m.pop("kit", None)
    m.update(name=spec["title"], family="Sport", rev="A", frame_label=spec["frame_label"])
    wb = spec["wheelbase"]
    dx = wb - k["geometry"]["wheelbase"]
    m["geometry"] = dict(k["geometry"], wheelbase=wb, trail=base["geometry"]["trail"])
    m["deck"], m["fork"] = copy.deepcopy(k["deck"]), copy.deepcopy(k["fork"])
    m["wheels"]["front"]["tyre_r"], m["wheels"]["rear"]["tyre_r"] = 300, 322
    m["front_caliper"] = [[p[0] + dx] + p[1:] for p in k["front_caliper"]]
    m["front_end"] = [shift(s, dx) for s in k["front_end"]]
    kb, b = k["body"], {}
    for part, shapes_ in kb.items():
        if part == "overall":
            b[part] = [shapes_[0], shapes_[1] + dx]
        elif part == "front_guard":
            b[part] = [shift(s, dx) for s in shapes_]
        elif part == "panels":
            b[part] = [shift(s, dx, 1010 if s.get("fairing") else (None if min(p[0] for p in s["pts"]) > SPLIT - 30 else 10 ** 6)) for s in shapes_]
        else:
            b[part] = copy.deepcopy(shapes_)
    m["body"] = b
    for n_ in ("S1", "S2", "S3", "S4"):
        if n_ in m["frame"]["nodes"]:
            m["frame"]["nodes"][n_] = k["frame"]["nodes"][n_]
    for fn in spec.get("style", []):
        fn(m)
    path = os.path.join(ROOT, "data", "models", name + ".json")
    json.dump(m, open(path, "w"), indent=1)
    open(path, "a").write("\n")
    return path


def corvano(m):
    """Corvano: both silencers tucked up under the tail (the brand's twin exit), a single painted
    slash vent in the fairing, and a shorter, more kicked-up tail tip."""
    ex = m["exhaust"]
    ex["headers"][0]["pts"] = [[p[0] - (6 if p[0] > 1000 else 0), p[1]] for p in ex["headers"][0]["pts"]]
    for h in ex["headers"]:
        h["pts"][-1] = [345, 712]
        h["pts"][-2] = [392, 690]
    ex["cans"] = [dict(ex["cans"][0], a=[345, 712], b=[50, 806], w=72)]
    pan = m["body"]["panels"]
    vents = [s for s in pan if s.get("draw_only") and s["cls"] == "plastic"]
    for v in vents[1:]:
        pan.remove(v)
    vents[0]["pts"] = [[1070, 690, 1], [900, 520], [800, 360, 1], [850, 350, 1], [950, 500], [1090, 640, 1]]


def _vents(m, shapes_):
    pan = m["body"]["panels"]
    pan[:] = [s for s in pan if not (s.get("draw_only") and s["cls"] == "plastic")]
    pan += [{"pts": p, "cls": "plastic", "t": 0.3, "draw_only": True} for p in shapes_]


def hartwell(m):
    """Hartwell: three upright gill slats behind the front wheel, the old-school touch on a modern fairing."""
    fr = m["frame"]
    fr["nodes"].update(S0=[575, 800], S1=[415, 815], S2=[150, 880], S3=[235, 848])   # the rail dips under the low seat
    for t in fr["tubes"]:
        if t["path"] == ["B", "S2"]:
            t["path"] = ["B", "S0", "S1", "S2"]
    _vents(m, [[[1085 - 62 * i, 640 - 40 * i, 1], [1050 - 62 * i, 470 - 30 * i, 1], [1022 - 62 * i, 455 - 30 * i, 1], [1052 - 62 * i, 618 - 40 * i, 1]] for i in range(3)])


def varga(m):
    """Varga: one big wedge cut out of the fairing side, showing the bright frame colour behind it."""
    _vents(m, [[[1060, 660, 1], [880, 600], [760, 470, 1], [1010, 500, 1]]])
    m["body"]["panels"][-1]["cls"] = "plastic"


DRAFTS = {
    "corvano_sport": {"base": "corvano_naked", "title": "Corvano sport (working title S1)", "wheelbase": 1455,
                      "frame_label": "steel trellis, full fairing", "style": [corvano]},
    "hartwell_sport": {"base": "hartwell_naked", "title": "Hartwell sport (working title S1)", "wheelbase": 1445,
                       "frame_label": "steel tube frame, full fairing", "style": [hartwell]},
    "varga_sport": {"base": "varga_naked", "title": "Varga sport (working title S1)", "wheelbase": 1430,
                    "frame_label": "bright steel trellis, full fairing", "style": [varga]},
}

if __name__ == "__main__":
    for n in (sys.argv[1:] or DRAFTS):
        print("sport drafted in", draft(n, DRAFTS[n]))
