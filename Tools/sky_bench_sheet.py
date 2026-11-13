"""Lay the sky bench's shots out on one sheet (skies plan): run after the game's -TorqueSkyBench.

    python Tools/sky_bench_sheet.py [Saved/Screenshots/SkyBench folder]

Reads bench.json there (written by ATorqueSky with each shot's sky numbers) and writes sheet.html beside
the shots (open it in a browser: each shot with its name, note and numbers), and sheet.jpg too when the
Pillow library is installed (a grid of the shots with their names, to post or keep).
"""
import html
import json
import os
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "Saved", "Screenshots", "SkyBench")


def main():
    with open(os.path.join(DIR, "bench.json")) as f:
        shots = json.load(f)["shots"]
    cells = []
    for s in shots:
        cells.append('<figure><img src="%s" loading="lazy"><figcaption><b>%s</b> %s<br><small>%s</small></figcaption></figure>' % (
            html.escape(s["file"]), html.escape(s["name"]), html.escape(s.get("note", "")), html.escape(s.get("sky", ""))))
    page = """<!doctype html><meta charset="utf-8"><title>Sky bench</title>
<style>body{font:13px system-ui;background:#111;color:#ddd;margin:16px}main{display:grid;grid-template-columns:repeat(auto-fill,minmax(420px,1fr));gap:12px}
figure{margin:0;background:#1c1c1c;padding:6px;border-radius:6px}img{width:100%%;display:block;border-radius:4px}small{color:#999}</style>
<h1>Sky bench</h1><main>%s</main>""" % "\n".join(cells)
    with open(os.path.join(DIR, "sheet.html"), "w") as f:
        f.write(page)
    print("wrote " + os.path.join(DIR, "sheet.html"))
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("(no Pillow: sheet.jpg skipped)")
        return
    w, h, cols = 480, 270, 5
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * w, rows * (h + 22)), (17, 17, 17))
    d = ImageDraw.Draw(sheet)
    for i, s in enumerate(shots):
        p = os.path.join(DIR, s["file"])
        x, y = (i % cols) * w, (i // cols) * (h + 22)
        if os.path.exists(p):
            sheet.paste(Image.open(p).convert("RGB").resize((w, h)), (x, y))
        d.text((x + 4, y + h + 4), s["name"], fill=(230, 230, 230))
    sheet.save(os.path.join(DIR, "sheet.jpg"), quality=88)
    print("wrote " + os.path.join(DIR, "sheet.jpg"))


if __name__ == "__main__":
    main()
