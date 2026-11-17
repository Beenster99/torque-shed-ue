"""Makes the decal library (Data/Decals) and its review sheets.

    python3 Tools/decals/make_decals.py                 # everything
    python3 Tools/decals/make_decals.py --shelf shapes  # one shelf
    python3 Tools/decals/make_decals.py --sheets DIR    # also the review sheets, into DIR

Each decal's picture is Data/Decals/<shelf>/<id>.png (R fill, G line, B shade, A cover; see dk.py),
and Data/Decals/decals.json lists them all (id, shelf, name, tags, size, colours). Lettering isn't
pictures: the game letters what's typed in one of the faces in decals.json's "fonts".
Needs: pip install skia-python numpy pillow (and libegl1 on Linux).
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "Data", "Decals")

import dk  # noqa: E402
import fonts  # noqa: E402

SHELVES = {}


def load_shelves():
    import shapes
    SHELVES["shapes"] = [f() for f in shapes.ALL]
    try:
        import flash
        SHELVES["art"] = [f() for f in flash.ALL]
    except ImportError:
        pass


def write_library(only=None):
    listing = []
    for shelf, decals in SHELVES.items():
        d = os.path.join(OUT, shelf)
        os.makedirs(d, exist_ok=True)
        for dc in decals:
            listing.append({"id": dc.id, "shelf": dc.shelf, "name": dc.name, "tags": dc.tags, "w": dc.w, "h": dc.h,
                            "colours": dc.colours, "file": f"{shelf}/{dc.id}.png"})
            if only and shelf != only:
                continue
            # The game's picture: at most 512 across its longer side... long stripes keep 1024.
            img = dc.rgba(1.0)
            Image.fromarray(img, "RGBA").save(os.path.join(d, dc.id + ".png"), optimize=True)
    data = {"version": 1, "fonts": fonts.FONTS, "decals": listing}
    with open(os.path.join(OUT, "decals.json"), "w") as f:
        json.dump(data, f, indent=1)
    print(f"{len(listing)} decals, {len(fonts.FONTS)} lettering faces -> {OUT}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shelf")
    ap.add_argument("--sheets")
    a = ap.parse_args()
    load_shelves()
    write_library(a.shelf)
    if a.sheets:
        import sheets
        sheets.make_all(SHELVES, a.sheets)


if __name__ == "__main__":
    main()
