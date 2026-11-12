"""The livestock Ride UK puts in the fields (Ride UK stage 6): Data/RideUK/Animals/*.glb, made once
here and kept in the repo, which Tools/ue_build_rideuk.py imports to /Game/RideUK/Animals and lists in
the ride's kit (a TorqueFoliage actor tagged RideAnimals) for World/TorqueRideWorld to place.

Each is named SM_Animal_<kind>_<n>, kind as World/WorldRide's RidePlan::AnimalName gives it: the
Swaledale ewes and lambs of the Buttertubs ride (buttertubs_stone.sheep), and cattle made here: dairy
Friesians, brown and black beef cattle, and shaggy, long-horned Highland cattle. All face +x, their
feet at y = 0, vertex-coloured (RGB the coat, A dirt) as the props are.

    python Tools/make_ride_animals.py
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import buttertubs_stone as ST            # noqa: E402
from export_buttertubs import write_glb  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "RideUK", "Animals")

COATS = {
    "friesian": ((0.018, 0.018, 0.018), (0.62, 0.6, 0.56)),   # black, white patches
    "brown": ((0.17, 0.075, 0.03), (0.3, 0.2, 0.12)),
    "black": ((0.022, 0.02, 0.02), (0.05, 0.045, 0.04)),
    "highland": ((0.3, 0.12, 0.04), (0.42, 0.22, 0.09)),       # ginger, lighter tips
}


def cow(rng, pose, coat):
    """A beef or dairy cow (or a Highland), about 2.3 m nose to tail and 1.4 m at the shoulder, facing
    +x. pose: 'graze' (head down), 'stand' or 'lie'."""
    M = ST.Mesh()
    base, other = (np.array(c) for c in COATS[coat])
    lie = pose == "lie"
    shaggy = coat == "highland"
    body_y = 0.55 if lie else 1.02
    # The body: a long, deep barrel, a little higher at the shoulders and the hips.
    w = ST.Waves(rng, 14, (2.0, 10.0), 1.0)
    patch = ST.Waves(rng, 10, (1.5, 4.0), 1.0)
    P = ST._unit(20, 30, 0.8, 0.9) * [0.95, 0.4 if lie else 0.38, 0.33]
    P[:, 1] += 0.04 * np.cos(P[:, 0] / 0.95 * math.pi * 1.5)
    nrm = P / np.maximum(np.linalg.norm(P, axis=1, keepdims=True), 1e-9)
    P += nrm * (np.abs(w(P)) * (0.06 if shaggy else 0.015))[:, None]
    P[:, 1] += body_y
    if lie:
        P[:, 1] = np.maximum(P[:, 1], 0.04)

    def colour(Q, dark=1.0):
        c = np.zeros((len(Q), 4))
        if coat == "friesian":
            white = (patch(Q) > 0.08) | (Q[:, 1] < body_y - 0.25)
            c[:, :3] = np.where(white[:, None], other, base)
        else:
            t = np.clip(0.5 + w(Q * 0.7), 0, 1)[:, None]
            c[:, :3] = base + (other - base) * t * (0.8 if shaggy else 0.35)
        c[:, :3] *= dark * rng.uniform(0.92, 1.06)
        c[:, 3] = np.clip((0.35 - Q[:, 1]) / 0.35, 0, 1) * 0.7   # muddy legs
        return c

    M.add(P, ST._topology(20, 30), colour(P), P[:, [0, 1]])
    # The neck and head.
    if pose == "graze":
        neck = np.array([0.75, body_y + 0.12, 0.0]); head = np.array([1.2, 0.3, 0.0]); tilt = -1.2
    elif lie:
        neck = np.array([0.7, body_y + 0.1, 0.0]); head = np.array([1.05, 0.72, rng.uniform(-0.15, 0.15)]); tilt = -0.35
    else:
        neck = np.array([0.75, body_y + 0.15, 0.0]); head = np.array([1.2, 1.18, rng.uniform(-0.15, 0.15)]); tilt = -0.55
    path = np.linspace(neck, head + (neck - head) * 0.25, 6)
    Pn, Tn = ST.tube(path, np.linspace(0.3, 0.17, 6), 12)
    M.add(Pn, Tn, colour(Pn), Pn[:, [0, 1]])
    R = ST.rot_xyz(0, 0, tilt)
    Ph = ST._unit(10, 14, 0.7, 0.8) * [0.27, 0.14, 0.12]
    Ph[:, 1] *= 1 - 0.25 * np.clip(Ph[:, 0] / 0.27, 0, 1)       # narrowing to the muzzle
    Ph[:, 2] *= 1 - 0.15 * np.clip(Ph[:, 0] / 0.27, 0, 1)
    loc = Ph @ R.T + head
    ch = colour(loc)
    if coat == "friesian":
        ch[np.abs(Ph[:, 2]) < 0.04, :3] = other                    # the white blaze
    muzzle = Ph[:, 0] > 0.2
    ch[muzzle, :3] = (0.2, 0.13, 0.12) if coat == "friesian" else (0.05, 0.04, 0.035)
    M.add(loc, ST._topology(10, 14), ch, Ph[:, [0, 1]])
    for side in (-1, 1):
        # Ears out to the side, and on a Highland its long horns, up and outwards.
        ear = np.array([-0.12, 0.06, side * 0.11])
        Pe, Te = ST.tube(np.array([ear, ear + [-0.03, 0.0, side * 0.14]]) @ R.T + head, [0.045, 0.02], 6)
        M.add(Pe, Te, colour(Pe, 0.9), Pe[:, [0, 1]])
        if shaggy:
            a = np.linspace(0.0, 1.0, 10)
            hp = np.stack([-0.14 + 0.08 * a, 0.1 + 0.25 * a ** 1.6, side * (0.1 + 0.42 * a)], 1)
            Ph_, Th_ = ST.tube(hp @ R.T + head, np.linspace(0.045, 0.012, 10), 7)
            chn = np.zeros((len(Ph_), 4)); chn[:, :3] = (0.5, 0.45, 0.36)
            chn[np.linalg.norm(Ph_ - (hp[0] @ R.T + head), axis=1) > 0.38, :3] = 0.04   # (dark tips)
            M.add(Ph_, Th_, chn, Ph_[:, [0, 1]])
    # Legs, with dark hooves.
    if not lie:
        for lx in (-0.66, 0.62):
            for lz in (-0.17, 0.17):
                knee = rng.normal(0, 0.02)
                path = np.array([[lx, body_y - 0.2, lz], [lx + knee, 0.45, lz], [lx, 0.1, lz], [lx, 0.0, lz]])
                Pl, Tl = ST.tube(path, [0.1, 0.065, 0.055, 0.06], 8)
                cl = colour(Pl)
                cl[Pl[:, 1] < 0.09, :3] = 0.03
                M.add(Pl, Tl, cl, Pl[:, [0, 1]])
    # The tail and its tuft.
    tail = np.array([[-0.93, body_y + 0.28, 0.0], [-0.98, body_y, 0.0], [-0.98, body_y - 0.45, 0.02], [-0.97, max(0.12, body_y - 0.75), 0.03]])
    Pt, Tt = ST.tube(tail, [0.04, 0.03, 0.03, 0.07], 6)
    M.add(Pt, Tt, colour(Pt, 0.8), Pt[:, [0, 1]])
    return M


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".glb"):
            os.remove(os.path.join(OUT, f))
    made = {}
    # The ewes and lambs, as Buttertubs has them.
    for k, pose in enumerate(("graze", "graze", "look")):
        kind = "ewe_look" if pose == "look" else "ewe_graze"
        made.setdefault(kind, []).append(ST.sheep(np.random.default_rng(700 + k), pose))
    made["ewe_lie"] = [ST.sheep(np.random.default_rng(703), "lie")]
    made["lamb"] = [ST.sheep(np.random.default_rng(720 + k), p, fleece=(0.6, 0.58, 0.52)) for k, p in enumerate(("graze", "look"))]
    # Cattle: two coats of each pose (a dairy herd and a beef one), and Highlands standing and grazing.
    for k, coat in enumerate(("friesian", "brown", "black")):
        made.setdefault("cow_graze", []).append(cow(np.random.default_rng(800 + k), "graze", coat))
        made.setdefault("cow_stand", []).append(cow(np.random.default_rng(810 + k), "stand", coat))
        made.setdefault("cow_lie", []).append(cow(np.random.default_rng(820 + k), "lie", coat))
    made["highland"] = [cow(np.random.default_rng(830), "graze", "highland"), cow(np.random.default_rng(831), "stand", "highland")]
    total = 0
    for kind, meshes in made.items():
        for n, M in enumerate(meshes):
            name = "SM_Animal_%s_%d" % (kind, n)
            nv, nt = M.write(os.path.join(OUT, name + ".glb"), write_glb, name)
            total += nt
            print("%s: %d triangles" % (name, nt))
    print("%d meshes, %d triangles in %s" % (sum(len(v) for v in made.values()), total, OUT))


if __name__ == "__main__":
    main()
