# What FTorqueLights::Hook will do, worked out from the game files: lamps per bike and where each beam starts.
import bpy, sys, json, glob, os
from mathutils import Vector
res = {}
files = sorted(f for f in glob.glob("Data/BikesV2/*_*.glb") if not os.path.basename(f).startswith("_")) + sorted(glob.glob("Data/BikesV2/common/headlamp/*.glb"))
for f in files:
    name = os.path.basename(f)[:-4]
    S_ = json.load(open(f[:-4] + ".json"))["surfaces"]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=f)
    surf = {}
    for ob in bpy.data.objects:
        if ob.type != "MESH": continue
        me = ob.data; me.calc_loop_triangles(); mw = ob.matrix_world
        co = [mw @ v.co * 100 for v in me.vertices]      # cm; UE: x forward, y right = -blender y (mirror: twin either way)
        for t in me.loop_triangles:
            k = me.materials[t.material_index].name.split(".")[0]
            surf.setdefault(k, []).append([co[i] for i in t.vertices])
    common = "common" in f
    tagged = []; shells = []
    for k, tris in surf.items():
        s = S_.get(k, {}); tag = s.get("tag") or k
        slot = "headlight" if common else s.get("slot")
        if slot not in ("headlight", "number_boards"): continue
        pts = [p for t in tris for p in t]
        shells.append(([min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)], s.get("node", "")))
        if tag != "lens_white": continue
        near = min(abs(p.y) for p in pts); twin = near >= 1.0 and min(p.y for p in pts) < 0 < max(p.y for p in pts)
        for side in ((-1, 1) if twin else (0,)):
            mid = Vector(); area = 0; front = -1e9
            for a, b, c in tris:
                if side and (a.y + b.y + c.y) * side <= 0: continue
                tw = (b - a).cross(c - a).length; mid += (a + b + c) * (tw / 3); area += tw; front = max(front, a.x, b.x, c.x)
            if area > 1e-6:
                mid /= area; tagged.append(Vector((front, 0 if side == 0 else mid.y, mid.z)))
    if not tagged:
        res[name] = None; continue
    if any(abs(p.y) < 1 for p in tagged):
        cs = [p for p in tagged if abs(p.y) < 1]
        lamps = [Vector((max(p.x for p in tagged), 0, sum(p.z for p in cs) / len(cs)))]
    else:
        lamps = []
        for side in (-1, 1):
            ps = [p for p in tagged if p.y * side > 0]
            if ps: lamps.append(Vector((max(p.x for p in ps), sum(p.y for p in ps) / len(ps), sum(p.z for p in ps) / len(ps))))
    out = []
    for at in lamps:
        front = at.x + 3
        for lo, hi, node in shells:
            if lo[1] - 1 < at.y < hi[1] + 1 and lo[2] - 1 < at.z < hi[2] + 1 and hi[0] < at.x + 25: front = max(front, hi[0] + 1.5)
        out.append(dict(lens_front=round(at.x, 1), beam_x=round(front, 1), y=round(at.y, 1), z=round(at.z, 1)))
    res[name] = out
json.dump(res, open(sys.argv[-1], "w"), indent=1)
