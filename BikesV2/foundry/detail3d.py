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
        bolt(f"{tag}_hubbolt{y}{k}", (q[0], y + s * 3, q[1]), (0, s, 0), 5.5, 4.5)
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
    slab("dashspeed", dc, n, u, [(-18, -16), (18, -16), (18, 10), (-18, 10)], 0.6, 1.1, glow("digits", (0.85, 0.9, 1.0), 0.35), 0)
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


SHOTS = {
    "fork": ((2300, -1250, 900), (1330, -60, 640), 60),      # close-up cameras: where from, looking at, lens (mm of bike space)
    "wheel": ((2250, -1050, 520), (1400, -100, 330), 62),
    "lamp": ((2350, -900, 1150), (1230, -40, 930), 70),
    "bars": ((560, -700, 1500), (1080, -60, 1060), 50),
    "engine": ((900, -1500, 560), (800, -100, 440), 55),
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
