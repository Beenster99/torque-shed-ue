"""Records the original Torque Shed's reference bikes from the game itself, so the Unreal port's
fitting core (Fit/BikeLayout, BikeBody, BikeParts) is checked against what Godot really draws.

    python Tools/make_godot_reference.py --godot ../torque-shed --bin path/to/Godot_v4.6.2-stable_linux.x86_64

It copies the Godot project to a temporary folder (the clone is left alone), imports it, runs
Tools/godot_reference/dump_ref.tscn headless (every style's reference bike: its build, the rig's
solve, and the bounds of every slot's drawn meshes in the Rig built look), and writes the result,
trimmed, to SimTest/data/godot_reference.json. SimTest/layout_test compares the port with it.

With --geo it runs Tools/godot_reference/dump_geo.tscn instead: geometry mode's maths (GeoGizmo's
drags and offsets, sanitize, BikeRig.clean_adjust) on a fixed set of inputs, written to
SimTest/data/godot_geo.json for SimTest/geo_test.

With --spec it runs Tools/godot_reference/dump_spec.tscn: BikeBuild's riding numbers (chassis,
wheels, tyres, suspension, brakes, gearbox, mass spread, aids, engine torque) for every style's
reference bike and a sweep of swaps, written to SimTest/data/godot_spec.json for SimTest/spec_test.
"""
import argparse
import json
import os
import shutil
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "SimTest", "data", "godot_reference.json")
GEO_OUT = os.path.join(REPO, "SimTest", "data", "godot_geo.json")
SPEC_OUT = os.path.join(REPO, "SimTest", "data", "godot_spec.json")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--godot", default=os.path.join(os.path.dirname(REPO), "torque-shed"))
    ap.add_argument("--bin", help="the official Godot 4.6.2 binary")
    ap.add_argument("--keep-raw", help="also keep Godot's untrimmed dump here")
    ap.add_argument("--from-raw", help="trim a kept dump again instead of running Godot")
    ap.add_argument("--geo", action="store_true", help="dump geometry mode's maths (godot_geo.json) instead")
    ap.add_argument("--spec", action="store_true", help="dump BikeBuild's riding numbers (godot_spec.json) instead")
    a = ap.parse_args()
    if a.from_raw:
        data = json.load(open(a.from_raw))
        commit = subprocess.run(["git", "-C", a.godot, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
        write(data, commit)
        return
    if not a.bin:
        ap.error("--bin is needed to run Godot")
    tmp = tempfile.mkdtemp(prefix="ts_ref_")
    try:
        proj = os.path.join(tmp, "ts")
        shutil.copytree(a.godot, proj, ignore=shutil.ignore_patterns(".git"))
        shutil.copytree(os.path.join(HERE, "godot_reference"), os.path.join(proj, "dump"))
        subprocess.run([a.bin, "--headless", "--path", proj, "--import"], check=True, capture_output=True)
        raw = os.path.join(tmp, "ref.json")
        if a.spec:
            subprocess.run([a.bin, "--headless", "--path", proj, "res://dump/dump_spec.tscn", "--", "--out=" + raw],
                           check=True, capture_output=True, timeout=900)
            data = json.load(open(raw))
            def r(v):
                # Six significant figures is far finer than the checks need, and keeps the file small.
                if isinstance(v, float):
                    return float("%.6g" % v)
                if isinstance(v, list):
                    return [r(x) for x in v]
                if isinstance(v, dict):
                    return {k: r(x) for k, x in v.items()}
                return v
            commit = subprocess.run(["git", "-C", a.godot, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
            data = {"about": "The original Torque Shed's BikeBuild riding numbers (new parts, no tune). Written by "
                             "Tools/make_godot_reference.py --spec; do not edit.",
                    "source": {"repo": "Beenster99/torque-shed", "commit": commit, "godot": "4.6.2-stable"}, **r(data)}
            json.dump(data, open(SPEC_OUT, "w"), separators=(",", ":"), sort_keys=True)
            print("wrote %s" % SPEC_OUT)
            return
        if a.geo:
            subprocess.run([a.bin, "--headless", "--path", proj, "res://dump/dump_geo.tscn", "--", "--out=" + raw],
                           check=True, capture_output=True, timeout=600)
            shutil.copyfile(raw, GEO_OUT)
            print("wrote %s" % GEO_OUT)
            return
        subprocess.run([a.bin, "--headless", "--path", proj, "res://dump/dump_ref.tscn", "--", "--out=" + raw],
                       check=True, capture_output=True, timeout=1800)
        data = json.load(open(raw))
        if a.keep_raw:
            shutil.copyfile(raw, a.keep_raw)
        commit = subprocess.run(["git", "-C", a.godot, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    write(data, commit)


def write(data, commit):
    r6 = lambda v: [round(float(x), 6) for x in v]
    out = {"about": "The original Torque Shed's reference bikes as Godot draws them (Rig built look), Godot frame "
                    "(y up, -z forward, x right). Written by Tools/make_godot_reference.py; do not edit.",
           "source": {"repo": "Beenster99/torque-shed", "commit": commit, "godot": "4.6.2-stable"}, "styles": {}}
    def glb_tags(meshes):
        # Each glb surface tag's meshes in the slot, as one box: what the port's Blender parts are checked against.
        out = {}
        for m in meshes:
            if not m.get("tag"):
                continue
            b = out.setdefault(m["tag"], {"min": list(m["min"]), "max": list(m["max"]), "meshes": 0})
            b["min"] = [min(x, y) for x, y in zip(b["min"], m["min"])]
            b["max"] = [max(x, y) for x, y in zip(b["max"], m["max"])]
            b["meshes"] += 1
        return {t: {"min": r6(b["min"]), "max": r6(b["max"]), "meshes": b["meshes"]} for t, b in sorted(out.items())}

    def hidden_boxes(slots):
        # Each slot's meshes a full fairing hides (BikeModel._hide_covered, _service_caps), as one
        # box, and any service cap put on the panel for one.
        out = {}
        for k, v in slots.items():
            # (Its Blender surfaces, tag by tag, as glb_tags; the code-drawn ones go with them.)
            hid = glb_tags([m for m in v["meshes"] if m.get("hidden")])
            caps = [m for m in v["meshes"] if m["name"].startswith("ServiceCap")]
            if hid:
                out.setdefault(k, {})["hidden"] = hid
            if caps:
                out.setdefault(k, {})["cap"] = {"min": r6(caps[0]["min"]), "max": r6(caps[0]["max"])}
        return out

    sweep = data.pop("sweep", {})
    for style, s in data.items():
        rig = {k: v for k, v in s["rig"].items() if isinstance(v, list) and len(v) == 3 and all(isinstance(x, (int, float)) for x in v)}
        rig["fit_tier"] = s["rig"]["fit"]["tier"]
        rig["fit_reason"] = s["rig"]["fit"]["reason"]
        slots = {k: {"min": r6(v["min"]), "max": r6(v["max"]),
                     "named": {m["name"]: {"min": r6(m["min"]), "max": r6(m["max"])} for m in v["meshes"] if not m["name"].startswith("@")},
                     "glb": glb_tags(v["meshes"])}
                 for k, v in s["slots"].items()}
        out["styles"][style] = {"parts": s["parts"], "stats": s["stats"], "rig": {k: (r6(v) if isinstance(v, list) else v) for k, v in rig.items()},
                                "slots": slots, "covered": hidden_boxes(s["slots"])}
    # The sweep: each build as its base style's parts plus what changed, and only the swept slots'
    # Blender surfaces (what the sweep is there to check).
    keep = ("wheel", "tyre", "disc", "caliper", "pads", "fork", "shock", "swingarm", "final_drive", "chain",
            "engine", "head", "spark_plugs", "fuel", "air_filter", "oil_filter", "cooling", "gearbox", "exhaust", "handlebars", "dashboard", "master", "headlight", "mirrors", "indicators", "front_mudguard", "frame", "fairing", "tail", "saddle", "tank", "side_stand", "centre_stand", "rearsets", "horn", "ecu", "side_panels", "guards", "panniers", "top_box", "number_boards", "fog_lights", "steering_damper", "lever_guards", "aux_tank", "satnav", "tank_bag", "toolbox")
    out["sweep"] = {}
    for key, s in sweep.items():
        base = out["styles"][s["base"]]["parts"]
        out["sweep"][key] = {"base": s["base"], "parts": {k: v for k, v in s["parts"].items() if base.get(k) != v},
                             "fit_tier": s["rig"]["fit"]["tier"],
                             "glb": {k: glb_tags(v["meshes"]) for k, v in s["slots"].items() if k.endswith(keep) and glb_tags(v["meshes"])}}
        # The swept slot whole (Blender and code-drawn meshes) and its named meshes, for the code-drawn shapes.
        cov = hidden_boxes(s["slots"])
        if cov:
            out["sweep"][key]["covered"] = cov
        slot = key.split("/")[0]
        if slot in s["slots"]:
            v = s["slots"][slot]
            out["sweep"][key]["slot"] = {"min": r6(v["min"]), "max": r6(v["max"]),
                                         "named": {m["name"]: {"min": r6(m["min"]), "max": r6(m["max"])} for m in v["meshes"] if not m["name"].startswith("@")}}
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w").write(json.dumps(out, separators=(",", ":")) + "\n")
    print("wrote %s: %d styles, %d swept builds" % (OUT, len(out["styles"]), len(out["sweep"])))


if __name__ == "__main__":
    main()
