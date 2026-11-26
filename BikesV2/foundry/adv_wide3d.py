"""The wide adventure cockpit (beenster, Nov 24-25, adventure fairing rounds 1-5; signed off Nov 25 10:16 "this is good
now. Please make sure it is added to the game"). Reference bikes: a Multistrada (beenster's own riding photo), Versys,
R 1300 GS, for proportions only.

From the saddle an adventure bike looked too narrow. The shape itself is in the model's data (adv.nose with shoulders
that spread at grip height and a tail that tapers back onto the tank, a tank wide only at the front, ytab, the screen
line); this pass adds what the shell builder can't draw, from model["adv"]["wide"]:

  bar_dw        wider bars: the bar tube stretched outboard of the clamps, everything on the bar ends moved out with it
  dash          a bigger TFT (scale, lift, fwd) set in a dark cockpit tray ("panel": rows of x, z, half width, sag)
  drop          parts the wide face does without (the deflector wings, the old painted lamp eyes)
  paint         the side panels in the bike's colour: the side fairing is one tailored unit
  headlights    a real lamp each side (fairing_lamps3d.strip): its own part behind an opening cut in the nose, wide
                at the back and narrow at the front, an LED bar along its top and lenses underneath
  tank_lines    character lines along the tank (a narrow raised fold, then a crisp edge on its crest)
  vee           the black V on the nose: a second material on the nose itself, cut on its exact edge (no extra part)
  screen_point  the screen's lower edge comes down to a point on the centre line, onto the top of the V
  radar         a flat forward-facing radar plate set a few mm back in a pocket above the beak
  crisp         the fairing's creases become real edges of a few mm radius, not soft rolls

    adv_wide3d.run(detail3d, model, c)      # called at the end of detail3d.run when the model has adv.wide
"""
import math

import bmesh
import bpy
from mathutils import Vector


def _game(D):
    """The game export (build3d --game): the same shapes with lighter meshes (crisp edges as one chamfer, the tank
    cut finer only once), so the five adventure bikes don't cost the frame rate (beenster: performance first)."""
    return D.B.TYRE_LOD.get("lod") == "lod1"


def _bbox(B, ob):
    bb = [ob.matrix_world @ Vector(q) for q in ob.bound_box]
    return [(min(getattr(q, a) for q in bb) / B.MM, max(getattr(q, a) for q in bb) / B.MM) for a in "xyz"]


def widen_bars(D, dw):
    """The bar is stretched outboard of the clamps; the grips, levers, switches, guards and mirrors move out with it."""
    B = D.B
    bpy.context.view_layer.update()
    for ob in list(bpy.data.objects):
        if ob.type not in ("MESH", "CURVE") or ob.name.startswith(("adv", "dash", "screen")):
            continue
        (x0, x1), (y0, y1), (z0, z1) = _bbox(B, ob)
        if ob.name == "bar" and ob.type == "MESH":
            for v in ob.data.vertices:
                w = ob.matrix_world @ v.co
                k = max(0.0, min(1.0, (abs(w.y) / B.MM - 70) / 130.0))
                w.y += math.copysign(dw * k * B.MM, w.y)
                v.co = ob.matrix_world.inverted() @ w
            continue
        yc = (y0 + y1) / 2
        if 1080 < x0 and x1 < 1340 and z0 > 1080 and abs(yc) > 150:
            ob.location.y += math.copysign(dw * B.MM, yc)
    bpy.context.view_layer.update()


def big_dash(D, spec):
    """A bigger TFT, set in a dark tray that runs from behind it out to the fairing's inner flanks."""
    B = D.B
    k = spec.get("scale", 1.2)
    bpy.context.view_layer.update()
    parts = [o for o in bpy.data.objects if o.name.startswith("dash") and not o.name.startswith("dashstay")]
    if parts:
        bbs = [_bbox(B, o) for o in parts]
        cx = (min(b[0][0] for b in bbs) + max(b[0][1] for b in bbs)) / 2
        cz = (min(b[2][0] for b in bbs) + max(b[2][1] for b in bbs)) / 2
        piv = Vector((cx * B.MM, 0, cz * B.MM))
        move = Vector((spec.get("fwd", 0) * B.MM, 0, spec.get("lift", 0) * B.MM))
        for o in parts:
            o.location = piv + (o.location - piv) * k + move
            o.scale = o.scale * k
        g = D.GAUGES.get("dashglass")
        if g:      # the game's lit page follows the glass
            at = Vector(g["at"]) * B.MM
            at = piv + (at - piv) * k + move
            g["at"] = [round(at.x / B.MM, 1), 0.0, round(at.z / B.MM, 1)]
            g["w"], g["h"] = round(g["w"] * k, 1), round(g["h"] * k, 1)
    P = spec.get("panel")
    if P:
        nu = 24
        verts, faces = [], []
        for (x, z, hw, sag) in P:
            for j in range(nu + 1):
                t = -1 + 2 * j / nu
                verts.append(B.V(x - sag * 0.5 * t * t, t * hw, z + sag * t * t))
        for i in range(len(P) - 1):
            for j in range(nu):
                a = i * (nu + 1) + j
                faces.append((a, a + 1, a + nu + 2, a + nu + 1))
        ob = B.mesh_obj("advcockpit", verts, faces, B.mat("plastic"), True)
        md = ob.modifiers.new("t", "SOLIDIFY")
        md.thickness, md.offset = 4 * B.MM, 0
    bpy.context.view_layer.update()


def tank_lines(D, lines, crisp):
    """Character lines on the tank: a narrow raised fold along each line on the tank's flanks, then a crisp
    few-mm edge on its crest, the same treatment as the fairing (beenster, round 5)."""
    import shapes
    B = D.B
    tk = bpy.data.objects.get("tank")
    if tk is None or tk.type != "MESH":
        return
    for md in list(tk.modifiers):
        if md.type == "SUBSURF":
            tk.modifiers.remove(md)
    bm = bmesh.new()
    bm.from_mesh(tk.data)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=1 if _game(D) else 2, use_grid_fill=True)
    bm.to_mesh(tk.data)
    bm.free()
    tk.data.update()
    mw = tk.matrix_world
    inv, nm = mw.inverted(), mw.to_3x3()
    for v in tk.data.vertices:
        w = mw @ v.co
        n = (nm @ v.normal).normalized()
        x, z = w.x / B.MM, w.z / B.MM
        side = max(0.0, min(1.0, (abs(n.y) - 0.35) / 0.35))
        if side <= 0:
            continue
        dy = 0.0
        for c in lines:
            xs = [q[0] for q in c["pts"]]
            if x < xs[0] - 40 or x > xs[-1] + 40:
                continue
            fade = min(1.0, (x - xs[0] + 40) / 60.0, (xs[-1] + 40 - x) / 60.0)
            d = abs(z - shapes.lerp_table(c["pts"], x))
            if d < c["w"]:
                dy += c["A"] * (1 - d / c["w"]) * fade
        if dy:
            w.y += math.copysign(dy * B.MM, w.y)
            v.co = inv @ w
    tk.data.update()
    if crisp:
        tk.data.set_sharp_from_angle(angle=math.radians(crisp["angle"]))
        bv = tk.modifiers.new("crisp", "BEVEL")
        bv.limit_method, bv.angle_limit = "ANGLE", math.radians(crisp["angle"])
        bv.width, bv.segments = crisp["width"] * B.MM, 1 if _game(D) else 3
        bv.harden_normals = not _game(D)


def vee(D, spec, name="advnose"):
    """The black V: the nose's faces inside |y| < (z - apex) * slope, ahead of xmin, take a second (satin black)
    material, after the nose is cut along the V's exact edges, so the line is crisp whatever the mesh."""
    B = D.B
    nose = bpy.data.objects.get(name)
    if nose is None:
        return
    apex, (zt, yt), xmin = spec["apex"], spec["top"], spec["xmin"]
    k = yt / (zt - apex)
    mw = nose.matrix_world
    inv = mw.inverted()
    bm = bmesh.new()
    bm.from_mesh(nose.data)
    p0 = inv @ Vector((xmin * B.MM, 0, apex * B.MM))
    nrm = inv.to_3x3()
    for n_ in (Vector((0, 1, -k)), Vector((0, -1, -k)), Vector((1, 0, 0))):
        geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
        bmesh.ops.bisect_plane(bm, geom=geom, dist=0.00001, plane_co=p0, plane_no=(nrm @ n_).normalized())
    black = D.surf("advvee", (0.03, 0.032, 0.036), 0.0, 0.42, wear=0.0)
    if black.name not in [m.name for m in nose.data.materials if m]:
        nose.data.materials.append(black)
    mi = [m.name if m else "" for m in nose.data.materials].index(black.name)
    n_in = 0
    for f in bm.faces:
        c = mw @ f.calc_center_median()
        y, z, x = c.y / B.MM, c.z / B.MM, c.x / B.MM
        if x > xmin and abs(y) < (z - apex) * k:
            f.material_index = mi
            n_in += 1
    bm.to_mesh(nose.data)
    bm.free()
    nose.data.update()
    print("vee faces", n_in)


def screen_point(D, spec):
    """The screen's lower edge comes down to a point on the centre line, lying just off the nose."""
    B = D.B
    scr = bpy.data.objects.get("advscreen")
    if scr is None:
        return
    bm = bmesh.new()
    bm.from_mesh(scr.data)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=1 if _game(D) else 3, use_grid_fill=True)
    bm.to_mesh(scr.data)
    bm.free()
    trn = D._trees(("advnose",))
    mw = scr.matrix_world
    inv = mw.inverted()
    zb = min((mw @ v.co).z / B.MM for v in scr.data.vertices)
    for v in scr.data.vertices:
        w = mw @ v.co
        y, z = w.y / B.MM, w.z / B.MM
        k = max(0.0, 1 - (z - zb) / spec["band"]) * max(0.0, 1 - abs(y) / spec["half"])
        if k <= 0:
            continue
        k = k * k * (3 - 2 * k)
        z2 = z - spec["drop"] * k
        xs_ = D._cast(trn, y, z2, "x", 1)
        x2 = w.x / B.MM
        if xs_ is not None:
            x2 = max(x2 + (xs_ + spec["gap"] - x2) * k, xs_ + spec["gap"])
        v.co = inv @ Vector((x2 * B.MM, w.y, z2 * B.MM))
    scr.data.update()


def _apply_all(ob):
    """Bake an object's modifiers into its mesh (so a boolean cuts the finished shell, not its bare surface)."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ev)
    old = ob.data
    ob.modifiers.clear()
    ob.data = me
    if old.users == 0:
        bpy.data.meshes.remove(old)


def radar(D, spec, parts=("advmask", "advnose", "advbeak")):
    """A flat radar plate square to the front, its face a few mm back in a pocket cut into the bodywork above the
    beak (beenster, round 4: facing forwards, slightly recessed, clear of the nose and the beak). Returns the
    number of faces of the plate that still touch the bodywork (0)."""
    B = D.B
    x0, y_, (za, zb), dp, pk = spec["x"], spec["y"], spec["z"], spec["depth"], spec["pocket"]
    bpy.ops.mesh.primitive_cube_add(size=1, location=B.V((x0 - dp - 6 + x0 + 100) / 2, 0, (za + zb) / 2))
    cut = bpy.context.object
    cut.name = "cut_radar"
    cut.scale = ((dp + 106) * B.MM, 2 * (y_ + pk) * B.MM, (zb - za + 2 * pk) * B.MM)
    bpy.context.view_layer.update()
    for tn in parts:
        t_ = bpy.data.objects.get(tn)
        if t_ is None:
            continue
        _apply_all(t_)
        bo = t_.modifiers.new("pocket", "BOOLEAN")
        bo.operation, bo.object, bo.solver = "DIFFERENCE", cut, "EXACT"
        _apply_all(t_)
    bpy.data.objects.remove(cut, do_unlink=True)
    bpy.ops.mesh.primitive_cube_add(size=1, location=B.V(x0 - dp / 2, 0, (za + zb) / 2))
    u = bpy.context.object
    u.name = "advradar"
    u.scale = (dp * B.MM, 2 * y_ * B.MM, (zb - za) * B.MM)
    bv = u.modifiers.new("b", "BEVEL")
    bv.width, bv.segments = 2.5 * B.MM, 2
    u.data.materials.append(D.surf("radar", (0.035, 0.036, 0.04), 0.0, 0.5, wear=0.0))
    bpy.ops.mesh.primitive_cube_add(size=1, location=B.V(x0 - dp - 12, 0, (za + zb) / 2))
    bk = bpy.context.object
    bk.name = "advradarback"
    bk.scale = (12 * B.MM, 2 * (y_ + pk) * B.MM, (zb - za + 2 * pk) * B.MM)
    bk.data.materials.append(D.surf("radarpocket", (0.01, 0.01, 0.012), 0.0, 0.7, wear=0.0))
    bpy.context.view_layer.update()
    from mathutils.bvhtree import BVHTree
    dg = bpy.context.evaluated_depsgraph_get()

    def tree(o):
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        t = BVHTree.FromPolygons([ev.matrix_world @ v.co for v in me.vertices], [tuple(p.vertices) for p in me.polygons])
        ev.to_mesh_clear()
        return t
    rt = tree(u)
    hits = sum(len(rt.overlap(tree(bpy.data.objects[tn]))) for tn in parts if bpy.data.objects.get(tn))
    print("radar clash", hits)
    return hits


def crisp(D, spec):
    """Every crease sharper than the angle becomes a real edge with a few-mm radius (beenster, Nov 25: "an edge of
    radius in millimetres", not all round)."""
    B = D.B
    for o in list(bpy.data.objects):
        if o.type == "MESH" and o.name.startswith(tuple(spec["objs"])):
            o.data.set_sharp_from_angle(angle=math.radians(spec["angle"]))
            bv = o.modifiers.new("crisp", "BEVEL")
            bv.limit_method, bv.angle_limit = "ANGLE", math.radians(spec["angle"])
            bv.width, bv.segments, bv.profile = spec["width"] * B.MM, 1 if _game(D) else 3, 0.5
            bv.harden_normals = not _game(D)


def tidy(D, prefixes=("advnose", "advmask", "advbeak", "advgrey", "advblack", "tank")):
    """Bake the panels' modifiers and clear the slivers the opening cuts, the V's cut lines and the radar pocket
    leave behind: merged points, collapsed faces, consistent winding, and any face still shading against its own
    direction drawn flat (build 172: dark triangles on the nose paint in the game, from slivers whose smoothed
    normals pointed into the panel). Returns the corners still facing the wrong way (0)."""
    left = 0
    for o in list(bpy.data.objects):
        if o.type != "MESH" or o.name != "tank" and not o.name.startswith(tuple(p for p in prefixes if p != "tank")):
            continue
        _apply_all(o)
        if _game(D):
            # thinned here, before the clean-up, and left alone by the export's own thinning (its collapse made new
            # folded slivers on these cut panels)
            n_ = sum(len(p.vertices) - 2 for p in o.data.polygons)
            if n_ > 2500 and o.name == "tank":
                bpy.context.view_layer.objects.active = o
                md = o.modifiers.new("thin", "DECIMATE")
                md.ratio = D.B.GAME_THIN[3]
                _apply_all(o)
            o["no_thin"] = True
        if _game(D) and o.data.has_custom_normals:
            # the game gets plain normals from the shape itself: a crease above the angle is an edge, the rest
            # smooth (custom normals carried through the thinning came out bent: build 172's dark triangles)
            bpy.context.view_layer.objects.active = o
            with bpy.context.temp_override(object=o, active_object=o):
                bpy.ops.mesh.customdata_custom_splitnormals_clear()
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.00005)
        bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=0.0002)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.to_mesh(o.data)
        bm.free()
        me = o.data
        if _game(D):
            me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
            me.set_sharp_from_angle(angle=math.radians(30))
        cn = me.corner_normals
        bad = [p for p in me.polygons if any(cn[li].vector.dot(p.normal) < 0.0 for li in p.loop_indices)]
        if bad:
            # those faces shade flat: their corners take the face's own direction
            ns = [cn[i].vector.copy() for i in range(len(me.loops))]
            for p in bad:
                for li in p.loop_indices:
                    ns[li] = p.normal.copy()
            me.normals_split_custom_set(ns)
            me.update()
            cn = me.corner_normals
            left += sum(1 for p in me.polygons for li in p.loop_indices if cn[li].vector.dot(p.normal) < 0.0)
    print("tidy: corners shading the wrong way", left)
    return left


def run(D, model, c):
    W = (model.get("adv") or {}).get("wide")
    if not W:
        return
    B = D.B
    if W.get("bar_dw"):
        widen_bars(D, W["bar_dw"])
    if W.get("dash"):
        big_dash(D, W["dash"])
    if W.get("drop"):
        D.drop(*W["drop"])
    if W.get("paint"):
        paint = bpy.data.objects["tank"].data.materials[0]
        for o in bpy.data.objects:
            if o.type == "MESH" and o.name.startswith(tuple(W["paint"])):
                for i in range(len(o.data.materials)):
                    o.data.materials[i] = paint
    bpy.context.view_layer.update()
    if W.get("headlights"):
        import fairing_lamps3d
        print("headlights:", fairing_lamps3d.strip(D, W["headlights"]))
    if W.get("tank_lines"):
        tank_lines(D, W["tank_lines"], W.get("tank_crisp"))
    if W.get("vee"):
        vee(D, W["vee"])
    if W.get("screen_point"):
        screen_point(D, W["screen_point"])
    if W.get("radar"):
        radar(D, W["radar"])
    if W.get("crisp"):
        crisp(D, W["crisp"])
    bpy.context.view_layer.update()
    tidy(D)
    bpy.context.view_layer.update()
