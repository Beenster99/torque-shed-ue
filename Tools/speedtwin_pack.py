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
    "screen_smoked": F((0.03, 0.035, 0.04), 0.0, 0.03, clearcoat_roughness=0.02),
    "wood_crate": F((0.33, 0.21, 0.10), 0.0, 0.75, detail="cast", uv_scale=3.0, normal_strength=0.4, rough_var=0.4),
    "black_header": F((0.03, 0.03, 0.032), 0.6, 0.45, detail="cast", uv_scale=6.0, normal_strength=0.3, rough_var=0.2),
    "piping_cream": F((0.76, 0.70, 0.56), 0.0, 0.45, detail="leather", uv_scale=7.0, normal_strength=0.5, rough_var=0.2),
    "dial_cream": F((0.80, 0.77, 0.68), 0.0, 0.35),
    "canvas_olive": F((0.20, 0.19, 0.11), 0.0, 0.85, detail="cast", uv_scale=12.0, normal_strength=0.5, rough_var=0.2),
    "tyre_white": F((0.80, 0.80, 0.78), 0.0, 0.7, detail="tyre", uv_scale=1.0, normal_strength=0.3, rough_var=0.3),
    "tyre_red": F((0.50, 0.05, 0.035), 0.0, 0.7, detail="tyre", uv_scale=1.0, normal_strength=0.3, rough_var=0.3),
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


# --- Indicators -----------------------------------------------------------------------------------
# A pair at the front (on the headlight's bracket, steer group) and a pair at the back (on the
# tail's), modelled where the stock ones are: bases at the headlight's sides and beside the plate.

HL_C = FA + st.FORK_DIR * 0.56 + np.array([0.075, 0.0, 0.0])
FRONT_IND = [HL_C + np.array([-0.04, -0.01, s * 0.11]) for s in (-1, 1)]
REAR_IND = [np.array([-0.86, 0.44, s * 0.06]) for s in (-1, 1)]


def indicator(base, s, facing, kind):
    """One indicator from its base (where its stalk meets the bracket) out to the side s: (body, lens)."""
    body, lens = st.Mesh(), st.Mesh()
    out = np.array([0.0, 0.0, s])
    if kind == "stalk":
        # A plain amber lamp on a long black rubber stalk.
        tip = base + out * 0.09
        body.extend(st.tube(np.array([base, tip]), 0.0065, 10))
        lens.extend(st.box(tip + out * 0.012 + [facing * 0.006, 0, 0], (0.04, 0.022, 0.024), 4.0))
    else:
        # A chrome bullet on a short chrome stalk, its amber lens at the tip (facing along x).
        big = kind == "bullet_large"
        L, R = (0.06, 0.02) if big else (0.048, 0.015)
        c = base + out * (0.045 if big else 0.04)
        body.extend(st.tube(np.array([base, c]), 0.005, 10))
        ax = np.array([facing, 0.0, 0.0])
        body.extend(st.lathe([(0.0, -L * 0.55), (R * 0.55, -L * 0.5), (R, -L * 0.15), (R, L * 0.3), (R * 1.05, L * 0.34), (0.0, L * 0.34)],
                             seg=20, axis=ax, centre=c))
        lens.extend(st.lathe([(R * 0.92, L * 0.34), (R * 0.85, L * 0.45), (R * 0.5, L * 0.55), (0.0, L * 0.58)],
                             seg=20, axis=ax, centre=c))
    return body, lens


def indicators(fit, cat):
    out = []
    for pid, kind, body_fin in (("stalk_amber", "stalk", "rubber"), ("bullet_chrome", "bullet", "chrome"),
                                ("classic_bullet_amber", "bullet_large", "chrome")):
        units, meshes = [], {}
        for end, bases, facing, grp, mount in (("front", FRONT_IND, 1.0, "steer", "headlight.indicator_mount"),
                                              ("rear", REAR_IND, -1.0, "body", "tail.indicator_mount")):
            body, lens = st.Mesh(), st.Mesh()
            prox = []
            for b, s in zip(bases, (-1, 1)):
                bo, le = indicator(b, s, facing, kind)
                body.extend(bo); lens.extend(le)
                prox.append(fit.capsule(b + [0, 0, s * 0.03], b + [0, 0, s * 0.09], 0.02))
            n = "opt_ind_%s_%s" % (pid, end)
            units.append({"id": "indicators_" + end, "group": grp, "meshes": [n, n + "_lens"], "slot": "indicators",
                          "mount": {"to": mount, "type": "bracket"}, "sockets": {}, "proxies": prox})
            meshes[n] = (body, body_fin, grp)
            meshes[n + "_lens"] = (lens, "lens_amber", grp)
        out.append(("indicators", pid, cat["indicators"][pid]["name"], ["indicators", pid], units, meshes))
    return out


# --- Mirrors --------------------------------------------------------------------------------------
# Modelled at the stock mirrors' base on the bar ends (so a bar's "moves" put them where it says).

def mirror_set(base, s, head_r, stalk_len):
    stalk = st.polyline([base, base + [0.0, 0.03, s * 0.02], base + [0.015, stalk_len, s * 0.05]], 6)
    mc = base + np.array([0.03, stalk_len + 0.06 * head_r / 0.045, s * 0.06])
    ax = (1, 0.1, 0)
    shell = st.lathe([(0.0, -0.01), (head_r, -0.01), (head_r * 1.06, 0.0), (head_r, 0.007), (0.0, 0.007)], seg=36, axis=ax, centre=mc)
    glass = st.lathe([(0.0, -0.0105), (head_r * 0.92, -0.0105)], seg=36, axis=ax, centre=mc)
    return shell, glass, st.tube(stalk, 0.0055, 10), mc


def mirrors(fit, cat):
    out = []
    for pid, head_r, stalk_len in (("round_mirrors", 0.05, 0.11), ("chrome_round_large", 0.05 * 1.4, 0.15)):
        units, meshes = [], {}
        for sd, s in (("l", -1), ("r", 1)):
            base = fit.BAR_CLAMP + [0.0, 0.0, s * 0.385]
            shell, glass, stalk, mc = mirror_set(base, s, head_r, stalk_len)
            n = "opt_mirror_%s_%s" % (pid, sd)
            units.append({"id": "mirror_" + sd, "group": "steer", "meshes": [n, n + "_glass", n + "_stalk"], "slot": "mirrors",
                          "mount": {"to": "bars.bar_" + sd, "type": "bar", "bar_mm": fit.BAR_MM, "from_end": 0.0},
                          "sockets": {}, "proxies": [fit.capsule(mc, mc, head_r + 0.004)] +
                          fit.chain([base, base + [0.015, stalk_len, s * 0.05]], 0.006)})
            meshes.update({n: (shell, "chrome", "steer"), n + "_glass": (glass, "mirror_glass", "steer"),
                           n + "_stalk": (stalk, "chrome", "steer")})
        out.append(("mirrors", pid, cat["mirrors"][pid]["name"], ["mirrors", pid], units, meshes))
    return out


# --- Levers and grips (no catalogue entries: UE-side choices) ------------------------------------
# Levers keep the stock pivots (the pawn swings them about speedtwin.json's lever pivots).

def lever_mesh(s, kind):
    pts = st.lever_points(s)
    piv = np.asarray(pts[0])
    if kind == "ball":
        # Long chrome blade, the classic ball on its end.
        blade = st.polyline([pts[0], pts[1], np.asarray(pts[2]) + [0.0, -0.004, s * 0.02]], 6)
        m = st.sweep(blade, 0.0055, 0.009, 3.0, 10)
        m.extend(st.lathe([(0.0, -0.012), (0.012, -0.006), (0.012, 0.006), (0.0, 0.012)], seg=16, axis=(0, 0, 1),
                          centre=blade[-1]))
    else:
        # A short polished dogleg: kinked forward, two fingers long.
        tip = piv + [0.06, -0.008, s * 0.09]
        blade = st.polyline([pts[0], piv + [0.05, 0.0, s * 0.035], tip], 6)
        m = st.sweep(blade, 0.006, 0.011, 4.0, 10)
    m.extend(st.lathe([(0.0, -0.012), (0.011, -0.012), (0.011, 0.012), (0.0, 0.012)], seg=16, axis=(0, 1, 0), centre=piv))
    return m


def levers(fit):
    out = []
    for pid, name, kind, fin in (("chrome_ball_levers", "Chrome ball-end levers", "ball", "chrome"),
                                 ("alloy_dogleg_levers", "Polished alloy dogleg levers", "dogleg", "alu_polished")):
        units, meshes = [], {}
        for unit_id, s, sd in (("lever_clutch", -1, "l"), ("lever_brake", 1, "r")):
            n = "opt_lever_%s_%s" % (pid, sd)
            m = lever_mesh(s, kind)
            units.append({"id": unit_id, "group": "steer", "meshes": [n], "slot": "levers",
                          "mount": {"to": "bars.bar_" + sd, "type": "bar", "bar_mm": fit.BAR_MM, "from_end": 0.38 - 0.23},
                          "sockets": {}, "proxies": fit.chain(st.polyline(st.lever_points(s), 6), 0.008, 2)})
            meshes[n] = (m, fin, "steer")
        out.append(("levers", pid, name, None, units, meshes))
    return out


def grips(fit):
    out = []
    for pid, name, kind, fin in (("barrel_grips", "Barrel grips", "barrel", "grip"),
                                 ("leather_grips", "Leather-wrapped grips", "leather", "leather_tan")):
        units, meshes = [], {}
        for sd, s in (("l", -1), ("r", 1)):
            g0 = fit.BAR_CLAMP + [-0.005, 0.012, s * 0.255]; g1 = fit.BAR_CLAMP + [0.0, 0.002, s * 0.375]
            t = np.linspace(0, 1, 40)
            if kind == "barrel":
                # Fat in the middle, rings round it, a flange at the inner end.
                r = 0.0165 + 0.003 * np.sin(t * math.pi) + 0.0012 * (np.sin(t * math.pi * 18) > 0.3)
                r[:3] = 0.021
            else:
                # Slim, wrapped in a spiral of tan leather (the ridges), stitched at the ends.
                r = 0.0155 + 0.0008 * np.sin(t * math.pi * 26)
            path = g0[None] + (g1 - g0)[None] * t[:, None]
            m = st.sweep(path, r, r, 2.0, 20)
            n = "opt_grip_%s_%s" % (pid, sd)
            units.append({"id": "grip_" + sd, "group": "steer", "meshes": [n], "slot": "grips",
                          "mount": {"to": "bars.bar_" + sd, "type": "bar", "bar_mm": fit.BAR_MM, "from_end": round(0.38 - 0.315, 5)},
                          "sockets": {}, "proxies": fit.chain([g0, g1], 0.017)})
            meshes[n] = (m, fin, "steer")
        out.append(("grips", pid, name, None, units, meshes))
    return out


# --- Screens (on the headlight, steer group) -------------------------------------------------------

def sheet(fn, nu=18, nv=12):
    """A one-sided surface from fn(u in -1..1, v in 0..1) -> point (flat pieces are two sided)."""
    u = np.linspace(-1, 1, nu); v = np.linspace(0, 1, nv)
    pts = np.array([fn(a, b) for b in v for a in u])
    uv = np.array([(a, b) for b in v for a in u]) * 0.3
    return st.Mesh().add(pts, st.grid_tris(nv, nu, wrap_cols=False), uv)


def screens(fit, cat):
    out = []
    c = HL_C
    # Flyscreen: a small smoked screen rising back from over the lamp, curved round, on two stays.
    def fly(u, v, w0=0.10, w1=0.13, h=0.2, back=0.075):
        w = w0 + (w1 - w0) * v
        return c + np.array([0.045 - back * v - 0.03 * u * u, 0.085 + h * v, u * w])
    screen = sheet(fly)
    stays_ = stays([(c + [0.0, 0.0, sg * 0.092], fly(sg * 0.8, 0.12)) for sg in (-1, 1)], 0.005)
    pts = np.vstack(screen.p)
    out.append(("screen", "flyscreen", cat["fairing"]["flyscreen"]["name"], ["fairing", "flyscreen"],
                [{"id": "screen", "group": "steer", "meshes": ["opt_screen_fly", "opt_screen_fly_stays"], "slot": "screen",
                  "mount": {"to": "headlight.screen_mount", "type": "bracket"}, "sockets": {},
                  "proxies": [fit.obb(pts)]}],
                {"opt_screen_fly": (screen, "screen_smoked", "steer"), "opt_screen_fly_stays": (stays_, "black_satin", "steer")}))

    # Bikini fairing: a black cowl round the lamp's top and sides (the indicators stay clear below),
    # a short screen on its top edge, all in front of the clock.
    def cowl(u, v):
        a = math.radians(5 + (u + 1) / 2 * 170)   # from the right side, over the top, to the left
        inner = np.array([0.03, 0.098 * math.sin(a), 0.098 * math.cos(a)])
        outer = np.array([-0.03, 0.135 * math.sin(a) + 0.01, 0.175 * math.cos(a)])
        p = inner + (outer - inner) * v
        p[0] += 0.025 * math.sin(math.pi * v)   # a little bulge forward
        return c + p
    shell = sheet(cowl, 40, 8)
    def bscreen(u, v):
        top = cowl(u * 0.5, 1.0)
        return top + np.array([0.01 * v - 0.015 * u * u, 0.11 * v, 0.0])
    scr = sheet(bscreen, 16, 8)
    # Proxies: a box round each sixth of the cowl, and the screen's.
    prox = []
    for k in range(6):
        u0, u1 = -1 + k / 3.0, -1 + (k + 1) / 3.0
        prox.append(fit.obb(np.array([cowl(u, v) for u in np.linspace(u0, u1, 5) for v in np.linspace(0, 1, 5)])))
    prox.append(fit.obb(np.vstack(scr.p)))
    out.append(("screen", "bikini_fairing", cat["fairing"]["bikini_fairing"]["name"], ["fairing", "bikini_fairing"],
                [{"id": "screen", "group": "steer", "meshes": ["opt_screen_bikini", "opt_screen_bikini_screen"], "slot": "screen",
                  "mount": {"to": "headlight.screen_mount", "type": "bracket"}, "sockets": {}, "proxies": prox}],
                {"opt_screen_bikini": (shell, "black_gloss", "steer"), "opt_screen_bikini_screen": (scr, "screen_smoked", "steer")}))
    return out


# --- Luggage rack (over the tail) ------------------------------------------------------------------

def rack_mesh():
    """A chrome tube rack behind the seat: a flat loop with cross bars, its sides sweeping down and
    forward outside the seat to the rails, and two legs down to the frame's rear loop."""
    y = 0.585
    loop = st.polyline([(-0.70, y, -0.11), (-0.97, y, -0.11), (-0.99, y, -0.07), (-0.99, y, 0.07), (-0.97, y, 0.11),
                        (-0.70, y, 0.11)], 6)
    m = st.tube(loop, 0.0085, 12)
    for x in (-0.76, -0.83, -0.90):
        m.extend(st.tube(np.array([(x, y, -0.11), (x, y, 0.11)]), 0.006, 10))
    prox_paths = [loop]
    for s in (-1, 1):
        side = st.polyline([(-0.70, y, s * 0.11), (-0.64, y - 0.02, s * 0.155), (-0.58, 0.50, s * 0.16), (-0.56, 0.47, s * 0.11)], 6)
        leg = np.array([(-0.92, y, s * 0.11), (-0.80, 0.47, s * 0.10)])
        m.extend(st.tube(side, 0.0085, 12)).extend(st.tube(leg, 0.007, 10))
        prox_paths += [side, leg]
    return m, prox_paths


def racks(fit, cat):
    out = []
    m, paths = rack_mesh()
    prox = [p for path in paths for p in fit.chain(path, 0.009, 3)]
    out.append(("rack", "chrome_rack", "Chrome luggage rack", None,
                [{"id": "rack", "group": "body", "meshes": ["opt_rack_chrome"], "slot": "rack",
                  "mount": {"to": "frame.seat_rails", "type": "seat_rails", "rail_mm": 180.0, "from_end": 0.0},
                  "sockets": {}, "proxies": prox}],
                {"opt_rack_chrome": (m, "chrome", "body")}))
    # The crate: slatted boards on the same rack.
    crate = st.Mesh()
    cx, cy, w, d, h = -0.845, 0.585 + 0.0085, 0.25, 0.30, 0.2
    for k in range(4):   # side slats, a gap between each
        yy = cy + 0.012 + k * (h / 4)
        for s in (-1, 1):
            crate.extend(st.box((cx, yy + h / 8 - 0.006, s * (w / 2 - 0.006)), (d, h / 4 - 0.012, 0.012), 8.0))
        for s in (-1, 1):
            crate.extend(st.box((cx + s * (d / 2 - 0.006), yy + h / 8 - 0.006, 0.0), (0.012, h / 4 - 0.012, w), 8.0))
    crate.extend(st.box((cx, cy + 0.006, 0.0), (d, 0.012, w), 8.0))   # its floor
    for sx in (-1, 1):
        for sz in (-1, 1):   # corner posts
            crate.extend(st.box((cx + sx * (d / 2 - 0.012), cy + h / 2, sz * (w / 2 - 0.012)), (0.024, h, 0.024), 8.0))
    out.append(("rack", "wooden_crate", cat["top_box"]["wooden_crate"]["name"], ["top_box", "wooden_crate"],
                [{"id": "rack", "group": "body", "meshes": ["opt_rack_crate_rack", "opt_rack_crate"], "slot": "rack",
                  "mount": {"to": "frame.seat_rails", "type": "seat_rails", "rail_mm": 180.0, "from_end": 0.0},
                  "sockets": {}, "proxies": prox + [fit.obb(np.vstack(crate.p))]}],
                {"opt_rack_crate_rack": (m, "black_satin", "body"), "opt_rack_crate": (crate, "wood_crate", "body")}))
    return out


# --- Headlights (fork tube clamps, steer group) ----------------------------------------------------
# Each keeps the stock headlight's brackets for the indicators and a screen.

def headlight_unit(fit, meshes, prox):
    return {"id": "headlight", "group": "steer", "meshes": meshes, "slot": "headlight",
            "mount": {"to": "yokes.fork_tubes", "type": "fork_tube", "tube_mm": 58.0, "from_end": float(0.69 - 0.56)},
            "sockets": {"indicator_mount": fit.sock("bracket", HL_C, (1, 0, 0)), "screen_mount": fit.sock("bracket", HL_C, (1, 0, 0))},
            "proxies": prox}


def lamp_insides(c, r):
    """Reflector bowl and a domed clear lens for a round lamp of radius r facing +x at c."""
    refl = st.lathe([(r * 0.93, 0.03), (r * 0.75, 0.012), (r * 0.45, -0.005), (0.0, -0.01)], seg=48, axis=(1, 0, 0), centre=c)
    bulb = st.lathe([(0.0, 0.0), (0.01, 0.005), (0.009, 0.02), (0.0, 0.026)], seg=16, axis=(1, 0, 0), centre=c)
    lens = st.lathe([(r * 0.95, 0.032), (r * 0.7, 0.042), (r * 0.35, 0.048), (0.0, 0.05)], seg=48, axis=(1, 0, 0), centre=c)
    return refl.extend(bulb), lens


def headlights(fit, cat):
    out = []
    c = HL_C
    r = 0.089   # 7 inches
    # 7" round: a deep black bucket, a thin chrome rim (the mask), a domed clear lens.
    bucket = st.lathe([(0.0, -0.075), (r * 0.6, -0.07), (r * 0.92, -0.035), (r, 0.0), (r * 1.01, 0.025), (r * 1.03, 0.03),
                       (r * 1.02, 0.038), (r * 0.95, 0.036)], seg=64, axis=(1, 0, 0), centre=c)
    bucket.mask_by(lambda p: ((p[:, 0] - c[0]) > 0.02).astype(float))
    refl, lens = lamp_insides(c, r)
    # A ring round the rim and a short core behind it (a box's corners would swing into the frame).
    prox = fit.torus(c + [-0.01, 0.0, 0.0], (1, 0, 0), r - 0.03, 0.033, 12) + [fit.capsule(c + [-0.035, 0, 0], c, 0.05)]
    out.append(("headlight", "round_7", cat["headlight"]["round_7"]["name"], ["headlight", "round_7"],
                [headlight_unit(fit, ["opt_hl_round7_bucket", "opt_hl_round7_refl", "opt_hl_round7_lens"], prox)],
                {"opt_hl_round7_bucket": (bucket, "headlight", "steer"), "opt_hl_round7_refl": (refl, "headlight_inner", "steer"),
                 "opt_hl_round7_lens": (lens, "lens_clear", "steer")}))
    # 7" with a fat chrome ring and chrome ears to the fork tubes.
    bucket = st.lathe([(0.0, -0.07), (r * 0.6, -0.066), (r * 0.92, -0.033), (r, 0.0), (r, 0.022)], seg=64, axis=(1, 0, 0), centre=c)
    ring = st.lathe([(r * 0.93, 0.02), (r * 1.06, 0.022), (r * 1.08, 0.032), (r * 1.04, 0.042), (r * 0.94, 0.04)], seg=64,
                    axis=(1, 0, 0), centre=c)
    ears = st.Mesh()
    for sg in (-1, 1):
        ears.extend(st.sweep(np.array([c + [-0.01, 0.0, sg * (r - 0.005)], c + [-0.03, 0.0, sg * 0.095]]), 0.004, 0.02, 4.0, 12,
                             up_hint=(0, 1, 0)))
        ears.extend(st.lathe([(0.0, -0.008), (0.016, -0.008), (0.016, 0.008), (0.0, 0.008)], seg=16, axis=(0, 0, 1),
                             centre=c + [-0.01, 0.0, sg * (r + 0.004)]))
    refl, lens = lamp_insides(c, r)
    out.append(("headlight", "chrome_ring_7", cat["headlight"]["chrome_ring_7"]["name"], ["headlight", "chrome_ring_7"],
                [headlight_unit(fit, ["opt_hl_ring7_bucket", "opt_hl_ring7_ring", "opt_hl_ring7_ears", "opt_hl_ring7_refl",
                                      "opt_hl_ring7_lens"], prox)],
                {"opt_hl_ring7_bucket": (bucket, "black_gloss", "steer"), "opt_hl_ring7_ring": (ring, "chrome", "steer"),
                 "opt_hl_ring7_ears": (ears, "chrome", "steer"), "opt_hl_ring7_refl": (refl, "headlight_inner", "steer"),
                 "opt_hl_ring7_lens": (lens, "lens_clear", "steer")}))
    # Nacelle: a cream cowl from the lamp's rim back over the fork tops, as the old Triumphs had,
    # with a chrome rim round the lamp.
    xs = np.linspace(c[0] + 0.025, c[0] - NAC_BACK, 22)
    t = (c[0] + 0.025 - xs) / (0.025 + NAC_BACK)
    hw = r + 0.006 + (0.11 - r) * np.sin(np.clip(t, 0, 1) * math.pi * 0.5)
    hh = r + 0.006 + 0.02 * np.sin(np.clip(t, 0, 1) * math.pi * 0.5)
    cy = c[1] + NAC_RISE * t ** 1.5
    nac = st.sweep(np.stack([xs, cy, 0 * xs], 1), hw, hh, 2.6, 64, caps=True)
    rim = st.lathe([(r * 0.93, 0.02), (r * 1.04, 0.022), (r * 1.06, 0.03), (r * 0.96, 0.036)], seg=64, axis=(1, 0, 0), centre=c)
    refl, lens = lamp_insides(c, r)
    nprox = fit.slices(xs[::-1], (cy + hh)[::-1], (cy - hh)[::-1], hw[::-1], 5)
    out.append(("headlight", "nacelle_lamp", cat["headlight"]["nacelle_lamp"]["name"], ["headlight", "nacelle_lamp"],
                [headlight_unit(fit, ["opt_hl_nacelle", "opt_hl_nacelle_rim", "opt_hl_nacelle_refl", "opt_hl_nacelle_lens"], nprox)],
                {"opt_hl_nacelle": (nac, "paint_cream", "steer"), "opt_hl_nacelle_rim": (rim, "chrome", "steer"),
                 "opt_hl_nacelle_refl": (refl, "headlight_inner", "steer"), "opt_hl_nacelle_lens": (lens, "lens_clear", "steer")}))
    return out


# --- Silencers --------------------------------------------------------------------------------------

NAC_BACK, NAC_RISE = 0.05, 0.015

S_START = np.array([-0.25, -0.065, 0.158])   # the stock headers' slip joint


def exhausts(fit, cat):
    out = []
    # Fishtails: long chrome tubes, each ending in a flattened, flared fishtail.
    end = np.array([-0.86, 0.12, 0.172])
    body = np.linspace(S_START, end, 18)
    radii = np.interp(np.linspace(0, 1, 18), [0.0, 0.2, 0.5, 1.0], [0.024, 0.027, 0.036, 0.04])
    tube_ = st.sweep(body, radii, radii, 2.0, 32, caps=False)
    d = (end - S_START) / np.linalg.norm(end - S_START)
    tail_path = np.array([end + d * k for k in np.linspace(0, 0.17, 10)])
    k = np.linspace(0, 1, 10)
    fish = st.sweep(tail_path, 0.04 - 0.026 * k, 0.04 + 0.03 * k, 3.0, 32, caps=True, up_hint=(0, 0, 1))
    m = st.both_sides(tube_.extend(fish))
    u = {"id": "silencers", "group": "body", "meshes": ["opt_exhaust_fishtail"], "slot": "exhaust"}
    u.update(fit.silencer_unit(np.vstack([body, tail_path[1:]]), np.concatenate([radii, 0.04 + 0.03 * k[1:]]), 48.0))
    out.append(("exhaust", "fishtail_pipes", cat["exhaust"]["fishtail_pipes"]["name"], ["exhaust", "fishtail_pipes"], [u],
                {"opt_exhaust_fishtail": (m, "chrome", "body")}))
    # Cigars, on black headers: fat in the middle, tapering to both ends.
    end = np.array([-0.82, 0.13, 0.172])
    body = np.linspace(S_START, end, 24)
    t = np.linspace(0, 1, 24)
    radii = np.maximum(0.024, 0.047 * np.sin(np.clip((t - 0.14) / 0.86, 0, 1) * math.pi) ** 0.55)
    radii[-1] = 0.012
    cig = st.both_sides(st.sweep(body, radii, radii, 2.0, 32, caps=True))
    u = {"id": "silencers", "group": "body", "meshes": ["opt_exhaust_cigar"], "slot": "exhaust"}
    u.update(fit.silencer_unit(body, radii, 48.0))
    hu, hmesh = fit.headers_unit(0.0)
    hu["meshes"] = ["opt_headers_black"]
    out.append(("exhaust", "cigar_mufflers", cat["exhaust"]["cigar_mufflers"]["name"], ["exhaust", "cigar_mufflers"], [hu, u],
                {"opt_exhaust_cigar": (cig, "chrome", "body"), "opt_headers_black": (hmesh.masked(0.0), "black_header", "body")}))
    return out


# --- Engine bars (add-ons on the frame's downtubes) -------------------------------------------------

def guards(fit, cat):
    out = []
    specs = {
        # A hoop a side in front of the cylinders, from the downtube's bend to its foot.
        "engine_bars": [(0.398, 0.31, 0.075), (0.45, 0.37, 0.18), (0.485, 0.26, 0.255), (0.48, 0.08, 0.262), (0.44, -0.04, 0.25),
                        (0.30, -0.13, 0.112)],
        # One long hoop a side from the head stock round the front of the engine and under it to the pegs.
        "wrap_bars": [(0.398, 0.31, 0.075), (0.45, 0.37, 0.19), (0.485, 0.24, 0.262), (0.48, 0.05, 0.27), (0.43, -0.07, 0.262),
                      (0.15, -0.115, 0.258), (-0.08, -0.11, 0.25), (-0.20, -0.135, 0.115)],
    }
    for pid, pts in specs.items():
        m = st.Mesh(); prox = []
        for sg in (-1, 1):
            path = st.polyline([(x, y, sg * z) for x, y, z in pts], 8)
            m.extend(st.tube(path, 0.0135, 14))
            prox += fit.chain(path, 0.015, 3)
        n = "opt_guards_" + pid
        out.append(("guards", pid, cat["guards"][pid]["name"], ["guards", pid],
                    [{"id": "guards", "group": "body", "meshes": [n], "slot": "guards",
                      "mount": {"to": "frame.lugs", "type": "bracket"}, "sockets": {}, "proxies": prox}],
                    {n: (m, "chrome", "body")}))
    return out


# --- Side panels ------------------------------------------------------------------------------------

def extrude(poly, z0, z1):
    """A flat polygon (x, y points, convex, anticlockwise) extruded across z0..z1, rounded a little."""
    poly = np.asarray(poly, float)
    n = len(poly)
    c = poly.mean(0)
    pts, tris = [], []
    for z in (z0, z1):
        pts.append(np.c_[poly, np.full(n, z)])
    pts = np.vstack(pts + [np.array([[c[0], c[1], z0], [c[0], c[1], z1]])])
    for i in range(n):
        j = (i + 1) % n
        tris += [(i, j, n + i), (j, n + j, n + i)]          # the edge band
        tris += [(2 * n, j, i), (2 * n + 1, n + i, n + j)]    # the two faces
    return st.Mesh().add(pts, np.array(tris), pts[:, :2])


def side_panels(fit, cat):
    out = []
    m = st.Mesh(); badge = st.Mesh(); prox = []
    tri = [(-0.43, 0.418), (-0.195, 0.425), (-0.22, 0.27)]
    for sg in (-1, 1):
        z0, z1 = sg * 0.118, sg * 0.138
        m.extend(extrude(tri, min(z0, z1), max(z0, z1)))
        badge.extend(st.lathe([(0.0, 0.0), (0.03, 0.0), (0.03, 0.003), (0.0, 0.004)], seg=24, axis=(0, 0, sg),
                              centre=(-0.29, 0.38, sg * 0.138)))
        zc = (z0 + z1) / 2
        prox += [fit.box((-0.31, 0.39, zc), np.eye(3), (0.11, 0.025, 0.012)), fit.box((-0.25, 0.32, zc), np.eye(3), (0.03, 0.045, 0.012))]
    out.append(("side_panels", "triangle_classic", cat["side_panels"]["triangle_classic"]["name"], ["side_panels", "triangle_classic"],
                [{"id": "side_panels", "group": "body", "meshes": ["opt_panels_triangle", "opt_panels_triangle_badge"],
                  "slot": "side_panels", "mount": {"to": "frame.seat_rails", "type": "seat_rails", "rail_mm": 180.0, "along": 0.0},
                  "sockets": {}, "proxies": prox}],
                {"opt_panels_triangle": (m, "black_gloss", "body"), "opt_panels_triangle_badge": (badge, "chrome", "body")}))
    return out


# --- Tank -------------------------------------------------------------------------------------------

def tanks(fit, cat):
    out = []
    # Teardrop: shorter, rounder and higher at the front than the stock tank, cream, with knee pads
    # and an oval badge; on the same mounts and tunnel.
    xs = np.linspace(-0.11, 0.36, 90)
    t = (xs - xs[0]) / (xs[-1] - xs[0])
    top = 0.50 + 0.12 * np.sin(np.clip(t * 1.15, 0, 1) * math.pi * 0.7) - 0.05 * np.clip(t - 0.85, 0, 1) / 0.15
    bot = 0.43 - 0.02 * t
    hw = 0.055 + 0.105 * np.sin(np.clip(t, 0, 1) * math.pi) ** 0.5
    path = np.stack([xs, (top + bot) / 2, np.zeros_like(xs)], 1)
    tank = st.sweep(path, hw, (top - bot) / 2, 2.4, 160, caps=True)
    pads, badges = st.Mesh(), st.Mesh()
    for sg in (-1, 1):
        x0 = 0.02
        pads.extend(st.box((x0, np.interp(x0, xs, (top + bot) / 2) - 0.005, sg * (np.interp(x0, xs, hw) - 0.004)), (0.12, 0.07, 0.012), 5.0))
        xb = 0.24
        badges.extend(st.lathe([(0.0, 0.0), (0.028, 0.0), (0.028, 0.004), (0.0, 0.005)], seg=24, axis=(0, 0.25, sg),
                               centre=(xb, np.interp(xb, xs, (top + bot) / 2) + 0.02, sg * (np.interp(xb, xs, hw) * 0.93))).transformed(
                                   np.diag([1.4, 1.0, 1.0]), (-xb * 0.4, 0, 0)))
    cap = st.lathe([(0.0, 0.0), (0.035, 0.0), (0.037, 0.012), (0.0, 0.016)], seg=28, axis=(0.1, 1, 0),
                   centre=(0.13, np.interp(0.13, xs, top) - 0.004, 0.0))
    out.append(("tank", "teardrop_14", cat["tank"]["teardrop_14"]["name"], ["tank", "teardrop_14"],
                [{"id": "tank", "group": "body", "meshes": ["opt_tank_teardrop", "opt_tank_teardrop_pads", "opt_tank_teardrop_badges",
                                                            "opt_tank_teardrop_cap"], "slot": "tank",
                  "mount": {"to": "frame.tank_mounts", "type": "tank_mounts", "tunnel_mm": 130.0}, "sockets": {},
                  "proxies": fit.slices(xs, top, bot, hw, 36)}],
                {"opt_tank_teardrop": (tank, "paint_cream", "body"), "opt_tank_teardrop_pads": (pads, "rubber", "body"),
                 "opt_tank_teardrop_badges": (badges, "chrome", "body"), "opt_tank_teardrop_cap": (cap, "chrome", "body")}))
    return out


# --- Clocks (the dashboard slot; on the bar clamp's bracket, steer group) ----------------------------

CLOCK_C = st.BAR_CLAMP + np.array([0.03, 0.065, 0.0])
CLOCK_AX = np.array([-0.5, 0.86, 0.0]) / np.linalg.norm([-0.5, 0.86, 0.0])


def clock(c, r, depth, ticks=12):
    """A chrome-cupped round clock facing up and back along CLOCK_AX: (cup, bezel, dial, marks)."""
    ax = CLOCK_AX
    cup = st.lathe([(0.0, -depth), (r * 0.8, -depth), (r * 0.98, -depth * 0.55), (r, 0.0), (r * 0.98, 0.004)], seg=48,
                   axis=ax, centre=c)
    bezel = st.lathe([(r * 0.86, 0.004), (r * 1.02, 0.002), (r * 1.05, 0.008), (r * 0.99, 0.014), (r * 0.88, 0.012)], seg=48,
                     axis=ax, centre=c)
    dial = st.lathe([(0.0, 0.003), (r * 0.9, 0.003)], seg=48, axis=ax, centre=c)
    u = np.array([0.0, 0.0, 1.0]); w = np.cross(ax, u)
    marks = st.Mesh()
    for k in range(ticks):
        a = math.radians(-135 + 270 * k / (ticks - 1))
        marks.extend(st.box(c + ax * 0.0035 + (u * math.sin(a) + w * math.cos(a)) * r * 0.74, (0.004, 0.004, 0.004), 4.0))
    a = math.radians(-60)
    tip = c + ax * 0.005 + (u * math.sin(a) + w * math.cos(a)) * r * 0.65
    marks.extend(st.tube(np.array([c + ax * 0.005, tip]), 0.0015, 6))
    marks.extend(st.box(c + ax * 0.0035 - w * r * 0.4, (0.018, 0.004, 0.012), 6.0))     # the LCD window
    stalk = st.tube(np.array([c - ax * depth, st.BAR_CLAMP + [0.012, 0.022, (c - st.BAR_CLAMP)[2] * 0.4]]), 0.006, 10)
    return cup.extend(stalk), bezel, dial, marks


def dashboards(fit, cat):
    out = []
    for oid, centres, r, depth, tag in (("analog_speedo", [CLOCK_C + [0.0, 0.005, 0.0]], 0.058, 0.05, "speedo"),
                                        ("twin_clocks", [CLOCK_C + [0.0, 0.018, s * 0.062] for s in (-1, 1)], 0.044, 0.045, "twin")):
        cup, bez, dial, marks = st.Mesh(), st.Mesh(), st.Mesh(), st.Mesh()
        prox = []
        for c in centres:
            a, b, d, m = clock(c, r, depth)
            cup.extend(a); bez.extend(b); dial.extend(d); marks.extend(m)
            prox.append(fit.capsule(c - CLOCK_AX * (depth - r * 0.6), c + CLOCK_AX * 0.004, r * 1.02))
        names = ["opt_dash_%s_%s" % (tag, k) for k in ("cup", "bezel", "dial", "marks")]
        out.append(("dashboard", oid, cat["dashboard"][oid]["name"], ["dashboard", oid],
                    [{"id": "clock", "group": "steer", "meshes": names, "slot": "dashboard",
                      "mount": {"to": "yokes.lugs", "type": "bracket"}, "sockets": {}, "proxies": prox}],
                    dict(zip(names, [(cup, "chrome", "steer"), (bez, "chrome", "steer"), (dial, "dial_cream", "steer"),
                                     (marks, "black_gloss", "steer")]))))
    return out


# --- Spotlights (clamped to the fork tubes under the lower yoke, steer group) -------------------------

def fog_lights(fit, cat):
    out = []
    for oid, r, body_f, lens_f, tag in (("halogen_spots", 0.045, "chrome", "lens_clear", "spots"),
                                         ("amber_fogs", 0.04, "black_gloss", "lens_amber", "fogs")):
        body, clamp, lens, inner = st.Mesh(), st.Mesh(), st.Mesh(), st.Mesh()
        prox = []
        for sg in (-1, 1):
            t = FA + st.FORK_DIR * 0.432 + [0.0, 0.0, sg * 0.095]
            clamp.extend(st.lathe([(0.03, -0.008), (0.035, -0.007), (0.035, 0.007), (0.03, 0.008)], seg=24, axis=st.FORK_DIR,
                                  centre=t))
            L = t + np.array([0.085, -0.025, sg * 0.08])
            arm = np.array([t + [0.025, -0.004, sg * 0.022], L + [-0.045, -0.006, -sg * 0.01], L + [-0.03, 0.0, 0.0]])
            clamp.extend(st.tube(st.polyline(arm, 4), 0.006, 10))
            body.extend(st.lathe([(0.0, -0.045), (r * 0.6, -0.042), (r * 0.95, -0.015), (r, 0.01), (r * 1.03, 0.016),
                                  (r * 0.95, 0.02)], seg=40, axis=(1, 0, 0), centre=L))
            rl, ln = lamp_insides(L + [-0.03, 0.0, 0.0], r)
            inner.extend(rl); lens.extend(ln)
            prox.append(fit.capsule(L + [-0.03, 0, 0], L + [0.012, 0, 0], r))
            prox += fit.chain(st.polyline(arm, 2), 0.006)
        names = ["opt_%s_body" % tag, "opt_%s_clamps" % tag, "opt_%s_refl" % tag, "opt_%s_lens" % tag]
        out.append(("fog_lights", oid, cat["fog_lights"][oid]["name"], ["fog_lights", oid],
                    [{"id": "fog_lights", "group": "steer", "meshes": names, "slot": "fog_lights",
                      "mount": {"to": "yokes.lugs", "type": "bracket"}, "sockets": {}, "proxies": prox}],
                    dict(zip(names, [(body, body_f, "steer"), (clamp, "chrome", "steer"), (inner, "headlight_inner", "steer"),
                                     (lens, lens_f, "steer")]))))
    return out


# --- Panniers (hung from the seat rails, outside the shocks) ------------------------------------------

def panniers(fit, cat):
    out = []
    # Canvas throw-overs: soft bags, a flap with two leather straps, hung from the rails by straps.
    bag, flap, straps = st.Mesh(), st.Mesh(), st.Mesh()
    prox = []
    for sg in (-1, 1):
        z = sg * 0.215
        side = st.box((-0.66, 0.36, z), (0.30, 0.22, 0.075), 3.0)
        bag.extend(side)
        f = st.box((-0.66, 0.44, z + sg * 0.002), (0.31, 0.075, 0.084), 4.0)
        flap.extend(f)
        for x in (-0.74, -0.58):
            straps.extend(st.box((x, 0.405, z + sg * 0.043), (0.025, 0.12, 0.004), 6.0))
            ry = np.interp(x, [-0.79, -0.55], [0.475, 0.44]); rz = np.interp(x, [-0.79, -0.55], [0.07, 0.09])
            straps.extend(st.tube(np.array([[x, 0.465, z - sg * 0.035], [x, ry + 0.02, sg * (rz + 0.015)], [x, ry + 0.016, sg * rz]]),
                                  0.005, 8))
        prox.append(fit.obb(np.vstack(side.p + f.p)))
    out.append(("panniers", "canvas_throwover", cat["panniers"]["canvas_throwover"]["name"], ["panniers", "canvas_throwover"],
                [{"id": "panniers", "group": "body", "meshes": ["opt_pann_canvas", "opt_pann_canvas_flap", "opt_pann_canvas_straps"],
                  "slot": "panniers", "mount": {"to": "frame.seat_rails", "type": "seat_rails", "rail_mm": 180.0, "from_end": 0.0},
                  "sockets": {}, "proxies": prox}],
                {"opt_pann_canvas": (bag, "canvas_olive", "body"), "opt_pann_canvas_flap": (flap, "canvas_olive", "body"),
                 "opt_pann_canvas_straps": (straps, "leather_brown", "body")}))
    # Leather saddlebags: stiff, square-cornered, a deep flap, two chrome buckles and a row of studs.
    bag, flap, metal = st.Mesh(), st.Mesh(), st.Mesh()
    prox = []
    for sg in (-1, 1):
        z = sg * 0.225
        side = st.box((-0.655, 0.355, z), (0.30, 0.21, 0.09), 7.0)
        bag.extend(side)
        f = st.box((-0.655, 0.425, z + sg * 0.002), (0.31, 0.10, 0.098), 7.0)
        flap.extend(f)
        for x in (-0.73, -0.58):
            flap.extend(st.box((x, 0.38, z + sg * 0.05), (0.02, 0.07, 0.004), 6.0))
            metal.extend(st.box((x, 0.36, z + sg * 0.053), (0.026, 0.02, 0.004), 8.0))
        for x in np.linspace(-0.79, -0.52, 10):
            metal.extend(st.lathe([(0.0, 0.0), (0.005, 0.0), (0.004, 0.003), (0.0, 0.004)], seg=12, axis=(0, 0, sg),
                                  centre=(x, 0.39, z + sg * 0.051)))
        for x in (-0.74, -0.57):
            ry = np.interp(x, [-0.79, -0.55], [0.475, 0.44]); rz = np.interp(x, [-0.79, -0.55], [0.07, 0.09])
            flap.extend(st.tube(np.array([[x, 0.47, z - sg * 0.04], [x, ry + 0.02, sg * (rz + 0.015)], [x, ry + 0.016, sg * rz]]),
                                0.005, 8))
        prox.append(fit.obb(np.vstack(side.p + f.p)))
    oid = "cruiser_leather_saddlebags"
    out.append(("panniers", oid, cat["panniers"][oid]["name"], ["panniers", oid],
                [{"id": "panniers", "group": "body", "meshes": ["opt_pann_leather", "opt_pann_leather_flap", "opt_pann_leather_metal"],
                  "slot": "panniers", "mount": {"to": "frame.seat_rails", "type": "seat_rails", "rail_mm": 180.0, "from_end": 0.0},
                  "sockets": {}, "proxies": prox}],
                {"opt_pann_leather": (bag, "leather_tan", "body"), "opt_pann_leather_flap": (flap, "leather_tan", "body"),
                 "opt_pann_leather_metal": (metal, "chrome", "body")}))
    return out


# --- Tool roll (strapped across the tail, behind the seat) -------------------------------------------------

TOOL_C = np.array([-0.845, 0.556, 0.0])


def toolboxes(fit, cat):
    roll = st.lathe([(0.0, -0.1), (0.028, -0.1), (0.032, -0.09), (0.033, 0.0), (0.032, 0.09), (0.028, 0.1), (0.0, 0.1)], seg=28,
                    axis=(0, 0, 1), centre=TOOL_C)
    straps = st.Mesh()
    for zz in (-0.055, 0.055):
        straps.extend(st.lathe([(0.0335, -0.007), (0.0345, -0.006), (0.0345, 0.006), (0.0335, 0.007)], seg=28, axis=(0, 0, 1),
                               centre=TOOL_C + [0, 0, zz]))
        sg = 1.0 if zz > 0 else -1.0
        straps.extend(st.tube(np.array([TOOL_C + [0.02, -0.028, zz], [-0.80, 0.505, sg * 0.068], [-0.79, 0.49, sg * 0.07]]), 0.005, 8))
    return [("toolbox", "tool_roll_pouch", cat["toolbox"]["tool_roll_pouch"]["name"], ["toolbox", "tool_roll_pouch"],
             [{"id": "toolbox", "group": "body", "meshes": ["opt_toolroll", "opt_toolroll_straps"], "slot": "toolbox",
               "mount": {"to": "frame.lugs", "type": "bracket"}, "sockets": {},
               "proxies": [fit.capsule(TOOL_C + [0, 0, -0.1], TOOL_C + [0, 0, 0.1], 0.034)]}],
             {"opt_toolroll": (roll, "leather_brown", "body"), "opt_toolroll_straps": (straps, "leather_tan", "body")})]


# --- More front guards ----------------------------------------------------------------------------

def more_front_guards(fit, cat):
    out = []
    o = np.zeros(3)
    # Alloy cafe blade: slim, shallow and short, polished, on wire stays.
    r, w, d = 0.322, 0.05, 0.012
    path = arc(o, r, 95, 22, 30)
    guard = section_sweep(path, u_profile(w, d, 0.002, crown=3.0), up_hint=(math.cos(math.radians(95)), math.sin(math.radians(95)), 0))
    st_pairs = [(arc(o, r - 0.006, a, a, 1, s * (w - 0.003))[0], (0.0, 0.0, s * 0.11)) for s in (-1, 1) for a in (35, 85)]
    out.append(("front_mudguard", "alloy_cafe_blade", cat["front_mudguard"]["alloy_cafe_blade"]["name"],
                ["front_mudguard", "alloy_cafe_blade"],
                [front_guard_unit(fit, ["opt_fguard_blade", "opt_fguard_blade_stays"], path, w, d)],
                {"opt_fguard_blade": (guard, "alu_polished", "front_lower"),
                 "opt_fguard_blade_stays": (stays(st_pairs, 0.0035), "steel_brushed", "front_lower")}))
    # Low brushed alloy: a full-length guard, a little valance, close over the tyre.
    r, w, d = 0.33, 0.068, 0.03
    path = arc(o, r, 98, -5, 36)
    guard = section_sweep(path, u_profile(w, d, 0.002, crown=5.0), up_hint=(math.cos(math.radians(98)), math.sin(math.radians(98)), 0))
    st_pairs = [(arc(o, r - d * 0.8, a, a, 1, s * (w - 0.003))[0], (0.0, 0.0, s * 0.11)) for s in (-1, 1) for a in (5, 80)]
    out.append(("front_mudguard", "scrambler_alloy_low", cat["front_mudguard"]["scrambler_alloy_low"]["name"],
                ["front_mudguard", "scrambler_alloy_low"],
                [front_guard_unit(fit, ["opt_fguard_low", "opt_fguard_low_stays"], path, w, d)],
                {"opt_fguard_low": (guard, "alu_brushed", "front_lower"),
                 "opt_fguard_low_stays": (stays(st_pairs), "black_satin", "front_lower")}))
    return out


# --- Tyre walls (a band on the stock tyres' sidewalls, riding with the wheels) -------------------------

def wall_band(r_tyre, w, rim_w, lo, hi):
    """A thin band on both sidewalls of the stock tyre between radii rim+lo and rim+hi, standing 0.4 to
    1.2 mm proud of the rubber."""
    rim_r = 0.2159
    prof = st.tyre_profile(r_tyre, w, rim_r, rim_w, n_side=80)
    side = [(r, z) for r, z in prof if z > 0 and rim_r + lo <= r <= rim_r + hi]
    side.sort(key=lambda q: -q[0])
    m = st.Mesh()
    for sg in (-1, 1):
        outer = [(r, sg * (z + 0.0012)) for r, z in side]
        inner = [(r, sg * (z + 0.0004)) for r, z in side[::-1]]
        loop = outer + inner + [outer[0]]
        if sg < 0:
            loop = loop[::-1]
        m.extend(st.lathe(loop, seg=144, axis=(0, 0, 1), centre=(0, 0, 0)))
    return m


def tyres(fit, cat):
    out = []
    for oid, lo, hi, f, tag in (("classic_whitewall_110", 0.022, 0.052, "tyre_white", "ww"),
                                ("cafe_redwall_100", 0.040, 0.047, "tyre_red", "rw")):
        units = [{"id": "tyre_wall_front", "group": "front_wheel", "meshes": ["opt_tyre_%s_front" % tag], "slot": "tyres",
                  "mount": {"to": "fork_lowers.front_axle", "type": "axle", "axle_mm": 25.0}, "sockets": {}, "proxies": []},
                 {"id": "tyre_wall_rear", "group": "rear_wheel", "meshes": ["opt_tyre_%s_rear" % tag], "slot": "tyres",
                  "mount": {"to": "swingarm.rear_axle", "type": "axle", "axle_mm": 25.0}, "sockets": {}, "proxies": []}]
        out.append(("tyres", oid, cat["tyre"][oid]["name"], ["tyre", oid], units,
                    {"opt_tyre_%s_front" % tag: (wall_band(0.300, 0.12, 0.096, lo, hi), f, "front_wheel"),
                     "opt_tyre_%s_rear" % tag: (wall_band(0.312, 0.16, 0.115, lo, hi), f, "rear_wheel")}))
    return out


def pack_options(fit, cat):
    return (seats(fit, cat) + front_guards(fit, cat) + rear_guards(fit, cat) + indicators(fit, cat) + mirrors(fit, cat)
            + levers(fit) + grips(fit) + screens(fit, cat) + racks(fit, cat) + headlights(fit, cat) + exhausts(fit, cat)
            + guards(fit, cat) + side_panels(fit, cat) + tanks(fit, cat) + dashboards(fit, cat) + fog_lights(fit, cat)
            + panniers(fit, cat) + toolboxes(fit, cat) + more_front_guards(fit, cat) + tyres(fit, cat))
