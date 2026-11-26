"""Ride UK's own parked cars (beenster Nov 26 sent a sheet of well-known cars: "and this car image?"). Our own
made-up cars and made-up makers, modelled in Blender: the sheet gave only the RANGE of body types (hatchback,
estate, SUV, saloons, luxury saloon, sports coupe, supercar, convertible, a 1950s cruiser, a rally car, a pickup).
No car on it is copied: every shape, grille, lamp and badge here is our own, and no real maker's name, badge or
lettering appears (the number plates carry blocks, not letters).

The made-up makers (their names are only on the review sheet, never on the cars):
  Calder      everyday hatchbacks and estates; the rally hatch (blunt, friendly, slatted grille)
  Mardon      family SUVs and the double-cab pickup (upright, black cladding, square lamps)
  Aldmoor     executive saloons (long, low belt, slim lamp blades, wide low grille)
  Ashcombe    the big luxury saloon (tall upright bright grille of vertical bars, long bonnet)
  Harrow      the sports coupe and its convertible (long bonnet, fastback, twin round tail lamps)
  Talvane     the mid-engined supercar (cab forward, side intakes, rear wing)
  Brightwater the 1950s cruiser (tail fins, chrome bumpers, whitewall tyres)

Each body is lofted: stations along the car, each a cross-section from the belly round to the roof's middle, whose
shape comes from the design's side line (belt), roof line (windscreen, roof, rear window), plan rounding and wheel
arches; its faces are painted, glazed or trimmed by where they fall (side windows between the pillars, the
windscreen and rear window on the slopes). Lamps, grilles, plates and trims are thin patches lying on that skin.

Writes Data/RideUK/Props/SM_Prop_parked_car_<n>.glb (n from 100 up: the Fab cars keep 0..11) and a hand-made
distance version SM_Prop_parked_car_<n>_LOD1.glb (never reduced by the engine), same material and frame as the
street kit (Tools/blender_street_kit.py: M_RideStreetKit, front +x, foot at 0, y up in the file). Tools/
ue_build_rideuk.py lists them with the Fab cars as the parked_car kind, so the car parks, the driveways and the
ferries draw ours among the Fab ones (a prop's Variant picks the model).

    python Tools/blender_car_kit.py                # the models
    python Tools/blender_car_kit.py --render DIR   # and the review sheet's pictures in DIR
"""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_street_kit as K   # noqa: E402
from blender_street_kit import box, lathe, rod, sweep   # noqa: E402
from make_street_kit_textures import alpha_of   # noqa: E402

FIRST = 100   # the first own car's number (SM_Prop_parked_car_<n>; the Fab copies are 0..11)

# (linear colours)
SILVER = (0.42, 0.43, 0.44)
WHITE = (0.72, 0.72, 0.70)
BLACK_PAINT = (0.012, 0.012, 0.014)
GREY = (0.09, 0.095, 0.1)
BLUE = (0.015, 0.05, 0.19)
BLUE_BRIGHT = (0.03, 0.14, 0.5)
RED = (0.34, 0.012, 0.01)
GREEN = (0.02, 0.07, 0.035)
ORANGE = (0.62, 0.16, 0.02)
YELLOW = (0.62, 0.42, 0.03)
LIME = (0.16, 0.42, 0.03)
PINK = (0.62, 0.28, 0.33)
CREAM = (0.62, 0.55, 0.4)
BRONZE = (0.17, 0.11, 0.07)
OLIVE = (0.09, 0.1, 0.05)
MAROON = (0.1, 0.008, 0.012)

TRIM = (0.015, 0.015, 0.017)          # black plastic
UNDER = (0.02, 0.02, 0.021)
GLASS = (0.012, 0.016, 0.02)
CHROME = (0.62, 0.63, 0.64)
TYRE = (0.016, 0.016, 0.017)
WALL_WHITE = (0.7, 0.7, 0.68)
RIM_SILVER = (0.5, 0.51, 0.52)
RIM_DARK = (0.04, 0.042, 0.045)
RIM_GOLD = (0.4, 0.27, 0.06)
LAMP = (0.62, 0.62, 0.6)              # headlamp lens over its reflector
LAMP_DRL = (0.85, 0.85, 0.82)
TAIL = (0.42, 0.012, 0.008)
AMBER = (0.5, 0.18, 0.01)
PLATE_W = (0.8, 0.8, 0.78)
PLATE_Y = (0.75, 0.55, 0.03)
SEAT = (0.03, 0.025, 0.022)
TAN = (0.3, 0.16, 0.07)

# The ring's half points, belly middle (0) round to the roof's middle (8); a band k lies between points k and k+1.
NP = 9


def _smooth(keys):
    """A smooth curve through keys [(x, v)] (any order): cubic Hermite, Catmull-Rom slopes, flat past the ends."""
    keys = sorted(keys)
    xs = [k[0] for k in keys]
    vs = [k[1] for k in keys]

    def f(x):
        if x <= xs[0]:
            return vs[0]
        if x >= xs[-1]:
            return vs[-1]
        i = max(j for j in range(len(xs) - 1) if xs[j] <= x)
        x0, x1, v0, v1 = xs[i], xs[i + 1], vs[i], vs[i + 1]
        h = x1 - x0
        m0 = (vs[i + 1] - vs[i - 1]) / (xs[i + 1] - xs[i - 1]) if i > 0 else (v1 - v0) / h
        m1 = (vs[i + 2] - vs[i]) / (xs[i + 2] - xs[i]) if i + 2 < len(xs) else (v1 - v0) / h
        t = (x - x0) / h
        h00, h10, h01, h11 = 2 * t ** 3 - 3 * t ** 2 + 1, t ** 3 - 2 * t ** 2 + t, -2 * t ** 3 + 3 * t ** 2, t ** 3 - t ** 2
        return h00 * v0 + h10 * h * m0 + h01 * v1 + h11 * h * m1
    return f


class Car:
    """A design: its sizes and lines (metres; x along the car, nose +x, the middle of its length at 0)."""

    def __init__(self, **kw):
        d = dict(L=4.2, W=1.78, R=0.32, tw=0.205, WB=2.6, axle_mid=0.0, clear=0.17, crown=0.05, roof_crown=0.035,
                 tumble=0.21, rc_front=0.32, rc_rear=0.26, lift_front=0.12, lift_rear=0.1, shoulder=0.14,
                 open_top=False, roof_rgb=None, sill_rgb=None, belt_trim=TRIM, pillar_rgb=TRIM, c_pillar=0.25,
                 b_pillar=None, track_in=0.07, arch_gap=0.03, flare=0.0)
        d.update(kw)
        self.__dict__.update(d)
        self.belt = _smooth(self.belt_keys)
        self.ax_f = self.axle_mid + self.WB / 2
        self.ax_r = self.axle_mid - self.WB / 2

    # --- the lines ---
    def half(self, x):
        """The plan's half width at x: the corners rounded."""
        hw = self.W / 2
        df = self.L / 2 - x
        dr = x + self.L / 2
        if df < self.rc_front:
            r = self.rc_front
            hw -= r - math.sqrt(max(r * r - (r - df) ** 2, 0.0))
        if dr < self.rc_rear:
            r = self.rc_rear
            hw -= r - math.sqrt(max(r * r - (r - dr) ** 2, 0.0))
        # (the arches' flares: a little proud over each wheel)
        for xa in (self.ax_f, self.ax_r):
            u = (x - xa) / (self.R + 0.25)
            if abs(u) < 1.0:
                hw += self.flare * (0.5 + 0.5 * math.cos(math.pi * u))
        return hw

    def roof_at(self, x):
        """The roof's height at x (the belt where there's no cabin)."""
        b = self.belt(x)
        if self.open_top:
            return b
        ws0, ws1, rw1, rw0 = self.ws0, self.ws1, self.rw1, self.rw0
        zr = self.roof
        mid, halfspan = (ws1 + rw1) / 2, max((ws1 - rw1) / 2, 0.1)
        roof = lambda xx: zr - self.roof_drop * ((xx - mid) / halfspan) ** 2
        if x >= ws0 or x <= rw0:
            return b
        if x >= ws1:
            t = (x - ws1) / (ws0 - ws1)
            return roof(ws1) + (self.belt(ws0) - roof(ws1)) * t + 0.035 * math.sin(math.pi * t)
        if x >= rw1:
            return roof(x)
        t = (rw1 - x) / (rw1 - rw0)
        return roof(rw1) + (self.belt(rw0) - roof(rw1)) * t + self.rear_bulge * math.sin(math.pi * t)

    def cabin(self, x):
        """How much of a cabin stands at x, 0 (deck) to 1 (full)."""
        return min(max((self.roof_at(x) - self.belt(x)) / 0.16, 0.0), 1.0)

    def bottom(self, x):
        z = self.clear
        for xa in (self.ax_f, self.ax_r):
            ra = self.R + self.arch_gap
            dx = x - xa
            if abs(dx) < ra:
                z = max(z, self.R + math.sqrt(ra * ra - dx * dx))
        df, dr = self.L / 2 - x, x + self.L / 2
        if df < 0.4:
            z += self.lift_front * (1 - df / 0.4) ** 2
        if dr < 0.4:
            z += self.lift_rear * (1 - dr / 0.4) ** 2
        return z

    def ring(self, x):
        """The half ring at x: NP points (y, z), y >= 0."""
        hw = self.half(x)
        zb = self.belt(x)
        zs = zb - self.shoulder
        z0 = min(self.bottom(x), zs - 0.1)
        c = self.cabin(x)
        zt = self.roof_at(x)
        wb = hw - 0.035
        wt = max(min(hw - self.tumble, wb - 0.05), 0.15)
        lerp = lambda a, b: (a[0] + (b[0] - a[0]) * c, a[1] + (b[1] - a[1]) * c)
        cr = self.crown
        p = [(0.0, z0), (hw - 0.09, z0), (hw - 0.012, z0 + 0.075), (hw, zs), (wb, zb),
             lerp((wb * 0.86, zb + cr * 0.45), (wb - 0.012, zb + 0.03)),
             lerp((wb * 0.58, zb + cr * 0.82), (wt, zt - 0.055)),
             lerp((wb * 0.3, zb + cr * 0.96), (wt - 0.085, zt)),
             (0.0, (zb + cr) * (1 - c) + (zt + self.roof_crown) * c)]
        return p

    def at(self, x, s, side=1.0):
        """The skin's point at x, s along the half ring (0..8), on side +1 (left, +y) or -1."""
        p = self.ring(x)
        s = min(max(s, 0.0), NP - 1.0)
        i = min(int(s), NP - 2)
        t = s - i
        y = p[i][0] + (p[i + 1][0] - p[i][0]) * t
        z = p[i][1] + (p[i + 1][1] - p[i][1]) * t
        return Vector((x, y * side, z))


# --- building --------------------------------------------------------------------------------------------------

class Tagged:
    """A piece whose faces are of several finishes: face material index -> (surface, colour, dirt)."""

    def __init__(self, obj, table):
        self.obj, self.table = obj, table
        self.surface, self.rgb, self.dirt = table[0]


def _mesh(name, verts, faces, mats, sharp_deg=38.0):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(map(float, v)) for v in verts], [], faces)
    me.validate()
    me.update()
    for poly, m in zip(me.polygons, mats):
        poly.material_index = m
        poly.use_smooth = True
    me.set_sharp_from_angle(angle=math.radians(sharp_deg))
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def stations(car, lod):
    """The x of every cross-section: even steps, closer round the arches, and the cabin's breaks."""
    L = car.L
    step = 0.11 if lod == 0 else 0.3
    xs = list(np.linspace(-L / 2, L / 2, max(int(L / step), 8) + 1))
    for xa in (car.ax_f, car.ax_r):
        ra = car.R + car.arch_gap
        n = 12 if lod == 0 else 5
        xs += [xa + ra * math.sin(-math.pi / 2 + math.pi * k / n) for k in range(n + 1)]
        xs += [xa - ra - 0.01, xa + ra + 0.01]
    if not car.open_top:
        xs += [car.ws0, car.ws1, car.rw1, car.rw0]
        if lod == 0:
            xs += [car.ws0 - (car.ws0 - car.ws1) * k / 4 for k in (1, 2, 3)]
            xs += [car.rw1 - (car.rw1 - car.rw0) * k / 4 for k in (1, 2, 3)]
            xs += list(car.window_x()) + list(car.b_pillar_x())
    xs = sorted(set(round(x, 4) for x in xs if -L / 2 <= x <= L / 2))
    out = [xs[0]]
    for x in xs[1:]:
        if x - out[-1] > 0.012:
            out.append(x)
    if out[-1] != round(L / 2, 4):
        out[-1] = round(L / 2, 4)
    return out


def _window_x(self):
    """The side windows' run: from the A pillar's foot back to the C pillar."""
    return (self.rw1 + self.c_pillar, self.ws1 + 0.02)


def _b_pillar_x(self):
    if self.b_pillar is None:
        return ()
    return (self.b_pillar - 0.05, self.b_pillar + 0.05)


Car.window_x = _window_x
Car.b_pillar_x = _b_pillar_x


def body(M, car, paint, lod):
    """The lofted body: one piece, its faces painted, glazed or trimmed by where they fall."""
    roof_rgb = car.roof_rgb or paint
    table = [("enamel", paint, 0.05), ("lens", GLASS, 0.0), ("paint", car.belt_trim, 0.1), ("paint", UNDER, 0.6),
             ("enamel", roof_rgb, 0.05), ("paint", car.pillar_rgb, 0.1), ("paint", car.sill_rgb or paint, 0.25)]
    PAINT, GL, BELT, UND, ROOF, PIL, SILL = range(7)
    if car.sill_rgb:
        table[SILL] = ("paint", car.sill_rgb, 0.25)
    else:
        table[SILL] = ("enamel", paint, 0.05)
    if car.belt_trim == CHROME:
        table[BELT] = ("galv", CHROME, 0.0)
    xs = stations(car, lod)
    rings = [car.ring(x) for x in xs]
    verts, faces, mats = [], [], []
    full = 2 * NP - 2   # (points round the whole ring: 0..8 on the left, 7..1 mirrored on the right)
    for x, r in zip(xs, rings):
        for k in range(NP):
            verts.append((x, r[k][0], r[k][1]))
        for k in range(NP - 2, 0, -1):
            verts.append((x, -r[k][0], r[k][1]))
    wx0, wx1 = car.window_x() if not car.open_top else (0, 0)
    bp = car.b_pillar_x() if not car.open_top else ()

    def tag(band, xa, xb, ca, cb):
        xm = (xa + xb) / 2
        slope = abs(car.roof_at(xb) - car.roof_at(xa)) / max(xb - xa, 1e-4) > 0.35 and min(ca, cb) > 0.0 or (0 < ca < 1 or 0 < cb < 1)
        incab = ca > 0.97 and cb > 0.97
        if band == 0:
            return UND
        if band == 1:
            return SILL
        if band in (2, 3):
            return PAINT
        if band == 4:
            return BELT if incab and wx0 <= xm <= wx1 else PAINT
        if band == 5:
            if incab and wx0 <= xm <= wx1:
                if bp and bp[0] <= xm <= bp[1]:
                    return PIL
                return GL
            return ROOF if (incab or slope) and car.roof_rgb else PAINT
        if band == 6:
            return ROOF if (incab or slope) else PAINT
        # band 7: the windscreen and rear window on the slopes, else roof or deck
        if slope and not incab:
            return GL
        return ROOF if incab else PAINT

    for i in range(len(xs) - 1):
        a, b = i * full, (i + 1) * full
        ca, cb = car.cabin(xs[i]), car.cabin(xs[i + 1])
        for k in range(full):
            k1 = (k + 1) % full
            band = k if k < NP - 1 else (full - 1 - k)
            faces.append((a + k, b + k, b + k1, a + k1))
            mats.append(tag(band, xs[i], xs[i + 1], ca, cb))
    # (the ends: the nose's and tail's faces)
    n = len(xs) - 1
    faces.append(tuple(range(n * full, n * full + full)))
    mats.append(PAINT)
    faces.append(tuple(reversed(range(0, full))))
    mats.append(PAINT)
    ob = _mesh("body", verts, faces, mats)
    # (outward: the ring runs belly -> left side -> roof -> right side, so the quads as made face in or out
    # depending on direction; flip all if the signed volume says inside out)
    me = ob.data
    vol = 0.0
    co = [v.co for v in me.vertices]
    for poly in me.polygons:
        idx = list(poly.vertices)
        for j in range(1, len(idx) - 1):
            vol += co[idx[0]].dot(co[idx[j]].cross(co[idx[j + 1]]))
    if vol < 0:
        for poly in me.polygons:
            poly.flip()
        me.update()
    M.parts.append(Tagged(ob, table))
    return ob


def patch(M, car, name, x0, x1, s0, s1, surface, rgb, off=0.004, nx=4, ns=3, sides=(1, -1), dirt=0.05):
    """A thin skin lying on the body over x0..x1 and s0..s1 (lamps, trims, grilles), on each side asked."""
    for side in sides:
        verts, faces = [], []
        for i in range(nx + 1):
            x = x0 + (x1 - x0) * i / nx
            for j in range(ns + 1):
                s = s0 + (s1 - s0) * j / ns
                p = car.at(x, s, side)
                # (out along the skin's normal)
                dx = car.at(min(x + 0.01, car.L / 2), s, side) - car.at(max(x - 0.01, -car.L / 2), s, side)
                ds = car.at(x, min(s + 0.05, NP - 1.0), side) - car.at(x, max(s - 0.05, 0.0), side)
                nrm = dx.cross(ds)
                if side < 0:
                    nrm = -nrm
                if nrm.length < 1e-9:
                    nrm = Vector((0, side, 0))
                nrm.normalize()
                # (out, whichever way the ring ran: away from the body's middle line at mid height)
                out = p - Vector((x, 0.0, car.belt(x) * 0.6))
                if nrm.dot(out) < 0:
                    nrm = -nrm
                # (the ring goes belly -> roof: dx x ds points out on the left)
                verts.append(p + nrm * off)
        for i in range(nx):
            for j in range(ns):
                a = i * (ns + 1) + j
                b = (i + 1) * (ns + 1) + j
                f = (a, b, b + 1, a + 1) if side > 0 else (a, a + 1, b + 1, b)
                faces.append(f)
        ob = K._object(name, verts, faces, 50.0)
        # (_object turns closed pieces out by volume: an open patch is left as wound, outward)
        M.add(ob, surface, rgb, dirt)


def end_plate(M, car, name, front, y0, y1, z0, z1, surface, rgb, proud=0.006, both=True, dirt=0.05):
    """A flat plate on the nose's or tail's face (that end's cross-section), y0..y1 (left side, mirrored when both)."""
    x = car.L / 2 + proud if front else -car.L / 2 - proud
    for side in ((1, -1) if both else (1,)):
        ya, yb = sorted((y0 * side, y1 * side))
        if ya == yb:
            continue
        M.add(box(name, (x - (0.004 if front else -0.004), (ya + yb) / 2, (z0 + z1) / 2), (0.008, yb - ya, z1 - z0)), surface, rgb, dirt)
        if y0 == -y1:
            break


def plate(M, car, front):
    """A UK number plate: white at the front, yellow at the back, its characters dark blocks (not letters)."""
    x = car.L / 2 + 0.008 if front else -car.L / 2 - 0.008
    sgn = 1 if front else -1
    z = car.plate_z_front if front else car.plate_z_rear
    M.add(box("plate", (x, 0.0, z), (0.01, 0.52, 0.111)), "enamel", PLATE_W if front else PLATE_Y, 0.05)
    for k, (w, gap) in enumerate([(0.05, 0), (0.05, 0.06), (0.05, 0.06), (0.05, 0.06), (0.05, 0.1), (0.05, 0.06), (0.05, 0.06)]):
        pass
    y = -0.21
    for k in range(7):
        y += 0.06 if k else 0.0
        if k == 4:
            y += 0.03
        M.add(box("char", (x + sgn * 0.004, y, z), (0.004, 0.036, 0.07)), "paint", (0.01, 0.01, 0.01), 0.0)


def wheels(M, car, rim, lod, whitewall=False, spokes=5, deep=False):
    """The four wheels: tyres, rims and their spokes, sat in the arches."""
    seg = 24 if lod == 0 else 10
    R, w = car.R, car.tw
    rr = R * (0.66 if not whitewall else 0.55)
    yt = car.W / 2 - car.track_in - w / 2
    for xa in (car.ax_f, car.ax_r):
        for side in (1, -1):
            y = side * yt
            prof = [(0.0, -w / 2), (rr * 0.98, -w / 2), (R * 0.94, -w / 2), (R, -w / 2 + 0.035), (R, w / 2 - 0.035), (R * 0.94, w / 2), (rr * 0.98, w / 2), (0.0, w / 2)] \
                if lod == 0 else [(0.0, -w / 2), (R, -w / 2), (R, w / 2), (0.0, w / 2)]
            t = lathe("tyre", prof, seg)
            t.matrix_world = Matrix.Translation((xa, y, R)) @ Matrix.Rotation(math.pi / 2, 4, "X")
            M.add(t, "paint", TYRE, 0.5)
            face = y + side * (w / 2 + 0.002)
            if whitewall and lod == 0:
                ww = lathe("wall", [(rr * 1.02, 0.0), (R * 0.86, 0.0), (R * 0.86, 0.004), (rr * 1.02, 0.004)], seg)
                ww.matrix_world = Matrix.Translation((xa, face, R)) @ Matrix.Rotation(-side * math.pi / 2, 4, "X")
                M.add(ww, "enamel", WALL_WHITE, 0.2)
            dish = lathe("rim", [(0.0, 0.0), (rr, 0.0), (rr, 0.012), (rr * 0.25, 0.03 if deep else 0.02), (0.0, 0.03 if deep else 0.02)] if lod == 0
                         else [(0.0, 0.0), (rr, 0.0), (rr, 0.01), (0.0, 0.01)], seg)
            # (the rim's face sits a little in from the tyre's wall)
            inset = -0.014 if lod == 0 else -0.008
            dish.matrix_world = Matrix.Translation((xa, face + side * inset, R)) @ Matrix.Rotation(-side * math.pi / 2, 4, "X")
            if lod == 0:
                # (the dish dark, the spokes bright over it)
                M.add(dish, "galv" if rim != RIM_DARK else "paint", tuple(c * 0.45 for c in rim) if rim != RIM_DARK else (0.012, 0.012, 0.013), 0.4)
                for k in range(spokes):
                    a = 2 * math.pi * k / spokes + 0.3
                    sp = box("spoke", (0, 0, 0), (rr * 0.84, 0.07 if spokes <= 6 else 0.042, 0.03))
                    sp.matrix_world = (Matrix.Translation((xa, face + side * (inset + 0.02), R)) @ Matrix.Rotation(a, 4, "Y")
                                       @ Matrix.Translation((rr * 0.5, 0, 0)))
                    M.add(sp, "galv" if rim != RIM_DARK else "paint", rim, 0.3)
                ring = lathe("lip", [(rr * 0.92, 0.0), (rr * 1.0, 0.0), (rr * 1.0, 0.012), (rr * 0.92, 0.012)], seg)
                ring.matrix_world = Matrix.Translation((xa, face + side * (inset + 0.004), R)) @ Matrix.Rotation(-side * math.pi / 2, 4, "X")
                M.add(ring, "galv" if rim != RIM_DARK else "paint", rim, 0.3)
                hub = lathe("hub", [(0.0, 0.0), (rr * 0.2, 0.0), (rr * 0.18, 0.03), (0.0, 0.03)], 10)
                hub.matrix_world = Matrix.Translation((xa, face + side * (inset + 0.012), R)) @ Matrix.Rotation(-side * math.pi / 2, 4, "X")
                M.add(hub, "galv" if whitewall else "paint", CHROME if whitewall else RIM_DARK, 0.2)
            else:
                M.add(dish, "galv" if rim != RIM_DARK else "paint", rim, 0.3)
        # (the arch's liner behind the wheels, dark)
        # (its top kept under the bonnet and wings: a low nose would show it through)
        top = min(2 * car.R + car.arch_gap + 0.02, car.belt(xa) - car.shoulder - 0.04)
        M.add(box("liner", (xa, 0.0, (top + 0.12) / 2), (2 * (car.R + car.arch_gap) - 0.02, car.W - 0.14, top - 0.12)), "paint", UNDER, 0.6)


def mirrors(M, car, rgb):
    x = car.ws0 - 0.12
    for side in (1, -1):
        hw = car.half(x)
        z = car.belt(x) + 0.11
        M.add(box("mirror_arm", (x, side * (hw + 0.04), z - 0.03), (0.06, 0.1, 0.03)), "paint", TRIM, 0.1)
        M.add(box("mirror", (x - 0.02, side * (hw + 0.12), z), (0.08, 0.17, 0.11), bevel=0.02), "enamel", rgb, 0.05)


def handles(M, car, xs, rgb, surface="enamel"):
    for x in xs:
        patch(M, car, "handle", x - 0.07, x + 0.07, 3.55, 3.75, surface, rgb, off=0.012, nx=1, ns=1)


def exhaust(M, car, ys, r=0.04, z=None):
    z = car.clear + 0.06 if z is None else z
    for y in ys:
        M.add(rod("pipe", (-car.L / 2 - 0.04, y, z), (-car.L / 2 + 0.25, y, z), r, 10), "galv", CHROME, 0.4)


def lamps_round(M, car, front, y, z, r, rgb, surface="lens", depth=0.03):
    x = car.L / 2 if front else -car.L / 2
    sgn = 1 if front else -1
    for side in (1, -1):
        ob = lathe("lamp", [(0.0, 0.0), (r, 0.0), (r, depth * 0.6), (r * 0.6, depth), (0.0, depth * 1.1)], 14)
        ob.matrix_world = Matrix.Translation((x, side * y, z)) @ Matrix.Rotation(sgn * math.pi / 2, 4, "Y")
        M.add(ob, surface, rgb, 0.0)
        bez = lathe("bezel", [(r * 0.98, 0.0), (r * 1.15, 0.0), (r * 1.15, depth * 0.5), (r * 0.98, depth * 0.5)], 14)
        bez.matrix_world = Matrix.Translation((x, side * y, z)) @ Matrix.Rotation(sgn * math.pi / 2, 4, "Y")
        M.add(bez, "galv", CHROME, 0.1)


# --- the designs -----------------------------------------------------------------------------------------------

def calder_hatch(lod):
    """Calder Pico: a five-door supermini hatchback, the commonest car on a British drive."""
    c = Car(L=4.06, W=1.76, R=0.31, WB=2.56, axle_mid=0.08, roof=1.46, roof_drop=0.03,
            belt_keys=[(-2.03, 0.86), (-1.6, 0.93), (-0.6, 0.95), (0.6, 0.92), (1.3, 0.86), (1.82, 0.74), (2.03, 0.64)],
            ws0=1.02, ws1=0.16, rw1=-1.42, rw0=-1.98, rear_bulge=0.02, c_pillar=0.3, b_pillar=-0.6,
            plate_z_front=0.36, plate_z_rear=0.62, rc_rear=0.22, lift_rear=0.08)
    return c


def calder_estate(lod):
    """Calder Tourer: the family estate, a long roof to a near-upright tailgate."""
    return Car(L=4.62, W=1.8, R=0.32, WB=2.72, axle_mid=0.1, roof=1.48, roof_drop=0.025,
               belt_keys=[(-2.31, 0.88), (-1.8, 0.96), (-0.5, 0.97), (0.7, 0.93), (1.5, 0.85), (2.05, 0.74), (2.31, 0.64)],
               ws0=1.25, ws1=0.36, rw1=-2.02, rw0=-2.24, rear_bulge=0.0, c_pillar=0.5, b_pillar=-0.5,
               plate_z_front=0.36, plate_z_rear=0.6, rc_rear=0.2, lift_rear=0.08)


def aldmoor_saloon(lod):
    """Aldmoor S: an executive four-door saloon, long and low, a short boot deck."""
    return Car(L=4.86, W=1.86, R=0.34, WB=2.92, axle_mid=0.04, roof=1.44, roof_drop=0.04,
               belt_keys=[(-2.43, 0.82), (-2.2, 0.95), (-1.3, 0.98), (0.4, 0.95), (1.6, 0.88), (2.2, 0.78), (2.43, 0.64)],
               ws0=1.05, ws1=0.18, rw1=-1.0, rw0=-1.72, rear_bulge=0.04, c_pillar=0.26, b_pillar=-0.42,
               plate_z_front=0.34, plate_z_rear=0.76, rc_front=0.34, rc_rear=0.3, belt_trim=CHROME)


def ashcombe_luxury(lod):
    """Ashcombe Regent: the big luxury saloon, tall, a long bonnet to an upright bright grille."""
    return Car(L=5.18, W=1.96, R=0.37, WB=3.22, axle_mid=0.12, roof=1.58, roof_drop=0.03,
               belt_keys=[(-2.59, 0.92), (-2.3, 1.03), (-1.2, 1.05), (0.4, 1.04), (1.9, 1.02), (2.45, 0.98), (2.59, 0.9)],
               ws0=1.0, ws1=0.22, rw1=-1.08, rw0=-1.82, rear_bulge=0.03, c_pillar=0.32, b_pillar=-0.52,
               plate_z_front=0.38, plate_z_rear=0.8, rc_front=0.22, rc_rear=0.3, belt_trim=CHROME, crown=0.03,
               lift_front=0.06, shoulder=0.16)


def mardon_suv(lod):
    """Mardon Ridge: a family SUV, upright, black cladding round its arches and sills, roof rails."""
    return Car(L=4.62, W=1.88, R=0.37, WB=2.72, axle_mid=0.06, roof=1.68, roof_drop=0.02, clear=0.24,
               belt_keys=[(-2.31, 1.0), (-1.9, 1.08), (-0.5, 1.08), (0.7, 1.05), (1.5, 1.0), (2.05, 0.9), (2.31, 0.78)],
               ws0=1.18, ws1=0.38, rw1=-1.98, rw0=-2.22, rear_bulge=0.0, c_pillar=0.42, b_pillar=-0.46,
               plate_z_front=0.48, plate_z_rear=0.68, rc_front=0.3, rc_rear=0.2, sill_rgb=TRIM, flare=0.025,
               lift_front=0.14, lift_rear=0.1, shoulder=0.16)


def harrow_coupe(lod, open_top=False):
    """Harrow GT: a sports coupe, a long bonnet and a fastback; its Spyder is the same car open."""
    return Car(L=4.5, W=1.88, R=0.34, WB=2.62, axle_mid=-0.12, roof=1.3, roof_drop=0.04, clear=0.13,
               belt_keys=[(-2.25, 0.76), (-1.9, 0.86), (-0.8, 0.86), (0.2, 0.84), (1.3, 0.78), (1.95, 0.66), (2.25, 0.52)],
               ws0=0.62, ws1=-0.24, rw1=-0.72, rw0=-1.82, rear_bulge=0.03, c_pillar=0.22, b_pillar=None,
               plate_z_front=0.3, plate_z_rear=0.56, rc_front=0.36, rc_rear=0.36, crown=0.06, open_top=open_top,
               lift_front=0.09, tumble=0.25, shoulder=0.12)


def talvane_super(lod):
    """Talvane Vira: a mid-engined supercar, very low and wide, the cabin pushed forward."""
    return Car(L=4.52, W=1.98, R=0.35, WB=2.66, axle_mid=0.0, roof=1.16, roof_drop=0.035, clear=0.11,
               belt_keys=[(-2.26, 0.8), (-1.9, 0.86), (-0.9, 0.82), (0.1, 0.74), (1.2, 0.66), (1.9, 0.52), (2.26, 0.4)],
               ws0=0.78, ws1=0.0, rw1=-0.62, rw0=-1.9, rear_bulge=-0.02, c_pillar=0.12, b_pillar=None,
               plate_z_front=0.28, plate_z_rear=0.5, rc_front=0.4, rc_rear=0.3, crown=0.07, tumble=0.28,
               lift_front=0.06, lift_rear=0.08, shoulder=0.1, flare=0.03)


def brightwater_cruiser(lod):
    """Brightwater Starliner: a 1950s cruiser, long and wide, tail fins, chrome bumpers, whitewalls."""
    return Car(L=5.4, W=2.0, R=0.36, WB=3.1, axle_mid=0.05, roof=1.46, roof_drop=0.02, clear=0.2,
               belt_keys=[(-2.7, 0.9), (-2.3, 0.97), (-1.0, 0.9), (0.6, 0.9), (2.0, 0.92), (2.7, 0.86)],
               ws0=0.62, ws1=0.12, rw1=-0.95, rw0=-1.45, rear_bulge=0.0, c_pillar=0.12, b_pillar=None,
               plate_z_front=0.42, plate_z_rear=0.6, rc_front=0.3, rc_rear=0.12, belt_trim=CHROME, crown=0.03,
               tumble=0.12, lift_front=0.06, lift_rear=0.04, shoulder=0.12, open_top=True, arch_gap=0.07)


def mardon_pickup(lod):
    """Mardon Haulier: a double-cab pickup, its load bed under a flat cover."""
    return Car(L=5.3, W=1.9, R=0.39, WB=3.1, axle_mid=0.18, roof=1.8, roof_drop=0.01, clear=0.26,
               belt_keys=[(-2.65, 1.04), (-1.2, 1.08), (0.4, 1.1), (1.5, 1.06), (2.2, 1.0), (2.65, 0.88)],
               ws0=1.25, ws1=0.45, rw1=-1.0, rw0=-1.06, rear_bulge=0.0, c_pillar=0.12, b_pillar=-0.28,
               plate_z_front=0.5, plate_z_rear=0.62, rc_front=0.24, rc_rear=0.08, sill_rgb=TRIM, flare=0.03,
               lift_front=0.16, lift_rear=0.05, shoulder=0.18, crown=0.03, tumble=0.12)


def calder_rally(lod):
    """Calder Pico R: the hatch gone rallying: wider arches, a bonnet scoop, a wing, spot lamps, mud flaps."""
    return Car(L=4.12, W=1.84, R=0.32, WB=2.56, axle_mid=0.08, roof=1.42, roof_drop=0.03, clear=0.15,
               belt_keys=[(-2.06, 0.86), (-1.6, 0.93), (-0.6, 0.95), (0.6, 0.92), (1.3, 0.86), (1.85, 0.74), (2.06, 0.62)],
               ws0=1.02, ws1=0.12, rw1=-1.66, rw0=-1.96, rear_bulge=0.0, c_pillar=0.26, b_pillar=-0.62,
               plate_z_front=0.36, plate_z_rear=0.62, rc_rear=0.22, lift_rear=0.08, flare=0.045)


# --- dressing each design ------------------------------------------------------------------------------------

def dress_common(M, car, paint, lod, rim=RIM_SILVER, doors=(), spokes=5):
    body(M, car, paint, lod)
    wheels(M, car, rim, lod, spokes=spokes)
    plate(M, car, True)
    plate(M, car, False)
    if lod == 0:
        if not car.open_top:
            mirrors(M, car, paint)
        handles(M, car, doors, paint)
        # (the rear bumper's dark lower lip)
        hw = car.half(-car.L / 2)
        end_plate(M, car, "lip", False, -hw * 0.8, hw * 0.8, car.bottom(-car.L / 2) + 0.03, car.bottom(-car.L / 2) + 0.13, "paint", TRIM)


SMOKED = (0.03, 0.034, 0.04)


def front_lamps(M, car, s0, s1, depth, rgb=LAMP, drl=True):
    """Headlamps wrapping the nose's corners (patches on the skin): a smoked housing, a bright projector in it
    and a light blade along it, so they read on a white car and a black one alike."""
    x1 = car.L / 2 - 0.004
    patch(M, car, "headlamp", x1 - depth, x1, s0, s1, "lens", SMOKED, off=0.006)
    patch(M, car, "projector", x1 - depth * 0.45, x1 - 0.01, s0 + (s1 - s0) * 0.35, s0 + (s1 - s0) * 0.8, "lens", rgb, off=0.009, nx=2, ns=2)
    if drl:
        patch(M, car, "drl", x1 - depth * 0.95, x1 - 0.005, s0 + (s1 - s0) * 0.05, s0 + (s1 - s0) * 0.22, "lens", LAMP_DRL, off=0.011, nx=3, ns=1)


def rear_lamps(M, car, s0, s1, depth, rgb=TAIL):
    x0 = -car.L / 2 + 0.004
    patch(M, car, "taillamp", x0, x0 + depth, s0, s1, "lens", rgb, off=0.006)


def face(car, front):
    """(the end's ring: its widest and its shoulder/belt heights, to lay plates inside it)"""
    x = car.L / 2 if front else -car.L / 2
    r = car.ring(x)
    return r


def grille_slats(M, car, y, z0, z1, n, rgb=TRIM, frame=None, vertical=False, surface="paint"):
    """A grille on the nose's face: a dark plate, slats across it, a frame round it."""
    x = car.L / 2 + 0.004
    M.add(box("grille", (x, 0.0, (z0 + z1) / 2), (0.01, 2 * y, z1 - z0)), "paint", (0.008, 0.008, 0.009), 0.1)
    if vertical:
        for k in range(n):
            yy = -y + 2 * y * (k + 0.5) / n
            M.add(box("bar", (x + 0.012, yy, (z0 + z1) / 2), (0.02, 0.016, z1 - z0 - 0.02)), surface, rgb, 0.05)
    else:
        for k in range(n):
            zz = z0 + (z1 - z0) * (k + 0.5) / n
            M.add(box("slat", (x + 0.01, 0.0, zz), (0.016, 2 * y - 0.02, (z1 - z0) / n * 0.38)), surface, rgb, 0.05)
    if frame:
        fr = frame
        t = 0.025
        M.add(box("gf", (x + 0.012, 0.0, z1), (0.024, 2 * y + 2 * t, t)), "galv", fr, 0.05)
        M.add(box("gf", (x + 0.012, 0.0, z0), (0.024, 2 * y + 2 * t, t)), "galv", fr, 0.05)
        for s in (1, -1):
            M.add(box("gf", (x + 0.012, s * y, (z0 + z1) / 2), (0.024, t, z1 - z0 + t)), "galv", fr, 0.05)


def badge(M, car, front, z, rgb=CHROME, r=0.04):
    x = car.L / 2 + 0.014 if front else -car.L / 2 - 0.006
    sgn = 1 if front else -1
    ob = lathe("badge", [(0.0, 0.0), (r, 0.0), (r * 0.8, 0.008), (0.0, 0.01)], 10)
    ob.matrix_world = Matrix.Translation((x, 0.0, z)) @ Matrix.Rotation(sgn * math.pi / 2, 4, "Y") @ Matrix.Diagonal((1.0, 1.0 if front else 1.0, 1.0, 1.0)) @ Matrix.Scale(1.0, 4)
    ob.scale = (1.0, 0.7, 1.0)
    M.add(ob, "galv", rgb, 0.05)


def model_hatch(name, paint, lod, rim=RIM_SILVER):
    M = K.Model(name)
    car = calder_hatch(lod)
    dress_common(M, car, paint, lod, rim, doors=(0.0, -0.95))
    front_lamps(M, car, 3.15, 4.7, 0.2)
    rear_lamps(M, car, 3.6, 5.6, 0.07)
    if lod == 0:
        grille_slats(M, car, 0.36, 0.42, 0.56, 3)
        end_plate(M, car, "intake", True, -0.5, 0.5, 0.2, 0.3, "paint", TRIM)
        badge(M, car, True, 0.6)
        exhaust(M, car, (-0.5,), 0.03)
    return M


def model_rally(name, paint, lod):
    M = K.Model(name)
    car = calder_rally(lod)
    dress_common(M, car, paint, lod, RIM_GOLD, doors=(0.0, -0.95), spokes=8)
    front_lamps(M, car, 3.15, 4.7, 0.2)
    rear_lamps(M, car, 3.6, 5.6, 0.07)
    # (the livery: white stripes along the sides and over the roof, our own)
    patch(M, car, "stripe", -1.9, 1.9, 2.35, 2.75, "enamel", WHITE, off=0.003, nx=16, ns=1)
    patch(M, car, "stripe", -1.6, 2.0, 7.35, 7.75, "enamel", WHITE, off=0.003, nx=18, ns=1)
    if lod == 0:
        grille_slats(M, car, 0.36, 0.42, 0.56, 2)
        end_plate(M, car, "intake", True, -0.55, 0.55, 0.18, 0.32, "paint", TRIM)
        # (four spot lamps on a bar across the nose)
        bar_x = car.L / 2 + 0.02
        M.add(box("lampbar", (bar_x, 0.0, 0.52), (0.03, 1.2, 0.04)), "paint", TRIM, 0.2)
        for k, y in enumerate((-0.5, -0.17, 0.17, 0.5)):
            ob = lathe("spot", [(0.0, 0.0), (0.085, 0.0), (0.085, 0.05), (0.06, 0.07), (0.0, 0.075)], 14)
            ob.matrix_world = Matrix.Translation((bar_x + 0.01, y, 0.6)) @ Matrix.Rotation(math.pi / 2, 4, "Y")
            M.add(ob, "lens", LAMP if k % 3 == 0 else (0.75, 0.6, 0.15), 0.0)
        # (a bonnet scoop, a roof vent, a big wing on stalks, mud flaps)
        M.add(box("scoop", (0.7, 0.0, car.belt(0.7) + car.crown + 0.04), (0.42, 0.42, 0.08), bevel=0.02), "enamel", paint, 0.05)
        M.add(box("scoopmouth", (0.92, 0.0, car.belt(0.7) + car.crown + 0.04), (0.02, 0.36, 0.05)), "paint", TRIM, 0.1)
        zr = car.roof_at(-1.7) + 0.06
        for y in (-0.55, 0.55):
            M.add(box("stalk", (-1.86, y, zr - 0.02), (0.1, 0.03, 0.12)), "paint", TRIM, 0.1)
        M.add(box("wing", (-1.9, 0.0, zr + 0.06), (0.32, 1.5, 0.025)), "enamel", paint, 0.05)
        for y in (-0.73, 0.73):
            M.add(box("endplate", (-1.9, y, zr + 0.04), (0.36, 0.012, 0.14)), "paint", TRIM, 0.1)
        for xa in (car.ax_f, car.ax_r):
            for y in (-0.82, 0.82):
                M.add(box("flap", (xa - car.R - 0.06, y, 0.17), (0.01, 0.22, 0.28)), "paint", (0.5, 0.5, 0.52), 0.4)
        exhaust(M, car, (-0.45,), 0.05)
    return M


def model_estate(name, paint, lod, rim=RIM_SILVER):
    M = K.Model(name)
    car = calder_estate(lod)
    dress_common(M, car, paint, lod, rim, doors=(0.15, -0.85))
    front_lamps(M, car, 3.2, 4.8, 0.24)
    rear_lamps(M, car, 2.9, 5.65, 0.06)
    if lod == 0:
        grille_slats(M, car, 0.4, 0.4, 0.56, 4)
        end_plate(M, car, "intake", True, -0.56, 0.56, 0.2, 0.3, "paint", TRIM)
        badge(M, car, True, 0.6)
        # (roof rails)
        for y in (-0.58, 0.58):
            pts = [(x, y, car.roof_at(x) + 0.05) for x in np.linspace(car.rw1 + 0.05, car.ws1 - 0.05, 6)]
            M.add(sweep("rail", pts, 0.018, 6), "galv", (0.3, 0.3, 0.31), 0.2)
        exhaust(M, car, (-0.55,), 0.035)
    return M


def model_saloon(name, paint, lod, rim=RIM_SILVER):
    M = K.Model(name)
    car = aldmoor_saloon(lod)
    dress_common(M, car, paint, lod, rim, doors=(0.05, -0.9), spokes=10)
    front_lamps(M, car, 3.35, 4.6, 0.3)
    rear_lamps(M, car, 3.4, 5.4, 0.18)
    if lod == 0:
        # (a wide low grille in a bright frame, slim intakes)
        grille_slats(M, car, 0.42, 0.36, 0.56, 5, frame=CHROME)
        end_plate(M, car, "intake", True, 0.48, 0.66, 0.18, 0.3, "paint", TRIM)
        badge(M, car, True, 0.6)
        badge(M, car, False, 0.82)
        exhaust(M, car, (-0.55, 0.55), 0.04)
    return M


def model_luxury(name, paint, lod):
    M = K.Model(name)
    car = ashcombe_luxury(lod)
    dress_common(M, car, paint, lod, RIM_SILVER, doors=(0.1, -0.95), spokes=7)
    front_lamps(M, car, 3.3, 4.4, 0.22, drl=True)
    rear_lamps(M, car, 3.2, 4.7, 0.12)
    if lod == 0:
        # (the tall upright grille of bright bars, a bright bar along each sill, an ornament-free bonnet)
        grille_slats(M, car, 0.32, 0.48, 0.86, 11, rgb=CHROME, frame=CHROME, vertical=True, surface="galv")
        end_plate(M, car, "intake", True, 0.44, 0.7, 0.24, 0.32, "galv", CHROME)
        patch(M, car, "sillbar", -1.6, 1.6, 1.05, 1.25, "galv", CHROME, off=0.004, nx=12, ns=1)
        badge(M, car, False, 0.86)
        exhaust(M, car, (-0.6, 0.6), 0.04)
    else:
        M.add(box("grille", (car.L / 2 + 0.006, 0.0, 0.67), (0.012, 0.64, 0.38)), "galv", CHROME, 0.05)
    return M


def model_suv(name, paint, lod, rim=RIM_DARK, black_roof=False):
    M = K.Model(name)
    car = mardon_suv(lod)
    if black_roof:
        car.roof_rgb = BLACK_PAINT
    dress_common(M, car, paint, lod, rim, doors=(0.15, -0.8), spokes=6)
    front_lamps(M, car, 3.4, 4.5, 0.18, rgb=LAMP)
    rear_lamps(M, car, 3.1, 5.2, 0.06)
    # (black cladding over the arches)
    for xa in (car.ax_f, car.ax_r):
        patch(M, car, "cladding", xa - car.R - 0.12, xa + car.R + 0.12, 1.0, 2.15, "paint", TRIM, off=0.004, nx=8, ns=2)
    if lod == 0:
        grille_slats(M, car, 0.5, 0.52, 0.78, 3, rgb=(0.2, 0.2, 0.21))
        end_plate(M, car, "skid", True, -0.55, 0.55, 0.24, 0.36, "galv", (0.35, 0.35, 0.36))
        end_plate(M, car, "skidr", False, -0.5, 0.5, 0.26, 0.34, "galv", (0.35, 0.35, 0.36))
        badge(M, car, True, 0.66)
        for y in (-0.62, 0.62):
            pts = [(x, y, car.roof_at(x) + 0.06) for x in np.linspace(car.rw1 + 0.05, car.ws1 - 0.05, 6)]
            M.add(sweep("rail", pts, 0.02, 6, square=True), "paint", TRIM, 0.2)
        exhaust(M, car, (-0.6,), 0.04, z=0.3)
    return M


def model_pickup(name, paint, lod):
    M = K.Model(name)
    car = mardon_pickup(lod)
    dress_common(M, car, paint, lod, RIM_SILVER, doors=(0.2, -0.55), spokes=6)
    front_lamps(M, car, 3.45, 4.6, 0.2)
    rear_lamps(M, car, 3.0, 3.85, 0.05, rgb=TAIL)
    for xa in (car.ax_f, car.ax_r):
        patch(M, car, "cladding", xa - car.R - 0.14, xa + car.R + 0.14, 1.0, 2.3, "paint", TRIM, off=0.004, nx=8, ns=2)
    # (the load bed's flat cover, black, and its rails)
    patch(M, car, "cover", -2.6, -1.1, 4.9, 8.0, "paint", (0.02, 0.02, 0.022), off=0.004, nx=4, ns=3)
    if lod == 0:
        grille_slats(M, car, 0.58, 0.56, 0.86, 4, rgb=CHROME, frame=CHROME, surface="galv")
        end_plate(M, car, "bumperf", True, -0.92, 0.92, 0.36, 0.5, "galv", CHROME)
        end_plate(M, car, "bumperr", False, -0.92, 0.92, 0.4, 0.52, "galv", CHROME)
        badge(M, car, True, 0.71)
        # (the cab's back: a small window above the bed)
        x = car.rw0 - 0.004
        M.add(box("backlight", (car.rw1 - 0.02, 0.0, 1.5), (0.01, 0.9, 0.3)), "lens", GLASS, 0.0)
        M.add(box("towbar", (-car.L / 2 - 0.12, 0.0, 0.42), (0.2, 0.08, 0.08)), "paint", TRIM, 0.4)
        # (side steps)
        for s in (1, -1):
            M.add(box("step", (-0.25, s * (car.W / 2 - 0.02), 0.38), (1.5, 0.12, 0.04)), "paint", TRIM, 0.3)
    return M


def model_coupe(name, paint, lod, open_top=False):
    M = K.Model(name)
    car = harrow_coupe(lod, open_top)
    dress_common(M, car, paint, lod, RIM_DARK if not open_top else RIM_SILVER, doors=(-0.45,), spokes=10)
    front_lamps(M, car, 3.25, 4.65, 0.28)
    if lod == 0:
        # (twin round tail lamps each side, our own way)
        for y in (0.58, 0.76):
            lamps_round(M, car, False, y, 0.72, 0.07, TAIL, depth=0.02)
        grille_slats(M, car, 0.38, 0.2, 0.4, 4)
        badge(M, car, True, 0.47)
        exhaust(M, car, (-0.62, -0.5, 0.5, 0.62), 0.04)
        # (side vents behind the front wheels)
        patch(M, car, "vent", car.ax_f - car.R - 0.32, car.ax_f - car.R - 0.12, 2.5, 2.9, "paint", TRIM, off=0.004, nx=1, ns=1)
    else:
        rear_lamps(M, car, 3.4, 4.6, 0.04)
    if open_top:
        cockpit(M, car, lod, x0=-0.95, x1=0.55, rear_seats=False, seat_rgb=TAN)
    return M


def model_super(name, paint, lod):
    M = K.Model(name)
    car = talvane_super(lod)
    dress_common(M, car, paint, lod, RIM_DARK, doors=(-0.35,), spokes=10)
    front_lamps(M, car, 3.6, 4.85, 0.36, rgb=LAMP_DRL)
    rear_lamps(M, car, 3.7, 5.0, 0.05)
    # (side intakes ahead of the rear wheels, an engine cover of dark louvres)
    patch(M, car, "intake", car.ax_r + car.R + 0.12, car.ax_r + car.R + 0.62, 2.4, 3.6, "paint", (0.006, 0.006, 0.007), off=0.004, nx=3, ns=2)
    patch(M, car, "louvres", -1.85, -0.75, 6.2, 8.0, "paint", (0.01, 0.01, 0.011), off=0.004, nx=3, ns=2)
    if lod == 0:
        end_plate(M, car, "splitter", True, -0.8, 0.8, 0.12, 0.3, "paint", TRIM)
        end_plate(M, car, "diffuser", False, -0.85, 0.85, 0.12, 0.34, "paint", TRIM)
        zr = car.belt(-2.0) + 0.16
        for y in (-0.6, 0.6):
            M.add(box("stalk", (-2.05, y, zr - 0.08), (0.08, 0.025, 0.16)), "paint", TRIM, 0.1)
        M.add(box("wing", (-2.1, 0.0, zr), (0.3, 1.78, 0.025)), "paint", TRIM, 0.05)
        exhaust(M, car, (-0.1, 0.1), 0.05, z=0.42)
    return M


def model_cruiser(name, paint, lod, two_tone=WHITE):
    M = K.Model(name)
    car = brightwater_cruiser(lod)
    body(M, car, paint, lod)
    wheels(M, car, CHROME, lod, whitewall=True, spokes=8)
    plate(M, car, True)
    plate(M, car, False)
    # (tail fins over the rear wings, a chrome spear along each side, chrome bumpers)
    for side in (1, -1):
        y = side * (car.W / 2 - 0.1)
        # (a blade standing on the rear wing, rising to a point over the tail)
        prof = [(-2.74, car.belt(-2.7) - 0.02), (-2.74, car.belt(-2.7) + 0.24), (-2.45, car.belt(-2.45) + 0.2), (-1.1, car.belt(-1.1) + 0.0)]
        t = 0.035
        v = [(x, y - t, z) for x, z in prof] + [(x, y + t, z) for x, z in prof]
        f = [(0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
        M.add(K._object("fin", v, f, 30.0), "enamel", paint, 0.05)
        if lod == 0:
            ob = lathe("finlamp", [(0.0, 0.0), (0.05, 0.0), (0.05, 0.03), (0.0, 0.05)], 12)
            ob.matrix_world = Matrix.Translation((-2.73, y, car.belt(-2.7) + 0.2)) @ Matrix.Rotation(-math.pi / 2, 4, "Y")
            M.add(ob, "lens", TAIL, 0.0)
    patch(M, car, "spear", -2.4, 2.2, 2.75, 2.95, "galv", CHROME, off=0.004, nx=14, ns=1)
    patch(M, car, "flash", -2.4, -0.7, 2.25, 2.75, "enamel", two_tone, off=0.003, nx=6, ns=1)
    for front in (True, False):
        x = car.L / 2 + 0.06 if front else -car.L / 2 - 0.06
        sgn = 1 if front else -1
        pts = [(x - sgn * 0.3, car.W / 2 - 0.02, 0.4), (x - sgn * 0.02, car.W / 2 - 0.2, 0.4), (x, 0.0, 0.4), (x - sgn * 0.02, -car.W / 2 + 0.2, 0.4), (x - sgn * 0.3, -car.W / 2 + 0.02, 0.4)]
        M.add(sweep("bumper", pts, 0.07, 8), "galv", CHROME, 0.1)
    if lod == 0:
        lamps_round(M, car, True, 0.72, 0.72, 0.09, LAMP)
        lamps_round(M, car, True, 0.52, 0.72, 0.07, LAMP)
        grille_slats(M, car, 0.48, 0.5, 0.66, 4, rgb=CHROME, frame=CHROME, surface="galv")
        # (a wraparound windscreen in its frame, and the open cabin)
        cockpit(M, car, lod, x0=-1.35, x1=0.45, rear_seats=True, seat_rgb=two_tone, screen_h=0.36)
    else:
        M.add(box("lamps", (car.L / 2 + 0.006, 0.0, 0.72), (0.012, 1.6, 0.16)), "lens", LAMP, 0.0)
        cockpit(M, car, lod, x0=-1.35, x1=0.45, rear_seats=True, seat_rgb=two_tone, screen_h=0.36)
    return M


def cockpit(M, car, lod, x0, x1, rear_seats, seat_rgb, screen_h=0.3):
    """An open car's cabin: a dark tub, seats, a windscreen in its frame."""
    patch(M, car, "tub", x0, x1, 6.4, 8.0, "paint", (0.012, 0.011, 0.01), off=0.003, nx=4, ns=2)
    zb = car.belt(x1)
    y_in = car.half(x1) - 0.12
    # (the windscreen: a raked pane in a bright frame)
    rake = math.radians(32)
    ws = box("screen", (0, 0, 0), (0.008, 2 * y_in, screen_h))
    ws.matrix_world = Matrix.Translation((x1 + 0.08, 0.0, zb + screen_h / 2 * math.cos(rake) + 0.02)) @ Matrix.Rotation(-rake, 4, "Y")
    M.add(ws, "lens", GLASS, 0.0)
    if lod == 0:
        fr = box("screenframe", (0, 0, 0), (0.03, 2 * y_in + 0.04, 0.03))
        fr.matrix_world = Matrix.Translation((x1 + 0.08 - math.sin(rake) * screen_h / 2, 0.0, zb + screen_h * math.cos(rake) + 0.02)) @ Matrix.Rotation(-rake, 4, "Y")
        M.add(fr, "galv", CHROME, 0.05)
    rows = [x1 - 0.75] + ([x1 - 1.55] if rear_seats else [])
    for xs in rows:
        for y in ((-0.38, 0.38) if not rear_seats or xs == rows[0] else (-0.38, 0.38)):
            M.add(box("seatback", (xs - 0.12, y, zb - 0.02), (0.12, 0.48, 0.4), rot=Matrix.Rotation(-0.25, 4, "Y"), bevel=0.03 if lod == 0 else 0.0), "paint", seat_rgb, 0.1)
            if lod == 0 and not rear_seats:
                M.add(box("headrest", (xs - 0.17, y, zb + 0.24), (0.09, 0.24, 0.15), bevel=0.03), "paint", seat_rgb, 0.1)
        if lod == 0:
            M.add(box("wheelrim", (x1 - 0.38, 0.38, zb + 0.08), (0.04, 0.36, 0.36)), "paint", (0.01, 0.01, 0.01), 0.1)


# --- the line-up ---------------------------------------------------------------------------------------------
# Weighted like a British street: hatches, estates, saloons and SUVs in their everyday colours several times over,
# one each of the rarer cars.

LINEUP = [
    ("Calder Pico, silver", lambda lod: model_hatch, SILVER, {}),
    ("Calder Pico, red", lambda lod: model_hatch, RED, {}),
    ("Calder Pico, white", lambda lod: model_hatch, WHITE, {"rim": RIM_DARK}),
    ("Calder Tourer, dark grey", lambda lod: model_estate, GREY, {}),
    ("Calder Tourer, blue", lambda lod: model_estate, BLUE, {}),
    ("Aldmoor S, black", lambda lod: model_saloon, BLACK_PAINT, {}),
    ("Aldmoor S, green", lambda lod: model_saloon, OLIVE, {"rim": RIM_DARK}),
    ("Mardon Ridge, white", lambda lod: model_suv, WHITE, {"black_roof": True}),
    ("Mardon Ridge, bright blue", lambda lod: model_suv, BLUE_BRIGHT, {}),
    ("Mardon Ridge, bronze", lambda lod: model_suv, BRONZE, {"rim": RIM_SILVER}),
    ("Ashcombe Regent, two greys", lambda lod: model_luxury, (0.16, 0.15, 0.14), {}),
    ("Harrow GT, yellow", lambda lod: model_coupe, YELLOW, {}),
    ("Harrow Spyder, white", lambda lod: model_coupe, WHITE, {"open_top": True}),
    ("Talvane Vira, lime", lambda lod: model_super, LIME, {}),
    ("Brightwater Starliner, pink", lambda lod: model_cruiser, PINK, {}),
    ("Calder Pico R, orange", lambda lod: model_rally, ORANGE, {}),
    ("Mardon Haulier, grey", lambda lod: model_pickup, (0.2, 0.2, 0.21), {}),
]


def build(lod):
    s = "" if lod == 0 else "_LOD1"
    out = []
    for n, (title, make, paint, kw) in enumerate(LINEUP):
        M = make(lod)("SM_Prop_parked_car_%d%s" % (FIRST + n, s), paint, lod, **kw)
        M.title = title
        out.append(M)
    return out


# --- out: a Tagged piece's faces each take their own finish ---------------------------------------------------

def triangles(M):
    """As blender_street_kit.triangles, but a Tagged piece's faces each take their table row's finish."""
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
        table = getattr(part, "table", None)
        for lt in me.loop_triangles:
            vi = list(lt.vertices)
            li = list(lt.loops)
            p = co[vi]
            fn = np.cross(p[1] - p[0], p[2] - p[0])
            ax = int(np.argmax(np.abs(fn)))
            uv = p[:, [0, 1]] if ax == 2 else (p[:, [1, 2]] if ax == 0 else p[:, [0, 2]])
            P.append(p); N.append(cn[li]); U.append(uv)
            surface, rgb, d0 = table[min(lt.material_index, len(table) - 1)] if table else (part.surface, part.rgb, part.dirt)
            dirt = np.clip(d0 + 0.3 * np.clip(1.0 - (p[:, 2] + 0.1) / 0.6, 0, 1), 0, 1)
            rgba = np.zeros((3, 4))
            rgba[:, :3] = rgb
            rgba[:, 3] = [alpha_of(surface, d) for d in dirt]
            C.append(rgba)
        ev.to_mesh_clear()
    P = np.concatenate(P); N = np.concatenate(N); C = np.concatenate(C); U = np.concatenate(U)
    to_frame = lambda a: np.stack([a[:, 0], a[:, 2], -a[:, 1]], 1)
    col = np.round(np.clip(C, 0, 1) * 255.0)
    return to_frame(P), to_frame(N), col.astype(np.uint8), U


def write(M):
    pos, nrm, col, uv = triangles(M)
    tris = np.arange(len(pos)).reshape(-1, 3)
    path = os.path.join(K.OUT, M.name + ".glb")
    K.write_glb(path, pos, tris, nrm, col, M.name, uv)
    return len(tris), pos[:, 1].min(), pos[:, 1].max(), pos[:, 0].min(), pos[:, 0].max(), pos[:, 2].min(), pos[:, 2].max()


def main():
    args = sys.argv[1:]
    for lod in (0, 1):
        K.clear()
        models = build(lod)
        for M in models:
            nt, y0, y1, x0, x1, z0, z1 = write(M)
            print("%-30s %-30s %5d triangles, %.2f m high, %.2f x %.2f m" % (M.name, M.title, nt, y1 - y0, x1 - x0, z1 - z0))
        if "--render" in args:
            render(models, args[args.index("--render") + 1], lod)
    write_row()


def write_row():
    """The parked_car row in prop_kit.json: our cars' hand-made distance versions' screen size."""
    import json
    lines = open(K.KIT).read().split("\n")
    for i, ln in enumerate(lines):
        if ln.startswith('  "parked_car": '):
            if '"hand_lods"' not in ln:
                lines[i] = ln.replace('"variants": 12,', '"variants": 12, "hand_lods": [0.14],')
            break
    open(K.KIT, "w").write("\n".join(lines))
    json.load(open(K.KIT))


def render(models, out_dir, lod):
    import blender_car_kit_render as CR
    CR.render_cars(models, out_dir, lod)


if __name__ == "__main__":
    main()
