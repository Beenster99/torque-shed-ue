"""Which game slot and which moving group every piece of a V2 bike belongs to (switch-over, stage 1).

The game keeps a bike as a part per slot (Data/Fitment/layout.json "slots") and moves it in groups
(Source/TorqueShedProto/Fit/BikeBody.h FittedPart.Node): "" the bike itself, "steer" (turns with the
bars), "front_hub" (also slides with the fork), "front_wheel" (also spins), "swingarm", "rear_wheel".
build3d and detail3d name every object they make, so the name's stem decides both. First match wins.

    python3 foundry/game_parts.py <dump.tsv> <family>     # counts per slot and group, and any stem with no rule
"""
import math
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
    (r"(bar|barend|barendbolt|grip|clamp|barclamp|barclampbolt|riser|briser|bar_lib|bclip|bclamp|grip_lib|barend_lib|switch|switchsplit|sw(horn|ind|kill|lights|start)|perch|perchbolt|lever|leveradj|leveradjdot|levpivot|cut_lever|throttle|clutchadj|clutchadjlock|handguard|handguardbar|handguardend)$", "handlebars", "steer"),
    (r"(mastercyl|masterneck|brakeres|brakerescap)$", "front_master", "steer"),
    (r"(mirror|mirrorboss|mirrorglass|mstalk|mstub)", "mirrors", "S?"),
    (r"(clock|dash)", "dashboard", "S?"),
    (r"(lamp|lampunit|lens|lensrim|nacelle|maincup|mainled|proj(bulb|lens|ring))", "headlight", "S?"),
    (r"ind_f", "indicators", "S?"), (r"ind_r", "indicators", ""),
    (r"numberboard", "number_boards", "steer"),
    (r"(screen|screenscrew|bubble)", "fairing", "S?"),
    (r"guardlamp", "tail", ""),                            # the lamp on a classic's rear mudguard
    (r"bashplate", "guards", ""), (r"collector", "exhaust", ""), (r"axleplate", "frame", ""),
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
    (r"ssmount", "frame", ""),                           # the side stand's lug on the frame (sidestand3d)
    (r"sidestand", "side_stand", ""),                    # the game swings the slot's pieces on the pivot (points "stand")
    (r"(stand|centrestand)", "side_stand", ""),
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
            elif grp == "swingarm" and model.get("swingarm", {}).get("rigid"):
                grp = ""                # a rigid chopper: the rear end is the frame
            return slot, grp
    return None


FRONT_GROUPS = ("steer", "front_hub", "front_wheel")


def placed(name, slot, grp, x0, x1, model):
    """(slot, group) put right by where the piece really is (mm along the bike from the rear axle): a name
    the front end's rules caught that sits wholly behind the middle of the bike is no part of the front end
    (build 154: rear mudguards and throttle bodies swung round with the bars)."""
    if grp in FRONT_GROUPS and x1 < model["geometry"]["wheelbase"] * 0.5:
        st = stem(name)
        if st.startswith("guard"):
            return "tail", ""
        if st.startswith("throttle"):
            return "fuel", ""
        return slot, ""
    return slot, grp


# What the game draws each of the pipeline's own materials with (the game's finishes: Data/Parts/Blender/index.json
# and the Speed Twin's; "v2glass" and "v2screen" are see-through, Tools/ue_build_level.py bike_materials).
# (pattern on the material's name, finish, light tag or "", keep the model's colour)
LOOKS = [
    (r"d3_(clearglass|lampglassblue|ribbedglass)$", "v2glass", "", True),
    (r"(d3_perspex|screen)$", "v2screen", "", True),
    (r"d3_(redglass|taillens|tailred)$", "lens_red", "lens_red", False),
    (r"d3_(amberglass|amberbulb|amberled)$", "lens_amber", "lens_amber", False),
    (r"d3_(lampdip|drl|ledchip|bulb)$", "lens_clear", "LAMP", False),
    (r"d3_mirrorglass$", "mirror_glass", "", False),
    (r"d3_grip", "grip", "", "GRIP"),
    (r"d3_(cf_twill|wheel_carbon)$", "cat_carbon", "", False),
    (r"d3_headertint$", "cat_titanium", "", False),
    (r"d3_leather", "leather", "", True),
    (r"d3_.*hose$", "cat_rubber", "", True),
]
LOOKS = [(re.compile("^" + p), f, t, c) for p, f, t, c in LOOKS]


def look(tag, slot, colour, metallic, roughness):
    """(finish, light tag, colour or None) for a material the materials library gives no game finish."""
    for rx, finish, light, keep in LOOKS:
        if rx.match(tag):
            if light == "LAMP":
                light = "lens_white" if slot in ("headlight", "fog_lights") else ""
                if not light:
                    return "cat_plastic", "", colour
            if keep == "GRIP":
                return finish, "", [0.016, 0.016, 0.018]      # (its colour is a texture in the pictures: white as a number)
            return finish, light, colour if keep else None
    dark = max(colour) < 0.1
    if metallic < 0.5:
        return "cat_plastic", "", colour
    if roughness <= 0.12:
        return "chrome", "", colour
    if dark:
        return "cat_black_metal", "", colour
    if max(colour) - min(colour) > 0.15:
        return "cat_anodised", "", colour
    if roughness >= 0.4:
        return "cat_cast_alloy", "", colour
    return ("cat_polished_alloy" if roughness < 0.2 else "cat_brushed_steel"), "", colour


def points(model):
    """The points the game's layout needs, in the model's own frame (mm; x forward from the rear axle,
    y to the rider's left, z up from the ground)."""
    g, wh, fk, sw = model["geometry"], model["wheels"], model["fork"], model["swingarm"]
    rake = math.radians(g["rake"])
    F, A = (g["wheelbase"], wh["front"]["tyre_r"]), (0.0, wh["rear"]["tyre_r"])
    uf, nb = (-math.sin(rake), math.cos(rake)), (-math.cos(rake), -math.sin(rake))
    at = lambda d, o=0.0: [round(F[0] + uf[0] * d + nb[0] * o, 1), 0.0, round(F[1] + uf[1] * d + nb[1] * o, 1)]
    peg = next((h["at"] for h in model.get("hangers", []) if h.get("circle")), None)
    grip = next((s_["at"] for s_ in model.get("front_end", []) if s_.get("circle") and s_.get("part") != "head"), None)
    sh = model.get("shock") or {}
    out = {
        "front_axle": [F[0], 0.0, F[1]], "rear_axle": [0.0, 0.0, A[1]],
        "steer_top": at(fk["length"] - 22, fk["offset"]), "steer_bottom": at(fk["lower_yoke"] + 18, fk["offset"]),
        "fork_top": at(fk["length"]), "fork_dir": [round(uf[0], 5), 0.0, round(uf[1], 5)],
        "swing_pivot": [sw["pivot"][0], 0.0, sw["pivot"][1]], "rigid": bool(sw.get("rigid")),
        "hip": [g["hip_x"], 0.0, g["seat_height"]],
        "stats": {"wheelbase": g["wheelbase"], "rake_deg": g["rake"], "trail": g["trail"], "seat_height": g["seat_height"],
                  "ground_clearance": g.get("ground_clearance"), "fork_length": fk["length"], "fork_usd": bool(fk.get("usd")),
                  "front_radius": wh["front"]["tyre_r"], "rear_radius": wh["rear"]["tyre_r"],
                  "front_rim": wh["front"]["rim_r"], "rear_rim": wh["rear"]["rim_r"],
                  "front_width": wh["front"].get("width", 120), "rear_width": wh["rear"].get("width", 180),
                  "shaft": bool(sw.get("shaft")), "twin_shock": bool(sh.get("twin")),
                  # the game bends its own tread sections round the drawn wheels (Fit/BikeParts V2Tyres)
                  "tread_front": {"knobbly": "knobs"}.get(wh["front"].get("tread"), wh["front"].get("tread") or "touring"),
                  "tread_rear": {"knobbly": "knobs"}.get(wh["rear"].get("tread"), wh["rear"].get("tread") or "touring")},
    }
    if peg:
        out["peg"] = [peg[0], 0.0, peg[1]]
    if grip:
        out["grip"] = [grip[0] - 3, 321.0, grip[1] - 2]          # bars3d.model_hand
    if sh.get("top"):
        out["shock_top"], out["shock_bottom"] = [sh["top"][0], 0.0, sh["top"][1]], [sh["bottom"][0], 0.0, sh["bottom"][1]]
    return out


if __name__ == "__main__":
    import collections
    model = {"family": sys.argv[2], "fork": {"usd": True}, "swingarm": {"rigid": sys.argv[2] == "chopper"}}
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
