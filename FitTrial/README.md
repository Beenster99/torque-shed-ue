# The bike fitting trial

**This branch is never merged.** It is a trial kept apart from the game on purpose: a program of its
own that reads the game's parts and the game's own fitting code, read only, and writes pictures and
reports. Nothing under `Source/`, `Data/` or `Content/` is changed here, geometry mode is untouched,
and deleting this folder (or the branch) removes the whole experiment.

The question it answers is the only one that matters: **when a solver has finished with a bike, does
the bike look good?** beenster judges that by eye from the picture sheets. Nothing goes near the
game unless the answer is yes.

## What is here

- `trial.cpp` (`fit_trial`): builds any bike from catalogue parts exactly as the game does
  (`BikeLayout::Solve`, `BikeParts::Fit`, the real Blender meshes), draws it with its own small
  renderer, measures it, and runs the trial solver.
  - **The checker.** Each slot's parts are measured as solids (8 mm columns across the bike) and as
    skins (12 mm cells). Two slots' solids sharing space is a clash, in cubic centimetres; a slot
    whose skin comes nowhere near another's is floating. What counts as wrong is set by the 14
    designed bikes: a clash or gap bigger than any of them shows (plus a margin). All 14 pass clean.
  - **The trial solver.** It does what a patient builder does in geometry mode and nothing else:
    the rig's four handles (tank mounts, pegs, seat hoop height and tilt), then each loose or
    clashing part moved (up to 12 cm), tipped (up to 8 degrees) or sized (up to 15%), always the
    smallest change that mends the most. It never touches the frame, engine, forks, wheels or drive,
    so a clash between two of those is reported as it is, not hidden.
- `make_grid.py`: the systematic grid of builds. Every sweep takes the 14 designed bikes as rows and
  changes one thing along the columns, with every kind of that thing the catalogue has (engines:
  smallest and biggest of each layout; tanks: every shape; frames: every type; forks, bars, seats,
  tails, exhausts, fairings, swingarms). Then engine and tank together on four styles, whole top
  halves moved between styles (14 x 13), and 160 seeded random bikes with every slot drawn freely.
- `make_sheets.py`: the picture sheets and the page that shows them.

## Running it

```
cmake -S FitTrial -B build && cmake --build build
python3 FitTrial/make_grid.py . builds.json
build/fit_trial . grid builds.json out/ 0 1        # every build: pictures before and after, results_0.jsonl
build/fit_trial . refs                             # what the 14 designed bikes measure
```
