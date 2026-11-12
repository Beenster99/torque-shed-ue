"""Data/World/uk_places.tsv: the places the Route planner's search finds in the UK (Ride UK stage 4).

From GeoNames' Great Britain dump (https://download.geonames.org/export/dump/GB.zip, CC BY 4.0,
credited on the map data credits page): every city, town, village and hamlet, and the hills,
mountains, passes, lakes, castles, lighthouses and stations with a name, inside the UK's box (the
dump also has the bases on Cyprus). One row each, the most important first:

    name <TAB> lat <TAB> lon <TAB> kind <TAB> population

kind is city, town, village, hamlet, peak, pass, hill, lake, castle, lighthouse or station.

    python Tools/make_uk_places.py [GB.zip or GB.txt]

With no file it downloads GB.zip (3.6 MB) into a temporary folder.
"""
import io
import os
import sys
import tempfile
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "Data", "World", "uk_places.tsv")
URL = "https://download.geonames.org/export/dump/GB.zip"

# GeoNames feature (class, code) -> our kind; populated places by code and population below.
KINDS = {("T", "MT"): "peak", ("T", "PK"): "peak", ("T", "PKS"): "peak", ("T", "PASS"): "pass", ("T", "HLL"): "hill",
         ("H", "LK"): "lake", ("H", "RSV"): "lake", ("S", "CSTL"): "castle", ("S", "LTHSE"): "lighthouse",
         ("S", "RSTN"): "station"}
# The order kinds come in when nothing else tells them apart (lower first).
RANK = {"city": 0, "town": 1, "peak": 2, "pass": 2, "village": 3, "lake": 4, "castle": 4, "lighthouse": 5, "station": 6,
        "hamlet": 7, "hill": 8}


def read_rows(path):
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            return z.read("GB.txt").decode("utf-8").splitlines()
    with open(path, encoding="utf-8") as f:
        return f.read().splitlines()


def kind_of(cls, code, pop):
    if cls == "P":
        if code in ("PPLC", "PPLA", "PPLA2") or pop >= 100000:
            return "city"
        if code == "PPLX":
            return None   # (a part of a town: the town is enough)
        if pop >= 5000 or code == "PPLA3":
            return "town"
        if code == "PPLL" or (0 < pop < 200):
            return "hamlet"
        return "village"
    return KINDS.get((cls, code))


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else None
    if src is None:
        src = os.path.join(tempfile.mkdtemp(), "GB.zip")
        print("downloading", URL)
        urllib.request.urlretrieve(URL, src)
    rows = []
    seen = set()
    for line in read_rows(src):
        f = line.split("\t")
        if len(f) < 15:
            continue
        name, lat, lon, cls, code = f[1].strip(), float(f[4]), float(f[5]), f[6], f[7]
        pop = int(f[14] or 0)
        if not name or not (49.8 <= lat <= 61.0 and -8.7 <= lon <= 2.0):
            continue
        kind = kind_of(cls, code, pop)
        if kind is None:
            continue
        key = (name.lower(), kind, round(lat, 2), round(lon, 2))
        if key in seen:
            continue
        seen.add(key)
        rows.append((name.replace("\t", " "), lat, lon, kind, pop))
    rows.sort(key=lambda r: (-r[4], RANK[r[3]], r[0]))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with io.open(OUT, "w", encoding="utf-8", newline="\n") as out:
        out.write("# UK places for the Route planner's search: name, lat, lon, kind, population. From GeoNames "
                  "(geonames.org), CC BY 4.0. Made by Tools/make_uk_places.py.\n")
        for name, lat, lon, kind, pop in rows:
            out.write("%s\t%.5f\t%.5f\t%s\t%d\n" % (name, lat, lon, kind, pop))
    counts = {}
    for r in rows:
        counts[r[3]] = counts.get(r[3], 0) + 1
    print("%d places -> %s (%s)" % (len(rows), os.path.normpath(OUT), ", ".join("%s %d" % kv for kv in sorted(counts.items()))))


if __name__ == "__main__":
    main()
