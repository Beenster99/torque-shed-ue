"""The night sky's picture (skies plan stage 5): Data/Sky/stars.png, made once with numpy.

The stars are the Yale Bright Star Catalogue, 5th edition (Hoffleit and Warren 1991, NASA Astronomical
Data Center; every star to about magnitude 6.5, J2000 positions), drawn into a 4096 x 2048 picture of
the whole sky by right ascension (across, 0 h at the left edge, increasing to the right) and
declination (down, +90 at the top), each a small soft dot, its colour from its B-V. The Milky Way goes
in the alpha channel, made here from the galaxy's shape (its plane, wider and brighter towards the
centre in Sagittarius, the bulge, and the dark dust lanes of the Great Rift), not from a photograph.
Both channels are stored as value ** (1 / 2.2) (M_RideStars undoes it), so a dim star keeps some bits.

Run: python Tools/make_stars.py [bsc5.dat or bsc5.dat.gz]   (needs numpy and Pillow; without a file it
fetches http://tdc-www.harvard.edu/catalogs/bsc5.dat.gz once into the system's temp folder)
"""
import gzip
import os
import sys
import tempfile
import urllib.request

import numpy as np

W, H = 4096, 2048
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Data", "Sky", "stars.png")
URL = "http://tdc-www.harvard.edu/catalogs/bsc5.dat.gz"


def catalogue_lines(path):
    if path is None:
        path = os.path.join(tempfile.gettempdir(), "bsc5.dat.gz")
        if not os.path.exists(path):
            urllib.request.urlretrieve(URL, path)
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt", encoding="latin-1") as f:
        return f.read().splitlines()


def stars(lines):
    """(ra deg, dec deg, V mag, B-V) for every star with a position and a magnitude."""
    out = []
    for ln in lines:
        ln = ln.ljust(200)
        try:
            rah, ram, ras = int(ln[75:77]), int(ln[77:79]), float(ln[79:83])
            sign = -1.0 if ln[83] == "-" else 1.0
            ded, dem, des = int(ln[84:86]), int(ln[86:88]), int(ln[88:90])
            vmag = float(ln[102:107])
        except ValueError:
            continue   # (a few entries are other objects with no J2000 position)
        try:
            bv = float(ln[109:114])
        except ValueError:
            bv = 0.6
        ra = (rah + ram / 60.0 + ras / 3600.0) * 15.0
        dec = sign * (ded + dem / 60.0 + des / 3600.0)
        if vmag <= 6.5:   # (what the eye sees on a dark night)
            out.append((ra, dec, vmag, bv))
    return np.array(out)


def bv_rgb(bv):
    """A star's colour from its B-V (Ballesteros' temperature, then a blackbody's rough colour)."""
    bv = np.clip(bv, -0.4, 2.0)
    np.seterr(invalid="ignore", divide="ignore")   # (np.where works out both sides)
    t = 4600.0 * (1.0 / (0.92 * bv + 1.7) + 1.0 / (0.92 * bv + 0.62)) / 100.0
    r = np.where(t <= 66, 1.0, np.clip(1.29293618 * (t - 60) ** -0.1332047592, 0, 1))
    g = np.where(t <= 66, np.clip(0.39008157 * np.log(t) - 0.63184144, 0, 1),
                 np.clip(1.12989086 * (t - 60) ** -0.0755148492, 0, 1))
    b = np.where(t >= 66, 1.0, np.where(t <= 19, 0.0, np.clip(0.54320678 * np.log(t - 10) - 1.19625408, 0, 1)))
    rgb = np.stack([r, g, b], -1)
    # (the eye sees star colours washed out: halfway to white)
    return 0.5 + 0.5 * rgb / np.maximum(rgb.max(-1, keepdims=True), 1e-6)


def draw_stars(cat):
    img = np.zeros((H, W, 3), np.float64)
    flux = 10.0 ** (-0.4 * (cat[:, 2] + 1.46))   # Sirius 1
    col = bv_rgb(cat[:, 3])
    for (ra, dec, mag, _), f, c in zip(cat, flux, col):
        x = ra / 360.0 * W
        y = (90.0 - dec) / 180.0 * H
        sy = 0.55 + 0.25 * max(0.0, 2.0 - mag) / 3.5   # (the brightest a touch bigger)
        sx = min(sy / max(np.cos(np.radians(dec)), 0.05), 40.0)
        rx, ry = int(np.ceil(sx * 3)), int(np.ceil(sy * 3))
        xs = np.arange(int(x) - rx, int(x) + rx + 2)
        ys = np.arange(max(int(y) - ry, 0), min(int(y) + ry + 2, H))
        if len(ys) == 0:
            continue
        gx = np.exp(-0.5 * ((xs + 0.5 - x) / sx) ** 2)
        gy = np.exp(-0.5 * ((ys + 0.5 - y) / sy) ** 2)
        k = np.outer(gy, gx)
        img[np.ix_(ys, xs % W)] += k[..., None] * (f * c)[None, None, :]
    return img


def milky_way():
    """The Milky Way's glow, 0 .. 1, by galactic longitude and latitude at every pixel."""
    ra = (np.arange(W) + 0.5) / W * 2 * np.pi
    dec = np.pi / 2 - (np.arange(H) + 0.5) / H * np.pi
    RA, DEC = np.meshgrid(ra, dec)
    # J2000 equatorial -> galactic
    ra_gp, dec_gp, l_ncp = np.radians(192.85948), np.radians(27.12825), np.radians(122.93192)
    sb = np.sin(DEC) * np.sin(dec_gp) + np.cos(DEC) * np.cos(dec_gp) * np.cos(RA - ra_gp)
    b = np.arcsin(np.clip(sb, -1, 1))
    y = np.cos(DEC) * np.sin(RA - ra_gp)
    x = np.sin(DEC) * np.cos(dec_gp) - np.cos(DEC) * np.sin(dec_gp) * np.cos(RA - ra_gp)
    l = l_ncp - np.arctan2(y, x)
    l = (l + np.pi) % (2 * np.pi) - np.pi          # -pi .. pi, 0 the centre
    bd, ld = np.degrees(b), np.degrees(l)
    toward = 0.5 + 0.5 * np.cos(l)                 # 1 towards the centre, 0 the anticentre
    width = 4.0 + 6.0 * toward
    band = np.exp(-(bd / width) ** 2) * (0.25 + 0.75 * toward ** 1.6)
    bulge = 0.9 * np.exp(-(ld / 18.0) ** 2 - (bd / 11.0) ** 2)
    # Clumps and lanes: tiling noise in (l, b), the Great Rift down the middle from Cygnus to Sagittarius.
    rng = np.random.default_rng(20261114)
    def noise(scale):
        n = 512
        k = np.fft.fftfreq(n) * n
        KX, KY = np.meshgrid(k, k)
        f = np.sqrt(KX ** 2 + KY ** 2)
        f[0, 0] = 1
        amp = f ** -scale
        amp[0, 0] = 0
        out = np.real(np.fft.ifft2(amp * np.exp(2j * np.pi * rng.random((n, n)))))
        return (out - out.mean()) / out.std()
    nz = noise(1.8)
    iu = ((ld + 180.0) / 360.0 * 512 * 4).astype(int) % 512
    iv = ((bd + 90.0) / 180.0 * 512 * 2).astype(int) % 512
    clumps = np.clip(0.7 + 0.45 * nz[iv, iu], 0.15, 1.8)
    # (the rift: from Cygnus (l 75) to Sagittarius (l -15), widest and darkest in Aquila, soft at both ends)
    along = np.clip((ld + 15.0) / 90.0, 0.0, 1.0)
    reach = np.sin(np.pi * along) ** 0.7
    rift = 1.0 - 0.7 * reach * np.exp(-((bd - 1.0 - 2.0 * np.sin(np.radians(ld * 2.0))) / (2.5 + 2.5 * reach)) ** 2)
    lanes = np.clip(1.0 - 0.5 * np.clip(noise(2.4)[iv, iu], 0, None) * np.exp(-(bd / 6.0) ** 2), 0.3, 1.0)
    mw = (band + bulge) * clumps * rift * lanes
    return np.clip(mw / np.percentile(mw, 99.9), 0, 1)


def main():
    from PIL import Image
    cat = stars(catalogue_lines(sys.argv[1] if len(sys.argv) > 1 else None))
    print("%d stars, brightest %.2f, faintest %.2f" % (len(cat), cat[:, 2].min(), cat[:, 2].max()))
    img = np.clip(draw_stars(cat), 0, 1) ** (1 / 2.2)
    mw = milky_way() ** (1 / 2.2)
    rgba = (np.concatenate([img, mw[..., None]], -1) * 255 + 0.5).astype(np.uint8)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    Image.fromarray(rgba, "RGBA").save(OUT, optimize=True)
    print("wrote %s (%d x %d: stars, Milky Way in alpha)" % (OUT, W, H))


if __name__ == "__main__":
    main()
