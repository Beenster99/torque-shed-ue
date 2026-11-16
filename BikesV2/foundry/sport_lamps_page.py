"""Builds the sport headlights review page (out/sport_headlights.html): each brand's signature lamp on its sport bike."""
import base64, io, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Sport Headlights</title>')
BIKES = [('kazeda_sport', 'Kazeda (lead, signed off, unchanged)', 'A long slanted eye each side that sweeps out and up, a running light along its top edge.'),
         ('corvano_sport', 'Corvano', 'A rounded pod each side with two projectors under a bright eyebrow.'),
         ('hartwell_sport', 'BSX', 'One long level blade each side with a fine running light underneath.'),
         ('varga_sport', 'Varga', 'A tall wedge close to the middle, lamps stacked, the running light up its outer edge.')]
body = '<main>\n<div class="k">Bike Models V2 · Sport · Headlights</div>\n<h1>A signature headlight for each sport bike</h1>\n<p class="lede">The four sport bikes shared the lead\'s headlight. Each brand now has its own, let into the same nose. Rendered straight from the models at low quality, nothing retouched. Tell me which to keep or change by brand.</p>\n<div class="grid">'
for nm, title, note in BIKES:
    for tag, lab in (('nose', 'nose close-up'), ('front34', 'front three-quarter')):
        p = os.path.join(ROOT, 'out', 'renders', f'{nm}_def_shot_{tag}.png')
        if not os.path.exists(p):
            continue
        im = Image.open(p).convert('RGB')
        im = im.resize((1100, int(im.height * 1100 / im.width)))
        b = io.BytesIO()
        im.save(b, 'JPEG', quality=82)
        body += f'<figure><img alt="{title}, {lab}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> {lab}. {note}</figcaption></figure>\n'
open(os.path.join(ROOT, 'out', 'sport_headlights.html'), 'w').write(head + body + '</div>\n</main>')
