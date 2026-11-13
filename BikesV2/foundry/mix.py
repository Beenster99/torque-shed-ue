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


def kit_names(model, part):
    """Names of a model's variants of one kit part ("tank_seat" or "tail"), its own first."""
    kit = model.get("kit", {})
    return [kit.get("base", {}).get(part, "standard")] + [v["name"] for v in kit.get(part, [])]


KIT_PARTS = ("tank_seat", "tail", "front", "exhaust")


def seat_cowl(body):
    """A painted cowl over the pillion half of whatever seat is fitted: follows the seat's own
    top line with a hump, and closes along the seat's underside, so it suits any seat."""
    import shapes
    seat = next(shapes.dense(shp["pts"], True, shp.get("t", 1.0)) for shp in body["seat"] if shp.get("cls") == "seat")
    xs = [p[0] for p in seat]
    x0, x1 = min(xs) + 4, min(xs) + 0.4 * (max(xs) - min(xs))
    n = 12
    top, bot = [], []
    for k in range(n + 1):
        f = k / n
        x = x0 + (x1 - x0) * f
        zt, zb = shapes.top_bottom(seat, x)
        # low and sleek: rises gently from the rider's back to the tail tip, never a ball
        top.append([round(x, 1), round(zt + 3 + 8 * math.sin(math.pi * (1 - f) ** 1.6), 1)])
        bot.append([round(x, 1), round(zb + 2, 1)])
    top[0].append(1)
    top[-1].append(1)
    bot[0].append(1)
    bot[-1].append(1)
    return {"pts": top[::-1] + bot, "cls": "body", "t": 0.6, "cowl": True}


def apply_kit(model, tank_seat=0, tail=0, front=0, exhaust=0, extras=()):
    """The model with kit variants fitted (0 is the bike as drawn; extras is a list of 1-based
    numbers of the optional add-ons). Returns a new model dict."""
    import shapes
    m = copy.deepcopy(model)
    kit, b = model.get("kit", {}), m["body"]
    if tank_seat:
        v = kit["tank_seat"][tank_seat - 1]
        tank = copy.deepcopy(v["tank"])
        outline = shapes.dense(tank[0]["pts"], True, tank[0].get("t", 1.0))
        xs = [p[0] for p in outline]
        x0, x1 = min(xs), max(xs)
        # a highlight under the top line, and the filler cap sat on the new top
        hl = [[x, shapes.top_bottom(outline, x)[0] - 30] for x in (x0 + (x1 - x0) * f for f in (0.3, 0.5, 0.68, 0.84))]
        tank.append({"pts": hl, "cls": "hl", "closed": False})
        for c in b.get("caps", []):
            c["at"][1] = round(shapes.top_bottom(outline, c["at"][0])[0] - 6, 1)
        b["tank"], b["seat"] = tank, copy.deepcopy(v["seat"])
    if tail:
        v = kit["tail"][tail - 1]
        b["tail"] = copy.deepcopy(v["tail"])
        for shp in b.get("behind", []):
            if "fender" in shp.get("cls", "") and v.get("tray"):
                shp["pts"] = copy.deepcopy(v["tray"])
    if front:
        # the head unit (lamp, its housing, the clocks) swaps as one; bars, levers and mirrors stay
        head = [dict(shp, part="head") for shp in copy.deepcopy(kit["front"][front - 1]["head"])]
        m["front_end"] = head + [shp for shp in m["front_end"] if shp.get("part") != "head"]
    if exhaust:
        m["exhaust"] = copy.deepcopy(kit["exhaust"][exhaust - 1]["exhaust"])
    fitted = []
    for e in extras:
        v = kit["extras"][e - 1]
        for part, shapes_ in v.get("add", {}).items():
            b.setdefault(part, []).extend(copy.deepcopy(shapes_))
        if v.get("auto") == "seat_cowl":
            b.setdefault("panels", []).append(seat_cowl(b))
        fitted.append(v["name"])
    pick = {"tank_seat": tank_seat, "tail": tail, "front": front, "exhaust": exhaust}
    m["kit_fitted"] = {p: kit_names(model, p)[i] for p, i in pick.items() if p in kit or p in kit.get("base", {})}
    m["kit_fitted"]["extras"] = fitted
    return m


def look_difference(a, b):
    """How different two whole bikes look from the side, over every part a kit can change
    (bodywork, head unit, exhaust): 0 the same, 1 nothing shared."""
    import shapes
    from PIL import Image, ImageChops, ImageDraw
    K = 0.4

    def q(p):
        return ((1400 - p[0]) * K, (1200 - p[1]) * K)

    def mask(m):
        im = Image.new("1", (760, 480), 0)
        d = ImageDraw.Draw(im)
        solid = [shp for part in ("tail", "seat", "tank", "panels") for shp in m["body"].get(part, [])]
        solid += [shp for shp in m["front_end"] if shp.get("part") == "head"]
        for shp in solid:
            if shp.get("circle"):
                (x, y), r = q(shp["at"]), shp["circle"] * K
                d.ellipse([x - r, y - r, x + r, y + r], fill=1)
            elif shp.get("closed", True) and "pts" in shp:
                d.polygon([q(p) for p in shapes.dense(shp["pts"], True, shp.get("t", 1.0))], fill=1)
        ex = m["exhaust"]
        for hp in ex.get("headers", []):
            d.line([q(p) for p in hp["pts"]], fill=1, width=int(hp.get("w", 44) * K), joint="curve")
        for can in ex.get("cans", []):
            d.line([q(can["a"]), q(can["b"])], fill=1, width=int(can["w"] * K))
        return im
    ma, mb = mask(a), mask(b)
    union = sum(ImageChops.logical_or(ma, mb).getdata())
    both = sum(ImageChops.logical_and(ma, mb).getdata())
    return 1 - both / union if union else 0.0


def silhouette_difference(a, b, parts=("tank", "seat", "tail")):
    """How different two bikes' upper bodies look from the side: 0 the same, 1 nothing shared.
    The share of the combined tank, seat and tail outline that only one of the two covers."""
    import shapes
    from PIL import Image, ImageChops, ImageDraw

    def mask(m):
        im = Image.new("1", (640, 300), 0)
        d = ImageDraw.Draw(im)
        for part in parts:
            for shp in m["body"][part]:
                if shp.get("closed", True) and shp.get("cls") in ("body", "seat"):
                    pts = shapes.dense(shp["pts"], True, shp.get("t", 1.0))
                    d.polygon([((1200 - x) * 0.5, (1150 - z) * 0.5) for x, z in pts], fill=1)
        return im
    ma, mb = mask(a), mask(b)
    union = sum(ImageChops.logical_or(ma, mb).getdata())
    both = sum(ImageChops.logical_and(ma, mb).getdata())
    return 1 - both / union if union else 0.0
