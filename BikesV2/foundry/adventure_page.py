"""Builds the adventure Definition page (out/adventure_definition.html)."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Adventure Definition</title>')
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
SETS = [
    ('corvano_adventure', 'Corvano adventure, round 4', 'Your round 3 notes on the adventure lead bike: the two blue gaps filled, the pipe swept up into the silencer, the frame hoop behind the tail light, the screen wrapped round on top of the fairing, and a rounded nose on the front mudguard. Rendered straight from the model, nothing retouched.', [
        ('shot_side', 'Whole bike, right side', 'The seat now runs up the back of the tank and sits down on the frame rail at the rear, so both blue gaps are closed.'),
        ('shot_midr', 'Pipe into the silencer', 'The link pipe leaves the collector and sweeps up in line with the silencer, which tapers out from it.'),
        ('shot_tail', 'Tail light and frame hoop', 'The hoop passes behind the light, which is now the rearmost part.'),
        ('shot_front34', 'Whole bike, front quarter', 'The screen is round at the bottom and wraps back along the top of the fairing.'),
        ('shot_nose', 'Front mudguard nose', 'The mudguard ends in a rounded nose. This is in the shared mudguard code, so every bike gets it.'),
        ('shot_rearq', 'Rear quarter, exhaust side', 'Silencer, hoop and rack together.'),
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
        shutil.copy(p, dst + f'{key}_r4_{tag}.png')
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
