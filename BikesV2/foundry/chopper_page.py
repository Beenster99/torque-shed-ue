"""Builds the chopper Definition page (out/chopper_definition.html)."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Chopper Definition</title>')
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
SETS = [
    ('lodestar_chopper', 'Lodestar chopper, first look', 'The chopper lead bike in full detail for the first time: rigid frame, long raked fork, 45 degree V-twin. It carries over what you signed off on the cruiser (chrome controls and mirrors, fluted lamp, chrome clocks, the mudguard tail lamp, pipes that run straight into their silencers) and adds the tall sissy bar and back rest. Rendered straight from the model, nothing retouched.', [
        ('shot_side_long', 'Whole bike, right side', 'Rigid frame with chrome axle plates, no rear suspension, pegs and seat height as positioned for the rider.'),
        ('shot_front34_long', 'Whole bike, front quarter', 'Chrome front mudguard with the rounded nose, fluted lamp, pull-back bars.'),
        ('shot_rearq', 'Rear quarter, exhaust side', 'Two slim upswept pipes with open ends, the sissy bar hoop and back rest.'),
        ('shot_rearq_l', 'Rear quarter, chain side', 'Primary cover, chain run and the left axle plate.'),
        ('shot_engine', 'Engine close-up', 'The V-twin, its pipes and the frame cradle.'),
        ('shot_sissy', 'Sissy bar and back rest', 'One polished hoop up from the axle either side of the mudguard, two straps carrying the pad.'),
        ('shot_glamp', 'Tail lamp', 'Chrome oval lamp on the mudguard end with the indicators growing from its sides.'),
        ('shot_cockpit', 'Cockpit', 'Chrome clocks, bars, levers and filler cap.'),
        ('shot_fpeg', 'Forward controls', 'Peg, pedal and the arm to the frame.'),
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
        shutil.copy(p, dst + f'{key}_r1_{tag}.png')
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
