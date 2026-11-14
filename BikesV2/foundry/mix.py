"""Mixed builds: one brand's frame and engine with another brand's parts, inside one family.

mix(host, donor, parts) lifts the named body parts off the donor and seats them on the host's
mounting deck: moved so the deck noses coincide, then turned about the nose by the difference in
section pitch (tank section for the tank, seat section for the seat and tail). The result is an
ordinary model dict, so the drawing step, the 3D step and every check run on it unchanged.

Usage: python3 foundry/mix.py corvano_naked kazeda_naked tank seat -> out/elevations/<host>+<donor>.svg
"""
import copy
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SECTION = {"tank": "tank_pitch", "caps": "tank_pitch", "seat": "seat_pitch", "tail": "seat_pitch"}
WITH = {"tank": ["caps"]}  # parts that travel together


def _mover(host, donor, key):
    (hx, hz), (dx, dz) = host["deck"]["nose"], donor["deck"]["nose"]
    a = math.radians(host["deck"][key] - donor["deck"][key])
    if key == "seat_pitch":
        a = -a  # the seat section runs backwards from the nose, so a steeper pitch turns it the other way
    c, s = math.cos(a), math.sin(a)

    def f(p):
        x, z = p[0] - dx, p[1] - dz
        return [round(hx + x * c - z * s, 1), round(hz + x * s + z * c, 1)] + list(p[2:])
    return f


def mix(host, donor, parts=("tank", "seat"), colour=True):
    m = copy.deepcopy(host)
    took = []
    for part in parts:
        for name in [part] + WITH.get(part, []):
            f = _mover(host, donor, SECTION[name])
            shapes_ = copy.deepcopy(donor["body"][name])
            for shp in shapes_:
                if "pts" in shp:
                    shp["pts"] = [f(p) for p in shp["pts"]]
                if "at" in shp:
                    shp["at"] = f(shp["at"])
            m["body"][name] = shapes_
        took.append(part)
    if colour and "tank" in parts:
        m["colours"]["body"] = donor["colours"]["body"]
    m["name"] = f"{host['brand'].title()} frame and engine, {donor['brand'].title()} {' and '.join(took)}"
    m["mix"] = {"host": host["brand"], "donor": donor["brand"], "parts": took}
    return m


if __name__ == "__main__":
    import elevation
    h, d = elevation.load(sys.argv[1]), elevation.load(sys.argv[2])
    m = mix(h, d, sys.argv[3:] or ("tank", "seat"))
    out = os.path.join(elevation.ROOT, "out", "elevations", f"{sys.argv[1]}+{sys.argv[2]}")
    svg = elevation.draw(m)
    open(out + ".svg", "w").write(svg)
    open(out + ".html", "w").write(f"<!doctype html><meta charset=utf-8><style>{elevation.CSS}body{{margin:0}}</style>{svg}")
    print(out + ".svg")
