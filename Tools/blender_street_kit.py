"""Ride UK's lamp posts and telegraph poles, modelled in Blender (Chat with Fable, approved by beenster
Nov 18: "please build them yourself in blender and use suitable materials to improve their look").

Writes Data/RideUK/Props/SM_Prop_street_lamp_<n>.glb and SM_Prop_power_pole_<n>.glb (Tools/make_ride_props.py
makes the other props and leaves these alone), and sets those kinds' height and drop in
Data/RideUK/prop_kit.json to the models' own, so the game draws them at the size they are made:

  street_lamp 0  a galvanised steel column with a slim LED lantern (the lamp of most streets now)
  street_lamp 1  a tapered concrete column with a swan-neck bracket and a low-pressure sodium lantern
                 (the older estates and B roads)
  power_pole 0   a creosoted timber pole, its step bolts, a cross arm with pin insulators, a slight lean
  power_pole 1   the same with a stay wire to the ground, its insulator and a yellow guard
  power_pole 2   the same carrying a pole-mounted transformer, its fuses and down-leads
  traffic_signal 0  a UK three-aspect signal: grey column, black head under deep hoods, white-edged board

(The heritage lamp stays Tools/make_ride_props.py lamp_heritage.) Every street lamp's lantern face is
1.1 m out along +x and 5.745 m up, where World/WorldRideScenery.cpp FurnitureTile draws its glowing face
(5.735 m) and its light; every model of a kind reaches the same height, so none is scaled to fit.

All of them draw with one material, M_RideStreetKit (Tools/ue_build_rideuk.py street_kit_material), its
textures made by Tools/make_street_kit_textures.py: RGB in the vertex colour is the surface's colour, A the
surface (timber, galvanised steel, concrete, porcelain, glass, enamel, steel) and its dirt, UV0 metres.
Same frame as the other props: front +x, foot at 0 (ground level; the poles and columns run on below it
by `drop` so they never stand proud of a slope), y up.

Needs Blender's Python module (pip install bpy, Blender 4.2 or later):

    python Tools/blender_street_kit.py            # the models
    python Tools/blender_street_kit.py --render DIR   # and a review sheet of renders in DIR
    python Tools/blender_street_kit.py --render-signal DIR   # the traffic signal's own sheet
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
from export_buttertubs import write_glb               # noqa: E402
from make_street_kit_textures import SURFACES, alpha_of  # noqa: E402

ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "Data", "RideUK", "Props")
KIT = os.path.join(ROOT, "Data", "RideUK", "prop_kit.json")
TEX = os.path.join(ROOT, "Data", "Textures", "StreetKit")

ARM_BEND = 0.45         # ([Nov 19, beenster] the arms bend over to level and run straight into the lantern)
LAMP_DROP = 0.3         # columns run on this far into the ground
LENS_Y = 5.745          # the lantern's lens, just above the code's glow (5.735) and light
LENS_X = 1.1
POLE_DROP = 0.5

# (linear colours)
GALV = (0.5, 0.51, 0.5)
GALV_DULL = (0.38, 0.39, 0.38)
CREOSOTE = (0.085, 0.058, 0.038)
CONCRETE = (0.34, 0.33, 0.3)
BODY_GREY = (0.05, 0.055, 0.06)
SOX_GREY = (0.2, 0.22, 0.21)
LENS = (0.55, 0.55, 0.55)
SOX_BOWL = (0.6, 0.52, 0.38)
PORCELAIN_BROWN = (0.16, 0.06, 0.025)
PORCELAIN_GREY = (0.45, 0.47, 0.47)
TANK_GREEN = (0.13, 0.17, 0.14)
STAY_YELLOW = (0.75, 0.52, 0.02)
PLATE = (0.6, 0.6, 0.58)
BLACK = (0.02, 0.02, 0.02)


class Part:
    """One piece of a model: a Blender object, the surface it is, its colour and its dirt."""
    def __init__(self, obj, surface, rgb, dirt):
        self.obj, self.surface, self.rgb, self.dirt = obj, surface, rgb, dirt


class Model:
    def __init__(self, name):
        self.name = name
        self.parts = []

    def add(self, obj, surface, rgb, dirt=0.2):
        self.parts.append(Part(obj, surface, rgb, dirt))
        return obj

    def transform(self, M, keep=lambda p: True):
        for p in self.parts:
            if keep(p):
                p.obj.matrix_world = M @ p.obj.matrix_world


# --- geometry ---------------------------------------------------------------------------------------------

def _object(name, verts, faces, sharp_deg=35.0):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(map(float, v)) for v in verts], [], faces)
    me.validate()
    me.update()
    # (closed pieces always face out: flipped if their signed volume says they were wound inside out)
    vol = 0.0
    co = [v.co for v in me.vertices]
    for poly in me.polygons:
        idx = list(poly.vertices)
        for i in range(1, len(idx) - 1):
            vol += co[idx[0]].dot(co[idx[i]].cross(co[idx[i + 1]]))
    if vol < 0:
        for poly in me.polygons:
            poly.flip()
        me.update()
    for poly in me.polygons:
        poly.use_smooth = True
    me.set_sharp_from_angle(angle=math.radians(sharp_deg))
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def lathe(name, profile, seg=12, at=(0.0, 0.0, 0.0), sharp_deg=35.0, phase=0.0):
    """A solid turned about z: profile [(radius, z)] from bottom to top; ends of radius 0 close to a point,
    others are capped flat."""
    verts, faces = [], []
    rings = []
    for r, z in profile:
        if r <= 1e-6:
            rings.append([len(verts)])
            verts.append((at[0], at[1], at[2] + z))
            continue
        ring = []
        for k in range(seg):
            a = phase + 2 * math.pi * k / seg
            ring.append(len(verts))
            verts.append((at[0] + r * math.cos(a), at[1] + r * math.sin(a), at[2] + z))
        rings.append(ring)
    for A, B in zip(rings, rings[1:]):
        if len(A) == 1 and len(B) == 1:
            continue
        if len(A) == 1:
            faces += [(A[0], B[(k + 1) % seg], B[k]) for k in range(seg)]
        elif len(B) == 1:
            faces += [(A[k], A[(k + 1) % seg], B[0]) for k in range(seg)]
        else:
            faces += [(A[k], A[(k + 1) % seg], B[(k + 1) % seg], B[k]) for k in range(seg)]
    if len(rings[0]) > 1:
        faces.append(tuple(reversed(rings[0])))
    if len(rings[-1]) > 1:
        faces.append(tuple(rings[-1]))
    return _object(name, verts, faces, sharp_deg)


def sweep(name, pts, radius, sides=8, cap=True, sharp_deg=35.0, square=False):
    """A tube along a polyline (parallel-transported frames); radius a number or one per point. square
    makes it a 4-sided bar turned 45 degrees, so its faces are flat to the path's up."""
    pts = [Vector(p) for p in pts]
    rad = radius if isinstance(radius, (list, tuple)) else [radius] * len(pts)
    if square:
        sides = 4
    tangents = []
    for i in range(len(pts)):
        a = pts[max(i - 1, 0)]; b = pts[min(i + 1, len(pts) - 1)]
        tangents.append((b - a).normalized())
    t0 = tangents[0]
    ref = Vector((0, 0, 1)) if abs(t0.z) < 0.9 else Vector((1, 0, 0))
    n = (ref - t0 * ref.dot(t0)).normalized()
    verts, faces = [], []
    for i, p in enumerate(pts):
        t = tangents[i]
        n = (n - t * n.dot(t)).normalized()
        bnorm = t.cross(n)
        for k in range(sides):
            a = 2 * math.pi * k / sides + (math.pi / 4 if square else 0.0)
            verts.append(p + (n * math.cos(a) + bnorm * math.sin(a)) * rad[i])
    for i in range(len(pts) - 1):
        for k in range(sides):
            a = i * sides + k; b = i * sides + (k + 1) % sides
            faces.append((a, b, b + sides, a + sides))
    if cap:
        faces.append(tuple(reversed(range(sides))))
        last = (len(pts) - 1) * sides
        faces.append(tuple(range(last, last + sides)))
    return _object(name, verts, faces, sharp_deg)


def rod(name, a, b, r, sides=6):
    return sweep(name, [a, b], r, sides)


def box(name, centre, size, rot_z=0.0, bevel=0.0, rot=None):
    """A box of size (x, y, z) about its centre, turned rot_z about z (or by the matrix rot), with an
    optional bevel on its edges."""
    sx, sy, sz = (s * 0.5 for s in size)
    v = [(x, y, z) for z in (-sz, sz) for y in (-sy, sy) for x in (-sx, sx)]
    f = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    ob = _object(name, v, f, 30.0)
    if bevel > 0:
        m = ob.modifiers.new("bevel", "BEVEL")
        m.width = bevel
        m.segments = 2
        m.limit_method = "ANGLE"
        m.harden_normals = True
    R = rot if rot is not None else Matrix.Rotation(rot_z, 4, "Z")
    ob.matrix_world = Matrix.Translation(Vector(centre)) @ R
    return ob


def tapered_box(name, z0, z1, w0, w1, bevel=0.012):
    """A square shaft tapering from w0 wide at z0 to w1 at z1 (the concrete column), corners bevelled."""
    v = []
    for z, w in ((z0, w0), (z1, w1)):
        h = w * 0.5
        v += [(-h, -h, z), (h, -h, z), (h, h, z), (-h, h, z)]
    f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    ob = _object(name, v, f, 30.0)
    m = ob.modifiers.new("bevel", "BEVEL")
    m.width = bevel
    m.segments = 3
    m.limit_method = "ANGLE"
    return ob


def bezier(p0, p1, p2, p3, n=10):
    out = []
    for i in range(n + 1):
        t = i / n
        out.append(tuple((1 - t) ** 3 * np.array(p0) + 3 * (1 - t) ** 2 * t * np.array(p1)
                         + 3 * (1 - t) * t * t * np.array(p2) + t ** 3 * np.array(p3)))
    return out


# --- the models -------------------------------------------------------------------------------------------

def lamp_led():
    """A 6 m galvanised column, a base with its door, an upswept single arm, a slim LED lantern."""
    M = Model("SM_Prop_street_lamp_0")
    M.add(lathe("root", [(0.1, -LAMP_DROP), (0.1, 0.0)], 14), "galv", GALV_DULL, 0.6)
    M.add(lathe("base", [(0.105, 0.0), (0.105, 0.04), (0.092, 0.06), (0.09, 1.0), (0.082, 1.03), (0.0725, 1.05)], 16), "galv", GALV, 0.35)
    M.add(lathe("shaft", [(0.0725, 1.05), (0.05, 5.2)], 16), "galv", GALV, 0.2)
    # (the door at the back, its two bolts, and the maintenance number plate on the front)
    M.add(box("door", (-0.088, 0, 0.62), (0.012, 0.09, 0.42), bevel=0.004), "galv", GALV_DULL, 0.3)
    for z in (0.44, 0.8):
        M.add(rod("doorbolt", (-0.093, 0, z), (-0.1, 0, z), 0.008, 6), "steel", GALV_DULL, 0.3)
    M.add(box("plate", (0.08, 0, 2.3), (0.006, 0.07, 0.16)), "paint", (0.6, 0.6, 0.55), 0.15)
    M.add(box("platenum", (0.0835, 0, 2.3), (0.001, 0.05, 0.1)), "paint", BLACK, 0.1)
    # (the arm: up out of the column top, over in one easy bend to level, and straight on into the back of
    # the lantern at its middle, so the lantern hangs flat in line with it)
    r = 0.03
    level = LENS_Y + 0.05
    arm = [(0, 0, 5.15), (0, 0, level - ARM_BEND)]
    arm += bezier((0, 0, level - ARM_BEND), (0, 0, level - 0.1), (0.1, 0, level), (ARM_BEND, 0, level), 10)[1:]
    arm += [(0.82, 0, level)]
    M.add(sweep("arm", arm, r, 10), "galv", GALV, 0.15)
    M.add(lathe("armcollar", [(0.054, 5.1), (0.054, 5.2), (0.042, 5.23)], 14), "galv", GALV, 0.2)
    # (the lantern: a slim cast body tapering to the front, its flat lens underneath, a heat-sink top)
    x0, x1 = 0.76, 1.44
    verts, faces = [], []
    prof = [(x0, 0.06, 0.055), (x0 + 0.05, 0.11, 0.08), (0.95, 0.135, 0.095), (1.25, 0.14, 0.09), (x1 - 0.04, 0.12, 0.07), (x1, 0.09, 0.04)]
    for x, hw, h in prof:
        bottom = LENS_Y - 0.005
        verts += [(x, -hw, bottom), (x, hw, bottom), (x, hw * 0.85, bottom + h), (x, -hw * 0.85, bottom + h)]
    n = len(prof)
    for i in range(n - 1):
        a = i * 4; b = a + 4
        faces += [(a + 0, a + 1, b + 1, b + 0), (a + 1, a + 2, b + 2, b + 1), (a + 2, a + 3, b + 3, b + 2), (a + 3, a + 0, b + 0, b + 3)]
    faces += [(3, 2, 1, 0), ((n - 1) * 4 + 0, (n - 1) * 4 + 1, (n - 1) * 4 + 2, (n - 1) * 4 + 3)]
    body = _object("lantern", verts, faces, 25.0)
    bv = body.modifiers.new("bevel", "BEVEL"); bv.width = 0.008; bv.segments = 2; bv.limit_method = "ANGLE"
    M.add(body, "enamel", BODY_GREY, 0.15)
    M.add(box("lens", (LENS_X, 0, LENS_Y - 0.004), (0.4, 0.2, 0.006)), "lens", LENS, 0.05)
    for k in range(7):
        x = 0.92 + k * 0.05
        M.add(box("fin", (x, 0, LENS_Y + 0.093), (0.012, 0.2, 0.02)), "enamel", BODY_GREY, 0.2)
    # (its photocell in a socket on top: as high as the sodium lantern's canopy, so both models are one height)
    M.add(lathe("pecell", [(0.03, 0.0), (0.03, 0.07), (0.022, 0.085), (0.0, 0.09)], 12, at=(1.3, 0, LENS_Y + 0.075)), "enamel", (0.15, 0.15, 0.15), 0.1)
    return M


def lamp_sodium():
    """An older estate lamp: a tapered concrete column, its door, a concrete swan-neck bracket and the
    long low-pressure sodium lantern under its enamel canopy, its warm bowl underneath."""
    M = Model("SM_Prop_street_lamp_1")
    M.add(tapered_box("column", -LAMP_DROP, 5.3, 0.21, 0.13, 0.015), "concrete", CONCRETE, 0.35)
    M.add(box("door", (-0.102, 0, 0.75), (0.012, 0.11, 0.48), bevel=0.003), "paint", (0.2, 0.22, 0.2), 0.45)
    M.add(box("plate", (0.098, 0, 2.2), (0.006, 0.08, 0.16)), "paint", (0.62, 0.6, 0.5), 0.2)
    M.add(box("platenum", (0.1015, 0, 2.2), (0.001, 0.06, 0.1)), "paint", BLACK, 0.1)
    # (the bracket: square concrete bar, tapering, up from the column's head, over in one easy bend to level
    # and straight on into the end of the canopy, so the lantern sits flat in line with it)
    level = LENS_Y + 0.12
    path = [(0, 0, 5.2), (0, 0, level - ARM_BEND)]
    path += bezier((0, 0, level - ARM_BEND), (0, 0, level - 0.1), (0.12, 0, level), (ARM_BEND, 0, level), 12)[1:]
    path += [(0.86, 0, level)]
    radii = [0.06 if p[2] < level - 0.2 else 0.052 if p[0] < ARM_BEND else 0.045 for p in path]
    M.add(sweep("bracket", path, radii, 8), "concrete", CONCRETE, 0.3)
    # (the lantern: a long enamel canopy, rounded at the ends, and the bowl under it)
    x0, x1 = 0.76, 1.46
    cx = (x0 + x1) * 0.5
    can = lathe("canopy", [(0.0, 0.0), (0.12, 0.0), (0.135, 0.02), (0.12, 0.06), (0.07, 0.09), (0.0, 0.1)], 16, at=(0, 0, 0))
    can.matrix_world = Matrix.Translation((cx, 0, LENS_Y + 0.075)) @ Matrix.Diagonal((2.6, 0.95, 1.0, 1.0))
    M.add(can, "enamel", SOX_GREY, 0.3)
    bowl = lathe("bowl", [(0.0, -0.002), (0.09, 0.0), (0.115, 0.03), (0.125, 0.07), (0.12, 0.08)], 16)
    bowl.matrix_world = Matrix.Translation((cx + 0.005, 0, LENS_Y - 0.002)) @ Matrix.Diagonal((2.55, 0.92, 1.0, 1.0))
    M.add(bowl, "lens", SOX_BOWL, 0.15)
    M.add(lathe("rim", [(0.124, 0.0), (0.13, 0.008), (0.124, 0.016)], 16, at=(0, 0, 0)), "steel", GALV_DULL, 0.3)
    M.parts[-1].obj.matrix_world = Matrix.Translation((cx + 0.005, 0, LENS_Y + 0.072)) @ Matrix.Diagonal((2.56, 0.93, 1.0, 1.0))
    return M


def insulator(name, at, glaze, scale=1.0):
    """An 11 kV pin insulator: its steel pin and the glazed body with two sheds."""
    x, y, z = at
    s = scale
    parts = [
        ("steel", GALV_DULL, lathe(name + "pin", [(0.012 * s, 0.0), (0.012 * s, 0.07 * s)], 8, at=(x, y, z))),
        ("glaze", glaze, lathe(name, [(0.03 * s, 0.05 * s), (0.075 * s, 0.07 * s), (0.08 * s, 0.085 * s), (0.045 * s, 0.1 * s),
                                      (0.06 * s, 0.115 * s), (0.065 * s, 0.128 * s), (0.035 * s, 0.14 * s), (0.032 * s, 0.17 * s),
                                      (0.0, 0.185 * s)], 14, at=(x, y, z), sharp_deg=50.0)),
    ]
    return parts


def power_pole(variant, seed):
    """A creosoted wooden pole, 9 m to its top, a cross arm across the line (x) with two pin insulators
    and a third on the pole's head, galvanised step bolts from 2.4 m, a number plate, a slight lean.
    Variant 1 adds a stay wire along the line (-y), 2 a pole-mounted transformer behind (-x)."""
    rng = np.random.default_rng(seed)
    M = Model("SM_Prop_power_pole_%d" % variant)
    wood = tuple(np.clip(np.array(CREOSOTE) * rng.uniform(0.85, 1.25), 0, 1))
    glaze = PORCELAIN_BROWN if variant != 1 else PORCELAIN_GREY
    top = 8.95
    M.add(lathe("pole", [(0.145, -POLE_DROP), (0.14, 0.0), (0.112, top - 0.02), (0.105, top)], 16), "timber", wood, 0.3)
    # (a bitumen-felt cap on its head, and the head's insulator on a bracket)
    M.add(lathe("cap", [(0.115, top - 0.04), (0.118, top), (0.05, top + 0.03), (0.0, top + 0.035)], 16), "paint", (0.03, 0.03, 0.03), 0.4)
    M.add(box("headbracket", (0.0, 0, top + 0.045), (0.06, 0.06, 0.02)), "galv", GALV_DULL, 0.3)
    for s, rgb, ob in insulator("headins", (0.0, 0, top + 0.055), glaze):
        M.add(ob, s, rgb, 0.15)
    # (the cross arm, its braces and its two insulators)
    arm_z = 8.42
    M.add(box("arm", (0, 0.13, arm_z), (1.7, 0.1, 0.11), bevel=0.01), "timber", tuple(np.array(wood) * 1.15), 0.25)
    for sx in (-1, 1):
        M.add(sweep("brace", [(sx * 0.03, 0.11, arm_z - 0.75), (sx * 0.62, 0.11, arm_z - 0.06)], 0.012, square=True), "galv", GALV_DULL, 0.35)
        for s, rgb, ob in insulator("armins", (sx * 0.72, 0.13, arm_z + 0.055), glaze):
            M.add(ob, s, rgb, 0.15)
    M.add(rod("armbolt", (0, -0.16, arm_z), (0, 0.2, arm_z), 0.012, 6), "steel", GALV_DULL, 0.4)
    # (step bolts, alternately either side across the line, from 2.4 m to under the arm)
    k = 0
    z = 2.4
    while z < arm_z - 0.5:
        side = 1 if k % 2 == 0 else -1
        r_here = 0.14 - (0.14 - 0.112) * (z / top)
        a = Vector((0, side * r_here, z))
        b = Vector((0, side * (r_here + 0.16), z))
        c = Vector((0, side * (r_here + 0.16), z + 0.045))
        M.add(sweep("step", [a, b, c], 0.011, 6), "galv", GALV_DULL, 0.35)
        z += 0.42
        k += 1
    # (the number plate and the danger plate on the road side)
    M.add(box("numplate", (0.135, 0, 2.6), (0.006, 0.07, 0.11)), "paint", PLATE, 0.2)
    M.add(box("numtext", (0.1385, 0, 2.6), (0.001, 0.05, 0.06)), "paint", BLACK, 0.1)
    if variant == 2:
        M.add(box("danger", (0.13, 0, 3.1), (0.006, 0.2, 0.26)), "paint", (0.7, 0.55, 0.02), 0.2)
        M.add(box("dangerband", (0.1335, 0, 3.12), (0.001, 0.16, 0.05)), "paint", BLACK, 0.1)
    if variant == 2:
        # (the transformer: a ribbed tank on a platform of two channels behind the pole, its three
        # bushings on the lid, fuses on a second arm and leads down to it)
        tz = 5.6
        for sy in (-0.2, 0.2):
            M.add(box("chan", (-0.32, sy, tz - 0.05), (0.55, 0.05, 0.1)), "galv", GALV_DULL, 0.4)
        M.add(box("tank", (-0.42, 0, tz + 0.35), (0.42, 0.55, 0.62), bevel=0.02), "paint", TANK_GREEN, 0.35)
        for i in range(9):
            y = -0.24 + i * 0.06
            M.add(box("fin", (-0.65, y, tz + 0.33), (0.07, 0.012, 0.5)), "paint", TANK_GREEN, 0.4)
        M.add(box("lid", (-0.42, 0, tz + 0.68), (0.46, 0.6, 0.03), bevel=0.008), "paint", tuple(np.array(TANK_GREEN) * 0.9), 0.3)
        for i, y in enumerate((-0.17, 0.0, 0.17)):
            M.add(lathe("bush", [(0.03, 0.0), (0.045, 0.03), (0.03, 0.05), (0.04, 0.08), (0.025, 0.1), (0.022, 0.16), (0.0, 0.17)], 10,
                        at=(-0.34, y, tz + 0.695), sharp_deg=50.0), "glaze", PORCELAIN_GREY, 0.2)
            # (leads from the fuse arm down to the bushings)
            fz = 7.55
            M.add(sweep("lead", bezier((-0.08 + (i - 1) * 0.0, y * 2.2 + 0.0, fz), (-0.15, y * 2.0, fz - 0.6), (-0.3, y, tz + 1.4), (-0.34, y, tz + 0.86), 8),
                        0.007, 5), "steel", (0.08, 0.08, 0.08), 0.2)
        M.add(box("fusearm", (0, 0, 7.6), (0.1, 0.95, 0.09), bevel=0.008), "timber", tuple(np.array(wood) * 1.15), 0.25)
        for y in (-0.37, 0.0, 0.37):
            M.add(rod("fuse", (-0.08, y, 7.65), (-0.06, y, 7.98), 0.022, 8), "glaze", PORCELAIN_GREY, 0.2)
            M.add(rod("fusecap", (-0.06, y, 7.98), (-0.055, y, 8.03), 0.03, 8), "steel", GALV_DULL, 0.3)
        # (the earth lead down the pole in its guard)
        M.add(box("earthguard", (0.0, -0.145, 1.4), (0.05, 0.02, 2.8)), "paint", (0.05, 0.05, 0.05), 0.35)
    # The lean: about the foot, a little off upright, its own way (the stay keeps its anchor in the ground)
    lean = math.radians(rng.uniform(1.0, 2.4))
    axis_a = rng.uniform(0, 2 * math.pi)
    L = Matrix.Rotation(lean, 4, Vector((math.cos(axis_a), math.sin(axis_a), 0.0)))
    if variant == 1:
        axis = Vector((1.0, 0.0, 0.0))
        L = Matrix.Rotation(-math.radians(rng.uniform(1.0, 1.8)), 4, axis)   # (a stayed pole leans away from its stay)
    M.transform(L)
    if variant == 1:
        # (the stay: from a band under the arm down along the line to the ground, an insulator a third of
        # the way, the yellow guard over its last 2 m, and its rod into the ground)
        band = L @ Vector((0, -0.12, 7.9))
        M.add(lathe("stayband", [(0.122, 7.86), (0.122, 7.94)], 16), "galv", GALV_DULL, 0.3).matrix_world = L
        anchor = Vector((0, -3.6, 0.15))
        d = anchor - band
        ins = band + d * 0.3
        M.add(rod("stay1", band, ins - d.normalized() * 0.08, 0.007, 6), "steel", GALV_DULL, 0.3)
        M.add(rod("stay2", ins + d.normalized() * 0.08, anchor, 0.007, 6), "steel", GALV_DULL, 0.3)
        ob = lathe("stayins", [(0.0, -0.08), (0.04, -0.05), (0.05, 0.0), (0.04, 0.05), (0.0, 0.08)], 10)
        ob.matrix_world = Matrix.Translation(ins) @ d.to_track_quat("Z", "Y").to_matrix().to_4x4()
        M.add(ob, "glaze", PORCELAIN_BROWN, 0.2)
        g0 = anchor - d.normalized() * 2.1
        M.add(rod("guard", g0, anchor - d.normalized() * 0.05, 0.03, 10), "enamel", STAY_YELLOW, 0.35)
        M.add(rod("stayrod", anchor, anchor + d.normalized() * 0.7, 0.012, 6), "steel", GALV_DULL, 0.7)
    return M


# --- out --------------------------------------------------------------------------------------------------

def triangles(M):
    """The model's triangles as arrays in the props' frame (x front, y up, z = Blender's -y), each corner
    its own vertex: positions, normals, colours (RGBA bytes), UVs (metres, box-projected)."""
    dg = bpy.context.evaluated_depsgraph_get()
    P, N, C, U = [], [], [], []
    for part in M.parts:
        ev = part.obj.evaluated_get(dg)
        me = ev.to_mesh()
        me.calc_loop_triangles()
        mw = part.obj.matrix_world
        nw = mw.to_3x3().inverted().transposed()
        co = np.array([mw @ v.co for v in me.vertices], np.float64)
        cn = np.zeros((len(me.loops), 3))
        me.corner_normals.foreach_get("vector", cn.ravel())
        cn = np.array([nw @ Vector(n) for n in cn])
        cn /= np.maximum(np.linalg.norm(cn, axis=1, keepdims=True), 1e-9)
        for lt in me.loop_triangles:
            vi = list(lt.vertices)
            li = list(lt.loops)
            p = co[vi]
            fn = np.cross(p[1] - p[0], p[2] - p[0])
            ax = int(np.argmax(np.abs(fn)))
            uv = p[:, [0, 1]] if ax == 2 else (p[:, [1, 2]] if ax == 0 else p[:, [0, 2]])
            P.append(p); N.append(cn[li]); U.append(uv)
            dirt = np.clip(part.dirt + 0.45 * np.clip(1.0 - (p[:, 2] + 0.1) / 0.9, 0, 1), 0, 1)
            rgba = np.zeros((3, 4))
            rgba[:, :3] = part.rgb
            rgba[:, 3] = [alpha_of(part.surface, d) for d in dirt]
            C.append(rgba)
        ev.to_mesh_clear()
    P = np.concatenate(P); N = np.concatenate(N); C = np.concatenate(C); U = np.concatenate(U)
    to_frame = lambda a: np.stack([a[:, 0], a[:, 2], -a[:, 1]], 1)
    col = np.round(np.clip(C, 0, 1) * 255.0)
    return to_frame(P), to_frame(N), col.astype(np.uint8), U


def write(M):
    pos, nrm, col, uv = triangles(M)
    tris = np.arange(len(pos)).reshape(-1, 3)
    path = os.path.join(OUT, M.name + ".glb")
    write_glb(path, pos, tris, nrm, col, M.name, uv)
    return len(tris), pos[:, 1].min(), pos[:, 1].max(), pos[:, 0].min(), pos[:, 0].max(), pos[:, 2].min(), pos[:, 2].max()


def clear():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for me in list(bpy.data.meshes):
        bpy.data.meshes.remove(me)


# --- the traffic signal ([Nov 19] beenster: "can you create a more realistic traffic light model") --------

SIG_HEAD_Y = 2.8      # the middle (amber) aspect's centre: World/WorldRideScenery.cpp TrafficSignal's lamps
SIG_STEP = 0.32       # aspect to aspect
SIG_LENS_X = 0.284    # the lens dome's front (the code's lit disc sits just in front, at 0.29)
SIG_BACK, SIG_FRONT = 0.07, 0.27   # the housing, behind the lenses
SIGNAL_BLACK = (0.012, 0.012, 0.013)
SIGNAL_WHITE = (0.8, 0.8, 0.78)
LENS_OFF = {"red": (0.22, 0.02, 0.015), "amber": (0.24, 0.1, 0.01), "green": (0.03, 0.14, 0.09)}


def rounded_plate(name, x, w, h, r, thick, zc, seg=6):
    """A flat plate in the y-z plane (facing +x), w wide and h tall about (x, 0, zc), corners rounded r."""
    pts = []
    for cy, cz, a0 in ((w / 2 - r, h / 2 - r, 0.0), (-(w / 2 - r), h / 2 - r, 90.0), (-(w / 2 - r), -(h / 2 - r), 180.0), (w / 2 - r, -(h / 2 - r), 270.0)):
        for k in range(seg + 1):
            a = math.radians(a0 + 90.0 * k / seg)
            pts.append((cy + r * math.cos(a), cz + r * math.sin(a)))
    n = len(pts)
    verts = [(x - thick / 2, py, zc + pz) for py, pz in pts] + [(x + thick / 2, py, zc + pz) for py, pz in pts]
    faces = [tuple(range(n)), tuple(range(2 * n - 1, n - 1, -1))]
    faces += [(k, (k + 1) % n, n + (k + 1) % n, n + k) for k in range(n)]
    return _object(name, verts, faces, 30.0)


def hood(name, x0, y, z, r, thick, top_len, side_len, span_deg=105.0, seg=14):
    """A tunnel visor (the cowl over a lens): part of a tube along +x round the lens, over the top and down
    the sides, open underneath, longest at the top and cut back towards its lower edges."""
    verts, faces = [], []
    ring = []
    for k in range(seg + 1):
        a = math.radians(-span_deg + 2 * span_deg * k / seg)
        L = side_len + (top_len - side_len) * (0.5 + 0.5 * math.cos(a * 180.0 / span_deg))
        cy, cz = math.sin(a), math.cos(a)
        i = len(verts)
        for rr in (r, r + thick):
            verts.append((x0, y + rr * cy, z + rr * cz))
            verts.append((x0 + L, y + rr * cy, z + rr * cz))
        ring.append(i)   # (inner back, inner front, outer back, outer front)
    for A, B in zip(ring, ring[1:]):
        faces += [(A, B, B + 1, A + 1), (A + 2, A + 3, B + 3, B + 2), (A + 1, B + 1, B + 3, A + 3), (A, A + 2, B + 2, B)]
    A, B = ring[0], ring[-1]
    faces += [(A, A + 1, A + 3, A + 2), (B, B + 2, B + 3, B + 1)]
    return _object(name, verts, faces, 40.0)


def traffic_signal():
    """A UK three-aspect signal: a 114 mm grey column, a black head of three moulded modules on two
    clamp brackets, each lens (200 mm, its off colour) under a deep tunnel hood, and the black backing
    board with its white reflective border. Faces +x, the traffic; the lit faces are the game's own
    (the shared clock's M_RideSignal), drawn just in front of these lenses."""
    M = Model("SM_Prop_traffic_signal_0")
    M.add(lathe("pole", [(0.057, -LAMP_DROP), (0.057, 3.33), (0.04, 3.36), (0.0, 3.37)], 16), "galv", GALV, 0.3)
    M.add(lathe("base", [(0.075, -0.05), (0.075, 0.25), (0.06, 0.29)], 16), "paint", (0.1, 0.1, 0.1), 0.6)
    # (the clamps: a band round the column and an arm to the head's back, top and bottom)
    for zc in (SIG_HEAD_Y - 1.1 * SIG_STEP, SIG_HEAD_Y + 1.1 * SIG_STEP):
        M.add(lathe("clamp", [(0.068, zc - 0.03), (0.068, zc + 0.03)], 16), "paint", SIGNAL_BLACK, 0.25)
        M.add(box("arm", (0.07, 0, zc), (0.06, 0.07, 0.05), bevel=0.006), "paint", SIGNAL_BLACK, 0.25)
    # (the backing board, black with a white border, its corners rounded; just behind the hoods)
    bh = 3 * SIG_STEP + 0.26
    M.add(rounded_plate("board_white", 0.172, 0.62, bh, 0.06, 0.008, SIG_HEAD_Y), "paint", SIGNAL_WHITE, 0.2)
    M.add(rounded_plate("board_black", 0.177, 0.54, bh - 0.08, 0.035, 0.004, SIG_HEAD_Y), "paint", SIGNAL_BLACK, 0.2)
    Ry = Matrix.Rotation(math.pi / 2, 4, "Y")
    for k, colour in enumerate(("red", "amber", "green")):
        zc = SIG_HEAD_Y + SIG_STEP * (1 - k)
        # (the module: a moulded box, a hair apart from the next so the joins show)
        M.add(box("module", ((SIG_BACK + SIG_FRONT) / 2, 0, zc), (SIG_FRONT - SIG_BACK, 0.3, SIG_STEP - 0.006), bevel=0.012), "paint", SIGNAL_BLACK, 0.2)
        # (the bezel round the lens and the lens: a shallow dome)
        bez = lathe("bezel", [(0.125, 0.0), (0.125, 0.008), (0.104, 0.012)], 20)
        bez.matrix_world = Matrix.Translation((SIG_FRONT, 0, zc)) @ Ry
        M.add(bez, "paint", SIGNAL_BLACK, 0.2)
        lens = lathe("lens_" + colour, [(0.102, 0.0), (0.08, 0.008), (0.045, 0.012), (0.0, 0.014)], 20)
        lens.matrix_world = Matrix.Translation((SIG_FRONT, 0, zc)) @ Ry
        M.add(lens, "lens", LENS_OFF[colour], 0.05)
        # (its hood: 270 mm at the top, cut back to 170 mm at the lower edges)
        M.add(hood("hood", SIG_FRONT, 0, zc, 0.118, 0.006, 0.27, 0.17, 100.0), "paint", SIGNAL_BLACK, 0.2)
    return M


def build():
    clear()
    return [lamp_led(), lamp_sodium(), power_pole(0, 3001), power_pole(1, 3002), power_pole(2, 3003), traffic_signal()]


def main():
    models = build()
    heights = {}
    for M in models:
        nt, y0, y1, x0, x1, z0, z1 = write(M)
        kind = M.name[len("SM_Prop_"):].rsplit("_", 1)[0]
        heights.setdefault(kind, []).append((y0, y1))
        print("%s: %d triangles, %.3f..%.3f m up, x %.2f..%.2f, z %.2f..%.2f" % (M.name, nt, y0, y1, x0, x1, z0, z1))
    lines = open(KIT).read().split("\n")
    for kind, spans in heights.items():
        tall = [y1 - y0 for y0, y1 in spans]
        if max(tall) - min(tall) > 0.02:
            raise SystemExit("%s: its models are of different heights (%s): they would be scaled to fit" % (kind, tall))
        # (the kind's one line in the file: its model's whole height, the part in the ground too, since the
        # game scales a model's bounds to it; its foot is already at 0, so no drop; ours, not Fab's)
        for i, ln in enumerate(lines):
            head = '  "%s": ' % kind
            if ln.startswith(head):
                body = ln[len(head):].rstrip()
                comma = body.endswith(",")
                row = json.loads(body.rstrip(","))
                row["words"] = []
                row["height"] = round(float(max(tall)), 3)
                row.pop("drop", None)
                row["credit"] = ""
                lines[i] = head + json.dumps(row) + ("," if comma else "")
                break
        else:
            raise SystemExit("%s: no one-line row in prop_kit.json" % kind)
    open(KIT, "w").write("\n".join(lines))
    json.load(open(KIT))
    return models


if __name__ == "__main__":
    models = main()
    if "--render" in sys.argv:
        sys.path.insert(0, HERE)
        from blender_street_kit_render import render_sheet   # noqa: E402
        render_sheet([M for M in models if "traffic_signal" not in M.name], sys.argv[sys.argv.index("--render") + 1], TEX, SURFACES)
    if "--render-signal" in sys.argv:
        # (the traffic signal's own sheet: a run of its own, one lit scene)
        from blender_street_kit_render import render_signal_sheet   # noqa: E402
        render_signal_sheet([M for M in models if "traffic_signal" in M.name], sys.argv[sys.argv.index("--render-signal") + 1], TEX, SURFACES)
