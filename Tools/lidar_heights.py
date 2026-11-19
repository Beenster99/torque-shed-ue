"""LiDAR building heights (LiDAR plan stage 1): for each building outline in a cell's .bld.zip tiles
inside a lat/lon box, the median of (surface - ground) over the outline from the Environment Agency's
1 m LiDAR composites (DSM first return minus DTM), fetched over WCS in 1 km chunks.

python lidar_heights.py CELLDIR south west north east out.json
"""
import json, sys, os, glob, io, urllib.request, time
import numpy as np, tifffile
from pyproj import Transformer
from matplotlib.path import Path

DTM = ("https://environment.data.gov.uk/spatialdata/lidar-composite-digital-terrain-model-dtm-1m/wcs",
       "13787b9a-26a4-4775-8523-806d13af58fc__Lidar_Composite_Elevation_DTM_1m")
DSM = ("https://environment.data.gov.uk/spatialdata/lidar-composite-digital-surface-model-first-return-dsm-1m/wcs",
       "df4e3ec3-315e-48aa-aaaf-b5ae74d7b2bb__Lidar_Composite_Elevation_FZ_DSM_1m")
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cache")
CHUNK = 1000

def fetch(src, e0, n0):
    os.makedirs(CACHE, exist_ok=True)
    name = os.path.join(CACHE, "%s_%d_%d.tif" % ("dsm" if src is DSM else "dtm", e0, n0))
    if not os.path.exists(name):
        url = ("%s?service=WCS&version=2.0.1&request=GetCoverage&CoverageId=%s&format=image/tiff"
               "&subset=E(%d,%d)&subset=N(%d,%d)") % (src[0], src[1], e0, e0 + CHUNK, n0, n0 + CHUNK)
        for t in range(4):
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "TorqueShed-lidar-baker"})
                data = urllib.request.urlopen(req, timeout=120).read()
                break
            except Exception as ex:
                print("retry", ex); time.sleep(2 * (t + 1))
        else:
            return None
        open(name, "wb").write(data)
    try:
        a = tifffile.imread(name).astype(np.float32)
    except Exception:
        return None
    a[(a < -100) | (a > 2000)] = np.nan
    return a   # row 0 = north edge (n0 + CHUNK), col 0 = west edge

def main():
    celldir, s, w, n, e, out = sys.argv[1], *map(float, sys.argv[2:6]), sys.argv[6]
    head = json.load(open(os.path.join(celldir, "buildings.json")))
    words = head["words"]
    tr = Transformer.from_crs("EPSG:4326", "EPSG:27700", always_xy=True)
    rows = []
    for f in sorted(glob.glob(os.path.join(celldir, "t*.json"))):
        tile = os.path.basename(f)[1:-5]
        for i, r in enumerate(json.load(open(f))):
            if len(r) < 15: continue
            lat, lon = r[9], r[10]; ring = [(lat, lon)]
            for k in range(11, len(r) - 1, 2):
                lat += r[k]; lon += r[k + 1]; ring.append((lat, lon))
            la = np.array([p[0] for p in ring]) * 1e-6; lo = np.array([p[1] for p in ring]) * 1e-6
            if not (s <= la.mean() <= n and w <= lo.mean() <= e): continue
            E, N = tr.transform(lo, la)
            rows.append(dict(tile=tile, i=i, dm=r[0], floors=r[2], cls=words[r[3]] if 0 < r[3] < len(words) else "",
                             E=np.array(E), N=np.array(N)))
    # The cell's own boxes over 2500 m2 (the outlines zip leaves those out): CELLBOXES=the cell's buildings.json.
    if os.environ.get("CELLBOXES"):
        import math
        reg = json.load(open(os.path.join(os.path.dirname(os.environ["CELLBOXES"]), "region.json")))
        lat0, lon0 = reg["frame"]["lat"], reg["frame"]["lon"]; f = math.radians(lat0)
        mlat = 111132.954 - 559.822 * math.cos(2 * f) + 1.175 * math.cos(4 * f)
        mlon = 111412.84 * math.cos(f) - 93.5 * math.cos(3 * f) + 0.118 * math.cos(5 * f)
        for i, b in enumerate(json.load(open(os.environ["CELLBOXES"]))):
            x, z, W, D, yaw, h = b[:6]
            if W * D < 2500: continue
            c, sn = math.cos(yaw), math.sin(yaw); pts = []
            for u, v in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                px = x + u * W / 2 * c + v * D / 2 * sn; pz = z - u * W / 2 * sn + v * D / 2 * c
                pts.append((lat0 - pz / mlat, lon0 + px / mlon))
            la = np.array([p[0] for p in pts]); lo = np.array([p[1] for p in pts])
            if not (s <= la.mean() <= n and w <= lo.mean() <= e): continue
            E, N = tr.transform(lo, la)
            rows.append(dict(tile="box", i=i, dm=h * 10, floors=-1, cls="box %d" % b[6] if len(b) > 6 else "box", E=np.array(E), N=np.array(N)))
    print(len(rows), "outlines in the box")
    chunks = {}
    for b in rows:
        b["c"] = (int(b["E"].mean() // CHUNK * CHUNK), int(b["N"].mean() // CHUNK * CHUNK))
        chunks.setdefault(b["c"], []).append(b)
    res = []
    for (e0, n0), bs in sorted(chunks.items()):
        # (a building that crosses the chunk edge: the chunks round it read too)
        big_s = np.full((3 * CHUNK, 3 * CHUNK), np.nan, np.float32); big_t = big_s.copy()
        need = set()
        for b in bs:
            for E, N in zip(b["E"], b["N"]):
                need.add((int(E // CHUNK * CHUNK), int(N // CHUNK * CHUNK)))
        for (ce, cn) in need:
            de, dn = (ce - e0) // CHUNK, (cn - n0) // CHUNK
            if abs(de) > 1 or abs(dn) > 1: continue
            a, t = fetch(DSM, ce, cn), fetch(DTM, ce, cn)
            if a is None or t is None or a.shape != (CHUNK, CHUNK) or t.shape != (CHUNK, CHUNK): continue
            r0 = (1 - dn) * CHUNK; c0 = (de + 1) * CHUNK
            big_s[r0:r0 + CHUNK, c0:c0 + CHUNK] = a; big_t[r0:r0 + CHUNK, c0:c0 + CHUNK] = t
        west, north = e0 - CHUNK, n0 + 2 * CHUNK
        for b in bs:
            x0, x1 = int(b["E"].min() - west) - 1, int(b["E"].max() - west) + 2
            y0, y1 = int(north - b["N"].max()) - 1, int(north - b["N"].min()) + 2
            if x0 < 0 or y0 < 0 or x1 > 3 * CHUNK or y1 > 3 * CHUNK: continue
            xs, ys = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
            pts = np.c_[(west + xs).ravel(), (north - ys).ravel()]
            path = Path(np.c_[b["E"], b["N"]])
            inside = path.contains_points(pts, radius=-1.0).reshape(xs.shape)   # (a metre in from the walls)
            if inside.sum() < 4: inside = path.contains_points(pts).reshape(xs.shape)
            h = (big_s[y0:y1, x0:x1] - big_t[y0:y1, x0:x1])[inside]
            h = h[np.isfinite(h)]
            area = 0.5 * abs(np.dot(b["E"], np.roll(b["N"], 1)) - np.dot(b["N"], np.roll(b["E"], 1)))
            row = dict(tile=b["tile"], i=b["i"], cls=b["cls"], mapped=b["dm"] * 0.1 if b["dm"] >= 0 else None,
                       floors=b["floors"] or None, area=round(float(area), 1), px=int(h.size))
            if h.size >= 4:
                row.update(med=round(float(np.median(h)), 2), p90=round(float(np.percentile(h, 90)), 2),
                           top=round(float(h.max()), 2), low=round(float(np.percentile(h, 10)), 2))
            res.append(row)
    json.dump(res, open(out, "w"))
    print(len(res), "measured;", sum(1 for r in res if "med" in r), "with LiDAR pixels")

main()
