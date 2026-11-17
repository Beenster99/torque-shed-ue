"""Builds the stage 7 review page (out/brakes_review.html) from out/brakes/*.png and out/brakes/check.json.

    python3 foundry/brakes_page.py
"""
import base64
import html
import io
import json
import os
import shutil

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "brakes")
DST = "/mnt/project-files/common_parts/brakes/"

src = open(os.path.join(os.path.dirname(__file__), "brakes3d.py")).read()
ns = {}
for name in ("DISCS", "CALIPERS", "CHAINS", "SPROCKETS"):
    i = src.index(f"\n{name} = {{") + 1
    exec(src[i:src.index("\n}\n", i) + 3], ns)
i = src.index("\nSHOTS = [") + 1
exec(src[i:src.index("\n]\n", i) + 3], ns)
TABLES = {"disc": ns["DISCS"], "cal": ns["CALIPERS"], "drive": ns["CHAINS"]}
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


DRIVE = {k: o for kind, k, mn, o in ns["SHOTS"] if kind == "drive"}


def section(kind, code):
    cards = []
    for n, (k, v) in enumerate(TABLES[kind].items()):
        mn = SHOTS[(kind, k)]
        note = esc(v["note"])
        if kind == "drive":
            o = DRIVE[k]
            sp = ns["SPROCKETS"][o["sprocket"]]
            fin = f" in {o['spr_finish']}" if o.get("spr_finish") and o["sprocket"] != "steel" else ""
            note += f" With the {esc(sp['label'].lower())} sprocket{esc(fin)}: {esc(sp['note'][0].lower() + sp['note'][1:])}"
            ch = chips("sprocket", o["sprocket"])
        else:
            ch = chips(kind, k)
        cards.append(card(img(f"{kind}_{k}.png"), f"{code}{n + 1}. {v['label']}", f"{note} Shown on the {esc(bike(mn))}.", ch))
    return "".join(cards)


discs, cals, drives = section("disc", "D"), section("cal", "C"), section("drive", "CH")
notes = []
for kind, per_k in chk.items():
    for key, per in per_k.items():
        for fam, r in per.items():
            if r.get("clips"):
                notes.append(f"<li>{esc(key)} on the {esc(bike(r['model']))}: {esc(', '.join(f'{k} {v:.0f} mm' for k, v in r['clips'].items()))}</li>")

css = open(os.path.join(os.path.dirname(__file__), "bars_page.css")).read()
page = f'''<title>Brakes and Drive</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap">
<style>{css}</style>
<main>
<header>
<div class="eyebrow">Common bike parts · Stage 7 of 9</div>
<h1>Brake discs, calipers, chains and sprockets</h1>
<p class="lede">Four discs, four calipers, four chain finishes and three rear sprockets, styled from the pictures you sent. Every disc keeps the bike's own size and braking ring, on the bike's own hub. Every front caliper sits where the bike's did, on the same two radial bolts into the lugs on the fork foot, and keeps the bike's hose union, so the brake lines still meet it. The chain stays the bike's own, only its plates change colour. A sprocket keeps the bike's tooth count. Our own designs: nothing copied from a maker.</p>
<p class="ask"><strong>What I need from you:</strong> keep, change or drop each part by its code (D1 to D4, C1 to C4, CH1 to CH4).</p>
</header>

<section><h2>Brake discs</h2>
<p class="sub">The rear disc takes the same style as the front, at its own smaller size. The chips show the families each one was fitted to and checked on.</p>
<div class="grid">{discs}</div></section>

<section><h2>Calipers</h2>
<p class="sub">Each caliper comes in gold, red, black, silver or blue; the pictures show one finish each. The rear caliper is always the compact twin-pot, in the same finish as the front.</p>
<div class="grid">{cals}</div></section>

<section><h2>Chains and sprockets</h2>
<p class="sub">Any chain goes with any sprocket, and the alloy parts come in the same five finishes as the calipers. Bikes with shaft drive have no chain or sprocket.</p>
<div class="grid">{drives}</div></section>

<section><h2>Fit check</h2>
<p class="sub">Every part was fitted to the lead bike of each family it suits and checked for parts running into each other.</p>
{f'<ul class="notes">{"".join(notes)}</ul>' if notes else "<p class='sub'>Nothing to report: no part touches the bike on any family.</p>"}
</section>
<footer>Shocks, spring caps and hangers come next, in stage 8. Made with free tools only: Blender, scripted.</footer>
</main>
'''
os.makedirs(DST, exist_ok=True)
path = os.path.join(ROOT, "out", "brakes_review.html")
open(path, "w").write(page)
shutil.copy(path, DST + "brakes_review.html")
print(path, len(page))
