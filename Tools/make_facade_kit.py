"""The houses' windows, doors and their surrounds as kit models (fps plan step 2, beenster Nov 15: "The
game NEEDS high fps"): Data/RideUK/Facade/*.glb, made once here and kept in the repo, which
Tools/ue_build_rideuk.py (prop_kit) imports beside the other props, and World/WorldRideBuildings.cpp
places as instances (RidePlan::FacadeKit) in place of the boxes and quads it drew for each one in 149.

Each is SM_Prop_<kind>_<n>, kind as World/WorldRideProps.cpp's RidePlan::PropName gives it, n its size
(the SIZES tables below, which World/WorldRideBuildings.cpp's FacadeSize must match). Like the other
props: the front to +x, the wall's face at x = 0, the opening's foot (a window's sill line, a door's
threshold at the ground) at y = 0 and its middle at z = 0, along the wall z, life size (the game places
them unscaled).

Colours: an instance carries three colours of its own (M_RideFacade reads them from its per-instance
custom data): the house's joinery paint, its trim (dressed stone, or the wall's own colour in a reveal),
and a third (a door's paint; a window's sill). A vertex says which with its alpha, and how light with
its RGB (half the shade, so up to 2x): alpha 1 its own colour as given (glass, brass, slate), 0.66 the
paint, 0.33 the trim, 0 the third.

    python Tools/make_facade_kit.py
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import buttertubs_stone as ST            # noqa: E402
from export_buttertubs import write_glb  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "RideUK", "Facade")

OWN, PAINT, TRIM, THIRD = 1.0, 0.66, 0.33, 0.0
GLASS = (0.02, 0.025, 0.03)
BRASS = (0.42, 0.30, 0.09)
IRON = (0.02, 0.02, 0.02)
SLATE = (0.07, 0.07, 0.078)

# The sizes (width, height of the opening, m), by n. Must match FacadeSize in WorldRideBuildings.cpp.
WINDOW_SIZES = {"sash": [(0.9, 1.45), (0.72, 1.45), (1.0, 2.0)],
                "casement": [(0.85, 1.0), (0.68, 1.0), (1.0, 2.0)],
                "mullion": [(1.3, 0.9), (1.04, 0.9), (1.0, 2.0)]}
# A surround's openings: the windows' nine in that order, then the doors' (width, height from the step).
SURROUND_SIZES = WINDOW_SIZES["sash"] + WINDOW_SIZES["casement"] + WINDOW_SIZES["mullion"] + [(0.95, 1.9), (0.9, 1.9), (1.6, 1.9), (1.6, 2.45)]
DOOR_SIZES = [(0.95, 2.05), (0.9, 2.05)]
WIDE_DOOR_SIZES = [(1.6, 2.05), (1.6, 2.6)]
REV_D = 0.15
# [build 153] beenster: "the frames stand out a little far from the brick faces": every model's depth out
# of the wall (x) is drawn at this share of the figures above.
DEPTH = 0.5


def colour(slot, shade=1.0, own=None):
    if slot == OWN:
        return (own[0], own[1], own[2], 1.0)
    s = min(1.0, shade / 2.0)
    return (s, s, s, slot)


def box(M, u0, u1, y0, y1, d0, d1, slot, shade=1.0, own=None, back=False):
    """A box out of the wall: u along it, y up, d out of it (x). Its back (against the wall) left off."""
    c = ((d0 + d1) / 2, (y0 + y1) / 2, (u0 + u1) / 2)
    h = (max(0.002, (d1 - d0) / 2), max(0.002, (y1 - y0) / 2), max(0.002, (u1 - u0) / 2))
    P, T, U = ST.obox(c, h)
    if not back:
        fc = P[T].mean(1)
        keep = fc[:, 0] > c[0] - h[0] + 1e-6
        T = T[keep]
    col = np.repeat(np.array([colour(slot, shade, own)]), len(P), 0)
    M.add(P, T, col, U)


def face(M, u0, u1, y0, y1, d, slot, shade=1.0, own=None):
    """A flat face d out from the wall, facing out."""
    P = np.array([[d, y0, u0], [d, y0, u1], [d, y1, u1], [d, y1, u0]], float)
    T = np.array([[0, 2, 1], [0, 3, 2]])
    fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    T[fn[:, 0] < 0] = T[fn[:, 0] < 0][:, [0, 2, 1]]
    col = np.repeat(np.array([colour(slot, shade, own)]), 4, 0)
    M.add(P, T, col, P[:, [2, 1]])


# --- Windows: the glass, the frame and the glazing (the surround is its own model) ---------------------

def window(kind, W, H):
    M = ST.Mesh()
    face(M, -W / 2, W / 2, 0, H, 0.04, OWN, own=GLASS)
    F0, F1, Fw = 0.04, 0.075, 0.06
    face(M, -W / 2, -W / 2 + Fw, 0, H, F0 + 0.004, PAINT)
    face(M, W / 2 - Fw, W / 2, 0, H, F0 + 0.004, PAINT)
    face(M, -W / 2, W / 2, H - Fw, H, F0 + 0.005, PAINT)
    face(M, -W / 2, W / 2, 0, Fw * 1.4, F0 + 0.005, PAINT)
    In0, In1 = -W / 2 + Fw, W / 2 - Fw
    Iw, Bot, Tp = In1 - In0, Fw * 1.4, H - Fw
    Gb = 0.022

    def vbar(uc, y0, y1, wd):
        face(M, uc - wd / 2, uc + wd / 2, y0, y1, F0 + 0.007, PAINT)

    def hbar(yc, wd):
        face(M, In0, In1, yc - wd / 2, yc + wd / 2, F0 + 0.008, PAINT)

    if kind.startswith("win_sash"):
        meet = (Bot + Tp) / 2
        box(M, In0, In1, meet - 0.035, meet + 0.035, F0, F1 + 0.01, PAINT)
        face(M, In0, In1, meet + 0.035, Tp, F0 + 0.003, OWN, own=tuple(g * 1.05 for g in GLASS))
        if kind == "win_sash6":
            for m in (1, 2):
                vbar(In0 + Iw * m / 3, Bot, Tp, Gb)
            hbar((Bot + meet) / 2, Gb)
            hbar((meet + Tp) / 2, Gb)
        else:
            vbar(0.0, Bot, Tp, Gb * 1.3)
    elif kind.startswith("win_casement"):
        tr = Bot + (Tp - Bot) * 0.7
        vbar(0.0, Bot, Tp, 0.06)
        face(M, In0, In1, tr - 0.03, tr + 0.03, F0 + 0.009, PAINT)
        if kind == "win_casement_top":
            for m in (1, 2):
                vbar(In0 + Iw * m / 3, tr, Tp, Gb)
    else:
        # Stone mullions between two or three lights (in the trim's colour), a transom across tall ones.
        lights = 3 if W > 1.1 else 2
        for m in range(1, lights):
            u = In0 + Iw * m / lights
            box(M, u - 0.05, u + 0.05, Bot, Tp, 0.0, REV_D - 0.02, TRIM)
        if H > 1.2:
            y = Bot + (Tp - Bot) * 0.68
            box(M, In0, In1, y - 0.04, y + 0.04, 0.0, REV_D - 0.02, TRIM)
        if kind == "win_mullion_leaded":
            for m in range(lights):
                l0, l1 = In0 + Iw * m / lights + 0.05, In0 + Iw * (m + 1) / lights - 0.05
                face(M, l0, l1, (Bot + Tp) / 2 - 0.006, (Bot + Tp) / 2 + 0.006, 0.05, PAINT, 0.2)
                face(M, (l0 + l1) / 2 - 0.006, (l0 + l1) / 2 + 0.006, Bot, Tp, 0.05, PAINT, 0.2)
    return M


def surround(style, W, H, sill):
    """The opening's reveal (its jambs and head standing 15 cm out of the glass), its lintel (dressed
    stone, a brick soldier arch, or a plain head on a rendered wall), and a window's projecting sill."""
    M = ST.Mesh()
    rev_w = 0.17 if style == "stone" else 0.11
    box(M, -W / 2 - rev_w, -W / 2, 0, H, 0.0, REV_D, TRIM)
    box(M, W / 2, W / 2 + rev_w, 0, H, 0.0, REV_D, TRIM)
    if style == "stone":
        box(M, -W / 2 - rev_w - 0.04, W / 2 + rev_w + 0.04, H, H + 0.24, 0.0, REV_D + 0.03, TRIM)
    elif style == "brick":
        box(M, -W / 2 - 0.12, W / 2 + 0.12, H, H + 0.22, 0.0, REV_D - 0.02, TRIM, 0.62 / 0.72)
    else:
        box(M, -W / 2 - rev_w, W / 2 + rev_w, H, H + 0.1, 0.0, REV_D, TRIM)
    if sill:
        box(M, -W / 2 - rev_w - 0.06, W / 2 + rev_w + 0.06, -0.1, 0.0, 0.0, REV_D + 0.07, THIRD)
    return M


# --- Doors: the step, the frame, the leaf (the third colour), fanlight, brass --------------------------

def door(style, W, H):
    M = ST.Mesh()
    box(M, -W / 2 - 0.18, W / 2 + 0.18, -0.6, 0.15, 0.0, 0.42, TRIM, 0.9)   # the step, in the trim's stone
    box(M, -W / 2, -W / 2 + 0.07, 0.15, H, 0.03, 0.08, PAINT)
    box(M, W / 2 - 0.07, W / 2, 0.15, H, 0.03, 0.08, PAINT)
    box(M, -W / 2, W / 2, H - 0.07, H, 0.03, 0.08, PAINT)
    D0, D1 = -W / 2 + 0.07, W / 2 - 0.07
    Dw = D1 - D0
    wide = style == "door_double"
    fan = style in ("door_panel6", "door_victorian") and H > 1.95
    Lh = H - 0.4 if fan else H
    Lo, Hi = 0.15, (Lh if fan else Lh - 0.07)
    Lf = 0.04
    face(M, D0, D1, Lo, Hi, Lf, THIRD)
    if fan:
        box(M, D0, D1, Lh - 0.03, Lh + 0.04, 0.03, 0.08, PAINT)
        face(M, D0, D1, Lh + 0.04, H - 0.07, 0.04, OWN, own=GLASS)
        if style == "door_panel6":
            for k in (1, 2, 3):
                u = D0 + Dw * k / 4
                face(M, u - 0.012, u + 0.012, Lh + 0.04, H - 0.07, 0.046, PAINT)
    RAISED, SUNK = 1.25, 0.7

    def panel(u0, u1, y0, y1, glazed=False):
        if glazed:
            face(M, u0, u1, y0, y1, Lf + 0.004, OWN, own=GLASS)
        else:
            face(M, u0, u1, y0, y1, Lf + 0.004, THIRD, SUNK)
            face(M, u0 + 0.035, u1 - 0.035, y0 + 0.035, y1 - 0.035, Lf + 0.012, THIRD, RAISED)
        face(M, u0 - 0.012, u1 + 0.012, y1, y1 + 0.012, Lf + 0.006, THIRD, RAISED)
        face(M, u0 - 0.012, u1 + 0.012, y0 - 0.012, y0, Lf + 0.006, THIRD, RAISED)

    sw, mid = 0.11, 0.0
    if wide:
        for k in (0, 1):
            a0, a1 = D0 + k * Dw / 2 + sw * 0.7, D0 + (k + 1) * Dw / 2 - sw * 0.7
            panel(a0, a1, Lo + 0.25, Lo + (Hi - Lo) * 0.42)
            panel(a0, a1, Lo + (Hi - Lo) * 0.5, Hi - 0.2)
        face(M, mid - 0.01, mid + 0.01, Lo, Hi, Lf + 0.006, THIRD, SUNK)
    elif style == "door_panel6":
        rows = [Lo + 0.16, Lo + (Hi - Lo) * 0.36, Lo + (Hi - Lo) * 0.44, Hi - 0.13]
        for su in (-1, 1):
            a0, a1 = (D0 + sw, mid - sw * 0.4) if su < 0 else (mid + sw * 0.4, D1 - sw)
            panel(a0, a1, rows[0], rows[1])
            panel(a0, a1, rows[2], rows[2] + (rows[3] - rows[2]) * 0.62)
            panel(a0, a1, rows[2] + (rows[3] - rows[2]) * 0.7, rows[3])
    elif style == "door_victorian":
        for su in (-1, 1):
            a0, a1 = (D0 + sw, mid - sw * 0.4) if su < 0 else (mid + sw * 0.4, D1 - sw)
            panel(a0, a1, Lo + 0.16, Lo + (Hi - Lo) * 0.42)
            panel(a0, a1, Lo + (Hi - Lo) * 0.5, Hi - 0.13, glazed=True)
    elif style.startswith("door_ledged"):
        u = D0 + 0.12
        while u < D1 - 0.05:
            face(M, u - 0.006, u + 0.006, Lo, Hi, Lf + 0.003, THIRD, SUNK)
            u += 0.12
        for y in (Lo + 0.2, (Lo + Hi) / 2, Hi - 0.2):
            box(M, D0 + 0.03, D1 - 0.03, y - 0.06, y + 0.06, Lf, Lf + 0.025, THIRD, RAISED)
        if style == "door_ledged_light":
            face(M, mid - 0.15, mid + 0.15, Hi - 0.45, Hi - 0.25, Lf + 0.004, OWN, own=GLASS)
    else:   # door_modern
        g0, g1 = D0 + Dw * 0.55, D1 - 0.12
        face(M, g0, g1, Lo + 0.55, Hi - 0.15, Lf + 0.004, OWN, own=GLASS)
        face(M, g0 - 0.02, g1 + 0.02, Lo + 0.53, Lo + 0.55, Lf + 0.006, THIRD, RAISED)
        face(M, g0 - 0.02, g1 + 0.02, Hi - 0.15, Hi - 0.13, Lf + 0.006, THIRD, RAISED)
    if not wide:
        metal = IRON if style.startswith("door_ledged") else BRASS
        y = Lo + (Hi - Lo) * 0.39
        box(M, mid - 0.14, mid + 0.14, y, y + 0.06, Lf, Lf + 0.03, OWN, own=metal)
        box(M, D1 - 0.11, D1 - 0.05, Lo + 0.95, Lo + 1.01, Lf, Lf + 0.07, OWN, own=metal)
        if style == "door_panel6":
            y = Lo + (Hi - Lo) * 0.7
            box(M, mid - 0.05, mid + 0.05, y, y + 0.14, Lf, Lf + 0.03, OWN, own=metal)
    return M


def hood(W, H):
    """A flat lead-topped canopy on two brackets over a door of width W and height H (from the ground)."""
    M = ST.Mesh()
    box(M, -W / 2 - 0.3, W / 2 + 0.3, H + 0.3, H + 0.42, 0.0, 0.5, PAINT)
    box(M, -W / 2 - 0.3, W / 2 + 0.3, H + 0.42, H + 0.45, 0.0, 0.52, OWN, own=SLATE)
    for su in (-1, 1):
        u = su * (W / 2 + 0.2)
        box(M, u - 0.04, u + 0.04, H + 0.05, H + 0.3, 0.0, 0.35, PAINT)
    return M


# --- [towns stage 4] Bay windows: a Victorian terrace's canted bay, an inter-war semi's square one ----------

def moved(M, part, n, t, origin):
    """part (made with the wall's frame: d out of it as x, y up, u along it as z) set on a face of the bay
    whose outward normal is n and whose run is t (both (d, u)), its u = 0 at origin (d, u)."""
    for k in range(len(part.P)):
        P = part.P[k]
        d, y, u = P[:, 0], P[:, 1], P[:, 2]
        out = np.stack([origin[0] + d * n[0] + u * t[0], y, origin[1] + d * n[1] + u * t[1]], 1)
        M.add(out, part.T[k] - sum(len(q) for q in part.P[:k]), part.C[k], part.U[k])


BAY_PIER = 0.12          # the masonry each side of a light
BAY_LIGHT = 1.45         # its sash's height (the house's own windows')


def bay_storey(F, L, y0, top):
    """One storey of one face of a bay, L long (u from -L/2): its plinth or the band between floors under
    the sill at y0 + 1.0, the light (a two-over-two sash) between piers, its head, and on the top storey a
    cornice. In the trim's colour (the bay's dressed or painted stone), the joinery in the paint's."""
    lo = y0 + 1.0
    face(F, -L / 2, L / 2, y0 - (0.6 if y0 == 0 else 0.0), lo - 0.1, 0.0, TRIM)
    box(F, -L / 2 - 0.02, L / 2 + 0.02, lo - 0.1, lo, -0.02, 0.06, TRIM, 1.1)      # the sill
    W = L - 2 * BAY_PIER
    if W < 0.5:
        # (a square bay's short side: too narrow for a light, solid masonry)
        face(F, -L / 2, L / 2, lo, lo + BAY_LIGHT, 0.0, TRIM)
        face(F, -L / 2, L / 2, lo + BAY_LIGHT, y0 + 2.7 + (0.15 if top else 0.0), 0.0, TRIM)
        if top:
            box(F, -L / 2 - 0.05, L / 2 + 0.05, y0 + 2.7 + 0.15, y0 + 2.7 + 0.27, -0.02, 0.09, TRIM, 1.15)
        return
    for su in (-1, 1):
        box(F, su * (L / 2 - BAY_PIER / 2) - BAY_PIER / 2, su * (L / 2 - BAY_PIER / 2) + BAY_PIER / 2, lo, lo + BAY_LIGHT, -0.08, 0.0, TRIM)
    win = window("win_sash2", W, BAY_LIGHT)
    for k in range(len(win.P)):
        win.P[k] = win.P[k] + np.array([-0.09, lo, 0.0])
    F.P += win.P; F.T += [t + F.n for t in win.T]; F.C += win.C; F.U += win.U
    F.n += sum(len(q) for q in win.P)
    face(F, -L / 2, L / 2, lo + BAY_LIGHT, y0 + 2.7 + (0.15 if top else 0.0), 0.0, TRIM)   # the head (the band up to the next sill)
    if top:
        box(F, -L / 2 - 0.05, L / 2 + 0.05, y0 + 2.7 + 0.15, y0 + 2.7 + 0.27, -0.02, 0.09, TRIM, 1.15)   # the cornice


def bay(shape, storeys):
    """A bay window, its back on the wall (x = 0), its middle at z = 0, its foot at the ground: canted (a
    front 1.1 m and sides at 45 degrees, 0.65 m out) or square (2.3 m wide, 0.6 m out), one storey or two,
    under a slate hipped roof (the third colour) leaning back to the wall."""
    M = ST.Mesh()
    if shape == "canted":
        P, Fw = 0.65, 1.1
        Wt = Fw + 2 * P
        pts = [(0.0, -Wt / 2), (P, -Fw / 2), (P, Fw / 2), (0.0, Wt / 2)]
    else:
        P, Wt = 0.6, 2.3
        pts = [(0.0, -Wt / 2), (P, -Wt / 2), (P, Wt / 2), (0.0, Wt / 2)]
    top_y = 2.7 * storeys + 0.27
    for a, b in zip(pts[:-1], pts[1:]):
        dd, du = b[0] - a[0], b[1] - a[1]
        L = float(np.hypot(dd, du))
        t = (dd / L, du / L)
        n = (t[1], -t[0])
        F = ST.Mesh()
        for s in range(storeys):
            bay_storey(F, L, 2.7 * s, s == storeys - 1)
        moved(M, F, n, t, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2))
    # The roof: from the cornice's edge (a hair outside the faces) up to the wall, 0.5 m higher.
    rim = [(0.0, pts[0][1] - 0.08)] + [(d + 0.09, u + (0.06 if u > 0 else -0.06) * (1 if d > 0 else 0)) for d, u in pts[1:-1]] + [(0.0, pts[-1][1] + 0.08)]
    hi = top_y + 0.5
    for (d0, u0), (d1, u1) in zip(rim[:-1], rim[1:]):
        # a slope from this run of the rim back to the wall's line (its ends drawn in toward the middle)
        q = np.array([[d0, top_y, u0], [d1, top_y, u1], [0.0, hi, u1 * 0.6], [0.0, hi, u0 * 0.6]], float)
        T = np.array([[0, 1, 2], [0, 2, 3]])
        fn = np.cross(q[T[:, 1]] - q[T[:, 0]], q[T[:, 2]] - q[T[:, 0]])
        T[fn[:, 1] < 0] = T[fn[:, 1] < 0][:, [0, 2, 1]]
        M.add(q, T, np.repeat(np.array([colour(THIRD, 1.0)]), 4, 0), q[:, [2, 0]])
    # (the roof's soffit, under the rim, so it doesn't show daylight from below)
    q = np.array([[r[0], top_y - 0.001, r[1]] for r in rim], float)
    T = np.array([[0, k, k + 1] for k in range(1, len(q) - 1)])
    fn = np.cross(q[T[:, 1]] - q[T[:, 0]], q[T[:, 2]] - q[T[:, 0]])
    T[fn[:, 1] > 0] = T[fn[:, 1] > 0][:, [0, 2, 1]]
    M.add(q, T, np.repeat(np.array([colour(TRIM, 0.8)]), len(q), 0), q[:, [2, 0]])
    return M


def made():
    out = {}
    for kind, sizes in (("win_sash6", "sash"), ("win_sash2", "sash"), ("win_casement", "casement"), ("win_casement_top", "casement"),
                        ("win_mullion", "mullion"), ("win_mullion_leaded", "mullion")):
        out[kind] = [window(kind, W, H) for W, H in WINDOW_SIZES[sizes]]
    for style in ("stone", "brick", "plain"):
        out["sur_" + style] = [surround(style, W, H, n < 9) for n, (W, H) in enumerate(SURROUND_SIZES)]
    for style in ("door_panel6", "door_victorian", "door_ledged", "door_ledged_light", "door_modern"):
        out[style] = [door(style, W, H) for W, H in DOOR_SIZES]
    out["door_double"] = [door("door_double", W, H) for W, H in WIDE_DOOR_SIZES]
    out["door_hood"] = [hood(W, H) for W, H in DOOR_SIZES]
    # [towns stage 4] (a bay's n its storeys less one)
    out["bay_canted"] = [bay("canted", 1), bay("canted", 2)]
    out["bay_square"] = [bay("square", 1), bay("square", 2)]
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".glb"):
            os.remove(os.path.join(OUT, f))
    total = 0
    count = 0
    for kind, meshes in made().items():
        for n, M in enumerate(meshes):
            name = "SM_Prop_%s_%d" % (kind, n)
            if not kind.startswith("bay_"):   # (a bay stands its own depth out: it is no frame)
                for k in range(len(M.P)):
                    M.P[k] = M.P[k] * np.array([DEPTH, 1.0, 1.0])
            nv, nt = M.write(os.path.join(OUT, name + ".glb"), write_glb, name)
            total += nt
            count += 1
            print("%s: %d triangles" % (name, nt))
    print("%d models, %d triangles in %s" % (count, total, OUT))


if __name__ == "__main__":
    main()
