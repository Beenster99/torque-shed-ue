"""Builds the stage 6 review page (out/pegs_review.html) from out/pegs/*.png and out/pegs/check.json.

    python3 foundry/pegs_page.py
"""
import base64
import html
import io
import json
import os
import shutil

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "pegs")
DST = "/mnt/project-files/common_parts/pegs/"

src = open(os.path.join(os.path.dirname(__file__), "pegs3d.py")).read()
ns = {}
for name in ("PEGS", "HEELS", "PEDALS"):
    i = src.index(f"\n{name} = {{") + 1
    exec(src[i:src.index("\n}\n", i) + 3], ns)
i = src.index("\nSHOTS = [") + 1
exec(src[i:src.index("\n]\n", i) + 3], ns)
TABLES = {"peg": ns["PEGS"], "heel": ns["HEELS"], "pedal": ns["PEDALS"]}
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


pegs, heels, pedals = section("peg", "P"), section("heel", "HP"), section("pedal", "FC")
notes = []
for kind, per_k in chk.items():
    for key, per in per_k.items():
        for fam, r in per.items():
            if r.get("clips"):
                notes.append(f"<li>{esc(key)} on the {esc(bike(r['model']))}: {esc(', '.join(f'{k} {v:.0f} mm' for k, v in r['clips'].items()))}</li>")

css = open(os.path.join(os.path.dirname(__file__), "bars_page.css")).read()
page = f'''<title>Footpegs and Controls</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap">
<style>{css}</style>
<main>
<header>
<div class="eyebrow">Common bike parts · Stage 6 of 9</div>
<h1>Footpegs, heel plates and foot controls</h1>
<p class="lede">Six footpegs, three heel plates and three sets of foot controls. The peg points never move: every peg stands where the bike's own peg stood, on the bike's own pivot pin, so the rider's feet stay where the rider pose work put them. Heel plates take the bike's own outline and bolt holes. The gear lever and brake pedal keep the bike's pivot and toe points and its gear linkage, so the boot finds them where it did. Our own designs, drawn from photographs of real parts with no maker's name or shape copied.</p>
<p class="ask"><strong>What I need from you:</strong> keep, change or drop each part by its code (P1 to P6, HP1 to HP3, FC1 to FC3).</p>
</header>

<section><h2>Footpegs</h2>
<p class="sub">The pillion pegs take the same style as the rider's. A floorboard bike's pillion gets the rubber peg. The chips show the families each one was fitted to and checked on: <span class="chip ok">fits</span> means nothing touches. The MX bikes have no peg pivot in their model yet, so their pegs stand at the end of the drawn peg bar; the bikes thread will confirm the point.</p>
<div class="grid">{pegs}</div></section>

<section><h2>Heel plates</h2>
<p class="sub">For bikes with heel plates: nakeds, sports, cafes, classics and adventures. Cruisers and choppers have forward controls on cast arms and no heel plates; MX bikes have none. HP3 uses the new woven carbon.</p>
<div class="grid three">{heels}</div></section>

<section><h2>Foot controls</h2>
<p class="sub">Gear lever on the left, brake pedal on the right.</p>
<div class="grid three">{pedals}</div></section>

<section><h2>Fit check</h2>
<p class="sub">Every part was fitted to the lead bike of each family it suits and checked for parts running into each other.</p>
{f'<ul class="notes">{"".join(notes)}</ul>' if notes else "<p class='sub'>Nothing to report: no part touches the bike on any family.</p>"}
</section>
<footer>Discs, calipers, chains and sprockets come next, in stage 7. Made with free tools only: Blender, scripted.</footer>
</main>
'''
os.makedirs(DST, exist_ok=True)
path = os.path.join(ROOT, "out", "pegs_review.html")
open(path, "w").write(page)
shutil.copy(path, DST + "pegs_review.html")
print(path, len(page))
