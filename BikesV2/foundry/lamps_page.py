"""Builds the stage 3 review page (out/lamps_review.html) from out/lamps/*.png and out/lamps/check.json.

    python3 foundry/lamps_page.py
"""
import base64
import html
import io
import json
import os
import shutil

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "lamps")
DST = "/mnt/project-files/common_parts/lamps/"

src = open(os.path.join(os.path.dirname(__file__), "lamps3d.py")).read()
ns = {}
for name in ("HEADLAMPS", "TAILS", "INDICATORS"):
    i = src.index(f"\n{name} = {{") + 1
    j = src.index("\n}\n", i) + 3
    exec(src[i:j], ns)
i = src.index("\nSHOTS = [") + 1
exec(src[i:src.index("\n]\n", i) + 3], ns)
HEADLAMPS, TAILS, INDICATORS = ns["HEADLAMPS"], ns["TAILS"], ns["INDICATORS"]
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
        tip = f' title="{esc(", ".join(r["clips"]))}"' if bad else ""
        out.append(f'<span class="chip {"moves" if bad else "ok"}"{tip}>{fam}{" *" if bad else ""}</span>')
    return f'<div class="chips">{"".join(out)}</div>' if out else ""


def section(kind, table, code, where):
    cards = []
    for n, (k, v) in enumerate(table.items()):
        mn = SHOTS.get((kind, k))
        cards.append(card(img(f"{kind}_{k}.png"), f"{code}{n + 1}. {v['label']}", f"{esc(v['note'])} Shown on the {esc(bike(mn))}{where}.", chips(kind, k)))
    return "".join(cards)


heads = section("head", HEADLAMPS, "H", "")
tails = section("tail", TAILS, "T", "")
inds = section("ind", INDICATORS, "I", "")
nights = [card(img(f"night_{mn}{sfx}.png"), f"At dusk: the {bike(mn)}{' from behind' if sfx else ''}", txt) for mn, sfx, txt in (
    ("corvano_naked", "", "H3 round halo, T3 LED strip and I1 LED indicators, lit. The headlamp throws its beam on the road ahead."),
    ("corvano_naked", "_rear", "The strip and the indicators from behind."),
    ("hartwell_classic", "", "H1 large round, T5 classic unit and I3 chrome indicators, lit."),
    ("hartwell_classic", "_rear", "The classic unit and its plate window from behind."))]
notes = []
for kind in ("head", "tail", "ind"):
    for key, per in chk.get(kind, {}).items():
        for fam, r in per.items():
            if r.get("clips"):
                notes.append(f"<li>{esc(key)} on the {esc(bike(r['model']))}: {esc(', '.join(f'{k} {v:.0f} mm' for k, v in r['clips'].items()))}</li>")

css = open(os.path.join(os.path.dirname(__file__), "bars_page.css")).read()
page = f'''<title>Lamps and Indicators</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap">
<style>{css}</style>
<main>
<header>
<div class="eyebrow">Common bike parts · Stage 3 of 9</div>
<h1>Headlamps, tail lights and indicators</h1>
<p class="lede">Eight headlamps, six tail lights and six indicators. Headlamps clamp to the fork tubes in the bike's own lamp place, tail lights stand where the bike's own lamp stood, and indicators go on the lamp ears at the front and the sides of the tail light at the back. Every lens is clear, so the reflector and bulb or LEDs show through. Our own designs, drawn from photographs of real parts with no maker's name or shape copied.</p>
<p class="ask"><strong>What I need from you:</strong> keep, change or drop each part by its code (H1 to H8, T1 to T6, I1 to I6).</p>
</header>

<section><h2>Headlamps</h2>
<p class="sub">For naked, classic, cafe, cruiser and chopper bikes. Sport and adventure bikes keep the lamps built into their fairings, and MX bikes keep their number board. Where the lower yoke sits close behind the lamp, the bowl is made shallower or the lamp stands a little further forward, and the ears clamp below the yoke. The chips show the families each one was fitted to and checked on: <span class="chip ok">fits</span> means nothing touches.</p>
<div class="grid">{heads}</div></section>

<section><h2>Tail lights</h2>
<p class="sub">On the tail end of a naked, the frame loop's bracket on a cafe, and the rear mudguard on classics, cruisers and choppers. The sport bike's lamp is part of its tail and stays. The hanger lamp hangs from a tail, so it is not offered on mudguard bikes.</p>
<div class="grid">{tails}</div></section>

<section><h2>Indicators</h2>
<p class="sub">Each comes as a set of four. Where a bike keeps its own lamps (adventure, MX), the indicators go in the bike's own places, standing out from the bodywork.</p>
<div class="grid three">{inds}</div></section>

<section><h2>At dusk</h2>
<p class="sub">Two bikes with the lamps lit, so you can see how the light reads.</p>
<div class="grid">{"".join(nights)}</div></section>

<section><h2>Fit check</h2>
<p class="sub">Every headlamp, tail light and indicator was fitted to the lead bike of each family it suits and checked for parts running into each other.</p>
{f'<ul class="notes">{"".join(notes)}</ul>' if notes else "<p class='sub'>Nothing to report: no part touches the bike or another part on any family.</p>"}
</section>
<footer>The plate hangers come in stage 8 and the clock housings in stage 4. Made with free tools only: Blender, scripted.</footer>
</main>
'''
os.makedirs(DST, exist_ok=True)
path = os.path.join(ROOT, "out", "lamps_review.html")
open(path, "w").write(page)
shutil.copy(path, DST + "lamps_review.html")
print(path, len(page))
