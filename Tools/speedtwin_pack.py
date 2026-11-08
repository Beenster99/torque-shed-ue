"""The classic part pack for the Speed Twin: alternative parts a player fits in the shed, each a fit
unit (or a few) replacing stock units of the same id, modelled in place on the stock bike like the
stock parts (Tools/make_speedtwin.py), so the fitment system (Source/TorqueShedProto/Fit) moves
them onto other parts by their sockets.

make_speedtwin_fit.py calls pack_options(); every option is (slot, id, name, catalogue [kind, id]
or None, units, meshes {name: (Mesh, finish, group)}). Catalogue ids are Data/Parts/parts.json's
(the Godot catalogue); parts it hasn't got (grips, levers, a rack) have none. The stock bike is
never changed here. FINISHES are the pack's own finishes, written into fitment.json.
"""
import math

import numpy as np

import make_speedtwin as st

FA, RA = st.FRONT_AXLE, st.REAR_AXLE
F = st.finish
FINISHES = {
    # Paint to match the catalogue's colours, under clear coat like the stock black.
    "paint_red": F((0.50, 0.035, 0.025), 0.0, 0.2, "paint", detail="flake", uv_scale=25.0, normal_strength=0.0),
    "paint_cream": F((0.78, 0.72, 0.58), 0.0, 0.22, "paint", detail="flake", uv_scale=25.0, normal_strength=0.0),
    # Red with a cream pinstripe (the mask).
    "paint_red_stripe": F((0.50, 0.035, 0.025), 0.0, 0.2, "paint", color2=(0.80, 0.74, 0.60), detail="flake",
                          uv_scale=25.0, normal_strength=0.0),
    "alu_polished": F((0.90, 0.90, 0.89), 1.0, 0.1, detail="brushed", uv_scale=10.0, normal_strength=0.2, rough_var=0.15),
    "leather_brown": F((0.045, 0.030, 0.022), 0.0, 0.55, detail="leather", uv_scale=7.0, normal_strength=0.8, rough_var=0.3),
    "leather_tan": F((0.30, 0.16, 0.07), 0.0, 0.5, detail="leather", uv_scale=7.0, normal_strength=0.8, rough_var=0.35),
    "leather_maroon": F((0.17, 0.018, 0.016), 0.0, 0.5, detail="leather", uv_scale=7.0, normal_strength=0.8, rough_var=0.3),
    "piping_white": F((0.80, 0.80, 0.78), 0.0, 0.45, detail="leather", uv_scale=7.0, normal_strength=0.5, rough_var=0.2),
    "piping_cream": F((0.76, 0.70, 0.56), 0.0, 0.45, detail="leather", uv_scale=7.0, normal_strength=0.5, rough_var=0.2),
}


# --- Shapes ---------------------------------------------------------------------------------------

def section_sweep(path, prof, up_hint=(0, 1, 0)):
    """A closed profile (k, 2): (along the path's normal, along its binormal), the first half the
    outer skin and the second half the inner one reversed, swept along a path; the two open ends
    are closed between the skins (good for U-sections such as a valanced guard)."""
    path = np.asarray(path, np.float64)
    prof = np.asarray(prof, np.float64)
    n, k = len(path), len(prof)
    tan, N, B = st.frames(path, up_hint)
    pts = path[:, None, :] + N[:, None, :] * prof[None, :, 0:1] + B[:, None, :] * prof[None, :, 1:2]
    along = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))])
    round_ = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(np.vstack([prof, prof[:1]]), axis=0), axis=1))])[:k]
    uv = np.stack([np.broadcast_to(round_[None, :], (n, k)), np.broadcast_to(along[:, None], (n, k))], -1)
    tris = list(st.grid_tris(n, k, wrap_cols=True))
    h = k // 2
    for i, flip in ((0, True), (n - 1, False)):
        o = i * k
        for j in range(h - 1):
            a, b = o + j, o + j + 1
            c, d = o + k - 1 - j, o + k - 2 - j
            q = [(a, c, b), (b, c, d)]
            tris += [(x, z, y) for x, y, z in q] if flip else q
    return st.Mesh().add(pts.reshape(-1, 3), np.array(tris), uv.reshape(-1, 2))


def u_profile(w, depth, t, n=14, crown=6.0):
    """A valanced guard's section: crowned across its width w (half), its sides turned down to depth
    at the edges, t thick. Normal is out from the wheel."""
    b = np.linspace(-w, w, n)
    outer = np.stack([-depth * np.abs(b / w) ** crown, b], 1)
    inner = outer[::-1] + [-t, 0.0]
    inner[:, 1] *= (w - t) / w
    return np.vstack([outer, inner])


def arc(centre, r, a0, a1, n=28, z=0.0):
    a = np.radians(np.linspace(a0, a1, n))
    return np.stack([centre[0] + r * np.cos(a), centre[1] + r * np.sin(a), np.full(n, z)], 1)


def coil(a, b, r, wire, turns, n=12):
    """A coil spring from a to b."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    ax = b - a
    L = np.linalg.norm(ax); ax /= L
    u = np.cross(ax, [0, 0, 1]) if abs(ax[2]) < 0.9 else np.cross(ax, [1, 0, 0])
    u /= np.linalg.norm(u)
    w = np.cross(ax, u)
    t = np.linspace(0, 1, int(turns * n) + 1)
    ang = t * turns * 2 * math.pi
    pts = a + ax * (t * L)[:, None] + (np.cos(ang)[:, None] * u + np.sin(ang)[:, None] * w) * r
    return st.tube(pts, wire, 8)


# --- Seats ----------------------------------------------------------------------------------------
# The stock seat sits on the rails from x -0.77 to -0.11 (its pan follows them); every seat keeps
# that underside, so it sits down on the frame the same way.

SEAT_XS = np.linspace(-0.77, -0.11, 48)
SEAT_BOT = np.interp(SEAT_XS, [-0.77, -0.60, -0.45, -0.30, -0.11], [0.515, 0.505, 0.49, 0.47, 0.445])


def seat_unit(fit, meshes, xs, top, bot, hw):
    return {"id": "seat", "group": "body", "meshes": meshes, "slot": "saddle",
            "mount": {"to": "frame.seat_rails", "type": "seat_rails", "rail_mm": 180.0, "along": 0.2},
            "sockets": {}, "proxies": fit.slices(xs, top, bot, hw, 10)}


def seat_loft(xs, top, bot, hw, power=4.0):
    return st.sweep(np.stack([xs, (top + bot) / 2, 0 * xs], 1), hw, (top - bot) / 2, power, 96)


def seats(fit, cat):
    out = []
    xs, bot = SEAT_XS, SEAT_BOT
    t = (xs - xs[0]) / (xs[-1] - xs[0])

    # Flat bench: one long flat top for two, square edged, a thin chrome strip round its base.
    top = np.interp(xs, [-0.77, -0.75, -0.25, -0.15, -0.11], [0.548, 0.575, 0.572, 0.515, 0.478])
    hw = 0.118 + 0.022 * np.sin(np.clip(t * 1.2, 0, 1) * math.pi * 0.5)
    seat = seat_loft(xs, top, bot, hw, 5.0)
    trim = st.Mesh()
    for s in (-1, 1):
        trim.extend(st.tube(np.stack([xs[1:-1], bot[1:-1] + 0.012, s * hw[1:-1] * 0.99], 1), 0.003, 8))
    pan = st.sweep(np.stack([xs, bot + 0.004, 0 * xs], 1), hw * 0.95, 0.008, 4.0, 20)
    out.append(("saddle", "flat_bench", cat["saddle"]["flat_bench"]["name"], ["saddle", "flat_bench"],
                [seat_unit(fit, ["opt_seat_flat_bench", "opt_seat_flat_bench_pan", "opt_seat_flat_bench_trim"], xs, top, bot, hw)],
                {"opt_seat_flat_bench": (seat, "leather_brown", "body"), "opt_seat_flat_bench_pan": (pan, "black_satin", "body"),
                 "opt_seat_flat_bench_trim": (trim, "chrome", "body")}))

    # Ribbed bench: flat, black, ribs across the top and white piping round the top edge.
    top = np.interp(xs, [-0.77, -0.755, -0.22, -0.14, -0.11], [0.55, 0.572, 0.568, 0.512, 0.476])
    hw = 0.112 + 0.025 * np.sin(np.clip(t * 1.2, 0, 1) * math.pi * 0.5)
    seat = seat_loft(xs, top, bot, hw, 4.5)
    ribs = st.Mesh()
    for x in np.arange(-0.73, -0.20, 0.035):
        k = np.interp(x, xs, top) - 0.001
        w = np.interp(x, xs, hw) * 0.84
        ribs.extend(st.sweep(np.array([(x, k, -w), (x, k, w)]), 0.006, 0.004, 2.0, 10, up_hint=(0, 1, 0)))
    piping = st.Mesh()
    for s in (-1, 1):
        piping.extend(st.tube(np.stack([xs[1:-1], top[1:-1] - 0.01, s * hw[1:-1] * 0.985], 1), 0.0035, 10))
    pan = st.sweep(np.stack([xs, bot + 0.004, 0 * xs], 1), hw * 0.95, 0.008, 4.0, 20)
    out.append(("saddle", "ribbed_bench", cat["saddle"]["ribbed_bench"]["name"], ["saddle", "ribbed_bench"],
                [seat_unit(fit, ["opt_seat_ribbed_bench", "opt_seat_ribbed_bench_ribs", "opt_seat_ribbed_bench_piping",
                                 "opt_seat_ribbed_bench_pan"], xs, top, bot, hw)],
                {"opt_seat_ribbed_bench": (seat, "leather", "body"), "opt_seat_ribbed_bench_ribs": (ribs, "leather", "body"),
                 "opt_seat_ribbed_bench_piping": (piping, "piping_white", "body"),
                 "opt_seat_ribbed_bench_pan": (pan, "black_satin", "body")}))

    # Tuck-and-roll dual seat: deep, maroon, pleats along it, cream piping and a grab strap.
    top = np.interp(xs, [-0.77, -0.755, -0.62, -0.45, -0.30, -0.15, -0.11], [0.56, 0.588, 0.592, 0.575, 0.535, 0.49, 0.476])
    hw = 0.118 + 0.03 * np.sin(np.clip(t * 1.3, 0, 1) * math.pi * 0.5)
    seat = seat_loft(xs, top, bot, hw, 3.6)
    pleats = st.Mesh()
    k = slice(2, -2)
    for f in np.linspace(-0.72, 0.72, 7):
        pleats.extend(st.tube(np.stack([xs[k], top[k] - 0.002 - 0.012 * f * f, f * hw[k]], 1), 0.009, 10))
    piping = st.Mesh()
    for s in (-1, 1):
        piping.extend(st.tube(np.stack([xs[1:-1], top[1:-1] - 0.016, s * hw[1:-1] * 0.975], 1), 0.004, 10))
    xg = -0.47
    yg = np.interp(xg, xs, top) + 0.004
    wg = np.interp(xg, xs, hw)
    strap_pts = [(xg, np.interp(xg, xs, bot) + 0.015, -wg - 0.004), (xg, yg - 0.03, -wg - 0.006), (xg, yg, -wg * 0.6),
                 (xg, yg + 0.002, 0.0), (xg, yg, wg * 0.6), (xg, yg - 0.03, wg + 0.006),
                 (xg, np.interp(xg, xs, bot) + 0.015, wg + 0.004)]
    strap = st.sweep(st.polyline(strap_pts, 5), 0.016, 0.003, 4.0, 12, caps=True, up_hint=(1, 0, 0))
    pan = st.sweep(np.stack([xs, bot + 0.004, 0 * xs], 1), hw * 0.95, 0.008, 4.0, 20)
    out.append(("saddle", "classic_tuck_seat", cat["saddle"]["classic_tuck_seat"]["name"], ["saddle", "classic_tuck_seat"],
                [seat_unit(fit, ["opt_seat_tuck", "opt_seat_tuck_pleats", "opt_seat_tuck_piping", "opt_seat_tuck_strap",
                                 "opt_seat_tuck_pan"], xs, top + 0.012, bot, hw)],
                {"opt_seat_tuck": (seat, "leather_maroon", "body"), "opt_seat_tuck_pleats": (pleats, "leather_maroon", "body"),
                 "opt_seat_tuck_piping": (piping, "piping_cream", "body"), "opt_seat_tuck_strap": (strap, "leather", "body"),
                 "opt_seat_tuck_pan": (pan, "black_satin", "body")}))

    # Sprung pan saddle: a tan leather solo pan over the rider's place, its nose on a pivot and its
    # back on two coil springs down to the rails. The pillion half of the frame is left bare.
    sx = np.linspace(-0.47, -0.13, 30)
    st_ = (sx - sx[0]) / (sx[-1] - sx[0])
    sbot = np.interp(sx, xs, bot) + 0.05 + 0.03 * (1 - st_)
    stop = sbot + 0.045 + 0.02 * np.sin(np.clip(1 - st_, 0, 1) * math.pi * 0.5) - 0.03 * np.clip(st_ - 0.8, 0, 1) / 0.2
    shw = 0.05 + 0.12 * np.sin(np.clip(1 - st_, 0, 1) * math.pi * 0.55) ** 0.8
    pad = st.sweep(np.stack([sx, (stop + sbot) / 2, 0 * sx], 1), shw, (stop - sbot) / 2, 3.0, 64)
    pan = st.sweep(np.stack([sx, sbot + 0.003, 0 * sx], 1), shw * 0.98, 0.006, 4.0, 20)
    springs = st.Mesh()
    for s in (-1, 1):
        a = (-0.42, np.interp(-0.42, xs, bot) + 0.005, s * 0.09)
        b = (-0.42, np.interp(-0.42, sx, sbot), s * 0.09)
        springs.extend(coil(a, b, 0.018, 0.0035, 6.5))
    nose = np.array([-0.14, np.interp(-0.14, xs, bot), 0.0])
    pivot = st.tube(np.array([nose + [0, 0, -0.03], nose + [0, 0, 0.03]]), 0.008, 12)
    pivot.extend(st.tube(np.array([nose, (-0.15, np.interp(-0.15, sx, sbot), 0.0)]), 0.006, 10))
    out.append(("saddle", "sprung_pan_seat", cat["saddle"]["sprung_pan_seat"]["name"], ["saddle", "sprung_pan_seat"],
                [seat_unit(fit, ["opt_seat_sprung_pad", "opt_seat_sprung_pan", "opt_seat_sprung_springs", "opt_seat_sprung_pivot"],
                           sx, stop, sbot, shw)],
                {"opt_seat_sprung_pad": (pad, "leather_tan", "body"), "opt_seat_sprung_pan": (pan, "black_satin", "body"),
                 "opt_seat_sprung_springs": (springs, "chrome", "body"), "opt_seat_sprung_pivot": (pivot, "black_satin", "body")}))
    return out


# --- Front mudguards ------------------------------------------------------------------------------
# On the fork lowers (group front_lower: vertices relative to the front axle). Stock is a short
# sports blade from 30 to 98 degrees, radius 0.325; the classic guards are longer and deeper.

def guard_proxies(fit, path, w, depth, group_origin):
    p = np.asarray(path) + group_origin
    out = []
    for i in range(0, len(p) - 3, 3):
        c = (p[i] + p[i + 3]) / 2
        out.append(fit.box(c, fit.frame_along(p[i + 3] - p[i]), (np.linalg.norm(p[i + 3] - p[i]) / 2, depth / 2 + 0.004, w)))
    return out


def front_guard_unit(fit, meshes, path, w, depth):
    return {"id": "front_mudguard", "group": "front_lower", "meshes": meshes, "slot": "front_mudguard",
            "mount": {"to": "fork_lowers.guard_mounts", "type": "bracket"}, "sockets": {},
            "proxies": guard_proxies(fit, path, w, depth, FA)}


def stays(points_pairs, r=0.005):
    m = st.Mesh()
    for a, b in points_pairs:
        m.extend(st.tube(np.array([a, b]), r, 10))
    return m


def front_guards(fit, cat):
    out = []
    o = np.zeros(3)
    # Chrome classic: long and deeply valanced, with a front and a rear stay each side to the lowers.
    r, w, d = 0.335, 0.072, 0.05
    path = arc(o, r, 100, -25, 40)
    guard = section_sweep(path, u_profile(w, d, 0.0025), up_hint=(math.cos(math.radians(100)), math.sin(math.radians(100)), 0))
    st_pairs = []
    for s in (-1, 1):
        for ang in (-10, 80):
            p = arc(o, r - d * 0.8, ang, ang, 1, s * (w - 0.004))[0]
            st_pairs.append((p, (0.0, 0.0, s * 0.11)))
    out.append(("front_mudguard", "chrome_classic", cat["front_mudguard"]["chrome_classic"]["name"],
                ["front_mudguard", "chrome_classic"],
                [front_guard_unit(fit, ["opt_fguard_chrome", "opt_fguard_chrome_stays"], path, w, d)],
                {"opt_fguard_chrome": (guard, "chrome", "front_lower"),
                 "opt_fguard_chrome_stays": (stays(st_pairs), "chrome", "front_lower")}))

    # Valanced painted guard: deep, red to match, a raised centre rib.
    r, w, d = 0.335, 0.075, 0.055
    path = arc(o, r, 97, -15, 40)
    guard = section_sweep(path, u_profile(w, d, 0.0025, crown=7.0), up_hint=(math.cos(math.radians(97)), math.sin(math.radians(97)), 0))
    rib = st.tube(arc(o, r + 0.003, 95, -13, 40), 0.006, 10)
    st_pairs = [(arc(o, r - d * 0.8, a, a, 1, s * (w - 0.004))[0], (0.0, 0.0, s * 0.11)) for s in (-1, 1) for a in (0, 80)]
    out.append(("front_mudguard", "valanced_painted_guard", cat["front_mudguard"]["valanced_painted_guard"]["name"],
                ["front_mudguard", "valanced_painted_guard"],
                [front_guard_unit(fit, ["opt_fguard_valanced", "opt_fguard_valanced_rib", "opt_fguard_valanced_stays"], path, w, d)],
                {"opt_fguard_valanced": (guard, "paint_red", "front_lower"),
                 "opt_fguard_valanced_rib": (rib, "paint_red", "front_lower"),
                 "opt_fguard_valanced_stays": (stays(st_pairs), "black_satin", "front_lower")}))

    # Ribbed alloy blade: shallow, polished, a raised rib; the catalogue's a0..a1 (radians) is its arc.
    look = cat["front_mudguard"]["ribbed_alloy_guard"].get("look", {})
    a0 = min(math.degrees(look.get("a1", 1.05)) + 38.0, 98.0)
    a1 = 5.0
    r, w, d = 0.33, 0.062, 0.018
    path = arc(o, r, a0, a1, 34)
    guard = section_sweep(path, u_profile(w, d, 0.002, crown=4.0), up_hint=(math.cos(math.radians(a0)), math.sin(math.radians(a0)), 0))
    rib = st.tube(arc(o, r + 0.002, a0 - 2, a1 + 2, 34), 0.005, 10)
    st_pairs = [(arc(o, r - 0.01, a, a, 1, s * (w - 0.003))[0], (0.0, 0.0, s * 0.11)) for s in (-1, 1) for a in (20, 80)]
    out.append(("front_mudguard", "ribbed_alloy_guard", cat["front_mudguard"]["ribbed_alloy_guard"]["name"],
                ["front_mudguard", "ribbed_alloy_guard"],
                [front_guard_unit(fit, ["opt_fguard_alloy", "opt_fguard_alloy_rib", "opt_fguard_alloy_stays"], path, w, d)],
                {"opt_fguard_alloy": (guard, "alu_polished", "front_lower"),
                 "opt_fguard_alloy_rib": (rib, "alu_polished", "front_lower"),
                 "opt_fguard_alloy_stays": (stays(st_pairs, 0.004), "steel_brushed", "front_lower")}))
    return out


# --- Rear guards (the tail slot) ------------------------------------------------------------------
# Body mounted (the stock tail is): a full guard clears the tyre at full bump, so it rides high over
# it at rest, as old twins' did. The rear indicators hang from the tail's "indicator_mount".

REAR_R = 0.312


def bumped_axle(travel=0.12):
    """Where the rear axle is at full bump: the swingarm turned about its pivot until the axle has
    risen by the travel (what the clearance check's rear_bump case does)."""
    p, a = st.SWING_PIVOT, RA
    d = a - p
    L = np.linalg.norm(d[:2])
    ang = math.atan2(d[1], d[0])
    target = math.asin(np.clip((d[1] + travel) / L, -1, 1))
    ang2 = math.pi - target if abs(ang) > math.pi / 2 else target
    return p + [L * math.cos(ang2), L * math.sin(ang2), 0.0]


def lamp_round(c, r=0.045):
    body = st.lathe([(0.0, 0.0), (r, 0.0), (r, 0.03), (r * 0.9, 0.04), (0.0, 0.042)], seg=28, axis=(-1, 0.15, 0), centre=c)
    lens = st.lathe([(0.0, 0.041), (r * 0.85, 0.041), (r * 0.6, 0.05), (0.0, 0.053)], seg=28, axis=(-1, 0.15, 0), centre=c)
    return body, lens


def lamp_tombstone(c):
    body = st.box(c, (0.06, 0.07, 0.11), 6.0)
    lens = st.box(np.asarray(c) + [-0.033, 0.0, 0.0], (0.012, 0.055, 0.095), 6.0)
    return body, lens


def rear_guards(fit, cat):
    out = []
    o = RA
    specs = [("classic_mudguard", "chrome", "round", 0.072, 0.045, "chrome"),
             ("classic_tombstone_guard", "chrome", "tombstone", 0.075, 0.05, "chrome"),
             ("painted_valance_guard", "paint_red_stripe", "round", 0.08, 0.06, "black_satin")]
    # The guard's arc is round the axle at full bump, just clear of the tyre there, so at rest it
    # stands off the tyre most over the top and least at the back, as a frame-mounted guard does.
    o = bumped_axle()
    for pid, fin, lamp, w, d, strut_fin in specs:
        r = REAR_R + 0.04
        a0, a1 = 78.0, 172.0
        path = arc(o, r, a0, a1, 44)
        up = (math.cos(math.radians(a0)), math.sin(math.radians(a0)), 0)
        guard = section_sweep(path, u_profile(w, d, 0.0025, crown=6.5), up_hint=up)
        if fin == "paint_red_stripe":
            # The pinstripe: a band of the mask either side of the crown.
            guard.mask_by(lambda p: (np.abs(np.abs(p[:, 2]) - 0.035) < 0.003).astype(float) *
                          (np.linalg.norm(p[:, :2] - o[:2], axis=1) > r - 0.004).astype(float))
        # The lamp sits on top near the end; the plate hangs square below it on the guard's back.
        lamp_at = arc(o, r + 0.035, 140, 140, 1)[0]
        body, lens = lamp_round(lamp_at) if lamp == "round" else lamp_tombstone(lamp_at + [0.0, 0.03, 0.0])
        pa = arc(o, r + 0.012, 160, 160, 1)[0]
        plate = st.sweep(np.array([pa + [0.012, 0.055, 0], pa + [-0.012, -0.065, 0]]), 0.1, 0.002, 8.0, 16, up_hint=(1, 0.2, 0))
        # Struts a side from the guard's edges to the end of the frame's rails (x -0.76), one straight
        # up and one raked forward from further back.
        strut_pts = []
        for s in (-1, 1):
            rail = np.array([-0.76, 0.47, s * 0.10])
            for ang in (96.0, 122.0):
                g = arc(o, r - d * 0.6, ang, ang, 1, s * (w + 0.005))[0]
                strut_pts.append((g, rail))
        struts = stays(strut_pts, 0.007)
        n = "opt_tail_" + pid
        sock_at = arc(o, r + 0.01, 152, 152, 1)[0]
        prox = guard_proxies(fit, path, w, d, np.zeros(3))
        prox.append(fit.obb(np.vstack([p for p in body.p])))
        unit = {"id": "tail", "group": "body", "meshes": [n, n + "_lamp", n + "_lens", n + "_plate", n + "_struts"], "slot": "tail",
                "mount": {"to": "frame.seat_rails", "type": "seat_rails", "rail_mm": 180.0, "from_end": 0.0},
                "sockets": {"indicator_mount": fit.sock("bracket", sock_at, (1, 0, 0))}, "proxies": prox}
        out.append(("tail", pid, cat["tail"][pid]["name"], ["tail", pid], [unit],
                    {n: (guard, fin, "body"), n + "_lamp": (body, "chrome" if lamp == "round" else fin, "body"),
                     n + "_lens": (lens, "lens_red", "body"), n + "_plate": (plate, "plate", "body"),
                     n + "_struts": (struts, strut_fin, "body")}))
    return out


def pack_options(fit, cat):
    return seats(fit, cat) + front_guards(fit, cat) + rear_guards(fit, cat)
