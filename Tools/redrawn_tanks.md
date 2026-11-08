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
