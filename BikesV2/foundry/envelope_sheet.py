"""Draws one side-view envelope diagram per family from data/families.json.

Side view, front of the bike to the left. Data coordinates are mm: x forward from
the rear axle, z up from the ground. Colours come from CSS variables so the same
SVG works on a light or dark page (see foundry/review_page.py).

Usage: python3 foundry/envelope_sheet.py   -> out/envelopes/<family>.svg
"""
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H, S = 900, 500, 0.3
OX, OY = W - 60, 430


def X(x):
    return OX - (x + 450) * S


def Y(z):
    return OY - z * S


def mid(r):
    return (r[0] + r[1]) / 2.0


def line(x1, z1, x2, z2, cls):
    return f"<line class='{cls}' x1='{X(x1):.1f}' y1='{Y(z1):.1f}' x2='{X(x2):.1f}' y2='{Y(z2):.1f}'/>"


def circle(x, z, r, cls):
    return f"<circle class='{cls}' cx='{X(x):.1f}' cy='{Y(z):.1f}' r='{r * S:.1f}'/>"


def box(x1, x2, z1, z2, cls):
    xa, xb = sorted((X(x1), X(x2)))
    ya, yb = sorted((Y(z1), Y(z2)))
    return f"<rect class='{cls}' x='{xa:.1f}' y='{ya:.1f}' width='{xb - xa:.1f}' height='{yb - ya:.1f}' rx='3'/>"


def text(x, z, s, cls="lbl", anchor="middle", dy=0):
    return f"<text class='{cls}' x='{X(x):.1f}' y='{Y(z) + dy:.1f}' text-anchor='{anchor}'>{s}</text>"


def envelope_svg(key, fam):
    wb, rake = fam["wheelbase"], fam["rake"]
    rr, rf = fam["rear_wheel"]["tyre_radius"], fam["front_wheel"]["tyre_radius"]
    seat, rider, deck = fam["seat_height"], fam["rider"], fam["deck"]
    wbm = mid(wb)
    o = [f"<svg class='env' viewBox='0 0 {W} {H}' role='img' aria-label='{fam['name']} family envelope'>"]
    # ground and clearance band
    o.append(box(-420, wb[1] + rf + 60, 0, mid(fam["ground_clearance"]), "clear"))
    o.append(line(-440, 0, wb[1] + rf + 80, 0, "ground"))
    # wheels: rear, front at shortest and longest wheelbase
    o.append(circle(0, rr, rr, "wheel"))
    o.append(circle(0, rr, rr - 95, "rim"))
    o.append(circle(wb[0], rf, rf, "wheel"))
    o.append(circle(wb[0], rf, rf - 85, "rim"))
    o.append(circle(wb[1], rf, rf, "wheel ghost"))
    # steering axis at both ends of the rake range, through the mid front axle
    for a in rake:
        L = 820 if key != "chopper" else 1000
        o.append(line(wbm, rf, wbm - L * math.sin(math.radians(a)), rf + L * math.cos(math.radians(a)), "axis"))
    # rider triangle boxes
    hx = rider["hip_x"]
    o.append(box(hx[0], hx[1], seat[0], seat[1], "hip"))
    pf, pd = rider["peg"]["fwd"], rider["peg"]["down"]
    o.append(box(mid(hx) + pf[0], mid(hx) + pf[1], mid(seat) - pd[1], mid(seat) - pd[0], "peg"))
    gf, gu = rider["grip"]["fwd"], rider["grip"]["up"]
    o.append(box(mid(hx) + gf[0], mid(hx) + gf[1], mid(seat) + gu[0], mid(seat) + gu[1], "grip"))
    hp = (mid(hx), mid(seat))
    pp = (hp[0] + mid(pf), hp[1] - mid(pd))
    gp = (hp[0] + mid(gf), hp[1] + mid(gu))
    o.append(f"<polygon class='tri' points='{X(hp[0]):.1f},{Y(hp[1]):.1f} {X(pp[0]):.1f},{Y(pp[1]):.1f} {X(gp[0]):.1f},{Y(gp[1]):.1f}'/>")
    # the deck: the hidden mounting standard (tank rail forward of the seat nose, seat rails behind it)
    nx, nz = mid(deck["nose_x"]), mid(deck["nose_z"])
    for p, cls in ((deck["tank_pitch"][0], "deck faint"), (deck["tank_pitch"][1], "deck faint"), (mid(deck["tank_pitch"]), "deck")):
        a = math.radians(p)
        o.append(line(nx, nz, nx + deck["tank_span"] * math.cos(a), nz + deck["tank_span"] * math.sin(a), cls))
    for p, cls in ((deck["seat_pitch"][0], "deck faint"), (deck["seat_pitch"][1], "deck faint"), (mid(deck["seat_pitch"]), "deck")):
        a = math.radians(p)
        o.append(line(nx, nz, nx - deck["seat_dual"] * math.cos(a), nz + deck["seat_dual"] * math.sin(a), cls))
    a = math.radians(mid(deck["tank_pitch"]))
    b = math.radians(mid(deck["seat_pitch"]))
    mounts = [(nx + deck["tank_span"] * math.cos(a), nz + deck["tank_span"] * math.sin(a)), (nx + 20 * math.cos(a), nz + 20 * math.sin(a)),
              (nx - deck["seat_solo"] * math.cos(b), nz + deck["seat_solo"] * math.sin(b)), (nx - deck["seat_dual"] * math.cos(b), nz + deck["seat_dual"] * math.sin(b))]
    for mx, mz in mounts:
        o.append(f"<circle class='mount' cx='{X(mx):.1f}' cy='{Y(mz):.1f}' r='3.2'/>")
    # dimensions
    o.append(line(0, -60, wb[0], -60, "dim"))
    o.append(line(0, -60, wb[1], -60, "dim faint"))
    o.append(text(wbm, -60, f"Wheelbase {wb[0]} to {wb[1]} mm", dy=22))
    La = 820 if key != "chopper" else 1000
    ar = math.radians(rake[0])
    o.append(text(wbm - La * math.sin(ar) + 50, rf + La * math.cos(ar), f"Rake {rake[0]:g} to {rake[1]:g} deg", anchor="end", dy=4))
    o.append(text(hx[0] - 20, mid(seat), f"Seat {seat[0]} to {seat[1]} mm", anchor="start", dy=4))
    o.append(text(wbm, -60, f"Ground clearance {fam['ground_clearance'][0]} to {fam['ground_clearance'][1]} mm", dy=44))
    o.append(text(pp[0], pp[1], "pegs", "tag", dy=-(mid(pd) * 0 + (pd[1] - pd[0]) * S / 2 + 4)))
    o.append(text(gp[0], gp[1], "grips", "tag", dy=-((gu[1] - gu[0]) * S / 2 + 4)))
    o.append(text(mounts[3][0], mounts[3][1], "tank and seat mounts", "tag deckc", anchor="end", dy=-14))
    o.append("</svg>")
    return "\n".join(o)


STANDALONE_CSS = """<style>
.env{--ink:#1c1d1f;--mute:#6b7076;--line:#9aa0a6;--acc:#c2410c;--deck:#1d4ed8;--fill:rgba(194,65,12,.14);--bg:#fff;background:var(--bg);font-family:system-ui,sans-serif}
.env .ground{stroke:var(--ink);stroke-width:1.5}.env .clear{fill:var(--line);opacity:.16}
.env .wheel{fill:none;stroke:var(--ink);stroke-width:2}.env .rim{fill:none;stroke:var(--line);stroke-width:1}
.env .ghost{stroke-dasharray:5 4;stroke:var(--line)}.env .axis{stroke:var(--mute);stroke-width:1.2;stroke-dasharray:7 4}
.env .hip,.env .peg,.env .grip{fill:var(--fill);stroke:var(--acc);stroke-width:1.2}.env .tri{fill:none;stroke:var(--acc);stroke-width:1;stroke-dasharray:3 3}
.env .deck{stroke:var(--deck);stroke-width:2.4;stroke-linecap:round}.env .faint{opacity:.3;stroke-width:1.2}.env .mount{fill:var(--bg);stroke:var(--deck);stroke-width:1.6}
.env .dim{stroke:var(--mute);stroke-width:1}.env .lbl{fill:var(--ink);font-size:19px}.env .tag{fill:var(--acc);font-size:17px}.env .deckc{fill:var(--deck)}
</style>"""

if __name__ == "__main__":
    fams = json.load(open(os.path.join(ROOT, "data", "families.json")))["families"]
    out = os.path.join(ROOT, "out", "envelopes")
    os.makedirs(out, exist_ok=True)
    for k, f in fams.items():
        svg = envelope_svg(k, f).replace("<svg ", "<svg xmlns='http://www.w3.org/2000/svg' ", 1).replace(">", ">" + STANDALONE_CSS, 1)
        open(os.path.join(out, k + ".svg"), "w").write(svg)
    print("wrote", len(fams), "envelopes to", out)
