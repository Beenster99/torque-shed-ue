"""Common parts: fog lights (beenster, 17 Nov: "add fog lights to the common part library, a few variations").

A pair of lamps clamped to the fork's upper tubes, one outside each leg, at the height between the lower
yoke and the slider top. They turn with the steering, stay clear of the front tyre (outside its width) and of
the headlamp and its indicators (below them). Each lamp sits on a short arm from a split clamp on the tube.
The game finds the lit faces as its fog lamps (the glowing emitter is a "lampdip" surface: lens_white in the
fog_lights slot) and switches them on the usual light toggle: off, dipped, high, high + fog.

Our own designs, with real lamps as reference only: no maker's name, logo or exact product.

    python3 foundry/foglamps3d.py shots [keys] [--samples N]   # out/foglamps/*.png
    python3 foundry/foglamps3d.py check                         # every lamp on the lead bike of each family it suits
"""
import json
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bars3d as BR  # noqa: E402
import detail3d as D  # noqa: E402
import lamps3d as LM  # noqa: E402

B = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "foglamps")

FOGS = {
    "chrome_spot": dict(label="Chrome spot lamps", fits=["Classic", "Cafe", "Cruiser", "Chopper"], stock="halogen_spots",
                        note="Deep chrome bowls with a rolled rim, a silvered reflector and a bulb behind clear domed glass, on chrome arms."),
    "amber_round": dict(label="Amber fog lamps", fits=["Classic", "Cafe", "Cruiser", "Chopper"], stock="amber_fogs",
                        note="Black bowls with a chrome rim and a yellow-amber glass: the old-school fog lamp."),
    "rally": dict(label="Rally lamps", fits=["Naked", "Adventure", "Classic"], stock="rally_lamps",
                  note="Bigger black round lamps with a satin rim, a reflector and a projector: for the long dark lanes."),
    "led_cube": dict(label="LED cube pods", fits=["Naked", "Adventure", "MX"], stock="led_pods",
                     note="Small square black pods with cooling fins at the back and four LEDs behind a clear face."),
    "slim_bar": dict(label="Slim LED bars", fits=["Naked", "Adventure", "MX"], stock="light_bar",
                     note="A short slim LED bar each side, black with a light strip across a clear face."),
}

TUBE_Y = 105.0      # the fork tubes' centres either side (as lamps3d's headlamp ears)


def use(b):
    global B
    B = b
    D.B = b
    BR.use(b)
    LM.use(b)


def mats():
    M = LM.mats()
    M["fogglass"] = D.glass_mat("fogamberglass", (1.0, 0.72, 0.12))
    return M


def fork_point(c, t, n=0.0):
    """The fork axis at t mm along it from the front axle (side view x, z), n back from it."""
    F, uf, nb = c["F"], c["uf"], c["nb"]
    return (F[0] + uf[0] * t + nb[0] * n, F[1] + uf[1] * t + nb[1] * n)


def clamp_t(model):
    """How far up the fork from the axle the clamps go: on the upper tube, between the slider top and the lower yoke."""
    fk = model["fork"]
    lo, hi = fk.get("slider_top", 300) + 45, fk["lower_yoke"] - 45
    return max(lo, min(hi, (lo + hi) / 2 + 30))


def build_lamp(key, s, at, M):
    """One lamp facing forward, its face's centre at (x, y, z). Returns the point its arm meets at the back."""
    x, y, z = at
    if key in ("chrome_spot", "amber_round", "rally"):
        R, depth = {"chrome_spot": (42.0, 52), "amber_round": (44.0, 48), "rally": (56.0, 58)}[key]
        house = M["chrome"] if key == "chrome_spot" else M["black"]
        rim = M["chrome"] if key != "rally" else M["satin"]
        glass = M["fogglass"] if key == "amber_round" else M["glass"]
        ol = LM.circle(R)
        LM.loft(f"fog{s}house", at, 1, ol, [(-depth, 0.5), (-depth * 0.75, 0.78), (-depth * 0.35, 0.96), (-4, 1.0), (0, 1.0)], house, (True, False))
        LM.loft(f"fog{s}rim", at, 1, ol, [(-3, 1.035), (2, 1.04), (4.5, 1.0), (4.5, 0.92), (0, 0.9)], rim, (False, False))
        LM.loft(f"fog{s}bowl", at, 1, ol, [(0.5, 0.9), (-depth * 0.25, 0.72), (-depth * 0.45, 0.42), (-depth * 0.5, 0.15)], M["silver"], (False, True))
        LM.loft(f"fog{s}glass", at, 1, ol, [(3, 0.91), (5, 0.84), (7.5, 0.6), (8.5, 0.3), (9, 0.06)], glass, (False, True))
        if key == "rally":
            LM.projector(f"fog{s}proj", (x - 4, y, z), 1, 24.0, M)
        else:
            LM.bulb(f"fog{s}bulb", (x - depth * 0.42, y, z), 1, M, 8)
        if key == "amber_round":
            # (the glow behind the amber glass: the lamp the game lights)
            D.lathe(f"fog{s}dip", (x - 2, y, z), (1, 0, 0), [(0, 0), (0, R * 0.55), (0.8, R * 0.55), (0.8, 0)], M["dip"], 32)
        return (x - depth * 0.8, y, z), house
    if key == "led_cube":
        h = 30.0
        ol = D.sell(h, h, 1.0, 5.0, 48)
        LM.loft(f"fog{s}house", at, 1, ol, [(-48, 0.92), (-44, 1.0), (-2, 1.0), (0, 1.0)], M["black"], (True, False))
        LM.loft(f"fog{s}bezel", at, 1, ol, [(-1, 1.04), (2.5, 1.04), (2.5, 0.9), (0, 0.88)], M["satin"], (False, False))
        LM.plate(f"fog{s}back", (x - 3, y, z), 1, D.sell(h * 0.86, h * 0.86, 1.0, 5.0, 40), -1, 0, M["black"])
        for dy in (-12, 12):
            for dz in (-12, 12):
                D.lathe(f"fog{s}led{dy}{dz}", (x - 3, y + dy, z + dz), (1, 0, 0), [(0, 0), (0, 9), (1.5, 9), (4, 6), (5, 0)], M["dip"], 20)
        LM.plate(f"fog{s}face", at, 1, D.sell(h * 0.88, h * 0.88, 1.0, 5.0, 40), 1.5, 2.5, M["glass"])
        for k in range(5):
            # the fins on the back
            fx = x - 48 - 3 - k * 5
            B.prism(f"fog{s}fin{k}", [(fx - 1.2, z - h * 0.8), (fx + 1.2, z - h * 0.8), (fx + 1.2, z + h * 0.8), (fx - 1.2, z + h * 0.8)],
                    y - h * 0.8, y + h * 0.8, M["black"], 0.4)
        return (x - 60, y, z), M["black"]
    if key == "slim_bar":
        hy, hz = 62.0, 17.0
        ol = D.sell(hy, hz, 1.0, 6.0, 56)
        LM.loft(f"fog{s}house", at, 1, ol, [(-34, 0.86), (-30, 1.0), (-2, 1.0), (0, 1.0)], M["black"], (True, False))
        LM.loft(f"fog{s}bezel", at, 1, ol, [(-1, 1.03), (2, 1.03), (2, 0.92), (0, 0.9)], M["satin"], (False, False))
        LM.plate(f"fog{s}back", (x - 3, y, z), 1, D.sell(hy * 0.92, hz * 0.8, 1.0, 6.0, 48), -1, 0, M["black"])
        LM.plate(f"fog{s}strip", (x - 2, y, z), 1, D.sell(hy * 0.84, hz * 0.4, 1.0, 6.0, 48), 0, 1.5, M["dip"])
        LM.plate(f"fog{s}face", at, 1, D.sell(hy * 0.94, hz * 0.84, 1.0, 6.0, 48), 1.5, 2.5, M["glass"])
        return (x - 40, y, z), M["black"]
    raise KeyError(key)


def lamp_size(key):
    """(half width across, how far it stands out in front of its arm)."""
    return {"chrome_spot": (42, 52), "amber_round": (44, 48), "rally": (56, 58), "led_cube": (30, 60), "slim_bar": (62, 40)}[key]


def fit(b, model, c, fog=None):
    """Fit a pair of fog lamps; returns a report and the names made."""
    use(b)
    M = mats()
    before = set(bpy.data.objects.keys())
    rep = dict(model=model.get("name", ""), family=model["family"])
    if not fog:
        return rep, []
    t = clamp_t(model)
    fx, fz = fork_point(c, t)
    hit = D.side_hit(("forktube", "forkouter", "fork"), fx, fz, 1)
    rf = max(18.0, (abs(hit[1]) - TUBE_Y) if hit is not None else 24.0)
    rf = min(rf, 40.0)
    half, depth = lamp_size(fog)
    d = (c["uf"][0], 0, c["uf"][1])
    for s in (-1, 1):
        ty = s * TUBE_Y
        # the split clamp round the tube, its bolt at the back
        cl = M["chrome"] if fog in ("chrome_spot",) else M["satin"]
        D.lathe(f"fogclamp{s}", (fx, ty, fz), d, [(-14, rf + 0.5), (-14, rf + 5.5), (14, rf + 5.5), (14, rf + 0.5), (-14, rf + 0.5)], cl, 36, caps=False)
        D.bolt(f"fogclampbolt{s}", (fx - rf - 5, ty, fz), (-1, 0, 0), 4.5, 4, M["steel"])
        # the lamp: outside the tube, its back just ahead of the clamp's front
        ly = s * (TUBE_Y + rf + 10 + half)
        lx = fx + rf + depth * 0.55
        lz = fz - 6
        back, _ = build_lamp(fog, s, (lx, ly, lz), M)
        # the arm: from the clamp's outside, out and forward to a pivot under the lamp's back
        y0 = s * (TUBE_Y + rf + 4)
        knee = (fx + rf * 0.4, s * (TUBE_Y + rf + 10 + half * 0.5), lz - 18)
        under = (back[0] + 8, ly, lz - (half if fog != "slim_bar" else 17) * 0.75)
        B.smooth_tube(f"fogarm{s}", [(fx + 2, y0, fz - 10), knee, under], 6.0, cl)
        D.lathe(f"fogpivot{s}", under, (0, 0, 1), [(-4, 0), (-4, 9), (8, 9), (8, 0)], cl, 20)
    made = [o for o in bpy.data.objects.keys() if o not in before]
    rep["fog"] = dict(key=fog, clamp_t=round(t), clamp=[round(fx), round(fz)], tube_r=round(rf, 1))
    return rep, made


def built(mn, fog=None):
    import build3d
    use(build3d)
    BR.fresh()
    m = BR.models()[mn]
    build3d.DETAIL["on"] = True
    build3d.build(m)
    top, F, uf = BR.model_top(m)
    rake = math.radians(m["geometry"]["rake"])
    cc = dict(F=F, uf=uf, nb=(-math.cos(rake), -math.sin(rake)), fk=m["fork"], top=top)
    use(build3d)
    rep, made = fit(build3d, m, cc, fog)
    return build3d, m, cc, rep, made


OK = ("fogclamp | fork", "fogclampbolt | fogclamp", "fogarm | fogclamp", "fogpivot | fogarm", "fogpivot | fog", "fogarm | fog",
      "fog | fog")


def ok_pair(pair):
    a, b_ = [x.strip() for x in pair.split("|")]
    for j in OK:
        ja, jb = [x.strip() for x in j.split("|")]
        if (a.startswith(ja) and b_.startswith(jb)) or (b_.startswith(ja) and a.startswith(jb)):
            return True
    return False


def leads():
    ld = dict(BR.LEADS)
    ld["Chopper"] = "lodestar_chopper"
    return ld


def check(keys=None):
    res = {}
    for key in keys or FOGS:
        for fam in FOGS[key]["fits"]:
            mn = leads()[fam]
            b3, m, cc, r, made = built(mn, key)
            bad = {k: v for k, v in BR.clip_check(made).items() if not ok_pair(k)}
            res.setdefault(key, {})[fam] = dict(model=mn, clips=bad, rep=r.get("fog"))
            print(f"FCHECK {key:12s} {fam:10s} {bad}", flush=True)
    os.makedirs(OUT, exist_ok=True)
    json.dump(res, open(os.path.join(OUT, "check.json"), "w"), indent=1)
    return res


SHOTS = [("chrome_spot", "hartwell_classic"), ("amber_round", "lodestar_cruiser"), ("rally", "corvano_adventure"),
         ("led_cube", "corvano_naked"), ("slim_bar", "varga_mx")]


def shots(which=None, samples=40):
    os.makedirs(OUT, exist_ok=True)
    for key, mn in SHOTS:
        if which and key not in which:
            continue
        b3, m, cc, r, made = built(mn, key)
        fx, fz = r["fog"]["clamp"]
        tgt = (fx + 40, 0, fz + 40)
        LM.render_at(b3, os.path.join(OUT, f"{key}_{mn}.png"), (fx + 1500, -1100, fz + 450), tgt, 40, samples=samples)
        LM.render_at(b3, os.path.join(OUT, f"{key}_{mn}_night.png"), (fx + 2600, -1400, fz + 500), (fx, 0, fz), 40, samples=samples, dark=True)


if __name__ == "__main__":
    a = sys.argv[1:]
    cmd = a[0] if a else "shots"
    smp = int(a[a.index("--samples") + 1]) if "--samples" in a else 40
    rest = [x for x in a[1:] if not x.startswith("--") and x != str(smp)]
    if cmd == "shots":
        shots(rest or None, smp)
    elif cmd == "check":
        check(rest or None)
