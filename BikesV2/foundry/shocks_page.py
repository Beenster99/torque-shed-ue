"""Builds the stage 8 review page (out/shocks_review.html) from out/shocks/*.png and out/shocks/check.json.

    python3 foundry/shocks_page.py
"""
import base64
import html
import io
import json
import os
import shutil

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "shocks")
DST = "/mnt/project-files/common_parts/shocks/"

src = open(os.path.join(os.path.dirname(__file__), "shocks3d.py")).read()
ns = {}
for name in ("SHOCKS", "CAPS", "HANGERS"):
    i = src.index(f"\n{name} = {{") + 1
    exec(src[i:src.index("\n}\n", i) + 3], ns)
i = src.index("\nSHOTS = [") + 1
exec(src[i:src.index("\n]\n", i) + 3], ns)
TABLES = {"shock": ns["SHOCKS"], "cap": ns["CAPS"], "hanger": ns["HANGERS"]}
SHOTS = {(k, key): mn for k, key, mn, o in ns["SHOTS"]}
FAMS = ["Naked", "Classic", "Cafe", "Cruiser", "Chopper", "Adventure", "MX", "Sport"]


def esc(s):
    return html.escape(str(s))


def bike(mn):
    return mn.replace("_", " ").title().replace("Hartwell", "BSX")


def img(fn, w=1100):
    p = os.path.join(OUT, fn)
    if not os.path.exists(p):
        return None
    os.makedirs(DST, exist_ok=True)
    shutil.copy(p, DST + fn)
    im = Image.open(p).convert("RGB")
    if im.width > w:
        im = im.resize((w, round(im.height * w / im.width)), Image.LANCZOS)
    b = io.BytesIO()
    im.save(b, "JPEG", quality=82)
    return "data:image/jpeg;base64," + base64.b64encode(b.getvalue()).decode()


def card(src_, title, body, chips=""):
    pic = f'<img alt="{esc(title)}" src="{src_}" loading="lazy">' if src_ else '<div class="nopic">picture to come</div>'
    return f'<figure class="card">{pic}<figcaption><h3>{esc(title)}</h3><p>{body}</p>{chips}</figcaption></figure>'


chk = json.load(open(os.path.join(OUT, "check.json"))) if os.path.exists(os.path.join(OUT, "check.json")) else {}


def chips(kind, key):
    per = chk.get(kind, {}).get(key, {})
    out = []
    for fam in FAMS:
        r = per.get(fam)
        if not r or not r.get("fitted"):
            continue
        bad = bool(r.get("clips"))
        out.append(f'<span class="chip {"moves" if bad else "ok"}">{fam}{" *" if bad else ""}</span>')
    return f'<div class="chips">{"".join(out)}</div>' if out else ""


def section(kind, code):
    cards = []
    for n, (k, v) in enumerate(TABLES[kind].items()):
        mn = SHOTS[(kind, k)]
        cards.append(card(img(f"{kind}_{k}.png"), f"{code}{n + 1}. {v['label']}", f"{esc(v['note'])} Shown on the {esc(bike(mn))}.", chips(kind, k)))
    return "".join(cards)


shocks, caps, hangers = section("shock", "S"), section("cap", "F"), section("hanger", "H")
notes = []
for kind, per_k in chk.items():
    for key, per in per_k.items():
        for fam, r in per.items():
            if r.get("clips"):
                notes.append(f"<li>{esc(key)} on the {esc(bike(r['model']))}: {esc(', '.join(f'{k} {v:.0f} mm' for k, v in r['clips'].items()))}</li>")

css = open(os.path.join(os.path.dirname(__file__), "bars_page.css")).read()
page = f'''<title>Shocks, Caps and Hangers</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap">
<style>{css}</style>
<main>
<header>
<div class="eyebrow">Common bike parts · Stage 8 of 9</div>
<h1>Rear shocks, filler caps and plate hangers</h1>
<p class="lede">Four rear shocks, four filler caps and two number plate hangers. A shock keeps the bike's two mounting eyes, so the eye-to-eye length and the ride height never change; the bike's brackets and bolts stay and only the shock between them is new. Every filler cap sits on the tank's own six-bolt ring, so any cap fits any tank. A plate hanger is an optional part that moves the plate and its lamp. Our own designs: nothing copied from a maker.</p>
<p class="ask"><strong>What I need from you:</strong> keep, change or drop each part by its code (S1 to S4, F1 to F4, H1 and H2).</p>
</header>

<section><h2>Rear shocks</h2>
<p class="sub">Shown on twin-shock bikes, where you can see them. The nakeds, sports, adventures and MX bikes take the same shocks as a single shock in the middle, mostly hidden by the frame. Springs come in red, yellow, black, chrome, blue or gold; the piggyback body in any of the five anodised colours.</p>
<div class="grid">{shocks}</div></section>

<section><h2>Filler caps</h2>
<p class="sub">The aero and billet caps come in silver, black, gold, red or blue.</p>
<div class="grid">{caps}</div></section>

<section><h2>Plate hangers</h2>
<p class="sub">The tail tidy replaces the long plate arm on bikes that have one. The side mount also gives cruisers and choppers a plate, which they have not had until now. The arms come in black or chrome.</p>
<div class="grid">{hangers}</div></section>

<section><h2>Fit check</h2>
<p class="sub">Every part was fitted to the lead bike of each family it suits and checked for parts running into each other.</p>
{f'<ul class="notes">{"".join(notes)}</ul>' if notes else "<p class='sub'>Nothing to report: no part touches the bike on any family.</p>"}
</section>
<footer>Chopper high bars come next (stage 8b), then everything goes into the game in stage 9. Made with free tools only: Blender, scripted.</footer>
</main>
'''
os.makedirs(DST, exist_ok=True)
path = os.path.join(ROOT, "out", "shocks_review.html")
open(path, "w").write(page)
shutil.copy(path, DST + "shocks_review.html")
print(path, len(page))
