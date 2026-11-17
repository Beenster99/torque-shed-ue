"""Which game slot and which moving group every piece of a V2 bike belongs to (switch-over, stage 1).

The game keeps a bike as a part per slot (Data/Fitment/layout.json "slots") and moves it in groups
(Source/TorqueShedProto/Fit/BikeBody.h FittedPart.Node): "" the bike itself, "steer" (turns with the
bars), "front_hub" (also slides with the fork), "front_wheel" (also spins), "swingarm", "rear_wheel".
build3d and detail3d name every object they make, so the name's stem decides both. First match wins.

    python3 foundry/game_parts.py <dump.tsv> <family>     # counts per slot and group, and any stem with no rule
"""
import re
import sys

FAIRED = ("sport", "adventure")        # their lamp, clocks, screen and indicators sit on the fairing, not the fork

# (stem pattern, slot, group); group "S?" = steer on a naked front end, the bike itself on a faired one
RULES = [
    # wheels, tyres, discs
    (r"front_tyre", "front_tyre", "front_wheel"), (r"rear_tyre", "rear_tyre", "rear_wheel"),
    (r"front_(rotor|button|buttonin|carhub|star|ear|tab|strut|frame|lug)", "front_disc", "front_wheel"),
    (r"rear_(rotor|carhub|carout|star|ear|tab|strut|frame|lug)", "rear_disc", "rear_wheel"),
    (r"front_", "front_wheel", "front_wheel"), (r"rear_", "rear_wheel", "rear_wheel"),
    (r"(sprocket|sprocketnut|cut_sprocket)$", "final_drive", "rear_wheel"),
    (r"(sprocketfront|sprocketcover|finaldrive|finaldrivecap)", "final_drive", ""),
    (r"chain(inner|outer|pins|rollers)", "chain", "swingarm"),
    # front end
    (r"(caliper|pinch|pinchboss|pinchslot)", "front_caliper", "front_hub"),
    (r"(guard|guardbolt|guardboss|guardshroud)$", "front_mudguard", "front_hub"),
    (r"(forkbottom|axle_f|axlenut_f|axleboss|axlehead|axlehex|brakeline)$", "fork", "front_hub"),
    (r"(forkouter|stanchion)$", "fork", "FORK"),          # the sliding half follows the hub; decided by fork.usd
    (r"(forkcap|forkadj|forkadjslot|forkwiper|yoke_|yokepinch_|yokeslot_|stemcap|stemnut|brakeline_up|brakesplit|gaiter)", "fork", "steer"),
    (r"(bar|barend|barendbolt|grip|clamp|barclamp|barclampbolt|riser|briser|bar_lib|bclip|bclamp|grip_lib|barend_lib|switch|switchsplit|sw(horn|ind|kill|lights|start)|perch|perchbolt|lever|leveradj|leveradjdot|levpivot|cut_lever|throttle|clutchadj|clutchadjlock|handguard)$", "handlebars", "steer"),
    (r"(mastercyl|masterneck|brakeres|brakerescap)$", "front_master", "steer"),
    (r"(mirror|mirrorboss|mirrorglass|mstalk|mstub)", "mirrors", "S?"),
    (r"(clock|dash)", "dashboard", "S?"),
    (r"(lamp|lampunit|lens|lensrim|nacelle)", "headlight", "S?"),
    (r"ind_f", "indicators", "S?"), (r"ind_r", "indicators", ""),
    (r"numberboard", "number_boards", "steer"),
    (r"(screen|screenscrew|bubble)", "fairing", "S?"),
    # rear end
    (r"(swingarm|axle_r|axlenut_r|hugger|huggerbolt|huggerboss|huggerleg|shockbridge)$", "swingarm", "swingarm"),
    (r"(rearcaliper_|rearcarrier|brakehose_r|brakehoseclip)", "rear_caliper", "swingarm"),
    (r"(rearmaster|rearmasterrod|rearres|rearrescap|rearreshose)", "rear_master", ""),
    (r"(shock|spring)", "shock", ""),
    # bodywork
    (r"(tank|cap(bolt|hinge|hingepin|key|lid|neck|ring)|filler|kneepad)", "tank", ""),
    (r"(seat|seatpan|stitch|stitchx|pillion$)", "saddle", ""),
    (r"(tail$|taillamp|taillight|looplamp|platelamp|platemount_|platearm|plate$|behind|tray|sissy|rack)", "tail", ""),
    (r"(fairing|fairingshelf|belly|chin|cowl|duct|ear$|nose|wing|winglet|adv|beak|shroud)", "fairing", ""),
    (r"(panel|sidepanel)", "side_panels", ""),
    # exhaust
    (r"(can|header|headerbranch|headermerge|pipe)", "exhaust", ""),
    # controls on the bike
    (r"(peg|pegpivot|heelplate|heelbolt|heelspacer|cut_heelplate|hanger|brakepedal|brakepedaltip|brakepivot|gear(arm|lever|pivot|rod|toe)|pillionhanger)", "rearsets", ""),
    # cooling, intake, service items
    (r"(rad|fan|thermostat|waterpump|pumpbolt|pumpstub|inlethose)", "cooling", ""),
    (r"(airbox|svc_air_filter|inletclamp|inletstub)", "air_filter", ""),
    (r"svc_oil_filter", "oil_filter", ""), (r"(svc_spark_plug|plugcap|pluglead|plugleadclip|plugwell)", "spark_plugs", ""),
    (r"(svc_fuel|injector|fuelrail|throttlepulley|throttleshaft)", "fuel", ""),
    (r"(stand|sidestand|centrestand)", "side_stand", ""),
    # frame
    (r"(frame|framecross|frameloop|weld|head$|pivot|pivotcap|pivotnut|spar|sparbolt|sparweld|mount|lug|cabletie|clutchcable|bolt$)", "frame", ""),
    # everything else that was matched nowhere above is the engine (cases, covers, barrels, heads, their bolts)
    (r"(alt|barrel|basenut|block|boxer|cam|casebolt|clutch|cover|crankcase|cylhead|drainplug|headgasket|oilcap|port|pushrod|sidecase|sightglass|starter|sump)", "engine", ""),
]
RULES = [(re.compile("^" + p), s, g) for p, s, g in RULES]


def stem(name):
    return re.sub(r"[-_.]?\d.*$", "", name)


def classify(name, model):
    """(slot, group) for an object name on this model, or None when no rule knows it."""
    st = stem(name)
    for rx, slot, grp in RULES:
        if rx.match(st):
            if grp == "S?":
                grp = "" if model["family"].lower() in FAIRED else "steer"
            elif grp == "FORK":
                usd = model["fork"].get("usd")
                grp = "front_hub" if (st == "stanchion") == bool(usd) else "steer"
            return slot, grp
    return None


if __name__ == "__main__":
    import collections
    model = {"family": sys.argv[2], "fork": {"usd": True}}
    count, unknown = collections.Counter(), collections.Counter()
    for line in open(sys.argv[1]):
        n = line.split("\t")[0]
        r = classify(n, model)
        if r:
            count[r] += 1
        else:
            unknown[stem(n)] += 1
    print(len(count), "pieces;", " ".join(f"{s}/{g or 'bike'}:{n}" for (s, g), n in sorted(count.items())))
    print("NO RULE:", dict(unknown))
