"""Bike Models V2: the cast and forged wheel library (beenster, 15 Nov).

A wheel is three things: a rim section turned about the axle (flanges, bead seats, drop well, and the ribs
and steps on the side you see), a hub, and a spoke pattern. A spoke pattern is a few bars drawn once in
polar coordinates (how far out, what angle, how wide, how thick) and repeated round the wheel, so a new
style is a short recipe. Every style is built at a front and a rear size.

Our own designs, drawn with real wheels as reference only: no maker's name, logo or exact pattern ships.

    python3 foundry/wheels3d.py                 # every style: out/wheels/<style>.png and out/wheels/sheet.png
    python3 foundry/wheels3d.py sweep7 web10    # only these
"""
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build3d as B
import detail3d as D

D.B = B
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "wheels")
INCH = 25.4

FINISH = {
    "gloss_black": dict(col=(0.012, 0.012, 0.014), metallic=0.0, rough=0.2, coat=1.0),
    "matt_black": dict(col=(0.022, 0.022, 0.024), metallic=0.0, rough=0.6, coat=0.0),
    "gold": dict(col=(0.62, 0.42, 0.13), metallic=1.0, rough=0.3, coat=0.5),
    "anodised_gold": dict(col=(0.75, 0.46, 0.05), metallic=1.0, rough=0.18, coat=0.2),
    "chrome": dict(col=(0.86, 0.86, 0.88), metallic=1.0, rough=0.07, coat=0.0),
    "gunmetal": dict(col=(0.12, 0.125, 0.13), metallic=0.9, rough=0.36, coat=0.3),
    "carbon": dict(col=(0.02, 0.02, 0.023), metallic=0.0, rough=0.3, coat=1.0),
    "bronze": dict(col=(0.3, 0.19, 0.09), metallic=1.0, rough=0.34, coat=0.3),
}


def finish(name):
    f = FINISH[name]
    return D.surf("wheel_" + name, f["col"], f["metallic"], f["rough"], coat=f["coat"], wear=0.05)


# A spoke pattern: "n" repeats of "bars". A bar is a list of points (f, angle, w, t):
#   f      0 at the hub, 1 at the rim
#   angle  degrees round the wheel from the bar's own spoke line
#   w      half width in the plane of the wheel, mm
#   t      half thickness along the axle, as a share of the hub's (1) tapering to the rim's (about 0.5)
# "curve" rounds the bar's corners; "inlay" lays a machined bright strip on both faces.
STYLES = {
    "sweep7": dict(label="Seven swept spokes", ref=1, n=7, finish="gloss_black", rim=1, hub="star", sizes=((17, 3.5), (17, 6.0)), bars=[
        dict(curve=True, pts=[(0, -6, 17, 1.0), (0.3, 4, 13, 0.9), (0.65, 20, 9.5, 0.7), (1, 37, 8, 0.55)])]),
    "y5": dict(label="Five Y spokes", ref=2, n=5, finish="gunmetal", rim=2, hub="flange", sizes=((17, 3.5), (17, 5.5)), bars=[
        dict(pts=[(0, 0, 15, 1.0), (0.42, 0, 11, 0.85)]),
        dict(curve=True, pts=[(0.36, 0, 9, 0.85), (0.5, 3, 8, 0.75), (1, 15, 6.5, 0.5)]),
        dict(curve=True, pts=[(0.36, 0, 9, 0.85), (0.5, -3, 8, 0.75), (1, -15, 6.5, 0.5)])]),
    "hex6": dict(label="Six hex web, machined face", ref=3, n=6, finish="gloss_black", rim=2, hub="flange", sizes=((19, 3.0), (17, 6.0)), bars=[
        dict(inlay=False, pts=[(0, 0, 20, 1.0), (0.38, 0, 15, 0.9)]),
        dict(curve=True, inlay=True, pts=[(0.3, 0, 12, 0.9), (0.5, 8, 9, 0.8), (0.72, 24, 8, 0.65), (1, 26, 9, 0.55)]),
        dict(curve=True, inlay=True, pts=[(0.3, 0, 12, 0.9), (0.5, -8, 9, 0.8), (0.72, -24, 8, 0.65), (1, -26, 9, 0.55)])]),
    "ten": dict(label="Ten spokes in five pairs", ref=4, n=5, finish="matt_black", rim=2, hub="drum", sizes=((17, 3.5), (17, 5.5)), bars=[
        dict(pts=[(0, 9, 8, 1.0), (0.5, 12, 6.5, 0.75), (1, 15, 6, 0.5)]),
        dict(pts=[(0, -9, 8, 1.0), (0.5, -12, 6.5, 0.75), (1, -15, 6, 0.5)])]),
    "turbine10": dict(label="Ten turbine blades", ref=5, n=10, finish="chrome", rim=0, hub="slim", sizes=((21, 2.5), (18, 5.5)), bars=[
        dict(curve=True, pts=[(0, -10, 12, 1.0), (0.25, 2, 13, 0.9), (0.6, 28, 9, 0.7), (0.85, 48, 6, 0.55), (1, 58, 5, 0.5)])]),
    "five_inlay": dict(label="Five straight spokes, bright inlay", ref=6, n=5, finish="gloss_black", rim=0, hub="slim", sizes=((21, 2.5), (18, 5.5)), bars=[
        dict(inlay=True, pts=[(0, 0, 13, 1.0), (0.5, 0, 19, 0.8), (1, 0, 26, 0.6)])]),
    "sickle5": dict(label="Five sickle spokes", ref=7, n=5, finish="chrome", rim=0, hub="tall", sizes=((21, 2.5), (18, 5.5)), bars=[
        dict(curve=True, pts=[(0, -14, 20, 1.0), (0.3, 0, 24, 0.9), (0.6, 26, 17, 0.7), (0.85, 48, 10, 0.55), (1, 52, 9, 0.5)])]),
    "twin5": dict(label="Five fine twin spokes", ref=8, wk=1.0, n=5, finish="carbon", rim=1, hub="star", sizes=((17, 3.5), (17, 6.0)), bars=[
        dict(pts=[(0, 14, 5.5, 1.0), (1, 5, 4.5, 0.5)]),
        dict(pts=[(0, -14, 5.5, 1.0), (1, -5, 4.5, 0.5)])]),
    "web10": dict(label="Petal web", ref=9, wk=1.0, n=10, finish="gloss_black", rim=2, hub="slim", sizes=((19, 3.0), (17, 6.0)), bars=[
        dict(pts=[(0, 0, 5, 1.0), (0.4, 0, 4.5, 0.85)]),
        dict(pts=[(0.4, 0, 4.5, 0.85), (0.68, 18, 4.5, 0.7)]),
        dict(pts=[(0.4, 0, 4.5, 0.85), (0.68, -18, 4.5, 0.7)]),
        dict(pts=[(0.68, 18, 4.5, 0.7), (0.86, 18, 4.5, 0.6)]),
        dict(pts=[(0.86, 18, 4.5, 0.6), (1, 12.5, 4.5, 0.5)]),
        dict(pts=[(0.86, 18, 4.5, 0.6), (1, 23.5, 4.5, 0.5)])]),
    "blade7": dict(label="Seven straight blades", ref=10, n=7, finish="carbon", rim=1, hub="star", sizes=((17, 3.5), (17, 6.0)), bars=[
        dict(pts=[(0, 0, 19, 1.0), (0.3, 0, 13, 0.85), (1, 0, 9.5, 0.5)])]),
    "offset5": dict(label="Five offset twin spokes", ref=0, n=5, finish="gloss_black", rim=1, hub="flange", sizes=((17, 3.5), (17, 5.5)), bars=[
        dict(pts=[(0, -30, 7, 1.0), (1, 4, 6, 0.5)]),
        dict(pts=[(0, -8, 7, 1.0), (1, 14, 6, 0.5)])]),
    "split6": dict(label="Six split spokes", ref=0, n=6, finish="bronze", rim=1, hub="flange", sizes=((17, 3.5), (17, 5.5)), bars=[
        dict(pts=[(0, 5, 8, 1.0), (0.5, 4, 6.5, 0.75), (1, 9, 6, 0.5)]),
        dict(pts=[(0, -5, 8, 1.0), (0.5, -4, 6.5, 0.75), (1, -9, 6, 0.5)]),
        dict(pts=[(0.48, 4, 5, 0.5), (0.48, -4, 5, 0.5)])]),
}


def rim_profile(R, W, ridges):
    """The rim section as a closed loop [(across, radius)]: tyre side first, then back along the side you see."""
    h = W / 2
    wd = max(13.0, h * 0.42)
    tyre = [(-h - 8, R + 12), (-h - 5, R + 14.5), (-h - 1.5, R + 13.5), (-h, R + 9), (-h + 1, R + 2), (-h + 3.5, R), (-h + 17, R - 1),
            (-h + 20, R + 1.2), (-h + 23, R - 1.5), (-wd - 2, R - 7), (-wd + 6, R - 19), (wd - 6, R - 19), (wd + 2, R - 7),
            (h - 23, R - 1.5), (h - 20, R + 1.2), (h - 17, R - 1), (h - 3.5, R), (h - 1, R + 2), (h, R + 9), (h + 1.5, R + 13.5), (h + 5, R + 14.5), (h + 8, R + 12)]
    side = [(h + 9, R + 8), (h + 6, R + 2), (h + 2, R - 5)]
    if ridges == 1:      # one raised rib on the shoulder
        side += [(h - 8, R - 7.5), (h - 9.5, R - 10.5), (h - 13, R - 10.5), (h - 14.5, R - 8)]
    elif ridges == 2:    # a stepped shoulder with two fine ribs
        side += [(h - 4, R - 6.5), (h - 5, R - 9.5), (h - 8, R - 9.5), (h - 9, R - 7.5), (h - 14, R - 8), (h - 15, R - 11), (h - 18, R - 11), (h - 19, R - 8.5)]
    side += [(h - 23, R - 9), (wd + 3, R - 14), (wd - 5, R - 26), (0, R - 27)]
    back = [(-y, r) for y, r in side[::-1][1:]]
    return tyre + side + back + [tyre[0]]


def bar(name, pts, n, k0, material, r_h, r_r, th_h, curve, yo=0.0, flat=None, wk=1.0):
    """One bar of a spoke pattern, at spoke k0 of n."""
    if curve:
        for _ in range(3):
            o = [pts[0]]
            for i in range(len(pts) - 1):
                a, b = pts[i], pts[i + 1]
                o += [tuple(0.75 * a[j] + 0.25 * b[j] for j in range(4)), tuple(0.25 * a[j] + 0.75 * b[j] for j in range(4))]
            pts = o + [pts[-1]]
    dense = []
    for i in range(len(pts) - 1):
        a, b = pts[i], pts[i + 1]
        m = 1 if curve else 10
        dense += [tuple(a[j] + (b[j] - a[j]) * q / m for j in range(4)) for q in range(m)]
    dense.append(pts[-1])
    base = 360.0 * k0 / n
    path = []
    for f, ang, w, t in dense:
        r = r_h + (r_r - r_h) * f
        th = math.radians(base + ang)
        path.append((r * math.cos(th), r * math.sin(th), w * wk, t * th_h))
    verts, faces, m = [], [], 12
    for i, (x, z, w, t) in enumerate(path):
        a, b = path[max(0, i - 1)], path[min(len(path) - 1, i + 1)]
        tx, tz = b[0] - a[0], b[1] - a[1]
        L = math.hypot(tx, tz) or 1.0
        nx, nz = -tz / L, tx / L
        for q in range(m):
            wq = 2 * math.pi * q / m
            cq, sq = math.cos(wq), math.sin(wq)
            ex = math.copysign(abs(cq) ** 0.45, cq) * w
            ey = math.copysign(abs(sq) ** 0.45, sq) * (flat if flat else t)
            verts.append(B.V(x + nx * ex, yo * (t if flat else 1) + ey, z + nz * ex))
    for i in range(len(path) - 1):
        for q in range(m):
            q2 = (q + 1) % m
            faces.append((i * m + q, i * m + q2, (i + 1) * m + q2, (i + 1) * m + q))
    faces += [tuple(range(m - 1, -1, -1)), tuple((len(path) - 1) * m + q for q in range(m))]
    ob = B.mesh_obj(name, verts, faces, material, True, 0, True)
    B.bm_fix_normals(ob)
    md = ob.modifiers.new("bevel", "BEVEL")
    md.width, md.segments, md.limit_method = 1.2 * B.MM, 2, "ANGLE"
    return ob


def wheel(tag, style, dia, width, rear):
    """Build one wheel about the origin, axle along y. Returns its objects."""
    before = set(bpy.data.objects)
    st = STYLES[style]
    R, W = dia * INCH / 2, width * INCH
    paint = finish(st["finish"])
    bright = D.surf("wheel_machined", (0.82, 0.83, 0.85), 1.0, 0.16, wear=0.05)
    steel = B.mat("steel")
    D.lathe(f"{tag}_rim", (0, 0, 0), (0, 1, 0), rim_profile(R, W, st["rim"]), paint, 96, caps=False)
    # valve
    B.cone(f"{tag}_valve", (R - 26, 6, 0), (R - 46, 16, 0), 3.2, 2.8, B.mat("rubber"), 10)
    # hub
    hub = st["hub"]
    hl = (62 if rear else 52) if hub != "tall" else (80 if rear else 66)
    r_body = {"star": 30, "flange": 36, "drum": 58 if rear else 44, "slim": 26, "tall": 30}[hub] * (1.25 if rear and hub != "drum" else 1.0)
    r_fl = r_body + (16 if hub in ("flange", "star") else 6)
    prof = [(-hl, 15), (-hl, r_fl - 6), (-hl + 3, r_fl), (-hl + 9, r_fl), (-hl + 15, r_body + 1), (-10, r_body + (5 if hub == "tall" else 0)), (10, r_body + (5 if hub == "tall" else 0)),
            (hl - 15, r_body + 1), (hl - 9, r_fl), (hl - 3, r_fl), (hl, r_fl - 6), (hl, 15)]
    D.lathe(f"{tag}_hub", (0, 0, 0), (0, 1, 0), prof, paint, 56)
    D.lathe(f"{tag}_bearing", (0, 0, 0), (0, 1, 0), [(-hl - 2, 10), (-hl - 2, 17), (hl + 2, 17), (hl + 2, 10)], steel, 32)
    D.lathe(f"{tag}_bore", (0, 0, 0), (0, 1, 0), [(-hl - 2.4, 0), (-hl - 2.4, 10.5), (hl + 2.4, 10.5), (hl + 2.4, 0)], D.surf("soot", (0.006, 0.006, 0.006), 0.0, 0.95, wear=0.0), 24)
    nb = 5 if hub in ("star", "drum") else 6
    for sy in (-1, 1):
        for k in range(nb):
            w = 2 * math.pi * (k + 0.5) / nb
            D.bolt(f"{tag}_hubbolt{sy}{k}", ((r_fl - 9) * math.cos(w), sy * hl, (r_fl - 9) * math.sin(w)), (0, sy, 0), 5.0, 3.5, steel)
        if hub == "star":      # a star carrier plate behind the bolts
            pts = []
            for k in range(nb * 2):
                w = 2 * math.pi * (k / 2 + 0.5) / nb
                rr = r_fl + 22 if k % 2 == 0 else r_fl - 2
                pts.append((rr * math.cos(w), rr * math.sin(w)))
            y0, y1 = sorted((sy * (hl - 15), sy * (hl - 8)))
            B.prism(f"{tag}_star{sy}", D.chaikin(pts, 1), y0, y1, paint, 1.0)
    # spokes
    r_h, r_r = r_body - 5, R - 24
    th_h = (min(hl - 16, 24) if not rear else min(hl - 16, 30)) * (1.25 if hub == "tall" else 1.0)
    wk = st.get("wk", 1.4) * (1.12 if rear else 1.0)
    for bi, b_ in enumerate(st["bars"]):
        for k in range(st["n"]):
            bar(f"{tag}_spoke{bi}_{k}", list(b_["pts"]), st["n"], k, paint, r_h, r_r, th_h, b_.get("curve", False), wk=wk)
            if b_.get("inlay"):
                for sy in (-1, 1):
                    pts = [(f, a, w * 0.62, t) for f, a, w, t in b_["pts"] if True]
                    pts = [(min(0.93, max(0.08, f)), a, w, t) for f, a, w, t in pts]
                    bar(f"{tag}_inlay{bi}_{k}{sy}", pts, st["n"], k, bright, r_h, r_r, th_h, b_.get("curve", False), yo=sy * 1.0, flat=1.0, wk=wk)
    return [o for o in bpy.data.objects if o not in before]


def place(objs, loc, rot_z):
    e = bpy.data.objects.new("pivot", None)
    bpy.context.collection.objects.link(e)
    for o in objs:
        if o.parent is None:
            o.parent = e
    e.location = tuple(v * B.MM for v in loc)
    e.rotation_euler = (0, 0, math.radians(rot_z))


def render(style, samples=40):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    B._mats.clear()
    st = STYLES[style]
    (fd, fw), (rd, rw) = st["sizes"]
    place(wheel("front", style, fd, fw, False), (300, -60, 0), 14)
    place(wheel("rear", style, rd, rw, True), (-290, 120, 0), -38)
    sc = bpy.context.scene
    sc.render.engine, sc.cycles.device, sc.cycles.samples, sc.cycles.use_denoising = "CYCLES", "CPU", samples, True
    sc.render.resolution_x, sc.render.resolution_y = 1280, 760
    w = bpy.data.worlds.new("w")
    sc.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.5, 0.51, 0.53, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.3
    bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 0, -(max(fd, rd) * INCH / 2 + 16) * B.MM))
    fl = bpy.context.object
    fm = bpy.data.materials.new("floor")
    fm.use_nodes = True
    fm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.33, 0.335, 0.345, 1)
    fm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.5
    fl.data.materials.append(fm)
    bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 1.6, 0), rotation=(math.radians(90), 0, 0))
    bpy.context.object.data.materials.append(fm)
    for loc, size, power in (((-1.4, -1.8, 1.6), 2.2, 380), ((1.8, -1.6, 0.9), 2.0, 220), ((0, -0.4, 2.2), 2.5, 180)):
        ld = bpy.data.lights.new("l", "AREA")
        ld.size, ld.energy = size, power
        lo = bpy.data.objects.new("l", ld)
        sc.collection.objects.link(lo)
        lo.location = loc
        from mathutils import Vector
        lo.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    cd = bpy.data.cameras.new("cam")
    cd.lens = 85
    cam = bpy.data.objects.new("cam", cd)
    sc.collection.objects.link(cam)
    from mathutils import Vector
    cam.location = (0.05, -2.9, 0.42)
    cam.rotation_euler = (Vector((0.0, 0, -0.02)) - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.camera = cam
    try:
        sc.view_settings.look = "AgX - Medium High Contrast"
    except Exception:
        pass
    sc.view_settings.exposure = -2.1
    os.makedirs(OUT, exist_ok=True)
    sc.render.filepath = os.path.join(OUT, style + ".png")
    bpy.ops.render.render(write_still=True)


def sheet(styles):
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 30)
    small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 22)
    cols = 3
    rows = (len(styles) + cols - 1) // cols
    tw, th = 960, 570
    im = Image.new("RGB", (cols * tw, rows * (th + 84)), (244, 242, 238))
    dr = ImageDraw.Draw(im)
    for i, s in enumerate(styles):
        st = STYLES[s]
        x, y = (i % cols) * tw, (i // cols) * (th + 84)
        im.paste(Image.open(os.path.join(OUT, s + ".png")).convert("RGB").resize((tw, th)), (x, y))
        (fd, fw), (rd, rw) = st["sizes"]
        dr.text((x + 18, y + th + 8), f"{i + 1}. {st['label']}", fill=(28, 27, 25), font=font)
        dr.text((x + 18, y + th + 46), f"front {fd} x {fw:g} in, rear {rd} x {rw:g} in, shown in {st['finish'].replace('_', ' ')}", fill=(93, 90, 84), font=small)
    im.save(os.path.join(OUT, "sheet.png"))


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    names = args or list(STYLES)
    if "--sheet-only" not in sys.argv:
        for s in names:
            render(s, 16 if "--draft" in sys.argv else 40)
    sheet(list(STYLES) if not args else names)
