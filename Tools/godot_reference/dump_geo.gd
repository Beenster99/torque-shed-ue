extends Node
## Dumps the original game's geometry-mode maths (GeoGizmo's offsets and drags, BikeRig.clean_adjust)
## on a fixed set of inputs, for the Unreal port's check (SimTest/geo_test). Each case keeps its
## inputs and Godot's answer. Run by Tools/make_godot_reference.py --geo.

var _seed := 12345


func rnd() -> float:
	# A small LCG, so the cases are the same every run (and need no Godot RNG to reproduce).
	_seed = (_seed * 1103515245 + 12345) % 2147483648
	return float(_seed) / 2147483648.0


func rv(lo: float, hi: float) -> Vector3:
	return Vector3(lerpf(lo, hi, rnd()), lerpf(lo, hi, rnd()), lerpf(lo, hi, rnd()))


func v3(v: Vector3) -> Array:
	return [v.x, v.y, v.z]


func off_out(o: Dictionary) -> Dictionary:
	var f := GeoGizmo.offset_of({"x": o}, "x")
	return {"p": v3(f["p"]), "r": v3(f["r"]), "s": v3(f["s"])}


func basis_out(b: Basis) -> Array:
	return [v3(b.x), v3(b.y), v3(b.z)]


func rand_off() -> Dictionary:
	return {"p": rv(-0.3, 0.3), "r": rv(-170.0, 170.0), "s": rv(0.5, 1.8)}


func _ready() -> void:
	var out := {"euler": [], "moved": [], "rotated": [], "scaled": [], "xform": [], "sanitize": [], "line": [], "plane": [], "angle": [], "slots": PartsDB.SLOTS}
	for i in 60:
		var e := rv(-179.0, 179.0)
		if i % 10 == 0:
			e.x = 90.0 if i % 20 == 0 else -90.0   # the gimbal-locked ends
		var b := Basis.from_euler(e * (PI / 180.0))
		out["euler"].append({"deg": v3(e), "basis": basis_out(b), "back": v3(b.get_euler() * (180.0 / PI))})
	for i in 60:
		var st := rand_off()
		var axis := i % 3
		var amt := lerpf(-1.0, 1.0, rnd()) * (2.0 if i % 7 == 0 else 0.2)
		var snap := i % 2 == 1
		out["moved"].append({"start": off_out(st), "axis": axis, "amount": amt, "snap": snap, "out": off_out(GeoGizmo.moved(st, axis, amt, snap))})
	for i in 60:
		var st := rand_off() if i % 4 != 0 else GeoGizmo.identity()
		var axis := i % 3
		var deg := lerpf(-200.0, 200.0, rnd())
		var snap := i % 2 == 1
		out["rotated"].append({"start": off_out(st), "axis": axis, "deg": deg, "snap": snap, "out": off_out(GeoGizmo.rotated(st, axis, deg, snap))})
	for i in 60:
		var st := rand_off()
		var axis := i % 4
		var f := lerpf(0.02, 3.0, rnd())
		var snap := i % 2 == 1
		out["scaled"].append({"start": off_out(st), "axis": axis, "factor": f, "snap": snap, "out": off_out(GeoGizmo.scaled(st, axis, f, snap))})
	for i in 30:
		var st := rand_off()
		var pivot := rv(-1.0, 1.0)
		var t := GeoGizmo.offset_xform(st, pivot)
		out["xform"].append({"off": off_out(st), "pivot": v3(pivot), "basis": basis_out(t.basis), "origin": v3(t.origin)})
	# Sanitize: in range and out of it, unknown slots, the rig's handles, parts left as dropped.
	var cases := [
		{"handlebars": {"p": Vector3(0.01, 0.02, -0.03), "r": Vector3(10, 0, 0), "s": Vector3.ONE}},
		{"tank": {"p": Vector3(3.0, -2.0, 0.5), "r": Vector3(190.0, -370.0, 45.0), "s": Vector3(0.01, 9.0, 1.0)}},
		{"not_a_slot": {"p": Vector3(0.1, 0, 0)}, "seat": {"p": Vector3(0.1, 0, 0)}, "saddle": {"p": Vector3(0.0, 0.05, 0.0)}},
		{"exhaust": {"p": Vector3.ZERO, "r": Vector3.ZERO, "s": Vector3.ONE}},
		{"rig": {"tank_up": 45.0, "pegs": -12.5, "hoop_up": 0.00001, "hoop_deg": -9.0, "nonsense": 3.0}},
		{"rig": {"tank_up": 0.0}, "mirrors": {"r": Vector3(0, 0, 179.5)}},
		{"fork": {"s": Vector3(1.0, 1.0, 1.2)}, "rear_wheel": {"p": Vector3(0, 0, 0.0000001)}},
	]
	for c in cases:
		var inp := {}
		for k in c:
			if k == "rig":
				inp[k] = c[k]
			else:
				inp[k] = {}
				for kk in c[k]:
					inp[k][kk] = v3(c[k][kk])
		var s := GeoGizmo.sanitize(c)
		var so := {}
		for k in s:
			so[k] = s[k] if k == "rig" else off_out(s[k])
		out["sanitize"].append({"in": inp, "out": so})
	for i in 40:
		var o := rv(-1.0, 1.0)
		var d := rv(-1.0, 1.0)
		var ro := rv(-3.0, 3.0)
		var rd := rv(-1.0, 1.0)
		if i == 0:
			rd = d   # looking straight down the axis
		out["line"].append({"origin": v3(o), "dir": v3(d), "ro": v3(ro), "rd": v3(rd), "t": GeoGizmo.line_param(o, d, ro, rd)})
		var hit = GeoGizmo.plane_point(o, d, ro, rd)
		out["plane"].append({"center": v3(o), "axis": v3(d), "ro": v3(ro), "rd": v3(rd), "hit": v3(hit) if hit is Vector3 else null})
		var a := rv(-1.0, 1.0)
		var b2 := rv(-1.0, 1.0)
		out["angle"].append({"a": v3(a), "b": v3(b2), "axis": v3(d), "deg": GeoGizmo.angle_about(a, b2, d)})
	var path := "user://geo_ref.json"
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--out="):
			path = arg.substr(6)
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(JSON.stringify(out))
	f.close()
	get_tree().quit()
