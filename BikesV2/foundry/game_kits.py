"""The kit variants as shop parts (plan stage 3: "kits in the shop"; beenster, 18 Nov: "move on through the plan").

A bike's kit (data/models/<model>.json "kit": tank and seat pairs, tails, fronts, exhausts) is built whole by
build3d (`--kit 0,n,0,0 --definition --game`, one group at a time), then cut down here to the slots that variant changes:

    Data/BikesV2/kits/<model>__<group><n>.glb + .json   (the same format as a bike's files, only those surfaces)

and sold as a V2 shop part "v2_<model>__<slot>_k<group letter><n>" whose "v2part" names that file as its model and
the bike's own frame as its frame, so the game draws it exactly as it draws another V2 bike's part (BikeParts
FitV2's donors). Bars, extras and paint variants are not sold yet (bars move the rider's grips).

    python3 foundry/game_kits.py build [model ...]    # build and cut (3 at a time; about 2 minutes a build)
    python3 foundry/game_kits.py bespoke [model ...]  # the chopper bespoke parts (pool.json), each design in each metal
    python3 foundry/game_kits.py catalogue             # the shop entries, into parts_v2.json (game_catalogue.py calls it)
"""
import json
import os
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.dirname(ROOT)
DATA = os.path.join(GAME, "Data", "BikesV2")
KITS = os.path.join(DATA, "kits")

GROUPS = ("tank_seat", "tail", "front", "exhaust")
# what a group may change (a slot outside these that differs from the bike as drawn is reported, not sold)
SLOTS = {"tank_seat": ("tank", "saddle"), "tail": ("tail", "saddle"), "front": ("headlight", "fairing"), "exhaust": ("exhaust",)}
LETTER = {"tank_seat": "t", "tail": "r", "front": "f", "exhaust": "x"}
WORD = {"tank": "tank", "saddle": "seat", "tail": "tail", "headlight": "headlamp", "fairing": "fairing", "exhaust": "exhaust"}


def read_glb(path):
    d = open(path, "rb").read()
    n = struct.unpack("<I", d[12:16])[0]
    return json.loads(d[20:20 + n]), d[20 + n + 8:]


def slot_shapes(g, info):
    """slot -> a signature of its drawing (surfaces, vertices, bounds to the millimetre)."""
    out = {}
    for pr in g["meshes"][0]["primitives"]:
        look = info["surfaces"][g["materials"][pr["material"]]["name"]]
        a = g["accessors"][pr["attributes"]["POSITION"]]
        s = out.setdefault(look["slot"], [0, 0, [1e9] * 3, [-1e9] * 3])
        s[0] += 1
        s[1] += a["count"]
        s[2] = [min(p, q) for p, q in zip(s[2], a["min"])]
        s[3] = [max(p, q) for p, q in zip(s[3], a["max"])]
    return {k: (v[0], v[1], tuple(round(q, 3) for q in v[2] + v[3])) for k, v in out.items()}


def cut(g, blob, info, slots, dst, meta=None):
    """Writes dst.glb / dst.json holding only the surfaces in these slots. Returns the triangles kept."""
    out = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": os.path.basename(dst)}],
           "meshes": [{"primitives": []}], "materials": [], "accessors": [], "bufferViews": [], "buffers": [{}]}
    data, looks, tris = bytearray(), {}, 0

    def take(i):
        a = dict(g["accessors"][i])
        bv = g["bufferViews"][a["bufferView"]]
        assert not bv.get("byteStride"), "interleaved buffer"
        size = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]] * {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}[a["componentType"]]
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        while len(data) % 4:
            data.append(0)
        view = {"buffer": 0, "byteOffset": len(data), "byteLength": size * a["count"]}
        if "target" in bv:
            view["target"] = bv["target"]
        data.extend(blob[off:off + size * a["count"]])
        a["bufferView"] = len(out["bufferViews"])
        a.pop("byteOffset", None)
        out["bufferViews"].append(view)
        out["accessors"].append(a)
        return len(out["accessors"]) - 1

    for pr in g["meshes"][0]["primitives"]:
        name = g["materials"][pr["material"]]["name"]
        look = info["surfaces"][name]
        if look["slot"] not in slots:
            continue
        p = {"attributes": {k: take(v) for k, v in pr["attributes"].items() if k in ("POSITION", "NORMAL")}, "indices": take(pr["indices"]),
             "material": len(out["materials"])}
        if "mode" in pr:
            p["mode"] = pr["mode"]
        tris += g["accessors"][pr["indices"]]["count"] // 3
        out["materials"].append({"name": name})
        out["meshes"][0]["primitives"].append(p)
        looks[name] = look
    while len(data) % 4:
        data.append(0)
    out["buffers"][0]["byteLength"] = len(data)
    js = json.dumps(out, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    with open(dst + ".glb", "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(js) + 8 + len(data)))
        f.write(struct.pack("<I4s", len(js), b"JSON") + js)
        f.write(struct.pack("<I4s", len(data), b"BIN\0") + bytes(data))
    json.dump(dict({"name": info["name"], "format": 2, "kit": True, "triangles": tris, "surfaces": looks}, **(meta or {})), open(dst + ".json", "w"), indent=1)
    return tris


def build_one(model, grp, n):
    """One build with variant n of one group (alone, so what differs from the bike as drawn is that variant's
    doing), cut to the slots it changes. Returns report lines."""
    ka = [n if g == grp else 0 for g in GROUPS]
    tag = "".join(str(x) for x in ka)
    name = f"{model}_kit{tag}_def"
    r = subprocess.run([sys.executable, os.path.join(ROOT, "foundry", "build3d.py"), model, "--kit", ",".join(str(x) for x in ka), "--definition", "--game"],
                       cwd=ROOT, capture_output=True, text=True, timeout=1500)
    glb = os.path.join(DATA, name + ".glb")
    if "game export" not in r.stdout or not os.path.exists(glb):
        return [f"FAILED {model} {grp} {n}: {(r.stdout + r.stderr)[-400:]}"]
    g, blob = read_glb(glb)
    info = json.load(open(os.path.join(DATA, name + ".json")))
    base_g, _ = read_glb(os.path.join(DATA, model + ".glb"))
    base = slot_shapes(base_g, json.load(open(os.path.join(DATA, model + ".json"))))
    mine = slot_shapes(g, info)
    changed = {s for s in mine if mine[s] != base.get(s)}
    slots = [s for s in SLOTS[grp] if s in changed]
    lines = []
    if slots:
        tris = cut(g, blob, info, slots, os.path.join(KITS, f"{model}__{grp}{n}"))
        lines.append(f"kit {model} {grp} {n}: {'+'.join(slots)}, {tris} triangles")
    else:
        lines.append(f"NOTE {model} {grp} {n}: nothing it may change differs from the bike as drawn")
    other = sorted(changed - set(slots))
    if other:
        lines.append(f"NOTE {model} {grp} {n}: also differs, not sold: {', '.join(other)}")
    os.remove(glb)
    os.remove(os.path.join(DATA, name + ".json"))
    with open(os.path.join(KITS, "_done.txt"), "a") as f:
        f.write(f"{model} {grp} {n}\n")
    return lines


def build(models):
    """(Resumable: a variant listed in kits/_done.txt is not built again; delete the line to redo it.)"""
    os.makedirs(KITS, exist_ok=True)
    done_path = os.path.join(KITS, "_done.txt")
    done = set(open(done_path).read().split("\n")) if os.path.exists(done_path) else set()
    jobs = []
    for model in models:
        kit = json.load(open(os.path.join(ROOT, "data", "models", model + ".json"))).get("kit") or {}
        for grp in GROUPS:
            for n in range(1, len(kit.get(grp) or []) + 1):
                if f"{model} {grp} {n}" not in done:
                    jobs.append((model, grp, n))
    print(len(jobs), "kit builds to do", flush=True)
    with ThreadPoolExecutor(3) as ex:
        for lines in ex.map(lambda j: build_one(*j), jobs):
            for line in lines:
                print(line, flush=True)
    print("ALL", flush=True)


BESPOKE = ("pegs", "covers", "lamps", "engine", "sissy", "wheels")
BESPOKE_WORD = {"pegs": "footpegs", "covers": "engine covers", "lamps": "lamp shells", "engine": "engine dress kit", "sissy": "sissy bar", "wheels": "billet wheel"}
# (never sold as a bespoke piece, whatever a build moves: the frame is the bike)
NOT_SOLD = ("frame",)


def bespoke_jobs(models):
    pool = json.load(open(os.path.join(ROOT, "data", "pool.json")))["chopper_bespoke"]
    jobs = []
    for model in models:
        if json.load(open(os.path.join(ROOT, "data", "models", model + ".json")))["family"] not in pool["families"]:
            continue
        for grp in BESPOKE:
            for d in pool.get(grp) or pool.get(grp + "s") or []:      # (the pool lists the dress kits as "engines")
                for m in pool["metals"]:
                    jobs.append((model, grp, d, m))
    return jobs


def build_bespoke(model, grp, design, metal):
    """One chopper bespoke item (pool.json chopper_bespoke) fitted alone to the bike as drawn, cut to the slots it
    changes: kits/<model>__b_<group>_<design>_<metal>."""
    metal_key = {"steel": "steel_brushed", "gold": "gold_bright"}.get(metal["key"], metal["key"]) if grp == "wheels" else metal["key"]
    out = f"{model}__b_{grp}_{design['key']}_{metal['key']}"
    name = f"{model}_kit0000_def"
    work = os.path.join(DATA, "_b_" + out)
    env = dict(os.environ, BIKE_BESPOKE=f"{grp}={design['key']}:{metal_key}", BIKE_GAME_NAME="_b_" + out)
    r = subprocess.run([sys.executable, os.path.join(ROOT, "foundry", "build3d.py"), model, "--kit", "0,0,0,0", "--definition", "--game"],
                       cwd=ROOT, capture_output=True, text=True, timeout=1500, env=env)
    glb = work + ".glb"
    if "game export" not in r.stdout or not os.path.exists(glb):
        return [f"FAILED {out}: {(r.stdout + r.stderr)[-400:]}"]
    g, blob = read_glb(glb)
    info = json.load(open(work + ".json"))
    base_g, _ = read_glb(os.path.join(DATA, model + ".glb"))
    base = slot_shapes(base_g, json.load(open(os.path.join(DATA, model + ".json"))))
    mine = slot_shapes(g, info)
    slots = sorted(s for s in mine if mine[s] != base.get(s) and s not in NOT_SOLD)
    lines = []
    if slots:
        meta = {"bespoke": {"group": grp, "design": design["key"], "metal": metal["key"], "name": f"{design['name']} {BESPOKE_WORD[grp]}, {metal['name'].lower()}",
                            "note": design.get("note", "")}}
        tris = cut(g, blob, info, slots, os.path.join(KITS, out), meta)
        lines.append(f"bespoke {out}: {'+'.join(slots)}, {tris} triangles")
    else:
        lines.append(f"NOTE {out}: nothing differs from the bike as drawn")
    os.remove(glb)
    os.remove(work + ".json")
    with open(os.path.join(KITS, "_done.txt"), "a") as f:
        f.write(f"{out}\n")
    return lines


def build_all_bespoke(models):
    os.makedirs(KITS, exist_ok=True)
    done_path = os.path.join(KITS, "_done.txt")
    done = set(open(done_path).read().split("\n")) if os.path.exists(done_path) else set()
    jobs = [j for j in bespoke_jobs(models) if f"{j[0]}__b_{j[1]}_{j[2]['key']}_{j[3]['key']}" not in done]
    print(len(jobs), "bespoke builds to do", flush=True)
    with ThreadPoolExecutor(3) as ex:
        for lines in ex.map(lambda j: build_bespoke(*j), jobs):
            for line in lines:
                print(line, flush=True)
    print("ALL", flush=True)


def bespoke_entries(parts, model, file, info):
    """A bespoke item's shop parts: one for each slot it changes that the bike has a part of its own in."""
    b, n = info["bespoke"], 0
    slots = sorted({v["slot"] for v in info["surfaces"].values()})
    for slot in slots:
        own_id = f"v2_{model}__{slot}"
        kind = next((k for k, es in parts.items() if isinstance(es, dict) and own_id in es), None)
        if not kind:
            print(f"NOTE no shop part {own_id}: bespoke {file} {slot} not sold")
            continue
        e = json.loads(json.dumps(parts[kind][own_id]))
        vp = e["v2part"]
        more = f" ({slot.replace('_', ' ')})" if len(slots) > 1 else ""
        e["name"] = f"{vp.get('brand', '')} {b['name']}{more}".strip()
        e["description"] = b.get("note") or e.get("description", "")
        if isinstance(e.get("price"), (int, float)):
            e["price"] = int(round(e["price"] * {"gold": 2.5, "brass": 1.8}.get(b["metal"], 1.6) / 5.0) * 5)
        vp.update(model=f"kits/{file}", frame=f"v2_{model}", slot=slot, kit=f"b_{b['group']}_{b['design']}_{b['metal']}", of=model, bespoke=True)
        parts[kind][f"{own_id}_b{b['group']}_{b['design']}_{b['metal']}"] = e
        n += 1
    return n


def catalogue():
    """The kit pieces on disk as shop parts in parts_v2.json: each a copy of the bike's own part of that slot
    (its numbers, what it fits), drawn from the kit file, a little dearer."""
    path = os.path.join(DATA, "parts_v2.json")
    parts = json.load(open(path))
    made = 0
    for kind, entries in parts.items():
        if isinstance(entries, dict):
            for pid in [p for p, v in entries.items() if isinstance(v, dict) and (v.get("v2part") or {}).get("kit")]:
                del entries[pid]
    for f in sorted(os.listdir(KITS)) if os.path.isdir(KITS) else []:
        if not f.endswith(".json") or f.startswith("_"):
            continue
        model, rest = f[:-5].split("__")
        if rest.startswith("b_"):
            made += bespoke_entries(parts, model, f[:-5], json.load(open(os.path.join(KITS, f))))
            continue
        if rest.startswith("e_"):
            import game_engineparts
            made += game_engineparts.entries(parts, model, f[:-5], json.load(open(os.path.join(KITS, f))))
            continue
        grp = next(g for g in GROUPS if rest.startswith(g))
        n = int(rest[len(grp):])
        variant = json.load(open(os.path.join(ROOT, "data", "models", model + ".json")))["kit"][grp][n - 1]
        slots = sorted({v["slot"] for v in json.load(open(os.path.join(KITS, f)))["surfaces"].values()})
        for slot in slots:
            own_id = f"v2_{model}__{slot}"
            kind = next((k for k, es in parts.items() if isinstance(es, dict) and own_id in es), None)
            own = parts[kind][own_id] if kind else None
            if not own:
                # (the bike is drawn without one, a cafe racer's flyscreen: priced as its family's)
                fam = json.load(open(os.path.join(ROOT, "data", "models", model + ".json")))["family"].lower()
                kind, own = next(((k, v) for k, es in parts.items() if isinstance(es, dict) for p, v in sorted(es.items())
                                  if isinstance(v, dict) and (v.get("v2part") or {}).get("slot") == slot and not v["v2part"].get("kit")
                                  and v["v2part"].get("family") == fam), (None, None))
            if not own:
                kind, own = next(((k, v) for k, es in parts.items() if isinstance(es, dict) for p, v in sorted(es.items())
                                  if isinstance(v, dict) and (v.get("v2part") or {}).get("slot") == slot and not v["v2part"].get("kit")), (None, None))
            if not own:
                print(f"NOTE no shop part to copy for {own_id}: kit piece {f[:-5]} {slot} not sold")
                continue
            e = json.loads(json.dumps(own))
            vp = e["v2part"]
            e["name"] = f"{vp.get('brand', '')} {vp.get('family', '')} {variant['name']} {WORD[slot]}".strip()
            e["description"] = (variant.get("note") or f"The {variant['name']} kit {WORD[slot]} for the {vp.get('brand', '')} {vp.get('family', '')}.").strip()
            if isinstance(e.get("price"), (int, float)):
                e["price"] = int(round(e["price"] * 1.15 / 5.0) * 5)
            base = json.load(open(os.path.join(ROOT, "data", "models", model + ".json")))
            vp.update(model=f"kits/{model}__{grp}{n}", frame=f"v2_{model}", slot=slot, kit=f"{grp}{n}", of=model, family=base["family"].lower())
            parts[kind][f"{own_id}_k{LETTER[grp]}{n}"] = e
            made += 1
    json.dump(parts, open(path, "w"), indent=1)
    print(f"{made} kit pieces in the shop")


if __name__ == "__main__":
    if sys.argv[1:2] == ["build"]:
        build(sys.argv[2:] or sorted(f[:-5] for f in os.listdir(os.path.join(ROOT, "data", "models")) if f.endswith(".json")))
    elif sys.argv[1:2] == ["bespoke"]:
        build_all_bespoke(sys.argv[2:] or sorted(f[:-5] for f in os.listdir(os.path.join(ROOT, "data", "models")) if f.endswith(".json")))
    elif sys.argv[1:2] == ["catalogue"]:
        catalogue()
    else:
        print(__doc__)
