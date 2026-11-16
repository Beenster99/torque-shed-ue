"""Builds the sport Definition first-look page (out/sport_definition.html) from out/renders/kazeda_sport_def_*.png."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Sport Definition</title>')
R = os.path.join(ROOT, 'out', 'renders', 'kazeda_sport_def_')
items = [
    ('q', 'Whole bike, front quarter', 'front_quarter', 'Round 2 of your markup. Mudguard down on the tyre, the nose flare joined to the side fairing, the tank sitting on the frame.'),
    ('shot_rearq', 'Rear quarter, exhaust side', 'rear_quarter', 'The tail now ends square with the brake light let into it, and the indicators grow out of the same housing, so the plate hanger can come off.'),
    ('side', 'Side view', 'side', 'Square-on, at the same scale as the drawings. The footrests have not moved.'),
    ('shot_tail', 'Brake light, indicators and plate arm', 'tail', 'The light faces straight back. Under the tail the plate arm has a foot held by four bolts, with a web each side running down on to the arm.'),
    ('shot_nose', 'Nose and the yellow-mark gap', 'nose', 'Same view as your marked-up picture. The flare now lies on the side fairing along their shared edge, and the fairing\'s top edge turns in to meet the cowl behind it.'),
    ('shot_prow', 'Fairing joined behind the front tyre', 'prow', 'Front wheel, mudguard and fork legs hidden for this one picture so the join can be seen: one closed prow from the keel up to the radiator opening.'),
    ('shot_guard_front', 'Front mudguard', 'front_mudguard', 'The crown is now a few millimetres off the tread and the edges follow the shoulder of the tyre down.'),
    ('shot_hugger', 'Rear hugger', 'hugger', 'Brought down by about 20 mm, on shorter legs bolted to the swingarm.'),
    ('shot_tankbase', 'Tank on the frame', 'tank_base', 'The base of the tank is flat and flares out to land on the spars, as it reads in the drawing.'),
    ('shot_stubby', 'Exhaust outlet', 'outlet', 'One open bore, black inside, with the perforated core far down it. A trim band had been built as a solid disc and was plugging it.'),
    ('shot_chain', 'Drive chain', 'chain', 'Unchanged from round 1.'),
    ('shot_cockpit', 'Cockpit', 'cockpit', 'Unchanged from round 1, shown so you can see the new shelf inside the fairing edge from the rider\'s side.'),
]
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
def fig(p, title, cap, save_as):
    if not os.path.exists(p):
        return None
    shutil.copy(p, dst + save_as)
    im = Image.open(p).convert('RGB')
    if im.width > 1400:
        im = im.resize((1400, int(im.height * 1400 / im.width)))
    b = io.BytesIO()
    im.save(b, 'JPEG', quality=82)
    return f'<figure><img alt="{title}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> {cap}</figcaption></figure>'


figs = [f for f in (fig(R + tag + '.png', title, cap, 'kazeda_sport_r2_' + slug + '.png') for tag, title, slug, cap in items) if f]
# the rest of the family, built automatically from the signed-off lead bike
OTHERS = [
    ('corvano_sport', 'Corvano sport', 'V-twin in a trellis, the short deep fairing, silencer under the tail, five Y-spoke wheels.'),
    ('hartwell_sport', 'Hartwell sport', 'Triple in a tube frame, the straight-edged fairing with gill slats, turbine wheels.'),
    ('varga_sport', 'Varga sport', 'Parallel twin in a bright trellis, the wedge fairing with a separate belly pan, seven-blade wheels.'),
]
others = []
for key, title, cap in OTHERS:
    for tag, view in (('rearq_front', 'front quarter'), ('rearq', 'rear quarter')):
        f = fig(os.path.join(ROOT, 'out', 'renders', f'{key}_def_shot_{tag}.png'), f'{title}, {view}', cap if tag == 'rearq_front' else '', f'{key}_def_{tag}.png')
        if f:
            others.append(f)
body = '''<main>
<div class="k">Bike Models V2 · Definition pass · Sport</div>
<h1>Kazeda sport, signed off</h1>
<p class="lede">Signed off on 15 November after two rounds of markup. The pictures show the eight points from round 2, each with its own close-up. Still unfinished and not part of this round: the engine behind the fairing is plain, the swingarm and the underside of the tail are bare, and the screen and fairing edges have no fixings yet. Rendered straight from the model, nothing retouched.</p>
<div class="grid">''' + '\n'.join(figs) + '</div>' + ('''
<h1 style="margin-top:48px">The other sport bikes</h1>
<p class="lede">Built automatically from the signed-off Kazeda: the same tail with its light and indicators, mudguards, tank base, chain and outlet rules, on each brand's own frame, engine, fairing and wheels. First look, not yet gone over by hand.</p>
<div class="grid">''' + '\n'.join(others) + '</div>' if others else '') + '\n</main>'
open(os.path.join(ROOT, 'out', 'sport_definition.html'), 'w').write(head + body)
print(len(figs), len(head + body))
