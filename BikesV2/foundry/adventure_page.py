"""Builds the adventure Definition page (out/adventure_definition.html)."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Adventure Definition</title>')
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
SETS = [
    ('corvano_adventure', 'Corvano adventure, first look', 'The adventure lead bike. The front is new for this family: a closed nose and beak across the bike with a headlamp let into each flank, and shaped tank shrouds behind it, in place of flat side panels. It already carries the lessons from the other bikes: real shock, bars straight through the grips, pipes run into the silencer, hollow outlet. Rendered straight from the model, nothing retouched. Known weak spots are listed in the thread.', [
        ('shot_rearq_front', 'Whole bike, front quarter', 'Nose, beak and screen, tank shrouds, trellis frame, long-travel fork.'),
        ('shot_rearq_l', 'Rear quarter, exhaust side', 'The big high silencer is on the left on this bike. Luggage rack, long two-up seat.'),
        ('shot_nose', 'Nose and headlamp', 'A headlamp each side of the beak, indicators on the fork brackets.'),
        ('shot_midl', 'Engine and exhaust, left side', 'Both pipes into the one silencer.'),
        ('shot_rearq', 'Rear quarter, right side', 'The open side of the rear wheel, shock and rear brake.'),
        ('shot_cockpit', 'Cockpit', 'Wide bars, grips in line, screen ahead.'),
        ('shot_rear', 'Rear wheel and brake', 'Rear disc, caliper and the swing arm.'),
    ]),
]
body = '<main>\n<div class="k">Bike Models V2 · Definition pass · Adventure</div>'
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
open(os.path.join(ROOT, 'out', 'adventure_definition.html'), 'w').write(head + body + '\n</main>')
print(n, len(head + body))
