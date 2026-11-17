"""Builds the stage 2 review page (out/controls_review.html) from out/controls/*.png and its check files.

    python3 foundry/controls_page.py
"""
import base64
import html
import io
import json
import os
import shutil

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "controls")
DST = "/mnt/project-files/common_parts/controls/"

src = open(os.path.join(os.path.dirname(__file__), "controls3d.py")).read()
ns = {}
for name in ("LEVERS", "SWITCHES", "MIRRORS"):
    i = src.index(f"\n{name} = {{") + 1
    j = src.index("\n}\n", i) + 3
    exec(src[i:j], ns)
LEVERS, SWITCHES, MIRRORS = ns["LEVERS"], ns["SWITCHES"], ns["MIRRORS"]
i = src.index("\nSHOTS = [") + 1
exec(src[i:src.index("\n]\n", i) + 3], ns)
SHOTS = {(k, key): (mn, o) for k, key, mn, o in ns["SHOTS"]}


def esc(s):
    return html.escape(str(s))


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


def bike(mn):
    # shown brand names (beenster, 16 Nov: Hartwell is now BSX; the id stays "hartwell")
    return mn.replace("_", " ").title().replace("Hartwell", "BSX")


chk = json.load(open(os.path.join(OUT, "check.json"))) if os.path.exists(os.path.join(OUT, "check.json")) else {}
FAMS = ["Naked", "Classic", "Adventure", "Cafe", "Sport", "Cruiser", "MX"]

levers = []
for n, (k, v) in enumerate(LEVERS.items()):
    mn, o = SHOTS[("lever", k)]
    levers.append(card(img(f"lever_{k}.png"), f"L{n + 1}. {v['label']}", f"{esc(v['note'])} Shown on the {esc(bike(mn))}, brake side."))
plan = img("lever_plan.png")
if plan:
    levers.append(card(plan, "From above: the lever's curve", "L1 seen from above, mirror hidden. It runs out from the pivot away from the grip, swings back in where the fingers pull, then eases away to the ball at the tip, as in your plan view. L2, L3 and L5 follow the same curve."))
switches = []
for n, (k, v) in enumerate(SWITCHES.items()):
    mn, o = SHOTS[("switch", k)]
    switches.append(card(img(f"switch_{k}.png"), f"S{n + 1}. {v['label']}", f"{esc(v['note'])} Shown on the {esc(bike(mn))}, left hand."))
mirrors = []
for n, (k, v) in enumerate(MIRRORS.items()):
    mn, o = SHOTS[("mirror", k)]
    per = chk.get(k, {})
    chips = []
    for fam in FAMS:
        r = per.get(fam)
        if not r or not r.get("fitted"):
            continue
        mr = r.get("mirror", {})
        seen = all(x.get("seen") for x in mr.values()) if mr else False
        bad = bool(r.get("clips"))
        state = "ok" if seen and not bad else "moves"
        why = []
        if not seen:
            why.append("the glass is hidden from the rider's eye by " + ", ".join(sorted({str(x.get("blocked_by")).rstrip("0123456789-.") for x in mr.values() if not x.get("seen")})))
        if bad:
            why.append("touches " + ", ".join(k_.split("|")[1].strip() for k_ in r["clips"]))
        tip = f' title="{esc("; ".join(why))}"' if why else ""
        chips.append(f'<span class="chip {state}"{tip}>{fam}{" *" if why else ""}</span>')
    glass = next((x.get("glass_cm2") for r in per.values() for x in r.get("mirror", {}).values()), None)
    body = f"{esc(v['note'])} Shown on the {esc(bike(mn))}." + (f" Glass {glass:g} cm²." if glass else "")
    mirrors.append(card(img(f"mirror_{k}.png"), f"M{n + 1}. {v['label']}", body, f'<div class="chips">{"".join(chips)}</div>' if chips else ""))
eyes = [card(img(f"mirror_{k}_eye.png"), f"What the rider sees: {MIRRORS[k]['label'].lower()}", "From the rider's eye. The glass shows the road straight behind, aimed by the script.") for k in ("oval", "fairing_aero", "barend_round") if img(f"mirror_{k}_eye.png")]

notes = []
for fam in FAMS:
    for k in MIRRORS:
        r = chk.get(k, {}).get(fam)
        if r and r.get("fitted") and (r.get("clips") or not all(x.get("seen") for x in r.get("mirror", {}).values())):
            mr = r.get("mirror", {})
            bits = []
            if r.get("clips"):
                bits.append("touches " + ", ".join(f"{k_.split('|')[1].strip()} ({v_:.0f} mm)" for k_, v_ in r["clips"].items()))
            hidden = [x.get("blocked_by") for x in mr.values() if not x.get("seen")]
            if hidden:
                bits.append("glass hidden from the eye by " + ", ".join(sorted({str(h).rstrip("0123456789-.") for h in hidden})))
            notes.append(f"<li><strong>{esc(MIRRORS[k]['label'])} on the {esc(bike(r['model']))}:</strong> {esc('; '.join(bits))}.</li>")
sets = chk.get("sets", {})
set_bad = [f"<li><strong>{esc(k)}:</strong> {esc(', '.join(f'{p} {v:.0f} mm' for p, v in v_.items()))}</li>" for k, v_ in sets.items() if v_]
set_line = f"All {len(sets)} lever set and switch pod pairings fit together on the Corvano naked with nothing clipping." if sets and not set_bad else ""

css = open(os.path.join(os.path.dirname(__file__), "bars_page.css")).read()
page = f'''<title>Levers, Switches and Mirrors</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap">
<style>{css}</style>
<main>
<header>
<div class="eyebrow">Common bike parts · Stage 2 of 9</div>
<h1>Levers, switches and mirrors</h1>
<p class="lede">Five lever sets with their master cylinders, three left-hand switch pods and eight mirrors. They sit on any bar from stage 1 or on a bike's own bar, in the same places as on the signed-off naked: perch, pivot, switch pod and mirror boss. Our own designs, drawn from photographs of real parts with no maker's name or shape copied.</p>
<p class="ask"><strong>What I need from you:</strong> keep, change or drop each part by its code (L1 to L5, S1 to S3, M1 to M8).</p>
<p class="sub" style="margin:0">Changed after your second notes: the levers L1, L2, L3 and L5 now curve like your plan view and end in a ball, the switch pods are rounded off, M6 is now the M4 teardrop head on the bar end, M7 has lost its orange strip and M8 has bigger glass. L4 classic is still as you kept it, and every set still has the red kill switch on the right.</p>
</header>

<section><h2>Lever sets</h2>
<p class="sub">Each set is a brake lever with its master cylinder on the right and a clutch lever on the left, on matching perches. The pictures show the brake side.</p>
<div class="grid">{"".join(levers)}</div></section>

<section><h2>Switch pods</h2>
<p class="sub">The pods sit between the lever perch and the grip. These are the left-hand pods, all buttons. Every set has the same right-hand pod: a red kill switch on top, a grey starter button and a mode button (you can see it in the lever pictures).</p>
<div class="grid three">{"".join(switches)}</div></section>

<section><h2>Mirrors</h2>
<p class="sub">Every mirror is turned by the script so that from the rider's eye its glass shows the road straight behind, a little outboard. Stalk mirrors stand on the perch, bar-end mirrors take the place of the bar end, and fairing mirrors stand on the fairing shoulder (sport bikes only). The chips show the families it was checked on: <span class="chip ok">fits</span> means the glass is in clear view and nothing touches, <span class="chip moves">look</span> means there is a note below.</p>
<div class="grid">{"".join(mirrors)}</div>
{f'<div class="grid three" style="margin-top:18px">{"".join(eyes)}</div>' if eyes else ""}
</section>

<section><h2>Fit check</h2>
<p class="sub">Each mirror was fitted to the lead bike of every family it suits and checked: can the rider see the glass from the eye (the bike's parts only; the rider's own arms are not modelled yet), and does anything new run into the bike. {esc(set_line)}</p>
{f'<ul class="notes">{"".join(notes)}</ul>' if notes else "<p class='sub'>Nothing to report: every mirror is in clear view and nothing clips.</p>"}
{f'<h3>Lever sets and pods</h3><ul class="notes">{"".join(set_bad)}</ul>' if set_bad else ""}
</section>
<footer>Clock housings come in stage 4 and lamps in stage 3. Made with free tools only: Blender, scripted.</footer>
</main>
'''
os.makedirs(DST, exist_ok=True)
path = os.path.join(ROOT, "out", "controls_review.html")
open(path, "w").write(page)
shutil.copy(path, DST + "controls_review.html")
print(path, len(page))
