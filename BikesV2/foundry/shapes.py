"""Outline maths shared by the drawing step and the 3D step.

dense() turns a described outline (points, with [x, z, 1] marking a sharp corner)
into a fine polyline, using the same curve rule as foundry/elevation.py, so the
3D model follows exactly the line that was drawn and approved.
"""


def _bez(p0, c1, c2, p3, n=10):
    out = []
    for i in range(1, n + 1):
        u = i / n
        a, b, c, d = (1 - u) ** 3, 3 * u * (1 - u) ** 2, 3 * u * u * (1 - u), u ** 3
        out.append((a * p0[0] + b * c1[0] + c * c2[0] + d * p3[0], a * p0[1] + b * c1[1] + c * c2[1] + d * p3[1]))
    return out


def _run(q, t):
    n = len(q)
    m = []
    for i in range(n):
        if i == 0:
            m.append((q[1][0] - q[0][0], q[1][1] - q[0][1]))
        elif i == n - 1:
            m.append((q[-1][0] - q[-2][0], q[-1][1] - q[-2][1]))
        else:
            m.append(((q[i + 1][0] - q[i - 1][0]) / 2, (q[i + 1][1] - q[i - 1][1]) / 2))
    out = []
    for i in range(n - 1):
        c1 = (q[i][0] + m[i][0] * t / 3, q[i][1] + m[i][1] * t / 3)
        c2 = (q[i + 1][0] - m[i + 1][0] * t / 3, q[i + 1][1] - m[i + 1][1] * t / 3)
        out += _bez(q[i], c1, c2, q[i + 1])
    return out


def dense(pts, closed=True, t=1.0):
    pts = [tuple(p) for p in pts]
    kinks = [i for i, p in enumerate(pts) if len(p) > 2 and p[2]]
    if not closed:
        idx = [0] + [k for k in kinks if 0 < k < len(pts) - 1] + [len(pts) - 1]
        out = [pts[0][:2]]
        for a, b in zip(idx, idx[1:]):
            out += _run(pts[a:b + 1], t) if b - a > 1 else [pts[b][:2]]
        return out
    if not kinks:
        n = len(pts)
        out = [pts[0][:2]]
        for i in range(n):
            p0, p1, p2, p3 = pts[(i - 1) % n], pts[i], pts[(i + 1) % n], pts[(i + 2) % n]
            c1 = (p1[0] + (p2[0] - p0[0]) * t / 6, p1[1] + (p2[1] - p0[1]) * t / 6)
            c2 = (p2[0] - (p3[0] - p1[0]) * t / 6, p2[1] - (p3[1] - p1[1]) * t / 6)
            out += _bez(p1, c1, c2, p2)
        return out[:-1]
    k0 = kinks[0]
    pts = pts[k0:] + pts[:k0] + [pts[k0]]
    kinks = [i for i, p in enumerate(pts) if len(p) > 2 and p[2]]
    out = [pts[0][:2]]
    for a, b in zip(kinks, kinks[1:]):
        out += _run(pts[a:b + 1], t) if b - a > 1 else [pts[b][:2]]
    return out[:-1]


def top_bottom(poly, x):
    """Highest and lowest z where the vertical line at x crosses a closed polyline, or None."""
    zs = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        if (a[0] - x) * (b[0] - x) <= 0 and a[0] != b[0]:
            zs.append(a[1] + (b[1] - a[1]) * (x - a[0]) / (b[0] - a[0]))
    return (max(zs), min(zs)) if len(zs) >= 2 else None


def lerp_table(tab, u):
    """Piecewise-linear lookup in [[u, value], ...]."""
    if u <= tab[0][0]:
        return tab[0][1]
    for (u0, v0), (u1, v1) in zip(tab, tab[1:]):
        if u <= u1:
            return v0 + (v1 - v0) * (u - u0) / (u1 - u0)
    return tab[-1][1]
