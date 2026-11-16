"""Common parts, stage 1: the handlebar, grip and bar end library (common parts plan, 16 Nov).

Parts no brand owns, made to fit any Bikes V2 bike through the joints in docs/common_parts_joints.md:
a bar clamps at the bike's riser point `rz` (or, for clip-ons, round the fork tube under the top yoke),
is 22.2 mm through the grips, and runs dead straight from y = +-170 out to its end, so the levers,
switches and grips sit on it the way they sit on the signed-off naked. A bar is a recipe: where the
hand ends up (rise and pullback from the clamp, how far out), how the grip end is swept back and up,
how steeply the bar climbs out of the clamp, and whether it is a fat bar or carries a brace.

Our own designs with real parts as reference only: no maker's name, logo or exact product ships.

    python3 foundry/bars3d.py parts                     # one close-up per bar, grip and bar end: out/bars/*.png
    python3 foundry/bars3d.py fit corvano_naked tracker [--grip ribbed --end tapered --finish polished]
    python3 foundry/bars3d.py fitted                    # each bar on a bike it suits: out/bars/fit_<bar>.png
    python3 foundry/bars3d.py check                     # hand points on every model + lock and clipping on one bike per family
"""
import json
import math
import os
import re
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import detail3d as D  # noqa: E402

B = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "bars")

BAR_R = 11.1          # 22.2 mm (7/8 in) bar
FAT_R = 14.3          # 28.6 mm (1 1/8 in) fat bar, between the clamps
GRIP_L = 126.0        # grip length along the bar
CTRL = 88.0           # straight bar inboard of the grip for the switch pod and the lever perch
HAND_IN = 63.0        # the hand point is the middle of the grip

# A bar: where the middle of the grip ends up from the clamp (rise up, pullback back, y out), the grip's
# back sweep and up sweep (degrees), and how the bar leaves the straight centre section: kick = climb
# angle out of the clamp, kb = how much it already turns back there. fat: 28.6 mm between the clamps.
# brace: a cross brace with a pad. clip: a clip-on (drop below the top yoke's underside, angle down, raise).
BARS = {
    "street": dict(label="Flat street bar", note="Fat tapered bar, low and flat: the modern naked.", rise=46, back=50, y=321, sweep=7, up=4, kick=34, kb=12, fat=True, finish="satin_black", families="Naked, Classic, Adventure"),
    "lowrise": dict(label="Low rise bar", note="A 22 mm bar with a gentle climb and a little more sweep.", rise=56, back=58, y=318, sweep=9, up=5, kick=40, kb=14, finish="satin_black"),
    "highrise": dict(label="High rise bar", note="Tall and wide, for an upright seat: adventure and scrambler.", rise=92, back=64, y=330, sweep=8, up=6, kick=62, kb=10, fat=True, finish="titanium"),
    "mx": dict(label="Braced motocross bar", note="Tall bend with a cross brace and its pad.", rise=64, back=56, y=336, sweep=8, up=7, kick=60, kb=8, brace=True, finish="anodised_red"),
    "clipon_low": dict(label="Clip-ons, low", note="Under the top yoke and angled well down: racing crouch.", clip=dict(drop=42, angle=11, raise_=0), back=72, y=318, sweep=14, finish="satin_black"),
    "clipon_raised": dict(label="Clip-ons, raised", note="On a short post above the clamp, nearly flat: a road sports reach.", clip=dict(drop=30, angle=4, raise_=34), back=60, y=322, sweep=12, finish="anodised_gold"),
    "drag": dict(label="Drag bar", note="Almost straight, flat across: a low, stretched reach.", rise=24, back=40, y=330, sweep=4, up=0, kick=8, kb=4, finish="chrome"),
    "touring": dict(label="Swept touring bar", note="Pulled well back to the rider: cruiser and tourer.", rise=22, back=178, y=318, sweep=20, up=4, kick=36, kb=22, finish="chrome"),
    "tracker": dict(label="Tracker bar", note="Wide with a mid rise and flat ends: flat-track style.", rise=72, back=74, y=348, sweep=10, up=3, kick=50, kb=12, finish="polished"),
    "ace": dict(label="Ace bar", note="Drops forward and down from the yoke: the classic cafe stance without clip-ons.", rise=-58, back=100, y=312, sweep=14, up=-5, kick=-30, kb=24, finish="chrome"),
}

FINISH = {
    "satin_black": dict(col=(0.03, 0.03, 0.034), metallic=0.85, rough=0.38, label="satin black"),
    "polished": dict(col=(0.8, 0.8, 0.82), metallic=1.0, rough=0.12, label="polished alloy"),
    "chrome": dict(col=(0.88, 0.88, 0.9), metallic=1.0, rough=0.05, label="chrome"),
    "brushed": dict(col=(0.62, 0.63, 0.65), metallic=1.0, rough=0.3, label="brushed alloy"),
    "titanium": dict(col=(0.28, 0.27, 0.26), metallic=1.0, rough=0.3, label="titanium anodised"),
    "anodised_gold": dict(col=(0.72, 0.46, 0.08), metallic=1.0, rough=0.24, label="gold anodised"),
    "anodised_red": dict(col=(0.52, 0.03, 0.03), metallic=1.0, rough=0.26, label="red anodised"),
    "anodised_blue": dict(col=(0.04, 0.14, 0.5), metallic=1.0, rough=0.26, label="blue anodised"),
}

GRIPS = {
    "knurl": dict(label="Diamond knurl", note="The signed-off naked grip: full diamond knurl, slots, big flange."),
    "ribbed": dict(label="Ribbed", note="Deep round ribs all along, rounded end collar: road and classic."),
    "waffle": dict(label="Waffle", note="Soft square waffle, thin flange: motocross and adventure."),
    "barrel": dict(label="Barrel", note="Fuller in the middle with long flutes and ringed ends: old-school."),
    "touring": dict(label="Soft touring", note="Fat soft grip with a dimpled palm and a grey insert band."),
    "leather": dict(label="Leather wrapped", note="Brown leather wound round, stitched seam, polished alloy end rings: classics."),
}

ENDS = {
    "cap": dict(label="Plain cap", note="A rubber cap flush with the grip end."),
    "weighted": dict(label="Weighted", note="The signed-off naked bar end: an alloy weight with a cap bolt."),
    "tapered": dict(label="Tapered billet", note="A machined cone with turned grooves, anodised."),
    "ringed": dict(label="Machined ring", note="A stepped billet end with a knurled ring and a bright face."),
    "bullet": dict(label="Chrome bullet", note="A rounded chrome bullet: classic and cruiser."),
    "mirror": dict(label="Mirror end", note="A clamp on the bar end carrying a round mirror on a short stem."),
}


def use(b):
    global B
    B = b
    D.B = b


def finish(name):
    f = FINISH[name]
    return D.surf("bars_" + name, f["col"], f["metallic"], f["rough"], wear=0.06)


# ---------------------------------------------------------------- small vector helpers (tuples, mm)
def vadd(a, b, k=1.0):
    return tuple(a[i] + b[i] * k for i in range(3))


def vsub(a, b):
    return tuple(a[i] - b[i] for i in range(3))


def vlen(a):
    return math.sqrt(sum(x * x for x in a))


def vnorm(a):
    L_ = vlen(a)
    return tuple(x / L_ for x in a)


def grip_dir(st, s):
    """Unit direction of the grip (outwards) on side s: swept back and up."""
    b, u = math.radians(st.get("sweep", 6)), math.radians(st.get("up", 0))
    return vnorm((-math.tan(b), s * 1.0, math.tan(u)))


# ---------------------------------------------------------------- geometry
def sweep(name, pts, radii, material, segs=32, caps=True):
    """A tube along 3D points (mm) whose radius follows radii: rings carried along with parallel frames."""
    P = [Vector(p) for p in pts]
    n = len(P)
    T = []
    for i in range(n):
        a, b = P[max(0, i - 1)], P[min(n - 1, i + 1)]
        T.append((b - a).normalized())
    ref = Vector((0, 0, 1)) if abs(T[0].z) < 0.9 else Vector((1, 0, 0))
    N0 = (ref - T[0] * ref.dot(T[0])).normalized()
    Ns = [N0]
    for i in range(1, n):
        v = Ns[-1] - T[i] * Ns[-1].dot(T[i])
        Ns.append(v.normalized() if v.length > 1e-6 else Ns[-1])
    verts, faces = [], []
    for i in range(n):
        Bn = T[i].cross(Ns[i])
        for k in range(segs):
            w = 2 * math.pi * k / segs
            q = P[i] + (Ns[i] * math.cos(w) + Bn * math.sin(w)) * radii[i]
            verts.append(B.V(q.x, q.y, q.z))
    for i in range(n - 1):
        for k in range(segs):
            k2 = (k + 1) % segs
            faces.append((i * segs + k, i * segs + k2, (i + 1) * segs + k2, (i + 1) * segs + k))
    if caps:
        faces += [tuple(range(segs - 1, -1, -1)), tuple((n - 1) * segs + k for k in range(segs))]
    ob = B.mesh_obj(name, verts, faces, material, True, 0, False)
    B.bm_fix_normals(ob)
    return ob


def hermite(p0, m0, p1, m1, n):
    out = []
    for i in range(n + 1):
        t = i / n
        h00, h10, h01, h11 = 2 * t ** 3 - 3 * t ** 2 + 1, t ** 3 - 2 * t ** 2 + t, -2 * t ** 3 + 3 * t ** 2, t ** 3 - t ** 2
        out.append(tuple(h00 * p0[k] + h10 * m0[k] + h01 * p1[k] + h11 * m1[k] for k in range(3)))
    return out


def bar_half(st, C, s):
    """Centre line of one half of a yoke bar, from the middle out to the end, and its grip line.
    Returns (points, hand point, grip start g0, grip direction d, end point)."""
    d = grip_dir(st, s)
    H = (C[0] - st["back"], s * st["y"], C[2] + st["rise"])
    ky = d[1] * s
    g0 = vadd(H, d, -HAND_IN / 1.0)
    E = vadd(H, d, GRIP_L - HAND_IN)
    S = vadd(g0, d, -CTRL)
    p0 = (C[0], s * 75.0, C[2])
    kick, kb = math.radians(st.get("kick", 30)), math.radians(st.get("kb", 10))
    t0 = vnorm((-math.tan(kb), s * 1.0, math.tan(kick)))
    k = vlen(vsub(S, p0)) * 1.15
    curve = hermite(p0, tuple(x * k for x in t0), S, tuple(x * k for x in d), 40)
    straight = [vadd(S, d, (vlen(vsub(E, S))) * j / 12) for j in range(1, 13)]
    pts = [(C[0], 0.0, C[2])] + [(C[0], s * 37.5, C[2])] + curve + straight
    del ky
    return pts, H, g0, d, E


def clip_half(st, c, s):
    """A clip-on on side s: the clamp round the fork tube, its body, and a straight bar out from it."""
    F, uf, fk = c["F"], c["uf"], c["fk"]
    cl = st["clip"]
    t_hi = fk["length"] - 34 - 3                 # just under the top yoke's underside
    t_c = min(t_hi - cl["drop"] + 20, t_hi - 22)      # the clamp ring always stays under the top yoke
    ax = (F[0] + uf[0] * t_c, F[1] + uf[1] * t_c)
    centre = (ax[0], s * 105.0, ax[1])
    b, a = math.radians(st.get("sweep", 6)), math.radians(cl["angle"])
    d = vnorm((-math.tan(b), s * 1.0, -math.tan(a)))
    ry = 172.0 if cl["raise_"] else 146.0      # a raised body climbs outboard of the top yoke's edge
    root = (centre[0] - 40, s * ry, centre[2] + cl["raise_"])
    # the hand point: along the bar to y = st["y"]
    k = (st["y"] - ry) / d[1] / s
    H = vadd(root, d, k)
    g0 = vadd(H, d, -HAND_IN)
    E = vadd(H, d, GRIP_L - HAND_IN)
    return dict(centre=centre, t=(t_c - 20, t_c + 20), root=root, d=d, H=H, g0=g0, E=E)


def clamp_point(model, c=None, clip=False):
    """The bar clamp point (x, z) for a model: its riser point, or build3d's default on bikes without one."""
    fe = model.get("front_end", [])
    rz = next((tuple(s_["pts"][1]) for s_ in fe if s_.get("tube") and s_.get("cls") == "cast" and s_.get("part") != "head"), None)
    has = rz is not None and not any(s_.get("clipon") for s_ in fe)
    top = c["top"] if c else model_top(model)
    if not has:
        # no risers in the model: clip-on bikes take low clamps straight on the top yoke; the rest (MX today)
        # take build3d's own default clamp point, as agreed with the bikes thread
        clipon = any(s_.get("clipon") for s_ in fe)
        rz = (top[0] - 12, top[1] + 22) if clipon else (top[0] - 28, top[1] + 62)
    return rz, has


def model_top(model):
    g, wh, fk = model["geometry"], model["wheels"], model["fork"]
    rake = math.radians(g["rake"])
    F = (g["wheelbase"], wh["front"]["tyre_r"])
    uf = (-math.sin(rake), math.cos(rake))
    return (F[0] + uf[0] * fk["length"], F[1] + uf[1] * fk["length"]), F, uf


def model_hand(model):
    """The model's own hand point: the middle of its grip (detail3d.controls draws the grip from the grip
    circle at y = 258 to y = 384, 6 mm back and 4 mm down)."""
    fe = model.get("front_end", [])
    g = next((s_["at"] for s_ in fe if s_.get("circle") and s_.get("part") != "head"), None)
    return (g[0] - 3, 321.0, g[1] - 2)


def hand_point(model, bar):
    """Where a bar puts the hands on this model (x, y, z), without building anything."""
    st = BARS[bar]
    top, F, uf = model_top(model)
    if st.get("clip"):
        c = dict(F=F, uf=uf, fk=model["fork"], top=top)
        return clip_half(st, c, 1)["H"]
    (rx, rz_), _ = clamp_point(model, dict(top=top))
    return bar_half(st, (rx, 0, rz_), 1)[1]


def build_bar(name, st, C, fin, c=None, risers=None):
    """A yoke bar (or clip-ons when c is given) with its clamps. Returns {s: (g0, d, E)} for the grips."""
    m = finish(fin)
    dark = B.mat("alu_dark")
    lines = {}
    if st.get("clip"):
        for s in (-1, 1):
            q = clip_half(st, c, s)
            uf = c["uf"]
            ax = (uf[0], 0, uf[1])
            # the clamp: a split ring round the fork tube with two pinch bolts on its back
            p0 = vadd(q["centre"], ax, -20)
            D.lathe(f"bclip_ring{s}", p0, ax, [(0, 0), (0, 36.5), (2, 38), (38, 38), (40, 36.5), (40, 0)], m, 48)
            back = vnorm((-uf[1], 0, uf[0]))     # square to the fork, pointing back
            back = back if back[0] < 0 else tuple(-x for x in back)
            for dt in (10, 30):
                D.bolt(f"bclip_pinch{s}{dt}", vadd(vadd(p0, ax, dt), back, 37), back, 4.5, 4)
            # the body: from the ring out to the bar root, tapering, with the bar socket
            r0 = q["root"]
            D.strap(f"bclip_body{s}", [vadd(q["centre"], (0, s * 30, -6)), vadd(q["centre"], (-18, s * 62, 0)), vadd(q["centre"], (-30, s * (r0[1] * s - 105 - 4), (r0[2] - q["centre"][2]) * 0.7)), vadd(r0, q["d"], -8)],
                    [(26, 16), (22, 15), (19, 14), (17, 14)], m)
            # the bar: straight out of the socket and through the grip
            bar_pts = [vadd(r0, q["d"], -20)] + [vadd(r0, q["d"], (vlen(vsub(q["E"], r0)) + 2) * j / 10) for j in range(1, 11)]
            sweep(f"bclip_bar{s}", bar_pts, [BAR_R] * len(bar_pts), m, 28)
            D.lathe(f"bclip_socket{s}", vadd(r0, q["d"], -14), q["d"], [(0, 0), (0, 15), (26, 15), (30, 13.5), (30, 0)], m, 32)
            D.bolt(f"bclip_sbolt{s}", vadd(vadd(r0, q["d"], 8), (0, 0, 1), 14), (0, 0, 1), 4.2, 3.5)
            lines[s] = (q["g0"], q["d"], q["E"], q["H"])
        return lines
    pts, rad = [], []
    for s in (-1, 1):
        half, H, g0, d, E = bar_half(st, C, s)
        if s == -1:
            half = half[::-1]
        else:
            half = half[1:]
        lines[s] = (g0, d, E, H)
        pts += half
    for p in pts:
        y = abs(p[1])
        if st.get("fat"):
            r = FAT_R if y < 120 else BAR_R if y > 175 else FAT_R + (BAR_R - FAT_R) * (y - 120) / 55
        else:
            r = BAR_R
        rad.append(r)
    sweep(name, pts, rad, m, 36)
    br = FAT_R if st.get("fat") else BAR_R
    if st.get("brace"):
        # the cross brace between the two climbing legs, and a pad over the clamp area
        ys = 150.0
        legs = []
        for s in (-1, 1):
            half = bar_half(st, C, s)[0]
            q = min(half[2:], key=lambda p: abs(abs(p[1]) - ys))
            legs.append(q)
        a_, b_ = legs
        brace = [vadd(a_, vsub(b_, a_), j / 12) for j in range(13)]
        brace = [(p[0], p[1], p[2] - 6 * (1 - (p[1] / ys) ** 2)) for p in brace]
        sweep(f"{name}_brace", brace, [7.0] * len(brace), m, 20)
        for s in (-1, 1):
            D.lathe(f"{name}_bracecap{s}", vadd(legs[0 if s == -1 else 1], (0, -s * 12, -4), 1), (0, s, 0), [(0, 0), (0, 12.5), (16, 12.5), (18, 11), (18, 0)], m, 24)
        mid = ((a_[0] + b_[0]) / 2, 0, (a_[2] + b_[2]) / 2 - 6)
        D.lathe(f"{name}_pad", (mid[0], -95, mid[2] + 4), (0, 1, 0), [(0, 0), (0, 17), (3, 21), (187, 21), (190, 17), (190, 0)], D.surf("barpad", (0.02, 0.02, 0.022), 0, 0.85, bump=(0.6, 0.3)), 32, square=3.0, ell=(1.2, 0.9))
    # the bar clamps on the risers: a lower cradle and a cap, with two bolts front and back
    for s in (-1, 1):
        cap = D.chaikin([(C[0] - 19, C[2] - br - 4), (C[0] + 19, C[2] - br - 4), (C[0] + 19, C[2] + br * 0.4), (C[0] + 10, C[2] + br + 6), (C[0] - 10, C[2] + br + 6), (C[0] - 19, C[2] + br * 0.4)], 1)
        B.prism(f"bclamp{s}", cap, s * 45 - 16, s * 45 + 16, dark, 1.5)
        for dx in (-13, 13):
            D.bolt(f"bclampbolt{s}{dx}", (C[0] + dx, s * 45, C[2] + br + 3), (0, 0, 1), 4.5, 4)
        if risers is not None:
            B.tube(f"briser{s}", [(risers[0], s * 45, risers[1]), (C[0], s * 45, C[2] - br - 3)], 15, dark)
    return lines


# ---------------------------------------------------------------- grips
def pattern_mat(key, length, rbody, fn, base, rough, ins_col=None, bump=1.0):
    """A grip skin from a height picture of the unrolled grip (mm): fn(u, v) -> (height 0..1, insert 0..1)."""
    import numpy as np
    if key in bpy.data.materials:
        return bpy.data.materials[key]
    W_, H_ = 1024, 2048
    circ = 2 * math.pi * rbody
    u = np.linspace(0, circ, W_, endpoint=False)[None, :] * np.ones((H_, 1))
    v = np.linspace(0, length, H_)[:, None] * np.ones((1, W_))
    h, ins = fn(u, v, circ)
    px = np.zeros((H_, W_, 4), dtype=np.float32)
    px[..., 0], px[..., 1], px[..., 3] = h, ins, 1.0
    img = bpy.data.images.new(key + "_h", W_, H_, alpha=False, float_buffer=True)
    img.colorspace_settings.name = "Non-Color"
    img.pixels.foreach_set(px.ravel())
    img.pack()
    m = bpy.data.materials.new(key)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    uvn, mp, tex, sep = nt.nodes.new("ShaderNodeUVMap"), nt.nodes.new("ShaderNodeMapping"), nt.nodes.new("ShaderNodeTexImage"), nt.nodes.new("ShaderNodeSeparateColor")
    uvn.uv_map = "skin"
    mp.inputs["Scale"].default_value = (1 / (2 * math.pi * rbody), 1 / length, 1)
    tex.image, tex.interpolation = img, "Cubic"
    nt.links.new(uvn.outputs["UV"], mp.inputs["Vector"])
    nt.links.new(mp.outputs["Vector"], tex.inputs["Vector"])
    nt.links.new(tex.outputs["Color"], sep.inputs[0])
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.inputs["A"].default_value, mix.inputs["B"].default_value = (*base, 1), (*(ins_col or base), 1)
    nt.links.new(sep.outputs[1], mix.inputs["Factor"])
    nt.links.new(mix.outputs["Result"], b.inputs["Base Color"])
    b.inputs["Roughness"].default_value = rough
    nz = nt.nodes.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 2500.0
    ad = nt.nodes.new("ShaderNodeMath")
    ad.operation = "MULTIPLY_ADD"
    ad.inputs[1].default_value = 0.05
    nt.links.new(nz.outputs["Fac"], ad.inputs[0])
    nt.links.new(sep.outputs[0], ad.inputs[2])
    bp = nt.nodes.new("ShaderNodeBump")
    bp.inputs["Strength"].default_value, bp.inputs["Distance"].default_value = bump, 0.0011
    nt.links.new(ad.outputs[0], bp.inputs["Height"])
    nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    return m


def _waffle(u, v, circ):
    import numpy as np
    p = circ / round(circ / 5.2)
    a = np.abs(((u / p) % 1.0) - 0.5) * 2
    b = np.abs(((v / 5.2) % 1.0) - 0.5) * 2
    groove = np.maximum(np.clip((a - 0.72) / 0.12, 0, 1), np.clip((b - 0.72) / 0.12, 0, 1))
    zone = np.clip((v - 8) / 1.5, 0, 1) * np.clip((v.max() - 10 - v) / 1.5, 0, 1)
    return 1 - 0.8 * groove * zone, np.zeros_like(u)


def _flutes(u, v, circ):
    import numpy as np
    n = 18
    a = np.abs(((u / circ * n) % 1.0) - 0.5) * 2
    zone = np.clip((v - 22) / 3, 0, 1) * np.clip((v.max() - 22 - v) / 3, 0, 1)
    return 1 - 0.75 * np.clip((a - 0.62) / 0.3, 0, 1) * zone, np.zeros_like(u)


def _dimples(u, v, circ):
    import numpy as np
    pu = circ / round(circ / 6.0)
    row = np.floor(v / 5.2)
    uu = u + (row % 2) * pu / 2
    du = ((uu / pu) % 1.0 - 0.5) * pu
    dv = ((v / 5.2) % 1.0 - 0.5) * 5.2
    r = np.hypot(du, dv)
    band = (np.abs(v - v.max() * 0.62) < 7).astype(float)
    zone = np.clip((v - 12) / 2, 0, 1) * np.clip((v.max() - 14 - v) / 2, 0, 1) * (1 - band)
    dimple = np.clip(1 - r / 1.9, 0, 1) ** 0.7 * zone
    edge = np.clip(1 - np.abs(np.abs(v - v.max() * 0.62) - 7) / 0.6, 0, 1)
    return 1 - 0.7 * dimple - 0.6 * edge, band


def _leather(u, v, circ):
    import numpy as np
    pitch = 21.0                                  # the strip winds round once every 21 mm
    ph = ((v + u / circ * pitch) / pitch) % 1.0
    seam = np.clip(1 - np.abs(ph - 0.5) / 0.035, 0, 1)
    st_pos = ((u / 3.2) % 1.0)
    stitch = (ph - 0.5 > 0.07) * (ph - 0.5 < 0.1) * (st_pos < 0.5)
    zone = np.clip((v - 9) / 1, 0, 1) * np.clip((v.max() - 9 - v) / 1, 0, 1)
    h = 1 - 0.85 * seam * zone - 0.35 * stitch * zone
    return h, stitch * zone * 1.0


def build_grip(name, g0, d, style):
    """A grip on the bar from g0 outwards along d (GRIP_L long). Returns the lathe object."""
    L_ = GRIP_L
    if style == "knurl":
        prof = [(0, 12), (0, 28.5), (1.2, 30.4), (3, 31), (4.8, 30.4), (6, 28.5), (6.8, 23), (8.2, 19.2), (10.5, 17.3)]
        for k_ in range(1, 30):
            t = 10.5 + (L_ - 26) * k_ / 30
            prof.append((t, 16.9 + 0.7 * math.sin(math.pi * k_ / 30)))
        prof += [(L_ - 15.5, 17.2), (L_ - 14, 19.4), (L_ - 12.5, 19.9), (L_ - 2, 19.9), (L_ - 0.6, 19.2), (L_, 17.8), (L_, 11)]
        return D.lathe(name, g0, d, prof, D.grip_mat(L_, 31.0), 96, sharp=False, uv=True)
    if style == "ribbed":
        prof = [(0, 12), (0, 26), (1.5, 27.5), (4, 27.5), (5.5, 26), (7, 20), (9, 17.6)]
        n = 24
        for k_ in range(n * 4 + 1):
            t = 10 + (L_ - 22) * k_ / (n * 4)
            ph = (k_ % 4)
            prof.append((t, (18.4, 18.2, 16.6, 16.8)[ph]))
        prof += [(L_ - 10, 18.0), (L_ - 8, 19.2), (L_ - 2, 19.2), (L_, 18), (L_, 11)]
        return D.lathe(name, g0, d, prof, D.surf("gripribbed", (0.018, 0.018, 0.02), 0.0, 0.62, bump=(0.12, 0.4), wear=0.05), 72, sharp=False)
    if style == "waffle":
        prof = [(0, 12), (0, 22), (1, 23.5), (3, 23.5), (4, 22), (5, 18.5)]
        for k_ in range(1, 30):
            t = 5 + (L_ - 13) * k_ / 30
            prof.append((t, 17.6 + 1.1 * math.sin(math.pi * k_ / 30)))
        prof += [(L_ - 6, 18.4), (L_ - 3, 18.6), (L_, 17.6), (L_, 11)]
        mat = pattern_mat("grip_waffle", L_, 18.0, _waffle, (0.03, 0.03, 0.035), 0.82, bump=1.4)
        return D.lathe(name, g0, d, prof, mat, 96, sharp=False, uv=True)
    if style == "barrel":
        prof = [(0, 12), (0, 21)]
        for k_ in range(6):      # ringed inner end
            t = 1 + k_ * 3.2
            prof += [(t, 21), (t + 1.6, 19.8)]
        for k_ in range(1, 30):
            t = 20 + (L_ - 40) * k_ / 30
            prof.append((t, 18.0 + 2.2 * math.sin(math.pi * k_ / 30)))
        for k_ in range(6):      # ringed outer end
            t = L_ - 20 + k_ * 3.2
            prof += [(t, 19.8), (t + 1.6, 21)]
        prof += [(L_, 20.5), (L_, 11)]
        mat = pattern_mat("grip_barrel", L_, 19.5, _flutes, (0.022, 0.02, 0.02), 0.55, bump=1.2)
        return D.lathe(name, g0, d, prof, mat, 96, sharp=False, uv=True)
    if style == "touring":
        prof = [(0, 12), (0, 27), (1.5, 28.5), (5, 28.5), (6.5, 27), (8, 21.5)]
        for k_ in range(1, 30):
            t = 9 + (L_ - 19) * k_ / 30
            prof.append((t, 19.6 + 1.6 * math.sin(math.pi * min(1.0, k_ / 22.0))))
        prof += [(L_ - 8, 20.2), (L_ - 6, 21.2), (L_ - 1, 21.2), (L_, 20), (L_, 11)]
        mat = pattern_mat("grip_touring", L_, 20.5, _dimples, (0.025, 0.025, 0.028), 0.78, ins_col=(0.055, 0.057, 0.06), bump=1.0)
        return D.lathe(name, g0, d, prof, mat, 96, sharp=False, uv=True)
    if style == "leather":
        # a polished alloy ring each end, the leather wound between them
        alu = D.surf("gripring", (0.8, 0.8, 0.82), 1.0, 0.14, wear=0.03)
        D.lathe(name + "_ringin", g0, d, [(0, 11), (0, 20.5), (1, 21.5), (7, 21.5), (8, 20.5), (8, 11)], alu, 48)
        D.lathe(name + "_ringout", vadd(g0, d, L_ - 8), d, [(0, 11), (0, 20.5), (1, 21.5), (7, 21.5), (8, 20.5), (8, 11)], alu, 48)
        prof = [(7.5, 11), (7.5, 18.4)]
        for k_ in range(1, 30):
            t = 7.5 + (L_ - 15) * k_ / 30
            prof.append((t, 18.4 + 0.5 * math.sin(math.pi * k_ / 30)))
        prof += [(L_ - 7.5, 18.4), (L_ - 7.5, 11)]
        mat = pattern_mat("grip_leather", L_ - 15, 18.6, _leather, (0.075, 0.034, 0.014), 0.5, ins_col=(0.42, 0.34, 0.22), bump=0.8)
        return D.lathe(name, vadd(g0, d, 7.5), d, [(t - 7.5, r) for t, r in prof], mat, 96, sharp=False, uv=True)
    raise KeyError(style)


# ---------------------------------------------------------------- bar ends
def build_end(name, E, d, style, s, fin="satin_black"):
    """A bar end at the bar's end E, facing out along d. The insert goes 17 mm into the bar."""
    dark = B.mat("alu_dark")
    ins = vadd(E, d, -17)
    if style == "cap":
        D.lathe(name, ins, d, [(0, 0), (0, 9), (17, 9), (17, 17.2), (19, 17.6), (21, 16.4), (22.5, 12), (23, 0)], B.mat("rubber"), 40)
    elif style == "weighted":
        D.lathe(name, ins, d, [(0, 0), (0, 9), (17, 9), (17, 15.5), (20, 17), (35, 17), (41, 13), (41, 0)], dark, 40)
        D.bolt(name + "bolt", vadd(E, d, 23), d, 5, 2.5)
    elif style == "tapered":
        prof = [(0, 0), (0, 9), (17, 9), (17, 17.5), (19, 18.5)]
        for k_ in range(3):
            t = 23 + k_ * 7
            prof += [(t, 18.4 - k_ * 1.4), (t + 0.8, 17.0 - k_ * 1.4), (t + 1.8, 17.0 - k_ * 1.4), (t + 2.6, 18.0 - k_ * 1.6)]
        prof += [(48, 12.5), (50, 11), (50, 0)]
        D.lathe(name, ins, d, prof, finish("anodised_gold" if fin in ("satin_black", "chrome", "polished") else fin), 48)
        D.bolt(name + "bolt", vadd(E, d, 33), d, 4.5, 2.2)
    elif style == "ringed":
        D.lathe(name, ins, d, [(0, 0), (0, 9), (17, 9), (17, 16.5), (18, 17.5), (24, 17.5), (25, 15.5), (36, 15.5), (37, 18.5), (39, 18.5), (40, 14), (40, 0)], dark, 48)
        knurl = D.surf("knurlring", (0.75, 0.75, 0.77), 1.0, 0.3, bump=(0.45, 0.8), wear=0.02)
        D.lathe(name + "ring", vadd(ins, d, 24.5), d, [(0, 15), (0, 18.6), (11.5, 18.6), (11.5, 15)], knurl, 64)
        D.lathe(name + "face", vadd(ins, d, 39.6), d, [(0, 0), (0, 13.6), (0.6, 13.6), (0.6, 0)], finish("polished"), 48)
    elif style == "bullet":
        prof = [(0, 0), (0, 9), (17, 9), (17, 16), (18, 17.4)]
        for k_ in range(1, 13):
            a = math.radians(90 * k_ / 12)
            prof.append((18 + 22 * math.sin(a), 17.4 * math.cos(a) + 0.01))
        D.lathe(name, ins, d, prof, B.mat("chrome"), 48)
    elif style == "mirror":
        # a clamp on the bar end, a short stem standing out and up, a round head facing back
        D.lathe(name, ins, d, [(0, 0), (0, 9), (17, 9), (17, 15.5), (20, 17.5), (33, 17.5), (36, 15), (36, 0)], dark, 40)
        root = vadd(E, d, 12)
        mc = vadd(root, (0, s * 26, 80), 1)
        mc = (mc[0] - 4, mc[1], mc[2])
        B.smooth_tube(name + "stem", [vadd(root, (0, s * 6, 10)), vadd(root, (0, s * 14, 46)), vadd(mc, (14, -s * 6, -40)), vadd(mc, (14, 0, -6))], 5.6, dark)
        D.lathe(name + "head", vadd(mc, (14, 0, 0)), (-1, 0, 0), [(0, 0), (0, 18), (5, 36), (10, 41), (14, 41), (14, 0)], dark, 48)
        D.lathe(name + "glass", vadd(mc, (-2.2, 0, 0)), (-1, 0, 0), [(0, 0), (0, 38.5), (0.4, 38.5), (0.4, 0)], D.surf("mirrorglass", (0.9, 0.92, 0.95), 1.0, 0.01, wear=0.0), 48)
    else:
        raise KeyError(style)


# ---------------------------------------------------------------- fitting to a built bike
CONTROL = ("switch", "swlights", "swind", "swhorn", "swkill", "swstart", "perch", "levpivot", "lever", "leveradj", "mastercyl", "masterneck", "brakeres", "clutchadj", "cut_lever", "handguard")
MIRROR = ("mirror", "mstalk")
CABLE = ("brakeline_up", "clutchcable", "throttle", "brakehose")
OLD_BAR = re.compile(r"^(bar|bar[-+]?\d+(\.\d+)?|riser.*|clamp[-+]?\d+.*|barclamp.*|grip.*|barend.*)$")


def side_of(ob):
    bb = [ob.matrix_world @ Vector(c_) for c_ in ob.bound_box]
    y = sum(v.y for v in bb) / 8
    return 1 if y > 0 else -1


def rot_between(a, b):
    a, b = Vector(a).normalized(), Vector(b).normalized()
    return a.rotation_difference(b).to_matrix().to_4x4()


def fit(b, model, c, bar, grip="knurl", end="weighted", fin=None):
    """Swap a built bike's bars, grips and bar ends for library parts. The levers, switches and mirrors
    move with the grip; cables bend to follow. Returns {s: (g0, d, E, H)} and the parts made."""
    use(b)
    st = BARS[bar]
    fin = fin or st["finish"]
    grip_m = c["grip"]
    old_g0 = {s: (grip_m[0], s * 258.0, grip_m[1]) for s in (-1, 1)}
    old_d = {s: vnorm((-6, s * 126.0, -4)) for s in (-1, 1)}
    before = set(bpy.data.objects.keys())
    for ob in list(bpy.data.objects):
        if OLD_BAR.match(ob.name):
            bpy.data.objects.remove(ob, do_unlink=True)
    if st.get("clip"):
        lines = build_bar("bar_lib", st, None, fin, c=c)
    else:
        rz, has = clamp_point(model, c)
        C = (rz[0], 0, rz[1])
        lines = build_bar("bar_lib", st, C, fin, risers=None if has else c["top"])
        if has:
            for s in (-1, 1):
                B.tube(f"briser{s}", [(c["top"][0], s * 45, c["top"][1]), (C[0], s * 45, C[2] - (FAT_R if st.get("fat") else BAR_R) - 3)], 15, B.mat("alu_dark"))
    faired = model["family"].lower() == "sport" and any(v.get("cowl") for v in model["body"].get("panels", []))
    guards = any(o.name.startswith("handguard") for o in bpy.data.objects)
    M = {}
    for s in (-1, 1):
        g0, d, E, H = lines[s]
        M[s] = Matrix.Translation(Vector(B.V(*g0))) @ rot_between(old_d[s], d) @ Matrix.Translation(-Vector(B.V(*old_g0[s])))
    for ob in list(bpy.data.objects):
        if ob.name in before and (ob.name.startswith(CONTROL) or (ob.name.startswith(MIRROR) and not faired)):
            if ob.type in ("MESH", "CURVE", "EMPTY"):
                ob.matrix_world = M[side_of(ob)] @ ob.matrix_world
    for ob in list(bpy.data.objects):
        if ob.name.startswith(CABLE) and ob.type == "CURVE":
            for sp in ob.data.splines:
                pts = list(sp.points) if sp.type in ("POLY", "NURBS") else []
                for p in pts:
                    w_ = ob.matrix_world @ Vector(p.co[:3])
                    s = 1 if w_.y > 0 else -1
                    perch = Vector(B.V(grip_m[0] + 20, s * 188, grip_m[1]))
                    k = max(0.0, min(1.0, 1 - ((w_ - perch).length * 1000 - 40) / 260))
                    if k > 0:
                        n_ = M[s] @ w_
                        q = w_.lerp(n_, k)
                        p.co = (*(ob.matrix_world.inverted() @ q), p.co[3])
    for s in (-1, 1):
        g0, d, E, H = lines[s]
        build_grip(f"grip_lib{s}", g0, d, grip)
        if not guards and model.get("detail", {}).get("mirrors") != "barend":
            build_end(f"barend_lib{s}", E, d, end, s, fin)
    made = [o for o in bpy.data.objects.keys() if o not in before]
    return lines, made


# ---------------------------------------------------------------- checks
LOCK = 30.0           # degrees of steering lock each way
CLEAR = 15.0          # mm the bars must clear the bodywork by at full lock
STATIC = ("tank", "cowl", "ear", "adv", "bubble", "nose", "wing")      # what the bars swing past: tank and frame-mounted bodywork
STEERS = ("bar_lib", "bclip", "bclamp", "briser", "grip_lib", "barend_lib", "switch", "swlights", "swind", "swhorn", "swkill", "swstart", "perch",
          "lever", "levpivot", "mastercyl", "brakeres", "clutchadj", "mirror", "mstalk", "handguard")


def _verts(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = ev.to_mesh()
    vs = [ev.matrix_world @ v.co for v in me.vertices]
    polys = [tuple(p.vertices) for p in me.polygons]
    ev.to_mesh_clear()
    return vs, polys


def lock_check(model, c):
    """Turn the bars to full lock each way about the steering axis and find the closest approach (mm) of
    anything on the bars to the bodywork and frame that does not steer."""
    from mathutils.bvhtree import BVHTree
    F, uf, nb, fk = c["F"], c["uf"], c["nb"], c["fk"]
    stem = B.add(B.add(F, uf, fk["length"] - 10), nb, fk["offset"])
    axis_p = Vector(B.V(stem[0], 0, stem[1]))
    axis_d = Vector((uf[0], 0, uf[1])).normalized()
    static, moving = [], []
    for o in bpy.data.objects:
        if o.type not in ("MESH", "CURVE") or o.hide_render or o.name.startswith("cut_"):
            continue
        if (o.name.startswith(STEERS) or OLD_BAR.match(o.name)) and not (o.name.startswith(MIRROR) and model["family"].lower() == "sport"):
            moving.append(o)
        elif o.name.startswith(STATIC):
            static.append(o)
    sv, sp, owner = [], [], []
    for o in static:
        vs, ps = _verts(o)
        base = len(sv)
        sv += vs
        sp += [tuple(i + base for i in p) for p in ps]
        owner += [o.name] * len(ps)
    if not sv:
        return None
    tree = BVHTree.FromPolygons(sv, sp, epsilon=0.0)
    mv = []
    for o in moving:
        vs, _ = _verts(o)
        step = max(1, len(vs) // 600)
        mv += [(o.name, v) for v in vs[::step] if abs(v.y) * 1000 > 120]      # the outer bar: what swings out past the tank
    def closest(ang):
        R = Matrix.Rotation(math.radians(ang), 4, axis_d)
        w_ = (1e9, None, None)
        for nm, v in mv:
            q = R @ (v - axis_p) + axis_p
            loc, nor, idx, dist = tree.find_nearest(q)
            if loc is None:
                continue
            dd = dist * 1000
            if dd < w_[0]:
                w_ = (dd, nm, owner[idx])
        return w_
    # the largest lock each way that keeps CLEAR mm between the bars and the bodywork
    out = dict(clear_at_30=None, part=None, against=None, at_rest=None, max_lock=None)
    worst = min((closest(a) for a in (-LOCK, LOCK)), key=lambda r: r[0])
    out["clear_at_30"], out["part"], out["against"] = round(worst[0], 1), worst[1], worst[2]
    r0 = closest(0.0)
    out["at_rest"] = [round(r0[0], 1), r0[1], r0[2]]
    if worst[0] >= CLEAR:
        out["max_lock"] = LOCK
    else:
        best = 0.0
        for a in [x * 2.0 for x in range(1, 15)]:
            if min(closest(-a)[0], closest(a)[0]) >= CLEAR:
                best = a
            else:
                break
        out["max_lock"] = best
    return out


def clip_check(made):
    """Our new parts against everything else on the bike (clip3d's measure, only for pairs with our parts)."""
    import clip3d
    P = clip3d.parts()
    ours = [p for p in P if p["name"] in made]
    rest = [p for p in P if p["name"] not in made]
    found = {}
    for a in ours:
        for b_ in rest:
            if any(a["lo"][k] > b_["hi"][k] or b_["lo"][k] > a["hi"][k] for k in range(3)):
                continue
            if not a["tree"].overlap(b_["tree"]):
                continue
            d1, _ = clip3d.depth(a, b_)
            d2, _ = clip3d.depth(b_, a)
            dd = max(d1, d2)
            if dd >= clip3d.NEW_MIN:
                key = f"{a['stem']} | {b_['stem']}"
                found[key] = max(found.get(key, 0), round(dd, 1))
    return found


# joints where our parts are meant to sit inside the bike's (the bar inside the clamp, the grip on the bar)
JOINTS = ("bar_lib | bclamp", "bar_lib | briser", "bar_lib | perch", "bar_lib | switch", "bar_lib | grip", "bar_lib | barend",
          "grip_lib | bar", "barend_lib | bar", "grip_lib | switch", "bclip_ring | fork", "bclip_bar | bclip", "bclip_body | fork",
          "bclip_body | bclip", "bclip_bar | perch", "bclip_bar | switch", "bclip_socket | bclip", "bclip_ring | yoke", "bclip_pinch | bclip",
          "bclamp | riser", "briser | yoke", "bar_lib_pad | bar_lib", "bar_lib_brace | bar_lib", "bar_lib_bracecap | bar_lib",
          "bclampbolt | bclamp", "barend_lib | grip", "grip_lib | barend", "bar_lib | mirror", "bar_lib | brakeres", "bar_lib | master",
          "bclip_bar | master", "bar_lib | clutchadj", "bclip_bar | clutchadj", "bar_lib | lever", "bclip_bar | lever", "bclip_bar | grip",
          "bclip_sbolt | bclip", "briser | stem", "bclip_ring | stanchion", "bclip_ring | forktube", "bclip_pinch | forktube", "bclip_body | forktube")


def joint_ok(pair):
    a, b_ = [x.strip() for x in pair.split("|")]
    for j in JOINTS:
        ja, jb = [x.strip() for x in j.split("|")]
        if (a.startswith(ja) and b_.startswith(jb)) or (b_.startswith(ja) and a.startswith(jb)):
            return True
    return False


LEADS = {"Naked": "corvano_naked", "Sport": "kazeda_sport", "Cafe": "hartwell_cafe", "Classic": "hartwell_classic",
         "Cruiser": "lodestar_cruiser", "Adventure": "corvano_adventure", "MX": "varga_mx"}
HAND_OK, HAND_MAX = 40.0, 70.0


def models():
    out = {}
    for f in sorted(os.listdir(os.path.join(ROOT, "data", "models"))):
        if f.endswith(".json"):
            out[f[:-5]] = json.load(open(os.path.join(ROOT, "data", "models", f)))
    return out


def hands_table():
    """For every bar and every model: how far the hands move from the model's own hand point (mm)."""
    rows = {}
    for n, m in models().items():
        if m["family"] == "Chopper":
            continue
        h0 = model_hand(m)
        for bar in BARS:
            h = hand_point(m, bar)
            rows.setdefault(bar, {})[n] = dict(family=m["family"], dx=round(h[0] - h0[0]), dy=round(h[1] - h0[1]), dz=round(h[2] - h0[2]),
                                               d=round(math.dist((h[0], h[2]), (h0[0], h0[2]))))
    return rows


def built(model_name, bar, grip="knurl", end="weighted", fin=None):
    import build3d
    use(build3d)
    m = models()[model_name]
    build3d.DETAIL["on"] = True
    c = build3d.build(m)
    # build() returns only a few values; the controls need the locals detail3d saw: rebuild them here
    top, F, uf = model_top(m)
    rake = math.radians(m["geometry"]["rake"])
    cc = dict(F=F, uf=uf, nb=(-math.cos(rake), -math.sin(rake)), fk=m["fork"], top=top, grip=next(s_["at"] for s_ in m["front_end"] if s_.get("circle") and s_.get("part") != "head"))
    del c
    lines, made = fit(build3d, m, cc, bar, grip, end, fin)
    return build3d, m, cc, lines, made


def check(fams=None, only=None):
    res = dict(hands=hands_table(), fit={})
    import build3d
    res["base"] = {}
    for fam, mn in LEADS.items():
        if fams and fam not in fams:
            continue
        # the bike's own bars first: how far they turn before they come within CLEAR of the tank or bodywork
        fresh()
        m = models()[mn]
        build3d.DETAIL["on"] = True
        build3d.build(m)
        top, F, uf = model_top(m)
        rake = math.radians(m["geometry"]["rake"])
        res["base"][fam] = dict(model=mn, lock=lock_check(m, dict(F=F, uf=uf, nb=(-math.cos(rake), -math.sin(rake)), fk=m["fork"], top=top)))
        print(f"BASE {fam:10s} {res['base'][fam]}", flush=True)
        for bar in BARS:
            if only and bar not in only:
                continue
            fresh()
            b3, m, cc, lines, made = built(mn, bar)
            lk = lock_check(m, cc)
            cl = clip_check(made)
            bad = {k: v for k, v in cl.items() if not joint_ok(k)}
            res["fit"].setdefault(bar, {})[fam] = dict(model=mn, lock=lk, clips=bad)
            print(f"CHECK {bar:14s} {fam:10s} lock {lk} clips {bad}", flush=True)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "check.json")
    old = json.load(open(path)) if ((fams or only) and os.path.exists(path)) else {}
    if old:
        for bar, v in res["fit"].items():
            old.setdefault("fit", {}).setdefault(bar, {}).update(v)
        old["hands"] = res["hands"]
        old.setdefault("base", {}).update(res["base"])
        res = old
    json.dump(res, open(path, "w"), indent=1)
    return res


# ---------------------------------------------------------------- pictures
def stage(samples=40, res=(1100, 720)):
    """A plain studio for one part on its own."""
    sc = bpy.context.scene
    sc.render.engine, sc.cycles.device, sc.cycles.samples, sc.cycles.use_denoising = "CYCLES", "CPU", samples, True
    sc.render.resolution_x, sc.render.resolution_y = res
    w = bpy.data.worlds.new("w")
    sc.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.56, 0.58, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.35
    try:
        sc.view_settings.look = "AgX - Medium High Contrast"
    except Exception:
        pass
    bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 0, 0))
    fl = bpy.context.object
    fm = bpy.data.materials.new("floor")
    fm.use_nodes = True
    fm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.2, 0.2, 0.21, 1)
    fm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.6
    fl.data.materials.append(fm)
    for loc, energy, size in (((0.8, 1.6, 2.2), 260, 2.5), ((2.2, -1.2, 1.4), 120, 2.0), ((-1.6, -0.8, 1.6), 90, 2.0)):
        bpy.ops.object.light_add(type="AREA", location=loc)
        L = bpy.context.object
        L.data.energy, L.data.size = energy, size
        L.rotation_euler = B._look(loc, (0, 0, 0.3))
    return sc


def shoot(sc, path, loc, tgt, lens):
    cam = bpy.data.cameras.new("cam")
    cam.lens = lens
    ob = bpy.data.objects.new("cam", cam)
    sc.collection.objects.link(ob)
    ob.location = B.V(*loc)
    ob.rotation_euler = B._look(ob.location, B.V(*tgt))
    sc.camera = ob
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)


def fresh():
    import build3d
    use(build3d)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    build3d._mats.clear()
    build3d.DETAIL["on"] = False


def bench_bar(bar, grip="knurl", end="weighted"):
    """One bar on its own, standing on a stub top yoke (or two fork tube stubs for clip-ons)."""
    st = BARS[bar]
    dark = B.mat("alu_dark")
    if st.get("clip"):
        rake = math.radians(24)
        F, uf = (0.0, -480.0), (-math.sin(rake), math.cos(rake))
        fk = dict(length=1000)
        c = dict(F=F, uf=uf, fk=fk)
        top = B.add(F, uf, 1000)
        for s in (-1, 1):
            a, b_ = B.add(F, uf, 1000 - 330), B.add(F, uf, 1000 + 8)
            B.tube(f"stub{s}", [(a[0], s * 105, a[1]), (b_[0], s * 105, b_[1])], 27, B.mat("gold"))
        y0 = B.add(F, uf, 1000 - 34)
        B.prism("stubyoke", D.chaikin([(y0[0] - 40, y0[1] - 6), (y0[0] + 40, y0[1] - 6), (y0[0] + 46, y0[1] + 18), (y0[0] - 46, y0[1] + 18)], 1), -140, 140, dark, 3)
        lines = build_bar("bar_lib", st, None, st["finish"], c=c)
    else:
        top = (0.0, 330.0)
        B.prism("stubyoke", D.chaikin([(-60, top[1] - 24), (60, top[1] - 24), (60, top[1]), (-60, top[1])], 1), -140, 140, dark, 3)
        C = (top[0] - 20, 0, top[1] + 45)
        lines = build_bar("bar_lib", st, C, st["finish"], risers=top)
    for s in (-1, 1):
        g0, d, E, H = lines[s]
        build_grip(f"grip_lib{s}", g0, d, grip)
        build_end(f"barend_lib{s}", E, d, end, s, st["finish"])
    return lines


def parts(which=None, samples=40):
    os.makedirs(OUT, exist_ok=True)
    jobs = [("bar", k) for k in BARS] + [("grip", k) for k in GRIPS] + [("end", k) for k in ENDS]
    for kind, key in jobs:
        if which and key not in which:
            continue
        fresh()
        sc = stage(samples)
        if kind == "bar":
            lines = bench_bar(key)
            H = lines[1][3]
            tgt = (H[0] * 0.5 - 10, 0, H[2] * 0.6 + 330 * 0.4)
            shoot(sc, os.path.join(OUT, f"bar_{key}.png"), vadd(tgt, (760, -900, 420)), tgt, 45)
        elif kind == "grip":
            g0, d = (0, 0, 120), (0, 1, 0)
            sweep("stubbar", [(0, -60, 120), (0, GRIP_L + 30, 120)], [BAR_R, BAR_R], finish("satin_black"), 32)
            build_grip("grip_lib", (0, 0, 120), d, key)
            build_end("barend_lib", (0, GRIP_L + 20, 120), d, "cap", 1)
            shoot(sc, os.path.join(OUT, f"grip_{key}.png"), (300, -10, 230), (0, 70, 118), 70)
        else:
            d = (0, 1, 0)
            sweep("stubbar", [(0, -40, 160), (0, 40, 160)], [BAR_R, BAR_R], finish("satin_black"), 32)
            build_grip("grip_lib", (0, 40 - GRIP_L, 160), d, "knurl")
            build_end("barend_lib", (0, 40, 160), d, key, 1)
            if key == "mirror":
                shoot(sc, os.path.join(OUT, f"end_{key}.png"), (330, 300, 330), (0, 50, 210), 60)
            else:
                shoot(sc, os.path.join(OUT, f"end_{key}.png"), (220, 200, 230), (0, 52, 160), 85)


# a bike for each bar's fitted picture: the family it suits best, with grips and ends that go with it
FITTED = {
    "street": ("corvano_naked", "knurl", "weighted"),
    "lowrise": ("rheinhart_naked", "ribbed", "tapered"),
    "highrise": ("corvano_adventure", "waffle", "weighted"),
    "mx": ("varga_mx", "waffle", "cap"),
    "clipon_low": ("hartwell_cafe", "barrel", "bullet"),
    "clipon_raised": ("kazeda_sport", "knurl", "ringed"),
    "drag": ("kazeda_naked", "ribbed", "ringed"),
    "touring": ("lodestar_cruiser", "touring", "bullet"),
    "tracker": ("varga_naked", "waffle", "tapered"),
    "ace": ("hartwell_classic", "leather", "mirror"),
}


FIT_CAM = (880, -760, 520)      # from ahead, to the right and above: both grips, the clamps and the bodywork behind


def fitted(which=None, samples=40):
    os.makedirs(OUT, exist_ok=True)
    for bar, (mn, grip, end) in FITTED.items():
        if which and bar not in which:
            continue
        fresh()
        b3, m, cc, lines, made = built(mn, bar, grip, end)
        H = lines[-1][3]
        sc = bpy.context.scene
        b3.studio(sc)
        sc.cycles.samples = samples
        sc.render.resolution_x, sc.render.resolution_y = 1400, 1000
        # the bars shot of detail3d, moved to this bike's hands
        tgt = (H[0] + 70, 0, H[2] - 60)
        shoot(sc, os.path.join(OUT, f"fit_{bar}.png"), vadd(tgt, FIT_CAM), tgt, 38)


def fit_one(mn, bar, grip, end, fin, samples=40):
    os.makedirs(OUT, exist_ok=True)
    fresh()
    b3, m, cc, lines, made = built(mn, bar, grip, end, fin)
    H = lines[-1][3]
    sc = bpy.context.scene
    b3.studio(sc)
    sc.cycles.samples = samples
    sc.render.resolution_x, sc.render.resolution_y = 1400, 1000
    tgt = (H[0] + 70, 0, H[2] - 60)
    path = os.path.join(OUT, f"one_{mn}_{bar}_{grip}_{end}.png")
    shoot(sc, path, vadd(tgt, FIT_CAM), tgt, 38)
    print("lock", lock_check(m, cc))
    print("clips", {k: v for k, v in clip_check(made).items() if not joint_ok(k)})
    return path


if __name__ == "__main__":
    a = sys.argv[1:]

    def opt(k, dflt=None):
        return a[a.index(k) + 1] if k in a else dflt
    cmd = a[0] if a else "parts"
    rest = [x for x in a[1:] if not x.startswith("--") and x not in [opt(k) for k in ("--grip", "--end", "--finish", "--samples", "--fam")]]
    smp = int(opt("--samples", 40))
    if cmd == "parts":
        parts(rest or None, smp)
    elif cmd == "fitted":
        fitted(rest or None, smp)
    elif cmd == "fit":
        print(fit_one(rest[0], rest[1], opt("--grip", "knurl"), opt("--end", "weighted"), opt("--finish"), smp))
    elif cmd == "check":
        fams = opt("--fam")
        check(fams.split(",") if fams else None, rest or None)
    elif cmd == "hands":
        print(json.dumps(hands_table(), indent=1))
