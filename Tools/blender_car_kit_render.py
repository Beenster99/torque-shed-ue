"""The own parked cars' review sheet (Tools/blender_car_kit.py --render DIR): every car from its front and rear
corners, in Cycles on the CPU, with the warehouse kit's lighting and plain stand-ins for the game's materials
(Tools/blender_warehouse_kit_render.py)."""
import os
import sys
from types import SimpleNamespace

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_warehouse_kit_render as R  # noqa: E402

R.LOOK.update({"galv": (0.18, 1.0, None), "enamel": (0.3, 0.0, None), "lens": (0.04, 0.0, None), "paint": (0.6, 0.0, None)})


def render_cars(models, out_dir, lod=0, title=None):
    os.makedirs(out_dir, exist_ok=True)

    class _U:
        def __init__(self, M):
            self.name, self.parts, self.title = M.name, M.parts, getattr(M, "title", M.name)
            for p in self.parts:
                p.lod = 2
    units = [_U(M) for M in models]
    # (R._scene clears each piece's materials, which drops a tagged piece's faces' indices: kept, then put back)
    kept = {id(p): [poly.material_index for poly in p.obj.data.polygons] for U in units for p in U.parts if getattr(p, "table", None)}
    sc = R._scene(units)
    cache = {}
    for U in units:
        for p in U.parts:
            table = getattr(p, "table", None)
            if table:
                p.obj.data.materials.clear()
                for surface, rgb, dirt in table:
                    p.obj.data.materials.append(R._material(cache, SimpleNamespace(surface=surface, rgb=rgb)))
                for poly, m in zip(p.obj.data.polygons, kept[id(p)]):
                    poly.material_index = m
    size = (520, 300) if lod == 0 else (260, 150)
    shots = []
    for U in units:
        pair = []
        for tag, yaw in (("front", 32), ("rear", 222)) if lod == 0 else (("front", 32),):
            a = os.path.join(out_dir, "%s_%s.png" % (U.name, tag))
            R._shoot(sc, U, yaw, 14, size, a, lens=50.0)
            pair.append(a)
        shots.append((U, pair))
    cols = 2 if lod == 0 else 6
    pad, head = 8, 24
    cw = size[0] * len(shots[0][1]) + pad * (len(shots[0][1]) - 1)
    rows = (len(shots) + cols - 1) // cols
    W = cols * cw + (cols + 1) * pad
    H = 70 + rows * (size[1] + head + pad)
    sheet = Image.new("RGB", (W, H), (245, 245, 242))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 18), title or ("Ride UK parked cars: our own made-up cars and makers (Blender renders)" if lod == 0 else
                                "Their hand-made distance versions"), fill=(20, 20, 20), font=R._font(24))
    for i, (U, pair) in enumerate(shots):
        x = pad + (i % cols) * (cw + pad)
        y = 70 + (i // cols) * (size[1] + head + pad)
        d.text((x, y + 2), "%s   (%s)" % (U.title, U.name.replace("SM_Prop_parked_car_", "#")), fill=(25, 25, 25), font=R._font(15))
        for k, a in enumerate(pair):
            sheet.paste(Image.open(a).convert("RGB"), (x + k * (size[0] + pad), y + head))
    path = os.path.join(out_dir, "own_cars%s.png" % ("" if lod == 0 else "_lod1"))
    sheet.save(path)
    print("sheet:", path)
    return path
