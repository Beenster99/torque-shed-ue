"""A look at built props with their real scanned materials, as M_PropMaster draws them in the game
(Tools/ue_import_props.py: tint shaded by the scan or the scan's own colours, Detail, Macro, Grime),
in daylight on concrete, tiled into one contact sheet. For checking the yard kit's realistic pass
without the PC. Needs the scans on disk: SCANS=<folder of <scan id>/{color,rough,normal}.jpg>
(Tools/fetch_props.py's build/props/_scans, or any Poly Haven 1k download laid out the same way).

    SCANS=build/props/_scans blender -b --factory-startup -P Tools/props/render_real.py -- OUT.png [id ...]
"""
import json
import math
import os
import sys

import bpy
from mathutils import Vector

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "Data", "Props", "Blender")
SCANS = os.environ.get("SCANS") or os.path.join(ROOT, "build", "props", "_scans")
SPEC = json.load(open(os.path.join(ROOT, "Data", "Props", "materials.json")))
args = sys.argv[sys.argv.index("--") + 1:]
out = args[0]
ids = args[1:] or sorted(f[:-4] for f in os.listdir(SRC) if f.endswith(".glb"))
TILE = int(os.environ.get("TILE", "480"))
_images = {}


def image(sid, name, colour):
    key = (sid, name)
    if key not in _images:
        path = os.path.join(SCANS, sid, name + ".jpg")
        img = bpy.data.images.load(path) if os.path.exists(path) else None
        if img and not colour:
            img.colorspace_settings.name = "Non-Color"
        _images[key] = img
    return _images[key]


def build(mat, key):
    s = SPEC["materials"].get(key)
    if not s:
        return
    nt = mat.node_tree
    nt.nodes.clear()
    N, L = nt.nodes, nt.links
    outn = N.new("ShaderNodeOutputMaterial")
    bsdf = N.new("ShaderNodeBsdfPrincipled")
    L.new(bsdf.outputs[0], outn.inputs[0])
    tint = s.get("tint", [0.5, 0.5, 0.5])
    bsdf.inputs["Roughness"].default_value = s.get("rough", 0.5)
    bsdf.inputs["Metallic"].default_value = s.get("metal", 0.0)
    bsdf.inputs["Base Color"].default_value = (tint[0], tint[1], tint[2], 1)
    if s["base"] == "glass":
        bsdf.inputs["Alpha"].default_value = s.get("opacity", 0.25)
        mat.blend_method = "BLEND"
        return
    if s.get("emissive"):
        bsdf.inputs["Emission Color"].default_value = (tint[0], tint[1], tint[2], 1)
        bsdf.inputs["Emission Strength"].default_value = s["emissive"]
    sid = s.get("scan") or SPEC["scans"].get(s["base"])
    if not sid or s["base"] == "emissive" or not image(sid, "color", True):
        return
    detail = s.get("detail", 0.6)

    def math_node(op, a, b=None):
        m = N.new("ShaderNodeMath")
        m.operation = op
        for i, v in enumerate((a, b)):
            if v is None:
                continue
            if isinstance(v, (int, float)):
                m.inputs[i].default_value = v
            else:
                L.new(v, m.inputs[i])
        return m.outputs[0]

    def mix(a, b, f):
        m = N.new("ShaderNodeMix")
        m.data_type = "RGBA"
        for v, sock in ((a, m.inputs[6]), (b, m.inputs[7])):
            if isinstance(v, tuple):
                sock.default_value = v
            else:
                L.new(v, sock)
        if isinstance(f, (int, float)):
            m.inputs[0].default_value = f
        else:
            L.new(f, m.inputs[0])
        return m.outputs[2]

    tc = N.new("ShaderNodeTexCoord")
    mp = N.new("ShaderNodeMapping")
    t = s.get("tile", 1.0)
    mp.inputs["Scale"].default_value = (t, t, t)
    L.new(tc.outputs["Generated" if s.get("world_uv") else "UV"], mp.inputs[0])
    tex = {}
    for name in ("color", "rough", "normal"):
        n = N.new("ShaderNodeTexImage")
        n.image = image(sid, name, name == "color")
        L.new(mp.outputs[0], n.inputs[0])
        tex[name] = n
    col = tex["color"].outputs["Color"]
    # Tint x lerp(1, luminance x 2, Detail), or Tint x the scan's own colour (TrueColour).
    lum = N.new("ShaderNodeRGBToBW")
    L.new(col, lum.inputs[0])
    shade = math_node("MULTIPLY", lum.outputs[0], 2.0)
    shade = mix((1, 1, 1, 1), shade, detail)
    tinted = mix((tint[0], tint[1], tint[2], 1), shade, 1.0)
    tinted.node.blend_type = "MULTIPLY"
    true_c = mix((tint[0], tint[1], tint[2], 1), col, 1.0)
    true_c.node.blend_type = "MULTIPLY"
    base = mix(tinted, true_c, s.get("true", 0.0))
    if s.get("macro"):
        nz = N.new("ShaderNodeTexNoise")
        nz.inputs["Scale"].default_value = 0.6
        nz.inputs["Detail"].default_value = 3.0
        L.new(tc.outputs["Object"], nz.inputs[0])
        f = math_node("MULTIPLY_ADD", nz.outputs[0], 2.0 * s["macro"])
        f.node.inputs[2].default_value = 1.0 - s["macro"]
        base = mix(base, f, 1.0)
        base.node.blend_type = "MULTIPLY"
    if s.get("grime"):
        sep = N.new("ShaderNodeSeparateXYZ")
        L.new(tc.outputs["Object"], sep.inputs[0])
        low = math_node("SUBTRACT", 1.0, math_node("MINIMUM", math_node("MAXIMUM", math_node("DIVIDE", sep.outputs[2], 0.4), 0.0), 1.0))
        g = math_node("MULTIPLY", math_node("MULTIPLY", low, low), s["grime"])
        dirty = mix(base, (0.5, 0.42, 0.33, 1), 1.0)
        dirty.node.blend_type = "MULTIPLY"
        base = mix(base, dirty, g)
    L.new(base, bsdf.inputs["Base Color"])
    r = mix((1, 1, 1, 1), math_node("MULTIPLY", tex["rough"].outputs["Color"], 2.0), detail)
    r = math_node("MINIMUM", math_node("MAXIMUM", math_node("MULTIPLY", r, s.get("rough", 0.5)), 0.03), 1.0)
    L.new(r, bsdf.inputs["Roughness"])
    nm = N.new("ShaderNodeNormalMap")
    nm.inputs["Strength"].default_value = detail
    L.new(tex["normal"].outputs["Color"], nm.inputs["Color"])
    L.new(nm.outputs[0], bsdf.inputs["Normal"])


def scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = int(os.environ.get("SAMPLES", "48"))
    sc.cycles.use_denoising = True
    sc.render.resolution_x = TILE
    sc.render.resolution_y = TILE
    sc.view_settings.view_transform = os.environ.get("VIEW", "AgX")
    sc.view_settings.exposure = float(os.environ.get("EXPOSURE", "0.0"))
    w = bpy.data.worlds.new("w")
    w.use_nodes = True
    # A plain daylight sky (a sky texture's physical units swamp the shading) and a low sun.
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.65, 0.85, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.6
    sc.world = w
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.5
    sun.data.angle = math.radians(1.5)
    sun.rotation_euler = (math.radians(55), 0, math.radians(140))
    sc.collection.objects.link(sun)
    bpy.ops.mesh.primitive_plane_add(size=60)
    fl = bpy.context.active_object
    m = bpy.data.materials.new("concrete_floor")
    m.use_nodes = True
    # Generated coordinates run 0..1 over the 60 m plane: 20 repeats of the 3 m scan.
    SPEC["materials"]["_floor"] = {"base": "concrete", "scan": "concrete_floor_worn_001", "true": 1.0, "tint": [0.8, 0.8, 0.78], "rough": 0.85,
                                   "tile": 20.0, "detail": 1.0, "world_uv": 1.0}
    build(m, "_floor")
    fl.data.materials.append(m)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.lens = 50
    sc.collection.objects.link(cam)
    sc.camera = cam
    return sc, cam


def shoot(sc, cam, pid, path):
    for o in list(sc.objects):
        if o.get("prop"):
            bpy.data.objects.remove(o, do_unlink=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, pid + ".glb"))
    objs = [o for o in bpy.context.selected_objects]
    lo = Vector((1e9, 1e9, 1e9))
    hi = -lo
    for o in objs:
        o["prop"] = 1
        if o.type != "MESH":
            continue
        for slot in o.material_slots:
            if slot.material and not slot.material.get("built"):
                build(slot.material, slot.material.name.split(".")[0])
                slot.material["built"] = 1
        for c in o.bound_box:
            p = o.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, p))
            hi = Vector(map(max, hi, p))
    centre = (lo + hi) * 0.5
    size = max((hi - lo).length, 0.3)
    d = Vector((1.0, -1.25, 0.6)).normalized() * size * 1.35
    cam.location = centre + d
    cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)


def main():
    sc, cam = scene()
    tmp = os.path.join(os.path.dirname(os.path.abspath(out)), "_real_tiles")
    os.makedirs(tmp, exist_ok=True)
    paths = []
    for pid in ids:
        p = os.path.join(tmp, pid + ".png")
        shoot(sc, cam, pid, p)
        paths.append(p)
    cols = min(4, len(paths))
    rows = (len(paths) + cols - 1) // cols
    sheet = bpy.data.images.new("sheet", TILE * cols, TILE * rows)
    px = [0.0] * (TILE * cols * TILE * rows * 4)
    for i, p in enumerate(paths):
        im = bpy.data.images.load(p)
        src = list(im.pixels)
        cx, cy = i % cols, rows - 1 - i // cols
        for y in range(TILE):
            row = (cy * TILE + y) * TILE * cols + cx * TILE
            px[row * 4:(row + TILE) * 4] = src[y * TILE * 4:(y + 1) * TILE * 4]
    sheet.pixels = px
    sheet.filepath_raw = out
    sheet.file_format = "PNG"
    sheet.save()


main()
