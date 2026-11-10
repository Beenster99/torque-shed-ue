"""The riding yard outside the shed: a large, flat concrete pad in front of the roller door (beenster,
Nov 9: "the outside should be a flat large concrete area where we can ride the bikes").

One place for its sizes, used twice:
  - Tools/ue_build_shed.py imports it to lay the concrete, the drystone walls round it and the lane
    beyond (outside());
  - run on its own (python Tools/shed_yard.py) it writes what the ridden bike feels there, beside
    each other in Data/Shed/Yard as the ride's are in Data/Buttertubs:
      route.json      where a bike rolls out (a straight line south from the door, for the co-op
                      start slots), and the open concrete a crashed rider is put back on;
      collision.json  the walls and buildings the bike can't ride through, as upright boxes
                      [cx, cz, ux, uz, hu, hv, y0, y1] (Tools/make_collision.py's format).

Shed frame (Unreal cm): x east, y south (out of the roller door), z up, the floor at z 0. The sim's
frame is metres, x east, y up, z south, so a sim point (x, y, z) is Unreal (100 x, 100 z, 100 y).
"""
import json
import os

# The shed's front wall: its outer face (ue_build_shed.py HALF_Y + WALL).
FRONT = 580.0

# The riding pad: 600 m across and 1.6 km out from the shed's front (beenster, Nov 9: "much bigger,
# big enough to allow the bike to get up to speed": a Speed Twin needs ~250 m to 100 mph and as much
# again to stop). Flat concrete, like an old airfield's apron.
PAD_X0, PAD_X1 = -30000.0, 30000.0
PAD_Y0, PAD_Y1 = FRONT, FRONT + 160000.0
# The yard behind and beside the shed (it was there before the pad), concrete too.
YARD_X0, YARD_X1, YARD_Y0, YARD_Y1 = -1600.0, 1840.0, -1400.0, FRONT

# Drystone walls (label, x0, x1, y0, y1), 1.3 m high: round the back yard, then round the pad.
DRYSTONE_H = 130.0
WALLS = (
    ("Wall_Back", -1700.0, 1900.0, -1560.0, -1500.0),
    ("Wall_YardW", -1700.0, -1640.0, -1500.0, FRONT),
    ("Wall_YardE", 1840.0, 1900.0, -1500.0, FRONT),
    ("Wall_PadNW", PAD_X0 - 60.0, -1640.0, FRONT - 60.0, FRONT),
    ("Wall_PadNE", 1840.0, PAD_X1 + 60.0, FRONT - 60.0, FRONT),
    ("Wall_PadW", PAD_X0 - 60.0, PAD_X0, FRONT - 60.0, PAD_Y1 + 60.0),
    ("Wall_PadE", PAD_X1, PAD_X1 + 60.0, FRONT - 60.0, PAD_Y1 + 60.0),
    ("Wall_PadS", PAD_X0 - 60.0, PAD_X1 + 60.0, PAD_Y1, PAD_Y1 + 60.0),
)
# The lane leaves past the pad's far wall (a field gate in it, shut), between walls of its own.
LANE_HALF = 200.0
LANE_Y0, LANE_Y1 = PAD_Y1 + 60.0, PAD_Y1 + 30000.0

# The buildings' footprints (outer faces), as the bike meets them: the shed's own four walls (the
# bikes are ridden in and out through the roller door, x -230 .. 230, and about the hall: its floor is
# ridable), the wash bay lean-to (open at its front: ridden into), the storage room, the dyno room and the paint shop with the mixing
# bay. Keep in step with ue_build_shed.py's shell and rooms.
DOOR_HALF = 230.0
BUILDINGS = (
    ("Shed_WallW", -680.0, -650.0, -580.0, FRONT),
    ("Shed_WallE", 650.0, 680.0, -580.0, FRONT),
    ("Shed_WallBack", -680.0, 680.0, -580.0, -550.0),
    ("Shed_WallFrontW", -680.0, -DOOR_HALF, 550.0, FRONT),
    ("Shed_WallFrontE", DOOR_HALF, 680.0, 550.0, FRONT),
    ("Shed_Workbench", -650.0, -555.0, -180.0, 300.0),
    # The wash bay lean-to is open to the yard at its front (y 440): a bike is ridden in off the pad
    # (beenster, Nov 10: "ride the bike from the test area into the wash bay"), up the wash lift's
    # ramp. So only its back and outer walls, the corner post and the pressure washer are in the way
    # (its east side is the shed's west wall). Keep in step with ue_build_shed.py wash_bay().
    ("WashBay_Back", -1190.0, -690.0, -260.0, -240.0),
    ("WashBay_Outer", -1190.0, -1170.0, -240.0, 460.0),
    ("WashBay_Post", -1170.0, -1150.0, 420.0, 440.0),
    ("WashBay_Washer", -1155.0, -1105.0, -200.0, -160.0),
    ("Storage", -1530.0, -650.0, -1130.0, -250.0),
    ("Dyno", -480.0, 480.0, -1330.0, -550.0),
    ("PaintShop", 650.0, 1730.0, -550.0, 550.0),
)
BUILDING_H = 350.0

# Where a bike rolls out when it's started from the door (F there): in front of it, facing out. From
# a bike in the shed it starts where that bike stands.
START_Y = FRONT + 300.0
LINE_END_Y = START_Y + 6000.0
# A crashed rider goes back on at the spot inside this (3 m clear of the walls); anywhere else
# (the back yard, against a wall) back to the door.
OPEN_MARGIN = 300.0


def _box(x0, x1, y0, y1, h):
    """An upright box in the sim frame, from shed cm."""
    return [round((x0 + x1) / 200.0, 3), round((y0 + y1) / 200.0, 3), 1.0, 0.0,
            round((x1 - x0) / 200.0, 3), round((y1 - y0) / 200.0, 3), -0.2, round(h / 100.0, 2)]


def route():
    pts = []
    y = START_Y
    while y <= LINE_END_Y + 1e-6:
        pts.append([0.0, 0.0, round(y / 100.0, 3)])
        y += 100.0
    return {
        "name": "Shed yard",
        "about": "Made by Tools/shed_yard.py. The ridden bike in the shed's yard: a straight line out of the "
                 "roller door (sim frame, m: x east, y up, z south) and the open concrete a crashed rider is put "
                 "back on [x0, z0, x1, z1].",
        "length_m": round((LINE_END_Y - START_Y) / 100.0, 2),
        "road_half_width_m": (PAD_X1 - PAD_X0) / 200.0,
        "line_sim": pts,
        "yard": True,
        "open_area_sim": [(PAD_X0 + OPEN_MARGIN) / 100.0, (PAD_Y0 + OPEN_MARGIN) / 100.0,
                          (PAD_X1 - OPEN_MARGIN) / 100.0, (PAD_Y1 - OPEN_MARGIN) / 100.0],
        "cattle_grids": [],
    }


def collision():
    boxes = [_box(x0, x1, y0, y1, DRYSTONE_H) for (_, x0, x1, y0, y1) in WALLS]
    boxes += [_box(x0, x1, y0, y1, BUILDING_H) for (_, x0, x1, y0, y1) in BUILDINGS]
    return {
        "about": "Made by Tools/shed_yard.py: upright boxes the bike can't ride through in the shed's yard, "
                 "[cx, cz, ux, uz, hu, hv, y0, y1] in the sim frame (m; x east, y up, z south).",
        "kinds": {"Wall": len(WALLS), "Bld": len(BUILDINGS)},
        "boxes": boxes,
    }


def write(project_dir):
    out = os.path.join(project_dir, "Data", "Shed", "Yard")
    os.makedirs(out, exist_ok=True)
    for name, data in (("route.json", route()), ("collision.json", collision())):
        with open(os.path.join(out, name), "w") as f:
            json.dump(data, f, indent=1)
            f.write("\n")
    return out


if __name__ == "__main__":
    where = write(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    print("wrote the yard's route.json and collision.json to %s" % where)
