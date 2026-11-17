"""Common parts, stage 6: footpegs, heel plates and foot controls (common parts plan, 16 Nov).

The joint (docs/common_parts_joints.md): the peg pivot points in the model (hangers, circle) NEVER move (rider
pose thread). Each peg stands where the bike's own peg stood, from the same inner face, on the bike's own
pivot pin. Heel plates take the bike's own heel plate outline and bolt holes (drilled or swept), so they cover
what it covered. Foot controls keep the bike's pivot and toe points (pivot 6 mm ahead of and 26 mm above the
peg, toe 135 mm ahead and 8 mm above), so the boot finds them where it did, and the bike keeps its gear
linkage and brake rod. Cruiser and chopper forward controls use their own peg points in the same way.

MX bikes have no 3D pegs in the V2 build yet (their hanger is drawn as a flat outline): here they take pegs
at the end of that outline's peg bar, (600, 404), until the bikes thread gives them a pivot point.

Our own designs with real parts as reference only: no maker's name, logo or exact product ships.

    python3 foundry/pegs3d.py shots [keys] [--samples N]   # out/pegs/*.png
    python3 foundry/pegs3d.py check                         # every part on the lead bike of each family
"""
import json
import math
import os
import re
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bars3d as BR  # noqa: E402
import detail3d as D  # noqa: E402
import lamps3d as L  # noqa: E402

B = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "pegs")
MX_PEG = (600, 404)

PEGS = {
    "knurled": dict(label="Knurled billet", fams=("Naked", "Sport", "Cafe", "Adventure"),
                    note="Machined aluminium with a fine knurled grip and a chamfered end."),
    "rubber": dict(label="Rubber topped", fams=("Naked", "Cafe", "Classic", "Cruiser", "Chopper"),
                   note="A steel core in a ribbed rubber sleeve, the classic road peg."),
    "cleat": dict(label="Wide cleat", fams=("Adventure", "MX"),
                  note="A wide steel platform with sharp teeth along both edges, for standing on the pegs off road."),
    "race": dict(label="Race peg", fams=("Sport", "Naked", "Cafe"),
                 note="Short and slim, black with a bright knurled band and a cone tip, for rear-set controls."),
    "board": dict(label="Floorboard", fams=("Cruiser", "Chopper"),
                  note="A long ribbed rubber board on a chrome-edged plate, pivoting where the peg was."),
    "bullet": dict(label="Chrome bullet", fams=("Classic", "Cruiser", "Chopper"),
                   note="Chrome with three rubber rings and a bullet nose."),
}
HEELS = {
    "lightened": dict(label="Lightened", fams=("Naked", "Sport", "Cafe", "Classic", "Adventure"),
                      note="Brushed aluminium with three big lightening holes."),
    "guard": dict(label="Black guard", fams=("Naked", "Sport", "Cafe", "Classic", "Adventure"),
                  note="Solid black anodised plate with a rubber heel pad where the boot rubs."),
    "carbon": dict(label="Carbon", fams=("Naked", "Sport", "Cafe", "Adventure"),
                   note="A carbon fibre plate on bright bolts."),
}
PEDALS = {
    "billet": dict(label="Billet folding tip", fams=("Naked", "Sport", "Cafe", "Classic", "Adventure"),
                   note="Flat machined arms with a lightening slot; a knurled folding toe on the gear lever and a folding pad on the brake."),
    "heeltoe": dict(label="Heel and toe", fams=("Cruiser", "Chopper"),
                    note="A chrome rocker shifter with a toe peg and a heel peg, and a chrome brake pedal with a big rubber pad."),
    "forged": dict(label="Forged steel", fams=("MX", "Adventure"),
                   note="Round-section bent steel; a sprung toe on the gear lever and a wide toothed tip on the brake."),
}


def use(b):
    global B
    B = b
    L.use(b)


def mats():
    return dict(alu=B.mat("alu"), steel=B.mat("steel"), chrome=B.mat("chrome"), dark=B.mat("alu_dark"), rubber=B.mat("rubber"),
                black=D.surf("pg_black", (0.02, 0.02, 0.022), 0.85, 0.32, wear=0.05), brushed=D.brushed(),
                carbon=__import__("carbon").material(),
                gold=D.surf("pg_gold", (0.42, 0.27, 0.08), 1.0, 0.3, wear=0.0))


# ---------------------------------------------------------------- pegs
def build_peg(key, nm, p, s, y0, M, pillion=False):
    """One peg from the inner face y0 (abs) outwards, centred on the pivot point p (x, z)."""
    at = (p[0], s * y0, p[1])
    ax = (0, s, 0)
    lt = lambda name, prof, mat, segs=24, **kw: D.lathe(nm + name, at, ax, prof, mat, segs, **kw)
    if key == "board" and pillion:
        key = "rubber"
    if key == "knurled":
        prof = [(0, 0), (0, 15), (6, 15), (8, 12.5)]
        t = 10.0
        while t < 76:
            prof += [(t, 12.5), (t + 0.8, 13.4), (t + 2.2, 13.4), (t + 3.0, 12.5)]
            t += 3.2
        prof += [(80, 12.5), (82, 13.5), (86, 13.5), (88, 11), (88, 0)]
        lt("body", prof, M["alu"], 28)
    elif key == "rubber":
        lt("core", [(0, 0), (0, 14), (8, 14), (8, 10), (90, 10), (90, 0)], M["steel"], 24)
        prof = [(10, 10), (10, 14)]
        t = 12.0
        while t < 82:
            prof += [(t, 14), (t + 1.5, 15.5), (t + 4, 15.5), (t + 5.5, 14)]
            t += 7.0
        prof += [(86, 14), (89, 12.5), (90, 9), (10, 9)]
        lt("rubber", prof, M["rubber"], 28, sharp=False, caps=False)
    elif key == "cleat":
        # a wide toothed platform: a channel section with teeth up the front and back edges
        x0, x1, zt = p[0] - 24, p[0] + 24, p[1] + 9
        B.prism(nm + "plate", [(x0, p[1] - 12), (x1, p[1] - 12), (x1 + 2, zt), (x0 - 2, zt)], s * (y0 + 2), s * (y0 + 82), M["steel"], 1.2)
        D.lathe(nm + "boss", at, ax, [(0, 0), (0, 15), (8, 15), (8, 0)], M["steel"], 20)
        for k in range(7):
            yy = s * (y0 + 10 + 11 * k)
            for xe in (x0 - 1, x1 + 1):
                B.cone(f"{nm}tooth{k}{int(xe)}", (xe, yy, zt - 1), (xe, yy, zt + 6), 4.5, 0.6, M["steel"], 4)
    elif key == "race":
        lt("body", [(0, 0), (0, 13), (5, 13), (7, 10), (52, 10), (58, 8), (70, 4), (72, 0)], M["black"], 28, sharp=False)
        prof = [(16, 10)]
        t = 16.0
        while t < 42:
            prof += [(t, 10.4), (t + 0.8, 11.2), (t + 2.0, 11.2), (t + 2.8, 10.4)]
            t += 2.8
        prof += [(42, 10)]
        lt("knurl", prof, M["alu"], 28, caps=False)
        lt("tip", [(58, 8.2), (70, 4.2), (72.5, 0)], M["gold"], 24, sharp=False)
    elif key == "board":
        # the board: the boot sits on it ahead of and behind the pivot, so it runs from 150 behind to 90 ahead
        xa, xb, zb = p[0] - 150, p[0] + 90, p[1] + 4
        ya, yb = s * (y0 + 6), s * (y0 + 118)
        ol = D.chaikin([(xa, zb - 8), (xb, zb - 8), (xb + 6, zb), (xb, zb + 4), (xa, zb + 4), (xa - 6, zb)], 2)
        B.prism(nm + "plate", ol, ya, yb, M["chrome"], 2.0)
        for k in range(9):
            xk = xa + 14 + k * (xb - xa - 28) / 8
            B.prism(f"{nm}rib{k}", [(xk - 7, zb + 3), (xk + 7, zb + 3), (xk + 6, zb + 9), (xk - 6, zb + 9)], s * (y0 + 14), s * (y0 + 110), M["rubber"], 1.5)
        D.lathe(nm + "boss", at, ax, [(0, 0), (0, 14), (10, 14), (10, 0)], M["chrome"], 20)
    elif key == "bullet":
        lt("body", [(0, 0), (0, 14), (6, 14), (8, 11.5), (66, 11.5), (76, 10.5), (84, 7), (88, 2.5), (88.6, 0)], M["chrome"], 28, sharp=False)
        for k, t in enumerate((20, 36, 52)):
            lt(f"ring{k}", [(t, 11.2), (t, 13.8), (t + 2, 14.6), (t + 8, 14.6), (t + 10, 13.8), (t + 10, 11.2)], M["rubber"], 28, sharp=False, caps=False)
    else:
        raise KeyError(key)


# ---------------------------------------------------------------- heel plates
def heel_outline(model, p):
    """The bike's own heel plate outline and bolt points (detail3d.rear_end), so ours cover the same ground."""
    hp = [(p[0] + 52, p[1] + 127), (p[0] + 34, p[1] + 74), (p[0] + 22, p[1] + 68), (p[0] - 12, p[1] + 66), (p[0] - 24, p[1] + 44),
          (p[0] - 46, p[1] + 36), (p[0] - 78, p[1] + 50), (p[0] - 92, p[1] + 92), (p[0] - 76, p[1] + 138), (p[0] - 30, p[1] + 152), (p[0] + 18, p[1] + 148)]
    holes = [(p[0] - 44, p[1] + 110, 13), (p[0] - 12, p[1] + 100, 9), (p[0] - 20, p[1] + 132, 8)]
    if model.get("detail", {}).get("heel_plate") == "swept":
        hp = [(p[0] + 50, p[1] + 130), (p[0] + 38, p[1] + 86), (p[0] + 12, p[1] + 70), (p[0] - 30, p[1] + 62), (p[0] - 84, p[1] + 68), (p[0] - 138, p[1] + 92),
              (p[0] - 156, p[1] + 114), (p[0] - 128, p[1] + 132), (p[0] - 64, p[1] + 146), (p[0], p[1] + 152), (p[0] + 30, p[1] + 149)]
        holes = [(p[0] - 104, p[1] + 108, 12), (p[0] - 62, p[1] + 112, 14), (p[0] - 22, p[1] + 116, 11)]
    return D.chaikin(hp, 3), holes, ((p[0] + 30, p[1] + 108), (p[0] + 8, p[1] + 130))


def build_heel(key, nm, model, p, s, ya, M, sp):
    hp, holes, bolts = heel_outline(model, p)
    y_in, y_out = (ya, ya + 4) if s > 0 else (-ya - 4, -ya)
    mat = M["brushed"] if key == "lightened" else M["black"] if key == "guard" else M["carbon"]
    pl = B.prism(nm + "plate", hp, y_in, y_out, mat, 0)
    cuts = [((sp[0], s * (ya - 6), sp[1]), (sp[0], s * (ya + 10), sp[1]), 25)]
    if key == "lightened":
        cuts += [((x, s * (ya - 6), z), (x, s * (ya + 10), z), r) for x, z, r in holes]
    D.drill(pl, cuts, 0)
    pl.data.polygons.foreach_set("use_smooth", [False] * len(pl.data.polygons))
    if key == "guard":
        # the rubber heel pad: a raised oval where the boot's heel rubs
        cx, cz = sum(q[0] for q in holes) / len(holes), sum(q[1] for q in holes) / len(holes)
        pad = [(cx + 34 * math.cos(2 * math.pi * k / 24), cz + 18 * math.sin(2 * math.pi * k / 24)) for k in range(24)]
        B.prism(nm + "pad", pad, s * (ya + 4) if s > 0 else -ya - 7, s * (ya + 7) if s > 0 else -ya - 4, M["rubber"], 1.0)
    for k, q in enumerate(bolts):
        D.lathe(f"{nm}spacer{k}", (q[0], s * (ya - 3), q[1]), (0, s, 0), [(0, 0), (0, 6), (3, 6), (3, 0)], M["steel"], 12)
        D.bolt(f"{nm}bolt{k}", (q[0], s * (ya + 4), q[1]), (0, s, 0), 5.2, 2.6, M["chrome"] if key == "carbon" else M["steel"])


# ---------------------------------------------------------------- foot controls
def build_pedals(key, model, p, M, ys):
    """Gear lever (left, +y) and brake pedal (right, -y) from the bike's pivot to its toe point."""
    piv = (p[0] + 6, p[1] + 26)
    toe = (p[0] + 135, p[1] + 8)
    for s, what in ((1, "gear"), (-1, "brake")):
        y0 = ys.get(s, 176)
        nm = f"fc{what}"
        D.lathe(nm + "pivot", (piv[0], s * y0, piv[1]), (0, s, 0), [(0, 0), (0, 11), (14, 11), (16, 9), (16, 0)], M["dark"], 24)
        yl = s * (y0 + 9)
        if key == "billet":
            # a flat arm, 8 thick, tapering from the pivot boss to the tip, with a slot through it
            dx, dz = toe[0] - piv[0], toe[1] - piv[1]
            Lr = math.hypot(dx, dz)
            u, n = (dx / Lr, dz / Lr), (-dz / Lr, dx / Lr)
            ol = [(piv[0] + n[0] * 12 - u[0] * 12, piv[1] + n[1] * 12 - u[1] * 12), (toe[0] + n[0] * 7, toe[1] + n[1] * 7),
                  (toe[0] + u[0] * 6, toe[1] + u[1] * 6), (toe[0] - n[0] * 7, toe[1] - n[1] * 7), (piv[0] - n[0] * 12 - u[0] * 12, piv[1] - n[1] * 12 - u[1] * 12)]
            arm = B.prism(nm + "arm", D.chaikin(ol, 2), yl - 4, yl + 4, M["black"], 1.5)
            m0, m1 = (piv[0] + u[0] * 40, piv[1] + u[1] * 40), (piv[0] + u[0] * 84, piv[1] + u[1] * 84)
            D.drill(arm, [((m0[0] + (m1[0] - m0[0]) * k / 5, yl - 8, m0[1] + (m1[1] - m0[1]) * k / 5), (m0[0] + (m1[0] - m0[0]) * k / 5, yl + 8, m0[1] + (m1[1] - m0[1]) * k / 5), 4.2) for k in range(6)], 0)
            if what == "gear":
                prof = [(0, 0), (0, 9)]
                t = 4.0
                while t < 38:
                    prof += [(t, 9), (t + 0.8, 9.8), (t + 2.0, 9.8), (t + 2.8, 9)]
                    t += 3.0
                prof += [(40, 9), (42, 7), (42, 0)]
                D.lathe(nm + "toe", (toe[0], yl + s * 2, toe[1]), (0, s, 0), prof, M["alu"], 20)
            else:
                B.prism(nm + "toe", D.chaikin([(toe[0] - 14, toe[1] - 7), (toe[0] + 18, toe[1] - 5), (toe[0] + 20, toe[1] + 4), (toe[0] - 14, toe[1] + 3)], 2),
                        yl - s * 3 if s > 0 else yl - 42, yl + 42 if s > 0 else yl + 3, M["alu"], 1.2)
            D.bolt(nm + "fold", (toe[0] - 6, yl + s * 4, toe[1]), (0, s, 0), 4.2, 2.4, M["steel"])
        elif key == "heeltoe":
            # a chrome rocker: toe arm forward, heel arm back and up, a chrome peg with a rubber ring on each
            mid = (piv[0] + 50, piv[1] - 12)
            B.smooth_tube(nm + "arm", [(piv[0], yl, piv[1]), (mid[0], yl + s * 4, mid[1]), (toe[0], yl + s * 6, toe[1])], 6.5, M["chrome"])
            if what == "gear":
                heel = (piv[0] - 96, piv[1] + 40)
                B.smooth_tube(nm + "heelarm", [(piv[0], yl, piv[1]), (piv[0] - 50, yl + s * 4, piv[1] + 24), (heel[0], yl + s * 6, heel[1])], 6.5, M["chrome"])
                for tag, q in (("toe", toe), ("heel", heel)):
                    D.lathe(nm + tag, (q[0], yl + s * 2, q[1]), (0, s, 0), [(0, 0), (0, 8), (4, 8), (4, 11), (36, 11), (40, 8), (42, 0)], M["rubber"], 20, sharp=False)
                    D.lathe(nm + tag + "cap", (q[0], yl + s * 42, q[1]), (0, s, 0), [(0, 0), (0, 8), (3, 6), (4, 0)], M["chrome"], 20, sharp=False)
            else:
                pad = [(toe[0] - 26 + 30 * (1 + math.cos(2 * math.pi * k / 28)), toe[1] + 14 * math.sin(2 * math.pi * k / 28)) for k in range(28)]
                B.prism(nm + "padplate", pad, yl - s * 2 if s > 0 else yl - 58, yl + 58 if s > 0 else yl + 2, M["chrome"], 2.0)
                B.prism(nm + "pad", [(x, z + 3) for x, z in pad], yl + 4 if s > 0 else yl - 54, yl + 54 if s > 0 else yl - 4, M["rubber"], 2.0)
        elif key == "forged":
            mid = (piv[0] + 64, piv[1] - 20)
            B.smooth_tube(nm + "arm", [(piv[0], yl, piv[1]), (mid[0], yl + s * 2, mid[1]), (toe[0] - 8, yl + s * 4, toe[1])], 7.0, M["steel"])
            if what == "gear":
                D.lathe(nm + "toe", (toe[0], yl + s * 2, toe[1]), (0, s, 0), [(0, 0), (0, 7.5), (30, 7.5), (32, 6), (32, 0)], M["steel"], 16)
                D.lathe(nm + "spring", (toe[0] - 10, yl + s * 4, toe[1]), (1, 0, 0), [(0, 0), (0, 5), (10, 5), (10, 0)], M["dark"], 12)
            else:
                x0, x1, zt = toe[0] - 18, toe[0] + 16, toe[1] + 6
                yy0, yy1 = (yl - 2, yl + 44) if s > 0 else (yl - 44, yl + 2)
                B.prism(nm + "tip", [(x0, toe[1] - 8), (x1, toe[1] - 8), (x1, zt), (x0, zt)], yy0, yy1, M["steel"], 1.0)
                for k in range(4):
                    yy = yy0 + 6 + (yy1 - yy0 - 12) * k / 3
                    B.cone(f"{nm}tooth{k}", (x1, yy, zt - 1), (x1, yy, zt + 5), 3.6, 0.5, M["steel"], 4)
        else:
            raise KeyError(key)


# ---------------------------------------------------------------- fitting
def _inner_y(name, default):
    bb = L._bbox((name,))
    if bb is None:
        return default
    lo, hi = bb
    return min(abs(lo[1]), abs(hi[1]))


def fit(b, model, c, peg=None, heel=None, pedal=None):
    use(b)
    M = mats()
    fam = model["family"]
    before = set(bpy.data.objects.keys())
    rep = dict(model=model.get("name", ""), family=fam)
    pegs = [tuple(s_["at"]) for s_ in model.get("hangers", []) if s_.get("circle")]
    if not pegs and fam == "MX":
        pegs = [MX_PEG]
        rep["mx_guess"] = True
    sp = tuple(model["swingarm"]["pivot"])
    if peg and fam in PEGS[peg]["fams"]:
        for i, p in enumerate(pegs):
            for s in (-1, 1):
                y0 = _inner_y(f"peg{i}{s}", 172.0)
                ob = bpy.data.objects.get(f"peg{i}{s}")
                if ob is not None:
                    bpy.data.objects.remove(ob, do_unlink=True)
                build_peg(peg, f"pg{i}{s}_", p, s, y0, M, pillion=i > 0)
        rep["peg"] = peg
    if heel and fam in HEELS[heel]["fams"] and bpy.data.objects.get("heelplate1") is not None:
        for s in (-1, 1):
            ya = _inner_y(f"heelplate{s}", 181.0)
            for o in [o for o in bpy.data.objects if o.name.startswith((f"heelplate{s}", f"heelspacer{s}", f"heelbolt{s}"))]:
                bpy.data.objects.remove(o, do_unlink=True)
            build_heel(heel, f"hp{s}_", model, pegs[0], s, ya, M, sp)
        rep["heel"] = heel
    if pedal and fam in PEDALS[pedal]["fams"] and pegs:
        ys = {1: _inner_y("gearpivot", 176.0), -1: _inner_y("brakepivot", 176.0)}
        for nm in ("gearpivot", "gearlever", "geartoe", "brakepivot", "brakepedal", "brakepedaltip"):
            ob = bpy.data.objects.get(nm)
            if ob is not None:
                bpy.data.objects.remove(ob, do_unlink=True)
        build_pedals(pedal, model, pegs[0], M, ys)
        rep["pedal"] = pedal
    made = [o for o in bpy.data.objects.keys() if o not in before and not o.startswith("cut_")]
    return rep, made


def built(mn, peg=None, heel=None, pedal=None):
    import build3d
    b3, m, cc, r, made0 = L.built(mn)
    use(build3d)
    rep, made = fit(build3d, m, cc, peg, heel, pedal)
    return build3d, m, cc, rep, made


# ---------------------------------------------------------------- pictures and the check
SHOTS = [
    ("peg", "knurled", "corvano_naked", dict(heel="lightened", pedal="billet")),
    ("peg", "rubber", "hartwell_classic", dict(pedal="billet")),
    ("peg", "cleat", "corvano_adventure", dict(pedal="forged")),
    ("peg", "race", "kazeda_sport", dict(heel="carbon", pedal="billet")),
    ("peg", "board", "lodestar_cruiser", dict(pedal="heeltoe")),
    ("peg", "bullet", "lodestar_chopper", dict(pedal="heeltoe")),
    ("heel", "lightened", "corvano_naked", dict(peg="knurled", pedal="billet")),
    ("heel", "guard", "hartwell_cafe", dict(peg="rubber", pedal="billet")),
    ("heel", "carbon", "kazeda_sport", dict(peg="race", pedal="billet")),
    ("pedal", "billet", "corvano_naked", dict(peg="knurled", heel="lightened")),
    ("pedal", "heeltoe", "lodestar_cruiser", dict(peg="board")),
    ("pedal", "forged", "varga_mx", dict(peg="cleat")),
]


def shots(which=None, samples=40):
    os.makedirs(OUT, exist_ok=True)
    rep = {}
    for kind, key, mn, other in SHOTS:
        if which and key not in which:
            continue
        kw = dict(other)
        kw[kind] = key
        b3, m, cc, r, made = built(mn, kw.get("peg"), kw.get("heel"), kw.get("pedal"))
        rep[f"{kind}_{key}"] = r
        pref = {"peg": ("pg0",), "heel": ("hp",), "pedal": ("fc",)}[kind]
        c_ = L._centre(pref)
        if not c_:
            print("NO PART", kind, key, mn)
            continue
        ctr, lo, hi = c_
        if kind == "pedal" and key != "heeltoe":
            tgt = ((lo[0] + hi[0]) / 2, 120, (lo[2] + hi[2]) / 2)
            loc = BR.vadd(tgt, (380, 520, 240))
        elif kind == "pedal":
            tgt = ((lo[0] + hi[0]) / 2, 140, (lo[2] + hi[2]) / 2)
            loc = BR.vadd(tgt, (420, 600, 300))
        else:
            tgt = (ctr[0], 200, ctr[2])
            loc = BR.vadd(tgt, (300, 560, 260))
        L.render_at(b3, os.path.join(OUT, f"{kind}_{key}.png"), loc, tgt, 50, res=(1100, 800), samples=samples)
    path = os.path.join(OUT, "shots.json")
    old = json.load(open(path)) if os.path.exists(path) else {}
    old.update(rep)
    json.dump(old, open(path, "w"), indent=1)


OK = ("pg | pegpivot", "pg | pillionhangereye", "pg | pegarmboss", "pg | pegarmlink", "pg | hanger", "pg | pg", "hp | hp", "hp | hanger",
      "hp | pegpivot", "fc | fc", "fc | geararm", "fc | gearrod", "fc | rearmasterrod", "fc | pegarmlink", "fc | hanger", "fc | pegpivot", "fc | rearmaster",
      "pg | heelplate", "hp | spar")


def ok_pair(pair):
    a, b_ = [x.strip() for x in pair.split("|")]
    for j in OK:
        ja, jb = [x.strip() for x in j.split("|")]
        if (a.startswith(ja) and b_.startswith(jb)) or (b_.startswith(ja) and a.startswith(jb)):
            return True
    return False


def check(keys=None):
    res = {}
    leads = dict(BR.LEADS)
    leads["Chopper"] = "lodestar_chopper"
    for fam, mn in leads.items():
        for kind, table in (("peg", PEGS), ("heel", HEELS), ("pedal", PEDALS)):
            for key, h in table.items():
                if fam not in h["fams"] or (keys and key not in keys):
                    continue
                b3, m, cc, r, made = built(mn, **{kind: key})
                bad = {k: v for k, v in BR.clip_check(made).items() if not ok_pair(k)}
                res.setdefault(kind, {}).setdefault(key, {})[fam] = dict(model=mn, clips=bad, fitted=kind in r, rep=r)
                print(f"PCHECK {kind:5s} {key:10s} {fam:10s} fitted={kind in r} {bad}", flush=True)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "check.json")
    old = json.load(open(path)) if (keys and os.path.exists(path)) else {}
    for kind, v in res.items():
        for k, per in v.items():
            old.setdefault(kind, {}).setdefault(k, {}).update(per)
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
