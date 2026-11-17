"""Builds the stage 4 review page (out/clocks_review.html) from out/clocks/*.png and out/clocks/check.json.

    python3 foundry/clocks_page.py
"""
import base64
import html
import io
import json
import os
import shutil

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "clocks")
DST = "/mnt/project-files/common_parts/clocks/"

src = open(os.path.join(os.path.dirname(__file__), "clocks3d.py")).read()
ns = {}
i = src.index("\nHOUSINGS = {") + 1
exec(src[i:src.index("\n}\n", i) + 3], ns)
i = src.index("\nSHOTS = [") + 1
exec(src[i:src.index("\n]\n", i) + 3], ns)
HOUSINGS = ns["HOUSINGS"]
SHOTS = {key: mn for key, mn, o in ns["SHOTS"]}
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


def chips(key):
    per = chk.get(key, {})
    out = []
    for fam in FAMS:
        r = per.get(fam)
        if not r or not r.get("fitted"):
            continue
        bad = bool(r.get("clips"))
        tip = f' title="{esc(", ".join(r["clips"]))}"' if bad else ""
        out.append(f'<span class="chip {"moves" if bad else "ok"}"{tip}>{fam}{" *" if bad else ""}</span>')
    return f'<div class="chips">{"".join(out)}</div>' if out else ""


cards = []
for n, (k, v) in enumerate(HOUSINGS.items()):
    mn = SHOTS[k]
    cards.append(card(img(f"{k}.png"), f"C{n + 1}. {v['label']}", f"{esc(v['note'])} Shown on the {esc(bike(mn))}, from the rider's seat.", chips(k)))
fronts = [card(img(f"{k}_front.png", 800), f"C{n + 1} from the front", f"On the {esc(bike(SHOTS[k]))}.") for n, k in enumerate(HOUSINGS)]
notes = []
lifts = []
for key, per in chk.items():
    for fam, r in per.items():
        if r.get("clips"):
            notes.append(f"<li>{esc(key)} on the {esc(bike(r['model']))}: {esc(', '.join(f'{k} {v:.0f} mm' for k, v in r['clips'].items()))}</li>")
        if (r.get("rep") or {}).get("lift"):
            lifts.append(f"{esc(HOUSINGS[key]['label'])} on the {esc(bike(r['model']))} {r['rep']['lift']} mm")

css = open(os.path.join(os.path.dirname(__file__), "bars_page.css")).read()
page = f'''<title>Clock Housings</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap">
<style>{css}</style>
<main>
<header>
<div class="eyebrow">Common bike parts · Stage 4 of 9</div>
<h1>Clock and screen housings</h1>
<p class="lede">Six housings for the clocks and screens. Each one takes the place of the bike's own clocks or display, turns its face square to the rider's eyes, and stands on two stays from the top yoke. Bikes with no dash, like the MX, take the small unit on a stalk above the bar clamp. These are the housings only: what the screens show comes from the screens work, so here a screen is dark glass with its warning lights in the chin. Our own designs, drawn from photographs of real parts with no maker's name or shape copied.</p>
<p class="ask"><strong>What I need from you:</strong> keep, change or drop each housing by its code (C1 to C6).</p>
</header>

<section><h2>From the rider's seat</h2>
<p class="sub">The chips show the families each housing was fitted to and checked on: <span class="chip ok">fits</span> means nothing touches. Round clocks are chrome on classics, cruisers and choppers and satin black elsewhere.</p>
<div class="grid">{"".join(cards)}</div></section>

<section><h2>From the front</h2>
<p class="sub">How each one sits above the headlamp and the top yoke.</p>
<div class="grid three">{"".join(fronts)}</div></section>

<section><h2>Fit check</h2>
<p class="sub">Every housing was fitted to the lead bike of each family it suits and checked for parts running into each other. Where a housing is deeper than the bike's own clocks, it is raised until it stands clear of the top yoke{": " + "; ".join(lifts) + "." if lifts else "."} On the sport bike the wide screen slides forward instead, so it stays inside the nose behind the screen.</p>
{f'<ul class="notes">{"".join(notes)}</ul>' if notes else "<p class='sub'>Nothing to report: no housing touches the bike on any family.</p>"}
</section>
<footer>The silencers come next, in stage 5. Made with free tools only: Blender, scripted.</footer>
</main>
'''
os.makedirs(DST, exist_ok=True)
path = os.path.join(ROOT, "out", "clocks_review.html")
open(path, "w").write(page)
shutil.copy(path, DST + "clocks_review.html")
print(path, len(page))
