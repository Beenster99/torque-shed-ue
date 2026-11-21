"""Engine parts pass, stage 3 (plan https://claude.ai/artifact/TxPXHSRkftpdYsppy6dhXt): engine covers and cases with looks
of their own, fitting any engine of their type.

The joints (docs/engine_joints.md): the clutch and alternator cover rings on the side cases, the rocker covers on the
heads, the outer side cases and the sump. Each new cover is turned on the bike's own cover ring (its centre, side and
size read from the bike's own cover), inside the stock cover's depth, so it fits every engine of the type the way the
stock one does; the case ribs follow each case's own outline and stop short of the covers.

    COVERS   cast (the bike's own), finned (concentric cooling rings), billet (flat machined face, cap screws)
    FINISH   alloy, black, polished, gold      (the clutch, alternator and rocker covers and cam caps; looks only)
    CASES    stock (the bike's own), ribbed (side case ribs and sump fins), crackle (black wrinkle finish)

Looks only: none of these change performance (plan, parts table).
Our own designs with real parts as reference only: no maker's name, logo or exact product ships.

    python3 foundry/covers3d.py shots [--samples N]      # out/covers/*.png
    python3 foundry/covers3d.py check                    # every cover x case look on one bike of each engine type
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
OUT = os.path.join(ROOT, "out", "covers")

COVERS = {
    "cast": dict(label="Cast covers", note="The bike's own: cast clutch and alternator covers with their caps and bolts."),
    "finned": dict(label="Finned covers", note="Domed covers stepped with five cooling rings, a flat boss in the middle."),
    "billet": dict(label="Billet covers", note="Flat machined faces with a chamfered edge, a recessed centre and a ring of cap screws."),
}
FINISHES = {
    "alloy": dict(label="Natural alloy"), "black": dict(label="Black"), "polished": dict(label="Polished"), "gold": dict(label="Gold"),
}
CASES = {
    "stock": dict(label="Stock cases", note="The bike's own side cases and sump."),
    "ribbed": dict(label="Ribbed cases", note="Raised ribs, 7 mm deep, along each side case, stopping short of the covers, and cooling fins under the sump."),
    "crackle": dict(label="Black crackle cases", note="The side cases and sump in a black wrinkle finish."),
}
# the parts each finish goes on (the bike's own and ours)
FINISHED = re.compile(r"^(clutchcover\d+|clutchcap\d+|altcover\d+|cover\d+|boxercover-?1|camcap[\d-]*|cv_.*)$")
CASE_PARTS = re.compile(r"^(sidecase-?1|sump|cs_.*)$")


def use(b):
    global B
    B = b


def finish_mat(key):
    if key == "alloy":
        return B.mat("alu_cast")
    if key == "black":
        return D.surf("coverblack", (0.018, 0.018, 0.02), 0.5, 0.32)
    if key == "polished":
        return D.surf("polishedcase", (0.82, 0.82, 0.84), 1.0, 0.16, wear=0.05)
    if key == "gold":
        return D.surf("covergold", (0.86, 0.6, 0.2), 1.0, 0.24, wear=0.05)
    raise KeyError(key)


def crackle_mat():
    return D.surf("casecrackle", (0.016, 0.016, 0.018), 0.25, 0.72, bump=(3.0, 0.9), wear=0.02)


def drop(pattern):
    rx = re.compile(pattern)
    for o in [o for o in bpy.data.objects if rx.match(o.name)]:
        bpy.data.objects.remove(o, do_unlink=True)


# ---------------------------------------------------------------- the joints, read from the bike's own covers
def rings():
    """Each round cover: (kind, index, centre x z mm, y of its seat on the case, side sign, radius mm, stock depth mm)."""
    out = []
    for o in sorted(bpy.data.objects, key=lambda o: o.name):
        m = re.match(r"^(clutchcover|altcover)(\d+)$", o.name)
        if not m or o.type != "MESH":
            continue
        vs = [o.matrix_world @ v.co / B.MM for v in o.data.vertices]
        ys = [v.y for v in vs]
        sg = 1 if sum(ys) > 0 else -1
        seat = min(ys, key=abs)
        depth = max(abs(y - seat) for y in ys)
        xs, zs = [v.x for v in vs], [v.z for v in vs]
        r = (max(xs) - min(xs)) / 2
        out.append((m.group(1), m.group(2), ((max(xs) + min(xs)) / 2, (max(zs) + min(zs)) / 2), seat, sg, r, depth))
    return out


# ---------------------------------------------------------------- the covers
# the stock covers' profiles (detail3d), as (t, radius / ring radius): ours stay inside them, so they clear whatever the
# stock cover clears (a gear lever toe, a pump bolt, an oil cap)
STOCK = {"clutchcover": [(0, 1.02), (26, 1.0), (34, 0.95), (37, 0.88), (41, 0.68), (44, 0.62)],
         "altcover": [(0, 1.0), (24, 0.94), (40, 0.77), (50, 0.52), (53, 0.31)]}


def envelope(kind, t, R):
    pts = STOCK[kind]
    for (t0, f0), (t1, f1) in zip(pts, pts[1:]):
        if t0 <= t <= t1:
            return R * (f0 + (f1 - f0) * (t - t0) / max(1e-6, t1 - t0))
    return R * pts[-1][1] if t > pts[-1][0] else R * pts[0][1]


def inside(kind, R, prof):
    return [(t, min(r, envelope(kind, t, R))) for t, r in prof]


def build_cover(look, kind, i, c, seat, sg, R, depth, mat):
    """One of our covers on a cover ring: R the stock outer radius (its flange), within the stock depth."""
    p0 = (c[0], seat, c[1])
    ax = (0, sg, 0)
    r = R - 3          # the body, inside the flange
    nm = f"cv_{kind}{i}_"
    dep = min(depth, 44 if kind == "clutchcover" else 50)
    if look == "finned":
        prof = [(0, R), (6, R), (6, r)]
        n = 5
        for j in range(n):
            t = 8 + j * (dep - 14) / n
            rj = r * (1 - 0.13 * j)
            prof += [(t, rj), (t + 2.5, rj), (t + 2.5, rj - 5), (t + (dep - 14) / n, rj - 5)]
        prof += [(dep - 6, r * 0.3), (dep - 4, r * 0.28), (dep - 4, r * 0.22), (dep, r * 0.2), (dep, 0)]
        D.lathe(nm + "body", p0, ax, inside(kind, R, prof), mat, 64)
        return
    if look == "billet":
        top = 30 if kind == "clutchcover" else 26        # a flat face, where the stock cover is still near full width
        prof = inside(kind, R, [(0, R), (6, R), (6, r + 1), (top - 5, r + 1), (top, r - 4), (top, r * 0.72), (top - 3, r * 0.69),
                                (top - 3, r * 0.42), (top + 2, r * 0.4), (top + 2, 0)])
        D.lathe(nm + "body", p0, ax, prof, mat, 64)
        nb = 8
        rs = min(r * 0.84, envelope(kind, top + 4, R) - 7)
        for k in range(nb):
            a = math.radians(k * 360 / nb + 22.5)
            q = (c[0] + math.cos(a) * rs, seat + sg * top, c[1] + math.sin(a) * rs)
            D.bolt(nm + f"screw{k}", q, ax, 4.2, 3.0)
        return
    raise KeyError(look)


def build_case_ribs(mat):
    """Ribs along each outer side case's face (stopping short of the covers) and fins under the sump."""
    holes = [(c, r + 8) for _k, _i, c, _s, _g, r, _d in rings()]
    keep = re.compile(r"^(sidecase|crankcase|clutch|alt|cv_|cs_|bolt|casebolt|sump|waterpump|pump)")
    others = []      # every other part's box: a rib stops short of it (the swingarm, mounts, the gear lever)
    for o in bpy.data.objects:
        if o.type == "MESH" and not keep.match(o.name) and not o.hide_render:
            pts = [o.matrix_world @ Vector(c) / B.MM for c in o.bound_box]
            others.append(([min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]))
    for o in [o for o in bpy.data.objects if re.match(r"^sidecase-?1$", o.name) and o.type == "MESH"]:
        vs = [o.matrix_world @ v.co / B.MM for v in o.data.vertices]
        yo = max((v.y for v in vs), key=abs)
        sg = 1 if yo > 0 else -1
        face = [v for v in vs if abs(v.y - yo) < 1.0]
        if len(face) < 6:
            continue
        cx, cz = sum(v.x for v in face) / len(face), sum(v.z for v in face) / len(face)
        poly = sorted(((v.x, v.z) for v in face), key=lambda p: math.atan2(p[1] - cz, p[0] - cx))
        zs = [p[1] for p in poly]
        z = min(zs) + 22
        k = 0
        while z < max(zs) - 18:
            xs = []
            for a, b in zip(poly, poly[1:] + poly[:1]):
                if (a[1] - z) * (b[1] - z) < 0:
                    xs.append(a[0] + (b[0] - a[0]) * (z - a[1]) / (b[1] - a[1]))
            if len(xs) >= 2:
                spans = [(min(xs) + 16, max(xs) - 16)]
                for (hx, hz), hr in holes:       # stop short of each cover
                    if abs(z - hz) < hr:
                        w = math.sqrt(hr * hr - (z - hz) ** 2)
                        spans = [s for x0, x1 in spans for s in ((x0, min(x1, hx - w)), (max(x0, hx + w), x1)) if s[1] - s[0] > 24]
                ya_, yb_ = min(yo - sg * 2, yo + sg * 6), max(yo - sg * 2, yo + sg * 6)
                for lo, hi in others:
                    if lo[1] - 3 < yb_ and ya_ < hi[1] + 3 and lo[2] - 3 < z + 2.5 and z - 2.5 < hi[2] + 3:
                        spans = [s for x0, x1 in spans for s in ((x0, min(x1, lo[0] - 6)), (max(x0, hi[0] + 6), x1)) if s[1] - s[0] > 24]
                for x0, x1 in spans:
                    B.prism(f"cs_rib{sg}_{k}", [(x0, z - 3.5), (x1, z - 3.5), (x1, z + 3.5), (x0, z + 3.5)], ya_, yb_, mat, 1.5)
                    k += 1
            z += 26
    s = bpy.data.objects.get("sump")
    if s:
        vs = [s.matrix_world @ v.co / B.MM for v in s.data.vertices]
        x0, x1 = min(v.x for v in vs), max(v.x for v in vs)
        y0, y1 = min(v.y for v in vs), max(v.y for v in vs)
        zb = min(v.z for v in vs)
        n = max(3, int((y1 - y0) / 30))
        clear = all(not (lo[0] < x1 and x0 < hi[0] and lo[1] < y1 and y0 < hi[1] and lo[2] < zb + 2 and zb - 16 < hi[2]) for lo, hi in others)
        for j in range(n if clear else 0):      # fins only where nothing hangs under the sump (a belly silencer)
            y = y0 + 14 + j * (y1 - y0 - 28) / max(1, n - 1)
            B.prism(f"cs_fin{j}", [(x0 + 20, zb + 2), (x1 - 20, zb + 2), (x1 - 30, zb - 9), (x0 + 34, zb - 9)], y - 2, y + 2, mat, 1.0)


# ---------------------------------------------------------------- fitting
def fit(b, model, cover="cast", finish=None, case="stock"):
    use(b)
    before = set(bpy.data.objects.keys())
    rep = dict(model=model.get("name", ""), cover=cover, finish=finish, case=case, rings=[])
    R_ = rings()
    fm = finish_mat(finish) if finish else None
    if cover != "cast":
        drop(r"^(clutchcover\d+|altcover\d+|clutchcap\d+|clutchcapbolt\d+|altplug\d+|altplugslot\d+)$")
        for kind, i, c, seat, sg, r, depth in R_:
            build_cover(cover, kind, i, c, seat, sg, r, depth, fm or (B.mat("alu_cast") if kind == "clutchcover" else D.surf("coverblack", (0.018, 0.018, 0.02), 0.5, 0.32)))
            rep["rings"].append(dict(kind=kind, at=[round(c[0]), round(seat), round(c[1])], r=round(r)))
    if fm:
        for o in bpy.data.objects:
            if FINISHED.match(o.name) and o.type == "MESH" and len(o.data.materials):
                o.data.materials[0] = fm
    if case == "ribbed":
        cm = bpy.data.objects["sidecase1"].data.materials[0] if bpy.data.objects.get("sidecase1") else B.mat("alu_cast")
        build_case_ribs(cm)
    if case == "crackle":
        cm = crackle_mat()
        for o in bpy.data.objects:
            if CASE_PARTS.match(o.name) and o.type == "MESH" and len(o.data.materials):
                o.data.materials[0] = cm
    made = [n for n in bpy.data.objects.keys() if n not in before]
    return rep, made


def built(mn, cover, finish=None, case="stock"):
    import build3d
    use(build3d)
    BR.fresh()
    m = BR.models()[mn]
    build3d.DETAIL["on"] = True
    build3d.build(m)
    use(build3d)
    rep, made = fit(build3d, m, cover, finish, case)
    return build3d, m, rep, made


# ---------------------------------------------------------------- pictures and the check
TYPE_BIKES = {"single": "varga_mx", "two_stroke": "corvano_mx", "parallel_twin": "hartwell_classic", "vtwin90": "corvano_naked",
              "vtwin45": "lodestar_cruiser", "boxer": "rheinhart_classic", "triple": "hartwell_naked", "inline4": "kazeda_naked"}
# (cover, finish, case, bike, side: -1 the clutch side, 1 the alternator side)
SHOTS = [("finned", "polished", "stock", "corvano_naked", -1), ("billet", "black", "stock", "corvano_naked", -1),
         ("billet", "gold", "ribbed", "kazeda_naked", -1), ("finned", "alloy", "crackle", "hartwell_classic", 1),
         ("finned", "polished", "ribbed", "lodestar_cruiser", -1), ("billet", "black", "crackle", "varga_mx", 1),
         ("cast", "gold", "ribbed", "rheinhart_classic", 1)]


def _bbox(rx):
    lo, hi = None, None
    for o in bpy.data.objects:
        if o.type != "MESH" or not rx.match(o.name):
            continue
        for c in o.bound_box:
            p = o.matrix_world @ Vector(c) / B.MM
            lo = p.copy() if lo is None else Vector(tuple(min(lo[i], p[i]) for i in range(3)))
            hi = p.copy() if hi is None else Vector(tuple(max(hi[i], p[i]) for i in range(3)))
    return (lo, hi) if lo is not None else None


def shots(samples=40):
    import lamps3d as L
    os.makedirs(OUT, exist_ok=True)
    rep = {}
    for cover, fin, case, mn, side in SHOTS:
        b3, m, r, made = built(mn, cover, fin, case)
        key = f"{mn}__{cover}__{fin}__{case}"
        rep[key] = r
        bb = _bbox(re.compile(r"^(sidecase-?1|sump|crankcase)$"))
        lo, hi = bb
        tgt = tuple((lo + hi) / 2)
        span = max(hi[0] - lo[0], hi[2] - lo[2], 380)
        L.render_at(b3, os.path.join(OUT, key + ".png"), BR.vadd(tgt, (span * 0.45, side * span * 2.3, span * 0.35)), tgt, 50, samples=samples)
    json.dump(rep, open(os.path.join(OUT, "shots.json"), "w"), indent=1)


OK = ("cv | sidecase", "cv | crankcase", "cv | bolt", "cv | cv", "cs | sidecase", "cs | crankcase", "cs | sump", "cs | cs",
      "cs | bolt", "cs | casebolt", "cv | casebolt", "cs | clutch", "cs | alt", "cv | waterpump", "cs | waterpump")


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
        built(mn, "cast")       # the stock covers' own touches (a gear lever toe over a cover): ours may match, not exceed
        own = [o.name for o in bpy.data.objects if re.match(r"^(clutchcover|altcover)\d+$", o.name)]
        base = {}
        for k, v in BR.clip_check(own).items():
            a_, b_ = [x.strip() for x in k.split("|")]
            base[(a_, b_)] = v
        for cover in COVERS:
            for case in ("stock", "ribbed"):
                if cover == "cast" and case == "stock":
                    continue
                b3, m, r, made = built(mn, cover, None, case)
                bad = {}
                for k, v in BR.clip_check(made).items():
                    a_, b_ = [x.strip() for x in k.split("|")]
                    kind = re.sub(r"^cv_(clutchcover|altcover).*$", r"\1", a_)
                    if ok_pair(k) or v <= base.get((kind, b_), -1) + 1.0:
                        continue
                    bad[k] = v
                res.setdefault(et, {})[f"{cover}+{case}"] = dict(model=mn, clips=bad, rep=r, made=len(made), stock_touches={f"{a} | {b}": v for (a, b), v in base.items() if not ok_pair(f"cv | {b}")})
                print(f"CCHECK {et:13s} {cover:7s} {case:7s} rings {len(r['rings'])} parts {len(made)} {bad}", flush=True)
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
