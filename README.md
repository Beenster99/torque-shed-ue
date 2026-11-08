# Torque Shed on Unreal Engine 5: the Buttertubs prototype

A first, small step towards Torque Shed in Unreal Engine 5: one bike (the 2025 Triumph Speed Twin 1200)
on 3 km of Buttertubs Pass, from the summit plateau down the drop towards Thwaite. It's here to answer
one question before committing to the whole plan: does Torque Shed's riding feel right in Unreal, on
real land, at 4K on an RTX 4090? Plan and ideas: https://claude.ai/artifact/6RwcD2ChqkkNmGsjZgFk5y

## What's in it

| Path | What it is |
| --- | --- |
| `Source/TorqueShedProto/Sim/` | Torque Shed's riding physics (bike.gd, tyre_model.gd) ported to plain C++, with no Unreal types, so it rides the same as the Godot game and can be tested on its own |
| `Source/TorqueShedProto/TorqueBikePawn.*` | The bike in Unreal: steps the sim at 120 Hz, sees the ground by line traces, chase and onboard cameras, keys and pad, HUD, respawn after a crash |
| `Data/Buttertubs/` | The 3 km: 54 terrain tiles (2 m grid, road bed cut in, land cover as vertex colour), the far hills (32 m, out to 6 km), the road ribbon, and `route.json` |
| `Tools/export_buttertubs.py` | Makes `Data/Buttertubs` from the Godot game's Dales region data (needs Python 3 and numpy) |
| `Tools/buttertubs_stone.py` | The countryside stone by stone: Dales drystone walls (footings, battered coursed faces, through stones, copes), wall heads, gates and stoops, limestone scars, boulders, Swaledale sheep, and the map's buildings as light shells: two-storey Swaledale houses facing the street, in terraces where their footprints meet, some rendered or limewashed, with porches, front garden walls, stone or blue slate roofs and gable chimney stacks; farms, field barns in the meadows, and Muker's church, chapel, school and pub. Walls run along the valley road where the map's field walls stop at it, and `village.json` marks the villages and front gardens. Beyond the near land, the map's walls and buildings carry on out to 3 km as simple far shapes (`far_X_Z.glb`) on the far hills. `export_buttertubs.py` writes them to `Data/Buttertubs/Props` and places them along the map's walls, cliffs and buildings in `props.json`; the level script draws them as Nanite instances. `Data/Buttertubs/prop_swaps.json` can swap any of them for a Megascans mesh |
| `Tools/make_foliage.py` | The moorland ground cover: grass tufts and tussocks, short turf, soft rush, heather, moss and bracken, modelled as Nanite geometry, and where each grows (by slope, wetness, cover, distance from the road, away from walls). Writes `Data/Buttertubs/Foliage`; `Source/TorqueShedProto/TorqueFoliage.*` plants them at Play. `--density 0.6` for fewer |
| `Tools/make_trees.py` | The trees, every one where a canopy height map (Meta and WRI, 1 m) says a tree stands, at its real height: ash and alder along the gills, sycamore and ash round Thwaite's fields, hawthorns on the fellside, spruce plantations, about 114,000 out to 6 km. Species from the map's leaf types, becks and stands. The models are grown by `Tools/tree_models.py` as Nanite geometry (opaque leaves, no cut-outs; root flare at the foot; UV1 along the bark for M_Tree's scanned bark, the `bark` texture role, ambientCG Bark004), with cheap far versions for trees more than 1.5 km from the ride. Writes `Data/Buttertubs/Trees`; a second TorqueFoliage actor plants them at Play |
| `Tools/make_far_rock.py`, `Tools/buttertubs_far.py` | The far fells' limestone scars and screes, from the map's cliff lines and scree areas: tinted into the 32 m far hills' colours (by `export_buttertubs.py`), and the scars more than 40 m outside the 2 m tiles built as a bedded face under a turf shelf set into the hillside (`Data/Buttertubs/Far`) |
| `Tools/make_becks.py` | The becks and gills on the ride's 2 m land (about 20 km of the map's streams), moved onto the gill floors: a wet stony bed, the water (M_Beck, ripples running downstream, white on the steep) and 29,000 cobbles and boulders planted as instances. Where the road crosses a beck, a culvert headwall either side (M_Masonry, upright coping stones, the culvert's dark mouth), the bed running in under it and the water stopping 1.6 m short of the tarmac. Where the map marks a waterfall (Catrake Force and East Gill Force at Keld, Cliff Force, Scar House Force, Aygill Force, Hoggarth's Leap), the beck's steep stretches are rebuilt as limestone steps with still water on each shelf pouring over its lip as a white sheet. `make_foliage.py` keeps the grass off them |
| `Tools/make_speedtwin.py` | Builds the Speed Twin model (65 parts as glb, with UVs and finish masks, in `Data/Bike/SpeedTwin`) from measurements of your photos; `--check photo.jpg` renders it over a side-on photo |
| `Tools/make_bike_textures.py` | Makes the bike's tileable detail textures (tyre tread, leather grain, brushed and cast alloy, paint flake, knurled grips) in `Data/Bike/Textures`; procedural, nothing to download |
| `Tools/make_rider.py` | Builds the rider in riding kit (jacket, jeans, boots, brown gloves, full-face helmet) as a skinned glb in `Data/Rider`, sat on the Speed Twin; the pawn poses it every frame (`Source/TorqueShedProto/RiderRig.*`) |
| `Tools/ue_build_level.py` | Run inside the Unreal editor: imports the meshes, makes the materials, sun, sky and fog, and saves the level |
| `Tools/import_parts.py` | Copies the Godot game's parts catalogue (`data/parts.json`, 717 parts) unchanged into `Data/Parts/parts.json`, the file the game reads; the Godot JSON stays the source of truth |
| `Tools/make_speedtwin_fit.py` | The Speed Twin's fitment data (`Data/Bike/SpeedTwin/fitment.json`): typed sockets, datums, collision proxies, and alternative bars and silencers in its style (`Data/Bike/SpeedTwin/Options`). Run after `make_speedtwin.py` |
| `Source/TorqueShedProto/Fit/` | The fitment system: parts fit by typed sockets (`Data/Fitment/interfaces.json`) with the adjustment each allows, clearance checks at full lock and full compression, geometry mode (player and author fitting). `TorqueFit.h` is the API the shed and the pawn use |
| `SimTest/` | Standalone tests: the physics (flat-ground checks and a robot rider down the exported 3 km), and `fit_test`, which assembles the Speed Twin from its sockets and reports clashes |

The bike is a first Speed Twin model built from your two side-on drone photos: wheels, forks, tank,
seat, engine, headers and silencers, in simple finishes. The rider is a skinned figure in riding kit, made by `Tools/make_rider.py`: hands on the grips as the bars turn, feet on the pegs or down when you stop, weight and lean as you ride. Its kit is textured from scanned CC0 surfaces (the `rider_` roles in `Data/Textures/textures.json`, fetched by `Tools/fetch_textures.py`): textile jacket, denim, leather gloves and boots, rubber soles, the weave following the sleeves and legs. Without them it falls back to plain colours.

## Setting it up on your PC

1. **Install Unreal Engine** 5.8 (the project is set to 5.8; newer should work too) from the Epic Games Launcher (free).
2. **Install Visual Studio 2022 Community** (free) with the *Game development with C++* workload. In
   that workload's options, tick the Unreal Engine installer and a Windows 11 SDK.
3. **Get the project**: clone this repository, for example with GitHub Desktop, to a short path such
   as `C:\Dev\torque-shed-ue`. The data is plain files, so no Git LFS is needed.
4. **Open it**: double-click `TorqueShedProto.uproject`. If it asks which engine to use, or says the
   engine version differs, right-click the file, choose *Switch Unreal Engine version*, and pick yours.
   When it says the TorqueShedProto module is missing and asks to rebuild it, say **Yes**. The first
   build and shader compile take a while.
   - If the rebuild fails, right-click the `.uproject`, choose *Generate Visual Studio project files*,
     open `TorqueShedProto.sln`, and build *Development Editor*. Paste any errors to Claude.
5. **Build the level**: in the editor choose *Tools > Execute Python Script...* and pick
   `Tools/ue_build_level.py`. It imports the land, road and bike, makes the materials and lights, and saves
   `/Game/Maps/Buttertubs`, which is also the startup map. The Output Log shows `[Torque]` lines as
   it goes. If the editor says the Python plugin needs a restart, restart it and run the script again.
   Without opening the editor: `UnrealEditor-Cmd.exe TorqueShedProto.uproject -ExecutePythonScript=Tools/ue_build_level.py -unattended`
   (not `-run=pythonscript`: the script needs the full editor's subsystems).
6. **Ride**: press *Play*. For the full frame rate, use *Play > Standalone Game* or
   *Selected Viewport* with the editor's other panels closed.

### Controls

Any Xbox-style controller works, and so does a PlayStation pad (DualSense, DualSense Edge or
DualShock 4, over USB or Bluetooth) with no extra tools: the game reads it directly. If DSX,
DS4Windows or Steam Input is running too, that works as well. The HUD says when a PlayStation pad is
connected. On a PlayStation pad, A is Cross, B is Circle, X is Square, Y is Triangle, the bumpers are
L1 and R1, View is Create and Menu is Options.

| | Keyboard | Pad |
| --- | --- | --- |
| Throttle | W or Up | Right trigger |
| Front brake | S or Down | Left trigger |
| Rear brake | Space | Left bumper |
| Gear up, gear down (the first press switches to manual) | X and Z, or Left Shift and Left Ctrl | A and X |
| Automatic or manual gears | G | Right stick click |
| ABS on or off (off lets the rear wheel lock) | B | B |
| Lean | A and D, or Left and Right | Left stick |
| Weight back and forward | Q and E | Left stick, pushed well up or down |
| Look around | | Right stick |
| Camera (chase, first person, chin camera) | C | Right bumper or View |
| Back on the road | R | Y |
| Start again | Home | Menu (Start) |
| Riding realism | [ and ] (0 full sim, 1 easy) | D-pad up and down |
| Quit | Esc | View and Menu together |
| Performance readout (fps, frame, game, render and GPU ms) | P | Left stick click |
| Log performance to Saved/Profiling/torque_perf_*.csv | O (or launch with -TorqueProfile) | |

Sound is made as you ride (Source/TorqueShedProto/TorqueAudio): a 270-degree twin synthesised from
the revs and throttle, with recorded idle, gear lever clunks and moorland birds from Torque Shed
(Tools/make_sounds.py puts them in Data/Audio).

### Riding with friends (co-op)

The game opens on a start menu: **Ride solo**, **Host a co-op ride**, **Join by address**, rides found
on your network, and **Quit**. The host's game is the server (port 7777); friends on the same network
see the ride in the menu, and friends elsewhere type the host's address (the host forwards UDP port
7777 on their router until Epic Online Services is set up). Every rider rides their own bike on their
own machine; the others' bikes, riders and engines follow smoothly. The host sets the weather and
takes everyone between the shed and the road. Any `-Torque...` test switch skips the menu, as before.
How it works, the one-PC test and the plan for EOS: [Docs/coop.md](Docs/coop.md).

### DLSS on the 4090

The project starts on Lumen with hardware ray tracing, virtual shadow maps and TSR. For DLSS, download
the free *NVIDIA DLSS plugin* for your engine version from NVIDIA's developer site, unzip it into a
`Plugins` folder next to the `.uproject`, enable *DLSS* and *Streamline DLSS-G* under
*Edit > Plugins*, and restart. Then in the console (the ` key), `r.NGX.DLSS.Enable 1` turns on
Super Resolution, and `r.ScreenPercentage 50` is DLSS Performance at 4K, `67` is Quality. Frame
generation and Reflex are under *Edit > Project Settings > Plugins > NVIDIA DLSS*.

## Rebuilding the data or testing the physics (optional)

You only need this if the route or the physics change.

```
python Tools/export_buttertubs.py --torque-shed ..\torque-shed
python Tools/make_becks.py --torque-shed ..\torque-shed
python Tools/make_foliage.py
python Tools/make_far_rock.py --torque-shed ..\torque-shed
python Tools/make_trees.py --torque-shed ..\torque-shed
cmake -S SimTest -B build/simtest
cmake --build build/simtest --config Release
build/simtest/Release/sim_test Data/Buttertubs
```

`build/simtest/Release/fit_test` (run from the repository) assembles the Speed Twin from its
sockets, checks the model against the physics' datums, runs the clearance checks, tries every
alternative part and exercises geometry mode. To try a swap in the game without the shed, start it
with `-TorqueBuild=handlebars:clubman_bar,exhaust:peashooter_twins`.

The physics test checks the bike settles on its springs, does 0 to 60 mph in about 3.2 s, holds a lean,
stands back up, stops from 60 mph in about 36 m, and that a simple robot rider gets down the 3 km
without crashing and within 3 m of the centre line (its run takes 234 s, leaning up to 32 degrees).

## Frames

The sim and the glb files use Torque Shed's region frame: metres, x east, y up, z south, relative to
an origin at the start of the ride (54.3535 N, 2.2058 W, 515 m). Unreal is centimetres, x east,
y south, z up, so a sim point (x, y, z) is Unreal (100x, 100z, 100y). The level script measures how
the glTF importer turned the files and lines the meshes up to match.

## Credits

Elevation: Mapterhorn (mapterhorn.com), built from national and global terrain models. Roads and land
cover: (c) OpenStreetMap contributors and others, Overture Maps Foundation (ODbL). Trees: canopy heights
from Meta and World Resources Institute, High Resolution Canopy Height Maps (2024), CC BY 4.0.
