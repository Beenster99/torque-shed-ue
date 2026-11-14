"""Builds the Phase 0 review page (brands and family envelopes) as one HTML file.

Usage: python3 foundry/review_page.py  -> out/phase0_review.html
The file has no <html>/<body> wrapper: the Artifact publisher adds it.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brand_thumbs  # noqa: E402
import envelope_sheet  # noqa: E402
import sketch  # noqa: E402

ROOT = envelope_sheet.ROOT
LAYOUT = {"vtwin90": "90 degree V-twin", "vtwin45": "narrow V-twin", "parallel_twin": "parallel twin", "triple": "triple",
          "inline4": "inline four", "single": "single", "boxer": "boxer twin"}
FRAME = {"trellis": "Steel trellis", "trellis_bright": "Bright exposed trellis", "cradle": "Tubular steel cradle", "spar": "Aluminium twin spar",
         "double_cradle": "Steel double cradle", "engine_carried": "Engine-carried, bolt-on rear"}
THUMB_OF = {"lodestar": "cruiser"}

ENV_RULES = "\n".join(envelope_sheet.STANDALONE_CSS.split("\n")[2:-1])

HEAD = """<title>Bike Models V2 Brands and Families</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:wght@400;500;600&display=swap">
<style>
/* Layout: a drawing-office review sheet. One column of sections; cards in a two-up grid that stacks on a phone. */
:root{--bg:#eceff1;--surface:#f9fafb;--ink:#14191e;--mute:#59626b;--line:#8b949c;--rule:#cfd5da;--acc:#c2410c;--deck:#1f4fd1;--fill:rgba(194,65,12,.14);
--eng:#c6cbd0;--plate:#383c42;--seat:#22252a;--lamp:#f4ecc0;
--display:"Barlow Condensed","Arial Narrow",sans-serif;--body:"IBM Plex Sans",system-ui,sans-serif;--mono:"IBM Plex Mono",ui-monospace,monospace}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--bg:#11161a;--surface:#182026;--ink:#e6eaed;--mute:#9aa5ae;--line:#75808a;--rule:#2c363e;--acc:#f08a4b;--deck:#7ea2ff;--fill:rgba(240,138,75,.18);
--eng:#59626b;--plate:#8a939b;--seat:#0b0d0f;--lamp:#f4ecc0;color-scheme:dark}}
:root[data-theme="dark"]{--bg:#11161a;--surface:#182026;--ink:#e6eaed;--mute:#9aa5ae;--line:#75808a;--rule:#2c363e;--acc:#f08a4b;--deck:#7ea2ff;--fill:rgba(240,138,75,.18);
--eng:#59626b;--plate:#8a939b;--seat:#0b0d0f;--lamp:#f4ecc0;color-scheme:dark}
body{background:var(--bg);color:var(--ink);font-family:var(--body);font-size:15px;line-height:1.5}
.wrap{max-width:1180px;margin:0 auto;padding-block:28px 56px;padding-left:max(16px,env(safe-area-inset-left,0px));padding-right:max(16px,env(safe-area-inset-right,0px));display:flex;flex-direction:column;gap:36px}
header{display:flex;flex-direction:column;gap:8px;border-bottom:2px solid var(--ink);padding-bottom:18px}
.eyebrow{font-family:var(--mono);font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--mute)}
h1{font-family:var(--display);font-weight:700;font-size:clamp(34px,6vw,54px);line-height:1;margin:0;text-wrap:balance;letter-spacing:.01em}
h2{font-family:var(--display);font-weight:600;font-size:30px;line-height:1.1;margin:0;text-wrap:balance}
h3{font-family:var(--display);font-weight:600;font-size:25px;line-height:1.1;margin:0}
p{margin:0;max-width:68ch}
.lede{font-size:17px}
.ask{background:var(--surface);border-left:4px solid var(--acc);padding:12px 16px;max-width:none}
section{display:flex;flex-direction:column;gap:16px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,460px),1fr));gap:18px}
.card{background:var(--surface);border:1px solid var(--rule);border-radius:4px;padding:16px;display:flex;flex-direction:column;gap:10px;min-width:0}
.card svg{width:100%;height:auto;display:block}
.sub{color:var(--mute)}
.chips{display:flex;flex-wrap:wrap;gap:6px;list-style:none;margin:0;padding:0}
.chips li{font-size:12.5px;border:1px solid var(--rule);border-radius:999px;padding:2px 10px}
.sw{display:flex;gap:6px;align-items:center}
.sw i{width:26px;height:16px;border:1px solid var(--line);border-radius:2px;display:block}
dl{display:grid;grid-template-columns:auto 1fr;gap:3px 14px;margin:0;font-size:13.5px}
dt{color:var(--mute)}dd{margin:0;font-family:var(--mono);font-variant-numeric:tabular-nums}
dd.t{font-family:var(--body)}
.key{display:flex;flex-wrap:wrap;gap:6px 18px;font-size:13.5px}
.key span::before{content:"";display:inline-block;width:14px;height:10px;margin-right:6px;border-radius:2px;vertical-align:middle}
.key .k1::before{background:var(--fill);border:1.2px solid var(--acc)}.key .k2::before{background:var(--deck);height:3px}.key .k3::before{border:1.2px dashed var(--line)}
ol.next{margin:0;padding-left:20px;display:flex;flex-direction:column;gap:4px;max-width:68ch}
""" + sketch.CSS + ENV_RULES + "\n</style>\n"


def brand_card(key, b, fams):
    chips = "".join(f"<li>{s}</li>" for s in b["signature"])
    sw = "".join(f"<i style='background:{c}'></i>" for c in b["colours"])
    eng = ", ".join(LAYOUT[e] for e in b["engines"])
    made = ", ".join(fams[f]["name"] for f in b["families"])
    return f"""<article class="card">
{brand_thumbs.thumb_svg(key)}
<div><h3>{b['name']}</h3><p class="sub">{b['spirit']}. Shown as its {THUMB_OF.get(key, 'naked')}.</p></div>
<ul class="chips">{chips}</ul>
<dl><dt>Frame</dt><dd class="t">{FRAME[b['frame']]}</dd><dt>Engines</dt><dd class="t">{eng.capitalize()}</dd><dt>Makes</dt><dd class="t">{made}</dd><dt>Colours</dt><dd class="t"><span class="sw">{sw}</span></dd></dl>
</article>"""


def fam_card(key, f, brands):
    r = lambda v, u="": f"{v[0]:g} to {v[1]:g}{u}"  # noqa: E731
    who = ", ".join(b["name"] for b in brands.values() if key in b["families"])
    lay = ", ".join(LAYOUT[e] for e in f["layouts"])
    fw, rw = f["front_wheel"]["rim_in"], f["rear_wheel"]["rim_in"]
    wheel = lambda w: f"{w[0]} in" if w[0] == w[1] else f"{w[0]} to {w[1]} in"  # noqa: E731
    return f"""<article class="card">
<div><h3>{f['name']}</h3><p class="sub">{f['defines']}.</p></div>
{envelope_sheet.envelope_svg(key, f)}
<dl><dt>Wheelbase</dt><dd>{r(f['wheelbase'], ' mm')}</dd><dt>Rake</dt><dd>{r(f['rake'], ' deg')}</dd><dt>Seat height</dt><dd>{r(f['seat_height'], ' mm')}</dd>
<dt>Ground clearance</dt><dd>{r(f['ground_clearance'], ' mm')}</dd><dt>Fork travel</dt><dd>{r(f['fork_travel'], ' mm')}</dd><dt>Wheels</dt><dd>front {wheel(fw)}, rear {wheel(rw)}</dd>
<dt>Engines</dt><dd class="t">{lay.capitalize()}</dd><dt>Made by</dt><dd class="t">{who}</dd></dl>
</article>"""


def build():
    fams = json.load(open(os.path.join(ROOT, "data", "families.json")))["families"]
    brands = json.load(open(os.path.join(ROOT, "data", "brands.json")))["brands"]
    n = sum(len(b["families"]) for b in brands.values())
    h = [HEAD, '<div class="wrap">',
         f"""<header><span class="eyebrow">Bike Models V2 · Phase 0 review</span><h1>Brands and Families</h1>
<p class="lede">Six fictional brands and eight family envelopes. Together they define {n} models. These are the rules every later drawing is held to.</p></header>
<p class="ask"><strong>What to look at:</strong> does each brand have a character you would recognise, and does each family's envelope look like its class? Comment on anything that feels wrong. The brand sketches are quick thumbnails to show character. The dimensioned whole-bike drawings come in Phase 1.</p>""",
         '<section><h2>The six brands</h2><p>Working names. Each brand keeps its frame type, engines and signature shapes across every family it makes.</p><div class="grid">']
    h += [brand_card(k, b, fams) for k, b in brands.items()]
    h.append("</div></section>")
    h.append("""<section><h2>The eight family envelopes</h2>
<p>Side view, front of the bike to the left. Every model in a family must fall inside these ranges. Figures are seeded from published dimensions of real bikes of each class and will be tightened as models are drawn.</p>
<p class="key"><span class="k1">Where the rider's hips, pegs and grips may sit</span><span class="k2">Hidden mounting line for tank and seat, with its mounts</span><span class="k3">Longest wheelbase and the rake range</span></p>
<div class="grid">""")
    h += [fam_card(k, f, brands) for k, f in fams.items()]
    h.append("</div></section>")
    h.append("""<section><h2>The hidden mounting line</h2>
<p>The blue line in each envelope is what lets a tank and seat from one brand bolt to a frame from another. Every frame in a family carries the same four mounts along it: two for the tank, then the seat nose, the end of a solo seat and the end of a dual seat. The tails use the last two.</p>
<p>The tank section is fixed to the frame. The seat section belongs to the bolt-on subframe and may tilt within the faint fan shown, so one brand can have a flat seat and another a high kicked-up tail. The seat nose always overlaps the back of the tank, which hides the change of angle.</p></section>
<section><h2>What happens next</h2>
<ol class="next"><li>The drawing, 3D, render and checking pipeline is built.</li><li>The shared material library is built.</li><li>The game learns to load a new model beside today's bikes.</li><li>Phase 1 starts: whole-bike side elevations of the Corvano and Kazeda nakeds for your markup.</li></ol></section>
</div>""")
    out = os.path.join(ROOT, "out", "phase0_review.html")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "w").write("\n".join(h))
    return out


if __name__ == "__main__":
    print(build())
