"""Builds the steering lock review page (out/steering_lock.html)."""
import base64, io, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Steering Lock</title>')
ROWS = [('Sport', 'Kazeda, Corvano, BSX, Varga', '4', '24', 'Scallops in the front corners of the tank (unchanged)'),
        ('Cruiser', 'Lodestar, Corvano, Kazeda, Rheinhart', '10', '28 to 30', 'Tanks untouched. Bars raised on taller risers (Lodestar 50 mm, Corvano 10, Kazeda 80, Rheinhart 85). New this round: the clocks come inboard to the rider\'s side of the risers, just under the bar'),
        ('Adventure', 'Corvano, BSX, Kazeda, Rheinhart, Varga', '0', '28 to 30', 'New this round: the fairing is its original shape again, moved 110 mm forward with the screen and lights, and ends in a sharp snout just short of the beak')]
PICS = [('corvano_adventure', 'side', 'Corvano adventure', 'The original fairing, moved 110 mm forward. Its nose now runs out to a sharp snout over the lamps, stopping just short of the beak tip.'),
        ('corvano_adventure', 'front34', 'Corvano adventure', 'The snout from the front, with the lights moved forward under it.'),
        ('corvano_adventure', 'advlamp', 'Corvano adventure', 'Close on the snout and the lamps.'),
        ('corvano_adventure', 'cockpit', 'Corvano adventure', 'From the saddle: the bars and hand guards now have room to turn behind the fairing.'),
        ('kazeda_adventure', 'front34', 'Kazeda adventure', 'The same change on a brand variant, with the woven carbon beak.'),
        ('kazeda_adventure', 'side', 'Kazeda adventure', 'Side view.'),
        ('varga_adventure', 'front34', 'Varga adventure', 'And on the Varga.'),
        ('lodestar_cruiser', 'cockpit', 'Lodestar cruiser', 'The clocks now sit on the rider\'s side of the risers, just under the bar, where the raised bars no longer hide them.'),
        ('lodestar_cruiser', 'side', 'Lodestar cruiser', 'From the side: clocks behind the risers on short stays.'),
        ('kazeda_cruiser', 'cockpit', 'Kazeda cruiser', 'The tallest risers of the four, with the clocks brought inboard the same way.'),
        ('kazeda_cruiser', 'side', 'Kazeda cruiser', 'Side view.')]
body = '<main>\n<div class="k">Bike Models V2 · Steering lock · Round 3</div>\n<h1>Bars that can turn</h1>\n<p class="lede">Third go, after your notes. Adventure bikes: the whole fairing is back to its original shape and moved forward far enough for the bars to turn, lights and screen with it, and it now has a sharp snout that mimics the beak and stops just short of it. Cruisers: the clocks have come inboard so the rider can see them past the raised bars. Pegs and seats are where you signed them off.</p>\n'
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
