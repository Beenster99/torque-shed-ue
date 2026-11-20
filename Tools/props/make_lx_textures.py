#!/usr/bin/env python3
"""The level crossings' printed faces (beenster, Nov 20 2026: "build a 3D blender model for these level crossing signs,
which flash when there is an imminent train"). Text is printed into these textures and never cut as 3D letters.
Writes Data/Props/Textures/lx_*.png, which Tools/props/make_level_crossing.py's plates show through the 'printed'
materials in Data/Props/materials.json (unit UVs over each plate's face, top of the image at the top):

    lx_keep_clear.png   KEEP CROSSING CLEAR: black on white, a thin black border
    lx_danger.png       DANGER / Crossing warning lights apply to all users including pedestrians: red border
    lx_contact.png      the blue "to contact railway" plate, 16 rows (the game picks one per crossing, its Courier
                        parameter): a made-up crossing's name and a made-up phone number from Ofcom's drama range
                        (01632 960000 to 960999: numbers set aside for fiction, never anyone's)
    lx_give_way.png     the Give Way triangle (point down) on a transparent-free square: white field, red border,
                        black GIVE WAY; the plate's own outline is a triangle, so only it shows
    lx_loco.png         the steam locomotive plate under a Give Way at an open crossing
    lx_board.png        the light unit's backboard: red and white chequered border, matt black middle
    lx_user_worked.png  the gated crossing's instruction board (made-up wording, our own pictograms)
    lx_road_paint.png   white road paint on black (its brightness is the paint, for the decal's opacity),
                        3 rows: a stop line with STOP over it, a stop line alone, a Give Way's double broken line

Fonts: DejaVu Sans (Bitstream Vera licence, free to use and pass on). Deterministic.

    python3 Tools/props/make_lx_textures.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "Data", "Props", "Textures")
BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
REG = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

WHITE = (240, 240, 236)
BLACK = (18, 18, 20)
RED = (200, 22, 28)
BLUE = (20, 62, 170)

# Made-up crossings (none is a real crossing's name as far as we know) and Ofcom's drama numbers.
CROSSINGS = [
    "HOLLIN MOSS", "COLDHARBOUR LANE", "FERNY BANK", "MILL LEAT", "ROOKERY FARM", "WHINFELL LANE",
    "BRACKEN RIGG", "NETHERBY DROVE", "SALTCOTE", "OXHEY MOOR", "LONG SLACK", "GREYSTONE ROAD",
    "BECKSIDE", "HAREWOOD END", "PEAT LANE", "THORNY CLOSE",
]


def font(path, size):
    return ImageFont.truetype(path, size)


def fit(d, text, path, size, width):
    """The largest size up to `size` at which text fits `width`."""
    while size > 8 and d.textlength(text, font=font(path, size)) > width:
        size -= 2
    return font(path, size)


def centred(d, y, text, path, size, width, w, ink):
    f = fit(d, text, path, size, width)
    d.text((w / 2, y), text, font=f, fill=ink, anchor="mm")


def save(img, name):
    """Saved at power-of-two sizes (so UE gives them mips): the plates' UVs run 0..1 whatever its size."""
    pow2 = lambda n: 1 << max(6, round(math.log2(n)))
    img = img.resize((pow2(img.size[0]), pow2(img.size[1])), Image.LANCZOS)
    path = os.path.join(OUT, name)
    img.save(path, optimize=True)
    print("wrote", path, img.size)


def keep_clear():
    W, H = 1024, 640
    img = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([10, 10, W - 11, H - 11], radius=36, outline=BLACK, width=14)
    for k, word in enumerate(("KEEP", "CROSSING", "CLEAR")):
        centred(d, 140 + k * 180, word, BOLD, 170, W - 120, W, BLACK)
    save(img, "lx_keep_clear.png")


def danger():
    W, H = 1024, 704
    img = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W - 1, H - 1], outline=(222, 52, 24), width=34)
    centred(d, 160, "DANGER", BOLD, 190, W - 160, W, BLACK)
    for k, line in enumerate(("Crossing warning lights", "apply to all users", "including", "pedestrians")):
        centred(d, 330 + k * 92, line, REG, 64, W - 160, W, BLACK)
    for x, y in ((W // 2, 48), (W // 2, H - 48)):
        d.ellipse([x - 9, y - 9, x + 9, y + 9], fill=(170, 170, 170))
    save(img, "lx_danger.png")


def contact():
    W, H = 512, 480
    img = Image.new("RGB", (W, H * len(CROSSINGS)), BLUE)   # (saved 512 x 8192: rows stay even)
    for row, name in enumerate(CROSSINGS):
        tile = Image.new("RGB", (W, H), BLUE)
        d = ImageDraw.Draw(tile)
        d.rounded_rectangle([6, 6, W - 7, H - 7], radius=22, outline=WHITE, width=8)
        d.line([(6, 168), (W - 7, 168)], fill=WHITE, width=8)
        # (the crossing's name on two lines when it's long)
        words = (name + " CROSSING").split()
        lines = [" ".join(words[:-1]), words[-1]] if len(words) > 1 else words
        for k, line in enumerate(lines):
            centred(d, 60 + k * 64, line, BOLD, 56, W - 60, W, WHITE)
        centred(d, 222, "TO CONTACT", BOLD, 66, W - 60, W, WHITE)
        centred(d, 296, "RAILWAY", BOLD, 66, W - 60, W, WHITE)
        centred(d, 364, "phone", REG, 52, W - 60, W, WHITE)
        number = "01632 960%03d" % (101 + row * 37 % 899)
        centred(d, 428, number, BOLD, 60, W - 50, W, WHITE)
        img.paste(tile, (0, row * H))
    save(img, "lx_contact.png")


def give_way():
    S = 1024
    img = Image.new("RGB", (S, S), WHITE)
    d = ImageDraw.Draw(img)
    # The plate's outline is the triangle with its point at the bottom middle; the image fills its bounding box.
    tri = [(0, 0), (S - 1, 0), (S / 2, S - 1)]
    d.polygon(tri, fill=RED)
    inset = 120
    # (the white field inside a red border about an eighth of the side)
    d.polygon([(inset * 1.73, inset * 0.75), (S - 1 - inset * 1.73, inset * 0.75), (S / 2, S - 1 - inset * 1.9)], fill=WHITE)
    centred(d, 235, "GIVE", BOLD, 130, 400, S, BLACK)
    centred(d, 375, "WAY", BOLD, 130, 290, S, BLACK)
    save(img, "lx_give_way.png")


def loco():
    W, H = 768, 640
    img = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([8, 8, W - 9, H - 9], radius=30, outline=BLACK, width=20)
    # A steam locomotive facing left: boiler, smokebox, chimney, dome, cab, wheels, smoke.
    ox, oy = 110, 210
    d.rectangle([ox + 40, oy + 120, ox + 380, oy + 250], fill=BLACK)              # boiler
    d.rectangle([ox + 380, oy + 40, ox + 540, oy + 290], fill=BLACK)              # cab
    d.rectangle([ox + 360, oy + 20, ox + 560, oy + 50], fill=BLACK)               # cab roof
    d.rectangle([ox + 410, oy + 80, ox + 500, oy + 160], fill=WHITE)              # cab window
    d.rectangle([ox + 70, oy + 50, ox + 120, oy + 125], fill=BLACK)               # chimney
    d.rectangle([ox + 58, oy + 40, ox + 132, oy + 62], fill=BLACK)
    d.ellipse([ox + 200, oy + 85, ox + 270, oy + 150], fill=BLACK)                # dome
    d.rectangle([ox + 0, oy + 250, ox + 560, oy + 280], fill=BLACK)               # running plate
    d.polygon([(ox - 30, oy + 330), (ox + 20, oy + 280), (ox + 40, oy + 280), (ox + 10, oy + 330)], fill=BLACK)  # buffer beam
    for cx in (ox + 120, ox + 260, ox + 400):
        d.ellipse([cx - 62, oy + 260, cx + 62, oy + 384], fill=BLACK)
        d.ellipse([cx - 20, oy + 302, cx + 20, oy + 342], fill=WHITE)
    d.line([(ox + 120, oy + 322), (ox + 400, oy + 322)], fill=WHITE, width=12)    # coupling rod
    for k, (cx, cy, r) in enumerate(((ox + 120, oy - 20, 46), (ox + 200, oy - 70, 58), (ox + 320, oy - 100, 66))):
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=BLACK)                   # smoke
    save(img, "lx_loco.png")


def board():
    W, H = 1024, 680
    img = Image.new("RGB", (W, H), (14, 14, 15))
    d = ImageDraw.Draw(img)
    # The chequered border: 120 px squares round the edge (the real board's about 150 mm), red at the corners.
    sq = 1024 / 8.0
    rows = H / 5.0
    for k in range(8):
        for y0, y1 in ((0, rows), (H - rows, H)):
            d.rectangle([k * sq, y0, (k + 1) * sq, y1], fill=RED if k % 2 == 0 else WHITE)
    for k in range(5):
        for x0, x1 in ((0, sq), (W - sq, W)):
            d.rectangle([x0, k * rows, x1, (k + 1) * rows], fill=RED if k % 2 == 0 else WHITE)
    # (the black middle, a touch weathered: a few lighter brush streaks)
    d.rectangle([sq * 0.55, rows * 0.55, W - sq * 0.55, H - rows * 0.55], fill=(14, 14, 15))
    for k in range(40):
        x = sq * 0.6 + (k * 97) % int(W - sq * 1.3)
        y = rows * 0.6 + (k * 53) % int(H - rows * 1.3)
        d.line([(x, y), (x + 60 + k % 5 * 18, y + 4)], fill=(30, 30, 32), width=3)
    save(img, "lx_board.png")


def user_worked():
    W, H = 640, 960
    img = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W - 1, H - 1], outline=BLACK, width=14)
    d.rectangle([14, 14, W - 15, 190], fill=RED)
    centred(d, 70, "STOP", BOLD, 96, W - 80, W, WHITE)
    centred(d, 150, "look and listen", REG, 44, W - 80, W, WHITE)
    lines = ["Open both gates", "Look both ways", "Cross quickly", "Close both gates", "", "Red light: STOP", "Green light: cross"]
    for k, line in enumerate(lines):
        if line:
            centred(d, 260 + k * 74, line, BOLD if line.startswith(("Red", "Green")) else REG, 46, W - 80, W, BLACK)
    for k, ink in enumerate((RED, (20, 160, 60))):
        cx, cy = 190 + k * 260, 830
        d.ellipse([cx - 52, cy - 52, cx + 52, cy + 52], fill=BLACK)
        d.ellipse([cx - 38, cy - 38, cx + 38, cy + 38], fill=ink)
    save(img, "lx_user_worked.png")


def road_paint():
    # 3 rows of 1024 x 512: the decal is laid 4 m along the road by 2 m across its lane (the traffic comes up the
    # image from the bottom, its stop line at the image's top).
    W, H = 1024, 512
    img = Image.new("L", (W, H * 3), 0)
    d = ImageDraw.Draw(img)
    # Row 0: the stop line (300 mm deep: 38 px of 4 m) at the top and STOP before it, the letters long along the road
    # as UK road paint is (stretched to read from a low eye).
    d.rectangle([0, 0, W, 38], fill=255)
    stop = Image.new("L", (900, 260), 0)
    ImageDraw.Draw(stop).text((450, 130), "STOP", font=font(BOLD, 250), fill=255, anchor="mm")
    stop = stop.resize((900, 380))
    img.paste(stop, ((W - 900) // 2, 90))
    # Row 1: the stop line alone.
    d.rectangle([0, H, W, H + 38], fill=255)
    # Row 2: a Give Way's two broken lines (600 mm dashes, 300 mm gaps, 200 mm wide, 300 mm apart).
    px = W / 4.0   # (pixels a metre across the lane, 4 m... the decal's width is the lane's)
    for y0 in (2 * H + 4, 2 * H + 4 + 64):
        x = 0.0
        while x < W:
            d.rectangle([x, y0, min(W, x + 0.6 * px), y0 + 26], fill=255)
            x += 0.9 * px
    save(img, "lx_road_paint.png")


def main():
    os.makedirs(OUT, exist_ok=True)
    keep_clear()
    danger()
    contact()
    give_way()
    loco()
    board()
    user_worked()
    road_paint()


if __name__ == "__main__":
    main()
