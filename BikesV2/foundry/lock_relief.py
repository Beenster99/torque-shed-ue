"""Steering lock relief (beenster, Nov 16: "Fix now, small bodywork changes").

The bars, levers, switches and guards are swept through the bike's steering lock and the tank or fairing is
scalloped wherever that sweep would come within CLEAR of it, the way a real tank has cut-outs for the bars.
Nothing on the bars moves: pegs, hand points and controls stay exactly where they were signed off.

A model asks for it with detail.lock_relief = degrees each way. build3d runs it after the detail pass."""
import math
import bpy
import bmesh
from mathutils import Vector, Matrix
from mathutils.kdtree import KDTree

CLEAR = 15.0      # mm, the same clearance bars3d.lock_check asks for
MARGIN = 5.0      # mm more, so the check passes with room to spare


def relieve(B, model, deg, parts=("tank",), report=True):
    import bars3d
    MM = B.MM
    g, wh, fk = model["geometry"], model["wheels"], model["fork"]
    rake = math.radians(g["rake"])
    F, uf, nb = (g["wheelbase"], wh["front"]["tyre_r"]), (-math.sin(rake), math.cos(rake)), (-math.cos(rake), -math.sin(rake))
    stem = B.add(B.add(F, uf, fk["length"] - 10), nb, fk["offset"])
    axis_p = Vector(B.V(stem[0], 0, stem[1]))
    axis_d = Vector((uf[0], 0, uf[1])).normalized()
    sport = model["family"].lower() == "sport"
    pts = []
    for o in bpy.data.objects:
        if o.type not in ("MESH", "CURVE") or o.hide_render or o.name.startswith("cut_"):
            continue
        if (o.name.startswith(bars3d.STEERS) or bars3d.OLD_BAR.match(o.name)) and not (o.name.startswith(bars3d.MIRROR) and sport):
            vs, _ = bars3d._verts(o)
            vs = [v for v in vs if abs(v.y) > 0.120]
            pts += vs[::max(1, len(vs) // 500)]
    if not pts:
        return 0
    cloud = []
    n_ = int(deg)
    for k in range(-n_, n_ + 1):
        R = Matrix.Rotation(math.radians(deg * k / n_), 4, axis_d)
        cloud += [R @ (v - axis_p) + axis_p for v in pts]
    kd = KDTree(len(cloud))
    for i, q in enumerate(cloud):
        kd.insert(q, i)
    kd.balance()
    need = (CLEAR + MARGIN) * MM
    moved = 0
    for o in bpy.data.objects:
        if o.type != "MESH" or not o.name.startswith(tuple(parts)) or o.name.startswith("cut_"):
            continue
        mw, mi = o.matrix_world, o.matrix_world.inverted()
        if o.modifiers:
            # work on the finished surface, not the cage a modifier would smooth back into the sweep
            dg = bpy.context.evaluated_depsgraph_get()
            o.data = bpy.data.meshes.new_from_object(o.evaluated_get(dg))
            o.modifiers.clear()
        bm = bmesh.new()
        bm.from_mesh(o.data)
        dist = lambda co: kd.find(mw @ co)[2]
        zone = lambda f: any(dist(v.co) < 0.11 for v in f.verts)
        fs = [f for f in bm.faces if zone(f)]
        if not fs:
            bm.free()
            continue
        bmesh.ops.triangulate(bm, faces=fs)
        for _ in range(6):
            es = list({e for f in bm.faces if zone(f) for e in f.edges if e.calc_length() > 0.009})
            if not es:
                break
            bmesh.ops.subdivide_edges(bm, edges=es, cuts=1)
            bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 4])
        bm.verts.ensure_lookup_table()
        ws = [mw @ v.co for v in bm.verts]
        lo = Vector((min(q.x for q in ws), 0, min(q.z for q in ws)))
        hi = Vector((max(q.x for q in ws), 0, max(q.z for q in ws)))
        mid = (lo + hi) / 2
        zv = [v for v in bm.verts if kd.find(mw @ v.co)[2] < 0.11]
        orig = {v.index: (mw @ v.co).copy() for v in zv}

        def way(q):
            d = Vector(((mid.x - q.x) * 0.5, -q.y, (mid.z - q.z) * 0.5))
            return d.normalized() if d.length > 1e-6 else Vector((0, 0, -1))
        dirs = {i: way(q) for i, q in orig.items()}

        def reach(i):
            """how far (m) the point must draw back towards the middle of the part before the sweep clears it"""
            q, t = orig[i], 0.0
            while t < 0.16 and kd.find(q + dirs[i] * t)[2] < need and abs((q + dirs[i] * t).y) > 0.004:
                t += 0.001
            return t
        req = {i: reach(i) for i in orig}
        hit = sum(1 for t in req.values() if t > 0)
        if not hit:
            bm.free()
            continue
        nb_ = {v.index: [e.other_vert(v).index for e in v.link_edges if e.other_vert(v).index in orig] for v in zv}
        edge = {v.index for v in zv if any(e.other_vert(v).index not in orig for e in v.link_edges)}      # stays put: the join to the untouched surface
        # one smooth dish: spread the deepest need over its neighbours, then even it out; never shallower than needed
        t = dict(req)
        for _ in range(3):
            t = {i: max([t[i]] + [t[j] for j in nb_[i]]) for i in t}
        for _ in range(60):
            t = {i: 0.0 if i in edge else (t[i] + sum(t[j] for j in nb_[i]) / max(1, len(nb_[i]))) / 2 for i in t}
        t = {i: 0.0 if i in edge else max(t[i], req[i]) for i in t}
        cur = {i: orig[i] + dirs[i] * t[i] for i in orig}
        for v in zv:
            v.co = mi @ cur[v.index]
        moved += hit
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
        bm.to_mesh(o.data)
        o.data.update()
        bm.free()
        if report:
            print(f"lock relief: {o.name} {hit} points eased for {deg} deg", flush=True)
    return moved
