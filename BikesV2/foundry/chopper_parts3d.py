"""Bespoke chopper parts (beenster, Nov 16: "a LOT of bespoke items to order for the bike in chrome, steel, gold,
brass, all beautifully machined metal parts"). This thread's share: pegs, covers, lamps, engines. Sissy bars,
wheels, high bars and tank decals live in the common parts thread (stage 8b).

    python3 foundry/chopper_parts3d.py pegs        # the footpeg sheet: every design in every metal

Each part is drawn from the joint it bolts to, so it fits any chopper: a peg from its hanger point outwards."""
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import detail3d as D

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "chopper_parts")
B = None

METALS = {
    "chrome": dict(label="Chrome", col=(0.93, 0.93, 0.95), rough=0.035),
    "steel": dict(label="Brushed steel", col=(0.56, 0.57, 0.59), rough=0.3),
    "gold": dict(label="Gold", col=(0.95, 0.66, 0.2), rough=0.09),
    "brass": dict(label="Brass", col=(0.78, 0.6, 0.27), rough=0.2),
}

PEGS = {
    "ringed": dict(label="Ringed", note="Turned from the bar: deep grip rings, a knurled band and a domed end cap."),
    "torpedo": dict(label="Torpedo", note="A long bullet with cooling fins round the root and a pointed spinner tip."),
    "faceted": dict(label="Faceted", note="Eight flat-cut faces with rubber rings let in, and a stepped end with a cap bolt."),
}


def use(b):
    global B
    B = b
    D.B = b


def metal(key):
    m = METALS[key]
    return D.surf("chop_" + key, m["col"], 1.0, m["rough"], wear=0.03)


def peg(name, p, s, style="ringed", fin="chrome"):
    """One footpeg from its hanger point p (x, z) on side s, pointing straight out. The boot sits where it always
    did: 88 mm long, the same height. Returns the names made."""
    mt, dark, rub = metal(fin), D.surf("chop_blacked", (0.02, 0.02, 0.022), 1.0, 0.35, wear=0.0), B.mat("rubber")
    o, ax = (p[0], s * 172, p[1]), (0, s, 0)
    made = []

    def turn(tag, prof, material, segs=48, **kw):
        D.lathe(f"{name}{tag}", o, ax, prof, material, segs, **kw)
        made.append(f"{name}{tag}")
    # every design starts with the same root: a clevis boss and its pivot pin, so they all hang on the same bracket
    turn("_root", [(0, 0), (0, 13), (1.5, 15), (6.5, 15), (8, 13), (8, 0)], mt)
    B.cone(f"{name}_pin", (p[0] - 12, s * 170, p[1] - 12), (p[0] - 12, s * 184, p[1] - 12), 5.5, 5.5, mt, 6)
    made.append(f"{name}_pin")
    if style == "ringed":
        prof, t = [(8, 0), (8, 10.5)], 10.0
        while t < 46:
            prof += [(t, 10.5), (t + 0.8, 13.4), (t + 4.0, 13.4), (t + 4.8, 10.5)]
            t += 7.2
        prof += [(46, 10.5), (46, 0)]
        turn("_rings", prof, mt)
        # the knurled band: a ring of fine diamond teeth, cut as shallow flats on two opposed helices
        n_ = 30
        verts, faces = [], []
        for j in range(9):
            for k in range(n_ * 2):
                w = math.pi * k / n_ + j * math.pi / n_ / 2
                r = 13.6 if (k + j) % 2 else 12.6
                verts.append(B.V(p[0] + r * math.cos(w), s * (172 + 47 + j * 2.4), p[1] + r * math.sin(w)))
        for j in range(8):
            for k in range(n_ * 2):
                k2 = (k + 1) % (n_ * 2)
                faces.append((j * n_ * 2 + k, j * n_ * 2 + k2, (j + 1) * n_ * 2 + k2, (j + 1) * n_ * 2 + k))
        B.mesh_obj(f"{name}_knurl", verts, faces, mt, False, 0, False)
        made.append(f"{name}_knurl")
        turn("_core", [(46, 0), (46, 12.2), (67, 12.2), (67, 0)], mt, 32)
        turn("_end", [(66.5, 0), (66.5, 10.5), (68, 14.2), (76, 14.2), (79, 13.2), (83, 10.4), (86.5, 5.6), (88, 0)], mt, 48, sharp=False)
        turn("_endring", [(70.5, 14.0), (70.5, 14.7), (73.5, 14.7), (73.5, 14.0)], dark, 48, caps=False)
    elif style == "torpedo":
        prof, t = [(8, 0), (8, 9)], 9.0
        for i in range(5):
            r = 16.5 - i * 0.9
            prof += [(t, 9), (t + 0.3, r), (t + 1.7, r), (t + 2.0, 9)]
            t += 4.2
        prof += [(30, 9), (30, 0)]
        turn("_fins", prof, mt)
        turn("_body", [(30, 0), (30, 11.5), (33, 13.4), (40, 14.2), (52, 14.0), (62, 12.6), (70, 10.2), (74, 8.6), (74, 0)], mt, 56, sharp=False)
        for i, t0 in enumerate((38.0, 46.0, 54.0)):
            turn(f"_groove{i}", [(t0, 14.25 - i * 0.25), (t0, 14.5 - i * 0.25), (t0 + 1.6, 14.45 - i * 0.3), (t0 + 1.6, 14.2 - i * 0.3)], rub, 48, caps=False)
        turn("_collar", [(73.5, 0), (73.5, 9.6), (75, 10.4), (77, 10.4), (78, 9.2), (78, 0)], dark, 40)
        turn("_tip", [(78, 0), (78, 8.4), (81, 7.0), (84.5, 4.6), (87, 2.0), (88, 0)], mt, 40, sharp=False)
    else:
        turn("_neck", [(8, 0), (8, 9.5), (12, 9.5), (12, 0)], mt, 32)
        turn("_body", [(12, 0), (12, 12.5), (14, 15.2), (66, 15.2), (68, 12.5), (68, 0)], mt, 8)
        for i, t0 in enumerate((20.0, 31.0, 42.0, 53.0)):
            turn(f"_ring{i}", [(t0, 13.6), (t0, 15.0), (t0 + 5, 15.0), (t0 + 5, 13.6)], rub, 40, caps=False)
        turn("_step", [(68, 0), (68, 11), (72, 11), (72, 13.5), (80, 13.5), (82, 11.5), (82, 0)], mt, 48)
        turn("_capbolt", [(82, 0), (82, 5.2), (86, 5.2), (88, 4.2), (88, 0)], dark, 6)
    return made


def fit_pegs(model, style, fin):
    """Swap the rider's pegs on a built bike for a bespoke pair. The hanger point is the model's own: never moved."""
    pegs = [tuple(s_["at"]) for s_ in model.get("hangers", []) if s_.get("circle")]
    if not pegs:
        return []
    D.drop("peg0", "pegpivot0")
    return [n for s in (-1, 1) for n in peg(f"peg0{s}", pegs[0], s, style, fin)]


COVERS = {
    "finned": dict(label="Finned", note="Straight cooling fins milled across the face, the tops polished, a plain rim."),
    "spoked": dict(label="Starburst", note="Tapered ribs fanning out from a domed centre boss to a stepped rim."),
    "target": dict(label="Target", note="Concentric turned steps down to a centre jewel, a ring of dome-head bolts round the edge."),
}


def cover(name, cen, s, r, style="finned", fin="chrome"):
    """A round machined cover of radius r, its back face at cen (x, y, z) on side s (1 = rider's left), standing
    out along y. For cam caps, clutch and primary inspection covers: any disc on the side of the engine."""
    mt, dark = metal(fin), D.surf("chop_blacked", (0.02, 0.02, 0.022), 1.0, 0.35, wear=0.0)
    o, ax = (cen[0], cen[1], cen[2]), (0, s, 0)
    k = r / 50.0
    made = []

    def turn(tag, prof, material, segs=64, **kw):
        D.lathe(f"{name}{tag}", o, ax, prof, material, segs, **kw)
        made.append(f"{name}{tag}")

    def bar(tag, u0, v0, u1, v1, w0, w1, h0, h1, material):
        """a raised rib on the face from (u0, v0) to (u1, v1) in the disc's own plane, widths w and heights h at each end"""
        du, dv = u1 - u0, v1 - v0
        L = math.hypot(du, dv) or 1.0
        nu, nv = -dv / L, du / L
        vs = []
        for (u, v, w, h) in ((u0, v0, w0, h0), (u1, v1, w1, h1)):
            for sg, hh in ((-1, 0.0), (-0.6, h), (0.6, h), (1, 0.0)):
                vs.append(B.V(cen[0] + u + nu * w / 2 * sg, cen[1] + s * (base + hh), cen[2] + v + nv * w / 2 * sg))
        fs = [(0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (0, 3, 2, 1), (4, 5, 6, 7), (0, 4, 7, 3)]
        B.mesh_obj(f"{name}{tag}", vs, fs, material, False, 0, False)
        made.append(f"{name}{tag}")
    if style == "finned":
        base = 5.0 * k + 2
        turn("_plate", [(0, 0), (0, r), (base * 0.5, r), (base, r - 2 * k), (base, 0)], mt)
        turn("_rim", [(base, r - 2.2 * k), (base + 3 * k, r - 3 * k), (base + 3 * k, r - 7 * k), (base, r - 8 * k)], mt, 64, caps=False)
        n_ = max(5, int(r / 4.2))
        for i in range(n_):
            u = -r * 0.78 + 2 * r * 0.78 * i / (n_ - 1)
            half = math.sqrt(max(1.0, (r - 9 * k) ** 2 - u * u))
            bar(f"_fin{i}", u, -half, u, half, 2.6 * k + 0.8, 2.6 * k + 0.8, 4.5 * k + 1.5, 4.5 * k + 1.5, mt)
    elif style == "spoked":
        base = 4.0 * k + 2
        turn("_plate", [(0, 0), (0, r), (base * 0.6, r), (base, r - 1.5 * k), (base, 0)], mt)
        turn("_rim", [(base, r - 1.5 * k), (base + 4 * k, r - 3 * k), (base + 4 * k, r - 6 * k), (base + 1.5 * k, r - 8 * k), (base, r - 8 * k)], mt, 64, caps=False)
        n_ = 12 if r > 34 else 8
        for i in range(n_):
            w = 2 * math.pi * i / n_
            bar(f"_rib{i}", r * 0.24 * math.cos(w), r * 0.24 * math.sin(w), (r - 8.5 * k) * math.cos(w), (r - 8.5 * k) * math.sin(w), 3.2 * k + 0.6, 7.5 * k + 1.0, 5 * k + 1.5, 2.2 * k + 0.8, mt)
        turn("_boss", [(base, 0), (base, r * 0.3), (base + 4 * k + 1, r * 0.28), (base + 7 * k + 1.5, r * 0.2), (base + 8.5 * k + 2, r * 0.09), (base + 9 * k + 2, 0)], mt, 48, sharp=False)
    else:
        base = 3.0 * k + 2
        prof, t, rr = [(0, 0), (0, r), (base, r)], base, r
        for i in range(4):
            rr -= r * 0.17
            prof += [(t, rr + 1.2 * k), (t + 2.2 * k + 0.6, rr)]
            t += 2.2 * k + 0.6
        prof += [(t, 0)]
        turn("_steps", prof, mt)
        turn("_jewel", [(t, 0), (t, rr * 0.82), (t + 1.2 * k + 0.5, rr * 0.8), (t + 3 * k + 1, rr * 0.5), (t + 3.6 * k + 1.2, 0)], dark, 40, sharp=False)
        n_ = 8 if r > 34 else 5
        for i in range(n_):
            w = 2 * math.pi * (i + 0.5) / n_
            q = (cen[0] + (r - r * 0.085) * math.cos(w), cen[1] + s * base, cen[2] + (r - r * 0.085) * math.sin(w))
            D.lathe(f"{name}_bolt{i}", q, ax, [(0, 0), (0, 2.6 * k + 0.9), (1.6 * k + 0.5, 2.4 * k + 0.8), (2.6 * k + 0.9, 1.2 * k + 0.4), (2.9 * k + 1.0, 0)], mt, 16, sharp=False)
            made.append(f"{name}_bolt{i}")
    return made


def fit_covers(model, style, fin):
    """Swap every round cover on a built engine (cam caps, clutch caps) for the bespoke design, each the same
    size and in the same place as the one it replaces."""
    from mathutils import Vector
    made = []
    for o in list(bpy.data.objects):
        if o.type != "MESH" or not (o.name.startswith("camcap") or o.name.startswith("clutchcap")) or "bolt" in o.name:
            continue
        bb = [o.matrix_world @ Vector(q) for q in o.bound_box]
        lo = [min(q[i] for q in bb) / B.MM for i in range(3)]
        hi = [max(q[i] for q in bb) / B.MM for i in range(3)]
        s = 1 if lo[1] + hi[1] > 0 else -1
        r = (hi[0] - lo[0]) / 2
        cen = ((lo[0] + hi[0]) / 2, lo[1] if s == 1 else hi[1], (lo[2] + hi[2]) / 2)
        nm = o.name
        D.drop(nm)
        made += cover(nm, cen, s, r, style, fin)
    for o in list(bpy.data.objects):
        if o.name.startswith("camcapbolt"):
            bpy.data.objects.remove(o, do_unlink=True)
    return made


def covers_sheet(samples=48):
    """Every cover design (rows) in every metal (columns), face up on the bench."""
    import build3d
    bpy.ops.wm.read_factory_settings(use_empty=True)
    build3d._mats.clear()
    use(build3d)
    for r_, st in enumerate(COVERS):
        for c_, fin in enumerate(METALS):
            made = cover(f"c_{st}_{fin}", (0, 0, 0), 1, 50, st, fin)
            e = bpy.data.objects.new("bay", None)
            bpy.context.collection.objects.link(e)
            for n in made:
                o = bpy.data.objects.get(n)
                if o is not None:
                    o.parent = e
            e.rotation_euler = (math.radians(90), 0, 0)          # the cover's own +y becomes up
            e.location = ((c_ - 1.5) * 0.125, (1 - r_) * 0.12, 0)
    sc = _studio(1800, 1300, samples, (0.0, -0.5, 0.52), (0.0, -0.005, 0.0), 50, 0.0)
    os.makedirs(OUT, exist_ok=True)
    sc.render.filepath = os.path.join(OUT, "covers.png")
    bpy.ops.render.render(write_still=True)
    return sc.render.filepath


LAMPS = {
    "bullet": dict(label="Bullet", note="A long shell drawn back to a pointed finial, three speed lines turned round it, a stepped rim."),
    "finned": dict(label="Finned", note="A short drum with cooling fins round it, a peak over the glass and a domed back with a centre nut."),
    "grille": dict(label="Grille", note="A plain polished bowl with a three-bar guard and a centre boss across the glass."),
}


def lamp_shell(name, front, r, depth, style="bullet", fin="chrome", ell=(1.0, 1.0), back=(-1, 0, 0)):
    """The metal of a lamp: shell and rim round a glass of radius r whose face is at front (x, y, z), the shell
    running back along `back` for about depth. The glass, reflector and bulb are the bike's own."""
    mt, dark = metal(fin), D.surf("chop_blacked", (0.02, 0.02, 0.022), 1.0, 0.35, wear=0.0)
    k = r / 50.0
    made = []

    def turn(tag, prof, material, segs=64, **kw):
        D.lathe(f"{name}{tag}", front, back, prof, material, segs, ell=ell, **kw)
        made.append(f"{name}{tag}")
    # a rim every design shares: stepped, lapping the edge of the glass
    turn("_rim", [(-3 * k, r - 1.5 * k), (-6 * k, r + 2 * k), (-6 * k, r + 8 * k), (-2 * k, r + 11 * k), (6 * k, r + 11 * k), (9 * k, r + 8.5 * k), (9 * k, r + 4 * k)], mt, 72, caps=False)
    turn("_rimgroove", [(1 * k, r + 11.1 * k), (1 * k, r + 11.6 * k), (3 * k, r + 11.6 * k), (3 * k, r + 11.1 * k)], dark, 72, caps=False)
    if style == "bullet":
        L = depth * 1.35
        prof = [(8 * k, r + 5 * k)]
        for i in range(1, 15):
            u = i / 14
            prof.append((8 * k + (L - 8 * k) * u, (r + 5 * k) * (1 - u ** 2.2) ** 0.62 + 2.2 * k * u))
        prof += [(L + 5 * k, 3.4 * k), (L + 9 * k, 4.6 * k), (L + 13 * k, 3.6 * k), (L + 22 * k, 0)]
        turn("_shell", prof, mt, 72, sharp=False)
        for i, u in enumerate((0.3, 0.38, 0.46)):
            t = 8 * k + (L - 8 * k) * u
            rr = (r + 5 * k) * (1 - u ** 2.2) ** 0.62 + 2.2 * k * u
            turn(f"_line{i}", [(t, rr + 0.2), (t, rr + 1.4 * k), (t + 2.2 * k, rr + 1.3 * k), (t + 2.2 * k, rr + 0.1)], mt, 72, caps=False)
    elif style == "finned":
        L = depth * 0.95
        prof, t = [(8 * k, r + 3 * k)], 12 * k
        while t < L - 14 * k:
            prof += [(t, r + 3 * k), (t + 0.6 * k, r + 10 * k), (t + 2.6 * k, r + 10 * k), (t + 3.2 * k, r + 3 * k)]
            t += 7 * k
        prof += [(L - 8 * k, r + 3 * k), (L, r - 4 * k), (L + 9 * k, r * 0.55), (L + 13 * k, r * 0.2), (L + 13 * k, 0)]
        turn("_shell", prof, mt, 72)
        turn("_nut", [(L + 13 * k, 0), (L + 13 * k, 7 * k), (L + 18 * k, 7 * k), (L + 19.5 * k, 5 * k), (L + 19.5 * k, 0)], mt, 6)
        # the peak: a hooded visor over the top half of the glass
        from mathutils import Vector
        bx = Vector(back).normalized()
        verts, faces, n_ = [], [], 24
        for j, (out, rr) in enumerate(((-4 * k, r + 11.5 * k), (22 * k, r + 9 * k), (22 * k, r + 7 * k), (-4 * k, r + 9.5 * k))):
            for i in range(n_ + 1):
                w = math.radians(12 + 156 * i / n_)
                reach = out * (math.sin(w) ** 0.6 if j in (1, 2) else 1.0) if j in (1, 2) else out
                q = Vector(front) - bx * reach + Vector((0, rr * ell[0] * math.cos(w), rr * ell[1] * math.sin(w)))
                verts.append(B.V(q.x, q.y, q.z))
        m_ = n_ + 1
        for j in range(4):
            for i in range(n_):
                j2 = (j + 1) % 4
                faces.append((j * m_ + i, j * m_ + i + 1, j2 * m_ + i + 1, j2 * m_ + i))
        B.mesh_obj(f"{name}_peak", verts, faces, mt, True, 0, False)
        made.append(f"{name}_peak")
    else:
        L = depth
        prof = [(8 * k, r + 6 * k)]
        for i in range(1, 13):
            u = i / 12
            prof.append((8 * k + (L - 8 * k) * u, (r + 6 * k) * math.sqrt(max(0.0, 1 - u * u * 0.94))))
        prof += [(L + 2 * k, 0)]
        turn("_shell", prof, mt, 72, sharp=False)
        from mathutils import Vector
        bx = Vector(back).normalized()
        for i, zf in enumerate((-0.5, 0.0, 0.5)):
            half = math.sqrt(1 - zf * zf) * (r + 9 * k)
            pts = []
            for q in range(9):
                u = -1 + q / 4
                bow = (10 + 9 * (1 - u * u)) * k
                v = Vector(front) - bx * bow + Vector((0, half * u * ell[0], zf * (r + 9 * k) * ell[1]))
                pts.append((v.x / 1, v.y / 1, v.z / 1))
            B.smooth_tube(f"{name}_bar{i}", pts, 2.6 * k + 0.4, mt)
            made.append(f"{name}_bar{i}")
        c0 = Vector(front) - bx * (19 * k)
        D.lathe(f"{name}_boss", (c0.x, c0.y, c0.z), tuple(-bx), [(-3 * k, 0), (-3 * k, 9 * k), (0, 10 * k), (3 * k, 8 * k), (5 * k, 4 * k), (5.6 * k, 0)], mt, 40, sharp=False)
        made.append(f"{name}_boss")
    return made


def fit_lamps(model, style, fin):
    """Swap the metal of the headlamp and the mudguard tail lamp for the bespoke design. Glass, reflector and
    bulb stay; each lamp keeps its size and place."""
    from mathutils import Vector

    def box(nm):
        o = bpy.data.objects.get(nm)
        if o is None:
            return None
        bb = [o.matrix_world @ Vector(q) for q in o.bound_box]
        return [min(q[i] for q in bb) / B.MM for i in range(3)], [max(q[i] for q in bb) / B.MM for i in range(3)]
    made = []
    g, bw = box("lampglass0"), box("lampbowl00")
    if g and bw:
        (lo, hi), (blo, _) = g, bw
        r = (hi[2] - lo[2]) / 2
        front = (hi[0] - 6, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2)
        for o in list(bpy.data.objects):
            if o.name.startswith("lampbowl") or o.name == "lamprim0":
                bpy.data.objects.remove(o, do_unlink=True)
        made += lamp_shell("lampbespoke", front, r, front[0] - blo[0], style, fin)
    g, bd = box("guardlamplens"), box("guardlampbody")
    if g and bd:
        (lo, hi), (_, bhi) = g, bd
        ry, rz = (hi[1] - lo[1]) / 2, (hi[2] - lo[2]) / 2
        front = (lo[0] + 5, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2)
        D.drop("guardlampbody", "guardlamprim")
        made += lamp_shell("guardlampbespoke", front, ry, (bhi[0] - front[0]) * 0.8, style, fin, ell=(1.0, rz / ry), back=(1, 0, 0))
    return made


def lamps_sheet(samples=48):
    """Every lamp design (rows) in every metal (columns), with a glass and reflector in so it reads as a lamp."""
    import build3d
    bpy.ops.wm.read_factory_settings(use_empty=True)
    build3d._mats.clear()
    use(build3d)
    for r_, st in enumerate(LAMPS):
        for c_, fin in enumerate(METALS):
            front = (-r_ * 0.3 / B.MM, (c_ - 1.5) * 0.21 / B.MM, 70.0)
            back = (-1, 0, 0)
            lamp_shell(f"l_{st}_{fin}", front, 50, 70, st, fin, back=back)
            D.lathe(f"l_{st}_{fin}_glass", front, back, [(-4, 0), (-3, 30), (0, 49), (2, 49), (2, 0)], D.glass_mat(), 48, sharp=False)
            D.lathe(f"l_{st}_{fin}_refl", front, back, [(2, 49), (12, 44), (26, 32), (38, 14), (40, 0)], D.surf("lampsilver", (0.9, 0.9, 0.92), 1.0, 0.06, wear=0.0), 48, sharp=False)
    sc = _studio(1800, 1300, samples, (1.0, -0.95, 0.8), (-0.3, 0.0, 0.03), 60, 0.0)
    os.makedirs(OUT, exist_ok=True)
    sc.render.filepath = os.path.join(OUT, "lamps.png")
    bpy.ops.render.render(write_still=True)
    return sc.render.filepath


ENGINES = {
    "pots": dict(label="Twin pot", note="Two domed rocker pots on each head with acorn nuts, and polished pushrod tubes up the right side of each cylinder."),
    "finned": dict(label="Finned top", note="A deep-finned rocker cover on each head, squared with rounded corners, with a turned breather cap."),
}


def fit_engine(model, style, fin):
    """Dress the top of a V-twin: the plain rocker boxes give way to a bespoke top end. Works from the model's own
    crank point and cylinder angles, so it follows any chopper's engine."""
    eng = model.get("engine", {})
    cyls = eng.get("cylinders", [])
    if not cyls or "crank" not in eng:
        return []
    mt, dark = metal(fin), D.surf("chop_blacked", (0.02, 0.02, 0.022), 1.0, 0.35, wear=0.0)
    made = []
    for o in list(bpy.data.objects):
        if o.name.startswith(("cover0", "cover1", "covergasket", "coverbolt")):
            bpy.data.objects.remove(o, do_unlink=True)
    for i, cy in enumerate(cyls):
        a = math.radians(cy["angle"])
        u = (math.cos(a), math.sin(a))
        v = (u[1], -u[0])
        top = cy["start"] + cy["length"] + cy["head"] - 8
        bx, bz = eng["crank"][0] + u[0] * top, eng["crank"][1] + u[1] * top
        ax = (u[0], 0, u[1])
        hw = cy["bore_w"] / 2

        def turn(tag, o, prof, material, segs=56, **kw):
            D.lathe(f"cover{i}{tag}", o, ax, prof, material, segs, **kw)
            made.append(f"cover{i}{tag}")
        if style == "pots":
            turn("_deck", (bx, 0, bz), [(0, 0), (0, hw - 8), (5, hw - 8), (8, hw - 12), (8, 0)], mt, 4, square=5.0)
            for j, sg in enumerate((-1, 1)):
                o = (bx + v[0] * sg * 36, 0, bz + v[1] * sg * 36)
                turn(f"_pot{j}", o, [(8, 0), (8, 33), (11, 35), (15, 35), (18, 32), (40, 32), (50, 27), (56, 18), (59, 7), (59, 0)], mt, 56, sharp=False, ell=(1.0, 1.75))
                turn(f"_band{j}", o, [(24, 32.2), (24, 33.6), (30, 33.6), (30, 32.2)], mt, 56, caps=False, ell=(1.0, 1.75))
                turn(f"_nut{j}", o, [(59, 0), (59, 7.5), (64, 7.5), (67, 5.5), (69, 0)], mt, 6)
            # pushrod tubes: two per cylinder up the right side, from the cam chest to the head, each with collars
            for j, sg in enumerate((-1, 1)):
                lo = (eng["crank"][0] + u[0] * (cy["start"] - 6) + v[0] * sg * 15, -(hw + 9), eng["crank"][1] + u[1] * (cy["start"] - 6) + v[1] * sg * 15)
                hi = (eng["crank"][0] + u[0] * (top - 28) + v[0] * sg * 30, -(hw + 3), eng["crank"][1] + u[1] * (top - 28) + v[1] * sg * 30)
                B.tube(f"pushrod{i}{j}", [lo, hi], 7.5, mt)
                made.append(f"pushrod{i}{j}")
                for kq, f in enumerate((0.04, 0.5, 0.96)):
                    q = tuple(lo[n] + (hi[n] - lo[n]) * f for n in range(3))
                    d = tuple(hi[n] - lo[n] for n in range(3))
                    L = math.sqrt(sum(x * x for x in d))
                    d = tuple(x / L for x in d)
                    D.lathe(f"pushrod{i}{j}collar{kq}", q, d, [(-5, 7.5), (-5, 10.5), (-3, 11.5), (3, 11.5), (5, 10.5), (5, 7.5)], mt if kq != 1 else dark, 24, caps=False)
                    made.append(f"pushrod{i}{j}collar{kq}")
        else:
            prof, t = [(0, 0), (0, hw - 6), (3, hw - 6)], 3.0
            while t < 40:
                prof += [(t, hw - 15), (t + 1.0, hw - 6), (t + 4.5, hw - 6), (t + 5.5, hw - 15)]
                t += 8.5
            prof += [(t, hw - 15), (t + 8, hw - 22), (t + 13, hw - 36), (t + 14, 0)]
            turn("_finned", (bx, 0, bz), prof, mt, 64, square=4.5)
            o = (bx, 0, bz)
            turn("_breather", o, [(t + 13, 0), (t + 13, 17), (t + 17, 19), (t + 23, 19), (t + 26, 15), (t + 27, 0)], mt, 40)
            turn("_breatherring", o, [(t + 19, 19.1), (t + 19, 19.8), (t + 21, 19.8), (t + 21, 19.1)], dark, 40, caps=False)
    return made


SISSY = {
    "chain": dict(label="Chain", note="Welded chain: every link fused to the next up both sides and over the top, with two chain cross bars."),
    "scroll": dict(label="Scrollwork", note="A round-topped hoop filled with wrought scrolls, a star medallion in a ring and a ball finial."),
    "filigree": dict(label="Filigree", note="A pointed arch with a diamond lattice, a rosette on every crossing, an oval boss and a flame finial."),
}


def sissy_bar(style, fin, sleft, r_tube=6.0, tag="sissy"):
    """A show sissy bar from the left upright's points (x, y, z), axle plate first: same feet, same lean, the
    back rest straps stay where they were. Everything above the last point is the design. Returns the names made."""
    m = metal(fin)
    made = []
    (x1, y1, z1), (xt, yt, zt) = sleft[-2], sleft[-1]
    ul = math.hypot(xt - x1, zt - z1)
    ux, uz = (xt - x1) / ul, (zt - z1) / ul
    nx, nz = uz, -ux                                      # the bar's face, pointing forwards and up
    P = lambda d, y, o=0.0: (xt + ux * d + nx * o, y, zt + uz * d + nz * o)
    wid = lambda d: y1 + (yt - y1) * (d + ul) / ul          # the upright's own half width at d
    add = lambda o: (made.append(o.name), o)[1]
    low = -ul + 30                                        # just above the back rest pad

    def spiral(name, cd, cy, R, turn, a0, n_turn=1.35, r=3.4, sgn=1):
        pts = []
        for j in range(29):
            t = j / 28
            w, rad = a0 + turn * t * 2 * math.pi * n_turn, R * (1 - 0.74 * t)
            pts.append(P(cd + rad * math.sin(w), sgn * (cy + rad * math.cos(w))))
        add(B.smooth_tube(name, pts, r, m))

    if style == "chain":
        # the path: up the left side, over a flat-shouldered top, down the right
        half = list(sleft) + [P(50, yt), P(92, yt * 0.9), P(118, yt * 0.55), P(128, 0.0)]
        path = half + [(q[0], -q[1], q[2]) for q in half[-2::-1]]
        add(B.tube(f"{tag}foot1", [sleft[0], tuple(a + (b - a) * 0.12 for a, b in zip(sleft[0], sleft[1]))], r_tube + 1.5, m))
        add(B.tube(f"{tag}foot-1", [(q[0], -q[1], q[2]) for q in (sleft[0], tuple(a + (b - a) * 0.12 for a, b in zip(sleft[0], sleft[1])))], r_tube + 1.5, m))

        def chain(name, pts, r=5.0, hl=16.0, re=11.0, first=0):
            from mathutils import Vector
            pv = [Vector(q) for q in pts]
            seg = [(b - a).length for a, b in zip(pv, pv[1:])]
            tot, pitch = sum(seg), 2 * (hl + re) - 2 * r
            n = max(1, round(tot / pitch))
            nrm = Vector((nx, 0.0, nz))
            for k in range(n):
                d = tot * (k + 0.5) / n
                i = 0
                while i < len(seg) - 1 and d > seg[i]:
                    d -= seg[i]
                    i += 1
                t = (pv[i + 1] - pv[i]).normalized()
                c = pv[i] + t * d
                side = t.cross(nrm).normalized()
                w = side if (k + first) % 2 == 0 else t.cross(side).normalized()
                loop = []
                for j in range(32):
                    a = 2 * math.pi * j / 32
                    ca, sa = math.cos(a), math.sin(a)
                    q = c + t * (re * ca + (hl if ca > 0 else -hl)) + w * (re * sa)
                    loop.append(tuple(q))
                add(B.tube(f"{name}{k}", loop, r, m, cyclic=True))
        chain(f"{tag}link", path[1:] if False else [tuple(a + (b - a) * 0.1 for a, b in zip(path[0], path[1]))] + path[1:-1] + [tuple(a + (b - a) * 0.1 for a, b in zip(path[-1], path[-2]))])
        for k, d in enumerate((low + 14, -18.0)):
            w_ = wid(d)
            chain(f"{tag}cross{k}_", [P(d, -w_ + 4), P(d, w_ - 4)], first=1)
    elif style == "scroll":
        half = list(sleft) + [P(60, yt), P(118, yt * 0.94), P(166, yt * 0.62), P(188, 0.0)]
        add(B.smooth_tube(f"{tag}bar", half + [(q[0], -q[1], q[2]) for q in half[-2::-1]], r_tube, m))
        for sgn in (1, -1):
            for k, (cd, turn, a0) in enumerate(((low + 42, 1, 0.0), (low + 118, -1, 0.0))):
                w_ = wid(cd) - r_tube - 3.0
                R = (w_ - 3.4) / 2
                spiral(f"{tag}scroll{k}{sgn}", cd, w_ - R, R, turn, 0.0, sgn=sgn)
            spiral(f"{tag}leaf{sgn}", 20.0, yt * 0.5, yt * 0.5 - r_tube - 3.4, 1, 0.0, 1.1, 3.0, sgn)
        for k, d in enumerate((low + 42, low + 118)):
            add(B.cone(f"{tag}knot{k}", P(d - 9, 0.0), P(d + 9, 0.0), 6.5, 6.5, m, 16))
        dm = low + 80
        add(B.cone(f"{tag}lozengea", P(dm - 20, 0.0), P(dm, 0.0), 0.6, 9, m, 4))
        add(B.cone(f"{tag}lozengeb", P(dm, 0.0), P(dm + 20, 0.0), 9, 0.6, m, 4))
        # the medallion: a ring in the round of the hoop with an eight point star and a stone
        cm, rm = 112.0, 40.0
        add(B.tube(f"{tag}ring", [P(cm + rm * math.sin(2 * math.pi * j / 40), rm * math.cos(2 * math.pi * j / 40)) for j in range(40)], 4.2, m, cyclic=True))
        for j in range(8):
            a = 2 * math.pi * j / 8
            tip = rm - 4.0 if j % 2 == 0 else rm * 0.62
            add(B.cone(f"{tag}ray{j}", P(cm, 0.0), P(cm + tip * math.sin(a), tip * math.cos(a)), 7.5, 0.8, m, 4))
        for o in (1, -1):
            add(B.cone(f"{tag}boss{o}", P(cm, 0.0), P(cm, 0.0, o * 9), 13, 7, m, 8))
        add(B.sphere(f"{tag}stone", P(cm, 0.0, 9), 6.5, D.surf("chop_stone", (0.5, 0.02, 0.03), 0.0, 0.08)))
        for j, tip in enumerate(((cm + rm, 0.0), (cm - rm, 0.0), (cm, rm), (cm, -rm))):
            pass
        top = 188.0
        add(B.cone(f"{tag}finial0", P(top - 4, 0.0), P(top + 10, 0.0), 8, 5, m, 20))
        add(B.sphere(f"{tag}finial1", P(top + 22, 0.0), 13, m))
        add(B.cone(f"{tag}finial2", P(top + 32, 0.0), P(top + 60, 0.0), 5, 0.6, m, 16))
    else:
        apex = 190.0
        wtop = lambda d: yt if d <= 40 else yt * max(0.0, 1 - ((d - 40) / (apex - 40)) ** 1.6)
        w_at = lambda d: wid(d) if d <= 0 else wtop(d)
        half = list(sleft) + [P(d, wtop(d)) for d in (20, 40, 70, 100, 130, 155, 175, apex)]
        for sgn in (1, -1):
            add(B.smooth_tube(f"{tag}bar{sgn}", [(q[0], sgn * q[1], q[2]) for q in half], r_tube, m))
        add(B.tube(f"{tag}sill", [P(low, -wid(low)), P(low, wid(low))], 4.5, m))
        # the lattice: thin rods on the diagonal, cut round the oval boss, a rosette on every crossing
        ce, ea, eb, sp = low + 105, 50.0, 34.0, 30.0
        inside = lambda d, y: low + 2 <= d and abs(y) <= w_at(d) - r_tube + 2 and ((d - ce) / ea) ** 2 + (y / eb) ** 2 >= 1.0
        k = 0
        for fam in (1, -1):
            for c in range(-12, 16):
                run = []
                for i in range(0, 421):
                    d = low + i
                    y = fam * (d - (low + c * sp))
                    if inside(d, y):
                        run.append(P(d, y))
                    if run and (not inside(d, y) or i == 420):
                        if len(run) > 6:
                            add(B.tube(f"{tag}rod{k}", [run[0], run[-1]], 2.2, m))
                            k += 1
                        run = []
        k = 0
        for c1 in range(-12, 16):
            for c2 in range(-12, 16):
                d, y = low + (c1 + c2) * sp / 2, (c2 - c1) * sp / 2
                if inside(d, y) and abs(y) <= w_at(d) - r_tube - 5 and ((d - ce) / (ea + 6)) ** 2 + (y / (eb + 6)) ** 2 >= 1.0:
                    add(B.cone(f"{tag}rose{k}", P(d, y, -3.4), P(d, y, 3.4), 5.2, 5.2, m, 6))
                    k += 1
        add(B.tube(f"{tag}oval", [P(ce + ea * math.sin(2 * math.pi * j / 48), eb * math.cos(2 * math.pi * j / 48)) for j in range(48)], 4.0, m, cyclic=True))
        for o in (1, -1):
            add(B.cone(f"{tag}boss{o}", P(ce, 0.0), P(ce, 0.0, o * 10), 20, 9, m, 6))
        for j in range(4):
            a = math.pi / 2 * j
            add(B.cone(f"{tag}stay{j}", P(ce + 18 * math.sin(a), 18 * math.cos(a)), P(ce + (ea if j % 2 else eb) * math.sin(a), (ea if j % 2 else eb) * math.cos(a)), 3.2, 2.2, m, 12))
        add(B.cone(f"{tag}finial0", P(apex - 8, 0.0), P(apex + 6, 0.0), 9, 9, m, 20))
        add(B.cone(f"{tag}finial1", P(apex + 6, 0.0), P(apex + 12, 0.0), 6, 6, m, 20))
        add(B.cone(f"{tag}finial2", P(apex + 12, 0.0), P(apex + 36, 0.0), 6, 17, m, 24))
        add(B.cone(f"{tag}finial3", P(apex + 36, 0.0), P(apex + 104, 0.0), 17, 0.6, m, 24))
    return made


def sissy_sheet(samples=48):
    """The three show bars, each in two metals, standing upright from the back rest up."""
    import build3d
    bpy.ops.wm.read_factory_settings(use_empty=True)
    build3d._mats.clear()
    use(build3d)
    for c_, (st, fin) in enumerate((("chain", "chrome"), ("scroll", "chrome"), ("filigree", "chrome"), ("chain", "steel"), ("scroll", "brass"), ("filigree", "gold"))):
        made = sissy_bar(st, fin, [(0.0, 92.0, -520.0), (0.0, 88.0, -190.0), (0.0, 84.0, 0.0)], tag=f"s{c_}")
        e = bpy.data.objects.new("bay", None)
        bpy.context.collection.objects.link(e)
        for n in made:
            o = bpy.data.objects.get(n)
            if o is not None:
                o.parent = e
        e.location = (0, (2.5 - c_) * 0.25, 0)
    sc = _studio(2200, 1000, samples, (-2.75, 0.0, 0.2), (0.0, 0.0, 0.055), 60, -0.5)
    os.makedirs(OUT, exist_ok=True)
    sc.render.filepath = os.path.join(OUT, "sissy.png")
    bpy.ops.render.render(write_still=True)
    return sc.render.filepath


def _studio(w, h, samples, cam_loc, cam_tgt, lens, floor_z):
    from mathutils import Vector
    sc = bpy.context.scene
    sc.render.engine, sc.cycles.device, sc.cycles.samples, sc.cycles.use_denoising = "CYCLES", "CPU", samples, True
    sc.render.resolution_x, sc.render.resolution_y = w, h
    wd = bpy.data.worlds.new("w")
    sc.world = wd
    wd.use_nodes = True
    wd.node_tree.nodes["Background"].inputs[0].default_value = (0.5, 0.51, 0.53, 1)
    wd.node_tree.nodes["Background"].inputs[1].default_value = 0.35
    fm = bpy.data.materials.new("floor")
    fm.use_nodes = True
    fm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.2, 0.2, 0.21, 1)
    fm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.45
    bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 0, floor_z))
    bpy.context.object.data.materials.append(fm)
    for loc, size, power in (((-0.5, -0.7, 0.7), 0.7, 60), ((0.7, -0.5, 0.5), 0.6, 40), ((0, 0.1, 0.9), 0.9, 45)):
        ld = bpy.data.lights.new("l", "AREA")
        ld.size, ld.energy = size, power
        lo = bpy.data.objects.new("l", ld)
        sc.collection.objects.link(lo)
        lo.location = loc
        lo.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    cd = bpy.data.cameras.new("cam")
    cd.lens = lens
    cam = bpy.data.objects.new("cam", cd)
    sc.collection.objects.link(cam)
    cam.location = cam_loc
    cam.rotation_euler = (Vector(cam_tgt) - Vector(cam_loc)).to_track_quat("-Z", "Y").to_euler()
    sc.camera = cam
    try:
        sc.view_settings.look = "AgX - Medium High Contrast"
    except Exception:
        pass
    sc.view_settings.exposure = -1.6
    return sc


def pegs_sheet(samples=48):
    """Every peg design (rows) in every metal (columns), lying on the bench."""
    import build3d
    bpy.ops.wm.read_factory_settings(use_empty=True)
    build3d._mats.clear()
    use(build3d)
    for r, st in enumerate(PEGS):
        for c_, fin in enumerate(METALS):
            # p is (x, z); the peg lies along +y from y 172, so slide each into its own bay with a parent
            made = peg(f"p_{st}_{fin}", (0, 0), 1, st, fin)
            e = bpy.data.objects.new("bay", None)
            bpy.context.collection.objects.link(e)
            for n in made:
                o = bpy.data.objects.get(n)
                if o is not None:
                    o.parent = e
            e.rotation_euler = (0, 0, math.radians(-62))
            e.location = ((c_ - 1.5) * 0.125 - 0.172 * math.sin(math.radians(62)) - 0.02, (1 - r) * 0.085 - 0.172 * math.cos(math.radians(62)), 0)
    sc = _studio(1800, 1100, samples, (0.0, -0.52, 0.42), (0.0, -0.01, 0.0), 50, -0.0165)
    os.makedirs(OUT, exist_ok=True)
    sc.render.filepath = os.path.join(OUT, "pegs.png")
    bpy.ops.render.render(write_still=True)
    return sc.render.filepath


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "pegs"
    if what == "pegs":
        print(pegs_sheet())
    elif what == "covers":
        print(covers_sheet())
    elif what == "lamps":
        print(lamps_sheet())
    elif what == "sissy":
        print(sissy_sheet(int(sys.argv[2]) if len(sys.argv) > 2 else 48))
