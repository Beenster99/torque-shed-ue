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
    fe = [move(s, b[0] - ht[0] - 6, b[1] - ht[1] + 22) for s in h["front_end"] if s.get("part") == "head"]
    # a larger lamp than the naked's, with a little peak over the lens
    bowl = next(s for s in fe if s.get("round"))["pts"]
    cx, cz = sum(q[0] for q in bowl) / len(bowl) + 20, sum(q[1] for q in bowl) / len(bowl)
    for s in fe:
        if "pts" in s:
            s["pts"] = [[round(cx + (q[0] - cx) * 1.22, 1), round(cz + (q[1] - cz) * 1.22, 1)] + q[2:] for q in s["pts"]]
    lx, lz = next(s for s in fe if s.get("cls") == "lens")["pts"][0][:2]
    fe.append({"pts": [[lx - 22, lz + 6, 1], [lx + 36, lz - 8, 1], [lx + 34, lz - 17, 1], [lx - 4, lz - 6, 1]], "cls": "chromef", "t": 0.3, "part": "head"})
    for s in base["front_end"]:
        if s.get("part") == "head":
            continue
        else:
            s2 = move(s, dx - 10, dz - 30)
            if s.get("tube") and abs(s["pts"][0][0] - a[0]) < 3 and abs(s["pts"][0][1] - a[1]) < 3:
                s2["pts"][0] = [round(b[0], 1), round(b[1], 1)]      # the riser still starts on the top yoke
            fe.append(s2)
    # classic mirrors: round, larger, on a taller stalk (beenster, 14 Nov)
    for s in fe:
        if s.get("mirror"):
            c = [sum(q[0] for q in s["pts"]) / len(s["pts"]), sum(q[1] for q in s["pts"]) / len(s["pts"])]
            n = [c[0] - 6, c[1] + 78]
            for t_ in fe:
                if t_.get("tube") and math.hypot(t_["pts"][-1][0] - c[0], t_["pts"][-1][1] - c[1]) < 45:
                    t_["pts"][-1] = [round(n[0] + 4, 1), round(n[1] - 40, 1)]
            s["pts"] = [[round(n[0] + 56 * math.cos(math.radians(k * 30)) * 0.62, 1), round(n[1] + 56 * math.sin(math.radians(k * 30)), 1)] for k in range(12)]
            s["cls"], s["t"] = "chromef", 1.0
    m["front_end"] = fe
    m["front_caliper"] = [[p[0] + wb_add, p[1] + FRONT_R - r0] + p[2:] for p in base["front_caliper"]]
    wb = m["geometry"]["wheelbase"]
    m["body"]["front_guard"] = [{"arc": {"c": [wb, FRONT_R], "r0": FRONT_R + 16, "r1": FRONT_R + 36, "a0": 48, "a1": 152}, "cls": "body"}]


def bench(m):
    """The classic's flat ribbed bench seat, rear mudguard, side covers and twin shocks."""
    b = m["body"]
    # a distinct two-seater: the rider's seat dips, then steps up to the pillion pad (beenster, 14 Nov)
    b["seat"] = [{"pts": [[668, 846, 1], [600, 838], [500, 832], [400, 836], [345, 848], [305, 868], [200, 874], [40, 874], [-50, 868], [-78, 850, 1],
                          [-76, 760, 1], [60, 752], [250, 750], [430, 752], [560, 756], [662, 790, 1]], "cls": "seat"},
                 {"pts": [[640, 828], [500, 816], [400, 820], [350, 832]], "cls": "stitch", "closed": False},
                 {"pts": [[338, 850], [326, 756]], "cls": "stitch", "closed": False},
                 {"pts": [[290, 858], [200, 862], [40, 862], [-40, 856]], "cls": "stitch", "closed": False}]
    # the rear mudguard is integrated into the tail: one sweep from under the seat down behind the wheel,
    # close over the tyre (one look, two parts in the game)
    b["tail"] = [{"pts": [[480, 762, 1], [300, 758], [60, 760], [-76, 764, 1], [-100, 726], [-172, 690], [-261, 633], [-333, 555], [-376, 474, 1], [-352, 464, 1],
                          [-311, 540], [-244, 613], [-161, 666], [-66, 696], [60, 708], [250, 714], [400, 722], [480, 736, 1]], "cls": "body", "t": 0.6},
                 {"pts": [[-205, 692, 1], [-246, 670, 1], [-262, 634, 1], [-232, 652, 1]], "cls": "redlens", "t": 0.3}]
    b["behind"] = []
    # support struts for both mudguards, and the plate under the rear one
    wb = m["geometry"]["wheelbase"]
    fr = m["wheels"]["front"]["tyre_r"]
    b["plate_hanger"] = [{"tube": 9, "pts": [[wb - 8, fr + 30], [wb - 290, fr + 172]], "cls": "steelt", "front": True},
                         {"tube": 9, "pts": [[wb + 8, fr + 30], [wb + 222, fr + 262]], "cls": "steelt", "front": True},
                         {"pts": [[-372, 468, 1], [-388, 474, 1], [-420, 384, 1], [-404, 378, 1]], "cls": "plate", "t": 0}]
    m["fit"] = dict(m.get("fit", {}), plate_hanger=True)
    # the side cover fills the whole triangle under the seat, back to the shock top (beenster, 14 Nov)
    b["panels"] = [{"pts": [[640, 772, 1], [560, 756], [430, 752], [262, 748, 1], [300, 700], [370, 640], [450, 596], [540, 578, 1], [645, 640, 1], [650, 715, 1]], "cls": "body", "t": 0.7}]
    # classics carry no pillion footpeg hanger: drop the high hanger tube and its peg
    hg = m.get("hangers", [])
    high = [h for h in hg if h.get("tube") and max(q[1] for q in h["pts"]) > 700]
    ends = [h["pts"][-1] for h in high]
    m["hangers"] = [h for h in hg if h not in high and not (h.get("circle") and any(math.hypot(h["at"][0] - e[0], h["at"][1] - e[1]) < 20 for e in ends))]
    sw = m["swingarm"]
    if sw.get("cls") == "alu" and not sw.get("shaft"):      # a two-sided swingarm takes the classic pair of shocks
        m["shock"] = {"top": [285, 744], "bottom": [95, 398], "twin": True}
    for n_, v in (("S1", [430, 800]), ("S2", [-40, 752]), ("S3", [300, 742]), ("S4", [250, 784])):
        if n_ in m["frame"]["nodes"]:
            m["frame"]["nodes"][n_] = v


def flat_pipes(m):
    """Classic exhausts run flat and low to a plain chrome silencer drawn in at the end (beenster, 14 Nov)."""
    ex = m["exhaust"]
    end = ex["headers"][0]["pts"][-1]
    if end[1] < 450 and ex.get("side") != "under" and not any(c.get("nip") for c in ex.get("cans", [])):
        z = min(max(end[1], 300), 345)
        ex["cans"] = [{"a": [end[0], z], "b": [-250, z - 6], "w": 84, "front": True, "nip": True}]


def hartwell(m):
    m["deck"] = {"nose": [650, 802], "tank_pitch": 0.5, "seat_pitch": 2, "seat_unit": True}
    b = m["body"]
    b["tank"] = [{"pts": copy.deepcopy(HART_TANK), "cls": "body"},
                 {"pts": [[1008, 952], [928, 982], [838, 978], [764, 948]], "cls": "hl", "closed": False},
                 {"pts": [[678, 861, 1], [738, 836], [828, 828], [908, 840], [883, 886], [778, 896, 1]], "cls": "plastic", "t": 0.8, "draw_only": True}]
    b["caps"] = [{"at": [908, 1008], "r": 16, "cls": "chromef"}]
    m["frame"]["nodes"]["B"] = [668, 792]
    for e in m["engine"].get("extras", []):      # the airbox comes down with the tank
        e["pts"] = [[q[0], min(q[1], 806)] + q[2:] for q in e["pts"]]
    for cy in m["engine"]["cylinders"]:
        cy["fins"] = True
    m["engine"]["layout"] = "parallel_twin"      # two finned barrels side by side, as its label says
    m.pop("radiator", None)
    # the pipe comes down the front of the engine and then runs flat all the way back past it
    m["exhaust"] = {"headers": [{"pts": [[985, 600], [992, 510], [1004, 440], [1004, 385], [984, 332], [935, 310], [700, 308], [520, 306]], "w": 44, "front": True}],
                    "cans": [{"a": [520, 306], "b": [-250, 302], "w": 84, "front": True, "nip": True}]}
    m["colours"] = dict(m["colours"], body="#1F4D3A", seat="#5a3a26", rim="#C9CED3", fork="#C9CED3")
    m["engine_label"], m["frame_label"] = "air-cooled look parallel twin", "steel tube cradle frame"


def own_tank(m):
    """A brand's own tank outline, moved onto the classic's lower and longer tank section."""
    c = HART_TANK
    b = m["body"]
    ct, bt = [p[0] for p in c], [p[0] for p in b["tank"][0]["pts"]]
    kx = (max(ct) - min(ct)) / (max(bt) - min(bt))
    d_ = dict({"nose": [650, 802], "tank_pitch": 0.5, "seat_pitch": 2, "seat_unit": True}, **m.pop("spec", {}))
    dz = d_["nose"][1] - m["deck"]["nose"][1]
    for s in b["tank"]:
        s["pts"] = [[round(min(ct) + (p[0] - min(bt)) * kx, 1), round(p[1] + dz, 1)] + p[2:] for p in s["pts"]]
    b["caps"] = [dict(s, at=[round(min(ct) + (s["at"][0] - min(bt)) * kx, 1), s["at"][1] + dz]) for s in b.get("caps", [])]
    for n_ in ("A", "B", "C"):          # the frame's top line comes down with the tank
        if n_ in m["frame"]["nodes"]:
            m["frame"]["nodes"][n_] = [m["frame"]["nodes"][n_][0], m["frame"]["nodes"][n_][1] + dz]
    m["deck"] = d_
    m["colours"] = dict(m["colours"], seat="#3a2a20")


def lodestar_base():
    """Lodestar has no naked: its classic takes the cruiser's 45 degree V-twin, double cradle and swingarm,
    stood up on the classic's wheels, fork and mid footrests (the range's shared pieces)."""
    from cruiser_draft import lodestar as cruiser
    c, m = cruiser(), copy.deepcopy(load("hartwell_naked"))
    m.pop("radiator", None)
    for k in ("engine", "frame", "swingarm", "colours", "brand", "engine_label", "frame_label"):
        m[k] = copy.deepcopy(c[k])
    x = m["engine"]["extras"]
    m["engine"]["extras"] = [x[0], x[3], x[4], {"pts": [[1004, 792, 1], [1100, 784, 1], [1082, 676, 1], [1012, 690, 1]], "cls": "plastic", "t": 0.6}]
    m["frame"]["nodes"]["B"] = [668, 792]
    m["frame"]["nodes"]["D1"] = [1056, 446]
    m["exhaust"] = {"headers": [{"pts": [[1016, 640], [1040, 545], [1036, 410], [1000, 300], [920, 262], [660, 258], [500, 256]], "w": 46, "front": True},
                                {"pts": [[650, 640], [585, 590], [530, 470], [480, 380], [440, 356]], "w": 46, "front": True}],
                    "cans": [{"a": [500, 258], "b": [-250, 262], "w": 80, "front": True, "nip": True},
                             {"a": [440, 356], "b": [-230, 360], "w": 80, "front": True, "nip": True}]}
    return m


def lodestar(m):
    m["deck"] = {"nose": [650, 802], "tank_pitch": 0.5, "seat_pitch": 2, "seat_unit": True}
    b = m["body"]
    # Lodestar's teardrop: full and high at the front, falling to the saddle
    b["tank"] = [{"pts": [[1066, 908, 1], [1040, 986], [956, 1020], [846, 1004], [752, 962], [684, 900], [640, 840, 1], [652, 796, 1], [1058, 800, 1], [1084, 858]], "cls": "body"},
                 {"pts": [[1016, 958], [944, 990], [850, 976], [770, 938]], "cls": "hl", "closed": False}]
    b["caps"] = [{"at": [950, 1018], "r": 16, "cls": "chromef"}]
    m["shock"] = {"top": [285, 744], "bottom": [60, 370], "twin": True}
    m["colours"] = dict(m["colours"], seat="#2a1c16", rim="#C9CED3", fork="#C9CED3")


HART_TANK = [[1066, 908, 1], [1028, 978], [940, 1010], [838, 1006], [748, 974], [684, 908], [640, 840, 1], [652, 796, 1], [1058, 800, 1], [1084, 858]]


def draft(name, spec):
    base = spec["base_fn"]() if "base_fn" in spec else load(spec["base"])
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
    "lodestar_classic": {"base_fn": lodestar_base, "title": "Lodestar classic (working title K1)", "style": [lodestar], "front_r": 314, "wb_add": 50},
}

if __name__ == "__main__":
    for n in (sys.argv[1:] or DRAFTS):
        print("classic drafted in", draft(n, DRAFTS[n]))
