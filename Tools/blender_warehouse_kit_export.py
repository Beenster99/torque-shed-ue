"""The warehouse kit (Tools/blender_warehouse_kit.py) cut into pieces for the game: World/WorldRideWarehouses.cpp
lays them along a real building's outline, so one design fits any size of shed.

For each design:
  long:<letter>    one bay of a long wall (its sheeting, plinth and whatever the letter stands for), the
                   design's long bay wide, up to the long walls' top
  gable:<letter>   one bay of a gable end, its gable bay wide, up to the eaves (or the parapet)
  gabletop         the gable above that, the design's whole depth wide (none on a flat roof)
  roof:P|L|S|SL    one long bay's length of the roof right across: plain, with rooflights, solar, both
  roofend          the roof's overhang past the gable (curve and pitch roofs)
  corner           the corner flashing, at a wall's start
  annex            the office annex, on the left gable (where the design has one)

Every piece is in its wall's frame: x along the wall from its start, y up from the ground, z out of the wall's
face (the roof's: x along the building, z out of the front wall, so the building lies at z < 0). Its parts
say which mesh they go in (the sections of World/WorldRideChurches.h's kit: 0 plain, 2 roofing, 3 brick,
4 render), their colour, the roofing or dirt code, and the furthest level of detail they stay in.

    python Tools/blender_warehouse_kit_export.py      # Data/RideUK/warehouse_kit.bin and .json
"""
import json
import math
import os
import struct
import sys

import bpy
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_warehouse_kit as K  # noqa: E402
import blender_estate_props as EP  # noqa: E402

DATA = os.path.join(os.path.dirname(HERE), "Data", "RideUK")
PLAIN, ROOFING, BRICKWORK, RENDER = 0, 2, 3, 4

# Which buildings each design suits: the map's class, the outline's short side (m) and area (m2).
SUITS = {
    "WH_big_box": dict(classes=["industrial", "warehouse", "transportation", "manufacture"], short=[42, 140], area=[4500, 60000], weight=1.0),
    "WH_blue_depot": dict(classes=["industrial", "warehouse", "transportation"], short=[34, 120], area=[3000, 60000], weight=1.0),
    "WH_solar_curve": dict(classes=["industrial", "warehouse", "manufacture"], short=[26, 70], area=[1800, 14000], weight=1.0),
    "WH_office_annex": dict(classes=["industrial", "warehouse", "manufacture"], short=[20, 55], area=[1000, 9000], weight=1.0),
    "WH_hangar": dict(classes=["industrial", "warehouse", "transportation"], short=[18, 45], area=[700, 4500], weight=0.6),
    "WH_trade_terrace": dict(classes=["industrial", "retail", "commercial"], short=[12, 30], area=[450, 4500], weight=1.0, ratio=2.0),
    "WH_workshop": dict(classes=["industrial", "warehouse", "manufacture", "garage"], short=[7, 22], area=[100, 1000], weight=1.0),
    "WH_retail_park": dict(classes=["retail", "supermarket", "commercial"], short=[24, 90], area=[1000, 16000], weight=1.0),
    "WH_supermarket": dict(classes=["supermarket", "retail"], short=[40, 120], area=[3000, 25000], weight=1.0),
    "WH_retail_row": dict(classes=["retail", "commercial"], short=[18, 50], area=[1100, 9000], weight=1.0, ratio=1.6),
}


def part_class(p):
    s = p.surface
    if s in ("clad", "clad_v", "roof"):
        return ROOFING, 1.0
    if s == "tile":
        return ROOFING, 0.0
    if s == "brick":
        return BRICKWORK, 0.06
    if s == "concrete":
        return RENDER, 0.08
    return PLAIN, 1.0


def clear():
    K.clear()


def frame_points(U, to_local):
    """Every triangle of a unit's parts in the piece's frame: places, normals, part indices."""
    dg = bpy.context.evaluated_depsgraph_get()
    P, N, I = [], [], []
    for i, part in enumerate(U.parts):
        ev = part.obj.evaluated_get(dg)
        me = ev.to_mesh()
        me.calc_loop_triangles()
        mw = part.obj.matrix_world
        nw = mw.to_3x3().inverted().transposed()
        co = np.array([mw @ v.co for v in me.vertices], np.float64)
        cn = np.zeros((len(me.loops), 3))
        me.corner_normals.foreach_get("vector", cn.ravel())
        if len(cn):
            cn = np.array([nw @ Vector(n) for n in cn])
            cn /= np.maximum(np.linalg.norm(cn, axis=1, keepdims=True), 1e-9)
        for lt in me.loop_triangles:
            P.append(co[list(lt.vertices)])
            N.append(cn[list(lt.loops)])
            I += [i, i, i]
        ev.to_mesh_clear()
    if not P:
        return None
    P = np.concatenate(P)
    N = np.concatenate(N)
    return to_local(P, False), to_local(N, True), np.array(I)


def wall_local(a, normal):
    # (built on a wall from (0, 0) along +x facing -y: x along, y up, z out)
    return np.stack([a[:, 0], a[:, 2], -a[:, 1]], 1)


def annex_local(W):
    # (the annex on the left gable: that wall runs from (0, W) along -y, facing -x)
    def f(a, normal):
        return np.stack([(0.0 if normal else W) - a[:, 1], a[:, 2], -a[:, 0]], 1)
    return f


def piece_bytes(name, U, to_local, wall=None):
    got = frame_points(U, to_local)
    if got is None:
        return None, 0
    pos, nrm, part_of = got
    if wall is not None:
        # (on a wall, the faces nobody sees: into the wall, underneath, and a full bay's sheeting's ends, hidden
        # by the next bay's; about a third of a wall's triangles)
        t = pos.reshape(-1, 3, 3)
        fn = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
        fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-9)
        x0, x1 = t[:, :, 0].min(1), t[:, :, 0].max(1)
        full = np.zeros(len(t), bool)
        for i in range(len(U.parts)):
            sel = part_of[0::3] == i
            if sel.any():
                px = pos[part_of == i][:, 0]
                full[sel] = px.min() < 0.02 and px.max() > wall - 0.02
        hide = (fn[:, 2] < -0.9) | (fn[:, 1] < -0.9) | (full & (np.abs(fn[:, 0]) > 0.9) & ((x1 < 0.02) | (x0 > wall - 0.02)))
        keep = np.repeat(~hide, 3)
        pos, nrm, part_of = pos[keep], nrm[keep], part_of[keep]
        if not len(pos):
            return None, 0
    # (a pale's thin edges: only its two faces kept, front and back; nobody sees a 2 cm edge)
    thin = np.array([p.surface == "pale" for p in U.parts])
    if thin.any():
        t = pos.reshape(-1, 3, 3)
        fn = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
        fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-9)
        keep = np.repeat(~(thin[part_of[0::3]] & (np.abs(fn[:, 2]) < 0.7)), 3)
        pos, nrm, part_of = pos[keep], nrm[keep], part_of[keep]
    parts = []
    for p in U.parts:
        sec, code = part_class(p)
        parts.append((sec, p.rgb, code, p.lod))
    q = np.round(pos * 100.0).astype(np.int64)
    qn = np.round(nrm * 127.0).astype(np.int64)
    key = np.concatenate([q, qn, part_of[:, None]], 1)
    uniq, inv = np.unique(key, axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    assert len(uniq) < 65536 and np.abs(q).max() < 32767, name
    b = bytearray()
    nm = name.encode()
    b += struct.pack("<H", len(nm)) + nm
    b += struct.pack("<H", len(parts))
    for sec, rgb, code, lod in parts:
        b += struct.pack("<6B", sec, *[int(round(max(0.0, min(1.0, c)) * 255)) for c in rgb], int(round(code * 255)), lod)
    b += struct.pack("<II", len(uniq), len(inv) // 3)
    v = np.zeros(len(uniq), dtype=[("p", "<i2", 3), ("n", "i1", 3), ("k", "u1")])
    v["p"] = uniq[:, 0:3]
    v["n"] = np.clip(uniq[:, 3:6], -127, 127)
    v["k"] = uniq[:, 6]
    b += v.tobytes()
    b += inv.astype("<u2").tobytes()
    return bytes(b), len(inv) // 3


def tops(spec):
    """The long walls' top and the gable bays' top (to the eaves, or the parapet on flat and multi roofs)."""
    H = spec["H"]
    par = spec.get("parapet", 1.0) if spec["roof"] in ("flat", "multi") else 0.0
    return H + par, H + par


def bay_unit(spec, letter, is_long):
    top = tops(spec)[0 if is_long else 1]
    bw = spec.get("bay", 7.5) if is_long else gable_bay(spec)
    U = K.Unit("bay", "")
    w = K.Wall((0, 0), (1, 0), bw, top)
    p = K.plinth_of(spec, letter)
    z0 = p[0] if p else K.FOOT
    K.cladding(U, w, 0.0, bw, z0, top, spec)
    K.bay_features(U, spec, w, letter, 0.0, bw, 1, 3, is_long, p, every_pipe=True)
    if spec["roof"] in ("flat", "multi"):
        trim = spec.get("trim", K.GOOSEWING)
        w.box(U, "coping", -0.01, bw + 0.01, top - 0.02, top + 0.12, -0.3, 0.05, "metal", trim, 1)
    return U


def gable_bay(spec):
    if spec["roof"] == "multi":
        return spec["W"] / (2 * spec.get("spans", 3))
    return spec.get("gable_bay", 6.0)


def gabletop_unit(spec):
    if spec["roof"] == "flat":
        return None
    W = spec["W"]
    base = tops(spec)[1]
    U = K.Unit("gabletop", "")
    w = K.Wall((0, 0), (1, 0), W, base)
    if spec["roof"] == "curve":
        ss = [W * j / 24 for j in range(25)]
    else:
        ss = K._gable_breaks(spec, w)
    surf = spec.get("clad_surface", "clad")
    rgb = spec["bands"][-1] if spec.get("bands") else spec.get("clad_rgb", K.GOOSEWING)
    ztop = lambda y: K.roof_z(spec, y) - 0.2
    for a, b in zip(ss[:-1], ss[1:]):
        za, zb = max(ztop(a), base), max(ztop(b), base)
        if max(za, zb) < base + 0.05:
            continue
        w.face(U, "gabletop", [(a, base - 0.01), (b, base - 0.01), (b, zb), (a, za)], -0.15, 0.0, surf, rgb, 2)
    return U if U.parts else None


def roof_unit(spec, lit, solar, end=False):
    """One long bay's length of the roof (x 0..bay), or the overhang past the gable (x -over..0)."""
    W = spec["W"]
    kind = spec["roof"]
    bw = spec.get("bay", 7.5)
    U = K.Unit("roof", "")
    if kind == "flat":
        if end:
            return None
        U.add(K.box("roof", (bw / 2, W / 2, spec["H"] - 0.6), (bw, W, 0.2)), "roof_flat", K.ROOF_GREY, 2)
        return U
    over = 0.0 if kind == "multi" else 0.35
    if end and over == 0.0:
        return None
    x0, x1 = (-over, 0.0) if end else (0.0, bw)
    ys = K.roof_samples(spec, -over, W + over, 24 if kind == "curve" else 2)
    top = [(y, K.roof_z(spec, y)) for y in ys]
    bot = [(y, z - 0.22) for y, z in top]
    U.add(K.strip("roof", top, bot, x0, x1), "roof", spec.get("roof_rgb", K.ROOF_GREY), 2)
    if kind != "multi":
        trim = spec.get("trim", K.GOOSEWING)
        for y, s in ((-over, -1), (W + over, 1)):
            z = K.roof_z(spec, y)
            U.add(K.box("gutter", ((x0 + x1) / 2, y + s * 0.1, z - 0.2), (x1 - x0, 0.22, 0.32)), "metal", trim, 1)
    if end:
        return U
    sol = spec.get("solar") if solar else None
    if lit:
        if kind == "curve":
            bands = [(W * 0.12, W * 0.42), (W * 0.58, W * 0.88)]
        elif kind == "pitch":
            bands = [(W * 0.1, W * 0.4), (W * 0.6, W * 0.9)]
        else:
            n = spec.get("spans", 3)
            sp = W / n
            bands = []
            for k in range(n):
                bands += [(k * sp + sp * 0.12, k * sp + sp * 0.4), (k * sp + sp * 0.6, k * sp + sp * 0.88)]
        for y0, y1 in bands:
            if sol and sol[0] <= (y0 + y1) / (2 * W) <= sol[1]:
                continue
            ys = K.roof_samples(spec, y0, y1, 6 if kind == "curve" else 1)
            t = [(y, K.roof_z(spec, y) + 0.06) for y in ys]
            bt = [(y, z - 0.07) for y, z in t]
            U.add(K.strip("rooflight", t, bt, bw * 0.3, bw * 0.7), "rooflight", K.ROOFLIGHT, 1)
    if sol:
        ya, yb = W * sol[0], W * sol[1]
        ys = K.roof_samples(spec, ya, yb, 10 if kind == "curve" else 1)
        t = [(y, K.roof_z(spec, y) + 0.12) for y in ys]
        bt = [(y, z - 0.05) for y, z in t]
        U.add(K.strip("solar", t, bt, 0.15, bw - 0.15), "solar", K.SOLAR, 2)
    return U


def corner_unit(spec):
    U = K.Unit("corner", "")
    trim = spec.get("trim", K.GOOSEWING)
    top = tops(spec)[0]
    w = K.Wall((0, 0), (1, 0), 1.0, top)
    z0 = K.DOCK_FLOOR if spec.get("dock_floor") else spec.get("plinth_h", 2.1) if spec.get("plinth", "brick") != "none" else K.FOOT
    w.box(U, "corner", -0.02, 0.32, z0, top, 0.0, 0.03, "metal", trim, 1)
    return U


def signs_local(to_local=wall_local):
    """The sign faces made since the scene was cleared (K.SIGNS), in the piece's frame: x, y, z of the middle,
    the way it faces (x, z), its width and height, the kind of name."""
    out = []
    for c, n, w, h, kind, _U in K.SIGNS:
        p = to_local(np.array([[c.x, c.y, c.z]]), False)[0]
        q = to_local(np.array([[n.x, n.y, n.z]]), True)[0]
        out.append([round(float(p[0]), 3), round(float(p[1]), 3), round(float(p[2]), 3), round(float(q[0]), 3), round(float(q[2]), 3),
                    round(w, 3), round(h, 3), kind])
    return out


def letters(spec, keys):
    out = set()
    for k in keys:
        out |= set(c for c in spec.get(k, ".*") if c not in " *")
    return sorted(out | {"."})


def main():
    blob = bytearray()
    count = 0
    rows = []
    for name, label, spec in K.DESIGNS:
        pieces = {}
        signs = {}

        def put(key, U, to_local=wall_local, wall=None):
            nonlocal blob, count
            if U is None:
                return
            b, tris = piece_bytes(name + "/" + key, U, to_local, wall)
            if b is not None:
                blob += b
                count += 1
                pieces[key] = tris
                if K.SIGNS:
                    signs[key] = signs_local(to_local)
        for t in letters(spec, ("front", "back")):
            clear(); put("long:" + t, bay_unit(spec, t, True), wall=spec.get("bay", 7.5))
        for t in letters(spec, ("right", "left")):
            clear(); put("gable:" + t, bay_unit(spec, t, False), wall=gable_bay(spec))
        clear(); put("gabletop", gabletop_unit(spec), wall=spec["W"])
        for key, lit, sol in (("roof:P", False, False), ("roof:L", True, False), ("roof:S", False, True), ("roof:SL", True, True)):
            if sol and not spec.get("solar"):
                continue
            clear(); put(key, roof_unit(spec, lit, sol))
        clear(); put("roofend", roof_unit(spec, False, False, end=True))
        clear(); put("corner", corner_unit(spec))
        if spec.get("annex"):
            clear()
            U = K.Unit("annex", "")
            K.annex(U, spec)
            put("annex", U, annex_local(spec["W"]))
        long_top, gable_top = tops(spec)
        row = dict(name=name, label=label, L=spec["L"], W=spec["W"], H=spec["H"], top=long_top, gable_top=gable_top,
                   bay=spec.get("bay", 7.5), gable_bay=round(gable_bay(spec), 4), roof=spec["roof"],
                   spans=spec.get("spans", 1), front=spec.get("front", ".*"), back=spec.get("back", ".*"),
                   right=spec.get("right", ".*"), left=spec.get("left", ".*"), solar=bool(spec.get("solar")),
                   dock_floor=bool(spec.get("dock_floor")),
                   skirt=[round(c, 4) for c in (K.CONCRETE_WHITE if spec.get("dock_floor") else spec.get("brick_rgb", K.BRICK_RED) if spec.get("plinth", "brick") == "brick" else K.BLOCK)],
                   pieces=pieces, signs=signs)
        row.update(SUITS[name])
        rows.append(row)
        print("%-18s %2d pieces, a 7.5 m bay of wall %s triangles" % (name, len(pieces), pieces.get("long:.")))
    # The yards' and car parks' props (Tools/blender_estate_props.py), each its own piece "prop/<name>".
    props = []
    for name, label, make in EP.PROPS:
        clear()
        U = make()
        b, tris = piece_bytes("prop/" + name, U, wall_local)
        lo, hi = np.full(3, 1e9), np.full(3, -1e9)
        for p in U.parts:
            for c in p.obj.bound_box:
                v = p.obj.matrix_world @ Vector(c)
                lo, hi = np.minimum(lo, v), np.maximum(hi, v)
        blob += b
        count += 1
        props.append(dict(name=name, label=label, tris=tris, size=[round(float(hi[0] - lo[0]), 2), round(float(hi[1] - lo[1]), 2), round(float(hi[2] - max(lo[2], 0.0)), 2)],
                          signs=signs_local()))
    head = b"TSWK" + struct.pack("<II", 1, count)
    with open(os.path.join(DATA, "warehouse_kit.bin"), "wb") as f:
        f.write(head + bytes(blob))
    about = ("Ride UK's industrial estates (the warehouse kit, beenster Nov 25-26): eight original designs of UK sheds, "
             "cut into pieces by Tools/blender_warehouse_kit_export.py (which writes this file and warehouse_kit.bin). "
             "World/WorldRideWarehouses.cpp gives a big shed the map draws as an oblong outline (industrial, warehouse, "
             "retail...) the design that suits its class and size ('classes', 'short', 'area', 'ratio' the least long "
             "to short), lays its walls' bays from the patterns (front, back, right, left; '*' fills), and stretches "
             "each bay, its roof and its gables to the outline. Letters: . plain, R roller door, D dock, P door, O office, "
             "E entrance, T trade counter, S shopfront, G glazing, M supermarket lobby, U shop unit, W high windows, H hangar door, "
             "L louvres. 'signs' per piece: x, y, z, facing x, z, width, height, kind of name (the game makes the names up). "
             "'props': the yards' and car parks' things (Tools/blender_estate_props.py), pieces 'prop/<name>', centred, "
             "x along, z towards the front, size [long, deep, high] m.")
    with open(os.path.join(DATA, "warehouse_kit.json"), "w") as f:
        json.dump({"_about": about, "stretch": [0.55, 1.8], "designs": rows, "props": props}, f, indent=1)
    print("warehouse kit: %d pieces, %d KB" % (count, (len(blob) + len(head)) // 1024))


if __name__ == "__main__":
    main()
