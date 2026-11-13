"""Thumbnail side sketches for the six brand style sheets (Phase 0).

One representative bike per brand, described as data for foundry/sketch.py.
These are style thumbnails, not the dimensioned elevations of Phase 1.
"""
from sketch import bike_svg

def seat(top, th):
    """Seat polygon from its top line (front to back) and a thickness."""
    return top + [(x, z - th) for x, z in reversed(top)]


NAKED = dict(wheelbase=1450, rake=24.5, rf=300, rr=316, pivot=(505, 415), engine_at=(800, 420), tail_end=(150, 930))

THUMBS = {
    "corvano": dict(NAKED, title="Corvano naked", layout="vtwin90", frame="trellis", lamp="slit", curve=0.55,
                    body="#B3122A", frame_col="#B08D57", engine_at=(790, 430),
                    tank=[(1085, 925), (1080, 995), (1015, 1058), (900, 1072), (770, 1012), (672, 905), (655, 858), (770, 838), (900, 846), (1020, 866)],
                    seat=seat([(672, 905), (615, 858), (520, 842), (440, 858), (385, 905), (330, 935), (225, 955)], 55),
                    tail=[(470, 860), (385, 890), (225, 935), (140, 930), (155, 895), (300, 815), (450, 775)], tail_end=(165, 950),
                    bars=[(1120, 1040), (1075, 1075), (1050, 1085)],
                    exhaust=dict(pipe=[(980, 470), (1000, 330), (820, 250), (560, 265)], can=[(560, 275), (330, 330)], can_w=110)),
    "hartwell": dict(NAKED, title="Hartwell naked", layout="parallel_twin", frame="cradle", lamp="round", curve=0.9, wire=True,
                     body="#1F4D3A", frame_col="#22262A", rake=25.5, rf=310, rr=320, engine_at=(810, 425),
                     tank=[(1075, 905), (1068, 968), (1000, 1014), (880, 1022), (745, 988), (650, 905), (650, 866), (800, 856), (1000, 866)],
                     seat=seat([(650, 905), (600, 885), (300, 885), (130, 892)], 75),
                     tail=None, rear_guard=(8, 118, 34), tail_end=(125, 870),
                     bars=[(1110, 1040), (1060, 1090), (1030, 1095)],
                     exhaust=dict(pipe=[(900, 470), (960, 300), (800, 270), (520, 290)], can=[(520, 290), (60, 380)], can_w=95)),
    "kazeda": dict(NAKED, title="Kazeda naked", layout="inline4", frame="spar", lamp="split", curve=0.18,
                   body="#1C4FBF", frame_col="#AEB4BA", rake=24, engine_at=(830, 440),
                   tank=[(1095, 905), (1090, 990), (1020, 1058), (900, 1076), (775, 1005), (676, 892), (668, 852), (830, 836), (965, 806), (1135, 762), (1185, 795), (1120, 862)],
                   seat=seat([(676, 892), (610, 850), (510, 840), (430, 862), (380, 915), (330, 950), (230, 985)], 50),
                   tail=[(470, 850), (380, 900), (230, 965), (60, 1045), (95, 985), (280, 860), (450, 770)], tail_end=(62, 1030),
                   bars=[(1110, 1035), (1075, 1068), (1050, 1076)],
                   exhaust=dict(pipe=[(900, 480), (990, 330), (860, 240), (660, 240)], can=[(660, 245), (470, 255)], can_w=130)),
    "varga": dict(NAKED, title="Varga naked", layout="parallel_twin", frame="trellis_bright", lamp="vertical", curve=0.08,
                  body="#F4F4F2", frame_col="#F26A1B", rake=24, wheelbase=1475, engine_at=(800, 430),
                  tank=[(1095, 915), (1090, 1005), (1015, 1068), (885, 1062), (705, 935), (684, 872), (805, 862), (905, 792), (1045, 752), (1115, 802), (1055, 872)],
                  seat=seat([(705, 935), (650, 888), (540, 866), (420, 890), (300, 935), (200, 968)], 50),
                  tail=[(520, 850), (300, 915), (200, 950), (40, 1005), (40, 975), (260, 860), (500, 780)], tail_end=(52, 1000),
                  bars=[(1125, 1045), (1085, 1085), (1055, 1092)],
                  exhaust=dict(pipe=[(880, 470), (960, 320), (820, 245), (640, 250)], can=[(640, 255), (430, 290)], can_w=120)),
    "lodestar": dict(title="Lodestar cruiser", wheelbase=1650, rake=30, rf=335, rr=325, fork_len=770, pivot=(470, 370), engine_at=(830, 400),
                     layout="vtwin45", frame="double_cradle", lamp="round", curve=0.9, wire=True, tyre_h=110,
                     body="#7A1F1F", frame_col="#22262A",
                     tank=[(1195, 905), (1185, 962), (1105, 1004), (955, 994), (765, 905), (645, 792), (685, 772), (905, 824), (1105, 852)],
                     seat=seat([(660, 800), (580, 735), (450, 712), (340, 730), (270, 790), (200, 800)], 60),
                     tail=None, rear_guard=(5, 125, 36), front_guard=(25, 150, 30), tail_end=(200, 760),
                     bars=[(1230, 1090), (1120, 1130), (1040, 1090)],
                     exhaust=dict(pipe=[(900, 480), (1010, 300), (820, 230), (500, 235)], can=[(500, 235), (-150, 250)], can_w=85)),
    "rheinhart": dict(NAKED, title="Rheinhart naked", layout="boxer", frame="engine_carried", lamp="round", curve=0.32,
                      body="#E8E9EA", frame_col="#1A3C7A", rake=25.5, wheelbase=1490, engine_at=(830, 420), pivot=(470, 430),
                      tank=[(1085, 905), (1090, 1012), (1045, 1062), (805, 1052), (705, 962), (664, 884), (664, 852), (1085, 862)],
                      seat=seat([(690, 930), (640, 880), (520, 858), (400, 872), (320, 905), (150, 915)], 60),
                      tail=[(330, 860), (150, 880), (40, 885), (40, 860), (150, 845), (320, 820)], tail_end=(70, 880),
                      bars=[(1130, 1055), (1085, 1095), (1055, 1100)],
                      exhaust=dict(pipe=[(860, 340), (760, 250), (560, 250)], can=[(560, 260), (180, 330)], can_w=110)),
}


def thumb_svg(brand):
    d = dict(THUMBS[brand])
    return bike_svg(d, d.pop("title"))
