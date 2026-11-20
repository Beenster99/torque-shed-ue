"""[build 164] Static copies of the Megaplants WITH their leaves, built from each tree's Nanite assembly.

Why (rounds 157-163): a Megaplant (UE 5.7+ Procedural Vegetation) is a SkeletalMesh whose LOD 0 is ONE
bark section: trunk and branches. Its leaves, needles and buds are separate static meshes placed on its
bones by its Nanite assembly (NaniteAssemblyData: Parts = meshes, Nodes = part + transform + space +
bone influences). Geometry Script's copy_mesh_from_skeletal_mesh copies only LOD 0, so every copy we
planted was bark only: bare conifers, thin or blobby broadleaves, impostors photographing 3-5% of an atlas.
Epic's forum has the same report for the PVE's own non-Nanite static export.

This module rebuilds a copy: the bark (as before) plus every assembly node's part mesh at its place in
the tree's reference pose, with the part's materials mapped to the tree's slots, saved as a Nanite static
mesh (shape preservation: preserve area, so leaf cards don't wear away). It logs every step as
"[Torque] megaplant assembly:" so a missing API name shows up in one round. It is read-only on the
Megaplant itself.

Used by Tools/ue_build_rideuk.py (scanned_trees.json "assembly_copies": true): flatten(skel_path, dest)
returns the new mesh's path or None (then the old bark copy stays).
"""
import math

import unreal


def log(msg):
    unreal.log("[Torque] megaplant assembly: " + msg)


def gs(*names):
    for home in ("GeometryScript_AssetUtils", "GeometryScript_NewAssetUtils", "GeometryScript_CreateNewAssetFunctions",
                 "GeometryScript_MeshBasicEditFunctions", "GeometryScript_MeshMaterialFunctions", "GeometryScript_MeshBoneWeightFunctions",
                 "GeometryScript_MeshQueryFunctions", "GeometryScript_SkeletalMeshFunctions", "GeometryScript_MeshTransformFunctions"):
        c = getattr(unreal, home, None)
        for n in names:
            if c is not None and hasattr(c, n):
                return getattr(c, n)
    return None


def prop(obj, *names):
    """The first of these properties obj has (editor property, then attribute), or None."""
    for n in names:
        try:
            return obj.get_editor_property(n)
        except Exception:
            pass
        try:
            v = getattr(obj, n)
            if not callable(v):
                return v
        except Exception:
            pass
    return None


def fields(obj):
    out = []
    for n in dir(obj):
        if n.startswith("_"):
            continue
        try:
            v = obj.get_editor_property(n)
        except Exception:
            continue
        out.append("%s=%s" % (n, str(v)[:60]))
    return "; ".join(out)


def assembly_of(skel):
    for holder, label in ((skel, "skel"), (prop(skel, "nanite_settings"), "skel.nanite_settings")):
        if holder is None:
            continue
        for n in ("nanite_assembly_data", "assembly_data", "nanite_assembly"):
            d = prop(holder, n)
            if d is not None:
                log("%s: assembly found at %s.%s" % (skel.get_name(), label, n))
                return d
    log("%s: NO assembly data readable from Python (tried skel / nanite_settings . nanite_assembly_data, assembly_data, nanite_assembly). "
        "skel properties with 'nanite' or 'assembl': %s" % (skel.get_name(), ", ".join(n for n in dir(skel) if "assembl" in n.lower() or "nanite" in n.lower())))
    return None


def bone_worlds(dm):
    """Reference-pose world transforms of the copy's bones, by index ({} if Geometry Script can't say)."""
    f = gs("get_all_bones_info")
    if f is None:
        log("no Geometry Script get_all_bones_info")
        return {}
    try:
        res = f(dm)
    except Exception as e:
        log("get_all_bones_info: %s" % e)
        return {}
    infos = None
    for part in (res if isinstance(res, (tuple, list)) else [res]):
        try:
            items = list(part)
        except Exception:
            continue
        if items and not isinstance(items[0], (int, float, str, bool)):
            infos = items
            break
    if not infos:
        log("get_all_bones_info gave nothing usable: %s" % str(res)[:200])
        return {}
    log("bone info fields: %s" % fields(infos[0]))
    world, local, parent = {}, {}, {}
    for k, b in enumerate(infos):
        idx = prop(b, "index", "bone_index")
        idx = k if idx is None else int(idx)
        w = prop(b, "world_transform", "global_transform", "ref_pose_world_transform")
        if isinstance(w, unreal.Transform):
            world[idx] = w
        l = prop(b, "local_transform", "ref_pose_transform", "transform")
        if isinstance(l, unreal.Transform):
            local[idx] = l
        p = prop(b, "parent_index", "parent")
        parent[idx] = int(p) if isinstance(p, int) else -1
    if len(world) == len(infos):
        return world
    # (world from the local chain: child * parent, UE order)
    def walk(i, depth=0):
        if i in world or depth > 512:
            return world.get(i, unreal.Transform())
        l = local.get(i, unreal.Transform())
        p = parent.get(i, -1)
        world[i] = l if p < 0 else unreal.MathLibrary.compose_transforms(l, walk(p, depth + 1))
        return world[i]
    for i in local:
        walk(i)
    return world


def flatten(skel_path, dest, most_nodes=200000):
    skel = unreal.load_asset(skel_path.split(".")[0])
    if skel is None:
        return None
    name = skel.get_name()
    data = assembly_of(skel)
    if data is None:
        return None
    parts = list(prop(data, "parts") or [])
    nodes = list(prop(data, "nodes") or [])
    log("%s: %d parts, %d nodes" % (name, len(parts), len(nodes)))
    if parts:
        log("%s: part fields: %s" % (name, fields(parts[0])))
    if nodes:
        log("%s: node fields: %s" % (name, fields(nodes[0])))
    if not parts or not nodes:
        return None
    copy_skel = gs("copy_mesh_from_skeletal_mesh")
    copy_static = gs("copy_mesh_from_static_mesh")
    append = gs("append_mesh")
    remap = gs("remap_material_ids")
    make = gs("create_new_static_mesh_asset_from_mesh")
    missing = [n for n, f in (("copy_mesh_from_skeletal_mesh", copy_skel), ("copy_mesh_from_static_mesh", copy_static),
                              ("append_mesh", append), ("create_new_static_mesh_asset_from_mesh", make)) if f is None]
    if missing:
        log("%s: missing Geometry Script functions: %s" % (name, ", ".join(missing)))
        return None
    tree = unreal.DynamicMesh()
    res = copy_skel(skel, tree, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    tree = res[0] if isinstance(res, (tuple, list)) and isinstance(res[0], unreal.DynamicMesh) else tree
    bones = bone_worlds(tree)
    log("%s: bark %d triangles, %d bones in the reference pose" % (name, tree.get_triangle_count() if hasattr(tree, "get_triangle_count") else -1, len(bones)))
    # (each part once, its materials moved onto the tree's slots by the part's remap)
    part_meshes = []
    for k, p in enumerate(parts):
        path = prop(p, "mesh_object_path", "mesh", "static_mesh", "object_path")
        mesh = None
        try:
            mesh = unreal.load_asset(str(path).split(".")[0]) if path is not None and not isinstance(path, unreal.StaticMesh) else path
        except Exception:
            mesh = None
        if not isinstance(mesh, unreal.StaticMesh):
            log("%s: part %d: not a static mesh (%s)" % (name, k, str(path)[:120]))
            part_meshes.append(None)
            continue
        dm = unreal.DynamicMesh()
        r = copy_static(mesh, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
        dm = r[0] if isinstance(r, (tuple, list)) and isinstance(r[0], unreal.DynamicMesh) else dm
        mremap = list(prop(p, "material_remap") or [])
        if mremap and remap is not None:
            # (from the highest id down, so a remapped id is never remapped again)
            for src in sorted(range(len(mremap)), reverse=True):
                dst = int(mremap[src])
                if dst >= 0 and dst != src:
                    try:
                        remap(dm, src, dst)
                    except Exception as e:
                        log("%s: part %d remap %d->%d: %s" % (name, k, src, dst, e))
        log("%s: part %d %s, material remap %s" % (name, k, mesh.get_name(), mremap))
        part_meshes.append(dm)
    placed = 0
    for n in nodes[:most_nodes]:
        pi = prop(n, "part_index")
        if pi is None or not (0 <= int(pi) < len(part_meshes)) or part_meshes[int(pi)] is None:
            continue
        xf = prop(n, "transform")
        if xf is None:
            continue
        if not isinstance(xf, unreal.Transform):
            try:
                xf = unreal.Transform(location=xf.translation, rotation=xf.rotation.rotator(), scale=xf.scale3d)
            except Exception:
                continue
        space = str(prop(n, "transform_space") or "")
        if "BONE" in space.upper():
            infl = list(prop(n, "bone_influences") or [])
            best = max(infl, key=lambda b: float(prop(b, "bone_weight", "weight") or 0.0)) if infl else None
            bi = int(prop(best, "bone_index", "index") or 0) if best is not None else 0
            if bi in bones:
                xf = unreal.MathLibrary.compose_transforms(xf, bones[bi])
        try:
            append(tree, part_meshes[int(pi)], xf)
        except TypeError:
            append(tree, part_meshes[int(pi)], xf, False)
        placed += 1
    tris = tree.get_triangle_count() if hasattr(tree, "get_triangle_count") else -1
    log("%s: %d of %d nodes placed: %d triangles in all" % (name, placed, len(nodes), tris))
    if placed == 0:
        return None
    opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    for key, val in (("enable_nanite", True), ("enable_collision", False), ("enable_recompute_normals", False), ("enable_recompute_tangents", False)):
        try:
            opts.set_editor_property(key, val)
        except Exception:
            pass
    if unreal.EditorAssetLibrary.does_asset_exist(dest):
        unreal.EditorAssetLibrary.delete_asset(dest)
    r = make(tree, dest, opts)
    sm = r[0] if isinstance(r, (tuple, list)) else r
    if not isinstance(sm, unreal.StaticMesh):
        log("%s: no mesh made at %s" % (name, dest))
        return None
    sm.set_editor_property("static_materials", [unreal.StaticMaterial(material_interface=m.get_editor_property("material_interface"),
                                                                      material_slot_name=m.get_editor_property("material_slot_name"))
                                                for m in skel.get_editor_property("materials")])
    try:
        ns = sm.get_editor_property("nanite_settings")
        ns.set_editor_property("enabled", True)
        enum = getattr(unreal, "NaniteShapePreservation", None)
        if enum is not None and hasattr(enum, "PRESERVE_AREA"):
            ns.set_editor_property("shape_preservation", enum.PRESERVE_AREA)
        sm.set_editor_property("nanite_settings", ns)
    except Exception as e:
        log("%s: nanite settings: %s" % (name, e))
    unreal.EditorAssetLibrary.save_asset(dest, only_if_is_dirty=False)
    log("%s: made %s (%d triangles, Nanite, preserve area)" % (name, dest, tris))
    return sm.get_path_name()


if __name__ == "__main__":
    # (a quick try on one tree, unattended and without rendering:
    #  UnrealEditor-Cmd.exe TorqueShedProto.uproject -run=pythonscript -script="G:/Dev/torque-shed-ue/Tools/ue_megaplant_assembly.py" -unattended -nullrhi
    #  makes /Game/ScannedTrees/Test/SM_<spruce>_asm and logs every step)
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    want = [a for a in reg.get_assets_by_path("/Game", recursive=True)
            if str(a.asset_class_path.asset_name) == "SkeletalMesh" and "spruce" in str(a.asset_name).lower()]
    if not want:
        log("test: no spruce SkeletalMesh under /Game")
    else:
        a = sorted(want, key=lambda x: str(x.asset_name))[0]
        flatten("%s.%s" % (a.package_name, a.asset_name), "/Game/ScannedTrees/Test/SM_%s_asm" % a.asset_name)
    log("test done")
