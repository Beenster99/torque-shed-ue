"""The Dales round the shed's riding pad (beenster, Nov 10: "please create a more appealing local scene
around the test area - maybe something from Dales or Scotland"). Until now the pad sat in a flat field
that ran to the horizon, with five green domes far off.

Now the shed stands at the head of a Yorkshire dale, the pad laid along its flat floor:

  - fells either side rising 400 to 500 m, stepped by limestone scars (the Yoredale beds), cut by
    ghylls, closing behind the shed at the dale head and far down the dale, with further ranges hazed
    blue on the horizon;
  - in-bye fields on the valley floor and the lower slopes, walled stone by stone (the Buttertubs
    ride's drystone walls, Tools/buttertubs_stone.py), each field its own green; the intake wall
    above them and fell walls running on up to the tops; moor above (rough grass, heather, bracken
    gone russet for November);
  - the pad's and the yard's own walls redrawn as real drystone walls (their colliders stay as they are,
    Tools/shed_yard.py), a gate where the lane leaves the pad;
  - field barns, a farm with sycamores round it, Swaledale ewes and lambs, a beck down the valley
    floor east of the pad with alders and ash along it, a dark spruce plantation on the west side,
    hawthorns on the walls, and the lane on down the dale between walls.

    python Tools/make_shed_dales.py            (about a minute; numpy only)

Writes Data/Shed/Dales/ (all deterministic):

    land_<zone>_<i>_<j>.glb   the land in tiles: 4 m near the pad, 20 m to about 7 km, 80 m to 19 km,
                              cover colour as vertex colour (the Buttertubs M_Land reads it), skirts
                              hiding the cracks where the spacing changes
    beck.glb, lane.glb        the beck's water and the lane's tarmac as ribbons
    Props/*.glb, props.json   walls, gates, scars, rocks, barns, sheep (as Data/Buttertubs/Props)
    trees.json                tree instances of the Buttertubs ride's tree models (/Game/Buttertubs/Trees)
    dales.json                what was made, for the log

Tools/ue_build_shed.py outside() puts it all in the shed map (only if this folder is there; without it
the old flat field and domes come back). Nothing here has collision: the bikes stay on the walled pad.

Frames: metres, x east, y up, z south, the shed frame of Tools/shed_yard.py (a point (x, y, z) here is
Unreal (100 x, 100 z, 100 y) cm, x east, y south out of the roller door, z up).
"""
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import shed_yard as SY            # noqa: E402
import buttertubs_stone as ST     # noqa: E402
import buttertubs_detail as BD    # noqa: E402
from export_buttertubs import write_glb   # noqa: E402

OUT = os.path.join(ROOT, "Data", "Shed", "Dales")

# The pad and the yard (metres): the land is flat and under them.
PAD = (SY.PAD_X0 / 100.0, SY.PAD_X1 / 100.0, SY.PAD_Y0 / 100.0, SY.PAD_Y1 / 100.0)   # x0, x1, z0, z1
YARD = (-17.2, 19.2, -15.8, PAD[2])            # the back yard with its walls
PAD_WALL = 0.3                                  # half the walls' thickness (60 cm boxes)
TEST_AREA = (150.0, 80.0)                       # middle of the test area (Tools/yard_layout.py)

# Zones of land: (name, spacing m, tile size m, x0, x1, z0, z1). Each coarser zone leaves out the
# tiles the finer one covers; tile edges line up.
NEAR = ("n", 4.0, 480.0, -960.0, 960.0, -960.0, 1920.0)
MID = ("m", 20.0, 960.0, -7680.0, 7680.0, -3840.0, 7680.0)
FAR = ("f", 60.0, 3840.0, -19200.0, 19200.0, -19200.0, 23040.0)
ZONES = (NEAR, MID, FAR)

# Where things stop: walls and barns within this box (so the wall count stays sensible).
WALLED_Z = (-1000.0, 2700.0)


def lin(rgb):
    """sRGB 0..255 to linear 0..255 (glTF COLOR_0 is linear)."""
    return np.power(np.asarray(rgb, np.float64) / 255.0, 2.2) * 255.0


# Cover colours (sRGB). M_Land tints its scans by colour / grass, so these are nudges, not paint.
GRASS = (92, 112, 52)
MEADOW = (106, 120, 56)      # hay meadow, cut and grown back
PASTURE = (80, 104, 46)      # grazed hard, deeper green
ROUGH = (96, 102, 54)        # rushy, poorer field
FIELDS = (GRASS, MEADOW, PASTURE, ROUGH, GRASS, PASTURE)
MOOR = (122, 116, 76)        # bleached moor grass
HEATHER = (92, 80, 60)       # heather out of flower
BRACKEN = (116, 90, 56)      # dead bracken in November
WET = (86, 94, 56)           # rush and flush by the becks
ROCK = (128, 126, 120)
FOREST = (46, 66, 32)
VERGE = (110, 112, 60)


# --- Noise -------------------------------------------------------------------------------------------

def _hash(ix, iz, seed):
    h = (ix.astype(np.int64) * 374761393 + iz.astype(np.int64) * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    h = h ^ (h >> 16)
    return h.astype(np.float64) / 4294967295.0 * 2.0 - 1.0


def vnoise(x, z, seed=0):
    """Smooth value noise in -1..1, one cell per unit."""
    x = np.asarray(x, np.float64); z = np.asarray(z, np.float64)
    ix = np.floor(x); iz = np.floor(z)
    fx = x - ix; fz = z - iz
    ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10); uz = fz * fz * fz * (fz * (fz * 6 - 15) + 10)
    a = _hash(ix, iz, seed); b = _hash(ix + 1, iz, seed)
    c = _hash(ix, iz + 1, seed); d = _hash(ix + 1, iz + 1, seed)
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uz


def fbm(x, z, octaves=5, seed=0, gain=0.5):
    out = 0.0; amp = 1.0; f = 1.0; tot = 0.0
    for o in range(octaves):
        out = out + amp * vnoise(x * f + o * 17.3, z * f - o * 9.1, seed + o * 101)
        tot += amp; amp *= gain; f *= 2.03
    return out / tot


def ridged(x, z, octaves=4, seed=0):
    out = 0.0; amp = 1.0; f = 1.0; tot = 0.0
    for o in range(octaves):
        n = 1.0 - np.abs(vnoise(x * f + o * 31.7, z * f + o * 5.3, seed + o * 57))
        out = out + amp * n * n; tot += amp; amp *= 0.5; f *= 2.1
    return out / tot


def smooth(e0, e1, v):
    t = np.clip((np.asarray(v, np.float64) - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# --- The shape of the dale ------------------------------------------------------------------------------

def rect_dist(x, z, r, grow=0.0):
    x0, x1, z0, z1 = r
    dx = np.maximum(np.maximum(x0 - grow - x, x - x1 - grow), 0.0)
    dz = np.maximum(np.maximum(z0 - grow - z, z - z1 - grow), 0.0)
    return np.hypot(dx, dz)


def flat_dist(x, z):
    """Metres from the pad or the yard (0 on them)."""
    return np.minimum(rect_dist(x, z, PAD), rect_dist(x, z, YARD))


def profile(c, H, c0, W):
    """A fellside: flat to c0, then rising as half a cosine to H over W, then a gentle moor top."""
    t = np.clip((c - c0) / W, 0.0, 1.0)
    return H * (0.5 - 0.5 * np.cos(np.pi * t)) + 0.012 * np.clip(c - c0 - W, 0.0, 4000.0)


def fells(x, z):
    """The fells either side, the dale head behind the shed, the fell closing the dale far south."""
    e_h = 510.0 + 60.0 * vnoise(z / 2500.0, 3.1, 11); e_c0 = 95.0 + 40.0 * vnoise(z / 1800.0, 7.7, 12)
    w_h = 390.0 + 60.0 * vnoise(z / 2500.0, 1.9, 13); w_c0 = 75.0 + 40.0 * vnoise(z / 1800.0, 4.2, 14)
    e = profile(x - PAD[1], e_h, e_c0, 2050.0)
    w = profile(PAD[0] - x, w_h, w_c0, 1900.0)
    n = profile(YARD[2] - z, 430.0 + 40.0 * vnoise(x / 2000.0, 8.8, 15), 140.0, 2000.0)
    s = profile(z - 4300.0, 400.0, 0.0, 2400.0)
    p = 4.0
    F = (e ** p + w ** p + n ** p + s ** p) ** (1.0 / p)
    # A few tops standing above the rest, for the skyline: a pike over the dale head, a long edge east.
    for hx, hz, r, h in ((1500.0, -1400.0, 900.0, 140.0), (-1700.0, -900.0, 1100.0, 90.0),
                         (2600.0, 1600.0, 1300.0, 110.0), (-2300.0, 3200.0, 1000.0, 120.0), (900.0, 6200.0, 1400.0, 160.0)):
        F = F + h * np.exp(-((x - hx) ** 2 + (z - hz) ** 2) / (r * r))
    return F


TERRACE = 38.0


def terrace(F, x, z):
    """Yoredale steps: benches of gentler ground between short steep risers (where the scars are)."""
    off = 9.0 * fbm(x / 900.0, z / 900.0, 3, 21)
    q = (F + off) / TERRACE
    fl = np.floor(q); fr = q - fl
    stepped = TERRACE * (fl + 0.45 * fr + 0.55 * smooth(0.72, 1.0, fr)) - off
    band = smooth(70.0, 130.0, F) * (1.0 - smooth(380.0, 470.0, F))
    # The beds crop out in stretches, not as rings all round the dale.
    band = band * smooth(-0.15, 0.35, fbm(x / 1300.0, z / 1300.0, 3, 23) + 0.25 * vnoise(x / 260.0, z / 260.0, 24))
    return F + (stepped - F) * band * 0.9


# Ghylls cutting down the fellsides: (side, z where they reach the valley floor, depth m).
GHYLLS = (("e", 330.0, 16.0), ("e", 1180.0, 22.0), ("e", 2350.0, 14.0), ("w", 620.0, 15.0), ("w", 1900.0, 18.0),
          ("e", -620.0, 12.0), ("w", 3300.0, 14.0))


def ghylls(x, z):
    cut = np.zeros_like(x)
    for side, z0, depth in GHYLLS:
        c = (x - PAD[1]) if side == "e" else (PAD[0] - x)
        zg = z0 + 45.0 * np.sin(c / 230.0) + 0.08 * c * (1 if side == "e" else -1)
        w = 22.0 + 0.03 * np.clip(c, 0, 3000)
        d = depth * smooth(140.0, 900.0, c) * (1.0 - smooth(2300.0, 2900.0, c))
        cut = np.maximum(cut, d * np.exp(-((z - zg) / w) ** 2))
    return cut


def beck_x(z):
    """The beck's line down the valley floor, east of the pad."""
    return PAD[1] + 100.0 + 22.0 * np.sin(z / 160.0) + 10.0 * np.sin(z / 57.0 + 1.3)


BECK_Z = (-1500.0, 4100.0)
LANE_Z0 = PAD[3] + 0.6             # the lane leaves the pad at its south wall, x 0
LANE_Z1 = 9000.0


def lane_x(z):
    t = smooth(LANE_Z0 + 250.0, LANE_Z0 + 700.0, z)
    return t * (35.0 * np.sin((z - LANE_Z0) / 420.0) + 18.0 * np.sin((z - LANE_Z0) / 150.0 + 0.4) * smooth(2000, 2600, z))


def far_relief(x, z, d):
    k = smooth(1400.0, 4000.0, d)
    return k * (70.0 * ridged(x / 1100.0, z / 1100.0, 3, 17) + 22.0 * ridged(x / 330.0, z / 330.0, 2, 19) - 42.0)


def raw_height(x, z):
    """The land before the beck and the lane are cut into it."""
    x = np.asarray(x, np.float64); z = np.asarray(z, np.float64)
    d = flat_dist(x, z)
    F = fells(x, z)
    F = terrace(F, x, z)
    F = F - ghylls(x, z)
    big = 0.35 + 11.0 * smooth(150.0, 1600.0, d)
    H = F + big * fbm(x / 520.0, z / 520.0, 5, 3)
    H = H + 0.22 * fbm(x / 22.0, z / 22.0, 2, 5) * smooth(4.0, 30.0, d)        # hummocky fields
    H = H - 0.004 * (np.clip(z, -2500.0, 9000.0) - 800.0) * smooth(15.0, 160.0, d)   # the dale falls south
    r = np.hypot(x - 0.0, (z - 1500.0) * 0.85)
    H = H + 560.0 * smooth(6000.0, 13000.0, r) * (0.35 + 0.65 * ridged(x / 4200.0, z / 4200.0, 4, 9))   # far ranges
    # Crisp ridges and gullies on the tops and the far hills (beenster: the distant hills looked
    # "a little muddy and blurred"), so they keep a shape at 5 to 15 km.
    H = H + far_relief(x, z, d)
    m = smooth(0.0, 28.0, d)
    return -0.08 * (1.0 - m) + H * m


def _line_levels(zs, xs_of, smooth_m, monotone):
    """Heights along a line (the beck's bed or the lane's), smoothed, optionally only ever falling."""
    h = raw_height(xs_of(zs), zs)
    k = max(1, int(smooth_m / (zs[1] - zs[0])))
    ker = np.exp(-0.5 * (np.arange(-3 * k, 3 * k + 1) / k) ** 2); ker /= ker.sum()
    hp = np.pad(h, 3 * k, mode="edge")
    hs = np.convolve(hp, ker, mode="valid")
    if monotone:
        hs = np.minimum.accumulate(hs)
    return hs


_BZ = np.arange(BECK_Z[0], BECK_Z[1] + 1.0, 2.0)
_BED = _line_levels(_BZ, beck_x, 30.0, True) - 1.15       # bed level along the beck
_LZ = np.arange(LANE_Z0, LANE_Z1 + 1.0, 2.0)
_LANE = _line_levels(_LZ, lane_x, 22.0, False)
_LANE = np.where(_LZ < LANE_Z0 + 40.0, -0.05, _LANE)       # level with the pad's apron at the gate
_LANE[:200] = np.minimum(_LANE[:200], -0.05 + (_LANE[:200] + 0.05) * smooth(LANE_Z0 + 40, LANE_Z0 + 400, _LZ[:200]))
BECK_HALF = 2.4
LANE_HALF = 2.0


def beck_dist(x, z):
    return np.where((z >= BECK_Z[0]) & (z <= BECK_Z[1]), np.abs(x - beck_x(z)), 1e9)


def lane_dist(x, z):
    return np.where((z >= LANE_Z0 - 1.0) & (z <= LANE_Z1), np.abs(x - lane_x(z)), 1e9)


def height(x, z):
    """The land, metres up, at x east, z south."""
    x = np.asarray(x, np.float64); z = np.asarray(z, np.float64)
    H = raw_height(x, z)
    # The beck: a 4 m stony bed a metre or so down, banks easing back over a few metres.
    bd = beck_dist(x, z)
    bed = np.interp(z, _BZ, _BED)
    f = smooth(2.0, 7.5, bd)
    H = np.where(bd < 7.5, np.minimum(H, bed + (H - bed) * f + 0.0), H)
    # The lane: its bed, the cut or fill blending back into the land over about 8 m.
    ld = lane_dist(x, z)
    lv = np.interp(z, _LZ, _LANE)
    g = smooth(LANE_HALF + 0.6, LANE_HALF + 9.0, ld)
    H = np.where(ld < LANE_HALF + 9.0, lv - 0.06 + (H - lv + 0.06) * g, H)
    # Under the pad and the yard: out of sight under the concrete.
    inside = (rect_dist(x, z, PAD) <= 0) | (rect_dist(x, z, YARD) <= 0)
    return np.where(inside, -0.13, H)


INTAKE_BASE = 108.0


def intake(z):
    """The intake wall's height above the dale floor: in-bye fields below, open moor above."""
    return INTAKE_BASE + 22.0 * vnoise(np.asarray(z) / 900.0, 2.2, 31)


# --- The fields ------------------------------------------------------------------------------------------

# Long walls down the dale (warped x positions) and, between each pair, cross walls at their own spacing.
LONG_E = (PAD[1] + 175.0, PAD[1] + 370.0, PAD[1] + 590.0, PAD[1] + 830.0)
LONG_W = (PAD[0] - 170.0, PAD[0] - 380.0, PAD[0] - 620.0)
LONG_S = (-150.0, 160.0)           # down the dale floor past the pad's end


def warp_x(z):
    return 26.0 * vnoise(np.asarray(z) / 520.0, 0.7, 41) + 9.0 * vnoise(np.asarray(z) / 130.0, 0.3, 42)


def warp_z(x):
    return 20.0 * vnoise(np.asarray(x) / 480.0, 5.5, 43) + 6.0 * vnoise(np.asarray(x) / 110.0, 2.5, 44)


FIELDS_Z = (-1600.0, 8200.0)       # the fields' colours go on down the dale past the walled stretch


def strips():
    """The field strips: (x_lo, x_hi, z range, cross wall z's) in warped coordinates."""
    rng = np.random.default_rng(7)
    out = []
    for xs, zr in ((list(LONG_W[::-1]) + [PAD[0]], FIELDS_Z),
                   ([PAD[1]] + list(LONG_E), FIELDS_Z),
                   ([PAD[0]] + list(LONG_S) + [PAD[1]], (PAD[3], FIELDS_Z[1])),
                   ([PAD[0], PAD[1]], (FIELDS_Z[0], YARD[2]))):
        for a, b in zip(xs[:-1], xs[1:]):
            zz = [zr[0]]
            while zz[-1] < zr[1]:
                zz.append(zz[-1] + float(rng.uniform(150.0, 320.0)))
            out.append((a, b, zr, np.array(zz)))
    return out


STRIPS = strips()


def field_index(x, z):
    """A number per field (for its colour), -1 off the fields."""
    xw = x + warp_x(z); zw = z + warp_z(x)
    idx = np.full(np.shape(x), -1, np.int64)
    for k, (a, b, zr, zz) in enumerate(STRIPS):
        m = (xw >= a) & (xw < b) & (zw >= zr[0]) & (zw < zr[1])
        if m.any():
            j = np.searchsorted(zz, zw[m])
            idx[m] = k * 64 + j
    # Beyond the walled stretch, fields still show as a patchwork of colour, from a coarse grid.
    off = idx < 0
    gx = np.floor((xw[off] - PAD[1]) / 170.0); gz = np.floor(zw[off] / 160.0)
    idx[off] = 100000 + ((gx.astype(np.int64) * 7919 + gz.astype(np.int64) * 104729) & 0xFFFF)
    return idx


def cover(x, z, H, slope):
    """Vertex colour (linear 0..255, RGBA) from where a point is and what it's like."""
    n = x.shape
    d = flat_dist(x, z)
    up = H - (-0.004 * (np.clip(z, -2500.0, 9000.0) - 800.0))          # height above the dale floor here
    inbye = smooth(intake(z) + 6.0, intake(z) - 6.0, up)
    fid = field_index(x, z)
    pal = np.array([lin(c) for c in FIELDS])
    f_col = pal[(fid * 2654435761 % 4294967296) % len(FIELDS)]
    f_col = f_col * (1.0 + 0.06 * fbm(x / 60.0, z / 60.0, 2, 61))[..., None]
    # The moor: rough grass, heather on the tops, bracken on the slopes just above the intake.
    hn = fbm(x / 260.0, z / 260.0, 4, 71)
    heather = 0.6 * smooth(0.0, 0.25, hn) * smooth(intake(z) + 120.0, intake(z) + 220.0, up)
    br = fbm(x / 140.0, z / 140.0, 3, 81)
    bracken = smooth(-0.1, 0.2, br) * smooth(intake(z) - 5.0, intake(z) + 15.0, up) * (1.0 - smooth(intake(z) + 90.0, intake(z) + 160.0, up))
    moor = np.repeat(lin(MOOR)[None, :], x.size, 0).reshape(n + (3,))
    moor = moor + (lin(HEATHER) - moor) * heather[..., None]
    moor = moor + (lin(BRACKEN) - moor) * (bracken * 0.85)[..., None]
    col = moor + (f_col - moor) * inbye[..., None]
    # Wet ground by the beck and down the ghylls, rock on the steep risers (M_Land adds rock by slope too).
    wet = (1.0 - smooth(4.0, 20.0, beck_dist(x, z))) * 0.7 + smooth(3.0, 9.0, ghylls(x, z)) * 0.6
    col = col + (lin(WET) - col) * np.clip(wet, 0, 1)[..., None]
    col = col + (lin(ROCK) - col) * (smooth(0.75, 1.1, slope) * 0.8)[..., None]
    # The spruce plantation: dark needles under it.
    col = col + (lin(FOREST) - col) * plantation_mask(x, z)[..., None]
    # Far away, M_Land leans on this colour: keep a broad, gentle variation in it.
    col = col * (1.0 + 0.07 * fbm(x / 1500.0, z / 1500.0, 3, 91))[..., None]
    # Shade baked into the far hills' colour: darker down the gullies, lighter on the ridges, so their
    # shape still reads through the haze.
    k = smooth(1400.0, 4000.0, d)
    rel = ridged(x / 1100.0, z / 1100.0, 3, 17) * 0.7 + ridged(x / 330.0, z / 330.0, 2, 19) * 0.3
    col = col * (1.0 + k * (rel - 0.5) * 0.45)[..., None]
    # The lane's verge: alpha 0 at the tarmac's edge (M_Land lays gravel there), 255 in the open.
    a = 255.0 * smooth(LANE_HALF + 0.2, LANE_HALF + 2.6, lane_dist(x, z))
    a = np.where(d < 1.0, 255.0, a)
    return np.concatenate([np.clip(col, 0, 255), a[..., None]], -1).round().astype(np.uint8)


PLANT = (-1900.0, -1380.0, 2600.0, 3300.0)   # the spruce plantation (x0, x1, z0, z1) on the west fellside


def plantation_mask(x, z):
    x0, x1, z0, z1 = PLANT
    sk = 0.18 * (z - z0)                      # planted on the skew, as they are
    return ((x - sk >= x0) & (x - sk <= x1) & (z >= z0) & (z <= z1)).astype(np.float64)


# --- Land tiles ---------------------------------------------------------------------------------------

def zone_tiles(zone, inner):
    name, step, size, x0, x1, z0, z1 = zone
    out = []
    for i, tx in enumerate(np.arange(x0, x1 - 1e-6, size)):
        for j, tz in enumerate(np.arange(z0, z1 - 1e-6, size)):
            if inner is not None:
                _, _, _, ix0, ix1, iz0, iz1 = inner
                if tx >= ix0 - 1e-6 and tx + size <= ix1 + 1e-6 and tz >= iz0 - 1e-6 and tz + size <= iz1 + 1e-6:
                    continue
            out.append((name, step, size, i, j, tx, tz))
    return out


def tile_mesh(step, size, tx, tz, skirt):
    n = int(round(size / step)) + 1
    xs = tx + np.arange(n) * step; zs = tz + np.arange(n) * step
    X, Z = np.meshgrid(xs, zs, indexing="xy")          # [row j (z), column i (x)]
    H = height(X, Z)
    # Normals from the heights a step either side (seamless across tiles).
    e = step
    hx = (height(X + e, Z) - height(X - e, Z)) / (2 * e)
    hz = (height(X, Z + e) - height(X, Z - e)) / (2 * e)
    nrm = np.stack([-hx, np.ones_like(hx), -hz], -1)
    nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    slope = np.hypot(hx, hz)
    col = cover(X, Z, H, slope)
    pos = np.stack([X, H, Z], -1).reshape(-1, 3)
    nrm = nrm.reshape(-1, 3); col = col.reshape(-1, 4)
    idx = np.arange(n * n).reshape(n, n)
    a = idx[:-1, :-1].ravel(); b = idx[:-1, 1:].ravel(); c = idx[1:, :-1].ravel(); d = idx[1:, 1:].ravel()
    # x east, y up, z south: (a, c, b) winds counter-clockwise seen from above (+y).
    tris = [np.stack([a, c, b], 1), np.stack([b, c, d], 1)]
    # Skirts: the border dropped by `skirt`, hiding cracks against a tile of other spacing.
    ring = np.concatenate([idx[0, :], idx[1:, -1], idx[-1, -2::-1], idx[-2:0:-1, 0]])
    m = len(ring)
    sp = pos[ring].copy(); sp[:, 1] -= skirt
    base = len(pos)
    pos = np.concatenate([pos, sp]); nrm = np.concatenate([nrm, nrm[ring]]); col = np.concatenate([col, col[ring]])
    r0 = ring; r1 = np.roll(ring, -1); s0 = base + np.arange(m); s1 = base + np.roll(np.arange(m), -1)
    for t0, t1 in ((np.stack([r0, s0, r1], 1), np.stack([r1, s0, s1], 1)),
                   (np.stack([r0, r1, s0], 1), np.stack([r1, s1, s0], 1))):   # both ways round: seen from either side
        tris.append(t0); tris.append(t1)
    return pos, np.concatenate(tris), nrm, col


_GRIDS = {}


def zone_grid(zone):
    """The zone's whole height grid, worked out once."""
    name, step, size, x0, x1, z0, z1 = zone
    if name not in _GRIDS:
        xs = x0 + np.arange(int(round((x1 - x0) / step)) + 2) * step
        zs = z0 + np.arange(int(round((z1 - z0) / step)) + 2) * step
        X, Z = np.meshgrid(xs, zs, indexing="xy")
        _GRIDS[name] = height(X, Z)
    return _GRIDS[name]


def ground_on_mesh(x, z):
    """The land's height as the tiles draw it (their triangles), for setting things on it."""
    x, z = np.broadcast_arrays(np.atleast_1d(np.asarray(x, np.float64)), np.atleast_1d(np.asarray(z, np.float64)))
    scalar = np.ndim(x) == 1 and x.size == 1
    out = np.empty(x.shape)
    done = np.zeros(x.shape, bool)
    for zone in ZONES:
        name, step, size, x0, x1, z0, z1 = zone
        m = ~done & (x >= x0) & (x < x1) & (z >= z0) & (z < z1)
        if not m.any():
            continue
        G = zone_grid(zone)
        gx = (x[m] - x0) / step; gz = (z[m] - z0) / step
        i = np.floor(gx).astype(np.int64); j = np.floor(gz).astype(np.int64); fx = gx - i; fz = gz - j
        ha = G[j, i]; hb = G[j, i + 1]; hc = G[j + 1, i]; hd = G[j + 1, i + 1]
        # triangles (a, c, b) and (b, c, d): split on the b-c diagonal (fx + fz = 1)
        lo = fx + fz <= 1.0
        h = np.where(lo, ha + (hb - ha) * fx + (hc - ha) * fz,
                     hd + (hc - hd) * (1.0 - fx) + (hb - hd) * (1.0 - fz))
        out[m] = h; done |= m
    if (~done).any():
        out[~done] = height(x[~done], z[~done])
    return float(out[0]) if scalar else out


# --- Ribbons: the beck's water and the lane ----------------------------------------------------------

def ribbon(zs, xs_of, level, half, crown=0.0, foam=None):
    zs = np.asarray(zs, np.float64)
    xc = xs_of(zs)
    d = np.stack([np.gradient(xc), np.gradient(zs)], 1)
    d /= np.linalg.norm(d, axis=1, keepdims=True)
    side = np.stack([-d[:, 1], d[:, 0]], 1)
    across = np.array([-1.0, -0.5, 0.0, 0.5, 1.0])
    P = np.zeros((len(zs), len(across), 3))
    P[:, :, 0] = xc[:, None] + side[:, None, 0] * across[None, :] * half
    P[:, :, 2] = zs[:, None] + side[:, None, 1] * across[None, :] * half
    P[:, :, 1] = level[:, None] + crown * (1.0 - across[None, :] ** 2)
    s = np.concatenate([[0.0], np.cumsum(np.hypot(np.diff(xc), np.diff(zs)))])
    U = np.stack([np.repeat(((across + 1.0) / 2.0)[None, :], len(zs), 0), np.repeat(s[:, None], len(across), 1)], -1)
    n, m = len(zs), len(across)
    idx = np.arange(n * m).reshape(n, m)
    a = idx[:-1, :-1].ravel(); b = idx[:-1, 1:].ravel(); c = idx[1:, :-1].ravel(); e = idx[1:, 1:].ravel()
    T = np.concatenate([np.stack([a, b, c], 1), np.stack([b, e, c], 1)])
    pos = P.reshape(-1, 3)
    fn = np.cross(pos[T[:, 1]] - pos[T[:, 0]], pos[T[:, 2]] - pos[T[:, 0]])
    if fn[:, 1].mean() < 0:
        T = T[:, [0, 2, 1]]
    nrm = np.zeros_like(pos)
    fn = np.cross(pos[T[:, 1]] - pos[T[:, 0]], pos[T[:, 2]] - pos[T[:, 0]])
    for k in range(3):
        np.add.at(nrm, T[:, k], fn)
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
    col = np.zeros((n, m, 4)); col[..., 3] = 255
    if foam is not None:
        col[..., 0] = (np.clip(foam, 0, 1) * 255.0)[:, None]
    return pos, T, nrm, col.reshape(-1, 4).astype(np.uint8), U.reshape(-1, 2)


# --- Props -------------------------------------------------------------------------------------------

def polyline_runs(pts, ok):
    """Split a sampled line into the runs where ok holds."""
    runs, cur = [], []
    for p, k in zip(pts, ok):
        if k:
            cur.append(p)
        elif cur:
            if len(cur) > 2:
                runs.append(np.array(cur))
            cur = []
    if len(cur) > 2:
        runs.append(np.array(cur))
    return runs


def field_walls():
    """The in-bye walls (long walls down the dale and the cross walls between them), the intake wall,
    and fell walls running on up to the tops."""
    lines = []
    for a, b, zr, zz in STRIPS:
        for xw in (a, b):
            if xw in (PAD[0], PAD[1]):
                continue                                  # the pad's own walls (drawn separately)
            z = np.arange(max(zr[0], WALLED_Z[0]), min(zr[1], WALLED_Z[1]), 1.0)
            lines.append(np.stack([xw - warp_x(z), z], 1))
        for zw in zz[1:-1]:
            if not WALLED_Z[0] < zw < WALLED_Z[1]:
                continue
            x = np.arange(a, b + 0.5, 1.0)
            # (warped: z + warp_z(x) = zw, and x + warp_x(z) = xw for the ends; near enough for a wall)
            z = zw - warp_z(x)
            x = x - warp_x(z)
            lines.append(np.stack([x, z], 1))
    # Clean duplicates (two strips share a long wall).
    uniq, seen = [], set()
    for L in lines:
        key = (round(float(L[0, 0]), 1), round(float(L[0, 1]), 1), round(float(L[-1, 0]), 1), len(L))
        if key not in seen:
            seen.add(key); uniq.append(L)
    return uniq


def intake_walls():
    """The intake wall either side: where the land reaches intake height above the dale floor."""
    out = []
    for side in (1, -1):
        pts = []
        for z in np.arange(WALLED_Z[0], WALLED_Z[1], 6.0):
            c = np.arange(30.0, 2200.0, 2.0)
            x = PAD[1] + c if side > 0 else PAD[0] - c
            up = raw_height(x, np.full_like(x, z)) + 0.004 * (min(max(z, -2500.0), 9000.0) - 800.0)
            k = np.flatnonzero(up >= float(intake(z)))
            if len(k):
                pts.append((x[k[0]], z))
        p = np.array(pts)
        # smooth it (the noise makes the crossing jumpy)
        for _ in range(4):
            p[1:-1, 0] = 0.25 * p[:-2, 0] + 0.5 * p[1:-1, 0] + 0.25 * p[2:, 0]
        q = BD.resample_line(p, 1.0)
        out.append(q)
    return out


def fell_walls():
    out = []
    for side, z0 in ((1, 450.0), (1, 1650.0), (-1, 1100.0), (-1, -300.0)):
        c = np.arange(500.0, 2100.0, 2.0)
        x = (PAD[1] + c) if side > 0 else (PAD[0] - c)
        z = z0 + 30.0 * vnoise(c / 400.0, z0, 51) + 0.05 * c
        out.append(BD.resample_line(np.stack([x, z], 1), 1.0))
    return out


def perimeter():
    """The pad's and the yard's walls (Tools/shed_yard.py WALLS) as one line round: their centres."""
    x0, x1 = PAD[0] - PAD_WALL, PAD[1] + PAD_WALL
    zn = PAD[2] - PAD_WALL; zs = PAD[3] + PAD_WALL
    yw, ye = -16.7, 18.7
    yb = -15.3
    corners = [(ye, zn), (ye, yb), (yw, yb), (yw, zn), (x0, zn), (x0, zs), (x1, zs), (x1, zn), (ye + 0.01, zn)]
    return BD.resample_line(np.array(corners), 1.0)


def scar_lines():
    """The scars: along the steep risers of the Yoredale steps, where the fellside is steeper than
    about 35 degrees, each riser as its own line."""
    lines = []
    for side in (1, -1):
        rows = {}
        for z in np.arange(WALLED_Z[0] - 300, WALLED_Z[1] + 900, 3.4):
            c = np.arange(200.0, 2600.0, 1.5)
            x = PAD[1] + c if side > 0 else PAD[0] - c
            h = raw_height(x, np.full_like(x, z))
            s = np.gradient(h, 1.5)
            steep = s > 0.9
            k = np.flatnonzero(steep[1:-1] & (s[1:-1] >= s[:-2]) & (s[1:-1] >= s[2:])) + 1
            for kk in k:
                lvl = int((h[kk] + 9.0 * float(fbm(x[kk] / 900.0, z / 900.0, 3, 21))) // TERRACE)
                rows.setdefault(lvl, []).append((x[kk], z))
        for lvl, pts in rows.items():
            pts = np.array(sorted(pts, key=lambda p: p[1]))
            # split where it jumps
            cut = np.flatnonzero((np.abs(np.diff(pts[:, 0])) > 12.0) | (np.diff(pts[:, 1]) > 8.0)) + 1
            for run in np.split(pts, cut):
                if len(run) >= 6 and np.hypot(*(run[-1] - run[0])) > 25.0:
                    lines.append(run)
    return lines


def make_props(counts, rng):
    props = BD.Props()

    def ground(x, z):
        return ground_on_mesh(x, z)

    def near_any(x, z, pad=0.0):
        return flat_dist(np.asarray(x, np.float64), np.asarray(z, np.float64)) > 0.45 + pad

    def near_field(x, z, pad=0.0):
        x = np.asarray(x, np.float64); z = np.asarray(z, np.float64)
        up = raw_height(x, z) + 0.004 * (np.clip(z, -2500.0, 9000.0) - 800.0)
        return near_any(x, z, pad) & (up < intake(z) - 1.0) & (beck_dist(x, z) > 3.4 + pad) & (plantation_mask(x, z) < 0.5)

    def no_road(x, z):
        return np.full(np.shape(x), 1e9) if np.ndim(x) else 1e9

    def road(x, z):
        return lane_dist(np.asarray(x, np.float64), np.asarray(z, np.float64))

    # The pad's walls: one line round, a field gate (shut) where the lane leaves it.
    BD.place_walls(props, {"roadside": [perimeter()], "roadside_gates": [(0.0, PAD[3] + PAD_WALL)]}, [],
                   ground, lambda x, z, pad=0.0: np.ones(np.shape(x), bool) if np.ndim(x) else True, no_road, LANE_HALF, rng, counts)
    pad_m = counts["wall_m"]; gates = {"pad": counts["gates"]}
    BD.place_walls(props, {"wall": field_walls()}, [], ground, near_field, road, LANE_HALF, rng, counts)
    field_m = counts["wall_m"]; gates["fields"] = counts["gates"]
    BD.place_walls(props, {"wall": intake_walls() + fell_walls()}, [], ground,
                   lambda x, z, pad=0.0: near_any(x, z, pad) & (beck_dist(np.asarray(x, np.float64), np.asarray(z, np.float64)) > 3.4),
                   road, LANE_HALF, rng, counts)
    fell_m = counts["wall_m"]
    # Walls along the lane, a little back from the tarmac.
    rl = []
    for off in (LANE_HALF + 1.9, -(LANE_HALF + 1.9)):
        z = np.arange(LANE_Z0 + 3.0, WALLED_Z[1] + 900.0, 1.0)
        x = lane_x(z)
        dx = np.gradient(x); n = np.hypot(dx, 1.0)
        rl.append(np.stack([x + off / n, z - off * dx / n], 1))
    BD.place_walls(props, {"roadside": rl, "roadside_gates": []}, [], ground,
                   lambda x, z, pad=0.0: near_any(x, z, pad) & (beck_dist(np.asarray(x, np.float64), np.asarray(z, np.float64)) > 3.4),
                   lambda x, z: np.maximum(road(x, z), LANE_HALF + 1.6), LANE_HALF, rng, counts)
    gates["lane"] = counts["gates"]; counts["gates"] = gates
    counts["wall_m"] = {"pad": pad_m, "fields": field_m, "intake_fell": fell_m, "lane": counts["wall_m"]}
    BD.place_scars(props, scar_lines(), ground, near_any, road, LANE_HALF, rng, counts)
    barns = place_barns(props, ground, rng, counts)
    place_sheep(props, ground, near_field, barns, rng, counts)
    # Boulders out on the moor.
    nb = 0
    for _ in range(4000):
        x = float(rng.uniform(-2600, 2600)); z = float(rng.uniform(-1800, 4200))
        up = float(raw_height(x, z)) + 0.004 * (min(max(z, -2500.0), 9000.0) - 800.0)
        if up < float(intake(z)) + 20 or not near_any(x, z) or road(x, z) < 6:
            continue
        props.rock(x, float(ground(x, z)), z, float(rng.choice([0.4, 0.7, 1.1, 1.6], p=[0.4, 0.3, 0.2, 0.1])), rng, 0.3)
        nb += 1
        if nb >= 700:
            break
    # Stones in and along the beck.
    for z in np.arange(-900.0, 3600.0, 3.0):
        if rng.uniform() < 0.55:
            x = float(beck_x(z)) + float(rng.normal(0, 1.6))
            props.rock(x, float(ground(x, z)) + 0.05, float(z), float(rng.choice([0.2, 0.35, 0.6], p=[0.5, 0.35, 0.15])), rng, 0.4)
    counts["boulders"] = nb
    return props, barns


def place_barns(props, ground, rng, counts):
    """Field barns in the in-bye fields, each near a corner of its field, ridge along the dale, and a
    farm (house and barn under one roof) east of the beck."""
    spots = []
    sites = [("farm", PAD[1] + 260.0, 1010.0, 22.0, 9.0, 5.6)]
    # Field barns: in some fields, close to the lower long wall.
    cand = []
    for k, (a, b, zr, zz) in enumerate(STRIPS):
        if a == PAD[0] and b == PAD[1]:
            continue
        for j in range(len(zz) - 1):
            if zz[j + 1] - zz[j] < 110:
                continue
            cand.append((k, j))
    order = rng.permutation(len(cand))
    for i in order:
        k, j = cand[i]
        a, b, zr, zz = STRIPS[k]
        lower = a if abs(a) < abs(b) else b             # the wall nearer the dale floor
        inward = 1.0 if lower == a else -1.0
        zc = zz[j] + float(rng.uniform(16.0, 30.0))
        w, d = float(rng.uniform(10.0, 14.0)), float(rng.uniform(6.2, 7.4))
        x = lower + inward * (d / 2 + 2.2)
        x = x - float(warp_x(zc)); zc = zc - float(warp_z(x))
        up = float(raw_height(x, zc)) + 0.004 * (min(max(zc, -2500.0), 9000.0) - 800.0)
        if up > float(intake(zc)) - 8 or beck_dist(x, zc) < 12 or lane_dist(x, zc) < 10 or flat_dist(x, zc) < 8:
            continue
        sites.append(("barn", x, zc, w, d, float(rng.uniform(3.6, 4.4))))
        if len(sites) >= 14:
            break
    shapes = {}
    for kind, x, z, w, d, h in sites:
        up = float(raw_height(x, z)) + 0.004 * (min(max(z, -2500.0), 9000.0) - 800.0)
        if kind == "barn" and (up > float(intake(z)) - 8 or beck_dist(x, z) < 12 or lane_dist(x, z) < 10):
            continue
        rot = math.pi / 2 + float(rng.normal(0, 0.02))      # ridge along the dale (z)
        c, s = math.cos(rot), math.sin(rot)
        g = float(ground(x, z))
        lo = min(float(ground(x + u * c - v * s, z + u * s + v * c)) for u in (-w / 2, w / 2) for v in (-d / 2, d / 2))
        plinth = math.ceil((g - lo + 0.4) * 2) / 2
        key = (kind, round(w * 2) / 2, round(d * 2) / 2, round(h, 1), plinth)
        if key not in shapes:
            parts = ST.building(np.random.default_rng(9100 + len(shapes)), kind, w, d, h, plinth,
                                {"roof": "stone", "porch": kind == "farm", "porch_ok": kind == "farm"})
            names = []
            for mat, M in parts.items():
                if M.n:
                    nm = "SM_Bld_%03d_%s" % (len(shapes), mat.capitalize())
                    props.meshes[nm] = M; props.mats[nm] = mat
                    names.append(nm)
            shapes[key] = names
        for nm in shapes[key]:
            props.add(nm, x, g, z, rot)
        spots.append((kind, x, z, w, d, rot))
    counts["barns"] = sum(1 for s in spots if s[0] == "barn"); counts["farms"] = sum(1 for s in spots if s[0] == "farm")
    return spots


def clear_of_barns(barns, x, z, r):
    for _k, bx, bz, w, d, rot in barns:
        c, s = math.cos(rot), math.sin(rot)
        u = (x - bx) * c + (z - bz) * s; v = -(x - bx) * s + (z - bz) * c
        if abs(u) < w / 2 + r and abs(v) < d / 2 + r:
            return False
    return True


def place_sheep(props, ground, near_field, barns, rng, counts):
    """Swaledale ewes, about half with lambs, in flocks in the fields (more of them in sight of the
    test area), and a few out on the moor."""
    poses = [("SM_Sheep_A", "graze"), ("SM_Sheep_B", "graze"), ("SM_Sheep_C", "look"), ("SM_Sheep_D", "lie")]
    lamb_poses = [("SM_Sheep_LambA", "graze"), ("SM_Sheep_LambB", "look"), ("SM_Sheep_LambC", "lie")]
    ewes = lambs = flocks = 0

    def at(name, make, x, z, a, sc):
        fx, fz = math.cos(a), math.sin(a)
        pitch = math.atan2(float(ground(x + fx * 0.5, z + fz * 0.5)) - float(ground(x - fx * 0.5, z - fz * 0.5)), 1.0)
        props.add(props.mesh(name, make), x, float(ground(x, z)) - 0.02, z, a, pitch, 0.0, (sc, sc, sc))

    tries = 0
    while flocks < 34 and tries < 5000:
        tries += 1
        # Most flocks within a kilometre or so of the test area.
        if rng.uniform() < 0.7:
            cx = TEST_AREA[0] + float(rng.normal(0, 700)); cz = TEST_AREA[1] + float(rng.normal(300, 700))
        else:
            cx = float(rng.uniform(-1500, 1700)); cz = float(rng.uniform(WALLED_Z[0], WALLED_Z[1]))
        if not bool(near_field(cx, cz, 10.0)) or not clear_of_barns(barns, cx, cz, 10) or lane_dist(cx, cz) < 15:
            continue
        flocks += 1
        heading = float(rng.uniform(0, 2 * math.pi))
        for _ in range(int(rng.integers(4, 11))):
            x = cx + float(rng.normal(0, 7)); z = cz + float(rng.normal(0, 7))
            if not bool(near_field(x, z, 1.5)) or not clear_of_barns(barns, x, z, 1.5) or lane_dist(x, z) < 6:
                continue
            k = int(rng.choice(4, p=[0.35, 0.3, 0.2, 0.15]))
            name, pose = poses[k]
            a = heading + float(rng.normal(0, 0.9))
            at(name, lambda k=k, pose=pose: ST.sheep(np.random.default_rng(700 + k), pose), x, z, a, float(rng.uniform(0.92, 1.06)))
            ewes += 1
            if rng.uniform() < 0.5:
                fx, fz = math.cos(a), math.sin(a)
                side = 1 if rng.uniform() < 0.5 else -1
                lx = x - fx * float(rng.uniform(0.1, 0.6)) - fz * side * float(rng.uniform(0.6, 0.9))
                lz = z - fz * float(rng.uniform(0.1, 0.6)) + fx * side * float(rng.uniform(0.6, 0.9))
                kl = int(rng.choice(3, p=[0.45, 0.35, 0.2] if pose != "lie" else [0.1, 0.2, 0.7]))
                ln, lp = lamb_poses[kl]
                at(ln, lambda kl=kl, lp=lp: ST.sheep(np.random.default_rng(720 + kl), lp, fleece=(0.6, 0.58, 0.52)),
                   lx, lz, a + float(rng.normal(0, 0.6)), float(rng.uniform(0.5, 0.58)))
                lambs += 1
    # A few out on the fell.
    for _ in range(40):
        x = float(rng.uniform(-2200, 2400)); z = float(rng.uniform(-1500, 3800))
        up = float(raw_height(x, z)) + 0.004 * (min(max(z, -2500.0), 9000.0) - 800.0)
        if up < float(intake(z)) + 10 or flat_dist(x, z) < 50:
            continue
        k = int(rng.choice(4, p=[0.35, 0.3, 0.2, 0.15]))
        name, pose = poses[k]
        at(name, lambda k=k, pose=pose: ST.sheep(np.random.default_rng(700 + k), pose), x, z, float(rng.uniform(0, 6.28)), 1.0)
        ewes += 1
    counts["ewes"] = ewes; counts["lambs"] = lambs; counts["flocks"] = flocks


# --- Trees ---------------------------------------------------------------------------------------------

TREE_MODELS = {"ash": 4, "sycamore": 4, "alder": 3, "birch": 2, "hawthorn": 3, "spruce": 2}
FAR_SPLIT = 1500.0     # beyond this from the test area, the light far models (as the ride does)


def make_trees(barns, rng, counts):
    rows = {}
    n = {}

    def plant(kind, x, z, sc=1.0):
        far = math.hypot(x - TEST_AREA[0], z - TEST_AREA[1]) > FAR_SPLIT
        k = int(rng.integers(TREE_MODELS[kind]))
        name = "SM_Tree%s_%s_%d" % ("Far" if far else "", kind, k)
        g = float(ground_on_mesh(x, z)) - 0.25
        s = sc * float(rng.uniform(0.85, 1.15))
        rows.setdefault(name, []).append([round(x * 100, 1), round(z * 100, 1), round(g * 100, 1), 0.0,
                                          round(float(rng.uniform(0, 360)), 1), 0.0, round(s, 3), round(s, 3), round(s, 3)])
        n[kind] = n.get(kind, 0) + 1

    # Alders and ash along the beck.
    z = BECK_Z[0] + 200.0
    while z < BECK_Z[1] - 300.0:
        z += float(rng.uniform(14.0, 70.0))
        side = 1 if rng.uniform() < 0.5 else -1
        x = float(beck_x(z)) + side * float(rng.uniform(4.0, 9.0))
        if flat_dist(x, z) < 12 or lane_dist(x, z) < 8:
            continue
        plant("alder" if rng.uniform() < 0.55 else ("ash" if rng.uniform() < 0.7 else "birch"), x, z)
        if rng.uniform() < 0.3:                       # in twos and threes
            plant("alder", x + float(rng.normal(0, 4)), z + float(rng.normal(0, 5)))
    # Sycamores round the farm, an ash or two by some barns.
    for kind, bx, bz, w, d, rot in barns:
        if kind == "farm":
            for _ in range(9):
                a = float(rng.uniform(0, 2 * math.pi)); r = float(rng.uniform(w * 0.6, w * 1.2))
                x, z = bx + math.cos(a) * r, bz + math.sin(a) * r
                if clear_of_barns(barns, x, z, 3.0) and beck_dist(x, z) > 6:
                    plant("sycamore", x, z, 1.05)
        elif rng.uniform() < 0.55:
            a = rot + (math.pi / 2 if rng.uniform() < 0.5 else -math.pi / 2)
            x, z = bx + math.cos(a) * (d / 2 + 6.0) + float(rng.normal(0, 3)), bz + math.sin(a) * (d / 2 + 6.0) + float(rng.normal(0, 6))
            if clear_of_barns(barns, x, z, 3.0):
                plant("ash" if rng.uniform() < 0.6 else "sycamore", x, z)
    # Hawthorns by the field walls, and the odd ash out in a field.
    for a, b, zr, zz in STRIPS:
        for zw in zz[1:-1]:
            if WALLED_Z[0] < zw < WALLED_Z[1] and rng.uniform() < 0.28:
                xm = float(rng.uniform(a + 5, b - 5)); zt = zw - float(warp_z(xm)) + float(rng.choice([-2.5, 2.5]))
                xt = xm - float(warp_x(zt))
                up = float(raw_height(xt, zt)) + 0.004 * (min(max(zt, -2500.0), 9000.0) - 800.0)
                if flat_dist(xt, zt) > 8 and up < float(intake(zt)) and clear_of_barns(barns, xt, zt, 3) and lane_dist(xt, zt) > 6:
                    plant("hawthorn" if rng.uniform() < 0.7 else "ash", xt, zt)
    # Ash and rowan-ish birch up the ghylls.
    for side, z0, depth in GHYLLS:
        for c in np.arange(200.0, 1100.0, 30.0):
            if rng.uniform() < 0.45:
                cc = float(c) + float(rng.normal(0, 6))
                zg = z0 + 45.0 * math.sin(cc / 230.0) + 0.08 * cc * (1 if side == "e" else -1) + float(rng.normal(0, 5))
                x = PAD[1] + cc if side == "e" else PAD[0] - cc
                plant("ash" if rng.uniform() < 0.6 else "birch", x, zg)
    # The plantation: spruce in rows.
    x0, x1, z0, z1 = PLANT
    for zz_ in np.arange(z0 + 4, z1 - 4, 11.0):
        for xx in np.arange(x0 + 4, x1 - 4, 11.0):
            x = float(xx + 0.18 * (zz_ - z0) + rng.normal(0, 1.2)); z = float(zz_ + rng.normal(0, 1.2))
            plant("spruce", x, z, float(rng.uniform(0.85, 1.05)))
    counts["trees"] = n
    return {"about": "Tree instances (Tools/make_shed_dales.py) of the Buttertubs ride's tree models in "
                     "/Game/Buttertubs/Trees (Tools/make_trees.py): Unreal [x, y, z] cm, [pitch, yaw, roll] "
                     "degrees, [sx, sy, sz].", "instances": rows}


# --- Writing it all -----------------------------------------------------------------------------------

def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".glb"):
            os.remove(os.path.join(OUT, f))
    counts = {}
    tiles = []
    verts = 0
    inner = None
    for zone in ZONES:
        skirt = {"n": 1.5, "m": 6.0, "f": 25.0}[zone[0]]
        for name, step, size, i, j, tx, tz in zone_tiles(zone, inner):
            pos, tris, nrm, col = tile_mesh(step, size, tx, tz, skirt)
            nm = "land_%s_%02d_%02d" % (name, i, j)
            write_glb(os.path.join(OUT, nm + ".glb"), pos, tris, nrm, col, nm)
            tiles.append(nm); verts += len(pos)
        inner = zone
    counts["land_tiles"] = len(tiles); counts["land_vertices"] = verts
    print("land: %d tiles, %d vertices" % (len(tiles), verts))

    # The beck's water: a little above its bed, foam where it falls faster.
    zs = np.arange(BECK_Z[0] + 60.0, BECK_Z[1] - 60.0, 2.0)
    bed = np.interp(zs, _BZ, _BED)
    fall = -np.gradient(bed, zs)
    lvl = np.maximum(bed + 0.32, ground_on_mesh(beck_x(zs), zs) + 0.03)   # (the 20 m land past 1.9 km can't hold its channel)
    pos, T, nrm, col, uv = ribbon(zs, beck_x, lvl, BECK_HALF, 0.0, foam=smooth(0.004, 0.03, fall) * 0.8 + 0.1)
    write_glb(os.path.join(OUT, "beck.glb"), pos, T, nrm, col, "beck", uv)
    zs = np.arange(LANE_Z0, LANE_Z1, 2.0)
    lvl = np.interp(zs, _LZ, _LANE) + 0.02
    for off in (-LANE_HALF, 0.0, LANE_HALF):            # on top of the coarse land too, edge to edge
        lvl = np.maximum(lvl, ground_on_mesh(lane_x(zs) + off, zs) + 0.03)
    pos, T, nrm, col, uv = ribbon(zs, lane_x, lvl, LANE_HALF, 0.04)
    write_glb(os.path.join(OUT, "lane.glb"), pos, T, nrm, col, "lane", uv)

    rng = np.random.default_rng(2026)
    props, barns = make_props(counts, rng)
    info = props.write(OUT, write_glb)
    counts.update(info)
    trees = make_trees(barns, rng, counts)
    with open(os.path.join(OUT, "trees.json"), "w") as f:
        json.dump(trees, f, separators=(",", ":"))
    js = {"about": "The Dales round the shed's pad (Tools/make_shed_dales.py); Tools/ue_build_shed.py places it.",
          "land": tiles, "ribbons": {"beck": "water", "lane": "tarmac"}, "counts": counts,
          "frame_probe": "land_f_00_00"}
    with open(os.path.join(OUT, "dales.json"), "w") as f:
        json.dump(js, f, indent=1)
        f.write("\n")
    print(json.dumps(counts, indent=1))


if __name__ == "__main__":
    main()
