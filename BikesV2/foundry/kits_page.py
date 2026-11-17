"""Builds the kit variants contact sheet (out/kit_variants.html): each lead bike with its kit choices fitted, in the Definition pass."""
import base64, io, json, os, sys
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mix

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Kit Variants</title>')
BIKES = [('corvano_naked', 'Corvano naked', ('1111', '2222'), 'side'), ('hartwell_classic', 'BSX classic', ('1111', '2222'), 'side'),
         ('lodestar_cruiser', 'Lodestar cruiser', ('1111', '2222'), 'side'), ('corvano_adventure', 'Corvano adventure', ('1111', '2222'), 'side'),
         ('lodestar_chopper', 'Lodestar chopper', ('1111', '2222', '3303'), 'side_long'), ('varga_mx', 'Varga MX', ('1111', '2222'), 'side'),
         ('kazeda_sport', 'Kazeda sport (new kit)', ('1111x1x2', '2222'), 'side'), ('hartwell_cafe', 'BSX cafe (new kit)', ('1111x1x2', '2222'), 'side')]
NOTES = json.load(open(os.path.join(ROOT, 'data', 'kit_notes.json'))) if os.path.exists(os.path.join(ROOT, 'data', 'kit_notes.json')) else {}
body = '<main>\n<div class="k">Bike Models V2 · Kits · First look in full detail</div>\n<h1>Kit variants on the lead bikes</h1>\n<p class="lede">Every bike carries a kit: other tank and seat pairs, tails, front ends and exhausts that were approved as outline drawings on 14 Nov, before the detail work. The sport and cafe bikes got their kits on 17 Nov (the last four pictures), so all 34 bikes now carry one. All the kit variants build in the detailed pass without an error. These pictures fit each lead bike with its first, second (and third) choice of every kit part at once, so the shapes can be judged against the signed-off bikes. Rendered at low quality, nothing retouched.</p>\n<div class="grid">'
for nm, title, kits, shot in BIKES:
    m = json.load(open(os.path.join(ROOT, 'data', 'models', nm + '.json')))
    for k in kits:
        p = os.path.join(ROOT, 'out', 'renders', f'{nm}_kit{k}_def_shot_{shot}.png')
        if not os.path.exists(p):
            continue
        names = [mix.kit_names(m, g)[int(i)] for g, i in zip(mix.KIT_PARTS, k) if int(i) < len(mix.kit_names(m, g))]
        im = Image.open(p).convert('RGB')
        im = im.resize((1100, int(im.height * 1100 / im.width)))
        b = io.BytesIO()
        im.save(b, 'JPEG', quality=82)
        note = NOTES.get(f'{nm}_{k}', '')
        body += f'<figure><img alt="{title} kit {k}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}, choice {k[0]}</strong> Tank and seat: {names[0]}. Tail: {names[1]}. Front: {names[2]}. Exhaust: {names[3]}. {note}</figcaption></figure>\n'
for k, t in (('1111', 'Basso exhaust: a short silencer low behind the engine.'), ('2222', 'Alto exhaust: one silencer swept up high.'), ('0003', 'Doppio exhaust: two short stacked silencers.')):
    p = os.path.join(ROOT, 'out', 'renders', f'corvano_naked_kit{k}_def_shot_rearq_l.png')
    if os.path.exists(p):
        im = Image.open(p).convert('RGB')
        im = im.resize((1100, int(im.height * 1100 / im.width)))
        b = io.BytesIO()
        im.save(b, 'JPEG', quality=82)
        body += f'<figure><img alt="Corvano naked from the left, kit {k}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>Corvano naked from the left</strong> {t} The Corvano exhausts sit on the swingarm side.</figcaption></figure>\n'
for fn, cap in (('varga_mx_kit0001_def_shot_rearq.png', '<strong>Varga MX, kit exhaust from behind</strong> The kit silencers now run inside the side plate like the standard pipe, so nothing pokes through the plate.'),
                ('hartwell_cafe_kit0020_def_shot_front34.png', '<strong>BSX cafe, second front choice</strong> The squared LED lamp in its black housing.')):
    p = os.path.join(ROOT, 'out', 'renders', fn)
    if os.path.exists(p):
        im = Image.open(p).convert('RGB')
        im = im.resize((1100, int(im.height * 1100 / im.width)))
        b = io.BytesIO()
        im.save(b, 'JPEG', quality=82)
        body += f'<figure><img alt="{fn}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption>{cap}</figcaption></figure>\n'
open(os.path.join(ROOT, 'out', 'kit_variants.html'), 'w').write(head + body + '</div>\n</main>')
