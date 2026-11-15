"""Builds the cafe Definition first-look page (out/cafe_definition.html) from out/renders/hartwell_cafe_def_*.png."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Cafe Definition</title>')
R = os.path.join(ROOT, 'out', 'renders', 'hartwell_cafe_def_')
items = [
    ('side', 'Side view', 'side', 'Points 1, 2, 8, 11: tank lower and slimmer, saddle lifted with the seat rail now under it, the black side panel filling the frame triangle, a squarer box-section swingarm. Black cast wheels (10).'),
    ('shot_rearq', 'Rear quarter, exhaust side', 'rear_quarter', 'Points 12, 14, 15, 16: a big megaphone silencer on a fatter header pipe; real twin shocks with black progressive springs, alloy top eyes and preload collars, their bottom eyes on lugs by the rear hub.'),
    ('shot_loop', 'Frame loop, brake light and indicators', 'loop', 'Point 3: the seat rails carry on as one hoop round the back of the saddle, and the hoop carries a traditional red lamp with an amber indicator on a chrome stalk each side.'),
    ('shot_midr', 'Shocks, side panel and pillion peg', 'mid_right', 'Points 7 and 8: the pillion peg hangs from two legs spread along the rail so they make a triangle; the side panel covers the frame triangle.'),
    ('shot_cockpit', 'Cockpit', 'cockpit', 'Points 4, 5, 6: bar-end mirrors, the dials in shallow tin cans (black here, chrome is an option), polished steel levers.'),
    ('shot_nose', 'Headlamp', 'headlamp', 'Point 13: the headlamp is now about 170 mm across. Front indicators changed to match the traditional rear ones.'),
    ('shot_rearq_front', 'Front quarter, left side', 'front_left', 'Point 9: a shorter, bobbed front mudguard.'),
    ('q', 'Whole bike, front quarter', 'front_quarter', ''),
]
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
figs = []
for tag, title, slug, cap in items:
    p = R + tag + '.png'
    if not os.path.exists(p):
        continue
    shutil.copy(p, dst + 'hartwell_cafe_r2_' + slug + '.png')
    im = Image.open(p).convert('RGB')
    if im.width > 1400:
        im = im.resize((1400, int(im.height * 1400 / im.width)))
    b = io.BytesIO()
    im.save(b, 'JPEG', quality=82)
    figs.append(f'<figure><img alt="{title}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> {cap}</figcaption></figure>')
OTHERS = [
    ('corvano_cafe', 'Corvano cafe', 'V-twin in a trellis, twin-spoke cast wheels.'),
    ('kazeda_cafe', 'Kazeda cafe', 'Inline four in twin spars, sickle-spoke cast wheels.'),
    ('lodestar_cafe', 'Lodestar cafe', 'Air-cooled V-twin in a double cradle, wire wheels.'),
    ('rheinhart_cafe', 'Rheinhart cafe', 'Boxer twin with shaft drive, web-spoke cast wheels.'),
]
others = []
for key, title, cap in OTHERS:
    for tag, view in (('rearq_front', 'front quarter'), ('rearq', 'rear quarter')):
        p = os.path.join(ROOT, 'out', 'renders', f'{key}_def_shot_{tag}.png')
        if not os.path.exists(p):
            continue
        shutil.copy(p, dst + f'{key}_r1_{tag}.png')
        im = Image.open(p).convert('RGB')
        if im.width > 1400:
            im = im.resize((1400, int(im.height * 1400 / im.width)))
        b = io.BytesIO()
        im.save(b, 'JPEG', quality=80)
        others.append(f'<figure><img alt="{title}, {view}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}, {view}</strong> {cap if tag == "rearq_front" else ""}</figcaption></figure>')
body = '''<main>
<div class="k">Bike Models V2 · Definition pass · Cafe</div>
<h1>Hartwell cafe, round 2</h1>
<p class="lede">Your sixteen points from 15 November, each named under the picture that shows it. Rendered straight from the model, nothing retouched.</p>
<div class="grid">''' + '\n'.join(figs) + '</div>' + ('''
<h1 style="margin-top:48px">The other cafes</h1>
<p class="lede">Built automatically from the Hartwell: the same seat hump with its light and indicators, round headlamp, clocks, mudguards and chain, on each brand's own frame, engine, exhaust, tank line and wheels. These still follow the first-look Hartwell; they pick up the round 2 changes once the Hartwell is signed off.</p>
<div class="grid">''' + '\n'.join(others) + '</div>' if others else '') + '\n</main>'
open(os.path.join(ROOT, 'out', 'cafe_definition.html'), 'w').write(head + body)
print(len(figs), len(head + body))
