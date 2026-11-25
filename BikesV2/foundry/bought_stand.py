"""A side stand for a bought bike (beenster 25 Nov on build 172: the Multistrada asked for a side stand and a shop stand
vanished when fitted, as a V2 bike only draws what its own drawing holds: "make sure the other new bikes ... have it
come with one").

None of the bought models draws a stand, so each takes the stand off our own bike of its style (Data/BikesV2, in git):
its pieces copied into the bought drawing, moved so its pivot sits at the same height on the bought bike's left side,
under the rider, and points_mm.stand to match (the game swings it down about that pivot as on every V2 bike).
"""
import os

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.dirname(os.path.dirname(HERE))
DONOR = {"adventure": "kazeda_adventure", "sport": "kazeda_sport", "cruiser": "kazeda_cruiser", "naked": "kazeda_naked"}


def add_stand(host, style, pts, looks, new_material):
    """host: the joined bought bike, already in the game's frame (metres, x forward from the rear axle, y left, z up).
    Copies the donor's stand into it; sets pts["stand"]; adds its surfaces to looks (new_material(look) -> material)."""
    import json
    donor = DONOR[style]
    info = json.load(open(os.path.join(GAME, "Data", "BikesV2", donor + ".json")))
    stand = dict(info["points_mm"]["stand"])
    dp = Vector(stand["pivot"]) / 1000.0
    # where it goes: the donor's pivot height, 43% of the way along the wheelbase, just outside the bike's own left side
    wb = pts["stats"]["wheelbase"] / 1000.0
    x, z = 0.43 * wb, dp.z
    mw = host.matrix_world
    near = [(mw @ v.co).y for v in host.data.vertices if abs((mw @ v.co).x - x) < 0.05 and abs((mw @ v.co).z - z) < 0.04]
    y = max([dp.y] + [yy + 0.02 for yy in near if yy > 0])
    hp = Vector((x, y, z))
    before = set(bpy.data.objects)
    mats_before = set(bpy.data.materials)
    bpy.ops.import_scene.gltf(filepath=os.path.join(GAME, "Data", "BikesV2", donor + ".glb"))
    for m in [m for m in bpy.data.materials if m not in mats_before]:
        m.name = "donor_" + m.name          # (its s000.. names are the bought drawing's own)
    got = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in got if o.type == "MESH"]
    keep = []
    for o in meshes:
        bpy.ops.object.select_all(action="DESELECT")
        o.select_set(True)
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.separate(type="MATERIAL")
        bpy.ops.object.mode_set(mode="OBJECT")
        for p in list(bpy.context.selected_objects):
            m = p.data.materials[p.data.polygons[0].material_index] if (p.data.polygons and p.data.materials) else None
            look = info["surfaces"].get(m.name[len("donor_"):].split(".")[0]) if m else None
            if look and look["slot"] == "side_stand":
                keep.append((p, look))
    for o in [o for o in bpy.data.objects if o not in before and o not in [k for k, _ in keep]]:
        bpy.data.objects.remove(o, do_unlink=True)
    if not keep:
        raise SystemExit(f"no side stand in {donor}.glb")
    for p, look in keep:
        mw2 = p.matrix_world.copy()
        p.parent = None
        p.matrix_world = mw2
        p.data.materials.clear()
        p.data.materials.append(new_material(dict(look, node="")))
        for f in p.data.polygons:
            f.material_index = 0
    bpy.ops.object.select_all(action="DESELECT")
    for p, _ in keep:
        p.select_set(True)
    bpy.context.view_layer.objects.active = keep[0][0]
    bpy.ops.object.make_single_user(object=True, obdata=True, material=False)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for p, _ in keep:
        for v in p.data.vertices:
            v.co += hp - dp
    host.select_set(True)
    bpy.context.view_layer.objects.active = host
    bpy.ops.object.join()
    stand["pivot"] = [round(hp.x * 1000, 1), round(hp.y * 1000, 1), round(hp.z * 1000, 1)]
    pts["stand"] = stand
    return bpy.context.view_layer.objects.active
