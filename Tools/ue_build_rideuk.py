"""Build the level the real world's roads are ridden in (Ride UK stage 2): /Game/Maps/RideUK.

Run inside the Unreal editor, after Tools/ue_build_level.py has built the Buttertubs ride (it borrows
that script's sun, sky, air and camera, and the ride's cloud shadows as they are):

    UnrealEditor-Cmd.exe TorqueShedProto.uproject -ExecutePythonScript=Tools/ue_build_rideuk.py -unattended

The level has no ground of its own: only the sky, the lights and a player start. The game lays the
ground and the road out round the rider as they go (World/TorqueRideWorld), from the ride the world
subsystem planned, and draws them with the materials made here in /Game/RideUK/Materials: the plain
M_RideLand (moor grass, mottled) and M_RideRoad (the road's vertex colours: tarmac, verge, paint, and
hedges, roofs and stand-in trees), and Buttertubs' own scanned ground and stone made again under
names of their own (M_RideLandScan, M_RideStone, M_RideMasonry; the game uses them when they are here).
Its grass and trees are Buttertubs' own too: the plant kit (place_kit) copies the Buttertubs level's
species, so run this after ue_build_level.py has built that level.
"""
import json
import os
import sys

import unreal

MAP = "/Game/Maps/RideUK"
MAT_DIR = "/Game/RideUK/Materials"


def log(msg):
    unreal.log("[Torque] ride UK: " + msg)


def level_lib():
    """Tools/ue_build_level.py as a module, without it building the Buttertubs ride."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    sys.torque_build_level_as_library = True
    try:
        import importlib
        if "ue_build_level" in sys.modules:
            L = importlib.reload(sys.modules["ue_build_level"])
        else:
            import ue_build_level as L
    finally:
        sys.torque_build_level_as_library = False
    return L


def land_material(L):
    """The ground's own colour from its vertices (what covers it: World/WorldRide's LandColour), darker
    and paler in broad patches, mottled close up, matte."""
    mel = L.mel
    m = L.new_material("M_RideLand", MAT_DIR)
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -900, 0)
    patch = L.noise(m, 0.0006, 0.72, 1.25, -900, 400)
    mix = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -600, 100)
    mel.connect_material_expressions(vc, "", mix, "A")   # the unnamed output is RGB
    mel.connect_material_expressions(patch, "", mix, "B")
    mid = L.noise(m, 0.01, 0.8, 1.15, -900, 600)
    mul1 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -400, 200)
    mel.connect_material_expressions(mix, "", mul1, "A")
    mel.connect_material_expressions(mid, "", mul1, "B")
    fine = L.noise(m, 0.15, 0.85, 1.1, -900, 800)
    mul2 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 300)
    mel.connect_material_expressions(mul1, "", mul2, "A")
    mel.connect_material_expressions(fine, "", mul2, "B")
    mel.connect_material_property(mul2, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(L.const1(m, 0.95, -250, 500), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(L.const1(m, 0.2, -250, 650), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def road_material(L):
    """The road's own colours from its vertices (tarmac, the verge's gravel, the paint: World/WorldRide's
    RoadPiece), a little uneven in tone."""
    mel = L.mel
    m = L.new_material("M_RideRoad", MAT_DIR)
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -900, 0)
    n = L.noise(m, 0.05, 0.8, 1.2, -900, 200)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -650, 100)
    mel.connect_material_expressions(vc, "", mul, "A")   # the unnamed output is RGB
    mel.connect_material_expressions(n, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(L.const1(m, 0.8, -400, 300), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def road_material_scanned(L, tex, roles):
    """Build 141 (beenster: "the road surface more like the Buttertubs demo"): M_RideRoad in Buttertubs'
    scanned tarmac (M_Road: the scan with its brown taken out to the Dales' light, cool grey, slow patches
    of older and newer tarmac, darker and glossy when wet). The road mesh's vertex colour still says what
    each strip is (World/WorldRide's RoadPiece): only the tarmac's own dark grey takes the scan; the paint, verge, kerbs, bridge stone and tunnel portals keep their colours."""
    g = L.Graph(L.new_material("M_RideRoad", MAT_DIR))
    road = g.layer(tex["asphalt"], roles["asphalt"]["tile_m"])
    flat = g.node(unreal.MaterialExpressionDesaturation, -600)
    g.link(road[0], flat)
    g.link(g.const(0.9), flat, "Fraction")
    road[0] = g.op(unreal.MaterialExpressionMultiply, flat, g.const((0.96, 0.99, 1.06)))
    road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], L.noise(g.m, 0.002, 0.6, 0.95, -1200, g.y + 200))
    g.y += 140
    # Patches of a fresher, darker surface dressing and paler, older tarmac, tens of metres long.
    patch = g.mask_noise(0.0012, -2.0, 3.0)
    road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], g.lerp(g.const(1.08), g.const(0.8), patch))
    tar_scan = list(road)   # (the tarmac's own scan: the verge's inner end, build 149)
    # Which strips are tarmac: the vertex colour within a hair of the tarmac's own (0.022, 0.022, 0.025),
    # so nothing else drawn in this material (dark glass, black paint) takes the scan.
    vc = g.node(unreal.MaterialExpressionVertexColor)
    d = g.node(unreal.MaterialExpressionDistance, -600)
    g.link(vc, d, "A")
    g.link(g.const((0.022, 0.022, 0.025)), d, "B")
    tar = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
               g.op(unreal.MaterialExpressionSubtract, g.const(0.012), d), const_b=166.0))   # 1 within 0.006 .. 0 by 0.012 (8-bit vertex colours)
    own = [g.op(unreal.MaterialExpressionMultiply, vc, L.noise(g.m, 0.05, 0.8, 1.2, -1200, g.y + 200)),
           g.const((0.0, 0.0, 1.0)), g.const(0.8)]
    g.y += 140
    # Pavements and kerbs (build 148; beenster on 147 in Whaley Bridge: the pavements "too low definition",
    # a soft blotchy colour). A pavement's vertex colour (0.12, 0.118, 0.11, World/WorldRide's Paving) takes
    # the tarmac's scan paler and greyer, a footway's asphalt; a kerb's (0.16, 0.16, 0.15) the same scan
    # paler again and almost grey, as worn concrete kerbs, so both are as sharp as the road.
    def keyed(rgb):
        dk = g.node(unreal.MaterialExpressionDistance, -600)
        g.link(vc, dk, "A")
        g.link(g.const(rgb), dk, "B")
        return g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                    g.op(unreal.MaterialExpressionSubtract, g.const(0.012), dk), const_b=166.0))
    for key, bright, grey in (((0.12, 0.118, 0.11), 1.45, 0.6), ((0.16, 0.16, 0.15), 2.3, 0.85)):
        dull = g.node(unreal.MaterialExpressionDesaturation, -600)
        g.link(road[0], dull)
        g.link(g.const(grey), dull, "Fraction")
        paler = [g.op(unreal.MaterialExpressionMultiply, dull, g.const(bright)), road[1], road[2]]
        if key == (0.12, 0.118, 0.11):
            # [build 154] Paving slabs (beenster: "I haven't yet seen anything that looks like paving slabs"):
            # most streets' pavements (by broad patches, a street or two each) laid in pale concrete slabs,
            # 600 mm across by 900 mm along, every other row half a slab on, each slab its own shade, dark
            # 8 mm joints; the rest stay asphalt footways. UV0 is the road's metres across and along over 4
            # (World/WorldRide's RoadPiece Put, kept small for UE's half-precision UVs).
            fu, fv, var = _coursed(g, L, 0.6 / 4.0, 0.9 / 4.0)
            sat = lambda e: g.op(unreal.MaterialExpressionSaturate, e)
            one_minus = lambda e: g.op(unreal.MaterialExpressionOneMinus, e)
            # ([build 155] beenster on 154: "was paving slabs in the last build? I didn't see any": 8 mm joints
            # vanished at a few metres. Now each joint is a dark 12 mm line in a 4 cm shaded edge (the slabs'
            # worn arrises and the dirt in them), and the slabs differ more, so the grid reads from the bike.)
            def edge(f, w):
                return g.op(unreal.MaterialExpressionMultiply, sat(g.op(unreal.MaterialExpressionDivide, f, const_b=w)),
                            sat(g.op(unreal.MaterialExpressionDivide, one_minus(f), const_b=w)))
            slab = g.op(unreal.MaterialExpressionMultiply, edge(fu, 0.012 / 0.6), edge(fv, 0.012 / 0.9))
            arris = g.op(unreal.MaterialExpressionMultiply, edge(fu, 0.04 / 0.6), edge(fv, 0.04 / 0.9))
            concrete = g.node(unreal.MaterialExpressionDesaturation, -600)
            g.link(road[0], concrete)
            g.link(g.const(0.92), concrete, "Fraction")
            face = g.op(unreal.MaterialExpressionMultiply, concrete, g.const((1.6, 1.58, 1.53)))
            if var is not None:
                face = g.op(unreal.MaterialExpressionMultiply, face,
                            g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, var, const_b=0.45, a_out="R"), const_b=0.76))
            face = g.op(unreal.MaterialExpressionMultiply, face, g.lerp(g.const(0.72), g.const(1.0), arris))
            slabs = [g.lerp(g.op(unreal.MaterialExpressionMultiply, face, const_b=0.25), face, slab), road[1],
                     g.lerp(g.const(0.95), g.const(0.75), slab)]
            slabbed = g.mask_noise(0.00011, -0.5, 3.5)   # (about three streets in four)
            paler = g.mix(paler, slabs, slabbed)
        own = g.mix(own, paler, keyed(key))
    # [towns stage 6] Setts and cobbles (World/WorldRide: a road the map says is "sett" or "cobblestone", its
    # vertex colour the key (0.09, 0.095, 0.11)): grey granite setts 220 mm across the road by 120 mm along it,
    # laid in courses, every other course half a sett on; each sett its own shade, its top domed (a normal
    # tilted towards its edges) and worn smooth, dark gritty joints 15 mm wide between.
    fu, fv, var = _coursed(g, L, 0.22 / 4.0, 0.12 / 4.0)
    sat = lambda e: g.op(unreal.MaterialExpressionSaturate, e)
    one_minus = lambda e: g.op(unreal.MaterialExpressionOneMinus, e)
    def sedge(f, w):
        return g.op(unreal.MaterialExpressionMultiply, sat(g.op(unreal.MaterialExpressionDivide, f, const_b=w)),
                    sat(g.op(unreal.MaterialExpressionDivide, one_minus(f), const_b=w)))
    sett = g.op(unreal.MaterialExpressionMultiply, sedge(fu, 0.015 / 0.22), sedge(fv, 0.015 / 0.12))
    dome = g.op(unreal.MaterialExpressionMultiply, sedge(fu, 0.3), sedge(fv, 0.3))
    granite = g.node(unreal.MaterialExpressionDesaturation, -600)
    g.link(road[0], granite)
    g.link(g.const(0.85), granite, "Fraction")
    sface = g.op(unreal.MaterialExpressionMultiply, granite, g.const((1.25, 1.27, 1.33)))
    if var is not None:
        sface = g.op(unreal.MaterialExpressionMultiply, sface,
                     g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, var, const_b=0.6, a_out="R"), const_b=0.7))
    sface = g.op(unreal.MaterialExpressionMultiply, sface, g.lerp(g.const(0.75), g.const(1.05), dome))
    tilt_u = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, fu, const_b=0.5), const_b=0.9)
    tilt_v = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, fv, const_b=0.5), const_b=0.9)
    snrm = g.node(unreal.MaterialExpressionNormalize, -600)   # (its input the unnamed pin)
    g.link(g.op(unreal.MaterialExpressionAppendVector, g.op(unreal.MaterialExpressionAppendVector, tilt_u, tilt_v), g.const(1.0)), snrm, "")
    setts = [g.lerp(g.op(unreal.MaterialExpressionMultiply, sface, const_b=0.2), sface, sett),
             g.lerp(g.const((0.0, 0.0, 1.0)), snrm, sett),
             g.lerp(g.const(0.95), g.const(0.55), sett)]
    own = g.mix(own, setts, keyed((0.09, 0.095, 0.11)))
    # Gravel tracks (beenster, Nov 13: the fade from tarmac was "too low res"; use proper gravel scans). The
    # mesh colours a gravel road in its grit (0.095, 0.088, 0.07) and fades the tarmac's colour into it over
    # its first metres. How far along that line a pixel's colour lies (Along, 0 tarmac .. 1 grit) and how near
    # the line it is (Near) pick the surface; where the two meet the gravel scan's own bright stones break
    # through the tarmac first, by their height, so the edge is as fine as the scans.
    # [build 154] beenster, shown the cloud preview of Nov 13 (rideuk/stage4_previews/gravel_fade_preview.png):
    # "use this when there is gravel on the road". That preview is the ambientCG scan (the "gravel" role,
    # always imported) graded as below, so the road uses exactly it, not the Megascans one.
    gravel_key = "gravel" if "gravel" in tex else ("track_gravel" if "track_gravel" in tex else None)
    if gravel_key:
        tar_c, grit_c = (0.022, 0.022, 0.025), (0.095, 0.088, 0.07)
        span = tuple(b - a for a, b in zip(tar_c, grit_c))
        span2 = sum(v * v for v in span)
        rel = g.op(unreal.MaterialExpressionSubtract, vc, g.const(tar_c))
        dot = g.node(unreal.MaterialExpressionDotProduct, -600)
        g.link(rel, dot, "A")
        g.link(g.const(span), dot, "B")
        along = g.op(unreal.MaterialExpressionMultiply, dot, const_b=1.0 / span2)
        on_line = g.op(unreal.MaterialExpressionAdd, g.const(tar_c), g.op(unreal.MaterialExpressionMultiply, g.const(span), along))
        off = g.node(unreal.MaterialExpressionDistance, -600)
        g.link(vc, off, "A")
        g.link(on_line, off, "B")
        near = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                    g.op(unreal.MaterialExpressionSubtract, g.const(0.012), off), const_b=166.0))
        # (and only between the two ends, a little past each for 8-bit rounding)
        ends = g.op(unreal.MaterialExpressionMultiply,
                    g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionAdd, along, const_b=0.15), const_b=8.0)),
                    g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, g.const(1.15), along), const_b=8.0)))
        near = g.op(unreal.MaterialExpressionMultiply, near, ends)
        grav = g.layer(tex[gravel_key], 1.6)
        # (the preview's grade: two thirds of the scan's colour kept, lifted 1.5 times, a touch warm; its
        # middle brightness 0.15, measured on the scan so graded. The Megascans fallback keeps build 148's.)
        megascan = "ue" in roles[gravel_key]
        tint, middle, keep = ((0.78, 0.78, 0.79), 0.22, 0.15) if megascan else ((1.5, 1.48, 1.4), 0.166, 0.65)   # (0.166: as Unreal samples it)
        gdull = g.node(unreal.MaterialExpressionDesaturation, -600)
        g.link(grav[0], gdull)
        g.link(g.const(1.0 - keep), gdull, "Fraction")
        grav[0] = g.op(unreal.MaterialExpressionMultiply, gdull, g.const(tint))
        # Its stones' height from the scan's brightness (with its AO in it), and a slow wobble so the edge
        # isn't a line across the road.
        lum = g.node(unreal.MaterialExpressionDesaturation, -600)
        g.link(grav[0], lum)
        g.link(g.const(1.0), lum, "Fraction")
        lum1 = g.node(unreal.MaterialExpressionComponentMask, -600, r=True, g=False, b=False, a=False)
        g.link(lum, lum1)
        height = g.op(unreal.MaterialExpressionSubtract, g.op(unreal.MaterialExpressionMultiply, lum1, const_b=1.0 / middle), const_b=1.0)   # (middle: the scan's middle brightness)
        wobble = g.op(unreal.MaterialExpressionSubtract, L.noise(g.m, 0.01, 0.0, 1.0, -1200, g.y + 200), const_b=0.5)
        g.y += 140
        edge = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionSubtract, along, const_b=0.5),
                    g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, height, const_b=0.54 if megascan else 0.9 * 0.6),
                         g.op(unreal.MaterialExpressionMultiply, wobble, const_b=0.25)))
        stones = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply, edge, const_b=25.0))
        # ([build 154] beenster: "it looks like you have blended gravel into everything; I only want it on the
        # middle of lanes". The scan's brightest stones stood out of the tarmac itself (Along 0) and of the
        # tarmac's blend into the verge (which crosses this line about a third of the way). Now only colours
        # well along to the grit carry stones: the lanes' middle grit (0.75), gravel roads and their tongues.)
        gate = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                    g.op(unreal.MaterialExpressionSubtract, along, const_b=0.5), const_b=10.0))
        stones = g.op(unreal.MaterialExpressionMultiply, stones, gate)
        surface = g.mix(road, grav, stones)
        road = g.mix(own, surface, near)
        hard = g.op(unreal.MaterialExpressionMultiply, near, g.op(unreal.MaterialExpressionOneMinus, stones))   # (the tarmac itself, for the wet)
    else:
        road = g.mix(own, road, tar)
        hard = tar
        near = tar
    # The verge (build 147; beenster on 146: "remove these green road borders and blend the road into the
    # side"): the shoulder's colour runs from (0.072, 0.078, 0.045) at its inner end to the verge's grass
    # (0.10, 0.16, 0.03) out on the shoulder (World/WorldRide's VergeGrass). Along that line, the land's own
    # grass scan, graded as M_RideLandScan grades it, breaks through in a ragged edge by a fine wobble; the
    # outer shoulder is all grass, so it meets the land unseen. Build 149 (beenster on 148: "have the
    # roadside butt up against the grass and have it merge over", no pale border): the inner end is the
    # tarmac's own scan, not grit, and the band starts a little in on the tarmac, so the grass grows over
    # the road's edge.
    if "grass" in tex:
        grit_c, lawn_c = (0.072, 0.078, 0.045), (0.10, 0.16, 0.03)
        vspan = tuple(b - a for a, b in zip(grit_c, lawn_c))
        vspan2 = sum(v * v for v in vspan)
        vrel = g.op(unreal.MaterialExpressionSubtract, vc, g.const(grit_c))
        vdot = g.node(unreal.MaterialExpressionDotProduct, -600)
        g.link(vrel, vdot, "A")
        g.link(g.const(vspan), vdot, "B")
        valong = g.op(unreal.MaterialExpressionMultiply, vdot, const_b=1.0 / vspan2)
        von = g.op(unreal.MaterialExpressionAdd, g.const(grit_c), g.op(unreal.MaterialExpressionMultiply, g.const(vspan), valong))
        voff = g.node(unreal.MaterialExpressionDistance, -600)
        g.link(vc, voff, "A")
        g.link(von, voff, "B")
        vnear = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                     g.op(unreal.MaterialExpressionSubtract, g.const(0.014), voff), const_b=140.0))
        vends = g.op(unreal.MaterialExpressionMultiply,
                     g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionAdd, valong, const_b=0.15), const_b=8.0)),
                     g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, g.const(1.15), valong), const_b=8.0)))
        vnear = g.op(unreal.MaterialExpressionMultiply, vnear, vends)
        # ([build 154] beenster: "remove the edge we currently have": the verge's grass is now graded exactly
        # as M_RideLandScan grades the ground beside it, its moor-grass patches, livelier greens and slow
        # brightness by the same world-position noises, so where the road's mesh ends and the land's begins
        # nothing shows; its odd peat hollow left out, to keep the road's pixels cheap)
        lawn = g.layer(tex["grass"], roles["grass"]["tile_m"])
        depth0 = g.node(unreal.MaterialExpressionPixelDepth)
        calm = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionDivide, g.op(unreal.MaterialExpressionSubtract, depth0, const_b=8000.0), const_b=30000.0))
        if "moor" in tex:
            lawn = g.mix(lawn, g.layer(tex["moor"], roles["moor"]["tile_m"]), g.lerp(g.mask_noise(0.00028, -0.6, 1.6), g.const(0.35), calm))
        lawn[0] = g.op(unreal.MaterialExpressionMultiply, lawn[0], const_b=0.75)
        lgrey = g.node(unreal.MaterialExpressionDesaturation, -600)
        g.link(lawn[0], lgrey)
        g.link(g.const(1.0), lgrey, "Fraction")
        lawn[0] = g.lerp(lgrey, lawn[0], g.const(1.6))
        lawn[0] = g.op(unreal.MaterialExpressionMultiply, lawn[0], L.noise(g.m, 0.0003, 0.9, 1.07, -1200, g.y + 200))
        g.y += 140
        # (the inner end: the tarmac's own scan, so the road runs on under the grass)
        grit = tar_scan
        wob = g.op(unreal.MaterialExpressionAdd, L.noise(g.m, 0.02, -0.5, 0.5, -1200, g.y + 200),
                   g.op(unreal.MaterialExpressionMultiply, L.noise(g.m, 0.004, -0.5, 0.5, -1200, g.y + 400), const_b=1.5))
        g.y += 140
        # (grass from about a third of the way out, at the tarmac's edge: [build 154] beenster wants it to blend
        # as the gravel does: as the gravel's stones break through the tarmac, the grass comes over it in tufts,
        # ragged at three sizes (half a metre, ten centimetres, a few centimetres: its tufts and blades), a
        # sharp line at each, never a straight one; tufts reach a hand's breadth or two onto the road)
        fine = g.op(unreal.MaterialExpressionAdd, L.noise(g.m, 0.09, -0.5, 0.5, -1200, g.y + 200),
                    g.op(unreal.MaterialExpressionMultiply, L.noise(g.m, 0.3, -0.5, 0.5, -1200, g.y + 400), const_b=0.6))
        g.y += 140
        reach = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, wob, const_b=0.45),
                     g.op(unreal.MaterialExpressionMultiply, fine, const_b=0.55))
        lawny = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                     g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionSubtract, valong, const_b=0.32), reach),
                     const_b=5.0))
        # (where the grass thins out over the tarmac, a little earth in it: the tarmac browned by the half-way)
        thin = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, lawny, g.op(unreal.MaterialExpressionOneMinus, lawny)), const_b=4.0)
        grit = [g.lerp(grit[0], g.op(unreal.MaterialExpressionMultiply, grit[0], g.const((0.95, 0.9, 0.75))), thin), grit[1], grit[2]]
        verge = g.mix(grit, lawn, lawny)
        road = g.mix(road, verge, vnear)
    # Wet: the tarmac dark and glossy (as Buttertubs' road), the gravel darker but still rough; dry keeps a
    # floor so there's no white sheen.
    wet = L.wetness(g)
    road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], g.lerp(g.const(1.0), g.lerp(g.const(0.75), g.lerp(g.const(0.6), g.const(0.5), hard), near), wet))
    road[2] = g.op(unreal.MaterialExpressionMax, road[2], g.lerp(g.const(0.6), g.lerp(g.const(0.6), g.const(0.12), hard), wet))
    return g.finish(road, specular=0.4)


def lamp_material(L):
    """A tunnel lamp's glowing face (World/WorldRide's RoadPiece: RideMesh::Glow, its vertex colour the
    light's): unlit, bright enough to read as a lamp under Ride UK's daytime exposure and at night."""
    mel = L.mel
    m = L.new_material("M_RideLamp", MAT_DIR)
    L.try_set(m, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -600, 0)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -350, 0)
    mel.connect_material_expressions(vc, "", mul, "A")   # the unnamed output is RGB
    mel.connect_material_expressions(L.const1(m, 150.0, -600, 200), "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def night_material(L):
    """What lights up after dark (Ride UK stage 7: RideMesh::Night): lit windows and street lamps' heads
    (vertex alpha 0) and cat's eyes (alpha 1), their vertex colour the light's. By day dark glass, or a
    pale stud; as MPC_Weather's Night rises they glow, the cat's eyes only near by (they shine back the
    headlight, which reaches a couple of hundred metres: lit only in the beam, never glowing by themselves)."""
    g = L.Graph(L.new_material("M_RideNight", MAT_DIR))
    vc = g.node(unreal.MaterialExpressionVertexColor)
    alpha = g.op(unreal.MaterialExpressionMultiply, vc, g.const(1.0), a_out="A")   # (the vertex colour's A output)
    rgb = vc                                                                        # (its unnamed output is RGB)
    # (by day: glass 4% of its colour, a stud half)
    base = g.op(unreal.MaterialExpressionMultiply, rgb, g.lerp(g.const(0.04), g.const(0.5), alpha))
    night = L.weather_param(g, "Night")
    # (a cat's eye or a red edge stud is a retroreflector, not a light: it shines only back along the
    # headlight's beam, which the camera rides just behind. So it is lit only in the beam, the middle
    # of the view a little below the horizon, brightest near and gone by about 120 m, and dark otherwise
    # (beenster, build 141: "should only reflect back our light, not glow in the dark"). A lit window
    # or lamp keeps its glow.)
    depth = g.node(unreal.MaterialExpressionPixelDepth)
    near = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionSubtract, g.const(1.2), g.op(unreal.MaterialExpressionDivide, depth, g.const(10000.0))))
    near = g.op(unreal.MaterialExpressionMultiply, near, near)
    beam = g.const(1.0)
    try:
        sp = g.node(unreal.MaterialExpressionScreenPosition)
        c2 = lambda a, b: g.node(unreal.MaterialExpressionConstant2Vector, r=a, g=b)   # (two numbers: ViewportUV is two)
        off = g.op(unreal.MaterialExpressionSubtract, sp, c2(0.5, 0.6), a_out="ViewportUV")
        off = g.op(unreal.MaterialExpressionMultiply, off, c2(1.5, 2.6))
        dist = g.op(unreal.MaterialExpressionDotProduct, off, off)
        beam = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionSubtract, g.const(1.0), dist))
    except Exception as e:
        unreal.log_warning("[Torque] ride UK: cat's eyes beam not made (%s); they fade by distance only" % e)
    fade = g.lerp(g.const(1.0), g.op(unreal.MaterialExpressionMultiply, near, beam), alpha)
    glow = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, rgb, night), g.op(unreal.MaterialExpressionMultiply, fade, g.const(60.0)))
    L.mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    L.mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    L.mel.connect_material_property(g.const(0.25), "", unreal.MaterialProperty.MP_ROUGHNESS)
    L.mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    return g.m


def signal_material(L):
    """Traffic lights' and Belisha beacons' lamps (OSM stage 5D: RideMesh::Signals). Their vertex colour is
    the lamp's, UV0 u the second its light's 60 s cycle starts at, v which lamp (0 red, 1 amber, 2 green,
    3 a beacon). From MPC_Weather's Signals (the server's clock, so every rider sees the same): red for
    the first 55% (with amber over its last 5%: red and amber), green to 95%, then amber alone; a
    beacon flashes, 0.6 s on and off. A lamp that's off is dark glass."""
    g = L.Graph(L.new_material("M_RideSignal", MAT_DIR))
    vc = g.node(unreal.MaterialExpressionVertexColor)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    def chan(src, c):
        e = g.node(unreal.MaterialExpressionComponentMask, r=c == "R", g=c == "G", b=False, a=False)
        g.link(src, e)
        return e
    start, lamp = chan(uv, "R"), chan(uv, "G")
    clock = L.weather_param(g, "Signals")
    t = g.op(unreal.MaterialExpressionFrac, g.op(unreal.MaterialExpressionDivide, g.op(unreal.MaterialExpressionAdd, clock, start), g.const(60.0)))
    # (a sharp step: 0 before the edge, 1 after)
    step = lambda x, edge: g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, x, g.const(edge)), g.const(1000.0)))
    mul = lambda a, b: g.op(unreal.MaterialExpressionMultiply, a, b)
    add = lambda a, b: g.op(unreal.MaterialExpressionAdd, a, b)
    off = lambda a: g.op(unreal.MaterialExpressionOneMinus, a)
    red = off(step(t, 0.55))
    amber = add(mul(step(t, 0.5), off(step(t, 0.55))), step(t, 0.95))
    green = mul(step(t, 0.55), off(step(t, 0.95)))
    flash = off(step(g.op(unreal.MaterialExpressionFrac, mul(clock, g.const(0.8))), 0.5))
    # (which lamp this is: 1 for the lamp code k, 0 for the others)
    pick = lambda k: g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionOneMinus, mul(g.op(unreal.MaterialExpressionAbs, g.op(unreal.MaterialExpressionSubtract, lamp, g.const(float(k)))), g.const(2.0))))
    on = add(add(mul(red, pick(0)), mul(amber, pick(1))), add(mul(green, pick(2)), mul(flash, pick(3))))
    glow = mul(vc, add(g.const(0.02), mul(on, g.const(40.0))))
    L.mel.connect_material_property(mul(vc, g.const(0.08)), "", unreal.MaterialProperty.MP_BASE_COLOR)
    L.mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    L.mel.connect_material_property(g.const(0.15), "", unreal.MaterialProperty.MP_ROUGHNESS)
    L.mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    return g.m


def sign_material(L):
    """Road signs' faces and lettering (OSM stage 6A: RideMesh::Signs), their vertex colour the paint's.
    By day plain paint, a little glossy. After dark (MPC_Weather's Night) they shine back the headlight as
    real signs' reflective sheeting does: lit only in the beam (the middle of the view, a little below the
    horizon, as the cat's eyes are), brightest near, gone by about 250 m; never glowing by themselves."""
    g = L.Graph(L.new_material("M_RideSign", MAT_DIR))
    vc = g.node(unreal.MaterialExpressionVertexColor)
    night = L.weather_param(g, "Night")
    depth = g.node(unreal.MaterialExpressionPixelDepth)
    near = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionSubtract, g.const(1.2), g.op(unreal.MaterialExpressionDivide, depth, g.const(25000.0))))
    near = g.op(unreal.MaterialExpressionMultiply, near, near)
    beam = g.const(1.0)
    try:
        sp = g.node(unreal.MaterialExpressionScreenPosition)
        c2 = lambda a, b: g.node(unreal.MaterialExpressionConstant2Vector, r=a, g=b)
        off = g.op(unreal.MaterialExpressionSubtract, sp, c2(0.5, 0.55), a_out="ViewportUV")
        off = g.op(unreal.MaterialExpressionMultiply, off, c2(1.2, 1.8))
        dist = g.op(unreal.MaterialExpressionDotProduct, off, off)
        beam = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionSubtract, g.const(1.0), dist))
    except Exception as e:
        unreal.log_warning("[Torque] ride UK: signs' beam not made (%s); they fade by distance only" % e)
    shine = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, night, near), g.op(unreal.MaterialExpressionMultiply, beam, g.const(6.0)))
    glow = g.op(unreal.MaterialExpressionMultiply, vc, shine)
    L.mel.connect_material_property(vc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    L.mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    L.mel.connect_material_property(g.const(0.45), "", unreal.MaterialProperty.MP_ROUGHNESS)
    L.mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    return g.m


def water_normal(L):
    """[build 154] Data/Textures/Water/normal.png (Tools/make_water_normal.py), a tiling normal map of wind
    ripples 8 m across, as /Game/RideUK/Textures/T_WaterNormal; None if it isn't there."""
    f = os.path.join(os.path.dirname(L.data_dir()), "Textures", "Water", "normal.png")
    if not os.path.exists(f):
        return None
    dest = "/Game/RideUK/Textures"
    t = unreal.AssetImportTask()
    for k, v in (("filename", f), ("destination_path", dest), ("destination_name", "T_WaterNormal"),
                 ("automated", True), ("replace_existing", True), ("save", False)):
        t.set_editor_property(k, v)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    tex = unreal.load_asset(dest + "/T_WaterNormal")
    if tex:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


def water_material(L):
    """The ride's water (Ride UK stage 5: World/WorldRideScenery's WaterTile): the sea, lakes and
    rivers, their colour the vertex colour (dark: deep sea blue-grey, peaty lake, brown river), glossy,
    with ripples drifting across it (UV0 is the world's metres over 8, whole tens of units from one square
    to the next, so every sample's tiling times 10 is a whole number). Opaque: open water at a distance
    shows the sky in it, not its bed.
    [build 154] beenster: "can we move to a proper water animation, as it does look like a lot of static
    tiles for the surface". Now the wave normal map (water_normal) three times over: long wind waves (27 m
    across) and two sizes of ripple on them, each drifting its own way at its own pace, so the surface
    moves all the time and no pattern repeats; the ripples fade with distance (where they would only
    shimmer) and are stronger and weaker in broad patches, as wind gusts make them."""
    g = L.Graph(L.new_material("M_RideWater"))
    vc = g.node(unreal.MaterialExpressionVertexColor)
    wn = water_normal(L)
    nt = L.noise_texture()
    if wn is not None:
        # [build 155] beenster: "Major tiling on the sea (pic)". The three samples were all square to the
        # world's axes and at tilings whose repeats all met every 80 m, and the long swell never faded,
        # so from a headland the sea was a grid of wave stripes to the horizon. Now each sample is turned
        # its own way (by 3-4-5, 5-12-13 and 20-21-29 triangles, so a square's 10 units still land on
        # whole repeats and the squares meet), the swell's coordinates are pushed about by the slow noise
        # so its rows wander, and every layer, the swell too, calms with distance, the far sea a little
        # rougher instead, which is how open water reads from a cliff. Same three texture reads.
        def turned(tiling, ca, sa):
            uv = g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=tiling, v_tiling=tiling)
            u = g.node(unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False); g.link(uv, u)
            v = g.node(unreal.MaterialExpressionComponentMask, r=False, g=True, b=False, a=False); g.link(uv, v)
            mul = lambda e, k: g.op(unreal.MaterialExpressionMultiply, e, const_b=float(k))
            ru = g.op(unreal.MaterialExpressionSubtract, mul(u, ca), mul(v, sa))
            rv = g.op(unreal.MaterialExpressionAdd, mul(u, sa), mul(v, ca))
            return g.op(unreal.MaterialExpressionAppendVector, ru, rv)

        def ripple(coord, sx, sy, strength):
            p = g.node(unreal.MaterialExpressionPanner, speed_x=sx, speed_y=sy)
            g.link(coord, p, "Coordinate")
            n = g.sample(wn, p, "normal")
            return g.op(unreal.MaterialExpressionMultiply, n, g.const((strength, strength, 0.0)))
        swell_uv = turned(0.5, 0.8, 0.6)            # 16 m, turned 37 degrees
        if nt is not None:
            # (the swell's rows bent by the slow noise, 80 m across, by up to half a repeat)
            wuv = g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=0.1, v_tiling=0.1)
            warp = g.sample(nt, wuv, "linear")
            wxy = g.node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=False, a=False); g.link(warp, wxy)
            swell_uv = g.op(unreal.MaterialExpressionAdd, swell_uv, g.op(unreal.MaterialExpressionMultiply, wxy, const_b=0.55))
        big = ripple(swell_uv, 0.010, 0.006, 0.7)                     # the swell of the wind waves
        mid = ripple(turned(1.3, 12.0 / 13.0, -5.0 / 13.0), -0.021, 0.034, 0.6)   # 6 m, turned -23 degrees
        small = ripple(turned(2.9, 20.0 / 29.0, 21.0 / 29.0), 0.055, -0.04, 0.5)  # 2.8 m, turned 46 degrees
        depth = g.node(unreal.MaterialExpressionPixelDepth)
        near = g.op(unreal.MaterialExpressionOneMinus, g.op(unreal.MaterialExpressionSaturate,
                    g.op(unreal.MaterialExpressionDivide, g.op(unreal.MaterialExpressionSubtract, depth, const_b=3000.0), const_b=25000.0)))
        # (the swell fades later and less: 60 m to 600 m, down to a third)
        swell_near = g.op(unreal.MaterialExpressionOneMinus, g.op(unreal.MaterialExpressionSaturate,
                    g.op(unreal.MaterialExpressionDivide, g.op(unreal.MaterialExpressionSubtract, depth, const_b=6000.0), const_b=54000.0)))
        big = g.op(unreal.MaterialExpressionMultiply, big, g.lerp(g.const(0.33), g.const(1.0), swell_near))
        detail = g.op(unreal.MaterialExpressionAdd, mid, small)
        if nt is not None:
            # (gusts: the ripples stronger and calmer in broad slow patches)
            guv = g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=0.1, v_tiling=0.1)
            gp = g.node(unreal.MaterialExpressionPanner, speed_x=0.004, speed_y=0.003)
            g.link(guv, gp, "Coordinate")
            gust = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, g.sample(nt, gp, "linear"), const_b=1.2, a_out="G"), const_b=0.4)
            detail = g.op(unreal.MaterialExpressionMultiply, detail, gust)
        detail = g.op(unreal.MaterialExpressionMultiply, detail, g.lerp(g.const(0.15), g.const(1.0), near))
        n = g.op(unreal.MaterialExpressionAdd, big, detail)
        normal = g.node(unreal.MaterialExpressionNormalize)
        g.link(g.op(unreal.MaterialExpressionAdd, n, g.const((0.0, 0.0, 1.0))), normal)
        rough = g.lerp(g.const(0.11), g.const(0.04), swell_near)
    elif nt is not None:
        uv = g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=0.9, v_tiling=0.9)
        p1 = g.node(unreal.MaterialExpressionPanner, speed_x=0.012, speed_y=0.02); g.link(uv, p1, "Coordinate")
        uv2 = g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=2.3, v_tiling=2.3)
        p2 = g.node(unreal.MaterialExpressionPanner, speed_x=-0.025, speed_y=0.01); g.link(uv2, p2, "Coordinate")
        n = g.op(unreal.MaterialExpressionAdd, g.sample(nt, p1, "linear"), g.sample(nt, p2, "linear"))
        n = g.op(unreal.MaterialExpressionSubtract, n, const_b=1.0)
        n = g.op(unreal.MaterialExpressionMultiply, n, g.const((0.35, 0.35, 0.0)))
        normal = g.op(unreal.MaterialExpressionAdd, n, g.const((0.0, 0.0, 1.0)))
    else:
        normal = g.const((0.0, 0.0, 1.0))
    if wn is None:
        rough = g.const(0.04)
    base = g.op(unreal.MaterialExpressionMultiply, vc, g.const(1.0))
    mel = L.mel
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(g.const(0.5), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
    mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    return g.m


def _coursed(g, L, u_m, v_m, stagger=0.5):
    """Running courses on UV0 (metres): units u_m along, v_m up, every other course offset by
    `stagger` of a unit. Returns (along-unit fraction, up-course fraction, the unit's own 0..1
    variation from the noise texture or None)."""
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    u = g.node(unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False); g.link(uv, u)
    v = g.node(unreal.MaterialExpressionComponentMask, r=False, g=True, b=False, a=False); g.link(uv, v)
    div = lambda a, b: g.op(unreal.MaterialExpressionDivide, a, const_b=float(b)) if isinstance(b, (int, float)) else g.op(unreal.MaterialExpressionDivide, a, b)
    rows = div(v, v_m)
    row = g.op(unreal.MaterialExpressionFloor, rows)
    fv = g.op(unreal.MaterialExpressionFrac, rows)
    units = g.op(unreal.MaterialExpressionAdd, div(u, u_m),
                 g.op(unreal.MaterialExpressionMultiply, row, const_b=stagger))
    fu = g.op(unreal.MaterialExpressionFrac, units)
    var = None
    nt = L.noise_texture()
    if nt is not None:
        # (one value per unit: the noise read at a point fixed by the unit's column and course)
        cell = g.op(unreal.MaterialExpressionAppendVector,
                    g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionFloor, units), const_b=0.1731),
                    g.op(unreal.MaterialExpressionMultiply, row, const_b=0.2917))
        var = g.sample(nt, cell, "linear")
    return fu, fv, var


def roof_material(L):
    """[OSM stage 3] The streamed world's roofs (World/WorldRideBuildings: RideMesh::Roofing), drawn
    without any texture: the vertex colour the roof's colour; the vertex alpha the roofing, 0 slate,
    0.25 stone slates, 0.5 clay tiles (courses of those three, each slate or tile a little different,
    a shadow under each course's edge), 0.75 thatch (streaks down the slope), 1 sheet or felt
    (corrugations). UV0 is metres along the eaves and up the slope."""
    g = L.Graph(L.new_material("M_RideRoof"))
    vc = g.node(unreal.MaterialExpressionVertexColor)
    k = g.op(unreal.MaterialExpressionMultiply, vc, const_b=1.0, a_out="A")
    sat = lambda e: g.op(unreal.MaterialExpressionSaturate, e)
    # (course height: slate 0.23 m, stone slates 0.36, tiles 0.2; units 1.3 times as wide)
    to_stone = sat(g.op(unreal.MaterialExpressionMultiply, k, const_b=4.0))
    to_tile = sat(g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, k, const_b=0.25), const_b=4.0))
    h = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, to_stone, const_b=0.13),
             g.op(unreal.MaterialExpressionMultiply, to_tile, const_b=-0.16))
    h = g.op(unreal.MaterialExpressionAdd, h, const_b=0.23)
    w = g.op(unreal.MaterialExpressionMultiply, h, const_b=1.3)
    fu, fv, var = _coursed(g, L, w, h)
    edge = g.lerp(g.const(0.62), g.const(1.0), sat(g.op(unreal.MaterialExpressionDivide, fv, const_b=0.14)))
    joint = g.op(unreal.MaterialExpressionMin, fu, g.op(unreal.MaterialExpressionOneMinus, fu))
    joint = g.lerp(g.const(0.78), g.const(1.0), sat(g.op(unreal.MaterialExpressionDivide, joint, const_b=0.035)))
    shade = g.op(unreal.MaterialExpressionMultiply, edge, joint)
    if var is not None:
        shade = g.op(unreal.MaterialExpressionMultiply, shade,
                     g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, var, const_b=0.3, a_out="R"), const_b=0.85))
    # (courses on slate, stone slate and tile only; thatch streaked; sheet corrugated)
    coursed = g.op(unreal.MaterialExpressionOneMinus, sat(g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, k, const_b=0.5), const_b=4.0)))
    sheet = sat(g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, k, const_b=0.875), const_b=8.0))
    thatch = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionOneMinus, coursed), g.op(unreal.MaterialExpressionOneMinus, sheet))
    other = g.const(1.0)
    nt = L.noise_texture()
    if nt is not None:
        streak = g.sample(nt, g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=3.0, v_tiling=0.12), "linear")
        streak = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, streak, const_b=0.35, a_out="R"), const_b=0.8)
        other = g.lerp(other, streak, thatch)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    u = g.node(unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False); g.link(uv, u)
    wave = g.node(unreal.MaterialExpressionSine, period=0.076); g.link(u, wave)
    wave = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, wave, const_b=0.12), const_b=0.92)
    other = g.lerp(other, wave, sheet)
    shade = g.lerp(other, shade, coursed)
    col = g.op(unreal.MaterialExpressionMultiply, vc, shade)
    col = L.far_dull(g, col)
    rough = g.lerp(g.const(0.88), g.const(0.62), g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionOneMinus, to_stone), coursed))   # (slate the glossiest)
    return g.finish([col, g.const((0.0, 0.0, 1.0)), rough], specular=0.4)


def brick_material(L):
    """[OSM stage 3] The streamed world's brick walls (RideMesh::Brickwork), without any texture:
    the vertex colour the brick's, in stretcher bond (215 by 65 mm bricks, 10 mm joints), each
    brick a little lighter or darker, pale mortar, dirt and green-brown splash at the foot from the
    vertex alpha as M_RideRender has. UV0 is metres along the wall and down it."""
    g = L.Graph(L.new_material("M_RideBrick"))
    vc = g.node(unreal.MaterialExpressionVertexColor)
    col = g.op(unreal.MaterialExpressionMultiply, vc, g.const(1.0))
    fu, fv, var = _coursed(g, L, 0.225, 0.075)
    sat = lambda e: g.op(unreal.MaterialExpressionSaturate, e)
    if var is not None:
        col = g.op(unreal.MaterialExpressionMultiply, col,
                   g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, var, const_b=0.4, a_out="R"), const_b=0.8))
    brick = g.op(unreal.MaterialExpressionMultiply, sat(g.op(unreal.MaterialExpressionDivide, fu, const_b=0.045)),
                 sat(g.op(unreal.MaterialExpressionDivide, fv, const_b=0.13)))
    mortar = g.op(unreal.MaterialExpressionMultiply, vc, g.const((1.5, 1.6, 1.8)))
    mortar = g.op(unreal.MaterialExpressionMin, mortar, g.const((0.42, 0.4, 0.36)))
    col = g.lerp(mortar, col, brick)
    nt = L.noise_texture()
    if nt:
        # [build 153] (beenster: "a repetitive tiling": a wall a shade lighter and darker over several metres)
        mv = g.sample(nt, g.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=1.0 / 23.0, v_tiling=1.0 / 23.0), "rough")
        mv = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, mv, a_out="G", const_b=0.3), const_b=0.85)
        col = g.op(unreal.MaterialExpressionMultiply, col, mv)
    dirt = g.op(unreal.MaterialExpressionMultiply, col, g.const((0.42, 0.42, 0.33)))
    col = g.lerp(col, dirt, vc, alpha_out="A")
    col = L.far_dull(g, col)
    rough = g.lerp(g.const(0.95), g.const(0.85), brick)
    return g.finish([col, g.const((0.0, 0.0, 1.0)), rough], specular=0.35)


def scanned_materials(L):
    """The hand-built ride's own scanned looks, made again here under /Game/RideUK/Materials so the
    streamed world is drawn as Buttertubs is (Ride UK stage 3): M_RideLandScan (Buttertubs' M_Land:
    grass, moor grass, peat, rock on steep ground, tinted by the vertex colour the ground's cover gives
    it, World/WorldRide's LandColour, which matches that colour's meaning), M_RideStone (M_Stone, the
    dry stone walls) and M_RideMasonry (M_Masonry, stone houses and barns). Their vertex colours are
    the stone's masks (R brightness, G which scan, B lichen, A moss) and UV0 metres of surface, as
    World/WorldRideScenery makes them. Built from the same textures as Buttertubs (Data/Textures)."""
    tex, roles = L.import_textures()
    made = []
    original = L.new_material
    try:
        for want, make in (("M_RideLandScan", lambda: L.land_material_scanned(tex, roles) if "grass" in tex else None),
                           ("M_RideStone", lambda: L.stone_material(tex, roles, tint_param=True)),
                           ("M_RideMasonry", lambda: L.stone_material(tex, roles, "M_Masonry", "masonry", "masonry", broad_moss=False, tint_param=True)),
                           # (stage 5: rendered and limewashed houses, the vertex colour their wash)
                           ("M_RideRender", lambda: L.render_material(tex, roles, "M_RideRender")),
                           # (stage 5: the sea, lakes and rivers)
                           ("M_RideWater", lambda: water_material(L)),
                           # (stage 7: lit windows, street lamps and cat's eyes after dark)
                           ("M_RideNight", lambda: night_material(L)),
                           # (OSM stage 5D: traffic lights and Belisha beacons, on the shared clock)
                           ("M_RideSignal", lambda: signal_material(L)),
                           # (OSM stage 6A: road signs' faces and lettering, shining back the headlight)
                           ("M_RideSign", lambda: sign_material(L)),
                           # (OSM stage 3: roofs coursed by their roofing, and brick walls in bond)
                           ("M_RideRoof", lambda: roof_material(L)),
                           ("M_RideBrick", lambda: brick_material(L)),
                           # (build 141: the road in Buttertubs' scanned tarmac; the plain one stays without the scan)
                           ("M_RideRoad", lambda: road_material_scanned(L, tex, roles) if "asphalt" in tex else None)):
            # (the level script's makers name their own material in its own folder: put it here, under our name)
            L.new_material = lambda name, folder=None, _want=want: original(_want, MAT_DIR)
            try:
                if make() is not None:
                    made.append(want)
            except Exception as e:
                unreal.log_warning("[Torque] ride UK: couldn't make %s (%s); the game draws it plain" % (want, e))
    finally:
        L.new_material = original
    if "grass" not in tex:
        # (a stale scanned ground would otherwise be used with textures that are gone)
        path = MAT_DIR + "/M_RideLandScan"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
        unreal.log_warning("[Torque] ride UK: no grass texture (python Tools/fetch_textures.py); the ground stays plain")
    return made


SPECIES_FIELDS = ("name", "cull_distance", "cast_shadow", "scale_multiplier", "native_mesh", "keep", "cull_spread", "min_draw_distance")


def read_kit(L):
    """Buttertubs' plants and trees as its level has them (its TorqueFoliage actors "Foliage" and
    "Trees"): {label: {"frame": (yaw, mirror, scale), "species": [{field: value, "mesh": path, ...}]}},
    asset paths rather than objects, as the level is let go before the ride's is built."""
    if not hasattr(unreal, "TorqueFoliage"):
        unreal.log_warning("[Torque] ride UK: the TorqueFoliage class isn't built; no grass or trees")
        return {}
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.load_level(L.MAP):
        unreal.log_warning("[Torque] ride UK: couldn't open %s for its plants; no grass or trees" % L.MAP)
        return {}
    path = lambda o: o.get_path_name() if o else ""
    kit = {}
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        label = a.get_actor_label()
        if not isinstance(a, unreal.TorqueFoliage) or label not in ("Foliage", "Trees"):
            continue
        rows = []
        for e in a.get_editor_property("species"):
            r = {k: e.get_editor_property(k) for k in SPECIES_FIELDS}
            r["name"] = str(r["name"])
            r["mesh"] = path(e.get_editor_property("mesh"))
            r["material"] = path(e.get_editor_property("material"))
            r["variants"] = [path(m) for m in e.get_editor_property("variants")]
            r["variant_materials"] = [path(m) for m in e.get_editor_property("variant_materials")]
            rows.append(r)
        kit[label] = {"frame": (a.get_editor_property("frame_yaw"), a.get_editor_property("frame_mirror"), a.get_editor_property("frame_scale")),
                      "species": rows}
    log("Buttertubs' plants: %s" % ", ".join("%s %d species" % (k, len(v["species"])) for k, v in kit.items()))
    return kit


def grass_materials(L, kit):
    """The grass's materials without Buttertubs' ground-colour virtual texture (the ride has none: the
    plants would sample black): M_RideFoliage for our own plants, and the scanned plants' instances
    copied onto M_RideFoliageScan, the scanned master made afresh with no ground blend. Changes the
    kit's "Foliage" rows in place."""
    rows = kit.get("Foliage", {}).get("species", [])
    if not rows:
        return
    original = L.new_material
    try:
        L.new_material = lambda name, folder=None: original("M_RideFoliage", MAT_DIR)
        plain = L.foliage_material(None, 1.0)
        defaults = {}
        for r in rows:
            for p in r["variants"]:
                m = unreal.load_asset(p)
                if isinstance(m, unreal.StaticMesh):
                    for k, v in L.scan_textures(m).items():
                        defaults.setdefault(k, v)
        master = None
        if "BaseColor" in defaults:
            L.new_material = lambda name, folder=None: original("M_RideFoliageScan", MAT_DIR)
            master = L.scan_foliage_master(defaults, None, 1.0)
    finally:
        L.new_material = original
    plain_path = plain.get_path_name() if plain else ""
    copied = {}
    for r in rows:
        if r["material"] or not r["variants"]:
            r["material"] = plain_path
        mats = []
        for p in r["variant_materials"]:
            if not p or master is None:
                mats.append(p if master is None else "")
                continue
            if p not in copied:
                src = p.split(".")[0]
                dst = MAT_DIR + "/Ride" + src.rsplit("/", 1)[-1]
                if unreal.EditorAssetLibrary.does_asset_exist(dst):
                    unreal.EditorAssetLibrary.delete_asset(dst)
                mi = unreal.EditorAssetLibrary.duplicate_asset(src, dst)
                if mi:
                    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, master)
                    unreal.EditorAssetLibrary.save_loaded_asset(mi)
                copied[p] = mi.get_path_name() if mi else ""
            mats.append(copied[p])
        r["variant_materials"] = mats
    log("grass materials: M_RideFoliage%s, %d scanned instances" % (", M_RideFoliageScan" if master else "", len(copied)))


# (outside /Game/RideUK, which DefaultGame.ini always cooks: the copies cook only when the kit plants them,
# so while scanned_trees.json is off they stay out of the package; round 144 cooked them, +452 MB)
SCAN_DEST = "/Game/ScannedTrees"
OLD_SCAN_DEST = "/Game/RideUK/ScannedTrees"


def scanned_trees(L, kit):
    """Scanned trees (Data/RideUK/scanned_trees.json: Quixel Megaplants once imported on the PC) in
    the kit's "Trees" in place of Buttertubs' own models of the same kind, as species named
    SM_Tree_<kind>_scan (native meshes with their own materials). Changes the kit in place."""
    f = os.path.join(os.path.dirname(L.data_dir()), "RideUK", "scanned_trees.json")
    if not os.path.exists(f) or "Trees" not in kit:
        return
    spec = json.load(open(f))
    use = spec.get("enabled", True)
    if not use:
        log("scanned trees: off (scanned_trees.json enabled=false): our own trees stay; a trial of what would be used follows")
    norm = lambda t: "".join(ch for ch in t.lower() if ch.isalnum())
    skip = [norm(w) for w in spec.get("skip", [])]
    most = int(spec.get("most", 6))
    meshes, others = [], []
    for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path("/Game", recursive=True):
        pkg = str(a.package_name)
        if pkg.startswith("/Game/Buttertubs/") or pkg.startswith("/Game/RideUK/") or pkg.startswith(SCAN_DEST + "/"):
            continue
        cls = str(a.asset_class_path.asset_name)
        path = "%s.%s" % (pkg, a.asset_name)
        if cls == "StaticMesh":
            meshes.append((norm(pkg), path))
        elif cls not in ("Material", "MaterialInstanceConstant", "Texture2D", "MaterialFunction", "PhysicsAsset", "Skeleton"):
            others.append((norm(pkg), path, cls))
    rows = kit["Trees"]["species"]
    template = dict(rows[0]) if rows else {"cull_distance": 3000000.0, "cast_shadow": True}
    found = {}

    def mesh_of(path):
        """A static mesh a non-mesh asset (a Megaplant's Procedural Vegetation or Nanite assembly asset,
        a blueprint) holds or makes, or None."""
        try:
            obj = unreal.load_asset(path)
        except Exception:
            return None
        if obj is None:
            return None
        if isinstance(obj, unreal.StaticMesh):
            return obj.get_path_name()
        for prop in ("static_mesh", "mesh", "output_mesh", "nanite_mesh", "generated_mesh", "final_mesh", "base_mesh"):
            try:
                m = obj.get_editor_property(prop)
            except Exception:
                continue
            if isinstance(m, unreal.StaticMesh):
                return m.get_path_name()
        return None

    def gs(*names):
        """The first Geometry Script function found by name (its Python home moved between releases)."""
        for home in ("GeometryScript_AssetUtils", "GeometryScript_SkeletalMeshFunctions", "GeometryScript_NewAssetUtils",
                     "GeometryScript_CreateNewAssetFunctions", "GeometryScript_StaticMeshFunctions"):
            c = getattr(unreal, home, None)
            for n in names:
                if c is not None and hasattr(c, n):
                    return getattr(c, n)
        return None

    why = []
    if unreal.EditorAssetLibrary.does_directory_exist(OLD_SCAN_DEST):
        # (round 144's copies, moved out of the always-cooked folder rather than made again)
        if unreal.EditorAssetLibrary.does_directory_exist(SCAN_DEST):
            unreal.EditorAssetLibrary.delete_directory(OLD_SCAN_DEST)
            log("scanned trees: %s deleted (%s already made)" % (OLD_SCAN_DEST, SCAN_DEST))
        else:
            ok = unreal.EditorAssetLibrary.rename_directory(OLD_SCAN_DEST, SCAN_DEST)
            log("scanned trees: %s moved to %s%s" % (OLD_SCAN_DEST, SCAN_DEST, "" if ok else " (FAILED: delete it in the editor)"))

    # (copies of parts a skip word now leaves out, made before it was added: round 144's pine needles)
    for a in unreal.EditorAssetLibrary.list_assets(SCAN_DEST, recursive=True, include_folder=False) if unreal.EditorAssetLibrary.does_directory_exist(SCAN_DEST) else []:
        n = norm(a.rsplit("/", 1)[-1])
        if any(w in n for w in skip):
            gone = unreal.EditorAssetLibrary.delete_asset(a.split(".")[0])
            log("scanned trees: %s %s (a skip word)" % (a.split(".")[0], "deleted" if gone else "NOT deleted"))

    def static_of_skeletal(path):
        """A Megaplant's whole tree is a SkeletalMesh (its bones sway in the wind): a static copy of it,
        made once with Geometry Script into SCAN_DEST (Nanite, no collision, the tree's own materials).
        The path of the copy, or None (the reason in why)."""
        name = "SM_" + path.rsplit(".", 1)[-1]
        dest = "%s/%s" % (SCAN_DEST, name)
        if unreal.EditorAssetLibrary.does_asset_exist(dest):
            return "%s.%s" % (dest, name)
        copy = gs("copy_mesh_from_skeletal_mesh")
        make = gs("create_new_static_mesh_asset_from_mesh")
        if copy is None or make is None:
            why.append("no Geometry Script (enable the GeometryScripting plugin)")
            return None
        try:
            skel = unreal.load_asset(path)
            dm = unreal.DynamicMesh()
            res = copy(skel, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
            dm = res[0] if isinstance(res, (tuple, list)) else dm
            opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
            for prop, val in (("enable_nanite", True), ("enable_collision", False), ("enable_recompute_normals", False),
                              ("enable_recompute_tangents", False)):
                try:
                    opts.set_editor_property(prop, val)
                except Exception:
                    pass
            res = make(dm, dest, opts)
            sm = res[0] if isinstance(res, (tuple, list)) else res
            if not isinstance(sm, unreal.StaticMesh):
                why.append("%s: no mesh made" % name)
                return None
            # (the tree's own materials, slot for slot as its sections were copied)
            mats = []
            for m in skel.get_editor_property("materials"):
                mats.append(unreal.StaticMaterial(material_interface=m.get_editor_property("material_interface"),
                                                  material_slot_name=m.get_editor_property("material_slot_name")))
            if mats:
                sm.set_editor_property("static_materials", mats)
            unreal.EditorAssetLibrary.save_asset(dest, only_if_is_dirty=False)
            return sm.get_path_name()
        except Exception as e:
            why.append("%s: %s" % (name, e))
            return None

    def ready_materials(path):
        """A static copy's materials made fit for the ride's instanced trees: a Megaplant's are made for
        its skeletal mesh, and a material not flagged for instanced static meshes draws as the default
        grey checker in a packaged game (the base material's usage flags)."""
        try:
            sm = unreal.load_asset(path.split(".")[0])
            done = []
            for m in sm.get_editor_property("static_materials"):
                mi = m.get_editor_property("material_interface")
                base = mi.get_base_material() if mi else None
                if base is None or base.get_path_name() in done:
                    continue
                done.append(base.get_path_name())
                changed = False
                for flag in ("used_with_instanced_static_meshes", "used_with_nanite"):
                    try:
                        if not base.get_editor_property(flag):
                            base.set_editor_property(flag, True)
                            changed = True
                    except Exception:
                        pass
                if changed:
                    unreal.MaterialEditingLibrary.recompile_material(base)
                    unreal.EditorAssetLibrary.save_loaded_asset(base)
                    log("scanned trees: %s now flagged for instanced static meshes and Nanite" % base.get_path_name())
            return True
        except Exception as e:
            why.append("%s materials: %s" % (path.rsplit(".", 1)[-1], e))
            return None

    for kind, words in spec["kinds"].items():
        keys = [norm(w) for w in words]
        wanted = lambda n: any(k in n for k in keys) and not any(w in n for w in skip)
        got = sorted(p for n, p in meshes if wanted(n))[:most]
        theirs = [(p, c) for n, p, c in others if wanted(n)]
        classes = sorted(set(c for _, c in theirs))
        log("scanned trees: %s: %d static meshes; other assets: %s" % (kind, len([1 for n, _ in meshes if wanted(n)]),
            ", ".join("%s x%d" % (c, len([1 for _, cc in theirs if cc == c])) for c in classes) or "none"))
        skels = sorted(p for p, c in theirs if c == "SkeletalMesh")[:most]
        if skels:
            # (Megaplants: the whole trees are skeletal meshes; the static ones are only leaves and buds)
            made = [m for m in (static_of_skeletal(p) for p in skels) if m]
            for m in made:
                ready_materials(m)
            log("scanned trees: %s: %d whole trees from its skeletal meshes (%s)%s" % (kind, len(made),
                ", ".join(p.rsplit(".", 1)[-1] for p in skels), ("; " + "; ".join(why[-3:])) if not made and why else ""))
            got = made or got
        if not got and theirs:
            # (no plain mesh: whatever the vegetation assets hold or make)
            made = sorted(set(m for m in (mesh_of(p) for p, _ in theirs[:24]) if m))[:most]
            log("scanned trees: %s: %d meshes from its %s assets%s" % (kind, len(made), "/".join(classes),
                "" if made else " (none: open one in the editor and export it as a static mesh, or tell the hub its class)"))
            got = made
        if not got:
            continue
        found[kind] = got
        if not use:
            continue
        # (ours of this kind go near: the scanned ones take every near tree of it. Our far models stay
        # for the trees past the finest squares: build 148 measured the scans at about 10 ms of GPU at
        # Whaley Bridge, most of it from the far ring's thousands of full Nanite trees)
        rows[:] = [r for r in rows if not r["name"].startswith("SM_Tree_%s_" % kind)]
        r = dict(template)
        r.update({"name": "SM_Tree_%s_scan" % kind, "mesh": "", "material": "", "variants": got, "variant_materials": [],
                  "native_mesh": True, "scale_multiplier": 1.0, "keep": 1.0, "cull_spread": 1.0, "min_draw_distance": 0.0, "cast_shadow": True})
        rows.append(r)
        # [perf, build 155] Far off, the scans as impostors baked from them (Tools/ue_tree_impostors.py)
        # in place of Buttertubs' low-poly far models (beenster, build 153: "the new trees are BAD").
        if spec.get("impostors", True):
            try:
                sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
                import ue_tree_impostors as IMP
                cards = IMP.bake_all(got, rebake=bool(spec.get("rebake_impostors", False)))
            except Exception as e:
                log("scanned trees: %s: impostors failed: %s" % (kind, e))
                cards = []
            # (the same order as the scans, so a tree's near scan and far card are the same variant;
            # if any one failed, the kind keeps its old far models rather than mismatching)
            if cards and all(cards):
                rows[:] = [x for x in rows if not x["name"].startswith("SM_TreeFar_%s_" % kind)]
                f = dict(template)
                f.update({"name": "SM_TreeFar_%s_imp" % kind, "mesh": "", "material": "", "variants": cards, "variant_materials": [],
                          "native_mesh": True, "scale_multiplier": 1.0, "keep": 1.0, "cull_spread": 1.0, "min_draw_distance": 0.0, "cast_shadow": False})
                rows.append(f)
                log("scanned trees: %s: %d impostors for the far trees" % (kind, len(cards)))
            else:
                log("scanned trees: %s: impostors NOT used (%d of %d baked); old far models kept" % (kind, len([c for c in cards if c]), len(got)))
    log("scanned trees%s: %s" % ("" if use else " (trial, not planted)", ", ".join("%s %d (%s)" % (k, len(v), v[0].rsplit("/", 1)[-1]) for k, v in found.items()) or
                               "none found (import the Megaplants on the PC; Data/RideUK/scanned_trees.json)"))


HEDGE_DEST = "/Game/RideUK/Hedges"


def hedge_kit(L, kit):
    """[build 149] The hedges (beenster, build 148: "look like something from roblox"): Tools/make_hedges.py's
    4.2 m lengths of hedge (Data/RideUK/Hedges/SM_Tree_hedge_<n>.glb) imported as Nanite foliage with
    the trees' material, added to the kit's "Trees" as kind "hedge"; World/TorqueRideWorld lays them
    end to end along the map's hedge lines on the finest squares (RidePlan::HedgePlants). A scanned
    hedge from Fab (a static mesh whose path holds one of scanned_trees.json's "hedge_words") takes
    their place, scaled to a 4.2 m length. Changes the kit in place."""
    if "Trees" not in kit:
        return
    rows = kit["Trees"]["species"]
    folder = os.path.join(os.path.dirname(L.data_dir()), "RideUK", "Hedges")
    norm = lambda t: "".join(ch for ch in t.lower() if ch.isalnum())
    spec_f = os.path.join(os.path.dirname(L.data_dir()), "RideUK", "scanned_trees.json")
    words = [norm(w) for w in (json.load(open(spec_f)).get("hedge_words", []) if os.path.exists(spec_f) else []) if norm(w)]
    skip = [norm(w) for w in ("lod", "billboard", "impostor", "leaf", "leaves", "atlas", "branch", "twig", "decal")]
    theirs = []
    if words:
        for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path("/Game", recursive=True):
            pkg = str(a.package_name)
            if str(a.asset_class_path.asset_name) != "StaticMesh" or pkg.startswith("/Game/Buttertubs/") or pkg.startswith("/Game/RideUK/"):
                continue
            n = norm(pkg)
            if any(w in n for w in words) and not any(k in n for k in skip):
                theirs.append("%s.%s" % (pkg, a.asset_name))
    # (a template row: a hawthorn's, else any of ours, for the material and the trees' settings)
    template = next((r for r in rows if r["name"].startswith("SM_Tree_hawthorn_")), None) or next((r for r in rows if r.get("material")), None)
    if template is None:
        log("hedges: no tree rows in the kit to copy; hedges stay as shapes")
        return
    if theirs:
        theirs = sorted(theirs)[:4]
        b = [unreal.load_asset(p).get_bounding_box() for p in theirs]
        longest = sum(max(x.max.x - x.min.x, x.max.y - x.min.y) for x in b) / len(b)
        r = dict(template)
        r.update({"name": "SM_Tree_hedge_scan", "mesh": "", "material": "", "variants": theirs, "variant_materials": [],
                  "native_mesh": True, "scale_multiplier": 420.0 / max(longest, 1.0), "cast_shadow": True,
                  "cull_distance": 60000.0})
        rows.append(r)
        log("hedges: %d scanned hedge models from Fab (%s)" % (len(theirs), theirs[0].rsplit("/", 1)[-1]))
        return
    if not os.path.isdir(folder) or not any(f.endswith(".glb") for f in os.listdir(folder)):
        log("hedges: none (python Tools/make_hedges.py); hedges stay as shapes")
        return
    meshes = L.import_glbs(folder, HEDGE_DEST)
    for name, mesh in sorted(meshes.items()):
        L.foliage_nanite(mesh)
        try:
            unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).remove_collisions(mesh)
        except Exception:
            pass
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
        r = dict(template)
        r.update({"name": name, "mesh": mesh.get_path_name(), "variants": [], "variant_materials": [],
                  "native_mesh": False, "scale_multiplier": 1.0, "cast_shadow": True, "cull_distance": 60000.0})
        rows.append(r)
    log("hedges: %d hedge models (%s)" % (len(meshes), ", ".join(sorted(meshes))))


def place_kit(kit):
    """The ride's plant kit: two TorqueFoliage actors that plant nothing themselves (Density 0), tagged
    RideGrass and RideTrees, with Buttertubs' species; World/TorqueRideWorld lays them out as it goes."""
    load = lambda p: unreal.load_asset(p) if p else None
    for label, tag in (("Foliage", "RideGrass"), ("Trees", "RideTrees")):
        k = kit.get(label)
        if not k:
            continue
        species = []
        for r in k["species"]:
            e = unreal.TorqueFoliageSpecies()
            for f in SPECIES_FIELDS:
                e.set_editor_property(f, r[f])
            e.set_editor_property("mesh", load(r["mesh"]))
            e.set_editor_property("material", load(r["material"]))
            e.set_editor_property("variants", [m for m in (load(p) for p in r["variants"]) if m])
            e.set_editor_property("variant_materials", [load(p) for p in r["variant_materials"]][:len(r["variants"])])
            species.append(e)
        a = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.TorqueFoliage, unreal.Vector(0, 0, 0))
        a.set_actor_label(tag)
        a.set_editor_property("tags", [unreal.Name(tag)])
        a.set_editor_property("instances_file", "")
        a.set_editor_property("density", 0.0)
        a.set_editor_property("species", species)
        a.set_editor_property("frame_yaw", float(k["frame"][0]))
        a.set_editor_property("frame_mirror", float(k["frame"][1]))
        a.set_editor_property("frame_scale", float(k["frame"][2]))
        log("%s: %d species" % (tag, len(species)))


ANIMAL_DEST = "/Game/RideUK/Animals"


def animal_kit(L):
    """The livestock (Ride UK stage 6): Data/RideUK/Animals/*.glb (Tools/make_ride_animals.py) imported
    to /Game/RideUK/Animals, no collision, and listed in a TorqueFoliage actor tagged RideAnimals that
    plants nothing itself; World/TorqueRideWorld puts them in the fields as it goes. The sheep wear
    M_RideFleece (vertex colour, grazing heads nodding as Buttertubs' do), the cattle M_RideHide.
    Returns a short note for the log."""
    if not hasattr(unreal, "TorqueFoliage"):
        return "no animals (TorqueFoliage isn't built)"
    folder = os.path.join(os.path.dirname(L.data_dir()), "RideUK", "Animals")
    if not os.path.isdir(folder) or not any(f.endswith(".glb") for f in os.listdir(folder)):
        return "no animals (python Tools/make_ride_animals.py)"
    meshes = L.import_glbs(folder, ANIMAL_DEST)
    sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for mesh in meshes.values():
        try:
            sme.remove_collisions(mesh)
        except Exception:
            pass
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    original = L.new_material
    try:
        L.new_material = lambda name, folder=None: original(name, MAT_DIR)
        fleece = L.flat_material("M_RideFleece", 0.95, fine=0.4, wpo=L.graze_wpo)
        hide = L.flat_material("M_RideHide", 0.8, fine=0.25)
    finally:
        L.new_material = original
    first = sorted(meshes)[0]
    frame = L.find_frame(meshes[first], os.path.join(folder, first + ".glb"))
    species = []
    for name, mesh in sorted(meshes.items()):
        e = unreal.TorqueFoliageSpecies()
        e.set_editor_property("name", name)
        e.set_editor_property("mesh", mesh)
        e.set_editor_property("material", fleece if ("_ewe_" in name or "_lamb_" in name) else hide)
        for k, v in (("cast_shadow", True), ("scale_multiplier", 1.0), ("cull_distance", 40000.0)):
            try:
                e.set_editor_property(k, v)
            except Exception:
                pass
        species.append(e)
    a = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.TorqueFoliage, unreal.Vector(0, 0, 0))
    a.set_actor_label("RideAnimals")
    a.set_editor_property("tags", [unreal.Name("RideAnimals")])
    a.set_editor_property("instances_file", "")
    a.set_editor_property("density", 0.0)
    a.set_editor_property("species", species)
    a.set_editor_property("frame_yaw", float(frame[0]))
    a.set_editor_property("frame_mirror", float(frame[1]))
    a.set_editor_property("frame_scale", float(frame[2]))
    return "RideAnimals: %d models" % len(species)


PROP_DEST = "/Game/RideUK/Props"
WALL_DEST = "/Game/RideUK/Walls"
# [build 154] The walls' styles' stone tints, in Tools/make_wall_kit.py's WALL_STYLES order (dales, cotswold,
# slate, gritstone, granite, sandstone), as World/WorldRideStyle.cpp's One, Honey, Slate, Gritstone, Granite
# and Sandstone; and the models of each.
WALL_TINTS = [(1.0, 1.0, 1.0), (1.30, 1.06, 0.66), (0.66, 0.70, 0.70), (0.80, 0.76, 0.68), (0.92, 0.90, 0.88), (1.12, 0.98, 0.82)]
WALL_VARIANTS = 4
FACADE_DEST = "/Game/RideUK/Facade"


def facade_material(L):
    """M_RideFacade (fps plan step 2): the facade kit's colours. Each vertex's alpha says which colour it
    takes (1 its own, as glass; 0.66 the paint, 0.33 the trim, 0 the third) and its RGB, at half, the
    shade; the three colours come per instance (nine custom data floats, World/TorqueRideWorld PlaceProps),
    so a street of houses is a handful of instanced models however its doors are painted."""
    g = L.Graph(L.new_material("M_RideFacade"))
    L.instanced_usage(g.m)
    vc = g.node(unreal.MaterialExpressionVertexColor)

    def custom(index):
        cls = getattr(unreal, "MaterialExpressionPerInstanceCustomData3Vector", None)
        if cls is not None:
            return g.node(cls, data_index=index)
        parts = [g.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=index + k) for k in range(3)]
        return g.op(unreal.MaterialExpressionAppendVector, g.op(unreal.MaterialExpressionAppendVector, parts[0], parts[1]), parts[2])

    def above(edge):   # 1 where the alpha is over edge, else 0
        return g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                    g.op(unreal.MaterialExpressionSubtract, vc, a_out="A", const_b=edge), const_b=100.0))

    paint, trim, third = custom(0), custom(3), custom(6)
    tint = g.lerp(third, trim, above(0.17))
    tint = g.lerp(tint, paint, above(0.5))
    shade = g.op(unreal.MaterialExpressionMultiply, vc, a_out="RGB", const_b=2.0)
    own = above(0.83)
    col = g.lerp(g.op(unreal.MaterialExpressionMultiply, tint, shade), vc, own, b_out="RGB")
    rough = g.lerp(g.const(0.7), g.const(0.12), own)
    L.mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    L.mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    L.mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    return g.m


STREET_KIT_KINDS = ("street_lamp", "power_pole")   # (Tools/blender_street_kit.py's models: M_RideStreetKit)


def street_kit_texture(L, file_name, asset, compression, nearest=False):
    """One of Data/Textures/StreetKit's textures (Tools/make_street_kit_textures.py) as
    /Game/RideUK/Textures/<asset>, linear; None if it isn't there."""
    f = os.path.join(os.path.dirname(L.data_dir()), "Textures", "StreetKit", file_name)
    if not os.path.exists(f):
        return None
    dest = "/Game/RideUK/Textures"
    t = unreal.AssetImportTask()
    for k, v in (("filename", f), ("destination_path", dest), ("destination_name", asset),
                 ("automated", True), ("replace_existing", True), ("save", False)):
        t.set_editor_property(k, v)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    tex = unreal.load_asset(dest + "/" + asset)
    if tex:
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", compression)
        if nearest:
            # (the table: one texel per surface, read exactly)
            tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
            tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


def street_kit_material(L):
    """[street kit] M_RideStreetKit: the one material of the lamp posts and telegraph poles that
    Tools/blender_street_kit.py builds in Blender (Chat with Fable, approved by beenster Nov 18: "please
    build them yourself in blender and use suitable materials to improve their look"). The vertex colour
    is the colour; its alpha a byte, 32 * surface + 2 + 28 * dirt, the surface a column of T_StreetKitLut
    (Tools/make_street_kit_textures.py SURFACES: timber, galvanised steel, concrete, porcelain, glass,
    enamel, bare steel), which gives its roughness, metal, specular and how much of each detail it shows:
    the timber's grain and checks, the zinc's spangle, the concrete's pits, and weathering, from
    T_StreetKitDetail and T_StreetKitNormal over UV0 in metres. Four texture reads for every kind of
    surface, so one material and draw for the whole kit. Falls back to M_RideProp's look (None) if the
    textures aren't there."""
    detail = street_kit_texture(L, "detail.png", "T_StreetKitDetail", unreal.TextureCompressionSettings.TC_DEFAULT)
    nmap = street_kit_texture(L, "normal.png", "T_StreetKitNormal", unreal.TextureCompressionSettings.TC_DEFAULT)
    lut = street_kit_texture(L, "lut.png", "T_StreetKitLut", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP, True)
    if detail is None or nmap is None or lut is None:
        return None
    g = L.Graph(L.new_material("M_RideStreetKit"))
    L.instanced_usage(g.m)
    E = unreal
    mul = lambda a, k, a_out="": g.op(E.MaterialExpressionMultiply, a, const_b=float(k), a_out=a_out)
    add = lambda a, b: g.op(E.MaterialExpressionAdd, a, b)
    addk = lambda a, k: g.op(E.MaterialExpressionAdd, a, const_b=float(k))
    times = lambda a, b, a_out="", b_out="": g.op(E.MaterialExpressionMultiply, a, b, a_out=a_out, b_out=b_out)
    pin = lambda node, p: mul(node, 1.0, p)
    vc = g.node(E.MaterialExpressionVertexColor)
    # (the surface and its dirt, from the alpha's byte)
    band = mul(vc, 255.0 / 32.0, "A")
    surface = g.op(E.MaterialExpressionFloor, band)
    dirt = g.op(E.MaterialExpressionSaturate, mul(addk(mul(g.op(E.MaterialExpressionFrac, band), 32.0), -2.0), 1.0 / 28.0))
    u = mul(addk(surface, 0.5), 1.0 / 8.0)
    row0 = g.sample(lut, g.op(E.MaterialExpressionAppendVector, u, g.const(0.25)), "linear")
    row1 = g.sample(lut, g.op(E.MaterialExpressionAppendVector, u, g.const(0.75)), "linear")
    uv = g.node(E.MaterialExpressionTextureCoordinate)
    d = g.sample(detail, uv, "linear")
    centred = lambda p: addk(pin(d, p), -0.5)
    f = times(mul(centred("R"), 0.7), row0, b_out="A")                       # grain
    f = add(f, times(mul(centred("G"), 0.5), row1, b_out="R"))               # spangle
    f = add(f, times(mul(centred("B"), 0.6), row1, b_out="G"))               # pits
    f = add(f, times(mul(centred("A"), 0.5), row1, b_out="B"))               # weathering
    f = add(f, addk(mul(dirt, -0.45), 1.0))
    base = times(vc, f)
    rough = add(pin(row0, "R"), add(mul(dirt, 0.25), times(mul(centred("G"), 0.25), row1, b_out="R")))
    metal = times(pin(row0, "G"), addk(mul(dirt, -0.5), 1.0))
    spec = pin(row0, "B")
    n = g.sample(nmap, uv, "linear")
    rg = g.node(E.MaterialExpressionComponentMask, r=True, g=True, b=False, a=False); g.link(n, rg)
    ba = g.node(E.MaterialExpressionComponentMask, r=False, g=False, b=True, a=True); g.link(n, ba)
    wood = times(addk(mul(rg, 2.0), -1.0), row0, b_out="A")
    pits = times(addk(mul(ba, 2.0), -1.0), row1, b_out="G")
    xy = times(add(wood, pits), row1, b_out="A")
    normal = g.node(E.MaterialExpressionNormalize)
    g.link(g.op(E.MaterialExpressionAppendVector, xy, g.const(1.0)), normal)
    mel = L.mel
    mel.connect_material_property(base, "", E.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(g.op(E.MaterialExpressionSaturate, rough), "", E.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(metal, "", E.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(spec, "", E.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(normal, "", E.MaterialProperty.MP_NORMAL)
    mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    return g.m


def prop_kit(L):
    """The things by the road (OSM stage 2): Data/RideUK/Props/*.glb (Tools/make_ride_props.py)
    imported to /Game/RideUK/Props, no collision, and any static mesh under /Game whose path holds one
    of a kind's words in Data/RideUK/prop_kit.json (Fab models imported on the PC; a scan of a real,
    named place only when the file's use_real_scans is true), which take that kind's place. All listed
    in a TorqueFoliage actor tagged RideProps that plants nothing itself, each as a species named
    SM_Prop_<kind>_<n>; World/TorqueRideWorld puts them by the road as it goes, each kind scaled to its
    height in life. Returns a short note for the log."""
    if not hasattr(unreal, "TorqueFoliage"):
        return "no props (TorqueFoliage isn't built)"
    ride = os.path.join(os.path.dirname(L.data_dir()), "RideUK")
    spec_file = os.path.join(ride, "prop_kit.json")
    if not os.path.exists(spec_file):
        return "no props (Data/RideUK/prop_kit.json is missing)"
    spec = json.load(open(spec_file))
    norm = lambda t: "".join(ch for ch in t.lower() if ch.isalnum())
    skip = [norm(w) for w in spec.get("skip", [])]
    most = int(spec.get("most", 4))
    real = bool(spec.get("use_real_scans", False))
    # Ours, made by Tools/make_ride_props.py.
    folder = os.path.join(ride, "Props")
    ours = {}
    if os.path.isdir(folder) and any(f.endswith(".glb") for f in os.listdir(folder)):
        ours = L.import_glbs(folder, PROP_DEST)
        sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        for mesh in ours.values():
            try:
                sme.remove_collisions(mesh)
            except Exception:
                pass
            unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    # [fps plan step 2] The houses' facade kit, made by Tools/make_facade_kit.py: windows, their surrounds
    # and doors, a model per size, coloured per instance by M_RideFacade (always ours, never Fab's).
    facade_folder = os.path.join(ride, "Facade")
    facade = {}
    if os.path.isdir(facade_folder) and any(f.endswith(".glb") for f in os.listdir(facade_folder)):
        facade = L.import_glbs(facade_folder, FACADE_DEST)
        sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        for mesh in facade.values():
            try:
                sme.remove_collisions(mesh)
            except Exception:
                pass
            unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    # Others' (Fab), by the kit's words.
    meshes = []
    for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path("/Game", recursive=True):
        if str(a.asset_class_path.asset_name) != "StaticMesh":
            continue
        pkg = str(a.package_name)
        if pkg.startswith("/Game/Buttertubs/") or pkg.startswith("/Game/RideUK/"):
            continue
        meshes.append((norm(pkg), "%s.%s" % (pkg, a.asset_name)))
    found = {}
    for kind, row in spec["kinds"].items():
        keys = [norm(w) for w in row.get("words", []) + (row.get("real_words", []) if real else []) if norm(w)]
        got = sorted(p for n, p in meshes if any(k in n for k in keys) and not any(w in n for w in skip))[:most] if keys else []
        if got:
            found[kind] = got
    original = L.new_material
    try:
        L.new_material = lambda name, folder=None: original(name, MAT_DIR)
        paint = L.flat_material("M_RideProp", 0.7, fine=0.25)
        kit_paint = street_kit_material(L) if any(n.startswith("SM_Prop_%s_" % k) for n in ours for k in STREET_KIT_KINDS) else None
        facade_paint = facade_material(L) if facade else None
    finally:
        L.new_material = original
    frame = (0.0, 1.0, 1.0)
    if ours:
        first = sorted(ours)[0]
        frame = L.find_frame(ours[first], os.path.join(folder, first + ".glb"))
    species = []

    def add(name, mesh, native, row, material=None):
        e = unreal.TorqueFoliageSpecies()
        e.set_editor_property("name", name)
        e.set_editor_property("mesh", mesh)
        if not native:
            e.set_editor_property("material", material or paint)
        for k, v in (("cast_shadow", bool(row.get("shadow", False))), ("scale_multiplier", 1.0),
                     ("cull_distance", float(row.get("reach", 400)) * 100.0), ("native_mesh", native)):
            try:
                e.set_editor_property(k, v)
            except Exception:
                pass
        species.append(e)

    size_of = lambda name: int(name.rsplit("_", 1)[1]) if name.rsplit("_", 1)[1].isdigit() else 0
    # [build 154] The dry stone walls, laid stone by stone in each region's walling (Tools/make_wall_kit.py):
    # Data/RideUK/Walls imported to /Game/RideUK/Walls, each style in its own instance of M_RideStone tinted
    # its stone (WALL_TINTS, as World/WorldRideStyle's region tints). The game picks a model by style, so a
    # kind goes in whole or not at all; and they must have come in the kit's frame (checked).
    walls = {}
    wall_folder = os.path.join(ride, "Walls")
    stone = unreal.load_asset(MAT_DIR + "/M_RideStone")
    if stone and os.path.isdir(wall_folder) and any(f.endswith(".glb") for f in os.listdir(wall_folder)):
        walls = L.import_glbs(wall_folder, WALL_DEST)
        sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        for name, mesh in walls.items():
            try:
                sme.remove_collisions(mesh)
            except Exception:
                pass
            try:
                ns = mesh.get_editor_property("nanite_settings")
                ns.set_editor_property("enabled", True)
                mesh.set_editor_property("nanite_settings", ns)
            except Exception as e:
                unreal.log_warning("[Torque] walls: Nanite not set on %s (%s)" % (name, e))
            unreal.EditorAssetLibrary.save_loaded_asset(mesh)
        first = sorted(walls)[0]
        if ours and tuple(L.find_frame(walls[first], os.path.join(wall_folder, first + ".glb"))) != tuple(frame):
            unreal.log_warning("[Torque] walls imported in another frame than the prop kit's: left code-made")
            walls = {}
    wall_tints = []
    if walls:
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        mel = unreal.MaterialEditingLibrary
        for si, tint in enumerate(WALL_TINTS):
            name = "MI_RideWall_%d" % si
            path = MAT_DIR + "/" + name
            if unreal.EditorAssetLibrary.does_asset_exist(path):
                unreal.EditorAssetLibrary.delete_asset(path)
            mi = asset_tools.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            mel.set_material_instance_parent(mi, stone)
            mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
            unreal.EditorAssetLibrary.save_loaded_asset(mi)
            wall_tints.append(mi)
    for kind, row in spec["kinds"].items():
        if row.get("folder") == "Walls":
            names = sorted((n for n in walls if n.startswith("SM_Prop_%s_" % kind) and n.rsplit("_", 1)[1].isdigit()), key=size_of)
            if len(names) != len(WALL_TINTS) * WALL_VARIANTS or [size_of(n) for n in names] != list(range(len(names))):
                if walls:
                    unreal.log_warning("[Torque] walls: %d models of %s, not %d: left code-made" % (len(names), kind, len(WALL_TINTS) * WALL_VARIANTS))
                continue
            for n in names:
                add(n, walls[n], False, row, wall_tints[size_of(n) // WALL_VARIANTS])
            continue
        if float(row.get("height", 1.0)) <= 0.0:
            # (the facade kit's: in order of size, the game's Variant)
            for name, mesh in sorted(facade.items(), key=lambda kv: size_of(kv[0])):
                if name.startswith("SM_Prop_%s_" % kind) and size_of(name) >= 0 and name[len("SM_Prop_%s_" % kind):].isdigit():
                    add(name, mesh, False, row, facade_paint)
        elif kind in found:
            for n, p in enumerate(found[kind]):
                m = unreal.load_asset(p)
                if m:
                    add("SM_Prop_%s_fab%d" % (kind, n), m, True, row)
        else:
            for name, mesh in sorted(ours.items()):
                if name.startswith("SM_Prop_%s_" % kind):
                    add(name, mesh, False, row, kit_paint if kind in STREET_KIT_KINDS else None)
    a = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.TorqueFoliage, unreal.Vector(0, 0, 0))
    a.set_actor_label("RideProps")
    a.set_editor_property("tags", [unreal.Name("RideProps")])
    a.set_editor_property("instances_file", "")
    a.set_editor_property("density", 0.0)
    a.set_editor_property("species", species)
    a.set_editor_property("frame_yaw", float(frame[0]))
    a.set_editor_property("frame_mirror", float(frame[1]))
    a.set_editor_property("frame_scale", float(frame[2]))
    kinds = {str(e.get_editor_property("name")).rsplit("_", 1)[0][len("SM_Prop_"):] for e in species}
    none = [k for k in spec["kinds"] if k not in kinds]
    return "RideProps: %d models (%s from Fab: %s), %d kinds with none (code-made): %s" % (
        len(species), len(found), ", ".join("%s %s" % (k, v[0].rsplit("/", 1)[-1]) for k, v in found.items()) or "none",
        len(none), ", ".join(none) or "none")


def build():
    L = level_lib()
    land_material(L)
    road_material(L)
    lamp_material(L)
    scanned = scanned_materials(L)
    kit = read_kit(L)
    grass_materials(L, kit)
    scanned_trees(L, kit)
    hedge_kit(L, kit)
    before = L.begin_fresh_level(MAP)
    L.sky_and_camera(reuse_cloud_shadow=True)
    # (the bike is put down at the ride's start by the game; a level still wants a start of its own)
    L.spawn(unreal.PlayerStart, (0, 0, 100000.0))
    place_kit(kit)
    log(animal_kit(L))
    log(prop_kit(L))
    L.save_fresh_level(MAP, before)
    log("built %s and %s/M_RideLand, M_RideRoad%s" % (MAP, MAT_DIR, "".join(", " + m for m in scanned)))


build()
