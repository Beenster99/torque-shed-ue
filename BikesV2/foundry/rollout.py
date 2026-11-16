"""Carries each family lead's signed-off Definition options (not its shapes) to the other bikes of the family.
Run after a lead is signed off: python3 foundry/rollout.py. Models carry kits, so the json is patched in place."""
import glob, json, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEADS = {"classic": "hartwell_classic", "cruiser": "lodestar_cruiser", "adventure": "corvano_adventure", "chopper": "lodestar_chopper", "mx": "varga_mx"}


def load(n):
    return json.load(open(os.path.join(ROOT, "data", "models", n + ".json")))


for fam, lead_n in LEADS.items():
    lead = load(lead_n)
    for p in sorted(glob.glob(os.path.join(ROOT, "data", "models", f"*_{fam}.json"))):
        n = os.path.basename(p)[:-5]
        if n == lead_n:
            continue
        m = json.load(open(p))
        d = dict(lead.get("detail") or {})
        if fam == "chopper" and not any(s.get("tube") for s in m["body"].get("tail", [])[1:]):
            d.pop("sissy", None)
        m["detail"] = dict(d, **{k: v for k, v in (m.get("detail") or {}).items() if k not in d})
        sh = m.get("shock")
        if sh and sh.get("twin"):
            sh["real"] = True
            if fam == "cruiser":
                sh["chrome"] = True
        ex = m["exhaust"]
        if fam in ("cruiser", "chopper"):
            ex["join"] = True
            for c in ex.get("cans", []):
                if not c.get("nip"):
                    c["open"] = True
        if fam == "chopper":
            ex["chrome"] = True
            m["frame"]["chrome"] = True
        if fam == "mx":
            for end in ("front", "rear"):
                for k in ("width", "spoke_fat", "single_disc"):
                    if k in lead["wheels"][end]:
                        m["wheels"][end][k] = lead["wheels"][end][k]
        json.dump(m, open(p, "w"), indent=1)
        print(n, m["detail"])
