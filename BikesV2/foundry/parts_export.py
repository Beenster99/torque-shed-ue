"""Common parts, stage 9 (the safe part, agreed with Bike Models V2 on 17 Nov): one game file per approved part.

Each approved part is fitted on one lead bike of a family it suits, cut out of that bike and written as its own
GLB the same way build3d.export_game writes a whole bike (one mesh, one primitive per surface, +Y up, metres),
with the same surface-look JSON beside it. The origin of each file is the part's mount point on that bike, and
parts_manifest.json lists every part: key, name, group, the families it fits, finishes, mount point (named, and
where it sits on the lead bike in mm) and what it moves with (steering, front wheel, swingarm and so on).

Nothing here reaches Data/ or the game: the switch-over design is still to be agreed with beenster.
Parts are fitted to each bike's own geometry (disc size, shock length, bar clamp height...), so a file is the
part as fitted on its lead bike; the scripts named in the manifest refit it to any other bike.

    python3 foundry/parts_export.py [groups or keys] [--jobs N]     # out/parts_game/<group>/<key>.glb/.json + manifest
"""
import json
import math
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "out", "parts_game")
ORDER = ("Naked", "Sport", "Cafe", "Classic", "Adventure", "MX", "Cruiser", "Chopper")
LEADS = {"Naked": "corvano_naked", "Sport": "kazeda_sport", "Cafe": "hartwell_cafe", "Classic": "hartwell_classic",
         "Cruiser": "lodestar_cruiser", "Adventure": "corvano_adventure", "MX": "varga_mx", "Chopper": "lodestar_chopper"}
BAR_FAMS = ["Naked", "Sport", "Cafe", "Classic", "Adventure", "MX", "Cruiser", "Chopper"]
CHAIN_FAMS = ["Naked", "Sport", "Cafe", "Classic", "Adventure", "MX", "Cruiser", "Chopper"]


def ho(name):
    return json.load(open(os.path.join(HERE, name + "_handoff.json")))


# group: (hand-off file, list key, script, mount point, moves with, object name prefixes or None = all the fit made)
# split: "front"/"rear" keeps only the objects in that half of the bike (by x against half the wheelbase)
GROUPS = [
    ("bar", "bars", "bars", "bars3d.py", "bar_clamp", "steering", ("bar_lib", "briser", "bclip")),
    ("high_bar", "highbars", "bars", "highbars3d.py", "bar_clamp", "steering", ("bar_lib", "briser", "bclip")),
    ("grip", "bars", "grips", "bars3d.py", "bar_clamp", "steering", ("grip_lib",)),
    ("bar_end", "bars", "ends", "bars3d.py", "bar_clamp", "steering", ("barend_lib",)),
    ("levers", "controls", "levers", "controls3d.py", "bar_clamp", "steering", ("clever", "cperch", "cpivot", "cmaster", "cclutch")),
    ("switches", "controls", "switches", "controls3d.py", "bar_clamp", "steering", ("csw",)),
    ("mirrors", "controls", "mirrors", "controls3d.py", "bar_clamp", "steering", ("cmirror", "cmstalk")),
    ("headlamp", "lamps", "headlamps", "lamps3d.py", "top_yoke", "steering", None),
    ("tail_light", "lamps", "tail_lights", "lamps3d.py", "part_centre", "frame", None),
    ("indicators_front", "lamps", "indicators", "lamps3d.py", "top_yoke", "steering", "front"),
    ("indicators_rear", "lamps", "indicators", "lamps3d.py", "part_centre", "frame", "rear"),
    ("clocks", "clocks", "housings", "clocks3d.py", "top_yoke", "steering", None),
    ("silencer", "silencers", "silencers", "silencers3d.py", "can_inlet", "frame", None),
    ("pegs", "pegs", "pegs", "pegs3d.py", "rider_peg", "frame", None),
    ("heel_plates", "pegs", "heel_plates", "pegs3d.py", "rider_peg", "frame", None),
    ("foot_controls", "pegs", "foot_controls", "pegs3d.py", "rider_peg", "frame", None),
    ("disc_front", "brakes", "discs", "brakes3d.py", "front_axle", "front_wheel", "front"),
    ("disc_rear", "brakes", "discs", "brakes3d.py", "rear_axle", "rear_wheel", "rear"),
    ("caliper_front", "brakes", "calipers", "brakes3d.py", "front_axle", "steering", "front"),
    ("caliper_rear", "brakes", "calipers", "brakes3d.py", "rear_axle", "swingarm", "rear"),
    ("sprocket", "brakes", "sprockets", "brakes3d.py", "rear_axle", "rear_wheel", None),
    ("shock", "shocks", "shocks", "shocks3d.py", "shock_top", "shock", None),
    ("filler_cap", "shocks", "caps", "shocks3d.py", "cap_base", "frame", None),
    ("plate_hanger", "shocks", "hangers", "shocks3d.py", "part_centre", "frame", None),
]

MOUNTS = {
    "bar_clamp": "the bar clamp centre on the top yoke risers (on the steering axis plane, y 0)",
    "top_yoke": "the top yoke centre where the fork tubes end (y 0)",
    "front_axle": "the front wheel axle centre (y 0)",
    "rear_axle": "the rear wheel axle centre (y 0)",
    "rider_peg": "the rider's footpeg point on the centre line (y 0); the pair stands either side",
    "shock_top": "the shock's top eye (y 0 for a twin pair)",
    "can_inlet": "the middle of the silencer inlet(s) where the pipe slips in (y 0 for a pair)",
    "cap_base": "the middle of the cap's foot on the tank",
    "part_centre": "the middle of the part (y 0 when it is a left and right pair)",
}


def items():
    """Every approved part as (group, key, name, fits, finishes, script, mount, moves, select, extra)."""
    out = []
    hos = {k: ho(k) for k in ("bars", "highbars", "controls", "lamps", "clocks", "silencers", "pegs", "brakes", "shocks")}
    for g, f, lst, script, mount, moves, sel in GROUPS:
        d = hos[f]
        for it in d[lst]:
            fits = it.get("fits") or (CHAIN_FAMS if f == "brakes" else BAR_FAMS)
            fin = []
            if f in ("bars",) and lst == "bars":
                fin = d["finishes"]
            elif f == "highbars":
                fin = [it.get("finish")]
            elif lst == "calipers":
                fin = d["caliper_finishes"]
            elif lst == "sprockets":
                fin = it.get("finishes", [])
            elif lst == "shocks":
                fin = ho("shocks")["spring_finishes"]
            elif lst == "caps":
                fin = d["cap_finishes"]
            out.append(dict(group=g, key=it["key"], name=it["name"], fits=fits, finishes=fin, script=script,
                            mount=mount, moves_with=moves, select=sel))
    return out


def lead_for(fits):
    return next(LEADS[f] for f in ORDER if f in fits)


# ---------------------------------------------------------------- the Blender side (one part per process)
def build_one(it):
    import bpy  # noqa: F401
    sys.path.insert(0, HERE)
    import bars3d as BR
    g, key, mn = it["group"], it["key"], it["model"]
    BR.fresh()
    if it["group"] == "high_bar":
        import highbars3d  # noqa: F401  registers the high bars into bars3d
    if g in ("bar", "high_bar"):
        b3, m, cc, lines, made = BR.built(mn, key, "ribbed", "weighted")
    elif g in ("grip", "bar_end"):
        b3, m, cc, lines, made = BR.built(mn, "street", key if g == "grip" else "ribbed", key if g == "bar_end" else "weighted")
    elif g in ("levers", "switches", "mirrors"):
        import controls3d as CT
        kw = dict(levers="standard", switches="dpad", mirrors="oval")
        kw[g] = key
        b3, m, cc, lines, info, made = CT.built(mn, kw["levers"], kw["switches"], kw["mirrors"])
    elif g.startswith(("headlamp", "tail_light", "indicators")):
        import lamps3d as LM
        kw = {"headlamp": dict(head=key), "tail_light": dict(tail=key)}.get(g, dict(ind=key))
        b3, m, cc, rep, made = LM.built(mn, **kw)
    elif g == "clocks":
        import clocks3d as CK
        b3, m, cc, rep, made = CK.built(mn, key)
    elif g == "silencer":
        import silencers3d as SL
        b3, m, cc, rep, made = SL.built(mn, key)
    elif g in ("pegs", "heel_plates", "foot_controls"):
        import pegs3d as PG
        kw = {"pegs": dict(peg=key), "heel_plates": dict(heel=key), "foot_controls": dict(pedal=key)}[g]
        b3, m, cc, rep, made = PG.built(mn, **kw)
    elif g.startswith(("disc", "caliper", "sprocket")):
        import brakes3d as BK
        kw = {"disc": dict(disc=key), "caliper": dict(cal=key), "sprocket": dict(sprocket=key)}[g.split("_")[0]]
        b3, m, cc, rep, made = BK.built(mn, **kw)
    elif g in ("shock", "filler_cap", "plate_hanger"):
        import shocks3d as SH
        kw = {"shock": dict(shock=key), "filler_cap": dict(cap=key), "plate_hanger": dict(hanger=key)}[g]
        b3, m, cc, rep, made = SH.built(mn, **kw)
    else:
        raise ValueError(g)
    return b3, m, cc, made


def m_family(mn):
    return json.load(open(os.path.join(ROOT, "data", "models", mn + ".json")))["family"]


def box(names):
    import bpy
    from mathutils import Vector
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    lo, hi = [1e9] * 3, [-1e9] * 3
    for n in names:
        ob = bpy.data.objects.get(n)
        if ob is None or ob.type not in ("MESH", "CURVE"):
            continue
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        mw = ev.matrix_world
        for v in me.vertices:
            w = mw @ v.co
            for i in range(3):
                lo[i], hi[i] = min(lo[i], w[i] * 1000), max(hi[i], w[i] * 1000)
        ev.to_mesh_clear()
    return lo, hi


def mount_point(it, m, cc, names):
    import bars3d as BR
    kind = it["mount"]
    lo, hi = box(names)
    pair = lo[1] < -20 and hi[1] > 20
    mid = [(lo[i] + hi[i]) / 2 for i in range(3)]
    if pair:
        mid[1] = 0.0
    if kind == "bar_clamp":
        rz, has = BR.clamp_point(m, cc)
        return (rz[0], 0.0, rz[1])
    if kind == "top_yoke":
        return (cc["top"][0], 0.0, cc["top"][1])
    if kind == "front_axle":
        return (cc["F"][0], 0.0, cc["F"][1])
    if kind == "rear_axle":
        return (0.0, 0.0, float(m["wheels"]["rear"]["tyre_r"]))
    if kind == "rider_peg":
        pegs = [tuple(s_["at"]) for s_ in m.get("hangers", []) if s_.get("circle")]
        if not pegs:
            import pegs3d as PG
            pegs = [PG.MX_PEG]
        return (pegs[0][0], 0.0, pegs[0][1])
    if kind == "shock_top":
        t = m["shock"]["top"]
        return (t[0], 0.0, t[1])
    if kind == "can_inlet":
        ins = [n for n in names if n.endswith("_inlet")]
        if ins:
            a, b = box(ins)
            c = [(a[i] + b[i]) / 2 for i in range(3)]
            if a[1] < -20 and b[1] > 20:
                c[1] = 0.0
            return tuple(c)
        return tuple(mid)
    if kind == "cap_base":
        return (mid[0], mid[1], lo[2])
    return tuple(mid)


def looks_of(ob, LIB):
    looks = {}
    for mt in ob.data.materials:
        tag = mt.name.split(".")[0]
        spec = LIB["surfaces"].get(tag.split("__")[0], {})
        b = mt.node_tree.nodes.get("Principled BSDF")
        look = {"ue": spec.get("ue", "cat_brushed_steel")}
        if b is not None and spec.get("ue_tint", True):
            look["color"] = [round(v, 4) for v in b.inputs["Base Color"].default_value[:3]]
            look["metallic"] = round(b.inputs["Metallic"].default_value, 3)
            look["roughness"] = round(b.inputs["Roughness"].default_value, 3)
            if "Coat Weight" in b.inputs and b.inputs["Coat Weight"].default_value > 0:
                look["coat"] = round(b.inputs["Coat Weight"].default_value, 3)
            if "Transmission Weight" in b.inputs and b.inputs["Transmission Weight"].default_value > 0:
                look["clear"] = round(b.inputs["Transmission Weight"].default_value, 3)
            em = b.inputs.get("Emission Strength")
            if em is not None and em.default_value > 0:
                look["glow"] = round(em.default_value, 3)
        looks[mt.name] = look
    return looks


def export_one(it):
    import bpy
    b3, m, cc, made = build_one(it)
    names = [n for n in made if not n.startswith("cut_") and bpy.data.objects.get(n) is not None
             and bpy.data.objects[n].type in ("MESH", "CURVE")]
    sel = it["select"]
    if isinstance(sel, tuple):
        names = [n for n in names if n.startswith(sel)]
    elif sel in ("front", "rear"):
        half = m["geometry"]["wheelbase"] / 2.0
        keep = []
        for n in names:
            lo, hi = box([n])
            front = (lo[0] + hi[0]) / 2 > half
            if front == (sel == "front"):
                keep.append(n)
        names = keep
    if not names:
        return dict(ok=False, why="the fit made no parts on " + it["model"])
    mp = mount_point(it, m, cc, names)
    keep = set(names)
    for ob in list(bpy.data.objects):
        if ob.name not in keep:
            bpy.data.objects.remove(ob, do_unlink=True)
    obs = [bpy.data.objects[n] for n in names]
    for ob in obs:
        ob.hide_set(False)
        ob.hide_viewport = False
        ob.parent = None
    bpy.ops.object.select_all(action="DESELECT")
    for ob in obs:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = obs[0]
    bpy.ops.object.convert(target="MESH")
    obs = [o for o in bpy.context.selected_objects if o.type == "MESH" and len(o.data.vertices)]
    for o in list(bpy.context.selected_objects):
        if o not in obs:
            o.select_set(False)
    bpy.context.view_layer.objects.active = obs[0]
    if len(obs) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    nm = f"{it['group']}_{it['key']}"
    ob.name = ob.data.name = nm
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    off = [v / 1000.0 for v in mp]
    for v in ob.data.vertices:
        v.co.x -= off[0]
        v.co.y -= off[1]
        v.co.z -= off[2]
    import numpy as np
    co = np.array([v.co[:] for v in ob.data.vertices]) * 1000.0
    lo, hi = co.min(0) + np.array(mp), co.max(0) + np.array(mp)
    dst = os.path.join(OUT, it["group"])
    os.makedirs(dst, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(dst, it["key"] + ".glb"), use_selection=True, export_apply=True, export_yup=True,
                              export_texcoords=True, export_normals=True, export_materials="EXPORT", export_cameras=False, export_lights=False)
    info = dict(group=it["group"], key=it["key"], name=it["name"], fitted_on=it["model"], family=m["family"],
                mount=it["mount"], mount_mm=[round(v, 1) for v in mp], moves_with=it["moves_with"],
                size_mm=[round(hi[i] - lo[i], 1) for i in range(3)], triangles=sum(len(p.vertices) - 2 for p in ob.data.polygons),
                surfaces=looks_of(ob, b3.LIB))
    json.dump(info, open(os.path.join(dst, it["key"] + ".json"), "w"), indent=1)
    return dict(ok=True, info=info)


# ---------------------------------------------------------------- driver
def manifest(results):
    parts = []
    for it in items():
        r = results.get(f"{it['group']}/{it['key']}")
        e = dict(id=f"{it['group']}/{it['key']}", group=it["group"], key=it["key"], name=it["name"], fits=it["fits"],
                 finishes=it["finishes"], mount=it["mount"], moves_with=it["moves_with"], script="foundry/" + it["script"],
                 fitted_on=lead_for(it["fits"]))
        if r and r.get("ok"):
            i = r["info"]
            e.update(file=f"{it['group']}/{it['key']}.glb", looks=f"{it['group']}/{it['key']}.json", mount_mm=i["mount_mm"],
                     size_mm=i["size_mm"], triangles=i["triangles"])
        else:
            e["file"] = None
            e["missing"] = (r or {}).get("why", "not exported")
        parts.append(e)
    chains = ho("brakes")["chains"]
    return dict(
        about="Common bike parts approved by beenster (stages 1-8b), one GLB per part: one mesh, one primitive per surface, "
              "+Y up, metres, origin at the mount point; looks JSON as build3d.export_game. Each file is the part as fitted "
              "on its lead bike (fitted_on); the script refits it to any bike. Not in Data/ until the switch-over is agreed.",
        axes="Blender +x forward, +y rider's left, +z up, written with export_yup like the bikes; mount_mm is the mount point "
             "on the lead bike in the bike's own mm (x from the rear axle line, z from the ground)",
        mounts=MOUNTS,
        moves_with=dict(steering="turns with the bars and fork", front_wheel="spins with the front wheel",
                        rear_wheel="spins with the rear wheel", swingarm="rides on the swingarm",
                        shock="spans frame to swingarm: stretches with the suspension", frame="fixed to the frame"),
        chains=dict(note="Chains are finishes of the bike's own chain (a material swap), so they have no file.",
                    finishes=[dict(key=c["key"], name=c["name"]) for c in chains]),
        parts=parts)


def main(argv):
    want = [a for a in argv if not a.startswith("--")]
    jobs = int(argv[argv.index("--jobs") + 1]) if "--jobs" in argv else 4
    want = [w for w in want if not w.isdigit()]
    todo = [it for it in items() if not want or it["group"] in want or it["key"] in want]
    os.makedirs(OUT, exist_ok=True)
    resf = os.path.join(OUT, "results.json")
    results = json.load(open(resf)) if os.path.exists(resf) else {}
    procs = []
    for it in todo:
        it = dict(it, model=lead_for(it["fits"]))
        while len(procs) >= jobs:
            procs = _reap(procs, results)
        log = open(os.path.join(OUT, f"log_{it['group']}_{it['key']}.txt"), "w")
        p = subprocess.Popen([sys.executable, os.path.abspath(__file__), "--one", json.dumps(it, default=list)],
                             stdout=log, stderr=subprocess.STDOUT, env=dict(os.environ, BIKE_DUMP_ONLY="1"))
        procs.append((p, it, log))
    while procs:
        procs = _reap(procs, results)
    json.dump(results, open(resf, "w"), indent=1)
    json.dump(manifest(results), open(os.path.join(OUT, "parts_manifest.json"), "w"), indent=1)
    bad = [k for k, v in results.items() if not v.get("ok")]
    print(f"EXPORT {len(results) - len(bad)} ok, {len(bad)} missing {bad}", flush=True)


def _reap(procs, results):
    import time
    left = []
    for p, it, log in procs:
        if p.poll() is None:
            left.append((p, it, log))
            continue
        log.close()
        path = os.path.join(OUT, f"log_{it['group']}_{it['key']}.txt")
        txt = open(path).read()
        line = next((ln for ln in txt.splitlines() if ln.startswith("PARTRESULT ")), None)
        r = json.loads(line[11:]) if line else dict(ok=False, why="crashed: " + txt.strip().splitlines()[-1][:300] if txt.strip() else "crashed")
        results[f"{it['group']}/{it['key']}"] = r
        print(f"PART {it['group']:16s} {it['key']:14s} {it['model']:18s} {'ok' if r.get('ok') else r.get('why')}", flush=True)
    if len(left) == len(procs):
        time.sleep(1)
    return left


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "--one":
        it = json.loads(a[1])
        it["select"] = tuple(it["select"]) if isinstance(it["select"], list) else it["select"]
        try:
            r = export_one(it)
        except Exception as e:  # noqa: BLE001
            import traceback
            traceback.print_exc()
            r = dict(ok=False, why=f"{type(e).__name__}: {e}"[:300])
        print("PARTRESULT " + json.dumps(r), flush=True)
    else:
        main(a)
