"""Definition pass, part by part (beenster, 14 Nov: "go right into the detail of every part").

build3d.py --definition builds the outline bike first, then this module replaces whole areas of it with
parts drawn the way the real things are made: fork legs with castings, seals and tapers, radial calipers
bolted to the fork, discs on spoked carriers, a lamp that faces forward, bars with real controls, an engine
made of separate castings, a stitched seat. Reference pictures: /mnt/project-files/bike_models_v2/refs/.
Every part is drawn from the model's own numbers, so the same code details any model of any family.
"""
import json
import math
import os
import struct

import bpy

B = None        # the build3d module, set by run()


def drop(*prefixes):
    """Remove outline-build parts that a detailed part replaces."""
    for ob in list(bpy.data.objects):
        if ob.name.startswith(prefixes):
            bpy.data.objects.remove(ob, do_unlink=True)


def surf(name, col, metallic=0.0, rough=0.5, coat=0.0, bump=None, wear=0.2):
    """A surface of our own, with the same slight wear as build3d.mat gives the library ones."""
    key = "d3_" + name
    if key in bpy.data.materials:
        return bpy.data.materials[key]
    m = bpy.data.materials.new(key)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*col, 1)
    b.inputs["Metallic"].default_value = metallic
    b.inputs["Roughness"].default_value = rough
    if coat:
        b.inputs["Coat Weight"].default_value = coat
        b.inputs["Coat Roughness"].default_value = 0.05
    tc = nt.nodes.new("ShaderNodeTexCoord")
    if wear:
        nz = nt.nodes.new("ShaderNodeTexNoise")
        nz.inputs["Scale"].default_value, nz.inputs["Detail"].default_value = 14.0, 6.0
        mr = nt.nodes.new("ShaderNodeMapRange")
        mr.inputs["From Min"].default_value, mr.inputs["From Max"].default_value = 0.3, 0.75
        mr.inputs["To Min"].default_value, mr.inputs["To Max"].default_value = max(0.02, rough * (1 - wear)), min(1.0, rough * (1 + wear) + 0.04)
        nt.links.new(tc.outputs["Object"], nz.inputs["Vector"])
        nt.links.new(nz.outputs["Fac"], mr.inputs["Value"])
        nt.links.new(mr.outputs["Result"], b.inputs["Roughness"])
    if bump:
        size_mm, depth = bump
        n2 = nt.nodes.new("ShaderNodeTexNoise")
        n2.inputs["Scale"].default_value, n2.inputs["Detail"].default_value = 1.0 / (size_mm * B.MM), 3.0
        bp = nt.nodes.new("ShaderNodeBump")
        bp.inputs["Strength"].default_value, bp.inputs["Distance"].default_value = depth, size_mm * B.MM * 0.3
        nt.links.new(tc.outputs["Object"], n2.inputs["Vector"])
        nt.links.new(n2.outputs["Fac"], bp.inputs["Height"])
        nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    return m


def lathe(name, p0, d, prof, material, segs=40, sharp=True, ell=(1.0, 1.0), uv=False, caps=True, square=0.0):
    """Turn a profile [(t along the axis, radius)] about the axis through p0 (mm, 3D) along unit d.
    ell squashes the section to an oval; uv lays the skin out flat in millimetres (round, along)."""
    from mathutils import Vector
    a, ax = Vector(p0), Vector(d).normalized()
    side = ax.cross(Vector((0, 1, 0)))
    if side.length < 1e-4:
        side = ax.cross(Vector((0, 0, 1)))
    side.normalize()
    up = side.cross(ax)
    verts, faces, m = [], [], len(prof)
    for t, r in prof:
        for k in range(segs):
            w = 2 * math.pi * k / segs
            cw_, sw_ = math.cos(w), math.sin(w)
            if square:       # a rounded-square section in place of a round one (square = the superellipse power)
                cw_, sw_ = math.copysign(abs(cw_) ** (2 / square), cw_), math.copysign(abs(sw_) ** (2 / square), sw_)
            q = a + ax * t + side * (r * ell[0] * cw_) + up * (r * ell[1] * sw_)
            verts.append(B.V(q.x, q.y, q.z))
    for j in range(m - 1):
        for k in range(segs):
            k2 = (k + 1) % segs
            faces.append((j * segs + k, j * segs + k2, (j + 1) * segs + k2, (j + 1) * segs + k))
    nq = len(faces)
    if caps:
        faces += [tuple(range(segs - 1, -1, -1)), tuple((m - 1) * segs + k for k in range(segs))]
    ob = B.mesh_obj(name, verts, faces, material, True, 0, sharp)
    if uv:
        rmax = max(r for t, r in prof)
        layer = ob.data.uv_layers.new(name="skin")
        for pi, poly in enumerate(ob.data.polygons):
            if pi >= nq:
                continue
            ks = [v % segs for v in poly.vertices]
            wrap = 0 in ks and segs - 1 in ks
            for li, v in zip(poly.loop_indices, poly.vertices):
                k = v % segs
                if wrap and k == 0:
                    k = segs
                layer.data[li].uv = (k / segs * 2 * math.pi * rmax, prof[v // segs][0])
    B.bm_fix_normals(ob)
    return ob


def bolt(name, p, d, r=5.0, h=5.0, material=None, socket=True):
    """A bolt head at p (mm, 3D) standing along unit d: a round cap head with a dark socket, or a hexagon."""
    material = material or B.mat("steel")
    q = tuple(p[i] + d[i] * h for i in range(3))
    B.cone(name, p, q, r, r * 0.94, material, 20 if socket else 6)
    if socket:
        q2 = tuple(p[i] + d[i] * (h + 0.3) for i in range(3))
        q1 = tuple(p[i] + d[i] * (h - 1.5) for i in range(3))
        B.cone(name + "s", q1, q2, r * 0.52, r * 0.52, B.mat("rubber"), 6)


def glb_prim(path, lod, prim=0):
    """Positions (metres) and triangle indices of one primitive of a game part file."""
    raw = open(path, "rb").read()
    n = struct.unpack("<I", raw[12:16])[0]
    j = json.loads(raw[20:20 + n])
    blob = raw[20 + n + 8:]
    node = next(x for x in j["nodes"] if x["name"] == lod)
    pr = j["meshes"][node["mesh"]]["primitives"][prim]

    def acc(i):
        a = j["accessors"][i]
        bv = j["bufferViews"][a["bufferView"]]
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        fmt, size = {5126: ("<3f", 12), 5123: ("<H", 2), 5125: ("<I", 4)}[a["componentType"]]
        stride = bv.get("byteStride", size)
        return [struct.unpack_from(fmt, blob, off + k * stride) for k in range(a["count"])]
    return acc(pr["attributes"]["POSITION"]), [v[0] for v in acc(pr["indices"])]


def P(q, y):
    return (q[0], y, q[1])


def bar(name, a, b, w, y0, y1, material, bevel=0.6):
    """A flat strut between two points of a side view."""
    L_ = math.hypot(b[0] - a[0], b[1] - a[1]) or 1.0
    n = (-(b[1] - a[1]) / L_ * w / 2, (b[0] - a[0]) / L_ * w / 2)
    return B.prism(name, [(a[0] + n[0], a[1] + n[1]), (b[0] + n[0], b[1] + n[1]), (b[0] - n[0], b[1] - n[1]), (a[0] - n[0], a[1] - n[1])], y0, y1, material, bevel)


def disc(tag, c, d, y, s, game_id, arms=6, buttons=10, carrier=None):
    """A brake disc as the game draws its rotors (the drilled steel ring is the game's own part), on a
    carrier drawn from the reference (disc carrier lattice): a bolted hub ring, an outer frame of straight
    struts from button to button, and pairs of struts from the hub out to the buttons, so the middle of
    the disc is a truss of triangles and open windows. The rotor floats on steel buttons at the corners."""
    add, unit = B.add, B.unit
    carrier = carrier or surf("carrier", (0.05, 0.05, 0.055), 0.35, 0.42)
    path = os.path.join(os.path.dirname(B.ROOT), "Data", "Parts", "Redrawn", "disc", game_id + ".glb")
    rin = d - 36
    if os.path.exists(path):
        pos, idx = glb_prim(path, "lod0", 0)
        r0 = max(math.hypot(p[1], p[2]) for p in pos)
        k = d / (r0 * 1000.0)
        # the steel primitive also holds the game's own carrier bolts near the middle: keep only the ring,
        # whose inner edge is where the game's carrier (the second primitive) ends
        try:
            cpos, _ = glb_prim(path, "lod0", 1)
            rin = max(math.hypot(p[1], p[2]) for p in cpos) * 1000.0 * k
        except Exception:
            rin = d * 0.62
        rad = [math.hypot(p[1], p[2]) * 1000.0 * k for p in pos]
        verts = [B.V(c[0] + p[1] * 1000 * k, y + s * p[0] * 1000, c[1] + p[2] * 1000 * k) for p in pos]
        faces = [(idx[t], idx[t + 1], idx[t + 2]) for t in range(0, len(idx), 3) if min(rad[idx[t]], rad[idx[t + 1]], rad[idx[t + 2]]) > rin - 1.0]
        ob = B.mesh_obj(f"{tag}_rotor{y}", verts, faces, surf("rotor", (0.5, 0.5, 0.51), 1.0, 0.3, wear=0.35), True)
        B.bm_fix_normals(ob)
    else:
        B.revolve(f"{tag}_rotor{y}", [(d, y + 2.5), (d, y - 2.5), (rin, y - 2.5), (rin, y + 2.5)], c, B.mat("steel"), 64)
    ya, yb = y - 2.4, y + 2.4
    n_out = buttons if buttons else 2 * arms
    n_hub = n_out // 2
    r_out = rin - 3.0
    rh = max(26.0, min(0.44 * r_out, 62.0))
    B.revolve(f"{tag}_carhub{y}", [(rh, yb + 1), (rh, ya - 1), (rh * 0.55, ya - 1), (rh * 0.55, yb + 1)], c, carrier, 48)
    r_frame = rin * 0.87
    outer = [add(c, unit(360.0 / n_out * k + 9), r_frame) for k in range(n_out)]
    wbar = max(5.0, rin * 0.095)
    for k in range(n_out):
        # the outer frame: a straight strut from corner to corner, and a short tab out to each button
        bar(f"{tag}_frame{y}{k}", outer[k], outer[(k + 1) % n_out], wbar, ya, yb, carrier)
        bar(f"{tag}_tab{y}{k}", outer[k], add(c, unit(360.0 / n_out * k + 9), rin + 1), wbar * 1.5, ya, yb, carrier)
        B.cone(f"{tag}_ear{y}{k}", (outer[k][0], ya, outer[k][1]), (outer[k][0], yb, outer[k][1]), wbar * 0.9, wbar * 0.9, carrier, 20)
    for h in range(n_hub):
        # from each lug of the hub two arms lean away from each other, out to the corners one step either
        # side of the nearest pair, so each arm crosses its neighbour's and the middle becomes a star of windows
        a_h = 360.0 / n_hub * h + 9 + 180.0 / n_out
        hp = add(c, unit(a_h), rh - 2)
        B.cone(f"{tag}_lug{y}{h}", (hp[0], ya, hp[1]), (hp[0], yb, hp[1]), wbar * 0.8, wbar * 0.8, carrier, 16)
        for sg, k in ((0, 2 * h - 1), (1, 2 * h + 2)):
            bar(f"{tag}_strut{y}{h}{sg}", hp, outer[k % n_out], wbar * 0.82, ya, yb, carrier)
    nb_ = 6 if n_hub >= 6 else 5
    for k in range(nb_):
        q = add(c, unit(360.0 / nb_ * k + 30), rh * 0.78)
        bolt(f"{tag}_hubbolt{y}{k}", (q[0], y + s * 3.2, q[1]), (0, s, 0), min(5.0, rh * 0.1), 4.0)
    if buttons:
        for k in range(n_out):
            q = add(c, unit(360.0 / n_out * k + 9), rin + 0.5)
            B.cone(f"{tag}_button{y}{k}", (q[0], y - 4.6, q[1]), (q[0], y + 4.6, q[1]), wbar * 0.95, wbar * 0.95, B.mat("steel"), 20)
            B.cone(f"{tag}_buttonin{y}{k}", (q[0], y - 4.9, q[1]), (q[0], y + 4.9, q[1]), wbar * 0.42, wbar * 0.42, B.mat("rubber"), 12)
    else:
        B.revolve(f"{tag}_carout{y}", [(rin + 3, yb), (rin + 3, ya), (rin - 4, ya), (rin - 4, yb)], c, carrier, 64)
    B.cone(f"{tag}_flange{y}", (c[0], s * 30, c[1]), (c[0], y - s * 2, c[1]), 50, rh * 0.9, B.mat("alu_dark"), 40)
    return rin


def caliper(tag, c, cc, y, s, body, fork_pt=None, uf=None, size=1.0, lugs=True):
    """A four-piston radial caliper: two halves either side of the disc joined by bridges with an open
    window over the pads, pistons standing proud of each face, pad pins, a bleed nipple, a banjo union,
    and two ears bolted radially to lugs cast on the fork bottom."""
    add = B.add
    L_ = math.hypot(cc[0] - c[0], cc[1] - c[1])
    er = ((cc[0] - c[0]) / L_, (cc[1] - c[1]) / L_)
    ea = (-er[1], er[0])
    if uf and ea[0] * uf[0] + ea[1] * uf[1] < 0:
        ea = (-ea[0], -ea[1])
    k = size

    def Q(a, r):
        return (cc[0] + (ea[0] * a + er[0] * r) * k, cc[1] + (ea[1] * a + er[1] * r) * k)
    steel, dark = B.mat("steel"), B.mat("rubber")
    half = [Q(-60, -30), Q(-50, -40), Q(50, -40), Q(60, -30), Q(64, 8), Q(57, 28), Q(40, 38), Q(-40, 38), Q(-57, 28), Q(-64, 8)]
    yo0, yo1, yi0, yi1 = y + s * 6.5, y + s * 37 * k, y - s * 6.5, y - s * 33 * k
    B.pillow(f"{tag}_out", half, min(yo0, yo1), max(yo0, yo1), body, 5, 10)
    B.pillow(f"{tag}_in", half, min(yi0, yi1), max(yi0, yi1), body, 5, 10)
    lo, hi = min(yi1, yo1), max(yi1, yo1)
    for n, (a0, a1) in enumerate(((-60, -24), (24, 60))):
        B.prism(f"{tag}_bridge{n}", [Q(a0, 20), Q(a1, 20), Q(a1 * 0.8 if a1 > 0 else a1, 41), Q(a0 if a0 > 0 else a0 * 0.8, 41)], lo + 1, hi - 1, body, 3)
    B.prism(f"{tag}_rib", [Q(-4, 24), Q(4, 24), Q(4, 40), Q(-4, 40)], lo + 2, hi - 2, body, 1.5)
    for sg in (-1, 1):
        B.prism(f"{tag}_pad{sg}", [Q(-42, -28), Q(42, -28), Q(42, 26), Q(-42, 26)], y + sg * 3.2, y + sg * 6.4, dark, 0.5)
        B.prism(f"{tag}_padback{sg}", [Q(-46, -30), Q(46, -30), Q(46, 36), Q(-46, 36)], y + sg * 6.4, y + sg * 8.5, steel, 0.5)
    for a in (-14, 14):
        q = Q(a, 31)
        B.tube(f"{tag}_pin{a}", [P(q, lo - 2), P(q, hi + 2)], 2.6, steel)
    for sg, yf in ((s, yo1), (-s, yi1)):
        for n, (a, rr) in enumerate(((-27, 18.5), (27, 16.5))):
            q = Q(a, -8)
            B.cone(f"{tag}_pot{sg}{n}", P(q, yf - sg * 2), P(q, yf + sg * 5.5), rr * k, rr * k * 0.9, body, 28)
            B.cone(f"{tag}_potcap{sg}{n}", P(q, yf + sg * 5.5), P(q, yf + sg * 6.5), rr * k * 0.55, rr * k * 0.5, steel, 20)
    e3 = (er[0], 0, er[1])
    q = Q(48, 38)
    B.cone(f"{tag}_bleedboss", P(q, yo1 - s * 12), P(add(q, er, 7), yo1 - s * 12), 6, 5, body, 12)
    B.cone(f"{tag}_bleed", P(add(q, er, 7), yo1 - s * 12), P(add(q, er, 19), yo1 - s * 12), 3.2, 2.2, steel, 10)
    B.cone(f"{tag}_bleedcap", P(add(q, er, 15), yo1 - s * 12), P(add(q, er, 21), yo1 - s * 12), 3.6, 3.4, dark, 10)
    banjo = Q(-30, 40)
    yb = yo1 - s * 14
    B.cone(f"{tag}_banjo", P(banjo, yb), P(add(banjo, er, 6), yb), 8.5, 8.5, steel, 20)
    bolt(f"{tag}_banjobolt", P(add(banjo, er, 6), yb), e3, 6.5, 5, steel, False)
    ears = []
    if lugs:
        ym = y + s * 16
        for n, a in enumerate((-52, 52)):
            e0, e1 = Q(a, -52), Q(a, -6)
            B.cone(f"{tag}_ear{n}", P(e0, ym), P(e1, ym), 12.5, 12.5, body, 24)
            bolt(f"{tag}_earbolt{n}", P(e1, ym), e3, 8.5, 7, steel, True)
            B.cone(f"{tag}_earwasher{n}", P(e1, ym), P(add(e1, er, 1.2), ym), 10.5, 10.5, steel, 20)
            ears.append((Q(a, -52), ym))
            if fork_pt and uf:
                # the lug cast on the fork bottom: from the leg out to the foot of the ear
                t = (e0[0] - fork_pt[0]) * uf[0] + (e0[1] - fork_pt[1]) * uf[1]
                f0, f1 = add(fork_pt, uf, t - 24), add(fork_pt, uf, t + 24)
                g0, g1 = add(add(e0, er, -9), ea, -13), add(add(e0, er, -9), ea, 13)
                h0, h1 = add(e0, ea, -13), add(e0, ea, 13)
                yl = fork_pt_y(fork_pt, s)
                B.pillow(f"{tag}_lug{n}", [f0, g0, h0, h1, g1, f1], min(ym - 11, ym + 11), max(ym - 11, ym + 11), yl, 3, 6)
    return dict(Q=Q, er=er, ea=ea, banjo=(banjo, yb), yo1=yo1)


_LUG = {}


def fork_pt_y(fork_pt, s):
    return _LUG["m"]


def front_end(model, c):
    """Fork legs, axle, front calipers and discs."""
    add = B.add
    F, uf, nb, fk, top = c["F"], c["uf"], c["nb"], c["fk"], c["top"]
    wh = model["wheels"]["front"]
    gold = c["forkm"].name.startswith("gold")
    drop("stanchion", "forktube", "forkfoot", "caliper", "forkseal", "pinch", "brakehose-1", "brakehose1", "brakehose_f",
         "front_disc", "front_carrier", "axlenut_f", "axle_f")
    cast = surf("forkcast", (0.33, 0.33, 0.34), 0.95, 0.42, bump=(1.2, 0.12))
    outer = surf("forkgold", (0.5, 0.33, 0.07), 1.0, 0.24, coat=0.4) if gold else surf("forkblack", (0.03, 0.03, 0.035), 0.9, 0.3, coat=0.3)
    chrome, steel, rubber = B.mat("chrome"), B.mat("steel"), B.mat("rubber")
    _LUG["m"] = cast
    d3 = (uf[0], 0, uf[1])
    st, ly, Lf = fk["slider_top"], fk["lower_yoke"], fk["length"]
    usd = fk.get("usd", True)
    cal_body = surf("caliper", (0.36, 0.36, 0.37), 0.9, 0.36, bump=(0.8, 0.08))
    d = wh.get("disc")
    cc = add(add(F, uf, 75), nb, math.sqrt(max(1.0, (d - 25) ** 2 - 75 ** 2))) if d else None
    hose_pts = {}
    for s in (-1, 1):
        y = s * 105
        base = (F[0], y, F[1])
        if usd:
            lathe(f"forkbottom{s}", base, d3, [(-36, 0), (-36, 19), (-33, 24), (-20, 27.5), (-6, 29.5), (26, 29.5), (40, 27.5), (84, 26.5), (98, 25.6), (106, 24.0), (110, 22.6), (110, 21)], cast)
            lathe(f"stanchion{s}", base, d3, [(108, 21.5), (st + 8, 21.5)], chrome, 36)
            # (reference: fork cutaway) the outer tube ends in a seal housing: a collar a little bigger than the
            # tube with two machined grooves, and the dust wiper's lip showing below it
            lathe(f"forkwiper{s}", base, d3, [(st - 7, 22.0), (st - 7, 24.6), (st - 4, 26.4), (st, 27.2), (st + 2, 22)], rubber, 36)
            lathe(f"forkouter{s}", base, d3, [(st, 22.5), (st, 31.2), (st + 3, 32.8), (st + 12, 32.8), (st + 13, 31.9), (st + 15, 31.9), (st + 16, 32.8), (st + 24, 32.8), (st + 25, 31.9),
                                             (st + 27, 31.9), (st + 28, 32.8), (st + 34, 32.6), (st + 44, 30.2), (st + 120, 29.6), (ly - 60, 29.6), (ly - 44, 30.8),
                                             (ly + 40, 30.8), (ly + 60, 28.2), (Lf - 40, 27.8), (Lf - 30, 28.6), (Lf + 5, 28.6), (Lf + 5, 0)], outer, 48)
        else:
            # right way up: chrome stanchion above, a cast slider below with its seal at the top
            lathe(f"forkbottom{s}", base, d3, [(-36, 0), (-36, 19), (-33, 24), (-20, 27.5), (-6, 29.5), (26, 29.5), (40, 27.5), (st - 30, 26.5), (st - 22, 28.5), (st - 4, 28.5), (st - 4, 22)], cast)
            lathe(f"forkwiper{s}", base, d3, [(st - 4, 27.5), (st + 3, 26.5), (st + 8, 23.5), (st + 8, 21.8)], rubber, 36)
            lathe(f"stanchion{s}", base, d3, [(st - 6, 20.5), (Lf + 5, 20.5), (Lf + 5, 0)], chrome, 36)
        # fork cap: a hexagon with the adjuster screw in the middle
        cap = add(F, uf, Lf + 5)
        B.cone(f"forkcap{s}", P(cap, y), P(add(cap, uf, 9), y), 17, 16, surf("capblue", (0.05, 0.12, 0.4), 1.0, 0.3), 6)
        B.cone(f"forkadj{s}", P(add(cap, uf, 9), y), P(add(cap, uf, 14), y), 7, 6.5, steel, 16)
        B.cone(f"forkadjslot{s}", P(add(cap, uf, 13.2), y), P(add(cap, uf, 14.3), y), 3.5, 3.5, rubber, 4)
        # axle clamp: a boss across the foot with two pinch bolts under it
        B.cone(f"axleboss{s}", (F[0], y - 31, F[1]), (F[0], y + 31, F[1]), 30.5, 30.5, cast, 36)
        for dy in (-17, 17):
            q = (F[0] + 6, y + dy, F[1] - 27)
            B.cone(f"pinchboss{s}{dy}", q, (q[0] + 26, q[1], q[2]), 8, 8, cast, 16)
            bolt(f"pinch{s}{dy}", (q[0] + 26, q[1], q[2]), (1, 0, 0), 5.5, 5)
        B.prism(f"pinchslot{s}", [(F[0] + 8, F[1] - 31.2), (F[0] + 30, F[1] - 31.2), (F[0] + 30, F[1] - 29), (F[0] + 8, F[1] - 29)], y - 1.2, y + 1.2, rubber, 0)
        if d:
            info = caliper(f"caliper{s}", F, cc, s * 78, s, cal_body, F, uf)
            rin = disc("front", F, d, s * 78, s, "drilled_floating_320" if d >= 155 else "drilled_floating_300", 6, 10 if d < 155 else 12)
            hose_pts[s] = info
    yokes(model, c, cast)
    # axle: a hollow spindle with a hexagon in one end and a nut on the other
    B.tube("axle_f", [(F[0], -138, F[1]), (F[0], 138, F[1])], 11, steel)
    B.cone("axlehead", (F[0], 136, F[1]), (F[0], 141, F[1]), 16, 15, steel, 28)
    B.cone("axlehex", (F[0], 139.5, F[1]), (F[0], 141.3, F[1]), 8.5, 8.5, rubber, 6)
    B.cone("axlenut_f", (F[0], -136, F[1]), (F[0], -147, F[1]), 16, 16, steel, 6)
    # brake lines: from each caliper's banjo up behind the leg to a splitter under the lower yoke, one line on to the bar
    hose = surf("hose", (0.015, 0.015, 0.017), 0.0, 0.42)
    grip = c.get("grip") or (top[0] - 60, top[1] + 60)
    split = add(add(F, uf, ly - 34), nb, fk["offset"] + 66)
    for s, info in hose_pts.items():
        bj, yb = info["banjo"]
        er = info["er"]
        p0 = add(bj, er, 4)
        B.smooth_tube(f"brakeline{s}", [P(p0, yb), P(add(p0, uf, 60), yb + s * 4), P(add(add(F, uf, 250), nb, 52), s * 104), P(add(add(F, uf, ly - 90), nb, 58), s * 92),
                                        P(split, s * 30), P(split, s * 8)], 4.2, hose)
    if hose_pts:
        B.cone("brakesplit", P(split, -20), P(split, 20), 9, 9, B.mat("alu_dark"), 6)
        B.smooth_tube("brakeline_up", [P(split, -12), P(add(split, uf, 70), -40), P(add(top, nb, 74), -66), (grip[0] + 86, -150, grip[1] - 30), (grip[0] + 46, -214, grip[1] - 8)], 4.2, hose)


def slab(name, org, uf, nb, poly, t0, t1, material, bevel=2.0):
    """A plate lying square to the fork: poly is [(y, n)] with n measured back from the fork line."""
    n = len(poly)
    verts = []
    for t in (t0, t1):
        for y, nn in poly:
            verts.append(B.V(org[0] + uf[0] * t + nb[0] * nn, y, org[1] + uf[1] * t + nb[1] * nn))
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, 2 * n))] + [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    ob = B.mesh_obj(name, verts, faces, material, True, bevel)
    B.bm_fix_normals(ob)
    return ob


def hull(points):
    pts = sorted(set(points))

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, up = [], []
    for q in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], q) <= 0:
            lo.pop()
        lo.append(q)
    for q in reversed(pts):
        while len(up) >= 2 and cross(up[-2], up[-1], q) <= 0:
            up.pop()
        up.append(q)
    return lo[:-1] + up[:-1]


def yokes(model, c, cast):
    """Top and bottom yokes as machined clamps (reference: fork cutaway): a plate that wraps each leg in a
    split clamp with pinch bolts, drawn back to the steering stem, with the stem nut on top."""
    F, uf, nb, fk = c["F"], c["uf"], c["nb"], c["fk"]
    Lf, ly, off = fk["length"], fk["lower_yoke"], fk["offset"]
    drop("yoke")
    steel = B.mat("steel")
    fwd = (-nb[0], 0, -nb[1])
    for tag, at, th, rc, rs in (("top", Lf - 34, 24, 40, 30), ("low", ly - 15, 34, 43, 36)):
        ring = lambda cy, cn, r, n=28: [(cy + r * math.cos(2 * math.pi * k / n), cn + r * math.sin(2 * math.pi * k / n)) for k in range(n)]
        poly = hull(ring(-105, 0, rc) + ring(105, 0, rc) + ring(0, off + 6, rs))
        # scallop the front edge between the legs so the plate reads as a bridge, not a block
        poly = [(y, n_ + (16 * (1 - (y / 62.0) ** 2) if abs(y) < 62 and n_ < 0 else 0)) for y, n_ in poly]
        slab(f"yoke_{tag}", F, uf, nb, poly, at, at + th, cast, 3.0)
        for s in (-1, 1):
            for k_, dt in enumerate((th * 0.3, th * 0.72) if th > 28 else (th * 0.5,)):
                q = B.add(B.add(F, uf, at + dt), nb, -rc + 3)
                bolt(f"yokepinch_{tag}{s}{k_}", (q[0], s * (105 + 20), q[1]), fwd, 5.5, 5)
            q0 = B.add(B.add(F, uf, at - 0.5), nb, -rc - 0.6)
            q1 = B.add(B.add(F, uf, at + th + 0.5), nb, -rc - 0.6)
            B.tube(f"yokeslot_{tag}{s}", [(q0[0], s * 105, q0[1]), (q1[0], s * 105, q1[1])], 1.4, B.mat("rubber"))
    stem = B.add(B.add(F, uf, Lf - 10), nb, off)
    B.cone("stemnut", (stem[0], 0, stem[1]), (stem[0] + uf[0] * 11, 0, stem[1] + uf[1] * 11), 22, 21, steel, 6)
    B.cone("stemcap", (stem[0] + uf[0] * 11, 0, stem[1] + uf[1] * 11), (stem[0] + uf[0] * 14, 0, stem[1] + uf[1] * 14), 13, 11, B.mat("alu_dark"), 24)


def chaikin(poly, n=2):
    for _ in range(n):
        out = []
        for a_, b_ in zip(poly, poly[1:] + poly[:1]):
            out += [(0.75 * a_[0] + 0.25 * b_[0], 0.75 * a_[1] + 0.25 * b_[1]), (0.25 * a_[0] + 0.75 * b_[0], 0.25 * a_[1] + 0.75 * b_[1])]
        poly = out
    return poly


def sell(hy, hz, k=1.0, n=3.2, m=56):
    """A superellipse outline [(y, z)]: the rounded-rectangle face of a lamp or a display."""
    out = []
    for i in range(m):
        w = 2 * math.pi * i / m
        cy, sz = math.cos(w), math.sin(w)
        out.append((hy * k * math.copysign(abs(cy) ** (2 / n), cy), hz * k * math.copysign(abs(sz) ** (2 / n), sz)))
    return out


def rings(name, org, stations, material, hy, hz, n=3.2, m=56, close=(True, True), sharp=False):
    """A shell through superellipse sections [(x offset, scale, z offset)] facing along x."""
    verts, faces = [], []
    for dx, k, dz in stations:
        verts += [B.V(org[0] + dx, y, org[1] + dz + z) for y, z in sell(hy, hz, k, n, m)]
    for j in range(len(stations) - 1):
        for i in range(m):
            i2 = (i + 1) % m
            faces.append((j * m + i, j * m + i2, (j + 1) * m + i2, (j + 1) * m + i))
    if close[0]:
        faces.append(tuple(range(m - 1, -1, -1)))
    if close[1]:
        faces.append(tuple((len(stations) - 1) * m + i for i in range(m)))
    ob = B.mesh_obj(name, verts, faces, material, True, 0, sharp)
    B.bm_fix_normals(ob)
    return ob


def glass_mat(name="clearglass", tint=(1, 1, 1)):
    key = "d3_" + name
    if key in bpy.data.materials:
        return bpy.data.materials[key]
    g = bpy.data.materials.new(key)
    g.use_nodes = True
    gb = g.node_tree.nodes["Principled BSDF"]
    gb.inputs["Base Color"].default_value = (*tint, 1)
    gb.inputs["Transmission Weight"].default_value, gb.inputs["Roughness"].default_value, gb.inputs["IOR"].default_value = 1.0, 0.02, 1.45
    return g


def glow(name, col, strength):
    key = "d3_" + name
    if key in bpy.data.materials:
        return bpy.data.materials[key]
    g = bpy.data.materials.new(key)
    g.use_nodes = True
    gb = g.node_tree.nodes["Principled BSDF"]
    gb.inputs["Base Color"].default_value = (*col, 1)
    gb.inputs["Emission Color"].default_value = (*col, 1)
    gb.inputs["Emission Strength"].default_value = strength
    return g


def headlamp(model, c):
    """A lamp that faces forward (reference: naked front three-quarter): a dark housing between the fork
    legs, a satin bezel, a running-light ring just inside it, a smoked reflector with a projector lens
    above a bar and two small main-beam cups below it, all behind a clear domed cover. The display stands
    behind it on a bracket, its face turned up to the rider."""
    F, uf, nb, fk = c["F"], c["uf"], c["nb"], c["fk"]
    head = [(i, s_) for i, s_ in enumerate(model.get("front_end", [])) if s_.get("part") == "head"]
    lens = next((s_ for i, s_ in head if s_.get("cls") == "lens" and "pts" in s_), None)
    if not lens or any(s_.get("round") for i, s_ in head):
        return
    if model["family"].lower() == "sport":
        # a faired bike keeps its lamps let into the nose; only the display is built here
        house = next((s_ for i, s_ in head if s_.get("cls") == "plastic" and "pts" in s_), None)
        for i, s_ in head:
            if s_ is house:
                drop(f"front{i}")
        if nose_lamps(model, c):
            drop("lens", "lensrim", "eye", "reflector")
        return display(model, c, head, None, house)  # the one dark shape in a sport nose is the display
    for i, s_ in head:
        drop(f"front{i}", f"lens{i}", f"bezel{i}", f"reflector{i}", f"eye{i}", f"eyeglass{i}", f"clock{i}", f"clockrim{i}")
    xs, zs = [q[0] for q in lens["pts"]], [q[1] for q in lens["pts"]]
    house = next((s_ for i, s_ in head if s_.get("cls") == "plastic" and "pts" in s_), lens)
    hz_ = [q[1] for q in house["pts"]]
    org = (max(xs) + 8, (min(hz_) + max(hz_)) / 2)
    hy, hz = 84.0, min(66.0, (max(hz_) - min(hz_)) / 2 + 2)
    plastic, satin = B.mat("plastic"), surf("bezel", (0.05, 0.05, 0.055), 0.85, 0.3)
    smoke = surf("smokechrome", (0.2, 0.2, 0.22), 1.0, 0.1, wear=0.0)
    # the housing narrows behind the face to pass between the fork legs
    rings("lamphouse", org, [(-118, 0.3, 4), (-96, 0.62, 3), (-52, 0.8, 1), (-34, 0.97, 0), (-4, 1.0, 0)], plastic, hy, hz, close=(True, False))
    rings("lampbezel", org, [(-6, 1.03, 0), (2, 1.03, 0), (5, 0.99, 0), (5, 0.9, 0), (-2, 0.88, 0)], satin, hy, hz, close=(False, False), sharp=True)
    rings("lampdrl", org, [(1, 0.895, 0), (3.5, 0.87, 0), (3.5, 0.81, 0), (0, 0.79, 0)], glow("drl", (0.9, 0.95, 1.0), 4.0), hy, hz, close=(False, False))
    rings("lampbowl", org, [(0, 0.8, 0), (-22, 0.7, 0), (-44, 0.45, 0), (-52, 0.2, 0)], smoke, hy, hz, close=(False, True))
    # the bar across the middle, with the projector above and the two main-beam cups below
    B.prism("lampbar", [(org[0] - 30, org[1] - 5), (org[0] - 2, org[1] - 4), (org[0] - 2, org[1] + 3), (org[0] - 30, org[1] + 5)], -hy * 0.8, hy * 0.8, satin, 1.5)
    pz = org[1] + hz * 0.38
    lathe("projring", (org[0] - 30, 0, pz), (1, 0, 0), [(0, 30), (22, 30), (24, 27), (24, 23.5), (4, 23.5)], satin, 40)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=23.5 * B.MM, location=B.V(org[0] - 15, 0, pz), segments=32, ring_count=16)
    ob = bpy.context.object
    ob.name = "projlens"
    ob.scale = (0.62, 1, 1)
    ob.data.materials.append(glass_mat())
    bpy.ops.object.shade_smooth()
    B.cone("projbulb", (org[0] - 34, 0, pz), (org[0] - 31, 0, pz), 9, 9, glow("bulb", (1.0, 0.97, 0.9), 2.0), 16)
    for sg in (-1, 1):
        q = (org[0] - 36, sg * hy * 0.36, org[1] - hz * 0.4)
        lathe(f"maincup{sg}", q, (1, 0, 0), [(0, 5), (6, 12), (16, 18), (30, 21), (32, 22.5), (33, 21)], smoke, 32)
        B.cone(f"mainled{sg}", (q[0] + 1, q[1], q[2]), (q[0] + 3, q[1], q[2]), 5, 5, glow("bulb", (1.0, 0.97, 0.9), 2.0), 12)
        # side stays from the lower yoke region to the housing
        a_ = B.add(B.add(F, uf, fk["lower_yoke"] + 20), nb, -30)
        b_ = B.add(B.add(F, uf, fk["lower_yoke"] + 120), nb, -30)
        for n_, q_ in enumerate((a_, b_)):
            B.smooth_tube(f"lampstay{sg}{n_}", [(q_[0], sg * 76, q_[1]), (q_[0] + 22, sg * 70, org[1] + (q_[1] - org[1]) * 0.5), (org[0] - 70, sg * hy * 0.74, org[1] + (10 if n_ else -10))], 5, B.mat("alu_dark"))
    # the clear cover: a shallow dome over the whole face
    rings("lampcover", org, [(4, 0.9, 0), (11, 0.84, 0), (17, 0.66, 0), (20, 0.4, 0), (21, 0.12, 0)], glass_mat(), hy, hz, close=(False, True))
    display(model, c, head, house)


def round_lamp(model, c):
    """A classic round headlamp (cafe, classic): a painted bowl, a rolled chrome rim, a deep chrome reflector
    with a projector in the middle behind a domed clear glass, and a bracket each side clamped to the fork."""
    import shapes
    head = [(i, s_) for i, s_ in enumerate(model.get("front_end", [])) if s_.get("part") == "head"]
    bowl_ = next((s_ for i, s_ in head if s_.get("round")), None)
    if not bowl_:
        return
    for i, s_ in head:
        if "pts" in s_:
            drop(f"front{i}", f"lens{i}", f"bezel{i}", f"reflector{i}", f"eye{i}", f"eyeglass{i}")
    drop("lamprim", "lampglass")
    bowl = shapes.dense(bowl_["pts"], True, bowl_.get("t", 1.0))
    x0, x1 = min(q[0] for q in bowl), max(q[0] for q in bowl)
    zc = (min(q[1] for q in bowl) + max(q[1] for q in bowl)) / 2
    R = (max(q[1] for q in bowl) - min(q[1] for q in bowl)) / 2
    chrome, dark = B.mat("chrome"), B.mat("plastic")
    fx = _cast(_trees(("forktube", "forkouter")), 105, zc, "x", 1)
    twin = any(s_.get("twin") for i, s_ in head)
    for yl in ((-R - 6, R + 6) if twin else (0,)):
        o, ax = (x1, yl, zc), (1, 0, 0)
        lathe(f"lamprim{yl}", o, ax, [(-9, R + 0.5), (-9, R + 4.5), (-2, R + 6.5), (6, R + 6.5), (11, R + 4), (12, R), (9, R - 4), (2, R - 5), (2, R + 0.5), (-9, R + 0.5)], chrome, 64, sharp=False, caps=False)
        lathe(f"lampreflector{yl}", o, ax, [(3, R - 5), (-8, R * 0.86), (-24, R * 0.66), (-38, R * 0.42), (-46, R * 0.24), (-48, 0)], chrome, 64, sharp=False, caps=False)
        lathe(f"lampshield{yl}", o, ax, [(-46, 0), (-46, R * 0.25), (-20, R * 0.25), (-12, R * 0.2), (-8, R * 0.1), (-7, 0)], surf("smokechrome", (0.2, 0.2, 0.22), 1.0, 0.1, wear=0.0), 32, sharp=False, caps=False)
        lathe(f"lampdrl{yl}", o, ax, [(2.5, R - 6), (2.5, R - 10), (4, R - 10), (4, R - 6), (2.5, R - 6)], glow("drl", (1.0, 0.97, 0.9), 1.6), 64, caps=False)
        lathe(f"lampglass{yl}", o, ax, [(5, R - 4.5), (11, R * 0.9), (17, R * 0.66), (21, R * 0.36), (22.5, 0)], glass_mat(), 64, sharp=False, caps=False)
    if fx is not None and not twin:
        # the brackets: a flat arm from each side of the bowl back to a split clamp on the fork leg
        xb, xf_ = x0 + 0.62 * (x1 - x0), fx - 31
        d = (-c["uf"][0], 0, -c["uf"][1]) if c["uf"][1] < 0 else (c["uf"][0], 0, c["uf"][1])
        for s in (-1, 1):
            B.prism(f"lampear{s}", [(xb + 16, zc - 15), (xb + 16, zc + 15), (xb - 10, zc + 20), (xf_ - 8, zc + 24), (xf_ - 8, zc - 24), (xb - 10, zc - 20)], s * (R - 3) if s > 0 else s * 72, s * 72 if s > 0 else s * (R - 3), dark, 2.0)
            lathe(f"lampearclamp{s}", (xf_, s * 105, zc), d, [(-16, 31.5), (-16, 36.5), (16, 36.5), (16, 31.5), (-16, 31.5)], dark, 36, caps=False)
            B.prism(f"lampearweb{s}", [(xf_ - 30, zc - 14), (xf_ - 30, zc + 14), (xf_ + 6, zc + 14), (xf_ + 6, zc - 14)], s * 70 if s > 0 else s * 78, s * 78 if s > 0 else s * 70, dark, 2.0)
            bolt(f"lampearbolt{s}", (xb, s * 73, zc), (0, s, 0), 6.5, 4, chrome)


def clocks(model, c):
    """Round clocks (cafe, classic): a black face with white minute marks and bolder numerals' marks round a
    270 degree sweep, an orange needle on a chrome hub, behind flat glass in the chrome rim. Left is the
    speedometer, right the rev counter with its red band."""
    d_ = (-0.45 / math.hypot(0.45, 0.89), 0.89 / math.hypot(0.45, 0.89))
    nb = (d_[1], -d_[0])
    white = glow("dialwhite", (0.9, 0.9, 0.86), 0.5)
    for i, s_ in enumerate(model.get("front_end", [])):
        if not (s_.get("circle") and s_.get("part") == "head" and s_.get("cls") == "plastic"):
            continue
        drop(f"clockface{i}", f"clocktick{i}", f"clockneedle{i}", f"clockhub{i}", f"clockglass{i}")
        r_, c_ = s_["circle"] * 1.2, s_["at"]
        for s in (-1, 1):
            yc = s * 46
            org = (c_[0] + d_[0] * 24.4, c_[1] + d_[1] * 24.4)
            # +y is the rider's left, so the sweep runs clockwise as the rider reads it
            rot = lambda rad, a, w, r0, r1: [(yc - r0 * math.sin(a) + w * math.cos(a), r0 * math.cos(a) + w * math.sin(a)), (yc - r1 * math.sin(a) + w * math.cos(a), r1 * math.cos(a) + w * math.sin(a)),
                                             (yc - r1 * math.sin(a) - w * math.cos(a), r1 * math.cos(a) - w * math.sin(a)), (yc - r0 * math.sin(a) - w * math.cos(a), r0 * math.cos(a) - w * math.sin(a))]
            slab(f"clockface{i}{s}", org, d_, nb, [(yc + r_ * math.cos(2 * math.pi * k / 40), r_ * math.sin(2 * math.pi * k / 40)) for k in range(40)], 0, 0.4, surf("dialblack", (0.012, 0.012, 0.013), 0.0, 0.6, wear=0.0), 0)
            for k in range(28):
                a = math.radians(-135 + 270 * k / 27)
                big = k % 3 == 0
                red = s < 0 and k >= 23
                slab(f"clocktick{i}{s}{k}", org, d_, nb, rot(0, a, 0.9 if big else 0.45, r_ * (0.72 if big else 0.82), r_ * 0.93), 0.4, 0.7, glow("dialred", (1.0, 0.05, 0.03), 0.8) if red else white, 0)
            a = math.radians(-135 + 270 * (0.0 if s > 0 else 0.08))
            slab(f"clockneedle{i}{s}", org, d_, nb, rot(0, a, 1.1, -r_ * 0.18, r_ * 0.84), 1.6, 2.2, glow("needle", (1.0, 0.32, 0.02), 1.2), 0)
            lathe(f"clockhub{i}{s}", (org[0], yc, org[1]), (d_[0], 0, d_[1]), [(0.4, 0), (0.4, r_ * 0.16), (3.0, r_ * 0.14), (3.4, 0)], B.mat("chrome"), 24)
            lathe(f"clockglass{i}{s}", (org[0], yc, org[1]), (d_[0], 0, d_[1]), [(4.2, 0), (4.2, r_ * 1.02), (5.0, r_ * 1.02), (5.0, 0)], glass_mat(), 40)


def display(model, c, head, house, dash=None):
    """The display: a slim tablet on a bracket, its face square to the rider's eyes."""
    F, uf, nb, fk = c["F"], c["uf"], c["nb"], c["fk"]
    plastic = B.mat("plastic")
    # the display: a slim tablet on a bracket, its face square to the rider's eyes
    dash = dash or next((s_ for i, s_ in head if s_ is not house and s_.get("cls") == "plastic" and "pts" in s_), None)
    top = c["top"]
    dc = (sum(q[0] for q in dash["pts"]) / len(dash["pts"]), sum(q[1] for q in dash["pts"]) / len(dash["pts"])) if dash else (top[0] + 60, top[1] + 40)
    eye = (c["model_hip"] + 80, 1560)
    L_ = math.hypot(eye[0] - dc[0], eye[1] - dc[1])
    n = ((eye[0] - dc[0]) / L_, (eye[1] - dc[1]) / L_)
    u = (n[1], -n[0]) if n[1] > 0 else (-n[1], n[0])
    if u[1] < 0:
        u = (-u[0], -u[1])
    face = [(y, z) for y, z in sell(78, 46, 1.0, 5.0, 48)]
    slab("dashbody", dc, n, u, face, -20, 0, plastic, 5.0)
    slab("dashglass", dc, n, u, [(y * 0.9, z * 0.86) for y, z in face], -1, 0.8, surf("screen", (0.004, 0.005, 0.008), 0.0, 0.04, coat=1.0, wear=0.0), 0)
    slab("dashrev", dc, n, u, [(-56, 22), (30, 22), (30, 27), (-56, 27)], 0.6, 1.1, glow("rev", (0.2, 0.75, 1.0), 2.5), 0)
    slab("dashrevred", dc, n, u, [(34, 22), (56, 22), (56, 27), (34, 27)], 0.6, 1.1, glow("revred", (1.0, 0.15, 0.05), 2.5), 0)
    back = (dc[0] - n[0] * 20, dc[1] - n[1] * 20)
    yk = B.add(B.add(F, uf, fk["length"] - 12), nb, -34)
    for sg in (-1, 1):
        B.smooth_tube(f"dashstay{sg}", [(yk[0], sg * 40, yk[1]), ((yk[0] + back[0]) / 2 + 6, sg * 36, (yk[1] + back[1]) / 2), (back[0], sg * 30, back[1])], 5.5, B.mat("alu_dark"))


def _trees(prefixes):
    from mathutils.bvhtree import BVHTree
    dg = bpy.context.evaluated_depsgraph_get()
    out = []
    for o in bpy.data.objects:
        if o.type != "MESH" or not o.name.startswith(tuple(prefixes)):
            continue
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        mw = ev.matrix_world
        out.append(BVHTree.FromPolygons([mw @ v.co for v in me.vertices], [tuple(p.vertices) for p in me.polygons]))
        ev.to_mesh_clear()
    return out


def _cast(trees, a, b, axis, s):
    """The outermost point (mm along the axis) where a ray from outside meets the parts. axis "x": a ray from
    the front at y = a, z = b. axis "y": a ray from side s at x = a, z = b."""
    from mathutils import Vector
    org, d = (Vector((4.0, a * B.MM, b * B.MM)), Vector((-1, 0, 0))) if axis == "x" else (Vector((a * B.MM, s * 0.8, b * B.MM)), Vector((0, -s, 0)))
    best = None
    for t in trees:
        loc = t.ray_cast(org, d)[0]
        if loc is not None:
            v = loc.x if axis == "x" else loc.y * s
            best = v if best is None or v > best else best
    return None if best is None else best / B.MM


def side_hit(prefixes, x, z, s):
    """Where a ray from the side (s = 1 left, -1 right) at (x, z) first meets a built part whose name starts
    with one of the prefixes: (x, y, z) in mm, or None."""
    v = _cast(_trees(prefixes), x, z, "y", s)
    return None if v is None else (x, s * v, z)


def skin_patch(name, trees, outline, material, lift=1.5, axis="x", s=1):
    """A thin patch lying on the surface of built parts (a lamp let into a nose, a flush indicator): the
    outline is drawn square-on (y, z from the front, or x, z from the side) and every point of the patch is
    dropped onto the surface, then lifted off it by a hair so it reads as its own part."""
    outline = chaikin(outline, 2)
    m = len(outline)
    ca, cb = sum(q[0] for q in outline) / m, sum(q[1] for q in outline) / m
    verts, faces, last = [], [], None
    for k in (1.0, 0.75, 0.5, 0.25):
        for a_, b_ in outline:
            a, b = ca + (a_ - ca) * k, cb + (b_ - cb) * k
            v = _cast(trees, a, b, axis, s)
            last = v if v is not None else last
            if last is None:
                return None
            verts.append(B.V(last + lift, a, b) if axis == "x" else B.V(a, s * (last + lift), b))
    v = _cast(trees, ca, cb, axis, s) or last
    verts.append(B.V(v + lift, ca, cb) if axis == "x" else B.V(ca, s * (v + lift), cb))
    for r in range(3):
        for i_ in range(m):
            i2 = (i_ + 1) % m
            faces.append((r * m + i_, r * m + i2, (r + 1) * m + i2, (r + 1) * m + i_))
    faces += [(3 * m + i_, 3 * m + (i_ + 1) % m, 4 * m) for i_ in range(m)]
    ob = B.mesh_obj(name, verts, faces, material, True, 0, False)
    B.bm_fix_normals(ob)
    return ob


def nose_lamps(model, c):
    """Sport lamps: a slim slanted eye each side let flush into the nose (reference: modern sport fairings).
    A gloss black surround, a smoked lens, a bright running-light blade along the top and two projector eyes."""
    lens = next((v for v in model["body"].get("panels", []) if v.get("cls") == "lens"), None)
    trees = _trees(("cowl",))
    if not lens or not trees:
        return False
    dz = sum(q[1] for q in lens["pts"]) / len(lens["pts"]) - 790
    black = surf("lampsurround", (0.012, 0.012, 0.014), 0.0, 0.12, coat=1.0, wear=0.0)
    smoke = surf("lampsmoke", (0.05, 0.055, 0.065), 1.0, 0.06, wear=0.0)
    ring = surf("bezel", (0.05, 0.05, 0.055), 0.85, 0.3)
    eye = [(26, 772), (60, 768), (104, 786), (124, 806), (112, 812), (70, 800), (30, 790)]
    ey, ez = sum(q[0] for q in eye) / len(eye), sum(q[1] for q in eye) / len(eye)
    drl = [(34, 787), (70, 797), (108, 808), (110, 804.5), (72, 793), (36, 783.5)]
    made = False
    for s in (-1, 1):
        # the brand's signature: long slanted eyes that sweep out and up along the nose's own lines
        g = lambda y, z: (22 + (y - 26) * 1.34, 770 + (z - 772) * 1.3)
        f = lambda pts, k=1.0: [(s * g(ey + (y - ey) * k, ez + (z - ez) * k)[0], g(ey + (y - ey) * k, ez + (z - ez) * k)[1] + dz) for y, z in pts]
        if not skin_patch(f"noselamp{s}", trees, f(eye, 1.16), black, 1.0):
            continue
        made = True
        skin_patch(f"noselens{s}", trees, f(eye, 0.94), smoke, 1.8)
        skin_patch(f"nosedrl{s}", trees, f(drl), glow("drl", (0.9, 0.95, 1.0), 4.0), 2.6)
        for n_, (py, pz) in enumerate(((56, 779), (86, 789))):
            py, pz = g(py, pz)
            circ = lambda r: [(s * py + r * math.cos(2 * math.pi * k / 10), pz + dz + r * math.sin(2 * math.pi * k / 10)) for k in range(10)]
            skin_patch(f"noseeye{s}{n_}", trees, circ(10.5), ring, 2.6)
            skin_patch(f"noseeyeglass{s}{n_}", trees, circ(8.2), glass_mat(), 3.0)
            skin_patch(f"noseeyeled{s}{n_}", trees, circ(3.2), glow("bulb", (1.0, 0.97, 0.9), 2.0), 3.3)
    return made


def tail_lamp(model, c):
    """The brake light (beenster, sport rounds 1 and 2): let into the square-cut end of the tail and facing
    straight back, two square LED units side by side behind one red lens. The rear indicators grow out of
    the same housing, one each side, so the bike is complete with the number plate hanger taken off."""
    tl = next((v for v in model["body"].get("tail", []) if v.get("cls") == "redlens"), None)
    if not tl or model["family"].lower() not in ("sport", "cafe"):
        return
    zs = [q[1] for q in tl["pts"]]
    xf, zc, hh = max(q[0] for q in tl["pts"]), (max(zs) + min(zs)) / 2, (max(zs) - min(zs)) / 2
    yw = side_hit(("tail",), xf + 10, zc, 1)
    W = max(34.0, (abs(yw[1]) if yw is not None else 56.0) - 7)
    org, n, u = (xf, zc), (-1.0, 0.0), (0.0, 1.0)
    drop("taillight")
    slab("taillamphouse", org, n, u, sell(W + 3, hh + 2.5, 1.0, 3.6, 40), -30, 1.2, B.mat("plastic"), 1.0)
    lensm = surf("taillens", (0.42, 0.006, 0.008), 0.0, 0.08, coat=1.0, wear=0.0)
    lensm.node_tree.nodes["Principled BSDF"].inputs["Emission Color"].default_value = (1.0, 0.02, 0.02, 1)
    lensm.node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = 0.25
    slab("taillamplens", org, n, u, sell(W, hh, 1.0, 3.6, 40), 1.0, 4.2, lensm, 1.2)
    for sg in (-1, 1):
        # two square light units: a bright frame round a darker middle, as LED tail lamps show
        slab(f"taillampled{sg}", org, n, u, [(sg * W * 0.5 + y, z) for y, z in sell(W * 0.38, hh - 4.5, 1.0, 4.0, 28)], 4.0, 4.7, glow("tailred", (1.0, 0.03, 0.02), 2.6), 0)
        slab(f"taillampcore{sg}", org, n, u, [(sg * W * 0.5 + y, z) for y, z in sell(W * 0.27, hh - 8.5, 1.0, 4.0, 24)], 4.6, 5.1, lensm, 0)
    c["tail_lamp"] = (xf, zc, W)


def guard_lamp(model, c):
    """The classic and cruiser brake light: a polished oval housing standing on the rear mudguard, its red
    lens domed and facing straight back, a small clear window underneath to light the plate. The indicators
    grow out of the housing's sides (the same rule as the sport and cafe tails)."""
    if model["family"].lower() not in ("classic", "cruiser"):
        return
    tl = next((v for v in model["body"].get("tail", []) if v.get("cls") == "redlens"), None)
    if not tl:       # a bobbed guard drawn with the panels carries its lamp there
        pi = next((i for i, v in enumerate(model["body"].get("panels", [])) if v.get("cls") == "redlens"), None)
        if pi is None:
            return
        tl = model["body"]["panels"][pi]
        drop(f"panel{pi + len(model['body'].get('front_guard', []))}")      # build3d numbers the panels after the front guard's shapes
    xs, zs = [q[0] for q in tl["pts"]], [q[1] for q in tl["pts"]]
    zc, x0, x1 = (max(zs) + min(zs)) / 2 + 4, max(xs) + 26, min(xs) - 4
    L, R, e = x0 - x1, 36.0, (0.72, 1.0)
    drop("taillight", "guardlamp")
    ax = (-1.0, 0.0, 0.0)
    lathe("guardlampbody", (x0, 0, zc), ax, [(0, 0), (0, R * 0.42), (L * 0.3, R * 0.8), (L * 0.75, R), (L - 3, R), (L, R - 1.2), (L, 0)], B.mat("chrome"), 48, sharp=False, ell=e)
    lathe("guardlamprim", (x1, 0, zc), ax, [(-2, R - 0.5), (-2, R + 1.6), (1.5, R + 2.2), (4.5, R + 1.2), (5, R - 3), (-2, R - 3)], B.mat("chrome"), 48, sharp=False, ell=e, caps=False)
    lensm = surf("taillens", (0.42, 0.006, 0.008), 0.0, 0.08, coat=1.0, wear=0.0)
    lensm.node_tree.nodes["Principled BSDF"].inputs["Emission Color"].default_value = (1.0, 0.02, 0.02, 1)
    lensm.node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = 0.25
    lathe("guardlamplens", (x1, 0, zc), ax, [(0, 0), (0, R - 2.6), (5, R - 3), (10, R - 7), (14, R - 16), (16, R - 28), (16.5, 0)], lensm, 48, sharp=False, ell=e)
    lathe("guardlampbulb", (x1, 0, zc), ax, [(1, 0), (1, R * 0.36), (9, R * 0.3), (11, 0)], glow("tailred", (1.0, 0.03, 0.02), 2.6), 24, sharp=False, ell=e)
    # the plate window: a clear strip let into the underside of the housing, lighting down on to the plate
    slab("guardlampwindow", (x1 + L * 0.2, zc - R * e[0] + 1.2), (0.0, -1.0), (1.0, 0.0), [(-20, -9), (20, -9), (20, 9), (-20, 9)], -1.5, 1.6, glow("bulb", (1.0, 0.97, 0.9), 1.2), 0)
    c["tail_lamp"] = (x1 + L * 0.38 - 13, zc, R * 0.9 + 14)
    c["tail_lamp_chrome"] = True


def plate_mount(model, c):
    """The number plate arm's fixing (beenster, sport round 2): a cast foot bolted flat to the underside of
    the tail, with a web each side running down on to the arm, so the arm is plainly held."""
    body = model["body"]
    arm = next((h for h in body.get("plate_hanger", []) if h.get("cls") == "plastic" and "pts" in h), None)
    tail = body.get("tail", [None])[0]
    if not arm or not tail or model["family"].lower() not in ("sport", "cafe") or not model.get("fit", {}).get("plate_hanger", True):
        return
    import shapes
    drop("platemount")
    top = max(arm["pts"], key=lambda q: q[0])
    tp = shapes.dense(tail["pts"], True, tail.get("t", 1.0))
    under = lambda x: shapes.top_bottom(tp, x)[1]
    x0 = top[0] - 12
    d = (-60.0, under(x0 - 60) - under(x0))
    L_ = math.hypot(*d)
    al = (d[0] / L_, d[1] / L_)                    # along the underside, towards the rear
    dn = (al[1], -al[0]) if -al[0] < 0 else (-al[1], al[0])
    if dn[1] > 0:
        dn = (-dn[0], -dn[1])
    org = (x0, under(x0))
    plastic = B.mat("plastic")
    slab("platemount_foot", org, dn, al, sell(46, 66, 1.0, 5.0, 40), -9, 9, plastic, 2.5)
    for k, (y, t) in enumerate(((-33, -50), (33, -50), (-33, 50), (33, 50))):
        p = (org[0] + al[0] * t + dn[0] * 9, y, org[1] + al[1] * t + dn[1] * 9)
        lathe(f"platemount_washer{k}", p, (dn[0], 0, dn[1]), [(0, 0), (0, 8.5), (1.4, 8.5), (1.4, 0)], B.mat("steel"), 20)
        bolt(f"platemount_bolt{k}", (p[0] + dn[0] * 1.4, y, p[2] + dn[1] * 1.4), (dn[0], 0, dn[1]), 5.0, 4.2, B.mat("steel"))
    ap = shapes.dense(arm["pts"], True, arm.get("t", 0))
    xa = x0 - 150
    tb = shapes.top_bottom(ap, xa)
    if tb:
        r_ = (org[0] + al[0] * 62 + dn[0] * 8, org[1] + al[1] * 62 + dn[1] * 8)
        f_ = (org[0] - al[0] * 20 + dn[0] * 8, org[1] - al[1] * 20 + dn[1] * 8)
        for s in (-1, 1):
            B.prism(f"platemount_web{s}", [r_, f_, (xa + 60, shapes.top_bottom(ap, xa + 60)[0] - 6), (xa, tb[0] - 3)], s * 15 - 4, s * 15 + 4, plastic, 2.0)


def bubble_screen(model, c):
    """The screen (beenster, sport round 1): thin clear Perspex, round at the bottom where it meets the nose,
    its sides swept back, a low bubble over the clocks."""
    sc_ = next((v for v in model["body"].get("panels", []) if v.get("cls") == "screen"), None)
    if not sc_ or model["family"].lower() != "sport":
        return
    pts = [tuple(q[:2]) for q in sc_["pts"]]
    lo = max(pts, key=lambda q: q[0])                      # where the screen leaves the nose
    hi = max(pts, key=lambda q: q[1])                      # its top edge
    mid = max((q for q in pts if q not in (lo, hi)), key=lambda q: q[0] + q[1])
    drop("screen", "pscreen")

    def centre(t):                                         # the leading line, a quadratic through the three
        a_, b_ = (1 - t) ** 2, 2 * t * (1 - t)
        ctrl = (2 * mid[0] - (lo[0] + hi[0]) / 2, 2 * mid[1] - (lo[1] + hi[1]) / 2)
        return (a_ * lo[0] + b_ * ctrl[0] + t * t * hi[0], a_ * lo[1] + b_ * ctrl[1] + t * t * hi[1])
    W, NT, NY = 128.0, 26, 24
    verts, faces = [], []
    for i in range(NT + 1):
        t = i / NT
        cx_, cz_ = centre(t)
        # round at the bottom (a half circle in front view), then easing in to a rounded top
        w = W * max(0.0, 1 - (1 - min(1.0, t / 0.17)) ** 3.4) ** (1 / 3.4) * (1 - 0.34 * max(0.0, (t - 0.17) / 0.83) ** 2)
        w *= math.sqrt(max(0.0, 1 - max(0.0, (t - 0.9) / 0.1) ** 2 * 0.75))
        for j in range(NY + 1):
            f = -1 + 2 * j / NY
            y = f * max(w, 0.5)
            back = (40 + 90 * t) * (y / W) ** 2        # wrapped round the nose low down, swept back higher up
            crown = 10 * (1 - f * f) * math.sin(math.pi * t)         # a low bubble
            verts.append(B.V(cx_ - back + crown * 0.5, y, cz_ - 0.25 * back + crown * 0.4))
    for i in range(NT):
        for j in range(NY):
            q = i * (NY + 1) + j
            faces.append((q, q + 1, q + NY + 2, q + NY + 1))
    ob = B.mesh_obj("screen", verts, faces, glass_mat("perspex", (0.82, 0.86, 0.9)), True, 0, False)
    md = ob.modifiers.new("thick", "SOLIDIFY")
    md.thickness = 2.6 * B.MM
    B.bm_fix_normals(ob)
    # four screen screws in rubber wellnuts along the lower edge
    for sg in (-1, 1):
        for t in (0.16, 0.36):
            cx_, cz_ = centre(t)
            w = W * max(0.0, 1 - (1 - min(1.0, t / 0.17)) ** 3.4) ** (1 / 3.4) * 0.9
            back = (40 + 90 * t) * (w / W) ** 2
            q = (cx_ - back + 3, sg * w, cz_ - 0.25 * back + 2)
            B.cone(f"screenscrew{sg}{t}", q, (q[0] + 2, q[1] + sg * 3, q[2] + 1), 4.2, 3.6, B.mat("plastic"), 12)


def chain(model, c):
    """A real drive chain (beenster, 15 Nov): rollers on pins at a 5/8 inch pitch, waisted inner and outer
    side plates in turn, round both sprockets, and sprockets with teeth and lightening holes."""
    sw, eng = model["swingarm"], model["engine"]
    if sw.get("shaft") or "sprocket_r" not in sw:
        return
    A, cs = c["A"], tuple(eng["countershaft"])
    PITCH, Y = 15.875, 108.0
    n_r = max(30, round(2 * math.pi * sw["sprocket_r"] / PITCH))
    n_f = 15
    rr, rf = PITCH / (2 * math.sin(math.pi / n_r)), PITCH / (2 * math.sin(math.pi / n_f))
    drop("chain", "sprocket")
    steel, dark = B.mat("steel"), surf("chainplate", (0.16, 0.16, 0.17), 1.0, 0.38, wear=0.3)
    gold = surf("chaingold", (0.72, 0.5, 0.16), 1.0, 0.3, wear=0.25)
    # sprockets
    for tag, cen, R, n, ya, yb in (("sprocket", A, rr, n_r, Y - 3.2, Y + 3.2), ("sprocketfront", cs, rf, n_f, Y - 3.2, Y + 3.2)):
        out = []
        for k in range(n):
            for da, dr in ((-0.5, -5.2), (-0.28, -4.6), (-0.12, 1.5), (0.0, 3.6), (0.12, 1.5), (0.28, -4.6)):
                w = 2 * math.pi * (k + 0.5 + da) / n
                out.append((cen[0] + (R + dr) * math.cos(w), cen[1] + (R + dr) * math.sin(w)))
        ob = B.prism(tag, out, ya, yb, steel if tag == "sprocket" else dark, 0)
        if tag == "sprocket":
            cuts = []
            for k in range(6):
                w = 2 * math.pi * k / 6
                for dw, rad_, r_ in ((0.0, R * 0.7, R * 0.13), (0.5, R * 0.58, R * 0.075)):
                    q = (cen[0] + rad_ * math.cos(w + dw * math.pi / 3), cen[1] + rad_ * math.sin(w + dw * math.pi / 3))
                    cuts.append(((q[0], ya - 4, q[1]), (q[0], yb + 4, q[1]), r_))
            drill(ob, cuts, 0)
            ob.data.polygons.foreach_set("use_smooth", [False] * len(ob.data.polygons))
            for k in range(6):
                w = 2 * math.pi * (k + 0.5) / 6
                bolt(f"sprocketnut{k}", (cen[0] + R * 0.36 * math.cos(w), yb + 1, cen[1] + R * 0.36 * math.sin(w)), (0, 1, 0), 6.5, 6, steel, False)
    # the loop: top run, round the rear sprocket, bottom run, round the gearbox sprocket
    d = math.hypot(A[0] - cs[0], A[1] - cs[1])
    th = math.atan2(A[1] - cs[1], A[0] - cs[0])
    al = math.acos((rf - rr) / d)
    path = []
    u1 = (math.cos(th + al), math.sin(th + al))
    u2 = (math.cos(th - al), math.sin(th - al))
    p0, p1 = B.add(cs, u1, rf), B.add(A, u1, rr)
    for k in range(200):
        path.append((p0[0] + (p1[0] - p0[0]) * k / 200, p0[1] + (p1[1] - p0[1]) * k / 200))
    for k in range(200):
        w = th + al - 2 * al * k / 200
        path.append((A[0] + rr * math.cos(w), A[1] + rr * math.sin(w)))
    p0, p1 = B.add(A, u2, rr), B.add(cs, u2, rf)
    for k in range(200):
        path.append((p0[0] + (p1[0] - p0[0]) * k / 200, p0[1] + (p1[1] - p0[1]) * k / 200))
    for k in range(120):
        w = th - al - (2 * math.pi - 2 * al) * k / 120
        path.append((cs[0] + rf * math.cos(w), cs[1] + rf * math.sin(w)))
    cum = [0.0]
    for a_, b_ in zip(path, path[1:] + path[:1]):
        cum.append(cum[-1] + math.hypot(b_[0] - a_[0], b_[1] - a_[1]))
    total = cum[-1]
    n = 2 * round(total / PITCH / 2)
    step, pins, j = total / n, [], 0
    for k in range(n):
        target = k * step
        while cum[j + 1] < target:
            j += 1
        f = (target - cum[j]) / max(1e-9, cum[j + 1] - cum[j])
        a_, b_ = path[j], path[(j + 1) % len(path)]
        pins.append((a_[0] + (b_[0] - a_[0]) * f, a_[1] + (b_[1] - a_[1]) * f))
    groups = {"chainrollers": ([], []), "chaininner": ([], []), "chainouter": ([], []), "chainpins": ([], [])}

    def cyl(key, q, r, y0, y1, m=10):
        vs, fs = groups[key]
        b0 = len(vs)
        for yy in (y0, y1):
            vs += [B.V(q[0] + r * math.cos(2 * math.pi * e / m), yy, q[1] + r * math.sin(2 * math.pi * e / m)) for e in range(m)]
        fs += [(b0 + e, b0 + (e + 1) % m, b0 + m + (e + 1) % m, b0 + m + e) for e in range(m)]
        fs += [tuple(b0 + e for e in range(m))[::-1], tuple(b0 + m + e for e in range(m))]

    def plate(key, a_, b_, y0, y1, r=7.4, waist=5.0):
        vs, fs = groups[key]
        L_ = math.hypot(b_[0] - a_[0], b_[1] - a_[1])
        t_ = ((b_[0] - a_[0]) / L_, (b_[1] - a_[1]) / L_)
        nn = (-t_[1], t_[0])
        ring = []
        for e in range(7):                                  # round one end
            w = math.pi / 2 + math.pi * e / 6
            ring.append((a_[0] + r * (math.cos(w) * t_[0] + math.sin(w) * nn[0]), a_[1] + r * (math.cos(w) * t_[1] + math.sin(w) * nn[1])))
        mid_ = ((a_[0] + b_[0]) / 2, (a_[1] + b_[1]) / 2)
        ring.append((mid_[0] - nn[0] * waist, mid_[1] - nn[1] * waist))
        for e in range(7):                                  # round the other
            w = -math.pi / 2 + math.pi * e / 6
            ring.append((b_[0] + r * (math.cos(w) * t_[0] + math.sin(w) * nn[0]), b_[1] + r * (math.cos(w) * t_[1] + math.sin(w) * nn[1])))
        ring.append((mid_[0] + nn[0] * waist, mid_[1] + nn[1] * waist))
        m, b0 = len(ring), len(vs)
        for yy in (y0, y1):
            vs += [B.V(q[0], yy, q[1]) for q in ring]
        fs += [(b0 + e, b0 + (e + 1) % m, b0 + m + (e + 1) % m, b0 + m + e) for e in range(m)]
        fs += [tuple(b0 + e for e in range(m))[::-1], tuple(b0 + m + e for e in range(m))]
    for k, q in enumerate(pins):
        nx = pins[(k + 1) % n]
        cyl("chainrollers", q, 5.1, Y - 4.6, Y + 4.6)
        cyl("chainpins", q, 2.6, Y - 11.2, Y + 11.2, 8)
        if k % 2 == 0:
            for sg in (-1, 1):
                plate("chaininner", q, nx, Y + sg * 4.8, Y + sg * 6.8)
        else:
            for sg in (-1, 1):
                plate("chainouter", q, nx, Y + sg * 7.2, Y + sg * 9.2)
    for key, material in (("chainrollers", steel), ("chaininner", dark), ("chainouter", gold), ("chainpins", steel)):
        vs, fs = groups[key]
        ob = B.mesh_obj(key, vs, fs, material, False)
        B.bm_fix_normals(ob)


def grip_mat(length, rmax, rbody=17.2):
    """Grip rubber (reference: hand grip, round 4): a diamond knurl of small pyramids pressed in all the way
    round and along, broken by moulded slots (short wide ones with a grey insert, long thin diagonal
    grooves), with smooth rubber at the flange and on the end collar. Drawn as a height picture of the
    unrolled grip, in millimetres, and laid on with the grip's own skin layout."""
    import numpy as np
    key = "d3_grip"
    if key in bpy.data.materials:
        return bpy.data.materials[key]
    W_, H_ = 1024, 2048
    circ = 2 * math.pi * rbody
    u = np.linspace(0, circ, W_, endpoint=False)[None, :]
    v = np.linspace(0, length, H_)[:, None]
    p_ = circ / round(circ / 2.7)

    def tri(x):
        return 1 - np.abs(2 * (x % 1.0) - 1)
    kn = np.minimum(tri((u + v) / p_), tri((u - v) / p_))

    def capsule(cu, cv, ang, hl):
        du = ((u - cu + circ / 2) % circ) - circ / 2
        dv = v - cv
        ca, sa = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        al = np.clip(du * sa + dv * ca, -hl, hl)       # along the slot (ang measured from the grip's axis)
        return np.hypot(du - al * sa, dv - al * ca)
    d_in = np.full((H_, W_), 1e3)
    d_gr = np.full((H_, W_), 1e3)
    pu, pv = circ / 3, 34.0
    for j_ in range(3):
        cv = 20.0 + (j_ % 2) * pv / 2
        while cv < length - 26:
            d_in = np.minimum(d_in, capsule(pu * (j_ + 0.22), cv, 14, 6.5))
            if cv + pv / 2 < length - 28:
                d_gr = np.minimum(d_gr, capsule(pu * (j_ + 0.7), cv + pv / 2, -32, 11.5))
            cv += pv
    hw_in, hw_gr = 2.5, 1.0
    flat = np.clip((d_in - hw_in) / 1.3, 0, 1) * np.clip((d_gr - hw_gr) / 1.0, 0, 1)
    flat = flat * np.clip((v - 10.5) / 1.0, 0, 1) * np.clip((length - 15.5 - v) / 1.0, 0, 1) * np.ones_like(u)
    h = 0.45 + 0.55 * kn * flat
    h = h - 0.4 * (1 - np.clip((d_in - hw_in + 0.5) / 0.5, 0, 1)) - 0.45 * (1 - np.clip((d_gr - hw_gr + 0.4) / 0.4, 0, 1))
    ins = 1 - np.clip((d_in - hw_in + 0.7) / 0.3, 0, 1)
    px = np.zeros((H_, W_, 4), dtype=np.float32)
    px[..., 0], px[..., 1], px[..., 3] = h, ins, 1.0
    img = bpy.data.images.new("d3_grip_height", W_, H_, alpha=False, float_buffer=True)
    img.colorspace_settings.name = "Non-Color"
    img.pixels.foreach_set(px.ravel())
    img.pack()
    m = bpy.data.materials.new(key)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    uvn, mp, tex, sep = nt.nodes.new("ShaderNodeUVMap"), nt.nodes.new("ShaderNodeMapping"), nt.nodes.new("ShaderNodeTexImage"), nt.nodes.new("ShaderNodeSeparateColor")
    uvn.uv_map = "skin"
    mp.inputs["Scale"].default_value = (1 / (2 * math.pi * rmax), 1 / length, 1)
    tex.image, tex.interpolation = img, "Cubic"
    nt.links.new(uvn.outputs["UV"], mp.inputs["Vector"])
    nt.links.new(mp.outputs["Vector"], tex.inputs["Vector"])
    nt.links.new(tex.outputs["Color"], sep.inputs[0])
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.inputs["A"].default_value, mix.inputs["B"].default_value = (0.014, 0.014, 0.016, 1), (0.2, 0.2, 0.21, 1)
    nt.links.new(sep.outputs[1], mix.inputs["Factor"])
    nt.links.new(mix.outputs["Result"], b.inputs["Base Color"])
    b.inputs["Roughness"].default_value = 0.74
    nz = nt.nodes.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 2500.0
    ad = nt.nodes.new("ShaderNodeMath")
    ad.operation = "MULTIPLY_ADD"
    ad.inputs[1].default_value = 0.06
    nt.links.new(nz.outputs["Fac"], ad.inputs[0])
    nt.links.new(sep.outputs[0], ad.inputs[2])
    bp = nt.nodes.new("ShaderNodeBump")
    bp.inputs["Strength"].default_value, bp.inputs["Distance"].default_value = 1.0, 0.0011
    nt.links.new(ad.outputs[0], bp.inputs["Height"])
    nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    return m


def drill(ob, cuts, bevel=0.0):
    """Cut round holes through a part: cuts are (from, to, radius) in mm. The cutters stay in the file, hidden."""
    for k_, (q0, q1, r) in enumerate(cuts):
        cu = B.cone(f"cut_{ob.name}_{k_}", q0, q1, r, r, B.mat("steel"), 24)
        cu.hide_render = True
        cu.hide_set(True) if cu.name in bpy.context.view_layer.objects else None
        md = ob.modifiers.new(f"cut{k_}", "BOOLEAN")
        md.operation, md.object, md.solver = "DIFFERENCE", cu, "EXACT"
    if bevel:
        md = ob.modifiers.new("bevel", "BEVEL")
        md.width, md.segments, md.limit_method = bevel * B.MM, 3, "ANGLE"
    return ob


def plan_plate(name, outline, z0, z1, material, bevel=0.0):
    """Extrude a plan-view outline (x, y in mm) between two heights: a part cut from plate or billet."""
    n = len(outline)
    verts = [B.V(q[0], q[1], z0) for q in outline] + [B.V(q[0], q[1], z1) for q in outline]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, 2 * n))] + [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    ob = B.mesh_obj(name, verts, faces, material, True, bevel)
    B.bm_fix_normals(ob)
    return ob


def strap(name, pts, secs, material, m=14):
    """A moulded leg along 3D points (mm): secs gives (half width fore and aft, half thickness) at each point."""
    for _ in range(2):
        out_p, out_s = [pts[0]], [secs[0]]
        for i in range(len(pts) - 1):
            a_, b_, sa, sb = pts[i], pts[i + 1], secs[i], secs[i + 1]
            out_p += [tuple(0.75 * a_[k] + 0.25 * b_[k] for k in range(3)), tuple(0.25 * a_[k] + 0.75 * b_[k] for k in range(3))]
            out_s += [tuple(0.75 * sa[k] + 0.25 * sb[k] for k in range(2)), tuple(0.25 * sa[k] + 0.75 * sb[k] for k in range(2))]
        pts, secs = out_p + [pts[-1]], out_s + [secs[-1]]
    verts, faces = [], []
    for i, (q, (hx, ht)) in enumerate(zip(pts, secs)):
        a_, b_ = pts[max(0, i - 1)], pts[min(len(pts) - 1, i + 1)]
        ty, tz = b_[1] - a_[1], b_[2] - a_[2]
        L_ = math.hypot(ty, tz) or 1.0
        ny, nz_ = -tz / L_, ty / L_
        for k in range(m):
            w = 2 * math.pi * k / m
            cw, sw = math.cos(w), math.sin(w)
            ex = math.copysign(abs(cw) ** 0.5, cw) * hx
            et = math.copysign(abs(sw) ** 0.5, sw) * ht
            verts.append(B.V(q[0] + ex, q[1] + ny * et, q[2] + nz_ * et))
    for i in range(len(pts) - 1):
        for k in range(m):
            k2 = (k + 1) % m
            faces.append((i * m + k, i * m + k2, (i + 1) * m + k2, (i + 1) * m + k))
    faces += [tuple(range(m - 1, -1, -1)), tuple((len(pts) - 1) * m + k for k in range(m))]
    ob = B.mesh_obj(name, verts, faces, material, True, 0, False)
    B.bm_fix_normals(ob)
    return ob


def brushed():
    """Brushed aluminium: a fine grain running one way across the plate."""
    key = "d3_brushed"
    if key in bpy.data.materials:
        return bpy.data.materials[key]
    m = bpy.data.materials.new(key)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.72, 0.73, 0.75, 1)
    b.inputs["Metallic"].default_value = 1.0
    tc, mp, nz = nt.nodes.new("ShaderNodeTexCoord"), nt.nodes.new("ShaderNodeMapping"), nt.nodes.new("ShaderNodeTexNoise")
    mp.inputs["Scale"].default_value = (30.0, 30.0, 6000.0)
    nz.inputs["Scale"].default_value = 1.0
    nz.inputs["Detail"].default_value = 3.0
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    nt.links.new(mp.outputs["Vector"], nz.inputs["Vector"])
    mr = nt.nodes.new("ShaderNodeMapRange")
    mr.inputs["To Min"].default_value, mr.inputs["To Max"].default_value = 0.22, 0.42
    nt.links.new(nz.outputs["Fac"], mr.inputs["Value"])
    nt.links.new(mr.outputs["Result"], b.inputs["Roughness"])
    bp = nt.nodes.new("ShaderNodeBump")
    bp.inputs["Strength"].default_value, bp.inputs["Distance"].default_value = 0.35, 0.0002
    nt.links.new(nz.outputs["Fac"], bp.inputs["Height"])
    nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    return m


def blade(name, pts, secs, material, m=16):
    """A flat bar swept along a path (mm): secs gives (half thickness across, half height) at each point."""
    for _ in range(2):          # round the path's corners, keeping its ends
        out_p, out_s = [pts[0]], [secs[0]]
        for i in range(len(pts) - 1):
            a_, b_, sa, sb = pts[i], pts[i + 1], secs[i], secs[i + 1]
            out_p += [tuple(0.75 * a_[k] + 0.25 * b_[k] for k in range(3)), tuple(0.25 * a_[k] + 0.75 * b_[k] for k in range(3))]
            out_s += [tuple(0.75 * sa[k] + 0.25 * sb[k] for k in range(2)), tuple(0.25 * sa[k] + 0.75 * sb[k] for k in range(2))]
        pts, secs = out_p + [pts[-1]], out_s + [secs[-1]]
    verts, faces = [], []
    for i, (q, (hx, hz)) in enumerate(zip(pts, secs)):
        a_, b_ = pts[max(0, i - 1)], pts[min(len(pts) - 1, i + 1)]
        tx, ty = b_[0] - a_[0], b_[1] - a_[1]
        L_ = math.hypot(tx, ty) or 1.0
        nx, ny = -ty / L_, tx / L_
        for k in range(m):
            w = 2 * math.pi * k / m
            cw, sw = math.cos(w), math.sin(w)
            ex = math.copysign(abs(cw) ** 0.6, cw) * hx
            ez = math.copysign(abs(sw) ** 0.6, sw) * hz
            verts.append(B.V(q[0] + nx * ex, q[1] + ny * ex, q[2] + ez))
    for i in range(len(pts) - 1):
        for k in range(m):
            k2 = (k + 1) % m
            faces.append((i * m + k, i * m + k2, (i + 1) * m + k2, (i + 1) * m + k))
    faces += [tuple(range(m - 1, -1, -1)), tuple((len(pts) - 1) * m + k for k in range(m))]
    ob = B.mesh_obj(name, verts, faces, material, True, 0, False)
    B.bm_fix_normals(ob)
    return ob


def indicator(name, base, s, face, variant):
    """An indicator on a ribbed rubber stalk (reference: indicators, two variants). "led": a slim black
    pod with a clear lens over a row of amber LEDs. "arrow": a long clear arrowhead lens over a bulb."""
    rubber, plastic = B.mat("rubber"), B.mat("plastic")
    ax = (0, s, 0)
    lathe(name + "_stalk", base, ax, [(0, 0), (0, 11.5), (3, 11.5), (4, 8.6), (7, 8.6), (8, 10.6), (11, 10.6), (12, 8.2), (15, 8.2), (16, 10), (19, 10), (21, 7.4), (38, 6.2), (38, 0)], rubber, 24)
    amber = glow("amberled", (1.0, 0.42, 0.02), 1.2)
    if variant == "arrow":
        org = (base[0], base[2])
        poly = [(base[1] + s * t, z) for t, z in ((34, -7), (50, -17.5), (66, -16.5), (114, -3.2), (114, 3.2), (66, 16.5), (50, 17.5), (34, 7))]
        if s < 0:
            poly = poly[::-1]
        slab(name + "_back", org, (face, 0), (0, 1), poly, -11, 0.5, plastic, 2.5)
        slab(name + "_reflector", org, (face, 0), (0, 1), [(base[1] + (y - base[1]) * 0.9 + s * 6, z * 0.8) for y, z in poly], 0.5, 1.4, B.mat("chrome"), 0)
        slab(name + "_lens", org, (face, 0), (0, 1), poly, 0.6, 10, ribbed_glass(), 3.0)
        B.cone(name + "_bulb", (base[0] + face * 1.5, base[1] + s * 58, base[2]), (base[0] + face * 7, base[1] + s * 58, base[2]), 5.5, 4, amber, 16)
        return
    head = [(34, 4.5), (38, 8.2), (46, 10.2), (84, 10.2), (95, 8.4), (102, 4), (103, 0)]
    lathe(name + "_pod", base, ax, [(34, 0)] + head, plastic, 28, sharp=False, ell=(0.74, 1.0))
    lens_o = (base[0] + face * 3.4, base[1], base[2])
    lathe(name + "_lens", lens_o, ax, [(42, 0), (42, 7.6), (48, 9.2), (84, 9.2), (94, 7.6), (100, 3.5), (101, 0)], glass_mat(), 28, sharp=False, ell=(0.7, 1.0))
    B.prism(name + "_strip", [(base[0] + face * 6.0, base[2] - 5.5), (base[0] + face * 7.2, base[2] - 5.5), (base[0] + face * 7.2, base[2] + 5.5), (base[0] + face * 6.0, base[2] + 5.5)],
            min(base[1] + s * 47, base[1] + s * 92), max(base[1] + s * 47, base[1] + s * 92), B.mat("chrome"), 0)
    for k in range(6):
        y = base[1] + s * (51 + 7.4 * k)
        B.cone(name + f"_led{k}", (base[0] + face * 7.0, y, base[2]), (base[0] + face * 9.2, y, base[2]), 3.1, 2.4, amber, 12)


def ribbed_glass():
    key = "d3_ribbedglass"
    if key in bpy.data.materials:
        return bpy.data.materials[key]
    g = bpy.data.materials.new(key)
    g.use_nodes = True
    nt = g.node_tree
    gb = nt.nodes["Principled BSDF"]
    gb.inputs["Transmission Weight"].default_value, gb.inputs["Roughness"].default_value, gb.inputs["IOR"].default_value = 1.0, 0.04, 1.49
    tc, wv, bp = nt.nodes.new("ShaderNodeTexCoord"), nt.nodes.new("ShaderNodeTexWave"), nt.nodes.new("ShaderNodeBump")
    wv.bands_direction = "Z"
    wv.inputs["Scale"].default_value = 1.0 / (2.2 * B.MM) / (2 * math.pi) * 6.28
    bp.inputs["Strength"].default_value, bp.inputs["Distance"].default_value = 0.6, 0.0006
    nt.links.new(tc.outputs["Object"], wv.inputs["Vector"])
    nt.links.new(wv.outputs["Fac"], bp.inputs["Height"])
    nt.links.new(bp.outputs["Normal"], gb.inputs["Normal"])
    return g


def indicators(model, c):
    drop("indstalk", "indicator", "rindstalk", "rindicator")
    variant = model.get("detail", {}).get("indicators", VARIANT.get("indicators", "led"))
    hb = c["head_b"]
    cowl = next((v for v in model["body"].get("panels", []) if v.get("cowl")), None) if model["family"].lower() == "sport" else None
    trees = _trees(("cowl", "ear")) if cowl else []
    for s in (-1, 1):
        if trees:
            # a faired bike: a slim amber blade let flush into the flank of the nose, no stalks
            x0, x1 = min(q[0] for q in cowl["pts"]), max(q[0] for q in cowl["pts"])
            z0, z1 = min(q[1] for q in cowl["pts"]), max(q[1] for q in cowl["pts"])
            bx, bz = x0 + (x1 - x0) * 0.66, z0 + (z1 - z0) * 0.5
            blade_ = [(bx - 34, bz - 9), (bx + 30, bz + 2), (bx + 36, bz + 9), (bx - 30, bz + 1)]
            if skin_patch(f"ind_f{s}_lens", trees, blade_, glow("amberled", (1.0, 0.42, 0.02), 1.2), 1.4, "y", s):
                grow = lambda k: [(bx + (x - bx) * k, bz + (z - bz) * k * 1.5) for x, z in blade_]
                skin_patch(f"ind_f{s}_pod", trees, grow(1.2), B.mat("plastic"), 0.8, "y", s)
                continue
        indicator(f"ind_f{s}", (hb[0] + 60, s * 92, hb[1] + 40), s, 1, variant)
    tl = next((shp for shp in model["body"].get("tail", []) if shp.get("cls") == "redlens"), None)
    if not tl and c.get("tail_lamp"):
        tl = {"pts": [list(c["tail_lamp"][:2])]}
    if tl:
        tx = sum(q[0] for q in tl["pts"]) / len(tl["pts"])
        tz = min(q[1] for q in tl["pts"]) - 25
        if c.get("tail_lamp"):
            # joined to the tail light's housing (beenster, sport round 2): a slim pod each side, grown out of
            # the housing through the flank of the tail, amber LED lens facing back
            xf, zc, W = c["tail_lamp"]
            amber = glow("amberled", (1.0, 0.42, 0.02), 1.2)
            for s in (-1, 1):
                base = (xf + 13, s * (W - 14), zc)
                lathe(f"ind_r{s}_pod", base, (0, s, 0), [(0, 0), (0, 13), (16, 13), (24, 10), (32, 8.6), (74, 8.2), (84, 6.4), (89, 3), (90, 0)], B.mat("chrome" if c.get("tail_lamp_chrome") else "plastic"), 28, sharp=False, ell=(0.8, 1.0))
                lathe(f"ind_r{s}_lens", (base[0] - 3.6, base[1], base[2]), (0, s, 0), [(31, 0), (31, 6.8), (73, 6.6), (82, 5.0), (86, 2.4), (87, 0)], amber, 28, sharp=False, ell=(0.72, 1.0))
            return
        arm = next((h for h in model["body"].get("plate_hanger", []) if h.get("cls") == "plastic" and "pts" in h), None)
        if arm and model.get("fit", {}).get("plate_hanger", True) and model["family"].lower() == "sport":
            # on the plate hanger's arm, a stalk each side, as on the reference rear views
            import shapes
            ax = min(q[0] for q in arm["pts"]) + 0.42 * (max(q[0] for q in arm["pts"]) - min(q[0] for q in arm["pts"]))
            tb = shapes.top_bottom(shapes.dense(arm["pts"], True, arm.get("t", 0)), ax)
            if tb:
                for s in (-1, 1):
                    lathe(f"ind_r{s}_boss", (ax, s * 20, (tb[0] + tb[1]) / 2), (0, s, 0), [(0, 0), (0, 14), (6, 14), (9, 11), (9, 0)], B.mat("plastic"), 20)
                    indicator(f"ind_r{s}", (ax, s * 27, (tb[0] + tb[1]) / 2), s, -1, variant)
                return
        for s in (-1, 1):
            indicator(f"ind_r{s}", (tx + 30, s * 58, tz), s, -1, variant)


VARIANT = {}


def cables(model, c):
    """Cables that go round things, not through them (beenster, 15 Nov). The clutch cable leaves the lever's
    adjuster, loops in front of the steering head, runs back along the outside of the frame's lower rail
    and drops to the release arm on top of the left case, clear of both cylinders."""
    grip = c.get("grip")
    if not grip:
        return
    drop("clutchcable", "throttlecable")
    half, nodes, eng = c["half"], c["nodes"], c["eng"]
    hb, ht = c["head_b"], c["head_t"]
    cs_ = eng["countershaft"]
    hose = surf("hose", (0.015, 0.015, 0.017), 0.0, 0.42)
    bx, bz = grip[0] + 1, grip[1]
    arm = (cs_[0] - 14, cs_[1] + 122)
    rail = [nodes[n] for n in ("D", "E", "F") if n in nodes]
    pts = [(bx + 66, 168, bz - 10), (bx + 118, 132, bz - 34), (ht[0] + 78, 74, ht[1] - 110), (hb[0] + 36, 62, hb[1] - 22)]
    if len(rail) == 3:
        pts += [(q[0], half(q) + 19, q[1] - 14) for q in rail]
    pts += [(arm[0] + 40, 150, arm[1] + 46), (arm[0] + 6, 128, arm[1] + 8)]
    B.smooth_tube("clutchcable", pts, 3.6, hose)
    for k, q in enumerate(rail[:3]):      # cable ties to the rail
        lathe(f"cabletie{k}", (q[0], half(q) + 8, q[1] - 6), (1, 0, -0.1), [(-2, 0), (-2, 20), (2, 20), (2, 0)], B.mat("plastic"), 16)
    B.cone("clutcharm", (arm[0], 108, arm[1] - 6), (arm[0] + 8, 132, arm[1] + 8), 6, 4.5, B.mat("steel"), 12)
    B.cone("clutcharmboss", (arm[0], 100, arm[1] - 24), (arm[0], 100, arm[1] - 2), 11, 9, B.mat("engine"), 16)


def service_parts(model, c, heads, hwa):
    """The parts the game services, each modelled as its own named part (svc_...): an oil filter canister
    on the front of the case, the air filter element in the top of the airbox, a fuel filter in the feed
    line with the pump plate under the tank. Spark plugs are made with the heads."""
    eng, crank, wide = c["eng"], c["crank"], c["wide"]
    steel, rubber = B.mat("steel"), B.mat("rubber")
    # oil filter: a spin-on canister, pointing forward and a little down, beside the front header
    case = eng["case"]
    xf = max(q[0] for q in case)
    zf = next(q[1] for q in case if q[0] == xf) - 62
    ex = model["exhaust"]
    side = -1 if ex.get("side") in ("under", "left", "both") else 1
    o = (xf - 22, side * 74, zf)
    d = (0.95, 0, -0.31)
    lathe("svc_oil_filter_boss", o, d, [(-16, 0), (-16, 30), (6, 30), (6, 0)], B.mat("engine"), 28)
    can = surf("oilcan", (0.015, 0.02, 0.05), 0.6, 0.28, coat=0.6, wear=0.1)
    prof = [(6, 30), (8, 33.5), (58, 33.5), (64, 31), (68, 25), (69, 0)]
    lathe("svc_oil_filter", o, d, prof, can, 40)
    for k in range(14):       # the flutes a filter wrench grips
        w = 2 * math.pi * k / 14
        up = (0.31 * math.cos(w), math.sin(w), 0.95 * math.cos(w))
        q = tuple(o[i] + d[i] * 60 + up[i] * 29.5 for i in range(3))
        q2 = tuple(o[i] + d[i] * 68 + up[i] * 23 for i in range(3))
        B.cone(f"svc_oil_filter_flute{k}", q, q2, 3.4, 2.6, can, 8)
    B.cone("svc_oil_filter_nut", tuple(o[i] + d[i] * 69 for i in range(3)), tuple(o[i] + d[i] * 76 for i in range(3)), 9, 9, can, 6)
    # air filter: a pleated panel in a rubber frame, let into the top of the airbox
    ab = next((shp for shp in eng.get("extras", [])), None)
    if ab:
        outline = B.shapes.dense(ab["pts"], True, ab.get("t", 0.9))
        xs = [q[0] for q in outline]
        xa, xb = min(xs) + (max(xs) - min(xs)) * 0.22, min(xs) + (max(xs) - min(xs)) * 0.72
        hw = max(30.0, hwa - 24)
        za = B.shapes.top_bottom(outline, xa)[0]
        zb = B.shapes.top_bottom(outline, xb)[0]
        sl = (zb - za) / (xb - xa)
        frame = surf("filterframe", (0.5, 0.2, 0.03), 0.0, 0.6)
        paper = surf("filterpaper", (0.62, 0.55, 0.38), 0.0, 0.9, wear=0.0)
        B.prism("svc_air_filter_frame", [(xa - 8, za - 8 - 8 * sl), (xb + 8, zb - 8 + 8 * sl), (xb + 8, zb + 3 + 8 * sl), (xa - 8, za + 3 - 8 * sl)], -hw - 8, hw + 8, frame, 2.5)
        verts, faces = [], []
        n_ = int((xb - xa) / 7)
        for k in range(n_):
            x_ = xa + (xb - xa) * k / n_
            x2 = x_ + (xb - xa) / n_
            z_ = za + sl * (x_ - xa) + 3
            o_ = len(verts)
            verts += [B.V(x_, -hw, z_), B.V(x_, hw, z_), B.V((x_ + x2) / 2, -hw, z_ + 7), B.V((x_ + x2) / 2, hw, z_ + 7), B.V(x2, -hw, z_ + sl * (x2 - x_)), B.V(x2, hw, z_ + sl * (x2 - x_))]
            faces += [(o_, o_ + 1, o_ + 3, o_ + 2), (o_ + 2, o_ + 3, o_ + 5, o_ + 4)]
        B.mesh_obj("svc_air_filter", verts, faces, paper, False)
        # fuel: the pump plate under the tank, a feed hose, and the filter in the line on the left rail
        xm = (xa + xb) / 2
        zt = za + sl * (xm - xa)
        B.cone("svc_fuel_pump_plate", (xb + 40, 0, zb + 6), (xb + 40, 0, zb + 14), 44, 44, B.mat("alu"), 32)
        for k in range(6):
            w = math.radians(60 * k)
            bolt(f"svc_fuel_pump_bolt{k}", (xb + 40 + 37 * math.cos(w), 37 * math.sin(w), zb + 6), (0, 0, -1), 3.2, 2.5)
        fy = hwa + 16
        fz = zt - 70
        B.cone("svc_fuel_filter", (xm + 40, fy, fz), (xm - 34, fy, fz - 8), 15, 15, surf("fuelfilter", (0.55, 0.56, 0.58), 1.0, 0.3), 24)
        B.cone("svc_fuel_filter_in", (xm + 40, fy, fz), (xm + 52, fy, fz + 1.3), 15, 5, surf("fuelfilter", (0.55, 0.56, 0.58), 1.0, 0.3), 24)
        B.cone("svc_fuel_filter_out", (xm - 34, fy, fz - 8), (xm - 46, fy, fz - 9.3), 15, 5, surf("fuelfilter", (0.55, 0.56, 0.58), 1.0, 0.3), 24)
        fhose = surf("fuelhose", (0.02, 0.02, 0.02), 0.0, 0.5, bump=(0.5, 0.6))
        B.smooth_tube("svc_fuel_hose_in", [(xb + 40, 30, zb + 4), (xb + 30, fy - 10, zb - 20), (xm + 80, fy, fz + 12), (xm + 50, fy, fz + 1)], 5, fhose)
        B.smooth_tube("svc_fuel_hose_out", [(xm - 44, fy, fz - 9), (xm - 70, fy - 6, fz - 30), (xm - 60, hwa - 4, fz - 60)], 5, fhose)
        for nm, q in (("a", (xm + 50, fy, fz + 1)), ("b", (xm - 44, fy, fz - 9))):
            lathe(f"svc_fuel_clip{nm}", q, (1, 0, 0.11), [(-4, 5.4), (-4, 7), (4, 7), (4, 5.4)], steel, 16)


def controls(model, c):
    """Bars: ribbed rubber grips with a flange, bar-end weights, switch housings, lever perches with levers
    that bow forward and come back to a ball end (reference: lever and switchgear), a brake master
    cylinder with its reservoir, and mirrors on stems from the perches, big enough to use."""
    grip = c.get("grip")
    if not grip:
        return
    drop("grip", "lever", "mirror", "mstalk", "mstub", "brakeres")
    rubber = surf("griprubber", (0.018, 0.018, 0.02), 0.0, 0.72, bump=(0.35, 0.5), wear=0.1)
    plastic, steel, dark = B.mat("plastic"), B.mat("steel"), B.mat("alu_dark")
    satin = surf("leverblack", (0.035, 0.035, 0.04), 0.85, 0.36)
    for s in (-1, 1):
        g0, g1 = (grip[0], s * 258, grip[1]), (grip[0] - 6, s * 384, grip[1] - 4)
        L_ = math.sqrt(sum((g1[i] - g0[i]) ** 2 for i in range(3)))
        d = tuple((g1[i] - g0[i]) / L_ for i in range(3))
        # (reference: hand grip) a big flared flange against the switchgear, a slightly barrelled body, a thicker end collar
        prof = [(0, 12), (0, 28.5), (1.2, 30.4), (3, 31), (4.8, 30.4), (6, 28.5), (6.8, 23), (8.2, 19.2), (10.5, 17.3)]
        for k_ in range(1, 30):
            t = 10.5 + (L_ - 26) * k_ / 30
            prof.append((t, 16.9 + 0.7 * math.sin(math.pi * k_ / 30)))
        prof += [(L_ - 15.5, 17.2), (L_ - 14, 19.4), (L_ - 12.5, 19.9), (L_ - 2, 19.9), (L_ - 0.6, 19.2), (L_, 17.8), (L_, 11)]
        lathe(f"grip{s}", g0, d, prof, grip_mat(L_, 31.0), 96, sharp=False, uv=True)
        lathe(f"barend{s}", g0, d, [(L_, 0), (L_, 15.5), (L_ + 3, 17), (L_ + 18, 17), (L_ + 24, 13), (L_ + 24, 0)], dark, 32)
        bolt(f"barendbolt{s}", tuple(g0[i] + d[i] * (L_ + 23) for i in range(3)), d, 5, 2.5)
        # switch housing: a two-part block round the bar inboard of the grip
        bx, bz = grip[0] + 1, grip[1]
        box = chaikin([(bx - 25, bz - 21), (bx + 23, bz - 23), (bx + 26, bz + 18), (bx - 8, bz + 25), (bx - 26, bz + 14)], 2)
        ya, yb = s * 206, s * 254
        B.prism(f"switch{s}", box, min(ya, yb), max(ya, yb), plastic, 3.5)
        B.tube(f"switchsplit{s}", [(bx - 26.4, ya, bz + 1), (bx - 26.4, yb, bz + 1)], 0.9, B.mat("rubber"))
        ym = s * 230
        if s == 1:      # left: lights rocker, indicator slider, horn button
            B.prism(f"swlights{s}", [(bx - 30, bz + 6), (bx - 25, bz + 6), (bx - 25, bz + 16), (bx - 31, bz + 15)], ym - 9, ym + 9, plastic, 1)
            B.prism(f"swind{s}", [(bx - 31, bz - 6), (bx - 25, bz - 6), (bx - 25, bz + 2), (bx - 31, bz + 2)], ym - 6, ym + 6, surf("swgrey", (0.12, 0.12, 0.13), 0, 0.5), 1)
            B.cone(f"swhorn{s}", (bx - 24, ym, bz - 14), (bx - 30, ym, bz - 15), 5.5, 5, plastic, 16)
        else:           # right: red kill rocker, starter button
            B.prism(f"swkill{s}", [(bx - 31, bz + 5), (bx - 25, bz + 5), (bx - 25, bz + 16), (bx - 32, bz + 14)], ym - 8, ym + 8, surf("killred", (0.5, 0.02, 0.02), 0, 0.45), 1)
            B.cone(f"swstart{s}", (bx - 24, ym, bz - 10), (bx - 30, ym, bz - 11), 6, 5.5, surf("swgrey", (0.12, 0.12, 0.13), 0, 0.5), 16)
        # lever perch: a clamp round the bar, split, with two bolts, and the pivot out in front
        yp0, yp1 = s * 176, s * 200
        yc = (yp0 + yp1) / 2
        perch = chaikin([(bx - 17, bz - 17), (bx + 22, bz - 18), (bx + 56, bz - 22), (bx + 60, bz - 8), (bx + 30, bz + 4), (bx + 14, bz + 18), (bx - 12, bz + 18), (bx - 19, bz)], 2)
        B.prism(f"perch{s}", perch, min(yp0, yp1), max(yp0, yp1), satin, 2.0)
        for dz in (-11, 11):
            bolt(f"perchbolt{s}{dz}", (bx - 18.5, yc, bz + dz), (-1, 0, 0), 4.2, 3.5)
        piv = (bx + 48, yc, bz - 14)
        bolt(f"levpivot{s}", (piv[0], piv[1], piv[2] + 9), (0, 0, 1), 6, 3.5, steel, False)
        # the lever (reference: beenster's drawing and the two lever photos, round 4): cut from billet plate.
        # A wide root at the pivot with lightening holes and the span adjuster dial, a dog-leg forward
        # at the knuckle, then a long finger section that curves gently back towards the grip and
        # ends in a ring.
        cl = [(0, 0, 11.5), (24, 7, 10.5), (50, 14, 9.5), (68, 16.5, 6.6), (92, 14.5, 5.6), (128, 8.5, 5.2), (162, 3, 4.9), (186, 1.5, 4.7)]
        for _ in range(3):
            o_ = [cl[0]]
            for i_ in range(len(cl) - 1):
                o_ += [tuple(0.75 * cl[i_][k] + 0.25 * cl[i_ + 1][k] for k in range(3)), tuple(0.25 * cl[i_][k] + 0.75 * cl[i_ + 1][k] for k in range(3))]
            cl = o_ + [cl[-1]]
        lft, rgt = [], []
        for i_, (t_, f_, w_) in enumerate(cl):
            a_, b_ = cl[max(0, i_ - 1)], cl[min(len(cl) - 1, i_ + 1)]
            dt, df = b_[0] - a_[0], b_[1] - a_[1]
            Ld = math.hypot(dt, df)
            lft.append((t_ - df / Ld * w_, f_ + dt / Ld * w_))
            rgt.append((t_ + df / Ld * w_, f_ - dt / Ld * w_))
        te, fe = cl[-1][0] + 6, cl[-1][1] + 0.5
        ring = [(te + 8.6 * math.cos(math.radians(w)), fe + 8.6 * math.sin(math.radians(w))) for w in range(140, -141, -20)]
        root = [(12.5 * math.cos(math.radians(w)), 12.5 * math.sin(math.radians(w))) for w in range(-100, -261, -20)]
        tf = lft[2:-1] + ring + rgt[2:-1][::-1] + root
        zl = piv[2]
        lev = plan_plate(f"lever{s}", [(piv[0] + f_, yc + s * t_) for t_, f_ in tf], zl - 5.5, zl + 5.5, satin, 0)
        holes = [(22, 6.6, 4.4), (37, 10.6, 3.7), (50.5, 13.8, 3.0), (te, fe, 3.8)]
        drill(lev, [((piv[0] + f_, yc + s * t_, zl - 9), (piv[0] + f_, yc + s * t_, zl + 9), r_) for t_, f_, r_ in holes], 1.6)
        lev.modifiers.new("wn", "WEIGHTED_NORMAL").keep_sharp = True
        red = surf("anored", (0.5, 0.025, 0.03), 1.0, 0.32, wear=0.05)
        B.cone(f"leveradj{s}", (piv[0] + 1.5, yc + s * 7, zl + 5.5), (piv[0] + 1.5, yc + s * 7, zl + 11), 8.2, 8.2, red, 14)
        B.cone(f"leveradjdot{s}", (piv[0] + 1.5, yc + s * 7, zl + 11), (piv[0] + 1.5, yc + s * 7, zl + 12.2), 4, 3.4, steel, 6)
        if s == -1:
            # brake master cylinder across the perch, and the fluid cup above it
            B.cone("mastercyl", (bx + 20, yc + 4, bz - 4), (bx + 70, yc + 4, bz - 10), 11.5, 10.5, satin, 24)
            B.cone("masterneck", (bx + 34, yc + 16, bz + 2), (bx + 34, yc + 16, bz + 20), 5, 5, B.mat("rubber"), 12)
            B.cone("brakeres", (bx + 34, yc + 16, bz + 18), (bx + 34, yc + 16, bz + 46), 17.5, 18.5, surf("rescup", (0.55, 0.5, 0.36), 0, 0.25, wear=0.0), 28)
            B.cone("brakerescap", (bx + 34, yc + 16, bz + 46), (bx + 34, yc + 16, bz + 54), 20, 19.5, plastic, 28)
        else:
            # clutch cable adjuster: a knurled barrel on the perch
            B.cone("clutchadj", (bx + 40, yc - 14, bz - 6), (bx + 66, yc - 20, bz - 10), 6.5, 6.5, steel, 12)
            B.cone("clutchadjlock", (bx + 46, yc - 15.4, bz - 7), (bx + 51, yc - 16.6, bz - 7.8), 8.5, 8.5, steel, 6)
        # mirror: a stem from the perch, a ball joint, and a shaped head with the glass facing back
        root = (bx + 4, yc, bz + 18)
        mc = (bx - 2, s * 318, bz + 150)
        cowl = next((v for v in model["body"].get("panels", []) if v.get("cowl")), None) if model["family"].lower() == "sport" else None
        hit = None
        if cowl:
            # a faired bike carries its mirrors on the fairing: a short aero stem out of the cowl's shoulder
            cx0, cx1 = min(q[0] for q in cowl["pts"]), max(q[0] for q in cowl["pts"])
            cz1 = max(q[1] for q in cowl["pts"])
            for zz in (cz1 - 26, cz1 - 40, cz1 - 60):
                hit = side_hit(("cowl", "ear"), cx0 + (cx1 - cx0) * 0.42, zz, s)
                if hit:
                    break
        if hit:
            root = (hit[0], hit[1] - s * 6, hit[2])
            mc = (root[0] - 34, root[1] + s * 150, root[2] + 58)
            B.cone(f"mirrorboss{s}", root, (root[0], root[1] + s * 12, root[2] + 3), 12, 9, plastic, 16)
            strap(f"mstalk{s}", [(root[0], root[1] + s * 8, root[2] + 2), (root[0] - 8, root[1] + s * 48, root[2] + 22), (mc[0] + 16, mc[1] - s * 52, mc[2] - 12), (mc[0] + 14, mc[1] - s * 8, mc[2] - 4)],
                  [(16, 6), (14, 5.5), (15, 5.5), (18, 6)], plastic)
        else:
            B.cone(f"mirrorboss{s}", root, (root[0], root[1], root[2] + 10), 8, 7, satin, 6)
            B.smooth_tube(f"mstalk{s}", [(root[0], root[1], root[2] + 8), (root[0] + 10, yc + s * 8, root[2] + 62), (mc[0] + 22, s * 262, mc[2] - 30), (mc[0] + 20, s * 290, mc[2] - 6)], 5.2, satin)
        outline = chaikin([(s * -86, -10), (s * -64, 30), (s * 10, 44), (s * 76, 32), (s * 86, -4), (s * 58, -36), (s * -52, -40)], 3)
        if s == -1:
            outline = outline[::-1]
        mo = (mc[0], mc[2])
        back = [(1.0, 0), (1.0, 8), (0.9, 18), (0.66, 26), (0.3, 30)]
        verts, faces, m = [], [], len(outline)
        for k_, dx in back:
            verts += [B.V(mo[0] + dx, mc[1] + y * k_, mo[1] + z * k_) for y, z in outline]
        for j in range(len(back) - 1):
            for i in range(m):
                i2 = (i + 1) % m
                faces.append((j * m + i, j * m + i2, (j + 1) * m + i2, (j + 1) * m + i))
        faces += [tuple(range(m - 1, -1, -1)), tuple((len(back) - 1) * m + i for i in range(m))]
        B.bm_fix_normals(B.mesh_obj(f"mirror{s}", verts, faces, plastic, True, 0, False))
        gv = [B.V(mo[0] - 0.6, mc[1] + y * 0.93, mo[1] + z * 0.9) for y, z in outline]
        B.mesh_obj(f"mirrorglass{s}", gv, [tuple(range(m))], surf("mirrorglass", (0.9, 0.92, 0.95), 1.0, 0.01, wear=0.0), False)
    # bar clamps on the risers: a cap each with two bolts
    rz = c.get("rz")
    if rz and not c.get("clip"):
        for s in (-1, 1):
            cap = chaikin([(rz[0] - 18, rz[1] - 2), (rz[0] + 18, rz[1] - 2), (rz[0] + 18, rz[1] + 12), (rz[0] + 8, rz[1] + 19), (rz[0] - 8, rz[1] + 19), (rz[0] - 18, rz[1] + 12)], 1)
            B.prism(f"barclamp{s}", cap, s * 45 - 15, s * 45 + 15, dark, 1.5)
            for dx in (-12, 12):
                bolt(f"barclampbolt{s}{dx}", (rz[0] + dx, s * 45, rz[1] + (15 if abs(dx) < 12 else 14)), (0, 0, 1), 4.5, 4)


def mudguard_fixings(model, c):
    """Fork shrouds (beenster's markup and explanation, 15 Nov): the fork-mounted part of the front mudguard
    sits on the FRONT of each fork leg. It is a U-shaped shell that wraps the front of the leg to turn
    the air round it and keep stones off the fork, bolted to the fork foot at its rear edge low down,
    and sweeping forward at the top into the edge of the shell over the tyre. Nothing behind the leg."""
    F, uf, nb = c["F"], c["uf"], c["nb"]
    guard = next((o for o in bpy.data.objects if o.name.startswith("guard")), None)
    if not guard or not any(v.get("shroud") for v in model["body"].get("front_guard", [])):
        return
    paint = guard.data.materials[0]
    arc = next(v["arc"] for v in model["body"]["front_guard"] if "arc" in v)
    r_edge = arc["r1"] + 4 - 17
    if model["family"].lower() in ("sport", "cafe"):
        r_edge = model["wheels"]["front"]["tyre_r"] + 11 - 28      # the shell sits tight on the tyre (build3d.guard_blade, tight)
    fw = (-nb[0], -nb[1])
    FY, n = 105.0, 21
    # along the leg: (t from the axle, reach forward of the leg's middle, half width across)
    tab = [(46, 20, 24), (54, 32, 33), (70, 36, 36), (150, 38, 36), (210, 44, 37), (255, 58, 38), (290, 82, 38), (r_edge - 14, 112, 38), (r_edge - 4, 128, 38)]
    st = []
    for k in range(len(tab) - 1):
        (t0, a0, b0), (t1, a1, b1) = tab[k], tab[k + 1]
        m_ = max(2, int((t1 - t0) / 8))
        for q in range(m_):
            u = q / m_
            st.append((t0 + (t1 - t0) * u, a0 + (a1 - a0) * u, b0 + (b1 - b0) * u))
    st.append(tab[-1])
    for _ in range(3):
        st = [st[0]] + [tuple(0.25 * st[i - 1][k] + 0.5 * st[i][k] + 0.25 * st[i + 1][k] for k in range(3)) for i in range(1, len(st) - 1)] + [st[-1]]
    for s in (-1, 1):
        verts, faces = [], []
        rows = []
        for t, a, b in st:
            o = B.add(F, uf, t)
            row = []
            for q in range(n):
                ph = math.radians(-96 + 192 * q / (n - 1))       # inner side, round the front, to the outer side
                d, y = a * math.cos(ph) ** 0.8 if math.cos(ph) > 0 else a * math.cos(ph), FY + b * math.sin(ph)
                row.append((o[0] + fw[0] * d, y, o[1] + fw[1] * d))
            rows.append(row)
        # nothing stands above the shell: where the shroud sweeps forward at the top it follows the shell's edge
        def under(x, y, z, rmax):
            L_ = math.hypot(x - F[0], z - F[1])
            k_ = min(1.0, rmax / L_)
            return (F[0] + (x - F[0]) * k_, y, F[1] + (z - F[1]) * k_)
        rows = [[under(x, y, z, r_edge - 3) for x, y, z in row] for row in rows]
        # the shoulder: the top edge leans in under the edge of the shell
        for lift, keep in ((4, 0.55), (7, 0.1)):
            rows.append([under(x, 70 + (y - 70) * keep, z, r_edge - 3 + lift) for x, y, z in ((x + uf[0] * 20, y, z + uf[1] * 20) for x, y, z in rows[len(st) - 1])])
        for row in rows:
            verts += [B.V(x, s * y, z) for x, y, z in row]
        for i in range(len(rows) - 1):
            for q in range(n - 1):
                faces.append((i * n + q, i * n + q + 1, (i + 1) * n + q + 1, (i + 1) * n + q))
        ob = B.mesh_obj(f"guardshroud{s}", verts, faces, paint, True, 0, sharp=False)
        B.bm_fix_normals(ob)
        sol = ob.modifiers.new("shell", "SOLIDIFY")
        sol.thickness, sol.offset = 3.5 * B.MM, 1
        for t in (74, 128):
            q = B.add(B.add(F, uf, t), fw, -4)
            B.cone(f"guardboss{s}{t}", (q[0], s * (FY + 26), q[1]), (q[0], s * (FY + 39), q[1]), 9, 8, paint, 20)
            bolt(f"guardbolt{s}{t}", (q[0], s * (FY + 39), q[1]), (0, s, 0), 5.2, 2.6)


def set_mat(prefix, material):
    for ob in bpy.data.objects:
        if ob.name.startswith(prefix) and getattr(ob.data, "materials", None) is not None and len(ob.data.materials):
            ob.data.materials[0] = material


def heat_tint():
    """Stainless header pipe that has been hot: straw, bronze and a little blue in patches."""
    key = "d3_headertint"
    if key in bpy.data.materials:
        return bpy.data.materials[key]
    m = bpy.data.materials.new(key)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Metallic"].default_value, b.inputs["Roughness"].default_value = 1.0, 0.22
    tc, nz, ramp = nt.nodes.new("ShaderNodeTexCoord"), nt.nodes.new("ShaderNodeTexNoise"), nt.nodes.new("ShaderNodeValToRGB")
    nz.inputs["Scale"].default_value, nz.inputs["Detail"].default_value = 5.0, 2.0
    el = ramp.color_ramp.elements
    el[0].position, el[0].color = 0.3, (0.55, 0.53, 0.5, 1)
    el[1].position, el[1].color = 0.7, (0.5, 0.33, 0.14, 1)
    e = el.new(0.52)
    e.color = (0.62, 0.5, 0.3, 1)
    e = el.new(0.62)
    e.color = (0.3, 0.26, 0.4, 1)
    nt.links.new(tc.outputs["Object"], nz.inputs["Vector"])
    nt.links.new(nz.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], b.inputs["Base Color"])
    return m


def engine(model, c):
    """The engine as its separate castings, each with its own finish: sand-cast cases, finned barrels on
    a base flange, square heads with cam end caps, valve covers bolted on, a clutch cover and an alternator
    cover that are different parts, a water pump, rubber inlet stubs with clamps and throttle bodies, and
    exhaust ports with a two-stud flange and collar where each header pipe starts."""
    add, unit = B.add, B.unit
    eng, crank, wide = c["eng"], c["crank"], c["wide"]
    if eng["layout"] in ("inline4", "triple", "boxer"):
        return
    silver = B.mat("engine")
    mag = surf("magcover", (0.2, 0.165, 0.12), 0.9, 0.4, bump=(1.0, 0.1))
    blk = surf("crinkle", (0.022, 0.022, 0.024), 0.3, 0.62, bump=(0.4, 0.5))
    steel, rubber, alu = B.mat("steel"), B.mat("rubber"), B.mat("alu")
    heads = []
    for i, cy in enumerate(eng["cylinders"]):
        drop(f"cylhead{i}", f"cover{i}", f"fin{i}_", f"barrel{i}", f"plugcap{i}", f"pluglead{i}")
        u = unit(cy["angle"])
        n = (-u[1], u[0])
        if n[1] < 0:
            n = (-n[0], -n[1])       # towards the middle of the V
        r = cy["bore_w"] / 2
        base, end = add(crank, u, cy["start"]), add(crank, u, cy["start"] + cy["length"])
        headend = add(end, u, cy["head"])
        d3 = (u[0], 0, u[1])
        # (beenster, 15 Nov) cooling fins are not rings round a pipe: the barrel is a squared casting and the
        # fins follow it, growing wider towards the head, with the top two stepped back in under the head
        prof = [(-6, r - 12), (0, r - 12), (0, r + 3), (9, r + 3), (9, r - 11)]
        k, Lc = 16.0, cy["length"]
        nf = int((Lc - 24) / 8.5)
        j_ = 0
        while k < Lc - 8:
            rf = r - 5 + 13 * (j_ / max(1, nf - 1)) ** 0.8 - (7 if j_ >= nf - 1 else 3 if j_ == nf - 2 else 0)
            prof += [(k, r - 11), (k, rf), (k + 2.4, rf), (k + 2.4, r - 11)]
            k += 8.5
            j_ += 1
        prof += [(Lc + 2, r - 11)]
        lathe(f"barrel{i}", P(base, 0), d3, prof, silver, 64, ell=(0.92, 1.0), square=4.6)
        for a_ in (45, 135, 225, 315):       # the four base nuts
            w = math.radians(a_)
            q = (base[0] + u[0] * 9 + n[0] * (r - 4) * math.cos(w), (r - 4) * math.sin(w), base[1] + u[1] * 9 + n[1] * (r - 4) * math.cos(w))
            bolt(f"basenut{i}{a_}", q, d3, 6.5, 6, steel, False)
        hw = r + 9
        B.pillow(f"cylhead{i}", [add(end, n, hw), add(headend, n, hw), add(headend, n, -hw), add(end, n, -hw)], -(r + 4), r + 4, silver, 7, 12)
        cov = add(headend, u, 30)
        cw = r - 3
        B.pillow(f"cover{i}", [add(headend, n, cw), add(cov, n, cw - 12), add(cov, n, -cw + 12), add(headend, n, -cw)], -(r - 9), r - 9, mag, 9, 16)
        B.prism(f"covergasket{i}", [add(headend, n, cw + 2), add(add(headend, u, 3), n, cw + 2), add(add(headend, u, 3), n, -cw - 2), add(headend, n, -cw - 2)], -(r - 6), r - 6, rubber, 0.5)
        for sn in (-1, 1):
            for sy in (-1, 1):
                q = add(add(headend, u, 27), n, sn * (cw - 22))
                bolt(f"coverbolt{i}{sn}{sy}", (q[0], sy * (r - 28), q[1]), d3, 5, 4.5)
        hc = add(end, u, cy["head"] * 0.5)
        for sy in (-1, 1):       # cam end caps
            B.cone(f"camcap{i}{sy}", P(hc, sy * (r + 2)), P(hc, sy * (r + 11)), 27, 24, mag, 28)
            for a_ in (30, 150, 270):
                q = add(hc, unit(a_ + cy["angle"]), 19)
                bolt(f"camcapbolt{i}{sy}{a_}", P(q, sy * (r + 11)), (0, sy, 0), 3.6, 3)
        # plug cap and lead out of the valve cover
        pc = add(cov, u, -2)
        white = surf("ceramic", (0.85, 0.84, 0.8), 0.0, 0.25, wear=0.0)
        lathe(f"svc_spark_plug{i}_hex", P(pc, 0), d3, [(0, 0), (0, 10.4), (9, 10.4), (9, 0)], steel, 6)
        lathe(f"svc_spark_plug{i}", P(pc, 0), d3, [(9, 0), (9, 6.6), (12, 6.6), (13, 5.8), (15, 6.4), (17, 5.8), (19, 6.4), (21, 5.8), (27, 5.2), (27, 0)], white, 20)
        lathe(f"svc_spark_plug{i}_tip", P(pc, 0), d3, [(27, 2.6), (32, 2.6), (32, 0)], steel, 12)
        B.cone(f"plugcap{i}", P(add(pc, u, 23), 0), P(add(pc, u, 56), 0), 10.5, 8.5, rubber, 16)
        B.smooth_tube(f"pluglead{i}", [P(add(pc, u, 54), 0), P(add(add(pc, u, 84), n, 20), 14), P(add(add(pc, u, 92), n, 80), 30)], 3.6, rubber)
        # inlet: rubber stub, clamp, throttle body, injector
        i3 = (n[0], 0, n[1])
        lathe(f"inletstub{i}", P(add(hc, n, hw - 4), 0), i3, [(0, 30), (8, 30), (12, 27), (30, 27), (30, 22)], rubber, 32)
        lathe(f"inletclamp{i}", P(add(hc, n, hw - 4), 0), i3, [(19, 27.4), (19, 28.6), (27, 28.6), (27, 27.4)], steel, 32)
        lathe(f"throttle{i}", P(add(hc, n, hw - 4), 0), i3, [(28, 25), (40, 25), (44, 28), (62, 28), (66, 25), (96, 25), (96, 0)], alu, 32)
        tq = add(add(hc, n, hw + 48), u, 0)
        B.tube(f"throttleshaft{i}", [P(tq, -40), P(tq, 40)], 4, steel)
        B.cone(f"throttlepulley{i}", P(tq, -44), P(tq, -36), 17, 17, B.mat("alu_dark"), 20)
        inj = add(add(hc, n, hw + 24), u, 26)
        B.cone(f"injector{i}", P(inj, 0), P(add(add(inj, u, 30), n, 14), 0), 8, 7, surf("injgreen", (0.1, 0.25, 0.12), 0, 0.5), 14)
        heads.append((hc, u, r, i))
    # side covers: clutch cover on the right, alternator cover on the left, different shapes and finishes
    drop("sidecover", "sidedome", "coverring", "coverboss", "coverrib", "oilfilter")
    for i, cv in enumerate(eng.get("covers", [])):
        drop(f"bolt{i}")
        cc_, r_ = tuple(cv["c"]), cv["r"]
        for sg in (-1, 1):
            p0 = P(cc_, sg * (wide / 2 - 28))
            if sg == -1:
                lathe(f"clutchcover{i}", p0, (0, sg, 0), [(0, r_ + 3), (6, r_ + 3), (6, r_), (26, r_ - 2), (34, r_ - 9), (37, r_ - 20), (37, r_ * 0.66), (41, r_ * 0.66), (44, r_ * 0.6), (44, 0)], mag, 64)
                lathe(f"clutchcap{i}", p0, (0, sg, 0), [(44, r_ * 0.52), (50, r_ * 0.5), (53, r_ * 0.4), (53, 0)], blk, 48)
                for k_ in range(6):
                    q = add(cc_, unit(60 * k_), r_ * 0.58)
                    bolt(f"clutchcapbolt{i}{k_}", P(q, p0[1] + sg * 44), (0, sg, 0), 4, 3.5)
                nb_ = cv.get("bolts", 8)
            else:
                lathe(f"altcover{i}", p0, (0, sg, 0), [(0, r_ * 0.96), (5, r_ * 0.96), (5, r_ * 0.92), (24, r_ * 0.9), (40, r_ * 0.74), (50, r_ * 0.5), (53, r_ * 0.3), (53, 0)], blk, 64)
                B.cone(f"altplug{i}", (p0[0], p0[1] + sg * 53, p0[2]), (p0[0], p0[1] + sg * 58, p0[2]), 15, 14, alu, 24)
                B.cone(f"altplugslot{i}", (p0[0], p0[1] + sg * 57.4, p0[2]), (p0[0], p0[1] + sg * 58.3, p0[2]), 7, 7, rubber, 6)
                nb_ = 6
            rb = (r_ - 7) if sg == -1 else r_ * 0.9 - 6
            for k_ in range(nb_):
                q = add(cc_, unit(k_ * 360 / nb_ + 15), rb)
                bolt(f"bolt{i}{sg}{k_}", P(q, p0[1] + sg * (6 if sg == -1 else 5)), (0, sg, 0), 5, 16)
    case = B.shapes.dense(eng["case"], True, 0.9)
    ccx, ccz = sum(q[0] for q in case) / len(case), sum(q[1] for q in case) / len(case)
    inner = [(ccx + (q[0] - ccx) * 0.9, ccz + (q[1] - ccz) * 0.88) for q in case]
    for sg in (-1, 1):       # the outer case halves: their own castings, bolted to the centre cases
        ya, yb = sg * (wide / 2 - 34), sg * (wide / 2 - 14)
        B.pillow(f"sidecase{sg}", inner, min(ya, yb), max(ya, yb), surf("sidecase", (0.15, 0.15, 0.16), 0.9, 0.45, bump=(1.2, 0.15)), 8, 12, both=False)
    set_mat("crankcase", surf("sandcast", (0.42, 0.42, 0.43), 0.9, 0.5, bump=(1.4, 0.25)))
    zb = min(q[1] for q in case)
    xb = [q[0] for q in case if q[1] < zb + 34]
    if xb:
        drop("sumprib")
        x0, x1 = min(xb) + 8, max(xb) - 8
        pan = [(x0, zb + 16), (x1, zb + 16), (x1 - 10, zb - 12), (x0 + 16, zb - 14)]
        B.pillow("sump", pan, -wide / 2 + 52, wide / 2 - 52, blk, 5, 8)
        B.prism("sumpflange", [(x0 - 6, zb + 14), (x1 + 6, zb + 14), (x1 + 6, zb + 19), (x0 - 6, zb + 19)], -wide / 2 + 46, wide / 2 - 46, silver, 1)
        nb_ = 6
        for k_ in range(nb_):
            for sy in (-1, 1):
                bolt(f"sumpbolt{k_}{sy}", (x0 + (x1 - x0) * k_ / (nb_ - 1), sy * (wide / 2 - 50), zb + 14), (0, 0, -1), 4, 3.5)
        B.cone("drainplug", ((x0 + x1) / 2, 0, zb - 13), ((x0 + x1) / 2, 0, zb - 20), 9, 9, steel, 6)
    set_mat("sprocketcover", B.mat("plastic"))
    # water pump low on the right of the case front, with its outlet stub
    wp = (crank[0] + 118, crank[1] - 78)
    y0 = -(wide / 2 - 16)
    lathe("waterpump", P(wp, y0), (0, -1, 0), [(0, 40), (10, 40), (10, 36), (17, 36), (20, 33), (20, 15), (24, 13), (24, 0)], silver, 40)
    for k_ in range(5):
        q = add(wp, unit(72 * k_ + 20), 31)
        bolt(f"pumpbolt{k_}", P(q, y0 - 12), (0, -1, 0), 3.8, 3.5)
    B.cone("pumpstub", P(add(wp, (0.5, 0.86), 20), y0 - 12), P(add(wp, (0.5, 0.86), 62), y0 - 12), 13, 13, silver, 20)
    # engine mounts: where a frame tube ends on the engine it ends in a boss with a bolt through it
    half, nodes = c["half"], c["nodes"]
    drop("lug_")
    for nm, pn in nodes.items():
        if not nm.startswith("M"):
            continue
        hw_ = half(pn)
        for sg in (-1, 1):
            B.cone(f"mountboss{nm}{sg}", P(pn, sg * (hw_ - 15)), P(pn, sg * (hw_ + 15)), 19, 19, c["framem"], 28)
            B.cone(f"mountlug{nm}{sg}", P(pn, sg * (hw_ - 46)), P(pn, sg * (hw_ - 15)), 21, 18, silver, 24)
            bolt(f"mountbolt{nm}{sg}", P(pn, sg * (hw_ + 15)), (0, sg, 0), 9, 7, steel, False)
            B.cone(f"mountwasher{nm}{sg}", P(pn, sg * (hw_ + 15)), P(pn, sg * (hw_ + 17)), 13, 13, steel, 24)
    exhaust_ports(model, c, heads)
    radiator(model, c, heads, wp, y0)
    ab = bpy.data.objects.get("airbox0")
    service_parts(model, c, heads, (ab.dimensions.y / 2 / B.MM) if ab else 80.0)


def exhaust_ports(model, c, heads):
    add = B.add
    ex = model["exhaust"]
    both, left, under = (ex.get("side") == k_ for k_ in ("both", "left", "under"))
    steel, silver = B.mat("steel"), B.mat("engine")
    set_mat("header", heat_tint())
    for i, hp in enumerate(ex["headers"]):
        sg = 1 if left or ((both or under) and i == 0) else -1
        y0 = sg * (60 if hp.get("front") else 40)
        p0, p1 = tuple(hp["pts"][0][:2]), tuple(hp["pts"][1][:2])
        L_ = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
        t = ((p1[0] - p0[0]) / L_, (p1[1] - p0[1]) / L_)
        d3 = (t[0], 0, t[1])
        rp = hp.get("w", 44) / 2 * 0.8
        o = P(p0, y0)
        lathe(f"port{i}", o, d3, [(-30, rp + 9), (-8, rp + 9), (-4, rp + 13), (-4, 0)], silver, 32)
        lathe(f"portflange{i}", o, d3, [(-4, rp + 2), (-4, rp + 12), (2, rp + 12), (2, rp + 2)], steel, 32)
        lathe(f"portcollar{i}", o, d3, [(2, rp + 0.5), (2, rp + 4.5), (14, rp + 4.5), (16, rp + 2.5), (16, rp + 0.5)], steel, 32)
        for sy in (-1, 1):
            q = (o[0], o[1] + sy * (rp + 14), o[2])
            lathe(f"portear{i}{sy}", q, d3, [(-4, 0), (-4, 10.5), (2, 10.5), (2, 0)], steel, 20)
            B.cone(f"portstud{i}{sy}", (q[0] - t[0] * 4, q[1], q[2] - t[1] * 4), (q[0] + t[0] * 15, q[1], q[2] + t[1] * 15), 3.4, 3.4, steel, 10)
            bolt(f"portnut{i}{sy}", (q[0] + t[0] * 2, q[1], q[2] + t[1] * 2), d3, 6.5, 7, B.mat("gold"), False)
        # lambda sensor boss a little way down the pipe
        k_ = min(len(hp["pts"]) - 2, 3)
        q0, q1 = tuple(hp["pts"][k_][:2]), tuple(hp["pts"][k_ + 1][:2])
    silencers(model, c)


def silencers(model, c):
    """The silencers' own detail: inlet clamp and cone, the outlet, rivet bands."""
    if c.get("_silencers_done"):
        return
    c["_silencers_done"] = True
    ex = model["exhaust"]
    both, left, under = (ex.get("side") == k_ for k_ in ("both", "left", "under"))
    steel = B.mat("steel")
    for i, can in enumerate(ex.get("cans", [])):
        for sg in ((1, -1) if both or under else (1,) if left else (-1,)):
            if B.is_belly(ex, i):
                continue
            y0c, y1c = B.can_y(ex, i, under)
            a3, b3 = (can["a"][0], sg * y0c, can["a"][1]), (can["b"][0], sg * y1c, can["b"][1])
            L_ = math.sqrt(sum((b3[k] - a3[k]) ** 2 for k in range(3)))
            d3 = tuple((b3[k] - a3[k]) / L_ for k in range(3))
            rc = can["w"] / 2 * 0.86
            lathe(f"canclamp{i}{sg}", a3, d3, [(L_ * 0.02, rc * 0.62), (L_ * 0.02, rc * 0.62 + 3), (L_ * 0.02 + 14, rc * 0.62 + 3), (L_ * 0.02 + 14, rc * 0.62)], steel, 28)
            lathe(f"cancone{i}{sg}", a3, d3, [(L_ * 0.03, rc * 0.6), (L_ * 0.14, rc)], B.mat("exhaust"), 36)
            # the outlet (beenster, round 4): a dished end cap with an open tail pipe standing out of it,
            # dark with soot inside, so the gases have a way out
            drop(f"canend{i}{sg}", f"canbore{i}{sg}")
            if B.is_belly(ex, 0):
                # a stubby sport outlet: a rolled steel lip round one big open bore, dark with soot inside
                soot = surf("pipeblack", (0.0015, 0.0015, 0.0015), 0.0, 1.0, wear=0.0)
                soot.node_tree.nodes["Principled BSDF"].inputs["Specular IOR Level"].default_value = 0.0      # soot has no sheen, even edge-on
                bright = surf("outletlip", (0.82, 0.82, 0.84), 0.85, 0.42, wear=0.1)
                drop(f"can{i}{sg}", f"canbaffle{i}{sg}", f"cancore{i}{sg}")
                lathe(f"can{i}{sg}", a3, d3, [(L_ * 0.12, rc), (L_ * 0.92, rc)], B.mat("exhaust"), 40, caps=False)
                # hollow (beenster, sport round 2): a thin rolled lip round ONE wide open bore that runs most
                # of the way up the can, black with soot, with the perforated core far down inside it
                rb = rc * 0.8
                lathe(f"canend{i}{sg}", a3, d3, [(L_ * 0.9, rc + 1.5), (L_ - 4, rc + 1.5), (L_ + 1, rc - 1.5), (L_ + 1, rb + 2), (L_ - 3, rb)], bright, 40, caps=False)
                lathe(f"canbore{i}{sg}", a3, d3, [(L_ - 2.5, rb), (L_ * 0.3, rb), (L_ * 0.3, rb * 0.55), (L_ * 0.3, 0)], soot, 40, caps=False)
                lathe(f"cancore{i}{sg}", a3, d3, [(L_ * 0.3, rb * 0.55), (L_ * 0.62, rb * 0.55), (L_ * 0.62, rb * 0.47), (L_ * 0.3, rb * 0.47)], B.mat("exhaust_black"), 28, caps=False)
            elif not can.get("nip"):
                black = B.mat("exhaust_black")
                lathe(f"canend{i}{sg}", a3, d3, [(L_ * 0.9, rc + 2), (L_ - 12, rc * 1.0), (L_ - 3, rc * 0.93), (L_, rc * 0.84), (L_ - 1.5, rc * 0.76), (L_ - 12, rc * 0.68), (L_ - 14, rc * 0.5)], black, 40, caps=False)
                lathe(f"canpipe{i}{sg}", a3, d3, [(L_ - 20, rc * 0.52), (L_ + 9, rc * 0.52), (L_ + 11, rc * 0.5), (L_ + 11, rc * 0.455), (L_ + 9, rc * 0.44), (L_ - 20, rc * 0.44)], steel, 36, caps=False)
                lathe(f"canbore{i}{sg}", a3, d3, [(L_ + 8, rc * 0.438), (L_ - 34, rc * 0.438), (L_ - 34, 0)], surf("soot", (0.006, 0.006, 0.006), 0.0, 0.95, wear=0.0), 36, caps=False)
            for a_ in (0.3, 0.86):       # rivet bands
                lathe(f"canrivetband{i}{sg}{a_}", a3, d3, [(L_ * a_, rc + 0.4), (L_ * a_, rc + 2.2), (L_ * a_ + 12, rc + 2.2), (L_ * a_ + 12, rc + 0.4), (L_ * a_, rc + 0.4)], steel, 36, caps=False)      # a ring, not a disc: a capped band plugged the outlet


def radiator(model, c, heads, wp, wp_y):
    """A radiator with a real core: flat tubes and fins between two side tanks, a pressure cap, a fan on
    the back, brackets up to the frame, and hoses that run to the head and the pump with clips."""
    add, unit = B.add, B.unit
    r = model.get("radiator")
    drop("radiator", "radcore", "radfin", "radhose")
    if not r:
        return
    u = unit(r["angle"])
    n = (-u[1], u[0])
    if n[0] < 0:
        n = (-n[0], -n[1])
    a, Lr, hw = tuple(r["at"]), r["len"], r["w"] / 2
    plastic, alu, steel = B.mat("plastic"), B.mat("alu"), B.mat("steel")
    core = surf("radcore", (0.03, 0.03, 0.032), 0.7, 0.5, bump=(0.25, 0.9))

    def R(t, d):
        return add(add(a, u, t), n, d)
    W = 126
    B.prism("radcore", [R(8, hw - 3), R(Lr - 8, hw - 3), R(Lr - 8, -hw + 3), R(8, -hw + 3)], -W, W, core, 0.3)
    verts, faces = [], []
    k_ = 12.0
    while k_ < Lr - 12:        # the flat water tubes, standing a little proud front and back
        for d0, d1 in ((hw - 3, hw - 1.6), (-hw + 1.6, -hw + 3)):
            o = len(verts)
            for t_, d_ in ((k_, d0), (k_ + 2.2, d0), (k_ + 2.2, d1), (k_, d1)):
                q = R(t_, d_)
                verts += [B.V(q[0], -W, q[1]), B.V(q[0], W, q[1])]
            for e in range(4):
                e2 = (e + 1) % 4
                faces.append((o + 2 * e, o + 2 * e2, o + 2 * e2 + 1, o + 2 * e + 1))
        k_ += 8.0
    B.mesh_obj("radtubes", verts, faces, B.mat("alu_dark"), False)
    for sg in (-1, 1):
        B.pillow(f"radtank{sg}", [R(0, hw + 2), R(Lr, hw + 2), R(Lr, -hw - 2), R(0, -hw - 2)], min(sg * W, sg * (W + 24)), max(sg * W, sg * (W + 24)), plastic, 5, 8)
    for t_ in (4, Lr - 4):
        B.prism(f"radrail{t_}", [R(t_ - 4, hw), R(t_ + 4, hw), R(t_ + 4, -hw), R(t_ - 4, -hw)], -W, W, alu, 0.8)
    # pressure cap on the right tank
    q = R(Lr, 0)
    B.cone("radneck", P(q, -(W + 12)), P(add(q, u, 10), -(W + 12)), 13, 13, alu, 20)
    B.cone("radcap", P(add(q, u, 10), -(W + 12)), P(add(q, u, 17), -(W + 12)), 17, 16, steel, 12)
    # fan and shroud on the back
    back = (-n[0], 0, -n[1])
    fc = R(Lr * 0.5, -hw + 2)
    lathe("fanshroud", P(fc, 0), back, [(0, 74), (16, 74), (16, 70), (2, 70)], plastic, 48)
    lathe("fanhub", P(fc, 0), back, [(2, 22), (20, 22), (24, 16), (24, 0)], plastic, 24)
    for k in range(7):
        w = unit(360 / 7 * k)
        tip = add(add(a, u, Lr * 0.5 + 66 * w[0]), n, -hw + 2)
        ob = B.tube(f"fanblade{k}", [(fc[0] - n[0] * 10 + u[0] * 20 * w[0], 20 * w[1], fc[1] - n[1] * 10 + u[1] * 20 * w[0]),
                                     (fc[0] - n[0] * 10 + u[0] * 66 * w[0], 66 * w[1], fc[1] - n[1] * 10 + u[1] * 66 * w[0])], 6, plastic)
    # brackets from the top corners back to the frame
    nodes, half = c["nodes"], c["half"]
    tgt = nodes.get("D")
    if tgt:
        for sg in (-1, 1):
            q0 = R(Lr - 6, -hw)
            B.smooth_tube(f"radbracket{sg}", [P(q0, sg * 70), P(((q0[0] + tgt[0]) / 2, (q0[1] + tgt[1]) / 2 + 8), sg * (half(tgt) + 70) / 2), P(tgt, sg * half(tgt))], 5, B.mat("alu_dark"))
    # hoses with clips: top from the left tank to the front head, bottom from the right tank to the pump
    hose = surf("coolhose", (0.02, 0.02, 0.022), 0.0, 0.55)
    front = max(heads, key=lambda h: h[0][0]) if heads else None
    runs = []
    if front:
        hc, uh, rh, _ = front
        s0 = R(Lr - 26, -hw - 2)
        th = add(hc, uh, 10)
        B.cone("thermostat", P(th, rh + 2), P(th, rh + 22), 17, 15, B.mat("engine"), 20)
        runs.append(("radhose_t", [P(s0, W + 12), P(add(s0, n, -26), W + 14), P(((s0[0] + th[0]) / 2 - 10, (s0[1] + th[1]) / 2 + 26), W - 16), P(add(th, (0.3, 0.95), 36), rh + 34), P(th, rh + 20)]))
    s1 = R(22, -hw - 2)
    pq = add(wp, (0.5, 0.86), 60)
    runs.append(("radhose_b", [P(s1, -(W + 12)), P(add(s1, n, -24), -(W + 14)), P((s1[0] - 30, s1[1] - 90), -(W + 10)), P((pq[0] + 70, pq[1] + 60), wp_y - 16), P(add(pq, (0.5, 0.86), 26), wp_y - 12), P(pq, wp_y - 12)]))
    for nm, pts in runs:
        B.smooth_tube(nm, pts, 11.5, hose)
        for e_ in (0, -1):
            d0, d1 = pts[e_], pts[1 if e_ == 0 else -2]
            L_ = math.sqrt(sum((d1[k] - d0[k]) ** 2 for k in range(3))) or 1.0
            dd = tuple((d1[k] - d0[k]) / L_ for k in range(3))
            lathe(nm + f"clip{e_}", d0, dd, [(5, 11.8), (5, 13.4), (13, 13.4), (13, 11.8)], steel, 20)


def tyre_mat(tag, c, w):
    """Rubber for one tyre: a matt scrubbed tread, a satin sidewall, and fine moulded rings near the bead,
    laid out from the wheel's own centre."""
    m = bpy.data.materials.new("d3_tyre_" + tag)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.012, 0.012, 0.013, 1)
    geo, sep, comb = nt.nodes.new("ShaderNodeNewGeometry"), nt.nodes.new("ShaderNodeSeparateXYZ"), nt.nodes.new("ShaderNodeCombineXYZ")
    nt.links.new(geo.outputs["Position"], sep.inputs[0])
    sx, sz = nt.nodes.new("ShaderNodeMath"), nt.nodes.new("ShaderNodeMath")
    sx.operation = sz.operation = "SUBTRACT"
    sx.inputs[1].default_value, sz.inputs[1].default_value = c[0] * B.MM, c[1] * B.MM
    nt.links.new(sep.outputs["X"], sx.inputs[0])
    nt.links.new(sep.outputs["Z"], sz.inputs[0])
    nt.links.new(sx.outputs[0], comb.inputs["X"])
    nt.links.new(sz.outputs[0], comb.inputs["Y"])
    ln = nt.nodes.new("ShaderNodeVectorMath")
    ln.operation = "LENGTH"
    nt.links.new(comb.outputs[0], ln.inputs[0])
    rr, rt = w["rim_r"] * B.MM, w["tyre_r"] * B.MM
    side = nt.nodes.new("ShaderNodeMapRange")       # 1 on the sidewall, 0 on the tread
    side.inputs["From Min"].default_value, side.inputs["From Max"].default_value = rr + (rt - rr) * 0.62, rr + (rt - rr) * 0.8
    side.inputs["To Min"].default_value, side.inputs["To Max"].default_value = 1.0, 0.0
    nt.links.new(ln.outputs["Value"], side.inputs["Value"])
    rough = nt.nodes.new("ShaderNodeMapRange")
    rough.inputs["To Min"].default_value, rough.inputs["To Max"].default_value = 0.92, 0.5
    nt.links.new(side.outputs[0], rough.inputs["Value"])
    nt.links.new(rough.outputs[0], b.inputs["Roughness"])
    wave = nt.nodes.new("ShaderNodeMath")
    wave.operation = "SINE"
    mul = nt.nodes.new("ShaderNodeMath")
    mul.operation = "MULTIPLY"
    mul.inputs[1].default_value = 2 * math.pi / (2.4 * B.MM)
    nt.links.new(ln.outputs["Value"], mul.inputs[0])
    nt.links.new(mul.outputs[0], wave.inputs[0])
    band = nt.nodes.new("ShaderNodeMapRange")       # rings only in a band above the rim
    band.inputs["From Min"].default_value, band.inputs["From Max"].default_value = rr + (rt - rr) * 0.3, rr + (rt - rr) * 0.34
    band.inputs["To Min"].default_value, band.inputs["To Max"].default_value = 1.0, 0.0
    nt.links.new(ln.outputs["Value"], band.inputs["Value"])
    band2 = nt.nodes.new("ShaderNodeMapRange")
    band2.inputs["From Min"].default_value, band2.inputs["From Max"].default_value = rr + (rt - rr) * 0.16, rr + (rt - rr) * 0.2
    nt.links.new(ln.outputs["Value"], band2.inputs["Value"])
    amp0 = nt.nodes.new("ShaderNodeMath")
    amp0.operation = "MULTIPLY"
    nt.links.new(wave.outputs[0], amp0.inputs[0])
    nt.links.new(band.outputs[0], amp0.inputs[1])
    amp = nt.nodes.new("ShaderNodeMath")
    amp.operation = "MULTIPLY"
    nt.links.new(amp0.outputs[0], amp.inputs[0])
    nt.links.new(band2.outputs[0], amp.inputs[1])
    nz = nt.nodes.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value, nz.inputs["Detail"].default_value = 900.0, 4.0
    addn = nt.nodes.new("ShaderNodeMath")
    addn.operation = "ADD"
    nt.links.new(amp.outputs[0], addn.inputs[0])
    nt.links.new(nz.outputs["Fac"], addn.inputs[1])
    bp = nt.nodes.new("ShaderNodeBump")
    bp.inputs["Strength"].default_value, bp.inputs["Distance"].default_value = 0.3, 0.0005
    nt.links.new(addn.outputs[0], bp.inputs["Height"])
    nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    return m


def leather(quilt=False):
    """Seat leather (reference: stitched seat): a fine grain all over, and a diamond-quilted panel on the
    top where the rider sits, its grooves pressed into the surface."""
    key = "d3_leather" + ("_quilt" if quilt else "")
    if key in bpy.data.materials:
        return bpy.data.materials[key]
    m = bpy.data.materials.new(key)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.014, 0.014, 0.016, 1)
    b.inputs["Roughness"].default_value = 0.46
    b.inputs["Sheen Weight"].default_value = 0.3
    tc = nt.nodes.new("ShaderNodeTexCoord")
    grain = nt.nodes.new("ShaderNodeTexVoronoi")
    grain.inputs["Scale"].default_value = 900.0
    nt.links.new(tc.outputs["Object"], grain.inputs["Vector"])
    # quilting: a square grid turned 45 degrees in plan, grooves where the distance to a cell edge is small
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Rotation"].default_value = (0, 0, math.radians(45))
    mp.inputs["Scale"].default_value = (1, 1, 0)
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    qn = nt.nodes.new("ShaderNodeTexVoronoi")
    qn.feature = "DISTANCE_TO_EDGE"
    qn.inputs["Scale"].default_value = 1.0 / (46 * B.MM)
    qn.inputs["Randomness"].default_value = 0.0
    nt.links.new(mp.outputs[0], qn.inputs["Vector"])
    groove = nt.nodes.new("ShaderNodeMapRange")
    groove.inputs["From Min"].default_value, groove.inputs["From Max"].default_value = 0.0, 0.14
    groove.interpolation_type = "SMOOTHSTEP"
    nt.links.new(qn.outputs["Distance"], groove.inputs["Value"])
    geo, sep = nt.nodes.new("ShaderNodeNewGeometry"), nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(geo.outputs["True Normal"], sep.inputs[0])
    topm = nt.nodes.new("ShaderNodeMapRange")
    topm.inputs["From Min"].default_value, topm.inputs["From Max"].default_value = 0.8, 0.88
    nt.links.new(sep.outputs["Z"], topm.inputs["Value"])
    mix = nt.nodes.new("ShaderNodeMix")           # off the top panel the groove value is 1 (flat)
    mix.inputs["A"].default_value = 1.0
    if quilt:
        nt.links.new(topm.outputs[0], mix.inputs["Factor"])
    else:
        mix.inputs["Factor"].default_value = 0.0
    nt.links.new(groove.outputs[0], mix.inputs["B"])
    b1, b2 = nt.nodes.new("ShaderNodeBump"), nt.nodes.new("ShaderNodeBump")
    b1.inputs["Strength"].default_value, b1.inputs["Distance"].default_value = 0.45, 0.0003
    b2.inputs["Strength"].default_value, b2.inputs["Distance"].default_value = 0.7, 0.002
    nt.links.new(grain.outputs["Distance"], b1.inputs["Height"])
    nt.links.new(mix.outputs["Result"], b2.inputs["Height"])
    if quilt:
        nt.links.new(b1.outputs["Normal"], b2.inputs["Normal"])
        nt.links.new(b2.outputs["Normal"], b.inputs["Normal"])
    else:
        nt.links.new(b1.outputs["Normal"], b.inputs["Normal"])
    return m


def dashes(name, path, dash, gap, r, material):
    """Rows of stitches: short dashes along a 3D path (mm), all in one curve."""
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth, cu.bevel_resolution, cu.use_fill_caps = r * B.MM, 2, True
    acc, on = 0.0, True
    seg = [path[0]]
    for a_, b_ in zip(path, path[1:]):
        L_ = math.sqrt(sum((b_[k] - a_[k]) ** 2 for k in range(3)))
        t = 0.0
        while t < L_:
            lim = dash if on else gap
            step = min(L_ - t, lim - acc)
            t += step
            acc += step
            q = tuple(a_[k] + (b_[k] - a_[k]) * t / L_ for k in range(3))
            if on:
                seg.append(q)
            if acc >= lim - 1e-6:
                if on and len(seg) > 1:
                    sp = cu.splines.new("POLY")
                    sp.points.add(len(seg) - 1)
                    for pnt, v in zip(sp.points, seg):
                        pnt.co = (*B.V(*v), 1)
                on, acc, seg = not on, 0.0, [q]
    ob = bpy.data.objects.new(name, cu)
    bpy.context.collection.objects.link(ob)
    cu.materials.append(material)
    return ob


def seat(model, c):
    """The seat is a leather pad with no piping. Quilting is for the classics only: every other family gets
    plain leather, and the joins are sewn with a double row of stitches you can see, along each shoulder
    and across the pad where the rider's part ends."""
    body = model["body"]
    drop("seam")
    quilt = model["family"].lower() in ("classic", "cruiser")
    lm = leather(quilt)
    set_mat("seat", lm)
    st = next((s_ for s_ in body.get("seat", []) if s_.get("cls") == "seat"), None)
    if not st:
        return
    outline = B.shapes.dense(st["pts"], True, st.get("t", 1.0))
    xs = [q[0] for q in outline]
    x0, x1, spec = min(xs), max(xs), model.get("widths", {}).get("seat", B.WIDTHS["seat"])
    thread = surf("thread", (0.55, 0.55, 0.56), 0.0, 0.75, wear=0.0)

    def top(u, f_):
        tb = B.shapes.top_bottom(outline, x0 + (x1 - x0) * u)
        if not tb:
            return None
        w = B.shapes.lerp_table(spec["w"], u) * (0.3 + 0.7 * math.sqrt(1.0 - (1.0 - min(1.0, min(u, 1 - u) / 0.1)) ** 2))
        zc, h = (tb[0] + tb[1]) / 2, (tb[0] - tb[1]) / 2
        return x0 + (x1 - x0) * u, w * f_, zc + h * (1 - abs(f_) ** spec["n"]) ** (1 / spec["n"]) + 0.5
    for sg in (-1, 1):
        for row, f_ in enumerate((0.72, 0.77)):
            pts = [top(k / 100, f_) for k in range(8, 93, 2)]
            pts = [(q[0], sg * q[1], q[2]) for q in pts if q]
            if len(pts) > 3:
                dashes(f"stitch{sg}{row}", pts, 4.6, 2.4, 0.95, thread)
    # the cross seam: where the outline's top line steps up to the pillion pad
    tops = [(k / 100, top(k / 100, 0.0)) for k in range(20, 80, 2)]
    tops = [(u, q[2]) for u, q in tops if q]
    if len(tops) > 4:
        rise = max(range(1, len(tops)), key=lambda i: abs(tops[i][1] - tops[i - 1][1]))
        ux = tops[rise][0] + 0.03
        for row, du in enumerate((0.0, 0.012)):
            pts = [top(ux + du, f_ / 100) for f_ in range(-70, 71, 5)]
            pts = [q for q in pts if q]
            if len(pts) > 3:
                dashes(f"stitchx{row}", pts, 4.6, 2.4, 0.95, thread)


def tank_cap(model, c):
    """A filler cap that stands above the tank (beenster, 15 Nov): a bolted ring on a raised neck, a hinged
    lid above that, and a cover over the lock. The tank badges go: those will be decals."""
    drop("filler", "capbolt", "badge")
    steel = B.mat("steel")
    for i, cp in enumerate(model["body"].get("caps", [])):
        o = (cp["at"][0], 0, cp["at"][1] - 4)
        lathe(f"capneck{i}", o, (0, 0, 1), [(-10, 0), (-10, 48), (4, 48), (9, 45), (9, 0)], B.mat("alu_dark"), 56)
        lathe(f"capring{i}", o, (0, 0, 1), [(9, 44), (12, 44), (14, 41.5), (14, 36), (12.5, 35), (12.5, 0)], surf("capring", (0.5, 0.5, 0.52), 1.0, 0.25), 56)
        lathe(f"caplid{i}", o, (0, 0, 1), [(12.5, 34), (15.5, 33.5), (18, 30), (19.5, 20), (20, 0)], surf("caplid", (0.04, 0.04, 0.045), 0.9, 0.3), 56)
        for k in range(6):
            w = math.radians(60 * k + 30)
            bolt(f"capbolt{i}{k}", (o[0] + 39.5 * math.cos(w), 39.5 * math.sin(w), o[2] + 13.6), (0, 0, 1), 2.6, 1.8)
        B.cone(f"capkey{i}", (o[0] + 15, 0, o[2] + 19.4), (o[0] + 15, 0, o[2] + 21.6), 8.5, 8, steel, 24)
        B.prism(f"caphinge{i}", [(o[0] - 39, o[2] + 12), (o[0] - 28, o[2] + 12), (o[0] - 28, o[2] + 19), (o[0] - 39, o[2] + 19)], -10, 10, B.mat("alu_dark"), 1.2)
        B.tube(f"caphingepin{i}", [(o[0] - 34, -12, o[2] + 16.5), (o[0] - 34, 12, o[2] + 16.5)], 2.2, steel)


def rear_end(model, c):
    """Rear brake and footrests: a small caliper on its carrier, a drilled disc on a spoked carrier, ribbed
    footrests on their hangers, a gear lever and a brake pedal with toe pieces."""
    A = c["A"]
    wh = model["wheels"]["rear"]
    rc = model.get("rear_caliper")
    d = wh.get("disc")
    steel, dark, rubber = B.mat("steel"), B.mat("alu_dark"), B.mat("rubber")
    if d:
        drop("rear_disc", "rear_carrier")
        disc("rear", A, d, -82, -1, "drilled_220_rear", 5, 0)
    if rc and d:
        drop("rearcaliper")
        cc = (sum(q[0] for q in rc["body"]) / len(rc["body"]), sum(q[1] for q in rc["body"]) / len(rc["body"]))
        L_ = math.hypot(cc[0] - A[0], cc[1] - A[1])
        cc = (A[0] + (cc[0] - A[0]) * (d - 14) / L_, A[1] + (cc[1] - A[1]) * (d - 14) / L_)
        caliper("rearcaliper", A, cc, -82, -1, surf("caliper", (0.36, 0.36, 0.37), 0.9, 0.36), None, None, 0.6, False)
    for wheel_, cen in (("front", c["F"]), ("rear", A)):
        w = model["wheels"][wheel_]
        set_mat(f"{wheel_}_tyre", tyre_mat(wheel_, cen, w))
        # valve stem on the rim
        q = B.add(cen, B.unit(200), w["rim_r"] - 24)
        q1 = B.add(cen, B.unit(200), w["rim_r"] - 46)
        B.cone(f"{wheel_}_valve", (q[0], 0, q[1]), (q1[0], 14, q1[1]), 3.2, 2.6, rubber, 10)
        B.cone(f"{wheel_}_valvecap", (q1[0], 14, q1[1]), (q1[0] + (q1[0] - q[0]) * 0.3, 18, q1[1] + (q1[1] - q[1]) * 0.3), 3.4, 3.2, dark, 10)
    pegs = [tuple(s_["at"]) for s_ in model.get("hangers", []) if s_.get("circle")]
    drop("peg")
    for i, p in enumerate(pegs):
        for s in (-1, 1):
            prof = [(0, 0), (0, 15), (6, 15), (8, 12)]
            t = 12.0
            while t < 74:
                prof += [(t, 12), (t + 1.5, 13.6), (t + 4.5, 13.6), (t + 6, 12)]
                t += 9.0
            prof += [(80, 12), (82, 14), (86, 14), (88, 10), (88, 0)]
            lathe(f"peg{i}{s}", (p[0], s * 172, p[1]), (0, s, 0), prof, B.mat("alu"), 24)
            B.cone(f"pegpivot{i}{s}", (p[0] - 12, s * 170, p[1] - 12), (p[0] - 12, s * 184, p[1] - 12), 6, 6, steel, 6)
    if pegs:
        p = pegs[0]
        # gear lever on the left: pivot by the peg, arm forward to a rubber toe piece, a rod up to the gearbox
        piv = (p[0] + 6, p[1] + 26)
        toe = (p[0] + 135, p[1] + 8)
        B.cone("gearpivot", (piv[0], 176, piv[1]), (piv[0], 192, piv[1]), 11, 11, dark, 20)
        B.smooth_tube("gearlever", [(piv[0], 184, piv[1]), (piv[0] + 50, 190, piv[1] - 14), (toe[0], 190, toe[1])], 6, dark)
        lathe("geartoe", (toe[0], 186, toe[1]), (0, 1, 0), [(0, 5), (4, 5), (4, 10), (10, 11), (40, 11), (44, 9), (44, 0)], rubber, 20, sharp=False)
        cs_ = c["eng"]["countershaft"]
        B.tube("gearrod", [(piv[0] - 4, 178, piv[1] + 30), (cs_[0] + 40, 158, cs_[1] + 36)], 3.2, steel)
        B.cone("geararm", (piv[0], 180, piv[1]), (piv[0] - 4, 178, piv[1] + 30), 6, 5, dark, 12)
        # brake pedal on the right, and the rear master cylinder with its cup behind the hanger
        B.cone("brakepivot", (piv[0], -176, piv[1]), (piv[0], -192, piv[1]), 11, 11, dark, 20)
        B.smooth_tube("brakepedal", [(piv[0], -184, piv[1]), (piv[0] + 60, -196, piv[1] - 18), (toe[0] - 4, -200, toe[1] - 2)], 6, dark)
        B.prism("brakepedaltip", [(toe[0] - 14, toe[1] - 6), (toe[0] + 16, toe[1] - 4), (toe[0] + 16, toe[1] + 3), (toe[0] - 14, toe[1] + 2)], -226, -196, dark, 1.2)
        mcyl = (piv[0] - 36, piv[1] + 30)
        B.cone("rearmaster", (mcyl[0], -150, mcyl[1]), (mcyl[0] - 14, -150, mcyl[1] + 62), 10, 10, dark, 20)
        B.cone("rearmasterrod", (mcyl[0], -150, mcyl[1]), (piv[0] - 18, -176, piv[1] + 6), 3, 3, steel, 10)
        drop("rearres")
        B.cone("rearres", (mcyl[0] - 30, -150, mcyl[1] + 96), (mcyl[0] - 30, -150, mcyl[1] + 130), 14, 15, surf("rescup", (0.55, 0.5, 0.36), 0, 0.25, wear=0.0), 24)
        B.cone("rearrescap", (mcyl[0] - 30, -150, mcyl[1] + 130), (mcyl[0] - 30, -150, mcyl[1] + 137), 16.5, 16, B.mat("plastic"), 24)
        B.smooth_tube("rearreshose", [(mcyl[0] - 30, -150, mcyl[1] + 96), (mcyl[0] - 26, -150, mcyl[1] + 76), (mcyl[0] - 14, -150, mcyl[1] + 60)], 3.5, rubber)

        # heel plates (beenster, round 4): brushed aluminium, drilled, bolted to the footrest hanger behind
        # the rider's peg, so a boot rubs the plate and not the swingarm or the silencer
        hp = chaikin([(p[0] + 52, p[1] + 127), (p[0] + 34, p[1] + 74), (p[0] + 22, p[1] + 68), (p[0] - 12, p[1] + 66), (p[0] - 24, p[1] + 44),
                      (p[0] - 46, p[1] + 36), (p[0] - 78, p[1] + 50), (p[0] - 92, p[1] + 92), (p[0] - 76, p[1] + 138), (p[0] - 30, p[1] + 152), (p[0] + 18, p[1] + 148)], 2)
        hh = [(p[0] - 46, p[1] + 96), (p[0] - 22, p[1] + 88), (p[0] - 60, p[1] + 122), (p[0] - 34, p[1] + 116), (p[0] - 8, p[1] + 110),
              (p[0] - 42, p[1] + 140), (p[0] - 14, p[1] + 134)]
        sp = tuple(model["swingarm"]["pivot"])
        for s in (-1, 1):
            ya, yb = (181, 185) if s == 1 else (-185, -181)
            pl = B.prism(f"heelplate{s}", hp, ya, yb, brushed(), 0)
            # drilled, and cut away round the swingarm pivot nut
            drill(pl, [((q[0], s * 176, q[1]), (q[0], s * 190, q[1]), 6.2) for q in hh] + [((sp[0], s * 176, sp[1]), (sp[0], s * 190, sp[1]), 25)], 0)
            pl.data.polygons.foreach_set("use_smooth", [False] * len(pl.data.polygons))
            for q in ((p[0] + 30, p[1] + 108), (p[0] + 8, p[1] + 130)):
                lathe(f"heelspacer{s}{q[0]}", (q[0], s * 178, q[1]), (0, s, 0), [(0, 0), (0, 6), (3, 6), (3, 0)], steel, 12)
                bolt(f"heelbolt{s}{q[0]}", (q[0], s * 185, q[1]), (0, s, 0), 5.2, 2.6, steel)
        # on a beam frame the pivot plates stand wider than a tube frame: the whole footrest moves out to
        # sit on the plate's outer face instead of being buried in it
        for s in (-1, 1):
            hits = [side_hit(("spar",), p[0] + dx, p[1] + dz, s) for dx, dz in ((0, 0), (-20, 96), (20, 60), (6, 26), (40, 110), (-60, 60))]
            dy = max([0.0] + [abs(h_[1]) + 3 - 181 for h_ in hits if h_])
            if dy > 1:
                shift_y(("peg0", "pegpivot0", "gearpivot", "gearlever", "geartoe", "geararm", "brakepivot", "brakepedal", "heelplate", "heelspacer", "heelbolt"), s, dy)
    hugger(model, c)
    pillion_hangers(model, c, pegs)
    beam_frame(model, c)


def shift_y(prefixes, s, dy):
    """Move the built parts on side s (and the cutters that drill them) outwards by dy mm."""
    for o in list(bpy.data.objects):
        if o.type != "MESH" or not o.name.startswith(tuple(prefixes)) or not o.data.vertices:
            continue
        bpy.context.view_layer.update()
        if sum((o.matrix_world @ v.co).y for v in o.data.vertices) * s <= 0:
            continue
        for ob in [o] + [m.object for m in o.modifiers if m.type == "BOOLEAN" and m.object]:
            ob.location.y += s * dy * B.MM


def beam_frame(model, c):
    """A beam frame's own detail (reference: sport twin-spar frames): weld seams where the extruded beam
    meets the cast steering head and the cast pivot plate, the swingarm pivot cap and the engine mount bolts
    on the plate's outer face."""
    plates = model["frame"].get("plates", [])
    trees = _trees(("spar",))
    if not plates or not trees:
        return
    import shapes
    pp = shapes.dense(plates[0]["pts"], True, plates[0].get("t", 0.6))
    xh = max(q[0] for q in pp)
    sp = tuple(model["swingarm"]["pivot"])
    framem = bpy.data.objects[next(o.name for o in bpy.data.objects if o.name.startswith("spar"))].data.materials[0]
    steel = B.mat("steel")
    for s in (-1, 1):
        for n_, x in enumerate((xh - 135, sp[0] + 150)):
            tb = shapes.top_bottom(pp, x)
            if not tb:
                continue
            pts = []
            for k in range(13):
                z = tb[1] + 6 + (tb[0] - tb[1] - 12) * k / 12
                xx = x + 10 * math.sin(k / 12 * math.pi)        # the seam bows a little, as a hand-laid weld does
                y = _cast(trees, xx, z, "y", s)
                if y is not None:
                    pts.append((xx, s * (y - 0.8), z))
            if len(pts) > 3:
                weld_bead(f"sparweld{s}{n_}", pts, framem)
        y = _cast(trees, sp[0], sp[1], "y", s)
        if y is not None:
            lathe(f"pivotcap{s}", (sp[0], s * (y - 2), sp[1]), (0, s, 0), [(0, 0), (0, 24), (5, 24), (7, 21), (7, 13), (4, 13), (4, 0)], steel, 32)
            bolt(f"pivotnut{s}", (sp[0], s * (y + 2), sp[1]), (0, s, 0), 11, 5, steel, False)
        for n_, (dx, dz) in enumerate(((95, 150), (60, -62), (150, 40))):
            q = (sp[0] + dx, sp[1] + dz)
            y = _cast(trees, q[0], q[1], "y", s)
            if y is not None:
                lathe(f"mountboss{s}{n_}", (q[0], s * (y - 2), q[1]), (0, s, 0), [(0, 0), (0, 13), (3, 13), (4.5, 11), (4.5, 0)], framem, 20)
                bolt(f"sparbolt{s}{n_}", (q[0], s * (y + 2.5), q[1]), (0, s, 0), 6, 4, steel)


def weld_bead(name, pts, material, r=3.4, pitch=3.4):
    """A rippled weld bead along 3D points (mm): overlapping pools, each a little proud of the last."""
    dense = []
    for a_, b_ in zip(pts, pts[1:]):
        L_ = math.sqrt(sum((b_[k] - a_[k]) ** 2 for k in range(3)))
        n = max(1, int(L_ / pitch))
        dense += [tuple(a_[k] + (b_[k] - a_[k]) * j / n for k in range(3)) for j in range(n)]
    dense.append(pts[-1])
    for k, q in enumerate(dense):
        bpy.ops.mesh.primitive_uv_sphere_add(radius=r * B.MM, location=B.V(*q), segments=10, ring_count=6)
        ob = bpy.context.object
        ob.name = f"{name}_{k}"
        ob.scale = (1.25, 0.55, 1.0)
        ob.data.materials.append(material)
        bpy.ops.object.shade_smooth()


def hugger(model, c):
    """The rear hugger (beenster, round 4): a close guard over the front of the rear tyre that belongs to the
    swingarm and moves with the wheel. Moulded legs come down from its edge to bosses bolted on top of
    the arm; the undertray stays with the tail above it."""
    A, sw = c["A"], model["swingarm"]
    if model["family"].lower() not in ("naked", "sport", "cafe"):
        return
    drop("hugger")
    # the old guard under the tail goes: the hugger replaces it (beenster, 15 Nov)
    for i, shp in enumerate(model["body"].get("behind", [])):
        if "fender" in shp.get("cls", ""):
            for ob in list(bpy.data.objects):
                if ob.name == f"behind{i}":
                    bpy.data.objects.remove(ob, do_unlink=True)
    R_ = model["wheels"]["rear"]["tyre_r"]
    plastic = surf("huggerplastic", (0.03, 0.03, 0.033), 0.0, 0.42, bump=(0.2, 0.5), wear=0.15)
    g_ = 20 if model["family"].lower() in ("sport", "cafe") else 0      # sport: closer still to the tyre (beenster, sport round 2)
    B.arc_band("hugger", {"c": A, "r0": R_ + 30 - g_, "r1": R_ + 40 - g_ * 1.7, "a0": 20, "a1": 68}, 94, plastic)
    top = [tuple(q[:2]) for q in sw["outline"]]

    def arm_top(x):
        zs = []
        for i in range(len(top)):
            (x0, z0), (x1, z1) = top[i], top[(i + 1) % len(top)]
            if min(x0, x1) <= x <= max(x0, x1) and x0 != x1:
                zs.append(z0 + (z1 - z0) * (x - x0) / (x1 - x0))
        return max(zs)
    sides = (1, -1) if sw.get("cls") == "alu" else (1,)
    for s in sides:
        for k_, ang in enumerate((27, 50)):
            w = math.radians(ang)
            e = (A[0] + (R_ + 19 - g_ * 0.75) * math.cos(w), A[1] + (R_ + 19 - g_ * 0.75) * math.sin(w))
            fx = e[0] + (16 if k_ == 0 else 10)
            fz = arm_top(fx) - 2
            strap(f"huggerleg{s}{k_}", [(e[0], s * 84, e[1] + 4), (e[0] + 2, s * 100, e[1] - 6), ((e[0] + fx) / 2, s * 124, (e[1] + fz) / 2 + 12), (fx, s * 142, fz + 26), (fx, s * 146, fz + 5)],
                  [(24, 3.2), (22, 3.2), (17, 3.4), (13, 4.0), (13, 4.5)], plastic)
            lathe(f"huggerboss{s}{k_}", (fx, s * 146, fz), (0, 0, 1), [(0, 0), (0, 13), (7, 13), (9, 11), (9, 0)], plastic, 20)
            bolt(f"huggerbolt{s}{k_}", (fx, s * 146, fz + 9), (0, 0, 1), 5.5, 3, B.mat("steel"))


def pillion_hangers(model, c, pegs):
    """Pillion footrest hangers: a cast bracket each side, bolted to the seat rail at two points and reaching
    down and out to the peg, in place of the plain strut."""
    if len(pegs) < 2:
        return
    nodes, half = c["nodes"], c["half"]
    n3 = nodes.get("S3")
    if not n3:
        return
    drop("pillionhanger")
    cast = surf("hangercast", (0.09, 0.09, 0.1), 0.9, 0.42, bump=(0.25, 0.6), wear=0.2)
    pg = pegs[1]
    for s in (-1, 1):
        fw = nodes.get("F", (n3[0] + 200, n3[1] - 110))
        Lf = math.hypot(fw[0] - n3[0], fw[1] - n3[1])
        up2 = (n3[0] + (fw[0] - n3[0]) * 52 / Lf, n3[1] + (fw[1] - n3[1]) * 52 / Lf)      # second bolt along the lower rail
        for k_, q in enumerate((n3, up2)):
            yq = c.get("SUB_OUT", 132)
            strap(f"pillionhanger{s}{k_}", [(q[0], s * (yq + 13), q[1] - 4), (q[0] - 4, s * (yq + 22), q[1] - 30), ((q[0] + pg[0]) / 2, s * (yq + 150) / 2 + s * 6, (q[1] + pg[1]) / 2), (pg[0], s * 164, pg[1] + 4)],
                  [(11, 4), (10, 4), (9, 4.5), (11, 5)], cast)
            lathe(f"pillionhangerboss{s}{k_}", (q[0], s * (yq + 8), q[1] - 4), (0, s, 0), [(0, 0), (0, 11), (8, 11), (10, 9), (10, 0)], cast, 16)
            bolt(f"pillionhangerbolt{s}{k_}", (q[0], s * (yq + 18), q[1] - 4), (0, s, 0), 5, 3, B.mat("steel"))
        lathe(f"pillionhangereye{s}", (pg[0], s * 157, pg[1]), (0, s, 0), [(0, 0), (0, 15), (14, 15), (16, 12), (16, 0)], cast, 20)



def welds(model, c):
    """Fillet welds at every frame joint (beenster, round 4; reference: frame fillet welds): where a tube
    ends on another tube a bead runs right round the joint, following the saddle where the two meet,
    laid in overlapping ripples and painted over with the frame."""
    from mathutils import Vector
    runs = c.get("frame_runs", [])
    R3 = [(nm, [Vector(q) for q in pts], r) for nm, pts, r in runs]

    def near(E, pts):
        """Closest point on a run to E: (distance, axis direction there, whether it is an end of the run)."""
        best = (1e9, None, False)
        for i in range(len(pts) - 1):
            a_, b_ = pts[i], pts[i + 1]
            ab = b_ - a_
            t = max(0.0, min(1.0, (E - a_).dot(ab) / ab.length_squared))
            dist = (E - (a_ + ab * t)).length
            if dist < best[0]:
                end = (i == 0 and t < 0.03) or (i == len(pts) - 2 and t > 0.97)
                best = (dist, ab.normalized(), end, a_)
        return best
    n_ = 0
    for ti, (nm, pts, r) in enumerate(R3):
        if nm == "head":
            continue
        for E, nxt in ((pts[0], pts[1]), (pts[-1], pts[-2])):
            d = (nxt - E).normalized()
            host = None
            for hi, (hn, hp_, hr) in enumerate(R3):
                if hi == ti:
                    continue
                dist, h, end, h0 = near(E, hp_)
                if dist > hr + 2:
                    continue
                if end and (hr < r or (hr == r and hi > ti)):
                    continue                      # two tube ends meet: only the smaller (or later) one is welded on
                al = abs(d.dot(h))                # 0 = square on, 1 = end to end
                key = (al < 0.8, not end, hr, -al)
                if host is None or key > host[5]:
                    host = (h, h0, hr, hn, al, key)
            if not host:
                continue
            h, h0, R_, hn, al, _ = host
            e1 = d.cross(h)
            if al >= 0.8 or e1.length < 0.05:
                # tubes joined end to end, or nearly: a plain ring of weld round the butt joint itself
                e1 = d.cross(Vector((0, 1, 0)))
                if e1.length < 0.05:
                    e1 = d.cross(Vector((0, 0, 1)))
                e1.normalize()
                e2 = d.cross(e1)
                rr = max(r, R_ if al >= 0.8 else r) - 1.2
                seg = max(48, int(2 * math.pi * rr / 0.8))
                path = [E + d * (r * 0.55) + (e1 * math.cos(2 * math.pi * k / seg) + e2 * math.sin(2 * math.pi * k / seg)) * rr for k in range(seg)]
            else:
                e1.normalize()
                e2 = d.cross(e1)
                path = None
            if path is None:
                seg = max(48, int(2 * math.pi * r / 0.8))
            for k in range(seg if path is None else 0):
                path = path if k else []
                w = 2 * math.pi * k / seg
                wv = (E - h0) + (e1 * math.cos(w) + e2 * math.sin(w)) * (r + 0.6)
                wp = wv - h * wv.dot(h)
                dp = d - h * d.dot(h)
                a_, b_, c_ = dp.dot(dp), 2 * wp.dot(dp), wp.dot(wp) - (R_ + 0.6) ** 2
                disc = b_ * b_ - 4 * a_ * c_
                t = (-b_ + math.sqrt(max(0.0, disc))) / (2 * a_)
                t = min(t, 3.0 * R_)
                path.append(h0 + wv + d * t)
            seg = len(path)
            # the bead: a round section that swells and pinches along the joint, one ripple every 4 mm or so
            L_ = sum((path[k] - path[k - 1]).length for k in range(seg))
            nr = max(8, round(L_ / 3.2))
            verts, faces, m = [], [], 8
            for k in range(seg):
                tg = (path[(k + 1) % seg] - path[k - 1]).normalized()
                out = (path[k] - (E + d * (path[k] - E).dot(d))).normalized()
                side = tg.cross(out).normalized()
                ph = (k / seg * nr) % 1.0
                rb = 2.5 + 0.75 * (1 - ph) ** 0.7 * min(1.0, ph * 5)
                for j in range(m):
                    a2 = 2 * math.pi * j / m
                    q = path[k] + (out * math.cos(a2) + side * math.sin(a2)) * rb
                    verts.append(B.V(q.x, q.y, q.z))
            for k in range(seg):
                k2 = (k + 1) % seg
                for j in range(m):
                    j2 = (j + 1) % m
                    faces.append((k * m + j, k * m + j2, k2 * m + j2, k2 * m + j))
            ob = B.mesh_obj(f"weld{n_}", verts, faces, c["framem"], True, 0, False)
            B.bm_fix_normals(ob)
            n_ += 1
    print("welds:", n_)


SHOTS = {
    "fork": ((2300, -1250, 900), (1330, -60, 640), 60),      # close-up cameras: where from, looking at, lens (mm of bike space)
    "midl": ((560, 1700, 620), (560, 0, 470), 50),
    "midr": ((560, -1700, 620), (560, 0, 470), 50),
    "stubby": ((-150, -820, 420), (440, -150, 270), 70),
    "chain": ((120, 950, 470), (140, 108, 330), 75),
    "chain_front": ((900, 1000, 520), (500, 108, 400), 75),
    "nose": ((2500, -620, 1020), (1420, 0, 840), 60),
    "cockpit": ((250, -260, 1420), (1250, 0, 900), 40),
    "wheel": ((2250, -1050, 520), (1400, -100, 330), 62),
    "lamp": ((2350, -900, 1150), (1230, -40, 930), 70),
    "bars": ((560, -700, 1500), (1080, -60, 1060), 50),
    "engine": ((1250, -1500, 700), (800, -100, 480), 50),
    "engine_l": ((500, 1500, 650), (800, 100, 480), 50),
    "seat": ((-250, -900, 1500), (420, 0, 880), 55),
    "rear": ((-700, -1150, 600), (60, -90, 330), 60),
    "rearq": ((-2500, -2700, 1500), (650, 0, 560), 70),
    "rearq_front": ((3600, -2600, 1450), (700, 0, 520), 70),
    "rearq_low": ((-1500, -1900, 620), (480, -40, 520), 55),
    "service": ((1500, 1150, 1750), (830, 0, 770), 60, ("tank", "cap")),
    "indicator": ((1900, -900, 950), (1270, -180, 830), 110),
    "junction": ((330, -900, 980), (440, -120, 760), 85),
    "heel": ((640, -950, 620), (560, -150, 440), 85),
    "grip": ((700, -620, 1330), (1060, -300, 1050), 85),
    "outlet": ((-650, -420, 1150), (80, -40, 860), 80),
    "tail": ((-1150, -520, 1180), (-60, 0, 900), 70),
    "prow": ((2300, 520, 330), (1150, 0, 330), 60, ("front", "guard", "shroud", "fork", "caliper")),
    "clocks": ((960, 0, 1250), (1225, 0, 1010), 80),
    "glamp": ((-1150, -560, 930), (-250, 0, 640), 70),
    "cowlgap": ((1950, -950, 1020), (1240, -170, 800), 90),
    "tankbase": ((500, -1500, 1050), (860, -100, 820), 80),
    "hugger": ((-250, 1050, 900), (260, 90, 500), 70),
    "weld": ((1250, -820, 1080), (930, -120, 740), 85),
    "weld2": ((420, -1000, 900), (640, -140, 700), 70),
    "guard": ((2350, -1150, 760), (1430, -60, 480), 62),
    "guard_front": ((2500, -420, 900), (1440, 0, 500), 70),
    "disc": ((1700, -1900, 380), (1470, -80, 300), 120),
}


def shots(build3d, name, tags, samples=24):
    sc = bpy.context.scene
    build3d.studio(sc)
    sc.cycles.samples = samples
    out = os.path.join(build3d.ROOT, "out", "renders")
    os.makedirs(out, exist_ok=True)
    for tag in tags:
        loc, tgt, lens = SHOTS[tag][:3]
        hide = [o for o in bpy.data.objects if len(SHOTS[tag]) > 3 and o.name.startswith(SHOTS[tag][3])]
        for o in hide:
            o.hide_render = True
        cam = bpy.data.cameras.new(tag)
        cam.lens = lens
        ob = bpy.data.objects.new(tag, cam)
        sc.collection.objects.link(ob)
        ob.location = build3d.V(*loc)
        ob.rotation_euler = build3d._look(ob.location, build3d.V(*tgt))
        sc.camera = ob
        sc.render.resolution_x, sc.render.resolution_y = (1600, 1000) if tag.startswith("rearq") else (1400, 1000)
        sc.render.filepath = os.path.join(out, f"{name}_shot_{tag}.png")
        bpy.ops.render.render(write_still=True)
        for o in hide:
            o.hide_render = False
    return out


def run(build3d, model, c):
    global B
    B = build3d
    c = dict(c)
    c["model_hip"] = model["geometry"].get("hip_x", 520)
    front_end(model, c)
    headlamp(model, c)
    round_lamp(model, c)
    clocks(model, c)
    controls(model, c)
    engine(model, c)
    silencers(model, c)        # (engines not yet detailed return early, before their exhaust)
    seat(model, c)
    tank_cap(model, c)
    rear_end(model, c)
    tail_lamp(model, c)
    guard_lamp(model, c)
    plate_mount(model, c)
    bubble_screen(model, c)
    chain(model, c)
    indicators(model, c)
    cables(model, c)
    mudguard_fixings(model, c)
    welds(model, c)
