"""Page showing the designer sketch sheets (out/portfolio_sheets.html)."""
import base64, glob, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = open(os.path.join(ROOT, 'out', 'definition_proof.html')).read()
head = s[:s.index('<main>')].replace('<title>Definition Proof 3</title>', '<title>Portfolio Sheets</title>')
body = '<main>\n<div class="k">Bike Models V2 · Phase 6 · Designer portfolio</div>\n<h1>Designer sketch sheets</h1>\n<p class="lede">A loose pencil working sheet per bike: the big three-quarter view, a side elevation with its real proportions, detail studies and handwritten notes. Every line is taken from the actual model, so the drawing is exactly the bike in the game, and the numbers are the model\'s own.</p>\n'
for f in sorted(glob.glob(os.path.join(ROOT, 'out', 'sheets', '*.jpg'))):
    body += f'<figure style="margin:0 0 28px"><img style="width:100%" alt="{os.path.basename(f)}" src="data:image/jpeg;base64,{base64.b64encode(open(f, "rb").read()).decode()}"></figure>\n'
open(os.path.join(ROOT, 'out', 'portfolio_sheets.html'), 'w').write(head + body + '</main>')
