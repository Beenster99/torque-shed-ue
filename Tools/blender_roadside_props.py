"""Ride UK's lay-by and roadside cafe set (the everyday kits plan, stage 5b): a catering trailer (the lay-by burger
van) and its feather flag, the blue lay-by sign, and a roadside cafe's board, modelled in Blender. Our own designs,
from general knowledge of British roadsides: no names, menus, prices or lettering (the lay-by sign's P is the
sign's symbol, as the Highway Code draws it; the cafe board shows a cup and an arrow).

Writes Data/RideUK/Props/SM_Prop_<kind>_<n>.glb, each with two distance versions made here by hand
(SM_Prop_<kind>_<n>_LOD1.glb and _LOD2.glb: Tools/ue_build_rideuk.py puts them in as the mesh's LODs; never reduced
by the engine), and sets each kind's height and reach in Data/RideUK/prop_kit.json. Same material and frame as the
street kit (Tools/blender_street_kit.py: M_RideStreetKit, front +x, foot at 0); all the models of a kind are the same
height. World/WorldRideRoadside.cpp lays them at the lay-bys and roadside cafes IndexTownLife finds; the lay-bys'
litter bins and picnic tables are the village set's and the OSM kit's.

  road_burgervan 0..1   a catering trailer, 4.3 m long along y on its two wheels, its hatch open to the front (+x)
                        under a lifted flap, gas bottles on its drawbar (-y): two paint schemes
  road_flag 0           a 3.2 m feather flag on its spike, its sail to +y
  road_laybysign 0      the blue lay-by sign (a white P) on its post, its face to the front (+x)
  road_cafesign 0..3    a roadside cafe's board on two posts: a cup and an arrow (cream on brown, white on green; 0 and 1
                        point right, 2 and 3 left)

    python Tools/blender_roadside_props.py                # the models
    python Tools/blender_roadside_props.py --render DIR   # and the review sheet's pictures in DIR
"""
import math
import os
import sys

import bpy  # noqa: F401  (before mathutils)
from mathutils import Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_street_kit as K   # noqa: E402
from blender_street_kit import box, lathe, rod   # noqa: E402

WHITE = (0.75, 0.75, 0.73)
GREY = (0.32, 0.33, 0.33)
DARK = (0.03, 0.03, 0.03)
TYRE = (0.02, 0.02, 0.02)
STEEL = (0.45, 0.45, 0.44)
SIGN_BLUE = (0.0, 0.09, 0.34)
GAS_RED = (0.5, 0.05, 0.03)
# (each van: body, stripe and flap's underside)
VANS = [
    (WHITE, (0.5, 0.04, 0.03), (0.55, 0.42, 0.06)),          # white, red stripe, yellow flap
    ((0.66, 0.6, 0.46), (0.04, 0.2, 0.1), (0.04, 0.2, 0.1)),  # cream, green
]
# (each board: its face, its picture and edge)
BOARDS = [((0.68, 0.62, 0.48), (0.2, 0.1, 0.05)), (WHITE, (0.03, 0.2, 0.09))]
VAN_H = 2.75

# kind: (reach in m, casts a shadow, hand-made LODs' screen sizes)
KINDS = {"road_burgervan": (350, True, [0.15, 0.04]), "road_flag": (200, False, [0.12, 0.03]),
         "road_laybysign": (250, False, [0.12, 0.03]), "road_cafesign": (250, True, [0.12, 0.03])}


def wheel(M, at, lod, r=0.3, w=0.18):
    """A trailer wheel at at, its axle along x."""
    seg = (16, 10, 6)[lod]
    ob = lathe("tyre", [(0.0, -w / 2), (r * 0.62, -w / 2), (r - 0.02, -w / 2), (r, -w / 2 + 0.03), (r, w / 2 - 0.03),
                        (r - 0.02, w / 2), (r * 0.62, w / 2), (0.0, w / 2)], seg)
    ob.matrix_world = Matrix.Translation(at) @ Matrix.Rotation(math.radians(90), 4, "Y")
    M.add(ob, "paint", TYRE, 0.5)
    if lod < 2:
        hub = lathe("hub", [(0.0, -0.01), (r * 0.6, -0.01), (r * 0.6, 0.01), (0.0, 0.03)], seg)
        hub.matrix_world = Matrix.Translation((at[0] + (w / 2 if at[0] > 0 else -w / 2), at[1], at[2])) @ Matrix.Rotation(math.radians(90 if at[0] > 0 else -90), 4, "Y")
        M.add(hub, "galv", STEEL, 0.4)


def burgervan(name, scheme, lod):
    """A catering trailer: a box body 4.3 m long (y) and 2.1 wide (x), 2.15 m tall on a chassis, two wheels under its
    middle, an A-frame drawbar and jockey wheel at -y with two gas bottles, its serving hatch in the +x side with the
    flap lifted as an awning (to 2.75 m), a counter shelf, a menu panel (plain), a roof vent and a flue."""
    M = K.Model(name)
    body, stripe, flap = VANS[scheme]
    L, W = 4.3, 2.1
    z0, z1 = 0.5, 2.5   # (the body's floor and roof)
    M.add(box("body", (0.0, 0.0, (z0 + z1) / 2), (W, L, z1 - z0), bevel=0.06 if lod == 0 else 0.0), "paint", body, 0.25)
    M.add(box("roof", (0.0, 0.0, z1 + 0.04), (W - 0.1, L - 0.1, 0.08), bevel=0.03 if lod == 0 else 0.0), "paint", body, 0.3)
    M.add(box("skirt", (0.0, 0.0, z0 - 0.1), (W - 0.2, L - 0.4, 0.2)), "paint", DARK, 0.6)
    for sx in (-1, 1):
        M.add(box("stripe", (sx * (W / 2 + 0.005), 0.0, z0 + 0.35), (0.01, L - 0.12, 0.16)), "paint", stripe, 0.25)
        wheel(M, (sx * (W / 2 - 0.25), 0.0, 0.3), lod)
        M.add(box("arch", (sx * (W / 2 - 0.25), 0.0, 0.68), (0.3, 0.9, 0.05)), "paint", DARK, 0.4)
    # (the drawbar: an A-frame to a hitch, the jockey wheel, two gas bottles in a cage on it)
    yb = -L / 2
    for sx in (-1, 1):
        M.add(rod("aframe", (sx * 0.6, yb + 0.1, 0.45), (0.0, yb - 1.1, 0.42), 0.04, (6, 4, 3)[lod]), "galv", STEEL, 0.4)
    M.add(box("hitch", (0.0, yb - 1.15, 0.42), (0.12, 0.25, 0.12)), "paint", DARK, 0.4)
    M.add(rod("jockey", (0.0, yb - 0.85, 0.0), (0.0, yb - 0.85, 0.55), 0.035, (6, 4, 3)[lod]), "galv", STEEL, 0.4)
    for k, sx in enumerate((-0.2, 0.2)):
        M.add(lathe("gas", [(0.0, 0.0), (0.15, 0.0), (0.15, 0.72), (0.11, 0.82), (0.05, 0.86), (0.0, 0.88)], (12, 8, 5)[lod],
                    at=(sx, yb - 0.4, 0.45)), "paint", GAS_RED, 0.35)
    # (the hatch: a dark opening in the +x side, the flap lifted out over it, the counter under it)
    hy0, hy1, hz0, hz1 = -1.3, 1.5, 1.15, 2.2
    hl, hc = hy1 - hy0, (hy0 + hy1) / 2
    M.add(box("hatch", (W / 2 + 0.005, hc, (hz0 + hz1) / 2), (0.02, hl, hz1 - hz0)), "paint", (0.06, 0.05, 0.045), 0.3)
    tilt = math.radians(62)
    fl = 1.0
    R = Matrix.Rotation(tilt, 4, "Y")
    M.add(box("flap", (W / 2 + math.sin(tilt) * fl / 2, hc, hz1 + math.cos(tilt) * fl / 2 * 0.5), (0.04, hl + 0.06, fl), rot=R),
          "paint", flap, 0.25)
    M.add(box("counter", (W / 2 + 0.17, hc, hz0 - 0.02), (0.34, hl, 0.04)), "galv", STEEL, 0.3)
    if lod < 2:
        for sy in (-1, 1):
            M.add(rod("stay", (W / 2, hc + sy * (hl / 2 - 0.1), hz1 - 0.35), (W / 2 + 0.75, hc + sy * (hl / 2 - 0.1), hz1 + 0.35), 0.012, 4), "galv", STEEL, 0.3)
        M.add(box("menu", (W / 2 + 0.008, hy0 - 0.55, 1.6), (0.012, 0.7, 0.9)), "paint", DARK, 0.2)
        M.add(box("door", (-W / 2 - 0.005, -0.9, (z0 + z1) / 2 + 0.05), (0.02, 0.8, 1.8)), "paint", tuple(c * 0.85 for c in body), 0.3)
        M.add(box("vent", (0.0, 0.6, z1 + 0.14), (0.5, 0.5, 0.12)), "galv", STEEL, 0.3)
        M.add(rod("flue", (-0.5, -1.2, z1), (-0.5, -1.2, VAN_H), 0.06, (8, 6, 4)[lod]), "galv", STEEL, 0.4)
        M.add(box("cap", (-0.5, -1.2, VAN_H - 0.02), (0.2, 0.2, 0.04)), "galv", STEEL, 0.4)
    if lod == 0:
        # (inside the hatch: a steel back wall's shine, the griddle's hood, a few lines on the menu, the corner steadies)
        M.add(box("backwall", (W / 2 - 0.5, hc, (hz0 + hz1) / 2), (0.02, hl - 0.1, hz1 - hz0 - 0.05)), "galv", STEEL, 0.2)
        M.add(box("hood", (W / 2 - 0.3, hc - 0.3, hz1 - 0.15), (0.4, 1.1, 0.2)), "galv", STEEL, 0.3)
        for k in range(5):
            M.add(box("menuline", (W / 2 + 0.016, hy0 - 0.55, 1.9 - k * 0.15), (0.004, 0.5, 0.03)), "paint", (0.6, 0.6, 0.58), 0.1)
        for sy in (-1, 1):
            for sx in (-1, 1):
                M.add(rod("steady", (sx * (W / 2 - 0.15), sy * (L / 2 - 0.3), z0 - 0.15), (sx * (W / 2 - 0.05), sy * (L / 2 - 0.2), 0.0), 0.025, 4), "galv", STEEL, 0.5)
        M.add(box("lamp", (-0.1, yb - 0.02, 0.62), (0.9, 0.04, 0.12)), "paint", (0.3, 0.05, 0.03), 0.3)
    return M


def flag(name, lod):
    """A feather flag: a pole on a ground spike, bending over at its top, and a tall sail hung from it to +y (a plain
    band of colour, no words)."""
    M = K.Model(name)
    H = 3.2
    M.add(rod("pole", (0.0, 0.0, 0.0), (0.0, 0.0, H), 0.018, (6, 4, 3)[lod]), "galv", STEEL, 0.3)
    n = (8, 4, 2)[lod]
    # (the sail: from the pole to its curved free edge; flat, two-sided as two faces)
    pts = []
    for k in range(n + 1):
        t = k / n
        z = 0.5 + t * (H - 0.5)
        w = 0.55 * math.sin(min(1.0, t * 1.3 + 0.15) * math.pi * 0.5) * (1.0 - 0.6 * max(0.0, t - 0.8) / 0.2)
        if t > 0.85:
            w *= 0.75
        pts.append((z, w))
    verts, faces = [], []
    for z, w in pts:
        verts += [(-0.004, 0.0, z), (-0.004, w + 0.02, z - 0.1 * (z / H) ** 4)]
    for k in range(n):
        a = 2 * k
        faces.append((a, a + 2, a + 3, a + 1))
    for z, w in pts:
        verts += [(0.004, 0.0, z), (0.004, w + 0.02, z - 0.1 * (z / H) ** 4)]
    m = 2 * (n + 1)
    for k in range(n):
        a = m + 2 * k
        faces.append((a, a + 1, a + 3, a + 2))
    sail = K._object("sail", verts, faces, 30.0)
    M.add(sail, "paint", (0.62, 0.06, 0.03), 0.15)
    if lod < 2:
        M.add(box("band", (0.0, 0.18, H * 0.55), (0.012, 0.32, 0.9)), "paint", (0.66, 0.5, 0.05), 0.15)
        M.add(box("foot", (0.0, 0.0, 0.03), (0.12, 0.12, 0.06)), "galv", STEEL, 0.5)
    return M


def laybysign(name, lod):
    """The lay-by sign: a blue board 0.6 m wide and 0.75 tall, its white border and white P, on a grey post 2.6 m to
    the board's top; the board's face to +x."""
    M = K.Model(name)
    Wd, Hb, top = 0.6, 0.75, 2.6
    zc = top - Hb / 2
    M.add(rod("post", (-0.05, 0.0, -0.3), (-0.05, 0.0, top - 0.05), 0.038, (8, 6, 4)[lod]), "galv", GREY, 0.3)
    M.add(box("board", (0.0, 0.0, zc), (0.02, Wd, Hb)), "paint", SIGN_BLUE, 0.15)
    if lod == 2:
        M.add(box("p", (0.012, -0.03, zc), (0.004, 0.28, 0.44)), "paint", (0.5, 0.52, 0.6), 0.1)
        return M
    # (the border, 2.5 cm in)
    b, t = 0.035, 0.025
    for sy in (-1, 1):
        M.add(box("edge", (0.012, sy * (Wd / 2 - b), zc), (0.004, t, Hb - 2 * b + t)), "paint", WHITE, 0.1)
    for sz in (-1, 1):
        M.add(box("edge", (0.012, 0.0, zc + sz * (Hb / 2 - b)), (0.004, Wd - 2 * b + t, t)), "paint", WHITE, 0.1)
    # (the P: its stem and a bowl of three bars and a rounded end)
    s = 0.075
    px0, ph = -0.13, 0.46
    pz0 = zc - ph / 2
    M.add(box("stem", (0.012, px0, zc), (0.004, s, ph)), "paint", WHITE, 0.1)
    bw = 0.17
    for z in (pz0 + ph - s / 2, pz0 + ph * 0.42 + s / 2):
        M.add(box("bar", (0.012, px0 + bw / 2, z), (0.004, bw, s)), "paint", WHITE, 0.1)
    # (the bowl's round end: a half ring in the board's plane)
    zt, zb = pz0 + ph - s / 2, pz0 + ph * 0.42 + s / 2
    zm, rr = (zt + zb) / 2, (zt - zb) / 2
    seg = 8 if lod == 0 else 4
    verts, faces = [], []
    for k in range(seg + 1):
        a = -math.pi / 2 + math.pi * k / seg
        for r in (rr - s / 2, rr + s / 2):
            verts.append((0.0, px0 + bw + math.cos(a) * r, zm + math.sin(a) * r))
    n = len(verts)
    for k in range(seg):
        a = 2 * k
        faces.append((a, a + 1, a + 3, a + 2))
    verts += [(0.004, v[1], v[2]) for v in verts]
    for k in range(seg):
        a = n + 2 * k
        faces.append((a, a + 2, a + 3, a + 1))
    for k in range(seg):   # (the outer and inner edges)
        for o in (0, 1):
            a, c = 2 * k + o, 2 * k + 2 + o
            faces.append((a, c, n + c, n + a) if o else (a, n + a, n + c, c))
    bowl = K._object("bowl", verts, faces, 40.0)
    bowl.matrix_world = Matrix.Translation((0.01, 0.0, 0.0))
    M.add(bowl, "paint", WHITE, 0.1)
    if lod == 0:
        M.add(box("clip", (-0.03, 0.0, zc + 0.2), (0.04, 0.1, 0.05)), "galv", STEEL, 0.3)
        M.add(box("clip", (-0.03, 0.0, zc - 0.2), (0.04, 0.1, 0.05)), "galv", STEEL, 0.3)
        M.add(box("back", (-0.012, 0.0, zc), (0.004, Wd - 0.04, Hb - 0.04)), "galv", GREY, 0.3)
    return M


def cafesign(name, scheme, lod, side=1.0):
    """A roadside cafe's board: 1.4 m wide and 0.9 tall on two timber posts, 1.7 m to its top, its face to +x: a
    steaming cup on its saucer and an arrow, no words; the arrow to the viewer's right (+y), or left with side -1."""
    M = K.Model(name)
    face, ink = BOARDS[scheme]
    Wd, Hb, top = 1.4, 0.9, 1.7
    zc = top - Hb / 2
    for sy in (-1, 1):
        M.add(box("post", (-0.06, sy * (Wd / 2 - 0.12), (top - 0.05 - 0.3) / 2), (0.08, 0.08, top - 0.05 + 0.3)), "timber", (0.22, 0.16, 0.1), 0.5)
    M.add(box("board", (0.0, 0.0, zc), (0.03, Wd, Hb), bevel=0.008 if lod == 0 else 0.0), "paint", face, 0.25)
    if lod == 2:
        M.add(box("ink", (0.018, -0.2 * side, zc), (0.004, 0.4, 0.4)), "paint", ink, 0.1)
        return M
    for sz in (-1, 1):
        M.add(box("rim", (0.018, 0.0, zc + sz * (Hb / 2 - 0.04)), (0.004, Wd - 0.06, 0.03)), "paint", ink, 0.1)
    # (the cup: a tapered body, its handle, the saucer; steam as three short strokes)
    cy, cz = -0.28 * side, zc - 0.08
    verts = [(0.0, cy - 0.15, cz + 0.17), (0.0, cy + 0.15, cz + 0.17), (0.0, cy + 0.1, cz - 0.1), (0.0, cy - 0.1, cz - 0.1)]
    verts += [(0.006, v[1], v[2]) for v in verts]
    cup = K._object("cup", verts, [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)], 30.0)
    cup.matrix_world = Matrix.Translation((0.016, 0.0, 0.0))
    M.add(cup, "paint", ink, 0.1)
    ring = lathe("handle", [(0.05, -0.003), (0.08, -0.003), (0.08, 0.003), (0.05, 0.003)], 10 if lod == 0 else 6)
    ring.matrix_world = Matrix.Translation((0.019, cy + 0.16 * side, cz + 0.05)) @ Matrix.Rotation(math.radians(90), 4, "Y")
    M.add(ring, "paint", ink, 0.1)
    M.add(box("saucer", (0.019, cy, cz - 0.13), (0.006, 0.42, 0.035)), "paint", ink, 0.1)
    if lod == 0:
        for k, dy in enumerate((-0.07, 0.0, 0.07)):
            M.add(box("steam", (0.019, cy + dy * side, cz + 0.28 + 0.02 * (k % 2)), (0.004, 0.025, 0.12), rot=Matrix.Rotation(math.radians(12 * (1 - k)), 4, "X")), "paint", ink, 0.1)
    # (the arrow, pointing along +y: a shaft and a head)
    ay, az = 0.3 * side, zc - 0.02
    M.add(box("shaft", (0.019, ay - 0.06 * side, az), (0.006, 0.3, 0.07)), "paint", ink, 0.1)
    verts = [(0.0, ay + 0.09 * side, az + 0.14), (0.0, ay + 0.3 * side, az), (0.0, ay + 0.09 * side, az - 0.14)]
    verts += [(0.006, v[1], v[2]) for v in verts]
    head = K._object("head", verts, [(0, 2, 1), (3, 4, 5), (0, 1, 4, 3), (1, 2, 5, 4), (2, 0, 3, 5)], 30.0)
    head.matrix_world = Matrix.Translation((0.016, 0.0, 0.0))
    M.add(head, "paint", ink, 0.1)
    return M


def build(lod):
    s = ("", "_LOD1", "_LOD2")[lod]
    out = [burgervan("SM_Prop_road_burgervan_%d%s" % (i, s), i, lod) for i in range(len(VANS))]
    out += [flag("SM_Prop_road_flag_0" + s, lod), laybysign("SM_Prop_road_laybysign_0" + s, lod)]
    out += [cafesign("SM_Prop_road_cafesign_%d%s" % (i, s), i % 2, lod, 1.0 if i < 2 else -1.0) for i in range(4)]
    return out


def render_sheet(models, out_dir, title, sheet_name, yaw=32.0, pitch=12.0, cols=3, size=(560, 400)):
    """Each model from its front (+x) corner, whole in the frame, then a sheet of them (the farm kit's look)."""
    import blender_warehouse_kit_render as R
    from PIL import Image, ImageDraw

    class _U:
        def __init__(self, M):
            self.name, self.parts = M.name, M.parts
            for p in self.parts:
                p.lod = 2
    os.makedirs(out_dir, exist_ok=True)
    units = [_U(M) for M in models]
    sc = R._scene(units)
    shots = []
    for U in units:
        a = os.path.join(out_dir, U.name + ".png")
        R._show(sc, U, 0)
        lo, hi = R._bounds(U)
        lo.z = max(lo.z, 0.0)
        R._shoot(sc, U, yaw, pitch, size, a, lens=50.0, focus=(lo + hi) * 0.5, zoom=max((hi - lo).length * 0.6, (hi.z - lo.z) * 1.05))
        shots.append((U, a))
    pad, head = 8, 24
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * size[0] + (cols + 1) * pad, 70 + rows * (size[1] + head + pad)), (245, 245, 242))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 18), title, fill=(20, 20, 20), font=R._font(24))
    for i, (U, a) in enumerate(shots):
        x = pad + (i % cols) * (size[0] + pad)
        y = 70 + (i // cols) * (size[1] + head + pad)
        d.text((x, y + 2), U.name.replace("SM_Prop_", ""), fill=(25, 25, 25), font=R._font(15))
        sheet.paste(Image.open(a).convert("RGB"), (x, y + head))
    path = os.path.join(out_dir, sheet_name)
    sheet.save(path)
    print("sheet:", path)
    return path


def main():
    import blender_farm_props as FP
    args = sys.argv[1:]
    out = {}
    for lod in (0, 1, 2):
        K.clear()
        models = build(lod)
        for M in models:
            nt, y0, y1, x0, x1, z0, z1 = K.write(M)
            out[M.name] = (nt, y0, y1)
            print("%-34s %6d triangles, %.2f..%.2f m up, %.1f x %.1f m" % (M.name, nt, y0, y1, x1 - x0, z1 - z0))
        if lod == 0 and "--render" in args:
            render_sheet(models, args[args.index("--render") + 1],
                         "Ride UK's lay-by and roadside cafe set (our own models; each with two hand-made distance versions)",
                         "roadside_props.png")
    FP.KINDS = KINDS
    FP.write_kinds(out)


if __name__ == "__main__":
    main()
