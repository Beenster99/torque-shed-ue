# Bike Models V2

A new way of making the bikes, agreed with beenster on 13 Nov 2026. Plan:
https://claude.ai/artifact/R1xYkzh7SV877HQuChKqf9

Bikes stop being assembled from loose parts on shared fix points. Each bike is a
designed **model** from a fictional **brand**, inside one of eight **families**,
drawn as a whole machine before any 3D exists. Within a family, parts mix across
brands on a hidden mounting standard.

Nothing here is read by the game yet. Today's part list, Fit/ code and fix points
stay untouched until the switch-over phase.

## Layout

| Path | What |
| --- | --- |
| `data/families.json` | The eight family envelopes and each family's mounting deck |
| `data/brands.json` | The six fictional brands and which families each makes (34 models) |
| `docs/audit.md` | What today's game does with bikes, what stays, what V2 replaces |
| `docs/mounting_standard.md` | The hidden mounting standard and joint rules |
| `foundry/sketch.py` | Side-view sketcher: a bike description (dict) to SVG |
| `foundry/brand_thumbs.py` | One thumbnail bike per brand for the style sheets |
| `foundry/envelope_sheet.py` | One envelope diagram per family |
| `foundry/review_page.py` | Builds `out/phase0_review.html` (published as an Artifact) |

`out/` is generated and not committed. Rebuild with `python3 foundry/review_page.py`.

## Conventions

- Lengths in mm, angles in degrees.
- Side view coordinates: x forward from the rear axle, z up from the ground.
- Drawings show the bike's left side with the front to the left.
- Brand names are working names, not yet checked against real companies. No real
  maker name, logo or exact design ships.

## Phases

0 Foundations, 1 Proof (Corvano and Kazeda nakeds), 2 Naked complete, 3 the other
seven families in four waves, 4 Switch-over, 5 Depth. beenster reviews drawings
and renders as pages, never by testing builds.

## In the game (first step, for looking only)

`python3 foundry/build3d.py <model> --game` writes `Data/BikesV2/<model>.glb` and
`<model>.json` (each surface's look). `Data/BikesV2/show.json` lists the bikes
that stand in the shed and where; `"enabled": false` hides them without a
rebuild, and `-TorqueNoBikesV2` on the command line does the same for one run.
The game side is `Shed/TorqueBikesV2Show` and `TorqueShedCatalogue::MakeV2Display`.
The bikes are not ridable, have no collision and are not saved or sent in co-op.
`SimTest/bikesv2_test` loads every shown bike with the game's own glb reader.

