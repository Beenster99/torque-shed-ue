"""The fells beyond the ride's own land: limestone scars and scree from the map, so the far hillsides
show the rock bands and screes they really have instead of an even green.

Two parts:

    tint(...)       called by export_buttertubs.py for the far hills' and the horizon's vertex colours: pale
                    limestone along the map's cliff lines and grey scree and river shingle over its scree
                    and shingle areas. The land material leans on the vertex colour more with distance, so
                    they show from afar and fade into the land's own texture close to.
    python Tools/make_far_rock.py
                    the scars as geometry (see that file): a bedded face under a turf-capped shelf, set into
                    the 32 m far hills along each mapped cliff more than 40 m outside the 2 m tiles (the
                    walls and rocks work does the ones on the tiles, stone by stone).

Only numpy is needed.
"""
import json, math, os
import numpy as np

LIMESTONE = (172, 170, 160)   # sRGB: a scar's face from afar
SCREE = (146, 142, 132)       # scree and shingle


def lin255(c):
    return np.power(np.asarray(c, np.float64) / 255.0, 2.2) * 255.0


def frame(meta, O):
    fl, fo = meta["frame"]["lat"], meta["frame"]["lon"]
    p = math.radians(fl)
    m_lat = 111132.92 - 559.82 * math.cos(2 * p) + 1.175 * math.cos(4 * p)
    m_lon = 111412.84 * math.cos(p) - 93.5 * math.cos(3 * p) + 0.118 * math.cos(5 * p)

    def to_ride(lat, lon):   # 1e-5 degrees to the ride frame (m)
        return (lon * 1e-5 - fo) * m_lon - O[0], -(lat * 1e-5 - fl) * m_lat - O[2]
    return to_ride


def features(region_root, meta, O):
    """The map's cliff lines and its scree and shingle areas, in the ride frame: (cliffs, areas) as
    lists of (n, 2) arrays (x east, z south)."""
    to_ride = frame(meta, O)
    f = json.load(open(os.path.join(region_root, "features.json")))
    kinds = f["kinds"]
    cliffs, areas = [], []
    for row in f["lines"]:
        if kinds[row[0]] != "cliff":
            continue
        lat, lon = row[6], row[7]
        pts = [to_ride(lat, lon)]
        for i in range(8, len(row) - 1, 2):
            lat += row[i]; lon += row[i + 1]
            pts.append(to_ride(lat, lon))
        cliffs.append(np.array(pts))
    for row in f["areas"]:
        k = kinds[row[0]]
        if k not in ("scree", "shingle", "cliff"):
            continue
        lat, lon = row[5], row[6]
        pts = [to_ride(lat, lon)]
        for i in range(7, len(row) - 1, 2):
            lat += row[i]; lon += row[i + 1]
            pts.append(to_ride(lat, lon))
        P = np.array(pts)
        if k == "cliff":
            cliffs.append(np.vstack([P, P[:1]]))
        else:
            areas.append(P)
    return cliffs, areas


def inside_poly(X, Z, P):
    """Even-odd test of points (X, Z) against polygon P (n, 2)."""
    x0, z0 = P[:, 0], P[:, 1]
    x1, z1 = np.roll(x0, -1), np.roll(z0, -1)
    out = np.zeros(X.shape, bool)
    for a, b, c, d in zip(x0, z0, x1, z1):
        if b == d:
            continue
        cross = ((b > Z) != (d > Z)) & (X < (c - a) * (Z - b) / (d - b) + a)
        out ^= cross
    return out


def seg_dist(X, Z, L):
    """Distance from points (X, Z) to the polyline L (n, 2)."""
    out = np.full(X.shape, np.inf)
    for (ax, az), (bx, bz) in zip(L[:-1], L[1:]):
        dx, dz = bx - ax, bz - az
        l2 = max(dx * dx + dz * dz, 1e-9)
        t = np.clip(((X - ax) * dx + (Z - az) * dz) / l2, 0, 1)
        out = np.minimum(out, np.hypot(X - ax - t * dx, Z - az - t * dz))
    return out


def tint(region_root, meta, O, X, Z, rgb, step):
    """rgb (linear 0..255, the grid's vertex colours) with the map's scars and screes painted in.
    X, Z are the vertices' positions (ride frame, m) and step the grid's spacing."""
    cliffs, areas = features(region_root, meta, O)
    rgb = np.asarray(rgb, np.float64).copy()
    lime, scree = lin255(LIMESTONE), lin255(SCREE)
    x0, z0 = X[0, 0], Z[0, 0]
    nz, nx = X.shape

    def window(P, pad):
        i0 = max(int((P[:, 0].min() - pad - x0) // step), 0); i1 = min(int((P[:, 0].max() + pad - x0) // step) + 2, nx)
        j0 = max(int((P[:, 1].min() - pad - z0) // step), 0); j1 = min(int((P[:, 1].max() + pad - z0) // step) + 2, nz)
        return slice(j0, j1), slice(i0, i1)

    w_scree = np.zeros((nz, nx))
    for P in areas:
        sj, si = window(P, step)
        if sj.start >= sj.stop or si.start >= si.stop:
            continue
        m = inside_poly(X[sj, si], Z[sj, si], P).astype(np.float64)
        if m.sum() == 0:
            # Smaller than a cell: its share of the nearest vertex.
            area = 0.5 * abs(np.dot(P[:, 0], np.roll(P[:, 1], 1)) - np.dot(P[:, 1], np.roll(P[:, 0], 1)))
            c = P.mean(0)
            i = int(round((c[0] - x0) / step)); j = int(round((c[1] - z0) / step))
            if 0 <= i < nx and 0 <= j < nz:
                w_scree[j, i] = max(w_scree[j, i], min(1.0, area / (step * step)) * 0.8)
            continue
        w_scree[sj, si] = np.maximum(w_scree[sj, si], m * 0.8)
    w_lime = np.zeros((nz, nx))
    for L in cliffs:
        if len(L) < 2:
            continue
        sj, si = window(L, step * 1.5)
        if sj.start >= sj.stop or si.start >= si.stop:
            continue
        d = seg_dist(X[sj, si], Z[sj, si], L)
        w_lime[sj, si] = np.maximum(w_lime[sj, si], np.clip(1.1 - d / (0.9 * step), 0, 1) * 0.6)
    rgb = rgb * (1 - w_scree[..., None]) + scree[None, None] * w_scree[..., None]
    rgb = rgb * (1 - w_lime[..., None]) + lime[None, None] * w_lime[..., None]
    return np.clip(np.round(rgb), 0, 255).astype(np.uint8)
