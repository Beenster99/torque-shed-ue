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
