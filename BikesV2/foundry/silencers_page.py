"""Builds the stage 5 review page (out/silencers_review.html) from out/silencers/*.png and out/silencers/check.json.

    python3 foundry/silencers_page.py
"""
import base64
import html
import io
import json
import os
import shutil

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "silencers")
DST = "/mnt/project-files/common_parts/silencers/"

src = open(os.path.join(os.path.dirname(__file__), "silencers3d.py")).read()
ns = {}
i = src.index("\nSILENCERS = {") + 1
exec(src[i:src.index("\n}\n", i) + 3], ns)
i = src.index("\nSHOTS = [") + 1
exec(src[i:src.index("\n]\n", i) + 3], ns)
HOUSINGS = ns["SILENCERS"]
SHOTS = {key: mn for key, mn in ns["SHOTS"]}
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
    cards.append(card(img(f"{k}.png"), f"S{n + 1}. {v['label']}", f"{esc(v['note'])} Shown on the {esc(bike(mn))}, ", chips(k)))
notes = []
lens = []
for key, per in chk.items():
    for fam, r in per.items():
        if r.get("clips"):
            notes.append(f"<li>{esc(key)} on the {esc(bike(r['model']))}: {esc(', '.join(f'{k} {v:.0f} mm' for k, v in r['clips'].items()))}</li>")
        cs = (r.get("rep") or {}).get("cans") or []
        if cs:
            q = cs[0]
            lens.append((fam, HOUSINGS[key]["label"], q["length"], q["own"], q["slim"] < 1, bool(q.get("stay"))))
rows = "".join(f"<tr><td>{esc(f)}</td><td>{esc(l)}</td><td>{ln} mm</td><td>{own} mm</td><td>{'slimmer' if sl else ''}</td><td>{'strap and stay' if st else 'on its pipe'}</td></tr>" for f, l, ln, own, sl, st in sorted(lens, key=lambda x: FAMS.index(x[0])))

css = open(os.path.join(os.path.dirname(__file__), "bars_page.css")).read()
page = f'''<title>Silencers</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap">
<style>{css}\n.tablewrap{{overflow-x:auto}} table.fit{{border-collapse:collapse;font-size:14px;width:100%}} table.fit td,table.fit th{{padding:6px 10px;border-bottom:1px solid var(--line, #8884);text-align:left}}</style>
<main>
<header>
<div class="eyebrow">Common bike parts · Stage 5 of 9</div>
<h1>Silencers</h1>
<p class="lede">Eight silencers. Each one slips over the end of the bike's own pipe, with a sleeve and a clamp, and runs on along the line of the bike's own can. Each bike gives the longest can that clears the swingarm, the rear tyre, the pegs and the tail: a silencer is shortened, and if it must be, slimmed, until it touches nothing. Where the bike has a subframe or peg hanger close by, it hangs from it on a strap and a stay. The pipes stay with the bikes. Our own designs, drawn from photographs of real parts with no maker's name or shape copied.</p>
<p class="ask"><strong>What I need from you:</strong> keep, change or drop each silencer by its code (S1 to S8).</p>
</header>

<section><h2>The silencers</h2>
<p class="sub">The chips show the families each one was fitted to and checked on: <span class="chip ok">fits</span> means nothing touches. The sport bike keeps its belly box, and the silencer replaces its short outlet.</p>
<div class="grid">{"".join(cards)}</div></section>

<section><h2>Fit check</h2>
<p class="sub">Every silencer on the lead bike of each family it suits: how long it could be against the bike's own can, and how it hangs.</p>
<div class="tablewrap"><table class="fit"><thead><tr><th>Family</th><th>Silencer</th><th>Length</th><th>Own can</th><th></th><th>Hangs</th></tr></thead><tbody>{rows}</tbody></table></div>
{f'<ul class="notes">{"".join(notes)}</ul>' if notes else "<p class='sub'>Nothing touches the bike on any family.</p>"}
</section>
<footer>The footpegs come next, in stage 6. Made with free tools only: Blender, scripted.</footer>
</main>
'''
os.makedirs(DST, exist_ok=True)
path = os.path.join(ROOT, "out", "silencers_review.html")
open(path, "w").write(page)
shutil.copy(path, DST + "silencers_review.html")
print(path, len(page))
