"""Builds the classic and cruiser Definition first-look page (out/classic_cruiser_definition.html)."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Classic and Cruiser Definition</title>')
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
SETS = [
    ('hartwell_classic', 'Hartwell classic, first look', 'The classic lead bike. New for this family: an air-cooled look finned twin, and a polished brake light standing on the rear mudguard with the indicators growing out of its housing. Rendered straight from the model, nothing retouched.', [
        ('shot_rearq_front', 'Whole bike, front quarter', 'Round headlamp and clocks carried over from the cafe, deep mudguards, wire wheels.'),
        ('shot_rearq', 'Rear quarter, exhaust side', 'Long flat seat, twin shocks and the long silencer.'),
        ('shot_glamp', 'Brake light and indicators', 'Polished oval housing on the mudguard, red lens facing straight back, an indicator out of each side so nothing depends on the plate.'),
        ('shot_midr', 'Engine, right side', 'Two finned barrels, the header down the front and back along the cases.'),
        ('shot_nose', 'Headlamp', 'The same round lamp and fork brackets as the cafe.'),
        ('shot_cockpit', 'Cockpit', 'Wide bars, twin clocks with faces, filler cap.'),
    ]),
    ('lodestar_cruiser', 'Lodestar cruiser, first look', 'The cruiser lead bike in its bobbed trim. Known gap: this trim has no number plate drawn yet.', [
        ('shot_rearq_front', 'Whole bike, front quarter', 'V-twin in a double cradle, fat front tyre, pulled-back bars.'),
        ('shot_rearq', 'Rear quarter, exhaust side', 'Staggered pipes, solo seat and the bobbed rear mudguard.'),
        ('shot_glamp', 'Brake light and indicators', 'The same lamp as the classic, on the end of the bobbed guard.'),
        ('shot_midr', 'Engine, right side', 'The V-twin with its fins, covers and pipes.'),
    ]),
]
body = '<main>\n<div class="k">Bike Models V2 · Definition pass · Classic and cruiser</div>'
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
open(os.path.join(ROOT, 'out', 'classic_cruiser_definition.html'), 'w').write(head + body + '\n</main>')
print(n, len(head + body))
