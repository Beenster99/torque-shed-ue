"""The object tables for bought_export.py: beenster's Indian Scout 101, Ducati Panigale V4 and Triumph Trident 800
(CGTrader, royalty free, Blender 4.2; all three by the Multistrada's maker, with its Spanish object names and materials).

Each object is (slot, moving group), None (left out: the tyres), or a rule: a function of one of its loose pieces
(p.lo / p.hi / p.c / p.size in the model's frame: metres, -Y forward, +X the rider's left, Z up; p.n its faces) and
ctx (ctx["F"], ctx["R"]: the front and rear axles) giving (slot, group[, role[, finish]]). Role "head" is a mirror's
head (the game aims each at the rider); a finish named here (NAMED) is used over the material's own.
Group "shock": a mono-shock drawn as one damper, cut across its length by the exporter.
"""

RED = [0.55, 0.012, 0.01]           # Ducati red (as the Multistrada)
SCOUT_RED = [0.3, 0.012, 0.012]     # the Scout's deep red
GRAPHITE = [0.075, 0.078, 0.084]    # the Trident's graphite

# The models' materials carry no real finish numbers: the game finish each stands for (as bought_multistrada.FINISH).
FINISH = {
    "m_paint_red": {"ue": "paint_silver", "paint": "body", "surface": "paint__body", "tag": "paint", "color": RED, "metallic": 0.0, "roughness": 0.26},
    "m_paint_white": {"ue": "paint_silver", "surface": "paint_white", "tag": "paint_white", "color": [0.78, 0.78, 0.77], "metallic": 0.0, "roughness": 0.3},
    "m_paint_green": {"ue": "paint_silver", "surface": "paint_green", "tag": "paint_green", "color": [0.01, 0.25, 0.04], "metallic": 0.0, "roughness": 0.3},
    "m_paint_grey": {"ue": "cat_brushed_steel", "surface": "lettering", "tag": "lettering", "color": [0.42, 0.43, 0.44], "metallic": 0.8, "roughness": 0.35},
    "m_paint_black": {"ue": "cat_black_metal", "surface": "gloss_black", "tag": "gloss_black", "color": [0.018, 0.019, 0.022], "metallic": 0.3, "roughness": 0.25},
    "m_paint_black_rough": {"ue": "cat_black_metal", "surface": "black_metal", "tag": "black_metal", "color": [0.03, 0.031, 0.034], "metallic": 0.85, "roughness": 0.45},
    "m_paint_yellow": {"ue": "cat_anodised", "surface": "spring_gold", "tag": "spring_gold", "color": [0.8, 0.5, 0.03], "metallic": 0.9, "roughness": 0.35},
    "m_paint_orange": {"ue": "cat_anodised", "surface": "bronze", "tag": "bronze", "color": [0.55, 0.25, 0.05], "metallic": 1.0, "roughness": 0.3},
    "m_aluminium_brushed": {"ue": "cat_cast_alloy", "surface": "alu", "tag": "alu", "color": [0.62, 0.63, 0.64], "metallic": 1.0, "roughness": 0.38},
    "m_steel": {"ue": "cat_brushed_steel", "surface": "steel", "tag": "steel", "color": [0.5, 0.51, 0.52], "metallic": 1.0, "roughness": 0.35},
    "m_iron": {"ue": "cat_cast_alloy", "surface": "iron", "tag": "iron", "color": [0.2, 0.2, 0.21], "metallic": 0.9, "roughness": 0.45},
    "m_iron_cast": {"ue": "cat_cast_alloy", "surface": "engine_dark", "tag": "engine_dark", "color": [0.16, 0.16, 0.17], "metallic": 0.9, "roughness": 0.5},
    "m_chrome": {"ue": "chrome", "surface": "chrome", "tag": "chrome"},
    "m_chrome_light": {"ue": "chrome", "surface": "lamp_chrome", "tag": "chrome"},
    "m_break_disk": {"ue": "cat_brushed_steel", "surface": "rotor", "tag": "rotor", "color": [0.55, 0.56, 0.57], "metallic": 1.0, "roughness": 0.3},
    "m_plastic_black": {"ue": "cat_plastic", "surface": "plastic", "tag": "plastic", "color": [0.02, 0.021, 0.023], "metallic": 0.0, "roughness": 0.52},
    "m_plastic_red": {"ue": "cat_plastic", "surface": "plastic_red", "tag": "plastic_red", "color": [0.5, 0.01, 0.01], "metallic": 0.0, "roughness": 0.45},
    "m_rubber": {"ue": "cat_rubber", "surface": "rubber", "tag": "rubber", "color": [0.015, 0.015, 0.016], "metallic": 0.0, "roughness": 0.8},
    "m_glass_clear": {"ue": "v2glass", "surface": "d3_clearglass", "tag": "d3_clearglass", "color": [1.0, 1.0, 1.0], "metallic": 0.0, "roughness": 0.02},
    "m_glass_black": {"ue": "v2screen", "surface": "d3_smoked", "tag": "d3_smoked", "color": [0.08, 0.08, 0.09], "metallic": 0.0, "roughness": 0.03},
    "m_glass_red": {"ue": "lens_red", "surface": "d3_redglass", "tag": "lens_red"},
    "m_glass_orange": {"ue": "lens_amber", "surface": "d3_amberglass", "tag": "reflector"},
    "m_light_white": {"ue": "lens_clear", "surface": "d3_lampdip", "tag": "lens_white"},
    "m_mirror": {"ue": "mirror_glass", "surface": "d3_mirrorglass", "tag": "d3_mirrorglass"},
    "m_leather_black": {"ue": "leather", "surface": "d3_leather", "tag": "d3_leather", "color": [0.025, 0.025, 0.027], "metallic": 0.0, "roughness": 0.6},
}
# Finishes by name, for a piece or object over its material's.
NAMED = {
    "lamp": {"ue": "lens_clear", "surface": "d3_lampdip", "tag": "lens_white"},
    "drl": {"ue": "lens_clear", "surface": "d3_drl", "tag": "lens_drl"},
    "amber": {"ue": "lens_amber", "surface": "d3_amberled", "tag": "lens_amber"},
    "tail": {"ue": "lens_red", "surface": "d3_tailred", "tag": "lens_red"},
    "red_reflector": {"ue": "lens_red", "surface": "redlens", "tag": "reflector"},
    "back_chrome": {"ue": "chrome", "surface": "lamp_chrome", "tag": "reflector"},
    "screen": {"ue": "cat_plastic", "surface": "d3_screen", "tag": "d3_screen", "color": [0.004, 0.005, 0.008], "metallic": 0.0, "roughness": 0.04},
    "perspex": {"ue": "v2screen", "surface": "d3_perspex", "tag": "d3_perspex", "color": [0.82, 0.86, 0.9], "metallic": 0.0, "roughness": 0.02},
    "grip": {"ue": "grip", "surface": "d3_grip", "tag": "d3_grip", "color": [0.016, 0.016, 0.018]},
    "rotor": {"ue": "cat_brushed_steel", "surface": "rotor", "tag": "rotor", "color": [0.55, 0.56, 0.57], "metallic": 1.0, "roughness": 0.3},
    "chain": {"ue": "cat_brushed_steel", "surface": "chain", "tag": "chain", "color": [0.3, 0.3, 0.31], "metallic": 1.0, "roughness": 0.4},
    "belt": {"ue": "cat_rubber", "surface": "belt", "tag": "belt", "color": [0.02, 0.02, 0.022], "metallic": 0.0, "roughness": 0.7},
    "rim_black": {"ue": "cat_powder", "paint": "rim", "surface": "rim__rim", "tag": "rim", "color": [0.0199, 0.022, 0.0265], "metallic": 0.85, "roughness": 0.42},
    "rim_dark": {"ue": "cat_powder", "paint": "rim", "surface": "rim__rim", "tag": "rim", "color": [0.06, 0.062, 0.066], "metallic": 0.85, "roughness": 0.4},
    "stanchion": {"ue": "chrome", "surface": "chrome", "tag": "chrome"},
    "gold": {"ue": "cat_anodised", "surface": "stanchion_gold", "tag": "stanchion_gold", "color": [0.75, 0.55, 0.2], "metallic": 1.0, "roughness": 0.15},
    "frame_red": {"ue": "cat_powder", "paint": "frame", "surface": "frame__frame", "tag": "frame", "color": RED, "metallic": 0.6, "roughness": 0.35},
    "frame_black": {"ue": "cat_powder", "paint": "frame", "surface": "frame__frame", "tag": "frame", "color": [0.02, 0.021, 0.024], "metallic": 0.6, "roughness": 0.4},
    "exhaust": {"ue": "cat_brushed_steel", "surface": "exhaust", "tag": "exhaust", "color": [0.5, 0.48, 0.45], "metallic": 1.0, "roughness": 0.3},
    "exhaust_black": {"ue": "cat_black_metal", "surface": "exhaust_black", "tag": "exhaust", "color": [0.03, 0.03, 0.032], "metallic": 0.8, "roughness": 0.5},
    "stripe_red": {"ue": "paint_silver", "surface": "stripe_red", "tag": "stripe", "color": [0.45, 0.01, 0.01], "metallic": 0.0, "roughness": 0.3},
    "spring_red": {"ue": "cat_anodised", "surface": "spring_red", "tag": "spring", "color": [0.5, 0.02, 0.015], "metallic": 0.6, "roughness": 0.35},
}


def spins(p, a, tol=0.018):
    """A round piece centred on the axle a (a wheel's disc, carrier, sprocket, hub)."""
    dy, dz = p.size.y, p.size.z
    return abs(p.c.y - a.y) < tol and abs(p.c.z - a.z) < tol and abs(dy - dz) < 0.03 and dy > 0.06


def front_end(spin_slot, other_slot):
    return lambda p, ctx: (spin_slot, "front_wheel") if spins(p, ctx["F"]) else (other_slot, "front_hub")


def rear_end(spin_slot, other_slot):
    return lambda p, ctx: (spin_slot, "rear_wheel") if spins(p, ctx["R"]) else (other_slot, "swingarm")


def side_amber(slot, group):
    return lambda p, ctx: (slot, group, "", "amber")


def by_y(split, front, rear):
    """front (y < split) or rear: each (slot, group[, role, finish])."""
    return lambda p, ctx: front if p.c.y < split else rear


# ---------------------------------------------------------------------------------------------------------------------
# Ducati Panigale V4 S (2025): sport. Single-sided swingarm; mono-shock under the seat on a linkage; USD fork; the
# fairing, its headlamps and screen on the frame; mirrors on the fairing; dash behind the screen.
def _pan_carroceria(p, ctx):
    return ("fairing", "") if p.c.y < -0.1 else ("tail", "")


def _pan_plastico(p, ctx):
    if p.hi.y < -0.66 and p.hi.z < 0.84:
        return "headlight", ""                       # the headlamps' housings
    if p.lo.y > -0.72 and p.hi.y < -0.59 and abs(p.c.x) < 0.01 and p.lo.z > 0.83 and p.n < 200:
        return "dashboard", ""                       # the dash
    if p.n > 1000 or p.hi.y < -0.6:
        return "fairing", ""
    return "frame", ""


def _pan_cristales(p, ctx):
    if p.n > 200:
        return "fairing", "", "", "perspex"          # the screen
    if p.c.y < 0:
        return "headlight", ""
    return "indicators", "", "", "amber"             # the rear indicators (the tail's clear lenses at the plate)


def _pan_reflectores(p, ctx):
    if p.c.y < -0.7:
        return ("headlight", "", "", "drl") if p.n <= 34 else ("headlight", "", "", "lamp")
    if p.c.y < -0.5:
        return "fork", "front_hub", "", "back_chrome"    # behind the fork feet's reflectors
    return "tail", "", "", "tail"


def _pan_rojo(p, ctx):
    return ("tail", "", "", "red_reflector") if p.lo.y > 0.85 else ("tail", "", "", "tail")


def _pan_direccion(p, ctx):
    return "fork", "steer"


def _pan_mirrors(p, ctx):
    return ("mirrors", "", "head") if p.n > 200 else ("mirrors", "")


def _pan_suspension(p, ctx):
    if abs(p.c.x) < 0.04 and p.hi.y < 0.33:
        return "shock", "shock"
    return "swingarm", "swingarm"


PANIGALE = {
    "name": "Ducati Panigale V4 S", "brand": "Ducati", "family_name": "Panigale", "style": "sport",
    "rims": ("rueda_delantera", "rueda_trasera"), "tyres": ("goma_delantera", "goma_trsera"),
    "fork_tubes": ("direccion", lambda p: abs(p.c.x) > 0.08 and p.size.z > 0.3),
    "stem": ("direccion", lambda p: abs(p.c.x) < 0.03),
    "peg": ("estriberas", lambda p: abs(p.c.x) > 0.19 and p.n == 50),
    "grip": "puños", "seat": "asiento", "hip_band": (0.3, 0.6), "cap": "tanque_tapa",
    "screen": ("cuerpo_plasticto", lambda p: p.lo.y > -0.72 and p.hi.y < -0.59 and abs(p.c.x) < 0.01 and p.lo.z > 0.83 and p.n < 200),
    "stats": {"fork_usd": True, "front_rim": 216, "rear_rim": 216, "front_width": 120, "rear_width": 200,
              "twin_shock": False, "tread_front": "sport", "tread_rear": "sport"},
    "finish": {"m_paint_black": NAMED["rim_black"]},
    "obj_finish": {"puños": "grip", "disco_freno_delantero": "rotor", "cadena": "chain", "escape": "exhaust",
                   "amortiguador_delantero": "gold", "manillar": "frame_black", "direccion": "frame_black", "vinillos": None},
    "objects": {
        "rueda_delantera": ("front_wheel", "front_wheel"), "rueda_trasera": ("rear_wheel", "rear_wheel"),
        "goma_delantera": None, "goma_trsera": None,
        "disco_freno_delantero": ("front_disc", "front_wheel"),
        "eje_delantero": front_end("front_disc", "fork"),
        "freno_delantero": ("front_caliper", "front_hub"), "guardabarro_delantero": ("front_mudguard", "front_hub"),
        "amortiguador_delantero": ("fork", "front_hub"), "cristal_naranja": ("fork", "front_hub"),
        "direccion": _pan_direccion, "direccion_eje": ("fork", "steer"), "presillas_direccion": ("fork", "steer"),
        "manillar": ("handlebars", "steer"), "puños": ("handlebars", "steer"), "controles": ("handlebars", "steer"),
        "botones": ("handlebars", "steer"), "boton_rojo": ("handlebars", "steer"), "soporte_manetas": ("handlebars", "steer"),
        "frasco_freno": ("front_master", "steer"),
        "retrovisores": _pan_mirrors, "espejos": ("mirrors", ""),
        "carroceria": _pan_carroceria, "carroceria_motor": ("fairing", ""), "cuerpo_plasticto": _pan_plastico,
        "cuerpo_plasticto.001": ("tail", ""), "bandera": ("fairing", ""), "bandera_roja": ("fairing", ""),
        "bandera_verde": ("fairing", ""), "vinillos": ("fairing", ""),
        "cristales": _pan_cristales, "reflectores": _pan_reflectores, "cristal_rojo": _pan_rojo,
        "portamatricula": ("tail", ""),
        "tanque": ("tank", ""), "tanque_tapa": ("tank", ""), "asiento": ("saddle", ""),
        "eje_trasero": rear_end("rear_disc", "rear_caliper"), "eje_trasero_soporte": rear_end("rear_wheel", "swingarm"),
        "cadena_engrane": ("final_drive", "rear_wheel"), "cadena": ("chain", "swingarm"),
        "suspension_trasera": _pan_suspension, "espiral": ("shock", "shock_spring"),
        "guardabarros_trasero": ("swingarm", "swingarm"),
        "motor": ("engine", ""), "escape": ("exhaust", ""), "tubaria_escape": ("exhaust", ""), "radiador": ("cooling", ""),
        "elementos_plasticos": ("frame", ""), "chasis": ("frame", ""), "cableado": ("frame", ""),
        "estriberas": ("rearsets", ""),
    },
}

# ---------------------------------------------------------------------------------------------------------------------
# Indian Scout 101 (2026): cruiser. Twin shocks; belt drive; headlight, nacelle, indicators, gauge and bars on the fork;
# Object003 holds the frame and the swingarm.
def _sco_frame(p, ctx):
    if p.lo.y > 0.6 and p.hi.z < 0.4 and p.lo.z > 0.25:
        return "swingarm", "swingarm"                # the arms' ends at the axle
    if p.lo.y > 0.28 and p.hi.y > 0.7 and p.lo.z > 0.25 and p.hi.z < 0.36:
        return "swingarm", "swingarm"                # the two arms
    if p.lo.y > 0.34 and p.hi.z < 0.33 and p.size.x > 0.2 and p.lo.z > 0.28:
        return "swingarm", "swingarm"                # the brace across
    return "frame", ""


def _sco_covers(p, ctx):
    if p.c.x > 0.1 and p.lo.y > 0.27:
        return "swingarm", "swingarm"                # the belt's guards
    return "side_panels", ""


def _sco_bolts(p, ctx):
    return ("handlebars", "steer") if p.lo.z > 0.95 else ("engine", "")


SCOUT = {
    "name": "Indian Scout 101", "brand": "Indian", "family_name": "Scout", "style": "cruiser",
    "rims": ("llanta_delantera", "llanta_trasera"), "tyres": ("neumatico_delantero", "neumatico_tracero"),
    "fork_tubes": ("ejes_direccion_laterales", None),
    "stem": ("Object003", lambda p: abs(p.c.x) < 0.01 and p.n == 24 and p.lo.z > 0.6),
    "peg": "estriberas", "grip": "puos", "seat": "asiento", "hip_band": (0.25, 0.6), "cap": "tapon_tanque",
    "screen": ("pantalla", None), "screen_node": "steer", "screen_fill": (0.7, 0.7),
    "stats": {"fork_usd": False, "front_rim": 241, "rear_rim": 203, "front_width": 130, "rear_width": 150,
              "twin_shock": True, "tread_front": "cruiser", "tread_rear": "cruiser"},
    "finish": {"m_paint_red": dict(FINISH["m_paint_red"], color=SCOUT_RED)},
    "obj_finish": {"puos": "grip", "llanta_delantera": "rim_black", "llanta_trasera": "rim_black", "correa": "belt",
                   "Object003": "frame_black", "interior_indicador_delantero": "amber", "interior_indicador_trasero": "amber",
                   "luz_delantera": "lamp", "interior_reflector_trasero": "tail", "interior_reflector_delantero": "back_chrome",
                   "cristal_reflector_trasero": "tail", "cupula": "perspex"},
    "objects": {
        "llanta_delantera": ("front_wheel", "front_wheel"), "llanta_delantera_cromo": ("front_wheel", "front_wheel"),
        "llanta_trasera": ("rear_wheel", "rear_wheel"), "llanta_trasera_cromo": ("rear_wheel", "rear_wheel"),
        "neumatico_delantero": None, "neumatico_tracero": None,
        "discos_freno_delantero": ("front_disc", "front_wheel"),
        "soporte_freno_delantero": front_end("front_disc", "fork"), "freno_delantero": ("front_caliper", "front_hub"),
        "suspension_delantera": ("fork", "front_hub"), "jumta_suspension": ("fork", "front_hub"),
        "eje_delantero": ("fork", "front_hub"), "guardabarro_delantero": ("front_mudguard", "front_hub"),
        "ejes_direccion_laterales": ("fork", "steer"), "direccion": ("fork", "steer"), "direccion_cromo": ("fork", "steer"),
        "control_direccion": ("fork", "steer"), "tapon_control_direccion": ("fork", "steer"),
        "seguro_tapon_control_direccion": ("fork", "steer"), "eje_direccion": ("fork", "steer"),
        "timon": ("handlebars", "steer"), "puos": ("handlebars", "steer"), "manetas": ("handlebars", "steer"),
        "controles": ("handlebars", "steer"), "tornillos": _sco_bolts,
        "carcasa_espejo": ("mirrors", "steer", "head"), "espejo": ("mirrors", "steer"), "soporte_espejos": ("mirrors", "steer"),
        "pantalla": ("dashboard", "steer"),
        "luz_delantera": ("headlight", "steer"), "ccubierta_luz_delantera": ("headlight", "steer"),
        "marco_luz_delantera": ("headlight", "steer"), "cristal_luz_delantera": ("headlight", "steer"),
        "cristal_interior_luz_delantera": ("headlight", "steer"), "cupula": ("fairing", "steer"),
        "cuepo_indicador_delantero": ("indicators", "steer"), "cristal_indicador_delantero": ("indicators", "steer"),
        "interior_indicador_delantero": ("indicators", "steer"),
        "cristal_reflector_delantero": ("fork", "steer"), "cuerpo_reflector_delantero": ("fork", "steer"),
        "interior_reflector_delantero": ("fork", "steer"),
        "cuerpo_indicador_trasero": ("indicators", ""), "cristal_indicador_trasero": ("indicators", ""),
        "interior_indicador_trasero": ("indicators", ""),
        "cristal_reflector_trasero": ("tail", ""), "cuerpo__reflector_trasero": ("tail", ""), "interior_reflector_trasero": ("tail", ""),
        "guardabarro_trasero": ("tail", ""),
        "tanque": ("tank", ""), "marco_tanque": ("tank", ""), "marco_tanque_plastico": ("tank", ""), "tapon_tanque": ("tank", ""),
        "graficas_blancas": ("tank", ""), "graficas_gris": ("tank", ""), "graficas_negras": ("tank", ""), "graficas_rojas": ("tank", ""),
        "asiento": ("saddle", ""),
        "Object003": _sco_frame, "cuerpo_pricipal_plastico": _sco_covers, "lineas_frenoLine029": ("frame", ""),
        "eje_trasero": ("swingarm", "swingarm"), "disco_freno_trasero": ("rear_disc", "rear_wheel"),
        "soporte_disco_freno_trasero": rear_end("rear_disc", "rear_caliper"),
        "traccion_cadena": ("final_drive", "rear_wheel"), "correa": ("chain", "swingarm"), "soporte_chapa": ("swingarm", "swingarm"),
        "pilar_suspension_tracera": ("shock", "shock_top"), "tapon_suspension_trasera": ("shock", "shock_top"),
        "deposito_suspension": ("shock", "shock_top"), "resorte_suspension_tracera": ("shock", "shock_spring"),
        "pilar_inferior_suspension_tracera": ("shock", "shock_bottom"), "suspension_tracera": ("shock", "shock_bottom"),
        "motor": ("engine", ""), "tapas_motor": ("engine", ""), "detalles_motor": ("engine", ""), "tuberias_motor": ("engine", ""),
        "cubierta_radiador": ("cooling", ""), "radiador": ("cooling", ""), "ventilador_radiador": ("cooling", ""),
        "escape": ("exhaust", ""), "escape.001": ("exhaust", ""), "escape_gloss": ("exhaust", ""), "cubierta_escape": ("exhaust", ""),
        "estriberas": ("rearsets", ""), "palanca_cambios": ("rearsets", ""),
    },
}

# ---------------------------------------------------------------------------------------------------------------------
# Triumph Trident 800 (2026): naked. USD fork; mono-shock on a linkage (drawn in the frame object); headlight,
# indicators, fly screen and TFT on the fork/bars.
def _tri_chasis(p, ctx):
    if abs(p.c.x) < 0.01 and p.lo.y > 0.07 and p.hi.y < 0.37 and p.lo.z > 0.43 and p.size.x < 0.07:
        return "shock", "shock"                      # the damper (its spring is "resorte")
    return "frame", ""


def _tri_reflectores(p, ctx):
    if p.c.y < 0:
        if abs(p.c.x) > 0.1:
            return "headlight", "steer", "", "back_chrome"   # behind the side reflectors
        return "headlight", "steer", "", ("drl" if p.n in (44, 235) else "lamp")
    if p.lo.y > 0.9:
        return "tail", "", "", "back_chrome"
    return "tail", "", "", "tail"


def _tri_cristales(p, ctx):
    if p.c.y < 0:
        if abs(p.c.x) > 0.15:
            return "indicators", "steer"
        if p.c.z > 0.95:
            return "dashboard", "steer"
        return "headlight", "steer"
    return "indicators", ""


def _tri_luces(p, ctx):
    if p.n > 100:
        return "tail", "", "", "tail"
    return ("indicators", "steer" if p.c.y < 0 else "", "", "amber")


def _tri_rojo(p, ctx):
    return ("tail", "", "", "red_reflector") if p.lo.y > 0.9 else ("tail", "", "", "tail")


def _tri_pantalla(p, ctx):
    return ("indicators", "steer") if abs(p.c.x) > 0.15 else ("dashboard", "steer")


def _tri_mirrors(p, ctx):
    return ("mirrors", "steer", "head") if p.n > 200 else ("mirrors", "steer")


def _tri_front_discs(p, ctx):
    if 0.05 < abs(p.c.x) < 0.075:
        return "front_disc", "front_wheel"
    return "fork", "front_hub"


def _tri_rear_axle(p, ctx):
    if 0.085 < abs(p.c.x) < 0.12:
        return ("rear_disc" if p.c.x < 0 else "final_drive"), "rear_wheel"
    return "swingarm", "swingarm"


def _tri_covers(p, ctx):
    return ("tail", "") if p.lo.y > 0.15 and p.n > 400 else ("side_panels", "")


TRIDENT = {
    "name": "Triumph Trident 800", "brand": "Triumph", "family_name": "Trident", "style": "naked",
    "rims": ("llanta_delantera", "llanta_trasera"), "tyres": ("goma_delantera", "goma_trasera"),
    "fork_tubes": ("direccion", None),
    "stem": ("presillas_direccion", lambda p: abs(p.c.x) < 0.01 and p.n == 192),
    "peg": ("estriberas", lambda p: p.n == 480 and p.lo.y < 0.3), "grip": "puños", "seat": "asiento", "hip_band": (0.3, 0.6),
    "cap": "deposito_tapa",
    "screen": ("cristales", lambda p: p.c.y < 0 and abs(p.c.x) < 0.15 and p.c.z > 0.95), "screen_node": "steer",
    "stats": {"fork_usd": True, "front_rim": 216, "rear_rim": 216, "front_width": 120, "rear_width": 180,
              "twin_shock": False, "tread_front": "sport", "tread_rear": "sport"},
    "finish": {"m_paint_grey": {"ue": "paint_silver", "paint": "body", "surface": "paint__body", "tag": "paint", "color": GRAPHITE,
                                "metallic": 0.5, "roughness": 0.3},
               "m_paint_red": NAMED["stripe_red"]},
    "obj_finish": {"puños": "grip", "llanta_delantera": "rim_dark", "llanta_trasera": "rim_dark", "cadena": "chain",
                   "resorte": "spring_red", "silenciador": "exhaust_black", "suspension_delantera": "gold",
                   "chasis": "frame_black"},
    "objects": {
        "llanta_delantera": ("front_wheel", "front_wheel"), "llanta_trasera": ("rear_wheel", "rear_wheel"),
        "goma_delantera": None, "goma_trasera": None,
        "eje_frontal": _tri_front_discs, "freno_frpntal": front_end("front_disc", "front_caliper"),
        "suspension_delantera": ("fork", "front_hub"), "guardabarro_delantero": ("front_mudguard", "front_hub"),
        "direccion": ("fork", "steer"), "presillas_direccion": ("fork", "steer"), "elementos_diraccion": ("fork", "steer"),
        "manillar": ("handlebars", "steer"), "puños": ("handlebars", "steer"), "manetas": ("handlebars", "steer"),
        "controles": ("handlebars", "steer"), "botones": ("handlebars", "steer"), "boton_rojo": ("handlebars", "steer"),
        "retrovisores": _tri_mirrors, "espejos": ("mirrors", "steer"),
        "pantalla": _tri_pantalla, "protector_pantalla": ("fairing", "steer"),
        "faro_delantero": ("headlight", "steer"), "anillo_faro_delantero": ("headlight", "steer"),
        "cristal_naranja": ("headlight", "steer"),
        "reflectores": _tri_reflectores, "cristales": _tri_cristales, "luces": _tri_luces, "cristal_rojo": _tri_rojo,
        "faro_trasero": ("tail", ""), "portamatricula": ("tail", ""), "lateral_platico_trasero": ("tail", ""),
        "deposito": ("tank", ""), "deposito_plastico_lateral": ("tank", ""), "deposito_tapa": ("tank", ""), "logo": ("tank", ""),
        "vinillo": ("tank", ""), "vinillo_negro": ("tank", ""), "vinillo_rojo": ("tank", ""),
        "asiento": ("saddle", ""), "lateral_rojo": ("side_panels", ""), "cubierta_plastica": _tri_covers,
        "cubierta_motor": ("side_panels", ""), "interior_plastico": ("frame", ""),
        "chasis": _tri_chasis, "resorte": ("shock", "shock_spring"), "conductos": ("frame", ""),
        "suspension_trasera": ("swingarm", "swingarm"),
        "eje_trasero": _tri_rear_axle, "freno_trasero": rear_end("final_drive", "rear_caliper"),
        "cadena": ("chain", "swingarm"),
        "motor": ("engine", ""), "tapa_motor": ("engine", ""), "radiador": ("cooling", ""),
        "conductos_escape": ("exhaust", ""), "silenciador": ("exhaust", ""), "elementos_escape": ("exhaust", ""),
        "estriberas": ("rearsets", ""),
    },
}

MODELS = {"ducati_panigale": PANIGALE, "indian_scout": SCOUT, "triumph_trident": TRIDENT}
BLENDS = {"ducati_panigale": "Ducati_Panigale_2025_Blender(4.2)\\ducati_panigale_2025.blend",
          "indian_scout": "Indian_Scout_101_2026_Blender(4.2)\\indian_scout_101_2026.blend",
          "triumph_trident": "Triumph_Trident_800_2026_Blender(4.2)\\triumph_trident_800_2026.blend"}
