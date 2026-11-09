"""The shed's real models in UE: imports every prop in Data/Props/props.json and dresses the shed with
them (Data/Props/swaps.json). Run in the editor, after Tools/fetch_props.py has downloaded the scans:

    UnrealEditor-Cmd TorqueShedProto.uproject -ExecutePythonScript="Tools/ue_import_props.py"
    UnrealEditor-Cmd TorqueShedProto.uproject -ExecutePythonScript="Tools/ue_import_props.py all"

'all' re-imports everything; without it, only props not yet in the project. Then build the shed
(Tools/ue_build_shed.py), which calls dress() below at its end.

  * Blender props (Data/Props/Blender/<id>.glb, committed): their material slots are named by the keys
    in Data/Props/materials.json. Each key becomes a material instance MI_Prop_<key> of one master
    (M_PropMaster, or M_PropGlass for see-through ones): its tint, roughness, metalness and glow,
    with the base's scanned maps (Poly Haven, materials.json 'scans') tiled over the props' UVs (laid
    out in metres) for the wear, grain and dents a flat colour hasn't got.
  * Poly Haven props (build/props/<id>/<id>.gltf): imported with their own scanned materials.
Each lands at /Game/Shed/Props/<id>/SM_<id>, Nanite off (they're a few thousand triangles, and
some have glass), no collision of their own: the shed's boxes keep doing the colliding.
"""
import json
import os
import sys

import unreal

ROOT = os.path.abspath(unreal.Paths.project_dir())
DATA = os.path.join(ROOT, "Data", "Props")
FETCHED = os.path.join(ROOT, "build", "props")
DEST = "/Game/Shed/Props"
MATS = DEST + "/Materials"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[Torque] props: " + msg)


def warn(msg):
    unreal.log_warning("[Torque] props: " + msg)


def try_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:
        warn("couldn't set %s: %s" % (prop, e))
        return False


# --- Materials --------------------------------------------------------------------------------------

def expr(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        try_set(e, k, v)
    return e


def link(a, a_out, b, b_in):
    if not mel.connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError("couldn't connect %s.%s -> %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))


def new_asset(name, folder, cls, factory):
    path = folder + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    return asset_tools.create_asset(name, folder, cls, factory)


def master():
    """M_PropMaster: Tint x (the scan's light and shade, by Detail), Rough x (the scan's roughness, by
    Detail), Metal, the scan's normal, Glow x Tint as light. UVs x Tile (the props' UVs are metres)."""
    m = new_asset("M_PropMaster", MATS, unreal.Material, unreal.MaterialFactoryNew())
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1600, 0)
    tile = expr(m, unreal.MaterialExpressionScalarParameter, -1600, 120, parameter_name="Tile", default_value=1.0)
    uvs = expr(m, unreal.MaterialExpressionMultiply, -1400, 0)
    link(uv, "", uvs, "A")
    link(tile, "", uvs, "B")
    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    grey = unreal.load_asset("/Engine/EngineResources/Black") or white
    flatn = unreal.load_asset("/Engine/EngineMaterials/FlatNormal") or unreal.load_asset("/Engine/EngineMaterials/DefaultNormal")
    col = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -1200, -300, parameter_name="ScanColor", texture=white)
    rgh = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -1200, 200, parameter_name="ScanRough", texture=white,
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    nrm = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -1200, 600, parameter_name="ScanNormal", texture=flatn,
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    for t in (col, rgh, nrm):
        link(uvs, "", t, "UVs")
    detail = expr(m, unreal.MaterialExpressionScalarParameter, -1200, -450, parameter_name="Detail", default_value=0.6)
    gain = expr(m, unreal.MaterialExpressionScalarParameter, -1200, -550, parameter_name="ScanGain", default_value=2.0)
    # Base: Tint x lerp(1, luminance(scan) x gain, Detail)
    lum = expr(m, unreal.MaterialExpressionDotProduct, -900, -300)
    w = expr(m, unreal.MaterialExpressionConstant3Vector, -1100, -150, constant=unreal.LinearColor(0.3, 0.59, 0.11, 1.0))
    link(col, "RGB", lum, "A")
    link(w, "", lum, "B")
    lg = expr(m, unreal.MaterialExpressionMultiply, -750, -300)
    link(lum, "", lg, "A")
    link(gain, "", lg, "B")
    ld = expr(m, unreal.MaterialExpressionLinearInterpolate, -600, -300, const_a=1.0)
    link(lg, "", ld, "B")
    link(detail, "", ld, "Alpha")
    tint = expr(m, unreal.MaterialExpressionVectorParameter, -600, -500, parameter_name="Tint", default_value=unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    base = expr(m, unreal.MaterialExpressionMultiply, -350, -350)
    link(tint, "", base, "A")
    link(ld, "", base, "B")
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # Roughness: Rough x lerp(1, scan x 2, Detail), clamped.
    r2 = expr(m, unreal.MaterialExpressionMultiply, -900, 200, const_b=2.0)
    link(rgh, "R", r2, "A")
    rd = expr(m, unreal.MaterialExpressionLinearInterpolate, -700, 200, const_a=1.0)
    link(r2, "", rd, "B")
    link(detail, "", rd, "Alpha")
    rough = expr(m, unreal.MaterialExpressionScalarParameter, -700, 350, parameter_name="Rough", default_value=0.5)
    rm = expr(m, unreal.MaterialExpressionMultiply, -500, 200)
    link(rough, "", rm, "A")
    link(rd, "", rm, "B")
    rc = expr(m, unreal.MaterialExpressionClamp, -350, 200, min_default=0.03, max_default=1.0)
    link(rm, "", rc, "")
    mel.connect_material_property(rc, "", unreal.MaterialProperty.MP_ROUGHNESS)
    metal = expr(m, unreal.MaterialExpressionScalarParameter, -350, 400, parameter_name="Metal", default_value=0.0)
    mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    # Normal: the scan's, flattened toward up by (1 - Detail) so a weak Detail is a weak dent.
    fl = expr(m, unreal.MaterialExpressionLinearInterpolate, -700, 600, const_b=0.0)
    flat_v = expr(m, unreal.MaterialExpressionConstant3Vector, -900, 750, constant=unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    link(flat_v, "", fl, "A")
    link(nrm, "RGB", fl, "B")
    link(detail, "", fl, "Alpha")
    mel.connect_material_property(fl, "", unreal.MaterialProperty.MP_NORMAL)
    glow = expr(m, unreal.MaterialExpressionScalarParameter, -350, 550, parameter_name="Glow", default_value=0.0)
    em = expr(m, unreal.MaterialExpressionMultiply, -200, 550)
    link(tint, "", em, "A")
    link(glow, "", em, "B")
    mel.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def glass_master():
    m = new_asset("M_PropGlass", MATS, unreal.Material, unreal.MaterialFactoryNew())
    try_set(m, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    try_set(m, "translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
    tint = expr(m, unreal.MaterialExpressionVectorParameter, -500, 0, parameter_name="Tint", default_value=unreal.LinearColor(0.85, 0.9, 0.9, 1.0))
    mel.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(m, unreal.MaterialExpressionScalarParameter, -500, 150, parameter_name="Rough", default_value=0.03)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    op = expr(m, unreal.MaterialExpressionScalarParameter, -500, 300, parameter_name="Opacity", default_value=0.25)
    mel.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    spec = expr(m, unreal.MaterialExpressionConstant, -500, 400, r=1.0)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def import_texture(path, dest, name, normal=False, srgb=True):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", path)
    t.set_editor_property("destination_path", dest)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", False)
    asset_tools.import_asset_tasks([t])
    tex = unreal.load_asset(dest + "/" + name)
    if not tex:
        warn("couldn't import " + path)
        return None
    tex.set_editor_property("srgb", srgb and not normal)
    if normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        try_set(tex, "flip_green_channel", True)   # Poly Haven's nor_gl is OpenGL's
    eal.save_loaded_asset(tex)
    return tex


def scan_textures(spec):
    """{scan id: {color, rough, normal}} for the 'scans' Tools/fetch_props.py has downloaded."""
    out = {}
    for sid in sorted(set(spec["scans"].values())):
        folder = os.path.join(FETCHED, "_scans", sid)
        dest = MATS + "/Scans/" + sid
        maps = {}
        for name in ("color", "rough", "normal"):
            asset = "%s/T_%s_%s" % (dest, sid, name)
            f = os.path.join(folder, name + ".jpg")
            if eal.does_asset_exist(asset):
                maps[name] = unreal.load_asset(asset)
            elif os.path.exists(f):
                maps[name] = import_texture(f, dest, "T_%s_%s" % (sid, name), normal=name == "normal", srgb=name == "color")
        if maps:
            out[sid] = maps
        else:
            warn("scan %s not downloaded (python Tools/fetch_props.py): its props stay plain" % sid)
    return out


def materials(spec):
    """MI_Prop_<key> for every key in materials.json."""
    m_master = master()
    m_glass = glass_master()
    scans = scan_textures(spec)
    out = {}
    for key, s in spec["materials"].items():
        glass = s["base"] == "glass"
        mi = new_asset("MI_Prop_" + key, MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mi.set_editor_property("parent", m_glass if glass else m_master)
        c = s.get("tint", [0.5, 0.5, 0.5])
        mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(c[0], c[1], c[2], 1.0))
        mel.set_material_instance_scalar_parameter_value(mi, "Rough", s.get("rough", 0.5))
        if glass:
            mel.set_material_instance_scalar_parameter_value(mi, "Opacity", s.get("opacity", 0.25))
        else:
            mel.set_material_instance_scalar_parameter_value(mi, "Metal", s.get("metal", 0.0))
            mel.set_material_instance_scalar_parameter_value(mi, "Tile", s.get("tile", 1.0))
            if s.get("emissive"):
                mel.set_material_instance_scalar_parameter_value(mi, "Glow", s["emissive"])
            sid = spec["scans"].get(s["base"])
            maps = scans.get(sid, {})
            for name, param in (("color", "ScanColor"), ("rough", "ScanRough"), ("normal", "ScanNormal")):
                if maps.get(name):
                    mel.set_material_instance_texture_parameter_value(mi, param, maps[name])
            if not maps:
                mel.set_material_instance_scalar_parameter_value(mi, "Detail", 0.0)
            elif s["base"] == "emissive":
                mel.set_material_instance_scalar_parameter_value(mi, "Detail", 0.0)
        eal.save_loaded_asset(mi)
        out[key] = mi
    log("%d prop materials (%d scans)" % (len(out), len(scans)))
    return out


# --- Meshes -----------------------------------------------------------------------------------------

def import_mesh(path, pid):
    dest = "%s/%s" % (DEST, pid)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", path)
    t.set_editor_property("destination_path", dest)
    t.set_editor_property("destination_name", "SM_" + pid)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    asset_tools.import_asset_tasks([t])
    mesh = None
    for p in t.get_editor_property("imported_object_paths") or []:
        a = unreal.load_asset(p)
        if isinstance(a, unreal.StaticMesh):
            mesh = a
            break
    if mesh is None:
        for p in eal.list_assets(dest, recursive=True):
            a = unreal.load_asset(p)
            if isinstance(a, unreal.StaticMesh):
                mesh = a
                break
    if mesh is None:
        raise RuntimeError("no static mesh imported from " + path)
    if mesh.get_path_name().split(".")[0] != mesh_path(pid):
        # Some importer versions name the mesh after the glTF node: give it its fixed name.
        eal.rename_asset(mesh.get_path_name().split(".")[0], mesh_path(pid))
        mesh = unreal.load_asset(mesh_path(pid))
    try:
        ns = mesh.get_editor_property("nanite_settings")
        ns.set_editor_property("enabled", False)
        unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).set_nanite_settings(mesh, ns, True)
    except Exception as e:
        warn("%s: couldn't turn Nanite off (%s)" % (pid, e))
    return mesh


def mesh_path(pid):
    return "%s/%s/SM_%s" % (DEST, pid, pid)


def give_materials(mesh, mats):
    """A Blender prop's slots are named by materials.json keys: put the matching instance in each."""
    slots = mesh.get_editor_property("static_materials")
    done = 0
    for i, s in enumerate(slots):
        name = str(s.get_editor_property("material_slot_name"))
        mi = mats.get(name)
        if mi is None:   # importers sometimes suffix slot names (paint_red_001, MI_paint_red...)
            for key in sorted(mats, key=len, reverse=True):
                if key in name:
                    mi = mats[key]
                    break
        if mi is None:
            warn("%s: slot %s has no prop material" % (mesh.get_name(), name))
            continue
        mesh.set_material(i, mi)
        done += 1
    eal.save_loaded_asset(mesh)
    return done


def import_all(force=False):
    props = json.load(open(os.path.join(DATA, "props.json")))["props"]
    spec = json.load(open(os.path.join(DATA, "materials.json")))
    mats = materials(spec)
    got, missing = 0, []
    for pid, p in sorted(props.items()):
        if p["source"] == "blender":
            src = os.path.join(DATA, "Blender", pid + ".glb")
        elif p["source"] == "polyhaven":
            src = os.path.join(FETCHED, p["polyhaven"], p["polyhaven"] + ".gltf")
        else:
            continue
        if not os.path.exists(src):
            missing.append(pid)
            continue
        if not force and eal.does_asset_exist(mesh_path(pid)):
            mesh = unreal.load_asset(mesh_path(pid))
        else:
            try:
                mesh = import_mesh(src, pid)
            except Exception as e:
                warn("%s: %s" % (pid, e))
                missing.append(pid)
                continue
        if p["source"] == "blender":
            give_materials(mesh, mats)
        got += 1
    log("%d props in %s%s" % (got, DEST, "; not there: " + ", ".join(missing) if missing else ""))
    if missing and any(props[m]["source"] == "polyhaven" for m in missing):
        warn("run python Tools/fetch_props.py for the Poly Haven ones")


# --- Dressing the shed (called by Tools/ue_build_shed.py at the end of its build) -----------------------

def importer_frame():
    """How the glTF importer lays a prop down: (extra yaw, mirror x), so that a prop's right (+x in
    Torque Shed's frame) lands on UE +x and its front (+z) on UE +y at yaw 0. Read from two props
    that are lopsided on purpose: the whiteboard is all in front of its wall (z 0 .. +0.07) and the
    mobile bench's handle sticks out on its right (+x). Assumes UE's usual (x, z, y) if they're missing."""
    import math
    def lopsided(pid, thin):
        if not eal.does_asset_exist(mesh_path(pid)):
            return None
        b = unreal.load_asset(mesh_path(pid)).get_bounding_box()
        lo, hi = (b.min.x, b.min.y), (b.max.x, b.max.y)
        axes = []
        for k in (0, 1):
            ext = hi[k] - lo[k]
            sign = 1.0 if hi[k] > -lo[k] else -1.0
            axes.append((ext, k, sign))
        ext, k, sign = (min if thin else max)(axes)
        v = [0.0, 0.0]
        v[k] = sign
        return v
    front = lopsided("whiteboard", True)
    right = lopsided("bench_mobile", False)
    if not front or not right or abs(front[0] * right[0] + front[1] * right[1]) > 0.5:
        warn("couldn't read the importer's frame from the props: assuming front +y, right +x")
        return 0.0, False
    mirror = right[0] * front[1] - right[1] * front[0] < 0.0
    if mirror:
        right, front = [-right[0], right[1]], [-front[0], front[1]]
    alpha = math.degrees(math.atan2(right[1], right[0]))
    log("importer frame: right %s, front %s -> yaw %+.0f%s" % (right, front, -alpha, ", mirrored" if mirror else ""))
    return -alpha, mirror


def dress():
    """Swaps the shed's grey boxes for the real models (Data/Props/swaps.json) and adds the dressing.
    The boxes stay as invisible colliders. Returns how many models went in."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    spec = json.load(open(os.path.join(DATA, "swaps.json")))
    level = actors.get_all_level_actors()
    placed, skipped = 0, []
    extra_yaw, mirror = importer_frame()

    def spawn(entry, folder):
        mesh = unreal.load_asset(mesh_path(entry["prop"])) if eal.does_asset_exist(mesh_path(entry["prop"])) else None
        if mesh is None:
            skipped.append(entry["prop"])
            return None
        x, y, z = entry["at"]
        a = actors.spawn_actor_from_object(mesh, unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, entry.get("yaw", 0.0) + extra_yaw))
        if mirror:
            a.set_actor_scale3d(unreal.Vector(-1.0, 1.0, 1.0))
        a.set_actor_label("Prop_" + entry["prop"])
        a.set_folder_path(folder)
        c = a.get_component_by_class(unreal.StaticMeshComponent)
        c.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        c.set_collision_profile_name("NoCollision")
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        return a

    for s in spec["swaps"]:
        if spawn(s, "Props") is None:
            continue
        hidden = 0
        for a in level:
            label = a.get_actor_label()
            if any(label.startswith(h[:-1]) if h.endswith("*") else label == h for h in s["hide"]):
                a.set_actor_hidden_in_game(True)
                try:
                    a.set_is_temporarily_hidden_in_editor(True)
                except Exception:
                    pass
                c = a.get_component_by_class(unreal.StaticMeshComponent)
                if c:
                    c.set_editor_property("visible", False)
                    c.set_editor_property("cast_shadow", False)
                hidden += 1
        if hidden == 0:
            warn("swap %s hid no boxes (labels %s): check the shed's labels" % (s["prop"], s["hide"]))
        placed += 1
    for d in spec.get("dress", []):
        if spawn(d, "Props/Dressing") is not None:
            placed += 1
    log("dressed the shed: %d models%s" % (placed, "; not imported yet: " + ", ".join(sorted(set(skipped))) if skipped else ""))
    return placed


if __name__ == "__main__":
    import_all(force="all" in sys.argv[1:] or any("all" == a for a in " ".join(sys.argv[1:]).split()))
