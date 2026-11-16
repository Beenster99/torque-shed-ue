"""Builds the adventure Definition page (out/adventure_definition.html)."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Adventure Definition</title>')
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
SETS = [
    ('corvano_adventure', 'Corvano adventure, round 2', 'Your 13 points on the adventure lead bike, redrawn from your four reference photos. The front is now taller and made of separate parts: a painted nose the tank runs into, a dark headlamp mask under its brow, a beak in a different colour, a grey side panel over a black panel with louvres, deflector wings and a tall screen. Rendered straight from the model, nothing retouched.', [
        ('shot_side', 'Whole bike, right side', 'Taller front end, bigger tank reaching down over the frame, two-sided swing arm, slim squared silencer.'),
        ('shot_front34', 'Whole bike, front quarter', 'Nose, mask, beak, grey and black side panels, wings and screen as separate parts.'),
        ('shot_rearq', 'Rear quarter, exhaust side', 'The squared silencer with two outlets, tucked in over the swing arm to leave room for panniers. Grab rails come down to the seat frame under the saddle.'),
        ('shot_rearq_front', 'Front quarter, closer', 'Louvres ahead of the radiator, twin pipes coming down the front of the engine.'),
        ('shot_fpeg', 'Pipes and underpan', 'The two front pipes gather and run under the engine inside the bash plate, whose front lip now stands ahead of them.'),
        ('shot_nose', 'Nose, lamps and beak', 'Twin lamps in the dark mask under the painted brow, the beak below in graphite.'),
        ('shot_cockpit', 'Cockpit', 'Large TFT above the bars, hand guards deep enough to cover the levers.'),
        ('shot_tail', 'Tail lamp and indicators', 'A clear red lens over LED cells, indicators with real lenses each side, rack rails joined to the frame.'),
        ('shot_rearq_l', 'Rear quarter, left side', 'Chain side: the rear cylinder pipe drops down this side and tucks under to the collector.'),
        ('shot_midr', 'Engine and exhaust, right side', 'Link pipe from the collector up into the silencer.'),
        ('shot_rear', 'Rear wheel', 'Two-sided alloy swing arm, rear brake, silencer outlets.'),
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
        shutil.copy(p, dst + f'{key}_r2_{tag}.png')
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
