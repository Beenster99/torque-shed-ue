"""The fluids' materials (Shed/TorqueFluidModels.cpp), built in the Unreal editor.

Run by Tools/ue_build_shed.py with the shed's other materials (or on its own: Tools > Execute Python
Script... and pick this file). Safe to run again: each material is rebuilt from scratch.

  M_FluidShell   a plastic can or bottle you can see the level through: translucent, lit per pixel
                 (so it has a proper highlight), two-sided (the far wall shows through the near one,
                 as real HDPE does). Color is the plastic's tint, Opacity how milky it is, Roughness.
                 Where the vertex colour's red is 1 it's the printed label instead: opaque, LabelColor.
  M_FluidLiquid  what's inside, sloshing: the inside of the bottle filled solid and cut off flat at the
                 surface, a plane the game moves and tips every frame (PlaneP, a point on it in world
                 space, and PlaneN, its normal pointing up out of the liquid). Two-sided, so where it's
                 cut open you look onto the inside of the far wall: that reads as the surface, lit
                 flatter and shinier (SurfaceColor, a little lighter). Color, Roughness.

The game falls back to opaque cans (no level showing) until these are made.
"""

import unreal

MAT_DIR = "/Game/Shed/Materials"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
M = unreal.MaterialProperty


def log(msg):
    unreal.log("[Torque] fluids: " + msg)


def try_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:
        unreal.log_warning("[Torque] fluids: couldn't set %s: %s" % (prop, e))
        return False


def enum(cls_name, *names):
    cls = getattr(unreal, cls_name, None)
    for n in names:
        if cls is not None and hasattr(cls, n):
            return getattr(cls, n)
    return None


def new_material(name):
    path = MAT_DIR + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    return asset_tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())


def expr(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        try_set(e, k, v)
    return e


def link(a, a_pin, b, b_pin):
    if not mel.connect_material_expressions(a, a_pin, b, b_pin):
        raise RuntimeError("couldn't connect %s.%s to %s.%s" % (a.get_name(), a_pin, b.get_name(), b_pin))


def out(e, pin, prop):
    if not mel.connect_material_property(e, pin, prop):
        raise RuntimeError("couldn't connect %s to %s" % (e.get_name(), prop))


def vec(m, name, x, y, rgb, a=1.0):
    return expr(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], a))


def num(m, name, x, y, v):
    return expr(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)


def lerp(m, x, y, a, b, alpha, a_pin="", b_pin="", alpha_pin=""):
    e = expr(m, unreal.MaterialExpressionLinearInterpolate, x, y)
    link(a, a_pin, e, "A")
    link(b, b_pin, e, "B")
    link(alpha, alpha_pin, e, "Alpha")
    return e


def finish(m):
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def shell_material():
    m = new_material("M_FluidShell")
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mode = enum("TranslucencyLightingMode", "TLM_SURFACE_PER_PIXEL_LIGHTING", "TLM_SURFACE")
    if mode is not None:
        try_set(m, "translucency_lighting_mode", mode)
    try_set(m, "two_sided", True)
    # Built at run time onto static meshes the game makes (UStaticMesh built from a mesh description).
    try_set(m, "used_with_static_lighting", False)
    vc = expr(m, unreal.MaterialExpressionVertexColor, -900, 0)
    label = expr(m, unreal.MaterialExpressionComponentMask, -750, 0, r=True, g=False, b=False, a=False)
    link(vc, "", label, "")
    tint = vec(m, "Color", -900, -250, (0.85, 0.82, 0.75))
    label_col = vec(m, "LabelColor", -900, -100, (0.15, 0.15, 0.6))
    out(lerp(m, -500, -200, tint, label_col, label), "", M.MP_BASE_COLOR)
    op = num(m, "Opacity", -900, 200, 0.35)
    one = expr(m, unreal.MaterialExpressionConstant, -900, 300, r=0.97)
    out(lerp(m, -500, 200, op, one, label), "", M.MP_OPACITY)
    rough = num(m, "Roughness", -900, 400, 0.35)
    label_rough = expr(m, unreal.MaterialExpressionConstant, -900, 500, r=0.45)
    out(lerp(m, -500, 400, rough, label_rough, label), "", M.MP_ROUGHNESS)
    out(expr(m, unreal.MaterialExpressionConstant, -500, 600, r=0.5), "", M.MP_SPECULAR)
    return finish(m)


def liquid_material():
    m = new_material("M_FluidLiquid")
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_MASKED)
    try_set(m, "two_sided", True)
    try_set(m, "opacity_mask_clip_value", 0.5)
    # Cut at the plane: keep where (world position - PlaneP) . PlaneN <= 0, so the mask is 0.5 - d
    # (d in cm: a hard edge within a hundredth of a millimetre).
    wp = expr(m, unreal.MaterialExpressionWorldPosition, -1300, 400)
    pp = vec(m, "PlaneP", -1300, 550, (0.0, 0.0, 0.0), 0.0)
    pp_rgb = expr(m, unreal.MaterialExpressionComponentMask, -1150, 550, r=True, g=True, b=True, a=False)
    link(pp, "", pp_rgb, "")
    d = expr(m, unreal.MaterialExpressionSubtract, -1000, 450)
    link(wp, "", d, "A")
    link(pp_rgb, "", d, "B")
    pn = vec(m, "PlaneN", -1300, 700, (0.0, 0.0, 1.0), 0.0)
    pn_rgb = expr(m, unreal.MaterialExpressionComponentMask, -1150, 700, r=True, g=True, b=True, a=False)
    link(pn, "", pn_rgb, "")
    dot = expr(m, unreal.MaterialExpressionDotProduct, -850, 500)
    link(d, "", dot, "A")
    link(pn_rgb, "", dot, "B")
    keep = expr(m, unreal.MaterialExpressionConstantBiasScale, -700, 500, bias=-0.5, scale=-1.0)   # 0.5 - d
    link(dot, "", keep, "")
    out(keep, "", M.MP_OPACITY_MASK)
    # The side walls in the liquid's colour; the surface (the far wall's inside, seen through the cut:
    # the back faces) a touch lighter and glossier.
    side = expr(m, unreal.MaterialExpressionTwoSidedSign, -900, -350)
    front = expr(m, unreal.MaterialExpressionConstantBiasScale, -750, -350, bias=1.0, scale=0.5)   # 1 front, 0 back
    link(side, "", front, "")
    col = vec(m, "Color", -900, -200, (0.55, 0.4, 0.1))
    surf = vec(m, "SurfaceColor", -900, -50, (0.7, 0.55, 0.2))
    out(lerp(m, -500, -150, surf, col, front), "", M.MP_BASE_COLOR)
    rough = num(m, "Roughness", -900, 150, 0.25)
    surf_rough = num(m, "SurfaceRoughness", -900, 250, 0.08)
    out(lerp(m, -500, 150, surf_rough, rough, front), "", M.MP_ROUGHNESS)
    return finish(m)


def build():
    made = []
    for f in (shell_material, liquid_material):
        try:
            made.append(f().get_name())
        except Exception as e:
            unreal.log_error("[Torque] fluids: %s failed: %s" % (f.__name__, e))
    log("made " + ", ".join(made))


if __name__ == "__main__":
    build()
