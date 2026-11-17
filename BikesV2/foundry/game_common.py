"""Common parts in the game: which COMMON PART (sub) each piece of a V2 bike is, so the game can take the bike's
own piece away when a common part is fitted in its place (beenster 17 Nov: "bring all of the common items into
the game now").

The rules are the exact names each common-part fit removes from the bike (bars3d, controls3d, lamps3d, clocks3d,
silencers3d, pegs3d, brakes3d, shocks3d), so a sub always covers what its part replaces, no more. Bike Models V2's
game export calls sub_of() beside game_parts.classify() and writes the sub on each surface, splitting pieces by
slot|node|sub. Pieces with no sub stay the bike's own and are never swapped.

    sub_of(stem, ob=None) -> sub or ""      (stem as game_parts.stem gives it; ob: the Blender object, for three rules)
    SUBS[sub] = dict(slot=game slot, group=common parts group(s), node=moving group the part goes in)
"""
import re

# sub: the game slot it lives in, the parts groups that fit it (parts_manifest.json), what it moves with
SUBS = {
    "bar": dict(slot="handlebars", groups=("bar", "high_bar"), node="steer"),
    "grip": dict(slot="grips", groups=("grip",), node="steer"),
    "bar_end": dict(slot="bar_ends", groups=("bar_end",), node="steer"),
    "levers": dict(slot="levers", groups=("levers",), node="steer"),
    "switches": dict(slot="switchgear", groups=("switches",), node="steer"),
    "mirrors": dict(slot="mirrors", groups=("mirrors",), node="S?"),
    "headlamp": dict(slot="headlight", groups=("headlamp",), node="S?"),
    "clocks": dict(slot="dashboard", groups=("clocks",), node="S?"),
    "ind_front": dict(slot="indicators", groups=("indicators_front",), node="S?"),
    "ind_rear": dict(slot="indicators_rear", groups=("indicators_rear",), node=""),
    "tail_light": dict(slot="tail_light", groups=("tail_light",), node=""),
    "plate_hanger": dict(slot="plate_hanger", groups=("plate_hanger",), node=""),
    "silencer": dict(slot="silencers", groups=("silencer",), node=""),
    "pegs": dict(slot="rearsets", groups=("pegs",), node=""),
    "heel_plates": dict(slot="heel_plates", groups=("heel_plates",), node=""),
    "foot_controls": dict(slot="foot_controls", groups=("foot_controls",), node=""),
    "disc_front": dict(slot="front_disc", groups=("disc_front",), node="front_wheel"),
    "disc_rear": dict(slot="rear_disc", groups=("disc_rear",), node="rear_wheel"),
    "caliper_front": dict(slot="front_caliper", groups=("caliper_front",), node="front_hub"),
    "caliper_rear": dict(slot="rear_caliper", groups=("caliper_rear",), node="swingarm"),
    "sprocket": dict(slot="final_drive", groups=("sprocket",), node="rear_wheel"),
    "shock": dict(slot="shock", groups=("shock",), node=""),
    "filler_cap": dict(slot="filler_cap", groups=("filler_cap",), node=""),
    "side_stand": dict(slot="side_stand", groups=("side_stand",), node=""),
}

DISC_PARTS = ("rotor", "carhub", "frame", "tab", "ear", "lug", "strut", "hubbolt", "button", "buttonin", "carout")
OLD_HEAD = ("lamphouse", "lampbezel", "lampdrl", "lampbowl", "lampbar", "projring", "projlens", "projbulb", "maincup", "mainled", "lampstay",
            "lampcover", "lamprim", "lampreflector", "lampshield", "lampbulb", "lampglass", "lampled", "lampproj", "lampear")
OLD_TAIL = ("taillight", "guardlamp", "looplamphouse", "looplamplens", "looplamptray", "looplampcell", "looplampled", "looplampplate", "taillamp")

# (regex on the object's STEM, as game_parts.stem gives it, sub); first FULL match wins. Library parts keep their own
# names, so a bike built with a common part already on it tags that part too.
H = "|".join(OLD_HEAD)
T = "|".join(OLD_TAIL)
DP = "|".join(DISC_PARTS)
RULES = [
    (r"sidestand_\w*", "side_stand"),
    (r"grip\w*", "grip"),
    (r"barend\w*", "bar_end"),
    (r"bar|riser\w*|clamp|barclamp\w*|bar_lib\w*|briser\w*|bclip\w*|bclamp\w*", "bar"),
    (r"perch\w*|levpivot|lever\w*|cut_lever|mastercyl|masterneck|brakeres\w*|clutchadj\w*|clever\w*|cperch\w*|cpivot\w*|cmaster\w*|cclutch\w*", "levers"),
    (r"switch\w*|sw(lights|ind|horn|kill|start)\w*|csw\w*", "switches"),
    (r"mirror\w*|mstalk|cmirror\w*|cmstalk\w*", "mirrors"),
    (r"lampunit\w*|lampbespoke\w*|guardlampbespoke\w*", ""),
    (rf"({H})\w*|lph\w*|lpear\w*", "headlamp"),
    (rf"({T})\w*|lpt\w*", "tail_light"),
    (r"ind_f\w*|lpind_f\w*|lpindboss|lpindlug", "ind_front"),
    (r"ind_r\w*|lpind_r\w*", "ind_rear"),
    (r"clock\w*|dash\w*|ck\w*", "clocks"),
    (r"canlink\w*", ""),
    (r"can\w*|sl", "silencer"),
    (r"peg|pg", "pegs"),
    (r"heelplate|heelspacer|heelbolt|hp", "heel_plates"),
    (r"gearpivot|gearlever|geartoe|brakepivot|brakepedal\w*|fc\w*", "foot_controls"),
    (rf"front_({DP})", "disc_front"),
    (rf"rear_({DP})", "disc_rear"),
    (r"rearcaliper_(lug|banjo)", ""),
    (r"rearcaliper_\w*|bkrcal\w*", "caliper_rear"),
    (r"caliper|bkcal\w*", "caliper_front"),
    (r"sprocket|cut_sprocket\w*|bkspr\w*", "sprocket"),
    (r"shocktop|shockbody|shockrod|shockbump|shockcollar|spring|sk_\w*", "shock"),
    (r"capneck|capring|caplid|capbolt|capkey|caphinge|caphingepin|skcap\w*", "filler_cap"),
    (r"platelamp\w*|platemount\w*|skhang\w*", "plate_hanger"),
]
RULES = [(re.compile(p), s) for p, s in RULES]


def stem(name):
    return re.sub(r"[-_.]?\d.*$", "", name)


def _verts(ob):
    import numpy as np
    me = ob.data
    if not hasattr(me, "vertices") or not len(me.vertices):
        return None
    mw = ob.matrix_world
    return np.array([tuple(mw @ v.co) for v in me.vertices]) * 1000.0


def sub_of(st, ob=None):
    """The common part (sub) a piece of the bike is, from its stem, or "" when no common part replaces it.
    Pass the Blender object as ob where you have it: three rules need it (the front caliper's lug and banjo stay
    on the fork; the plate arm and plate behind the axle go with the plate hanger, by their shape)."""
    if ob is not None:
        if re.match(r"caliper-?1_(lug|banjo)", ob.name):
            return ""
        if st.startswith("platearm"):
            v = _verts(ob)
            return "plate_hanger" if v is not None and v[:, 0].min() < 0 else ""
        if st.startswith("behind"):
            v = _verts(ob)
            mt = ob.active_material.name if ob.active_material else ""
            if v is not None and (mt == "plate" or (mt == "plastic" and v[:, 1].max() - v[:, 1].min() <= 60)):
                return "plate_hanger"
            return ""
    for rx, s in RULES:
        if rx.fullmatch(st):
            return s
    return ""


if __name__ == "__main__":
    # a quick self-check on names the fits remove and keep
    cases = {"bar": "bar", "bar0.5": "bar", "riser1": "bar", "grip1": "grip", "barend-1": "bar_end", "perch-1": "levers",
             "lever1": "levers", "swkill": "switches", "mirror1": "mirrors", "lamphouse": "headlamp", "lampunit0": "",
             "taillight": "tail_light", "ind_f1_lens": "ind_front", "ind_r-1_pod": "ind_rear", "clockface": "clocks",
             "can01": "silencer", "canband0-1": "silencer", "canlink": "", "header0": "", "peg01": "pegs", "pegpivot01": "",
             "heelplate1": "heel_plates", "gearlever": "foot_controls", "front_rotor78": "disc_front", "rear_rotor-82": "disc_rear",
             "caliper1_out": "caliper_front", "rearcaliper_body": "caliper_rear", "rearcaliper_banjo": "",
             "sprocket": "sprocket", "sprocketfront": "", "shockbody": "shock", "spring1": "shock", "capneck": "filler_cap",
             "platelamp": "plate_hanger", "sidestand_leg": "side_stand", "ssmount_lug": "", "tank": "", "frame3": ""}
    cases.update({"barrel1": "", "caninlet0-1": "silencer", "inletclamp": "", "springer": "", "lpind_f1_lens": "ind_front"})
    bad = {k: (sub_of(stem(k)), v) for k, v in cases.items() if sub_of(stem(k)) != v}
    print("OK" if not bad else f"MISMATCH {bad}")
