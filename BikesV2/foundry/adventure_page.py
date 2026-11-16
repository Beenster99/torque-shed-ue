"""Builds the adventure Definition page (out/adventure_definition.html)."""
import base64, io, os, shutil
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Adventure Definition</title>')
dst = '/mnt/project-files/bike_models_v2/renders/definition/'
os.makedirs(dst, exist_ok=True)
SETS = [
    ('corvano_adventure', 'Corvano adventure, round 3', 'Your 12 points from round 2 on the adventure lead bike. The fairing now carries on higher and the dark plates beside the screen grow out of its shoulder line; the beak is flat underneath; the seat is deeper with a clear step up to the pillion; the tail is level; the seat frame is joined in a hoop at the back. Rendered straight from the model, nothing retouched.', [
        ('shot_side', 'Whole bike, right side', 'Level tail, lower grab rail, stepped seat, flat-bottomed beak, lower front mudguard, longer silencer with a vertical end.'),
        ('shot_front34', 'Whole bike, front quarter', 'The fairing carried higher, the side plates beside the screen as part of it, slimmer hand deflectors.'),
        ('shot_rearq', 'Rear quarter, exhaust side', 'Silencer end cut vertical, shorter number plate bracket, frame hoop round the back.'),
        ('shot_rearq_front', 'Front quarter, closer', 'Louvre blades stand out from the black panel and cover the radiator pipe.'),
        ('shot_nose', 'Nose, lamps and beak', 'The beak runs level underneath.'),
        ('shot_seat', 'Seat', 'Deeper pads, a distinct second step for the pillion, the rail beside it.'),
        ('shot_tail', 'Tail, lamp and frame hoop', 'The seat frame joined and looped at the back, lamp in the level tail end.'),
        ('shot_cockpit', 'Cockpit', 'Large TFT, slimmer hand deflectors.'),
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
        shutil.copy(p, dst + f'{key}_r3_{tag}.png')
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
