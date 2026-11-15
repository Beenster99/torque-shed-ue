"""Every letter a British road sign can need, as flat triangles: Data/RideUK/sign_glyphs.json (OSM stage 6A).

    pip install fonttools mapbox_earcut
    python3 Tools/make_sign_glyphs.py path/to/NotoSans-Bold.ttf

Ported from the Godot game's tools/sign_glyphs.py, which baked whole words. This one bakes single letters, with
each one's width, so the game can set any name (World/WorldRideSigns.cpp lays them out). Its letters: ASCII, the
Latin-1 and Latin Extended-A accented letters (Welsh's circumflexes, Gaelic's graves, Irish and Cornish names
besides) and a few marks signs use (the pound, the curly apostrophe, dashes, the fractions on distances).

The font is Noto Sans Bold (SIL Open Font License 1.1; fonts.google.com, or github.com/google/fonts ofl/notosans).
Outlines made from it stay under the OFL: see Data/RideUK/sign_glyphs.licence.txt.

A letter's points are in em (the font's size): x from 0 at its start, y up from the baseline; "w" its advance.
The file's "cap" is a capital's height and "xh" a small letter's, both in em.
"""
import json
import os
import sys

import mapbox_earcut
import numpy
from fontTools.pens.basePen import BasePen
from fontTools.ttLib import TTFont

ROOT = os.path.join(os.path.dirname(__file__), "..")
OUT = os.path.join(ROOT, "Data", "RideUK", "sign_glyphs.json")
FORMAT = 1
# Curves are cut into straight pieces at most this far (in em) from the curve: at a letter 0.25 m high, about 3 mm.
TOLERANCE = 0.012

LETTERS = (
    [chr(c) for c in range(0x21, 0x7F)]
    + [chr(c) for c in range(0xC0, 0x180) if c not in (0xD7, 0xF7)]
    + list("£’‘“”–—…½¼¾°·")
)


class FlatPen(BasePen):
    """A glyph's outline as closed polygons, its curves cut into straight pieces."""

    def __init__(self, glyphset, step):
        super().__init__(glyphset)
        self.step = step
        self.contours = []
        self.cur = []

    def _moveTo(self, p):
        self.cur = [p]

    def _lineTo(self, p):
        self.cur.append(p)

    def _curveToOne(self, p1, p2, p3):
        p0 = self.cur[-1]
        n = self._pieces(p0, p3, p1, p2)
        for k in range(1, n + 1):
            t = k / n
            u = 1 - t
            self.cur.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                             u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))

    def _qCurveToOne(self, p1, p2):
        p0 = self.cur[-1]
        n = self._pieces(p0, p2, p1)
        for k in range(1, n + 1):
            t = k / n
            u = 1 - t
            self.cur.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
                             u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))

    def _pieces(self, a, b, *ctrl):
        # How far the control points stand off the chord says how many pieces keep within the tolerance.
        dev = 0.0
        for c in ctrl:
            dx, dy = b[0] - a[0], b[1] - a[1]
            length = (dx * dx + dy * dy) ** 0.5 or 1.0
            dev = max(dev, abs((c[0] - a[0]) * dy - (c[1] - a[1]) * dx) / length)
        return max(1, min(12, int((dev / self.step) ** 0.5 * 1.5) + 1))

    def _closePath(self):
        if len(self.cur) > 2:
            if self.cur[0] == self.cur[-1]:
                self.cur.pop()
            self.contours.append(self.cur)
        self.cur = []

    _endPath = _closePath


def area(c):
    return 0.5 * sum(c[k][0] * c[(k + 1) % len(c)][1] - c[(k + 1) % len(c)][0] * c[k][1] for k in range(len(c)))


def inside(p, c):
    x, y = p
    hit = False
    for k in range(len(c)):
        x0, y0 = c[k]
        x1, y1 = c[(k + 1) % len(c)]
        if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
            hit = not hit
    return hit


def triangles(contours):
    """The filled outline as (points, indices): outer contours wind one way, holes the other."""
    contours = [c for c in contours if abs(area(c)) > 1e-9]
    if not contours:
        return [], []
    sign = 1 if area(max(contours, key=lambda c: abs(area(c)))) > 0 else -1
    outers = [c for c in contours if area(c) * sign > 0]
    holes = [c for c in contours if area(c) * sign < 0]
    groups = {id(o): [] for o in outers}
    for h in holes:
        best = None
        for o in outers:
            if inside(h[0], o) and (best is None or abs(area(o)) < abs(area(best))):
                best = o
        if best is not None:
            groups[id(best)].append(h)
    points, index = [], []
    for o in outers:
        rings = [o] + groups[id(o)]
        verts = numpy.array([p for r in rings for p in r], dtype=numpy.float64).reshape(-1, 2)
        ends = numpy.cumsum([len(r) for r in rings]).astype(numpy.uint32)
        base = len(points) // 2
        for v in verts:
            points.extend([round(float(v[0]), 4), round(float(v[1]), 4)])
        index.extend(int(i) + base for i in mapbox_earcut.triangulate_float64(verts, ends))
    return points, index


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    tt = TTFont(sys.argv[1])
    upem = tt["head"].unitsPerEm
    cmap = tt.getBestCmap()
    glyphs = tt.getGlyphSet()
    hmtx = tt["hmtx"]
    out = {"format": FORMAT, "font": "Noto Sans Bold", "licence": "SIL Open Font License 1.1 (see sign_glyphs.licence.txt)",
           "cap": round(tt["OS/2"].sCapHeight / upem, 4), "xh": round(tt["OS/2"].sxHeight / upem, 4),
           "space": round(hmtx[cmap[0x20]][0] / upem, 4), "glyphs": {}}
    missing = []
    for ch in LETTERS:
        name = cmap.get(ord(ch))
        if name is None:
            missing.append(ch)
            continue
        pen = FlatPen(glyphs, TOLERANCE * upem)
        glyphs[name].draw(pen)
        points, index = triangles([[(x / upem, y / upem) for x, y in c] for c in pen.contours])
        out["glyphs"][ch] = {"w": round(hmtx[name][0] / upem, 4), "v": points, "i": index}
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, separators=(",", ":"))
    tris = sum(len(g["i"]) // 3 for g in out["glyphs"].values())
    print("%d letters (%d triangles, %d bytes) to %s; missing: %s" % (len(out["glyphs"]), tris, os.path.getsize(OUT), OUT, "".join(missing) or "none"))


if __name__ == "__main__":
    main()
