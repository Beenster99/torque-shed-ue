"""What the ridden bike bumps into on the Buttertubs ride: the solid props (drystone walls and their
heads, gate stoops and gates, buildings, boulders, cars, benches, signs...) as upright boxes, written to
Data/Buttertubs/collision.json for the bike's contacts (Source/TorqueShedProto/Sim/BikeContacts).

The level draws these props without collision (Nanite meshes, Tools/ue_build_level.py), so the wheels'
ground traces only ever see the land and the road; the bike's body, rider and wheels are kept out of
these boxes by the sim instead, as Torque Shed's Jolt body was kept out of its walls.

    python Tools/make_collision.py

Each prop mesh's bounds come from its glb in Data/Buttertubs/Props (glb frame: x, y up, z, metres) and
each instance from props.json (Tools/buttertubs_detail.py's Props.add: Unreal cm location, pitch, yaw,
roll in degrees, scale along the mesh's Unreal axes, with the glb's (x, y, z) as Unreal's (x, z, y)).
A box is [cx, cz, ux, uz, hu, hv, y0, y1] in the sim frame (metres, x east, y up, z south): its
centre on the ground plane, the unit direction of its first axis, its half lengths along that axis and
the one square to it, and its height range. Pitch and roll are left out (walls are only tilted a few
degrees to the ground). Only boxes within REACH of the road's line are kept.
"""
import json
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
RIDE = os.path.join(REPO, "Data", "Buttertubs")
OUT = os.path.join(RIDE, "collision.json")
REACH = 40.0   # m from the road's centre line
SOLID = ("Wall", "WallEnd", "Bld", "GateStoop", "Gate", "Rock", "Bales", "Farm4x4", "Car", "Bin", "QuadBike",
         "SignJunction", "ChurnStand", "LogPile", "Bench", "SignVillage", "FingerPost", "PhoneBox", "PostBox", "PicnicTable")
LOWEST = 0.25   # m: a rock lower than this the wheels just ride over (the ground traces don't see it, but it's small)


def glb_bounds(path):
    with open(path, "rb") as f:
        data = f.read()
    length = struct.unpack_from("<I", data, 8)[0]
    off = 12
    js = None
    while off < length:
        clen, ctype = struct.unpack_from("<II", data, off)
        if ctype == 0x4E4F534A:
            js = json.loads(data[off + 8: off + 8 + clen])
            break
        off += 8 + clen
    lo = [1e30] * 3
    hi = [-1e30] * 3
    for mesh in js.get("meshes", []):
        for prim in mesh.get("primitives", []):
            acc = js["accessors"][prim["attributes"]["POSITION"]]
            for i in range(3):
                lo[i] = min(lo[i], acc["min"][i])
                hi[i] = max(hi[i], acc["max"][i])
    for node in js.get("nodes", []):
        if any(k in node for k in ("matrix", "rotation", "scale", "translation")):
            raise ValueError("%s: its nodes are transformed; this reads mesh bounds only" % path)
    return lo, hi


def main():
    props = json.load(open(os.path.join(RIDE, "props.json")))
    route = json.load(open(os.path.join(RIDE, "route.json")))
    line = route["line_sim"]
    pts = [(p[0], p[2]) for p in line]
    # A coarse grid of the road's line for the distance check.
    cell = 20.0
    grid = {}
    for x, z in pts:
        grid.setdefault((int(math.floor(x / cell)), int(math.floor(z / cell))), []).append((x, z))

    def near_road(x, z):
        cx, cz = int(math.floor(x / cell)), int(math.floor(z / cell))
        r = int(math.ceil(REACH / cell))
        for i in range(cx - r, cx + r + 1):
            for j in range(cz - r, cz + r + 1):
                for px, pz in grid.get((i, j), ()):
                    if (px - x) ** 2 + (pz - z) ** 2 < REACH * REACH:
                        return True
        return False

    boxes = []
    kinds = {}
    for name, insts in props["instances"].items():
        kind = name.split("_")[1]
        if kind not in SOLID:
            continue
        path = os.path.join(RIDE, "Props", name + ".glb")
        if not os.path.exists(path):
            continue
        lo, hi = glb_bounds(path)
        for t in insts:
            ux, uy, uz, _pitch, yaw, _roll, sx, sz, sy = t   # scale stored along Unreal (x, y, z) = glb (x, z, y)
            x, z, y = ux / 100.0, uy / 100.0, uz / 100.0
            if not near_road(x, z):
                continue
            a = math.radians(yaw)
            ca, sa = math.cos(a), math.sin(a)
            # The box in the mesh's own frame, scaled: x across lo..hi along the mesh's +x, z along its +z.
            mx, mz = 0.5 * (lo[0] + hi[0]) * sx, 0.5 * (lo[2] + hi[2]) * sz
            hu, hv = 0.5 * (hi[0] - lo[0]) * abs(sx), 0.5 * (hi[2] - lo[2]) * abs(sz)
            y0, y1 = y + lo[1] * sy, y + hi[1] * sy
            if kind == "Rock" and y1 - max(y0, y - 0.05) < LOWEST:
                continue
            # +x turns to (cos a, sin a) and +z to (-sin a, cos a) in (x, z).
            cx, cz = x + mx * ca - mz * sa, z + mx * sa + mz * ca
            boxes.append([round(cx, 3), round(cz, 3), round(ca, 4), round(sa, 4), round(hu, 3), round(hv, 3), round(y0, 3), round(y1, 3)])
            kinds[kind] = kinds.get(kind, 0) + 1
    out = {"about": "Upright boxes the ridden bike can't pass through, made by Tools/make_collision.py from props.json and "
                    "the prop glbs: [cx, cz, ux, uz, hu, hv, y0, y1] in the sim frame (m; x east, y up, z south).",
           "kinds": kinds, "boxes": boxes}
    with open(OUT, "w") as f:
        json.dump(out, f, separators=(",", ":"))
    print("collision: %d boxes (%s) -> %s" % (len(boxes), ", ".join("%s %d" % kv for kv in sorted(kinds.items())), OUT))


if __name__ == "__main__":
    main()
