"""Try beenster's own MetaHuman biker jackets ("Crosswind": plain, Worn, Distressed) on Avatar A, as a
preview only (run in the Unreal editor, on the PC). beenster made them in Blender, rigged to the
MetaHuman body skeleton; they live on the PC, never in git:

    F:/OneDrive/Documents/ChatGPT/Biker Clothes for metahumans/
        SK_Crosswind_Avatar_A.fbx, SK_Crosswind_Worn_Avatar_A.fbx, SK_Crosswind_Distressed_Avatar_A.fbx
        Leather/Oiled_Hide_*.png, Distressed Leather/Dry_Hide_*.png

    UnrealEditor-Cmd.exe TorqueShedProto.uproject -ExecutePythonScript="G:/Dev/torque-shed-ue/Tools/ue_try_jacket.py" -unattended
    (or with another folder: ".../ue_try_jacket.py F:/some/folder")

Imports each FBX as a skeletal mesh on Avatar A's body skeleton into /Game/AvatarTry/Crosswind (with the
textures; the leather on each leather slot through M_GarmentPush, no push), and logs every slot, bone
count and box as "[Torque] try jacket: ...". The game then wears one over the avatar's shirt with
-TorqueTryJacket=<name> (see TorqueAvatar.cpp), e.g.
    -TorqueAvatarDemo -TorqueTryJacket=SK_Crosswind_Worn_Avatar_A   then look:avatars:body / back
/Game/AvatarTry is a preview: delete it once beenster has chosen (it is cooked while it's there).
"""
import os
import sys

import unreal

SRC = "F:/OneDrive/Documents/ChatGPT/Biker Clothes for metahumans"
DEST = "/Game/AvatarTry/Crosswind"
BODY = "/Game/MetaHumans/Avatar_A/Avatar_A/Body/SKM_Avatar_A_BodyMesh"
PUSH = "/Game/Rider/Materials/M_GarmentPush"
JACKETS = {   # fbx: the leather it wears (folder, file prefix); None: the plain one, its own (or a smooth black)
    "SK_Crosswind_Avatar_A.fbx": None,
    "SK_Crosswind_Worn_Avatar_A.fbx": ("Leather", "Oiled_Hide"),
    "SK_Crosswind_Distressed_Avatar_A.fbx": ("Distressed Leather", "Dry_Hide"),
}
METAL_WORDS = ("metal", "zip", "buckle", "hardware", "stud", "chrome", "rivet", "snap", "steel", "silver")


def log(msg):
    unreal.log("[Torque] try jacket: " + msg)


def import_file(path, dest, options=None):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", path)
    t.set_editor_property("destination_path", dest)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    if options is not None:
        t.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    return [str(p) for p in t.get_editor_property("imported_object_paths")]


def texture(folder, prefix, kind, dest):
    path = os.path.join(SRC, folder, "%s_%s.png" % (prefix, kind))
    if not os.path.isfile(path):
        log("no %s" % path)
        return None
    got = import_file(path, dest)
    tex = unreal.load_asset(got[0].split(".")[0]) if got else None
    if tex is not None and kind.startswith("Normal"):
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
    if tex is not None and kind == "Roughness":
        tex.set_editor_property("srgb", False)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


def leather(name, folder, prefix, dest):
    """An instance of M_GarmentPush with the leather's colour and normal maps (DirectX normals, as UE wants)."""
    parent = unreal.load_asset(PUSH)
    if parent is None:
        log("no %s (run ue_build_level.py first); the jacket keeps its imported materials" % PUSH)
        return None
    mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset("MI_" + name, dest, unreal.MaterialInstanceConstant,
                                                                 unreal.MaterialInstanceConstantFactoryNew())
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(mi, parent)
    col, nrm = texture(folder, prefix, "BaseColor", dest), texture(folder, prefix, "Normal_DirectX", dest)
    if col is not None:
        mel.set_material_instance_texture_parameter_value(mi, "BaseColorMap", col)
    if nrm is not None:
        mel.set_material_instance_texture_parameter_value(mi, "NormalMap", nrm)
    mel.set_material_instance_scalar_parameter_value(mi, "Push", 0.0)
    mel.set_material_instance_scalar_parameter_value(mi, "HemPush", 0.0)
    mel.set_material_instance_scalar_parameter_value(mi, "Roughness", 0.5)
    mel.set_material_instance_scalar_parameter_value(mi, "Specular", 0.45)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    log("%s: leather MI_%s (colour %s, normal %s)" % (name, name, col is not None, nrm is not None))
    return mi


def main():
    global SRC
    args = [a for a in sys.argv[1:] if a.strip()]
    if args:
        SRC = " ".join(args)
    if not os.path.isdir(SRC):
        log("NO folder %s; nothing imported" % SRC)
        return
    body = unreal.load_asset(BODY)
    skel = body.get_editor_property("skeleton") if body else None
    log("Avatar A body %s, skeleton %s" % (body.get_path_name() if body else "MISSING", skel.get_path_name() if skel else "MISSING"))
    for fbx, leather_of in JACKETS.items():
        path = os.path.join(SRC, fbx)
        if not os.path.isfile(path):
            log("no %s" % path)
            continue
        name = os.path.splitext(fbx)[0]
        dest = DEST + "/" + name
        ui = unreal.FbxImportUI()
        ui.set_editor_property("import_mesh", True)
        ui.set_editor_property("import_as_skeletal", True)
        ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
        ui.set_editor_property("import_materials", True)
        ui.set_editor_property("import_textures", True)
        ui.set_editor_property("import_animations", False)
        ui.set_editor_property("create_physics_asset", False)
        if skel is not None:
            ui.set_editor_property("skeleton", skel)
        sk = ui.get_editor_property("skeletal_mesh_import_data")
        sk.set_editor_property("import_morph_targets", False)
        sk.set_editor_property("update_skeleton_reference_pose", False)
        sk.set_editor_property("use_t0_as_ref_pose", False)
        got = import_file(path, dest, ui)
        mesh = None
        for p in got:
            a = unreal.load_asset(p.split(".")[0])
            if isinstance(a, unreal.SkeletalMesh):
                mesh = a
        if mesh is None:
            log("%s: NO skeletal mesh imported (got %s); is it on Avatar A's skeleton?" % (fbx, ", ".join(got) or "nothing"))
            continue
        b = mesh.get_bounds()
        lo, hi = b.origin - b.box_extent, b.origin + b.box_extent
        msk = mesh.get_editor_property("skeleton")
        log("%s -> %s: skeleton %s, box (%.0f %.0f %.0f)..(%.0f %.0f %.0f) cm" % (fbx, mesh.get_path_name(), msk.get_path_name() if msk else "none",
            lo.x, lo.y, lo.z, hi.x, hi.y, hi.z))
        mi = leather(name, leather_of[0], leather_of[1], dest) if leather_of else None
        mats = mesh.get_editor_property("materials")
        for i, sm in enumerate(mats):
            slot = str(sm.material_slot_name)
            was = sm.material_interface
            words = (slot + " " + (was.get_name() if was else "")).lower()
            metal = any(w in words for w in METAL_WORDS)
            if mi is not None and not metal:
                sm.material_interface = mi
                mats[i] = sm
            log("%s slot %d %s: %s%s" % (name, i, slot, was.get_name() if was else "none",
                " -> " + mi.get_name() if (mi is not None and not metal) else (" (metal, kept)" if metal else "")))
        mesh.set_editor_property("materials", mats)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    log("done: wear one in game with -TorqueTryJacket=<mesh name> (Avatar A; -TorqueTryJacketOn=all for everyone)")


main()
