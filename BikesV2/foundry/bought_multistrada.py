"""beenster's bought Ducati Multistrada V4 (CGTrader, royalty free, Blender 4.2) as a V2 bike the game rides as is
(beenster, 25 Nov: "a one off bike you can buy from the shop, no construction ... we can choose to ride this bike as is").

The bought model is never in git (its licence lets it be used inside the game, not handed out as a file). This turns the
.blend into the game's own V2 format, the same as build3d.py --game writes for our own bikes:

    <out>/ducati_multistrada.glb    the drawing (one mesh, a primitive per surface s000..): NOT committed, gitignored
    Data/BikesV2/ducati_multistrada.json   its slots, moving groups, finishes and key points: committed (no geometry)

    pip install bpy   (Blender 4.2+ as a module; the cloud has it)
    python3 -I BikesV2/foundry/bought_multistrada.py "<...>/ducati_multistrada_v4_2024.blend" --out <dir>

The model's frame: metres, -Y forward, +X the rider's left, Z up, ground at z 0. The game's (build3d's): x forward from
the rear axle, y to the rider's left, z up, millimetres in the json (metres in the glb, exported y-up).
Every object is put in a game slot and a moving group by its (Spanish) name; the few that hold pieces of two groups
(the swingarm with the footrest plates, the mirror stalks with their heads, a side's indicator) are split into loose
pieces first and each piece placed by where it is. The model's tyres are left out: the game bends its own tread round
the drawn rims, to the sizes in points_mm.stats (as every V2 bike).
"""
import json
import math
import os
import sys


import bpy  # noqa (bmesh comes with it)
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.dirname(os.path.dirname(HERE))
NAME = "ducati_multistrada"

# object -> (slot, moving group). Groups: "" the bike, steer (turns with the bars), front_hub (also slides with the
# fork), front_wheel / rear_wheel (spin), swingarm, shock_top / shock_spring / shock_bottom (TorqueBikePawn [shocks]).
OBJECTS = {
    # wheels (tyres: the game's own)
    "llanta_delantera": ("front_wheel", "front_wheel"), "base_disco_freno_delantero": ("front_disc", "front_wheel"),
    "discos_freno_delantero": ("front_disc", "front_wheel"),
    "llanta_tracera": ("rear_wheel", "rear_wheel"), "centro_tracero": ("rear_wheel", "rear_wheel"),
    "disco_freno_tracero": ("rear_disc", "rear_wheel"), "trasmision_cadena": ("final_drive", "rear_wheel"),
    "neumatico_delantero": None, "neumatico_tracero": None,
    # front end: the USD fork's black outer tubes and yokes turn with the bars; its gold inner tubes, axle feet,
    # calipers, mudguard and the amber reflectors on the feet ride with the axle
    "ejes_direccion_laterales": ("fork", "steer"), "direccion": ("fork", "steer"), "control_direccion": ("fork", "steer"),
    "suspencion_delantera": ("fork", "front_hub"), "sostenedor_frenos_delantero": ("fork", "front_hub"),
    "intermitente_delantero_base": ("fork", "front_hub"), "intermitente_delantero_cristal": ("fork", "front_hub"),
    "intermitente_delantero_interior": ("fork", "front_hub"),
    "freno_delantero": ("front_caliper", "front_hub"),
    "guardabarro_delantero": ("front_mudguard", "front_hub"), "guardabarro_delantero1": ("front_mudguard", "front_hub"),
    # bars and what's on them
    "handle": ("handlebars", "steer"), "manetas": ("handlebars", "steer"), "matetas": ("handlebars", "steer"),
    "contrapesos": ("handlebars", "steer"), "controles": ("handlebars", "steer"), "botones": ("handlebars", "steer"),
    "boton": ("handlebars", "steer"), "cobertura_manetas": ("handlebars", "steer"),
    "frascos": ("front_master", "steer"), "tapa_frascos": ("front_master", "steer"), "soporte_frasco": ("front_master", "steer"),
    "retrovisores": ("mirrors", "steer"), "retrovisores1": ("mirrors", "steer"),
    # rear end (chasis04 is split: see piece_place)
    "eje_tracero": ("swingarm", "swingarm"), "guarda": ("swingarm", "swingarm"),
    "soporte_disco_freno_tracero": ("rear_caliper", "swingarm"), "freno_tracero": ("rear_caliper", "swingarm"),
    "trasmision_cadena1": ("chain", "swingarm"),
    "pilar_suspension_tracera": ("shock", "shock_top"), "resorte_suspension_tracera": ("shock", "shock_spring"),
    "shock_rod": ("shock", "shock_bottom"),
    # bodywork (frame-mounted fairing: its lamp, screen, dash and indicators stand still)
    "cobertura_deposito": ("tank", ""), "tapa_deposito": ("tank", ""),
    "asiento": ("saddle", ""),
    "cuerpo_tracero": ("tail", ""), "marco_luz_tracera": ("tail", ""), "luz_tracera": ("tail", ""),
    "cristal_luz_tracera": ("tail", ""), "Intermitentes": ("tail", ""), "intermitentes": ("tail", ""),
    "cuerpo_indicadores_traceros": ("indicators", ""), "cristal_indicador_tracero": ("indicators", ""),
    "interior_indicador_tracero": ("indicators", ""), "cristal_intermitente": ("indicators", ""),
    "luz_intermitentes": ("indicators", ""),
    "cuerpo_principal": ("fairing", ""), "cuerpo_principal1": ("fairing", ""), "frente": ("fairing", ""),
    "alerones": ("fairing", ""), "cupula": ("fairing", ""), "soporte_cupula": ("fairing", ""), "decals": ("fairing", ""),
    "luz_delantera": ("headlight", ""), "cristal_luz_delantera": ("headlight", ""), "interior_luz_delantera": ("headlight", ""),
    "interior_luces_delanteras": ("headlight", ""), "marco_luz_delantera": ("headlight", ""),
    "pantalla_indicadores": ("dashboard", ""),
    "covertura_lateral": ("side_panels", ""),
    # engine and what hangs on it
    "motor": ("engine", ""), "motor1": ("engine", ""), "bloque_motor": ("engine", ""), "tapa_motor": ("engine", ""),
    "tapas_motor": ("engine", ""), "coberturas_motor": ("engine", ""), "frasco_motor": ("engine", ""),
    "radiador": ("cooling", ""), "ventilador_radiador": ("cooling", ""),
    "escape": ("exhaust", ""), "escape1": ("exhaust", ""),
    "estriberas": ("rearsets", ""), "estriberas1": ("rearsets", ""), "estriberas_traceras": ("rearsets", ""),
    "estriberas_traceras02": ("rearsets", ""), "sostenedores_estriveras": ("rearsets", ""),
    "palancas_motor": ("rearsets", ""), "palanca_motor02": ("rearsets", ""),
    # frame: the main frame, the red trellis subframe ("bars"), covers, cables
    "chasis": ("frame", ""), "chasis04": ("frame", ""), "chasis05": ("frame", ""), "bars": ("frame", ""),
    "coberturas_chasis": ("frame", ""), "lineas": ("frame", ""),
}
SPLIT = ("chasis04", "retrovisores", "retrovisores1", "luz_intermitentes", "interior_indicador_tracero",
         "cristal_intermitente", "cristal_indicador_tracero")

# The model's materials carry no real finish numbers (every one metallic 1, rough 0.75, normal maps with no image):
# the game finishes each stands for (Data/BikesV2 "ue"), its colour (linear) and paint slot.
RED = [0.55, 0.012, 0.01]           # Ducati red
FINISH = {
    "m_paint_red": {"ue": "paint_silver", "paint": "body", "surface": "paint__body", "tag": "paint", "color": RED, "metallic": 0.0, "roughness": 0.26},
    "m_paint_white": {"ue": "paint_silver", "surface": "paint_white", "tag": "paint_white", "color": [0.78, 0.78, 0.77], "metallic": 0.0, "roughness": 0.3},
    "m_paint_grey": {"ue": "cat_brushed_steel", "surface": "lettering", "tag": "lettering", "color": [0.42, 0.43, 0.44], "metallic": 0.8, "roughness": 0.35},
    "m_paint_black": {"ue": "cat_powder", "paint": "rim", "surface": "rim__rim", "tag": "rim", "color": [0.0199, 0.022, 0.0265], "metallic": 0.85, "roughness": 0.42},
    "m_paint_black_rough": {"ue": "cat_black_metal", "surface": "black_metal", "tag": "black_metal", "color": [0.03, 0.031, 0.034], "metallic": 0.85, "roughness": 0.45},
    "m_paint_yellow": {"ue": "cat_anodised", "surface": "spring_gold", "tag": "spring_gold", "color": [0.8, 0.5, 0.03], "metallic": 0.9, "roughness": 0.35},
    "m_aluminium_brushed": {"ue": "cat_cast_alloy", "surface": "alu", "tag": "alu", "color": [0.62, 0.63, 0.64], "metallic": 1.0, "roughness": 0.38},
    "m_chrome": {"ue": "chrome", "surface": "chrome", "tag": "chrome"},
    "m_iron_cast": {"ue": "cat_cast_alloy", "surface": "engine_dark", "tag": "engine_dark", "color": [0.16, 0.16, 0.17], "metallic": 0.9, "roughness": 0.5},
    "m_plastic_black": {"ue": "cat_plastic", "surface": "plastic", "tag": "plastic", "color": [0.02, 0.021, 0.023], "metallic": 0.0, "roughness": 0.52},
    "m_plastic_red": {"ue": "cat_plastic", "surface": "plastic_red", "tag": "plastic_red", "color": [0.5, 0.01, 0.01], "metallic": 0.0, "roughness": 0.45},
    "m_plastic_transparent": {"ue": "v2glass", "surface": "d3_clearglass", "tag": "d3_clearglass", "color": [0.8, 0.8, 0.8], "metallic": 0.0, "roughness": 0.04},
    "m_glass_clear": {"ue": "v2glass", "surface": "d3_clearglass", "tag": "d3_clearglass", "color": [1.0, 1.0, 1.0], "metallic": 0.0, "roughness": 0.02},
    "m_glass_red": {"ue": "lens_red", "surface": "d3_redglass", "tag": "lens_red"},
    "m_glass_orange": {"ue": "lens_amber", "surface": "d3_amberglass", "tag": "reflector"},     # the fork feet's reflectors: never lit
    "m_light": {"ue": "lens_clear", "surface": "d3_lampdip", "tag": "lens_white"},
    "m_mirror": {"ue": "mirror_glass", "surface": "d3_mirrorglass", "tag": "d3_mirrorglass"},
    "m_leather_black": {"ue": "leather", "surface": "d3_leather", "tag": "d3_leather", "color": [0.025, 0.025, 0.027], "metallic": 0.0, "roughness": 0.6},
}
# A finish of its own for some objects, over their material's.
OBJ_FINISH = {
    "cupula": {"ue": "v2screen", "surface": "d3_perspex", "tag": "d3_perspex", "color": [0.82, 0.86, 0.9], "metallic": 0.0, "roughness": 0.02},
    "handle": {"ue": "cat_black_metal", "surface": "black_metal", "tag": "black_metal", "color": [0.03, 0.031, 0.034], "metallic": 0.85, "roughness": 0.4},
    "matetas": {"ue": "grip", "surface": "d3_grip", "tag": "d3_grip", "color": [0.016, 0.016, 0.018]},
    "bars": {"ue": "cat_powder", "paint": "frame", "surface": "frame__frame", "tag": "frame", "color": RED, "metallic": 0.6, "roughness": 0.35},
    "discos_freno_delantero": {"ue": "cat_brushed_steel", "surface": "rotor", "tag": "rotor", "color": [0.55, 0.56, 0.57], "metallic": 1.0, "roughness": 0.3},
    "disco_freno_tracero": {"ue": "cat_brushed_steel", "surface": "rotor", "tag": "rotor", "color": [0.55, 0.56, 0.57], "metallic": 1.0, "roughness": 0.3},
    "trasmision_cadena1": {"ue": "cat_brushed_steel", "surface": "chain", "tag": "chain", "color": [0.3, 0.3, 0.31], "metallic": 1.0, "roughness": 0.4},
    "escape": {"ue": "cat_brushed_steel", "surface": "exhaust", "tag": "exhaust", "color": [0.5, 0.48, 0.45], "metallic": 1.0, "roughness": 0.3},
    "suspencion_delantera": {"ue": "cat_anodised", "surface": "stanchion_gold", "tag": "stanchion_gold", "color": [0.75, 0.55, 0.2], "metallic": 1.0, "roughness": 0.15},
    # the lit pieces: the thin LED bar along each lamp's top edge is its daytime running light (beenster in 174: "the
    # headlight LED bars should be set as DRL lights"), the lamps' inner reflectors are the headlamps, the indicators'
    # inner reflectors are lit amber
    "luz_delantera": {"ue": "lens_clear", "surface": "d3_drl", "tag": "lens_drl"},
    "interior_luces_delanteras": {"ue": "lens_clear", "surface": "d3_lampdip", "tag": "lens_white"},
    "luz_intermitentes": {"ue": "lens_amber", "surface": "d3_amberled", "tag": "lens_amber"},
    "interior_indicador_tracero": {"ue": "lens_amber", "surface": "d3_amberled", "tag": "lens_amber"},
    "luz_tracera": {"ue": "lens_red", "surface": "d3_tailred", "tag": "lens_red"},
    "Intermitentes": {"ue": "lens_red", "surface": "redlens", "tag": "reflector"},      # the plate's red reflector
    "pantalla_indicadores": {"ue": "cat_plastic", "surface": "d3_screen", "tag": "d3_screen", "color": [0.004, 0.005, 0.008], "metallic": 0.0, "roughness": 0.04},
}


def to_game(v):
    """A model point (metres) in the game's frame (metres): x forward from the rear axle, y left, z up."""
    return Vector((REAR_Y - v.y, v.x, v.z))


def bbox(ob):
    ws = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    return Vector([min(w[i] for w in ws) for i in range(3)]), Vector([max(w[i] for w in ws) for i in range(3)])


def verts(ob):
    return [ob.matrix_world @ v.co for v in ob.data.vertices]


def piece_place(base, ob):
    """(slot, group, role) of one loose piece of a split object."""
    lo, hi = bbox(ob)
    c = (lo + hi) / 2
    side = "L" if c.x > 0 else "R"
    if base == "chasis04":
        # the swingarm (its big casting, the axle adjusters at its end) moves; the footrest plates and the bolts
        # round the pivot are the frame's
        if (hi.y - lo.y) > 0.5 or lo.y > 0.6:
            return "swingarm", "swingarm", ""
        return "frame", "", ""
    if base == "retrovisores":
        # stalks reach in to the bars; the heads (and their ball joints) sit out at the ends, over the stalks' tops:
        # the game turns each head to the rider's eye
        if lo.z > 1.29 and abs(c.x) > 0.33:
            return "mirrors", "steer", "head" + side
        return "mirrors", "steer", ""
    if base == "retrovisores1":
        return "mirrors", "steer", "glass" + side
    return OBJECTS[base][0], OBJECTS[base][1], side


def main():
    global REAR_Y
    blend = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else sys.argv[1]
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else os.path.join(GAME, "build", "bought")
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=blend)
    names = {o.name for o in bpy.data.objects if o.type == "MESH"}
    unknown = sorted(names - set(OBJECTS))
    missing = sorted(n for n in OBJECTS if n not in names and n != "shock_rod")
    if unknown or missing:
        raise SystemExit(f"model changed: no rule for {unknown}, not found {missing}")
    for o in list(bpy.data.objects):
        if o.type != "MESH":
            bpy.data.objects.remove(o, do_unlink=True)
    # where the axles are (the rims' middles) and the fork's line, from the drawing itself
    rl, rh = bbox(bpy.data.objects["llanta_tracera"])
    fl, fh = bbox(bpy.data.objects["llanta_delantera"])
    REAR_Y = (rl.y + rh.y) / 2
    rear_axle, front_axle = to_game(Vector((0, REAR_Y, (rl.z + rh.z) / 2))), to_game(Vector((0, (fl.y + fh.y) / 2, (fl.z + fh.z) / 2)))
    tl, th = bbox(bpy.data.objects["neumatico_tracero"]) if "neumatico_tracero" in bpy.data.objects else (rl, rh)
    pts_extra = {}
    for nm in ("neumatico_delantero", "neumatico_tracero"):
        lo, hi = bbox(bpy.data.objects[nm])
        pts_extra[nm] = ((hi.z - lo.z) / 2, hi.x - lo.x)
        bpy.data.objects.remove(bpy.data.objects[nm], do_unlink=True)

    # split the objects that hold pieces of more than one group or side into loose pieces
    pieces = []      # (object, base name)
    for o in list(bpy.data.objects):
        base = o.name
        if base in SPLIT:
            bpy.ops.object.select_all(action="DESELECT")
            o.select_set(True)
            bpy.context.view_layer.objects.active = o
            bpy.ops.object.mode_set(mode="EDIT")
            bpy.ops.mesh.select_all(action="SELECT")
            bpy.ops.mesh.separate(type="LOOSE")
            bpy.ops.object.mode_set(mode="OBJECT")
            for p in list(bpy.context.selected_objects):
                pieces.append((p, base))
        else:
            pieces.append((o, base))

    # the shock: the damper body's rear half is its rod end, on the swingarm's mount (TorqueBikePawn [shocks] slides it
    # up into the front half as the shock works); the model draws the body as one piece
    sh_ob = bpy.data.objects["pilar_suspension_tracera"]
    ys = [(sh_ob.matrix_world @ v.co).y for v in sh_ob.data.vertices]
    cut = min(ys) + (max(ys) - min(ys)) * 0.62
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(sh_ob.data)
    rear = [f for f in bm.faces if (sh_ob.matrix_world @ f.calc_center_median()).y > cut]
    rod = bpy.data.meshes.new("shock_rod")
    bm2 = bm.copy()
    bmesh.ops.delete(bm2, geom=[f for f in bm2.faces if (sh_ob.matrix_world @ f.calc_center_median()).y <= cut], context="FACES")
    bm2.to_mesh(rod)
    bmesh.ops.delete(bm, geom=rear, context="FACES")
    bm.to_mesh(sh_ob.data)
    rod_ob = bpy.data.objects.new("shock_rod", rod)
    rod_ob.matrix_world = sh_ob.matrix_world.copy()
    for m in sh_ob.data.materials:
        rod.materials.append(m)
    bpy.context.scene.collection.objects.link(rod_ob)
    pieces.append((rod_ob, "shock_rod"))
    sh_all = [sh_ob.matrix_world @ v.co for v in sh_ob.data.vertices] + [rod_ob.matrix_world @ v.co for v in rod.vertices]

    # key points, from the pieces as drawn (game frame, metres)
    def gv(objs):
        return [to_game(v) for ob in objs for v in verts(ob)]

    fork = gv([bpy.data.objects["ejes_direccion_laterales"]])
    mid = sum(fork, Vector()) / len(fork)
    # the fork's line: its tubes' long axis (the covariance's main direction in the x-z plane)
    sxx = sum((p.x - mid.x) ** 2 for p in fork); szz = sum((p.z - mid.z) ** 2 for p in fork); sxz = sum((p.x - mid.x) * (p.z - mid.z) for p in fork)
    ang = 0.5 * math.atan2(2 * sxz, sxx - szz)
    d = Vector((math.cos(ang), 0, math.sin(ang)))
    if d.z < 0:
        d = -d
    rake = math.degrees(math.atan2(-d.x, d.z))
    stem = gv([p for p, b in pieces if b == "control_direccion"])
    # the steering stem: the top yoke's middle nut (the smallest piece on the centre line of control_direccion)
    ctl = [p for p, b in pieces if b == "control_direccion"]
    top_y = [v for v in stem if abs(v.y) < 0.03]
    stem_at = Vector((sum(v.x for v in top_y) / len(top_y), 0, min(v.z for v in top_y)))
    low = gv([bpy.data.objects["direccion"]])
    low_z = sum(v.z for v in low) / len(low)
    on_axis = lambda z: stem_at + d * ((z - stem_at.z) / d.z)
    steer_top, steer_bottom = on_axis(stem_at.z - 0.01), on_axis(low_z)
    # trail: where the steering axis meets the ground, behind... the front tyre's contact under the axle
    ground = on_axis(0.0)
    trail = ground.x - front_axle.x
    fork_top_z = max(v.z for v in fork)
    fork_mid = Vector((sum(v.x for v in fork) / len(fork), 0, 0))
    # the fork's own line (the tubes' middle) up to their tops
    fork_line = lambda z: Vector((mid.x + d.x * (z - mid.z) / d.z, 0, z))
    fork_top = fork_line(fork_top_z)
    fork_len = (fork_top - front_axle).length
    # swingarm pivot: the round caps either side over the pivot (chasis04's two matching pieces nearest it)
    arm = [p for p, b in pieces if b == "chasis04" and piece_place("chasis04", p)[1] == "swingarm"]
    arm_v = gv(arm)
    front_end = [v for v in arm_v if v.x > max(v.x for v in arm_v) - 0.06]
    pivot = Vector((max(v.x for v in arm_v) - 0.035, 0, sum(v.z for v in front_end) / len(front_end)))
    # shock: its two ends along the damper body
    sh = [to_game(v) for v in sh_all]
    sh_front = max(sh, key=lambda v: v.x)
    sh_rear = min(sh, key=lambda v: v.x)
    # rider: left peg, left grip, hip over the lowest point of the rider's seat
    peg = gv([bpy.data.objects["estriberas"]])
    peg_l = [v for v in peg if v.y > 0]
    grip = gv([bpy.data.objects["matetas"]])
    grip_l = [v for v in grip if v.y > 0]
    seat = gv([bpy.data.objects["asiento"]])
    rider_seat = [v for v in seat if abs(v.y) < 0.04 and v.x > 0.35 and v.x < 0.65]
    hip_v = min(rider_seat, key=lambda v: v.z) if rider_seat else Vector((0.52, 0, 0.84))
    seat_top = max((v for v in seat if abs(v.y) < 0.04 and abs(v.x - hip_v.x) < 0.04), key=lambda v: v.z, default=hip_v)
    cap = gv([bpy.data.objects["tapa_deposito"]])
    # the dash's screen: its face, the side that looks back at the rider
    scr = bpy.data.objects["pantalla_indicadores"]
    best = max(scr.data.polygons, key=lambda p: p.area)
    n = (scr.matrix_world.to_3x3() @ best.normal).normalized()
    n_g = Vector((-n.y, n.x, n.z))
    if n_g.x > 0:
        n_g = -n_g
    sv = gv([scr])
    sc = sum(sv, Vector()) / len(sv)
    # (on the face that looks back at the rider, not in the middle of the screen's slab, where the drawn screen hid it:
    # beenster in 174, "please fit the TFT to the area marked in blue")
    sc = sc + n_g * max((v - sc).dot(n_g) for v in sv)
    up = Vector((-n_g.z, 0, -n_g.x)) if n_g.z < 0 else Vector((n_g.z, 0, -n_g.x))
    if up.z < 0:
        up = -up
    span_w = max(v.y for v in sv) - min(v.y for v in sv)
    span_h = max((v - sc).dot(up) for v in sv) - min((v - sc).dot(up) for v in sv)

    # the rider's knee and the width along the thighs, measured beside the frame's own pieces as bought_export.py does
    # (beenster in 174: "avatar knees are too far out when riding")
    def place_of(ob, base):
        return piece_place(base, ob) if base in SPLIT else (*OBJECTS.get(base, ("frame", "")), "")
    beside = gv([ob for ob, base in pieces if place_of(ob, base)[1] == "" and place_of(ob, base)[0] not in
                 ("exhaust", "handlebars", "mirrors", "rearsets", "dashboard", "headlight", "indicators", "side_stand")])

    def half_w(x0, x1, z0, z1, floor=0.06):
        ys = sorted(abs(v.y) for v in beside if x0 < v.x < x1 and z0 < v.z < z1)
        return ys[int(len(ys) * 0.9)] if ys else floor
    kz = seat_top.z
    knee_y = min(max(half_w(hip_v.x + 0.22, hip_v.x + 0.36, kz - 0.12, kz + 0.06) + 0.05, 0.11), 0.16)
    tank_w = [[round((hip_v.x + 0.05 * i) * 1000, 1),
               round(min(half_w(hip_v.x + 0.05 * i - 0.03, hip_v.x + 0.05 * i + 0.03, kz - 0.15, kz + 0.12), knee_y - 0.05) * 1000, 1),
               round(kz * 1000, 1)] for i in range(1, 9)]

    mm = lambda v: [round(v.x * 1000, 1), round(v.y * 1000, 1), round(v.z * 1000, 1)]
    avg = lambda vs: sum(vs, Vector()) / len(vs)
    (fr, fw), (rr, rw) = pts_extra["neumatico_delantero"], pts_extra["neumatico_tracero"]
    pts = {
        "front_axle": mm(front_axle), "rear_axle": mm(rear_axle),
        "steer_top": mm(steer_top), "steer_bottom": mm(steer_bottom), "fork_top": mm(fork_top),
        "fork_dir": [round(d.x, 5), 0.0, round(d.z, 5)], "swing_pivot": mm(pivot), "rigid": False,
        "hip": [round(hip_v.x * 1000, 1), 0.0, round(seat_top.z * 1000, 1)],
        "stats": {"wheelbase": round((front_axle.x - rear_axle.x) * 1000), "rake_deg": round(rake, 2), "trail": round(trail * 1000),
                  "seat_height": round(seat_top.z * 1000), "ground_clearance": 180, "fork_length": round(fork_len * 1000),
                  "fork_usd": True, "front_radius": round(fr * 1000), "rear_radius": round(rr * 1000),
                  "front_rim": 241, "rear_rim": 216, "front_width": 120, "rear_width": 170,
                  "shaft": False, "twin_shock": False, "tread_front": "trail", "tread_rear": "trail"},
        "peg": mm(avg(peg_l)), "grip": mm(avg(grip_l)),
        "shock_top": [round(sh_front.x * 1000, 1), 0.0, round(sh_front.z * 1000, 1)],
        "shock_bottom": [round(sh_rear.x * 1000, 1), 0.0, round(sh_rear.z * 1000, 1)],
        "tank_cap": mm(avg(cap)),
        "knee": [round((hip_v.x + 0.36) * 1000, 1), round(knee_y * 1000, 1), round(kz * 1000, 1)], "tank_half_w": tank_w,
        "gauges": [{"what": "screen", "at": mm(sc), "axis": [round(n_g.x, 5), 0.0, round(n_g.z, 5)], "up": [round(up.x, 5), 0.0, round(up.z, 5)],
                    "w": round(span_w * 1000 * 0.86, 1), "h": round(span_h * 1000 * 0.82, 1), "node": ""}],
    }

    # surfaces: one per slot + group + finish (+ side, for what the game lights or aims a side at a time)
    tagged, looks = {}, {}
    for ob, base in pieces:
        slot, node, role = piece_place(base, ob) if base in SPLIT else (*OBJECTS[base], "")
        for i, m in enumerate(ob.data.materials):
            if m is None:
                continue
            f = dict(OBJ_FINISH.get(base) or FINISH[m.name.split(".")[0]])
            lens = f["tag"]
            if role.startswith("head"):
                lens = "mirror_head"
            side = role[-1:] if (role and (f["tag"] in ("lens_amber", "d3_mirrorglass") or role.startswith("head"))) else ""
            if lens == "lens_amber":
                # front and back indicators apart too (the game's glow sits beside each lens's own box)
                lo, hi = bbox(ob)
                side += "F" if (lo.y + hi.y) / 2 < REAR_Y - 0.7 else "B"
            key = (slot, node, f["surface"], lens, side)
            if key not in tagged:
                c = bpy.data.materials.new(f"s{len(tagged):03d}")
                tagged[key] = c
                look = {"slot": slot, "node": node, "surface": f["surface"], "tag": lens, "ue": f["ue"]}
                if f.get("paint"):
                    look["paint"] = f["paint"]
                if slot == "mirrors":
                    look["sub"] = "mirrors"
                if slot == "dashboard":
                    look["sub"] = "clocks"
                if "color" in f:
                    look["color"], look["metallic"], look["roughness"] = f["color"], f.get("metallic", 0.0), f.get("roughness", 0.5)
                looks[c.name] = look
            ob.data.materials[i] = tagged[key]

    # join, put in the game's frame, export as build3d does (y up, metres, normals, no UVs)
    obs = [p for p, _ in pieces]
    bpy.ops.object.select_all(action="DESELECT")
    for o in obs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = obs[0]
    bpy.ops.object.make_single_user(object=True, obdata=True, material=False)
    for o in obs:
        mw = o.matrix_world.copy()
        o.parent = None
        o.matrix_world = mw
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = ob.data.name = NAME
    ob.matrix_world = Matrix.Translation((REAR_Y, 0, 0)) @ Matrix.Rotation(math.radians(90), 4, "Z")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # it comes with a side stand (beenster on 172: a shop stand vanished when fitted): bought_stand.py
    sys.path.insert(0, HERE)
    import bought_stand

    def new_material(look):
        c = bpy.data.materials.new(f"s{len(looks):03d}")
        looks[c.name] = look
        return c
    ob = bought_stand.add_stand(ob, "adventure", pts, looks, new_material)
    ob.name = ob.data.name = NAME
    # (the model's custom split normals would be lost in the transform: shade by angle as drawn)
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    glb = os.path.join(out, NAME + ".glb")
    bpy.ops.export_scene.gltf(filepath=glb, use_selection=True, export_apply=True, export_yup=True, export_texcoords=False,
                              export_normals=True, export_materials="EXPORT", export_cameras=False, export_lights=False)
    info = {"name": "Ducati Multistrada V4 S", "brand": "Ducati", "family": "Multistrada", "rev": "bought",
            "bought": {"from": "CGTrader, royalty free (beenster's own copy)", "source": os.path.basename(blend),
                       "note": "the glb is never committed: Tools/bought_bikes.py puts it in the packaged build"},
            "wheelbase_mm": pts["stats"]["wheelbase"], "seat_height_mm": pts["stats"]["seat_height"], "format": 2, "triangles": tris,
            "points_mm": pts, "pieces": sorted({f"{v['slot']}|{v['node']}" for v in looks.values()}), "surfaces": looks}
    jpath = os.path.join(out, NAME + ".json")
    json.dump(info, open(jpath, "w"), indent=1)
    print(f"bought export {NAME}: {len(looks)} surfaces, {len(info['pieces'])} pieces, {tris} triangles -> {glb}")
    print(json.dumps({k: v for k, v in pts.items() if k != "gauges"}))
    print("gauges", pts["gauges"])


if __name__ == "__main__":
    main()
