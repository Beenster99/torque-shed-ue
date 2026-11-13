"""The things that stand by the road on a Ride UK ride (OSM stage 2): Data/RideUK/Props/*.glb, made once
here and kept in the repo, which Tools/ue_build_rideuk.py (prop_kit) imports to /Game/RideUK/Props and
lists in the ride's prop kit (a TorqueFoliage actor tagged RideProps) for World/TorqueRideWorld to place.
Where a Fab model matching a kind's words in Data/RideUK/prop_kit.json has been imported, that is used
instead; these are the ones nobody sells: British field gates, stiles and kissing gates, the bus stop
flag, the white bollard, the milestone, and plain ones for the rest.

Each is named SM_Prop_<kind>_<n>, kind as World/WorldRideProps.cpp's RidePlan::PropName gives it. All
have their front to +x and their foot at y = 0, life size (the game scales each to the kit's height, so
keep a kind's 'height' in prop_kit.json what is made here), vertex-coloured (RGB the paint, A dirt).
The things that stand in a wall or fence (gates, stiles, kissing gates) run along z, centred on the
gap the plan leaves for them: 3.6 m for a field gate, 1.5 m for a kissing gate, 1.0 m for a stile.
Street lamps are left to the code (their heads carry the lights).

    python Tools/make_ride_props.py
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import buttertubs_stone as ST            # noqa: E402
from export_buttertubs import write_glb  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "RideUK", "Props")

GALV = (0.42, 0.43, 0.42)
TIMBER = (0.19, 0.15, 0.1)          # weathered softwood, linear
OAK = (0.13, 0.085, 0.05)
WHITE = (0.7, 0.7, 0.68)
BLACK = (0.015, 0.015, 0.015)
STONE = (0.32, 0.3, 0.26)


def paint(M, P, T, U, rgb, dirt=0.2):
    ST._paint(M, P, T, U, rgb, dirt)


def block(M, c, h, rgb, dirt=0.2, R=None):
    P, T, U = ST.obox(c, h, R)
    paint(M, P, T, U, rgb, dirt)


def rod(M, a, b, r, rgb, dirt=0.2, sides=6):
    P, T = ST.tube([a, b], r, sides)
    paint(M, P, T, P[:, [0, 1]] + P[:, [2, 2]], rgb, dirt)


def toned(rng, rgb, spread=0.1):
    return tuple(np.clip(np.array(rgb) * rng.uniform(1 - spread, 1 + spread), 0, 1))


def turned(M):
    """A model made facing +z, turned to face +x."""
    for k in range(len(M.P)):
        P = M.P[k]
        M.P[k] = np.stack([P[:, 2], P[:, 1], -P[:, 0]], 1)
    return M


def merged(parts):
    """The parts of a helper model (wood, metal, glass) as one mesh, vertex colours kept."""
    out = ST.Mesh()
    for M in parts.values():
        for P, T, C, U in zip(M.P, M.T, M.C, M.U):
            base = T.min() if len(T) else 0
            out.add(P, T - base, C, U)
    return out


# --- In the line: the line runs along z, the front (+x) is across it ----------------------------------

def field_gate(rng, wooden):
    """A five-bar field gate, 3.35 m, between two posts 3.6 m apart: galvanised tube, or a timber gate
    with its diagonal brace, on timber or stone posts. 1.35 m to the post tops."""
    M = ST.Mesh()
    half = 1.8
    post = toned(rng, STONE if rng.random() < 0.4 else OAK)
    for z in (-half - 0.1, half + 0.1):
        block(M, (0, 0.6, z), (0.1, 0.75, 0.1), post, 0.5)
    y0, y1 = 0.12, 1.15
    z0, z1 = -half + 0.04, half - 0.06
    if wooden:
        wood = toned(rng, TIMBER, 0.15)
        for z, hh in ((z0 + 0.05, y1 + 0.08), (z1 - 0.04, y1)):
            block(M, (0, (y0 - 0.04 + hh) / 2, z), (0.035, (hh - y0 + 0.04) / 2, 0.05), wood, 0.4)
        for k in range(5):
            y = y0 + (y1 - y0) * (k / 4) ** 1.2
            block(M, (0, y, (z0 + z1) / 2), (0.022, 0.05 if k == 4 else 0.04, (z1 - z0) / 2), wood, 0.35)
        L = math.hypot(z1 - z0 - 0.2, y1 - y0)
        ang = math.atan2(y1 - y0, z1 - z0 - 0.2)
        block(M, (0.03, (y0 + y1) / 2, (z0 + z1) / 2), (0.02, 0.04, L / 2), wood, 0.35, ST.rot_xyz(-ang, 0, 0))
    else:
        grey = toned(rng, GALV, 0.08)
        rod(M, (0, y0 - 0.05, z0), (0, y1 + 0.08, z0), 0.03, grey, 0.3)
        rod(M, (0, y0 - 0.02, z1), (0, y1 + 0.02, z1), 0.025, grey, 0.3)
        for k in range(5):
            y = y0 + (y1 - y0) * (k / 4) ** 1.25
            rod(M, (0, y, z0), (0, y, z1), 0.022 if k == 4 else 0.018, grey, 0.25)
        rod(M, (0, y0, z0 + 0.02), (0, y1, z0 + (z1 - z0) * 0.62), 0.016, grey, 0.3)
        rod(M, (0, y0, (z0 + z1) / 2), (0, y1, (z0 + z1) / 2), 0.014, grey, 0.3)
    # The hinges and the latch.
    for y in (0.3, 1.0):
        block(M, (0, y, z0 - 0.08), (0.03, 0.02, 0.08), BLACK, 0.4)
    block(M, (0, 1.0, z1 + 0.08), (0.03, 0.03, 0.09), BLACK, 0.4)
    return M


def step_stile(rng):
    """A timber step stile in a fence: two posts 0.9 m apart, a top rail, and a step each side."""
    M = ST.Mesh()
    wood = toned(rng, TIMBER, 0.15)
    for z in (-0.45, 0.45):
        block(M, (0, 0.6, z), (0.05, 0.6, 0.05), wood, 0.5)
    block(M, (0, 0.98, 0), (0.035, 0.04, 0.52), wood, 0.3)
    block(M, (0, 0.62, 0), (0.025, 0.04, 0.48), wood, 0.3)
    for x in (-0.32, 0.32):
        block(M, (x, 0.4, 0), (0.12, 0.025, 0.4), toned(rng, wood, 0.08), 0.6)
        for z in (-0.3, 0.3):
            block(M, (x, 0.2, z), (0.04, 0.2, 0.04), wood, 0.7)
    return M


def squeeze_stile(rng):
    """A Dales squeeze stile: two upright stones in the wall with a slot between them, narrower at
    the foot, and a through-stone step."""
    M = ST.Mesh()
    for s in (-1, 1):
        P, T, uv = ST.stone(rng, (0.12, 0.6, 0.32), (0, 0.55, s * 0.42), ST.rot_xyz(0, 0, s * 0.06),
                            res=(6, 9), round_=(0.12, 0.18), chops=3, chop_depth=(0.85, 0.97))
        col = ST.stone_col(rng, len(P), lichen=0.5)
        col[:, 3] = np.clip(0.4 - P[:, 1], 0, 1)
        M.add(P, T, col, uv)
    P, T, uv = ST.stone(rng, (0.35, 0.06, 0.2), (0, 0.22, 0), None, res=(5, 8), round_=(0.1, 0.15), chops=2, chop_depth=(0.9, 0.98))
    col = ST.stone_col(rng, len(P), lichen=0.3)
    M.add(P, T, col, uv)
    return M


def kissing_gate(rng):
    """A kissing gate: two posts 1.5 m apart in the fence, a pen of rails curving out to the front, and
    the gate hung in it, 1.3 m to the post tops."""
    M = ST.Mesh()
    wood = toned(rng, TIMBER, 0.15)
    for z in (-0.75, 0.75):
        block(M, (0, 0.6, z), (0.07, 0.7, 0.07), wood, 0.5)
    # The pen: a half-round of posts and two rails out on the +x side.
    pts = [(0.75 * math.sin(a), 0.75 * math.cos(a)) for a in np.linspace(0.25, math.pi - 0.25, 5)]
    for x, z in pts[1:-1]:
        block(M, (x, 0.55, z), (0.045, 0.6, 0.045), wood, 0.5)
    for y in (0.5, 1.05):
        for (xa, za), (xb, zb) in zip([(0, 0.75)] + pts, pts + [(0, -0.75)]):
            L = math.hypot(xb - xa, zb - za)
            ang = math.atan2(xb - xa, zb - za)
            block(M, ((xa + xb) / 2, y, (za + zb) / 2), (0.02, 0.045, L / 2), wood, 0.35, ST.rot_xyz(0, ang, 0))
    # The gate, swung part open inside the pen.
    R = ST.rot_xyz(0, 0.5, 0)
    hinge = np.array([0.0, 0.0, -0.68])
    for y in (0.3, 0.7, 1.08):
        block(M, hinge + np.array([0, y, 0.35]) @ R.T, (0.02, 0.04, 0.36), wood, 0.3, R)
    for dz in (0.03, 0.68):
        block(M, hinge + np.array([0, 0.68, dz]) @ R.T, (0.025, 0.42, 0.03), wood, 0.4, R)
    return M


# --- By the road: front (+x) to the road --------------------------------------------------------------

def lift_gate(rng):
    """A barrier gate at a track's mouth, swung open along the verge: a post, the arm resting on a
    second post, painted red and white."""
    M = ST.Mesh()
    block(M, (0, 0.5, 0), (0.08, 0.55, 0.08), toned(rng, GALV), 0.4)
    block(M, (0, 0.45, 3.6), (0.06, 0.5, 0.06), toned(rng, GALV), 0.4)
    for k in range(6):
        z0, z1 = 0.1 + k * 0.6, 0.1 + (k + 1) * 0.6
        rod(M, (0, 1.0, z0), (0, 1.0, z1), 0.04, (0.42, 0.01, 0.008) if k % 2 == 0 else WHITE, 0.2, 8)
    block(M, (0, 1.0, -0.25), (0.05, 0.05, 0.18), BLACK, 0.3)   # the counterweight
    return M


def bus_stop(rng):
    """A bus stop: a grey pole, and the flag square to the road (so it is read along it) with BUS on
    both faces."""
    M = ST.Mesh()
    rod(M, (0, 0, 0), (0, 2.75, 0), 0.04, toned(rng, (0.25, 0.26, 0.26)), 0.3, 8)
    c = np.array([0.32, 2.4, 0.0])
    block(M, c, (0.27, 0.22, 0.012), WHITE, 0.1)
    block(M, c + [0, 0.17, 0], (0.27, 0.05, 0.014), (0.02, 0.06, 0.25), 0.1)   # the blue band
    for s in (1, -1):
        ST.lettering(M, "BUS", c + [0, -0.05, s * 0.014], (s * -1.0, 0, 0), (0, 1, 0), (0, 0, s), 0.13, BLACK)
    block(M, (0.06, 1.5, 0), (0.03, 0.12, 0.09), (0.6, 0.6, 0.58), 0.2)   # the timetable case
    return M


def bollard(rng):
    """A white bollard by the road, with a black band and a red reflector facing each way along it."""
    M = ST.Mesh()
    block(M, (0, 0.45, 0), (0.075, 0.5, 0.075), WHITE, 0.35)
    block(M, (0, 0.82, 0), (0.078, 0.06, 0.078), BLACK, 0.2)
    P, T = ST.tube([[0, 0.95, 0], [0, 1.0, 0]], [0.075, 0.02], 8); paint(M, P, T, P[:, [0, 1]], WHITE, 0.3)
    for s in (1, -1):
        block(M, (0, 0.7, s * 0.077), (0.035, 0.05, 0.004), (0.5, 0.02, 0.01), 0.05)
    return M


def milestone(rng):
    """A milestone: a squared stone with a rounded top and two faces angled to the road, 0.8 m."""
    M = ST.Mesh()
    P, T, uv = ST.stone(rng, (0.17, 0.45, 0.22), (0, 0.38, 0), None, res=(7, 10), round_=(0.08, 0.14), chops=2, chop_depth=(0.9, 0.98))
    P[:, 1] = np.minimum(P[:, 1], 0.8 - 0.25 * (P[:, 2] / 0.22) ** 2)
    col = ST.stone_col(rng, len(P), lichen=0.5, bright=rng.uniform(0.85, 1.1))
    col[:, 3] = np.clip(0.35 - P[:, 1], 0, 1)
    if rng.random() < 0.5:   # painted white, as many are, the faces black-lettered (worn to nothing here)
        col[:, :3] = col[:, :3] * 0.35 + np.array(WHITE) * 0.65
    M.add(P, T, col, uv)
    return M


def info_board(rng):
    """An information board on two oak legs, its panel sloping back from the road, under a little roof."""
    M = ST.Mesh()
    oak = toned(rng, OAK, 0.15)
    for z in (-0.55, 0.55):
        block(M, (0, 0.62, z), (0.05, 0.62, 0.05), oak, 0.5)
    R = ST.rot_xyz(0, 0, 0.45)
    block(M, (0.0, 1.0, 0), (0.025, 0.32, 0.6), oak, 0.3, R)
    block(M, (0.03, 1.0 + 0.03 * math.tan(0.45), 0), (0.012, 0.28, 0.55), toned(rng, (0.12, 0.2, 0.12), 0.2), 0.05, R)   # the panel (a map, in green)
    block(M, (-0.05, 1.27, 0), (0.12, 0.02, 0.66), oak, 0.3, ST.rot_xyz(0, 0, 0.2))
    return M


def picnic_table(rng):
    """A timber picnic table along the road, a bench each side joined to it by A-frame legs."""
    M = ST.Mesh()
    wood = toned(rng, TIMBER, 0.12)
    for k in range(4):
        block(M, (-0.27 + k * 0.18, 0.74, 0), (0.08, 0.02, 0.9), toned(rng, wood, 0.06), 0.3)
    for x in (-0.62, 0.62):
        block(M, (x, 0.44, 0), (0.12, 0.02, 0.9), toned(rng, wood, 0.06), 0.3)
    for z in (-0.7, 0.7):
        block(M, (0, 0.42, z), (0.75, 0.03, 0.03), wood, 0.4)
        for s in (-1, 1):
            ang = s * 0.42
            block(M, (s * 0.2, 0.37, z), (0.035, 0.4, 0.035), wood, 0.5, ST.rot_xyz(0, 0, ang))
    return M


def seat(rng):
    M = merged(ST.bench(rng))
    return turned(M)


def post_box(rng):
    return merged(ST.post_box(rng))


def phone_box(rng):
    return merged(ST.phone_box(rng))


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".glb"):
            os.remove(os.path.join(OUT, f))
    R = np.random.default_rng
    made = {
        "field_gate": [field_gate(R(900), False), field_gate(R(901), False), field_gate(R(902), True)],
        "stile": [step_stile(R(910)), step_stile(R(911)), squeeze_stile(R(912))],
        "kissing_gate": [kissing_gate(R(920)), kissing_gate(R(921))],
        "lift_gate": [lift_gate(R(930))],
        "bus_stop": [bus_stop(R(940))],
        "bollard": [bollard(R(950))],
        "milestone": [milestone(R(960)), milestone(R(961))],
        "info_board": [info_board(R(970))],
        "picnic_table": [picnic_table(R(980)), picnic_table(R(981))],
        "seat": [seat(R(990)), seat(R(991))],
        "post_box": [post_box(R(1000))],
        "phone_box": [phone_box(R(1010))],
    }
    total = 0
    for kind, meshes in made.items():
        for n, M in enumerate(meshes):
            name = "SM_Prop_%s_%d" % (kind, n)
            P = np.concatenate(M.P)
            nv, nt = M.write(os.path.join(OUT, name + ".glb"), write_glb, name)
            total += nt
            print("%s: %d triangles, %.2f m tall, x %.2f..%.2f, z %.2f..%.2f" % (
                name, nt, P[:, 1].max(), P[:, 0].min(), P[:, 0].max(), P[:, 2].min(), P[:, 2].max()))
    print("%d meshes, %d triangles in %s" % (sum(len(v) for v in made.values()), total, OUT))


if __name__ == "__main__":
    main()
