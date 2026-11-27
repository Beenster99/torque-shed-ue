"""Our own bikes parked outside houses (beenster Nov 26: "how hard would it be to park some of our bikes outside a house
now and then?" ... "yes"). Our own V2 bikes only (Data/BikesV2/<brand>_<family>.glb, the Bike Models V2 designs;
never the bought V3 bikes, which are never committed), each baked whole into one light static prop:

  - read from its glb and json: every face keeps its finish (the json's surfaces: paint, powder, chrome, alloy,
    rubber, lenses...), mapped onto the street kit's surfaces (M_RideStreetKit, one material for every prop);
    the body paint ("paint__body") in the colour this parked copy is given, so the same bike comes in several;
  - given its tyres (the game fits them at run time, so the glbs have none): turned from the json's sizes;
  - its side stand swung down (the json's stand pivot, axis and angle) and the whole bike leaned onto it, 12 degrees
    to its left as the game parks bikes (BikesV2/foundry/sidestand3d.py LEAN);
  - welded and cut down (Blender's collapse decimation) to a close-up version and a hand-made distance version, the
    thin open sheets (mudguards, panels drawn double-sided in the bike's own material) given their back faces so
    the one-sided prop material shows no holes (Tools/check_prop_faces.py).

Writes Data/RideUK/Props/SM_Prop_house_bike_<n>.glb and _LOD1, in the props' frame (front +x, foot at 0, y up in the
file), and the house_bike row in Data/RideUK/prop_kit.json. World/WorldRideDriveways.cpp parks one now and then
beside a drive's car (HouseBikeShare).

    python Tools/blender_house_bikes.py                # the models
    python Tools/blender_house_bikes.py --render DIR   # and a review sheet in DIR
"""
import json
import math
import os
import struct
import sys
from types import SimpleNamespace

import bpy
import bmesh
import numpy as np
from mathutils import Matrix, Vector
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_street_kit as K   # noqa: E402
from make_street_kit_textures import alpha_of   # noqa: E402

ROOT = os.path.dirname(HERE)
BIKES = os.path.join(ROOT, "Data", "BikesV2")
LEAN = 12.0
FACES0, FACES1 = 26000, 7000   # (the close-up and distance versions' triangles, before the back faces)

# (the bikes: our own V2 designs, each in its own colour or a second one; a street's mix of everyday bikes)
LINEUP_ALL = [
    ("kazeda_naked", None), ("kazeda_naked", (0.35, 0.012, 0.01)),
    ("varga_sport", None), ("hartwell_classic", None), ("hartwell_cafe", None),
    ("rheinhart_adventure", None), ("lodestar_cruiser", None), ("corvano_naked", None),
    ("rheinhart_classic", None), ("varga_naked", (0.6, 0.6, 0.58)),
]

LINEUP = LINEUP_ALL[:int(os.environ.get("BIKES_ONLY", len(LINEUP_ALL)))]

TYRE = (0.016, 0.016, 0.017)


# --- out: a Tagged piece's faces each take their own finish ---------------------------------------------------

class Tagged:
    """A piece whose faces are of several finishes: face material index -> (surface, colour, dirt)."""

    def __init__(self, obj, table):
        self.obj, self.table = obj, table
        self.surface, self.rgb, self.dirt = table[0]


def triangles(M):
    """As blender_street_kit.triangles, but a Tagged piece's faces each take their table row's finish."""
    dg = bpy.context.evaluated_depsgraph_get()
    P, N, C, U = [], [], [], []
    for part in M.parts:
        ev = part.obj.evaluated_get(dg)
        me = ev.to_mesh()
        me.calc_loop_triangles()
        mw = part.obj.matrix_world
        nw = mw.to_3x3().inverted().transposed()
        co = np.array([mw @ v.co for v in me.vertices], np.float64)
        cn = np.zeros((len(me.loops), 3))
        me.corner_normals.foreach_get("vector", cn.ravel())
        cn = np.array([nw @ Vector(n) for n in cn])
        cn /= np.maximum(np.linalg.norm(cn, axis=1, keepdims=True), 1e-9)
        table = getattr(part, "table", None)
        for lt in me.loop_triangles:
            vi = list(lt.vertices)
            li = list(lt.loops)
            p = co[vi]
            fn = np.cross(p[1] - p[0], p[2] - p[0])
            # (box-projected by each corner's own normal, so a smooth surface's corners share their UVs and weld)
            ax = np.argmax(np.abs(cn[li]), 1)[:, None]
            uv = np.where(ax == 2, p[:, [0, 1]], np.where(ax == 0, p[:, [1, 2]], p[:, [0, 2]]))
            P.append(p); N.append(cn[li]); U.append(uv)
            surface, rgb, d0 = table[min(lt.material_index, len(table) - 1)] if table else (part.surface, part.rgb, part.dirt)
            dirt = np.clip(d0 + 0.3 * np.clip(1.0 - (p[:, 2] + 0.1) / 0.6, 0, 1), 0, 1)
            rgba = np.zeros((3, 4))
            rgba[:, :3] = rgb
            rgba[:, 3] = [alpha_of(surface, d) for d in dirt]
            C.append(rgba)
        ev.to_mesh_clear()
    P = np.concatenate(P); N = np.concatenate(N); C = np.concatenate(C); U = np.concatenate(U)
    to_frame = lambda a: np.stack([a[:, 0], a[:, 2], -a[:, 1]], 1)
    col = np.round(np.clip(C, 0, 1) * 255.0)
    return to_frame(P), to_frame(N), col.astype(np.uint8), U


def write(M):
    pos, nrm, col, uv = triangles(M)
    # (corners that agree in place, normal, colour and UV shared: a smooth panel's corners are one vertex, so the
    # file and the mesh are a fraction of one-vertex-per-corner; hard edges and finish changes stay split)
    key = np.concatenate([np.round(pos, 5), np.round(nrm, 3), col.astype(np.float64), np.round(uv, 5)], 1)
    _, first, inverse = np.unique(key, axis=0, return_index=True, return_inverse=True)
    pos, nrm, col, uv = pos[first], nrm[first], col[first], uv[first]
    tris = inverse.reshape(-1, 3)
    path = os.path.join(K.OUT, M.name + ".glb")
    K.write_glb(path, pos, tris, nrm, col, M.name, uv)
    return len(tris), pos[:, 1].min(), pos[:, 1].max(), pos[:, 0].min(), pos[:, 0].max(), pos[:, 2].min(), pos[:, 2].max()




def finish(s, paint):
    """The street kit's surface, colour and dirt for one of the bike's surfaces (its json row)."""
    ue = s.get("ue", "")
    col = tuple(s.get("color", [])[:3]) or None
    if s.get("surface", "").startswith("paint__body"):
        return ("enamel", paint or col or (0.3, 0.3, 0.3), 0.05)
    if ue == "paint_silver" or ue == "cat_powder":
        return ("enamel", col or (0.05, 0.05, 0.05), 0.08)
    if ue == "chrome":
        return ("galv", (0.66, 0.67, 0.68), 0.05)
    if ue in ("cat_brushed_steel", "cat_polished_alloy", "cat_cast_alloy", "cat_titanium"):
        return ("galv", col or (0.5, 0.5, 0.52), 0.2)
    if ue in ("lens_red",):
        return ("lens", (0.42, 0.012, 0.008), 0.0)
    if ue in ("lens_amber",):
        return ("lens", (0.5, 0.18, 0.01), 0.0)
    if ue in ("lens_clear", "v2glass"):
        return ("lens", (0.6, 0.6, 0.58), 0.0)
    if ue == "v2screen":
        return ("lens", (0.03, 0.035, 0.04), 0.0)   # (a tinted screen: the prop material can't be see-through)
    if ue == "mirror_glass":
        return ("lens", (0.25, 0.27, 0.3), 0.0)
    if ue == "cat_rubber" or ue == "grip":
        return ("paint", (0.016, 0.016, 0.017), 0.4)
    return ("paint", col or (0.025, 0.025, 0.028), 0.2)


def read_glb(path):
    """Positions (n, 3) and, per primitive, its material name and triangles."""
    b = open(path, "rb").read()
    jl = struct.unpack("<I", b[12:16])[0]
    js = json.loads(b[20:20 + jl])
    binary = b[20 + jl + 8:]

    def acc(i):
        a = js["accessors"][i]
        bv = js["bufferViews"][a["bufferView"]]
        n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        dt = np.dtype({5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a["componentType"]])
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = bv.get("byteStride", 0) or n * dt.itemsize
        raw = np.frombuffer(binary, np.uint8, (a["count"] - 1) * stride + n * dt.itemsize, off)
        out = np.lib.stride_tricks.as_strided(raw, (a["count"], n * dt.itemsize), (stride, 1)).copy().view(dt)
        return out.reshape(a["count"], n)
    P, T, M = [], [], []
    base = 0
    for mesh in js["meshes"]:
        for pr in mesh["primitives"]:
            p = acc(pr["attributes"]["POSITION"]).astype(np.float64)
            i = acc(pr["indices"]).reshape(-1, 3).astype(np.int64)
            P.append(p)
            T.append(i + base)
            M += [js["materials"][pr["material"]]["name"]] * len(i)
            base += len(p)
    return np.concatenate(P), np.concatenate(T), M


def tyre(name, at, R, w, rim, seg=36):
    """A tyre turned about the axle (along y): tread crown, shoulders, a sidewall down to the rim."""
    h = w / 2
    prof = [(rim * 0.98, -h * 0.82), (rim + (R - rim) * 0.35, -h), (R - 0.03, -h * 0.98), (R - 0.008, -h * 0.75), (R, -h * 0.3),
            (R, h * 0.3), (R - 0.008, h * 0.75), (R - 0.03, h * 0.98), (rim + (R - rim) * 0.35, h), (rim * 0.98, h * 0.82)]
    # (open: no flat caps at the bead, or they'd be solid discs hiding the wheel)
    verts, faces = [], []
    for r, z in prof:
        for k in range(seg):
            a = 2 * math.pi * k / seg
            verts.append((r * math.cos(a), r * math.sin(a), z))
    for i in range(len(prof) - 1):
        A, B = i * seg, (i + 1) * seg
        faces += [(A + k, A + (k + 1) % seg, B + (k + 1) % seg, B + k) for k in range(seg)]
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.validate()
    for poly in me.polygons:
        poly.use_smooth = True
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    ob.matrix_world = Matrix.Translation(at) @ Matrix.Rotation(math.pi / 2, 4, "X")
    return ob


def bake(model, paint, n, lod):
    """One parked bike: its own model, coloured, tyred, on its stand, leaned, cut down to lod's size."""
    j = json.load(open(os.path.join(BIKES, model + ".json")))
    P, T, mats = read_glb(os.path.join(BIKES, model + ".glb"))
    # (the glb's frame, y up and z the bike's right, to the bike's: x forward from the rear axle, y left, z up)
    V = np.stack([P[:, 0], -P[:, 2], P[:, 1]], 1)
    surf = j["surfaces"]
    names = sorted(set(mats))
    table = [finish(surf.get(m, {}), paint) for m in names]
    index = {m: k for k, m in enumerate(names)}
    stand = set(m for m in names if surf.get(m, {}).get("slot") == "side_stand")
    pts = j["points_mm"]
    st = pts.get("stand")
    if st:
        # (the stand down: its parts swung about the pivot's axis by its angle, folded to down)
        piv = np.array(st["pivot"]) / 1000.0
        R = np.array(Matrix.Rotation(math.radians(st["angle_down_deg"]), 3, Vector(st["axis"])))
        on = np.zeros(len(V), bool)
        for t, m in zip(T, mats):
            if m in stand:
                on[t] = True
        V[on] = (V[on] - piv) @ R.T + piv
    me = bpy.data.meshes.new(model)
    me.from_pydata([tuple(v) for v in V], [], [tuple(t) for t in T])
    me.validate()
    for poly, m in zip(me.polygons, mats):
        poly.material_index = index[m]
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0004)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(model, me)
    bpy.context.scene.collection.objects.link(ob)
    for k in range(len(table)):
        me.materials.append(bpy.data.materials.new("m%d" % k))
    # (the tyres: their own finish, a row of the table)
    stats = pts["stats"]
    tyre_k = len(table)
    table.append(("paint", TYRE, 0.45))
    me.materials.append(bpy.data.materials.new("tyre"))
    seg = 36 if lod == 0 else 12
    for key, r, wd, rim in (("front_axle", "front_radius", "front_width", "front_rim"), ("rear_axle", "rear_radius", "rear_width", "rear_rim")):
        ax = np.array(pts[key]) / 1000.0
        t = tyre("tyre", Vector(ax), stats[r] / 1000.0, stats[wd] / 1000.0, stats[rim] / 1000.0, seg)
        t.data.materials.append(me.materials[tyre_k])
        for poly in t.data.polygons:
            poly.material_index = 0
        bpy.context.view_layer.objects.active = ob
        with bpy.context.temp_override(active_object=ob, selected_editable_objects=[ob, t], object=ob):
            bpy.ops.object.join()
    # (the distance version first loses the small loose bits, bolts, cables, levers, lamps' insides: unseen from
    # past a few metres, and they would keep the cut-down mesh from getting small)
    if lod != 0:
        bm = bmesh.new()
        bm.from_mesh(me)
        bm.verts.ensure_lookup_table()
        seen, drop = set(), []
        for v0 in bm.verts:
            if v0.index in seen:
                continue
            stack, isl = [v0], []
            seen.add(v0.index)
            while stack:
                v = stack.pop()
                isl.append(v)
                for e in v.link_edges:
                    o = e.other_vert(v)
                    if o.index not in seen:
                        seen.add(o.index)
                        stack.append(o)
            co = np.array([v.co for v in isl])
            if np.linalg.norm(co.max(0) - co.min(0)) < 0.09:
                drop += isl
        bmesh.ops.delete(bm, geom=drop, context="VERTS")
        bm.to_mesh(me)
        bm.free()
    # (cut down: Blender's collapse decimation on the welded mesh, faces keep their finish)
    target = FACES0 if lod == 0 else FACES1
    now = sum(len(p.vertices) - 2 for p in me.polygons)
    if now > target:
        d = ob.modifiers.new("cut", "DECIMATE")
        d.decimate_type = "COLLAPSE"
        d.ratio = target / now
        d.use_collapse_triangulate = True
        with bpy.context.temp_override(object=ob, active_object=ob):
            bpy.ops.object.modifier_apply(modifier="cut")
    # (leaned onto its stand about the line the tyres touch the floor along, then its middle over the origin, foot 0)
    ob.matrix_world = Matrix.Rotation(math.radians(-LEAN), 4, "X")
    bpy.context.view_layer.update()
    co = np.array([ob.matrix_world @ v.co for v in me.vertices])
    mid = (pts["front_axle"][0] + pts["rear_axle"][0]) / 2000.0
    ob.matrix_world = Matrix.Translation((-mid, 0.0, -co[:, 2].min())) @ ob.matrix_world
    for poly in me.polygons:
        poly.use_smooth = True
    me.set_sharp_from_angle(angle=math.radians(40))
    # (the back faces: every face again, turned round and lifted a hair off, so open sheets show from both sides)
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.normal_update()
    faces = list(bm.faces)
    dup = bmesh.ops.duplicate(bm, geom=faces)
    new_faces = [g for g in dup["geom"] if isinstance(g, bmesh.types.BMFace)]
    bmesh.ops.reverse_faces(bm, faces=new_faces)
    for v in set(v for f in new_faces for v in f.verts):
        v.co -= v.normal * 0.0006
    bm.to_mesh(me)
    bm.free()
    M = K.Model("SM_Prop_house_bike_%d%s" % (n, "" if lod == 0 else "_LOD1"))
    M.parts.append(Tagged(ob, table))
    M.title = "%s%s" % (model.replace("_", " ").title(), "" if paint is None else ", repainted")
    return M


def main():
    args = sys.argv[1:]
    out = {}
    for lod in (0, 1):
        models = []
        K.clear()
        for n, (model, paint) in enumerate(LINEUP):
            M = bake(model, paint, n, lod)
            nt, y0, y1, x0, x1, z0, z1 = write(M)
            out[M.name] = y1 - y0
            print("%-28s %-28s %6d triangles, %.2f m high, %.2f x %.2f m" % (M.name, M.title, nt, y1 - y0, x1 - x0, z1 - z0), flush=True)
            if "--render" in args:
                models.append(M)
        if "--render" in args:
            render_sheet(models, args[args.index("--render") + 1], lod)
    write_row(max(v for k, v in out.items() if not k.endswith("_LOD1")))


def write_row(height):
    """The house_bike row: as modelled (height 0), a hand-made distance version, near reach."""
    row = {"words": [], "reach": 160, "height": 0, "shadow": True, "credit": "", "hand_lods": [0.12]}
    lines = open(K.KIT).read().split("\n")
    head = '  "house_bike": '
    for i, ln in enumerate(lines):
        if ln.startswith(head):
            lines[i] = head + json.dumps(row) + ("," if ln.rstrip().endswith(",") else "")
            break
    else:
        at = max(i for i, ln in enumerate(lines) if ln.startswith('  "') and ln.rstrip().endswith("},"))
        lines.insert(at + 1, head + json.dumps(row) + ",")
    open(K.KIT, "w").write("\n".join(lines))
    json.load(open(K.KIT))


# --- the review sheet: each bike from its stand side and its other side, Cycles on the CPU (the warehouse kit's
# lighting and stand-in materials, Tools/blender_warehouse_kit_render.py) ---------------------------------------

def _R():
    import blender_warehouse_kit_render as R
    R.LOOK.update({"galv": (0.18, 1.0, None), "enamel": (0.3, 0.0, None), "lens": (0.04, 0.0, None), "paint": (0.6, 0.0, None)})
    return R


def render_sheet(models, out_dir, lod=0, title=None):
    os.makedirs(out_dir, exist_ok=True)

    class _U:
        def __init__(self, M):
            self.name, self.parts, self.title = M.name, M.parts, getattr(M, "title", M.name)
            for p in self.parts:
                p.lod = 2
    units = [_U(M) for M in models]
    # (_R()._scene clears each piece's materials, which drops a tagged piece's faces' indices: kept, then put back)
    kept = {id(p): [poly.material_index for poly in p.obj.data.polygons] for U in units for p in U.parts if getattr(p, "table", None)}
    sc = _R()._scene(units)
    cache = {}
    for U in units:
        for p in U.parts:
            table = getattr(p, "table", None)
            if table:
                p.obj.data.materials.clear()
                for surface, rgb, dirt in table:
                    p.obj.data.materials.append(_R()._material(cache, SimpleNamespace(surface=surface, rgb=rgb)))
                for poly, m in zip(p.obj.data.polygons, kept[id(p)]):
                    poly.material_index = m
    size = (520, 300) if lod == 0 else (260, 150)
    shots = []
    for U in units:
        pair = []
        for tag, yaw in (("front", 32), ("rear", 222)) if lod == 0 else (("front", 32),):
            a = os.path.join(out_dir, "%s_%s.png" % (U.name, tag))
            _R()._shoot(sc, U, yaw, 14, size, a, lens=50.0)
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
    d.text((pad, 18), title or ("Ride UK: our own bikes parked outside houses (Blender renders of the baked props)" if lod == 0 else
                                "Their hand-made distance versions"), fill=(20, 20, 20), font=_R()._font(24))
    for i, (U, pair) in enumerate(shots):
        x = pad + (i % cols) * (cw + pad)
        y = 70 + (i // cols) * (size[1] + head + pad)
        d.text((x, y + 2), "%s   (%s)" % (U.title, U.name.replace("SM_Prop_house_bike_", "#")), fill=(25, 25, 25), font=_R()._font(15))
        for k, a in enumerate(pair):
            sheet.paste(Image.open(a).convert("RGB"), (x + k * (size[0] + pad), y + head))
    path = os.path.join(out_dir, "house_bikes%s.png" % ("" if lod == 0 else "_lod1"))
    sheet.save(path)
    print("sheet:", path)
    return path


if __name__ == "__main__":
    main()
