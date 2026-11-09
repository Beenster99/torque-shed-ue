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

# New chopper tanks

beenster's five chopper tank drawings (Nov 2026) replace the chopper's tanks:

| Drawing | Part and shape | Capacity |
|---|---|---|
| Tank 1 | `chopper_1` (the chopper reference bike's tank) | 7 L |
| Tank 2 | `chopper_2` | 9 L |
| Tank 3 | `chopper_3` | 4 L |
| Tank 4 | `chopper_4` | 7 L |
| Tank 5 | `chopper_5` | 5 L |

- `peanut_9` and `coffin_10` are no longer offered for choppers (they stay for bobbers and cruisers).
  `godot_removed.json` keeps Godot's own entry of every part the overlay removes or takes a style off,
  so the Godot comparisons use Godot's parts.
- The tanks are `like` the `coffin` shape and drawn whole at its length (0.5 m): they taper to the tail
  with no separate tail. Each section is the front view's round outline scaled to the station, over a
  round-roofed tunnel the whole length; tank 1's side line is a crease, and tank 2's double outline is its
  rounded edge (no crease).
- Models, renders and scripts are in the project files under `chopper_tanks/`.

# New cruiser tanks

beenster's five cruiser tank drawings (Nov 2026) replace the cruiser's tanks:

| Drawing | Part and shape | Capacity |
|---|---|---|
| Tank 1 (recessed side panel, 2 mm) | `cruiser_1` (the cruiser reference bike's tank) | 30 L |
| Tank 2 | `cruiser_2` | 23 L |
| Tank 3 | `cruiser_3` | 32 L |
| Tank 4 | `cruiser_4` | 29 L |
| Tank 5 | `cruiser_5` | 21 L |

- `fatbob_23` is taken off sale (it had no other style left; old saves move to `cruiser_1`), and `peanut_9`
  is now a bobber tank only.
- The tanks are `like` the `fatbob` shape, drawn whole at its length (0.64 m), with round sections from the
  front view (as the chopper tanks); the thin lines are flush creases and the bottom line a crease.
- Models, renders and scripts are in the project files under `cruiser_tanks/`.

# New naked fairings

beenster's five naked fairing drawings (Nov 2026, side | front | top) are the naked bike's fairings now:
parts `naked_fairing_1` to `naked_fairing_5` (frame-mounted, naked only), drawn as the sets
`Data/Parts/Redrawn/fairing/set/naked_1..5.glb` (`look.shape` `naked_N`; `BikeParts::Fairing` reads any
`naked_*` shape as its own set). `flyscreen`, `headlight_cowl`, `bikini_fairing` and `half_fairing` no longer
list naked; the naked reference bike still has no fairing.

- How they are made: the front view is split into the panels its lines close off (screen, cowl panels,
  lamps, vents). Each front pixel's depth comes from the side view (the cowl's / screen's front and rear
  edge at that height) and the top view (how the front contour sweeps back across the width). Each panel
  gets its own smooth surface fitted over that depth, so neighbouring panels meet in a crease along the
  drawn line. The front view sets widths and heights (420 mm across, as the half fairing), the side and
  top views the depth.
- Clean edges: the surface is a 3 px grid over the front view, but its outline points, the lamp, lamp
  housing and vent edges, and the drawn creases are snapped onto smooth curves (the zero level of each
  mask's smoothed signed distance), so no edge follows the pixel grid. The stays' outlines are snapped the same way.
- Shells: cowl 3 mm, separate smoked screen 4 mm (`glass`), lamps drawn in the fairing (`lens_white`,
  6 mm back, in a dark `plastic` housing 3 mm back); the fitted bike's own headlight is hidden. Hatched
  vents are 5 mm `plastic` recesses. The stays are 6 mm `black_metal` plates where the side view draws them,
  at the top view's spacing, ending at their bolt eyes at the head stock.
- Left out, as agreed: N3's fork legs, N4's yoke and bar clamps, and the dashes.
- Placed as the half set: nose about 0.35 m ahead of the head stock, lamps at -0.17 m (`mount_lamp`).
- Models, renders and scripts are in the project files under `naked_fairings/`.
