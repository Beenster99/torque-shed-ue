"""Ride UK's churches, stage 2 of "Churches that look like their place": the kit of parts and the rules that
put them together. A church is a spec (its region's stones and roof, a nave, perhaps a chancel, aisles, a
clerestory, a parapet, a porch, a tower of one of eight kinds or a bellcote or a timber belfry, a window
style); a chapel is a gable-fronted Nonconformist chapel of one or two tiers, or a tin tabernacle.
SPECS holds forty assembled for the stage 2 review sheet, five or so from each region, the four stage 1
designs among them.

    python Tools/blender_church_assemble.py --render DIR     # the sheet of forty in DIR

Frame as in blender_church_kit.py: x east, y north, z up, the nave's west end at x = 0.
"""
import math
import os
import sys

import bpy
from mathutils import Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from blender_street_kit import Model, _object, box, lathe  # noqa: E402
import blender_church_kit as K  # noqa: E402
from blender_church_kit import (FOOT, SOUTH, NORTH, WEST, EAST, opening, range_block, coping, battlements,  # noqa: E402
                                buttress, cross, porch, prism, place)

TILE = (0.22, 0.07, 0.035)
THATCH = (0.2, 0.15, 0.07)
COTS_SLATE = (0.3, 0.26, 0.19)
SHINGLE = (0.12, 0.09, 0.06)
WEATHERBOARD = (0.5, 0.5, 0.47)

# The regions' walls, dressings and roofs: (colour, surface).
REGIONS = {
    "pennine": dict(wall=(K.GRIT, "stone"), dressed=K.GRIT_DRESSED, roof=(K.STONE_SLATE, "roof"), quoins=True),
    "cotswold": dict(wall=((0.42, 0.31, 0.16), "stone"), dressed=(0.55, 0.43, 0.25), roof=(COTS_SLATE, "roof"), quoins=True),
    "east_anglia": dict(wall=((0.1, 0.1, 0.1), "flint"), dressed=K.LIME_DRESSED, roof=(TILE, "roof"), quoins=True),
    "south_east": dict(wall=((0.3, 0.29, 0.24), "stone"), dressed=(0.45, 0.42, 0.34), roof=(TILE, "roof"), quoins=False),
    "somerset": dict(wall=((0.25, 0.25, 0.22), "stone"), dressed=(0.5, 0.36, 0.17), roof=(K.SLATE, "roof"), quoins=True),
    "cornwall": dict(wall=((0.3, 0.3, 0.28), "stone"), dressed=(0.4, 0.4, 0.37), roof=(K.SLATE, "roof"), quoins=True),
    "wales": dict(wall=((0.12, 0.12, 0.115), "stone"), dressed=(0.3, 0.3, 0.28), roof=(K.SLATE, "roof"), quoins=False),
    "town": dict(wall=(K.ROCKFACE, "stone"), dressed=K.BATH, roof=(K.SLATE, "roof"), quoins=False),
    "scotland": dict(wall=((0.58, 0.58, 0.55), "render"), dressed=(0.32, 0.28, 0.23), roof=(K.SLATE, "roof"), quoins=True),
}

# Window styles: (width, head, mullions, the jambs' height wanted).
WINDOWS = {"lancet": (0.55, "pointed", 0, 1.6), "round": (0.6, "round", 0, 1.4), "dec": (1.2, "pointed", 1, 2.4),
           "perp": (1.8, "pointed", 2, 2.6)}


def _head(w, kind):
    return w * 0.866 if kind == "pointed" else (w * 0.5 if kind == "round" else 0.0)


def window(M, name, x, y, facing, top, style, dressed, sill=None, scale=1.0):
    """A window in a wall under eaves at `top`: its jambs shortened, then the window narrowed, to fit."""
    w, kind, mul, want = WINDOWS[style]
    w *= scale
    sill = sill if sill is not None else (1.0 if top < 5.0 else 1.5)
    room = top - 0.4 - sill
    spring = min(want * scale, room - _head(w, kind))
    if spring < 0.7:
        w *= 0.7
        mul = min(mul, 1)
        spring = max(0.5, room - _head(w, kind))
    opening(M, name, (x, y, sill), facing, w, spring, kind, 0.12 + 0.03 * mul, dressed, mullions=mul)


def quoins(M, name, corners, top, dressed):
    """Long and short dressed stones up each corner: corners [(x, y, sx, sy)] with (sx, sy) pointing in."""
    for i, (x, y, sx, sy) in enumerate(corners):
        for k in range(int((top - FOOT - 0.6) / 0.6)):
            z = FOOT + 0.6 + k * 0.6
            big = k % 2 == 0
            M.add(box("%s%d_%d" % (name, i, k), (x + sx * 0.18 * (1 if big else 0.5), y + sy * 0.18 * (0.5 if big else 1), z + 0.25),
                      (0.42 if big else 0.25, 0.25 if big else 0.42, 0.28)), "stone", dressed, 0.3)


def eaves_parapet(M, name, x0, x1, y, z, dressed, battled):
    """A parapet along an eaves at y (hiding a low lead roof), battlemented if asked."""
    M.add(box(name, ((x0 + x1) * 0.5, y, z + 0.4), (x1 - x0, 0.35, 0.8)), "stone", dressed, 0.3)
    M.add(box(name + "_str", ((x0 + x1) * 0.5, y + (0.08 if y < 0 else -0.08) * -1, z - 0.05), (x1 - x0, 0.5, 0.16)), "stone", dressed, 0.3)
    if battled:
        n = int((x1 - x0) / 1.0)
        for k in range(n):
            M.add(box("%s_m%d" % (name, k), (x0 + (k + 0.5) * (x1 - x0) / n, y, z + 1.05), (0.55, 0.35, 0.5)), "stone", dressed, 0.3)


def bellcote(M, name, x, ridge, dressed, double=True):
    """A bellcote on a gable's apex: one or two openings under a little gabled head, a bell hanging."""
    wide = 1.3 if double else 0.9
    M.add(box(name + "_base", (x, 0.0, ridge + 0.1), (0.5, wide, 0.5)), "stone", dressed, 0.3)
    piers = (-1, 0, 1) if double else (-1, 1)
    for s in piers:
        M.add(box("%s_pier%d" % (name, s), (x, s * (wide * 0.5 - 0.15), ridge + 0.85), (0.45, 0.3, 1.1)), "stone", dressed, 0.3)
    M.add(box(name + "_head", (x, 0.0, ridge + 1.5), (0.5, wide, 0.25)), "stone", dressed, 0.3)
    gb = prism(name + "_gable", [(-wide * 0.58, 0.0), (wide * 0.58, 0.0), (0.0, 0.55)], 0.5)
    gb.matrix_world = Matrix.Translation((x + 0.25, 0.0, ridge + 1.62)) @ Matrix.Rotation(math.pi * 0.5, 4, "Z") @ gb.matrix_world
    M.add(gb, "stone", dressed, 0.3)
    for s in ((-0.3, 0.3) if double else (0.0,)):
        M.add(lathe("%s_bell%.1f" % (name, s), [(0.0, 0.0), (0.18, 0.05), (0.12, 0.28), (0.0, 0.32)], seg=10, at=(x, s, ridge + 0.6)),
              "steel", (0.25, 0.18, 0.08), 0.5)


def belfry(M, name, x, ridge, roof_pitch, nave_w):
    """A timber belfry astride the nave's west end, weatherboarded, under a shingled spire (the south-east)."""
    B = 2.6
    base = ridge - 1.2
    M.add(box(name, (x, 0.0, base + 1.6), (B, B, 3.2)), "timber", WEATHERBOARD, 0.3)
    for f, (dx, dy) in ((SOUTH, (0, -1)), (WEST, (-1, 0)), (NORTH, (0, 1)), (EAST, (1, 0))):
        opening(M, "%s_l%d%d" % (name, dx, dy), (x + dx * B * 0.5, dy * B * 0.5, base + 2.0), f, 0.5, 0.6, "flat", 0.08, WEATHERBOARD,
                surface="timber")
    M.add(lathe(name + "_spire", [(B * 0.62, 0.0), (0.0, 7.5)], seg=8, at=(x, 0.0, base + 3.2), phase=math.pi / 8), "timber", SHINGLE, 0.3)
    M.add(lathe(name + "_vane", [(0.04, 0.0), (0.0, 1.0)], seg=6, at=(x, 0.0, base + 10.6)), "steel", (0.05, 0.05, 0.05), 0.2)


def tower(M, name, cx, cy, T, TH, R, kind, spire="stone"):
    """A tower T square (or round) TH to its top, and what crowns it:
       norman   a corbel table and a low pyramid cap        perp     battlements, pinnacles, angle buttresses
       plain    battlements, no pinnacles                   broach   a broach spire with lucarnes
       needle   battlements and a spire set back behind     saddle   a gabled top (a saddleback)
       round    a round tower with a conical cap (East Anglia)"""
    wall, ws = R["wall"]
    dr = R["dressed"]
    spire_c = {"stone": dr, "lead": K.LEAD, "shingle": SHINGLE, "slate": K.SLATE}[spire]
    spire_s = "stone" if spire == "stone" else "roof"
    if kind == "round":
        r = T * 0.5
        M.add(lathe(name, [(r, 0.0), (r, TH - FOOT)], seg=16, at=(cx, cy, FOOT)), ws, wall, 0.35)
        M.add(lathe(name + "_band", [(r + 0.08, 0.0), (r + 0.08, 0.2)], seg=16, at=(cx, cy, TH - 3.6)), "stone", dr, 0.3)
        M.add(lathe(name + "_cap", [(r + 0.25, 0.0), (0.0, T * 0.9)], seg=16, at=(cx, cy, TH)), "roof", R["roof"][0], 0.3)
        for f, (dx, dy) in ((SOUTH, (0, -1)), (WEST, (-1, 0)), (NORTH, (0, 1)), (EAST, (1, 0))):
            opening(M, "%s_b%d%d" % (name, dx, dy), (cx + dx * (r - 0.05), cy + dy * (r - 0.05), TH - 3.0), f, 0.5, 1.0, "round", 0.12, dr)
        opening(M, name + "_slit", (cx, cy - r + 0.05, TH * 0.45), SOUTH, 0.25, 0.8, "round", 0.1, dr)
        return
    if kind == "saddle":
        range_block(M, name, cx - T * 0.5, cx + T * 0.5, T, TH, 52.0, wall, R["roof"][0], overhang=0.1, verge=0.1, wall_surface=ws)
        coping(M, name + "_cw", cx - T * 0.5, T, TH, TH + T * 0.5 * math.tan(math.radians(52.0)), dr, kneeler=False)
    else:
        M.add(box(name, (cx, cy, (FOOT + TH) * 0.5), (T, T, TH - FOOT)), ws, wall, 0.35)
    M.add(box(name + "_plinth", (cx, cy, (FOOT + 0.8) * 0.5), (T + 0.3, T + 0.3, 0.8 - FOOT)), "stone", dr, 0.4)
    faces = ((SOUTH, (0, -1)), (NORTH, (0, 1)), (WEST, (-1, 0)), (EAST, (1, 0)))
    big = kind in ("perp", "broach", "needle")
    for f, (dx, dy) in faces:
        at = (cx + dx * T * 0.5, cy + dy * T * 0.5)
        hd = "round" if kind == "norman" else "pointed"
        opening(M, "%s_bel%d%d" % (name, dx, dy), (at[0], at[1], TH - (3.4 if big else 2.6)), f, 1.3 if big else 0.75,
                1.6 if big else 1.0, hd, 0.16, dr, mullions=1)
        opening(M, "%s_mid%d%d" % (name, dx, dy), (at[0], at[1], TH * 0.5), f, 0.3, 0.9, hd, 0.1, dr)
    for z in ((TH * 0.45, TH - 4.0) if big else (TH * 0.5 - 0.4,)):
        M.add(box("%s_str%.0f" % (name, z), (cx, cy, z), (T + 0.16, T + 0.16, 0.18)), "stone", dr, 0.3)
    if kind in ("perp", "needle"):
        for sx in (-1, 1):
            for sy in (-1, 1):
                for f in ((sx, 0), (0, sy)):
                    buttress(M, "%s_b%d%d%d%d" % (name, sx, sy, f[0], f[1]),
                             (cx + sx * T * 0.5 * (1 if f[0] else 0.78), cy + sy * T * 0.5 * (1 if f[1] else 0.78), 0.0),
                             (float(f[0]), float(f[1])), TH * 0.7, deep=0.6, wide=0.5, stone=dr)
    if kind == "norman":
        M.add(box(name + "_ledge", (cx, cy, TH - 0.3), (T + 0.35, T + 0.35, 0.18)), "stone", dr, 0.3)
        M.add(box(name + "_par", (cx, cy, TH + 0.3), (T + 0.25, T + 0.25, 1.0)), ws, wall, 0.35)
        M.add(lathe(name + "_cap", [(T * 0.68, 0.0), (0.0, T * 0.45)], seg=4, at=(cx, cy, TH + 0.5), phase=math.pi / 4),
              spire_s, spire_c, 0.3)
    elif kind in ("perp", "plain", "needle"):
        battlements(M, name + "_bat", cx, cy, T, TH, dr, pinnacles=(1.4 if kind == "perp" else (0.5 if kind == "needle" else 0.0)))
        if kind == "needle":
            M.add(lathe(name + "_spire", [(T * 0.34, 0.0), (0.0, TH * 1.1)], seg=8, at=(cx, cy, TH), phase=math.pi / 8), spire_s, spire_c, 0.35)
    elif kind == "broach":
        SH = TH * 1.25
        M.add(lathe(name + "_spire", [(T * 0.51, 0.0), (T * 0.41, 2.4), (0.0, SH)], seg=8, at=(cx, cy, TH), phase=math.pi / 8),
              spire_s, spire_c, 0.35)
        for sx in (-1, 1):
            for sy in (-1, 1):
                v = [(cx + sx * T * 0.5, cy + sy * T * 0.5, TH), (cx + sx * T * 0.2, cy + sy * T * 0.5, TH),
                     (cx + sx * T * 0.5, cy + sy * T * 0.2, TH), (cx + sx * T * 0.36, cy + sy * T * 0.36, TH + 3.0)]
                M.add(_object("%s_br%d%d" % (name, sx, sy), v, [(0, 2, 1), (0, 1, 3), (1, 2, 3), (2, 0, 3)]), spire_s, spire_c, 0.35)
    top = TH + {"norman": T * 0.45 + 0.5, "broach": TH * 1.25, "needle": TH * 1.1, "saddle": T * 0.64}.get(kind, 1.2)
    M.add(lathe(name + "_vane", [(0.05, 0.0), (0.03, 1.3), (0.0, 1.4)], seg=6, at=(cx, cy, top - 0.2)), "steel", (0.05, 0.05, 0.05), 0.2)


def church(spec):
    """An Anglican-pattern church from a spec (see SPECS)."""
    R = dict(REGIONS[spec["region"]])
    for k in ("wall", "roof", "dressed"):
        if k in spec:
            R[k] = spec[k]
    wall, ws = R["wall"]
    roof, rs = R["roof"]
    dr = R["dressed"]
    M = Model(spec["name"])
    L, W, E, P = spec["nave"]
    par = spec.get("parapet")          # None, "plain" or "battled"
    thatch = roof == THATCH
    if par:
        P, roof, rs = 14.0, K.LEAD, "roof"
    rs = "thatch" if thatch else rs
    nr = range_block(M, "nave", 0.0, L, W, E, P, wall, roof, roof_thick=0.45 if thatch else 0.2,
                     overhang=0.45 if thatch else (0.0 if par else 0.25), verge=0.3 if thatch else 0.0, wall_surface=ws, roof_surface=rs)
    tw = spec.get("tower")
    win = spec.get("win", "lancet")
    if not thatch:
        coping(M, "nw", 0.0, W, E, nr, dr, kneeler=not par)
    if par:
        for y in (-W * 0.5, W * 0.5):
            eaves_parapet(M, "npar%.0f" % y, 0.0, L, y, E, dr, par == "battled")
    # (the chancel: narrower and lower, east of the nave)
    ch = spec.get("chancel")
    xe = L
    if ch:
        CL, CW, CE = ch
        cr = range_block(M, "chancel", L, L + CL, CW, CE, P if not par else 45.0, wall, roof if not par else R["roof"][0],
                         roof_thick=0.45 if thatch else 0.2, overhang=0.45 if thatch else 0.25, verge=0.3 if thatch else 0.0,
                         wall_surface=ws, roof_surface=rs if not par else R["roof"][1])
        xe = L + CL
        if not thatch:
            coping(M, "ce", xe, CW, CE, cr, dr)
            coping(M, "ne", L, W, E, nr, dr, kneeler=False)
        cross(M, "ex", (xe, 0.0, cr + 0.15), dr, 0.7)
        for k, x in enumerate((L + CL * 0.35, L + CL * 0.75)):
            window(M, "cs%d" % k, x, -CW * 0.5, SOUTH, CE, win, dr)
            window(M, "cn%d" % k, x, CW * 0.5, NORTH, CE, win, dr)
        ew, eh = CW, CE
    else:
        if not thatch:
            coping(M, "ne", L, W, E, nr, dr)
        cross(M, "ex", (L, 0.0, nr + 0.15), dr, 0.7)
        ew, eh = W, E
    # (the east window, the style's best: three lancets, or a big traceried window)
    if win == "lancet":
        for u, h in ((-0.8, 0.85), (0.0, 1.0), (0.8, 0.85)):
            window(M, "e%.1f" % u, xe, -u, EAST, eh, "lancet", dr, scale=h * 1.1)
    elif win == "round":
        window(M, "e", xe, 0.0, EAST, eh, "round", dr, scale=1.2)
    else:
        w = min(ew * 0.42, 3.2)
        opening(M, "east", (xe, 0.0, 1.4), EAST, w, max(1.2, eh - 1.8 - w * 0.866 + (ew * 0.5 * math.tan(math.radians(P)) * 0.5)),
                "pointed", 0.2, dr, mullions=2 if win == "dec" else 3)
    # (aisles: lean-to roofs against the nave; a clerestory above them if asked)
    aisles = spec.get("aisles", "")
    clere = spec.get("clerestory", False)
    AW, AE = spec.get("aisle", (4.0, min(4.2, E - 1.4)))
    for side, s in (("N", 1), ("S", -1)):
        if side not in aisles:
            continue
        hi = E - (1.5 if clere else 0.2)
        M.add(box("aisle" + side, (L * 0.5, s * (W * 0.5 + AW * 0.5), (FOOT + AE) * 0.5), (L - 0.6, AW, AE - FOOT)), ws, wall, 0.35)
        if par:
            eaves_parapet(M, "apar" + side, 0.3, L - 0.3, s * (W * 0.5 + AW), AE, dr, par == "battled")
            ar = box("aroof" + side, (L * 0.5, s * (W * 0.5 + AW * 0.5), AE + 0.15), (L - 0.6, AW, 0.18))
        else:
            a = math.atan2(hi - AE, AW)
            ar = box("aroof" + side, (0, 0, 0), (L - 0.4, math.hypot(AW + 0.3, hi - AE) + 0.1, 0.18))
            ar.matrix_world = Matrix.Translation((L * 0.5, s * (W * 0.5 + AW * 0.5 + 0.1), (AE + hi) * 0.5 + 0.12)) @ Matrix.Rotation(-s * a, 4, "X")
        M.add(ar, "roof", roof if par else R["roof"][0], 0.3)
        if clere:
            for k in range(int((L - 3.0) / 3.5)):
                x = 2.5 + k * 3.5
                f = NORTH if s > 0 else SOUTH
                opening(M, "cl%s%d" % (side, k), (x, s * W * 0.5, E - 1.25), f, 1.0 if win == "perp" else 0.5, 0.5, "pointed" if win != "round" else "round",
                        0.1, dr, mullions=1 if win == "perp" else 0)
    # (the side windows, in the aisle walls where there are aisles, skipping the porch's bay)
    pside = spec.get("porch", "S")
    px = spec.get("porch_x", 3.0)
    bay = 3.5 if win in ("lancet", "round") else 4.2
    for side, s, f in (("N", 1, NORTH), ("S", -1, SOUTH)):
        y = s * (W * 0.5 + (AW if side in aisles else 0.0))
        top = AE if side in aisles else E
        n = max(2, int((L - 3.0) / bay))
        for k in range(n):
            x = 2.0 + (L - 3.5) * (k + 0.5) / n
            if pside == side and abs(x - px) < 2.0:
                continue
            if tw and tw.get("at") == "SW" and side == "S" and x < tw["T"] + 0.5:
                continue
            window(M, "w%s%d" % (side, k), x, y, f, top, win, dr)
            if spec.get("buttresses") and side not in aisles and k < n - 1:
                buttress(M, "bt%s%d" % (side, k), (x + (L - 3.5) / n * 0.5, y, 0.0), f, top - 0.6, stone=dr)
    if pside:      # (south porches only for now; a north porch is stage 3's, mirrored)
        s = -1
        wy = s * (W * 0.5 + (AW if pside in aisles else 0.0))
        pe = min(2.6, (AE if pside in aisles else E) - 0.3)
        pk = "round" if win == "round" else "pointed"
        porch(M, "porch", px, wy, 3.0, 2.6, pe, 45.0, wall, R["roof"][0] if not thatch else THATCH, dr, pk, 1.1,
              wall_surface=ws, roof_surface="thatch" if thatch else R["roof"][1])
    if R.get("quoins") and not par:
        quoins(M, "q", [(0.0, -W * 0.5, 1, 1), (0.0, W * 0.5, 1, -1), (L, -W * 0.5, -1, 1), (L, W * 0.5, -1, -1)], E, dr)
    # (the tower, a bellcote or a belfry; a west window when the west end is open)
    if tw:
        T, TH, kind = tw["T"], tw["TH"], tw["kind"]
        if tw.get("at") == "SW":
            cx, cy = T * 0.5 - 0.3, -W * 0.5 - T * 0.5 + 0.6
            opening(M, "tdoor", (cx, cy - T * 0.5, 0.0), SOUTH, 1.3, 2.0, "pointed" if win != "round" else "round", 0.25, dr, door=K.DOOR_OAK)
            window(M, "west", 0.0, 0.0, WEST, E, win, dr, scale=1.3)
        else:
            cx, cy = -T * 0.5 + 0.2, 0.0
            if kind == "round":
                cx = -T * 0.5 + 0.4
            else:
                opening(M, "tdoor", (cx - T * 0.5, cy, 0.0), WEST, 1.2, 2.0, "pointed" if win != "round" else "round", 0.25, dr, door=K.DOOR_OAK)
                ww = {"perp": 2.2, "dec": 1.5}.get(win)
                if ww and TH > 12:
                    opening(M, "twin", (cx - T * 0.5, cy, 3.2), WEST, ww, 2.6, "pointed", 0.2, dr, mullions=2 if win == "perp" else 1)
        tower(M, "tower", cx, cy, T, TH, R, kind, tw.get("spire", "stone"))
    else:
        window(M, "west", 0.0, 0.0, WEST, E + 1.0, win, dr, sill=2.0, scale=1.2 if win in ("dec", "perp") else 1.0)
        if spec.get("belfry"):
            belfry(M, "belfry", 1.6, nr, P, W)
        elif spec.get("bellcote", 2):
            bellcote(M, "bell", 0.0, nr, dr, double=spec.get("bellcote", 2) == 2)
    return M


def tin_tabernacle(spec):
    """A tin tabernacle: a prefabricated corrugated-iron mission church, painted, with a little fleche."""
    M = Model(spec["name"])
    L, W, E = 13.0, 6.5, 3.2
    walls = spec.get("colour", (0.04, 0.1, 0.05))
    roof = (0.18, 0.035, 0.025)
    trim = (0.55, 0.55, 0.5)
    nr = range_block(M, "body", 0.0, L, W, E, 45.0, walls, roof, roof_thick=0.08, overhang=0.35, verge=0.25,
                     wall_surface="tin", roof_surface="tin")
    for x in (2.5, 5.5, 8.5, 11.0):
        for s, f in ((-1, SOUTH), (1, NORTH)):
            opening(M, "w%d%d" % (int(x), s), (x, s * W * 0.5, 1.0), f, 0.6, 1.2, "pointed", 0.08, trim, surface="timber")
    opening(M, "e", (L, 0.0, 1.0), EAST, 0.9, 1.5, "pointed", 0.08, trim, surface="timber")
    # (a gabled entrance lobby at the west end)
    range_block(M, "lobby", -2.2, 0.0, 2.6, 2.6, 45.0, walls, roof, roof_thick=0.08, overhang=0.2, verge=0.15, wall_surface="tin",
                roof_surface="tin")
    opening(M, "door", (-2.2, 0.0, 0.0), WEST, 1.0, 1.7, "pointed", 0.08, trim, door=K.DOOR_PAINT, surface="timber")
    M.add(box("fleche", (L - 1.0, 0.0, nr + 0.5), (0.8, 0.8, 1.0)), "timber", trim, 0.3)
    M.add(lathe("fleche_cap", [(0.65, 0.0), (0.0, 2.0)], seg=4, at=(L - 1.0, 0.0, nr + 1.0), phase=math.pi / 4), "tin", roof, 0.3)
    cross(M, "x", (-2.2, 0.0, 2.6 + 1.3 + 0.1), trim, 0.6)
    return M


def chapel(spec):
    R = REGIONS[spec["region"]]
    wall, ws = spec.get("wall", R["wall"])
    return K.methodist_chapel(spec["name"], D=spec.get("D", 14.0), W=spec.get("W", 10.0), E=spec.get("E", 8.6), wall=wall,
                              dressed=spec.get("dressed", R["dressed"]), roof=spec.get("roof", (K.SLATE,))[0], tiers=spec.get("tiers", 2),
                              wall_surface=ws)


def S(name, label, region, **kw):
    kw.update(name="SM_Church_" + name, label=label, region=region)
    return kw


BRICK = (K.BRICK, "stone")
YELLOW_BRICK = ((0.42, 0.33, 0.18), "stone")
SPECS = [
    # (the Pennines and the Dales)
    S("dales_chapel", "Dales chapel of ease (stage 1)", "pennine", hero=K.dales_chapel),
    S("pennine_perp", "Pennine: Perpendicular tower, chancel", "pennine", nave=(16, 7.5, 5.0, 40), chancel=(7, 5.5, 4.2),
      tower=dict(kind="plain", T=5.0, TH=15.0), win="perp"),
    S("pennine_broach", "Mill town: broach spire, aisle, clerestory", "pennine", nave=(22, 8.0, 7.0, 50), chancel=(8, 6.5, 6.0),
      aisles="N", clerestory=True, tower=dict(kind="broach", T=5.5, TH=15.0, at="SW"), win="dec", roof=(K.SLATE, "roof"), buttresses=True),
    S("pennine_chapel", "Pennine Methodist chapel, two tiers", "pennine", kind="chapel", D=15, W=11, tiers=2),
    S("pennine_saddle", "Dales church: saddleback tower", "pennine", nave=(14, 6.8, 4.2, 40), tower=dict(kind="saddle", T=4.4, TH=10.5),
      win="lancet"),
    # (the Cotswolds)
    S("norman_tower", "Norman village church (stage 1)", "cotswold", hero=K.norman_tower),
    S("cots_wool", "Cotswold wool church: tall tower, aisles, battlements", "cotswold", nave=(24, 8.0, 8.0, 40), chancel=(9, 6.5, 6.0),
      aisles="NS", clerestory=True, parapet="battled", tower=dict(kind="perp", T=6.5, TH=24.0), win="perp", porch_x=4.5),
    S("cots_needle", "Cotswold: tower and needle spire", "cotswold", nave=(18, 7.5, 5.5, 50), chancel=(7, 5.6, 4.6), aisles="S",
      tower=dict(kind="needle", T=5.0, TH=13.0), win="dec"),
    S("cots_bellcote", "Cotswold hamlet church: bellcote", "cotswold", nave=(12, 6.0, 4.0, 50), chancel=(5, 4.6, 3.5), win="round",
      bellcote=1),
    S("cots_baptist", "Cotswold Baptist chapel, one tier", "cotswold", kind="chapel", D=12, W=9, tiers=1),
    # (East Anglia)
    S("ea_round_thatch", "Norfolk: round tower, thatched nave", "east_anglia", nave=(15, 6.8, 4.4, 52), chancel=(6, 5.2, 3.8),
      roof=(THATCH, "thatch"), tower=dict(kind="round", T=5.0, TH=13.0), win="dec"),
    S("ea_round_tile", "Suffolk: round tower, tiled roof", "east_anglia", nave=(16, 7.0, 4.8, 48), chancel=(7, 5.5, 4.0),
      tower=dict(kind="round", T=4.8, TH=14.0), win="lancet"),
    S("ea_wool", "East Anglian wool church: flint, clerestory, battlements", "east_anglia", nave=(28, 8.5, 9.0, 40), chancel=(10, 7.0, 7.0),
      aisles="NS", clerestory=True, parapet="battled", tower=dict(kind="perp", T=7.0, TH=28.0), win="perp", porch_x=5.0),
    S("ea_bellcote", "Fenland: flint, bellcote", "east_anglia", nave=(13, 6.2, 4.0, 48), win="lancet", bellcote=1),
    S("ea_methodist", "Norfolk Primitive Methodist chapel, brick", "east_anglia", kind="chapel", wall=BRICK, D=11, W=8.5, tiers=1),
    # (the south-east)
    S("se_belfry", "Weald: timber belfry, shingled spire", "south_east", nave=(14, 6.5, 4.0, 50), chancel=(6, 5.0, 3.6), belfry=True,
      win="lancet"),
    S("se_cap", "Sussex: tower with a low pyramid cap", "south_east", nave=(15, 7.0, 4.5, 50), chancel=(7, 5.5, 4.0),
      tower=dict(kind="norman", T=5.2, TH=12.0, spire="shingle"), win="lancet"),
    S("se_perp", "Kent ragstone: battlemented tower, aisle", "south_east", nave=(18, 7.5, 6.0, 45), chancel=(7, 6.0, 5.0), aisles="N",
      tower=dict(kind="plain", T=5.5, TH=17.0), win="perp"),
    S("se_tin", "Tin tabernacle", "south_east", kind="tin"),
    S("se_brick_broach", "Victorian brick: broach spire", "south_east", wall=BRICK, dressed=K.STUCCO, nave=(20, 8.0, 6.5, 52),
      chancel=(7, 6.0, 5.6), tower=dict(kind="broach", T=5.2, TH=13.0, at="SW"), win="dec", buttresses=True),
    # (Somerset)
    S("som_tower", "Somerset: tall pinnacled tower, aisles", "somerset", nave=(22, 8.0, 7.5, 40), chancel=(8, 6.5, 6.0), aisles="NS",
      clerestory=True, parapet="battled", tower=dict(kind="perp", T=6.5, TH=30.0), win="perp", porch_x=4.5),
    S("som_village", "Somerset village: Perpendicular tower", "somerset", nave=(16, 7.0, 5.2, 45), chancel=(7, 5.5, 4.5),
      tower=dict(kind="perp", T=5.2, TH=18.0), win="perp", buttresses=True),
    S("som_small", "Somerset Levels: small church, bellcote", "somerset", nave=(12, 6.0, 4.0, 45), chancel=(5, 4.6, 3.5), win="lancet"),
    S("som_chapel", "Somerset Wesleyan chapel, stone", "somerset", kind="chapel", D=15, W=11, tiers=2),
    # (Cornwall)
    S("corn_granite", "Cornwall: granite tower, wide aisles", "cornwall", nave=(20, 7.5, 5.0, 40), chancel=(6, 7.5, 5.0), aisles="NS",
      aisle=(5.5, 4.4), tower=dict(kind="perp", T=5.8, TH=20.0), win="perp"),
    S("corn_small", "Cornish cove church: bellcote", "cornwall", nave=(13, 6.0, 3.8, 42), win="round", bellcote=1),
    S("corn_chapel", "Cornish Methodist chapel, granite", "cornwall", kind="chapel", D=13, W=10, tiers=1),
    # (Wales)
    S("wal_bellcote", "Welsh hill church: double bellcote", "wales", nave=(14, 6.0, 3.8, 48), chancel=(5, 4.6, 3.4), win="lancet"),
    S("wal_tower", "Pembrokeshire: tall slim battlemented tower", "wales", nave=(15, 6.6, 4.4, 48), chancel=(6, 5.0, 3.8),
      tower=dict(kind="plain", T=4.4, TH=17.0), win="lancet"),
    S("wal_capel", "Welsh capel, two tiers", "wales", kind="chapel", D=16, W=13, tiers=2, dressed=(0.5, 0.48, 0.42)),
    S("wal_capel_render", "Welsh capel, rendered, one tier", "wales", kind="chapel", D=12, W=9.5, tiers=1, wall=((0.55, 0.55, 0.52), "render"),
      dressed=(0.18, 0.2, 0.22)),
    S("wal_saddle", "Welsh valley: saddleback tower", "wales", nave=(16, 7.0, 4.8, 48), chancel=(6, 5.2, 4.0),
      tower=dict(kind="saddle", T=4.8, TH=12.5), win="dec"),
    # (towns)
    S("victorian_spire", "Victorian Gothic town church (stage 1)", "town", hero=K.victorian_spire),
    S("town_brick_perp", "Town church in brick: pinnacled tower, aisles", "town", wall=BRICK, dressed=K.STUCCO, nave=(24, 9.0, 8.0, 52),
      chancel=(8, 7.0, 6.5), aisles="NS", clerestory=True, tower=dict(kind="perp", T=6.5, TH=22.0), win="dec"),
    S("town_needle", "Victorian limestone: needle spire, aisles", "town", wall=(K.LIME, "stone"), dressed=K.LIME_DRESSED,
      nave=(24, 8.5, 8.0, 55), chancel=(9, 7.0, 7.0), aisles="NS", clerestory=True, tower=dict(kind="needle", T=6.0, TH=17.0, at="SW"),
      win="dec"),
    S("town_catholic", "Catholic church: brick, big west window", "town", wall=YELLOW_BRICK, dressed=K.STUCCO, nave=(22, 10.0, 8.5, 50),
      chancel=(6, 8.0, 7.5), win="dec", bellcote=1, buttresses=True, porch=None),
    S("methodist_chapel", "Methodist chapel (stage 1)", "town", hero=K.methodist_chapel),
    # (Scotland)
    S("sco_kirk", "Scottish kirk: harled, birdcage bellcote", "scotland", nave=(15, 7.5, 4.6, 45), win="round", bellcote=1, porch=None),
    S("sco_tower", "Scottish parish kirk: tower and slate spire", "scotland", nave=(18, 9.0, 6.0, 45), tower=dict(kind="needle", T=4.8, TH=14.0,
      spire="slate"), win="round"),
    S("sco_free", "Scottish Free Church: sandstone, needle spire", "scotland", wall=((0.3, 0.27, 0.22), "stone"), dressed=(0.38, 0.34, 0.27),
      nave=(20, 10.0, 8.0, 50), tower=dict(kind="needle", T=5.0, TH=16.0, at="SW"), win="dec", buttresses=True, porch=None),
]


def build_one(spec):
    if "hero" in spec:
        return spec["hero"]()
    kind = spec.get("kind", "church")
    return {"church": church, "chapel": chapel, "tin": tin_tabernacle}[kind](spec)


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    K.clear()
    models = []
    for spec in SPECS:
        M = build_one(spec)
        M.label = spec["label"]
        models.append(M)
    dg = bpy.context.evaluated_depsgraph_get()
    for M in models:
        tris = 0
        for p in M.parts:
            me = p.obj.evaluated_get(dg).to_mesh()
            me.calc_loop_triangles()
            tris += len(me.loop_triangles)
        M.tris = tris
        print("%-34s %4d parts %6d triangles" % (M.name, len(M.parts), tris))
    if "--render" in args:
        from blender_church_kit_render import render_grid
        render_grid(models, args[args.index("--render") + 1])


if __name__ == "__main__":
    main()
