"""Builds the cafe Definition first-look page (out/cafe_definition.html) from out/renders/hartwell_cafe_def_*.png."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Cafe Definition</title>')
R = os.path.join(ROOT, 'out', 'renders', 'hartwell_cafe_def_')
items = [
    ('side', 'Side view', 'side', 'Round 3: no pillion pegs, the radiator moved forward of the down tube, the airbox cut back behind the cylinder head. Round 2: lower slimmer tank, lifted saddle, side panel over the frame triangle, squarer swingarm, black wheels.'),
    ('shot_rearq', 'Rear quarter, exhaust side', 'rear_quarter', 'The megaphone on the fatter header, real twin shocks with their bottom eyes by the rear hub, the frame loop with its lamp.'),
    ('shot_midr', 'Header into the megaphone', 'mid_right', 'Round 3: the header now comes up in one line with the megaphone and slides into its inlet sleeve. The lower radiator hose takes one clean bend to the pump.'),
    ('shot_engine', 'Engine, radiator and airbox', 'engine', 'Round 3: the radiator stands ahead of the down tube, as far forward as the front tyre allows at full fork travel; the black airbox stops behind the head, with a small coil pack under the tank above it.'),
    ('shot_lamp', 'Headlamp lens', 'headlamp', 'Round 3: an LED unit in the classic shell: gloss black inside, a halo ring, a small projector above a big one, LED blocks each side.'),
    ('shot_cockpit', 'Cockpit', 'cockpit', 'Round 3: deeper dial cans. Bar-end mirrors and steel levers from round 2.'),
    ('shot_loop', 'Frame loop, brake light and indicators', 'loop', 'The hoop round the back of the saddle carrying the traditional lamp and indicators.'),
    ('shot_rearq_front', 'Front quarter, left side', 'front_left', 'Chain side, with the bobbed front mudguard.'),
    ('q', 'Whole bike, front quarter', 'front_quarter', ''),
]
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
figs = []
for tag, title, slug, cap in items:
    p = R + tag + '.png'
    if not os.path.exists(p):
        continue
    shutil.copy(p, dst + 'hartwell_cafe_r3_' + slug + '.png')
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
<h1>Hartwell cafe, round 3</h1>
<p class="lede">Your seven points from late on 15 November on top of the sixteen from round 2, each named under the picture that shows it. Rendered straight from the model, nothing retouched.</p>
<div class="grid">''' + '\n'.join(figs) + '</div>' + ('''
<h1 style="margin-top:48px">The other cafes</h1>
<p class="lede">Built automatically from the Hartwell: the same seat hump with its light and indicators, round headlamp, clocks, mudguards and chain, on each brand's own frame, engine, exhaust, tank line and wheels. These still follow the first-look Hartwell; they pick up the round 2 changes once the Hartwell is signed off.</p>
<div class="grid">''' + '\n'.join(others) + '</div>' if others else '') + '\n</main>'
open(os.path.join(ROOT, 'out', 'cafe_definition.html'), 'w').write(head + body)
print(len(figs), len(head + body))
