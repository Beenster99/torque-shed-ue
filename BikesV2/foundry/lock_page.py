"""Builds the steering lock review page (out/steering_lock.html)."""
import base64, io, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Steering Lock</title>')
ROWS = [('Sport', 'Kazeda, Corvano, BSX, Varga', '4', '24', 'Scallops in the front corners of the tank (unchanged)'),
        ('Cruiser', 'Lodestar, Corvano, Kazeda, Rheinhart', '10', '28 to 30', 'Tanks untouched. Bars raised on taller risers (Lodestar 50 mm, Corvano 10, Kazeda 80, Rheinhart 85). New this round: the clocks come inboard to the rider\'s side of the risers, just under the bar'),
        ('Adventure', 'Corvano, BSX, Kazeda, Rheinhart, Varga', '0', '28 to 30', 'Fairing moved 110 mm forward with the screen and lights. The nose is a wedge that runs straight down from the screen foot and now ends in a short blunt nose above the lamps (round 5: the pointy tip is gone). The flanks and tank flare out below the hand guards')]
PICS = [('corvano_adventure', 'side', 'Corvano adventure', 'Round 5: the pointy tip is off. The brow still runs straight down from the screen foot, and now ends in a short blunt nose above the lamps. The screen is raked back to follow the same line.'),
        ('corvano_adventure', 'front34', 'Corvano adventure', 'From the front: no spike, just a small dip in the brow between the lamps.'),
        ('corvano_adventure', 'advlamp', 'Corvano adventure', 'Close on the snout and the lamps.'),
        ('corvano_adventure', 'cockpit', 'Corvano adventure', 'From the saddle: the tank front and the fairing flanks now flare out below the bars, as on the bike in your riding picture, and the bars still turn.'),
        ('kazeda_adventure', 'front34', 'Kazeda adventure', 'The same change on a brand variant, with the woven carbon beak.'),
        ('kazeda_adventure', 'side', 'Kazeda adventure', 'Side view.'),
        ('varga_adventure', 'front34', 'Varga adventure', 'And on the Varga.'),
        ('lodestar_cruiser', 'cockpit', 'Lodestar cruiser', 'The clocks now sit on the rider\'s side of the risers, just under the bar, where the raised bars no longer hide them.'),
        ('lodestar_cruiser', 'side', 'Lodestar cruiser', 'From the side: clocks behind the risers on short stays.'),
        ('kazeda_cruiser', 'cockpit', 'Kazeda cruiser', 'The tallest risers of the four, with the clocks brought inboard the same way.'),
        ('kazeda_cruiser', 'side', 'Kazeda cruiser', 'Side view.')]
body = '<main>\n<div class="k">Bike Models V2 · Steering lock · Round 5</div>\n<h1>Bars that can turn</h1>\n<p class="lede">Fifth go: the pointy tip is off the adventure fairing. Adventure bikes: the fairing sits forward far enough for the bars to turn, lights and screen with it. Its nose is a wedge from the screen foot that ends in a short blunt nose above the lamps, and the fairing flanks and tank front flare out below the hand guards. Cruisers: the clocks have come inboard so the rider can see them past the raised bars. Pegs and seats are where you signed them off.</p>\n'
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
