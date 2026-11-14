"""Classics (wave B): a brand's naked frame and engine under the classic family's shape.

The classic family: a round lamp, a flat ribbed bench seat, twin shocks, wire wheels with a bigger
front wheel, a deep mudguard over each wheel, upright bars and a rounded tank with knee cuts. The
Hartwell classic is the lead model and is drawn here by hand on the Hartwell naked's frame and
engine; the other brands' first drafts keep their own frame, engine, exhaust, colours and tank
outline and take the seat, mudguards, stance and front end from the Hartwell.

    python3 foundry/classic_draft.py            # (re)write the classics
"""
import copy
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sport_draft import ROOT, load  # noqa: E402

GEO = {"rake": 26.0, "trail": 108, "seat_height": 800, "hip_x": 500}
FRONT_R, REAR_R = 328, 322


def move(shp, dx, dz):
    out = copy.deepcopy(shp)
    if "pts" in out:
        out["pts"] = [[round(p[0] + dx, 1), round(p[1] + dz, 1)] + p[2:] for p in out["pts"]]
    if "at" in out:
        out["at"] = [round(out["at"][0] + dx, 1), round(out["at"][1] + dz, 1)]
    return out


def fork_top(g, r, length):
    a = math.radians(g["rake"])
    return g["wheelbase"] - length * math.sin(a), r + length * math.cos(a)


def stance(m, base, front_r=FRONT_R, wb_add=0):
    """Classic wheels, fork angle and front end on the brand's own frame: the lamp, clocks and bars
    follow the fork's new top, and the bars come back and down a little for an upright seat."""
    FRONT_R = front_r
    g0, r0 = base["geometry"], base["wheels"]["front"]["tyre_r"]
    m["geometry"] = dict(g0, **GEO, wheelbase=g0["wheelbase"] + wb_add, ground_clearance=max(140, g0["ground_clearance"]))
    m["wheels"]["front"].update(tyre_r=FRONT_R, rim_r=240, wire=True, spokes=36, tread="touring")
    m["wheels"]["rear"].update(tyre_r=REAR_R, rim_r=228, wire=True, spokes=36, tread="touring")
    m["fork"] = dict(base["fork"], usd=False)
    a, b = fork_top(g0, r0, base["fork"]["length"]), fork_top(m["geometry"], FRONT_R, m["fork"]["length"])
    dx, dz = b[0] - a[0], b[1] - a[1]
    # every classic carries the round lamp and twin clocks (the Hartwell's), set on its own fork
    h = load("hartwell_naked")
    ht = fork_top(h["geometry"], h["wheels"]["front"]["tyre_r"], h["fork"]["length"])
    fe = [move(s, b[0] - ht[0] - 4, b[1] - ht[1] + 14) for s in h["front_end"] if s.get("part") == "head"]
    for s in base["front_end"]:
        if s.get("part") == "head":
            continue
        else:
            s2 = move(s, dx - 10, dz - 30)
            if s.get("tube") and abs(s["pts"][0][0] - a[0]) < 3 and abs(s["pts"][0][1] - a[1]) < 3:
                s2["pts"][0] = [round(b[0], 1), round(b[1], 1)]      # the riser still starts on the top yoke
            fe.append(s2)
    m["front_end"] = fe
    m["front_caliper"] = [[p[0] + wb_add, p[1] + FRONT_R - r0] + p[2:] for p in base["front_caliper"]]
    wb = m["geometry"]["wheelbase"]
    m["body"]["front_guard"] = [{"arc": {"c": [wb, FRONT_R], "r0": FRONT_R + 16, "r1": FRONT_R + 36, "a0": 48, "a1": 152}, "cls": "body"}]


def bench(m):
    """The classic's flat ribbed bench seat, rear mudguard, side covers and twin shocks."""
    b = m["body"]
    b["seat"] = [{"pts": [[692, 874, 1], [640, 852], [560, 842], [430, 840], [250, 846], [60, 854], [-40, 852], [-72, 838, 1], [-68, 802, 1], [60, 796], [250, 788], [430, 782], [560, 780], [682, 812, 1]], "cls": "seat"}]
    b["seat"] += [{"pts": [[x, 843], [x - 6, 792]], "cls": "stitch", "closed": False} for x in range(20, 620, 58)]
    # a long substantial tail under the seat that turns down behind it to meet the rear mudguard
    b["tail"] = [{"pts": [[480, 792, 1], [300, 794], [60, 802], [-72, 804, 1], [-112, 760], [-146, 686, 1], [-112, 676, 1], [-70, 730], [60, 752], [250, 748], [480, 744, 1]], "cls": "body", "t": 0.6},
                 {"pts": [[-76, 802, 1], [-106, 790, 1], [-102, 760, 1], [-76, 770, 1]], "cls": "redlens", "t": 0.3}]
    b["behind"] = []
    b["plate_hanger"] = []
    m["fit"] = dict(m.get("fit", {}), plate_hanger=False)
    rg = {"c": [0, REAR_R], "r0": REAR_R + 16, "r1": REAR_R + 48, "a0": 50, "a1": 160}
    b["panels"] = [{"pts": [[640, 800, 1], [560, 776], [450, 776], [420, 750, 1], [470, 650], [555, 600, 1], [645, 640, 1], [650, 715, 1]], "cls": "body", "t": 0.7},
                   {"arc": rg, "cls": "body"}]
    sw = m["swingarm"]
    if sw.get("cls") == "alu" and not sw.get("shaft"):      # a two-sided swingarm takes the classic pair of shocks
        m["shock"] = {"top": [285, 764], "bottom": [95, 398], "twin": True}
    for n_, v in (("S1", [430, 800]), ("S2", [-30, 796]), ("S3", [300, 762]), ("S4", [250, 784])):
        if n_ in m["frame"]["nodes"]:
            m["frame"]["nodes"][n_] = v


def flat_pipes(m):
    """Classic exhausts run flat and low to a plain chrome silencer drawn in at the end (beenster, 14 Nov)."""
    ex = m["exhaust"]
    end = ex["headers"][0]["pts"][-1]
    if end[1] < 450 and ex.get("side") != "under":
        z = min(max(end[1], 300), 345)
        ex["cans"] = [{"a": [end[0], z], "b": [-250, z - 6], "w": 84, "front": True, "nip": True}]


def hartwell(m):
    m["deck"] = {"nose": [672, 846], "tank_pitch": 0.5, "seat_pitch": 2, "seat_unit": True}
    b = m["body"]
    b["tank"] = [{"pts": copy.deepcopy(HART_TANK), "cls": "body"},
                 {"pts": [[1030, 996], [950, 1026], [860, 1022], [786, 992]], "cls": "hl", "closed": False},
                 {"pts": [[700, 905, 1], [760, 880], [850, 872], [930, 884], [905, 930], [800, 940, 1]], "cls": "plastic", "t": 0.8, "draw_only": True}]
    b["caps"] = [{"at": [930, 1052], "r": 16, "cls": "chromef"}]
    m["frame"]["nodes"]["B"] = [690, 836]
    for cy in m["engine"]["cylinders"]:
        cy["fins"] = True
    m.pop("radiator", None)
    for h in m["exhaust"]["headers"]:      # the bigger front wheel needs the pipes' forward sweep pulled back
        h["pts"] = [[min(q[0], 1004 if q[1] > 400 else 1008) if 340 < q[1] < 480 else q[0]] + q[1:] for q in h["pts"]]
    m["colours"] = dict(m["colours"], body="#1F4D3A", seat="#5a3a26", rim="#C9CED3", fork="#C9CED3")
    m["engine_label"], m["frame_label"] = "air-cooled look parallel twin", "steel tube cradle frame"


def own_tank(m):
    """A brand's own tank outline, moved onto the classic's lower and longer tank section."""
    c = HART_TANK
    b = m["body"]
    ct, bt = [p[0] for p in c], [p[0] for p in b["tank"][0]["pts"]]
    kx = (max(ct) - min(ct)) / (max(bt) - min(bt))
    d_ = dict({"nose": [672, 846], "tank_pitch": 0.5, "seat_pitch": 2, "seat_unit": True}, **m.pop("spec", {}))
    dz = d_["nose"][1] - m["deck"]["nose"][1]
    for s in b["tank"]:
        s["pts"] = [[round(min(ct) + (p[0] - min(bt)) * kx, 1), round(p[1] + dz, 1)] + p[2:] for p in s["pts"]]
    b["caps"] = [dict(s, at=[round(min(ct) + (s["at"][0] - min(bt)) * kx, 1), s["at"][1] + dz]) for s in b.get("caps", [])]
    for n_ in ("A", "B", "C"):          # the frame's top line comes down with the tank
        if n_ in m["frame"]["nodes"]:
            m["frame"]["nodes"][n_] = [m["frame"]["nodes"][n_][0], m["frame"]["nodes"][n_][1] + dz]
    m["deck"] = d_
    m["colours"] = dict(m["colours"], seat="#3a2a20")


HART_TANK = [[1088, 952, 1], [1050, 1022], [962, 1054], [860, 1050], [770, 1018], [706, 952], [662, 884, 1], [674, 840, 1], [1080, 844, 1], [1106, 902]]


def draft(name, spec):
    base = load(spec["base"])
    m = copy.deepcopy(base)
    m.pop("kit", None)
    m.update(name=spec["title"], family="Classic", rev="A")
    stance(m, base, spec.get("front_r", FRONT_R), spec.get("wb_add", 0))
    m["spec"] = spec.get("deck", {})
    bench(m)
    for fn in spec.get("style", []):
        fn(m)
    m.pop("spec", None)
    flat_pipes(m)
    xs = [p[0] for part in ("tail", "seat", "tank", "panels", "front_guard") for s in m["body"][part] if "pts" in s for p in s["pts"]]
    m["body"]["overall"] = [-REAR_R - 36, m["geometry"]["wheelbase"] + FRONT_R + 2]
    path = os.path.join(ROOT, "data", "models", name + ".json")
    json.dump(m, open(path, "w"), indent=1)
    open(path, "a").write("\n")
    return path


DRAFTS = {
    "hartwell_classic": {"base": "hartwell_naked", "title": "Hartwell classic (working title K1)", "style": [hartwell]},
    "corvano_classic": {"base": "corvano_naked", "title": "Corvano classic (working title K1)", "style": [own_tank], "front_r": 314, "wb_add": 30},
    "kazeda_classic": {"base": "kazeda_naked", "title": "Kazeda classic (working title K1)", "style": [own_tank], "front_r": 314, "wb_add": 30, "deck": {"nose": [672, 850], "tank_pitch": 2}},
    "rheinhart_classic": {"base": "rheinhart_naked", "title": "Rheinhart classic (working title K1)", "style": [own_tank], "front_r": 314, "wb_add": 20},
}

if __name__ == "__main__":
    for n in (sys.argv[1:] or DRAFTS):
        print("classic drafted in", draft(n, DRAFTS[n]))
