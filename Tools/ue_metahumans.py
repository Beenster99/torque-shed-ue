"""MetaHuman avatars for the shed's co-op players (run in the Unreal editor, on the PC).

MetaHuman Creator is part of the editor since UE 5.6 (free; the cloud auto-rig and texture download
need an Epic account sign-in). The game shows another player as a MetaHuman when
Data/Avatars/avatars.json lists one (Source/TorqueShedProto/Shed/TorqueAvatar.*), one per player
colour, posed in code; without the file it keeps the shape body.

    py Tools/ue_metahumans.py              register: list the MetaHumans under /Game/MetaHumans in
                                           Data/Avatars/avatars.json (run after making or changing any)
    py Tools/ue_metahumans.py discover     write Saved/metahuman_api.txt: the editor's MetaHuman Python
                                           API in this engine version, to script the making next
    py Tools/ue_metahumans.py make         try to make the avatars below by script (best effort: logs
                                           what this engine's API lets it do, then registers)

Or without opening the editor:
    UnrealEditor-Cmd.exe TorqueShedProto.uproject -ExecutePythonScript="Tools/ue_metahumans.py discover" -unattended

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


def make():
    """Best effort with the API this engine has: make a MetaHuman Character asset per avatar, rig it,
    and assemble it. Each step logs what it found; a step the API hasn't stops with a clear note
    (then make them by hand as Docs/avatars.md says, and run this script again to register)."""
    discover()
    cls = getattr(unreal, "MetaHumanCharacter", None)
    sub_cls = getattr(unreal, "MetaHumanCharacterEditorSubsystem", None)
    if cls is None or sub_cls is None:
        unreal.log_warning("[Torque] this editor has no MetaHumanCharacter / MetaHumanCharacterEditorSubsystem in Python: "
                           "turn the MetaHuman plugins on, or make the avatars by hand (Docs/avatars.md)")
        return
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = None
    for fname in ("MetaHumanCharacterFactoryNew", "MetaHumanCharacterFactory"):
        if hasattr(unreal, fname):
            factory = getattr(unreal, fname)()
            break
    sub = unreal.get_editor_subsystem(sub_cls)
    log("subsystem methods: " + ", ".join(m for m in dir(sub) if not m.startswith("_")))
    for av in AVATARS:
        folder = ROOT + "/" + av["name"]
        path = folder + "/" + av["name"]
        ch = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
        if ch is None:
            try:
                ch = tools.create_asset(av["name"], folder, cls, factory)
            except Exception as e:
                unreal.log_warning("[Torque] couldn't create %s: %s" % (path, e))
                continue
        log("character: %s" % ch.get_path_name())
        # Open it for editing, rig it in the cloud and assemble, by whichever names this version uses.
        for step in (("try_add_object_to_edit", "add_object_to_edit"),
                     ("auto_rig_face", "request_auto_rigging"),
                     ("request_texture_sources", "download_texture_sources")):
            fn = next((getattr(sub, n) for n in step if hasattr(sub, n)), None)
            if fn is None:
                unreal.log_warning("[Torque] %s: no %s in this API; see Saved/metahuman_api.txt" % (av["name"], " / ".join(step)))
                continue
            try:
                fn(ch)
                log("%s: %s done" % (av["name"], fn.__name__))
            except Exception as e:
                unreal.log_warning("[Torque] %s: %s failed: %s" % (av["name"], fn.__name__, e))
    log("made what this API allowed; assemble each in its editor (Assemble, UE Optimized, Medium) if they aren't yet")


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
        make()
        register()
    else:
        register()


main([a for a in sys.argv[1:] if not a.startswith("-")])
