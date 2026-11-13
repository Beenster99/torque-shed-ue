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
    py Tools/ue_metahumans.py make rig nowardrobe   the same with no hair or clothes (whole bodies)
    py Tools/ue_metahumans.py make rig A B          only Avatar_A and Avatar_B (the others kept)
    py Tools/ue_metahumans.py make nofab A          the Creator's own wardrobe only (no Fab clothes)
    (make keeps each avatar's old build in /Game/MetaHumanBackup until the new one has its meshes)
    py Tools/ue_metahumans.py wardrobe     list what can dress them (Saved/metahuman_content.txt)
    py Tools/ue_metahumans.py fab          import beenster's Fab MetaHuman clothes (.mhpkg) for `make` (make does it too)
    py Tools/ue_metahumans.py slim         cap the avatars' textures at MAX_TEXTURE (make does it too)

Or without opening the editor:
    UnrealEditor-Cmd.exe TorqueShedProto.uproject -ExecutePythonScript="G:/Dev/torque-shed-ue/Tools/ue_metahumans.py discover" -unattended
(forward slashes in the path: UE escape-processes backslashes, so "\\t" becomes a tab)

Needs the MetaHuman plugins on (Edit > Plugins > "MetaHuman": MetaHuman Creator / MetaHuman Character).
"""
import json
import os
import sys

import unreal

ROOT = "/Game/MetaHumans"            # what the game uses: the built Blueprints and meshes (cooked once avatars exist)
SOURCES = "/Game/MetaHumanSources"   # the MetaHuman Character assets they're built from: never cooked
# How the avatars are built: shed walkers seen at 2-10 m. UE Optimized at Medium keeps a face that
# holds up at 2 m, with card hair (strands only come at High/Cinematic); try "LOW" if size matters
# more. Every texture under /Game/MetaHumans is then capped at MAX_TEXTURE (`py ... slim` redoes it).
PIPELINE_TYPE = "OPTIMIZED"    # the pipeline_type member whose name has this
PIPELINE_QUALITY = "MEDIUM"    # the pipeline_quality member whose name has this
MAX_TEXTURE = 1024
# Hair styles too short to build as a groom (a buzz cut or waves are painted on the scalp at
# Optimized/Medium, so the avatar looks bald in game): never picked.
# The short (_S_) styles' cards sit flush with the scalp and don't show in game either (round 116:
# only the medium BobCurly did), so the avatars wear medium (_M_) styles.
HAIR_TOO_SHORT = ("buzz", "360", "shaved", "bald", "fade", "stubble", "_s_")
AVATARS = [   # the people made by `make`: a name each, their height (cm), and their hair and clothes (wardrobe keywords)
    {"name": "Avatar_A", "height": 170.0, "hair": ["_m_", "straight", "part", "slick"], "hair_colour": [0.045, 0.028, 0.016], "outfit": ["jacket", "work", "jean", "boot"]},
    {"name": "Avatar_B", "height": 176.0, "hair": ["_m_", "wav", "messy", "tousle"], "hair_colour": [0.11, 0.065, 0.035], "outfit": ["hoodie", "jacket", "jean", "boot"]},
    {"name": "Avatar_C", "height": 182.0, "hair": ["_m_", "side", "fringe", "swept"], "hair_colour": [0.25, 0.17, 0.09], "outfit": ["jacket", "shirt", "jean", "boot"]},
    {"name": "Avatar_D", "height": 188.0, "hair": ["_m_", "curl", "wav"], "hair_colour": [0.02, 0.015, 0.012], "outfit": ["shirt", "jacket", "jean", "boot"]},
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


def _constraint_name(c):
    try:
        return str(c.get_editor_property("name"))
    except Exception:
        return str(getattr(c, "name", ""))


def _vary_body(sub, ch, i):
    """A different height for each avatar, through the parametric body constraints. (The array's
    structs come back as copies: each changed one is put back by its index, then read back.)"""
    want = AVATARS[i % len(AVATARS)].get("height", 176.0)
    try:
        log("%s: fixed_body_type = %s" % (ch.get_name(), ch.get_editor_property("fixed_body_type")))
    except Exception:
        pass
    try:
        cons = list(sub.get_body_constraints(ch, False))
    except Exception as e:
        unreal.log_warning("[Torque] get_body_constraints failed: %s" % e)
        return
    for idx, c in enumerate(cons):
        n = _constraint_name(c).lower()
        if "height" in n and "head" not in n:
            try:
                lo, hi = c.get_editor_property("min_measurement"), c.get_editor_property("max_measurement")
                log("%s: height range %s .. %s" % (ch.get_name(), lo, hi))
                v = min(max(want, lo), hi) if hi > lo else want
            except Exception:
                v = want
            c.set_editor_property("target_measurement", v)
            c.set_editor_property("is_active", True)
            cons[idx] = c
    log("body constraints: " + ", ".join(_constraint_name(c) for c in cons))
    try:
        sub.set_body_constraints(ch, cons)
    except Exception as e:
        unreal.log_warning("[Torque] set_body_constraints failed: %s" % e)
        return
    try:
        back = [c for c in sub.get_body_constraints(ch, False) if _constraint_name(c).lower() == "height"]
        got = back[0].get_editor_property("target_measurement") if back else None
        log("%s: height asked %.0f cm, constraint now %s (active %s)" % (ch.get_name(), want, got,
            back[0].get_editor_property("is_active") if back else None))
    except Exception as e:
        log("couldn't read the height back: %s" % e)


_WARDROBE = None


WEAR_CLASSES = ("wardrobe", "groom", "outfit", "cloth", "garment")


NO_FAB = False   # `make nofab`: only the Creator's own wardrobe, never the Fab clothes
BACKUP = "/Game/MetaHumanBackup"   # an avatar's previous build while a new one is made; never cooked
FAB_WARDROBE = "/Game/MetaHumanWardrobe"   # beenster's Fab MetaHuman clothes (.mhpkg), imported by `fab`; cooked only by reference
FAB_VAULT = os.environ.get("TORQUE_FAB_VAULT") or r"C:\ProgramData\Epic\EpicGamesLauncher\VaultCache\FabLibrary"
FAB_SKIP = ("helmet",)   # the helmet goes on the rider by Tools/ue_build_level.py


def fab_wardrobe():
    """Every MetaHuman package (.mhpkg) in the Fab cache but the helmet, imported through the MetaHuman
    plugin's MetaHumanPackageFactory to /Game/MetaHumanWardrobe/<folder>, so `make` can dress the
    avatars with its wardrobe items (beenster's MetaHuman Jeans and Boots, Nov 12). The build bakes
    the garments into each avatar's own folder, using these sources' materials and textures (cooked
    by reference; the wardrobe items themselves are editor-only and nothing in the game uses them)."""
    if not os.path.isdir(FAB_VAULT):
        log("fab: no folder %s" % FAB_VAULT)
        return
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = getattr(unreal, "MetaHumanPackageFactory", None)
    for d in sorted(os.listdir(FAB_VAULT)):
        full = os.path.join(FAB_VAULT, d)
        if not os.path.isdir(full) or any(w in d.lower() for w in FAB_SKIP):
            continue
        pkgs = []
        for root, _, names in os.walk(full):
            pkgs += [os.path.join(root, n) for n in names if n.lower().endswith(".mhpkg")]
        if not pkgs:
            others = []
            for root, _, names in os.walk(full):
                others += names
            log("fab: %s has no MetaHuman package (%s); not wearable by the avatars" % (d, ", ".join(others[:8])))
            continue
        dest = FAB_WARDROBE + "/" + "".join(c if c.isalnum() else "_" for c in d.split("-")[0])
        if unreal.EditorAssetLibrary.does_directory_exist(dest) and unreal.EditorAssetLibrary.list_assets(dest, recursive=True):
            log("fab: %s already imported at %s" % (d, dest))
            continue
        src = max(pkgs, key=os.path.getsize)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", src)
        t.set_editor_property("destination_path", dest)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        if factory is not None:
            t.set_editor_property("factory", factory())
        try:
            tools.import_asset_tasks([t])
        except Exception as e:
            unreal.log_warning("[Torque] fab: importing %s failed: %s" % (src, e))
            continue
        got = [str(g) for g in (t.get_editor_property("imported_object_paths") or [])]
        log("fab: %s -> %s: %d assets: %s" % (src, dest, len(got), ", ".join(got[:30])))
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Game"], True)
    global _WARDROBE
    _WARDROBE = None


def _wardrobe():
    """Everything that could dress a MetaHuman: wardrobe items, and the hair grooms, outfits and
    garments they wrap, in any content mount whose path or class mentions MetaHuman, grooms or
    outfits (Optional Content lands in a mount of its own). The whole inventory, by mount and
    class, goes to Saved/metahuman_content.txt."""
    global _WARDROBE
    if _WARDROBE is not None:
        return _WARDROBE
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        assets = reg.get_all_assets(True)
    except Exception:
        assets = []
        for root in ("/MetaHumanCharacter", "/MetaHumanCoreTech", "/MetaHumanSDK", "/MetaHumanDefaultPipeline", "/Game"):
            try:
                assets += list(reg.get_assets_by_path(root, recursive=True))
            except Exception:
                pass
    found, counts = [], {}
    for a in assets:
        pkg = str(a.package_name)
        cls = _cls_name(a)
        mount = "/" + pkg.split("/")[1] if pkg.count("/") > 1 else pkg
        low_cls, low_pkg = cls.lower(), pkg.lower()
        if "metahuman" in low_pkg or any(w in low_cls for w in WEAR_CLASSES):
            counts[(mount, cls)] = counts.get((mount, cls), 0) + 1
        if any(w in low_cls for w in WEAR_CLASSES) and "binding" not in low_cls and "cache" not in low_cls:
            # Never the rider's helmet (round 141: its wardrobe item has no editor pipeline, and the
            # build that picked it made no meshes at all), nor our own backups.
            if "helmet" in low_pkg or low_pkg.startswith(BACKUP.lower() + "/") or (NO_FAB and low_pkg.startswith(FAB_WARDROBE.lower() + "/")):
                continue
            found.append((pkg, str(a.asset_name), cls))
    path = os.path.join(project_dir(), "Saved", "metahuman_content.txt")
    try:
        with open(path, "w", encoding="utf-8") as f:
            for (mount, cls), n in sorted(counts.items()):
                f.write("%-40s %-40s %d\n" % (mount, cls, n))
            f.write("\n")
            for pkg, name, cls in sorted(found):
                f.write("%s  (%s)\n" % (pkg, cls))
    except Exception:
        pass
    _WARDROBE = found
    log("wardrobe candidates found: %d (inventory in %s)" % (len(found), path))
    for (mount, cls), n in sorted(counts.items()):
        if any(w in cls.lower() for w in WEAR_CLASSES):
            log("  %s: %d %s" % (mount, n, cls))
    return found


def _as_wardrobe_item(obj):
    """obj itself if it's a wardrobe item, else a new transient wardrobe item wrapping it (a groom,
    an outfit or a garment mesh) as its principal asset; None when this API can't."""
    if obj is None:
        return None
    if "wardrobe" in type(obj).__name__.lower():
        return obj
    cls = _find_class("metahuman", "wardrobe", "item")
    if cls is None:
        return None
    try:
        item = unreal.new_object(cls)
    except Exception as e:
        log("couldn't make a %s: %s" % (cls.__name__, e))
        return None
    used = _set_first(item, obj, "principal_asset", "asset", "item_asset", "source_asset")
    log("wrapped %s in a %s (via %s; fields %s)" % (obj.get_path_name(), cls.__name__, used, _props(item)))
    return item if used else None


def _collections(sub, ch):
    """The character's own collection (what the build assembles from) and the editor's preview one."""
    out = []
    for get in (lambda: ch.get_editor_property("internal_collection"),
                lambda: sub.get_preview_collection(ch) if hasattr(sub, "get_preview_collection") else None):
        try:
            c = get()
        except Exception:
            c = None
        if c is not None and all(c != o for o in out):
            out.append(c)
    return out


def _key_for(coll, slot, item, r):
    """The palette item key for item in slot: from try_add's result, else among the slot's keys."""
    for v in (r if isinstance(r, (tuple, list)) else [r]):
        if v is not None and not isinstance(v, bool) and "key" in type(v).__name__.lower():
            return v
    try:
        keys = list(coll.get_item_keys_for_slot(unreal.Name(slot)))
    except Exception:
        keys = []
    stem = item.get_name().lower()
    for k in keys:
        if stem in str(k).lower():
            return k
    return keys[-1] if keys else None


UNDRESS_SLOTS = ("hair", "beard", "mustache", "outfit", "garment", "cloth", "top", "bottom", "shoe", "feet")


def _drop_slot_selections(inst, slot, name):
    """Take slot's entries out of the instance's own selection list (the property that
    get_slot_selection_data reads), written back whole. Logs what it saw."""
    out = []
    try:
        log("%s: %s selection data before: %s" % (name, slot, inst.get_slot_selection_data()))
    except Exception as e:
        log("%s: get_slot_selection_data failed: %s" % (name, e))
    for prop in ("slot_selections", "slot_selection_data", "selections", "pipeline_slot_selections"):
        try:
            sels = list(inst.get_editor_property(prop))
        except Exception:
            continue
        keep = [x for x in sels if slot.lower() not in str(x).lower()]
        if len(keep) == len(sels):
            out.append("%s: %d entries, none for %s" % (prop, len(sels), slot))
            continue
        try:
            inst.set_editor_property(prop, keep)
            out.append("%s: %d -> %d entries" % (prop, len(sels), len(keep)))
        except Exception as e:
            log("%s: writing %s failed: %s" % (name, prop, e))
    if not out:
        log("%s: instance properties: %s" % (name, _props(inst)[:600]))
    try:
        log("%s: %s selection data after: %s" % (name, slot, inst.get_slot_selection_data()))
    except Exception:
        pass
    return out


def _undress(sub, ch, name):
    """`nowardrobe`: take the hair and clothes off again (brows and lashes stay), so the build gives
    the whole body. The selection on each collection's default instance is cleared, then the slot's
    items are removed from the collection; every attempt is logged."""
    colls = _collections(sub, ch)
    if not colls:
        log("%s: no collection to undress" % name)
        return
    try:
        slots = [str(n) for n in colls[0].get_slot_names()]
    except Exception as e:
        unreal.log_warning("[Torque] %s: get_slot_names failed: %s" % (name, e))
        return
    for ci, coll in enumerate(colls):
        try:
            inst = coll.get_editor_property("default_instance")
        except Exception:
            inst = getattr(coll, "default_instance", None)
        if ci == 0:
            log("collection methods: " + ", ".join(m for m in dir(coll) if not m.startswith("_")))
            if inst is not None:
                log("instance methods: " + ", ".join(m for m in dir(inst) if not m.startswith("_")))
        for slot in slots:
            if not any(w in slot.lower() for w in UNDRESS_SLOTS):
                continue
            sn = unreal.Name(slot)
            try:
                keys = list(coll.get_item_keys_for_slot(sn))
            except Exception:
                keys = []
            done = []
            if inst is not None:
                for m in ("clear_slot_selection", "clear_slot_selections", "clear_selection_for_slot", "remove_slot_selection"):
                    if hasattr(inst, m):
                        try:
                            getattr(inst, m)(sn)
                            done.append(m)
                            break
                        except Exception as e:
                            log("%s: %s(%s) failed: %s" % (name, m, slot, e))
                if not done and hasattr(inst, "set_single_slot_selection"):
                    # Selecting nothing: an empty palette item key (the parameter is a key struct).
                    empties = []
                    kcls = getattr(unreal, "MetaHumanPaletteItemKey", None)
                    if kcls is not None:
                        empties.append(kcls())
                    empties += [unreal.Name("None"), None]
                    for none in empties:
                        try:
                            r = inst.set_single_slot_selection(sn, none)
                            done.append("set_single_slot_selection(%r)=%r" % (none, r))
                            break
                        except Exception as e:
                            log("%s: set_single_slot_selection(%s, %r) failed: %s" % (name, slot, none, e))
                if inst is not None:
                    done += _drop_slot_selections(inst, slot, name)
            for k in keys:
                for m in ("remove_item", "try_remove_item", "remove_item_by_key"):
                    if hasattr(coll, m):
                        try:
                            r = getattr(coll, m)(k)
                            done.append("%s(%s)=%r" % (m, k, r))
                            break
                        except Exception as e:
                            log("%s: %s(%s) failed: %s" % (name, m, k, e))
            try:
                left = ", ".join(str(k) for k in coll.get_item_keys_for_slot(sn))
            except Exception:
                left = "?"
            log("%s: undressed %s in collection %d: %s; items still in the collection (not selected): %s" % (name, slot, ci, "; ".join(done) or "nothing worked", left or "none"))
    try:
        unreal.EditorAssetLibrary.save_loaded_asset(ch, only_if_is_dirty=False)
    except Exception:
        pass


def _dress(sub, ch, av):
    """Hair and workshop clothes from the wardrobe: for each of the character's slots, the item whose
    name best matches the avatar's keywords (hair slots: av["hair"]; outfit slots: av["outfit"]),
    added to the character's collections and selected on their default instance."""
    colls = _collections(sub, ch)
    if not colls:
        unreal.log_warning("[Torque] %s: no collection to dress" % av["name"])
        return
    try:
        slots = [str(n) for n in colls[0].get_slot_names()]
    except Exception as e:
        unreal.log_warning("[Torque] %s: get_slot_names failed: %s" % (av["name"], e))
        return
    log("%s: wardrobe slots %s (%d collections)" % (av["name"], ", ".join(slots), len(colls)))
    items = _wardrobe()
    for slot in slots:
        low = slot.lower()
        if "hair" in low:
            words = av.get("hair", [])
        elif any(w in low for w in ("outfit", "cloth", "garment", "body", "top", "bottom", "shoe", "feet")):
            words = av.get("outfit", [])
        else:
            continue   # brows, lashes, beards, skin: the character's own
        hair_slot = "hair" in low
        best, score = None, 0
        for pkg, name, cls in items:
            n = name.lower() + " " + pkg.lower()
            # Wardrobe items first (what the Creator itself offers); a bare groom or cloth asset is wrapped.
            is_hair = ("hair" in n or "groom" in cls.lower()) and not any(w in n for w in ("brow", "beard", "mustache", "lash", "fuzz"))
            if hair_slot != is_hair:
                continue
            if hair_slot and any(w in n for w in HAIR_TOO_SHORT):
                continue
            if not hair_slot and "groom" in cls.lower():
                continue
            if "top" in low and any(w in n for w in ("pant", "trouser", "jean", "short", "boot", "shoe")):
                continue
            if "bottom" in low and any(w in n for w in ("shirt", "jacket", "hoodie", "top", "tee")):
                continue
            # Round 142: the Fab boots tied the Fab jeans for the bottom slot and won it: legs in boots.
            if "bottom" in low and any(w in n for w in ("boot", "shoe", "trainer", "sneaker", "sock")):
                continue
            if any(w in low for w in ("shoe", "feet", "foot")) and any(w in n for w in ("jean", "pant", "trouser", "short", "shirt", "jacket")):
                continue
            sc = sum(3 if w in name.lower() else (1 if w in n else 0) for w in words) + 1
            if "wardrobe" in cls.lower():
                sc += 2
            if pkg.startswith(FAB_WARDROBE + "/"):
                sc += 4   # beenster's own Fab clothes beat the Creator's stock ones
            if sc > score:
                best, score = (pkg, name), sc
        if best is None:
            log("%s: nothing in the wardrobe for slot %s (%s)" % (av["name"], slot, ", ".join(words)))
            continue
        item = _as_wardrobe_item(unreal.load_asset(best[0]))
        if item is None:
            log("%s: %s isn't a wardrobe item and couldn't be wrapped in one" % (av["name"], best[0]))
            continue
        for ci, coll in enumerate(colls):
            try:
                r = coll.try_add_item_from_wardrobe_item(unreal.Name(slot), item)
            except Exception as e:
                unreal.log_warning("[Torque] %s: dressing %s with %s failed: %s" % (av["name"], slot, best[0], e))
                log("try_add_item_from_wardrobe_item: " + (coll.try_add_item_from_wardrobe_item.__doc__ or "")[:600])
                continue
            key = _key_for(coll, slot, item, r)
            log("%s: %s <- %s (collection %d, result %r, key %s)" % (av["name"], slot, best[0], ci, r, key))
            try:
                inst = coll.get_editor_property("default_instance")
            except Exception:
                inst = getattr(coll, "default_instance", None)
            if inst is None or key is None:
                log("%s: %s not selected (instance %s, key %s)" % (av["name"], slot, inst, key))
                continue
            try:
                sel = inst.set_single_slot_selection(unreal.Name(slot), key)
                log("%s: %s selected (%r)" % (av["name"], slot, sel))
            except Exception as e:
                unreal.log_warning("[Torque] %s: selecting %s failed: %s; %s" % (av["name"], slot, e,
                                   (inst.set_single_slot_selection.__doc__ or "")[:400]))
    for ci, coll in enumerate(colls):
        try:
            for slot in slots:
                log("  collection %d %s: %s" % (ci, slot, ", ".join(str(k) for k in coll.get_item_keys_for_slot(unreal.Name(slot)))))
        except Exception:
            pass
    try:
        unreal.EditorAssetLibrary.save_loaded_asset(ch, only_if_is_dirty=False)
    except Exception:
        pass


def _find_class(*words):
    """The first unreal class whose name has all of words (case blind), for structs whose exact
    name this engine version decides."""
    for n in sorted(dir(unreal)):
        low = n.lower()
        if all(w in low for w in words):
            return getattr(unreal, n)
    return None


def _is_rigged(sub, ch):
    """True / False from whichever is-rigged query this version has, or None when it has none."""
    for n in ("is_character_rigged", "is_rigged", "has_face_dna", "get_rigging_state"):
        if not hasattr(sub, n):
            continue
        try:
            v = getattr(sub, n)(ch)
        except Exception:
            continue
        if isinstance(v, bool):
            return v
        txt = str(v).upper()   # an enum such as RIGGED / UNRIGGED / RIG_REQUIRES_REFIT
        return ("RIGGED" in txt and "UNRIGGED" not in txt and "NOT" not in txt)
    for n in ("is_rigged", "has_face_dna"):
        if hasattr(ch, n):
            try:
                return bool(getattr(ch, n)())
            except Exception:
                pass
    return None


def _auto_rig(sub, ch, name):
    try:
        params = unreal.MetaHumanCharacterAutoRiggingRequestParams() if hasattr(unreal, "MetaHumanCharacterAutoRiggingRequestParams") else None
        if params is not None:
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


def _texture_sources(sub, ch, name):
    """Download Texture Sources (cloud, blocking, 2K where it can be chosen): the build refuses a
    character without them."""
    if not hasattr(sub, "request_texture_sources"):
        unreal.log_warning("[Torque] %s: no request_texture_sources in this API" % name)
        return
    try:
        cls = _find_class("metahuman", "texture", "request", "param") or _find_class("metahuman", "texture", "source", "param")
        params = cls() if cls else None
        if params is not None:
            log("texture request params (%s): %s" % (type(params).__name__, _props(params)))
            _set_first(params, True, "blocking", "b_blocking")
            res = _find_class("metahuman", "texture", "resolution") or _find_class("metahuman", "texture", "quality")
            if res is not None:
                v = next((getattr(res, n) for n in dir(res) if "2K" in n.upper() or "2048" in n), None)
                if v is not None:
                    for f in ("resolution", "face_resolution", "body_resolution", "texture_resolution", "quality"):
                        _set_first(params, v, f)
            sub.request_texture_sources(ch, params)
        else:
            sub.request_texture_sources(ch)
        log("%s: texture sources requested (cloud)" % name)
    except Exception as e:
        unreal.log_warning("[Torque] %s: request_texture_sources failed: %s" % (name, e))


def _save(ch, name):
    try:
        ok = unreal.EditorAssetLibrary.save_loaded_asset(ch, only_if_is_dirty=False)
        log("%s: saved = %s" % (name, ok))
    except Exception as e:
        unreal.log_warning("[Torque] %s: save failed: %s" % (name, e))


def _export_materials(lib, ch, name, folder):
    """Copy the character's materials into its avatar folder, so the exported meshes stop using the
    plugin's placeholder (which isn't packaged) and register can take them as a body-only avatar."""
    if not hasattr(lib, "export_materials"):
        unreal.log_warning("[Torque] %s: no export_materials in this API" % name)
        return
    unreal.log("[Torque] export_materials: " + (lib.export_materials.__doc__ or "").strip()[:800])
    cls = _find_class("metahuman", "material", "export", "param")
    try:
        if cls is not None:
            mp = cls()
            log("material export params (%s): %s" % (cls.__name__, _props(mp)))
            _set_first(mp, folder, "project_path", "output_folder", "path")
            _set_first(mp, True, "overwrite_existing_assets")
            r = lib.export_materials(ch, mp)
        else:
            r = lib.export_materials(ch, folder)
        log("%s: export_materials to %s = %s" % (name, folder, r))
    except Exception as e:
        unreal.log_warning("[Torque] %s: export_materials failed: %s" % (name, e))


def _set_enum(obj, field, word):
    """Set an enum field to its member whose name contains word (logging the choices)."""
    try:
        cur = obj.get_editor_property(field)
    except Exception:
        log("no %s in %s" % (field, type(obj).__name__))
        return
    members = [n for n in dir(type(cur)) if n.isupper()]
    pick = next((n for n in members if word in n), None)
    log("%s: %s of %s -> %s" % (field, cur, ", ".join(members), pick))
    if pick:
        obj.set_editor_property(field, getattr(type(cur), pick))


def slim():
    """Cap every texture under /Game/MetaHumans at MAX_TEXTURE (what's cooked is the cap, the
    source stays), and list the grooms (strand hair is the big one; cards are fine)."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    if not unreal.EditorAssetLibrary.does_directory_exist(ROOT):
        return
    capped = 0
    for a in reg.get_assets_by_path(ROOT, recursive=True):
        cls = str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)
        pkg = str(a.package_name)
        if cls in ("Texture2D", "VirtualTexture2D"):
            t = unreal.load_asset(pkg)
            if t is None:
                continue
            try:
                w = t.blueprint_get_size_x()
                cur = t.get_editor_property("max_texture_size")
                if w > MAX_TEXTURE and (cur == 0 or cur > MAX_TEXTURE):
                    t.set_editor_property("max_texture_size", MAX_TEXTURE)
                    unreal.EditorAssetLibrary.save_loaded_asset(t, only_if_is_dirty=False)
                    capped += 1
            except Exception as e:
                unreal.log_warning("[Torque] couldn't cap %s: %s" % (pkg, e))
        elif cls in ("GroomAsset", "GroomBindingAsset"):
            log("groom: %s %s" % (cls, pkg))
    log("capped %d textures under %s at %d" % (capped, ROOT, MAX_TEXTURE))


def make(rig=False, wardrobe=True, only=None):
    """Make a MetaHuman Character asset per avatar with the UE 5.8 editor API and build it.

    Without `rig`, nothing goes to Epic's cloud: the build uses whatever rig the character has, and
    can_build_meta_human logs why when it needs one. With `rig` (`make rig`), each character is first
    auto-rigged in the cloud (Joints Only, blocking), which needs the Epic account sign-in. With
    wardrobe False (`make nowardrobe`), the hair and clothes are taken off instead of put on."""
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
        if only and not any(name.lower().endswith("_" + o.lower()) or name.lower() == o.lower() for o in only):
            continue
        folder = ROOT + "/" + name                 # build output
        src_folder = SOURCES + "/" + name
        path = src_folder + "/" + name
        old = folder + "/" + name                  # where earlier runs made the character
        eal = unreal.EditorAssetLibrary
        if not eal.does_asset_exist(path) and eal.does_asset_exist(old):
            log("%s: moving the character source %s -> %s (sources aren't cooked)" % (name, old, path))
            if not eal.rename_asset(old, path):
                unreal.log_warning("[Torque] couldn't move %s to %s" % (old, path))
        ch = unreal.load_asset(path) if eal.does_asset_exist(path) else None
        if ch is None:
            try:
                ch = tools.create_asset(name, src_folder, cls, factory() if factory else None)
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
        if wardrobe:
            _dress(sub, ch, AVATARS[i % len(AVATARS)])
        else:
            _undress(sub, ch, name)
        if rig:
            if _is_rigged(sub, ch) is not True:
                _auto_rig(sub, ch, name)
                if _is_rigged(sub, ch) is False:
                    log("%s: still not rigged after the auto-rig; trying once more" % name)
                    _auto_rig(sub, ch, name)
                state = _is_rigged(sub, ch)
                log("%s: rigged = %s" % (name, state))
                if state is not True:
                    # Round 141: the cloud rig is asynchronous (it finishes on later editor ticks, which a
                    # headless script never gives it), and building straight after made no meshes. So
                    # build only once the rig can be seen to be done: rerun `make` (without rig) after it.
                    unreal.log_warning("[Torque] %s: the auto-rig was requested but isn't confirmed done; NOT building. "
                                       "Rig it in the editor (Docs/avatars.md), then run make without rig." % name)
                    _save(ch, name)
                    continue
            else:
                log("%s: already rigged" % name)
            _texture_sources(sub, ch, name)
            _save(ch, name)
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
            for f in [m for m in dir(bp) if not m.startswith("_")]:
                try:
                    v = bp.get_editor_property(f)
                except Exception:
                    continue
                log("  build param %s = %r" % (f, v))
                if wardrobe and isinstance(v, bool) and not v and any(w in f.lower() for w in ("groom", "hair", "cloth", "outfit", "wardrobe", "garment")):
                    bp.set_editor_property(f, True)
                    log("  build param %s -> True" % f)
            _set_enum(bp, "pipeline_type", PIPELINE_TYPE)
            _set_enum(bp, "pipeline_quality", PIPELINE_QUALITY)
            for f, want in (("pipeline_type", PIPELINE_TYPE), ("pipeline_quality", PIPELINE_QUALITY)):
                try:
                    got = str(bp.get_editor_property(f))
                except Exception:
                    got = "?"
                if want not in got.upper():
                    unreal.log_warning("[Torque] %s: %s reads back %s, not %s" % (name, f, got, want))
                log("%s: building with %s = %s" % (name, f, got))
            # A rebuild at another quality leaves the old build's files behind, and /Game/MetaHumans
            # is always cooked, so the old build is moved aside first (to BACKUP, never cooked) and
            # only deleted once the new one has its meshes. Round 141 lost all four avatars when a
            # build made only a Blueprint after the old build had been deleted.
            eal = unreal.EditorAssetLibrary
            backup = BACKUP + "/" + name
            had_old = eal.does_directory_exist(folder) and bool(eal.list_assets(folder, recursive=True))
            if had_old:
                if eal.does_directory_exist(backup):
                    eal.delete_directory(backup)
                moved = eal.rename_directory(folder, backup)
                log("%s: old build moved aside to %s (%s)" % (name, backup, "ok" if moved else "FAILED"))
                if not moved:
                    unreal.log_warning("[Torque] %s: couldn't move the old build aside; not rebuilding it" % name)
                    continue
            made = {}
            try:
                sub.build_meta_human(ch, bp)
                log("%s: build_meta_human done" % name)
                # What it made, by class: grooms and garments should be among them.
                reg = unreal.AssetRegistryHelpers.get_asset_registry()
                reg.scan_paths_synchronous([folder], True)
                for a in reg.get_assets_by_path(folder, recursive=True):
                    made[_cls_name(a)] = made.get(_cls_name(a), 0) + 1
                log("%s: built %s" % (name, ", ".join("%d %s" % (n, c) for c, n in sorted(made.items()))))
            except Exception as e:
                unreal.log_warning("[Torque] %s: build_meta_human failed: %s" % (name, e))
            built = made.get("SkeletalMesh", 0) > 0
            if built:
                if had_old:
                    eal.delete_directory(backup)
                    log("%s: new build has %d skeletal meshes; old build deleted" % (name, made["SkeletalMesh"]))
            else:
                unreal.log_warning("[Torque] %s: the new build has NO skeletal meshes; putting the old build back" % name)
                if eal.does_directory_exist(folder):
                    eal.delete_directory(folder)
                if had_old:
                    back = eal.rename_directory(backup, folder)
                    log("%s: old build restored from %s (%s)" % (name, backup, "ok" if back else "FAILED: it's still in " + backup))
                    continue   # never the bare geometry export over a restored build
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
                    xp = unreal.MetaHumanGeometryExportParams()
                    log("geometry export params: " + _props(xp))
                    used = _set_first(xp, folder, "project_path")   # else it exports into /Game/MetaHumans itself
                    if used is None:   # a DirectoryPath struct field
                        dp = unreal.DirectoryPath()
                        dp.set_editor_property("path", folder)
                        used = _set_first(xp, dp, "project_path")
                    _set_first(xp, True, "overwrite_existing_assets")
                    _set_first(xp, True, "full_body_skeletal_mesh")   # head and body as one posable mesh
                    _set_first(xp, True, "body_skeletal_mesh")
                    _set_first(xp, True, "head_skeletal_mesh")
                    for f in ("body_skeletal_mesh", "full_body_skeletal_mesh", "head_skeletal_mesh", "project_path"):
                        try:
                            v = xp.get_editor_property(f)
                            log("  %s = %r (%s)" % (f, v, type(v).__name__))
                        except Exception:
                            pass
                    r = lib.export_geometry(ch, xp)
                    log("%s: export_geometry to %s (folder via %s) = %s" % (name, folder, used, r))
                except Exception as e:
                    unreal.log_warning("[Torque] %s: export_geometry failed: %s" % (name, e))
                _export_materials(lib, ch, name, folder)
        for d in (src_folder, folder):
            if unreal.EditorAssetLibrary.does_directory_exist(d):
                unreal.EditorAssetLibrary.save_directory(d, only_if_is_dirty=True, recursive=True)
    slim()
    log("make finished: see the can_build_meta_human lines above for whether a cloud rig (Epic sign-in) is needed")


INI = ("Config", "DefaultGame.ini")
# /Game/MetaHumans is in here too: until avatars are assembled it only holds unbuilt
# MetaHumanCharacter sources, which reference the never-cooked plugin and fail the cook
# ("Content is missing from cook"). NeverCook wins over its DirectoriesToAlwaysCook line.
NEVER_COOK = ["/MetaHumanCharacter", "/MetaHumanCoreTech", "/MetaHumanSDK", "/MetaHumanCoreTechLib",
              "/MetaHumanDefaultPipeline", "/Game/MetaHumans"]


def plugin_mounts():
    """The content roots of the enabled MetaHuman plugins (to keep out of the cook), logged."""
    out = []
    lib = getattr(unreal, "PluginBlueprintLibrary", None)
    if lib is None or not hasattr(lib, "get_enabled_plugin_names"):
        return out
    for n in lib.get_enabled_plugin_names():
        if "metahuman" in str(n).lower():
            out.append("/" + str(n))
    log("enabled MetaHuman plugins: " + ", ".join(out))
    return out


def set_never_cook(on):
    """Put the MetaHuman plugin content in DirectoriesToNeverCook (no avatars) or take it out
    (avatars listed: their Blueprints may use plugin materials), between the BEGIN/END markers."""
    path = os.path.join(project_dir(), *INI)
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    try:
        a = lines.index("; BEGIN metahuman-never-cook")
        b = lines.index("; END metahuman-never-cook")
    except ValueError:
        unreal.log_warning("[Torque] no metahuman-never-cook block in %s; cook settings unchanged" % path)
        return
    roots = sorted(set(NEVER_COOK + plugin_mounts())) if on else []
    body = ['+DirectoriesToNeverCook=(Path="%s")' % r for r in roots]
    if lines[a + 1:b] == body:
        return
    lines[a + 1:b] = body
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    log("MetaHuman plugin content %s the cook (%s): commit it" % ("kept out of" if on else "back in", path))


POSED_BONES = ("pelvis", "spine_01", "head", "upperarm_l", "hand_l", "upperarm_r", "hand_r",
               "thigh_l", "foot_l", "thigh_r", "foot_r")


def _bone_names(mesh):
    lib = getattr(unreal, "SkeletalMeshEditorSubsystem", None)
    for fn in (lambda: [str(n) for n in mesh.get_bone_names()] if hasattr(mesh, "get_bone_names") else None,
               lambda: [str(n) for n in unreal.get_editor_subsystem(lib).get_bone_names(mesh)] if lib else None):
        try:
            names = fn()
            if names:
                return names
        except Exception:
            continue
    return None   # this version can't list bones from Python: not checked


def _incomplete_body(pkg):
    """Why the body mesh at pkg isn't a usable avatar body, or "" when it is."""
    mesh = unreal.load_asset(pkg)
    if not isinstance(mesh, unreal.SkeletalMesh):
        return "not a skeletal mesh"
    try:
        sk = mesh.get_editor_property("skeleton")
    except Exception:
        sk = None
    if sk is None:
        return "no skeleton"
    try:
        mats = [m.get_editor_property("material_interface") for m in mesh.get_editor_property("materials")]
    except Exception:
        mats = []
    mats = [m for m in mats if m is not None]
    if not mats:
        return "no materials"
    outside = [m.get_path_name() for m in mats if not m.get_path_name().startswith("/Game/")]
    if outside:
        return "materials outside /Game (plugin content isn't packaged): " + ", ".join(outside[:3])
    mel = getattr(unreal, "MaterialEditingLibrary", None)
    if mel is not None and hasattr(mel, "get_used_textures"):
        tex = set()
        for m in mats:
            try:
                tex.update(t.get_path_name() for t in mel.get_used_textures(m) if t is not None)
            except Exception:
                pass
        outside = sorted(t for t in tex if not t.startswith("/Game/") and not t.startswith("/Engine/"))
        if outside:
            return "textures outside /Game (plugin content isn't packaged): " + ", ".join(outside[:3])
    names = _bone_names(mesh)
    if names is None:
        log("%s: couldn't list its bones from Python; not checked" % pkg)
    else:
        missing = [b for b in POSED_BONES if b not in names]
        if missing:
            return "missing bones " + ", ".join(missing)
    return ""


def _cls_name(a):
    return str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)


def _path(obj):
    return obj.get_path_name() if obj is not None else ""


def _editor_only_deps(pkg):
    """The packages under /MetaHuman* (editor-only plugins, not in the game) that pkg pulls in."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=False, include_hard_package_references=True,
                                                 include_searchable_names=False, include_soft_management_references=False,
                                                 include_hard_management_references=False)
    seen, todo, bad = set(), [pkg], []
    while todo:
        p = todo.pop()
        if p in seen:
            continue
        seen.add(p)
        if p.startswith("/MetaHuman"):
            bad.append(p)
            continue
        if p.startswith("/Script/") or p.startswith("/Engine/"):
            continue
        for d in reg.get_dependencies(p, opts) or []:
            todo.append(str(d))
    return bad


def _make_two_sided(mat, av, why):
    """A garment's material drawn from both sides: in first person (shed walker, riding) the camera
    looks at the clothes from inside the collar, where single-sided cloth draws nothing (beenster,
    build 137: see-through clothes in the shed and on the bike)."""
    if mat is None:
        return
    path = _path(mat)
    if path.startswith("/MetaHuman"):
        log("%s: %s: material %s is the plugin's; can't make it two-sided" % (av, why, path))
        return
    try:
        if isinstance(mat, unreal.MaterialInstanceConstant):
            ov = mat.get_editor_property("base_property_overrides")
            if ov.get_editor_property("override_two_sided") and ov.get_editor_property("two_sided"):
                return
            ov.set_editor_property("override_two_sided", True)
            ov.set_editor_property("two_sided", True)
            mat.set_editor_property("base_property_overrides", ov)
            unreal.MaterialEditingLibrary.update_material_instance(mat)
        elif isinstance(mat, unreal.Material):
            if mat.get_editor_property("two_sided"):
                return
            mat.set_editor_property("two_sided", True)
            unreal.MaterialEditingLibrary.recompile_material(mat)
        else:
            return
        unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False)
        log("%s: %s: material %s made two-sided" % (av, why, path))
    except Exception as e:
        unreal.log_warning("[Torque] %s: %s: couldn't make %s two-sided: %s" % (av, why, path, e))


def _game_safe_mesh(mesh, av):
    """Drop a skeletal mesh's post-process Anim Blueprint when it needs the MetaHuman plugin (the
    game poses the bones itself), then report anything else of the plugin it still pulls in."""
    pkg = mesh.get_outermost().get_name()
    try:
        abp = mesh.get_editor_property("post_process_anim_blueprint")
    except Exception:
        abp = None
    if abp is not None and _editor_only_deps(abp.get_outermost().get_name()):
        mesh.set_editor_property("post_process_anim_blueprint", None)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
        log("%s: %s: post-process Anim Blueprint %s dropped (needs the MetaHuman plugin)" % (av, pkg, _path(abp)))
    # Its LOD settings asset (only used to regenerate LODs in the editor) and its skeleton, when they
    # are the plugin's: the settings dropped, the skeleton swapped for a project copy.
    changed = False
    try:
        ls = mesh.get_editor_property("lod_settings")
        if ls is not None and ls.get_path_name().startswith("/MetaHuman"):
            mesh.set_editor_property("lod_settings", None)
            changed = True
            log("%s: %s: LOD settings %s dropped" % (av, pkg, ls.get_path_name()))
    except Exception as e:
        log("%s: lod_settings: %s" % (av, e))
    try:
        sk = mesh.get_editor_property("skeleton")
        if sk is not None and sk.get_path_name().startswith("/MetaHuman"):
            copy = ROOT + "/Common/" + sk.get_name()
            eal = unreal.EditorAssetLibrary
            if not eal.does_asset_exist(copy):
                eal.duplicate_asset(sk.get_outermost().get_name(), copy)
            new = unreal.load_asset(copy)
            if new is not None:
                mesh.set_editor_property("skeleton", new)
                changed = True
                log("%s: %s: skeleton %s -> %s" % (av, pkg, sk.get_path_name(), copy))
    except Exception:
        pass   # the skeleton is read-only from Python; the cook takes the plugin's (it's fine)
    if changed:
        unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    # The clothes' and hair's colour parameters, for tinting them to the player's colour later.
    mel = getattr(unreal, "MaterialEditingLibrary", None)
    if mel is not None:
        try:
            for slot in mesh.get_editor_property("materials"):
                m = slot.get_editor_property("material_interface")
                if m is not None and hasattr(mel, "get_vector_parameter_names"):
                    log("%s: %s material %s colours: %s" % (av, pkg.rsplit("/", 1)[-1], m.get_name(),
                        ", ".join(str(n) for n in mel.get_vector_parameter_names(m))))
        except Exception:
            pass
    # Garments (not the body or face) drawn from both sides.
    if "/face/" not in pkg.lower() and "/body/" not in pkg.lower():
        try:
            for slot in mesh.get_editor_property("materials"):
                _make_two_sided(slot.get_editor_property("material_interface"), av, pkg.rsplit("/", 1)[-1])
        except Exception as e:
            log("%s: %s materials: %s" % (av, pkg, e))
    bad = _editor_only_deps(pkg)
    if bad:
        unreal.log_warning("[Torque] %s: %s still pulls in editor-only MetaHuman content: %s" % (av, pkg, ", ".join(sorted(bad)[:5])))


def _get_any(obj, *names):
    for n in names:
        try:
            v = obj.get_editor_property(n)
        except Exception:
            v = getattr(obj, n, None)
        if v is not None:
            return v
    return None


def _cards_lod(groom):
    """The first LOD the groom can draw without strands in every hair group: drawn as cards or
    meshes, visible, and with that card or mesh geometry actually in the asset (a short style built
    at Medium can list cards at a LOD that has none, or hide its cards LODs). -1 if there isn't one.
    Logs the whole LOD table, so a groom that won't show can be read off."""
    def prop(o, *names):
        for n in names:
            try:
                return o.get_editor_property(n)
            except Exception:
                pass
        return None
    try:
        groups = list(groom.get_editor_property("hair_groups_lod"))
    except Exception as e:
        log("groom %s: couldn't read its LODs: %s" % (_path(groom), e))
        return -1
    # Which (group, lod) have card or mesh geometry.
    have = set()
    for field, mesh_names in (("hair_groups_cards", ("imported_mesh", "procedural_mesh", "mesh")),
                              ("hair_groups_meshes", ("imported_mesh", "mesh"))):
        for d in (prop(groom, field) or []):
            gi, li = prop(d, "group_index"), prop(d, "lod_index")
            m = next((v for v in (prop(d, n) for n in mesh_names) if v is not None), None)
            log("groom %s: %s group %s lod %s mesh %s" % (_path(groom).rsplit(".", 1)[-1], field, gi, li, _path(m) if m is not None else "none"))
            if m is not None and gi is not None and li is not None:
                have.add((int(gi), int(li)))
    table = []
    for gi, grp in enumerate(groups):
        row = []
        for li, l in enumerate(prop(grp, "lods") or []):
            row.append((str(prop(l, "geometry_type")).upper(), prop(l, "visible", "b_visible"), prop(l, "screen_size")))
        table.append(row)
    log("groom %s LODs (type, visible, screen size) per group: %s" % (_path(groom).rsplit(".", 1)[-1], table))
    n = max((len(r) for r in table), default=0)
    for i in range(n):
        ok = True
        for gi, r in enumerate(table):
            if i >= len(r):
                ok = False
                break
            typ, vis, _ = r[i]
            if "STRAND" in typ or vis is False or (have and (gi, i) not in have):
                ok = False
                break
        if ok:
            return i
    log("groom %s: no LOD drawn without strands in every group" % _path(groom))
    return -1


HAIR_WORDS = ("hair", "brow", "lash", "beard", "mustache", "moustache")


def _cards_mesh(groom, lod):
    """The groom's card geometry at lod (group 0) as a static mesh path, and the material its cards
    use (by the cards' slot name among the groom's materials), for drawing the hair as a plain mesh."""
    def prop(o, *names):
        for n in names:
            try:
                return o.get_editor_property(n)
            except Exception:
                pass
        return None
    mesh, slot = None, None
    for d in (prop(groom, "hair_groups_cards") or []):
        if prop(d, "group_index") == 0 and prop(d, "lod_index") == lod:
            mesh = next((v for v in (prop(d, n) for n in ("imported_mesh", "procedural_mesh", "mesh")) if v is not None), None)
            slot = prop(d, "material_slot_name")
            break
    mat = None
    mats = list(prop(groom, "hair_groups_materials") or [])
    for m in mats:
        if slot is not None and str(prop(m, "slot_name")) == str(slot):
            mat = prop(m, "material")
    if mat is None and mats:
        mat = prop(mats[0], "material")
    # The cards' textures (coverage, tangent, depth...): without them the cards draw nothing.
    atlas, atlas_any = None, None
    for d in (prop(groom, "hair_groups_cards") or []):
        tex = prop(d, "textures")
        names = []
        if tex is not None:
            for f in _fields(tex):
                try:
                    v = tex.get_editor_property(f)
                except Exception:
                    continue
                for t in (v if isinstance(v, (list, tuple, unreal.Array)) else [v]):
                    if isinstance(t, unreal.Texture):
                        names.append("%s=%s" % (f, _path(t)))
                        atlas_any = atlas_any or t
                        if "attribute" in _path(t).lower() and (atlas is None or (prop(d, "group_index") == 0 and prop(d, "lod_index") == lod)):
                            atlas = t
                    elif t is not None and not isinstance(t, (bool, int, float, str)):
                        names.append("%s=%s" % (f, t))
        log("groom %s: cards group %s lod %s textures: %s" % (_path(groom).rsplit(".", 1)[-1], prop(d, "group_index"), prop(d, "lod_index"),
            "; ".join(names) or "NONE"))
    bad = _editor_only_deps(groom.get_outermost().get_name())
    if bad:
        log("groom %s pulls in plugin content: %s" % (_path(groom).rsplit(".", 1)[-1], ", ".join(sorted(bad)[:12])))
    log("groom %s: cards at LOD %d = %s, slot %s, material %s (materials: %s)" % (_path(groom).rsplit(".", 1)[-1], lod, _path(mesh),
        slot, _path(mat), ", ".join("%s=%s" % (prop(m, "slot_name"), _path(prop(m, "material"))) for m in mats)))
    return _path(mesh), _path(mat), _path(atlas or atlas_any)


HAIR_CARDS_MATERIAL = ROOT + "/Common/M_HairCards"   # the game's own card hair material (the plugin's is editor-only)
_hair_cards_made = {}   # made once per run




def _hair_cards_material(atlas_path):
    """M_HairCards: masked, two-sided, its opacity one channel of a cards atlas (parameter Atlas,
    channel picked by the vector parameter Mask, default G), its colour the parameter Colour. The
    game draws each groom's cards mesh with it, instead of the groom (round 129: grooms are bald close up)."""
    if "path" in _hair_cards_made:
        return _hair_cards_made["path"]
    atlas = unreal.load_asset(atlas_path) if atlas_path else None
    if atlas is None:
        log("hair cards material: no atlas texture; not made")
        return ""
    mel = unreal.MaterialEditingLibrary
    folder, name = HAIR_CARDS_MATERIAL.rsplit("/", 1)
    if unreal.EditorAssetLibrary.does_asset_exist(HAIR_CARDS_MATERIAL):
        unreal.EditorAssetLibrary.delete_asset(HAIR_CARDS_MATERIAL)
    m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("opacity_mask_clip_value", 0.33)
    tex = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -800, 0)
    tex.set_editor_property("parameter_name", "Atlas")
    tex.set_editor_property("texture", atlas)
    comp = str(atlas.get_editor_property("compression_settings")).upper()
    srgb = bool(atlas.get_editor_property("srgb"))
    st = unreal.MaterialSamplerType
    sampler = (st.SAMPLERTYPE_MASKS if "MASK" in comp else st.SAMPLERTYPE_NORMAL if "NORMAL" in comp
               else (st.SAMPLERTYPE_GRAYSCALE if srgb else st.SAMPLERTYPE_LINEAR_GRAYSCALE) if "GRAYSCALE" in comp
               else st.SAMPLERTYPE_COLOR if srgb else st.SAMPLERTYPE_LINEAR_COLOR)
    tex.set_editor_property("sampler_type", sampler)
    mask = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -800, 300)
    mask.set_editor_property("parameter_name", "Mask")
    mask.set_editor_property("default_value", unreal.LinearColor(0.0, 1.0, 0.0, 0.0))
    ta = mel.create_material_expression(m, unreal.MaterialExpressionAppendVector, -500, 0)
    ma = mel.create_material_expression(m, unreal.MaterialExpressionAppendVector, -500, 300)
    dot = mel.create_material_expression(m, unreal.MaterialExpressionDotProduct, -300, 100)
    colour = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -500, -300)
    colour.set_editor_property("parameter_name", "Colour")
    colour.set_editor_property("default_value", unreal.LinearColor(0.045, 0.028, 0.016, 1.0))
    rough = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -300, -150)
    rough.set_editor_property("r", 0.55)
    spec = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -300, -100)
    spec.set_editor_property("r", 0.3)
    ok = [mel.connect_material_expressions(tex, "RGB", ta, "A"), mel.connect_material_expressions(tex, "A", ta, "B"),
          mel.connect_material_expressions(mask, "", ma, "A"), mel.connect_material_expressions(mask, "A", ma, "B"),
          mel.connect_material_expressions(ta, "", dot, "A"), mel.connect_material_expressions(ma, "", dot, "B"),
          mel.connect_material_property(dot, "", unreal.MaterialProperty.MP_OPACITY_MASK),
          mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR),
          mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS),
          mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)]
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    log("hair cards material %s: atlas %s (compression %s, sRGB %s, sampler %s); links %s" % (HAIR_CARDS_MATERIAL, atlas_path, comp, srgb,
        sampler, "all made" if all(ok) else "FAILED at %s" % [i for i, v in enumerate(ok) if not v]))
    _hair_cards_made["path"] = _path(m)
    return _path(m)


PLUGIN_COPIES = ROOT + "/Common/FromPlugin"   # copies of the MetaHuman plugin's content the avatars need


def _deps_closure(pkg):
    """Every package under /MetaHuman* that pkg needs, through hard references, following on
    through those packages too (never through /Game, /Engine or /Script)."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=False, include_hard_package_references=True,
                                                 include_searchable_names=False, include_soft_management_references=False,
                                                 include_hard_management_references=False)
    seen, todo, out = set(), [pkg], []
    while todo:
        p = todo.pop()
        for d in reg.get_dependencies(p, opts) or []:
            d = str(d)
            if d in seen or not d.startswith("/MetaHuman"):
                continue
            seen.add(d)
            out.append(d)
            todo.append(d)
    return out


# Properties that hold the references, by name: Python's dir() doesn't list every editor property
# of a groom or its structs, so these are tried as well as whatever dir() gives.
REF_FIELDS = ("hair_groups_materials", "hair_groups_cards", "hair_groups_meshes", "hair_groups_rendering",
              "hair_groups_lod", "hair_groups_physics", "hair_groups_interpolation", "hair_groups_info",
              "material", "materials", "textures", "texture", "imported_mesh", "procedural_mesh", "mesh",
              "parent", "texture_parameter_values", "parameter_value", "runtime_virtual_texture_parameter_values",
              "layout", "cards", "meshes")


def _fields(value):
    return list(dict.fromkeys([m for m in dir(value) if not m.startswith("_")] + list(REF_FIELDS)))


def _swap_refs(value, mapping):
    """value with every object reference whose package is in mapping swapped for its copy, walking
    into arrays and structs. Returns (value, changed)."""
    if isinstance(value, unreal.Object):
        pkg = value.get_outermost().get_name()
        if pkg in mapping:
            new = unreal.load_asset(mapping[pkg])
            if new is not None:
                return new, True
        return value, False
    if isinstance(value, (unreal.Array, list, tuple)):
        items, changed = [], False
        for v in value:
            nv, ch = _swap_refs(v, mapping)
            items.append(nv)
            changed = changed or ch
        return (items if changed else value), changed
    if isinstance(value, unreal.StructBase):
        changed = False
        for f in _fields(value):
            try:
                v = value.get_editor_property(f)
            except Exception:
                continue
            nv, ch = _swap_refs(v, mapping)
            if ch:
                try:
                    value.set_editor_property(f, nv)
                    changed = True
                except Exception as e:
                    log("couldn't repoint %s.%s: %s" % (type(value).__name__, f, e))
        return value, changed
    return value, False


def _repoint(obj, mapping):
    """Swap obj's references to plugin content for the copies (each editable property); a material
    instance's parent too. Returns the properties changed."""
    done, read = [], []
    for f in _fields(obj):
        try:
            v = obj.get_editor_property(f)
        except Exception:
            continue
        if f in REF_FIELDS:
            read.append(f)
        nv, ch = _swap_refs(v, mapping)
        if ch:
            try:
                obj.set_editor_property(f, nv)
                done.append(f)
            except Exception as e:
                if f == "parent" and hasattr(unreal, "MaterialEditingLibrary"):
                    try:
                        unreal.MaterialEditingLibrary.set_material_instance_parent(obj, nv)
                        done.append(f)
                        continue
                    except Exception as e2:
                        e = e2
                log("couldn't repoint %s.%s: %s" % (obj.get_name(), f, e))
    if done:
        unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False)
    if isinstance(obj, unreal.Object) and "groom" in type(obj).__name__.lower():
        log("%s: reference properties read: %s; repointed: %s" % (obj.get_name(), ", ".join(read) or "none", ", ".join(done) or "none"))
    return done


def _own_plugin_content(asset, av):
    """The MetaHuman plugin is editor-only, so nothing of its content reaches the game: a groom's
    materials and card atlases from it draw as nothing. Copy what asset needs from it into
    PLUGIN_COPIES (once, shared by the avatars) and point asset and the copies at the copies."""
    pkg = asset.get_outermost().get_name()
    deps = _deps_closure(pkg)
    if not deps:
        return {}
    eal = unreal.EditorAssetLibrary
    mapping = {}
    for d in deps:
        copy = PLUGIN_COPIES + d   # e.g. .../FromPlugin/MetaHumanCharacter/Materials/MI_Hair
        if not eal.does_asset_exist(copy):
            if not eal.duplicate_asset(d, copy):
                unreal.log_warning("[Torque] %s: couldn't copy %s to %s" % (av, d, copy))
                continue
        mapping[d] = copy
    changed = _repoint(asset, mapping)
    for d, copy in mapping.items():
        c = unreal.load_asset(copy)
        if c is not None:
            _repoint(c, mapping)
    left = _editor_only_deps(pkg)
    log("%s: %s: %d plugin assets copied to %s; repointed %s; still from the plugin: %s" % (av, pkg.rsplit("/", 1)[-1], len(mapping),
        PLUGIN_COPIES, ", ".join(changed) or "nothing", ", ".join(sorted(left)[:8]) or "none"))
    return mapping


def _hair_from_build(entry, av):
    """The hair the build made under /Game/MetaHumans/<Name>/, for when the Blueprint's groom
    components read empty (UE 5.8 fills them at run time): each GroomAsset with the GroomBindingAsset
    made for it, on the face; and when there's no groom for a group, its hair-card static mesh,
    which the game puts on the head bone. Every groom, binding and static mesh found is logged."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    folder = ROOT + "/" + av
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        return
    grooms, bindings, statics = [], [], []
    for a in reg.get_assets_by_path(folder, recursive=True):
        cls, obj = _cls_name(a), "%s.%s" % (a.package_name, a.asset_name)
        if cls == "GroomAsset":
            grooms.append(obj)
        elif cls == "GroomBindingAsset":
            bindings.append(obj)
        elif cls == "StaticMesh":
            statics.append(obj)
    log("%s: in the build: grooms %s; bindings %s; static meshes %s" % (av, grooms or "none", bindings or "none", statics or "none"))

    def group(path):
        low = path.lower()
        for w in HAIR_WORDS:
            if w in low:
                return {"moustache": "mustache"}.get(w, w)
        return None

    done = set()
    for g in grooms:
        if "fuzz" in g.lower():
            continue
        ga = unreal.load_asset(g.split(".")[0])
        if ga is None:
            continue
        copies = _own_plugin_content(ga, av) or {}
        # The groom's materials by slot, as the copies, for the game to put on the component too
        # (in case the asset itself still points at the plugin's).
        groom_mats = []
        try:
            for m in ga.get_editor_property("hair_groups_materials"):
                mat = m.get_editor_property("material")
                mp = mat.get_outermost().get_name() if mat is not None else ""
                groom_mats.append(("%s.%s" % (copies[mp], mat.get_name())) if mp in copies else _path(mat))
        except Exception as e:
            log("%s: couldn't read %s's materials: %s" % (av, g, e))
        log("%s: %s materials for the game: %s" % (av, g.rsplit(".", 1)[-1], groom_mats or "none"))
        bind = ""
        for b in bindings:
            ba = unreal.load_asset(b.split(".")[0])
            target = _get_any(ba, "groom", "groom_asset") if ba is not None else None
            if target is not None and _path(target) == _path(ga):
                bind = b
                break
        if not bind:
            stem = g.rsplit(".", 1)[-1].lower()
            bind = next((b for b in bindings if stem.replace("groom_", "") in b.lower()), "")
        name = (group(g) or g.rsplit(".", 1)[-1]).capitalize()
        lod = _cards_lod(ga)
        cards, cards_mat, atlas = _cards_mesh(ga, lod) if lod >= 0 else ("", "", "")
        if (not cards or not atlas) and "hair" in (group(g) or g).lower():
            # Round 142: Avatar_A's groom gave no cards LOD, so A went bald. The build's own cards mesh
            # (lowest LOD of group 0) and atlas, found by name, as the game's fallback does.
            def lod_of(p):
                try:
                    return int(p.rsplit("_LOD", 1)[1].split(".")[0])
                except Exception:
                    return 99
            cm = sorted((m for m in statics if "CardsMesh_Group0_LOD" in m and m.rsplit("/", 1)[-1].startswith("Hair_")), key=lod_of)
            tex = [("%s.%s" % (a.package_name, a.asset_name)) for a in reg.get_assets_by_path(folder, recursive=True)
                   if _cls_name(a) == "Texture2D" and str(a.asset_name).startswith("Hair_") and "CardsAtlas_Attribute" in str(a.asset_name)]
            cards = cards or (cm[0] if cm else "")
            atlas = atlas or (tex[0] if tex else "")
            log("%s: no cards from the groom's LOD table; by name: mesh %s, atlas %s" % (av, cards or "none", atlas or "none"))
        # The game draws the cards mesh with its own M_HairCards (the plugin's MI_Hair is editor-only).
        hair_mat = _hair_cards_material(atlas) if cards and atlas else ""
        colour = next((a.get("hair_colour") for a in AVATARS if a["name"] == av), None) or [0.045, 0.028, 0.016]
        entry["parts"].append({"name": name, "groom": g, "binding": bind, "attach": "Face", "lod": lod, "materials": groom_mats,
                               "cards_mesh": cards, "cards_materials": [hair_mat] if hair_mat else ([cards_mat] if cards_mat else []),
                               "cards_texture": atlas, "hair_colour": colour})
        log("%s: groom %s from the build = %s (binding %s, cards from LOD %d)" % (av, name, g, bind or "none", lod))
        done.add(group(g))
    # Hair cards as static meshes, one per group not covered by a groom: the lowest LOD of each.
    by_group = {}
    for m in sorted(statics):
        grp = group(m)
        if grp is None or grp in done or "fuzz" in m.lower():
            continue
        by_group.setdefault(grp, []).append(m)
    for grp, ms in by_group.items():
        cards = [m for m in ms if "card" in m.lower()] or ms
        pick = cards[0]
        entry["parts"].append({"name": grp.capitalize() + "Cards", "static": pick, "attach": "Face", "bone": "head", "materials": []})
        log("%s: %s as a static mesh %s (of %s)" % (av, grp, pick, ", ".join(ms)))
        bad = _editor_only_deps(pick.split(".")[0])
        if bad:
            unreal.log_warning("[Torque] %s: %s pulls in editor-only content: %s" % (av, pick, ", ".join(bad[:5])))


def _add_clothes(entry, av, skins):
    """The clothes the build made under /Game/MetaHumans/<Name>/: skeletal meshes that aren't the
    body or face (posed by the body), else the Chaos outfit or cloth assets (a cloth component posed
    by the body). The Blueprint has no component of its own for them in UE 5.8."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    folder = ROOT + "/" + av
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        return
    used = {_path(m) for c, m in skins}
    meshes, outfits, cloths = [], [], []
    for a in reg.get_assets_by_path(folder, recursive=True):
        cls, pkg, name = _cls_name(a), str(a.package_name), str(a.asset_name)
        obj = "%s.%s" % (pkg, name)
        low = pkg.lower()
        if cls == "SkeletalMesh" and obj not in used and "/face/" not in low and "/body/" not in low:
            meshes.append(obj)
        elif cls == "ChaosOutfitAsset":
            outfits.append(obj)
        elif cls == "ChaosClothAsset":
            cloths.append(obj)
        elif any(w in low for w in ("cloth", "outfit", "garment")):
            log("%s: clothing asset %s (%s) not used" % (av, obj, cls))
    if meshes:
        for m in meshes:
            entry["parts"].append({"name": "Clothes_" + m.rsplit(".", 1)[-1], "mesh": m, "materials": []})
            mesh = unreal.load_asset(m.split(".")[0])
            if mesh is not None:
                _game_safe_mesh(mesh, av)
    else:
        for c in (outfits or cloths):
            entry["parts"].append({"name": "Clothes_" + c.rsplit(".", 1)[-1], "cloth": c, "materials": []})
            bad = _editor_only_deps(c.split(".")[0])
            if bad:
                unreal.log_warning("[Torque] %s: clothes %s pull in editor-only content: %s" % (av, c, ", ".join(bad[:5])))
    log("%s: clothes: %d meshes, %d outfits, %d cloth assets found; %s used" % (av, len(meshes), len(outfits), len(cloths),
        "the meshes" if meshes else ("the outfits" if outfits else ("the cloth assets" if cloths else "none"))))


def _parts_of_blueprint(pkg, name, av):
    """The avatar entry for an assembled MetaHuman Blueprint: its body mesh (the one the game poses)
    and every other skeletal mesh and groom, with the components' material overrides and what each
    groom is attached to. Read from an instance spawned in the editor world for a moment."""
    cls = unreal.load_class(None, "%s.%s_C" % (pkg, name)) if hasattr(unreal, "load_class") else None
    if cls is None:
        bp = unreal.load_asset(pkg)
        cls = bp.generated_class() if bp is not None and hasattr(bp, "generated_class") else None
    if cls is None:
        unreal.log_warning("[Torque] %s: couldn't load %s" % (av, pkg))
        return None
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    try:
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(0, 0, -100000), unreal.Rotator(0, 0, 0), transient=True)
    except TypeError:
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(0, 0, -100000))
    if actor is None:
        unreal.log_warning("[Torque] %s: couldn't spawn %s in the editor world" % (av, pkg))
        return None
    try:
        try:
            log("%s: Blueprint components: %s" % (av, ", ".join("%s (%s)" % (c.get_name(), type(c).__name__)
                                                              for c in actor.get_components_by_class(unreal.ActorComponent))))
        except Exception:
            pass
        skins = []
        for c in actor.get_components_by_class(unreal.SkeletalMeshComponent):
            try:
                m = c.get_skeletal_mesh_asset() if hasattr(c, "get_skeletal_mesh_asset") else c.get_editor_property("skeletal_mesh")
            except Exception:
                m = None
            if m is not None:
                skins.append((c, m))
        if not skins:
            unreal.log_warning("[Torque] %s: %s has no skeletal meshes" % (av, pkg))
            return None
        # The body by its mesh's path first: UE 5.8's rebuilt Blueprints name every component
        # SkeletalMesh, SkeletalMesh1... (round 142 took a garment for the body: headless, armless riders).
        body = next(((c, m) for c, m in skins if "/body/" in _path(m).lower() or "bodymesh" in _path(m).lower()), None) \
            or next(((c, m) for c, m in skins if c.get_name().lower() == "body"), None) \
            or next(((c, m) for c, m in skins if "body" in c.get_name().lower()), None) or skins[0]
        log("%s: body mesh %s (component %s)" % (av, _path(body[1]), body[0].get_name()))

        def mats(c):
            try:
                out = [_path(m) for m in (c.get_editor_property("override_materials") or [])]
            except Exception:
                return []
            for m in out:
                if m and _editor_only_deps(m.split(".")[0]):
                    unreal.log_warning("[Torque] %s: %s's material %s needs editor-only MetaHuman content" % (av, c.get_name(), m))
            return out
        entry = {"name": av, "blueprint": "", "body": _path(body[1]), "body_materials": mats(body[0]), "parts": [],
                 "height_cm": next((a.get("height", 0.0) for a in AVATARS if a["name"] == av), 0.0)}
        _game_safe_mesh(body[1], av)
        for c, m in skins:
            if c == body[0]:
                continue
            pname = c.get_name()
            if "/face/" in _path(m).lower() or "facemesh" in _path(m).lower():
                pname = "Face"   # (UE 5.8's rebuilt Blueprints name every component SkeletalMesh, SkeletalMesh1...)
            entry["parts"].append({"name": pname, "mesh": _path(m), "materials": mats(c)})
            _game_safe_mesh(m, av)
            if "face" not in c.get_name().lower():
                for mp in entry["parts"][-1]["materials"]:
                    if mp:
                        _make_two_sided(unreal.load_asset(mp.split(".")[0]), av, c.get_name())
        # The grooms (hair, brows, lashes, beard): any component whose class says Groom. The
        # peach fuzz is strands only and too fine to see at shed distances, so it's left out.
        for g in actor.get_components_by_class(unreal.ActorComponent):
            if "groom" not in type(g).__name__.lower():
                continue
            gname = g.get_name()
            if "fuzz" in gname.lower():
                log("%s: groom %s left out (peach fuzz)" % (av, gname))
                continue
            groom = _get_any(g, "groom_asset", "GroomAsset")
            binding = _get_any(g, "binding_asset", "BindingAsset")
            if groom is None:
                log("%s: groom component %s has no groom asset (fields: %s)" % (av, gname, _props(g)[:300]))
                continue
            parent = g.get_attach_parent()
            lod = _cards_lod(groom)
            entry["parts"].append({"name": gname, "groom": _path(groom), "binding": _path(binding),
                                   "attach": parent.get_name() if parent is not None else "", "lod": lod, "materials": mats(g)})
            log("%s: groom %s = %s (binding %s, on %s, cards from LOD %d)" % (av, gname, _path(groom), _path(binding),
                parent.get_name() if parent is not None else "-", lod))
            bad = _editor_only_deps(groom.get_outermost().get_name())
            if bad:
                unreal.log_warning("[Torque] %s: groom %s pulls in editor-only content: %s" % (av, _path(groom), ", ".join(bad[:5])))
        if not any(p.get("groom") for p in entry["parts"]):
            _hair_from_build(entry, av)
        _add_clothes(entry, av, skins)
        _fill_cards(entry, av)
        log("avatar %s: body %s, parts %s" % (av, entry["body"], ", ".join(p["name"] for p in entry["parts"])))
        return entry
    finally:
        actors.destroy_actor(actor)


def _fill_cards(entry, av):
    """Every hair groom part gets what the game draws it with: its cards mesh, atlas, M_HairCards and
    the avatar's hair colour (round 143: grooms read from the Blueprint came without them, so B to D
    would have gone bald). From the groom's LOD table, else by name in the avatar's build folder."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    folder = ROOT + "/" + av
    colour = next((a.get("hair_colour") for a in AVATARS if a["name"] == av), None) or [0.045, 0.028, 0.016]
    for p in entry.get("parts", []):
        g = p.get("groom", "")
        if not g or p.get("cards_mesh") or "hair" not in (p.get("name", "") + g.rsplit("/", 1)[-1]).lower():
            continue
        if any(w in g.lower() for w in ("brow", "lash", "beard", "mustache", "fuzz")):
            continue
        ga = unreal.load_asset(g.split(".")[0])
        lod = p.get("lod", -1)
        cards, cards_mat, atlas = _cards_mesh(ga, lod) if ga is not None and lod is not None and lod >= 0 else ("", "", "")
        if not cards or not atlas:
            assets = list(reg.get_assets_by_path(folder, recursive=True))
            def lod_of(n):
                try:
                    return int(n.rsplit("_LOD", 1)[1])
                except Exception:
                    return 99
            cm = sorted((str(a.asset_name) for a in assets if _cls_name(a) == "StaticMesh" and str(a.asset_name).startswith("Hair_")
                         and "CardsMesh_Group0_LOD" in str(a.asset_name)), key=lod_of)
            paths = {str(a.asset_name): "%s.%s" % (a.package_name, a.asset_name) for a in assets}
            tex = [paths[n] for n in paths if n.startswith("Hair_") and "CardsAtlas_Attribute" in n]
            cards = cards or (paths[cm[0]] if cm else "")
            atlas = atlas or (tex[0] if tex else "")
        mat = _hair_cards_material(atlas) if cards and atlas else ""
        p["cards_mesh"] = cards
        p["cards_texture"] = atlas
        p["cards_materials"] = [mat] if mat else ([cards_mat] if cards_mat else [])
        p["hair_colour"] = colour
        log("%s: hair %s drawn as cards: mesh %s, atlas %s, material %s" % (av, p.get("name"), cards or "NONE", atlas or "NONE", mat or cards_mat or "NONE"))


# Stopgap (round 123): only Avatar_D's groom draws in game, whatever the others' LOD, material or
# binding, so the others wear D's hair, rigid on their head, until theirs are sorted. Set to None to
# give each its own again. Round 131: hair drawn as cards meshes works on all four, so each wears its own.
SHARE_HAIR_FROM = None


def _share_hair(by_name):
    src = by_name.get(SHARE_HAIR_FROM) if SHARE_HAIR_FROM else None
    if not src:
        return
    face = next((p.get("mesh") for p in src.get("parts", []) if p.get("name") == "Face" and p.get("mesh")), "")
    hair = next((p for p in src.get("parts", []) if p.get("groom") and p.get("name", "").lower() == "hair"), None)
    if hair is None or not face:
        log("hair sharing: %s has no Hair groom or Face mesh; nothing shared" % SHARE_HAIR_FROM)
        return
    for name, e in by_name.items():
        if name == SHARE_HAIR_FROM:
            continue
        for p in e.get("parts", []):
            if p.get("groom") and p.get("name", "").lower() == "hair":
                p["name"] = "OwnHair"
                p["loose_only"] = True    # kept for the game's -TorqueHairAlone test only
        shared = dict(hair)
        shared["binding"] = ""          # D's binding is made for D's face
        shared["fit_mesh"] = face       # the groom sits as it does on D's head, moved onto this one's
        shared["hair_colour"] = next((a.get("hair_colour") for a in AVATARS if a["name"] == name), None) or hair.get("hair_colour")
        e["parts"].append(shared)
        log("hair sharing: %s wears %s's %s (stopgap)" % (name, SHARE_HAIR_FROM, hair["groom"]))


def register():
    """Data/Avatars/avatars.json from the assembled MetaHumans under /Game/MetaHumans/<Name>/: each
    Blueprint named BP_* with its body mesh, or a complete body mesh on its own. The never-cook block
    is lifted only when at least one real avatar is listed."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = reg.get_assets_by_path(ROOT, recursive=True) if unreal.EditorAssetLibrary.does_directory_exist(ROOT) else []
    blueprints, meshes = [], []
    if unreal.EditorAssetLibrary.does_directory_exist(SOURCES):   # Blueprints moved there by an earlier register
        for a in reg.get_assets_by_path(SOURCES, recursive=True):
            cls = str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)
            if cls == "Blueprint" and str(a.asset_name).startswith("BP_"):
                blueprints.append((str(a.package_name), str(a.asset_name)))
    for a in assets:
        cls = str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)
        pkg = str(a.package_name); name = str(a.asset_name)
        if cls == "Blueprint" and name.startswith("BP_"):
            blueprints.append((pkg, name))
        elif cls == "SkeletalMesh":
            meshes.append((pkg, name))
        elif cls == "MetaHumanCharacter":
            # A character source made by hand in here: move it to SOURCES, which is never cooked
            # (it references the plugin's build pipeline, so cooking it fails or bloats the package).
            dest = SOURCES + pkg[len(ROOT):]
            ok = unreal.EditorAssetLibrary.rename_asset(pkg, dest)
            log("character source %s %s %s" % (pkg, "moved to" if ok else "COULDN'T be moved to", dest))
    # Clear redirectors the moves left behind (a cooked redirector would pull in the source).
    redirs = [unreal.load_asset(str(a.package_name)) for a in reg.get_assets_by_path(ROOT, recursive=True)
              if (str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)) == "ObjectRedirector"]
    redirs = [r for r in redirs if r is not None]
    if redirs:
        unreal.AssetToolsHelpers.get_asset_tools().fixup_referencers(redirs)
        log("fixed up %d redirectors under %s" % (len(redirs), ROOT))
    # Only real avatars: each in its own folder /Game/MetaHumans/<Name>/..., named after that folder.
    # A Blueprint BP_* there is one; with no Blueprint, a body mesh counts only if it's complete
    # (skeleton, the mannequin bones the game poses, and materials of its own in /Game). Anything
    # else (a stray export, a loose mesh in /Game/MetaHumans itself) is skipped and lifts nothing.
    def avatar_of(pkg):
        base = SOURCES if pkg.startswith(SOURCES + "/") else ROOT
        rest = pkg[len(base) + 1:].split("/")
        return rest[0] if len(rest) >= 2 else None
    by_name = {}
    for pkg, name in sorted(blueprints):
        av = avatar_of(pkg)
        if av is None:
            unreal.log_warning("[Torque] skipping %s: not in an avatar folder %s/<Name>/" % (pkg, ROOT))
            continue
        if av in by_name:
            continue
        entry = _parts_of_blueprint(pkg, name, av)
        if entry is None:
            continue
        by_name[av] = entry
        # The Blueprint itself isn't used by the game (its Anim Blueprints need the editor-only
        # MetaHuman plugin): keep it with the sources, which are never cooked.
        if pkg.startswith(ROOT + "/"):
            dest = SOURCES + pkg[len(ROOT):]
            ok = unreal.EditorAssetLibrary.rename_asset(pkg, dest)
            log("%s: Blueprint %s %s %s" % (av, pkg, "moved to" if ok else "COULDN'T be moved to", dest))
    for pkg, name in sorted(meshes, key=lambda m: "full" not in m[1].lower()):   # a full body (with head) first
        av = avatar_of(pkg)
        if av in by_name or "body" not in name.lower() or "face" in name.lower():
            continue
        if av is None:
            unreal.log_warning("[Torque] skipping %s: a loose mesh, not an avatar" % pkg)
            continue
        why = _incomplete_body(pkg)
        if why:
            unreal.log_warning("[Torque] skipping body %s: %s" % (pkg, why))
            continue
        by_name[av] = {"name": av, "blueprint": "", "body": "%s.%s" % (pkg, name),
                       "height_cm": next((a.get("height", 0.0) for a in AVATARS if a["name"] == av), 0.0)}
        log("avatar %s: body only %s" % (av, by_name[av]["body"]))
    _share_hair(by_name)
    out = [by_name[k] for k in sorted(by_name)]
    path = os.path.join(project_dir(), "Data", "Avatars", "avatars.json")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if not out:
        unreal.log_warning("[Torque] no assembled MetaHumans (BP_*) under %s; players keep the shape body" % ROOT)
        if os.path.exists(path):
            os.remove(path)
        set_never_cook(True)
        return
    with open(path, "w") as f:
        json.dump({"about": "Co-op avatars: assembled MetaHumans, one per player colour in this order "
                            "(Tools/ue_metahumans.py writes this; Source/TorqueShedProto/Shed/TorqueAvatar.* reads it).",
                   "avatars": out}, f, indent=1)
    log("%d avatars written to %s (%s). Commit it: it's tracked, so a reset or pull on the PC puts the old copy back"
        % (len(out), path, "; ".join("%s: %d parts" % (e["name"], len(e.get("parts", []))) for e in out)))
    set_never_cook(False)


def main(args):
    mode = args[0] if args else "register"
    if mode == "discover":
        discover()
    elif mode == "wardrobe":
        _wardrobe()
    elif mode == "slim":
        slim()
    elif mode == "fab":
        fab_wardrobe()
        _wardrobe()
    elif mode == "make":
        rest = args[1:]
        global NO_FAB
        NO_FAB = "nofab" in rest
        if not NO_FAB:
            fab_wardrobe()
        only = [a for a in rest if a not in ("rig", "nowardrobe", "nofab")]   # e.g. `make rig A B`: just those
        make(rig="rig" in rest, wardrobe="nowardrobe" not in rest, only=only or None)
        register()
    else:
        register()


main([a for a in sys.argv[1:] if not a.startswith("-")])
