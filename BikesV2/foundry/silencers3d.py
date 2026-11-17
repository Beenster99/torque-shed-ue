"""Common parts, stage 5: silencers (common parts plan, 16 Nov).

The joint (docs/common_parts_joints.md): at the end of the bike's own pipe, a slip joint whose sleeve
reaches forward over the pipe (the bikes thread's join rule: pipe in line with the can's mouth, the
inlet sleeve about 34 mm over the pipe), its point and direction taken from the bike's own can. One
hanger strap round the body with a stay to the nearest part of the subframe or peg hanger. Each bike gives
the longest can that clears the swingarm, the rear tyre, the pegs and the rider's heel: a can is shortened
in 20 mm steps until it touches nothing. Pipes stay with the bikes thread; only the can and its joint are
common.

Our own designs with real parts as reference only: no maker's name, logo or exact product ships.

    python3 foundry/silencers3d.py shots [keys] [--samples N]   # out/silencers/*.png
    python3 foundry/silencers3d.py check                         # every silencer on the lead bike of each family
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
import lamps3d as L  # noqa: E402

B = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "silencers")

ALL = ("Naked", "Sport", "Cafe", "Classic", "Cruiser", "Adventure", "MX", "Chopper")
SILENCERS = {
    "reverse_cone": dict(label="Reverse cone", fams=("Naked", "Cafe", "Classic", "Cruiser", "Chopper"), length=560, k=1.0,
                         note="A long chrome megaphone that widens towards the back and closes in a short reverse cone round the outlet."),
    "peashooter": dict(label="Peashooter", fams=("Cafe", "Classic", "Cruiser", "Chopper"), length=620, k=0.66,
                       note="A slim chrome tube that tapers a little to a rolled lip, open and dark inside."),
    "shaker": dict(label="Cocktail shaker", fams=("Naked", "Cafe", "Classic"), length=430, k=0.95,
                   note="A plain round can with a domed end and a short tail pipe, in brushed steel."),
    "round_ti": dict(label="Round titanium", fams=("Naked", "Sport", "Adventure", "MX"), length=400, k=1.0,
                     note="A titanium tube between carbon end caps, riveted, with a short steel outlet."),
    "oval_carbon": dict(label="Oval carbon", fams=("Naked", "Sport", "Adventure", "MX"), length=390, k=1.05,
                        note="A tall, slim oval carbon sleeve with titanium end caps, slim enough to tuck in by the tyre."),
    "hex": dict(label="Hexagon", fams=("Naked", "Sport", "Adventure"), length=340, k=1.0,
                note="A six-sided black stainless body with a bright cone at each end."),
    "short_cone": dict(label="Short cone", fams=("Naked", "Sport"), length=250, k=1.05,
                       note="A stubby black cone that turns outboard from its sleeve, so the wide bore with its bright rolled lip points out to the side."),
    "slash_cut": dict(label="Slash cut", fams=("Cruiser", "Chopper"), length=700, k=0.9,
                      note="A straight chrome drag pipe silencer, its end cut at a slant."),
}
STAY_TO = ("pillionhanger", "hanger", "subframe", "frameloop", "rearframe", "frame", "tailframe", "rearloop")


def use(b):
    global B
    B = b
    L.use(b)


def mats():
    soot = D.surf("pipeblack", (0.0015, 0.0015, 0.0015), 0.0, 1.0, wear=0.0)
    soot.node_tree.nodes["Principled BSDF"].inputs["Specular IOR Level"].default_value = 0.0
    return dict(chrome=B.mat("chrome"), steel=B.mat("steel"), pipe=B.mat("exhaust"), black=B.mat("exhaust_black"), brushed=D.brushed(),
                carbon=__import__("carbon").material(),
                ti=D.surf("sl_ti", (0.56, 0.57, 0.62), 1.0, 0.3, wear=0.05), soot=soot, dark=B.mat("alu_dark"))


# ---------------------------------------------------------------- one silencer
def build_can(key, nm, a3, d3, Ln, R, rh, M):
    """One silencer from the slip joint at a3 along d3, Ln long, body radius R, over a pipe of radius rh.
    Returns (strap t, outer radius there) for the hanger."""
    lt = lambda name, prof, mat, segs=48, **kw: D.lathe(nm + name, a3, d3, prof, mat, segs, **kw)
    sharp = dict(sharp=False, caps=False)
    s_in = rh + 2.6
    # the slip joint: a sleeve over the pipe end and a clamp round it
    lt("inlet", [(-34, rh + 0.4), (-34, s_in), (8, s_in), (8, rh + 0.4)], M["pipe"] if key not in ("round_ti", "oval_carbon") else M["ti"], 36, **sharp)
    lt("clamp", [(-26, s_in), (-26, s_in + 3), (-12, s_in + 3), (-12, s_in), (-26, s_in)], M["steel"], 28, caps=False)
    if key == "reverse_cone":
        lt("body", [(6, s_in), (Ln * 0.12, s_in + 3), (Ln * 0.5, s_in + (R * 1.12 - s_in) * 0.45), (Ln * 0.86, R * 1.12), (Ln * 0.88, R * 1.12 + 1.2)], M["chrome"], 56, **sharp)
        lt("end", [(Ln * 0.88, R * 1.12 + 1.2), (Ln * 0.9, R * 1.12 + 1.2), (Ln, R * 0.6), (Ln + 1, R * 0.56), (Ln - 2, R * 0.52)], M["chrome"], 56, **sharp)
        lt("bore", [(Ln - 1, R * 0.53), (Ln - 70, R * 0.53), (Ln - 70, 0)], M["soot"], 40, caps=False)
        return Ln * 0.62, s_in + (R * 1.12 - s_in) * 0.62
    if key == "peashooter":
        lt("body", [(6, s_in), (40, R), (Ln - 8, R * 0.9), (Ln - 2, R * 0.88), (Ln, R * 0.82), (Ln - 1.5, R * 0.76)], M["chrome"], 48, **sharp)
        lt("bore", [(Ln - 1.2, R * 0.77), (Ln - 90, R * 0.77), (Ln - 90, 0)], M["soot"], 36, caps=False)
        return Ln * 0.6, R * 0.94
    if key == "shaker":
        lt("body", [(6, s_in), (36, R * 0.7), (54, R), (Ln - 46, R), (Ln - 26, R * 0.88), (Ln - 12, R * 0.62), (Ln - 6, R * 0.4), (Ln - 6, 0)], M["brushed"], 56, sharp=False, caps=False)
        lt("band", [(54, R + 0.4), (54, R + 1.6), (60, R + 1.6), (60, R + 0.4)], M["chrome"], 56, caps=False)
        lt("tail", [(Ln - 14, R * 0.28), (Ln + 22, R * 0.28), (Ln + 24, R * 0.26), (Ln + 24, R * 0.22), (Ln - 14, R * 0.22)], M["chrome"], 32, caps=False)
        lt("bore", [(Ln + 23, R * 0.221), (Ln - 20, R * 0.221), (Ln - 20, 0)], M["soot"], 28, caps=False)
        return Ln * 0.5, R
    if key in ("round_ti", "oval_carbon", "hex"):
        ell = (1.0, 0.7) if key == "oval_carbon" else (1.0, 1.0)
        body, cap = (M["ti"], M["carbon"]) if key == "round_ti" else (M["carbon"], M["ti"]) if key == "oval_carbon" else (M["black"], M["steel"])
        segs, sh = (6, True) if key == "hex" else (56, False)
        f0, f1 = Ln * 0.16, Ln * 0.84
        lt("capf", [(6, s_in), (f0 * 0.5, s_in + (R - s_in) * 0.55), (f0, R * 0.98), (f0 + 2, R * 0.98)], cap, 56, ell=ell, **sharp)
        lt("body", [(f0 - 1, R), (f1 + 1, R)], body, segs, sharp=sh, caps=False, ell=ell)
        lt("capr", [(f1 - 2, R * 0.98), (f1, R * 0.98), (Ln - 6, R * 0.86), (Ln, R * 0.6), (Ln, R * 0.44)], cap, 56, ell=ell, **sharp)
        ro = R * 0.42 * (ell[1] if key == "oval_carbon" else 1.0)
        lt("outlet", [(Ln - 8, ro), (Ln + 9, ro), (Ln + 11, ro - 1.2), (Ln + 11, ro - 2.6), (Ln - 8, ro - 2.8)], M["steel"], 36, caps=False)
        lt("bore", [(Ln + 10, ro - 2.7), (Ln - 50, ro - 2.7), (Ln - 50, 0)], M["soot"], 28, caps=False)
        # rivets round both joints between body and caps
        ax = Vector(d3).normalized()
        side = ax.cross(Vector((0, 1, 0))).normalized()
        up = side.cross(ax)
        for j, tt in enumerate((f0 + 4, f1 - 4)):
            for k in range(8 if key != "hex" else 6):
                w = 2 * math.pi * (k + 0.5) / (8 if key != "hex" else 6)
                rr = R * (math.cos(math.pi / 6) if key == "hex" else 1.0)
                p = Vector(a3) + ax * tt + side * (rr * ell[0] * math.cos(w)) + up * (rr * ell[1] * math.sin(w))
                n_ = (side * (ell[1] * math.cos(w)) + up * (ell[0] * math.sin(w))).normalized()
                B.sphere(f"{nm}rivet{j}{k}", tuple(p + n_ * 0.2), 2.0, M["steel"])
        return Ln * 0.5, R
    if key == "short_cone":
        # the body turns outboard from the sleeve, so the outlet points out to the side (beenster, stage 5)
        sgn = 1 if a3[1] >= 0 else -1
        w = math.radians(28)
        d_ = Vector(d3)
        dn = d_
        dh = Vector((d_.x, d_.y, 0.0))
        hl = dh.length
        if hl > 1e-6:
            dh /= hl
            outv = Vector((0.0, float(sgn), 0.0))
            dh = (dh * math.cos(w) + outv * math.sin(w)).normalized() * hl
            dn = Vector((dh.x, dh.y, d_.z)).normalized()
        p8 = tuple(a3[k] + d3[k] * 6 for k in range(3))
        d3 = tuple(dn)
        a3 = tuple(p8[k] - d3[k] * 6 for k in range(3))
        lt = lambda name, prof, mat, segs=48, **kw: D.lathe(nm + name, a3, d3, prof, mat, segs, **kw)
        lt("elbow", [(2, s_in), (12, s_in)], M["pipe"], 36, sharp=False, caps=False)
        lt("body", [(6, s_in), (30, R * 0.72), (Ln - 12, R * 1.0), (Ln - 3, R * 1.02)], M["black"], 48, **sharp)
        lt("lip", [(Ln - 6, R * 1.02), (Ln - 3, R * 1.05), (Ln + 1, R * 1.0), (Ln + 1, R * 0.88), (Ln - 3, R * 0.85)], M["steel"], 48, **sharp)
        lt("bore", [(Ln - 2, R * 0.86), (Ln * 0.35, R * 0.86), (Ln * 0.35, 0)], M["soot"], 40, caps=False)
        lt("core", [(Ln * 0.35, R * 0.45), (Ln * 0.7, R * 0.45), (Ln * 0.7, R * 0.38), (Ln * 0.35, R * 0.38)], M["black"], 28, caps=False)
        return Ln * 0.55, R * 0.9
    if key == "slash_cut":
        lt("body", [(6, s_in), (44, R), (Ln + 40, R), (Ln + 42, R - 1.5), (Ln + 40, R - 3)], M["chrome"], 56, **sharp)
        lt("bore", [(Ln + 40, R - 3), (Ln - 80, R - 3), (Ln - 80, 0)], M["soot"], 40, caps=False)
        # the slant: everything past Ln - 60 slides along the axis by its height, so the cut faces down and back
        ax = Vector(d3).normalized()
        side = ax.cross(Vector((0, 1, 0))).normalized()
        up = side if side.z > 0 else -side
        a_v = Vector(B.V(*a3))
        for o in (bpy.data.objects[nm + "body"], bpy.data.objects[nm + "bore"]):
            for v in o.data.vertices:
                wv = o.matrix_world @ v.co
                r_ = wv - a_v
                t_, h_ = r_.dot(ax) / B.MM, r_.dot(up) / B.MM
                if t_ > Ln - 60:
                    w_ = min(1.0, (t_ - (Ln - 60)) / 60)
                    wv = wv + ax * ((-h_ - R) * 0.7 * w_ * B.MM)
                    v.co = o.matrix_world.inverted() @ wv
        return Ln * 0.6, R
    raise KeyError(key)


# ---------------------------------------------------------------- fitting
def _tree(o):
    from mathutils.bvhtree import BVHTree
    dg = bpy.context.evaluated_depsgraph_get()
    ev = o.evaluated_get(dg)
    me = ev.to_mesh()
    t = BVHTree.FromPolygons([ev.matrix_world @ v.co for v in me.vertices], [tuple(p.vertices) for p in me.polygons])
    ev.to_mesh_clear()
    return t


def _hits(mine, skip):
    """The bike parts any of our parts overlaps (names not starting with skip)."""
    ours = [_tree(bpy.data.objects[n]) for n in mine if bpy.data.objects[n].type == "MESH" and not n.endswith(("inlet", "clamp"))]
    out = []
    for o in bpy.data.objects:
        if o.type != "MESH" or o.name in mine or o.name.startswith(skip):
            continue
        t = _tree(o)
        if any(a.overlap(t) for a in ours):
            out.append(o.name)
    return out


def _nearest(p, prefixes):
    best = None
    for o in bpy.data.objects:
        if o.type != "MESH" or not o.name.startswith(prefixes):
            continue
        q = _tree(o).find_nearest(Vector(B.V(*p)))
        if q and q[0] is not None:
            d = q[3] / B.MM
            if best is None or d < best[0]:
                best = (d, tuple(c / B.MM for c in q[0]), o.name)
    return best


def fit(b, model, c, key):
    """Swap the bike's silencers for one of ours. Returns a report and the names of the parts made."""
    use(b)
    M = mats()
    fam = model["family"]
    rep = dict(model=model.get("name", ""), family=fam, key=key, cans=[])
    before = set(bpy.data.objects.keys())
    st = SILENCERS[key]
    if fam not in st["fams"]:
        return rep, []
    ex = model["exhaust"]
    both, left, under = (ex.get("side") == k_ for k_ in ("both", "left", "under"))
    belly = B.is_belly(ex, 0)
    skip = ("header", "canlink") + (("can0", "canband0", "canrivetband0", "cancore0", "canclamp0", "cancone0", "canend0", "canbore0") if belly else ())
    for i, can in enumerate(ex.get("cans", [])):
        if B.is_belly(ex, i):
            continue
        for sg in ((1, -1) if both or under else (1,) if left else (-1,)):
            pat = re.compile(rf"can[a-z]*{i}{sg}")
            for o in [o for o in bpy.data.objects if pat.match(o.name)]:
                bpy.data.objects.remove(o, do_unlink=True)
            y0c, y1c = B.can_y(ex, i, under)
            a3, b3 = (can["a"][0], sg * y0c, can["a"][1]), (can["b"][0], sg * y1c, can["b"][1])
            own = math.sqrt(sum((b3[k] - a3[k]) ** 2 for k in range(3)))
            d3 = tuple((b3[k] - a3[k]) / own for k in range(3))
            rh = ex["headers"][min(i, len(ex["headers"]) - 1)].get("w", 44) / 2 * 0.8
            R = can["w"] / 2 * 0.86 * st["k"]
            nm = f"sl{i}{sg}_"
            if can.get("box"):
                R = min(R, can["box"] / 2 * (1.0 if key != "oval_carbon" else 1.0 / 0.7))     # as slim across as the bike's own box
            top = min(st["length"], max(own * 1.3, own + 150))      # no further than the bike's own space allows
            best = None
            for rs in (1.0, 0.9, 0.8):
                Ln = top
                while True:
                    mark = set(bpy.data.objects.keys())
                    strap = build_can(key, nm, a3, d3, Ln, R * rs, rh, M)
                    mine = [o for o in bpy.data.objects.keys() if o not in mark]
                    hit = _hits(mine, skip + ("sl",))
                    for o in mine:
                        bpy.data.objects.remove(bpy.data.objects[o], do_unlink=True)
                    if not hit or Ln <= top * 0.45:
                        break
                    Ln -= 20
                if not hit and (best is None or Ln > best[0] + 30):
                    best = (Ln, rs)
                if best and best[0] >= top * 0.75:
                    break
            Ln, rs = best or (top * 0.45, 0.8)
            mark = set(bpy.data.objects.keys())
            strap = build_can(key, nm, a3, d3, Ln, R * rs, rh, M)
            mine = [o for o in bpy.data.objects.keys() if o not in mark]
            hit = _hits(mine, skip + ("sl",))
            # the hanger: a strap round the body and a stay to the nearest subframe or peg hanger part
            tS, rS = strap
            ctr = tuple(a3[k] + d3[k] * (tS + 8) for k in range(3))
            nb = _nearest(ctr, STAY_TO)
            if nb and nb[0] < 260:      # no strap where the bike gives nothing to hang it from (it hangs on its pipe)
                D.lathe(nm + "strap", a3, d3, [(tS, rS + 0.4), (tS, rS + 3), (tS + 16, rS + 3), (tS + 16, rS + 0.4), (tS, rS + 0.4)], M["black"], 48, caps=False,
                        ell=(1.0, 0.7) if key == "oval_carbon" else (1.0, 1.0))
            stay = None
            if nb and nb[0] < 260:
                q = Vector(nb[1])
                v = (q - Vector(ctr))
                v.normalize()
                p0 = Vector(ctr) + v * (rS + 1)
                p1 = q + v * 4
                B.smooth_tube(nm + "stay", [tuple(p0), tuple((p0 + p1) / 2), tuple(p1)], 4.5, M["dark"])
                stay = dict(to=nb[2], mm=round(nb[0]))
            rep["cans"].append(dict(i=i, side=sg, length=round(Ln), own=round(own), radius=round(R * rs, 1), slim=rs, touches=hit, stay=stay))
    made = [o for o in bpy.data.objects.keys() if o not in before]
    return rep, made


def built(mn, key):
    import build3d
    b3, m, cc, r, made0 = L.built(mn)
    use(build3d)
    rep, made = fit(build3d, m, cc, key)
    return build3d, m, cc, rep, made


# ---------------------------------------------------------------- pictures and the check
SHOTS = [
    ("reverse_cone", "hartwell_classic"),
    ("peashooter", "lodestar_cruiser"),
    ("shaker", "hartwell_cafe"),
    ("round_ti", "corvano_adventure"),
    ("oval_carbon", "varga_mx"),
    ("hex", "corvano_naked"),
    ("short_cone", "kazeda_sport"),
    ("slash_cut", "lodestar_chopper"),
]


def shots(which=None, samples=40):
    os.makedirs(OUT, exist_ok=True)
    rep = {}
    for key, mn in SHOTS:
        if which and key not in which:
            continue
        b3, m, cc, r, made = built(mn, key)
        rep[key] = r
        c_ = L._centre(("sl",))
        if not c_:
            print("NO SILENCER", mn, key)
            continue
        ctr, lo, hi = c_
        side = -1 if hi[1] <= 0 or (abs(lo[1]) > abs(hi[1])) else 1
        tgt = (ctr[0], ctr[1], ctr[2])
        span = max(hi[0] - lo[0], 300)
        L.render_at(b3, os.path.join(OUT, f"{key}.png"), BR.vadd(tgt, (-span * 0.9, side * span * 2.0, span * 0.5)), tgt, 50, samples=samples)
    path = os.path.join(OUT, "shots.json")
    old = json.load(open(path)) if os.path.exists(path) else {}
    old.update(rep)
    json.dump(old, open(path, "w"), indent=1)


OK = ("sl_inlet | can", "sl_clamp | can", "sl | header", "sl | canlink", "sl | sl", "sl | can0", "sl | canband0", "sl | canrivetband0", "sl | cancore0", "sl | canclamp0", "sl | cancone0")


def ok_pair(pair):
    a, b_ = [x.strip() for x in pair.split("|")]
    for j in OK:
        ja, jb = [x.strip() for x in j.split("|")]
        if (a.startswith(ja) and b_.startswith(jb)) or (b_.startswith(ja) and a.startswith(jb)):
            return True
    if "stay" in a or "stay" in b_:
        tgt = b_ if "stay" in a else a
        return tgt.startswith(STAY_TO)
    return False


def check(keys=None):
    res = {}
    leads = dict(BR.LEADS)
    leads["Chopper"] = "lodestar_chopper"
    for fam, mn in leads.items():
        for key, h in SILENCERS.items():
            if fam not in h["fams"] or (keys and key not in keys):
                continue
            b3, m, cc, r, made = built(mn, key)
            bad = {k: v for k, v in BR.clip_check(made).items() if not ok_pair(k)}
            res.setdefault(key, {})[fam] = dict(model=mn, clips=bad, fitted=bool(made), rep=r)
            print(f"SCHECK {key:13s} {fam:10s} {[(q['length'], q['own'], q['slim'], q['touches'], q['stay'] and q['stay']['to']) for q in r['cans']]} {bad}", flush=True)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "check.json")
    old = json.load(open(path)) if (keys and os.path.exists(path)) else {}
    for k, v in res.items():
        old.setdefault(k, {}).update(v)
    json.dump(old, open(path, "w"), indent=1)


if __name__ == "__main__":
    a = sys.argv[1:]
    cmd = a[0] if a else "shots"
    smp = int(a[a.index("--samples") + 1]) if "--samples" in a else 40
    rest = [x for x in a[1:] if not x.startswith("--") and x != str(smp)]
    if cmd == "shots":
        shots(rest or None, smp)
    elif cmd == "check":
        check(rest or None)
