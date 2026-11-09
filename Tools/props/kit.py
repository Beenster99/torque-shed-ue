"""Modelling kit for the shed props (run inside Blender: see make_props.py).

Every prop is written in Torque Shed's own local frame, so its numbers can be read straight across
from the Godot scripts (equipment_view.gd, workshop.gd, dyno_room.gd...): metres, x right, y up,
+z the front (the side you work at / push from), the origin on the floor under the middle of the
footprint. The glTF exporter turns that into glTF's frame (y up, +z front), which the UE glTF import
lays down with its front facing the shed's +y (towards the roller door) at yaw 0.

Pieces are bevelled (no razor edges: that's most of what makes a primitive look real), smooth
shaded with weighted normals, UV'd by box projection in metres (UV 1.0 = 1 m, so the scanned
materials tile at the same density on every prop), then joined into one mesh with one material slot
per material key in Data/Props/materials.json.
"""
import math

import bmesh
import bpy
from mathutils import Matrix, Vector

_MATS = {}     # material key -> its spec from materials.json
_pieces = []   # the current prop's objects


def set_materials(spec):
    _MATS.clear()
    _MATS.update(spec)


def B(p):
    """Torque Shed local (x right, y up, +z front) -> Blender (x right, -y front, z up)."""
    return Vector((p[0], -p[2], p[1]))


def reset():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for m in list(bpy.data.meshes):
        bpy.data.meshes.remove(m)
    for c in list(bpy.data.curves):
        bpy.data.curves.remove(c)
    _pieces.clear()


def material(key):
    m = bpy.data.materials.get(key)
    if m:
        return m
    spec = _MATS.get(key)
    if spec is None:
        raise KeyError("no material %r in Data/Props/materials.json" % key)
    m = bpy.data.materials.new(key)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    c = spec.get("tint", [0.5, 0.5, 0.5])
    bsdf.inputs["Base Color"].default_value = (c[0], c[1], c[2], 1.0)
    bsdf.inputs["Roughness"].default_value = spec.get("rough", 0.5)
    bsdf.inputs["Metallic"].default_value = spec.get("metal", 0.0)
    if spec.get("emissive"):
        bsdf.inputs["Emission Color"].default_value = (c[0], c[1], c[2], 1.0)
        bsdf.inputs["Emission Strength"].default_value = spec["emissive"]
    if spec.get("opacity", 1.0) < 1.0:
        bsdf.inputs["Alpha"].default_value = spec["opacity"]
        m.blend_method = "BLEND"
    return m


def _finish(obj, mat, bevel):
    obj.data.materials.append(material(mat))
    if bevel and bevel > 0.0:
        mod = obj.modifiers.new("bevel", "BEVEL")
        mod.width = bevel
        mod.segments = 2 if bevel < 0.01 else 3
        mod.limit_method = "ANGLE"
        mod.angle_limit = math.radians(40)
        mod.harden_normals = False
    _pieces.append(obj)
    return obj


def _new(name, bm):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(obj)
    return obj


def _place(obj, centre, rot):
    """rot: (x, y, z) Euler degrees in Torque Shed's frame, applied x then y then z."""
    m = Matrix.Identity(4)
    if rot:
        rx, ry, rz = (math.radians(a) for a in rot)
        # Torque Shed's x, y, z axes are Blender's x, z, -y; Godot's Euler order (YXZ: Y * X * Z).
        m =Matrix.Rotation(ry, 4, Vector((0, 0, 1))) @ Matrix.Rotation(rx, 4, Vector((1, 0, 0))) @ Matrix.Rotation(rz, 4, Vector((0, -1, 0)))
    obj.matrix_world = Matrix.Translation(B(centre)) @ m


def box(name, centre, size, mat, bevel=0.004, rot=None):
    """A box `size` (x, y, z in Torque Shed's frame) about `centre`."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=Vector((size[0], size[2], size[1])), verts=bm.verts)
    obj = _new(name, bm)
    _place(obj, centre, rot)
    return _finish(obj, mat, min(bevel, min(size) * 0.45) if bevel else 0.0)


def span(name, lo, hi, mat, bevel=0.004):
    """A box from corner `lo` to corner `hi`."""
    c = [(a + b) * 0.5 for a, b in zip(lo, hi)]
    s = [abs(b - a) for a, b in zip(lo, hi)]
    return box(name, c, s, mat, bevel)


def cyl(name, centre, radius, length, mat, axis="y", verts=32, bevel=0.003, radius2=None, rot=None):
    """A cylinder (a cone when radius2 is given) along Torque Shed's `axis` through `centre`."""
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=verts, radius1=radius,
                          radius2=radius if radius2 is None else radius2, depth=length)
    # create_cone builds along Blender z (= TS y). Turn it onto the axis asked for.
    if axis == "x":
        bmesh.ops.rotate(bm, verts=bm.verts, matrix=Matrix.Rotation(math.radians(90), 3, "Y"))
    elif axis == "z":
        bmesh.ops.rotate(bm, verts=bm.verts, matrix=Matrix.Rotation(math.radians(-90), 3, "X"))
    obj = _new(name, bm)
    _place(obj, centre, rot)
    return _finish(obj, mat, min(bevel, radius * 0.4, length * 0.4) if bevel else 0.0)


def sphere(name, centre, radius, mat, scale=(1, 1, 1)):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=12, radius=radius)
    bmesh.ops.scale(bm, vec=Vector((scale[0], scale[2], scale[1])), verts=bm.verts)
    obj = _new(name, bm)
    _place(obj, centre, None)
    return _finish(obj, mat, 0.0)


def torus(name, centre, major, minor, mat, axis="y", verts=32):
    """A ring (a tyre, a hose coil) round Torque Shed's `axis`."""
    bm = bmesh.new()
    segs, ring = verts, 12
    rows = []
    for i in range(segs):
        a = 2 * math.pi * i / segs
        row = []
        for j in range(ring):
            b = 2 * math.pi * j / ring
            r = major + minor * math.cos(b)
            row.append(bm.verts.new((r * math.cos(a), r * math.sin(a), minor * math.sin(b))))
        rows.append(row)
    for i in range(segs):
        for j in range(ring):
            a, b = rows[i][j], rows[(i + 1) % segs][j]
            c, d = rows[(i + 1) % segs][(j + 1) % ring], rows[i][(j + 1) % ring]
            bm.faces.new((a, b, c, d))
    if axis == "x":
        bmesh.ops.rotate(bm, verts=bm.verts, matrix=Matrix.Rotation(math.radians(90), 3, "Y"))
    elif axis == "z":
        bmesh.ops.rotate(bm, verts=bm.verts, matrix=Matrix.Rotation(math.radians(-90), 3, "X"))
    obj = _new(name, bm)
    _place(obj, centre, None)
    return _finish(obj, mat, 0.0)


def tube(name, points, radius, mat, sides=12, caps=True):
    """A round bar (a handle, a hose, a bent tube frame) along `points` in Torque Shed's frame."""
    pts = [B(p) for p in points]
    bm = bmesh.new()
    rings = []
    for i, p in enumerate(pts):
        if i == 0:
            t = (pts[1] - pts[0]).normalized()
        elif i == len(pts) - 1:
            t = (pts[-1] - pts[-2]).normalized()
        else:
            t = ((pts[i] - pts[i - 1]).normalized() + (pts[i + 1] - pts[i]).normalized()).normalized()
        up = Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))
        u = t.cross(up).normalized()
        v = t.cross(u).normalized()
        rings.append([bm.verts.new(p + (u * math.cos(2 * math.pi * k / sides) + v * math.sin(2 * math.pi * k / sides)) * radius)
                      for k in range(sides)])
    for i in range(len(rings) - 1):
        for k in range(sides):
            bm.faces.new((rings[i][k], rings[i + 1][k], rings[i + 1][(k + 1) % sides], rings[i][(k + 1) % sides]))
    if caps:
        bm.faces.new(list(reversed(rings[0])))
        bm.faces.new(rings[-1])
    obj = _new(name, bm)
    return _finish(obj, mat, 0.0)


def bent(points, r):
    """Round the corners of a polyline with radius r (for bent tube frames and handles)."""
    if len(points) < 3:
        return points
    out = [points[0]]
    for i in range(1, len(points) - 1):
        p0, p1, p2 = (Vector(points[i - 1]), Vector(points[i]), Vector(points[i + 1]))
        a, b = (p0 - p1), (p2 - p1)
        la, lb = a.length, b.length
        rr = min(r, la * 0.45, lb * 0.45)
        a.normalize(), b.normalize()
        s, e = p1 + a * rr, p1 + b * rr
        for k in range(6):
            t = k / 5.0
            q = (1 - t) ** 2 * s + 2 * (1 - t) * t * p1 + t * t * e
            out.append(tuple(q))
    out.append(points[-1])
    return out


def prism(name, outline, depth, mat, axis="x", centre=0.0, bevel=0.003):
    """A flat shape (a list of (u, v) points) pushed `depth` along `axis`, centred on `centre`.
    axis "x": the outline is in (z, y); "z": in (x, y); "y": in (x, z)."""
    bm = bmesh.new()
    def at(u, v, w):
        if axis == "x":
            return B((w, v, u))
        if axis == "z":
            return B((u, v, w))
        return B((u, w, v))
    lo = [bm.verts.new(at(u, v, centre - depth * 0.5)) for u, v in outline]
    hi = [bm.verts.new(at(u, v, centre + depth * 0.5)) for u, v in outline]
    f0 = bm.faces.new(lo)
    f1 = bm.faces.new(hi)
    n = len(outline)
    for i in range(n):
        bm.faces.new((lo[i], lo[(i + 1) % n], hi[(i + 1) % n], hi[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    obj = _new(name, bm)
    return _finish(obj, mat, bevel)


def castor(name, at, wheel_r, mat_fork="chrome", mat_wheel="rubber", swivel=True):
    """A swivel castor whose wheel touches the floor at `at` (x, z): plate, fork, wheel, hub."""
    x, z = at
    h = wheel_r * 2.0
    cyl(name + "_Wheel", (x, wheel_r, z), wheel_r, wheel_r * 0.7, mat_wheel, axis="x", bevel=wheel_r * 0.25)
    cyl(name + "_Hub", (x, wheel_r, z), wheel_r * 0.45, wheel_r * 0.75, "steel", axis="x", bevel=0.002)
    for s in (-1, 1):
        box(name + "_Fork%d" % s, (x + s * wheel_r * 0.45, wheel_r * 1.25, z - wheel_r * 0.25), (0.004, wheel_r * 1.4, wheel_r * 1.1), mat_fork, bevel=0.001)
    if swivel:
        cyl(name + "_Swivel", (x, h + 0.008, z - wheel_r * 0.25), wheel_r * 0.6, 0.016, mat_fork, bevel=0.002)
    box(name + "_Plate", (x, h + 0.02, z - wheel_r * 0.25), (wheel_r * 1.3, 0.006, wheel_r * 1.3), mat_fork, bevel=0.002)


def label(name, centre, size, mat="label_white"):
    """A thin plate (a name plate, a sticker) facing +z."""
    return box(name, centre, (size[0], size[1], 0.002), mat, bevel=0.0008)


def bolt_row(name, start, step, count, radius, mat="steel", axis="z", length=0.006):
    for i in range(count):
        p = [start[k] + step[k] * i for k in range(3)]
        cyl("%s_%d" % (name, i), p, radius, length, mat, axis=axis, verts=6, bevel=0.0005)


# --- Finishing ---------------------------------------------------------------------------------------

def _box_uv(obj):
    """UVs by box projection in metres, from each face's own direction."""
    me = obj.data
    bm = bmesh.new()
    bm.from_mesh(me)
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        n = f.normal
        ax = max(range(3), key=lambda k: abs(n[k]))
        for l in f.loops:
            p = l.vert.co
            if ax == 0:
                l[uv].uv = (p.y * (1 if n.x > 0 else -1), p.z)
            elif ax == 1:
                l[uv].uv = (p.x * (-1 if n.y > 0 else 1), p.z)
            else:
                l[uv].uv = (p.x, p.y * (1 if n.z > 0 else -1))
    # Printed things (a label from an atlas) get 0..1 over their front face instead of metres.
    for i, slot in enumerate(obj.material_slots):
        if not (_MATS.get(slot.material.name, {}).get("unit_uv")):
            continue
        loops = [l for f in bm.faces if f.material_index == i for l in f.loops]
        if not loops:
            continue
        us = [l[uv].uv.x for l in loops]
        vs = [l[uv].uv.y for l in loops]
        u0, u1, v0, v1 = min(us), max(us), min(vs), max(vs)
        for l in loops:
            l[uv].uv = ((l[uv].uv.x - u0) / max(1e-6, u1 - u0), (l[uv].uv.y - v0) / max(1e-6, v1 - v0))
    bm.to_mesh(me)
    bm.free()


def finish(name):
    """Apply every piece's modifiers, join them into one object `name`, box-UV it, shade it."""
    dg = bpy.context.evaluated_depsgraph_get()
    objs = []
    for o in _pieces:
        if o.modifiers:
            me = bpy.data.meshes.new_from_object(o.evaluated_get(dg))
            o.modifiers.clear()
            old = o.data
            o.data = me
            bpy.data.meshes.remove(old)
        o.data.transform(o.matrix_world)
        o.matrix_world = Matrix.Identity(4)
        objs.append(o)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = name
    obj.data.name = name
    # (join already shares one slot per material between the pieces)
    _box_uv(obj)
    for p in obj.data.polygons:
        p.use_smooth = True
    mod = obj.modifiers.new("wn", "WEIGHTED_NORMAL")
    mod.keep_sharp = True
    try:
        obj.data.set_sharp_from_angle(angle=math.radians(35))
    except Exception:
        pass
    bpy.ops.object.modifier_apply(modifier="wn")
    _pieces.clear()
    return obj


def export(path):
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=False,
                              export_apply=True, export_yup=True, export_materials="EXPORT",
                              export_image_format="NONE")


def bounds(obj):
    """(min, max) in Torque Shed's frame."""
    cs = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    ts = [(c.x, c.z, -c.y) for c in cs]
    lo = tuple(min(t[k] for t in ts) for k in range(3))
    hi = tuple(max(t[k] for t in ts) for k in range(3))
    return lo, hi
