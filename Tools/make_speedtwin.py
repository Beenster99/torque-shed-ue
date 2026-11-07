"""Build the prototype's 2025 Triumph Speed Twin 1200 as glb parts, from measurements of beenster's
own bike (side-on photos scaled to the 1413 mm wheelbase, details from a walkaround video).

    python Tools/make_speedtwin.py            writes Data/Bike/SpeedTwin/*.glb and speedtwin.json
    python Tools/make_speedtwin.py --check P  also renders the right side over photo P (a check)

Frame of every part: metres, x forward, y up, z to the bike's right (right handed, a valid glTF
frame). The origin is the body origin the riding sim uses: axle height with the suspension fully
extended, midway between the axles. Parts come in groups the bike pawn moves differently:

    body         rigid with the frame (body origin)
    steer        turns with the bars about the steering axis (body origin)
    front_lower  fork sliders, mudguard, calipers: follow the front axle, steered (origin: front axle)
    front_wheel  spins (origin: front axle)
    swingarm     pivots with the rear suspension (origin: the swingarm pivot)
    rear_wheel   spins (origin: rear axle)

Each part is one glb with one material slot; speedtwin.json lists them with their group and finish.
Shapes are lathes, tubes and superellipse lofts: a clean stand-in to refine, not a scan.
"""
import argparse, json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from export_buttertubs import write_glb  # noqa: E402

WB = 1.413
RAKE = math.radians(22.8)
OFFSET = 0.030
FRONT_AXLE = np.array([WB / 2, 0.0, 0.0])
REAR_AXLE = np.array([-WB / 2, 0.0, 0.0])
FORK_DIR = np.array([-math.sin(RAKE), math.cos(RAKE), 0.0])        # up the fork legs
FORK_BACK = np.array([-math.cos(RAKE), -math.sin(RAKE), 0.0])      # square to the legs, rearwards
STEER_POINT = FRONT_AXLE + FORK_BACK * OFFSET                      # a point on the steering axis
SWING_PIVOT = np.array([-0.335, 0.075, 0.0])

# Finishes: the level script makes a material instance of each. "paint" finishes use the clear coat
# master (a metallic or solid base under a glossy lacquer), the rest the plain one. Each can have a
# second finish its parts blend to by their vertex mask, and a detail set from Tools/make_bike_textures.py
# (a normal map and a roughness/shade map) repeated uv_scale times per metre of UV.
def finish(color, metallic, roughness, shader="base", color2=None, metallic2=None, roughness2=None,
           detail="flat", uv_scale=1.0, normal_strength=1.0, rough_var=0.0, clearcoat_roughness=0.03, emissive=0.0):
    return {"shader": shader, "color": list(color), "metallic": metallic, "roughness": roughness,
            "color2": list(color2 if color2 is not None else color),
            "metallic2": metallic if metallic2 is None else metallic2,
            "roughness2": roughness if roughness2 is None else roughness2,
            "detail": detail, "uv_scale": uv_scale, "normal_strength": normal_strength, "rough_var": rough_var,
            "clearcoat_roughness": clearcoat_roughness, "emissive": emissive}


GLOSS_BLACK = (0.010, 0.010, 0.011)
MACHINED = (0.78, 0.78, 0.77)
FINISHES = {
    # Silver tank: metallic base with flake under lacquer; the mask is the black pinstripe.
    "paint_silver": finish((0.60, 0.61, 0.62), 1.0, 0.32, "paint", color2=GLOSS_BLACK, metallic2=0.0, roughness2=0.2,
                           detail="flake", uv_scale=25.0, normal_strength=0.0, rough_var=0.25),
    "black_gloss": finish(GLOSS_BLACK, 0.0, 0.2, "paint", detail="flake", uv_scale=25.0, normal_strength=0.0),
    "black_satin": finish((0.018, 0.018, 0.019), 0.0, 0.48, detail="powder", uv_scale=6.0, normal_strength=0.35, rough_var=0.2),
    # Black cases and barrels; the mask is machined metal (the fin edges, the cover rims).
    "black_engine": finish((0.022, 0.022, 0.024), 0.25, 0.4, color2=MACHINED, metallic2=1.0, roughness2=0.22,
                           detail="cast", uv_scale=8.0, normal_strength=0.5, rough_var=0.25),
    "chrome": finish((0.93, 0.93, 0.93), 1.0, 0.04),
    "steel_brushed": finish((0.74, 0.73, 0.71), 1.0, 0.26, detail="brushed", uv_scale=5.0, normal_strength=0.6, rough_var=0.35),
    # Brushed stainless headers, golden and blue heat tint near the ports (the mask).
    "header": finish((0.74, 0.73, 0.71), 1.0, 0.26, color2=(0.62, 0.40, 0.22), metallic2=1.0, roughness2=0.3,
                     detail="brushed", uv_scale=5.0, normal_strength=0.6, rough_var=0.35),
    "alu_cast": finish((0.80, 0.80, 0.80), 1.0, 0.35, detail="cast", uv_scale=8.0, normal_strength=0.6, rough_var=0.3),
    "alu_brushed": finish((0.86, 0.86, 0.85), 1.0, 0.2, detail="brushed", uv_scale=10.0, normal_strength=0.5, rough_var=0.3),
    "rubber": finish((0.018, 0.018, 0.018), 0.0, 0.85, detail="rubber", uv_scale=12.0, normal_strength=0.6, rough_var=0.2),
    "grip": finish((0.016, 0.016, 0.016), 0.0, 0.8, detail="knurl", uv_scale=30.0, normal_strength=1.0, rough_var=0.2),
    # Tyres: the tread map is wrapped 24 times round and once across, from bead to bead.
    "tyre": finish((0.022, 0.022, 0.022), 0.0, 0.78, detail="tyre", uv_scale=1.0, normal_strength=1.0, rough_var=0.4),
    "leather": finish((0.020, 0.019, 0.018), 0.0, 0.55, detail="leather", uv_scale=7.0, normal_strength=0.8, rough_var=0.3),
    "disc_steel": finish((0.55, 0.55, 0.56), 1.0, 0.3, detail="machined", uv_scale=8.0, normal_strength=0.5, rough_var=0.4),
    "fork_inner": finish((0.62, 0.62, 0.63), 1.0, 0.12, color2=(0.018, 0.018, 0.019), metallic2=0.0, roughness2=0.45),
    # Headlight: black bowl, the mask is its brushed aluminium bezel.
    "headlight": finish(GLOSS_BLACK, 0.0, 0.18, color2=(0.86, 0.86, 0.85), metallic2=1.0, roughness2=0.18,
                        detail="brushed", uv_scale=10.0, normal_strength=0.3),
    "bolt": finish((0.62, 0.62, 0.62), 1.0, 0.3, detail="cast", uv_scale=20.0, normal_strength=0.3),
    "lens_clear": finish((0.85, 0.87, 0.9), 1.0, 0.04),
    "lens_red": finish((0.6, 0.02, 0.02), 0.0, 0.1),
    "lens_amber": finish((0.9, 0.35, 0.02), 0.0, 0.15),
    "chain": finish((0.16, 0.15, 0.14), 1.0, 0.45, detail="cast", uv_scale=30.0, normal_strength=0.5, rough_var=0.3),
}


# --- Geometry --------------------------------------------------------------------------------------

class Mesh:
    """Pieces of triangles, each with its own vertices, local indices, UVs and a finish mask. UVs are
    in metres (u round a section, v along it) unless a part asks otherwise; the mask (0..1, written as
    vertex colour red) blends a part's finish towards its second finish (a pinstripe, fin edges, heat
    tint)."""
    def __init__(self):
        self.p, self.t, self.uv, self.m = [], [], [], []

    def add(self, pos, tris, uv=None, mask=None):
        pos = np.asarray(pos, np.float64)
        self.p.append(pos)
        self.t.append(np.asarray(tris, np.int64))
        self.uv.append(np.zeros((len(pos), 2)) if uv is None else np.asarray(uv, np.float64))
        self.m.append(np.zeros(len(pos)) if mask is None else np.broadcast_to(np.asarray(mask, np.float64), (len(pos),)).copy())
        return self

    def extend(self, other):
        self.p += other.p; self.t += other.t; self.uv += other.uv; self.m += other.m
        return self

    def masked(self, value):
        """The whole mesh on its second finish (value 1) or its first (0)."""
        self.m = [np.full(len(p), float(value)) for p in self.p]
        return self

    def mask_by(self, fn):
        """Mask from a function of each vertex position (an (n, 3) array in, n values in 0..1 out)."""
        self.m = [np.clip(fn(p), 0, 1) for p in self.p]
        return self

    def mask_by_uv(self, fn):
        """Mask from a function of each vertex's UVs (an (n, 2) array in, n values out)."""
        self.m = [np.clip(fn(uv), 0, 1) for uv in self.uv]
        return self

    def arrays(self):
        offs = np.cumsum([0] + [len(p) for p in self.p[:-1]])
        return (np.concatenate(self.p), np.concatenate([t + o for t, o in zip(self.t, offs)]),
                np.concatenate(self.uv), np.concatenate(self.m))

    def transformed(self, R=None, T=(0, 0, 0)):
        m = Mesh()
        for p, t, uv, k in zip(self.p, self.t, self.uv, self.m):
            m.add(p @ (np.eye(3) if R is None else R).T + np.asarray(T), t, uv, k)
        return m


def grid_tris(rows, cols, wrap_cols=True):
    """Triangles over a rows x cols vertex grid; columns wrap if asked."""
    tris = []
    cc = cols if wrap_cols else cols - 1
    for r in range(rows - 1):
        for c in range(cc):
            a = r * cols + c; b = r * cols + (c + 1) % cols
            d = (r + 1) * cols + c; e = (r + 1) * cols + (c + 1) % cols
            tris += [(a, d, b), (b, d, e)]
    return np.array(tris)


def frames(path, up_hint=(0, 1, 0)):
    """Tangent, normal, binormal along a path, by parallel transport from an up hint."""
    path = np.asarray(path, np.float64)
    tan = np.gradient(path, axis=0)
    tan /= np.linalg.norm(tan, axis=1, keepdims=True)
    up = np.asarray(up_hint, np.float64)
    if abs(np.dot(up, tan[0])) > 0.95:
        up = np.array([0, 0, 1.0]) if abs(tan[0][2]) < 0.9 else np.array([1.0, 0, 0])
    n = up - tan[0] * np.dot(up, tan[0]); n /= np.linalg.norm(n)
    N = [n]
    for i in range(1, len(path)):
        v = N[-1] - tan[i] * np.dot(N[-1], tan[i])
        if np.linalg.norm(v) < 1e-9:
            v = N[-1]
        N.append(v / np.linalg.norm(v))
    N = np.array(N)
    B = np.cross(tan, N)
    return tan, N, B


def sweep(path, half_w, half_h, power=2.0, seg=32, caps=True, up_hint=(0, 1, 0), offset_up=None):
    """A superellipse section (half width along the binormal, half height along the normal) swept
    along a path. half_w, half_h (and offset_up, shifting the section along its normal) can be
    arrays, one per path point. power 2 is an ellipse, 4 to 8 a rounded box. UVs in metres: u round
    the section, v along the path; the seam column is doubled so textures wrap cleanly."""
    path = np.asarray(path, np.float64)
    n = len(path)
    hw = np.broadcast_to(np.asarray(half_w, np.float64), (n,))
    hh = np.broadcast_to(np.asarray(half_h, np.float64), (n,))
    off = np.zeros(n) if offset_up is None else np.broadcast_to(np.asarray(offset_up, np.float64), (n,))
    tan, N, B = frames(path, up_hint)
    a = np.linspace(0, 2 * math.pi, seg + 1)
    c, s = np.cos(a), np.sin(a)
    e = 2.0 / power
    cx = np.sign(c) * np.abs(c) ** e
    sy = np.sign(s) * np.abs(s) ** e
    pts = (path[:, None, :] + N[:, None, :] * (off[:, None, None] + hh[:, None, None] * sy[None, :, None])
           + B[:, None, :] * (hw[:, None, None] * cx[None, :, None]))
    pts[:, -1] = pts[:, 0]
    perim = 2 * math.pi * np.sqrt((hw ** 2 + hh ** 2) / 2)
    along = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))])
    uv = np.stack([(a / (2 * math.pi))[None, :] * perim[:, None], np.broadcast_to(along[:, None], (n, seg + 1))], -1)
    pos = pts.reshape(-1, 3)
    uv = uv.reshape(-1, 2)
    tris = grid_tris(n, seg + 1, wrap_cols=False)
    if caps:
        # Caps in the same piece, wound to match the sides, so the piece is closed.
        for i, flip in ((0, True), (n - 1, False)):
            ring = np.arange(seg) + i * (seg + 1)
            ci = len(pos)
            pos = np.vstack([pos, (path[i] + N[i] * off[i])[None]])
            uv = np.vstack([uv, [[perim[i] / 2, along[i]]]])
            ct = np.array([(ci, ring[k], ring[(k + 1) % seg]) for k in range(seg)])
            tris = np.vstack([tris, ct[:, [0, 2, 1]] if flip else ct])
    return Mesh().add(pos, tris, uv)


def tube(path, r, seg=14, caps=True):
    return sweep(path, r, r, 2.0, max(seg, 20), caps)


def lathe(profile, seg=48, axis=(0, 0, 1), centre=(0, 0, 0), closed=False, uv_norm=None):
    """Revolve a profile of (radius, along-axis) points about an axis through centre. UVs in metres
    (u round the largest radius, v along the profile), or with uv_norm=k, u = k repeats round and
    v = 0..1 along the profile (the tyres' tread map)."""
    prof = np.asarray(profile, np.float64)
    ax = np.asarray(axis, np.float64); ax /= np.linalg.norm(ax)
    u = np.cross(ax, [0, 1, 0] if abs(ax[1]) < 0.9 else [1, 0, 0]); u /= np.linalg.norm(u)
    v = np.cross(ax, u)
    a = np.linspace(0, 2 * math.pi, seg + 1)
    ca, sa = np.cos(a), np.sin(a)
    ca[-1], sa[-1] = ca[0], sa[0]
    pts = (np.asarray(centre)[None, None, :]
           + prof[:, None, 1:2] * ax[None, None, :]
           + prof[:, None, 0:1] * (ca[None, :, None] * u + sa[None, :, None] * v))
    rows = len(prof)
    arc = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(prof, axis=0), axis=1))])
    if uv_norm:
        uu = a / (2 * math.pi) * uv_norm; vv = arc / max(arc[-1], 1e-9)
    else:
        uu = a * max(np.abs(prof[:, 0]).max(), 1e-3); vv = arc
    uv = np.stack(np.broadcast_arrays(uu[None, :], vv[:, None]), -1).reshape(-1, 2)
    tris = grid_tris(rows, seg + 1, wrap_cols=False)
    return Mesh().add(pts.reshape(-1, 3), tris, uv)


def box(centre, size, power=6.0, seg=28):
    """A rounded box along x."""
    cx, cy, cz = centre; sx, sy, sz = size
    k = 13
    xs = cx + np.linspace(-sx / 2, sx / 2, k)
    t = np.linspace(-1, 1, k)
    taper = (1 - np.abs(t) ** power) ** (1 / power)  # round the ends too
    taper = np.maximum(taper, 0.05)
    path = np.stack([xs, np.full(k, cy), np.full(k, cz)], 1)
    return sweep(path, sz / 2 * taper, sy / 2 * taper, power, seg, caps=True)


def ring_points(centre, radius, n, axis_z=True):
    a = np.linspace(0, 2 * math.pi, n, endpoint=False)
    return [np.asarray(centre) + radius * np.array([math.cos(x), math.sin(x), 0.0]) for x in a]


def polyline(pts, n_per=8):
    """A smooth path through points (Catmull-Rom)."""
    P = np.asarray(pts, np.float64)
    P = np.vstack([P[0] * 2 - P[1], P, P[-1] * 2 - P[-2]])
    out = []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        for t in np.linspace(0, 1, n_per, endpoint=False):
            t2, t3 = t * t, t * t * t
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    out.append(P[-2])
    return np.array(out)


def extrude(outline, z0, z1):
    """A flat plate from a 2D outline (x, y points, star shaped about the origin) between z0 and z1."""
    o = np.asarray(outline, np.float64)
    n = len(o)
    o = np.vstack([o, o[:1]])
    side = np.concatenate([np.c_[o, np.full(n + 1, z0)], np.c_[o, np.full(n + 1, z1)]])
    perim = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(o, axis=0), axis=1))])
    uv = np.c_[np.r_[perim, perim], np.r_[np.zeros(n + 1), np.full(n + 1, z1 - z0)]]
    m = Mesh().add(side, grid_tris(2, n + 1, wrap_cols=False), uv)
    for z, flip in ((z0, True), (z1, False)):
        pts = np.vstack([[0, 0, z], np.c_[o[:-1], np.full(n, z)]])
        t = np.array([(0, 1 + k, 1 + (k + 1) % n) for k in range(n)])
        m.add(pts, t[:, [0, 2, 1]] if flip else t, pts[:, :2])
    return m


def sprocket_outline(teeth, r_tip, r_root):
    """A toothed outline: round-bottomed gaps between flat-topped teeth."""
    pts = []
    for k in range(teeth):
        a0 = 2 * math.pi * k / teeth
        da = 2 * math.pi / teeth
        for f, r in ((0.0, r_root), (0.12, r_root + (r_tip - r_root) * 0.35), (0.3, r_tip), (0.7, r_tip),
                     (0.88, r_root + (r_tip - r_root) * 0.35)):
            pts.append((r * math.cos(a0 + f * da), r * math.sin(a0 + f * da)))
    return pts


def hex_bolt(centre, axis, size=0.0065):
    """A hex bolt head with a washer, standing out along axis from centre."""
    return (lathe([(0.0, 0.0), (size * 1.35, 0.0), (size * 1.35, 0.0012), (0.0, 0.0012)], seg=20, axis=axis, centre=centre)
            .extend(lathe([(0.0, 0.0012), (size, 0.0012), (size, size * 0.75), (size * 0.75, size * 0.95), (0.0, size * 0.95)],
                          seg=6, axis=axis, centre=centre)))


def mirror_z(m):
    """The same part on the other side (mirror in z), with the winding fixed."""
    out = Mesh()
    for p, t, uv, k in zip(m.p, m.t, m.uv, m.m):
        q = p.copy(); q[:, 2] *= -1
        out.add(q, t[:, [0, 2, 1]], uv, k)
    return out


def both_sides(m):
    return Mesh().extend(m).extend(mirror_z(m))


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1.0]])


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


# --- The bike, part by part --------------------------------------------------------------------------

def wheel(radius_tyre, tyre_w, rim_r=0.2159, rim_w=None, spokes=10, front=True):
    """Tyre, rim and split-spoke cast wheel, centred on the axle, axis z."""
    parts = {}
    sec_h = radius_tyre - rim_r
    a = np.linspace(-math.pi * 0.5, math.pi * 1.5, 25)
    # Tyre: a fat rounded section from bead to bead.
    prof = []
    for t in np.linspace(0, 1, 26):
        ang = math.pi * (t - 0.5) * 1.6
        r = rim_r + 0.012 + (sec_h - 0.012) * (0.5 + 0.5 * math.cos(ang * 0.75)) ** 0.5
        z = tyre_w / 2 * math.sin(ang)
        prof.append((r, z))
    prof = [(rim_r + 0.005, -tyre_w * 0.42)] + prof + [(rim_r + 0.005, tyre_w * 0.42)]
    parts["tyre"] = (lathe(prof, seg=144, uv_norm=24), "tyre")
    rw = rim_w or tyre_w * 0.8
    rim = lathe([(rim_r + 0.012, -rw / 2), (rim_r, -rw / 2 + 0.006), (rim_r - 0.012, -rw / 2 + 0.01),
                 (rim_r - 0.016, 0.0), (rim_r - 0.012, rw / 2 - 0.01), (rim_r, rw / 2 - 0.006),
                 (rim_r + 0.012, rw / 2), (rim_r - 0.02, rw / 2 - 0.004), (rim_r - 0.02, -rw / 2 + 0.004),
                 (rim_r + 0.012, -rw / 2)], seg=72)
    hub = lathe([(0.0, -0.07), (0.035, -0.07), (0.05, -0.05), (0.055, 0.0), (0.05, 0.05), (0.035, 0.07),
                 (0.0, 0.07)], seg=32)
    sp = Mesh()
    for k in range(spokes):
        base = 2 * math.pi * k / spokes
        for side, da in ((-1, 0.07), (1, -0.07)):
            # Split spokes: each pair meets at the hub and spreads at the rim, a little dished.
            p0 = np.array([0.05 * math.cos(base), 0.05 * math.sin(base), side * 0.012])
            p1 = np.array([(rim_r - 0.015) * math.cos(base + da), (rim_r - 0.015) * math.sin(base + da), side * 0.008])
            sp.extend(sweep(np.linspace(p0, p1, 6), 0.0055, 0.009, 3.0, 10, caps=True, up_hint=(0, 0, 1)))
    parts["rim"] = (Mesh().extend(rim).extend(hub).extend(sp), "black_gloss")
    return parts


def disc(centre_z, r_out, r_in, holes=True):
    d = lathe([(r_in, centre_z - 0.0025), (r_out, centre_z - 0.0025), (r_out, centre_z + 0.0025),
               (r_in, centre_z + 0.0025), (r_in, centre_z - 0.0025)], seg=64)
    carrier = lathe([(0.04, centre_z - 0.003), (r_in + 0.005, centre_z - 0.003), (r_in + 0.005, centre_z + 0.003),
                     (0.04, centre_z + 0.003), (0.04, centre_z - 0.003)], seg=40)
    # Floating disc: ten bobbins joining the carrier to the rotor.
    for k in range(10):
        a = 2 * math.pi * (k + 0.5) / 10
        c = (r_in * math.cos(a), r_in * math.sin(a), centre_z - 0.004)
        carrier.extend(lathe([(0.0, 0.0), (0.0075, 0.0), (0.0075, 0.008), (0.0, 0.008)], seg=16, centre=c))
    return d, carrier


def build():
    P = {}   # name -> (mesh, finish, group)

    def put(name, mesh, finish, group="body"):
        P[name] = (mesh, finish, group)

    # Wheels.
    for name, w in wheel(0.300, 0.12, front=True).items():
        put("front_" + name, w[0], w[1], "front_wheel")
    for name, w in wheel(0.312, 0.16, rim_w=0.115, front=False).items():
        put("rear_" + name, w[0], w[1], "rear_wheel")
    d1, c1 = disc(0.075, 0.16, 0.115)
    d2, c2 = disc(-0.075, 0.16, 0.115)
    put("front_discs", Mesh().extend(d1).extend(d2), "disc_steel", "front_wheel")
    put("front_carriers", Mesh().extend(c1).extend(c2), "alu_cast", "front_wheel")
    rd, rc = disc(-0.07, 0.11, 0.07)
    put("rear_disc", rd, "disc_steel", "rear_wheel")
    # Sprocket on the right (as on beenster's bike), 41 teeth, about 104 mm.
    sprocket = extrude(sprocket_outline(41, 0.106, 0.098), 0.068, 0.074)
    for k in range(5):     # the five bolts holding it to the cush drive
        a = 2 * math.pi * k / 5
        sprocket.extend(hex_bolt((0.055 * math.cos(a), 0.055 * math.sin(a), 0.074), (0, 0, 1), 0.006))
    put("rear_sprocket", sprocket, "alu_cast", "rear_wheel")

    # Front end, fork lowers (origin: front axle; legs run up FORK_DIR).
    lower = Mesh()
    for z in (-0.095, 0.095):
        a = np.array([0.0, 0.0, z]) - FORK_DIR * 0.03
        b = a + FORK_DIR * 0.32
        lower.extend(tube(np.linspace(a, b, 6), 0.0215, 28))   # the upside-down fork's slim inner tubes
        foot = Mesh()
        foot.extend(lathe([(0.0, -0.035), (0.028, -0.035), (0.03, 0.0), (0.024, 0.03), (0.0, 0.03)], seg=28,
                          axis=(0, 0, 1), centre=(0.0, 0.0, z)))           # axle clamp
        foot.extend(tube(np.linspace(a, a + FORK_DIR * 0.07, 4), 0.026, 28))   # the foot the tube sits in
        lower.extend(foot.masked(1.0))
    put("fork_lowers", lower, "fork_inner", "front_lower")
    calipers = Mesh()
    for z in (-0.085, 0.085):
        c = np.array([-0.12, 0.08, z])
        calipers.extend(box(c + FORK_BACK * 0.0, (0.05, 0.11, 0.035), 4.0))
    put("front_calipers", calipers.transformed(rot_z(-0.5)), "black_engine", "front_lower")
    a = np.linspace(math.radians(125), math.radians(30), 20)
    rg = 0.325
    guard_path = np.stack([rg * np.cos(a), rg * np.sin(a), np.zeros_like(a)], 1)
    put("front_mudguard", sweep(guard_path, 0.075, 0.006, 4.0, 20, caps=True, up_hint=(1, 0, 0)), "black_gloss", "front_lower")

    # Steering: stanchions, yokes, bars, clocks, headlight, mirrors (body origin).
    steer = Mesh()
    for z in (-0.095, 0.095):
        a = FRONT_AXLE + np.array([0, 0, z]) + FORK_DIR * 0.25
        b = FRONT_AXLE + np.array([0, 0, z]) + FORK_DIR * 0.69
        steer.extend(tube(np.linspace(a, b, 6), 0.029, 28))      # thick outer tubes, clamped in the yokes
    put("fork_stanchions", steer, "black_satin", "steer")
    yokes = Mesh()
    for along in (0.47, 0.64):
        c = FRONT_AXLE + FORK_DIR * along + FORK_BACK * 0.015
        yokes.extend(box(c, (0.07, 0.028, 0.25), 5.0).transformed())
    put("yokes", yokes, "alu_cast", "steer")
    top = FRONT_AXLE + FORK_DIR * 0.66
    bar_clamp = top + np.array([-0.035, 0.04, 0.0])
    bar_clamp_pt = bar_clamp
    bars = polyline([bar_clamp + [0.0, 0, -0.38], bar_clamp + [-0.03, 0.03, -0.30], bar_clamp + [0.0, 0.025, -0.12],
                     bar_clamp, bar_clamp + [0.0, 0.025, 0.12], bar_clamp + [-0.03, 0.03, 0.30],
                     bar_clamp + [0.0, 0, 0.38]], 6)
    put("handlebar", tube(bars, 0.011, 12), "black_satin", "steer")
    grips = Mesh(); switches = Mesh()
    for s in (-1, 1):
        g0 = bar_clamp + [-0.005, 0.012, s * 0.255]; g1 = bar_clamp + [0.0, 0.002, s * 0.375]
        grips.extend(tube(np.linspace(g0, g1, 4), 0.017, 20))
        sw = bar_clamp + [0.0, 0.015, s * 0.225]
        switches.extend(box(sw, (0.05, 0.045, 0.045), 4.0))
    put("grips", grips, "grip", "steer")
    put("grips_switches", switches, "black_engine", "steer")
    levers = Mesh()
    for s in (-1, 1):
        lp = polyline([bar_clamp + [0.02, 0.01, s * 0.23], bar_clamp + [0.07, 0.0, s * 0.27],
                       bar_clamp + [0.07, -0.01, s * 0.36]], 6)
        levers.extend(sweep(lp, 0.006, 0.01, 3.0, 10))
    put("levers", levers, "alu_cast", "steer")
    clock_c = bar_clamp + np.array([0.03, 0.065, 0.0])
    clock_axis = np.array([-0.5, 0.86, 0.0])
    put("clock_body", lathe([(0.0, -0.035), (0.05, -0.035), (0.055, -0.01), (0.056, 0.02), (0.05, 0.025),
                             (0.0, 0.025)], seg=40, axis=clock_axis, centre=clock_c), "black_engine", "steer")
    put("clock_face", lathe([(0.0, 0.026), (0.047, 0.026)], seg=40, axis=clock_axis, centre=clock_c), "lens_clear", "steer")
    hl_c = FRONT_AXLE + FORK_DIR * 0.53 + np.array([0.075, 0.0, 0.0])
    shell = lathe([(0.0, -0.06), (0.05, -0.058), (0.08, -0.03), (0.088, 0.0), (0.0895, 0.012), (0.091, 0.02),
                   (0.091, 0.03), (0.084, 0.034), (0.0, 0.03)], seg=64, axis=(1, 0, 0), centre=hl_c)
    put("headlight_shell", shell.mask_by(lambda p: (p[:, 0] - hl_c[0] - 0.006) / 0.006), "headlight", "steer")
    put("headlight_lens", lathe([(0.0, 0.05), (0.06, 0.045), (0.084, 0.032)], seg=48, axis=(1, 0, 0), centre=hl_c),
        "lens_clear", "steer")
    mirrors = Mesh(); stalks = Mesh()
    for s in (-1, 1):
        end = bar_clamp + [0.0, 0.0, s * 0.385]
        stalk = polyline([end, end + [0.0, 0.03, s * 0.02], end + [0.02, 0.1, s * 0.05]], 6)
        stalks.extend(tube(stalk, 0.006, 10))
        mc = end + np.array([0.03, 0.16, s * 0.06])
        mirrors.extend(lathe([(0.0, -0.008), (0.045, -0.008), (0.048, 0.0), (0.045, 0.006), (0.0, 0.006)], seg=32,
                             axis=(1, 0.1, 0), centre=mc).transformed())
    put("mirrors", mirrors, "black_gloss", "steer")
    put("mirror_stalks", stalks, "black_satin", "steer")
    ind = Mesh()
    for s in (-1, 1):
        base = hl_c + np.array([-0.04, -0.01, s * 0.11])
        ind.extend(tube(np.linspace(base, base + [0.0, 0.0, s * 0.06], 4), 0.007, 10))
        ind.extend(lathe([(0.0, 0.0), (0.017, 0.012), (0.012, 0.05), (0.0, 0.055)], seg=16, axis=(1, 0, 0),
                         centre=base + [0.0, 0.0, s * 0.07]))
    put("front_indicators", ind, "lens_amber", "steer")

    # Tank: a lofted superellipse, highest just behind its front, tucked in at the knees.
    xs = np.linspace(-0.135, 0.37, 110)
    t = (xs - xs[0]) / (xs[-1] - xs[0])
    top = 0.505 + 0.115 * np.sin(np.clip(t * 1.25, 0, 1) * math.pi * 0.62) - 0.06 * np.clip(t - 0.8, 0, 1) / 0.2
    bot = 0.43 - 0.02 * t
    hw = 0.06 + 0.115 * np.sin(np.clip(t, 0, 1) * math.pi) ** 0.6
    hw = np.where(t < 0.05, 0.07, hw)
    path = np.stack([xs, (top + bot) / 2, np.zeros_like(xs)], 1)
    tank = sweep(path, hw, (top - bot) / 2, 2.6, 384, caps=True)

    def pinstripe(p):
        # Two fine black lines along each side, straight in side view, as on beenster's bike
        # (measured off the drone profile): the upper one under the "1200", rising to the badge, and
        # a lower one that dips a little towards the front.
        x, y = p[:, 0], p[:, 1]

        def line(y0, slope, x0, x1, half=0.0018):
            d = np.abs(y - (y0 + slope * x)) - half
            ends = np.clip((x - x0) / 0.02, 0, 1) * np.clip((x1 - x) / 0.02, 0, 1)
            return np.clip(1 - d / 0.0015, 0, 1) * ends
        side = np.abs(p[:, 2]) > 0.05
        return np.maximum(line(0.484, 0.0465, -0.12, 0.335), line(0.450, -0.027, -0.10, 0.30)) * side
    put("tank", tank.mask_by(pinstripe), "paint_silver")
    # A chrome Triumph badge high on each side of the tank, towards the front.
    badges = Mesh()
    for s_ in (-1, 1):
        bt = (0.25 - xs[0]) / (xs[-1] - xs[0])
        bw = 0.06 + 0.115 * math.sin(min(max(bt, 0), 1) * math.pi) ** 0.6
        badges.extend(lathe([(0.0, 0.0), (0.03, 0.0), (0.032, 0.003), (0.0, 0.005)], seg=32, axis=(0, 0.3, s_),
                            centre=(0.25, 0.56, s_ * (bw * 0.93))))
    put("tank_badges", badges, "chrome")
    put("tank_cap", lathe([(0.0, 0.0), (0.04, 0.0), (0.042, 0.012), (0.0, 0.016)], seg=28, axis=(0.15, 1, 0),
                          centre=(0.12, 0.618, 0.0)), "alu_cast")

    # Seat: flat bench from the tank back to the tail.
    xs = np.linspace(-0.95, -0.11, 40)
    t = (xs - xs[0]) / (xs[-1] - xs[0])
    top = 0.515 + 0.012 * np.sin(t * math.pi) - 0.02 * np.clip((t - 0.85) / 0.15, 0, 1)
    bot = 0.425 + 0.02 * t
    hw = 0.10 + 0.045 * np.sin(np.clip(t * 1.3, 0, 1) * math.pi * 0.5)
    seat = sweep(np.stack([xs, (top + bot) / 2, 0 * xs], 1), hw, (top - bot) / 2, 4.0, 96)
    for s_ in (-1, 1):     # the welt seam round the top panel
        k = slice(1, -1)
        seat.extend(tube(np.stack([xs[k], top[k] - 0.012, s_ * hw[k] * 0.97], 1), 0.0028, 10))
    put("seat", seat, "leather")
    put("seat_pan", sweep(np.stack([xs, bot + 0.004, 0 * xs], 1), hw * 0.95, 0.008, 4.0, 20), "black_satin")

    # Tail, rear light and number plate hanger.
    put("tail", box((-0.90, 0.44, 0.0), (0.12, 0.05, 0.16), 5.0), "black_gloss")
    put("rear_light", lathe([(0.0, 0.0), (0.024, 0.0), (0.024, 0.02), (0.0, 0.03)], seg=24, axis=(-1, 0, 0),
                            centre=(-0.945, 0.485, 0.0)), "lens_red")
    hanger = polyline([(-0.88, 0.42, 0.0), (-0.96, 0.4, 0.0), (-1.02, 0.31, 0.0)], 6)
    put("plate_hanger", sweep(hanger, 0.09, 0.006, 6.0, 16, up_hint=(1, 1, 0)), "black_satin")
    put("plate", sweep(np.array([(-1.0, 0.35, 0.0), (-1.06, 0.23, 0.0)]), 0.1, 0.002, 8.0, 16, up_hint=(1, 0.5, 0)),
        "lens_amber")

    # Side panels and the battery box behind them.
    for s, nm in ((1, "side_panel_r"), (-1, "side_panel_l")):
        xs = np.linspace(-0.50, -0.22, 10)
        t = (xs - xs[0]) / (xs[-1] - xs[0])
        topy = 0.44 - 0.01 * t; boty = 0.26 + 0.03 * t
        put(nm, sweep(np.stack([xs, (topy + boty) / 2, np.full_like(xs, s * 0.13)], 1), 0.018, (topy - boty) / 2,
                      6.0, 20), "black_gloss")
    put("battery_box", box((-0.36, 0.34, 0.0), (0.26, 0.17, 0.22), 6.0), "black_satin")

    # Engine: 270 degree parallel twin, upright finned cylinders, wide black cases.
    put("crankcase", box((-0.05, 0.075, 0.0), (0.56, 0.3, 0.36), 5.0), "black_engine")
    put("sump", box((0.03, -0.085, 0.0), (0.36, 0.07, 0.30), 5.0), "black_engine")
    put("gearbox", box((-0.27, 0.13, 0.0), (0.2, 0.2, 0.3), 5.0), "black_engine")
    fins = Mesh()
    cyl_c = np.array([0.035, 0.205, 0.0])
    # Thirteen fine fins on the barrels, a few deeper ones round the head.
    for k in range(13):
        y = cyl_c[1] - 0.08 + k * 0.0125
        depth = 0.16 - 0.002 * abs(k - 6) + (0.012 if k >= 10 else 0.0)
        fin = box((cyl_c[0], y, 0.0), (depth, 0.0035, 0.40), 8.0, 24)
        # Black barrels with their fin edges machined bright.
        fins.extend(fin.mask_by(lambda p, d=depth: np.maximum(np.abs(p[:, 0] - cyl_c[0]) - (d / 2 - 0.012),
                                                              np.abs(p[:, 2]) - 0.188) / 0.006))
    fins.extend(box(tuple(cyl_c), (0.11, 0.17, 0.34), 4.0))
    put("cylinders", fins.transformed(rot_z(math.radians(-6))), "black_engine")
    put("cam_cover", box((0.03, 0.375, 0.0), (0.13, 0.05, 0.32), 5.0).transformed(rot_z(math.radians(-6))),
        "black_engine")
    covers = Mesh()
    covers.extend(lathe([(0.0, 0.18), (0.085, 0.18), (0.09, 0.2), (0.0, 0.205)], seg=40, axis=(0, 0, 1),
                        centre=(0.13, -0.035, 0.0)))
    covers.extend(lathe([(0.0, -0.18), (0.075, -0.18), (0.08, -0.2), (0.0, -0.205)], seg=40, axis=(0, 0, -1),
                        centre=(0.10, -0.02, 0.0)))
    put("engine_covers", covers, "black_gloss")
    put("cover_badges", Mesh().extend(lathe([(0.0, 0.206), (0.03, 0.206)], seg=24, centre=(0.135, 0.0, 0.0)))
        .extend(lathe([(0.0, 0.0), (0.02, 0.0), (0.022, 0.01), (0.0, 0.012)], seg=24, axis=(0, 0, 1),
                      centre=(-0.02, 0.36, 0.17))), "alu_brushed")
    # Bolts round the clutch and alternator covers and along the cam cover.
    bolts = Mesh()
    for (cx, cy, cz, sgn, r, n) in ((0.13, -0.035, 0.192, 1, 0.077, 9), (0.10, -0.02, -0.192, -1, 0.068, 8)):
        for k in range(n):
            a = 2 * math.pi * (k + 0.25) / n
            bolts.extend(hex_bolt((cx + r * math.cos(a), cy + r * math.sin(a), cz), (0, 0, sgn)))
    for z in (-0.12, -0.04, 0.04, 0.12):
        bolts.extend(hex_bolt((0.03, 0.40, z), (0.1, 1, 0), 0.006).transformed(rot_z(math.radians(-6))))
    put("bolts", bolts, "bolt")
    put("throttle_bodies", both_sides(Mesh().extend(lathe([(0.025, 0.0), (0.03, 0.07), (0.028, 0.1)], seg=20,
                                                            axis=(-1, 0.2, 0.3), centre=(-0.07, 0.33, 0.11)))),
        "alu_cast")

    # Exhaust: a copper-heat header from each cylinder sweeping down past the cases, into a
    # megaphone silencer on each side.
    header = polyline([(0.13, 0.30, 0.10), (0.21, 0.30, 0.125), (0.33, 0.25, 0.15), (0.375, 0.12, 0.16),
                       (0.345, -0.04, 0.155), (0.22, -0.07, 0.15), (-0.05, -0.065, 0.15), (-0.22, -0.05, 0.155)], 8)
    # Heat tint: gold and bronze for the first hand-span from each port, fading into bare steel.
    put("headers", both_sides(tube(header, 0.022, 24).mask_by_uv(lambda uv: 1.0 - (uv[:, 1] - 0.12) / 0.3)), "header")
    s_path = np.linspace([-0.22, -0.05, 0.16], [-0.76, 0.16, 0.175], 12)
    radii = np.linspace(0.035, 0.064, 12)
    sil = sweep(s_path, radii, radii, 2.0, 32, caps=False)
    put("silencers", both_sides(sil), "steel_brushed")
    end_cap = lathe([(0.064, 0.0), (0.066, 0.04), (0.05, 0.05), (0.0, 0.052)], seg=32,
                    axis=s_path[-1] - s_path[-2], centre=s_path[-1])
    put("silencer_caps", both_sides(end_cap), "black_satin")

    # Frame: twin tube cradle, backbone, rear frame, swingarm mounts.
    frame = Mesh()
    head_top = STEER_POINT + FORK_DIR * 0.66 + FORK_BACK * 0.0
    head_bot = STEER_POINT + FORK_DIR * 0.46
    frame.extend(tube(np.linspace(head_bot, head_top, 5), 0.03, 18))
    back = polyline([head_top + [-0.02, -0.01, 0], (0.15, 0.46, 0.0), (-0.12, 0.42, 0.0), (-0.32, 0.40, 0.0)], 8)
    frame.extend(tube(back, 0.024, 14))
    for s in (-1, 1):
        down = polyline([head_bot + [0.0, 0.0, s * 0.02], (0.30, 0.25, s * 0.07), (0.27, -0.03, s * 0.10),
                         (0.10, -0.11, s * 0.11), (-0.20, -0.10, s * 0.11), (-0.33, 0.02, s * 0.11),
                         (-0.34, 0.20, s * 0.11), (-0.32, 0.40, s * 0.10)], 8)
        frame.extend(tube(down, 0.017, 12))
        rear = polyline([(-0.32, 0.40, s * 0.10), (-0.62, 0.43, s * 0.09), (-0.88, 0.43, s * 0.07)], 8)
        frame.extend(tube(rear, 0.014, 12))
        stay = polyline([(-0.34, 0.20, s * 0.11), (-0.52, 0.33, s * 0.10), (-0.62, 0.43, s * 0.09)], 8)
        frame.extend(tube(stay, 0.012, 12))
    put("frame", frame, "black_gloss")
    pegs = Mesh()
    for s in (-1, 1):
        pegs.extend(box((-0.43, 0.15, s * 0.165), (0.16, 0.12, 0.01), 4.0).transformed(rot_x(0)))
        pegs.extend(tube(np.linspace([-0.44, 0.165, s * 0.17], [-0.44, 0.165, s * 0.255], 4), 0.012, 12))
        pegs.extend(tube(np.linspace([-0.65, 0.20, s * 0.13], [-0.65, 0.20, s * 0.20], 4), 0.01, 10))
    put("footrests", pegs, "alu_cast")
    shocks = Mesh(); springs = Mesh()
    top_m = np.array([-0.38, 0.425, 0.0]); bot_m = np.array([-0.635, 0.17, 0.0])
    for s in (-1, 1):
        a = top_m + [0, 0, s * 0.125]; b = bot_m + [0, 0, s * 0.125]
        shocks.extend(tube(np.linspace(a, a + (b - a) * 0.35, 4), 0.026, 16))
        shocks.extend(tube(np.linspace(a + (b - a) * 0.3, b, 5), 0.012, 12))
        d = (b - a) / np.linalg.norm(b - a)
        u = np.cross(d, [0, 0, 1.0]); u /= np.linalg.norm(u); v = np.cross(d, u)
        tt = np.linspace(0.32, 0.92, 160)
        helix = a[None] + (b - a)[None] * tt[:, None] + 0.03 * (np.cos(tt * 70)[:, None] * u + np.sin(tt * 70)[:, None] * v)
        springs.extend(tube(helix, 0.0045, 8))
    put("shock_bodies", shocks, "black_satin")
    put("shock_springs", springs, "black_gloss")
    put("shock_tops", both_sides(lathe([(0.03, 0.0), (0.032, 0.05)], seg=20, axis=(0.6, -0.8, 0.0),
                                       centre=top_m + [0.0, 0.0, 0.125])), "alu_cast")

    # Swingarm (origin at its pivot) and the chain run.
    arm = Mesh()
    rel = REAR_AXLE - SWING_PIVOT
    for s in (-1, 1):
        path = np.linspace([0.0, 0.0, s * 0.105], [rel[0] - 0.02, rel[1], s * 0.11], 10)
        arm.extend(sweep(path, 0.018, np.linspace(0.04, 0.026, 10), 5.0, 18))
    arm.extend(tube(np.linspace([0, 0, -0.12], [0, 0, 0.12], 4), 0.025, 16))
    arm.extend(box((-0.06, 0.0, 0.0), (0.05, 0.05, 0.2), 5.0))
    put("swingarm", arm, "black_satin", "swingarm")
    front_sp = np.array([0.03, 0.03, 0.075]); rear_sp = np.array([rel[0], rel[1], 0.075])
    # The chain, link by link (525 pitch): top run, round the back of the sprocket, bottom run.
    arc = np.linspace(math.pi / 2, 3 * math.pi / 2, 24)
    run = np.vstack([np.linspace(front_sp + [0, 0.047, 0], rear_sp + [0, 0.104, 0], 30),
                     np.stack([rear_sp[0] + 0.104 * np.cos(arc), rear_sp[1] + 0.104 * np.sin(arc), np.full_like(arc, 0.075)], 1),
                     np.linspace(rear_sp + [0, -0.104, 0], front_sp + [0, -0.047, 0], 30)])
    seglen = np.linalg.norm(np.diff(run, axis=0), axis=1)
    dist = np.concatenate([[0.0], np.cumsum(seglen)])
    chain = Mesh()
    pitch = 0.015875
    for k, d0 in enumerate(np.arange(0.0, dist[-1] - pitch, pitch)):
        p0 = np.array([np.interp(d0, dist, run[:, i]) for i in range(3)])
        p1 = np.array([np.interp(d0 + pitch, dist, run[:, i]) for i in range(3)])
        inner = 0.0045 if k % 2 else 0.0075        # inner and outer plates alternate
        for side in (-1, 1):
            off = np.array([0, 0, side * inner])
            chain.extend(sweep(np.linspace(p0 + off, p1 + off, 3), 0.0011, 0.0075, 6.0, 8, up_hint=(0, 0, 1)))
        chain.extend(lathe([(0.0, -0.0045), (0.005, -0.0045), (0.005, 0.0045), (0.0, 0.0045)], seg=10, centre=p0))
    put("chain", chain, "chain", "swingarm")
    guard = Mesh()
    guard.extend(sweep(np.linspace(front_sp + [0.02, 0.135, 0.0], rear_sp + [0.12, 0.125, 0.0], 8), 0.012, 0.004, 6.0, 16))
    put("chain_guard", guard, "black_gloss", "swingarm")
    # Hugger over the rear tyre, off the swingarm.
    a = np.linspace(math.radians(30), math.radians(110), 16)
    hug = np.stack([rel[0] + 0.335 * np.cos(a), rel[1] + 0.335 * np.sin(a), np.zeros_like(a)], 1)
    put("hugger", sweep(hug, 0.085, 0.004, 4.0, 20, up_hint=(1, 0, 0)), "black_gloss", "swingarm")

    # Radiator between the down tubes, low and ahead of the barrels.
    rad = Mesh()
    rc = np.array([0.24, 0.13, 0.0])
    rad.extend(box(tuple(rc), (0.04, 0.2, 0.26), 8.0))
    for k in range(16):                       # the core's slats, seen through the frame
        rad.extend(box((rc[0] + 0.021, rc[1] - 0.09 + k * 0.012, 0.0), (0.004, 0.0025, 0.24), 8.0, 12))
    put("radiator", rad.transformed(rot_z(math.radians(-12))), "black_satin")
    hoses = Mesh()
    for s_ in (-1, 1):
        hoses.extend(tube(polyline([(0.22, 0.22, s_ * 0.08), (0.17, 0.27, s_ * 0.08), (0.10, 0.30, s_ * 0.06)], 6), 0.011, 14))
    hoses.extend(tube(polyline([(0.25, 0.04, 0.12), (0.18, -0.02, 0.15), (0.10, 0.0, 0.17)], 6), 0.011, 14))
    put("coolant_hoses", hoses, "rubber")

    # Cables and brake lines: they make a bike read as real.
    lines = Mesh()
    lever_l = bar_clamp_pt + np.array([0.02, 0.0, -0.22]); lever_r = bar_clamp_pt + np.array([0.02, 0.0, 0.22])
    for z in (-0.085, 0.085):                 # front brake hoses, master cylinder to each caliper
        cal = FRONT_AXLE + np.array([-0.06, 0.07, z])
        lines.extend(tube(polyline([lever_r, lever_r + [0.06, -0.12, -0.12 - (0.0 if z > 0 else 0.06)],
                                    FRONT_AXLE + FORK_DIR * 0.45 + FORK_BACK * -0.04 + [0, 0, z * 1.25],
                                    FRONT_AXLE + FORK_DIR * 0.2 + FORK_BACK * -0.04 + [0, 0, z * 1.3], cal], 8), 0.004, 10))
    lines.extend(tube(polyline([lever_l, lever_l + [0.05, -0.1, 0.05], (0.33, 0.33, -0.09), (0.2, 0.28, -0.17),
                                (0.0, 0.15, -0.19)], 8), 0.0035, 10))     # clutch cable
    lines.extend(tube(polyline([lever_r + [0.0, 0.0, 0.08], lever_r + [0.05, -0.1, 0.0], (0.30, 0.35, 0.06),
                                (0.0, 0.36, 0.08), (-0.07, 0.34, 0.1)], 8), 0.003, 10))   # throttle
    put("cables", lines, "rubber", "steer")
    rear_line = tube(polyline([(-0.40, 0.12, 0.17), (-0.48, 0.10, 0.15), (-0.62, 0.05, 0.13), (-0.70, 0.02, 0.12)], 8), 0.004, 10)
    put("rear_brake_line", rear_line, "rubber")
    calr = lathe([(0.0, -0.02), (0.03, -0.02), (0.03, 0.02), (0.0, 0.02)], seg=20, axis=(0, 0, 1),
                 centre=(REAR_AXLE[0] + 0.03, REAR_AXLE[1] + 0.09, -0.07))
    put("rear_caliper", box((REAR_AXLE[0] + 0.02, REAR_AXLE[1] + 0.085, -0.075), (0.07, 0.045, 0.03), 4.0).extend(calr),
        "black_engine")

    # Rear indicators on stalks beside the plate, a side stand tucked up, the horn.
    rind = Mesh()
    for s_ in (-1, 1):
        base = np.array([-0.93, 0.40, s_ * 0.06])
        rind.extend(tube(np.linspace(base, base + [0.0, 0.0, s_ * 0.06], 4), 0.006, 10))
        rind.extend(lathe([(0.0, 0.0), (0.016, 0.01), (0.012, 0.045), (0.0, 0.05)], seg=16, axis=(-1, 0, 0),
                          centre=base + [0.0, 0.0, s_ * 0.07]))
    put("rear_indicators", rind, "lens_amber")
    put("side_stand", tube(polyline([(-0.20, -0.08, -0.13), (-0.38, -0.12, -0.15), (-0.52, -0.13, -0.15)], 6), 0.012, 12)
        .extend(box((-0.53, -0.135, -0.15), (0.05, 0.01, 0.035), 4.0)), "black_satin")
    put("horn", lathe([(0.0, 0.0), (0.035, 0.0), (0.035, 0.02), (0.0, 0.025)], seg=24, axis=(1, 0, 0),
                      centre=(0.30, 0.38, 0.0)), "black_satin")
    return P


# --- Output -------------------------------------------------------------------------------------------

def smooth_normals(pos, tris):
    """Vertex normals averaged over the faces round each position (the doubled seam vertices that
    let textures wrap share theirs)."""
    key = np.round(pos * 1e6).astype(np.int64)
    _, inv = np.unique(key, axis=0, return_inverse=True)
    inv = inv.ravel()
    fn = np.cross(pos[tris[:, 1]] - pos[tris[:, 0]], pos[tris[:, 2]] - pos[tris[:, 0]])
    n = np.zeros((inv.max() + 1, 3))
    for k in range(3):
        np.add.at(n, inv[tris[:, k]], fn)
    n = n[inv]
    ln = np.linalg.norm(n, axis=1, keepdims=True)
    return n / np.maximum(ln, 1e-12)


def outward(mesh):
    """Each piece turned so its faces point outwards (positive signed volume about its own centre).
    Flat pieces have no inside; the bike's materials are two sided for them."""
    out = Mesh()
    for p, t, uv, k in zip(mesh.p, mesh.t, mesh.uv, mesh.m):
        c = p.mean(0)
        a, b, d = p[t[:, 0]] - c, p[t[:, 1]] - c, p[t[:, 2]] - c
        vol = np.einsum("ij,ij->i", a, np.cross(b, d)).sum()
        out.add(p, t[:, [0, 2, 1]] if vol < 0 else t, uv, k)
    return out


def normals(mesh):
    """Smooth normals piece by piece (pieces that touch keep their own)."""
    return np.concatenate([smooth_normals(p, t) for p, t in zip(mesh.p, mesh.t)])


def write_parts(P, out):
    os.makedirs(out, exist_ok=True)
    origins = {
        "body": [0, 0, 0], "steer": [0, 0, 0], "front_lower": FRONT_AXLE.tolist(),
        "front_wheel": FRONT_AXLE.tolist(), "swingarm": SWING_PIVOT.tolist(), "rear_wheel": REAR_AXLE.tolist(),
    }
    for f in os.listdir(out) if os.path.isdir(out) else []:
        if f.endswith(".glb") and f[:-4] not in P:
            os.remove(os.path.join(out, f))   # a part that's gone or renamed
    listing = []
    for name, (mesh, fin, group) in sorted(P.items()):
        mesh = outward(mesh)
        pos, tris, uv, mask = mesh.arrays()
        col = np.zeros((len(pos), 4), np.uint8)
        col[:, 0] = np.round(np.clip(mask, 0, 1) * 255)
        col[:, 3] = 255
        write_glb(os.path.join(out, name + ".glb"), pos, tris, normals(mesh), col, name, uv=uv)
        listing.append({"name": name, "group": group, "finish": fin, "tris": int(len(tris))})
    info = {
        "bike": "2025 Triumph Speed Twin 1200",
        "frame": "metres, x forward, y up, z right; body origin at axle height (suspension extended), mid wheelbase",
        "group_origins": origins,
        "steer_axis": {"point": STEER_POINT.tolist(), "dir": FORK_DIR.tolist()},
        "swing_pivot": SWING_PIVOT.tolist(),
        "uv": "TEXCOORD_0 in metres (u round, v along) except tyres (24 repeats round, 0..1 bead to bead); "
              "COLOR_0 red is the mask towards each finish's second finish",
        "finishes": FINISHES,
        "parts": listing,
    }
    json.dump(info, open(os.path.join(out, "speedtwin.json"), "w"), indent=1)
    return info


def place(P):
    """Every part in body space, for the check render."""
    off = {"front_lower": FRONT_AXLE, "front_wheel": FRONT_AXLE, "swingarm": SWING_PIVOT, "rear_wheel": REAR_AXLE}
    out = []
    for name, (mesh, fin, group) in P.items():
        pos, tris, uv, mask = outward(mesh).arrays()
        out.append((name, pos + off.get(group, 0), tris, fin, mask))
    return out


def check_render(P, photo, path, px_per_m=500.0, origin_px=(818, 597), view="right"):
    from PIL import Image, ImageDraw
    im = Image.open(photo).convert("RGB")
    im.thumbnail((1400, 1400))
    over = Image.new("RGB", im.size, (255, 255, 255))
    d = ImageDraw.Draw(over)
    polys = []
    light = np.array([0.3, 0.8, 0.5]); light /= np.linalg.norm(light)
    for name, pos, tris, fin, mask in place(P):
        c1 = np.array(FINISHES[fin]["color"]) ** (1 / 2.2); c2 = np.array(FINISHES[fin]["color2"]) ** (1 / 2.2)
        fn = np.cross(pos[tris[:, 1]] - pos[tris[:, 0]], pos[tris[:, 2]] - pos[tris[:, 0]])
        ln = np.linalg.norm(fn, axis=1); ok = ln > 1e-12
        fn[ok] /= ln[ok, None]
        sgn = 1 if view == "right" else -1
        for k in np.nonzero(ok)[0]:
            tri = pos[tris[k]]
            depth = tri[:, 2].mean() * sgn
            shade = 0.35 + 0.65 * abs(np.dot(fn[k], light))
            col = c1 + (c2 - c1) * mask[tris[k]].mean()
            c = tuple(int(255 * min(1, v * shade + 0.05)) for v in col)
            xy = [(origin_px[0] + sgn * p[0] * px_per_m, origin_px[1] - p[1] * px_per_m) for p in tri]
            polys.append((depth, xy, c))
    polys.sort(key=lambda q: q[0])
    mask = Image.new("L", im.size, 0); dm = ImageDraw.Draw(mask)
    for _, xy, c in polys:
        d.polygon(xy, fill=c); dm.polygon(xy, fill=255)
    blend = Image.blend(im, over, 0.6)
    res = Image.composite(blend, im, mask)
    side = Image.new("RGB", (im.size[0] * 2, im.size[1]))
    side.paste(res, (0, 0)); side.paste(Image.composite(over, Image.new("RGB", im.size, (235, 235, 235)), mask), (im.size[0], 0))
    side.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "Data", "Bike", "SpeedTwin"))
    ap.add_argument("--check", help="a right-side photo to render the model over")
    ap.add_argument("--check-out", default="speedtwin_check.png")
    args = ap.parse_args()
    P = build()
    info = write_parts(P, args.out)
    print("%d parts, %d triangles" % (len(info["parts"]), sum(p["tris"] for p in info["parts"])))
    if args.check:
        check_render(P, args.check, args.check_out)


if __name__ == "__main__":
    main()
