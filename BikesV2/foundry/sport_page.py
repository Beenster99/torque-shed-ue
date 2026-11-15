"""Builds the sport Definition first-look page (out/sport_definition.html) from out/renders/kazeda_sport_def_*.png."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Sport Definition</title>')
R = os.path.join(ROOT, 'out', 'renders', 'kazeda_sport_def_')
items = [
    ('q', 'Whole bike, front quarter', 'front_quarter', 'Mirrors now stand on the fairing, not the bars. The naked headlamp that sat on top of the nose is gone.'),
    ('shot_rearq', 'Rear quarter, exhaust side', 'rear_quarter', 'The pipes gather into a box under the engine and leave through a stubby outlet beside the belly pan. Footrests with heel plates, hugger on the swingarm.'),
    ('side', 'Side view', 'side', 'Square-on, at the same scale as the drawings.'),
    ('shot_nose', 'Nose', 'nose', 'Slim slanted lamps let flush into the nose: black surround, smoked lens, a running-light blade and two projector eyes each side. Amber indicator blades flush in the flanks, no stalks.'),
    ('shot_cockpit', 'Cockpit', 'cockpit', 'Clip-on bars with the same grips, levers and switches as the naked, and the display behind the screen.'),
    ('shot_midl', 'Frame and footrest, left side', 'frame_left', 'Beam frame with weld seams where the beam meets the cast pivot plate, pivot cap and mount bolts. The footrests stay exactly where the rider pose work put them; the lower rear edge of the fairing is cut away round the pedals instead.'),
]
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
figs = []
for tag, title, slug, cap in items:
    p = R + tag + '.png'
    if not os.path.exists(p):
        continue
    shutil.copy(p, dst + 'kazeda_sport_r1_' + slug + '.png')
    im = Image.open(p).convert('RGB')
    if im.width > 1400:
        im = im.resize((1400, int(im.height * 1400 / im.width)))
    b = io.BytesIO()
    im.save(b, 'JPEG', quality=82)
    figs.append(f'<figure><img alt="{title}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> {cap}</figcaption></figure>')
body = '''<main>
<div class="k">Bike Models V2 · Definition pass · Sport</div>
<h1>Kazeda sport, first look</h1>
<p class="lede">The sport lead bike with the naked's detail carried over and the sport-only parts started. This is a first look for your markup, not a finished proof. Known to be unfinished: the engine behind the fairing is still plain, the swingarm and tail underside are bare, and the screen and fairing edges have no fixings yet. Rendered straight from the model, nothing retouched.</p>
<div class="grid">''' + '\n'.join(figs) + '</div>\n</main>'
open(os.path.join(ROOT, 'out', 'sport_definition.html'), 'w').write(head + body)
print(len(figs), len(head + body))
