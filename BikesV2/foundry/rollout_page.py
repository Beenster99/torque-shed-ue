"""Builds the family rollout contact sheet (out/rollout_sheet.html): every bike that is not a family lead,
side and exhaust-side rear quarter, grouped by family."""
import base64, io, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Family Rollout</title>')
FAMS = [
    ('Classic', 'Lead: BSX (signed off). Round 2: each brand has its own rear mudguard and brake light (Corvano short guard and a small bullet lamp, Kazeda long guard with a flicked tip and a slim wide lamp, Lodestar deep long guard and a tall lamp, Rheinhart mid guard with a rolled lip and a big round lamp); the Corvano and Rheinhart are air cooled, so their radiators are gone; the Rheinhart plug leads lie along the barrels.', ['corvano_classic', 'kazeda_classic', 'rheinhart_classic', 'lodestar_classic']),
    ('Cruiser', 'Lead: Lodestar (signed off). Round 2: the Corvano tank is wider and clears the frame, and its front tyre is smaller; the rear brake reservoir now stands on the master cylinder on every cruiser, and the brake hose runs along the swingarm.', ['corvano_cruiser', 'kazeda_cruiser', 'rheinhart_cruiser']),
    ('Adventure', 'Lead: Corvano (signed off). Round 2: grab rails 40 mm lower on all of them, beside the pillion seat and not above it; front mudguards down close to the tyre; taller screens on the BSX and the Rheinhart; the Kazeda and Varga side panels have their own cutaways and the Varga one is colour coded; the BSX has the full pillion seat over its seat frame; every swingarm has two legs, so the rear brake and its hose are carried; new wire wheels on the BSX, Rheinhart and Varga.', ['hartwell_adventure', 'kazeda_adventure', 'rheinhart_adventure', 'varga_adventure']),
    ('Chopper', 'Lead: Lodestar (signed off). Round 2: the Kazeda saddle sits 10 mm higher, clear of the frame rail.', ['hartwell_chopper', 'kazeda_chopper']),
    ('MX', 'Lead: Varga (signed off). Round 2: the Corvano silencer is tucked inside the side plate; new wire wheels (Corvano heavy black spokes on a gold hub, Kazeda gold flat rims).', ['corvano_mx', 'kazeda_mx']),
    ('Naked', 'Lead: Corvano (signed off). Round 2: the Kazeda number plate hangs on a short arm from the end of the tail; the Varga radiator is shorter and sits ahead of the frame, and its silencer is lower so the pillion footrest clears it; two-legged swingarms.', ['hartwell_naked', 'kazeda_naked', 'rheinhart_naked', 'varga_naked']),
]
body = '<main>\n<div class="k">Bike Models V2 · Definition pass · Family rollout</div>\n<h1>Family rollout, round 2 changes</h1>\n<p class="lede">Every bike that is not a family lead, after your second set of notes on 16 November. One change runs through every family: no bike has a one-legged swingarm any more, so the rear brake caliper and its hose always have an arm to sit on. Sport and cafe are on their own pages. Rendered straight from the models at low quality, nothing retouched.</p>'
n = 0
for fam, note, names in FAMS:
    figs = []
    for nm in names:
        for tag, lab in (('side_long' if 'chopper' in nm else 'side', 'right side'), ('rearq', 'rear quarter'), ('front34', 'front three-quarter') if 'adventure' in nm else ('engine', 'engine close-up')):
            p = os.path.join(ROOT, 'out', 'renders', f'{nm}_def_shot_{tag}.png')
            if not os.path.exists(p):
                continue
            im = Image.open(p).convert('RGB')
            im = im.resize((1000, int(im.height * 1000 / im.width)))
            b = io.BytesIO()
            im.save(b, 'JPEG', quality=80)
            title = nm.replace('_', ' ').title().replace('Mx', 'MX').replace('Hartwell', 'BSX')
            figs.append(f'<figure><img alt="{title}, {lab}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> {lab}</figcaption></figure>')
    n += len(figs)
    body += f'\n<h1 style=margin-top:48px>{fam}</h1>\n<p class="lede">{note}</p>\n<div class="grid">' + '\n'.join(figs) + '</div>'
open(os.path.join(ROOT, 'out', 'rollout_sheet.html'), 'w').write(head + body + '\n</main>')
print(n, len(head + body))
