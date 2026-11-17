"""Builds the steering lock review page (out/steering_lock.html)."""
import base64, io, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Steering Lock</title>')
ROWS = [('Sport', 'Kazeda, Corvano, BSX, Varga', '4', '24', 'Scallops in the front corners of the tank (unchanged from the last round)'),
        ('Cruiser', 'Lodestar, Corvano, Kazeda, Rheinhart', '10', '28 to 30', 'Tanks back to their signed-off shape. Bars raised on taller risers: Lodestar 50 mm, Corvano 10 mm, Kazeda 80 mm, Rheinhart 85 mm'),
        ('Adventure', 'Corvano, BSX, Kazeda, Rheinhart, Varga', '0', '26 to 30', 'Fairing back to its smooth shape. The flare now sits further forward, with a waist where the bars and hand guards swing')]
PICS = [('lodestar_cruiser', 'cockpit', 'Lodestar cruiser', 'The tank is its smooth signed-off shape again. The bars sit 50 mm higher on longer risers, which is just enough for the levers and switch pods to pass over the tank.'),
        ('lodestar_cruiser', 'side', 'Lodestar cruiser', 'From the side: the taller risers. Pegs and seat have not moved.'),
        ('kazeda_cruiser', 'side', 'Kazeda cruiser', 'The biggest rise of the four cruisers, 80 mm.'),
        ('corvano_adventure', 'cockpit', 'Corvano adventure', 'The fairing is smooth again. It narrows beside the bars and flares out ahead of them, so the hand guards swing in the waist.'),
        ('corvano_adventure', 'front34', 'Corvano adventure', 'The flare is now forward, around the headlight and screen.'),
        ('corvano_adventure', 'side', 'Corvano adventure', 'Side view of the reshaped nose.'),
        ('kazeda_adventure', 'front34', 'Kazeda adventure', 'The same reshaped nose on a brand variant, here with the new carbon weave beak.'),
        ('kazeda_sport', 'cockpit', 'Kazeda sport', 'Sport bikes keep the tank scallops from the last round.')]
body = '<main>\n<div class="k">Bike Models V2 · Steering lock · Round 2</div>\n<h1>Bars that can turn</h1>\n<p class="lede">Second go, after your notes. The cruiser tanks are back to normal and the bars are raised just enough to clear them. The adventure fairings are back to normal too, with the flared shape moved towards the front so the bars turn in a narrower waist. Pegs and seats are where you signed them off. On the cruisers the hands rise with the bars.</p>\n'
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
