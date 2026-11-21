"""Phase 6: the designer portfolio book of the V2 bikes (beenster, 14 Nov: a compendium for himself, DOCX and PDF).
One page a bike by brand (the white-background renders, its facts, its kit options, his own sketch where there is
one), the brands and families up front, the chopper bespoke parts and billet wheels at the back.

    python3 foundry/portfolio_book.py            -> /mnt/project-files/bike_models_v2/portfolio/TorqueShed_Bike_Portfolio.docx + .pdf

Pictures: /mnt/project-files/bike_models_v2/portfolio_renders/<model>/ (thread "Render bike views for Copilot
sketches"); beenster's sketches: /mnt/project-files/bike_models_v2/portfolio_sketches/ (a file goes with a bike when
its name holds the bike's id or its brand and family; the rest go in the sketchbook chapter at the back)."""
import glob
import json
import os
import re
import subprocess
import sys

from docx import Document
from docx.enum.section import WD_ORIENT
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Mm, Pt, RGBColor
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = "/mnt/project-files/bike_models_v2/"
RENDERS, SKETCHES, OUT = FILES + "portfolio_renders/", FILES + "portfolio_sketches/", FILES + "portfolio/"
TMP = os.path.join(ROOT, "out", "tmp", "book")
INK, SOFT, RULE = RGBColor(0x1B, 0x1D, 0x21), RGBColor(0x6A, 0x6F, 0x78), "B9BEC6"
FAMILIES = ["Naked", "Sport", "Cafe", "Classic", "Adventure", "MX", "Cruiser", "Chopper"]
FAMILY_LINE = {
    "Naked": "The bike with nothing hidden: engine and frame on show, upright bars, a short tail.",
    "Sport": "Full fairing, clip-on bars, a crouched rider and a tail that kicks up behind.",
    "Cafe": "A racer for the road in the old style: low bars, a humped seat, a round lamp.",
    "Classic": "The upright roadster: bench seat, twin shocks, chrome, spoked wheels.",
    "Adventure": "Tall and long-legged, with a beak, a screen and room for luggage.",
    "MX": "Light, tall and narrow: long suspension, knobbly tyres, a number board for a face.",
    "Cruiser": "Long and low, feet forward, a deep seat and a fat rear tyre.",
    "Chopper": "A rigid frame, a raked-out fork and as much machined metal as it will carry.",
}
ENGINE = {"vtwin90": "90 degree V-twin", "vtwin45": "45 degree V-twin", "single": "single", "parallel_twin": "parallel twin", "triple": "inline triple",
          "inline4": "inline four", "boxer": "boxer twin", "vtwin60": "60 degree V-twin", "v4": "V4"}
KIT_WORD = {"tank_seat": "Tank and seat", "tail": "Tail", "front": "Front", "exhaust": "Exhaust", "bars": "Bars", "extras": "Extras", "paint": "Paint"}


def picture(path, width=1500, quality=84):
    """A copy sized for the page (the renders are 2400 wide: the book would be 300 MB)."""
    os.makedirs(TMP, exist_ok=True)
    dst = os.path.join(TMP, re.sub(r"[^A-Za-z0-9_.-]", "_", os.path.relpath(path, "/"))[-150:] + f"_{width}.jpg")
    if not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(path):
        im = Image.open(path)
        if im.mode in ("RGBA", "LA", "P"):
            im = im.convert("RGBA")
            bg = Image.new("RGB", im.size, (255, 255, 255))
            bg.paste(im, mask=im.split()[3])
            im = bg
        im = im.convert("RGB")
        if im.width > width:
            im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
        im.save(dst, quality=quality)
    return dst


def fit(path, max_w, max_h):
    """(width, height) in mm for a picture kept inside a box."""
    w, h = Image.open(path).size
    k = min(max_w / w, max_h / h)
    return w * k, h * k


class Book:
    def __init__(self):
        self.d = Document()
        s = self.d.sections[0]
        s.orientation = WD_ORIENT.LANDSCAPE
        s.page_width, s.page_height = Mm(297), Mm(210)
        s.left_margin = s.right_margin = Mm(16)
        s.top_margin, s.bottom_margin = Mm(13), Mm(13)
        self.W, self.H = 297 - 32, 210 - 26
        st = self.d.styles["Normal"]
        st.font.name, st.font.size, st.font.color.rgb = "Arial", Pt(9.5), INK
        st.element.rPr.rFonts.set(qn("w:eastAsia"), "Arial")
        st.paragraph_format.space_after = Pt(3)
        foot = s.footer.paragraphs[0]
        foot.alignment = WD_ALIGN_PARAGRAPH.RIGHT
        r = foot.add_run("Torque Shed  |  Bike Models V2  |  designer portfolio")
        r.font.size, r.font.color.rgb = Pt(7.5), SOFT
        self.first = True

    def page(self):
        # (the break rides on the page's first line: a break paragraph of its own spilled full pages on to a blank one)
        self.turn = not self.first
        self.first = False

    def text(self, words, size=9.5, bold=False, colour=INK, after=3, align=None, caps=False, italic=False, cell=None):
        p = (cell or self.d).add_paragraph()
        p.paragraph_format.space_after, p.paragraph_format.space_before = Pt(after), Pt(0)
        if cell is None and getattr(self, "turn", False):
            p.paragraph_format.page_break_before = True
            self.turn = False
        if align:
            p.alignment = align
        r = p.add_run(words.upper() if caps else words)
        r.font.size, r.font.bold, r.font.italic, r.font.color.rgb = Pt(size), bold, italic, colour
        if caps:
            r.font.size = Pt(size)
            rpr = r._element.get_or_add_rPr()
            sp = OxmlElement("w:spacing")
            sp.set(qn("w:val"), "30")
            rpr.append(sp)
        return p

    def rule(self):
        p = self.d.add_paragraph()
        p.paragraph_format.space_after = Pt(4)
        pb = OxmlElement("w:pBdr")
        b = OxmlElement("w:bottom")
        for k, v in (("val", "single"), ("sz", "6"), ("space", "1"), ("color", RULE)):
            b.set(qn("w:" + k), v)
        pb.append(b)
        p._p.get_or_add_pPr().append(pb)

    def table(self, rows, cols, widths):
        t = self.d.add_table(rows=rows, cols=cols)
        t.alignment = WD_TABLE_ALIGNMENT.CENTER
        self.fixed(t, widths)
        for row in t.rows:
            for c, w in zip(row.cells, widths):
                c.width = Mm(w)
                for p in c.paragraphs:
                    p.paragraph_format.space_after = Pt(0)
        return t

    @staticmethod
    def fixed(t, widths):
        """Column widths that Word and LibreOffice both keep (a fixed layout, the grid and every cell told)."""
        t.autofit = False
        pr = t._tbl.tblPr
        lay = OxmlElement("w:tblLayout")
        lay.set(qn("w:type"), "fixed")
        pr.append(lay)
        tw = pr.find(qn("w:tblW"))
        if tw is None:
            tw = OxmlElement("w:tblW")
            pr.append(tw)
        tw.set(qn("w:type"), "dxa")
        tw.set(qn("w:w"), str(round(sum(widths) * 56.7)))
        for col, w in zip(t.columns, widths):
            col.width = Mm(w)
        for row in t.rows:
            for c, w in zip(row.cells, widths):
                c.width = Mm(w)

    def image(self, cell, path, max_w, max_h, width=1500):
        src = picture(path, width)
        w, h = fit(src, max_w, max_h)
        p = cell.paragraphs[0] if cell.paragraphs and not cell.paragraphs[0].runs else cell.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.space_after = Pt(0)
        p.add_run().add_picture(src, width=Mm(w), height=Mm(h))

    def swatches(self, cell, colours):
        p = cell.add_paragraph()
        p.paragraph_format.space_after = Pt(2)
        for c in colours:
            r = p.add_run("██ ")
            r.font.size, r.font.color.rgb = Pt(13), RGBColor.from_string(c.lstrip("#").upper())


def wheel_word(w):
    if w.get("wire") or w.get("style") in ("wire", "spoked"):
        return "wire spoked"
    if w.get("style"):
        return "cast, style " + str(w["style"]).replace("_", " ")
    return f"cast, {w['spokes']} spokes" if w.get("spokes") else "cast"


def shown_brand(brands, key):
    return brands[key]["name"]


def sketches_for(model, brand_name, family, files):
    keys = [model, model.replace("hartwell", "bsx"), f"{brand_name}_{family}".lower(), f"{brand_name} {family}".lower(), f"{brand_name}-{family}".lower()]
    return [f for f in files if any(k in os.path.basename(f).lower() for k in keys)]


def build():
    brands = json.load(open(os.path.join(ROOT, "data", "brands.json")))["brands"]
    pool = json.load(open(os.path.join(ROOT, "data", "pool.json")))
    models = {os.path.basename(f)[:-5]: json.load(open(f)) for f in sorted(glob.glob(os.path.join(ROOT, "data", "models", "*.json")))}
    sketch_files = sorted(f for f in glob.glob(SKETCHES + "**/*", recursive=True) if f.lower().endswith((".png", ".jpg", ".jpeg", ".webp")))
    used = set()
    b = Book()

    # ---- cover
    b.page()
    b.text("Torque Shed", 12, colour=SOFT, caps=True, after=2)
    b.text("Bike Models V2", 40, bold=True, after=0)
    b.text("Designer portfolio", 20, colour=SOFT, after=8)
    b.rule()
    hero = RENDERS + "corvano_cafe/corvano_cafe_2_front_quarter.jpg"
    t = b.table(1, 1, [b.W])
    b.image(t.rows[0].cells[0], hero, b.W, 112, 2000)
    b.text(f"{len(models)} motorcycles  |  {len(brands)} makers  |  {len(FAMILIES)} families  |  every part drawn to fit every other", 10.5, colour=SOFT, align=WD_ALIGN_PARAGRAPH.CENTER, after=0)

    # ---- the idea
    b.page()
    b.text("The idea", 9, colour=SOFT, caps=True, after=1)
    b.text("Two friends start from the same frame and end with clearly different bikes", 22, bold=True, after=6)
    b.rule()
    t = b.table(1, 2, [b.W * 0.5, b.W * 0.5])
    left, right = t.rows[0].cells
    for words in (
        "Bike Models V2 is the motorcycle range of Torque Shed, drawn from scratch for the Unreal port. Each bike begins as a side elevation: wheels, engine, frame, "
        "tank, seat and tail drawn as outlines to millimetre sizes. The outlines are then built in three dimensions by script, detailed part by part, and exported to the game.",
        "The makers are invented. Each has a spirit, a frame type, engines it favours and a handful of signature shapes, so a bike is recognisably its maker's from "
        "across the yard. No real maker's name, badge or exact design is used anywhere.",
        "Under the styling, every family shares a hidden mounting standard. A tank from one maker sits on another's frame; joints overlap so mixed parts still close up. "
        "On top of that each bike has kit options (other tanks, seats, tails, screens and exhausts), and the choppers have a catalogue of machined parts in four metals."):
        b.text(words, 10, after=7, cell=left)
    b.text("The eight families", 11, bold=True, after=4, cell=right)
    for fam in FAMILIES:
        n = sum(1 for m in models.values() if m["family"] == fam)
        p = b.text("", 9.5, after=3, cell=right)
        r = p.add_run(f"{fam}  ")
        r.font.bold, r.font.size = True, Pt(9.5)
        r = p.add_run(f"({n})  ")
        r.font.color.rgb, r.font.size = SOFT, Pt(9.5)
        p.add_run(FAMILY_LINE[fam]).font.size = Pt(9.5)

    # ---- the makers
    b.page()
    b.text("The makers", 9, colour=SOFT, caps=True, after=1)
    b.text("Six invented marques", 22, bold=True, after=6)
    b.rule()
    order = [k for k in brands if any(m["brand"] == k for m in models.values())]
    t = b.table(2, 3, [b.W / 3] * 3)
    for i, key in enumerate(order[:6]):
        br, c = brands[key], t.rows[i // 3].cells[i % 3]
        b.text(br["name"], 16, bold=True, after=1, cell=c)
        b.swatches(c, br.get("colours", []))
        b.text(br["spirit"], 9.5, italic=True, colour=SOFT, after=3, cell=c)
        b.text("Frame: " + br.get("frame", "").replace("_", " "), 9, after=1, cell=c)
        b.text("Engines: " + ", ".join(ENGINE.get(e, e) for e in br.get("engines", [])), 9, after=1, cell=c)
        b.text("Signature: " + ", ".join(br.get("signature", [])), 9, after=1, cell=c)
        fams = sorted({m["family"] for m in models.values() if m["brand"] == key}, key=FAMILIES.index)
        b.text("Builds: " + ", ".join(fams), 9, colour=SOFT, after=12, cell=c)

    # ---- a page a bike, by maker
    for key in order:
        br = brands[key]
        mine = sorted((k for k, m in models.items() if m["brand"] == key), key=lambda k: FAMILIES.index(models[k]["family"]))
        for model in mine:
            m = models[model]
            g, eng = m["geometry"], m.get("engine", {})
            b.page()
            title = f"{br['name']} {m['family']}"
            work = re.search(r"working title ([^)]+)", m.get("name", ""))
            b.text(f"{br['name']}  |  {br['spirit']}", 8.5, colour=SOFT, caps=True, after=1)
            head = b.text(title, 24, bold=True, after=2)
            if work:
                r = head.add_run(f"   {work.group(1)}")
                r.font.size, r.font.color.rgb = Pt(12), SOFT
            b.rule()
            shots = {n: RENDERS + f"{model}/{model}_{n}.jpg" for n in ("1_profile", "2_front_quarter", "3_rear_quarter", "4_low_front")}
            mine_sk = sketches_for(model, br["name"], m["family"], sketch_files)
            used.update(mine_sk)
            # (a chopper's kit list is long: its pictures a little smaller so the page still holds)
            tight = sum(len(json.dumps([v.get("name", "") for v in vs if isinstance(v, dict)])) for g_, vs in m.get("kit", {}).items() if isinstance(vs, list)) > 290
            t = b.table(1, 2, [b.W * 0.62, b.W * 0.38])
            big, side = t.rows[0].cells
            if os.path.exists(shots["1_profile"]):
                b.image(big, shots["1_profile"], b.W * 0.62 - 6, 86 if not tight else 72, 1800)
            # the facts
            b.text(FAMILY_LINE[m["family"]], 9.5, italic=True, colour=SOFT, after=5, cell=side)
            cyl = len(eng.get("cylinders", []))
            facts = [("Engine", m.get("engine_label") or ENGINE.get(eng.get("layout", ""), eng.get("layout", ""))),
                     ("Frame", m.get("frame_label", "")),
                     ("Wheelbase", f"{g['wheelbase']} mm"), ("Rake and trail", f"{g['rake']} degrees, {g['trail']} mm"),
                     ("Seat height", f"{g['seat_height']} mm"), ("Ground clearance", f"{g.get('ground_clearance', '-')} mm"),
                     ("Fork", ("upside-down" if m["fork"].get("usd") else "conventional") + f", {m['fork']['length']} mm"),
                     ("Rear", "rigid" if m.get("swingarm", {}).get("rigid") else ("twin shocks" if m.get("shock", {}).get("twin") else "single shock")
                      + (", shaft drive" if m.get("swingarm", {}).get("shaft") else "")),
                     ("Wheels", wheel_word(m["wheels"]["front"]) if wheel_word(m["wheels"]["front"]) == wheel_word(m["wheels"]["rear"])
                      else wheel_word(m["wheels"]["front"]) + " / " + wheel_word(m["wheels"]["rear"]))]
            ft = side.add_table(rows=len(facts), cols=2)
            b.fixed(ft, [32, b.W * 0.38 - 36])
            for row, (k, v) in zip(ft.rows, facts):
                for c, w, col in ((row.cells[0], k, SOFT), (row.cells[1], str(v), INK)):
                    p = c.paragraphs[0]
                    p.paragraph_format.space_after = Pt(1)
                    r = p.add_run(w)
                    r.font.size, r.font.color.rgb = Pt(8.5), col
            b.swatches(side, [c for c in m.get("colours", {}).values() if isinstance(c, str) and c.startswith("#")][:5])
            kit = m.get("kit", {})
            if kit:
                b.text("Kit options", 10, bold=True, after=2, cell=side)
                for grp in ("tank_seat", "tail", "front", "exhaust", "bars", "extras"):
                    opts = [v.get("name", "") for v in kit.get(grp, []) if isinstance(v, dict)]
                    if not opts:
                        continue
                    base = (kit.get("base") or {}).get(grp)
                    p = b.text("", 8.5, after=1, cell=side)
                    r = p.add_run(KIT_WORD[grp] + ": ")
                    r.font.size, r.font.color.rgb = Pt(8.5), SOFT
                    p.add_run(", ".join(([base + " (as drawn)"] if base else []) + opts)).font.size = Pt(8.5)
            small = b.table(1, 3, [b.W / 3] * 3)
            for c, n in zip(small.rows[0].cells, ("2_front_quarter", "3_rear_quarter", "4_low_front")):
                if os.path.exists(shots[n]):
                    b.image(c, shots[n], b.W / 3 - 8, 54 if not tight else 40, 1100)
            # his sketch of it, on a page of its own
            for sk in mine_sk:
                b.page()
                b.text(f"{title}  |  sketch", 8.5, colour=SOFT, caps=True, after=1)
                b.rule()
                t2 = b.table(1, 1, [b.W])
                b.image(t2.rows[0].cells[0], sk, b.W, 150, 2000)

    # ---- chopper bespoke parts
    cb = pool["chopper_bespoke"]
    b.page()
    b.text("The chopper catalogue", 9, colour=SOFT, caps=True, after=1)
    b.text("Bespoke machined parts", 22, bold=True, after=3)
    b.text("To order for the three choppers, each in " + ", ".join(mt["name"].lower() for mt in cb["metals"]) + ". Own designs throughout.", 10, colour=SOFT, after=5)
    b.rule()
    groups = [("pegs", "Footpegs"), ("covers", "Engine covers"), ("lamps", "Lamp shells"), ("engines", "Engine dress kits"), ("sissy", "Sissy bars"), ("wheels", "Billet wheels")]
    t = b.table(2, 3, [b.W / 3] * 3)
    for i, (k, word) in enumerate(groups):
        c = t.rows[i // 3].cells[i % 3]
        b.text(word, 12, bold=True, after=2, cell=c)
        for dsg in cb.get(k, []):
            p = b.text("", 9, after=2, cell=c)
            r = p.add_run(dsg["name"] + ("  " if dsg.get("note") else ""))
            r.font.bold, r.font.size = True, Pt(9)
            if dsg.get("note"):
                p.add_run(dsg["note"]).font.size = Pt(9)
        b.text("", 4, after=6, cell=c)
    sheets = [(os.path.join(ROOT, "out", "chopper_parts", n + ".png"), w) for n, w in
              (("pegs", "Footpegs"), ("covers", "Engine covers"), ("lamps", "Lamp shells"), ("sissy", "Sissy bars"), ("fit_brass_side_long", "A full brass set fitted"),
               ("fit_engine_pots", "Twin pot dress kit"), ("fit_engine_finned", "Finned top dress kit"), ("fit_filigree_rearq", "Filigree sissy bar"))]
    sheets += [(FILES + "wheels/sheet.png", "Wheel styles")] + [(FILES + f"wheels/{d['key']}.png", d["name"]) for d in cb.get("wheels", [])]
    for path, word in sheets:
        if not os.path.exists(path):
            continue
        b.page()
        b.text(f"The chopper catalogue  |  {word}", 8.5, colour=SOFT, caps=True, after=1)
        b.rule()
        t2 = b.table(1, 1, [b.W])
        b.image(t2.rows[0].cells[0], path, b.W, 152, 2000)

    # ---- the sketchbook: his sketches that name no one bike
    rest = [f for f in sketch_files if f not in used]
    if rest:
        b.page()
        b.text("Sketchbook", 9, colour=SOFT, caps=True, after=1)
        b.text("Sketches by beenster", 22, bold=True, after=6)
        b.rule()
        b.text(f"{len(sketch_files)} sketches drawn over the renders of the range; those that name a bike sit with it, the rest follow here.", 10, colour=SOFT)
        for sk in rest:
            b.page()
            b.text("Sketchbook  |  " + os.path.splitext(os.path.basename(sk))[0].replace("_", " "), 8.5, colour=SOFT, caps=True, after=1)
            b.rule()
            t2 = b.table(1, 1, [b.W])
            b.image(t2.rows[0].cells[0], sk, b.W, 152, 2000)

    os.makedirs(OUT, exist_ok=True)
    docx = OUT + "TorqueShed_Bike_Portfolio.docx"
    b.d.save(docx)
    subprocess.run(["soffice", "--headless", "--convert-to", "pdf", "--outdir", OUT, docx], capture_output=True, timeout=600)
    print("book:", docx, os.path.getsize(docx) // 1024, "KB;", len(models), "bikes,", len(sketch_files), "sketches,", len(used), "placed with a bike")
    print("pdf:", os.path.exists(docx[:-5] + ".pdf"))


if __name__ == "__main__":
    build()
