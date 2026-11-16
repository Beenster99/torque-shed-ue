"""Builds the cafe Definition first-look page (out/cafe_definition.html) from out/renders/hartwell_cafe_def_*.png."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Cafe Definition</title>')
R = os.path.join(ROOT, 'out', 'renders', 'hartwell_cafe_def_')
items = [
    ('shot_loop', 'Brake light and indicator lenses', 'loop', 'Round 4: the brake light is a clear red lens over a chrome tray of LED cells; the indicators are clear amber lenses with a ribbed reflector and bulb inside.'),
    ('shot_rearq', 'Rear quarter, exhaust side', 'rear_quarter', 'Round 4: the rear hugger is gone, so the rear tyre is open under the hump.'),
    ('shot_heel', 'Heel plate variant', 'heel', 'Round 4: a plain brushed plate with no holes, one blade swept back from the peg. The drilled plate stays as the other variant.'),
    ('shot_wheel', 'Spokes into the rim', 'wheel', 'Round 4: on all twelve wheel styles the spokes now run into the rim and are trimmed to its inner surface, so no gap shows at any spoke angle.'),
    ('side', 'Side view', 'side', 'Everything so far: rounds 2, 3 and 4.'),
    ('shot_midr', 'Header into the megaphone', 'mid_right', 'From round 3.'),
    ('shot_engine', 'Engine, radiator and airbox', 'engine', 'From round 3.'),
    ('shot_lamp', 'Headlamp lens', 'headlamp', 'From round 3.'),
    ('shot_cockpit', 'Cockpit', 'cockpit', 'From round 3.'),
    ('q', 'Whole bike, front quarter', 'front_quarter', ''),
]
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
figs = []
for tag, title, slug, cap in items:
    p = R + tag + '.png'
    if not os.path.exists(p):
        continue
    shutil.copy(p, dst + 'hartwell_cafe_r4_' + slug + '.png')
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
<h1>Hartwell cafe, signed off</h1>
<p class="lede">Signed off on 16 November after four rounds of markup. Rendered straight from the model, nothing retouched.</p>
<div class="grid">''' + '\n'.join(figs) + '</div>' + ('''
<h1 style="margin-top:48px">The other cafes</h1>
<p class="lede">Built automatically from the Hartwell: the same seat hump with its light and indicators, round headlamp, clocks, mudguards and chain, on each brand's own frame, engine, exhaust, tank line and wheels. Now with the real shocks. They still carry the first-look tail and lights; the Hartwell's signed-off parts become options for them with the common parts.</p>
<div class="grid">''' + '\n'.join(others) + '</div>' if others else '') + '\n</main>'
open(os.path.join(ROOT, 'out', 'cafe_definition.html'), 'w').write(head + body)
print(len(figs), len(head + body))
