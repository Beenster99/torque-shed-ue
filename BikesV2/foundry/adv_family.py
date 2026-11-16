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
    # British, classic and sculpted: a stub of a beak, one big round lamp, a tall rounded screen
    "hartwell": {"beak": (0.6, 1.0, 1.0, 0, 0.0), "eye": circle_half(43, 1045), "drl": circle_half(43, 1045, 8, 84) + circle_half(37, 1045, 8, 84)[::-1],
                 "proj": [[19, 1045]], "slots": slots(1096, 62, 2, 28), "screen": 1.06, "wing": 58},
    # Japanese, angular and layered: a long sharp beak that droops, a slit running light over a pair of projectors
    "kazeda": {"beak": (1.18, 0.92, 0.9, 0, -0.11), "eye": [[14, 1060], [90, 1074], [97, 1088], [62, 1090], [14, 1074]],
               "drl": [[14, 1074], [62, 1090], [97, 1088], [95, 1083], [62, 1084], [14, 1068]], "proj": [[38, 1030], [68, 1036]],
               "slots": slots(1072, 42, 4, 20), "screen": 0.88, "wing": 96},
    # German, functional and blocky: a broad flat beak, square lamps under a light bar, the tallest screen
    "rheinhart": {"beak": (0.92, 1.2, 0.72, 0, 0.0), "eye": [[12, 1028], [92, 1032], [92, 1070], [12, 1070]],
                  "drl": [[12, 1074], [92, 1074], [92, 1082], [12, 1082]], "proj": [[33, 1049], [68, 1050]],
                  "slots": slots(1090, 72, 2, 40), "screen": 1.13, "wing": 70},
    # Central European, tall and flat sided: a short high rally beak, a vertical lamp stack, a low screen
    "varga": {"beak": (0.8, 0.9, 0.9, 20, 0.0), "eye": [[0, 1032], [22, 1034], [26, 1086], [0, 1090]],
              "drl": [[31, 1034], [38, 1036], [44, 1084], [35, 1086]], "proj": [[0, 1048], [0, 1074]],
              "slots": slots(1100, 50, 3, 22, 22), "screen": 0.74, "wing": 46},
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
        # the tail on these bikes kicks up at the back, so the rack stands higher over it
        up = lambda x, z: round(z + 16 + 0.2 * max(0.0, 60 - x), 1)
        rk = adv["rack"]
        rk["rail"] = [[x, y, up(x, z) if x < 200 else z] for x, y, z in rk["rail"]]
        rk["legs"] = [[[lg[0][0], lg[0][1], up(lg[0][0], lg[0][2])], [lg[1][0], lg[1][1], round(lg[1][2] + 0.5 * (up(lg[0][0], lg[0][2]) - lg[0][2]), 1)], lg[2]] for lg in rk["legs"]]
        lens = [s for s in m["body"]["tail"] if s.get("cls") == "redlens"]
        if lens:
            xs, zs = [p[0] for p in lens[0]["pts"]], [p[1] for p in lens[0]["pts"]]
            adv["tail_lamp"]["at"] = [round(sum(xs) / len(xs) - 2), round(sum(zs) / len(zs))]
        adv.pop("loop", None)
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
