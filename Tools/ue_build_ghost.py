"""The respawn ghost's look (TorqueBikePawn::UpdateGhostLook), built in the Unreal editor.

Run on its own (Tools > Execute Python Script... and pick this file, or start the editor with
-ExecutePythonScript=Tools/ue_build_ghost.py); Tools/ue_build_shed.py runs it too with the shed's
other materials. Safe to run again: the material is rebuilt from scratch. Quick: one material.

  M_RideGhostMatrix  laid over every mesh of the bike and rider (an overlay material) for the 5 s
                     after a respawn, while nothing can knock you off: a faint wireframe of the
                     model's own polygons drawn over it, so you read as a mesh for a moment. Quiet,
                     steady, never blinking (beenster, Nov 10: "we are a mesh of polygons from the
                     model - something LESS intrusive"; not the film's code rain).
                     Amount (0 off .. 1 full: the game fades it over the ghost's last second),
                     Color (the lines' colour), Opacity (how strongly the lines show close up; they
                     thin out with distance so a far bike doesn't turn solid).

The game falls back to a faint steady tint (M_ShedHighlight) until this is made.
"""

import unreal

MAT_DIR = "/Game/Shed/Materials"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
M = unreal.MaterialProperty


def log(msg):
    unreal.log("[Torque] ghost: " + msg)


def try_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:
        unreal.log_warning("[Torque] ghost: couldn't set %s: %s" % (prop, e))
        return False


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


def vec(m, name, x, y, rgb):
    return expr(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))


def num(m, name, x, y, v):
    return expr(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)


def mask(m, x, y, src, r, g, b, a):
    e = expr(m, unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=a)
    link(src, "", e, "")
    return e


def finish(m):
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def matrix_material():
    m = new_material("M_RideGhostMatrix")
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # Draw only the triangles' edges: the model's own polygons.
    if not try_set(m, "wireframe", True):
        raise RuntimeError("can't make a wireframe material")
    try_set(m, "used_with_static_lighting", False)
    # The rider's MetaHuman is skeletal (with morphs and cloth); the bike's parts are static meshes.
    for flag in ("used_with_skeletal_mesh", "used_with_morph_targets", "used_with_clothing"):
        try_set(m, flag, True)

    amount = num(m, "Amount", -900, 300, 1.0)
    strength = num(m, "Opacity", -900, 400, 0.4)
    col = vec(m, "Color", -900, 0, (0.45, 1.1, 0.95))
    glow = expr(m, unreal.MaterialExpressionMultiply, -500, 0)
    link(col, "", glow, "A")
    link(amount, "", glow, "B")
    out(glow, "", M.MP_EMISSIVE_COLOR)
    # Thinner with distance: full to 5 m, down to a third by 35 m (dense polygons far off would fill in).
    depth = expr(m, unreal.MaterialExpressionPixelDepth, -900, 550)
    far = expr(m, unreal.MaterialExpressionConstantBiasScale, -750, 550, bias=-500.0, scale=1.0 / 3000.0)
    link(depth, "", far, "")
    sat = expr(m, unreal.MaterialExpressionSaturate, -600, 550)
    link(far, "", sat, "")
    keep = expr(m, unreal.MaterialExpressionLinearInterpolate, -450, 550, const_a=1.0, const_b=0.35)
    link(sat, "", keep, "Alpha")
    op = expr(m, unreal.MaterialExpressionMultiply, -300, 350)
    link(amount, "", op, "A")
    link(strength, "", op, "B")
    op2 = expr(m, unreal.MaterialExpressionMultiply, -150, 400)
    link(op, "", op2, "A")
    link(keep, "", op2, "B")
    out(op2, "", M.MP_OPACITY)
    return finish(m)


def build():
    try:
        made = matrix_material().get_name()
        log("made " + made)
    except Exception as e:
        unreal.log_error("[Torque] ghost: M_RideGhostMatrix failed: %s" % e)


if __name__ == "__main__":
    build()
