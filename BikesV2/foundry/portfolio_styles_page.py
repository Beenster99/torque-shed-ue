"""Sample page for the designer portfolio: one bike in each sketch style (out/portfolio_styles.html)."""
import base64, io, os
from PIL import Image
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Portfolio Styles</title>')
body = '<main>\n<div class="k">Bike Models V2 · Phase 6 · Designer portfolio</div>\n<h1>Three sketch styles to choose from</h1>\n<p class="lede">The portfolio will show every bike and a selection of parts as artist sketches, in a Word file and a PDF. These are three ways of drawing the same Kazeda sport. Each is made from the real model, so the proportions are exact. Pick one for the whole book, or a mix (for example marker for the bikes and pencil for the parts).</p>\n<div class="grid">'
for st, t in (('pencil', 'A. Pencil: graphite line and hatching on cream paper.'), ('marker', 'B. Ink and marker: black ink line with flat colour washes in the bike\'s own paint.'), ('blueprint', 'C. Blueprint: white line on blue drafting paper.')):
    for shot in ('side', 'front34'):
        im = Image.open(os.path.join(ROOT, 'out', 'sketch', f'kazeda_sport_def_shot_{shot}_{st}.jpg')).convert('RGB')
        im = im.resize((1100, int(im.height * 1100 / im.width)))
        b = io.BytesIO()
        im.save(b, 'JPEG', quality=84)
        body += f'<figure><img alt="{st} {shot}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{t.split(":")[0]}</strong>{t.split(":")[1]}</figcaption></figure>\n'
open(os.path.join(ROOT, 'out', 'portfolio_styles.html'), 'w').write(head + body + '</div>\n</main>')
