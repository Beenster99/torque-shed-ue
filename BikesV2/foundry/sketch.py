"""Side-view sketcher: draws a whole bike from a small description (dict).

This is the seed of the drawing step of the pipeline. Phase 0 uses it at thumbnail
fidelity for the brand style sheets; Phase 1 grows it into the dimensioned side
elevation. Front of the bike is to the left. Data coordinates are mm: x forward
from the rear axle, z up from the ground.
"""
import math

W, H, S = 760, 430, 0.32
OX, OY = W - 150, 402


def X(x):
    return OX - x * S


def Y(z):
    return OY - z * S


def pt(p):
    return f"{X(p[0]):.1f} {Y(p[1]):.1f}"


def smooth_path(pts, t=0.5, closed=True):
    """Catmull-Rom through pts turned into cubic Beziers. t=0 gives straight edges, t=1 full curves."""
    n = len(pts)
    if n < 3 or t <= 0.01:
        return "M" + " L".join(pt(p) for p in pts) + ("Z" if closed else "")
    d = ["M" + pt(pts[0])]
    last = n if closed else n - 1
    for i in range(last):
        p0 = pts[(i - 1) % n] if (closed or i > 0) else pts[0]
        p1, p2 = pts[i % n], pts[(i + 1) % n]
        p3 = pts[(i + 2) % n] if (closed or i + 2 < n) else pts[-1]
        c1 = (p1[0] + (p2[0] - p0[0]) * t / 6, p1[1] + (p2[1] - p0[1]) * t / 6)
        c2 = (p2[0] - (p3[0] - p1[0]) * t / 6, p2[1] - (p3[1] - p1[1]) * t / 6)
        d.append(f"C{pt(c1)} {pt(c2)} {pt(p2)}")
    return " ".join(d) + ("Z" if closed else "")


def poly(pts, cls, t=0.0, closed=True, style=""):
    return f"<path class='{cls}' d='{smooth_path(pts, t, closed)}' {style}/>"


def circ(c, r, cls, style=""):
    return f"<circle class='{cls}' cx='{X(c[0]):.1f}' cy='{Y(c[1]):.1f}' r='{r * S:.1f}' {style}/>"


def seg(a, b, cls, w=None, style=""):
    sw = f"stroke-width='{w * S:.1f}'" if w else ""
    return f"<line class='{cls}' x1='{X(a[0]):.1f}' y1='{Y(a[1]):.1f}' x2='{X(b[0]):.1f}' y2='{Y(b[1]):.1f}' {sw} {style}/>"


def rot_rect(c, ang_deg, length, width, cls, style=""):
    """Rectangle starting at c, running `length` along ang (deg from +x forward, anticlockwise = up)."""
    a = math.radians(ang_deg)
    ux, uz = math.cos(a), math.sin(a)
    nx, nz = -uz, ux
    h = width / 2
    p = [(c[0] + nx * h, c[1] + nz * h), (c[0] + ux * length + nx * h, c[1] + uz * length + nz * h),
         (c[0] + ux * length - nx * h, c[1] + uz * length - nz * h), (c[0] - nx * h, c[1] - nz * h)]
    return poly(p, cls, 0.0, True, style)


def arc_pts(c, r, a0, a1, n=14):
    return [(c[0] + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)), c[1] + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def engine(e, layout):
    """Schematic engine. e = crank centre."""
    o = []
    cyl = {"vtwin90": [(34, 300, 150), (124, 300, 150)], "vtwin45": [(67, 400, 175), (113, 400, 175)],
           "parallel_twin": [(78, 390, 195)], "triple": [(68, 390, 205)], "inline4": [(60, 390, 215)],
           "single": [(84, 420, 165)], "boxer": []}[layout]
    k = 1.22
    cyl = [(a_, l_ * 1.1, w_ * k) for a_, l_, w_ in cyl]
    for ang, ln, wd in cyl:
        o.append(rot_rect(e, ang, ln, wd, "eng"))
        a = math.radians(ang)
        for k in (0.55, 0.7, 0.85):  # fins / head joint lines
            cx, cz = e[0] + math.cos(a) * ln * k, e[1] + math.sin(a) * ln * k
            o.append(seg((cx - math.sin(a) * wd / 2, cz + math.cos(a) * wd / 2), (cx + math.sin(a) * wd / 2, cz - math.cos(a) * wd / 2), "engl"))
    g = (e[0] - 240, e[1] + 20)
    o.append(poly([(e[0] + 175, e[1] + 80), (e[0] + 160, e[1] - 150), (g[0] - 60, g[1] - 165), (g[0] - 140, g[1] - 10), (g[0] - 90, g[1] + 150), (e[0] + 40, e[1] + 175)], "eng", 0.7))
    o.append(circ(e, 115, "engl nofill"))
    o.append(circ(g, 85, "engl nofill"))
    if layout == "boxer":
        h = (e[0] + 40, e[1] + 30)
        o.append(circ(h, 150, "eng"))
        for r in (122, 92, 60):
            o.append(circ(h, r, "engl nofill"))
    return o


def frame(kind, Ht, Hb, P, seatnose, tail_end, e):
    o = []
    if kind in ("trellis", "trellis_bright"):
        top = [Ht, ((Ht[0] + seatnose[0]) / 2 + 20, (Ht[1] + seatnose[1]) / 2 - 40), (seatnose[0] - 30, seatnose[1] - 70)]
        bot = [Hb, ((Hb[0] + P[0]) / 2 + 60, (Hb[1] + P[1]) / 2 + 80), (P[0] + 40, P[1] + 110)]
        o.append(poly(top, "frm", 0.0, False))
        o.append(poly(bot, "frm", 0.0, False))
        zig = [top[0], bot[1], top[1], bot[2], top[2]]
        o.append(poly(zig, "frm thin", 0.0, False))
        o.append(poly([top[2], (P[0] - 30, P[1] + 40), (P[0] - 20, P[1] - 50), (P[0] + 50, P[1] - 40), bot[2]], "plate", 0.3))
    elif kind in ("cradle", "double_cradle"):
        o.append(poly([Ht, (seatnose[0] - 20, seatnose[1] - 55)], "frm", 0.0, False))
        low = e[1] - 175
        o.append(poly([Hb, (e[0] + 215, low + 60), (e[0] + 150, low), (P[0] - 20, low), (P[0] - 60, P[1] + 20), (seatnose[0] - 110, seatnose[1] - 60)], "frm", 0.25, False))
        if kind == "double_cradle":
            o.append(poly([(Ht[0] - 10, Ht[1] - 40), (seatnose[0] + 60, seatnose[1] - 150), (P[0] - 40, P[1] + 60)], "frm thin", 0.3, False))
    elif kind == "spar":
        o.append(poly([Ht, ((Ht[0] + P[0]) / 2 + 60, (Ht[1] + P[1]) / 2 + 150), (P[0] + 30, P[1] + 200), (P[0] - 50, P[1] + 150), (P[0] - 60, P[1] - 50), (P[0] + 40, P[1] - 70),
                       (P[0] + 110, P[1] + 70), ((Hb[0] + P[0]) / 2 + 80, (Hb[1] + P[1]) / 2 + 95), Hb], "spar", 0.3))
    elif kind == "engine_carried":
        o.append(poly([Ht, (e[0] + 60, e[1] + 190)], "frm", 0.0, False))
        o.append(poly([Hb, (e[0] + 150, e[1] + 120)], "frm", 0.0, False))
        o.append(poly([Ht, (seatnose[0] - 20, seatnose[1] - 55)], "frm", 0.0, False))
        o.append(poly([(seatnose[0] - 60, seatnose[1] - 55), (e[0] - 250, e[1] + 110)], "frm", 0.0, False))
    # subframe: two rails from the main frame to the tail
    up = (seatnose[0] - 60, seatnose[1] - 55)
    o.append(poly([up, (tail_end[0] + 60, tail_end[1] - 40)], "frm thin", 0.0, False))
    o.append(poly([(P[0] - 40, P[1] + 60), (tail_end[0] + 160, tail_end[1] - 55)], "frm thin", 0.0, False))
    return o


def lamp(kind, Ht, Hb):
    c = ((Ht[0] + Hb[0]) / 2 + 95, (Ht[1] + Hb[1]) / 2 + 5)
    if kind == "round":
        return [circ(c, 88, "lampb"), circ((c[0] + 12, c[1]), 70, "lamp")]
    if kind == "slit":
        return [poly([(c[0] - 60, c[1] + 75), (c[0] + 70, c[1] + 40), (c[0] + 95, c[1] - 10), (c[0] + 40, c[1] - 55), (c[0] - 70, c[1] - 45)], "lampb", 0.5),
                poly([(c[0] + 30, c[1] + 22), (c[0] + 86, c[1] + 6), (c[0] + 80, c[1] - 12), (c[0] + 30, c[1] - 6)], "lamp", 0.3)]
    if kind == "split":
        return [poly([(c[0] - 70, c[1] + 95), (c[0] + 60, c[1] + 60), (c[0] + 150, c[1] - 60), (c[0] + 40, c[1] - 85), (c[0] - 80, c[1] - 30)], "lampb", 0.15),
                poly([(c[0] + 50, c[1] + 10), (c[0] + 125, c[1] - 45), (c[0] + 70, c[1] - 55)], "lamp", 0.0)]
    if kind == "vertical":
        return [poly([(c[0] - 50, c[1] + 130), (c[0] + 55, c[1] + 110), (c[0] + 95, c[1] - 60), (c[0] + 20, c[1] - 120), (c[0] - 60, c[1] - 70)], "lampb", 0.1),
                poly([(c[0] + 40, c[1] + 80), (c[0] + 78, c[1] - 40), (c[0] + 40, c[1] - 70)], "lamp", 0.0)]
    return []


def bike_svg(d, title=""):
    wb, rake, rf, rr = d["wheelbase"], math.radians(d["rake"]), d["rf"], d["rr"]
    Lf = d.get("fork_len", 740)
    A, F = (0, rr), (wb, rf)
    Ht = (wb - Lf * math.sin(rake), rf + Lf * math.cos(rake))
    Hb = (wb - (Lf - 180) * math.sin(rake), rf + (Lf - 180) * math.cos(rake))
    P, e = d["pivot"], d["engine_at"]
    global S, OX
    span = wb + rf + rr + 160
    S = min(0.32, (W - 30) / span)
    OX = W / 2 + (wb + rf - rr) / 2 * S
    o = [f"<svg class='bike' viewBox='0 0 {W} {H}' role='img' aria-label='{title}' style='--body:{d['body']};--frame:{d['frame_col']}'>"]
    o.append(seg((-rr - 120, 0), (wb + rf + 120, 0), "ground"))
    # rear mudguard arc (classic / cruiser), drawn behind the wheel line
    if d.get("rear_guard"):
        a0, a1, gap = d["rear_guard"]
        o.append(poly(arc_pts(A, rr + gap, a0, a1) + arc_pts(A, rr + gap + 28, a1, a0), "bodyp", 0.0))
    # wheels
    for c, r in ((A, rr), (F, rf)):
        o.append(circ(c, r, "tyre"))
        o.append(circ(c, r - d.get("tyre_h", 90), "rim"))
        o.append(circ(c, 150, "disc"))
        o.append(circ(c, 34, "hub"))
        if d.get("wire"):
            for k in range(18):
                a = math.radians(k * 20)
                o.append(seg(c, (c[0] + (r - d.get("tyre_h", 90)) * math.cos(a), c[1] + (r - d.get("tyre_h", 90)) * math.sin(a)), "spoke"))
    # swingarm and chain or shaft
    o.append(poly([(P[0], P[1] + 38), (A[0] + 10, A[1] + 26), (A[0] - 40, A[1]), (A[0] + 10, A[1] - 26), (P[0], P[1] - 38)], "plate", 0.2))
    # exhaust
    ex = d["exhaust"]
    o.append(poly(ex["pipe"], "pipe", 0.6, False))
    o.append(seg(ex["can"][0], ex["can"][1], "can", ex.get("can_w", 95)))
    # engine and frame
    o += engine(e, d["layout"])
    o += frame(d["frame"], Ht, Hb, P, d["seat"][0], d["tail_end"], e)
    # fork, front mudguard, bars
    o.append(seg(F, Ht, "fork", 46))
    o.append(seg(F, ((F[0] + Ht[0]) / 2, (F[1] + Ht[1]) / 2), "forkl", 58))
    fg = d.get("front_guard", (35, 135, 28))
    o.append(poly(arc_pts(F, rf + fg[2], fg[0], fg[1]) + arc_pts(F, rf + fg[2] + 22, fg[1], fg[0]), "bodyp", 0.0))
    o += lamp(d["lamp"], Ht, Hb)
    o.append(poly([Ht] + d["bars"], "bar", 0.4, False))
    # bodywork: tail, seat, tank (tank last so its rear overlaps the seat nose line)
    if d.get("tail"):
        o.append(poly(d["tail"], "bodyp", d["curve"]))
    o.append(poly(d["seat"], "seat", max(0.35, d["curve"])))
    o.append(poly(d["tank"], "bodyp", d["curve"]))
    for extra in d.get("panels", []):
        o.append(poly(extra, "panel", d["curve"]))
    o.append("</svg>")
    return "\n".join(o)


CSS = """
.bike .ground{stroke:var(--mute);stroke-width:1}
.bike .tyre{fill:none;stroke:var(--ink);stroke-width:7}.bike .rim{fill:none;stroke:var(--line);stroke-width:2.5}
.bike .disc{fill:none;stroke:var(--line);stroke-width:1.2}.bike .hub{fill:var(--eng);stroke:var(--ink);stroke-width:1}.bike .spoke{stroke:var(--line);stroke-width:.6}
.bike .eng{fill:var(--eng);stroke:var(--ink);stroke-width:1.2;stroke-linejoin:round}.bike .engl{stroke:var(--ink);stroke-width:.8;opacity:.55}.bike .nofill{fill:none}
.bike .frm{fill:none;stroke:var(--frame);stroke-width:7;stroke-linecap:round;stroke-linejoin:round}.bike .thin{stroke-width:4.2}
.bike .spar{fill:var(--frame);stroke:var(--ink);stroke-width:1.2;stroke-linejoin:round}.bike .plate{fill:var(--plate);stroke:var(--ink);stroke-width:1.2;stroke-linejoin:round}
.bike .fork{stroke:var(--line);stroke-linecap:round}.bike .forkl{stroke:var(--plate);stroke-linecap:round}
.bike .bar{fill:none;stroke:var(--ink);stroke-width:4;stroke-linecap:round}
.bike .pipe{fill:none;stroke:var(--line);stroke-width:9;stroke-linecap:round}.bike .can{stroke:var(--line);stroke-linecap:round}
.bike .bodyp{fill:var(--body);stroke:var(--ink);stroke-width:1.4;stroke-linejoin:round}.bike .panel{fill:var(--plate);stroke:var(--ink);stroke-width:1.2;stroke-linejoin:round}
.bike .seat{fill:var(--seat);stroke:var(--ink);stroke-width:1.2;stroke-linejoin:round}
.bike .lampb{fill:var(--plate);stroke:var(--ink);stroke-width:1.2;stroke-linejoin:round}.bike .lamp{fill:var(--lamp);stroke:var(--ink);stroke-width:1}
"""
