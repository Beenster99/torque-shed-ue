#!/usr/bin/env python3
"""The rules round's page: every build whose type has rules, as the game builds it and by the rules.

  make_rules_page.py builds.json <folder of fit_trial rules output> <out folder>
"""
import glob, html, json, os, sys
from PIL import Image

builds = json.load(open(sys.argv[1]))["builds"]
src, out = sys.argv[2], sys.argv[3]
os.makedirs(os.path.join(out, "img"), exist_ok=True)
TITLES = {"0_designed": "Designed bike", "1_engine": "Engine swap", "2_tank": "Tank swap", "3_frame": "Frame swap", "4_fork": "Fork swap",
          "5_bars": "Handlebar swap", "6_seat": "Seat swap", "7_tail": "Tail swap", "8_exhaust": "Exhaust swap", "9_fairing": "Fairing swap",
          "10_swingarm": "Swingarm swap", "12_top_half": "Top half from another style", "13_random": "Random bike"}
rows = []
for path in glob.glob(os.path.join(src, "rules_*.jsonl")):
    for line in open(path):
        if line.strip():
            rows.append(json.loads(line))
rows.sort(key=lambda r: r["n"])
cards = []
for r in rows:
    n = r["n"]
    b = builds[n]
    a_im, b_im = Image.open(os.path.join(src, "%05d_a.ppm" % n)), Image.open(os.path.join(src, "%05d_b.ppm" % n))
    W, H = a_im.size
    pair = Image.new("RGB", (W * 2 + 8, H), (255, 255, 255))
    pair.paste(a_im, (0, 0))
    pair.paste(b_im, (W + 8, 0))
    pair = pair.resize((int(pair.width * 0.7), int(pair.height * 0.7)), Image.LANCZOS)
    name = "img/%05d.jpg" % n
    pair.save(os.path.join(out, name), quality=72, optimize=True)
    notes = r["notes"]
    acted = any(x.startswith("tail moved") for x in notes)
    nosuit = [x for x in notes if "does not suit" in x or "no subframe" in x or "no tail over" in x or "too far" in x]
    state = "suit" if nosuit else "acted" if acted else "none"
    sweep = b["sweep"]
    what = TITLES.get(sweep, sweep)
    label = "%s: %s" % (what, b["col"]) if sweep != "0_designed" else "The designed naked bike (as in the shop)"
    if sweep in ("13_random",):
        label = "Random bike %s" % b["col"]
    cards.append((n, state, label, b["row"], notes, name))

count = {k: sum(1 for c in cards if c[1] == k) for k in ("acted", "suit", "none")}
def card(c):
    n, state, label, row, notes, img = c
    pill = {"acted": ("Rules applied", "ok"), "suit": ("Does not suit", "warn"), "none": ("No rules for this tail yet", "idle")}[state]
    lis = "".join("<li>%s</li>" % html.escape(x) for x in notes) or "<li>Nothing to do: this tail has no cowl, and only cowl tails have rules so far.</li>"
    return ('<article class="card" data-state="%s"><header><span class="num">#%d</span><h3>%s</h3><span class="pill %s">%s</span></header>'
            '<figure><img loading="lazy" src="%s" alt="Build %d as the game builds it and by the naked rules" width="676" height="546">'
            '<figcaption><span>As the game builds it</span><span>By the naked rules</span></figcaption></figure><ul>%s</ul></article>') % (
        state, n, html.escape(label), pill[1], pill[0], img, n, lis)

page = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "rules_page.html")).read()
page = page.replace("{{CARDS}}", "\n".join(card(c) for c in cards))
page = page.replace("{{TOTAL}}", str(len(cards))).replace("{{ACTED}}", str(count["acted"])).replace("{{SUIT}}", str(count["suit"])).replace("{{NONE}}", str(count["none"]))
open(os.path.join(out, "index.html"), "w").write(page)
print(len(cards), count)
