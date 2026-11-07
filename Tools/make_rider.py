"""Build the prototype's rider: a skinned body in riding kit (jacket, jeans, boots, brown gloves) and a
full-face helmet, sat on the Speed Twin with hands on the grips and feet on the pegs.

    python Tools/make_rider.py           writes Data/Rider/rider.glb, helmet.glb and rider.json
    python Tools/make_rider.py --coarse  a quicker, rougher build for trying shapes

Needs numpy and scikit-image (pip install scikit-image).

How it's made. Each layer of kit is a signed distance field (smooth unions of round cones and
ellipsoids, with seams, folds and armour cut in), polygonised by marching cubes. Where one layer
goes inside another (the jacket's hem over the jeans, the gloves over the cuffs) its triangles are
clipped at the other's surface, so every edge is crisp and nothing hidden is kept. The rest pose is
the riding pose, so the pawn's posing only ever moves it a little from where it was made.

Frames. Shapes are built in the pawn's local frame (centimetres, x forward, y right, z up, from the
body origin: axle height mid wheelbase). The glbs are written in the bike's model frame (metres,
x forward, y up, z right), like Tools/make_speedtwin.py, so the level script and the pawn treat
them the same way. rider.json lists the bones' rest positions in the pawn's frame (cm), which the
pawn uses to find how the importer turned the skeleton.

The skeleton has Unreal mannequin bone names (pelvis, spine_01..03, neck_01, head, clavicle_l,
upperarm_l, lowerarm_l, hand_l, thigh_l, calf_l, foot_l, ball_l and the _r side).
"""
import argparse, json, math, os, struct
import numpy as np
from skimage.measure import marching_cubes

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Data", "Rider")
COARSE = 1.0   # set by --coarse: voxel sizes are multiplied by this


def unit(v):
    v = np.asarray(v, np.float64)
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def mid_joint(root, tip, a, b, pole):
    """The middle joint of a two-bone limb from root to tip (lengths a then b), bending towards pole.
    The same as MidJoint in TorqueBikePawn.cpp."""
    d = tip - root
    ln = min(max(np.linalg.norm(d), abs(a - b) + 0.1), (a + b) * 0.999)
    d = unit(d)
    along = (a * a - b * b + ln * ln) / (2 * ln)
    out = math.sqrt(max(a * a - along * along, 0.0))
    side = unit(np.asarray(pole, np.float64) - d * np.dot(pole, d))
    return root + d * along + side * out


# --- Where the bike puts the rider (pawn local cm, from Tools/make_speedtwin.py) ---------------------

GRIP = 41.32, 31.5, 65.54           # grip centre (x, |y|, z): the grips are 34 mm across
GRIP_R = 1.7
PEG = -44.0, 21.0, 16.5             # footrest tube (x, inner |y|, z), 12 mm radius


def grip_frame(s):
    """Grip centre, outward axis along the grip, forward and up square to it (s = -1 left, +1 right)."""
    c = np.array([GRIP[0], GRIP[1] * s, GRIP[2]])
    a = unit([0.0415, 0.9957 * s, -0.083])
    up = unit(np.array([0, 0, 1.0]) - a * a[2])
    fw = unit(np.cross(a, up) * s)
    return c, a, fw, up


def on_grip(s, theta_deg, r, along):
    """A point around the grip: theta from forward towards up, r from the grip's axis (cm), along
    the grip outwards from its centre."""
    c, a, fw, up = grip_frame(s)
    t = math.radians(theta_deg)
    return c + a * along + fw * (r * math.cos(t)) + up * (r * math.sin(t))


# --- The skeleton, in the riding pose ----------------------------------------------------------------

UPPER_ARM, FOREARM = 32.0, 28.0
THIGH, SHIN = 41.0, 41.0
KNEE_POLE = (0.93, 0.32, -0.19)    # knees forward and a little out, round the engine
ELBOW_POLE = (-0.25, 0.8, -0.55)


def skeleton():
    J = {}
    J["pelvis"] = np.array([-44.0, 0.0, 60.5])
    J["spine_01"] = np.array([-40.0, 0.0, 69.5])
    J["spine_02"] = np.array([-33.0, 0.0, 81.0])
    J["spine_03"] = np.array([-23.0, 0.0, 93.0])
    J["neck_01"] = np.array([-11.0, 0.0, 106.0])
    J["head"] = np.array([-7.0, 0.0, 112.5])
    parents = {"pelvis": None, "spine_01": "pelvis", "spine_02": "spine_01", "spine_03": "spine_02",
               "neck_01": "spine_03", "head": "neck_01"}
    for s, x in ((-1, "l"), (1, "r")):
        J["clavicle_" + x] = np.array([-14.0, 3.0 * s, 100.0])
        S = np.array([-9.0, 18.0 * s, 97.0])
        W = on_grip(s, 152.0, 7.2, -0.6)
        E = mid_joint(S, W, UPPER_ARM, FOREARM, unit([ELBOW_POLE[0], ELBOW_POLE[1] * s, ELBOW_POLE[2]]))
        J["upperarm_" + x], J["lowerarm_" + x], J["hand_" + x] = S, E, W
        H = np.array([-44.0, 10.5 * s, 60.0])
        A = np.array([-56.0, 23.5 * s, 24.5])
        K = mid_joint(H, A, THIGH, SHIN, unit([KNEE_POLE[0], KNEE_POLE[1] * s, KNEE_POLE[2]]))
        J["thigh_" + x], J["calf_" + x], J["foot_" + x] = H, K, A
        J["ball_" + x] = np.array([-43.0, 24.5 * s, 20.5])
        parents.update({"clavicle_" + x: "spine_03", "upperarm_" + x: "clavicle_" + x,
                        "lowerarm_" + x: "upperarm_" + x, "hand_" + x: "lowerarm_" + x,
                        "thigh_" + x: "pelvis", "calf_" + x: "thigh_" + x, "foot_" + x: "calf_" + x,
                        "ball_" + x: "foot_" + x})
    return J, parents


J, PARENTS = skeleton()
BONES = list(J.keys())          # parents come before their children
SPINE_U = unit(J["neck_01"] - J["pelvis"])               # up the back
SPINE_F = unit(np.cross(SPINE_U, [0, 1.0, 0]))            # out of the chest, square to the back
SPINE_F = SPINE_F if SPINE_F[0] > 0 else -SPINE_F
HELMET_C = J["head"] + np.array([2.0, 0.0, 9.0])         # the helmet's centre


# --- Distance fields ---------------------------------------------------------------------------------

def rcone(p, a, b, ra, rb):
    """A round cone from a (radius ra) to b (radius rb)."""
    a = np.asarray(a, np.float64); ba = np.asarray(b, np.float64) - a
    t = np.clip(((p - a) @ ba) / (ba @ ba), 0.0, 1.0)
    q = a + t[:, None] * ba
    return np.linalg.norm(p - q, axis=1) - (ra + (rb - ra) * t)


def ellipsoid(p, c, radii, axes=None):
    """An ellipsoid at c with radii along axes (rows; default x, y, z). A close bound, not exact."""
    q = p - np.asarray(c, np.float64)
    if axes is not None:
        q = q @ np.asarray(axes, np.float64).T
    r = np.asarray(radii, np.float64)
    k0 = np.linalg.norm(q / r, axis=1)
    k1 = np.linalg.norm(q / (r * r), axis=1)
    return k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)


def rbox(p, c, half, axes=None, r=0.5):
    """A rounded box at c, half sizes along axes (rows), corners rounded by r."""
    q = p - np.asarray(c, np.float64)
    if axes is not None:
        q = q @ np.asarray(axes, np.float64).T
    d = np.abs(q) - (np.asarray(half, np.float64) - r)
    return np.linalg.norm(np.maximum(d, 0.0), axis=1) + np.minimum(d.max(1), 0.0) - r


def plane(p, point, normal):
    """Positive on the side the normal points to."""
    return (p - np.asarray(point, np.float64)) @ unit(normal)


def smin(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b + (a - b) * h - k * h * (1.0 - h)


def smax(a, b, k):
    return -smin(-a, -b, k)


def groove(d, dist_to_line, width, depth):
    """A stitched seam: a shallow round groove where dist_to_line is near zero."""
    return d + depth * np.exp(-(dist_to_line / width) ** 2)


def seg_dist(p, a, b):
    a = np.asarray(a, np.float64); ba = np.asarray(b, np.float64) - a
    t = np.clip(((p - a) @ ba) / (ba @ ba), 0.0, 1.0)
    return np.linalg.norm(p - (a + t[:, None] * ba), axis=1), t


def fold_noise(p, scale, seed):
    """Smooth value noise in 3D (sum of a few random plane waves): for cloth that isn't too regular."""
    rng = np.random.default_rng(seed)
    out = np.zeros(len(p))
    for _ in range(4):
        k = unit(rng.normal(size=3)) * (2 * math.pi / scale) * rng.uniform(0.7, 1.4)
        out += np.sin(p @ k + rng.uniform(0, 2 * math.pi))
    return out / 2.0


# The kit, layer by layer. Each returns the layer's distance and, for skinning, the distances of its
# parts ("groups").

def spine_axes():
    return np.stack([SPINE_U, np.array([0, 1.0, 0]), SPINE_F])


def arm_points(s):
    x = "l" if s < 0 else "r"
    S, E, W = J["upperarm_" + x], J["lowerarm_" + x], J["hand_" + x]
    return S, E, W


def loft(p, table, power=2.4, ends=2.0):
    """A body lofted up the spine: rows of (u, half width, half depth, forward offset), u in cm up
    the spine from the pelvis. Superellipse sections; a close bound, not an exact distance."""
    T = np.asarray(table, np.float64)
    rel = p - J["pelvis"]
    u = rel @ SPINE_U
    uc = np.clip(u, T[0, 0], T[-1, 0])
    w = np.interp(uc, T[:, 0], T[:, 1]); dp = np.interp(uc, T[:, 0], T[:, 2]); fo = np.interp(uc, T[:, 0], T[:, 3])
    y = rel[:, 1]; f = rel @ SPINE_F - fo
    k = (np.abs(y / w) ** power + np.abs(f / dp) ** power) ** (1.0 / power)
    d = (k - 1.0) * np.minimum(w, dp)
    return smax(smax(d, T[0, 0] - u, ends), u - T[-1, 0], ends)


TORSO = ((-4, 15.5, 11.5, 0.0), (6, 15.0, 11.0, 1.0), (16, 15.5, 11.5, 2.0), (28, 17.0, 12.5, 3.0),
         (38, 18.0, 12.5, 3.5), (46, 18.0, 11.0, 3.5), (51, 15.0, 9.0, 3.0), (55, 10.0, 7.5, 2.5),
         (57, 7.0, 6.5, 2.5))
HIPS = ((-9, 12.0, 9.0, -1.0), (-4, 16.0, 11.5, -0.5), (4, 15.6, 10.8, 0.5), (10, 15.0, 10.5, 1.0))


def jacket(p):
    P = J["pelvis"]
    torso = loft(p, TORSO)
    # The hem, at the belt, slightly lower at the back (it rides up at the front sat down).
    hem_n = unit(SPINE_U + SPINE_F * 0.18)
    hem = -plane(p, P + SPINE_U * 4.0, hem_n)
    torso_cut = smax(torso, hem, 0.6)
    # A waistband: a slightly proud band just above the hem.
    band = np.abs(plane(p, P + SPINE_U * 6.2, hem_n)) - 2.2
    torso_cut = torso_cut - 0.25 * np.clip(-band / 0.6, 0, 1)
    # The front zip: a groove down the middle of the chest.
    front = (p - P) @ SPINE_F
    torso_cut = np.where(front > 0, groove(torso_cut, p[:, 1], 0.35, 0.18), torso_cut)
    # Seams: across the chest above the pockets, and the back yoke.
    torso_cut = groove(torso_cut, np.abs((p - P) @ SPINE_U - 36.0), 0.25, 0.12)
    # Collar: a stand-up band round the neck, its top edge a ridge.
    N = J["neck_01"]
    neck_dir = unit(J["head"] - J["spine_03"])
    collar = rcone(p, N - neck_dir * 4.0, N + neck_dir * 4.5, 7.6, 6.6)
    collar = smax(collar, -plane(p, N - neck_dir * 6.0, neck_dir), 1.0)
    collar = groove(collar, np.abs((p - N) @ neck_dir - 1.2), 0.3, 0.12)
    groups = {"torso": smin(torso_cut, collar, 1.2)}
    out = groups["torso"]
    # Sleeves, with shoulder and elbow armour under the shell and folds at the inner elbow.
    for s, x in ((-1, "l"), (1, "r")):
        S, E, W = arm_points(s)
        cuff = W + unit(E - W) * 3.0
        upper = rcone(p, S, E, 6.3, 5.2)
        fore = rcone(p, E, cuff, 5.2, 4.25)
        arm = smin(upper, fore, 2.5)
        # Elbow pad: a flattened ellipsoid on the outside of the elbow, pointing back and out.
        out_dir = unit(np.cross(unit(E - S), unit(W - E)) * -s + np.array([0, 0.4 * s, -0.2]))
        pad = ellipsoid(p, E + out_dir * 2.6 + unit(W - E) * 2.0,
                        (5.0, 4.0, 2.4), np.stack([unit(W - E), unit(np.cross(out_dir, unit(W - E))), out_dir]))
        arm = smin(arm, pad, 1.2)
        # Shoulder pad: on the outside of the upper arm.
        sp_dir = unit(np.array([0.0, s, 0.7]))
        spad = ellipsoid(p, S + sp_dir * 3.0 + unit(E - S) * 3.0, (6.0, 5.0, 2.5),
                         np.stack([unit(E - S), unit(np.cross(sp_dir, unit(E - S))), sp_dir]))
        arm = smin(arm, spad, 1.5)
        # Folds bunched in the crook of the elbow and along the forearm.
        dE = np.linalg.norm(p - E, axis=1)
        crook = np.clip(1.0 - dE / 9.0, 0, 1) ** 1.5
        along = (p - E) @ unit(W - E)
        arm = arm + crook * 0.45 * np.sin(along * 1.6 + 0.8 * fold_noise(p, 9.0, 3 + s))
        arm = arm + 0.12 * fold_noise(p, 5.0, 7 + s)
        # The sleeve's end: cut square, with a turned cuff.
        arm = smax(arm, plane(p, cuff, unit(W - E)), 0.4)
        groups["arm_" + x] = arm
        out = smin(out, arm, 3.0)
    return out, groups


def jeans(p):
    seat = loft(p, HIPS, 2.3, 3.0)
    # Flattened where it sits on the seat.
    seat = smax(seat, -plane(p, (0, 0, 52.8), (0, 0, 1.0)), 2.0)
    groups = {"hips": seat}
    out = seat
    for s, x in ((-1, "l"), (1, "r")):
        H, K, A = J["thigh_" + x], J["calf_" + x], J["foot_" + x]
        thigh = rcone(p, H, K, 8.9, 6.2)
        # Sat on, so flatter underneath where it meets the seat.
        thigh = smax(thigh, np.where((p[:, 0] < -30.0) & (np.abs(p[:, 1]) < 13.0), 52.9 - p[:, 2], -1e3), 2.5)
        shin_end = A + unit(K - A) * 5.0
        shin = rcone(p, K, shin_end, 6.0, 5.3)
        leg = smin(thigh, shin, 3.0)
        # Denim bunches behind the knee and in the hip crease, and stretches over the thigh.
        dK = np.linalg.norm(p - K, axis=1)
        knee = np.clip(1.0 - dK / 12.0, 0, 1) ** 1.2
        leg = leg + knee * 0.55 * np.sin(((p - K) @ unit(A - K)) * 1.3 + fold_noise(p, 8.0, 11 + s))
        dH = np.linalg.norm(p - (H + unit(K - H) * 8.0), axis=1)
        crease = np.clip(1.0 - dH / 13.0, 0, 1)
        leg = leg + crease * 0.4 * np.sin(((p - H) @ unit(K - H)) * 1.1 + fold_noise(p, 10.0, 13 + s))
        leg = leg + 0.1 * fold_noise(p, 4.0, 17 + s)
        # Outer seam down the leg.
        side = unit(np.cross(unit(K - H), np.array([0, 0, 1.0]))) * -s
        leg = groove(leg, np.abs((p - H) @ side - 7.5), 0.3, 0.1)
        # The hem, square, a little flared.
        leg = smax(leg, plane(p, shin_end, unit(A - K)), 0.4)
        groups["leg_" + x] = leg
        out = smin(out, leg, 3.5)
    return out, groups


def foot_frame(x):
    A, B = J["foot_" + x], J["ball_" + x]
    fw = B - A; fw[2] = 0.0; fw = unit(fw)
    up = np.array([0, 0, 1.0])
    return A, B, fw, unit(np.cross(up, fw)), up


def boots(p):
    out = np.full(len(p), 1e9); groups = {}
    for s, x in ((-1, "l"), (1, "r")):
        A, B, fw, side, up = foot_frame(x)
        K = J["calf_" + x]
        shaft = rcone(p, A + unit(K - A) * 15.0, A, 5.0, 4.9)
        ankle = ellipsoid(p, A + fw * 0.5 - up * 1.0, (5.6, 4.6, 5.2), np.stack([fw, side, up]))
        heel = ellipsoid(p, A - fw * 3.6 - up * 5.0, (4.6, 4.2, 3.8), np.stack([fw, side, up]))
        toe = ellipsoid(p, B + fw * 3.5 - up * 0.6, (8.0, 4.8, 3.6), np.stack([fw, side, up]))
        bt = smin(smin(shaft, ankle, 2.0), smin(heel, toe, 3.0), 2.5)
        # Boots stop at the sole's top.
        sole_top = B[2] - 3.3 + 1.5
        bt = smax(bt, -plane(p, (0, 0, sole_top - 0.2), up), 0.3)
        # The shift pad on the left toe, and a zip down the inner side.
        if s < 0:
            bt = smin(bt, ellipsoid(p, B + fw * 3.0 + up * 3.0, (4.0, 3.5, 0.8), np.stack([fw, side, unit(up + fw * 0.3)])), 0.6)
        bt = groove(bt, np.abs((p - A) @ side * s + 4.2), 0.25, 0.1)
        groups["boot_" + x] = bt
        out = np.minimum(out, bt)
    return out, groups


def soles(p):
    out = np.full(len(p), 1e9); groups = {}
    for s, x in ((-1, "l"), (1, "r")):
        A, B, fw, side, up = foot_frame(x)
        bottom = B[2] - 3.3
        a = A - fw * 8.6; b = B + fw * 10.3
        c = (a + b) / 2; c[2] = bottom + 0.9
        half_len = np.linalg.norm(b - a) / 2
        sole = rbox(p, c, (half_len, 5.0, 0.9), np.stack([fw, side, up]), 0.6)
        # The heel block, and a waist under the arch where the peg sits.
        sole = smin(sole, rbox(p, A - fw * 5.0 + up * 0.0 + np.array([0, 0, bottom + 1.6 - A[2]]), (3.6, 4.4, 1.6),
                               np.stack([fw, side, up]), 0.5), 0.3)
        # Tread grooves underneath.
        under = np.clip(1.0 - (p[:, 2] - bottom) / 0.6, 0, 1)
        sole = sole + under * 0.15 * (np.sin((p - A) @ fw * 2.2) > 0.4)
        groups["sole_" + x] = sole
        out = np.minimum(out, sole)
    return out, groups


FINGERS = ((-3.15, 1.0, 1.06), (-1.05, 1.05, 1.1), (1.05, 1.0, 1.05), (3.1, 0.86, 0.95))   # along, length, width


def gloves(p):
    out = np.full(len(p), 1e9); groups = {}
    for s, x in ((-1, "l"), (1, "r")):
        S, E, W = arm_points(s)
        c, a, fw, up = grip_frame(s)
        inner = -1.0   # towards the bike: along the grip, negative is inwards
        # Palm and back of the hand, from the wrist over the top of the grip to the knuckles.
        palm_c = on_grip(s, 118.0, 4.4, 0.0)
        tang = unit(on_grip(s, 70.0, 4.3, 0.0) - on_grip(s, 160.0, 5.6, 0.0))
        nrm = unit(palm_c - c - a * ((palm_c - c) @ a))
        hand = ellipsoid(p, palm_c, (4.8, 4.9, 1.9), np.stack([tang, a, nrm]))
        hand = smin(hand, rcone(p, on_grip(s, 165.0, 4.2, -0.4), on_grip(s, 150.0, 6.6, -0.5), 2.6, 2.9), 1.5)
        # Knuckles.
        k0 = on_grip(s, 72.0, 4.5, FINGERS[0][0] * 1.0); k1 = on_grip(s, 72.0, 4.4, FINGERS[3][0] * 1.0)
        hand = smin(hand, rcone(p, k0, k1, 1.45, 1.25), 1.0)
        # Fingers wrapped round the grip.
        for along, ln, wd in FINGERS:
            pts = [on_grip(s, 72.0, 4.4, along * inner * -1.0),
                   on_grip(s, 72.0 - 70.0 * ln, 2.95, along * inner * -1.0 * 1.02),
                   on_grip(s, 72.0 - 70.0 * ln - 72.0 * ln, 2.8, along * inner * -1.0 * 1.04),
                   on_grip(s, 72.0 - 70.0 * ln - 72.0 * ln - 48.0 * ln, 2.7, along * inner * -1.0 * 1.04)]
            r = (1.08 * wd, 1.0 * wd, 0.93 * wd, 0.86 * wd)
            f = rcone(p, pts[0], pts[1], r[0], r[1])
            f = smin(f, rcone(p, pts[1], pts[2], r[1], r[2]), 0.3)
            f = smin(f, rcone(p, pts[2], pts[3], r[2], r[3]), 0.3)
            # Leather creases across the knuckle joints.
            for j in (1, 2):
                f = groove(f, np.linalg.norm(p - pts[j], axis=1) - r[j] * 0.9, 0.25, 0.08)
            hand = smin(hand, f, 0.45)
        # Thumb: from the inner side of the palm, under the grip.
        t0 = on_grip(s, 175.0, 4.6, -3.9); t1 = on_grip(s, -150.0, 3.2, -4.6); t2 = on_grip(s, -112.0, 2.9, -3.6)
        th = smin(rcone(p, t0, t1, 1.55, 1.25), rcone(p, t1, t2, 1.25, 1.05), 0.3)
        hand = smin(hand, th, 1.0)
        # Knuckle armour: a low hard pad over the knuckles.
        kp = on_grip(s, 85.0, 5.7, -0.1)
        hand = smin(hand, ellipsoid(p, kp, (1.6, 4.6, 0.75), np.stack([tang, a, nrm])), 0.5)
        # The gauntlet over the sleeve, and the wrist strap.
        back = unit(E - W)
        gaunt = rcone(p, W + back * -0.5, W + back * 7.5, 4.0, 5.0)
        gaunt = smax(gaunt, plane(p, W + back * 7.5, back), 0.3)
        gaunt = gaunt - 0.25 * np.clip(1.0 - np.abs((p - W) @ back - 2.0) / 1.0, 0, 1)
        hand = smin(hand, gaunt, 1.8)
        groups["glove_" + x] = hand
        out = np.minimum(out, hand)
    return out, groups


LAYERS = {"jacket": jacket, "jeans": jeans, "boots": boots, "sole": soles, "gloves": gloves}
VOXEL = {"jacket": 0.7, "jeans": 0.7, "boots": 0.5, "sole": 0.45, "gloves": 0.28}


def helmet_parts(p):
    """The helmet, rigid on the head bone: shell, visor and the dark liner seen inside the eye port."""
    C = HELMET_C
    fw, side, up = np.array([1.0, 0, 0]), np.array([0, 1.0, 0]), np.array([0, 0, 1.0])
    shell = ellipsoid(p, C, (14.6, 12.6, 14.0))
    chin = ellipsoid(p, C + np.array([6.5, 0.0, -9.0]), (8.5, 10.4, 7.5))
    shell = smin(shell, chin, 4.0)
    # A small spoiler at the back and a ridge over the top.
    shell = smin(shell, ellipsoid(p, C + np.array([-13.0, 0, 3.0]), (2.0, 7.0, 3.0)), 2.0)
    # The neck opening, higher at the back.
    shell = smax(shell, -plane(p, C + np.array([0, 0, -13.5]), unit([-0.32, 0, 1.0])), 1.0)
    # The eye port: a deep recess in the front.
    port = rbox(p, C + np.array([12.5, 0.0, -1.0]), (7.0, 8.8, 3.4), None, 2.2)
    shell_cut = smax(shell, -port, 0.5)
    # Vents: a slot on the chin bar and two on the crown.
    shell_cut = smax(shell_cut, -rbox(p, C + np.array([14.0, 0, -9.0]), (2.0, 2.6, 0.45), None, 0.3), 0.2)
    for y in (-3.5, 3.5):
        shell_cut = smax(shell_cut, -rbox(p, C + np.array([4.0, y, 13.6]), (2.4, 0.5, 1.2), None, 0.3), 0.2)
    # The visor: a thin curved shell standing just proud of the helmet, over the port.
    vis_d = ellipsoid(p, C + np.array([0.6, 0, 0.0]), (14.9, 13.2, 14.4))
    visor = np.abs(vis_d) - 0.22
    visor = smax(visor, rbox(p, C + np.array([10.5, 0.0, 0.0]), (6.0, 12.6, 5.4), None, 2.5), 0.3)
    visor = smax(visor, -plane(p, C + np.array([2.5, 0, 0]), fw), 0.2)
    # Visor pivots on each side.
    for y in (-1, 1):
        piv = rcone(p, C + np.array([2.0, 12.0 * y, -1.0]), C + np.array([2.0, 13.4 * y, -1.0]), 1.8, 1.8)
        visor = np.minimum(visor, piv)
    liner = smax(port + 0.0, -(shell + 1.5), 0.2)   # the inside of the port, where the face would be
    liner = smax(port, plane(p, C + np.array([8.0, 0, 0]), fw * -1.0) - 0.0, 0.5)
    liner = smax(liner, shell - 0.05, 0.2)
    trim = np.abs(plane(p, C + np.array([0, 0, -13.2]), unit([-0.32, 0, 1.0]))) - 0.6
    trim = smax(trim, shell - 0.35, 0.2)
    return {"helmet": shell_cut, "visor": visor, "liner": liner, "trim": trim}


# --- Meshing ----------------------------------------------------------------------------------------

def eval_grid(fn, lo, hi, h):
    nx, ny, nz = [int(math.ceil((hi[i] - lo[i]) / h)) + 1 for i in range(3)]
    xs = lo[0] + np.arange(nx) * h; ys = lo[1] + np.arange(ny) * h; zs = lo[2] + np.arange(nz) * h
    vol = np.empty((nx, ny, nz), np.float32)
    Y, Z = np.meshgrid(ys, zs, indexing="ij")
    yz = np.stack([Y.ravel(), Z.ravel()], 1)
    for i, x in enumerate(xs):
        p = np.column_stack([np.full(len(yz), x), yz])
        vol[i] = fn(p).reshape(ny, nz)
    return vol


def mesh_field(fn, lo, hi, h):
    vol = eval_grid(fn, lo, hi, h)
    if vol.min() > 0 or vol.max() < 0:
        return np.zeros((0, 3)), np.zeros((0, 3), np.int64)
    v, f, _, _ = marching_cubes(vol, 0.0, spacing=(h, h, h))
    return v + np.asarray(lo), f.astype(np.int64)


def grad(fn, p, e=0.03):
    g = np.zeros_like(p)
    for k in range(3):
        d = np.zeros(3); d[k] = e
        g[:, k] = fn(p + d) - fn(p - d)
    return unit(g + 1e-12)


def clip_tris(v, f, keep):
    """Keep the parts of triangles where keep > 0 (keep given per vertex), cutting straddling
    triangles at the zero crossing. Returns new vertices and faces."""
    inside = keep > 0
    n_in = inside[f].sum(1)
    full = f[n_in == 3]
    part = f[(n_in == 1) | (n_in == 2)]
    newv = [v]; nv = len(v)
    cache = {}

    def cut(i, j):
        nonlocal nv
        key = (min(i, j), max(i, j))
        if key in cache:
            return cache[key]
        t = keep[i] / (keep[i] - keep[j])
        newv.append((v[i] + (v[j] - v[i]) * t)[None])
        cache[key] = nv; nv += 1
        return cache[key]

    out = [full]
    for tri in part:
        ins = [inside[k] for k in tri]
        # Rotate so the odd one out is first, keeping the winding.
        for r in range(3):
            if ins[r] != ins[(r + 1) % 3] and ins[r] != ins[(r + 2) % 3]:
                break
        a, b, c = tri[r], tri[(r + 1) % 3], tri[(r + 2) % 3]
        ab, ac = cut(a, b), cut(a, c)
        if inside[a]:
            out.append(np.array([[a, ab, ac]]))
        else:
            out.append(np.array([[ab, b, c], [ab, c, ac]]))
    v2 = np.concatenate(newv)
    f2 = np.concatenate(out) if out else np.zeros((0, 3), np.int64)
    used = np.unique(f2)
    remap = -np.ones(len(v2), np.int64); remap[used] = np.arange(len(used))
    return v2[used], remap[f2]


def drop_specks(v, f, min_tris=60):
    """Remove tiny disconnected bits that clipping can leave."""
    if len(f) == 0:
        return v, f
    parent = np.arange(len(v))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]; i = parent[i]
        return i
    for a, b, c in f:
        ra, rb, rc = find(a), find(b), find(c)
        parent[rb] = ra; parent[find(rc)] = ra
    roots = np.array([find(i) for i in range(len(v))])
    comp = roots[f[:, 0]]
    ids, counts = np.unique(comp, return_counts=True)
    keep = np.isin(comp, ids[counts >= min_tris])
    f = f[keep]
    used = np.unique(f)
    remap = -np.ones(len(v), np.int64); remap[used] = np.arange(len(used))
    return v[used], remap[f]


def weld(v, f, tol=1e-4):
    q = np.round(v / tol).astype(np.int64)
    _, first, inv = np.unique(q, axis=0, return_index=True, return_inverse=True)
    f = inv.ravel()[f]
    f = f[(f[:, 0] != f[:, 1]) & (f[:, 1] != f[:, 2]) & (f[:, 0] != f[:, 2])]
    return v[first], f


def layer_meshes(fields, voxels, bounds):
    """Mesh each field, then clip away what lies inside any of the others."""
    meshes = {}
    for name, fn in fields.items():
        lo, hi = bounds[name]
        h = voxels[name] * COARSE
        v, f = mesh_field(fn, lo, hi, h)
        others = [o for o in fields if o != name]
        if len(v) and others:
            keep = np.min([fields[o](v) for o in others], axis=0)
            v, f = clip_tris(v, f, keep)
            v, f = weld(v, f, h * 1e-3)
            v, f = drop_specks(v, f)
        n = grad(fn, v)
        # Faces wound to match the outward normals.
        fn_ = np.cross(v[f[:, 1]] - v[f[:, 0]], v[f[:, 2]] - v[f[:, 0]])
        flip = (fn_ * n[f].sum(1)).sum(1) < 0
        f[flip] = f[flip][:, [0, 2, 1]]
        meshes[name] = (v, f, n)
        print("  %-7s %7d triangles" % (name, len(f)))
    return meshes


def bounds_of(points, pad):
    P = np.asarray(points)
    return P.min(0) - pad, P.max(0) + pad


# --- Skinning -----------------------------------------------------------------------------------------

def chain_weights(p, joints, bones, end_bone=None, blend=4.0):
    """Weights of the bones along a chain of joints: each vertex goes to the segment it's nearest,
    blended with the neighbour near a joint. Returns {bone: weight array}."""
    P = [J[j] if isinstance(j, str) else np.asarray(j) for j in joints]
    dists, ts = [], []
    for a, b in zip(P[:-1], P[1:]):
        d, t = seg_dist(p, a, b)
        dists.append(d); ts.append(t)
    seg = np.argmin(np.stack(dists), axis=0)
    w = {b: np.zeros(len(p)) for b in bones + ([end_bone] if end_bone else [])}
    for i, b in enumerate(bones):
        m = seg == i
        L = np.linalg.norm(P[i + 1] - P[i])
        a_from = ts[i][m] * L                 # distance from the segment's start joint
        a_to = (1 - ts[i][m]) * L
        own = np.ones(m.sum())
        if i > 0:
            k = np.clip(0.5 * (1 - a_from / blend), 0, 0.5)
            w[bones[i - 1]][m] += k; own -= k
        nxt = bones[i + 1] if i + 1 < len(bones) else end_bone
        if nxt:
            k = np.clip(0.5 * (1 - a_to / blend), 0, 0.5)
            w[nxt][m] += k; own -= k
        w[b][m] += own
    return w


def chain_weights_spine(p):
    return chain_weights(p, ["pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "head"],
                         ["pelvis", "spine_01", "spine_02", "spine_03", "neck_01"], blend=7.0)


def skin(layer, v, groups_fn):
    """Bone weights for a layer's vertices: which part (group) each vertex belongs to, softly,
    then the bones along that part."""
    _, groups = groups_fn(v)
    names = list(groups)
    G = np.stack([groups[n] for n in names])
    soft = np.exp(-(G - G.min(0)) / 1.2)
    soft /= soft.sum(0)
    W = np.zeros((len(v), len(BONES)))
    for gi, g in enumerate(names):
        x = g[-1]
        if g in ("torso", "hips"):
            cw = chain_weights_spine(v)
        elif g.startswith("arm_"):
            cw = chain_weights(v, ["upperarm_" + x, "lowerarm_" + x, "hand_" + x],
                               ["upperarm_" + x, "lowerarm_" + x], "hand_" + x, blend=4.5)
        elif g.startswith("leg_"):
            cw = chain_weights(v, ["thigh_" + x, "calf_" + x, "foot_" + x], ["thigh_" + x, "calf_" + x], "foot_" + x, blend=5.0)
        elif g.startswith("boot_") or g.startswith("sole_"):
            toe = J["ball_" + x] + foot_frame(x)[2] * 9.0
            cw = chain_weights(v, ["calf_" + x, "foot_" + x, "ball_" + x, toe], ["calf_" + x, "foot_" + x, "ball_" + x], blend=3.0)
        elif g.startswith("glove_"):
            cw = {"hand_" + x: np.ones(len(v))}
        else:
            raise ValueError(g)
        for b, w in cw.items():
            W[:, BONES.index(b)] += soft[gi] * w
    # Four strongest bones per vertex.
    idx = np.argsort(-W, axis=1)[:, :4]
    w4 = np.take_along_axis(W, idx, 1)
    w4[w4 < 0.02] = 0.0
    w4 /= w4.sum(1, keepdims=True)
    return idx.astype(np.uint16), w4.astype(np.float32)


# --- Ambient occlusion baked into the vertex colour -------------------------------------------------

def bake_ao(v, n, field, steps=6, step=0.9):
    occ = np.zeros(len(v))
    for i in range(1, steps + 1):
        h = step * i
        d = field(v + n * h)
        occ += (h - np.minimum(d, h)) / h * (0.5 ** (i - 1) * 0.9)
    ao = np.clip(1.0 - 0.75 * occ, 0.15, 1.0)
    return ao


# --- Writing glTF --------------------------------------------------------------------------------------

def to_model(p):
    p = np.asarray(p, np.float64)
    return np.stack([p[..., 0], p[..., 2], p[..., 1]], -1) / 100.0


def write_glb(path, prims, skin_info=None, name="mesh"):
    """prims: list of dicts with pos, nrm, idx, col (RGBA float 0..1), material name, and for a
    skinned mesh joints (uint16 x4) and weights (float x4). Positions are pawn-local cm; written in
    the model frame. skin_info: (bone names, parents, rest positions)."""
    blobs, views, accs = [], [], []
    off = 0

    def add(arr, target, comp, typ, count, norm=False, minmax=False):
        nonlocal off
        b = np.ascontiguousarray(arr).tobytes()
        pad = (-len(b)) % 4
        v = {"buffer": 0, "byteOffset": off, "byteLength": len(b)}
        if target:
            v["target"] = target
        views.append(v)
        a = {"bufferView": len(views) - 1, "componentType": comp, "count": count, "type": typ}
        if norm:
            a["normalized"] = True
        if minmax:
            a["min"] = arr.min(0).tolist(); a["max"] = arr.max(0).tolist()
        accs.append(a)
        blobs.append(b + b"\0" * pad); off += len(b) + pad
        return len(accs) - 1

    materials, gl_prims = [], []
    for pr in prims:
        pos = to_model(pr["pos"]).astype(np.float32)
        nrm = unit(to_model(pr["nrm"])).astype(np.float32)
        idx = np.asarray(pr["idx"])[:, [0, 2, 1]]       # the frame swap mirrors, so the winding flips
        attrs = {"POSITION": add(pos, 34962, 5126, "VEC3", len(pos), minmax=True),
                 "NORMAL": add(nrm, 34962, 5126, "VEC3", len(pos))}
        if pr.get("col") is not None:
            col = (np.clip(pr["col"], 0, 1) * 65535).astype(np.uint16)
            attrs["COLOR_0"] = add(col, 34962, 5123, "VEC4", len(pos), norm=True)
        if pr.get("uv") is not None:
            attrs["TEXCOORD_0"] = add(np.asarray(pr["uv"], np.float32), 34962, 5126, "VEC2", len(pos))
        if pr.get("joints") is not None:
            attrs["JOINTS_0"] = add(np.asarray(pr["joints"], np.uint16), 34962, 5123, "VEC4", len(pos))
            attrs["WEIGHTS_0"] = add(np.asarray(pr["weights"], np.float32), 34962, 5126, "VEC4", len(pos))
        small = len(pos) < 65536
        ia = add(idx.astype(np.uint16 if small else np.uint32).ravel(), 34963, 5123 if small else 5125, "SCALAR", idx.size)
        materials.append({"name": pr["material"], "pbrMetallicRoughness": {
            "baseColorFactor": list(pr.get("color", (0.5, 0.5, 0.5))) + [1.0], "metallicFactor": 0.0,
            "roughnessFactor": float(pr.get("roughness", 0.6))}})
        gl_prims.append({"attributes": attrs, "indices": ia, "mode": 4, "material": len(materials) - 1})

    nodes = []
    gl = {"asset": {"version": "2.0", "generator": "torque-shed-ue make_rider.py"}, "scene": 0,
          "meshes": [{"name": name, "primitives": gl_prims}], "materials": materials}
    if skin_info:
        names, parents, rest = skin_info
        for b in names:
            par = parents[b]
            t = to_model(rest[b] - (rest[par] if par else 0.0))
            nodes.append({"name": b, "translation": [float(c) for c in t]})
        for i, b in enumerate(names):
            kids = [names.index(c) for c in names if parents[c] == b]
            if kids:
                nodes[i]["children"] = kids
        ibm = []
        for b in names:
            m = np.eye(4); m[:3, 3] = -to_model(rest[b])
            ibm.append(m.T.ravel())                    # column major
        ibm_acc = add(np.asarray(ibm, np.float32), None, 5126, "MAT4", len(names))
        nodes.append({"name": name, "mesh": 0, "skin": 0})
        gl["skins"] = [{"name": name + "_skeleton", "joints": list(range(len(names))), "skeleton": 0,
                        "inverseBindMatrices": ibm_acc}]
        gl["scenes"] = [{"nodes": [0, len(nodes) - 1]}]
    else:
        nodes.append({"name": name, "mesh": 0})
        gl["scenes"] = [{"nodes": [0]}]
    gl["nodes"] = nodes
    gl["buffers"] = [{"byteLength": off}]
    gl["bufferViews"] = views
    gl["accessors"] = accs
    js = json.dumps(gl, separators=(",", ":")).encode()
    js += b" " * ((-len(js)) % 4)
    bin_ = b"".join(blobs)
    with open(path, "wb") as fh:
        fh.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_)))
        fh.write(struct.pack("<II", len(js), 0x4E4F534A)); fh.write(js)
        fh.write(struct.pack("<II", len(bin_), 0x004E4942)); fh.write(bin_)


# Kit finishes: base colour (linear), roughness. The level script makes the materials; these are
# the glb's own fallbacks.
FINISHES = {
    "jacket": ((0.022, 0.021, 0.020), 0.55),    # black waxed cotton / leather
    "jeans": ((0.030, 0.045, 0.085), 0.85),     # riding denim
    "boots": ((0.016, 0.014, 0.013), 0.42),     # black leather
    "sole": ((0.012, 0.012, 0.012), 0.9),
    "gloves": ((0.26, 0.12, 0.05), 0.5),        # tan-brown leather
    "helmet": ((0.012, 0.012, 0.013), 0.18),    # gloss black
    "visor": ((0.02, 0.02, 0.022), 0.03),       # dark smoked
    "liner": ((0.008, 0.008, 0.008), 0.95),
    "trim": ((0.01, 0.01, 0.01), 0.7),
}


def build(out):
    os.makedirs(out, exist_ok=True)
    pad = 9.0
    body_pts = list(J.values())
    b_all = bounds_of(body_pts, pad)
    feet = [J[k] for k in J if k.startswith(("foot_", "ball_", "calf_"))]
    hands = [on_grip(s, t, r, a) for s in (-1, 1) for t in (0, 90, 180, 270) for r in (4.0, 7.5) for a in (-6, 6)]
    hands += [J["hand_l"], J["hand_r"], J["hand_l"] + unit(J["lowerarm_l"] - J["hand_l"]) * 9, J["hand_r"] + unit(J["lowerarm_r"] - J["hand_r"]) * 9]
    upper = [J[k] for k in J if not k.startswith(("thigh_", "calf_", "foot_", "ball_"))]
    lower = [J[k] for k in J if k.startswith(("pelvis", "spine_01", "thigh_", "calf_", "foot_"))]
    bounds = {
        "jacket": bounds_of(upper, 11.0),
        "jeans": bounds_of(lower, 12.0),
        "boots": bounds_of(feet[:2] + [J["ball_l"] + [12, -8, 0], J["ball_r"] + [12, 8, 0], J["foot_l"] + [-12, -8, -10], J["foot_r"] + [-12, 8, -10]] + [J["foot_l"] + unit(J["calf_l"] - J["foot_l"]) * 17, J["foot_r"] + unit(J["calf_r"] - J["foot_r"]) * 17], 3.0),
        "sole": bounds_of([J["ball_l"] + [13, -7, -5], J["ball_r"] + [13, 7, -5], J["foot_l"] + [-12, -7, -9], J["foot_r"] + [-12, 7, 0]], 1.0),
        "gloves": bounds_of(hands, 1.5),
    }
    fields = {k: (lambda f: (lambda p: f(p)[0]))(fn) for k, fn in LAYERS.items()}
    print("rider body:")
    meshes = layer_meshes(fields, VOXEL, bounds)
    whole = lambda p: np.min([fields[k](p) for k in fields], axis=0)   # noqa: E731
    prims = []
    tris = 0
    for name, (v, f, n) in meshes.items():
        joints, weights = skin(name, v, LAYERS[name])
        ao = bake_ao(v, n, whole)
        col = np.stack([ao, ao, ao, np.ones(len(v))], 1)
        prims.append({"pos": v, "nrm": n, "idx": f, "col": col, "joints": joints, "weights": weights,
                      "material": name, "color": FINISHES[name][0], "roughness": FINISHES[name][1]})
        tris += len(f)
    write_glb(os.path.join(out, "rider.glb"), prims, (BONES, PARENTS, J), "rider")

    print("helmet:")
    hfields = {k: (lambda k: (lambda p: helmet_parts(p)[k]))(k) for k in ("helmet", "visor", "liner", "trim")}
    lo, hi = HELMET_C - 17.5, HELMET_C + 17.5
    hmeshes = layer_meshes(hfields, {"helmet": 0.42, "visor": 0.3, "liner": 0.45, "trim": 0.4},
                           {k: (lo, hi) for k in hfields})
    hwhole = lambda p: np.min([hfields[k](p) for k in hfields], axis=0)   # noqa: E731
    hprims = []
    for name, (v, f, n) in hmeshes.items():
        if not len(f):
            continue
        ao = bake_ao(v, n, hwhole, 5, 0.8)
        hprims.append({"pos": v, "nrm": n, "idx": f, "col": np.stack([ao, ao, ao, np.ones(len(v))], 1),
                       "material": name, "color": FINISHES[name][0], "roughness": FINISHES[name][1]})
        tris += len(f)
    write_glb(os.path.join(out, "helmet.glb"), hprims, None, "helmet")

    info = {
        "about": "Made by Tools/make_rider.py. Bones' rest positions in the pawn's local frame (cm, x forward, y right, z up); the glbs are in the model frame (metres, x forward, y up, z right) like the bike's parts.",
        "bones": [{"name": b, "parent": PARENTS[b], "rest": [round(float(c), 3) for c in J[b]]} for b in BONES],
        "helmet_centre": [round(float(c), 3) for c in HELMET_C],
        "helmet_bone": "head",
        "lengths": {"upper_arm": UPPER_ARM, "forearm": FOREARM, "thigh": THIGH, "shin": SHIN},
        "materials": {"rider": [p["material"] for p in prims], "helmet": [p["material"] for p in hprims]},
        "finishes": {k: {"color": list(v[0]), "roughness": v[1]} for k, v in FINISHES.items()},
    }
    json.dump(info, open(os.path.join(out, "rider.json"), "w"), indent=1)
    print("%d triangles in all" % tris)
    return meshes, hmeshes


def main():
    global COARSE
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--coarse", action="store_true")
    args = ap.parse_args()
    if args.coarse:
        COARSE = 2.0
    build(args.out)


if __name__ == "__main__":
    main()
