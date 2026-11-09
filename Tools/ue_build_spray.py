"""The spray mini game's materials (TorqueSprayJob.cpp, TorqueSprayFx.cpp), built in the Unreal editor.

Run by Tools/ue_build_shed.py with the shed's other materials (or on its own: Tools > Execute Python
Script... and pick this file). Safe to run again: each material is rebuilt from scratch.

  M_SprayCoat   the wet paint going on, laid over the part (an overlay material). It reads how many
                coats landed on each 2 cm cube of the part, facing each of six ways, from two small
                atlases the game updates as you spray (MaskA, MaskB: the cubes' z slices side by side,
                BGRA: A r +x, g -x, b +y, a -y; B r +z, g -z, b how much landed dry, a 1 where any
                landed; coats / 8 to 255). A thin coat shows speckled and see-through, a full one
                glossy and wet, too much sags in runs, and paint held too far off lands dry, matte and
                dusty. Once the coat's begun, the missed old surface shows flatted back (UnderColor).
                X's scan colours it by how it went: red missed, amber thin, green good, purple runs,
                blue dry, with a band sweeping up it.
  M_SprayMist   the spray's particles: soft camera-facing sprites on one instanced mesh, lit by the room,
                their colour, opacity, softness and a seed in each instance's custom data (0..5).
  M_SprayRing   the fan's footprint on the surface: a ring, unlit and over everything (Color, Opacity).
"""

import unreal

MAT_DIR = "/Game/Shed/Materials"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
M = unreal.MaterialProperty


def log(msg):
    unreal.log("[Torque] spray: " + msg)


def try_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:
        unreal.log_warning("[Torque] spray: couldn't set %s: %s" % (prop, e))
        return False


def enum(cls_name, *names):
    """The first of names that the enum has (they're spelt differently across engine versions)."""
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


def finish(m):
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def custom(m, x, y, code, inputs, out=None):
    """A custom HLSL node; inputs is [(name, expression, output pin)]."""
    c = expr(m, unreal.MaterialExpressionCustom, x, y)
    try_set(c, "code", code)
    try_set(c, "output_type", out or unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    ins = []
    for n, _, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    try_set(c, "inputs", ins)
    for n, e, pin in inputs:
        if pin == "RGBA" and not mel.connect_material_expressions(e, "RGBA", c, n):
            # No RGBA pin on this engine: rebuild the four channels as RGB appended with A.
            ap = expr(m, unreal.MaterialExpressionAppendVector, x - 150, y)
            mel.connect_material_expressions(e, "", ap, "A")
            mel.connect_material_expressions(e, "A", ap, "B")
            mel.connect_material_expressions(ap, "", c, n)
            continue
        mel.connect_material_expressions(e, pin, c, n)
    return c


def mask(m, x, y, src, r, g, b, a):
    e = expr(m, unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=a)
    mel.connect_material_expressions(src, "", e, "")
    return e


def translucent(m, lighting):
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mode = None
    if lighting == "surface":
        mode = enum("TranslucencyLightingMode", "TLM_SURFACE_PER_PIXEL_LIGHTING", "TLM_SURFACE")
    elif lighting == "volume":
        mode = enum("TranslucencyLightingMode", "TLM_VOLUMETRIC_NON_DIRECTIONAL", "TLM_VOLUMETRIC_PER_VERTEX_NON_DIRECTIONAL")
    if mode is not None:
        try_set(m, "translucency_lighting_mode", mode)


def mask_default_texture():
    """A black, linear (not sRGB) texture for MaskA/MaskB to default to: the game's atlases are linear
    BGRA, so the parameters must sample as linear colour, and unset they show no coat."""
    path = MAT_DIR + "/T_SprayMaskNone"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    for src in ("/Engine/EngineResources/Black", "/Engine/EngineResources/BlackSquareTexture",
                "/Engine/EngineMaterials/BlackSquareTexture", "/Engine/EngineResources/WhiteSquareTexture"):
        if not unreal.EditorAssetLibrary.does_asset_exist(src):
            continue
        t = unreal.EditorAssetLibrary.duplicate_asset(src, path)
        if t:
            try_set(t, "srgb", False)
            unreal.EditorAssetLibrary.save_loaded_asset(t)
            log("mask default from %s" % src)
            return t
    log("no default texture for the coat's masks")
    return None


# --- M_SprayCoat ----------------------------------------------------------------------------------

# Shared by the coat's three custom nodes (Mode 0: base colour and opacity; 1: glow and roughness;
# 2: metal). Functions live in a struct: a custom node's code is the body of one function.
COAT_HLSL = """
struct FSprayCoat
{
    float3 Dims; float Grid; float2 Atlas;

    float4 Tap(Texture2D T, SamplerState S, float2 xy, float z)
    {
        if (z < 0.0 || z > Dims.z - 1.0) return float4(0, 0, 0, 0);
        float tx = fmod(z, Grid), ty = floor(z / Grid);
        float2 p = float2(tx * Dims.x, ty * Dims.y) + clamp(xy, 0.5, Dims.xy - 0.5);
        return T.SampleLevel(S, p / Atlas, 0);
    }
    float4 Tri(Texture2D T, SamplerState S, float3 v)
    {
        float zc = v.z - 0.5;
        float z0 = floor(zc);
        return lerp(Tap(T, S, v.xy, z0), Tap(T, S, v.xy, z0 + 1.0), zc - z0);
    }
    float Hash(float3 p)
    {
        p = frac(p * 0.3183099 + 0.1);
        p *= 17.0;
        return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
    }
    float Noise(float3 p)
    {
        float3 i = floor(p), f = frac(p);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(lerp(Hash(i), Hash(i + float3(1, 0, 0)), f.x), lerp(Hash(i + float3(0, 1, 0)), Hash(i + float3(1, 1, 0)), f.x), f.y),
                    lerp(lerp(Hash(i + float3(0, 0, 1)), Hash(i + float3(1, 0, 1)), f.x), lerp(Hash(i + float3(0, 1, 1)), Hash(i + float3(1, 1, 1)), f.x), f.y), f.z);
    }
};

FSprayCoat F;
F.Dims = Dims.xyz; F.Grid = max(Dims.w, 1.0); F.Atlas = max(AtlasSize.xy, float2(1, 1));

// Where this pixel is in the part's frame, which way it faces there, and the cubes round it.
float3 m = float3(dot(R0.xyz, P), dot(R1.xyz, P), dot(R2.xyz, P)) + Off.xyz;
float3 n = float3(dot(R0.xyz, N), dot(R1.xyz, N), dot(R2.xyz, N));
n = n / max(length(n), 1e-4);
float3 v = (m - Origin.xyz) / max(Voxel, 0.01);
float4 A = F.Tri(MaskA, MaskASampler, v);
float4 B = F.Tri(MaskB, MaskBSampler, v);

// Coats facing this way (each side's count weighted by how square on it faces), averaged over the
// cubes that have any (so a bare neighbour off the surface doesn't thin it), its edge softened.
float w = max(B.a, 1e-3);
float3 n2 = n * n;
float c = (n2.x * (n.x > 0 ? A.r : A.g) + n2.y * (n.y > 0 ? A.b : A.a) + n2.z * (n.z > 0 ? B.r : B.g)) / w * 8.0;
float dry = saturate(B.b / w);
float edge = saturate(B.a * 2.5);
c *= edge;

// A thin coat goes on in specks before it flows into a film; a full one (0.8 coats) hides what's under.
float grain = F.Noise(m * 2.6) * 0.6 + F.Noise(m * 7.0) * 0.4;
float cover = saturate((c / 0.8) * 1.35 - grain * 0.45 * (1.0 - saturate(c / 0.8)));
cover = smoothstep(0.0, 1.0, cover);

// Too much: it sags in curtains down the part, heavier at each curtain's lip.
float3 up = UpInModel.xyz / max(length(UpInModel.xyz), 1e-4);
float3 side = normalize(cross(up, abs(up.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0)));
float3 side2 = cross(up, side);
float across = dot(m, side) + dot(m, side2) * 0.7;
float h = dot(m, up);
float col = F.Hash(float3(floor(across / 0.9), 3.1, 7.7));
float sag = frac(h / (2.2 + col * 2.0) + col * 5.0);
float lip = smoothstep(0.65, 0.95, sag) * (1.0 - smoothstep(0.95, 1.0, sag));
float run = saturate((c - RunLevel) / max(RunLevel * 0.45, 0.1)) * smoothstep(0.25, 0.6, col + 0.2);

// The coat as it shows: wet and deep, dulled and dusty where it went on dry.
float wet = saturate(Wet);
float3 albedo = CoatColor.rgb;
albedo = lerp(albedo, albedo * 0.82, wet * 0.5);
albedo = lerp(albedo, albedo * 0.7, run * (0.35 + 0.65 * lip));
float3 dusty = albedo * 0.72 + 0.16;
albedo = lerp(albedo, dusty, dry * 0.75);
float rough = lerp(CoatRough, 0.03, wet * (1.0 - dry));
rough = lerp(rough, 0.04, run * 0.8);
rough = lerp(rough, max(CoatRough, 0.85), dry);
float opacity = cover;
if (ClearLayer > 0.5)
{
    // Lacquer: a glassy film over the colour, barely there where it's kept (stripes, effects).
    opacity = cover * (KeepUnder > 0.5 ? 0.18 : 0.6) + run * 0.25 + dry * cover * 0.35;
}
else opacity = saturate(cover + run * 0.3);

// What the missed bits show: the old surface flatted back for it, once the coat's begun.
float under = Begun > 0.5 ? UnderColor.a : 0.0;
float shown = saturate(opacity + under * (1.0 - opacity));
float3 base = lerp(UnderColor.rgb, albedo, saturate(opacity / max(shown, 1e-3)));
float r2 = lerp(UnderRough, rough, saturate(opacity / max(shown, 1e-3)));

// The scan: how it went, swept up the part in a band.
float3 glow = float3(0, 0, 0);
if (Scan > 0.001)
{
    float3 sc = float3(0.9, 0.08, 0.05);                                  // missed
    if (c > 0.15) sc = float3(1.0, 0.6, 0.05);                            // thin
    if (c > 0.72) sc = float3(0.1, 0.85, 0.2);                            // good
    if (dry > 0.45 && c > 0.15) sc = float3(0.1, 0.45, 1.0);              // dry
    if (c >= RunLevel) sc = float3(0.75, 0.15, 0.95);                     // runs
    float band = frac(h / 60.0 - ScanT * 0.6);
    float sweep = 0.55 + 0.45 * smoothstep(0.85, 1.0, band);
    glow = sc * sweep * Scan * 1.6;
    base = lerp(base, sc * 0.4, Scan * 0.6);
    shown = lerp(shown, 0.85, Scan);
}
// The flash as the coat's done.
glow += CoatColor.rgb * Flash * cover * 0.8 + Flash * cover * 0.15;

if (Mode < 0.5) return float4(base, shown);
if (Mode < 1.5) return float4(glow, saturate(r2));
float metal = lerp(CoatMetal * (1.0 - dry * 0.6), 0.0, ClearLayer);
return float4(metal * saturate(opacity / max(shown, 1e-3)), 0, 0, 0);
"""


def coat_material():
    m = new_material("M_SprayCoat")
    translucent(m, "surface")
    try_set(m, "two_sided", False)
    try_set(m, "used_with_static_lighting", True)
    try_set(m, "used_with_instanced_static_meshes", True)
    try_set(m, "used_with_nanite", True)

    def vec(name, x, y, rgba):
        return expr(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                    default_value=unreal.LinearColor(*rgba))

    def num(name, x, y, v):
        return expr(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)

    none = mask_default_texture()

    def tex(name, x, y):
        t = expr(m, unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name=name)
        if none:
            try_set(t, "texture", none)
        st = enum("MaterialSamplerType", "SAMPLERTYPE_LINEAR_COLOR")
        if st is not None:
            try_set(t, "sampler_type", st)
        return t

    pos = expr(m, unreal.MaterialExpressionLocalPosition, -1800, 0)
    nws = expr(m, unreal.MaterialExpressionVertexNormalWS, -2000, 100)
    nloc = expr(m, unreal.MaterialExpressionTransform, -1800, 100)
    src = enum("MaterialVectorCoordTransformSource", "TRANSFORMSOURCE_WORLD")
    dst = enum("MaterialVectorCoordTransform", "TRANSFORM_LOCAL")
    if src is not None:
        try_set(nloc, "transform_source_type", src)
    if dst is not None:
        try_set(nloc, "transform_type", dst)
    mel.connect_material_expressions(nws, "", nloc, "")
    shared = [
        ("P", pos, ""), ("N", nloc, ""),
        ("R0", vec("Row0", -1800, 250, (1, 0, 0, 0)), ""), ("R1", vec("Row1", -1800, 350, (0, 1, 0, 0)), ""),
        ("R2", vec("Row2", -1800, 450, (0, 0, 1, 0)), ""), ("Off", vec("RowOffset", -1800, 550, (0, 0, 0, 0)), ""),
        ("MaskA", tex("MaskA", -1800, 650), ""), ("MaskB", tex("MaskB", -1800, 800), ""),
        ("Origin", vec("MaskOrigin", -1800, 950, (0, 0, 0, 0)), ""), ("Dims", vec("MaskDims", -1800, 1050, (1, 1, 1, 1)), "RGBA"),
        ("AtlasSize", vec("AtlasSize", -1800, 1150, (1, 1, 0, 0)), ""), ("Voxel", num("Voxel", -1800, 1250, 2.0), ""),
        ("RunLevel", num("RunLevel", -1800, 1350, 4.0), ""), ("CoatColor", vec("CoatColor", -1800, 1450, (0.8, 0.1, 0.1, 1)), ""),
        ("CoatRough", num("CoatRough", -1800, 1550, 0.15), ""), ("CoatMetal", num("CoatMetal", -1800, 1650, 0.0), ""),
        ("ClearLayer", num("ClearLayer", -1800, 1750, 0.0), ""), ("KeepUnder", num("KeepUnder", -1800, 1850, 0.0), ""),
        ("UnderColor", vec("UnderColor", -1800, 1950, (0.5, 0.5, 0.5, 0)), "RGBA"), ("UnderRough", num("UnderRough", -1800, 2050, 0.85), ""),
        ("UpInModel", vec("UpInModel", -1800, 2150, (0, 0, 1, 0)), ""), ("Wet", num("Wet", -1800, 2250, 1.0), ""),
        ("Scan", num("Scan", -1800, 2350, 0.0), ""), ("ScanT", num("ScanT", -1800, 2450, 0.0), ""),
        ("Flash", num("Flash", -1800, 2550, 0.0), ""), ("Begun", num("Begun", -1800, 2650, 0.0), ""),
    ]
    outs = []
    for mode in range(3):
        k = expr(m, unreal.MaterialExpressionConstant, -1200, 2800 + mode * 100, r=float(mode))
        outs.append(custom(m, -900, mode * 400, COAT_HLSL, shared + [("Mode", k, "")]))
    look, glow, metal = outs
    mel.connect_material_property(mask(m, -500, 0, look, True, True, True, False), "", M.MP_BASE_COLOR)
    mel.connect_material_property(mask(m, -500, 100, look, False, False, False, True), "", M.MP_OPACITY)
    mel.connect_material_property(mask(m, -500, 400, glow, True, True, True, False), "", M.MP_EMISSIVE_COLOR)
    mel.connect_material_property(mask(m, -500, 500, glow, False, False, False, True), "", M.MP_ROUGHNESS)
    mel.connect_material_property(mask(m, -500, 800, metal, True, False, False, False), "", M.MP_METALLIC)
    mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant, -500, 900, r=0.5), "", M.MP_SPECULAR)
    return finish(m)


# --- M_SprayMist ----------------------------------------------------------------------------------

# A soft blob (Soft 0 a crisp droplet, 1 a cloud), a little wispy and uneven by its seed.
MIST_HLSL = """
float2 q = UV * 2.0 - 1.0;
float d = length(q);
float a = atan2(q.y, q.x);
float wisp = 1.0 + Soft * 0.28 * (sin(a * 3.0 + Seed * 40.0) * 0.6 + sin(a * 5.0 - Seed * 23.0) * 0.4);
float r = d * wisp;
float core = saturate(1.0 - r);
float shape = pow(core, lerp(0.7, 2.2, Soft));
shape *= 1.0 - smoothstep(0.92, 1.0, r);
return shape;
"""


def mist_material():
    m = new_material("M_SprayMist")
    translucent(m, "volume")
    try_set(m, "two_sided", True)
    try_set(m, "used_with_instanced_static_meshes", True)
    try_set(m, "used_with_static_lighting", True)

    def data(i, x, y, default):
        return expr(m, unreal.MaterialExpressionPerInstanceCustomData, x, y, data_index=i, const_default_value=default)

    r, g, b = data(0, -1400, 0, 0.8), data(1, -1400, 100, 0.1), data(2, -1400, 200, 0.1)
    alpha, soft, seed = data(3, -1400, 300, 0.2), data(4, -1400, 400, 0.5), data(5, -1400, 500, 0.0)
    rg = expr(m, unreal.MaterialExpressionAppendVector, -1200, 50)
    mel.connect_material_expressions(r, "", rg, "A")
    mel.connect_material_expressions(g, "", rg, "B")
    rgb = expr(m, unreal.MaterialExpressionAppendVector, -1000, 100)
    mel.connect_material_expressions(rg, "", rgb, "A")
    mel.connect_material_expressions(b, "", rgb, "B")
    mel.connect_material_property(rgb, "", M.MP_BASE_COLOR)
    # A touch of its own light, so the mist never goes muddy in the booth's shadows.
    em = expr(m, unreal.MaterialExpressionMultiply, -700, 200, const_b=0.06)
    mel.connect_material_expressions(rgb, "", em, "A")
    mel.connect_material_property(em, "", M.MP_EMISSIVE_COLOR)
    mel.connect_material_property(expr(m, unreal.MaterialExpressionConstant, -700, 300, r=0.6), "", M.MP_ROUGHNESS)
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1400, 650)
    shape = custom(m, -1100, 600, MIST_HLSL, [("UV", uv, ""), ("Soft", soft, ""), ("Seed", seed, "")],
                   out=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    op = expr(m, unreal.MaterialExpressionMultiply, -800, 500)
    mel.connect_material_expressions(shape, "", op, "A")
    mel.connect_material_expressions(alpha, "", op, "B")
    # Fades where it meets a surface, so the cloud doesn't show a hard line across the part.
    fade = expr(m, unreal.MaterialExpressionDepthFade, -550, 500)
    try_set(fade, "fade_distance_default", 6.0)
    mel.connect_material_expressions(op, "", fade, "Opacity")
    mel.connect_material_property(fade, "", M.MP_OPACITY)
    return finish(m)


# --- M_SprayRing ----------------------------------------------------------------------------------

RING_HLSL = """
float d = length(UV - 0.5) * 2.0;
float ring = smoothstep(0.84, 0.9, d) * (1.0 - smoothstep(0.96, 1.0, d));
float fill = 0.1 * (1.0 - smoothstep(0.85, 0.95, d));
float dot_ = 1.0 - smoothstep(0.035, 0.06, d);
return saturate(ring + fill + dot_ * 0.8);
"""


def ring_material():
    m = new_material("M_SprayRing")
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    try_set(m, "two_sided", True)
    try_set(m, "disable_depth_test", True)
    col = expr(m, unreal.MaterialExpressionVectorParameter, -800, 0, parameter_name="Color",
               default_value=unreal.LinearColor(0.2, 0.9, 0.3, 1.0))
    bright = expr(m, unreal.MaterialExpressionMultiply, -500, 0, const_b=2.0)
    mel.connect_material_expressions(col, "", bright, "A")
    mel.connect_material_property(bright, "", M.MP_EMISSIVE_COLOR)
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1000, 300)
    shape = custom(m, -800, 300, RING_HLSL, [("UV", uv, "")], out=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    op = expr(m, unreal.MaterialExpressionScalarParameter, -800, 450, parameter_name="Opacity", default_value=0.6)
    mul = expr(m, unreal.MaterialExpressionMultiply, -500, 300)
    mel.connect_material_expressions(shape, "", mul, "A")
    mel.connect_material_expressions(op, "", mul, "B")
    mel.connect_material_property(mul, "", M.MP_OPACITY)
    return finish(m)


def build():
    out = {}
    for name, fn in (("coat", coat_material), ("mist", mist_material), ("ring", ring_material)):
        try:
            out[name] = fn()
        except Exception as e:
            unreal.log_error("[Torque] spray: %s material failed: %s" % (name, e))
    log("materials: %s" % ", ".join(sorted(out)))
    return out


if __name__ == "__main__":
    build()
