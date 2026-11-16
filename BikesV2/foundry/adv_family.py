"""Give every other adventure bike its own shaped bodywork (model["adv"]) from the signed-off lead
(corvano_adventure): the lead's nose, mask, panels, screen and rack, restyled to each brand's signature.
Run once: python3 foundry/adv_family.py   (writes data/models/<brand>_adventure.json)"""
import copy
import json
import math
import os

ROOT = os.path.join(os.path.dirname(__file__), "..", "data", "models")
LEAD = "corvano_adventure"


def circle_half(r, zc, a0=0, a1=180, n=14):
    return [[round(r * math.sin(math.radians(a0 + (a1 - a0) * k / n)), 1), round(zc + r * math.cos(math.radians(a0 + (a1 - a0) * k / n)), 1)] for k in range(n + 1)]


def slots(x0, pitch, n, w=24, lean=38):
    return [[[x0 + pitch * k, 700], [x0 + pitch * k + w, 700], [x0 + pitch * k + w + lean, 872], [x0 + pitch * k + lean, 872]] for k in range(n)]


# each brand's own face: beak (kx length, ky width, kz depth, lift, droop per mm), lamps, louvres, screen height, wings
STYLE = {
    # British, classic and sculpted: a deep rounded stub of a beak, one big round lamp in a bright ring, a tall rounded screen
    "hartwell": {"beak": (0.5, 1.05, 1.3, 0, 0.0), "eye": circle_half(47, 1046), "drl": circle_half(47, 1046, 4, 176) + circle_half(41, 1046, 4, 176)[::-1],
                 "proj": [[19, 1046]], "slots": slots(1096, 62, 2, 28), "screen": 1.25, "wing": 58, "louvre_paint": True},
    # Japanese, angular and layered: a long thin beak that droops to a point, a slit running light over a pair of projectors
    "kazeda": {"beak": (1.32, 0.85, 0.7, 0, -0.12), "eye": [[12, 1058], [92, 1074], [99, 1090], [60, 1092], [12, 1074]],
               "drl": [[12, 1074], [60, 1092], [99, 1090], [97, 1084], [60, 1085], [12, 1067]], "proj": [[38, 1028], [70, 1034]],
               "slots": slots(1072, 42, 4, 20), "screen": 0.88, "wing": 96,
               # its own cutaway: the black panel ends in a raked notch behind the louvres
               "black": [[958, 903, 1], [1290, 898, 1], [1345, 930, 1], [1318, 840], [1270, 740], [1230, 660, 1], [1150, 690, 1], [1060, 680, 1], [1040, 760], [1000, 830, 1]]},
    # German, functional and blocky: a broad flat duck bill, two big square lamps under a light bar, the tallest screen
    "rheinhart": {"beak": (1.0, 1.35, 0.58, 0, 0.0), "eye": [[10, 1020], [96, 1024], [96, 1070], [10, 1070]],
                  "drl": [[10, 1075], [96, 1075], [96, 1084], [10, 1084]], "proj": [[34, 1045], [72, 1046]],
                  "slots": slots(1090, 72, 2, 40), "screen": 1.42, "wing": 70},
    # Central European, tall and flat sided: a short rally beak set high with its tip turned up, a vertical lamp stack, a low screen
    "varga": {"beak": (0.7, 0.85, 1.0, 34, 0.14), "eye": [[0, 1042], [25, 1044], [29, 1090], [0, 1093]],
              "drl": [[34, 1044], [41, 1046], [47, 1088], [38, 1090]], "proj": [[0, 1055], [0, 1079]],
              "slots": slots(1100, 50, 3, 22, 22), "screen": 0.74, "wing": 46,
              # its own cutaway, and painted: the panel is scooped away behind the front wheel and carries the bike's colour
              "black": [[958, 903, 1], [1290, 898, 1], [1345, 930, 1], [1330, 860, 1], [1270, 800], [1250, 690, 1], [1196, 640, 1], [1096, 618, 1], [1024, 684], [978, 790]], "black_paint": True},
}


def bbox(shp):
    xs = [p[0] for p in shp["pts"]]
    return min(xs), max(xs)


def run():
    lead = json.load(open(os.path.join(ROOT, LEAD + ".json")))
    for brand, st in STYLE.items():
        path = os.path.join(ROOT, f"{brand}_adventure.json")
        m = json.load(open(path))
        adv = copy.deepcopy(lead["adv"])
        kx, ky, kz, lift, droop = st["beak"]
        x0 = adv["beak"][0][0]
        adv["beak"] = [[round(x0 + (x - x0) * kx, 1), [[round(y * ky, 1), round(900 + (z - 900) * kz + lift + droop * (x - x0) * kx, 1)] for y, z in sec]] for x, sec in adv["beak"]]
        adv["eye"], adv["drl"], adv["proj"], adv["slots"] = st["eye"], st["drl"], st["proj"], st["slots"]
        adv["wing"]["h"] = st["wing"]
        ks = st["screen"]
        b0 = adv["screen"]["line"][0]
        adv["screen"]["line"] = [[round(b0[0] + (x - b0[0]) * ks, 1), round(b0[1] + (z - b0[1]) * ks, 1)] for x, z in adv["screen"]["line"]]
        # the lead's level tail, low rack and number plate arm (beenster, rollout round 1: the tail must not turn
        # up, the grab rail sits low, the plate joins the tail properly)
        up = lambda x, z: z
        m["body"]["tail"] = copy.deepcopy(lead["body"]["tail"])
        m["body"]["plate_hanger"] = copy.deepcopy(lead["body"]["plate_hanger"])
        if st.get("louvre_paint"):
            adv["louvre_paint"] = True
        if st.get("black"):
            adv["black"] = st["black"]
        if st.get("black_paint"):
            adv["black_paint"] = True
        # the front mudguard sits down close to the tyre, as the lead's does (beenster, rollout round 2)
        for s in m["body"]["front_guard"]:
            if "arc" in s:
                s["arc"]["r0"], s["arc"]["r1"] = 338, 360
        if brand == "hartwell":
            # the lead's pillion seat, so the seat frame's hoop and the rack sit under and beside it, not across it
            ls = lead["body"]["seat"]
            own = m["body"]["seat"][0]["pts"]
            i0 = next(i for i, q in enumerate(own) if q[0] <= 404)
            i1 = max(i for i, q in enumerate(own) if q[0] <= 404)
            m["body"]["seat"][0]["pts"] = own[:i0] + copy.deepcopy([q for q in ls[0]["pts"] if q[0] <= 404]) + own[i1 + 1:]
            m["body"]["seat"][2] = copy.deepcopy(ls[2])
        m["adv"] = adv
        # the drawing: drop the old flat fairing, lamp and rack; carry the lead's shaped parts with this brand's beak and louvres
        keep = [s for s in m["body"]["panels"] if not s.get("adv") and ("pts" not in s or (bbox(s)[1] < 960 and not s.get("tube")))]
        new = []
        nsl = 0
        for s in lead["body"]["panels"]:
            if not s.get("adv"):
                continue
            s = copy.deepcopy(s)
            if s.get("cls") == "hl":
                if nsl == 0:
                    new += [{"pts": [sl[0], sl[3]], "cls": "hl", "closed": False, "adv": True} for sl in st["slots"]]
                nsl += 1
                continue
            if "tube" not in s and bbox(s)[0] == x0 and s.get("cls") == "plastic":
                top = [[x, sec[0][1]] for x, sec in adv["beak"]]
                bot = [[x, sec[-1][1]] for x, sec in adv["beak"]]
                s["pts"] = [top[0] + [1]] + top[1:-1] + [top[-1] + [1], bot[-1] + [1], bot[0] + [1]]
            if "tube" in s:
                s["pts"] = [[x, up(x, z) if x < 200 and z > 960 else z] for x, z in s["pts"]]
            new.append(s)
        m["body"]["panels"] = keep + new
        fe = m["front_end"]
        for k, s in enumerate(fe[:2]):
            if s.get("part") == "head":
                fe[k] = copy.deepcopy(lead["front_end"][k])
                if fe[k].get("cls") == "screen":
                    fe[k]["pts"] = [[round(b0[0] + (p[0] - b0[0]) * ks, 1), round(b0[1] + (p[1] - b0[1]) * ks, 1)] + p[2:] for p in fe[k]["pts"]]
        json.dump(m, open(path, "w"), indent=1)
        print(brand, "panels", len(m["body"]["panels"]))


if __name__ == "__main__":
    run()
