"""[perf, build 155] Impostors for Ride UK's far trees, baked from the scanned trees themselves.

Round 153 drew trees past 200 m (and the whole far ring) with Buttertubs' low-poly far models, and
beenster found them "ridiculous" (blobby green lumps on the hills). The full scans cost ~9 ms of GPU
at Whaley Bridge. An impostor is the usual answer (Forza, Flight Simulator, Ride): each scanned tree
is photographed once from 64 directions over the upper half of a sphere (a hemi-octahedral 8 x 8
grid), and drawn far off as one camera-facing card showing the views nearest to where you look from,
blended, with their normals lit by the real sun. Baked once per tree MODEL (not per area), so the
ride stays ride-anywhere.

Used by Tools/ue_build_rideuk.py (scanned_trees): for each scanned tree mesh it makes
    /Game/RideUK/Impostors/T_Imp_<tree>_C   base colour (sRGB), 1024 x 1024, 8 x 8 frames of 128 px
    /Game/RideUK/Impostors/T_Imp_<tree>_N   normals in the tree's own space (HDR)
    /Game/RideUK/Impostors/T_Imp_<tree>_D   depth from the capture camera, for the leaves' outline (HDR)
    /Game/RideUK/Impostors/MI_Imp_<tree>    M_RideImpostor with those and the tree's size
    /Game/RideUK/Impostors/SM_Imp_<tree>    a card as tall as the tree (so the game sizes it as the tree)
and the kit gets a species SM_TreeFar_<kind>_imp with the cards, in the scans' order (the game picks
the same variant for a tree's near scan and its far card), in place of that kind's old far models.

Knobs on MI_Imp_<tree> / M_RideImpostor if the first pictures are off: ColourGamma (1: base colour as
captured; 2.2 if it comes out pale), Brightness, NormalEncoded01 (1 if the normal capture is 0..1, which
the bake log says), MaskDepth (the depth that counts as tree, in the tree's radii).
"""
import math

import unreal

IMP_DIR = "/Game/RideUK/Impostors"
FRAMES = 8          # frames a side: 64 views
CAPTURE_PX = 256    # each view captured at this size
CELL_PX = 128       # and drawn into the atlas at this size
ATLAS_PX = FRAMES * CELL_PX
MASTER = "M_RideImpostor"

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[Torque] tree impostors: " + msg)


def _world():
    try:
        return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    except Exception:
        return unreal.EditorLevelLibrary.get_editor_world()


def _actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _set(obj, **props):
    for k, v in props.items():
        try:
            obj.set_editor_property(k, v)
        except Exception as e:
            log("could not set %s on %s: %s" % (k, obj.get_name(), e))


def frame_dir(i, j):
    """The direction (from the tree towards the camera) of frame (i, j): hemi-octahedral, z up.
    The material's view-to-frame mapping is the inverse of this (keep the two the same)."""
    ax = (i / (FRAMES - 1.0)) * 2.0 - 1.0
    ay = (j / (FRAMES - 1.0)) * 2.0 - 1.0
    px, py = (ax + ay) * 0.5, (ax - ay) * 0.5
    z = max(0.0, 1.0 - abs(px) - abs(py))
    n = math.sqrt(px * px + py * py + z * z) or 1.0
    return unreal.Vector(px / n, py / n, z / n)


# ---------------------------------------------------------------- the material
# The card faces the camera (world position offset) about the tree's centre; the pixel shader works
# out where on the card it is, which 4 frames are nearest the view (in the tree's own space, so a
# tree turned by the game shows its matching side), and blends them. All camera/centre subtraction
# is done in the graph (large world coordinates), so the custom code sees only small float3s.
_FRAME_CODE = """
float3 Vl = normalize(RelLocal);
Vl.z = max(Vl.z, 0.0);
Vl = normalize(Vl + float3(0, 0, 1e-4));
float3 Vw = normalize(Rel);
float3 Rt = cross(Vw, float3(0, 0, 1));
Rt = dot(Rt, Rt) < 1e-6 ? float3(0, 1, 0) : normalize(Rt);
float3 Upv = cross(Rt, Vw);
float Sc = max(length(ScaleX), 1e-4);
float2 q = float2(dot(PmC, Rt), dot(PmC, Upv)) / (R * Sc);
float2 inF = saturate(float2(q.x * 0.5 + 0.5, 0.5 - q.y * 0.5));
float sm = abs(Vl.x) + abs(Vl.y) + Vl.z;
float2 p = Vl.xy / sm;
float2 g = saturate(float2(p.x + p.y, p.x - p.y) * 0.5 + 0.5);
float2 f = g * (Frames - 1);
float2 f0 = floor(f);
float2 w = f - f0;
float2 f1 = min(f0 + 1, Frames - 1);
float2 uvs[4] = { (f0 + inF) / Frames, (float2(f1.x, f0.y) + inF) / Frames, (float2(f0.x, f1.y) + inF) / Frames, (f1 + inF) / Frames };
float ws[4] = { (1 - w.x) * (1 - w.y), w.x * (1 - w.y), (1 - w.x) * w.y, w.x * w.y };
float masks[4];
float msum = 0;
for (int k = 0; k < 4; k++)
{
    masks[k] = Texture2DSampleLevel(TexD, TexDSampler, uvs[k], 0).r < MaskDepth * R ? 1.0 : 0.0;
    msum += ws[k] * masks[k];
}
"""

_CODE_OPACITY = _FRAME_CODE + "return msum;"
_CODE_COLOUR = _FRAME_CODE + """
float3 c = 0;
for (int k2 = 0; k2 < 4; k2++) c += ws[k2] * masks[k2] * Texture2DSample(TexC, TexCSampler, uvs[k2]).rgb;
c /= max(msum, 1e-3);
return pow(max(c, 0), ColourGamma) * Brightness;
"""
_CODE_NORMAL = _FRAME_CODE + """
float3 n = 0;
for (int k2 = 0; k2 < 4; k2++)
{
    float3 nk = Texture2DSample(TexN, TexNSampler, uvs[k2]).rgb;
    nk = lerp(nk, nk * 2 - 1, NormalEncoded01);
    n += ws[k2] * masks[k2] * nk;
}
return normalize(n + float3(0, 0, 1e-3));
"""
_CODE_WPO = """
float3 Vw = normalize(Rel);
float3 Rt = cross(Vw, float3(0, 0, 1));
Rt = dot(Rt, Rt) < 1e-6 ? float3(0, 1, 0) : normalize(Rt);
float3 Upv = cross(Rt, Vw);
float Sc = max(length(ScaleX), 1e-4);
float u = clamp(LocalP.x / R, -1, 1);
float v = saturate((LocalP.z - MinZ) / max(MaxZ - MinZ, 1)) * 2 - 1;
return CtoV + (Rt * u + Upv * v) * R * Sc;
"""


def _expr(m, cls, x, y):
    return mel.create_material_expression(m, cls, x, y)


def _scalar(m, name, value, x, y):
    e = _expr(m, unreal.MaterialExpressionScalarParameter, x, y)
    _set(e, parameter_name=name, default_value=value)
    return e


def _custom(m, code, out_type, inputs, x, y, label):
    e = _expr(m, unreal.MaterialExpressionCustom, x, y)
    _set(e, code=code, output_type=out_type, description=label,
         inputs=[unreal.CustomInput(input_name=name) for name, _ in inputs])
    for name, src in inputs:
        mel.connect_material_expressions(src, "", e, name)
    return e


def _pos_transform(m, src, to_world, x, y):
    e = _expr(m, unreal.MaterialExpressionTransformPosition, x, y)
    P = unreal.MaterialPositionTransformSource
    if to_world:
        _set(e, transform_source_type=P.TRANSFORMPOSSOURCE_LOCAL, transform_type=P.TRANSFORMPOSSOURCE_WORLD)
    else:
        _set(e, transform_source_type=P.TRANSFORMPOSSOURCE_WORLD, transform_type=P.TRANSFORMPOSSOURCE_LOCAL)
    mel.connect_material_expressions(src, "", e, "Input")
    return e


def _vec_transform(m, src, to_world, x, y):
    e = _expr(m, unreal.MaterialExpressionTransform, x, y)
    S, D = unreal.MaterialVectorCoordTransformSource, unreal.MaterialVectorCoordTransform
    if to_world:
        _set(e, transform_source_type=S.TRANSFORMSOURCE_LOCAL, transform_type=D.TRANSFORM_WORLD)
    else:
        _set(e, transform_source_type=S.TRANSFORMSOURCE_WORLD, transform_type=D.TRANSFORM_LOCAL)
    mel.connect_material_expressions(src, "", e, "Input")
    return e


def _sub(m, a, b, x, y):
    e = _expr(m, unreal.MaterialExpressionSubtract, x, y)
    mel.connect_material_expressions(a, "", e, "A")
    mel.connect_material_expressions(b, "", e, "B")
    return e


def master_material():
    path = "%s/%s" % (IMP_DIR, MASTER)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MASTER, IMP_DIR, unreal.Material, unreal.MaterialFactoryNew())
    _set(m, blend_mode=unreal.BlendMode.BLEND_MASKED, two_sided=True, tangent_space_normal=False,
         opacity_mask_clip_value=0.5, used_with_instanced_static_meshes=True)
    tex = {}
    for k, (name, y) in enumerate((("TexC", 0), ("TexN", 300), ("TexD", 600))):
        e = _expr(m, unreal.MaterialExpressionTextureObjectParameter, -1600, y)
        _set(e, parameter_name=name)
        tex[name] = e
    centre = _expr(m, unreal.MaterialExpressionVectorParameter, -2200, 900)
    _set(centre, parameter_name="Centre", default_value=unreal.LinearColor(0, 0, 500, 0))
    centre3 = _expr(m, unreal.MaterialExpressionComponentMask, -2000, 900)
    _set(centre3, r=True, g=True, b=True, a=False)
    mel.connect_material_expressions(centre, "", centre3, "")
    centre_ws = _pos_transform(m, centre3, True, -1800, 900)
    cam = _expr(m, unreal.MaterialExpressionCameraPositionWS, -1800, 1100)
    wp = _expr(m, unreal.MaterialExpressionWorldPosition, -1800, 1250)
    rel = _sub(m, cam, centre_ws, -1500, 1000)               # camera - centre
    pmc = _sub(m, wp, centre_ws, -1500, 1200)                # pixel - centre
    ctov = _sub(m, centre_ws, wp, -1500, 1400)               # centre - vertex (for the card's offset)
    rel_local = _vec_transform(m, rel, False, -1300, 1000)
    unit_x = _expr(m, unreal.MaterialExpressionConstant3Vector, -1800, 1500)
    _set(unit_x, constant=unreal.LinearColor(1, 0, 0, 0))
    scale_x = _vec_transform(m, unit_x, True, -1500, 1600)
    local_p = _pos_transform(m, wp, False, -1500, 1800)
    r = _scalar(m, "R", 500.0, -1300, 1700)
    frames = _scalar(m, "Frames", float(FRAMES), -1300, 1800)
    minz = _scalar(m, "MinZ", 0.0, -1300, 1900)
    maxz = _scalar(m, "MaxZ", 1000.0, -1300, 2000)
    gamma = _scalar(m, "ColourGamma", 1.0, -1300, 2100)
    bright = _scalar(m, "Brightness", 1.0, -1300, 2200)
    enc = _scalar(m, "NormalEncoded01", 0.0, -1300, 2300)
    mdepth = _scalar(m, "MaskDepth", 3.5, -1300, 2400)
    O = unreal.CustomMaterialOutputType
    common = [("Rel", rel), ("RelLocal", rel_local), ("PmC", pmc), ("ScaleX", scale_x), ("R", r), ("Frames", frames),
              ("MaskDepth", mdepth), ("TexD", tex["TexD"])]
    colour = _custom(m, _CODE_COLOUR, O.CMOT_FLOAT3, common + [("TexC", tex["TexC"]), ("ColourGamma", gamma), ("Brightness", bright)], -800, 0, "impostor colour")
    opacity = _custom(m, _CODE_OPACITY, O.CMOT_FLOAT1, common, -800, 300, "impostor outline")
    normal_l = _custom(m, _CODE_NORMAL, O.CMOT_FLOAT3, common + [("TexN", tex["TexN"]), ("NormalEncoded01", enc)], -800, 600, "impostor normal")
    normal_w = _vec_transform(m, normal_l, True, -500, 600)
    wpo = _custom(m, _CODE_WPO, O.CMOT_FLOAT3, [("Rel", rel), ("CtoV", ctov), ("LocalP", local_p), ("ScaleX", scale_x), ("R", r),
                                                ("MinZ", minz), ("MaxZ", maxz)], -800, 1000, "impostor card faces the camera")
    rough = _expr(m, unreal.MaterialExpressionConstant, -500, 900)
    _set(rough, r=0.85)
    spec = _expr(m, unreal.MaterialExpressionConstant, -500, 1000)
    _set(spec, r=0.2)
    MP = unreal.MaterialProperty
    mel.connect_material_property(colour, "", MP.MP_BASE_COLOR)
    mel.connect_material_property(opacity, "", MP.MP_OPACITY_MASK)
    mel.connect_material_property(normal_w, "", MP.MP_NORMAL)
    mel.connect_material_property(wpo, "", MP.MP_WORLD_POSITION_OFFSET)
    mel.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    mel.connect_material_property(spec, "", MP.MP_SPECULAR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    log("made %s" % path)
    return m


# ---------------------------------------------------------------- the bake
def _rt(fmt, px):
    return unreal.RenderingLibrary.create_render_target2d(_world(), px, px, fmt, unreal.LinearColor(0, 0, 0, 0), False)


def _rt_asset(name, fmt, px):
    """An atlas render target saved as an asset (a texture is made next to it at the end: a transient
    render target has no package to make one in)."""
    folder = IMP_DIR + "/Work"
    path = "%s/%s" % (folder, name)
    rt = unreal.load_asset(path) if eal.does_asset_exist(path) else None
    if rt is None:
        rt = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.TextureRenderTarget2D, unreal.TextureRenderTargetFactoryNew())
    _set(rt, render_target_format=fmt, clear_color=unreal.LinearColor(0, 0, 0, 0))
    unreal.RenderingLibrary.resize_render_target2d(rt, px, px)
    unreal.RenderingLibrary.clear_render_target2d(_world(), rt, unreal.LinearColor(0, 0, 0, 0))
    return rt


def _bounds(sm):
    b = sm.get_bounds()
    centre = b.origin
    ext = b.box_extent
    radius = b.sphere_radius
    return centre, ext, radius


def bake(mesh_path):
    """Bakes one scanned tree; the path of its card mesh, or None."""
    sm = unreal.load_asset(mesh_path.split(".")[0])
    if not isinstance(sm, unreal.StaticMesh):
        log("%s: not a static mesh" % mesh_path)
        return None
    base = sm.get_name()
    if base.startswith("SM_"):
        base = base[3:]
    card_path = "%s/SM_Imp_%s" % (IMP_DIR, base)
    if eal.does_asset_exist(card_path) and eal.does_asset_exist("%s/MI_Imp_%s" % (IMP_DIR, base)):
        return "%s.SM_Imp_%s" % (card_path, base)
    centre, ext, radius = _bounds(sm)
    world = _world()
    acts = _actors()
    at = unreal.Vector(0, 0, 2000000.0)   # (well away from anything in the editor's level)
    tree = acts.spawn_actor_from_class(unreal.StaticMeshActor, at)
    tree.static_mesh_component.set_static_mesh(sm)
    tree.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    cap_actor = acts.spawn_actor_from_class(unreal.SceneCapture2D, at)
    cap = cap_actor.capture_component2d
    _set(cap, capture_every_frame=False, capture_on_movement=False, projection_type=unreal.CameraProjectionMode.ORTHOGRAPHIC,
         ortho_width=radius * 2.0, primitive_render_mode=unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST,
         show_only_actors=[tree])
    F = unreal.TextureRenderTargetFormat
    shots = {"C": (unreal.SceneCaptureSource.SCS_BASE_COLOR, _rt(F.RTF_RGBA8, CAPTURE_PX), _rt_asset("RT_Imp_C", F.RTF_RGBA8, ATLAS_PX)),
             "N": (unreal.SceneCaptureSource.SCS_NORMAL, _rt(F.RTF_RGBA16F, CAPTURE_PX), _rt_asset("RT_Imp_N", F.RTF_RGBA16F, ATLAS_PX)),
             "D": (unreal.SceneCaptureSource.SCS_SCENE_DEPTH, _rt(F.RTF_R16F, CAPTURE_PX), _rt_asset("RT_Imp_D", F.RTF_R16F, ATLAS_PX))}
    centre_ws = at + centre
    dist = radius * 2.0
    probe = None
    for key, (source, shot, atlas) in shots.items():
        _set(cap, capture_source=source, texture_target=shot)
        for i in range(FRAMES):
            for j in range(FRAMES):
                d = frame_dir(i, j)
                cap_actor.set_actor_location_and_rotation(centre_ws + d * dist, unreal.MathLibrary.make_rot_from_x(d * -1.0), False, False)
                cap.capture_scene()
                # (a canvas draws its batch when it ends, so one per frame: the shot is the next view's by then)
                canvas, size, ctx = unreal.RenderingLibrary.begin_draw_canvas_to_render_target(world, atlas)
                canvas.draw_texture(shot, unreal.Vector2D(i * CELL_PX, j * CELL_PX), unreal.Vector2D(CELL_PX, CELL_PX),
                                    unreal.Vector2D(0, 0), unreal.Vector2D(1, 1), unreal.LinearColor(1, 1, 1, 1), unreal.BlendMode.BLEND_OPAQUE)
                unreal.RenderingLibrary.end_draw_canvas_to_render_target(world, ctx)
                if key == "N" and i == FRAMES // 2 and j == FRAMES // 2 and probe is None:
                    try:
                        probe = unreal.RenderingLibrary.read_render_target_raw_pixel(world, shot, CAPTURE_PX // 2, CAPTURE_PX // 2)
                    except Exception:
                        probe = None
    if probe is not None:
        log("%s: normal capture at the middle of a side view reads %.2f %.2f %.2f (negatives: -1..1, set NormalEncoded01 0; all 0..1: maybe 1)"
            % (base, probe.r, probe.g, probe.b))
    # (the atlases as textures)
    T = unreal.TextureCompressionSettings
    made = {}
    for key, (source, shot, atlas) in shots.items():
        name = "T_Imp_%s_%s" % (base, key)
        dest = "%s/%s" % (IMP_DIR, name)
        if eal.does_asset_exist(dest):
            eal.delete_asset(dest)
        comp = T.TC_DEFAULT if key == "C" else T.TC_HDR
        mips = unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP if key != "D" else unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS
        tex = unreal.RenderingLibrary.render_target_create_static_texture2d_editor(atlas, comp, mips)
        if tex is None:
            log("%s: no texture from the %s atlas" % (base, key))
            break
        tex_path = tex.get_path_name().split(".")[0]
        if tex_path != dest:
            eal.rename_asset(tex_path, dest)
            tex = unreal.load_asset(dest)
        # (base colour as captured: linear; ColourGamma on the material if it comes out pale)
        _set(tex, srgb=False)
        eal.save_asset(dest, only_if_is_dirty=False)
        made[key] = tex
    tree.destroy_actor()
    cap_actor.destroy_actor()
    if len(made) < 3:
        return None
    # (the tree's own material instance)
    mi_name = "MI_Imp_%s" % base
    mi_path = "%s/%s" % (IMP_DIR, mi_name)
    if eal.does_asset_exist(mi_path):
        eal.delete_asset(mi_path)
    mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(mi_name, IMP_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, unreal.load_asset("%s/%s" % (IMP_DIR, MASTER)))
    for key in ("C", "N", "D"):
        mel.set_material_instance_texture_parameter_value(mi, "Tex" + key, made[key])
    mel.set_material_instance_scalar_parameter_value(mi, "R", radius)
    mel.set_material_instance_scalar_parameter_value(mi, "MinZ", centre.z - ext.z)
    mel.set_material_instance_scalar_parameter_value(mi, "MaxZ", centre.z + ext.z)
    mel.set_material_instance_vector_parameter_value(mi, "Centre", unreal.LinearColor(centre.x, centre.y, centre.z, 0))
    eal.save_loaded_asset(mi)
    # (the card: as tall as the tree, 2 radii wide, in the tree's x-z plane; the material turns it to
    # face the camera, so its bounds reach a radius out in x and y)
    card = _card(card_path, radius, centre.z - ext.z, centre.z + ext.z, mi)
    if card is None:
        return None
    log("%s: baked (radius %.0f cm, height %.0f cm)" % (base, radius, ext.z * 2.0))
    return card.get_path_name()


def _card(path, radius, min_z, max_z, mi):
    gs_prim = getattr(unreal, "GeometryScript_MeshPrimitiveFunctions", None)
    make = None
    for home in ("GeometryScript_AssetUtils", "GeometryScript_NewAssetUtils", "GeometryScript_CreateNewAssetFunctions"):
        c = getattr(unreal, home, None)
        if c is not None and hasattr(c, "create_new_static_mesh_asset_from_mesh"):
            make = c.create_new_static_mesh_asset_from_mesh
            break
    if gs_prim is None or make is None:
        log("no Geometry Script (enable the GeometryScripting plugin): no card made")
        return None
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    dm = unreal.DynamicMesh()
    height = max(max_z - min_z, 1.0)
    # (a rectangle in x-y, turned up into x-z, its bottom at min_z)
    xf = unreal.Transform(location=unreal.Vector(0, 0, min_z + height * 0.5), rotation=unreal.Rotator(roll=90.0, pitch=0.0, yaw=0.0), scale=unreal.Vector(1, 1, 1))
    gs_prim.append_rectangle_xy(dm, unreal.GeometryScriptPrimitiveOptions(), xf, radius * 2.0, height, 0, 0)
    opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    for prop, val in (("enable_nanite", False), ("enable_collision", False)):
        try:
            opts.set_editor_property(prop, val)
        except Exception:
            pass
    res = make(dm, path, opts)
    sm = res[0] if isinstance(res, (tuple, list)) else res
    if not isinstance(sm, unreal.StaticMesh):
        log("%s: no card mesh made" % path)
        return None
    sm.set_editor_property("static_materials", [unreal.StaticMaterial(material_interface=mi, material_slot_name="Impostor")])
    # (the card turns to face the camera: bounds a radius out sideways, none up, so the game's sizing
    # by the model's height is the tree's)
    _set(sm, positive_bounds_extension=unreal.Vector(radius, radius, 0), negative_bounds_extension=unreal.Vector(radius, radius, 0))
    b = sm.get_bounds()
    log("%s: card %.0f x %.0f cm, bounds height %.0f (tree %.0f)" % (path.rsplit("/", 1)[-1], radius * 2.0, height, b.box_extent.z * 2.0, height))
    eal.save_asset(path, only_if_is_dirty=False)
    return sm


def bake_all(mesh_paths, rebake=False):
    """The card meshes for these scanned trees, in their order (None where a bake failed)."""
    if not eal.does_directory_exist(IMP_DIR):
        eal.make_directory(IMP_DIR)
    if rebake or not eal.does_asset_exist("%s/%s" % (IMP_DIR, MASTER)):
        master_material()
    if rebake:
        for a in eal.list_assets(IMP_DIR, recursive=False, include_folder=False):
            n = a.rsplit("/", 1)[-1]
            if n.startswith("SM_Imp_") or n.startswith("MI_Imp_"):
                eal.delete_asset(a.split(".")[0])
    out = []
    for p in mesh_paths:
        try:
            out.append(bake(p))
        except Exception as e:
            log("%s: bake failed: %s" % (p, e))
            out.append(None)
    return out
