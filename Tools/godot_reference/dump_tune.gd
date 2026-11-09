extends Node
## Dumps the original game's dyno desk tune (BikeBuild's tune_block, tune_range, tune_default,
## tune_value, aid_strength, _tune_engine and what a tune does to the riding numbers, and the desk's
## readings: TuningComputer.value_text, aids_note, _ecu_text) for a fixed set of bikes and tunes, for
## the Unreal port's check (SimTest/tune_test). Run by Tools/make_godot_reference.py --tune.

const KEYS := ["map", "ignition", "limiter", "f_preload", "f_bump", "f_rebound", "r_preload", "r_bump", "r_rebound",
	"damper", "psi_f", "psi_r", "chain", "sprocket_f", "sprocket_r", "spread", "aid_abs", "aid_traction",
	"aid_lean_traction", "aid_stability", "aid_anti_wheelie"]
## Each style's reference bike, and these swaps on the classic and the sport bike.
const VARIANTS := {
	"safety": {"ecu": "ecu_safety"},
	"race": {"ecu": "race_ecu", "fork": "usd_sport", "shock": "sport_mono", "gearbox": "six_close", "steering_damper": "damper_linear_adj"},
	"remap": {"ecu": "remap_ecu", "fork": "enduro_usd", "shock": "twin_shock_gas"},
	"sportecu": {"ecu": "sport_ecu", "steering_damper": "damper_friction"},
}
const TUNES := [
	{},
	{"map": 0.2, "ignition": 0.15, "limiter": 600.0},
	{"map": -0.8, "ignition": 0.9, "limiter": -1500.0},
	{"f_preload": 0.5, "f_bump": -0.6, "f_rebound": 1.0, "r_preload": -1.0, "r_bump": 0.3, "r_rebound": -0.4, "damper": 0.7},
	{"psi_f": 1.7, "psi_r": 3.0, "sprocket_f": 14.0, "sprocket_r": 50.0, "spread": -0.6},
	{"aid_traction": 1.0, "aid_abs": 0.0, "aid_anti_wheelie": 5.0, "aid_lean_traction": 2.0, "aid_stability": 4.0},
	{"aid_traction": 0.0, "psi_f": 5.0, "sprocket_r": 99.0, "spread": 1.0, "map": 1.0, "ignition": -1.0},
]
## A condition with the chain let out and two parts lightened, used with the second tune.
const CONDITION := {"chain": {"slack": 55.0}, "frame": {"lightened": true}, "front_wheel": {"lightened": true}, "exhaust": {"lightened": true}}


func v3(v: Vector3) -> Array:
	return [v.x, v.y, v.z]


func tyre_out(p: Dictionary) -> Dictionary:
	var o := {}
	for k in ["kappa_peak", "alpha_peak", "relax"]:
		o[k] = p.get(k)
	return o


func case_of(parts: Dictionary, tune: Dictionary, condition: Dictionary) -> Dictionary:
	var b := BikeBuild.new(parts, condition, tune)
	var desk := TuningComputer.new()
	var keys := {}
	for k in KEYS:
		var r := b.tune_range(k)
		var v := b.tune_value(k) if k != "chain" else b.chain_slack
		keys[k] = {"block": b.tune_block(k), "lo": r.x, "hi": r.y, "default": b.tune_default(k), "value": v,
			"text": TuningComputer.value_text(k, v, b)}
	var aids := {}
	for aid in ["abs", "traction", "lean_traction", "anti_wheelie", "stability"]:
		aids[aid] = {"has": b.has_aid(aid), "level": b.aid_level(aid), "strength": b.aid_strength(aid)}
	var torque := []
	var rpm := []
	for i in 25:
		var x := lerpf(b.idle_rpm, b.redline, float(i) / 24.0)
		rpm.append(x)
		torque.append(b.engine_torque(x))
	var out := {
		"parts": b.parts, "tune": tune, "condition": condition, "keys": keys, "aids": aids,
		"aids_note": TuningComputer.aids_note(b), "ecu_map": desk._ecu_text(b, "map"), "ecu_limiter": desk._ecu_text(b, "limiter"),
		"redline": b.redline, "stock_redline": b.stock_redline, "peak_torque": b.peak_torque, "engine_stress": b.engine_stress,
		"torque_rpm": rpm, "torque_nm": torque, "notes": b.notes,
		"bike_mass": b.bike_mass, "com_height": b.com_height, "inertia_add": v3(b.inertia_add),
		"front_preload": b.front_preload, "front_bump": b.front_bump, "front_rebound": b.front_rebound,
		"rear_preload": b.rear_preload, "rear_bump": b.rear_bump, "rear_rebound": b.rear_rebound,
		"damper_damping": b.damper_damping, "damper_friction": b.damper_friction,
		"grip_front": b.grip_front, "grip_rear": b.grip_rear, "rolling": b.rolling,
		"tyre_front": tyre_out(b.tyre_front), "tyre_rear": tyre_out(b.tyre_rear),
		"gear_ratios": b.gear_ratios, "final_drive": b.final_drive, "sprocket_r_front": b.sprocket_r_front, "sprocket_r_rear": b.sprocket_r_rear,
		"drivetrain_efficiency": b.drivetrain_efficiency,
		"abs_front": b.abs_on(true), "abs_rear": b.abs_on(false), "traction": b.aid_strength("traction"),
		"anti_wheelie": b.aid_strength("anti_wheelie"), "lean_traction": b.aid_strength("lean_traction"), "stability": b.aid_strength("stability"),
	}
	desk.free()
	return out


func _ready() -> void:
	var out_path := "user://tune.json"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out_path = a.trim_prefix("--out=")
	var cases := {}
	var bikes := {}
	for style in PartsDB.STYLES:
		bikes[style] = PartsDB.sanitize_build(Contracts.style_build(style))
	for v in VARIANTS:
		for base in ["classic", "sport"]:
			var parts: Dictionary = Contracts.style_build(base)
			for slot in VARIANTS[v]:
				parts[slot] = VARIANTS[v][slot]
			bikes["%s+%s" % [base, v]] = PartsDB.sanitize_build(parts)
	for name in bikes:
		for i in TUNES.size():
			cases["%s/tune%d" % [name, i]] = case_of(bikes[name], TUNES[i], {})
		cases["%s/worked" % name] = case_of(bikes[name], TUNES[1], CONDITION)
	var f := FileAccess.open(out_path, FileAccess.WRITE)
	f.store_string(JSON.stringify({"cases": cases}, " "))
	f.close()
	print("dumped %d tune cases to %s" % [cases.size(), out_path])
	get_tree().quit()
