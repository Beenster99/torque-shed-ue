"""Ride UK's post boxes and red telephone kiosks, modelled in Blender ([Nov 21] beenster: "time to do the
post boxes and telephone boxes - these look like they are out of roblox. Make them as good as the traffic
lights"). They take the place of Tools/make_ride_props.py's block models and draw with the street kit's
one material, M_RideStreetKit (Tools/blender_street_kit.py, Tools/make_street_kit_textures.py): RGB in the
vertex colour the surface's colour, A the surface and its dirt, UV0 metres.

Writes Data/RideUK/Props/SM_Prop_post_box_<n>.glb and SM_Prop_phone_box_<n>.glb, and sets those kinds'
height in Data/RideUK/prop_kit.json to the models' own (every model of a kind is made to the same height,
so the game draws each at the size it is made):

  post_box 0   a cylindrical pillar box: black skirt, moulded cap and dome, the slot under its hood, the
               door with its collection plate, hinges and lock, and an invented crown badge (no real cypher,
               no maker's or operator's name)
  post_box 1   the wide oval pillar with two slots (the town centre's)
  post_box 2   a lamp box on its own black post (the village lane's)
  post_box 3   a wall box let into a brick pier under a concrete coping (the rural one)
  phone_box 0  a red cast-iron kiosk: square plinth on its slab, corner pilasters, eight rows of three
               panes each side under glazing bars, the lit TELEPHONE fascia, an arched pediment with an
               invented crown each side, the stepped domed roof; the door at the front (+x) with its handle
  phone_box 1  the same kiosk older: its red faded, its dirt heavier

Glass is the kit's opaque "lens" surface (it shows the sky; no interior is drawn). Front +x (the slot and
the kiosk's door face the road), foot at 0, y up, the base running 0.1 m into the ground. LODs: the kit
rows' "lods" (Tools/ue_build_rideuk.py prop_kit makes half, quarter and tenth reductions).

    python Tools/blender_post_phone_kit.py               # the models
    python Tools/blender_post_phone_kit.py --render DIR  # and a review sheet in DIR
"""
import json
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_street_kit as SK                       # noqa: E402
from blender_street_kit import Model, box, lathe, _object   # noqa: E402

KIT = SK.KIT
DROP = 0.1                 # every base runs this far into the ground
POST_TOP = 1.62            # every post box's top (kit height = POST_TOP + DROP)
PHONE_TOP = 2.51           # the kiosk's top

# (linear colours)
PO_RED = (0.62, 0.018, 0.01)        # pillar box red, gloss
K_RED = (0.5, 0.012, 0.008)         # the kiosk's deeper red
K_RED_FADED = (0.36, 0.022, 0.016)
BLACK = (0.012, 0.012, 0.012)
SLOT = (0.004, 0.004, 0.004)
WHITE = (0.78, 0.78, 0.75)
SIGN_WHITE = (0.85, 0.84, 0.78)
INK = (0.02, 0.02, 0.025)
STEEL = (0.45, 0.45, 0.43)
BRASS = (0.42, 0.3, 0.1)
CONCRETE = (0.36, 0.35, 0.32)
MORTAR = (0.34, 0.32, 0.29)
BRICK = (0.23, 0.075, 0.04)
GLASS = (0.035, 0.045, 0.05)


# --- shapes ----------------------------------------------------------------------------------------------

def shell(name, rx, ry, a0, a1, z0, z1, t_out, t_in=0.003, seg=10, zseg=1):
    """A curved panel on an elliptic column of radii rx (front, +x) and ry (side): from angle a0 to a1
    (0 is the front), z0 to z1, from t_in inside the surface to t_out proud of it, closed."""
    ring = []
    for k in range(seg + 1):
        a = a0 + (a1 - a0) * k / seg
        p = Vector((rx * math.cos(a), ry * math.sin(a), 0.0))
        n = Vector((math.cos(a) / rx, math.sin(a) / ry, 0.0)).normalized()
        ring.append((p, n))
    verts, faces = [], []
    zs = [z0 + (z1 - z0) * j / zseg for j in range(zseg + 1)]
    # (grid: k along, j up, two layers: inner 0, outer 1)
    idx = {}
    for layer, d in ((0, -t_in), (1, t_out)):
        for k, (p, n) in enumerate(ring):
            for j, z in enumerate(zs):
                idx[layer, k, j] = len(verts)
                q = p + n * d
                verts.append((q.x, q.y, z))
    K, J = seg, zseg
    for k in range(K):
        for j in range(J):
            faces.append((idx[1, k, j], idx[1, k + 1, j], idx[1, k + 1, j + 1], idx[1, k, j + 1]))
            faces.append((idx[0, k, j + 1], idx[0, k + 1, j + 1], idx[0, k + 1, j], idx[0, k, j]))
    for k in range(K):
        faces.append((idx[0, k, 0], idx[0, k + 1, 0], idx[1, k + 1, 0], idx[1, k, 0]))
        faces.append((idx[1, k, J], idx[1, k + 1, J], idx[0, k + 1, J], idx[0, k, J]))
    for j in range(J):
        faces.append((idx[0, 0, j], idx[1, 0, j], idx[1, 0, j + 1], idx[0, 0, j + 1]))
        faces.append((idx[1, K, j], idx[0, K, j], idx[0, K, j + 1], idx[1, K, j + 1]))
    return _object(name, verts, faces, 30.0)


def prism(name, poly, depth, at, u, v, n, back=0.0):
    """A flat shape poly [(u, v)] in the plane through at spanned by u (along) and v (up), extruded along
    its outward normal n from -back to depth."""
    at, u, v, n = (Vector(x) for x in (at, u, v, n))
    m = len(poly)
    verts = [tuple(at + u * a + v * b - n * back) for a, b in poly] + [tuple(at + u * a + v * b + n * depth) for a, b in poly]
    faces = [tuple(reversed(range(m))), tuple(range(m, 2 * m))]
    faces += [(k, (k + 1) % m, m + (k + 1) % m, m + k) for k in range(m)]
    return _object(name, verts, faces, 30.0)


def rounded_rect(w, h, r, seg=4, top_only=False):
    """A rectangle w by h about its centre, its corners (or only its top two) rounded r: [(u, v)],
    anticlockwise."""
    pts = []
    corners = [(w / 2 - r, h / 2 - r, 0.0), (-(w / 2 - r), h / 2 - r, 90.0)]
    if not top_only:
        corners += [(-(w / 2 - r), -(h / 2 - r), 180.0), (w / 2 - r, -(h / 2 - r), 270.0)]
    for cu, cv, a0 in corners:
        for k in range(seg + 1):
            a = math.radians(a0 + 90.0 * k / seg)
            pts.append((cu + r * math.cos(a), cv + r * math.sin(a)))
    if top_only:
        pts += [(-w / 2, -h / 2), (w / 2, -h / 2)]
    return pts


CROWN = [(-0.05, 0.0), (0.05, 0.0), (0.05, 0.018), (0.06, 0.062), (0.038, 0.036), (0.028, 0.07), (0.012, 0.042), (0.0, 0.08),
         (-0.012, 0.042), (-0.028, 0.07), (-0.038, 0.036), (-0.06, 0.062), (-0.05, 0.018)]


def crown(M, rgb, at, u, v, n, scale=1.0, depth=0.008, back=0.01, dirt=0.15):
    """An invented crown badge in relief (no real cypher): a band, five points, a ball on each."""
    at, u, v, n = (Vector(x) for x in (at, u, v, n))
    poly = [(a * scale, b * scale) for a, b in CROWN]
    M.add(prism("crown", poly, depth, at, u, v, n, back), "enamel", rgb, dirt)
    M.add(prism("crown_band", [(-0.054 * scale, -0.004 * scale), (0.054 * scale, -0.004 * scale), (0.054 * scale, 0.014 * scale), (-0.054 * scale, 0.014 * scale)],
                depth + 0.003, at, u, v, n, back), "enamel", rgb, dirt)
    for a, b in ((0.06, 0.062), (0.028, 0.07), (0.0, 0.08), (-0.028, 0.07), (-0.06, 0.062)):
        c = at + u * (a * scale) + v * (b * scale + 0.006 * scale) + n * (depth * 0.5)
        M.add(lathe("pearl", [(0.0, -0.006 * scale), (0.0055 * scale, -0.003 * scale), (0.0055 * scale, 0.003 * scale), (0.0, 0.006 * scale)], 6, at=tuple(c)), "enamel", rgb, dirt)


def heightfield(name, A, top, bottom, N=14, B=None):
    """A solid over the rectangle |x| <= A, |y| <= B (B = A if not given) between two surfaces z = bottom(x, y)
    and z = top(x, y) (the kiosk's roof): grids top and bottom, walls round the edge."""
    B = A if B is None else B
    xs = np.linspace(-A, A, N + 1)
    ys = np.linspace(-B, B, N + 1)
    verts, faces = [], []
    T = lambda i, j: i * (N + 1) + j
    Bo = lambda i, j: (N + 1) ** 2 + i * (N + 1) + j
    for f in (top, bottom):
        for x in xs:
            for y in ys:
                verts.append((x, y, f(x, y)))
    for i in range(N):
        for j in range(N):
            faces.append((T(i, j), T(i + 1, j), T(i + 1, j + 1), T(i, j + 1)))
            faces.append((Bo(i, j + 1), Bo(i + 1, j + 1), Bo(i + 1, j), Bo(i, j)))
    edge = [(i, 0) for i in range(N)] + [(N, j) for j in range(N)] + [(i, N) for i in range(N, 0, -1)] + [(0, j) for j in range(N, 0, -1)]
    for k in range(len(edge)):
        a, b = edge[k], edge[(k + 1) % len(edge)]
        faces.append((T(*a), Bo(*a), Bo(*b), T(*b)))
    return _object(name, verts, faces, 40.0)


def text_mesh(name, body, size, at, u, v, n, thick=0.0012):
    """Flat lettering (Blender's own sans font) of capital height about `size`, centred on at, its baseline
    along u and up v, standing thick proud along n."""
    cu = bpy.data.curves.new(name, "FONT")
    cu.body = body
    cu.size = size / 0.72
    cu.align_x = "CENTER"
    cu.align_y = "CENTER"
    cu.resolution_u = 2
    cu.extrude = thick * 0.5
    cu.fill_mode = "BOTH"
    tmp = bpy.data.objects.new(name + "_c", cu)
    bpy.context.scene.collection.objects.link(tmp)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(tmp.evaluated_get(dg))
    bpy.data.objects.remove(tmp, do_unlink=True)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    u, v, n = Vector(u).normalized(), Vector(v).normalized(), Vector(n).normalized()
    R = Matrix((u, v, n)).transposed().to_4x4()
    ob.matrix_world = Matrix.Translation(Vector(at) + n * (thick * 0.5)) @ R
    for p in me.polygons:
        p.use_smooth = False
    return ob


def side_frame(k):
    """The kiosk's side k (0 front +x, 1 left +y, 2 back, 3 right): its outward normal and its along
    (to the right as you face it), as vectors."""
    a = k * math.pi / 2
    n = Vector((math.cos(a), math.sin(a), 0.0))
    t = Vector((-math.sin(a), math.cos(a), 0.0))
    return n, t


# --- the post boxes --------------------------------------------------------------------------------------

def slot_and_hood(M, rx, ry, a_half, z, red):
    """The posting slot (dark) at z on an elliptic body, its lip under it and the hood over it."""
    M.add(shell("slot", rx, ry, -a_half, a_half, z, z + 0.034, 0.0015, 0.003, 12), "paint", SLOT, 0.0)
    M.add(shell("lip", rx, ry, -a_half - 0.03, a_half + 0.03, z - 0.016, z, 0.012, 0.003, 12), "enamel", red, 0.15)
    M.add(shell("hood", rx, ry, -a_half - 0.06, a_half + 0.06, z + 0.034, z + 0.052, 0.034, 0.003, 14), "enamel", red, 0.15)
    M.add(shell("hood_top", rx, ry, -a_half - 0.05, a_half + 0.05, z + 0.052, z + 0.064, 0.018, 0.003, 14), "enamel", red, 0.15)


def door_on_column(M, rx, ry, a_half, z0, z1, red, plates=((0.0, 1.0),), badge_z=None):
    """The door: a panel proud of the body, its edge picked out by a dark gap; the collection plate(s)
    (a black frame, the white plate, the times in dark lines) at (angle, z top); hinges on its left, the
    lock under the plate, and the badge."""
    M.add(shell("door_gap", rx, ry, -a_half - 0.01, a_half + 0.01, z0 - 0.008, z1 + 0.008, 0.0008, 0.003, 16), "paint", SLOT, 0.1)
    M.add(shell("door", rx, ry, -a_half, a_half, z0, z1, 0.005, 0.003, 16), "enamel", red, 0.2)
    M.add(shell("door_rim", rx + 0.005, ry + 0.005, -a_half + 0.03, a_half - 0.03, z0 + 0.03, z1 - 0.03, 0.002, 0.001, 16), "enamel", red, 0.2)
    for a_c, zt in plates:
        w = 0.085 / rx
        M.add(shell("plate_frame", rx + 0.007, ry + 0.007, a_c - w, a_c + w, zt - 0.115, zt, 0.003, 0.002, 6), "paint", BLACK, 0.1)
        M.add(shell("plate", rx + 0.01, ry + 0.01, a_c - w * 0.86, a_c + w * 0.86, zt - 0.103, zt - 0.012, 0.0015, 0.001, 6), "enamel", WHITE, 0.08)
        M.add(shell("plate_head", rx + 0.0115, ry + 0.0115, a_c - w * 0.8, a_c + w * 0.8, zt - 0.03, zt - 0.018, 0.0006, 0.0003, 4), "paint", PO_RED, 0.05)
        for i in range(4):
            zl = zt - 0.045 - i * 0.014
            M.add(shell("times", rx + 0.0115, ry + 0.0115, a_c - w * (0.7 if i % 2 else 0.6), a_c + w * (0.5 if i % 2 else 0.65), zl - 0.004, zl, 0.0005, 0.0003, 3), "paint", INK, 0.05)
        # (the lock under it)
        p = Vector((rx * math.cos(a_c), ry * math.sin(a_c), 0.0))
        nrm = Vector((math.cos(a_c) / rx, math.sin(a_c) / ry, 0.0)).normalized()
        R = Matrix.Translation(p + nrm * 0.005 + Vector((0, 0, zt - 0.16))) @ nrm.to_track_quat("Z", "Y").to_matrix().to_4x4()
        lk = lathe("lock", [(0.0, -0.004), (0.016, -0.004), (0.016, 0.004), (0.012, 0.008), (0.0, 0.008)], 12)
        lk.matrix_world = R
        M.add(lk, "steel", BRASS, 0.4)
        kh = box("keyhole", (0, 0, 0.0085), (0.004, 0.012, 0.002))
        kh.matrix_world = R @ kh.matrix_world
        M.add(kh, "paint", SLOT, 0.0)
    for z in (z0 + 0.1, z1 - 0.1):
        a = -a_half - 0.02
        p = Vector((rx * math.cos(a), ry * math.sin(a), 0.0))
        M.add(lathe("hinge", [(0.0, z - 0.04), (0.011, z - 0.036), (0.011, z + 0.036), (0.0, z + 0.04)], 8, at=(p.x, p.y, 0.0)), "enamel", red, 0.25)
    if badge_z is not None:
        nrm = Vector((1, 0, 0))
        crown(M, red, (rx - 0.002, 0.0, badge_z), (0, 1, 0), (0, 0, 1), nrm, 0.9, 0.009, 0.01)


def pillar_box(oval):
    """A pillar box: round (radius 0.235 m) or the wide oval with two slots, 1.62 m to the top."""
    M = Model("SM_Prop_post_box_%d" % (1 if oval else 0))
    sy = 1.62 if oval else 1.0
    r = 0.235
    seg = 40 if oval else 32
    S = Matrix.Diagonal((1.0, sy, 1.0, 1.0))
    def turned(name, prof, rgb, surface="enamel", dirt=0.2):
        ob = lathe(name, prof, seg)
        ob.matrix_world = S
        return M.add(ob, surface, rgb, dirt)
    # (the skirt, black, flared to the pavement)
    turned("skirt", [(r + 0.035, -DROP), (r + 0.035, 0.03), (r + 0.03, 0.05), (r + 0.016, 0.085), (r + 0.01, 0.12), (r + 0.005, 0.135)], BLACK, "paint", 0.6)
    # (the body, a fillet over the skirt and a band under the cap)
    turned("body", [(r + 0.005, 0.135), (r, 0.15), (r, 1.29), (r + 0.006, 1.3), (r + 0.006, 1.318), (r, 1.325)], PO_RED, "enamel", 0.2)
    # (the cap: a moulded neck, the overhanging rim, the dome and its little top)
    cap = [(r, 1.325), (r + 0.012, 1.335), (r + 0.016, 1.35), (r + 0.01, 1.36), (r + 0.036, 1.37), (r + 0.05, 1.385), (r + 0.05, 1.41), (r + 0.042, 1.425), (r + 0.03, 1.43)]
    for k in range(1, 10):
        t = k / 10.0
        cap.append(((r + 0.03) * math.cos(t * math.pi / 2 * 0.92), 1.43 + 0.165 * math.sin(t * math.pi / 2)))
    cap += [(0.032 * (1.0 if not oval else 0.7), 1.598), (0.034, 1.605), (0.03, 1.615), (0.0, POST_TOP)]
    turned("cap", cap, PO_RED, "enamel", 0.25)
    rx, ry = r, r * sy
    # (the slots: one central, or two either side of the middle on the oval)
    if oval:
        for yc in (-0.14, 0.14):
            ac = math.asin(yc / ry)
            # (each slot spans about 0.2 m)
            half = 0.1 / ry
            M.add(shell("slot", rx, ry, ac - half, ac + half, 1.18, 1.214, 0.0015, 0.003, 10), "paint", SLOT, 0.0)
            M.add(shell("lip", rx, ry, ac - half - 0.03, ac + half + 0.03, 1.164, 1.18, 0.012, 0.003, 10), "enamel", PO_RED, 0.15)
        M.add(shell("hood", rx, ry, -0.62, 0.62, 1.214, 1.232, 0.034, 0.003, 24), "enamel", PO_RED, 0.15)
        M.add(shell("hood_top", rx, ry, -0.6, 0.6, 1.232, 1.244, 0.018, 0.003, 24), "enamel", PO_RED, 0.15)
        # (the divider between the slots)
        M.add(shell("divider", rx, ry, -0.06, 0.06, 1.164, 1.214, 0.013, 0.003, 4), "enamel", PO_RED, 0.15)
        door_on_column(M, rx, ry, 0.62, 0.4, 1.1, PO_RED, plates=((math.asin(-0.13 / ry), 1.07), (math.asin(0.13 / ry), 1.07)), badge_z=0.62)
    else:
        slot_and_hood(M, rx, ry, 0.52, 1.18, PO_RED)
        door_on_column(M, rx, ry, 0.86, 0.42, 1.1, PO_RED, plates=((0.0, 1.07),), badge_z=0.66)
    return M


def lamp_box():
    """A lamp box on its own post: a 76 mm black post in a concrete collar, the box strapped to it,
    a hood over its top, the slot, the door with its plate, lock and badge."""
    M = Model("SM_Prop_post_box_2")
    M.add(lathe("collar", [(0.11, -DROP), (0.11, 0.0), (0.1, 0.025), (0.05, 0.035)], 14), "concrete", CONCRETE, 0.5)
    M.add(lathe("post", [(0.038, -DROP), (0.038, 1.5), (0.03, 1.51), (0.0, 1.515)], 14), "paint", BLACK, 0.4)
    # (the box: 0.3 wide, 0.2 deep, its back against the post; straps round the post)
    x0, x1, y = 0.05, 0.25, 0.15
    zb, zt = 1.08, 1.545
    M.add(box("box", ((x0 + x1) / 2, 0, (zb + zt) / 2), (x1 - x0, 2 * y, zt - zb), bevel=0.012), "enamel", PO_RED, 0.2)
    M.add(box("foot", ((x0 + x1) / 2, 0, zb - 0.01), (x1 - x0 - 0.02, 2 * y - 0.02, 0.02), bevel=0.005), "enamel", PO_RED, 0.3)
    for z in (1.17, 1.46):
        M.add(lathe("strap", [(0.041, z - 0.02), (0.041, z + 0.02)], 14), "paint", BLACK, 0.3)
        M.add(box("strap_arm", (0.045, 0, z), (0.02, 0.06, 0.04), bevel=0.003), "paint", BLACK, 0.3)
    # (the hood: a sloping roof over the top, overhanging the front and sides)
    hood = [(-0.0, -0.0), (0.24, 0.0), (0.24, 0.018), (0.0, 0.075)]
    M.add(prism("hood", [(x0 - 0.01 + a, zt - 0.003 + b) for a, b in hood], y + 0.02, (0, 0, 0), (1, 0, 0), (0, 0, 1), (0, 1, 0), y + 0.02), "enamel", PO_RED, 0.25)
    # (the slot under a little hood of its own)
    zs = 1.455
    M.add(box("slot", (x1 + 0.0005, 0, zs + 0.016), (0.003, 0.19, 0.032)), "paint", SLOT, 0.0)
    M.add(box("slot_lip", (x1 + 0.006, 0, zs - 0.006), (0.014, 0.22, 0.012), bevel=0.003), "enamel", PO_RED, 0.15)
    M.add(box("slot_hood", (x1 + 0.014, 0, zs + 0.04), (0.03, 0.24, 0.014), bevel=0.004), "enamel", PO_RED, 0.15)
    # (the door, the plate, the lock and the badge)
    zd0, zd1 = 1.11, 1.41
    M.add(box("door_gap", (x1 + 0.0004, 0, (zd0 + zd1) / 2), (0.002, 0.25, zd1 - zd0 + 0.012)), "paint", SLOT, 0.1)
    M.add(box("door", (x1 + 0.003, 0, (zd0 + zd1) / 2), (0.006, 0.238, zd1 - zd0), bevel=0.0025), "enamel", PO_RED, 0.2)
    xp = x1 + 0.006
    M.add(box("plate_frame", (xp + 0.0015, 0, 1.345), (0.003, 0.15, 0.1)), "paint", BLACK, 0.1)
    M.add(box("plate", (xp + 0.0035, 0, 1.345), (0.0015, 0.132, 0.082)), "enamel", WHITE, 0.08)
    M.add(box("plate_head", (xp + 0.0045, 0, 1.374), (0.0006, 0.12, 0.012)), "paint", PO_RED, 0.05)
    for i in range(3):
        M.add(box("times", (xp + 0.0045, -0.005 * (i % 2), 1.352 - i * 0.015), (0.0006, 0.1 - 0.02 * (i % 2), 0.004)), "paint", INK, 0.05)
    lk = lathe("lock", [(0.0, -0.004), (0.014, -0.004), (0.014, 0.004), (0.0, 0.008)], 12)
    lk.matrix_world = Matrix.Translation((xp, 0.0, 1.268)) @ Matrix.Rotation(math.pi / 2, 4, "Y")
    M.add(lk, "steel", BRASS, 0.4)
    crown(M, PO_RED, (xp - 0.004, 0.0, 1.165), (0, 1, 0), (0, 0, 1), (1, 0, 0), 0.65, 0.007, 0.004)
    return M


def wall_box(seed=4040):
    """A wall box let into a brick pier (0.56 wide, 0.46 deep) under a concrete coping: the cast front with
    its rounded-top frame, hood and slot, the door, plate, lock and badge."""
    M = Model("SM_Prop_post_box_3")
    rng = np.random.default_rng(seed)
    W, D = 0.56, 0.46
    top = POST_TOP - 0.1
    # (the mortar: the pier's core, a little in from the bricks' faces)
    M.add(box("core", (0, 0, (top - DROP) / 2), (D - 0.012, W - 0.012, top + DROP)), "concrete", MORTAR, 0.4)
    # (the bricks: 215 x 65 mm faces in 10 mm of mortar, stretcher bond, each its own shade; cut round the box)
    hole = (0.79, 1.36, 0.2)        # z0, z1, half width of the box's opening in the front
    course, length = 0.075, 0.225
    z = -DROP
    row = 0
    while z < top - 0.01:
        zc = z + course / 2
        h = min(0.065, top - z - 0.005)
        for k in range(4):
            n, t = side_frame(k)
            span = W if k % 2 == 0 else D           # (front and back faces span the width)
            off = (row % 2) * length / 2 + (k * 0.07)
            # (the corners bonded: a course's front and back bricks run to the corners and its sides' stop
            # behind them, the next course the other way round)
            lim = span / 2 if (k + row) % 2 == 0 else span / 2 - 0.1
            s = -span / 2 - off
            while s < span / 2:
                a, b = max(s + 0.005, -lim), min(s + length - 0.005, lim)
                if b - a > 0.006:
                    c = (a + b) / 2
                    if k == 0 and hole[0] - 0.01 < zc < hole[1] + 0.01 and abs(c) - (b - a) / 2 < hole[2]:
                        # (cut back to the opening's edge)
                        if c < 0:
                            b = -hole[2] - 0.005
                        else:
                            a = hole[2] + 0.005
                        if b - a < 0.03:
                            s += length
                            continue
                        c = (a + b) / 2
                    depth_n = (D if k % 2 == 0 else W) / 2
                    centre = n * (depth_n - 0.05) + t * c + Vector((0, 0, zc))
                    size_n, size_t = 0.1, b - a
                    R = Matrix.Rotation(k * math.pi / 2, 4, "Z")
                    ob = box("brick", tuple(centre), (size_n, size_t, h))
                    ob.matrix_world = Matrix.Translation(centre) @ R
                    shade = np.clip(np.array(BRICK) * rng.uniform(0.75, 1.3) * np.array([1.0, rng.uniform(0.85, 1.25), rng.uniform(0.85, 1.3)]), 0, 1)
                    M.add(ob, "concrete", tuple(shade), float(rng.uniform(0.15, 0.45)))
                s += length
        z += course
        row += 1
    # (the coping: a weathered concrete cap, its top sloping off to a drip each side)
    cw, cd = W + 0.07, D + 0.07
    M.add(box("coping", (0, 0, top + 0.03), (cd, cw, 0.06), bevel=0.006), "concrete", CONCRETE, 0.45)
    M.add(prism("coping_top", [(-cw / 2 + 0.01, 0.0), (cw / 2 - 0.01, 0.0), (0.0, 0.035)], cd / 2 - 0.01, (0, 0, top + 0.06), (0, 1, 0), (0, 0, 1), (1, 0, 0), cd / 2 - 0.01), "concrete", CONCRETE, 0.45)
    # (the box's cast front: a frame with a rounded top set in the opening, standing 15 mm proud)
    xf = D / 2
    fw, fh = 2 * hole[2] + 0.04, hole[1] - hole[0] + 0.04
    fz = (hole[0] + hole[1]) / 2
    frame = rounded_rect(fw, fh, 0.09, 6, top_only=True)
    M.add(prism("frame", [(a, fz + b) for a, b in frame], 0.015, (xf, 0, 0), (0, 1, 0), (0, 0, 1), (1, 0, 0), 0.06), "enamel", PO_RED, 0.2)
    inner = rounded_rect(fw - 0.05, fh - 0.05, 0.07, 6, top_only=True)
    M.add(prism("frame_in", [(a, fz + b) for a, b in inner], 0.02, (xf, 0, 0), (0, 1, 0), (0, 0, 1), (1, 0, 0), 0.0), "enamel", PO_RED, 0.2)
    xs = xf + 0.02
    zs = 1.235
    M.add(box("slot", (xs + 0.0008, 0, zs + 0.016), (0.002, 0.2, 0.032)), "paint", SLOT, 0.0)
    M.add(box("slot_lip", (xs + 0.006, 0, zs - 0.006), (0.012, 0.23, 0.012), bevel=0.003), "enamel", PO_RED, 0.15)
    M.add(box("slot_hood", (xs + 0.016, 0, zs + 0.04), (0.032, 0.26, 0.014), bevel=0.004), "enamel", PO_RED, 0.15)
    zd0, zd1 = 0.84, 1.19
    M.add(box("door_gap", (xs + 0.0004, 0, (zd0 + zd1) / 2), (0.002, 0.3, zd1 - zd0 + 0.012)), "paint", SLOT, 0.1)
    M.add(box("door", (xs + 0.003, 0, (zd0 + zd1) / 2), (0.006, 0.288, zd1 - zd0), bevel=0.0025), "enamel", PO_RED, 0.2)
    xp = xs + 0.006
    M.add(box("plate_frame", (xp + 0.0015, 0, 1.125), (0.003, 0.16, 0.1)), "paint", BLACK, 0.1)
    M.add(box("plate", (xp + 0.0035, 0, 1.125), (0.0015, 0.142, 0.082)), "enamel", WHITE, 0.08)
    M.add(box("plate_head", (xp + 0.0045, 0, 1.154), (0.0006, 0.13, 0.012)), "paint", PO_RED, 0.05)
    for i in range(3):
        M.add(box("times", (xp + 0.0045, -0.006 * (i % 2), 1.132 - i * 0.015), (0.0006, 0.11 - 0.02 * (i % 2), 0.004)), "paint", INK, 0.05)
    lk = lathe("lock", [(0.0, -0.004), (0.014, -0.004), (0.014, 0.004), (0.0, 0.008)], 12)
    lk.matrix_world = Matrix.Translation((xp, 0.0, 1.045)) @ Matrix.Rotation(math.pi / 2, 4, "Y")
    M.add(lk, "steel", BRASS, 0.4)
    crown(M, PO_RED, (xp - 0.004, 0.0, 0.9), (0, 1, 0), (0, 0, 1), (1, 0, 0), 0.75, 0.007, 0.004)
    return M


# --- the telephone kiosk ---------------------------------------------------------------------------------

KA = 0.455          # the body's half width (0.91 m square)


def roof_z(x, y, A=0.49, lift=0.0):
    """The domed roof: arched along each side (corners 0.15 m below the middle of a side), the middle
    0.07 m above that (a sail vault, as the kiosks are)."""
    k1 = 0.07 / (A * A)
    k2 = 0.08 / (A * A)
    return PHONE_TOP - k1 * (x * x + y * y) - k2 * (x * x * y * y) / (A * A) + lift


def phone_box(faded):
    M = Model("SM_Prop_phone_box_%d" % (1 if faded else 0))
    red = K_RED_FADED if faded else K_RED
    dirt = 0.4 if faded else 0.2
    a = KA
    # (the concrete slab it stands on, the cast plinth and its step)
    M.add(box("slab", (0, 0, (0.03 - DROP) / 2), (1.12, 1.12, 0.03 + DROP), bevel=0.01), "concrete", CONCRETE, 0.55)
    M.add(box("plinth", (0, 0, 0.065), (2 * a + 0.07, 2 * a + 0.07, 0.07), bevel=0.008), "enamel", red, dirt + 0.25)
    M.add(box("plinth_step", (0, 0, 0.115), (2 * a + 0.035, 2 * a + 0.035, 0.03), bevel=0.006), "enamel", red, dirt + 0.2)
    # (the corner pilasters, the full height to the cornice)
    pw = 0.105
    for sx in (-1, 1):
        for sy in (-1, 1):
            c = (sx * (a - pw / 2 + 0.008), sy * (a - pw / 2 + 0.008), 1.155)
            M.add(box("pilaster", c, (pw, pw, 2.07), bevel=0.008), "enamel", red, dirt)
            M.add(box("pilaster_cap", (c[0], c[1], 2.19), (pw + 0.02, pw + 0.02, 0.03), bevel=0.005), "enamel", red, dirt)
    # (the solid bands: under the windows and the frieze over them)
    M.add(box("kick_core", (0, 0, 0.23), (2 * a - 0.03, 2 * a - 0.03, 0.22)), "enamel", red, dirt)
    M.add(box("frieze_core", (0, 0, 2.06), (2 * a - 0.004, 2 * a - 0.004, 0.24)), "enamel", red, dirt)
    # (the cornice over the pilasters, the head and the roof's layers)
    M.add(box("cornice", (0, 0, 2.215), (2 * a + 0.05, 2 * a + 0.05, 0.035), bevel=0.008), "enamel", red, dirt)
    M.add(box("cornice_2", (0, 0, 2.243), (2 * a + 0.03, 2 * a + 0.03, 0.022), bevel=0.005), "enamel", red, dirt)
    M.add(heightfield("head", a + 0.0, lambda x, y: roof_z(x, y, 0.49, -0.035), lambda x, y: 2.24, 16), "enamel", red, dirt)
    M.add(heightfield("roof", 0.49, lambda x, y: roof_z(x, y, 0.49, -0.0), lambda x, y: roof_z(x, y, 0.49, -0.04), 16), "enamel", red, dirt + 0.1)
    M.add(heightfield("roof_2", 0.44, lambda x, y: roof_z(x, y, 0.49, 0.022), lambda x, y: roof_z(x, y, 0.49, -0.01), 14), "enamel", red, dirt + 0.1)
    M.add(lathe("roof_knob", [(0.0, PHONE_TOP - 0.02), (0.12, PHONE_TOP - 0.02), (0.11, PHONE_TOP + 0.022), (0.0, PHONE_TOP + 0.03)], 20), "enamel", red, dirt + 0.1)
    rng = np.random.default_rng(6060 + int(faded))
    gx = a - 0.04               # the glass's plane
    for k in range(4):
        n, t = side_frame(k)
        Rk = Matrix.Rotation(k * math.pi / 2, 4, "Z")
        def at(along, z, out):
            return n * out + t * along + Vector((0, 0, z))
        def slab(name, along, z, w, h, out0, out1, rgb=red, surface="enamel", d=dirt, bevel=0.0):
            c = at(along, z, (out0 + out1) / 2)
            ob = box(name, tuple(c), (out1 - out0, w, h), bevel=bevel)
            ob.matrix_world = Matrix.Translation(c) @ Rk
            return M.add(ob, surface, rgb, d)
        # (the kick panel under the window: a fielded panel in a frame)
        slab("kick", 0.0, 0.23, 2 * a - 2 * pw + 0.02, 0.2, a - 0.03, a - 0.01)
        slab("kick_field", 0.0, 0.23, 2 * a - 2 * pw - 0.08, 0.12, a - 0.01, a + 0.002, bevel=0.006)
        # (the window: its frame, the panes and the bars; eight rows of three, the middle row wider)
        wz0, wz1 = 0.34, 1.94
        ww = 2 * (a - pw) + 0.01
        fr = 0.042
        slab("stile_l", -ww / 2 + fr / 2, (wz0 + wz1) / 2, fr, wz1 - wz0, gx - 0.012, a - 0.004)
        slab("stile_r", ww / 2 - fr / 2, (wz0 + wz1) / 2, fr, wz1 - wz0, gx - 0.012, a - 0.004)
        slab("rail_b", 0.0, wz0 + fr / 2, ww - 2 * fr + 0.004, fr, gx - 0.011, a - 0.0055)
        slab("rail_t", 0.0, wz1 - fr / 2, ww - 2 * fr + 0.004, fr, gx - 0.011, a - 0.0055)
        iw = ww - 2 * fr
        ih = wz1 - wz0 - 2 * fr
        bar = 0.02
        side = (iw - 2 * bar) * 0.29
        cols = [(-iw / 2, -iw / 2 + side), (-iw / 2 + side + bar, iw / 2 - side - bar), (iw / 2 - side, iw / 2)]
        rh = (ih - 7 * bar) / 8
        for x0_, x1_ in cols[:2]:
            slab("bar_v", x1_ + bar / 2, (wz0 + wz1) / 2, bar, ih, gx - 0.008, a - 0.016)
        for r_ in range(1, 8):
            zb = wz0 + fr + r_ * rh + (r_ - 1) * bar + bar / 2
            slab("bar_h", 0.0, zb, iw + 0.004, bar, gx - 0.0075, a - 0.0175)
        for r_ in range(8):
            z0_ = wz0 + fr + r_ * (rh + bar)
            for x0_, x1_ in cols:
                tint = np.clip(np.array(GLASS) * rng.uniform(0.75, 1.35) + rng.uniform(0, 0.012), 0, 1)
                slab("pane", (x0_ + x1_) / 2, z0_ + rh / 2, x1_ - x0_ + 0.01, rh + 0.01, gx - 0.003, gx, tuple(tint), "lens", 0.05)
        if k == 0:
            # (the door is the front: its edges, the handle on its right and the hinges on its left)
            for s_ in (-1, 1):
                slab("door_gap", s_ * (a - pw + 0.002), 1.14, 0.006, 1.58, a - 0.03, a - 0.012, SLOT, "paint", 0.1)
            hz0, hz1 = 0.98, 1.2
            hx = ww / 2 - fr / 2
            c0, c1 = at(hx, hz0, a + 0.004), at(hx, hz1, a + 0.004)
            M.add(SK.rod("handle", tuple(at(hx, hz0, a + 0.028)), tuple(at(hx, hz1, a + 0.028)), 0.011, 10), "steel", BRASS, 0.35)
            M.add(SK.rod("handle_a", tuple(c0), tuple(at(hx, hz0, a + 0.03)), 0.009, 8), "steel", BRASS, 0.35)
            M.add(SK.rod("handle_b", tuple(c1), tuple(at(hx, hz1, a + 0.03)), 0.009, 8), "steel", BRASS, 0.35)
            for hz in (0.5, 1.75):
                M.add(SK.rod("hinge", tuple(at(-ww / 2 - 0.004, hz - 0.05, a + 0.004)), tuple(at(-ww / 2 - 0.004, hz + 0.05, a + 0.004)), 0.011, 8), "enamel", red, dirt)
        # (the fascia: the lit white sign in its frame, TELEPHONE in black)
        slab("sign_frame", 0.0, 2.065, 2 * a - 2 * pw + 0.02, 0.15, a - 0.02, a + 0.006, bevel=0.004)
        slab("sign", 0.0, 2.065, 2 * a - 2 * pw - 0.02, 0.11, a - 0.01, a + 0.0085, SIGN_WHITE, "lens", 0.05)
        M.add(text_mesh("telephone", "TELEPHONE", 0.062, at(0.0, 2.061, a + 0.0085), t, (0, 0, 1), n), "paint", INK, 0.05)
        # (the pediment's crown, in the arch over the sign)
        crown(M, red, tuple(at(0.0, 2.315, a + 0.0)), tuple(t), (0, 0, 1), tuple(n), 1.0, 0.01, 0.01, dirt)
    return M


# --- out -------------------------------------------------------------------------------------------------

def build():
    SK.clear()
    return [pillar_box(False), pillar_box(True), lamp_box(), wall_box(), phone_box(False), phone_box(True)]


def set_kit_heights(heights):
    lines = open(KIT).read().split("\n")
    for kind, spans in heights.items():
        tall = [y1 - y0 for y0, y1 in spans]
        if max(tall) - min(tall) > 0.02:
            raise SystemExit("%s: its models are of different heights (%s): they would be scaled to fit" % (kind, tall))
        head = '  "%s": ' % kind
        for i, ln in enumerate(lines):
            if ln.startswith(head):
                # (the row may run onto a second line: the post box's credit)
                j = i
                body = ln[len(head):].rstrip()
                while True:
                    try:
                        json.loads(body.rstrip(","))
                        break
                    except ValueError:
                        j += 1
                        body += " " + lines[j].strip()
                comma = body.endswith(",")
                row = json.loads(body.rstrip(","))
                row["words"] = []          # (ours, not Fab's: no Fab kiosk takes its place)
                row["height"] = round(float(max(tall)), 3)
                row.pop("drop", None)
                row["shadow"] = True
                row["lods"] = [0.3, 0.1, 0.035]
                lines[i:j + 1] = [head + json.dumps(row) + ("," if comma else "")]
                break
        else:
            raise SystemExit("%s: no row in prop_kit.json" % kind)
    open(KIT, "w").write("\n".join(lines))
    json.load(open(KIT))


def main():
    models = build()
    heights = {}
    for M in models:
        nt, y0, y1, x0, x1, z0, z1 = SK.write(M)
        kind = M.name[len("SM_Prop_"):].rsplit("_", 1)[0]
        heights.setdefault(kind, []).append((y0, y1))
        print("%s: %d triangles, %.3f..%.3f m up, x %.2f..%.2f, z %.2f..%.2f" % (M.name, nt, y0, y1, x0, x1, z0, z1))
    set_kit_heights(heights)
    return models


if __name__ == "__main__":
    models = main()
    if "--render" in sys.argv:
        from blender_post_phone_render import render_sheet   # noqa: E402
        render_sheet(models, sys.argv[sys.argv.index("--render") + 1], SK.TEX, SK.SURFACES)
