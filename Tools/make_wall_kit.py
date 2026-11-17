"""Ride UK's dry stone walls, stone by stone, in each part of Britain's own walling (build 154: beenster,
"the walls remain too low def; I remember how detailed they were in Buttertubs", then "don't just use
Buttertubs walls, they're for Yorkshire, but make other walls more defined"). Written once here as
Data/RideUK/Walls/*.glb and kept in the repo; Tools/ue_build_rideuk.py (prop_kit) imports them to
/Game/RideUK/Walls and lists them in the ride's prop kit, each style in M_RideStone tinted its stone;
World/WorldRideScenery.cpp lays them along the map's walls on the finest squares, each style where its
region is (RidePlan::WallStyle).

    SM_Prop_wall_stone_<4s+k>   2 m of wall in style s, variant k (0..3), centred on x = 0
    SM_Prop_wall_end_<4s+k>     a 1 m wall head in style s, finished at +x

The styles (WALL_STYLES, in the game's order):
    0 dales       Buttertubs' own Dales limestone wall (Tools/buttertubs_stone.py, same seeds)
    1 cotswold    thin beds of honey limestone, many courses, a cock-and-hen cope of thin slabs on edge
    2 slate       Lakes and Welsh slate: long thin flat slabs, through stones showing, tall slate copes
    3 gritstone   Peak and Pennine gritstone: big squared blocks in few courses, a rounded block cope
    4 granite     a Scottish dyke: rounded boulders, rough courses, a cope of round stones
    5 sandstone   squared sandstone in level courses, a flat saddle cope (the rest of England, the Moors)

Same frame and vertex colour masks as buttertubs_stone (metres, x along, y up, z across; R brightness,
G which scan, B lichen, A moss), so M_RideStone draws them as it draws Buttertubs' walls.

    python Tools/make_wall_kit.py
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import buttertubs_stone as ST            # noqa: E402
from export_buttertubs import write_glb  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "RideUK", "Walls")
VARIANTS = 4
WALL_STYLES = ["dales", "cotswold", "slate", "gritstone", "granite", "sandstone"]

# Each style: its height to the cope and half widths, its course tops, the stones' length, depth and how
# much of a course's height each fills, their shape (stone()'s round_, chops, chop_depth, bump), how far
# each sits off the line (jitter), the footing, and the cope.
STYLES = {
    "cotswold": dict(h=0.95, foot=0.36, top=0.22, courses=np.linspace(0.12, 0.95, 11).round(3).tolist(),
                     length=(0.18, 0.42), depth=(0.16, 0.26), fill=(0.75, 0.95), round_=(0.12, 0.22), chops=3,
                     chop_depth=(0.8, 0.97), bump=0.006, tilt=0.03, jitter=0.012, res=(5, 8), bright=0.5,
                     bare=(0.6, 1.0), cope="upright", cope_h=(0.18, 0.26), cope_t=(0.04, 0.08), cope_lean=0.035),
    "slate": dict(h=1.15, foot=0.4, top=0.2, courses=np.linspace(0.12, 1.15, 13).round(3).tolist(),
                  length=(0.3, 0.75), depth=(0.18, 0.3), fill=(0.6, 0.85), round_=(0.08, 0.16), chops=4,
                  chop_depth=(0.7, 0.95), bump=0.004, tilt=0.06, jitter=0.02, res=(5, 8), bright=0.36,
                  bare=(0.7, 1.0), throughs=True, cope="upright", cope_h=(0.22, 0.32), cope_t=(0.04, 0.07), cope_lean=0.03),
    "gritstone": dict(h=1.08, foot=0.4, top=0.26, courses=[0.16, 0.38, 0.6, 0.82, 1.08],
                      length=(0.38, 0.8), depth=(0.2, 0.3), fill=(0.8, 0.95), round_=(0.14, 0.26), chops=3,
                      chop_depth=(0.85, 0.97), bump=0.01, tilt=0.03, jitter=0.012, res=(6, 10), bright=0.4,
                      bare=(0.2, 0.6), cope="round", cope_h=(0.22, 0.28), cope_t=(0.45, 0.8)),
    "granite": dict(h=1.15, foot=0.45, top=0.26, courses=[0.2, 0.44, 0.66, 0.86, 1.02, 1.15],
                    length=(0.28, 0.6), depth=(0.22, 0.34), fill=(0.7, 0.98), round_=(0.45, 0.8), chops=2,
                    chop_depth=(0.75, 0.95), bump=0.02, tilt=0.09, jitter=0.03, res=(6, 10), bright=0.48,
                    bare=(0.0, 0.4), cope="boulder", cope_h=(0.2, 0.32), cope_t=(0.22, 0.4)),
    "sandstone": dict(h=1.05, foot=0.36, top=0.24, courses=[0.14, 0.32, 0.5, 0.67, 0.84, 1.05],
                      length=(0.32, 0.62), depth=(0.18, 0.28), fill=(0.85, 0.97), round_=(0.1, 0.18), chops=3,
                      chop_depth=(0.88, 0.98), bump=0.006, tilt=0.02, jitter=0.008, res=(6, 10), bright=0.46,
                      bare=(0.3, 0.8), cope="saddle", cope_h=(0.2, 0.24), cope_t=(0.5, 0.9)),
}


def wall_length(M, rng, s, x0, x1, end_hi=False):
    """Stones of a wall from x0 to x1 in style s, faces battered from its foot to its top half widths."""
    H, FOOT, TOP = s["h"], s["foot"], s["top"]
    hw_at = lambda y: FOOT + (TOP - FOOT) * min(max(y / H, 0.0), 1.0)
    batter = math.atan((FOOT - TOP) / H)
    # Hearting: dark rubble behind the faces, seen through the gaps.
    cp = []
    for y in (-0.3, H + 0.02):
        h = hw_at(max(y, 0)) - 0.08
        cp += [[x0 + 0.02, y, -h], [x1 - 0.02, y, -h], [x1 - 0.02, y, h], [x0 + 0.02, y, h]]
    cp = np.array(cp)
    T = np.array([[0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7], [0, 1, 5], [0, 5, 4], [1, 2, 6], [1, 6, 5],
                  [2, 3, 7], [2, 7, 6], [3, 0, 4], [3, 4, 7]])
    cc = np.zeros((8, 4)); cc[:, 0] = 0.07; cc[:, 1] = 1.0
    M.add(cp, T, cc, cp[:, [0, 1]] + cp[:, [2, 2]])

    def place(cx, cy, cz, sx, sy, sz, R, moss, res=None, round_=None):
        P, Tt, uv = ST.stone(rng, (sx, sy, sz), (cx, cy, cz), R, res=res or s["res"], round_=round_ or s["round_"],
                             chops=s["chops"], chop_depth=s["chop_depth"], bump=s["bump"])
        col = ST.stone_col(rng, len(P), s["bright"] + rng.normal(0, 0.06), rng.uniform(*s["bare"]))
        col[:, 3] = np.clip(ST.moss_for(P, H, rng, moss) + rng.normal(0, 0.03, len(P)), 0, 1)
        M.add(P, Tt, col, uv)

    # Footings: big stones a little proud of the face, mostly buried.
    for side in (-1, 1):
        x = x0 - rng.uniform(0, 0.25)
        while x < x1 - 0.1:
            ln = min(rng.uniform(0.45, 0.8), x1 - x)
            h = rng.uniform(0.3, 0.4); d = rng.uniform(0.3, 0.42)
            R = ST.rot_xyz(rng.normal(0, 0.05), rng.normal(0, 0.06), rng.normal(0, 0.04))
            place(x + ln / 2, s["courses"][0] - h / 2, side * (FOOT + 0.04 - d / 2), ln / 2, h / 2, d / 2, R, 0.6, res=(6, 10))
            x += ln + rng.uniform(0.0, 0.03)

    # Courses of face stones; each course's seams shifted by the same amounts in every variant, so the
    # lengths line up course for course.
    yb = s["courses"][0]
    shift = np.random.default_rng(7).uniform(-0.14, 0.14, len(s["courses"]))
    for ci, yt in enumerate(s["courses"][1:]):
        ch = yt - yb
        for side in (-1, 1):
            lo = x0 + shift[ci]
            hi = x1 + (shift[ci] if not end_hi else -(0.22 if ci % 2 == 0 else 0.0))
            x = lo
            while x < hi - 0.04:
                ln = rng.uniform(*s["length"])
                if hi - (x + ln) < 0.12:
                    ln = hi - x
                gap = rng.uniform(0.004, 0.025)
                h = ch * rng.uniform(*s["fill"])
                d = rng.uniform(*s["depth"])
                cy = yb + h / 2 + rng.uniform(0, ch - h) * 0.6 + 0.004
                face = hw_at(cy) + rng.normal(0, s["jitter"])
                R = ST.rot_xyz(-side * batter + rng.normal(0, s["tilt"]), rng.normal(0, s["tilt"]), rng.normal(0, s["tilt"]))
                place(x + ln / 2, cy, side * (face - d / 2), ln / 2 - gap / 2, h / 2, d / 2, R, 0.06)
                x += ln
        if end_hi:
            # The head: a header right across the wall, then a pair of stretchers, in turn.
            if ci % 2 == 0:
                ln = rng.uniform(0.28, 0.38); h = ch * 0.94; cy = yb + h / 2 + 0.004
                place(x1 - ln / 2, cy, 0.0, ln / 2, h / 2, hw_at(cy) + 0.012, ST.rot_xyz(0, rng.normal(0, 0.02), 0), 0.06,
                      round_=(0.12, 0.2))
            else:
                for side in (-1, 1):
                    ln = rng.uniform(0.45, 0.6); h = ch * 0.92; cy = yb + h / 2 + 0.004; w = hw_at(cy) / 2
                    place(x1 - ln / 2, cy, side * w, ln / 2, h / 2, w - 0.005, ST.rot_xyz(-side * batter, 0, 0), 0.06,
                          round_=(0.12, 0.2))
        # Through stones half way up, sticking out a hand's width: the slate walls show theirs.
        if s.get("throughs") and abs(yt - H * 0.5) < ch * 0.6:
            for xt in np.arange(x0 + rng.uniform(0.2, 0.5), x1 - 0.15, 0.75 + rng.uniform(-0.1, 0.1)):
                w = hw_at(yt) + rng.uniform(0.08, 0.16)
                place(xt, yt + 0.03, rng.normal(0, 0.03), rng.uniform(0.12, 0.2), 0.035, w, ST.rot_xyz(0, rng.normal(0, 0.05), 0), 0.2)
        yb = yt

    # The cope along the top.
    x = x0 + shift[-1] * 0.5
    hi = x1 - 0.03 if end_hi else x1 + shift[-1] * 0.5
    lean = 0.0
    while x < hi - 0.03:
        t = rng.uniform(*s["cope_t"])
        if hi - (x + t) < 0.05:
            t = hi - x
        h = rng.uniform(*s["cope_h"])
        kind = s["cope"]
        if kind == "upright":
            # thin slabs on edge, leaning together; every few a taller one (the cock of cock-and-hen)
            if rng.uniform() < 0.25:
                h *= 1.3
            w = TOP + rng.uniform(0.01, 0.05)
            lean = 0.6 * lean + rng.normal(0, s.get("cope_lean", 0.07))
            R = ST.rot_xyz(rng.normal(0, 0.04), rng.normal(0, 0.05), lean)
            P, Tt, uv = ST.stone(rng, (t / 2, h / 2, w), (x + t / 2, H + h / 2 - 0.03, rng.normal(0, 0.015)), R,
                                 res=(5, 8), round_=(0.15, 0.3), chops=4, chop_depth=(0.6, 0.9))
        elif kind == "round":
            # half-round blocks laid across, flat below and round above
            w = TOP + 0.04
            P, Tt, uv = ST.stone(rng, (t / 2, h / 2, w), (x + t / 2, H + h * 0.25, 0.0), ST.rot_xyz(0, rng.normal(0, 0.02), 0),
                                 res=(6, 12), round_=(0.6, 0.7), chops=1, chop_depth=(0.95, 0.99), bump=0.006)
            P[:, 1] = np.maximum(P[:, 1], H - 0.01)
        elif kind == "saddle":
            # dressed saddle-backed copes: a square block with a ridge along its middle, overhanging a little
            w = TOP + 0.05
            xa, xb = x, x + t - 0.006
            yb, yr = H + h * 0.45, H + h
            Pb, Tb, ub = ST.box((xa, H - 0.01, -w), (xb, yb, w))
            Pr = np.array([[xa, yb, -w], [xa, yb, w], [xa, yr, 0.0], [xb, yb, -w], [xb, yb, w], [xb, yr, 0.0]])
            Tr = np.array([[0, 2, 1], [3, 4, 5], [0, 3, 5], [0, 5, 2], [1, 2, 5], [1, 5, 4]])
            fn = np.cross(Pr[Tr[:, 1]] - Pr[Tr[:, 0]], Pr[Tr[:, 2]] - Pr[Tr[:, 0]])
            inward = (fn * (Pr[Tr].mean(1) - Pr.mean(0))).sum(1) < 0
            Tr[inward] = Tr[inward][:, [0, 2, 1]]
            Tr = Tr + len(Pb)
            P = np.vstack([Pb, Pr]); Tt = np.vstack([Tb, Tr])
            uv = np.vstack([ub, np.stack([Pr[:, 0], Pr[:, 1] + np.abs(Pr[:, 2])], 1)]) + rng.uniform(0, 5, 2)
        else:
            # rounded field stones heaped along the top
            w = TOP * rng.uniform(0.7, 1.0)
            P, Tt, uv = ST.stone(rng, (t / 2, h / 2, w), (x + t / 2, H + h * 0.4, rng.normal(0, 0.03)),
                                 ST.rot_xyz(rng.normal(0, 0.2), rng.normal(0, 0.3), rng.normal(0, 0.2)),
                                 res=(6, 10), round_=(0.6, 0.9), chops=1, chop_depth=(0.8, 0.95), bump=0.02)
        col = ST.stone_col(rng, len(P), s["bright"] + rng.normal(0, 0.05), rng.uniform(*s["bare"]), lichen=rng.uniform(0.2, 0.8))
        top = np.clip((P[:, 1] - H - 0.08) / 0.15, 0, 1)
        col[:, 3] = np.clip(0.12 + 0.5 * top * rng.uniform(0.2, 1.0) ** 2, 0, 1)
        M.add(P, Tt, col, uv)
        x += t + rng.uniform(0.0, 0.012)


def wall_module(style, rng):
    M = ST.Mesh()
    wall_length(M, rng, STYLES[style], -1.0, 1.0)
    return M


def wall_end(style, rng):
    """A wall head: the last metre finished at +x, coming down a little to its end, as wall ends do."""
    M = ST.Mesh()
    wall_length(M, rng, STYLES[style], -0.5, 0.5, end_hi=True)
    for P in M.P:
        t = np.clip((P[:, 0] + 0.5), 0, 1) ** 1.3
        up = P[:, 1] > 0.15
        P[up, 1] = 0.15 + (P[up, 1] - 0.15) * (1.0 - 0.25 * t[up])
        P[:, 2] *= 1.0 - 0.1 * t
    return M


def main():
    os.makedirs(OUT, exist_ok=True)
    total = 0
    for si, style in enumerate(WALL_STYLES):
        for k in range(VARIANTS):
            n = si * VARIANTS + k
            if style == "dales":
                v = "ABCD"[k]
                lengths = ST.wall_module(np.random.default_rng(100 + ord(v)))
                head = ST.wall_end(np.random.default_rng(200 + ord(v)))
            else:
                lengths = wall_module(style, np.random.default_rng(1000 * si + k))
                head = wall_end(style, np.random.default_rng(1000 * si + 500 + k))
            for name, M in (("SM_Prop_wall_stone_%d" % n, lengths), ("SM_Prop_wall_end_%d" % n, head)):
                nv, nt = M.write(os.path.join(OUT, name + ".glb"), write_glb, name)
                total += nt
                print("%-26s %-10s %6d triangles" % (name, style, nt))
    print("%d triangles in all, in %s" % (total, OUT))


if __name__ == "__main__":
    main()
