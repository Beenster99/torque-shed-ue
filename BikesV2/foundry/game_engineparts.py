"""The engine parts pass into the game (plan https://claude.ai/artifact/TxPXHSRkftpdYsppy6dhXt, stage 6): every look of
stages 2 to 4 drawn on every V2 engine, cut to its slot and sold as a shop part of that engine.

Each engine type's looks are drawn on each engine of the type from its own joints (its inlet stubs, cover rings, cases
and header run), so every engine has every look of its type, bolted to that engine: the shop part fits its engine
block the way the bike's own part does ("engine" fit), and an open air intake fits only the fuel system it was drawn
on (v2part "with_fuel"; a carb needs an open intake: "open_air").

One Blender build draws a bike with one look in each slot at once (a fuel system and air intake, covers and cases, a
header finish, an oil filter finish); the build is cut into one file per slot it changed:

    Data/BikesV2/kits/<model>__e_<slot>_<look>.glb + .json      (game_kits.cut's format)

    python3 foundry/game_engineparts.py build [model ...]     # 4 models side by side (resumable: kits/_done.txt)
    python3 foundry/game_engineparts.py one <model>           # one model's builds (what build runs)
    python3 foundry/game_engineparts.py catalogue             # the shop entries (game_kits.catalogue calls it)
"""
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.dirname(ROOT)
DATA = os.path.join(GAME, "Data", "BikesV2")
KITS = os.path.join(DATA, "kits")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

FUEL_WORD = {"throttle_bodies": "throttle bodies", "round_slide": "round-slide carbs", "flat_slide": "flat-slide carbs"}
AIR_WORD = {"pods": "pod filters", "stacks": "velocity stacks"}
COVER_WORD = {"cast": "cast", "finned": "finned", "billet": "billet"}
FINISH_WORD = {"alloy": "natural alloy", "black": "black", "polished": "polished", "gold": "gold"}
CASE_WORD = {"ribbed": "ribbed cases", "crackle": "black crackle cases"}
HEADER_WORD = {"race": "race headers", "ceramic": "ceramic headers", "wrapped": "wrapped headers"}


def plan(model):
    """The builds for one model: [{fuel, air, oil, cover, finish, case, header, sells: {slot: look key}}]."""
    import engine_joints
    et = engine_joints.engine_type(model)
    two = et == "two_stroke"
    fa = []
    for air in ("pods", "stacks"):            # (pods first: each fuel system is sold from its pods build)
        for fuel in (("throttle_bodies",) if two else ("throttle_bodies", "round_slide", "flat_slide")):
            fa.append((fuel, air))
    boxer = et == "boxer"
    cov = []
    if not boxer:
        cov += [("cast", f, "stock") for f in ("black", "polished", "gold")]
        cov += [(c, f, "stock") for c in ("finned", "billet") for f in ("alloy", "black", "polished", "gold")]
    cov += [("cast", None, "ribbed"), ("cast", None, "crackle")]
    hdr = ["stock", "ceramic", "wrapped"] + ([] if two else ["race"])
    oil = ["black", "chrome"]
    n = max(len(fa), len(cov), len(hdr), len(oil))
    out = []
    for i in range(n):
        b = dict(fuel="throttle_bodies", air="airbox", oil=None, cover="cast", finish=None, case="stock", header="stock", sells={})
        if i < len(fa):
            b["fuel"], b["air"] = fa[i]
            fk = f"{'own' if two else b['fuel']}"
            if b["air"] == "pods" and not two:
                b["sells"]["fuel"] = b["fuel"]
            b["sells"]["air_filter"] = f"{b['air']}_{fk}"
        if i < len(cov):
            b["cover"], b["finish"], b["case"] = cov[i]
            b["sells"]["gearbox"] = "_".join(x for x in cov[i] if x and x != "stock")
        if i < len(hdr) and hdr[i] != "stock":
            b["header"] = hdr[i]
            b["sells"]["exhaust"] = hdr[i]
        if i < len(oil):
            b["oil"] = oil[i]
            b["sells"]["oil_filter"] = oil[i]
        out.append(b)
    return out


def one(mn):
    """Every build of one model, in this Blender: each drawn, fitted, cut to the slots it sells."""
    import bpy
    import bars3d as BR
    import build3d
    import covers3d
    import exhaust3d
    import game_kits
    import game_parts
    import intake3d
    done_path = os.path.join(KITS, "_done.txt")
    done = set(open(done_path).read().split("\n")) if os.path.exists(done_path) else set()
    model0 = BR.models()[mn]
    bends = {}
    base_clips = None
    build3d.TYRE_LOD["lod"] = "lod1"
    for i, b in enumerate(plan(model0)):
        tag = f"{mn} e{i}"
        if tag in done:
            continue
        BR.fresh()
        bore = None
        if b["header"] == "race":
            bore = 1.06
        m = exhaust3d.prepare(model0, b["header"], bore)
        build3d.DETAIL["on"] = True
        build3d.build(m)
        intake3d.use(build3d)
        bk = bends.get(b["fuel"]) if b["air"] == "stacks" else None
        rep, _made = intake3d.fit(build3d, m, b["fuel"], b["air"], b["oil"], bk)
        if b["air"] == "pods":
            bends[b["fuel"]] = {p["port"]: (p["bend"], p["roll"]) for p in rep["ports"]}
        covers3d.fit(build3d, m, b["cover"], b["finish"], b["case"])
        _r, pipes = exhaust3d.fit(build3d, m, b["header"])
        sells = dict(b["sells"])
        if b["header"] in ("stock", "race"):
            clips = {k: v for k, v in BR.clip_check(exhaust3d.meshed(pipes)).items()}
            if b["header"] == "stock":
                base_clips = clips
            elif base_clips is not None:
                ok = re.compile(r"^(header|headerbranch|headermerge|port|can|collector|exhaust)")
                bad = {k: v for k, v in clips.items() if not ok.match(k.split("|")[1].strip()) and v > base_clips.get(k, -1) + 1.0}
                if bad:
                    print(f"NOTE {mn} race headers touch {bad}: not sold", flush=True)
                    sells.pop("exhaust", None)
        # only the slots this build sells go into the file
        keep = set(sells)
        for o in list(bpy.data.objects):
            if o.type not in ("MESH", "CURVE"):
                continue
            r = game_parts.classify(o.name, m)
            if not r or r[0] not in keep:
                bpy.data.objects.remove(o, do_unlink=True)
        name = f"_e_{mn}_{i}"
        build3d.export_game(name, m)
        glb, js = os.path.join(DATA, name + ".glb"), os.path.join(DATA, name + ".json")
        g, blob = game_kits.read_glb(glb)
        info = json.load(open(js))
        have = {v["slot"] for v in info["surfaces"].values()}
        for slot, look in sorted(sells.items()):
            if slot not in have:
                print(f"NOTE {mn} e{i}: no {slot} drawn", flush=True)
                continue
            meta = {"engine_part": {"slot": slot, "look": look, "fuel": b["fuel"], "air": b["air"], "cover": b["cover"], "finish": b["finish"],
                                    "case": b["case"], "header": b["header"], "oil": b["oil"]}}
            tris = game_kits.cut(g, blob, info, [slot], os.path.join(KITS, f"{mn}__e_{slot}_{look}"), meta)
            print(f"engine part {mn} {slot} {look}: {tris} triangles", flush=True)
        os.remove(glb)
        os.remove(js)
        with open(done_path, "a") as f:
            f.write(tag + "\n")
    print("ALL", mn, flush=True)


def build(models):
    os.makedirs(KITS, exist_ok=True)

    def run(mn):
        r = subprocess.run([sys.executable, os.path.abspath(__file__), "one", mn], cwd=ROOT, capture_output=True, text=True, timeout=6 * 3600)
        lines = [ln for ln in r.stdout.split("\n") if ln.startswith(("engine part", "NOTE", "ALL"))]
        if r.returncode:
            lines.append(f"FAILED {mn}: {(r.stdout + r.stderr)[-600:]}")
        return lines
    with ThreadPoolExecutor(4) as ex:
        for lines in ex.map(run, models):
            for ln in lines:
                print(ln, flush=True)


# ---------------------------------------------------------------- the shop
FUEL_PERF = {"round_slide": {"torque_mult": 1.02, "peak_shift": 0.98}, "flat_slide": {"torque_mult": 0.97, "peak_shift": 1.04}}
AIR_PERF = {"pods": {"flow": 1.03, "dirt": 1.2}, "stacks": {"flow": 1.02, "dirt": 1.6}}
FUEL_TYPE = {"throttle_bodies": "Throttle bodies", "round_slide": "Round-slide carburettors", "flat_slide": "Flat-slide carburettors"}


def entries(parts, model, file, info):
    """One shop part for an engine part file: a copy of the bike's own part of that slot (its numbers, what it fits),
    drawn from the file, with the look's performance."""
    e_ = info["engine_part"]
    slot, look = e_["slot"], e_["look"]
    own_id = f"v2_{model}__{slot}"
    kind = next((k for k, es in parts.items() if isinstance(es, dict) and own_id in es), None)
    own = None
    if not kind and slot == "oil_filter":
        # (The V2 bikes take the shop's own oil filters, so the canister looks are paper filters, drawn as the engine's.)
        eng = (parts.get("engine") or {}).get(f"v2_{model}__engine")
        if eng:
            base = json.load(open(os.path.join(GAME, "Data", "Parts", "parts.json")))["oil_filter"]["paper_oil"]
            ev = eng["v2part"]
            own = dict(base, price=25, v2part={k: ev[k] for k in ("brand", "family", "layout") if k in ev} | dict(fits="engine"))
            kind = "oil_filter"
            parts.setdefault(kind, {})
    if not kind:
        print(f"NOTE no shop part {own_id}: engine part {file} not sold")
        return 0
    e = json.loads(json.dumps(parts[kind][own_id] if own_id in parts[kind] else own))
    vp = e["v2part"]
    who = f"{vp.get('brand', '')} {vp.get('family', '')}".strip()
    if slot == "fuel":
        fuel = e_["fuel"]
        e["name"] = f"{who} {FUEL_WORD[fuel]}" + (" (for open intakes)" if fuel == "throttle_bodies" else "")
        e["description"] = {"throttle_bodies": "The engine's throttle bodies with open mouths, for pod filters or velocity stacks.",
                            "round_slide": "A cast round-slide carb on each port: a little more pull, a little less top end. Needs pod filters or velocity stacks.",
                            "flat_slide": "Square flat-slide race carbs: sharper and stronger at the top, softer low down. Needs pod filters or velocity stacks."}[fuel]
        for k, f in FUEL_PERF.get(fuel, {}).items():
            e[k] = round(float(e.get(k, 1.0)) * f, 4)
        e["type"] = FUEL_TYPE[fuel]
        if fuel != "throttle_bodies":
            e["injected"] = False
        vp["open_air"] = True
    elif slot == "air_filter":
        air, fk = look.split("_", 1)
        fuel_id = own_id.replace("__air_filter", "__fuel") + ("" if fk == "own" else f"_e{fk}")
        e["name"] = f"{who} {AIR_WORD[air]}" + ("" if fk == "own" else f" for the {FUEL_WORD[fk]}")
        e["description"] = {"pods": "An open cone filter on each mouth: red pleated gauze, a chrome clamp and cap. More top end; mind the dust.",
                            "stacks": "A polished trumpet on each mouth with a fine mesh: the most top end, a touch less low down, and no filter to speak of."}[air]
        e["flow"] = round(float(e.get("flow", 1.0)) * AIR_PERF[air]["flow"], 4)
        e["dirt_sensitivity"] = AIR_PERF[air]["dirt"]
        vp["with_fuel"] = fuel_id
    elif slot == "gearbox":
        bits = look.split("_")
        if bits[0] in CASE_WORD or (len(bits) == 2 and bits[1] in CASE_WORD):
            word = CASE_WORD[bits[-1]]
            e["name"] = f"{who} gearbox, {word}"
        else:
            e["name"] = f"{who} gearbox, {COVER_WORD[bits[0]]} covers in {FINISH_WORD[bits[1]]}"
        e["description"] = "The engine's gearbox and side cases, dressed: " + e["name"].split(", ", 1)[1] + ". Looks only."
    elif slot == "exhaust":
        e["name"] = f"{who} {HEADER_WORD[look]}"
        e["description"] = {"race": "Bigger-bore stainless headers on the bike's own run, the silencers' sleeves opened to suit: a full system, a little more pull.",
                            "ceramic": "The bike's own header pipes in a satin black ceramic coat. Looks only.",
                            "wrapped": "Woven heat wrap over the header pipes. Looks only."}[look]
        if look == "race":
            e["torque_mult"] = round(float(e.get("torque_mult", 1.0)) * 1.04, 4)
            e["mass"] = round(float(e.get("mass", 7.0)) * 0.85, 2)
    elif slot == "oil_filter":
        e["name"] = f"{who} oil filter, {look}"
        e["description"] = f"The engine's oil filter in a {look} canister. Serviced as ever."
    else:
        return 0
    if isinstance(e.get("price"), (int, float)):
        e["price"] = int(round(e["price"] * (1.35 if slot in ("fuel", "exhaust") else 1.2) / 5.0) * 5)
    vp.update(model=f"kits/{file}", frame=f"v2_{model}", slot=slot, kit=f"e_{look}", of=model)
    pid = own_id + f"_e{look}"
    parts[kind][pid] = e
    return 1


if __name__ == "__main__":
    allm = sorted(f[:-5] for f in os.listdir(os.path.join(ROOT, "data", "models")) if f.endswith(".json"))
    if sys.argv[1:2] == ["build"]:
        build(sys.argv[2:] or allm)
    elif sys.argv[1:2] == ["one"]:
        one(sys.argv[2])
