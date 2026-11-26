"""The farm kit (Tools/blender_farm_kit.py) cut into pieces for the game, in the warehouse kit's format
(Tools/blender_warehouse_kit_export.py), so World/WorldRideWarehouses.cpp's BuildWarehouse lays a farm building
along a real outline exactly as it lays a warehouse: Data/RideUK/farm_kit.bin and farm_kit.json.

Pieces per design: long:<letter>, gable:<letter> (to the eaves), gabletop (the gable above the eaves, its whole
depth; none where a gable is open), roof:P and roof:L (one long bay of roof, plain or with rooflights), roofend
(the overhang past a gable) and corner (sheeted walls' corner flashing).

Sections: 0 plain (doors, steel, straw), 1 masonry (stone: the houses' scanned masonry, its colour the masks
as the church kit's: brightness about 0.5, 0.8, 0.3, the place's own stone tint), 2 roofing (sheet 1.0, boards
0.75, clay 0.5, stone slates 0.25, slate 0), 3 brick, 4 render (concrete and block).

    python Tools/blender_farm_kit_export.py
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_farm_kit as F  # noqa: E402
import blender_warehouse_kit as K  # noqa: E402
import blender_warehouse_kit_export as X  # noqa: E402

DATA = X.DATA
PLAIN, MASONRY, ROOFING, BRICKWORK, RENDER = 0, 1, 2, 3, 4
TILE_CODE = {"slate": 0.0, "stone_slate": 0.25, "clay": 0.5, "pantile": 0.5}
OPEN = set("ABF")


def _lum(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def part_class(p, spec):
    """(section, colour, code) of a part."""
    s = p.surface
    if s in ("clad", "clad_v", "roof", "tin"):
        return ROOFING, p.rgb, 1.0
    if s in ("boarding", "weatherboard"):
        return ROOFING, p.rgb, 0.75
    if s == "tile":
        return ROOFING, p.rgb, TILE_CODE.get(spec.get("roof_kind"), 0.0)
    if s == "stone":
        main = F.wall_rgb(spec)
        b = min(0.69, max(0.31, 0.5 * _lum(p.rgb) / max(1e-4, _lum(main))))
        return MASONRY, (b, 0.8, 0.3), 0.06
    if s == "brick":
        return BRICKWORK, p.rgb, 0.06
    if s in ("concrete", "block"):
        return RENDER, p.rgb, 0.08
    return PLAIN, p.rgb, 1.0


def is_open(spec):
    return spec.get("gable_open") or any(c in OPEN for k in ("front", "back", "right", "left") for c in spec.get(k, ""))


def piece_bytes(name, U, spec, wall=None):
    """As the warehouse export's, with the farm's sections; an open building keeps its walls' inner faces."""
    got = X.frame_points(U, X.wall_local)
    if got is None:
        return None, 0
    import numpy as np
    pos, nrm, part_of = got
    if wall is not None:
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
        hide = (fn[:, 1] < -0.9) | (full & (np.abs(fn[:, 0]) > 0.9) & ((x1 < 0.02) | (x0 > wall - 0.02)))
        if not is_open(spec):
            hide |= fn[:, 2] < -0.9
        keep = np.repeat(~hide, 3)
        pos, nrm, part_of = pos[keep], nrm[keep], part_of[keep]
        if not len(pos):
            return None, 0
    parts = [part_class(p, spec) + (p.lod,) for p in U.parts]
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


def gable_bay(spec):
    return spec.get("gable_bay", spec["W"] / max(1, round(spec["W"] / 6.0)))


def bay_unit(spec, letter, is_long):
    """One bay of wall (a middle one: no corner trimming), up to the eaves."""
    bw = spec.get("bay", 6.0) if is_long else gable_bay(spec)
    U = K.Unit("bay", "")
    w = K.Wall((0, 0), (1, 0), bw, spec["H"])
    F.bay(U, spec, w, letter, 0.0, bw, 1, 3, is_long, spec["H"], every_pipe=is_long and (spec.get("wall") in ("sheet", "boarding")))
    return U


def gabletop_unit(spec):
    """The gable over the eaves, right across; none where either gable has an open bay."""
    if spec.get("gable_open") or any(c in OPEN for k in ("right", "left") for c in spec.get(k, "")):
        return None
    W, H = spec["W"], spec["H"]
    U = K.Unit("gabletop", "")
    w = K.Wall((0, 0), (1, 0), W, lambda s: max(H, F.roof_z(spec, s) - 0.15))
    masonry = spec.get("wall") in ("stone", "brick")
    boards = not masonry and all(c in "FY" for k in ("right", "left") for c in spec.get(k, ".").replace(" ", "").replace("*", ""))
    surf = "boarding" if boards else F.wall_surface(spec)
    rgb = spec.get("board_rgb", F.BOARDS) if boards else F.wall_rgb(spec)
    w.face(U, "gabletop", F.gable_outline(spec, w, 0.0, W, H - 0.01), -F.thick(spec), 0.0, surf, rgb, 2)
    if "O" in spec.get("right", "") + spec.get("left", ""):
        z = F.roof_z(spec, W / 2) - 1.3
        if z > H + 0.4:
            w.box(U, "owlhole", W / 2 - 0.12, W / 2 + 0.12, z, z + 0.35, -0.02, 0.002, "plain", F.DARK, 0)
    return U


def roof_unit(spec, lit, end=False):
    U = K.Unit("roof", "")
    if end:
        o = F.over(spec)
        F.build_roof(U, spec, -o, 0.0, end=True)
    else:
        F.build_roof(U, spec, 0.0, spec.get("bay", 6.0), lit=lit)
    return U if U.parts else None


def corner_unit(spec):
    if spec.get("wall") not in ("sheet", "boarding", "tin"):
        return None
    U = K.Unit("corner", "")
    w = K.Wall((0, 0), (1, 0), 1.0, spec["H"])
    p = F.plinth(spec, ".")
    z0 = p[0] if p else F.FOOT
    w.box(U, "corner", -0.02, 0.25, z0, spec["H"], 0.0, 0.03, "metal", spec.get("trim", F.STEEL_GALV), 1)
    return U


def letters(spec, keys):
    out = set()
    for k in keys:
        out |= set(c for c in spec.get(k, ".*") if c not in " *")
    return sorted(out | {"."})


def main():
    blob = bytearray()
    count = 0
    rows = []
    for name, label, spec, suits in F.DESIGNS:
        pieces = {}

        def put(key, U, wall=None):
            nonlocal blob, count
            if U is None:
                return
            b, tris = piece_bytes(name + "/" + key, U, spec, wall)
            if b is not None:
                blob += b
                count += 1
                pieces[key] = tris
        for t in letters(spec, ("front", "back")):
            K.clear(); put("long:" + t, bay_unit(spec, t, True), wall=spec.get("bay", 6.0))
        for t in letters(spec, ("right", "left")):
            K.clear(); put("gable:" + t, bay_unit(spec, t, False), wall=gable_bay(spec))
        K.clear(); put("gabletop", gabletop_unit(spec), wall=spec["W"])
        K.clear(); put("roof:P", roof_unit(spec, False))
        if spec.get("rooflights", True) and F.roof_surface(spec) == "roof":
            K.clear(); put("roof:L", roof_unit(spec, True))
        K.clear(); put("roofend", roof_unit(spec, False, end=True))
        K.clear(); put("corner", corner_unit(spec))
        p = F.plinth(spec, ".")
        skirt = p[2] if p else (F.wall_rgb(spec) if spec.get("wall") == "brick" else F.BLOCK)
        row = dict(name=name, label=label, L=spec["L"], W=spec["W"], H=spec["H"], top=spec["H"], gable_top=spec["H"],
                   bay=spec.get("bay", 6.0), gable_bay=round(gable_bay(spec), 4), roof=spec["roof"],
                   front=spec.get("front", ".*"), back=spec.get("back", ".*"), right=spec.get("right", ".*"), left=spec.get("left", ".*"),
                   skirt=[round(c, 4) for c in skirt], pieces=pieces)
        row.update({k: v for k, v in suits.items()})
        rows.append(row)
        print("%-18s %2d pieces, a bay of wall %s triangles" % (name, len(pieces), pieces.get("long:.")))
    head = b"TSWK" + struct.pack("<II", 1, count)
    with open(os.path.join(DATA, "farm_kit.bin"), "wb") as f:
        f.write(head + bytes(blob))
    about = ("Ride UK's farmyards (the everyday kits plan, stage 4): twelve original designs of UK farm buildings, cut "
             "into pieces by Tools/blender_farm_kit_export.py (which writes this file and farm_kit.bin) in the warehouse "
             "kit's format. World/WorldRideFarms.cpp gives a farm building the map draws (barn, farm_auxiliary, cowshed, "
             "stable, sty, or a big shed in a farmyard) the design that suits its class and size and lays it with the "
             "warehouse kit's BuildWarehouse. Classes: farm_shed, farm_store, farm_livestock, farm_small (modern), "
             "barn_stone, barn_brick, barn_boarded, farm_old (old). 'regions' is a note; the game picks old barns' "
             "walls by the place's own look.")
    with open(os.path.join(DATA, "farm_kit.json"), "w") as f:
        json.dump({"_about": about, "stretch": [0.55, 1.8], "designs": rows}, f, indent=1)
    print("farm kit: %d pieces, %d KB" % (count, (len(blob) + len(head)) // 1024))


if __name__ == "__main__":
    main()
