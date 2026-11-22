#!/usr/bin/env python3
"""Does anything cut through a mirror's glass? Measured on the files the game loads (beenster, build 166:
"some mirrors still have geometry poking through, please fix ALL of them").

    python3 BikesV2/foundry/mirror_check.py [file.glb ...]      (default: every bike, kit and common mirror)

For each GLB: the triangles whose surface is mirror glass against every other triangle in the file. A file
passes when none cross, as drawn and with each head turned on its ball joint as far as the game may turn it
to aim it at the rider's eyes (TILT degrees, eight ways round: ATorqueBikePawn::AimMirrors). Every glass must
also have a head of its own (surface tag mirror_head) for the game to turn with it. Prints one line per failing
file and exits 1 if any fails.
"""
import glob, json, math, os, sys
import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

BALL = 0.014      # the ball joint: this far behind the middle of the glass (detail3d.BALL, the game's 1.4 cm)
TILT = float(os.environ.get("MIRROR_TILT", "20"))   # the furthest the game turns a head (MirrorAim MaxAngle 0.35 rad)

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Data", "BikesV2")


def check(path):
    tags = {}
    jp = path[:-4] + ".json"
    if os.path.exists(jp):
        tags = {k: str(v.get("tag", "")) + str(v.get("ue", "")) for k, v in json.load(open(jp)).get("surfaces", {}).items()}
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)
    glass, rest, head = [], [], []
    for ob in bpy.data.objects:
        if ob.type != "MESH":
            continue
        me = ob.data
        me.calc_loop_triangles()
        mw = ob.matrix_world
        co = [mw @ v.co for v in me.vertices]
        for t in me.loop_triangles:
            mat = me.materials[t.material_index].name if me.materials else ""
            mat = mat.split(".")[0]
            is_glass = "mirrorglass" in mat or "mirror_glass" in tags.get(mat, "") or "mirrorglass" in tags.get(mat, "")
            is_head = not is_glass and ("mirror_head" in mat or "mirror_head" in tags.get(mat, ""))
            (glass if is_glass else head if is_head else rest).append([co[i] for i in t.vertices])
    if not glass:
        return None
    def tree(tris):
        vs = [p for t in tris for p in t]
        return BVHTree.FromPolygons(vs, [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(len(tris))], epsilon=0.0)
    crossings = tree(glass).overlap(tree(rest + head)) if rest or head else []
    allrest = rest + head
    rest_tree = tree(rest) if rest else None
    # how far the worst of it stands proud of the glass (mm), per glass island (left and right by the sign of y,
    # or of x for a part drawn on its side: split on the axis the glass centres are furthest apart on)
    cents = [sum(t, Vector()) / 3 for t in glass]
    axis = max(range(3), key=lambda i: max(c[i] for c in cents) - min(c[i] for c in cents))
    mid = (max(c[axis] for c in cents) + min(c[axis] for c in cents)) / 2
    proud = 0.0
    tilted, headless = 0, 0
    for sign in (-1, 1):
        isl = [t for t, c in zip(glass, cents) if (c[axis] - mid) * sign >= 0]
        if not isl:
            continue
        pts = [p for t in isl for p in t]
        gc = sum(pts, Vector()) / len(pts)
        n = sum(((t[1] - t[0]).cross(t[2] - t[0]) for t in isl), Vector()).normalized()
        for _, j in crossings:
            for p in allrest[j]:
                if abs((p - gc)[axis]) < 0.2:
                    proud = max(proud, abs((p - gc).dot(n)))
        # its own head, and which side of the glass that is
        mine = [t for t in head if (sum(t, Vector()) / 3 - gc).length < 0.12]
        if not mine:
            headless += 1
            continue
        hc = sum((p for t in mine for p in t), Vector()) / (3 * len(mine))
        if (hc - gc).dot(n) > 0:
            n = -n
        ball = gc - n * BALL
        side = n.cross(Vector((0, 0, 1))).normalized()
        up = side.cross(n).normalized()
        worst = 0
        for k in range(8):
            ax = (side * math.cos(k * math.pi / 4) + up * math.sin(k * math.pi / 4)).normalized()
            for deg in (TILT / 2, TILT):
                rot = Matrix.Rotation(math.radians(deg), 3, ax)
                moved = [[ball + rot @ (p - ball) for p in t] for t in isl]
                if rest_tree:
                    worst = max(worst, len(set(i for i, _ in tree(moved).overlap(rest_tree))))
        tilted += worst
    return len(set(j for _, j in crossings)), proud * 1000, tilted, headless


def main():
    files = [a for a in sys.argv[1:] if a.endswith(".glb")]
    if not files:
        files = sorted(glob.glob(os.path.join(DATA, "*.glb")) + glob.glob(os.path.join(DATA, "kits", "*.glb")) +
                       glob.glob(os.path.join(DATA, "common", "*", "*.glb")))
    bad = seen = 0
    for f in files:
        r = check(f)
        if r is None:
            continue
        seen += 1
        if r[0] or r[2] or r[3]:
            bad += 1
            why = ([f"{r[0]} triangles cross the glass, up to {r[1]:.1f} mm through"] if r[0] else []) + \
                  ([f"{r[2]} glass triangles are cut when the head is turned up to {TILT:g} degrees"] if r[2] else []) + \
                  ([f"{r[3]} glass without a head of its own to turn with"] if r[3] else [])
            print(f"POKE {os.path.relpath(f, DATA)}: " + "; ".join(why))
    print(f"{seen} files with mirror glass, {bad} failing (through the glass as drawn, or turned {TILT:g} degrees, or no head)")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
