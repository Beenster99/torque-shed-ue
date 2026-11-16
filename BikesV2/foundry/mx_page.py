"""Builds the MX Definition page (out/mx_definition.html)."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>MX Definition</title>')
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
SETS = [
    ('varga_mx', 'Varga MX, first look', 'The motocross lead bike in full detail for the first time: a tall single-cylinder bike on knobbly tyres with a high front mudguard, flat seat and number boards. It has no mirrors or lamps, one front disc, a liquid-cooled barrel with no fins and the exhaust tucked high under the seat. Rendered straight from the model, nothing retouched.', [
        ('shot_side', 'Whole bike, right side', 'Exhaust side. Pegs and seat height as positioned for the rider.'),
        ('shot_front34', 'Whole bike, front quarter', 'White front number board, high mudguard, fork guards and the single front disc.'),
        ('shot_rearq', 'Rear quarter, exhaust side', 'The silencer under the seat, the single shock and the aluminium swingarm.'),
        ('shot_rearq_l', 'Rear quarter, chain side', 'Side number board, chain and rear sprocket.'),
        ('shot_engine', 'Engine, right side', 'Plain liquid-cooled barrel, the header curling round the front and back over the engine.'),
        ('shot_engine_l', 'Engine, left side', ''),
        ('shot_cockpit', 'Cockpit', 'Bare bars, no clocks.'),
        ('shot_wheel', 'Front wheel', 'Knobbly tyre from the game, wire spokes, one disc.'),
        ('shot_rear', 'Rear wheel and brake', ''),
        ('shot_seat', 'Seat', ''),
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
        shutil.copy(p, dst + f'{key}_r1_{tag}.png')
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
