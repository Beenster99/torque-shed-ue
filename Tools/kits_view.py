#!/usr/bin/env python3
"""[everyday kits] A cloud picture of a world_ride mesh dump (SimTest world_ride with WORLD_RIDE_MESH=FILE and
WORLD_RIDE_MESH_R): the ground and what stands on it (FILE: x z y r g b a vertex, three a triangle), the facade kit's
models (FILE.props), the prop kit's (FILE.kitprops, with WORLD_RIDE_PROPKIT=1) and the wall kit's (FILE.walls), lit by
a sun and sky in Cycles. Trees and grass (FILE.plants) are left out: the picture is of the things, not the planting.

    python3 Tools/kits_view.py FILE OUT.png --at X,Z --yaw DEG --pitch DEG --dist M [--fov DEG] [--size W,H]
           [--samples N] [--look Y]

The camera looks at (X, ground there + --look, Z) of the dump's frame (metres from its middle; Z is south) from
--dist metres, --yaw degrees round from the north (90 = from the east), --pitch degrees down. Needs bpy
(pip install bpy). Models are read from Data/RideUK/{Facade,Props,Walls} beside this script."""
import argparse
import math
import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "Data", "RideUK")
SKIP_KIT = {"parked_car"}   # (Fab cars on the PC only)


def read_tris(path):
    pos, col = [], []
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) < 6:
                continue
            x, z, y, r, g, b = (float(v) for v in p[:6])
            pos.append((x, -z, y))
            col.append((r, g, b, 1.0))
    n = len(pos) // 3 * 3
    return pos[:n], col[:n]


def ground_at(pos, x, y, reach=6.0):
    best, bz = 1e18, 0.0
    for p in pos[::3]:
        d = (p[0] - x) ** 2 + (p[1] - y) ** 2
        if d < best:
            best, bz = d, p[2]
    return bz


def make_mesh(name, pos, col):
    me = bpy.data.meshes.new(name)
    faces = [(i, i + 1, i + 2) for i in range(0, len(pos), 3)]
    me.from_pydata(pos, [], faces)
    me.update()
    attr = me.color_attributes.new("Col", "FLOAT_COLOR", "CORNER")
    for i, c in enumerate(col):
        attr.data[i].color = c
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    mat = bpy.data.materials.new(name + "_m")
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    ca = nt.nodes.new("ShaderNodeVertexColor")
    ca.layer_name = "Col"
    nt.links.new(ca.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.85
    me.materials.append(mat)
    for poly in me.polygons:
        poly.use_smooth = False
    return ob


_cache = {}


def load_glb(path):
    """The glb's objects as a hidden collection to instance (read once)."""
    if path in _cache:
        return _cache[path]
    if not os.path.exists(path):
        _cache[path] = None
        return None
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    coll = bpy.data.collections.new(os.path.basename(path))
    for o in new:
        for c in list(o.users_collection):
            c.objects.unlink(o)
        coll.objects.link(o)
    _cache[path] = coll
    return coll


def tinted(coll, rgb):
    """A copy of coll's materials' base colour multiplied by rgb (the facade kit's paint)."""
    key = (coll.name, tuple(round(v, 2) for v in rgb))
    if key in _cache:
        return _cache[key]
    new = bpy.data.collections.new(coll.name + "_t")
    for o in coll.objects:
        c = o.copy()
        if o.data is not None:
            c.data = o.data.copy()
            for i, m in enumerate(c.data.materials):
                if m is None:
                    continue
                m2 = m.copy()
                if m2.use_nodes and "Principled BSDF" in m2.node_tree.nodes:
                    b = m2.node_tree.nodes["Principled BSDF"].inputs["Base Color"]
                    if not b.is_linked:
                        v = b.default_value
                        b.default_value = (v[0] * rgb[0] * 1.6, v[1] * rgb[1] * 1.6, v[2] * rgb[2] * 1.6, 1.0)
                c.data.materials[i] = m2
        new.objects.link(c)
    _cache[key] = new
    return new


def place(coll, x, z, y, yaw, pitch=0.0, sx=1.0):
    e = bpy.data.objects.new("i", None)
    e.instance_type = "COLLECTION"
    e.instance_collection = coll
    e.location = (x, -z, y)
    e.rotation_euler = (0.0, pitch, yaw)
    e.scale = (sx, 1.0, 1.0)
    bpy.context.scene.collection.objects.link(e)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("out")
    ap.add_argument("--at", default="0,0")
    ap.add_argument("--yaw", type=float, default=200.0)
    ap.add_argument("--pitch", type=float, default=22.0)
    ap.add_argument("--dist", type=float, default=45.0)
    ap.add_argument("--fov", type=float, default=40.0)
    ap.add_argument("--size", default="1200,800")
    ap.add_argument("--samples", type=int, default=24)
    ap.add_argument("--look", type=float, default=2.0)
    ap.add_argument("--sun", default="135,38", help="sun azimuth from north, elevation, degrees")
    ap.add_argument("--no-models", action="store_true")
    a = ap.parse_args(argv)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    pos, col = read_tris(a.dump)
    make_mesh("world", pos, col)
    if not a.no_models:
        for line in open(a.dump + ".props") if os.path.exists(a.dump + ".props") else []:
            p = line.split()
            coll = load_glb(os.path.join(DATA, "Facade", "SM_Prop_%s_%s.glb" % (p[0], p[1])))
            if coll is None:
                continue
            rgb = [float(v) for v in p[6:9]] if len(p) >= 9 else [1, 1, 1]
            place(tinted(coll, rgb), float(p[2]), float(p[3]), float(p[4]), float(p[5]))
        for line in open(a.dump + ".kitprops") if os.path.exists(a.dump + ".kitprops") else []:
            p = line.split()
            if p[0] in SKIP_KIT:
                continue
            coll = load_glb(os.path.join(DATA, "Props", "SM_Prop_%s_%s.glb" % (p[0], p[1])))
            if coll is None:
                coll = load_glb(os.path.join(DATA, "Props", "SM_Prop_%s_0.glb" % p[0]))
            if coll is not None:
                place(coll, float(p[2]), float(p[3]), float(p[4]), float(p[5]), float(p[6]), float(p[7]))
        for line in open(a.dump + ".walls") if os.path.exists(a.dump + ".walls") else []:
            p = line.split()
            coll = load_glb(os.path.join(DATA, "Walls", "SM_Prop_%s_%s.glb" % (p[0], p[1])))
            if coll is not None:
                place(coll, float(p[2]), float(p[3]), float(p[4]), float(p[5]), float(p[6]), float(p[7]))
    # camera
    tx, tz = (float(v) for v in a.at.split(","))
    ty = ground_at(pos, tx, -tz) + a.look
    yaw, pitch = math.radians(a.yaw), math.radians(a.pitch)
    cx = tx + math.sin(yaw) * math.cos(pitch) * a.dist
    cy = -tz + math.cos(yaw) * math.cos(pitch) * a.dist
    cz = ty + math.sin(pitch) * a.dist
    cam = bpy.data.cameras.new("cam")
    cam.lens_unit = "FOV"
    cam.angle = math.radians(a.fov)
    cam.clip_start, cam.clip_end = 0.5, 5000.0
    co = bpy.data.objects.new("cam", cam)
    sc.collection.objects.link(co)
    co.location = (cx, cy, cz)
    d = (tx - cx, -tz - cy, ty - cz)
    import mathutils
    co.rotation_euler = mathutils.Vector(d).to_track_quat("-Z", "Y").to_euler()
    sc.camera = co
    # light: a sun and a sky
    saz, sel = (math.radians(float(v)) for v in a.sun.split(","))
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 3.2
    sun.angle = math.radians(2.0)
    so = bpy.data.objects.new("sun", sun)
    sc.collection.objects.link(so)
    sv = mathutils.Vector((math.sin(saz) * math.cos(sel), math.cos(saz) * math.cos(sel), math.sin(sel)))
    so.rotation_euler = (-sv).to_track_quat("-Z", "Y").to_euler()
    w = bpy.data.worlds.new("w")
    sc.world = w
    w.use_nodes = True
    bg = w.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.55, 0.68, 0.85, 1.0)
    bg.inputs["Strength"].default_value = 0.9
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = a.samples
    sc.cycles.use_denoising = True
    sc.view_settings.view_transform = "AgX"
    sc.view_settings.exposure = 0.3
    W, H = (int(v) for v in a.size.split(","))
    sc.render.resolution_x, sc.render.resolution_y = W, H
    sc.render.resolution_percentage = 100
    sc.render.filepath = os.path.abspath(a.out)
    bpy.ops.render.render(write_still=True)
    print("wrote", a.out)


if __name__ == "__main__":
    main(sys.argv[1:])
