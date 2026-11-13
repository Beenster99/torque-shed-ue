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
.shots{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,420px),1fr));gap:12px}
.shots figure{margin:0;display:flex;flex-direction:column;gap:4px}.shots img{width:100%;height:auto;display:block;border:1px solid var(--rule)}
.shots figcaption{font-size:12.5px;color:var(--mute)}
.sw{display:grid;grid-template-columns:repeat(auto-fill,minmax(104px,1fr));gap:10px}
.sw figure{margin:0;display:flex;flex-direction:column;align-items:center;gap:2px;text-align:center;font-size:12px;color:var(--mute)}
.sw img{width:84px;height:84px;display:block}
h3{font-family:"Barlow Condensed","Arial Narrow",sans-serif;font-size:22px;margin:18px 0 4px}figure.sheet{margin:0}figure.sheet figcaption{font-size:12.5px;color:var(--mute)}
ul.plain{margin:0;padding-left:20px;display:flex;flex-direction:column;gap:4px;max-width:68ch}
"""


def renders(name):
    """Three pictures of the 3D model as data URIs: three-quarter, side, and side with the drawing laid over it."""
    import base64
    import io
    import subprocess
    from PIL import Image
    rd = os.path.join(ROOT, "out", "renders")
    side, q = os.path.join(rd, name + "_side.png"), os.path.join(rd, name + "_q.png")
    if not (os.path.exists(side) and os.path.exists(q)):
        return ""
    el = os.path.join(ROOT, "out", "elevations", name)
    here = os.path.dirname(os.path.abspath(__file__))
    subprocess.run([sys.executable, os.path.join(here, "elevation.py"), name], check=True, capture_output=True)
    subprocess.run(["node", os.path.join(here, "shot.js"), "file://" + el + ".html", el + ".png", "1600", "1000"], check=True, timeout=90, cwd="/tmp", capture_output=True)
    a, b = Image.open(side).convert("RGB"), Image.open(el + ".png").convert("RGB")
    over = Image.blend(a, b, 0.45)

    def uri(im, box=None):
        if box:
            im = im.crop(box)
        buf = io.BytesIO()
        im.save(buf, "JPEG", quality=84)
        return "data:image/jpeg;base64," + base64.b64encode(buf.getvalue()).decode()

    crop = (230, 130, 1520, 880)
    return f"""<div class="shots"><figure><img src="{uri(Image.open(q).convert('RGB'))}" alt="{name} 3D model, three-quarter view"><figcaption>3D model built from the same file</figcaption></figure>
<figure><img src="{uri(over, crop)}" alt="{name} drawing laid over the 3D side view"><figcaption>Drawing laid over the 3D side view</figcaption></figure></div>"""


def kit_renders(names):
    """Three-quarter 3D pictures of kit bikes and mixed bikes, from whatever build3d.py --kit / --donor has rendered."""
    import base64
    import glob
    import io
    import re
    import elevation
    import mix
    from PIL import Image
    figs = []
    for q in sorted(glob.glob(os.path.join(ROOT, "out", "renders", "*_q.png"))):
        tag = os.path.basename(q)[:-6]
        k = re.match(r"(.+)_kit(\d)(\d)(\d)(\d)((?:x\d)*)$", tag)
        if k and k.group(1) in names:
            v = mix.apply_kit(elevation.load(k.group(1)), *[int(k.group(i)) for i in range(2, 6)], [int(x) for x in k.group(6).split("x") if x])
            f = v["kit_fitted"]
            cap = f"{v['brand'].title()}: {f['tank_seat']} tank and seat, {f['tail']} tail, {f['front']} front, {f['exhaust']} exhaust" + "".join(", " + x for x in f["extras"])
        elif "+" in tag and all(t in names for t in tag.split("+")):
            a, b = [elevation.load(t)["brand"].title() for t in tag.split("+")]
            cap = f"{a} frame and engine with the {b} tank and seat"
        else:
            continue
        buf = io.BytesIO()
        Image.open(q).convert("RGB").resize((1000, 625)).save(buf, "JPEG", quality=82)
        figs.append(f'<figure><img src="data:image/jpeg;base64,{base64.b64encode(buf.getvalue()).decode()}" alt="{cap}, 3D model"><figcaption>{cap}.</figcaption></figure>')
    if not figs:
        return ""
    return '<section><h2>Kit bikes and mixed bikes in 3D</h2><p class="lede">The same kit choices and cross-brand mixes, built as 3D models straight from the files with nothing modelled by hand. The shapes are still the first rough build.</p><div class="shots">' + "".join(figs) + "</div></section>"


def swatch_section():
    """The shared surface library as labelled balls, cut from out/renders/materials.png."""
    import base64
    import io
    from PIL import Image
    src = os.path.join(ROOT, "out", "renders", "materials.png")
    if not os.path.exists(src):
        return ""
    lib = json.load(open(os.path.join(ROOT, "data", "materials.json")))
    names = [lib["finishes"][f]["label"] for f in lib["finishes"]] + [v["label"] for k, v in lib["surfaces"].items() if k != "paint"]
    im, cols = Image.open(src).convert("RGBA"), 6
    cell = im.width // cols
    figs = []
    for i, label in enumerate(names):
        c = im.crop(((i % cols) * cell, (i // cols) * cell, (i % cols + 1) * cell, (i // cols + 1) * cell)).resize((168, 168), Image.LANCZOS)
        buf = io.BytesIO()
        c.save(buf, "PNG", optimize=True)
        figs.append(f'<figure><img src="data:image/png;base64,{base64.b64encode(buf.getvalue()).decode()}" alt=""><figcaption>{label.split(" (")[0]}</figcaption></figure>')
    return f"""<section><h2>Shared surface library</h2><p class="lede">Every model and every shared part uses only these {len(names)} surfaces, so parts from different brands sit together. Paint, frame, wheel and accent surfaces take the bike's own colours, and each brand has a house paint finish.</p><div class="sheet"><div class="sw">{"".join(figs)}</div></div></section>"""


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
        res = checks.check(m, fams) + checks.deck(m, fams)
        pk = checks.packing(m)
        if pk is not None:
            res.append((pk <= checks.PACKING_MAX, f"empty space inside the bike {pk * 100:.0f}% (at most {checks.PACKING_MAX * 100:.0f}%)"))
        li = "".join(f"<li class='{'' if ok else 'bad'}'>{msg}</li>" for ok, msg in res)
        h.append(f"""<section><h2>{m['name']}</h2><div class="sheet">{elevation.draw(m)}<ul class="checks">{li}</ul>{renders(n)}</div></section>""")
    import copy
    bare = []
    for n in names:
        m2 = copy.deepcopy(elevation.load(n))
        if m2["body"].get("plate_hanger"):
            m2["fit"] = {"plate_hanger": False}
            m2["dimensions"] = False
            m2["name"] = m2["brand"].title() + " without the plate hanger"
            bare.append(f'<div class="sheet">{elevation.draw(m2)}</div>')
    if bare:
        h.append('<section><h2>Plate hanger: its own part, optional</h2><p class="lede">The long hanger with the number plate and its lamp is drawn in on every bike above, as most countries require it, but it is a separate bolt-on part. A build can leave it off, as here.</p><div class="shots">' + "".join(bare) + "</div></section>")
    import itertools
    import mix
    import random
    kit_html, combos = [], 0
    show = [(1, 2, 1, 1, (1,)), (2, 1, 2, 2, (2,)), (0, 0, 2, 1, (1, 2)), (1, 1, 1, 3, ()), (2, 2, 0, 3, (1,)), (0, 1, 1, 0, (2,))]
    for n in names:
        base = elevation.load(n)
        kit = base.get("kit")
        if not kit:
            continue
        nm = {p: mix.kit_names(base, p) for p in mix.KIT_PARTS}
        xn = [v["name"] + " (" + v["note"] + ")" for v in kit.get("extras", [])]
        count = 2 ** len(xn)
        for p in mix.KIT_PARTS:
            count *= len(nm[p])
        combos += count
        rnd = random.Random(7)
        def pick():
            return mix.apply_kit(base, *[rnd.randrange(len(nm[p])) for p in mix.KIT_PARTS], [e + 1 for e in range(len(xn)) if rnd.random() < 0.5])
        diffs = sorted(mix.look_difference(pick(), pick()) for _ in range(150))
        diffs = [d for d in diffs if d > 0] or [0]
        figs = []
        for args in show:
            args = tuple(min(a, len(nm[p]) - 1) for a, p in zip(args[:4], mix.KIT_PARTS)) + (args[4],)
            v = mix.apply_kit(base, *args)
            v["dimensions"] = False
            f = v["kit_fitted"]
            v["name"] = base["brand"].title() + " kit build"
            cap = f"{f['tank_seat']} tank and seat, {f['tail']} tail, {f['front']} front, {f['exhaust']} exhaust" + ("".join(", " + x for x in f["extras"]) if f["extras"] else "")
            figs.append(f'<figure class="sheet">{elevation.draw(v)}<figcaption>{cap}.</figcaption></figure>')
        def menu(p, label):
            notes = ["as drawn above"] + [v["note"] for v in kit[p]]
            return f"<li><b>{label}:</b> " + "; ".join(f"{a} ({b})" for a, b in zip(nm[p], notes)) + ".</li>"
        kit_html.append(f"""<h3>{base['brand'].title()} kit</h3><ul>{menu("tank_seat", "Tank and seat pairs")}{menu("tail", "Tails")}{menu("front", "Front ends")}{menu("exhaust", "Exhausts")}<li><b>Optional extras:</b> {"; ".join(xn)}.</li></ul><p>That gives {count} different bikes on this one frame, before colour and before any part from another brand. Every choice passes the fit and clearance checks. Measured side on over the parts a kit can change, two bikes picked at random differ by {diffs[len(diffs) // 2] * 100:.0f}% on average (from {diffs[0] * 100:.0f}% to {diffs[-1] * 100:.0f}%). Six of them:</p><div class="shots">{"".join(figs)}</div>""")
    if kit_html:
        h.append('<section><h2>Kits: the same frame, different bikes</h2><p class="lede">Each model gets its own choice of tank and seat pairs, tails, front ends and exhausts, plus optional extras. This is what stops two people who pick the same frame from ending up with the same bike. The tank and seat pairs also fit the other brand\'s frame.</p>' + "".join(kit_html) + "</section>")
    loaded = [elevation.load(n) for n in names]
    pairs = [(a, b) for a in loaded for b in loaded if a is not b]
    if pairs:
        h.append("""<section><h2>Mixed builds</h2><p class="lede">One brand's frame and engine with the other brand's tank and seat, straight from the two files above with nothing redrawn by hand. The parts sit on the hidden mounting deck that every frame in the family shares, and a hidden pan under each seat covers any difference in tail line.</p>""")
        for a, b in pairs:
            mm = mix.mix(a, b)
            li = "".join(f"<li class='{'' if ok else 'bad'}'>{msg}</li>" for ok, msg in checks.deck(mm, fams))
            h.append(f"""<div class="sheet" style="margin-bottom:14px">{elevation.draw(mm)}<ul class="checks">{li}</ul></div>""")
        h.append("</section>")
    h.append(kit_renders(names))
    h.append(swatch_section())
    h.append("""<section><h2>What these drawings are, and are not yet</h2>
<ul class="plain"><li>They are drawn to scale from real numbers: wheelbase, rake, trail, seat height, wheel sizes and every frame tube.</li>
<li>The automatic checks under each drawing run on every change. Three came from your comments: the front tyre must clear the engine and radiator with the fork fully compressed, the middle of the bike may not be more than 12% empty space, and a naked tank must be flat underneath.</li>
<li>Changed from your 13 Nov markup: flat tank bottoms, shorter front mudguards with a fork shroud, the rear mudguard either a short hugger on the swingarm (Corvano) or formed under the tail (Kazeda), the Corvano engine moved forward with a curving exhaust and a high silencer each side, and the plate hanger made a separate optional part.</li>
<li>The 3D pictures are a first rough build to prove that drawing and model agree. Shapes are still blocky. Refining the 3D is the next job.</li>
<li>The mixed builds are the first proof of the mounting standard.</li>
<li>Not yet drawn: top and front views.</li></ul></section></div>""")
    out = os.path.join(ROOT, "out", f"{family}_elevations.html")
    open(out, "w").write("\n".join(h))
    return out


if __name__ == "__main__":
    print(build(sys.argv[1], sys.argv[2:]))
