"""Builds the family rollout contact sheet (out/rollout_sheet.html): every bike that is not a family lead,
side and exhaust-side rear quarter, grouped by family."""
import base64, io, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Family Rollout</title>')
FAMS = [
    ('Classic', 'Lead: BSX (signed off). Changed from your notes: every classic has a two-sided swingarm; the Corvano has low pipes into two nipped silencers (nothing through the seat) and twin shocks; the Kazeda pipe meets its silencer; the Rheinhart has twin shocks.', ['corvano_classic', 'kazeda_classic', 'rheinhart_classic', 'lodestar_classic']),
    ('Cruiser', 'Lead: Lodestar (signed off). Changed from your notes: two-sided swingarms; each tank has its own shape (Corvano slim and low, Kazeda fat and tall, Rheinhart square shouldered); the Corvano has a side cover over the frame and no cable through the seat.', ['corvano_cruiser', 'kazeda_cruiser', 'rheinhart_cruiser']),
    ('Adventure', 'Lead: Corvano (signed off). Changed from your notes: all four have the level tail, the low grab rail and the number plate arm of the lead; the BSX louvres are colour coded; beaks and headlights pushed further apart (BSX stub beak and one round lamp in a bright ring, Kazeda long drooping beak and slit over twin projectors, Rheinhart broad flat bill and two square lamps, Varga short high beak and a vertical stack).', ['hartwell_adventure', 'kazeda_adventure', 'rheinhart_adventure', 'varga_adventure']),
    ('Chopper', 'Lead: Lodestar (signed off). Changed from your notes: the Kazeda four now has two pipes, one into each silencer.', ['hartwell_chopper', 'kazeda_chopper']),
    ('MX', 'Lead: Varga (signed off). Changed from your notes: the tank side pads differ in shape and are colour coded (Corvano red, Kazeda yellow, the lead stays black); the Kazeda rear mudguard no longer turns up.', ['corvano_mx', 'kazeda_mx']),
    ('Naked', 'Lead: Corvano (signed off). Changed from your notes: BSX pipe lined up with its silencer and radiator moved ahead of the frame; Kazeda collector joined to its silencer; Rheinhart one long fat silencer (it is on the left, so look at the side view); Varga pipe turns into line with its silencer.', ['hartwell_naked', 'kazeda_naked', 'rheinhart_naked', 'varga_naked']),
]
body = '<main>\n<div class="k">Bike Models V2 · Definition pass · Family rollout</div>\n<h1>Family rollout, round 1 changes</h1>\n<p class="lede">Every bike that is not a family lead, after your notes of 16 November. All tyres are the reshaped sections, and front tyres are now fitted the right way round. Sport and cafe were rolled out earlier and are on their own pages. Rendered straight from the models at low quality, nothing retouched.</p>'
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
