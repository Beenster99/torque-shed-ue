"""Ride UK's detached houses: a kit of whole, detailed UK houses modelled by script (beenster Nov 26: "Can we
build these with blender and use as some of the houses", four sheets of commercial house packs as the look to
aim for). Our own original designs in that general style, from general knowledge of British houses: nothing of
any pack's models is copied. Three sets:

  estate  red or buff brick new-build detached houses: grey or brown concrete tiles, white uPVC fascias and
          casements, soldier-course heads, a dark engineering-brick plinth, canopy porches (often run on over
          an integral garage), canted bays under tiled roofs, gabled dormers breaking the eaves, stacks on the
          gable ends.
  villa   bigger Edwardian and inter-war style houses, two and three storeys: hipped slate roofs, tall stacks
          with pots, stone sills and heads, string courses, gabled porch hoods, dormers, some with solar panels.
  render  cream or white rendered and painted houses with slate or tile roofs; two weatherboarded (the Kent,
          Essex and coastal kind).

Each house is built from blocks (a box of walls under a gabled, hipped or lean-to roof) with patterns of
windows, doors, garage doors and bays per floor of each wall, so the designs read as data. Every part is
tagged with the furthest level of detail it stays in: 2 the far shell (walls with their openings, the glass,
the roof planes, the stacks), 1 the middle (frames, sills, heads, fascias, gutters, ridge tiles), 0 close only
(glazing bars, handles, letter plates, brackets, pots' rims, garage door ribs). Each part also has a role
(brick, tile, trim, door ...) so the game can recolour a design into its colourways.

Frame here: x along the front (left to right seen from the road), y into the house, z up, the front wall's
foot at y = 0, the ground at z = 0; walls run 0.6 m below ground so a slope never shows under them.

    python Tools/blender_house_kit.py                 # writes Data/RideUK/house_kit.bin and .json
    python Tools/blender_house_kit.py --render DIR    # and the review sheets (needs bpy: pip install bpy)

World/WorldRideHouseKit.cpp lays them on the map's detached houses.
"""
import json
import math
import os
import struct
import sys
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(os.path.dirname(HERE), "Data", "RideUK")
FOOT = -0.6

# --- colours (linear) --------------------------------------------------------------------------------------
C = {
    "brick_red": (0.27, 0.095, 0.058), "brick_orange": (0.34, 0.13, 0.07), "brick_buff": (0.42, 0.29, 0.16),
    "brick_brown": (0.2, 0.1, 0.065), "brick_victorian": (0.3, 0.085, 0.05), "brick_dark": (0.035, 0.032, 0.034),
    "soldier_red": (0.22, 0.075, 0.048), "soldier_buff": (0.34, 0.22, 0.12),
    "tile_grey": (0.075, 0.072, 0.07), "tile_brown": (0.1, 0.075, 0.06), "tile_red": (0.22, 0.085, 0.05),
    "slate": (0.06, 0.062, 0.07), "white": (0.78, 0.78, 0.76), "cream_trim": (0.7, 0.66, 0.55),
    "anthracite": (0.03, 0.032, 0.035), "black": (0.012, 0.012, 0.013), "stone": (0.55, 0.5, 0.4),
    "render_cream": (0.66, 0.6, 0.46), "render_white": (0.74, 0.73, 0.69), "render_grey": (0.5, 0.5, 0.48),
    "boards_white": (0.7, 0.7, 0.67), "boards_black": (0.025, 0.025, 0.027), "boards_sage": (0.32, 0.38, 0.3),
    "concrete": (0.4, 0.39, 0.36), "terracotta": (0.32, 0.12, 0.06), "lead": (0.16, 0.17, 0.18),
    "solar": (0.01, 0.015, 0.04), "alu": (0.45, 0.46, 0.47), "chrome": (0.6, 0.6, 0.6),
    "door_grey": (0.05, 0.055, 0.06), "door_navy": (0.012, 0.02, 0.05), "door_red": (0.2, 0.02, 0.02),
    "door_green": (0.02, 0.06, 0.035), "door_oak": (0.16, 0.08, 0.035), "door_white": (0.75, 0.75, 0.73),
    "glass": (0.0, 0.0, 0.0), "glass_lit": (0.42, 0.26, 0.11),
}

# role -> (section, code) for the game: 0 plain, 1 masonry, 2 roofing (code the courses), 3 brick, 4 render,
# 5 glass (M_RideNight: the colour the light after dark, alpha 0 dark glass by day)
ROLE_SECTION = {
    "brick": (3, 0.06), "brick2": (3, 0.06), "soldier": (3, 0.06), "plinth": (3, 0.1),
    "render": (4, 0.08), "stone": (4, 0.05), "step": (4, 0.12), "flaunch": (4, 0.1),
    "tile": (2, None), "ridge": (2, None), "boards": (2, 0.75), "lead": (2, 1.0), "solar": (0, 1.0),
    "trim": (0, 1.0), "frame": (0, 1.0), "door": (0, 1.0), "garage": (0, 1.0), "gutter": (0, 1.0),
    "metal": (0, 1.0), "pot": (0, 1.0), "solar_frame": (0, 1.0), "glass": (5, 0.0), "glass_lit": (5, 0.0),
    "post": (0, 1.0),
}
TILE_CODE = {"tile": 0.5, "slate": 0.0}

# glass and light stay as they are in every colourway
FIXED_ROLES = {"glass": C["glass"], "glass_lit": C["glass_lit"], "metal": C["chrome"], "solar": C["solar"],
               "solar_frame": C["alu"], "step": C["concrete"], "flaunch": C["concrete"], "plinth": C["brick_dark"],
               "lead": C["lead"], "pot": C["terracotta"]}


def rnd(seed, *k):
    return (zlib.crc32(("%s|" % seed + "|".join(str(x) for x in k)).encode()) & 0xFFFFFF) / float(0x1000000)


# --- the mesh ----------------------------------------------------------------------------------------------

class House:
    """Triangles by (role, lod): positions (n, 3, 3) and each corner's normal (n, 3, 3)."""
    def __init__(self, name):
        self.name = name
        self.parts = {}
        self.lit_seed = name

    def tri(self, role, lod, pts, out=None, normals=None):
        """A convex polygon (3+ points), fanned, wound to face out (a direction) where given."""
        pts = [np.asarray(p, float) for p in pts]
        n = np.cross(pts[1] - pts[0], pts[2] - pts[0])
        for i in range(2, len(pts) - 1):
            if np.linalg.norm(n) > 1e-9:
                break
            n = np.cross(pts[i] - pts[0], pts[i + 1] - pts[0])
        ln = np.linalg.norm(n)
        if ln < 1e-12:
            return
        n = n / ln
        if out is not None and np.dot(n, out) < 0:
            pts = pts[::-1]
            n = -n
        P, N = self.parts.setdefault((role, lod), ([], []))
        for i in range(1, len(pts) - 1):
            P.append([pts[0], pts[i], pts[i + 1]])
            if normals is None:
                N.append([n, n, n])
            else:
                N.append([normals[0], normals[i], normals[i + 1]])

    def quad(self, role, lod, a, b, c, d, out=None):
        self.tri(role, lod, [a, b, c, d], out)

    def box(self, role, lod, lo, hi, skip=()):
        """An axis box."""
        x0, y0, z0 = lo
        x1, y1, z1 = hi
        if x1 - x0 < 1e-6 or y1 - y0 < 1e-6 or z1 - z0 < 1e-6:
            return
        cx, cy, cz = (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2
        F = {
            "-x": ([(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1)], (-1, 0, 0)),
            "+x": ([(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)], (1, 0, 0)),
            "-y": ([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0)),
            "+y": ([(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)], (0, 1, 0)),
            "-z": ([(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)], (0, 0, -1)),
            "+z": ([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1)),
        }
        for k, (p, o) in F.items():
            if k not in skip:
                self.tri(role, lod, p, o)

    def obox(self, role, lod, centre, ax, ay, az, skip=()):
        """A box about centre with half-axes ax, ay, az (any directions)."""
        c = np.asarray(centre, float)
        A, B, Z = np.asarray(ax, float), np.asarray(ay, float), np.asarray(az, float)
        for s, (u, v, w), k in ((1, (A, B, Z), "+a"), (-1, (A, B, Z), "-a"), (1, (B, A, Z), "+b"), (-1, (B, A, Z), "-b"),
                                (1, (Z, A, B), "+c"), (-1, (Z, A, B), "-c")):
            if k in skip:
                continue
            f = c + s * u
            self.tri(role, lod, [f - v - w, f + v - w, f + v + w, f - v + w], s * u)

    def sweep(self, role, lod, path, profile, smooth=True, cap=False):
        """A profile [(a, b)] (a across, b up, about the path) swept along a straight or bent path of points."""
        path = [np.asarray(p, float) for p in path]
        rings = []
        for i, p in enumerate(path):
            t = (path[min(i + 1, len(path) - 1)] - path[max(i - 1, 0)])
            t /= np.linalg.norm(t)
            up = np.array([0, 0, 1.0]) if abs(t[2]) < 0.9 else np.array([1.0, 0, 0])
            a = np.cross(up, t); a /= np.linalg.norm(a)
            b = np.cross(t, a)
            rings.append([p + a * u + b * v for u, v in profile])
        m = len(profile)
        cen = [np.mean(r, axis=0) for r in rings]
        for i in range(len(rings) - 1):
            for j in range(m):
                k = (j + 1) % m
                p0, p1, p2, p3 = rings[i][j], rings[i][k], rings[i + 1][k], rings[i + 1][j]
                mid = (p0 + p1 + p2 + p3) / 4
                out = mid - (cen[i] + cen[i + 1]) / 2
                if smooth:
                    n0 = rings[i][j] - cen[i]; n1 = rings[i][k] - cen[i]; n2 = rings[i + 1][k] - cen[i + 1]; n3 = rings[i + 1][j] - cen[i + 1]
                    ns = [x / max(np.linalg.norm(x), 1e-9) for x in (n0, n1, n2, n3)]
                    pts = [p0, p1, p2, p3]
                    nn = np.cross(p1 - p0, p2 - p0)
                    if np.dot(nn, out) < 0:
                        pts = pts[::-1]; ns = ns[::-1]
                    self.tri(role, lod, pts[:3], None, ns[:3])
                    self.tri(role, lod, [pts[0], pts[2], pts[3]], None, [ns[0], ns[2], ns[3]])
                else:
                    self.quad(role, lod, p0, p1, p2, p3, out)
        if cap:
            for i, s in ((0, -1), (len(rings) - 1, 1)):
                t = path[-1] - path[0]
                self.tri(role, lod, rings[i], s * t)

    def lathe(self, role, lod, base, prof, seg=8):
        """Round about a vertical axis at base: prof [(radius, height)] bottom to top; a cap on top."""
        base = np.asarray(base, float)
        ring = lambda r, h: [base + np.array([r * math.cos(2 * math.pi * i / seg), r * math.sin(2 * math.pi * i / seg), h]) for i in range(seg)]
        rings = [ring(r, h) for r, h in prof]
        for k in range(len(rings) - 1):
            for i in range(seg):
                j = (i + 1) % seg
                a, b, c, d = rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i]
                mid = (a + b + c + d) / 4
                out = mid - np.array([base[0], base[1], mid[2]])
                na = [(p - np.array([base[0], base[1], p[2]])) for p in (a, b, c, d)]
                na = [x / max(np.linalg.norm(x), 1e-9) for x in na]
                pts = [a, b, c, d]
                if np.dot(np.cross(b - a, c - a), out) < 0:
                    pts = pts[::-1]; na = na[::-1]
                self.tri(role, lod, pts[:3], None, na[:3])
                self.tri(role, lod, [pts[0], pts[2], pts[3]], None, [na[0], na[2], na[3]])
        self.tri(role, lod, rings[-1], (0, 0, 1))

    def counts(self):
        out = [0, 0, 0]
        for (role, lod), (P, N) in self.parts.items():
            for k in range(3):
                if lod >= k:
                    out[k] += len(P)
        return out


# --- a wall --------------------------------------------------------------------------------------------------

class Wall:
    """A wall's face: s along it (left to right seen from outside) from origin o, t out of it, z up."""
    def __init__(self, o, u, length):
        self.o = np.array([o[0], o[1], 0.0])
        self.u = np.array([u[0], u[1], 0.0])
        self.n = np.array([u[1], -u[0], 0.0])
        self.L = length

    def at(self, s, t, z):
        return self.o + self.u * s + self.n * t + np.array([0, 0, z])

    def box(self, H, role, lod, s0, s1, t0, t1, z0, z1, skip=()):
        c = self.at((s0 + s1) / 2, (t0 + t1) / 2, (z0 + z1) / 2)
        H.obox(role, lod, c, self.u * (s1 - s0) / 2, self.n * (t1 - t0) / 2, np.array([0, 0, (z1 - z0) / 2]), skip)

    def rect(self, H, role, lod, s0, s1, z0, z1, t, facing=1.0):
        H.quad(role, lod, self.at(s0, t, z0), self.at(s1, t, z0), self.at(s1, t, z1), self.at(s0, t, z1), self.n * facing)

    def slab(self, H, role, lod, s0, s1, t0, t1, zb0, zb1, zt0, zt1):
        """A board whose top runs from zt0 at s0 to zt1 at s1 (a bargeboard), bottom zb0..zb1, t0..t1 thick."""
        P = lambda s, t, z: self.at(s, t, z)
        for t, f in ((t1, 1), (t0, -1)):
            H.quad(role, lod, P(s0, t, zb0), P(s1, t, zb1), P(s1, t, zt1), P(s0, t, zt0), self.n * f)
        mid_up = np.array([0, 0, 1.0])
        H.quad(role, lod, P(s0, t0, zt0), P(s1, t0, zt1), P(s1, t1, zt1), P(s0, t1, zt0), mid_up)
        H.quad(role, lod, P(s0, t0, zb0), P(s1, t0, zb1), P(s1, t1, zb1), P(s0, t1, zb0), -mid_up)


def wall_face(H, w, role, lod, top, holes, t=0.0, z_low=FOOT, facing=1.0):
    """The wall's face from z_low up to top (points [(s, z)] along it, piecewise straight), with rectangular
    holes [(s0, s1, z0, z1)] cut out: in columns between every corner and hole edge."""
    cuts = sorted(set([p[0] for p in top] + [h[0] for h in holes] + [h[1] for h in holes]))
    cuts = [c for c in cuts if top[0][0] - 1e-6 <= c <= top[-1][0] + 1e-6]

    def z_top(s):
        for (s0, z0), (s1, z1) in zip(top, top[1:]):
            if s0 - 1e-9 <= s <= s1 + 1e-9:
                return z0 if s1 - s0 < 1e-9 else z0 + (z1 - z0) * (s - s0) / (s1 - s0)
        return top[-1][1]

    for a, b in zip(cuts, cuts[1:]):
        if b - a < 1e-5:
            continue
        m = (a + b) / 2
        spans = sorted((h[2], h[3]) for h in holes if h[0] <= m <= h[1])
        z = z_low
        for z0, z1 in spans:
            if z0 > z + 1e-5:
                w.rect(H, role, lod, a, b, z, z0, t, facing)
            z = max(z, z1)
        ta, tb = z_top(a), z_top(b)
        if min(ta, tb) > z + 1e-5:
            H.quad(role, lod, w.at(a, t, z), w.at(b, t, z), w.at(b, t, tb), w.at(a, t, ta), w.n * facing)
        elif max(ta, tb) > z + 1e-5:   # (the top crosses the cut's height: the part above it, a triangle)
            c = a + (b - a) * (z - ta) / (tb - ta)
            if tb > ta:
                H.tri(role, lod, [w.at(c, t, z), w.at(b, t, z), w.at(b, t, tb)], w.n * facing)
            else:
                H.tri(role, lod, [w.at(a, t, z), w.at(c, t, z), w.at(a, t, ta)], w.n * facing)


def reveal(H, w, role, lod, s0, s1, z0, z1, depth, sill=True):
    """The returns of a hole, into the wall."""
    for s, f in ((s0, 1), (s1, -1)):
        H.quad(role, lod, w.at(s, 0, z0), w.at(s, -depth, z0), w.at(s, -depth, z1), w.at(s, 0, z1), w.u * f)
    H.quad(role, lod, w.at(s0, 0, z1), w.at(s1, 0, z1), w.at(s1, -depth, z1), w.at(s0, -depth, z1), (0, 0, -1))
    if sill:
        H.quad(role, lod, w.at(s0, 0, z0), w.at(s1, 0, z0), w.at(s1, -depth, z0), w.at(s0, -depth, z0), (0, 0, 1))


# --- openings ------------------------------------------------------------------------------------------------

REC = 0.1     # how far windows and doors sit back in the wall
FR = 0.065    # a uPVC frame's face


def frame_ring(H, w, role, lod, s0, s1, z0, z1, t0, t1, f=FR):
    w.box(H, role, lod, s0, s0 + f, t0, t1, z0, z1)
    w.box(H, role, lod, s1 - f, s1, t0, t1, z0, z1)
    w.box(H, role, lod, s0 + f, s1 - f, t0, t1, z1 - f, z1)
    w.box(H, role, lod, s0 + f, s1 - f, t0, t1, z0, z0 + f)


def glass_role(H, w, s, z):
    return "glass_lit" if rnd(H.lit_seed, round(float(w.at(s, 0, z)[0]), 1), round(float(w.at(s, 0, z)[1]), 1), round(z, 1)) < 0.38 else "glass"


def window(H, st, w, s0, s1, z0, z1, lights=2, bars=False, sash=False):
    """A uPVC casement (or a sliding sash) set back REC: its frame, mullions, opening lights, glass."""
    t = -REC
    gr = glass_role(H, w, (s0 + s1) / 2, z0)
    w.rect(H, gr, 2, s0, s1, z0, z1, t - 0.03)
    frame_ring(H, w, "frame", 1, s0, s1, z0, z1, t - 0.07, t)
    inner = (s0 + FR, s1 - FR)
    n = max(1, lights)
    edges = [inner[0] + (inner[1] - inner[0]) * k / n for k in range(n + 1)]
    for e in edges[1:-1]:
        w.box(H, "frame", 1, e - 0.035, e + 0.035, t - 0.07, t, z0 + FR, z1 - FR)
    if sash:
        mid = z0 + (z1 - z0) * 0.52
        w.box(H, "frame", 1, inner[0], inner[1], t - 0.05, t + 0.005, mid - 0.03, mid + 0.03)
        if bars:   # (Edwardian: the top sash in small panes, the bottom one whole)
            for k in range(1, 3):
                zz = mid + (z1 - FR - mid) * k / 3
                w.box(H, "frame", 0, inner[0], inner[1], t - 0.045, t - 0.025, zz - 0.012, zz + 0.012)
            for e in edges[:-1]:
                for k in (1, 2):
                    ss = e + (inner[1] - inner[0]) / n * k / 3
                    w.box(H, "frame", 0, ss - 0.012, ss + 0.012, t - 0.045, t - 0.025, mid, z1 - FR)
        return
    if z1 - z0 > 0.7:   # (trickle vents along the head)
        for k in range(max(1, lights // 2 + 1)):
            vs = s0 + (s1 - s0) * (k + 0.5) / max(1, lights // 2 + 1)
            w.box(H, "frame", 0, vs - 0.18, vs + 0.18, t - 0.085, t - 0.07, z1 - FR + 0.012, z1 - 0.012)
    # (the outer lights open: their own sashes a little proud, a top vent over a transom; the middle fixed)
    tz = z1 - FR - min(0.38, (z1 - z0) * 0.3)
    for k in range(n):
        a, b = edges[k] + (0.035 if k else 0), edges[k + 1] - (0.035 if k < n - 1 else 0)
        opener = n == 1 or k == 0 or k == n - 1
        if n >= 2 and (z1 - z0) > 0.9:
            w.box(H, "frame", 1, a, b, t - 0.07, t, tz - 0.03, tz + 0.03)
            frame_ring(H, w, "frame", 0, a, b, tz + 0.03, z1 - FR, t - 0.09, t - 0.07, 0.05)
        if opener:
            frame_ring(H, w, "frame", 0, a, b, z0 + FR, (tz - 0.03) if (n >= 2 and (z1 - z0) > 0.9) else z1 - FR, t - 0.09, t - 0.07, 0.05)
            if (b - a) > 0.3:   # (a handle)
                hs = b - 0.07 if k == 0 else a + 0.07
                w.box(H, "metal", 0, hs - 0.012, hs + 0.012, t - 0.12, t - 0.09, z0 + 0.45, z0 + 0.6)
        if bars:   # (Georgian bars between the panes)
            top = (tz - 0.03) if (n >= 2 and (z1 - z0) > 0.9) else z1 - FR
            mz = (z0 + FR + top) / 2
            w.box(H, "frame", 0, a, b, t - 0.06, t - 0.04, mz - 0.012, mz + 0.012)
            ms = (a + b) / 2
            w.box(H, "frame", 0, ms - 0.012, ms + 0.012, t - 0.06, t - 0.04, z0 + FR, top)


def door(H, st, w, s0, s1, z0, z1, kind="composite", side=0.0):
    """A front door: its frame, a composite leaf with a glazed top, a handle and letter plate; a side light."""
    t = -REC
    frame_ring(H, w, "frame", 1, s0, s1, z0, z1, t - 0.07, t)
    a, b = s0 + FR, s1 - FR
    if side > 0:
        w.box(H, "frame", 1, b - side - 0.04, b - side + 0.02, t - 0.07, t, z0, z1 - FR)
        w.rect(H, glass_role(H, w, b - side / 2, z0 + 1), 2, b - side + 0.02, b, z0 + 0.5, z1 - FR, t - 0.03)
        w.rect(H, "frame", 2, b - side + 0.02, b, z0 + FR, z0 + 0.5, t - 0.03)
        b = b - side - 0.04
    w.rect(H, "door", 2, a, b, z0 + FR, z1 - FR, t - 0.05)
    # (raised panels below, a glazed light above: a half-moon, a strip or squares, by the house)
    g0, g1 = z1 - FR - 0.62, z1 - FR - 0.12
    if kind == "victorian":
        w.rect(H, glass_role(H, w, (a + b) / 2, g0), 2, a + 0.1, b - 0.1, g0 - 0.25, g1, t - 0.06)
        for zz in ((z0 + 0.25, z0 + 0.75), (z0 + 0.85, g0 - 0.35)):
            for ss in ((a + 0.1, (a + b) / 2 - 0.04), ((a + b) / 2 + 0.04, b - 0.1)):
                w.box(H, "door", 0, ss[0], ss[1], t - 0.07, t - 0.05, zz[0], zz[1])
    else:
        w.rect(H, glass_role(H, w, (a + b) / 2, g0), 2, a + 0.14, b - 0.14, g0, g1, t - 0.06)
        frame_ring(H, w, "door", 0, a + 0.12, b - 0.12, g0 - 0.02, g1 + 0.02, t - 0.075, t - 0.05, 0.03)
        for zz in ((z0 + 0.25, z0 + 0.9), (z0 + 1.0, g0 - 0.12)):
            w.box(H, "door", 0, a + 0.14, b - 0.14, t - 0.075, t - 0.05, zz[0], zz[1])
    hs = b - 0.09
    w.box(H, "metal", 0, hs - 0.015, hs + 0.015, t - 0.13, t - 0.05, z0 + 0.95, z0 + 1.15)
    w.box(H, "metal", 0, hs - 0.12, hs + 0.0, t - 0.15, t - 0.13, z0 + 1.08, z0 + 1.1)
    w.box(H, "metal", 0, (a + b) / 2 - 0.15, (a + b) / 2 + 0.15, t - 0.07, t - 0.05, z0 + 0.78, z0 + 0.84)
    # (a wall lantern beside it, and the house's number)
    if kind != "back":
        ls = s0 - 0.35
        w.box(H, "metal", 0, ls - 0.06, ls + 0.06, 0.0, 0.03, z1 - 0.45, z1 - 0.25)
        w.box(H, "gutter", 0, ls - 0.08, ls + 0.08, 0.03, 0.19, z1 - 0.52, z1 - 0.18)
        w.box(H, "glass_lit", 0, ls - 0.06, ls + 0.06, 0.05, 0.17, z1 - 0.48, z1 - 0.24)
        w.box(H, "gutter", 0, ls - 0.1, ls + 0.1, 0.01, 0.21, z1 - 0.18, z1 - 0.14)
        w.box(H, "metal", 0, s1 + 0.12, s1 + 0.32, 0.0, 0.015, z0 + 1.45, z0 + 1.6)
    # (a step out to the path)
    w.box(H, "step", 1, s0 - 0.15, s1 + 0.15, 0.0, 0.4, FOOT + 0.3, z0 - 0.01)


def garage_door(H, st, w, s0, s1, z0, z1, kind="ribbed"):
    t = -0.12
    frame_ring(H, w, "frame", 1, s0, s1, z0 - 0.05, z1, t - 0.06, t, 0.07)
    a, b = s0 + 0.07, s1 - 0.07
    w.rect(H, "garage", 2, a, b, z0, z1 - 0.07, t - 0.05)
    if kind == "glazed":   # (a sectional door: a row of small lights along the top panel, panels below)
        n = max(3, int(round((b - a) / 0.55)))
        top = z1 - 0.07
        for k in range(n):
            g0 = a + 0.12 + (b - a - 0.24) * k / n + 0.04
            g1 = a + 0.12 + (b - a - 0.24) * (k + 1) / n - 0.04
            w.rect(H, glass_role(H, w, g0, top), 1, g0, g1, top - 0.38, top - 0.12, t - 0.055)
        for k in range(1, 4):
            zz = z0 + (top - z0) * k / 4
            w.box(H, "garage", 0, a, b, t - 0.07, t - 0.05, zz - 0.015, zz + 0.015)
        for k in range(1, n):
            ss = a + (b - a) * k / n
            w.box(H, "garage", 0, ss - 0.012, ss + 0.012, t - 0.07, t - 0.05, z0, top - 0.45)
    else:   # (up-and-over steel: fine horizontal ribs)
        z = z0 + 0.12
        while z < z1 - 0.2:
            w.box(H, "garage", 0, a + 0.05, b - 0.05, t - 0.07, t - 0.05, z, z + 0.035)
            z += 0.155
        w.box(H, "metal", 0, (a + b) / 2 - 0.08, (a + b) / 2 + 0.08, t - 0.09, t - 0.05, z0 + 1.0, z0 + 1.06)


# --- a block -------------------------------------------------------------------------------------------------

class Block:
    """A box of walls under its roof: x0..x1 along the front, y0..y1 deep, its walls' top at eaves (z) above
    base, the roof 'gx' (ridge along x, gables at the ends), 'gy' (ridge along y, gables front and back),
    'hip', 'lean' (one slope down to the front, its back against a taller block), 'none'."""
    def __init__(self, x0, x1, y0, y1, eaves, roof="gx", pitch=38.0, base=FOOT, wall="brick", over=0.3, verge=0.22,
                 floors=None, fh=2.7, gable_skin=None, roof_role="tile", fascia=True, sill0=0.9):
        self.x0, self.x1, self.y0, self.y1 = x0, x1, y0, y1
        self.eaves, self.roof, self.pitch, self.base, self.wall = eaves, roof, pitch, base, wall
        self.over, self.verge, self.fh, self.sill0 = over, verge, fh, sill0
        self.floors = floors if floors is not None else max(1, int(round((eaves - 0.3) / fh)))
        self.t = math.tan(math.radians(pitch))
        self.pat = {}       # side -> [floor 0 pattern, floor 1 ...], 'g' + side -> gable pattern
        self.gable_skin = gable_skin   # (the gables above the eaves: 'render', 'boards', 'tile' or None)
        self.roof_role = roof_role
        self.fascia = fascia
        self.rise = 0.22   # (the roof's top at the wall line above the eaves)

    def side(self, k):
        if k == "f":
            return Wall((self.x0, self.y0), (1, 0), self.x1 - self.x0)
        if k == "b":
            return Wall((self.x1, self.y1), (-1, 0), self.x1 - self.x0)
        if k == "r":
            return Wall((self.x1, self.y0), (0, 1), self.y1 - self.y0)
        return Wall((self.x0, self.y1), (0, -1), self.y1 - self.y0)

    def gables(self):
        return {"gx": "lr", "gy": "fb"}.get(self.roof, "")

    def ridge_z(self):
        d = (self.y1 - self.y0) / 2 if self.roof in ("gx",) else (self.x1 - self.x0) / 2 if self.roof == "gy" else min(self.x1 - self.x0, self.y1 - self.y0) / 2
        if self.roof == "lean":
            d = self.y1 - self.y0
        return self.eaves + self.rise + d * self.t

    def top(self, k):
        """The wall's top along it: [(s, z)]."""
        w = self.side(k)
        L = w.L
        e = self.eaves
        if k in self.gables():
            return [(0, e), (L / 2, e + self.rise * 0.6 + L / 2 * self.t), (L, e)]
        if self.roof == "lean" and k in "lr":
            hi = e + self.rise * 0.6 + L * self.t
            return [(0, e), (L, hi)] if k == "r" else [(0, hi), (L, e)]
        if self.roof == "lean" and k == "b":
            hi = e + self.rise * 0.6 + (self.y1 - self.y0) * self.t
            return [(0, hi), (L, hi)]
        return [(0, e), (L, e)]

    def z_roof(self, x, y):
        """The roof's top over a point (for things set on it)."""
        e = self.eaves + self.rise
        if self.roof == "gx":
            return e + ((self.y1 - self.y0) / 2 - abs(y - (self.y0 + self.y1) / 2)) * self.t
        if self.roof == "gy":
            return e + ((self.x1 - self.x0) / 2 - abs(x - (self.x0 + self.x1) / 2)) * self.t
        if self.roof == "lean":
            return e + (y - self.y0) * self.t
        if self.roof == "hip":
            return e + min(x - self.x0, self.x1 - x, y - self.y0, self.y1 - y) * self.t
        return self.eaves


def inside(blocks, me, x, y, z):
    for b in blocks:
        if b is me:
            continue
        if b.x0 + 0.02 < x < b.x1 - 0.02 and b.y0 + 0.02 < y < b.y1 - 0.02 and z < b.eaves + 0.05:
            return True
    return False


def hidden_ranges(blocks, me, w, ztop):
    """The stretches of a wall another block covers (to its own eaves, at least this wall's height)."""
    out = []
    for b in blocks:
        if b is me or b.eaves + 0.05 < ztop:
            continue
        a0, a1 = None, None
        n = 60
        for i in range(n + 1):
            s = w.L * i / n
            pin, pout = w.at(s, -0.03, 1.0), w.at(s, 0.03, 1.0)
            within = lambda p: b.x0 - 0.01 <= p[0] <= b.x1 + 0.01 and b.y0 - 0.01 <= p[1] <= b.y1 + 0.01
            # (inside the other on both sides of the face: buried; on the inner side only, the two walls share a
            # plane, and the later block's wall is the one drawn there)
            if within(pin) and (within(pout) or blocks.index(b) > blocks.index(me)):
                if a0 is None:
                    a0 = s
                a1 = s
        if a0 is not None and a1 - a0 > 0.05:
            out.append((max(0.0, a0 - w.L / n), min(w.L, a1 + w.L / n), b.eaves))
    return out


# --- patterns ------------------------------------------------------------------------------------------------

TOKEN_W = {"W1": 0.63, "W2": 1.2, "W3": 1.77, "W4": 2.35, "S": 0.5, "D": 1.0, "D2": 1.45, "F": 1.7, "G": 2.55,
           "GG": 4.4, "B": 2.5, "B3": 3.0, "w": 0.75, ".": 0.0, "P": 0.9}


def parse(pattern, L):
    """'W2 D W3' -> [(token, centre s)], spread evenly along L; 'W2@3.2' at its own place."""
    toks = pattern.split()
    free = [t for t in toks if "@" not in t]
    out = []
    i = 0
    for t in toks:
        if "@" in t:
            k, s = t.split("@")
            out.append((k, float(s)))
        else:
            out.append((t, L * (i + 0.5) / len(free)))
            i += 1
    return [(k, s) for k, s in out if k != "."]


class Style:
    def __init__(self, **kw):
        self.window_bars = kw.get("bars", False)
        self.sash = kw.get("sash", False)
        self.heads = kw.get("heads", "soldier")   # soldier, stone, none, keystone
        self.sills = kw.get("sills", "stone")     # stone, brick, none
        self.plinth = kw.get("plinth", True)
        self.door = kw.get("door", "composite")
        self.garage = kw.get("garage", "glazed")
        self.canopy = kw.get("canopy", "gable")   # gable, flat, hood, none
        self.win_h = kw.get("win_h", 1.2)
        self.gutter = kw.get("gutter", "gutter")
        self.string = kw.get("string", False)
        self.quoins = kw.get("quoins", False)
        self.bay_roof = kw.get("bay_roof", "tile")
        self.fascia_role = "trim"


def window_heights(B, st, floor, token):
    """Sill and head of a window on a floor."""
    if token == "w":   # (a small gable or dormer window)
        return None
    sill = B.sill0 + floor * B.fh
    h = st.win_h if token not in ("S",) else min(0.9, st.win_h)
    if token == "S":
        sill += st.win_h - h
    return sill, sill + h


def build_wall(H, blocks, B, st, k, extra_holes=()):
    """One wall of a block: its face with the openings cut, plinth, heads, sills, and what fills the holes."""
    w = B.side(k)
    top = B.top(k)
    zt = min(z for _, z in top)
    hid = hidden_ranges(blocks, B, w, B.eaves)
    rows = B.pat.get(k, [])
    holes, fills = [], []
    for floor, row in enumerate(rows):
        for tok, s in parse(row, w.L):
            wd = TOKEN_W.get(tok, 1.0)
            s0, s1 = s - wd / 2, s + wd / 2
            if tok in ("D", "D2"):
                z0, z1 = 0.15 + floor * B.fh, 2.25 + floor * B.fh
                holes.append((s0, s1, z0, z1))
                fills.append(("door", s0, s1, z0, z1, 0.4 if tok == "D2" else 0.0))
            elif tok == "P":   # (a side or back door: plain, half glazed)
                z0, z1 = 0.15, 2.25
                holes.append((s0, s1, z0, z1))
                fills.append(("back", s0, s1, z0, z1, 0))
            elif tok == "F":
                z0, z1 = 0.15, 2.25
                holes.append((s0, s1, z0, z1))
                fills.append(("french", s0, s1, z0, z1, 0))
            elif tok in ("G", "GG"):
                z0, z1 = 0.0, 2.2
                holes.append((s0, s1, z0, z1))
                fills.append(("garage", s0, s1, z0, z1, 0))
            elif tok in ("B", "B3"):
                fills.append(("bay", s0, s1, 0, 0, floor))
            else:
                z0, z1 = window_heights(B, st, floor, tok)
                holes.append((s0, s1, z0, z1))
                n = {"W1": 1, "W2": 2, "W3": 3, "W4": 4, "S": 1}.get(tok, 2)
                fills.append(("win", s0, s1, z0, z1, n))
    # (the gable's own windows, over the eaves)
    for tok, s in parse(B.pat.get("g" + k, ""), w.L):
        wd = TOKEN_W.get(tok, 0.7)
        z0 = B.eaves + 0.35
        z1 = z0 + (0.6 if tok == "w" else 1.1)
        holes.append((s - wd / 2, s + wd / 2, z0, z1))
        fills.append(("win", s - wd / 2, s + wd / 2, z0, z1, 1 if tok == "w" else 2))
    holes += list(extra_holes)
    # The face, less what another block hides.
    skin = B.wall
    if hid:
        cut = [(a, b, FOOT - 1, zc) for a, b, zc in hid]
        holes_all = holes + cut
    else:
        holes_all = holes
    gable_hi = k in B.gables() or (B.roof == "lean" and k in "lrb")
    if B.gable_skin and gable_hi:
        lowtop = [(s, min(z, B.eaves)) for s, z in top]
        lowtop = [(0, B.eaves), (w.L, B.eaves)]
        wall_face(H, w, skin, 2, lowtop, holes_all, z_low=B.base)
        gh = [h for h in holes_all if h[3] > B.eaves]
        wall_face(H, w, B.gable_skin, 2, top, gh, z_low=B.eaves)
        # (a board where the skin changes)
        w.box(H, "trim", 1, 0.0, w.L, 0.0, 0.04, B.eaves - 0.02, B.eaves + 0.08)
    else:
        wall_face(H, w, skin, 2, top, holes_all, z_low=B.base)
    surf = skin
    # Plinth: dark engineering brick, a course proud, to 0.45 m.
    if st.plinth and B.base <= FOOT + 0.01 and skin in ("brick", "brick2", "render", "boards"):
        pr = "brick" if skin == "boards" else "plinth"
        ph = [h for h in holes if h[2] < 0.5] + ([(a, b, FOOT - 1, 99) for a, b, _ in hid])
        wall_face(H, w, pr, 1, [(0, 0.42), (w.L, 0.42)], ph, t=0.012, z_low=B.base)
        for a, b, _, _ in [h for h in ph if h[2] < 0.5]:
            for s in (a, b):
                if 0 < s < w.L:
                    H.quad(pr, 1, w.at(s, 0, B.base), w.at(s, 0.012, B.base), w.at(s, 0.012, 0.42), w.at(s, 0, 0.42), w.u * (1 if s == b else -1))
        segs = [(0.0, w.L)]
        for a, b, _, _ in ph:
            segs = [p for s0, s1 in segs for p in ((s0, min(s1, a)), (max(s0, b), s1)) if p[1] - p[0] > 0.02]
        for a, b in segs:   # (the plinth's weathered top)
            w.box(H, pr, 1, a, b, 0, 0.03, 0.42, 0.45)
    # Air bricks just over the plinth, every couple of metres where nothing is.
    if st.plinth and B.base <= FOOT + 0.01 and skin in ("brick", "brick2", "render") and w.L > 1.5:
        n_ab = max(1, int(w.L / 2.2))
        for i in range(n_ab):
            sa = w.L * (i + 0.5) / n_ab
            if any(h[0] - 0.3 < sa < h[1] + 0.3 for h in holes) or any(a <= sa <= b for a, b, _ in hid):
                continue
            w.box(H, "plinth", 0, sa - 0.11, sa + 0.11, 0.012, 0.02, 0.15, 0.22)
    # String course between the floors.
    if st.string and B.floors >= 2:
        for f in range(1, B.floors):
            zs = f * B.fh + 0.05
            segs = [(0.0, w.L)]
            for a, b, _ in hid:
                segs = [p for s0, s1 in segs for p in ((s0, min(s1, a)), (max(s0, b), s1)) if p[1] - p[0] > 0.05]
            for a, b in segs:
                w.box(H, "brick2" if st.string == "brick" else "stone", 1, a, b, 0, 0.035, zs, zs + 0.15)
    # Quoins: alternate long and short blocks up each corner.
    if st.quoins:
        for s, d in ((0.0, 1), (w.L, -1)):
            if any(a - 0.05 <= s <= b + 0.05 for a, b, _ in hid):
                continue
            z, i = 0.45, 0
            while z + 0.28 < zt:
                ln = 0.42 if i % 2 == 0 else 0.22
                s0, s1 = (s, s + ln) if d > 0 else (s - ln, s)
                w.box(H, "stone" if B.wall != "render" else "render2", 0, s0, s1, 0, 0.018, z, z + 0.26)
                z += 0.3
                i += 1
    # What fills each hole.
    for kind, s0, s1, z0, z1, n in fills:
        if kind == "bay":
            bay(H, B, st, w, s0, s1, n, blocks)
            continue
        reveal(H, w, surf if surf != "boards" else "trim", 1, s0, s1, z0, z1, REC + 0.05, sill=kind not in ("door", "back", "french", "garage"))
        if kind == "win":
            window(H, st, w, s0, s1, z0, z1, n, bars=st.window_bars, sash=st.sash and z1 - z0 > 1.0)
        elif kind == "door":
            door(H, st, w, s0, s1, z0, z1, st.door, side=n)
        elif kind == "back":
            door(H, st, w, s0, s1, z0, z1, "composite")
        elif kind == "french":
            t = -REC
            frame_ring(H, w, "frame", 1, s0, s1, z0, z1, t - 0.07, t)
            m = (s0 + s1) / 2
            w.box(H, "frame", 1, m - 0.04, m + 0.04, t - 0.07, t, z0, z1)
            w.rect(H, glass_role(H, w, m, z0 + 1), 2, s0, s1, z0, z1, t - 0.03)
            for a, b in ((s0 + FR, m - 0.04), (m + 0.04, s1 - FR)):
                w.box(H, "frame", 1, a, b, t - 0.07, t, z0 + 0.4, z0 + 0.47)
            w.box(H, "step", 1, s0 - 0.1, s1 + 0.1, 0.0, 0.35, FOOT + 0.3, z0 - 0.01)
        elif kind == "garage":
            garage_door(H, st, w, s0, s1, z0, z1, st.garage)
        # Heads and sills (none in boarding: a white board round the opening there).
        if surf == "boards":
            frame_ring(H, w, "trim", 1, s0 - 0.09, s1 + 0.09, max(z0 - 0.09, 0.0), z1 + 0.09, 0.0, 0.025, 0.09)
            continue
        if kind != "garage" or st.heads == "soldier":
            if st.heads == "soldier" and surf in ("brick", "brick2"):
                w.box(H, "soldier", 1, s0 - 0.04, s1 + 0.04, 0, 0.012, z1, z1 + 0.215)
            elif st.heads in ("stone", "keystone"):
                w.box(H, "stone", 1, s0 - 0.12, s1 + 0.12, 0, 0.03, z1, z1 + 0.22)
                if st.heads == "keystone" and kind == "win":
                    m = (s0 + s1) / 2
                    w.box(H, "stone", 0, m - 0.09, m + 0.09, 0.03, 0.05, z1 - 0.04, z1 + 0.26)
        if kind == "win" and st.sills != "none":
            if st.sills == "brick" and surf in ("brick", "brick2"):
                w.box(H, "soldier", 1, s0 - 0.05, s1 + 0.05, -0.03, 0.06, z0 - 0.075, z0)
            else:
                w.box(H, "stone" if surf != "render" else "render2", 1, s0 - 0.07, s1 + 0.07, -0.05, 0.06, z0 - 0.07, z0)
    return w


def bay(H, B, st, w, s0, s1, floor, blocks):
    """A canted bay on the ground floor (or both): brick to the sill, three windows, a tiled or lead roof."""
    p = 0.62 if s1 - s0 < 2.8 else 0.72   # (how far it stands out)
    front = (s1 - s0) - 2 * p
    a, b = s0 + p, s1 - p
    two = floor == 1   # (pattern on floor 1: runs up both floors)
    sill = B.sill0 - 0.25
    top = min(B.fh + B.sill0 + st.win_h + 0.25, B.eaves - 0.62) if two else (B.sill0 + st.win_h + 0.22)
    o = w.at(0, 0, 0)
    pts = [w.at(s0, 0, 0), w.at(a, p, 0), w.at(b, p, 0), w.at(s1, 0, 0)]
    faces = []
    for i in range(3):
        u = pts[i + 1] - pts[i]
        L = np.linalg.norm(u)
        faces.append(Wall(pts[i][:2], (u / L)[:2], L))
    for i, f in enumerate(faces):
        z0, z1 = sill, sill + st.win_h + 0.25 if not two else sill + st.win_h + 0.25
        holes = [(0.08, f.L - 0.08, z0, z1)]
        if two:
            holes.append((0.08, f.L - 0.08, B.fh + B.sill0, B.fh + B.sill0 + st.win_h))
        wall_face(H, f, B.wall, 2, [(0, top), (f.L, top)], holes, z_low=FOOT)
        if st.plinth:
            wall_face(H, f, "plinth", 1, [(0, 0.42), (f.L, 0.42)], [], t=0.012)
        for h in holes:
            reveal(H, f, B.wall, 1, h[0], h[1], h[2], h[3], REC + 0.05)
            window(H, st, f, h[0], h[1], h[2], h[3], 2 if i == 1 and f.L > 1.3 else 1, bars=st.window_bars, sash=st.sash)
            f.box(H, "stone" if B.wall != "render" else "render2", 1, h[0] - 0.08, h[1] + 0.08, -0.05, 0.06, h[2] - 0.07, h[2])
        # (mitred corner posts so the faces meet)
    # Roof: from the bay's eaves up to the wall.
    ov = 0.14
    ze = top + 0.05
    zr = ze + 0.55
    q = [w.at(s0 - ov * 0.4, 0, ze), w.at(a - ov * 0.4, p + ov, ze), w.at(b + ov * 0.4, p + ov, ze), w.at(s1 + ov * 0.4, 0, ze)]
    r0, r1 = w.at(a - 0.05, 0, zr), w.at(b + 0.05, 0, zr)
    role = "lead" if st.bay_roof == "lead" else "tile"
    H.quad(role, 2, q[1], q[2], r1, r0, (0, 0, 1) - w.n * 0 + w.n)
    H.tri(role, 2, [q[0], q[1], r0], np.array([0, 0, 1.0]) + w.n)
    H.tri(role, 2, [q[2], q[3], r1], np.array([0, 0, 1.0]) + w.n)
    H.tri(role, 2, [q[0], r0, q[1]], None)   # (both faces of the small hip triangles kept: the winding above decides)
    # fascia round it, white, and a soffit under
    for i in range(3):
        A_, B_ = q[i], q[i + 1]
        u = (B_ - A_); L = np.linalg.norm(u); u /= L
        n = np.array([u[1], -u[0], 0.0])
        if np.dot(n, w.n) < -0.01:
            n = -n
        fw = Wall(A_[:2], u[:2], L)
        if np.dot(fw.n, n) < 0:
            fw = Wall(B_[:2], (-u)[:2], L)
        fw.box(H, "trim", 1, 0, L, -0.03, 0.0, ze - 0.2, ze + 0.01)
        H.quad("trim", 1, A_ - np.array([0, 0, 0.2]), B_ - np.array([0, 0, 0.2]), pts[i + 1] + np.array([0, 0, top]), pts[i] + np.array([0, 0, top]), (0, 0, -1))
    # flashing where it meets the wall
    w.box(H, "lead", 1, a - 0.1, b + 0.1, 0.0, 0.03, zr - 0.05, zr + 0.1)


# --- roofs ---------------------------------------------------------------------------------------------------

def roof(H, blocks, B, st, code):
    if B.roof == "none":
        return
    role = B.roof_role
    t, o, g = B.t, B.over, B.verge
    e = B.eaves + B.rise
    ze = e - o * t
    up = np.array([0, 0, 1.0])
    x0, x1, y0, y1 = B.x0, B.x1, B.y0, B.y1
    fz = B.eaves - 0.12   # (the soffit's level)
    edges = []   # eaves: (a, b, outward n) for fascia, soffit, gutter

    def plane(pts, out):
        H.tri(role, 2, pts, out)

    if B.roof == "gx":
        d = (y1 - y0) / 2
        ym = (y0 + y1) / 2
        zr = e + d * t
        plane([(x0 - g, y0 - o, ze), (x1 + g, y0 - o, ze), (x1 + g, ym, zr), (x0 - g, ym, zr)], (0, -1, 2))
        plane([(x0 - g, y1 + o, ze), (x1 + g, y1 + o, ze), (x1 + g, ym, zr), (x0 - g, ym, zr)], (0, 1, 2))
        edges += [((x0 - g, y0 - o), (x1 + g, y0 - o), (0, -1), y0), ((x1 + g, y1 + o), (x0 - g, y1 + o), (0, 1), y1)]
        ridges = [((x0 - g, ym, zr), (x1 + g, ym, zr))]
        for x, sgn in ((x0 - g, -1), (x1 + g, 1)):
            verge(H, B, (x, y0 - o, ze), (x, ym, zr), (sgn, 0, 0), x0 if sgn < 0 else x1)
            verge(H, B, (x, y1 + o, ze), (x, ym, zr), (sgn, 0, 0), x0 if sgn < 0 else x1)
    elif B.roof == "gy":
        d = (x1 - x0) / 2
        xm = (x0 + x1) / 2
        zr = e + d * t
        plane([(x0 - o, y0 - g, ze), (x0 - o, y1 + g, ze), (xm, y1 + g, zr), (xm, y0 - g, zr)], (-1, 0, 2))
        plane([(x1 + o, y0 - g, ze), (x1 + o, y1 + g, ze), (xm, y1 + g, zr), (xm, y0 - g, zr)], (1, 0, 2))
        edges += [((x0 - o, y1 + g), (x0 - o, y0 - g), (-1, 0), x0), ((x1 + o, y0 - g), (x1 + o, y1 + g), (1, 0), x1)]
        ridges = [((xm, y0 - g, zr), (xm, y1 + g, zr))]
        for y, sgn in ((y0 - g, -1), (y1 + g, 1)):
            verge(H, B, (x0 - o, y, ze), (xm, y, zr), (0, sgn, 0), y0 if sgn < 0 else y1)
            verge(H, B, (x1 + o, y, ze), (xm, y, zr), (0, sgn, 0), y0 if sgn < 0 else y1)
    elif B.roof == "lean":
        zr = e + (y1 - y0) * t
        plane([(x0 - g, y0 - o, ze), (x1 + g, y0 - o, ze), (x1 + g, y1, zr), (x0 - g, y1, zr)], (0, -1, 2))
        edges += [((x0 - g, y0 - o), (x1 + g, y0 - o), (0, -1), y0)]
        ridges = []
        for x, sgn in ((x0 - g, -1), (x1 + g, 1)):
            verge(H, B, (x, y0 - o, ze), (x, y1, zr), (sgn, 0, 0), x0 if sgn < 0 else x1)
        # (lead flashing where it meets the wall above)
        H.box("lead", 1, (x0 - g, y1 - 0.25, zr - 0.02), (x1 + g, y1 + 0.02, zr + 0.06))
    else:   # hip
        X0, X1, Y0, Y1 = x0 - o, x1 + o, y0 - o, y1 + o
        if X1 - X0 >= Y1 - Y0:
            D = (Y1 - Y0) / 2
            ym = (Y0 + Y1) / 2
            zr = ze + D * t
            ra, rb = X0 + D, X1 - D
            plane([(X0, Y0, ze), (X1, Y0, ze), (rb, ym, zr), (ra, ym, zr)], (0, -1, 2))
            plane([(X0, Y1, ze), (X1, Y1, ze), (rb, ym, zr), (ra, ym, zr)], (0, 1, 2))
            plane([(X0, Y0, ze), (X0, Y1, ze), (ra, ym, zr)], (-1, 0, 2))
            plane([(X1, Y0, ze), (X1, Y1, ze), (rb, ym, zr)], (1, 0, 2))
            ridges = [((ra, ym, zr), (rb, ym, zr))] if rb - ra > 0.05 else []
            hips = [((X0, Y0, ze), (ra, ym, zr)), ((X0, Y1, ze), (ra, ym, zr)), ((X1, Y0, ze), (rb, ym, zr)), ((X1, Y1, ze), (rb, ym, zr))]
        else:
            D = (X1 - X0) / 2
            xm = (X0 + X1) / 2
            zr = ze + D * t
            ra, rb = Y0 + D, Y1 - D
            plane([(X0, Y0, ze), (X0, Y1, ze), (xm, rb, zr), (xm, ra, zr)], (-1, 0, 2))
            plane([(X1, Y0, ze), (X1, Y1, ze), (xm, rb, zr), (xm, ra, zr)], (1, 0, 2))
            plane([(X0, Y0, ze), (X1, Y0, ze), (xm, ra, zr)], (0, -1, 2))
            plane([(X0, Y1, ze), (X1, Y1, ze), (xm, rb, zr)], (0, 1, 2))
            ridges = [((xm, ra, zr), (xm, rb, zr))] if rb - ra > 0.05 else []
            hips = [((X0, Y0, ze), (xm, ra, zr)), ((X1, Y0, ze), (xm, ra, zr)), ((X0, Y1, ze), (xm, rb, zr)), ((X1, Y1, ze), (xm, rb, zr))]
        edges += [((X0, Y0), (X1, Y0), (0, -1), y0), ((X1, Y1), (X0, Y1), (0, 1), y1), ((X1, Y0), (X1, Y1), (1, 0), x1), ((X0, Y1), (X0, Y0), (-1, 0), x0)]
        for a, b in hips:
            H.sweep("ridge", 1, [a, b], [(-0.13, 0.0), (-0.09, 0.08), (0.0, 0.11), (0.09, 0.08), (0.13, 0.0)], smooth=True)
    for a, b in ridges:
        H.sweep("ridge", 1, [a, b], [(-0.15, -0.02), (-0.11, 0.09), (0.0, 0.13), (0.11, 0.09), (0.15, -0.02)], smooth=True, cap=True)
    # Eaves: fascia, soffit, gutter.
    for a, b, n, wall_at in edges:
        a = np.array([a[0], a[1], 0.0]); b = np.array([b[0], b[1], 0.0]); n = np.array([n[0], n[1], 0.0])
        u = b - a; L = np.linalg.norm(u); u /= L
        # (the hip's corners: the fascia runs to the corner; the soffit back to the wall line)
        wa = a - n * o if B.roof == "hip" else a - n * o
        top_z, bot_z = ze + 0.03, min(fz, ze - 0.16)
        fw = Wall(a[:2], u[:2], L)
        if np.dot(fw.n, n) < 0:
            fw = Wall(b[:2], (-u)[:2], L)
        if B.fascia:
            fw.box(H, "trim", 1, 0, L, -0.025, 0.0, bot_z, top_z)
            # soffit back to the wall
            in_a, in_b = fw.at(0, -0.025, bot_z), fw.at(L, -0.025, bot_z)
            H.quad("trim", 1, in_a, in_b, in_b - n * (o - 0.025), in_a - n * (o - 0.025), (0, 0, -1))
        if st.gutter:
            gz = bot_z + 0.06
            path = [fw.at(-0.02, 0.06, gz), fw.at(L + 0.02, 0.06, gz)]
            prof = [(-0.065, 0.06), (-0.06, 0.0), (-0.035, -0.045), (0.0, -0.06), (0.035, -0.045), (0.06, 0.0), (0.065, 0.06)]
            H.sweep("gutter", 1, path, prof, smooth=True)
    return edges


def verge(H, B, a, b, out, wall_x):
    """A gable's verge: the bargeboard under the roof's edge, and the soffit back to the wall."""
    a = np.array(a, float); b = np.array(b, float); out = np.array(out, float)
    u = b - a
    L = np.linalg.norm(u[:2])
    if L < 1e-6:
        return
    w = Wall(a[:2], (u[:2] / L), L)
    if np.dot(w.n, out) < 0:
        # (flip so the wall faces out of the gable)
        w = Wall(b[:2], (-u[:2] / L), L)
        a, b = b, a
    if B.fascia:
        w.slab(H, "trim", 1, 0, L, -0.03, 0.0, a[2] - 0.2, b[2] - 0.2, a[2] + 0.03, b[2] + 0.03)
        # soffit under the overhang, from the board back to the gable wall
        back = abs(np.dot(a[:2] - np.array([wall_x, wall_x]), out[:2])) if out[0] != 0 else abs(a[1] - wall_x)
        back = abs((a[0] - wall_x) if out[0] != 0 else (a[1] - wall_x))
        p0, p1 = w.at(0, -0.03, a[2] - 0.2), w.at(L, -0.03, b[2] - 0.2)
        H.quad("trim", 1, p0, p1, p1 - out * (back - 0.03), p0 - out * (back - 0.03), (0, 0, -1))


def downpipes(H, B, edges, spots):
    """Downpipes from the gutter at the given eaves' ends: 'a' its start, 'b' its end."""
    ze = B.eaves + B.rise - B.over * B.t
    bot_z = min(B.eaves - 0.12, ze - 0.16)
    for i, end in spots:
        if i >= len(edges):
            continue
        a, b, n, wall_at = edges[i]
        a = np.array([a[0], a[1], 0.0]); b = np.array([b[0], b[1], 0.0]); n = np.array([n[0], n[1], 0.0])
        u = (b - a) / np.linalg.norm(b - a)
        p = (a + u * (B.verge + 0.25)) if end == "a" else (b - u * (B.verge + 0.25))
        gutter = p + n * 0.06
        wallp = p - n * (B.over - 0.05)
        zg = bot_z + 0.02
        H.sweep("gutter", 1, [gutter + np.array([0, 0, zg]), wallp + np.array([0, 0, zg - 0.25]), wallp + np.array([0, 0, zg - 0.45])],
                [(0.034 * math.cos(k * math.pi / 3), 0.034 * math.sin(k * math.pi / 3)) for k in range(6)], smooth=True)
        H.sweep("gutter", 1, [wallp + np.array([0, 0, zg - 0.45]), wallp + np.array([0, 0, 0.05])],
                [(0.034 * math.cos(k * math.pi / 3), 0.034 * math.sin(k * math.pi / 3)) for k in range(6)], smooth=True)
        H.sweep("gutter", 1, [wallp + np.array([0, 0, 0.05]), wallp + np.array([0, 0, 0.0]) + n * 0.12],
                [(0.034 * math.cos(k * math.pi / 3), 0.034 * math.sin(k * math.pi / 3)) for k in range(6)], smooth=True)
        z = 0.9
        while z < zg - 0.6:
            H.box("gutter", 0, tuple(wallp - np.array([0.05, 0.05, 0]) + np.array([0, 0, z])), tuple(wallp + np.array([0.05, 0.05, 0]) + np.array([0, 0, z + 0.04])))
            z += 1.8


# --- stacks, dormers, canopies, solar ---------------------------------------------------------------------

def stack(H, B, side, s, kind="gable", pots=2, top_extra=0.9, wide=1.05):
    """A chimney stack: 'gable' up the outside of a gable end (stepping in at the eaves), 'ridge' on the ridge."""
    zr = B.ridge_z()
    top = zr + top_extra
    role = "brick" if B.wall in ("brick", "boards") else B.wall
    if kind == "gable":
        w = B.side(side)
        # (the breast: wide to the first floor, a weathered step, then the flue)
        H_ = B.eaves - 0.4
        w.box(H, role, 2, s - wide / 2 - 0.12, s + wide / 2 + 0.12, 0.0, 0.5, FOOT, H_)
        w.box(H, "tile", 1, s - wide / 2 - 0.12, s + wide / 2 + 0.12, 0.38, 0.52, H_, H_ + 0.05)
        sl = 0.36
        H.quad("tile", 1, w.at(s - wide / 2 - 0.12, 0.5, H_), w.at(s + wide / 2 + 0.12, 0.5, H_), w.at(s + wide / 2, 0.42, H_ + sl), w.at(s - wide / 2, 0.42, H_ + sl), w.n + np.array([0, 0, 1.0]))
        w.box(H, role, 2, s - wide / 2, s + wide / 2, 0.0, 0.42, H_, top)
        c = w.at(s, 0.21, 0)
        hx, hy = w.u * wide / 2, w.n * 0.21
    else:
        w = B.side("f")
        c = np.array([B.x0 + s, (B.y0 + B.y1) / 2, 0.0]) if B.roof != "gy" else np.array([(B.x0 + B.x1) / 2, B.y0 + s, 0.0])
        hx, hy = np.array([wide / 2, 0, 0]), np.array([0, 0.3, 0])
        zb = B.eaves
        H.obox(role, 2, c + np.array([0, 0, (zb + top) / 2]), hx, hy, np.array([0, 0, (top - zb) / 2]))
    # (two corbelled courses near the top, the flaunching, the pots)
    for zc in (top - 0.42, top - 0.2):
        H.obox(role, 1, c + np.array([0, 0, zc + 0.04]), hx * (1 + 0.09 / max(np.linalg.norm(hx), 0.1)), hy * (1 + 0.09 / max(np.linalg.norm(hy), 0.1)), np.array([0, 0, 0.04]))
    H.obox("flaunch", 1, c + np.array([0, 0, top + 0.05]), hx * 0.92, hy * 0.85, np.array([0, 0, 0.05]))
    for k in range(pots):
        off = (k - (pots - 1) / 2) * 0.32
        ax = hx / max(np.linalg.norm(hx), 1e-6)
        p = c + ax * off + np.array([0, 0, top + 0.08])
        H.lathe("pot", 1, p, [(0.11, 0.0), (0.095, 0.25), (0.085, 0.4), (0.105, 0.45), (0.105, 0.5)], seg=8)


def dormer(H, B, st, s, width=1.6, setback=0.0, tok="W2", cheek="tile", through=False):
    """A gabled dormer on the front slope of a 'gx' block at s (from its left end). Through the eaves when
    'through' (the wall carried up into it, the estate's way), else set back on the slope."""
    x = B.x0 + s
    yf = B.y0 + setback
    zb = B.z_roof(x, yf) - 0.05 if not through else B.eaves - 0.6
    he = (B.eaves + 1.35) if through else zb + 1.35
    d = Block(x - width / 2, x + width / 2, yf, yf + 2.0, he, roof="gy", pitch=max(B.pitch, 40), base=zb - 0.4 if not through else B.eaves - 1.0,
              wall=B.wall if (through or cheek is None) else cheek, over=0.12, verge=0.18, fh=B.fh)
    # depth so its ridge runs into the main slope
    zr = d.ridge_z()
    yback = B.y0 + (zr - (B.eaves + B.rise)) / B.t + 0.3
    d.y1 = max(yf + 0.8, yback)
    d.pat = {}
    sub_st = st
    w = d.side("f")
    win_w = TOKEN_W.get(tok, 1.2)
    z0 = (B.eaves + 0.14) if through else zb + 0.3   # (through: clear of the main gutter)
    z1 = he - 0.1
    holes = [(w.L / 2 - win_w / 2, w.L / 2 + win_w / 2, z0, z1)]
    front_skin = B.wall if through else (cheek if cheek in ("render", "boards", "tile") else B.wall)
    wall_face(H, w, front_skin, 2, d.top("f"), holes, z_low=B.eaves if through else d.base)
    reveal(H, w, front_skin if front_skin != "boards" else "trim", 1, holes[0][0], holes[0][1], z0, z1, REC + 0.05)
    window(H, st, w, holes[0][0], holes[0][1], z0, z1, {"W1": 1, "W2": 2, "W3": 3}.get(tok, 2), bars=st.window_bars)
    for k in "lr":
        wall_face(H, d.side(k), cheek if not through else B.wall, 2, d.top(k), [], z_low=d.base)
    roof(H, [], d, st, None)
    return d


def canopy(H, B, st, w, s0, s1, kind, z=2.45, depth=1.0, posts=False):
    """A porch canopy over a door (or run on over a garage): 'gable' a small tiled gable on brackets or posts,
    'lean' a tiled lean-to across, 'flat' a lead-topped flat hood on brackets, 'hood' a Victorian pitched hood."""
    if kind == "none":
        return
    L = s1 - s0
    if kind in ("gable", "hood"):
        ov = 0.12
        rise = (L / 2) * math.tan(math.radians(40 if kind == "gable" else 45))
        ze, zr = z, z + rise
        m = (s0 + s1) / 2
        for a, b, f in ((s0 - ov, m, -1), (s1 + ov, m, 1)):
            pa0, pa1 = w.at(a, depth + 0.05, ze - 0.03), w.at(a, 0.0, ze - 0.03)
            pr0, pr1 = w.at(m, depth + 0.05, zr), w.at(m, 0.0, zr)
            H.quad("tile", 2, pa0, pa1, pr1, pr0, w.u * f + np.array([0, 0, 1.0]))
        H.sweep("ridge", 1, [w.at(m, depth + 0.05, zr), w.at(m, 0.0, zr)], [(-0.11, -0.02), (0.0, 0.1), (0.11, -0.02)], cap=True)
        # bargeboards on the front, the gable filled with boarding
        for a, f in ((s0 - ov, -1), (s1 + ov, 1)):
            pass
        H.tri("trim", 1, [w.at(s0, depth, ze), w.at(s1, depth, ze), w.at(m, depth, zr - 0.08)], w.n)
        for a, b in ((s0 - ov, m), (s1 + ov, m)):
            p0, p1 = w.at(a, depth + 0.06, ze - 0.05), w.at(b, depth + 0.06, zr + 0.02)
            H.sweep("trim", 1, [p0, p1], [(-0.02, -0.16), (0.02, -0.16), (0.02, 0.02), (-0.02, 0.02)], smooth=False)
        # soffit
        H.quad("trim", 1, w.at(s0 - ov, depth, ze - 0.04), w.at(s1 + ov, depth, ze - 0.04), w.at(s1 + ov, 0, ze - 0.04), w.at(s0 - ov, 0, ze - 0.04), (0, 0, -1))
        w.box(H, "lead", 1, s0 - ov - 0.05, s1 + ov + 0.05, 0.0, 0.03, ze + 0.05, zr + 0.12)
    elif kind == "lean":
        ze, zr = z, z + depth * 0.55
        H.quad("tile", 2, w.at(s0 - 0.1, depth + 0.1, ze), w.at(s1 + 0.1, depth + 0.1, ze), w.at(s1 + 0.1, 0.0, zr), w.at(s0 - 0.1, 0.0, zr), w.n + np.array([0, 0, 1.0]))
        w.box(H, "trim", 1, s0 - 0.1, s1 + 0.1, depth + 0.07, depth + 0.1, ze - 0.2, ze + 0.02)
        for s, f in ((s0 - 0.1, -1), (s1 + 0.1, 1)):
            H.tri("trim", 1, [w.at(s, depth + 0.1, ze - 0.2), w.at(s, depth + 0.1, ze), w.at(s, 0, zr)], w.u * f)
            H.tri("trim", 1, [w.at(s, depth + 0.1, ze - 0.2), w.at(s, 0, zr), w.at(s, 0, zr - 0.2)], w.u * f)
        H.quad("trim", 1, w.at(s0 - 0.1, depth + 0.07, ze - 0.2), w.at(s1 + 0.1, depth + 0.07, ze - 0.2), w.at(s1 + 0.1, 0, zr - 0.2), w.at(s0 - 0.1, 0, zr - 0.2), (0, 0, -1))
        w.box(H, "lead", 1, s0 - 0.15, s1 + 0.15, 0.0, 0.03, zr - 0.05, zr + 0.12)
        H.sweep("gutter", 1, [w.at(s0 - 0.12, depth + 0.16, ze - 0.12), w.at(s1 + 0.12, depth + 0.16, ze - 0.12)],
                [(-0.05, 0.05), (-0.045, 0.0), (0.0, -0.045), (0.045, 0.0), (0.05, 0.05)])
    elif kind == "flat":
        w.box(H, "trim", 1, s0 - 0.15, s1 + 0.15, 0.0, depth * 0.7, z, z + 0.16)
        w.box(H, "lead", 1, s0 - 0.13, s1 + 0.13, 0.0, depth * 0.7 - 0.02, z + 0.16, z + 0.19)
    # posts or brackets
    if posts:
        for s in (s0 - 0.02, s1 + 0.02):
            w.box(H, "post", 1, s - 0.05, s + 0.05, depth - 0.05, depth + 0.05, 0.0, z)
            w.box(H, "post", 0, s - 0.07, s + 0.07, depth - 0.07, depth + 0.07, 0.0, 0.12)
            w.box(H, "step", 1, s - 0.22, s + 0.22, depth - 0.22, depth + 0.22, FOOT, 0.02)
    elif kind in ("gable", "hood", "flat"):
        for s in (s0 + 0.05, s1 - 0.05):
            w.box(H, "trim", 0, s - 0.04, s + 0.04, 0.0, 0.08, z - 0.55, z)
            p0, p1 = w.at(s, 0.06, z - 0.5), w.at(s, min(depth, 0.7), z - 0.04)
            H.sweep("trim", 0, [p0, p1], [(-0.03, -0.03), (0.03, -0.03), (0.03, 0.03), (-0.03, 0.03)], smooth=False)
    if kind in ("lean",) and posts:
        w.box(H, "step", 1, s0 - 0.1, s1 + 0.1, 0.0, depth + 0.1, FOOT + 0.3, 0.12)


def solar(H, B, rows, cols, slope="b", s=None):
    """Panels on a slope of a 'gx' or 'hip' block: rows up it, cols along it."""
    if B.roof not in ("gx", "hip"):
        return
    pw, ph = 1.04, 1.72
    n = (0, -1) if slope == "f" else (0, 1)
    yE = B.y0 if slope == "f" else B.y1
    sgn = -1 if slope == "f" else 1
    up_dir = np.array([0, -sgn, B.t]); up_dir /= np.linalg.norm(up_dir)
    nrm = np.array([0, sgn, 1.0 / max(B.t, 0.1)]); nrm /= np.linalg.norm(nrm)
    nrm = np.array([0, sgn * B.t, 1.0]); nrm /= np.linalg.norm(nrm)
    xm = (B.x0 + B.x1) / 2 if s is None else B.x0 + s
    total_w = cols * (pw + 0.02)
    y_start = yE - sgn * 0.3 * 0 + (-sgn) * 0.9   # (0.9 m up from the wall line)
    base = np.array([xm - total_w / 2, y_start, B.z_roof(xm, y_start)]) + nrm * 0.1
    along = np.array([1.0, 0, 0])
    for r in range(rows):
        for c in range(cols):
            o = base + along * c * (pw + 0.02) + up_dir * r * (ph + 0.02)
            ctr = o + along * pw / 2 + up_dir * ph / 2
            H.obox("solar", 1, ctr, along * pw / 2, up_dir * ph / 2, nrm * 0.02)
            for e, ax, ay in ((ctr - up_dir * (ph / 2 - 0.015), along * pw / 2, up_dir * 0.015), (ctr + up_dir * (ph / 2 - 0.015), along * pw / 2, up_dir * 0.015),
                              (ctr - along * (pw / 2 - 0.015), along * 0.015, up_dir * ph / 2), (ctr + along * (pw / 2 - 0.015), along * 0.015, up_dir * ph / 2)):
                H.obox("solar_frame", 0, e + nrm * 0.005, ax, ay, nrm * 0.022)


# --- assembling a design ------------------------------------------------------------------------------------

def assemble(spec):
    H = House(spec["name"])
    st = Style(**spec.get("style", {}))
    blocks = spec["blocks"]
    for B in blocks:
        for k in "fblr":
            if k in B.pat and B.pat[k] is None:
                continue
            w = B.side(k)
            # (a wall wholly inside another block isn't built)
            hid = hidden_ranges(blocks, B, w, B.eaves)
            if hid and sum(b - a for a, b, _ in hid) > w.L - 0.1 and all(zc >= max(z for _, z in B.top(k)) for _, _, zc in hid):
                continue
            build_wall(H, blocks, B, st, k)
        roof(H, blocks, B, st, None)
    services(H, blocks, st, spec)
    for (bi, i, end) in spec.get("pipes", []):
        B = blocks[bi]
        edges = roof_edges(B)
        downpipes(H, B, edges, [(i, end)])
    for item in spec.get("extras", []):
        item(H, blocks, st)
    return H


def services(H, blocks, st, spec):
    """What every house has: a boiler flue and a meter box on a side wall, a soil pipe through the back slope."""
    m = blocks[0]
    side = spec.get("service_side", "r")
    w = m.side(side)
    hid = hidden_ranges(blocks, m, w, m.eaves)
    free = [s for s in (w.L * 0.25, w.L * 0.5, w.L * 0.75) if not any(a - 0.6 <= s <= b + 0.6 for a, b, _ in hid)]
    if free:
        s = free[0]
        # (a meter box at waist height near the front, a flue higher up)
        w.box(H, "frame", 1, s - 0.3, s + 0.3, 0.0, 0.2, 0.55, 1.2)
        w.box(H, "frame", 0, s - 0.27, s + 0.27, 0.2, 0.21, 0.58, 1.17)
        fs = free[-1] if len(free) > 1 else s + 1.0
        H.sweep("metal", 0, [w.at(fs, 0.0, 1.9), w.at(fs, 0.18, 1.9)], [(0.05 * math.cos(k * math.pi / 4), 0.05 * math.sin(k * math.pi / 4)) for k in range(8)], cap=True)
        w.box(H, "frame", 0, fs - 0.1, fs + 0.1, 0.0, 0.015, 1.8, 2.0)
    if m.roof in ("gx", "hip"):
        x = m.x0 + (m.x1 - m.x0) * 0.68
        y = m.y1 - 1.1
        z = m.z_roof(x, y)
        H.lathe("gutter", 0, (x, y, z - 0.3), [(0.055, 0.0), (0.055, 0.75), (0.075, 0.78), (0.075, 0.9)], seg=8)


def roof_edges(B):
    o, g = B.over, B.verge
    x0, x1, y0, y1 = B.x0, B.x1, B.y0, B.y1
    if B.roof == "gx":
        return [((x0 - g, y0 - o), (x1 + g, y0 - o), (0, -1), y0), ((x1 + g, y1 + o), (x0 - g, y1 + o), (0, 1), y1)]
    if B.roof == "gy":
        return [((x0 - o, y1 + g), (x0 - o, y0 - g), (-1, 0), x0), ((x1 + o, y0 - g), (x1 + o, y1 + g), (1, 0), x1)]
    if B.roof == "lean":
        return [((x0 - g, y0 - o), (x1 + g, y0 - o), (0, -1), y0)]
    X0, X1, Y0, Y1 = x0 - o, x1 + o, y0 - o, y1 + o
    return [((X0, Y0), (X1, Y0), (0, -1), y0), ((X1, Y1), (X0, Y1), (0, 1), y1), ((X1, Y0), (X1, Y1), (1, 0), x1), ((X0, Y1), (X0, Y0), (-1, 0), x0)]


# The designs live in Tools/house_kit_designs.py (data, one function per house).


# --- export ------------------------------------------------------------------------------------------------

def arrays(H, lod_need=0):
    """All parts' triangles as (positions, normals, part index), and the parts [(role, lod)]."""
    keys = sorted(H.parts.keys())
    P, N, K = [], [], []
    for i, k in enumerate(keys):
        p, n = H.parts[k]
        if not p:
            continue
        P.append(np.asarray(p, float).reshape(-1, 3))
        N.append(np.asarray(n, float).reshape(-1, 3))
        K.append(np.full(len(p) * 3, i, np.int64))
    return np.concatenate(P), np.concatenate(N), np.concatenate(K), keys


def footprint(H):
    """The walls' extent on the ground: x0, x1, y0, y1."""
    pts = []
    for (role, lod), (p, n) in H.parts.items():
        if role in ("brick", "brick2", "render", "boards", "plinth") and p:
            a = np.asarray(p).reshape(-1, 3)
            a = a[a[:, 2] < 0.5]
            if len(a):
                pts.append(a)
    a = np.concatenate(pts)
    return a[:, 0].min(), a[:, 0].max(), a[:, 1].min(), a[:, 1].max()


def piece_bytes(name, pos, nrm, part_of, parts):
    """The warehouse kit's piece format (Tools/blender_warehouse_kit_export.py): name, parts (section, rgb,
    code, lod), vertices (cm int16, normal int8 over 127, part), triangles (uint16)."""
    q = np.round(pos * 100.0).astype(np.int64)
    qn = np.round(nrm * 127.0).astype(np.int64)
    key = np.concatenate([q, qn, part_of[:, None]], 1)
    uniq, inv = np.unique(key, axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    assert len(uniq) < 65536 and np.abs(q).max() < 32767, (name, len(uniq))
    b = bytearray()
    nm = name.encode()
    b += struct.pack("<H", len(nm)) + nm
    b += struct.pack("<H", len(parts))
    for sec, rgb, code, lod in parts:
        b += struct.pack("<6B", sec, *[int(round(max(0.0, min(1.0, c)) * 255)) for c in rgb], int(round(code * 255)), lod)
    b += struct.pack("<II", len(uniq), len(inv) // 3)
    v = np.zeros(len(uniq), dtype=[("p", "<i2", 3), ("n", "i1", 3), ("k", "u1")])
    v["p"] = uniq[:, 0:3]
    v["n"] = np.clip(uniq[:, 3:6], -127, 127)
    v["k"] = uniq[:, 6]
    b += v.tobytes()
    b += inv.astype("<u2").tobytes()
    return bytes(b), len(inv) // 3, len(uniq)


def role_rgb(role, colours):
    if role in colours:
        return colours[role]
    if role in FIXED_ROLES:
        return FIXED_ROLES[role]
    return {"render2": colours.get("render", C["render_white"])}.get(role, C["white"])


def export(designs, out="house_kit"):
    blob = bytearray()
    count = 0
    rows = []
    for spec, H in designs:
        pos, nrm, part_of, keys = arrays(H)
        x0, x1, y0, y1 = footprint(H)
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        # (to the game's frame: x along the front, y up, z out of the front, about the footprint's middle)
        gp = np.stack([pos[:, 0] - cx, pos[:, 2], -(pos[:, 1] - cy)], 1)
        gn = np.stack([nrm[:, 0], nrm[:, 2], -nrm[:, 1]], 1)
        gn /= np.maximum(np.linalg.norm(gn, axis=1, keepdims=True), 1e-9)
        cw0 = spec["colourways"][0]
        code_tile = TILE_CODE[spec.get("roofing", "tile")]
        parts, roles = [], []
        for role, lod in keys:
            sec, code = ROLE_SECTION.get(role if role != "render2" else "render", (0, 1.0))
            if role in ("tile", "ridge"):
                code = code_tile
            if role == "boards":
                sec = 2
            parts.append((sec, role_rgb(role, cw0), code, lod))
            roles.append(role)
        # (split into pieces of fewer than 65536 points, whole triangles each)
        names = []
        tri_n = len(gp) // 3
        start = 0
        chunk = 0
        step = 18000
        while start < tri_n:
            end = min(tri_n, start + step)
            sl = slice(start * 3, end * 3)
            nm = "%s/%d" % (spec["name"], chunk)
            b, tris, verts = piece_bytes(nm, gp[sl], gn[sl], part_of[sl], parts)
            blob += b
            count += 1
            names.append(nm)
            start = end
            chunk += 1
        lod_tris = H.counts()
        rows.append({
            "name": spec["name"], "label": spec["label"], "set": spec["set"],
            "L": round(x1 - x0, 2), "W": round(y1 - y0, 2), "H": round(float(gp[:, 1].max()), 2),
            "floors": spec.get("floors", 2), "classes": spec.get("classes", [spec["set"]]),
            "regions": spec.get("regions", []), "weight": spec.get("weight", 1.0),
            "pieces": names, "roles": roles,
            "colourways": [{r: [round(c, 4) for c in v] for r, v in cw.items()} for cw in spec["colourways"]],
            "tris": lod_tris,
        })
    head = b"TSWK" + struct.pack("<II", 1, count)
    os.makedirs(DATA, exist_ok=True)
    with open(os.path.join(DATA, out + ".bin"), "wb") as f:
        f.write(head + bytes(blob))
    about = ("Ride UK's detached houses (beenster Nov 26: house pack sheets as the look to aim for; our own designs): "
             "whole houses modelled by Tools/blender_house_kit.py (designs in Tools/house_kit_designs.py), which writes "
             "this file and house_kit.bin (the warehouse kit's piece format). Each design: its footprint L along the front "
             "by W deep and height H (m), its pieces, each part's role (in the pieces' part order) and colourways (role -> "
             "linear colour; the first is the pieces' own). Sets: estate (brick new-builds), villa (Edwardian and inter-war), "
             "render (rendered, painted, weatherboarded). World/WorldRideHouseKit.cpp lays them on the map's detached houses "
             "whose footprint one fits.")
    with open(os.path.join(DATA, out + ".json"), "w") as f:
        json.dump({"_about": about, "designs": rows}, f, indent=1)
    return rows


def main():
    sys.path.insert(0, HERE)
    import house_kit_designs as D
    built = []
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1].split(",")
    for spec in D.designs():
        if only and spec["name"] not in only:
            continue
        H = assemble(spec)
        built.append((spec, H))
        print("%-22s %-8s LOD0 %6d  LOD1 %6d  LOD2 %5d" % ((spec["name"], spec["set"]) + tuple(H.counts())))
    if "--render" in sys.argv:
        import blender_house_kit_render as R
        R.render(built, sys.argv[sys.argv.index("--render") + 1])
    if not only:
        export(built)


if __name__ == "__main__":
    main()
