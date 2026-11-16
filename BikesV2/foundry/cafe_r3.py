"""Hartwell cafe rev G: beenster's round 3 markup (15 Nov 22:47) applied to the model file. Run once on rev F."""
import json, os
P = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data", "models", "hartwell_cafe.json")
m = json.load(open(P))
assert m["rev"] == "F", m["rev"]
m["rev"] = "G"
# no pillion seat, so no pillion pegs
m["hangers"] = [h for h in m["hangers"] if not (h.get("tube") == 14 or h.get("circle") == 13)]
m["detail"].pop("pillion_span", None)
# the header comes up in line with the megaphone's own axis
ex = m["exhaust"]
ex["headers"][0]["pts"] = ex["headers"][0]["pts"][:6] + [[760, 246], [700, 256], [640, 280], [585, 302], [545, 313], [505, 321.3]]
ex["cans"][0]["a"] = [500, 322.3]
# radiator moved to the spot marked on the round 2 picture: ahead of the down tube, leaning forward
m["radiator"] = {"at": [1012, 482], "angle": 83, "len": 215, "w": 40}      # as far forward as the 50 mm to the tyre at full compression allows, parallel to the down tube
# the airbox stops behind the cylinder head; the cover that sat in the head goes
x = m["engine"]["extras"]
x[0]["pts"] = [[680, 850], [800, 852], [880, 858], [845, 812], [808, 676], [780, 662], [690, 712]]
del x[2]
x.append({"pts": [[896, 806, 1], [990, 792, 1], [1004, 836, 1], [902, 848, 1]], "cls": "plastic", "t": 0.6})      # coil pack under the tank, clear above the head
m["detail"].update(hoses="tidy", clocks="deep", lamp="led")
json.dump(m, open(P, "w"), indent=1)
open(P, "a").write("\n")
print("rev", m["rev"])
