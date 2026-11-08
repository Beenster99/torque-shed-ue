"""MetaHuman avatars for the shed's co-op players (run in the Unreal editor, on the PC).

MetaHuman Creator is part of the editor since UE 5.6 (free; the cloud auto-rig and texture download
need an Epic account sign-in). The game shows another player as a MetaHuman when
Data/Avatars/avatars.json lists one (Source/TorqueShedProto/Shed/TorqueAvatar.*), one per player
colour, posed in code; without the file it keeps the shape body.

    py Tools/ue_metahumans.py              register: list the MetaHumans under /Game/MetaHumans in
                                           Data/Avatars/avatars.json (run after making or changing any)
    py Tools/ue_metahumans.py discover     write Saved/metahuman_api.txt: the editor's MetaHuman Python
                                           API in this engine version, to script the making next
    py Tools/ue_metahumans.py make         make the avatars below by script and build them without the
                                           cloud (logs can_build_meta_human's reason if a rig is needed)
    py Tools/ue_metahumans.py make rig     the same, auto-rigging each in Epic's cloud first (sign-in)

Or without opening the editor:
    UnrealEditor-Cmd.exe TorqueShedProto.uproject -ExecutePythonScript="G:/Dev/torque-shed-ue/Tools/ue_metahumans.py discover" -unattended
(forward slashes in the path: UE escape-processes backslashes, so "\\t" becomes a tab)

Needs the MetaHuman plugins on (Edit > Plugins > "MetaHuman": MetaHuman Creator / MetaHuman Character).
"""
import json
import os
import sys

import unreal

ROOT = "/Game/MetaHumans"
AVATARS = [   # the people made by `make`: a name each and the preset to start from (the first that exists)
    {"name": "Avatar_A"}, {"name": "Avatar_B"}, {"name": "Avatar_C"}, {"name": "Avatar_D"},
]


def log(msg):
    unreal.log("[Torque] " + msg)


def project_dir():
    return os.path.abspath(unreal.Paths.project_dir())


def discover():
    """Every class, enum and function in the editor's Python with MetaHuman in its name, with their
    methods and docs, to Saved/metahuman_api.txt."""
    out = []
    for name in sorted(dir(unreal)):
        if "metahuman" not in name.lower():
            continue
        obj = getattr(unreal, name)
        out.append("=" * 100)
        out.append(name + "  " + str(type(obj)))
        doc = (getattr(obj, "__doc__", "") or "").strip()
        if doc:
            out.append(doc[:3000])
        for m in sorted(dir(obj)):
            if m.startswith("_"):
                continue
            try:
                a = getattr(obj, m)
            except Exception:
                continue
            d = (getattr(a, "__doc__", "") or "").strip().replace("\n", "\n        ")
            out.append("    %s: %s" % (m, d[:1500]))
    path = os.path.join(project_dir(), "Saved", "metahuman_api.txt")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    log("MetaHuman API: %d names, written to %s" % (sum(1 for l in out if l.startswith("=")), path))
    return path


def _props(obj):
    """An editor struct or object's Python properties, for the log (names this version uses)."""
    return ", ".join(m for m in dir(obj) if not m.startswith("_") and not callable(getattr(obj, m, None)))


def _set_first(obj, value, *names):
    for n in names:
        try:
            obj.set_editor_property(n, value)
            return n
        except Exception:
            continue
    return None


def _vary_body(sub, ch, i):
    """A different height and build for each avatar, through the parametric body constraints."""
    heights = (170.0, 176.0, 182.0, 188.0)
    try:
        cons = sub.get_body_constraints(ch, False)
    except Exception as e:
        unreal.log_warning("[Torque] get_body_constraints failed: %s" % e)
        return
    names = []
    for c in cons:
        try:
            n = str(c.get_editor_property("name"))
        except Exception:
            n = str(getattr(c, "name", ""))
        names.append(n)
        if "height" in n.lower() and "head" not in n.lower():
            for prop in ("target_measurement", "target", "value"):
                try:
                    c.set_editor_property(prop, heights[i % len(heights)])
                    break
                except Exception:
                    continue
            for prop in ("is_active", "active", "b_is_active"):
                try:
                    c.set_editor_property(prop, True)
                    break
                except Exception:
                    continue
    log("body constraints: " + ", ".join(names))
    if cons:
        log("body constraint fields: " + _props(cons[0]))
    try:
        sub.set_body_constraints(ch, cons)
        log("%s: height %.0f cm" % (ch.get_name(), heights[i % len(heights)]))
    except Exception as e:
        unreal.log_warning("[Torque] set_body_constraints failed: %s" % e)


def make(rig=False):
    """Make a MetaHuman Character asset per avatar with the UE 5.8 editor API and build it.

    Without `rig`, nothing goes to Epic's cloud: the build uses whatever rig the character has, and
    can_build_meta_human logs why when it needs one. With `rig` (`make rig`), each character is first
    auto-rigged in the cloud (Joints Only, blocking), which needs the Epic account sign-in."""
    discover()
    cls = getattr(unreal, "MetaHumanCharacter", None)
    sub_cls = getattr(unreal, "MetaHumanCharacterEditorSubsystem", None)
    if cls is None or sub_cls is None:
        unreal.log_warning("[Torque] this editor has no MetaHumanCharacter / MetaHumanCharacterEditorSubsystem in Python: "
                           "turn the MetaHuman plugins on, or make the avatars by hand (Docs/avatars.md)")
        return
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = getattr(unreal, "MetaHumanCharacterFactoryNew", None)
    sub = unreal.get_editor_subsystem(sub_cls)
    for i, av in enumerate(AVATARS):
        name = av["name"]
        folder = ROOT + "/" + name
        path = folder + "/" + name
        ch = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
        if ch is None:
            try:
                ch = tools.create_asset(name, folder, cls, factory() if factory else None)
            except Exception as e:
                unreal.log_warning("[Torque] couldn't create %s: %s" % (path, e))
                continue
        if ch is None:
            unreal.log_warning("[Torque] create_asset gave nothing for %s" % path)
            continue
        log("character: %s" % ch.get_path_name())
        if not sub.try_add_object_to_edit(ch):
            unreal.log_warning("[Torque] %s: try_add_object_to_edit refused it" % name)
            continue
        _vary_body(sub, ch, i)
        if rig:
            try:
                params = unreal.MetaHumanCharacterAutoRiggingRequestParams() if hasattr(unreal, "MetaHumanCharacterAutoRiggingRequestParams") else None
                if params is not None:
                    log("auto-rig params: " + _props(params))
                    rt = getattr(unreal, "MetaHumanRigType", None)
                    if rt is not None and hasattr(rt, "JOINTS_ONLY"):
                        _set_first(params, rt.JOINTS_ONLY, "rig_type")
                    _set_first(params, True, "blocking", "b_blocking")
                    sub.request_auto_rigging(ch, params)
                else:
                    sub.request_auto_rigging(ch)
                log("%s: auto-rig requested (cloud)" % name)
            except Exception as e:
                unreal.log_warning("[Torque] %s: request_auto_rigging failed (signed in to Epic?): %s" % (name, e))
        ok = False
        try:
            ok = sub.can_build_meta_human(ch, True)   # logs its reason when it says no
        except Exception as e:
            unreal.log_warning("[Torque] %s: can_build_meta_human failed: %s" % (name, e))
        log("%s: can_build_meta_human = %s" % (name, ok))
        built = False
        if ok and hasattr(unreal, "MetaHumanCharacterEditorBuildParameters"):
            bp = unreal.MetaHumanCharacterEditorBuildParameters()
            log("build parameters: " + _props(bp))
            used = _set_first(bp, folder, "absolute_build_path", "build_path", "output_folder", "output_path")
            _set_first(bp, name, "name_override", "name", "asset_name")
            log("%s: build path set via %s" % (name, used))
            try:
                sub.build_meta_human(ch, bp)
                built = True
                log("%s: build_meta_human done" % name)
            except Exception as e:
                unreal.log_warning("[Torque] %s: build_meta_human failed: %s" % (name, e))
        if not built:
            # No rig to build with: export the preview body's skeletal mesh instead, so the game
            # can still pose a body (register lists a body-only mesh too).
            lib = getattr(unreal, "MetaHumanCharacterExportBlueprintLibrary", None)
            if lib is not None and hasattr(lib, "export_geometry"):
                try:
                    sub.assemble_for_preview(ch)
                except Exception as e:
                    unreal.log_warning("[Torque] %s: assemble_for_preview failed: %s" % (name, e))
                try:
                    unreal.log("[Torque] export_geometry: " + (lib.export_geometry.__doc__ or "").strip()[:800])
                    lib.export_geometry(ch, folder)
                    log("%s: export_geometry to %s" % (name, folder))
                except Exception as e:
                    unreal.log_warning("[Torque] %s: export_geometry failed: %s" % (name, e))
        unreal.EditorAssetLibrary.save_directory(folder, only_if_is_dirty=True, recursive=True)
    log("make finished: see the can_build_meta_human lines above for whether a cloud rig (Epic sign-in) is needed")


def register():
    """Data/Avatars/avatars.json from the assembled MetaHumans under /Game/MetaHumans: each
    Blueprint named BP_*, and its body mesh (the skeletal mesh in its folder with body in the name)."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = reg.get_assets_by_path(ROOT, recursive=True) if unreal.EditorAssetLibrary.does_directory_exist(ROOT) else []
    blueprints, meshes = [], []
    for a in assets:
        cls = str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)
        pkg = str(a.package_name); name = str(a.asset_name)
        if cls == "Blueprint" and name.startswith("BP_"):
            blueprints.append((pkg, name))
        elif cls == "SkeletalMesh":
            meshes.append((pkg, name))
    out = []
    bp_folders = set(pkg.rsplit("/", 1)[0] for pkg, _ in blueprints)
    for pkg, name in sorted(meshes):   # a body mesh with no Blueprint beside it (export_geometry)
        folder = pkg.rsplit("/", 1)[0]
        if folder in bp_folders or "body" not in name.lower() or "face" in name.lower():
            continue
        out.append({"name": folder.rsplit("/", 1)[1], "blueprint": "", "body": "%s.%s" % (pkg, name)})
        bp_folders.add(folder)
        log("avatar %s: body only %s" % (out[-1]["name"], out[-1]["body"]))
    for pkg, name in sorted(blueprints):
        folder = pkg.rsplit("/", 1)[0]
        body = [p for p, n in meshes if p.startswith(folder + "/") and "body" in n.lower() and "face" not in n.lower()]
        out.append({"name": name[3:], "blueprint": "%s.%s_C" % (pkg, name),
                    "body": "%s.%s" % (body[0], body[0].rsplit("/", 1)[1]) if body else ""})
        log("avatar %s: %s (body %s)" % (name[3:], out[-1]["blueprint"], out[-1]["body"] or "found in the Blueprint"))
    path = os.path.join(project_dir(), "Data", "Avatars", "avatars.json")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if not out:
        unreal.log_warning("[Torque] no assembled MetaHumans (BP_*) under %s; players keep the shape body" % ROOT)
        if os.path.exists(path):
            os.remove(path)
        return
    with open(path, "w") as f:
        json.dump({"about": "Co-op avatars: assembled MetaHumans, one per player colour in this order "
                            "(Tools/ue_metahumans.py writes this; Source/TorqueShedProto/Shed/TorqueAvatar.* reads it).",
                   "avatars": out}, f, indent=1)
    log("%d avatars written to %s" % (len(out), path))


def main(args):
    mode = args[0] if args else "register"
    if mode == "discover":
        discover()
    elif mode == "make":
        make(rig=len(args) > 1 and args[1] == "rig")
        register()
    else:
        register()


main([a for a in sys.argv[1:] if not a.startswith("-")])
