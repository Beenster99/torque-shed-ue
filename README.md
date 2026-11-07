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
| `Tools/ue_build_level.py` | Run inside the Unreal editor: imports the meshes, makes the materials, sun, sky and fog, and saves the level |
| `SimTest/` | A standalone test of the physics: flat-ground checks and a robot rider down the exported 3 km |

The bike is placeholder shapes for now. The next step is a proper Speed Twin model from your photos
and footage.

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
   `Tools/ue_build_level.py`. It imports the land and road, makes the materials and lights, and saves
   `/Game/Maps/Buttertubs`, which is also the startup map. The Output Log shows `[Torque]` lines as
   it goes. If the editor says the Python plugin needs a restart, restart it and run the script again.
6. **Ride**: press *Play*. For the full frame rate, use *Play > Standalone Game* or
   *Selected Viewport* with the editor's other panels closed.

### Controls

| | Keyboard | Pad |
| --- | --- | --- |
| Throttle | W or Up | Right trigger |
| Front brake | S or Down | Left trigger |
| Rear brake | Space | Left bumper |
| Lean | A and D, or Left and Right | Left stick |
| Weight back and forward | Q and E | Left stick up and down |
| Camera (chase, onboard) | C | Right bumper |
| Back on the road | R | Y |
| Start again | Home | |
| Riding realism | [ and ] (0 full sim, 1 easy) | |

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
cmake -S SimTest -B build/simtest
cmake --build build/simtest --config Release
build/simtest/Release/sim_test Data/Buttertubs
```

The test checks the bike settles on its springs, does 0 to 60 mph in about 3.2 s, holds a lean,
stands back up, stops from 60 mph in about 36 m, and that a simple robot rider gets down the 3 km
without crashing and within 3 m of the centre line (its run takes 234 s, leaning up to 32 degrees).

## Frames

The sim and the glb files use Torque Shed's region frame: metres, x east, y up, z south, relative to
an origin at the start of the ride (54.3535 N, 2.2058 W, 515 m). Unreal is centimetres, x east,
y south, z up, so a sim point (x, y, z) is Unreal (100x, 100z, 100y). The level script measures how
the glTF importer turned the files and lines the meshes up to match.

## Credits

Elevation: Mapterhorn (mapterhorn.com), built from national and global terrain models. Roads and land
cover: (c) OpenStreetMap contributors and others, Overture Maps Foundation (ODbL).
