#!/usr/bin/env python3
"""The six makers' glossy sales brochures, one PDF each, and their logo posters for the shed walls.

    python3 BikesV2/foundry/brochures.py [brand ...] [--posters]

Each brochure is written as the maker would write it (brochure_brands.py): cover, a letter to the customer,
the company's story, why to buy, the range, then every bike. beenster's own sketches are the main pictures:
every PNG in SKETCHES whose name holds "<brand> <family>" leads that bike's pages (BSX is hartwell, "chop" is
chopper); bikes with no sketch yet fall back on studio renders. Pages are HTML laid out by Chromium.
"""
import glob, html, json, os, re, subprocess, sys
from PIL import Image
from brochure_brands import BRANDS

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SHARE = "/mnt/project-files/bike_models_v2/"
SKETCHES = SHARE + "portfolio_sketches/"
RENDERS = SHARE + "portfolio_renders/_src/"
OUT = SHARE + "brochures/"
WORK = os.environ.get("BROCHURE_WORK", "/tmp/brochures_work")

ORDER = ["naked", "sport", "cafe", "classic", "adventure", "mx", "cruiser", "chopper"]
FIG = {"adventure": (900, 90, 6200, 8800), "cafe": (750, 75, 6700, 9600), "classic": (750, 68, 5300, 7700),
       "cruiser": (1200, 114, 5200, 8200), "mx": (450, 47, 8000, 11000), "naked": (850, 87, 7500, 10500),
       "sport": (600, 59, 10000, 14000), "chopper": (1800, 158, 4000, 5800)}
FAMILY = {"naked": "Naked", "sport": "Sport", "cafe": "Café racer", "classic": "Classic", "adventure": "Adventure",
          "mx": "Off-road", "cruiser": "Cruiser", "chopper": "Custom"}
KIT = [("tank_seat", "Tank and seat"), ("tail", "Tail"), ("front", "Front"), ("bars", "Handlebars"), ("exhaust", "Exhaust"),
       ("paint", "Colours"), ("extras", "Accessories")]
IMPERIAL = ("hartwell", "lodestar")
E = html.escape


def sketches():
    found = {}
    for f in sorted(glob.glob(SKETCHES + "*.png") + glob.glob(SKETCHES + "*.jpg")):
        n = os.path.basename(f).lower().replace("bsx", "hartwell").replace("chop", "chopper").replace("chopperper", "chopper")
        m = re.match(r"\s*([a-z]+)[ _-]*([a-z]+)", n)
        if m and m.group(1) in BRANDS and m.group(2) in FIG:
            found.setdefault(m.group(1) + "_" + m.group(2), []).append(f)
    return found


def prep_sketch(f):
    out = os.path.join(WORK, "img", re.sub(r"[^a-z0-9]+", "_", os.path.basename(f).lower())[:-4] + ".jpg")
    if not os.path.exists(out):
        Image.open(f).convert("RGB").save(out, quality=90)
    return out


def prep_render(model, shot, paper, width=1700):
    cands = sorted(glob.glob(RENDERS + f"{model}_def_shot_{shot}*.png"), key=len)
    if not cands:
        return None
    out = os.path.join(WORK, "img", f"{model}_{shot}_{paper.strip('#')}.jpg")
    if not os.path.exists(out):
        im = Image.open(cands[0]).convert("RGBA")
        box = im.split()[3].getbbox()
        pad = 40
        im = im.crop((max(0, box[0] - pad), max(0, box[1] - pad), min(im.width, box[2] + pad), min(im.height, box[3] + pad)))
        bg = Image.new("RGBA", im.size, paper)
        bg.alpha_composite(im)
        bg = bg.convert("RGB")
        if bg.width > width:
            bg = bg.resize((width, round(bg.height * width / bg.width)), Image.LANCZOS)
        bg.save(out, quality=90)
    return out


def specs(bid, model, fam):
    cc, nm, at, red = FIG[fam]
    g = model["geometry"]
    imp = bid in IMPERIAL
    mm = lambda v: f"{v:,.0f} mm" + (f" ({v / 25.4:.1f} in)" if imp else "")
    eng = model["engine_label"].replace(" (boxer), shaft drive", " (boxer)").replace("air-cooled look ", "")
    frame = model["frame_label"].split(",")[0]
    return [("Engine", eng[0].upper() + eng[1:]), ("Capacity", f"{cc:,} cc" + (f" ({cc / 16.387:.0f} cu in)" if bid == "lodestar" else "")),
            ("Peak torque", f"{nm} Nm at {at:,} rpm" if not imp else f"{nm} Nm ({nm * 0.7376:.0f} lb ft) at {at:,} rpm"),
            ("Red line", f"{red:,} rpm"), ("Frame", frame[0].upper() + frame[1:]),
            ("Final drive", "Shaft" if bid == "rheinhart" else "Chain"),
            ("Wheelbase", mm(g["wheelbase"])), ("Rake and trail", f"{g['rake']:g}° / {g['trail']} mm"), ("Seat height", mm(g["seat_height"]))]


def options(model):
    rows = []
    for key, label in KIT:
        v = model.get("kit", {}).get(key)
        if isinstance(v, list) and v:
            names = [o.get("name") if isinstance(o, dict) else str(o) for o in v]
            rows.append((label, ", ".join(n for n in names if n)))
    return rows


CSS = """
@page { size: 297mm 210mm; margin: 0 }
* { box-sizing: border-box; margin: 0; padding: 0 }
html, body { background: var(--paper); color: var(--ink); font-family: var(--text); font-size: 10.6pt; line-height: 1.5; -webkit-print-color-adjust: exact; print-color-adjust: exact }
.page { width: 297mm; height: 210mm; position: relative; overflow: hidden; page-break-after: always; background: var(--paper) }
.d { font-family: var(--display); font-style: var(--dstyle); font-weight: var(--dweight); text-transform: var(--dcase); letter-spacing: var(--dtrack); line-height: 1.02 }
.lab { font-size: 7.4pt; letter-spacing: .24em; text-transform: uppercase; font-weight: 600 }
.acc { color: var(--accent) }
img { display: block }
.folio { position: absolute; bottom: 8mm; left: 16mm; right: 16mm; display: flex; justify-content: space-between; font-size: 7pt; letter-spacing: .22em; text-transform: uppercase; opacity: .62 }
/* cover */
.cover { background: var(--cover); color: var(--cover-ink) }
.cover .panel { position: absolute; left: 0; top: 0; bottom: 0; width: 104mm; padding: 20mm 16mm; display: flex; flex-direction: column; justify-content: space-between }
.cover .pic { position: absolute; left: 104mm; right: 0; top: 0; bottom: 0; background: var(--paper) }
.cover .pic img { width: 100%; height: 100%; object-fit: cover }
.cover .pic.render img { object-fit: contain; padding: 14mm }
.cover .logo svg { width: var(--logo-w); max-width: 72mm; height: auto }
.cover h1 { font-size: calc(30pt * var(--dsize)); margin-bottom: 4mm }
.cover .rule { width: 22mm; height: 1.2mm; background: var(--cover-rule); margin: 0 0 6mm }
/* letter */
.letter .left { position: absolute; left: 0; top: 0; bottom: 0; width: 112mm; background: var(--accent); color: var(--on-accent); padding: 26mm 16mm 20mm; display: flex; flex-direction: column; justify-content: space-between }
.letter .left .q { font-size: calc(23pt * var(--dsize)); line-height: 1.16 }
.letter .right { position: absolute; left: 112mm; right: 0; top: 0; bottom: 0; padding: 0 30mm 0 22mm; display: flex; flex-direction: column; justify-content: center }
.letter p { margin-bottom: 4.2mm; font-size: 11.6pt; line-height: 1.58 }
.sig { font-family: 'Mr Dafoe', cursive; font-size: 32pt; line-height: 1; margin: 5mm 0 2mm; color: var(--ink); transform: rotate(-3deg); transform-origin: left }
/* history */
.hist .left { position: absolute; left: 16mm; top: 0; bottom: 0; width: 152mm; display: flex; flex-direction: column; justify-content: center }
.hist h2, .why h2, .range h2 { font-size: calc(32pt * var(--dsize)); margin: 3mm 0 9mm }
.hist .row { display: grid; grid-template-columns: 26mm 1fr; gap: 0 5mm; padding: 4.6mm 0; font-size: 11pt; border-top: .3mm solid color-mix(in srgb, var(--ink) 22%, transparent) }
.hist .yr { font-size: calc(19pt * var(--dsize)); color: var(--accent) }
.hist .pic { position: absolute; right: 0; top: 0; bottom: 0; width: 112mm; background: var(--paper) }
.hist .pic img { width: 100%; height: 100%; object-fit: cover; object-position: var(--hist-pos, 0 50%) }
.hist .pic.render img { object-fit: contain; padding: 8mm }
/* why */
.why { padding: 24mm 16mm }
.why .grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 10mm; margin-top: 8mm; font-size: 11.4pt }
.why .n { font-size: calc(54pt * var(--dsize)); color: var(--accent); line-height: 1 }
.why h3 { font-size: calc(19pt * var(--dsize)); margin: 6mm 0 4mm; padding-top: 5mm; border-top: .5mm solid var(--ink) }
.why .care { position: absolute; left: 16mm; right: 16mm; bottom: 22mm; display: grid; grid-template-columns: 62mm 1fr; gap: 10mm; padding: 9mm 10mm; background: var(--ink); color: var(--paper); align-items: center }
.why .care .d { font-size: calc(17pt * var(--dsize)) }
.why .care p { font-size: 11.6pt }
/* range */
.range { padding: 18mm 16mm }
.range .grid { display: grid; gap: 5mm 7mm }
.range .card img { width: 100%; height: var(--card-h); object-fit: contain }
.range .card .d { font-size: calc(14pt * var(--dsize)); margin-top: 1.5mm }
.range .card { border-top: .5mm solid var(--ink); padding-top: 2mm }
/* bike hero */
.hero .pic { position: absolute; left: 0; top: 0; width: 297mm; height: 198mm }
.hero .pic img { width: 100%; height: 100%; object-fit: cover }
.hero .strip { position: absolute; left: 0; right: 0; bottom: 0; height: 12mm; background: var(--ink); color: var(--paper); display: flex; align-items: center; justify-content: space-between; padding: 0 16mm }
.hero .strip .d { font-size: calc(15pt * var(--dsize)) }
.hero.render .ghost { position: absolute; left: 14mm; top: 12mm; right: 0; z-index: 2; font-size: calc(64pt * var(--dsize)); color: var(--ink); white-space: nowrap; line-height: .95 }
.hero.render .pic { top: 40mm; height: 150mm; left: 60mm; width: 225mm }
.hero.render .pic img { object-fit: contain }
.hero.render .tag { position: absolute; left: 16mm; top: 44mm; z-index: 2; font-size: calc(19pt * var(--dsize)); max-width: 120mm }
/* bike detail */
.bike .left { position: absolute; left: 16mm; top: 16mm; width: 94mm; bottom: 18mm; display: flex; flex-direction: column }
.bike h2 { font-size: calc(33pt * var(--dsize)); margin: 2mm 0 2mm }
.bike .tag { font-size: calc(13pt * var(--dsize)); color: var(--accent); margin-bottom: 5mm }
.bike p { margin-bottom: 2.6mm }
.bike .opts { margin-top: 3mm; border-top: .5mm solid var(--ink); padding-top: 2.4mm; font-size: 9pt }
.bike .opts div { display: grid; grid-template-columns: 25mm 1fr; padding: .7mm 0 }
.bike .opts b { font-weight: 600 }
.bike .small { margin-top: auto; display: flex; gap: 3mm; align-items: flex-end }
.bike .small img { width: 100%; max-height: 38mm; object-fit: contain; object-position: left bottom }
.bike .right { position: absolute; left: 120mm; top: 16mm; right: 12mm }
.bike .right .main { width: 165mm; height: 110mm; object-fit: cover; box-shadow: 0 1.2mm 5mm rgba(0,0,0,.16) }
.bike .right .main.render { object-fit: contain; box-shadow: none }
.bike .cap { font-size: 7pt; letter-spacing: .2em; text-transform: uppercase; opacity: .6; margin: 2mm 0 0 }
.bike .spec { position: absolute; left: 120mm; right: 12mm; top: 138mm; display: grid; grid-template-columns: repeat(3, 1fr); gap: 0 6mm }
.bike .spec div { border-top: .3mm solid color-mix(in srgb, var(--ink) 30%, transparent); padding: 2mm 0 3.4mm }
.bike .spec b { display: block; font-size: 11.6pt; font-weight: 600; line-height: 1.25 }
/* back */
.back { background: var(--cover); color: var(--cover-ink); display: flex; flex-direction: column; align-items: center; justify-content: center; text-align: center }
.back .logo svg { width: calc(var(--logo-w) * 1.1); height: auto }
.back .d { font-size: calc(24pt * var(--dsize)); margin: 12mm 0 3mm }
.back .small { position: absolute; bottom: 12mm; left: 40mm; right: 40mm; font-size: 6.6pt; opacity: .6; line-height: 1.5 }
/* poster */
.poster { width: 600px; height: 850px; background: var(--cover); color: var(--cover-ink); position: relative; overflow: hidden; display: flex; flex-direction: column; align-items: center; justify-content: center; text-align: center }
.poster .logo svg { width: 440px; height: auto }
.poster .d { font-size: calc(40px * var(--dsize)); margin-top: 60px; padding: 0 40px }
.poster .lab { position: absolute; bottom: 44px; left: 0; right: 0; font-size: 12px }
.poster .frame { position: absolute; inset: 22px; border: 2px solid var(--cover-rule) }
.poster.pic { justify-content: flex-start; background: var(--paper); color: var(--ink) }
.poster.pic img { width: 600px; height: 400px; object-fit: cover; margin-top: 140px }
.poster.pic .logo { position: absolute; top: 36px } .poster.pic .logo svg { width: 250px; max-height: 96px }
.poster.pic .d { margin-top: 44px; font-size: calc(46px * var(--dsize)) }
.poster.pic .t { font-size: 19px; margin-top: 14px; color: var(--accent) }
"""


def fonts_css(b):
    faces = list(b["fonts"]) + [("Mr Dafoe", "fonts/brochure/MrDafoe-Regular.ttf", "400", "normal")]
    return "".join(f"@font-face{{font-family:'{f}';src:url('file://{HERE}/{p}');font-weight:{w};font-style:{s}}}" for f, p, w, s in faces)


def theme(b):
    on_accent = b.get("on_accent", "#FFFFFF" if b["accent"] != "#F26A1B" else b["ink"])
    v = {"paper": b["paper"], "ink": b["ink"], "accent": b["accent"], "cover": b["cover"], "cover-ink": b["cover_ink"],
         "cover-rule": b.get("cover_rule", b["metal"]), "on-accent": on_accent, "display": b["display"], "dstyle": b["dstyle"],
         "dweight": b["dweight"], "dcase": b["dcase"], "dtrack": b["dtrack"], "dsize": b["dsize"], "text": b["text"],
         "logo-w": f"{b['logo_w']}mm" if b["logo_w"] > 80 else f"{b['logo_w'] * 0.8}mm"}
    return ":root{" + ";".join(f"--{k}:{x}" for k, x in v.items()) + "}"


def cover_logo(bid, b, word=True):
    c = {"hartwell": (b["cover_ink"], "#163A2B", "#E9DFC4"), "varga": (b["ink"], "#FFFFFF", b["metal"]),
         "lodestar": (b["cover_ink"], "#B5392F", b["metal"]), "corvano": (b["cover_ink"], "#D0213B", b["metal"]),
         "kazeda": (b["cover_ink"], "#3F74F0", b["metal"])}.get(bid, (b["ink"], b["accent"], b["metal"]))
    return b["logo"](*c, word=word)


def build(bid, sk):
    b = BRANDS[bid]
    models = {}
    for fam in ORDER:
        p = os.path.join(ROOT, "data", "models", f"{bid}_{fam}.json")
        if os.path.exists(p) and fam in b["bikes"]:
            models[fam] = json.load(open(p))
    pic = lambda f: "file://" + f
    brand_sk = [s for fam in models for s in sk.get(f"{bid}_{fam}", [])]
    P = []
    folio = lambda n, t: f'<div class="folio"><span>{E(b["name"])} · {E(b["year"])}</span><span>{E(t)} · {n:02d}</span></div>'

    # cover
    cov = b.get("cover_sketch") and SKETCHES + b["cover_sketch"]
    cov = cov if cov and os.path.exists(cov) else (brand_sk[0] if brand_sk else None)
    cpic = (f'<div class="pic"><img src="{pic(prep_sketch(cov))}"></div>' if cov else
            f'<div class="pic render"><img src="{pic(prep_render(bid + "_" + list(models)[0], "front34", b["paper"]))}"></div>')
    P.append(f'<section class="page cover"><div class="panel"><div class="logo">{cover_logo(bid, b)}</div>'
             f'<div><div class="rule"></div><h1 class="d">{E(b["strap"])}</h1><div>{E(b["strap_sub"])}</div></div>'
             f'<div class="lab">{E(b["year"])}</div></div>{cpic}</section>')

    # letter
    paras = "".join(f"<p>{t}</p>" for t in b["letter"])
    P.append(f'<section class="page letter"><div class="left"><div class="lab">A letter to you</div>'
             f'<div class="d q">“{E(b["quote"])}”</div><div class="lab">{E(b["country"])}</div></div>'
             f'<div class="right">{paras}<div class="sig">{E(b["signer"])}</div><div><b>{E(b["signer"])}</b><br>{E(b["role"])}</div></div></section>')

    # history
    rows = "".join(f'<div class="row"><div class="d yr">{y}</div><div>{E(t)}</div></div>' for y, t in b["history"])
    hsk = brand_sk[1] if len(brand_sk) > 1 else None
    hpic = (f'<div class="pic"><img src="{pic(prep_sketch(hsk))}"></div>' if hsk else
            f'<div class="pic render"><img src="{pic(prep_render(bid + "_" + list(models)[-1], "rear34", b["paper"]))}"></div>')
    P.append(f'<section class="page hist"><div class="left"><div class="lab acc">Our story</div><h2 class="d">{E(b["history_title"])}</h2>{rows}</div>{hpic}</section>')

    # why
    cols = "".join(f'<div><div class="d n">{i + 1:02d}</div><h3 class="d">{E(t)}</h3><p>{E(x)}</p></div>' for i, (t, x) in enumerate(b["why"]))
    P.append(f'<section class="page why"><div class="lab acc">{E(b["strap"])}</div><h2 class="d">{E(b["why_title"])}</h2><div class="grid">{cols}</div>'
             f'<div class="care"><div class="d">Yours, and looked after</div><p>{E(b["care"])}</p></div>{folio(4, b["why_title"])}</section>')

    # range
    n = len(models)
    ncol = 4 if n > 6 else 3 if n > 4 else 2
    nrow = -(-n // ncol)
    card_h = {1: 100, 2: 52, 3: 30}[nrow] if ncol > 2 else 50
    cards = ""
    for fam, m in models.items():
        name, tag, _ = b["bikes"][fam]
        cards += (f'<div class="card"><div class="lab acc">{E(FAMILY[fam])}</div><img src="{pic(prep_render(bid + "_" + fam, "side", b["paper"], 900))}">'
                  f'<div class="d">{E(name)}</div><div>{E(tag)}</div></div>')
    P.append(f'<section class="page range" style="--card-h:{card_h}mm"><div class="lab acc">{E(b["year"])}</div><h2 class="d">{E(b["range_title"])}</h2>'
             f'<div class="grid" style="grid-template-columns:repeat({ncol},1fr)">{cards}</div>{folio(5, b["range_title"])}</section>')

    # bikes
    page = 6
    for fam, m in models.items():
        mid = f"{bid}_{fam}"
        name, tag, body = b["bikes"][fam]
        s = sk.get(mid, [])
        mark = cover_logo(bid, dict(b, cover_ink=b["paper"]), word=False) if bid not in ("hartwell", "lodestar") else ""
        if s:
            P.append(f'<section class="page hero"><div class="pic"><img src="{pic(prep_sketch(s[0]))}"></div>'
                     f'<div class="strip"><span class="d">{E(name)}</span><span class="lab">{E(FAMILY[fam])} · From the design studio</span></div></section>')
        else:
            P.append(f'<section class="page hero render"><div class="d ghost">{E(name)}</div>'
                     f'<div class="pic"><img src="{pic(prep_render(mid, "front34", b["paper"]))}"></div>'
                     f'<div class="d tag acc">{E(tag)}</div>'
                     f'<div class="strip"><span class="d">{E(name)}</span><span class="lab">{E(FAMILY[fam])}</span></div></section>')
        if len(s) > 1:
            main = f'<img class="main" src="{pic(prep_sketch(s[1]))}"><div class="cap">Design study, {E(name)}</div>'
            small = [prep_render(mid, "side", b["paper"], 1000)]
        elif s:
            main = f'<img class="main render" src="{pic(prep_render(mid, "side", b["paper"]))}"><div class="cap">{E(name)}</div>'
            small = [prep_render(mid, "rear34", b["paper"], 1000)]
        else:
            main = f'<img class="main render" src="{pic(prep_render(mid, "side", b["paper"]))}"><div class="cap">{E(name)}</div>'
            small = [prep_render(mid, "rear34", b["paper"], 800), prep_render(mid, "low_front", b["paper"], 800)]
        opts = "".join(f"<div><b>{E(k)}</b><span>{E(v)}</span></div>" for k, v in options(m))
        sp = "".join(f'<div><span class="lab">{E(k)}</span><b>{E(v)}</b></div>' for k, v in specs(bid, m, fam))
        smalls = "".join(f'<img src="{pic(x)}" style="width:{100 // len(small)}%">' for x in small if x)
        P.append(f'<section class="page bike"><div class="left"><div class="lab acc">{E(FAMILY[fam])}</div><h2 class="d">{E(name)}</h2>'
                 f'<div class="d tag">{E(tag)}</div>' + "".join(f"<p>{E(t)}</p>" for t in body) +
                 f'<div class="opts"><div class="lab acc" style="display:block;margin-bottom:1.5mm">Factory options</div>{opts}</div>'
                 f'<div class="small">{smalls}</div></div>'
                 f'<div class="right">{main}</div><div class="spec">{sp}</div>{folio(page + 1, name)}</section>')
        page += 2

    # back cover
    P.append(f'<section class="page back"><div class="logo">{cover_logo(bid, b)}</div><div class="d">{E(b["strap"])}</div><div>{E(b["country"])}</div>'
             f'<div class="small">Sketches are design studies from the {E(b["name"])} studio and may show details that differ from production machines. '
             f'Specifications, colours and options vary by market and may change without notice. Ride responsibly and always wear a helmet and protective clothing. '
             f'© 2027 {E(b["role"].split(", ")[-1])}.</div></section>')

    head = f'<!doctype html><html><head><meta charset="utf-8"><style>{fonts_css(b)}{theme(b)}{CSS}</style></head><body>'
    path = os.path.join(WORK, f"{b['file']}.html")
    open(path, "w").write(head + "".join(P) + "</body></html>")

    # posters: the logo with the strapline, and one per sketched bike
    posters = [("logo", f'<div class="poster"><div class="frame"></div><div class="logo">{cover_logo(bid, b)}</div><div class="d">{E(b["strap"])}</div>'
                        f'<div class="lab">{E(b["country"])}</div></div>')]
    for fam in models:
        s = sk.get(f"{bid}_{fam}", [])
        if s:
            name, tag, _ = b["bikes"][fam]
            lg = b["logo"](b["ink"], b["accent"], b["metal"] if bid != "hartwell" else "#E9DFC4")
            posters.append((fam, f'<div class="poster pic"><div class="logo">{lg}</div><img src="{pic(prep_sketch(s[0]))}">'
                                 f'<div class="d">{E(name)}</div><div class="t">{E(tag)}</div><div class="lab">{E(b["strap"])}</div></div>'))
    pp = []
    for key, body in posters:
        q = os.path.join(WORK, f"poster_{b['file'].lower()}_{key}.html")
        open(q, "w").write(head + body + "</body></html>")
        pp.append(q)
    return path, pp


NODE = """
const { chromium } = require('playwright');
(async () => {
  const jobs = JSON.parse(process.argv[2]);
  const br = await chromium.launch();
  for (const j of jobs) {
    const pg = await br.newPage({ viewport: { width: 600, height: 850 }, deviceScaleFactor: 2 });
    await pg.goto('file://' + j.src); await pg.evaluate(() => document.fonts.ready);
    if (j.pdf) await pg.pdf({ path: j.pdf, width: '297mm', height: '210mm', printBackground: true, preferCSSPageSize: true });
    else { for (let t = 0; t < 4; t++) { try { await pg.screenshot({ path: j.png, clip: { x: 0, y: 0, width: 600, height: 850 } }); break; } catch (e) { if (t == 3) throw e; await pg.waitForTimeout(1500); } } }
    await pg.close();
  }
  await br.close();
})();
"""


def main():
    want = [a for a in sys.argv[1:] if not a.startswith("-")] or list(BRANDS)
    os.makedirs(os.path.join(WORK, "img"), exist_ok=True)
    os.makedirs(OUT + "posters", exist_ok=True)
    sk = sketches()
    jobs = []
    for bid in want:
        path, posters = build(bid, sk)
        jobs.append({"src": path, "pdf": OUT + BRANDS[bid]["file"] + "_2027.pdf"})
        jobs += [{"src": q, "png": OUT + "posters/" + os.path.basename(q)[7:-5] + ".png"} for q in posters]
    js = os.path.join(WORK, "render.js")
    open(js, "w").write(NODE)
    subprocess.run(["node", js, json.dumps(jobs)], check=True)
    for j in jobs:
        print(j.get("pdf") or j.get("png"))


if __name__ == "__main__":
    main()
