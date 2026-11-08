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

func _ready() -> void:
	await get_tree().process_frame
	var out_path := "user://godot_reference.json"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out_path = a.trim_prefix("--out=")
	var out := {}
	for style in PartsDB.STYLES:
		var parts: Dictionary = PartsDB.sanitize_build(Contracts.style_build(style))
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
				meshes.append({"name": str((mi as Node).name), "min": v3(mlo), "max": v3(mhi)})
			if n > 0:
				slots[slot] = {"min": v3(lo), "max": v3(hi), "verts": n, "meshes": meshes}
		out[style] = {"parts": parts, "stats": {"wheelbase": build.wheelbase, "rake_deg": build.rake_deg, "fork_length": build.fork_length,
			"seat_height": build.seat_height, "front_radius": build.front_radius, "rear_radius": build.rear_radius, "rear_travel": build.rear_travel},
			"rig": conv(rig), "slots": slots}
		m.queue_free()
		await get_tree().process_frame
	var f := FileAccess.open(out_path, FileAccess.WRITE)
	f.store_string(JSON.stringify(out, " "))
	f.close()
	print("dumped %d styles to %s" % [out.size(), out_path])
	get_tree().quit()
