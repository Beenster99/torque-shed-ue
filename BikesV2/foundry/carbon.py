"""Woven carbon fibre (beenster, 17 Nov: "the weave of carbon, not just a plain dark grey").

A 2x2 twill, the usual weave on bike parts: tows about 3 mm wide, each passing over two and under two, so the
over-crossings line up in diagonal steps. Each tow is rounded across its width and made of fine fibres along
it, so it catches the light in a band; warp and weft tows lie at right angles and so read light and dark
against each other. All under a thick clear coat.

    python3 foundry/carbon.py      # writes foundry/textures/carbon_twill_{col,rough,height,normal}.png (one tile, 24 mm)

carbon.material() gives the Blender material: the tile laid on by box projection in the part's own space, so
it needs no UVs. In the game the same tiles go on the cat_carbon material (the bikes thread's material library).
"""
import math
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TEX = os.path.join(HERE, "textures")
TILE_MM = 24.0          # 8 tows of 3 mm: the 2x2 twill repeats every 4 tows, so the tile holds two repeats
N = 1024


def make(n=N, tows=8, seed=3):
    rng = np.random.default_rng(seed)
    u = (np.arange(n) + 0.5) / n * tows
    X, Y = np.meshgrid(u, u)             # X along the warp (left to right), Y along the weft
    i, j = np.floor(X).astype(int), np.floor(Y).astype(int)
    fx, fy = X - i, Y - j
    warp_up = ((i - j) % 4) < 2          # 2 over, 2 under, stepping one tow each row: the twill's diagonal
    # across-tow position (0..1) and along-tow position for whichever tow lies on top
    across = np.where(warp_up, fy, fx)
    along = np.where(warp_up, X, Y)
    # a tow is rounded across its width, flattening where it dives under its neighbour at the ends of a float
    crown = np.sin(np.pi * np.clip(across, 0, 1)) ** 0.6
    # where along its float (two crossings long) the tow is: it dives under only at the two ends of the float
    i0 = i - ((i - j) % 4)
    j0 = j - ((j - i - 1) % 4)
    run = np.where(warp_up, (X - i0) / 2.0, (Y - j0) / 2.0)
    dive = np.clip(np.minimum(run, 1 - run) * 6.0, 0, 1) ** 0.5
    height = crown * (0.55 + 0.45 * dive)
    # fine fibres along each tow: thin streaks varying across its width only
    fib_n = 2048
    fib = rng.normal(0, 1, (tows, fib_n))
    fib = np.convolve(fib.ravel(), np.ones(3) / 3, "same").reshape(tows, fib_n)
    tow_idx = np.where(warp_up, j, i) % tows
    fpos = np.clip((across * fib_n).astype(int), 0, fib_n - 1)
    fibres = fib[tow_idx, fpos] * 0.5 + 0.5 * np.sin(along * 37.0 + tow_idx) * 0.15
    # colour: near-black resin, the warp tows a touch lighter than the weft, a soft sheen band on each crown
    base = np.where(warp_up, 0.020, 0.007)
    sheen = 0.06 * crown ** 3 * np.where(warp_up, 1.0, 0.4)
    col = np.clip(base + sheen + 0.010 * fibres, 0.004, 0.2)
    gaps = 1 - crown ** 0.25 * dive ** 0.3
    col = col * (1 - 0.6 * gaps)
    rough = np.clip(0.34 - 0.12 * crown + 0.05 * gaps + 0.02 * fibres, 0.18, 0.5)
    h = np.clip(height + 0.06 * fibres * crown, 0, 1)
    # a normal map from the height (tangent space, +y up as Blender reads it)
    k = 3.0
    gy, gx = np.gradient(h * k)
    nrm = np.dstack([-gx, gy, np.ones_like(h) * 0.5 * n / 1024 / tows * 8])
    nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)
    return col, rough, h, nrm


def write():
    from PIL import Image
    os.makedirs(TEX, exist_ok=True)
    col, rough, h, nrm = make()
    srgb = np.where(col <= 0.0031308, col * 12.92, 1.055 * col ** (1 / 2.4) - 0.055)
    Image.fromarray((srgb * 255).round().astype(np.uint8)).convert("RGB").save(os.path.join(TEX, "carbon_twill_col.png"))
    Image.fromarray((rough * 255).round().astype(np.uint8)).save(os.path.join(TEX, "carbon_twill_rough.png"))
    Image.fromarray((h * 255).round().astype(np.uint8)).save(os.path.join(TEX, "carbon_twill_height.png"))
    Image.fromarray(((nrm * 0.5 + 0.5) * 255).round().astype(np.uint8)).save(os.path.join(TEX, "carbon_twill_normal.png"))


def material(name="cf_twill", tile_mm=TILE_MM, tint=(1.0, 1.0, 1.0)):
    """The Blender material: the twill tile by box projection in object space, under a clear coat."""
    import bpy
    key = "d3_" + name
    if key in bpy.data.materials:
        return bpy.data.materials[key]
    if not os.path.exists(os.path.join(TEX, "carbon_twill_col.png")):
        write()
    m = bpy.data.materials.new(key)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    s = 1.0 / (tile_mm * 0.001)
    mp.inputs["Scale"].default_value = (s, s, s)
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])

    def img(fn, colour):
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = bpy.data.images.load(os.path.join(TEX, fn), check_existing=True)
        t.image.colorspace_settings.name = "sRGB" if colour else "Non-Color"
        t.projection = "BOX"
        t.projection_blend = 0.25
        nt.links.new(mp.outputs["Vector"], t.inputs["Vector"])
        return t
    c = img("carbon_twill_col.png", True)
    if tint != (1.0, 1.0, 1.0):
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type, mix.blend_type = "RGBA", "MULTIPLY"
        mix.inputs["Factor"].default_value = 1.0
        nt.links.new(c.outputs["Color"], mix.inputs[6])
        mix.inputs[7].default_value = (*tint, 1)
        nt.links.new(mix.outputs[2], b.inputs["Base Color"])
    else:
        nt.links.new(c.outputs["Color"], b.inputs["Base Color"])
    r = img("carbon_twill_rough.png", False)
    nt.links.new(r.outputs["Color"], b.inputs["Roughness"])
    h = img("carbon_twill_height.png", False)
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.5
    bump.inputs["Distance"].default_value = 0.0004
    nt.links.new(h.outputs["Color"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], b.inputs["Normal"])
    b.inputs["Metallic"].default_value = 0.0
    b.inputs["Coat Weight"].default_value = 1.0
    b.inputs["Coat Roughness"].default_value = 0.03
    b.inputs["Specular IOR Level"].default_value = 0.6
    return m


if __name__ == "__main__":
    write()
    print("written", TEX)
