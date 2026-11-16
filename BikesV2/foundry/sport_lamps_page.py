"""Builds the sport headlights review page (out/sport_headlights.html): each brand's signature lamp on its sport bike."""
import base64, io, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Sport Headlights</title>')
SPORT = [('kazeda_sport', 'Kazeda sport', 'Slanted eyes: one projector with a halo ring inboard, a fluted reflector outboard, a running light blade along the top edge.'),
         ('corvano_sport', 'Corvano sport', 'A V face: twin projectors each side under a long running light blade that sweeps up and out.'),
         ('hartwell_sport', 'BSX sport', 'A projector pair with halo rings each side, a short running light underneath.'),
         ('varga_sport', 'Varga sport', 'A tall narrow unit close to the middle: stacked LED cells over one projector, the running light up the outer edge.')]
ADV = [('corvano_adventure', 'Corvano adventure (lead)', ''), ('hartwell_adventure', 'BSX adventure', ''), ('kazeda_adventure', 'Kazeda adventure', ''),
       ('rheinhart_adventure', 'Rheinhart adventure', ''), ('varga_adventure', 'Varga adventure', '')]
body = '<main>\n<div class="k">Bike Models V2 · Sport and adventure · Headlight units</div>\n<h1>Headlights as real units behind the fairing</h1>\n<p class="lede">The painted lamps are gone. Each headlight is now a separate unit: a housing, a fluted or faceted reflector, projector lenses in shrouds with halo rings, a running light guide and a clear outer lens. The fairing has a real opening cut in it and the unit sits in the hole with a dark surround. Rendered straight from the models at low quality, nothing retouched.</p>\n'
for head_, bikes, shots in (('Sport bikes', SPORT, (('lampunit', 'lamp close-up'), ('nose', 'nose'))), ('Adventure bikes', ADV, (('advlamp', 'lamp close-up'), ('front34', 'front three-quarter')))):
    body += f'<h2>{head_}</h2>\n<div class="grid">'
    for nm, title, note in bikes:
        for tag, lab in shots:
            p = os.path.join(ROOT, 'out', 'renders', f'{nm}_def_shot_{tag}.png')
            if not os.path.exists(p):
                continue
            im = Image.open(p).convert('RGB')
            im = im.resize((1100, int(im.height * 1100 / im.width)))
            b = io.BytesIO()
            im.save(b, 'JPEG', quality=82)
            body += f'<figure><img alt="{title}, {lab}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> {lab}. {note}</figcaption></figure>\n'
    body += '</div>\n'
body += '<h2>Single-sided swingarms</h2>\n<div class="grid">'
for nm, title in (('corvano_naked', 'Corvano naked'), ('corvano_sport', 'Corvano sport')):
    p = os.path.join(ROOT, 'out', 'renders', f'{nm}_def_shot_rearq.png')
    if os.path.exists(p):
        im = Image.open(p).convert('RGB')
        im = im.resize((1100, int(im.height * 1100 / im.width)))
        b = io.BytesIO()
        im.save(b, 'JPEG', quality=82)
        body += f'<figure><img alt="{title}, rear" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> one-legged arm again, rear disc, caliper and hose moved to the arm side so nothing hangs.</figcaption></figure>\n'
open(os.path.join(ROOT, 'out', 'sport_headlights.html'), 'w').write(head + body + '</div>\n</main>')
