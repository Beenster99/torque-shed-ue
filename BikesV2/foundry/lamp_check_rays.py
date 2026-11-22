# How much of each beam the bike itself blocks: rays from the beam's start through the main beam's bright middle
# (13.5 degrees half-angle, 0.6 down) and the dipped beam's (9.6, 5 down), against the whole bike. Old start: one
# beam from the middle of the lenses, 3 cm ahead of the lens. New: FTorqueLights::Hook's (sim.json).
import bpy, sys, json, glob, os, math
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sim = json.load(open(sys.argv[-2])); out = {}
for name, lamps in sim.items():
    f = f"Data/BikesV2/{name}.glb"
    if not lamps or not os.path.exists(f): continue
    bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=f)
    vs, fs = [], []
    for ob in bpy.data.objects:
        if ob.type != "MESH": continue
        me = ob.data; me.calc_loop_triangles(); mw = ob.matrix_world; b = len(vs)
        vs += [mw @ v.co for v in me.vertices]; fs += [tuple(b + i for i in t.vertices) for t in me.loop_triangles]
    tree = BVHTree.FromPolygons(vs, fs)
    def blocked(at, down, half):
        n = hit = 0
        for i in range(-6, 7):
            for j in range(-6, 7):
                a, e = math.radians(i * half / 6), math.radians(j * half / 6 - down)
                if i * i + j * j > 36: continue
                d = Vector((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)))
                n += 1; hit += tree.ray_cast(Vector(at) / 100, d, 3.0)[0] is not None
        return round(100 * hit / n)
    old = (max(l["lens_front"] for l in lamps) + 3, 0, sum(l["z"] for l in lamps) / len(lamps))
    new = [(l["beam_x"], l["y"], l["z"]) for l in lamps]
    out[name] = dict(old_main=blocked(old, 0.6, 13.5), old_dip=blocked(old, 5, 9.6), new_main=max(blocked(p, 0.6, 13.5) for p in new), new_dip=max(blocked(p, 5, 9.6) for p in new))
json.dump(out, open(sys.argv[-1], "w"), indent=1)
