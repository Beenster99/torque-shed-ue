"""[race tracks stage 4] The race circuits' trackside kit, modelled in Blender (the plan "UK race tracks you can
ride", approved by beenster Nov 19). Our own models, no sponsors, no logos, no lettering.

Writes Data/RideUK/Props/SM_Prop_<kind>_<n>.glb and sets each kind's height in Data/RideUK/prop_kit.json to the
model's own, as Tools/blender_street_kit.py does (same material, M_RideStreetKit, same frame: front +x, foot at 0,
y up in the file). World/WorldRideCircuits.cpp (RidePlan::IndexCircuits) places them:

  armco 0          4 m of double steel crash barrier (W-beam) on its posts: along the straights, at the run-off's edge
  tyre_wall 0      2 m of tyres stacked four high, bolted, the top ones painted: the corners' outsides
  pit_wall 0       4 m of concrete pit wall with a debris fence on it: between the pit lane and the track
  marshal_post 0   a marshals' post: a block hut open to the track, its roof, its flag board
  braking_board 0..2   the countdown boards before a hard corner (three, two and one stripes), on two posts
  start_gantry 0   the start lights' gantry over the line: two columns, the beam, the light box facing the grid
  grandstand 0     a 10 m bay of open steel grandstand: stepped seating under a pitched roof on columns

The long pieces (armco, tyre wall, pit wall) run along +x from 0 to their length, so the game lays them end to end
(Stretch fits a piece to its run). The others face the track with their +x.

Needs Blender's Python module (pip install bpy):

    python Tools/blender_circuit_kit.py               # the models
    python Tools/blender_circuit_kit.py --render DIR  # and a review sheet in DIR
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
import blender_street_kit as K   # noqa: E402  (its geometry helpers, writer and kit file)

# (linear colours)
GALV = (0.5, 0.51, 0.5)
GALV_DULL = (0.36, 0.37, 0.36)
RUBBER = (0.018, 0.018, 0.019)
TYRE_WHITE = (0.72, 0.72, 0.7)
TYRE_RED = (0.5, 0.03, 0.025)
WALL_WHITE = (0.66, 0.66, 0.62)
CONCRETE = (0.34, 0.33, 0.3)
BLOCK = (0.42, 0.41, 0.38)
ROOF = (0.16, 0.18, 0.2)
BOARD_WHITE = (0.78, 0.78, 0.76)
BLACK = (0.02, 0.02, 0.02)
GANTRY_GREY = (0.22, 0.23, 0.24)
LIGHT_OFF = (0.06, 0.012, 0.01)
SEAT_BLUE = (0.05, 0.09, 0.2)
STEEL_GREY = (0.3, 0.31, 0.32)


def tyre(name, x, y, z, r_out=0.31, r_in=0.19, h=0.22, seg=12):
    """A car tyre lying flat: its tread (outside), its bead (inside), its two sidewalls, foot at z."""
    verts, faces = [], []
    for k in range(seg):
        a = 2 * math.pi * k / seg
        c, s = math.cos(a), math.sin(a)
        # (each spoke: outer bottom, outer top, inner top, inner bottom; the tread a little rounded)
        verts += [(x + r_out * 0.94 * c, y + r_out * 0.94 * s, z), (x + r_out * c, y + r_out * s, z + h * 0.5),
                  (x + r_out * 0.94 * c, y + r_out * 0.94 * s, z + h), (x + r_in * c, y + r_in * s, z + h), (x + r_in * c, y + r_in * s, z)]
    n = 5
    for k in range(seg):
        a, b = k * n, ((k + 1) % seg) * n
        for i in range(n):
            j = (i + 1) % n
            faces.append((a + i, b + i, b + j, a + j))
    return K._object(name, verts, faces, 50.0)


def w_beam(name, x0, x1, z_mid, y_face, side=1, seg_x=2):
    """The W-beam rail: a thin steel sheet in two humps, 0.31 m deep, its humps out to side (+1 +y, -1 -y), from x0 to x1."""
    prof = [(0.0, -0.155), (0.03, -0.12), (0.045, -0.08), (0.03, -0.04), (0.0, 0.0), (0.03, 0.04), (0.045, 0.08), (0.03, 0.12), (0.0, 0.155)]
    t = 0.006
    verts, faces = [], []
    xs = [x0 + (x1 - x0) * i / seg_x for i in range(seg_x + 1)]
    n = len(prof)
    for x in xs:
        for dy, dz in prof:
            verts.append((x, y_face + side * dy, z_mid + dz))
        for dy, dz in prof:
            verts.append((x, y_face + side * (dy - t), z_mid + dz))
    row = 2 * n
    for i in range(len(xs) - 1):
        a, b = i * row, (i + 1) * row
        for k in range(n - 1):
            faces.append((a + k, a + k + 1, b + k + 1, b + k))               # front
            faces.append((a + n + k + 1, a + n + k, b + n + k, b + n + k + 1))   # back
        faces.append((a, b, b + n, a + n))                                   # bottom edge
        faces.append((a + n - 1, a + 2 * n - 1, b + 2 * n - 1, b + n - 1))   # top edge
    a, b = 0, (len(xs) - 1) * row
    faces.append(tuple(range(a, a + n)) + tuple(range(a + 2 * n - 1, a + n - 1, -1)))
    faces.append(tuple(range(b + n, b + 2 * n)) + tuple(range(b + n - 1, b - 1, -1)))
    return K._object(name, verts, faces, 20.0)


def armco():
    """Double-rail crash barrier: two W-beams (one over the other) on Z-section posts every 2 m, their
    spacer blocks, the rails' splice bolts; both faces railed (a barrier between two tracks is met from
    either side), 0.78 m high."""
    M = K.Model("SM_Prop_armco_0")
    L = 4.0
    for x in (1.0, 3.0):
        M.add(K.box("post", (x, 0, 0.42 - 0.25), (0.1, 0.06, 0.84 + 0.5)), "galv", GALV_DULL, 0.45)
        M.add(K.box("spacer", (x, 0, 0.6), (0.08, 0.3, 0.3)), "galv", GALV_DULL, 0.35)
    for side in (1, -1):
        for zm in (0.41, 0.6):
            M.add(w_beam("rail", 0.0, L, zm, side * 0.17, side), "galv", GALV, 0.25)
        for x in (0.15, L - 0.15):
            for zz in (0.36, 0.46, 0.55, 0.65):
                M.add(K.rod("bolt", (x, side * 0.2, zz), (x, side * 0.23, zz), 0.012, 6), "steel", GALV_DULL, 0.4)
    return M


def tyre_wall():
    """Tyres four high in a row, 2 m of it, bolted through; the top layer painted red and white in turn, so
    the wall reads from the bike."""
    M = K.Model("SM_Prop_tyre_wall_0")
    L, d = 2.0, 0.64
    n = int(round(L / d))
    for layer in range(4):
        for i in range(n):
            x = d * 0.5 + i * (L / n)
            col, dirt = RUBBER, 0.5
            if layer == 3:
                col, dirt = (TYRE_RED if i % 2 == 0 else TYRE_WHITE), 0.35
            M.add(tyre("tyre", x, 0.0, layer * 0.23, h=0.22), "paint", col, dirt)
    # (the bolt through each stack, its washer on top)
    for i in range(n):
        x = d * 0.5 + i * (L / n)
        M.add(K.lathe("washer", [(0.0, 0.92), (0.06, 0.92), (0.06, 0.93), (0.0, 0.935)], 10, at=(x + 0.25, 0, 0)), "steel", (0.3, 0.3, 0.3), 0.5)
    return M


def pit_wall():
    """4 m of concrete pit wall, 1.05 m high and 0.4 thick, painted white, its debris fence of posts and
    mesh-like wires to 2.6 m."""
    M = K.Model("SM_Prop_pit_wall_0")
    L = 4.0
    v = [(0, -0.2, -0.3), (L, -0.2, -0.3), (L, 0.2, -0.3), (0, 0.2, -0.3), (0, -0.17, 1.05), (L, -0.17, 1.05), (L, 0.17, 1.05), (0, 0.17, 1.05)]
    f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    M.add(K._object("wall", v, f, 30.0), "concrete", WALL_WHITE, 0.3)
    for x in (0.0, 2.0):
        M.add(K.rod("fencepost", (x + 0.05, 0.0, 1.0), (x + 0.05, 0.0, 2.6), 0.04, 8), "galv", GALV, 0.3)
    for z in np.arange(1.15, 2.61, 0.18):
        M.add(K.rod("wire", (0.0, 0.0, float(z)), (L, 0.0, float(z)), 0.006, 4), "steel", GALV_DULL, 0.3)
    for x in np.arange(0.25, L, 0.25):
        M.add(K.rod("upright", (float(x), 0.0, 1.05), (float(x), 0.0, 2.6), 0.004, 4), "steel", GALV_DULL, 0.3)
    return M


def marshal_post():
    """A marshals' post: a 2.4 m block hut, its front (+x, the track) open above a low wall, a flat roof
    overhanging the front, the flag board on its side, a fire extinguisher stand."""
    M = K.Model("SM_Prop_marshal_post_0")
    w, dep, h = 2.4, 1.8, 2.3
    M.add(K.box("back", (-dep / 2 + 0.1, 0, h / 2), (0.2, w, h)), "concrete", BLOCK, 0.4)
    for s in (1, -1):
        M.add(K.box("side", (0, s * (w / 2 - 0.1), h / 2), (dep, 0.2, h)), "concrete", BLOCK, 0.4)
    M.add(K.box("front", (dep / 2 - 0.1, 0, 0.5), (0.2, w, 1.0)), "concrete", BLOCK, 0.4)
    M.add(K.box("roof", (0.15, 0, h + 0.06), (dep + 0.6, w + 0.3, 0.12), bevel=0.01), "paint", ROOF, 0.3)
    M.add(K.box("floor", (0, 0, 0.05), (dep, w, 0.1)), "concrete", CONCRETE, 0.5)
    # (the flag board on the left side, its pegs; flags furled, in their colours, no lettering)
    M.add(K.box("board", (0.1, w / 2 + 0.02, 1.4), (1.2, 0.03, 0.7)), "paint", BOARD_WHITE, 0.3)
    for i, c in enumerate(((0.75, 0.6, 0.02), (0.02, 0.15, 0.5), (0.5, 0.03, 0.02), (0.05, 0.3, 0.06), BLACK)):
        x = -0.35 + i * 0.2
        M.add(K.rod("flagpole", (x, w / 2 + 0.05, 1.15), (x, w / 2 + 0.05, 1.7), 0.01, 6), "timber", (0.3, 0.22, 0.12), 0.3)
        M.add(K.box("flag", (x, w / 2 + 0.06, 1.55), (0.07, 0.05, 0.25)), "paint", c, 0.2)
    # (the extinguisher on its stand by the front, red)
    M.add(K.lathe("extinguisher", [(0.0, 0.0), (0.08, 0.0), (0.08, 0.5), (0.05, 0.56), (0.0, 0.58)], 12, at=(dep / 2 + 0.25, -w / 2 + 0.25, 0.0)), "paint", (0.55, 0.03, 0.02), 0.2)
    return M


def braking_board(stripes):
    """A countdown board before a hard corner: a white board on two posts, diagonal black stripes (three,
    two, one), facing +x (the riders coming). All three are one height, 2.0 m."""
    M = K.Model("SM_Prop_braking_board_%d" % (3 - stripes))
    bw, bh, z0 = 1.1, 1.3, 0.7
    for y in (-0.4, 0.4):
        M.add(K.box("post", (-0.05, y, (z0 + bh) / 2 - 0.25), (0.08, 0.08, z0 + bh + 0.5)), "galv", GALV_DULL, 0.4)
    M.add(K.box("board", (0.0, 0, z0 + bh / 2), (0.04, bw, bh), bevel=0.004), "paint", BOARD_WHITE, 0.2)
    # (the stripes: parallelograms across the board's face, rising to the right as the rider sees it)
    gap = bw / (stripes + 1)
    for i in range(stripes):
        yc = -bw / 2 + gap * (i + 1)
        sw = 0.14
        verts = []
        for xo in (0.021, 0.026):
            verts += [(xo, yc - sw / 2 - 0.18, z0 + 0.06), (xo, yc + sw / 2 - 0.18, z0 + 0.06), (xo, yc + sw / 2 + 0.18, z0 + bh - 0.06), (xo, yc - sw / 2 + 0.18, z0 + bh - 0.06)]
        f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        M.add(K._object("stripe", verts, f, 30.0), "paint", BLACK, 0.15)
    return M


def start_gantry():
    """The start lights' gantry: two square steel columns 16 m apart (either side of the track), a box
    beam across at 5.6 m, and the light box in its middle facing +x (the grid), five pairs of red lamps
    (off: the game lights none)."""
    M = K.Model("SM_Prop_start_gantry_0")
    span, top = 16.0, 5.6
    for s in (1, -1):
        M.add(K.box("column", (0, s * span / 2, (top + 0.3) / 2 - 0.15), (0.4, 0.4, top + 0.6)), "paint", GANTRY_GREY, 0.3)
        M.add(K.box("foot", (0, s * span / 2, 0.1), (0.8, 0.8, 0.3)), "concrete", CONCRETE, 0.5)
    M.add(K.box("beam", (0, 0, top + 0.2), (0.5, span + 0.4, 0.6), bevel=0.01), "paint", GANTRY_GREY, 0.3)
    M.add(K.box("lightbox", (0.15, 0, top - 0.4), (0.4, 3.2, 0.9), bevel=0.02), "paint", BLACK, 0.2)
    Ry = Matrix.Rotation(math.pi / 2, 4, "Y")
    for i in range(5):
        y = -1.2 + i * 0.6
        for z in (top - 0.18, top - 0.62):
            lamp = K.lathe("lamp", [(0.14, 0.0), (0.12, 0.012), (0.07, 0.02), (0.0, 0.022)], 14)
            lamp.matrix_world = Matrix.Translation((0.35, y, z)) @ Ry
            M.add(lamp, "lens", LIGHT_OFF, 0.05)
            M.add(K.hood("hood", 0.35, y, z, 0.15, 0.006, 0.16, 0.08, 100.0), "paint", BLACK, 0.2)
    return M


def grandstand():
    """A 10 m bay (across y) of open steel grandstand: eight stepped rows of seats rising away from the
    track (-x), a back wall, a roof pitched up to the front on four columns, 7.5 m tall. Faces +x."""
    M = K.Model("SM_Prop_grandstand_0")
    w, rows, tread, rise = 10.0, 8, 0.8, 0.42
    for r in range(rows):
        x = -r * tread
        z = 0.5 + r * rise
        M.add(K.box("step", (x - tread / 2, 0, z / 2), (tread, w, z)), "concrete", CONCRETE, 0.45)
        M.add(K.box("seats", (x - tread * 0.35, 0, z + 0.2), (0.35, w - 0.3, 0.4)), "paint", SEAT_BLUE, 0.3)
    back = -rows * tread
    zt = 0.5 + rows * rise
    M.add(K.box("backwall", (back - 0.1, 0, (zt + 2.2) / 2), (0.2, w, zt + 2.2)), "paint", STEEL_GREY, 0.35)
    for y in (-w / 2 + 0.1, w / 2 - 0.1):
        M.add(K.box("endwall", (back / 2, y, (zt + 1.0) / 2), (-back, 0.15, zt + 1.0)), "paint", STEEL_GREY, 0.35)
    # (the roof: a sheet from the back wall's top, up and forward, held on columns at the front and back)
    front_x = 0.6
    z_back, z_front = zt + 2.2, 7.5
    pitch = math.atan2(z_front - z_back, front_x - back)
    length = math.hypot(z_front - z_back, front_x - back)
    roof = K.box("roof", (0, 0, 0), (length, w + 0.6, 0.1))
    roof.matrix_world = Matrix.Translation(((front_x + back) / 2, 0, (z_front + z_back) / 2 - 0.05)) @ Matrix.Rotation(-pitch, 4, "Y")
    M.add(roof, "paint", ROOF, 0.3)
    for y in (-w / 2 + 0.3, w / 2 - 0.3):
        M.add(K.box("column", (front_x - 0.2, y, (z_front - 0.1) / 2), (0.25, 0.25, z_front - 0.1)), "paint", STEEL_GREY, 0.3)
    M.add(K.box("rail", (0.15, 0, 1.05), (0.05, w, 0.05)), "galv", GALV, 0.3)
    return M


def build():
    K.clear()
    return [armco(), tyre_wall(), pit_wall(), marshal_post(), braking_board(3), braking_board(2), braking_board(1), start_gantry(), grandstand()]


def main():
    models = build()
    heights = {}
    for M in models:
        nt, y0, y1, x0, x1, z0, z1 = K.write(M)
        kind = M.name[len("SM_Prop_"):].rsplit("_", 1)[0]
        heights.setdefault(kind, []).append((y0, y1, x0, x1))
        print("%s: %d triangles, %.3f..%.3f m up, x %.2f..%.2f, z %.2f..%.2f" % (M.name, nt, y0, y1, x0, x1, z0, z1))
    kit = json.load(open(K.KIT))
    reach = {"armco": 500, "tyre_wall": 500, "pit_wall": 500, "marshal_post": 600, "braking_board": 500, "start_gantry": 1200, "grandstand": 1500}
    shadow = {"armco": False, "tyre_wall": False, "pit_wall": True, "marshal_post": True, "braking_board": False, "start_gantry": True, "grandstand": True}
    lines = open(K.KIT).read().split("\n")
    for kind, spans in heights.items():
        tall = [y1 - y0 for y0, y1, _, _ in spans]
        if max(tall) - min(tall) > 0.02:
            raise SystemExit("%s: its models are of different heights (%s): they would be scaled to fit" % (kind, tall))
        row = {"words": [], "reach": reach[kind], "height": round(float(max(tall)), 3), "shadow": shadow[kind], "credit": ""}
        head = '  "%s": ' % kind
        for i, ln in enumerate(lines):
            if ln.startswith(head):
                comma = ln.rstrip().endswith(",")
                lines[i] = head + json.dumps(row) + ("," if comma else "")
                break
        else:
            # (a new kind: its row after traffic_signal's)
            at = next(i for i, ln in enumerate(lines) if ln.startswith('  "traffic_signal": '))
            lines.insert(at + 1, head + json.dumps(row) + ",")
    open(K.KIT, "w").write("\n".join(lines))
    json.load(open(K.KIT))
    return models


def render_sheet(models, out_dir):
    from blender_street_kit_render import _scene, _shoot
    from PIL import Image, ImageDraw, ImageFont
    from make_street_kit_textures import SURFACES
    os.makedirs(out_dir, exist_ok=True)
    _scene(models, K.TEX, SURFACES)
    sc = bpy.context.scene
    by = {M.name: M for M in models}
    shots = [
        ("SM_Prop_armco_0", "Armco barrier, 4 m", (2.0, 0, 0.45), 6.0, 50, 14, 50),
        ("SM_Prop_tyre_wall_0", "Tyre wall, 2 m", (1.0, 0, 0.5), 4.5, 40, 16, 50),
        ("SM_Prop_pit_wall_0", "Pit wall and fence", (2.0, 0, 1.3), 7.0, 35, 10, 50),
        ("SM_Prop_marshal_post_0", "Marshals' post", (0, 0, 1.2), 7.5, 30, 12, 50),
        ("SM_Prop_braking_board_0", "Braking boards", (0, 0, 1.3), 5.0, 15, 6, 50),
        ("SM_Prop_start_gantry_0", "Start gantry", (0, 0, 3.2), 22.0, 25, 8, 50),
        ("SM_Prop_grandstand_0", "Grandstand bay", (-2.5, 0, 3.5), 20.0, 30, 12, 50),
        ("SM_Prop_start_gantry_0", "Start lights", (0.3, 0, 5.2), 4.5, 10, -4, 50),
    ]
    files = []
    for i, (name, label, target, dist, yaw, pitch, lens) in enumerate(shots):
        f = os.path.join(out_dir, "circuit_%02d.png" % i)
        objs = [p.obj for p in by[name].parts]
        if name == "SM_Prop_braking_board_0":
            # (the three boards side by side, as they stand before a corner, nearest last)
            for j, nm in enumerate(("SM_Prop_braking_board_0", "SM_Prop_braking_board_1", "SM_Prop_braking_board_2")):
                for p in by[nm].parts:
                    p.obj.matrix_world = Matrix.Translation((0, (j - 1) * 1.6, 0)) @ p.obj.matrix_world
                    if nm != name:
                        objs.append(p.obj)
        _shoot(sc, objs, target, dist, yaw, pitch, lens, (520, 400), f)
        files.append((f, label))
    font = None
    for cand in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf"):
        if os.path.exists(cand):
            font = ImageFont.truetype(cand, 18)
    pad, cols = 10, 4
    W = cols * 520 + (cols + 1) * pad
    H = 50 + 2 * (400 + 28 + pad)
    sheet = Image.new("RGB", (W, H), (245, 245, 242))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 12), "Race circuits: trackside kit (Blender renders, the street kit's material; no sponsors, no lettering)", fill=(20, 20, 20), font=font)
    for i, (f, label) in enumerate(files):
        im = Image.open(f).convert("RGB")
        x = pad + (i % cols) * (520 + pad)
        y = 50 + (i // cols) * (400 + 28 + pad)
        sheet.paste(im, (x, y))
        d.text((x, y + 402), label, fill=(30, 30, 30), font=font)
    path = os.path.join(out_dir, "circuit_kit_review.png")
    sheet.save(path)
    print("review sheet:", path)
    return path


if __name__ == "__main__":
    ms = main()
    if "--render" in sys.argv:
        render_sheet(ms, sys.argv[sys.argv.index("--render") + 1])
