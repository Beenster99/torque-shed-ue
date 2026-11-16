"""BSX cafe rev H: beenster's round 4 markup (16 Nov 07:16) applied to the model file. Run once on rev G."""
import json, os
P = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data", "models", "hartwell_cafe.json")
m = json.load(open(P))
assert m["rev"] == "G", m["rev"]
m["rev"] = "H"
m["fit"]["hugger"] = False                                                      # no rear hugger
m["body"]["panels"] = [s for s in m["body"]["panels"] if "arc" not in s]       # nor the drawn arc over the rear tyre
m["detail"]["heel_plate"] = "swept"                                             # plain heel plates swept to the rear
json.dump(m, open(P, "w"), indent=1)
open(P, "a").write("\n")
