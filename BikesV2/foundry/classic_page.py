"""Builds the classic and cruiser Definition first-look page (out/classic_cruiser_definition.html)."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Classic and Cruiser Definition</title>')
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
SETS = [
    ('hartwell_classic', 'BSX classic, round 3', 'Round 3 (16 Nov): the headlamp moved forward and up, clear of the headstock and lower yoke; the exhaust now leaves the front of a slightly narrower engine angled down, sweeps down in one wide curve and turns into its horizontal run in another; the bars run dead straight under the grips. Round 2 points all kept. Pegs untouched.', [
        ('shot_rearq_front', 'Whole bike, front quarter', 'Headlamp forward of the fork, the header sweeping down from the head.'),
        ('shot_midr', 'Engine, right side', 'Out of the front of the engine, a wide sweep down, a wide turn into the horizontal.'),
        ('shot_nose', 'Headlamp', 'Clear of the headstock and the lower yoke, on its fork brackets.'),
        ('shot_cockpit', 'Cockpit', 'Grips in line with the bars.'),
        ('shot_rearq', 'Rear quarter, exhaust side', 'Squared swing arm, hollow silencer, twin shocks.'),
        ('shot_rear', 'Shock bracket and silencer end', 'From round 2, unchanged.'),
        ('shot_clocks', 'Clocks', 'From round 2, unchanged.'),
        ('shot_glamp', 'Brake light and indicators', 'From round 1, unchanged.'),
    ]),
    ('lodestar_cruiser', 'Lodestar cruiser, round 2', 'Round 2 (16 Nov): the headlamp moved forward and up, clear of the headstock; the rear brake cylinder and its reservoir moved back to sit low behind the engine by the swingarm pivot, worked from the pedal by a rod along the frame rail; the bars run dead straight under the grips. Round 1 points all kept. Pegs untouched.', [
        ('shot_rearq_front', 'Whole bike, front quarter', 'Headlamp forward, bright bars and mirrors.'),
        ('shot_rearq', 'Rear quarter, exhaust side', 'Open silencers, chrome shocks.'),
        ('shot_midr', 'Engine and pipes, right side', 'The brake reservoir now sits behind the engine, between the pipes and the cases.'),
        ('shot_nose', 'Headlamp', 'Clear of the headstock, same fluted glass as the classic.'),
        ('shot_cockpit', 'Cockpit', 'Grips in line with the pulled-back bars, larger chrome clocks.'),
        ('shot_fpeg', 'Forward pegs', 'Each peg on its cast arm from the down tube, with the reservoir gone from the front.'),
        ('shot_glamp', 'Brake light and indicators', 'From round 1, unchanged.'),
        ('shot_rear', 'Shocks and silencer ends', 'From round 1, unchanged.'),
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
        shutil.copy(p, dst + f'{key}_r3_{tag}.png')
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
