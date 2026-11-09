"""The road dirt on a bike, drawn in the bike's own materials (Torque Shed's dirt_layer.gd look), and
the wash mask that takes it off where the pressure washer's jet has been.

Used by ue_build_level.py (M_BikeMaster, M_BikePaint) and ue_build_shed.py (M_ShedEffectPaint):
add_dirt(m, x, y) puts the two custom nodes and their parameters in a material and returns them
(look: rgb the dirt's colour, a how much it covers; glow: rgb its emissive, a its roughness), for
the material to lay over its own finish.

How it looks depends on where it is on the bike, as in Torque Shed: clots and splatter of mud low down
and underneath, drawn out along the bike the way it flew, dark and wet where it's thick and dried
pale at the edges; a soft film of dust up top, thickest on what faces up and streaked where rain ran
down it; and dead flies on the forward faces of the parts up front (FlyGain, by slot), each a dark
body in a smear of guts thrown back along the airflow, some with their wings, among little midges.
Grime (the part's dirt, 0..1) sets how much: light dirt is a few specks and a haze, heavy dirt
covers it. The game (TorqueShedModel::ApplyLooks) sets each mesh's frame in the bike's (Row0..2,
RowOffset: its local cm to the bike's cm, x forward, y right, z up) and the bike's height (ZRange:
the bottom of its tyres, its top).

While the bike's pressure washed (TorqueWashJob) HasMask is 1 and the mask says how much has been
washed off each little cube round the bike, facing each of six ways (MaskA, MaskB: the cubes' z
slices side by side, BGRA: A r +x, g -x, b +y, a -y; B r +z, g -z, a 1 where there's surface), so
the dirt comes off exactly where the jet's been, glossy wet for a while after (Wet). X's scan lights
up the dirt still to do (Scan, ScanT); a part come clean flashes green (Flash).
"""

import unreal

import ue_build_spray as sp

mel = unreal.MaterialEditingLibrary

DIRT_HLSL = """
struct FBikeDirt
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
        p = frac(p * 0.3183099 + float3(0.1, 0.2, 0.3));
        p *= 17.0;
        return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
    }
    float Noise(float3 x)
    {
        float3 i = floor(x), f = frac(x);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(lerp(Hash(i), Hash(i + float3(1, 0, 0)), f.x), lerp(Hash(i + float3(0, 1, 0)), Hash(i + float3(1, 1, 0)), f.x), f.y),
                    lerp(lerp(Hash(i + float3(0, 0, 1)), Hash(i + float3(1, 0, 1)), f.x), lerp(Hash(i + float3(0, 1, 1)), Hash(i + float3(1, 1, 1)), f.x), f.y), f.z);
    }
    float Fbm(float3 p)
    {
        return Noise(p) * 0.5 + Noise(p * 2.07 + float3(3.1, 1.7, 5.3)) * 0.3 + Noise(p * 4.13 + float3(7.7, 2.9, 0.4)) * 0.2;
    }
    // A dead fly in a cell of q (on a forward face, across and up): [there, body, smear, wing].
    float4 Splat(float2 q, float chance, float2 dir, float seed)
    {
        float2 ci = floor(q);
        float3 h = float3(Hash(float3(ci, seed)), Hash(float3(ci, seed + 1.3)), Hash(float3(ci, seed + 2.9)));
        if (h.x > chance) return float4(0, 0, 0, 0);
        float2 l = frac(q) - 0.5 - (h.yz - 0.5) * 0.4;
        float a = (h.y - 0.5) * 1.2;
        float2 d = normalize(dir + float2(cos(a), sin(a)) * 0.35);
        float2 u = float2(dot(l, d), dot(l, float2(-d.y, d.x)));
        u /= 0.6 + 0.6 * h.z;
        float body = 1.0 - smoothstep(0.045, 0.075, length(u * float2(0.8, 1.25)));
        float rag = Noise(float3(u * 40.0, seed + h.x * 9.0));
        float tail = smoothstep(-0.05, 0.02, u.x) * (1.0 - smoothstep(0.15, 0.38 + 0.1 * rag, u.x));
        float width = lerp(0.12, 0.03, saturate(u.x / 0.38)) * (0.7 + 0.6 * rag);
        float smear = max(tail * (1.0 - smoothstep(width * 0.6, width, abs(u.y))), 1.0 - smoothstep(0.07, 0.11 + 0.03 * rag, length(u)));
        float2 dp = u - float2(0.3 + 0.12 * h.y, (h.z - 0.5) * 0.12);
        smear = max(smear, 1.0 - smoothstep(0.012, 0.022, length(dp)));
        float wing = step(0.55, h.z) * (1.0 - smoothstep(0.02, 0.035, length(float2(u.x + 0.01, abs(u.y) - 0.065) * float2(0.6, 1.0))));
        return float4(1.0, body, smear * 0.85, wing);
    }
};

if (Grime <= 0.0 && Scan <= 0.0 && Flash <= 0.0 && Wet <= 0.0) return float4(0, 0, 0, 0);
FBikeDirt F;
F.Dims = Dims.xyz; F.Grid = max(Dims.w, 1.0); F.Atlas = max(AtlasSize.xy, float2(1, 1));

// Where this pixel is on the bike (cm: x forward, y right, z up) and which way it faces there.
float3 m = float3(dot(R0.xyz, P), dot(R1.xyz, P), dot(R2.xyz, P)) + Off.xyz;
float3 n = float3(dot(R0.xyz, N), dot(R1.xyz, N), dot(R2.xyz, N));
n = n / max(length(n), 1e-4);

// How much is on it here (TorqueWashJob Pattern and FlyWeight work it out the same way): muck thick
// low down and underneath, a film of dust on top, flies on the forward faces of the parts up front.
float h = saturate((m.z - ZRange.x) / max(ZRange.y - ZRange.x, 1.0));
float pat = lerp(1.35, 0.4, smoothstep(0.0, 1.0, h));
if (n.z < -0.5) pat *= 1.45;
else if (n.z > 0.6) pat *= 0.75;
float up = (m.z - ZRange.x) * 0.01;              // metres off the ground
float flyw = FlyGain > 0.0 ? smoothstep(0.45, 0.8, n.x) * smoothstep(0.12, 0.4, up) : 0.0;
float lvl = min(Grime * max(pat, FlyGain * flyw * 1.6), 1.0);

// Washed off by the jet (a crisp edge where it went).
float washed = 0.0;
if (HasMask > 0.5)
{
    float3 v = (m - Origin.xyz) / max(Voxel, 0.01);
    float4 A = F.Tri(MaskA, MaskASampler, v);
    float4 B = F.Tri(MaskB, MaskBSampler, v);
    float w = max(B.a, 1e-3);
    float3 wf = float3(n.x > 0 ? A.r : A.g, n.y > 0 ? A.b : A.a, n.z > 0 ? B.r : B.g);
    float3 fair = saturate((abs(n) - 0.25) / 0.35);   // a slant washed from a side it faces well enough (TorqueWash SlantLo/Hi)
    washed = saturate(max(dot(n * n, wf), max(max(wf.x * fair.x, wf.y * fair.y), wf.z * fair.z)) / w) * saturate(B.a * 2.5);
}
float s = lvl * (1.0 - smoothstep(0.55, 0.98, washed)) * (1.0 - 0.45 * washed);

// Torque Shed's axes from here (metres: x across, y up, z back), so its dirt reads the same.
float3 mp0 = float3(m.y, m.z - ZRange.x, -m.x) * 0.01;
float3 tn = float3(n.y, n.z, -n.x);
float fly = flyw;
float mudw = saturate((1.0 - smoothstep(-0.12, 0.35, mp0.y)) + smoothstep(0.2, 0.9, -tn.y) * 0.6) * (1.0 - fly);
float dustw = max(0.0, 1.0 - mudw - fly);
float alpha = 0.0, rough = 0.0;
float3 col = float3(0, 0, 0);
float px = length(fwidth(mp0));                    // a pixel here, in metres: fine detail fades before it shimmers
if (s > 0.004)
{
    if (mudw > 0.0)
    {
        float3 mp = mp0 * float3(1.0, 1.0, 0.42);
        float shape = F.Fbm(mp * 13.0) * 0.75 + F.Noise(mp * 46.0) * 0.25;
        float thr = 1.0 - s * 1.1;
        float clot = smoothstep(thr - 0.03, thr + 0.03, shape);
        float thick = smoothstep(thr, thr + 0.22, shape);
        float3 sp = mp0 * float3(110.0, 110.0, 55.0);
        float3 si = floor(sp);
        float nearc = smoothstep(thr - 0.3, thr - 0.02, shape);
        float drop = step(1.0 - s * (0.12 + 0.5 * nearc), F.Hash(si + 5.0));
        float dr = length(frac(sp) - 0.5 - (float3(F.Hash(si), F.Hash(si + 2.1), F.Hash(si + 4.7)) - 0.5) * 0.5);
        float speck = drop * (1.0 - smoothstep(0.16, 0.26, dr)) * (1.0 - smoothstep(0.003, 0.012, px));
        speck = max(speck, drop * 0.25 * smoothstep(0.003, 0.012, px));
        float film = smoothstep(0.03, 1.0, s) * (0.25 + 0.2 * F.Noise(mp0 * 8.0));
        float mud_a = max(max(clot * 0.97, speck * 0.9), film);
        float grit = F.Noise(mp0 * 260.0);
        float dry = saturate((1.0 - thick) * 0.7 + (1.0 - clot) * 0.5 - speck * 0.6);
        float3 wet_c = lerp(float3(0.16, 0.115, 0.075), float3(0.26, 0.19, 0.12), F.Noise(mp0 * 5.0));
        float3 dry_c = lerp(float3(0.5, 0.43, 0.34), float3(0.6, 0.53, 0.42), grit);
        float3 mud_c = lerp(wet_c, dry_c, dry) * (0.88 + 0.24 * F.Noise(mp0 * 60.0));
        mud_c *= lerp(float3(1.06, 0.98, 0.9), float3(0.95, 0.98, 1.04), F.Noise(mp0 * 2.0 + 9.0));
        alpha += mud_a * mudw;
        col += mud_c * mud_a * mudw;
        rough += lerp(0.32, 0.95, dry) * mud_a * mudw;
    }
    if (dustw > 0.0)
    {
        float grain = F.Noise(mp0 * 95.0);
        float patchy = F.Fbm(mp0 * 7.0);
        float lie = lerp(0.6, 1.3, saturate(tn.y * 0.5 + 0.5));
        float runs = smoothstep(0.55, 0.85, F.Noise(mp0 * float3(70.0, 3.0, 70.0))) * (1.0 - abs(tn.y));
        float dust_a = clamp(smoothstep(0.03, 1.0, s) * (0.28 + 0.14 * grain + 0.3 * patchy) * lie * (1.0 - 0.6 * runs), 0.0, 0.92);
        float3 dust_c = lerp(float3(0.62, 0.6, 0.56), float3(0.72, 0.69, 0.63), grain);
        alpha += dust_a * dustw;
        col += dust_c * dust_a * dustw;
        rough += dust_a * dustw;
    }
    if (fly > 0.0)
    {
        float2 q = mp0.xy;
        float2 flow = tn.xy;
        flow = dot(flow, flow) > 0.02 ? normalize(flow) : float2(0.0, 1.0);
        float4 big = F.Splat(q * 26.0, s * 0.45, flow, 1.0);
        float4 small = F.Splat(q * 105.0 + float2(0.37, 0.71), s * 0.65, flow, 7.0);
        float fine = smoothstep(0.008, 0.025, px);
        float gut_t = F.Hash(float3(floor(q * 26.0), 3.0));
        float3 gut = lerp(lerp(float3(0.66, 0.62, 0.3), float3(0.62, 0.46, 0.2), gut_t), float3(0.5, 0.16, 0.1), step(0.85, gut_t));
        float body = max(big.y, small.y * 0.9);
        float smear = max(big.z, small.z * 0.7);
        float wing = big.w;
        float haze = s * 0.16;
        float fly_a = max(max(body, smear * 0.8), max(wing * 0.5, haze));
        fly_a = lerp(fly_a, max(s * 0.22, haze), fine);
        float3 fly_c = lerp(float3(0.6, 0.57, 0.46), gut, smear);
        fly_c = lerp(fly_c, float3(0.85, 0.87, 0.9), wing * (1.0 - body));
        fly_c = lerp(fly_c, float3(0.1, 0.08, 0.06), body);
        fly_c = lerp(fly_c, float3(0.55, 0.5, 0.38), fine);
        alpha += fly_a * fly;
        col += fly_c * fly_a * fly;
        rough += lerp(0.35, 0.55, body) * fly_a * fly;
    }
    col /= max(alpha, 0.001);
    rough /= max(alpha, 0.001);
    col = pow(saturate(col), 2.2);                 // picked as sRGB, as in Torque Shed
    alpha = saturate(alpha) * smoothstep(0.004, 0.03, s);
}
// Just washed: dark and glossy with water, drying off.
float sheen = Wet * smoothstep(0.15, 0.7, washed) * (1.0 - alpha);
col = lerp(col, float3(0.0, 0.0, 0.0), sheen);
rough = lerp(rough, 0.04, sheen);
alpha = max(alpha, sheen * 0.3);
// The scan: the dirt still to do lit amber to red, a band sweeping up the bike; a part come clean
// flashes green.
float3 glow = float3(0, 0, 0);
if (Scan > 0.001 && s > 0.06)
{
    float3 sc = lerp(float3(1.0, 0.62, 0.08), float3(1.0, 0.15, 0.05), saturate(s * 1.5));
    float band = frac(m.z / 60.0 - ScanT * 0.6);
    float sweep = 0.45 + 0.55 * smoothstep(0.85, 1.0, band);
    float k = Scan * saturate((s - 0.06) * 8.0);
    glow = sc * sweep * k * 1.4;
    col = lerp(col, sc * 0.35, k * 0.5);
    alpha = max(alpha, k * 0.8);
}
glow += float3(0.25, 1.0, 0.45) * Flash * 0.9;
if (!(alpha >= 0.0 && alpha <= 1.0)) return float4(0, 0, 0, 0);   // never a NaN to the screen
if (Mode < 0.5) return float4(col, alpha);
return float4(glow, clamp(rough, 0.08, 1.0));
"""


def add_dirt(m, x=-2600, y=2400):
    """The dirt's parameters and two custom nodes in material m: returns (look, glow) (see the top)."""

    def vec(name, dy, rgba):
        return sp.expr(m, unreal.MaterialExpressionVectorParameter, x, y + dy, parameter_name=name,
                       default_value=unreal.LinearColor(*rgba))

    def num(name, dy, value):
        return sp.expr(m, unreal.MaterialExpressionScalarParameter, x, y + dy, parameter_name=name, default_value=value)

    none = sp.mask_default_texture()

    def tex(name, dy):
        t = sp.expr(m, unreal.MaterialExpressionTextureObjectParameter, x, y + dy, parameter_name=name)
        if none:
            sp.try_set(t, "texture", none)
        st = sp.enum("MaterialSamplerType", "SAMPLERTYPE_LINEAR_COLOR")
        if st is not None:
            sp.try_set(t, "sampler_type", st)
        return t

    pos = sp.expr(m, unreal.MaterialExpressionLocalPosition, x, y - 200)
    nws = sp.expr(m, unreal.MaterialExpressionVertexNormalWS, x - 200, y - 100)
    nloc = sp.expr(m, unreal.MaterialExpressionTransform, x, y - 100)
    src = sp.enum("MaterialVectorCoordTransformSource", "TRANSFORMSOURCE_WORLD")
    dst = sp.enum("MaterialVectorCoordTransform", "TRANSFORM_LOCAL")
    if src is not None:
        sp.try_set(nloc, "transform_source_type", src)
    if dst is not None:
        sp.try_set(nloc, "transform_type", dst)
    mel.connect_material_expressions(nws, "", nloc, "")
    shared = [
        ("P", pos, ""), ("N", nloc, ""),
        ("R0", vec("Row0", 0, (1, 0, 0, 0)), ""), ("R1", vec("Row1", 100, (0, 1, 0, 0)), ""),
        ("R2", vec("Row2", 200, (0, 0, 1, 0)), ""), ("Off", vec("RowOffset", 300, (0, 0, 0, 0)), ""),
        ("ZRange", vec("ZRange", 400, (0, 120, 0, 0)), ""), ("Grime", num("Grime", 500, 0.0), ""),
        ("FlyGain", num("FlyGain", 600, 0.0), ""), ("HasMask", num("HasMask", 700, 0.0), ""),
        ("MaskA", tex("MaskA", 800), ""), ("MaskB", tex("MaskB", 950), ""),
        ("Origin", vec("MaskOrigin", 1100, (0, 0, 0, 0)), ""), ("Dims", vec("MaskDims", 1200, (1, 1, 1, 1)), "RGBA"),
        ("AtlasSize", vec("AtlasSize", 1300, (1, 1, 0, 0)), ""), ("Voxel", num("Voxel", 1400, 3.0), ""),
        ("Wet", num("Wet", 1500, 0.0), ""), ("Scan", num("Scan", 1600, 0.0), ""),
        ("ScanT", num("ScanT", 1700, 0.0), ""), ("Flash", num("Flash", 1800, 0.0), ""),
    ]
    outs = []
    for mode in range(2):
        k = sp.expr(m, unreal.MaterialExpressionConstant, x + 300, y + 1900 + mode * 100, r=float(mode))
        outs.append(sp.custom(m, x + 600, y + mode * 400, DIRT_HLSL, shared + [("Mode", k, "")]))
    return outs[0], outs[1]


def lay_over(m, x, y, look, glow, base, metal, rough, emissive=None):
    """The dirt over a finish: returns (base colour, metallic, roughness, emissive, how much it covers)."""

    def mask(src, r, g, b, a, dy):
        return sp.mask(m, x, y + dy, src, r, g, b, a)

    def lerp(a, b, alpha, dy):
        e = sp.expr(m, unreal.MaterialExpressionLinearInterpolate, x + 200, y + dy)
        mel.connect_material_expressions(a, "", e, "A")
        mel.connect_material_expressions(b, "", e, "B")
        mel.connect_material_expressions(alpha, "", e, "Alpha")
        return e

    col = mask(look, True, True, True, False, 0)
    cover = mask(look, False, False, False, True, 100)
    drough = mask(glow, False, False, False, True, 200)
    gl = mask(glow, True, True, True, False, 300)
    zero = sp.expr(m, unreal.MaterialExpressionConstant, x, y + 400, r=0.0)
    out_base = lerp(base, col, cover, 0)
    out_metal = lerp(metal, zero, cover, 100)
    out_rough = lerp(rough, drough, cover, 200)
    if emissive is not None:
        out_emis = sp.expr(m, unreal.MaterialExpressionAdd, x + 200, y + 300)
        mel.connect_material_expressions(emissive, "", out_emis, "A")
        mel.connect_material_expressions(gl, "", out_emis, "B")
    else:
        out_emis = gl
    return out_base, out_metal, out_rough, out_emis, cover
