"""First-draft cafe racers: a brand's naked frame, engine and tank line under the cafe family's shape.

The approved Hartwell cafe (beenster, 14 Nov: "cafe bike looks great") sets the family's shape: low
clip-ons, a round lamp, a long flat tank line, a short single seat and a humped tail. A brand's first
cafe draft keeps its own frame, engine, exhaust, colours and its own tank outline (stretched to the
cafe's longer tank section), and takes the seat, hump tail, front end and stance from the Hartwell.
The result is a model file like any other and is then restyled by hand.

    python3 foundry/cafe_draft.py            # (re)write the cafe racers listed in DRAFTS
"""
import copy
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sport_draft import ROOT, load, shift  # noqa: E402

SUB = {"S1": [430, 800], "S2": [161, 796], "S3": [455, 760], "S4": [250, 784]}


def draft(name, spec):
    base, c = load(spec["base"]), load("hartwell_cafe")
    m = copy.deepcopy(base)
    kit = m.pop("kit", None)
    m.update(name=spec["title"], family="Cafe", rev="A")
    wb = base["geometry"]["wheelbase"]
    dx = wb - c["geometry"]["wheelbase"]
    g = base["geometry"]
    m["geometry"] = dict(g, seat_height=c["geometry"]["seat_height"], hip_x=c["geometry"]["hip_x"],
                         ground_clearance=min(g["ground_clearance"], 160))
    m["deck"], m["fork"] = copy.deepcopy(c["deck"]), copy.deepcopy(c["fork"])
    m["wheels"]["rear"]["tyre_r"] = c["wheels"]["rear"]["tyre_r"]
    dr = c["wheels"]["front"]["tyre_r"] - m["wheels"]["front"]["tyre_r"]   # the brand keeps its own front tyre, for its engine's clearance
    m["front_caliper"] = [[p[0] + dx] + p[1:] for p in c["front_caliper"]]
    m["front_end"] = [shift(s, dx) for s in c["front_end"]]
    b = copy.deepcopy(c["body"])
    b["overall"] = [b["overall"][0], b["overall"][1] + dx]
    b["front_guard"] = [shift(s, dx) for s in c["body"]["front_guard"]]
    for s in b["front_guard"]:
        if "arc" in s:
            s["arc"].update(c=[s["arc"]["c"][0], s["arc"]["c"][1] - dr], r0=s["arc"]["r0"] - dr, r1=s["arc"]["r1"] - dr)
    # the brand's own tank outline, stretched onto the cafe's longer tank section
    ct = [p[0] for p in c["body"]["tank"][0]["pts"]]
    bt = [p[0] for p in base["body"]["tank"][0]["pts"]]
    kx = (max(ct) - min(ct)) / (max(bt) - min(bt))
    dz = c["deck"]["nose"][1] - base["deck"]["nose"][1]
    b["tank"] = []
    for s in base["body"]["tank"]:
        s = copy.deepcopy(s)
        s["pts"] = [[round(min(ct) + (p[0] - min(bt)) * kx, 1), round(p[1] + dz, 1)] + p[2:] for p in s["pts"]]
        b["tank"].append(s)
    # no separate arc over the rear tyre: these brands' silencers sit where the Hartwell's guard is
    b["panels"] = [s for s in b["panels"] if "arc" not in s]
    b["caps"] = [dict(s, at=[round(min(ct) + (s["at"][0] - min(bt)) * kx, 1), s["at"][1] + dz]) for s in base["body"].get("caps", [])]
    m["body"] = b
    for n_, v in dict(SUB, **spec.get("nodes", {})).items():
        if n_ in m["frame"]["nodes"]:
            m["frame"]["nodes"][n_] = v
    if "kit_exhaust" in spec:
        m["exhaust"] = copy.deepcopy(kit["exhaust"][spec["kit_exhaust"]]["exhaust"])
    sw = m["swingarm"]
    if sw.get("cls") == "alu" and not sw.get("shaft"):   # a two-sided swingarm takes the cafe's pair of shocks
        m["shock"] = copy.deepcopy(c["shock"])
    for fn in spec.get("style", []):
        fn(m)
    path = os.path.join(ROOT, "data", "models", name + ".json")
    json.dump(m, open(path, "w"), indent=1)
    open(path, "a").write("\n")
    return path


DRAFTS = {
    "corvano_cafe": {"base": "corvano_naked", "title": "Corvano cafe (working title C1)", "kit_exhaust": 2},
    "kazeda_cafe": {"base": "kazeda_naked", "title": "Kazeda cafe (working title C1)", "kit_exhaust": 0},
    "rheinhart_cafe": {"base": "rheinhart_naked", "title": "Rheinhart cafe (working title C1)", "nodes": {"S3": [300, 778]}},
}

if __name__ == "__main__":
    for n in (sys.argv[1:] or DRAFTS):
        print("cafe drafted in", draft(n, DRAFTS[n]))
