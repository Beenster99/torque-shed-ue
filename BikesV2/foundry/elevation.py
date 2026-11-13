"""Whole-bike side elevation from a model description file (data/models/<model>.json).

The drawing step of the pipeline. Everything drawn comes from the description:
hard points, frame tubes, engine layout, bodywork outlines. The 3D step reads the
same file, so drawing and model cannot drift apart.

Left side of the bike, front to the left. Data coordinates are mm: x forward from
the rear axle, z up from the ground.

Usage: python3 foundry/elevation.py corvano_naked   -> out/elevations/corvano_naked.svg
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shapes  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 1600, 1000
S = 0.56
OX, OY = 1275.0, 850.0


def X(x):
    return OX - x * S


def Y(z):
    return OY - z * S


def P(p):
    return f"{X(p[0]):.1f} {Y(p[1]):.1f}"


def rad(a):
    return math.radians(a)


def add(p, q, k=1.0):
    return (p[0] + q[0] * k, p[1] + q[1] * k)


def unit(a_deg):
    """Unit vector at a_deg above the forward horizontal."""
    return (math.cos(rad(a_deg)), math.sin(rad(a_deg)))


def _run(q, t):
    n = len(q)
    m = []
    for i in range(n):
        if i == 0:
            m.append((q[1][0] - q[0][0], q[1][1] - q[0][1]))
        elif i == n - 1:
            m.append((q[-1][0] - q[-2][0], q[-1][1] - q[-2][1]))
        else:
            m.append(((q[i + 1][0] - q[i - 1][0]) / 2, (q[i + 1][1] - q[i - 1][1]) / 2))
    d = []
    for i in range(n - 1):
        c1 = (q[i][0] + m[i][0] * t / 3, q[i][1] + m[i][1] * t / 3)
        c2 = (q[i + 1][0] - m[i + 1][0] * t / 3, q[i + 1][1] - m[i + 1][1] * t / 3)
        d.append(f"C{P(c1)} {P(c2)} {P(q[i + 1])}")
    return d


def shape_d(pts, closed=True, t=1.0):
    """Path through pts. A point [x, z, 1] is a sharp corner; other points are smooth."""
    pts = [tuple(p) for p in pts]
    kinks = [i for i, p in enumerate(pts) if len(p) > 2 and p[2]]
    if not closed:
        idx = [0] + [k for k in kinks if 0 < k < len(pts) - 1] + [len(pts) - 1]
        d = ["M" + P(pts[0])]
        for a, b in zip(idx, idx[1:]):
            d += _run(pts[a:b + 1], t) if b - a > 1 else ["L" + P(pts[b])]
        return " ".join(d)
    if not kinks:
        n = len(pts)
        d = ["M" + P(pts[0])]
        for i in range(n):
            p0, p1, p2, p3 = pts[(i - 1) % n], pts[i], pts[(i + 1) % n], pts[(i + 2) % n]
            c1 = (p1[0] + (p2[0] - p0[0]) * t / 6, p1[1] + (p2[1] - p0[1]) * t / 6)
            c2 = (p2[0] - (p3[0] - p1[0]) * t / 6, p2[1] - (p3[1] - p1[1]) * t / 6)
            d.append(f"C{P(c1)} {P(c2)} {P(p2)}")
        return " ".join(d) + "Z"
    k0 = kinks[0]
    pts = pts[k0:] + pts[:k0] + [pts[k0]]
    kinks = [i for i, p in enumerate(pts) if len(p) > 2 and p[2]]
    d = ["M" + P(pts[0])]
    for a, b in zip(kinks, kinks[1:]):
        d += _run(pts[a:b + 1], t) if b - a > 1 else ["L" + P(pts[b])]
    return " ".join(d) + "Z"


def pts_of(shp):
    """A shape's outline: explicit points, or a band following a circle ({"arc": {"c", "r0", "r1", "a0", "a1"}})."""
    if "arc" not in shp:
        return shp["pts"]
    a = shp["arc"]
    c, n = tuple(a["c"]), 16
    ang = [a["a0"] + (a["a1"] - a["a0"]) * i / n for i in range(n + 1)]
    outer = [add(c, unit(x), a["r1"]) for x in ang]
    inner = [add(c, unit(x), a["r0"]) for x in reversed(ang)]
    outer[0] += (1,)
    outer[-1] += (1,)
    inner[0] += (1,)
    inner[-1] += (1,)
    return outer + inner


class Sheet:
    def __init__(self):
        self.o = []

    def path(self, pts, cls, closed=True, t=1.0, extra=""):
        self.o.append(f"<path class='{cls}' d='{shape_d(pts, closed, t)}' {extra}/>")

    def poly(self, pts, cls, closed=True):
        self.o.append(f"<path class='{cls}' d='M{' L'.join(P(p) for p in pts)}{'Z' if closed else ''}'/>")

    def circ(self, c, r, cls, extra=""):
        self.o.append(f"<circle class='{cls}' cx='{X(c[0]):.1f}' cy='{Y(c[1]):.1f}' r='{r * S:.2f}' {extra}/>")

    def ring(self, c, ro, ri, cls):
        """Filled annulus as a thick stroke, with ink edges."""
        self.o.append(f"<circle class='{cls} ringf' cx='{X(c[0]):.1f}' cy='{Y(c[1]):.1f}' r='{(ro + ri) / 2 * S:.2f}' stroke-width='{(ro - ri) * S:.2f}'/>")
        self.circ(c, ro, "edge")
        self.circ(c, ri, "edge")

    def line(self, a, b, cls, w=None):
        sw = f" stroke-width='{w * S:.2f}'" if w else ""
        self.o.append(f"<line class='{cls}' x1='{X(a[0]):.1f}' y1='{Y(a[1]):.1f}' x2='{X(b[0]):.1f}' y2='{Y(b[1]):.1f}'{sw}/>")

    def tube(self, pts, w, cls, t=0.0):
        """A tube along pts: an ink stroke with the material colour on top, so it reads as an outlined tube."""
        d = shape_d(pts, False, t) if t else "M" + " L".join(P(p) for p in pts)
        self.o.append(f"<path class='tubeo' d='{d}' stroke-width='{w * S + 2.4:.2f}'/>")
        self.o.append(f"<path class='tubef {cls}' d='{d}' stroke-width='{w * S:.2f}'/>")

    def slab(self, a, b, w0, w1, cls):
        """Tapered bar from a (width w0) to b (width w1)."""
        dx, dz = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dx, dz) or 1
        n = (-dz / L, dx / L)
        self.poly([add(a, n, w0 / 2), add(b, n, w1 / 2), add(b, n, -w1 / 2), add(a, n, -w0 / 2)], cls)

    def box(self, c, a_deg, length, width, cls, back=0.0):
        u = unit(a_deg)
        n = (-u[1], u[0])
        a, b = add(c, u, -back), add(c, u, length)
        self.poly([add(a, n, width / 2), add(b, n, width / 2), add(b, n, -width / 2), add(a, n, -width / 2)], cls)

    def text(self, p, s, cls="dimt", anchor="middle", dx=0, dy=0):
        self.o.append(f"<text class='{cls}' x='{X(p[0]) + dx:.1f}' y='{Y(p[1]) + dy:.1f}' text-anchor='{anchor}'>{s}</text>")


def wheel(sh, c, w):
    rt, rr = w["tyre_r"], w["rim_r"]
    sh.ring(c, rt, rr + 6, "tyre")
    sh.circ(c, rt - 14, "tread")
    sh.ring(c, rr + 6, rr - 22, "rim")
    n = w.get("spokes", 5)
    if w.get("wire"):
        for k in range(36):
            a = rad(k * 10)
            b = rad(k * 10 + (28 if k % 2 else -28))
            sh.line(add(c, (math.cos(a), math.sin(a)), 46), add(c, (math.cos(b), math.sin(b)), rr - 22), "wire")
    else:
        for k in range(n):
            a0 = 360 / n * k + w.get("phase", 12)
            for side in (-1, 1):
                a_h, a_r = a0 + side * 16, a0 + side * 9 + 20
                h1, h2 = add(c, unit(a_h - 7), 52), add(c, unit(a_h + 7), 52)
                r1, r2 = add(c, unit(a_r - 3.2), rr - 20), add(c, unit(a_r + 3.2), rr - 20)
                sh.poly([h1, r1, r2, h2], "rimf")
    d = w.get("disc")
    if d:
        sh.ring(c, d, d - 36, "steel")
        for k in range(30):
            sh.circ(add(c, unit(k * 12), d - (12 if k % 2 else 24)), 3.2, "hole")
        sh.ring(c, d - 36, d - 70, "carrier")
    sh.circ(c, 52, "hubf")
    sh.circ(c, 20, "axle")


def chain_run(sh, c1, r1, c2, r2):
    dx, dz = c2[0] - c1[0], c2[1] - c1[1]
    d = math.hypot(dx, dz)
    th = math.atan2(dz, dx)
    al = math.acos((r1 - r2) / d)
    for sgn in (1, -1):
        a = th + sgn * al
        v = (math.cos(a), math.sin(a))
        sh.line(add(c1, v, r1), add(c2, v, r2), "chain", 14)
    sh.circ(c1, r1, "sprocket")
    sh.circ(c2, r2, "sprocket")


def cylinder(sh, crank, ang, bore_w, start, length, head, fins=True):
    u = unit(ang)
    base = add(crank, u, start)
    sh.box(base, ang, length, bore_w, "engc")
    if fins:
        n = (-u[1], u[0])
        k = 18
        while k < length - 8:
            q = add(base, u, k)
            sh.line(add(q, n, bore_w / 2 + 5), add(q, n, -bore_w / 2 - 5), "fin")
            k += 15
    top = add(base, u, length)
    sh.box(top, ang, head, bore_w + 26, "engh")
    sh.box(add(top, u, head), ang, 30, bore_w - 10, "engcov")
    return add(top, u, head / 2), u


def draw(model):
    g, wh = model["geometry"], model["wheels"]
    sh = Sheet()
    wb, rake = g["wheelbase"], g["rake"]
    A, F = (0, wh["rear"]["tyre_r"]), (wb, wh["front"]["tyre_r"])
    uf = (-math.sin(rad(rake)), math.cos(rad(rake)))     # up the fork
    nb = (-math.cos(rad(rake)), -math.sin(rad(rake)))    # square to the fork, rearward
    fk = model["fork"]
    top, low = add(F, uf, fk["length"]), add(F, uf, fk["lower_yoke"])
    head_t, head_b = add(add(F, uf, fk["length"] - 22), nb, fk["offset"]), add(add(F, uf, fk["lower_yoke"] + 18), nb, fk["offset"])
    eng, body = model["engine"], model["body"]
    crank, piv = tuple(eng["crank"]), tuple(model["swingarm"]["pivot"])
    col = model["colours"]
    o = sh.o
    o.append(f"<svg class='elev' viewBox='0 0 {W} {H}' role='img' aria-label='{model['name']} side elevation' "
             f"style='--body:{col['body']};--frame:{col['frame']};--rimc:{col.get('rim', '#2b2d31')};--forkc:{col.get('fork', '#b8975a')};--accent2:{col.get('accent', col['body'])}'>")
    o.append(f"<rect class='paper' x='0' y='0' width='{W}' height='{H}'/>")
    sh.line((-520, 0), (wb + 520, 0), "ground")

    # ---- behind: far-side exhaust can, rear hugger, plate hanger
    ex = model["exhaust"]
    def draw_can(can):
        sh.tube([tuple(can["a"]), tuple(can["b"])], can["w"], "exh")
        sh.tube([add(tuple(can["b"]), (-1, 0), 0), add(tuple(can["b"]), ((can["b"][0] - can["a"][0]), (can["b"][1] - can["a"][1])), 0.06)], can["w"] - 16, "dark")

    for can in ex.get("cans", []):
        if not can.get("front"):
            draw_can(can)
    # (the plate hanger is a part of its own, body["plate_hanger"]: road legal in most countries, but optional in a build)
    hanger = body.get("plate_hanger", []) if model.get("fit", {}).get("plate_hanger", True) else []
    for shp in body.get("behind", []) + [h for h in hanger if not h.get("front")]:
        sh.path(pts_of(shp), shp["cls"], True, shp.get("t", 1.0))

    # ---- wheels
    wheel(sh, A, wh["rear"])
    wheel(sh, F, wh["front"])

    # ---- chain and sprockets, swingarm, shock
    sw = model["swingarm"]
    cs = tuple(eng["countershaft"])
    chain_run(sh, cs, 36, A, sw["sprocket_r"])
    sh.path(sw["outline"], sw.get("cls", "cast"), True, 0.9)
    for hole in sw.get("holes", []):
        sh.path(hole, "castin", True, 0.9)
    sh.circ(piv, 26, "bolt")
    sh.circ(A, 38, sw.get("cls", "cast"))
    sh.circ(A, 16, "axle")
    sk = model["shock"]
    sa, sb = tuple(sk["top"]), tuple(sk["bottom"])
    sh.tube([sa, sb], 30, "dark")
    L = math.hypot(sb[0] - sa[0], sb[1] - sa[1])
    us = ((sb[0] - sa[0]) / L, (sb[1] - sa[1]) / L)
    ns = (-us[1], us[0])
    coil = [add(add(sa, us, 40 + i * (L - 110) / 18), ns, 25 if i % 2 else -25) for i in range(19)]
    sh.poly(coil, "spring", False)

    # ---- exhaust headers (behind the engine)
    for hp in ex["headers"]:
        sh.tube([tuple(p) for p in hp["pts"]], hp.get("w", 44), "exh", 1.0)

    # ---- engine
    sh.path(eng["case"], "engc", True, 0.9)
    cyl_pts = []
    for cy in eng["cylinders"]:
        cyl_pts.append(cylinder(sh, crank, cy["angle"], cy["bore_w"], cy["start"], cy["length"], cy["head"], cy.get("fins", True)))
    for cv in eng.get("covers", []):
        sh.circ(tuple(cv["c"]), cv["r"], "engcov")
        sh.circ(tuple(cv["c"]), cv["r"] * 0.72, "detail")
        for k in range(cv.get("bolts", 6)):
            sh.circ(add(tuple(cv["c"]), unit(k * 360 / cv.get("bolts", 6) + 15), cv["r"] * 0.86), 5, "boltd")
    for shp in eng.get("extras", []):
        sh.path(shp["pts"], shp["cls"], shp.get("closed", True), shp.get("t", 0.9))
    sh.circ(cs, 44, "engcov")
    if model.get("radiator"):
        r = model["radiator"]
        sh.box(tuple(r["at"]), r["angle"], r["len"], r["w"], "rad")
        u = unit(r["angle"])
        k = 14
        while k < r["len"]:
            q = add(tuple(r["at"]), u, k)
            n = (-u[1], u[0])
            sh.line(add(q, n, r["w"] / 2), add(q, n, -r["w"] / 2), "fin")
            k += 12

    # ---- frame and subframe tubes
    fr = model["frame"]
    nodes = {k: tuple(v) for k, v in fr["nodes"].items()}
    nodes.update(head_t=head_t, head_b=head_b)
    for tb in fr["tubes"]:
        sh.tube([nodes[n] for n in tb["path"]], tb["w"], tb.get("cls", "frame"), tb.get("t", 0.0))
    for pl in fr.get("plates", []):
        sh.path(pl["pts"], pl.get("cls", "framep"), True, pl.get("t", 0.6))
    sh.tube([head_t, head_b], 62, "frame")

    # ---- fork, yokes, front mudguard, caliper
    stan = add(F, uf, fk["slider_top"])
    if fk.get("usd", True):
        sh.tube([add(F, uf, 40), stan], 43, "chrome")
        sh.tube([stan, top], 58, "forkc")
        sh.box(add(F, uf, -32), 90 + rake, 95, 64, "cast")
    else:
        sh.tube([stan, top], 43, "chrome")
        sh.tube([add(F, uf, -20), stan], 58, "cast")
    sh.circ(F, 22, "axle")
    for at, wd in ((fk["length"] - 22, 46), (fk["lower_yoke"], 54)):
        c = add(F, uf, at)
        sh.box(add(c, nb, -44), math.degrees(math.atan2(nb[1], nb[0])), fk["offset"] + 86, wd, "cast")
    cal = model.get("front_caliper")
    if cal:
        sh.path(cal, "caliper", True, 0.8)
    for shp in body.get("front_guard", []):
        sh.path(pts_of(shp), shp["cls"], True, shp.get("t", 1.0))

    # ---- controls: bars, risers, levers, mirror, lamp, dash
    for shp in model.get("front_end", []):
        if shp.get("tube"):
            sh.tube([tuple(p) for p in shp["pts"]], shp["tube"], shp["cls"], shp.get("t", 0.0))
        elif shp.get("circle"):
            sh.circ(tuple(shp["at"]), shp["circle"], shp["cls"])
        else:
            sh.path(shp["pts"], shp["cls"], shp.get("closed", True), shp.get("t", 1.0))

    # ---- footrests and stand
    for shp in model.get("hangers", []):
        if shp.get("tube"):
            sh.tube([tuple(p) for p in shp["pts"]], shp["tube"], shp["cls"], shp.get("t", 0.0))
        elif shp.get("circle"):
            sh.circ(tuple(shp["at"]), shp["circle"], shp["cls"])
        else:
            sh.path(shp["pts"], shp["cls"], shp.get("closed", True), shp.get("t", 0.8))

    # ---- a silencer on the near (left) side sits in front of the swingarm and wheel
    for can in ex.get("cans", []):
        if can.get("front"):
            draw_can(can)
    for shp in hanger:
        if shp.get("front") and shp.get("tube"):
            sh.tube([tuple(q) for q in shp["pts"]], shp["tube"], shp["cls"], shp.get("t", 0.0))

    # ---- seat pan: a hidden base under the seat, behind the tail, so that a seat from another
    # brand never shows daylight over this bike's tail (docs/mounting_standard.md, joint rule 1)
    pan = seat_pan(body)
    if pan:
        sh.poly(pan, "plastic")
    # ---- bodywork, back to front: tail, seat, tank, panels, trim lines
    for part in ("tail", "seat", "tank", "panels", "trim"):
        for shp in body.get(part, []):
            sh.path(pts_of(shp), shp["cls"], shp.get("closed", True), shp.get("t", 1.0))
    for c in body.get("caps", []):
        sh.circ(tuple(c["at"]), c["r"], c["cls"])

    # ---- dimensions and title block
    if model.get("dimensions", True):
        seat_z, hipx = g["seat_height"], g["hip_x"]
        zd = -70
        for xx in (0, wb):
            sh.line((xx, -20), (xx, zd - 20), "dim")
        sh.line((0, zd), (wb, zd), "dim arrow")
        sh.text((wb / 2, zd), f"wheelbase {wb} mm", dy=-8)
        xs = -400
        sh.line((xs, 0), (xs, seat_z), "dim arrow")
        sh.line((xs - 20, seat_z), (hipx - 40, seat_z), "dim dash")
        sh.text((xs, seat_z / 2), f"seat {seat_z} mm", anchor="start", dx=-8, dy=4)
        axis_b = add(add(F, nb, fk["offset"]), uf, -F[1] / uf[1] * 1.0)
        sh.line(add(add(F, nb, fk["offset"]), uf, -(F[1] / uf[1])), add(head_t, uf, 150), "dim dash")
        sh.text(add(head_t, uf, 165), f"rake {rake:g} deg, trail {g['trail']} mm", anchor="start", dx=8)
        gc = g["ground_clearance"]
        sh.line((crank[0] + 40, 0), (crank[0] + 40, gc), "dim arrow")
        sh.text((crank[0] + 40, gc / 2), f"{gc}", anchor="start", dx=-10, dy=4)
        length = body.get("overall", [-330, wb + 300])
        sh.line((length[0], zd - 70), (length[1], zd - 70), "dim arrow")
        for xx in length:
            sh.line((xx, zd - 50), (xx, zd - 90), "dim")
        sh.text(((length[0] + length[1]) / 2, zd - 70), f"overall length {length[1] - length[0]} mm", dy=-8)
        o.append(f"<text class='title' x='60' y='78'>{model['name']}</text>")
        o.append(f"<text class='subt' x='60' y='108'>{model['family']} family · {model['engine_label']} · {model['frame_label']}</text>")
        o.append(f"<text class='subt' x='60' y='{H - 40}'>Side elevation, left side · scale 1 px = {1 / S:.2f} mm · Bike Models V2 · drawing {model.get('rev', 'A')}</text>")
    o.append("</svg>")
    return "\n".join(o)


CSS = """
.elev{font-family:"IBM Plex Sans",system-ui,sans-serif;--ink:#16181b;--thin:#3b4046}
.elev .paper{fill:#f4f5f3}
.elev .ground{stroke:#16181b;stroke-width:1.6}
.elev path,.elev circle{stroke-linejoin:round;stroke-linecap:round}
.elev .edge{fill:none;stroke:var(--ink);stroke-width:1.2}
.elev .ringf{fill:none}
.elev .tyre{stroke:#1d1e21}.elev .tread{fill:none;stroke:#3a3c40;stroke-width:1;stroke-dasharray:2 5}
.elev .rim{stroke:var(--rimc)}.elev .rimf{fill:var(--rimc);stroke:var(--ink);stroke-width:1}
.elev .wire{stroke:#9aa0a6;stroke-width:.8}
.elev .steel{stroke:#b9bec4}.elev .carrier{stroke:#4a4e54}.elev .hole{fill:#f4f5f3;stroke:var(--thin);stroke-width:.5}
.elev .hubf{fill:var(--rimc);stroke:var(--ink);stroke-width:1.2}.elev .axle{fill:#cfd3d7;stroke:var(--ink);stroke-width:1}
.elev .chain{stroke:#5a5f66;stroke-dasharray:3 2}.elev .sprocket{fill:#aab0b6;stroke:var(--ink);stroke-width:1;stroke-dasharray:4 2}
.elev .cast{fill:#3a3d42;stroke:var(--ink);stroke-width:1.3}.elev .castin{fill:#f4f5f3;stroke:var(--ink);stroke-width:1}
.elev .bolt,.elev .boltd{fill:#cfd3d7;stroke:var(--ink);stroke-width:.9}
.elev .tubeo{fill:none;stroke:var(--ink)}.elev .tubef{fill:none}
.elev .tubef.frame{stroke:var(--frame)}.elev .tubef.dark{stroke:#2e3136}.elev .tubef.exh{stroke:#aeb2b5}.elev .tubef.chrome{stroke:#dfe3e6}
.elev .tubef.forkc{stroke:var(--forkc)}.elev .tubef.cast{stroke:#3a3d42}.elev .tubef.grip{stroke:#1d1e21}.elev .tubef.sub{stroke:var(--frame)}.elev .tubef.steelt{stroke:#9aa0a6}
.elev .framep{fill:var(--frame);stroke:var(--ink);stroke-width:1.3}
.elev .spring{fill:none;stroke:var(--accent2);stroke-width:3;stroke-linejoin:miter}
.elev .alu{fill:#b9bec4;stroke:var(--ink);stroke-width:1.3}.elev .tubef.alu{stroke:#b9bec4}
.elev .engc{fill:#a4aab0;stroke:var(--ink);stroke-width:1.3}.elev .engh{fill:#8d9399;stroke:var(--ink);stroke-width:1.3}.elev .engcov{fill:#6f757b;stroke:var(--ink);stroke-width:1.2}
.elev .fin{stroke:var(--thin);stroke-width:.8}.elev .detail{fill:none;stroke:var(--thin);stroke-width:.8}
.elev .rad{fill:#2e3136;stroke:var(--ink);stroke-width:1.2}
.elev .caliper{fill:var(--forkc);stroke:var(--ink);stroke-width:1.2}
.elev .body{fill:var(--body);stroke:var(--ink);stroke-width:1.6}.elev .bodyline{fill:none;stroke:var(--ink);stroke-width:.9;opacity:.6}
.elev .hl{fill:none;stroke:#fff;stroke-width:3;opacity:.28}
.elev .seat{fill:#222429;stroke:var(--ink);stroke-width:1.4}.elev .stitch{fill:none;stroke:#8a8f95;stroke-width:.8;stroke-dasharray:4 3}
.elev .plastic{fill:#2b2d31;stroke:var(--ink);stroke-width:1.3}.elev .lens{fill:#e9e2b8;stroke:var(--ink);stroke-width:1}.elev .redlens{fill:#b3202a;stroke:var(--ink);stroke-width:1}
.elev .plate{fill:#f1e9a8;stroke:var(--ink);stroke-width:1}
.elev .dim{stroke:#1f4fd1;stroke-width:1;fill:none}.elev .dash{stroke-dasharray:7 5}.elev .dimt{fill:#1f4fd1;font-size:15px;font-family:"IBM Plex Mono",ui-monospace,monospace}
.elev .title{font-family:"Barlow Condensed","Arial Narrow",sans-serif;font-weight:700;font-size:44px;fill:#16181b}.elev .subt{font-size:16px;fill:#4a5057}
"""


PAN = 40  # depth of the hidden seat pan, mm


def _solid(body, part, cls):
    for shp in body.get(part, []):
        if shp.get("closed", True) and shp.get("cls") == cls and "pts" in shp:
            return shapes.dense(shp["pts"], True, shp.get("t", 1.0))
    return None


def seat_tail(body, step=10):
    """[(x, seat underside z, tail top z)] wherever the seat sits over the tail."""
    seat, tail = _solid(body, "seat", "seat"), _solid(body, "tail", "body")
    if not seat or not tail:
        return []
    lo = max(min(p[0] for p in seat), min(p[0] for p in tail)) + 12
    hi = min(max(p[0] for p in seat), max(p[0] for p in tail)) - 12
    out, x = [], lo
    while x <= hi:
        a, b = shapes.top_bottom(seat, x), shapes.top_bottom(tail, x)
        if a and b:
            out.append((x, a[1], b[0]))
        x += step
    return out


def seat_pan(body):
    st = seat_tail(body)
    if len(st) < 2:
        return None
    return [(x, z + 3) for x, z, _ in st] + [(x, z - PAN) for x, z, _ in reversed(st)]


def load(name):
    return json.load(open(os.path.join(ROOT, "data", "models", name + ".json")))


if __name__ == "__main__":
    name = sys.argv[1]
    svg = draw(load(name))
    out = os.path.join(ROOT, "out", "elevations")
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, name + ".svg"), "w").write(svg)
    open(os.path.join(out, name + ".html"), "w").write("<html><head><meta charset='utf-8'><style>body{margin:0}" + CSS + "</style></head><body>" + svg + "</body></html>")
    print("wrote", os.path.join(out, name + ".svg"))
