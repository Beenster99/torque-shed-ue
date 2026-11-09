# Redrawn sports tanks

beenster's four three-view tank drawings (Nov 2026), modelled in Blender and swapped in for the Godot
game's sports tanks:

| Drawing | Replaces (shape) | Parts using it |
|---|---|---|
| Tank 4 (the first test tank) | `sport` | sport_17 |
| Tank 1 | `race` | carbon_sport_16 |
| Tank 2 | `wedge` | angular_sport_17 |
| Tank 3 | `streetfighter` | angular_naked_15 |

- `Data/Parts/Redrawn/tank/<shape>.glb` holds each one in the Godot parts agreement (node `part`, lod0-2,
  material `paint`, shape keys as morph targets). `Tools/import_blender_parts.py` reads it in place of the
  Godot glb (`--redrawn` converts just these, no Godot checkout needed) and marks the index entry `"redrawn"`.
- The import also measures each tank's loft keys (`[u, half width, up, down]`) off the new mesh into
  `Data/Parts/Redrawn/tank_shapes.json` and `layout.json`, keeping Godot's as `godot_keys`
  (`Tools/import_layout_data.py` lays the file over the Godot shapes), so the seat nose, tank bag and
  layout read the tank that is drawn.
- Size: the drawings have no dimensions, so each tank keeps the drawing's proportions at the game's length
  for its shape (`len`), drawn at the reach the game always gives a tank's nose (`nose_reach` 0.6).
- Shape keys: `tunnel_height`, `flank_depth`, `tunnel_width` and `tunnel_close` are made for each tank's
  own tunnel and flanks. `nose_reach`, `knee_fore` and `knee_aft` are left empty, because the nose and the
  knee recesses are drawn in.
- `SimTest/layout_test` compares the rest of each bike with Godot using Godot's tank shapes, and leaves out
  the redrawn tank's own mesh. The bodywork rules still hold the tank to its frame. A redrawn tunnel is
  arched, so the tank must rest on the floor line at its lowest point.
- The model files, renders, silhouette checks and Blender build scripts are in the project files, under
  `tanks/` (scripts in `tanks/source/`).

# New bobber tanks

beenster's five bobber tank drawings (one sheet, Nov 2026) are added to the parts list as five new
bobber-only tanks. The Godot game has no tank like them, so nothing is replaced (peanut_9 and coffin_10
stay):

| Drawing | Part and shape | Capacity |
|---|---|---|
| Row 1 | `bobber_1` | 14 L |
| Row 2 (recessed side panel, 2 mm) | `bobber_2` | 14 L |
| Row 3 | `bobber_3` | 10 L |
| Row 4 | `bobber_4` | 11 L |
| Row 5 | `bobber_5` | 10 L |

- The parts are in `Data/Parts/Redrawn/parts_overlay.json`. `Tools/parts_overlay.py` lays it over the Godot
  catalogue, and `import_parts.py` and `import_layout_data.py` apply it on every re-import. The overlay can
  also remove parts, drop styles from a part, map removed parts to their replacements (`retired`, for old
  saves) and set a style's reference-bike parts.
- A new shape's `tank_shapes.json` entry names the Godot shape it is most `like` (`peanut`). The import
  takes that shape's u's, up/down split, `len`, `n` and `bw`, and measures the keys off the new mesh. The
  part id is the shape name.
- Size: drawn at the peanut tank's length, with the drawing's proportions. Capacity is 85% of the shell's
  modelled volume, which also sets how big the game draws it (as for every tank).
- `layout_test` puts each new tank on its style's reference bike and holds it to the same bodywork rules.
- Models, renders and scripts are in the project files under `bobber_tanks/`.

# New adventure tanks

beenster's five adventure tank drawings (Nov 2026) replace the Godot game's adventure tank:

| Drawing | Part and shape | Capacity |
|---|---|---|
| Tank 1 | `adventure_1` (the adventure reference bike's tank) | 39 L |
| Tank 2 | `adventure_2` | 34 L |
| Tank 3 | `adventure_3` | 37 L |
| Tank 4 | `adventure_4` | 33 L |
| Tank 5 | `adventure_5` | 21 L |

- `adv_25` is taken off sale (old saves move to `adventure_1`), and `enduro_12` is no longer offered for
  adventure bikes (it stays for mx, supermoto and scrambler). Godot's own parts that the overlay removes
  are kept in `Data/Parts/Redrawn/godot_removed.json` (`parts_overlay.py` writes it), and `layout_test`
  compares with Godot using them and Godot's own reference bikes (`SimTest/data/godot_reference.json`).
- Each tank has a low tail that runs back under the seat. The body (nose to where the top drops under
  55% of its height) is drawn at the adv tank's length, 0.58 m, and the tail runs on past x = -1.
  Capacity is 85% of the body's volume (the tail is not fuel), measured off the crease cage (`vol3.py`).
- The game grows a tank with its capacity; these are modelled at full size, so each part's `look.scale`
  cancels that growth and the game draws the tank as modelled.
- The tank rests on the floor line at the lowest point of its tunnel over the middle 60% of its whole
  length, tail included (where `layout_test` reads it).
- Models, renders and scripts are in the project files under `adventure_tanks/`.

# New sports tourer tanks

beenster's five sports tourer tank drawings (Nov 2026) replace the tourer's tanks:

| Drawing | Part and shape | Capacity |
|---|---|---|
| Tank 1 | `sports_tourer_1` (the tourer reference bike's tank) | 32 L |
| Tank 2 | `sports_tourer_2` | 30 L |
| Tank 3 | `sports_tourer_3` | 28 L |
| Tank 4 | `sports_tourer_4` | 30 L |
| Tank 5 | `sports_tourer_5` | 28 L |

- `angular_gt_19`, `touring_23` and `fatbob_23` are no longer offered for tourers (they keep their other
  styles). The new tanks are `like` the `gt` shape, with the body drawn at its length (0.6 m) and the
  short tail step running on past x = -1, as the adventure tanks.
- The front U cut-out goes right through the nose (`gnotch.py`, as the sports tanks' steering-head notch);
  the game mesh has it too, with each vertex's shape keys taken from its nearest uncut vertex.
- The lugs under the side view are mirrored pairs on the front view's feet (matched by height), the tail's
  step bracket sits at the plan's rear tabs, and tank 5's flank loop is a 2 mm pocket. Lugs, cap and pocket
  are in the model files, not the game mesh (body only, as every tank).
- Models, renders and scripts are in the project files under `tourer_tanks/`.
