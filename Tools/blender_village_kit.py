"""Ride UK's village set (the everyday kits plan, stage 6): schools, modelled by script in Blender with the farm kit's
bays (Tools/blender_farm_kit.py) and a few more letters for a school's windows and doors. Our own designs from general
knowledge of British schools: no real school, name, badge or sign.

Extra letters (besides the farm kit's):
  W  a classroom's window band: a long glazed strip, white frames and mullions, a sill
  H  a tall Victorian window, stone or brick dressed head and sill
  E  the entrance: glazed double doors under a flat canopy
  S  a small high window (cloakrooms, toilets)
Extra walls: "render" (smooth painted render) and "cladding" (vertical timber boarding, close jointed); plinth "brick".

Cut into pieces in the warehouse kit's format by Tools/blender_farm_kit_export.py (Data/RideUK/village_kit.bin and
village_kit.json); World/WorldRideBuildings.cpp lays a school the map draws with the design that suits its size and
the place's walls, by the warehouse kit's BuildWarehouse.

    python Tools/blender_village_kit.py                  # the kit's pieces
    python Tools/blender_village_kit.py --render DIR     # and its review sheet in DIR
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blender_farm_kit as F  # noqa: E402

GLASS = (0.018, 0.022, 0.026)
FRAME_WHITE = (0.62, 0.62, 0.6)
FRAME_GREY = (0.09, 0.095, 0.1)
DOOR_BLUE = (0.03, 0.07, 0.16)
RENDER_WHITE = (0.58, 0.58, 0.55)
RENDER_CREAM = (0.55, 0.5, 0.4)
CLAD = (0.24, 0.16, 0.09)
BRICK_YELLOW = (0.38, 0.28, 0.16)
BRICK_SIXTIES = (0.3, 0.16, 0.1)
STONE_DRESS = (0.48, 0.45, 0.4)

_wall_surface, _wall_rgb, _thick, _plinth, _bay = F.wall_surface, F.wall_rgb, F.thick, F.plinth, F.bay


def wall_surface(spec):
    w = spec.get("wall")
    if w == "render":
        return "concrete"
    if w == "cladding":
        return "boarding"
    return _wall_surface(spec)


def wall_rgb(spec):
    w = spec.get("wall")
    if w == "render":
        return spec.get("wall_rgb", RENDER_WHITE)
    if w == "cladding":
        return spec.get("wall_rgb", CLAD)
    return _wall_rgb(spec)


def thick(spec):
    return 0.3 if spec.get("wall") == "render" else _thick(spec)


def plinth(spec, letter):
    if spec.get("plinth") == "brick" and letter not in "A":
        return (spec.get("plinth_h", 0.6), "brick", spec.get("plinth_rgb", F.BRICK_RED))
    return _plinth(spec, letter)


def window(U, w, a, b, z0, z1, frame, n, lod=1):
    """A glazed opening a..b, z0..z1: the glass just proud of the wall, a frame round it, n lights."""
    w.box(U, "glass", a, b, z0, z1, -0.03, 0.004, "plain", GLASS, 2 if (b - a) * (z1 - z0) > 3.0 else 1)
    t = 0.06
    w.box(U, "frame", a - t, b + t, z1, z1 + t, 0.0, 0.03, "plain", frame, lod)
    w.box(U, "frame", a - t, b + t, z0 - t, z0, 0.0, 0.05, "plain", frame, lod)
    w.box(U, "frame", a - t, a, z0, z1, 0.0, 0.03, "plain", frame, lod)
    w.box(U, "frame", b, b + t, z0, z1, 0.0, 0.03, "plain", frame, lod)
    for k in range(1, n):
        s = a + (b - a) * k / n
        w.box(U, "mullion", s - 0.03, s + 0.03, z0, z1, 0.0, 0.025, "plain", frame, 0)


def bay(U, spec, w, t, a, b, i, n, is_long, top_z, every_pipe=False):
    _bay(U, spec, w, t, a, b, i, n, is_long, top_z, every_pipe)
    H = spec["H"]
    s = (a + b) / 2
    bw = b - a
    frame = spec.get("frame_rgb", FRAME_WHITE)
    if t == "W":
        z0, z1 = spec.get("sill", 0.9), min(H - 0.45, spec.get("head", 2.4))
        m = 0.15 if i == 0 and not is_long else 0.12
        window(U, w, a + m + 0.1, b - m - 0.1, z0, z1, frame, max(1, int(round(bw / 1.2))))
        if spec.get("panel_rgb"):
            # (a coloured panel under the glass, the sixties' way)
            w.box(U, "panel", a + 0.22, b - 0.22, (F.plinth(spec, t) or (F.FOOT,))[0] + 0.05, z0 - 0.08, 0.0, 0.02, "plain", spec["panel_rgb"], 1)
    elif t == "H":
        wd = min(1.3, bw * 0.55)
        z0, z1 = 1.0, min(H - 0.6, 3.9) if is_long else min(H + 1.5, 4.6)
        window(U, w, s - wd / 2, s + wd / 2, z0, z1, frame, 2)
        dress = spec.get("dress_rgb", STONE_DRESS)
        surf = "stone" if spec.get("wall") == "stone" else "brick" if spec.get("wall") == "brick" else "concrete"
        w.box(U, "head", s - wd / 2 - 0.15, s + wd / 2 + 0.15, z1 + 0.06, z1 + 0.36, 0.0, 0.05, surf, dress, 1)
        w.box(U, "sill", s - wd / 2 - 0.1, s + wd / 2 + 0.1, z0 - 0.16, z0 - 0.06, 0.0, 0.08, surf, dress, 1)
    elif t == "E":
        wd = min(2.0, bw * 0.6)
        window(U, w, s - wd / 2, s + wd / 2, 0.0, 2.3, spec.get("door_rgb", DOOR_BLUE), 2)
        if bw > wd + 1.2:
            window(U, w, s + wd / 2 + 0.2, min(b - 0.2, s + wd / 2 + 0.9), 0.6, 2.3, frame, 1, 0)
        w.box(U, "canopy", s - wd / 2 - 0.6, s + wd / 2 + 0.6, 2.75, 2.9, 0.0, 1.4, "metal", spec.get("trim", FRAME_GREY), 1)
    elif t == "S":
        for k in ((-0.3,) if bw < 2.6 else (-0.6, 0.6)):
            x = s + k * bw * 0.5
            window(U, w, x - 0.35, x + 0.35, 1.7, 2.3, frame, 1, 0)


# The designs: ("classes": the game's school classes; "short" the outline's short side, "area" m2)
DESIGNS = [
    ("VL_board_school_brick", "Victorian board school: red brick, slate roof, tall windows, gabled ends",
     dict(L=24, W=9, H=4.6, roof="pitch", pitch=40, bay=3.0, gable_bay=4.5, wall="brick", wall_rgb=F.BRICK_RED, dress_rgb=STONE_DRESS,
          front="H H E H H H H H", back="H*", right="H H", left="H H", roof_kind="slate", gutters=True, trim=FRAME_GREY),
     dict(classes=["school_old_brick", "school_old"], short=[6, 11], area=[60, 900], weight=1.0)),
    ("VL_board_school_stone", "Village school: stone, slate roof, tall windows with dressed heads",
     dict(L=20, W=8, H=4.4, roof="pitch", pitch=42, bay=3.0, gable_bay=4.0, wall="stone", wall_rgb=F.SANDSTONE, dress_rgb=(0.5, 0.46, 0.4),
          front="H E H H H H H", back="H*", right="H H", left="H .", roof_kind="slate", gutters=True, trim=FRAME_GREY),
     dict(classes=["school_old_stone", "school_old"], short=[6, 11], area=[60, 900], weight=1.0)),
    ("VL_sixties_school", "Post-war school: brick, a long window band, coloured panels, a shallow roof",
     dict(L=40, W=14, H=3.8, roof="pitch", pitch=8, bay=3.6, gable_bay=4.7, wall="brick", wall_rgb=BRICK_SIXTIES,
          front="W W W E W W W W W W W", back="W*", right=". S .", left=". S .", roof_kind="sheet", roof_rgb=(0.05, 0.05, 0.055),
          sill=1.0, head=3.2, panel_rgb=(0.05, 0.12, 0.25), rooflights=False, trim=FRAME_GREY),
     dict(classes=["school_new"], short=[9, 40], area=[200, 9000], weight=1.0)),
    ("VL_modern_school", "New primary school: white render and timber cladding, big windows, a grey roof",
     dict(L=36, W=16, H=4.2, roof="pitch", pitch=10, bay=3.6, gable_bay=5.3, wall="render", plinth="brick", plinth_h=0.6, plinth_rgb=BRICK_YELLOW,
          front="W W E W W W W W W W", back="W*", right=". S .", left=". S .", roof_kind="sheet", roof_rgb=(0.08, 0.085, 0.09),
          sill=0.7, head=3.0, frame_rgb=FRAME_GREY, rooflights=False, trim=FRAME_GREY),
     dict(classes=["school_new"], short=[9, 40], area=[200, 9000], weight=0.9)),
    ("VL_timber_school", "Timber-clad classroom block: vertical boarding, window band, a low roof",
     dict(L=24, W=10, H=3.6, roof="pitch", pitch=12, bay=3.0, gable_bay=5.0, wall="cladding", plinth="brick", plinth_h=0.5, plinth_rgb=F.BRICK_RED,
          front="W W E W W W W W", back="W*", right=". S", left="S .", roof_kind="sheet", roof_rgb=(0.06, 0.065, 0.07),
          sill=0.8, head=2.8, frame_rgb=FRAME_GREY, rooflights=False, trim=FRAME_GREY),
     dict(classes=["school_new", "school_small"], short=[6, 14], area=[50, 600], weight=0.8)),
]

ABOUT = ("Ride UK's village set (the everyday kits plan, stage 6): original designs of UK schools, modelled by "
         "Tools/blender_village_kit.py with the farm kit's bays and cut into pieces by Tools/blender_farm_kit_export.py "
         "(this file and village_kit.bin) in the warehouse kit's format. World/WorldRideBuildings.cpp gives a school the "
         "map draws the design that suits its size and the place's walls and lays it with BuildWarehouse. Classes: "
         "school_old_brick, school_old_stone, school_old (small, old), school_new, school_small.")


def patch():
    """The farm kit's builder with the village letters and walls."""
    F.wall_surface, F.wall_rgb, F.thick, F.plinth, F.bay = wall_surface, wall_rgb, thick, plinth, bay


def main():
    patch()
    args = sys.argv[1:]
    import blender_farm_kit_export as X
    X.F = F
    X.main(DESIGNS, "village_kit", ABOUT)
    if "--render" in args:
        import blender_warehouse_kit as K
        import blender_farm_kit_render as R
        K.clear()
        units = []
        for name, label, spec, suits in DESIGNS:
            U = F.unit(name, label, spec)
            U.spec, U.suits = spec, suits
            U.tris = K.triangle_counts(U)
            units.append(U)
        R.render_sheet(units, args[args.index("--render") + 1], title="Ride UK's village set: schools (Blender renders, our own designs; each fits any outline)",
                       sheet_name="village_kit_schools.png")


if __name__ == "__main__":
    main()
