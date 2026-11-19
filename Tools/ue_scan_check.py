"""[build 159] Do the scanned trees' static copies have their leaves?

Rounds 157-158: the far trees' impostors came out as bare trunks, the game's photographs of each scan
covering only 3-8% of the atlas (spruce, pine) whatever the camera's distance, and beenster found the
near Megaplants "young, no bulk". Megaplants need Nanite Foliage: a whole tree is a SkeletalMesh whose
leaves, needles and buds are most likely separate static meshes (the "leaf", "bud" and "needle" parts
scanned_trees.json skips) placed on its bones as a Nanite assembly. Tools/ue_build_rideuk.py's
static_of_skeletal() copies the skeletal mesh with Geometry Script's copy_mesh_from_skeletal_mesh,
which most likely copies only the base mesh: trunk and branches.

This script only READS (no captures, no rendering, nothing saved), so it is safe unattended:

    UnrealEditor-Cmd.exe TorqueShedProto.uproject -run=pythonscript -script="G:/Dev/torque-shed-ue/Tools/ue_scan_check.py" -unattended -nullrhi

For each whole-tree SkeletalMesh with a static copy in /Game/ScannedTrees it logs "[Torque] scan check:" lines:
  - the skeletal mesh: triangles by material slot (Geometry Script copy), its material slots, and
    every property of it and its Nanite settings whose name mentions assembly / part / foliage / instance;
  - its static copy: triangles and sections;
  - the static meshes in the skeletal mesh's own folder (the likely leaf parts) with their triangles.
"""
import unreal

SCAN_DEST = "/Game/ScannedTrees"
WORDS = ("assembl", "part", "foliage", "instance", "attach", "socket", "leaf", "leaves", "nanite")


def log(msg):
    unreal.log("[Torque] scan check: " + msg)


def gs(*names):
    for home in ("GeometryScript_AssetUtils", "GeometryScript_NewAssetUtils", "GeometryScript_MeshQueryFunctions",
                 "GeometryScript_MeshMaterialFunctions", "GeometryScript_StaticMeshFunctions"):
        c = getattr(unreal, home, None)
        for n in names:
            if c is not None and hasattr(c, n):
                return getattr(c, n)
    return None


def props_of(obj, label, depth=0):
    """Properties whose names mention WORDS, and one level into structs/objects among them."""
    if obj is None or depth > 2:
        return
    for name in sorted(dir(obj)):
        if name.startswith("_") or not any(w in name.lower() for w in WORDS):
            continue
        try:
            val = obj.get_editor_property(name)
        except Exception:
            try:
                val = getattr(obj, name)
                if callable(val):
                    log("%s.%s() (function)" % (label, name))
                    continue
            except Exception:
                continue
        text = str(val)
        log("%s.%s = %s" % (label, name, text[:400]))
        if isinstance(val, (list, tuple)):
            log("%s.%s: %d items" % (label, name, len(val)))
            for k, item in enumerate(val[:6]):
                log("%s.%s[%d] = %s" % (label, name, k, str(item)[:300]))
                if not isinstance(item, (str, int, float, bool)):
                    props_of(item, "%s.%s[%d]" % (label, name, k), depth + 1)
        elif val is not None and not isinstance(val, (str, int, float, bool)):
            props_of(val, "%s.%s" % (label, name), depth + 1)


def tris_by_material(dm):
    count = gs("get_num_triangle_ids")
    mat_of = gs("get_triangle_material_id")
    ids = gs("get_all_triangle_material_ids")
    out = {}
    total = count(dm) if count else -1
    try:
        if ids is not None:
            res = ids(dm)
            lst = res[0] if isinstance(res, (tuple, list)) and len(res) and isinstance(res[0], (list, tuple, unreal.Array)) else res
            for m in list(lst):
                out[m] = out.get(m, 0) + 1
        elif mat_of is not None and total > 0:
            for t in range(total):
                r = mat_of(dm, t)
                m = r[0] if isinstance(r, (tuple, list)) else r
                out[m] = out.get(m, 0) + 1
    except Exception as e:
        log("  (triangles by material: %s)" % e)
    return total, out


def check(static_path):
    sm = unreal.load_asset(static_path)
    if not isinstance(sm, unreal.StaticMesh):
        return
    name = sm.get_name()
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    try:
        log("%s: static copy: %d sections, %d vertices, %d material slots" % (
            name, sm.get_num_sections(0), sub.get_number_verts(sm, 0), len(sm.get_editor_property("static_materials"))))
    except Exception as e:
        log("%s: static copy: %s" % (name, e))
    # (the skeletal mesh it was copied from: SM_<skeletal name>)
    skel_name = name[3:] if name.startswith("SM_") else name
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    found = [a for a in reg.get_assets_by_path("/Game", recursive=True)
             if str(a.asset_name) == skel_name and str(a.asset_class_path.asset_name) == "SkeletalMesh"]
    if not found:
        log("%s: no SkeletalMesh named %s" % (name, skel_name))
        return
    skel_path = "%s.%s" % (found[0].package_name, found[0].asset_name)
    skel = unreal.load_asset(skel_path)
    log("%s: from %s" % (name, skel_path))
    for k, m in enumerate(skel.get_editor_property("materials")):
        mi = m.get_editor_property("material_interface")
        log("%s:   slot %d %s -> %s" % (name, k, m.get_editor_property("material_slot_name"), mi.get_path_name() if mi else None))
    copy = gs("copy_mesh_from_skeletal_mesh")
    if copy is not None:
        try:
            dm = unreal.DynamicMesh()
            res = copy(skel, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
            dm = res[0] if isinstance(res, (tuple, list)) else dm
            total, by = tris_by_material(dm)
            log("%s: skeletal copy: %d triangles; by material slot: %s" % (name, total, ", ".join("%s: %d" % kv for kv in sorted(by.items())) or "?"))
        except Exception as e:
            log("%s: skeletal copy failed: %s" % (name, e))
    props_of(skel, "skel")
    for nm in ("nanite_settings", "nanite_assembly", "nanite_assembly_data"):
        try:
            props_of(skel.get_editor_property(nm), "skel.%s" % nm)
            log("skel.%s exists: %s" % (nm, str(skel.get_editor_property(nm))[:300]))
        except Exception:
            pass
    # (the static meshes beside it: the leaf, needle and bud parts)
    folder = str(found[0].package_path)
    for a in reg.get_assets_by_path(folder, recursive=True):
        if str(a.asset_class_path.asset_name) != "StaticMesh":
            continue
        part = unreal.load_asset("%s.%s" % (a.package_name, a.asset_name))
        try:
            log("%s:   part beside it: %s (%d vertices, %d slots: %s)" % (name, a.asset_name, sub.get_number_verts(part, 0), len(part.get_editor_property("static_materials")),
                ", ".join(str(x.get_editor_property("material_slot_name")) for x in part.get_editor_property("static_materials"))))
        except Exception as e:
            log("%s:   part beside it: %s (%s)" % (name, a.asset_name, e))
    # (anything else in that folder that isn't a mesh, texture or material: assembly / vegetation assets)
    for a in reg.get_assets_by_path(folder, recursive=True):
        cls = str(a.asset_class_path.asset_name)
        if cls not in ("StaticMesh", "SkeletalMesh", "Texture2D", "Material", "MaterialInstanceConstant", "MaterialFunction", "Skeleton", "PhysicsAsset"):
            log("%s:   other asset beside it: %s (%s)" % (name, a.asset_name, cls))


def main():
    paths = []
    if unreal.EditorAssetLibrary.does_directory_exist(SCAN_DEST):
        for a in unreal.EditorAssetLibrary.list_assets(SCAN_DEST, recursive=True, include_folder=False):
            p = a.split(".")[0]
            n = p.rsplit("/", 1)[-1]
            if n.startswith("SM_"):
                paths.append(p)
    paths.sort()
    log("%d static copies in %s" % (len(paths), SCAN_DEST))
    # (one spruce, one pine, one broadleaf is enough: the first of each, then the rest briefly)
    picked, seen = [], set()
    for p in paths:
        n = p.lower()
        kind = "spruce" if "spruce" in n else "pine" if "pine" in n else "broadleaf"
        if kind not in seen:
            seen.add(kind)
            picked.append(p)
    for p in picked:
        try:
            check(p)
        except Exception as e:
            import traceback
            log("%s: failed: %s\n%s" % (p, e, traceback.format_exc()))
    log("done")


main()
