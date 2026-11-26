"""Review renders of Tools/blender_forecourt_kit.py (Cycles, CPU): each whole station from the road, close views
of the pieces, a night view and a level-of-detail strip, on tarmac in daylight. Plain procedural materials stand
in for the game's."""
import math
import os

import bpy
from mathutils import Vector
from PIL import Image, ImageDraw, ImageFont

import blender_forecourt_kit as K

LOOK = {
    "paint": (0.35, 0.0), "metal": (0.35, 0.6), "panel": (0.3, 0.3), "glass": (0.03, 0.0),
    "concrete": (0.9, 0.0), "render": (0.85, 0.0), "brick": (0.85, 0.0), "tile": (0.75, 0.0),
    "roof": (0.5, 0.3), "roof_flat": (0.8, 0.0), "rubber": (0.8, 0.0), "lamp": (0.3, 0.0),
    "plant": (0.8, 0.0), "shutter": (0.4, 0.6),
}
DAY = {K.NIGHT_SOFFIT: (0.8, 0.8, 0.78), K.NIGHT_SHOP: None, K.NIGHT_DIGITS: (0.12, 0.06, 0.0), K.NIGHT_SIGN: (0.6, 0.6, 0.58)}
NIGHT = {K.NIGHT_SOFFIT: K.LAMP, K.NIGHT_SHOP: K.SHOP_GLOW, K.NIGHT_DIGITS: K.AMBER, K.NIGHT_SIGN: K.LAMP}


def colour(rgb, livery, night):
    if rgb == K.LIVERY_MAIN:
        return livery[1], False
    if rgb == K.LIVERY_ACCENT:
        return livery[2], False
    if isinstance(rgb, str):
        c = (NIGHT if night else DAY)[rgb]
        return c, night
    return rgb, False


def material(cache, surface, rgb, glow):
    key = (surface, tuple(round(c, 4) for c in rgb), glow)
    if key in cache:
        return cache[key]
    rough, metal = LOOK.get(surface, (0.7, 0.0))
    m = bpy.data.materials.new("m%d" % len(cache))
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    b.inputs["Base Color"].default_value = tuple(min(c, 1.0) for c in rgb) + (1.0,)
    if glow:
        b.inputs["Emission Color"].default_value = tuple(rgb) + (1.0,)
        b.inputs["Emission Strength"].default_value = 3.0
    if surface == "glass":
        b.inputs["Coat Weight"].default_value = 0.8
    if surface in ("brick", "tile", "concrete", "render"):
        br = nt.nodes.new("ShaderNodeTexBrick" if surface in ("brick", "tile") else "ShaderNodeTexNoise")
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        if surface in ("brick", "tile"):
            sep = nt.nodes.new("ShaderNodeSeparateXYZ"); nt.links.new(geo.outputs["Position"], sep.inputs[0])
            add = nt.nodes.new("ShaderNodeMath"); add.operation = "ADD"
            nt.links.new(sep.outputs[0], add.inputs[0]); nt.links.new(sep.outputs[1], add.inputs[1])
            com = nt.nodes.new("ShaderNodeCombineXYZ")
            nt.links.new(add.outputs[0], com.inputs[0]); nt.links.new(sep.outputs[2], com.inputs[1])
            nt.links.new(com.outputs[0], br.inputs["Vector"])
            br.inputs["Scale"].default_value = 1.0
            br.inputs["Brick Width"].default_value = 0.225 if surface == "brick" else 0.27
            br.inputs["Row Height"].default_value = 0.075 if surface == "brick" else 0.11
            br.inputs["Mortar Size"].default_value = 0.01 if surface == "brick" else 0.006
            br.inputs["Color1"].default_value = tuple(rgb) + (1.0,)
            br.inputs["Color2"].default_value = tuple(c * 0.7 for c in rgb) + (1.0,)
            br.inputs["Mortar"].default_value = (0.3, 0.29, 0.27, 1) if surface == "brick" else tuple(c * 0.5 for c in rgb) + (1,)
            nt.links.new(br.outputs["Color"], b.inputs["Base Color"])
        else:
            br.inputs["Scale"].default_value = 4.0
            nt.links.new(geo.outputs["Position"], br.inputs["Vector"])
            mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = "RGBA"; mix.blend_type = "MULTIPLY"
            mix.inputs["Factor"].default_value = 0.25
            mix.inputs[6].default_value = tuple(rgb) + (1.0,)
            nt.links.new(br.outputs["Color"], mix.inputs[7])
            nt.links.new(mix.outputs[2], b.inputs["Base Color"])
    cache[key] = m
    return m


def flat(name, x0, y0, x1, y1, z, rgb, rough=0.9):
    me = bpy.data.meshes.new(name)
    me.from_pydata([(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    mat = bpy.data.materials.new(name); mat.use_nodes = True
    b = mat.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = rgb + (1,); b.inputs["Roughness"].default_value = rough
    me.materials.append(mat)
    return ob


def scene():
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = int(os.environ.get("KIT_SAMPLES", "32"))
    sc.cycles.use_denoising = True
    names = [v.identifier for v in type(sc.view_settings).bl_rna.properties["view_transform"].enum_items]
    sc.view_settings.view_transform = "AgX" if "AgX" in names else "Filmic"
    world = bpy.data.worlds.new("w")
    sc.world = world
    world.use_nodes = True
    sky = world.node_tree.nodes.new("ShaderNodeTexSky")
    try:
        sky.sky_type = "MULTIPLE_SCATTERING"
    except Exception:
        pass
    sky.sun_elevation = math.radians(34)
    sky.sun_rotation = math.radians(215)
    world.node_tree.links.new(sky.outputs["Color"], world.node_tree.nodes["Background"].inputs["Color"])
    sun = bpy.data.lights.new("sun", "SUN")
    sun.angle = math.radians(1.0)
    so = bpy.data.objects.new("sun", sun)
    d = Vector((math.cos(math.radians(235)), math.sin(math.radians(235)), math.tan(math.radians(34)))).normalized()
    so.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    sc.collection.objects.link(so)
    flat("ground", -400, -400, 400, 400, -0.02, (0.05, 0.075, 0.025), 0.95)
    flat("tarmac", -60, -12, 60, 40, -0.005, (0.045, 0.045, 0.048), 0.85)
    flat("road", -60, -8, 60, -0.2, -0.004, (0.035, 0.035, 0.038), 0.8)
    flat("line", -60, -4.15, 60, -4.05, -0.003, (0.6, 0.6, 0.58), 0.6)
    return sc, so, sky, world


def light_day(sc, so, world, sky):
    for ob in [o for o in sc.collection.objects if o.name.startswith("nightlamp")]:
        bpy.data.objects.remove(ob, do_unlink=True)
    so.data.energy = 3.2
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.35
    sc.view_settings.exposure = float(os.environ.get("KIT_EXPOSURE", "-2.6"))


def light_night(sc, so, world, sky):
    so.data.energy = 0.0
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.003
    sc.view_settings.exposure = 0.3


def dress(group, livery, night, cache):
    """Gives each part its material for this livery and time of day; parts with no day look are hidden."""
    for p in group.parts:
        rgb, glow = colour(p.rgb, livery, night)
        p.obj.data.materials.clear()
        if rgb is None:
            p.obj.hide_render = True
            p.obj["skip"] = True
            continue
        p.obj["skip"] = False
        p.obj.data.materials.append(material(cache, p.surface, rgb, glow))
        if glow and p.rgb == K.NIGHT_SOFFIT and p.obj.name.startswith("light"):
            # (a light under each soffit panel, so the forecourt is lit as a real one is)
            ld = bpy.data.lights.new("nl", "AREA"); ld.energy = 160.0; ld.size = 2.4
            lo = bpy.data.objects.new("nightlamp", ld)
            lo.location = p.obj.matrix_world.translation - Vector((0, 0, 0.1))
            bpy.context.scene.collection.objects.link(lo)
            lo.rotation_euler = (0.0, 0.0, 0.0)
            lo.rotation_euler.x = math.pi   # (pointing down)


def show(sc, group, lod=0):
    mine = {p.obj.name for p in group.parts if p.lod >= lod and not p.obj.get("skip")}
    for ob in sc.collection.objects:
        if ob.type != "MESH" or ob.name in ("ground", "tarmac", "road", "line"):
            continue
        ob.hide_render = ob.name not in mine


def bounds(group):
    lo = Vector((1e9, 1e9, 1e9)); hi = Vector((-1e9, -1e9, -1e9))
    for p in group.parts:
        for c in p.obj.bound_box:
            w = p.obj.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
    return lo, hi


def shoot(sc, group, yaw, pitch, size, path, lens=35.0, focus=None, radius=None, lod=0):
    show(sc, group, lod)
    lo, hi = bounds(group)
    lo.z = max(lo.z, 0.0)
    centre = (lo + hi) * 0.5
    centre.z = lo.z + (hi.z - lo.z) * 0.4
    r = (hi - lo).length * 0.5
    if focus is not None:
        centre = Vector(focus)
    if radius is not None:
        r = radius
    cd = bpy.data.cameras.new("cam"); cd.lens = lens; cd.clip_end = 5000.0
    cam = bpy.data.objects.new("cam", cd)
    sc.collection.objects.link(cam)
    yw, pt = math.radians(yaw), math.radians(pitch)
    dist = r / math.tan(math.atan(18.0 / lens)) * 1.05
    cam.location = centre + Vector((math.cos(yw) * math.cos(pt), math.sin(yw) * math.cos(pt), math.sin(pt))) * dist
    cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = size
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam, do_unlink=True)


def font(n):
    for c in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf"):
        if os.path.exists(c):
            return ImageFont.truetype(c, n)
    return ImageFont.load_default()


def sheet(title, shots, size, path, cols=2):
    f, small = font(24), font(17)
    pad, head = 12, 30
    rows = (len(shots) + cols - 1) // cols
    W = cols * size[0] + (cols + 1) * pad
    H = 64 + rows * (size[1] + head + pad)
    img = Image.new("RGB", (W, H), (245, 245, 242))
    d = ImageDraw.Draw(img)
    d.text((pad, 18), title, fill=(20, 20, 20), font=f)
    for i, (label, p) in enumerate(shots):
        x = pad + (i % cols) * (size[0] + pad)
        y = 64 + (i // cols) * (size[1] + head + pad)
        d.text((x, y + 5), label, fill=(30, 30, 30), font=small)
        img.paste(Image.open(p).convert("RGB"), (x, y + head))
    img.save(path)
    print("sheet:", path)
    return path


def render_all(kit, stations, out):
    os.makedirs(out, exist_ok=True)
    sc, so, sky, world = scene()
    cache = {}
    size = (900, 540)
    only = os.environ.get("KIT_ONLY")
    # Hide the bare kit pieces (they sit at the origin); the stations hold copies.
    for P in kit.values():
        for p in P.parts:
            p.obj.hide_render = True
    light_day(sc, so, world, sky)
    # Each station on its own (all built at the origin: shown one at a time).
    shots = []
    for S in stations:
        if only and S.name not in only.split(","):
            continue
        dress(S, S.livery, False, cache)
        a = os.path.join(out, S.name + "_road.png")
        shoot(sc, S, 242, 9, size, a, lens=30)
        b = os.path.join(out, S.name + "_high.png")
        shoot(sc, S, 300, 30, size, b, lens=30)
        t = S.tris
        shots.append(("%d. %s, %s livery" % (len(shots) // 2 + 1, S.label, S.livery[0]), a))
        shots.append(("    from above the way in:  %s / %s / %s triangles near / middle / far" % (f"{t[0]:,}", f"{t[1]:,}", f"{t[2]:,}"), b))
    sheet("Ride UK petrol stations: the forecourt kit (our own designs, made-up liveries; grey box = the ride-in garage, unchanged)", shots, size,
          os.path.join(out, "forecourt_kit_sheet.png"))
    if only:
        return
    # Close views on the first town station and the first village one.
    town, village = stations[0], stations[2]
    close = []
    dress(town, town.livery, False, cache)
    vm = K.ENTRY_LANE + K.CANOPY_DEEP / 2
    kv = K.ENTRY_LANE + K.CANOPY_DEEP + K.KIOSK_GAP
    long = town.islands * K.ISLAND_GAP + 1.5
    plan = [
        (town, 250, 6, (0.0, vm, 1.4), 3.4, "Pump island: dispensers, screens, holsters, hoses, crash hoops, bin, towels"),
        (town, 330, 6, (long / 2 + 4.0, 1.6, 3.2), 4.2, "Price pole: livery panel, plain emblem, four price rows (lit amber after dark)"),
        (town, 255, 7, (2.5, kv, 2.0), 6.5, "Glass kiosk: shopfront, sliding doors, cash machine, gas cage, ice chest, logs"),
        (town, 290, 12, (long / 2 + 5.5, K.ENTRY_LANE + K.CANOPY_DEEP + 4.0, 2.0), 7.0, "Jet wash and the air and water machine"),
        (town, 270, -12, (0.0, vm, 4.0), 9.0, "Under the canopy: soffit light panels, clad columns, kick plates"),
    ]
    for g, yaw, pitch, focus, rad, label in plan:
        p = os.path.join(out, "close_%d.png" % len(close))
        shoot(sc, g, yaw, pitch, size, p, lens=35, focus=focus, radius=rad)
        close.append((label, p))
    dress(village, village.livery, False, cache)
    p = os.path.join(out, "close_village.png")
    shoot(sc, village, 250, 7, size, p, lens=35, focus=(3.0, kv, 2.4), radius=6.5)
    close.append(("Village kiosk: brick, tiled roof, timber shopfront, sign board in the livery", p))
    # Night: the town station lit.
    light_night(sc, so, world, sky)
    dress(town, town.livery, True, cache)
    p = os.path.join(out, "night.png")
    shoot(sc, town, 242, 9, size, p, lens=30)
    close.append(("After dark: soffit panels, shop glow, pump headers, price digits", p))
    light_day(sc, so, world, sky)
    dress(town, town.livery, False, cache)
    for lod in range(3):
        p = os.path.join(out, "lod%d.png" % lod)
        shoot(sc, town, 242, 9, size, p, lens=30, lod=lod)
        close.append(("Hand-made level of detail %d (%s): %s triangles" % (lod, ("near", "middle", "far")[lod], f"{town.tris[lod]:,}"), p))
    sheet("Forecourt kit: close views, after dark, levels of detail", close, size, os.path.join(out, "forecourt_kit_details.png"))
