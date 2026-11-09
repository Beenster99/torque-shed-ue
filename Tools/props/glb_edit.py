"""Small glTF binary (.glb) helpers in plain Python + numpy, for editing the committed prop models
without Blender (the cloud sessions can't run it): read and write, positions as arrays, the
connected pieces ("islands") of one material, and moving, resizing or dropping pieces.

Coordinates are the glb's own: Torque Shed's local frame, metres, x right, y up, +z front."""
import json
import struct

import numpy as np

COMP = {5126: np.float32, 5123: np.uint16, 5125: np.uint32, 5121: np.uint8}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def read(path):
    f = open(path, "rb").read()
    _, _, total = struct.unpack_from("<III", f, 0)
    jl, _ = struct.unpack_from("<II", f, 12)
    j = json.loads(f[20:20 + jl])
    o = 20 + jl
    bl, _ = struct.unpack_from("<II", f, o)
    return j, bytearray(f[o + 8:o + 8 + bl])


def write(path, j, b):
    j["buffers"] = [{"byteLength": len(b)}]
    js = json.dumps(j, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    b = bytes(b) + b"\0" * ((4 - len(b) % 4) % 4)
    j["buffers"][0]["byteLength"] = len(b)
    js = json.dumps(j, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(b)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(b), 0x004E4942) + b)


def get(j, b, acc):
    """An accessor as an (n, comps) array (a copy)."""
    a = j["accessors"][acc]
    bv = j["bufferViews"][a["bufferView"]]
    dt = np.dtype(COMP[a["componentType"]]).newbyteorder("<")
    nc = NCOMP[a["type"]]
    start = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride", dt.itemsize * nc)
    assert stride == dt.itemsize * nc, "interleaved buffers aren't handled"
    return np.frombuffer(bytes(b[start:start + a["count"] * stride]), dt).reshape(a["count"], nc).copy()


def put(j, b, acc, arr):
    """Writes arr back over an accessor (same type; count may shrink)."""
    a = j["accessors"][acc]
    bv = j["bufferViews"][a["bufferView"]]
    dt = np.dtype(COMP[a["componentType"]]).newbyteorder("<")
    arr = np.ascontiguousarray(arr, dt)
    start = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    raw = arr.tobytes()
    assert len(raw) <= bv["byteLength"], "can't grow an accessor in place"
    b[start:start + len(raw)] = raw
    a["count"] = int(arr.shape[0]) if arr.ndim > 1 or a["type"] == "SCALAR" else int(arr.shape[0])
    if a["type"] == "VEC3" and a["componentType"] == 5126:
        a["min"] = [float(v) for v in arr.min(0)]
        a["max"] = [float(v) for v in arr.max(0)]
    if a["type"] == "SCALAR":
        a["min"], a["max"] = [int(arr.min())], [int(arr.max())]


def primitives(j, material):
    """The primitives drawn with a material (by name)."""
    names = [m.get("name") for m in j.get("materials", [])]
    if material not in names:
        return []
    mi = names.index(material)
    return [p for m in j["meshes"] for p in m["primitives"] if p.get("material") == mi]


def islands(pos, idx):
    """Triangles grouped into connected pieces (welding equal positions): [array of triangle ids]."""
    key = {}
    weld = np.empty(len(pos), np.int64)
    for i, p in enumerate(np.round(pos, 6)):
        weld[i] = key.setdefault(tuple(p), len(key))
    parent = list(range(len(key)))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    tris = weld[idx.reshape(-1, 3)]
    for a, b_, c in tris:
        ra, rb, rc = find(a), find(b_), find(c)
        parent[rb] = ra
        parent[find(rc)] = ra
    groups = {}
    for t, (a, _, _) in enumerate(tris):
        groups.setdefault(find(a), []).append(t)
    return [np.array(g) for g in groups.values()]


def select(j, b, material, near, tol, extent=None):
    """[(primitive, vertex ids)] of the islands of `material` whose bounds' centre is within tol (m,
    per axis) of `near`; extent (optional, per axis or None) must also match within tol."""
    out = []
    for p in primitives(j, material):
        pos = get(j, b, p["attributes"]["POSITION"])
        idx = get(j, b, p["indices"]).reshape(-1)
        for tris in islands(pos, idx):
            v = np.unique(idx.reshape(-1, 3)[tris].reshape(-1))
            lo, hi = pos[v].min(0), pos[v].max(0)
            c = (lo + hi) / 2
            if np.all(np.abs(c - near) <= tol) and (extent is None or all(
                    e is None or abs((hi - lo)[k] - e) <= tol for k, e in enumerate(extent))):
                out.append((p, v, tris))
    return out


def set_range(j, b, sel, axis, lo, hi):
    """Stretches each selected island along `axis` (a unit vector) so its extent there is lo..hi."""
    d = np.asarray(axis, float)
    d /= np.linalg.norm(d)
    for p, v, _ in sel:
        acc = p["attributes"]["POSITION"]
        pos = get(j, b, acc).astype(float)
        t = pos[v] @ d
        t0, t1 = t.min(), t.max()
        s = (hi - lo) / (t1 - t0)
        nt = lo + (t - t0) * s
        pos[v] += np.outer(nt - t, d)
        put(j, b, acc, pos)


def drop(j, b, sel):
    """Removes the selected islands' triangles (their vertices stay, unused)."""
    by_prim = {}
    for p, _, tris in sel:
        by_prim.setdefault(id(p), (p, []))[1].extend(tris.tolist())
    for p, tris in by_prim.values():
        idx = get(j, b, p["indices"]).reshape(-1, 3)
        keep = np.ones(len(idx), bool)
        keep[tris] = False
        put(j, b, p["indices"], idx[keep].reshape(-1))
