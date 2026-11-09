"""The wash mini game's material (TorqueWashJob.cpp), built in the Unreal editor.

Run by Tools/ue_build_shed.py with the shed's other materials (after ue_build_spray.py, whose helpers
and mask default texture it uses), or on its own. Safe to run again: rebuilt from scratch.

  M_WashDirt   the muck on a bike in the wash bay, laid over each part (an overlay material), washed off
               exactly where the jet has been. It reads how much has been washed off each little cube
               of the bike, facing each of six ways, from two small atlases the game updates as you wash
               (MaskA, MaskB: the cubes' z slices side by side, BGRA: A r +x, g -x, b +y, a -y; B r +z,
               g -z, a 1 where there's surface; 0..255 washed). The muck lies as road muck does: thick
               low down and on the undersides, a film of dust on top (Grime: the part's dirt; PatScale
               evens its pattern out to that), blotchy and splattered. X's scan lights the muck still
               to do amber to red, a band sweeping up the bike; a part coming clean flashes green.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue_build_spray as sp  # noqa: E402

M = unreal.MaterialProperty
mel = unreal.MaterialEditingLibrary


def log(msg):
    unreal.log("[Torque] wash: " + msg)


WASH_HLSL = """
struct FWashDirt
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

FWashDirt F;
F.Dims = Dims.xyz; F.Grid = max(Dims.w, 1.0); F.Atlas = max(AtlasSize.xy, float2(1, 1));

// Where this pixel is in the bike's frame, which way it faces there, and the cubes round it.
float3 m = float3(dot(R0.xyz, P), dot(R1.xyz, P), dot(R2.xyz, P)) + Off.xyz;
float3 n = float3(dot(R0.xyz, N), dot(R1.xyz, N), dot(R2.xyz, N));
n = n / max(length(n), 1e-4);
float3 v = (m - Origin.xyz) / max(Voxel, 0.01);
float4 A = F.Tri(MaskA, MaskASampler, v);
float4 B = F.Tri(MaskB, MaskBSampler, v);

// How much has been washed off the surfaces here facing this way (each side weighted by how square on
// it faces), averaged over the cubes that have surface in them.
float w = max(B.a, 1e-3);
float3 n2 = n * n;
float washed = saturate((n2.x * (n.x > 0 ? A.r : A.g) + n2.y * (n.y > 0 ? A.b : A.a) + n2.z * (n.z > 0 ? B.r : B.g)) / w);
washed *= saturate(B.a * 2.5);

// The muck as it lies: thick low down and underneath, a film of dust up top (TorqueWashJob Pattern).
float h = saturate((m.z - ZRange.x) / max(ZRange.y - ZRange.x, 1.0));
float pat = lerp(1.35, 0.4, smoothstep(0.0, 1.0, h));
if (n.z < -0.5) pat *= 1.45;
else if (n.z > 0.6) pat *= 0.75;
float level = Grime * pat * PatScale * (1.0 - washed);

// Blotchy, splattered, a little streaked where the last wash dried; the edge of what's washed ragged.
float nz = F.Noise(m * 0.35) * 0.55 + F.Noise(m * 1.3) * 0.3 + F.Noise(m * 4.0) * 0.15;
float thick = saturate(level * 1.5);
float opacity = saturate(thick * (0.5 + 1.0 * nz));
float spl = smoothstep(0.78, 0.86, F.Noise(m * 2.3 + 17.0)) * saturate(level * 3.0);
opacity = min(max(opacity, spl * 0.9), 0.96);
float3 dust = float3(0.50, 0.47, 0.42);
float3 mud = lerp(float3(0.30, 0.25, 0.19), float3(0.40, 0.34, 0.26), F.Noise(m * 0.8));
float3 base = lerp(dust, mud, saturate(level * 1.6));
float rough = lerp(0.8, 0.95, saturate(level * 2.0));

// The scan: the muck still to do, lit amber to red, a band sweeping up the bike.
float3 glow = float3(0, 0, 0);
if (Scan > 0.001 && level > 0.04)
{
    float3 sc = lerp(float3(1.0, 0.62, 0.08), float3(1.0, 0.15, 0.05), saturate(level * 1.5));
    float band = frac(m.z / 60.0 - ScanT * 0.6);
    float sweep = 0.45 + 0.55 * smoothstep(0.85, 1.0, band);
    float k = Scan * saturate((level - 0.04) * 8.0);
    glow = sc * sweep * k * 1.4;
    base = lerp(base, sc * 0.35, k * 0.5);
    opacity = max(opacity, k * 0.8);
}
// A part come clean: a green flash over it.
glow += float3(0.25, 1.0, 0.45) * Flash * 0.9;
opacity = max(opacity, Flash * 0.4);

if (Mode < 0.5) return float4(base, opacity);
return float4(glow, rough);
"""


def dirt_material():
    m = sp.new_material("M_WashDirt")
    sp.translucent(m, "surface")
    sp.try_set(m, "two_sided", False)
    sp.try_set(m, "used_with_static_lighting", True)
    sp.try_set(m, "used_with_instanced_static_meshes", True)
    sp.try_set(m, "used_with_nanite", True)

    def vec(name, x, y, rgba):
        return sp.expr(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                       default_value=unreal.LinearColor(*rgba))

    def num(name, x, y, value):
        return sp.expr(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)

    none = sp.mask_default_texture()

    def tex(name, x, y):
        t = sp.expr(m, unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name=name)
        if none:
            sp.try_set(t, "texture", none)
        st = sp.enum("MaterialSamplerType", "SAMPLERTYPE_LINEAR_COLOR")
        if st is not None:
            sp.try_set(t, "sampler_type", st)
        return t

    pos = sp.expr(m, unreal.MaterialExpressionLocalPosition, -1800, 0)
    nws = sp.expr(m, unreal.MaterialExpressionVertexNormalWS, -2000, 100)
    nloc = sp.expr(m, unreal.MaterialExpressionTransform, -1800, 100)
    src = sp.enum("MaterialVectorCoordTransformSource", "TRANSFORMSOURCE_WORLD")
    dst = sp.enum("MaterialVectorCoordTransform", "TRANSFORM_LOCAL")
    if src is not None:
        sp.try_set(nloc, "transform_source_type", src)
    if dst is not None:
        sp.try_set(nloc, "transform_type", dst)
    mel.connect_material_expressions(nws, "", nloc, "")
    shared = [
        ("P", pos, ""), ("N", nloc, ""),
        ("R0", vec("Row0", -1800, 250, (1, 0, 0, 0)), ""), ("R1", vec("Row1", -1800, 350, (0, 1, 0, 0)), ""),
        ("R2", vec("Row2", -1800, 450, (0, 0, 1, 0)), ""), ("Off", vec("RowOffset", -1800, 550, (0, 0, 0, 0)), ""),
        ("MaskA", tex("MaskA", -1800, 650), ""), ("MaskB", tex("MaskB", -1800, 800), ""),
        ("Origin", vec("MaskOrigin", -1800, 950, (0, 0, 0, 0)), ""), ("Dims", vec("MaskDims", -1800, 1050, (1, 1, 1, 1)), "RGBA"),
        ("AtlasSize", vec("AtlasSize", -1800, 1150, (1, 1, 0, 0)), ""), ("Voxel", num("Voxel", -1800, 1250, 3.0), ""),
        ("ZRange", vec("ZRange", -1800, 1350, (0, 100, 0, 0)), ""), ("Grime", num("Grime", -1800, 1450, 0.0), ""),
        ("PatScale", num("PatScale", -1800, 1550, 1.0), ""), ("Scan", num("Scan", -1800, 1650, 0.0), ""),
        ("ScanT", num("ScanT", -1800, 1750, 0.0), ""), ("Flash", num("Flash", -1800, 1850, 0.0), ""),
    ]
    outs = []
    for mode in range(2):
        k = sp.expr(m, unreal.MaterialExpressionConstant, -1200, 2000 + mode * 100, r=float(mode))
        outs.append(sp.custom(m, -900, mode * 400, WASH_HLSL, shared + [("Mode", k, "")]))
    look, glow = outs
    mel.connect_material_property(sp.mask(m, -500, 0, look, True, True, True, False), "", M.MP_BASE_COLOR)
    mel.connect_material_property(sp.mask(m, -500, 100, look, False, False, False, True), "", M.MP_OPACITY)
    mel.connect_material_property(sp.mask(m, -500, 400, glow, True, True, True, False), "", M.MP_EMISSIVE_COLOR)
    mel.connect_material_property(sp.mask(m, -500, 500, glow, False, False, False, True), "", M.MP_ROUGHNESS)
    mel.connect_material_property(sp.expr(m, unreal.MaterialExpressionConstant, -500, 700, r=0.0), "", M.MP_METALLIC)
    mel.connect_material_property(sp.expr(m, unreal.MaterialExpressionConstant, -500, 800, r=0.3), "", M.MP_SPECULAR)
    return sp.finish(m)


def build():
    out = {}
    try:
        out["dirt"] = dirt_material()
    except Exception as e:
        unreal.log_error("[Torque] wash: dirt material failed: %s" % e)
    log("materials: %s" % ", ".join(sorted(out)))
    return out


if __name__ == "__main__":
    build()
