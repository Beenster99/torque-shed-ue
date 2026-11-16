"""Builds the MX Definition page (out/mx_definition.html)."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>MX Definition</title>')
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
SETS = [
    ('varga_mx', 'Varga MX, round 2', 'Your round 1 notes on the motocross lead bike: one front mudguard, narrower knobbly tyres front and rear, heavy round spokes, the tank raised and cut back clear of the forks, the seat moved back, and the water pump moved on to the engine so the radiator hose meets it. Radiator shrouds are new. Rendered straight from the model, nothing retouched.', [
        ('shot_side', 'Whole bike, right side', 'Tank 36 mm higher, seat 40 mm further back. Pegs are unchanged.'),
        ('shot_front34', 'Whole bike, front quarter', 'One mudguard blade on the centre line. Front tyre is now 84 mm wide (was 120).'),
        ('shot_rearq', 'Rear quarter, exhaust side', 'Rear tyre is now 116 mm wide (was 180).'),
        ('shot_rearq_l', 'Rear quarter, chain side', ''),
        ('shot_wheel', 'Front wheel', 'Heavy round spokes, each with a nipple at the rim.'),
        ('shot_hoses_r', 'Bottom hose and water pump', 'The pump now sits on the engine case beside the clutch cover, and the hose from the radiator runs to it.'),
        ('shot_hoses', 'Top hose and radiator shroud', 'The top hose runs to the thermostat on the side of the head. The black panel is the radiator shroud.'),
        ('shot_engine', 'Engine, right side', ''),
        ('shot_seat', 'Seat', ''),
        ('shot_cockpit', 'Cockpit', ''),
    ]),
]
body = '<main>\n<div class="k">Bike Models V2 · Definition pass · MX</div>'
n = 0
for key, h1, lede, items in SETS:
    figs = []
    for tag, title, cap in items:
        p = os.path.join(ROOT, 'out', 'renders', f'{key}_def_{tag}.png')
        if not os.path.exists(p):
            continue
        shutil.copy(p, dst + f'{key}_r2_{tag}.png')
        im = Image.open(p).convert('RGB')
        if im.width > 1400:
            im = im.resize((1400, int(im.height * 1400 / im.width)))
        b = io.BytesIO()
        im.save(b, 'JPEG', quality=82)
        figs.append(f'<figure><img alt="{title}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> {cap}</figcaption></figure>')
    n += len(figs)
    body += f'\n<h1{" style=margin-top:48px" if n > len(figs) else ""}>{h1}</h1>\n<p class="lede">{lede}</p>\n<div class="grid">' + '\n'.join(figs) + '</div>'
open(os.path.join(ROOT, 'out', 'mx_definition.html'), 'w').write(head + body + '\n</main>')
print(n, len(head + body))
