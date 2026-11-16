# Common parts: the joints (stage 0, draft for the bikes thread to agree)

Part of the plan "Common bike parts and their variants"
(https://claude.ai/artifact/MLqpH4FZ8HVYEcc5TwTVdA). The common parts thread makes bolt-on parts that
no brand owns; the Bikes V2 thread owns every bike and `data/pool.json`. This page says, for each kind
of part, **what the bike supplies** and **what every variant must fit**. Nothing here changes a model
file: every number below is read from what the models already hold.

Lengths in mm, x forward, y to the bike's left, z up (the build3d axes). "Model" = `data/models/<model>.json`.

## How a part finds its place

A part is drawn relative to an **anchor** the bike already has, never to absolute coordinates, so the
same part lands correctly on any frame. The anchors are the ones `build3d.py` and `detail3d.py` already
compute (`top`, `rz`, `grip`, `F`, `A`, peg points, `heads`, the silencer joint). When a later family's
Definition pass moves an anchor, every common part follows by itself, because each part is a script.

## 1. Handlebars (stage 1)

| | |
|---|---|
| Bike supplies | Bar clamp point `rz` (top of the risers, risers at y = ±45, their height stays with the bike), or for clip-on bikes the fork tube above or below the top yoke. The fork top `top`. The **hand point** = the model's grip circle (`front_end`, circle 20, not part "head"). |
| Every bar | 22.2 mm (7/8 in) at the clamps and through the grips; fat bars are 28.6 mm in the middle and step down to 22.2 outboard of the clamps. Straight across the clamps for at least y = ±70. **Straight from y = ±170 out to the end** (levers, switches and grips all sit there, and the bar runs straight through the grip). Bar end at y = ±(384 to 420). |
| A bar says | Its own rise, pullback, width and sweep from the clamp. |
| Check | The hand point the bar gives is compared with the model's hand point. Within 40 mm the bar is fine for that bike; 40 to 70 mm it is marked "changes the riding position"; beyond 70 mm it is not offered on that bike. At full steering lock (30 deg each way) the bar, grips, levers and bar ends clear the tank, fairing and screen by 15 mm. |

Measured from the models today (hand point relative to the clamp: rise, pullback):

| Family | Clamp | Clamp above fork top | Hand: rise, pullback |
|---|---|---|---|
| Naked | risers | 59 to 95 | +47 to +63, 47 to 68 |
| Classic | risers | 29 to 34 | +47 to +50, 47 to 48 |
| Adventure | risers | 79 to 115 | +47 to +63, 47 to 68 |
| Cruiser | risers | 60 | -12, 186 (pulled-back bar) |
| MX | no riser in the model yet: clamp taken at `top` + 62 | 62 | +28, 56 |
| Cafe, Sport | clip-ons on the fork tube | -23 to -36 | -24, 72 |
| Chopper | each brand's own bar | | **bars stay with the chopper** (part of its look); grips and bar ends still fit |

## 2. Grips and bar ends (stage 1)

- Grips: 22.2 mm bore, inner (flange) end at y = ±258, 126 mm long, outer end open for a bar end.
  The throttle grip is the same shape (its sleeve is hidden). Outside diameter at most 36 mm in the
  body and 64 mm at the flange, so levers and switches keep their places.
- Bar ends: a plug into the bar end (17 mm insert) with the visible part outboard of the grip,
  at most 45 mm across, at most 40 mm long. A mirror bar end carries its own clamp.

## 3. Levers, switchgear, mirrors (stage 2)

- Lever perch: a clamp on the bar at y = ±176 to ±200, pivot 48 mm ahead of the bar. Brake master
  cylinder on the right perch, clutch on the left. Switch pods at y = ±206 to ±254.
- Mirrors: an M10 boss on top of each perch (stalk mirrors), the bar end clamp (bar-end mirrors), or
  for faired bikes a boss on the fairing shoulder, which the bike supplies (as `detail3d.controls` finds
  it today).

## 4. Headlamps, tail lights, indicators (stage 3)

- Headlamp: two ears on the fork tubes or the bottom yoke, and a **lamp box** the bike supplies (centre
  and largest size, from the model's head shapes). Faired bikes (sport, adventure) keep their lamps in the
  bodywork: lamps there stay with the bikes thread.
- Tail light: a mount face on the tail end or the rear mudguard, facing back, with a largest size.
  Tails with the light built in (sport) stay with the bike.
- Indicators: an M10 stalk hole each side at the lamp bracket and at the tail or plate hanger.

## 5. Clock housings (stage 4)

A bracket on the top yoke (or inside the fairing) and a **dash box** the bike supplies (the space the
model's dash shape takes). The face looks at the rider's eye. Faces belong to the screens thread.

## 6. Silencers (stage 5)

The joint is at the end of the bike's own pipe: a 51 mm slip joint, its point and direction taken from
the pipe end. One hanger tab to the subframe or the peg hanger, at a point the bike gives. Each bike
gives the longest can that clears the swingarm, the rear tyre and the rider's heel. Pipes stay with the
bikes thread; only the can and its joint are common.

## 7. Footpegs, heel plates, pedals (stage 6)

The peg pivot points in the model (`hangers`, circle) **never move** (rider pose thread). Each peg set
pivots on a clevis at that point; heel plates bolt to the bike's own peg hanger. Cruiser and chopper
forward controls use their own peg points in the same way.

## 8. Discs, calipers, chains, sprockets (stage 7)

- Discs: the hub's six-bolt carrier circle and the disc size from the model. They must match the game's
  rotor part, so the disc ring keeps the game's own sizes.
- Front calipers: radial mount on the fork foot, 100 mm between bolts; classics may take an axial mount.
  Rear caliper on its carrier plate.
- Chain: 5/8 in pitch along the model's chain line; sprockets on the model's centres with the model's
  tooth counts.

## 9. Shocks, filler caps, plate hangers (stage 8)

- Shocks: the model's two eye points (twin shocks: forked bracket top and bottom, as beenster asked);
  every variant keeps the eye-to-eye length.
- Filler caps: a point on the tank top and a bolt ring size, both from the tank (the bikes thread adds the
  point when a tank does not have one). The cap stands above the tank.
- Plate hangers: a tail mount under the tail, or a swingarm mount at the axle, both optional parts.

## What each thread does

- Common parts thread: new files only (`foundry/<kind>3d.py`, their pages), reads the anchors above, runs
  the clipping check, shows the result on pages.
- Bikes V2 thread: owns models, `build3d.py`, `detail3d.py` and `pool.json`; adds approved common parts to
  the pool and says which families each suits; tells the common parts thread when an anchor moves.
