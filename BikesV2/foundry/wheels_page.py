"""Builds the wheel styles review page (out/wheel_styles.html) from out/wheels/*.png."""
import base64, io, os, shutil, sys
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re
src = open(os.path.join(os.path.dirname(__file__), 'wheels3d.py')).read()
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
head = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = head[:head.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Wheel Styles</title>')
rows = re.findall(r'"(\w+)": dict\(label="([^"]+)", ref=(\d+),.*?finish="(\w+)".*?sizes=\(\((\d+), ([\d.]+)\), \((\d+), ([\d.]+)\)\)', src)
dst = '/mnt/project-files/bike_models_v2/wheels/'
os.makedirs(dst, exist_ok=True)
figs = []
for i, (key, label, ref, fin, fd, fw, rd, rw) in enumerate(rows):
    p = os.path.join(ROOT, 'out', 'wheels', key + '.png')
    shutil.copy(p, dst + key + '.png')
    im = Image.open(p).convert('RGB')
    b = io.BytesIO(); im.save(b, 'JPEG', quality=84)
    note = f'From your reference picture {ref}.' if int(ref) else ('Wire spoked, for the MX and adventure bikes.' if key.startswith('wire') else 'Cut from billet, for the choppers.' if key.startswith('billet') else 'An extra of my own.')
    figs.append(f'<figure><img alt="{label}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{i + 1}. {label}</strong> Front {fd} x {float(fw):g} in, rear {rd} x {float(rw):g} in. Shown in {fin.replace("_", " ")}. {note}</figcaption></figure>')
shutil.copy(os.path.join(ROOT, 'out', 'wheels', 'sheet.png'), dst + 'sheet.png')
fitted = []
for fn, cap in (('corvano_naked_pool000150_def_q.png', 'Style 12, six split spokes in bronze, on the Corvano naked.'),
                ('corvano_naked_pool00050_def_shot_rearq.png', 'Style 2, five Y spokes in gunmetal, from the exhaust side.')):
    p = os.path.join(ROOT, 'out', 'renders', fn)
    if os.path.exists(p):
        shutil.copy(p, dst + 'on_bike_' + fn)
        b = io.BytesIO(); Image.open(p).convert('RGB').save(b, 'JPEG', quality=84)
        fitted.append(f'<figure><img alt="{cap}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>On the bike.</strong> {cap}</figcaption></figure>')
body = '''<main>
<div class="k">Bike Models V2 · Wheels</div>
<h1>Wheel styles, first selection</h1>
<p class="lede">Twelve cast and forged styles, then five wire-spoked rims for the MX and adventure bikes (13 to 17, added 16 Nov), then five cut billet wheels for the choppers (18 to 22, added 17 Nov), each built as a rear wheel (left) and a front wheel (right). Every spoke on all 22 styles is now tested by a script to end inside the rim metal, front and rear; the test found short spokes on narrow rims and steeply swept blades, and those are fixed. The swept, turbine and sickle spokes now spread into a foot where they join the rim. Our own designs guided by your reference pictures; no maker's name or exact pattern is copied. Any style can be made in any size and any finish: gloss black, matt black, gunmetal, gold, anodised gold, bronze, chrome, carbon, brushed steel, bright gold, brass. Tell me which to keep, drop or change by number.</p>
<div class="grid">''' + '\n'.join(fitted + figs) + '</div>\n</main>'
open(os.path.join(ROOT, 'out', 'wheel_styles.html'), 'w').write(head + body)
print(len(rows), len(head + body))
