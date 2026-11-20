"""Builds the stage 8b review page (out/highbars_review.html) from out/highbars/*.png and out/highbars/check.json.

    python3 foundry/highbars_page.py
"""
import base64
import html
import io
import json
import os
import shutil

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "highbars")
DST = "/mnt/project-files/common_parts/highbars/"

src = open(os.path.join(os.path.dirname(__file__), "highbars3d.py")).read()
ns = {}
for name in ("HIGH",):
    i = src.index(f"\n{name} = {{") + 1
    exec(src[i:src.index("\n}\n", i) + 3], ns)
i = src.index("\nSHOTS = [") + 1
exec(src[i:src.index("\n]\n", i) + 3], ns)
TABLES = {"bar": ns["HIGH"]}
SHOTS = {("bar", key): mn for key, mn, g, e in ns["SHOTS"]}
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
        cards.append(card(img(f"bar_{k}.png"), f"{code}{n + 1}. {v['label']}", f"{esc(v['note'])} Shown on the {esc(bike(mn))}.", chips(kind, k)))
    return "".join(cards)


bars = section("bar", "HB")
notes = []
for kind, per_k in chk.items():
    for key, per in per_k.items():
        for fam, r in per.items():
            if r.get("clips"):
                notes.append(f"<li>{esc(key)} on the {esc(bike(r['model']))}: {esc(', '.join(f'{k} {v:.0f} mm' for k, v in r['clips'].items()))}</li>")

css = open(os.path.join(os.path.dirname(__file__), "bars_page.css")).read()
moves = []
for bar, per in chk.get("bar", {}).items():
    for fam, r in per.items():
        moves.append(f"<li>{esc(ns['HIGH'][bar]['label'])} on the {esc(bike(r['model']))}: hands {r['hand_move']} mm from where they were; {r['lock']['clear_at_30']:.0f} mm clear of the tank at full lock</li>")
page = f'''<title>Chopper High Bars</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap">
<style>{css}</style>
<main>
<header>
<div class="eyebrow">Common bike parts · Stage 8b · Chopper specials</div>
<h1>High bars for choppers and cruisers</h1>
<p class="lede">Four tall bars. Each one clamps where the bike's own bar clamps and is the same 22.2 mm bar through the grips as the rest of the library, so the levers, switches and mirrors you approved in stage 2 sit on them unchanged. Only the path between the clamp and the grips is new. The rider's hands go where the grips go. Our own designs: nothing copied from a maker.</p>
<p class="ask"><strong>What I need from you:</strong> keep, change or drop each bar by its code (HB1 to HB4).</p>
</header>

<section><h2>High bars</h2>
<p class="sub">The chips show the families each bar was fitted to and checked on. Chrome or satin black; any of the stage 1 finishes also works.</p>
<div class="grid">{bars}</div></section>

<section><h2>Fit check</h2>
<p class="sub">Every bar turns to full lock (30 degrees each way) clear of the tank. They move the hands a long way, which is the point of a high bar; the rider's arms follow the grips in the game.</p>
<ul class="notes">{"".join(moves)}</ul>
</section>
<footer>Next is stage 9: every approved part goes into the game. Made with free tools only: Blender, scripted.</footer>
</main>
'''
os.makedirs(DST, exist_ok=True)
path = os.path.join(ROOT, "out", "highbars_review.html")
open(path, "w").write(page)
shutil.copy(path, DST + "highbars_review.html")
print(path, len(page))
