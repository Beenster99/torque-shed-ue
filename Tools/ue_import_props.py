"""The shed's real models in UE: imports every prop in Data/Props/props.json and dresses the shed with
them (Data/Props/swaps.json). Run in the editor, after Tools/fetch_props.py has downloaded the scans:

    UnrealEditor-Cmd TorqueShedProto.uproject -ExecutePythonScript="Tools/ue_import_props.py"
    UnrealEditor-Cmd TorqueShedProto.uproject -ExecutePythonScript="Tools/ue_import_props.py all"

'all' re-imports everything; prop ids (e.g. "Tools/ue_import_props.py bin_general bin_metal")
re-import just those; with neither, only props not yet in the project. Then build the shed
(Tools/ue_build_shed.py), which calls dress() below at its end.

  * Blender props (Data/Props/Blender/<id>.glb, committed): their material slots are named by the keys
    in Data/Props/materials.json. Each key becomes a material instance MI_Prop_<key> of one master
    (M_PropMaster, or M_PropGlass for see-through ones): its tint, roughness, metalness and glow,
    with the base's scanned maps (Poly Haven, materials.json 'scans') tiled over the props' UVs (laid
    out in metres) for the wear, grain and dents a flat colour hasn't got.
  * Poly Haven props (build/props/<id>/<id>.gltf): imported with their own scanned materials.
  * Fab props (Data/Props/fab.json): beenster's own Fab downloads, read from the launcher's cache on
    the PC (never committed), put in the props' frame and imported with their own materials. A swap
    in swaps.json with a 'prefer' list uses them when they're in, else its own prop.
Each lands at /Game/Shed/Props/<id>/SM_<id>, Nanite off (they're a few thousand triangles, and
some have glass), no collision of their own: the shed's boxes keep doing the colliding.
"""
import json
import os
import sys

import unreal

ROOT = os.path.abspath(unreal.Paths.project_dir())
DATA = os.path.join(ROOT, "Data", "Props")
FETCHED = os.path.join(ROOT, "build", "props")
FAB_OUT = os.path.join(ROOT, "build", "fab")
FAB_VAULT = os.environ.get("TORQUE_FAB_VAULT") or r"C:\ProgramData\Epic\EpicGamesLauncher\VaultCache\FabLibrary"
DEST = "/Game/Shed/Props"
MATS = DEST + "/Materials"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[Torque] props: " + msg)


def warn(msg):
    unreal.log_warning("[Torque] props: " + msg)


def try_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:
        warn("couldn't set %s: %s" % (prop, e))
        return False


# --- Materials --------------------------------------------------------------------------------------

def expr(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        try_set(e, k, v)
    return e


def link(a, a_out, b, b_in):
    if not mel.connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError("couldn't connect %s.%s -> %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))


def new_asset(name, folder, cls, factory):
    path = folder + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    return asset_tools.create_asset(name, folder, cls, factory)


def linear_white(white):
    """A linear (sRGB off) copy of the engine's white square, the default for linear-colour samplers.
    WhiteSquareTexture is sRGB, and a Linear Color sampler with an sRGB default fails to compile, so the
    whole master falls back to the checker Default Material."""
    path = MATS + "/T_PropLinearWhite"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    tex = eal.duplicate_asset("/Engine/EngineResources/WhiteSquareTexture", path)
    if not tex:
        warn("couldn't make %s: ScanRough keeps the sRGB white and M_PropMaster may not compile" % path)
        return white
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    eal.save_loaded_asset(tex)
    return tex


def master():
    """M_PropMaster: Tint x (the scan's light and shade, by Detail), Rough x (the scan's roughness, by
    Detail), Metal, the scan's normal, Glow x Tint as light. UVs x Tile (the props' UVs are metres)."""
    m = new_asset("M_PropMaster", MATS, unreal.Material, unreal.MaterialFactoryNew())
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1600, 0)
    tile = expr(m, unreal.MaterialExpressionScalarParameter, -1600, 120, parameter_name="Tile", default_value=1.0)
    uvs = expr(m, unreal.MaterialExpressionMultiply, -1400, 0)
    # WorldUV 1 lays the scan by world position in metres (the yard's painted marks, which are the
    # engine cube stretched: its own UVs would smear one scan down a 20 m line).
    wpos = expr(m, unreal.MaterialExpressionWorldPosition, -2000, 250)
    wxy = expr(m, unreal.MaterialExpressionComponentMask, -1850, 250, r=True, g=True, b=False, a=False)
    link(wpos, "", wxy, "")
    wm = expr(m, unreal.MaterialExpressionMultiply, -1700, 250, const_b=0.01)
    link(wxy, "", wm, "A")
    world_uv = expr(m, unreal.MaterialExpressionScalarParameter, -1700, 350, parameter_name="WorldUV", default_value=0.0)
    uvw = expr(m, unreal.MaterialExpressionLinearInterpolate, -1500, 120)
    link(uv, "", uvw, "A")
    link(wm, "", uvw, "B")
    link(world_uv, "", uvw, "Alpha")
    link(uvw, "", uvs, "A")
    link(tile, "", uvs, "B")
    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    grey = unreal.load_asset("/Engine/EngineResources/Black") or white
    flatn = unreal.load_asset("/Engine/EngineMaterials/FlatNormal") or unreal.load_asset("/Engine/EngineMaterials/DefaultNormal")
    col = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -1200, -300, parameter_name="ScanColor", texture=white)
    rgh = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -1200, 200, parameter_name="ScanRough", texture=linear_white(white),
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    nrm = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -1200, 600, parameter_name="ScanNormal", texture=flatn,
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    for t in (col, rgh, nrm):
        link(uvs, "", t, "UVs")
    detail = expr(m, unreal.MaterialExpressionScalarParameter, -1200, -450, parameter_name="Detail", default_value=0.6)
    gain = expr(m, unreal.MaterialExpressionScalarParameter, -1200, -550, parameter_name="ScanGain", default_value=2.0)
    # Base: Tint x lerp(1, luminance(scan) x gain, Detail)
    lum = expr(m, unreal.MaterialExpressionDotProduct, -900, -300)
    w = expr(m, unreal.MaterialExpressionConstant3Vector, -1100, -150, constant=unreal.LinearColor(0.3, 0.59, 0.11, 1.0))
    link(col, "RGB", lum, "A")
    link(w, "", lum, "B")
    lg = expr(m, unreal.MaterialExpressionMultiply, -750, -300)
    link(lum, "", lg, "A")
    link(gain, "", lg, "B")
    ld = expr(m, unreal.MaterialExpressionLinearInterpolate, -600, -300, const_a=1.0)
    link(lg, "", ld, "B")
    link(detail, "", ld, "Alpha")
    tint = expr(m, unreal.MaterialExpressionVectorParameter, -600, -500, parameter_name="Tint", default_value=unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    base = expr(m, unreal.MaterialExpressionMultiply, -350, -350)
    link(tint, "", base, "A")
    link(ld, "", base, "B")
    # The yard kit's realistic pass, each off (0) by default so the shed's props are unchanged:
    #   TrueColour  the scan's own colours x Tint (dirt, plywood, brick, rust look like the real thing)
    #   Macro       a soft light/dark variation over a couple of metres, so a big surface doesn't
    #               show the scan repeating
    #   Grime       darker and browner towards the ground, over the bottom GrimeHeight cm of the piece
    true_c = expr(m, unreal.MaterialExpressionMultiply, -350, -650)
    link(tint, "", true_c, "A")
    link(col, "RGB", true_c, "B")
    true_amt = expr(m, unreal.MaterialExpressionScalarParameter, -350, -750, parameter_name="TrueColour", default_value=0.0)
    picked = expr(m, unreal.MaterialExpressionLinearInterpolate, -150, -450)
    link(base, "", picked, "A")
    link(true_c, "", picked, "B")
    link(true_amt, "", picked, "Alpha")
    out_base = picked
    try:
        noise = expr(m, unreal.MaterialExpressionNoise, -350, -950, scale=0.004, levels=3, output_min=-1.0, output_max=1.0, quality=1)
        # Unconnected, Noise samples the absolute world position, which is what it wants anyway
        # (UE 5.8 refuses this pin by name: round 129's log).
        if not mel.connect_material_expressions(wpos, "", noise, "Position"):
            log("Macro noise on its default world position")
        macro = expr(m, unreal.MaterialExpressionScalarParameter, -350, -1050, parameter_name="Macro", default_value=0.0)
        nm = expr(m, unreal.MaterialExpressionMultiply, -150, -950)
        link(noise, "", nm, "A")
        link(macro, "", nm, "B")
        n1 = expr(m, unreal.MaterialExpressionAdd, 0, -950, const_b=1.0)
        link(nm, "", n1, "A")
        mac = expr(m, unreal.MaterialExpressionMultiply, 50, -450)
        link(out_base, "", mac, "A")
        link(n1, "", mac, "B")
        out_base = mac
    except Exception as e:
        warn("no Macro variation in M_PropMaster (%s)" % e)
    try:
        opos = expr(m, unreal.MaterialExpressionObjectPositionWS, -2000, -1300)
        wz = expr(m, unreal.MaterialExpressionComponentMask, -1850, -1200, r=False, g=False, b=True, a=False)
        link(wpos, "", wz, "")
        oz = expr(m, unreal.MaterialExpressionComponentMask, -1850, -1300, r=False, g=False, b=True, a=False)
        link(opos, "", oz, "")
        up = expr(m, unreal.MaterialExpressionSubtract, -1700, -1250)
        link(wz, "", up, "A")
        link(oz, "", up, "B")
        gh = expr(m, unreal.MaterialExpressionScalarParameter, -1700, -1150, parameter_name="GrimeHeight", default_value=40.0)
        frac = expr(m, unreal.MaterialExpressionDivide, -1550, -1250)
        link(up, "", frac, "A")
        link(gh, "", frac, "B")
        sat = expr(m, unreal.MaterialExpressionSaturate, -1400, -1250)
        link(frac, "", sat, "")
        low = expr(m, unreal.MaterialExpressionOneMinus, -1250, -1250)
        link(sat, "", low, "")
        low2 = expr(m, unreal.MaterialExpressionMultiply, -1100, -1250)
        link(low, "", low2, "A")
        link(low, "", low2, "B")
        grime = expr(m, unreal.MaterialExpressionScalarParameter, -1100, -1150, parameter_name="Grime", default_value=0.0)
        ga = expr(m, unreal.MaterialExpressionMultiply, -950, -1250)
        link(low2, "", ga, "A")
        link(grime, "", ga, "B")
        dirt = expr(m, unreal.MaterialExpressionConstant3Vector, -950, -1100, constant=unreal.LinearColor(0.5, 0.42, 0.33, 1.0))
        dirty = expr(m, unreal.MaterialExpressionMultiply, -800, -1150)
        link(out_base, "", dirty, "A")
        link(dirt, "", dirty, "B")
        gl = expr(m, unreal.MaterialExpressionLinearInterpolate, 150, -600)
        link(out_base, "", gl, "A")
        link(dirty, "", gl, "B")
        link(ga, "", gl, "Alpha")
        out_base = gl
    except Exception as e:
        warn("no Grime in M_PropMaster (%s)" % e)
    mel.connect_material_property(out_base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # Roughness: Rough x lerp(1, scan x 2, Detail), clamped.
    r2 = expr(m, unreal.MaterialExpressionMultiply, -900, 200, const_b=2.0)
    link(rgh, "R", r2, "A")
    rd = expr(m, unreal.MaterialExpressionLinearInterpolate, -700, 200, const_a=1.0)
    link(r2, "", rd, "B")
    link(detail, "", rd, "Alpha")
    rough = expr(m, unreal.MaterialExpressionScalarParameter, -700, 350, parameter_name="Rough", default_value=0.5)
    rm = expr(m, unreal.MaterialExpressionMultiply, -500, 200)
    link(rough, "", rm, "A")
    link(rd, "", rm, "B")
    rc = expr(m, unreal.MaterialExpressionClamp, -350, 200, min_default=0.03, max_default=1.0)
    link(rm, "", rc, "")
    mel.connect_material_property(rc, "", unreal.MaterialProperty.MP_ROUGHNESS)
    metal = expr(m, unreal.MaterialExpressionScalarParameter, -350, 400, parameter_name="Metal", default_value=0.0)
    mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    # Normal: the scan's, flattened toward up by (1 - Detail) so a weak Detail is a weak dent.
    fl = expr(m, unreal.MaterialExpressionLinearInterpolate, -700, 600, const_b=0.0)
    flat_v = expr(m, unreal.MaterialExpressionConstant3Vector, -900, 750, constant=unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    link(flat_v, "", fl, "A")
    link(nrm, "RGB", fl, "B")
    link(detail, "", fl, "Alpha")
    mel.connect_material_property(fl, "", unreal.MaterialProperty.MP_NORMAL)
    glow = expr(m, unreal.MaterialExpressionScalarParameter, -350, 550, parameter_name="Glow", default_value=0.0)
    em = expr(m, unreal.MaterialExpressionMultiply, -200, 550)
    link(tint, "", em, "A")
    link(glow, "", em, "B")
    mel.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def plain_master():
    """M_PropPlain: Tint, Rough, Metal, Glow x Tint. No textures at all, for a prop whose scan isn't
    downloaded yet (a texture parameter with nothing behind it shows UE's checkerboard)."""
    m = new_asset("M_PropPlain", MATS, unreal.Material, unreal.MaterialFactoryNew())
    tint = expr(m, unreal.MaterialExpressionVectorParameter, -500, -100, parameter_name="Tint", default_value=unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    mel.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(m, unreal.MaterialExpressionScalarParameter, -500, 100, parameter_name="Rough", default_value=0.5)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    metal = expr(m, unreal.MaterialExpressionScalarParameter, -500, 250, parameter_name="Metal", default_value=0.0)
    mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    glow = expr(m, unreal.MaterialExpressionScalarParameter, -500, 400, parameter_name="Glow", default_value=0.0)
    em = expr(m, unreal.MaterialExpressionMultiply, -250, 400)
    link(tint, "", em, "A")
    link(glow, "", em, "B")
    mel.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def glass_master():
    m = new_asset("M_PropGlass", MATS, unreal.Material, unreal.MaterialFactoryNew())
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    try_set(m, "translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
    tint = expr(m, unreal.MaterialExpressionVectorParameter, -500, 0, parameter_name="Tint", default_value=unreal.LinearColor(0.85, 0.9, 0.9, 1.0))
    mel.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(m, unreal.MaterialExpressionScalarParameter, -500, 150, parameter_name="Rough", default_value=0.03)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    op = expr(m, unreal.MaterialExpressionScalarParameter, -500, 300, parameter_name="Opacity", default_value=0.25)
    mel.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    spec = expr(m, unreal.MaterialExpressionConstant, -500, 400, r=1.0)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def printed_master():
    """M_PropPrinted: a printed atlas (Atlas, Rows rows top to bottom) showing row Courier (the game
    sets it per box), unit UVs over the face; Rough as for the rest."""
    m = new_asset("M_PropPrinted", MATS, unreal.Material, unreal.MaterialFactoryNew())
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1200, 0)
    u = expr(m, unreal.MaterialExpressionComponentMask, -1000, -100, r=True, g=False, b=False, a=False)
    v = expr(m, unreal.MaterialExpressionComponentMask, -1000, 100, r=False, g=True, b=False, a=False)
    link(uv, "", u, "")
    link(uv, "", v, "")
    row = expr(m, unreal.MaterialExpressionScalarParameter, -1000, 250, parameter_name="Courier", default_value=0.0)
    rows = expr(m, unreal.MaterialExpressionScalarParameter, -1000, 350, parameter_name="Rows", default_value=8.0)
    add = expr(m, unreal.MaterialExpressionAdd, -800, 150)
    link(v, "", add, "A")
    link(row, "", add, "B")
    div = expr(m, unreal.MaterialExpressionDivide, -650, 150)
    link(add, "", div, "A")
    link(rows, "", div, "B")
    uv2 = expr(m, unreal.MaterialExpressionAppendVector, -500, 0)
    link(u, "", uv2, "A")
    link(div, "", uv2, "B")
    tex = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -300, 0, parameter_name="Atlas",
               texture=unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
    link(uv2, "", tex, "UVs")
    mel.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(m, unreal.MaterialExpressionScalarParameter, -300, 250, parameter_name="Rough", default_value=0.55)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def import_texture(path, dest, name, normal=False, srgb=True):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", path)
    t.set_editor_property("destination_path", dest)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", False)
    asset_tools.import_asset_tasks([t])
    tex = unreal.load_asset(dest + "/" + name)
    if not tex:
        warn("couldn't import " + path)
        return None
    tex.set_editor_property("srgb", srgb and not normal)
    if normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        try_set(tex, "flip_green_channel", True)   # Poly Haven's nor_gl is OpenGL's
    eal.save_loaded_asset(tex)
    return tex


def scan_textures(spec):
    """{scan id: {color, rough, normal}} for the 'scans' Tools/fetch_props.py has downloaded."""
    out = {}
    wanted = set(spec["scans"].values()) | {m["scan"] for m in spec["materials"].values() if m.get("scan")}
    for sid in sorted(wanted):
        # One drawn here and kept in git (Data/Props/Scans/<id>/) before one fetched into build/props.
        kept = os.path.join(ROOT, "Data", "Props", "Scans", sid)
        folder = kept if os.path.isdir(kept) else os.path.join(FETCHED, "_scans", sid)
        dest = MATS + "/Scans/" + sid
        maps = {}
        for name in ("color", "rough", "normal"):
            asset = "%s/T_%s_%s" % (dest, sid, name)
            f = os.path.join(folder, name + ".jpg")
            if eal.does_asset_exist(asset):
                maps[name] = unreal.load_asset(asset)
            elif os.path.exists(f):
                maps[name] = import_texture(f, dest, "T_%s_%s" % (sid, name), normal=name == "normal", srgb=name == "color")
        if maps:
            out[sid] = maps
        else:
            warn("scan %s not downloaded (python Tools/fetch_props.py): its props stay plain" % sid)
    return out


def materials(spec):
    """MI_Prop_<key> for every key in materials.json."""
    m_master = master()
    m_plain = plain_master()
    m_glass = glass_master()
    m_printed = printed_master()
    scans = scan_textures(spec)
    out = {}
    for key, s in spec["materials"].items():
        glass = s["base"] == "glass"
        mi = new_asset("MI_Prop_" + key, MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        if s["base"] == "printed":
            mi.set_editor_property("parent", m_printed)
            f = os.path.join(ROOT, s["texture"])
            name = "T_" + os.path.splitext(os.path.basename(f))[0]
            tex = import_texture(f, MATS + "/Printed", name) if os.path.exists(f) else None
            if tex:
                tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
                mel.set_material_instance_texture_parameter_value(mi, "Atlas", tex)
            mel.set_material_instance_scalar_parameter_value(mi, "Rows", float(s.get("rows", 1)))
            mel.set_material_instance_scalar_parameter_value(mi, "Rough", s.get("rough", 0.55))
            eal.save_loaded_asset(mi)
            out[key] = mi
            continue
        sid = s.get("scan") or spec["scans"].get(s["base"])
        maps = {} if glass or s["base"] == "emissive" else scans.get(sid, {})
        # Scanned only when all three maps are in: an empty texture slot renders as a checkerboard.
        scanned = all(maps.get(n) for n in ("color", "rough", "normal"))
        mi.set_editor_property("parent", m_glass if glass else (m_master if scanned else m_plain))
        c = s.get("tint", [0.5, 0.5, 0.5])
        mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(c[0], c[1], c[2], 1.0))
        mel.set_material_instance_scalar_parameter_value(mi, "Rough", s.get("rough", 0.5))
        if glass:
            mel.set_material_instance_scalar_parameter_value(mi, "Opacity", s.get("opacity", 0.25))
        else:
            mel.set_material_instance_scalar_parameter_value(mi, "Metal", s.get("metal", 0.0))
            mel.set_material_instance_scalar_parameter_value(mi, "Tile", s.get("tile", 1.0))
            if s.get("emissive"):
                mel.set_material_instance_scalar_parameter_value(mi, "Glow", s["emissive"])
            # The yard kit's realistic pass (M_PropMaster only; every one defaults to off, so the shed's
            # props are as they were): the scan's own colours, how much of it shows, no visible
            # repeat, grime towards the ground, world-laid UVs.
            if scanned:
                for key_, param in (("true", "TrueColour"), ("detail", "Detail"), ("macro", "Macro"), ("grime", "Grime"), ("world_uv", "WorldUV")):
                    if key_ in s:
                        mel.set_material_instance_scalar_parameter_value(mi, param, float(s[key_]))
            if scanned:
                for name, param in (("color", "ScanColor"), ("rough", "ScanRough"), ("normal", "ScanNormal")):
                    mel.set_material_instance_texture_parameter_value(mi, param, maps[name])
        eal.save_loaded_asset(mi)
        out[key] = mi
    log("%d prop materials (%d scans; without one a prop is plain colour until Tools/fetch_props.py runs)" % (len(out), len(scans)))
    return out


# --- Meshes -----------------------------------------------------------------------------------------

def unskinned(path):
    """A glTF whose meshes are skinned (Poly Haven rigs a vice's moving jaw) imports as skeletal
    meshes, with no static mesh to place. Props don't move, so write a copy beside it with the
    skin, the joint weights and any animation taken out: the meshes in their rest pose, which the
    importer brings in as static. Anything else is returned unchanged."""
    if not path.lower().endswith(".gltf"):
        return path
    with open(path) as f:
        g = json.load(f)
    if not g.get("skins"):
        return path
    g.pop("skins", None)
    g.pop("animations", None)
    for n in g.get("nodes", []):
        n.pop("skin", None)
    for m in g.get("meshes", []):
        for prim in m.get("primitives", []):
            attrs = prim.get("attributes", {})
            for k in [k for k in attrs if k.startswith("JOINTS_") or k.startswith("WEIGHTS_")]:
                del attrs[k]
    out = path[:-5] + "_static.gltf"   # beside the original, so its .bin and textures still resolve
    with open(out, "w") as f:
        json.dump(g, f)
    log("%s: skinned, importing a static copy" % os.path.basename(path))
    return out


def import_mesh(path, pid):
    dest = "%s/%s" % (DEST, pid)
    static = unskinned(path)
    if static != path and eal.does_directory_exist(dest):
        eal.delete_directory(dest)   # an earlier skeletal import's leftovers would clash by name
    path = static
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", path)
    t.set_editor_property("destination_path", dest)
    t.set_editor_property("destination_name", "SM_" + pid)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    asset_tools.import_asset_tasks([t])
    mesh = None
    for p in t.get_editor_property("imported_object_paths") or []:
        a = unreal.load_asset(p)
        if isinstance(a, unreal.StaticMesh):
            mesh = a
            break
    if mesh is None:
        for p in eal.list_assets(dest, recursive=True):
            a = unreal.load_asset(p)
            if isinstance(a, unreal.StaticMesh):
                mesh = a
                break
    if mesh is None:
        raise RuntimeError("no static mesh imported from " + path)
    if mesh.get_path_name().split(".")[0] != mesh_path(pid):
        # Some importer versions name the mesh after the glTF node: give it its fixed name.
        eal.rename_asset(mesh.get_path_name().split(".")[0], mesh_path(pid))
        mesh = unreal.load_asset(mesh_path(pid))
    return mesh


def _nanite_on(mesh):
    try:
        return bool(mesh.get_editor_property("nanite_settings").get_editor_property("enabled"))
    except Exception:
        return False


def no_nanite(mesh, pid):
    """Props are small and many have glass or paper (translucent, which Nanite can't draw), so every
    prop is a plain static mesh. Checked on every run, not just fresh imports: an import from an
    older run (or an importer that turns Nanite on) is put right too. The materials the Poly Haven
    imports made also get their Nanite usage flag, so nothing falls back to the default material if
    a mesh is ever Nanite again."""
    if _nanite_on(mesh):
        try:
            ns = mesh.get_editor_property("nanite_settings")
            ns.set_editor_property("enabled", False)
            sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
            sub.set_nanite_settings(mesh, ns, True)
        except Exception as e:
            warn("%s: set_nanite_settings failed (%s), setting the property" % (pid, e))
        if _nanite_on(mesh):
            try:
                ns = mesh.get_editor_property("nanite_settings")
                ns.set_editor_property("enabled", False)
                mesh.set_editor_property("nanite_settings", ns)
                mesh.modify()
                mesh.post_edit_change()
            except Exception as e:
                warn("%s: couldn't turn Nanite off (%s)" % (pid, e))
        if _nanite_on(mesh):
            warn("%s: still Nanite" % pid)
        else:
            log("%s: Nanite off" % pid)
            eal.save_loaded_asset(mesh)
    # Usage flags on the base materials under this mesh's own (imported) slots.
    for s in mesh.get_editor_property("static_materials"):
        m = s.get_editor_property("material_interface")
        base = m.get_base_material() if m else None
        # Only the project's own copies can change (an engine or plugin master can't be saved).
        if not base or not base.get_path_name().startswith("/Game/") or base.get_path_name().startswith(MATS):
            continue
        try:
            if not base.get_editor_property("used_with_nanite"):
                base.set_editor_property("used_with_nanite", True)
                mel.recompile_material(base)
                eal.save_loaded_asset(base)
        except Exception as e:
            warn("%s: no Nanite usage on %s (%s)" % (pid, base.get_name(), e))


def complex_collision(mesh, pid):
    """Its triangles are its collision (complex as simple): the movable kit (TorqueFurniture) is
    walked into and stood on by its real shape, and the bike's wheels ride up a ramp's deck."""
    try:
        bs = mesh.get_editor_property("body_setup")
        if bs is None:
            # No body yet (imported without collision): a box makes one, then its triangles take over.
            unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)
            bs = mesh.get_editor_property("body_setup")
        if bs and bs.get_editor_property("collision_trace_flag") != unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
            bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            mesh.modify()
            mesh.post_edit_change()
            eal.save_loaded_asset(mesh)
            log("%s: collides by its own shape" % pid)
    except Exception as e:
        warn("%s: couldn't set complex collision (%s)" % (pid, e))


def mesh_path(pid):
    return "%s/%s/SM_%s" % (DEST, pid, pid)


def give_materials(mesh, mats):
    """A Blender prop's slots are named by materials.json keys: put the matching instance in each."""
    slots = mesh.get_editor_property("static_materials")
    done = 0
    for i, s in enumerate(slots):
        name = str(s.get_editor_property("material_slot_name"))
        mi = mats.get(name)
        if mi is None:   # importers sometimes suffix slot names (paint_red_001, MI_paint_red...)
            for key in sorted(mats, key=len, reverse=True):
                if key in name:
                    mi = mats[key]
                    break
        if mi is None:
            warn("%s: slot %s has no prop material" % (mesh.get_name(), name))
            continue
        mesh.set_material(i, mi)
        done += 1
    eal.save_loaded_asset(mesh)
    return done


def fab_glb(pid):
    """A Fab prop's corrected copy (build/fab/<id>.glb), written from the launcher's cache: its
    scene put under one node with fab.json's 'matrix' (into the props' frame) and its materials'
    alpha mode replaced. Pure glb surgery, so the PC needs no Blender. None when it isn't downloaded."""
    import struct
    spec = json.load(open(os.path.join(DATA, "fab.json")))["models"].get(pid)
    if not spec:
        warn("%s: not in fab.json" % pid)
        return None
    src = os.path.join(FAB_VAULT, *spec["vault"].split("/"))
    if not os.path.exists(src):
        log("%s: %s isn't downloaded (Fab: %s); our own model stays" % (pid, src, spec["listing"]))
        return None
    d = open(src, "rb").read()
    if d[:4] != b"glTF":
        warn("%s: %s isn't a glb" % (pid, src))
        return None
    n = struct.unpack("<I", d[12:16])[0]
    g = json.loads(d[20:20 + n].decode("utf-8"))
    rest = d[20 + n:]   # the BIN chunk, untouched
    scene = g["scenes"][g.get("scene", 0)]
    g["nodes"].append({"name": "TorqueFrame", "matrix": spec["matrix"], "children": scene["nodes"]})
    scene["nodes"] = [len(g["nodes"]) - 1]
    if spec.get("alpha"):
        for m in g.get("materials", []):
            m["alphaMode"] = spec["alpha"]
            if spec["alpha"] == "MASK":
                m["alphaCutoff"] = spec.get("alpha_cutoff", 0.5)
            else:
                m.pop("alphaCutoff", None)
    j = json.dumps(g, separators=(",", ":")).encode("utf-8")
    j += b" " * (-len(j) % 4)
    if not os.path.isdir(FAB_OUT):
        os.makedirs(FAB_OUT)
    out = os.path.join(FAB_OUT, pid + ".glb")
    with open(out, "wb") as f:
        f.write(b"glTF" + struct.pack("<II", 2, 20 + len(j) + len(rest)) + struct.pack("<I", len(j)) + b"JSON" + j + rest)
    log("%s: from Fab (%s)" % (pid, spec["listing"]))
    return out


def import_all(force=False, only=()):
    """force re-imports everything; only (prop ids) re-imports just those, the rest as without force."""
    props = json.load(open(os.path.join(DATA, "props.json")))["props"]
    spec = json.load(open(os.path.join(DATA, "materials.json")))
    # The movable shed and yard kit (Data/Furniture/models.json) is walked into, looked at and ridden
    # over by its own shape, so its meshes collide by their triangles.
    furniture = set(json.load(open(os.path.join(ROOT, "Data", "Furniture", "models.json")))["models"])
    mats = materials(spec)
    got, missing = 0, []
    for pid, p in sorted(props.items()):
        if p["source"] == "blender":
            src = os.path.join(DATA, "Blender", pid + ".glb")
        elif p["source"] == "polyhaven":
            src = os.path.join(FETCHED, p["polyhaven"], p["polyhaven"] + ".gltf")
        elif p["source"] == "fab":
            if not force and eal.does_asset_exist(mesh_path(pid)):
                got += 1
                continue
            src = fab_glb(pid)
            if src is None:
                continue   # not downloaded on this PC: not 'missing', the swap keeps our own model
        else:
            continue
        if not os.path.exists(src):
            missing.append(pid)
            continue
        if not (force or pid in only) and eal.does_asset_exist(mesh_path(pid)):
            mesh = unreal.load_asset(mesh_path(pid))
        else:
            try:
                mesh = import_mesh(src, pid)
            except Exception as e:
                warn("%s: %s" % (pid, e))
                missing.append(pid)
                continue
        no_nanite(mesh, pid)
        if p["source"] == "blender":
            give_materials(mesh, mats)
        if pid in furniture:
            complex_collision(mesh, pid)
        got += 1
    log("%d props in %s%s" % (got, DEST, "; not there: " + ", ".join(missing) if missing else ""))
    if missing and any(props[m]["source"] == "polyhaven" for m in missing):
        warn("run python Tools/fetch_props.py for the Poly Haven ones")


# --- Dressing the shed (called by Tools/ue_build_shed.py at the end of its build) -----------------------

def importer_frame():
    """How the glTF importer lays a prop down: (extra yaw, mirror x), so that a prop's right (+x in
    Torque Shed's frame) lands on UE +x and its front (+z) on UE +y at yaw 0. Read from two props
    that are lopsided on purpose: the whiteboard is all in front of its wall (z 0 .. +0.07) and the
    mobile bench's handle sticks out on its right (+x). Assumes UE's usual (x, z, y) if they're missing."""
    import math
    def lopsided(pid, thin):
        if not eal.does_asset_exist(mesh_path(pid)):
            return None
        b = unreal.load_asset(mesh_path(pid)).get_bounding_box()
        lo, hi = (b.min.x, b.min.y), (b.max.x, b.max.y)
        axes = []
        for k in (0, 1):
            ext = hi[k] - lo[k]
            sign = 1.0 if hi[k] > -lo[k] else -1.0
            axes.append((ext, k, sign))
        ext, k, sign = (min if thin else max)(axes)
        v = [0.0, 0.0]
        v[k] = sign
        return v
    front = lopsided("whiteboard", True)
    right = lopsided("bench_mobile", False)
    if not front or not right or abs(front[0] * right[0] + front[1] * right[1]) > 0.5:
        warn("couldn't read the importer's frame from the props: assuming front +y, right +x")
        return 0.0, False
    mirror = right[0] * front[1] - right[1] * front[0] < 0.0
    if mirror:
        right, front = [-right[0], right[1]], [-front[0], front[1]]
    alpha = math.degrees(math.atan2(right[1], right[0]))
    log("importer frame: right %s, front %s -> yaw %+.0f%s" % (right, front, -alpha, ", mirrored" if mirror else ""))
    return -alpha, mirror


def tag(actor, name):
    tags = list(actor.get_editor_property("tags") or [])
    if unreal.Name(name) not in tags:
        tags.append(unreal.Name(name))
        actor.set_editor_property("tags", tags)


def dress():
    """Swaps the shed's grey boxes for the real models (Data/Props/swaps.json) and adds the dressing.
    The boxes stay as invisible colliders. Returns how many models went in."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    spec = json.load(open(os.path.join(DATA, "swaps.json")))
    level = actors.get_all_level_actors()
    placed, skipped = 0, []
    extra_yaw, mirror = importer_frame()

    def spawn(entry, folder):
        mesh = unreal.load_asset(mesh_path(entry["prop"])) if eal.does_asset_exist(mesh_path(entry["prop"])) else None
        if mesh is None:
            skipped.append(entry["prop"])
            return None
        x, y, z = entry["at"]
        a = actors.spawn_actor_from_object(mesh, unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, entry.get("yaw", 0.0) + extra_yaw))
        if mirror:
            a.set_actor_scale3d(unreal.Vector(-1.0, 1.0, 1.0))
        a.set_actor_label("Prop_" + entry["prop"])
        a.set_folder_path(folder)
        # The game finds a prop it moves or hides by this (ATorqueShedKit: the engine stand, the
        # fire extinguisher): labels don't reach a packaged game, tags do.
        tag(a, "Prop:" + entry["prop"])
        c = a.get_component_by_class(unreal.StaticMeshComponent)
        c.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        c.set_collision_profile_name("NoCollision")
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        return a

    for s in spec["swaps"]:
        # 'prefer': the props to use instead, all at the swap's place, when every one is imported
        # (a Fab model on our shelf); else the swap's own prop.
        alt = s.get("prefer") or []
        if alt and all(eal.does_asset_exist(mesh_path(p)) for p in alt):
            for p in alt:
                spawn(dict(s, prop=p), "Props")
            log("%s: using %s" % (s["prop"], " + ".join(alt)))
        elif spawn(s, "Props") is None:
            continue
        hidden = 0
        for a in level:
            label = a.get_actor_label()
            if any(label.startswith(h[:-1]) if h.endswith("*") else label == h for h in s["hide"]):
                a.set_actor_hidden_in_game(True)
                tag(a, "Hidden:" + s["prop"])   # its collider, for the game to move or drop with the prop
                try:
                    a.set_is_temporarily_hidden_in_editor(True)
                except Exception:
                    pass
                c = a.get_component_by_class(unreal.StaticMeshComponent)
                if c:
                    c.set_editor_property("visible", False)
                    c.set_editor_property("cast_shadow", False)
                hidden += 1
        if hidden == 0:
            warn("swap %s hid no boxes (labels %s): check the shed's labels" % (s["prop"], s["hide"]))
        placed += 1
    for d in spec.get("dress", []):
        if spawn(d, "Props/Dressing") is not None:
            placed += 1
    log("dressed the shed: %d models%s" % (placed, "; not imported yet: " + ", ".join(sorted(set(skipped))) if skipped else ""))
    fab_look_anchors(actors)
    return placed


def fab_look_anchors(actors):
    """The Fab meshes the yard kit wears (Data/Furniture/fab_looks.json) are loaded by path at run time,
    so the package needs them cooked: one hidden actor per mesh in the shed map does that, cooking
    just those meshes and what they use (cooking their whole pack folders added ~830 MB)."""
    looks = json.load(open(os.path.join(ROOT, "Data", "Furniture", "fab_looks.json")))["looks"]
    for i, path in enumerate(sorted({l["mesh"].split(".")[0] for l in looks.values()})):
        mesh = unreal.load_asset(path) if eal.does_asset_exist(path) else None
        if mesh is None:
            warn("Fab look %s isn't in the project: the yard keeps our own model" % path)
            continue
        a = actors.spawn_actor_from_object(mesh, unreal.Vector(-50000.0 - 2000.0 * i, -50000.0, -20000.0), unreal.Rotator(0.0, 0.0, 0.0))
        a.set_actor_label("FabLookCook_%s" % path.rsplit("/", 1)[-1])
        a.set_folder_path("Props/FabLookCook")
        a.set_actor_hidden_in_game(True)
        c = a.get_component_by_class(unreal.StaticMeshComponent)
        if c:
            c.set_collision_profile_name("NoCollision")
            c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            c.set_editor_property("visible", False)
            c.set_editor_property("cast_shadow", False)
        log("Fab look %s: anchored in the shed map so it's cooked" % path)


if __name__ == "__main__":
    words = " ".join(sys.argv[1:]).split()
    import_all(force="all" in words, only=tuple(w for w in words if w != "all"))
