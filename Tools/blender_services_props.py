"""Ride UK's motorway service area set (the everyday kits plan, stage 8): the services' main building, the name
pylon at the way in and the lorries parked in the lorry park, modelled in Blender. Our own designs, from general
knowledge of British motorway services: no operators' names, logos or liveries (the made-up name on the building and
the pylon is set by the game in the sign alphabet, World/WorldRideServices.cpp).

  services_hall 0..1   the main building, 20 m deep (x) and 40 m across (y), 7.4 m to its top, its glazed front and
                       entrance canopy to +x, a plain fascia over the front for the name (laid stretched to the map's
                       building: Stretch its depth, Across its width): 0 flat roof behind a parapet, grey cladding and a
                       brick plinth; 1 a shallow barrel roof, timber cladding and a canopy the whole front
  services_pylon 0     the name pylon by the way in: a 7.5 m two-legged frame, a plain panel at its top each side for
                       the name, and a row of blank amenity tiles under it
  services_lorry 0..5  a parked lorry, its cab's front at +8.25 and its roof 4.0 m up (Tools/blender_lorry_models.py):
                       0 a grey tractor unit and a white fridge semi-trailer, 1 red with a grey curtain-sider, 2 white
                       with a navy box, 5 navy with a white box (all 16.5 m); 3 a white 6x4 rigid box lorry with a
                       tail-lift, 4 a grey 6x4 rigid fridge lorry, its fridge over the cab (10 m)

Each with two hand-made distance versions (SM_Prop_<kind>_<n>_LOD1.glb and _LOD2.glb); each kind's height and reach in
Data/RideUK/prop_kit.json. Same material and frame as the street kit (Tools/blender_street_kit.py).

    python Tools/blender_services_props.py                # the models
    python Tools/blender_services_props.py --render DIR   # and the review sheet's pictures in DIR
"""
import math
import os
import sys

import bpy  # noqa: F401  (before mathutils)
from mathutils import Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_street_kit as K   # noqa: E402
from blender_street_kit import box, lathe, rod   # noqa: E402
import blender_lorry_models as L   # noqa: E402

GLASS = (0.05, 0.07, 0.08)
FRAME = (0.12, 0.13, 0.14)
CLAD = (0.52, 0.53, 0.53)
DARKCLAD = (0.2, 0.21, 0.22)
TIMBER = (0.42, 0.28, 0.16)
BRICK = (0.42, 0.22, 0.14)
FASCIA = (0.06, 0.18, 0.12)
WHITE = (0.75, 0.75, 0.73)
STEEL = (0.45, 0.45, 0.44)
CONCRETE = (0.4, 0.39, 0.36)
TYRE = (0.02, 0.02, 0.02)
DARK = (0.03, 0.03, 0.035)
TILES = [(0.6, 0.35, 0.03), (0.05, 0.25, 0.45), (0.1, 0.35, 0.12), (0.5, 0.06, 0.05)]
TOP = 7.4

# kind: (reach in m, casts a shadow, hand-made LODs' screen sizes)
KINDS = {"services_hall": (1500, True, [0.25, 0.06]), "services_pylon": (600, True, [0.12, 0.03]),
         "services_lorry": (600, True, [0.15, 0.04])}


def hall(name, design, lod):
    """The main building: x from -10 (back) to +10 (front), y from -20 to 20."""
    M = K.Model(name)
    D, W = 20.0, 40.0
    xf = D / 2
    if design == 0:
        H = 6.2
        M.add(box("body", (0.0, 0.0, H / 2), (D, W, H)), "paint", CLAD, 0.3)
        M.add(box("parapet", (0.0, 0.0, H + (TOP - H) / 2), (D + 0.3, W + 0.3, TOP - H)), "paint", DARKCLAD, 0.3)
        M.add(box("plinth", (0.0, 0.0, 0.5), (D + 0.06, W + 0.06, 1.0)), "concrete", BRICK, 0.4)
        if lod < 2:
            for k, (y, w) in enumerate(((-11.0, 6.0), (11.0, 5.0))):
                M.add(box("plant", (-3.0 + 2.0 * k, y, H + 0.6), (3.0, w, 1.2)), "galv", STEEL, 0.4)
    else:
        H = 5.6
        M.add(box("body", (0.0, 0.0, H / 2), (D, W, H)), "timber", TIMBER, 0.4)
        # (the barrel roof: a shallow arc across x, its eaves out 0.6 m)
        n = (12, 6, 3)[lod]
        rise = TOP - H - 0.15
        verts, faces = [], []
        for i in range(n + 1):
            t = i / n
            x = -xf - 0.6 + t * (D + 1.2)
            z = H + 0.15 + rise * (1.0 - (2.0 * t - 1.0) ** 2)
            verts += [(x, -W / 2 - 0.6, z), (x, W / 2 + 0.6, z), (x, -W / 2 - 0.6, z - 0.15), (x, W / 2 + 0.6, z - 0.15)]
        for i in range(n):
            a, b = 4 * i, 4 * (i + 1)
            faces += [(a, a + 1, b + 1, b), (a + 2, b + 2, b + 3, a + 3), (a, b, b + 2, a + 2), (a + 1, a + 3, b + 3, b + 1)]
        faces += [(0, 2, 3, 1), (4 * n, 4 * n + 1, 4 * n + 3, 4 * n + 2)]
        M.add(K._object("roof", verts, faces, 25.0), "paint", DARKCLAD, 0.3)
        M.add(box("plinth", (0.0, 0.0, 0.4), (D + 0.06, W + 0.06, 0.8)), "concrete", CONCRETE, 0.5)
    # (the front: glazing over its middle 24 m, mullions every 1.5 m, the fascia over it for the name)
    gw, gh = 24.0, H - 1.6
    M.add(box("glaze", (xf + 0.02, 0.0, 0.2 + gh / 2), (0.04, gw, gh)), "glaze", GLASS, 0.05)
    if lod < 2:
        step = 1.5 if lod == 0 else 3.0
        k = 0
        while -gw / 2 + k * step <= gw / 2 + 1e-6:
            y = -gw / 2 + k * step
            M.add(box("mullion", (xf + 0.06, y, 0.2 + gh / 2), (0.08, 0.08, gh)), "paint", FRAME, 0.3)
            k += 1
        M.add(box("transom", (xf + 0.06, 0.0, 0.2 + gh), (0.1, gw, 0.12)), "paint", FRAME, 0.3)
        M.add(box("doors", (xf + 0.05, 0.0, 1.25), (0.06, 4.0, 2.5)), "glaze", (0.12, 0.14, 0.15), 0.05)
    M.add(box("fascia", (xf + 0.12, 0.0, H - 0.65), (0.2, 16.0, 1.1)), "paint", FASCIA, 0.25)
    # (the canopy: over the doors (0), or the whole glazing (1), on slim columns)
    cw = 10.0 if design == 0 else gw + 2.0
    cd = 4.0
    M.add(box("canopy", (xf + cd / 2, 0.0, 3.6), (cd, cw, 0.3)), "paint", DARKCLAD if design == 0 else TIMBER, 0.3)
    if lod < 2:
        for y in ([-cw / 2 + 0.4, cw / 2 - 0.4] if design == 0 else [-cw / 2 + 0.4 + i * (cw - 0.8) / 4 for i in range(5)]):
            M.add(rod("column", (xf + cd - 0.3, y, 0.0), (xf + cd - 0.3, y, 3.5), 0.12, (10, 6, 4)[lod]), "galv", STEEL, 0.3)
    # (the sides: a door and high windows each, a service door at the back)
    if lod < 2:
        for sy in (-1, 1):
            M.add(box("sidedoor", (2.0, sy * (W / 2 + 0.02), 1.1), (1.2, 0.04, 2.2)), "paint", FRAME, 0.3)
            for x in (-6.0, -2.0):
                M.add(box("sidewin", (x, sy * (W / 2 + 0.02), H - 1.6), (2.4, 0.04, 1.0)), "glaze", GLASS, 0.05)
        M.add(box("backdoor", (-xf - 0.02, -8.0, 1.5), (0.04, 3.0, 3.0)), "galv", STEEL, 0.4)
        M.add(box("backdoor", (-xf - 0.02, 8.0, 1.1), (0.04, 1.2, 2.2)), "paint", FRAME, 0.3)
    # (a wide pavement apron in front, under the canopy)
    M.add(box("apron", (xf + 3.0, 0.0, 0.05), (6.0, W - 2.0, 0.1)), "concrete", CONCRETE, 0.6)
    return M


def pylon(name, lod):
    """The name pylon: two legs 2.6 m apart, a panel 3 by 1.4 m at the top, four amenity tiles under it; both faces
    (+x and -x) the same."""
    M = K.Model(name)
    H = 7.5
    for sy in (-1, 1):
        M.add(box("leg", (0.0, sy * 1.4, H / 2), (0.3, 0.3, H)), "paint", DARKCLAD, 0.3)
    M.add(box("panel", (0.0, 0.0, H - 0.9), (0.35, 3.1, 1.5)), "paint", FASCIA, 0.25)
    M.add(box("cap", (0.0, 0.0, H + 0.03), (0.45, 3.4, 0.08)), "paint", DARKCLAD, 0.3)
    for k, c in enumerate(TILES):
        y = -1.05 + k * 0.7
        M.add(box("tilebg", (0.0, y, H - 2.35), (0.32, 0.62, 0.62)), "paint", WHITE, 0.3)
        if lod < 2:
            for sx in (-1, 1):
                M.add(box("tile", (sx * 0.165, y, H - 2.35), (0.01, 0.42, 0.42)), "paint", c, 0.3)
    M.add(box("base", (0.0, 0.0, 0.15), (0.8, 3.6, 0.3)), "concrete", CONCRETE, 0.6)
    return M


# ([Nov 26] beenster's pictures: our own detailed lorries, Tools/blender_lorry_models.py, in place of the plain ones)
LORRIES = [("artic", L.GREY_CAB, "reefer", L.WHITE), ("artic", (0.5, 0.05, 0.04), "curtain", L.CURTAIN_GREY),
           ("artic", L.WHITE, "box", L.NAVY), ("rigid", L.WHITE, False, L.WHITE), ("rigid", L.GREY_CAB, True, L.WHITE),
           ("artic", L.NAVY, "box", L.WHITE)]


def lorry(name, v, lod):
    """A parked lorry, its front at +8.25 and its roof at 4.0 m (LORRIES)."""
    form, cab, body, colour = LORRIES[v]
    if form == "artic":
        return L.artic_model(name, cab, body, colour, lod)
    return L.rigid_model(name, cab, colour, lod, reefer=body)


def build(lod):
    s = ("", "_LOD1", "_LOD2")[lod]
    return ([hall("SM_Prop_services_hall_%d" % d + s, d, lod) for d in (0, 1)] + [pylon("SM_Prop_services_pylon_0" + s, lod)]
            + [lorry("SM_Prop_services_lorry_%d" % v + s, v, lod) for v in range(len(LORRIES))])


def main():
    import blender_farm_props as FP
    import blender_roadside_props as RS
    args = sys.argv[1:]
    out = {}
    for lod in (0, 1, 2):
        K.clear()
        models = build(lod)
        if lod == 0 and "--render" in args:
            RS.render_sheet(models, args[args.index("--render") + 1],
                            "Ride UK's motorway services set (our own models; each with two hand-made distance versions)",
                            "services_props.png", cols=3, size=(520, 360))
        for M in models:
            nt, y0, y1, x0, x1, z0, z1 = K.write(M)
            out[M.name] = (nt, y0, y1)
            print("%-34s %6d triangles, %.2f..%.2f m up, %.2f x %.2f m" % (M.name, nt, y0, y1, x1 - x0, z1 - z0))
    FP.KINDS = KINDS
    FP.write_kinds(out)


if __name__ == "__main__":
    main()
