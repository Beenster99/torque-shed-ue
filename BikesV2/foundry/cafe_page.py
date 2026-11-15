"""Builds the cafe Definition first-look page (out/cafe_definition.html) from out/renders/hartwell_cafe_def_*.png."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Cafe Definition</title>')
R = os.path.join(ROOT, 'out', 'renders', 'hartwell_cafe_def_')
items = [
    ('q', 'Whole bike, front quarter', 'front_quarter', 'The cafe lead bike with everything agreed on the naked and the sport carried over.'),
    ('shot_rearq', 'Rear quarter, exhaust side', 'rear_quarter', 'Seat hump cut square at the back for the brake light, clip-on bars, twin shocks.'),
    ('side', 'Side view', 'side', 'Square-on, at the same scale as the drawings. The footrests are where the rider pose work put them.'),
    ('shot_nose', 'Round headlamp', 'headlamp', 'New for the cafe and classic families: painted bowl, rolled chrome rim, deep reflector with a projector behind domed glass, and a bracket each side clamped to the fork leg.'),
    ('shot_tail', 'Brake light and indicators', 'tail', 'The sport rule applied: the light faces straight back, let into the end of the hump, and the indicators grow out of its housing so the plate hanger can come off.'),
    ('shot_rearq_front', 'Front quarter, left side', 'front_left', 'Chain side: the detailed chain and sprockets, the engine cases and the header pipe.'),
    ('shot_guard_front', 'Front mudguard', 'front_mudguard', 'Down on the tyre as on the sport, with the fork shrouds on the front of each leg.'),
    ('shot_hugger', 'Rear hugger and chain', 'hugger', 'Short hugger bolted to the swingarm, close to the tyre.'),
    ('shot_rear', 'Silencer outlet', 'outlet', 'Open outlet, dark inside.'),
    ('shot_midr', 'Engine, right side', 'engine_right', 'The parallel twin with its fins, covers and pipework.'),
    ('shot_cockpit', 'Cockpit', 'cockpit', 'Clip-ons, twin clocks and the filler cap. The clock faces are still blank.'),
]
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
figs = []
for tag, title, slug, cap in items:
    p = R + tag + '.png'
    if not os.path.exists(p):
        continue
    shutil.copy(p, dst + 'hartwell_cafe_r1_' + slug + '.png')
    im = Image.open(p).convert('RGB')
    if im.width > 1400:
        im = im.resize((1400, int(im.height * 1400 / im.width)))
    b = io.BytesIO()
    im.save(b, 'JPEG', quality=82)
    figs.append(f'<figure><img alt="{title}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> {cap}</figcaption></figure>')
body = '''<main>
<div class="k">Bike Models V2 · Definition pass · Cafe</div>
<h1>Hartwell cafe, first look</h1>
<p class="lede">The cafe lead bike, built from the signed-off naked and sport work, with a new classic round headlamp. Known to be unfinished: the clock faces are blank. Rendered straight from the model, nothing retouched.</p>
<div class="grid">''' + '\n'.join(figs) + '</div>\n</main>'
open(os.path.join(ROOT, 'out', 'cafe_definition.html'), 'w').write(head + body)
print(len(figs), len(head + body))
