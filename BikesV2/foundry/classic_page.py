"""Builds the classic and cruiser Definition first-look page (out/classic_cruiser_definition.html)."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Classic and Cruiser Definition</title>')
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
SETS = [
    ('hartwell_classic', 'Hartwell classic, round 2', 'Your eight points from 16 Nov, all in: fluted headlight glass, deeper clocks, plainer grips, chrome petrol cap, the exhaust leaving the engine in one clean sweep, a hollow silencer with no cuff, a squared-off swing arm, and the shocks bolted into a forked bracket on the arm. Pegs untouched. Rendered straight from the model, nothing retouched.', [
        ('shot_rearq_front', 'Whole bike, front quarter', 'The header now comes forward out of the head and sweeps down in one curve.'),
        ('shot_rearq', 'Rear quarter, exhaust side', 'Squared swing arm, hollow silencer, twin shocks.'),
        ('shot_rear', 'Shock bracket and silencer end', 'Each shock eye sits in a forked bracket welded to the top of the arm, with the bolt through. The silencer draws in to a thin lip round an open bore.'),
        ('shot_midr', 'Engine, right side', 'The pipe leaves the port in line with it, no kink, then runs back flat along the cases.'),
        ('shot_nose', 'Headlamp', 'Moulded glass with upright flutes and a cross-cut block, over a bright silvered bowl.'),
        ('shot_cockpit', 'Cockpit', 'Plain grips, steel levers, chrome petrol cap.'),
        ('shot_clocks', 'Clocks', 'Deeper chrome cans.'),
        ('shot_glamp', 'Brake light and indicators', 'Unchanged from round 1.'),
    ]),
    ('lodestar_cruiser', 'Lodestar cruiser, round 1', 'Your nine points from 16 Nov: heel plates gone, the forward pegs on cast arms out of the frame (peg positions unchanged, so the rider sits exactly as before), the brake light moved up on to the mudguard with traditional indicators, a bigger fluted headlamp, all-chrome real shocks, pipes lined up with open hollow silencers, bright bars, levers and mirrors, larger chrome clocks.', [
        ('shot_rearq_front', 'Whole bike, front quarter', 'Bigger headlamp, bright bars and mirrors, the peg arms out of the down tubes.'),
        ('shot_rearq', 'Rear quarter, exhaust side', 'Open silencers with no black caps, chrome shocks.'),
        ('shot_fpeg', 'Forward pegs', 'No heel plates. Each peg and pedal stands on a cast arm that grows out of the frame down tube. The pegs themselves have not moved.'),
        ('shot_glamp', 'Brake light and indicators', 'The lamp now stands on the mudguard instead of past its end.'),
        ('shot_rear', 'Shocks and silencer ends', 'Chrome shocks in their brackets on the arm, hollow outlets.'),
        ('shot_midr', 'Engine and pipes, right side', 'Each pipe arrives in line with its silencer.'),
        ('shot_nose', 'Headlamp', 'The same fluted glass as the classic, in a bigger shell.'),
        ('shot_cockpit', 'Cockpit', 'Larger chrome clocks, steel levers.'),
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
        shutil.copy(p, dst + f'{key}_r2_{tag}.png')
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
