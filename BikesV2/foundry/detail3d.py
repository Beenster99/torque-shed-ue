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


def lathe(name, p0, d, prof, material, segs=40, sharp=True):
    """Turn a profile [(t along the axis, radius)] about the axis through p0 (mm, 3D) along unit d."""
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
            q = a + ax * t + side * (r * math.cos(w)) + up * (r * math.sin(w))
            verts.append(B.V(q.x, q.y, q.z))
    for j in range(m - 1):
        for k in range(segs):
            k2 = (k + 1) % segs
            faces.append((j * segs + k, j * segs + k2, (j + 1) * segs + k2, (j + 1) * segs + k))
    faces += [tuple(range(segs - 1, -1, -1)), tuple((m - 1) * segs + k for k in range(segs))]
    ob = B.mesh_obj(name, verts, faces, material, True, 0, sharp)
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


def disc(tag, c, d, y, s, game_id, arms=6, buttons=10, carrier=None):
    """A brake disc as the game draws its rotors (the drilled steel ring is the game's own part), on a
    carrier made the way real ones are: a hub ring bolted to the wheel, a thin outer ring, and swept arms
    crossing between them so the carrier is a lattice, with the rotor floating on buttons."""
    add, unit = B.add, B.unit
    carrier = carrier or surf("carrier", (0.1, 0.1, 0.11), 0.95, 0.34)
    path = os.path.join(os.path.dirname(B.ROOT), "Data", "Parts", "Redrawn", "disc", game_id + ".glb")
    rin = d - 36
    if os.path.exists(path):
        pos, idx = glb_prim(path, "lod0", 0)
        r0 = max(math.hypot(p[1], p[2]) for p in pos)
        k = d / (r0 * 1000.0)
        rin = min(math.hypot(p[1], p[2]) for p in pos) * 1000.0 * k
        verts = [B.V(c[0] + p[1] * 1000 * k, y + s * p[0] * 1000, c[1] + p[2] * 1000 * k) for p in pos]
        faces = [(idx[t], idx[t + 1], idx[t + 2]) for t in range(0, len(idx), 3)]
        ob = B.mesh_obj(f"{tag}_rotor{y}", verts, faces, surf("rotor", (0.5, 0.5, 0.51), 1.0, 0.3, wear=0.35), True)
        B.bm_fix_normals(ob)
    else:
        B.revolve(f"{tag}_rotor{y}", [(d, y + 2.5), (d, y - 2.5), (rin, y - 2.5), (rin, y + 2.5)], c, B.mat("steel"), 64)
    ya, yb = y - 2.2, y + 2.2
    ro, rh = rin - 1.5, min(66.0, rin * 0.5)
    B.revolve(f"{tag}_carout{y}", [(ro + 4, yb), (ro + 4, ya), (ro - 8, ya), (ro - 8, yb)], c, carrier, 72)
    B.revolve(f"{tag}_carhub{y}", [(rh, yb + 1), (rh, ya - 1), (rh - 24, ya - 1), (rh - 24, yb + 1)], c, carrier, 48)
    sweep = 150.0 / arms
    for k in range(arms):
        a0 = 360.0 / arms * k + 7
        for sg in (-1, 1):
            left, right = [], []
            for n in range(9):
                u = n / 8
                r = rh - 3 + (ro - 5 - rh + 3) * u
                a = a0 + sg * sweep * (u ** 0.85)
                w = (7.5 - 3.0 * u) / r
                left.append(add(c, unit(a - math.degrees(w)), r))
                right.append(add(c, unit(a + math.degrees(w)), r))
            B.prism(f"{tag}_arm{y}{k}{sg}", left + right[::-1], ya, yb, carrier, 0.8)
    nb_ = 6 if arms >= 6 else 5
    for k in range(nb_):
        q = add(c, unit(360.0 / nb_ * k + 30), rh - 12)
        bolt(f"{tag}_hubbolt{y}{k}", (q[0], y + s * 3, q[1]), (0, s, 0), min(5.5, rh * 0.09), 4.5)
    for k in range(buttons):
        q = add(c, unit(360.0 / buttons * k + 12), rin + 0.5)
        B.cone(f"{tag}_button{y}{k}", (q[0], y - 4.6, q[1]), (q[0], y + 4.6, q[1]), 7.2, 7.2, B.mat("steel"), 18)
        B.cone(f"{tag}_buttonin{y}{k}", (q[0], y - 4.9, q[1]), (q[0], y + 4.9, q[1]), 3.6, 3.6, carrier, 12)
    # the flange the carrier bolts to, out from the hub
    B.cone(f"{tag}_flange{y}", (c[0], s * 30, c[1]), (c[0], y - s * 2, c[1]), 50, rh - 3, B.mat("alu_dark"), 40)
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
    # the display: a slim tablet on a bracket, its face square to the rider's eyes
    dash = next((s_ for i, s_ in head if s_ is not house and s_.get("cls") == "plastic" and "pts" in s_), None)
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
        prof = [(0, 12), (0, 22.5), (4, 22.5), (6, 17.4)]
        t = 9.0
        while t < L_ - 12:
            prof += [(t, 17.4), (t + 1.2, 16.0), (t + 2.6, 16.0), (t + 3.8, 17.4)]
            t += 10.0
        prof += [(L_ - 5, 17.6), (L_ - 2, 18.6), (L_, 18.0), (L_, 11)]
        lathe(f"grip{s}", g0, d, prof, rubber, 36, sharp=False)
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
        # the lever: bows forward from the pivot, then back towards the grip to a ball end
        pts = [(piv[0], yc + s * 2, piv[2]), (piv[0] + 16, s * 232, piv[2] - 2), (piv[0] + 22, s * 280, piv[2] - 4), (piv[0] + 14, s * 330, piv[2] - 6), (piv[0] + 2, s * 372, piv[2] - 7)]
        ob = B.smooth_tube(f"lever{s}", pts, 5.6, satin)
        B.cone(f"leverroot{s}", (piv[0] - 6, yc + s * 1, piv[2]), (piv[0] + 14, yc + s * 26, piv[2] - 1), 9, 6.5, satin, 16)
        bpy.ops.mesh.primitive_uv_sphere_add(radius=8.2 * B.MM, location=B.V(*pts[-1]), segments=20, ring_count=12)
        bpy.context.object.name = f"leverball{s}"
        bpy.context.object.data.materials.append(satin)
        bpy.ops.object.shade_smooth()
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
        B.cone(f"mirrorboss{s}", root, (root[0], root[1], root[2] + 10), 8, 7, satin, 6)
        B.smooth_tube(f"mstalk{s}", [(root[0], root[1], root[2] + 8), (root[0] + 10, yc + s * 8, root[2] + 62), (mc[0] + 22, s * 262, mc[1] if False else mc[2] - 30), (mc[0] + 20, s * 290, mc[2] - 6)], 5.2, satin)
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
        prof = [(-6, r - 12), (0, r - 12), (0, r + 5), (9, r + 5), (9, r - 9)]
        k = 16.0
        while k < cy["length"] - 8:
            prof += [(k, r - 9), (k, r + 1), (k + 2.4, r + 1), (k + 2.4, r - 9)]
            k += 8.5
        prof += [(cy["length"] + 2, r - 9)]
        lathe(f"barrel{i}", P(base, 0), d3, prof, silver, 44)
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
        B.cone(f"plugcap{i}", P(pc, 0), P(add(pc, u, 30), 0), 10.5, 8.5, rubber, 16)
        B.smooth_tube(f"pluglead{i}", [P(add(pc, u, 28), 0), P(add(add(pc, u, 60), n, 20), 14), P(add(add(pc, u, 70), n, 80), 30)], 3.6, rubber)
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
    # the clamp where the headers enter the silencer
    for i, can in enumerate(ex.get("cans", [])):
        for sg in ((1, -1) if both or under else (1,) if left else (-1,)):
            y0c, y1c = (58, 58) if under else (218, 228)
            a3, b3 = (can["a"][0], sg * y0c, can["a"][1]), (can["b"][0], sg * y1c, can["b"][1])
            L_ = math.sqrt(sum((b3[k] - a3[k]) ** 2 for k in range(3)))
            d3 = tuple((b3[k] - a3[k]) / L_ for k in range(3))
            rc = can["w"] / 2 * 0.86
            lathe(f"canclamp{i}{sg}", a3, d3, [(L_ * 0.02, rc * 0.62), (L_ * 0.02, rc * 0.62 + 3), (L_ * 0.02 + 14, rc * 0.62 + 3), (L_ * 0.02 + 14, rc * 0.62)], steel, 28)
            lathe(f"cancone{i}{sg}", a3, d3, [(L_ * 0.03, rc * 0.6), (L_ * 0.14, rc)], B.mat("exhaust"), 36)
            for a_ in (0.3, 0.86):       # rivet bands
                lathe(f"canrivetband{i}{sg}{a_}", a3, d3, [(L_ * a_, rc + 0.4), (L_ * a_, rc + 2.2), (L_ * a_ + 12, rc + 2.2), (L_ * a_ + 12, rc + 0.4)], steel, 36)


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


def leather():
    """Seat leather (reference: stitched seat): a fine grain all over, and a diamond-quilted panel on the
    top where the rider sits, its grooves pressed into the surface."""
    key = "d3_leather"
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
    grain.inputs["Scale"].default_value = 1400.0
    nt.links.new(tc.outputs["Object"], grain.inputs["Vector"])
    # quilting: a square grid turned 45 degrees in plan, grooves where the distance to a cell edge is small
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Rotation"].default_value = (0, 0, math.radians(45))
    mp.inputs["Scale"].default_value = (1, 1, 0)
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    quilt = nt.nodes.new("ShaderNodeTexVoronoi")
    quilt.feature = "DISTANCE_TO_EDGE"
    quilt.inputs["Scale"].default_value = 1.0 / (46 * B.MM)
    quilt.inputs["Randomness"].default_value = 0.0
    nt.links.new(mp.outputs[0], quilt.inputs["Vector"])
    groove = nt.nodes.new("ShaderNodeMapRange")
    groove.inputs["From Min"].default_value, groove.inputs["From Max"].default_value = 0.0, 0.14
    groove.interpolation_type = "SMOOTHSTEP"
    nt.links.new(quilt.outputs["Distance"], groove.inputs["Value"])
    geo, sep = nt.nodes.new("ShaderNodeNewGeometry"), nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(geo.outputs["True Normal"], sep.inputs[0])
    topm = nt.nodes.new("ShaderNodeMapRange")
    topm.inputs["From Min"].default_value, topm.inputs["From Max"].default_value = 0.8, 0.88
    nt.links.new(sep.outputs["Z"], topm.inputs["Value"])
    mix = nt.nodes.new("ShaderNodeMix")           # off the top panel the groove value is 1 (flat)
    mix.inputs["A"].default_value = 1.0
    nt.links.new(topm.outputs[0], mix.inputs["Factor"])
    nt.links.new(groove.outputs[0], mix.inputs["B"])
    b1, b2 = nt.nodes.new("ShaderNodeBump"), nt.nodes.new("ShaderNodeBump")
    b1.inputs["Strength"].default_value, b1.inputs["Distance"].default_value = 0.25, 0.0002
    b2.inputs["Strength"].default_value, b2.inputs["Distance"].default_value = 0.7, 0.002
    nt.links.new(grain.outputs["Distance"], b1.inputs["Height"])
    nt.links.new(mix.outputs["Result"], b2.inputs["Height"])
    nt.links.new(b1.outputs["Normal"], b2.inputs["Normal"])
    nt.links.new(b2.outputs["Normal"], b.inputs["Normal"])
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
    """The seat is a leather pad: no piping. A quilted top panel, plain sides, and the join between them
    sewn with a double row of stitches."""
    body = model["body"]
    drop("seam")
    lm = leather()
    set_mat("seat", lm)
    st = next((s_ for s_ in body.get("seat", []) if s_.get("cls") == "seat"), None)
    if not st:
        return
    outline = B.shapes.dense(st["pts"], True, st.get("t", 1.0))
    xs = [q[0] for q in outline]
    x0, x1, spec = min(xs), max(xs), model.get("widths", {}).get("seat", B.WIDTHS["seat"])
    thread = surf("thread", (0.25, 0.25, 0.26), 0.0, 0.7, wear=0.0)
    for sg in (-1, 1):
        for row, f_ in enumerate((0.74, 0.79)):
            pts = []
            for k in range(8, 93, 2):
                u = k / 100
                tb = B.shapes.top_bottom(outline, x0 + (x1 - x0) * u)
                if not tb:
                    continue
                w = B.shapes.lerp_table(spec["w"], u) * (0.3 + 0.7 * math.sqrt(1.0 - (1.0 - min(1.0, min(u, 1 - u) / 0.1)) ** 2))
                zc, h = (tb[0] + tb[1]) / 2, (tb[0] - tb[1]) / 2
                pts.append((x0 + (x1 - x0) * u, sg * w * f_, zc + h * (1 - f_ ** spec["n"]) ** (1 / spec["n"]) + 0.3))
            if len(pts) > 3:
                dashes(f"stitch{sg}{row}", pts, 4.2, 2.6, 0.75, thread)


def tank_cap(model, c):
    """A flush filler cap: a bolted ring let into the tank top, a hinged lid, and a cover over the lock."""
    drop("filler", "capbolt")
    steel = B.mat("steel")
    for i, cp in enumerate(model["body"].get("caps", [])):
        o = (cp["at"][0], 0, cp["at"][1] - 3)
        lathe(f"capring{i}", o, (0, 0, 1), [(-8, 0), (-8, 46), (2, 46), (4.5, 43), (4.5, 37), (3, 36), (3, 0)], B.mat("alu_dark"), 48)
        lathe(f"caplid{i}", o, (0, 0, 1), [(3, 35), (5.5, 34), (7, 28), (7.5, 0)], surf("caplid", (0.04, 0.04, 0.045), 0.9, 0.3), 48)
        for k in range(5):
            w = math.radians(72 * k + 36)
            bolt(f"capbolt{i}{k}", (o[0] + 40 * math.cos(w), 40 * math.sin(w), o[2] + 4.5), (0, 0, 1), 2.8, 1.6)
        B.cone(f"capkey{i}", (o[0] + 16, 0, o[2] + 7), (o[0] + 16, 0, o[2] + 9), 8, 7.5, steel, 20)
        B.prism(f"caphinge{i}", [(o[0] - 37, o[2] + 4), (o[0] - 27, o[2] + 4), (o[0] - 27, o[2] + 9), (o[0] - 37, o[2] + 9)], -9, 9, B.mat("alu_dark"), 1)


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
        B.cone("rearmaster", (mcyl[0], -168, mcyl[1]), (mcyl[0] - 14, -168, mcyl[1] + 62), 10, 10, dark, 20)
        B.cone("rearmasterrod", (mcyl[0], -168, mcyl[1]), (piv[0] - 18, -176, piv[1] + 6), 3, 3, steel, 10)
        drop("rearres")
        B.cone("rearres", (mcyl[0] - 30, -150, mcyl[1] + 96), (mcyl[0] - 30, -150, mcyl[1] + 130), 14, 15, surf("rescup", (0.55, 0.5, 0.36), 0, 0.25, wear=0.0), 24)
        B.cone("rearrescap", (mcyl[0] - 30, -150, mcyl[1] + 130), (mcyl[0] - 30, -150, mcyl[1] + 137), 16.5, 16, B.mat("plastic"), 24)
        B.smooth_tube("rearreshose", [(mcyl[0] - 30, -150, mcyl[1] + 96), (mcyl[0] - 26, -160, mcyl[1] + 76), (mcyl[0] - 14, -168, mcyl[1] + 60)], 3.5, rubber)


SHOTS = {
    "fork": ((2300, -1250, 900), (1330, -60, 640), 60),      # close-up cameras: where from, looking at, lens (mm of bike space)
    "wheel": ((2250, -1050, 520), (1400, -100, 330), 62),
    "lamp": ((2350, -900, 1150), (1230, -40, 930), 70),
    "bars": ((560, -700, 1500), (1080, -60, 1060), 50),
    "engine": ((1250, -1500, 700), (800, -100, 480), 50),
    "engine_l": ((500, 1500, 650), (800, 100, 480), 50),
    "seat": ((-250, -900, 1500), (420, 0, 880), 55),
    "rear": ((-700, -1150, 600), (60, -90, 330), 60),
}


def shots(build3d, name, tags, samples=24):
    sc = bpy.context.scene
    build3d.studio(sc)
    sc.cycles.samples = samples
    out = os.path.join(build3d.ROOT, "out", "renders")
    os.makedirs(out, exist_ok=True)
    for tag in tags:
        loc, tgt, lens = SHOTS[tag]
        cam = bpy.data.cameras.new(tag)
        cam.lens = lens
        ob = bpy.data.objects.new(tag, cam)
        sc.collection.objects.link(ob)
        ob.location = build3d.V(*loc)
        ob.rotation_euler = build3d._look(ob.location, build3d.V(*tgt))
        sc.camera = ob
        sc.render.resolution_x, sc.render.resolution_y = 1400, 1000
        sc.render.filepath = os.path.join(out, f"{name}_shot_{tag}.png")
        bpy.ops.render.render(write_still=True)
    return out


def run(build3d, model, c):
    global B
    B = build3d
    c = dict(c)
    c["model_hip"] = model["geometry"].get("hip_x", 520)
    front_end(model, c)
    headlamp(model, c)
    controls(model, c)
    engine(model, c)
    seat(model, c)
    tank_cap(model, c)
    rear_end(model, c)
