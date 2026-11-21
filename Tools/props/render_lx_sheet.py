"""The level crossings' review sheet: each type of crossing laid out as the game lays it (SimTest lx_test's layout
dump of World/WorldLevelCrossing), on a stand-in road, track and grass, with the kit's models
(Tools/props/make_level_crossing.py) as M_PropMaster draws them (Tools/props/render_real.py's build; plain colours
without the scans). Each type twice: a train due (amber or a red lit, booms down, gates shut) from the rider's eye,
and open from a three-quarter view; then close-ups of a light unit, the passive crossing's post and the plates.

    lx_test layout.json      (SimTest)
    python3 Tools/props/render_lx_sheet.py -- layout.json OUT.png
"""
import json
import math
import os
import sys

import bpy  # noqa: F401
import bmesh
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(ROOT, "Data", "Props", "Blender")
args = sys.argv[sys.argv.index("--") + 1:]
LAYOUT, out = args[0], args[1]
ONLY = args[2:]
TILE = int(os.environ.get("TILE", "720"))

_src = open(os.path.join(HERE, "render_real.py")).read()
_src = _src[:_src.rindex("\nmain()")]
_ns = {"__file__": os.path.join(HERE, "render_real.py")}
_argv = sys.argv
sys.argv = ["x", "--", out]
exec(compile(_src, "render_real.py", "exec"), _ns)
sys.argv = _argv
build_scan = _ns["build"]
SPEC = _ns["SPEC"]

PIVOT = (0.0, 1.0, 0.32)
BOOM_H = 0.11
SKIRT = 0.75
TYPES = ["open (no lights)", "open with lights", "half barrier", "full barrier", "gated"]


def B(x, y, z):
    return Vector((x, -z, y))


_images = {}


def build(mat, key):
    s = SPEC["materials"].get(key)
    if s and s["base"] == "printed":
        path = os.path.join(ROOT, s["texture"])
        nt = mat.node_tree
        nt.nodes.clear()
        N, L = nt.nodes, nt.links
        o = N.new("ShaderNodeOutputMaterial")
        b = N.new("ShaderNodeBsdfPrincipled")
        b.inputs["Roughness"].default_value = s.get("rough", 0.5)
        L.new(b.outputs[0], o.inputs[0])
        t = N.new("ShaderNodeTexImage")
        if path not in _images:
            _images[path] = bpy.data.images.load(path)
        t.image = _images[path]
        rows = float(s.get("rows", 1))
        row = float(mat.get("row", 0))
        uv = N.new("ShaderNodeUVMap")
        sep = N.new("ShaderNodeSeparateXYZ")
        L.new(uv.outputs[0], sep.inputs[0])
        # y_image = 1 - (1 - v + row) / rows
        m1 = N.new("ShaderNodeMath"); m1.operation = "SUBTRACT"; m1.inputs[0].default_value = 1.0 + row
        L.new(sep.outputs[1], m1.inputs[1])
        m2 = N.new("ShaderNodeMath"); m2.operation = "DIVIDE"; m2.inputs[1].default_value = rows
        L.new(m1.outputs[0], m2.inputs[0])
        m3 = N.new("ShaderNodeMath"); m3.operation = "SUBTRACT"; m3.inputs[0].default_value = 1.0
        L.new(m2.outputs[0], m3.inputs[1])
        comb = N.new("ShaderNodeCombineXYZ")
        L.new(sep.outputs[0], comb.inputs[0])
        L.new(m3.outputs[0], comb.inputs[1])
        L.new(comb.outputs[0], t.inputs[0])
        L.new(t.outputs[0], b.inputs["Base Color"])
        return
    build_scan(mat, key)


_meshes = {}


def place(pid, mat_world, row=None):
    """An instance of the kit's model pid at a Blender world matrix (its meshes imported once and shared)."""
    if pid not in _meshes:
        bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, pid + ".glb"))
        objs = [o for o in bpy.context.selected_objects if o.type == "MESH"]
        o = objs[0]
        for slot in o.material_slots:
            if slot.material and not slot.material.get("built"):
                build(slot.material, slot.material.name.split(".")[0])
                slot.material["built"] = 1
        _meshes[pid] = (o.data, o.matrix_world.copy())
        for x in bpy.context.selected_objects:
            bpy.data.objects.remove(x, do_unlink=True)
    me, base = _meshes[pid]
    if row is not None:
        me = me.copy()
        for i, m in enumerate(me.materials):
            if m and SPEC["materials"].get(m.name.split(".")[0], {}).get("base") == "printed":
                m2 = m.copy()
                m2["row"] = row
                build(m2, m.name.split(".")[0])
                me.materials[i] = m2
    o = bpy.data.objects.new(pid, me)
    bpy.context.scene.collection.objects.link(o)
    o.matrix_world = mat_world @ base
    o["lx"] = 1
    return o


def yaw_front(fx, fz):
    """The Blender rotation turning a model's front (+z) onto the world's (fx, fz)."""
    return Matrix.Rotation(math.atan2(fx, fz), 4, "Z")


def yaw_along(dx, dz):
    """The rotation laying a model's +x along the world's (dx, dz): its front is (-dz, dx)."""
    return yaw_front(-dz, dx)


def flat(name, corners, colour, rough=0.8, image=None, row=0, rows=1, lift=0.0):
    me = bpy.data.meshes.new(name)
    me.from_pydata([B(x, y + lift, z) for x, y, z in corners], [], [(0, 1, 2, 3)])
    me.update()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*colour, 1)
    b.inputs["Roughness"].default_value = rough
    if image:
        bm = bmesh.new()
        bm.from_mesh(me)
        uv = bm.loops.layers.uv.verify()
        for f in bm.faces:
            for lp, (u, v) in zip(f.loops, ((0, 0), (1, 0), (1, 1), (0, 1))):
                lp[uv].uv = (u, 1.0 - (1.0 - v + row) / rows)
        bm.to_mesh(me)
        bm.free()
        nt = m.node_tree
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = bpy.data.images.load(image)
        t.image.colorspace_settings.name = "Non-Color"
        nt.links.new(t.outputs[0], b.inputs["Alpha"])
        m.blend_method = "BLEND"
    o.data.materials.append(m)
    o["lx"] = 1
    return o


def ground(c):
    """Stand-ins: grass, the road north-south, the line across it (ballast, sleepers, two rails)."""
    mid, road, rail, half = Vector((c["mid"][0], c["mid"][1])), Vector(c["road"]), Vector(c["rail"]), c["half"]
    L = Vector((road.y, -road.x))
    def at(u, v):
        p = mid + road * u + L * v
        return p
    g = 40.0
    flat("grass", [(mid.x - g, -0.02, mid.y - g), (mid.x + g, -0.02, mid.y - g), (mid.x + g, -0.02, mid.y + g), (mid.x - g, -0.02, mid.y + g)], (0.06, 0.1, 0.03), 0.95)
    pts = [at(-g, -half), at(g, -half), at(g, half), at(-g, half)]
    flat("road", [(p.x, 0.0, p.y) for p in pts], (0.045, 0.045, 0.048), 0.8)
    for sv in (-1, 1):
        q = [at(-g, sv * (half - 0.25)), at(g, sv * (half - 0.25)), at(g, sv * (half - 0.15)), at(-g, sv * (half - 0.15))]
        flat("edge", [(p.x, 0.003, p.y) for p in q], (0.7, 0.7, 0.68), 0.6)
    rn = Vector((-rail.y, rail.x))
    def r(t, n):
        p = mid + rail * t + rn * n
        return p
    for n0, n1, y, col in ((-1.6, 1.6, 0.004, (0.18, 0.17, 0.16)),):
        q = [r(-g, n0), r(g, n0), r(g, n1), r(-g, n1)]
        flat("ballast", [(p.x, y, p.y) for p in q], col, 0.95)
    for n in (-0.7175, 0.7175):
        q = [r(-g, n - 0.035), r(g, n - 0.035), r(g, n + 0.035), r(-g, n + 0.035)]
        flat("rail", [(p.x, 0.02, p.y) for p in q], (0.45, 0.45, 0.47), 0.3)
    t = -g
    while t < g:
        q = [r(t, -1.3), r(t + 0.25, -1.3), r(t + 0.25, 1.3), r(t, 1.3)]
        if all(abs((p - mid).dot(L)) > half + 0.2 or True for p in q):
            flat("sleeper", [(p.x, 0.012, p.y) for p in q], (0.12, 0.1, 0.08), 0.9)
        t += 0.65


def crossing(pieces, closed, lit_red):
    """Every piece of one crossing; closed: booms down, gates shut, lamps lit (lit_red: which red, or 'amber')."""
    for p in pieces:
        pid, role = p["mesh"], p["role"]
        x, z, up = p["x"], p["z"], p["up"]
        at = Matrix.Translation(B(x, up, z))
        if role == 7:   # paint
            fx, fz = p["fx"], p["fz"]
            F = Vector((fx, fz))
            Lv = Vector((fz, -fx))   # (left of the front: the traffic's right as it comes)
            c = Vector((x, z))
            hl, hw = p["length"] / 2, p["width"] / 2
            # image top towards the line (-front): corners bottom-left, bottom-right, top-right, top-left seen by the traffic
            bl = c + F * hl - Lv * hw
            br = c + F * hl + Lv * hw
            tr = c - F * hl + Lv * hw
            tl = c - F * hl - Lv * hw
            flat("paint", [(bl.x, 0.0, bl.y), (br.x, 0.0, br.y), (tr.x, 0.0, tr.y), (tl.x, 0.0, tl.y)], (0.85, 0.85, 0.82), 0.6,
                 image=os.path.join(ROOT, "Data", "Props", "Textures", "lx_road_paint.png"), row=p["row"], rows=3, lift=0.006)
            continue
        if role == 5:   # fence panel
            place(pid, at @ yaw_front(p["fx"], p["fz"]))
            continue
        if role == 2:   # boom on its machine
            m = at @ yaw_front(p["fx"], p["fz"])
            place("lx_barrier_mech", m)
            ang = 0.0 if closed else (82.0 if p["skirted"] else 87.0)
            n = float(pid.split("_")[-1])
            pv = m @ Matrix.Translation(B(*PIVOT)) @ Matrix.Rotation(math.radians(-ang), 4, "Y")
            place(pid, pv @ Matrix.Diagonal((p["length"] / n, 1, 1, 1)))
            if closed:
                for k, lx in enumerate((0.8, p["length"] * 0.5 + 0.2, p["length"] - 0.15)):
                    if k == 2 or (k == 0) == (lit_red == "l"):
                        place("lx_boom_lit", pv @ Matrix.Translation(B(lx, 0, 0)))
            if p["skirted"]:
                a = math.radians(ang)
                xs = 0.35
                while xs < p["length"] - 0.1:
                    top = pv @ Vector((xs, 0.0, -BOOM_H / 2))
                    place("lx_skirt_bar", Matrix.Translation(top) @ Matrix.Diagonal((1, 1, SKIRT, 1)))
                    xs += 0.16
                p0 = pv @ Vector((0.35, 0.0, -BOOM_H / 2))
                rail_m = Matrix.Translation(p0 - Vector((0, 0, SKIRT))) @ m.to_3x3().to_4x4() @ Matrix.Rotation(-a, 4, "Y") @ Matrix.Diagonal((p["length"] - 0.45, 1, 1, 1))
                place("lx_skirt_rail", rail_m)
            continue
        if role == 3:   # gate
            place("lx_gate_post", at @ yaw_front(p["fx"], p["fz"]))
            d = (p["sx"], p["sz"]) if closed else (p["fx"], p["fz"])
            n = float(pid.split("_")[-1]) / 10.0
            place(pid, at @ yaw_along(*d) @ Matrix.Diagonal((p["length"] / n, 1, 1, 1)))
            continue
        m = at @ yaw_front(p["fx"], p["fz"])
        place(pid, m, row=p["row"] if role == 6 else None)
        if role == 1 and closed:
            place("lx_lit_amber" if lit_red == "amber" else "lx_lit_red_" + lit_red, m)
        if role == 4:
            place("lx_msl_lit_red" if closed else "lx_msl_lit_green", m)


def clear():
    for o in list(bpy.context.scene.objects):
        if o.get("lx"):
            bpy.data.objects.remove(o, do_unlink=True)


def main():
    data = json.load(open(LAYOUT))["crossings"]
    sc, cam = _ns["scene"]()
    for o in list(sc.objects):
        if o.type == "MESH":
            bpy.data.objects.remove(o, do_unlink=True)
    sc.render.resolution_x = TILE
    sc.render.resolution_y = int(TILE * 0.62)
    sun = [o for o in sc.objects if o.type == "LIGHT"][0]
    sun.rotation_euler = (math.radians(52), 0, math.radians(210))
    shots = []
    for t in range(5):
        for skew in (0,):
            ps = [p for p in data if p["type"] == t and p["skew"] == skew]
            c = ps[0]
            mid = Vector((c["mid"][0], c["mid"][1]))
            road = Vector(c["road"])
            Lv = Vector((road.y, -road.x))
            half = c["half"]
            # (the rider coming north up the nearside lane: from u = -25)
            eye = mid - road * 24.0 + Lv * (half * 0.5)
            shots.append(("%s: a train due, from the rider's eye" % TYPES[t], ps, True, "l" if t != 1 else "r",
                          B(eye.x, 1.25, eye.y), B(mid.x, 1.6, mid.y)))
            e2 = mid - road * 15.0 - Lv * 14.0
            shots.append(("%s: open" % TYPES[t], ps, False, "l", B(e2.x, 5.0, e2.y), B(mid.x, 1.0, mid.y)))
    # Close-ups: the light units of the open crossing with lights (amber, then a red), the passive post.
    ps1 = [p for p in data if p["type"] == 1 and p["skew"] == 0]
    unit = [p for p in ps1 if p["role"] == 1][0]
    ue = Vector((unit["x"], unit["z"])) + Vector((unit["fx"], unit["fz"])) * 5.5
    shots.append(("light unit, amber first", ps1, True, "amber", B(ue.x - 0.8, 2.3, ue.y), B(unit["x"], 2.6, unit["z"])))
    ps0 = [p for p in data if p["type"] == 0 and p["skew"] == 0]
    post = [p for p in ps0 if p["mesh"] == "lx_give_way"][0]
    pe = Vector((post["x"], post["z"])) + Vector((post["fx"], post["fz"])) * 6.0
    shots.append(("passive crossing's post", ps0, False, "l", B(pe.x - 1.0, 2.0, pe.y), B(post["x"], 2.4, post["z"])))
    ps4 = [p for p in data if p["type"] == 3 and p["skew"] == 1]
    c4 = ps4[0]
    m4 = Vector((c4["mid"][0], c4["mid"][1]))
    shots.append(("full barrier on a skewed crossing, from above", ps4, True, "r", B(m4.x + 12, 18.0, m4.y + 10), B(m4.x, 0.0, m4.y)))
    ps5 = [p for p in data if p["type"] == 2 and p["skew"] == 1]
    c5 = ps5[0]
    m5 = Vector((c5["mid"][0], c5["mid"][1]))
    shots.append(("half barrier on a skewed lane, barriers coming down", ps5, True, "l", B(m5.x - 10, 3.0, m5.y + 18), B(m5.x, 1.0, m5.y)))
    ps3 = [p for p in data if p["type"] == 3 and p["skew"] == 0]
    c3 = ps3[0]
    m3 = Vector((c3["mid"][0], c3["mid"][1]))
    r3 = Vector(c3["road"])
    l3 = Vector((r3.y, -r3.x))
    e3 = m3 - r3 * 9.0 - l3 * 7.0
    shots.append(("full barrier, booms and skirts down", ps3, True, "l", B(e3.x, 1.7, e3.y), B(m3.x, 0.8, m3.y)))
    shots.append(("full barrier, booms up", ps3, False, "l", B(e3.x, 1.7, e3.y), B(m3.x, 2.5, m3.y)))
    if ONLY:
        shots = [s for i, s in enumerate(shots) if str(i) in ONLY]
    tmp = os.path.join(os.path.dirname(os.path.abspath(out)), "_lx_tiles")
    os.makedirs(tmp, exist_ok=True)
    paths = []
    last = None
    for i, (name, ps, closed, red, eye, look) in enumerate(shots):
        clear()
        ground(ps[0])
        crossing(ps, closed, red)
        cam.location = eye
        cam.rotation_euler = (look - eye).to_track_quat("-Z", "Y").to_euler()
        cam.data.lens = 30 if "eye" in name else 26
        if "above" in name:
            cam.data.lens = 24
        if "unit" in name or "post" in name:
            cam.data.lens = 40
        p = os.path.join(tmp, "%d.png" % i)
        sc.render.filepath = p
        bpy.ops.render.render(write_still=True)
        paths.append((p, name))
        print("shot", i, name, flush=True)
    # The sheet, two across, each tile captioned (Pillow).
    from PIL import Image, ImageDraw, ImageFont
    W, H = sc.render.resolution_x, sc.render.resolution_y
    cols = 2
    rows = (len(paths) + 1) // cols
    sheet = Image.new("RGB", (W * cols, (H + 34) * rows), (24, 24, 26))
    f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20)
    d = ImageDraw.Draw(sheet)
    for i, (p, name) in enumerate(paths):
        x, y = (i % cols) * W, (i // cols) * (H + 34)
        sheet.paste(Image.open(p).convert("RGB"), (x, y + 34))
        d.text((x + 10, y + 6), name, font=f, fill=(230, 230, 230))
    sheet.save(out)
    print("wrote", out)


main()
