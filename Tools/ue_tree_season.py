"""[seasonal trees TEST, Nov 19 2026] Leaves that follow the season on the scanned (Megaplants) trees and
their far impostors. beenster: "yes, lets have a test render before comitting" and "lets try seasonal
trees for this afternoon". A test switch only: nothing changes in the game unless the season console
variable is set (torque.RideTreeSeason, 0 = as now) or the season test runs (-TorqueRideAB=2).

Three scalars in MPC_Weather (Tools/ue_build_level.py WEATHER_PARAMS), set by the game
(Coop/TorqueGameState.cpp) from torque.RideTreeSeason:
    LeafFresh  0..1  spring: leaves paler and yellower
    LeafTurn   0..1  autumn: leaves take the species' autumn colour (and stay that colour, dead, in winter)
    LeafDrop   0..1  the share of leaves gone (a random cut per leaf clump of about 25 cm)

The scans' own leaf master (a Megaplants material: masked and two-sided) gets two Custom nodes put in
front of its base colour and opacity mask, once (found again by their description); each scanned
kind's material instance then gets its own:
    SeasonOn     1 broadleaves, 0 pines and spruces (unchanged all year)
    AutumnColour the species' autumn leaf
    WinterKeep   the share of (dead) leaves kept through winter: oak and beech hold some
At summer (all three 0) the leaves come out exactly as before.

The impostor master (Tools/ue_tree_impostors.py) gets the same colour turn and a lighter thinning; the
game sets each impostor's SeasonOn and AutumnColour from its scan's name (World/TorqueImpostors.cpp).
"""
import unreal

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary

COLOUR_DESC = "ride season colour"
OPACITY_DESC = "ride season leaves"

# (linear albedo of the turned leaf; the node keeps the leaf's own brightness and takes this hue)
AUTUMN = {
    "oak": (0.30, 0.17, 0.04),        # brown
    "beech": (0.45, 0.17, 0.03),      # copper
    "birch": (0.62, 0.48, 0.06),      # yellow
    "sycamore": (0.55, 0.36, 0.05),   # yellow-orange
    "alder": (0.16, 0.15, 0.05),      # drops nearly green
    "willow": (0.45, 0.42, 0.10),     # yellow
    "rowan": (0.55, 0.12, 0.03),      # red
    "hazel": (0.50, 0.40, 0.07),      # yellow
}
WINTER_KEEP = {"oak": 0.06, "beech": 0.12}
EVERGREEN = ("pine", "spruce")

_HASH = "frac(sin(dot(floor(Pos / %s), float3(12.9898, 78.233, 37.719))) * 43758.5453)"

# (Orig: the leaf's own colour. A 40 cm cell's hash varies the turn a little, leaf clump by clump.)
COLOUR_CODE = """float on = saturate(SeasonOn);
float l = dot(Orig, float3(0.3, 0.59, 0.11));
float3 c = lerp(Orig, l * float3(1.5, 2.1, 0.55), saturate(Fresh * on) * 0.55);
float al = max(dot(AutumnColour.rgb, float3(0.3, 0.59, 0.11)), 0.01);
float3 a = AutumnColour.rgb * (l / al) * 1.15;
float h = """ + _HASH % "40.0" + """;
return lerp(c, a, saturate(Turn * on * (0.75 + 0.5 * h)));
"""

# (Mask: the leaf's own opacity mask. A leaf clump goes when its hash is under the drop.)
OPACITY_CODE = """float on = saturate(SeasonOn);
float d = min(Drop, 1.0 - WinterKeep) * on;
float h = """ + _HASH % "25.0" + """;
return Mask * step(d, h);
"""

# (the far cards: the turn as above, and a lighter cut, since a bare wood still reads as twigs far off;
# winter also greys what's left to bare-twig brown)
IMPOSTOR_COLOUR_CODE = """float on = saturate(SeasonOn);
float l = dot(Orig, float3(0.3, 0.59, 0.11));
float3 c = lerp(Orig, l * float3(1.5, 2.1, 0.55), saturate(Fresh * on) * 0.55);
float al = max(dot(AutumnColour.rgb, float3(0.3, 0.59, 0.11)), 0.01);
float h = """ + _HASH % "300.0" + """;
c = lerp(c, AutumnColour.rgb * (l / al) * 1.15, saturate(Turn * on * (0.75 + 0.5 * h)));
return lerp(c, float3(0.075, 0.062, 0.052), saturate((Drop - 0.5) * 2.0) * on * 0.8);
"""
IMPOSTOR_OPACITY_CODE = """float on = saturate(SeasonOn);
float h = """ + _HASH % "60.0" + """;
return Mask * step(Drop * 0.45 * on, h);
"""


def log(msg):
    unreal.log("[Torque] tree season: " + msg)


def _set(obj, **props):
    for k, v in props.items():
        obj.set_editor_property(k, v)


def _custom_input(name):
    ci = unreal.CustomInput()
    ci.set_editor_property("input_name", name)
    return ci


def _connect(src, src_out, dst, pin):
    for name in ([pin, ""] if pin else [""]):
        try:
            if mel.connect_material_expressions(src, src_out or "", dst, name):
                return True
        except Exception:
            pass
    return False


def _custom(m, code, out_type, inputs, x, y, label):
    """inputs: (name, expression, its output's name)."""
    e = mel.create_material_expression(m, unreal.MaterialExpressionCustom, x, y)
    _set(e, code=code, output_type=out_type, description=label, inputs=[_custom_input(n) for n, _, _ in inputs])
    bad = [n for n, src, out in inputs if not _connect(src, out, e, n)]
    if bad:
        raise RuntimeError("%s: inputs not connected: %s" % (label, ", ".join(bad)))
    return e


def _params(m, L, x, y, impostor=False):
    """The season's own inputs: the three collection scalars and the per-instance ones."""
    mpc = L.weather_mpc()
    if mpc is None:
        raise RuntimeError("no MPC_Weather")
    names = set(str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("scalar_parameters") or [])
    out = []
    for k, name in enumerate(("LeafFresh", "LeafTurn", "LeafDrop")):
        if name not in names:
            raise RuntimeError("MPC_Weather has no %s (Tools/ue_build_level.py WEATHER_PARAMS)" % name)
        e = mel.create_material_expression(m, unreal.MaterialExpressionCollectionParameter, x, y + k * 80)
        _set(e, collection=mpc, parameter_name=name)
        out.append(e)
    on = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, x, y + 260)
    _set(on, parameter_name="SeasonOn", default_value=1.0)
    autumn = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, x, y + 340)
    _set(autumn, parameter_name="AutumnColour", default_value=unreal.LinearColor(*AUTUMN["oak"], 1.0))
    keep = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, x, y + 480)
    _set(keep, parameter_name="WinterKeep", default_value=0.0)
    pos = mel.create_material_expression(m, unreal.MaterialExpressionWorldPosition, x, y + 560)
    fresh, turn, drop = out
    return fresh, turn, drop, on, autumn, keep, pos


def wrap(m, L, colour, colour_out, mask, mask_out, x=-400, y=0, impostor=False):
    """Two Custom nodes taking colour and mask through the season; returns them (colour, mask)."""
    fresh, turn, drop, on, autumn, keep, pos = _params(m, L, x - 400, y + 900, impostor)
    O = unreal.CustomMaterialOutputType
    c = _custom(m, IMPOSTOR_COLOUR_CODE if impostor else COLOUR_CODE, O.CMOT_FLOAT3,
                [("Orig", colour, colour_out), ("Fresh", fresh, ""), ("Turn", turn, ""), ("Drop", drop, ""),
                 ("SeasonOn", on, ""), ("AutumnColour", autumn, ""), ("Pos", pos, "")], x, y, COLOUR_DESC)
    if mask is None:
        mask = mel.create_material_expression(m, unreal.MaterialExpressionConstant, x - 200, y + 300)
        _set(mask, r=1.0)
        mask_out = ""
    o = _custom(m, IMPOSTOR_OPACITY_CODE if impostor else OPACITY_CODE, O.CMOT_FLOAT1,
                [("Mask", mask, mask_out), ("Drop", drop, ""), ("WinterKeep", keep, ""), ("SeasonOn", on, ""), ("Pos", pos, "")],
                x, y + 300, OPACITY_DESC)
    return c, o


def _is_leaf(base):
    try:
        masked = base.get_editor_property("blend_mode") == unreal.BlendMode.BLEND_MASKED
        two = bool(base.get_editor_property("two_sided"))
        return masked and two
    except Exception:
        return False


def _input(m, prop):
    src = mel.get_material_property_input_node(m, prop)
    if src is None:
        return None, ""
    try:
        out = mel.get_material_property_input_node_output_name(m, prop)
    except Exception:
        out = ""
    return src, out or ""


def _season_master(base, L):
    """Put the season in front of a leaf master's base colour and opacity mask, once. True if it has it."""
    MP = unreal.MaterialProperty
    src, out = _input(base, MP.MP_BASE_COLOR)
    if src is None:
        try:
            attrs = bool(base.get_editor_property("use_material_attributes"))
        except Exception:
            attrs = False
        log("%s: nothing feeds its base colour%s: left as it is" % (base.get_path_name(),
            " (it uses material attributes)" if attrs else ""))
        return False
    try:
        if isinstance(src, unreal.MaterialExpressionCustom) and str(src.get_editor_property("description")) == COLOUR_DESC:
            return True
    except Exception:
        pass
    mask, mask_out = _input(base, MP.MP_OPACITY_MASK)
    c, o = wrap(base, L, src, out, mask, mask_out, -500, 0)
    if not mel.connect_material_property(c, "", MP.MP_BASE_COLOR):
        raise RuntimeError("couldn't connect the season colour")
    if not mel.connect_material_property(o, "", MP.MP_OPACITY_MASK):
        raise RuntimeError("couldn't connect the season leaves")
    mel.recompile_material(base)
    eal.save_loaded_asset(base)
    log("%s: season put in front of its leaves' colour and opacity" % base.get_path_name())
    return True


def season_kind(L, kind, mesh_paths):
    """The season on one scanned kind: its leaf master(s) wired once, its instances given the kind's own
    autumn colour, winter keep and SeasonOn. Logs what it did; never raises."""
    evergreen = kind in EVERGREEN
    colour = AUTUMN.get(kind, AUTUMN["oak"])
    done_masters, done_mis, plain = [], [], []
    for path in mesh_paths:
        try:
            sm = unreal.load_asset(path.split(".")[0])
            for slot in sm.get_editor_property("static_materials"):
                mi = slot.get_editor_property("material_interface")
                base = mi.get_base_material() if mi else None
                if base is None or not _is_leaf(base):
                    continue
                if base.get_path_name() not in done_masters:
                    done_masters.append(base.get_path_name())
                    if not _season_master(base, L):
                        continue
                if not isinstance(mi, unreal.MaterialInstanceConstant):
                    plain.append(mi.get_name())
                    continue
                if mi.get_path_name() in done_mis:
                    continue
                done_mis.append(mi.get_path_name())
                mel.set_material_instance_scalar_parameter_value(mi, "SeasonOn", 0.0 if evergreen else 1.0)
                mel.set_material_instance_vector_parameter_value(mi, "AutumnColour", unreal.LinearColor(*colour, 1.0))
                mel.set_material_instance_scalar_parameter_value(mi, "WinterKeep", WINTER_KEEP.get(kind, 0.01))
                mel.update_material_instance(mi)
                eal.save_loaded_asset(mi)
        except Exception as e:
            log("%s: %s: FAILED: %s" % (kind, path.rsplit(".", 1)[-1], e))
    log("%s: %s; %d leaf instances set%s" % (kind, "evergreen (unchanged all year)" if evergreen else "autumn %s" % (colour,),
        len(done_mis), ("; NOT set, no instance of their own: " + ", ".join(plain)) if plain else ""))
