"""Helmet plan stage 1 (run in the Unreal editor, on the PC): export what the cloud needs to design
helmet colours and liveries for beenster's Fab racing helmet, without the Fab model going in the repo.

    UnrealEditor-Cmd.exe TorqueShedProto.uproject -ExecutePythonScript="G:/Dev/torque-shed-ue/Tools/ue_helmet_export.py" -unattended

Writes Saved/HelmetExport/ (copy the whole folder to the project files, helmet/export/; NEVER commit it):
    helmet.json      the mesh worn (the same pick as Tools/ue_build_level.py), its box, LODs, UV channels,
                     and every material slot: its material, parent chain, blend mode, two-sided, and its
                     scalar / vector / texture parameter values
    helmet.obj       LOD0 with UVs and a group per material slot (for the layout pictures and livery masks)
    helmet.fbx       the same, as a fallback if the OBJ exporter is missing
    tex/*.png        every texture the helmet's materials use (to see its own paint, trim and visor maps)
    avatars.json     each avatar's height, face mesh box and head bone place (helmet size on each head)
    rider_fit.json   the helmet_fit the rider pawn uses (from Data/Bike/SpeedTwin/ue_assets.json, if there)
Everything is logged as "[Torque] helmet export: ...".
"""
import json
import os

import unreal

WORDS = ("helmet",)
SKIP_WORDS = ("Hair_", "Groom", "Rider/helmet", "BACKUP")
SKIP_PATHS = ("/Game/MetaHumans/Avatar_", "/Game/MetaHumanSources", "/Game/MetaHumanBackup", "/Game/Rider/")


def log(msg):
    unreal.log("[Torque] helmet export: " + msg)


def project_dir():
    return os.path.abspath(unreal.Paths.project_dir())


OUT = os.path.join(project_dir(), "Saved", "HelmetExport")


def cls_of(a):
    return str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)


def find_helmet():
    """The Fab helmet mesh: static before skeletal, LOD0 (or no LOD in its name) first, as ue_build_level."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    found = []
    for a in reg.get_assets_by_path("/Game", recursive=True):
        cls, pkg, name = cls_of(a), str(a.package_name), str(a.asset_name)
        if cls not in ("StaticMesh", "SkeletalMesh") or not any(w in name.lower() for w in WORDS):
            continue
        if any(k in pkg for k in SKIP_WORDS) or any(pkg.startswith(k) for k in SKIP_PATHS):
            continue
        found.append(("%s.%s" % (pkg, name), cls))
    log("candidates: %s" % (", ".join("%s (%s)" % f for f in found) or "NONE"))
    if not found:
        return None

    def key(f):
        n = f[0].rsplit("/", 1)[-1]
        return (f[1] != "StaticMesh", "LOD" in n and "LOD0" not in n, len(f[0]), f[0])
    return sorted(found, key=key)[0][0]


def vec(v):
    return [round(v.x, 3), round(v.y, 3), round(v.z, 3)]


def material_info(m, textures):
    """A material's path, parent chain, blend mode, two-sided and parameter values (textures noted)."""
    if m is None:
        return {"material": None}
    info = {"material": m.get_path_name(), "class": type(m).__name__, "parents": []}
    p = m
    while isinstance(p, unreal.MaterialInstance):
        p = p.get_editor_property("parent")
        if p is None:
            break
        info["parents"].append(p.get_path_name())
    base = m.get_base_material() if hasattr(m, "get_base_material") else None
    if base is not None:
        try:
            info["blend_mode"] = str(base.get_editor_property("blend_mode"))
            info["two_sided"] = bool(base.get_editor_property("two_sided"))
            info["shading_model"] = str(base.get_editor_property("shading_model"))
        except Exception as e:
            info["base_error"] = str(e)
    if isinstance(m, unreal.MaterialInstance):
        try:
            info["scalars"] = {str(v.parameter_info.name): v.parameter_value for v in m.get_editor_property("scalar_parameter_values")}
            info["vectors"] = {str(v.parameter_info.name): [v.parameter_value.r, v.parameter_value.g, v.parameter_value.b, v.parameter_value.a]
                               for v in m.get_editor_property("vector_parameter_values")}
            info["textures"] = {}
            for v in m.get_editor_property("texture_parameter_values"):
                t = v.parameter_value
                info["textures"][str(v.parameter_info.name)] = t.get_path_name() if t else None
                if t is not None:
                    textures.add(t.get_path_name())
        except Exception as e:
            info["param_error"] = str(e)
    # Every texture the material draws with, overridden or not (the parent's defaults too).
    try:
        for t in unreal.MaterialEditingLibrary.get_used_textures(m):
            textures.add(t.get_path_name())
    except Exception as e:
        info["used_textures_error"] = str(e)
    return info


def export(obj, filename):
    t = unreal.AssetExportTask()
    t.set_editor_property("object", obj)
    t.set_editor_property("filename", filename)
    t.set_editor_property("automated", True)
    t.set_editor_property("prompt", False)
    t.set_editor_property("replace_identical", True)
    ok = unreal.Exporter.run_asset_export_task(t)
    size = os.path.getsize(filename) // 1024 if os.path.isfile(filename) else -1
    log("export %s -> %s: %s (%d KB)" % (obj.get_name(), os.path.basename(filename), "ok" if ok else "FAILED", size))
    return ok and size >= 0


def head_place(mesh):
    """Whether a skeletal mesh has a head bone, and its skeleton (UE 5.8's Python has no ref-pose bone
    query; the face box is what sizes the helmet on each head)."""
    out = {}
    try:
        skel = mesh.get_editor_property("skeleton")
        out["skeleton"] = skel.get_path_name() if skel else None
    except Exception:
        pass
    return out


def avatars():
    path = os.path.join(project_dir(), "Data", "Avatars", "avatars.json")
    out = []
    if not os.path.isfile(path):
        log("no avatars.json")
        return out
    for av in json.load(open(path)).get("avatars", []):
        face = next((p.get("mesh") for p in av.get("parts", []) if p.get("name") == "Face" or "FaceMesh" in (p.get("mesh") or "")), None)
        row = {"name": av.get("name"), "height_cm": av.get("height_cm"), "face": face}
        m = unreal.load_asset(face.split(".")[0]) if face else None
        if m is not None:
            try:
                b = m.get_bounds()
                row["face_box_min"] = vec(b.origin - b.box_extent)
                row["face_box_max"] = vec(b.origin + b.box_extent)
            except Exception as e:
                row["face_box_error"] = str(e)
            row.update(head_place(m))
        out.append(row)
        log("avatar %s: %s" % (row["name"], json.dumps({k: v for k, v in row.items() if k != "name"})))
    return out


def main():
    os.makedirs(os.path.join(OUT, "tex"), exist_ok=True)
    path = find_helmet()
    if not path:
        log("NO helmet mesh found in the project; nothing exported")
        return
    mesh = unreal.load_asset(path.split(".")[0])
    log("helmet %s (%s)" % (path, type(mesh).__name__))
    info = {"mesh": path, "class": type(mesh).__name__, "slots": []}
    textures = set()
    if isinstance(mesh, unreal.StaticMesh):
        b = mesh.get_bounding_box()
        info["box_min"], info["box_max"] = vec(b.min), vec(b.max)
        info["lods"] = mesh.get_num_lods()
        try:
            info["uv_channels"] = unreal.StaticMeshEditorSubsystem().get_num_uv_channels(mesh, 0) if hasattr(unreal, "StaticMeshEditorSubsystem") else None
        except Exception:
            info["uv_channels"] = None
        try:
            info["triangles_lod0"] = mesh.get_num_triangles(0)
        except Exception:
            pass
        for i, sm in enumerate(mesh.get_editor_property("static_materials")):
            row = {"slot": i, "name": str(sm.material_slot_name)}
            row.update(material_info(sm.material_interface, textures))
            info["slots"].append(row)
    else:
        bs = mesh.get_bounds()
        info["box_min"], info["box_max"] = vec(bs.origin - bs.box_extent), vec(bs.origin + bs.box_extent)
        for i, sm in enumerate(mesh.get_editor_property("materials")):
            row = {"slot": i, "name": str(sm.material_slot_name)}
            row.update(material_info(sm.material_interface, textures))
            info["slots"].append(row)
    for s in info["slots"]:
        log("slot %d %s: %s (blend %s, two-sided %s, textures %s)" % (s["slot"], s["name"], s.get("material"), s.get("blend_mode"),
            s.get("two_sided"), ", ".join("%s=%s" % (k, (v or "none").rsplit(".", 1)[-1]) for k, v in (s.get("textures") or {}).items())))
    info["obj"] = export(mesh, os.path.join(OUT, "helmet.obj"))
    info["fbx"] = export(mesh, os.path.join(OUT, "helmet.fbx"))
    info["texture_files"] = {}
    for tp in sorted(textures):
        t = unreal.load_asset(tp.split(".")[0])
        if t is None:
            continue
        name = t.get_name() + ".png"
        if export(t, os.path.join(OUT, "tex", name)):
            info["texture_files"][tp] = "tex/" + name
        try:
            info.setdefault("texture_sizes", {})[tp] = [t.blueprint_get_size_x(), t.blueprint_get_size_y()]
        except Exception:
            pass
    json.dump(info, open(os.path.join(OUT, "helmet.json"), "w"), indent=1)
    json.dump({"avatars": avatars()}, open(os.path.join(OUT, "avatars.json"), "w"), indent=1)
    assets = os.path.join(project_dir(), "Data", "Bike", "SpeedTwin", "ue_assets.json")
    fit = {}
    if os.path.isfile(assets):
        rig = json.load(open(assets)).get("rider_rig", {})
        fit = {"helmet": rig.get("helmet"), "helmet_fit": rig.get("helmet_fit")}
    json.dump(fit, open(os.path.join(OUT, "rider_fit.json"), "w"), indent=1)
    log("done: %d slots, %d textures, written to %s (copy to the project files helmet/export/, never the repo)"
        % (len(info["slots"]), len(info["texture_files"]), OUT))


main()
