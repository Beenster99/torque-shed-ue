#!/usr/bin/env python3
"""The three bins' labels (beenster, Nov 9 2026: "Bins need labels").

Draws Data/Props/Textures/bin_label_<kind>.png (a printed sticker: a band in the bin's colour with
what goes in it, a symbol, a line of small print) and fits each bin model's label plate to it:
Data/Props/Blender/bin_<kind>.glb's 'label_white' plate becomes material 'bin_label_<kind>'
(Data/Props/materials.json, printed), with 0..1 UVs over its face, laid flat on the bin's sloping
front instead of standing a little proud at the top and buried at the bottom.

make_props.py's _wheelie_bin draws the same plate the same way, so a Blender rebuild matches; this
patches the committed glbs in place (re-running redraws the labels and leaves a patched glb be).
Needs Pillow; deterministic.

    python3 Tools/props/bin_labels.py
"""
import json, math, os, struct
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
W, H = 1024, 634   # the plate is 0.42 x 0.26 m
BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
BOOK = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

# kind: (band colour, title, small print, symbol)
BINS = {
    "general": ((38, 38, 40), "GENERAL WASTE", "No recycling, no oil, no batteries", "bin"),
    "plastic": ((28, 78, 160), "PLASTIC", "Bottles, tubs, wrap and packaging", "recycle"),
    "metal": ((30, 120, 52), "METAL", "Scrap metal, cans and old parts", "recycle"),
}

# The bin's body (make_props.py _wheelie_bin): 0.6 deep, its front leaning out 5 cm from y 0.05 to 0.85.
D, H_BIN = 0.6, 0.9
LEAN = math.atan2(0.05, H_BIN - 0.1)


def front_z(y):
    return D / 2 - 0.05 + 0.05 * (y - 0.05) / (H_BIN - 0.1)


def arrow_ring(d, cx, cy, r, ink, width):
    """Three chasing arrows round a triangle (the recycling mark)."""
    pts = [(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))) for a in (-90, 30, 150)]
    for i in range(3):
        a, b = pts[i], pts[(i + 1) % 3]
        # Each arrow runs from 12% to 72% along its side, a head at the end.
        s = (a[0] + (b[0] - a[0]) * 0.12, a[1] + (b[1] - a[1]) * 0.12)
        e = (a[0] + (b[0] - a[0]) * 0.70, a[1] + (b[1] - a[1]) * 0.70)
        d.line([s, e], fill=ink, width=width)
        ang = math.atan2(b[1] - a[1], b[0] - a[0])
        tip = (a[0] + (b[0] - a[0]) * 0.86, a[1] + (b[1] - a[1]) * 0.86)
        hw = width * 1.25
        left = (e[0] + hw * math.cos(ang + math.pi / 2), e[1] + hw * math.sin(ang + math.pi / 2))
        right = (e[0] + hw * math.cos(ang - math.pi / 2), e[1] + hw * math.sin(ang - math.pi / 2))
        d.polygon([tip, left, right], fill=ink)


def bin_mark(d, cx, cy, s, ink, width):
    """A wheelie bin outline."""
    d.rectangle([cx - s * 0.62, cy - s * 0.78, cx + s * 0.62, cy - s * 0.62], fill=ink)
    d.polygon([(cx - s * 0.5, cy - s * 0.55), (cx + s * 0.5, cy - s * 0.55), (cx + s * 0.42, cy + s * 0.8),
               (cx - s * 0.42, cy + s * 0.8)], outline=ink, width=width)
    for k in (-1, 0, 1):
        d.line([(cx + k * s * 0.2, cy - s * 0.35), (cx + k * s * 0.17, cy + s * 0.6)], fill=ink, width=max(2, width // 2))


def draw(kind):
    band, title, small, symbol = BINS[kind]
    img = Image.new("RGB", (W, H), (240, 238, 230))
    d = ImageDraw.Draw(img)
    top = 210
    d.rectangle([0, 0, W, top], fill=band)
    size = 150
    while d.textlength(title, font=ImageFont.truetype(BOLD, size)) > W - 80 and size > 60:
        size -= 4
    d.text((W / 2, top / 2 + 4), title, font=ImageFont.truetype(BOLD, size), fill=(250, 250, 248), anchor="mm")
    ink = band if kind != "general" else (30, 30, 30)
    if symbol == "recycle":
        arrow_ring(d, 190, top + 205, 120, ink, 26)
    else:
        bin_mark(d, 190, top + 205, 130, ink, 18)
    d.text((370, top + 120), "THIS BIN" if kind == "general" else "RECYCLING", font=ImageFont.truetype(BOLD, 64), fill=(30, 30, 30), anchor="lm")
    d.text((370, top + 205), "ONLY" if kind != "general" else "EVERYTHING ELSE", font=ImageFont.truetype(BOLD, 64 if kind != "general" else 50),
           fill=(30, 30, 30), anchor="lm")
    d.text((370, top + 290), small, font=ImageFont.truetype(BOOK, 34), fill=(70, 70, 70), anchor="lm")
    d.text((W - 30, H - 24), "TORQUE SHED", font=ImageFont.truetype(BOLD, 26), fill=(150, 148, 140), anchor="rs")
    d.rectangle([0, 0, W - 1, H - 1], outline=(205, 203, 195), width=6)
    out = os.path.join(ROOT, "Data", "Props", "Textures", "bin_label_%s.png" % kind)
    img.save(out, optimize=True)
    return out


def read_glb(path):
    f = open(path, "rb").read()
    magic, ver, total = struct.unpack("<III", f[:12])
    jl, jt = struct.unpack("<II", f[12:20])
    j = json.loads(f[20:20 + jl])
    o = 20 + jl
    bl, bt = struct.unpack("<II", f[o:o + 8])
    return j, bytearray(f[o + 8:o + 8 + bl])


def write_glb(path, j, bin_):
    js = json.dumps(j, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    bin_ = bytes(bin_) + b"\0" * ((4 - len(bin_) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(bin_)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(bin_), 0x004E4942) + bin_)


def view(j, bin_, acc, comps):
    a = j["accessors"][acc]
    bv = j["bufferViews"][a["bufferView"]]
    start = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride", 4 * comps)
    assert a["componentType"] == 5126
    return a, start, stride


def patch(kind):
    path = os.path.join(ROOT, "Data", "Props", "Blender", "bin_%s.glb" % kind)
    j, b = read_glb(path)
    key = "bin_label_" + kind
    mats = [m["name"] for m in j["materials"]]
    if key in mats:
        return path, 0   # done already
    mi = mats.index("label_white")
    j["materials"][mi]["name"] = key
    prim = [p for m in j["meshes"] for p in m["primitives"] if p["material"] == mi][0]
    pa, ps, pst = view(j, b, prim["attributes"]["POSITION"], 3)
    na, ns, nst = view(j, b, prim["attributes"]["NORMAL"], 3)
    ta, ts, tst = view(j, b, prim["attributes"]["TEXCOORD_0"], 2)
    n = pa["count"]
    P = [list(struct.unpack_from("<3f", b, ps + i * pst)) for i in range(n)]
    xs, ys = [p[0] for p in P], [p[1] for p in P]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    yc = (y0 + y1) / 2
    zs = [p[2] for p in P]
    z1 = max(zs)
    thick = z1 - min(zs)
    # Lay it on the front: its back 0.4 mm off the body at every height (the front leans out).
    sh = front_z(yc) + 0.0004 + thick - z1
    ca, sa = math.cos(LEAN), math.sin(LEAN)
    mn, mx = [1e9] * 3, [-1e9] * 3
    for i, p in enumerate(P):
        x, y, z = p[0], p[1], zs[i] + sh + (p[1] - yc) * math.tan(LEAN)
        struct.pack_into("<3f", b, ps + i * pst, x, y, z)
        for k, v in enumerate((x, y, z)):
            mn[k], mx[k] = min(mn[k], v), max(mx[k], v)
        nx, ny, nz = struct.unpack_from("<3f", b, ns + i * nst)
        if abs(nz) > 0.9:   # the front and back faces tip with the lean
            ny, nz = -sa * (1 if nz > 0 else -1), ca * (1 if nz > 0 else -1)
            struct.pack_into("<3f", b, ns + i * nst, nx, ny, nz)
        # 0..1 over the face, v down from the top (glTF's UV origin is the image's top left).
        struct.pack_into("<2f", b, ts + i * tst, (x - x0) / (x1 - x0), (y1 - y) / (y1 - y0))
    pa["min"], pa["max"] = mn, mx
    if "min" in ta:
        ta["min"], ta["max"] = [0.0, 0.0], [1.0, 1.0]
    write_glb(path, j, b)
    return path, n


def main():
    for kind in BINS:
        print("wrote", draw(kind))
        print("patched %s (%d label verts)" % patch(kind))


if __name__ == "__main__":
    main()
