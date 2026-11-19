"""Builds the engine parts stage 2 review page (out/intake_review.html) from out/intake/*.png and out/intake/check.json.

    python3 foundry/intake_page.py
"""
import base64
import html
import io
import json
import os
import shutil

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "intake")
DST = "/mnt/project-files/engine_parts/intake/"

src = open(os.path.join(os.path.dirname(__file__), "intake3d.py")).read()
ns = {}
import ast  # noqa: E402
for node in ast.parse(src).body:      # the tables only (intake3d itself needs Blender)
    if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") in ("FUEL", "AIR", "SHOTS", "TYPE_BIKES"):
        exec(ast.get_source_segment(src, node), ns)
FUEL, AIR, SHOTS, TYPE_BIKES = ns["FUEL"], ns["AIR"], ns["SHOTS"], ns["TYPE_BIKES"]
TYPE_NAMES = {"single": "Four-stroke single", "two_stroke": "Two-stroke single", "parallel_twin": "Parallel twin",
              "vtwin90": "90 degree V-twin", "vtwin45": "45 degree V-twin", "boxer": "Flat twin", "triple": "Inline triple",
              "inline4": "Inline four"}


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


def card(src_, title, body):
    pic = f'<img alt="{esc(title)}" src="{src_}" loading="lazy">' if src_ else '<div class="nopic">picture to come</div>'
    return f'<figure class="card">{pic}<figcaption><h3>{esc(title)}</h3><p>{body}</p></figcaption></figure>'


def perf(p):
    out = []
    for k, lab in (("torque_mult", "pull"), ("top_mult", "top end")):
        if k in p:
            out.append(f"{lab} {'+' if p[k] >= 1 else '−'}{abs(p[k] - 1) * 100:.0f}%")
    return ", ".join(out) or "as the bike's own"


chk = json.load(open(os.path.join(OUT, "check.json"))) if os.path.exists(os.path.join(OUT, "check.json")) else {}
code = {}
cards = []
for n, (k, v) in enumerate(list(FUEL.items()) + list(AIR.items())):
    c = ("F" if k in FUEL else "A") + str((list(FUEL) if k in FUEL else list(AIR)).index(k) + 1)
    code[k] = c
shots = []
for fuel, air, oil, mn in SHOTS:
    fn = f"{mn}__{fuel}__{air}.png"
    t = f"{FUEL[fuel]['label']} with {AIR[air]['label'].lower()}"
    body = (f"{code[fuel]} + {code[air]}, {oil} oil filter, on the {esc(bike(mn))}. {esc(FUEL[fuel]['note'])} {esc(AIR[air]['note'])}")
    shots.append(card(img(fn), t, body))
for o, lab in (("black", "Black oil filter"), ("chrome", "Chrome oil filter")):
    shots.append(card(img(f"oil__{o}.png"), lab, f"O{1 if o == 'black' else 2}. The oil filter canister in {o}, shown on the Kazeda Naked. "
                      "A look only: the filter still wears and is serviced as now."))
parts = "".join(f"<tr><td>{code[k]}</td><td>{esc(v['label'])}</td><td>{esc(v['note'])}</td><td>{perf(v['perf'])}</td></tr>"
                for k, v in list(FUEL.items()) + list(AIR.items()))
rows, notes = [], []
for et, per in chk.items():
    for pair, r in per.items():
        fuel, air = pair.split("+")
        ps = r["rep"]["ports"]
        if r["rep"].get("skipped"):
            continue
        lens = ", ".join(str(p["air_length"]) for p in ps)
        turned = max((p.get("bend") or 0) for p in ps)
        roll = max(abs(p.get("roll") or 0) for p in ps)
        how = []
        if turned:
            how.append("turned towards level")
        if roll:
            how.append(f"tower leans {roll:.0f}°")
        if ps and ps[0]["port"] in ("s", "9"):
            how.append("one carb on the right, Y manifold")
        ok = not r["clips"]
        rows.append(f"<tr><td>{esc(TYPE_NAMES[et])}</td><td>{code[fuel]} + {code[air]}</td><td>{lens} mm</td><td>{esc(', '.join(how)) or 'straight on the port'}</td>"
                    f"<td>{'clear' if ok else 'touches'}</td></tr>")
        if not ok:
            notes.append(f"<li>{esc(TYPE_NAMES[et])}, {code[fuel]} + {code[air]} on the {esc(bike(r['model']))}: "
                         f"{esc(', '.join(f'{k} {v:.0f} mm' for k, v in r['clips'].items()))}</li>")

css = open(os.path.join(os.path.dirname(__file__), "bars_page.css")).read()
page = f'''<title>Engine intakes</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap">
<style>{css}\n.tablewrap{{overflow-x:auto}} table.fit{{border-collapse:collapse;font-size:14px;width:100%}} table.fit td,table.fit th{{padding:6px 10px;border-bottom:1px solid var(--line, #8884);text-align:left}}</style>
<main>
<header>
<div class="eyebrow">Engine parts · Stage 2 of 6</div>
<h1>Fuel systems, air intakes and oil filters</h1>
<p class="lede">Two new fuel systems (round-slide and flat-slide carbs) and two open air intakes (pod filters and velocity stacks) join each engine's own throttle bodies and airbox. They fit by engine type: every part bolts to the inlet stubs measured in stage 1, so one set fits every engine of a type. Where the tank sits low, a run turns towards level at its rubber manifold, a carb's slide tower leans out, and a pod or stack is shortened (and a short pod slimmed) until it touches nothing. The 45 degree V-twin takes one carb on the right fed by a Y manifold, as real ones do. The two-stroke keeps its own carb and can take either open intake. The oil filter canister comes in black or chrome. Our own designs: no maker's name or shape copied.</p>
<p class="ask"><strong>What I need from you:</strong> keep, change or drop each part by its code (F2, F3, A2, A3, O1, O2), and say if the performance changes look right.</p>
</header>

<section><h2>On the bikes</h2>
<div class="grid">{"".join(shots)}</div></section>

<section><h2>The parts</h2>
<p class="sub">Carbs come with an open intake: the airbox is shaped round each bike's own throttle bodies. Throttle bodies take any of the three.</p>
<div class="tablewrap"><table class="fit"><thead><tr><th>Code</th><th>Part</th><th>Look</th><th>Performance</th></tr></thead><tbody>{parts}</tbody></table></div></section>

<section><h2>Fit check</h2>
<p class="sub">Every fuel and air pair on one bike of each engine type: the air intake length each port got and how the run sits.</p>
<div class="tablewrap"><table class="fit"><thead><tr><th>Engine type</th><th>Pair</th><th>Intake length</th><th>How it sits</th><th>Result</th></tr></thead><tbody>{"".join(rows)}</tbody></table></div>
{f'<ul class="notes">{"".join(notes)}</ul>' if notes else "<p class='sub'>Nothing touches the bike on any engine type.</p>"}
</section>
<footer>Covers and side cases come next, in stage 3. Made with free tools only: Blender, scripted.</footer>
</main>
'''
os.makedirs(DST, exist_ok=True)
path = os.path.join(ROOT, "out", "intake_review.html")
open(path, "w").write(page)
shutil.copy(path, DST + "intake_review.html")
print(path, len(page))
