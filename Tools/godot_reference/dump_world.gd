extends Node
## Dumps the original game's answers about the real world on its own test fixtures (two Dales cells and the map
## tiles along Gunnerside to Grinton), for the Unreal port's check (SimTest/world_test): the grid and map-tile
## maths, each height tile decoded, the two cells stitched into one region, and the ground's height at a spread of
## points. Run by Tools/make_world_reference.py.

const CELLS := "res://tests/fixtures/cells"
const HEIGHTS := "res://tests/fixtures/heights"
const AREA := "area:swaledale"
const PINS := [Vector2(-2422, -4363), Vector2(7187, -4599)]

var _seed := 24680


func rnd() -> float:
	_seed = (_seed * 1103515245 + 12345) % 2147483648
	return float(_seed) / 2147483648.0


func num(v: float) -> Variant:
	return null if is_nan(v) else v


func sha(bytes: PackedByteArray) -> String:
	var ctx := HashingContext.new()
	ctx.start(HashingContext.HASH_SHA256)
	ctx.update(bytes)
	return ctx.finish().hex_encode()


func _ready() -> void:
	var path := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			path = a.trim_prefix("--out=")
	RealRegion.cell_dirs = [CELLS]
	HeightTiles.dirs = [HEIGHTS]
	HeightTiles.offline = true
	HeightTiles.reset()
	var out := {}

	# The grid and the map tiles' maths.
	var geo := {"metres": [], "cell_of": [], "mercator": [], "zoom_for": [], "frame": []}
	for lat: float in [-60.0, -12.5, 0.0, 35.7, 54.375, 70.2]:
		var m := RealTerrain.metres_per_degree(lat)
		(geo["metres"] as Array).append([lat, m.x, m.y])
		for sp: float in [4.0, 32.0, 128.0]:
			(geo["zoom_for"] as Array).append([lat, sp, HeightTiles.zoom_for(lat, sp)])
	for i in 40:
		var lat := lerpf(-80.0, 80.0, rnd())
		var lon := lerpf(-179.9, 179.9, rnd())
		var c := RealRegion.cell_of(lat, lon)
		var mid := RealRegion.cell_middle(c)
		(geo["cell_of"] as Array).append([lat, lon, c.x, c.y, mid.x, mid.y])
		for zm: int in [9, 11, 14]:
			var my := HeightTiles.mercator_y(lat, zm)
			(geo["mercator"] as Array).append([lat, lon, zm, HeightTiles.mercator_x(lon, zm), my, HeightTiles.lat_of(my, zm)])
	out["geo"] = geo

	# Every fixture tile decoded: its size and what its pixels hash to, plain and with the pits filled.
	var tiles: Array = []
	for z in DirAccess.get_directories_at(HEIGHTS):
		for x in DirAccess.get_directories_at(HEIGHTS.path_join(z)):
			for fn in DirAccess.get_files_at(HEIGHTS.path_join(z).path_join(x)):
				if not fn.ends_with(".webp") and not fn.ends_with(".png"):
					continue
				var bytes := FileAccess.get_file_as_bytes(HEIGHTS.path_join(z).path_join(x).path_join(fn))
				var key := Vector3i(int(z), int(x), int(fn.get_basename()))
				var t := HeightTiles.decode(bytes, key, false)
				var t2 := HeightTiles.decode(bytes, key, true)
				tiles.append({"z": key.x, "x": key.y, "y": key.z, "ext": fn.get_extension(), "size": t.size, "rgb": sha(t.rgb),
					"despiked": sha(t2.rgb), "sea": not t.sea.is_empty(), "sea_mask": sha(t.sea) if not t.sea.is_empty() else "", "at": [t.at(0, 0), t.at(t.size - 1, 0), t.at(17, 401), t.at(t.size - 1, t.size - 1)]})
	out["tiles"] = tiles

	# The two cells stitched.
	var r := RealRegion.get_region(AREA)
	var st := {"id": r.id, "hash": r.hash, "frame": [r.frame_lat, r.frame_lon], "nodes": r.node_count(), "edges": r.edge_count(),
		"bounds": [r.bounds.position.x, r.bounds.position.y, r.bounds.end.x, r.bounds.end.y], "country": r.country, "sea": r.sea,
		"names": r.names.size(), "bans": r.bans.size(), "buildings": r.buildings.size(), "pois": r.pois.size(),
		"landmarks": r.landmarks.size(), "places": r.places.size(), "components": r.component_size.size(),
		"classes": r.classes, "surfaces": r.surfaces, "credits": r.credits}
	var sx := 0.0
	var sz := 0.0
	for p in r.node_pos:
		sx += p.x
		sz += p.y
	var sl := 0.0
	for l in r.edge_len:
		sl += l
	st["node_sum"] = [sx, sz]
	st["length_sum"] = sl
	var some_nodes: Array = []
	for i in range(0, r.node_count(), 97):
		some_nodes.append([i, r.node_pos[i].x, r.node_pos[i].y, r.adjacent[i].size(), r.component[i]])
	st["some_nodes"] = some_nodes
	var some_edges: Array = []
	for ei in range(0, r.edge_count(), 89):
		var line := r.edge_line[ei]
		some_edges.append([ei, r.edge_a[ei], r.edge_b[ei], r.edge_class[ei], r.edge_surface[ei], r.edge_limit[ei], r.edge_oneway[ei],
			r.name_of(ei), r.edge_len[ei], line.size(), line[line.size() / 2].x, line[line.size() / 2].y])
	st["some_edges"] = some_edges
	var some_buildings: Array = []
	for i in range(0, r.buildings.size(), 331):
		some_buildings.append([i] + (r.buildings[i] as Array))
	st["some_buildings"] = some_buildings
	st["some_places"] = r.places.slice(0, 6)
	var near: Array = []
	for i in 40:
		var x := lerpf(r.bounds.position.x, r.bounds.end.x, rnd())
		var z := lerpf(r.bounds.position.y, r.bounds.end.y, rnd())
		near.append([x, z, r.nearest_node(x, z), r.nearest_connected(x, z)])
	near.append([PINS[0].x, PINS[0].y, r.nearest_node(PINS[0].x, PINS[0].y), r.nearest_connected(PINS[0].x, PINS[0].y)])
	near.append([PINS[1].x, PINS[1].y, r.nearest_node(PINS[1].x, PINS[1].y), r.nearest_connected(PINS[1].x, PINS[1].y)])
	st["nearest"] = near
	var bans: Array = []
	for k: Vector3i in r.bans:
		bans.append([k.x, k.y, k.z])
	bans.sort()
	st["some_bans"] = bans.slice(0, 12)
	out["stitch"] = st

	# The ground: any tile on disk (the map's own view), with the buildings' pads and without.
	var heights: Array = []
	var terrain := r.terrain
	for i in 2500:
		var x := lerpf(-3600.0, 8600.0, rnd())
		var z := lerpf(-6600.0, -2800.0, rnd())
		heights.append([x, z, num(terrain.height(x, z)), num(terrain.height_at_level(0, x, z)), num(terrain.height_at_level(1, x, z)),
			num(terrain.height_at_level(2, x, z))])
	# (and close by buildings, where the pads level the ground)
	for i in range(0, r.buildings.size(), 13):
		var b: Array = r.buildings[i]
		var x := float(b[0]) + lerpf(-12.0, 12.0, rnd())
		var z := float(b[1]) + lerpf(-12.0, 12.0, rnd())
		var h := terrain.height(x, z)
		if not is_nan(h):
			heights.append([x, z, h, num(terrain.height_at_level(0, x, z)), num(terrain.height_at_level(1, x, z)), num(terrain.height_at_level(2, x, z))])
	out["zooms"] = Array(terrain.zooms)
	out["heights"] = heights

	# A ride's own view: only the tiles within reach of its road, eased into the coarser level where they end.
	var road := PackedVector2Array()
	var n := int(PINS[0].distance_to(PINS[1]) / 30.0)
	for i in n + 1:
		road.append(PINS[0].lerp(PINS[1], float(i) / n))
	var ride := terrain.for_road(road)
	var ride_out := {"road": [PINS[0].x, PINS[0].y, PINS[1].x, PINS[1].y, n], "keys": [], "heights": [], "ready": [], "boxes": []}
	for k in ride.used_keys():
		(ride_out["keys"] as Array).append([k.x, k.y, k.z])
		if (ride_out["boxes"] as Array).size() < 6:
			var bx := ride.key_box(k)
			(ride_out["boxes"] as Array).append([k.x, k.y, k.z, bx.position.x, bx.position.y, bx.end.x, bx.end.y])
	for i in 2500:
		var x := lerpf(-4200.0, 9000.0, rnd())
		var z := lerpf(-7000.0, -2200.0, rnd())
		(ride_out["heights"] as Array).append([x, z, num(ride.height(x, z)), ride.sea_at(x, z)])
	for i in 30:
		var x := lerpf(-4200.0, 9000.0, rnd())
		var z := lerpf(-7000.0, -2200.0, rnd())
		var w := lerpf(50.0, 900.0, rnd())
		(ride_out["ready"] as Array).append([x, z, w, ride.ready(Rect2(x, z, w, w), 0), ride.ready(Rect2(x, z, w, w), 1), ride.ready(Rect2(x, z, w, w), 2)])
	out["ride"] = ride_out

	# The rest of what the store holds for the two cells (the store's own files, res://dump/store, copied in by
	# Tools/make_godot_reference.py): their hash as the baker makes it, bridges and tunnels, streams, scenery, trees
	# and cover.
	var store := "res://dump/store"
	var extras := {"hash_of_dir": [RegionStore.hash_of_dir(CELLS.path_join("c577_711")), RegionStore.hash_of_dir(CELLS.path_join("c577_712"))]}
	var frame := Vector2(r.frame_lat, r.frame_lon)
	var feats := {"points": [], "lines": [], "areas": []}
	var spans: Array = []
	var streams: Array = []
	var sketch: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(store + "/sketch/b28_35.json"))
	for c: String in ["577_711", "577_712"]:
		spans.append_array(RealRegion._spans_in(sketch["spans"][c], frame))
		streams.append_array(RealRegion._streams_in(JSON.parse_string(FileAccess.get_file_as_string(store + "/stream/c%s.json" % c))["streams"], frame))
		RealRegion.add_features(feats, JSON.parse_string(FileAccess.get_file_as_string(store + "/feature/c%s.json" % c)), frame)
		terrain.add_trees(JSON.parse_string(FileAccess.get_file_as_string(store + "/tree/c%s.json" % c)))
	extras["spans"] = spans.size()
	extras["some_spans"] = []
	for i in range(0, spans.size(), 7):
		(extras["some_spans"] as Array).append([i] + (spans[i] as Array))
	extras["streams"] = streams.size()
	extras["some_streams"] = []
	var stream_points := 0
	for i in streams.size():
		var pts: PackedVector2Array = streams[i][1]
		stream_points += pts.size()
		if i % 53 == 0:
			(extras["some_streams"] as Array).append([i, streams[i][0], pts.size(), pts[0].x, pts[0].y, pts[pts.size() - 1].x, pts[pts.size() - 1].y])
	extras["stream_points"] = stream_points
	extras["points"] = (feats["points"] as Array).size()
	extras["lines"] = (feats["lines"] as Array).size()
	extras["areas"] = (feats["areas"] as Array).size()
	extras["looks"] = (feats.get("buildings", []) as Array).size()
	extras["some_points"] = []
	for i in range(0, (feats["points"] as Array).size(), 211):
		(extras["some_points"] as Array).append([i] + (feats["points"][i] as Array))
	extras["some_lines"] = []
	for i in range(0, (feats["lines"] as Array).size(), 397):
		var l: Array = feats["lines"][i]
		var pts: PackedVector2Array = l[1]
		(extras["some_lines"] as Array).append([i, l[0], pts.size(), pts[0].x, pts[0].y, pts[pts.size() - 1].x, pts[pts.size() - 1].y, l[2], l[3], l[4], l[5], l[6]])
	extras["some_areas"] = []
	for i in (feats["areas"] as Array).size():
		var a: Array = feats["areas"][i]
		if i % 37 != 0 and a.size() < 7:
			continue
		var pts: PackedVector2Array = a[1]
		(extras["some_areas"] as Array).append([i, a[0], pts.size(), pts[0].x, pts[0].y, pts[pts.size() - 1].x, pts[pts.size() - 1].y, a[2], a[3], a[4], a[5],
			str(a[6]["group"]) if a.size() > 6 else ""])
	extras["some_looks"] = []
	for i in range(0, (feats.get("buildings", []) as Array).size(), 9):
		(extras["some_looks"] as Array).append([i] + (feats["buildings"][i] as Array))
	var land: Array = []
	for i in 2500:
		var x := lerpf(r.bounds.position.x, r.bounds.end.x, rnd())
		var z := lerpf(r.bounds.position.y, r.bounds.end.y, rnd())
		land.append([x, z, terrain.cover(x, z), terrain.trees_at(x, z)])
	extras["land"] = land
	out["extras"] = extras

	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(JSON.stringify(out, "", false, true))
	f.close()
	get_tree().quit()
