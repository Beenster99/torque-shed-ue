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
| `Tools/make_godot_reference.py` | Runs the Godot game headless and records its reference bikes (parts, datums, mesh bounds) into `SimTest/data/godot_reference.json`, which `layout_test` checks the port against |
| `Tools/import_layout_data.py` | Copies what the Godot game lays a bike out with (style lines, tank and seat shapes, slots, the 14 reference bikes) into `Data/Fitment/layout.json` for `Fit/BikeLayout` |
| `Tools/import_blender_parts.py` | Converts the Godot game's 520 Blender part glbs (`assets/bike_parts`) into our frame at `Data/Parts/Blender`: three LOD glbs per part with a material slot per surface tag, shape keys in `.morph.bin`, and `index.json` (mounts as sockets, surfaces and the finish each is drawn in, bounds, triangles). `--sheet out.png` renders every part |
| `Tools/make_speedtwin_fit.py` | The Speed Twin's fitment data (`Data/Bike/SpeedTwin/fitment.json`): typed sockets, datums, collision proxies, and alternative bars and silencers in its style (`Data/Bike/SpeedTwin/Options`). Run after `make_speedtwin.py` |
| `Tools/speedtwin_pack.py` | The classic part pack (seats, mudguards and more), modelled in the Speed Twin's style and fitted by sockets; called by `make_speedtwin_fit.py`, never changes the stock bike. Outputs are committed: the PC doesn't regenerate them |
| `Source/TorqueShedProto/Fit/` | The fitment system: parts fit by typed sockets (`Data/Fitment/interfaces.json`) with the adjustment each allows, clearance checks at full lock and full compression, geometry mode (player and author fitting). `TorqueFit.h` is the API the shed and the pawn use |
| `SimTest/` | Standalone tests: the physics (flat-ground checks and a robot rider down the exported 3 km), `fit_test`, which assembles the Speed Twin from its sockets and reports clashes, and `layout_test`, which lays out every style's reference bike |

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

### Weather

Settings > Gameplay > Time and weather > **Weather**: **Live** (the default), changes by itself,
always sunny, always overcast or always wet roads. Live asks [Open-Meteo](https://open-meteo.com)
(free for non-commercial use, no account; weather data CC BY 4.0) for the real weather at the
route's place every 15 minutes, with MET Norway as the backup; with no internet it keeps the last
weather it had (up to 3 hours old), then changes by itself. Every source fills one weather state
(`Source/TorqueShedProto/Weather/WeatherModel.h`: cloud, rain, wind, gusts, air, visibility, road
wetness); changes fade, and roads stay wet after rain and dry over hours. The bikes' grip still
takes the nearest of the three kinds until the physics stages are done. Test switches:
`-TorqueWeather=live|auto|sunny|overcast|wet`, `-TorqueWeatherAt=lat,lon` (anywhere on Earth);
test runs stay sunny unless asked. `weather_test` checks the engine and both feeds' answers.

What the weather shows (stage 3, `Weather/TorqueWeatherFx`): falling rain round the camera (streaking
past as you ride, slanting with the wind, gone under a roof), splashes on the ground, and spray off
wet roads behind every bike's tyres, all as sprites with the spray game's `M_SprayMist`, as many as
Settings > Graphics > Effects allows. The grass and trees sway with the real wind's strength and
direction, and the cloud shadows follow the real cloud: both read `MPC_Weather` (Wind, WindX, WindY,
CloudShadow), which `Tools/ue_build_level.py` makes. Low cloud: on a day with a low deck the haze
closes in as you ride up into it on the pass top (the cloud base from the air's dew point). The
ride's sound has the real wind (heard standing still too, swelling in its gusts) and the rain.

What the weather does to the ride (stage 4): grip follows how wet the road is (`WetGrip`: a damp
road about 0.9 of dry, soaked 0.68, standing water a little less), the tyres and engine warm in the
real air (Live) on a road the sun has or hasn't warmed, and the wind pushes the bike: drag works on
its speed through the moving air (headwinds hold it back) and a crosswind leans on its side, in
gusts a few times a minute (`WindAtRider`, the 10 m wind slowed to rider height and by shelter).
Test runs (`-TorqueNoInput`) ride in still air unless they pass `-TorqueWeather=`. `sim_test`'s
Wind and `weather_test` check it.

Weather across distance (stage 5): Live asks Open-Meteo for a 5 x 5 grid of points 2.5 km apart
round where the host is (one call; again every 15 minutes, or sooner after riding out towards its
edge), and every machine works out the weather where its own camera is (`TorqueWeather::GridAt`):
between the points, the showers drifting on the wind since the fetch, and the air 0.65 C colder per
100 m above the ground. The host sends the grid to everyone (`FTorqueWeatherGridNet`). Settings >
Gameplay > Time and weather > **Real time of day** (off by default): the clock follows the real local
time where you ride (the place's UTC offset from Open-Meteo, this PC's until then) and the sun stands
as it really does for the date and place (declination, equation of time, longitude).

### Ride UK: reading the real world

The next phase lets a crew pick any road in Britain (and later the world) and ride it. The original
game already keeps the whole world as a store of 0.25 degree cells (roads, buildings, places, land
cover) with heights read live from map tiles; this game reads the same store, so nothing is baked
twice. Stage 1 is the readers, in plain C++ under `Source/TorqueShedProto/World/`:

- `WorldGeo`: the flat frame a journey is laid out in, the cells' grid and the map tiles' maths.
- `WorldImage`, `WorldInflate`, `WorldSha256`: the game's own readers for lossless WebP, PNG and
  zlib, so every rider's machine turns the same file into the same ground, to the last bit.
- `HeightTiles`: the height tiles on disk and in memory (Mapterhorn, AWS's below zoom 13), the
  source's pits filled and the sea given a sloping bed.
- `WorldRegion`: the cells an area covers stitched into one road graph (a road that crosses from one
  cell to the next joined by its ids; buildings and places on the shared edge kept once).
- `WorldTerrain`: the ground's height anywhere, from three levels of tiles eased into one another,
  levelled under each building. A ride uses only the tiles within reach of its own road, so
  everyone who plans the same road gets the same ground.

- `CellStore`, `WorldZip`: the store itself. Its index names every cell's download and hash; a cell
  is fetched as a zip, checked against that hash and kept on disk (the 400 used most recently), with
  each cell's streams, trees, scenery and place profile as small files of their own. The game hands
  it a function that fetches a URL, so the same code runs in the tests with no network.
- `WorldLoad`: an area put together to ride: its cells fetched if need be and stitched, with their
  bridges and tunnels, rivers and streams, scenery (141 kinds of thing, from a post box to a
  viaduct), land cover and trees.

`world_test` checks all of it against the original's answers on the original's own test files and
the real store's files for two Dales cells: the 73 height tiles' pixels and sea beds bit for bit, the
stitched graph junction for junction, the ground at 6,279 points (the same to the last bit), the
cells' zips against the store's hashes, and the bridges, streams, scenery, cover and trees place for
place. `Tools/make_godot_reference.py --world` records those answers again.

Inside the game, `World/TorqueWorldSubsystem` gives the readers the game's disk (what is fetched is
kept under `Saved/World`, which a new build never wipes) and its network (each fetch is sent by the
game thread and waited for on a worker). The stage's check is a ride with no bike: the cells between
two places fetched and stitched, the shortest way by road found, its ground fetched, and the road's
height read every 30 m. Start the game with `-TorqueWorldTest=hawes` (or four numbers: `latA,lonA,
latB,lonB`) and it logs `Torque world test: PASS` or `FAIL: why` and writes
`Saved/World/world_test.json`; add `-TorqueWorldTestQuit` to close the game afterwards. From a desk,
`world_ride <folder>` (SimTest, needs `curl`) runs the same check: Hawes to Leyburn is 26.5 km by
road over 2 cells and 68 tiles, 105 m to 255 m above the sea, fetched in about 14 seconds. Nothing
(Stage 1 drew nothing from any of this; stage 2, below, is the first ride on it.)

**Riding it (stage 2).** A ride between two places is planned by `World/WorldRide` (plain C++, checked
by `world_test`): the way by road with its corners rounded and a smooth height of its own, the ground
with the road's bench cut into it, and both as meshes a piece at a time. In the game,
`UTorqueWorldSubsystem::StartRide` plans one on a worker, writes its route where the bike reads it
(`Saved/World/ride_route.json`) and opens `/Game/Maps/RideUK`, a level with only the sky and lights,
made once by `Tools/ue_build_rideuk.py` in the editor (after `Tools/ue_build_level.py`). There
`ATorqueRideWorld` lays the ground out round the rider as a quadtree of squares (a point every 4 m
within 400 m, with collision; every 8 m to 1.5 km; every 64 m to 10 km) and the road in 256 m pieces,
made on workers and dropped behind. The road's line is made as the Godot game's route planner makes
one (`World/WorldRoadLine`, a port of `route_planner.gd`'s smoothing): the quick way by class of road,
every mapped corner rounded and faired so no bend is under 10 m, as wide as its class, graded to 15%,
with shoulders that slope into the ground, edge lines and a dashed centre line. The other roads
within about 300 m of the ride are laid too, junction to junction, level with the ride's road where
they meet it. Each junction's mouth has its corners rounded with tarmac, the ride's road's edge line
broken across it and, where the side road is the lesser, the UK's give-way marking (two broken lines)
across its near lane; where two side roads meet is still plain. Where the ride's road crosses a
mapped bridge it is a level deck with stone parapets over the land as it lies, and so are the other
roads' bridges; where one road goes over another with no junction, each keeps its own height. Through
a mapped tunnel the road runs in a tube (tiled kerbs, concrete walls, a sooty arch, as the Godot
game's `AdventureTunnels`) with a headwall at each mouth and the hill laid back over it; no lamps
yet. The roads are
crowned on the straight (2.5% each side) and banked in the bends by the UK's design rule (V squared
over 2.828 R per cent for the class's speed, 7% at most): the port's own, as the Godot game's roads
are flat across. The pause menu's *Map data credits* names the makers of the world's data, as their
licences ask. The ground is coloured by what covers it (pasture, moor, meadow, woods, rock,
water) and each of its finer squares carries what stands on it (`World/WorldRideScenery`): dry stone
walls, hedges and fences on their mapped lines, buildings on their mapped footprints under pitched
roofs, and stand-in trees where the map's canopy has trees, all plain coloured meshes for now.
The things that stand by the road (post boxes, seats, bus stops, milestones, phone boxes, poles,
field gates, stiles, kissing gates, barrier gates, bollards, picnic tables, information boards) are
drawn from real models where the prop kit has them (OSM stage 2, `World/WorldRideProps`): gates and
stiles snap into the wall or fence they stand in, which is cut for them, and the rest face the road.
`python Tools/make_ride_props.py` makes the plain ones (`Data/RideUK/Props/*.glb`) and
`Tools/ue_build_rideuk.py` lists them, with any Fab models matching `Data/RideUK/prop_kit.json`'s
words (preferred), in a kit actor tagged RideProps; a kind with none keeps its code-made shape.
Each road's speed limit (mapped, or the UK's rule by class where not), its name and whether it is
gravel come from the map too (OSM stage 1, `World/WorldRideFacts`): the HUD shows the limit and the
road's name, and the route planner the county and what is on the way.
Still to come: scanned materials and the satellite's tint, the grass carpet, scanned trees in the
stand-ins' places; solo only. Start it from the Options menu in the shed or yard
(*Ride UK: Hawes to Leyburn*), or with `-TorqueRideUK=hawes` (or four numbers), which logs
`Torque ride UK: PASS` once planned or `FAIL: why`; add `-TorqueAutoRide` and the robot rides it.

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

`build/simtest/Release/realism_test` (run from the repository) rides every style's stock bike against
the real bikes it stands for: 0-60 mph, the stop from 60 mph and a full-lean turn, held to the
real figures in Torque Shed's `docs/bike_style_performance.md` rather than to the Godot game's own
numbers. `realism_test --table` prints the results with the real and Godot figures beside them, and
`--swap style:slot=part,...` tries other parts on the stock bikes first. Three launches still fail
(the scrambler, adventure bike and chopper are slow to 60).

`build/simtest/Release/fit_test` (run from the repository) assembles the Speed Twin from its
sockets, checks the model against the physics' datums, runs the clearance checks, tries every
alternative part and exercises geometry mode; `fit_test --pairs` also fits every two alternatives
from different slots together and fails if they clash (about a minute). To try a swap in the game without the shed, start it
with `-TorqueBuild=handlebars:clubman_bar,exhaust:peashooter_twins`.

The rest of the catalogue comes in through `Data/Fitment/catalogue.json`, which says how each kind of
part reaches a bike (`BikeFit::AddCatalogue`). Every part of `parts.json` gets an answer: an authored
model; a placeholder (it goes in its slot and looks like the closest authored part, tinted its
catalogue colour); a tune part (its own slot, no model: air filters, maps, horns); a loose part
(no authored mount: geometry mode snaps its box to the nearest interface it can use, such as a bar
or the top of the tank, clear of everything at rest, full lock and full bump, and re-snaps when what
it sits on is swapped); or refused, with the reason (engines, frames and anything that changes the
riding physics wait for their own steps; consumables aren't fitted). `fit_test` checks all 717.

Every bike type, not just the Speed Twin, is laid out by `Fit/BikeLayout`, a port of the Godot
game's layout solver (`BikeRig.solve`): from any set of parts it works out the steering head, pivot,
engine, backbone, tank, seat, tail, pegs and grips to the style's eight lines (measured from real
bikes), says whether the engine and frame bolt together, need fabrication (named, with hours and
cost) or won't fit, and takes geometry mode's four range handles. Its data is
`Data/Fitment/layout.json` (`Tools/import_layout_data.py`: the lines, tank shapes, slots and the 14
reference bikes, one per style). `build/simtest/Release/layout_test` lays out all 14 and checks them
as the Godot game's own rig tests do; `--dump` prints their datums.

The Blender parts go on from those datums. `Fit/BikeBody` (a port of the Godot `RigBodywork`) fits
the tank, seat and tail cowl: it works out each part's shape-key weights and the warp that bends it
onto the bike's lines, and `Fit/PartMesh` reads the converted glb and `.morph.bin` and deforms it.
`layout_test` checks the bodywork of all 14 as the Godot `rig_body_tests` do (the tank's tunnel on
the backbone, its top under the style's line, the seat at its published height over the hip, and no
gaps between tank, seat and tail).

It also checks the port against the original game itself. `Tools/make_godot_reference.py` runs the
Godot project headless (official Godot 4.6.2) and records every style's reference bike as Godot
builds it: its parts, the rig's datums and fit tier, and the bounds of each slot's drawn meshes.
The result is committed as `SimTest/data/godot_reference.json`, and `layout_test` requires the same
parts, the datums within 0.1 mm, the same tier, and the tank, seat pad, tail cowl, engine and top
end within 0.5 mm. (Today they agree to under 0.01 mm.) Rerun the tool after changing the Godot game.

`Fit/BikeParts` puts the rest of the Blender parts on, slot by slot, as the Godot `BikeModel` places
them: the engine's crankcase and top end and its ancillaries (plugs and coil, carbs or throttle
bodies, air filter, oil filter, cooling, the gearbox's pieces), the exhaust (headers swept from the
Blender pipe section, and the cans), and the running gear (rims and cush drive, tyres bent round
the hoop from their tread's section, discs or drums, calipers, pads, the fork, sprockets and chain,
the swingarm's Blender pieces and the shocks), and the handlebars (the bar swept from the Blender
tube section in any of the catalogue's bends, risers or clamp, grips, switch gear, throttle, bar ends,
clutch perch and lever, and the clutch and throttle cables with their adjusters), both master
cylinders, the clocks (raised clear of the bars from the rider's eye, as Godot raises them, which
needs the glbs' meshes), the headlight's body, the mirrors' clamps and heads (turned to the rider's
eye), the indicators (or a road bike's stock pair), the front mudguard bent from its Blender
section, and the frame (Fit/BikeFrame.cpp: every style's tubes swept from the tube glb, a sport
bike's cast spar beams, welds, bearing cups, battery box, footpegs and gussets) with the swingarm's tubes, and a frame-mounted fairing's Blender panel set (half, touring,
adventure, Seeley, sport and race) widened round the engine as RigBodywork.fairing_fit does, with the
mirrors on its mounts; the tail unit (rear mudguard bent from its section, tidy tray, lamp bracket,
rear light), the stands, rearsets, horn, ECU, side panels, engine guards and plates, number boards,
steering damper, lever guards, panniers and their rack, top boxes on theirs, fog lights, extra fuel
tanks, toolboxes, the tank bag and a satnav unit. Under a full fairing the plain headlight is marked Hidden, as Godot hides it. Each placed part also says what it moves with as the
bike rides (steering, a hub, a wheel or the swingarm). The Godot reference records every Blender
surface's bounds per slot, and also a single-part swap for every part of the kinds ported so far
(every wheel, tyre, disc, caliper, pads, fork, shock, swingarm, sprocket pair, chain, engine, head,
plugs, fuel system, filters, cooling, gearbox, exhaust, handlebars, clocks, master cylinders, headlights, mirrors, indicators, front mudguards, frames and fairings), each on its style's reference bike. `layout_test` requires all of
them to match surface by surface. A fairing on the bars (flyscreen, headlight cowl, bikini, batwing) is Godot's code loft given the
Blender edge section's thickness and edges, and its screen's fasteners: made here as a mesh
(FittedPart.Made) rather than a glb deformed. The swingarms Godot draws in code (a single-sided gull
arm's cast beam, holes, hub carrier and centre nut; the box, banana and MX arms' shells, axle blocks and
chain slider; the hardtail's and softail's axle plates) are made the same way from Fit/CodeShapes.h,
which builds Godot's box, cylinder, sphere, torus and closed loft with the same vertices; those parts
carry FittedPart.Code and Godot's mesh Name, and `layout_test` checks each named one and the whole
swingarm against Godot. A phone satnav is fitted round the bars as Godot fits it: each place tried
is scored for running into the bars and what's round them, hiding the clocks and having its screen
seen, and the best one is used. The code-drawn pieces round the bars are made too: the mirrors'
stalks, joints, stems and glasses; the clocks' faces, ticks, needles, screens, shift lights and
stalk; the front brake hose and banjo; the headlight's eyes, brows, hot spots, rings, grilles,
visors, brackets and fly-screen; the indicators' stalks; a satnav's arm, ball, crossbar, stays,
visor and screen. The swingarm, mirrors, clocks, front master, headlight, indicators and satnav
are checked whole against Godot, and a phone satnav lands where Godot puts it. Under a full fairing's
panel set, what of the engine bay sits wholly behind it side on is hidden surface by surface
(FittedPart.HiddenTags), and the oil filter (under a touring set, the air filter) gets a
quick-release service cap on the panel, as Godot's _hide_covered and _service_caps; the Godot
reference records which meshes it hides, and `layout_test` matches them tag by tag.

Also drawn in code as Godot does: the front caliper's hose and clip, a drum brake's torque stay,
link rod, cable or rod, the gear linkage rod, the HT leads, the radiator's grille, fan, shrouds,
thermostat and coolant hoses, an oil cooler's lines, the ECU loom, the tank's filler cap, badges,
knee pads, stripes, console, knee panels, chest pad and shrouds, the seat's stitching, seam,
piping, grab rail, pillion pegs, touring back, springs and sissy bar, the front mudguard's rib,
stripes, brackets and stays, the air filter's boots, a single carb's runners, a tyre's whitewall bands, a gas shock's reservoirs, the steering damper rod's eye, luggage latches, reflectors, straps, flaps and the top box's mounting plate, the aux tanks' and toolboxes' brackets, fuel lines and straps, the tank bag's zips, handle, map pocket, straps and ring, and the tail's struts, brackets, cowl trim and number plate (moved
off the rear mudguard the shortest way, as Godot's _clear_plates does), and on the frame a
perimeter's cast beams and pivot plates, a trestle's castings, lugs and pivot plates, a chopper's
neck gusset, the rearsets' hanger plates and the engine mounting plates and head steady out to the
nearest tube (_draw_rig_fittings). `layout_test` checks every slot whole against Godot (its
NotWhole list, for a slot whose code pieces aren't all ported, is now empty).

Geometry mode's fit is `Fit/BikeGeometry`, a port of Godot's geometry mode (geo_gizmo.gd,
geometry_mode.gd, BikeModel.apply_geometry): the rig's four range handles and, with Expert free
move, a move, turn and size per slot about the part's middle, Ctrl snapping to 1 cm, 5 degrees and
5%. On top of it, finer control: Ctrl+Shift snaps to 1 mm, half a degree and 1%, Shift drags at a
tenth of the mouse, keys or a pad nudge the part a step at a time, a readout value can be typed in,
moves can follow the part's own turned axes, and the gaps from the moved part to its neighbours are
listed. A bike's fit travels as text (`Geo::ToText`, sanitized by the host), so co-op shares it as
it shares part fits. `geo_test` checks the maths case by case against the real game
(`SimTest/data/godot_geo.json`, from `Tools/make_godot_reference.py --geo`) and applies a fit to
every reference bike.

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
