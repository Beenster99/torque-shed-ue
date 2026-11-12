"""Build the level the real world's roads are ridden in (Ride UK stage 2): /Game/Maps/RideUK.

Run inside the Unreal editor, after Tools/ue_build_level.py has built the Buttertubs ride (it borrows
that script's sun, sky, air and camera, and the ride's cloud shadows as they are):

    UnrealEditor-Cmd.exe TorqueShedProto.uproject -ExecutePythonScript=Tools/ue_build_rideuk.py -unattended

The level has no ground of its own: only the sky, the lights and a player start. The game lays the
ground and the road out round the rider as they go (World/TorqueRideWorld), from the ride the world
subsystem planned, and draws them with the materials made here in /Game/RideUK/Materials: the plain
M_RideLand (moor grass, mottled) and M_RideRoad (the road's vertex colours: tarmac, verge, paint, and
hedges, roofs and stand-in trees), and Buttertubs' own scanned ground and stone made again under
names of their own (M_RideLandScan, M_RideStone, M_RideMasonry; the game uses them when they are here).
Its grass and trees are Buttertubs' own too: the plant kit (place_kit) copies the Buttertubs level's
species, so run this after ue_build_level.py has built that level.
"""
import json
import os
import sys

import unreal

MAP = "/Game/Maps/RideUK"
MAT_DIR = "/Game/RideUK/Materials"


def log(msg):
    unreal.log("[Torque] ride UK: " + msg)


def level_lib():
    """Tools/ue_build_level.py as a module, without it building the Buttertubs ride."""
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


def land_material(L):
    """The ground's own colour from its vertices (what covers it: World/WorldRide's LandColour), darker
    and paler in broad patches, mottled close up, matte."""
    mel = L.mel
    m = L.new_material("M_RideLand", MAT_DIR)
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -900, 0)
    patch = L.noise(m, 0.0006, 0.72, 1.25, -900, 400)
    mix = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -600, 100)
    mel.connect_material_expressions(vc, "", mix, "A")   # the unnamed output is RGB
    mel.connect_material_expressions(patch, "", mix, "B")
    mid = L.noise(m, 0.01, 0.8, 1.15, -900, 600)
    mul1 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -400, 200)
    mel.connect_material_expressions(mix, "", mul1, "A")
    mel.connect_material_expressions(mid, "", mul1, "B")
    fine = L.noise(m, 0.15, 0.85, 1.1, -900, 800)
    mul2 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 300)
    mel.connect_material_expressions(mul1, "", mul2, "A")
    mel.connect_material_expressions(fine, "", mul2, "B")
    mel.connect_material_property(mul2, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(L.const1(m, 0.95, -250, 500), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(L.const1(m, 0.2, -250, 650), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def road_material(L):
    """The road's own colours from its vertices (tarmac, the verge's gravel, the paint: World/WorldRide's
    RoadPiece), a little uneven in tone."""
    mel = L.mel
    m = L.new_material("M_RideRoad", MAT_DIR)
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -900, 0)
    n = L.noise(m, 0.05, 0.8, 1.2, -900, 200)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -650, 100)
    mel.connect_material_expressions(vc, "", mul, "A")   # the unnamed output is RGB
    mel.connect_material_expressions(n, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(L.const1(m, 0.8, -400, 300), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def scanned_materials(L):
    """The hand-built ride's own scanned looks, made again here under /Game/RideUK/Materials so the
    streamed world is drawn as Buttertubs is (Ride UK stage 3): M_RideLandScan (Buttertubs' M_Land:
    grass, moor grass, peat, rock on steep ground, tinted by the vertex colour the ground's cover gives
    it, World/WorldRide's LandColour, which matches that colour's meaning), M_RideStone (M_Stone, the
    dry stone walls) and M_RideMasonry (M_Masonry, stone houses and barns). Their vertex colours are
    the stone's masks (R brightness, G which scan, B lichen, A moss) and UV0 metres of surface, as
    World/WorldRideScenery makes them. Built from the same textures as Buttertubs (Data/Textures)."""
    tex, roles = L.import_textures()
    made = []
    original = L.new_material
    try:
        for want, make in (("M_RideLandScan", lambda: L.land_material_scanned(tex, roles) if "grass" in tex else None),
                           ("M_RideStone", lambda: L.stone_material(tex, roles)),
                           ("M_RideMasonry", lambda: L.stone_material(tex, roles, "M_Masonry", "masonry", "masonry", broad_moss=False))):
            # (the level script's makers name their own material in its own folder: put it here, under our name)
            L.new_material = lambda name, folder=None, _want=want: original(_want, MAT_DIR)
            try:
                if make() is not None:
                    made.append(want)
            except Exception as e:
                unreal.log_warning("[Torque] ride UK: couldn't make %s (%s); the game draws it plain" % (want, e))
    finally:
        L.new_material = original
    if "grass" not in tex:
        # (a stale scanned ground would otherwise be used with textures that are gone)
        path = MAT_DIR + "/M_RideLandScan"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
        unreal.log_warning("[Torque] ride UK: no grass texture (python Tools/fetch_textures.py); the ground stays plain")
    return made


SPECIES_FIELDS = ("name", "cull_distance", "cast_shadow", "scale_multiplier", "native_mesh", "keep", "cull_spread", "min_draw_distance")


def read_kit(L):
    """Buttertubs' plants and trees as its level has them (its TorqueFoliage actors "Foliage" and
    "Trees"): {label: {"frame": (yaw, mirror, scale), "species": [{field: value, "mesh": path, ...}]}},
    asset paths rather than objects, as the level is let go before the ride's is built."""
    if not hasattr(unreal, "TorqueFoliage"):
        unreal.log_warning("[Torque] ride UK: the TorqueFoliage class isn't built; no grass or trees")
        return {}
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.load_level(L.MAP):
        unreal.log_warning("[Torque] ride UK: couldn't open %s for its plants; no grass or trees" % L.MAP)
        return {}
    path = lambda o: o.get_path_name() if o else ""
    kit = {}
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        label = a.get_actor_label()
        if not isinstance(a, unreal.TorqueFoliage) or label not in ("Foliage", "Trees"):
            continue
        rows = []
        for e in a.get_editor_property("species"):
            r = {k: e.get_editor_property(k) for k in SPECIES_FIELDS}
            r["name"] = str(r["name"])
            r["mesh"] = path(e.get_editor_property("mesh"))
            r["material"] = path(e.get_editor_property("material"))
            r["variants"] = [path(m) for m in e.get_editor_property("variants")]
            r["variant_materials"] = [path(m) for m in e.get_editor_property("variant_materials")]
            rows.append(r)
        kit[label] = {"frame": (a.get_editor_property("frame_yaw"), a.get_editor_property("frame_mirror"), a.get_editor_property("frame_scale")),
                      "species": rows}
    log("Buttertubs' plants: %s" % ", ".join("%s %d species" % (k, len(v["species"])) for k, v in kit.items()))
    return kit


def grass_materials(L, kit):
    """The grass's materials without Buttertubs' ground-colour virtual texture (the ride has none: the
    plants would sample black): M_RideFoliage for our own plants, and the scanned plants' instances
    copied onto M_RideFoliageScan, the scanned master made afresh with no ground blend. Changes the
    kit's "Foliage" rows in place."""
    rows = kit.get("Foliage", {}).get("species", [])
    if not rows:
        return
    original = L.new_material
    try:
        L.new_material = lambda name, folder=None: original("M_RideFoliage", MAT_DIR)
        plain = L.foliage_material(None, 1.0)
        defaults = {}
        for r in rows:
            for p in r["variants"]:
                m = unreal.load_asset(p)
                if isinstance(m, unreal.StaticMesh):
                    for k, v in L.scan_textures(m).items():
                        defaults.setdefault(k, v)
        master = None
        if "BaseColor" in defaults:
            L.new_material = lambda name, folder=None: original("M_RideFoliageScan", MAT_DIR)
            master = L.scan_foliage_master(defaults, None, 1.0)
    finally:
        L.new_material = original
    plain_path = plain.get_path_name() if plain else ""
    copied = {}
    for r in rows:
        if r["material"] or not r["variants"]:
            r["material"] = plain_path
        mats = []
        for p in r["variant_materials"]:
            if not p or master is None:
                mats.append(p if master is None else "")
                continue
            if p not in copied:
                src = p.split(".")[0]
                dst = MAT_DIR + "/Ride" + src.rsplit("/", 1)[-1]
                if unreal.EditorAssetLibrary.does_asset_exist(dst):
                    unreal.EditorAssetLibrary.delete_asset(dst)
                mi = unreal.EditorAssetLibrary.duplicate_asset(src, dst)
                if mi:
                    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, master)
                    unreal.EditorAssetLibrary.save_loaded_asset(mi)
                copied[p] = mi.get_path_name() if mi else ""
            mats.append(copied[p])
        r["variant_materials"] = mats
    log("grass materials: M_RideFoliage%s, %d scanned instances" % (", M_RideFoliageScan" if master else "", len(copied)))


def scanned_trees(L, kit):
    """Scanned trees (Data/RideUK/scanned_trees.json: Quixel Megaplants once imported on the PC) in
    the kit's "Trees" in place of Buttertubs' own models of the same kind, as species named
    SM_Tree_<kind>_scan (native meshes with their own materials). Changes the kit in place."""
    f = os.path.join(os.path.dirname(L.data_dir()), "RideUK", "scanned_trees.json")
    if not os.path.exists(f) or "Trees" not in kit:
        return
    spec = json.load(open(f))
    norm = lambda t: "".join(ch for ch in t.lower() if ch.isalnum())
    skip = [norm(w) for w in spec.get("skip", [])]
    most = int(spec.get("most", 6))
    meshes = []
    for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path("/Game", recursive=True):
        if str(a.asset_class_path.asset_name) != "StaticMesh":
            continue
        pkg = str(a.package_name)
        if pkg.startswith("/Game/Buttertubs/") or pkg.startswith("/Game/RideUK/"):
            continue
        meshes.append((norm(pkg), "%s.%s" % (pkg, a.asset_name)))
    rows = kit["Trees"]["species"]
    template = dict(rows[0]) if rows else {"cull_distance": 3000000.0, "cast_shadow": True}
    found = {}
    for kind, words in spec["kinds"].items():
        keys = [norm(w) for w in words]
        got = sorted(p for n, p in meshes if any(k in n for k in keys) and not any(w in n for w in skip))[:most]
        if not got:
            continue
        found[kind] = got
        # (ours of this kind go: the scanned ones take every tree of it, near and far)
        rows[:] = [r for r in rows if not (r["name"].startswith("SM_Tree_%s_" % kind) or r["name"].startswith("SM_TreeFar_%s_" % kind))]
        r = dict(template)
        r.update({"name": "SM_Tree_%s_scan" % kind, "mesh": "", "material": "", "variants": got, "variant_materials": [],
                  "native_mesh": True, "scale_multiplier": 1.0, "keep": 1.0, "cull_spread": 1.0, "min_draw_distance": 0.0, "cast_shadow": True})
        rows.append(r)
    log("scanned trees: %s" % (", ".join("%s %d (%s)" % (k, len(v), v[0].rsplit("/", 1)[-1]) for k, v in found.items()) or
                               "none found (import the Megaplants on the PC; Data/RideUK/scanned_trees.json)"))


def place_kit(kit):
    """The ride's plant kit: two TorqueFoliage actors that plant nothing themselves (Density 0), tagged
    RideGrass and RideTrees, with Buttertubs' species; World/TorqueRideWorld lays them out as it goes."""
    load = lambda p: unreal.load_asset(p) if p else None
    for label, tag in (("Foliage", "RideGrass"), ("Trees", "RideTrees")):
        k = kit.get(label)
        if not k:
            continue
        species = []
        for r in k["species"]:
            e = unreal.TorqueFoliageSpecies()
            for f in SPECIES_FIELDS:
                e.set_editor_property(f, r[f])
            e.set_editor_property("mesh", load(r["mesh"]))
            e.set_editor_property("material", load(r["material"]))
            e.set_editor_property("variants", [m for m in (load(p) for p in r["variants"]) if m])
            e.set_editor_property("variant_materials", [load(p) for p in r["variant_materials"]][:len(r["variants"])])
            species.append(e)
        a = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.TorqueFoliage, unreal.Vector(0, 0, 0))
        a.set_actor_label(tag)
        a.set_editor_property("tags", [unreal.Name(tag)])
        a.set_editor_property("instances_file", "")
        a.set_editor_property("density", 0.0)
        a.set_editor_property("species", species)
        a.set_editor_property("frame_yaw", float(k["frame"][0]))
        a.set_editor_property("frame_mirror", float(k["frame"][1]))
        a.set_editor_property("frame_scale", float(k["frame"][2]))
        log("%s: %d species" % (tag, len(species)))


def build():
    L = level_lib()
    land_material(L)
    road_material(L)
    scanned = scanned_materials(L)
    kit = read_kit(L)
    grass_materials(L, kit)
    scanned_trees(L, kit)
    before = L.begin_fresh_level(MAP)
    L.sky_and_camera(reuse_cloud_shadow=True)
    # (the bike is put down at the ride's start by the game; a level still wants a start of its own)
    L.spawn(unreal.PlayerStart, (0, 0, 100000.0))
    place_kit(kit)
    L.save_fresh_level(MAP, before)
    log("built %s and %s/M_RideLand, M_RideRoad%s" % (MAP, MAT_DIR, "".join(", " + m for m in scanned)))


build()
