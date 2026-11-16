"""Builds the chopper bespoke parts review page (out/chopper_parts.html)."""
import base64, io, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Chopper Parts</title>')
PICS = [('pegs.png', 'Footpegs', 'Rows: Ringed, Torpedo, Faceted. Columns: chrome, brushed steel, gold, brass. All hang on the same pivot, so the boot sits exactly where it did.'),
        ('covers.png', 'Engine covers', 'Rows: Finned, Starburst, Target. Columns: chrome, brushed steel, gold, brass. Made to any size, so one design does the cam caps and the clutch covers.'),
        ('fit_brass_engine.png', 'Fitted: Starburst covers in brass', 'On the chopper lead, in place of the plain chrome caps on the heads and the clutch covers.'),
        ('fit_brass_fpeg.png', 'Fitted: Torpedo pegs in brass', 'On the forward controls of the chopper lead.'),
        ('lamps.png', 'Lamp shells', 'Back row to front: Grille, Finned, Bullet. Left to right: chrome, brushed steel, gold, brass. The same design is made for the headlamp and the tail lamp; the glass and reflector here are stand-ins.'),
        ('fit_brass_lamp.png', 'Fitted: Bullet headlamp in brass', 'The bike keeps its own fluted glass, reflector and bulb.'),
        ('fit_brass_side_long.png', 'Fitted: a full brass set', 'Bullet lamps front and rear, Starburst covers and Torpedo pegs on the chopper lead.'),
        ('fit_engine_pots.png', 'Engine: Twin pot top end in chrome', 'Two domed rocker pots with acorn nuts on each head and polished pushrod tubes up each cylinder, shown with Target covers.'),
        ('fit_engine_finned.png', 'Engine: Finned top end in brass', 'A deep-finned rocker cover with a turned breather cap on each head, shown with Target covers.')]
body = '<main>\n<div class="k">Bike Models V2 · Choppers · Bespoke parts</div>\n<h1>Machined parts to order</h1>\n<p class="lede">Bespoke chopper parts in four metals: three footpegs, three engine covers, three lamp shells and two engine top ends. Each fits any chopper without moving a peg or a mounting. Rendered straight from the models, nothing retouched.</p>\n<div class="grid">'
for f, title, note in PICS:
    p = os.path.join(ROOT, 'out', 'chopper_parts', f)
    if not os.path.exists(p):
        continue
    im = Image.open(p).convert('RGB')
    im = im.resize((1300, int(im.height * 1300 / im.width)))
    b = io.BytesIO()
    im.save(b, 'JPEG', quality=84)
    body += f'<figure><img alt="{title}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> {note}</figcaption></figure>\n'
open(os.path.join(ROOT, 'out', 'chopper_parts.html'), 'w').write(head + body + '</div>\n</main>')
