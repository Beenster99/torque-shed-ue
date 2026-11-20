"""Builds the stage 1 review page (out/bars_review.html) from out/bars/*.png and out/bars/check.json.

    python3 foundry/bars_page.py
"""
import base64
import html
import io
import json
import os
import shutil
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "bars")
DST = "/mnt/project-files/common_parts/bars/"

# the library tables, read without Blender
src = open(os.path.join(os.path.dirname(__file__), "bars3d.py")).read()
ns = {}
for name in ("BARS", "FINISH", "GRIPS", "ENDS", "FITTED"):
    i = src.index(f"\n{name} = {{") + 1
    j = src.index("\n}\n", i) + 3
    exec(src[i:j], ns)
BARS, FINISH, GRIPS, ENDS, FITTED = ns["BARS"], ns["FINISH"], ns["GRIPS"], ns["ENDS"], ns["FITTED"]
FAMS = ["Naked", "Classic", "Adventure", "Cafe", "Sport", "Cruiser", "MX"]


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


NICE = {"bclip_ring": "clip-on clamp ring", "bclip_body": "clip-on body", "spar": "frame spar", "tank": "tank", "advnose": "nose fairing",
        "yoke_top": "top yoke", "stanchion": "fork tube"}


def nice(n):
    n = str(n).rstrip("0123456789-")
    return NICE.get(n, n.replace("_", " "))


def esc(s):
    return html.escape(str(s))


chk = json.load(open(os.path.join(OUT, "check.json"))) if os.path.exists(os.path.join(OUT, "check.json")) else {}
hands, fit, base = chk.get("hands", {}), chk.get("fit", {}), chk.get("base", {})


def fam_hand(bar, fam):
    rows = [r for r in hands.get(bar, {}).values() if r["family"] == fam]
    return min(r["d"] for r in rows) if rows else None


def verdict(bar, fam):
    """ok / moves / no for one bar on one family, from the hand point and the steering lock."""
    d = fam_hand(bar, fam)
    if d is None:
        return "no", "", ""
    f = fit.get(bar, {}).get(fam)
    lock_note = ""
    lock_bad = False
    if f and f.get("lock") and base.get(fam, {}).get("lock"):
        ml, own = f["lock"]["max_lock"], base[fam]["lock"]["max_lock"]
        if ml is not None and own is not None and ml < min(26, own) - 2:
            lock_bad = True
            lock_note = f"turns only {ml:g}° each way before coming within 15 mm of the {nice(f['lock']['against'])} (the bike's own bars: {own:g}°)"
    clips = f.get("clips") if f else None
    clip_note = ", ".join(f"the {nice(k.split('|')[0].strip())} runs {v:.0f} mm into the {nice(k.split('|')[1].strip())}" for k, v in clips.items()) if clips else ""
    if d > 70:
        return "no", f"{d} mm", ""
    state = "ok" if d <= 40 else "moves"
    if lock_bad or clip_note:
        state = "moves" if state == "ok" else state
    return state, f"{d} mm", "; ".join(x for x in (lock_note, clip_note) if x)


def card(src_, title, body, chips=""):
    pic = f'<img alt="{esc(title)}" src="{src_}" loading="lazy">' if src_ else '<div class="nopic">picture to come</div>'
    return f'<figure class="card">{pic}<figcaption><h3>{esc(title)}</h3><p>{body}</p>{chips}</figcaption></figure>'


parts = []
# fitted pictures first: each bar on a bike it suits
fitted = []
for i, (bar, (mn, grip, end)) in enumerate(FITTED.items()):
    st = BARS[bar]
    body = f'On the {esc(mn.replace("_", " ").title().replace("Hartwell", "BSX"))} with {esc(GRIPS[grip]["label"].lower())} grips and {esc(ENDS[end]["label"].lower())} ends. {esc(FINISH[st["finish"]]["label"].capitalize())}.'
    fitted.append(card(img(f"fit_{bar}.png"), f'{i + 1}. {st["label"]}', body))

bars = []
for i, (bar, st) in enumerate(BARS.items()):
    chips = []
    for fam in FAMS:
        v, d, note = verdict(bar, fam)
        if v == "no":
            continue
        tip = f' title="{note}"' if note else ""
        chips.append(f'<span class="chip {v}"{tip}>{fam}{" *" if note else ""}</span>')
    chip_html = f'<div class="chips">{"".join(chips) or "<span class=chip>none yet</span>"}</div>'
    shape = "clip-ons on the fork tube" if st.get("clip") else f'hands {st["rise"]:+d} mm up and {st["back"]} mm back from the clamp'
    body = f'{esc(st["note"])} {esc(shape.capitalize())}, {"28.6 mm fat bar" if st.get("fat") else "22.2 mm bar"}{", cross brace" if st.get("brace") else ""}. Shown in {esc(FINISH[st["finish"]]["label"])}.'
    bars.append(card(img(f"bar_{bar}.png"), f"B{i + 1}. {st['label']}", body, chip_html))

grips = [card(img(f"grip_{k}.png"), f"G{i + 1}. {g['label']}", esc(g["note"])) for i, (k, g) in enumerate(GRIPS.items())]
ends = [card(img(f"end_{k}.png"), f"E{i + 1}. {e['label']}", esc(e["note"])) for i, (k, e) in enumerate(ENDS.items())]

# the fit table
rows = []
for i, (bar, st) in enumerate(BARS.items()):
    cells = []
    for fam in FAMS:
        v, d, note = verdict(bar, fam)
        cells.append(f'<td class="{v}"{f" title=" + chr(34) + note + chr(34) if note else ""}>{d or "–"}{" *" if note else ""}</td>')
    rows.append(f'<tr><th scope="row">B{i + 1}. {esc(st["label"])}</th>{"".join(cells)}</tr>')
notes = []
for bar, per in fit.items():
    for fam, f in per.items():
        v, d, note = verdict(bar, fam)
        if note:
            notes.append(f"<li><strong>{esc(BARS[bar]['label'])} on the {esc(fam.lower())}:</strong> {note}.</li>")
own = []
for fam in FAMS:
    b = base.get(fam)
    if b and b.get("lock"):
        L = b["lock"]
        own.append(f"<li>{esc(b['model'].replace('_', ' ').title().replace('Hartwell', 'BSX'))}: its own bars turn {L['max_lock']:g}° each way before coming within 15 mm of the {nice(L['against'])}.</li>")

css = open(os.path.join(os.path.dirname(__file__), "bars_page.css")).read()
page = f'''<title>Bars, Grips and Bar Ends</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap">
<style>{css}</style>
<main>
<header>
<div class="eyebrow">Common bike parts · Stage 1 of 9</div>
<h1>Bars, grips and bar ends</h1>
<p class="lede">Ten handlebars, six grips and six bar ends that bolt onto any Bikes V2 bike. Each is our own design, modelled from photographs of real parts with no maker's name or shape copied. The levers, switches and mirrors are the signed-off naked ones for now; they get their own library in stage 2.</p>
<p class="ask"><strong>What I need from you:</strong> keep, change or drop each part by its code (B1 to B10, G1 to G6, E1 to E6). A reference photo helps whenever something looks wrong.</p>
</header>

<section><h2>On the bikes</h2>
<p class="sub">Each bar on a bike from a family it suits, with the levers, switches and mirrors moved to the new grips and the cables following them.</p>
<div class="grid fitted">{"".join(fitted)}</div></section>

<section><h2>Handlebars</h2>
<p class="sub">Every bar is 22.2 mm through the grips and runs dead straight from the switch pods to its end, so the controls always sit square on it. The chips show the families each bar suits: <span class="chip ok">fits</span> keeps the hands within 40 mm of where that family's rider holds them, <span class="chip moves">moves</span> changes the riding position by 40 to 70 mm. Any bar comes in any finish: {", ".join(esc(f["label"]) for f in FINISH.values())}.</p>
<div class="grid">{"".join(bars)}</div></section>

<section><h2>Grips</h2>
<p class="sub">All six are 126 mm long on a 22.2 mm bar, with the same inside end, so any grip goes on any bar.</p>
<div class="grid three">{"".join(grips)}</div></section>

<section><h2>Bar ends</h2>
<p class="sub">Each plugs 17 mm into the end of the bar. Bikes with hand guards or bar-end mirrors keep those in place of a bar end.</p>
<div class="grid three">{"".join(ends)}</div></section>

<section><h2>Fit check</h2>
<p class="sub">Measured by script on every model: how far each bar moves the rider's hands from the family's own hand point (smallest move in the family). Then each bar was fitted to the lead bike of every family, turned to full lock, and checked for parts running into each other.</p>
<div class="tablewrap"><table><thead><tr><th>Bar</th>{"".join(f"<th>{f}</th>" for f in FAMS)}</tr></thead><tbody>{"".join(rows)}</tbody></table></div>
<p class="legend"><span class="chip ok">fits</span> <span class="chip moves">moves the hands</span> <span class="chip no">not offered</span> * see the note below</p>
{f'<h3>Notes from the fit check</h3><ul class="notes">{"".join(notes)}</ul>' if notes else ""}
{f'<h3>The bikes&#39; own bars, for comparison</h3><ul class="notes">{"".join(own)}</ul>' if own else ""}
</section>
<footer>Choppers keep their own bars, which are part of their look; grips and bar ends still fit them. Made with free tools only: Blender, scripted.</footer>
</main>
'''
os.makedirs(DST, exist_ok=True)
path = os.path.join(ROOT, "out", "bars_review.html")
open(path, "w").write(page)
shutil.copy(path, DST + "bars_review.html")
print(path, len(page))
