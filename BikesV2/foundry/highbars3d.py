"""Common parts, stage 8b: high bars for choppers and cruisers (common parts plan, chopper specials).

Four tall bars built on the stage 1 bar library (bars3d): the same clamp at the bike's riser point, the same
22.2 mm bar dead straight through the grips, so the stage 2 levers, switches and mirrors ride on them as on any
bar. Only the path between the clamp and the grip is new: legs that climb out of the clamp and bend over into
the grips. The rider's hands go where the grips go (the pose thread reads the hand point from the bar).

    ape     Classic ape hanger: legs straight up about 300 mm, round top bends, grips pulled back.
    ape_tall  Tall ape: about 400 mm, narrower legs.
    zbar    Z-bar: legs climb outwards on a slant between two sharp bends, then flat grips.
    beach   Beach bar: wide and only 150 mm up, pulled far back to the rider.

    python3 foundry/highbars3d.py shots [keys] [--samples N]   # out/highbars/*.png
    python3 foundry/highbars3d.py check                         # lock and clipping on the chopper and cruiser leads
"""
import json
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bars3d as BR  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "out", "highbars")
FAMS = ("Chopper", "Cruiser")

# rise and back: where the middle of the grip ends up from the clamp; leg_y: how far out the legs stand;
# lean: how far the legs lean back over their height; bend: corner rounding passes (more = rounder)
HIGH = {
    "ape": dict(label="Ape hanger", note="Legs straight up about 300 mm with round top bends, the grips pulled back and a little down.",
                rise=300, back=120, y=330, sweep=14, up=-4, leg_y=165, lean=30, bend=4, finish="chrome", families="Chopper, Cruiser"),
    "ape_tall": dict(label="Tall ape", note="About 400 mm tall on narrower legs: the show chopper bar.",
                     rise=400, back=130, y=320, sweep=16, up=-6, leg_y=150, lean=40, bend=4, finish="chrome", families="Chopper"),
    "zbar": dict(label="Z-bar", note="Legs climb outwards on a slant between two sharp bends, then flat grips: an angular, modern look.",
                 rise=250, back=90, y=340, sweep=8, up=0, leg_y=120, lean=20, bend=2, zig=True, finish="satin_black", families="Chopper, Cruiser"),
    "beach": dict(label="Beach bar", note="Wide, only 150 mm up and pulled far back to the rider, for a laid-back cruise.",
                  rise=150, back=230, y=350, sweep=24, up=-2, kick=62, kb=34, finish="chrome", families="Cruiser, Chopper", plain=True),
}

_orig_half = BR.bar_half


def chaikin3(pts, n):
    """Round the corners of an open 3D polyline, keeping both ends where they are."""
    for _ in range(n):
        out = [pts[0]]
        for a, b in zip(pts, pts[1:]):
            out += [tuple(0.75 * a[k] + 0.25 * b[k] for k in range(3)), tuple(0.25 * a[k] + 0.75 * b[k] for k in range(3))]
        out.append(pts[-1])
        pts = out
    return pts


def dense(pts, step=6.0):
    out = [pts[0]]
    for a, b in zip(pts, pts[1:]):
        L_ = BR.vlen(BR.vsub(b, a))
        n = max(1, int(L_ / step))
        out += [BR.vadd(a, BR.vsub(b, a), j / n) for j in range(1, n + 1)]
    return out


def high_half(st, C, s):
    """Centre line of one half of a high bar: out of the clamp, up the leg, over the top bend into the grip.
    Returns (points, hand point, grip start g0, grip direction d, end point), as bars3d.bar_half."""
    hb = st["high"]
    d = BR.grip_dir(st, s)
    H = (C[0] - st["back"], s * st["y"], C[2] + st["rise"])
    g0 = BR.vadd(H, d, -BR.HAND_IN)
    E = BR.vadd(H, d, BR.GRIP_L - BR.HAND_IN)
    S = BR.vadd(g0, d, -BR.CTRL)            # where the straight grip section starts (switches and perch sit on it)
    ly = hb["leg_y"]
    if hb.get("zig"):
        knee = (C[0] - 4, s * 78.0, C[2] + 26)
        top = (C[0] - hb["lean"], s * ly + s * (abs(S[1]) - ly) * 0.15, S[2] + 6)
        poly = [(C[0], s * 37.5, C[2]), (C[0], s * 58.0, C[2]), knee, (C[0] - hb["lean"] * 0.8, s * (ly * 0.9), S[2] - 30), top, BR.vadd(S, d, -10), S]
    else:
        foot = (C[0], s * (ly - 10), C[2] + 6)
        topc = (C[0] - hb["lean"], s * ly, S[2] - 18)
        poly = [(C[0], s * 37.5, C[2]), (C[0], s * 62.0, C[2]), foot, (C[0] - hb["lean"] * 0.3, s * ly, C[2] + 50), topc,
                (C[0] - hb["lean"] - (C[0] - hb["lean"] - S[0]) * 0.2, s * (ly + 8), S[2] + 2), BR.vadd(S, d, -14), S]
    curve = chaikin3(dense(poly, 8.0), hb.get("bend", 4))
    straight = [BR.vadd(S, d, BR.vlen(BR.vsub(E, S)) * j / 12) for j in range(1, 13)]
    pts = [(C[0], 0.0, C[2])] + curve + straight
    return pts, H, g0, d, E


def bar_half(st, C, s):
    return high_half(st, C, s) if st.get("high") else _orig_half(st, C, s)


def register():
    BR.bar_half = bar_half
    for k, v in HIGH.items():
        st = dict(v)
        if not v.get("plain"):
            st["high"] = dict(leg_y=v["leg_y"], lean=v["lean"], bend=v["bend"], zig=v.get("zig", False))
        BR.BARS[k] = st


register()


def built(mn, bar, grip="ribbed", end="weighted", fin=None):
    BR.fresh()
    return BR.built(mn, bar, grip, end, fin)


SHOTS = [
    ("ape", "lodestar_chopper", "ribbed", "weighted"),
    ("ape_tall", "lodestar_chopper", "ribbed", "weighted"),
    ("zbar", "lodestar_cruiser", "knurl", "weighted"),
    ("beach", "lodestar_cruiser", "ribbed", "weighted"),
]


def shots(which=None, samples=40):
    os.makedirs(OUT, exist_ok=True)
    for bar, mn, grip, end in SHOTS:
        if which and bar not in which:
            continue
        b3, m, cc, lines, made = built(mn, bar, grip, end)
        H = lines[1][3]
        sc = bpy.context.scene
        b3.studio(sc)
        sc.cycles.samples = samples
        sc.render.resolution_x, sc.render.resolution_y = 1100, 800
        tgt = (H[0] + 80, 0, H[2] - 260)
        BR.shoot(sc, os.path.join(OUT, f"bar_{bar}.png"), BR.vadd(tgt, (1600, 1150, 240)), tgt, 40)


def check(keys=None):
    res = {}
    leads = {"Chopper": "lodestar_chopper", "Cruiser": "lodestar_cruiser"}
    for fam, mn in leads.items():
        for bar, h in HIGH.items():
            if fam not in h["families"] or (keys and bar not in keys):
                continue
            b3, m, cc, lines, made = built(mn, bar)
            lk = BR.lock_check(m, cc)
            # the right switch pod clamps on the bar, kill switch and all: a joint, not a clash
            bad = {k: v for k, v in BR.clip_check(made).items() if not BR.joint_ok(k) and k != "bar_lib | swkill"}
            H = lines[1][3]
            own = BR.model_hand(m)
            move = BR.vlen(BR.vsub(H, own))
            res.setdefault("bar", {}).setdefault(bar, {})[fam] = dict(model=mn, fitted=True, clips=bad, lock=lk, hand_move=round(move), rep=dict(bar=bar))
            print(f"HCHECK {bar:9s} {fam:8s} lock {lk} hand moves {move:.0f} mm clips {bad}", flush=True)
    os.makedirs(OUT, exist_ok=True)
    json.dump(res, open(os.path.join(OUT, "check.json"), "w"), indent=1)


if __name__ == "__main__":
    a = sys.argv[1:]
    cmd = a[0] if a else "shots"
    smp = int(a[a.index("--samples") + 1]) if "--samples" in a else 40
    rest = [x for x in a[1:] if not x.startswith("--") and x != str(smp)]
    if cmd == "shots":
        shots(rest or None, smp)
    elif cmd == "check":
        check(rest or None)
