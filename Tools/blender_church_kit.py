"""Ride UK's churches, stage 1 of "Churches that look like their place" (plan approved by beenster Nov 18,
https://claude.ai/artifact/HoAhwYfK2CpN5z5vzH87gP): the four first designs, modelled by script in Blender, to
become the kit's first parts in stage 2.

  dales_chapel     a small gritstone chapel of ease: low walls, a heavy stone slate roof, a bellcote on the
                   west gable, a south porch, small lancets, kneelers and a cross on the east gable
  norman_tower     a Norman village church: nave, a lower chancel, a squat west tower with a corbel table and
                   a low pyramid cap, small round-headed windows, a round-arched south door in a porch
  victorian_spire  a Victorian Gothic town church: tall nave with a steep roof, a north aisle under a lean-to,
                   a clerestory, buttresses, two-light traceried windows, a south-west tower with a broach spire
  methodist_chapel a Nonconformist chapel, gable to the street: a pedimented front with a blank date panel,
                   two tiers of tall round-headed windows, a central doorcase with a fanlight, quoins

Our own designs from general knowledge of English church building; no named building is copied. Blender's
frame: x east (the altar end), y north, z up, the west end at x = 0, the ground at z = 0; walls run 0.6 m
below the ground so they never stand proud of a slope.

    python Tools/blender_church_kit.py --render DIR     # a review sheet of the four in DIR

(Stage 2 writes the parts as glbs for the game, with the kit's shared material; stage 1 is the designs.)
"""
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from blender_street_kit import Model, _object, box, lathe  # noqa: E402

FOOT = -0.6   # (walls run on below the ground)

# Colours (linear): the stones and roofs the regions use.
GRIT = (0.16, 0.145, 0.12)        # Pennine gritstone, sooted
GRIT_DRESSED = (0.24, 0.22, 0.18)
STONE_SLATE = (0.13, 0.125, 0.11)
LIME = (0.42, 0.37, 0.27)         # a pale limestone (Norman church: rubble with dressed quoins)
LIME_DRESSED = (0.55, 0.5, 0.38)
SLATE = (0.07, 0.075, 0.085)
LEAD = (0.2, 0.21, 0.22)
ROCKFACE = (0.3, 0.26, 0.2)       # Victorian rock-faced sandstone
BATH = (0.5, 0.44, 0.32)          # its dressed stone
BRICK = (0.28, 0.1, 0.06)         # the chapel's red brick
STUCCO = (0.55, 0.53, 0.48)       # the chapel's painted stucco dressings
GLASS = (0.025, 0.03, 0.035)
DOOR_OAK = (0.09, 0.05, 0.025)
DOOR_PAINT = (0.04, 0.07, 0.05)


# --- shapes ----------------------------------------------------------------------------------------------

def prism(name, outline, depth):
    """A flat shape (a convex outline [(u, v)] in the plane, anticlockwise) given depth along -y: the
    window's and the wall's own frame is u along x, v up z, its face at y = 0 and its back at y = -depth."""
    n = len(outline)
    verts = [(u, 0.0, v) for u, v in outline] + [(u, -depth, v) for u, v in outline]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, 2 * n))]
    faces += [(k, (k + 1) % n, n + (k + 1) % n, n + k) for k in range(n)]
    return _object(name, verts, faces, 30.0)


def arch(w, spring, kind, seg=8):
    """An opening's outline w wide, its jambs `spring` high, its head round, pointed (equilateral) or flat."""
    h = w * 0.5
    pts = [(-h, 0.0), (h, 0.0)]
    if kind == "flat":
        return pts + [(h, spring), (-h, spring)]
    if kind == "round":
        for k in range(seg + 1):
            a = math.pi * k / seg
            pts.append((h * math.cos(a), spring + h * math.sin(a)))
        return pts[:2] + pts[2:]
    # (pointed: each side an arc of radius w about the other springing point)
    right = []
    for k in range(seg + 1):
        a = math.radians(60.0) * k / seg
        right.append((-h + w * math.cos(a), spring + w * math.sin(a)))
    left = [(-x, z) for x, z in reversed(right)]
    return pts[:2] + right[1:] + left[1:-1]


def place(ob, at, facing):
    """Turn a shape made facing -y (its face at y = 0) to face `facing` (a unit vector in the ground plane)
    and move it to `at`."""
    yaw = math.atan2(facing[1], facing[0]) + math.pi * 0.5
    ob.matrix_world = Matrix.Translation(Vector(at)) @ Matrix.Rotation(yaw, 4, "Z") @ ob.matrix_world
    return ob


def ring(name, outer, inner, depth):
    """A surround: the band between two outlines of the same number of points (an opening's, and that
    opening widened), open along their first edge (the sill), given depth along -y from y = 0."""
    n = len(outer)
    v = [(u, 0.0, z) for u, z in outer] + [(u, 0.0, z) for u, z in inner]
    v += [(u, -depth, z) for u, z in outer] + [(u, -depth, z) for u, z in inner]
    O, I, Ob, Ib = 0, n, 2 * n, 3 * n
    f = []
    for k in range(1, n):
        a, b = k, (k + 1) % n
        f.append((O + a, O + b, I + b, I + a))          # (its face)
        f.append((Ob + b, Ob + a, Ib + a, Ib + b))      # (its back)
        f.append((O + b, O + a, Ob + a, Ob + b))        # (its outer edge)
        f.append((I + a, I + b, Ib + b, Ib + a))        # (its inner edge, the reveal's lip)
    for a in (1, 0):   # (the two ends at the sill)
        f.append((O + a, I + a, Ib + a, Ob + a) if a == 1 else (I + a, O + a, Ob + a, Ib + a))
    me = bpy.data.meshes.new(name)
    me.from_pydata(v, [], f)
    me.validate()
    me.update()
    for poly in me.polygons:
        poly.use_smooth = False
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def sleeve(name, outline, depth):
    """The reveal: the opening's sides carried back into the wall, depth along -y, facing in."""
    n = len(outline)
    v = [(u, 0.0, z) for u, z in outline] + [(u, -depth, z) for u, z in outline]
    f = [((k + 1) % n, k, n + k, n + (k + 1) % n) for k in range(1, n)]
    me = bpy.data.meshes.new(name)
    me.from_pydata(v, [], f)
    me.validate()
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def opening(M, name, at, facing, w, spring, kind, surround, stone, glass=GLASS, reveal=0.12, mullions=0, door=None, surface="stone"):
    """A window or door in a wall: the dark glass (or the door's leaf) set back `reveal` in the wall, the
    reveal's sides, and its dressed stone surround standing 4 cm proud round it. at: the opening's foot on
    the wall's face."""
    hole = arch(w, spring, kind)
    big = [(u * (w + 2 * surround) / w, z) for u, z in hole]
    big = [(u, z + (surround if z > spring + 1e-6 else 0.0) * (z - spring) / max(1e-6, max(zz for _, zz in hole) - spring)) for u, z in big]
    sur = ring(name + "_sur", big, hole, 0.1)
    sur.matrix_world = Matrix.Translation((0, 0.04, 0)) @ sur.matrix_world
    M.add(place(sur, at, facing), surface, stone, 0.3)
    rev = sleeve(name + "_in", hole, reveal + 0.05)
    rev.matrix_world = Matrix.Translation((0, 0.05, 0)) @ rev.matrix_world
    M.add(place(rev, at, facing), surface, tuple(c * 0.55 for c in stone), 0.4)   # (the reveal, in shade)
    leaf = prism(name + "_g", hole, 0.03)
    leaf.matrix_world = Matrix.Translation((0, -reveal + 0.05, 0)) @ leaf.matrix_world
    M.add(place(leaf, at, facing), "glass" if door is None else "timber", glass if door is None else door, 0.1)
    # (stone mullions dividing the lights, and on a pointed window a Y of tracery over them)
    if mullions:
        for k in range(1, mullions + 1):
            u = -w * 0.5 + w * k / (mullions + 1)
            mb = box(name + "_mul%d" % k, (u, -reveal * 0.5 + 0.05, spring * 0.5 + (0.0 if kind == "flat" else w * 0.2)),
                     (0.09, reveal, spring + (0.0 if kind == "flat" else w * 0.4)))
            M.add(place(mb, at, facing), surface, stone, 0.3)
    return M


def gable_wall(w, eaves, ridge):
    """A gable end's outline (u across, v up): a pentagon from the foot to the ridge."""
    h = w * 0.5
    return [(-h, FOOT), (h, FOOT), (h, eaves), (0.0, ridge), (-h, eaves)]


def range_block(M, name, x0, x1, w, eaves, pitch_deg, wall, roof, roof_thick=0.18, overhang=0.25, verge=0.12,
                ends=(True, True), ridge_tiles=None, wall_surface="stone", roof_surface="roof"):
    """A gabled range along x from x0 to x1, w wide: its walls (the gable ends as pentagons) and its roof of
    two thick slabs with eaves overhang and a little verge past each gable."""
    h = w * 0.5
    rise = h * math.tan(math.radians(pitch_deg))
    ridge = eaves + rise
    # (the walls as one closed solid: a pentagon prism along x)
    outline = [(-h, FOOT), (h, FOOT), (h, eaves), (0.0, ridge), (-h, eaves)]
    n = len(outline)
    verts = [(x0, y, z) for y, z in outline] + [(x1, y, z) for y, z in outline]
    faces = [tuple(range(n)), tuple(range(2 * n - 1, n - 1, -1))] + [(k, n + k, n + (k + 1) % n, (k + 1) % n) for k in range(n)]
    M.add(_object(name + "_walls", verts, faces), wall_surface, wall, 0.35)
    # (the roof: each slope a slab from the ridge out past the eaves)
    a = math.radians(pitch_deg)
    xa, xb = x0 - (verge if ends[0] else 0.0), x1 + (verge if ends[1] else 0.0)
    for s in (-1, 1):
        ox = (h + overhang) * s
        oz = eaves - overhang * math.tan(a)
        top = [(0.0, ridge + roof_thick / math.cos(a)), (ox, oz + roof_thick / math.cos(a))]
        bot = [(0.0, ridge), (ox, oz)]
        quad = [top[0], top[1], bot[1], bot[0]]
        v = [(xa, y, z) for y, z in quad] + [(xb, y, z) for y, z in quad]
        f = [(0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
        M.add(_object("%s_roof%d" % (name, s), v, f), roof_surface, roof, 0.3)
    if ridge_tiles is not None:
        rb = box(name + "_ridge", ((xa + xb) * 0.5, 0.0, ridge + roof_thick / math.cos(a) + 0.04), (xb - xa, 0.22, 0.1))
        M.add(rb, "roof", ridge_tiles, 0.3)
    return ridge


def coping(M, name, x, w, eaves, ridge, stone, thick=0.3, proud=0.08, kneeler=True):
    """Stone copings up a gable's two slopes (the verge stands proud of the roof) and kneelers at the eaves."""
    h = w * 0.5
    for s in (-1, 1):
        a = Vector((x, s * (h + 0.1), eaves))
        b = Vector((x, 0.0, ridge))
        d = b - a
        L = d.length
        mid = (a + b) * 0.5
        ang = math.atan2(d.z, -s * d.y)
        R = Matrix.Rotation(-s * ang, 4, "X")
        cb = box("%s_cope%d" % (name, s), (0, 0, 0), (thick, L + 0.15, 0.22))
        cb.matrix_world = Matrix.Translation(mid + Vector((0, 0, 0.14))) @ R
        M.add(cb, "stone", stone, 0.35)
        if kneeler:
            M.add(box("%s_kneel%d" % (name, s), (x, s * (h + 0.05), eaves - 0.1), (thick + 0.05, 0.5, 0.4)), "stone", stone, 0.35)


def battlements(M, name, cx, cy, side, z, stone, merlon=0.55, crenel=0.45, high=0.7, thick=0.35, pinnacles=0.0):
    """A parapet round a square top: a string course, the wall, merlons; corner pinnacles if asked."""
    M.add(box(name + "_string", (cx, cy, z + 0.08), (side + 0.16, side + 0.16, 0.16)), "stone", stone, 0.3)
    h = side * 0.5
    for s in range(4):
        dx, dy = [(1, 0), (0, 1), (-1, 0), (0, -1)][s]
        # (each side's parapet: a low wall, then merlons along it)
        along = (-dy, dx)
        cxs, cys = cx + dx * (h - thick * 0.5), cy + dy * (h - thick * 0.5)
        size = (thick if dx else side, side if dx else thick, high * 0.45)
        M.add(box("%s_par%d" % (name, s), (cxs, cys, z + 0.16 + high * 0.225), size), "stone", stone, 0.3)
        n = max(2, int((side - thick) / (merlon + crenel)))
        step = (side - thick) / n
        for k in range(n):
            t = -h + thick * 0.5 + step * (k + 0.5)
            px, py = cxs + along[0] * t, cys + along[1] * t
            ms = (thick if dx else merlon, merlon if dx else thick, high * 0.55)
            M.add(box("%s_mer%d_%d" % (name, s, k), (px, py, z + 0.16 + high * 0.45 + high * 0.275), ms), "stone", stone, 0.3)
    if pinnacles > 0:
        for sx in (-1, 1):
            for sy in (-1, 1):
                px, py = cx + sx * (h - 0.2), cy + sy * (h - 0.2)
                M.add(box("%s_pin%d%d" % (name, sx, sy), (px, py, z + 0.16 + high + 0.3), (0.4, 0.4, 0.6)), "stone", stone, 0.3)
                M.add(lathe("%s_pint%d%d" % (name, sx, sy), [(0.2, 0.0), (0.0, pinnacles)], seg=4, at=(px, py, z + 0.76 + high), phase=math.pi / 4),
                      "stone", stone, 0.3)


def buttress(M, name, at, facing, high, deep=0.6, wide=0.5, stone=GRIT):
    """A stepped buttress against a wall: two stages, each with a sloped weathering on top."""
    fx, fy = facing
    for k, (d, h0, h1) in enumerate(((deep, FOOT, high * 0.55), (deep * 0.6, high * 0.55, high))):
        c = (at[0] + fx * d * 0.5, at[1] + fy * d * 0.5, (h0 + h1) * 0.5)
        b = box("%s_%d" % (name, k), c, (d if abs(fx) > 0.5 else wide, wide if abs(fx) > 0.5 else d, h1 - h0))
        M.add(b, "stone", stone, 0.35)
        cap = box("%s_w%d" % (name, k), (c[0], c[1], h1 + 0.08), (d if abs(fx) > 0.5 else wide, wide if abs(fx) > 0.5 else d, 0.16))
        tilt = Matrix.Rotation(math.radians(-30) * (fx if abs(fx) > 0.5 else 0), 4, "Y") @ Matrix.Rotation(math.radians(30) * (fy if abs(fy) > 0.5 else 0), 4, "X")
        cap.matrix_world = cap.matrix_world @ tilt
        M.add(cap, "stone", stone, 0.35)


def cross(M, name, at, stone, size=0.7):
    x, y, z = at
    M.add(box(name + "_v", (x, y, z + size * 0.5), (0.12, 0.12, size)), "stone", stone, 0.3)
    M.add(box(name + "_h", (x, y, z + size * 0.68), (0.12, size * 0.6, 0.12)), "stone", stone, 0.3)


def porch(M, name, px, wall_y, pw, pd, eaves, pitch, wall, roof, dressed, kind, door_w, wall_surface="stone", roof_surface="roof"):
    """A gabled south porch in front of a wall at y = wall_y: its two side walls, its gable face with the
    outer arch, its roof; the inner door in the church's wall."""
    y0 = wall_y - pd
    for s in (-1, 1):
        M.add(box("%s_w%d" % (name, s), (px + s * (pw * 0.5 - 0.2), wall_y - pd * 0.5, (FOOT + eaves) * 0.5), (0.4, pd, eaves - FOOT)), wall_surface, wall, 0.35)
    pr = eaves + (pw * 0.5) * math.tan(math.radians(pitch))
    face = prism(name + "_face", [(-pw * 0.5, eaves), (pw * 0.5, eaves), (0.0, pr)], 0.4)
    M.add(place(face, (px, y0, 0.0), SOUTH), wall_surface, wall, 0.35)
    for s in (-1, 1):
        v = [(px, y0 - 0.15, pr + 0.02), (px + s * (pw * 0.5 + 0.2), y0 - 0.15, eaves - 0.2 * math.tan(math.radians(pitch))),
             (px + s * (pw * 0.5 + 0.2), wall_y + 0.05, eaves - 0.2 * math.tan(math.radians(pitch))), (px, wall_y + 0.05, pr + 0.02)]
        v = v + [(x, y, z + 0.16) for x, y, z in v]
        f = [(0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
        M.add(_object("%s_roof%d" % (name, s), v, f), roof_surface, roof, 0.3)
    opening(M, name + "_arch", (px, y0, 0.0), SOUTH, door_w + 0.2, 1.8, kind, 0.2, dressed, door=(0.01, 0.01, 0.01))
    opening(M, name + "_door", (px, wall_y, 0.0), SOUTH, door_w, 1.8, kind, 0.15, dressed, door=DOOR_OAK)


SOUTH, NORTH, WEST, EAST = (0.0, -1.0), (0.0, 1.0), (-1.0, 0.0), (1.0, 0.0)


# --- the four designs ------------------------------------------------------------------------------------

def dales_chapel():
    """A small chapel of ease in the Dales: 14 m by 6.4 m, walls 3.6 m, a 40-degree stone slate roof."""
    M = Model("SM_Church_dales_chapel")
    L, W, E, P = 14.0, 6.4, 3.6, 40.0
    ridge = range_block(M, "nave", 0.0, L, W, E, P, GRIT, STONE_SLATE, roof_thick=0.22, overhang=0.15, verge=0.0)
    for x in (0.0, L):
        coping(M, "g%d" % int(x), x, W, E, ridge, GRIT_DRESSED)
    # (quoins: dressed stone at the corners, long and short)
    for x in (0.0, L):
        for y in (-W * 0.5, W * 0.5):
            for k in range(7):
                z = FOOT + 0.6 + k * 0.6
                big = k % 2 == 0
                M.add(box("q%d%d%d" % (int(x), int(y), k), (x + (0.18 if x == 0 else -0.18) * (1 if big else 0.5), y + (0.18 if y < 0 else -0.18) * (0.5 if big else 1), z + 0.25),
                          (0.42 if big else 0.25, 0.25 if big else 0.42, 0.28)), "stone", GRIT_DRESSED, 0.3)
    # (a double bellcote on the west gable's apex, one bell hanging)
    bx = 0.0
    M.add(box("bell_base", (bx, 0.0, ridge + 0.1), (0.5, 1.3, 0.5)), "stone", GRIT_DRESSED, 0.3)
    for s in (-1, 1):
        M.add(box("bell_pier%d" % s, (bx, s * 0.5, ridge + 0.85), (0.45, 0.3, 1.1)), "stone", GRIT_DRESSED, 0.3)
    M.add(box("bell_head", (bx, 0.0, ridge + 1.5), (0.5, 1.3, 0.25)), "stone", GRIT_DRESSED, 0.3)
    gab = [(-0.75, 0.0), (0.75, 0.0), (0.0, 0.55)]
    gb = prism("bell_gable", gab, 0.5)
    gb.matrix_world = Matrix.Translation((bx + 0.25, 0.0, ridge + 1.62)) @ Matrix.Rotation(math.pi * 0.5, 4, "Z") @ gb.matrix_world
    M.add(gb, "stone", GRIT_DRESSED, 0.3)
    M.add(lathe("bell", [(0.0, 0.0), (0.2, 0.05), (0.14, 0.3), (0.0, 0.34)], seg=10, at=(bx, 0.0, ridge + 0.55)), "steel", (0.25, 0.18, 0.08), 0.5)
    cross(M, "ecross", (L, 0.0, ridge + 0.2), GRIT_DRESSED)
    porch(M, "porch", 2.6, -W * 0.5, 2.6, 2.4, 2.4, 45.0, GRIT, STONE_SLATE, GRIT_DRESSED, "pointed", 1.0)
    # (lancets: three down the south side, two on the north, a triple lancet in the east gable)
    for x in (6.0, 9.0, 12.0):
        opening(M, "s%d" % int(x), (x, -W * 0.5, 1.2), SOUTH, 0.55, 1.3, "pointed", 0.14, GRIT_DRESSED)
    for x in (4.5, 10.0):
        opening(M, "n%d" % int(x), (x, W * 0.5, 1.2), NORTH, 0.55, 1.3, "pointed", 0.14, GRIT_DRESSED)
    for u, h in ((-0.75, 1.5), (0.0, 1.9), (0.75, 1.5)):
        opening(M, "e%.1f" % u, (L, -u, 1.1), EAST, 0.45, h, "pointed", 0.12, GRIT_DRESSED)
    opening(M, "w", (0.0, 0.0, 2.2), WEST, 0.5, 1.0, "pointed", 0.14, GRIT_DRESSED)
    return M


def norman_tower():
    """A Norman village church: nave 17 m by 7.5 m, a chancel 8 m by 5.5 m, a west tower 5.4 m square and
    14 m to its parapet, a low pyramid cap. Limestone rubble with dressed quoins and round-headed openings."""
    M = Model("SM_Church_norman_tower")
    T, NL, NW, NE = 5.4, 17.0, 7.5, 5.2
    CL, CW, CE = 8.0, 5.5, 4.4
    x0 = T
    nr = range_block(M, "nave", x0, x0 + NL, NW, NE, 48.0, LIME, SLATE, overhang=0.2, verge=0.0, ridge_tiles=SLATE)
    cr = range_block(M, "chancel", x0 + NL, x0 + NL + CL, CW, CE, 48.0, LIME, SLATE, overhang=0.2, verge=0.0, ridge_tiles=SLATE)
    coping(M, "ce", x0 + NL + CL, CW, CE, cr, LIME_DRESSED, kneeler=False)
    coping(M, "ne", x0 + NL, NW, NE, nr, LIME_DRESSED, kneeler=False)
    cross(M, "cx", (x0 + NL + CL, 0.0, cr + 0.15), LIME_DRESSED, 0.6)
    # (the tower: a little battered at its foot, a corbel table under its parapet, belfry openings on all four
    # faces, a low lead pyramid cap behind a plain parapet)
    TH = 14.0
    tc = T * 0.5
    M.add(box("tower", (tc, 0.0, (FOOT + TH) * 0.5), (T, T, TH - FOOT)), "stone", LIME, 0.35)
    M.add(box("plinth", (tc, 0.0, (FOOT + 0.8) * 0.5), (T + 0.3, T + 0.3, 0.8 - FOOT)), "stone", LIME_DRESSED, 0.4)
    for k in range(9):
        for s, (dx, dy) in enumerate(((1, 0), (-1, 0), (0, 1), (0, -1))):
            t = -T * 0.5 + T * (k + 0.5) / 9
            px = tc + dx * (T * 0.5 + 0.1) + (-dy) * t * (1 if dy else 0)
            py = dy * (T * 0.5 + 0.1) + dx * t * (1 if dx else 0)
            if dx:
                px, py = tc + dx * (T * 0.5 + 0.1), t
            else:
                px, py = tc + t, dy * (T * 0.5 + 0.1)
            M.add(box("corbel%d_%d" % (s, k), (px, py, TH - 0.55), (0.25, 0.25, 0.25)), "stone", LIME_DRESSED, 0.35)
    M.add(box("parapet_ledge", (tc, 0.0, TH - 0.35), (T + 0.35, T + 0.35, 0.18)), "stone", LIME_DRESSED, 0.3)
    M.add(box("parapet", (tc, 0.0, TH + 0.35), (T + 0.25, T + 0.25, 1.2)), "stone", LIME, 0.35)
    M.add(lathe("cap", [(T * 0.66, 0.0), (0.0, 2.2)], seg=4, at=(tc, 0.0, TH + 0.6), phase=math.pi / 4), "roof", LEAD, 0.3)
    M.add(lathe("vane", [(0.04, 0.0), (0.03, 1.4), (0.0, 1.45)], seg=6, at=(tc, 0.0, TH + 2.7)), "steel", (0.05, 0.05, 0.05), 0.2)
    for f, at in ((SOUTH, (tc, -T * 0.5, 0.0)), (NORTH, (tc, T * 0.5, 0.0)), (WEST, (0.0, 0.0, 0.0))):
        opening(M, "bel%s" % str(f), (at[0], at[1], 10.6), f, 0.75, 1.1, "round", 0.18, LIME_DRESSED, mullions=1)
        opening(M, "slit%s" % str(f), (at[0], at[1], 5.5), f, 0.25, 0.9, "round", 0.14, LIME_DRESSED)
    opening(M, "belE", (T, 0.0, 10.6), EAST, 0.75, 1.1, "round", 0.18, LIME_DRESSED, mullions=1)
    opening(M, "west", (0.0, 0.0, 1.9), WEST, 0.6, 1.4, "round", 0.18, LIME_DRESSED)
    # (nave and chancel: small round-headed windows, a south door with a porch, a priest's door)
    for x in (x0 + 4.5, x0 + 8.5, x0 + 13.5):
        opening(M, "ns%d" % int(x), (x, -NW * 0.5, 2.0), SOUTH, 0.55, 1.4, "round", 0.18, LIME_DRESSED)
        opening(M, "nn%d" % int(x), (x, NW * 0.5, 2.0), NORTH, 0.55, 1.4, "round", 0.18, LIME_DRESSED)
    for x in (x0 + NL + 2.5, x0 + NL + 5.5):
        opening(M, "cs%d" % int(x), (x, -CW * 0.5, 1.6), SOUTH, 0.5, 1.3, "round", 0.16, LIME_DRESSED)
    opening(M, "east", (x0 + NL + CL, 0.0, 1.6), EAST, 0.7, 1.8, "round", 0.2, LIME_DRESSED)
    opening(M, "priest", (x0 + NL + 4.0, -CW * 0.5, 0.0), SOUTH, 0.8, 1.7, "round", 0.14, LIME_DRESSED, door=DOOR_OAK)
    porch(M, "porch", x0 + 2.8, -NW * 0.5, 3.2, 3.0, 2.6, 45.0, LIME, SLATE, LIME_DRESSED, "round", 1.1)
    for x in (x0, x0 + NL):
        for y in (-NW * 0.5, NW * 0.5):
            for k in range(8):
                z = FOOT + 0.6 + k * 0.65
                big = k % 2 == 0
                if x == x0 and abs(y) < T * 0.5 + 0.1:
                    continue
                M.add(box("q%.0f%.0f%d" % (x, y, k), (x + (0.18 if x == x0 else -0.18) * (1 if big else 0.5), y + (0.18 if y < 0 else -0.18) * (0.5 if big else 1), z + 0.25),
                          (0.42 if big else 0.25, 0.25 if big else 0.42, 0.3)), "stone", LIME_DRESSED, 0.3)
    return M


def victorian_spire():
    """A Victorian Gothic town church: nave 24 m by 8.5 m, walls 7.5 m under a 55-degree roof, a north aisle,
    a clerestory, a chancel, and a south-west tower 6 m square, 17 m to its broach spire's foot, 40 m in all."""
    M = Model("SM_Church_victorian_spire")
    NL, NW, NE = 24.0, 8.5, 7.5
    AW, AE = 4.2, 4.0
    CL, CW, CE = 9.0, 7.0, 6.5
    nr = range_block(M, "nave", 0.0, NL, NW, NE, 55.0, ROCKFACE, SLATE, overhang=0.3, verge=0.0, ridge_tiles=(0.18, 0.06, 0.04))
    cr = range_block(M, "chancel", NL, NL + CL, CW, CE, 55.0, ROCKFACE, SLATE, overhang=0.3, verge=0.0, ridge_tiles=(0.18, 0.06, 0.04))
    coping(M, "nw", 0.0, NW, NE, nr, BATH, kneeler=True)
    coping(M, "ne", NL, NW, NE, nr, BATH, kneeler=False)
    coping(M, "ce", NL + CL, CW, CE, cr, BATH, kneeler=True)
    cross(M, "wx", (0.0, 0.0, nr + 0.2), BATH, 1.0)
    cross(M, "ex", (NL + CL, 0.0, cr + 0.2), BATH, 1.0)
    # (the north aisle under a lean-to roof against the nave's wall, below the clerestory)
    ay = NW * 0.5 + AW * 0.5
    M.add(box("aisle", (NL * 0.5 + 0.5, ay, (FOOT + AE) * 0.5), (NL - 1.0, AW, AE - FOOT)), "stone", ROCKFACE, 0.35)
    a = math.atan2(NE - 1.4 - AE, AW)
    roof = box("aisle_roof", (0, 0, 0), (NL - 0.6, math.hypot(AW + 0.3, NE - 1.4 - AE) + 0.1, 0.18))
    roof.matrix_world = Matrix.Translation((NL * 0.5 + 0.5, NW * 0.5 + AW * 0.5 + 0.1, (AE + NE - 1.4) * 0.5 + 0.12)) @ Matrix.Rotation(-a, 4, "X")
    M.add(roof, "roof", SLATE, 0.3)
    # (the clerestory: small paired lancets over the aisle roof; the aisle's own two-light windows)
    for x in range(3, int(NL) - 1, 4):
        opening(M, "cl%d" % x, (x + 0.5, NW * 0.5, NE - 1.25), NORTH, 0.5, 0.6, "pointed", 0.1, BATH)
        opening(M, "aw%d" % x, (x + 0.5, NW * 0.5 + AW, 1.0), NORTH, 1.1, 1.6, "pointed", 0.14, BATH, mullions=1)
    # (the south wall: tall two-light windows between buttresses)
    for k, x in enumerate(range(8, int(NL), 4)):
        opening(M, "sw%d" % x, (x + 1.0, -NW * 0.5, 1.6), SOUTH, 1.3, 3.2, "pointed", 0.16, BATH, mullions=1)
        buttress(M, "sb%d" % k, (x - 1.0, -NW * 0.5, 0.0), SOUTH, NE - 0.6, stone=BATH)
    for k, x in enumerate((NL + 3.0, NL + 6.0)):
        opening(M, "cs%d" % k, (x, -CW * 0.5, 1.4), SOUTH, 1.0, 2.6, "pointed", 0.14, BATH, mullions=1)
    opening(M, "east", (NL + CL, 0.0, 1.6), EAST, 3.0, 3.6, "pointed", 0.22, BATH, mullions=3)
    opening(M, "west", (0.0, 0.0, 2.6), WEST, 2.4, 3.0, "pointed", 0.2, BATH, mullions=2)
    for y in (-NW * 0.5, NW * 0.5):
        buttress(M, "wb%.0f" % y, (0.0, y * 0.8, 0.0), WEST, NE - 0.4, stone=BATH)
    for y in (-CW * 0.5, CW * 0.5):
        buttress(M, "eb%.0f" % y, (NL + CL, y * 0.8, 0.0), EAST, CE - 0.4, stone=BATH)
    # (the south-west tower: four stages with string courses and angle buttresses, a west door, a belfry of
    # paired louvred lancets, a broach spire with lucarnes)
    T, TH = 6.0, 17.0
    tx, ty = T * 0.5 - 0.5, -NW * 0.5 - T * 0.5 + 0.8
    M.add(box("tower", (tx, ty, (FOOT + TH) * 0.5), (T, T, TH - FOOT)), "stone", ROCKFACE, 0.35)
    for z in (0.9, 6.0, 11.0, TH - 0.1):
        M.add(box("tstring%.0f" % z, (tx, ty, z), (T + 0.2, T + 0.2, 0.2)), "stone", BATH, 0.3)
    for sx in (-1, 1):
        for sy in (-1, 1):
            for f in ((sx, 0), (0, sy)):
                buttress(M, "tb%d%d%d%d" % (sx, sy, f[0], f[1]), (tx + sx * (T * 0.5 - 0.35), ty + sy * (T * 0.5 - 0.35), 0.0) if False else
                         (tx + sx * T * 0.5 * (1 if f[0] else 0.75), ty + sy * T * 0.5 * (1 if f[1] else 0.75), 0.0), (float(f[0]), float(f[1])), 11.5, deep=0.7, wide=0.55, stone=BATH)
    opening(M, "tdoor", (tx - T * 0.5, ty, 0.0), WEST, 1.5, 2.2, "pointed", 0.3, BATH, door=DOOR_OAK)
    opening(M, "tdoorS", (tx, ty - T * 0.5, 0.0), SOUTH, 1.5, 2.2, "pointed", 0.3, BATH, door=DOOR_OAK)
    for f, at in ((SOUTH, (tx, ty - T * 0.5)), (NORTH, (tx, ty + T * 0.5)), (WEST, (tx - T * 0.5, ty)), (EAST, (tx + T * 0.5, ty))):
        opening(M, "bel%s" % str(f), (at[0], at[1], 12.0), f, 1.6, 2.6, "pointed", 0.18, BATH, mullions=1)
        opening(M, "mid%s" % str(f), (at[0], at[1], 7.2), f, 0.4, 1.6, "pointed", 0.12, BATH)
    # (the broach spire: octagonal, its broaches the square's corners sloping in, 23 m tall)
    SH = 23.0
    M.add(lathe("spire", [(T * 0.5 * 1.02, 0.0), (T * 0.5 * 0.82, 2.4), (0.0, SH)], seg=8, at=(tx, ty, TH), phase=math.pi / 8), "roof", (0.24, 0.21, 0.16), 0.35)
    for sx in (-1, 1):
        for sy in (-1, 1):
            v = [(tx + sx * T * 0.5, ty + sy * T * 0.5, TH), (tx + sx * T * 0.5 * 0.4, ty + sy * T * 0.5, TH), (tx + sx * T * 0.5, ty + sy * T * 0.5 * 0.4, TH),
                 (tx + sx * T * 0.36, ty + sy * T * 0.36, TH + 3.2)]
            f = [(0, 2, 1), (0, 1, 3), (1, 2, 3), (2, 0, 3)]
            M.add(_object("broach%d%d" % (sx, sy), v, f), "roof", (0.24, 0.21, 0.16), 0.35)
    for f, (dx, dy) in ((SOUTH, (0, -1)), (NORTH, (0, 1)), (WEST, (-1, 0)), (EAST, (1, 0))):
        at = (tx + dx * T * 0.42, ty + dy * T * 0.42, TH + 2.2)
        opening(M, "luc%s" % str(f), at, f, 0.6, 0.9, "pointed", 0.12, BATH)
        lr = prism("lucgab%s" % str(f), [(-0.6, 0.0), (0.6, 0.0), (0.0, 0.9)], 0.6)
        lr.matrix_world = Matrix.Translation((0, 0.1, 0)) @ lr.matrix_world
        M.add(place(lr, (at[0], at[1], at[2] + 1.4), f), "stone", BATH, 0.3)
    M.add(lathe("finial", [(0.06, 0.0), (0.04, 1.2), (0.0, 1.3)], seg=6, at=(tx, ty, TH + SH - 0.3)), "steel", (0.05, 0.05, 0.05), 0.2)
    return M


def methodist_chapel(name="SM_Church_methodist_chapel", D=16.0, W=12.0, E=9.0, wall=BRICK, dressed=STUCCO, roof=SLATE,
                     tiers=2, wall_surface="stone"):
    """A Methodist chapel of the 1860s, gable to the street: 16 m deep by 12 m wide, walls 9 m, a 25-degree
    slate roof; a pedimented stucco front over brick, two tiers of tall round-headed windows, a doorcase."""
    M = Model(name)
    if tiers == 1:
        E = min(E, 6.4)
    ridge = range_block(M, "body", 0.0, D, W, E, 25.0, wall, roof, overhang=0.35, verge=0.0, ridge_tiles=roof, wall_surface=wall_surface)
    # (the front at x = 0 faces west: the pediment's cornices, the date panel, pilasters, a plinth)
    rise = ridge - E
    M.add(box("cornice", (-0.15, 0.0, E + 0.1), (0.4, W + 0.6, 0.3)), "stone", dressed, 0.3)
    for s in (-1, 1):
        a = Vector((-0.15, s * (W * 0.5 + 0.3), E + 0.2))
        b = Vector((-0.15, 0.0, ridge + 0.25))
        d = b - a
        cb = box("rake%d" % s, (0, 0, 0), (0.4, d.length + 0.2, 0.3))
        cb.matrix_world = Matrix.Translation((a + b) * 0.5) @ Matrix.Rotation(-s * math.atan2(d.z, -s * d.y), 4, "X")
        M.add(cb, "stone", dressed, 0.3)
    tym = prism("tympanum", [(-W * 0.5 + 0.4, E + 0.3), (W * 0.5 - 0.4, E + 0.3), (0.0, ridge - 0.25)], 0.05)
    M.add(place(tym, (-0.02, 0.0, 0.0), WEST), "stone", dressed, 0.3)
    dp = box("datepanel", (-0.08, 0.0, E + rise * 0.42), (0.1, 2.4, 0.9))
    M.add(dp, "stone", tuple(c * 1.05 for c in dressed), 0.2)
    for u in (-W * 0.5 + 0.25, -W * 0.18, W * 0.18, W * 0.5 - 0.25):
        M.add(box("pil%.1f" % u, (-0.1, u, (0.6 + E) * 0.5), (0.25, 0.6, E - 0.6)), "stone", dressed, 0.3)
    M.add(box("plinth", (-0.08, 0.0, (FOOT + 0.6) * 0.5), (0.2, W + 0.1, 0.6 - FOOT)), "stone", dressed, 0.4)
    if tiers == 2:
        M.add(box("band", (-0.08, 0.0, 4.4), (0.18, W, 0.25)), "stone", dressed, 0.3)
    # (the doorcase: two columns, an entablature, a fanlight over double doors; steps up to it)
    M.add(box("step0", (-0.9, 0.0, 0.08), (1.2, 3.4, 0.16)), "stone", dressed, 0.4)
    M.add(box("step1", (-0.6, 0.0, 0.24), (0.6, 3.0, 0.16)), "stone", dressed, 0.4)
    opening(M, "door", (0.0, 0.0, 0.32), WEST, 1.8, 2.5, "round", 0.18, dressed, door=DOOR_PAINT)
    for s in (-1, 1):
        M.add(lathe("col%d" % s, [(0.18, 0.0), (0.16, 3.2), (0.22, 3.25), (0.22, 3.4)], seg=12, at=(-0.35, s * 1.35, 0.32)), "stone", dressed, 0.3)
    M.add(box("entab", (-0.35, 0.0, 3.9), (0.6, 3.3, 0.45)), "stone", dressed, 0.3)
    # (front windows: the lower tier each side of the door, the upper tier five across)
    low = 2.0 if tiers == 2 else E - 1.2 - 1.4 - 0.6
    for u in (-W * 0.34, -W * 0.18 - 0.9, W * 0.18 + 0.9, W * 0.34):
        if abs(u) < W * 0.5 - 0.8:
            opening(M, "fl%.1f" % u, (0.0, -u, 1.2), WEST, 1.0, low, "round", 0.14, dressed)
    if tiers == 2:
        for u in (-W * 0.34, -W * 0.18, 0.0, W * 0.18, W * 0.34):
            opening(M, "fu%.1f" % u, (0.0, -u, 5.2), WEST, 1.1, 2.4, "round", 0.14, dressed)
    # (side windows, two tiers, in brick with stucco heads and sills)
    bays = max(2, int((D - 2.0) / 3.5))
    for k in range(bays):
        x = 1.0 + (D - 2.0) * (k + 0.5) / bays
        for s, f in ((-1, SOUTH), (1, NORTH)):
            opening(M, "sl%d%d" % (k, s), (x, s * W * 0.5, 1.2), f, 1.0, low, "round", 0.12, dressed)
            if tiers == 2:
                opening(M, "su%d%d" % (k, s), (x, s * W * 0.5, 5.2), f, 1.1, 2.4, "round", 0.12, dressed)
    # (quoins on the front corners)
    for y in (-W * 0.5, W * 0.5):
        for k in range(int((E - FOOT - 0.9) / 0.7) + 1):
            big = k % 2 == 0
            M.add(box("q%.0f%d" % (y, k), (0.12 if big else 0.06, y + (0.15 if y < 0 else -0.15) * (0.5 if big else 1), FOOT + 0.9 + k * 0.7),
                      (0.26 if big else 0.14, 0.3 if big else 0.5, 0.3)), "stone", dressed, 0.3)
    return M


DESIGNS = [dales_chapel, norman_tower, victorian_spire, methodist_chapel]


def clear():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for me in list(bpy.data.meshes):
        bpy.data.meshes.remove(me)


def build():
    clear()
    return [f() for f in DESIGNS]


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    models = build()
    for M in models:
        tris = 0
        dg = bpy.context.evaluated_depsgraph_get()
        for p in M.parts:
            me = p.obj.evaluated_get(dg).to_mesh()
            me.calc_loop_triangles()
            tris += len(me.loop_triangles)
        print("%s: %d parts, %d triangles" % (M.name, len(M.parts), tris))
    if "--render" in args:
        from blender_church_kit_render import render_sheet
        render_sheet(models, args[args.index("--render") + 1])


if __name__ == "__main__":
    main()
