"""Engine parts pass, stage 2 (plan https://claude.ai/artifact/TxPXHSRkftpdYsppy6dhXt): fuel systems, air intakes and
oil filters with looks of their own, fitting any engine of their type.

The joint (docs/engine_joints.md): each cylinder's inlet stub on the head, its point and axis read from the bike's own
stub (detail3d lathes it from t 0 at the port along the inlet axis). A fuel system replaces the bike's throttle bodies
from the stub's clamp outwards; an air intake replaces the airbox (and its hoses) at the fuel system's mouth. Pod
filters and velocity stacks are shortened in 10 mm steps until they touch nothing, as the silencers are.

    FUEL   throttle_bodies (the bike's own; redrawn as ours on its stubs when an open intake goes on), round_slide carbs,
           flat_slide carbs (a 45 degree V-twin takes one, on the right, from a Y manifold)
    AIR    airbox (the bike's own), pods (open cone filters), stacks (velocity stacks)
    OIL    black, chrome (the oil filter canister's finish)

Carbs come with an open intake (the airbox is shaped round the bike's throttle bodies). A run may turn at the stub's
rubber manifold towards level and a carb's slide tower may lean out where the tank sits low (the fit search).
The two-stroke keeps its own carb (a fuel system for it only); every other engine takes all three.
Our own designs with real parts as reference only: no maker's name, logo or exact product ships.

    python3 foundry/intake3d.py shots [--samples N]      # out/intake/*.png
    python3 foundry/intake3d.py check                    # every fuel x air pair on one bike of each engine type
"""
import json
import math
import os
import re
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bars3d as BR  # noqa: E402
import detail3d as D  # noqa: E402

B = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "intake")

FUEL = {
    "throttle_bodies": dict(label="Throttle bodies", note="The bike's own: an alloy body on each port, an injector on top, one shaft and pulley.",
                            perf={}),
    "round_slide": dict(label="Round-slide carbs", note="A cast carb on each port: round slide tower with a chrome cap, float bowl underneath, short bellmouth.",
                        perf={"torque_mult": 1.02, "top_mult": 0.98}),
    "flat_slide": dict(label="Flat-slide carbs", note="Square race carbs: flat slide housing, a cable lever box on the side, a long bellmouth.",
                       perf={"torque_mult": 0.97, "top_mult": 1.04}),
}
AIR = {
    "airbox": dict(label="Airbox", note="The bike's own box and paper filter.", perf={}),
    "pods": dict(label="Pod filters", note="An open cone filter on each mouth: red pleated gauze between a chrome clamp ring and end cap.",
                 perf={"top_mult": 1.03}, length=95),
    "stacks": dict(label="Velocity stacks", note="A polished trumpet on each mouth with a rolled lip and a fine mesh.",
                   perf={"top_mult": 1.05, "torque_mult": 0.97}, length=70),
}
OIL = {"black": dict(label="Black canister"), "chrome": dict(label="Chrome canister")}
ONLY_OWN = ("two_stroke",)       # engine types whose fuel system is their own only


def pair_ok(fuel, air):
    """Carbs come with an open intake (pods or stacks): the bike's airbox is shaped round its own throttle bodies."""
    return not (fuel == "throttle_bodies" and air == "airbox") and not (fuel != "throttle_bodies" and air == "airbox")


def use(b):
    global B
    B = b


def mats():
    return dict(alu=B.mat("alu_cast"), alu_bright=B.mat("alu"), chrome=B.mat("chrome"), steel=B.mat("steel"), rubber=B.mat("rubber"),
                dark=B.mat("alu_dark"), gauze=D.surf("podgauze", (0.55, 0.05, 0.04), 0.0, 0.85, wear=0.0),
                mesh=D.surf("stackmesh", (0.05, 0.05, 0.05), 0.6, 0.6, wear=0.0),
                polished=D.surf("polishedcase", (0.82, 0.82, 0.84), 1.0, 0.16, wear=0.05))


def engine_type(model):
    import engine_joints
    return engine_joints.engine_type(model)


# ---------------------------------------------------------------- the joint
def ports():
    """Each inlet stub on the built bike: (key, origin mm, unit axis), read from its first and last rings."""
    out = []
    for o in sorted(bpy.data.objects, key=lambda o: o.name):
        m = re.match(r"^inletstub(-?\d+)$", o.name)
        if not m or o.type != "MESH":
            continue
        vs = [o.matrix_world @ v.co for v in o.data.vertices]
        segs = len(vs) // 5            # detail3d: five profile rings
        if segs < 3:
            continue
        c0 = sum(vs[:segs], Vector()) / segs
        c1 = sum(vs[-segs:], Vector()) / segs
        ax = (c1 - c0).normalized()
        out.append((m.group(1), tuple(c0 / B.MM), tuple(ax)))
    return out


def frame_at(ax):
    """Two unit vectors across the axis: up (nearest the world's up) and side."""
    a = Vector(ax)
    up = Vector((0, 0, 1)) - a * a.z
    if up.length < 0.3:
        up = Vector((1, 0, 0)) - a * a.x
    up.normalize()
    return up, a.cross(up).normalized()


def drop(*prefixes):
    for o in [o for o in bpy.data.objects if o.name.startswith(prefixes)]:
        bpy.data.objects.remove(o, do_unlink=True)


# ---------------------------------------------------------------- the parts
def build_fuel(key, nm, o, ax, M, roll=0.0):
    """One fuel system on a port, its slide tower turned roll degrees about the axis (towards the side). Returns the
    mouth: (t along the axis, radius)."""
    lt = lambda name, prof, mat, segs=40, **kw: D.lathe(nm + name, o, ax, prof, mat, segs, **kw)
    a, (up0, side0) = Vector(o), frame_at(ax)
    r_ = math.radians(roll)
    up, side = up0 * math.cos(r_) + side0 * math.sin(r_), side0 * math.cos(r_) - up0 * math.sin(r_)
    at = lambda t, u=0.0, s=0.0: tuple(a + Vector(ax) * t + up * u + side * s)
    if key == "throttle_bodies":
        lt("body", [(24, 22), (30, 24), (80, 24), (84, 22)], M["alu"], 40)
        lt("mouth", [(82, 23), (88, 25), (90, 25), (90, 0)], M["alu"], 40)
        B.cone(nm + "injector", at(40, 18), at(40, 40), 7, 6, M["dark"], 16)
        B.cone(nm + "injcap", at(40, 40), at(40, 46), 8, 8, M["rubber"], 16)
        B.cone(nm + "pulley", at(60, 0, 24), at(60, 0, 30), 15, 15, M["alu_bright"], 24)
        return 90, 25
    if key == "round_slide":
        lt("body", [(24, 21), (30, 23.5), (78, 23.5), (82, 21)], M["alu"], 40)
        lt("mouth", [(80, 22), (92, 24), (98, 27), (100, 27), (100, 0)], M["alu"], 40)
        B.cone(nm + "tower", at(50, 10), at(50, 52), 21, 20, M["alu"], 32)
        B.cone(nm + "towercap", at(50, 52), at(50, 60), 22.5, 19, M["chrome"], 32)
        B.cone(nm + "cablenut", at(50, 60), at(50, 72), 4.5, 4.5, M["steel"], 12)
        B.cone(nm + "bowl", at(54, -14), at(54, -46), 22, 20, M["alu"], 32)
        B.cone(nm + "drain", at(54, -46), at(54, -52), 5, 5, M["steel"], 6)
        B.cone(nm + "tickler", at(34, 18, -18), at(34, 30, -22), 4, 4, M["chrome"], 10)
        return 100, 27
    if key == "flat_slide":
        lt("body", [(24, 22), (30, 25), (84, 25), (88, 22)], M["dark"], 4, square=4.0)
        lt("mouth", [(86, 23), (104, 25), (118, 30), (121, 31), (121, 0)], M["alu_bright"], 40)
        B.cone(nm + "slidebox", at(56, 18), at(56, 44), 26, 24, M["dark"], 4)
        B.cone(nm + "slidecap", at(56, 44), at(56, 48), 24, 22, M["alu_bright"], 4)
        B.cone(nm + "leverbox", at(56, 30, 26), at(56, 30, 44), 12, 12, M["alu_bright"], 16)
        B.cone(nm + "leverpin", at(56, 30, 44), at(56, 30, 50), 5, 5, M["steel"], 10)
        B.cone(nm + "bowl", at(58, -14), at(58, -42), 20, 18, M["dark"], 4)
        return 121, 31
    raise KeyError(key)


def build_air(key, nm, o, ax, t0, r0, Ln, M):
    """One air intake on a fuel system's mouth (t0, radius r0 along the port's axis), Ln long."""
    lt = lambda name, prof, mat, segs=40, **kw: D.lathe(nm + name, o, ax, prof, mat, segs, **kw)
    if key == "pods":
        R = r0 + 12 * min(1.0, Ln / 60.0)     # a short pod is a slim one too
        lt("clamp", [(t0 - 6, r0 + 0.6), (t0 - 6, r0 + 3), (t0 + 10, r0 + 3), (t0 + 10, r0 + 0.6)], M["chrome"], 40, caps=False)
        prof = [(t0 + 8, r0 + 4)]
        n = max(4, int((Ln - 22) / 7))
        for k in range(n):        # the pleats: rings of gauze, wider towards the end
            t = t0 + 14 + k * (Ln - 26) / n
            rk = r0 + 4 + (R - r0 - 4) * (k / max(1, n - 1)) ** 0.7
            prof += [(t, rk), (t + 3.5, rk - 1.6)]
        lt("gauze", prof + [(t0 + Ln - 10, R)], M["gauze"], 56, sharp=False, caps=False)
        lt("cap", [(t0 + Ln - 12, R + 1.5), (t0 + Ln - 4, R + 1.2), (t0 + Ln, R * 0.7), (t0 + Ln, 0)], M["chrome"], 56, sharp=False)
        return
    if key == "stacks":
        lt("stack", [(t0 - 4, r0 - 1), (t0 + Ln * 0.55, r0 + 2), (t0 + Ln * 0.85, r0 + 9), (t0 + Ln - 3, r0 + 18), (t0 + Ln, r0 + 21),
                     (t0 + Ln - 2, r0 + 23), (t0 + Ln - 7, r0 + 21)], M["polished"], 56, sharp=False, caps=False)
        lt("mesh", [(t0 + Ln - 6, r0 + 19), (t0 + Ln - 6, 0)], M["mesh"], 40)
        return
    raise KeyError(key)


# ---------------------------------------------------------------- fitting
OWN_FUEL = ("throttle", "injector", "fuelrail", "throttleshaft", "throttlepulley")
OWN_AIR = ("airbox", "inlethose", "airfilter", "svc_air_filter")


def fit(b, model, fuel="throttle_bodies", air="airbox", oil=None):
    """Swap the bike's fuel system, air intake and oil filter finish for ours. Returns a report and the parts made."""
    use(b)
    M = mats()
    et = engine_type(model)
    rep = dict(model=model.get("name", ""), type=et, fuel=fuel, air=air, oil=oil, ports=[], skipped=None)
    before = set(bpy.data.objects.keys())
    if oil:
        m_ = M["chrome"] if oil == "chrome" else D.surf("oilcan", (0.03, 0.03, 0.035), 0.3, 0.45)
        for o in bpy.data.objects:
            if o.name.startswith("svc_oil_filter") and o.type == "MESH":
                o.data.materials.clear()
                o.data.materials.append(m_)
    if et in ONLY_OWN and fuel != "throttle_bodies":
        rep["skipped"] = "a two-stroke keeps its own carb"
        return rep, []
    P = ports()
    own = et in ONLY_OWN       # a two-stroke keeps its own carb: an open intake goes straight on its mouth
    if (fuel != "throttle_bodies" or air != "airbox") and not own:
        drop(*OWN_FUEL)
    if air != "airbox":
        drop(*OWN_AIR)
    carb = (fuel != "throttle_bodies" or air != "airbox") and not own    # our own fuel system, from the stub's clamp
    if carb and et == "vtwin45" and len(P) == 2:
        P = [_side_carb(P, M)]       # one carb on the right, fed from both heads by a Y manifold, as on real 45 degree twins
    bike = _bike_parts()
    for key, o, ax in P:
        nm = f"in{key}_"
        ax_v, o_v = Vector(ax), Vector(o)
        # where the run may bend (a rubber manifold): at the stub's clamp, turning towards level (TILT) on its own heading
        tb = 24
        flat = Vector((ax_v.x, ax_v.y, 0))
        flat = (flat.normalized() + Vector((0, 0, TILT))).normalized() if flat.length > 0.2 else ax_v
        sgn = 1 if o_v.y > 5 else (-1 if o_v.y < -5 else 0)
        rolls = [0.0] if fuel == "throttle_bodies" else ([0.0] + [r * sgn for r in ROLL] if sgn else [0.0] + [r for q in ROLL for r in (q, -q)])
        best = None

        def make(f, roll, Ln):
            ax2 = (ax_v * (1 - f) + flat * f).normalized()
            bend = o_v + ax_v * tb
            o2 = tuple(bend - ax2 * tb)
            if f > 0:
                _boot(nm + "boot", o_v, ax_v, bend, ax2, tb, 26 if carb else 27, M)
            mouth = build_fuel(fuel, nm, o2, tuple(ax2), M, roll) if carb else (96, 25)
            if air != "airbox":
                build_air(air, nm + "a", o2, tuple(ax2), mouth[0], mouth[1], Ln, M)

        for f in (BEND if carb else (0.0,)):
            for roll in rolls:
                for Ln in _lengths(air):
                    mark = set(bpy.data.objects.keys())
                    make(f, roll, Ln)
                    mine = [n for n in bpy.data.objects.keys() if n not in mark]
                    hit = _hits(mine, bike)
                    score = (sum(hit.values()), f, abs(roll), -(Ln or 0))
                    if best is None or score < best[0]:
                        best = (score, f, roll, Ln, hit)
                    for n in mine:
                        bpy.data.objects.remove(bpy.data.objects[n], do_unlink=True)
                    if not hit:
                        break
                if best[0][0] == 0:
                    break
            if best[0][0] == 0:
                break
        _score, f, roll, Ln, hit = best
        make(f, roll, Ln)
        rep["ports"].append(dict(port=key, at=[round(v) for v in o], axis=[round(v, 3) for v in ax], bend=round(f, 2), roll=roll,
                                 air_length=Ln if air != "airbox" else None, touching=hit))
    made = [n for n in bpy.data.objects.keys() if n not in before]
    return rep, made


def _side_carb(P, M):
    """The 45 degree V-twin's single carb: a Y manifold from both stubs to the middle of the V, then out to the right."""
    (k0, o0, a0), (k1, o1, a1) = P
    o0, a0, o1, a1 = Vector(o0), Vector(a0), Vector(o1), Vector(a1)
    e0, e1 = o0 + a0 * 20, o1 + a1 * 20          # the stubs' clamp ends
    mid = (e0 + e1) / 2
    for k, e in ((k0, e0), (k1, e1)):
        L_ = (mid - e).length + 14
        D.lathe(f"iny{k}", tuple(e), tuple((mid - e).normalized()), [(-6, 23), (L_, 23)], M["alu"], 32)
    out = Vector((0, -1, 0))
    D.lathe("inyside", tuple(mid), tuple(out), [(-20, 25), (26, 25)], M["alu"], 32)
    return ("s", tuple(mid), tuple(out))


BEND = (0.0, 0.35, 0.7, 1.0)          # how far a run may turn from the port's axis towards level
TILT = -0.1                           # level: the port's heading, a touch downhill so fuel runs to the bowl
ROLL = (35.0, 60.0)                   # a carb's slide tower may lean out to the side by these (degrees) where the tank sits low
HIT_MM = 3.0                          # deeper than this into a bike part is a clash


def _lengths(air):
    if air == "airbox":
        return [None]
    top = AIR[air]["length"]
    return list(range(top, int(top * 0.35) - 1, -10))


def _boot(name, o, ax, bend, ax2, tb, r, M):
    """The rubber manifold where a run turns: along the stub to the bend, then a short collar along the new axis."""
    D.lathe(name, tuple(o), tuple(ax), [(tb - 12, r), (tb + 2, r)], M["rubber"], 32)
    D.lathe(name + "c", tuple(bend - ax2 * tb), tuple(ax2), [(tb - 4, r), (tb + 6, r)], M["rubber"], 32)


def _part(o):
    from mathutils.bvhtree import BVHTree
    import clip3d
    dg = bpy.context.evaluated_depsgraph_get()
    ev = o.evaluated_get(dg)
    me = ev.to_mesh()
    vs = [ev.matrix_world @ v.co for v in me.vertices]
    polys = [tuple(p.vertices) for p in me.polygons]
    ev.to_mesh_clear()
    if len(vs) < 4 or not polys:
        return None
    lo = Vector(tuple(min(v[i] for v in vs) for i in range(3)))
    hi = Vector(tuple(max(v[i] for v in vs) for i in range(3)))
    return dict(name=o.name, stem=clip3d.stem(o.name), vs=vs, lo=lo, hi=hi, tree=BVHTree.FromPolygons(vs, polys, epsilon=0.0))


def _bike_parts():
    """Every bike part our runs may not enter (the stubs, cameras and lamps left out), measured once per fit."""
    out = []
    for o in bpy.data.objects:
        if o.type != "MESH" or o.hide_render or o.name.startswith(("in", "cam", "lamp")):
            continue
        p = _part(o)
        if p:
            out.append(p)
    return out


def _hits(mine, bike):
    """The bike parts our new run sits inside, with how deep (mm): clip3d's measure both ways."""
    import clip3d
    ours = [p for p in (_part(bpy.data.objects[n]) for n in mine if bpy.data.objects[n].type == "MESH") if p]
    out = {}
    for a_ in ours:
        for b_ in bike:
            if any(a_["lo"][k] > b_["hi"][k] or b_["lo"][k] > a_["hi"][k] for k in range(3)):
                continue
            if not a_["tree"].overlap(b_["tree"]):
                continue
            dd = max(clip3d.depth(a_, b_)[0], clip3d.depth(b_, a_)[0])
            if dd > HIT_MM:
                out[b_["name"]] = max(out.get(b_["name"], 0), round(dd, 1))
    return out


def built(mn, fuel, air, oil=None):
    import build3d
    use(build3d)
    BR.fresh()
    m = BR.models()[mn]
    build3d.DETAIL["on"] = True
    build3d.build(m)
    use(build3d)
    rep, made = fit(build3d, m, fuel, air, oil)
    return build3d, m, rep, made


# ---------------------------------------------------------------- pictures and the check
# one bike of each engine type (the family leads where they have the type)
TYPE_BIKES = {"single": "varga_mx", "two_stroke": "corvano_mx", "parallel_twin": "hartwell_classic", "vtwin90": "corvano_naked",
              "vtwin45": "lodestar_cruiser", "boxer": "rheinhart_classic", "triple": "hartwell_naked", "inline4": "kazeda_sport"}
SHOTS = [("round_slide", "pods", "black", "hartwell_classic"), ("flat_slide", "stacks", "chrome", "kazeda_naked"),
         ("round_slide", "stacks", "chrome", "lodestar_cruiser"), ("flat_slide", "pods", "black", "corvano_naked"),
         ("round_slide", "pods", "chrome", "rheinhart_classic"), ("throttle_bodies", "pods", "black", "hartwell_naked"),
         ("throttle_bodies", "stacks", "chrome", "varga_mx")]


def _bbox(prefix):
    lo, hi = None, None
    for o in bpy.data.objects:
        if o.type != "MESH" or not o.name.startswith(prefix):
            continue
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            p = Vector(tuple(v / B.MM for v in w))
            lo = p.copy() if lo is None else Vector(tuple(min(lo[i], p[i]) for i in range(3)))
            hi = p.copy() if hi is None else Vector(tuple(max(hi[i], p[i]) for i in range(3)))
    return (lo, hi) if lo is not None else None


LIFT_OFF = ("tank", "cap", "radshroud", "seam", "behind", "svc_fuel_pump", "svc_fuel_clip", "seat", "saddle", "panel", "sidecover", "fairing", "belly", "numberboard", "cowl", "tail")


def shots(samples=40):
    """A close three-quarter view from behind and above of each pair's intake, and one of each oil filter finish."""
    import lamps3d as L
    os.makedirs(OUT, exist_ok=True)
    rep = {}
    ours = tuple(f"in{k}" for k in ("0", "1", "2", "3", "-1", "s_", "y", "s")) + ("inletstub",)
    jobs = [(f, a_, o, mn, f"{mn}__{f}__{a_}", ours) for f, a_, o, mn in SHOTS]
    jobs += [("throttle_bodies", "airbox", o, "kazeda_naked", f"oil__{o}", ("svc_oil_filter",)) for o in OIL]
    for fuel, air, oil, mn, key, prefix in jobs:
        b3, m, r, made = built(mn, fuel, air, oil)
        rep[key] = r
        bb = _bbox(prefix)
        if not bb:
            print("NOTHING MADE", key)
            continue
        lo, hi = bb
        tgt = tuple((lo + hi) / 2)
        side = -1 if tgt[1] < -5 else 1
        if prefix == ours:      # the tank, seat and bodywork lifted off so the intake shows
            for o in bpy.data.objects:
                if o.name.startswith(LIFT_OFF) or (o.type != "MESH" and o.parent and o.parent.name.startswith(LIFT_OFF)):
                    o.hide_render = True
            span = max(hi[0] - lo[0], hi[2] - lo[2], 320)
            cam = (-span * 0.7, side * span * 3.4, span * 1.6)
        else:
            span = max(hi[0] - lo[0], hi[2] - lo[2], 260)
            cam = (span * 0.9, side * span * 2.2, span * 0.15)
        L.render_at(b3, os.path.join(OUT, key + ".png"), BR.vadd(tgt, cam), tgt, 50, samples=samples)
    json.dump(rep, open(os.path.join(OUT, "shots.json"), "w"), indent=1)


OK = ("in | inletstub", "in | inletclamp", "in | in", "in | throttle", "in | injector")


def ok_pair(pair):
    a, b_ = [x.strip() for x in pair.split("|")]
    for j in OK:
        ja, jb = [x.strip() for x in j.split("|")]
        if (a.startswith(ja) and b_.startswith(jb)) or (b_.startswith(ja) and a.startswith(jb)):
            return True
    return False


def check(types=None):
    res = {}
    for et, mn in TYPE_BIKES.items():
        if types and et not in types:
            continue
        for fuel in FUEL:
            for air in AIR:
                if not pair_ok(fuel, air):
                    continue
                b3, m, r, made = built(mn, fuel, air)
                bad = {k: v for k, v in BR.clip_check(made).items() if not ok_pair(k)}
                res.setdefault(et, {})[f"{fuel}+{air}"] = dict(model=mn, clips=bad, rep=r)
                print(f"ICHECK {et:13s} {fuel:15s} {air:7s} ports {len(r['ports'])} lens {[p['air_length'] for p in r['ports']]} {r['skipped'] or ''} {bad}", flush=True)
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
