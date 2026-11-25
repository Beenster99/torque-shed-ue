"""Cloud render of the 3D preview windows' photo studio (Shed/TorqueStudioRig.h) in Blender, for judging the
look before a PC round: the same soft boxes, backdrop sweep and fixed exposure as the game's previews, on
real part models from Data/BikesV2.

  blender -b -P Tools/studio_preview_render.py -- OUT_DIR MODEL.glb[:node] [MODEL.glb[:node] ...]

Units are matched to the engine's: a soft box of I candelas becomes an area light whose on-axis intensity
is I (so a surface d metres away gets I/d^2 'lux'), the backdrop glows at its nits, and the film's
exposure is the engine's manual one (a pixel is nits x 2^Bias / 1.2) before a filmic tone curve. It is a
stand-in for the engine, not the engine: it shows the rig's light and shade, not Unreal's exact shaders.
"""
import math
import os
import re
import sys

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
RIG_H = os.path.join(HERE, "..", "Source", "TorqueShedProto", "Shed", "TorqueStudioRig.h")


def read_rig():
    text = open(RIG_H).read()
    lights = []
    for m in re.finditer(r'\{"(\w+)",\s*([-\d.]+),\s*([-\d.]+),\s*([-\d.]+),\s*([-\d.]+),\s*([-\d.]+),\s*([-\d.]+),'
                         r'\s*([-\d.]+)f,\s*([-\d.]+)f,\s*([-\d.]+)f,\s*(true|false)\}', text):
        g = m.groups()
        lights.append(dict(name=g[0], toward=float(g[1]), right=float(g[2]), up=float(g[3]), cd=float(g[4]),
                           w=float(g[5]), h=float(g[6]), col=(float(g[7]), float(g[8]), float(g[9])), shadows=g[10] == "true"))

    def num(name):
        return float(re.search(name + r"\s*=\s*([-\d.]+)", text).group(1))

    sweep = {k: num(k) for k in ("SweepTop", "SweepFloor", "SweepGlow", "SweepCorner")}
    return lights, num("ShopExposureBias"), num("ShopBackdropBrightness"), sweep


def srgb_to_lin(v):
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def smoothstep(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)


def gradient_image(sweep, n=256):
    """TorqueShopPreview's MakeGradient, as the texture holds it (sRGB)."""
    img = bpy.data.images.new("Sweep", n, n, alpha=False)
    px = []
    top, floor, glow, corner = sweep["SweepTop"], sweep["SweepFloor"], sweep["SweepGlow"], sweep["SweepCorner"]
    for y_img in range(n):
        y = n - 1 - y_img                       # Blender's rows run bottom up; the game's V runs top down
        for x in range(n):
            u, v = (x + 0.5) / n, (y + 0.5) / n
            fall = smoothstep(0.0, 1.0, v ** 1.25)
            c = top + (floor - top) * fall
            dx, dy = (u - 0.5) * 1.25, (v - 0.46) * 1.6
            pool = math.exp(-(dx * dx + dy * dy) * 5.0)
            c = c + (glow - c) * pool * 0.55
            cx, cy = u - 0.5, v - 0.5
            c *= 1.0 - corner * min(max((cx * cx + cy * cy) * 2.2 - 0.12, 0.0), 1.0)
            px += [c, c, c, 1.0]
    img.pixels = px
    img.colorspace_settings.name = "sRGB"
    return img


def look_at(obj, target):
    d = (target - obj.location).normalized()
    obj.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()


def render(out_png, glb, node, lights, bias, brightness, sweep, aspect=1.76, fov=30.0, yaw=-145.0, pitch=12.0):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=glb)
    made = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in made if o.type == "MESH"]
    if node:
        keep = [o for o in meshes if re.search(node, o.name, re.I)]
        for o in meshes:
            if o not in keep:
                o.hide_render = True
        meshes = keep
    if not meshes:
        raise SystemExit("no meshes matching %r in %s" % (node, glb))
    # Its box, centred on a turntable at the origin, turned to the start view.
    pivot = bpy.data.objects.new("Pivot", None)
    sc.collection.objects.link(pivot)
    lo = Vector((1e9,) * 3)
    hi = Vector((-1e9,) * 3)
    for o in meshes:
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w))
            hi = Vector(map(max, hi, w))
    centre = (lo + hi) / 2
    radius = max((hi - lo).length / 2, 0.05)
    for o in made:
        if o.parent is None:
            o.parent = pivot
            o.location -= centre
    pivot.rotation_euler = (0, 0, math.radians(-yaw))

    # The camera: looking along +y (the game's +x), down by pitch, far enough to fit the sphere.
    tan_h = math.tan(math.radians(fov / 2))
    tan_v = tan_h / aspect
    dist = max(radius / min(tan_h, tan_v) * 1.06, radius * 0.45 + 0.05)
    fwd = Vector((0, math.cos(math.radians(pitch)), -math.sin(math.radians(pitch))))
    right = Vector((1, 0, 0))
    up = right.cross(fwd).normalized()
    cam_d = bpy.data.cameras.new("Cam")
    cam_d.angle = math.radians(fov)
    cam_d.sensor_fit = "HORIZONTAL"
    cam = bpy.data.objects.new("Cam", cam_d)
    sc.collection.objects.link(cam)
    cam.location = -fwd * dist
    look_at(cam, Vector((0, 0, 0)))
    sc.camera = cam

    # The backdrop sweep behind, square to the view, glowing at its nits.
    back = dist + radius * 1.8 + 0.2
    w, h = 2 * back * tan_h * 1.08, 2 * back * tan_v * 1.08
    bpy.ops.mesh.primitive_plane_add(size=1.0)
    bd = bpy.context.active_object
    bd.scale = (w, h, 1)
    bd.location = cam.location + fwd * back
    bd.rotation_euler = cam.rotation_euler
    mat = bpy.data.materials.new("Sweep")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = gradient_image(sweep)
    tex.extension = "EXTEND"
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Strength"].default_value = brightness
    outn = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(tex.outputs["Color"], em.inputs["Color"])
    nt.links.new(em.outputs["Emission"], outn.inputs["Surface"])
    bd.data.materials.append(mat)
    bd.visible_shadow = False
    bd.visible_diffuse = False      # unlit in the game: it lights nothing
    bd.visible_glossy = True        # (the game's screen-space reflections see it behind the part)

    # The soft boxes.
    D = radius * 2.4 + 0.6
    gain = (D / 2.5) ** 2
    for L in lights:
        d = (fwd * -L["toward"] + right * L["right"] + up * L["up"]).normalized()
        ld = bpy.data.lights.new(L["name"], "AREA")
        ld.shape = "RECTANGLE"
        ld.size = L["w"] * D
        ld.size_y = L["h"] * D
        ld.energy = math.pi * L["cd"] * gain
        ld.color = L["col"]
        ld.use_shadow = L["shadows"]
        lo_ = bpy.data.objects.new(L["name"], ld)
        sc.collection.objects.link(lo_)
        lo_.location = d * D
        # An area light shines down its -z; its long side (size_y) kept upright, as the game's.
        lo_.rotation_euler = (-d).to_track_quat("-Z", "Y" if abs(d.z) < 0.95 else "X").to_euler()
    # No world light: the studio is a closed room.
    world = bpy.data.worlds.new("Black")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.0
    sc.world = world

    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = int(os.environ.get("STUDIO_SAMPLES", "96"))
    sc.cycles.use_denoising = True
    sc.render.resolution_x = int(os.environ.get("STUDIO_W", "1056"))
    sc.render.resolution_y = int(sc.render.resolution_x / aspect)
    sc.view_settings.view_transform = "Filmic"
    sc.view_settings.look = "Medium High Contrast"
    sc.view_settings.exposure = bias - math.log2(1.2)
    sc.render.filepath = out_png
    bpy.ops.render.render(write_still=True)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    out_dir, models = argv[0], argv[1:]
    os.makedirs(out_dir, exist_ok=True)
    lights, bias, brightness, sweep = read_rig()
    for spec in models:
        glb, _, node = spec.partition(":")
        name = os.path.splitext(os.path.basename(glb))[0] + ("_" + re.sub(r"\W+", "", node) if node else "")
        render(os.path.join(out_dir, name + ".png"), glb, node, lights, bias, brightness, sweep)


if __name__ == "__main__":
    main()
