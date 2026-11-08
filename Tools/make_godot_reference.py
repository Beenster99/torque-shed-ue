"""Records the original Torque Shed's reference bikes from the game itself, so the Unreal port's
fitting core (Fit/BikeLayout, BikeBody, BikeParts) is checked against what Godot really draws.

    python Tools/make_godot_reference.py --godot ../torque-shed --bin path/to/Godot_v4.6.2-stable_linux.x86_64

It copies the Godot project to a temporary folder (the clone is left alone), imports it, runs
Tools/godot_reference/dump_ref.tscn headless (every style's reference bike: its build, the rig's
solve, and the bounds of every slot's drawn meshes in the Rig built look), and writes the result,
trimmed, to SimTest/data/godot_reference.json. SimTest/layout_test compares the port with it.
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--godot", default=os.path.join(os.path.dirname(REPO), "torque-shed"))
    ap.add_argument("--bin", required=True, help="the official Godot 4.6.2 binary")
    a = ap.parse_args()
    tmp = tempfile.mkdtemp(prefix="ts_ref_")
    try:
        proj = os.path.join(tmp, "ts")
        shutil.copytree(a.godot, proj, ignore=shutil.ignore_patterns(".git"))
        shutil.copytree(os.path.join(HERE, "godot_reference"), os.path.join(proj, "dump"))
        subprocess.run([a.bin, "--headless", "--path", proj, "--import"], check=True, capture_output=True)
        raw = os.path.join(tmp, "ref.json")
        subprocess.run([a.bin, "--headless", "--path", proj, "res://dump/dump_ref.tscn", "--", "--out=" + raw],
                       check=True, capture_output=True, timeout=1800)
        data = json.load(open(raw))
        commit = subprocess.run(["git", "-C", a.godot, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
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
                                "slots": slots}
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
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w").write(json.dumps(out, separators=(",", ":")) + "\n")
    print("wrote %s: %d styles, %d swept builds" % (OUT, len(out["styles"]), len(out["sweep"])))


if __name__ == "__main__":
    main()
