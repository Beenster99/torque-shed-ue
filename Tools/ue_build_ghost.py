"""The respawn ghost's "matrix" look (TorqueBikePawn::UpdateGhostLook), built in the Unreal editor.

Run on its own (Tools > Execute Python Script... and pick this file, or start the editor with
-ExecutePythonScript=Tools/ue_build_ghost.py); Tools/ue_build_shed.py runs it too with the shed's
other materials. Safe to run again: the material is rebuilt from scratch. Quick: one material.

  M_RideGhostMatrix  laid over every mesh of the bike and rider (an overlay material) for the 5 s
                     after a respawn, while nothing can knock you off: a see-through green digital
                     copy of you. Glowing edges (fresnel), a fine lattice locked to the bike, columns
                     of code glyphs falling down it, and a soft scan band rising slowly through it.
                     Steady, never blinking (beenster, Nov 10: "a matrix of me for 5 seconds rather
                     than flashing").
                     Parameters the game sets every frame: Amount (0 off .. 1 full: the ghost's
                     look, fading over its last second), Origin (the bike's position, world cm) and
                     AxisX/AxisY/AxisZ (its forward, right and up), so the pattern rides with the
                     bike instead of sliding over it. Color: the green.

The game falls back to a plain steady green tint (M_ShedHighlight) until this is made.
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


def custom(m, x, y, code, inputs):
    """A custom HLSL node returning a float4; inputs is [(name, expression, output pin)]."""
    c = expr(m, unreal.MaterialExpressionCustom, x, y)
    try_set(c, "code", code)
    try_set(c, "output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    ins = []
    for n, _, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    try_set(c, "inputs", ins)
    for n, e, pin in inputs:
        link(e, pin, c, n)
    return c


def finish(m):
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


# P: the pixel's offset from the bike (world cm); Ax/Ay/Az: the bike's forward, right, up; N: the
# pixel's world normal; Fr: fresnel (0 facing you .. 1 edge-on); T: time, s; Amount 0..1; Col: green.
# Returns the glow (rgb) and how much it covers (a). Everything is in the bike's own frame, so the
# pattern stays put on the bike at any speed (in world space it would stream over it: a shimmer).
MATRIX_HLSL = """
float3 q = float3(dot(P, Ax), dot(P, Ay), dot(P, Az));
float3 n = abs(float3(dot(N, Ax), dot(N, Ay), dot(N, Az)));
// The face's own plane: sides use (forward, up), the front and back (right, up), tops (forward, right).
float2 uv = n.y >= n.x ? q.xz : q.yz;
float upness = saturate((n.z - 0.6) * 2.5);
uv = lerp(uv, q.xy, upness);

// A fine lattice, 5 cm, anti-aliased by fwidth and fading out where it would get too dense to draw.
float2 gc = uv / 5.0;
float2 gw = max(fwidth(gc), 1e-4);
float2 gd = abs(frac(gc) - 0.5);
float2 gl = smoothstep(0.5 - gw * 1.5, 0.5, gd);
float grid = max(gl.x, gl.y) * saturate(1.0 - max(gw.x, gw.y) * 3.0);

// Columns of glyphs falling down it (the code rain): 2.5 cm columns, 3.5 cm rows, each column its
// own speed and start; a bright head with a fading trail behind it. Glyphs are 3 x 5 dot blocks that
// change now and then, each cell on its own beat (so it shimmers like code, never all at once).
float2 cell = float2(2.5, 3.5);
float2 cid = floor(uv / cell);
float h1 = frac(sin(dot(cid.xx, float2(12.9898, 78.233))) * 43758.5453);
float h2 = frac(sin(dot(cid.xx, float2(39.3468, 11.135))) * 24634.6345);
float speed = 0.35 + 0.65 * h1;                           // 40-row runs per second, x0.6
float run = 40.0;
float head = frac(cid.y / run + T * speed * 0.6 + h2);    // 0 at the head (falling), rising up the trail above it
float trail = pow(saturate(1.0 - head * 2.2), 1.6);
float isHead = smoothstep(0.03, 0.0, head);
float beat = floor(T * (1.5 + 2.0 * h2) + h1 * 7.0);
float2 sub = floor(frac(uv / cell) * float2(4.0, 6.0));   // 4 x 6 with a 1-dot gap: 3 x 5 glyph dots
float gap = (sub.x > 2.5 || sub.y > 4.5) ? 0.0 : 1.0;
float bit = step(0.45, frac(sin(dot(float3(cid, beat) + float3(sub * 0.37, 0.0), float3(17.13, 91.71, 47.37))) * 31718.927));
float cw = max(fwidth(uv.x / cell.x), fwidth(uv.y / cell.y));
float glyphs = gap * bit * saturate(1.0 - cw * 2.5);      // fades to a soft glow where too small to read
float rain = (glyphs * 0.85 + 0.15) * trail * (1.0 - upness * 0.7);
float heads = isHead * gap * (0.5 + 0.5 * bit) * (1.0 - upness * 0.7);

// A soft scan band rising slowly through the whole bike and rider (every 3 s).
float band = exp(-pow((frac(q.z / 220.0 - T / 3.0) - 0.5) * 9.0, 2.0));

float glow = 0.10 + Fr * 1.1 + grid * 0.45 + rain * 0.9 + heads * 1.6 + band * 0.35;
float3 rgb = Col * glow + float3(0.6, 1.0, 0.6) * heads * 1.2;
float a = saturate(0.16 + Fr * 0.55 + grid * 0.25 + rain * 0.45 + heads * 0.5 + band * 0.12) * Amount;
return float4(rgb * Amount, a);
"""


def matrix_material():
    m = new_material("M_RideGhostMatrix")
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    try_set(m, "used_with_static_lighting", False)
    # The rider's MetaHuman is skeletal (with morphs and cloth); the bike's parts are static meshes.
    for flag in ("used_with_skeletal_mesh", "used_with_morph_targets", "used_with_clothing", "used_with_nanite"):
        try_set(m, flag, True)

    wp = expr(m, unreal.MaterialExpressionWorldPosition, -1400, -200)
    origin = vec(m, "Origin", -1400, -100, (0.0, 0.0, 0.0))
    rel = expr(m, unreal.MaterialExpressionSubtract, -1200, -150)
    link(wp, "", rel, "A")
    link(origin, "", rel, "B")
    ax = vec(m, "AxisX", -1400, 50, (1.0, 0.0, 0.0))
    ay = vec(m, "AxisY", -1400, 150, (0.0, 1.0, 0.0))
    az = vec(m, "AxisZ", -1400, 250, (0.0, 0.0, 1.0))
    nrm = expr(m, unreal.MaterialExpressionVertexNormalWS, -1400, 350)
    fr = expr(m, unreal.MaterialExpressionFresnel, -1400, 450, exponent=2.5, base_reflect_fraction=0.0)
    t = expr(m, unreal.MaterialExpressionTime, -1400, 550)
    amount = num(m, "Amount", -1400, 650, 1.0)
    col = vec(m, "Color", -1400, 750, (0.15, 1.0, 0.35))

    def rgb(e, x, y):
        return mask(m, x, y, e, True, True, True, False)

    c = custom(m, -900, 200, MATRIX_HLSL, [
        ("P", rel, ""),
        ("Ax", rgb(ax, -1200, 50), ""), ("Ay", rgb(ay, -1200, 150), ""), ("Az", rgb(az, -1200, 250), ""),
        ("N", nrm, ""), ("Fr", fr, ""), ("T", t, ""), ("Amount", amount, ""),
        ("Col", rgb(col, -1200, 750), ""),
    ])
    out(mask(m, -600, 150, c, True, True, True, False), "", M.MP_EMISSIVE_COLOR)
    out(mask(m, -600, 300, c, False, False, False, True), "", M.MP_OPACITY)
    return finish(m)


def build():
    try:
        made = matrix_material().get_name()
        log("made " + made)
    except Exception as e:
        unreal.log_error("[Torque] ghost: M_RideGhostMatrix failed: %s" % e)


if __name__ == "__main__":
    build()
