"""Finds z-fighting in the shed without the editor: two visible surfaces in the same plane, facing
the same way and overlapping, flicker against each other in the depth buffer.

    python3 Tools/check_zfight.py            # the shed level (ue_build_shed.py, swaps.json) and the props
    python3 Tools/check_zfight.py props      # only the prop models (Data/Props/Blender/*.glb)

It runs Tools/ue_build_shed.py against a stand-in `unreal` module that only records the boxes and
shapes it spawns (labels, centres, sizes, turns), hides the ones Data/Props/swaps.json hides, and
compares every pair of visible faces. The props' glbs are read as triangles and compared the same
way, inside each model and against the level's boxes where the model stands. Exit code 1 if any
fight is found, so it can gate a build.

Faces count as one plane when they are within PLANE_TOL cm (props: 0.2 mm) and overlap by more than
a square millimetre or so. Faces facing each other (a plate on a wall) don't fight: one is behind."""
import json
import math
import os
import struct
import sys
import types

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PLANE_TOL = 0.05     # cm: closer than this and the depth buffer can't tell two planes apart at range
PROP_TOL = 0.02      # cm, inside a model (2 mm at most from the eye in the shed: 0.2 mm is too close)
MIN_AREA = 0.01      # cm^2


# --- A stand-in unreal module --------------------------------------------------------------------

class Any:
    """Accepts every call and attribute and returns itself (materials, settings, logging)."""
    def __init__(self, name="any", *a, **kw):
        self._name = name if isinstance(name, str) else "any"

    def __getattr__(self, k):
        if k.startswith("__"):
            raise AttributeError(k)
        return Any(self._name + "." + k)

    def __call__(self, *a, **kw):
        return Any(self._name + "()")

    def __iter__(self):
        return iter(())

    def __bool__(self):
        return True

    def __or__(self, o):
        return self

    def __getitem__(self, k):
        return Any()


class AnyClassMeta(type):
    def __getattr__(cls, k):
        if k.startswith("__"):
            raise AttributeError(k)
        return Any(k)


SPAWNED = []   # dicts: label, mesh, centre, scale, rot, visible, folder


class Actor(Any):
    def __init__(self, mesh, loc, rot):
        super().__init__("actor")
        self.rec = {"label": "", "mesh": mesh, "centre": loc, "scale": (1.0, 1.0, 1.0), "rot": rot,
                    "visible": True, "folder": ""}
        SPAWNED.append(self.rec)

    def set_actor_scale3d(self, v):
        self.rec["scale"] = v

    def set_actor_label(self, s):
        self.rec["label"] = s

    def get_actor_label(self):
        return self.rec["label"]

    def set_folder_path(self, s):
        self.rec["folder"] = s

    def get_component_by_class(self, c):
        rec = self.rec

        class Component(Any):
            def set_material(self, i, m):
                rec["material"] = id(m)
        return Component("component")


class Vec(tuple):
    def __new__(cls, x=0.0, y=0.0, z=0.0):
        return tuple.__new__(cls, (float(x), float(y), float(z)))


class Rot(tuple):
    def __new__(cls, roll=0.0, pitch=0.0, yaw=0.0):
        return tuple.__new__(cls, (float(roll), float(pitch), float(yaw)))


class Actors(Any):
    def spawn_actor_from_object(self, mesh, loc, rot):
        return Actor(mesh, loc, rot)

    def spawn_actor_from_class(self, cls, loc, rot):
        return Actor("class", loc, rot)

    def get_all_level_actors(self):
        return []


def fake_unreal():
    u = types.ModuleType("unreal")
    actors = Actors("actors")
    meshes = {"/Engine/BasicShapes/Cube": "cube", "/Engine/BasicShapes/Cylinder": "cylinder",
              "/Engine/BasicShapes/Sphere": "sphere"}
    u.Vector = Vec
    u.Rotator = Rot
    u.get_editor_subsystem = lambda c: actors
    u.load_asset = lambda p: meshes.get(p, Any(p))
    u.log = u.log_warning = u.log_error = lambda *a: None
    paths = types.SimpleNamespace(project_dir=lambda: ROOT + "/", project_content_dir=lambda: os.path.join(ROOT, "Content") + "/")
    u.Paths = paths

    def rest(k):
        if k[:1].isupper():   # a class (isinstance checks, enums): a type whose attributes are Any
            return AnyClassMeta(k, (Any,), {})
        return Any(k)
    u.__getattr__ = rest
    return u


def run_shed():
    sys.modules["unreal"] = fake_unreal()
    stub = types.ModuleType("ue_import_props")
    stub.dress = lambda: 0
    sys.modules["ue_import_props"] = stub
    sys.path.insert(0, os.path.join(ROOT, "Tools"))
    src = open(os.path.join(ROOT, "Tools", "ue_build_shed.py")).read()
    # Everything but the save at the end.
    g = {"__name__": "ue_build_shed", "__file__": "ue_build_shed.py"}
    code = compile(src, "ue_build_shed.py", "exec")
    os.path.getmtime_real = os.path.getmtime
    try:
        exec(code, g)
    except RuntimeError as e:   # save_fresh_level: no editor to write the map
        if "on disk" not in str(e) and "save" not in str(e):
            raise
    return g


# --- Geometry ------------------------------------------------------------------------------------

def rot_matrix(roll, pitch, yaw):
    """UE's FRotator to a matrix (degrees; yaw about z, pitch about y, roll about x)."""
    sr, cr = math.sin(math.radians(roll)), math.cos(math.radians(roll))
    sp, cp = math.sin(math.radians(pitch)), math.cos(math.radians(pitch))
    sy, cy = math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
    # FRotationMatrix columns: X axis, Y axis, Z axis.
    x = np.array([cp * cy, cp * sy, sp])
    y = np.array([sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp])
    z = np.array([-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp])
    return np.stack([x, y, z], axis=1)


def cube_tris(centre, size, R):
    """The 12 triangles of a cube (100 cm basic shape scaled to size), as (n, 3, 3)."""
    h = np.array(size) * 0.5
    c = np.array(centre)
    corners = np.array([[sx, sy, sz] for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)], float)
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    pts = (corners * h) @ R.T + c
    tris = []
    for a, b, cc, d in faces:
        tris.append((pts[a], pts[b], pts[cc]))
        tris.append((pts[a], pts[cc], pts[d]))
    return np.array(tris)


def cylinder_tris(centre, size, R, sides=24):
    """A cylinder's caps and side (UE's basic cylinder: 100 cm, axis z), as triangles."""
    c = np.array(centre)
    rx, ry, hz = size[0] * 0.5, size[1] * 0.5, size[2] * 0.5
    ring = [(rx * math.cos(2 * math.pi * i / sides), ry * math.sin(2 * math.pi * i / sides)) for i in range(sides)]
    tris = []
    for i in range(sides):
        (x0, y0), (x1, y1) = ring[i], ring[(i + 1) % sides]
        tris.append(((0, 0, hz), (x0, y0, hz), (x1, y1, hz)))
        tris.append(((0, 0, -hz), (x1, y1, -hz), (x0, y0, -hz)))
        tris.append(((x0, y0, -hz), (x1, y1, -hz), (x1, y1, hz)))
        tris.append(((x0, y0, -hz), (x1, y1, hz), (x0, y0, hz)))
    return np.array(tris, float) @ R.T + c


def clip(poly, a, b):
    """Sutherland-Hodgman: poly (2D points) clipped to the left of edge a->b."""
    out = []
    n = len(poly)
    if n == 0:
        return out
    def side(p):
        return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])
    for i in range(n):
        p, q = poly[i], poly[(i + 1) % n]
        sp, sq = side(p), side(q)
        if sp >= 0:
            out.append(p)
        if (sp >= 0) != (sq >= 0):
            t = sp / (sp - sq)
            out.append((p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t))
    return out


def area(poly):
    s = 0.0
    for i in range(len(poly)):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % len(poly)]
        s += x0 * y1 - x1 * y0
    return abs(s) * 0.5


def ccw(t):
    if (t[1][0] - t[0][0]) * (t[2][1] - t[0][1]) - (t[1][1] - t[0][1]) * (t[2][0] - t[0][0]) < 0:
        return [t[0], t[2], t[1]]
    return list(t)


def overlap(t1, t2):
    """The polygon two 2D triangles share."""
    poly = ccw(t1)
    t2 = ccw(t2)
    for i in range(3):
        poly = clip(poly, t2[i], t2[(i + 1) % 3])
        if not poly:
            return []
    return poly


class Faces:
    """Triangles with owners, bucketed by plane (normal direction + offset) for the pair search."""
    def __init__(self, tol, solids=(), same_ok=None):
        self.tol = tol
        self.solids = solids       # [(centre, R, half size)]: boxes that hide what's inside them
        self.same_ok = same_ok     # (a, b) -> True when the two look the same (one material)
        self.tris, self.owner, self.normal, self.d = [], [], [], []

    def add(self, tris, owner):
        for t in tris:
            n = np.cross(t[1] - t[0], t[2] - t[0])
            ln = np.linalg.norm(n)
            if ln < 1e-9:
                continue
            n = n / ln
            self.tris.append(t)
            self.owner.append(owner)
            self.normal.append(n)
            self.d.append(float(np.dot(n, t[0])))

    def hidden(self, poly, u, v, n, d):
        """True when the shared patch, a hair in front of it, is inside a solid at every sample."""
        if not self.solids:
            return False
        cx = sum(p[0] for p in poly) / len(poly)
        cy = sum(p[1] for p in poly) / len(poly)
        samples = [(cx, cy)] + [(cx + (p[0] - cx) * 0.9, cy + (p[1] - cy) * 0.9) for p in poly]
        for (a, b) in samples:
            q = n * (d + 0.3) + u * a + v * b
            if not any(np.all(np.abs(R.T @ (q - c)) < h) for c, R, h in self.solids):
                return False
        return True

    def fights(self, same_owner=False, pairs_of=None):
        """[(owner a, owner b, area, normal, offset)] of overlapping same-facing coplanar triangles."""
        found = {}
        keyed = {}
        for i, n in enumerate(self.normal):
            k = tuple(np.round(n, 3))
            keyed.setdefault(k, []).append(i)
        for k, idx in keyed.items():
            idx.sort(key=lambda i: self.d[i])
            n = np.array(k)
            # A 2D frame in the plane.
            u = np.cross(n, [0, 0, 1] if abs(n[2]) < 0.9 else [1, 0, 0])
            u /= np.linalg.norm(u)
            v = np.cross(n, u)
            for a in range(len(idx)):
                i = idx[a]
                for b in range(a + 1, len(idx)):
                    j = idx[b]
                    if self.d[j] - self.d[i] > self.tol:
                        break
                    oi, oj = self.owner[i], self.owner[j]
                    if oi == oj and not same_owner:
                        continue
                    if pairs_of and not pairs_of(oi, oj):
                        continue
                    ti = [(float(np.dot(p, u)), float(np.dot(p, v))) for p in self.tris[i]]
                    tj = [(float(np.dot(p, u)), float(np.dot(p, v))) for p in self.tris[j]]
                    # Quick bounds test.
                    if (max(p[0] for p in ti) <= min(p[0] for p in tj) or max(p[0] for p in tj) <= min(p[0] for p in ti)
                            or max(p[1] for p in ti) <= min(p[1] for p in tj) or max(p[1] for p in tj) <= min(p[1] for p in ti)):
                        continue
                    poly = overlap(ti, tj)
                    ar = area(poly) if poly else 0.0
                    if ar > MIN_AREA and self.same_ok and self.same_ok(oi, oj):
                        continue
                    if ar > MIN_AREA and self.hidden(poly, u, v, n, self.d[i]):
                        continue
                    if ar > MIN_AREA:
                        key = tuple(sorted((oi, oj)))
                        f = found.setdefault(key, [0.0, n, self.d[i]])
                        f[0] += ar
        return [(a, b, f[0], f[1], f[2]) for (a, b), f in found.items()]


# --- glb reading ---------------------------------------------------------------------------------

def read_glb(path):
    """[(node name, material name, triangles (n, 3, 3) in metres, glTF frame)] of a .glb."""
    data = open(path, "rb").read()
    _, _, total = struct.unpack_from("<III", data, 0)
    off, js, binc = 12, None, b""
    while off < total:
        ln, kind = struct.unpack_from("<II", data, off)
        chunk = data[off + 8: off + 8 + ln]
        if kind == 0x4E4F534A:
            js = json.loads(chunk)
        else:
            binc = chunk
        off += 8 + ln
    comp = {5126: ("f", 4), 5123: ("H", 2), 5125: ("I", 4), 5121: ("B", 1)}
    ncomp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}

    def accessor(i):
        a = js["accessors"][i]
        bv = js["bufferViews"][a["bufferView"]]
        fmt, sz = comp[a["componentType"]]
        nc = ncomp[a["type"]]
        start = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = bv.get("byteStride", sz * nc)
        dt = np.dtype("<" + fmt)
        out = np.empty((a["count"], nc), dt)
        for k in range(a["count"]):
            out[k] = np.frombuffer(binc, dt, nc, start + k * stride)
        return out

    def local(node):
        m = np.eye(4)
        if "matrix" in node:
            return np.array(node["matrix"]).reshape(4, 4).T
        t = node.get("translation", [0, 0, 0])
        r = node.get("rotation", [0, 0, 0, 1])
        s = node.get("scale", [1, 1, 1])
        x, y, z, w = r
        R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                      [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                      [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
        m[:3, :3] = R * np.array(s)
        m[:3, 3] = t
        return m

    out = []
    mats = [m.get("name", "") for m in js.get("materials", [])]

    def walk(ni, parent):
        node = js["nodes"][ni]
        M = parent @ local(node)
        if "mesh" in node:
            for p in js["meshes"][node["mesh"]]["primitives"]:
                if p.get("mode", 4) != 4:
                    continue
                pos = accessor(p["attributes"]["POSITION"]).astype(float)
                pos = pos @ M[:3, :3].T + M[:3, 3]
                idx = accessor(p["indices"]).reshape(-1) if "indices" in p else np.arange(len(pos))
                tris = pos[idx.reshape(-1, 3)]
                out.append((node.get("name", "?"), mats[p["material"]] if "material" in p else "", tris))
        for c in node.get("children", []):
            walk(c, M)

    for ni in js["scenes"][js.get("scene", 0)]["nodes"]:
        walk(ni, np.eye(4))
    return out


def prop_tris_in_shed(tris, at, yaw):
    """A prop's glTF triangles (m; y up, +z its front) placed in the shed (cm) as ue_import_props
    does: UE's glTF import makes glTF x -> UE x, glTF z -> UE y (mirrored), glTF y -> UE z; then
    the swap's yaw about z (0: its front faces +y)."""
    p = tris * 100.0
    ue = np.stack([p[..., 0], p[..., 2], p[..., 1]], axis=-1)
    R = rot_matrix(0.0, 0.0, yaw)
    # Swapping glTF's y and z mirrors the triangles: UE's importer keeps them facing out (glTF is
    # right handed, UE left handed), so here the winding turns back.
    return (ue @ R.T + np.array(at))[:, ::-1, :]


# --- The checks -----------------------------------------------------------------------------------

def swap_hidden(labels):
    spec = json.load(open(os.path.join(ROOT, "Data", "Props", "swaps.json")))
    hidden = set()
    for s in spec["swaps"]:
        for lab in labels:
            if any(lab.startswith(h[:-1]) if h.endswith("*") else lab == h for h in s["hide"]):
                hidden.add(lab)
    return spec, hidden


def level_faces():
    run_shed()
    labels = [r["label"] for r in SPAWNED]
    spec, hidden = swap_hidden(labels)
    shapes, solids = {}, []
    for r in SPAWNED:
        if r["mesh"] not in ("cube", "cylinder") or r["label"] in hidden:
            continue
        R = rot_matrix(*r["rot"])
        size = np.array(r["scale"]) * 100.0
        tris = cube_tris(r["centre"], size, R) if r["mesh"] == "cube" else cylinder_tris(r["centre"], size, R)
        shapes[r["label"]] = (tris, r["folder"], r.get("material"))
        if r["mesh"] == "cube":
            solids.append((np.array(r["centre"]), R, size * 0.5))
    return spec, shapes, solids


def main(argv):
    only_props = "props" in argv
    bad = 0
    blender = os.path.join(ROOT, "Data", "Props", "Blender")
    if not only_props:
        spec, shapes, solids = level_faces()
        mats = {lab: v[2] for lab, v in shapes.items()}
        f = Faces(PLANE_TOL, solids, lambda a, b: a in mats and b in mats and mats[a] == mats[b])
        for lab, (tris, folder, mat) in shapes.items():
            f.add(tris, lab)
        # The swapped-in props' outsides against the level's boxes.
        placed = []
        for s in spec["swaps"] + spec.get("dress", []):
            for prop in [s["prop"]]:
                g = os.path.join(blender, prop + ".glb")
                if os.path.exists(g):
                    for node, mat, tris in read_glb(g):
                        f.add(prop_tris_in_shed(tris, s["at"], s.get("yaw", 0.0)), "Prop_%s/%s" % (prop, node))
                    placed.append(prop)
        fights = f.fights(pairs_of=lambda a, b: not (a.startswith("Prop_") and b.startswith("Prop_")
                                                         and a.split("/")[0] == b.split("/")[0]))
        print("level: %d shapes visible, %d props placed" % (len(shapes), len(placed)))
        for a, b, ar, n, d in sorted(fights, key=lambda x: -x[2]):
            print("  FIGHT %-34s %-34s %9.1f cm2  facing %s at %.2f" % (a, b, ar, np.round(n, 2), d))
        bad += len(fights)
    props = sorted(p[:-4] for p in os.listdir(blender) if p.endswith(".glb"))
    for prop in props:
        f = Faces(PROP_TOL / 100.0 * 100.0)   # the glb is in metres: tolerance in m below
        f.tol = PROP_TOL / 100.0
        for node, mat, tris in read_glb(os.path.join(blender, prop + ".glb")):
            f.add(tris, "%s[%s]" % (node, mat))
        global MIN_AREA
        keep = MIN_AREA
        MIN_AREA = 1e-6   # m^2: a square millimetre
        # Faces down on the floor (y 0) and backs on the wall (wall kit's origin is on the wall, z 0)
        # are never seen.
        fights = [x for x in f.fights() if not (abs(x[4]) < 1e-4 and (x[3][1] < -0.99 or x[3][2] < -0.99))]
        MIN_AREA = keep
        for a, b, ar, n, d in sorted(fights, key=lambda x: -x[2]):
            print("  PROP %-24s %-40s %-40s %8.1f cm2" % (prop, a, b, ar * 1e4))
        bad += len(fights)
    print("%d z-fights" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
