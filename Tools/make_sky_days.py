"""Recorded days for the sky (skies plan): real Open-Meteo hours saved as test files, so the sky tests
and the sky bench can check a look against the same real sky again later. Not for replaying in the
game (beenster: past days are not replayed).

    python Tools/make_sky_days.py [lat lon]

asks Open-Meteo (free, no account; data CC BY 4.0) for the last 92 days of hours over Wensleydale,
picks one hour for each kind of sky below (the clearest example it finds), and writes each as
Data/Sky/days/<kind>.json in the shape of a live answer (its "current" is that hour, its "hourly" the
rain of the eight hours before), so the game's own reader (TorqueWeather::ParseOpenMeteo) reads it.
"""
import json
import math
import os
import sys
import urllib.request

LAT, LON = (float(sys.argv[1]), float(sys.argv[2])) if len(sys.argv) > 2 else (54.30, -2.20)
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "Sky", "days")
FIELDS = ("temperature_2m,dew_point_2m,relative_humidity_2m,precipitation,snowfall,snow_depth,cloud_cover,cloud_cover_low,"
          "cloud_cover_mid,cloud_cover_high,wind_speed_10m,wind_direction_10m,wind_gusts_10m,weather_code,visibility,"
          "cape,freezing_level_height,direct_radiation,diffuse_radiation,"
          "wind_speed_850hPa,wind_direction_850hPa,wind_speed_700hPa,wind_direction_700hPa,wind_speed_300hPa,wind_direction_300hPa")


def sun_elevation(t, lat, lon):
    """The sun's height, degrees, at a unix time (good to a degree)."""
    day = (t / 86400.0) % 365.2422
    doy = int((t / 86400.0 + 0.5) - 365.2422 * math.floor((t / 86400.0) / 365.2422)) % 366 + 1
    dec = 23.44 * math.sin(2 * math.pi * (284 + doy) / 365.0)
    b = 2 * math.pi * (doy - 81) / 364.0
    eot = 9.87 * math.sin(2 * b) - 7.53 * math.cos(b) - 1.5 * math.sin(b)
    solar = ((t % 86400) / 3600.0 + lon / 15.0 + eot / 60.0) % 24.0
    h = math.radians(15.0 * (solar - 12.0))
    la, de = math.radians(lat), math.radians(dec)
    del day
    return math.degrees(math.asin(math.sin(la) * math.sin(de) + math.cos(la) * math.cos(de) * math.cos(h)))


# Each kind: a test of an hour (its values, the sun's height) and a score to pick the clearest example.
KINDS = {
    "clear_day": (lambda v, e: e > 12 and v["cloud_cover"] <= 10, lambda v, e: e - v["cloud_cover"]),
    "high_veil": (lambda v, e: e > 10 and v["cloud_cover_high"] >= 80 and v["cloud_cover_low"] + v["cloud_cover_mid"] <= 25,
                  lambda v, e: v["cloud_cover_high"] - v["cloud_cover_low"] - v["cloud_cover_mid"]),
    "fair_cumulus": (lambda v, e: e > 15 and 25 <= v["cloud_cover_low"] <= 65 and v["cloud_cover_mid"] <= 30 and v["precipitation"] == 0,
                     lambda v, e: v["cape"] + e),
    "low_grey": (lambda v, e: e > 5 and v["cloud_cover_low"] >= 95 and v["precipitation"] == 0,
                 lambda v, e: -(v["temperature_2m"] - v["dew_point_2m"])),
    "frontal_rain": (lambda v, e: e > 3 and v["weather_code"] in (61, 63, 65) and v["cloud_cover_low"] >= 90 and v["precipitation"] >= 1.0,
                     lambda v, e: v["precipitation"] + v["cloud_cover_mid"] / 50.0),
    "showery": (lambda v, e: e > 5 and v["weather_code"] in (80, 81, 82),
                   lambda v, e: v["cape"] + v["precipitation"] * 50),
    "foggy_dawn": (lambda v, e: -6 < e < 15 and (v["weather_code"] in (45, 48) or v["visibility"] < 3000),
                   lambda v, e: -v["visibility"]),
    "storm": (lambda v, e: v["weather_code"] >= 95 or (v["cape"] >= 300 and v["precipitation"] > 0), lambda v, e: v["cape"]),
    "dusk_clear": (lambda v, e: -5 < e < 1 and v["cloud_cover"] <= 20, lambda v, e: -v["cloud_cover"]),
}


def main():
    url = ("https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&hourly=%s&past_days=92&forecast_days=1"
           "&wind_speed_unit=ms&timeformat=unixtime" % (LAT, LON, FIELDS))
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "TorqueShedUE-tools"}), timeout=60) as r:
        data = json.load(r)
    hourly = data["hourly"]
    names = [k for k in hourly if k != "time"]
    times = hourly["time"]
    best = {}
    for i, t in enumerate(times):
        v = {k: hourly[k][i] for k in names}
        if any(x is None for x in v.values()):
            continue
        e = sun_elevation(t, LAT, LON)
        for kind, (test, score) in KINDS.items():
            if test(v, e):
                s = score(v, e)
                if kind not in best or s > best[kind][0]:
                    best[kind] = (s, i, e)
    os.makedirs(OUT, exist_ok=True)
    for kind, (_, i, e) in sorted(best.items()):
        cur = {k: hourly[k][i] for k in names}
        cur["time"] = times[i]
        cur["interval"] = 3600
        answer = {"latitude": data["latitude"], "longitude": data["longitude"], "elevation": data["elevation"],
                  "utc_offset_seconds": 0, "current": cur,
                  "hourly": {"time": times[max(i - 8, 0):i + 1], "precipitation": hourly["precipitation"][max(i - 8, 0):i + 1]},
                  "recorded": {"kind": kind, "sun_elevation": round(e, 1), "note": "Open-Meteo, CC BY 4.0"}}
        with open(os.path.join(OUT, kind + ".json"), "w") as f:
            json.dump(answer, f, indent=1)
        print("%-13s %s  sun %5.1f  cloud %3d (low %3d mid %3d high %3d)  code %2d  rain %.1f  cape %d  vis %d" % (
            kind, __import__("time").strftime("%Y-%m-%d %H:%M", __import__("time").gmtime(times[i])), e, cur["cloud_cover"],
            cur["cloud_cover_low"], cur["cloud_cover_mid"], cur["cloud_cover_high"], cur["weather_code"], cur["precipitation"],
            cur["cape"], cur["visibility"]))
    missing = sorted(set(KINDS) - set(best))
    if missing:
        print("no hour found for: " + ", ".join(missing))


if __name__ == "__main__":
    main()
