"""Builds the family rollout contact sheet (out/rollout_sheet.html): every bike that is not a family lead,
side and exhaust-side rear quarter, grouped by family."""
import base64, io, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Family Rollout</title>')
FAMS = [
    ('Classic', 'Lead: Hartwell (signed off). Carried over: chrome clocks, fluted lamp, steel levers, mudguard tail lamp, real twin shocks where the bike has them.', ['corvano_classic', 'kazeda_classic', 'rheinhart_classic', 'lodestar_classic']),
    ('Cruiser', 'Lead: Lodestar (signed off). Carried over: chrome controls and mirrors, big chrome clocks, chrome twin shocks, pipes that run straight into open silencers.', ['corvano_cruiser', 'kazeda_cruiser', 'rheinhart_cruiser']),
    ('Adventure', 'Lead: Corvano (signed off). Carried over so far: mirrors, large display, arrow indicators. NOT yet done: each of these still has the old flat fairing; each brand needs its own shaped nose, beak, screen and panels like the lead.', ['hartwell_adventure', 'kazeda_adventure', 'rheinhart_adventure', 'varga_adventure']),
    ('Chopper', 'Lead: Lodestar (signed off). Carried over: chrome frame, chrome controls, chrome pipes into open silencers, slanted grips, sissy bar. NOT yet done: their engines (triple, four) are still plain blocks, and they have no tank panel yet.', ['hartwell_chopper', 'kazeda_chopper']),
    ('MX', 'Lead: Varga (signed off). Carried over: no mirrors, one front disc, narrow knobbly tyres, heavy spokes, one front mudguard, number boards. NOT yet done: pipe inside the side panel, tank clear of the forks, shrouds.', ['corvano_mx', 'kazeda_mx']),
    ('Naked', 'Lead: Corvano (signed off). These share the detail parts of the lead already. NOT yet done: the triple, four and boxer engines are still plain blocks.', ['hartwell_naked', 'kazeda_naked', 'rheinhart_naked', 'varga_naked']),
]
body = '<main>\n<div class="k">Bike Models V2 · Definition pass · Family rollout</div>\n<h1>Family rollout, first pass</h1>\n<p class="lede">Every bike that is not a family lead, built with its lead\'s signed-off options carried over automatically. Sport and cafe were rolled out earlier and are on their own pages. This is a progress sheet, not a finished round: each family says what is still to do. Rendered straight from the models at low quality, nothing retouched.</p>'
n = 0
for fam, note, names in FAMS:
    figs = []
    for nm in names:
        for tag, lab in (('side_long' if 'chopper' in nm else 'side', 'right side'), ('rearq', 'rear quarter')):
            p = os.path.join(ROOT, 'out', 'renders', f'{nm}_def_shot_{tag}.png')
            if not os.path.exists(p):
                continue
            im = Image.open(p).convert('RGB')
            im = im.resize((1000, int(im.height * 1000 / im.width)))
            b = io.BytesIO()
            im.save(b, 'JPEG', quality=80)
            title = nm.replace('_', ' ').title().replace('Mx', 'MX')
            figs.append(f'<figure><img alt="{title}, {lab}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> {lab}</figcaption></figure>')
    n += len(figs)
    body += f'\n<h1 style=margin-top:48px>{fam}</h1>\n<p class="lede">{note}</p>\n<div class="grid">' + '\n'.join(figs) + '</div>'
open(os.path.join(ROOT, 'out', 'rollout_sheet.html'), 'w').write(head + body + '\n</main>')
print(n, len(head + body))
