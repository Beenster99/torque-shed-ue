"""Builds the MX Definition page (out/mx_definition.html)."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>MX Definition</title>')
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
SETS = [
    ('varga_mx', 'Varga MX, round 3', 'Your note on the exhaust: the pipe now runs inside the right-hand rear side panel, under the saddle, and the silencer comes out behind the panel. The right panel stands a little further out than the left to make room for it. Everything else is as round 2. Rendered straight from the model, nothing retouched.', [
        ('shot_side', 'Whole bike, right side', 'The pipe disappears behind the side panel; only the end of the silencer shows.'),
        ('shot_rearq', 'Rear quarter, exhaust side', 'The silencer between the subframe and the panel.'),
        ('shot_seat', 'Seat and side panel', 'The header curls round the barrel and goes in behind the panel.'),
        ('shot_front34', 'Whole bike, front quarter', 'From round 2: one mudguard, narrower tyres, heavy spokes.'),
        ('shot_wheel', 'Front wheel', 'From round 2.'),
        ('shot_hoses_r', 'Bottom hose and water pump', 'From round 2.'),
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
        shutil.copy(p, dst + f'{key}_r3_{tag}.png')
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
