"""Builds the chopper Definition page (out/chopper_definition.html)."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Chopper Definition</title>')
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
SETS = [
    ('lodestar_chopper', 'Lodestar chopper, round 2', 'Your round 1 notes on the chopper lead bike: much more chrome (frame, engine covers, pipes), both pipes swept round the front of the engine and back along one side clear of the legs, grips slanted inwards, spark plugs moved out from under the tank, an elaborate sissy bar and a painted panel on the teardrop tank. Rendered straight from the model, nothing retouched.', [
        ('shot_side_long', 'Whole bike, right side', 'Chrome frame, engine covers and pipes. Proportions, pegs and seat height are unchanged.'),
        ('shot_pipes', 'Pipes', 'The rear pipe now leaves its head forwards, passes the front cylinder and drops beside the front pipe; the two run back together low on the right, tucked in under the line of the rider\'s legs.'),
        ('shot_tankside', 'Tank panel', 'A pinstriped teardrop scallop with the Lodestar star, the same on both sides. It is a painted decal, so each brand and paint option can have its own.'),
        ('shot_sissy', 'Sissy bar', 'A pointed arch with a spear finial, a collared centre bar and wrought scrolls above the back rest.'),
        ('shot_barsf', 'Bars and grips', 'The grips, levers, switches and mirrors are slanted inwards by 18 degrees about the hand point, so the hands stay where the rider pose has them.'),
        ('shot_engine', 'Engine, right side', 'Chrome rocker covers, clutch cover and air cleaner; polished outer cases; barrels and heads left cast.'),
        ('shot_engine_l', 'Engine, left side', 'The spark plugs now come out of the left side of the heads, clear of the tank.'),
        ('shot_front34_long', 'Whole bike, front quarter', ''),
        ('shot_rearq', 'Rear quarter, exhaust side', ''),
        ('shot_rearq_l', 'Rear quarter, chain side', ''),
        ('shot_glamp', 'Tail lamp', 'Unchanged from round 1.'),
    ]),
]
body = '<main>\n<div class="k">Bike Models V2 · Definition pass · Chopper</div>'
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
open(os.path.join(ROOT, 'out', 'chopper_definition.html'), 'w').write(head + body + '\n</main>')
print(n, len(head + body))
