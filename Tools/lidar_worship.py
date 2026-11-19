"""Places of worship's highest points from the Environment Agency's 1 m LiDAR (LiDAR plan stage 3, for
churches stage 6): for each worship landmark with a building (the cell's box nearest it, within 30 m), DSM
minus DTM over the box: its highest point, where that is, whether it is a spire (a narrow peak) or a tower
(a flat top), and the roof's median.

python lidar_worship.py WORSHIP_DIR out.json [threads]
python lidar_worship.py --file out.json Data/RideUK/church_heights.json   (the table the game reads)
WORSHIP_DIR: the per-cell files (worship points with their nearest building box, and the cell's region).
"""
import json, sys, os, glob, math, time, urllib.request, urllib.error
from concurrent.futures import ThreadPoolExecutor
import numpy as np, tifffile, io
from pyproj import Transformer
from matplotlib.path import Path

DTM = ("https://environment.data.gov.uk/spatialdata/lidar-composite-digital-terrain-model-dtm-1m/wcs",
       "13787b9a-26a4-4775-8523-806d13af58fc__Lidar_Composite_Elevation_DTM_1m")
DSM = ("https://environment.data.gov.uk/spatialdata/lidar-composite-digital-surface-model-first-return-dsm-1m/wcs",
       "df4e3ec3-315e-48aa-aaaf-b5ae74d7b2bb__Lidar_Composite_Elevation_FZ_DSM_1m")
TO = Transformer.from_crs("EPSG:4326", "EPSG:27700", always_xy=True)
PAUSE = float(os.environ.get("LIDAR_PAUSE", "0.5"))
BACK = Transformer.from_crs("EPSG:27700", "EPSG:4326", always_xy=True)

def get(src, e0, n0, e1, n1):
    url = ("%s?service=WCS&version=2.0.1&request=GetCoverage&CoverageId=%s&format=image/tiff"
           "&subset=E(%d,%d)&subset=N(%d,%d)") % (src[0], src[1], e0, e1, n0, n1)
    # (gently: the service turns away a burst with 429, so a turned-away ask waits a minute or more)
    for t in range(6):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "TorqueShed-lidar-baker"})
            a = tifffile.imread(io.BytesIO(urllib.request.urlopen(req, timeout=60).read())).astype(np.float32)
            a[(a < -100) | (a > 2000)] = np.nan
            time.sleep(PAUSE)
            return a
        except urllib.error.HTTPError as ex:
            time.sleep(60 * (t + 1) if ex.code == 429 else 3)
        except Exception:
            time.sleep(3 * (t + 1))
    return None

def frame(lat0):
    f = math.radians(lat0)
    return (111132.954 - 559.822 * math.cos(2 * f) + 1.175 * math.cos(4 * f),
            111412.84 * math.cos(f) - 93.5 * math.cos(3 * f) + 0.118 * math.cos(5 * f))

def one(site):
    lat0, lon0, p, b = site
    mlat, mlon = frame(lat0)
    x, z, W, D, yaw = b[:5]
    c, s = math.cos(yaw), math.sin(yaw)
    corners = []
    for u, v in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        px = x + u * W / 2 * c + v * D / 2 * s; pz = z - u * W / 2 * s + v * D / 2 * c
        corners.append((lon0 + px / mlon, lat0 - pz / mlat))
    E, N = TO.transform([q[0] for q in corners], [q[1] for q in corners])
    e0, e1, n0, n1 = int(min(E)) - 2, int(max(E)) + 3, int(min(N)) - 2, int(max(N)) + 3
    a, t = get(DSM, e0, n0, e1, n1), get(DTM, e0, n0, e1, n1)
    plat, plon = lat0 - p["z"] / mlat, lon0 + p["x"] / mlon
    row = {"lat": round(plat, 6), "lon": round(plon, 6), "name": p.get("name", "")}
    if a is None or t is None or a.shape != t.shape or a.size == 0: return row
    h = a - t
    ys, xs = np.mgrid[0:h.shape[0], 0:h.shape[1]]
    pe, pn = e0 + xs + 0.5, n1 - ys - 0.5
    inside = Path(np.c_[E, N]).contains_points(np.c_[pe.ravel(), pn.ravel()], radius=-0.5).reshape(h.shape)
    if inside.sum() < 6: inside = Path(np.c_[E, N]).contains_points(np.c_[pe.ravel(), pn.ravel()]).reshape(h.shape)
    v = np.where(inside & np.isfinite(h), h, np.nan)
    if np.isfinite(v).sum() < 6: return row
    top = float(np.nanmax(v)); k = int(np.nanargmax(v))
    near = np.isfinite(v) & (v > top - 3.0)
    lo, la = BACK.transform(float(pe.ravel()[k]), float(pn.ravel()[k]))
    row.update(top=round(top, 1), roof=round(float(np.nanmedian(v)), 1), near=int(near.sum()), px=int(np.isfinite(v).sum()),
               tlat=round(la, 6), tlon=round(lo, 6), mapped=b[5], area=round(W * D, 1))
    return row

def main():
    sites = []
    for f in sorted(glob.glob(os.path.join(sys.argv[1], "*.json"))):
        d = json.load(open(f))
        fr = d.get("region", {}).get("frame")
        if not fr: continue
        for p in d["worship"]:
            if p.get("bld"): sites.append((fr["lat"], fr["lon"], p, p["bld"]))
    # England only (the EA's survey); a site outside it comes back unmeasured
    print(len(sites), "sites with a building", flush=True)
    out = sys.argv[2]; done = {}
    if os.path.exists(out):
        for r in json.load(open(out)): done[(r["lat"], r["lon"])] = r
    todo = [s for s in sites if (round(s[0] - s[2]["z"] / frame(s[0])[0], 6), round(s[1] + s[2]["x"] / frame(s[0])[1], 6)) not in done]
    print(len(todo), "to measure", flush=True)
    res = list(done.values()); t0 = time.time()
    with ThreadPoolExecutor(int(sys.argv[3]) if len(sys.argv) > 3 else 12) as ex:
        for i, r in enumerate(ex.map(one, todo)):
            res.append(r)
            if i % 500 == 499:
                json.dump(res, open(out, "w")); print(i + 1, "in", int(time.time() - t0), "s", flush=True)
    json.dump(res, open(out, "w")); print("DONE", len(res), sum(1 for r in res if "top" in r), "measured")

def write_file(src, dst):
    sites, seen = [], set()
    for r in json.load(open(src)):
        if "top" not in r or r["px"] < 6: continue
        key = (round(r["lat"] * 1e5), round(r["lon"] * 1e5))
        if key in seen: continue
        seen.add(key)
        sites.append([key[0], key[1], int(round(r["top"] * 10)), int(round(r["roof"] * 10)), r["near"],
                      int(round((r["tlat"] - r["lat"]) * 1e5)), int(round((r["tlon"] - r["lon"]) * 1e5))])
    sites.sort()
    with open(dst, "w") as f:
        f.write('{"v":1,"about":"Places of worship: [lat, lon (1e-5 deg), highest point, roof median (dm over the ground), '
                'metres within 3 m of the top, the top point less lat, lon (1e-5 deg)]. Contains public sector information '
                'licensed under the Open Government Licence v3.0.","sites":[\n')
        f.write(",\n".join(json.dumps(x, separators=(",", ":")) for x in sites))
        f.write("\n]}\n")
    print(dst, len(sites), "sites")

if sys.argv[1] == "--file":
    write_file(sys.argv[2], sys.argv[3])
else:
    main()
