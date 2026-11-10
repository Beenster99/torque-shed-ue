"""Put the Dales round the shed's pad (Data/Shed/Dales, from Tools/make_shed_dales.py) in the shed map.
Called by Tools/ue_build_shed.py outside() while it builds /Game/Shed/Shed; not run on its own.

It borrows the Buttertubs level builder's materials and import helpers (Tools/ue_build_level.py, loaded
as a library) so the dale looks like the ride: the scanned moorland (M_Land, which reads the cover from
the vertex colour), stone with moss and lichen for the walls, scars and barns, fleece for the sheep, the
beck's water and the trees. They are made afresh into /Game/Shed/Dales/Materials, so rebuilding the
ride (which deletes and remakes its own) never leaves the shed pointing at nothing. The weather still
reaches them: they read the ride's MPC_Weather, which the game drives in every map.

  - the land tiles and the beck and the lane as plain meshes at the origin, in the importer's frame
    (worked out from a far tile, as the ride does from its road); the land is Nanite, none of it has
    collision (the bikes stay on the walled pad, Tools/shed_yard.py);
  - walls, gates, scars, rocks, barns and sheep as Nanite instances (props.json), with the ride's
    swaps (Data/Buttertubs/prop_swaps.json: the Megascans scree);
  - the trees as instances of the ride's tree models (/Game/Buttertubs/Trees, imported by
    Tools/ue_build_level.py). If they aren't there yet the dale goes without trees, and says so.

TORQUE_DALES=off (in the environment) leaves it all out and brings the flat field back.
"""
import json
import os
import sys

import unreal

DEST = "/Game/Shed/Dales"
MAT_DIR = DEST + "/Materials"
PROP_DEST = DEST + "/Props"


def log(msg):
    unreal.log("[Torque] dales: " + msg)


def data_dir():
    return os.path.join(os.path.abspath(unreal.Paths.project_dir()), "Data", "Shed", "Dales")


def wanted():
    return os.environ.get("TORQUE_DALES", "on").lower() != "off" and os.path.exists(os.path.join(data_dir(), "dales.json"))


def level_lib():
    """Tools/ue_build_level.py as a module, without it building the ride."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    sys.torque_build_level_as_library = True
    try:
        import importlib
        if "ue_build_level" in sys.modules:
            L = importlib.reload(sys.modules["ue_build_level"])
        else:
            import ue_build_level as L
    finally:
        sys.torque_build_level_as_library = False
    return L


def no_collision(mesh):
    try:
        unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).remove_collisions(mesh)
    except Exception:
        pass


def nanite_usage(L, m):
    """Nanite meshes need the material to say so for a packaged build."""
    L.instanced_usage(m)
    unreal.MaterialEditingLibrary.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)


def tree_meshes(folder, names):
    """The ride's tree meshes by their glb names. The importer doesn't always name the asset after the
    file (it can put it in a subfolder, or give it a prefix), so look through the folder as
    ue_build_level.import_glbs does: an exact name first, else the shortest name that ends with it,
    else the shortest that contains it."""
    paths = unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False)
    by = {}
    for p in paths:
        base = str(p).rsplit("/", 1)[-1].split(".")[0]
        by.setdefault(base, []).append(str(p))
    out = {}
    for name in names:
        cands = by.get(name) or sorted((p for b, ps in by.items() if b.endswith(name) for p in ps), key=len) \
            or sorted((p for b, ps in by.items() if name in b and ("Far" in b) == ("Far" in name)
                       and not b[b.index(name) + len(name):][:1].isdigit() for p in ps), key=len)
        for p in cands:
            a = unreal.load_asset(p)
            if isinstance(a, unreal.StaticMesh):
                out[name] = a
                break
    log("tree models: %d of %d found under %s (%d assets there)" % (len(out), len(names), folder, len(paths)))
    if len(out) < len(names):
        log("not found: %s; some there: %s" % (", ".join(n for n in names if n not in out)[:300],
                                               ", ".join(sorted(by)[:12])))
    return out


def place(tarmac):
    """Build it all into the open level. tarmac: the shed's lane material, for the lane."""
    folder = data_dir()
    spec = json.load(open(os.path.join(folder, "dales.json")))
    L = level_lib()
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    make = L.new_material
    L.new_material = lambda name, folder=None: make(name, MAT_DIR)   # our own copies, not the ride's
    try:
        tex, roles = L.import_textures()
        land = L.land_material_scanned(tex, roles) if "grass" in tex else L.land_material()
        nanite_usage(L, land)
        mats = {"stone": L.stone_material(tex, roles),
                "fleece": L.flat_material("M_Fleece", 0.95, fine=0.4, wpo=L.graze_wpo),
                "metal": L.flat_material("M_GalvanisedSteel", 0.42, metallic=1.0),
                "wood": L.flat_material("M_BarnWood", 0.8, fine=0.15),
                "masonry": L.stone_material(tex, roles, "M_Masonry", "masonry", "masonry", broad_moss=False),
                "glass": L.flat_material("M_WindowGlass", 0.06),
                "render": L.render_material(tex, roles),
                "paint": L.flat_material("M_Paint", 0.32, fine=0.05)}
        water = L.beck_material()
        tree = L.tree_material(tex, roles)
    finally:
        L.new_material = make

    # The land, the beck and the lane.
    meshes = L.import_glbs(folder, DEST)
    probe = spec.get("frame_probe", "land_f_00_00")
    if probe not in meshes:
        raise RuntimeError("the dale's land didn't import (no %s); see the Output Log" % probe)
    sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for name, mesh in meshes.items():
        # Distance fields (Lumen's) for kilometre-wide tiles are big and buy little out there: a
        # coarse one near the pad, none further off.
        try:
            bs = sme.get_lod_build_settings(mesh, 0)
            bs.set_editor_property("distance_field_resolution_scale",
                                   0.5 if name.startswith("land_n") else 0.0 if name.startswith("land_") else 1.0)
            sme.set_lod_build_settings(mesh, 0, bs)
        except Exception as e:
            unreal.log_warning("[Torque] dales: %s distance field left as it was (%s)" % (name, e))
        if name.startswith("land_"):
            L.nanite_on(mesh)
        else:
            L.nanite_off(mesh)
        no_collision(mesh)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    frame = L.find_frame(meshes[probe], os.path.join(folder, probe + ".glb"))
    yaw, mirror, scale = frame
    rot = unreal.Rotator(0.0, 0.0, float(yaw))
    for name, mesh in sorted(meshes.items()):
        a = actors.spawn_actor_from_object(mesh, unreal.Vector(0, 0, 0), rot)
        a.set_actor_scale3d(unreal.Vector(scale, scale * mirror, scale))
        a.set_actor_label(name)
        a.set_folder_path("Dales/Land" if name.startswith("land_") else "Dales")
        comp = a.get_component_by_class(unreal.StaticMeshComponent)
        comp.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        comp.set_material(0, land if name.startswith("land_") else water if name == "beck" else tarmac)
        comp.set_collision_profile_name("NoCollision")
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_editor_property("can_ever_affect_navigation", False)
        if name == "beck":
            comp.set_editor_property("cast_shadow", False)
    log("land: %d meshes" % len(meshes))

    # Walls, scars, rocks, barns, sheep.
    pspec = json.load(open(os.path.join(folder, "props.json")))
    pmeshes = L.import_glbs(os.path.join(folder, "Props"), PROP_DEST)
    for name, mesh in pmeshes.items():
        no_collision(mesh)
        L.nanite_on(mesh)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    L.place_props(pmeshes, pspec, mats, frame)

    # Trees: the ride's models.
    tspec = json.load(open(os.path.join(folder, "trees.json")))
    n = missing = 0
    found = tree_meshes(L.TREE_DEST, list(tspec["instances"]))
    for name, rows in sorted(tspec["instances"].items()):
        mesh = found.get(name)
        if not isinstance(mesh, unreal.StaticMesh):
            missing += len(rows)
            continue
        n += L.instanced("Tree_" + name, mesh, rows, tree, frame)
    if missing:
        unreal.log_warning("[Torque] dales: %d trees left out: the ride's tree models aren't in %s yet "
                           "(run Tools/ue_build_level.py, then this again)" % (missing, L.TREE_DEST))
    log("%d trees; %s" % (n, json.dumps(spec.get("counts", {}))))
