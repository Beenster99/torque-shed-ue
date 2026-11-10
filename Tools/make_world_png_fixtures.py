#!/usr/bin/env python3
"""Writes the small PNGs SimTest/world_test reads to check the game's own PNG reader (the AWS height
tiles are PNGs): one of each kind the reader takes, every row filtered a different way, with the
hash of the pixels each should give in png_check.json. Run it again only if the reader takes more kinds."""
import hashlib, json, os, struct, zlib

OUT = os.path.join(os.path.dirname(__file__), "..", "SimTest", "data", "world", "png")


def chunk(tag, body):
    return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def filtered(rows, bpp):
    out = bytearray()
    prev = bytes(len(rows[0]))
    for y, row in enumerate(rows):
        f = y % 5
        out.append(f)
        for x, v in enumerate(row):
            a = row[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            pred = [0, a, b, (a + b) // 2, paeth(a, b, c)][f]
            out.append((v - pred) & 255)
        prev = row
    return bytes(out)


def write(name, w, h, ctype, rows, rgb, palette=None, split=1):
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    data = zlib.compress(filtered(rows, bpp), 9)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
    if palette:
        png += chunk(b"PLTE", palette)
    step = max(1, len(data) // split)
    for i in range(0, len(data), step):
        png += chunk(b"IDAT", data[i:i + step])
    png += chunk(b"IEND", b"")
    open(os.path.join(OUT, name), "wb").write(png)
    return {"file": name, "width": w, "height": h, "rgb": hashlib.sha256(rgb).hexdigest()}


def main():
    os.makedirs(OUT, exist_ok=True)
    w = h = 64
    seed = 12345
    px = []
    for i in range(w * h):
        seed = (seed * 1103515245 + 12345) % 2147483648
        # (a height-like picture: smooth with a little noise, as a Terrarium tile is)
        x, y = i % w, i // w
        px.append(((128 + (x * y) // 97) & 255, (x * 3 + y * 5 + (seed >> 20) % 7) & 255, (seed >> 12) & 255))
    rgb = bytes(v for p in px for v in p)
    checks = []
    checks.append(write("rgb.png", w, h, 2, [bytes(v for p in px[y * w:(y + 1) * w] for v in p) for y in range(h)], rgb, split=3))
    checks.append(write("rgba.png", w, h, 6, [bytes(v for p in px[y * w:(y + 1) * w] for v in (p[0], p[1], p[2], 200)) for y in range(h)], rgb))
    grey = bytes(p[1] for p in px)
    checks.append(write("grey.png", w, h, 0, [grey[y * w:(y + 1) * w] for y in range(h)], bytes(v for g in grey for v in (g, g, g))))
    pal = bytes((i * 7 & 255, 255 - i, i * 3 & 255)[k] for i in range(256) for k in range(3))
    checks.append(write("palette.png", w, h, 3, [grey[y * w:(y + 1) * w] for y in range(h)], bytes(pal[g * 3 + k] for g in grey for k in range(3)), palette=pal))
    json.dump(checks, open(os.path.join(OUT, "png_check.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
