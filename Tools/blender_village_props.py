"""Ride UK's village set (the everyday kits plan, stage 6): what stands in playgrounds, on pitches and courts and
round a village green, modelled in Blender. Our own models, from general knowledge of British parks and greens: no
maker's names, badges or lettering (the village sign's picture is a made-up country scene; its name board is blank).

Writes Data/RideUK/Props/SM_Prop_<kind>_<n>.glb, each with a distance version made here by hand,
SM_Prop_<kind>_<n>_LOD1.glb (Tools/ue_build_rideuk.py puts it in as the mesh's LOD 1; never reduced by the engine),
and sets each kind's height and reach in Data/RideUK/prop_kit.json. Same material and frame as the street kit
(Tools/blender_street_kit.py: M_RideStreetKit, front +x, foot at 0); all the models of a kind are the same height
(the game scales a kind's models to its height). World/WorldRideVillage.cpp lays them on the map's playgrounds,
pitches, courts and village greens.

  play_swings 0..1      a two-seat swing frame (flat seats, green); the toddlers' cradle seats (red)
  play_slide 0..1       a steel chute off a platform with steps (blue); a yellow plastic chute under a hood
  play_climber 0..1     a two-tower climbing frame, bridge, net and slide: painted steel; timber with felt roofs
  play_springer 0..2    a spring rider: a pony, a scooter, a duck (shapes only)
  play_roundabout 0     a roundabout with its rails
  play_seesaw 0         a seesaw with handles
  play_fence 0..1       a 2.5 m bow-top railing panel and post: green; black (the game strings them round a playground)
  pitch_goal 0          a full-size football goal with its net
  pitch_posts 0         rugby posts with their pads
  tennis_net 0          a tennis net and posts
  court_fence 0         a 3 m panel of a court's tall wire fence and its post
  basket_hoop 0         a basketball post, board and hoop
  notice_board 0        the parish notice board: timber posts, a glazed case, a little roof
  litter_bin 0..1       a park's green bin; a black one with a gold band
  village_sign 0..1     a village sign: a painted picture on a post over a brick plinth; on two posts over a blank board

    python Tools/blender_village_props.py                # the models
    python Tools/blender_village_props.py --render DIR   # and the review sheet's pictures in DIR
"""
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_street_kit as K   # noqa: E402
from blender_street_kit import box, lathe, rod, sweep   # noqa: E402

GREEN = (0.03, 0.13, 0.05)
DARK_GREEN = (0.02, 0.06, 0.03)
RED = (0.36, 0.03, 0.02)
YELLOW = (0.62, 0.42, 0.02)
BLUE = (0.02, 0.08, 0.3)
ORANGE = (0.6, 0.18, 0.02)
BLACK = (0.015, 0.015, 0.016)
RUBBER = (0.02, 0.05, 0.03)
RUBBER_BLACK = (0.018, 0.018, 0.02)
WHITE = (0.78, 0.78, 0.76)
STEEL = (0.5, 0.5, 0.5)
GALV = (0.46, 0.47, 0.46)
TIMBER = (0.2, 0.13, 0.07)
TIMBER_PALE = (0.33, 0.24, 0.14)
FELT = (0.03, 0.04, 0.03)
BRICK = (0.3, 0.11, 0.06)
CREAM = (0.62, 0.58, 0.48)
GOLD = (0.5, 0.35, 0.06)
NET = (0.04, 0.04, 0.04)
GLASS = (0.02, 0.025, 0.03)

# kind: (reach in m, casts a shadow)
KINDS = {"play_swings": (250, True), "play_slide": (250, True), "play_climber": (300, True), "play_springer": (120, False),
         "play_roundabout": (150, True), "play_seesaw": (150, True), "play_fence": (150, False), "pitch_goal": (400, True),
         "pitch_posts": (800, True), "tennis_net": (200, False), "court_fence": (300, False), "basket_hoop": (250, True),
         "notice_board": (150, True), "litter_bin": (100, True), "village_sign": (250, True)}


def M_(name):
    return K.Model(name)


def bar(M, a, b, r, rgb, lod, surface="paint", dirt=0.2):
    """A round bar, fewer sides at a distance."""
    return M.add(rod("bar", a, b, r, 8 if lod == 0 else 4), surface, rgb, dirt)


def mat(M, x, y, sx, sy):
    """The safety surface under a piece: a dark rubber mat."""
    M.add(box("mat", (x, y, 0.015), (sx, sy, 0.03)), "paint", RUBBER, 0.3)


# --- the playground --------------------------------------------------------------------------------------

def swings(name, cradle, lod):
    """A swing frame 3.8 m long (along y), A-frame legs each end, two seats on chains; the seats swing along x."""
    M = M_(name)
    rgb = RED if cradle else GREEN
    H, half = 2.4, 1.9
    for s in (-1, 1):
        y = s * half
        for f in (-1, 1):
            bar(M, (f * 0.95, y, 0.0), (0.0, y, H), 0.045, rgb, lod)
        if lod == 0:
            bar(M, (-0.45, y, 1.2), (0.45, y, 1.2), 0.025, rgb, lod)
    bar(M, (0.0, -half - 0.05, H), (0.0, half + 0.05, H), 0.055, rgb, lod)
    for y in (-0.8, 0.8):
        z = 0.45
        for d in (-0.2, 0.2):
            if lod == 0:
                M.add(box("chain", (0.0, y + d, (H + z) / 2), (0.015, 0.015, H - z)), "steel", STEEL, 0.2)
            else:
                M.add(box("chain", (0.0, y + d, (H + z) / 2), (0.02, 0.02, H - z)), "steel", STEEL, 0.2)
        if cradle:
            M.add(box("cradle", (0.0, y, z + 0.1), (0.36, 0.42, 0.26), bevel=0.04 if lod == 0 else 0.0), "paint", BLACK, 0.2)
        else:
            M.add(box("seat", (0.0, y, z), (0.18, 0.44, 0.04)), "paint", BLACK, 0.2)
        mat(M, 0.0, y, 3.0, 1.2)
    return M


def slide(name, plastic, lod):
    """A slide: a platform 1.5 m up on four posts with rails, steps up from -x, the chute down to +x."""
    M = M_(name)
    frame = GREEN if plastic else BLUE
    chute = YELLOW if plastic else STEEL
    P, top = 1.5, 2.6
    for x in (-0.4, 0.4):
        for y in (-0.4, 0.4):
            bar(M, (x, y, 0.0), (x, y, top if plastic else 2.45), 0.04, frame, lod)
    M.add(box("deck", (0.0, 0.0, P - 0.03), (0.84, 0.84, 0.06)), "paint", frame, 0.3)
    for y in (-0.4, 0.4):
        bar(M, (-0.4, y, P + 0.75), (0.4, y, P + 0.75), 0.025, frame, lod)
    # (the steps)
    for y in (-0.32, 0.32):
        M.add(box("stringer", (-0.4 - 0.45, y, P / 2), (0.06, 0.05, P * 1.25), rot=Matrix.Rotation(math.atan2(0.9, P), 4, "Y")), "paint", frame, 0.3)
        if lod == 0:
            bar(M, (-1.25, y, 0.9), (-0.4, y, P + 0.95), 0.025, frame, lod)
    for k in range(5 if lod == 0 else 3):
        t = (k + 1) / (6 if lod == 0 else 4)
        M.add(box("tread", (-0.4 - 0.9 * (1 - t), 0.0, P * t), (0.18, 0.6, 0.04)), "paint", BLACK if plastic else GALV, 0.3)
    # (the chute: its bed and sides, down and out at the foot)
    pts = [(0.4, P), (0.9, P - 0.2), (2.6, 0.55), (3.3, 0.35), (3.7, 0.33)]
    for (x0, z0), (x1, z1) in zip(pts, pts[1:]):
        L = math.hypot(x1 - x0, z1 - z0)
        a = math.atan2(z1 - z0, x1 - x0)
        R = Matrix.Rotation(-a, 4, "Y")
        M.add(box("bed", ((x0 + x1) / 2, 0.0, (z0 + z1) / 2), (L + 0.02, 0.5, 0.03), rot=R), "steel" if not plastic else "paint", chute, 0.15)
        for s in (-1, 1):
            M.add(box("side", ((x0 + x1) / 2, s * 0.27, (z0 + z1) / 2 + 0.12), (L + 0.02, 0.04, 0.26), rot=R), "steel" if not plastic else "paint", chute, 0.15)
    for x in (2.6, 3.6):
        z = 0.55 if x < 3 else 0.33
        bar(M, (x, 0.0, 0.0), (x, 0.0, z - 0.02), 0.035, frame, lod)
    if plastic:
        # (the hood over the top of the chute)
        n = 6 if lod == 0 else 3
        pts = [(0.4, -0.35, P + 0.2)]
        for k in range(n + 1):
            a = math.pi * k / n
            M.add(box("hood", (0.25, -0.4 * math.cos(a), P + 0.55 + 0.4 * math.sin(a)), (0.3, 0.3, 0.05), rot=Matrix.Rotation(a + math.pi / 2, 4, "X")), "paint", RED, 0.2)
        M.add(box("cap", (0.0, 0.0, top - 0.02), (0.9, 0.9, 0.04)), "paint", RED, 0.2)
    else:
        M.add(box("cap", (0.0, 0.0, top - 0.06), (0.86, 0.86, 0.04)), "paint", frame, 0.2)
        for y in (-0.4, 0.4):
            bar(M, (-0.4, y, 2.45), (-0.4, y, top - 0.06), 0.03, frame, lod)
    mat(M, 1.4, 0.0, 6.2, 2.4)
    return M


def roof(M, x, y, w, ridge, eave, rgb, lod):
    """A small pitched roof over a tower, its ridge along y."""
    for s in (-1, 1):
        a = math.atan2(ridge - eave, w / 2)
        L = math.hypot(w / 2, ridge - eave) + 0.12
        M.add(box("roof", (x + s * w / 4, y, (ridge + eave) / 2), (L, w + 0.2, 0.05), rot=Matrix.Rotation(s * a, 4, "Y")), "paint", rgb, 0.3)
    if lod == 0:
        for s in (-1, 1):
            # (the gable's triangle)
            K_ = K._object("gable", [(x - w / 2, y + s * w / 2, eave), (x + w / 2, y + s * w / 2, eave), (x, y + s * w / 2, ridge - 0.03),
                                     (x - w / 2, y + s * (w / 2 - 0.03), eave), (x + w / 2, y + s * (w / 2 - 0.03), eave), (x, y + s * (w / 2 - 0.03), ridge - 0.03)],
                           [(0, 1, 2), (3, 5, 4), (0, 3, 4, 1), (1, 4, 5, 2), (2, 5, 3, 0)])
            M.add(K_, "paint", rgb, 0.3)


def climber(name, timber, lod):
    """Two towers (1.2 m square, decks 1.2 and 1.0 m up) joined by a bridge, a net up one side, a slide off the
    second tower; roofs over both."""
    M = M_(name)
    post = TIMBER if timber else GREEN
    surf = "timber" if timber else "paint"
    roof_rgb = FELT if timber else RED
    panel = TIMBER_PALE if timber else YELLOW
    H = 3.8
    towers = [(-1.6, 1.2), (1.6, 1.0)]
    for tx, deck in towers:
        for x in (tx - 0.6, tx + 0.6):
            for y in (-0.6, 0.6):
                if timber:
                    M.add(box("post", (x, y, 1.3), (0.12, 0.12, 2.6)), surf, post, 0.4)
                else:
                    bar(M, (x, y, 0.0), (x, y, 2.6), 0.05, post, lod)
        M.add(box("deck", (tx, 0.0, deck - 0.03), (1.24, 1.24, 0.06)), surf, TIMBER_PALE if timber else GALV, 0.3)
        # (the panels round the deck, one side open to the bridge)
        for s in (-1, 1):
            M.add(box("panel", (tx, s * 0.6, deck + 0.4), (1.1, 0.04, 0.7)), surf if timber else "paint", panel if s < 0 else (BLUE if not timber else panel), 0.2)
        far = tx - 0.6 if tx < 0 else tx + 0.6
        if tx < 0:
            M.add(box("panel", (far, 0.0, deck + 0.4), (0.04, 1.1, 0.7)), surf if timber else "paint", panel, 0.2)
        roof(M, tx, 0.0, 1.4, H, 2.6, roof_rgb, lod)
    # (the bridge: planks between the decks, a chain each side)
    n = 8 if lod == 0 else 3
    for k in range(n):
        t = (k + 0.5) / n
        x = -1.0 + 2.0 * t
        z = 1.2 + (1.0 - 1.2) * t - 0.08 * math.sin(math.pi * t)
        M.add(box("plank", (x, 0.0, z - 0.03), (2.0 / n - 0.04, 0.9, 0.05)), surf, TIMBER_PALE, 0.3)
    for y in (-0.45, 0.45):
        M.add(sweep("rope", [(-1.0, y, 2.0), (0.0, y, 1.85), (1.0, y, 1.8)], 0.02, 4), "paint", BLACK, 0.2)
        if lod == 0:
            for x in (-0.5, 0.0, 0.5):
                bar(M, (x, y, 1.2 - 0.1 * (x + 1)), (x, y, 1.88 - 0.05 * x), 0.012, BLACK, lod)
    # (the net up the first tower's -y side, sloping out)
    if lod == 0:
        for k in range(6):
            x = -2.15 + k * 0.22
            M.add(sweep("netv", [(x, -0.6, 1.2), (x, -1.6, 0.0)], 0.014, 4), "paint", BLACK, 0.2)
        for k in range(5):
            t = k / 4
            M.add(sweep("neth", [(-2.2, -0.6 - t, 1.2 - 1.2 * t), (-1.0, -0.6 - t, 1.2 - 1.2 * t)], 0.014, 4), "paint", BLACK, 0.2)
    else:
        ob = box("net", (-1.6, -1.1, 0.6), (1.1, 0.02, 1.55), rot=Matrix.Rotation(0.69, 4, "X"))
        M.add(ob, "paint", BLACK, 0.2)
    # (the slide off the second tower to +x)
    pts = [(2.2, 1.0), (3.6, 0.35), (4.0, 0.32)]
    for (x0, z0), (x1, z1) in zip(pts, pts[1:]):
        L = math.hypot(x1 - x0, z1 - z0)
        R = Matrix.Rotation(-math.atan2(z1 - z0, x1 - x0), 4, "Y")
        M.add(box("bed", ((x0 + x1) / 2, 0.0, (z0 + z1) / 2), (L + 0.02, 0.5, 0.03), rot=R), "paint" if timber else "steel", YELLOW if timber else STEEL, 0.15)
        for s in (-1, 1):
            M.add(box("side", ((x0 + x1) / 2, s * 0.27, (z0 + z1) / 2 + 0.12), (L + 0.02, 0.04, 0.26), rot=R), "paint" if timber else "steel", YELLOW if timber else STEEL, 0.15)
    mat(M, 0.6, 0.0, 8.2, 4.4)
    return M


def springer(name, which, lod):
    """A spring rider: a base plate, a coil spring, a body on top with handles and foot rests. which: 0 a pony,
    1 a scooter, 2 a duck (rounded shapes only)."""
    M = M_(name)
    M.add(box("base", (0.0, 0.0, 0.01), (0.5, 0.5, 0.02)), "galv", GALV, 0.4)
    if lod == 0:
        pts = []
        for k in range(49):
            a = k * 2 * math.pi / 8
            pts.append((0.11 * math.cos(a), 0.11 * math.sin(a), 0.03 + 0.38 * k / 48))
        M.add(sweep("coil", pts, 0.018, 5), "steel", BLACK, 0.2)
    else:
        M.add(lathe("coil", [(0.13, 0.03), (0.13, 0.41)], 6), "steel", BLACK, 0.2)
    seat_z = 0.62
    if which == 0:
        rgb = RED
        M.add(box("body", (0.0, 0.0, 0.52), (0.62, 0.22, 0.24), bevel=0.08 if lod == 0 else 0.0), "paint", rgb, 0.2)
        M.add(box("neck", (0.3, 0.0, 0.72), (0.14, 0.16, 0.36), rot=Matrix.Rotation(0.35, 4, "Y"), bevel=0.04 if lod == 0 else 0.0), "paint", rgb, 0.2)
        M.add(box("head", (0.42, 0.0, 0.86), (0.28, 0.14, 0.14), bevel=0.04 if lod == 0 else 0.0), "paint", rgb, 0.2)
        handle_x = 0.27
    elif which == 1:
        rgb = BLUE
        M.add(box("deck", (0.0, 0.0, 0.48), (0.7, 0.24, 0.1), bevel=0.04 if lod == 0 else 0.0), "paint", rgb, 0.2)
        M.add(box("nose", (0.32, 0.0, 0.66), (0.12, 0.26, 0.36), rot=Matrix.Rotation(-0.25, 4, "Y"), bevel=0.04 if lod == 0 else 0.0), "paint", rgb, 0.2)
        M.add(box("seat", (-0.12, 0.0, 0.6), (0.34, 0.2, 0.1), bevel=0.03 if lod == 0 else 0.0), "paint", BLACK, 0.2)
        handle_x = 0.34
    else:
        rgb = YELLOW
        ob = lathe("body", [(0.0, -0.18), (0.18, -0.15), (0.24, 0.0), (0.18, 0.15), (0.0, 0.18)], 12 if lod == 0 else 6)
        ob.matrix_world = Matrix.Translation((0.0, 0.0, 0.56)) @ Matrix.Diagonal((1.6, 1.0, 1.0, 1.0))
        M.add(ob, "paint", rgb, 0.2)
        ob = lathe("head", [(0.0, -0.11), (0.11, 0.0), (0.0, 0.11)], 10 if lod == 0 else 5)
        ob.matrix_world = Matrix.Translation((0.3, 0.0, 0.84))
        M.add(ob, "paint", rgb, 0.2)
        M.add(box("beak", (0.44, 0.0, 0.82), (0.12, 0.08, 0.04)), "paint", ORANGE, 0.2)
        handle_x = 0.2
    for s in (-1, 1):
        bar(M, (handle_x, s * 0.08, seat_z + 0.1), (handle_x, s * 0.2, seat_z + 0.1), 0.02, YELLOW if which != 2 else RED, lod)
        bar(M, (-0.05, s * 0.12, 0.46), (-0.05, s * 0.24, 0.46), 0.02, YELLOW if which != 2 else RED, lod)
    # (all three the same height: a little cap at 0.95 m on the pony's ears, the scooter's lamp, the duck's crest)
    M.add(box("top", (handle_x + (0.15 if which == 0 else 0.0 if which == 1 else 0.1), 0.0, 0.93), (0.06, 0.06, 0.04)), "paint", rgb if which != 1 else WHITE, 0.2)
    if which == 1:
        bar(M, (0.34, 0.0, 0.8), (0.34, 0.0, 0.91), 0.02, rgb, lod)
    mat(M, 0.0, 0.0, 1.6, 1.2)
    return M


def roundabout(name, lod):
    M = M_(name)
    seg = 24 if lod == 0 else 10
    M.add(lathe("mat", [(0.0, 0.0), (1.7, 0.0), (1.7, 0.03), (0.0, 0.03)], seg), "paint", RUBBER, 0.3)
    M.add(lathe("deck", [(0.0, 0.16), (0.95, 0.16), (0.95, 0.22), (0.0, 0.22)], seg), "galv", GALV, 0.3)
    M.add(lathe("hub", [(0.12, 0.0), (0.12, 0.16)], 8), "galv", GALV, 0.3)
    for k in range(4):
        a = k * math.pi / 2
        c, s = math.cos(a), math.sin(a)
        bar(M, (0.85 * c, 0.85 * s, 0.22), (0.85 * c, 0.85 * s, 0.86), 0.025, RED, lod)
        bar(M, (0.1 * c, 0.1 * s, 0.86), (0.85 * c, 0.85 * s, 0.86), 0.025, RED, lod)
    bar(M, (0.0, 0.0, 0.22), (0.0, 0.0, 0.88), 0.04, RED, lod)
    return M


def seesaw(name, lod):
    M = M_(name)
    M.add(box("pivot", (0.0, 0.0, 0.2), (0.3, 0.5, 0.4), bevel=0.03 if lod == 0 else 0.0), "paint", GREEN, 0.3)
    t = 0.12
    R = Matrix.Rotation(-t, 4, "Y")
    M.add(box("beam", (0.0, 0.0, 0.45), (3.6, 0.12, 0.08), rot=R), "paint", BLUE, 0.2)
    for s in (-1, 1):
        x = s * 1.6
        z = 0.45 + math.sin(t) * x
        M.add(box("seat", (x, 0.0, z + 0.06), (0.35, 0.36, 0.04)), "paint", BLACK, 0.2)
        hx = x - s * 0.3
        hz = 0.45 + math.sin(t) * hx
        bar(M, (hx, 0.0, hz), (hx, 0.0, hz + 0.32), 0.025, YELLOW, lod)
        bar(M, (hx, -0.16, hz + 0.32), (hx, 0.16, hz + 0.32), 0.022, YELLOW, lod)
        M.add(box("tyre", (x, 0.0, 0.06), (0.4, 0.4, 0.12)), "paint", RUBBER_BLACK, 0.4)
    # (the high end's handle at 0.9 m: the kind's height)
    mat(M, 0.0, 0.0, 4.2, 1.4)
    return M


def fence(name, rgb, lod):
    """A bow-top railing panel 2.5 m along x, 1.0 m high, with its post at +x (panels laid end to end)."""
    M = M_(name)
    L, H = 2.5, 1.0
    M.add(box("post", (L / 2, 0.0, H / 2 + 0.01), (0.06, 0.06, H + 0.02)), "paint", rgb, 0.3)
    M.add(box("cap", (L / 2, 0.0, H + 0.0), (0.08, 0.08, 0.02)), "paint", rgb, 0.3)
    for z in (0.12, 0.82):
        M.add(box("rail", (0.0, 0.0, z), (L, 0.04, 0.03)), "paint", rgb, 0.3)
    n = 20 if lod == 0 else 10
    step = L / n
    for k in range(n):
        x = -L / 2 + (k + 0.5) * step
        if lod == 0:
            M.add(box("bar", (x - step / 2 + 0.01, 0.0, 0.47), (0.012, 0.02, 0.86)), "paint", rgb, 0.3)
            pts = [(x - step / 2 + 0.01 + (step - 0.02) * (1 - math.cos(math.pi * i / 6)) / 2, 0.0, 0.9 + 0.07 * math.sin(math.pi * i / 6)) for i in range(7)]
            M.add(sweep("bow", pts, 0.008, 4), "paint", rgb, 0.3)
        else:
            M.add(box("bar", (x, 0.0, 0.48), (0.02, 0.02, 0.9)), "paint", rgb, 0.3)
    if lod == 1:
        M.add(box("top", (0.0, 0.0, 0.92), (L, 0.02, 0.04)), "paint", rgb, 0.3)
    return M


# --- pitches and courts ----------------------------------------------------------------------------------

def goal(name, lod):
    """A full-size football goal (7.32 by 2.44 m) facing +x, its net back 2 m to -x."""
    M = M_(name)
    W, H, D = 7.32, 2.44, 2.0
    r = 0.06
    for s in (-1, 1):
        bar(M, (0.0, s * W / 2, 0.0), (0.0, s * W / 2, H + r), r, WHITE, lod)
    bar(M, (0.0, -W / 2 - r, H), (0.0, W / 2 + r, H), r, WHITE, lod)
    # (the net's frame: stanchions back and down)
    for s in (-1, 1):
        bar(M, (0.0, s * W / 2, H), (-0.8, s * W / 2, H), 0.025, WHITE, lod)
        bar(M, (-0.8, s * W / 2, H), (-D, s * W / 2, 0.0), 0.025, WHITE, lod)
    bar(M, (-D, -W / 2, 0.02), (-D, W / 2, 0.02), 0.025, WHITE, lod)
    bar(M, (-0.8, -W / 2, H), (-0.8, W / 2, H), 0.02, WHITE, lod)
    if lod == 0:
        # (the net: cords on the back slope, the roof and the sides)
        def cord(a, b):
            M.add(rod("cord", a, b, 0.006, 3), "paint", WHITE, 0.3)
        n = 30
        for k in range(n + 1):
            y = -W / 2 + W * k / n
            cord((0.0, y, H), (-0.8, y, H))
            cord((-0.8, y, H), (-D, y, 0.0))
        for k in range(1, 10):
            t = k / 10
            cord((-0.8 - (D - 0.8) * t, -W / 2, H * (1 - t)), (-0.8 - (D - 0.8) * t, W / 2, H * (1 - t)))
        for k in range(1, 4):
            x = -0.8 * k / 4
            cord((x, -W / 2, H), (x, W / 2, H))
        for s in (-1, 1):
            for k in range(1, 8):
                x = -D * k / 8
                top = H if x > -0.8 else H * (1 - (-0.8 - x) / (D - 0.8))
                cord((x, s * W / 2, 0.0), (x, s * W / 2, top))
            for k in range(1, 6):
                z = H * k / 6
                xb = -0.8 - (D - 0.8) * (1 - z / H)
                cord((0.0, s * W / 2, z), (xb, s * W / 2, z))
    return M


def rugby(name, lod):
    """Rugby posts: uprights 5.6 m apart to 11 m, the crossbar at 3 m, pads round their feet."""
    M = M_(name)
    for s in (-1, 1):
        bar(M, (0.0, s * 2.8, 0.0), (0.0, s * 2.8, 11.0), 0.06, WHITE, lod)
        M.add(box("pad", (0.0, s * 2.8, 0.9), (0.45, 0.45, 1.8), bevel=0.04 if lod == 0 else 0.0), "paint", BLUE, 0.2)
    bar(M, (0.0, -2.8, 3.0), (0.0, 2.8, 3.0), 0.06, WHITE, lod)
    return M


def tennis(name, lod):
    """A tennis net 12.8 m across (along y), its posts, the white band sagging to 0.914 m at the middle."""
    M = M_(name)
    half = 6.4
    for s in (-1, 1):
        bar(M, (0.0, s * half, 0.0), (0.0, s * half, 1.07), 0.04, DARK_GREEN, lod)
        if lod == 0:
            M.add(box("winder", (0.0, s * half, 0.85), (0.08, 0.08, 0.1)), "paint", DARK_GREEN, 0.2)
    n = 12 if lod == 0 else 4
    band = []
    for k in range(n + 1):
        y = -half + 2 * half * k / n
        band.append((0.0, y, 0.914 + (1.07 - 0.914) * (y / half) ** 2))
    M.add(sweep("band", band, 0.03, 4, square=True), "paint", WHITE, 0.2)
    M.add(box("strap", (0.0, 0.0, 0.46), (0.02, 0.05, 0.92)), "paint", WHITE, 0.2)
    step = 0.2 if lod == 0 else 0.8
    y = -half + step
    while y < half - 0.01:
        z = 0.914 + (1.07 - 0.914) * (y / half) ** 2
        M.add(box("mesh", (0.0, y, z / 2), (0.006, 0.006, z)), "paint", NET, 0.2)
        y += step
    for k in range(1, 6 if lod == 0 else 2):
        z = 0.9 * k / (6 if lod == 0 else 2)
        M.add(box("mesh", (0.0, 0.0, z), (0.006, 2 * half, 0.006)), "paint", NET, 0.2)
    return M


def court_fence(name, lod):
    """A 3 m panel (along x) of a court's 2.75 m wire fence, its post at +x."""
    M = M_(name)
    L, H = 3.0, 2.75
    M.add(box("post", (L / 2, 0.0, H / 2), (0.06, 0.06, H)), "paint", DARK_GREEN, 0.3)
    for z in (0.05, H - 0.03):
        M.add(box("rail", (0.0, 0.0, z), (L, 0.035, 0.035)), "paint", DARK_GREEN, 0.3)
    step = 0.15 if lod == 0 else 0.5
    n = int(round(L / step))
    for k in range(n):
        x = -L / 2 + (k + 0.5) * L / n
        M.add(box("wire", (x, 0.0, H / 2), (0.006, 0.006, H - 0.1)), "paint", DARK_GREEN, 0.3)
    for k in range(1, 9 if lod == 0 else 3):
        z = H * k / (9 if lod == 0 else 3)
        M.add(box("wire", (0.0, 0.0, z), (L, 0.006, 0.006)), "paint", DARK_GREEN, 0.3)
    return M


def hoop(name, lod):
    """A basketball post at -x, its arm out to the board facing +x, the hoop at 3.05 m."""
    M = M_(name)
    bar(M, (-1.2, 0.0, 0.0), (-1.2, 0.0, 3.2), 0.08, BLUE, lod)
    M.add(sweep("arm", [(-1.2, 0.0, 3.1), (-0.9, 0.0, 3.35), (-0.15, 0.0, 3.4)], 0.05, 8 if lod == 0 else 4), "paint", BLUE, 0.2)
    M.add(box("board", (-0.1, 0.0, 3.425), (0.04, 1.8, 1.05)), "paint", WHITE, 0.2)
    if lod == 0:
        M.add(box("square", (-0.075, 0.0, 3.25), (0.01, 0.59, 0.45)), "paint", BLACK, 0.2)
        M.add(box("square_in", (-0.07, 0.0, 3.26), (0.01, 0.53, 0.39)), "paint", WHITE, 0.2)
    ring = [(0.15 + 0.23 * math.cos(2 * math.pi * k / 16), 0.23 * math.sin(2 * math.pi * k / 16), 3.05) for k in range(17)]
    M.add(sweep("rim", ring, 0.01, 4), "paint", ORANGE, 0.2)
    bar(M, (-0.08, 0.0, 3.05), (-0.08 + 0.02, 0.0, 3.05), 0.03, ORANGE, lod)
    M.add(box("mat", (0.5, 0.0, 0.01), (0.1, 0.1, 0.02)), "paint", BLUE, 0.2)
    return M


# --- the village green -----------------------------------------------------------------------------------

def notice(name, lod):
    """The parish notice board facing +x: two posts, a glazed case, a little roof; notices pinned up inside."""
    M = M_(name)
    for y in (-0.65, 0.65):
        M.add(box("post", (0.0, y, 0.95), (0.1, 0.1, 1.9)), "timber", TIMBER, 0.4)
    M.add(box("case", (0.0, 0.0, 1.3), (0.1, 1.24, 0.9), bevel=0.01 if lod == 0 else 0.0), "timber", DARK_GREEN, 0.3)
    M.add(box("back", (0.04, 0.0, 1.3), (0.02, 1.1, 0.78)), "paint", (0.25, 0.18, 0.1), 0.2)
    if lod == 0:
        import random
        random.seed(5)
        for k in range(7):
            w, h = random.uniform(0.14, 0.26), random.uniform(0.18, 0.3)
            M.add(box("note", (0.055, random.uniform(-0.4, 0.4), random.uniform(1.05, 1.55)), (0.005, w, h)), "paint",
                  random.choice([(0.7, 0.7, 0.68), (0.62, 0.55, 0.2), (0.4, 0.6, 0.62), (0.68, 0.4, 0.4)]), 0.1)
        M.add(box("glass", (0.06, 0.0, 1.3), (0.006, 1.12, 0.8)), "lens", GLASS, 0.1)
    for s in (-1, 1):
        M.add(box("roof", (s * 0.12, 0.0, 1.95), (0.32, 1.5, 0.03), rot=Matrix.Rotation(s * 0.6, 4, "Y")), "timber", FELT, 0.3)
    M.add(box("ridge", (0.0, 0.0, 2.07), (0.05, 1.52, 0.06)), "timber", TIMBER, 0.3)
    return M


def litter_bin(name, black, lod):
    M = M_(name)
    seg = 16 if lod == 0 else 8
    rgb = BLACK if black else DARK_GREEN
    M.add(lathe("body", [(0.0, 0.0), (0.24, 0.0), (0.27, 0.8), (0.0, 0.8)], seg), "paint", rgb, 0.3)
    if black:
        M.add(lathe("band", [(0.272, 0.66), (0.278, 0.66), (0.278, 0.72), (0.272, 0.72)], seg), "paint", GOLD, 0.2)
        M.add(lathe("lid", [(0.0, 0.8), (0.29, 0.8), (0.29, 0.86), (0.2, 0.93), (0.06, 0.97), (0.06, 1.0), (0.0, 1.0)], seg), "paint", rgb, 0.3)
    else:
        M.add(lathe("lid", [(0.0, 0.8), (0.29, 0.8), (0.28, 0.9), (0.16, 0.98), (0.0, 1.0)], seg), "paint", rgb, 0.3)
    if lod == 0:
        M.add(box("slot", (0.24, 0.0, 0.86), (0.08, 0.22, 0.07)), "paint", BLACK, 0.2)
    return M


def village_sign(name, two_posts, lod):
    """A village sign: a painted picture panel (a made-up scene: hills, a church tower, a field) up on a post
    over a brick plinth, or on two posts over a blank name board; facing +x."""
    M = M_(name)
    top = 3.2
    if not two_posts:
        M.add(box("plinth", (0.0, 0.0, 0.3), (0.9, 0.9, 0.6), bevel=0.01 if lod == 0 else 0.0), "concrete", BRICK, 0.5)
        M.add(box("coping", (0.0, 0.0, 0.63), (1.0, 1.0, 0.06)), "concrete", CREAM, 0.4)
        zc, pw, ph = 2.45, 1.2, 1.0
        M.add(box("post", (0.0, 0.0, (0.66 + zc - ph / 2) / 2), (0.16, 0.16, zc - ph / 2 - 0.62)), "timber", TIMBER, 0.3)
    else:
        for y in (-0.75, 0.75):
            M.add(box("post", (0.0, y, top / 2 - 0.05), (0.14, 0.14, top - 0.1)), "timber", TIMBER, 0.3)
        M.add(box("board", (0.0, 0.0, 1.0), (0.05, 1.5, 0.35)), "paint", CREAM, 0.2)
        zc, pw, ph = 2.25, 1.36, 1.0
    M.add(box("frame", (0.0, 0.0, zc), (0.08, pw + 0.08, ph + 0.08)), "timber", TIMBER, 0.3)
    f = 0.045
    # (the picture, both faces: sky, hills, a field, a tower)
    for s in (-1, 1):
        x = s * f
        M.add(box("sky", (x, 0.0, zc + ph * 0.2), (0.005, pw, ph * 0.6)), "paint", (0.25, 0.42, 0.6), 0.1)
        M.add(box("field", (x, 0.0, zc - ph * 0.3), (0.005, pw, ph * 0.4)), "paint", (0.1, 0.28, 0.06), 0.1)
        if lod == 0:
            M.add(box("hill", (x * 1.01, -pw * 0.2, zc - ph * 0.08), (0.005, pw * 0.6, ph * 0.18)), "paint", (0.06, 0.18, 0.05), 0.1)
            M.add(box("tower", (x * 1.03, pw * 0.22, zc + ph * 0.02), (0.005, 0.16, ph * 0.42)), "paint", (0.55, 0.5, 0.4), 0.1)
            M.add(box("spire", (x * 1.03, pw * 0.22, zc + ph * 0.28), (0.005, 0.08, ph * 0.12)), "paint", (0.3, 0.3, 0.32), 0.1)
            M.add(box("sun", (x * 1.03, -pw * 0.3, zc + ph * 0.32), (0.005, 0.12, 0.12)), "paint", (0.75, 0.6, 0.15), 0.1)
    # (a little roof and finial to the height)
    M.add(box("cap", (0.0, 0.0, zc + ph / 2 + 0.07), (0.16, pw + 0.2, 0.06)), "timber", TIMBER, 0.3)
    fin_z = zc + ph / 2 + 0.1
    M.add(lathe("finial", [(0.05, fin_z), (0.07, fin_z + 0.1), (0.0, top)], 8 if lod == 0 else 4), "paint", GOLD, 0.2)
    return M


def build(lod):
    """Every model at lod (0 the model, 1 its distance version)."""
    s = "" if lod == 0 else "_LOD1"
    return [
        swings("SM_Prop_play_swings_0" + s, False, lod),
        swings("SM_Prop_play_swings_1" + s, True, lod),
        slide("SM_Prop_play_slide_0" + s, False, lod),
        slide("SM_Prop_play_slide_1" + s, True, lod),
        climber("SM_Prop_play_climber_0" + s, False, lod),
        climber("SM_Prop_play_climber_1" + s, True, lod),
        springer("SM_Prop_play_springer_0" + s, 0, lod),
        springer("SM_Prop_play_springer_1" + s, 1, lod),
        springer("SM_Prop_play_springer_2" + s, 2, lod),
        roundabout("SM_Prop_play_roundabout_0" + s, lod),
        seesaw("SM_Prop_play_seesaw_0" + s, lod),
        fence("SM_Prop_play_fence_0" + s, GREEN, lod),
        fence("SM_Prop_play_fence_1" + s, BLACK, lod),
        goal("SM_Prop_pitch_goal_0" + s, lod),
        rugby("SM_Prop_pitch_posts_0" + s, lod),
        tennis("SM_Prop_tennis_net_0" + s, lod),
        court_fence("SM_Prop_court_fence_0" + s, lod),
        hoop("SM_Prop_basket_hoop_0" + s, lod),
        notice("SM_Prop_notice_board_0" + s, lod),
        litter_bin("SM_Prop_litter_bin_0" + s, False, lod),
        litter_bin("SM_Prop_litter_bin_1" + s, True, lod),
        village_sign("SM_Prop_village_sign_0" + s, False, lod),
        village_sign("SM_Prop_village_sign_1" + s, True, lod),
    ]


def main():
    import blender_farm_props as FP
    args = sys.argv[1:]
    out = {}
    for lod in (0, 1):
        K.clear()
        models = build(lod)
        for M in models:
            nt, y0, y1, x0, x1, z0, z1 = K.write(M)
            out[M.name] = (nt, y0, y1)
            print("%-30s %5d triangles, %.2f..%.2f m up, %.1f x %.1f m" % (M.name, nt, y0, y1, x1 - x0, z1 - z0))
        if lod == 0 and "--render" in args:
            import blender_farm_kit_render as R
            R.render_props(models, args[args.index("--render") + 1],
                           title="Ride UK's village set: playgrounds, pitches and greens (our own models; each with a hand-made distance version)",
                           sheet_name="village_props.png")
    FP.KINDS = KINDS
    FP.write_kinds(out)


if __name__ == "__main__":
    main()
