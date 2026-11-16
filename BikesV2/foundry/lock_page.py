"""Builds the steering lock review page (out/steering_lock.html)."""
import base64, io, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Steering Lock</title>')
ROWS = [('Sport', 'Kazeda, Corvano, BSX, Varga', '4', '24', 'Scallops in the front corners of the tank'),
        ('Cruiser', 'Lodestar, Corvano, Kazeda, Rheinhart', '10', '28', 'A shallow dish each side of the tank front'),
        ('Adventure', 'Corvano, BSX, Kazeda, Rheinhart, Varga', '0', '26', 'Nose sides and the plates beside the screen eased back')]
PICS = [('kazeda_sport', 'cockpit', 'Kazeda sport', 'The tank front now tucks in each side so the clip-ons, levers and switches swing past it.'),
        ('lodestar_cruiser', 'cockpit', 'Lodestar cruiser', 'A dish each side of the tank front lets the levers and switch pods pass.'),
        ('corvano_adventure', 'cockpit', 'Corvano adventure', 'The nose sides beside the bars are eased so the hand guards clear at rest and through the turn.'),
        ('corvano_adventure', 'front34', 'Corvano adventure', 'From the front the face is unchanged.')]
body = '<main>\n<div class="k">Bike Models V2 · Steering lock</div>\n<h1>Bars that can turn</h1>\n<p class="lede">On three families the bars touched the tank or fairing almost as soon as they turned. The bodywork is now relieved exactly where the bars, levers, switches and guards sweep, with 15 mm to spare. Nothing on the bars moved: pegs, hand points and controls are where you signed them off.</p>\n'
body += '<div style="overflow-x:auto"><table style="border-collapse:collapse;width:100%"><tr>' + ''.join(f'<th style="text-align:left;padding:6px 10px">{h}</th>' for h in ('Family', 'Bikes', 'Turned before', 'Turns now', 'What changed')) + '</tr>'
for r in ROWS:
    body += '<tr>' + ''.join(f'<td style="padding:6px 10px;border-top:1px solid rgba(128,128,128,.35)">{c}{" degrees" if i in (2, 3) else ""}</td>' for i, c in enumerate(r)) + '</tr>'
body += '</table></div>\n<div class="grid">'
for nm, tag, title, note in PICS:
    p = os.path.join(ROOT, 'out', 'renders', f'{nm}_def_shot_{tag}.png')
    if not os.path.exists(p):
        continue
    im = Image.open(p).convert('RGB')
    im = im.resize((1100, int(im.height * 1100 / im.width)))
    b = io.BytesIO()
    im.save(b, 'JPEG', quality=82)
    body += f'<figure><img alt="{title}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> {note}</figcaption></figure>\n'
open(os.path.join(ROOT, 'out', 'steering_lock.html'), 'w').write(head + body + '</div>\n</main>')
