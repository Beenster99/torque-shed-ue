"""Common parts: panniers and top boxes for the adventure bikes (beenster, 17 Nov: "add panniers, top boxes as
common items and only as options for adventure bikes").

Every adventure bike carries the same rear rack and subframe (the family's hidden mounting standard: advrack at
x -182..305, y +-172, z ~900..987 from the rear axle; pillion hangers at x ~380..530, y +-169), so the luggage is
built once on the lead and fits all five. The panniers hang on side frames bolted to the rack and the pillion
hangers, their inner faces outside the widest silencer (y 287) with room to spare; the top box sits on a base
plate on the rack, clear of the seat and the tail light.

Our own designs, with real luggage as reference only: no maker's name, logo or exact product.

    python3 foundry/luggage3d.py shots [keys] [--samples N]   # out/luggage/*.png
    python3 foundry/luggage3d.py check                         # clip check on all five adventure bikes
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
OUT = os.path.join(ROOT, "out", "luggage")

PANNIERS = {
    "alu_silver": dict(label="Aluminium side boxes, silver", stock="adv_alloy_trunks_silver",
                       note="Square brushed aluminium boxes with black corner caps, a lid seam, two latches and a carry handle."),
    "alu_black": dict(label="Aluminium side boxes, black", stock="adv_alloy_trunks_silver",
                      note="The same boxes in black anodised aluminium with silver corner caps."),
    "hard_shell": dict(label="Hard shell side cases", stock="adv_hard_side_cases",
                       note="Rounded dark grey moulded cases, wider at the top, with a brushed panel on the outside."),
    "soft_bags": dict(label="Soft adventure bags", stock="adv_slim_soft_cases",
                      note="Olive canvas bags with a roll top and black straps and buckles, on the same side frames."),
}
TOPBOXES = {
    "alu_top": dict(label="Aluminium top box, silver", stock="adv_alloy_top_55",
                    note="A square brushed aluminium box with black corners, a backrest pad and a red reflector."),
    "alu_top_black": dict(label="Aluminium top box, black", stock="adv_black_top_42",
                          note="The same box in black anodised aluminium."),
    "shell_top": dict(label="Moulded top case", stock="adv_plastic_top_50",
                      note="A rounded dark grey case with a red reflector strip across the back."),
}
FITS = ["Adventure"]
ADVENTURE = ["corvano_adventure", "hartwell_adventure", "kazeda_adventure", "rheinhart_adventure", "varga_adventure"]

IN_Y = 322.0               # the panniers' inner faces (the widest silencer reaches y 287)
P_X, P_Z = (-235.0, 305.0), (470.0, 860.0)   # pannier length and height (side view, mm from the rear axle)
P_W = 185.0                # pannier width
RAIL_Y = IN_Y - 10.0       # the side frame loop, just inside the box
TOP_X, TOP_Y, TOP_Z = (-255.0, 115.0), 188.0, (1004.0, 1295.0)


def use(b):
    global B
    B = b
    D.B = b
    BR.use(b)
    LM.use(b)


def mats():
    M = LM.mats()
    M["alu"] = D.surf("lugalu", (0.8, 0.8, 0.79), 1.0, 0.3, wear=0.0)
    M["alublack"] = D.surf("lugaluk", (0.025, 0.025, 0.028), 1.0, 0.38, wear=0.0)
    M["shell"] = D.surf("lugshell", (0.09, 0.095, 0.1), 0.0, 0.45, coat=0.3, wear=0.0)
    M["canvas"] = D.surf("lugcanvas", (0.2, 0.22, 0.13), 0.0, 0.92, wear=0.0)
    M["strap"] = D.surf("lugstrap", (0.02, 0.02, 0.022), 0.0, 0.8, wear=0.0)
    M["pad"] = D.surf("lugpad", (0.03, 0.03, 0.032), 0.0, 0.7, wear=0.0)
    M["rack"] = M["satin"]
    return M


def box(name, x0, x1, y0, y1, z0, z1, mt, bevel=6.0):
    return B.prism(name, [(x0, z0), (x1, z0), (x1, z1), (x0, z1)], min(y0, y1), max(y0, y1), mt, bevel)


def side_frame(s, M):
    """The pannier frame on side s: a loop on the box's inner face, struts to the rack and the pillion hanger."""
    y = s * RAIL_Y
    (x0, x1), (z0, z1) = P_X, P_Z
    loop = [(x0 + 25, y, z0 + 30), (x1 - 25, y, z0 + 30), (x1 - 25, y, z1 - 40), (x0 + 25, y, z1 - 40)]
    B.tube(f"lugrail{s}", loop, 10.0, M["rack"], cyclic=True)
    # up to the rack's side rail, rear and middle
    B.smooth_tube(f"lugstrut_r{s}", [(x0 + 60, y, z1 - 40), (x0 + 75, s * (RAIL_Y - 70), z1 + 20), (-95, s * 160, 935)], 9.0, M["rack"])
    B.smooth_tube(f"lugstrut_m{s}", [(140, y, z1 - 40), (150, s * (RAIL_Y - 70), z1 + 15), (160, s * 150, 912)], 9.0, M["rack"])
    # forward to the pillion hanger
    B.smooth_tube(f"lugstrut_f{s}", [(x1 - 25, y, z0 + 160), (x1 + 50, s * (RAIL_Y - 50), 640), (392, s * 176, 680)], 9.0, M["rack"])
    for i, (bx, bz) in enumerate([(-95, 935), (160, 912)]):
        D.bolt(f"lugrackbolt{s}{i}", (bx, s * 158, bz), (0, s * 1, 0), 6.0, 6.0, M["steel"])
    D.bolt(f"lughangbolt{s}", (392, s * 172, 680), (0, s * 1, 0), 6.0, 6.0, M["steel"])


def alu_case(name, s, x0, x1, z0, z1, w, body, trim, M, y_in):
    """A square aluminium case: body, lid band, corner caps, latches, handle."""
    y1 = y_in + s * w
    box(f"{name}body", x0, x1, y_in, y1, z0, z1 - 70, body, 7.0)
    box(f"{name}lid", x0 - 4, x1 + 4, y_in - s * 4, y1 + s * 4, z1 - 74, z1, body, 7.0)
    # the lid seam: a dark band under the lid
    box(f"{name}seal", x0 + 2, x1 - 2, y_in + s * 2, y1 - s * 2, z1 - 82, z1 - 72, M["pad"], 1.0)
    c = 38.0
    for cx in (x0 - 5, x1 + 5 - c):
        for cz in (z0 - 5, z1 + 4 - c):
            box(f"{name}corner{int(cx)}{int(cz)}", cx, cx + c, y1 + s * 5 - s * c, y1 + s * 5, cz, cz + c, trim, 9.0)
    for i, lx in enumerate((x0 + 0.28 * (x1 - x0), x0 + 0.72 * (x1 - x0))):
        box(f"{name}latch{i}", lx - 18, lx + 18, y1 - s * 2, y1 + s * 10, z1 - 120, z1 - 64, trim, 3.0)
        D.bolt(f"{name}lock{i}", (lx, y1 + s * 10, z1 - 104), (0, s, 0), 5.0, 3.0, M["chrome"])


def build_pannier(key, s, M):
    (x0, x1), (z0, z1) = P_X, P_Z
    y_in = s * IN_Y
    nm = f"lugp{s}"
    if key in ("alu_silver", "alu_black"):
        body, trim = (M["alu"], M["black"]) if key == "alu_silver" else (M["alublack"], M["silver"])
        alu_case(nm, s, x0, x1, z0, z1, P_W, body, trim, M, y_in)
        # the carry handle on the lid
        hy = y_in + s * P_W * 0.5
        B.smooth_tube(f"{nm}handle", [(x0 + 150, hy, z1), (x0 + 175, hy, z1 + 28), (x1 - 175, hy, z1 + 28), (x1 - 150, hy, z1)], 6.0, M["black"])
    elif key == "hard_shell":
        # wider at the top (a trapezoid side view), rounded all round
        poly = [(x0 + 35, z0), (x1 - 25, z0), (x1, z1 - 60), (x1 - 10, z1), (x0 - 10, z1), (x0, z1 - 60)]
        B.pillow(f"{nm}body", poly, min(y_in, y_in + s * P_W), max(y_in, y_in + s * P_W), M["shell"], inset=26.0)
        # the brushed panel on the outside and a dark seam under the lid
        yo = y_in + s * (P_W + 2)
        B.prism(f"{nm}panel", [(x0 + 70, z0 + 90), (x1 - 60, z0 + 90), (x1 - 40, z1 - 120), (x0 + 50, z1 - 120)],
                min(yo, yo + s * 5), max(yo, yo + s * 5), M["alu"], 4.0)
        box(f"{nm}seam", x0 + 5, x1 - 5, y_in + s * 8, y_in + s * (P_W - 4), z1 - 95, z1 - 86, M["pad"], 1.0)
        for i, lx in enumerate((x0 + 120, x1 - 120)):
            box(f"{nm}latch{i}", lx - 22, lx + 22, yo - s * 2, yo + s * 12, z1 - 92, z1 - 40, M["black"], 4.0)
    else:  # soft bags: canvas with a roll top, straps and buckles, a little narrower and lower
        w = P_W - 25
        poly = [(x0 + 30, z0 + 40), (x1 - 30, z0 + 40), (x1 - 10, z1 - 90), (x0 + 10, z1 - 90)]
        B.pillow(f"{nm}body", poly, min(y_in, y_in + s * w), max(y_in, y_in + s * w), M["canvas"], inset=40.0)
        D.lathe(f"{nm}roll", (x0 + 20, y_in + s * w * 0.5, z1 - 70), (1, 0, 0),
                [(0, 0), (0, 44), (x1 - x0 - 40, 44), (x1 - x0 - 40, 0)], M["canvas"], 28)
        yo = y_in + s * (w + 3)
        for i, sx in enumerate((x0 + 120, x1 - 120)):
            box(f"{nm}strap{i}", sx - 16, sx + 16, yo - s * 1, yo + s * 4, z0 + 70, z1 - 40, M["strap"], 1.0)
            box(f"{nm}buckle{i}", sx - 20, sx + 20, yo + s * 2, yo + s * 10, z0 + 200, z0 + 236, M["black"], 2.0)
    side_frame(s, M)


def build_topbox(key, M):
    (x0, x1), (z0, z1) = TOP_X, TOP_Z
    if key == "shell_top":
        # (the moulded case's rounded belly sits on a thicker base, clear of the taller pillion seats)
        z0, z1 = z0 + 26, z1 + 26
    # the base plate on the rack
    box("lugtopbase", x0 + 60, x1 - 10, -150, 150, 988, z0, M["black"], 3.0)
    for i, (bx, by) in enumerate([(-120, -110), (-120, 110), (60, -110), (60, 110)]):
        D.bolt(f"lugtopbolt{i}", (bx, by, 990), (0, 0, 1), 6.0, 4.0, M["steel"])
    if key in ("alu_top", "alu_top_black"):
        body, trim = (M["alu"], M["black"]) if key == "alu_top" else (M["alublack"], M["silver"])
        box("lugtopbody", x0, x1, -TOP_Y, TOP_Y, z0, z1 - 70, body, 7.0)
        box("lugtoplid", x0 - 4, x1 + 4, -TOP_Y - 4, TOP_Y + 4, z1 - 74, z1, body, 7.0)
        box("lugtopseal", x0 + 2, x1 - 2, -TOP_Y + 2, TOP_Y - 2, z1 - 82, z1 - 72, M["pad"], 1.0)
        c = 38.0
        for cx in (x0 - 5, x1 + 5 - c):
            for sy in (-1, 1):
                for cz in (z0 - 5, z1 + 4 - c):
                    box(f"lugtopcorner{int(cx)}{sy}{int(cz)}", cx, cx + c, sy * (TOP_Y + 5), sy * (TOP_Y + 5 - c), cz, cz + c, trim, 9.0)
        box("lugtoplatch", x0 - 12, x0 + 2, -24, 24, z1 - 130, z1 - 64, trim, 3.0)
        # the red reflector on the back and the pillion's backrest pad on the front
        box("lugtopreflector", x0 - 10, x0 - 2, -70, 70, z0 + 120, z0 + 150, M["red"], 2.0)
    else:
        poly = [(x0 + 20, z0), (x1 - 45, z0), (x1 - 15, z0 + 140), (x1 - 50, z1), (x0 + 40, z1), (x0 - 5, z0 + 140)]
        B.pillow("lugtopbody", poly, -TOP_Y, TOP_Y, M["shell"], inset=45.0)
        box("lugtopseam", x0 + 15, x1 - 15, -TOP_Y + 15, TOP_Y - 15, z0 + 175, z0 + 184, M["pad"], 1.0)
        box("lugtopreflector", x0 - 9, x0 - 1, -110, 110, z0 + 120, z0 + 150, M["red"], 2.0)
    box("lugtoppad", x1 + 2, x1 + 30, -120, 120, z0 + 60, z0 + 220, M["pad"], 10.0)


def fit(b, model, c=None, pannier=None, top=None):
    use(b)
    M = mats()
    before = set(bpy.data.objects.keys())
    if pannier:
        for s in (-1, 1):
            build_pannier(pannier, s, M)
    if top:
        build_topbox(top, M)
    made = [o for o in bpy.data.objects.keys() if o not in before]
    return dict(model=model.get("name", ""), family=model["family"], pannier=pannier, top=top), made


def built(mn, pannier=None, top=None):
    import build3d
    use(build3d)
    BR.fresh()
    m = BR.models()[mn]
    build3d.DETAIL["on"] = True
    build3d.build(m)
    top_, F, uf = BR.model_top(m)
    rake = math.radians(m["geometry"]["rake"])
    cc = dict(F=F, uf=uf, nb=(-math.cos(rake), -math.sin(rake)), fk=m["fork"], top=top_)
    use(build3d)
    rep, made = fit(build3d, m, cc, pannier, top)
    return build3d, m, cc, rep, made


# what may touch: the luggage's own pieces, its frame on the rack and hangers, the base plate on the rack
OK = ("lug | lug", "lugstrut | advrack", "lugrackbolt | advrack", "lugrackbolt | lugstrut", "lugstrut | pillionhanger",
      "lughangbolt | pillionhanger", "lughangbolt | lugstrut", "lugtopbase | advrack", "lugtopbolt | advrack",
      "lugtopbolt | lugtopbase")


def ok_pair(pair):
    a, b_ = [x.strip() for x in pair.split("|")]
    for j in OK:
        ja, jb = [x.strip() for x in j.split("|")]
        if (a.startswith(ja) and b_.startswith(jb)) or (b_.startswith(ja) and a.startswith(jb)):
            return True
    return False


def check(bikes=None):
    res = {}
    for mn in bikes or ADVENTURE:
        for pk in PANNIERS:
            b3, m, cc, r, made = built(mn, pk, "alu_top" if pk == "alu_silver" else ("shell_top" if pk == "hard_shell" else None))
            bad = {k: v for k, v in BR.clip_check(made).items() if not ok_pair(k)}
            res.setdefault(mn, {})[pk] = bad
            print(f"LCHECK {mn:20s} {pk:12s} {bad}", flush=True)
    os.makedirs(OUT, exist_ok=True)
    json.dump(res, open(os.path.join(OUT, "check.json"), "w"), indent=1)
    return res


# (pannier, top box, bike)
SHOTS = [("alu_silver", "alu_top", "corvano_adventure"), ("alu_black", "alu_top_black", "varga_adventure"),
         ("hard_shell", "shell_top", "kazeda_adventure"), ("soft_bags", None, "hartwell_adventure"),
         (None, "alu_top", "rheinhart_adventure")]


def shots(which=None, samples=40):
    os.makedirs(OUT, exist_ok=True)
    for pk, tk, mn in SHOTS:
        tag = f"{pk or 'none'}_{tk or 'none'}_{mn}"
        if which and not any(w in tag for w in which):
            continue
        b3, m, cc, r, made = built(mn, pk, tk)
        tgt = (120, 0, 820)
        LM.render_at(b3, os.path.join(OUT, f"{tag}_rear.png"), (-2700, -2300, 1500), tgt, 40, samples=samples)
        LM.render_at(b3, os.path.join(OUT, f"{tag}_side.png"), (500, -3600, 900), (500, 0, 700), 35, samples=samples)


if __name__ == "__main__":
    a = sys.argv[1:]
    cmd = a[0] if a else "shots"
    smp = int(a[a.index("--samples") + 1]) if "--samples" in a else 40
    rest = [x for x in a[1:] if not x.startswith("--") and x != str(smp)]
    if cmd == "shots":
        shots(rest or None, smp)
    elif cmd == "check":
        check(rest or None)
