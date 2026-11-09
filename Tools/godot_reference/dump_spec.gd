extends Node
## Dumps the original game's riding numbers (BikeBuild: chassis, wheels and tyres, suspension,
## brakes, gearbox, mass spread, rider aids, the engine's torque) for every style's reference bike
## and for a sweep of single-part swaps on them, as new parts with no tune, for the Unreal port's
## check (SimTest/spec_test). Run by Tools/make_godot_reference.py --spec.

const SWEEP_KINDS := ["frame", "fork", "shock", "wheel", "tyre", "disc", "caliper", "pads", "master", "handlebars",
	"saddle", "tank", "engine", "gearbox", "final_drive", "chain", "ecu", "fairing", "panniers", "top_box",
	"steering_damper", "exhaust", "head", "mirrors", "tank_bag", "aux_tank"]


func v3(v: Vector3) -> Array:
	return [v.x, v.y, v.z]


func tyre_out(p: Dictionary) -> Dictionary:
	var o := {}
	for k in ["construction", "profile", "stiffness", "slide", "kappa_peak", "alpha_peak", "camber", "relax", "load_sens", "crown", "C", "B"]:
		o[k] = p.get(k)
	return o


func spec_of(parts: Dictionary) -> Dictionary:
	var b := BikeBuild.new(parts)
	var torque := []
	var rpm := []
	for i in 25:
		var r := lerpf(b.idle_rpm, b.redline, float(i) / 24.0)
		rpm.append(r)
		torque.append(b.engine_torque(r))
	return {
		"parts": b.parts,
		"bike_mass": b.bike_mass, "total_mass": b.total_mass, "wheelbase": b.wheelbase, "com_height": b.com_height,
		"com_offset": v3(b.com_offset), "inertia_add": v3(b.inertia_add), "rake_deg": b.rake_deg, "seat_height": b.seat_height,
		"cda": b.cda, "steer_rate": b.steer_rate, "lean_clearance": b.lean_clearance, "flick": b.flick,
		"front_travel": b.front_travel, "front_spring": b.front_spring, "front_bump": b.front_bump, "front_rebound": b.front_rebound,
		"front_progression": b.front_progression, "front_preload": b.front_preload, "front_knee": b.front_knee,
		"rear_travel": b.rear_travel, "rear_spring": b.rear_spring, "rear_bump": b.rear_bump, "rear_rebound": b.rear_rebound,
		"rear_progression": b.rear_progression, "rear_preload": b.rear_preload, "rear_knee": b.rear_knee,
		"fork_path": v3(b.fork_path), "fork_link": b.fork_link, "swing_pivot": v3(b.swing_pivot), "swing_length": b.swing_length,
		"sprocket_r_front": b.sprocket_r_front, "sprocket_r_rear": b.sprocket_r_rear,
		"front_radius": b.front_radius, "rear_radius": b.rear_radius, "front_inertia": b.front_inertia, "rear_inertia": b.rear_inertia,
		"tyre_front": tyre_out(b.tyre_front), "tyre_rear": tyre_out(b.tyre_rear), "grip_front": b.grip_front, "grip_rear": b.grip_rear,
		"rolling": b.rolling, "fork_offset": b.fork_offset, "trail": b.trail, "steer_mass": b.steer_mass, "steer_inertia": b.steer_inertia,
		"damper_damping": b.damper_damping, "damper_friction": b.damper_friction,
		"front_brake_torque": b.front_brake_torque, "rear_brake_torque": b.rear_brake_torque,
		"abs_front": b.abs_on(true), "abs_rear": b.abs_on(false), "traction": b.aid_strength("traction"),
		"primary": b.primary, "gear_ratios": b.gear_ratios, "final_drive": b.final_drive, "shift_time": b.shift_time,
		"drivetrain_efficiency": b.drivetrain_efficiency,
		"idle_rpm": b.idle_rpm, "redline": b.redline, "peak_torque": b.peak_torque, "peak_torque_rpm": b.peak_torque_rpm,
		"torque_rpm": rpm, "torque_nm": torque,
	}


func _ready() -> void:
	var out_path := "user://spec.json"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out_path = a.trim_prefix("--out=")
	var out := {"styles": {}, "sweep": {}}
	for style in PartsDB.STYLES:
		out["styles"][style] = spec_of(PartsDB.sanitize_build(Contracts.style_build(style)))
	# Each part of the kinds that change the ride, on the reference bike of its first style.
	for kind in SWEEP_KINDS:
		for id in PartsDB.by_kind.get(kind, []):
			var styles: Array = PartsDB.styles_of(id)
			var base: String = str(styles[0]) if not styles.is_empty() and str(styles[0]) in PartsDB.STYLES else "classic"
			for slot in PartsDB.slots_for_kind(kind):
				var parts: Dictionary = Contracts.style_build(base)
				parts[slot] = id
				out["sweep"]["%s/%s" % [slot, id]] = spec_of(PartsDB.sanitize_build(parts))
	var f := FileAccess.open(out_path, FileAccess.WRITE)
	f.store_string(JSON.stringify(out, " "))
	f.close()
	print("dumped %d styles and %d swaps to %s" % [out["styles"].size(), out["sweep"].size(), out_path])
	get_tree().quit()
