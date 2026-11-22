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
    // A droplet of road spray in a cell of c (drawn out fore and aft the way it flew): 0..1 how much
    // of it covers here. chance: how many cells have one; size about a third of the cell, varied.
    float Drop(float3 c, float chance, float seed)
    {
        float3 ci = floor(c);
        if (Hash(ci + seed) > chance) return 0.0;
        float3 j = float3(Hash(ci + seed + 2.1), Hash(ci + seed + 4.7), Hash(ci + seed + 6.3)) - 0.5;
        float r = length(frac(c) - 0.5 - j * 0.45);
        float size = 0.1 + 0.22 * (j.x + 0.5);
        return 1.0 - smoothstep(size * 0.65, size, r);
    }
    // The muck a wheel throws onto a point on the bike (m cm, facing n, the bike's frame), 0..1.
    // W: its axle's x and z, its tyre's radius and half width (cm). Muck picked up off the road rides
    // round on the tread and flies off it along the tyre's way of turning (a straight line on from the
    // tangent where it lets go), most of it just after it leaves the road, at the back of the tyre,
    // less carried on over the top. So a point's hit by what let go where the tangent through it
    // touches the tyre, if it faces back along that line, as wide as the tread and spreading as it goes:
    // behind the front wheel the front of the engine; above and behind the rear one the undertray and
    // the underside of the hugger and the tail. Beside a wheel, inside its round (a caliper, a fork
    // leg's foot, the swingarm's end), the spray off its sides. TorqueBikeDirt::Spray works the same.
    float Spray(float3 m, float3 n, float4 W)
    {
        if (W.z < 1.0) return 0.0;
        float2 v = float2(m.x - W.x, m.z - W.y);
        float r = length(v);
        float d = r - W.z;
        float side = abs(m.y) - W.w;
        if (d < 0.0) return 0.5 * (1.0 - smoothstep(4.0, 16.0, side));
        float psi = atan2(v.y, -v.x);                 // round from straight back, up over the top
        float rel = psi - acos(saturate(W.z / r));    // where it let go of the tyre (0: straight back)
        float relD = degrees(rel);
        float leave = smoothstep(-100.0, -70.0, relD) * lerp(1.0, 0.25, smoothstep(15.0, 120.0, relD)) * (1.0 - smoothstep(130.0, 165.0, relD));
        float2 t = float2(sin(rel), cos(rel));        // its way (x forward, z up)
        float face = smoothstep(-0.05, 0.45, -dot(n.xz, t));
        float lat = 1.0 - smoothstep(0.25 * d, 6.0 + 0.6 * d, side);
        return leave * face * lat * exp(-d / 35.0);
    }
};

if (Grime <= 0.0 && Scan <= 0.0 && Flash <= 0.0 && Wet <= 0.0) return float4(0, 0, 0, 0);
FBikeDirt F;
F.Dims = Dims.xyz; F.Grid = max(Dims.w, 1.0); F.Atlas = max(AtlasSize.xy, float2(1, 1));

// Where this pixel is on the bike (cm: x forward, y right, z up) and which way it faces there.
float3 m = float3(dot(R0.xyz, P), dot(R1.xyz, P), dot(R2.xyz, P)) + Off.xyz;
float3 n = float3(dot(R0.xyz, N), dot(R1.xyz, N), dot(R2.xyz, N));
n = n / max(length(n), 1e-4);

// A part that turns with a wheel (Spin: the axle's x and z, on, the axle's y): its muck lies evenly round it, as
// it would on a wheel that's been ridden, not caked on whichever side was at the bottom when it was
// drawn. It's taken as lying at the height half way down from the axle to where the point is round
// the wheel, and facing neither up nor down (TorqueBikeDirt::Pattern works it out the same way).
float3 mw = m, nw = n;
if (Spin.z > 0.5)
{
    mw.z = Spin.y - 0.5 * length(float2(m.x - Spin.x, m.z - Spin.y));
    nw.z = 0.0;
    nw = nw / max(length(nw), 1e-4);
}

// How much is on it here (TorqueBikeDirt::Pattern works it out the same way for the wash game).
// On a bike (its wheels known, SprayF and SprayR: each axle's x and z, its tyre's radius and half
// width): the muck the wheels throw (Spray), splash off the road on whatever's low down, and road film
// under the belly, with no more than a faint settled dust anywhere else; the wheels and tyres mucky
// all round, but for the middle two thirds of the tread, which the road keeps clean. A loose part
// (no wheels): muck thick low down and underneath, a film of dust on top. Flies on the forward faces
// of the parts up front.
float up = (mw.z - ZRange.x) * 0.01;             // metres off the ground
float pat, mudk;
if (SprayF.z > 0.5 || SprayR.z > 0.5)
{
    float muck, dust = 0.1 + 0.1 * smoothstep(0.6, 1.0, nw.z);
    if (Spin.z > 0.5)
    {
        float4 W = abs(m.x - SprayF.x) < abs(m.x - SprayR.x) ? SprayF : SprayR;
        float R = max(W.z, 1.0), hw = max(W.w, 1.0);
        float r = length(float2(m.x - Spin.x, m.z - Spin.y));
        muck = 0.35 + 0.85 * smoothstep(0.4 * R, 0.95 * R, r);   // more out towards the tyre
        float crown = smoothstep(0.82 * R, 0.88 * R, r) * (1.0 - smoothstep(0.85, 0.97, abs(n.y)));
        float mid = 1.0 - smoothstep(0.6 * hw, 0.72 * hw, abs(m.y - Spin.w));
        float kept = 1.0 - 0.97 * crown * mid;   // the tread's middle two thirds: worn clean on the road
        muck *= kept;
        dust *= kept;
    }
    else
    {
        muck = max(F.Spray(m, n, SprayF), F.Spray(m, n, SprayR)) * 1.4;
        muck = max(muck, (1.0 - smoothstep(0.08, 0.4, up)) * (0.55 + 0.45 * saturate(0.5 - n.z)) * 1.1);
        muck = max(muck, smoothstep(0.5, 0.9, -n.z) * (1.0 - smoothstep(0.3, 0.8, up)) * 0.7);
    }
    pat = max(muck, dust);
    mudk = saturate((muck - dust) * 3.0);
}
else
{
    float h = saturate((mw.z - ZRange.x) / max(ZRange.y - ZRange.x, 1.0));
    pat = lerp(1.35, 0.4, smoothstep(0.0, 1.0, h));
    if (nw.z < -0.5) pat *= 1.45;
    else if (nw.z > 0.6) pat *= 0.75;
    mudk = -1.0;
}
float flyw = FlyGain > 0.0 ? smoothstep(0.45, 0.8, nw.x) * smoothstep(0.12, 0.4, up) : 0.0;
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
float3 tn = float3(nw.y, nw.z, -nw.x);
float fly = flyw;
float mudw = (mudk >= 0.0 ? mudk : saturate((1.0 - smoothstep(-0.12, 0.35, up)) + smoothstep(0.2, 0.9, -tn.y) * 0.6)) * (1.0 - fly);
float dustw = max(0.0, 1.0 - mudw - fly);
float alpha = 0.0, rough = 0.0;
float3 col = float3(0, 0, 0);
float px = length(fwidth(mp0));                    // a pixel here, in metres: fine detail fades before it shimmers
if (s > 0.004)
{
    if (mudw > 0.0 && mudk >= 0.0)
    {
        // On a ridden bike: road spray, not mud. Fine dark speckles flicked up off the road, spatters
        // of thin brown road water dried to a pale stain with a darker rim, and a whitish bloom of
        // road salt where it's wettest; the paint shows through all of it. Heavier dirt is more of
        // them, not a thicker coat.
        // (Build 168, "unusual swing arm graphic effect": a heavily dirty swingarm and silencer read as
        // a dense yellow-green texture. So: thinner, never more than about half covered, and in neutral
        // grey-browns that stay grey once they're turned from sRGB, not tan.)
        float k = saturate(s) * 0.6;                         // how thick the spray is here
        float3 sp = mp0 * float3(1.0, 1.0, 0.5);              // drawn out fore and aft, the way it flew
        float far = smoothstep(0.002, 0.008, px);           // too small to draw one by one: a faint haze
        float fine = F.Drop(sp * 420.0, k * 0.5, 1.0);
        float speck = max(F.Drop(sp * 160.0, k * 0.35, 9.0), fine * 0.75);
        speck = lerp(speck, k * 0.1, far);
        float3 c2 = sp * 38.0;
        float rag = (F.Noise(mp0 * 320.0) - 0.5) * 0.08;
        float blot = max(F.Drop(c2, k * 0.35, 21.0), F.Drop(c2 * 2.3 + 0.37, k * 0.3, 33.0));
        float rim = blot * (1.0 - smoothstep(0.2, 0.75, blot + rag * 4.0));
        float stain = (saturate(blot + rag) * 0.12 + rim * 0.16) * (1.0 - far * 0.6);
        float salt = smoothstep(0.4, 1.0, s) * smoothstep(0.5, 0.85, F.Fbm(mp0 * float3(9.0, 3.0, 9.0))) * 0.18;
        float wash = k * 0.08 * (0.6 + 0.8 * F.Noise(mp0 * 14.0));   // the thin road water's grey-brown film
        float3 spk_c = lerp(float3(0.2, 0.19, 0.18), float3(0.3, 0.28, 0.26), F.Noise(mp0 * 90.0));
        float3 stn_c = lerp(float3(0.42, 0.4, 0.37), float3(0.3, 0.28, 0.26), saturate(rim * 2.5));
        float3 salt_c = float3(0.7, 0.7, 0.68);
        // laid one over another: salt, then the stains, then the specks on top
        float3 c = lerp(float3(0.4, 0.38, 0.35), salt_c, salt / max(salt + wash, 0.001)) * max(salt, wash);
        float a = max(salt, wash);
        c = c * (1.0 - stain) + stn_c * stain;
        a = a + stain * (1.0 - a);
        c = c * (1.0 - speck) + spk_c * speck;
        a = a + speck * (1.0 - a);
        float spray_a = min(saturate(a), 0.5);
        float3 spray_c = c / max(a, 0.001);
        alpha += spray_a * mudw;
        col += spray_c * spray_a * mudw;
        rough += lerp(0.55, 0.85, salt / max(a, 0.001)) * spray_a * mudw;
    }
    else if (mudw > 0.0)
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
    // A bright highlight that reads as the scan, never as more mud: the dirt itself stays as it is
    // and only glows (a touch of its colour where the dirt's too faint to glow by itself).
    col = lerp(col, sc, k * 0.25 * (1.0 - alpha));
    alpha = max(alpha, k * 0.25);
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
        ("Spin", vec("Spin", 1850, (0, 0, 0, 0)), "RGBA"),
        ("SprayF", vec("SprayF", 1950, (0, 0, 0, 0)), "RGBA"), ("SprayR", vec("SprayR", 2050, (0, 0, 0, 0)), "RGBA"),
    ]
    outs = []
    for mode in range(2):
        k = sp.expr(m, unreal.MaterialExpressionConstant, x + 300, y + 2200 + mode * 100, r=float(mode))
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
