"""Builds the chopper bespoke parts review page (out/chopper_parts.html)."""
import base64, io, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Chopper Parts</title>')
PICS = [('pegs.png', 'Footpegs', 'Rows: Ringed, Torpedo, Faceted. Columns: chrome, brushed steel, gold, brass. All hang on the same pivot, so the boot sits exactly where it did.'),
        ('covers.png', 'Engine covers', 'Rows: Finned, Starburst, Target. Columns: chrome, brushed steel, gold, brass. Made to any size, so one design does the cam caps and the clutch covers.'),
        ('fit_brass_engine.png', 'Fitted: Starburst covers in brass', 'On the chopper lead, in place of the plain chrome caps on the heads and the clutch covers.'),
        ('fit_brass_fpeg.png', 'Fitted: Torpedo pegs in brass', 'On the forward controls of the chopper lead.'),
        ('lamps.png', 'Lamp shells', 'Back row to front: Grille, Finned, Bullet. Left to right: chrome, brushed steel, gold, brass. The same design is made for the headlamp and the tail lamp; the glass and reflector here are stand-ins.'),
        ('fit_brass_lamp.png', 'Fitted: Bullet headlamp in brass', 'The bike keeps its own fluted glass, reflector and bulb.'),
        ('fit_brass_side_long.png', 'Fitted: a full brass set', 'Bullet lamps front and rear, Starburst covers and Torpedo pegs on the chopper lead.'),
        ('fit_engine_pots.png', 'Engine: Twin pot top end in chrome', 'Two domed rocker pots with acorn nuts on each head and polished pushrod tubes up each cylinder, shown with Target covers.'),
        ('fit_engine_finned.png', 'Engine: Finned top end in brass', 'A deep-finned rocker cover with a turned breather cap on each head, shown with Target covers.'),
        ('sissy.png', 'Sissy bars', 'Left to right: Chain, Scrollwork and Filigree in chrome, then Chain in brushed steel, Scrollwork in brass and Filigree in gold. Shown from the back rest up. All three stand on the same feet as the plain bar.'),
        ('fit_chain_sissy.png', 'Fitted: Chain sissy bar in chrome', 'Every link is welded to the next, with two chain cross bars above the back rest.'),
        ('fit_chain_side_long.png', 'Fitted: Chain bar and hooked blade wheels', 'The chopper lead on the five hooked blade billet wheels, in chrome.'),
        ('fit_scroll_sissy.png', 'Fitted: Scrollwork sissy bar in brass', 'Wrought scrolls, a star medallion with a red stone and a ball finial.'),
        ('fit_scroll_side_long.png', 'Fitted: a full brass build', 'Scrollwork bar, leaf blade wheels, Bullet lamps, Starburst covers and Torpedo pegs, all in brass.'),
        ('fit_filigree_sissy.png', 'Fitted: Filigree sissy bar in gold', 'A pointed arch, diamond lattice with rosettes, an oval boss and a flame finial.'),
        ('fit_filigree_rearq.png', 'Fitted: Filigree bar and crossed petal wheels in gold', 'From the exhaust side.'),
        ('fit_bsx_side_long.png', 'On the BSX chopper: a brushed steel set', 'Filigree bar, seven S blade wheels, Faceted pegs, Target covers and Grille lamps, all in brushed steel. Fitted with no changes to the bike.'),
        ('fit_bsx_sissy.png', 'On the BSX chopper: Filigree bar in brushed steel', 'The bar stands on the bike\'s own feet behind its back rest.'),
        ('fit_kazeda_side_long.png', 'On the Kazeda chopper: a gold set', 'Chain bar, nine split blade wheels, Ringed pegs, Finned covers and Finned lamps, all in gold.'),
        ('fit_kazeda_sissy.png', 'On the Kazeda chopper: Chain bar in gold', 'This bike has a short bar and no back rest, so the chain bar is short too.'),
        ('../wheels/billet_reaper5.png', 'Billet wheel: five hooked blades', 'Rear 18 x 5.5 in on the left, front 21 x 2.15 in on the right. Chrome.'),
        ('../wheels/billet_cyclone9.png', 'Billet wheel: nine split blades', 'Chrome.'),
        ('../wheels/billet_bloom8.png', 'Billet wheel: crossed petals', 'Shown in gold.'),
        ('../wheels/billet_fang6.png', 'Billet wheel: six leaf blades with a rib', 'Shown in brass.'),
        ('../wheels/billet_twist7.png', 'Billet wheel: seven S blades', 'Shown in brushed steel.')]
body = '<main>\n<div class="k">Bike Models V2 · Choppers · Bespoke parts</div>\n<h1>Machined parts to order</h1>\n<p class="lede">Bespoke chopper parts in four metals: three footpegs, three engine covers, three lamp shells and two engine top ends, and now three show sissy bars and five cut billet wheels (added 17 Nov, at the end of the page). Each fits any chopper without moving a peg or a mounting. The wheels are our own patterns guided by your pictures. Every spoke on every wheel in the library is now tested by a script to end inside the rim metal, front and rear, and all 22 styles pass. Rendered straight from the models, nothing retouched.</p>\n<div class="grid">'
for f, title, note in PICS:
    p = os.path.join(ROOT, 'out', 'chopper_parts', f)
    if not os.path.exists(p):
        continue
    im = Image.open(p).convert('RGB')
    im = im.resize((1300, int(im.height * 1300 / im.width)))
    b = io.BytesIO()
    im.save(b, 'JPEG', quality=84)
    body += f'<figure><img alt="{title}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> {note}</figcaption></figure>\n'
open(os.path.join(ROOT, 'out', 'chopper_parts.html'), 'w').write(head + body + '</div>\n</main>')
