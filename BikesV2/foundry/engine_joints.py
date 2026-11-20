"""Engine parts pass, stage 1 (beenster, 18 Nov: plan https://claude.ai/artifact/TxPXHSRkftpdYsppy6dhXt): the joints
the new engine parts bolt to, measured on every V2 bike and agreed per engine type.

A new airbox, fuel system, cover, side case, oil filter or header pipe fits any engine of its type (not just its own
bike's), so each type gets one set of joints: where the intake ports, exhaust ports, covers, side cases, sump and oil
filter sit, from the crank (mm: x forward, y left, z up). Each bike's own engine is measured from its built parts
(build3d BIKE_DUMP: every part's box), the type's joint is the median, and a bike whose joint sits off it by more than
ADAPT mm gets an adapter drawn at that joint (a stub, a spacer or a flange) so a part made to the type's joint meets it.

    for m in <models>; do BIKE_DUMP=dumps/$m.txt BIKE_DUMP_ONLY=1 python3 foundry/build3d.py $m --definition --game; done
    python3 foundry/engine_joints.py dumps/        # data/engine_joints.json and docs/engine_joints.md
"""
import json
import os
import re
import statistics
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ADAPT = 15.0   # mm a bike's joint may sit off its type's before it needs an adapter

TYPES = ("single", "two_stroke", "parallel_twin", "vtwin90", "vtwin45", "boxer", "triple", "inline4")
TYPE_NAMES = {"single": "Four-stroke single", "two_stroke": "Two-stroke single", "parallel_twin": "Parallel twin",
              "vtwin90": "90 degree V-twin", "vtwin45": "45 degree V-twin", "boxer": "Flat twin (boxer)",
              "triple": "Inline triple", "inline4": "Inline four"}
# joint -> the dumped parts that make it (name patterns; bolts and their heads left out)
JOINTS = {
    "exhaust_port": r"^portflange[\d_]+$",
    "intake": r"^(inletstub\d+|carb\d*|carbbody\d*|throttlebody\d*|tb\d+)$",
    "clutch_cover": r"^(clutchcap|clutchcover)\d*$",
    "alternator_cover": r"^altcover\d*$",
    "cam_caps": r"^camcap[\d-]+$",
    "rocker_cover": r"^(cover\d+|boxercover-?1)$",
    "side_case": r"^sidecase-?1$",
    "sump": r"^sump$",
    "oil_filter": r"^svc_oil_filter$",
}


def engine_type(model):
    eng = model["engine"]
    if "two-stroke" in model.get("engine_label", "").lower():
        return "two_stroke"
    lay = eng["layout"]
    return "parallel_twin" if lay == "twin" else lay


def read_dump(path):
    """name -> (material, (x0, x1), (y0, y1), (z0, z1)) in mm."""
    out = {}
    for line in open(path):
        parts = line.rstrip("\n").split("\t")
        if len(parts) < 3:
            continue
        rng = [tuple(float(v) for v in r.split("..")) for r in parts[2].split(" ")]
        out[parts[0]] = (parts[1], *rng)
    return out


# Joints on the top end are measured in their cylinder's own frame, so engines of a type whose cylinders lean at
# different angles still share them: along the bore from the head face (t), across it (u), out to the side (y).
TOP_END = ("exhaust_port", "intake", "cam_caps", "rocker_cover")


def cylinder_frame(model, c):
    """[t, y, u] of a point c (crank-centred [x, y, z]) in the frame of the cylinder it sits over."""
    import math
    eng = model["engine"]
    best = None
    for cy in eng.get("cylinders", []):
        a = math.radians(cy["angle"])
        t = c[0] * math.cos(a) + c[2] * math.sin(a)
        u = -c[0] * math.sin(a) + c[2] * math.cos(a)
        if best is None or t > best[0]:
            best = (t - (cy["start"] + cy["length"]), u)
    if best is None:   # a boxer: its cylinders lie flat out to the sides, the head face at the reach
        b = eng["boxer"]
        return [abs(c[1]) - b["reach"], c[0] - (b["at"][0] - eng["crank"][0]), c[2] - (b["at"][1] - eng["crank"][1])]
    return [best[0], c[1], best[1]]


def measure(model, dump):
    """Each joint's parts as boxes centred on the crank: [{name, c: [x, y, z], size: [dx, dy, dz]}]; the top end's in
    their cylinder's frame ([t, y, u], cylinder_frame)."""
    cx, cz = model["engine"]["crank"]
    found = {}
    for joint, pat in JOINTS.items():
        rx = re.compile(pat)
        boxes = []
        for name, (_mat, xs, ys, zs) in sorted(dump.items()):
            if not rx.match(name):
                continue
            c = [(xs[0] + xs[1]) / 2 - cx, (ys[0] + ys[1]) / 2, (zs[0] + zs[1]) / 2 - cz]
            if joint in TOP_END:
                c = cylinder_frame(model, c)
            boxes.append({"name": name, "c": [round(v, 1) for v in c], "size": [round(r[1] - r[0], 1) for r in (xs, ys, zs)]})
        if boxes:
            found[joint] = boxes
    return found


def summary(boxes):
    """One point for a joint with several parts: the mean centre, |y| for a pair either side."""
    n = len(boxes)
    c = [sum(b["c"][i] for b in boxes) / n for i in range(3)]
    c[1] = sum(abs(b["c"][1]) for b in boxes) / n
    return c, n


def main():
    dumps = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "out", "dumps")
    per_bike, per_type = {}, {t: {} for t in TYPES}
    for f in sorted(os.listdir(dumps)):
        if not f.endswith(".txt"):
            continue
        name = f[:-4]
        model = json.load(open(os.path.join(ROOT, "data", "models", name + ".json")))
        t = engine_type(model)
        m = measure(model, read_dump(os.path.join(dumps, f)))
        per_bike[name] = {"type": t, "joints": m}
        for joint, boxes in m.items():
            per_type[t].setdefault(joint, {})[name] = summary(boxes)
    agreed, report = {}, []
    for t in TYPES:
        agreed[t] = {}
        for joint, bikes in sorted(per_type[t].items()):
            pts = [c for c, _n in bikes.values()]
            med = [round(statistics.median(p[i] for p in pts), 1) for i in range(3)]
            counts = sorted({n for _c, n in bikes.values()})
            off = {b: round(max(abs(c[i] - med[i]) for i in range(3)), 1) for b, (c, _n) in bikes.items()}
            adapt = sorted(b for b, d in off.items() if d > ADAPT)
            agreed[t][joint] = {"at": med, "count": counts, "bikes": sorted(bikes), "off_mm": off, "adapter": adapt}
            report.append((t, joint, med, counts, len(bikes), max(off.values()), adapt))
    out = {"adapt_mm": ADAPT, "types": agreed, "bikes": per_bike}
    json.dump(out, open(os.path.join(ROOT, "data", "engine_joints.json"), "w"), indent=1)
    lines = ["# Engine joints (engine parts pass, stage 1)", "",
             "Measured from every V2 bike's built parts by `foundry/engine_joints.py`; mm from the crank (x forward, y out to one side, z up).",
             "Top-end joints (exhaust port, intake, cam caps, rocker cover) are in their cylinder's own frame: t along the bore from the head face, y out to the side, u across the bore.",
             "Airboxes are not a joint: each new airbox starts at the intake joint and is shaped to the room each frame leaves.",
             "Header pipes and intakes are drawn from each engine's own ports (this file's `bikes`), as the silencers are from each bike's own pipe,",
             "so a type's parts fit all its engines even where ports sit apart (a different head or cylinder lean); the type's point is the reference the looks are designed round.",
             f"Covers, cases, sump and oil filter: a bike more than {ADAPT:.0f} mm off its type's joint gets an adapter (spacer) there.", "",
             "| Engine type | Joint | At (x, y, z) | Parts | Bikes | Worst off (mm) | Adapters |", "| --- | --- | --- | --- | --- | --- | --- |"]
    for t, joint, med, counts, nb, worst, adapt in report:
        lines.append(f"| {TYPE_NAMES[t]} | {joint.replace('_', ' ')} | {med[0]:.0f}, {med[1]:.0f}, {med[2]:.0f} | {'/'.join(str(c) for c in counts)} | {nb} | {worst:.0f} | {', '.join(adapt) or '-'} |")
    missing = [(t, j) for t in TYPES for j in JOINTS if j not in agreed[t]]
    if missing:
        lines += ["", "Joints no bike of the type has (the type's parts don't fit there): " + "; ".join(f"{TYPE_NAMES[t]} {j.replace('_', ' ')}" for t, j in missing)]
    open(os.path.join(ROOT, "docs", "engine_joints.md"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
