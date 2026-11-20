"""The level crossings' road paint in UE (World/TorqueLevelCrossing.cpp): run in the editor after the props are
imported (Tools/ue_import_props.py lx_...), and again whenever Data/Props/Textures/lx_road_paint.png changes.

    UnrealEditor-Cmd TorqueShedProto.uproject -ExecutePythonScript="Tools/ue_build_crossings.py"

  T_lx_road_paint   Data/Props/Textures/lx_road_paint.png (Tools/props/make_lx_textures.py): white paint on black,
                    three rows (a stop line with STOP over it, a stop line alone, a Give Way's broken lines)
  M_CrossingPaint   a deferred decal laying one row on the road: its brightness the paint's opacity, a slightly
                    shinier, worn white. Parameters: Row (0..2), Rows (3), FlipV (the game sets it so the words read
                    from the traffic's side). The decal's U runs along its Z (the road's length, the image's top to
                    bottom), V along its Y (across).
"""
import os

import unreal

ROOT = os.path.abspath(unreal.Paths.project_dir())
MAT_DIR = "/Game/RideUK/Materials"
TEX = os.path.join(ROOT, "Data", "Props", "Textures", "lx_road_paint.png")

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
M = unreal.MaterialProperty


def log(msg):
    unreal.log("[Torque] crossings: " + msg)


def try_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:
        unreal.log_warning("[Torque] crossings: couldn't set %s: %s" % (prop, e))
        return False


def expr(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        try_set(e, k, v)
    return e


def texture():
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", TEX)
    t.set_editor_property("destination_path", MAT_DIR)
    t.set_editor_property("destination_name", "T_lx_road_paint")
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", False)
    asset_tools.import_asset_tasks([t])
    tex = unreal.load_asset(MAT_DIR + "/T_lx_road_paint")
    if tex:
        try_set(tex, "srgb", False)
        try_set(tex, "compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
        eal.save_loaded_asset(tex)
    return tex


UV_HLSL = """
float v = lerp(UV.y, 1.0 - UV.y, FlipV);
return float2(v, (UV.x + Row) / Rows);
"""


def paint_material(tex):
    path = MAT_DIR + "/M_CrossingPaint"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    m = asset_tools.create_asset("M_CrossingPaint", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    domain = getattr(unreal.MaterialDomain, "MD_DEFERRED_DECAL", None)
    if domain is None or not try_set(m, "material_domain", domain):
        raise RuntimeError("can't make a deferred decal material on this engine")
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1200, 0)
    row = expr(m, unreal.MaterialExpressionScalarParameter, -1200, 120, parameter_name="Row", default_value=1.0)
    rows = expr(m, unreal.MaterialExpressionScalarParameter, -1200, 220, parameter_name="Rows", default_value=3.0)
    flip = expr(m, unreal.MaterialExpressionScalarParameter, -1200, 320, parameter_name="FlipV", default_value=0.0)
    c = expr(m, unreal.MaterialExpressionCustom, -950, 100)
    try_set(c, "code", UV_HLSL)
    try_set(c, "output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    ins = []
    for n in ("UV", "Row", "Rows", "FlipV"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    try_set(c, "inputs", ins)
    for n, e in (("UV", uv), ("Row", row), ("Rows", rows), ("FlipV", flip)):
        mel.connect_material_expressions(e, "", c, n)
    s = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 100, parameter_name="Atlas", texture=tex,
             sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    mel.connect_material_expressions(c, "", s, "UVs")
    # The paint: a worn white (the tarmac shows a little through it), a touch shinier than the road.
    op = expr(m, unreal.MaterialExpressionMultiply, -450, 200, const_b=0.88)
    mel.connect_material_expressions(s, "R", op, "A")
    mel.connect_material_property(op, "", M.MP_OPACITY)
    mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant3Vector, -450, -50, constant=unreal.LinearColor(0.72, 0.72, 0.7, 1.0)), "", M.MP_BASE_COLOR)
    mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant, -450, 50, r=0.6), "", M.MP_ROUGHNESS)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def main():
    tex = texture()
    if not tex:
        unreal.log_error("[Torque] crossings: couldn't import %s" % TEX)
        return
    paint_material(tex)
    log("M_CrossingPaint and T_lx_road_paint made")


main()
