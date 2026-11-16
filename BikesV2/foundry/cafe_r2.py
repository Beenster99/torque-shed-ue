"""BSX cafe rev F: beenster's round 2 markup (15 Nov) applied to the model file. Run once on rev E."""
import json, os
P = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data", "models", "hartwell_cafe.json")
m = json.load(open(P))
assert m["rev"] == "E", m["rev"]
m["rev"] = "F"
b, LIFT = m["body"], 14
# 1 tank lower and slimmer
b["tank"][0]["pts"] = [[1064, 972, 1], [1012, 1022], [921, 1040], [820.5, 1034], [731.3, 1008], [669.9, 956], [623, 900, 1], [642, 850, 1], [1077.2, 862, 1], [1086, 920]]
b["tank"][1]["pts"] = [[1021.4, 996], [921, 1016], [826.1, 1008], [748, 982]]
b["tank"][2]["pts"] = [[658.7, 900], [764.7, 916], [876.3, 944], [1060.5, 950]]
b["caps"][0]["at"] = [909.8, 1038]
m["widths"]["tank"] = {"w": [[0, 88], [0.25, 130], [0.6, 150], [0.85, 132], [1, 98]], "n": 2.9, "bottom": 0.62}
# 2 saddle lifted; the seat rail leaves the backbone lower so it runs under the saddle, not through it
for s in b["seat"]:
    s["pts"] = [[p[0], p[1] + LIFT] + p[2:] for p in s["pts"]]
sp = b["seat"][0]["pts"]
sp[9], sp[10] = [560, 814], [640, 842, 1]
keep = {(158.7, 782), (350, 755), (470, 745)}
b["tail"][0]["pts"] = [p if (p[0], p[1]) in keep else [p[0], p[1] + LIFT] + p[2:] for p in b["tail"][0]["pts"]]
m["frame"]["nodes"]["R1"] = [668, 822]
for t in m["frame"]["tubes"]:
    if t["path"] == ["B", "S2"]:
        t["path"] = ["R1", "S2"]
# 3 the frame loops round the back of the saddle and carries the lamp and indicators; no lamp let into the hump
b["tail"] = [s for s in b["tail"] if s.get("cls") != "redlens"]
m["frame"]["loop"] = {"w": 22, "pts": [[120, 103, 800], [40, 89, 818], [-20, 77, 833], [-50, 50, 838], [-64, 0, 839]]}
# 4, 5, 6 bar-end mirrors, shallow dial cans, steel levers; 7 pillion hanger bolts spread into a triangle
m["detail"] = dict(m.get("detail", {}), mirrors="barend", levers="steel", clocks="can", pillion_span=135, indicators="trad")
m["front_end"] = [s for s in m["front_end"] if not s.get("mirror") and not (s.get("tube") == 8 and s["pts"][1][1] > 1000)]
# 8 the side panel fills the frame triangle under the saddle
b["panels"][0]["pts"] = [[664, 826, 1], [560, 808], [430, 794], [326, 774, 1], [348, 742], [430, 652], [512, 584, 1], [560, 588, 1], [650, 640, 1], [660, 720, 1]]
# 9 mudguard variation: a shorter bobbed blade, longer behind the fork than ahead of it
b["front_guard"][0]["arc"].update(a0=86, a1=142)
# 10 black cast wheels
for w in ("front", "rear"):
    m["wheels"][w].update(style="sweep7", finish="gloss_black")
# 11 a squarer, box-section swingarm
m["swingarm"].update(outline=[[575, 468, 1], [440, 474, 1], [120, 410, 1], [-36, 374, 1], [-50, 300, 1], [30, 274, 1], [430, 338, 1], [575, 372, 1]], square=True)
# 12, 14 megaphone silencer, fatter header
ex = m["exhaust"]
ex["headers"][0]["w"] = 54
for q in ex["headers"][0]["pts"]:      # the fatter pipe keeps its 25 mm from the front tyre at full compression
    if q[0] >= 1000 and q[1] < 450:
        q[0] -= 6
ex["cans"][0].update(a=[540, 314], b=[40, 418], w=84, mega=132)
# 13 bigger headlamp (about 170 mm across)
cx, cz, k = 1250.0, 873.0, 1.24
for s in m["front_end"]:
    if s.get("part") == "head" and "pts" in s:
        s["pts"] = [[round(cx + (p[0] - cx) * k + 14, 1), round(cz + (p[1] - cz) * k + 8, 1)] + p[2:] for p in s["pts"]]
# 15, 16 real shocks, their bottom eyes on lugs by the rear hub
m["shock"].update(bottom=[46, 418], real=True)
json.dump(m, open(P, "w"), indent=1)
open(P, "a").write("\n")
print("rev", m["rev"])
