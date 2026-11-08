extends Node
## Dumps the original game's reference bikes for the Unreal port's checks: per style its build,
## its rig solve, and every slot's drawn meshes (model space, Godot frame) as bounds and a hash.

func v3(v: Vector3) -> Array:
	return [v.x, v.y, v.z]

func conv(x):
	if x is Vector3:
		return v3(x)
	if x is PackedVector3Array or (x is Array and not x.is_empty() and x[0] is Vector3):
		return Array(x).map(func(p): return v3(p))
	if x is AABB:
		return {"min": v3(x.position), "max": v3(x.end)}
	if x is Dictionary:
		var o := {}
		for k in x:
			o[str(k)] = conv(x[k])
		return o
	if x is Array:
		return x.map(func(e): return conv(e))
	if x is float or x is int or x is String or x is bool:
		return x
	return str(x)

const SWEEP_KINDS := ["wheel", "tyre", "disc", "caliper", "pads", "fork", "shock", "swingarm", "final_drive", "chain",
	"engine", "head", "spark_plugs", "fuel", "air_filter", "oil_filter", "cooling", "gearbox", "exhaust", "handlebars", "dashboard", "master", "headlight", "mirrors", "indicators", "front_mudguard", "frame", "fairing", "tail", "saddle", "tank", "side_stand", "centre_stand", "rearsets", "horn", "ecu", "side_panels", "guards", "panniers", "top_box", "number_boards", "fog_lights", "steering_damper", "lever_guards", "aux_tank", "satnav", "tank_bag", "toolbox"]


func dump_build(parts: Dictionary) -> Dictionary:
	var build := BikeBuild.new(parts)
	var rig := BikeRig.solve(build)
	var m := BikeModel.new()
	m.look = BikeLook.NEW
	add_child(m)
	m.build_from(build)
	var slots := {}
	for slot in m._slot_meshes:
		var lo := Vector3(INF, INF, INF)
		var hi := -lo
		var n := 0
		var meshes := []
		for mi in m._slot_meshes[slot]:
			if not (mi is MeshInstance3D) or (mi as MeshInstance3D).mesh == null:
				continue
			var xf: Transform3D = m._local_xform(mi)
			var mesh := (mi as MeshInstance3D).mesh
			var mlo := Vector3(INF, INF, INF)
			var mhi := -mlo
			for k in mesh.get_surface_count():
				for v: Vector3 in mesh.surface_get_arrays(k)[Mesh.ARRAY_VERTEX]:
					var p := xf * v
					lo = lo.min(p)
					hi = hi.max(p)
					mlo = mlo.min(p)
					mhi = mhi.max(p)
					n += 1
			# (A Blender part's surface keeps its glb material, named by its tag: "chrome", "paint__engine".)
			var gm: Material = mesh.surface_get_material(0) if mesh.get_surface_count() > 0 else null
			var tag := gm.resource_name.get_slice(".", 0) if gm != null else ""
			meshes.append({"name": str((mi as Node).name), "tag": tag, "min": v3(mlo), "max": v3(mhi)})
		if n > 0:
			slots[slot] = {"min": v3(lo), "max": v3(hi), "verts": n, "meshes": meshes}
	var out := {"parts": parts, "stats": {"wheelbase": build.wheelbase, "rake_deg": build.rake_deg, "fork_length": build.fork_length,
		"seat_height": build.seat_height, "front_radius": build.front_radius, "rear_radius": build.rear_radius, "rear_travel": build.rear_travel},
		"rig": conv(rig), "slots": slots}
	m.queue_free()
	await get_tree().process_frame
	return out


func _ready() -> void:
	await get_tree().process_frame
	var out_path := "user://godot_reference.json"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out_path = a.trim_prefix("--out=")
	var out := {}
	for style in PartsDB.STYLES:
		out[style] = await dump_build(PartsDB.sanitize_build(Contracts.style_build(style)))
	# The running gear swept part by part: each part on the reference bike of its first style
	# (else the classic's), so every glb gets placed at least once.
	var sweep := {}
	for kind in SWEEP_KINDS:
		for id in PartsDB.by_kind[kind]:
			var styles: Array = PartsDB.styles_of(id)
			var base: String = str(styles[0]) if not styles.is_empty() and str(styles[0]) in PartsDB.STYLES else "classic"
			for slot in PartsDB.slots_for_kind(kind):
				var parts: Dictionary = Contracts.style_build(base)
				parts[slot] = id
				var d := await dump_build(PartsDB.sanitize_build(parts))
				d["base"] = base
				sweep["%s/%s" % [slot, id]] = d
	out["sweep"] = sweep
	var f := FileAccess.open(out_path, FileAccess.WRITE)
	f.store_string(JSON.stringify(out, " "))
	f.close()
	print("dumped %d styles to %s" % [out.size(), out_path])
	get_tree().quit()
