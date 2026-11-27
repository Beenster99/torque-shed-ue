"""The house kit's designs (Tools/blender_house_kit.py): our own UK detached houses, one function each.

Patterns per wall, one string per floor, tokens spread evenly along it (or at '@s' metres from its left end
seen from outside): W1..W4 casements of 1-4 lights, S a small window, D a front door (D2 with a side light),
P a side door, F french doors, G a single garage door, GG a double, B a canted bay (B3 a wide one; on floor 1
it runs up both floors), '.' a gap. 'g<side>' a gable's own windows (w small, W2 bigger).
"""
from blender_house_kit import Block, C, stack, dormer, canopy, solar, FOOT

ESTATE_CW = [
    {"brick": C["brick_red"], "soldier": C["soldier_red"], "tile": C["tile_grey"], "ridge": C["tile_grey"], "trim": C["white"],
     "frame": C["white"], "door": C["door_grey"], "garage": C["white"], "gutter": C["black"], "render": C["render_white"], "post": C["white"], "stone": C["stone"]},
    {"brick": C["brick_buff"], "soldier": C["soldier_buff"], "tile": C["tile_brown"], "ridge": C["tile_brown"], "trim": C["white"],
     "frame": C["white"], "door": C["door_navy"], "garage": C["white"], "gutter": C["black"], "render": C["render_cream"], "post": C["white"], "stone": C["stone"]},
    {"brick": C["brick_orange"], "soldier": C["brick_brown"], "tile": C["tile_brown"], "ridge": C["tile_brown"], "trim": C["white"],
     "frame": C["anthracite"], "door": C["anthracite"], "garage": C["anthracite"], "gutter": C["black"], "render": C["render_white"], "post": C["white"], "stone": C["stone"]},
]
VILLA_CW = [
    {"brick": C["brick_victorian"], "brick2": C["brick_buff"], "soldier": C["brick_victorian"], "tile": C["slate"], "ridge": C["tile_red"],
     "trim": C["white"], "frame": C["white"], "door": C["door_green"], "garage": C["door_white"], "gutter": C["black"], "stone": C["stone"],
     "render": C["render_cream"], "post": C["white"]},
    {"brick": C["brick_red"], "brick2": C["brick_dark"], "soldier": C["brick_red"], "tile": C["tile_red"], "ridge": C["tile_red"],
     "trim": C["cream_trim"], "frame": C["white"], "door": C["door_navy"], "garage": C["door_white"], "gutter": C["black"], "stone": C["stone"],
     "render": C["render_white"], "post": C["white"]},
]
RENDER_CW = [
    {"render": C["render_cream"], "brick": C["brick_red"], "tile": C["slate"], "ridge": C["slate"], "trim": C["white"], "frame": C["white"],
     "door": C["door_navy"], "garage": C["door_white"], "gutter": C["black"], "stone": C["render_white"], "post": C["white"], "boards": C["boards_white"]},
    {"render": C["render_white"], "brick": C["brick_brown"], "tile": C["tile_grey"], "ridge": C["tile_grey"], "trim": C["white"], "frame": C["anthracite"],
     "door": C["anthracite"], "garage": C["anthracite"], "gutter": C["anthracite"], "stone": C["render_grey"], "post": C["white"], "boards": C["boards_sage"]},
]
BOARD_CW = [
    {"boards": C["boards_white"], "render": C["render_white"], "brick": C["brick_red"], "tile": C["slate"], "ridge": C["slate"], "trim": C["white"],
     "frame": C["white"], "door": C["door_red"], "garage": C["door_white"], "gutter": C["black"], "stone": C["render_white"], "post": C["white"]},
    {"boards": C["boards_black"], "render": C["render_white"], "brick": C["brick_red"], "tile": C["tile_red"], "ridge": C["tile_red"], "trim": C["white"],
     "frame": C["white"], "door": C["door_green"], "garage": C["door_oak"], "gutter": C["black"], "stone": C["render_white"], "post": C["white"]},
]

ESTATE = dict(heads="soldier", sills="stone", plinth=True, door="composite", garage="glazed", win_h=1.2)


def e_ashby():
    """Two storeys, a front gable wing with a bay under it, an integral garage under a tiled canopy run on over the door."""
    m = Block(0, 9.0, 0, 8.4, 5.15, "gx", 37)
    w = Block(5.5, 9.0, -1.3, 6.0, 5.15, "gy", 40)
    m.pat = {"f": ["G@1.55 D@4.25", "W2@1.55 S@4.25"], "b": ["F@2.2 W2@5.2 W3@7.4", "W2@2.2 S@4.5 W2@7.0"],
             "l": ["P@5.8", "S@4.2"], "r": ["", ""], "gl": ".", "gr": ""}
    w.pat = {"f": ["B", "W3"], "gf": "w", "r": ["W2@1.4", "W1@1.4"], "l": ["", ""]}
    def extras(H, blocks, st):
        f = m.side("f")
        canopy(H, m, st, f, 0.1, 5.4, "lean", z=2.5, depth=1.05, posts=True)
        stack(H, m, "l", 2.8)
    return dict(name="estate_ashby", label="Ashby: front gable, bay, integral garage", set="estate", blocks=[m, w], style=ESTATE,
                colourways=ESTATE_CW, extras=[extras], pipes=[(0, 0, "a"), (0, 1, "b"), (1, 0, "b"), (1, 1, "a")], floors=2)



def e_bramley():
    """Double-fronted: a door in the middle under a gabled canopy, three-light windows each side, a stack on each gable."""
    m = Block(0, 10.2, 0, 8.2, 5.15, "gx", 37)
    m.pat = {"f": ["W3 D W3", "W2 W1 W2"], "b": ["W2 F W2", "W2 S W2"], "l": ["", "S"], "r": ["P", ""]}
    def extras(H, blocks, st):
        f = m.side("f")
        canopy(H, m, st, f, 4.35, 5.85, "gable", z=2.5, depth=0.95)
        stack(H, m, "l", 4.1)
        stack(H, m, "r", 4.1, pots=1)
    return dict(name="estate_bramley", label="Bramley: double-fronted, gabled canopy", set="estate", blocks=[m], style=ESTATE,
                colourways=ESTATE_CW, extras=[extras], pipes=[(0, 0, "a"), (0, 0, "b"), (0, 1, "b")], floors=2, service_side="l")


def e_calder():
    """A narrow three-bed: door and a window under a tiled canopy, a stack up one gable."""
    m = Block(0, 6.8, 0, 8.2, 5.15, "gx", 38)
    m.pat = {"f": ["D2 W2", "W2 W2"], "b": ["F W2", "W2 S"], "r": ["P@5.5", "S@3.0"], "l": ["", ""]}
    def extras(H, blocks, st):
        f = m.side("f")
        canopy(H, m, st, f, 0.2, 2.3, "lean", z=2.5, depth=0.9)
        stack(H, m, "l", 4.1)
    return dict(name="estate_calder", label="Calder: narrow three-bed", set="estate", blocks=[m], style=ESTATE, colourways=ESTATE_CW,
                extras=[extras], pipes=[(0, 0, "b"), (0, 1, "a")], floors=2)


def e_denby():
    """Two front gables either end, a lean-to canopy between them over the door."""
    m = Block(0, 10.6, 0, 8.6, 5.15, "gx", 37)
    a = Block(0, 3.4, -0.6, 4.0, 5.15, "gy", 42)
    b = Block(7.2, 10.6, -0.6, 4.0, 5.15, "gy", 42)
    m.pat = {"f": ["D@5.3", "W1@5.3"], "b": ["W2 F W3", "W2 S W2"], "l": ["", ""], "r": ["", ""]}
    a.pat = {"f": ["W3", "W2"], "gf": "w", "l": ["W1@2.0", ""]}
    b.pat = {"f": ["W3", "W2"], "r": ["P@2.6", ""]}
    def extras(H, blocks, st):
        canopy(H, m, st, m.side("f"), 3.5, 7.1, "lean", z=2.45, depth=0.6)
        stack(H, m, "r", 6.2)
    return dict(name="estate_denby", label="Denby: twin front gables", set="estate", blocks=[m, a, b], style=ESTATE, colourways=ESTATE_CW,
                extras=[extras], pipes=[(1, 0, "a"), (2, 1, "b"), (0, 1, "a"), (0, 1, "b")], floors=2, service_side="l")


def e_elmore():
    """A double garage with rooms over it under two gabled dormers, a bay and the door beside it."""
    m = Block(0, 11.8, 0, 8.8, 5.15, "gx", 38)
    m.pat = {"f": ["B@1.5 D@3.6 GG@8.6", "W2@1.5 W1@3.6"], "b": ["W2 F W2 W2", "W2 S W2 W2"], "l": ["", "S"], "r": ["P@6.5", ""]}
    def extras(H, blocks, st):
        dormer(H, m, st, 7.3, 1.7, tok="W1", through=True)
        dormer(H, m, st, 9.9, 1.7, tok="W1", through=True)
        stack(H, m, "l", 4.4)
    return dict(name="estate_elmore", label="Elmore: double garage, rooms over", set="estate", blocks=[m], style=ESTATE,
                colourways=ESTATE_CW, extras=[extras], pipes=[(0, 0, "a"), (0, 0, "b"), (0, 1, "a")], floors=2)


def e_fenwick():
    """Hipped, a front gable with a bay, a flat canopy, a single garage beside under its own hipped roof."""
    m = Block(0, 9.6, 0, 8.4, 5.15, "hip", 35)
    g = Block(9.6, 12.7, 1.0, 7.4, 2.55, "hip", 30)
    w = Block(0, 3.6, -0.8, 4.4, 5.15, "gy", 42)
    m.pat = {"f": ["D2@5.0 W2@7.9", "S@5.0 W2@7.9"], "b": ["F W3 W2", "W2 S W2"], "l": ["", "S"], "r": ["", ""]}
    w.pat = {"f": ["B", "W2"], "gf": "w", "l": ["W1@2.4", ""]}
    g.pat = {"f": ["G"], "b": ["P"], "r": [""]}
    def extras(H, blocks, st):
        canopy(H, m, st, m.side("f"), 4.2, 5.8, "flat", z=2.4, depth=1.0)
        stack(H, m, "ridge", 5.5, kind="ridge")
    return dict(name="estate_fenwick", label="Fenwick: hipped, front gable, side garage", set="estate", blocks=[m, w, g], style=ESTATE,
                colourways=ESTATE_CW, extras=[extras], pipes=[(0, 0, "b"), (1, 0, "a"), (0, 1, "a"), (2, 0, "b")], floors=2, service_side="l")


def e_grantley():
    """An L: the house, and a garage wing standing forward with a room in its roof."""
    m = Block(0, 8.8, 0, 8.0, 5.15, "gx", 37)
    g = Block(8.8, 12.4, -2.6, 6.0, 2.6, "gy", 42)
    m.pat = {"f": ["W2 D W2", "W2 S W2"], "b": ["F W2", "W2 W2"], "l": ["", ""]}
    g.pat = {"f": ["G"], "gf": "w", "r": ["P@5.0", ""], "l": ["W1@1.2", ""]}
    def extras(H, blocks, st):
        canopy(H, m, st, m.side("f"), 3.65, 5.15, "gable", z=2.5, depth=0.95)
        stack(H, m, "l", 4.0)
    return dict(name="estate_grantley", label="Grantley: garage wing forward", set="estate", blocks=[m, g], style=ESTATE,
                colourways=ESTATE_CW, extras=[extras], pipes=[(0, 0, "a"), (0, 1, "b"), (1, 1, "a")], floors=2, service_side="l")


def e_harlow():
    """Gable to the road: the house end-on, a single garage beside under a lean-to."""
    m = Block(0, 7.6, 0, 9.6, 5.15, "gy", 40)
    g = Block(7.6, 10.7, 0.6, 7.0, 2.4, "lean", 22)
    m.pat = {"f": ["W3 D", "W2 W2"], "gf": "W1", "b": ["F W2", "W2 S"], "l": ["", "S S"], "r": ["", ""]}
    g.pat = {"f": ["G"], "r": ["P@4.5"], "b": None}
    def extras(H, blocks, st):
        canopy(H, m, st, m.side("f"), 4.8, 6.9, "gable", z=2.5, depth=0.9)
        stack(H, m, "l", 6.5)
    return dict(name="estate_harlow", label="Harlow: gable-fronted, side garage", set="estate", blocks=[m, g], style=ESTATE,
                colourways=ESTATE_CW, extras=[extras], pipes=[(0, 0, "a"), (0, 1, "b"), (1, 0, "b")], floors=2, service_side="l")


def e_ingleby():
    """A four-bed: a gabled wing with a square-headed bay, a gabled porch on posts, two more windows across."""
    m = Block(0, 11.0, 0, 8.8, 5.15, "gx", 36)
    w = Block(7.4, 11.0, -1.0, 5.0, 5.15, "gy", 42, gable_skin="tile")
    m.pat = {"f": ["W2@1.6 D@4.4 W2@6.3", "W2@1.6 W1@4.4 W2@6.3"], "b": ["W2 F W3", "W2 S W2"], "l": ["P@6.0", ""], "r": ["", ""]}
    w.pat = {"f": ["B", "W3"], "r": ["W1@1.5", "W1@1.5"]}
    def extras(H, blocks, st):
        canopy(H, m, st, m.side("f"), 3.6, 5.2, "gable", z=2.55, depth=1.2, posts=True)
        stack(H, m, "l", 2.5, pots=2)
    return dict(name="estate_ingleby", label="Ingleby: four-bed, tile-hung gable, porch on posts", set="estate", blocks=[m, w], style=ESTATE,
                colourways=ESTATE_CW, extras=[extras], pipes=[(0, 0, "a"), (1, 1, "a"), (0, 1, "a")], floors=2)


def e_kirkham():
    """A dormer bungalow: rooms in the roof under two dormers, a bay, a gabled porch."""
    m = Block(0, 11.2, 0, 9.0, 2.7, "gx", 42, fh=2.7)
    m.pat = {"f": ["B W2@5.2 D@7.3 W2@9.6", ""], "b": ["W2 F W2 S", ""], "gl": "W2", "gr": "w", "l": [""], "r": ["P@6.5"]}
    def extras(H, blocks, st):
        dormer(H, m, st, 3.2, 1.8, setback=1.1, tok="W2", cheek="tile")
        dormer(H, m, st, 8.2, 1.8, setback=1.1, tok="W2", cheek="tile")
        canopy(H, m, st, m.side("f"), 6.55, 8.05, "gable", z=2.45, depth=1.0, posts=True)
        stack(H, m, "ridge", 7.8, kind="ridge")
    return dict(name="estate_kirkham", label="Kirkham: dormer bungalow", set="estate", blocks=[m], style=ESTATE, colourways=ESTATE_CW,
                extras=[extras], pipes=[(0, 0, "a"), (0, 0, "b"), (0, 1, "a")], floors=1)


def e_langley():
    """Two and a half storeys: rooms in a steep roof under dormers, an integral garage, windows in the gables."""
    m = Block(0, 8.4, 0, 9.4, 5.15, "gx", 46)
    m.pat = {"f": ["G@1.6 D@4.5 W2@6.9", "W2@1.6 S@4.5 W2@6.9"], "b": ["F W2 W2", "W2 S W2"], "gl": "W1", "gr": "W1", "l": ["", ""], "r": ["P@6.0", ""]}
    def extras(H, blocks, st):
        dormer(H, m, st, 2.4, 1.6, setback=1.3, tok="W1", cheek="tile")
        dormer(H, m, st, 6.0, 1.6, setback=1.3, tok="W1", cheek="tile")
        canopy(H, m, st, m.side("f"), 0.1, 5.3, "lean", z=2.45, depth=0.9)
        stack(H, m, "r", 2.8)
    return dict(name="estate_langley", label="Langley: two and a half storeys", set="estate", blocks=[m], style=ESTATE, colourways=ESTATE_CW,
                extras=[extras], pipes=[(0, 0, "a"), (0, 1, "a")], floors=3)


def e_marston():
    """Hipped and wide, a shallow central gable, a door with a side light, a double garage set back beside."""
    m = Block(0, 10.8, 0, 8.6, 5.15, "hip", 33)
    c = Block(3.8, 7.0, -0.45, 3.0, 5.15, "gy", 40, gable_skin="render")
    g = Block(10.8, 16.4, 1.2, 7.4, 2.5, "gx", 30)
    m.pat = {"f": ["W3@1.8 W3@9.0", "W2@1.8 W2@9.0"], "b": ["W2 F W3 W2", "W2 S W2 W2"], "l": ["P", "S"], "r": ["", ""]}
    c.pat = {"f": ["D2", "W3"]}
    g.pat = {"f": ["GG"], "b": ["P@4.2"], "r": [""]}
    def extras(H, blocks, st):
        canopy(H, c, st, c.side("f"), 0.6, 2.6, "flat", z=2.45, depth=0.9)
        stack(H, m, "ridge", 3.3, kind="ridge")
    return dict(name="estate_marston", label="Marston: hipped, double garage", set="estate", blocks=[m, c, g], style=ESTATE, colourways=ESTATE_CW,
                extras=[extras], pipes=[(0, 0, "a"), (0, 1, "a"), (2, 0, "b")], floors=2, service_side="l")


def e_newby():
    """A front gable with a two-storey window band, a porch tucked in its corner under a lean-to."""
    m = Block(0, 9.4, 0, 8.4, 5.15, "gx", 38)
    w = Block(0, 4.2, -1.4, 4.5, 5.15, "gy", 44)
    m.pat = {"f": ["D@5.3 W2@7.8", "W1@5.3 W2@7.8"], "b": ["F W2 W2", "S W2 W2"], "l": ["", ""], "r": ["P@6.0", "S@4.0"]}
    w.pat = {"f": ["W4", "W3"], "gf": "w", "r": ["", ""], "l": ["W1@2.2", "W1@2.2"]}
    def extras(H, blocks, st):
        canopy(H, m, st, m.side("f"), 4.3, 6.4, "lean", z=2.45, depth=0.95, posts=True)
        stack(H, m, "r", 4.2)
    return dict(name="estate_newby", label="Newby: front gable, corner porch", set="estate", blocks=[m, w], style=ESTATE, colourways=ESTATE_CW,
                extras=[extras], pipes=[(1, 0, "a"), (0, 0, "b"), (0, 1, "a")], floors=2, service_side="l")


# --- villas -----------------------------------------------------------------------------------------------

VILLA = dict(heads="stone", sills="stone", plinth=True, door="victorian", garage="ribbed", win_h=1.5, sash=True, bars=False, string="brick", bay_roof="tile")


def V(x0, x1, y0, y1, eaves, roof, pitch, **kw):
    return Block(x0, x1, y0, y1, eaves, roof, pitch, fh=3.0, sill0=0.85, over=0.4, verge=0.3, **kw)


def v_ashcombe():
    """Edwardian: hipped, two gabled wings with two-storey bays, a hooded door between, stacks on the ridge."""
    m = V(0, 13.2, 0, 10.2, 6.1, "hip", 38)
    a = V(0, 4.2, -0.8, 4.0, 6.1, "gy", 45, gable_skin="render")
    b = V(9.0, 13.2, -0.8, 4.0, 6.1, "gy", 45, gable_skin="render")
    m.pat = {"f": ["D@6.6", "W2@6.6"], "b": ["W2 F W2 W2", "W2 W2 S W2"], "l": ["", "W2"], "r": ["P", "W2"]}
    a.pat = {"f": ["", "B3"], "gf": "w"}
    b.pat = {"f": ["", "B3"], "gf": "w"}
    def extras(H, blocks, st):
        canopy(H, m, st, m.side("f"), 5.9, 7.3, "hood", z=2.75, depth=0.8)
        stack(H, m, "ridge", 4.7, kind="ridge", pots=3, top_extra=1.4, wide=1.3)
        stack(H, m, "ridge", 8.5, kind="ridge", pots=3, top_extra=1.4, wide=1.3)
        solar(H, m, 2, 4, "b")
    return dict(name="villa_ashcombe", label="Ashcombe: Edwardian twin bays", set="villa", blocks=[m, a, b], style=VILLA, colourways=VILLA_CW,
                extras=[extras], pipes=[(1, 0, "a"), (2, 1, "b"), (0, 1, "a"), (0, 1, "b")], floors=2, roofing="slate")


def v_beaumont():
    """Three storeys under a big gabled roof, three dormers, tall stacks up both gables, a hooded door."""
    m = V(0, 12.4, 0, 10.0, 8.95, "gx", 40)
    m.pat = {"f": ["W2 W2 D W2", "W2 W2 W2 W2", "W2 W2 W2 W2"], "b": ["W2 F W2 W2", "W2 W2 S W2", "W2 W2 W2 W2"],
             "l": ["", "W1", "W1"], "r": ["P", "", "W1"]}
    def extras(H, blocks, st):
        for s in (3.1, 6.2, 9.3):
            dormer(H, m, st, s, 1.7, setback=1.3, tok="W1", cheek="tile")
        canopy(H, m, st, m.side("f"), 7.1, 8.4, "hood", z=2.75, depth=0.8)
        stack(H, m, "l", 5.0, pots=4, top_extra=1.1, wide=1.4)
        stack(H, m, "r", 5.0, pots=4, top_extra=1.1, wide=1.4)
    return dict(name="villa_beaumont", label="Beaumont: three storeys, three dormers", set="villa", blocks=[m], style=VILLA, colourways=VILLA_CW,
                extras=[extras], pipes=[(0, 0, "a"), (0, 0, "b"), (0, 1, "a")], floors=3, roofing="slate")


def v_carlton():
    """Three storeys, a full-height central gable, hipped wings either side, an integral garage on one end."""
    m = V(0, 14.0, 0, 10.0, 8.9, "hip", 38)
    c = V(4.8, 9.2, -0.6, 4.5, 8.9, "gy", 45)
    m.pat = {"f": ["W2@1.6 W2@3.3 D@10.7 G@12.6", "W2@1.6 W2@3.3 W2@10.7 W2@12.6", "W2@1.6 W2@3.3 W2@10.7 W2@12.6"],
             "b": ["W2 F W2 W2 W2", "W2 W2 S W2 W2", "W2 W2 W2 W2 W2"], "l": ["", "W2", "W2"], "r": ["P@6.5", "", "W1"]}
    c.pat = {"f": ["W3", "W3", "W3"], "gf": "w"}
    def extras(H, blocks, st):
        canopy(H, m, st, m.side("f"), 10.05, 11.35, "hood", z=2.75, depth=0.8)
        stack(H, m, "ridge", 3.2, kind="ridge", pots=3, top_extra=1.3, wide=1.3)
        stack(H, m, "ridge", 10.8, kind="ridge", pots=3, top_extra=1.3, wide=1.3)
        solar(H, m, 2, 3, "b", s=4.0)
    return dict(name="villa_carlton", label="Carlton: three storeys, central gable", set="villa", blocks=[m, c], style=VILLA, colourways=VILLA_CW,
                extras=[extras], pipes=[(0, 0, "a"), (0, 0, "b"), (0, 1, "b")], floors=3, roofing="slate")


def v_dunmore():
    """Double-fronted Edwardian: two two-storey canted bays, keystone heads, a string course, stacks either side."""
    m = V(0, 11.6, 0, 10.4, 6.1, "hip", 36)
    m.pat = {"f": ["B3@2.4 D@5.8 B3@9.2", ". B3@2.4 W1@5.8 B3@9.2"], "b": ["W2 F W2", "W2 S W2"], "l": ["", "W2"], "r": ["P", "W1"]}
    m.pat["f"] = ["D@5.8", "B3@2.4 W1@5.8 B3@9.2"]
    st = dict(VILLA, heads="keystone", bay_roof="lead")
    def extras(H, blocks, s_):
        canopy(H, m, s_, m.side("f"), 5.05, 6.55, "flat", z=2.8, depth=0.8)
        stack(H, m, "ridge", 3.0, kind="ridge", pots=3, top_extra=1.3, wide=1.2)
        stack(H, m, "ridge", 8.6, kind="ridge", pots=3, top_extra=1.3, wide=1.2)
    return dict(name="villa_dunmore", label="Dunmore: double-fronted, two bays", set="villa", blocks=[m], style=st, colourways=VILLA_CW,
                extras=[extras], pipes=[(0, 0, "a"), (0, 0, "b"), (0, 1, "a")], floors=2, roofing="slate")


def v_evesley():
    """Arts and crafts: steep gables at each end with rendered tops, tall stacks, a deep porch hood."""
    m = V(0, 12.2, 0, 9.0, 6.0, "gx", 45)
    a = V(0, 4.4, -1.0, 4.0, 6.0, "gy", 50, gable_skin="render")
    b = V(7.8, 12.2, -1.0, 4.0, 6.0, "gy", 50, gable_skin="render")
    m.pat = {"f": ["D2@6.1", "W2@6.1"], "b": ["W3 F W3", "W2 S W2"], "l": ["", ""], "r": ["", ""]}
    a.pat = {"f": ["W3", "W3"], "gf": "w"}
    b.pat = {"f": ["W3", "W3"], "gf": "w"}
    st = dict(VILLA, sash=False, bars=True, heads="soldier", string=False)
    def extras(H, blocks, s_):
        canopy(H, m, s_, m.side("f"), 5.2, 7.0, "gable", z=2.75, depth=1.1, posts=True)
        stack(H, m, "l", 6.5, pots=2, top_extra=1.6)
        stack(H, m, "r", 6.5, pots=2, top_extra=1.6)
    return dict(name="villa_evesley", label="Evesley: arts and crafts gables", set="villa", blocks=[m, a, b], style=st, colourways=VILLA_CW,
                extras=[extras], pipes=[(1, 0, "a"), (2, 1, "b"), (0, 1, "a"), (0, 1, "b")], floors=2, roofing="tile")


def v_fairholme():
    """A three-storey corner tower under a pyramid roof, the house beside it with dormers and solar panels."""
    m = V(0, 11.0, 0, 9.4, 6.1, "gx", 42)
    t = V(-4.2, 0.0, -0.6, 3.6, 9.0, "hip", 50)
    m.pat = {"f": ["W2 D W2", "W2 W2 W2"], "b": ["F W2 W2", "W2 S W2"], "r": ["P", "W1"], "l": ["", ""]}
    t.pat = {"f": ["W2", "W2", "W2"], "l": ["W1", "W1", "W1"], "b": ["", "", "W1"]}
    def extras(H, blocks, st):
        for s in (3.0, 8.0):
            dormer(H, m, st, s, 1.6, setback=1.2, tok="W1", cheek="tile")
        canopy(H, m, st, m.side("f"), 4.8, 6.2, "hood", z=2.75, depth=0.8)
        stack(H, m, "r", 4.7, pots=3, top_extra=1.2, wide=1.3)
        solar(H, m, 2, 4, "b", s=5.5)
    return dict(name="villa_fairholme", label="Fairholme: corner tower", set="villa", blocks=[m, t], style=VILLA, colourways=VILLA_CW,
                extras=[extras], pipes=[(0, 0, "b"), (0, 1, "a"), (1, 0, "a")], floors=3, roofing="slate")


def v_glendale():
    """Inter-war: hipped, two-storey bays both sides under their own hips, a porch arch between."""
    m = V(0, 10.4, 0, 9.0, 5.9, "hip", 38)
    m.pat = {"f": ["D@5.2", "B3@2.1 S@5.2 B3@8.3"], "b": ["W2 F W2", "W2 S W2"], "l": ["", "W1"], "r": ["P", "W1"]}
    st = dict(VILLA, sash=False, bars=True, heads="none", string=False, door="composite", bay_roof="tile")
    def extras(H, blocks, s_):
        canopy(H, m, s_, m.side("f"), 4.45, 5.95, "gable", z=2.75, depth=0.9)
        stack(H, m, "ridge", 2.6, kind="ridge", pots=2, top_extra=1.0)
        solar(H, m, 2, 3, "b")
    return dict(name="villa_glendale", label="Glendale: inter-war twin bays", set="villa", blocks=[m], style=st, colourways=VILLA_CW,
                extras=[extras], pipes=[(0, 0, "a"), (0, 0, "b"), (0, 1, "a")], floors=2, roofing="tile")


def v_hartfield():
    """Wide and three storeys: two forward gables with bays, a recessed middle, a garage block beside."""
    m = V(0, 15.0, 0, 10.0, 8.9, "hip", 36)
    a = V(0, 4.4, -1.0, 4.5, 8.9, "gy", 45)
    b = V(10.6, 15.0, -1.0, 4.5, 8.9, "gy", 45)
    g = V(15.0, 18.6, 1.5, 8.0, 2.8, "hip", 30)
    m.pat = {"f": ["D@7.5", "W2@6.0 W2@9.0", "W1@6.0 W1@9.0"], "b": ["W2 F W2 W2 W2", "W2 W2 S W2 W2", "W2 W2 W2 W2 W2"], "l": ["", "W2", "W2"], "r": ["", "", "W1"]}
    a.pat = {"f": ["", "B3", "W3"], "gf": "w"}
    b.pat = {"f": ["", "B3", "W3"], "gf": "w"}
    g.pat = {"f": ["G"], "r": ["P"], "b": [""]}
    def extras(H, blocks, st):
        canopy(H, m, st, m.side("f"), 6.8, 8.2, "hood", z=2.75, depth=0.8)
        stack(H, m, "ridge", 3.5, kind="ridge", pots=4, top_extra=1.3, wide=1.5)
        stack(H, m, "ridge", 11.5, kind="ridge", pots=4, top_extra=1.3, wide=1.5)
        solar(H, m, 2, 5, "b")
    return dict(name="villa_hartfield", label="Hartfield: wide, three storeys", set="villa", blocks=[m, a, b, g], style=VILLA, colourways=VILLA_CW,
                extras=[extras], pipes=[(1, 0, "a"), (2, 1, "b"), (0, 1, "a"), (0, 1, "b")], floors=3, roofing="slate")


# --- rendered, painted and boarded ------------------------------------------------------------------------

RENDER = dict(heads="none", sills="stone", plinth=True, door="composite", garage="glazed", win_h=1.25, bars=True, canopy="gable")


def r_seaview():
    """Cream render: a front gable with an arched-feel tall window, a veranda on posts across the rest."""
    m = Block(0, 9.8, 0, 8.4, 5.15, "gx", 40, wall="render", roof_role="tile")
    w = Block(0, 3.9, -1.3, 4.5, 5.15, "gy", 44, wall="render")
    m.pat = {"f": ["D@5.4 W3@8.2", "W2@5.4 W2@8.2"], "b": ["F W2 W2", "W2 S W2"], "r": ["P", "S"], "l": ["", ""]}
    w.pat = {"f": ["W3", "W2"], "gf": "w", "l": ["W1@2.2", ""]}
    def extras(H, blocks, st):
        canopy(H, m, st, m.side("f"), 4.0, 9.7, "lean", z=2.6, depth=1.8, posts=True)
        stack(H, m, "r", 4.2)
    return dict(name="render_seaview", label="Seaview: rendered, veranda", set="render", blocks=[m, w], style=RENDER, colourways=RENDER_CW,
                extras=[extras], pipes=[(1, 0, "a"), (0, 0, "b"), (0, 1, "a")], floors=2, roofing="slate", regions=["coast", "south"])


def r_willowmead():
    """White render, hipped, a central gable over a gabled porch on posts, a rendered garage beside."""
    m = Block(0, 10.2, 0, 8.6, 5.15, "hip", 35, wall="render")
    c = Block(3.6, 6.6, -0.5, 3.0, 5.15, "gy", 42, wall="render")
    g = Block(10.2, 13.4, 1.0, 7.6, 2.5, "gx", 32, wall="render")
    m.pat = {"f": ["W3@1.6 W3@8.6", "W2@1.6 W2@8.6"], "b": ["W2 F W2", "W2 S W2"], "l": ["", "S"], "r": ["", ""]}
    c.pat = {"f": ["D2", "W2"], "gf": "w"}
    g.pat = {"f": ["G"], "b": ["P"], "r": [""]}
    def extras(H, blocks, st):
        canopy(H, c, st, c.side("f"), 0.45, 2.55, "gable", z=2.55, depth=1.2, posts=True)
        stack(H, m, "ridge", 6.8, kind="ridge")
    return dict(name="render_willowmead", label="Willowmead: rendered, hipped, central gable", set="render", blocks=[m, c, g], style=RENDER,
                colourways=RENDER_CW, extras=[extras], pipes=[(0, 0, "a"), (0, 1, "a"), (2, 0, "b")], floors=2, roofing="tile", service_side="l")


def r_rosebank():
    """A rendered cottage-style chalet: low walls, a steep roof with three dormers, a gabled porch."""
    m = Block(0, 10.6, 0, 8.2, 3.0, "gx", 45, wall="render")
    m.pat = {"f": ["W2 W2 D W2", ""], "b": ["W2 F W2", ""], "gl": "W2", "gr": "W1", "l": [""], "r": ["P@5.5"]}
    def extras(H, blocks, st):
        for s in (2.2, 5.3, 8.4):
            dormer(H, m, st, s, 1.5, setback=0.9, tok="W1", cheek="render")
        canopy(H, m, st, m.side("f"), 6.95, 8.35, "gable", z=2.35, depth=1.0, posts=True)
        stack(H, m, "l", 4.1)
    return dict(name="render_rosebank", label="Rosebank: rendered chalet, three dormers", set="render", blocks=[m], style=RENDER,
                colourways=RENDER_CW, extras=[extras], pipes=[(0, 0, "a"), (0, 0, "b")], floors=1, roofing="slate")


def r_larkrise():
    """Painted, a tall gable-fronted block and a lower wing at its side, windows in the gable."""
    m = Block(0, 6.8, 0, 9.6, 5.3, "gy", 45, wall="render")
    s_ = Block(6.8, 11.6, 1.2, 8.4, 4.0, "gx", 40, wall="render")
    m.pat = {"f": ["W3 D", "W2 W2"], "gf": "W2", "b": ["F", "W2"], "l": ["", "S S"], "r": ["", ""]}
    s_.pat = {"f": ["W2 P", "W1 W1"], "b": ["W2 W2", ""], "r": ["", ""]}
    def extras(H, blocks, st):
        canopy(H, m, st, m.side("f"), 4.4, 6.4, "gable", z=2.5, depth=1.0)
        stack(H, s_, "r", 3.6)
    return dict(name="render_larkrise", label="Larkrise: painted, tall gable front", set="render", blocks=[m, s_], style=RENDER,
                colourways=RENDER_CW, extras=[extras], pipes=[(0, 0, "a"), (1, 0, "b")], floors=2, roofing="slate", service_side="l")


BOARD = dict(heads="none", sills="none", plinth=True, door="composite", garage="ribbed", win_h=1.2, bars=False, canopy="gable")


def b_saltings():
    """Weatherboarded (the Kent, Essex and coastal kind): steep slate roof, a gabled porch, windows in the gables."""
    m = Block(0, 9.2, 0, 7.6, 4.9, "gx", 45, wall="boards")
    m.pat = {"f": ["W2 D W2", "W2 W1 W2"], "b": ["W2 F", "W2 S"], "gl": "w", "gr": "W1", "l": ["", ""], "r": ["P", ""]}
    def extras(H, blocks, st):
        canopy(H, m, st, m.side("f"), 3.85, 5.35, "gable", z=2.45, depth=1.0, posts=True)
        stack(H, m, "l", 3.8)
    return dict(name="boards_saltings", label="Saltings: weatherboarded", set="render", blocks=[m], style=BOARD, colourways=BOARD_CW,
                extras=[extras], pipes=[(0, 0, "a"), (0, 1, "b")], floors=2, roofing="slate", classes=["boards"], regions=["coast", "kent", "essex"])


def b_marshgate():
    """Black weatherboard on an L: a cross gable to the road, red tiles, a brick plinth."""
    m = Block(0, 9.0, 0, 7.4, 4.9, "gx", 45, wall="boards")
    w = Block(5.0, 9.0, -2.0, 4.0, 4.9, "gy", 48, wall="boards")
    m.pat = {"f": ["W2@1.6 D@3.6", "W2@1.6 W1@3.6"], "b": ["F W2 W2", "W2 S W2"], "l": ["", ""], "r": ["", ""]}
    w.pat = {"f": ["W3", "W2"], "gf": "w", "r": ["P@1.5", ""], "l": ["", ""]}
    def extras(H, blocks, st):
        canopy(H, m, st, m.side("f"), 2.9, 4.3, "gable", z=2.45, depth=0.9)
        stack(H, m, "l", 3.7)
    return dict(name="boards_marshgate", label="Marshgate: black weatherboard", set="render", blocks=[m, w], style=BOARD, colourways=BOARD_CW,
                extras=[extras], pipes=[(1, 0, "a"), (0, 1, "a")], floors=2, roofing="tile", classes=["boards"], regions=["coast", "kent", "essex"])


def designs():
    return [e_ashby(), e_bramley(), e_calder(), e_denby(), e_elmore(), e_fenwick(), e_grantley(), e_harlow(), e_ingleby(), e_kirkham(),
            e_langley(), e_marston(), e_newby(),
            v_ashcombe(), v_beaumont(), v_carlton(), v_dunmore(), v_evesley(), v_fairholme(), v_glendale(), v_hartfield(),
            r_seaview(), r_willowmead(), r_rosebank(), r_larkrise(), b_saltings(), b_marshgate()]
