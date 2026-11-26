"""Our own detailed lorries, modelled in Blender ([Nov 26] beenster's pictures of a 6x4 rigid box lorry with a tail-lift,
an articulated tractor unit with a tri-axle fridge semi-trailer, and a rigid fridge lorry with its fridge over the cab:
"please make an equivalent of this in blender"). Our own designs: no makers' badges, names, logos or liveries, and no
writing anywhere (the number plates are blank).

Pieces, all in the props' frame (metres, x along the length with the FRONT to +x, y across, z up from the ground),
each built into a street kit Model (Tools/blender_street_kit.py) at three levels of detail (lod 0 near, 1 middle, 2 far):

  tractor(M, xf, cab, lod)                 an articulated lorry's tractor unit, its bumper's front at x = xf: a sleeper cab
                                           2.5 m wide and 3.35 m to its roof with a roof deflector to 3.95, 4x2 axles
  trailer(M, xt, body, colour, lod, legs_down=False)
                                           a 13.6 m semi-trailer, its front face at x = xt (to xt - 13.6), its roof at
                                           4.0 m: body "reefer" (a fridge box, the fridge unit on its front face), "box" or
                                           "curtain" (a curtain-sider); tri-axle, side guards, landing legs (down to the
                                           ground when legs_down, for a trailer standing on its own), rear doors with a red
                                           reflective outline, hinges and lock bars, a rear bumper bar with its lamps
  rigid(M, xf, cab, colour, lod, reefer=False)
                                           a 6x4 rigid lorry, 10.0 m long from x = xf back: a day cab, a 7.4 m box body to
                                           4.0 m with a folded tail-lift at its back; reefer puts the fridge unit over the
                                           cab and gives it a plain fridge box

  artic_model(name, cab, body, colour, lod)    a whole articulated lorry, 16.5 m, its front at +8.25
  rigid_model(name, cab, colour, lod, reefer)  a whole rigid lorry, its front at +8.25 (so it stands in an artic's bay
                                               with its cab in line with theirs)
  trailer_model(name, body, colour, lod)       a semi-trailer standing on its landing legs, centred on 0, its REAR to +x
                                               (the warehouse yards' frame: backed onto a loading dock)

Every piece is closed (faces outward: Tools/check_prop_faces.py). Used by Tools/blender_services_props.py (the services
lorry park) and free for any other kit's yard.
"""
import math

import bpy  # noqa: F401  (before mathutils)
from mathutils import Matrix

import blender_street_kit as K
from blender_street_kit import box, lathe, rod

TYRE = (0.02, 0.02, 0.02)
DARK = (0.03, 0.03, 0.035)
CHASSIS = (0.05, 0.05, 0.055)
GALV = (0.5, 0.5, 0.49)
CHROME = (0.7, 0.7, 0.7)
GLASS = (0.05, 0.07, 0.08)
RUBBER = (0.04, 0.04, 0.04)
RED = (0.55, 0.03, 0.02)
AMBER = (0.6, 0.28, 0.02)
CLEAR = (0.7, 0.7, 0.66)
PLATE = (0.75, 0.62, 0.08)       # a blank rear plate: yellow, no writing
WHITE = (0.76, 0.76, 0.74)
GREY_CAB = (0.33, 0.35, 0.36)
NAVY = (0.04, 0.07, 0.2)
CURTAIN_GREY = (0.27, 0.28, 0.29)

TOP = 4.0          # every lorry's roof
FLOOR = 1.25       # the bodies' floors
TRAILER_LEN = 13.6
HALF = 1.275       # half a body's width (2.55 m)
R_TYRE = 0.52


def tone(c, k):
    return tuple(min(1.0, v * k) for v in c)


def prism(name, pts, y0, y1, bevel=0.0):
    """A solid from a side outline [(x, z)] (either way round) between y0 and y1."""
    n = len(pts)
    verts = [(x, y0, z) for x, z in pts] + [(x, y1, z) for x, z in pts]
    faces = [tuple(range(n)), tuple(range(2 * n - 1, n - 1, -1))]
    faces += [(k, (k + 1) % n, n + (k + 1) % n, n + k) for k in range(n)]
    ob = K._object(name, verts, faces, 30.0)
    if bevel > 0:
        m = ob.modifiers.new("bevel", "BEVEL")
        m.width = bevel
        m.segments = 2
        m.limit_method = "ANGLE"
        m.harden_normals = True
    return ob


def cylinder_x(name, x0, x1, y, z, r, seg):
    """A drum along x (a fuel tank, an air tank)."""
    ob = lathe(name, [(0.0, 0.0), (r, 0.0), (r, x1 - x0), (0.0, x1 - x0)], seg)
    ob.matrix_world = Matrix.Translation((x0, y, z)) @ Matrix.Rotation(math.radians(90), 4, "Y")
    return ob


def wheel(M, x, y, lod, width=0.3, chrome=True):
    """One wheel about (x, y, R_TYRE), its axle along y: a tyre with rounded shoulders and, nearer, the wheel's disc
    and hub on its outer face (y's side)."""
    seg = (16, 10, 6)[lod]
    w = width * 0.5
    ob = lathe("tyre", [(0.0, -w), (R_TYRE - 0.07, -w), (R_TYRE, -w + 0.06), (R_TYRE, w - 0.06), (R_TYRE - 0.07, w), (0.0, w)], seg)
    ob.matrix_world = Matrix.Translation((x, y, R_TYRE)) @ Matrix.Rotation(math.radians(90), 4, "X")
    M.add(ob, "paint", TYRE, 0.55)
    if lod == 2:
        return
    side = 1.0 if y >= 0 else -1.0
    disc = lathe("disc", [(0.0, -0.012), (0.3, -0.012), (0.3, 0.012), (0.0, 0.012)], seg)
    disc.matrix_world = Matrix.Translation((x, y + side * (w + 0.002), R_TYRE)) @ Matrix.Rotation(math.radians(90), 4, "X")
    M.add(disc, "steel" if chrome else "paint", CHROME if chrome else GALV, 0.35)
    if lod == 0:
        hub = lathe("hub", [(0.0, -0.06), (0.12, -0.06), (0.1, 0.06), (0.0, 0.06)], 10)
        hub.matrix_world = Matrix.Translation((x, y + side * (w + 0.05), R_TYRE)) @ Matrix.Rotation(math.radians(-90 * side), 4, "X")
        M.add(hub, "steel", CHROME, 0.3)
        for k in range(8):
            a = 2 * math.pi * k / 8
            M.add(box("nut", (x + 0.2 * math.cos(a), y + side * (w + 0.02), R_TYRE + 0.2 * math.sin(a)), (0.035, 0.03, 0.035)),
                  "steel", CHROME, 0.3)


def axle(M, x, lod, dual=False, width=0.3, chrome=True):
    """Both wheels of an axle (duals: two tyres a side), the outer faces 1.25 m out."""
    for sy in (-1, 1):
        if dual:
            wheel(M, x, sy * (1.25 - 0.14), lod, 0.28, chrome)
            if lod < 2:
                wheel(M, x, sy * (1.25 - 0.43), lod, 0.28, False)
        else:
            wheel(M, x, sy * (1.25 - width * 0.5), lod, width, chrome)


def mirrors(M, xf, top, lod):
    """The mirrors on their arms, both sides of the cab, and the kerb mirror over the near door."""
    for sy in (-1, 1):
        if lod < 2:
            M.add(rod("arm", (xf - 0.35, sy * 1.24, top - 0.35), (xf + 0.02, sy * 1.5, top - 0.35), 0.022), "paint", DARK, 0.3)
            M.add(rod("arm2", (xf - 0.35, sy * 1.24, top - 1.25), (xf + 0.02, sy * 1.5, top - 1.15), 0.022), "paint", DARK, 0.3)
        M.add(box("mirror", (xf + 0.05, sy * 1.53, top - 0.75), (0.1, 0.24, 0.62), bevel=0.02 if lod == 0 else 0.0), "paint", DARK, 0.3)
        if lod == 0:
            M.add(box("glass", (xf + 0.001, sy * 1.53, top - 0.75), (0.01, 0.2, 0.56)), "glaze", (0.25, 0.28, 0.3), 0.05)
            M.add(box("wide", (xf + 0.05, sy * 1.5, top - 1.35), (0.1, 0.22, 0.25)), "paint", DARK, 0.3)
    if lod == 0:
        M.add(rod("kerbarm", (xf - 0.1, 0.9, top + 0.02), (xf + 0.22, 1.0, top + 0.02), 0.02), "paint", DARK, 0.3)
        M.add(box("kerb", (xf + 0.25, 1.0, top - 0.12), (0.1, 0.3, 0.22)), "paint", DARK, 0.3)


def cab(M, xf, colour, lod, roof=3.35, deep=2.3, deflector=True):
    """A forward-control cab, its bumper's front at xf and its back at xf - deep: the cab on the chassis from 1.1 m
    up, its windscreen raked back, a sun visor over it, the grille, the bumper and headlamps, the steps under the doors,
    the doors' shut lines and handles, the side windows, the mirrors, and on a sleeper cab the roof deflector."""
    xb = xf - deep
    rake = 0.25
    sz = roof - 2.3
    M.add(prism("cab", [(xf, 1.1), (xf, 2.3), (xf - rake, roof), (xb, roof), (xb, 1.1)], -1.25, 1.25,
                bevel=0.06 if lod == 0 else 0.0), "paint", colour, 0.25)
    ang = math.atan2(rake, sz)
    if lod < 2:
        nx, nz = math.cos(ang), math.sin(ang)
        M.add(box("screen", (xf - rake * 0.5 + nx * 0.012, 0.0, 2.3 + sz * 0.5 + nz * 0.012), (0.02, 2.2, sz * 0.86),
                  rot=Matrix.Rotation(-ang, 4, "Y")), "glaze", GLASS, 0.05)
        M.add(box("visor", (xf - rake + 0.12, 0.0, roof - 0.05), (0.32, 2.4, 0.05), rot=Matrix.Rotation(math.radians(6), 4, "Y")),
              "paint", colour, 0.25)
        M.add(box("grille", (xf + 0.01, 0.0, 1.72), (0.02, 1.75, 0.82)), "paint", DARK, 0.4)
        if lod == 0:
            for k in range(5):
                M.add(box("slat", (xf + 0.025, 0.0, 1.4 + k * 0.16), (0.02, 1.7, 0.04)), "paint", tone(colour, 0.8), 0.4)
        for sy in (-1, 1):
            M.add(box("sidewin", (xb + deep * 0.62, sy * 1.256, roof - 0.55), (deep * 0.55, 0.02, 0.7)), "glaze", GLASS, 0.05)
            M.add(box("lamp", (xf + 0.13, sy * 0.95, 0.88), (0.03, 0.5, 0.18)), "lens", CLEAR, 0.1)
            M.add(box("indicator", (xf + 0.13, sy * 1.17, 0.88), (0.03, 0.1, 0.12)), "lens", AMBER, 0.1)
            M.add(box("step1", (xf - 0.75, sy * 1.17, 0.5), (0.55, 0.2, 0.05)), "paint", DARK, 0.5)
            M.add(box("step2", (xf - 0.75, sy * 1.17, 0.85), (0.55, 0.2, 0.05)), "paint", DARK, 0.5)
            M.add(box("stepside", (xf - 0.75, sy * 1.262, 0.68), (0.6, 0.025, 0.45)), "paint", tone(colour, 0.85), 0.5)
            if lod == 0:
                M.add(box("shut", (xf - 0.04, sy * 1.258, 1.95), (0.02, 0.012, 1.6)), "paint", DARK, 0.3)
                M.add(box("shut2", (xb + deep * 0.33, sy * 1.258, 2.1), (0.02, 0.012, 1.9)), "paint", DARK, 0.3)
                M.add(box("handle", (xb + deep * 0.42, sy * 1.262, 1.9), (0.16, 0.02, 0.04)), "paint", DARK, 0.3)
                M.add(box("grab", (xb + deep * 0.3, sy * 1.262, 1.6), (0.03, 0.02, 0.6)), "paint", DARK, 0.3)
        mirrors(M, xf, roof, lod)
    M.add(box("bumper", (xf + 0.04, 0.0, 0.85), (0.22, 2.5, 0.48), bevel=0.03 if lod == 0 else 0.0), "paint", tone(colour, 0.9), 0.4)
    if deflector:
        M.add(prism("deflector", [(xf - 0.55, roof - 0.01), (xb + 0.08, roof - 0.01), (xb + 0.08, TOP - 0.06), (xf - 1.35, TOP - 0.06)],
                    -1.15, 1.15, bevel=0.05 if lod == 0 else 0.0), "paint", colour, 0.2)
        if lod < 2:
            for sy in (-1, 1):
                M.add(box("collar", (xb - 0.12, sy * 1.15, 2.3), (0.24, 0.04, 1.9)), "paint", colour, 0.3)


def chassis(M, x0, x1, lod):
    """The two chassis rails from x0 back to x1 and the cross members."""
    for sy in (-1, 1):
        M.add(box("rail", ((x0 + x1) / 2, sy * 0.43, 0.95), (x0 - x1, 0.1, 0.28)), "paint", CHASSIS, 0.5)
    if lod < 2:
        n = max(2, int((x0 - x1) / 1.4))
        for k in range(n + 1):
            M.add(box("cross", (x1 + (x0 - x1) * k / n, 0.0, 0.95), (0.08, 0.8, 0.2)), "paint", CHASSIS, 0.5)


def guard(M, x, lod, colour=DARK, length=1.2):
    """A wheel's mudguard over the tyre's top, both sides."""
    if lod == 2:
        return
    for sy in (-1, 1):
        M.add(box("guard", (x, sy * 1.06, R_TYRE * 2 + 0.1), (length, 0.42, 0.04)), "paint", colour, 0.5)


def mudflaps(M, x, lod, width=0.55):
    if lod == 2:
        return
    for sy in (-1, 1):
        M.add(box("flap", (x, sy * (1.25 - width * 0.5), 0.55), (0.02, width, 0.7)), "paint", RUBBER, 0.6)


def side_guards(M, xa, xb, lod, bars=(0.55, 0.88)):
    """The side under-run guards from xa back to xb: two rails and their brackets up to the floor, both sides."""
    if lod == 2:
        return
    for sy in (-1, 1):
        y = sy * (HALF - 0.06)
        for z in bars:
            M.add(box("siderail", ((xa + xb) / 2, y, z), (xa - xb, 0.04, 0.1)), "paint", GALV, 0.5)
        n = max(1, int((xa - xb) / 2.0))
        for k in range(n + 1):
            M.add(box("bracket", (xb + 0.1 + (xa - xb - 0.2) * k / n, y + sy * -0.08, (bars[0] + FLOOR) / 2), (0.06, 0.12, FLOOR - bars[0] + 0.05)),
                  "paint", CHASSIS, 0.5)


def body_sides(M, x0, x1, colour, lod, style):
    """A box body from its front x0 back to x1, its floor at FLOOR and its roof at TOP: the body, its top and bottom
    rails, the corner posts, and the panels' seams (box and reefer) or the curtain's straps and buckles (curtain)."""
    L = x0 - x1
    xm = (x0 + x1) / 2
    h = TOP - FLOOR
    if style == "curtain":
        M.add(box("frame", (xm, 0.0, TOP - 0.06), (L, 2.55, 0.12)), "paint", GALV, 0.3)
        M.add(box("curtain", (xm, 0.0, FLOOR + h * 0.5 - 0.06), (L - 0.04, 2.5, h - 0.12)), "paint", colour, 0.3)
    else:
        M.add(box("body", (xm, 0.0, FLOOR + h * 0.5), (L, 2.55, h), bevel=0.035 if lod == 0 else 0.0), "paint", colour, 0.3)
    M.add(box("sill", (xm, 0.0, FLOOR - 0.1), (L, 2.52, 0.2)), "paint", CHASSIS, 0.5)
    if lod == 2:
        return
    rail = GALV if style != "box" or colour[0] > 0.5 else tone(colour, 0.8)
    for sy in (-1, 1):
        M.add(box("toprail", (xm, sy * (HALF + 0.006), TOP - 0.07), (L, 0.02, 0.12)), "paint", rail, 0.3)
        M.add(box("botrail", (xm, sy * (HALF + 0.006), FLOOR + 0.09), (L, 0.02, 0.18)), "paint", rail, 0.45)
        M.add(box("post", (x1 + 0.06, sy * (HALF + 0.004), FLOOR + h * 0.5), (0.12, 0.02, h)), "paint", GALV, 0.35)
        M.add(box("post", (x0 - 0.06, sy * (HALF + 0.004), FLOOR + h * 0.5), (0.12, 0.02, h)), "paint", GALV, 0.35)
        if style == "curtain":
            n = int(L / (0.68 if lod == 0 else 1.36))
            for k in range(1, n):
                x = x1 + k * L / n
                M.add(box("strap", (x, sy * 1.26, FLOOR + h * 0.45), (0.05, 0.012, h * 0.82)), "paint", tone(colour, 0.75), 0.3)
                if lod == 0:
                    M.add(box("buckle", (x, sy * 1.268, FLOOR + 0.22), (0.07, 0.012, 0.1)), "paint", GALV, 0.4)
        elif lod == 0:
            n = int(round(L / 1.13))
            for k in range(1, n):
                M.add(box("seam", (x1 + k * L / n, sy * (HALF + 0.004), FLOOR + h * 0.5), (0.03, 0.01, h - 0.3)), "paint",
                      tone(colour, 0.86), 0.3)
        if lod == 0:
            for k in range(1, int(L / 3.0) + 1):
                M.add(box("marker", (x1 + k * L / (int(L / 3.0) + 1), sy * (HALF + 0.015), FLOOR + 0.1), (0.1, 0.02, 0.05)), "lens", AMBER, 0.2)


def rear_end(M, x, colour, lod, tail_lift=False):
    """The body's back at x: the rear frame, two doors with a red reflective outline, the hinges and lock bars, and
    under it the bumper bar with its lamps and a blank plate (or a folded tail-lift doing the bumper's job)."""
    h = TOP - FLOOR
    zc = FLOOR + h * 0.5
    if lod == 2:
        M.add(box("bar", (x + 0.15, 0.0, 0.5), (0.12, 2.4, 0.14)), "paint", GALV, 0.5)
        return
    M.add(box("header", (x - 0.02, 0.0, TOP - 0.1), (0.04, 2.55, 0.2)), "paint", GALV, 0.35)
    M.add(box("sillr", (x - 0.02, 0.0, FLOOR + 0.06), (0.04, 2.55, 0.16)), "paint", GALV, 0.45)
    for sy in (-1, 1):
        M.add(box("cpost", (x - 0.02, sy * 1.2, zc), (0.04, 0.15, h)), "paint", GALV, 0.35)
        M.add(box("door", (x - 0.012, sy * 0.565, zc), (0.024, 1.11, h - 0.36)), "paint", tone(colour, 0.95), 0.3)
    # (the red reflective outline round the doors)
    for z in (TOP - 0.24, FLOOR + 0.18):
        M.add(box("redline", (x - 0.027, 0.0, z), (0.01, 2.2, 0.05)), "lens", RED, 0.15)
    for sy in (-1, 1):
        M.add(box("redline", (x - 0.027, sy * 1.1, zc), (0.01, 0.05, h - 0.42)), "lens", RED, 0.15)
    M.add(box("doorgap", (x - 0.026, 0.0, zc), (0.01, 0.02, h - 0.36)), "paint", DARK, 0.3)
    if lod == 0:
        for sy in (-1, 1):
            for k in range(4):
                M.add(box("hinge", (x - 0.035, sy * 1.07, FLOOR + 0.35 + k * (h - 0.7) / 3), (0.03, 0.14, 0.08)), "paint", GALV, 0.4)
            for yy in (0.25, 0.85):
                M.add(rod("lockbar", (x - 0.05, sy * yy, FLOOR + 0.12), (x - 0.05, sy * yy, TOP - 0.12), 0.022, 8), "paint", GALV, 0.4)
                M.add(box("lockhandle", (x - 0.08, sy * (yy + 0.07), FLOOR + 0.95), (0.03, 0.16, 0.05)), "paint", GALV, 0.4)
                for z in (FLOOR + 0.1, TOP - 0.1):
                    M.add(box("keeper", (x - 0.05, sy * yy, z), (0.06, 0.07, 0.07)), "paint", GALV, 0.4)
    if tail_lift:
        M.add(box("liftplate", (x + 0.1, 0.0, 0.8), (0.1, 2.4, 0.78)), "paint", GALV, 0.5)
        M.add(box("liftbar", (x + 0.2, 0.0, 0.45), (0.12, 2.42, 0.12)), "paint", GALV, 0.5)
        for sy in (-1, 1):
            M.add(box("liftarm", (x + 0.35, sy * 0.6, 0.85), (0.5, 0.1, 0.12)), "paint", CHASSIS, 0.5)
            M.add(box("lamps", (x + 0.06, sy * 1.0, 1.05), (0.03, 0.42, 0.12)), "lens", RED, 0.2)
            if lod == 0:
                M.add(box("amber", (x + 0.06, sy * 0.7, 1.05), (0.03, 0.14, 0.12)), "lens", AMBER, 0.2)
    else:
        M.add(box("bar", (x + 0.15, 0.0, 0.5), (0.12, 2.4, 0.14)), "paint", GALV, 0.5)
        M.add(box("lampbar", (x + 0.1, 0.0, 0.92), (0.06, 2.3, 0.16)), "paint", CHASSIS, 0.5)
        for sy in (-1, 1):
            M.add(box("strut", (x + 0.18, sy * 0.55, 0.72), (0.08, 0.08, 0.36)), "paint", CHASSIS, 0.5)
            M.add(box("lamps", (x + 0.065, sy * 0.92, 0.92), (0.02, 0.4, 0.12)), "lens", RED, 0.2)
            if lod == 0:
                M.add(box("amber", (x + 0.065, sy * 0.62, 0.92), (0.02, 0.14, 0.12)), "lens", AMBER, 0.2)
                M.add(box("reverse", (x + 0.065, sy * 0.48, 0.92), (0.02, 0.1, 0.12)), "lens", CLEAR, 0.2)
                M.add(box("reflector", (x + 0.205, sy * 0.95, 0.5), (0.01, 0.3, 0.06)), "lens", RED, 0.2)
        M.add(box("plate", (x + 0.135, 0.0, 0.92), (0.01, 0.52, 0.11)), "paint", PLATE, 0.2)


def fridge_unit(M, x, z0, z1, lod, half=0.95):
    """A fridge unit on a body's front face at x: its casing to +x and, on its face, the condenser's grilles."""
    M.add(box("fridge", (x + 0.24, 0.0, (z0 + z1) / 2), (0.48, half * 2, z1 - z0), bevel=0.05 if lod == 0 else 0.0), "paint", (0.7, 0.71, 0.7), 0.3)
    if lod == 2:
        return
    for sy in (-1, 1):
        M.add(box("grille", (x + 0.485, sy * half * 0.45, (z0 + z1) / 2 + 0.05), (0.01, half * 0.75, (z1 - z0) * 0.55)), "paint", DARK, 0.3)
    M.add(box("panel", (x + 0.485, 0.0, z0 + 0.14), (0.01, half * 1.6, 0.12)), "paint", (0.5, 0.51, 0.5), 0.3)
    if lod == 0:
        for sy in (-1, 1):
            fan = lathe("fan", [(0.0, -0.01), (0.22, -0.01), (0.22, 0.01), (0.0, 0.01)], 14)
            fan.matrix_world = Matrix.Translation((x + 0.495, sy * half * 0.45, (z0 + z1) / 2 + 0.05)) @ Matrix.Rotation(math.radians(90), 4, "Y")
            M.add(fan, "paint", (0.12, 0.12, 0.13), 0.3)


def tractor(M, xf, colour, lod):
    """An articulated lorry's 4x2 tractor unit: cab, chassis, fuel tank, air tanks and battery box, side skirts, the
    fifth wheel, the air lines, its front axle 1.4 m back and its driven axle (duals) 5.0 m back."""
    cab(M, xf, colour, lod)
    chassis(M, xf - 0.2, xf - 6.1, lod)
    axle(M, xf - 1.4, lod)
    axle(M, xf - 5.0, lod, dual=True)
    guard(M, xf - 5.0, lod, CHASSIS, 1.25)
    mudflaps(M, xf - 5.75, lod)
    M.add(box("fifth", (xf - 4.6, 0.0, 1.13), (1.0, 1.1, 0.08)), "paint", CHASSIS, 0.5)
    seg = (14, 9, 6)[lod]
    M.add(cylinder_x("tank", xf - 3.9, xf - 2.55, -0.85, 0.72, 0.31, seg), "steel", CHROME, 0.4)
    if lod < 2:
        M.add(box("battery", (xf - 3.2, 0.85, 0.75), (1.1, 0.5, 0.5)), "paint", DARK, 0.5)
        for sy in (-1, 1):
            M.add(box("skirt", (xf - 3.25, sy * 1.2, 0.72), (1.7, 0.06, 0.55)), "paint", colour, 0.4)
        M.add(cylinder_x("air", xf - 4.4, xf - 3.9, -0.7, 0.72, 0.15, seg), "paint", CHASSIS, 0.5)
    if lod == 0:
        for k, c in enumerate((RED, AMBER)):
            M.add(rod("line", (xf - 2.32, 0.2 + 0.1 * k, 2.0), (xf - 2.9, 0.25 + 0.1 * k, 1.85 - 0.1 * k), 0.018), "paint", c, 0.3)
        for sy in (-1, 1):
            M.add(box("tankstrap", (xf - 3.6 + sy * 0.4, -0.85, 0.72), (0.04, 0.66, 0.66)), "paint", CHASSIS, 0.4)


def trailer(M, xt, body, colour, lod, legs_down=False):
    """A 13.6 m semi-trailer from its front face at xt back to xt - 13.6, standing on its tri-axle at the back and,
    forward, on the tractor's fifth wheel or (legs_down) its own landing legs."""
    x1 = xt - TRAILER_LEN
    body_sides(M, xt, x1, colour, lod, "box" if body == "reefer" else body)
    if body == "reefer":
        fridge_unit(M, xt, 2.45, 3.85, lod)
    rear_end(M, x1, colour, lod)
    # (the underframe: the main beams, and at the front the coupling plate over the king pin)
    for sy in (-1, 1):
        M.add(box("beam", ((xt + x1) / 2, sy * 0.48, FLOOR - 0.3), (TRAILER_LEN - 0.4, 0.14, 0.24)), "paint", CHASSIS, 0.5)
    axles = [x1 + 1.75, x1 + 3.06, x1 + 4.37]
    for x in axles:
        axle(M, x, lod, width=0.38, chrome=False)
    if lod < 2:
        for sy in (-1, 1):
            M.add(box("valance", ((axles[0] + axles[-1]) / 2, sy * (HALF - 0.02), FLOOR - 0.17), (axles[-1] - axles[0] + 1.6, 0.04, 0.22)),
                  "paint", CHASSIS, 0.5)
            M.add(box("spray", ((axles[0] + axles[-1]) / 2, sy * 1.06, R_TYRE * 2 + 0.08), (axles[-1] - axles[0] + 1.5, 0.42, 0.04)),
                  "paint", CHASSIS, 0.5)
    mudflaps(M, axles[0] - 0.75, lod, 0.42)
    # (the landing legs: wound down to the ground standing alone, up off it when coupled)
    xl = xt - 2.4
    foot = 0.04 if legs_down else 0.38
    for sy in (-1, 1):
        M.add(box("leg", (xl, sy * 0.78, (FLOOR - 0.1 + foot) / 2), (0.13, 0.13, FLOOR - 0.1 - foot)), "paint", CHASSIS, 0.5)
        M.add(box("foot", (xl, sy * 0.78, foot - 0.02), (0.32, 0.32, 0.05)), "paint", CHASSIS, 0.5)
    if lod < 2:
        M.add(rod("brace", (xl, -0.78, 0.6), (xl, 0.78, FLOOR - 0.25), 0.03), "paint", CHASSIS, 0.5)
        M.add(box("bracebar", (xl, 0.0, 0.6), (0.06, 1.56, 0.06)), "paint", CHASSIS, 0.5)
    if lod == 0:
        M.add(box("gearbox", (xl, 0.92, 0.85), (0.16, 0.16, 0.2)), "paint", CHASSIS, 0.5)
        M.add(rod("crank", (xl, 1.0, 0.85), (xl + 0.35, 1.12, 0.6), 0.015), "paint", GALV, 0.4)
    side_guards(M, xl - 0.45, axles[-1] + 0.7, lod)
    # (the spare wheel in its cradle under the floor, a toolbox by it)
    if lod < 2:
        sw = lathe("spare", [(0.0, -0.15), (R_TYRE - 0.07, -0.15), (R_TYRE, -0.09), (R_TYRE, 0.09), (R_TYRE - 0.07, 0.15), (0.0, 0.15)], (14, 9, 6)[lod])
        sw.matrix_world = Matrix.Translation((xl - 2.6, 0.0, FLOOR - 0.6))
        M.add(sw, "paint", TYRE, 0.55)
        M.add(box("cradle", (xl - 2.6, 0.0, FLOOR - 0.78), (1.1, 1.1, 0.04)), "paint", CHASSIS, 0.5)
        M.add(box("toolbox", (xl - 4.4, -0.75, FLOOR - 0.35), (0.9, 0.5, 0.45)), "paint", GALV, 0.45)


def rigid(M, xf, colour, body_colour, lod, reefer=False):
    """A 6x4 rigid lorry: a day cab from xf back 2.3 m, the box body behind it from xf - 2.5 back to xf - 9.9, its
    floor on the chassis, its front axle 1.4 m back and its tandem (duals) under the body; a folded tail-lift at the
    back. reefer: the fridge unit over the cab on the body's front face, and no tail-lift."""
    cab(M, xf, colour, lod, roof=3.05, deflector=False)
    xb, xr = xf - 2.5, xf - 9.9
    chassis(M, xf - 0.2, xr + 0.2, lod)
    body_sides(M, xb, xr, body_colour, lod, "box")
    rear_end(M, xr, body_colour, lod, tail_lift=not reefer)
    if reefer:
        fridge_unit(M, xb, 3.12, 3.92, lod, half=0.9)
    else:
        M.add(box("bodyfront", (xb + 0.04, 0.0, (3.05 + TOP) / 2 + 0.02), (0.08, 2.55, TOP - 3.05 - 0.05)), "paint", tone(body_colour, 0.92), 0.3)
    axle(M, xf - 1.4, lod)
    for x in (xr + 2.35, xr + 3.7):
        axle(M, x, lod, dual=True)
    if lod < 2:
        for sy in (-1, 1):
            M.add(box("spray", (xr + 3.02, sy * 1.06, R_TYRE * 2 + 0.08), (2.9, 0.42, 0.04)), "paint", CHASSIS, 0.5)
    mudflaps(M, xr + 1.6, lod)
    side_guards(M, xf - 2.15, xr + 4.45, lod, bars=(0.55, 0.85))
    seg = (14, 9, 6)[lod]
    M.add(cylinder_x("tank", xf - 3.6, xf - 2.4, -0.85, 0.72, 0.28, seg), "steel", CHROME, 0.4)
    if lod < 2:
        M.add(box("battery", (xf - 3.0, 0.85, 0.75), (0.9, 0.5, 0.5)), "paint", DARK, 0.5)
    if reefer and lod < 2:
        M.add(box("fridgebox", (xf - 4.2, 0.85, 0.75), (0.8, 0.5, 0.55)), "paint", (0.7, 0.71, 0.7), 0.4)


def artic_model(name, cab_colour, body, colour, lod):
    M = K.Model(name)
    tractor(M, 8.25, cab_colour, lod)
    trailer(M, 8.25 - 2.9, body, colour, lod)
    return M


def rigid_model(name, cab_colour, colour, lod, reefer=False):
    M = K.Model(name)
    rigid(M, 8.25, cab_colour, colour, lod, reefer)
    return M


def trailer_model(name, body, colour, lod):
    """A semi-trailer on its own, standing on its landing legs: centred on 0 along its length, its rear to +x."""
    M = K.Model(name)
    trailer(M, TRAILER_LEN * 0.5, body, colour, lod, legs_down=True)
    M.transform(Matrix.Rotation(math.pi, 4, "Z"))
    return M
