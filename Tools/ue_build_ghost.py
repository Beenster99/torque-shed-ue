"""The respawn ghost's look (TorqueBikePawn::UpdateGhostLook), built in the Unreal editor.

Run on its own (Tools > Execute Python Script... and pick this file, or start the editor with
-ExecutePythonScript=Tools/ue_build_ghost.py); Tools/ue_build_shed.py runs it too with the shed's
other materials. Safe to run again: the materials are rebuilt from scratch. Quick: two materials.

  M_RideGhostMatrix  the green wireframe of the model's own polygons: the bike and rider drawn as
                     only this for the ghost's first 3.5 s after a respawn, while nothing can knock
                     you off. Steady, never blinking. Amount (0 off .. 1), Color (the lines'
                     colour), Opacity (how strongly the lines show close up; they thin out with
                     distance so a far bike doesn't turn solid).

  M_RideGhostFill    the fade back to solid (the last 1.5 s): over the bike's own materials, a green
                     fill that clears to nothing (Color, Opacity).

beenster, Nov 11: "only show the green mesh and then fade to solid": for the ghost's first 3.5 s the
game puts M_RideGhostMatrix in place of all the bike's and rider's materials (only the wire shows),
then gives them back under M_RideGhostFill. Without the wire the fill stands in, see-through.
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


# It's put in place of every material of the bike and rider (and over them): the rider's MetaHuman
# is skeletal (with morphs and cloth); the bike's parts are static meshes.
WITH = ("used_with_skeletal_mesh", "used_with_morph_targets", "used_with_clothing",
        "used_with_instanced_static_meshes", "used_with_spline_meshes")


def fill_material():
    """M_RideGhostFill: the fade back to solid. Laid over the bike and rider (the overlay) once their
    own materials are back, green and nearly opaque at first, brighter at the edges, clearing as the
    game takes Opacity down to 0. Color, Opacity."""
    m = new_material("M_RideGhostFill")
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    try_set(m, "used_with_static_lighting", False)
    for flag in WITH:
        try_set(m, flag, True)
    col = vec(m, "Color", -900, 0, (0.45, 1.1, 0.95))
    strength = num(m, "Opacity", -900, 300, 0.9)
    fr = expr(m, unreal.MaterialExpressionFresnel, -900, 150, exponent=2.0, base_reflect_fraction=0.0)
    rim = expr(m, unreal.MaterialExpressionConstantBiasScale, -750, 150, bias=0.6, scale=1.0)   # 0.6 facing you .. 1.6 edge-on
    link(fr, "", rim, "")
    glow = expr(m, unreal.MaterialExpressionMultiply, -500, 0)
    link(col, "", glow, "A")
    link(rim, "", glow, "B")
    out(glow, "", M.MP_EMISSIVE_COLOR)
    out(strength, "", M.MP_OPACITY)
    return finish(m)


def matrix_material():
    m = new_material("M_RideGhostMatrix")
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # Draw only the triangles' edges: the model's own polygons.
    if not try_set(m, "wireframe", True):
        raise RuntimeError("can't make a wireframe material")
    try_set(m, "used_with_static_lighting", False)
    # The rider's MetaHuman is skeletal (with morphs and cloth); the bike's parts are static meshes.
    for flag in WITH:
        try_set(m, flag, True)

    amount = num(m, "Amount", -900, 300, 1.0)
    strength = num(m, "Opacity", -900, 400, 0.65)
    col = vec(m, "Color", -900, 0, (0.7, 2.2, 1.8))
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
    for f in (matrix_material, fill_material):
        try:
            log("made " + f().get_name())
        except Exception as e:
            unreal.log_error("[Torque] ghost: %s failed: %s" % (f.__name__, e))


if __name__ == "__main__":
    build()
