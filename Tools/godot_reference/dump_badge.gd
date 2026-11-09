extends Node
## Dumps the original game's tank badge (BikeBadge) for the Unreal port's check
## (SimTest/badge_test): each logo's picture (the coverage of each 8 x 8 block of its pixels), and
## a few badges stamped on a made-up tank (an egg of a tank with a tunnel inside it, its triangles
## dumped too): where each picture goes, the brand's picture's size, and each decal's skin. Run by
## Tools/make_godot_reference.py --badge.

const BADGES := [
	{"logo": "wings", "logo_color": Color(0.9, 0.8, 0.2)},
	{"brand": "Torque", "brand_color": Color(1, 1, 1)},
	{"logo": "star", "logo_color": Color(1, 0, 0), "brand": "Kestrel", "brand_color": Color(0, 0, 1), "brand_font": "bebas",
		"logo_size": 1.4, "logo_x": 0.1, "logo_y": -0.05, "brand_size": 0.8, "brand_x": -0.05, "brand_y": 0.1},
	{"logo": "roundel", "brand": "Morgan's script", "brand_font": "lobster", "brand_size": 1.6, "brand_y": 0.2, "logo_x": -0.1},
]


## An egg of a tank (centre c, half sizes r) as triangles, `n` around and `m` along.
func egg(c: Vector3, r: Vector3, n: int, m: int) -> PackedVector3Array:
	var out := PackedVector3Array()
	var p := func(i: int, j: int) -> Vector3:
		var lat := PI * j / m - PI / 2.0
		var lon := TAU * i / n
		return c + Vector3(cos(lat) * cos(lon) * r.x, cos(lat) * sin(lon) * r.y, sin(lat) * r.z)
	for j in m:
		for i in n:
			var a: Vector3 = p.call(i, j)
			var b: Vector3 = p.call(i + 1, j)
			var cc: Vector3 = p.call(i + 1, j + 1)
			var d: Vector3 = p.call(i, j + 1)
			out.append_array([a, b, cc, a, cc, d])
	return out


func _ready() -> void:
	var out_path := "user://badge.json"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out_path = a.substr(6)
	var out := {"logos": {}, "badges": []}
	for id in BikeBadge.LOGOS:
		var img: Image = BikeBadge.texture(str(id)).get_image()
		var blocks := []
		for by in 32:
			for bx in 32:
				var s := 0.0
				for y in 8:
					for x in 8:
						s += img.get_pixel(bx * 8 + x, by * 8 + y).a
				blocks.append(snappedf(s, 0.001))
		out["logos"][id] = {"w": img.get_width(), "h": img.get_height(), "blocks": blocks}
	var faces := egg(Vector3(0.02, 0.86, -0.25), Vector3(0.15, 0.11, 0.3), 24, 16)
	faces.append_array(egg(Vector3(0.02, 0.8, -0.25), Vector3(0.07, 0.05, 0.26), 12, 8))
	var flat := []
	for p in faces:
		flat.append_array([p.x, p.y, p.z])
	out["faces"] = flat
	var box := AABB(faces[0], Vector3.ZERO)
	for p in faces:
		box = box.expand(p)
	var parent := Node3D.new()
	add_child(parent)
	for b in BADGES:
		var entry := {"badge": b.duplicate(), "clean": {}}
		for k in BikeBadge.clean(b):
			var v = BikeBadge.clean(b)[k]
			entry["clean"][k] = [v.r, v.g, v.b] if v is Color else v
		for k in b:
			if b[k] is Color:
				entry["badge"][k] = [b[k].r, b[k].g, b[k].b]
		var cb := BikeBadge.clean(b)
		if cb.has("brand"):
			var tex := BikeBadge.text_texture(BikeBadge.brand_text(cb), str(cb.get("brand_font", "plain")))
			entry["brand_px"] = [tex.get_width(), tex.get_height()]
			entry["brand_text"] = BikeBadge.brand_text(cb)
		var nodes := []
		for n in BikeBadge.stamp(parent, box, b, faces):
			var mi := n as MeshInstance3D
			var arr := mi.mesh.surface_get_arrays(0)
			var pos := []
			for v in arr[Mesh.ARRAY_VERTEX]:
				pos.append_array([v.x, v.y, v.z])
			var nrm := []
			for v in arr[Mesh.ARRAY_NORMAL]:
				nrm.append_array([v.x, v.y, v.z])
			var uv := []
			for v in arr[Mesh.ARRAY_TEX_UV]:
				uv.append_array([v.x, v.y])
			var r: Rect2 = mi.get_meta("rect")
			nodes.append({"name": str(mi.name), "rect": [r.position.x, r.position.y, r.size.x, r.size.y], "pos": pos, "nrm": nrm, "uv": uv})
			mi.queue_free()
		entry["nodes"] = nodes
		out["badges"].append(entry)
	var f := FileAccess.open(out_path, FileAccess.WRITE)
	f.store_string(JSON.stringify(out))
	f.close()
	get_tree().quit()
