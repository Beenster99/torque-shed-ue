"""Brings the Godot game's Blender bike parts (Beenster99/torque-shed, assets/bike_parts) into this repo
at Data/Parts/Blender, in our frame, ready for Unreal and for the fitting core.

    python Tools/import_blender_parts.py --godot ../torque-shed [--sheet out.png] [kind ...]

What each Godot glb holds (blender/README.md there, "the parts agreement"): a node "part" with three
meshes lod0, lod1 and lod2, mount points as empties mount_<name>, every surface's material named for
its surface tag (paint, chrome, rubber, ...; a "__<slot>" ending takes another slot's colour), UVs at
1 unit = 1 m, and on some parts shape keys (a tank's tunnel_height, say) the layout mixes in to fit
the part to a frame. Their frame is glTF's: y up, the bike's front along -z, its right along +x.

What this writes, per part (same kind/id paths as the Godot repo):
  <kind>/<id>.glb, <id>_lod1.glb, <id>_lod2.glb   one mesh each, a material slot per surface tag
  <kind>/<id>.morph.bin                            the shape keys, when it has any (see index.json)
and index.json: per part its mounts (at, x, y as our sockets have them), surfaces (tag, colour slot,
the Blender colour, metallic, roughness, the finish we draw it in), shape keys, bounds and triangles.

Our frame is x forward, y up, z right, so ours = (-gz, gy, gx): a rotation (no mirror), so triangles
keep their winding and normals turn the same way. Tangents are left out (Unreal builds its own from
the normals and UVs, as it does for every bike part). Shape keys are kept outside the glbs, so the
static meshes Unreal imports are plain, and the fitting core can mix them in itself.

The Godot glbs stay the source of truth: the output is generated, committed, and never edited by hand.
The one exception is Data/Parts/Redrawn/<kind>/<id>.glb: parts beenster has had redrawn for this game
(the sports tanks, from his three-view drawings; Tools/redrawn_tanks.md). They follow the same parts
agreement, and wherever one exists it is read in place of the Godot glb of the same kind/id.

    python Tools/import_blender_parts.py --redrawn      only the redrawn parts (no Godot checkout needed)
"""
import argparse
import glob
import hashlib
import json
import os
import struct
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Data", "Parts", "Blender")
REDRAWN = os.path.join(REPO, "Data", "Parts", "Redrawn")

# glTF to ours: ours = M @ g.
M = np.array([[0.0, 0.0, -1.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0]])

# The surface tags (blender/lib/kit.py) and the finish each is drawn in here. "paint" and "paint_2"
# take the bike's colours; the Blender colour rides along in the index so a part keeps its own look
# until the shed paints it. The metals, plastics, rubber, carbon and fabric have catalogue finishes
# of their own (CATALOGUE_FINISHES: kit.py's colour, metalness and roughness, which is how Torque
# Shed draws them, with a detail set from Tools/make_bike_textures.py for the close-up surface);
# chrome, leather, glass, lenses, dials and decals share the Speed Twin's (speedtwin_pack /
# make_speedtwin FINISHES).
TAG_FINISH = {
    "paint": "paint", "paint_2": "paint_2", "powder": "cat_powder", "chrome": "chrome",
    "polished_alloy": "cat_polished_alloy", "cast_alloy": "cat_cast_alloy", "brushed_steel": "cat_brushed_steel",
    "raw_steel": "cat_raw_steel", "black_metal": "cat_black_metal", "anodised": "cat_anodised",
    "titanium": "cat_titanium", "carbon": "cat_carbon", "rubber": "cat_rubber", "plastic": "cat_plastic",
    "plastic_paint": "paint", "leather": "leather", "fabric": "cat_fabric", "glass": "glass_dark",
    "lens_white": "lens_clear", "lens_amber": "lens_amber", "lens_red": "lens_red", "dial": "dial_cream",
    "decal": "decal_white",
}


def _cat(color, metallic, roughness, detail, uv_scale, normal_strength, rough_var, shader="base", grime=0.6):
    """A catalogue finish in make_speedtwin's terms (the level script makes MI_<name> of each)."""
    return {"shader": shader, "color": list(color), "metallic": metallic, "roughness": roughness,
            "color2": list(color), "metallic2": metallic, "roughness2": roughness, "detail": detail,
            "uv_scale": uv_scale, "normal_strength": normal_strength, "rough_var": rough_var,
            "clearcoat_roughness": 0.03, "emissive": 0.0, "grime": grime}


# Colour, metalness and roughness are kit.py's TAG_LOOK; the detail sets' scales are per metre (the
# catalogue surfaces are box-mapped in metres, TorqueShedCatalogue BuildMesh).
CATALOGUE_FINISHES = {
    "cat_powder": _cat((0.08, 0.08, 0.09), 0.0, 0.55, "powder", 6.0, 0.35, 0.2),
    "cat_polished_alloy": _cat((0.88, 0.88, 0.86), 1.0, 0.12, "brushed", 10.0, 0.25, 0.2),
    "cat_cast_alloy": _cat((0.6, 0.6, 0.58), 0.85, 0.48, "cast", 8.0, 0.6, 0.3),
    "cat_brushed_steel": _cat((0.75, 0.75, 0.74), 1.0, 0.28, "brushed", 5.0, 0.6, 0.35),
    "cat_raw_steel": _cat((0.45, 0.45, 0.46), 1.0, 0.4, "cast", 12.0, 0.35, 0.35),
    "cat_black_metal": _cat((0.035, 0.035, 0.04), 0.5, 0.55, "powder", 6.0, 0.3, 0.2),
    "cat_anodised": _cat((0.1, 0.25, 0.7), 0.9, 0.3, "brushed", 10.0, 0.3, 0.2),
    "cat_titanium": _cat((0.55, 0.53, 0.5), 1.0, 0.3, "brushed", 6.0, 0.4, 0.3),
    "cat_carbon": _cat((0.03, 0.03, 0.03), 0.0, 0.2, "carbon", 21.0, 0.5, 0.1, shader="paint", grime=0.25),
    "cat_rubber": _cat((0.02, 0.02, 0.02), 0.0, 0.85, "rubber", 12.0, 0.6, 0.2),
    "cat_plastic": _cat((0.04, 0.04, 0.045), 0.0, 0.6, "rubber", 25.0, 0.25, 0.15),
    "cat_fabric": _cat((0.12, 0.12, 0.12), 0.0, 0.9, "fabric", 20.0, 0.8, 0.2),
}

COMP = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


class Glb:
    """A glb's JSON and binary chunk, with accessors read as numpy arrays (sparse ones too)."""

    def __init__(self, path):
        b = open(path, "rb").read()
        if b[:4] != b"glTF":
            raise ValueError("%s: not a glb" % path)
        n = struct.unpack("<I", b[12:16])[0]
        self.j = json.loads(b[20:20 + n])
        off = 20 + n
        self.bin = b[off + 8:off + 8 + struct.unpack("<I", b[off:off + 4])[0]] if off < len(b) else b""

    def view(self, i, dtype, count, width, extra=0):
        bv = self.j["bufferViews"][i]
        stride = bv.get("byteStride", 0)
        item = np.dtype(dtype).itemsize * width
        start = bv.get("byteOffset", 0) + extra
        if stride and stride != item:
            raw = np.frombuffer(self.bin, np.uint8, count=stride * (count - 1) + item, offset=start)
            rows = np.lib.stride_tricks.as_strided(raw, (count, item), (stride, 1))
            return np.frombuffer(rows.copy().tobytes(), dtype).reshape(count, width)
        return np.frombuffer(self.bin, dtype, count=count * width, offset=start).reshape(count, width)

    def acc(self, i):
        a = self.j["accessors"][i]
        w = WIDTH[a["type"]]
        dt = COMP[a["componentType"]]
        if "bufferView" in a:
            out = self.view(a["bufferView"], dt, a["count"], w, a.get("byteOffset", 0)).astype(np.float64)
        else:
            out = np.zeros((a["count"], w))
        if a.get("normalized"):
            out = out / float(np.iinfo(dt).max)
        sp = a.get("sparse")
        if sp:
            idx = self.view(sp["indices"]["bufferView"], COMP[sp["indices"]["componentType"]], sp["count"], 1,
                            sp["indices"].get("byteOffset", 0)).reshape(-1).astype(np.int64)
            val = self.view(sp["values"]["bufferView"], dt, sp["count"], w, sp["values"].get("byteOffset", 0))
            out = out.copy()
            out[idx] = val
        return out


def node_matrix(n):
    if "matrix" in n:
        return np.array(n["matrix"], np.float64).reshape(4, 4).T
    t = np.array(n.get("translation", [0, 0, 0]), np.float64)
    x, y, z, w = n.get("rotation", [0, 0, 0, 1])
    r = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    s = np.array(n.get("scale", [1, 1, 1]), np.float64)
    m = np.eye(4)
    m[:3, :3] = r * s
    m[:3, 3] = t
    return m


def world_matrices(j):
    """Every node's transform in the scene (glTF frame)."""
    out = {}
    def walk(i, parent):
        m = parent @ node_matrix(j["nodes"][i])
        out[i] = m
        for c in j["nodes"][i].get("children", []):
            walk(c, m)
    scene = j["scenes"][j.get("scene", 0)]
    for r in scene["nodes"]:
        walk(r, np.eye(4))
    return out


def to_ours(m):
    """A glTF-frame transform as ours: M m M^T."""
    o = np.eye(4)
    o[:3, :3] = M @ m[:3, :3] @ M.T
    o[:3, 3] = M @ m[:3, 3]
    return o


def tag_of(name):
    base = name.split(".")[0]
    tag, _, slot = base.partition("__")
    return tag, slot


def read_part(path):
    """{lods: [[{tag, slot, pos, nrm, uv, idx, targets: [(dpos, dnrm)]}...] x3], mounts, keys, mats}."""
    g = Glb(path)
    j = g.j
    W = world_matrices(j)
    lods = [None, None, None]
    mounts = {}
    keys = []
    mats = j.get("materials", [])
    for i, n in enumerate(j["nodes"]):
        name = n.get("name", "")
        if name.startswith("mount_"):
            o = to_ours(W[i])
            r = o[:3, :3]
            sx = np.linalg.norm(r[:, 0]); sy = np.linalg.norm(r[:, 1])
            mounts[name[len("mount_"):]] = {"at": rnd(o[:3, 3]), "x": rnd(r[:, 0] / sx), "y": rnd(r[:, 1] / sy)}
        elif name in ("lod0", "lod1", "lod2") and "mesh" in n:
            k = int(name[3])
            m = to_ours(W[i])
            R = m[:3, :3]
            if np.linalg.det(R) <= 0:
                raise ValueError("%s: %s is mirrored" % (path, name))
            NR = np.linalg.inv(R).T
            R = R @ M       # glTF vertices to ours, then the node's own transform (in ours)
            NR = NR @ M
            mesh = j["meshes"][n["mesh"]]
            if k == 0:
                keys = list(mesh.get("extras", {}).get("targetNames", []))
            prims = []
            for pr in mesh["primitives"]:
                if pr.get("mode", 4) != 4:
                    raise ValueError("%s: a primitive that isn't triangles" % path)
                a = pr["attributes"]
                pos = g.acc(a["POSITION"])
                nrm = g.acc(a["NORMAL"]) if "NORMAL" in a else np.zeros_like(pos)
                uv = g.acc(a["TEXCOORD_0"]) if "TEXCOORD_0" in a else np.zeros((len(pos), 2))
                idx = (g.acc(pr["indices"]).reshape(-1) if "indices" in pr else np.arange(len(pos))).astype(np.int64)
                targets = []
                for t in pr.get("targets", []):
                    dp = g.acc(t["POSITION"]) if "POSITION" in t else np.zeros_like(pos)
                    dn = g.acc(t["NORMAL"]) if "NORMAL" in t else np.zeros_like(pos)
                    targets.append((dp @ R.T, dn @ NR.T))
                nn = nrm @ NR.T
                ln = np.linalg.norm(nn, axis=1, keepdims=True)
                nn = np.where(ln > 1e-12, nn / np.maximum(ln, 1e-12), nn)
                mi = pr.get("material")
                mname = mats[mi]["name"] if mi is not None else "raw_steel"
                tag, slot = tag_of(mname)
                prims.append({"tag": tag, "slot": slot, "mat": mi, "pos": pos @ R.T + m[:3, 3], "nrm": nn,
                              "uv": uv, "idx": idx, "targets": targets})
            lods[k] = prims
    if lods[0] is None:
        raise ValueError("%s: no lod0" % path)
    for k in (1, 2):
        if lods[k] is None:
            lods[k] = lods[k - 1]
    return {"lods": lods, "mounts": mounts, "keys": keys, "mats": mats}


def rnd(v, d=6):
    return [round(float(x), d) + 0.0 for x in v]


# --- Writing -----------------------------------------------------------------------------------------

def pad4(b, fill=b"\0"):
    return b + fill * ((4 - len(b) % 4) % 4)


def write_glb(path, prims, name):
    """One mesh, a primitive per surface, each with its own material named for its tag (and colour
    slot), so Unreal makes a material slot per surface. Positions, normals and UVs (as float), and
    16-bit indices where they fit."""
    blob = bytearray()
    views, accs, gprims, mats, mat_of = [], [], [], [], {}

    def add(arr, comp, typ, target, minmax=False):
        data = np.ascontiguousarray(arr).astype(COMP[comp]).tobytes()
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), **({"target": target} if target else {})})
        blob.extend(data)
        a = {"bufferView": len(views) - 1, "componentType": comp, "count": int(arr.shape[0]) if typ != "SCALAR" else int(arr.size), "type": typ}
        if minmax:
            a["min"] = [float(x) for x in arr.min(axis=0)]
            a["max"] = [float(x) for x in arr.max(axis=0)]
        accs.append(a)
        return len(accs) - 1

    for p in prims:
        key = p["tag"] + ("__" + p["slot"] if p["slot"] else "")
        if key not in mat_of:
            mat_of[key] = len(mats)
            mats.append({"name": key, "doubleSided": True,
                         "pbrMetallicRoughness": {"baseColorFactor": p.get("color", [0.5, 0.5, 0.5, 1.0]),
                                                  "metallicFactor": p.get("metallic", 0.0),
                                                  "roughnessFactor": p.get("roughness", 0.5)}})
        attrs = {"POSITION": add(p["pos"].astype(np.float32), 5126, "VEC3", 34962, True),
                 "NORMAL": add(p["nrm"].astype(np.float32), 5126, "VEC3", 34962),
                 "TEXCOORD_0": add(p["uv"].astype(np.float32), 5126, "VEC2", 34962)}
        comp = 5123 if len(p["pos"]) < 65536 else 5125
        ia = add(p["idx"], comp, "SCALAR", 34963)
        gprims.append({"attributes": attrs, "indices": ia, "material": mat_of[key], "mode": 4})
    while len(blob) % 4:
        blob.append(0)
    j = {"asset": {"version": "2.0", "generator": "Torque Shed UE Tools/import_blender_parts.py"},
         "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
         "meshes": [{"name": name, "primitives": gprims}], "materials": mats, "accessors": accs,
         "bufferViews": views, "buffers": [{"byteLength": len(blob)}]}
    js = pad4(json.dumps(j, separators=(",", ":")).encode(), b" ")
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob))
    out += struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(blob), 0x004E4942) + bytes(blob)
    write_if_changed(path, out)


def write_morphs(path, prims, keys):
    """The shape keys of lod0: for each key and surface, the vertices it moves (uint32) and how far
    (position then normal deltas, float32 x3 each), back to back. Returns the index's description."""
    blob = bytearray()
    out = []
    for ki, key in enumerate(keys):
        surf = []
        for si, p in enumerate(prims):
            if ki >= len(p["targets"]):
                continue
            dp, dn = p["targets"][ki]
            moved = np.nonzero((np.abs(dp) > 1e-7).any(axis=1) | (np.abs(dn) > 1e-7).any(axis=1))[0]
            if not len(moved):
                continue
            rec = {"surface": si, "count": int(len(moved)), "offset": len(blob)}
            blob.extend(moved.astype(np.uint32).tobytes())
            blob.extend(dp[moved].astype(np.float32).tobytes())
            blob.extend(dn[moved].astype(np.float32).tobytes())
            surf.append(rec)
        out.append({"name": key, "surfaces": surf})
    write_if_changed(path, bytes(blob))
    return out


def write_if_changed(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path) and open(path, "rb").read() == data:
        return
    with open(path, "wb") as f:
        f.write(data)


# --- The whole library -------------------------------------------------------------------------------

def surface_info(part, prim):
    mat = part["mats"][prim["mat"]] if prim["mat"] is not None else {}
    pbr = mat.get("pbrMetallicRoughness", {})
    return {"tag": prim["tag"], **({"colour_slot": prim["slot"]} if prim["slot"] else {}),
            "color": rnd(pbr.get("baseColorFactor", [1, 1, 1, 1])[:3], 4),
            "metallic": round(float(pbr.get("metallicFactor", 1.0)), 4),
            "roughness": round(float(pbr.get("roughnessFactor", 1.0)), 4),
            "finish": TAG_FINISH.get(prim["tag"], "black_satin")}


def convert(src_root, rel, out_root):
    part = read_part(os.path.join(src_root, rel))
    base = rel[:-4]
    lod_tris = []
    for k, prims in enumerate(part["lods"]):
        for p in prims:
            mat = part["mats"][p["mat"]] if p["mat"] is not None else {}
            pbr = mat.get("pbrMetallicRoughness", {})
            p["color"] = [float(x) for x in pbr.get("baseColorFactor", [0.5, 0.5, 0.5, 1.0])]
            p["metallic"] = float(pbr.get("metallicFactor", 0.0))
            p["roughness"] = float(pbr.get("roughnessFactor", 0.5))
        suffix = "" if k == 0 else "_lod%d" % k
        write_glb(os.path.join(out_root, base + suffix + ".glb"), prims, os.path.basename(base) + suffix)
        lod_tris.append(int(sum(len(p["idx"]) // 3 for p in prims)))
    lod0 = part["lods"][0]
    allp = np.concatenate([p["pos"] for p in lod0])
    entry = {"kind": rel.split("/")[0], "id": os.path.basename(base), "glb": base + ".glb",
             "lods": [base + ".glb", base + "_lod1.glb", base + "_lod2.glb"], "tris": lod_tris,
             "bounds": {"min": rnd(allp.min(axis=0), 4), "max": rnd(allp.max(axis=0), 4)},
             "surfaces": [surface_info(part, p) for p in lod0], "mounts": part["mounts"]}
    if rel.startswith("head/"):
        entry["layout"] = rel.split("/")[1]
    if part["keys"] and any(p["targets"] for p in lod0):
        entry["shape_keys"] = write_morphs(os.path.join(out_root, base + ".morph.bin"), lod0, part["keys"])
        entry["morph"] = base + ".morph.bin"
    return entry, part


def tank_keys(rels):
    """A redrawn tank's loft keys ([u, half width, up, down], as bike_model.gd's TANK_SHAPES) measured off
    its lod0 at the u's the Godot shape uses: its half width and top there, split into up and down in
    the Godot shape's own proportion (for a tank new to this game, the shape its tank_shapes.json entry
    names as "like"). Written to Data/Parts/Redrawn/tank_shapes.json (which
    import_layout_data.py lays over the Godot shapes) and into Data/Fitment/layout.json now."""
    if not rels:
        return
    lay_path = os.path.join(REPO, "Data", "Fitment", "layout.json")
    lay = json.load(open(lay_path))
    over_path = os.path.join(REDRAWN, "tank_shapes.json")
    over = json.load(open(over_path)) if os.path.exists(over_path) else {}
    for rel in rels:
        name = os.path.basename(rel)[:-4]
        pos = np.concatenate([p["pos"] for p in read_part(os.path.join(REDRAWN, rel))["lods"][0]])
        like = over.get(name, {}).get("like")
        if like:
            # A tank the Godot game does not have: the u's and up/down split of the shape it is
            # most like (its "like"), and that shape's len, n and bw unless the overlay sets its own.
            base = lay["tank_shapes"][like]
            old = base.get("godot_keys") or base["keys"]
            for k in ("len", "n", "bw"):
                over[name].setdefault(k, base[k])
        else:
            old = over.get(name, {}).get("godot_keys") or lay["tank_shapes"][name]["keys"]
        keys = []
        for u, _, up, down in old:
            near = np.abs(-pos[:, 0] - u)
            m = near < max(0.02, np.sort(near)[min(40, len(near) - 1)])
            top = float(pos[m, 1].max())
            d = down * top / max(up + down, 1e-6)
            keys.append([u, round(float(np.abs(pos[m, 2]).max()), 4), round(top - d, 4), round(d, 4)])
        if like:
            over[name]["keys"] = keys
        else:
            over[name] = {"keys": keys, "godot_keys": old}
        lay["tank_shapes"].setdefault(name, {}).update(over[name])
    write_if_changed(over_path, (json.dumps(over, indent=1) + "\n").encode())
    write_if_changed(lay_path, (json.dumps(lay, indent=1) + "\n").encode())


def git_head(path):
    try:
        return subprocess.run(["git", "-C", path, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--godot", default=os.path.join(os.path.dirname(REPO), "torque-shed"))
    ap.add_argument("--sheet", help="also render a contact sheet (png) of every part")
    ap.add_argument("--redrawn", action="store_true", help="only the parts in Data/Parts/Redrawn")
    ap.add_argument("kinds", nargs="*")
    a = ap.parse_args()
    redrawn = sorted(os.path.relpath(p, REDRAWN).replace(os.sep, "/") for p in glob.glob(os.path.join(REDRAWN, "**", "*.glb"), recursive=True))
    if a.redrawn:
        idx_path = os.path.join(OUT, "index.json")
        index = json.load(open(idx_path))
        for rel in redrawn:
            entry, _ = convert(REDRAWN, rel, OUT)
            entry["redrawn"] = True
            index["parts"][rel[:-4]] = entry
        write_if_changed(idx_path, (json.dumps(index, indent=1, sort_keys=False) + "\n").encode())
        tank_keys([r for r in redrawn if r.startswith("tank/")])
        print("%d redrawn parts: %s" % (len(redrawn), ", ".join(r[:-4] for r in redrawn)))
        return
    src = os.path.join(a.godot, "assets", "bike_parts")
    rels = sorted(os.path.relpath(p, src).replace(os.sep, "/") for p in glob.glob(os.path.join(src, "**", "*.glb"), recursive=True))
    if a.kinds:
        rels = [r for r in rels if r.split("/")[0] in a.kinds]
    if not rels:
        sys.exit("no glbs under %s" % src)
    idx_path = os.path.join(OUT, "index.json")
    index = json.load(open(idx_path)) if (a.kinds and os.path.exists(idx_path)) else {"parts": {}}
    parts = index["parts"]
    sheet = []
    for rel in rels:
        entry, part = convert(REDRAWN if rel in redrawn else src, rel, OUT)
        if rel in redrawn:
            entry["redrawn"] = True
        parts[rel[:-4]] = entry
        sheet.append((rel[:-4], part))
    # Leftovers from parts the Godot repo no longer has.
    if not a.kinds:
        keep = {os.path.normpath(os.path.join(OUT, f)) for e in parts.values() for f in e["lods"] + ([e["morph"]] if "morph" in e else [])}
        for f in glob.glob(os.path.join(OUT, "**", "*"), recursive=True):
            if os.path.isfile(f) and (f.endswith(".glb") or f.endswith(".bin")) and os.path.normpath(f) not in keep:
                os.remove(f)
    digest = hashlib.sha256()
    for rel in sorted(os.path.relpath(p, src) for p in glob.glob(os.path.join(src, "**", "*.glb"), recursive=True)):
        digest.update(rel.encode()); digest.update(open(os.path.join(src, rel), "rb").read())
    index.update({"about": "The Godot game's Blender bike parts in our frame (x forward, y up, z right; metres). "
                           "Written by Tools/import_blender_parts.py; do not edit.",
                  "source": {"repo": "Beenster99/torque-shed", "commit": git_head(a.godot), "glbs_sha256": digest.hexdigest()},
                  "tag_finish": TAG_FINISH,
                  "finishes": CATALOGUE_FINISHES,
                  "lod_at_m": [30.0, 120.0]})
    index["parts"] = dict(sorted(parts.items()))
    data = (json.dumps(index, indent=1, sort_keys=False) + "\n").encode()
    write_if_changed(idx_path, data)
    tris = sum(e["tris"][0] for e in parts.values())
    print("%d parts (%d with shape keys, %d mounts), %d lod0 triangles" % (
        len(parts), sum("morph" in e for e in parts.values()), sum(len(e["mounts"]) for e in parts.values()), tris))
    if a.sheet:
        contact_sheet(a.sheet, sheet)


# --- Contact sheet -----------------------------------------------------------------------------------

def contact_sheet(out, parts, tile=150, cols=26):
    """Every part's lod0 from the front right, a little above (x forward to the left of the tile, so a
    part reads the way it sits on a bike seen from its right), lit from the front and above. The mounts
    are red dots."""
    from PIL import Image, ImageDraw
    rows = (len(parts) + cols - 1) // cols
    img = np.full((rows * tile, cols * tile, 3), 0.93)
    yaw, pitch = np.radians(30.0), np.radians(15.0)
    # Camera on the bike's right (+z), turned 30 degrees towards the front, looking down 15 degrees.
    cy, sy, cp, sp = np.cos(yaw), np.sin(yaw), np.cos(pitch), np.sin(pitch)
    Ry = np.array([[cy, 0, -sy], [0, 1, 0], [sy, 0, cy]])
    Rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    V = Rx @ Ry @ np.array([[-1, 0, 0], [0, 1, 0], [0, 0, 1]])   # forward to screen left
    L = np.array([-0.3, 0.8, 0.5]); L /= np.linalg.norm(L)
    labels = []
    for n, (name, part) in enumerate(parts):
        r0, c0 = (n // cols) * tile, (n % cols) * tile
        tris, cols_ = [], []
        for p in part["lods"][0]:
            t = p["pos"][p["idx"].reshape(-1, 3)]
            tris.append(t)
            c = np.array(p["color"][:3]) if p["tag"] not in ("paint", "paint_2") else np.array([0.55, 0.08, 0.06])
            cols_.append(np.broadcast_to(c, (len(t), 3)))
        tri = np.concatenate(tris) @ V.T
        col = np.concatenate(cols_)
        lo, hi = tri.reshape(-1, 3).min(0), tri.reshape(-1, 3).max(0)
        s = (tile - 24) / max(hi[0] - lo[0], hi[1] - lo[1], 1e-6)
        mid = (lo + hi) / 2
        sx = tile / 2 + (tri[..., 0] - mid[0]) * s
        syy = tile / 2 - 6 - (tri[..., 1] - mid[1]) * s
        z = tri[..., 2]
        nrm = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
        nrm /= np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-12
        shade = 0.3 + 0.7 * np.clip(np.abs(nrm @ (V @ L)), 0, 1)
        rgb = np.clip(col * shade[:, None], 0, 1) ** (1 / 2.2)
        zb = np.full((tile, tile), -1e9)
        sub = img[r0:r0 + tile, c0:c0 + tile]
        for i in np.argsort(z.mean(1)):
            xs, ys = sx[i], syy[i]
            x0, x1 = int(max(xs.min(), 0)), int(min(xs.max(), tile - 1))
            y0, y1 = int(max(ys.min(), 0)), int(min(ys.max(), tile - 1))
            if x1 < x0 or y1 < y0:
                continue
            X, Y = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            (ax, bx, cx), (ay, by, cyy) = xs, ys
            d = (by - cyy) * (ax - cx) + (cx - bx) * (ay - cyy)
            if abs(d) < 1e-12:
                continue
            l1 = ((by - cyy) * (X - cx) + (cx - bx) * (Y - cyy)) / d
            l2 = ((cyy - ay) * (X - cx) + (ax - cx) * (Y - cyy)) / d
            l3 = 1 - l1 - l2
            m = (l1 >= 0) & (l2 >= 0) & (l3 >= 0)
            zz = l1 * z[i, 0] + l2 * z[i, 1] + l3 * z[i, 2]
            zs = zb[y0:y1 + 1, x0:x1 + 1]
            u = m & (zz > zs)
            zs[u] = zz[u]
            sub[y0:y1 + 1, x0:x1 + 1][u] = rgb[i]
        dots = []
        for mnt in part["mounts"].values():
            q = np.array(mnt["at"]) @ V.T
            dots.append((c0 + tile / 2 + (q[0] - mid[0]) * s, r0 + tile / 2 - 6 - (q[1] - mid[1]) * s))
        labels.append((c0 + 3, r0 + tile - 12, name.split("/")[-1][:24], name.split("/")[0], dots))
    im = Image.fromarray((img * 255).astype(np.uint8))
    dr = ImageDraw.Draw(im)
    for x, y, t, k, dots in labels:
        dr.text((x, y), t, fill=(30, 30, 30))
        dr.text((x, y - 128), k, fill=(110, 110, 110))
        for (px, py) in dots:
            dr.ellipse((px - 2, py - 2, px + 2, py + 2), fill=(220, 30, 30))
    im.save(out)
    print("contact sheet:", out)


if __name__ == "__main__":
    main()
