"""The farm kit's review sheet (Tools/blender_farm_kit.py --render DIR): every building from its front corner on a
farmyard's concrete, and the yard's pieces (Tools/blender_farm_props.py) each alone, in Cycles on the CPU, with
the warehouse kit's lighting and plain stand-in materials (Tools/blender_warehouse_kit_render.py)."""
import math
import os
import sys

import bpy
from mathutils import Vector
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_warehouse_kit_render as R  # noqa: E402

R.LOOK.update({
    "boarding": (0.85, 0.0, ("wave", "h", 0.17, 0.8)),
    "weatherboard": (0.7, 0.0, ("wave", "z", 0.2, 0.9)),
    "stone": (0.9, 0.0, ("noise", None, None, 0.6)),
    "tin": (0.55, 0.35, ("wave", "z", 0.076, 0.9)),
    "timber": (0.8, 0.0, ("wave", "h", 0.15, 0.3)),
    "straw": (0.95, 0.0, ("noise", None, None, 0.4)),
    "block": (0.9, 0.0, ("noise", None, None, 0.2)),
    "plain": (0.9, 0.0, None),
    "galv": (0.42, 0.6, None), "enamel": (0.35, 0.0, None), "lens": (0.05, 0.0, None), "glaze": (0.05, 0.0, None),
})


def render_sheet(units, out_dir, props=None, title="Ride UK farmyards: the farm kit's buildings (Blender renders, our own designs; each fits any outline)", sheet_name="farm_kit_buildings.png"):
    os.makedirs(out_dir, exist_ok=True)
    sc = R._scene(units)
    for U in units:
        L, W = U.spec["L"], U.spec["W"]
        R._flat("yard_" + U.name, -14, -16, L + 10, W + 10, -0.01, (0.27, 0.265, 0.24), 0.9)
    size = (640, 400)
    shots = []
    for U in units:
        a = os.path.join(out_dir, U.name + "_front.png")
        if not os.path.exists(a) or not os.environ.get("KIT_KEEP"):
            R._shoot(sc, U, 236, 12, size, a)
        shots.append((U, a))
    cols, pad, head = 3, 10, 46
    rows = (len(shots) + cols - 1) // cols
    W = cols * size[0] + (cols + 1) * pad
    H = 70 + rows * (size[1] + head + pad)
    sheet = Image.new("RGB", (W, H), (245, 245, 242))
    d = ImageDraw.Draw(sheet)
    font, small, tiny = R._font(24), R._font(16), R._font(14)
    d.text((pad, 18), title, fill=(20, 20, 20), font=font)
    for i, (U, a) in enumerate(shots):
        x = pad + (i % cols) * (size[0] + pad)
        y = 70 + (i // cols) * (size[1] + head + pad)
        t = U.tris
        d.text((x, y + 2), "%d. %s" % (i + 1, U.label[:72]), fill=(25, 25, 25), font=small)
        d.text((x, y + 23), "%g x %g m as drawn;  LOD0 %s / LOD1 %s / LOD2 %s triangles" % (U.spec["L"], U.spec["W"], f"{t[0]:,}", f"{t[1]:,}", f"{t[2]:,}"),
               fill=(90, 90, 90), font=tiny)
        sheet.paste(Image.open(a).convert("RGB"), (x, y + head))
    path = os.path.join(out_dir, sheet_name)
    sheet.save(path)
    print("sheet:", path)
    return path


def render_props(models, out_dir, title="Ride UK farmyards: the yard's pieces (our own models; each with a hand-made distance version)", sheet_name="farm_kit_yard.png"):
    """Each yard piece from its front corner, then a sheet of them."""
    os.makedirs(out_dir, exist_ok=True)

    class _U:
        def __init__(self, M):
            self.name, self.parts = M.name, M.parts
            for p in self.parts:
                p.lod = 2
    units = [_U(M) for M in models]
    sc = R._scene(units)
    size = (420, 300)
    shots = []
    for U in units:
        a = os.path.join(out_dir, U.name + ".png")
        R._show(sc, U, 0)
        lo, hi = R._bounds(U)
        if hi.z - lo.z > 1.6 * max(hi.x - lo.x, hi.y - lo.y):
            # (a tall thin piece, a feed bin: framed on its middle, whole)
            R._shoot(sc, U, 236, 16, size, a, lens=50.0, focus=((lo + hi) * 0.5), zoom=(hi - lo).length * 0.62)
        else:
            R._shoot(sc, U, 236, 16, size, a, lens=50.0)
        shots.append((U, a))
    cols, pad, head = 5, 8, 24
    rows = (len(shots) + cols - 1) // cols
    W = cols * size[0] + (cols + 1) * pad
    H = 70 + rows * (size[1] + head + pad)
    sheet = Image.new("RGB", (W, H), (245, 245, 242))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 18), title, fill=(20, 20, 20), font=R._font(24))
    for i, (U, a) in enumerate(shots):
        x = pad + (i % cols) * (size[0] + pad)
        y = 70 + (i // cols) * (size[1] + head + pad)
        d.text((x, y + 2), U.name.replace("SM_Prop_", ""), fill=(25, 25, 25), font=R._font(15))
        sheet.paste(Image.open(a).convert("RGB"), (x, y + head))
    path = os.path.join(out_dir, sheet_name)
    sheet.save(path)
    print("sheet:", path)
    return path
