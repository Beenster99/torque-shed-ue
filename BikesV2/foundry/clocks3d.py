"""Common parts, stage 4: clock and screen housings (common parts plan, 16 Nov).

The joint (docs/common_parts_joints.md): the housing takes the bike's dash box, found from the bike's own
clocks or display, with its face square to the rider's eyes (eye at hip + 80 mm, 1560 mm up, as the
signed-off display uses), on two stays from the top yoke. Bikes with no dash (MX) take the small unit on a
stalk above the bar clamp.

Housings only: what a screen shows comes from the screens thread (the TFT faces are drawn in game), so a
screen here is dark glass with its warning lights in the chin. The round clocks keep a plain dial.

Our own designs with real parts as reference only: no maker's name, logo or exact product ships.

    python3 foundry/clocks3d.py shots [keys] [--samples N]   # out/clocks/*.png: each housing on a bike
    python3 foundry/clocks3d.py check                         # every housing on the lead bike of each family
"""
import json
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bars3d as BR  # noqa: E402
import detail3d as D  # noqa: E402
import lamps3d as L  # noqa: E402

B = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "clocks")

HOUSINGS = {
    "twin_round": dict(label="Twin round clocks", fams=("Classic", "Cafe", "Cruiser", "Chopper"),
                       note="A speedometer and a rev counter in deep cups with chrome bezels, on one bridge plate, the warning lights in a slim pod between them."),
    "single_round": dict(label="Single round clock", fams=("Naked", "Classic", "Cafe", "Cruiser", "Chopper"),
                         note="One large speedometer in a deep cup, four warning lights set into the lower dial. It stands 30 mm higher than the bike's own clocks so the rider sees all of it over the bar.", raise_=30),
    "round_screen": dict(label="Round screen", fams=("Naked", "Classic", "Cafe", "Cruiser"),
                         note="A round screen in a satin black cup with a thin bright ring and a short visor over the top, for modern classics."),
    "wide_screen": dict(label="Wide screen", fams=("Naked", "Sport", "Adventure"),
                        note="A landscape screen in a tapered housing with a hood over the top, warning lights and two buttons in the chin."),
    "tall_screen": dict(label="Tall screen", fams=("Naked", "Adventure"),
                        note="A portrait screen in the same style, the rally look, with a deeper hood."),
    "bar_unit": dict(label="Small bar unit", fams=("Naked", "Cafe", "Chopper", "MX"),
                     note="A small screen on a stalk above the bar clamp, for bikes with no dash or a bare front end."),
}
OLD = ("clock", "dash")


def use(b):
    global B
    B = b
    L.use(b)


def mats():
    M = L.mats()
    M.update(
        screen=D.surf("ck_screen", (0.004, 0.005, 0.008), 0.0, 0.04, coat=1.0, wear=0.0),
        dial=D.surf("dialblack", (0.012, 0.012, 0.013), 0.0, 0.6, wear=0.0),
        tick=D.glow("dialwhite", (0.9, 0.9, 0.86), 0.5), red=D.glow("dialred", (1.0, 0.05, 0.03), 0.8),
        needle=D.glow("needle", (1.0, 0.32, 0.02), 1.2),
        tg=D.glow("ck_green", (0.1, 1.0, 0.2), 1.5), tb=D.glow("ck_blue", (0.15, 0.35, 1.0), 1.5),
        ta=D.glow("ck_amber", (1.0, 0.45, 0.02), 1.5), tr=D.glow("ck_red", (1.0, 0.05, 0.03), 1.5),
        screenglow=D.glow("ck_boot", (0.25, 0.55, 0.9), 0.25),
    )
    return M


# ---------------------------------------------------------------- frame and shapes
class Frame:
    """The housing's frame: O the face centre (x, z), n the face normal (towards the eyes), u up in the face."""

    def __init__(self, O, n, u):
        self.O, self.n, self.u = O, n, u

    def p(self, t, y, v):
        return (self.O[0] + self.n[0] * t + self.u[0] * v, y, self.O[1] + self.n[1] * t + self.u[1] * v)

    def ax(self):
        return (self.n[0], 0.0, self.n[1])


def tloft(name, f, yc, outline, stations, mat, close=(True, True), sharp=False):
    """A shell through an outline [(y, v)] in the face plane, at stations [(t, scale)] or [(t, scale, dv)]."""
    m = len(outline)
    verts, faces = [], []
    for st in stations:
        t, k = st[0], st[1]
        dv = st[2] if len(st) > 2 else 0.0
        verts += [B.V(*f.p(t, yc + y * k, v * k + dv)) for y, v in outline]
    for j in range(len(stations) - 1):
        for i in range(m):
            i2 = (i + 1) % m
            faces.append((j * m + i, j * m + i2, (j + 1) * m + i2, (j + 1) * m + i))
    if close[0]:
        faces.append(tuple(range(m - 1, -1, -1)))
    if close[1]:
        faces.append(tuple((len(stations) - 1) * m + i for i in range(m)))
    ob = B.mesh_obj(name, verts, faces, mat, True, 0, sharp)
    B.bm_fix_normals(ob)
    return ob


def flat(name, f, yc, poly, t0, t1, mat, bevel=0.0):
    """A plate of the outline [(y, v)] between t0 and t1."""
    return D.slab(name, f.O, f.n, f.u, [(yc + y, v) for y, v in poly], t0, t1, mat, bevel)


def dot(name, f, y, v, t, r, mat):
    D.lathe(name, f.p(t, y, v), f.ax(), [(-1.5, 0), (-1.5, r), (0.6, r), (1.2, r * 0.6), (1.4, 0)], mat, 16, sharp=False)


def arc(r0, r1, a0, a1, m=32):
    """A band between radii r0 and r1 from angle a0 to a1 (degrees, 0 = rider's right, 90 = up)."""
    a = [math.radians(a0 + (a1 - a0) * k / m) for k in range(m + 1)]
    return [(-r1 * math.cos(w), r1 * math.sin(w)) for w in a] + [(-r0 * math.cos(w), r0 * math.sin(w)) for w in reversed(a)]


def top_band(hy, hz, n, lift, thick, m=40, span=0.92):
    """A band over the top edge of a superellipse hy x hz: the visor of a screen housing."""
    pts = []
    for k in range(m + 1):
        y = -hy * span + 2 * hy * span * k / m
        q = abs(y) / hy
        pts.append((y, hz * (max(0.0, 1 - q ** n)) ** (1 / n)))
    return [(y, v + lift + thick) for y, v in pts] + [(y, v + lift) for y, v in reversed(pts)]


def dial(name, f, yc, r, M, red=False, lights=False):
    """A plain dial: black face, minute marks round a 270 degree sweep, an orange needle on a chrome hub."""
    flat(name + "face", f, yc, L.circle(r, 48), -1.0, 0.0, M["dial"])
    for k in range(28):
        a = math.radians(-135 + 270 * k / 27)
        big = k % 3 == 0
        w, r0, r1 = (0.9 if big else 0.45), r * (0.74 if big else 0.83), r * 0.93
        ca, sa = math.cos(a), math.sin(a)
        q = [(-r0 * sa + w * ca, r0 * ca + w * sa), (-r1 * sa + w * ca, r1 * ca + w * sa), (-r1 * sa - w * ca, r1 * ca - w * sa), (-r0 * sa - w * ca, r0 * ca - w * sa)]
        flat(f"{name}tick{k}", f, yc, q, 0.0, 0.4, M["red"] if red and k >= 23 else M["tick"])
    a = math.radians(-135 + 270 * 0.06)
    ca, sa = math.cos(a), math.sin(a)
    r0, r1, w = -r * 0.18, r * 0.84, 1.1
    flat(name + "needle", f, yc, [(-r0 * sa + w * ca, r0 * ca + w * sa), (-r1 * sa + w * ca, r1 * ca + w * sa), (-r1 * sa - w * ca, r1 * ca - w * sa), (-r0 * sa - w * ca, r0 * ca - w * sa)], 1.2, 1.8, M["needle"])
    D.lathe(name + "hub", f.p(0, yc, 0), f.ax(), [(0, 0), (0, r * 0.15), (2.6, r * 0.13), (3.0, 0)], M["chrome"], 24)
    if lights:
        for j, key in enumerate(("tg", "tb", "ta", "tr")):
            dot(f"{name}tell{j}", f, yc - 18 + 12 * j, -r * 0.52, 0.0, 2.6, M[key])


def cup(name, f, yc, r, depth, M, shell, bezel):
    """A deep round cup with a proud bezel and flat glass; its face at t = 0."""
    D.lathe(name + "cup", f.p(0, yc, 0), f.ax(), [(-depth, 0), (-depth, r * 0.72), (-depth + 5, r * 0.94), (-depth + 12, r + 2.5), (-2, r + 2.5), (-2, 0)], shell, 56, sharp=False)
    D.lathe(name + "bezel", f.p(0, yc, 0), f.ax(), [(-4, r + 2.0), (-4, r + 3.6), (1.5, r + 3.8), (4.0, r + 2.2), (4.0, r - 1.2), (1.0, r - 1.2), (1.0, r)], bezel, 56, sharp=False, caps=False)
    D.lathe(name + "glass", f.p(0, yc, 0), f.ax(), [(2.4, 0), (2.4, r - 0.6), (3.2, r - 0.6), (3.2, 0)], M["glass"], 48)


def screen_box(name, f, ol, depth, M, hood, glass_k=(0.88, 0.8), lift=4.0, chin=True, sy=None):
    """A screen housing: a shell tapering to the back, a closed front, dark glass sunk in it, a hood over the top
    and the warning lights (and two buttons) in the chin. ol is the outline [(y, v)]; returns its half sizes."""
    hy = max(abs(y) for y, v in ol)
    hz = max(abs(v) for y, v in ol)
    tloft(name + "house", f, 0, ol, [(-depth, 0.62), (-depth * 0.7, 0.86), (-depth * 0.3, 0.98), (-1.5, 1.0), (0, 0.985)], M["satin"], (True, True))
    gy, gz = hy * glass_k[0], hz * glass_k[1]
    g = [(y * gy / hy, v * gz / hz) for y, v in ol]
    flat(name + "glass", f, 0, [(y, v + lift) for y, v in g], -0.4, 0.6, M["screen"])
    flat(name + "boot", f, 0, [(y * 0.6, v * 0.08 + lift) for y, v in g], 0.6, 0.8, M["screenglow"])
    if hood:
        n_ = 4.0
        flat(name + "hood", f, 0, top_band(hy, hz, n_, -1.0, 3.0), -6, hood, M["satin"], 1.0)
    if chin:
        cv = -(hz + (lift - gz)) / 2 - 0.5
        for j, key in enumerate(("tg", "tb", "ta", "tr")):
            dot(f"{name}tell{j}", f, -16 + 10.5 * j, cv, 0.0, 2.2, M[key])
        for s in (-1, 1):
            D.lathe(f"{name}button{s}", f.p(0, s * (sy or hy * 0.72), cv), f.ax(), [(-1, 0), (-1, 4.2), (2.2, 4.0), (2.8, 0)], M["rubber"], 20, sharp=False)
    return hy, hz


# ---------------------------------------------------------------- the housings
def build_housing(key, f, M):
    """Build one housing in the frame f. Returns the points [(t, y, v)] where its two stays meet it."""
    nm = "ck" + key
    if key == "twin_round":
        r, dp = 37.0, 34.0
        yc = r + 9
        for s in (-1, 1):
            cup(f"{nm}{s}", f, s * yc, r, dp, M, M["chrome"] if M.get("_chrome") else M["satin"], M["chrome"])
            dial(f"{nm}dial{s}", f, s * yc, r - 2, M, red=s < 0)
        bh = r * 0.62
        flat(nm + "bridge", f, 0, D.sell(yc + r * 0.7, bh, 1.0, 4.0, 48), -dp - 6, -dp + 8, M["satin"], 2.0)
        flat(nm + "pod", f, 0, D.sell(4.5, 17, 1.0, 3.0, 24), -dp + 6, -3, M["satin"], 1.0)
        for j, key_ in enumerate(("tg", "ta", "tr")):
            dot(f"{nm}tell{j}", f, 0, -12 + 12 * j, -3, 2.3, M[key_])
        return [(-dp - 4, s * 30, 0) for s in (-1, 1)]
    if key == "single_round":
        r, dp = 50.0, 38.0
        cup(nm, f, 0, r, dp, M, M["chrome"] if M.get("_chrome") else M["satin"], M["chrome"])
        dial(nm + "dial", f, 0, r - 2, M, lights=True)
        return [(-dp + 1, s * 18, 0) for s in (-1, 1)]
    if key == "round_screen":
        r, dp = 48.0, 30.0
        D.lathe(nm + "cup", f.p(0, 0, 0), f.ax(), [(-dp, 0), (-dp, r * 0.66), (-dp + 6, r * 0.92), (-dp + 14, r + 3), (0, r + 3), (0, 0)], M["satin"], 56, sharp=False)
        D.lathe(nm + "ring", f.p(0, 0, 0), f.ax(), [(-1, r + 0.4), (-1, r + 2.6), (1.2, r + 2.6), (1.2, r + 0.4)], M["silver"], 56, caps=False)
        flat(nm + "glass", f, 0, L.circle(r - 2, 56), -0.4, 0.6, M["screen"])
        flat(nm + "boot", f, 0, arc(r * 0.62, r * 0.7, 210, 330), 0.6, 0.8, M["screenglow"])
        flat(nm + "hood", f, 0, arc(r + 2.4, r + 5.2, 25, 155), -8, 13, M["satin"], 1.0)
        for j, key_ in enumerate(("tg", "tb", "ta", "tr")):
            dot(f"{nm}tell{j}", f, -15 + 10 * j, -r * 0.8, 0.6, 2.2, M[key_])
        return [(-dp + 1, s * 20, 0) for s in (-1, 1)]
    if key == "wide_screen":
        screen_box(nm, f, D.sell(72, 44, 1.0, 4.2, 56), 32, M, 14)
        return [(-31, s * 30, 0) for s in (-1, 1)]
    if key == "tall_screen":
        screen_box(nm, f, D.sell(46, 62, 1.0, 4.2, 56), 32, M, 20, glass_k=(0.84, 0.82), lift=5.0, sy=33)
        return [(-31, s * 22, 0) for s in (-1, 1)]
    if key == "bar_unit":
        screen_box(nm, f, D.sell(38, 23, 1.0, 4.0, 48), 20, M, 8, glass_k=(0.84, 0.66), lift=4.0, chin=False)
        for j, key_ in enumerate(("tg", "ta", "tr")):
            dot(f"{nm}tell{j}", f, -9 + 9 * j, -17.5, 0.0, 1.8, M[key_])
        return [(-19, 0, -6)]
    raise KeyError(key)


def _touches(mine, prefixes):
    """Whether any of our parts overlaps a bike part whose name starts with one of the prefixes."""
    from mathutils.bvhtree import BVHTree
    dg = bpy.context.evaluated_depsgraph_get()

    def tree(o):
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        t = BVHTree.FromPolygons([ev.matrix_world @ v.co for v in me.vertices], [tuple(p.vertices) for p in me.polygons])
        ev.to_mesh_clear()
        return t
    ours = [tree(bpy.data.objects[n]) for n in mine if bpy.data.objects[n].type == "MESH"]
    for o in bpy.data.objects:
        if o.type != "MESH" or o.name in mine or not o.name.startswith(prefixes):
            continue
        t = tree(o)
        if any(a.overlap(t) for a in ours):
            return True
    return False


def frame_at(dc, c):
    eye = (c["model_hip"] + 80, 1560)
    L_ = math.hypot(eye[0] - dc[0], eye[1] - dc[1])
    n = ((eye[0] - dc[0]) / L_, (eye[1] - dc[1]) / L_)
    u = (n[1], -n[0])
    if u[1] < 0:
        u = (-u[0], -u[1])
    return Frame(dc, n, u)


def fit(b, model, c, key):
    """Swap the bike's clocks or display for one of ours. Returns a report and the names of the parts made."""
    use(b)
    M = mats()
    fam = model["family"]
    before = set(bpy.data.objects.keys())
    rep = dict(model=model.get("name", ""), family=fam, key=key)
    if fam not in HOUSINGS[key]["fams"]:
        return rep, []
    c = dict(c)
    c.setdefault("model_hip", model["geometry"].get("hip_x", 520))
    M["_chrome"] = fam in ("Classic", "Cruiser", "Chopper")
    F, uf, nb, fk = c["F"], c["uf"], c["nb"], c["fk"]
    yk = B.add(B.add(F, uf, fk["length"] - 12), nb, -34)
    face = L._bbox(("clockface", "dashglass"))
    box = L._bbox(OLD)
    if key == "bar_unit":
        # on a stalk from the top yoke's middle, standing above the bar clamp
        (rx, rz), _ = BR.clamp_point(model, c)
        top = c["top"]
        dc = (rx - 6, max(rz, top[1]) + 58)
        if box is not None:
            lo, hi = box
            dc = (min(dc[0], (lo[0] + hi[0]) / 2), max(dc[1], lo[2] + 20))
        D.drop(*OLD)
        f = frame_at(dc, c)
        st = build_housing(key, f, M)
        t, y, v = st[0]
        a = f.p(t, y, v)
        foot = (top[0] - 4, 0, top[1] - 4)
        B.smooth_tube("ckstalk", [foot, (foot[0] * 0.6 + a[0] * 0.4 - 4, 0, foot[2] * 0.5 + a[2] * 0.5), (a[0], 0, a[2])], 6.5, M["dark"])
        D.lathe("ckstalkfoot", foot, (0, 0, 1), [(-4, 0), (-4, 14), (4, 12), (6, 0)], M["dark"], 24)
        rep.update(face=[round(dc[0]), round(dc[1])])
    else:
        if box is None:
            return rep, []
        lo, hi = box
        if face is not None:
            flo, fhi = face
            dc = ((flo[0] + fhi[0]) / 2, (flo[2] + fhi[2]) / 2)
        else:
            dc = ((lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2)
        D.drop(*OLD)
        # a deeper housing than the bike's own can reach the top yoke: raise it until it stands clear
        # (under a fairing, first try sliding it forward towards the screen, so it stays inside the nose)
        base = HOUSINGS[key].get("raise_", 0)
        tries = sorted(((up, fw) for up in range(0, 64, 4) for fw in (range(0, 44, 4) if fam == "Sport" else (0,))), key=lambda q: q[0] + 0.5 * q[1])
        for lift, fwd in tries:
            mark = set(bpy.data.objects.keys())
            f = frame_at((dc[0] + fwd, dc[1] + lift), c)
            st = build_housing(key, f, M)
            mine = [o for o in bpy.data.objects.keys() if o not in mark]
            if not _touches(mine, ("yoke", "fork", "lamp", "lph", "bar", "screen", "advscreen", "nose", "fairing")):
                break
            for o in mine:
                bpy.data.objects.remove(bpy.data.objects[o], do_unlink=True)
        if base:
            # then higher still, where a housing should read over the bar (beenster: C2 raised so the rider sees it)
            for o in mine:
                bpy.data.objects.remove(bpy.data.objects[o], do_unlink=True)
            lift += base
            f = frame_at((dc[0] + fwd, dc[1] + lift), c)
            st = build_housing(key, f, M)
        rep["lift"], rep["forward"] = lift, fwd
        dc = (dc[0] + fwd, dc[1] + lift)
        for i, (t, y, v) in enumerate(st):
            a = f.p(t, y, v)
            sg = 1 if y > 0 else -1
            B.smooth_tube(f"ckstay{sg}", [(yk[0], sg * 40, yk[1]), ((yk[0] + a[0]) / 2 + 6, (sg * 40 + a[1]) / 2, (yk[1] + a[2]) / 2), a], 5.5, M["dark"])
        rep.update(face=[round(dc[0]), round(dc[1])], own_box=[[round(x) for x in lo], [round(x) for x in hi]])
    made = [o for o in bpy.data.objects.keys() if o not in before]
    return rep, made


def built(mn, key, lamps=None):
    import build3d
    b3, m, cc, r, made0 = L.built(mn, **(lamps or {}))
    use(build3d)
    rep, made = fit(build3d, m, cc, key)
    return build3d, m, cc, rep, made


# ---------------------------------------------------------------- pictures and the check
SHOTS = [
    ("twin_round", "hartwell_classic", dict(head="round_big", ind="chrome_short", tail="classic")),
    ("single_round", "lodestar_chopper", dict(head="oval", ind="bullet", tail="round")),
    ("round_screen", "hartwell_cafe", dict(head="round_small", ind="chrome_short", tail="oval")),
    ("wide_screen", "kazeda_sport", dict()),
    ("tall_screen", "corvano_adventure", dict()),
    ("bar_unit", "varga_mx", dict()),
]


def shots(which=None, samples=40):
    os.makedirs(OUT, exist_ok=True)
    rep = {}
    for key, mn, lamps in SHOTS:
        if which and key not in which:
            continue
        b3, m, cc, r, made = built(mn, key, lamps)
        rep[key] = r
        c_ = L._centre(("ck" + key,))
        if not c_:
            print("NO HOUSING", mn, key)
            continue
        ctr = c_[0]
        # from over the rider's right shoulder, and a three-quarter view from the front
        tgt = (ctr[0] - 10, 0, ctr[2] - 10)
        L.render_at(b3, os.path.join(OUT, f"{key}.png"), (m["geometry"].get("hip_x", 520) + 40, -260, 1640), tgt, 60, samples=samples)
        tgt2 = (ctr[0] - 30, 0, ctr[2] - 60)
        L.render_at(b3, os.path.join(OUT, f"{key}_front.png"), BR.vadd(tgt2, (700, -520, 260)), tgt2, 50, res=(900, 700), samples=samples)
    path = os.path.join(OUT, "shots.json")
    old = json.load(open(path)) if os.path.exists(path) else {}
    old.update(rep)
    json.dump(old, open(path, "w"), indent=1)


OK = ("ckstay | yoke", "ckstay | ck", "ckstalk | yoke", "ckstalk | ck", "ckstalkfoot | yoke", "ckstalk | ckstalkfoot", "ck | ck",
      "ckstay | topyoke", "ckstalk | stem", "ckstalkfoot | stem")


def ok_pair(pair):
    a, b_ = [x.strip() for x in pair.split("|")]
    for j in OK:
        ja, jb = [x.strip() for x in j.split("|")]
        if (a.startswith(ja) and b_.startswith(jb)) or (b_.startswith(ja) and a.startswith(jb)):
            return True
    return False


def check(keys=None):
    """Every housing on the lead bike of each family it suits: what clips, and how square the face is to the eyes."""
    res = {}
    leads = dict(BR.LEADS)
    leads["Chopper"] = "lodestar_chopper"
    for fam, mn in leads.items():
        for key, h in HOUSINGS.items():
            if fam not in h["fams"] or (keys and key not in keys):
                continue
            b3, m, cc, r, made = built(mn, key)
            bad = {k: v for k, v in BR.clip_check(made).items() if not ok_pair(k)}
            res.setdefault(key, {})[fam] = dict(model=mn, clips=bad, fitted=bool(made), rep=r)
            print(f"CCHECK {key:13s} {fam:10s} {r.get('face')} {bad}", flush=True)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "check.json")
    old = json.load(open(path)) if (keys and os.path.exists(path)) else {}
    for k, v in res.items():
        old.setdefault(k, {}).update(v)
    json.dump(old, open(path, "w"), indent=1)


if __name__ == "__main__":
    a = sys.argv[1:]
    cmd = a[0] if a else "shots"
    smp = int(a[a.index("--samples") + 1]) if "--samples" in a else 40
    rest = [x for x in a[1:] if not x.startswith("--") and x != str(smp)]
    if cmd == "shots":
        shots(rest or None, smp)
    elif cmd == "check":
        check(rest or None)
