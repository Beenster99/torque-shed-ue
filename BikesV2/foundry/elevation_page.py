"""Builds the review page of whole-bike side elevations for one family.

Usage: python3 foundry/elevation_page.py naked corvano_naked kazeda_naked -> out/naked_elevations.html
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import checks  # noqa: E402
import elevation  # noqa: E402
import review_page  # noqa: E402

ROOT = elevation.ROOT
EXTRA = """
.sheet{background:var(--surface);border:1px solid var(--rule);border-radius:4px;padding:14px;display:flex;flex-direction:column;gap:12px;min-width:0}
.sheet svg{width:100%;height:auto;display:block;border:1px solid var(--rule)}
.checks{list-style:none;margin:0;padding:0;display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,330px),1fr));gap:4px 18px;font-size:13.5px}
.checks li::before{content:"pass";font-family:var(--mono);font-size:11px;letter-spacing:.06em;text-transform:uppercase;border:1px solid var(--deck);color:var(--deck);border-radius:3px;padding:0 5px;margin-right:8px}
.checks li.bad::before{content:"fail";border-color:var(--acc);color:var(--acc)}
ul.plain{margin:0;padding-left:20px;display:flex;flex-direction:column;gap:4px;max-width:68ch}
"""


def build(family, names):
    fams = json.load(open(os.path.join(ROOT, "data", "families.json")))["families"]
    head = review_page.HEAD.replace("<title>Bike Models V2 Brands and Families</title>", f"<title>{fams[family]['name']} Side Elevations</title>")
    head = head.replace("\n</style>\n", EXTRA + elevation.CSS + "\n</style>\n")
    h = [head, '<div class="wrap">',
         f"""<header><span class="eyebrow">Bike Models V2 · Phase 1 proof · {fams[family]['name']} family</span><h1>{fams[family]['name']} Side Elevations</h1>
<p class="lede">The first whole-bike drawings: two deliberately opposite nakeds. Every line comes from one description file per model, and the 3D models will be built from the same file.</p></header>
<p class="ask"><strong>What to look at:</strong> does each bike look like a believable machine of its brand, and are the proportions right? Comment on any part you want changed, for example the tank line, the tail, the lamp or the stance. A marked-up change is redrawn and rechecked automatically.</p>"""]
    for n in names:
        m = elevation.load(n)
        res = checks.check(m, fams)
        li = "".join(f"<li class='{'' if ok else 'bad'}'>{msg}</li>" for ok, msg in res)
        h.append(f"""<section><h2>{m['name']}</h2><div class="sheet">{elevation.draw(m)}<ul class="checks">{li}</ul></div></section>""")
    h.append("""<section><h2>What these drawings are, and are not yet</h2>
<ul class="plain"><li>They are drawn to scale from real numbers: wheelbase, rake, trail, seat height, wheel sizes and every frame tube.</li>
<li>The automatic checks under each drawing already run on every change. The front tyre check was added after your comment that the Corvano engine sat too close to the front tyre. The engine has been rotated back and the wheelbase lengthened by 15 mm.</li>
<li>Not yet drawn: top and front views, the kit variants (three tank and seat pairs, three tails, front ends) and small detail such as cables, hoses and fasteners.</li></ul></section></div>""")
    out = os.path.join(ROOT, "out", f"{family}_elevations.html")
    open(out, "w").write("\n".join(h))
    return out


if __name__ == "__main__":
    print(build(sys.argv[1], sys.argv[2:]))
