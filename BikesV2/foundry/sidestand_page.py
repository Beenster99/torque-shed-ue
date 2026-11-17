"""Builds the side stand review page (out/sidestand_review.html) from out/sidestand/*.png and the checks.

    python3 foundry/sidestand_page.py [check json files...]
"""
import base64
import html
import io
import json
import os
import shutil
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "sidestand")
DST = "/mnt/project-files/common_parts/sidestand/"
LEADS = [("Naked", "corvano_naked"), ("Sport", "kazeda_sport"), ("Cafe", "hartwell_cafe"), ("Classic", "hartwell_classic"),
         ("Cruiser", "lodestar_cruiser"), ("Chopper", "lodestar_chopper"), ("Adventure", "corvano_adventure"), ("MX", "varga_mx")]
LABEL = {"sport": "Forged sport, black, rubber-tipped tang", "street": "Pressed street, black",
         "classic": "Chrome classic, kicked foot", "adventure": "Wide-foot adventure, black", "mx": "Slim off-road, silver, no tang"}


def esc(s):
    return html.escape(str(s))


def bike(mn):
    return mn.replace("_", " ").title().replace("Hartwell", "BSX")


def img(fn, w=900):
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


chk = {}
for f in sys.argv[1:] or [os.path.join(OUT, "check.json")]:
    if os.path.exists(f):
        chk.update(json.load(open(f)))

cards = []
for n, (fam, mn) in enumerate(LEADS):
    r = chk.get(mn, {})
    st = r.get("stand", {})
    pics = "".join(f'<img alt="{esc(bike(mn))} stand {p}" src="{s}" loading="lazy">' for p in ("folded", "down") if (s := img(f"{mn}_{p}.png")))
    body = (f"{esc(LABEL.get(r.get('style'), ''))}. Leg {st.get('length_mm', 0):.0f} mm; it swings {abs(st.get('angle_down_deg', 0)):.0f} degrees "
            f"from folded (left picture) to down (right picture, the bike leaned {st.get('lean_deg', 12):.0f} degrees onto it).")
    cards.append(f'<figure class="card wide"><div class="pair">{pics or "<div class=nopic>picture to come</div>"}</div>'
                 f'<figcaption><h3>SS{n + 1}. {esc(fam)}: {esc(bike(mn))}</h3><p>{body}</p></figcaption></figure>')

rows, bad = [], []
for mn in sorted(chk):
    r = chk[mn]
    st = r.get("stand", {})
    clash = {**{f"down {k}": v for k, v in (r.get("clash_down") or {}).items()}, **{f"folded {k}": v for k, v in (r.get("clash_folded") or {}).items()}}
    if not r.get("fitted") or clash:
        bad.append(f"<li>{esc(bike(mn))}: {'not fitted' if not r.get('fitted') else esc(', '.join(f'{k} {v:.0f} mm' for k, v in clash.items()))}</li>")
    rows.append(f"<tr><td>{esc(bike(mn))}</td><td>{esc(r.get('family', ''))}</td><td>{st.get('length_mm', 0):.0f}</td>"
                f"<td>{'clear' if r.get('fitted') and not clash else 'see below'}</td></tr>")

css = open(os.path.join(os.path.dirname(__file__), "bars_page.css")).read()
css += """
.card.wide{grid-column:1/-1}
.pair{display:grid;grid-template-columns:1fr 1fr;gap:4px}
.pair img{width:100%;height:auto;display:block}
@media (max-width:700px){.pair{grid-template-columns:1fr}}
table.fit{border-collapse:collapse;width:100%;font-size:14px}
table.fit td,table.fit th{padding:6px 8px;border-bottom:1px solid var(--line, #ddd);text-align:left}
.tablewrap{overflow-x:auto}
"""
page = f'''<title>V2 Side Stands</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap">
<style>{css}</style>
<main>
<header>
<div class="eyebrow">Common bike parts · From your build 154 test</div>
<h1>Side stands for the V2 bikes</h1>
<p class="lede">Every V2 bike now has a side stand on its left, on a lug under the engine a little ahead of the swingarm pivot. It is a separate part: a pivot boss and pin, a tapered leg, a foot pad, a tang to flick it down with your boot, and a return spring. Ridden, it sits folded up and back under the chain line. When you get off, the game swings it down on its pivot and the bike leans 12 degrees onto it. The leg is sized on each bike so the foot pad sits flat on the floor at that lean. There are five styles, one per kind of bike, and all are our own designs.</p>
<p class="ask"><strong>What I need from you:</strong> keep or change each style by its code (SS1 to SS8). The Bike Models V2 thread puts the stands into the bikes in its next export and makes the game use each bike's own pivot.</p>
</header>
<section><h2>On each family's lead bike</h2>
<div class="grid">{"".join(cards)}</div></section>
<section><h2>Fit check on every V2 bike</h2>
<p class="sub">Checked folded and down against every other part of the bike: frame, engine, exhaust, chain, pegs, gear lever and swingarm.</p>
{"<ul class='notes'>" + "".join(bad) + "</ul>" if bad else "<p>Nothing touches on any bike, folded or down.</p>"}
<div class="tablewrap"><table class="fit"><tr><th>Bike</th><th>Family</th><th>Leg (mm)</th><th>Fit</th></tr>{"".join(rows)}</table></div>
</section>
<footer>Made with free tools only: Blender, scripted.</footer>
</main>
'''
os.makedirs(DST, exist_ok=True)
path = os.path.join(ROOT, "out", "sidestand_review.html")
open(path, "w").write(page)
shutil.copy(path, DST + "sidestand_review.html")
print(path, len(page))
