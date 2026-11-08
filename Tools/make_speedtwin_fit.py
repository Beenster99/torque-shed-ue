"""The Speed Twin's fitment data: typed sockets, datums, collision proxies and the alternative parts
the shed can fit, for the fitment system (Source/TorqueShedProto/Fit, the plan's "Fitting parts").

    python Tools/make_speedtwin_fit.py     writes Data/Bike/SpeedTwin/fitment.json and
                                           Data/Bike/SpeedTwin/Options/*.glb

Run it after Tools/make_speedtwin.py: it reads that script's numbers and shapes, so the sockets sit
exactly where the model's parts are. Same frame as speedtwin.json: metres, x forward, y up, z right,
origin at axle height midway between the axles (suspension extended).

How the data reads (Fit/Fitment.cpp is the other half):
  units     what fits as one piece: its model meshes, its group (how the pawn moves it), the sockets it
            offers its children, the socket it mounts by ("mount": parent unit.socket plus its own
            type and size), and simple collision proxies (capsules and boxes) for the clearance checks.
            Each unit's meshes are modelled in place on the stock bike, so a child's mount frame is its
            parent socket's frame on the stock bike; a part on a path socket (the bars) keeps its
            distance from the path's end.
  options   per slot (parts.json kinds), stock first: each names its parts.json entry and the units it
            brings in place of the stock ones (same unit id), with their own meshes and sockets.
  conform   parts made to fit between two sockets (cables, hoses, headers, shocks, the chain): placed
            rigidly for now, and the check reports how far their ends moved from where they were made.
"""
import json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import make_speedtwin as st  # noqa: E402
import speedtwin_pack as pack  # noqa: E402

OUT = os.path.join(REPO, "Data", "Bike", "SpeedTwin")
OPT_DIR = os.path.join(OUT, "Options")

FA, RA = st.FRONT_AXLE, st.REAR_AXLE
FORK_DIR, FORK_BACK = st.FORK_DIR, st.FORK_BACK
STEER_POINT, SWING_PIVOT, BAR_CLAMP = st.STEER_POINT, st.SWING_PIVOT, st.BAR_CLAMP
GROUP_ORIGIN = {"body": np.zeros(3), "steer": np.zeros(3), "front_lower": FA, "front_wheel": FA,
                "swingarm": SWING_PIVOT, "rear_wheel": RA}
BAR_MM = 22.0          # 7/8 inch bars (the model's bar is 22 mm across)
STEER_LOCK_DEG = 33.0  # approximate: check against beenster's bike at full lock


def v(a):
    return [round(float(x), 6) for x in a]


def unit_vec(a):
    a = np.asarray(a, np.float64)
    return a / np.linalg.norm(a)


# --- Proxies ------------------------------------------------------------------------------------------

def capsule(a, b, r):
    return {"shape": "capsule", "a": v(a), "b": v(b), "r": round(float(r), 5)}


def chain(points, r, every=1):
    pts = np.asarray(points)[::every]
    if not np.allclose(pts[-1], points[-1]):
        pts = np.vstack([pts, points[-1]])
    return [capsule(pts[i], pts[i + 1], r) for i in range(len(pts) - 1)]


def box(centre, axes, half):
    return {"shape": "box", "c": v(centre), "x": v(axes[0]), "y": v(axes[1]), "z": v(axes[2]), "h": v(half)}


def obb(points):
    """An oriented box round points (principal axes)."""
    p = np.asarray(points, np.float64)
    c = p.mean(0)
    if len(p) < 4:
        return box(c, np.eye(3), np.full(3, 0.002))
    _, _, vt = np.linalg.svd(p - c, full_matrices=False)
    ax = vt
    if np.linalg.det(ax) < 0:
        ax[2] *= -1
    q = (p - c) @ ax.T
    lo, hi = q.min(0), q.max(0)
    centre = c + ((lo + hi) / 2) @ ax
    return box(centre, ax, np.maximum((hi - lo) / 2, 0.002))


def auto_proxies(mesh, group, max_pieces=12):
    """Boxes round each piece of a mesh (both sides of a split mesh separately), in the model frame."""
    o = GROUP_ORIGIN[group]
    pieces = [p + o for p in mesh.p if len(p) >= 4]
    if len(pieces) > max_pieces:          # many small pieces: one box per side
        allp = np.vstack(pieces)
        sides = [allp[allp[:, 2] < -0.01], allp[np.abs(allp[:, 2]) <= 0.01], allp[allp[:, 2] > 0.01]]
        pieces = [s for s in sides if len(s) >= 4]
    out = []
    for p in pieces:
        b = obb(p)
        if np.prod(b["h"]) * 8 < 2e-7:     # smaller than a sugar cube: decoration
            continue
        out.append(b)
    return out


def torus(centre, axis, major, minor, n=24):
    """A tyre: a ring of capsules round the axle."""
    axis = unit_vec(axis)
    u = unit_vec(np.cross(axis, [0, 1, 0]) if abs(axis[1]) < 0.9 else np.cross(axis, [1, 0, 0]))
    w = np.cross(axis, u)
    pts = [centre + major * (math.cos(a) * u + math.sin(a) * w) for a in np.linspace(0, 2 * math.pi, n + 1)]
    return [capsule(pts[i], pts[i + 1], minor) for i in range(n)]


def slices(xs, top, bot, hw, n=8):
    """Boxes along a loft (tank, seat): n slices with their own height and half width."""
    out = []
    idx = np.linspace(0, len(xs) - 1, n + 1).astype(int)
    for i0, i1 in zip(idx[:-1], idx[1:]):
        seg = slice(i0, i1 + 1)
        x0, x1 = xs[i0], xs[i1]
        t, b, w = np.max(top[seg]), np.min(bot[seg]), np.max(hw[seg])
        out.append(box(((x0 + x1) / 2, (t + b) / 2, 0.0), np.eye(3), (abs(x1 - x0) / 2, (t - b) / 2, w)))
    return out


# --- Sockets ------------------------------------------------------------------------------------------

def sock(kind, at, x, y=(0, 1, 0), **extra):
    x = unit_vec(x)
    y = np.asarray(y, np.float64)
    y = unit_vec(y - x * x.dot(y))
    d = {"type": kind, "at": v(at), "x": v(x), "y": v(y)}
    d.update(extra)
    return d


def path_sock(kind, path, up=(0, 1, 0), **extra):
    d = {"type": kind, "path": [v(p) for p in path], "up": v(up)}
    d.update(extra)
    return d


def bar_halves(path):
    """A bar's centre line (left end to right end) as two path sockets, each from the clamp outwards."""
    p = np.asarray(path)
    mid = int(np.argmin(np.abs(p[:, 2])))
    right = p[mid:]
    left = p[:mid + 1][::-1]
    return left, right


# --- The stock bike as fit units ----------------------------------------------------------------------

def stock_units(P):
    """P: make_speedtwin.build()'s parts. Returns the units in assembly order (parents first)."""
    U = []

    def unit(uid, meshes, group=None, mount=None, sockets=None, proxies=None, slot=None, cat=None,
             conform=None, auto=True):
        group = group or P[meshes[0]][2]
        prox = list(proxies or [])
        if auto and proxies is None:
            for m in meshes:
                prox += auto_proxies(P[m][0], P[m][2])
        u = {"id": uid, "group": group, "meshes": meshes}
        if slot:
            u["slot"] = slot
        if cat:
            u["catalogue"] = cat
        if mount:
            u["mount"] = mount
        u["sockets"] = sockets or {}
        u["proxies"] = prox
        if conform:
            u["conform"] = conform
        U.append(u)
        return u

    # Frame: the datum part. Its tubes, as make_speedtwin.py lays them.
    head_top = STEER_POINT + FORK_DIR * 0.66
    head_bot = STEER_POINT + FORK_DIR * 0.46
    fp = chain([head_bot, head_top], 0.03)
    fp += chain(st.polyline([head_top + [-0.02, -0.01, 0], (0.15, 0.46, 0.0), (-0.12, 0.42, 0.0), (-0.32, 0.40, 0.0)], 8), 0.024, 3)
    for s in (-1, 1):
        down = st.polyline([head_bot + [0.0, 0.0, s * 0.02], (0.40, 0.30, s * 0.07), (0.37, 0.10, s * 0.09),
                            (0.35, -0.04, s * 0.10), (0.27, -0.135, s * 0.11), (-0.20, -0.135, s * 0.11), (-0.33, 0.0, s * 0.11),
                            (-0.34, 0.20, s * 0.11), (-0.32, 0.40, s * 0.10)], 8)
        fp += chain(down, 0.017, 3)
        fp += chain(st.polyline([(-0.32, 0.40, s * 0.10), (-0.55, 0.44, s * 0.09), (-0.79, 0.475, s * 0.07)], 8), 0.014, 3)
        fp += chain(st.polyline([(-0.34, 0.20, s * 0.11), (-0.47, 0.33, s * 0.10), (-0.55, 0.44, s * 0.09)], 8), 0.012, 3)
    engine_pts = [(0.37, 0.10, -0.09), (0.37, 0.10, 0.09), (-0.20, -0.135, -0.11), (-0.20, -0.135, 0.11),
                  (-0.33, 0.0, -0.11), (-0.33, 0.0, 0.11)]
    rail = np.array([(-0.32, 0.43, 0.0), (-0.55, 0.47, 0.0), (-0.79, 0.50, 0.0)])
    unit("frame", ["frame"], sockets={
        "head_tube": sock("steering_head", head_top, FORK_DIR, (1, 0, 0), stem_mm=30.0),
        "engine_mounts": sock("engine_hard_points", (0, 0, 0), (1, 0, 0), points=[v(p) for p in engine_pts]),
        "tank_mounts": sock("tank_mounts", (-0.12, 0.42, 0.0), (1, 0, 0), tunnel_mm=130.0),
        "seat_rails": path_sock("seat_rails", rail, rail_mm=180.0),
        "swingarm_pivot": sock("swingarm_pivot", SWING_PIVOT, (0, 0, 1), spindle_mm=20.0),
        "rearset_plates": sock("rearset_plates", (-0.26, 0.12, 0.0), unit_vec((-0.6, 0.8, 0.0)), (1, 0, 0),
                               holes=[0.0, 0.02, 0.04, -0.02]),
        "silencer_hangers": sock("bracket", (-0.60, 0.21, 0.0), (1, 0, 0)),
        "lugs": sock("bracket", (0, 0, 0), (1, 0, 0)),
    }, proxies=fp)

    # Steering: yokes and stanchions turn about the head tube.
    stanch = FA + FORK_DIR * 0.25, FA + FORK_DIR * 0.69
    yp = []
    for z in (-0.095, 0.095):
        yp += chain([stanch[0] + [0, 0, z], stanch[1] + [0, 0, z]], 0.029)
    for along in (0.47, 0.64):
        c = FA + FORK_DIR * along + FORK_BACK * 0.015
        yp.append(box(c, st.rot_z(st.RAKE).T, (0.035, 0.014, 0.125)))   # rows: the yoke's own axes
    unit("yokes", ["yokes", "fork_stanchions"], mount={"to": "frame.head_tube", "type": "steering_head", "stem_mm": 30.0},
         sockets={
             "bar_clamp": sock("bar_clamp", BAR_CLAMP, (0, 0, 1), (0, 1, 0), bar_mm=BAR_MM),
             "fork_tubes": path_sock("fork_tube", [FA + FORK_DIR * 0.25, FA + FORK_DIR * 0.69], up=(1, 0, 0), tube_mm=58.0),
             "fork_slide": sock("fork_slide", FA, FORK_DIR, (1, 0, 0)),
             "lugs": sock("bracket", BAR_CLAMP, (1, 0, 0)),
         }, proxies=yp)
    unit("clock", ["clock_body", "clock_face"], mount={"to": "yokes.lugs", "type": "bracket"})
    # The indicators are their own units (the indicators slot), on brackets the headlight and the
    # tail carry, so they go where another headlight or tail puts them.
    hl_c = FA + FORK_DIR * 0.56 + np.array([0.075, 0.0, 0.0])
    unit("headlight", ["headlight_shell", "headlight_lens", "headlight_projector"], slot="headlight",
         mount={"to": "yokes.fork_tubes", "type": "fork_tube", "tube_mm": 58.0, "from_end": float(0.69 - 0.56)},
         sockets={"indicator_mount": sock("bracket", hl_c, (1, 0, 0))})
    unit("indicators_front", ["front_indicators"], mount={"to": "headlight.indicator_mount", "type": "bracket"})

    # Bars, and what rides on them.
    bars_path = st.polyline([BAR_CLAMP + [0.0, 0, -0.38], BAR_CLAMP + [-0.03, 0.03, -0.30], BAR_CLAMP + [0.0, 0.025, -0.12],
                             BAR_CLAMP, BAR_CLAMP + [0.0, 0.025, 0.12], BAR_CLAMP + [-0.03, 0.03, 0.30],
                             BAR_CLAMP + [0.0, 0, 0.38]], 6)
    unit("bars", ["handlebar"], slot="handlebars", cat=["handlebars", None], **bar_unit(bars_path, BAR_MM))
    for sd, s in (("l", -1), ("r", 1)):
        side = "bars.bar_" + sd

        def on_bar(z_out):
            return {"to": side, "type": "bar", "bar_mm": BAR_MM, "from_end": round(float(0.38 - z_out), 5)}
        g0 = BAR_CLAMP + [-0.005, 0.012, s * 0.255]; g1 = BAR_CLAMP + [0.0, 0.002, s * 0.375]
        unit("grip_" + sd, ["grips_" + sd], mount=on_bar(0.315), proxies=chain([g0, g1], 0.017))
        unit("switch_" + sd, ["grips_switches_" + sd], mount=on_bar(0.225))
        lever = "lever_clutch" if s < 0 else "lever_brake"
        lp = st.polyline(st.lever_points(s), 6)
        unit(lever, [lever], mount=on_bar(0.23), proxies=chain(lp, 0.008, 2))
        end = BAR_CLAMP + [0.0, 0.0, s * 0.385]
        mc = end + np.array([0.03, 0.16, s * 0.06])
        unit("mirror_" + sd, ["mirrors_" + sd, "mirror_glass_" + sd, "mirror_stalks_" + sd], mount=on_bar(0.38),
             proxies=[capsule(mc, mc, 0.048)] + chain([end, end + [0.02, 0.1, s * 0.05]], 0.006))
    lever_r = BAR_CLAMP + np.array([0.02, 0.0, 0.22]); lever_l = BAR_CLAMP + np.array([0.02, 0.0, -0.22])
    unit("cables", ["cables"], mount={"to": "yokes.lugs", "type": "bracket"}, proxies=[],
         conform={"ends": [{"unit": u_id, "at": v(p)} for (p, _, _, u_id) in stock_cable_ends().values()]})

    # Front suspension and wheel.
    lower = auto_proxies(P["fork_lowers"][0], "front_lower") + auto_proxies(P["front_calipers"][0], "front_lower")
    unit("fork_lowers", ["fork_lowers", "front_calipers"], mount={"to": "yokes.fork_slide", "type": "fork_slide"},
         sockets={"front_axle": sock("axle", FA, (0, 0, 1), axle_mm=25.0),
                  "guard_mounts": sock("bracket", FA, (1, 0, 0))}, proxies=lower)
    a = np.linspace(math.radians(98), math.radians(30), 20)
    guard = np.stack([0.325 * np.cos(a), 0.325 * np.sin(a), np.zeros_like(a)], 1) + FA
    unit("front_mudguard", ["front_mudguard"], slot="front_mudguard", mount={"to": "fork_lowers.guard_mounts", "type": "bracket"},
         proxies=[box(guard[i] * 0.5 + guard[i + 3] * 0.5, frame_along(guard[i + 3] - guard[i]),
                      (np.linalg.norm(guard[i + 3] - guard[i]) / 2, 0.006, 0.075)) for i in range(0, 17, 3)])
    unit("front_wheel", ["front_rim", "front_tyre", "front_discs", "front_carriers"], mount={"to": "fork_lowers.front_axle", "type": "axle", "axle_mm": 25.0},
         proxies=torus(FA, (0, 0, 1), 0.300 - 0.058, 0.058))

    # Engine and what hangs off it.
    eng = ["crankcase", "sump", "gearbox", "cylinders", "cam_cover", "engine_covers", "cover_badges", "bolts", "throttle_bodies"]
    ep = []
    for m in ["crankcase", "sump", "gearbox", "cylinders", "cam_cover", "engine_covers", "throttle_bodies"]:
        ep += auto_proxies(P[m][0], "body")
    ports = [np.array([0.25, 0.315, s * 0.09]) for s in (-1, 1)]
    unit("engine", eng, cat=["engine", "parallel_1200"], mount={"to": "frame.engine_mounts", "type": "engine_hard_points",
                                                                "points": [v(p) for p in engine_pts]},
         sockets={"exhaust_ports": sock("exhaust_ports", ports[1], unit_vec((0.33, 0.31, 0.12) - ports[1]), port_mm=44.0),
                  "lugs": sock("bracket", (0, 0, 0), (1, 0, 0))}, proxies=ep)
    hu, _ = headers_unit(0.0)
    unit("headers", ["headers"], mount=hu["mount"], sockets=hu["sockets"], proxies=hu["proxies"], conform=hu["conform"])
    unit("silencers", ["silencers", "silencer_caps"], slot="exhaust", cat=["exhaust", None],
         **silencer_unit(np.linspace([-0.25, -0.065, 0.158], [-0.84, 0.155, 0.175], 16),
                         np.interp(np.linspace(0, 1, 16), [0.0, 0.12, 0.45, 1.0], [0.024, 0.03, 0.046, 0.05]), 48.0))
    unit("radiator", ["radiator"], mount={"to": "frame.lugs", "type": "bracket"})
    unit("coolant_hoses", ["coolant_hoses"], mount={"to": "engine.lugs", "type": "bracket"}, proxies=[],
         conform={"ends": [{"unit": "radiator", "at": [0.34, 0.265, 0.05]}, {"unit": "engine", "at": [0.26, 0.37, 0.05]}]})

    # Tank, seat, tail, side panels.
    xs = np.linspace(-0.135, 0.37, 110)
    t = (xs - xs[0]) / (xs[-1] - xs[0])
    top = 0.505 + 0.115 * np.sin(np.clip(t * 1.25, 0, 1) * math.pi * 0.62) - 0.06 * np.clip(t - 0.8, 0, 1) / 0.2
    bot = 0.43 - 0.02 * t
    hw = np.where(t < 0.05, 0.07, 0.06 + 0.115 * np.sin(np.clip(t, 0, 1) * math.pi) ** 0.6)
    unit("tank", ["tank", "tank_decals", "tank_badges", "tank_cap"], slot="tank",
         mount={"to": "frame.tank_mounts", "type": "tank_mounts", "tunnel_mm": 130.0}, proxies=slices(xs, top, bot, hw, 36))
    xs = np.linspace(-0.77, -0.11, 48)
    t = (xs - xs[0]) / (xs[-1] - xs[0])
    top = np.interp(xs, [-0.77, -0.76, -0.60, -0.45, -0.30, -0.15, -0.11], [0.55, 0.570, 0.568, 0.548, 0.515, 0.48, 0.472])
    bot = np.interp(xs, [-0.77, -0.60, -0.45, -0.30, -0.11], [0.515, 0.505, 0.49, 0.47, 0.445])
    hw = 0.10 + 0.045 * np.sin(np.clip(t * 1.3, 0, 1) * math.pi * 0.5)
    unit("seat", ["seat", "seat_pan"], slot="saddle", mount={"to": "frame.seat_rails", "type": "seat_rails", "rail_mm": 180.0,
                                                             "along": 0.2},
         proxies=slices(xs, top, bot, hw, 10))
    hanger = st.polyline([(-0.78, 0.468, 0.0), (-0.86, 0.445, 0.0), (-0.95, 0.335, 0.0)], 6)
    tp = [box((-0.775, 0.49, 0.0), np.eye(3), (0.05, 0.02, 0.07)), capsule((-0.822, 0.494, 0), (-0.848, 0.494, 0), 0.022)]
    tp += [box(hanger[i] * 0.5 + hanger[i + 3] * 0.5, frame_along(hanger[i + 3] - hanger[i]),
               (np.linalg.norm(hanger[i + 3] - hanger[i]) / 2, 0.006, 0.09)) for i in range(0, len(hanger) - 3, 3)]
    tp.append(box(((-0.90 - 0.98) / 2, (0.42 + 0.30) / 2, 0.0), frame_along(np.array([-0.08, -0.12, 0.0])),
                  (0.072, 0.002, 0.1)))
    unit("tail", ["tail", "rear_light", "plate_hanger", "plate"], slot="tail",
         mount={"to": "frame.seat_rails", "type": "seat_rails", "rail_mm": 180.0, "from_end": 0.0}, proxies=tp,
         sockets={"indicator_mount": sock("bracket", (-0.86, 0.44, 0.0), (1, 0, 0))})
    unit("indicators_rear", ["rear_indicators"], mount={"to": "tail.indicator_mount", "type": "bracket"},
         proxies=[capsule((-0.86, 0.44, sg * 0.06), (-0.86, 0.44, sg * 0.13), 0.016) for sg in (-1, 1)])
    unit("side_panels", ["side_panel_l", "side_panel_l_decal", "side_panel_r", "side_panel_r_decal"], slot="side_panels",
         mount={"to": "frame.seat_rails", "type": "seat_rails", "rail_mm": 180.0, "along": 0.0})
    unit("battery_box", ["battery_box"], mount={"to": "frame.lugs", "type": "bracket"})
    unit("footrests", ["footrests"], slot="rearsets", mount={"to": "frame.rearset_plates", "type": "rearset_plates"})
    unit("side_stand", ["side_stand"], mount={"to": "frame.lugs", "type": "bracket"})
    unit("horn", ["horn"], mount={"to": "frame.lugs", "type": "bracket"})

    # Rear: swingarm on its pivot, the wheel in its axle slot, and what hangs off them.
    rel = RA - SWING_PIVOT
    sp = []
    for s in (-1, 1):
        sp += chain(np.linspace(SWING_PIVOT + [0, 0, s * 0.105], SWING_PIVOT + [rel[0] - 0.02, rel[1], s * 0.11], 10), 0.022, 3)
    sp += chain([SWING_PIVOT + [0, 0, -0.12], SWING_PIVOT + [0, 0, 0.12]], 0.025)
    unit("swingarm", ["swingarm"], slot="swingarm", mount={"to": "frame.swingarm_pivot", "type": "swingarm_pivot", "spindle_mm": 20.0},
         sockets={"rear_axle": sock("axle", RA, unit_vec(rel), (0, 1, 0), axle_mm=25.0),
                  "lugs": sock("bracket", SWING_PIVOT, (1, 0, 0))}, proxies=sp)
    unit("rear_wheel", ["rear_rim", "rear_tyre", "rear_disc", "rear_sprocket"], mount={"to": "swingarm.rear_axle", "type": "axle", "axle_mm": 25.0},
         proxies=torus(RA, (0, 0, 1), 0.312 - 0.075, 0.075))
    # The caliper was modelled with the body; it belongs on the axle (it rides with the wheel).
    unit("rear_caliper", ["rear_caliper"], group="swingarm", mount={"to": "swingarm.rear_axle", "type": "axle", "axle_mm": 25.0})
    unit("rear_brake_line", ["rear_brake_line"], group="body", mount={"to": "frame.lugs", "type": "bracket"}, proxies=[],
         conform={"ends": [{"unit": "rear_caliper", "at": [-0.70, 0.02, 0.12]}]})
    unit("chain", ["chain"], mount={"to": "swingarm.lugs", "type": "bracket"}, proxies=[],
         conform={"ends": [{"unit": "rear_wheel", "at": v(RA + [0, 0.104, 0.075])}]})
    unit("chain_guard", ["chain_guard"], mount={"to": "swingarm.lugs", "type": "bracket"})
    a = np.linspace(math.radians(30), math.radians(70), 16)
    hug = np.stack([rel[0] + 0.335 * np.cos(a), rel[1] + 0.335 * np.sin(a), np.zeros_like(a)], 1) + SWING_PIVOT
    unit("hugger", ["hugger"], mount={"to": "swingarm.lugs", "type": "bracket"},
         proxies=[box(hug[i] * 0.5 + hug[i + 3] * 0.5, frame_along(hug[i + 3] - hug[i]),
                      (np.linalg.norm(hug[i + 3] - hug[i]) / 2, 0.004, 0.085)) for i in range(0, 13, 3)])
    unit("shocks", ["shock_bodies", "shock_springs", "shock_tops"], group="body", mount={"to": "frame.lugs", "type": "bracket"}, proxies=[],
         conform={"ends": [{"unit": "swingarm", "at": [-0.66, 0.10, 0.125]}]})
    return U


def headers_unit(dz):
    """The header pipes from the exhaust ports to the silencers' slip joint, their tails kicked dz
    metres outboard (0: as modelled). Returns the unit and its mesh."""
    pts = [(0.25, 0.315, 0.09), (0.33, 0.31, 0.12), (0.42, 0.26, 0.15), (0.45, 0.15, 0.16), (0.43, 0.03, 0.16),
           (0.36, -0.055, 0.155), (0.20, -0.08, 0.15), (-0.08, -0.085, 0.15 + dz * 0.6), (-0.25, -0.065, 0.155 + dz)]
    header = st.polyline(pts, 8)
    hp = []
    for s in (-1, 1):
        hp += chain(header * [1, 1, s], 0.022, 4)
    port = np.array([0.25, 0.315, 0.09])
    s_dir = unit_vec(np.array([-0.84, 0.155, 0.175]) - np.array([-0.25, -0.065, 0.158]))
    u = {"id": "headers", "group": "body", "mount": {"to": "engine.exhaust_ports", "type": "exhaust_ports", "port_mm": 44.0},
         "sockets": {"outlet": sock("exhaust_slip", header[-1], s_dir, pipe_mm=44.0)}, "proxies": hp,
         "conform": {"ends": [{"unit": "engine", "at": v(port)}]}}
    mesh = st.both_sides(st.tube(header, 0.022, 24).mask_by_uv(lambda uv: 1.0 - (uv[:, 1] - 0.12) / 0.3))
    return u, mesh


def frame_along(d):
    x = unit_vec(d)
    z = np.array([0.0, 0.0, 1.0])
    y = unit_vec(np.cross(z, x))
    return [x, y, np.cross(x, y)]


def bar_unit(path, bar_mm, extra_proxies=()):
    left, right = bar_halves(path)
    return {"mount": {"to": "yokes.bar_clamp", "type": "bar_clamp", "bar_mm": bar_mm},
            "sockets": {"bar_l": path_sock("bar", left, bar_mm=bar_mm), "bar_r": path_sock("bar", right, bar_mm=bar_mm)},
            "proxies": chain(path, bar_mm / 2000.0, 3) + list(extra_proxies)}


def silencer_unit(s_path, radii, bore_mm):
    prox = []
    for s in (-1, 1):
        q = s_path * [1, 1, s]
        for i in range(0, len(q) - 1, 3):
            j = min(i + 3, len(q) - 1)
            prox.append(capsule(q[i], q[j], max(radii[i], radii[j])))
    return {"mount": {"to": "headers.outlet", "type": "exhaust_slip", "pipe_mm": bore_mm}, "proxies": prox}



# --- Where a part on the bar lands on another bar (the same maths as Fit/Fitment.cpp) -----------------

def frame_from(at, x, up):
    x = unit_vec(x)
    y = np.asarray(up, np.float64) - x * x.dot(up)
    y = unit_vec(y)
    return np.stack([x, y, np.cross(x, y)], 1), np.asarray(at, np.float64)   # columns are the axes


def path_frame(path, s, up=(0, 1, 0)):
    """The frame s metres along a path socket: x along the path, y towards up."""
    left = max(s, 0.0)
    for k in range(1, len(path)):
        seg = path[k] - path[k - 1]
        L = np.linalg.norm(seg)
        if left <= L or k + 1 == len(path):
            f = min(max(left / L, 0.0), 1.0) if L > 1e-9 else 0.0
            return frame_from(path[k - 1] + seg * f, seg, up)
        left -= L
    return frame_from(path[-1], path[-1] - path[0], up)


def path_length(path):
    return float(np.sum(np.linalg.norm(np.diff(path, axis=0), axis=1)))


def on_new_bar(point, old_half, new_half, from_end):
    """A point on a part mounted from_end metres from a bar end, moved from the old bar to the new."""
    R0, T0 = path_frame(old_half, path_length(old_half) - from_end)
    R1, T1 = path_frame(new_half, path_length(new_half) - from_end)
    return R1 @ (R0.T @ (np.asarray(point) - T0)) + T1


# --- Alternatives, in the Speed Twin's own style ------------------------------------------------------

STOCK_BAR = st.polyline([BAR_CLAMP + [0.0, 0, -0.38], BAR_CLAMP + [-0.03, 0.03, -0.30], BAR_CLAMP + [0.0, 0.025, -0.12],
                         BAR_CLAMP, BAR_CLAMP + [0.0, 0.025, 0.12], BAR_CLAMP + [-0.03, 0.03, 0.30],
                         BAR_CLAMP + [0.0, 0, 0.38]], 6)


CLUBMAN_MIN_WIDTH = 700.0   # mm: narrower puts the gloves and forearms over the Speed Twin's tank
MIRROR_FROM_END = 0.21   # where option bars carry the mirrors: on the lever clamps, inboard of the switches


def stock_cable_ends():
    """Each cable's bar end: (point, bar side, the mount's distance from the bar end, the unit it rides)."""
    lever_l = BAR_CLAMP + np.array([0.02, 0.0, -0.22]); lever_r = BAR_CLAMP + np.array([0.02, 0.0, 0.22])
    return {"clutch": (lever_l, "l", 0.38 - 0.23, "lever_clutch"), "brake": (lever_r, "r", 0.38 - 0.23, "lever_brake"),
            "throttle": (lever_r + [0.0, 0.0, 0.08], "r", round(0.38 - 0.315, 5), "grip_r")}


def bar_shape(width_mm, rise, pullback, drop_mid=0.0):
    """A bar's centre line from its catalogue width and rise: flat across the clamp, then a bend up (or
    down) and back to the ends. Same 7 control points as the stock bar."""
    h = width_mm / 2000.0
    c = BAR_CLAMP
    pts = [c + [-pullback, rise, -h], c + [-pullback * 0.7, rise * 0.9 + 0.005, -h * 0.78], c + [0.0, drop_mid, -0.12],
           c, c + [0.0, drop_mid, 0.12], c + [-pullback * 0.7, rise * 0.9 + 0.005, h * 0.78], c + [-pullback, rise, h]]
    return st.polyline(pts, 6)


def clubman_shape(width_mm, drop, pull):
    """A clubman drop bar: flat across the clamp, a short kick forward, then down by drop and back by
    pull to the ends, so the grips sit low and narrow like clip-ons. Most of the sweep back is done
    by 250 mm out, so the grips sit on a nearly straight end and the hands aren't turned in towards
    the tank."""
    h = width_mm / 2000.0
    c = BAR_CLAMP
    half = [c + [0.02, -0.008, 0.15], c + [0.0, -drop * 0.6, 0.19], c + [-pull * 0.6, -drop, 0.25], c + [-pull, -drop, h]]
    pts = [p * [1, 1, -1] + c * [0, 0, 2] for p in half[::-1]] + [c] + half
    return st.polyline(pts, 6)


def options(cat):
    """The alternatives: (slot, id, display name, catalogue id, unit, meshes {name: (Mesh, finish, group)})."""
    out = []
    # Bars: shapes from the catalogue's width and rise (rise in metres).
    shapes = {
        "clubman_bar": dict(pullback=0.10, drop_mid=-0.005),
        "touring_bend_bar": dict(pullback=0.13),
        "scrambler_moto_chrome": dict(pullback=0.06),
    }
    for pid, kw in shapes.items():
        e = cat["handlebars"][pid]
        look = e.get("look", {})
        if look.get("bend") == "clubman":
            # The catalogue's look (drop and pull back, metres): a real clubman, not just a flat bar.
            # The Speed Twin's tank top is only about 40 mm below its bar clamp, so the catalogue's 80 mm
            # drop (and 650 mm width) put the rider's gloves and forearms on the tank: 25 mm down,
            # 100 mm back and at least 700 mm wide keeps them clear and still reads as a drop bar.
            path = clubman_shape(max(e["width"], CLUBMAN_MIN_WIDTH), min(look.get("drop", 0.08), 0.025), min(look.get("pull", 0.12), 0.10))
        else:
            path = bar_shape(e["width"], e["rise"], **kw)
        mesh = st.Mesh()
        bar_mm = BAR_MM
        extra = []
        if pid == "scrambler_moto_chrome":
            bar_mm = 28.6     # fat MX bar: 1 1/8 inch at the clamp
            mesh.extend(st.tube(path, 0.0143, 14))
            i0, i1 = int(len(path) * 0.3), int(len(path) * 0.7)
            brace = np.array([path[i0] + [0, 0.035, 0], path[i1] + [0, 0.035, 0]])
            mesh.extend(st.tube(np.linspace(brace[0], brace[1], 6), 0.009, 12))
            extra = chain(brace, 0.009)
            fin = "chrome" if "chrome" in st.FINISHES else "steel_brushed"
        else:
            mesh.extend(st.tube(path, 0.011, 12))
            fin = "black_satin"
        name = "opt_bars_" + pid
        u = {"id": "bars", "group": "steer", "meshes": [name], "slot": "handlebars", "catalogue": ["handlebars", pid]}
        u.update(bar_unit(path, bar_mm, extra))
        # The cables re-routed from where the levers and throttle land on these bars (plan point 3).
        old_l, old_r = bar_halves(STOCK_BAR)
        new_l, new_r = bar_halves(path)
        ends = stock_cable_ends()
        moved = {k: on_new_bar(p, old_r if side == "r" else old_l, new_r if side == "r" else new_l, fe)
                 for k, (p, side, fe, _) in ends.items()}
        cname = "opt_cables_" + pid
        cables = {"id": "cables", "group": "steer", "meshes": [cname], "mount": {"to": "yokes.lugs", "type": "bracket"},
                  "sockets": {}, "proxies": [],
                  "conform": {"ends": [{"unit": unit_id, "at": v(p), "to": v(moved[k])} for k, (p, _, _, unit_id) in ends.items()]}}
        meshes = {name: (mesh, fin, "steer"),
                  cname: (st.cable_lines(moved["clutch"], moved["brake"], moved["throttle"]), "rubber", "steer")}
        # Mirrors: the stock ones are on the bar ends, which these bars sweep back towards the rider, so
        # the option brings the same mirrors on the lever clamps, inboard of the switches. Modelled on the
        # stock bar at that distance from its end, so they ride the new bar there. (Stock is unchanged.)
        units = [u, cables]
        # Whatever mirrors are fitted (stock or the mirrors slot's) go on the lever clamps of these
        # bars, inboard of the switches, not on their swept-back ends: modelled on the stock bar ends,
        # so their mount frame is the stock bar's frame there, at the stock mirror's base.
        moves = {}
        for sd, sgn, half in (("l", -1, old_l), ("r", 1, old_r)):
            R0 = path_frame(half, path_length(half) - MIRROR_FROM_END)[0]
            base = BAR_CLAMP + [0.0, 0.0, sgn * 0.385]
            moves["mirror_" + sd] = {"from_end": MIRROR_FROM_END, "child": {"at": v(base), "x": v(R0[:, 0]), "y": v(R0[:, 1])}}
        out.append(("handlebars", pid, e["name"], ["handlebars", pid], units, meshes, moves))
    # Silencers: a long slim peashooter, and a short reverse cone megaphone; same slip joint.
    for pid in ("peashooter_twins", "cafe_reverse_cones"):
        e = cat["exhaust"][pid]
        # The fat cones need the header's tail kicked out to clear the frame, swingarm and side stand
        # (the header is routed to the silencer, plan point 3); the slim peashooters fit the stock one.
        dz = 0.05 if pid == "cafe_reverse_cones" else 0.0
        a0 = np.array([-0.25, -0.065, 0.158 + dz])
        if pid == "peashooter_twins":
            # Long, low and slim, in chrome, past the rear axle, tapering to a small tail pipe: the
            # stock cans are fatter (100 mm), shorter and kick up more.
            end = np.array([-1.02, 0.05, 0.17])
            s_path = np.linspace(a0, end, 24)
            radii = np.interp(np.linspace(0, 1, 24), [0.0, 0.06, 0.16, 0.74, 0.97, 1.0],
                              [0.024, 0.03, 0.035, 0.035, 0.016, 0.014])
            fin, cap_fin = "chrome", "chrome"
        else:
            end = np.array([-0.70, 0.12, 0.17 + dz])
            s_path = np.linspace(a0, end, 16)
            radii = np.interp(np.linspace(0, 1, 16), [0.0, 0.15, 0.25, 0.85, 1.0], [0.024, 0.03, 0.062, 0.042, 0.036])
            fin, cap_fin = "steel_brushed", "black_satin"
        sil = st.sweep(s_path, radii, radii, 2.0, 32, caps=False)
        cap = st.lathe([(radii[-1], 0.0), (radii[-1] + 0.002, 0.02), (radii[-1] * 0.7, 0.028), (0.0, 0.03)], seg=32,
                       axis=s_path[-1] - s_path[-2], centre=s_path[-1])
        n1, n2 = "opt_exhaust_" + pid, "opt_exhaust_" + pid + "_caps"
        u = {"id": "silencers", "group": "body", "meshes": [n1, n2], "slot": "exhaust", "catalogue": ["exhaust", pid]}
        u.update(silencer_unit(s_path, radii, 48.0))
        units = [u]
        meshes = {n1: (st.both_sides(sil), fin, "body"), n2: (st.both_sides(cap), cap_fin, "body")}
        if dz:
            hname = "opt_headers_" + pid
            hu, hmesh = headers_unit(dz)
            hu["meshes"] = [hname]
            # Modelled on its own headers, so its mount frame is their outlet, not the stock one.
            o = hu["sockets"]["outlet"]
            u["mount"]["child"] = {"at": o["at"], "x": o["x"], "y": o["y"]}
            units.insert(0, hu)
            meshes[hname] = (hmesh, "header", "body")
        out.append(("exhaust", pid, e["name"], ["exhaust", pid], units, meshes))
    return out


def write_option_glbs(meshes):
    os.makedirs(OPT_DIR, exist_ok=True)
    keep = set(meshes)
    for f in os.listdir(OPT_DIR):
        if f.endswith(".glb") and f[:-4] not in keep:
            os.remove(os.path.join(OPT_DIR, f))
    for name, (mesh, fin, group) in sorted(meshes.items()):
        mesh = st.outward(mesh)
        pos, tris, uv, mask = mesh.arrays()
        col = np.zeros((len(pos), 4), np.uint8)
        col[:, 0] = np.round(np.clip(mask, 0, 1) * 255)
        col[:, 3] = 255
        grime = np.clip((0.28 - pos[:, 1]) / 0.38, 0, 1) ** 1.3
        st.write_glb(os.path.join(OPT_DIR, name + ".glb"), pos, tris, st.normals(mesh), col, name, uv=uv,
                     uv1=np.c_[np.clip(mask, 0, 1), grime])


def main():
    P = st.build()
    cat = json.load(open(os.path.join(REPO, "Data", "Parts", "parts.json")))
    units = stock_units(P)
    used = [m for u in units for m in u["meshes"]]
    missing = sorted(set(P) - set(used))
    twice = sorted({m for m in used if used.count(m) > 1})
    if missing or twice:
        sys.exit("model parts not in exactly one unit: missing %s, twice %s" % (missing, twice))
    opts = {"handlebars": [{"id": "stock", "name": "Speed Twin bars (stock)", "stock": True, "units": []}],
            "exhaust": [{"id": "stock", "name": "Speed Twin silencers (stock)", "stock": True, "units": []}]}
    meshes = {}
    stock_names = {"saddle": "Speed Twin bench seat (stock)", "front_mudguard": "Speed Twin front mudguard (stock)",
                   "tail": "Speed Twin tail and plate hanger (stock)"}
    for slot, pid, name, ce, u, ms, *rest in options(cat) + pack.pack_options(sys.modules[__name__], cat):
        if slot not in opts:
            opts[slot] = [{"id": "stock", "name": stock_names.get(slot, "Stock"), "stock": True, "units": []}]
        o = {"id": pid, "name": name, "catalogue": ce, "units": u,
             "meshes": {k: {"finish": f, "group": g, "file": "Options/%s.glb" % k} for k, (_, f, g) in ms.items()}}
        if rest and rest[0]:
            o["moves"] = rest[0]
        opts[slot].append(o)
        meshes.update(ms)
    write_option_glbs(meshes)
    data = {
        "bike": "speed_twin_1200",
        "name": "2025 Triumph Speed Twin 1200",
        "frame": "metres, x forward, y up, z right; body origin at axle height (suspension extended), mid wheelbase (as speedtwin.json)",
        "made_by": "Tools/make_speedtwin_fit.py",
        "datums": {
            "wheelbase": st.WB, "rake_deg": round(math.degrees(st.RAKE), 4), "fork_offset": st.OFFSET,
            "front_axle": v(FA), "rear_axle": v(RA), "steer_point": v(STEER_POINT), "steer_dir": v(FORK_DIR),
            "swing_pivot": v(SWING_PIVOT), "front_radius": 0.300, "rear_radius": 0.312,
            "front_travel": 0.120, "rear_travel": 0.120, "steer_lock_deg": STEER_LOCK_DEG,
        },
        "group_origins": {k: v(o) for k, o in GROUP_ORIGIN.items()},
        "clearance": {
            "tolerance_mm": 1.0,
            "cases": [
                {"name": "rest"},
                {"name": "lock_left", "steer_deg": -STEER_LOCK_DEG}, {"name": "lock_right", "steer_deg": STEER_LOCK_DEG},
                {"name": "front_bump", "front": 1.0},
                {"name": "front_bump_lock_left", "front": 1.0, "steer_deg": -STEER_LOCK_DEG},
                {"name": "front_bump_lock_right", "front": 1.0, "steer_deg": STEER_LOCK_DEG},
                {"name": "rear_bump", "rear": 1.0},
                {"name": "both_bump_lock_left", "front": 1.0, "rear": 1.0, "steer_deg": -STEER_LOCK_DEG},
                {"name": "both_bump_lock_right", "front": 1.0, "rear": 1.0, "steer_deg": STEER_LOCK_DEG},
            ],
            # Pairs that touch by design (beyond a part and the part it mounts on).
            "touch": [["frame", "yokes"], ["frame", "engine"], ["frame", "tank"], ["frame", "seat"], ["frame", "swingarm"],
                      ["yokes", "fork_lowers"], ["fork_lowers", "front_wheel"], ["swingarm", "rear_wheel"],
                      ["rear_wheel", "rear_caliper"], ["engine", "headers"], ["headers", "silencers"]],
        },
        # Every stock model part, with the group it was modelled in (its vertices are relative to that
        # group's origin) and its finish, as speedtwin.json has them.
        "meshes": {m: {"group": P[m][2], "finish": P[m][1]} for m in sorted(P)},
        "units": units,
        "options": opts,
        # The part pack's own finishes (Tools/speedtwin_pack.py); the stock ones are in speedtwin.json.
        "finishes": pack.FINISHES,
    }
    with open(os.path.join(OUT, "fitment.json"), "w") as f:
        json.dump(data, f, indent=1)
    n_prox = sum(len(u["proxies"]) for u in units)
    print("%d units, %d proxies, %d option meshes" % (len(units), n_prox, len(meshes)))


if __name__ == "__main__":
    main()
