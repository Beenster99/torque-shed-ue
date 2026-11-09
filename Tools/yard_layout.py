"""Torque Shed's yard test area (world_yard.gd) laid out on the UE riding pad, written to
Data/Furniture/yard.json for the game (Source/TorqueShedProto/Shed/TorqueFurniture):

  start   the yard kit a new shed starts with (WorldYard.starting_kit): movable, pushed about with F;
  fixed   the pieces built into the yard (the seven jump lines' ramps, tabletops, landings, the old car
          and the row of buses, the whoops, the speed humps, the braking-zone boards, the bunting):
          drawn the same way but they stay put;
  paint   the markings, as flat rectangles [x, y, length, width, yaw, r, g, b] (cm, degrees, linear);
  words   painted words [text, x, y, height cm, yaw, r, g, b];
  drop    the yard kit drop-off box [x0, y0, x1, y1] (cm): ordered yard kit is left here.

    python Tools/yard_layout.py

Torque Shed's yard is behind its shed, laid along x with the shed to its south (+z). Here it's out in
front of the roller door, turned half round so the jump park is still the far side from the shed, and
east of the door (x 20 .. 280 m), so the straight run out of the door for top speed stays clear
(Tools/shed_yard.py: the pad is x -300 .. 300 m, from the shed's front at y 5.8 m out to 1.6 km).
A Torque Shed point (x, z) metres is UE (OX - 100 x, OY - 100 z) cm, and a heading (Godot yaw a about
up, radians) is UE yaw 180 - a in degrees: a piece's front (+z in its own frame, UE +y at yaw 0)
points the same way on the ground in both.
"""
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Data", "Furniture", "yard.json")
MODELS = json.load(open(os.path.join(ROOT, "Data", "Furniture", "models.json")))["models"]

OX = 15000.0            # Torque Shed x 0 -> 150 m east of the door
OY = 580.0 + 4000.0 + 3500.0   # its z 35 (the yard's south edge) -> 40 m out from the shed's front

PI = math.pi


def P(x, z):
    return [round(OX - 100.0 * x, 1), round(OY - 100.0 * z, 1)]


def Y(a):
    """Godot yaw (radians) -> UE yaw (degrees)."""
    return round((180.0 - math.degrees(a)) % 360.0, 2)


# --- world_yard.gd ---------------------------------------------------------------------------------------
START_X, BRAKE_X, BRAKE_END, RUN_END = -58.0, 0.0, 40.0, 62.0
LANE_Z = (-14.0, -6.0)
SKID_PAD = (50.0, -36.0)
SKID_RADIUS = 11.0
SLALOM_Z = 2.0
SLALOM_X = [-24.0, -18.0, -12.0, -6.0, 0.0, 6.0]
JUMP_Z = 13.0
HUMPS_X = [-56.0, -50.0, -44.0, -38.0, -32.0]
HUMPS_Z = -19.0
UTURN = (-62.0, -31.0, 38.0, 7.0)
FIGURE_EIGHT = [(-52.0, -44.0), (-38.0, -44.0)]
FIGURE_EIGHT_R = 6.0
DROP_OFF = (-62.0, -5.0, 32.0, 16.0)
WINGS = [(-128.0, -53.0, 61.0, 86.0), (66.0, -53.0, 62.0, 86.0)]
LINES = [
    {"name": "FIRST AIR", "at": (-76.0, 31.0), "yaw": 0.0, "pieces": [
        ["ramp_small", 12.0, ""], ["ramp_medium", 33.0, "", 42], ["ramp_landing", 42.1, "back"],
        ["tabletop", 58.0, ""], ["ramp_kicker", 72.0, ""]]},
    {"name": "DOUBLES", "at": (-91.0, 31.0), "yaw": 0.0, "pieces": [
        ["ramp_medium", 10.0, "", 42], ["ramp_landing", 19.1, "back"],
        ["ramp_large", 34.0, "", 45], ["ramp_landing", 47.9, "back"], ["tabletop", 67.0, ""]]},
    {"name": "BIG AIR", "at": (-106.0, 31.0), "yaw": 0.0, "pieces": [
        ["ramp_xl", 14.0, "", 50], ["car_wreck", 22.0, "across"], ["landing_xl", 32.25, "back"],
        ["ramp_large", 52.0, "", 45], ["ramp_landing", 65.9, "back"]]},
    {"name": "WHOOPS", "at": (-121.0, 31.0), "yaw": 0.0, "pieces": [
        ["tabletop", 58.0, ""], ["ramp_kicker", 74.0, ""]]},
    {"name": "BUS JUMP", "at": (30.0, 19.0), "yaw": -PI * 0.5, "pieces": [
        ["ramp_huge", 56.0, "", 60], ["bus_row", 67.75, ""], ["landing_huge", 80.5, "back"]]},
    {"name": "RHYTHM", "at": (86.0, 8.0), "yaw": 0.0, "pieces": [
        ["tabletop", 8.0, ""], ["tabletop", 20.0, ""], ["tabletop", 32.0, ""], ["ramp_kicker", 46.0, ""]]},
    {"name": "KICKERS", "at": (110.0, 8.0), "yaw": 0.0, "pieces": [
        ["ramp_kicker", 6.0, ""], ["ramp_small", 22.0, ""], ["ramp_kicker", 38.0, ""], ["ramp_medium", 52.0, ""]]},
]
WHOOPS_LINE, WHOOPS_FROM, WHOOPS_COUNT, WHOOPS_PITCH, WHOOPS_LENGTH, WHOOPS_WIDTH = 3, 8.0, 9, 4.0, 3.2, 4.0

WHITE = (0.93, 0.93, 0.9)
YELLOW = (0.95, 0.8, 0.15)
RED = (0.8, 0.15, 0.1)
AMBER = (0.95, 0.55, 0.1)
GREEN = (0.2, 0.6, 0.25)
BLUE = (0.2, 0.4, 0.8)


def rot(a, v):
    """Godot Basis(UP, a) * (x, z)."""
    x, z = v
    return (x * math.cos(a) + z * math.sin(a), -x * math.sin(a) + z * math.cos(a))


def line_point(line, along, side=0.0):
    dx, dz = rot(line["yaw"], (side, -along))
    return (line["at"][0] + dx, line["at"][1] + dz)


def size(model):
    return MODELS[model]["size"]


paint, words, start, fixed = [], [], [], []


def rect(x, z, sx, sz, c, angle=0.0):
    paint.append(P(x, z) + [round(sx * 100.0, 1), round(sz * 100.0, 1), Y(angle)] + [round(v, 3) for v in c])


def ring(cx, cz, r, w, c, dashes=0):
    n = max(24, int(2 * PI * r / 1.2))
    if dashes:
        n = dashes * 2
    for i in range(n):
        if dashes and i % 2:
            continue
        a = 2 * PI * (i + 0.5) / n
        x, z = cx + math.cos(a) * r, cz + math.sin(a) * r
        rect(x, z, w, 2 * PI * r / n * (0.98 if not dashes else 1.0), c, -a)


def spot(x, z, r, c):
    rect(x, z, r * 1.6, r * 1.6, c, PI * 0.25)


def outline(rx, rz, w, h, lw, c):
    rect(rx + w * 0.5, rz, w, lw, c)
    rect(rx + w * 0.5, rz + h, w, lw, c)
    rect(rx, rz + h * 0.5, lw, h, c)
    rect(rx + w, rz + h * 0.5, lw, h, c)


def word(text, x, z, px, c, angle=0.0):
    # Torque Shed's Label3D font size (px at 0.01 m a pixel, about 0.7 of it capital height).
    words.append([text] + P(x, z) + [round(px * 0.7, 1), Y(angle)] + [round(v, 3) for v in c])


def piece(model, x, z, a, into):
    into.append({"model": model, "at": P(x, z), "yaw": Y(a)})


# --- The markings ----------------------------------------------------------------------------------------
def markings():
    mid = (LANE_Z[0] + LANE_Z[1]) * 0.5
    for z in LANE_Z:
        rect((START_X + RUN_END) * 0.5, z, RUN_END - START_X, 0.18, WHITE)
    x = START_X
    while x < BRAKE_X:
        rect(x + 1.5, mid, 3.0, 0.15, YELLOW)
        x += 6.0
    rect(START_X, mid, 0.5, LANE_Z[1] - LANE_Z[0], WHITE)
    for z in (LANE_Z[0] + 2.0, LANE_Z[1] - 2.0):
        word("START", START_X + 3.5, z, 160, WHITE, -PI * 0.5)
    for d in range(10, int(BRAKE_X - START_X), 10):
        rect(START_X + d, LANE_Z[0] - 0.6, 0.3, 1.2, WHITE)
        rect(START_X + d, LANE_Z[1] + 0.6, 0.3, 1.2, WHITE)
        word("%d m" % d, START_X + d, LANE_Z[0] - 2.0, 90, WHITE, -PI * 0.5)
    for d in range(0, int(BRAKE_END - BRAKE_X), 5):
        band = GREEN if d < 15 else (AMBER if d < 25 else RED)
        rect(BRAKE_X + d + 2.5, LANE_Z[0] - 0.9, 5.0, 1.2, band)
        rect(BRAKE_X + d + 2.5, LANE_Z[1] + 0.9, 5.0, 1.2, band)
        if d > 0:
            rect(BRAKE_X + d, mid, 0.14, LANE_Z[1] - LANE_Z[0], WHITE)
            word("%d" % d, BRAKE_X + d + 0.8, mid - 2.0, 110, WHITE, -PI * 0.5)
    word("BRAKE", BRAKE_X - 3.0, mid - 2.0, 150, WHITE, -PI * 0.5)
    word("BRAKE", BRAKE_X - 3.0, mid + 2.0, 150, WHITE, -PI * 0.5)
    h = BRAKE_END + 1.0
    while h < RUN_END - 1.0:
        rect(h, mid, 0.4, LANE_Z[1] - LANE_Z[0] - 1.0, (0.85, 0.85, 0.8), 0.6)
        h += 2.5
    rect(BRAKE_END, mid, 0.3, LANE_Z[1] - LANE_Z[0], WHITE)
    for i in range(16):
        for j in range(2):
            rect(BRAKE_X - 0.25 + j * 0.5, LANE_Z[0] + 0.25 + i * 0.5, 0.5, 0.5,
                 (0.05, 0.05, 0.05) if (i + j) % 2 == 0 else (0.95, 0.95, 0.95))
    # The skid pad.
    ring(SKID_PAD[0], SKID_PAD[1], SKID_RADIUS, 0.3, YELLOW)
    ring(SKID_PAD[0], SKID_PAD[1], SKID_RADIUS - 3.0, 0.15, WHITE, 24)
    ring(SKID_PAD[0], SKID_PAD[1], SKID_RADIUS + 3.0, 0.15, WHITE, 24)
    for i in range(8):
        a = 2 * PI * i / 8
        spot(SKID_PAD[0] + math.cos(a) * (SKID_RADIUS - 1.5), SKID_PAD[1] + math.sin(a) * (SKID_RADIUS - 1.5), 0.3, AMBER)
    word("SKID PAD", SKID_PAD[0], SKID_PAD[1], 200, WHITE)
    # Slalom and the jump box.
    rect(-30.0, SLALOM_Z, 0.3, 6.0, WHITE)
    rect(12.0, SLALOM_Z, 0.3, 6.0, WHITE)
    for x in SLALOM_X:
        spot(x, SLALOM_Z, 0.35, AMBER)
    word("SLALOM", -27.0, SLALOM_Z + 4.4, 140, WHITE, -PI * 0.5)
    outline(-28.0, JUMP_Z - 7.5, 42.0, 11.0, 0.2, YELLOW)
    for z in (JUMP_Z, JUMP_Z - 4.5):
        for k in range(3):
            rect(-34.0 + k * 1.5, z, 1.0, 0.25, WHITE)
    word("JUMPS", -25.5, JUMP_Z + 2.3, 120, YELLOW, -PI * 0.5)
    # The slow-control area.
    outline(*UTURN, 0.2, WHITE)
    word("U-TURN BOX", UTURN[0] + UTURN[2] * 0.5, UTURN[1] + UTURN[3] * 0.5, 140, WHITE)
    for c in FIGURE_EIGHT:
        ring(c[0], c[1], FIGURE_EIGHT_R, 0.18, BLUE)
        spot(c[0], c[1], 0.35, AMBER)
    word("FIGURE OF EIGHT", -45.0, FIGURE_EIGHT[0][1] + 8.0, 110, WHITE)
    outline(HUMPS_X[0] - 6.0, HUMPS_Z - 1.8, HUMPS_X[-1] - HUMPS_X[0] + 12.0, 3.6, 0.15, WHITE)
    word("SPEED HUMPS", HUMPS_X[0] - 2.0, HUMPS_Z + 3.0, 110, WHITE)
    # The jump lines.
    for line in LINES:
        yaw = line["yaw"]
        last = line["pieces"][-1]
        end = last[1] + size(last[0])[2] * 0.5 + 6.0
        s = line_point(line, 0.0)
        rect(s[0], s[1], 5.6, 0.4, WHITE, yaw)
        t = line_point(line, -2.2)
        word(line["name"], t[0], t[1], 150, YELLOW, yaw)
        for side in (-2.8, 2.8):
            d = 1.0
            while d < end:
                p = line_point(line, d + 1.0, side)
                rect(p[0], p[1], 0.15, 2.0, WHITE, yaw)
                d += 4.0
        for pc in line["pieces"]:
            if len(pc) < 4:
                continue
            toe = pc[1] - size(pc[0])[2] * 0.5
            for k in range(3):
                l = line_point(line, toe - 3.0 - k * 1.2, -0.62)
                r = line_point(line, toe - 3.0 - k * 1.2, 0.62)
                rect(l[0], l[1], 1.4, 0.3, AMBER, yaw + 0.55)
                rect(r[0], r[1], 1.4, 0.3, AMBER, yaw - 0.55)
            w = line_point(line, toe - 8.0)
            word("%d km/h" % pc[3], w[0], w[1], 110, WHITE, yaw)
    dirt_len = (WHOOPS_COUNT - 1) * WHOOPS_PITCH + WHOOPS_LENGTH + 2.0
    m = line_point(LINES[WHOOPS_LINE], WHOOPS_FROM + (WHOOPS_COUNT - 1) * WHOOPS_PITCH * 0.5)
    rect(m[0], m[1], WHOOPS_WIDTH + 1.2, dirt_len, (0.3, 0.21, 0.13), LINES[WHOOPS_LINE]["yaw"])
    for w in WINGS:
        outline(*w, 0.25, YELLOW)
    word("JUMP PARK", WINGS[0][0] + WINGS[0][2] * 0.5, WINGS[0][1] + 3.0, 260, YELLOW)
    # The drop-off box.
    x0, z0, w, h = DROP_OFF
    outline(x0, z0, w, h, 0.25, YELLOW)
    x = x0 + 1.0
    while x < x0 + w - 1.0:
        rect(x, z0 + 0.8, 0.3, 1.2, YELLOW, 0.5)
        x += 2.0
    word("YARD KIT DROP-OFF", x0 + w * 0.5, z0 + h - 1.4, 150, YELLOW)


# --- The pieces ------------------------------------------------------------------------------------------
def pieces():
    east, west, north = -PI * 0.5, PI * 0.5, 0.0
    piece("ramp_large", -20.0, JUMP_Z, east, start)
    piece("ramp_landing", -6.0, JUMP_Z, west, start)
    piece("flag_chequered", -1.0, JUMP_Z + 2.4, north, start)
    piece("ramp_small", -22.0, 8.5, east, start)
    piece("ramp_medium", -12.0, 8.5, east, start)
    piece("tabletop", 4.0, 8.5, east, start)
    piece("flag_red", -30.0, SLALOM_Z - 2.2, north, start)
    piece("flag_blue", -30.0, SLALOM_Z + 2.2, north, start)
    for i, x in enumerate(SLALOM_X):
        piece("cone" if i % 3 != 2 else "post", x, SLALOM_Z, north, start)
    piece("barrels", 44.0, 0.0, north, start)
    piece("log", 49.0, 0.0, north, start)
    piece("hay_bales", 54.0, 0.0, north, start)
    piece("tyre_stack", 59.0, 0.0, north, start)
    piece("car_wreck", 58.0, 7.0, PI * 0.5, start)
    piece("wall_concrete", 43.0, 14.0, north, start)
    piece("wall_concrete", 45.2, 14.0, north, start)
    piece("wall_brick", 49.0, 14.0, north, start)
    piece("fence_wood", 53.0, 14.0, north, start)
    piece("fence_mesh", 57.5, 14.0, north, start)
    # The jump lines' pieces, fixed.
    for line in LINES:
        for pc in line["pieces"]:
            turn = PI if pc[2] == "back" else (PI * 0.5 if pc[2] == "across" else 0.0)
            x, z = line_point(line, pc[1])
            piece(pc[0], x, z, line["yaw"] + turn, fixed)
    for k in range(WHOOPS_COUNT):
        x, z = line_point(LINES[WHOOPS_LINE], WHOOPS_FROM + k * WHOOPS_PITCH)
        piece("yard_whoop", x, z, LINES[WHOOPS_LINE]["yaw"], fixed)
    # Speed humps across the hump lane (their length across it: turned a quarter).
    for x in HUMPS_X:
        piece("yard_hump", x, HUMPS_Z, PI * 0.5, fixed)
    # The skid pad's cones stay put.
    for i in range(8):
        a = 2 * PI * i / 8
        piece("cone", SKID_PAD[0] + math.cos(a) * (SKID_RADIUS - 1.5), SKID_PAD[1] + math.sin(a) * (SKID_RADIUS - 1.5), 0.0, fixed)
    # Boards beside the braking zone, read side on from the lane.
    for d in range(0, int(BRAKE_END - BRAKE_X) + 1, 10):
        piece("yard_board_%d" % d, BRAKE_X + d, LANE_Z[0] - 2.6, -PI * 0.5, fixed)
    # Bunting along the start of each wing and either side of the bus jump: 7 m spans, end to end.
    runs = []
    for w in WINGS:
        runs.append(((w[0] + 2.0, w[1] + w[3] + 2.6), (w[0] + w[2] - 2.0, w[1] + w[3] + 2.6)))
    bus = LINES[4]
    for side in (-7.0, 7.0):
        runs.append((line_point(bus, 50.0, side), line_point(bus, 86.0, side)))
    for a, b in runs:
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        n = max(1, int(round(L / 7.0)))
        dx, dz = (b[0] - a[0]) / L, (b[1] - a[1]) / L
        # The model's string runs along its +x: Godot yaw that turns +x onto (dx, dz).
        yaw = math.atan2(-dz, dx)
        for k in range(n):
            t = k * 7.0
            piece("yard_bunting", a[0] + dx * t, a[1] + dz * t, yaw, fixed)


def main():
    markings()
    pieces()
    x0, z0, w, h = DROP_OFF
    a, b = P(x0, z0), P(x0 + w, z0 + h)
    out = {"about": __doc__.strip().split("\n\n")[0], "start": start, "fixed": fixed, "paint": paint, "words": words,
           "drop": [min(a[0], b[0]), min(a[1], b[1]), max(a[0], b[0]), max(a[1], b[1])]}
    with open(OUT, "w") as f:
        json.dump(out, f, indent=0, separators=(",", ":"))
    print("wrote %s: %d to start with, %d fixed, %d painted marks, %d words" % (OUT, len(start), len(fixed), len(paint), len(words)))


if __name__ == "__main__":
    main()
