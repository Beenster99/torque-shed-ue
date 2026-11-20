"""Britain's level crossing kit (beenster, Nov 20 2026: "Please build a 3D blender model for these level crossing
signs, which flash when there is an imminent train", from four photos: an open crossing with lights and a saltire,
a passive crossing with Give Way and the steam locomotive plate, a gated crossing with STOP on the road, a half
barrier and a full barrier crossing with picket and palisade fencing). Built in Blender into Data/Props/Blender/
<id>.glb like the ferry kit (Tools/props/make_ferry.py), so the PC imports them with the shed's props
(Tools/ue_import_props.py) and World/TorqueLevelCrossing.cpp stands each crossing up from them near the rider.

    blender -b --factory-startup -P Tools/props/make_level_crossing.py                 all of them
    blender -b --factory-startup -P Tools/props/make_level_crossing.py -- lx_unit      just these
    python3 Tools/props/make_level_crossing.py -- ...                                    the same with bpy from pip

Torque Shed's frame (metres, x right, y up, +z the front: the face the traffic reads), each piece's origin where the
game hangs it. World/TorqueLevelCrossing.cpp has the same numbers (its TLx namespace); change both together.

    lx_post           a galvanised signal post, 3.9 m above ground (and 0.4 m below it), its cap; origin at its foot
    lx_unit           the road traffic light unit: the red and white chequered backboard (1.2 x 0.8 m) with two red
                      lamps and an amber one under deep hoods, clamped to a post. Origin on the post's axis level
                      with the board's middle (the game hangs it UNIT_Y up the post).
    lx_lit_red_l / lx_lit_red_r / lx_lit_amber
                      each lamp's lit face alone (an emissive disc just in front of its dark lens), in lx_unit's
                      frame: the game shows and hides them (amber steady, then the reds flashing in turn).
    lx_saltire        the St Andrew's cross on its two stays over a light unit (origin at the board's top middle)
    lx_saltire_top    the same cross on a post's top (origin at the post's top)
    lx_keep_clear     KEEP CROSSING CLEAR, clamped to a post (origin on the post's axis at the plate's middle)
    lx_contact        the blue plate: the crossing's (made-up) name and the railway's (made-up) number, its row
                      in lx_contact.png picked per crossing by the game (the printed material's Courier)
    lx_give_way       the Give Way triangle (750 mm), clamped to a post (origin on the post's axis, the plate's middle)
    lx_loco           the steam locomotive plate under it
    lx_danger         DANGER, crossing warning lights apply to all users: a board on two timber posts (origin its foot)
    lx_user_worked    the gated crossing's instruction board on its own post (origin its foot)
    lx_barrier_mech   the barrier machine: its galvanised case and the hub the boom turns on (origin its foot; the
                      boom's pivot PIVOT, the boom swinging up about z from lying along +x)
    lx_boom_<n>       a boom n metres long from the pivot (n 3 to 9), red and white, its counterweight behind, three
                      lamp housings on top; origin at the pivot, lying along +x
    lx_boom_lit       one boom lamp's lit faces (both sides), origin at the lamp: the game puts three on a boom
    lx_skirt_bar      a full barrier's hanging skirt bar, 1 m down from its origin (the game scales it to SKIRT)
    lx_skirt_rail     the skirt's bottom rail, 1 m along +x (scaled to the boom's length)
    lx_gate_<n>       a white steel field-style crossing gate n metres long (n 2.5 to 4.5 in halves: _25 .. _45),
                      a red disc on it; origin at its hinge foot, lying along +x
    lx_gate_post      the gate's hanging post
    lx_msl            the gated crossing's miniature stop light (red over green) on its post, origin its foot
    lx_msl_lit_red / lx_msl_lit_green    its lit faces, in lx_msl's frame
    lx_picket         2 m of white timber picket fence (along +x from its origin)
    lx_palisade       2 m of white steel palisade fence (along +x from its origin)
    lx_lamp_post      a full barrier crossing's tall floodlight column (origin its foot)
    lx_cabinet        the crossing's equipment case (origin its foot)

Text is printed (Tools/props/make_lx_textures.py), never cut into 3D letters; no real operator's name or logo.
Every piece is bevelled and box-UV'd in metres by kit.finish, a plate's printed face 0..1 over it. Deterministic.
"""
import json
import math
import os
import sys

import bpy  # noqa: E402,F401  (first: with bpy from pip, bmesh and mathutils come with it)
import bmesh  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kit  # noqa: E402
from kit import box, span, cyl, tube, prism  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "Data", "Props", "Blender")
MODELS = {}

# --- The kit's numbers (World/TorqueLevelCrossing.cpp TLx has the same) ----------------------------------
POST_R = 0.057          # a signal post's radius (114 mm galvanised tube)
POST_TOP = 3.9
UNIT_Y = 2.75           # the light unit's middle up its post
BOARD_W, BOARD_H = 1.2, 0.8
BOARD_Z = 0.10          # the board's face in front of the post's axis
LAMPS = {"red_l": (-0.33, 0.1), "red_r": (0.33, 0.1), "amber": (0.0, -0.17)}   # each lamp's middle on the board
LENS_R = 0.115
LENS_Z = BOARD_Z + 0.07   # the dark lens, recessed in its hood
PLATE_Z = 0.07          # a post plate's back in front of the post's axis
KEEP_W, KEEP_H = 0.62, 0.31
CONTACT_W, CONTACT_H = 0.36, 0.34
PIVOT = (0.0, 1.0, 0.32)   # the boom's pivot on the barrier machine
BOOM_H, BOOM_D = 0.11, 0.07
SKIRT = 0.75            # a full barrier's skirt hangs this far under its boom
GATE_H = 1.3
MSL_Y = 2.1             # the miniature stop light's middle (between its two lamps)


def model(pid):
    def wrap(fn):
        MODELS[pid] = fn
        return fn
    return wrap


def face(name, pts, z, mat):
    """A flat face (its printed side towards +z) through the outline pts ((x, y), anticlockwise seen from the front)."""
    bm = bmesh.new()
    vs = [bm.verts.new(kit.B((x, y, z))) for x, y in pts]
    bm.faces.new(vs)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    f = bm.faces[0]
    # (make it face +z: Blender's -y)
    if f.normal.y > 0:
        f.normal_flip()
    o = kit._new(name, bm)
    return kit._finish(o, mat, 0.0)


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def ring(name, centre, r_out, r_in, z0, z1, mat, a0=0.0, a1=360.0, segs=24):
    """An open tube (a lamp hood) along z from z0 to z1 round centre (x, y), its wall between r_in and r_out, over
    the angles a0..a1 (degrees, 0 to +x, 90 up)."""
    full = abs(a1 - a0) >= 359.9
    n = segs if full else max(4, int(segs * abs(a1 - a0) / 360.0))
    bm = bmesh.new()
    cx, cy = centre
    rows = []
    for i in range(n + (0 if full else 1)):
        a = math.radians(a0 + (a1 - a0) * i / n)
        c, s = math.cos(a), math.sin(a)
        rows.append([bm.verts.new(kit.B((cx + r * c, cy + r * s, z))) for r, z in ((r_out, z0), (r_out, z1), (r_in, z1), (r_in, z0))])
    m = len(rows)
    for i in range(m if full else m - 1):
        a, b = rows[i], rows[(i + 1) % m]
        for k in range(4):
            bm.faces.new((a[k], b[k], b[(k + 1) % 4], a[(k + 1) % 4]))
    if not full:
        bm.faces.new(rows[0])
        bm.faces.new(list(reversed(rows[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    o = kit._new(name, bm)
    return kit._finish(o, mat, 0.0)


def disc(name, centre, r, z, mat, segs=24, back=False):
    pts = [(centre[0] + r * math.cos(2 * math.pi * k / segs), centre[1] + r * math.sin(2 * math.pi * k / segs)) for k in range(segs)]
    if back:
        bm = bmesh.new()
        vs = [bm.verts.new(kit.B((x, y, z))) for x, y in pts]
        f = bm.faces.new(vs)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        if f.normal.y < 0:
            f.normal_flip()
        o = kit._new(name, bm)
        return kit._finish(o, mat, 0.0)
    return face(name, pts, z, mat)


def clamp(name, y, z_back, mat="galvanised"):
    """A pair of U-bolt clamps holding a plate to a post at height y: a strap round the post and its bar."""
    for dy in (-0.06, 0.06):
        # (a band round the post, turned to lie about y: a flat ring seen from above)
        bm = bmesh.new()
        bmesh.ops.create_cone(bm, cap_ends=False, segments=12, radius1=POST_R + 0.008, radius2=POST_R + 0.008, depth=0.03)
        o = kit._new("%sBand%+.0f" % (name, dy * 100), bm)
        kit._place(o, (0.0, y + dy, 0.0), None)
        kit._finish(o, mat, 0.0)
        span("%sBar%+.0f" % (name, dy * 100), (-0.09, y + dy - 0.015, POST_R), (0.09, y + dy + 0.015, z_back), mat, bevel=0.004)


def plate(name, w, h, y, printed, back="galvanised", z_back=PLATE_Z, edge=0.008):
    """A post plate: a pressed aluminium plate w x h with its middle at height y, printed face forward."""
    span(name + "Back", (-w / 2, y - h / 2, z_back), (w / 2, y + h / 2, z_back + 0.004), back, bevel=0.0015)
    # (a turned-over edge round it, as an aluminium sign is: a slightly proud rim)
    face(name + "Face", rect(-w / 2 + edge * 0.3, y - h / 2 + edge * 0.3, w / 2 - edge * 0.3, y + h / 2 - edge * 0.3), z_back + 0.0045, printed)
    clamp(name + "Clamp", y, z_back)


# --- Posts, the light unit and its plates ---------------------------------------------------------------

@model("lx_post")
def lx_post():
    cyl("Post", (0.0, (POST_TOP - 0.4) / 2, 0.0), POST_R, POST_TOP + 0.4, "galvanised", verts=16, bevel=0.004)
    cyl("Cap", (0.0, POST_TOP + 0.015, 0.0), POST_R + 0.006, 0.03, "galvanised", verts=16, bevel=0.006)
    # (the concrete it's set in, just showing at the ground)
    cyl("Footing", (0.0, 0.01, 0.0), 0.2, 0.06, "concrete", verts=16, bevel=0.01)


@model("lx_unit")
def lx_unit():
    w, h = BOARD_W, BOARD_H
    # The backboard: an aluminium panel, its front printed (the chequers and the black middle), its back grey.
    span("Board", (-w / 2, -h / 2, BOARD_Z - 0.012), (w / 2, h / 2, BOARD_Z), "paint_grey", bevel=0.004)
    face("BoardFace", rect(-w / 2 + 0.003, -h / 2 + 0.003, w / 2 - 0.003, h / 2 - 0.003), BOARD_Z + 0.0005, "lx_board")
    # Its frame behind (two rails) and the clamps to the post.
    for y in (-0.25, 0.25):
        span("Rail%+.0f" % (y * 10), (-0.5, y - 0.025, POST_R + 0.005), (0.5, y + 0.025, BOARD_Z - 0.012), "steel_dark", bevel=0.004)
        clamp("Clamp%+.0f" % (y * 10), y, POST_R + 0.005, "steel_dark")
    # The lamps: a black body through the board, a deep hood over its top, a dark lens inside.
    for key, (x, y) in LAMPS.items():
        cyl("Body_" + key, (x, y, BOARD_Z + 0.03), LENS_R + 0.03, 0.08, "paint_black", axis="z", verts=24, bevel=0.006)
        cyl("Back_" + key, (x, y, BOARD_Z - 0.09), LENS_R + 0.01, 0.16, "paint_black", axis="z", verts=20, bevel=0.01)
        ring("Hood_" + key, (x, y), LENS_R + 0.035, LENS_R + 0.022, BOARD_Z + 0.06, BOARD_Z + 0.27, "paint_black", a0=-25.0, a1=205.0, segs=28)
        ring("Rim_" + key, (x, y), LENS_R + 0.035, LENS_R, BOARD_Z + 0.06, BOARD_Z + 0.075, "paint_black", segs=28)
        disc("Lens_" + key, (x, y), LENS_R, LENS_Z, "lx_lens_amber" if key == "amber" else "lx_lens_red", segs=24)
        # (the lens's ribs: a slightly smaller disc a touch proud, catching the light)
        disc("LensDome_" + key, (x, y), LENS_R * 0.55, LENS_Z + 0.006, "lx_lens_amber" if key == "amber" else "lx_lens_red", segs=16)


def lit(key, mat):
    x, y = LAMPS[key]
    disc("Lit", (x, y), LENS_R - 0.004, LENS_Z + 0.009, mat, segs=24)


@model("lx_lit_red_l")
def lx_lit_red_l():
    lit("red_l", "lx_lit_red")


@model("lx_lit_red_r")
def lx_lit_red_r():
    lit("red_r", "lx_lit_red")


@model("lx_lit_amber")
def lx_lit_amber():
    lit("amber", "lx_lit_amber")


def saltire_cross(cy, z):
    """The St Andrew's cross: two boards 1.5 m by 0.25 m crossing at its middle (cy up), each at 28 degrees, its
    ends cut to points; a red border round a white middle (red first, the white one just in front)."""
    L, W, B = 1.5, 0.25, 0.045
    for k, ang in enumerate((28.0, -28.0)):
        a = math.radians(ang)
        c, s = math.cos(a), math.sin(a)

        def outline(half_l, half_w, tip):
            pts = [(-half_l, 0.0), (-half_l + tip, -half_w), (half_l - tip, -half_w), (half_l, 0.0), (half_l - tip, half_w), (-half_l + tip, half_w)]
            return [(u * c - v * s, cy + u * s + v * c) for u, v in pts]

        dz = k * 0.012   # (one board in front of the other, as they're bolted)
        prism("Red%d" % k, outline(L / 2, W / 2, W * 0.45), 0.008, "lx_sign_red", axis="z", centre=z + dz, bevel=0.0)
        prism("White%d" % k, outline(L / 2 - B * 1.6, W / 2 - B, (W / 2 - B) * 0.9), 0.003, "lx_sign_white", axis="z", centre=z + dz + 0.0055, bevel=0.0)
    span("Bolt", (-0.02, cy - 0.02, z + 0.012), (0.02, cy + 0.02, z + 0.026), "galvanised", bevel=0.006)


@model("lx_saltire")
def lx_saltire():
    """Over a light unit: its stays from the board's top (the origin) up to the cross 0.9 m over it."""
    cy = 0.95
    for sx in (-1.0, 1.0):
        tube("Stay%+d" % sx, [(sx * 0.38, -0.02, 0.06), (sx * 0.22, cy - 0.05, 0.08)], 0.016, "rust", sides=6)
    saltire_cross(cy, 0.1)


@model("lx_saltire_top")
def lx_saltire_top():
    """On a post's top (the origin): a bracket up from the post to the cross's middle 0.3 m over it."""
    cy = 0.3
    cyl("Spigot", (0.0, 0.15, 0.0), POST_R - 0.008, 0.36, "galvanised", verts=12, bevel=0.003)
    span("Bracket", (-0.05, cy - 0.12, 0.0), (0.05, cy + 0.12, 0.06), "galvanised", bevel=0.005)
    saltire_cross(cy, 0.07)


@model("lx_keep_clear")
def lx_keep_clear():
    plate("Keep", KEEP_W, KEEP_H, 0.0, "lx_keep_clear")


@model("lx_contact")
def lx_contact():
    plate("Contact", CONTACT_W, CONTACT_H, 0.0, "lx_contact")


@model("lx_give_way")
def lx_give_way():
    s = 0.75
    hgt = s * math.sqrt(3) / 2
    y0, y1 = -hgt / 2, hgt / 2
    tri = [(-s / 2, y1), (0.0, y0), (s / 2, y1)]
    # (round the triangle's corners a little: aluminium plates are)
    prism("Back", [(x, y) for x, y in reversed(tri)], 0.004, "paint_grey", axis="z", centre=PLATE_Z + 0.002, bevel=0.0)
    face("Face", [(-s / 2 + 0.004, y1 - 0.002), (0.0, y0 + 0.005), (s / 2 - 0.004, y1 - 0.002)], PLATE_Z + 0.0045, "lx_give_way")
    clamp("Clamp", 0.05, PLATE_Z)


@model("lx_loco")
def lx_loco():
    plate("Loco", 0.6, 0.3, 0.0, "lx_loco")


@model("lx_danger")
def lx_danger():
    w, h, y0 = 1.05, 0.72, 0.95
    for x in (-0.38, 0.38):
        span("Post%+.0f" % (x * 10), (x - 0.05, -0.4, -0.12), (x + 0.05, y0 + h + 0.05, -0.02), "fence_wood", bevel=0.008)
    span("Board", (-w / 2, y0, -0.02), (w / 2, y0 + h, 0.0), "paint_white", bevel=0.004)
    face("Face", rect(-w / 2 + 0.004, y0 + 0.004, w / 2 - 0.004, y0 + h - 0.004), 0.0008, "lx_danger")


@model("lx_user_worked")
def lx_user_worked():
    w, h, y0 = 0.5, 0.75, 0.9
    span("Post", (-0.045, -0.4, -0.11), (0.045, y0 + h + 0.05, -0.02), "fence_wood", bevel=0.008)
    span("Board", (-w / 2, y0, -0.02), (w / 2, y0 + h, 0.0), "paint_white", bevel=0.004)
    face("Face", rect(-w / 2 + 0.004, y0 + 0.004, w / 2 - 0.004, y0 + h - 0.004), 0.0008, "lx_user_worked")


# --- Barriers -------------------------------------------------------------------------------------------

@model("lx_barrier_mech")
def lx_barrier_mech():
    px, py, pz = PIVOT
    # The case: a galvanised box on a plinth, a sloping lid; the hub out of its front.
    span("Plinth", (-0.32, -0.1, -0.3), (0.32, 0.08, 0.3), "concrete", bevel=0.01)
    span("Case", (-0.25, 0.08, -0.22), (0.25, py + 0.2, 0.22), "galvanised", bevel=0.012)
    prism("Lid", [(-0.24, py + 0.2), (0.24, py + 0.2), (0.24, py + 0.26), (-0.24, py + 0.3)], 0.46, "galvanised", axis="z", centre=0.0, bevel=0.006)
    cyl("Hub", (px, py, 0.22 + 0.05), 0.11, 0.1, "steel_dark", axis="z", verts=20, bevel=0.01)
    cyl("Shaft", (px, py, pz - 0.02), 0.05, 0.12, "steel_dark", axis="z", verts=12, bevel=0.005)
    span("Door", (-0.2, 0.2, 0.221), (0.2, py - 0.1, 0.226), "galvanised", bevel=0.002)


def boom(length):
    """A boom from the pivot along +x: red and white stripes 0.6 m long (white at the tip), its counterweight
    behind, three lamp housings on its top (the game lights them: lx_boom_lit)."""
    hh, hd = BOOM_H / 2, BOOM_D / 2
    span("Clamp", (-0.18, -hh - 0.03, -hd - 0.02), (0.22, hh + 0.03, hd + 0.02), "steel_dark", bevel=0.01)
    span("Counter", (-0.75, -0.12, -0.06), (-0.2, 0.12, 0.06), "steel_dark", bevel=0.015)
    n = max(1, int(round((length - 0.22) / 0.6)))
    step = (length - 0.22) / n
    for k in range(n):
        x0 = 0.22 + k * step
        white = (n - 1 - k) % 2 == 0
        span("Stripe%d" % k, (x0, -hh, -hd), (x0 + step, hh, hd), "lx_reflect_white" if white else "lx_reflect_red", bevel=0.008)
    for x in boom_lamps(length):
        cyl("Lamp%.1f" % x, (x, hh + 0.05, 0.0), 0.045, 0.1, "paint_black", axis="y", verts=12, bevel=0.006)
        for sz in (-1.0, 1.0):
            disc("LampLens%.1f%+d" % (x, sz), (x, hh + 0.06), 0.03, sz * 0.046, "lx_lens_red", segs=12, back=sz < 0)


def boom_lamps(length):
    return (0.8, length * 0.5 + 0.2, length - 0.15)


for _n in range(3, 10):
    MODELS["lx_boom_%d" % _n] = (lambda n: (lambda: boom(float(n))))(_n)


@model("lx_boom_lit")
def lx_boom_lit():
    hh = BOOM_H / 2
    for sz in (-1.0, 1.0):
        disc("Lit%+d" % sz, (0.0, hh + 0.06), 0.028, sz * 0.049, "lx_lit_red", segs=12, back=sz < 0)


@model("lx_skirt_bar")
def lx_skirt_bar():
    cyl("Bar", (0.0, -0.5, 0.0), 0.012, 1.0, "lx_reflect_white", verts=8, bevel=0.0)


@model("lx_skirt_rail")
def lx_skirt_rail():
    cyl("Rail", (0.5, 0.0, 0.0), 0.018, 1.0, "lx_reflect_white", axis="x", verts=8, bevel=0.0)


def gate(length):
    """A white steel crossing gate from its hinge (the origin) along +x: top and bottom rails, upright bars, a
    diagonal brace, a red disc on its middle (both faces)."""
    h = GATE_H
    r = 0.025
    tube("Top", [(0.06, h, 0.0), (length, h, 0.0)], r * 1.4, "paint_white", sides=10)
    tube("Bottom", [(0.06, 0.2, 0.0), (length, 0.2, 0.0)], r, "paint_white", sides=8)
    tube("Mid", [(0.06, 0.75, 0.0), (length, 0.75, 0.0)], r, "paint_white", sides=8)
    for x in (0.06, length):
        tube("End%.1f" % x, [(x, 0.15, 0.0), (x, h, 0.0)], r * 1.3, "paint_white", sides=10)
    k = int(length / 0.13)
    for i in range(1, k):
        x = 0.06 + (length - 0.06) * i / k
        tube("Bar%d" % i, [(x, 0.2, 0.0), (x, h, 0.0)], 0.009, "paint_white", sides=6)
    tube("Brace", [(0.06, 0.25, 0.0), (length * 0.55, h - 0.05, 0.0)], r * 0.9, "paint_white", sides=8)
    cx = length * 0.55
    disc("Disc", (cx, 0.78), 0.22, 0.03, "lx_reflect_red", segs=28)
    disc("DiscBack", (cx, 0.78), 0.22, -0.03, "lx_reflect_red", segs=28, back=True)
    span("DiscPlate", (cx - 0.05, 0.55, -0.028), (cx + 0.05, 1.0, 0.028), "paint_white", bevel=0.004)
    for y in (0.3, h - 0.1):
        cyl("Hinge%.1f" % y, (0.0, y, 0.0), 0.03, 0.08, "steel_dark", verts=10, bevel=0.004)


for _n in (25, 30, 35, 40, 45):
    MODELS["lx_gate_%d" % _n] = (lambda n: (lambda: gate(n / 10.0)))(_n)


@model("lx_gate_post")
def lx_gate_post():
    span("Post", (-0.075, -0.5, -0.075), (0.075, GATE_H + 0.15, 0.075), "paint_white", bevel=0.008)
    prism("Cap", [(-0.085, GATE_H + 0.15), (0.085, GATE_H + 0.15), (0.0, GATE_H + 0.23)], 0.17, "paint_white", axis="z", centre=0.0, bevel=0.004)


@model("lx_msl")
def lx_msl():
    cyl("Post", (0.0, (MSL_Y + 0.2) / 2 - 0.2, 0.0), 0.045, MSL_Y + 0.6, "galvanised", verts=12, bevel=0.003)
    span("Head", (-0.12, MSL_Y - 0.22, 0.02), (0.12, MSL_Y + 0.22, 0.18), "paint_black", bevel=0.012)
    span("Backboard", (-0.19, MSL_Y - 0.3, 0.012), (0.19, MSL_Y + 0.3, 0.022), "paint_black", bevel=0.004)
    face("BackboardEdge", rect(-0.19, MSL_Y - 0.3, 0.19, MSL_Y + 0.3), 0.0225, "lx_sign_white")
    face("BackboardMid", rect(-0.16, MSL_Y - 0.27, 0.16, MSL_Y + 0.27), 0.023, "paint_black")
    for name, dy, mat in (("Red", 0.11, "lx_lens_red"), ("Green", -0.11, "lx_lens_green")):
        ring("Hood" + name, (0.0, MSL_Y + dy), 0.085, 0.075, 0.18, 0.3, "paint_black", a0=-20.0, a1=200.0, segs=20)
        disc("Lens" + name, (0.0, MSL_Y + dy), 0.07, 0.181, mat, segs=20)
    clamp("Clamp", MSL_Y, 0.02)


@model("lx_msl_lit_red")
def lx_msl_lit_red():
    disc("Lit", (0.0, MSL_Y + 0.11), 0.066, 0.188, "lx_lit_red", segs=20)


@model("lx_msl_lit_green")
def lx_msl_lit_green():
    disc("Lit", (0.0, MSL_Y - 0.11), 0.066, 0.188, "lx_lit_green", segs=20)


# --- Fencing, the lamp column, the case -----------------------------------------------------------------

@model("lx_picket")
def lx_picket():
    """2 m of picket fence along +x: a post at its start, two rails behind, 75 mm pickets 120 mm apart, pointed."""
    span("Post", (-0.05, -0.4, -0.12), (0.05, 1.2, -0.02), "lx_picket_white", bevel=0.008)
    for y in (0.25, 0.85):
        span("Rail%.2f" % y, (0.0, y, -0.07), (2.0, y + 0.07, -0.025), "lx_picket_white", bevel=0.005)
    x = 0.06
    while x + 0.075 <= 2.0:
        prism("Picket%.2f" % x, [(x, 0.06), (x + 0.075, 0.06), (x + 0.075, 1.0), (x + 0.0375, 1.08), (x, 1.0)], 0.02, "lx_picket_white", axis="z", centre=-0.012, bevel=0.003)
        x += 0.12


@model("lx_palisade")
def lx_palisade():
    """2 m of steel palisade along +x, 1.8 m: a post, two angle rails, pales 65 mm wide 115 mm apart, each with
    three points (a slight fold down its middle)."""
    span("Post", (-0.05, -0.5, -0.12), (0.05, 1.85, -0.02), "lx_palisade_white", bevel=0.006)
    for y in (0.3, 1.45):
        span("Rail%.2f" % y, (0.0, y, -0.07), (2.0, y + 0.05, -0.02), "lx_palisade_white", bevel=0.004)
    x = 0.04
    while x + 0.065 <= 2.0:
        w = 0.065
        outline = [(x, 0.03), (x + w, 0.03), (x + w, 1.7), (x + w * 0.83, 1.78), (x + w * 0.67, 1.72), (x + w * 0.5, 1.82),
                   (x + w * 0.33, 1.72), (x + w * 0.17, 1.78), (x, 1.7)]
        prism("Pale%.2f" % x, outline, 0.012, "lx_palisade_white", axis="z", centre=-0.008, bevel=0.002)
        prism("Fold%.2f" % x, [(x + w * 0.4, 0.03), (x + w * 0.6, 0.03), (x + w * 0.6, 1.7), (x + w * 0.4, 1.7)], 0.03, "lx_palisade_white", axis="z", centre=0.0, bevel=0.002)
        x += 0.115


@model("lx_lamp_post")
def lx_lamp_post():
    cyl("Column", (0.0, 3.8, 0.0), 0.075, 8.4, "galvanised", verts=16, bevel=0.004, radius2=0.05)
    cyl("Base", (0.0, 0.5, 0.0), 0.11, 1.0, "galvanised", verts=16, bevel=0.01)
    span("Bracket", (-0.55, 7.85, -0.04), (0.55, 7.93, 0.04), "galvanised", bevel=0.01)
    for sx in (-1.0, 1.0):
        span("Head%+d" % sx, (sx * 0.55 - 0.22, 7.95, -0.15), (sx * 0.55 + 0.22, 8.03, 0.15), "steel_dark", bevel=0.01)
        span("Glass%+d" % sx, (sx * 0.55 - 0.2, 7.935, -0.13), (sx * 0.55 + 0.2, 7.95, 0.13), "lamp_lens", bevel=0.0)


@model("lx_cabinet")
def lx_cabinet():
    span("Plinth", (-0.55, -0.1, -0.35), (0.55, 0.1, 0.35), "concrete", bevel=0.01)
    span("Case", (-0.5, 0.1, -0.3), (0.5, 1.45, 0.3), "lx_case_grey", bevel=0.015)
    prism("Roof", [(-0.53, 1.45), (0.53, 1.45), (0.53, 1.5), (-0.53, 1.55)], 0.66, "lx_case_grey", axis="z", centre=0.0, bevel=0.006)
    for x in (-0.25, 0.25):
        span("Door%+.0f" % (x * 10), (x - 0.23, 0.15, 0.3), (x + 0.23, 1.4, 0.306), "lx_case_grey", bevel=0.003)
        span("Handle%+.0f" % (x * 10), (x + (0.17 if x < 0 else -0.19), 0.7, 0.306), (x + (0.19 if x < 0 else -0.17), 0.85, 0.325), "steel_dark", bevel=0.004)


def main():
    import bpy  # noqa: F401
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    spec = json.load(open(os.path.join(ROOT, "Data", "Props", "materials.json")))
    kit.set_materials(spec["materials"])
    want = [a for a in args if not a.startswith("-")] or sorted(MODELS)
    os.makedirs(OUT, exist_ok=True)
    for pid in want:
        kit.reset()
        MODELS[pid]()
        obj = kit.finish("SM_" + pid)
        lo, hi = kit.bounds(obj)
        path = os.path.join(OUT, pid + ".glb")
        kit.export(path)
        print("%s: %d faces, %.2f x %.2f x %.2f m -> %s" % (pid, len(obj.data.polygons), hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], path))


if __name__ == "__main__":
    main()
