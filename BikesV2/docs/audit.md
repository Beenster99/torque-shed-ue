# Audit of today's bikes (13 Nov 2026, commit c02024f)

## What exists

- `Data/Parts/parts.json`: the part list copied from the Godot game. 52 kinds,
  for example 37 frames, 55 engines, 36 tanks, 34 saddles, 23 tails, 15 fairings,
  59 wheels, 47 exhausts.
- `Data/Fitment/layout.json`: 14 bike types (classic, cafe, bobber, chopper,
  cruiser, scrambler, adventure, roader, tourer, sport, race, naked, supermoto,
  mx), each with a real reference bike, its published wheelbase, seat height,
  rake and ground clearance, eight "lines" that place parts, and one reference
  build.
- `Source/TorqueShedProto/Fit/` (about 11,800 lines of plain C++): BikeLayout
  (the rig solver), BikeFrame, BikeBody (tank, seat and tail warps), BikeParts
  (every bolt-on), BikeGeometry (geometry mode), BikeRideSpec (physics spec from
  a build), BikeShop, BikeTune, PartMesh (glb reader).
- `Data/Parts/Blender` and `Data/Parts/Redrawn`: part meshes as glb, read at
  run time, so new meshes are data and do not need a new C++ build.

## What stays untouched by V2

Physics and tuning (BikeTune, engine internals, gearbox, brakes, tyres as
performance parts), the dyno, sounds per engine layout, rider and pose system,
shop and save plumbing, co-op transport, wash, fluids, HUD and screens.

## What V2 replaces (at switch-over, Phase 4)

- The 14 types become 8 families. Dropped by beenster: supermoto, race, bobber.
  Not yet decided: scrambler, roader, tourer.
- BikeLayout's fix points and "lines", BikeBody's warps and BikeFrame's coded
  frames are replaced by models authored in place on a family deck.
- The outline-changing kinds (frame, tank, saddle, tail, fairing, side panels,
  headlight, mudguards, swingarm, subframe) become model kits and the shared
  pool. Performance kinds keep their data and gain V2 meshes where visible.

## What V2 reuses

- The published reference figures per type seed the family envelopes.
- The rider figures (`5_rider`: peg and grip offsets from the hip) seed each
  family's rider triangle, so the existing rider pose code can be driven by a V2
  model's seat, peg and grip points.
- BikeRideSpec's inputs (wheelbase, rake, masses, wheel sizes) can be filled
  from a V2 model description instead of a solved rig.
- The glb reader and the per-part material tags.
