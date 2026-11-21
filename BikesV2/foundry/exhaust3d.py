"""Engine parts pass, stage 4 (plan https://claude.ai/artifact/TxPXHSRkftpdYsppy6dhXt): header pipes with looks of
their own on every engine, joined to the bike's own silencer cans.

The joint (docs/engine_joints.md): each exhaust port's flange on the head, and the can's inlet sleeve where the pipe
ends. Every header keeps its bike's own run from port to can (so the silencers, and the common silencer cans, still
slip on), and the pipe-to-silencer joint is drawn by the bike itself from the header's size.

    HEADERS  stock (the bike's own), race (bigger-bore pipes, the can sleeves opened to suit: a full system, +4% pull),
             ceramic (satin black ceramic coat), wrapped (woven heat wrap over the header pipes)

    python3 foundry/exhaust3d.py shots [--samples N]     # out/headers/*.png
    python3 foundry/exhaust3d.py check                   # every look on one bike of each engine type
"""
import copy
import json
import os
import re
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bars3d as BR  # noqa: E402
import detail3d as D  # noqa: E402

B = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "headers")

HEADERS = {
    "stock": dict(label="Stock headers", note="The bike's own header pipes, heat-tinted stainless.", perf={}),
    "race": dict(label="Race headers", note="Bigger-bore stainless headers on the bike's own run, the silencers' sleeves opened to suit.",
                 perf={"torque_mult": 1.04}, bore=1.15),
    "ceramic": dict(label="Ceramic headers", note="The bike's own run in a satin black ceramic coat.", perf={}),
    "wrapped": dict(label="Wrapped headers", note="Woven heat wrap over the header pipes, stainless clamps at the ends.", perf={}),
}
PIPES = re.compile(r"^(header[\d_]*|headerbranch|headermerge\d*)$")


def use(b):
    global B
    B = b


BORES = (1.15, 1.1, 1.06)     # a race header's bore over the stock one: the biggest that clears the bike


def prepare(model, look, bore=None):
    """The model as the look draws it (a race header's bigger bore: the can sleeves follow it in detail3d)."""
    m = copy.deepcopy(model)
    k = bore or HEADERS[look].get("bore")
    if k and look == "race":
        for h in m["exhaust"]["headers"]:
            h["w"] = round(h.get("w", 44) * k, 1)
    return m


def wrap_mat():
    m = D.surf("headerwrap", (0.55, 0.5, 0.42), 0.0, 0.85, bump=(0.25, 1.2), wear=0.15)
    return m


def fit(b, model, look):
    """After the build: the finish of the header pipes. Returns a report and the pipes it touched."""
    use(b)
    pipes = [o for o in bpy.data.objects if PIPES.match(o.name) and getattr(o.data, "materials", None) is not None]
    mat = None
    if look == "ceramic":
        mat = D.surf("ceramicblack", (0.025, 0.025, 0.027), 0.35, 0.55, wear=0.05)
    elif look == "wrapped":
        mat = wrap_mat()
    if mat:
        for o in pipes:
            if len(o.data.materials):
                o.data.materials[0] = mat
            else:
                o.data.materials.append(mat)
    return dict(model=model.get("name", ""), look=look, pipes=len(pipes), bore=[h.get("w") for h in model["exhaust"]["headers"]]), [o.name for o in pipes]


def built(mn, look, bore=None):
    import build3d
    use(build3d)
    BR.fresh()
    m = prepare(BR.models()[mn], look, bore)
    build3d.DETAIL["on"] = True
    build3d.build(m)
    use(build3d)
    rep, made = fit(build3d, m, look)
    return build3d, m, rep, made


TYPE_BIKES = {"single": "varga_mx", "two_stroke": "corvano_mx", "parallel_twin": "hartwell_classic", "vtwin90": "corvano_naked",
              "vtwin45": "lodestar_cruiser", "boxer": "rheinhart_classic", "triple": "hartwell_naked", "inline4": "kazeda_naked"}
SHOTS = [("race", "kazeda_naked", -1), ("ceramic", "corvano_naked", -1), ("wrapped", "hartwell_classic", -1), ("wrapped", "rheinhart_classic", 1)]


def shots(samples=40):
    import lamps3d as L
    from mathutils import Vector
    os.makedirs(OUT, exist_ok=True)
    for look, mn, side in SHOTS:
        b3, m, r, made = built(mn, look)
        pts = [o.matrix_world @ Vector(c) / b3.MM for n in made for o in [bpy.data.objects[n]] for c in o.bound_box]
        lo = Vector(tuple(min(p[i] for p in pts) for i in range(3)))
        hi = Vector(tuple(max(p[i] for p in pts) for i in range(3)))
        tgt = tuple((lo + hi) / 2)
        span = max(hi[0] - lo[0], hi[2] - lo[2], 400)
        L.render_at(b3, os.path.join(OUT, f"{mn}__{look}.png"), BR.vadd(tgt, (span * 0.5, side * span * 1.9, span * 0.3)), tgt, 50, samples=samples)


def meshed(names):
    """The pipes as meshes (smooth_tube draws curves; the clip measure reads meshes)."""
    obs = [bpy.data.objects[n] for n in names if bpy.data.objects[n].type == "CURVE"]
    if obs:
        bpy.ops.object.select_all(action="DESELECT")
        for o in obs:
            o.select_set(True)
        bpy.context.view_layer.objects.active = obs[0]
        bpy.ops.object.convert(target="MESH")
    return names


def check(types=None):
    """The race bore against the bike (the finishes only change the material). The pipes' own joints to their flanges,
    sleeves and each other are left out, as are the stock pipes' own touches."""
    res = {}
    ok = re.compile(r"^(header|headerbranch|headermerge|port|can|collector|exhaust)")
    for et, mn in TYPE_BIKES.items():
        if types and et not in types:
            continue
        b3, m, r, made = built(mn, "stock")
        base = {k: v for k, v in BR.clip_check(meshed(made)).items()}
        for bore in BORES:
            b3, m, r, made = built(mn, "race", bore)
            bad = {}
            for k, v in BR.clip_check(meshed(made)).items():
                other = k.split("|")[1].strip()
                if ok.match(other) or v <= base.get(k, -1) + 1.0:
                    continue
                bad[k] = v
            if not bad:
                break
        r["bore_k"] = bore
        res[et] = dict(model=mn, clips=bad, rep=r)
        print(f"XCHECK {et:13s} race x{bore} pipes {r['pipes']} bore {r['bore']} {bad}", flush=True)
    os.makedirs(OUT, exist_ok=True)
    json.dump(res, open(os.path.join(OUT, "check.json"), "w"), indent=1)


if __name__ == "__main__":
    a = sys.argv[1:]
    cmd = a[0] if a else "shots"
    smp = int(a[a.index("--samples") + 1]) if "--samples" in a else 40
    if cmd == "shots":
        shots(smp)
    elif cmd == "check":
        check([x for x in a[1:] if not x.startswith("--")] or None)
