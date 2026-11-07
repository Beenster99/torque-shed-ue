"""Build the Buttertubs level in the Unreal editor from Data/Buttertubs (made by export_buttertubs.py).

In the editor: Tools > Execute Python Script... and pick this file (or type `py Tools/ue_build_level.py`
in the Output Log's Cmd box). It takes a few minutes the first time and is safe to run again: it
reimports the meshes and rebuilds /Game/Maps/Buttertubs from scratch.

What it does:
  1. imports every glb in Data/Buttertubs into /Game/Buttertubs (terrain tiles, far hills, road),
     with complex collision so the bike's wheel traces hit the real surface;
  2. works out how the glTF importer turned the files' frame (metres, x east, y up, z south) into
     Unreal's, by comparing the road's imported bounds with its known ones, so the level lines up
     with the riding sim whatever the importer version does;
  3. makes simple materials (land coloured by its cover, tarmac, the walls, rocks, grass and sheep
     from Tools/buttertubs_detail.py, which get no collision);
  4. makes the level: the meshes, a sun, sky atmosphere, sky light, height fog, clouds, an unbound
     post process volume, and a player start at the top of the ride.
"""
import json
import math
import os

import unreal

MAP = "/Game/Maps/Buttertubs"
DEST = "/Game/Buttertubs"
MAT_DIR = "/Game/Buttertubs/Materials"
BIKE_DEST = "/Game/Bike/SpeedTwin"
BIKE_MAT_DIR = "/Game/Bike/Materials"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def log(msg):
    unreal.log("[Torque] " + msg)


def data_dir():
    return os.path.join(os.path.abspath(unreal.Paths.project_dir()), "Data", "Buttertubs")


def glb_bounds(path):
    """POSITION min/max from a glb's accessor (written by export_buttertubs.py)."""
    import struct
    with open(path, "rb") as f:
        head = f.read(20)
        jlen = struct.unpack("<I", head[12:16])[0]
        js = json.loads(f.read(jlen))
    acc = js["accessors"][js["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
    return acc["min"], acc["max"]


def import_glbs(folder, dest=DEST):
    files = sorted(f for f in os.listdir(folder) if f.endswith(".glb"))
    tasks = []
    for f in files:
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", os.path.join(folder, f))
        t.set_editor_property("destination_path", dest)
        t.set_editor_property("destination_name", os.path.splitext(f)[0])
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        tasks.append(t)
    log("importing %d meshes..." % len(tasks))
    asset_tools.import_asset_tasks(tasks)
    meshes = {}
    for f, t in zip(files, tasks):
        name = os.path.splitext(f)[0]
        for p in t.get_editor_property("imported_object_paths") or []:
            a = unreal.load_asset(p)
            if isinstance(a, unreal.StaticMesh):
                meshes[name] = a
                break
        if name not in meshes:
            # Some importer versions put the mesh in a subfolder or prefix its name; search for it.
            for p in unreal.EditorAssetLibrary.list_assets(dest, recursive=True):
                a = unreal.load_asset(p)
                if isinstance(a, unreal.StaticMesh) and name in a.get_name():
                    meshes[name] = a
                    break
        if name not in meshes:
            unreal.log_warning("[Torque] no static mesh imported from " + f)
    return meshes


def nanite_off(mesh):
    """The importer turns Nanite on, and Nanite's fallback (what collision and traces use) keeps only
    a few hundred triangles of a tile: the wheels then land on a rough copy of the hill and the road
    and land, simplified separately, cut through each other. A 33k-triangle tile doesn't need it."""
    try:
        ns = mesh.get_editor_property("nanite_settings")
        ns.set_editor_property("enabled", False)
        try:
            sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
            sme.set_nanite_settings(mesh, ns, True)
        except Exception:
            mesh.set_editor_property("nanite_settings", ns)
    except Exception as e:
        unreal.log_warning("[Torque] couldn't turn Nanite off on %s: %s" % (mesh.get_name(), e))


def setup_mesh(mesh, full_precision_uvs=False):
    nanite_off(mesh)
    body = mesh.get_editor_property("body_setup")
    if body:
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    try:
        sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        s = sme.get_lod_build_settings(mesh, 0)
        s.set_editor_property("recompute_normals", False)
        s.set_editor_property("recompute_tangents", True)
        s.set_editor_property("generate_lightmap_u_vs", False)
        if full_precision_uvs:
            s.set_editor_property("use_full_precision_u_vs", True)
        sme.set_lod_build_settings(mesh, 0, s)
    except Exception as e:  # older or newer API: the defaults still work
        unreal.log_warning("[Torque] build settings not changed for %s: %s" % (mesh.get_name(), e))
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)


# --- Working out the importer's frame --------------------------------------------------------------

def expected_ue_box(gmin, gmax):
    """The box Unreal should see for glb positions (x east, y up, z south, metres): (100x, 100z, 100y)."""
    return ([gmin[0] * 100, gmin[2] * 100, gmin[1] * 100], [gmax[0] * 100, gmax[2] * 100, gmax[1] * 100])


def transform_box(bmin, bmax, yaw_deg, mirror_y, scale):
    c, s = math.cos(math.radians(yaw_deg)), math.sin(math.radians(yaw_deg))
    pts = []
    for x in (bmin[0], bmax[0]):
        for y in (bmin[1], bmax[1]):
            for z in (bmin[2], bmax[2]):
                x1, y1, z1 = x * scale, y * scale * mirror_y, z * scale
                pts.append((x1 * c - y1 * s, x1 * s + y1 * c, z1))
    return [min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]


def find_frame(mesh, glb_path):
    box = mesh.get_bounding_box()
    imin = [box.min.x, box.min.y, box.min.z]
    imax = [box.max.x, box.max.y, box.max.z]
    emin, emax = expected_ue_box(*glb_bounds(glb_path))
    best = None
    for scale in (1.0, 100.0, 0.01):
        for yaw in (0, 90, 180, 270):
            for mirror in (1, -1):
                tmin, tmax = transform_box(imin, imax, yaw, mirror, scale)
                err = sum(abs(a - b) for a, b in zip(tmin + tmax, emin + emax))
                if best is None or err < best[0]:
                    best = (err, yaw, mirror, scale)
    err, yaw, mirror, scale = best
    log("importer frame: yaw %d, mirror y %d, scale %g (fit error %.1f cm)" % (yaw, mirror, scale, err))
    if err > 500:
        unreal.log_warning("[Torque] the imported road doesn't match its expected bounds; the level may be off")
    return yaw, mirror, scale


# --- Materials -------------------------------------------------------------------------------------

def new_material(name, folder=MAT_DIR):
    path = folder + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    return asset_tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())


def const3(mat, rgb, x, y):
    e = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, x, y)
    e.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return e


def const1(mat, v, x, y):
    e = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y)
    e.set_editor_property("r", v)
    return e


def noise(mat, scale, lo, hi, x, y):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionNoise, x, y)
    n.set_editor_property("scale", scale)
    n.set_editor_property("output_min", lo)
    n.set_editor_property("output_max", hi)
    n.set_editor_property("levels", 4)
    return n


def land_material():
    """Cover colour from the vertex colour, verge colour where its alpha falls to the road, and two
    scales of noise so the hillside isn't flat paint. A start: real grass and stone come later."""
    m = new_material("M_Land")
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -900, 0)
    verge = const3(m, (0.10, 0.09, 0.06), -900, 250)
    lerp = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -600, 100)
    mel.connect_material_expressions(verge, "", lerp, "A")
    mel.connect_material_expressions(vc, "", lerp, "B")   # the unnamed output is RGB
    mel.connect_material_expressions(vc, "A", lerp, "Alpha")
    n1 = noise(m, 0.0015, 0.7, 1.2, -900, 450)
    n2 = noise(m, 0.02, 0.85, 1.1, -900, 650)
    mul1 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -400, 200)
    mel.connect_material_expressions(lerp, "", mul1, "A")
    mel.connect_material_expressions(n1, "", mul1, "B")
    mul2 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 250)
    mel.connect_material_expressions(mul1, "", mul2, "A")
    mel.connect_material_expressions(n2, "", mul2, "B")
    n3 = noise(m, 0.15, 0.88, 1.08, -900, 850)   # fine mottling close up
    mul3 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -120, 300)
    mel.connect_material_expressions(mul2, "", mul3, "A")
    mel.connect_material_expressions(n3, "", mul3, "B")
    mel.connect_material_property(mul3, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(const1(m, 0.92, -250, 450), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def detail_material():
    """Walls, rocks, grass, sheep (Tools/buttertubs_detail.py): their vertex colour, two-sided for the
    grass blades and fence wires, with a little noise so stone isn't flat paint."""
    m = new_material("M_Detail")
    m.set_editor_property("two_sided", True)
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -700, 0)
    n1 = noise(m, 0.4, 0.75, 1.15, -700, 250)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -400, 100)
    mel.connect_material_expressions(vc, "", mul, "A")
    mel.connect_material_expressions(n1, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(const1(m, 0.9, -400, 300), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def road_material():
    """Tarmac with white edge lines. The ribbon's vertex colour red is 0 on the centre line and 1 at
    the edges, so the outer few centimetres (red above about 0.96) are painted."""
    m = new_material("M_Road")
    base = const3(m, (0.03, 0.03, 0.029), -900, 0)
    n = noise(m, 0.05, 0.75, 1.25, -900, 200)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -650, 100)
    mel.connect_material_expressions(base, "", mul, "A")
    mel.connect_material_expressions(n, "", mul, "B")
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -1100, 400)
    sub = mel.create_material_expression(m, unreal.MaterialExpressionSubtract, -900, 400)
    sub.set_editor_property("const_b", 0.955)
    mel.connect_material_expressions(vc, "R", sub, "A")
    gain = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -750, 400)
    gain.set_editor_property("const_b", 60.0)
    mel.connect_material_expressions(sub, "", gain, "A")
    sat = mel.create_material_expression(m, unreal.MaterialExpressionSaturate, -600, 400)
    mel.connect_material_expressions(gain, "", sat, "")
    paint = const3(m, (0.7, 0.7, 0.68), -650, 250)
    mix = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -400, 150)
    mel.connect_material_expressions(mul, "", mix, "A")
    mel.connect_material_expressions(paint, "", mix, "B")
    mel.connect_material_expressions(sat, "", mix, "Alpha")
    mel.connect_material_property(mix, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(const1(m, 0.72, -400, 300), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def bike_materials(finishes):
    """A two sided master (colour, metallic, roughness as parameters) and an instance per finish."""
    m = new_material("M_BikeMaster", BIKE_MAT_DIR)
    m.set_editor_property("two_sided", True)
    col = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -500, 0)
    col.set_editor_property("parameter_name", "Color")
    met = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -500, 250)
    met.set_editor_property("parameter_name", "Metallic")
    rough = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -500, 400)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.5)
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(met, "", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    paths = {}
    for name, f in finishes.items():
        mi_name = "MI_" + name
        path = BIKE_MAT_DIR + "/" + mi_name
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
        mi = asset_tools.create_asset(mi_name, BIKE_MAT_DIR, unreal.MaterialInstanceConstant,
                                      unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, m)
        c = f["color"]
        mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(c[0], c[1], c[2], 1.0))
        mel.set_material_instance_scalar_parameter_value(mi, "Metallic", float(f["metallic"]))
        mel.set_material_instance_scalar_parameter_value(mi, "Roughness", float(f["roughness"]))
        unreal.EditorAssetLibrary.save_loaded_asset(mi)
        paths[name] = mi.get_path_name()
    return paths


# The rider's kit (the pawn builds a simple rider from shapes): leather jacket, jeans, boots, gloves,
# a dark helmet and visor.
RIDER_FINISHES = {
    "rider_jacket": {"color": [0.030, 0.027, 0.025], "metallic": 0.0, "roughness": 0.55},
    "rider_jeans": {"color": [0.035, 0.050, 0.090], "metallic": 0.0, "roughness": 0.85},
    "rider_boots": {"color": [0.018, 0.016, 0.015], "metallic": 0.0, "roughness": 0.45},
    "rider_gloves": {"color": [0.30, 0.14, 0.06], "metallic": 0.0, "roughness": 0.55},
    "rider_helmet": {"color": [0.10, 0.10, 0.11], "metallic": 0.0, "roughness": 0.25},
    "rider_visor": {"color": [0.01, 0.01, 0.012], "metallic": 0.0, "roughness": 0.05},
    "rider_skin": {"color": [0.45, 0.30, 0.24], "metallic": 0.0, "roughness": 0.6},
    # The clock's dial and needles (the pawn draws them over the model's plain face).
    "rider_dial": {"color": [0.012, 0.012, 0.014], "metallic": 0.0, "roughness": 0.3},
    "rider_needle": {"color": [0.95, 0.95, 0.92], "metallic": 0.0, "roughness": 0.4},
    "rider_revneedle": {"color": [0.95, 0.35, 0.05], "metallic": 0.0, "roughness": 0.4},
}


def import_bike(frame):
    """The Speed Twin's parts (Tools/make_speedtwin.py) and a list of where they landed, for the pawn."""
    folder = os.path.join(os.path.abspath(unreal.Paths.project_dir()), "Data", "Bike", "SpeedTwin")
    if not os.path.isdir(folder):
        unreal.log_warning("[Torque] no bike parts in %s; the bike stays as placeholder shapes" % folder)
        return
    info = json.load(open(os.path.join(folder, "speedtwin.json")))
    meshes = import_glbs(folder, BIKE_DEST)
    for mesh in meshes.values():
        nanite_off(mesh)
        body = mesh.get_editor_property("body_setup")
        if body:
            body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    finishes = dict(info["finishes"])
    finishes.update(RIDER_FINISHES)
    mats = bike_materials(finishes)
    assets = {"frame": {"yaw": frame[0], "mirror": frame[1], "scale": frame[2]}, "parts": {},
              "rider": {k[len("rider_"):]: mats[k] for k in RIDER_FINISHES}}
    for part in info["parts"]:
        mesh = meshes.get(part["name"])
        if mesh:
            assets["parts"][part["name"]] = {"mesh": mesh.get_path_name(), "material": mats.get(part["finish"], "")}
    with open(os.path.join(folder, "ue_assets.json"), "w") as f:
        json.dump(assets, f, indent=1)
    log("bike: %d parts imported" % len(assets["parts"]))


# --- The level -------------------------------------------------------------------------------------

def spawn(cls, loc=(0, 0, 0), rot=(0, 0, 0)):
    return actors.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(*rot))


def is_detail(name):
    return name.startswith("detail_") or name.startswith("grass_")


def build():
    folder = data_dir()
    route = json.load(open(os.path.join(folder, "route.json")))
    meshes = import_glbs(folder)
    if "road" not in meshes:
        raise RuntimeError("the road mesh didn't import; see the Output Log")
    for name, mesh in meshes.items():
        if is_detail(name):
            nanite_off(mesh)   # no collision: the wheels only see the land and the road
            body = mesh.get_editor_property("body_setup")
            if body:
                body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
            unreal.EditorAssetLibrary.save_loaded_asset(mesh)
        else:
            setup_mesh(mesh, full_precision_uvs=(name == "road"))
    try:
        sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        for n in ("road", "backdrop", "terrain_0_0"):
            if n in meshes:
                log("%s: %d vertices at LOD0 (Nanite %s)" % (n, sme.get_number_verts(meshes[n], 0),
                    meshes[n].get_editor_property("nanite_settings").get_editor_property("enabled")))
    except Exception as e:
        unreal.log_warning("[Torque] couldn't count vertices: %s" % e)
    yaw, mirror, scale = find_frame(meshes["road"], os.path.join(folder, "road.glb"))
    land, road, detail = land_material(), road_material(), detail_material()
    import_bike((yaw, mirror, scale))

    # Start the level afresh (a second run replaces the first).
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        levels.new_level("/Game/Maps/Scratch")
        unreal.EditorAssetLibrary.delete_asset(MAP)
    levels.new_level(MAP)
    rot = unreal.Rotator(0.0, 0.0, float(yaw))   # (roll, pitch, yaw) in the Python constructor
    for name, mesh in sorted(meshes.items()):
        a = actors.spawn_actor_from_object(mesh, unreal.Vector(0, 0, 0), rot)
        a.set_actor_scale3d(unreal.Vector(scale, scale * mirror, scale))
        a.set_actor_label(name)
        comp = a.get_component_by_class(unreal.StaticMeshComponent)
        comp.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        if is_detail(name):
            comp.set_material(0, detail)
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            comp.set_editor_property("can_ever_affect_navigation", False)
            if name.startswith("grass_"):   # tufts are lost in the distance: draw them only within 250 m
                comp.set_editor_property("ld_max_draw_distance", 25000.0)
                comp.set_editor_property("cast_shadow", False)
            a.set_folder_path("Detail")
            continue
        comp.set_material(0, road if name == "road" else land)
        if name == "road":
            a.set_editor_property("tags", [unreal.Name("Road")])
        a.set_folder_path("Terrain" if name != "road" else "Road")

    # Sun, sky and air. A late-morning Dales sun from the south east, a little haze.
    sun = spawn(unreal.DirectionalLight, (0, 0, 50000), (0, -38, 125))
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sc.set_editor_property("atmosphere_sun_light", True)
    sc.set_editor_property("intensity", 9.0)
    sc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    spawn(unreal.SkyAtmosphere)
    sky = spawn(unreal.SkyLight, (0, 0, 20000))
    skc = sky.get_component_by_class(unreal.SkyLightComponent)
    skc.set_editor_property("real_time_capture", True)
    skc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    fog = spawn(unreal.ExponentialHeightFog, (0, 0, -20000))
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fc.set_editor_property("fog_density", 0.008)
    fc.set_editor_property("fog_height_falloff", 0.05)
    fc.set_editor_property("enable_volumetric_fog", True)
    try:
        spawn(unreal.VolumetricCloud)
    except Exception as e:
        unreal.log_warning("[Torque] no volumetric cloud: %s" % e)
    ppv = spawn(unreal.PostProcessVolume)
    ppv.set_editor_property("unbound", True)

    sp = route["spawn"]["ue"]
    heading_deg = math.degrees(route["spawn"]["heading"])
    # Sim heading 0 faces north (-y in Unreal) and turns left as it grows; Unreal yaw 0 faces east (+x)
    # and turns right (towards +y) as it grows.
    spawn(unreal.PlayerStart, (sp[0], sp[1], sp[2] + 100.0), (0, 0, -90.0 - heading_deg))

    levels.save_current_level()
    log("built %s with %d meshes; press Play to ride" % (MAP, len(meshes)))


build()
