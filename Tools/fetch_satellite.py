"""Real ground colour for the dale from Sentinel-2 (ESA Copernicus, free and open), so the far fells
show their true patchwork: hay meadows, pasture, heather moor, bracken, peat, scree.

Reads the bottom-of-atmosphere reflectance (bands 4, 3, 2 at 10 m) of a few clear summer scenes of
tile 30UWF from the public sentinel-cogs bucket (Element 84's Earth Search), windowed over the
dales region, keeps only clear land pixels (scene classes vegetation, bare, water), takes the per
pixel median, and writes build/satellite/albedo.npz: linear RGB reflectance on a 20 m grid in the
region's frame (x east, z south, metres from region.json's frame lat/lon), NaN where no scene saw
the ground. export_buttertubs.py reads it if present. (Earth Search has already applied the
BOA offset of processing baseline 04+, so reflectance is just DN / 10000.)

    pip install rasterio pyproj
    python Tools/fetch_satellite.py --torque-shed ../torque-shed
"""
import argparse
import json
import math
import os

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUCKET = "https://sentinel-cogs.s3.us-west-2.amazonaws.com/sentinel-s2-l2a-cogs/30/U/WF"
# Clear summer passes (tile cloud under 10%), late June to mid August like beenster's footage.
SCENES = ["2025/7/S2C_30UWF_20250712_0_L2A", "2022/8/S2B_30UWF_20220812_0_L2A",
          "2023/6/S2A_30UWF_20230613_0_L2A"]
GRID = 20.0
X_RANGE = (-42000.0, 16000.0)    # region metres: the horizon ring around the ride, with a margin
Z_RANGE = (-30000.0, 26000.0)
CLEAR = (4, 5, 6)                # SCL: vegetation, not vegetated, water


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--torque-shed", default=os.path.join(os.path.dirname(REPO), "torque-shed"))
    ap.add_argument("--out", default=os.path.join(REPO, "build", "satellite", "albedo.npz"))
    args = ap.parse_args()
    os.environ.setdefault("GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR")
    if os.path.exists("/root/.ccr/ca-bundle.crt"):
        os.environ.setdefault("CURL_CA_BUNDLE", "/root/.ccr/ca-bundle.crt")
    import rasterio
    from rasterio.windows import from_bounds
    from pyproj import Transformer

    meta = json.load(open(os.path.join(args.torque_shed, "data", "regions", "dales", "region.json")))
    lat0, lon0 = meta["frame"]["lat"], meta["frame"]["lon"]
    xs = np.arange(X_RANGE[0], X_RANGE[1] + 1, GRID)
    zs = np.arange(Z_RANGE[0], Z_RANGE[1] + 1, GRID)
    X, Z = np.meshgrid(xs, zs)
    lat = lat0 - Z / 111320.0
    lon = lon0 + X / (111320.0 * math.cos(math.radians(lat0)))
    E, N = Transformer.from_crs("EPSG:4326", "EPSG:32630", always_xy=True).transform(lon, lat)
    e0, e1, n0, n1 = E.min() - 100, E.max() + 100, N.min() - 100, N.max() + 100

    stack = []
    for sc in SCENES:
        name = sc.split("/")[-1]
        bands = {}
        for b in ("B04", "B03", "B02", "SCL"):
            with rasterio.open("%s/%s/%s.tif" % (BUCKET, sc, b)) as r:
                win = from_bounds(e0, n0, e1, n1, r.transform).round_offsets().round_lengths()
                res = 20.0
                shape = (int(round(win.height * r.res[1] / res)), int(round(win.width * r.res[0] / res)))
                a = r.read(1, window=win, out_shape=shape,
                           resampling=rasterio.enums.Resampling.average if b != "SCL" else rasterio.enums.Resampling.nearest)
                wt = r.window_transform(win) * rasterio.Affine.scale(win.width / shape[1], win.height / shape[0])
            col = ((E - wt.c) / wt.a).astype(np.int64).clip(0, shape[1] - 1)
            row = ((N - wt.f) / wt.e).astype(np.int64).clip(0, shape[0] - 1)
            bands[b] = a[row, col]
        ok = np.isin(bands["SCL"], CLEAR) & (bands["B04"] > 0)
        rgb = np.stack([bands[b].astype(np.float64) * 1e-4 for b in ("B04", "B03", "B02")], -1)
        rgb[~ok] = np.nan
        print("%s: %.0f%% clear" % (name, 100 * ok.mean()))
        stack.append(rgb)
    med = np.nanmedian(np.stack(stack), axis=0).clip(0, 1).astype(np.float32)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    np.savez_compressed(args.out, rgb=med, x0=xs[0], z0=zs[0], step=GRID, scenes=np.array(SCENES))
    print("wrote %s: %dx%d, %.0f%% seen, median rgb %s" % (args.out, len(xs), len(zs),
          100 * np.isfinite(med[..., 0]).mean(), np.nanmedian(med.reshape(-1, 3), 0).round(4)))


if __name__ == "__main__":
    main()
