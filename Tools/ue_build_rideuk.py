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
    tone = L.noise(g.m, 0.002, 0.6, 0.95, -1200, g.y + 200)
    road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], tone)
    g.y += 140
    # Patches of a fresher, darker surface dressing and paler, older tarmac, tens of metres long.
    patch = g.mask_noise(0.0012, -2.0, 3.0)
    patch_tone = g.lerp(g.const(1.08), g.const(0.8), patch)
    road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], patch_tone)
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
    # [race tracks, build 168] A race circuit's track (World/WorldRide's RoadPiece: its vertex colour the key
    # (0.010, 0.010, 0.011)). beenster on 167: "make the tarmac on all tracks black and as smooth as silk, no
    # bumps at all": the tarmac's scan taken to grey and almost all its contrast out, near black, its normal flat
    # (no grain to catch the light), a faint satin sheen from fine aggregate; dark and glossy when wet as roads are.
    circuit_key = keyed((0.010, 0.010, 0.011))
    cgrey = g.node(unreal.MaterialExpressionDesaturation, -600)
    g.link(tar_scan[0], cgrey)
    g.link(g.const(1.0), cgrey, "Fraction")
    silk = [g.lerp(g.const((0.016, 0.016, 0.017)), g.op(unreal.MaterialExpressionMultiply, cgrey, const_b=0.45), g.const(0.12)),
            g.const((0.0, 0.0, 1.0)), g.const(0.62)]
    # [race tracks, build 169] Race-track character (beenster on 168: "the race track need a little more race track
    # character to the surface"), still black and smooth, the normal still flat: the racing line rubbered in, a
    # darker satin band where every lap's tyres run (out wide into each corner, the inside at its apex), streaked
    # along the way the bikes go; tyre marks in the braking zones before each corner, darker and heavier; and
    # patch repairs, crisp-edged squares of fresher or older asphalt. World/WorldRide's RoadPiece gives a circuit's
    # track UV1: x metres to the left of the racing line (World/WorldRideCircuits.cpp's CircuitLine; 1000 off any
    # line), y how hard riders brake there (0 .. 1); UV0 is metres across and along over 4.
    U = unreal
    def chan(src, r):
        mk = g.node(U.MaterialExpressionComponentMask, r=r, g=not r, b=False, a=False)
        g.link(src, mk)
        return mk
    def hump(x, w):   # 1 at 0 .. 0 by w either side, smooth: (1 - (x/w)^2)^2
        q = g.op(U.MaterialExpressionDivide, x, const_b=w)
        sq = g.op(U.MaterialExpressionSaturate, g.op(U.MaterialExpressionOneMinus, g.op(U.MaterialExpressionMultiply, q, q)))
        return g.op(U.MaterialExpressionMultiply, sq, sq)
    def streaks(across, along, lo, hi):   # noise stretched along the track (UV0 is metres over 4)
        uv0 = g.node(U.MaterialExpressionTextureCoordinate)
        pos = g.op(U.MaterialExpressionAppendVector,
                   g.op(U.MaterialExpressionAppendVector, g.op(U.MaterialExpressionMultiply, chan(uv0, True), const_b=4.0 * across),
                        g.op(U.MaterialExpressionMultiply, chan(uv0, False), const_b=4.0 * along)), g.const(0.0))
        n = g.node(U.MaterialExpressionNoise, scale=1.0, output_min=lo, output_max=hi, levels=3)
        g.link(pos, n, "Position")
        return g.op(U.MaterialExpressionSaturate, n)
    uv1 = g.node(U.MaterialExpressionTextureCoordinate, coordinate_index=1)
    off_line = chan(uv1, True)
    brake_zone = g.op(U.MaterialExpressionSaturate, chan(uv1, False))
    # (the rubbered band: a 2.2 m core and a fainter haze out to 2.6 m either side, streaked along)
    rubber = g.op(U.MaterialExpressionSaturate, g.op(U.MaterialExpressionAdd, hump(off_line, 1.1),
                  g.op(U.MaterialExpressionMultiply, hump(off_line, 2.6), const_b=0.4)))
    rubber = g.op(U.MaterialExpressionMultiply, rubber, g.lerp(g.const(0.6), g.const(1.0), streaks(9.0, 0.08, -0.4, 1.4)))
    # (the braking marks: thin black streaks either side of the line, heaviest just before the turn in)
    marks = g.op(U.MaterialExpressionMultiply, g.op(U.MaterialExpressionMultiply, brake_zone, hump(off_line, 1.5)),
                 streaks(16.0, 0.05, -2.0, 2.0))
    worn = g.op(U.MaterialExpressionSaturate, g.op(U.MaterialExpressionAdd, g.op(U.MaterialExpressionMultiply, rubber, const_b=0.65),
                g.op(U.MaterialExpressionMultiply, marks, const_b=0.85)))
    # (patch repairs: crisp-edged patches tens of metres apart, a few fresher and blacker, a few older and greyer)
    fresh = g.op(U.MaterialExpressionSaturate, g.op(U.MaterialExpressionMultiply,
                 g.op(U.MaterialExpressionSubtract, g.mask_noise(0.0035, 0.0, 1.0), const_b=0.74), const_b=40.0))
    old = g.op(U.MaterialExpressionSaturate, g.op(U.MaterialExpressionMultiply,
               g.op(U.MaterialExpressionSubtract, g.mask_noise(0.0026, 0.0, 1.0), const_b=0.78), const_b=40.0))
    patched = g.op(U.MaterialExpressionMultiply, g.lerp(g.const(1.0), g.const(0.8), fresh), g.lerp(g.const(1.0), g.const(1.22), old))
    silk[0] = g.op(U.MaterialExpressionMultiply, g.op(U.MaterialExpressionMultiply, silk[0], patched),
                   g.lerp(g.const(1.12), g.const(0.38), worn))   # (off the line a touch greyer, dusty)
    # (rubber has a satin sheen, a fresh patch a little; an old one is drier)
    silk[2] = g.lerp(g.lerp(g.lerp(g.const(0.62), g.const(0.55), fresh), g.const(0.7), old), g.const(0.5), rubber)
    own = g.mix(own, silk, circuit_key)
    # [race tracks, build 168] The circuits' gravel traps, as sand (beenster on 167: "Make the gravel traps look like
    # sand"): World/WorldRideCircuits.cpp's CircuitSandTile lays each trap's ground over in the key (0.205, 0.152,
    # 0.083); the shore's sand scan, raked pale and dry (a circuit's trap is kept loose and clean), coarse grained.
    sand_role = "sand" if "sand" in tex else ("gravel" if "gravel" in tex else None)
    if sand_role:
        trap = g.layer(tex[sand_role], roles[sand_role]["tile_m"] * 0.8)
        tsand = g.node(unreal.MaterialExpressionDesaturation, -600)
        g.link(trap[0], tsand)
        g.link(g.const(0.35), tsand, "Fraction")
        trap = [g.op(unreal.MaterialExpressionMultiply, tsand, g.const((0.95, 0.88, 0.72) if sand_role == "sand" else (1.9, 1.7, 1.25))),
                trap[1], g.const(0.92)]
        own = g.mix(own, trap, keyed((0.205, 0.152, 0.083)))
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
        lane_tar = list(road)
        road = g.mix(own, surface, near)
        hard = g.op(unreal.MaterialExpressionMultiply, near, g.op(unreal.MaterialExpressionOneMinus, stones))   # (the tarmac itself, for the wet)
        # [build 168] The grit up a lane's middle (beenster on 167: "make gravel on the centre of the road more
        # sparse, better colour matched to the road (it's made up of stones from the road itself), improve the
        # blending, no edges"). Its own key now (World/WorldRide's LaneGrit: tarmac at the band's edges to
        # (0.06, 0.062, 0.085) at its core, off the gravel tracks' line, so their approved look is unchanged).
        # The stones are the gravel scan's shapes and shading in the tarmac's own colour: the asphalt scan's
        # average (its smallest mip, so one tiny cached sample) graded exactly as the tarmac is, by the same
        # slow tone and patch nodes. Only the tallest stones show, fewer and fewer from the core out, and in
        # loose clumps along the lane (the gravel's metre-wide wobble, reused); at the band's edges none at
        # all, so the tarmac runs on unbroken under it. Cloud preview: rideuk/lane_grit_168/.
        lane_c = (0.06, 0.062, 0.085)
        lspan = tuple(b - a for a, b in zip(tar_c, lane_c))
        lrel = g.op(unreal.MaterialExpressionSubtract, vc, g.const(tar_c))
        ldot = g.node(unreal.MaterialExpressionDotProduct, -600)
        g.link(lrel, ldot, "A")
        g.link(g.const(lspan), ldot, "B")
        lalong = g.op(unreal.MaterialExpressionMultiply, ldot, const_b=1.0 / sum(v * v for v in lspan))
        lon = g.op(unreal.MaterialExpressionAdd, g.const(tar_c), g.op(unreal.MaterialExpressionMultiply, g.const(lspan), lalong))
        loff = g.node(unreal.MaterialExpressionDistance, -600)
        g.link(vc, loff, "A")
        g.link(lon, loff, "B")
        # (a tighter key than the gravel's: the older patches' grey (0.038, 0.037, 0.036) and the covers'
        # lie 0.006 and 0.013 off this line; 8-bit rounding puts the band's own colours under 0.003 off)
        lnear = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                     g.op(unreal.MaterialExpressionSubtract, g.const(0.0065), loff), const_b=400.0))
        lends = g.op(unreal.MaterialExpressionMultiply,
                     g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionAdd, lalong, const_b=0.15), const_b=8.0)),
                     g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, g.const(1.15), lalong), const_b=8.0)))
        lnear = g.op(unreal.MaterialExpressionMultiply, lnear, lends)
        # (how thick the grit lies: none at the band's edges, eased in to the core)
        dens = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionSubtract,
                    g.op(unreal.MaterialExpressionMultiply, lalong, const_b=1.15), const_b=0.1))
        dens = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, dens, dens),
                    g.op(unreal.MaterialExpressionSubtract, g.const(3.0), g.op(unreal.MaterialExpressionMultiply, dens, const_b=2.0)))
        clump = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMax,
                     g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, wobble, const_b=1.2), const_b=0.6), g.const(0.25)))
        cover = g.op(unreal.MaterialExpressionMultiply, dens, clump)
        # (the height a stone must stand to show: at the core about one in six of the scan's stones,
        # only the odd tallest towards the edges)
        thr = g.op(unreal.MaterialExpressionSubtract, g.const(0.95), g.op(unreal.MaterialExpressionMultiply, cover, const_b=0.95))
        chips = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                     g.op(unreal.MaterialExpressionSubtract, height, thr), const_b=8.0))
        mean = g.node(unreal.MaterialExpressionTextureSample, -900, texture=tex["asphalt"]["color"],
                      sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                      sampler_source=unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        g.link(g.world_uv(roles["asphalt"]["tile_m"]), mean, "UVs")
        try:
            mean.set_editor_property("mip_value_mode", unreal.TextureMipValueMode.TMVM_MIP_LEVEL)
            mean.set_editor_property("const_mip_value", 14)   # (past the last mip: the scan's 1x1 average)
        except Exception as e:
            unreal.log_warning("[Torque] lane grit: no mip level on the tarmac's average (%s)" % e)
        rmean = mean
        if "ao" in tex["asphalt"]:
            rmean = g.op(unreal.MaterialExpressionMultiply, rmean, const_b=0.92)   # (the tarmac's colour carries its AO)
        rdull = g.node(unreal.MaterialExpressionDesaturation, -600)
        g.link(rmean, rdull)
        g.link(g.const(0.9), rdull, "Fraction")
        rtone = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply,
                     g.op(unreal.MaterialExpressionMultiply, rdull, g.const((0.96, 0.99, 1.06))), tone), patch_tone)
        chip_c = g.op(unreal.MaterialExpressionMultiply, rtone,
                      g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionAdd, height, const_b=1.0), const_b=1.12))
        lane = g.mix(lane_tar, [chip_c, grav[1], grav[2]], chips)
        road = g.mix(road, lane, lnear)
        hard = g.op(unreal.MaterialExpressionMax, hard,
                    g.op(unreal.MaterialExpressionMultiply, lnear, g.op(unreal.MaterialExpressionOneMinus, chips)))
        near = g.op(unreal.MaterialExpressionMax, near, lnear)
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
    whard = g.op(unreal.MaterialExpressionMax, hard, circuit_key)   # ([race tracks] a circuit's tarmac wets as a road's)
    wnear = g.op(unreal.MaterialExpressionMax, near, circuit_key)
    road[0] = g.op(unreal.MaterialExpressionMultiply, road[0], g.lerp(g.const(1.0), g.lerp(g.const(0.75), g.lerp(g.const(0.6), g.const(0.5), whard), wnear), wet))
    road[2] = g.op(unreal.MaterialExpressionMax, road[2], g.lerp(g.const(0.6), g.lerp(g.const(0.6), g.const(0.12), whard), wet))
    # [weather, Nov 18] Standing water, as Buttertubs' M_Road (beenster on 156: "I am not seeing any puddles (we
    # had them on buttertubs)"). The camber drains rain to the edges, most on a bend's inside; only after long
    # rain does it reach across the middle; few puddles, now and then a bigger one. The road mesh's vertex
    # alpha says where water stands (World/WorldRide's RoadPiece StandsAt): 0 the crown .. 0.9 the edge of a
    # bend's inside half (0.72 a straight's, 0.45 an outside's); 1 is no data (other meshes in this material:
    # no puddles). Only on the tarmac itself, never the paving, setts, gravel or verge.
    va = g.node(unreal.MaterialExpressionVertexColor)
    known = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                 g.op(unreal.MaterialExpressionSubtract, g.const(0.97), va, b_out="A"), const_b=40.0))     # 1 up to 0.945 .. 0 by 0.97
    share = g.op(unreal.MaterialExpressionMultiply, va, const_b=1.0 / 0.9, a_out="A")                    # 0 crown .. 1 an inside edge
    p_edge = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                  g.op(unreal.MaterialExpressionSubtract, share, const_b=0.5), const_b=2.6))
    big = g.mask_noise(0.0009, -3.0, 1.4)
    p_wide = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                  g.op(unreal.MaterialExpressionSubtract, share, const_b=0.25), const_b=1.6))
    p_edge = g.lerp(p_edge, p_wide, big)
    pud = L.weather_param(g, "Puddles")
    fill = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply, pud, const_b=2.5))
    where = g.op(unreal.MaterialExpressionMultiply, p_edge, fill)
    centre = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMultiply,
                  g.op(unreal.MaterialExpressionSubtract, pud, const_b=0.6), const_b=2.5))
    where = g.op(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionMax, where, centre))
    small = g.mask_noise(0.006, -7.0, 2.0)
    large = g.op(unreal.MaterialExpressionMultiply, g.mask_noise(0.0022, -2.5, 2.2), big)
    patches = g.op(unreal.MaterialExpressionMax, small, large)
    puddle = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, patches, where), wet)
    puddle = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, puddle, hard), known)
    road[0] = g.lerp(road[0], g.op(unreal.MaterialExpressionMultiply, road[0], const_b=0.6), puddle)
    road[2] = g.lerp(road[2], g.const(0.03), puddle)
    road[1] = g.lerp(road[1], g.const((0.0, 0.0, 1.0)), puddle)   # still water is flat
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


def water_texture(L, name, asset, kind="data"):
    """[real water] Data/Textures/Water/<name>.png (Tools/make_water_normal.py, Tools/make_water_fx.py) as
    /Game/RideUK/Textures/<asset>, linear: kind "normal" a normal map, "data" kept uncompressed (the rain's
    drop data, the foam; sampled as linear colour). None if it isn't there."""
    f = os.path.join(os.path.dirname(L.data_dir()), "Textures", "Water", name + ".png")
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
        comp = {"normal": unreal.TextureCompressionSettings.TC_NORMALMAP,
                "data": unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP}[kind]
        tex.set_editor_property("compression_settings", comp)
        tex.set_editor_property("srgb", False)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


def water_normal(L):
    """[build 154] Data/Textures/Water/normal.png (Tools/make_water_normal.py), a tiling normal map of wind
    ripples 8 m across, as /Game/RideUK/Textures/T_WaterNormal; None if it isn't there."""
    return water_texture(L, "normal", "T_WaterNormal", "normal")


def water_material(L, real=False):
    """The ride's water (Ride UK stage 5: World/WorldRideScenery's WaterTile): the sea, lakes and
    rivers, their colour the vertex colour (dark: deep sea blue-grey, peaty lake, brown river), glossy,
    with ripples drifting across it (UV0 is the world's metres over 8, whole tens of units from one square
    to the next, so every sample's tiling times 10 is a whole number). Opaque: open water at a distance
    shows the sky in it, not its bed.
    [build 154] beenster: "can we move to a proper water animation, as it does look like a lot of static
    tiles for the surface". Now the wave normal map (water_normal) three times over: long wind waves (27 m
    across) and two sizes of ripple on them, each drifting its own way at its own pace, so the surface
    moves all the time and no pattern repeats; the ripples fade with distance (where they would only
    shimmer) and are stronger and weaker in broad patches, as wind gusts make them.
    [build 156] beenster: "let's do the single layer water material route and put a toggle in the settings".
    real=True makes M_RideWaterReal: the same surface in Unreal's single layer water, which you see into:
    the light scattered and absorbed in the water as it deepens (sea blue-green and clear, a lake or river
    peaty brown, told apart by the vertex colour), the bed refracted under it. World/TorqueRideWorld draws
    it while Settings > Graphics > Realistic water is on.
    [real water, Nov 21 2026] beenster: "make the water finally look like water ... ALL watercourses, lakes,
    lochs, the sea, all coastlines, banks". World/WorldRideScenery's WaterTile now gives each point UV1:
    u its depth (m, under 0 beneath the bank), v the sea's tidal half range or a river's pace (m/s), and
    the alpha white water. So: foam where the water meets the land (gentle lapping on lochs, surf running
    up and back on sea beaches, stronger with the wind), whitecaps in a gale, white water where a river
    falls fast; rivers' ripples flowing downstream at their own pace (UV0 runs down them); the sea and
    lakes calm to a mirror in still air and choppy in wind (MPC_Weather Wind, WindX, WindY); rain rings
    on the water (Rain); swell that moves the surface itself near the rider (WaterWaves, off for an A/B);
    and the sea lifted and lowered by the tide (Tide times its half range, MPC_Weather from the ride's
    date and clock)."""
    g = L.Graph(L.new_material("M_RideWaterReal" if real else "M_RideWater"))
    U = unreal
    vc = g.node(U.MaterialExpressionVertexColor)
    wn = water_normal(L)
    nt = L.noise_texture()
    foam_t = water_texture(L, "foam", "T_WaterFoam", "data")
    rain_t = water_texture(L, "rain", "T_WaterRain", "data")

    def num(v):
        return g.const(float(v)) if isinstance(v, (int, float)) else v

    def mul(a, b):
        return g.op(U.MaterialExpressionMultiply, a, const_b=float(b)) if isinstance(b, (int, float)) else g.op(U.MaterialExpressionMultiply, a, b)

    def add(a, b):
        return g.op(U.MaterialExpressionAdd, a, const_b=float(b)) if isinstance(b, (int, float)) else g.op(U.MaterialExpressionAdd, a, b)

    def sub(a, b):
        return g.op(U.MaterialExpressionSubtract, num(a), const_b=float(b)) if isinstance(b, (int, float)) else g.op(U.MaterialExpressionSubtract, num(a), b)

    def div(a, b):
        return g.op(U.MaterialExpressionDivide, a, const_b=float(b))

    def sat(a):
        return g.op(U.MaterialExpressionSaturate, a)

    def chan(src, c):
        # (a vertex colour's or texture sample's default pin is RGB, a float3: a mask of its A fails to
        # compile (round 167), so take each channel from its own pin and mask that scalar's R)
        if isinstance(src, (U.MaterialExpressionVertexColor, U.MaterialExpressionTextureSample)):
            e = g.node(U.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False)
            g.link(src, e, "", c)
            return e
        e = g.node(U.MaterialExpressionComponentMask, r=c == "R", g=c == "G", b=c == "B", a=c == "A")
        g.link(src, e)
        return e

    def pair(src, a="R", b="G"):
        if "A" in (a, b) and isinstance(src, (U.MaterialExpressionVertexColor, U.MaterialExpressionTextureSample)):
            return g.op(U.MaterialExpressionAppendVector, chan(src, a), chan(src, b))
        e = g.node(U.MaterialExpressionComponentMask, r="R" in (a, b), g="G" in (a, b), b="B" in (a, b), a="A" in (a, b))
        g.link(src, e)
        return e

    def append(a, b):
        return g.op(U.MaterialExpressionAppendVector, num(a), num(b))

    def sine(a):
        return g.op(U.MaterialExpressionSine, a, period=6.2831853)

    def cosine(a):
        return g.op(U.MaterialExpressionCosine, a, period=6.2831853)

    param = lambda name: L.weather_param(g, name)
    time = g.node(U.MaterialExpressionTime)
    wind, wind_x, wind_y = param("Wind"), param("WindX"), param("WindY")
    uv1 = g.node(U.MaterialExpressionTextureCoordinate, coordinate_index=1)
    depth, second = chan(uv1, "R"), chan(uv1, "G")
    white = chan(vc, "A")
    red = chan(vc, "R")
    # (which water: the vertex colour's red, as bytes: the sea 6, a lake or canal 7, a river 9 of 255)
    is_sea = sat(mul(sub(0.0255, red), 600.0))
    is_river = sat(mul(sub(red, 0.031), 400.0))
    is_lake = sat(sub(sub(1.0, is_sea), is_river))
    # (the tide: the sea's water lifted by Tide times its half range there; its depth with it)
    tide_m = mul(mul(param("Tide"), second), is_sea)
    deep = add(depth, tide_m)
    # (0 in still air .. 1 in a strong wind; the usual breeze about 0.6)
    calm = sat(div(wind, 1.6))

    # The sea's and lakes' own ripples (as before), stronger in wind, a mirror in still air.
    n = None
    rough = None
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
        mid_uv = turned(1.3, 12.0 / 13.0, -5.0 / 13.0)     # 6 m, turned -23 degrees
        small_uv = turned(2.9, 20.0 / 29.0, 21.0 / 29.0)   # 2.8 m, turned 46 degrees
        if nt is not None:
            # [real water, beenster on 166: "I could see the tiling"] (the ripples' rows bent too, by the same
            # noise read as the swell's, its broad channel only so they bend and never swirl, each its own
            # way by a repeat or more: the 6 m and 2.8 m patterns wander instead of lying in a grid; no extra reads)
            broad = g.node(unreal.MaterialExpressionComponentMask, r=False, g=True, b=False, a=False); g.link(warp, broad)
            mid_uv = g.op(unreal.MaterialExpressionAdd, mid_uv, g.op(unreal.MaterialExpressionMultiply, broad, const_b=1.0))
            small_uv = g.op(unreal.MaterialExpressionSubtract, small_uv, g.op(unreal.MaterialExpressionMultiply, broad, const_b=1.6))
        mid = ripple(mid_uv, -0.021, 0.034, 0.6)
        small = ripple(small_uv, 0.055, -0.04, 0.5)
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
        rough = g.lerp(g.const(0.11), g.const(0.04), swell_near)
    if n is None:
        n = g.const((0.0, 0.0, 0.0))
        rough = g.const(0.05)
    still = g.lerp(g.const(0.1), g.const(1.35), calm)
    n = g.op(U.MaterialExpressionMultiply, n, g.lerp(still, mul(still, 0.7), is_lake))
    uv0 = g.node(U.MaterialExpressionTextureCoordinate)
    if wn is not None:
        # A river's ripples run down it at its pace (UV0 is metres across it and down it over 8).
        pace = mul(second, 0.125)
        def flowing(tiling, speed, side):
            uv = g.op(U.MaterialExpressionMultiply, uv0, const_b=float(tiling))
            drift = append(mul(time, side), mul(mul(time, pace), -tiling * speed))
            return g.sample(wn, g.op(U.MaterialExpressionAdd, uv, drift), "normal")
        # ([real water, beenster on 170: "we never seem to get rid of the tiling on the water surface"] the
        # river's two samples ran square down it at near-matching sizes, unbent, as strong everywhere and to
        # any distance: a grid of the same cells down every river. Now a third, broad sample at 11 m turned
        # 30 degrees off the flow, all three bent by the slow noise (its broad channel, so they wander and
        # never swirl), the ripples stronger and calmer in slow patches as a river's surface is (smooth
        # glides between rougher runs), and fading to a third by 300 m, where any repeat would show.)
        def flowing_at(tiling, speed, side, bend):
            uv = g.op(U.MaterialExpressionMultiply, uv0, const_b=float(tiling))
            if bend is not None:
                uv = g.op(U.MaterialExpressionAdd, uv, bend)
            drift = append(mul(time, side), mul(mul(time, pace), -tiling * speed))
            return g.sample(wn, g.op(U.MaterialExpressionAdd, uv, drift), "normal")
        bend_a = bend_b = None
        if nt is not None:
            rwuv = g.node(U.MaterialExpressionTextureCoordinate, u_tiling=0.05, v_tiling=0.05)
            rbroad = chan(g.sample(nt, rwuv, "linear"), "G")
            bend_a = mul(rbroad, 1.3)
            bend_b = mul(rbroad, -2.1)
        layer_a = flowing_at(1.6, 1.0, 0.004, bend_a)
        layer_b = flowing_at(3.1, 1.15, -0.011, bend_b)
        # (the broad one, UV0 turned 30 degrees: cos 0.866, sin 0.5)
        tu = g.node(U.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False); g.link(uv0, tu)
        tv = g.node(U.MaterialExpressionComponentMask, r=False, g=True, b=False, a=False); g.link(uv0, tv)
        turned_uv = append(sub(mul(tu, 0.866 * 0.73), mul(tv, 0.5 * 0.73)), add(mul(tu, 0.5 * 0.73), mul(tv, 0.866 * 0.73)))
        if bend_a is not None:
            turned_uv = g.op(U.MaterialExpressionAdd, turned_uv, mul(rbroad, 0.7))
        layer_c = g.sample(wn, g.op(U.MaterialExpressionAdd, turned_uv, append(mul(time, 0.003), mul(mul(time, pace), -0.73 * 0.8))), "normal")
        river = g.op(U.MaterialExpressionAdd, g.op(U.MaterialExpressionAdd, mul(layer_a, 0.7), mul(layer_b, 0.45)), mul(layer_c, 0.5))
        strength = add(mul(white, 0.9), 0.32)   # (calmer than before: a slow river's glides are near mirrors)
        if nt is not None:
            # (glides and runs: 0.35 .. 1.25 in patches 40 m or so long, drifting down with the water)
            pguv = g.op(U.MaterialExpressionAdd, mul(uv0, 0.1), append(0.0, mul(mul(time, pace), -0.1)))
            patch = chan(g.sample(nt, pguv, "linear"), "G")
            strength = mul(strength, add(mul(patch, 0.9), 0.35))
            far = g.op(U.MaterialExpressionSaturate, g.op(U.MaterialExpressionDivide,
                       g.op(U.MaterialExpressionSubtract, g.node(U.MaterialExpressionPixelDepth), const_b=2000.0), const_b=28000.0))
            strength = mul(strength, g.lerp(g.const(1.0), g.const(0.33), far))
        river = g.op(U.MaterialExpressionMultiply, river, append(append(strength, strength), 0.0))
        n = g.lerp(n, river, is_river)
    if rain_t is not None:
        # Rain: a ring grows out from each drop in turn and fades (the texture: distance, when, which way).
        drop = g.sample(rain_t, g.world_uv(1.0), "linear")
        t = g.op(U.MaterialExpressionFrac, add(mul(time, 0.9), chan(drop, "G")))
        d = sub(chan(drop, "R"), t)
        ring = mul(sat(sub(1.0, mul(g.op(U.MaterialExpressionAbs, d), 7.0))), sub(1.0, t))
        ring = mul(mul(ring, sine(mul(d, 40.0))), mul(param("Rain"), 0.9))
        way = mul(sub(pair(drop, "B", "A"), 0.5), 2.0)
        n = g.op(U.MaterialExpressionAdd, n, append(mul(way, ring), 0.0))

    # Waves that move the surface near the rider: two trains with the wind (28 m and 13 m), the sea's up to
    # 0.45 m in a gale, a lake's a few cm, smaller in the shallows, gone past 500 m (MPC_Weather WaterWaves).
    wp = g.node(U.MaterialExpressionWorldPosition)
    xy = div(pair(wp, "R", "G"), 100.0)
    dir1 = append(wind_x, wind_y)
    dir2 = append(add(mul(wind_x, 0.82), mul(wind_y, -0.57)), add(mul(wind_x, 0.57), mul(wind_y, 0.82)))
    ph1 = sub(mul(g.op(U.MaterialExpressionDotProduct, xy, dir1), 0.224), mul(time, 1.48))
    ph2 = sub(mul(g.op(U.MaterialExpressionDotProduct, xy, dir2), 0.483), mul(time, 2.18))
    cam = g.node(U.MaterialExpressionCameraPositionWS)
    dist = g.op(U.MaterialExpressionDistance, wp, cam)
    near_waves = sat(sub(1.0, div(sub(dist, 30000.0), 20000.0)))
    amp = add(mul(is_sea, 0.45), mul(is_lake, 0.05))
    amp = mul(mul(mul(amp, mul(calm, calm)), sat(div(deep, 3.0))), mul(near_waves, param("WaterWaves")))
    swell = mul(amp, add(sine(ph1), mul(sine(ph2), 0.45)))
    slope = g.op(U.MaterialExpressionAdd, mul(mul(dir1, cosine(ph1)), 0.224), mul(mul(dir2, cosine(ph2)), 0.217))
    n = g.op(U.MaterialExpressionSubtract, n, append(mul(slope, amp), 0.0))
    normal = g.node(U.MaterialExpressionNormalize)
    g.link(g.op(U.MaterialExpressionAdd, n, g.const((0.0, 0.0, 1.0))), normal)
    lift = mul(add(swell, tide_m), 100.0)
    wpo = append(g.node(U.MaterialExpressionConstant2Vector, r=0.0, g=0.0), lift)

    # Foam: along the shore, the sea's surf running in, lapping at a loch's edge, whitecaps, white water.
    # (a narrower band on rivers: they are shallow right across, and a 0.55 m band would whiten the lot)
    shore = mul(sat(sub(1.0, g.op(U.MaterialExpressionDivide, deep, g.lerp(g.const(0.55), g.const(0.12), is_river)))), sat(add(mul(deep, 20.0), 1.0)))
    wobble = g.const(0.0)
    if nt is not None:
        nuv = g.node(U.MaterialExpressionTextureCoordinate, u_tiling=0.05, v_tiling=0.05)
        wobble = chan(g.sample(nt, nuv, "linear"), "G")
    surf = sat(sub(mul(sine(add(sub(mul(deep, 4.5), mul(time, 1.2)), mul(wobble, 6.0))), 2.5), 1.5))
    surf = mul(mul(mul(surf, sat(sub(1.0, div(deep, 2.5)))), sat(mul(deep, 3.0))), mul(is_sea, g.lerp(g.const(0.35), g.const(1.0), calm)))
    lap = sat(sub(mul(sine(sub(mul(deep, 30.0), mul(time, 2.2))), 2.0), 1.0))
    lap = mul(mul(lap, sat(sub(1.0, div(deep, 0.3)))), mul(is_lake, 0.6))
    caps = mul(mul(is_sea, sat(mul(sub(wind, 1.4), 0.9))), sat(sub(mul(wobble, 4.0), 2.6)))
    # ([real water, beenster on 170 at Aysgarth: "is this realistic water?", the calm Ure blotched white along
    # its banks] a slow river has no foam at its edge, only where it falls: a faint scum line at most, and the
    # white water by its fall alone)
    rapids = mul(white, is_river)
    amount = g.op(U.MaterialExpressionMax, mul(shore, g.lerp(g.const(0.85), g.const(0.08), is_river)), surf)
    amount = g.op(U.MaterialExpressionMax, amount, lap)
    amount = g.op(U.MaterialExpressionMax, amount, caps)
    amount = g.op(U.MaterialExpressionMax, amount, rapids)
    foam = amount
    if foam_t is not None:
        drift_uv = g.op(U.MaterialExpressionAdd, g.world_uv(4.0), mul(append(wind_x, wind_y), mul(time, 0.004)))
        river_uv = g.op(U.MaterialExpressionAdd, mul(uv0, 2.0), append(0.0, mul(mul(time, second), -0.25)))
        lace = chan(g.sample(foam_t, g.lerp(drift_uv, river_uv, is_river), "linear"), "R")
        foam = sat(mul(mul(amount, lace), 1.8))
    rough = g.op(U.MaterialExpressionMultiply, rough, g.lerp(g.const(0.4), g.const(1.3), calm))
    # ([171 at Aysgarth: the river blue-grey with the sky] a river's surface is never a clean mirror: its
    # ripples and drift blur the sky's reflection, which leaves more of the water's own brown)
    rough = g.lerp(rough, g.op(U.MaterialExpressionMax, rough, const_b=0.14), is_river)
    rough = g.lerp(rough, g.const(0.6), foam)
    foam_colour = g.const((0.82, 0.85, 0.86))
    # [real water, beenster on 166: "it never really looked like water"] (the plain water, opaque, can't be
    # seen into, so it is given what you'd see: its bed through the shallows, the sea's sand and a loch's or
    # a river's peaty stones, going to the water's own deep colour by about 2 m down (UV1's depth); a
    # little lighter where wind roughens it. Single layer water does this itself, by its absorption.)
    bed = g.lerp(g.const((0.10, 0.085, 0.06)), g.const((0.20, 0.18, 0.13)), is_sea)
    murk = sat(div(deep, 2.2))
    murk = mul(murk, sub(2.0, murk))   # (quickly at first: the bed fades fast in the first metre)
    water_c = g.lerp(bed, g.op(U.MaterialExpressionMultiply, vc, g.const(1.0)), murk)
    base = g.lerp(water_c, foam_colour, foam)
    mel = L.mel
    if real:
        # (round 168: the output's 4 pins read back linked, yet the log said "requires the use of
        # SingleLayerWaterMaterial output node" and "No inputs": the errors of the material compiled in
        # between, when the shading model was set before the output was made and linked. So the output
        # and its links come first and the shading model last, then one compile.)
        out = g.node(U.MaterialExpressionSingleLayerWaterMaterialOutput, 0)
        # (how brown the water is: the sea's colour is blue-grey, a lake's peaty, a river's brown, by the red)
        brown = g.op(U.MaterialExpressionSaturate, g.op(U.MaterialExpressionMultiply,
                     g.op(U.MaterialExpressionSubtract, vc, const_b=0.024, a_out="R"), const_b=80.0))
        # (per metre: the sea absorbs the red most, so it goes blue-green as it deepens; peaty water the
        # blue, so it goes brown; both scatter a little of their own colour back)
        # ([real water, beenster's 168 shot at Uig: the sea a Caribbean turquoise over pale sand] British
        # coastal water carries plankton and peat from the hills: it takes the blue nearly as fast as the
        # green, so it goes a grey green with depth, not the clear tropics' cyan)
        absorb = g.lerp(g.const((0.45, 0.15, 0.19)), g.const((0.32, 0.5, 1.1)), brown)
        scatter = g.op(U.MaterialExpressionMultiply, vc, const_b=0.9)
        # ([real water, beenster on 170 at Aysgarth] the Ure came out a deep saturated blue: it absorbed so
        # little that it was nearly clear glass, the summer sky all it showed. A Dales river is peat-stained:
        # tea-clear over its stones in the shallows, amber to brown as it deepens, a little of that brown
        # scattered back from the silt in it.)
        absorb = g.lerp(absorb, g.const((0.55, 1.1, 2.4)), is_river)
        # ([171 on the PC at Aysgarth: no trees, no repeat, but still "blue-grey, not clearly brown"] twice the
        # brown scattered back from the silt, so the body of the water outweighs the sky in it looking down)
        scatter = g.lerp(scatter, g.const((0.13, 0.09, 0.04)), is_river)
        pins = {"scatter": (scatter, ("ScatteringCoefficients", "Scattering Coefficients")),
                "absorb": (absorb, ("AbsorptionCoefficients", "Absorption Coefficients")),
                "phase": (g.const(0.15), ("PhaseG", "Phase G")),
                "behind": (g.const(1.0), ("ColorScaleBehindWater", "Color Scale Behind Water"))}
        # (round 167: the PC said "No inputs to Single Layer Water Material" though every link here said it
        # worked. So the pins' names come from the node itself, and the links are read back afterwards.)
        try:
            have = [str(n_) for n_ in mel.get_material_expression_input_names(out)]
        except Exception as e:
            have = []
            unreal.log_warning("[Torque] M_RideWaterReal: couldn't read the output's pin names (%s)" % e)
        unreal.log("[Torque] M_RideWaterReal: single layer water output pins %s" % have)
        squash = lambda t: t.lower().replace(" ", "").replace("_", "")
        for key, (node, names) in pins.items():
            tries = [n_ for n_ in have if squash(n_).startswith(squash(names[0]))] + list(names)
            if not any(mel.connect_material_expressions(node, "", out, n_) for n_ in tries):
                unreal.log_warning("[Torque] M_RideWaterReal: couldn't link its %s to the single layer water output" % key)
        try:
            got = [e_ for e_ in mel.get_inputs_for_material_expression(g.m, out) if e_ is not None]
            unreal.log("[Torque] M_RideWaterReal: %d of the output's %d pins linked" % (len(got), len(have)))
            if not got:
                unreal.log_error("[Torque] M_RideWaterReal: the single layer water output has NO inputs after linking")
        except Exception as e:
            unreal.log_warning("[Torque] M_RideWaterReal: couldn't read the output's links back (%s)" % e)
        # (single layer water: its own colour is the depth's; the base colour is the foam on top of it, laid
        # over the water by the opacity)
        base = foam_colour
        mel.connect_material_property(foam, "", U.MaterialProperty.MP_OPACITY)
        g.m.set_editor_property("blend_mode", U.BlendMode.BLEND_OPAQUE)
        g.m.set_editor_property("shading_model", U.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    mel.connect_material_property(base, "", U.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, "", U.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(g.const(0.5), "", U.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(normal, "", U.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(wpo, "", U.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(g.m)
    if real:
        # (did the final material compile? a failed one has no pixel shader instructions)
        try:
            st = mel.get_statistics(g.m)
            px = st.get_editor_property("num_pixel_shader_instructions")
            unreal.log("[Torque] M_RideWaterReal compiled: %s pixel shader instructions" % px)
            if not px:
                unreal.log_error("[Torque] M_RideWaterReal: NO pixel shader after the final compile")
        except Exception as e:
            unreal.log_warning("[Torque] M_RideWaterReal: couldn't read its statistics (%s)" % e)
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
    # (slate the glossiest. [lighting pass, build 170] beenster on 169, Hartsop overcast: the slate roofs read
    # pale lavender-blue, as bright as the fields: at roughness 0.62 and specular 0.4 they mirrored the grey
    # sky. Real dry slate is a dull dark grey-green.)
    rough = g.lerp(g.const(0.88), g.const(0.74), g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionOneMinus, to_stone), coursed))
    return g.finish([col, g.const((0.0, 0.0, 1.0)), rough], specular=0.3)


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
        caustics = water_texture(L, "caustics", "T_WaterCaustics", "data")   # [real water] (the bed under shallow water)
        for want, make in (("M_RideLandScan", lambda: L.land_material_scanned(tex, roles, shore={"caustics": caustics}) if "grass" in tex else None),
                           ("M_RideStone", lambda: L.stone_material(tex, roles, tint_param=True)),
                           ("M_RideMasonry", lambda: L.stone_material(tex, roles, "M_Masonry", "masonry", "masonry", broad_moss=False, tint_param=True)),
                           # (stage 5: rendered and limewashed houses, the vertex colour their wash)
                           ("M_RideRender", lambda: L.render_material(tex, roles, "M_RideRender")),
                           # (stage 5: the sea, lakes and rivers)
                           ("M_RideWater", lambda: water_material(L)),
                           # ([build 156] the same in single layer water: Settings > Graphics > Realistic water)
                           ("M_RideWaterReal", lambda: water_material(L, real=True)),
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


_IMPOSTOR_MATERIAL = []   # ([build 157] M_RideImpostor made once a run)


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

    def section_materials(skel):
        """[build 161] The skeletal mesh's materials in its LOD 0 SECTION order: copy_mesh_from_skeletal_mesh
        gives each triangle its section's index as material ID, not the material slot's. Until 160 the copy
        took skel.materials (slot order), so a tree whose sections aren't in slot order drew some parts with
        another part's material: round 160's spruce and pine were bare brown trunks near and far (their
        needles drawn with a bark or decoration material) while the broadleaf trees were right. None if
        Geometry Script can't say."""
        mats = [m.get_editor_property("material_interface") for m in skel.get_editor_property("materials")]
        names = [str(m.get_editor_property("material_slot_name")) for m in skel.get_editor_property("materials")]
        smap = skeletal_section_slots(skel)
        if smap is not None:
            return [(mats[k] if 0 <= k < len(mats) else None, names[k] if 0 <= k < len(names) else "Section%d" % n) for n, k in enumerate(smap)]
        f = gs("get_section_material_list_from_skeletal_mesh")
        if f is None:
            return None
        for args in ((skel, unreal.GeometryScriptMeshReadLOD()), (skel, 0), (skel,)):
            try:
                res = f(*args)
            except TypeError:
                continue
            except Exception as e:
                why.append("section materials: %s" % e)
                return None
            parts = list(res) if isinstance(res, (tuple, list)) else [res]
            mats = names = None
            for part in parts:
                try:
                    items = list(part)
                except Exception:
                    continue
                if items and all(isinstance(x, unreal.MaterialInterface) or x is None for x in items) and mats is None:
                    mats = items
                elif items and all(isinstance(x, (unreal.Name, str)) for x in items) and names is None:
                    names = [str(x) for x in items]
            if mats:
                return [(m, names[k] if names and k < len(names) else "Section%d" % k) for k, m in enumerate(mats)]
        return None

    def skeletal_section_slots(skel):
        """[build 162] LOD 0's material slot for each section, in section order, or None. UE 5.8's Python
        has no Geometry Script get_section_material_list_from_skeletal_mesh (round 161), so the skeletal
        mesh editor's own functions are tried (their home and names moved between releases), with the
        LOD's material map overriding a section's slot where it says so."""
        homes = [getattr(unreal, n, None) for n in ("SkeletalMeshEditorSubsystem", "EditorSkeletalMeshLibrary", "SkeletalMeshEditingLibrary")]
        objs = []
        for h in homes:
            if h is None:
                continue
            try:
                objs.append(unreal.get_editor_subsystem(h))
            except Exception:
                objs.append(h)
        count = None
        for o in objs:
            for fn in ("get_num_sections", "get_section_count", "get_lod_section_count"):
                f = getattr(o, fn, None)
                if f is None:
                    continue
                try:
                    count = int(f(skel, 0))
                    break
                except Exception:
                    continue
            if count is not None:
                break
        slots = None
        for o in objs:
            f = getattr(o, "get_lod_material_slot", None)
            if f is None or count is None:
                continue
            try:
                slots = [int(f(skel, 0, n)) for n in range(count)]
                break
            except Exception as e:
                why.append("get_lod_material_slot: %s" % e)
        if slots is None:
            log("scanned trees: %s: no way to read section slots (sections %s; functions: %s)" % (skel.get_name(), count,
                ", ".join(sorted(set(n for o in objs for n in dir(o) if "section" in n.lower() or "material" in n.lower())))[:600]))
            return None
        try:
            info = skel.get_editor_property("lod_info")
            remap = list(info[0].get_editor_property("lod_material_map")) if info else []
            slots = [int(remap[n]) if n < len(remap) and int(remap[n]) >= 0 else s0 for n, s0 in enumerate(slots)]
        except Exception:
            pass
        log("scanned trees: %s: LOD 0 sections -> material slots %s" % (skel.get_name(), slots))
        return slots

    def fix_slots(copy_path, skel_path):
        """The copy's material slots in the skeletal mesh's section order (see section_materials)."""
        try:
            sm = unreal.load_asset(copy_path.split(".")[0])
            skel = unreal.load_asset(skel_path.split(".")[0])
        except Exception as e:
            why.append("slots of %s: %s" % (copy_path.rsplit(".", 1)[-1], e))
            return False
        want = section_materials(skel)
        name = copy_path.rsplit(".", 1)[-1]
        if not want:
            log("scanned trees: %s: section materials unknown (no Geometry Script get_section_material_list_from_skeletal_mesh); slots left as they are" % name)
            return False
        have = sm.get_editor_property("static_materials")
        before = [(m.get_editor_property("material_interface").get_name() if m.get_editor_property("material_interface") else "None") for m in have]
        after = [(m.get_name() if m else "None") for m, _ in want]
        if len(after) != len(before):
            # ([round 162] a Megaplant reads as ONE section at LOD 0: cutting the slots to it left every
            # tree bark only. Never change the number of slots.)
            log("scanned trees: %s: %d sections but %d slots: slots left as they are" % (name, len(after), len(before)))
            return False
        if before == after:
            log("scanned trees: %s: material slots already in section order (%s)" % (name, ", ".join(after)))
            return False
        sm.set_editor_property("static_materials", [unreal.StaticMaterial(material_interface=m, material_slot_name=n) for m, n in want])
        unreal.EditorAssetLibrary.save_asset(copy_path.split(".")[0], only_if_is_dirty=False)
        log("scanned trees: %s: material slots put in section order: was [%s], now [%s]" % (name, ", ".join(before), ", ".join(after)))
        return True

    def plain_copy(copy_path):
        """A copy match_nanite gave Nanite Foliage (round 160) put back as it was made: Nanite off."""
        try:
            sm = unreal.load_asset(copy_path.split(".")[0])
            ns = sm.get_editor_property("nanite_settings")
            shape = str(ns.get_editor_property("shape_preservation")) if "shape_preservation" in dir(ns) else ""
        except Exception as e:
            why.append("nanite settings of %s: %s" % (copy_path.rsplit(".", 1)[-1], e))
            return False
        if "VOXEL" not in shape.upper():
            return False
        changed = []
        for name, value in (("enabled", False), ("voxel_ndf", False)):
            try:
                ns.set_editor_property(name, value)
                changed.append("%s %s" % (name, value))
            except Exception:
                pass
        enum = getattr(unreal, "NaniteShapePreservation", None)
        none = getattr(enum, "NONE", None) if enum else None
        if none is not None:
            try:
                ns.set_editor_property("shape_preservation", none)
                changed.append("shape_preservation NONE")
            except Exception:
                pass
        sm.set_editor_property("nanite_settings", ns)
        unreal.EditorAssetLibrary.save_asset(copy_path.split(".")[0], only_if_is_dirty=False)
        log("scanned trees: %s: Nanite Foliage undone (%s)" % (copy_path.rsplit(".", 1)[-1], ", ".join(changed)))
        return True

    def match_nanite(copy_path, skel_path):
        """[build 160] The copy's Nanite settings as the Megaplant's own. A Megaplant's skeletal mesh is Nanite
        Foliage (round 159 scan check: shape_preservation Voxelize, voxel_ndf on); its static copy was made
        with plain Nanite, whose simplification wears thin leaf and needle cards away: the near trees looked
        "young, no bulk" and the far trees' impostors photographed as bare trunks (3-8% covered)."""
        try:
            sm = unreal.load_asset(copy_path.split(".")[0])
            skel = unreal.load_asset(skel_path.split(".")[0])
            theirs = skel.get_editor_property("nanite_settings")
            ours = sm.get_editor_property("nanite_settings")
        except Exception as e:
            why.append("nanite settings of %s: %s" % (copy_path.rsplit(".", 1)[-1], e))
            return False
        changed = []
        for name in sorted(dir(theirs)):
            if name.startswith("_"):
                continue
            try:
                want = theirs.get_editor_property(name)
                have = ours.get_editor_property(name)
            except Exception:
                continue
            if callable(want) or str(want) == str(have):
                continue
            try:
                ours.set_editor_property(name, want)
                changed.append("%s %s -> %s" % (name, have, want))
            except Exception:
                pass
        if not changed:
            return False
        sm.set_editor_property("nanite_settings", ours)   # (rebuilds its Nanite data, on the CPU)
        unreal.EditorAssetLibrary.save_asset(copy_path.split(".")[0], only_if_is_dirty=False)
        log("scanned trees: %s: Nanite settings as the Megaplant's: %s" % (copy_path.rsplit(".", 1)[-1], "; ".join(changed)))
        return True

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

    # [gardens] The villages' and towns' garden planting (World/WorldRideGardens.cpp): kinds garden_rose and so
    # on, each with its words and its own cull (cm) and shadow, near the rider only (no far models, no impostors).
    garden = spec.get("garden_kinds", {})
    def mature_first(paths):
        """([Nov 19] beenster, watching 157: "they all seem a little young, no bulk to the trees. No cover at
        all in the canopy". The first six by name were taken, young ones among them, and the game stretches
        each to the canopy map's height. Now the grown trees: names saying young, small or juvenile last,
        then the tallest as modelled first.)"""
        young = ("young", "small", "juvenile", "sapling", "seedling", "baby", "sprout")
        def tall(p):
            try:
                sk = unreal.load_asset(p.split(".")[0])
                b = sk.get_bounds() if hasattr(sk, "get_bounds") else sk.get_imported_bounds()
                return float(b.box_extent.z) * 2.0
            except Exception:
                return 0.0
        ranked = sorted(paths, key=lambda p: (any(w in p.lower() for w in young), -tall(p), p))
        if ranked:
            log("scanned trees: grown first: %s" % ", ".join("%s %.1f m" % (p.rsplit(".", 1)[-1], tall(p) / 100.0) for p in ranked[:8]))
        return ranked

    for kind, words in list(spec["kinds"].items()) + [(k, g.get("words", [])) for k, g in garden.items()]:
        keys = [norm(w) for w in words]
        wanted = lambda n: any(k in n for k in keys) and not any(w in n for w in skip)
        got = sorted(p for n, p in meshes if wanted(n))[:most]
        theirs = [(p, c) for n, p, c in others if wanted(n)]
        classes = sorted(set(c for _, c in theirs))
        log("scanned trees: %s: %d static meshes; other assets: %s" % (kind, len([1 for n, _ in meshes if wanted(n)]),
            ", ".join("%s x%d" % (c, len([1 for _, cc in theirs if cc == c])) for c in classes) or "none"))
        skels = mature_first(sorted(p for p, c in theirs if c == "SkeletalMesh"))[:most]
        if skels:
            # (Megaplants: the whole trees are skeletal meshes; the static ones are only leaves and buds)
            made = []
            for p in skels:
                m = static_of_skeletal(p)
                if m:
                    if spec.get("slots_by_section", False):
                        fix_slots(m, p)
                    # ([build 162] round 161: with the Megaplants' Voxelize Nanite the broadleaf trees turned
                    # to smooth green blobs at 30-45 m and the conifers stayed bare: back to the copies as
                    # they were until 159 (Nanite off), unless scanned_trees.json "match_nanite" is true)
                    if spec.get("match_nanite", False):
                        match_nanite(m, p)
                    else:
                        plain_copy(m)
                    made.append(m)
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
        # ([seasonal trees test, Nov 19] the leaves follow torque.RideTreeSeason: Tools/ue_tree_season.py)
        if spec.get("season_test", False):
            try:
                sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
                import ue_tree_season as SEASON
                SEASON.season_kind(L, kind, got)
            except Exception as e:
                log("scanned trees: %s: season test FAILED (leaves stay as they are): %s" % (kind, e))
        # (ours of this kind go near: the scanned ones take every near tree of it. Our far models stay
        # for the trees past the finest squares: build 148 measured the scans at about 10 ms of GPU at
        # Whaley Bridge, most of it from the far ring's thousands of full Nanite trees)
        rows[:] = [r for r in rows if not r["name"].startswith("SM_Tree_%s_" % kind)]
        r = dict(template)
        r.update({"name": "SM_Tree_%s_scan" % kind, "mesh": "", "material": "", "variants": got, "variant_materials": [],
                  "native_mesh": True, "scale_multiplier": 1.0, "keep": 1.0, "cull_spread": 1.0, "min_draw_distance": 0.0, "cast_shadow": True})
        rows.append(r)
        if kind in garden:
            r.update({"cull_distance": float(garden[kind].get("cull", 8000.0)), "cast_shadow": bool(garden[kind].get("shadow", True))})
            continue
        # [perf, build 157] Far off, the scans as impostors: the GAME photographs each scan at ride start
        # (World/TorqueImpostors.cpp) and draws its far trees with M_RideImpostor. Here only the material
        # is made (once per run, below); no editor captures (rounds 155-156 crashed beenster's GPU), and
        # the kit keeps the old far models for when impostors are off (torque.RideImpostors 0).
        if spec.get("impostors", True) and not _IMPOSTOR_MATERIAL:
            try:
                sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
                import ue_tree_impostors as IMP
                IMP.master_material(L if spec.get("season_test", False) else None)
                _IMPOSTOR_MATERIAL.append(True)
            except Exception as e:
                log("scanned trees: impostor material failed (the game uses the old far models): %s" % e)
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


GARDEN_DEST = "/Game/RideUK/Gardens"
# (each garden kind's cull, cm, and shadow, when scanned_trees.json doesn't say: as its scanned ones')
GARDEN_DEFAULTS = {"garden_rose": (9000.0, True), "garden_shrub": (9000.0, True), "garden_flowers": (4500.0, False),
                   "garden_low": (5000.0, False), "garden_tree": (25000.0, True), "garden_hedge": (9000.0, True)}


def garden_kit(L, kit):
    """[Nov 20] The villages' and towns' garden planting as our own models (beenster: "please consider building
    a small library of bushes, hedges for gardens"; the scanned Megaplants are off since Nov 20): Tools/
    make_gardens.py's roses, shrubs, flowers, low cover, small trees and clipped hedge lengths
    (Data/RideUK/Gardens/SM_Tree_garden_<kind>_<n>.glb) imported as Nanite foliage with the trees' material
    (M_Tree: they turn and drop with the season too), added to the kit's "Trees" as kinds garden_<kind>,
    each with its cull and shadow (scanned_trees.json "garden_kinds", else GARDEN_DEFAULTS). A kind that
    already has scanned models in the kit keeps them. Changes the kit in place."""
    if "Trees" not in kit:
        return
    rows = kit["Trees"]["species"]
    folder = os.path.join(os.path.dirname(L.data_dir()), "RideUK", "Gardens")
    if not os.path.isdir(folder) or not any(f.endswith(".glb") for f in os.listdir(folder)):
        log("gardens: none (python Tools/make_gardens.py); no garden planting")
        return
    spec_f = os.path.join(os.path.dirname(L.data_dir()), "RideUK", "scanned_trees.json")
    spec = (json.load(open(spec_f)).get("garden_kinds", {}) if os.path.exists(spec_f) else {})
    template = next((r for r in rows if r["name"].startswith("SM_Tree_hawthorn_")), None) or next((r for r in rows if r.get("material")), None)
    if template is None:
        log("gardens: no tree rows in the kit to copy; no garden planting")
        return
    scanned = set(r["name"][len("SM_Tree_"):-len("_scan")] for r in rows if r["name"].startswith("SM_Tree_garden_") and r["name"].endswith("_scan"))
    meshes = L.import_glbs(folder, GARDEN_DEST)
    added = []
    for name, mesh in sorted(meshes.items()):
        kind = next((k for k in GARDEN_DEFAULTS if name.startswith("SM_Tree_%s_" % k)), None)
        if kind is None or kind in scanned:
            continue
        L.foliage_nanite(mesh)
        try:
            unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).remove_collisions(mesh)
        except Exception:
            pass
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
        cull, shadow = GARDEN_DEFAULTS[kind]
        if kind in spec:
            cull = float(spec[kind].get("cull", cull)); shadow = bool(spec[kind].get("shadow", shadow))
        r = dict(template)
        r.update({"name": name, "mesh": mesh.get_path_name(), "variants": [], "variant_materials": [],
                  "native_mesh": False, "scale_multiplier": 1.0, "keep": 1.0, "cull_spread": 1.0, "min_draw_distance": 0.0,
                  "cast_shadow": shadow, "cull_distance": cull})
        rows.append(r)
        added.append(name)
    log("gardens: %d garden models (%s)" % (len(added), ", ".join(added[:6]) + (" ..." if len(added) > 6 else "")))


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
    shade = g.op(unreal.MaterialExpressionMultiply, vc, const_b=2.0)   # (vertex colour's unnamed pin is its RGB)
    own = above(0.83)
    col = g.lerp(g.op(unreal.MaterialExpressionMultiply, tint, shade), vc, own)
    rough = g.lerp(g.const(0.7), g.const(0.12), own)
    L.mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    L.mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    L.mel.recompile_material(g.m)
    unreal.EditorAssetLibrary.save_loaded_asset(g.m)
    return g.m


STREET_KIT_KINDS = ("street_lamp", "power_pole", "armco", "tyre_wall", "pit_wall", "marshal_post", "braking_board", "start_gantry", "grandstand",
                    "touring_caravan", "static_caravan", "post_box", "phone_box",
                    "farm_bales", "farm_bale", "farm_clamp", "farm_tank", "farm_feedbin", "farm_trough", "farm_tractor", "farm_trailer",
                    "farm_diesel", "farm_muck")   # ([everyday kits stage 4] Tools/blender_farm_props.py's, the same material)   # (Tools/blender_street_kit.py's models: M_RideStreetKit;
# [Nov 21] the post boxes and telephone kiosks are Tools/blender_post_phone_kit.py's, the same material)
# ([circuits] the race circuits' trackside kit, Tools/blender_circuit_kit.py, the same material)


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
    rg = g.node(E.MaterialExpressionComponentMask, r=True, g=True, b=False, a=False); g.link(n, rg, "", "RGBA")
    ba = g.node(E.MaterialExpressionComponentMask, r=False, g=False, b=True, a=True); g.link(n, ba, "", "RGBA")   # (the sample's default pin is RGB: no alpha)
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


CAR_DEST = "/Game/RideUK/Cars"


def parked_cars(L, kind, row, add):
    """[car parks] The car parks' parked cars (World/WorldRideCarParks.cpp, PropParkedCar): the Fab cars Thwaite's
    parked cars use (Data/Buttertubs/car_swaps.json; the packs live in /Content, never committed), each copied to
    /Game/RideUK/Cars/SM_ParkedCar_<n> in one of the kit row's colours (its body slots in MI_FabCarPaint_Park_<n>;
    a copy per colour, so each is one instanced mesh), Nanite on, no collision (the bike meets plain boxes,
    RidePlan::ParkedCarsNear), listed as SM_Prop_<kind>_<n> at their own size. The row's "models" names which of
    car_swaps' meshes (by their names' words: vans and pickups are too long for a bay). Returns a note."""
    cp = os.path.join(L.data_dir(), "car_swaps.json")
    if not os.path.exists(cp):
        return "parked cars: no car_swaps.json"
    cfg = json.load(open(cp))
    want = [w.lower() for w in row.get("models", [])]
    models = []
    for md in cfg.get("models", []):
        if want and not any(w in md["mesh"].lower() for w in want):
            continue
        m = L.find_static_mesh(md["mesh"])
        if m is not None:
            models.append((m, md))
    if not models:
        return "parked cars: none of the Fab cars are in the project (car parks have none)"
    colours = row.get("colours") or cfg.get("colours") or [[0.5, 0.5, 0.5]]
    count = int(row.get("variants", 12))
    if unreal.EditorAssetLibrary.does_asset_exist(L.MAT_DIR + "/M_FabCarPaint"):
        L.fab_car_paint.made = True   # (Thwaite's master kept: its own instances point at it)
    sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    made = 0
    flagged, reflagged = set(), []
    for n in range(count):
        mesh, md = models[n % len(models)]
        colour = colours[(n * 5 + n // len(models)) % len(colours)]
        paint = L.fab_car_paint("Park_%d" % n, colour, cfg.get("metallic", 0.6), cfg.get("roughness", 0.32))
        path = "%s/SM_ParkedCar_%d" % (CAR_DEST, n)
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
        copy = unreal.EditorAssetLibrary.duplicate_asset(mesh.get_path_name().split(".")[0], path)
        if copy is None:
            unreal.log_warning("[Torque] parked cars: couldn't copy %s" % mesh.get_name())
            continue
        listed = set(md.get("paint_slots", []))
        painted = []
        for si, sm in enumerate(copy.get_editor_property("static_materials")):
            slot = str(sm.get_editor_property("material_slot_name"))
            mat = sm.get_editor_property("material_interface")
            base = mat.get_base_material() if mat else None
            names = " ".join([slot, mat.get_name() if mat else "", base.get_name() if base else ""])
            if slot in listed or str(si) in listed or "paint" in names.lower():
                copy.set_material(si, paint)
                painted.append(si)
        # [build 157 feedback] (beenster: the parked cars came out all grey, untextured) The Fab cars' materials
        # were made for Thwaite's placed actors; drawn instanced (an ISM, as every prop is) a material without
        # these usage flags draws as the default grey in a packaged game: flagged here, each base material once.
        for sm in copy.get_editor_property("static_materials"):
            mat = sm.get_editor_property("material_interface")
            base = mat.get_base_material() if mat else None
            if base is None or base.get_path_name() in flagged:
                continue
            flagged.add(base.get_path_name())
            changed = False
            for flag in ("used_with_instanced_static_meshes", "used_with_nanite"):
                try:
                    if not base.get_editor_property(flag):
                        base.set_editor_property(flag, True)
                        changed = True
                except Exception as e:
                    unreal.log_warning("[Torque] parked cars: %s not set on %s (%s)" % (flag, base.get_name(), e))
            if changed:
                unreal.MaterialEditingLibrary.recompile_material(base)
                unreal.EditorAssetLibrary.save_loaded_asset(base)
                reflagged.append(base.get_name())
        try:
            sme.remove_collisions(copy)
        except Exception:
            pass
        # (Nanite only where every material is opaque or masked: a Nanite mesh draws a translucent material,
        # the cars' glass, as the default grey)
        see_through = False
        for sm in copy.get_editor_property("static_materials"):
            mat = sm.get_editor_property("material_interface")
            base = mat.get_base_material() if mat else None
            try:
                mode = base.get_editor_property("blend_mode") if base else None
                see_through = see_through or mode in (unreal.BlendMode.BLEND_TRANSLUCENT, unreal.BlendMode.BLEND_ADDITIVE, unreal.BlendMode.BLEND_MODULATE)
            except Exception:
                pass
        try:
            ns = copy.get_editor_property("nanite_settings")
            ns.set_editor_property("enabled", not see_through)
            copy.set_editor_property("nanite_settings", ns)
        except Exception as e:
            unreal.log_warning("[Torque] parked cars: Nanite not set on %s (%s)" % (path, e))
        unreal.EditorAssetLibrary.save_loaded_asset(copy)
        add("SM_Prop_%s_%d" % (kind, n), copy, True, row)
        made += 1
        if not painted:
            unreal.log_warning("[Torque] parked cars: %s has no paint slot (left in its own colour)" % mesh.get_name())
    if reflagged:
        unreal.log("[Torque] parked cars: flagged for instancing/Nanite: %s" % ", ".join(reflagged))
    return "parked cars: %d (%d Fab models, %d colours; %d materials flagged for instancing)" % (made, len(models), len(colours), len(reflagged))


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
        for name, mesh in ours.items():
            try:
                sme.remove_collisions(mesh)
            except Exception:
                pass
            # [caravans] A kind whose row has "lods" (screen sizes, nearest first) gets reduced LODs: half,
            # a quarter, a tenth of its triangles (the bigger models drawn many times over, as the caravans).
            if name.endswith("_LOD1"):
                continue   # ([everyday kits] a hand-made distance version: set on its model below)
            kind = name[len("SM_Prop_"):].rsplit("_", 1)[0] if name.startswith("SM_Prop_") else ""
            lods = spec["kinds"].get(kind, {}).get("lods")
            hand = spec["kinds"].get(kind, {}).get("hand_lods")
            if hand and (name + "_LOD1") in ours:
                # [everyday kits] Hand-made distance versions (never auto-reduced): the model's _LOD1 glb as its
                # LOD 1, from the screen size in "hand_lods".
                try:
                    if sme.get_lod_count(mesh) > 1:
                        sme.remove_lods(mesh)
                    sme.set_lod_from_static_mesh(mesh, 1, ours[name + "_LOD1"], 0, True)
                    try:
                        sme.set_lod_screen_sizes(mesh, [1.0, float(hand[0])])
                    except Exception as e:
                        unreal.log_warning("[Torque] props: %s: LOD screen size not set (%s)" % (name, e))
                    log("props: %s: hand-made LOD1" % name)
                except Exception as e:
                    unreal.log_warning("[Torque] props: hand-made LOD of %s not set (%s)" % (name, e))
            elif not lods:
                # ([caravans, build 166] reduced LODs of these models break up: each triangle has its own corners,
                # so the reducer cuts holes. A model without "lods" is drawn whole, its old LODs taken off.)
                try:
                    if sme.get_lod_count(mesh) > 1:
                        sme.remove_lods(mesh)
                        log("props: %s: LODs removed" % name)
                except Exception as e:
                    unreal.log_warning("[Torque] props: LODs of %s not removed (%s)" % (name, e))
            elif lods:
                try:
                    settings = [unreal.EditorScriptingMeshReductionSettings(1.0, 1.0)]
                    for keep, screen in zip((0.5, 0.25, 0.1), lods):
                        settings.append(unreal.EditorScriptingMeshReductionSettings(keep, float(screen)))
                    opts = unreal.EditorScriptingMeshReductionOptions()
                    opts.set_editor_property("auto_compute_lod_screen_size", False)
                    opts.set_editor_property("reduction_settings", settings)
                    got = sme.set_lods_with_notification(mesh, opts, True)
                    log("props: %s: %d LODs" % (name, got))
                except Exception as e:
                    unreal.log_warning("[Torque] props: no LODs made for %s (%s)" % (name, e))
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
        if row.get("folder") == "Cars":
            continue   # ([car parks] made below)
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
                if name.startswith("SM_Prop_%s_" % kind) and not name.endswith("_LOD1"):
                    add(name, mesh, False, row, kit_paint if kind in STREET_KIT_KINDS else None)
    cars_note = ""
    for kind, row in spec["kinds"].items():
        if row.get("folder") == "Cars":
            cars_note = parked_cars(L, kind, row, add)
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
    return "RideProps: %d models (%s from Fab: %s), %d kinds with none (code-made): %s%s" % (
        len(species), len(found), ", ".join("%s %s" % (k, v[0].rsplit("/", 1)[-1]) for k, v in found.items()) or "none",
        len(none), ", ".join(none) or "none", "; " + cars_note if cars_note else "")


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
    garden_kit(L, kit)
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
