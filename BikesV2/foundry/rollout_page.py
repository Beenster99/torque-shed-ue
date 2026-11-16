"""Builds the family rollout contact sheet (out/rollout_sheet.html): every bike that is not a family lead,
side and exhaust-side rear quarter, grouped by family."""
import base64, io, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Family Rollout</title>')
FAMS = [
    ('Classic', 'Lead: BSX (signed off). Carried over: chrome clocks, fluted lamp, steel levers, mudguard tail lamp, real twin shocks where the bike has them. New in this pass: the Kazeda four and the Rheinhart flat twin now have detailed engines. Still to do: twin shocks on the Corvano and Rheinhart.', ['corvano_classic', 'kazeda_classic', 'rheinhart_classic', 'lodestar_classic']),
    ('Cruiser', 'Lead: Lodestar (signed off). Carried over: chrome controls and mirrors, big chrome clocks, chrome twin shocks, pipes that run straight into open silencers. New in this pass: detailed engines on the Kazeda and the Rheinhart.', ['corvano_cruiser', 'kazeda_cruiser', 'rheinhart_cruiser']),
    ('Adventure', 'Lead: Corvano (signed off). New in this pass: the old flat fairings are gone. Each bike has the lead\'s shaped nose, panels, screen and rack with its own brand face: BSX a stub beak, round lamp and tall screen; Kazeda a long drooping beak, slit running light over twin projectors and four louvres; Rheinhart a broad flat beak, square lamps under a light bar and the tallest screen; Varga a short high rally beak, vertical lamp stack and low screen.', ['hartwell_adventure', 'kazeda_adventure', 'rheinhart_adventure', 'varga_adventure']),
    ('Chopper', 'Lead: Lodestar (signed off). Carried over: chrome frame, chrome controls, chrome pipes into open silencers, slanted grips. New in this pass: detailed triple and four engines, a tank panel on each, and their own sissy bars (BSX a ladder bar, Kazeda a square bar with spikes and a cross brace).', ['hartwell_chopper', 'kazeda_chopper']),
    ('MX', 'Lead: Varga (signed off). Carried over: no mirrors, one front disc, narrow knobbly tyres, heavy spokes, one front mudguard, number boards. New in this pass: tank cut back clear of the forks, radiator shrouds, water pump on the case, and the right-hand side panel stands out over the silencer.', ['corvano_mx', 'kazeda_mx']),
    ('Naked', 'Lead: Corvano (signed off). These share the detail parts of the lead already. New in this pass: the triple, four and flat twin engines are detailed (a pipe from every cylinder, plugs and leads, throttle bodies, covers and bolts).', ['hartwell_naked', 'kazeda_naked', 'rheinhart_naked', 'varga_naked']),
]
body = '<main>\n<div class="k">Bike Models V2 · Definition pass · Family rollout</div>\n<h1>Family rollout, second pass</h1>\n<p class="lede">Every bike that is not a family lead, built with its lead\'s signed-off options and shapes. Sport and cafe were rolled out earlier and are on their own pages. Each family says what is new since the first pass and what is still to do. Rendered straight from the models at low quality, nothing retouched.</p>'
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
