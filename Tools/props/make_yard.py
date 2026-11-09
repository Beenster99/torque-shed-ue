"""Builds Torque Shed's yard kit and the yard's own fixtures as glbs in Blender, into
Data/Props/Blender/<id>.glb beside the shed props (make_props.py), with the same kit (kit.py), frame
and materials (Data/Props/materials.json), so Tools/ue_import_props.py brings them in the same way.

    blender -b --factory-startup -P Tools/props/make_yard.py                 every yard model
    blender -b --factory-startup -P Tools/props/make_yard.py -- cone bus     just these
    blender -b --factory-startup -P Tools/props/make_yard.py -- --list       what there is

The kit is Torque Shed's equipment.gd yard kinds (ramps, things to jump, walls and fences, cones,
posts and flags), drawn to the same footprints and, for the ramps and the tabletop, the same deck
profiles as yard_kit.gd (PROFILES: the deck's slope eased up from the toe, a clean lip), so the bike
rides the drawn deck. The fixtures are world_yard.gd's built-in pieces: speed humps, the whoops'
earth rollers, the braking-zone boards and the bunting. Torque Shed's frame (kit.py): metres, x right,
y up, +z the front; a ramp's toe is at +z and it rises towards -z (ride it from the front).
Made-up liveries and no real makes or logos anywhere. Deterministic: committed, the PC never needs Blender.
"""
import json
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kit  # noqa: E402
from kit import box, cyl, tube, torus, prism, sphere  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "Data", "Props", "Blender")
MODELS = {}


def model(pid, size):
    def wrap(fn):
        MODELS[pid] = (fn, size)
        return fn
    return wrap


# --- Meshes the kit hasn't got ---------------------------------------------------------------------------

def mesh(name, verts, faces, mat, bevel=0.0):
    """Raw faces (lists of vertex indices) on vertices in Torque Shed's frame."""
    import bmesh
    bm = bmesh.new()
    vs = [bm.verts.new(kit.B(v)) for v in verts]
    for f in faces:
        try:
            bm.faces.new([vs[i] for i in f])
        except ValueError:
            pass   # a face twice (a degenerate end): skip it
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    obj = kit._new(name, bm)
    return kit._finish(obj, mat, bevel)


def loft(name, sections, mat, closed=True, caps=True, bevel=0.0):
    """Joins cross-sections (each a list of points, the same count) into a skin, end caps on."""
    verts, faces = [], []
    n = len(sections[0])
    for s in sections:
        verts.extend(s)
    rings = len(sections)
    for r in range(rings - 1):
        for i in range(n if closed else n - 1):
            j = (i + 1) % n
            faces.append([r * n + i, r * n + j, (r + 1) * n + j, (r + 1) * n + i])
    if caps:
        faces.append(list(range(n))[::-1])
        faces.append([(rings - 1) * n + i for i in range(n)])
    return mesh(name, verts, faces, mat, bevel)


# Torque Shed's deck profiles (yard_kit.gd PROFILES): the slope as a share of its steepest, from the
# toe (s 0, +z, on the ground) to the far end (s 1, -z), straight between keys; heights follow.
PROFILES = {
    "launch": [(0.0, 0.0), (0.3, 1.0), (1.0, 1.0)],
    "kicker": [(0.0, 0.0), (1.0, 1.0)],
    "landing": [(0.0, 0.0), (0.3, 1.0), (0.82, 1.0), (0.92, 0.0), (1.0, 0.0)],
    "table": [(0.0, 0.0), (0.08, 1.0), (0.28, 1.0), (0.36, 0.0), (0.64, 0.0), (0.72, -1.0), (0.92, -1.0), (1.0, 0.0)],
}
TABLE_BANK = 0.6


def _shape(keys, s):
    for a, b in zip(keys, keys[1:]):
        if s <= b[0]:
            return a[1] + (b[1] - a[1]) * (s - a[0]) / max(b[0] - a[0], 1e-6)
    return keys[-1][1]


def profile(kind, length, height):
    """The deck's (z, y) points from the toe (z +length/2) to the far end, as yard_kit.gd profile()."""
    keys = PROFILES[kind]
    n = max(24, min(240, int(math.ceil(length / 0.06))))
    ys, acc = [0.0], 0.0
    for i in range(n):
        s0, s1 = i / n, (i + 1) / n
        acc += (_shape(keys, s0) + 4.0 * _shape(keys, (s0 + s1) * 0.5) + _shape(keys, s1)) / 6.0 / n
        ys.append(acc)
    top = max(ys)
    return [(length * 0.5 - length * i / n, max(ys[i], 0.0) / top * height) for i in range(n + 1)]


def thin(points, step):
    """Every point at least `step` metres along from the last kept (the ends always kept)."""
    out = [points[0]]
    for p in points[1:-1]:
        if math.hypot(p[0] - out[-1][0], p[1] - out[-1][1]) >= step:
            out.append(p)
    out.append(points[-1])
    return out


def deck_at(pts, z):
    for a, b in zip(pts, pts[1:]):
        if a[0] >= z >= b[0]:
            t = (a[0] - z) / max(a[0] - b[0], 1e-6)
            return a[1] + (b[1] - a[1]) * t
    return 0.0


def slab(name, pts, x0, x1, thick, mat, lift=0.0):
    """A sheet laid along the deck line `pts` ((z, y), toe first) from x0 to x1, `thick` deep under it."""
    secs = []
    for i, (z, y) in enumerate(pts):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, len(pts) - 1)]
        dz, dy = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dz, dy) or 1.0
        nz, ny = -dy / L, dz / L   # the deck's normal in (z, y): up off the deck
        if ny < 0:
            nz, ny = -nz, -ny
        top = (z + nz * lift, y + ny * lift)
        bot = (z - nz * thick, y - ny * thick)
        secs.append([(x0, top[1], top[0]), (x1, top[1], top[0]), (x1, bot[1], bot[0]), (x0, bot[1], bot[0])])
    return loft(name, secs, mat)


def side_panel(name, pts, x, depth, mat, y0=0.0):
    """A board along a ramp's side: under the deck line down to the ground."""
    outline = [(z, max(y, y0 + 0.002)) for z, y in pts] + [(pts[-1][0], y0), (pts[0][0], y0)]
    return prism(name, outline, depth, mat, axis="x", centre=x, bevel=0.0)


# --- Ramps ---------------------------------------------------------------------------------------------

def wood_ramp(W, H, L, kind):
    pts = profile(kind, L, H)
    deck = thin(pts, 0.05)
    hw = W * 0.5
    # Plywood sheets along the deck, a hairline gap every 1.2 m, steel plates at the toe and the lip.
    run = [0.0]
    for a, b in zip(deck, deck[1:]):
        run.append(run[-1] + math.hypot(a[0] - b[0], a[1] - b[1]))
    sheet, start = 0, 0
    for i in range(1, len(deck)):
        if run[i] - run[start] >= 1.2 or i == len(deck) - 1:
            slab("Deck%d" % sheet, deck[start:i + 1], -hw + 0.01, hw - 0.01, 0.018, "plywood")
            sheet += 1
            start = i
    toe = [p for p in deck if p[0] >= L * 0.5 - 0.16]
    slab("ToePlate", toe, -hw + 0.02, hw - 0.02, 0.004, "steel_dark", lift=0.004)
    if kind != "landing":
        lip = [p for p in deck if p[0] <= -L * 0.5 + 0.12]
        slab("LipPlate", lip, -hw + 0.02, hw - 0.02, 0.004, "steel", lift=0.004)
    # The sides: plywood on the frame, battens screwed on, a hazard band along the top edge.
    under = [(z, y - 0.018) for z, y in deck]
    for s in (-1, 1):
        side_panel("Side%d" % s, under, s * (hw - 0.009), 0.018, "plywood")
        band = [(z, y) for z, y in under]
        top = [(z, y) for z, y in band]
        low = [(z, max(y - 0.06, 0.0)) for z, y in band]
        mesh("Band%d" % s, [(s * (hw + 0.001), y, z) for z, y in top] + [(s * (hw + 0.001), y, z) for z, y in low],
             [[i, i + 1, len(top) + i + 1, len(top) + i] for i in range(len(top) - 1)], "paint_yellow")
        z = L * 0.5 - 0.4
        while z > -L * 0.5 + 0.1:
            h = deck_at(under, z)
            if h > 0.1:
                box("Batten%d%.2f" % (s, z), (s * (hw + 0.012), h * 0.5, z), (0.024, h - 0.02, 0.045), "crate_wood_dark", bevel=0.003)
            z -= 0.6
    # The end that stands up off the ground: a closed panel with a cross-brace.
    end_h = deck[-1][1]
    if end_h > 0.05:
        zb = -L * 0.5 + 0.009
        box("Back", (0.0, end_h * 0.5, zb), (W - 0.02, end_h - 0.02, 0.018), "plywood", bevel=0.002)
        box("BackBatten", (0.0, end_h * 0.5, zb - 0.02), (0.045, end_h - 0.04, 0.024), "crate_wood_dark", bevel=0.003)
    # Skids along the ground.
    for s in (-1, 1):
        box("Skid%d" % s, (s * (hw - 0.05), 0.02, 0.0), (0.07, 0.04, L - 0.1), "crate_wood_dark", bevel=0.004)


def steel_ramp(W, H, L, kind, paint):
    pts = profile(kind, L, H)
    deck = thin(pts, 0.08)
    hw = W * 0.5
    # A chequer plate deck in sections, the grip plate's rows as thin raised strips every 25 cm.
    slab("Deck", deck, -hw, hw, 0.012, "steel_dark")
    run = 0.0
    for i, (a, b) in enumerate(zip(deck, deck[1:])):
        seg = math.hypot(a[0] - b[0], a[1] - b[1])
        if int((run + seg) / 0.25) != int(run / 0.25):
            ang = math.degrees(math.atan2(b[1] - a[1], a[0] - b[0]))
            box("Grip%d" % i, (0.0, (a[1] + b[1]) * 0.5 + 0.004, (a[0] + b[0]) * 0.5), (W - 0.1, 0.006, 0.02), "steel", bevel=0.0, rot=(ang, 0, 0))
        run += seg
    # Channel stringers under each edge and down the middle, following the deck.
    for x in (-hw + 0.05, 0.0, hw - 0.05):
        stringer = [(z, y - 0.012) for z, y in deck]
        slab("Stringer%.2f" % x, stringer, x - 0.04, x + 0.04, 0.14, paint)
    # Legs on foot plates every metre or so, ties across and diagonal braces down each side.
    n = max(2, int(L / 1.1))
    legs = []
    for k in range(n + 1):
        z = L * 0.5 - 0.5 - (L - 0.6) * k / n
        h = deck_at(deck, z) - 0.16
        if h < 0.12:
            continue
        legs.append((z, h))
        for s in (-1, 1):
            box("Leg%d%d" % (s, k), (s * (hw - 0.06), h * 0.5, z), (0.08, h, 0.08), paint, bevel=0.006)
            box("Foot%d%d" % (s, k), (s * (hw - 0.06), 0.006, z), (0.2, 0.012, 0.2), "steel_dark", bevel=0.002)
        box("Tie%d" % k, (0.0, h - 0.05, z), (W - 0.12, 0.06, 0.06), paint, bevel=0.005)
        if h > 0.6:
            box("TieLow%d" % k, (0.0, 0.25, z), (W - 0.12, 0.05, 0.05), paint, bevel=0.005)
    for (za, ha), (zb, hb) in zip(legs, legs[1:]):
        for s in (-1, 1):
            tube("Brace%d%.2f" % (s, za), [(s * (hw - 0.06), 0.08, za), (s * (hw - 0.06), hb - 0.08, zb)], 0.022, paint, sides=8)
    for s in (-1, 1):
        box("Rail%d" % s, (s * (hw - 0.06), 0.03, 0.0), (0.08, 0.06, L - 0.3), paint, bevel=0.005)
    # The lip's back face in hazard stripes (yellow and black).
    end_h = deck[-1][1]
    if end_h > 0.3 and kind != "landing":
        stripes = max(4, int(W / 0.25))
        for k in range(stripes):
            x = -hw + W * (k + 0.5) / stripes
            box("Hazard%d" % k, (x, end_h - 0.08, -L * 0.5 - 0.004), (W / stripes - 0.004, 0.14, 0.008),
                "paint_yellow" if k % 2 == 0 else "paint_black", bevel=0.0)
    # Side kick plates along the deck edge so a foot peg can't catch.
    for s in (-1, 1):
        edge = [(z, y + 0.02) for z, y in deck]
        mesh("Kick%d" % s, [(s * hw, y, z) for z, y in edge] + [(s * hw, y - 0.12, z) for z, y in edge],
             [[i, i + 1, len(edge) + i + 1, len(edge) + i] for i in range(len(edge) - 1)], paint)


@model("ramp_small", (1.6, 0.45, 2.0))
def ramp_small():
    wood_ramp(1.6, 0.45, 2.0, "launch")


@model("ramp_kicker", (1.4, 0.6, 1.6))
def ramp_kicker():
    wood_ramp(1.4, 0.6, 1.6, "kicker")


@model("ramp_medium", (2.0, 0.9, 3.2))
def ramp_medium():
    wood_ramp(2.0, 0.9, 3.2, "launch")


@model("ramp_landing", (2.8, 1.5, 7.0))
def ramp_landing():
    wood_ramp(2.8, 1.5, 7.0, "landing")


@model("ramp_large", (2.6, 1.6, 4.8))
def ramp_large():
    steel_ramp(2.6, 1.6, 4.8, "launch", "paint_grey")


@model("ramp_xl", (2.8, 2.2, 7.0))
def ramp_xl():
    steel_ramp(2.8, 2.2, 7.0, "launch", "paint_blue")


@model("ramp_huge", (3.0, 3.0, 9.0))
def ramp_huge():
    steel_ramp(3.0, 3.0, 9.0, "launch", "paint_red")


@model("landing_xl", (3.0, 2.2, 9.5))
def landing_xl():
    steel_ramp(3.0, 2.2, 9.5, "landing", "paint_blue")


@model("landing_huge", (3.2, 3.0, 13.0))
def landing_huge():
    steel_ramp(3.2, 3.0, 13.0, "landing", "paint_red")


# --- Things to jump ------------------------------------------------------------------------------------

@model("tabletop", (3.2, 1.2, 9.0))
def tabletop():
    """Packed dirt: up, across the flat top, down the far side; banks either side."""
    W, H, L = 3.2, 1.2, 9.0
    pts = thin(profile("table", L, H), 0.06)
    secs = []
    for z, y in pts:
        f = y / H
        top = W * 0.5 - TABLE_BANK * f
        secs.append([(-W * 0.5, 0.0, z), (-top, y, z), (-top * 0.35, y + 0.01 * f, z), (top * 0.35, y + 0.01 * f, z), (top, y, z), (W * 0.5, 0.0, z)])
    loft("Mound", secs, "dirt", closed=True)
    # Ruts down the line, half-buried tyres at the corners, a few stones.
    for x in (-0.35, 0.35):
        rut = [(z, y + 0.004) for z, y in pts]
        slab("Rut%.2f" % x, rut, x - 0.09, x + 0.09, 0.006, "dirt_dark")
    for sx in (-1, 1):
        for sz in (-1, 1):
            torus("Tyre%d%d" % (sx, sz), (sx * (W * 0.5 - 0.1), 0.12, sz * (L * 0.5 - 0.6)), 0.3, 0.1, "rubber", axis="x", verts=20)
    rng = random.Random(7)
    for i in range(14):
        x = rng.uniform(-W * 0.5, W * 0.5)
        z = rng.uniform(-L * 0.5, L * 0.5)
        sphere("Stone%d" % i, (x, 0.02, z), rng.uniform(0.04, 0.09), "stone", scale=(1.0, 0.55, 0.8))


def drum(name, c, r, length, paint, turn):
    """An oil drum lying along z at c: body, two rolling hoops, chimes at the ends, bungs."""
    cyl(name, c, r, length, paint, axis="z", verts=28, bevel=0.01)
    for dz in (-0.28, 0.28):
        torus(name + "Hoop%.2f" % dz, (c[0], c[1], c[2] + dz), r + 0.002, 0.008, paint, axis="z", verts=28)
    for dz in (-length * 0.5, length * 0.5):
        torus(name + "Chime%.2f" % dz, (c[0], c[1], c[2] + dz), r - 0.004, 0.01, "steel", axis="z", verts=28)
    a = turn
    bx, by = c[0] + math.cos(a) * 0.15, c[1] + math.sin(a) * 0.15
    cyl(name + "Bung", (bx, by, c[2] + length * 0.5 + 0.004), 0.03, 0.012, "steel_dark", axis="z", verts=10)
    cyl(name + "Vent", (c[0] - math.cos(a) * 0.17, c[1] - math.sin(a) * 0.17, c[2] + length * 0.5 + 0.004), 0.016, 0.01, "steel_dark", axis="z", verts=8)


@model("barrels", (1.9, 0.62, 0.9))
def barrels():
    for z in (-0.3, 0.3):
        box("Timber%.1f" % z, (0.0, 0.025, z), (1.9, 0.05, 0.1), "crate_wood_dark", bevel=0.008)
    paints = ["drum_blue", "drum_red", "drum_green"]
    for i in range(3):
        drum("Drum%d" % i, (-0.62 + i * 0.62, 0.335, 0.0), 0.285, 0.84, paints[i], i * 1.3)
        # Rust where the paint's worn through on the top.
        box("Rust%d" % i, (-0.62 + i * 0.62 + 0.05, 0.6, 0.1 - i * 0.1), (0.12, 0.01, 0.2), "rust", bevel=0.0)


@model("log", (2.4, 0.45, 0.45))
def log():
    L, r = 2.4, 0.22
    # Bark that isn't a perfect cylinder: rings of points nudged in and out along it.
    rng = random.Random(3)
    secs = []
    for k in range(13):
        x = -L * 0.5 + L * k / 12
        ring = []
        for j in range(18):
            a = 2 * math.pi * j / 18
            rr = r * (1.0 + 0.04 * math.sin(a * 3 + k) + rng.uniform(-0.015, 0.015)) * (1.0 - 0.06 * abs(k - 6) / 6)
            ring.append((x, r + math.sin(a) * rr, math.cos(a) * rr))
        secs.append(ring)
    loft("Bark", secs, "bark")
    for s in (-1, 1):
        cyl("End%d" % s, (s * (L * 0.5 + 0.002), r, 0.0), r * 0.9, 0.006, "log_end", axis="x", verts=18, bevel=0.0)
    cyl("Branch", (0.4, 0.42, 0.04), 0.05, 0.22, "bark", axis="y", verts=10, rot=(17, 0, -29))
    for x in (-0.8, 0.8):
        for s in (-1, 1):
            box("Chock%d%.1f" % (s, x), (x, 0.035, s * 0.2), (0.12, 0.07, 0.12), "crate_wood_dark", bevel=0.01, rot=(s * 30, 0, 0))


@model("tyre_stack", (0.75, 0.9, 0.75))
def tyre_stack():
    for i in range(5):
        off = (math.sin(i * 2.1) * 0.025, 0.095 + i * 0.18, math.cos(i * 1.7) * 0.025)
        torus("Tyre%d" % i, off, 0.27, 0.092, "rubber", axis="y", verts=28)
        # The tread's blocks, a ring of them round the outside.
        for j in range(24):
            a = 2 * math.pi * j / 24 + i * 0.3
            box("Tread%d_%d" % (i, j), (off[0] + math.cos(a) * 0.36, off[1], off[2] + math.sin(a) * 0.36), (0.02, 0.12, 0.05), "rubber",
                bevel=0.004, rot=(0, -math.degrees(a), 0))
        cyl("Rim%d" % i, off, 0.19, 0.05, "rust", axis="y", verts=20, bevel=0.004)


@model("hay_bales", (2.2, 0.5, 1.0))
def hay_bales():
    for x in (-0.55, 0.55):
        yaw = math.degrees(x * 0.05)
        box("Bale%.2f" % x, (x, 0.25, 0.0), (1.05, 0.5, 1.0), "hay", bevel=0.06, rot=(0, yaw, 0))
        for t in (-0.25, 0.25):
            box("Twine%.2f%.2f" % (x, t), (x + t, 0.25, 0.0), (0.015, 0.505, 1.004), "twine", bevel=0.005, rot=(0, yaw, 0))
        # Stalks sticking out of the cut ends.
        rng = random.Random(int(x * 100))
        for k in range(18):
            z = rng.choice((-0.5, 0.5))
            box("Stalk%.2f_%d" % (x, k), (x + rng.uniform(-0.45, 0.45), rng.uniform(0.05, 0.45), z + (0.03 if z > 0 else -0.03)),
                (0.004, 0.004, 0.08), "hay", bevel=0.0, rot=(rng.uniform(-30, 30), rng.uniform(-40, 40), 0))


@model("car_wreck", (1.75, 1.35, 4.0))
def car_wreck():
    body = [(-2.0, 0.32), (1.98, 0.32), (2.0, 0.56), (1.95, 0.78), (1.1, 0.86), (-1.6, 0.89), (-1.98, 0.82), (-2.0, 0.56)]
    prism("Body", body, 1.7, "car_paint", axis="x", bevel=0.03)
    cabin = [(0.95, 0.86), (0.32, 1.33), (-0.95, 1.33), (-1.45, 0.88)]
    prism("Cabin", cabin, 1.5, "car_paint", axis="x", bevel=0.03)
    glass = [(0.8, 0.91), (0.3, 1.28), (-0.9, 1.28), (-1.33, 0.91)]
    for s in (-1, 1):
        prism("Glass%d" % s, glass, 0.006, "glass_dirty", axis="x", centre=s * 0.754, bevel=0.0)
        box("Pillar%d" % s, (s * 0.757, 1.1, -0.28), (0.012, 0.4, 0.09), "car_paint", bevel=0.0)
    # Windscreen and rear screen, just proud of the sloping ends.
    for k, (a, b) in enumerate((((0.95, 0.86), (0.32, 1.33)), ((-0.95, 1.33), (-1.45, 0.88)))):
        dz, dy = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dz, dy)
        # Tilted about x so the pane's height runs up the slope; nudged out off the cabin's face.
        nz, ny = dy / L, -dz / L
        if nz * (a[0] + b[0]) < 0.0:
            nz, ny = -nz, -ny
        cz, cy = (a[0] + b[0]) * 0.5 + nz * 0.006, (a[1] + b[1]) * 0.5 + ny * 0.006
        box("Screen%d" % k, (0.0, cy, cz), (1.36, L - 0.1, 0.006), "glass", bevel=0.0, rot=(math.degrees(math.atan2(dz, dy)), 0, 0))
    for z in (-1.3, 1.3):
        for s in (-1, 1):
            arch = [(z + math.cos(math.pi * i / 8) * 0.36, 0.32 + math.sin(math.pi * i / 8) * 0.3) for i in range(9)]
            prism("Arch%d%.1f" % (s, z), arch, 0.006, "plastic_black", axis="x", centre=s * 0.853, bevel=0.0)
            flat = 0.04 if (z > 0 and s > 0) else 0.0
            torus("Tyre%d%.1f" % (s, z), (s * 0.74, 0.3 - flat, z), 0.21, 0.09, "rubber", axis="x", verts=24)
            cyl("Rim%d%.1f" % (s, z), (s * 0.74, 0.3 - flat, z), 0.16, 0.12, "rust", axis="x", verts=18)
            cyl("Hub%d%.1f" % (s, z), (s * 0.805, 0.3 - flat, z), 0.05, 0.02, "chrome", axis="x", verts=10)
    for e in (-1, 1):
        box("Bumper%d" % e, (0.0, 0.42, e * 2.03), (1.74, 0.11, 0.08), "chrome", bevel=0.02)
        for s in (-1, 1):
            if e > 0:
                cyl("Lamp%d" % s, (s * 0.62, 0.63, 1.99), 0.1, 0.03, "chrome", axis="z", verts=16)
                cyl("Lens%d" % s, (s * 0.62, 0.63, 2.006), 0.08, 0.01, "lamp_lens", axis="z", verts=16)
                box("Indicator%d" % s, (s * 0.75, 0.5, 2.0), (0.12, 0.05, 0.02), "amber_lens", bevel=0.004)
            else:
                box("Tail%d" % s, (s * 0.68, 0.68, -2.0), (0.24, 0.12, 0.02), "red_lens", bevel=0.004)
    box("Grille", (0.0, 0.62, 2.0), (0.9, 0.16, 0.02), "plastic_black", bevel=0.004)
    for i in range(6):
        box("GrilleBar%d" % i, (0.0, 0.56 + i * 0.024, 2.008), (0.86, 0.006, 0.006), "chrome", bevel=0.0)
    for p in ((0.852, 0.4, 0.55), (-0.852, 0.42, -0.7), (0.852, 0.45, -1.75), (-0.852, 0.4, 1.75)):
        box("RustSill%.2f" % p[2], p, (0.008, 0.12, 0.35), "rust", bevel=0.002)
    box("RustBoot", (0.3, 0.885, -1.75), (0.5, 0.01, 0.3), "rust", bevel=0.0)


LIVERIES = [("bus_red", "bus_cream"), ("bus_green", "bus_cream"), ("bus_blue", "bus_silver"), ("bus_cream", "bus_maroon"), ("bus_orange", "bus_cream")]


def bus(n, off_z=0.0, flip=False):
    """An old single-decker in livery n, its length along x, its door on +z (on -z when flipped)."""
    main, band = LIVERIES[n % len(LIVERIES)]
    BL, BW, BH = 10.0, 2.5, 3.0
    hl, hw = BL * 0.5, BW * 0.5 - 0.01
    sz = -1.0 if flip else 1.0
    p = "Bus%d_" % n

    def bb(name, c, s, mat, bevel=0.0):
        box(p + name, (c[0], c[1], off_z + sz * c[2]), s, mat, bevel=bevel)

    bb("Upper", (0.0, 2.0, 0.0), (BL, 1.76, hw * 2.0), main, 0.1)
    bb("Roof", (0.0, BH - 0.06, 0.0), (BL - 0.12, 0.12, hw * 2.0 - 0.1), "bus_roof", 0.04)
    front, rear, arch = hl - 2.35, -hl + 2.7, 0.66
    for a, b in ((-hl, rear - arch), (rear + arch, front - arch), (front + arch, hl)):
        bb("Lower%.1f" % a, ((a + b) * 0.5, 0.78, 0.0), (b - a, 0.8, hw * 2.0), main, 0.03)
    for ax in (rear, front):
        bb("ArchTop%.1f" % ax, (ax, 1.1, 0.0), (arch * 2.0, 0.16, hw * 2.0), main)
        bb("Well%.1f" % ax, (ax, 0.75, 0.0), (arch * 2.0, 0.7, hw * 2.0 - 0.7), "plastic_black")
        for s in (-1, 1):
            z = off_z + sz * s * (hw - 0.17)
            cyl(p + "Tyre%.1f%d" % (ax, s), (ax, 0.5, z), 0.5, 0.3, "rubber", axis="z", verts=28, bevel=0.03)
            cyl(p + "Wheel%.1f%d" % (ax, s), (ax, 0.5, z + sz * s * 0.152), 0.3, 0.01, "steel", axis="z", verts=20)
            cyl(p + "Nut%.1f%d" % (ax, s), (ax, 0.5, z + sz * s * 0.165), 0.09, 0.03, "steel_dark", axis="z", verts=8)
            bb("ArchTrim%.1f%d" % (ax, s), (ax, 1.03, s * (hw + 0.004)), (arch * 2.0 + 0.06, 0.05, 0.01), "plastic_black")
            for e in (-1, 1):
                bb("ArchSide%.1f%d%d" % (ax, s, e), (ax + e * (arch + 0.015), 0.69, s * (hw + 0.004)), (0.05, 0.68, 0.01), "plastic_black")
    bb("Band", (0.0, 1.2, 0.0), (BL + 0.01, 0.16, hw * 2.0 + 0.008), band)
    bb("BandHigh", (0.0, 2.36, 0.0), (BL + 0.01, 0.1, hw * 2.0 + 0.008), band)
    bb("Rubbing", (0.0, 0.42, 0.0), (BL + 0.02, 0.08, hw * 2.0 + 0.02), "plastic_black")
    for e in (-1, 1):
        bb("Bumper%d" % e, (e * (hl + 0.04), 0.5, 0.0), (0.1, 0.24, hw * 2.0 + 0.02), "plastic_black", 0.02)
    wa, wb = -hl + 0.5, hl - 1.95
    for s in (-1, 1):
        bb("Windows%d" % s, ((wa + wb) * 0.5, 1.84, s * (hw + 0.003)), (wb - wa, 0.84, 0.008), "glass")
        for k in range(9):
            x = wa + (wb - wa) * k / 8
            bb("Pillar%d%d" % (s, k), (x, 1.84, s * (hw + 0.007)), (0.09, 0.88, 0.01), main)
        bb("Rain%d" % s, ((wa + wb) * 0.5, 2.27, s * (hw + 0.009)), (wb - wa, 0.02, 0.01), "plastic_black")
    bb("Door", (hl - 1.1, 1.34, hw + 0.003), (1.4, 1.76, 0.008), "glass")
    bb("DoorSplit", (hl - 1.1, 1.34, hw + 0.008), (0.035, 1.76, 0.01), "plastic_black")
    bb("DoorStep", (hl - 1.1, 0.47, hw + 0.008), (1.45, 0.05, 0.01), "plastic_black")
    bb("Driver", (hl - 1.1, 1.84, -hw - 0.003), (1.4, 0.84, 0.008), "glass")
    bb("Screen", (hl + 0.003, 1.78, 0.0), (0.008, 1.02, hw * 2.0 - 0.2), "glass")
    bb("ScreenSplit", (hl + 0.006, 1.78, 0.0), (0.01, 1.04, 0.07), main)
    bb("Blind", (hl + 0.004, 2.55, 0.0), (0.01, 0.26, 1.5), "plastic_black")
    bb("BlindLit", (hl + 0.008, 2.55, 0.0), (0.01, 0.13, 1.1), "lamp_lens")
    bb("RearWindow", (-hl - 0.003, 2.0, 0.0), (0.008, 0.62, 1.7), "glass")
    bb("Grille", (-hl - 0.004, 0.86, 0.0), (0.01, 0.46, 1.9), "plastic_black")
    for i in range(5):
        bb("Louvre%d" % i, (-hl - 0.008, 0.7 + i * 0.08, 0.0), (0.008, 0.02, 1.9), main)
    for s in (-1, 1):
        cyl(p + "Lamp%d" % s, (hl + 0.01, 0.8, off_z + sz * s * 0.9), 0.12, 0.03, "chrome", axis="x", verts=16)
        cyl(p + "Lens%d" % s, (hl + 0.026, 0.8, off_z + sz * s * 0.9), 0.095, 0.01, "lamp_lens", axis="x", verts=16)
        bb("Ind%d" % s, (hl + 0.01, 0.8, s * 0.65), (0.02, 0.08, 0.12), "amber_lens")
        bb("Tail%d" % s, (-hl - 0.01, 0.8, s * 0.95), (0.02, 0.22, 0.16), "red_lens")
        bb("TailInd%d" % s, (-hl - 0.01, 1.0, s * 0.95), (0.02, 0.1, 0.16), "amber_lens")
        bb("MirrorArm%d" % s, (hl + 0.12, 2.15, s * (hw - 0.05)), (0.24, 0.03, 0.03), "plastic_black")
        bb("Mirror%d" % s, (hl + 0.24, 1.95, s * (hw - 0.05)), (0.04, 0.32, 0.18), "plastic_black", 0.01)
    rng = random.Random(911 + n)
    for i in range(7):
        s = -1 if rng.random() < 0.5 else 1
        bb("Rust%d" % i, (rng.uniform(-hl + 0.5, hl - 0.5), rng.uniform(0.5, 1.0), s * (hw + 0.004)),
           (rng.uniform(0.15, 0.6), rng.uniform(0.06, 0.2), 0.008), "rust")


@model("bus", (10.0, 3.0, 2.5))
def bus_one():
    bus(0)


@model("bus_row", (10.0, 3.0, 12.5))
def bus_row():
    for i in range(5):
        bus(i, off_z=-5.0 + i * 2.5, flip=i % 2 == 1)


# --- Walls and fences ----------------------------------------------------------------------------------

JERSEY = [(-0.3, 0.0), (0.3, 0.0), (0.3, 0.08), (0.17, 0.26), (0.085, 0.76), (0.05, 0.8), (-0.05, 0.8), (-0.085, 0.76), (-0.17, 0.26), (-0.3, 0.08)]


@model("wall_concrete", (2.0, 0.8, 0.6))
def wall_concrete():
    prism("Barrier", JERSEY, 1.99, "concrete", axis="x", bevel=0.01)
    # A red and white reflective band on both faces, fork slots at the foot, lifting eyes on top.
    for s in (-1, 1):
        a, b = (s * 0.17, 0.26), (s * 0.085, 0.76)
        ang = math.degrees(math.atan2(a[0] - b[0], b[1] - a[1]))
        t = 0.62
        cy, cz = a[1] + (b[1] - a[1]) * t, a[0] + (b[0] - a[0]) * t
        for k in range(8):
            x = -0.875 + k * 0.25
            box("Band%d%d" % (s, k), (x, cy, cz + s * 0.004), (0.25, 0.1, 0.004), "reflect_red" if k % 2 == 0 else "reflect_white",
                bevel=0.0, rot=(-ang * s if s > 0 else ang, 0, 0))
    for x in (-0.55, 0.55):
        box("Slot%.2f" % x, (x, 0.04, 0.0), (0.22, 0.06, 0.61), "concrete_dark", bevel=0.004)
        torus("Eye%.2f" % x, (x, 0.82, 0.0), 0.04, 0.008, "steel_dark", axis="z", verts=12)


@model("wall_brick", (2.4, 1.2, 0.3))
def wall_brick():
    # A steel channel the wall stands on (so it can be moved), then stretcher bond in two skins.
    box("Channel", (0.0, 0.03, 0.0), (2.4, 0.06, 0.3), "paint_grey", bevel=0.005)
    for s in (-1, 1):
        box("Foot%d" % s, (s * 1.12, 0.008, 0.0), (0.16, 0.016, 0.5), "paint_grey", bevel=0.003)
    box("Mortar", (0.0, 0.63, 0.0), (2.38, 1.12, 0.23), "mortar", bevel=0.0)
    rng = random.Random(42)
    course_h, brick_l, brick_h, joint = 0.075, 0.215, 0.065, 0.01
    courses = int(1.08 / course_h)
    for c in range(courses):
        y = 0.07 + c * course_h + brick_h * 0.5
        off = (brick_l + joint) * 0.5 if c % 2 else 0.0
        x = -1.2 + off - (brick_l + joint) * 0.5
        while x < 1.2:
            x0, x1 = max(x - brick_l * 0.5, -1.195), min(x + brick_l * 0.5, 1.195)
            if x1 - x0 > 0.04:
                for sk in (-1, 1):
                    tone = "brick" if rng.random() > 0.25 else "brick_dark"
                    box("Brick%d_%.3f_%d" % (c, x, sk), ((x0 + x1) * 0.5, y, sk * 0.0725), (x1 - x0, brick_h, 0.1025 + rng.uniform(-0.003, 0.003)),
                        tone, bevel=0.004)
            x += brick_l + joint
    box("Coping", (0.0, 1.2 - 0.04, 0.0), (2.42, 0.08, 0.32), "concrete", bevel=0.01)


@model("fence_wood", (2.4, 1.1, 0.12))
def fence_wood():
    for i, x in enumerate((-1.15, 0.0, 1.15)):
        box("Post%d" % i, (x, 0.55, 0.0), (0.1, 1.1, 0.1), "fence_wood", bevel=0.006)
        prism("PostCap%d" % i, [(-0.05, 1.1), (0.05, 1.1), (0.0, 1.14)], 0.1, "fence_wood", axis="x", centre=x, bevel=0.002)
        box("PostFoot%d" % i, (x, 0.02, 0.0), (0.3, 0.04, 0.12), "fence_wood", bevel=0.004)
    for j, y in enumerate((0.3, 0.62, 0.94)):
        box("Rail%d" % j, (0.0, y, 0.06), (2.4, 0.1, 0.03), "fence_wood", bevel=0.004)
        for x in (-1.15, 0.0, 1.15):
            cyl("Nail%d%.2f" % (j, x), (x, y, 0.077), 0.006, 0.004, "steel_dark", axis="z", verts=6, bevel=0.0)


@model("fence_mesh", (3.0, 2.0, 0.15))
def fence_mesh():
    """Site fencing on concrete feet: a tube frame with welded mesh."""
    W, H = 3.0, 2.0
    y0 = 0.1
    frame = [(-W * 0.5 + 0.02, y0, 0.0), (-W * 0.5 + 0.02, H - 0.02, 0.0), (W * 0.5 - 0.02, H - 0.02, 0.0), (W * 0.5 - 0.02, y0, 0.0)]
    tube("Frame", frame + [frame[0]], 0.019, "galvanised", sides=10)
    nx, ny = 40, 10
    for i in range(1, nx):
        x = -W * 0.5 + 0.02 + (W - 0.04) * i / nx
        box("Wv%d" % i, (x, (y0 + H) * 0.5, 0.0), (0.004, H - y0 - 0.04, 0.004), "galvanised", bevel=0.0)
    for j in range(1, ny):
        y = y0 + (H - y0 - 0.02) * j / ny
        box("Wh%d" % j, (0.0, y, 0.004), (W - 0.04, 0.004, 0.004), "galvanised", bevel=0.0)
    for s in (-1, 1):
        box("Foot%d" % s, (s * (W * 0.5 - 0.02), 0.07, 0.0), (0.62, 0.14, 0.15), "rubber", bevel=0.02)
        cyl("Leg%d" % s, (s * (W * 0.5 - 0.02), 0.14, 0.0), 0.02, 0.12, "galvanised", axis="y", verts=10)
    for x in (-0.5, 0.5):
        box("Stay%.1f" % x, (x, 1.3, 0.0), (0.04, 0.03, 0.02), "galvanised", bevel=0.0)


# --- Cones, posts and flags ----------------------------------------------------------------------------

@model("cone", (0.36, 0.5, 0.36))
def cone():
    box("Base", (0.0, 0.015, 0.0), (0.36, 0.03, 0.36), "rubber", bevel=0.03)
    cyl("Cone", (0.0, 0.265, 0.0), 0.15, 0.47, "cone_orange", axis="y", verts=32, radius2=0.025, bevel=0.004)
    for y0, y1 in ((0.2, 0.28), (0.34, 0.4)):
        r0 = 0.15 + (0.025 - 0.15) * (y0 - 0.03) / 0.47 + 0.002
        r1 = 0.15 + (0.025 - 0.15) * (y1 - 0.03) / 0.47 + 0.002
        cyl("Band%.2f" % y0, (0.0, (y0 + y1) * 0.5, 0.0), r0, y1 - y0, "reflect_white", axis="y", verts=32, radius2=r1, bevel=0.0)


@model("post", (0.15, 1.0, 0.15))
def post():
    cyl("Foot", (0.0, 0.02, 0.0), 0.075, 0.04, "rubber", axis="y", verts=20, bevel=0.01)
    for k in range(5):
        cyl("Band%d" % k, (0.0, 0.04 + 0.19 * k + 0.095, 0.0), 0.045, 0.19, "paint_yellow" if k % 2 == 0 else "paint_black", axis="y", verts=20, bevel=0.0)
    cyl("Cap", (0.0, 1.0 - 0.01, 0.0), 0.047, 0.02, "paint_black", axis="y", verts=20, bevel=0.006)
    box("Reflector", (0.0, 0.85, 0.046), (0.05, 0.08, 0.004), "reflect_red", bevel=0.0)


def flag(pid, cloth):
    cyl("Base", (0.0, 0.03, 0.0), 0.22, 0.06, "rubber", axis="y", verts=24, bevel=0.015)
    cyl("Pole", (0.0, 1.1, 0.0), 0.018, 2.16, "aluminium", axis="y", verts=12, bevel=0.0)
    sphere("Knob", (0.0, 2.19, 0.0), 0.025, "plastic_black")
    # The cloth hangs off the pole towards +x with a lazy wave in it.
    W, H, top = 0.58, 0.42, 2.12
    cols, rows = 12, 6
    def at(i, j):
        x = 0.02 + W * i / cols
        y = top - H * j / rows - 0.03 * (i / cols) ** 2
        z = 0.04 * math.sin(x * 9.0) * (i / cols)
        return (x, y, z)
    if cloth == "chequered":
        for i in range(cols):
            for j in range(rows):
                q = [at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)]
                mat = "cloth_black" if (i // 2 + j // 2) % 2 == 0 else "cloth_white"
                mesh("Sq%d_%d" % (i, j), q + [(x, y, z - 0.003) for x, y, z in q], [[0, 1, 2, 3], [7, 6, 5, 4]], mat)
    else:
        verts = []
        for j in range(rows + 1):
            for i in range(cols + 1):
                verts.append(at(i, j))
        n = len(verts)
        verts += [(x, y, z - 0.003) for x, y, z in verts]
        faces = []
        for j in range(rows):
            for i in range(cols):
                a = j * (cols + 1) + i
                faces.append([a, a + 1, a + cols + 2, a + cols + 1])
                faces.append([n + a + cols + 1, n + a + cols + 2, n + a + 1, n + a])
        mesh("Cloth", verts, faces, cloth)


@model("flag_red", (0.6, 2.2, 0.1))
def flag_red():
    flag("flag_red", "cloth_red")


@model("flag_blue", (0.6, 2.2, 0.1))
def flag_blue():
    flag("flag_blue", "cloth_blue")


@model("flag_chequered", (0.6, 2.2, 0.1))
def flag_chequered():
    flag("flag_chequered", "chequered")


# --- The yard's own fixtures (world_yard.gd) -------------------------------------------------------------

@model("yard_hump", (3.4, 0.08, 1.4))
def yard_hump():
    """A rubber speed hump across the lane (its length along x): 8 cm high, rounded front to back."""
    W, L, H = 3.4, 1.4, 0.08
    secs = []
    for k in range(13):
        t = k / 12
        z = (0.5 - t) * L
        y = H * math.sin(math.pi * t)
        secs.append((z, y))
    loft("Hump", [[(-W * 0.5, 0.0, z), (-W * 0.5, y, z), (W * 0.5, y, z), (W * 0.5, 0.0, z)] for z, y in secs], "rubber_hump")
    for s in range(3):
        box("Stripe%d" % s, (-1.1 + s * 1.1, H + 0.002, 0.0), (0.5, 0.004, 0.3), "paint_yellow", bevel=0.0)


@model("yard_whoop", (4.0, 0.5, 3.2))
def yard_whoop():
    """An earth roller across the whoops line: smooth from the ground up and over, 50 cm high."""
    W, L, H = 4.0, 3.2, 0.5
    secs = []
    for k in range(25):
        t = k / 24
        z = (0.5 - t) * L
        y = H * (0.5 - 0.5 * math.cos(2 * math.pi * t))
        bank = 0.3 * (y / H)
        secs.append([(-W * 0.5, 0.0, z), (-W * 0.5 + bank, y, z), (W * 0.5 - bank, y, z), (W * 0.5, 0.0, z)])
    loft("Roller", secs, "dirt")


def digits(name, text, cx, cy, z, h, mat):
    """Seven-segment numbers (black on the board), centred at (cx, cy) on the plane at depth z."""
    SEG = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgedc", "7": "abc", "8": "abcdefg", "9": "abcdfg"}
    w, t = h * 0.55, h * 0.13
    gap = w * 0.35
    total = len(text) * w + (len(text) - 1) * gap
    for k, ch in enumerate(text):
        x0 = cx - total * 0.5 + k * (w + gap)
        segs = {"a": (x0 + w * 0.5, cy + h * 0.5 - t * 0.5, w, t), "g": (x0 + w * 0.5, cy, w, t), "d": (x0 + w * 0.5, cy - h * 0.5 + t * 0.5, w, t),
                "f": (x0 + t * 0.5, cy + h * 0.25, t, h * 0.5), "b": (x0 + w - t * 0.5, cy + h * 0.25, t, h * 0.5),
                "e": (x0 + t * 0.5, cy - h * 0.25, t, h * 0.5), "c": (x0 + w - t * 0.5, cy - h * 0.25, t, h * 0.5)}
        for s in SEG[ch]:
            x, y, sw, sh = segs[s]
            box("%s%d%s" % (name, k, s), (x, y, z), (sw, sh, 0.004), mat, bevel=0.0)


def board(n):
    """A braking-zone board: a white board on a post, the distance in black on both faces (it's
    read from the lane, side on: its faces are ±z)."""
    box("Post", (0.0, 0.35, 0.0), (0.06, 0.7, 0.06), "paint_white", bevel=0.004)
    box("Board", (0.0, 1.0, 0.0), (0.8, 0.6, 0.06), "paint_white", bevel=0.008)
    box("Rim", (0.0, 1.0, 0.0), (0.82, 0.62, 0.05), "paint_red", bevel=0.006)
    for s in (-1, 1):
        digits("Num%d_" % s, str(n), 0.0, 1.0, s * 0.032, 0.34, "paint_black")


for _n in (0, 10, 20, 30, 40):
    model("yard_board_%d" % _n, (0.8, 1.3, 0.06))(lambda n=_n: board(n))


@model("yard_bunting", (7.0, 5.0, 0.12))
def yard_bunting():
    """A 5 m mast at x 0 and the 7 m string of little flags from its top to the next mast's (at +x 7,
    that mast being the next piece): placed end to end, a run of bunting."""
    cyl("Mast", (0.0, 2.5, 0.0), 0.05, 5.0, "paint_white", axis="y", verts=12, radius2=0.035, bevel=0.0)
    cyl("MastFoot", (0.0, 0.04, 0.0), 0.16, 0.08, "concrete", axis="y", verts=16, bevel=0.01)
    L, top, sag = 7.0, 4.8, 7.0 * 0.06
    def hang(t):
        return (L * t, top - sag * 4.0 * t * (1 - t), 0.0)
    pts = [hang(i / 24) for i in range(25)]
    tube("String", pts, 0.004, "twine", sides=6)
    colours = ["cloth_red", "cloth_yellow", "cloth_blue", "cloth_white", "cloth_green", "cloth_orange"]
    count = int(L / 0.55)
    for i in range(count):
        t = (i + 0.5) / count
        x, y, _ = hang(t)
        a, b = hang(t - 0.02), hang(t + 0.02)
        mesh("Pennant%d" % i, [(a[0], a[1], 0.0), (b[0], b[1], 0.0), (x, y - 0.34, 0.0), (a[0], a[1], -0.002), (b[0], b[1], -0.002), (x, y - 0.34, -0.002)],
             [[0, 1, 2], [5, 4, 3]], colours[i % len(colours)])


# --- Running it --------------------------------------------------------------------------------------

def main():
    import bpy
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if "--list" in args:
        for pid, (_, size) in sorted(MODELS.items()):
            print("%-24s %.2f x %.2f x %.2f" % ((pid,) + tuple(size)))
        return
    spec = json.load(open(os.path.join(ROOT, "Data", "Props", "materials.json")))
    kit.set_materials(spec["materials"])
    os.makedirs(OUT, exist_ok=True)
    want = [a for a in args if not a.startswith("-")] or sorted(MODELS)
    report = {}
    for pid in want:
        fn, size = MODELS[pid]
        kit.reset()
        for m in list(bpy.data.materials):
            bpy.data.materials.remove(m)
        fn()
        obj = kit.finish("SM_" + pid)
        lo, hi = kit.bounds(obj)
        kit.export(os.path.join(OUT, pid + ".glb"))
        tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        report[pid] = {"bounds_min": [round(v, 4) for v in lo], "bounds_max": [round(v, 4) for v in hi], "tris": tris,
                       "materials": [s.material.name for s in obj.material_slots]}
        print("[yard] %-22s %6d tris  %s .. %s" % (pid, tris, tuple(round(v, 3) for v in lo), tuple(round(v, 3) for v in hi)))
    path = os.path.join(OUT, "_built.json")
    old = {}
    try:
        old = json.load(open(path))
    except Exception:
        pass
    old.update(report)
    with open(path, "w") as f:
        json.dump(dict(sorted(old.items())), f, indent=1)


main()
