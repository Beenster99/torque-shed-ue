"""Decal kit: the drawing helpers every decal script uses (Tools/decals/*.py).

A decal is drawn as up to three layers, each a skia.Path in pixels (y down) on a W x H canvas:
  fill  - the main colour (colour 1 in the game)
  line  - the outline / ink (colour 2), drawn over the fill
  shade - a darker tone of the fill (a little tattoo shading), only where the fill is
The game's PNG keeps them as R = fill, G = line, B = shade, A = coverage, so one picture can be
shown filled or line-only and in any two colours the player picks (M_ShedDecal).

Everything here is plain geometry: no outside artwork goes in.
"""
import math

import numpy as np
import skia

# --------------------------------------------------------------------------------------------- paths


def P():
    return skia.Path()


def union(*paths):
    out = None
    for p in paths:
        if p is None:
            continue
        out = p if out is None else skia.Op(out, p, skia.PathOp.kUnion_PathOp)
    return out if out is not None else P()


def diff(a, *cut):
    out = a
    for c in cut:
        if c is not None:
            out = skia.Op(out, c, skia.PathOp.kDifference_PathOp)
    return out


def inter(a, b):
    return skia.Op(a, b, skia.PathOp.kIntersect_PathOp)


def xform(path, sx=1.0, sy=1.0, rot=0.0, tx=0.0, ty=0.0, cx=0.0, cy=0.0):
    """Scale and turn (degrees) about (cx, cy), then move by (tx, ty)."""
    m = skia.Matrix()
    m.setTranslate(tx + cx, ty + cy)
    m.preRotate(rot)
    m.preScale(sx, sy)
    m.preTranslate(-cx, -cy)
    q = skia.Path(path)
    q.transform(m)
    return q


def mirror_x(path, cx):
    return xform(path, sx=-1.0, cx=cx)


def mirror_y(path, cy):
    return xform(path, sy=-1.0, cy=cy)


def sym_x(path, cx):
    """The path and its mirror image about x = cx, joined."""
    return union(path, mirror_x(path, cx))


def poly(pts, close=True):
    p = P()
    p.moveTo(*pts[0])
    for q in pts[1:]:
        p.lineTo(*q)
    if close:
        p.close()
    return p


def circle(x, y, r):
    p = P()
    p.addCircle(x, y, r)
    return p


def ellipse(x, y, rx, ry, rot=0.0):
    p = P()
    p.addOval(skia.Rect.MakeLTRB(x - rx, y - ry, x + rx, y + ry))
    return xform(p, rot=rot, cx=x, cy=y) if rot else p


def rect(x0, y0, x1, y1, r=0.0):
    p = P()
    if r > 0:
        p.addRRect(skia.RRect.MakeRectXY(skia.Rect.MakeLTRB(x0, y0, x1, y1), r, r))
    else:
        p.addRect(skia.Rect.MakeLTRB(x0, y0, x1, y1))
    return p


def stroke(path, w, cap="round", join="round", miter=4.0):
    """A path's outline as a filled shape, w wide."""
    paint = skia.Paint(Style=skia.Paint.kStroke_Style, StrokeWidth=w, AntiAlias=True)
    paint.setStrokeCap({"round": skia.Paint.kRound_Cap, "butt": skia.Paint.kButt_Cap, "square": skia.Paint.kSquare_Cap}[cap])
    paint.setStrokeJoin({"round": skia.Paint.kRound_Join, "miter": skia.Paint.kMiter_Join, "bevel": skia.Paint.kBevel_Join}[join])
    paint.setStrokeMiter(miter)
    out = P()
    paint.getFillPath(path, out)
    return skia.Op(out, P(), skia.PathOp.kUnion_PathOp)  # simplified (no self overlaps)


def outline(path, w):
    """The ink line round a shape: a stroke centred on its edge."""
    return stroke(path, w)


def grow(path, d):
    """The shape made d bigger all round (d < 0: smaller)."""
    if d >= 0:
        return union(path, stroke(path, 2 * d))
    return diff(path, stroke(path, -2 * d))


# ------------------------------------------------------------------------------------- curves & ink


def bez(p0, p1, p2, p3, n=48):
    out = []
    for i in range(n + 1):
        t = i / n
        a = (1 - t) ** 3
        b = 3 * (1 - t) ** 2 * t
        c = 3 * (1 - t) * t * t
        d = t ** 3
        out.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return out


def quad(p0, p1, p2, n=40):
    return bez(p0, (p0[0] + 2 / 3 * (p1[0] - p0[0]), p0[1] + 2 / 3 * (p1[1] - p0[1])),
               (p2[0] + 2 / 3 * (p1[0] - p2[0]), p2[1] + 2 / 3 * (p1[1] - p2[1])), p2, n)


def spline(pts, n=16, closed=False):
    """A smooth curve through pts (Catmull-Rom)."""
    if len(pts) < 2:
        return list(pts)
    q = list(pts)
    if closed:
        q = [pts[-1]] + list(pts) + [pts[0], pts[1]]
    else:
        q = [pts[0]] + list(pts) + [pts[-1]]
    out = []
    for i in range(1, len(q) - 2):
        p0, p1, p2, p3 = q[i - 1], q[i], q[i + 1], q[i + 2]
        for k in range(n):
            t = k / n
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2
                                    + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3) for j in range(2)))
    if not closed:
        out.append(tuple(pts[-1]))
    return out


def smooth(pts, n=16):
    """A closed smooth shape through pts."""
    return poly(spline(pts, n, closed=True))


def resample(pts, step):
    """pts with points added so none are more than step apart."""
    out = [tuple(pts[0])]
    for a, b in zip(pts, pts[1:]):
        d = math.dist(a, b)
        k = max(1, int(d / step))
        for i in range(1, k + 1):
            out.append((a[0] + (b[0] - a[0]) * i / k, a[1] + (b[1] - a[1]) * i / k))
    return out


def line_path(pts):
    return poly(pts, close=False)


def taper(pts, w0, w1=None, wmid=None, ends="point"):
    """A brush stroke along pts: w0 wide at the start, w1 at the end, wmid at the middle (ink look).
    ends="point" thins both tips to nothing, "round" keeps them blunt."""
    if w1 is None:
        w1 = w0
    if wmid is None:
        wmid = (w0 + w1) / 2
    pts = resample(pts, 6.0)
    n = len(pts)
    if n < 2:
        return P()
    # length along
    L = [0.0]
    for i in range(1, n):
        L.append(L[-1] + math.dist(pts[i - 1], pts[i]))
    tot = L[-1] or 1.0
    left, right = [], []
    for i in range(n):
        t = L[i] / tot
        w = (1 - t) * (1 - t) * w0 + 2 * t * (1 - t) * wmid + t * t * w1
        if ends == "point":
            w *= min(1.0, math.sin(math.pi * min(t, 1 - t)) * 3.0 + 0.08)
        elif ends == "tip":
            w *= min(1.0, (1 - t) ** 0.5 * 1.3 + 0.03)
        a = pts[max(0, i - 1)]
        b = pts[min(n - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        d = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / d, dx / d
        left.append((pts[i][0] + nx * w / 2, pts[i][1] + ny * w / 2))
        right.append((pts[i][0] - nx * w / 2, pts[i][1] - ny * w / 2))
    p = poly(left + right[::-1])
    if ends == "round":
        p = union(p, circle(*pts[0], w0 / 2), circle(*pts[-1], w1 / 2))
    return skia.Op(p, P(), skia.PathOp.kUnion_PathOp)


def curl(cx, cy, r0, turns=1.25, sign=1, start=0.0, shrink=0.75, n=80):
    """A spiral (a pinstripe's curled end, a wave's curl)."""
    pts = []
    for i in range(n + 1):
        t = i / n
        a = start + sign * t * turns * 2 * math.pi
        r = r0 * (1 - shrink * t)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def arc_pts(cx, cy, r, a0, a1, n=40):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
            for i in range(n + 1)]


def star(cx, cy, r0, r1, n=5, rot=-90.0):
    pts = []
    for i in range(2 * n):
        r = r0 if i % 2 == 0 else r1
        a = math.radians(rot + i * 180.0 / n)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return poly(pts)


# ------------------------------------------------------------------------------------------ raster


class Decal:
    """One library decal: its id, shelf, name, tags, size and layers."""

    def __init__(self, id, shelf, name, tags, w, h, fill=None, line=None, shade=None, colours=1, note=""):
        self.id, self.shelf, self.name, self.tags = id, shelf, name, tags
        self.w, self.h = w, h
        self.fill, self.line, self.shade = fill, line, shade
        self.colours = colours  # how many colours it takes (1: fill only or line only)
        self.note = note

    def fitted(self, margin=12):
        """Scaled and centred to fill the canvas less margin (keeps its proportions)."""
        b = None
        for k in ("fill", "line", "shade"):
            p = getattr(self, k)
            if p is not None and not p.isEmpty():
                r = p.computeTightBounds()
                b = r if b is None else skia.Rect.MakeLTRB(min(b.left(), r.left()), min(b.top(), r.top()), max(b.right(), r.right()), max(b.bottom(), r.bottom()))
        if b is None:
            return self
        s = min((self.w - 2 * margin) / b.width(), (self.h - 2 * margin) / b.height())
        tx = self.w / 2 - (b.left() + b.width() / 2) * s
        ty = self.h / 2 - (b.top() + b.height() / 2) * s
        for k in ("fill", "line", "shade"):
            p = getattr(self, k)
            if p is not None and not p.isEmpty():
                setattr(self, k, xform(p, sx=s, sy=s, tx=tx, ty=ty))
        return self

    def masks(self, scale=1.0):
        W, H = int(round(self.w * scale)), int(round(self.h * scale))
        out = {}
        for k in ("fill", "line", "shade"):
            p = getattr(self, k)
            if p is None or p.isEmpty():
                out[k] = np.zeros((H, W), np.float32)
                continue
            surf = skia.Surface(W, H)
            c = surf.getCanvas()
            c.clear(skia.Color(0, 0, 0, 0))
            c.scale(scale, scale)
            c.drawPath(p, skia.Paint(Color=skia.ColorWHITE, AntiAlias=True))
            a = surf.makeImageSnapshot().toarray()[:, :, 3].astype(np.float32) / 255.0
            out[k] = a
        out["shade"] = out["shade"] * out["fill"]
        return out

    def rgba(self, scale=1.0):
        m = self.masks(scale)
        a = np.maximum(m["fill"], m["line"])
        img = np.stack([m["fill"], m["line"], m["shade"], a], -1)
        return (np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)


def compose(masks, c1, c2, filled=True, shade_k=0.45, bg=None):
    """A decal as it looks in colours c1 (fill) and c2 (line), RGBA float. filled=False: line only."""
    f = masks["fill"] if filled else np.zeros_like(masks["fill"])
    l = masks["line"]
    s = masks["shade"] if filled else np.zeros_like(f)
    c1 = np.array(c1, np.float32)
    c2 = np.array(c2, np.float32)
    fillc = c1[None, None, :] * (1 - shade_k * s[..., None])
    col = fillc * f[..., None]
    a = f.copy()
    col = col * (1 - l[..., None]) + c2[None, None, :] * l[..., None]
    a = a * (1 - l) + l
    if bg is not None:
        bg = np.broadcast_to(np.array(bg, np.float32), col.shape).copy() if np.ndim(bg) == 1 else bg
        col = col + bg * (1 - a[..., None])
        a = np.ones_like(a)
    else:
        col = np.where(a[..., None] > 1e-4, col / np.maximum(a[..., None], 1e-4), 0)
    return np.concatenate([col, a[..., None]], -1)


def painter(pieces, w):
    """Fill and ink for shapes laid back to front: each one's outline hidden where a later one
    covers it (feathers, petals, scales)."""
    fill = union(*pieces)
    lines = []
    for i, p in enumerate(pieces):
        later = union(*pieces[i + 1:]) if i + 1 < len(pieces) else None
        o = outline(p, w)
        lines.append(diff(o, grow(later, -w * 0.5)) if later is not None and not later.isEmpty() else o)
    return fill, union(*lines)


def feather(root, ang, L, wd, curve=0.08):
    """A feather from root, ang degrees (0 right, 90 down), L long, wd wide, with a rounded tip."""
    a = math.radians(ang)
    dx, dy = math.cos(a), math.sin(a)
    nx, ny = -dy, dx
    def at(t, side):
        bend = curve * L * math.sin(t * math.pi)
        w = wd * (0.55 + 0.45 * math.sin(min(t, 0.999) * math.pi * 0.62)) * (1.0 if t < 0.82 else math.sqrt(max(0.0, 1 - ((t - 0.82) / 0.18) ** 2)))
        return (root[0] + dx * L * t + nx * (side * w / 2 + bend), root[1] + dy * L * t + ny * (side * w / 2 + bend))
    n = 24
    pts = [at(i / n, -1) for i in range(n + 1)] + [at(i / n, 1) for i in range(n, -1, -1)]
    return poly(pts)
