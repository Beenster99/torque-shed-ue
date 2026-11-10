"""Marks which edge of the Buttertubs road is the inside of each bend, for where puddles gather
(beenster: the camber drains water to the edges, and on a bend the road leans in, so it stands on
the inside). Writes the road's vertex colour G in Data/Buttertubs/road.glb in place:
0.5 on a straight (and down the middle), towards 1 at a bend's inside edge, towards 0 its outside,
by how tight the bend is (a 40 m radius or tighter counts fully). R (0 centre .. 1 edge) and the
rest of the file are left as they are. export_buttertubs.py writes the same when it makes the road.

    python3 Tools/road_puddle_sides.py [Data/Buttertubs/road.glb]

Then rebuild the level (Tools/ue_build_level.py) so Unreal takes the new road.
"""

import json
import struct
import sys

import numpy as np

ACROSS = np.array([-1.0, -0.5, 0.0, 0.5, 1.0])   # the road's five vertices across, left to right
HALF_CHORD = 6                                     # points either side (2 m apart): a 24 m chord
FULL_RADIUS = 40.0                                 # metres: this tight or tighter is a full bend


def inside_sides(centre, right):
    """For each point along the road, -1 .. 1: positive when the bend turns towards its right side."""
    n = len(centre)
    out = np.zeros(n)
    for i in range(n):
        a, b = max(i - HALF_CHORD, 0), min(i + HALF_CHORD, n - 1)
        if b - a < 2:
            continue
        mid = (centre[a] + centre[b]) / 2.0
        sag = mid - centre[i]
        half = np.linalg.norm(centre[b] - centre[a]) / 2.0
        if half < 1.0:
            continue
        kappa = 2.0 * np.linalg.norm(sag) / (half * half)          # 1 / radius
        towards = np.sign(np.dot(sag, right[i]))
        out[i] = towards * min(kappa * FULL_RADIUS, 1.0)
    # A little smoothing so a kink doesn't flip a single row.
    k = np.ones(5) / 5.0
    return np.convolve(np.pad(out, 2, mode="edge"), k, mode="valid")


def g_channel(side, rows):
    """The G byte for every vertex (rows of five across)."""
    g = 0.5 + 0.5 * np.clip(side[:, None] * ACROSS[None, :], -1.0, 1.0)
    return np.round(g * 255).astype(np.uint8).reshape(-1)


def patch(path):
    data = bytearray(open(path, "rb").read())
    jlen = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20:20 + jlen])
    bin_start = 20 + jlen + 8
    prim = doc["meshes"][0]["primitives"][0]
    acc_pos = doc["accessors"][prim["attributes"]["POSITION"]]
    acc_col = doc["accessors"][prim["attributes"]["COLOR_0"]]
    if acc_col["componentType"] != 5121 or acc_col["type"] != "VEC4":
        raise SystemExit("road colour isn't 8-bit RGBA")
    def view(acc):
        bv = doc["bufferViews"][acc["bufferView"]]
        return bin_start + bv.get("byteOffset", 0) + acc.get("byteOffset", 0), bv.get("byteStride")
    n = acc_pos["count"]
    if n % 5:
        raise SystemExit("road vertices aren't rows of five")
    po, _ = view(acc_pos)
    pos = np.frombuffer(bytes(data[po:po + n * 12]), np.float32).reshape(n // 5, 5, 3).astype(np.float64)
    centre = pos[:, 2, [0, 2]]                                       # x east, z south (y up dropped)
    right = pos[:, 4, [0, 2]] - pos[:, 2, [0, 2]]
    right /= np.maximum(np.linalg.norm(right, axis=1, keepdims=True), 1e-6)
    side = inside_sides(centre, right)
    g = g_channel(side, n // 5)
    co, stride = view(acc_col)
    stride = stride or 4
    for v in range(n):
        data[co + v * stride + 1] = int(g[v])
    open(path, "wb").write(bytes(data))
    bends = np.abs(side) > 0.5
    print("road: %d rows, %d on a bend tighter than %.0f m radius; G written" % (n // 5, int(bends.sum()), FULL_RADIUS * 2))
    return side


if __name__ == "__main__":
    patch(sys.argv[1] if len(sys.argv) > 1 else "Data/Buttertubs/road.glb")
