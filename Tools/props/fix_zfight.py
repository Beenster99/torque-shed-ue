"""Takes the z-fighting out of the committed prop models (Data/Props/Blender/*.glb) in place, the
same changes make_props.py now makes when Blender rebuilds them (Tools/check_zfight.py found them:
two faces of different materials in one plane flicker against each other).

    python3 Tools/props/fix_zfight.py

Every change sets an absolute size, so running it again changes nothing. Also takes the old flat
"spanner" bars and their hook stubs off the workbench's pegboard: the real tools are their own
model now (Tools/props/make_pegboard_tools.py, prop pegboard_tools)."""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import glb_edit as g   # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BLENDER = os.path.join(ROOT, "Data", "Props", "Blender")
X, Y, Z = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)


def fix(prop, edits):
    path = os.path.join(BLENDER, prop + ".glb")
    j, b = g.read(path)
    for e in edits:
        sel = g.select(j, b, e["mat"], np.array(e["near"]), np.array(e.get("tol", 0.004)), e.get("extent"))
        want = e.get("count")
        if want is not None and len(sel) not in (want if isinstance(want, tuple) else (want,)):
            raise RuntimeError("%s: %s near %s: %d pieces, expected %d" % (prop, e["mat"], e["near"], len(sel), want))
        if e.get("drop"):
            g.drop(j, b, sel)
        for axis, lo, hi in e.get("ranges", []):
            g.set_range(j, b, sel, axis, lo, hi)
    g.write(path, j, b)
    print("%s: %d edits" % (prop, len(edits)))


def blaster(pid, W, H, D):
    # GlassLow/GlassHigh ran the full width, their ends in the sides' outer faces: 3 mm short now.
    # The glass ran exactly to the rails' and bars' ends: 3 mm short at each end now.
    top = H - 0.15
    front_top = 0.72 + 0.43
    glass_z = D / 2 - 0.04 - (top - front_top) * 0.6
    a = np.array((0.0, front_top, D / 2 - 0.02))
    c = np.array((0.0, top + 0.02, glass_z))
    d = (c - a) / np.linalg.norm(c - a)
    return [
        {"mat": "paint_black", "near": tuple(a), "count": 1, "ranges": [(X, -W / 2 + 0.003, W / 2 - 0.003)]},
        {"mat": "paint_black", "near": tuple(c), "count": 1, "ranges": [(X, -W / 2 + 0.003, W / 2 - 0.003)]},
        {"mat": "glass", "near": tuple((a + c) / 2), "count": 1, "tol": 0.01,
         "ranges": [(d, float(a @ d) + 0.003, float(c @ d) - 0.003)]},
    ]


def shelving(W, H, D):
    # The heavy racking's orange beams' fronts (and backs) were flush with the posts': 1 mm in.
    pitch = (H - 0.15) / 4
    out = []
    for level in range(5):
        y = min(0.06 + level * pitch, H - 0.02) - 0.03
        for s in (-1, 1):
            z = s * (D / 2 - 0.015)
            out.append({"mat": "paint_orange", "near": (0.0, y, z), "count": 1,
                        "ranges": [(Z, z - 0.015 + 0.001, z + 0.015 - 0.001)]})
    return out


def washer(W, D, top=0.86):
    # The grate's steel bars ended in the grate's own end faces: 1 mm short each end now.
    n = int((W - 0.12) / 0.05)
    half = (D - 0.12) / 2
    return [{"mat": "steel", "near": (-W / 2 + 0.085 + i * 0.05, top - 0.012, 0.0), "count": 1,
             "extent": (0.008, 0.012, None), "ranges": [(Z, -half + 0.001, half - 0.001)]} for i in range(n)]


def trolley():
    # The parts trolley's decks were flush with their lips' outsides: 2 mm in all round.
    W, D = 0.62, 1.0
    return [{"mat": "paint_grey", "near": (0.0, y, 0.0), "count": 1,
             "ranges": [(X, -W / 2 + 0.002, W / 2 - 0.002), (Z, -D / 2 + 0.002, D / 2 - 0.002)]}
            for y in (0.76, 0.24)]


def main():
    D = 0.8
    fix("parts_computer", [
        # The desk's front edge strip shared the top's top, bottom, ends and front: it wraps the
        # front 2 mm proud now, as edge banding does.
        {"mat": "plastic_black", "near": (0.0, 0.755, D / 2 - 0.004), "count": 1, "tol": 0.003,
         "ranges": [(X, -0.677, 0.677), (Y, 0.738, 0.772), (Z, D / 2 - 0.006, D / 2 + 0.002)]},
    ])
    fix("blaster_cabinet", blaster("blaster_cabinet", 1.7, 2.35, 1.7))
    fix("blaster_pro", blaster("blaster_pro", 2.0, 2.45, 1.95))
    # The bead blaster's gloves' ends were in the cabinet's front face, inside the ports: 2 mm out.
    fix("repair_blaster", [{"mat": "rubber", "near": (s * 0.2, 0.96 - 0.18, 0.4 - 0.06), "count": 1, "tol": 0.01,
                            "ranges": [(Z, 0.4 - 0.118, 0.4 + 0.002)]} for s in (-1, 1)])
    fix("shelf_racking", shelving(2.4, 2.2, 0.6))
    fix("shelf_delivery", shelving(3.6, 2.2, 0.7))
    fix("washer_standard", washer(1.3, 0.9))
    fix("washer_industrial", washer(1.9, 1.0))
    fix("washer_ultrasonic", washer(0.9, 0.7))
    fix("trolley_parts", trolley())
    # The workbench's flat chrome "spanners" and hook stubs on the pegboard go: pegboard_tools
    # hangs real ones there.
    L, Dw = 4.8, 0.9
    fix("shed_workbench", [{"mat": "chrome", "near": (0.0, 1.4, -Dw / 2 + 0.035), "tol": (L * 0.3, 0.2, 0.02),
                            "drop": True, "count": (14, 0)}])   # 0: done already


if __name__ == "__main__":
    main()
