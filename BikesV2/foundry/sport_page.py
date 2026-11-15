"""Builds the sport Definition first-look page (out/sport_definition.html) from out/renders/kazeda_sport_def_*.png."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Sport Definition</title>')
R = os.path.join(ROOT, 'out', 'renders', 'kazeda_sport_def_')
items = [
    ('q', 'Whole bike, front quarter', 'front_quarter', 'Round 1 of your markup. Mudguard close to the tyre, flat bottom line to the fairing, tank down on the frame, gold ten-spoke wheels from the new wheel library.'),
    ('shot_rearq', 'Rear quarter, exhaust side', 'rear_quarter', 'Twin square brake light behind a red lens, rear indicators mounted on the plate hanger, winglet tips turned down.'),
    ('side', 'Side view', 'side', 'Square-on, at the same scale as the drawings. The footrests are where the rider pose work put them; the fairing is cut away round the pedals.'),
    ('shot_nose', 'Nose, lamps and screen', 'nose', 'The brand lamps: long slanted eyes let flush into the nose, with a running-light blade and two projectors each side. The screen is thin clear Perspex, round at the bottom.'),
    ('shot_guard_front', 'Front mudguard and fairing prow', 'front_mudguard', 'The mudguard hugs the tyre. Behind the tyre the two sides of the fairing taper in to meet.'),
    ('shot_stubby', 'Exhaust outlet', 'outlet', 'A satin end ring with an open tail pipe standing in it.'),
    ('shot_chain', 'Drive chain', 'chain', 'New for every chain-driven bike: rollers on pins at a real 5/8 inch pitch, inner and outer side plates, and a toothed, drilled rear sprocket.'),
    ('shot_cockpit', 'Cockpit', 'cockpit', 'Clip-on bars with the naked\'s grips, levers and switches, the display behind the screen.'),
    ('shot_midl', 'Frame and footrest, left side', 'frame_left', 'Beam frame with weld seams, pivot cap and mount bolts.'),
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
<h1>Kazeda sport, markup round 1</h1>
<p class="lede">The sport lead bike with the naked's detail carried over and the sport-only parts started. Your nine points, the winglets, the new wheels and the first detailed chain. Known to be unfinished: the engine behind the fairing is still plain, the swingarm and tail underside are bare, and the screen and fairing edges have no fixings yet. Rendered straight from the model, nothing retouched.</p>
<div class="grid">''' + '\n'.join(figs) + '</div>\n</main>'
open(os.path.join(ROOT, 'out', 'sport_definition.html'), 'w').write(head + body)
print(len(figs), len(head + body))
