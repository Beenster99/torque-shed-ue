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
  4. makes the level: the meshes, the props as instances from props.json, a sun, sky atmosphere, sky light, height fog, clouds, an unbound
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
        self.link(a, e, "A" if b is not None or cls not in (unreal.MaterialExpressionSaturate, unreal.MaterialExpressionOneMinus) else "", a_out)
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

    def finish(self, layer, extra_rough=None):
        mel.connect_material_property(layer[0], "", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(layer[1], "", unreal.MaterialProperty.MP_NORMAL)
        r_out = "R" if layer[2].get_class().get_name() == "MaterialExpressionTextureSample" else ""
        mel.connect_material_property(layer[2], r_out, unreal.MaterialProperty.MP_ROUGHNESS)
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
    if "moor" in tex:
        land = g.mix(land, g.layer(tex["moor"], roles["moor"]["tile_m"]), g.mask_noise(0.0008, -1.5, 2.5))
    if "peat" in tex:
        # Peat only in the odd hollow, darkened and desaturated: the scan is a red-brown litter.
        peat = g.layer(tex["peat"], roles["peat"]["tile_m"])
        dull = g.node(unreal.MaterialExpressionDesaturation, -600)   # Fraction is an input pin, not a property
        g.link(peat[0], dull)
        g.link(g.const(0.7), dull, "Fraction")
        peat[0] = g.op(unreal.MaterialExpressionMultiply, dull, g.const((0.55, 0.5, 0.45)))
        land = g.mix(land, peat, g.mask_noise(0.003, -7.0, 0.8))
    # The map's cover: vertex colour relative to plain grass's, softened.
    vc = g.node(unreal.MaterialExpressionVertexColor)
    tint = g.op(unreal.MaterialExpressionDivide, vc, g.const(lin((92, 112, 52))))
    tint = g.lerp(g.const((1.0, 1.0, 1.0)), tint, g.const(0.55))
    land[0] = g.op(unreal.MaterialExpressionMultiply, land[0], tint)
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
    land[0] = g.lerp(grey, land[0], g.const(1.3))
    # Big, slow variation in brightness, so the hillside isn't one even colour from afar.
    land[0] = g.op(unreal.MaterialExpressionMultiply, land[0], noise(g.m, 0.0003, 0.8, 1.15, -1200, g.y + 200))
    return g.finish(land)


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
    road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], noise(g.m, 0.002, 0.45, 0.8, -1200, g.y + 200))   # darker, as Dales tarmac is
    road = g.mix(road, paint, line)
    return g.finish(road)


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
        if ext.lower() != ".png" or stem[-2:] not in ("_n", "_d"):
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
    vc = g.node(unreal.MaterialExpressionVertexColor, -1500)
    col = g.lerp(vector("Color", (0.5, 0.5, 0.5)), vector("Color2", (0.5, 0.5, 0.5)), vc, alpha_out="R")
    shaded = g.op(unreal.MaterialExpressionMultiply, col, dtex, b_out="G")
    met = g.lerp(scalar("Metallic", 0.0), scalar("Metallic2", 0.0), vc, alpha_out="R")
    rough = g.lerp(scalar("Roughness", 0.5), scalar("Roughness2", 0.5), vc, alpha_out="R")
    roff = g.op(unreal.MaterialExpressionSubtract, dtex, a_out="R", const_b=0.5)
    roff = g.op(unreal.MaterialExpressionMultiply, roff, scalar("RoughVar", 0.0))
    rough = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionAdd, rough, roff))
    emis = g.op(unreal.MaterialExpressionMultiply, col, scalar("Emissive", 0.0))
    outs = [(shaded, "BaseColor", "MP_BASE_COLOR"), (met, "Metallic", "MP_METALLIC"), (rough, "Roughness", "MP_ROUGHNESS"),
            (nrm, "Normal", "MP_NORMAL"), (emis, "EmissiveColor", "MP_EMISSIVE_COLOR")]
    coat = [(g.const(1.0), "ClearCoat", "MP_CUSTOM_DATA0"),
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


def bike_materials(finishes):
    """A master for plain finishes and a clear coat one for paint, and an instance per finish (see
    Tools/make_speedtwin.py for what a finish holds; the rider's kit gives just colour and the two
    numbers, and the rest take defaults)."""
    tex = bike_textures()
    masters = {"base": bike_master("M_BikeMaster", tex, False), "paint": bike_master("M_BikePaint", tex, True)}
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
        for key, pname in (("color", "Color"), ("color2", "Color2")):
            c = f.get(key, f["color"])
            mel.set_material_instance_vector_parameter_value(mi, pname, unreal.LinearColor(c[0], c[1], c[2], 1.0))
        for key, pname, default in (("metallic", "Metallic", 0.0), ("roughness", "Roughness", 0.5),
                                    ("uv_scale", "UVScale", 1.0), ("normal_strength", "NormalStrength", 1.0),
                                    ("rough_var", "RoughVar", 0.0), ("emissive", "Emissive", 0.0)):
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


def rider_materials(finishes):
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
    # Leather and textiles are dielectrics with low specular; denim is very matt.
    spec = {"jeans": 0.3, "jacket": 0.45, "sole": 0.3, "liner": 0.2, "trim": 0.35}
    for name, f in finishes.items():
        kind = "gloss" if name in ("helmet", "visor") else "kit"
        mi_name = "MI_Rider_" + name
        path = RIDER_MAT_DIR + "/" + mi_name
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
        mi = asset_tools.create_asset(mi_name, RIDER_MAT_DIR, unreal.MaterialInstanceConstant,
                                      unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, masters[kind])
        c = f["color"]
        mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(c[0], c[1], c[2], 1.0))
        mel.set_material_instance_scalar_parameter_value(mi, "Roughness", float(f["roughness"]))
        mel.set_material_instance_scalar_parameter_value(mi, "Specular", float(spec.get(name, 0.5)))
        unreal.EditorAssetLibrary.save_loaded_asset(mi)
        paths[name] = mi.get_path_name()
    return paths


def import_rider():
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
    if helmet:
        nanite_off(helmet)
        unreal.EditorAssetLibrary.save_loaded_asset(helmet)
    mats = rider_materials(info["finishes"])
    log("rider: %s, helmet %s" % (body.get_path_name(), helmet.get_path_name() if helmet else "missing"))
    return {"mesh": body.get_path_name(), "helmet": helmet.get_path_name() if helmet else "", "materials": mats}


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
    rig = import_rider()
    if rig:
        assets["rider_rig"] = rig
    for part in info["parts"]:
        mesh = meshes.get(part["name"])
        if mesh:
            assets["parts"][part["name"]] = {"mesh": mesh.get_path_name(), "material": mats.get(part["finish"], "")}
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
    return g.op(unreal.MaterialExpressionMultiply, g.const((0.78, -0.62, -0.08)), mag)   # towards the north east, dipping a little


def foliage_flags(m, masked=False):
    m.set_editor_property("two_sided", True)
    try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    try_set(m, "used_with_instanced_static_meshes", True)
    try_set(m, "used_with_nanite", True)
    if masked:
        try_set(m, "blend_mode", unreal.BlendMode.BLEND_MASKED)
        try_set(m, "opacity_mask_clip_value", 0.4)


def foliage_material():
    """Our plants: vertex colour (a little varied per plant), two-sided foliage shading so the sun
    glows through the blades, ambient occlusion from the vertex alpha, and wind: UV y is how far each
    vertex may move (its height in metres, less for stiff plants)."""
    g = Graph(new_material("M_Foliage"))
    m = g.m
    foliage_flags(m)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    pir = g.node(unreal.MaterialExpressionPerInstanceRandom)
    tint = g.lerp(g.const((0.86, 0.9, 0.8)), g.const((1.12, 1.06, 0.92)), pir)
    base = g.op(unreal.MaterialExpressionMultiply, vc, tint)
    sss = g.op(unreal.MaterialExpressionMultiply, base, g.const((0.75, 0.9, 0.35)))
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    h = g.node(unreal.MaterialExpressionComponentMask, r=False, g=True, b=False, a=False)
    g.link(uv, h)
    wpo = foliage_wind(g, h, pir)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    mel.connect_material_property(vc, "A", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    mel.connect_material_property(g.const(0.62), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(g.const(0.3), "", unreal.MaterialProperty.MP_SPECULAR)
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


def scan_foliage_master(defaults):
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
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    op = tex("Opacity")
    if op:
        mel.connect_material_property(op, "R", unreal.MaterialProperty.MP_OPACITY_MASK)
    nrm = tex("Normal")
    if nrm:
        mel.connect_material_property(nrm, "RGB", unreal.MaterialProperty.MP_NORMAL)
    rough = tex("Roughness")
    mel.connect_material_property(rough if rough else g.const(0.6), "R" if rough else "", unreal.MaterialProperty.MP_ROUGHNESS)
    ao = tex("AO")
    if ao:
        mel.connect_material_property(ao, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    tr = tex("Translucency")
    sss = g.op(unreal.MaterialExpressionMultiply, tr if tr else base, g.const((0.8, 0.9, 0.5)))
    mel.connect_material_property(sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    mel.connect_material_property(g.const(0.3), "", unreal.MaterialProperty.MP_SPECULAR)
    # Height above the plant's base, in metres, from its own (local) vertex position.
    lp = g.node(unreal.MaterialExpressionLocalPosition)
    z = g.node(unreal.MaterialExpressionComponentMask, r=False, g=False, b=True, a=False)
    g.link(lp, z)
    h = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMax, z, const_b=0.0), const_b=0.01)
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
    overrides = {k: v for k, v in overrides.items() if not k.startswith("_")}
    meshes = import_glbs(folder, FOLIAGE_DEST)
    mat = foliage_material()
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
            master = scan_foliage_master(defaults)
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
            e.set_editor_property("mesh", ours)
            e.set_editor_property("material", mat)
        e.set_editor_property("cull_distance", float(s["cull_m"]) * 100.0)
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
    meshes = import_glbs(folder, PROP_DEST)
    sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for name, mesh in meshes.items():
        try:
            sme.remove_collisions(mesh)
        except Exception:
            pass
        nanite_on(mesh)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    log("props: %d meshes, %d instances" % (len(meshes), sum(len(v) for v in spec["instances"].values())))
    return meshes, spec


def instanced_usage(m):
    """Instanced Nanite meshes need these usage flags set on the material for a packaged build."""
    try_set(m, "used_with_instanced_static_meshes", True)
    try_set(m, "used_with_nanite", True)


def stone_material(tex, roles):
    """Stone from two scans (lichened limestone and bare, bedded limestone), chosen per stone by the
    vertex colour's green; red sets its brightness; blue whitens it with lichen in patches; alpha is
    moss, thickest where the surface faces up. UV0 is metres of surface."""
    g = Graph(new_material("M_Stone"))
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

    stone = layer("stone", (150, 148, 140))
    bare = layer("stone_bare", (160, 158, 152))
    st = g.mix(stone, bare, vc, alpha_out="G")
    bright = g.op(unreal.MaterialExpressionMultiply, vc, a_out="R", const_b=2.0)
    st[0] = g.op(unreal.MaterialExpressionMultiply, st[0], bright)
    # Lichen: pale crusts in patches, on the stones the blue channel picks.
    lichen = g.op(unreal.MaterialExpressionMultiply, g.mask_noise(0.02, -1.2, 1.6), vc, b_out="B")
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
    m = g.op(unreal.MaterialExpressionAdd, m, noise(g.m, 0.03, -0.8, 0.0, -1200, g.y))
    m = g.op(unreal.MaterialExpressionSaturate, m)
    m = g.op(unreal.MaterialExpressionMultiply, m, up)
    moss = layer("moss", (70, 84, 38))
    st = g.mix(st, moss, m)
    return g.finish(st)


def flat_material(name, rough, metallic=0.0, fine=0.0):
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
    mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    return g.m


def instanced(label, mesh, rows, material, frame):
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


def place_props(meshes, spec, materials, frame):
    """Instances from props.json. Data/Buttertubs/prop_swaps.json, if it's there, swaps a generated mesh
    for another asset already in the project, keeping its own materials: {"SM_Rock_03":
    "/Game/Megascans/...", "SM_Rock_04": {"mesh": "/Game/...", "scale": 0.8}}."""
    swaps = {}
    sp = os.path.join(data_dir(), "prop_swaps.json")
    if os.path.exists(sp):
        swaps = json.load(open(sp))
    n = 0
    for name, rows in sorted(spec["instances"].items()):
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
        n += instanced(name, mesh, rows, mat, frame)
    log("placed %d prop instances" % n)


# --- The level -------------------------------------------------------------------------------------

def spawn(cls, loc=(0, 0, 0), rot=(0, 0, 0)):
    return actors.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(*rot))


def is_detail(name):
    return name.startswith("detail_")


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
        prop_mats = {"stone": stone_material(tex, roles), "fleece": flat_material("M_Fleece", 0.95, fine=0.4),
                     "metal": flat_material("M_GalvanisedSteel", 0.42, metallic=1.0),
                     "wood": flat_material("M_BarnWood", 0.8, fine=0.15)}
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
            comp.set_collision_profile_name("NoCollision")
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            comp.set_editor_property("can_ever_affect_navigation", False)
            a.set_folder_path("Detail")
            continue
        comp.set_material(0, road if name == "road" else land)
        # The bike's wheels and its respawn only see actors tagged Land or Road.
        a.set_editor_property("tags", [unreal.Name("Road" if name == "road" else "Land")])
        a.set_folder_path("Terrain" if name != "road" else "Road")

    if prop_meshes:
        place_props(prop_meshes, prop_spec, prop_mats, (yaw, mirror, scale))

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
    atm = spawn(unreal.SkyAtmosphere)
    ac = atm.get_component_by_class(unreal.SkyAtmosphereComponent)
    # British air: more haze than the default, so far fells go blue-grey.
    for k, v in (("mie_scattering_scale", 0.008), ("aerial_pespective_view_distance_scale", 2.0),   # sic: the engine spells it so,
                 ("height_fog_contribution", 1.0)):
        try_set(ac, k, v)
    sky = spawn(unreal.SkyLight, (0, 0, 20000))
    skc = sky.get_component_by_class(unreal.SkyLightComponent)
    skc.set_editor_property("real_time_capture", True)
    skc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    try_set(skc, "intensity", 1.0)
    fog = spawn(unreal.ExponentialHeightFog, (0, 0, -20000))
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    for k, v in (("fog_density", 0.01), ("fog_height_falloff", 0.06), ("enable_volumetric_fog", True),
                 ("volumetric_fog_scattering_distribution", 0.7), ("volumetric_fog_extinction_scale", 0.6),
                 ("volumetric_fog_distance", 8000.0), ("start_distance", 1500.0),
                 ("fog_inscattering_luminance", unreal.LinearColor(0.35, 0.42, 0.55, 1.0))):
        try_set(fc, k, v)
    try:
        cloud = spawn(unreal.VolumetricCloud)
        cc = cloud.get_component_by_class(unreal.VolumetricCloudComponent)
        for k, v in (("layer_bottom_altitude", 1.4), ("layer_height", 5.0), ("tracing_max_distance", 60.0),
                     ("shadow_tracing_distance", 12.0)):
            try_set(cc, k, v)
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
                 ("auto_exposure_bias", 0.5), ("auto_exposure_speed_up", 2.0), ("auto_exposure_speed_down", 1.5),
                 ("local_exposure_highlight_contrast_scale", 0.75), ("local_exposure_shadow_contrast_scale", 0.85),
                 ("bloom_intensity", 0.35), ("vignette_intensity", 0.3), ("film_grain_intensity", 0.08),
                 ("motion_blur_amount", 0.35), ("motion_blur_max", 3.0),
                 ("white_temp", 6300.0), ("color_saturation", unreal.Vector4(1.05, 1.05, 1.05, 1.0)),
                 ("color_contrast", unreal.Vector4(1.12, 1.12, 1.12, 1.0)),
                 ("lumen_final_gather_quality", 2.0), ("lumen_scene_lighting_quality", 2.0),
                 ("lumen_scene_detail", 2.0), ("lumen_reflection_quality", 2.0),
                 ("lumen_ray_lighting_mode", enum("LumenRayLightingModeOverride.HIT_LIGHTING_FOR_REFLECTIONS"))):
        if v is not None and try_set(pp, k, v):
            try_set(pp, "override_" + k, True)
    ppv.set_editor_property("settings", pp)

    place_foliage((yaw, mirror, scale))

    sp = route["spawn"]["ue"]
    heading_deg = math.degrees(route["spawn"]["heading"])
    # Sim heading 0 faces north (-y in Unreal) and turns left as it grows; Unreal yaw 0 faces east (+x)
    # and turns right (towards +y) as it grows.
    spawn(unreal.PlayerStart, (sp[0], sp[1], sp[2] + 100.0), (0, 0, -90.0 - heading_deg))

    levels.save_current_level()
    log("built %s with %d meshes; press Play to ride" % (MAP, len(meshes)))


build()
