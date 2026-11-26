"""Ride UK's wild birds (beenster Nov 26: "more birds flying ... flying out of bushes or trees as we
approach ... occasional birds of prey perched on tree tops"): twelve UK species modelled here, each in the
poses World/TorqueRideBirds flips between as it flies, with two hand-made distance versions.

  buzzard, red_kite, kestrel        birds of prey: soaring, hovering (the kestrel) and perched on tree tops
  gull                              herring gulls by the sea
  wood_pigeon, rook, starling       flocks over the fields; pigeons clatter out of trees
  goose                             greylag skeins in the winter half of the year
  blackbird, chaffinch, sparrow     out of the hedges as a rider comes by (sparrows by houses)
  pheasant                          up out of the verge in farmland, mostly autumn and winter

Poses (SM_Bird_<species>_<pose>): flap0..flap3 a wingbeat (top, down, bottom, up with the hand
flexed), glide (wings spread), closed (wings shut, as small birds bound and pigeons dive), perch (standing,
legs and toes; its origin at the feet; the others' at the body's middle). Front +x, up +y, wings along z,
as the props are (Tools/blender_street_kit.py's frame). Real sizes in metres.

Every model is drawn with M_RideBird (Tools/ue_build_rideuk.py bird_kit: two-sided, the vertex colour the
plumage; its alpha the surface: 1 feathers, 0.55 the bare parts (beak, legs, cere), 0.15 the wet eye).
Each with _LOD1 (about a quarter of the triangles) and _LOD2 (a silhouette of a couple of hundred), never
reduced by the engine.

    python Tools/make_ride_birds.py                 # writes Data/RideUK/Birds/*.glb and birds.json
    python Tools/make_ride_birds.py --render DIR    # and review pictures in DIR (needs pip install bpy)
"""
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from export_buttertubs import write_glb   # noqa: E402

ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "Data", "RideUK", "Birds")

FEATHER, BARE, EYE = 1.0, 0.55, 0.15
POSES_ALL = ["flap0", "flap1", "flap2", "flap3", "glide", "closed", "perch"]
# (the order World/WorldRideBirds.h's EPose has them in)


def srgb(h):
    """A colour written as sRGB hex, as linear."""
    c = np.array([int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5)])
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def mix(a, b, t):
    t = np.asarray(t, float)[..., None] if np.ndim(t) else t
    return a * (1 - t) + b * t


def noise1(x, seed=0.0):
    """Cheap smooth-ish 1D noise for feather mottling."""
    return 0.5 + 0.25 * np.sin(x * 12.9898 + seed) * np.sin(x * 4.1414 + 2.0 * seed) + 0.25 * np.sin(x * 31.7 + seed * 3.1)


def hash3(p, seed=0.0):
    v = np.sin(p[..., 0] * 127.1 + p[..., 1] * 311.7 + p[..., 2] * 74.7 + seed) * 43758.5453
    return v - np.floor(v)


# --- species -----------------------------------------------------------------------------------------------

class Sp:
    def __init__(self, name, **k):
        self.name = name
        self.length = 0.5        # body+tail, bill tip to tail tip
        self.span = 1.2          # wingtip to wingtip
        self.girth = 0.12        # body width
        self.depth = 0.13        # body height
        self.tail = 0.35         # tail share of the length
        self.tail_w = 0.6        # tail width at its end, of the body width x 2
        self.fork = 0.0          # forked tail (kite)
        self.head = 1.0          # head size factor
        self.neck = 1.0          # neck length factor
        self.bill = 0.05         # bill length (m)
        self.bill_d = 0.5        # bill depth factor
        self.hook = 0.0          # hooked bill
        self.prims = 10
        self.fingers = 0         # emarginated outer primaries (slotted wingtip)
        self.point = 0.5         # wingtip pointedness 0 round .. 1 sharp
        self.chord = 0.3         # arm chord of the semi-span
        self.hand = 0.55         # hand share of the semi-span
        self.glide_up = 6.0      # glide dihedral (deg)
        self.glide_hand = 0.0    # hand angle at glide (deg; - a gull's crook)
        self.leg = 0.06          # tarsus length
        self.upright = 55.0      # perched body pitch (deg)
        self.tail_spread = 40.0  # tail fan at glide (deg)
        self.poses = POSES_ALL
        self.__dict__.update(k)


SPECIES = [
    Sp("buzzard", length=0.53, span=1.2, girth=0.15, depth=0.15, tail=0.36, tail_w=0.72, head=1.05, bill=0.035, bill_d=0.7,
       hook=1.0, fingers=5, point=0.15, chord=0.36, hand=0.5, glide_up=11.0, glide_hand=4.0, leg=0.08, upright=62.0,
       tail_spread=55.0, poses=POSES_ALL),
    Sp("red_kite", length=0.64, span=1.62, girth=0.13, depth=0.13, tail=0.46, tail_w=0.55, fork=0.45, head=0.95, bill=0.035,
       bill_d=0.65, hook=1.0, fingers=5, point=0.25, chord=0.28, hand=0.55, glide_up=2.0, glide_hand=-6.0, leg=0.07,
       upright=60.0, tail_spread=35.0, poses=["flap0", "flap1", "flap2", "flap3", "glide"]),
    Sp("kestrel", length=0.34, span=0.75, girth=0.075, depth=0.08, tail=0.45, tail_w=0.55, head=1.1, bill=0.018, bill_d=0.8,
       hook=1.0, fingers=0, point=0.85, chord=0.27, hand=0.58, glide_up=3.0, leg=0.04, upright=65.0, tail_spread=80.0,
       poses=POSES_ALL),
    Sp("gull", length=0.6, span=1.42, girth=0.13, depth=0.13, tail=0.28, tail_w=0.6, head=1.0, bill=0.055, bill_d=0.45,
       hook=0.3, fingers=0, point=0.8, chord=0.24, hand=0.55, glide_up=6.0, glide_hand=-16.0, leg=0.065, upright=25.0,
       tail_spread=25.0, poses=["flap0", "flap1", "flap2", "flap3", "glide"]),
    Sp("wood_pigeon", length=0.41, span=0.78, girth=0.11, depth=0.11, tail=0.38, tail_w=0.62, head=0.8, bill=0.018, bill_d=0.35,
       fingers=0, point=0.55, chord=0.32, hand=0.55, glide_up=8.0, leg=0.03, upright=25.0, tail_spread=35.0),
    Sp("rook", length=0.46, span=0.9, girth=0.11, depth=0.115, tail=0.36, tail_w=0.62, head=1.0, bill=0.055, bill_d=0.8,
       fingers=5, point=0.2, chord=0.33, hand=0.52, glide_up=5.0, leg=0.055, upright=30.0, tail_spread=35.0),
    Sp("starling", length=0.21, span=0.39, girth=0.06, depth=0.062, tail=0.26, tail_w=0.6, head=1.0, bill=0.026, bill_d=0.35,
       fingers=0, point=0.85, chord=0.3, hand=0.58, glide_up=4.0, leg=0.03, upright=20.0, tail_spread=25.0,
       poses=["flap0", "flap1", "flap2", "flap3", "glide", "closed"]),
    Sp("blackbird", length=0.25, span=0.37, girth=0.065, depth=0.07, tail=0.36, tail_w=0.6, head=1.0, bill=0.022, bill_d=0.4,
       fingers=0, point=0.35, chord=0.36, hand=0.55, glide_up=4.0, leg=0.035, upright=25.0, tail_spread=35.0,
       poses=["flap0", "flap1", "flap2", "flap3", "glide", "closed"]),
    Sp("chaffinch", length=0.15, span=0.26, girth=0.045, depth=0.048, tail=0.42, tail_w=0.6, head=1.05, bill=0.012, bill_d=0.7,
       fingers=0, point=0.45, chord=0.36, hand=0.55, glide_up=4.0, leg=0.02, upright=25.0, tail_spread=35.0,
       poses=["flap0", "flap1", "flap2", "flap3", "glide", "closed"]),
    Sp("sparrow", length=0.15, span=0.23, girth=0.05, depth=0.05, tail=0.38, tail_w=0.6, head=1.05, bill=0.012, bill_d=0.8,
       fingers=0, point=0.35, chord=0.38, hand=0.52, glide_up=4.0, leg=0.02, upright=25.0, tail_spread=30.0,
       poses=["flap0", "flap1", "flap2", "flap3", "glide", "closed"]),
    Sp("pheasant", length=0.82, span=0.8, girth=0.15, depth=0.15, tail=0.55, tail_w=0.22, head=0.75, neck=1.3, bill=0.03, bill_d=0.6,
       hook=0.3, fingers=0, point=0.25, chord=0.42, hand=0.5, glide_up=-4.0, leg=0.08, upright=10.0, tail_spread=18.0,
       poses=["flap0", "flap1", "flap2", "flap3", "glide"]),
    Sp("goose", length=0.8, span=1.6, girth=0.2, depth=0.18, tail=0.17, tail_w=0.55, head=0.85, neck=2.4, bill=0.065, bill_d=0.6,
       fingers=0, point=0.6, chord=0.25, hand=0.55, glide_up=3.0, leg=0.09, upright=10.0, tail_spread=25.0,
       poses=["flap0", "flap1", "flap2", "flap3", "glide"]),
]
BY_NAME = {s.name: s for s in SPECIES}


# --- plumage -----------------------------------------------------------------------------------------------
# Each species' colour as a function of where on the bird: part ("body", "wing", "tail", "bill", "leg",
# "eye", "cere"), u (0 tail..1 bill along the body; 0 root..1 tip on a wing or tail feather), v (around the
# body: 0 the back .. 1 the belly; across a wing: 0 leading .. 1 trailing edge), top (True: the upper side),
# k (a feather's index from 0 inner, n its count) and p (the point, for speckle).

def plumage(sp, part, u, v, top, k, n, p):
    N = len(u)
    out = np.zeros((N, 3))
    nm = sp.name
    sp_ = hash3(p * 140.0)           # fine speckle 0..1
    sp2 = hash3(p * 37.0, 3.0)       # coarser
    if part == "eye":
        return np.tile(srgb("#0d0806"), (N, 1))
    if part == "eyering":
        c = {"kestrel": "#e8c020", "buzzard": "#b88a3a", "red_kite": "#d8c8a0", "gull": "#e0a020", "wood_pigeon": "#e8e0c8",
             "pheasant": "#b81810", "starling": "#101010"}.get(nm, "#1a1410")
        return np.tile(srgb(c), (N, 1))
    if part == "leg":
        c = {"buzzard": "#e0b830", "red_kite": "#e0b830", "kestrel": "#e8c020", "gull": "#e0a8a0", "wood_pigeon": "#c86070",
             "rook": "#151515", "starling": "#c08070", "blackbird": "#4a3a30", "chaffinch": "#906858", "sparrow": "#a08070",
             "pheasant": "#8a7a6a", "goose": "#e8a090"}[nm]
        return np.tile(srgb(c), (N, 1))
    if part == "cere":
        c = {"buzzard": "#e8c030", "red_kite": "#e8c030", "kestrel": "#f0c828", "wood_pigeon": "#e8e8e0"}.get(nm, "#303030")
        return np.tile(srgb(c), (N, 1))
    if part == "bill":
        if nm == "gull":
            c = mix(srgb("#e8c030"), srgb("#d02010"), smooth(0.7, 0.75, u) * smooth(0.9, 0.85, u) * (v > 0.5))
            return np.broadcast_to(c, (N, 3)).copy() if c.ndim == 1 else c
        if nm == "goose":
            return mix(np.tile(srgb("#e89040"), (N, 1)), np.tile(srgb("#f0e0c8"), (N, 1)), smooth(0.9, 0.97, u))
        if nm == "blackbird":
            return np.tile(srgb("#f0a010"), (N, 1))
        if nm == "pheasant":
            return np.tile(srgb("#d8c8a0"), (N, 1))
        if nm == "wood_pigeon":
            return mix(np.tile(srgb("#d89090"), (N, 1)), np.tile(srgb("#f0e060"), (N, 1)), smooth(0.5, 0.8, u))
        if nm == "rook":
            return mix(np.tile(srgb("#c8c0b8"), (N, 1)), np.tile(srgb("#202020"), (N, 1)), smooth(0.45, 0.75, u))
        if nm in ("buzzard", "red_kite", "kestrel"):
            return mix(np.tile(srgb("#d8b030"), (N, 1)), np.tile(srgb("#181818"), (N, 1)), smooth(0.25, 0.5, u))
        if nm == "starling":
            return np.tile(srgb("#2a2420"), (N, 1))   # (winter: dark)
        if nm == "chaffinch":
            return np.tile(srgb("#8a9aa8"), (N, 1))
        return np.tile(srgb("#3a3028"), (N, 1))

    def col(h):
        return np.tile(srgb(h), (N, 1))

    ones = np.ones(N)
    if nm == "buzzard":
        # dark brown above with paler fringes; below pale with a dark breast and belly band, pale U across the breast;
        # underwing pale with dark carpal patches and a dark trailing edge, dark primary tips; barred tail.
        brown, dark, pale = col("#4a3322"), col("#2c1e14"), col("#d8c8b0")
        if part == "body":
            belly = smooth(0.45, 0.75, v)
            under = mix(dark, pale, smooth(0.52, 0.6, u) * smooth(0.75, 0.65, u))   # pale breast U
            under = mix(under, mix(pale, brown, 0.6), smooth(0.35, 0.3, u) * smooth(0.12, 0.2, u))
            c = mix(mix(brown, dark, sp2 * 0.5), under, belly)
            head = smooth(0.82, 0.88, u)
            c = mix(c, mix(col("#5a4030"), col("#b8a080"), belly * 0.6), head)
        elif part == "wing":
            if top:
                c = mix(brown, dark, smooth(0.5, 0.9, v) * 0.6 + sp2 * 0.3)
                c = mix(c, col("#1a120c"), smooth(0.65, 0.85, u) * (k >= 0))
            else:
                c = mix(pale, col("#e8e0d0"), sp_ * 0.3)
                carpal = np.exp(-((u - 0.5) ** 2) / 0.004 - ((v - 0.15) ** 2) / 0.02)
                c = mix(c, dark, np.clip(carpal * 1.4, 0, 1))
                c = mix(c, dark, smooth(0.8, 0.98, v) * 0.8)
                c = mix(c, col("#1a120c"), smooth(0.78, 0.92, u) * (k >= 0))
                c = mix(c, col("#5a4535"), smooth(0.05, 0.0, v) * 0.5)
        else:   # tail
            bars = (np.sin(u * 2 * math.pi * 6.0) > 0.55)
            c = mix(col("#8a6a50") if top else col("#c8b8a0"), col("#3a2a1e"), bars * 0.7)
            c = mix(c, col("#2a1e14"), smooth(0.82, 0.9, u))
        return c
    if nm == "red_kite":
        rufous, dark, grey, white = col("#8a3a18"), col("#2a1810"), col("#b0b0b0"), col("#e8e8e4")
        if part == "body":
            c = mix(rufous, mix(rufous, col("#6a2810"), 0.4), sp2)
            streak = (np.sin(p[:, 2] * 400.0) > 0.6) * smooth(0.4, 0.7, v)
            c = mix(c, dark, streak * 0.5)
            c = mix(c, mix(grey, white, 0.4), smooth(0.8, 0.86, u))
        elif part == "wing":
            if top:
                c = mix(col("#6a3a20"), dark, smooth(0.6, 0.8, u))
                c = mix(c, col("#b89070"), np.exp(-((v - 0.25) ** 2) / 0.01) * smooth(0.1, 0.3, u) * smooth(0.6, 0.45, u))  # pale bar
            else:
                c = mix(rufous, dark, smooth(0.45, 0.55, u))
                c = mix(c, white, smooth(0.5, 0.58, u) * smooth(0.82, 0.74, u) * (v > 0.25))   # the white windows
                c = mix(c, dark, smooth(0.8, 0.9, u))
        else:
            c = mix(col("#b05020"), col("#d87840"), (not top) * 0.5 + 0 * ones)
            c = mix(c, col("#402010"), smooth(0.9, 1.0, u) * 0.6)
        return c
    if nm == "kestrel":
        # male: blue-grey head and tail, chestnut back spotted black, buff spotted breast, black tail band, dark primaries
        chest, buff, grey, black = col("#a0501e"), col("#e0c090"), col("#7a8890"), col("#151210")
        if part == "body":
            spots = (sp_ > 0.82)
            c = mix(mix(chest, black, spots * 0.8), mix(buff, col("#5a3018"), (sp_ > 0.85) * 0.7), smooth(0.45, 0.7, v))
            c = mix(c, grey, smooth(0.8, 0.86, u) * smooth(0.7, 0.5, v))
            c = mix(c, black, np.exp(-((u - 0.9) ** 2) / 0.0006 - ((v - 0.55) ** 2) / 0.01) * 0.8)  # moustache
        elif part == "wing":
            if top:
                c = mix(mix(chest, black, (sp_ > 0.8) * 0.8), black, smooth(0.45, 0.6, u))
            else:
                c = mix(col("#e8dcc8"), col("#7a6050"), (np.sin(v * 50 + u * 30) > 0.3) * 0.35)
                c = mix(c, col("#504038"), smooth(0.85, 0.98, u) * 0.6)
        else:
            c = mix(grey if top else col("#c8c8c0"), black, smooth(0.78, 0.84, u) * smooth(0.95, 0.9, u))
        return c
    if nm == "gull":
        white, grey, black = col("#f0f0ec"), col("#a8b4bc"), col("#121212")
        if part == "body":
            c = mix(white, col("#d8d8d4"), smooth(0.5, 0.0, v) * (u > 0.75) * sp2 * 0.5)
            c = mix(grey, c, smooth(0.15, 0.3, v) + smooth(0.75, 0.8, u))
        elif part == "wing":
            c = mix(grey, white, smooth(0.85, 0.98, v)) if top else mix(col("#d8dce0"), white, 0.6)
            tip = smooth(0.78, 0.86, u)
            c = mix(c, black, tip)
            c = mix(c, white, (np.exp(-((u - 0.95) ** 2) / 0.0008)) * (k >= n - 2))   # mirrors
        else:
            c = white
        return c
    if nm == "wood_pigeon":
        blue, pink, slate, white, dark = col("#8a90a0"), col("#b08a90"), col("#6a7080"), col("#f0f0f0"), col("#2a2a30")
        if part == "body":
            c = mix(slate, pink, smooth(0.45, 0.65, v) * smooth(0.45, 0.6, u))
            c = mix(c, blue, smooth(0.8, 0.85, u))
            c = mix(c, white, np.exp(-((u - 0.79) ** 2) / 0.0006 - ((v - 0.35) ** 2) / 0.012))   # neck patch
            c = mix(c, col("#5a8a70"), np.exp(-((u - 0.77) ** 2) / 0.001 - ((v - 0.2) ** 2) / 0.02) * 0.5)  # green sheen
        elif part == "wing":
            c = mix(slate, blue, 0.4) if top else mix(blue, col("#a0a8b4"), 0.5)
            c = mix(c, white, (np.abs(u - 0.42) < 0.06) * (v < 0.4) * top)   # white wing bar across the wrist
            c = mix(c, dark, smooth(0.7, 0.85, u))
        else:
            c = mix(slate, dark, smooth(0.75, 0.85, u))
        return c
    if nm == "rook":
        black = col("#0c0c10")
        c = mix(black, col("#1a1a2c"), sp2 * 0.6)   # purple-blue gloss
        if part == "body":
            c = mix(c, col("#9a9090"), np.exp(-((u - 0.95) ** 2) / 0.0008) * smooth(0.6, 0.9, v))   # bare face
        return c
    if nm == "starling":
        dark = col("#121414")
        c = mix(dark, col("#1a3028"), sp2 * 0.6)
        if part == "body":
            c = mix(c, col("#e8e0d0"), (sp_ > 0.86) * 0.9)   # winter spots
        elif part == "wing":
            c = mix(c, col("#5a4a3a"), (np.abs(v - 0.9) < 0.07) * 0.6 + smooth(0.7, 0.9, u) * 0.2)
        return c
    if nm == "blackbird":
        black = col("#0e0d0c")
        c = mix(black, col("#1a1816"), sp2 * 0.5)
        return c
    if nm == "chaffinch":
        pinkb, greyb, brown, white, dark, green = col("#c07a68"), col("#6a7c94"), col("#7a5a40"), col("#f0ece4"), col("#1a1a1a"), col("#7a9a50")
        if part == "body":
            c = mix(brown, pinkb, smooth(0.35, 0.55, v))
            c = mix(c, greyb, smooth(0.8, 0.84, u) * smooth(0.6, 0.45, v))
            c = mix(c, green, smooth(0.12, 0.05, u) * smooth(0.4, 0.2, v))   # green rump
            c = mix(c, col("#e8d8d0"), smooth(0.25, 0.15, u) * smooth(0.6, 0.8, v))
        elif part == "wing":
            c = mix(dark, brown, 0.2) if top else col("#c0b8b0")
            c = mix(c, white, (np.abs(u - 0.25) < 0.08) * top + (np.abs(v - 0.5) < 0.06) * (u < 0.45) * top)   # double bars
            c = mix(c, col("#e8d060"), smooth(0.5, 0.6, u) * (v > 0.85) * 0.5)
        else:
            c = mix(dark, white, (k >= n - 2) * smooth(0.3, 0.5, u) * 0.8)
        return c
    if nm == "sparrow":
        brown, chest, grey, black, buff = col("#7a5030"), col("#c0a888"), col("#8a8a8a"), col("#151515"), col("#b89070")
        if part == "body":
            streak = (np.sin(p[:, 2] * 500.0 + p[:, 0] * 200) > 0.4) * smooth(0.45, 0.3, v)
            c = mix(mix(brown, black, streak * 0.8), mix(grey, chest, 0.5), smooth(0.4, 0.6, v))
            c = mix(c, grey, smooth(0.82, 0.86, u) * smooth(0.25, 0.1, v))   # grey crown
            c = mix(c, col("#7a4a28"), smooth(0.8, 0.84, u) * smooth(0.15, 0.3, v) * smooth(0.5, 0.35, v))
            c = mix(c, black, np.exp(-((u - 0.82) ** 2) / 0.001 - ((v - 0.85) ** 2) / 0.01))   # bib
        elif part == "wing":
            streak = (np.sin(v * 40.0) > 0.2)
            c = mix(mix(brown, black, streak * 0.6), buff, sp2 * 0.3) if top else col("#c0b0a0")
            c = mix(c, col("#f0e8d8"), (np.abs(u - 0.3) < 0.05) * top)
        else:
            c = mix(brown, col("#3a2818"), 0.4)
        return c
    if nm == "pheasant":
        copper, dark, green, white, red = col("#8a4418"), col("#2a1408"), col("#1a4a30"), col("#f0f0f0"), col("#c01810")
        if part == "body":
            scal = (sp_ > 0.7)
            c = mix(copper, dark, scal * 0.7)
            c = mix(c, col("#c08040"), smooth(0.1, 0.0, v) * 0.3)
            c = mix(c, white, (np.abs(u - 0.74) < 0.012) * 1.0)   # ring neck
            c = mix(c, green, smooth(0.75, 0.77, u))
            c = mix(c, red, np.exp(-((u - 0.9) ** 2) / 0.0012 - ((v - 0.45) ** 2) / 0.02))   # wattle
        elif part == "wing":
            c = mix(col("#8a7050"), dark, (np.sin(v * 30) > 0.5) * 0.5) if top else col("#b8a888")
            c = mix(c, col("#5a4a3a"), smooth(0.4, 0.6, u) * 0.6)
        else:
            bars = (np.sin(u * 2 * math.pi * 14.0) > 0.7)
            c = mix(col("#a88050"), dark, bars * 0.9)
        return c
    if nm == "goose":
        grey, dark, pale, white = col("#6a6254"), col("#3e3a32"), col("#a89e8c"), col("#f0ece4")
        if part == "body":
            c = mix(mix(grey, dark, sp2 * 0.4), pale, smooth(0.5, 0.8, v))
            c = mix(c, white, smooth(0.15, 0.05, u))   # white under the tail
            c = mix(c, mix(grey, dark, 0.3), smooth(0.7, 0.75, u))
            bars = (np.sin(p[:, 0] * 160.0) > 0.6) * smooth(0.6, 0.2, v) * smooth(0.65, 0.55, u)
            c = mix(c, pale, bars * 0.6)
        elif part == "wing":
            c = mix(col("#a8a8a4"), grey, smooth(0.25, 0.4, u)) if top else mix(pale, grey, 0.5)
            c = mix(c, dark, smooth(0.7, 0.85, u))
            c = mix(c, pale, (np.sin(v * 25.0) > 0.7) * top * (u < 0.5) * 0.5)
        else:
            c = mix(dark, white, smooth(0.6, 0.2, u) * (not top))
        return c
    return np.tile(srgb("#806040"), (N, 1))


# --- mesh building -----------------------------------------------------------------------------------------

class Mesh:
    def __init__(self):
        self.P, self.F, self.C, self.A = [], [], [], []
        self.n = 0

    def add(self, P, F, C, A, flip=False):
        P = np.asarray(P, float)
        F = np.asarray(F, np.int64)
        if flip:
            F = F[:, ::-1]
        self.P.append(P)
        self.F.append(F + self.n)
        self.C.append(np.asarray(C, float))
        self.A.append(np.full(len(P), A) if np.isscalar(A) else np.asarray(A, float))
        self.n += len(P)

    def arrays(self):
        P = np.concatenate(self.P); F = np.concatenate(self.F); C = np.concatenate(self.C); A = np.concatenate(self.A)
        # smooth normals per vertex (pieces keep their own vertices, so seams between pieces stay sharp)
        Nn = np.zeros_like(P)
        a, b, c = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]
        fn = np.cross(b - a, c - a)
        for i in range(3):
            np.add.at(Nn, F[:, i], fn)
        Nn /= np.maximum(np.linalg.norm(Nn, axis=1, keepdims=True), 1e-12)
        return P, F, Nn, C, A

    def tris(self):
        return sum(len(f) for f in self.F)


def grid_faces(rows, cols, wrap=False):
    """Faces of a rows x cols grid of vertices (row-major); wrap joins the last column to the first."""
    F = []
    cc = cols if wrap else cols - 1
    for r in range(rows - 1):
        for c in range(cc):
            a = r * cols + c
            b = r * cols + (c + 1) % cols
            F.append((a, b, a + cols))
            F.append((b, b + cols, a + cols))
    return np.array(F, np.int64)


def catmull(points, n):
    """n points along a Catmull-Rom curve through `points` (k x d)."""
    P = np.asarray(points, float)
    P = np.vstack([2 * P[0] - P[1], P, 2 * P[-1] - P[-2]])
    seg = len(P) - 3
    out = []
    for i in range(n):
        t = i / (n - 1) * seg
        j = min(int(t), seg - 1)
        u = t - j
        p0, p1, p2, p3 = P[j], P[j + 1], P[j + 2], P[j + 3]
        out.append(0.5 * ((2 * p1) + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u + (-p0 + 3 * p1 - 3 * p2 + p3) * u ** 3))
    return np.array(out)


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def ellipsoid(centre, radii, nu, nv):
    """A closed ellipsoid: returns points, faces."""
    P = []
    for i in range(nv + 1):
        th = math.pi * i / nv
        for j in range(nu):
            ph = 2 * math.pi * j / nu
            P.append([radii[0] * math.cos(th), radii[1] * math.sin(th) * math.cos(ph), radii[2] * math.sin(th) * math.sin(ph)])
    P = np.array(P) + np.asarray(centre)
    F = grid_faces(nv + 1, nu, wrap=True)
    return P, F


def tube(points, radii, sides):
    """A tube along points (k x 3) with radii (k), capped."""
    P = np.asarray(points, float)
    k = len(P)
    T = np.gradient(P, axis=0)
    T /= np.maximum(np.linalg.norm(T, axis=1, keepdims=True), 1e-12)
    up = np.array([0.0, 1.0, 0.0]) if abs(T[0][1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    V = []
    Nrm = np.cross(T[0], up); Nrm /= np.linalg.norm(Nrm)
    for i in range(k):
        Nrm = Nrm - T[i] * np.dot(Nrm, T[i]); Nrm /= max(np.linalg.norm(Nrm), 1e-12)
        B = np.cross(T[i], Nrm)
        for j in range(sides):
            a = 2 * math.pi * j / sides
            V.append(P[i] + radii[i] * (math.cos(a) * Nrm + math.sin(a) * B))
    V = np.array(V)
    F = list(grid_faces(k, sides, wrap=True))
    c0 = len(V); V = np.vstack([V, P[0], P[-1]])
    for j in range(sides):
        F.append((c0, (j + 1) % sides, j))
        F.append((c0 + 1, (k - 1) * sides + j, (k - 1) * sides + (j + 1) % sides))
    return V, np.array(F)


def blade(root, tip_dir, side_dir, length, width_fn, bend_up, segs, curl=0.0, thick=0.002, asym=0.35, lift_fn=None):
    """A feather: a thin closed blade from root along tip_dir, its vanes along side_dir (the trailing side
    gets 1-asym of the width), cambered, its tip curled up by curl (m). Returns points, faces, (u, v, top)."""
    tip_dir = np.asarray(tip_dir, float); tip_dir /= np.linalg.norm(tip_dir)
    side_dir = np.asarray(side_dir, float); side_dir -= tip_dir * np.dot(side_dir, tip_dir); side_dir /= np.linalg.norm(side_dir)
    upv = np.cross(side_dir, tip_dir)
    if np.dot(upv, bend_up) < 0:
        upv = -upv
    cols = 4
    P, U, Vv, Top = [], [], [], []
    for s in (0, 1):   # top then bottom
        for i in range(segs + 1):
            t = i / segs
            w = width_fn(t)
            centre = np.asarray(root) + tip_dir * length * t + upv * (curl * t * t + (lift_fn(t) if lift_fn else 0.0))
            for j in range(cols):
                q = j / (cols - 1)               # 0 leading vane edge .. 1 trailing
                off = (q - asym) * w
                cam = 0.012 * w * 4 * q * (1 - q) * (1 if s == 0 else 0.6)
                P.append(centre + side_dir * off + upv * (cam + (thick * 0.5 if s == 0 else -thick * 0.5)))
                U.append(t); Vv.append(q); Top.append(s == 0)
    rows = segs + 1
    F = []
    for s in (0, 1):
        g = grid_faces(rows, cols) + s * rows * cols
        F.extend(g[:, ::-1] if s == 0 else g)
    # edges: join the top and bottom along both sides and the tip
    base = rows * cols
    for i in range(rows - 1):
        for j in (0, cols - 1):
            a, b = i * cols + j, (i + 1) * cols + j
            if j == 0:
                F.append((a, b, base + a)); F.append((b, base + b, base + a))
            else:
                F.append((a, base + a, b)); F.append((b, base + a, base + b))
    for j in range(cols - 1):
        a, b = (rows - 1) * cols + j, (rows - 1) * cols + j + 1
        F.append((a, base + a, b)); F.append((b, base + a, base + b))
    return np.array(P), np.array(F), (np.array(U), np.array(Vv), np.array(Top))


# --- the bird ----------------------------------------------------------------------------------------------

POSE_WING = {
    # shoulder elevation, hand angle (deg), extension (0..1: 1 spread), hand sweep back (deg), tail spread factor
    "flap0": (58.0, -14.0, 0.97, 8.0, 0.7),
    "flap1": (8.0, 10.0, 1.0, 0.0, 0.8),
    "flap2": (-42.0, 14.0, 0.95, 4.0, 0.9),
    "flap3": (6.0, -32.0, 0.62, 48.0, 0.6),
    "glide": (None, None, 1.0, 0.0, 1.0),
    "closed": (0.0, 0.0, 0.0, 0.0, 0.25),
    "perch": (0.0, 0.0, 0.0, 0.0, 0.25),
}


def body_stations(sp, lod):
    """The body's lofted stations: (x, y centre, half width, half height) tail base -> bill base."""
    L = sp.length * (1.0 - sp.tail * 0.82)       # body, head and bill without the tail
    W, H = sp.girth * 0.5, sp.depth * 0.5
    hd = sp.head
    nk = sp.neck
    # (fraction of L, w, h, y centre as a share of H)
    neck0 = 0.66
    neck1 = 0.66 + 0.06 * nk
    head0 = neck1 + 0.02
    total = head0 + 0.2 * hd
    key = [
        (0.00, 0.30, 0.22, 0.30), (0.06, 0.52, 0.45, 0.15), (0.18, 0.82, 0.80, 0.02), (0.32, 1.00, 0.98, -0.06),
        (0.48, 0.98, 1.00, -0.04), (0.60, 0.80, 0.86, 0.10), (neck0, 0.58, 0.62, 0.28),
        (neck1, 0.36 * hd + 0.1, 0.4 * hd + 0.1, 0.45 + 0.4 * (nk - 1)), (head0 + 0.04 * hd, 0.48 * hd, 0.52 * hd, 0.5 + 0.4 * (nk - 1)),
        (head0 + 0.11 * hd, 0.5 * hd, 0.52 * hd, 0.52 + 0.4 * (nk - 1)), (head0 + 0.17 * hd, 0.36 * hd, 0.38 * hd, 0.46 + 0.4 * (nk - 1)),
        (total, 0.18 * hd * (0.6 + sp.bill_d), 0.2 * hd * (0.6 + sp.bill_d), 0.4 + 0.4 * (nk - 1)),
    ]
    key = np.array(key, float)
    key[:, 0] *= L / total
    x0 = -L * 0.45
    n = {0: 30, 1: 14, 2: 7}[lod]
    S = catmull(key, n)
    out = np.stack([x0 + S[:, 0], S[:, 3] * H, S[:, 1] * W, S[:, 2] * H], 1)
    return out, x0, L, (x0 + neck0 * L * (1.0 / 1.0) * 0 + key[6, 0] + x0 * 0)


def build_bird(sp, pose, lod):
    M = Mesh()
    st, x0, L, _ = body_stations(sp, lod)
    around = {0: 22, 1: 10, 2: 6}[lod]
    perch = pose == "perch"
    xs = st[:, 0]
    u_of = lambda x: (x - (x0 - sp.length * sp.tail * 0.82)) / sp.length   # 0 tail tip .. 1 bill tip
    neck_x = x0 + L * 0.62
    # --- body loft
    P, Uu, Vv = [], [], []
    for (x, yc, w, h) in st:
        for j in range(around):
            ph = 2 * math.pi * j / around
            cy, sz = math.cos(ph), math.sin(ph)
            belly = 1.0 + (0.12 if cy < 0 else 0.0) * min(1.0, max(0.0, (neck_x - x) / L * 2))
            P.append([x, yc + h * cy * belly, w * sz])
            Uu.append(0); Vv.append((1 - cy) * 0.5)
    P = np.array(P)
    F = list(grid_faces(len(st), around, wrap=True))
    c0 = len(P)
    P = np.vstack([P, [st[0, 0] - 0.002, st[0, 1], 0], [st[-1, 0] + 0.001, st[-1, 1], 0]])
    Vv = list(Vv) + [0.5, 0.5]
    for j in range(around):
        F.append((c0, j, (j + 1) % around))
        F.append((c0 + 1, (len(st) - 1) * around + (j + 1) % around, (len(st) - 1) * around + j))
    F = np.array(F)
    # make sure it faces out
    u = np.clip((P[:, 0] - x0) / (st[-1, 0] - x0), 0, 1)   # (the body's own: 0 the tail's root .. 1 the bill's)
    c = plumage(sp, "body", u, np.array(Vv), True, 0, 1, P)
    head_x = st[-1, 0]
    head_y = st[-1, 1]
    M.add(P, F, c, FEATHER, flip=True)
    # --- bill
    hb = st[-1]
    bl = sp.bill
    nb = {0: 8, 1: 4, 2: 2}[lod]
    if lod < 2:
        pts, rad = [], []
        for i in range(nb + 1):
            t = i / nb
            hook = sp.hook * (smooth(0.55, 1.0, t)) * bl * 0.45
            pts.append([hb[0] + bl * t * (1 - 0.15 * sp.hook * t), hb[1] - hook - 0.1 * bl * t * (sp.bill_d - 0.4), 0.0])
            rad.append(max(0.0006, (1 - t ** 1.3) * hb[3] * 0.62 * (0.7 + 0.3 * sp.bill_d)))
        bv, bf = tube(np.array(pts), np.array(rad), {0: 8, 1: 5}[lod])
        bu = (bv[:, 0] - hb[0]) / bl
        bvv = np.where(bv[:, 1] < hb[1] - 0.001, 1.0, 0.0)
        M.add(bv, bf, plumage(sp, "bill", 0.5 + 0.5 * np.clip(bu, 0, 1), bvv, True, 0, 1, bv), BARE)
        if sp.name in ("buzzard", "red_kite", "kestrel", "wood_pigeon"):
            cv, cf = ellipsoid([hb[0] + bl * 0.08, hb[1] + hb[3] * 0.25, 0], [bl * 0.16, hb[3] * 0.4, hb[2] * 0.75], {0: 8, 1: 5}[lod], {0: 5, 1: 3}[lod])
            M.add(cv, cf, plumage(sp, "cere", np.zeros(len(cv)), np.zeros(len(cv)), True, 0, 1, cv), BARE)
    # --- eyes
    if lod < 2:
        ex = head_x - (st[-1, 0] - st[-4, 0]) * 0.55
        k = np.argmin(np.abs(st[:, 0] - ex))
        er = max(0.0035, st[k, 2] * (0.28 if sp.name in ("buzzard", "red_kite", "kestrel") else 0.22))
        for s in (-1, 1):
            ec = [ex, st[k, 1] + st[k, 3] * 0.22, s * st[k, 2] * 0.86]
            ev, ef = ellipsoid(ec, [er, er, er * 0.7], {0: 10, 1: 6}[lod], {0: 6, 1: 4}[lod])
            M.add(ev, ef, plumage(sp, "eye", np.zeros(len(ev)), np.zeros(len(ev)), True, 0, 1, ev), EYE)
            if lod == 0:
                rv, rf = ellipsoid([ec[0], ec[1], ec[2] - s * er * 0.15], [er * 1.35, er * 1.35, er * 0.6], 10, 6)
                M.add(rv, rf, plumage(sp, "eyering", np.zeros(len(rv)), np.zeros(len(rv)), True, 0, 1, rv), BARE)
    # --- tail
    ext, hand_a, ext_k, sweep, tail_f = POSE_WING[pose]
    tl = sp.length * sp.tail
    tx = st[0, 0] + 0.02 * L
    ty = st[0, 1]
    nt = {0: 12, 1: 6, 2: 1}[lod]
    spread = math.radians(sp.tail_spread * tail_f)
    tw = sp.girth * sp.tail_w
    # the tail's coverts: a flattened wedge from the rump over the feathers' roots, so a shut tail has some depth
    if lod < 2:
        k = {0: 6, 1: 3}[lod]
        pts = np.array([[tx + 0.03 * L - tl * 0.55 * i / k, ty + st[0, 3] * 0.15 * (1 - i / k), 0.0] for i in range(k + 1)])
        rad = np.array([st[2, 3] * 0.75 * (1 - 0.85 * (i / k) ** 1.1) + 0.002 for i in range(k + 1)])
        cv, cf = tube(pts, rad, {0: 10, 1: 6}[lod])
        cv[:, 1] = ty + (cv[:, 1] - ty) * 0.6           # flattened top to bottom
        cv[:, 2] *= 1.0 + 0.6 * tail_f                   # wider as the tail spreads
        cu = np.clip((cv[:, 0] - x0) / (st[-1, 0] - x0), 0, 1)
        M.add(cv, cf, plumage(sp, "body", cu, np.where(cv[:, 1] > ty, 0.0, 1.0), True, 0, 1, cv), FEATHER)
    for i in range(nt):
        q = (i + 0.5) / nt - 0.5 if nt > 1 else 0.0
        a = q * spread
        length = tl * (1.0 + sp.fork * (abs(q) * 2) ** 1.5 - 0.06 * (1 - abs(q) * 2) * (sp.fork == 0) * 0)
        if sp.name == "pheasant":
            length = tl * (0.35 + 0.65 * (1 - abs(q) * 2) ** 1.8)
        d = np.array([-math.cos(a), 0.0, math.sin(a)])
        wid = (max(tw / max(1, nt) * 3.2, tw * 0.42) if nt > 1 else tw) * (1.0 if sp.name != "pheasant" else 0.6)
        wf = (lambda t, wid=wid: wid * (0.75 + 0.35 * math.sin(math.pi * min(1, t * 1.1)) ** 0.5) * (1 - 0.5 * max(0, t - 0.85) / 0.15)) if sp.name != "pheasant" \
            else (lambda t, wid=wid: wid * (1.0 - 0.85 * t))
        root = np.array([tx, ty + 0.002 * (i % 2) + (1.0 - tail_f) * 0.008 * (0.5 - abs(q)) * sp.girth / 0.1, 0.0]) + np.array([0, 0, q * sp.girth * 0.35])
        side = np.array([0.0, 0.0, 1.0 if q >= 0 else -1.0])
        segs = {0: 6, 1: 3, 2: 1}[lod]
        sd = np.cross(d, [0, 1, 0]) * (1 if q >= 0 else -1) + np.array([0, -0.45 * (1.0 - tail_f) * (1 if q != 0 else 0), 0])   # (a shut tail folds like a roof)
        bp, bf, (bu, bv, btop) = blade(root, d, sd, length, wf, np.array([0, 1, 0]), segs,
                                      curl=0.0, thick=0.003 if lod < 2 else 0.004, asym=0.5)
        cols_ = np.zeros((len(bp), 3))
        for tflag in (True, False):
            m = btop == tflag
            cols_[m] = plumage(sp, "tail", bu[m], bv[m], tflag, i, nt, bp[m])
        M.add(bp, bf, cols_, FEATHER)
    # --- wings
    folded = pose in ("closed", "perch")
    semi = sp.span * 0.5
    sx = st[np.argmin(np.abs(st[:, 0] - (x0 + L * 0.46)))]
    shoulder = np.array([x0 + L * 0.48, sx[1] + sx[3] * 0.55, sx[2] * 0.7])
    for side in (1, -1):
        W = Mesh()
        if folded:
            fold_wing(sp, W, shoulder, st, lod, tl)
        else:
            spread_wing(sp, W, pose, shoulder, lod)
        P, F, Nn, C, A = W.arrays()
        if side < 0:
            P = P * np.array([1, 1, -1])
            F = F[:, ::-1]
        M.add(P, F, C, A)
    # --- legs
    if perch and lod < 2:
        legs(sp, M, st, lod)
    P, F, Nn, C, A = M.arrays()
    if perch:
        # stand it up on its feet: the body pitched up by `upright` about the feet, the head turned back to level
        P = perch_pose(sp, P, st)
    return M, P, F, C, A


def spread_wing(sp, W, pose, shoulder, lod):
    """The right wing spread (z > 0), made in its plane (span s outward, chord c back) then bent up by the
    pose's shoulder and hand angles."""
    ext, hand_a, ext_k, sweep, _ = POSE_WING[pose]
    if ext is None:
        ext, hand_a = sp.glide_up, sp.glide_hand
    semi = sp.span * 0.5 - shoulder[2]
    arm = semi * (1 - sp.hand)
    hand = semi * sp.hand
    chord = sp.chord * sp.span * 0.5
    # the planform: leading edge x(s) (relative to the shoulder), trailing edge
    arm_s = arm * (0.7 + 0.3 * ext_k)
    n_arm = {0: 12, 1: 5, 2: 2}[lod]
    a_sh, a_h = math.radians(ext), math.radians(hand_a)
    sweep_r = math.radians(sweep + (8.0 if sp.name == "red_kite" else 0.0) + (6.0 if sp.name == "gull" else 0.0))

    def bend(s, c, y):
        """plane (s span, c chord back, y up from the plane) -> 3D relative to the shoulder; the shoulder
        rotates the whole wing by a_sh about x, the wrist the hand by a_h more (smoothly)."""
        s = np.asarray(s, float); c = np.asarray(c, float); y = np.asarray(y, float)
        ds = 0.004
        out = np.zeros(s.shape + (3,))
        # integrate the span curve
        grid = np.linspace(0, max(semi * 1.3, s.max() + 0.01), 200)
        ang = a_sh + a_h * smooth(arm_s - 0.04 * semi, arm_s + 0.06 * semi, grid)
        dy = np.sin(ang); dz = np.cos(ang)
        cy = np.concatenate([[0], np.cumsum((dy[1:] + dy[:-1]) * 0.5 * np.diff(grid))])
        cz = np.concatenate([[0], np.cumsum((dz[1:] + dz[:-1]) * 0.5 * np.diff(grid))])
        Y = np.interp(s, grid, cy); Z = np.interp(s, grid, cz); A = np.interp(s, grid, ang)
        out[..., 0] = -c
        out[..., 1] = Y + y * np.cos(A)
        out[..., 2] = Z - y * np.sin(A)
        return out + shoulder

    # the hand's length and the bone the primaries grow from
    hand_len = hand * (0.6 + 0.4 * ext_k)
    bone = 0.32 * hand_len
    wing_end = arm_s + bone
    # one cambered shell (the coverts) from the shoulder out over the wrist to the end of the hand bone;
    # its leading edge comes forward to the wrist and sweeps back over the hand
    def le(s):
        s = np.asarray(s, float)
        a = -0.05 * chord + 0.11 * chord * smooth(0.0, arm_s, s)
        return a + np.maximum(0.0, s - arm_s) * (0.35 + math.tan(sweep_r) * 0.8)

    def ch(s):
        s = np.asarray(s, float)
        arm_c = chord * (1.02 - 0.18 * np.minimum(s, arm_s) / arm_s) * (0.75 + 0.25 * ext_k)
        return arm_c * (1.0 - 0.45 * smooth(arm_s - 0.02 * semi, wing_end, s))

    def u_at(s):
        s = np.asarray(s, float)
        return np.where(s < arm_s, 0.5 * s / arm_s, 0.5 + 0.5 * (s - arm_s) / hand)

    nc = {0: 7, 1: 4, 2: 2}[lod]
    n_hand = {0: 5, 1: 2, 2: 1}[lod]
    S = np.concatenate([np.linspace(0.0, arm_s, n_arm + 1), arm_s + bone * np.linspace(0, 1, n_hand + 1)[1:]])
    rows = []
    for top in (True, False):
        pts, uu, vv = [], [], []
        for s in S:
            reach = (0.8 if lod < 2 else 1.0) - 0.25 * smooth(arm_s, wing_end, s)   # (the coverts' share of the chord)
            for j in range(nc + 1):
                q = j / nc
                c = le(s) + ch(s) * q * reach
                taper = 1.0 - 0.6 * smooth(0, wing_end, s)
                th = chord * 0.09 * (1 - q) ** 1.5 * taper * (1 if top else -0.5)
                cam = chord * 0.05 * math.sin(math.pi * min(1.0, q * 1.2)) * taper
                pts.append((s, float(c), cam + th)); uu.append(float(u_at(s))); vv.append(q * reach)
        pts = np.array(pts)
        P3 = bend(pts[:, 0], pts[:, 1], pts[:, 2])
        F = grid_faces(len(S), nc + 1)
        col = plumage(sp, "wing", np.array(uu), np.array(vv), top, -1, 1, P3)
        W.add(P3, F if not top else F[:, ::-1], col, FEATHER)
        rows.append((P3, col))
    # (close the shell's leading edge and outer end)
    (T, Tc), (B, Bc) = rows
    m = nc + 1
    F = []
    for i in range(len(S) - 1):
        a, b = i * m, (i + 1) * m
        F += [(a, b, len(T) + a), (b, len(T) + b, len(T) + a)]
    i = len(S) - 1
    for j in range(nc):
        a, b = i * m + j, i * m + j + 1
        F += [(a, len(T) + a, b), (b, len(T) + a, len(T) + b)]
    W.add(np.vstack([T, B]), np.array(F), np.vstack([Tc, Bc]), FEATHER)
    # secondaries: a row of overlapping blades along the arm's trailing edge
    ns = {0: 12, 1: 5, 2: 0}[lod]
    for i in range(ns):
        s = arm_s * (i + 0.5) / ns
        c0 = float(le(s) + ch(s) * 0.5)
        root = np.array([s, c0, 0.0])
        length = float(ch(s)) * 0.52
        wid = arm_s / ns * 1.9
        pts = blade_plane(root, length, wid, {0: 3, 1: 2}[lod], math.radians(-4), tip_round=True)
        place_blade(sp, W, bend, pts, 0.5 * s / arm_s, i, ns, sec=True)
    # the primaries fanned from the hand bone, overlapping, the outer ones fingered on the broad-winged
    np_ = sp.prims if lod == 0 else (5 if lod == 1 else 3)
    for i in range(np_):
        q = i / max(1, np_ - 1)          # 0 inner .. 1 outer
        sb = arm_s + bone * q
        base = np.array([sb, float(le(sb) + ch(sb) * 0.3), 0.0])
        ang_back = (1 - q) ** 1.2 * math.radians(62.0 - 20 * sp.point) + (0.0 if sp.fingers == 0 else -math.radians(6) * q)
        ang_back += sweep_r * (0.4 + 0.6 * q)
        if sp.point > 0.6:
            ang_back += math.radians(14.0 * sp.point) * q
        reach = hand_len - bone * q
        length = reach * (0.6 + 0.4 * (q ** (0.6 + sp.point)))
        if sp.point > 0.6 and q > 0.85:
            length *= 1.0 - 0.4 * (q - 0.85) / 0.15 * (1 - sp.point) - 0.03
        if sp.point <= 0.6 and q > 0.8:
            length *= 1.0 - 0.35 * (q - 0.8) / 0.2 * (1 - sp.point)
        wid = chord * (0.46 if lod == 0 else 0.62 if lod == 1 else 1.0) * (0.8 + 0.2 * (1 - q))
        finger = sp.fingers and q > 1 - (sp.fingers - 0.5) / np_
        pts = blade_plane(base, length, wid, {0: 6, 1: 3, 2: 1}[lod], ang_back, tip_round=not (sp.point > 0.6),
                          emarg=(0.5 if finger else 0.0))
        curl = (0.06 * length if finger and pose == "glide" else 0.0) + (0.04 * length if pose == "flap1" else 0.0)
        place_blade(sp, W, bend, pts, 0.5 + 0.5 * (sb - arm_s) / hand, i, np_, sec=False, curl=curl, span_u=0.5 * length / hand)



def blade_plane(root, length, width, segs, ang_back, tip_round=True, emarg=0.0):
    """A feather's outline in the wing plane (s, c): rows of (leading edge, middle, trailing edge) points
    from root to tip, pointing out along +s turned back by ang_back. Returns (segs+1) x 3 x 2."""
    d = np.array([math.cos(ang_back), math.sin(ang_back)])
    n = np.array([-d[1], d[0]])     # toward the trailing edge
    rows = []
    for i in range(segs + 1):
        t = i / segs
        w = width * (math.sqrt(max(0.0, 1 - t ** 4)) if tip_round else (1 - t ** 1.6))
        if emarg:
            w *= 1 - emarg * smooth(0.35, 0.55, t)
        w = max(w, width * 0.04)
        c = np.array(root[:2]) + d * length * t
        rows.append([c - n * w * 0.3, c + n * w * 0.15, c + n * w * 0.7])
    return np.array(rows)


def place_blade(sp, W, bend, pts, u, k, n, sec=False, curl=0.0, span_u=0.0):
    rows, cols = pts.shape[0], pts.shape[1]
    for top in (True, False):
        y = np.zeros((rows, cols))
        t = np.linspace(0, 1, rows)[:, None]
        y += curl * t * t
        y += (0.0015 if top else -0.0015) + 0.0006 * k * (1 if not sec else 0)
        P3 = bend(pts[:, :, 0].ravel(), pts[:, :, 1].ravel(), y.ravel())
        tt = np.repeat(t.ravel(), cols)
        uu = np.clip(np.full(rows * cols, u) + tt * span_u, 0, 1)
        vv = (0.55 + 0.45 * tt) if sec else (np.tile(np.linspace(0, 1, cols), rows) * 0.3 + 0.35 + 0.3 * tt)
        F = grid_faces(rows, cols)
        W.add(P3, F[:, ::-1] if top else F, plumage(sp, "wing", uu, np.clip(vv, 0, 1), top, k, n, P3), FEATHER)


def fold_wing(sp, W, shoulder, st, lod, tl):
    """The right wing folded along the flank: a long cambered shell from the shoulder back over the rump,
    the primaries' tips crossing over the tail."""
    length = sp.span * 0.5 * (0.52 if sp.point > 0.6 else 0.46)
    n = {0: 14, 1: 6, 2: 3}[lod]
    m = {0: 7, 1: 4, 2: 2}[lod]
    pts, uu, vv, tops = [], [], [], []
    for top in (True, False):
        for i in range(n + 1):
            t = i / n
            x = shoulder[0] + 0.02 * sp.girth - length * t
            k = np.clip(np.searchsorted(st[:, 0], x), 1, len(st) - 1)
            yc, w, h = np.interp(x, st[:, 0], st[:, 1]), max(np.interp(x, st[:, 0], st[:, 2]), sp.girth * 0.2), max(np.interp(x, st[:, 0], st[:, 3]), sp.depth * 0.2)
            hgt = sp.depth * (0.55 - 0.25 * t) * (1 - t ** 3 * 0.6)
            for j in range(m + 1):
                q = j / m
                ang = math.radians(70 - 95 * q)              # from over the back down the flank
                r = 1.06 + (0.03 if top else 0.0)
                yy = yc + h * 0.85 * math.sin(ang) * (1.0 - 0.3 * t) * r * 0.9 + (0.25 * h * t if t > 0.6 else 0)
                zz = w * r * math.cos(ang) * (1.0 - 0.55 * t ** 1.5) + 0.004
                if not top:
                    zz -= 0.004
                pts.append((x, yy, zz)); uu.append(0.2 + 0.75 * t); vv.append(q); tops.append(top)
    P = np.array(pts)
    half = (n + 1) * (m + 1)
    F = grid_faces(n + 1, m + 1)
    for top in (True, False):
        sl = slice(0, half) if top else slice(half, 2 * half)
        col = plumage(sp, "wing", np.array(uu[sl]), np.array(vv[sl]), True, -1 if top else 0, 1, P[sl])
        W.add(P[sl], F[:, ::-1] if top else F, col, FEATHER)


def legs(sp, M, st, lod):
    """Two legs, standing: thighs into the belly, tarsi down to toes gripping a perch at y = foot level."""
    x0 = st[np.argmax(st[:, 3]), 0]
    belly = st[np.argmax(st[:, 3]), 1] - st[np.argmax(st[:, 3]), 3] * 0.9
    L = sp.leg
    r = max(0.0025, L * 0.07)
    sides = {0: 6, 1: 4}[lod]
    for s in (-1, 1):
        top = np.array([x0 + 0.02 * sp.length, belly + 0.01, s * sp.girth * 0.22])
        # (the body will be pitched about the feet, so the legs are drawn in the pitched frame afterwards; here
        # they go straight down a little forward of the belly)
        foot = top + np.array([0.0, -L * 1.15, 0.0])
        pts = np.array([top, (top + foot) * 0.5 + np.array([0.004, 0, 0]), foot])
        v, f = tube(pts, np.array([r * 1.7, r, r * 0.9]), sides)
        M.add(v, f, plumage(sp, "leg", np.zeros(len(v)), np.zeros(len(v)), True, 0, 1, v), BARE)
        # toes: three forward, one back, curled round the perch
        for a in (-0.35, 0.0, 0.35, math.pi):
            d = np.array([math.cos(a), 0.0, math.sin(a) * s])
            tl = L * (0.5 if a != math.pi else 0.35)
            tp = np.array([foot + d * tl * 0.0, foot + d * tl * 0.6 + np.array([0, -0.004, 0]), foot + d * tl + np.array([0, -0.012, 0])])
            v, f = tube(tp, np.array([r * 0.8, r * 0.6, r * 0.3]), max(4, sides - 2))
            M.add(v, f, plumage(sp, "leg", np.zeros(len(v)), np.zeros(len(v)), True, 0, 1, v) * (0.6 if lod == 0 and a == 0 else 1.0), BARE)
    M.foot_y = belly - L * 1.15 - 0.012


def perch_pose(sp, P, st):
    """Pitch the bird up about its feet, its head bent back down to level; origin to the feet."""
    x0 = st[np.argmax(st[:, 3]), 0] + 0.02 * sp.length
    belly = st[np.argmax(st[:, 3]), 1] - st[np.argmax(st[:, 3]), 3] * 0.9
    foot = np.array([x0, belly - sp.leg * 1.15 - 0.012, 0.0])
    a = math.radians(sp.upright)
    Q = P.copy()
    is_leg = (Q[:, 1] < belly - 0.002) & (np.abs(Q[:, 0] - x0) < sp.leg * 0.8)
    neck_x = st[0, 0] + (st[-1, 0] - st[0, 0]) * 0.62
    rel = Q - foot
    # body: rotate about the hip (just above the feet) by a
    hip = np.array([x0, belly + 0.005, 0.0]) - foot
    R = rot_z(a)
    body = ~is_leg
    rel_b = rel[body] - hip
    # the head and neck are turned back by most of the pitch so the bird looks out level
    hx = rel_b[:, 0]
    nk = neck_x - x0
    head = smooth(nk - 0.02, nk + 0.02, hx)
    rb = rel_b @ R.T
    neck_pt = (np.array([nk, st[np.argmin(np.abs(st[:, 0] - neck_x)), 1] - foot[1] - hip[1], 0.0])) @ R.T
    Rh = rot_z(-a * 0.92)
    rh = (rb - neck_pt) @ Rh.T + neck_pt
    rb = rb * (1 - head[:, None]) + rh * head[:, None]
    rel[body] = rb + hip
    return rel


# --- output ------------------------------------------------------------------------------------------------

def write_model(name, P, F, C, A):
    Nn = np.zeros_like(P)
    a, b, c = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]
    fn = np.cross(b - a, c - a)
    for i in range(3):
        np.add.at(Nn, F[:, i], fn)
    Nn /= np.maximum(np.linalg.norm(Nn, axis=1, keepdims=True), 1e-12)
    col = np.zeros((len(P), 4))
    col[:, :3] = np.clip(C, 0, 1)   # (linear, as the other kits write them)
    col[:, 3] = A
    col = np.round(col * 255).astype(np.uint8)
    uv = np.stack([P[:, 0], P[:, 2]], 1)
    write_glb(os.path.join(OUT, name + ".glb"), P.astype(np.float32), F, Nn.astype(np.float32), col, name, uv.astype(np.float32))


def make_all(only=None):
    os.makedirs(OUT, exist_ok=True)
    rows = {}
    for sp in SPECIES:
        if only and sp.name not in only:
            continue
        info = {"poses": sp.poses, "span": sp.span, "length": sp.length, "tris": {}}
        for pose in sp.poses:
            for lod in (0, 1, 2):
                M, P, F, C, A = build_bird(sp, pose, lod)
                name = "SM_Bird_%s_%s%s" % (sp.name, pose, "" if lod == 0 else "_LOD%d" % lod)
                write_model(name, P, F, C, A)
                info["tris"].setdefault(pose, []).append(int(len(F)))
        rows[sp.name] = info
        print("%-12s %s" % (sp.name, " ".join("%s %d/%d/%d" % (p, *info["tris"][p]) for p in sp.poses)))
    spec = {"note": "Tools/make_ride_birds.py: the birds' models (World/TorqueRideBirds); lod screen sizes for ue_build_rideuk bird_kit",
            "hand_lods": [0.08, 0.02], "species": rows}
    if not only:
        json.dump(spec, open(os.path.join(OUT, "birds.json"), "w"), indent=1)
    return rows


if __name__ == "__main__":
    args = sys.argv[1:]
    only = None
    if "--only" in args:
        only = args[args.index("--only") + 1].split(",")
    make_all(only)
    if "--render" in args:
        import render_birds   # noqa: E402  (Tools/render_birds.py, Blender)
        render_birds.main(OUT, args[args.index("--render") + 1], only)
