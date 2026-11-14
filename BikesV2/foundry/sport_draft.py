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


TAIL = [[1470, 756], [1440, 778], [1380, 790], [1240, 772], [1150, 800], [1088, 862, 1], [1108, 930], [1190, 975, 1]]   # the nose's underside and back, shared
TOP = [[1240, 772], [1150, 800], [1088, 862]]                                                                            # where every fairing side meets the nose


def _shape(m, fairing=None, nose=None, vents=None, pan=None):
    """Give a brand its own fairing side, nose, vents and belly pan. Points are written for the Kazeda's
    wheelbase and the front of them moves with the brand's own front wheel."""
    dx = m["geometry"]["wheelbase"] - 1440
    mv = lambda pts: [[p[0] + (dx if p[0] > 1010 else 0)] + p[1:] for p in pts]  # noqa: E731
    pan_ = m["body"]["panels"]
    if fairing:
        next(s for s in pan_ if s.get("fairing"))["pts"] = mv(fairing)
    if nose:
        next(s for s in pan_ if s.get("cowl"))["pts"] = mv(nose + TAIL)
    if vents is not None:
        pan_[:] = [s for s in pan_ if not (s.get("draw_only") and s["cls"] == "plastic")]
        pan_ += [dict({"pts": mv(p), "cls": "plastic", "t": 0.3, "draw_only": True}, **o) for p, o in vents]
    if pan:
        pan_.append({"pts": mv(pan), "cls": "body", "t": 0.5})


def corvano(m):
    """Corvano: a short, deep, rounded fairing cut steeply behind the front cylinder so the trellis and
    the V-twin stay on show, a sharper nose, one slash vent, and the silencer tucked up under the tail."""
    ex = m["exhaust"]
    ex["headers"][0]["pts"] = [[p[0] - (6 if p[0] > 1000 else 0), p[1]] for p in ex["headers"][0]["pts"]]
    for h in ex["headers"]:
        h["pts"][-1] = [345, 712]
        h["pts"][-2] = [392, 690]
    ex["cans"] = [dict(ex["cans"][0], a=[345, 712], b=[50, 806], w=72)]
    _shape(m,
           fairing=[[1528, 730], [1440, 778], [1380, 790]] + TOP + [[1010, 830], [960, 760, 1], [930, 620], [880, 480], [800, 380], [700, 330, 1],
                    [640, 250], [700, 165], [900, 140], [1040, 150], [1100, 200], [1112, 330], [1126, 402], [1162, 482], [1222, 562], [1312, 628], [1420, 672], [1500, 706]],
           nose=[[1375, 965], [1460, 895], [1525, 815], [1560, 755, 1], [1535, 728]],
           vents=[([[1070, 690, 1], [930, 540], [880, 430, 1], [925, 425, 1], [975, 520], [1090, 640, 1]], {"reveal": True})])


def hartwell(m):
    """Hartwell: straight lines. The nose's underside runs straight back to the shoulder, the front edge
    drops upright behind the wheel, and the keel is one flat line to a sharp tip; three gill slats."""
    fr = m["frame"]
    fr["nodes"].update(S0=[575, 800], S1=[415, 815], S2=[150, 880], S3=[235, 848])   # the rail dips under the low seat
    for t in fr["tubes"]:
        if t["path"] == ["B", "S2"]:
            t["path"] = ["B", "S0", "S1", "S2"]
    _shape(m,
           fairing=[[1528, 730, 1], [1380, 790, 1]] + TOP + [[1000, 822], [900, 742], [830, 650, 1], [780, 470, 1], [640, 300, 1], [420, 250, 1],
                    [470, 190, 1], [1060, 150, 1], [1105, 190, 1], [1105, 470, 1], [1200, 585, 1], [1330, 650, 1]],
           nose=[[1375, 975], [1460, 930], [1522, 860], [1548, 790], [1540, 745], [1515, 728]],
           vents=[([[1080 - 62 * i, 640 - 40 * i, 1], [1045 - 62 * i, 470 - 30 * i, 1], [1017 - 62 * i, 455 - 30 * i, 1], [1047 - 62 * i, 618 - 40 * i, 1]], {}) for i in range(3)])


def varga(m):
    """Varga: the least bodywork. A sharp wedge of a fairing that stops above the crankcases, a separate
    small belly pan under the engine, a faceted nose, and one big wedge cut out of the side."""
    _shape(m,
           fairing=[[1528, 730, 1], [1440, 778], [1380, 790]] + TOP + [[1000, 822], [900, 742], [830, 650, 1], [800, 560], [720, 440, 1],
                    [860, 420], [1000, 360], [1112, 300, 1], [1126, 402], [1162, 482], [1222, 562], [1312, 628], [1420, 672, 1]],
           nose=[[1375, 965, 1], [1500, 860, 1], [1545, 770, 1], [1520, 727, 1]],
           vents=[([[1060, 660, 1], [900, 610], [820, 500, 1], [1010, 500, 1]], {"reveal": True})],
           pan=[[1005, 300, 1], [940, 200], [700, 160], [520, 190, 1], [600, 240], [900, 280]])


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
