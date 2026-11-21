"""A review sheet of Tools/blender_post_phone_kit.py's post boxes and telephone kiosks, rendered in Blender
(Cycles) with Tools/blender_street_kit_render.py's stand-ins for M_RideStreetKit. Called by
blender_post_phone_kit.py --render DIR."""
import math
import os

from PIL import Image, ImageDraw, ImageFont

from blender_street_kit_render import _scene, _shoot


def render_sheet(models, out_dir, tex, surfaces):
    os.makedirs(out_dir, exist_ok=True)
    sc = _scene(models, tex, surfaces)
    by = {M.name: M for M in models}
    objs = lambda name: [p.obj for p in by[name].parts]
    big = (420, 620)
    shots = [
        ("SM_Prop_post_box_0", "Pillar box", (0, 0, 0.8), 4.2, 28, 8, 50, big),
        ("SM_Prop_post_box_1", "Oval pillar, two slots", (0, 0, 0.8), 4.6, 32, 8, 50, big),
        ("SM_Prop_post_box_2", "Lamp box on a post", (0.1, 0, 0.85), 4.2, 30, 8, 50, big),
        ("SM_Prop_post_box_3", "Wall box in a brick pier", (0, 0, 0.8), 4.4, 34, 8, 50, big),
        ("SM_Prop_phone_box_0", "Telephone kiosk", (0, 0, 1.25), 6.4, 30, 8, 50, big),
        ("SM_Prop_post_box_0", "Slot, plate and badge", (0.2, 0, 1.0), 1.5, 15, 6, 50, (420, 340)),
        ("SM_Prop_post_box_3", "Wall box close", (0.25, 0, 1.05), 1.6, 25, 4, 50, (420, 340)),
        ("SM_Prop_phone_box_0", "Fascia and pediment", (0.4, 0, 2.2), 2.1, 25, -2, 50, (420, 340)),
        ("SM_Prop_phone_box_0", "Door and handle", (0.45, 0.2, 1.1), 2.2, 40, 4, 50, (420, 340)),
        ("SM_Prop_phone_box_1", "The older kiosk, from behind", (0, 0, 1.25), 6.4, 215, 10, 50, (420, 340)),
    ]
    files = []
    for i, (name, label, target, dist, yaw, pitch, lens, size) in enumerate(shots):
        f = os.path.join(out_dir, "pp_%02d.png" % i)
        _shoot(sc, objs(name), target, dist, yaw, pitch, lens, size, f)
        files.append((f, label))
    font = None
    for cand in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf"):
        if os.path.exists(cand):
            font = ImageFont.truetype(cand, 18)
    pad = 10
    W = 5 * 420 + 6 * pad
    H = 50 + big[1] + 28 + pad + 340 + 28 + pad
    sheet = Image.new("RGB", (W, H), (245, 245, 242))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 12), "Ride UK street kit: new post boxes and telephone kiosk (Blender renders, the street kit's one material)", fill=(20, 20, 20), font=font)
    for i, (f, label) in enumerate(files):
        im = Image.open(f).convert("RGB")
        col = i % 5
        y = 50 if i < 5 else 50 + big[1] + 28 + pad
        x = pad + col * (420 + pad)
        sheet.paste(im, (x, y))
        d.text((x, y + im.size[1] + 4), label, fill=(30, 30, 30), font=font)
    path = os.path.join(out_dir, "post_phone_review.png")
    sheet.save(path)
    print("review sheet:", path)
    return path
