extends Node
## Renders Torque Shed's synthesised sounds to 16-bit mono WAVs, exactly as the Godot game makes
## them (SfxSynth.make, the same seeds and loudness), for the Unreal port's TorqueSfx.
##   godot --headless --path <torque-shed copy> res://ts_audio/render.tscn -- --out=<dir>
## Writes <out>/synth/<sound>_<v>.wav (44.1 kHz), <out>/traffic/<petrol|diesel>.wav (22.05 kHz)
## and <out>/synth.json: every sound, its variants, and whether it loops.

func _ready() -> void:
	var out := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.trim_prefix("--out=")
	if out == "":
		push_error("render.gd: --out=<dir> is needed")
		get_tree().quit(1)
		return
	DirAccess.make_dir_recursive_absolute(out + "/synth")
	DirAccess.make_dir_recursive_absolute(out + "/traffic")
	var list := {}
	var n := 0
	for sound in SfxSynth.all_sounds():
		var files := []
		for v in SfxSynth.variant_count(sound):
			var d := SfxSynth.make(sound, v)
			var name := "%s_%d" % [sound, v]
			_write(out + "/synth/" + name + ".wav", _pcm(d), SfxSynth.RATE)
			files.append(name)
			n += 1
		list[sound] = {"files": files, "loop": SfxSynth.is_loop(sound)}
	for which in ["petrol", "diesel"]:
		var wav: AudioStreamWAV = TrafficSounds.make_loop(which)
		# make_loop pads the loop's start onto its end for Godot's resampler: the loop is loop_end long.
		_write(out + "/traffic/" + which + ".wav", wav.data.slice(0, wav.loop_end * 2), TrafficSounds.RATE)
	var f := FileAccess.open(out + "/synth.json", FileAccess.WRITE)
	f.store_string(JSON.stringify({"rate": SfxSynth.RATE, "sounds": list, "godot": Engine.get_version_info()["string"]}, " ", true))
	f.close()
	print("render.gd: %d synthesised takes" % n)
	get_tree().quit(0)


static func _pcm(d: PackedFloat32Array) -> PackedByteArray:
	var b := PackedByteArray()
	b.resize(d.size() * 2)
	for i in d.size():
		b.encode_s16(i * 2, clampi(roundi(d[i] * 32767.0), -32768, 32767))
	return b


static func _write(path: String, pcm: PackedByteArray, rate: int) -> void:
	var h := PackedByteArray()
	h.resize(44)
	h.encode_u32(0, 0x46464952)        # RIFF
	h.encode_u32(4, 36 + pcm.size())
	h.encode_u32(8, 0x45564157)        # WAVE
	h.encode_u32(12, 0x20746d66)       # "fmt "
	h.encode_u32(16, 16)
	h.encode_u16(20, 1)                # PCM
	h.encode_u16(22, 1)                # mono
	h.encode_u32(24, rate)
	h.encode_u32(28, rate * 2)
	h.encode_u16(32, 2)
	h.encode_u16(34, 16)
	h.encode_u32(36, 0x61746164)       # data
	h.encode_u32(40, pcm.size())
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_buffer(h)
	f.store_buffer(pcm)
	f.close()
