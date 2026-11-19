"""LiDAR stage 4: the game's ground under the roads (Mapterhorn z14 Terrarium tiles, drawn on a 4 m grid)
against the EA's 1 m DTM, along every road in a box. python road_test.py CELLDIR TILEDIR lat0 lon0 lat1 lon1"""
import json, sys, os, math, io, time, urllib.request
import numpy as np, tifffile
from PIL import Image
from pyproj import Transformer
cell, tiles, la0, lo0, la1, lo1 = sys.argv[1], sys.argv[2], *map(float, sys.argv[3:7])
reg = json.load(open(os.path.join(cell, "region.json"))); f0 = reg["frame"]; LAT0, LON0 = f0["lat"], f0["lon"]
f = math.radians(LAT0)
MLAT = 111132.954 - 559.822 * math.cos(2 * f) + 1.175 * math.cos(4 * f)
MLON = 111412.84 * math.cos(f) - 93.5 * math.cos(3 * f) + 0.118 * math.cos(5 * f)
TO = Transformer.from_crs("EPSG:4326", "EPSG:27700", always_xy=True)
roads = json.load(open(os.path.join(cell, "roads.json")))
pts = []
for e in roads["edges"]:
    p = np.array(e["p"]).reshape(-1, 2)
    for a, b in zip(p[:-1], p[1:]):
        n = max(1, int(np.hypot(*(b - a)) / 2))
        for t in np.arange(n) / n: pts.append((a + (b - a) * t, e["c"]))
xy = np.array([q[0] for q in pts]); cls = np.array([q[1] for q in pts])
lat = LAT0 - xy[:, 1] / MLAT; lon = LON0 + xy[:, 0] / MLON
keep = (lat > la0) & (lat < la1) & (lon > lo0) & (lon < lo1)
xy, cls, lat, lon = xy[keep], cls[keep], lat[keep], lon[keep]
print(len(xy), "road points every 2 m")
def tile_h(la, lo, z=14):
    n = 2 ** z; X = (lo + 180) / 360 * n; Y = (1 - math.asinh(math.tan(math.radians(la))) / math.pi) / 2 * n
    tx, ty = int(X), int(Y); img = TILE.get((tx, ty))
    if img is None:
        fn = os.path.join(tiles, str(z), str(tx), "%d.webp" % ty)
        a = np.asarray(Image.open(fn).convert("RGB")).astype(np.float64)
        img = TILE[(tx, ty)] = a[:, :, 0] * 256 + a[:, :, 1] + a[:, :, 2] / 256 - 32768
    s = img.shape[0]; px = (X - tx) * s - 0.5; py = (Y - ty) * s - 0.5
    i, j = int(np.clip(np.floor(px), 0, s - 2)), int(np.clip(np.floor(py), 0, s - 2)); u, v = np.clip(px - i, 0, 1), np.clip(py - j, 0, 1)
    return img[j, i] * (1 - u) * (1 - v) + img[j, i + 1] * u * (1 - v) + img[j + 1, i] * (1 - u) * v + img[j + 1, i + 1] * u * v
TILE = {}
def game_h(x, z):
    # (the 4 m grid in the cell's frame, each corner from the tiles, bilinear between)
    gx, gz = math.floor(x / 4) * 4, math.floor(z / 4) * 4; u, v = (x - gx) / 4, (z - gz) / 4
    c = [tile_h(LAT0 - zz / MLAT, LON0 + xx / MLON) for xx, zz in ((gx, gz), (gx + 4, gz), (gx, gz + 4), (gx + 4, gz + 4))]
    return c[0] * (1 - u) * (1 - v) + c[1] * u * (1 - v) + c[2] * (1 - u) * v + c[3] * u * v
E, N = TO.transform(lon, lat); E, N = np.array(E), np.array(N)
e0, n0 = int(E.min()) - 2, int(N.min()) - 2; e1, n1 = int(E.max()) + 3, int(N.max()) + 3
url = ("https://environment.data.gov.uk/spatialdata/lidar-composite-digital-terrain-model-dtm-1m/wcs?service=WCS&version=2.0.1"
       "&request=GetCoverage&CoverageId=13787b9a-26a4-4775-8523-806d13af58fc__Lidar_Composite_Elevation_DTM_1m&format=image/tiff"
       "&subset=E(%d,%d)&subset=N(%d,%d)") % (e0, e1, n0, n1)
dtm = tifffile.imread(io.BytesIO(urllib.request.urlopen(url, timeout=120).read())).astype(np.float64)
dtm[(dtm < -100) | (dtm > 2000)] = np.nan
def dtm_h(e, n):
    px, py = e - e0 - 0.5, n1 - n - 0.5; i, j = int(px), int(py); u, v = px - i, py - j
    return dtm[j, i] * (1 - u) * (1 - v) + dtm[j, i + 1] * u * (1 - v) + dtm[j + 1, i] * (1 - u) * v + dtm[j + 1, i + 1] * u * v
g = np.array([game_h(x, z) for x, z in xy]); t = np.array([dtm_h(e, n) for e, n in zip(E, N)])
ok = np.isfinite(t) & np.isfinite(g); d = (g - t)[ok]
d -= np.median(d)   # (the two surveys' datums a fraction apart)
print("game ground minus 1 m LiDAR along the roads: median abs %.2f m, 95%% within %.2f m, 99%% %.2f m, worst %.2f m" % (
    np.median(abs(d)), np.percentile(abs(d), 95), np.percentile(abs(d), 99), abs(d).max()))
# bumps: the 1 m profile's roughness over 10 m that the game's misses
for name, arr in (("1 m LiDAR", t[ok]), ("game", g[ok])):
    k = np.convolve(arr, np.ones(5) / 5, mode="same"); print(name, "roughness (5-point) sd %.3f m" % np.std((arr - k)[3:-3]))
bad = np.argsort(-abs(d))[:6]
la_ok, lo_ok = lat[ok], lon[ok]
for b in bad: print("  off by %.1f m at %.5f,%.5f" % (d[b], la_ok[b], lo_ok[b]))
