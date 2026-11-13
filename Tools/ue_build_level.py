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
  3. makes the materials (scanned land and tarmac, stone with moss and lichen, fleece, galvanised
     steel, barn wood) and imports the stone-by-stone props (Data/Buttertubs/Props: walls, scars,
     boulders, barns, gates, sheep, from Tools/buttertubs_detail.py) as Nanite meshes with no collision;
  3b. imports the moorland plants (Tools/make_foliage.py) as Nanite meshes and adds the TorqueFoliage
     actor that plants them when the level starts;
  3c. the becks (Tools/make_becks.py), the limestone scars on the far fells (Tools/make_far_rock.py),
     and the same as 3b for the trees (Tools/make_trees.py): ash, sycamore, alder, birch, hawthorn and spruce
     from a canopy height map, planted by a second TorqueFoliage actor, "Trees";
  4. makes the level: the meshes, the props as instances from props.json, a sun, sky atmosphere, sky light, height fog, clouds, an unbound
     post process volume, and a player start at the top of the ride.
"""
import json
import math
import os

import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue_bike_dirt  # noqa: E402

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


def try_set(obj, prop, value):
    """Set an editor property if this engine version has it; log (don't fail) when it doesn't."""
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:
        unreal.log_warning("[Torque] couldn't set %s: %s" % (prop, e))
        return False


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


WEATHER_MPC = MAT_DIR + "/MPC_Weather"   # ATorqueGameState sets these at run time (Weather/WeatherModel.h)
# Each with the look the level was made with as its default, so a level without the game running looks as before.
WEATHER_PARAMS = (
    ("Wetness", 0.0),        # the roads, land and walls: 0 dry .. 1 wet
    ("Wind", 1.0),           # the grass and trees' sway: 1 the moor's usual breeze, 0.15 calm .. 2.5 a gale
    ("WindX", 0.78),         # the way the wind blows (towards; x east, y south): north east by default
    ("WindY", -0.62),
    ("CloudShadow", 0.6),    # where the cloud shadows' noise turns to shade: 1 none (clear) .. 0.45 most of the ground
    ("Rain", 0.0),           # how hard it's raining for the eye, 0 .. 1
    ("Puddles", 0.4),        # standing water: up to 0.4 the road's edges fill, past 0.6 the middle too
    ("Snow", 0.0),           # lying snow (Ride UK stage 6): 0 none .. 1 a full cover on the ground and wall tops
    ("Night", 0.0),          # how dark it is (Ride UK stage 7): 0 day .. 1 night; street lamps, lit windows, cat's eyes
)
_weather_mpc = None


def weather_mpc():
    """The weather's material parameter collection, made once per build: a scalar Wetness that the
    road, land and walls read, set by the game (-TorqueWeather=sunny|overcast|wet, V or D-pad right)."""
    global _weather_mpc
    if _weather_mpc is not None:
        return _weather_mpc or None
    try:
        # Reuse the collection if a previous build made it: deleting it while the old M_Road still
        # pointed at it made M_Road fail to compile (harmless, but noisy) until it was rebuilt.
        mpc = unreal.load_asset(WEATHER_MPC) if unreal.EditorAssetLibrary.does_asset_exist(WEATHER_MPC) else None
        if mpc is None:
            folder, name = WEATHER_MPC.rsplit("/", 1)
            mpc = asset_tools.create_asset(name, folder, unreal.MaterialParameterCollection,
                                           unreal.MaterialParameterCollectionFactoryNew())
        # Keep the parameters already there (their ids): materials find a parameter by its id, so
        # remaking them all with new ids broke every material built against the old ones in another
        # map (the shed's dale: "CollectionParameter has invalid parameter None").
        have = {}
        for p in mpc.get_editor_property("scalar_parameters") or []:
            have[str(p.get_editor_property("parameter_name"))] = p
        params = []
        for name, default in WEATHER_PARAMS:
            p = have.get(name)
            if p is None:
                p = unreal.CollectionScalarParameter()
                p.set_editor_property("parameter_name", name)
            p.set_editor_property("default_value", default)
            params.append(p)
        mpc.set_editor_property("scalar_parameters", params)
        unreal.EditorAssetLibrary.save_loaded_asset(mpc)
        _weather_mpc = mpc
        log("weather parameter collection made")
    except Exception as e:
        unreal.log_warning("[Torque] no weather parameter collection: %s" % e)
        _weather_mpc = False
    return _weather_mpc or None


def weather_param(g, name):
    """One of the weather's scalars in a material (its default when the collection couldn't be made)."""
    mpc = weather_mpc()
    if mpc is None:
        return g.const(dict(WEATHER_PARAMS)[name])
    return g.node(unreal.MaterialExpressionCollectionParameter, collection=mpc, parameter_name=name)


def wetness(g):
    """The Wetness scalar in a material (0 when the collection couldn't be made)."""
    return weather_param(g, "Wetness")


SKY_MPC = MAT_DIR + "/MPC_Sky"   # ATorqueSky sets these at run time (Weather/TorqueSky.h, skies plan stage 2)
SKY_PARAMS = (
    ("Cover", 0.3),     # the cloud cover drawn, 0 clear .. 1 overcast (the real cover, remapped for the view from the saddle)
    ("Density", 0.8),   # how dense the cloud is: x 0.1 per metre of extinction (0.6 a fair day, 1 a deck, 1.6 rain cloud)
    ("Albedo", 1.0),    # the cloud's own colour: 1 white, 0.7 the grey of rain cloud
    ("Shape", 0.3),     # 0 heaped (cumulus) .. 1 a flat deck (stratus)
    ("DriftX", 0.0),    # how far the wind at cloud height has carried the clouds, metres along x (east)
    ("DriftY", 0.0),    # and along y (south)
)
_sky_mpc = None


def sky_mpc():
    """The sky's material parameter collection, as weather_mpc (parameters kept with their ids)."""
    global _sky_mpc
    if _sky_mpc is not None:
        return _sky_mpc or None
    try:
        mpc = unreal.load_asset(SKY_MPC) if unreal.EditorAssetLibrary.does_asset_exist(SKY_MPC) else None
        if mpc is None:
            folder, name = SKY_MPC.rsplit("/", 1)
            mpc = asset_tools.create_asset(name, folder, unreal.MaterialParameterCollection,
                                           unreal.MaterialParameterCollectionFactoryNew())
        have = {}
        for p in mpc.get_editor_property("scalar_parameters") or []:
            have[str(p.get_editor_property("parameter_name"))] = p
        params = []
        for name, default in SKY_PARAMS:
            p = have.get(name)
            if p is None:
                p = unreal.CollectionScalarParameter()
                p.set_editor_property("parameter_name", name)
            p.set_editor_property("default_value", default)
            params.append(p)
        mpc.set_editor_property("scalar_parameters", params)
        unreal.EditorAssetLibrary.save_loaded_asset(mpc)
        _sky_mpc = mpc
        log("sky parameter collection made")
    except Exception as e:
        unreal.log_warning("[Torque] no sky parameter collection: %s" % e)
        _sky_mpc = False
    return _sky_mpc or None


def sky_param(g, name):
    """One of the sky's scalars in a material (its default when the collection couldn't be made)."""
    mpc = sky_mpc()
    if mpc is None:
        return g.const(dict(SKY_PARAMS)[name])
    return g.node(unreal.MaterialExpressionCollectionParameter, collection=mpc, parameter_name=name)


def snow_cover(g, layer, keep=0.0):
    """Lying snow over a layer [colour, normal, roughness] (Ride UK stage 6), from MPC_Weather's Snow:
    on faces that look up first, in drifts, a full cover at 1; keep (0..1) holds some of the surface
    showing through (grass tufts, a wall's stones). Changes the layer in place."""
    nz = g.node(unreal.MaterialExpressionVertexNormalWS)
    up = g.node(unreal.MaterialExpressionComponentMask, r=False, g=False, b=True, a=False)
    g.link(nz, up)
    snow = weather_param(g, "Snow")
    # (how much lies: the up-facing first, steeper faces only as it deepens; broken into drifts)
    lie = g.op(unreal.MaterialExpressionSubtract, g.op(unreal.MaterialExpressionMultiply, snow, const_b=1.6),
               g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionOneMinus, up), const_b=2.2))
    drift = g.mask_noise(0.004, 0.5, 1.6)
    lie = g.op(unreal.MaterialExpressionMultiply, lie, drift)
    lie = g.op(unreal.MaterialExpressionSaturate, lie)
    lie = g.op(unreal.MaterialExpressionMultiply, lie, const_b=1.0 - keep)
    layer[0] = g.lerp(layer[0], g.const((0.78, 0.8, 0.84)), lie)
    layer[1] = g.lerp(layer[1], g.const((0.0, 0.0, 1.0)), g.op(unreal.MaterialExpressionMultiply, lie, const_b=0.8))
    r_src = layer[2]
    r_out = "R" if r_src.get_class().get_name() == "MaterialExpressionTextureSample" else ""
    layer[2] = g.lerp(r_src, g.const(0.65), lie, a_out=r_out)
    return layer


def wind_dir(g, dip):
    """The way the wind blows as a vector (WindX, WindY, dip), for a plant's lean."""
    xy = g.op(unreal.MaterialExpressionAppendVector, weather_param(g, "WindX"), weather_param(g, "WindY"))
    return g.op(unreal.MaterialExpressionAppendVector, xy, g.const(dip))


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


# --- Scanned textures (Tools/fetch_textures.py) -----------------------------------------------------

TEX_DEST = "/Game/Textures"


def import_textures():
    """The roles in Data/Textures/textures.json whose images are here: {role: {map: Texture2D}, ...}.
    Colour maps are sRGB; normal, roughness and occlusion maps are linear data."""
    root = os.path.join(os.path.dirname(data_dir()), "Textures")
    spec_path = os.path.join(root, "textures.json")
    if not os.path.exists(spec_path):
        return {}, {}
    spec = json.load(open(spec_path))
    out = {}
    for role, r in spec["roles"].items():
        if "ue" in r:
            # Textures already in the project (a Megascans surface from Fab, say):
            # "ue": {"color": "/Game/...", "normal": "/Game/...", "rough": "/Game/...", "ao": "/Game/..."}
            maps = {k: unreal.load_asset(v) for k, v in r["ue"].items()}
            maps = {k: v for k, v in maps.items() if isinstance(v, unreal.Texture)}
            if "color" in maps:
                out[role] = maps
                continue
            unreal.log_warning("[Torque] %s: the project textures in 'ue' didn't load; trying %s" % (role, r.get("id")))
        folder = os.path.join(root, r["id"])
        if not os.path.exists(os.path.join(folder, "color.jpg")):
            unreal.log_warning("[Torque] texture %s for %s not downloaded (python Tools/fetch_textures.py)" % (r["id"], role))
            continue
        maps = {}
        for name in ("color", "normal", "rough", "ao"):
            f = os.path.join(folder, name + ".jpg")
            if not os.path.exists(f):
                continue
            t = unreal.AssetImportTask()
            t.set_editor_property("filename", f)
            t.set_editor_property("destination_path", TEX_DEST + "/" + r["id"])
            t.set_editor_property("destination_name", "T_%s_%s" % (r["id"], name))
            t.set_editor_property("automated", True)
            t.set_editor_property("replace_existing", True)
            t.set_editor_property("save", False)
            asset_tools.import_asset_tasks([t])
            tex = unreal.load_asset(TEX_DEST + "/" + r["id"] + "/T_%s_%s" % (r["id"], name))
            if not tex:
                unreal.log_warning("[Torque] couldn't import " + f)
                continue
            if name != "color":
                tex.set_editor_property("srgb", False)
            if name == "normal":
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            unreal.EditorAssetLibrary.save_loaded_asset(tex)
            maps[name] = tex
        if "color" in maps:
            out[role] = maps
    log("textures: " + ", ".join("%s=%s" % (k, spec["roles"][k]["id"]) for k in out))
    return out, spec["roles"]


class Graph:
    """A little helper for building material graphs from Python, laying nodes out in a column."""

    def __init__(self, mat):
        self.m = mat
        self.y = -2000

    def node(self, cls, x=-1200, **props):
        e = mel.create_material_expression(self.m, cls, x, self.y)
        self.y += 140
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def link(self, a, b, pin="", out=""):
        if mel.connect_material_expressions(a, out, b, pin):
            return
        if out == "" and mel.connect_material_expressions(a, "RGB", b, pin):   # a texture's colour output
            return
        unreal.log_warning("[Torque] couldn't link %s.%s to %s.%s" % (a.get_name(), out, b.get_name(), pin))

    def op(self, cls, a, b=None, a_out="", b_out="", **props):
        e = self.node(cls, -600, **props)
        one_input = tuple(getattr(unreal, n) for n in (
            "MaterialExpressionSaturate", "MaterialExpressionOneMinus", "MaterialExpressionFrac",
            "MaterialExpressionFloor", "MaterialExpressionCeil", "MaterialExpressionAbs",
            "MaterialExpressionRound", "MaterialExpressionTruncate", "MaterialExpressionSine",
            "MaterialExpressionCosine", "MaterialExpressionSquareRoot") if hasattr(unreal, n))
        # One-input nodes (Frac, Floor, ...) take their input on the unnamed pin, not "A" (round 141:
        # M_RideRoof/M_RideBrick failed with "Missing Frac input").
        self.link(a, e, "A" if b is not None or cls not in one_input else "", a_out)
        if b is not None:
            self.link(b, e, "B", b_out)
        return e

    def const(self, v):
        if isinstance(v, (tuple, list)):
            return self.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(v[0], v[1], v[2], 1.0))
        return self.node(unreal.MaterialExpressionConstant, r=float(v))

    def lerp(self, a, b, alpha, a_out="", b_out="", alpha_out=""):
        e = self.node(unreal.MaterialExpressionLinearInterpolate, -400)
        self.link(a, e, "A", a_out)
        self.link(b, e, "B", b_out)
        self.link(alpha, e, "Alpha", alpha_out)
        return e

    def world_uv(self, metres):
        """World position (x, y) in repeats of `metres`."""
        if not hasattr(self, "_wp"):
            wp = self.node(unreal.MaterialExpressionWorldPosition)
            self._wp = self.node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=False, a=False)
            self.link(wp, self._wp)
        return self.op(unreal.MaterialExpressionDivide, self._wp, const_b=metres * 100.0)

    def sample(self, tex, uv, kind):
        st = {"color": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
              "normal": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL}.get(kind, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        # Shared wrap sampler: SM5 allows only 16 sampler slots, and the land blends more maps than that.
        e = self.node(unreal.MaterialExpressionTextureSample, -900, texture=tex, sampler_type=st,
                      sampler_source=unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        self.link(uv, e, "UVs")
        return e

    def layer(self, maps, metres):
        """(colour, normal, roughness) of a scanned surface, the colour blended with a second, larger
        sample of itself so the repeat doesn't show from a distance."""
        uv = self.world_uv(metres)
        col = self.sample(maps["color"], uv, "color")
        far = self.sample(maps["color"], self.world_uv(metres * 4.7), "color")
        col = self.lerp(col, far, self.const(0.4))
        nrm = self.sample(maps["normal"], uv, "normal") if "normal" in maps else self.const((0.0, 0.0, 1.0))
        rough = self.sample(maps["rough"], uv, "rough") if "rough" in maps else self.const(0.85)
        if "ao" in maps:
            col = self.op(unreal.MaterialExpressionMultiply, col, self.sample(maps["ao"], uv, "ao"), b_out="R")
        return [col, nrm, rough]

    def mix(self, a, b, alpha, alpha_out=""):
        return [self.lerp(a[i], b[i], alpha, b_out="R" if (i == 2 and b[i].get_class().get_name() == "MaterialExpressionTextureSample") else "",
                          a_out="R" if (i == 2 and a[i].get_class().get_name() == "MaterialExpressionTextureSample") else "",
                          alpha_out=alpha_out) for i in range(3)]

    def mask_noise(self, scale, lo, hi):
        """Soft patches: noise stretched beyond 0..1 and clamped."""
        n = noise(self.m, scale, lo, hi, -1200, self.y)
        self.y += 140
        return self.op(unreal.MaterialExpressionSaturate, n)

    def finish(self, layer, extra_rough=None, min_rough=None, specular=None):
        mel.connect_material_property(layer[0], "", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(layer[1], "", unreal.MaterialProperty.MP_NORMAL)
        r_out = "R" if layer[2].get_class().get_name() == "MaterialExpressionTextureSample" else ""
        rough = layer[2]
        if min_rough is not None:
            # Scans like "Wet Grass" carry wet-day roughness; a floor stops a white sheen into the sun.
            rough = self.op(unreal.MaterialExpressionMax, layer[2], self.const(min_rough), a_out=r_out)
            r_out = ""
        mel.connect_material_property(rough, r_out, unreal.MaterialProperty.MP_ROUGHNESS)
        if specular is not None:
            mel.connect_material_property(self.const(specular), "", unreal.MaterialProperty.MP_SPECULAR)
        mel.recompile_material(self.m)
        unreal.EditorAssetLibrary.save_loaded_asset(self.m)
        return self.m


def lin(rgb255):
    return tuple((c / 255.0) ** 2.2 for c in rgb255)


def land_material_scanned(tex, roles):
    """Moorland from scanned surfaces: grass and moor grass in big patches, peat in hollows, rock on
    slopes steeper than about 35 degrees, gravel on the verge, all tinted by the map's cover colour so
    fields, moor and scree still fall where the map says."""
    g = Graph(new_material("M_Land"))
    grass = g.layer(tex["grass"], roles["grass"]["tile_m"])
    land = grass
    # beenster (8 Nov): the far ground read as "mottled polka dot". The moor and peat patches were
    # 12 m and 3 m blobs at full contrast, which shrink to dots across the dale; make them broad and
    # soft, and from about 80 m out let them settle to an even mix instead of dots.
    depth0 = g.node(unreal.MaterialExpressionPixelDepth)
    calm = g.op(unreal.MaterialExpressionSubtract, depth0, const_b=8000.0)
    calm = g.op(unreal.MaterialExpressionDivide, calm, const_b=30000.0)
    calm = g.op(unreal.MaterialExpressionSaturate, calm)
    if "moor" in tex:
        moor_mask = g.lerp(g.mask_noise(0.00028, -0.6, 1.6), g.const(0.35), calm)
        land = g.mix(land, g.layer(tex["moor"], roles["moor"]["tile_m"]), moor_mask)
    if "peat" in tex:
        # Peat only in the odd hollow, darkened and desaturated: the scan is a red-brown litter.
        peat = g.layer(tex["peat"], roles["peat"]["tile_m"])
        dull = g.node(unreal.MaterialExpressionDesaturation, -600)   # Fraction is an input pin, not a property
        g.link(peat[0], dull)
        g.link(g.const(0.7), dull, "Fraction")
        peat[0] = g.op(unreal.MaterialExpressionMultiply, dull, g.const((0.55, 0.5, 0.45)))
        peat_mask = g.op(unreal.MaterialExpressionMultiply, g.mask_noise(0.0011, -6.0, 0.9),
                         g.op(unreal.MaterialExpressionOneMinus, calm))
        land = g.mix(land, peat, peat_mask)
    # The map's cover: vertex colour relative to plain grass's, softened.
    vc = g.node(unreal.MaterialExpressionVertexColor)
    tint = g.op(unreal.MaterialExpressionDivide, vc, g.const(lin((92, 112, 52))))
    tint = g.lerp(g.const((1.0, 1.0, 1.0)), tint, g.const(0.55))
    land[0] = g.op(unreal.MaterialExpressionMultiply, land[0], tint)
    land[0] = g.op(unreal.MaterialExpressionMultiply, land[0], const_b=0.75)   # grade 2/4: grass 1.3x too bright vs the DJI footage
    if "rock" in tex:
        nz = g.node(unreal.MaterialExpressionVertexNormalWS)
        up = g.node(unreal.MaterialExpressionComponentMask, r=False, g=False, b=True, a=False)
        g.link(nz, up)
        steep = g.op(unreal.MaterialExpressionSubtract, up, const_b=0.80)
        steep = g.op(unreal.MaterialExpressionMultiply, steep, const_b=-7.0)
        steep = g.op(unreal.MaterialExpressionSaturate, steep)
        land = g.mix(land, g.layer(tex["rock"], roles["rock"]["tile_m"]), steep)
    if "gravel" in tex:
        verge = g.op(unreal.MaterialExpressionOneMinus, vc, a_out="A")
        verge = g.op(unreal.MaterialExpressionMultiply, verge, verge)   # a narrower verge: grit only near the edge
        land = g.mix(land, g.layer(tex["gravel"], roles["gravel"]["tile_m"]), verge)
    # Livelier greens: push the colour away from its own grey (lerp past 1 extrapolates).
    grey = g.node(unreal.MaterialExpressionDesaturation, -600)   # Fraction is an input pin, not a property
    g.link(land[0], grey)
    g.link(g.const(1.0), grey, "Fraction")
    land[0] = g.lerp(grey, land[0], g.const(1.6))
    # Big, slow variation in brightness, so the hillside isn't one even colour from afar.
    land[0] = g.op(unreal.MaterialExpressionMultiply, land[0], noise(g.m, 0.0003, 0.9, 1.07, -1200, g.y + 200))
    # Far away, the scanned textures shrink into a busy mottle (contour-like bands of moor and peat);
    # from about 150 m out lean part of the way towards the map's own cover colour and a softer normal.
    # Only part of the way (at most 55%): a full fade to flat colour read as mush with Lumen on.
    depth = g.node(unreal.MaterialExpressionPixelDepth)
    far = g.op(unreal.MaterialExpressionSubtract, depth, const_b=12000.0)
    far = g.op(unreal.MaterialExpressionDivide, far, const_b=60000.0)
    far = g.op(unreal.MaterialExpressionSaturate, far)
    far = g.op(unreal.MaterialExpressionMultiply, far, const_b=0.55)
    cover = g.op(unreal.MaterialExpressionMultiply, vc, const_b=1.1)
    cover = g.lerp(land[0], cover, g.const(0.7))
    land = g.mix(land, [cover, g.const((0.0, 0.0, 1.0)), g.const(0.85)], far)
    # Fellside breakup that survives the distance: brightness patches tens of metres across, and
    # drifts of dead bracken / bleached moor grass (a straw-brown tint), so far slopes aren't even green.
    land[0] = g.op(unreal.MaterialExpressionMultiply, land[0], noise(g.m, 0.0005, 0.88, 1.08, -1200, g.y + 400))
    straw = g.op(unreal.MaterialExpressionMultiply, land[0], g.const((1.35, 1.0, 0.62)))
    drift = g.mask_noise(0.00035, -1.8, 1.4)   # big drifts; finer read as polka dots far off
    drift = g.op(unreal.MaterialExpressionMultiply, drift, const_b=0.5)
    land[0] = g.lerp(land[0], straw, drift)
    # No purple: flowers in the scans, tiled small and saturated far off, read as pink polka dots.
    # Moorland blue never exceeds about 0.85 of its green.
    def chan(src, c):
        e = g.node(unreal.MaterialExpressionComponentMask, r=c == "R", g=c == "G", b=c == "B", a=False)
        g.link(src, e)
        return e
    rg = g.node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=False, a=False)
    g.link(land[0], rg)
    blue = g.op(unreal.MaterialExpressionMin, chan(land[0], "B"), g.op(unreal.MaterialExpressionMultiply, chan(land[0], "G"), const_b=0.4))
    land[0] = g.op(unreal.MaterialExpressionAppendVector, rg, blue)
    # Rain darkens the ground a little and takes the edge off its roughness.
    wet = wetness(g)
    land[0] = g.op(unreal.MaterialExpressionMultiply, land[0], g.lerp(g.const(1.0), g.const(0.8), wet))
    r_src = land[2]
    r_out = "R" if r_src.get_class().get_name() == "MaterialExpressionTextureSample" else ""
    # beenster (8 Nov): still a sheen on the ground looking into the sun. Moorland is matte: a high
    # roughness floor and low specular when dry (rain still brings some shine back).
    land[2] = g.op(unreal.MaterialExpressionMax, r_src, g.lerp(g.const(0.92), g.const(0.55), wet), a_out=r_out)
    snow_cover(g, land)
    return g.finish(land, specular=0.12)


def road_blemish_textures():
    """Data/Textures/RoadBlemish (Tools/make_road_blemish.py): mask.png (R patch tint, G bitumen seal,
    B pothole, A wheel-track polish) and normal.png, laid along the road by its UV. None if not made."""
    out = {}
    for name, normal in (("mask", False), ("normal", True)):
        f = os.path.join(os.path.dirname(data_dir()), "Textures", "RoadBlemish", name + ".png")
        if not os.path.exists(f):
            return None
        t = unreal.AssetImportTask()
        for k, v in (("filename", f), ("destination_path", TEX_DEST + "/RoadBlemish"),
                     ("destination_name", "T_RoadBlemish_" + name), ("automated", True),
                     ("replace_existing", True), ("save", False)):
            t.set_editor_property(k, v)
        asset_tools.import_asset_tasks([t])
        tx = unreal.load_asset(TEX_DEST + "/RoadBlemish/T_RoadBlemish_" + name)
        if not tx:
            return None
        tx.set_editor_property("srgb", False)
        if normal:
            tx.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        unreal.EditorAssetLibrary.save_loaded_asset(tx)
        out[name] = tx
    return out


def road_material_scanned(tex, roles):
    """Scanned tarmac, darkened a little towards the middle where the wheels polish it, with the white
    edge lines (vertex colour red above about 0.955) painted on top."""
    g = Graph(new_material("M_Road"))
    road = g.layer(tex["asphalt"], roles["asphalt"]["tile_m"])
    vc = g.node(unreal.MaterialExpressionVertexColor)
    line = g.op(unreal.MaterialExpressionSubtract, vc, a_out="R", const_b=0.955)
    line = g.op(unreal.MaterialExpressionMultiply, line, const_b=60.0)
    line = g.op(unreal.MaterialExpressionSaturate, line)
    # Worn paint: broken up by a fine noise.
    wear = g.mask_noise(0.05, -1.0, 1.6)
    line = g.op(unreal.MaterialExpressionMultiply, line, wear)
    paint = [g.const((0.62, 0.62, 0.6)), g.const((0.0, 0.0, 1.0)), g.const(0.55)]
    # Patches, repairs and darker wheel tracks: slow variation in the tarmac's brightness.
    # Dales tarmac in beenster's DJI footage is a light, neutral, faintly cool grey (sRGB about 138,139,142):
    # take the scan's brown out, then the slow variation in brightness.
    flat = g.node(unreal.MaterialExpressionDesaturation, -600)   # Fraction is an input pin, not a property
    g.link(road[0], flat)
    g.link(g.const(0.9), flat, "Fraction")
    road[0] = g.op(unreal.MaterialExpressionMultiply, flat, g.const((0.96, 0.99, 1.06)))
    road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], noise(g.m, 0.002, 0.6, 0.95, -1200, g.y + 200))
    blem = road_blemish_textures()
    if blem:
        # The road's UV: u across the carriageway, v = metres / 20. The blemish map is 204.8 m long;
        # a second, mirrored sample at another period takes over in patches so the repeat doesn't show.
        tc = g.node(unreal.MaterialExpressionTextureCoordinate)
        uv1 = g.op(unreal.MaterialExpressionMultiply, tc, g.node(unreal.MaterialExpressionConstant2Vector, r=1.0, g=20.0 / 204.8))
        uv2 = g.op(unreal.MaterialExpressionMultiply, tc, g.node(unreal.MaterialExpressionConstant2Vector, r=-1.0, g=20.0 / 133.7))
        uv2 = g.op(unreal.MaterialExpressionAdd, uv2, g.node(unreal.MaterialExpressionConstant2Vector, r=1.0, g=0.37))
        pick = g.mask_noise(0.004, -6.0, 7.0)
        m = g.lerp(g.sample(blem["mask"], uv1, "mask"), g.sample(blem["mask"], uv2, "mask"), pick,
                   a_out="RGBA", b_out="RGBA")   # keep alpha: the default link is RGB
        # Overall strength (beenster: "a bit over the top"): 0.7 of the baked marks.
        neutral = g.node(unreal.MaterialExpressionConstant4Vector, constant=unreal.LinearColor(0.5, 0.0, 0.0, 0.0))
        m = g.lerp(neutral, m, g.const(0.7))
        bn = g.lerp(g.sample(blem["normal"], uv1, "normal"), g.sample(blem["normal"], uv2, "normal"), pick)
        def ch(c):
            e = g.node(unreal.MaterialExpressionComponentMask, r=c == "R", g=c == "G", b=c == "B", a=c == "A")
            g.link(m, e)
            return e
        rough_in = road[2]
        r_out = "R" if rough_in.get_class().get_name() == "MaterialExpressionTextureSample" else ""
        rough = g.op(unreal.MaterialExpressionMultiply, rough_in, g.const(1.0), a_out=r_out)
        # Patches: R 0.5 none; lower a fresh dark patch, higher pale old tarmac.
        tint = g.op(unreal.MaterialExpressionMultiply, ch("R"), const_b=1.6)
        tint = g.op(unreal.MaterialExpressionAdd, tint, const_b=0.2)
        road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], tint)
        rt = g.op(unreal.MaterialExpressionMultiply, ch("R"), const_b=0.4)   # fresh tarmac smoother, old rougher
        rough = g.op(unreal.MaterialExpressionMultiply, rough, g.op(unreal.MaterialExpressionAdd, rt, const_b=0.8))
        # Wheel tracks and oil: darker and a little smoother.
        trk = g.op(unreal.MaterialExpressionMultiply, ch("A"), const_b=-0.25)
        trk = g.op(unreal.MaterialExpressionAdd, trk, const_b=1.0)
        road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], trk)
        rough = g.op(unreal.MaterialExpressionMultiply, rough, g.lerp(g.const(1.0), g.const(0.75), ch("A")))
        # Potholes: dark, broken and rough.
        road[0] = g.lerp(road[0], g.op(unreal.MaterialExpressionMultiply, road[0], const_b=0.45), ch("B"))
        rough = g.lerp(rough, g.const(1.0), ch("B"))
        # Bitumen seal over cracks and patch seams: near black and shiny.
        road[0] = g.lerp(road[0], g.const((0.012, 0.012, 0.012)), ch("G"))
        rough = g.lerp(rough, g.const(0.3), ch("G"))
        road[2] = rough
        # Lips, dips and ridges: add the blemish normal's slope to the tarmac's.
        slope = g.op(unreal.MaterialExpressionMultiply, bn, g.const((1.0, 1.0, 0.0)))
        road[1] = g.op(unreal.MaterialExpressionAdd, road[1], slope)
    road = g.mix(road, paint, line)
    # Weather: dry tarmac keeps a roughness floor (no white sheen into the sun); wet, it goes dark and
    # glossy, with standing water in patches and along the wheel-polished tracks.
    wet = wetness(g)
    r_src = road[2]
    r_out = "R" if r_src.get_class().get_name() == "MaterialExpressionTextureSample" else ""
    rough = g.op(unreal.MaterialExpressionMax, r_src, g.lerp(g.const(0.6), g.const(0.12), wet), a_out=r_out)
    # Standing water (beenster): the camber drains it to the edges, most on the inside of a bend where
    # the road leans in and little on the outside; only after hours of rain does it reach across the
    # middle. The road's vertex colour: R 0 centre .. 1 edge, G 0.5 a straight, 1 a bend's inside
    # edge, 0 its outside (Tools/road_puddle_sides.py). MPC_Weather's Puddles says how much water stands.
    vc_p = g.node(unreal.MaterialExpressionVertexColor)
    edge = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                g.op(unreal.MaterialExpressionSubtract, vc_p, a_out="R", const_b=0.5), const_b=2.6))      # 0 inside 0.5 .. 1 by 0.88
    side = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionAdd,
                g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, vc_p, a_out="G", const_b=0.5),
                     const_b=1.8), const_b=0.5))                                                          # outside 0 .. straight 0.5 .. inside 1
    pud = weather_param(g, "Puddles")
    fill = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply, pud, const_b=2.5))
    # Build 131 (beenster): fewer puddles, but now and then a bigger one. A slow noise picks the odd
    # stretch of road (tens of metres) where a big puddle sits; there it reaches further in from the edge.
    big = g.mask_noise(0.0009, -3.0, 1.4)
    wide = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                g.op(unreal.MaterialExpressionSubtract, vc_p, a_out="R", const_b=0.25), const_b=1.6))     # 0 inside 0.25 .. 1 by 0.88
    edge = g.lerp(edge, wide, big)
    where = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, edge, g.op(unreal.MaterialExpressionMultiply, side, const_b=1.6)), fill)
    centre = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                  g.op(unreal.MaterialExpressionSubtract, pud, const_b=0.6), const_b=2.5))
    where = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMax, where, centre))
    # In patches along it: small ones few and far between (most of the edge stays dry), and larger,
    # longer pools only where the slow noise says so.
    small = g.mask_noise(0.006, -7.0, 2.0)
    large = g.op(unreal.MaterialExpressionMultiply, g.mask_noise(0.0022, -2.5, 2.2), big)
    patches = g.op(unreal.MaterialExpressionMax, small, large)
    puddle = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, patches, where), wet)
    road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], g.lerp(g.const(1.0), g.const(0.5), wet))
    road[0] = g.lerp(road[0], g.op(unreal.MaterialExpressionMultiply, road[0], const_b=0.6), puddle)
    road[2] = g.lerp(rough, g.const(0.03), puddle)
    road[1] = g.lerp(road[1], g.const((0.0, 0.0, 1.0)), puddle)   # still water is flat
    return g.finish(road, specular=0.4)


BIKE_TEX_DEST = "/Game/Bike/Textures"


def bike_textures():
    """The detail sets from Tools/make_bike_textures.py: {name: {"n": normal map, "d": detail map}}."""
    folder = os.path.join(os.path.abspath(unreal.Paths.project_dir()), "Data", "Bike", "Textures")
    out = {}
    if not os.path.isdir(folder):
        unreal.log_warning("[Torque] no bike textures in %s (python Tools/make_bike_textures.py)" % folder)
        return out
    for f in sorted(os.listdir(folder)):
        stem, ext = os.path.splitext(f)
        if ext.lower() != ".png" or stem[-2:] not in ("_n", "_d", "_a"):
            continue
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", os.path.join(folder, f))
        t.set_editor_property("destination_path", BIKE_TEX_DEST)
        t.set_editor_property("destination_name", "T_Bike_" + stem)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", False)
        asset_tools.import_asset_tasks([t])
        tex = unreal.load_asset(BIKE_TEX_DEST + "/T_Bike_" + stem)
        if not tex:
            unreal.log_warning("[Torque] couldn't import " + f)
            continue
        tex.set_editor_property("srgb", False)
        if stem.endswith("_a"):
            # Coverage for masked materials: no mips, or thin lines fall under the clip threshold as
            # they average away with distance and vanish (the pinstripes did).
            try_set(tex, "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        if stem.endswith("_n"):
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        else:
            try_set(tex, "compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
        out.setdefault(stem[:-2], {})[stem[-1]] = tex
    log("bike textures: " + ", ".join(sorted(out)))
    return out


def bike_master(name, tex, paint):
    """The bike's master material. Two finishes (Color/Metallic/Roughness and the same with 2) blended
    by the mesh's vertex colour red (pinstripes, fin edges, heat tint); a detail set (NormalMap,
    DetailMap: red a roughness offset about 0.5, green a shade) repeated UVScale times per UV unit;
    paint masters are clear coat (a lacquer over the metallic or solid base)."""
    m = new_material(name, BIKE_MAT_DIR)
    m.set_editor_property("two_sided", True)
    if paint:
        m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    g = Graph(m)
    flat = tex.get("flat", {})

    def scalar(pname, v):
        return g.node(unreal.MaterialExpressionScalarParameter, -1500, parameter_name=pname, default_value=float(v))

    def vector(pname, v):
        return g.node(unreal.MaterialExpressionVectorParameter, -1500, parameter_name=pname,
                      default_value=unreal.LinearColor(v[0], v[1], v[2], 1.0))

    def texparam(pname, t, kind):
        props = {"parameter_name": pname, "sampler_source": unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS,
                 "sampler_type": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind == "n"
                 else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}
        if t:
            props["texture"] = t
        e = g.node(unreal.MaterialExpressionTextureSampleParameter2D, -900, **props)
        g.link(uv, e, "UVs")
        return e

    tc = g.node(unreal.MaterialExpressionTextureCoordinate, -1500)
    uv = g.op(unreal.MaterialExpressionMultiply, tc, scalar("UVScale", 1.0))
    ntex = texparam("NormalMap", flat.get("n"), "n")
    dtex = texparam("DetailMap", flat.get("d"), "d")
    nrm = g.lerp(g.const((0.0, 0.0, 1.0)), ntex, scalar("NormalStrength", 1.0), b_out="RGB")
    # The finish mask is UV channel 1's u (written by make_speedtwin.py; the importer drops vertex
    # colour, which then reads white, so it isn't used).
    uv1 = g.node(unreal.MaterialExpressionTextureCoordinate, -1500, coordinate_index=1)
    vc = g.node(unreal.MaterialExpressionComponentMask, -1300, r=True, g=False, b=False, a=False)   # TexCoord has no R pin
    g.link(uv1, vc)
    col = g.lerp(vector("Color", (0.5, 0.5, 0.5)), vector("Color2", (0.5, 0.5, 0.5)), vc)
    shaded = g.op(unreal.MaterialExpressionMultiply, col, dtex, b_out="G")
    met = g.lerp(scalar("Metallic", 0.0), scalar("Metallic2", 0.0), vc)
    rough = g.lerp(scalar("Roughness", 0.5), scalar("Roughness2", 0.5), vc)
    roff = g.op(unreal.MaterialExpressionSubtract, dtex, a_out="R", const_b=0.5)
    roff = g.op(unreal.MaterialExpressionMultiply, roff, scalar("RoughVar", 0.0))
    rough = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionAdd, rough, roff))
    # Road dirt (Torque Shed's look: mud low down, dust on top, flies up front) laid over the finish,
    # washed off where the pressure washer's jet has been (Tools/ue_bike_dirt.py).
    look, glow = ue_bike_dirt.add_dirt(m)
    emis = g.op(unreal.MaterialExpressionMultiply, col, scalar("Emissive", 0.0))
    shaded, met, rough, emis, cover = ue_bike_dirt.lay_over(m, -500, 2400, look, glow, shaded, met, rough, emis)
    outs = [(shaded, "BaseColor", "MP_BASE_COLOR"), (met, "Metallic", "MP_METALLIC"), (rough, "Roughness", "MP_ROUGHNESS"),
            (nrm, "Normal", "MP_NORMAL"), (emis, "EmissiveColor", "MP_EMISSIVE_COLOR")]
    coat = [(g.op(unreal.MaterialExpressionOneMinus, cover), "ClearCoat", "MP_CUSTOM_DATA0"),   # no lacquer shine through the dirt
            (scalar("ClearCoatRoughness", 0.03), "ClearCoatRoughness", "MP_CUSTOM_DATA1")] if paint else []
    done = False
    if coat and not hasattr(unreal.MaterialProperty, "MP_CUSTOM_DATA0") and hasattr(unreal.MaterialProperty, "MP_MATERIAL_ATTRIBUTES"):
        # UE 5.8's Python has no clear coat material properties, but Make Material Attributes has the
        # pins: route everything through it.
        try:
            m.set_editor_property("use_material_attributes", True)
            mma = g.node(unreal.MaterialExpressionMakeMaterialAttributes, -200)
            ok = all(mel.connect_material_expressions(e, "", mma, pin) for e, pin, _ in outs + coat)
            ok = ok and mel.connect_material_property(mma, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
            if ok:
                done = True
                log("%s: clear coat through material attributes" % name)
            else:
                unreal.log_warning("[Torque] %s: material attributes didn't link; plain inputs, default clear coat" % name)
                m.set_editor_property("use_material_attributes", False)
        except Exception as e:
            unreal.log_warning("[Torque] %s: material attributes failed (%s); plain inputs, default clear coat" % (name, e))
            try_set(m, "use_material_attributes", False)
    if not done:
        for e, _, prop in outs + coat:
            if hasattr(unreal.MaterialProperty, prop):
                mel.connect_material_property(e, "", getattr(unreal.MaterialProperty, prop))
            elif coat:
                # Unconnected, clear coat is 1.0 at roughness 0.1.
                unreal.log_warning("[Torque] %s not exposed to Python; clear coat left at its default" % prop)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def bike_decal_master(tex, name="M_BikeDecal", wrap=False):
    """Decals (the tank's "1200" and pinstripes, the side panels' lettering): a masked material, the
    coverage map's red as the opacity mask, a flat colour, metallic and roughness. Cheap: one sample.
    With wrap, the same for repeating cut-outs (the brake discs' drilling)."""
    m = new_material(name, BIKE_MAT_DIR)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    try_set(m, "opacity_mask_clip_value", 0.4)
    g = Graph(m)
    props = {"parameter_name": "DecalMap", "sampler_type": unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
             "sampler_source": unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS if wrap
             else unreal.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS}
    first = next((v["a"] for v in tex.values() if "a" in v), None)
    if first:
        props["texture"] = first
    cover = g.node(unreal.MaterialExpressionTextureSampleParameter2D, -900, **props)
    col = g.node(unreal.MaterialExpressionVectorParameter, -900, parameter_name="Color",
                 default_value=unreal.LinearColor(0.01, 0.01, 0.01, 1.0))
    met = g.node(unreal.MaterialExpressionScalarParameter, -900, parameter_name="Metallic", default_value=0.0)
    rough = g.node(unreal.MaterialExpressionScalarParameter, -900, parameter_name="Roughness", default_value=0.2)
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(met, "", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(cover, "R", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def bike_materials(finishes):
    """A master for plain finishes and a clear coat one for paint, and an instance per finish (see
    Tools/make_speedtwin.py for what a finish holds; the rider's kit gives just colour and the two
    numbers, and the rest take defaults)."""
    tex = bike_textures()
    masters = {"base": bike_master("M_BikeMaster", tex, False), "paint": bike_master("M_BikePaint", tex, True),
               "decal": bike_decal_master(tex), "cutout": bike_decal_master(tex, "M_BikeCutout", True)}
    paths = {}
    for name, f in finishes.items():
        mi_name = "MI_" + name
        path = BIKE_MAT_DIR + "/" + mi_name
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
        mi = asset_tools.create_asset(mi_name, BIKE_MAT_DIR, unreal.MaterialInstanceConstant,
                                      unreal.MaterialInstanceConstantFactoryNew())
        shader = f.get("shader", "base")
        mel.set_material_instance_parent(mi, masters.get(shader, masters["base"]))
        if shader in ("decal", "cutout"):
            c = f["color"]
            mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(c[0], c[1], c[2], 1.0))
            mel.set_material_instance_scalar_parameter_value(mi, "Metallic", float(f.get("metallic", 0.0)))
            mel.set_material_instance_scalar_parameter_value(mi, "Roughness", float(f.get("roughness", 0.2)))
            cover = tex.get(f.get("detail", ""), {}).get("a")
            if cover:
                mel.set_material_instance_texture_parameter_value(mi, "DecalMap", cover)
            else:
                unreal.log_warning("[Torque] decal map %s for %s not found" % (f.get("detail"), name))
            unreal.EditorAssetLibrary.save_loaded_asset(mi)
            paths[name] = mi.get_path_name()
            continue
        for key, pname in (("color", "Color"), ("color2", "Color2")):
            c = f.get(key, f["color"])
            mel.set_material_instance_vector_parameter_value(mi, pname, unreal.LinearColor(c[0], c[1], c[2], 1.0))
        for key, pname, default in (("metallic", "Metallic", 0.0), ("roughness", "Roughness", 0.5),
                                    ("uv_scale", "UVScale", 1.0), ("normal_strength", "NormalStrength", 1.0),
                                    ("rough_var", "RoughVar", 0.0), ("emissive", "Emissive", 0.0), ("grime", "Grime", 0.0)):
            mel.set_material_instance_scalar_parameter_value(mi, pname, float(f.get(key, default)))
        mel.set_material_instance_scalar_parameter_value(mi, "Metallic2", float(f.get("metallic2", f["metallic"])))
        mel.set_material_instance_scalar_parameter_value(mi, "Roughness2", float(f.get("roughness2", f["roughness"])))
        if shader == "paint":
            mel.set_material_instance_scalar_parameter_value(mi, "ClearCoatRoughness", float(f.get("clearcoat_roughness", 0.03)))
        detail = tex.get(f.get("detail", "flat"))
        if detail:
            if "n" in detail:
                mel.set_material_instance_texture_parameter_value(mi, "NormalMap", detail["n"])
            if "d" in detail:
                mel.set_material_instance_texture_parameter_value(mi, "DetailMap", detail["d"])
        elif f.get("detail", "flat") != "flat":
            unreal.log_warning("[Torque] detail set %s for %s not found" % (f.get("detail"), name))
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


RIDER_DEST = "/Game/Rider"
RIDER_MAT_DIR = "/Game/Rider/Materials"


# The kit's scanned surfaces (roles in Data/Textures/textures.json), by finish in rider.json.
RIDER_TEX = {"jacket": "rider_jacket", "jeans": "rider_denim", "gloves": "rider_glove", "boots": "rider_boot",
             "sole": "rider_sole", "liner": "rider_jacket", "trim": "rider_sole"}


def rider_scan_master(maps):
    """The kit from a scanned surface, on the UVs make_rider.py writes (metres, a box map blended by
    the normal). Three samples: colour, normal, roughness. The colour is the scan's tinted to the
    finish (Tint = finish / the scan's mean, so the weave and grain stay and the average is the
    kit's), times the baked occlusion; the normal map is ambientCG's OpenGL one, green flipped for
    Unreal; the roughness is the scan's scaled to the finish, and rougher in the creases."""
    m = new_material("M_Rider_scan", RIDER_MAT_DIR)
    m.set_editor_property("used_with_skeletal_mesh", True)
    m.set_editor_property("used_with_static_lighting", False)
    X = -1400

    def node(cls, x, y, **props):
        e = mel.create_material_expression(m, cls, x, y)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def link(a, b, pin="", out=""):
        if not mel.connect_material_expressions(a, out, b, pin):
            unreal.log_warning("[Torque] rider material: couldn't link %s.%s to %s.%s" % (a.get_name(), out, b.get_name(), pin))

    def op(cls, a, b, x, y, a_out="", b_out=""):
        e = node(cls, x, y)
        link(a, e, "A", a_out)
        link(b, e, "B", b_out)
        return e

    def scalar(name, v, x, y):
        return node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)

    tc = node(unreal.MaterialExpressionTextureCoordinate, X, 0, coordinate_index=0)
    uv = op(unreal.MaterialExpressionMultiply, tc, scalar("Repeats", 4.0, X, 120), X + 200, 0)

    def sample(name, tex, kind, y):
        st = {"color": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
              "normal": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL}.get(kind, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        e = node(unreal.MaterialExpressionTextureSampleParameter2D, X + 450, y, parameter_name=name, texture=tex,
                 sampler_type=st, sampler_source=unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        link(uv, e, "UVs")
        return e

    col = sample("ColorMap", maps["color"], "color", -300)
    vc = node(unreal.MaterialExpressionVertexColor, X + 450, 300)
    ao = node(unreal.MaterialExpressionLinearInterpolate, X + 800, 300)
    link(const1(m, 1.0, X + 600, 260), ao, "A")
    link(vc, ao, "B", "R")
    link(scalar("Occlusion", 1.0, X + 600, 420), ao, "Alpha")
    tint = node(unreal.MaterialExpressionVectorParameter, X + 450, -120, parameter_name="Tint",
                default_value=unreal.LinearColor(1, 1, 1, 1))
    base = op(unreal.MaterialExpressionMultiply, col, tint, X + 800, -300, a_out="RGB")
    base = op(unreal.MaterialExpressionMultiply, base, ao, X + 1000, -300)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(ao, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)

    if "normal" in maps:
        nrm = sample("NormalMap", maps["normal"], "normal", 600)
        flip = node(unreal.MaterialExpressionConstant3Vector, X + 600, 760, constant=unreal.LinearColor(1, -1, 1, 1))
        nrm = op(unreal.MaterialExpressionMultiply, nrm, flip, X + 800, 600, a_out="RGB")
        flat = node(unreal.MaterialExpressionConstant3Vector, X + 800, 760, constant=unreal.LinearColor(0, 0, 1, 1))
        nl = node(unreal.MaterialExpressionLinearInterpolate, X + 1000, 600)
        link(flat, nl, "A")
        link(nrm, nl, "B")
        link(scalar("Bump", 1.0, X + 800, 860), nl, "Alpha")
        mel.connect_material_property(nl, "", unreal.MaterialProperty.MP_NORMAL)

    if "rough" in maps:
        r = sample("RoughMap", maps["rough"], "rough", 1000)
        r = op(unreal.MaterialExpressionMultiply, r, scalar("RoughScale", 1.0, X + 600, 1150), X + 800, 1000, a_out="R")
    else:
        r = scalar("RoughScale", 0.7, X + 600, 1000)
    crease = node(unreal.MaterialExpressionLinearInterpolate, X + 1000, 1000)
    link(const1(m, 1.0, X + 800, 1250), crease, "A")
    link(r, crease, "B")
    link(vc, crease, "Alpha", "R")
    sat = node(unreal.MaterialExpressionSaturate, X + 1200, 1000)
    link(crease, sat)
    mel.connect_material_property(sat, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(scalar("Specular", 0.5, X + 1000, 1300), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def rider_materials(finishes, tex=None, roles=None):
    """The kit: a master lit by the rider's baked occlusion (vertex colour), and a clear-coated one
    for the helmet's paint and visor. One instance per finish in rider.json."""
    paths = {}
    masters = {}
    for kind in ("kit", "gloss"):
        m = new_material("M_Rider_" + kind, RIDER_MAT_DIR)
        m.set_editor_property("used_with_skeletal_mesh", True)
        if kind == "gloss":
            try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_CLEAR_COAT)
        col = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -700, 0)
        col.set_editor_property("parameter_name", "Color")
        vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -700, 200)
        ao_amt = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -700, 350)
        ao_amt.set_editor_property("parameter_name", "Occlusion")
        ao_amt.set_editor_property("default_value", 1.0)
        one = const1(m, 1.0, -700, 480)
        ao = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -450, 300)
        mel.connect_material_expressions(one, "", ao, "A")
        mel.connect_material_expressions(vc, "R", ao, "B")
        mel.connect_material_expressions(ao_amt, "", ao, "Alpha")
        base = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 100)
        mel.connect_material_expressions(col, "", base, "A")
        mel.connect_material_expressions(ao, "", base, "B")
        mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(ao, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
        rough = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -450, 600)
        rough.set_editor_property("parameter_name", "Roughness")
        rough.set_editor_property("default_value", 0.6)
        # Rougher in the creases, where kit wears dull and dust sits.
        rmul = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -250, 600)
        rmax = const1(m, 1.0, -450, 720)
        mel.connect_material_expressions(rmax, "", rmul, "A")
        mel.connect_material_expressions(rough, "", rmul, "B")
        mel.connect_material_expressions(vc, "R", rmul, "Alpha")
        mel.connect_material_property(rmul, "", unreal.MaterialProperty.MP_ROUGHNESS)
        spec = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -450, 850)
        spec.set_editor_property("parameter_name", "Specular")
        spec.set_editor_property("default_value", 0.5)
        mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
        if kind == "gloss":
            try:
                cc = const1(m, 1.0, -450, 1000)
                ccr = const1(m, 0.04, -450, 1100)
                mel.connect_material_property(cc, "", unreal.MaterialProperty.MP_CUSTOM_DATA0)
                mel.connect_material_property(ccr, "", unreal.MaterialProperty.MP_CUSTOM_DATA1)
            except AttributeError:
                pass   # UE 5.8's Python has no MP_CUSTOM_DATA0/1; unconnected, the coat is 1.0 at roughness 0.1
        mel.recompile_material(m)
        unreal.EditorAssetLibrary.save_loaded_asset(m)
        masters[kind] = m
    tex = tex or {}
    roles = roles or {}
    scanned = {name: RIDER_TEX[name] for name in finishes
               if RIDER_TEX.get(name) in tex and "mean" in roles.get(RIDER_TEX[name], {})}
    if scanned:
        first = tex[next(iter(scanned.values()))]
        masters["scan"] = rider_scan_master(first)
    log("rider kit: scanned %s; plain %s" % (", ".join("%s=%s" % (k, roles[v]["id"]) for k, v in scanned.items()) or "none",
                                              ", ".join(k for k in finishes if k not in scanned) or "none"))
    # Leather and textiles are dielectrics with low specular; denim is very matt.
    spec = {"jeans": 0.3, "jacket": 0.45, "sole": 0.3, "liner": 0.2, "trim": 0.35}
    for name, f in finishes.items():
        kind = "gloss" if name in ("helmet", "visor") else "scan" if name in scanned else "kit"
        mi_name = "MI_Rider_" + name
        path = RIDER_MAT_DIR + "/" + mi_name
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
        mi = asset_tools.create_asset(mi_name, RIDER_MAT_DIR, unreal.MaterialInstanceConstant,
                                      unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, masters[kind])
        c = f["color"]
        if kind == "scan":
            r = roles[scanned[name]]
            maps = tex[scanned[name]]
            mel.set_material_instance_texture_parameter_value(mi, "ColorMap", maps["color"])
            if "normal" in maps:
                mel.set_material_instance_texture_parameter_value(mi, "NormalMap", maps["normal"])
            if "rough" in maps:
                mel.set_material_instance_texture_parameter_value(mi, "RoughMap", maps["rough"])
            mean = r["mean"]
            tint = [c[i] / max(mean[i], 1e-4) for i in range(3)]
            mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
            mel.set_material_instance_scalar_parameter_value(mi, "Repeats", 1.0 / float(r["tile_m"]))
            mel.set_material_instance_scalar_parameter_value(mi, "Bump", float(r.get("bump", 1.0)))
            mel.set_material_instance_scalar_parameter_value(
                mi, "RoughScale", float(f["roughness"]) / max(float(r.get("rough_mean", 0.7)), 0.05))
        else:
            mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(c[0], c[1], c[2], 1.0))
            mel.set_material_instance_scalar_parameter_value(mi, "Roughness", float(f["roughness"]))
        mel.set_material_instance_scalar_parameter_value(mi, "Specular", float(spec.get(name, 0.5)))
        unreal.EditorAssetLibrary.save_loaded_asset(mi)
        paths[name] = mi.get_path_name()
    return paths


def rider_build_settings(body):
    """The kit is big, smooth and dark, where 8-bit normals and tangents band into blotches: keep the
    normals make_rider.py wrote, build the tangent frame from its UVs (MikkTSpace), and store it at
    high precision. Logged either way, to check from the PC's log."""
    try:
        sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
        bs = sub.get_lod_build_settings(body, 0)
        try_set(bs, "recompute_normals", False)
        try_set(bs, "recompute_tangents", True)
        try_set(bs, "use_mikk_t_space", True)
        try_set(bs, "use_high_precision_tangent_basis", True)
        sub.set_lod_build_settings(body, 0, bs)
        unreal.EditorAssetLibrary.save_loaded_asset(body)
        bs = sub.get_lod_build_settings(body, 0)
        log("rider build: recompute normals %s, tangents %s, mikk %s, high precision basis %s" % tuple(
            bs.get_editor_property(k) for k in ("recompute_normals", "recompute_tangents", "use_mikk_t_space",
                                                "use_high_precision_tangent_basis")))
    except Exception as e:
        unreal.log_warning("[Torque] rider build settings not set: %s" % e)


def import_rider(tex=None, roles=None):
    """The rider (Tools/make_rider.py): rider.glb as a skeletal mesh, helmet.glb as a static mesh, and
    the kit's materials. Returns what the pawn needs, or None."""
    folder = os.path.join(os.path.abspath(unreal.Paths.project_dir()), "Data", "Rider")
    info_path = os.path.join(folder, "rider.json")
    if not os.path.exists(info_path):
        unreal.log_warning("[Torque] no rider in %s (python Tools/make_rider.py); the rider stays as shapes" % folder)
        return None
    info = json.load(open(info_path))
    tasks = []
    for f in ("rider.glb", "helmet.glb"):
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", os.path.join(folder, f))
        t.set_editor_property("destination_path", RIDER_DEST)
        t.set_editor_property("destination_name", os.path.splitext(f)[0])
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        tasks.append(t)
    log("importing the rider...")
    asset_tools.import_asset_tasks(tasks)
    body = helmet = None
    for p in unreal.EditorAssetLibrary.list_assets(RIDER_DEST, recursive=True):
        a = unreal.load_asset(p)
        if isinstance(a, unreal.SkeletalMesh) and "rider" in a.get_name().lower():
            body = a
        elif isinstance(a, unreal.StaticMesh) and "helmet" in a.get_name().lower():
            helmet = a
    if not body:
        unreal.log_warning("[Torque] rider.glb didn't import as a skeletal mesh; the rider stays as shapes")
        return None
    rider_build_settings(body)
    if helmet:
        nanite_off(helmet)
        unreal.EditorAssetLibrary.save_loaded_asset(helmet)
    mats = rider_materials(info["finishes"], tex, roles)
    log("rider: %s, helmet %s" % (body.get_path_name(), helmet.get_path_name() if helmet else "missing"))
    out = {"mesh": body.get_path_name(), "helmet": helmet.get_path_name() if helmet else "", "materials": mats}
    fab = fab_helmet(helmet) if helmet else None
    if fab:
        out["helmet"], out["helmet_fit"] = fab
        FAB_HELMET_USED[:] = [fab[0]]
    return out


def place_fab_helmet_anchor():
    """The Fab helmet is loaded by path at run time and lives outside the always-cooked folders, so a
    hidden actor in the ride's map holds it to get it (and only it, with its materials) cooked."""
    for path in FAB_HELMET_USED:
        m = unreal.load_asset(path)
        if isinstance(m, unreal.StaticMesh):
            a = actors.spawn_actor_from_object(m, unreal.Vector(0, 0, -100000), unreal.Rotator(0, 0, 0))
        elif isinstance(m, unreal.SkeletalMesh):
            a = actors.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, -100000))
            a.get_component_by_class(unreal.SkeletalMeshComponent).set_skeletal_mesh_asset(m)
        else:
            unreal.log_warning("[Torque] fab helmet: no mesh at %s to anchor" % path)
            continue
        a.set_actor_label("CookAnchor_FabHelmet")
        a.set_folder_path("CookAnchors")
        a.set_actor_hidden_in_game(True)
        a.set_actor_enable_collision(False)
        log("fab helmet: cook anchor for %s in the ride's map" % path)


FAB_HELMET_DEST = "/Game/Fab/RiderHelmet"
# Where a .mhpkg helmet goes: outside every DirectoriesToAlwaysCook folder, because the package brings in a
# MetaHumanWardrobeItem (editor-only), which fails the cook (round 133). Only the helmet mesh the rider
# wears is cooked, through a hidden anchor in the ride's map (place_fab_helmet_anchor). The round 133
# copy in /Game/Fab/RiderHelmetPkg is in DirectoriesToNeverCook.
FAB_HELMET_PKG = "/Game/RiderHelmetPkg"
FAB_HELMET_USED = []   # the Fab helmet mesh the rider wears this build (for the anchor)


HELMET_SKIP_PATHS = ("/Game/MetaHumans/Avatar_", "/Game/MetaHumanSources", RIDER_DEST + "/", FAB_HELMET_DEST + "/",
                     "/Game/Fab/RiderHelmetPkg/")   # round 133's import, never cooked


def _helmet_assets(words, skip):
    """Static or skeletal meshes in the project whose name has one of words (not ours, not the avatars')."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    found = []
    for a in reg.get_assets_by_path("/Game", recursive=True):
        cls = str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)
        pkg, name = str(a.package_name), str(a.asset_name)
        if cls not in ("StaticMesh", "SkeletalMesh") or not any(w.lower() in name.lower() for w in words):
            continue
        if any(k in pkg for k in skip) or any(pkg.startswith(k) for k in HELMET_SKIP_PATHS):
            continue
        found.append(("%s.%s" % (pkg, name), cls))
    return found


def _import_mhpkg(path):
    """A MetaHuman package (.mhpkg, what Fab gives for MetaHuman wardrobe items) into the project,
    through the MetaHuman plugin's own factory. Logs everything it brought in."""
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", path)
    t.set_editor_property("destination_path", FAB_HELMET_PKG)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    factory = getattr(unreal, "MetaHumanPackageFactory", None)
    if factory is not None:
        try:
            t.set_editor_property("factory", factory())
        except Exception as e:
            unreal.log_warning("[Torque] fab helmet: couldn't use MetaHumanPackageFactory: %s" % e)
    log("fab helmet: importing the MetaHuman package %s (factory %s)" % (path, "MetaHumanPackageFactory" if factory else "MISSING, the importer picks"))
    try:
        asset_tools.import_asset_tasks([t])
    except Exception as e:
        unreal.log_warning("[Torque] fab helmet: package import failed: %s" % e)
    got = list(t.get_editor_property("imported_object_paths") or [])
    log("fab helmet: the package brought in %d: %s" % (len(got), ", ".join(str(g) for g in got[:40]) or "nothing"))
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Game"], True)
    if not got:
        import zipfile
        if zipfile.is_zipfile(path):
            names = zipfile.ZipFile(path).namelist()
            log("fab helmet: the package is a zip of %d: %s" % (len(names), ", ".join(names[:40])))
        else:
            log("fab helmet: the package isn't a zip; it starts %s" % open(path, "rb").read(16).hex())
    return got


def _best_helmet(found):
    """Static before skeletal, then the full-detail mesh (LOD0, or no LOD in its name) before LOD1/LOD2."""
    def key(f):
        n = f[0].rsplit("/", 1)[-1]
        return (f[1] != "StaticMesh", "LOD" in n and "LOD0" not in n, len(f[0]), f[0])
    return sorted(found, key=key)[0][0]


def _fab_helmet_source(spec):
    """beenster's Fab helmet: a static or skeletal mesh already in the project, else a file in the Fab
    cache (a MetaHuman package is imported first, then looked for again)."""
    words = spec.get("keywords") or ["helmet"]
    skip = spec.get("skip_keywords") or []
    found = _helmet_assets(words, skip)
    log("fab helmet: in the project: %s" % (", ".join("%s (%s)" % f for f in found) or "none"))
    if found:
        return "asset", _best_helmet(found)
    vault = os.environ.get("TORQUE_FAB_VAULT") or r"C:\ProgramData\Epic\EpicGamesLauncher\VaultCache\FabLibrary"
    roots = [vault, os.path.dirname(vault)]
    if spec.get("vault"):
        f = os.path.join(vault, *spec["vault"].split("/"))
        return ("file", f) if os.path.isfile(f) else (None, None)
    dirs = []
    for r in roots:
        if not os.path.isdir(r):
            log("fab helmet: no folder %s" % r)
            continue
        for d in os.listdir(r):
            full = os.path.join(r, d)
            if os.path.isdir(full) and any(w.lower() in d.lower() for w in words):
                dirs.append(full)
    log("fab helmet: in the Fab cache: %s" % (", ".join(dirs) or "none"))
    for d in sorted(dirs, key=os.path.getmtime, reverse=True):
        files = []
        for root, _, names in os.walk(d):
            for n in names:
                files.append(os.path.join(root, n))
        log("fab helmet: %s holds %s" % (d, ", ".join("%s (%d KB)" % (os.path.relpath(f, d), os.path.getsize(f) // 1024) for f in files[:40])))
        for ext in (".glb", ".gltf", ".fbx", ".obj"):
            pick = [f for f in files if f.lower().endswith(ext)]
            if pick:
                return "file", max(pick, key=os.path.getsize)
        pkgs = [f for f in files if f.lower().endswith(".mhpkg")]
        if pkgs:
            _import_mhpkg(max(pkgs, key=os.path.getsize))
            found = _helmet_assets(words, skip)
            if not found:   # named for what it is rather than 'helmet': anything the package brought in
                reg = unreal.AssetRegistryHelpers.get_asset_registry()
                for a in reg.get_assets_by_path(FAB_HELMET_PKG, recursive=True):
                    cls = str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)
                    if cls in ("StaticMesh", "SkeletalMesh"):
                        found.append(("%s.%s" % (a.package_name, a.asset_name), cls))
            log("fab helmet: after the package: %s" % (", ".join("%s (%s)" % f for f in found) or "none"))
            if found:
                # The full-detail mesh: LOD0 (or no LOD in its name) before LOD1/LOD2.
                return "asset", _best_helmet(found)
    return None, None


def _box(mesh):
    """A static or skeletal mesh's local box, as (min, max)."""
    if isinstance(mesh, unreal.StaticMesh):
        b = mesh.get_bounding_box()
        return b.min, b.max
    bs = mesh.get_bounds() if hasattr(mesh, "get_bounds") else mesh.get_imported_bounds()
    return bs.origin - bs.box_extent, bs.origin + bs.box_extent


def fab_helmet(ours):
    """beenster's Fab helmet (Data/Rider/fab_helmet.json) over our helmet: its mesh path and the fit
    (rotate, scale, offset) the pawn puts it on with, or None to keep ours."""
    path = os.path.join(os.path.abspath(unreal.Paths.project_dir()), "Data", "Rider", "fab_helmet.json")
    if not os.path.isfile(path):
        return None
    spec = json.load(open(path))
    kind, src = _fab_helmet_source(spec)
    mesh = None
    if kind == "asset":
        mesh = unreal.load_asset(src)
    elif kind == "file":
        if unreal.EditorAssetLibrary.does_directory_exist(FAB_HELMET_DEST):
            unreal.EditorAssetLibrary.delete_directory(FAB_HELMET_DEST)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", src)
        t.set_editor_property("destination_path", FAB_HELMET_DEST)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        asset_tools.import_asset_tasks([t])
        meshes = [a for a in (unreal.load_asset(p) for p in unreal.EditorAssetLibrary.list_assets(FAB_HELMET_DEST, recursive=True))
                  if isinstance(a, unreal.StaticMesh)]
        log("fab helmet: imported %s: static meshes %s" % (src, ", ".join(m.get_name() for m in meshes) or "NONE"))
        if meshes:
            def size(m):
                lo, hi = _box(m)
                return (hi - lo).length()
            mesh = max(meshes, key=size)
            if len(meshes) > 1:
                unreal.log_warning("[Torque] fab helmet: %d meshes imported (not combined); using the biggest, %s" % (len(meshes), mesh.get_name()))
    if mesh is None:
        log("fab helmet: not found; the rider keeps our own helmet")
        return None
    if isinstance(mesh, unreal.StaticMesh):
        nanite_off(mesh)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    # Fit by bounding boxes: theirs turned by 'rotate', scaled to our helmet's largest size, centred on ours.
    rot = [float(v) for v in spec.get("rotate", [0, 0, 0])]
    q = unreal.Rotator(roll=rot[2], pitch=rot[0], yaw=rot[1]).quaternion()
    bmin, bmax = _box(mesh)
    corners = [q.rotate_vector(unreal.Vector(x, y, z)) for x in (bmin.x, bmax.x) for y in (bmin.y, bmax.y) for z in (bmin.z, bmax.z)]
    lo = unreal.Vector(min(c.x for c in corners), min(c.y for c in corners), min(c.z for c in corners))
    hi = unreal.Vector(max(c.x for c in corners), max(c.y for c in corners), max(c.z for c in corners))
    ob = ours.get_bounding_box()
    ours_size, theirs_size = ob.max - ob.min, hi - lo
    scale = float(spec.get("scale", 1.0))
    if spec.get("fit_size", True):   # sized to ours; false keeps its own size (a real-size MetaHuman helmet)
        scale *= max(ours_size.x, ours_size.y, ours_size.z) / max(theirs_size.x, theirs_size.y, theirs_size.z, 1e-3)
    oc, tc = (ob.max + ob.min) * 0.5, (hi + lo) * 0.5
    off = [float(v) for v in spec.get("offset", [0, 0, 0])]
    offset = [oc.x - tc.x * scale + off[0], oc.y - tc.y * scale + off[1], oc.z - tc.z * scale + off[2]]
    log("fab helmet: %s; its box %s (turned %s) vs ours %s (%s); fit scale %.4f, offset %s; materials %s" % (
        mesh.get_path_name(), [round(v, 1) for v in (theirs_size.x, theirs_size.y, theirs_size.z)], rot,
        [round(v, 1) for v in (ours_size.x, ours_size.y, ours_size.z)], ours.get_path_name(), scale, [round(v, 1) for v in offset],
        ", ".join("%s=%s" % (m.get_editor_property("material_slot_name"), m.get_editor_property("material_interface").get_path_name()
                                  if m.get_editor_property("material_interface") else "none")
                  for m in mesh.get_editor_property("static_materials" if isinstance(mesh, unreal.StaticMesh) else "materials"))))
    return mesh.get_path_name(), {"rotate": rot, "scale": scale, "offset": offset}


def check_bike(meshes, mats):
    """Log what the bike's look depends on, so a build on the PC can be checked from the log: the UV
    channels the tank came in with (2 means the finish mask arrived) and what its paint instance holds."""
    sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for name in ("tank", "cylinders", "silencers"):
        mesh = meshes.get(name)
        if not mesh:
            continue
        try:
            log("bike check: %s has %d UV channels, %d vertices" % (name, sme.get_num_uv_channels(mesh, 0),
                                                                    sme.get_number_verts(mesh, 0)))
        except Exception as e:
            unreal.log_warning("[Torque] bike check: couldn't read %s: %s" % (name, e))
    for fin in ("paint_silver", "steel_brushed"):
        mi = unreal.load_asset(mats.get(fin, "")) if mats.get(fin) else None
        if not mi:
            continue
        try:
            c = mel.get_material_instance_vector_parameter_value(mi, "Color")
            c2 = mel.get_material_instance_vector_parameter_value(mi, "Color2")
            d = mel.get_material_instance_texture_parameter_value(mi, "DetailMap")
            log("bike check: %s parent %s, Color (%.2f %.2f %.2f), Color2 (%.2f %.2f %.2f), Metallic %.2f, DetailMap %s"
                % (fin, mi.get_editor_property("parent").get_name(), c.r, c.g, c.b, c2.r, c2.g, c2.b,
                   mel.get_material_instance_scalar_parameter_value(mi, "Metallic"), d.get_name() if d else "none"))
        except Exception as e:
            unreal.log_warning("[Torque] bike check: couldn't read %s: %s" % (fin, e))


def import_bike(frame, tex=None, roles=None):
    """The Speed Twin's parts (Tools/make_speedtwin.py) and a list of where they landed, for the pawn."""
    folder = os.path.join(os.path.abspath(unreal.Paths.project_dir()), "Data", "Bike", "SpeedTwin")
    if not os.path.isdir(folder):
        unreal.log_warning("[Torque] no bike parts in %s; the bike stays as placeholder shapes" % folder)
        return
    info = json.load(open(os.path.join(folder, "speedtwin.json")))
    meshes = import_glbs(folder, BIKE_DEST)
    # The alternative parts the fitment system can fit (Tools/make_speedtwin_fit.py), with their finishes.
    options = {}
    fit_path = os.path.join(folder, "fitment.json")
    if os.path.isfile(fit_path) and os.path.isdir(os.path.join(folder, "Options")):
        for slot_opts in json.load(open(fit_path)).get("options", {}).values():
            for opt in slot_opts:
                for name, m in opt.get("meshes", {}).items():
                    options[name] = m.get("finish", "")
        # A loose catalogue part's stand-in (scaled and tinted by Fit/TorqueFit.cpp).
        for ph in json.load(open(fit_path)).get("placeholders", {}).values():
            options[ph["mesh"]] = ph.get("finish", "")
        meshes.update(import_glbs(os.path.join(folder, "Options"), BIKE_DEST + "/Options"))
    sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for mesh in meshes.values():
        nanite_off(mesh)
        try:
            # Keep the file's normals and both UV channels: a generated lightmap UV would land on
            # channel 1, where the finish mask is (Lumen needs no lightmaps).
            s = sme.get_lod_build_settings(mesh, 0)
            s.set_editor_property("recompute_normals", False)
            s.set_editor_property("recompute_tangents", True)
            s.set_editor_property("generate_lightmap_u_vs", False)
            sme.set_lod_build_settings(mesh, 0, s)
        except Exception as e:
            unreal.log_warning("[Torque] build settings not changed for %s: %s" % (mesh.get_name(), e))
        body = mesh.get_editor_property("body_setup")
        if body:
            body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    finishes = dict(info["finishes"])
    if os.path.isfile(fit_path):
        finishes.update(json.load(open(fit_path)).get("finishes", {}))   # the part pack's (speedtwin_pack.py)
    cat_index = os.path.join(os.path.abspath(unreal.Paths.project_dir()), "Data", "Parts", "Blender", "index.json")
    if os.path.isfile(cat_index):
        # The catalogue parts' own (import_blender_parts.py CATALOGUE_FINISHES: cat_*), only adding.
        for name, f in json.load(open(cat_index)).get("finishes", {}).items():
            finishes.setdefault(name, f)
    finishes.update(RIDER_FINISHES)
    mats = bike_materials(finishes)
    check_bike(meshes, mats)
    assets = {"frame": {"yaw": frame[0], "mirror": frame[1], "scale": frame[2]}, "parts": {},
              "rider": {k[len("rider_"):]: mats[k] for k in RIDER_FINISHES}}
    rig = import_rider(tex, roles)
    if rig:
        assets["rider_rig"] = rig
    for part in info["parts"]:
        mesh = meshes.get(part["name"])
        if mesh:
            assets["parts"][part["name"]] = {"mesh": mesh.get_path_name(), "material": mats.get(part["finish"], "")}
    for name, fin in sorted(options.items()):
        mesh = meshes.get(name)
        if mesh:
            assets["parts"][name] = {"mesh": mesh.get_path_name(), "material": mats.get(fin, "")}
    with open(os.path.join(folder, "ue_assets.json"), "w") as f:
        json.dump(assets, f, indent=1)
    log("bike: %d parts imported" % len(assets["parts"]))


# --- Ground cover (Tools/make_foliage.py) ------------------------------------------------------------

FOLIAGE_DEST = "/Game/Buttertubs/Foliage"


def foliage_wind(g, h, pir):
    """World position offset for a plant: `h` is how far up the plant a vertex is, in metres (or how
    free it is to move). Two waves and slow gusts rolling across the moor from the south west."""
    amp = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, h, h), const_b=20.0)   # cm: 5 at 0.5 m, 20 at 1 m
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    along = g.op(unreal.MaterialExpressionDotProduct, wp, g.const((0.0035, 0.0028, 0.0)))   # wave fronts
    t = g.node(unreal.MaterialExpressionTime)
    ph1 = g.op(unreal.MaterialExpressionSubtract, g.op(unreal.MaterialExpressionMultiply, t, const_b=0.9), along)
    ph2 = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionSubtract,
               g.op(unreal.MaterialExpressionMultiply, t, const_b=2.3), g.op(unreal.MaterialExpressionMultiply, along, const_b=2.1)), pir)
    s1 = g.node(unreal.MaterialExpressionSine, period=1.0); g.link(ph1, s1)
    s2 = g.node(unreal.MaterialExpressionSine, period=1.0); g.link(ph2, s2)
    sway = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, s1, const_b=0.6),
                g.op(unreal.MaterialExpressionMultiply, s2, const_b=0.25))
    sway = g.op(unreal.MaterialExpressionAdd, sway, const_b=0.55)              # leans downwind on average
    gph = g.op(unreal.MaterialExpressionSubtract, g.op(unreal.MaterialExpressionMultiply, t, const_b=0.12),
               g.op(unreal.MaterialExpressionMultiply, along, const_b=0.08))
    gs = g.node(unreal.MaterialExpressionSine, period=1.0); g.link(gph, gs)
    gust = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, gs, const_b=0.4), const_b=0.75)
    mag = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, amp, sway), gust)
    mag = g.op(unreal.MaterialExpressionMultiply, mag, weather_param(g, "Wind"))   # the real wind's strength
    return g.op(unreal.MaterialExpressionMultiply, wind_dir(g, -0.08), mag)        # downwind (north east by default), dipping a little


def foliage_flags(m, masked=False):
    m.set_editor_property("two_sided", True)
    try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    try_set(m, "used_with_instanced_static_meshes", True)
    try_set(m, "used_with_nanite", True)
    if masked:
        try_set(m, "blend_mode", unreal.BlendMode.BLEND_MASKED)
        try_set(m, "opacity_mask_clip_value", 0.4)


def ground_rvt():
    """A runtime virtual texture of the ground's colour, so the plants can take on the colour of the
    ground they grow from: RVT_Ground (base colour only), M_Land writing to it, a volume over the near
    land tiles (not the far hills), and the tiles drawing into it. None if this engine can't."""
    try:
        path = FOLIAGE_DEST + "/RVT_Ground"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
        rvt = asset_tools.create_asset("RVT_Ground", FOLIAGE_DEST, unreal.RuntimeVirtualTexture,
                                       unreal.RuntimeVirtualTextureFactory())
        try_set(rvt, "material_type", unreal.RuntimeVirtualTextureMaterialType.BASE_COLOR)
        unreal.EditorAssetLibrary.save_loaded_asset(rvt)
        # M_Land: send whatever feeds its base colour to the virtual texture too.
        land = unreal.load_asset(MAT_DIR + "/M_Land")
        src = mel.get_material_property_input_node(land, unreal.MaterialProperty.MP_BASE_COLOR)
        try:
            src_out = mel.get_material_property_input_node_output_name(land, unreal.MaterialProperty.MP_BASE_COLOR)
        except Exception:
            src_out = ""
        out = mel.create_material_expression(land, unreal.MaterialExpressionRuntimeVirtualTextureOutput, 400, 600)
        if not mel.connect_material_expressions(src, src_out or "", out, "BaseColor"):
            raise RuntimeError("couldn't wire M_Land's base colour to the virtual texture output")
        mel.recompile_material(land)
        unreal.EditorAssetLibrary.save_loaded_asset(land)
        # The volume: over the near tiles, in Unreal's frame (100x, 100z, 100y).
        route = json.load(open(os.path.join(data_dir(), "route.json")))
        t = route["tiles"]
        x0 = min(r["min"][0] for r in t) * 100; x1 = max(r["max"][0] for r in t) * 100
        y0 = min(r["min"][2] for r in t) * 100; y1 = max(r["max"][2] for r in t) * 100
        z0 = min(r["min"][1] for r in t) * 100 - 5000; z1 = max(r["max"][1] for r in t) * 100 + 5000
        vol = spawn(unreal.RuntimeVirtualTextureVolume, (x0, y0, z0))
        vol.set_actor_scale3d(unreal.Vector(x1 - x0, y1 - y0, z1 - z0))
        vol.set_actor_label("GroundColourRVT")
        vol.set_folder_path("Detail")
        vol.get_component_by_class(unreal.RuntimeVirtualTextureComponent).set_editor_property("virtual_texture", rvt)
        n = 0
        for a in actors.get_all_level_actors():
            if a.get_actor_label().startswith("terrain_"):
                c = a.get_component_by_class(unreal.StaticMeshComponent)
                c.set_editor_property("runtime_virtual_textures", [rvt])
                n += 1
        b = vol.get_actor_bounds(False)
        log("ground colour virtual texture over %d tiles, volume centre %s extent %s" % (n, b[0], b[1]))
        return rvt
    except Exception as e:
        unreal.log_warning("[Torque] no ground colour for the plants (%s); they keep their own colour" % e)
        return None


def ground_blend(g, base, h, rvt, strength):
    """Pull a plant's colour towards the ground's under it (from the virtual texture): most at the
    root, a little at the tips, so the grass grows out of the ground rather than sitting on it."""
    if rvt is None or strength <= 0:
        return base
    gc = g.node(unreal.MaterialExpressionRuntimeVirtualTextureSample, -900, virtual_texture=rvt,
                material_type=unreal.RuntimeVirtualTextureMaterialType.BASE_COLOR)
    f = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionDivide, h, const_b=0.35))
    w = g.lerp(g.const(0.75 * strength), g.const(0.22 * strength), f)
    return g.lerp(base, gc, w, b_out="BaseColor")


def grade_green(g, base, value=0.65, sat=1.25, blue=0.4):
    """Grass blades and cards, matched to beenster's DJI footage (grade-2 stats: near grass about 1.4x
    too bright and twice too blue): darker, a little richer, blue at most 0.4 of green."""
    base = g.op(unreal.MaterialExpressionMultiply, base, const_b=value)
    grey = g.node(unreal.MaterialExpressionDesaturation, -600)
    g.link(base, grey)
    g.link(g.const(1.0), grey, "Fraction")
    base = g.lerp(grey, base, g.const(sat))
    rg = g.node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=False, a=False)
    g.link(base, rg)
    bb = g.node(unreal.MaterialExpressionComponentMask, r=False, g=False, b=True, a=False)
    g.link(base, bb)
    gg = g.node(unreal.MaterialExpressionComponentMask, r=False, g=True, b=False, a=False)
    g.link(base, gg)
    cap = g.op(unreal.MaterialExpressionMin, bb, g.op(unreal.MaterialExpressionMultiply, gg, const_b=blue))
    return g.op(unreal.MaterialExpressionAppendVector, rg, cap)


def foliage_material(rvt=None, blend=1.0, name="M_Foliage"):
    """Our plants: vertex colour (a little varied per plant), two-sided foliage shading so the sun
    glows through the blades, ambient occlusion from the vertex alpha, and wind: UV y is how far each
    vertex may move (its height in metres, less for stiff plants)."""
    g = Graph(new_material(name))
    m = g.m
    foliage_flags(m)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    pir = g.node(unreal.MaterialExpressionPerInstanceRandom)
    tint = g.lerp(g.const((0.86, 0.9, 0.8)), g.const((1.12, 1.06, 0.92)), pir)
    base = g.op(unreal.MaterialExpressionMultiply, vc, tint)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    h = g.node(unreal.MaterialExpressionComponentMask, r=False, g=True, b=False, a=False)
    g.link(uv, h)
    base = grade_green(g, base)
    base = ground_blend(g, base, h, rvt, blend)
    sss = g.op(unreal.MaterialExpressionMultiply, base, g.const((0.75, 0.9, 0.35)))
    wpo = foliage_wind(g, h, pir)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    mel.connect_material_property(vc, "A", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    # Dry moor grass is matte: high roughness, low specular (no sheen into the sun).
    mel.connect_material_property(g.const(0.92), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(g.const(0.1), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


# Scanned plants (Megascans): their texture maps, by the suffix of the texture's name.
SCAN_MAPS = {   # map: (name suffixes, sampler, sRGB)
    "BaseColor": (("_BaseColor", "_Albedo"), "color", True),
    "Normal": (("_Normal",), "normal", False),
    "Roughness": (("_Roughness",), "linear", False),
    "Opacity": (("_Opacity",), "linear", False),
    "Translucency": (("_Translucency",), "color", True),
    "AO": (("_AO",), "linear", False),
}
SAMPLERS = {"color": "SAMPLERTYPE_COLOR", "normal": "SAMPLERTYPE_NORMAL", "linear": "SAMPLERTYPE_LINEAR_COLOR"}


def scan_textures(mesh):
    """The maps sitting beside a scanned plant's mesh (Fab puts them in the same folder), set up to
    match the master's samplers: colour maps sRGB, data maps linear, the normal map as a normal map."""
    folder = mesh.get_path_name().rsplit("/", 1)[0]
    found = {}
    for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False):
        name = path.rsplit("/", 1)[-1].split(".")[0]
        for key, (sfx, kind, srgb) in SCAN_MAPS.items():
            if key in found or not any(name.endswith(x) for x in sfx):
                continue
            tex = unreal.load_asset(path)
            if not isinstance(tex, unreal.Texture2D):
                continue
            changed = False
            if tex.get_editor_property("srgb") != srgb:
                tex.set_editor_property("srgb", srgb); changed = True
            want = (unreal.TextureCompressionSettings.TC_NORMALMAP if kind == "normal"
                    else unreal.TextureCompressionSettings.TC_DEFAULT)
            if tex.get_editor_property("compression_settings") != want:
                tex.set_editor_property("compression_settings", want); changed = True
            if changed:
                unreal.EditorAssetLibrary.save_loaded_asset(tex)
            found[key] = tex
    return found


def scan_foliage_master(defaults, rvt=None, blend=1.0):
    """Masked two-sided foliage for scanned plants, its maps as parameters (one instance per plant set),
    the same wind as ours with the sway weighted by height above the plant's base."""
    g = Graph(new_material("M_FoliageScan"))
    m = g.m
    foliage_flags(m, masked=True)
    pir = g.node(unreal.MaterialExpressionPerInstanceRandom)

    def tex(key):
        if key not in defaults:
            return None
        kind = SCAN_MAPS[key][1]
        e = g.node(unreal.MaterialExpressionTextureSampleParameter2D, -900, parameter_name=key,
                   texture=defaults[key], sampler_type=getattr(unreal.MaterialSamplerType, SAMPLERS[kind]),
                   sampler_source=unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        return e
    col = tex("BaseColor")
    tint = g.lerp(g.const((0.88, 0.9, 0.84)), g.const((1.1, 1.05, 0.95)), pir)
    gtint = g.node(unreal.MaterialExpressionVectorParameter, parameter_name="Tint",
                   default_value=unreal.LinearColor(1, 1, 1, 1))
    base = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, col, tint), gtint, b_out="")
    # Height above the plant's base, in metres, from its own (local) vertex position.
    lp = g.node(unreal.MaterialExpressionLocalPosition)
    z = g.node(unreal.MaterialExpressionComponentMask, r=False, g=False, b=True, a=False)
    g.link(lp, z)
    h = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMax, z, const_b=0.0), const_b=0.01)
    base = grade_green(g, base)
    base = ground_blend(g, base, h, rvt, blend)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    op = tex("Opacity")
    if op:
        mel.connect_material_property(op, "R", unreal.MaterialProperty.MP_OPACITY_MASK)
    nrm = tex("Normal")
    if nrm:
        mel.connect_material_property(nrm, "RGB", unreal.MaterialProperty.MP_NORMAL)
    rough = tex("Roughness")
    # A floor on the scan's roughness and low specular: dry moor grass is matte.
    rough = g.op(unreal.MaterialExpressionMax, rough, g.const(0.9), a_out="R") if rough else g.const(0.92)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    ao = tex("AO")
    if ao:
        mel.connect_material_property(ao, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    tr = tex("Translucency")
    sss = g.op(unreal.MaterialExpressionMultiply, tr if tr else base, g.const((0.8, 0.9, 0.5)))
    mel.connect_material_property(sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    mel.connect_material_property(g.const(0.1), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(foliage_wind(g, h, pir), "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def scan_instance(master, mesh, maps, tint=None):
    """A material instance of M_FoliageScan with one plant set's maps, kept beside our materials."""
    folder = mesh.get_path_name().rsplit("/", 1)[0]
    name = "MI_" + folder.rsplit("/", 1)[-1]
    path = FOLIAGE_DEST + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    mi = asset_tools.create_asset(name, FOLIAGE_DEST, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, master)
    for key, t in maps.items():
        mel.set_material_instance_texture_parameter_value(mi, key, t)
    if tint:
        mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    return mi


def footprint(mesh):
    """Half the mean of a mesh's width and depth (cm)."""
    b = mesh.get_bounding_box()
    return max(0.25 * ((b.max.x - b.min.x) + (b.max.y - b.min.y)), 1.0)


def foliage_nanite(mesh):
    """Nanite on (the plants are real geometry), keeping the blades' coverage as they simplify with
    distance, and no collision."""
    try:
        ns = mesh.get_editor_property("nanite_settings")
        ns.set_editor_property("enabled", True)
        if not (hasattr(unreal, "NaniteShapePreservation")
                and try_set(ns, "shape_preservation", unreal.NaniteShapePreservation.PRESERVE_AREA)):
            try_set(ns, "preserve_area", True)
        try:
            unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).set_nanite_settings(mesh, ns, True)
        except Exception:
            mesh.set_editor_property("nanite_settings", ns)
    except Exception as e:
        unreal.log_warning("[Torque] couldn't set Nanite on %s: %s" % (mesh.get_name(), e))
    try:
        unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).remove_collisions(mesh)
    except Exception:
        pass
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)


def place_foliage(frame):
    """The ground cover: import the plants, and a TorqueFoliage actor that plants them at BeginPlay
    from Data/Buttertubs/Foliage/instances.bin. A species can use scanned plants instead (Megascans from
    Fab, say) through Data/Buttertubs/Foliage/overrides.json:

        {"tuft_a": {"meshes": ["/Game/Fab/.../Wild_Grass_..._VarA_LOD0", ...],
                    "scale": 1.0, "keep": null, "tint": [1, 1, 1]}}

    Each plant of the species takes one of the meshes. The meshes get Nanite and a masked foliage
    material made from the textures beside them (M_FoliageScan + one instance per set). "keep" is the
    share of the species' plants kept; left out, it is worked out from how much bigger the scanned
    plant is than ours, so the ground is covered about as densely."""
    folder = os.path.join(data_dir(), "Foliage")
    spec_path = os.path.join(folder, "foliage.json")
    if not os.path.exists(spec_path):
        unreal.log_warning("[Torque] no foliage (python Tools/make_foliage.py)")
        return
    if not hasattr(unreal, "TorqueFoliage"):
        unreal.log_warning("[Torque] the TorqueFoliage class isn't built yet: rebuild the editor, then run this again")
        return
    spec = json.load(open(spec_path))
    over_path = os.path.join(folder, "overrides.json")
    overrides = json.load(open(over_path)) if os.path.exists(over_path) else {}
    # "_ground_blend": how strongly the plants take the ground's colour (0 turns it off).
    blend = overrides.get("_ground_blend", 1.0)
    blend = float(blend) if isinstance(blend, (int, float)) else 1.0
    overrides = {k: v for k, v in overrides.items() if not k.startswith("_")}
    meshes = import_glbs(folder, FOLIAGE_DEST)
    rvt = ground_rvt() if blend > 0 else None
    mat = foliage_material(rvt, blend)
    mats = {1.0: mat}   # by ground blend: heather keeps its own dark colour (foliage.json "ground_blend")
    # Scanned plants: load them and their maps first, so the master gets defaults of the right kinds.
    scans = {}
    for name, o in overrides.items():
        paths = o.get("meshes") or ([o["mesh"]] if "mesh" in o else [])
        got = []
        for p in paths:
            m_ = unreal.load_asset(p)
            if isinstance(m_, unreal.StaticMesh):
                got.append(m_)
            else:
                unreal.log_warning("[Torque] %s: no static mesh at %s" % (name, p))
        if got:
            scans[name] = got
        else:
            unreal.log_warning("[Torque] %s: none of its scanned meshes loaded; using ours" % name)
    sets = {}   # folder: (maps, material instance)
    master = None
    for name, ms in scans.items():
        for m_ in ms:
            f = m_.get_path_name().rsplit("/", 1)[0]
            if f not in sets:
                sets[f] = [scan_textures(m_), None]
    if sets:
        defaults = {}
        for maps, _ in sets.values():
            for k, v in maps.items():
                defaults.setdefault(k, v)
        if "BaseColor" in defaults:
            master = scan_foliage_master(defaults, rvt, blend)
        else:
            unreal.log_warning("[Torque] the scanned plants have no BaseColor maps beside them; using ours")
            scans = {}
    species = []
    for s in spec["species"]:
        e = unreal.TorqueFoliageSpecies()
        e.set_editor_property("name", s["name"])
        ours = meshes.get(s["name"])
        if s["name"] in scans and master:
            o = overrides[s["name"]]
            variants, vmats = [], []
            for m_ in scans[s["name"]]:
                foliage_nanite(m_)
                st = sets[m_.get_path_name().rsplit("/", 1)[0]]
                if st[1] is None:
                    st[1] = scan_instance(master, m_, st[0], o.get("tint"))
                variants.append(m_); vmats.append(st[1])
            e.set_editor_property("variants", variants)
            e.set_editor_property("variant_materials", vmats)
            e.set_editor_property("native_mesh", True)
            k = float(o.get("scale", 1.0))
            e.set_editor_property("scale_multiplier", k)
            keep = o.get("keep")
            if keep is None and ours is not None:
                theirs = sum(footprint(m_) for m_ in variants) / len(variants) * k
                mine = footprint(ours) * abs(frame[2])
                keep = min(1.0, max(0.12, (mine / theirs) ** 2 * 1.3))
            e.set_editor_property("keep", float(keep if keep is not None else 1.0))
            log("%s: %d scanned plants (%s), about %.0f cm across against our %.0f cm, keeping %.0f%%" % (
                s["name"], len(variants), variants[0].get_name(),
                2 * sum(footprint(m_) for m_ in variants) / len(variants) * k,
                2 * footprint(ours) * abs(frame[2]) if ours else 0, 100 * float(keep if keep is not None else 1.0)))
        elif ours is not None:
            foliage_nanite(ours)
            gb = float(s.get("ground_blend", 1.0))
            if gb not in mats:
                mats[gb] = foliage_material(rvt, blend * gb, "M_Foliage_Blend%02d" % round(gb * 100))
            e.set_editor_property("mesh", ours)
            e.set_editor_property("material", mats[gb])
        e.set_editor_property("cull_distance", float(s["cull_m"]) * 100.0)
        e.set_editor_property("cull_spread", float(s.get("cull_spread", 1.0)))
        e.set_editor_property("min_draw_distance", float(s.get("min_draw_m", 0.0)) * 100.0)
        e.set_editor_property("cast_shadow", bool(s["shadow"]))
        species.append(e)
    a = spawn(unreal.TorqueFoliage)
    a.set_actor_label("Foliage")
    a.set_folder_path("Detail")
    a.set_editor_property("species", species)
    a.set_editor_property("frame_yaw", float(frame[0]))
    a.set_editor_property("frame_mirror", float(frame[1]))
    a.set_editor_property("frame_scale", float(frame[2]))
    log("foliage: %d species (%d scanned), %d plants (placed at Play)" % (len(species), len(scans), spec.get("total", 0)))


# --- Far scars (Tools/make_far_rock.py) --------------------------------------------------------------

FAR_DEST = "/Game/Buttertubs/Far"


def place_far_rock(frame, land_mat, stone_mat):
    """The limestone scars on the far fells: the faces (scars_NN) in M_Stone and the turf shelves
    above them (shelf_NN) in M_Land, coloured as the far hills under them so they are the same
    hillside. Plain static meshes at the level's origin, like the land, with no collision."""
    folder = os.path.join(data_dir(), "Far")
    if not os.path.isdir(folder) or not any(f.endswith(".glb") for f in os.listdir(folder)):
        log("no far scars (python Tools/make_far_rock.py)")
        return
    meshes = import_glbs(folder, FAR_DEST)
    yaw, mirror, scale = frame
    rot = unreal.Rotator(0.0, 0.0, float(yaw))
    sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for name, mesh in sorted(meshes.items()):
        nanite_off(mesh)
        try:
            sme.remove_collisions(mesh)
        except Exception:
            pass
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
        a = actors.spawn_actor_from_object(mesh, unreal.Vector(0, 0, 0), rot)
        a.set_actor_scale3d(unreal.Vector(scale, scale * mirror, scale))
        a.set_actor_label(name)
        a.set_folder_path("Far")
        comp = a.get_component_by_class(unreal.StaticMeshComponent)
        comp.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        comp.set_material(0, land_mat if name.startswith("shelf_") else stone_mat)
        comp.set_collision_profile_name("NoCollision")
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_editor_property("can_ever_affect_navigation", False)
    log("far scars: %d meshes" % len(meshes))


# --- Becks (Tools/make_becks.py) ---------------------------------------------------------------------

BECK_DEST = "/Game/Buttertubs/Becks"


def beck_material():
    """Running water: dark peaty brown, glossy, ripples running downstream (UV y), white where it runs
    fast (vertex colour R). Opaque: a beck this shallow shows its colour, not its bed."""
    g = Graph(new_material("M_Beck"))
    vc = g.node(unreal.MaterialExpressionVertexColor)
    nt = noise_texture()
    if nt is not None:
        uv = g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=1.5, v_tiling=0.6)
        p1 = g.node(unreal.MaterialExpressionPanner, speed_x=0.0, speed_y=-0.55); g.link(uv, p1, "Coordinate")
        uv2 = g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=2.7, v_tiling=1.3)
        p2 = g.node(unreal.MaterialExpressionPanner, speed_x=0.05, speed_y=-0.9); g.link(uv2, p2, "Coordinate")
        n1 = g.sample(nt, p1, "linear"); n2 = g.sample(nt, p2, "linear")
        n = g.op(unreal.MaterialExpressionAdd, n1, n2)
        n = g.op(unreal.MaterialExpressionSubtract, n, const_b=1.0)                 # about -0.5..0.5 each way
        n = g.op(unreal.MaterialExpressionMultiply, n, g.const((0.9, 0.9, 0.0)))
        normal = g.op(unreal.MaterialExpressionAdd, n, g.const((0.0, 0.0, 1.0)))
        foam = g.op(unreal.MaterialExpressionMultiply, n1, vc, a_out="R", b_out="R")
    else:
        normal = g.const((0.0, 0.0, 1.0))
        foam = g.op(unreal.MaterialExpressionMultiply, vc, a_out="R", const_b=0.5)
    foam = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply, foam, const_b=1.6))
    base = g.lerp(g.const((0.02, 0.016, 0.01)), g.const((0.5, 0.52, 0.5)), foam)
    rough = g.lerp(g.const(0.04), g.const(0.45), foam)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(g.const(0.5), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
    mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    return g.m


def place_becks(frame, stone_mat, wall_mat=None):
    """The becks: their stony beds (M_Stone) and water (M_Beck) as plain meshes at the origin like
    the land, the culverts' headwalls (M_Masonry, wall_) and coping stones (M_Stone, cope_), and their
    cobbles and boulders planted by a TorqueFoliage actor, "BeckStones"."""
    folder = os.path.join(data_dir(), "Becks")
    spec_path = os.path.join(folder, "becks.json")
    if not os.path.exists(spec_path):
        log("no becks (python Tools/make_becks.py)")
        return
    spec = json.load(open(spec_path))
    meshes = import_glbs(folder, BECK_DEST)
    water = beck_material()
    yaw, mirror, scale = frame
    rot = unreal.Rotator(0.0, 0.0, float(yaw))
    sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for name in spec.get("meshes", []):
        mesh = meshes.get(name)
        if mesh is None:
            unreal.log_warning("[Torque] no beck mesh " + name)
            continue
        nanite_off(mesh)
        try:
            sme.remove_collisions(mesh)
        except Exception:
            pass
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
        a = actors.spawn_actor_from_object(mesh, unreal.Vector(0, 0, 0), rot)
        a.set_actor_scale3d(unreal.Vector(scale, scale * mirror, scale))
        a.set_actor_label(name)
        a.set_folder_path("Becks")
        comp = a.get_component_by_class(unreal.StaticMeshComponent)
        comp.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        comp.set_material(0, water if name.startswith("water_") else
                          (wall_mat or stone_mat) if name.startswith("wall_") else stone_mat)
        comp.set_collision_profile_name("NoCollision")
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_editor_property("can_ever_affect_navigation", False)
    if hasattr(unreal, "TorqueFoliage"):
        species = []
        for s in spec["species"]:
            e = unreal.TorqueFoliageSpecies()
            e.set_editor_property("name", s["name"])
            m = meshes.get(s["mesh"])
            if m is not None:
                foliage_nanite(m)
                e.set_editor_property("mesh", m)
                e.set_editor_property("material", stone_mat)
            e.set_editor_property("cull_distance", float(s.get("cull_m", 600)) * 100.0)
            e.set_editor_property("cull_spread", 1.6)
            e.set_editor_property("cast_shadow", bool(s.get("shadow", True)))
            species.append(e)
        a = spawn(unreal.TorqueFoliage)
        a.set_actor_label("BeckStones")
        a.set_folder_path("Becks")
        a.set_editor_property("instances_file", "Data/Buttertubs/Becks/instances.bin")
        a.set_editor_property("species", species)
        a.set_editor_property("frame_yaw", float(yaw))
        a.set_editor_property("frame_mirror", float(mirror))
        a.set_editor_property("frame_scale", float(scale))
    log("becks: %s km, %d meshes, %d stones" % (spec.get("km"), len(spec.get("meshes", [])),
                                              sum(s.get("count", 0) for s in spec["species"])))


# --- Trees (Tools/make_trees.py) --------------------------------------------------------------------

TREE_DEST = "/Game/Buttertubs/Trees"


def tree_wind(g, w, leaf):
    """World position offset for a tree: `w` (UV y) is how free the spot is to move, 0 at the trunk
    and 1 at the outer twigs; `leaf` (UV x) is 1 on leaves. Slow sway of the limbs downwind (to the
    north east) in gusts, and a quick flutter of the leaves."""
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    along = g.op(unreal.MaterialExpressionDotProduct, wp, g.const((0.0011, 0.0009, 0.0)))   # gusts roll across
    t = g.node(unreal.MaterialExpressionTime)
    ph = g.op(unreal.MaterialExpressionSubtract, g.op(unreal.MaterialExpressionMultiply, t, const_b=0.45), along)
    s1 = g.node(unreal.MaterialExpressionSine, period=1.0); g.link(ph, s1)
    gph = g.op(unreal.MaterialExpressionSubtract, g.op(unreal.MaterialExpressionMultiply, t, const_b=0.09),
               g.op(unreal.MaterialExpressionMultiply, along, const_b=0.3))
    gs = g.node(unreal.MaterialExpressionSine, period=1.0); g.link(gph, gs)
    gust = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, gs, const_b=0.35), const_b=0.65)
    sway = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, s1, const_b=0.5), const_b=0.6)
    amp = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, w, w), const_b=22.0)   # cm at the tips
    mag = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, amp, sway), gust)
    wind = weather_param(g, "Wind")
    mag = g.op(unreal.MaterialExpressionMultiply, mag, wind)                        # the real wind's strength
    limb = g.op(unreal.MaterialExpressionMultiply, wind_dir(g, -0.05), mag)
    # Flutter: fast, small, out of step from leaf to leaf (by world position), leaves only.
    fph = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, t, const_b=3.1),
               g.op(unreal.MaterialExpressionDotProduct, wp, g.const((0.031, 0.027, 0.043))))
    fs = g.node(unreal.MaterialExpressionSine, period=1.0); g.link(fph, fs)
    fl = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, fs, leaf),
              g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, gust, const_b=1.6), wind))
    nrm = g.node(unreal.MaterialExpressionVertexNormalWS)
    flutter = g.op(unreal.MaterialExpressionMultiply, nrm, fl)
    return g.op(unreal.MaterialExpressionAdd, limb, flutter)


def tree_material(tex=None, roles=None):
    """The trees: vertex colour (each tree's leaves a little different, whole stands drifting in tone),
    two-sided foliage shading so the sun glows through the leaves and not the bark, ambient occlusion
    from the vertex alpha (dark inside the crown), and wind (tree_wind). Opaque: no alpha cut-outs.
    The bark takes a scanned bark (the "bark" texture role) on UV1, which runs around and along each
    branch in metres: its relief as the normal, its detail on the vertex colour, its occlusion."""
    g = Graph(new_material("M_Tree"))
    m = g.m
    foliage_flags(m)
    try_set(m, "max_world_position_offset_displacement", 40.0)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    leaf = g.node(unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False)
    g.link(uv, leaf)
    w = g.node(unreal.MaterialExpressionComponentMask, r=False, g=True, b=False, a=False)
    g.link(uv, w)
    pir = g.node(unreal.MaterialExpressionPerInstanceRandom)
    # Per tree: a little lighter or darker, yellower or bluer (leaves only; bark a touch).
    tint = g.lerp(g.const((0.84, 0.9, 0.86)), g.const((1.14, 1.08, 0.86)), pir)
    tint = g.lerp(g.const((0.95, 0.95, 0.95)), tint, g.op(unreal.MaterialExpressionAdd,
                  g.op(unreal.MaterialExpressionMultiply, leaf, const_b=0.8), const_b=0.2))
    base = g.op(unreal.MaterialExpressionMultiply, vc, tint)
    bark_w = g.op(unreal.MaterialExpressionOneMinus, leaf)
    normal = g.const((0.0, 0.0, 1.0))
    ao = g.op(unreal.MaterialExpressionMultiply, vc, a_out="A", const_b=1.0)
    if tex and "bark" in tex:
        # The bark: the scan's detail (mostly its light and dark, a little of its colour) on the tree's
        # own bark colour, so ash stays grey and birch white; its relief; its crevices' occlusion.
        bm = tex["bark"]; tile = float(roles["bark"].get("tile_m", 0.7))
        buv = g.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=1, u_tiling=1.0 / tile, v_tiling=1.0 / tile)
        bc = g.sample(bm["color"], buv, "color")
        bd = g.node(unreal.MaterialExpressionDesaturation, -700)
        g.link(bc, bd)
        g.link(g.const(0.6), bd, "Fraction")
        detail = g.op(unreal.MaterialExpressionMultiply, bd, const_b=1.0 / float(roles["bark"].get("mean", 0.3)))
        base = g.lerp(base, g.op(unreal.MaterialExpressionMultiply, base, detail), bark_w)
        if "normal" in bm:
            bn = g.sample(bm["normal"], buv, "normal")
            bn = g.op(unreal.MaterialExpressionMultiply, bn, g.const((1.0, -1.0, 1.0)))   # ambientCG's are OpenGL's
            normal = g.lerp(normal, bn, bark_w)
        if "ao" in bm:
            bao = g.lerp(g.const(1.0), g.sample(bm["ao"], buv, "ao"), bark_w, b_out="R")
            ao = g.op(unreal.MaterialExpressionMultiply, ao, bao)
    nt = noise_texture()
    if nt is not None:
        # Stands a few hundred metres across drift a little in tone, as real woods do.
        n = g.sample(nt, g.world_uv(700.0), "linear")
        k = g.lerp(g.const(0.86), g.const(1.12), n, alpha_out="G")
        base = g.op(unreal.MaterialExpressionMultiply, base, k)
    # With distance a wood goes darker and greyer (its shade, and the air, outweigh its sunlit leaves),
    # from about 250 m to 2 km, so far woods sit in the fells' haze instead of glowing green.
    depth = g.node(unreal.MaterialExpressionPixelDepth)
    far = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionDivide,
               g.op(unreal.MaterialExpressionSubtract, depth, const_b=20000.0), const_b=150000.0))
    grey = g.node(unreal.MaterialExpressionDesaturation, -600)
    g.link(base, grey)
    g.link(g.const(0.65), grey, "Fraction")
    dull = g.op(unreal.MaterialExpressionMultiply, grey, const_b=0.52)   # beenster: far trees "a tiny bit more subtle"
    base = g.lerp(base, dull, far)
    # Far off, no glow: the light through the leaves and their sheen are a near tree's, and on the far
    # models' smooth lumps they made bright blobs on the fellsides. Both fade out with the same distance.
    near = g.op(unreal.MaterialExpressionOneMinus, far)
    sss = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, base, g.const((0.7, 0.85, 0.3))), leaf)
    sss = g.op(unreal.MaterialExpressionMultiply, sss, near)
    rough = g.lerp(g.lerp(g.const(0.88), g.const(0.58), leaf), g.const(0.95), far)
    spec = g.lerp(g.const(0.32), g.const(0.12), far)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    mel.connect_material_property(ao, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(tree_wind(g, w, leaf), "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def cloud_shadow_material():
    """Cloud shadows sweeping over the fells, a Dales day's signature: a light function on the sun,
    the broad noise in patches a kilometre or two across, drifting north east at about 9 m/s (the
    prevailing south westerly), the shade let through at 45% (thin summer cumulus, not storm cloud).
    How much of the ground is in shade follows the weather (MPC_Weather's CloudShadow)."""
    nt = noise_texture()
    if nt is None:
        return None
    g = Graph(new_material("M_CloudShadow"))
    try_set(g.m, "material_domain", unreal.MaterialDomain.MD_LIGHT_FUNCTION)
    if sky_mpc() is not None:
        # Carried by the real wind at cloud height (MPC_Sky's drift, metres, added up by the game) where
        # the clouds aren't drawn (the Low setting); elsewhere ATorqueSky turns these shadows off.
        drift = g.node(unreal.MaterialExpressionAppendVector)
        g.link(sky_param(g, "DriftX"), drift, "A")
        g.link(sky_param(g, "DriftY"), drift, "B")
        drift = g.op(unreal.MaterialExpressionMultiply, drift, const_b=-1.0 / 1800.0)
    else:
        tm = g.node(unreal.MaterialExpressionTime)
        drift = g.op(unreal.MaterialExpressionMultiply, tm,
                     g.node(unreal.MaterialExpressionConstant2Vector, r=-6.5 / 1800.0, g=-6.5 / 1800.0))   # +x east, +y south
    uv = g.op(unreal.MaterialExpressionAdd, g.world_uv(1800.0), drift)
    a = g.sample(nt, uv, "linear")
    uv2 = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, uv, const_b=2.3), g.node(unreal.MaterialExpressionConstant2Vector, r=0.31, g=0.67))
    b = g.sample(nt, uv2, "linear")
    n = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, a, const_b=0.7, a_out="G"),
             g.op(unreal.MaterialExpressionMultiply, b, const_b=0.3, a_out="G"))
    # Where the noise turns to shade follows the real cloud (CloudShadow: 0.6 as made, 1 a clear sky).
    cloud = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                 g.op(unreal.MaterialExpressionSubtract, n, weather_param(g, "CloudShadow")), const_b=7.0))
    light = g.lerp(g.const(1.0), g.const(0.3), cloud)
    mel.connect_material_property(light, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    return g.m


def tree_shadow_nanite(mesh):
    """The near trees' leaves, as Nanite simplifies them (for the shadow map more than for the eye):
    with preserve-area, a crown's tiny leaves merge into a few big triangles grown to keep their area,
    and their shadows on the road read as big angular shards. A cap on how long a simplified edge can
    grow (relative to the source's edges) keeps them leaf-sized. TORQUE_TREE_EDGE sets the cap
    (default 2; 0 turns it off). TORQUE_TREE_VOXEL=on tries Nanite's voxel foliage instead (5.7+,
    experimental), which simplifies a crown to fine voxels rather than to triangles."""
    try:
        ns = mesh.get_editor_property("nanite_settings")
        did = []
        if os.environ.get("TORQUE_TREE_VOXEL", "off").lower() == "on" and hasattr(unreal, "NaniteShapePreservation") \
                and hasattr(unreal.NaniteShapePreservation, "VOXELIZE"):
            if try_set(ns, "shape_preservation", unreal.NaniteShapePreservation.VOXELIZE):
                did.append("voxelize")
        cap = float(os.environ.get("TORQUE_TREE_EDGE", "2.0"))
        if cap > 0 and try_set(ns, "max_edge_length_factor", cap):
            did.append("edge cap %.1f" % cap)
        if did:
            try:
                unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).set_nanite_settings(mesh, ns, True)
            except Exception:
                mesh.set_editor_property("nanite_settings", ns)
            unreal.EditorAssetLibrary.save_loaded_asset(mesh)
        else:
            unreal.log_warning("[Torque] %s: no Nanite edge cap or voxel setting in this engine" % mesh.get_name())
        return did
    except Exception as e:
        unreal.log_warning("[Torque] tree shadow Nanite settings on %s: %s" % (mesh.get_name(), e))
        return []


def place_trees(frame, tex=None, roles=None):
    """The trees (Tools/make_trees.py): import the models as Nanite meshes and add a second
    TorqueFoliage actor, "Trees", that plants them from Data/Buttertubs/Trees/instances.bin at
    BeginPlay. No cull distance: the far ones are their own cheap models, so nothing pops in.
    TORQUE_TREES=off leaves them out; TORQUE_TREE_AREA=off builds the near models without Nanite's
    preserve-area (an A/B for the shape of their shadows). Data/Buttertubs/Trees/overrides.json can swap a species' near
    models for scanned ones (Megascans), which keep their own materials:

        {"ash": {"meshes": ["/Game/Fab/Megascans/3D/.../SM_European_Ash_01", ...]}}
    """
    folder = os.path.join(data_dir(), "Trees")
    spec_path = os.path.join(folder, "trees.json")
    if os.environ.get("TORQUE_TREES", "on").lower() == "off":
        log("trees left out (TORQUE_TREES=off)")
        return
    if not os.path.exists(spec_path):
        unreal.log_warning("[Torque] no trees (python Tools/make_trees.py)")
        return
    if not hasattr(unreal, "TorqueFoliage"):
        unreal.log_warning("[Torque] the TorqueFoliage class isn't built yet: rebuild the editor, then run this again")
        return
    spec = json.load(open(spec_path))
    over_path = os.path.join(folder, "overrides.json")
    overrides = json.load(open(over_path)) if os.path.exists(over_path) else {}
    meshes = import_glbs(folder, TREE_DEST)
    mat = tree_material(tex, roles)
    species = []
    total = 0
    for s in spec["species"]:
        e = unreal.TorqueFoliageSpecies()
        e.set_editor_property("name", s["name"])
        ours = meshes.get(s["mesh"])
        if ours is None:
            unreal.log_warning("[Torque] no tree mesh " + s["mesh"])
        elif os.environ.get("TORQUE_TREE_AREA", "on").lower() == "off" and not s.get("far"):
            # A/B for the shadows: plain Nanite simplification (leaves drop out with distance rather
            # than growing to keep the crown's area, which can show as blocky shadow shapes).
            nanite_on(ours)
            try:
                unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).remove_collisions(ours)
            except Exception:
                pass
        else:
            foliage_nanite(ours)
            if not s.get("far"):
                tree_shadow_nanite(ours)
        kind = s["name"].split("_")[2] if s["name"].count("_") >= 3 else ""
        o = overrides.get(kind) if not s.get("far") else None
        scans = []
        for p in (o or {}).get("meshes", []):
            a = unreal.load_asset(p)
            if isinstance(a, unreal.StaticMesh):
                scans.append(a)
            else:
                unreal.log_warning("[Torque] trees: no static mesh at %s" % p)
        if scans and ours is not None:
            for a in scans:
                foliage_nanite(a)
            # Scale theirs to our model's height, so the map's heights still hold.
            ob = ours.get_bounding_box(); oh = (ob.max.z - ob.min.z) * abs(frame[2])
            th = sum(a.get_bounding_box().max.z - a.get_bounding_box().min.z for a in scans) / len(scans)
            e.set_editor_property("variants", scans)
            e.set_editor_property("native_mesh", True)
            e.set_editor_property("scale_multiplier", float(o.get("scale", oh / max(th, 1.0))))
            log("%s: %d scanned trees (%s)" % (s["name"], len(scans), scans[0].get_name()))
        elif ours is not None:
            e.set_editor_property("mesh", ours)
            e.set_editor_property("material", mat)
        e.set_editor_property("cull_distance", float(s.get("cull_m", 30000)) * 100.0)
        e.set_editor_property("cast_shadow", bool(s.get("shadow", True)))
        species.append(e)
        total += int(s.get("count", 0))
    a = spawn(unreal.TorqueFoliage)
    a.set_actor_label("Trees")
    a.set_folder_path("Detail")
    a.set_editor_property("instances_file", "Data/Buttertubs/Trees/instances.bin")
    a.set_editor_property("species", species)
    a.set_editor_property("frame_yaw", float(frame[0]))
    a.set_editor_property("frame_mirror", float(frame[1]))
    a.set_editor_property("frame_scale", float(frame[2]))
    try_set(a, "wpo_disable_distance", 8000)
    log("trees: %d models, %d trees (planted at Play): %s" % (len(species), total, spec.get("counts", {})))


# --- Stone props: walls, scars, boulders, barns, gates, sheep (Tools/buttertubs_detail.py) -----------

PROP_DEST = "/Game/Buttertubs/Props"


def nanite_on(mesh):
    try:
        ns = mesh.get_editor_property("nanite_settings")
        ns.set_editor_property("enabled", True)
        try:
            unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).set_nanite_settings(mesh, ns, True)
        except Exception:
            mesh.set_editor_property("nanite_settings", ns)
    except Exception as e:
        unreal.log_warning("[Torque] couldn't turn Nanite on for %s: %s" % (mesh.get_name(), e))


def import_props():
    """The prop meshes, Nanite and without collision, and props.json's instances."""
    folder = os.path.join(data_dir(), "Props")
    spec_path = os.path.join(data_dir(), "props.json")
    if not (os.path.isdir(folder) and os.path.exists(spec_path)):
        log("no props (run Tools/export_buttertubs.py)")
        return {}, {}
    spec = json.load(open(spec_path))
    if os.environ.get("TORQUE_PROPS", "on").lower() == "off":
        log("props left out (TORQUE_PROPS=off)")
        return {}, {}
    meshes = import_glbs(folder, PROP_DEST)
    sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    # TORQUE_WALLS=lod: the wall lengths drop Nanite and switch to a light shell (SM_WallFar) in the
    # distance, since Nanite can't simplify thousands of separate stones far off. Default: Nanite.
    lod_walls = os.environ.get("TORQUE_WALLS", "nanite").lower() == "lod"
    for name, mesh in meshes.items():
        try:
            sme.remove_collisions(mesh)
        except Exception:
            pass
        far = spec["meshes"].get(name, {}).get("lod1")
        if lod_walls and far and far in meshes:
            nanite_off(mesh)
            try:
                sme.set_lod_from_static_mesh(mesh, 1, meshes[far], 0, True)
                try:
                    mesh.set_editor_property("auto_compute_lod_screen_size", False)
                    sme.set_lod_screen_sizes(mesh, [1.0, 0.012])   # the shell from about 90 m (90 degree view)
                except Exception as e:
                    unreal.log_warning("[Torque] %s: LOD screen sizes left automatic (%s)" % (name, e))
            except Exception as e:
                unreal.log_warning("[Torque] %s: couldn't add the far LOD (%s); back to Nanite" % (name, e))
                nanite_on(mesh)
        else:
            nanite_on(mesh)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    log("walls: %s" % ("LODs (stones near, a shell far)" if lod_walls else "Nanite"))
    log("props: %d meshes, %d instances" % (len(meshes), sum(len(v) for v in spec["instances"].values())))
    return meshes, spec


def noise_texture():
    """Data/Textures/Noise/color.png: three tiling noises (R fine, G broad, B finer), made once in
    numpy. A texture sample is far cheaper per pixel than the material Noise node."""
    path = TEX_DEST + "/Noise/T_Noise"
    f = os.path.join(os.path.dirname(data_dir()), "Textures", "Noise", "color.png")
    if not os.path.exists(f):
        return None
    t = unreal.AssetImportTask()
    for k, v in (("filename", f), ("destination_path", TEX_DEST + "/Noise"), ("destination_name", "T_Noise"),
                 ("automated", True), ("replace_existing", True), ("save", False)):
        t.set_editor_property(k, v)
    asset_tools.import_asset_tasks([t])
    tex = unreal.load_asset(path)
    if tex:
        tex.set_editor_property("srgb", False)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


def instanced_usage(m):
    """Instanced Nanite meshes need these usage flags set on the material for a packaged build."""
    try_set(m, "used_with_instanced_static_meshes", True)
    try_set(m, "used_with_nanite", True)


def far_dull(g, col, start_cm=30000.0, span_cm=150000.0, most=0.4):
    """Buildings and walls far off sit back into the dale (beenster: "a tiny bit more subtle"): from
    start_cm out, lean towards a darker, greyer version of themselves, as the trees do."""
    depth = g.node(unreal.MaterialExpressionPixelDepth)
    far = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionDivide,
               g.op(unreal.MaterialExpressionSubtract, depth, const_b=start_cm), const_b=span_cm))
    far = g.op(unreal.MaterialExpressionMultiply, far, const_b=most)
    grey = g.node(unreal.MaterialExpressionDesaturation, -600)
    g.link(col, grey)
    g.link(g.const(0.5), grey, "Fraction")
    return g.lerp(col, g.op(unreal.MaterialExpressionMultiply, grey, const_b=0.75), far)


def stone_material(tex, roles, name="M_Stone", a_role="stone", b_role="stone_bare", broad_moss=True, tint_param=False):
    """Stone from two scans (lichened limestone and bare, bedded limestone), chosen per stone by the
    vertex colour's green; red sets its brightness; blue whitens it with lichen in patches; alpha is
    moss, thickest where the surface faces up. UV0 is metres of surface. With tint_param, a "Tint"
    vector parameter (1 as scanned) colours the stone: Ride UK's local stone (honey, slate, granite)."""
    g = Graph(new_material(name))
    if a_role not in tex:   # not fetched: fall back to another scan rather than flat grey
        a_role = b_role if b_role in tex else ("stone_bare" if "stone_bare" in tex else a_role)
    if b_role not in tex:
        b_role = a_role
    instanced_usage(g.m)
    vc = g.node(unreal.MaterialExpressionVertexColor)

    def uv(metres):
        return g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=1.0 / metres, v_tiling=1.0 / metres)

    def layer(role, fallback):
        if role in tex:
            m = tex[role]; u = uv(roles[role]["tile_m"])
            col = g.sample(m["color"], u, "color")
            if "ao" in m:
                col = g.op(unreal.MaterialExpressionMultiply, col, g.sample(m["ao"], u, "ao"), b_out="R")
            nrm = g.sample(m["normal"], u, "normal") if "normal" in m else g.const((0.0, 0.0, 1.0))
            rough = g.sample(m["rough"], u, "rough") if "rough" in m else g.const(0.85)
            return [col, nrm, rough]
        return [g.op(unreal.MaterialExpressionMultiply, g.const(lin(fallback)), noise(g.m, 0.5, 0.7, 1.2, -1200, g.y)),
                g.const((0.0, 0.0, 1.0)), g.const(0.88)]

    stone = layer(a_role, (150, 148, 140))
    if b_role != a_role:
        bare = layer(b_role, (160, 158, 152))
        st = g.mix(stone, bare, vc, alpha_out="G")
    else:
        st = stone
    bright = g.op(unreal.MaterialExpressionMultiply, vc, a_out="R", const_b=2.0)
    st[0] = g.op(unreal.MaterialExpressionMultiply, st[0], bright)
    if tint_param:
        tp = g.node(unreal.MaterialExpressionVectorParameter, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
        st[0] = g.op(unreal.MaterialExpressionMultiply, st[0], tp, b_out="")
    # Lichen: pale crusts in patches, on the stones the blue channel picks. The patches come from a
    # noise texture on each stone's own UVs (the Noise node costs too much on 10 km of wall).
    nt = noise_texture()
    if nt:
        ns = g.sample(nt, uv(1.3), "rough")
        patch = g.op(unreal.MaterialExpressionSubtract, ns, a_out="R", const_b=0.45)
        patch = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply, patch, const_b=4.0))
        broad = g.op(unreal.MaterialExpressionSubtract, ns, a_out="G", const_b=0.75)   # -0.75..0.25, as the Noise node's range
    else:
        patch = g.mask_noise(0.02, -1.2, 1.6)
        broad = noise(g.m, 0.03, -0.8, 0.0, -1200, g.y)
    lichen = g.op(unreal.MaterialExpressionMultiply, patch, vc, b_out="B")
    lichen = g.op(unreal.MaterialExpressionMultiply, lichen, const_b=0.55)
    st[0] = g.lerp(st[0], g.const(lin((178, 180, 170))), lichen)
    # Moss: vertex alpha, broken up by noise, more on upward faces.
    nz = g.node(unreal.MaterialExpressionVertexNormalWS)
    up = g.node(unreal.MaterialExpressionComponentMask, r=False, g=False, b=True, a=False)
    g.link(nz, up)
    up = g.op(unreal.MaterialExpressionMultiply, up, const_b=0.6)
    up = g.op(unreal.MaterialExpressionAdd, up, const_b=0.7)
    up = g.op(unreal.MaterialExpressionSaturate, up)
    m = g.op(unreal.MaterialExpressionMultiply, vc, a_out="A", const_b=1.6)
    if broad_moss:   # walls in the open: moss in broad patches anywhere; house walls only where the vertex alpha says
        m = g.op(unreal.MaterialExpressionAdd, m, broad)
    else:
        m = g.op(unreal.MaterialExpressionAdd, m, broad)
        m = g.op(unreal.MaterialExpressionSubtract, m, const_b=0.35)
    m = g.op(unreal.MaterialExpressionSaturate, m)
    m = g.op(unreal.MaterialExpressionMultiply, m, up)
    moss = layer("moss", (70, 84, 38))
    st = g.mix(st, moss, m)
    st[0] = far_dull(g, st[0])
    snow_cover(g, st, keep=0.15)
    return g.finish(st)


def render_material(tex, roles, name="M_LimeRender"):
    """Lime render and limewash on the village houses: the vertex colour (a warm off-white), given
    the grain of a stone scan seen through the wash, a fine speckle, rain streaks running down from
    the eaves (noise stretched along the wall's height, the UVs being metres of wall), and dirt and
    green-brown splash at the foot from the vertex alpha."""
    g = Graph(new_material(name))
    instanced_usage(g.m)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    col = g.op(unreal.MaterialExpressionMultiply, vc, g.const(1.0))
    role = "stone_bare" if "stone_bare" in tex else ("masonry" if "masonry" in tex else None)
    nrm = g.const((0.0, 0.0, 1.0))
    if role:
        uv = g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=0.5, v_tiling=0.5)
        grain = g.sample(tex[role]["color"], uv, "color")
        gr = g.op(unreal.MaterialExpressionMultiply, grain, a_out="R", const_b=0.5)
        gr = g.op(unreal.MaterialExpressionAdd, gr, const_b=0.82)          # about 0.82..1.1
        col = g.op(unreal.MaterialExpressionMultiply, col, gr)
        if "normal" in tex[role]:
            n = g.sample(tex[role]["normal"], uv, "normal")
            nrm = g.lerp(n, g.const((0.0, 0.0, 1.0)), g.const(0.65))       # the wash softens the stone under it
    nt = noise_texture()
    if nt:
        fine = g.sample(nt, g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=1.7, v_tiling=1.7), "rough")
        sp = g.op(unreal.MaterialExpressionMultiply, fine, a_out="B", const_b=0.18)
        sp = g.op(unreal.MaterialExpressionAdd, sp, const_b=0.9)
        col = g.op(unreal.MaterialExpressionMultiply, col, sp)
        streak = g.sample(nt, g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=1.6, v_tiling=0.09), "rough")
        st = g.op(unreal.MaterialExpressionSubtract, streak, a_out="G", const_b=0.45)
        st = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply, st, const_b=2.2))
        st = g.op(unreal.MaterialExpressionMultiply, st, const_b=0.22)
        col = g.lerp(col, g.op(unreal.MaterialExpressionMultiply, col, g.const((0.62, 0.6, 0.55))), st)
    dirt = g.op(unreal.MaterialExpressionMultiply, col, g.const((0.42, 0.42, 0.33)))
    col = g.lerp(col, dirt, vc, alpha_out="A")
    rough = g.lerp(g.const(0.88), g.const(0.95), vc, alpha_out="A")
    col = far_dull(g, col)
    return g.finish([col, nrm, rough], specular=0.35)


def graze_wpo(g):
    """Sheep come alive: the head and neck (the front of the ewe, local x past about 45 cm; she faces
    +x) dip and nibble in bouts and turn now and then, each ewe on her own clock (per-instance random)."""
    lp = g.node(unreal.MaterialExpressionLocalPosition)
    x = g.node(unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False)
    g.link(lp, x)
    w = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionDivide,
             g.op(unreal.MaterialExpressionSubtract, x, const_b=45.0), const_b=30.0))
    tm = g.node(unreal.MaterialExpressionTime)
    pir = g.node(unreal.MaterialExpressionPerInstanceRandom)
    def wave(rate, phase_mul):
        a = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, tm, const_b=rate),
                 g.op(unreal.MaterialExpressionMultiply, pir, const_b=phase_mul))
        s = g.node(unreal.MaterialExpressionSine, -600, period=1.0)   # period 1: the rates are cycles per second
        g.link(a, s)
        return s
    nib = wave(0.9, 3.7)                                       # a nibble a second or so
    bout = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionAdd, wave(0.05, 9.1), const_b=0.2))
    dz = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, nib, bout), const_b=4.0)
    dy = g.op(unreal.MaterialExpressionMultiply, wave(0.07, 5.3), const_b=6.0)   # a slow look round
    off = g.op(unreal.MaterialExpressionAppendVector, g.op(unreal.MaterialExpressionAppendVector, g.const(0.0), dy), dz)
    off = g.op(unreal.MaterialExpressionMultiply, off, w)
    tl = g.node(unreal.MaterialExpressionTransform)   # local to world (the ewe's yaw)
    try_set(tl, "transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    try_set(tl, "transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    g.link(off, tl)
    try_set(g.m, "max_world_position_offset_displacement", 12.0)
    return tl


def flat_material(name, rough, metallic=0.0, fine=0.0, wpo=None):
    """Vertex colour as the colour, darkened and roughened by its alpha (dirt), with a little noise."""
    g = Graph(new_material(name))
    instanced_usage(g.m)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    dirt = g.op(unreal.MaterialExpressionOneMinus, vc, a_out="A")
    dirt = g.lerp(g.const(0.55), g.const(1.0), dirt)
    col = g.op(unreal.MaterialExpressionMultiply, vc, dirt)
    if fine:
        col = g.op(unreal.MaterialExpressionMultiply, col, noise(g.m, fine, 0.75, 1.15, -1200, g.y))
    r = g.lerp(g.const(rough), g.const(min(1.0, rough + 0.35)), vc, alpha_out="A")
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    if metallic:
        mel.connect_material_property(g.const(metallic), "", unreal.MaterialProperty.MP_METALLIC)
    if wpo:
        mel.connect_material_property(wpo(g), "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    return g.m


def instanced(label, mesh, rows, material, frame, shadow=True):
    """One actor holding an instanced static mesh component with all of a mesh's instances (Nanite
    draws them cheaply). Falls back to one actor per instance if components can't be added here."""
    yaw, mirror, scale = frame
    fix = None
    if abs(yaw) > 0.5 or mirror < 0 or abs(scale - 1.0) > 1e-3:
        fix = unreal.Transform(unreal.Vector(0, 0, 0), unreal.Rotator(0.0, 0.0, float(yaw)), unreal.Vector(scale, scale * mirror, scale))
    xf = []
    for r in rows:
        t = unreal.Transform(unreal.Vector(r[0], r[1], r[2]), unreal.Rotator(roll=r[5], pitch=r[3], yaw=r[4]),
                             unreal.Vector(r[6], r[7], r[8]))
        if fix is not None:
            t = fix.multiply(t)   # the importer's frame first, then the instance's
        xf.append(t)
    holder = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
    holder.set_actor_label(label)
    holder.set_folder_path("Props")
    root = holder.get_component_by_class(unreal.StaticMeshComponent)
    root.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    try:
        sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        handles = sds.k2_gather_subobject_data_for_instance(holder)
        parent = handles[1] if len(handles) > 1 else handles[0]
        params = unreal.AddNewSubobjectParams(parent_handle=parent, new_class=unreal.InstancedStaticMeshComponent,
                                              blueprint_context=None)
        handle, fail = sds.add_new_subobject(params)
        if not fail.is_empty():
            raise RuntimeError(str(fail))
        comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle))
        comp.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        comp.set_static_mesh(mesh)
        if material:
            comp.set_material(0, material)
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_editor_property("can_ever_affect_navigation", False)
        if not shadow:
            comp.set_editor_property("cast_shadow", False)
        comp.add_instances(xf, False, True)
        return len(xf)
    except Exception as e:
        unreal.log_warning("[Torque] instancing %s failed (%s); placing %d separate actors" % (label, e, len(xf)))
    actors.destroy_actor(holder)
    for i, t in enumerate(xf):
        a = actors.spawn_actor_from_object(mesh, t.translation, t.rotation.rotator())
        a.set_actor_scale3d(t.scale3d)
        a.set_actor_label("%s_%d" % (label, i))
        a.set_folder_path("Props/" + label)
        c = a.get_component_by_class(unreal.StaticMeshComponent)
        if material:
            c.set_material(0, material)
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    return len(xf)


def find_static_mesh(path):
    """A static mesh by its path, or else by its name anywhere under /Game (a pack moved or renamed)."""
    m = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if isinstance(m, unreal.StaticMesh):
        return m
    name = path.rsplit("/", 1)[-1].split(".")[0]
    try:
        reg = unreal.AssetRegistryHelpers.get_asset_registry()
        flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "StaticMesh")],
                              package_paths=["/Game"], recursive_paths=True)
        for d in reg.get_assets(flt):
            if str(d.asset_name) == name:
                m = d.get_asset()
                if isinstance(m, unreal.StaticMesh):
                    log("car swaps: %s found at %s" % (name, d.package_name))
                    return m
    except Exception as e:
        unreal.log_warning("[Torque] car swaps: searching for %s failed (%s)" % (name, e))
    return None


def fab_car_paint(i, c, metallic, roughness):
    """MI_FabCarPaint_<i>: a car paint colour on M_FabCarPaint (made once per build)."""
    master = MAT_DIR + "/M_FabCarPaint"
    m = unreal.load_asset(master) if unreal.EditorAssetLibrary.does_asset_exist(master) else None
    if m is None or not getattr(fab_car_paint, "made", False):
        g = Graph(new_material("M_FabCarPaint"))
        col = g.node(unreal.MaterialExpressionVectorParameter, -900, parameter_name="Color",
                     default_value=unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
        met = g.node(unreal.MaterialExpressionScalarParameter, -900, parameter_name="Metallic", default_value=0.6)
        rough = g.node(unreal.MaterialExpressionScalarParameter, -900, parameter_name="Roughness", default_value=0.32)
        mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(met, "", unreal.MaterialProperty.MP_METALLIC)
        mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
        mel.recompile_material(g.m)
        unreal.EditorAssetLibrary.save_loaded_asset(g.m)
        m = g.m
        fab_car_paint.made = True
    name = "MI_FabCarPaint_%s" % i
    path = MAT_DIR + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    mi = asset_tools.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, m)
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(c[0], c[1], c[2], 1.0))
    mel.set_material_instance_scalar_parameter_value(mi, "Metallic", float(metallic))
    mel.set_material_instance_scalar_parameter_value(mi, "Roughness", float(roughness))
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    return mi


def place_fab_cars(spec, frame, kind="Car"):
    """The dale's parked cars (kind Car) and the farms' 4x4s (kind Farm4x4, car_swaps.json "farm4x4") as
    Fab models (Data/Buttertubs/car_swaps.json). Returns True when they were placed, so place_props leaves
    out the generated SM_<kind>_* meshes; False (nothing found) keeps them."""
    cp = os.path.join(data_dir(), "car_swaps.json")
    if not os.path.exists(cp):
        return False
    cfg = json.load(open(cp))
    if kind != "Car":
        cfg = cfg.get(kind.lower())
        if not cfg:
            return False
    models = []
    for md in cfg.get("models", []):
        m = find_static_mesh(md["mesh"])
        if m is None:
            unreal.log_warning("[Torque] car swaps: no static mesh at %s (left out)" % md["mesh"])
            continue
        models.append((m, md))
    if not models:
        unreal.log_warning("[Torque] car swaps: none of the Fab cars are in the project; the generated cars stay")
        return False
    rows = []
    for name in sorted(spec["instances"]):
        if name.startswith("SM_%s_" % kind) and name.endswith("_Paint"):
            rows += [(name, r) for r in spec["instances"][name]]
    colours = cfg.get("colours") or [[0.5, 0.5, 0.5]]
    paints = {}
    yaw0 = frame[0]
    q_fix = unreal.Rotator(0.0, 0.0, float(yaw0)).quaternion()
    for i, (name, r) in enumerate(rows):
        mesh, md = models[i % len(models)]
        q_inst = unreal.Rotator(roll=r[5], pitch=r[3], yaw=r[4]).quaternion()
        fwd = q_inst.rotate_vector(q_fix.rotate_vector(unreal.Vector(1, 0, 0)))
        up = q_inst.rotate_vector(q_fix.rotate_vector(unreal.Vector(0, 0, 1)))
        world = unreal.MathLibrary.make_rot_from_xz(fwd, up)
        rot = unreal.Transform(unreal.Vector(0, 0, 0), unreal.Rotator(0.0, 0.0, float(md.get("yaw", 0.0)))).multiply(
            unreal.Transform(unreal.Vector(0, 0, 0), world)).rotation
        # Its wheels on the ground and its middle on the old car's: the mesh's box bottom-centre to the row.
        b = mesh.get_bounds()
        o, e = b.origin, b.box_extent
        lift = rot.rotate_vector(unreal.Vector(-o.x, -o.y, -(o.z - e.z)))
        loc = unreal.Vector(r[0], r[1], r[2]) + lift
        a = actors.spawn_actor_from_object(mesh, loc, rot.rotator())
        a.set_actor_label("Fab%s_%d" % (kind, i))
        a.set_folder_path("Props/Cars")
        c = a.get_component_by_class(unreal.StaticMeshComponent)
        c.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        c.set_editor_property("can_ever_affect_navigation", False)
        ci = i % len(colours)
        if ci not in paints:
            paints[ci] = fab_car_paint(ci if kind == "Car" else "%s_%d" % (kind, ci), colours[ci],
                                       cfg.get("metallic", 0.6), cfg.get("roughness", 0.32))
        listed = set(md.get("paint_slots", []))
        painted = []
        slots = []
        for si, sm in enumerate(mesh.get_editor_property("static_materials")):
            slot = str(sm.get_editor_property("material_slot_name"))
            mat = sm.get_editor_property("material_interface")
            base = mat.get_base_material() if mat else None
            names = " ".join([slot, mat.get_name() if mat else "", base.get_name() if base else ""])
            slots.append("%d %s: %s (base %s)" % (si, slot, mat.get_name() if mat else "none", base.get_name() if base else "none"))
            if slot in listed or str(si) in listed or "paint" in names.lower():
                c.set_material(si, paints[ci])
                painted.append(si)
        log(("car swaps: %s #%d -> Fab" + kind + "_%d %s, colour %d, at (%.0f, %.0f, %.0f) yaw %.1f, painted slots %s"
            ) % (name, i, i, mesh.get_name(), ci, loc.x, loc.y, loc.z, rot.rotator().yaw, painted))
        if not painted:
            unreal.log_warning("[Torque] car swaps: %s has no paint slot; its slots: %s" % (mesh.get_name(), "; ".join(slots)))
        elif i < len(models):
            log("car swaps: %s slots: %s" % (mesh.get_name(), "; ".join(slots)))
    log("car swaps: %d %s from %d Fab models" % (len(rows), "parked cars" if kind == "Car" else kind, len(models)))
    return True


def place_props(meshes, spec, materials, frame):
    """Instances from props.json. Data/Buttertubs/prop_swaps.json, if it's there, swaps a generated mesh
    for another asset already in the project, keeping its own materials: {"SM_Rock_03":
    "/Game/Megascans/...", "SM_Rock_04": {"mesh": "/Game/...", "scale": 0.8}}."""
    swaps = {}
    sp = os.path.join(data_dir(), "prop_swaps.json")
    if os.path.exists(sp):
        swaps = json.load(open(sp))
    # Kinds left out of the ride for now (Data/Buttertubs/props_off.json, e.g. the parked cars): not placed.
    off = set()
    op = os.path.join(data_dir(), "props_off.json")
    if os.path.exists(op):
        off = set(json.load(open(op)).get("kinds", []))
    fab_kinds = [k for k in ("Car", "Farm4x4") if k not in off and place_fab_cars(spec, frame, k)]
    n = 0
    for name, rows in sorted(spec["instances"].items()):
        if any(name.startswith("SM_%s_" % k) for k in fab_kinds):
            continue
        if name.split("_")[1:2] and name.split("_")[1] in off:
            log("%s left out (props_off.json)" % name)
            continue
        mesh = meshes.get(name)
        mat = materials.get(spec["meshes"].get(name, {}).get("material", "stone"))
        sw = swaps.get(name)
        if sw:
            path = sw if isinstance(sw, str) else sw["mesh"]
            k = 1.0 if isinstance(sw, str) else float(sw.get("scale", 1.0))
            other = unreal.load_asset(path)
            if isinstance(other, unreal.StaticMesh):
                mesh, mat = other, None
                rows = [r[:6] + [r[6] * k, r[7] * k, r[8] * k] for r in rows]
                log("%s swapped for %s" % (name, path))
            else:
                unreal.log_warning("[Torque] swap for %s: no static mesh at %s" % (name, path))
        if mesh is None:
            unreal.log_warning("[Torque] no mesh for " + name)
            continue
        n += instanced(name, mesh, rows, mat, frame, shadow=spec["meshes"].get(name, {}).get("shadow", True))
    log("placed %d prop instances" % n)


# --- The level -------------------------------------------------------------------------------------

# --- Replacing a map in place (shared with ue_build_level.py / ue_build_shed.py; keep the copies alike) ---
# Build into a blank unsaved world and save it over the map's path. Deleting the old map first doesn't
# work when it was the editor's start-up map: delete_asset says yes but the .umap stays on disk, and
# new_level(MAP) then refuses ("An asset already exists at this location").

def umap_file(map_path):
    return os.path.abspath(os.path.join(unreal.Paths.project_content_dir(), map_path[len("/Game/"):] + ".umap"))


def begin_fresh_level(map_path):
    """An empty unsaved world to build map_path in; returns the .umap's old modified time (or None)."""
    f = umap_file(map_path)
    before = os.path.getmtime(f) if os.path.exists(f) else None
    if not unreal.EditorLoadingAndSavingUtils.new_blank_map(False):
        raise RuntimeError("couldn't open a blank map to build %s in" % map_path)
    unreal.SystemLibrary.collect_garbage()   # let the old map's world go before saving over it
    log("building %s in a blank world (old .umap %s)" % (map_path, "from %.0f" % before if before else "absent"))
    return before


def save_fresh_level(map_path, before):
    """Save the world being built over map_path, and check the .umap really was written."""
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not world:
        raise RuntimeError("no editor world to save as %s" % map_path)
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
        raise RuntimeError("couldn't save the level as %s" % map_path)
    f = umap_file(map_path)
    if not os.path.exists(f):
        raise RuntimeError("%s isn't on disk after saving (%s)" % (map_path, f))
    after = os.path.getmtime(f)
    if before is not None and after <= before:
        raise RuntimeError("%s wasn't rewritten (%s still from %.0f)" % (map_path, f, before))
    log("saved %s (%s)" % (map_path, f))


def spawn(cls, loc=(0, 0, 0), rot=(0, 0, 0)):
    return actors.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(*rot))


def is_detail(name):
    return name.startswith("detail_") or name.startswith("far_")   # far_: walls and buildings beyond the near land


def cloud_noise_texture():
    """Data/Sky/cloud_noise.png (Tools/make_cloud_noise.py): the clouds' 3D noise as 64 slices in an 8 x 8
    sheet, linear, no mips (the cloud material picks the slices itself)."""
    f = os.path.join(os.path.dirname(data_dir()), "Sky", "cloud_noise.png")
    if not os.path.exists(f):
        unreal.log_warning("[Torque] no %s: run Tools/make_cloud_noise.py" % f)
        return None
    t = unreal.AssetImportTask()
    for k, v in (("filename", f), ("destination_path", TEX_DEST + "/Sky"), ("destination_name", "T_CloudNoise"),
                 ("automated", True), ("replace_existing", True), ("save", False)):
        t.set_editor_property(k, v)
    asset_tools.import_asset_tasks([t])
    tex = unreal.load_asset(TEX_DEST + "/Sky/T_CloudNoise")
    if tex:
        tex.set_editor_property("srgb", False)
        try_set(tex, "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        try_set(tex, "compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)   # (uncompressed: the slices must not bleed)
        try_set(tex, "lod_group", unreal.TextureGroup.TEXTUREGROUP_EFFECTS_NOT_FILTERED)
        try_set(tex, "never_stream", True)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


# The clouds' density at a point of the layer (skies plan stage 2), per metre of extinction.
# The graph hands the code only small floats (round 141 crashed on a cloud material that didn't compile,
# most likely the large-world WorldPosition into the Custom node): W0, W1, W2 are the point in repeats of
# 7 km, 1.8 km and 600 m (frac'd in the graph, carried back along the wind's drift), WB in repeats of
# 25 km for the weather's own patches across the sky. Three samples of the 3D noise (a pseudo-volume: 64
# slices of 66 texels in an 8 x 8 sheet, each with a wrapped border). The cover (MPC_Sky Cover) sets how
# much of the shape is cloud; Shape takes the height profile from a heaped cumulus (a flat base, a
# rounded top) to a flat deck; the finer noise eats the edges.
CLOUD_HLSL = r"""
float h = saturate(NormAlt);
float heaped = saturate(h / 0.07) * saturate((1.0 - h) / 0.65);
float deck = saturate(h / 0.04) * saturate((1.0 - h) / 0.2);
float profile = lerp(heaped, deck, saturate(Shape));
float3 ws[3] = { W0, W1, W2 };
float nv[3] = { 0.0, 0.0, 0.0 };
[unroll] for (int i = 0; i < 3; i++)
{
    float3 w = frac(ws[i] + float3(0.13, 0.37, 0.0) * i);
    float z = w.z * 64.0 - 0.5;
    float z0 = floor(z);
    float zf = z - z0;
    float s0 = fmod(z0 + 64.0, 64.0);
    float s1 = fmod(z0 + 1.0, 64.0);
    float2 inTile = (1.0 + w.xy * 64.0) / 66.0;
    float2 t0 = float2(fmod(s0, 8.0), floor(s0 / 8.0));
    float2 t1 = float2(fmod(s1, 8.0), floor(s1 / 8.0));
    float4 va = CloudTex.SampleLevel(CloudTexSampler, (t0 + inTile) / 8.0, 0);
    float4 vb = CloudTex.SampleLevel(CloudTexSampler, (t1 + inTile) / 8.0, 0);
    float4 v = lerp(va, vb, zf);
    nv[i] = i == 0 ? v.r : (i == 1 ? v.g : v.b);
}
float2 bw = frac(WB.xy);
float broad = CloudTex.SampleLevel(CloudTexSampler, (1.0 + bw * 64.0) / 66.0 / 8.0, 0).r;
float shp = saturate(nv[0] * 0.65 + broad * 0.35);
float cv = saturate(Cover);
float dn = saturate((shp * profile - (1.0 - cv)) / 0.18);
dn = max(dn, saturate(profile * 1.5) * smoothstep(0.9, 1.0, cv));
float erode = nv[1] * 0.7 + nv[2] * 0.3;
dn = saturate(dn - (1.0 - erode) * 0.45 * (1.0 - dn * 0.5) * (1.0 - 0.6 * saturate(Shape)));
return dn * Density * 0.1;
"""


# Our own cloud on the levels. Round 141 crashed with it (off since); now the cloud's inputs are small
# floats, the material is checked here, and the game checks it again at start (ATorqueSky's guard puts
# the engine's cloud back if it isn't a working Volume material). TORQUE_RIDE_CLOUD=0 keeps the engine's.
USE_RIDE_CLOUD = os.environ.get("TORQUE_RIDE_CLOUD", "1") != "0"


def cloud_material():
    """M_RideCloud (skies plan stage 2): the Volumetric Cloud's own material in place of the engine's
    default, which followed nothing. It reads MPC_Sky (Cover, Density, Albedo, Shape, DriftX/Y), set
    by ATorqueSky from the weather; ATorqueSky also sets the layer's base and depth. None when any part
    of it didn't go together (the engine's cloud then stays)."""
    nt = cloud_noise_texture()
    if nt is None or sky_mpc() is None:
        return None
    g = Graph(new_material("M_RideCloud"))
    bad = []
    def link(a, b, pin="", out=""):
        if not (mel.connect_material_expressions(a, out, b, pin)):
            bad.append("%s.%s -> %s" % (a.get_name(), out, pin))
    if not try_set(g.m, "material_domain", unreal.MaterialDomain.MD_VOLUME):
        bad.append("domain")
    if not try_set(g.m, "blend_mode", unreal.BlendMode.BLEND_ADDITIVE):
        bad.append("blend")
    c = g.node(unreal.MaterialExpressionCustom, -500)
    try_set(c, "code", CLOUD_HLSL)
    try_set(c, "output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    names = ("W0", "W1", "W2", "WB", "NormAlt", "Cover", "Shape", "Density", "CloudTex")
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    try_set(c, "inputs", ins)
    # The point, metres, carried back along the drift (x east, y south: MPC DriftX/DriftY, metres), then
    # in repeats of each scale and frac'd here, so the code sees small floats only.
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    q = g.node(unreal.MaterialExpressionDivide, -900, const_b=100.0)
    link(wp, q, "A")
    drift = g.node(unreal.MaterialExpressionAppendVector, -900)
    link(sky_param(g, "DriftX"), drift, "A")
    link(sky_param(g, "DriftY"), drift, "B")
    drift3 = g.node(unreal.MaterialExpressionAppendVector, -800)
    link(drift, drift3, "A")
    link(g.const(0.0), drift3, "B")
    qd = g.node(unreal.MaterialExpressionSubtract, -700)
    link(q, qd, "A")
    link(drift3, qd, "B")
    for pin, metres in (("W0", 7000.0), ("W1", 1800.0), ("W2", 600.0), ("WB", 25000.0)):
        d = g.node(unreal.MaterialExpressionDivide, -650, const_b=metres)
        link(qd, d, "A")
        f = g.node(unreal.MaterialExpressionFrac, -600)
        link(d, f, "")
        link(f, c, pin)
    attr = g.node(unreal.MaterialExpressionCloudSampleAttribute)
    link(attr, c, "NormAlt", "NormAltitudeInLayer")
    for n in ("Cover", "Shape", "Density"):
        link(sky_param(g, n), c, n)
    link(g.node(unreal.MaterialExpressionTextureObject, texture=nt), c, "CloudTex")
    # Volume outputs: albedo (base colour), extinction (subsurface colour), no glow of its own.
    for src, prop in ((sky_param(g, "Albedo"), unreal.MaterialProperty.MP_BASE_COLOR),
                      (c, unreal.MaterialProperty.MP_SUBSURFACE_COLOR),
                      (g.const(0.0), unreal.MaterialProperty.MP_EMISSIVE_COLOR)):
        if not mel.connect_material_property(src, "", prop):
            bad.append(str(prop))
    mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    try:   # (a material that didn't compile shows no instructions: the log says so for the round)
        st = mel.get_statistics(g.m)
        log("cloud material stats: pixel %s, vertex %s, samplers %s" % (
            st.get_editor_property("num_pixel_shader_instructions"), st.get_editor_property("num_vertex_shader_instructions"),
            st.get_editor_property("num_samplers")))
    except Exception as e:
        log("cloud material stats: unavailable (%s)" % e)
    domain = g.m.get_editor_property("material_domain")
    if domain != unreal.MaterialDomain.MD_VOLUME:
        bad.append("domain is %s" % domain)
    if bad:
        unreal.log_warning("[Torque] cloud material M_RideCloud NOT used: %s" % "; ".join(bad))
        return None
    log("cloud material M_RideCloud made (Volume, all links made)")
    return g.m


def sky_and_camera(reuse_cloud_shadow=False):
    """The sun, sky, air, clouds and the camera's grade, as every outdoor level has them (the ride on
    Buttertubs, and Tools/ue_build_rideuk.py's level for the real world's roads)."""
    # Sun, sky and air: a mid-afternoon Dales sun from the south west, at a sixteenth of its real
    # 75,000 lux (and the camera exposed four stops brighter to match) because Lumen's cached lighting
    # clips above EV100 12; Lumen bouncing it off the hills, clouds that cast shadows, and the valley's haze.
    sun = spawn(unreal.DirectionalLight, (0, 0, 50000), (0, -32, -45))   # shining towards the north west
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    for k, v in (("atmosphere_sun_light", True), ("intensity", 4700.0), ("light_source_angle", 0.53),
                 ("cast_cloud_shadows", True), ("cast_shadows_on_clouds", True), ("cast_shadows_on_atmosphere", True),
                 ("light_color", unreal.Color(255, 247, 235, 255))):
        try_set(sc, k, v)
    # (another level's script borrows the ride's cloud shadows as they are: making them afresh would
    # delete the material this level's sun points at)
    cs = unreal.load_asset(MAT_DIR + "/M_CloudShadow") if reuse_cloud_shadow else cloud_shadow_material()
    if cs is not None:
        ok = try_set(sc, "light_function_material", cs)
        log("cloud shadows: light function %s" % ("set" if ok else "NOT set"))
        try_set(sc, "light_function_fade_distance", 3000000.0)   # 30 km: the far fells get them too
        try_set(sc, "disabled_brightness", 1.0)
    # The planet's surface goes under the lowest land (the valley floors are 300 m below the start):
    # by default it sits at the level's origin, and land below it got a broken, banded aerial
    # perspective that read as a flat blue sea.
    atm = spawn(unreal.SkyAtmosphere, (0, 0, -80000))
    ac = atm.get_component_by_class(unreal.SkyAtmosphereComponent)
    mode = getattr(unreal, "SkyAtmosphereTransformMode", None)
    if mode is not None:
        try_set(ac, "transform_mode", getattr(mode, "PLANET_TOP_AT_COMPONENT_TRANSFORM", None) or mode.PLANET_TOP_AT_COMPONENT_TRANSFORM)
    # British air: more haze than the default, so far fells go blue-grey.
    for k, v in (("mie_scattering_scale", 0.004), ("rayleigh_scattering_scale", 0.045), ("aerial_pespective_view_distance_scale", 0.6),   # sic: the engine spells it so,
                 ("height_fog_contribution", 1.0)):
        try_set(ac, k, v)
    sky = spawn(unreal.SkyLight, (0, 0, 20000))
    skc = sky.get_component_by_class(unreal.SkyLightComponent)
    skc.set_editor_property("real_time_capture", True)
    skc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    try_set(skc, "intensity", 1.0)
    fog = spawn(unreal.ExponentialHeightFog, (0, 0, -40000))   # a thin haze, not a layer lying in the valley
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    for k, v in (("fog_density", 0.0018), ("fog_height_falloff", 0.15), ("enable_volumetric_fog", True),
                 ("volumetric_fog_scattering_distribution", 0.7), ("volumetric_fog_extinction_scale", 0.6),
                 ("volumetric_fog_distance", 8000.0), ("start_distance", 1500.0),
                 ("fog_inscattering_luminance", unreal.LinearColor(0.45, 0.46, 0.47, 1.0))):
        try_set(fc, k, v)
    try:
        cloud = spawn(unreal.VolumetricCloud)
        cc = cloud.get_component_by_class(unreal.VolumetricCloudComponent)
        for k, v in (("layer_bottom_altitude", 1.4), ("layer_height", 5.0), ("tracing_max_distance", 60.0),
                     ("shadow_tracing_distance", 12.0)):
            try_set(cc, k, v)
        # Our own cloud, following the real weather (ATorqueSky): borrowed as it is by another level's
        # script, as the cloud shadows are (making it afresh would delete what this level points at).
        rc = unreal.load_asset(MAT_DIR + "/M_RideCloud") if reuse_cloud_shadow else cloud_material()
        # Round 141 crashed on Buttertubs (VolumetricCloudRendering assert: Material domain must be
        # MD_Volume): only hand the cloud a material that really is Volume domain; otherwise the
        # engine's default cloud stays (as it does with TORQUE_RIDE_CLOUD=0).
        if rc is not None and (not USE_RIDE_CLOUD or rc.get_editor_property("material_domain") != unreal.MaterialDomain.MD_VOLUME):
            unreal.log_warning("[Torque] clouds: M_RideCloud not used (enabled %s, domain %s); the engine's default cloud stays"
                               % (USE_RIDE_CLOUD, rc.get_editor_property("material_domain")))
            rc = None
        if rc is not None:
            ok = try_set(cc, "material", rc)
            log("clouds: M_RideCloud %s" % ("set" if ok else "NOT set"))
        else:
            unreal.log_warning("[Torque] clouds: no M_RideCloud; the engine's default cloud stays")
    except Exception as e:
        unreal.log_warning("[Torque] no volumetric cloud: %s" % e)
    ppv = spawn(unreal.PostProcessVolume)
    ppv.set_editor_property("unbound", True)
    pp = ppv.get_editor_property("settings")
    # A camera exposed for a bright day (EV100 13 to 15 in real terms, four stops lower here), a filmic grade
    # nudged towards the cool, slightly muted greens of the Dales, and gentle lens touches.
    def enum(path):
        try:
            v = unreal
            for part in path.split("."):
                v = getattr(v, part)
            return v
        except AttributeError:
            unreal.log_warning("[Torque] no " + path)
            return None
    for k, v in (("auto_exposure_method", enum("AutoExposureMethod.AEM_HISTOGRAM")),
                 ("auto_exposure_min_brightness", 8.5), ("auto_exposure_max_brightness", 11.5),
                 ("auto_exposure_bias", 0.9), ("auto_exposure_speed_up", 2.0), ("auto_exposure_speed_down", 1.5),
                 ("local_exposure_highlight_contrast_scale", 0.75), ("local_exposure_shadow_contrast_scale", 0.7),
                 ("bloom_intensity", 0.35), ("vignette_intensity", 0.3), ("film_grain_intensity", 0.08),
                 ("motion_blur_amount", 0.12), ("motion_blur_max", 1.0),
                 ("white_temp", 6300.0), ("color_saturation", unreal.Vector4(1.12, 1.12, 1.12, 1.0)),
                 ("color_contrast", unreal.Vector4(1.04, 1.04, 1.04, 1.0)), ("film_toe", 0.45),
                 ("lumen_final_gather_quality", 2.0), ("lumen_scene_lighting_quality", 2.0),
                 ("lumen_scene_detail", 2.0), ("lumen_reflection_quality", 2.0),
                 ("lumen_ray_lighting_mode", enum("LumenRayLightingModeOverride.HIT_LIGHTING_FOR_REFLECTIONS"))):
        if v is not None and try_set(pp, k, v):
            try_set(pp, "override_" + k, True)
    ppv.set_editor_property("settings", pp)


def build():
    folder = data_dir()
    route = json.load(open(os.path.join(folder, "route.json")))
    meshes = import_glbs(folder)
    if "road" not in meshes:
        raise RuntimeError("the road mesh didn't import; see the Output Log")
    for name, mesh in meshes.items():
        if is_detail(name):
            nanite_off(mesh)   # no collision: the wheels only see the land and the road
            try:
                unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).remove_collisions(mesh)
            except Exception as e:
                unreal.log_warning("[Torque] couldn't remove collision from %s: %s" % (name, e))
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
    tex, roles = import_textures()
    land = land_material_scanned(tex, roles) if "grass" in tex else land_material()
    road = road_material_scanned(tex, roles) if "asphalt" in tex else road_material()
    detail = detail_material()
    prop_meshes, prop_spec = import_props()
    prop_mats = {}
    if prop_meshes:
        prop_mats = {"stone": stone_material(tex, roles), "fleece": flat_material("M_Fleece", 0.95, fine=0.4, wpo=graze_wpo),
                     "metal": flat_material("M_GalvanisedSteel", 0.42, metallic=1.0),
                     "wood": flat_material("M_BarnWood", 0.8, fine=0.15),
                     "masonry": stone_material(tex, roles, "M_Masonry", "masonry", "masonry", broad_moss=False),
                     "glass": flat_material("M_WindowGlass", 0.06),
                     "render": render_material(tex, roles),
                     "paint": flat_material("M_Paint", 0.32, fine=0.05)}
    import_bike((yaw, mirror, scale), tex, roles)

    # Start the level afresh (a second run replaces the first): built in a blank world and saved over
    # MAP at the end, checked (see begin_fresh_level).
    before = begin_fresh_level(MAP)
    rot = unreal.Rotator(0.0, 0.0, float(yaw))   # (roll, pitch, yaw) in the Python constructor
    for name, mesh in sorted(meshes.items()):
        a = actors.spawn_actor_from_object(mesh, unreal.Vector(0, 0, 0), rot)
        a.set_actor_scale3d(unreal.Vector(scale, scale * mirror, scale))
        a.set_actor_label(name)
        comp = a.get_component_by_class(unreal.StaticMeshComponent)
        comp.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        if is_detail(name):
            comp.set_material(0, detail)
            comp.set_collision_profile_name("NoCollision")
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            comp.set_editor_property("can_ever_affect_navigation", False)
            a.set_folder_path("Detail")
            continue
        comp.set_material(0, road if name == "road" else land)
        # The bike's wheels and its respawn only see actors tagged Land or Road.
        a.set_editor_property("tags", [unreal.Name("Road" if name == "road" else "Land")])
        a.set_folder_path("Terrain" if name != "road" else "Road")

    place_fab_helmet_anchor()
    if prop_meshes:
        place_props(prop_meshes, prop_spec, prop_mats, (yaw, mirror, scale))
    stone = prop_mats.get("stone") or stone_material(tex, roles)
    place_far_rock((yaw, mirror, scale), land, stone)
    place_becks((yaw, mirror, scale), stone, prop_mats.get("masonry"))

    sky_and_camera()

    place_foliage((yaw, mirror, scale))
    place_trees((yaw, mirror, scale), tex, roles)

    sp = route["spawn"]["ue"]
    heading_deg = math.degrees(route["spawn"]["heading"])
    # Sim heading 0 faces north (-y in Unreal) and turns left as it grows; Unreal yaw 0 faces east (+x)
    # and turns right (towards +y) as it grows.
    spawn(unreal.PlayerStart, (sp[0], sp[1], sp[2] + 100.0), (0, 0, -90.0 - heading_deg))

    save_fresh_level(MAP, before)
    log("built %s with %d meshes; press Play to ride" % (MAP, len(meshes)))



# Tools/ue_shed_dales.py borrows the materials and helpers above for the dale round the shed: it sets
# this flag and imports the file as a module, so the ride isn't built then.
if not getattr(sys, "torque_build_level_as_library", False):
    build()
