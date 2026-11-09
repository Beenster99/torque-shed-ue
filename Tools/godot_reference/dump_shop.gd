extends Node
## Dumps the original game's New bike configurator (BikeShop, the Bike shop tab's "New: configure
## your own") for the Unreal port's check (SimTest/shop_test): each style's stock build as the
## configurator starts it, the parts every configurator slot offers on it with how each goes on
## (BikeRig.part_fit), and a fixed walk of part changes through it (BikeShopTab.set_part: the part
## in, then sanitize_build and fit_tyres), with what fabrication each build takes beyond the
## style's stock one, the brief and the mudguard colour group. Run by
## Tools/make_godot_reference.py --shop.

const STEPS := 12


func options_of(style: String, parts: Dictionary) -> Dictionary:
	var out := {}
	for slot in BikeShop.CONFIG_SLOTS:
		var rows := []
		for id in BikeShop.options(slot, style, parts):
			rows.append([str(id), str(BikeRig.part_fit(parts, slot, str(id))["tier"])])
		out[slot] = rows
	return out


func state_of(style: String, parts: Dictionary) -> Dictionary:
	return {"parts": parts.duplicate(), "fab": BikeShop.fab_work(parts, style).map(func(w): return str(w["what"])),
		"brief": BikeRig.brief_text(parts), "mudguards": BikeShop.colour_slots("mudguards", parts),
		"model": Sales.model_text(parts)}


func _ready() -> void:
	var out_path := "user://shop.json"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out_path = a.substr(6)
	# (The catalogue's kinds in Godot's order, so the check can lay its parts out the same way.)
	var kinds := {}
	for k in PartsDB.by_kind:
		kinds[k] = PartsDB.by_kind[k].map(func(id): return str(id))
	# (And the styles each part suits, which this repo's parts overlay changes for a few.)
	var suits := {}
	for id in PartsDB.parts:
		if not PartsDB.styles_of(str(id)).is_empty():
			suits[str(id)] = PartsDB.styles_of(str(id))
	var out := {"slots": BikeShop.CONFIG_SLOTS, "by_kind": kinds, "styles_of": suits, "styles": {}}
	var n := BikeShop.CONFIG_SLOTS.size()
	var si := 0
	for style in PartsDB.STYLES:
		var parts := BikeShop.fit_tyres(PartsDB.sanitize_build(Contracts.style_build(style).duplicate()), style)
		var entry := {"name": PartsDB.style_name(style), "stock": state_of(style, parts), "options": options_of(style, parts), "walk": []}
		for k in STEPS:
			var slot: String = BikeShop.CONFIG_SLOTS[(si * 7 + k * 3) % n]
			var opts := BikeShop.options(slot, style, parts)
			if opts.is_empty():
				continue
			# The k'th option along that goes on (the configurator greys out the ones that won't).
			var pick := ""
			for j in opts.size():
				var id := str(opts[(si * 3 + k * 2 + 1 + j) % opts.size()])
				if str(BikeRig.part_fit(parts, slot, id)["tier"]) != BikeRig.TIER_NO:
					pick = id
					break
			if pick == "":
				continue
			parts[slot] = pick
			parts = BikeShop.fit_tyres(PartsDB.sanitize_build(parts), style)
			var st := state_of(style, parts)
			st["slot"] = slot
			st["id"] = pick
			st["options"] = options_of(style, parts)
			entry["walk"].append(st)
		out["styles"][style] = entry
		si += 1
	var f := FileAccess.open(out_path, FileAccess.WRITE)
	f.store_string(JSON.stringify(out))
	f.close()
	get_tree().quit()
