#!/usr/bin/env python3
"""Download the scanned CC0 textures named in Data/Textures/textures.json from ambientCG, 2K JPG, into
Data/Textures/<id>/ as color.jpg, normal.jpg (OpenGL), rough.jpg, ao.jpg and height.jpg. The images
aren't kept in git; run this once on the PC that builds the level.

    python Tools/fetch_textures.py                 the textures the roles use
    python Tools/fetch_textures.py --candidates    every candidate as well, to choose between
    python Tools/fetch_textures.py --res 4K        sharper (and bigger)
"""
import argparse, io, json, os, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TEX = os.path.join(os.path.dirname(HERE), "Data", "Textures")
MAPS = {"_Color": "color", "_NormalGL": "normal", "_Roughness": "rough", "_AmbientOcclusion": "ao",
        "_Displacement": "height"}
UA = {"User-Agent": "TorqueShedProto-texture-fetch/1.0"}


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as r:
        return r.read()


def zip_url(asset, res):
    """The zip's address from ambientCG's API, falling back to its usual pattern."""
    try:
        j = json.loads(get("https://ambientcg.com/api/v2/full_json?id=%s&include=downloadData" % asset))
        for a in j.get("foundAssets", []):
            for cat in a.get("downloadFolders", {}).values():
                for d in cat.get("downloadFiletypeCategories", {}).get("zip", {}).get("downloads", []):
                    if d.get("attribute") == "%s-JPG" % res:
                        return d["downloadLink"]
    except Exception as e:
        print("  api lookup failed (%s), trying the usual address" % e)
    return "https://ambientcg.com/get?file=%s_%s-JPG.zip" % (asset, res)


def fetch(asset, res):
    out = os.path.join(TEX, asset)
    if os.path.exists(os.path.join(out, "color.jpg")):
        print(asset, "already here")
        return
    url = zip_url(asset, res)
    print(asset, "<-", url)
    z = zipfile.ZipFile(io.BytesIO(get(url)))
    os.makedirs(out, exist_ok=True)
    got = []
    for n in z.namelist():
        stem = os.path.splitext(os.path.basename(n))[0]
        for suffix, name in MAPS.items():
            if stem.endswith(suffix) and n.lower().endswith((".jpg", ".png")):
                with open(os.path.join(out, name + ".jpg"), "wb") as f:
                    f.write(z.read(n))
                got.append(name)
    print("  ", ", ".join(sorted(got)))
    if "color" not in got:
        raise RuntimeError("no colour map in %s" % url)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--candidates", action="store_true")
    ap.add_argument("--res", default="2K")
    args = ap.parse_args()
    spec = json.load(open(os.path.join(TEX, "textures.json")))
    want = [r["id"] for r in spec["roles"].values()]
    if args.candidates:
        want += [c for cs in spec["candidates"].values() for c in cs]
    for a in dict.fromkeys(want):
        fetch(a, args.res)


if __name__ == "__main__":
    main()
