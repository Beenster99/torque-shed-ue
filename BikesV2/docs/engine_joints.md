# Engine joints (engine parts pass, stage 1)

Measured from every V2 bike's built parts by `foundry/engine_joints.py`; mm from the crank (x forward, y out to one side, z up).
Top-end joints (exhaust port, intake, cam caps, rocker cover) are in their cylinder's own frame: t along the bore from the head face, y out to the side, u across the bore.
Airboxes are not a joint: each new airbox starts at the intake joint and is shaped to the room each frame leaves.
Header pipes and intakes are drawn from each engine's own ports (this file's `bikes`), as the silencers are from each bike's own pipe,
so a type's parts fit all its engines even where ports sit apart (a different head or cylinder lean); the type's point is the reference the looks are designed round.
Covers, cases, sump and oil filter: a bike more than 15 mm off its type's joint gets an adapter (spacer) there.

| Engine type | Joint | At (x, y, z) | Parts | Bikes | Worst off (mm) | Adapters |
| --- | --- | --- | --- | --- | --- | --- |
| Four-stroke single | alternator cover | 0, 148, 0 | 1 | 2 | 0 | - |
| Four-stroke single | cam caps | 51, 74, 0 | 2 | 2 | 1 | - |
| Four-stroke single | clutch cover | 0, 157, 0 | 2 | 2 | 0 | - |
| Four-stroke single | exhaust port | 89, 55, -5 | 1 | 2 | 30 | kazeda_mx, varga_mx |
| Four-stroke single | intake | 50, 0, 1 | 1 | 2 | 86 | kazeda_mx, varga_mx |
| Four-stroke single | oil filter | 129, 74, -56 | 1 | 2 | 0 | - |
| Four-stroke single | rocker cover | 115, 0, 0 | 1 | 2 | 2 | - |
| Four-stroke single | side case | -64, 126, 11 | 2 | 2 | 0 | - |
| Four-stroke single | sump | -39, 0, -106 | 1 | 2 | 0 | - |
| Two-stroke single | alternator cover | 0, 148, 0 | 1 | 1 | 0 | - |
| Two-stroke single | cam caps | 37, 66, 0 | 2 | 1 | 0 | - |
| Two-stroke single | clutch cover | 0, 157, 0 | 2 | 1 | 0 | - |
| Two-stroke single | exhaust port | -17, 60, -22 | 1 | 1 | 0 | - |
| Two-stroke single | intake | 36, 0, 78 | 1 | 1 | 0 | - |
| Two-stroke single | oil filter | 117, 74, -58 | 1 | 1 | 0 | - |
| Two-stroke single | rocker cover | 87, 0, 3 | 1 | 1 | 0 | - |
| Two-stroke single | side case | -58, 126, 10 | 2 | 1 | 0 | - |
| Two-stroke single | sump | -36, 0, -95 | 1 | 1 | 0 | - |
| Parallel twin | alternator cover | 0, 148, 0 | 1 | 7 | 0 | - |
| Parallel twin | cam caps | 58, 74, -0 | 2 | 7 | 38 | kazeda_cruiser |
| Parallel twin | clutch cover | 0, 157, 0 | 2 | 7 | 0 | - |
| Parallel twin | exhaust port | -28, 40, -62 | 1 | 7 | 127 | hartwell_cafe, hartwell_chopper, hartwell_classic, kazeda_cruiser, varga_naked |
| Parallel twin | intake | 56, 0, 86 | 1 | 7 | 38 | kazeda_cruiser |
| Parallel twin | oil filter | 162, 74, -52 | 1 | 7 | 0 | - |
| Parallel twin | rocker cover | 128, 0, 3 | 1 | 7 | 5 | - |
| Parallel twin | side case | -82, 126, 14 | 2 | 7 | 0 | - |
| Parallel twin | sump | -48, 0, -136 | 1 | 7 | 0 | - |
| 90 degree V-twin | alternator cover | 0, 148, 0 | 1 | 6 | 0 | - |
| 90 degree V-twin | cam caps | 33, 74, 0 | 4 | 6 | 6 | - |
| 90 degree V-twin | clutch cover | 0, 157, 0 | 2 | 6 | 0 | - |
| 90 degree V-twin | exhaust port | 41, 40, -8 | 2/3 | 6 | 22 | corvano_adventure |
| 90 degree V-twin | intake | -60, 0, -179 | 2 | 6 | 24 | corvano_cruiser |
| 90 degree V-twin | oil filter | 167, 74, -52 | 1 | 6 | 0 | - |
| 90 degree V-twin | rocker cover | 76, 0, 0 | 2 | 6 | 14 | - |
| 90 degree V-twin | side case | -64, 126, 2 | 2 | 6 | 3 | - |
| 90 degree V-twin | sump | -26, 0, -150 | 1 | 6 | 3 | - |
| 45 degree V-twin | alternator cover | -108, 148, 7 | 2 | 4 | 0 | - |
| 45 degree V-twin | cam caps | -13, 88, -128 | 2/4 | 4 | 14 | - |
| 45 degree V-twin | clutch cover | -108, 157, 7 | 4 | 4 | 0 | - |
| 45 degree V-twin | exhaust port | -69, 60, -92 | 2 | 4 | 40 | lodestar_cafe, lodestar_chopper, lodestar_cruiser |
| 45 degree V-twin | intake | 22, 0, -142 | 2 | 4 | 14 | - |
| 45 degree V-twin | oil filter | 163, 74, -48 | 1 | 4 | 0 | - |
| 45 degree V-twin | rocker cover | 32, 0, -147 | 2 | 4 | 14 | - |
| 45 degree V-twin | side case | -98, 126, 11 | 2 | 4 | 0 | - |
| 45 degree V-twin | sump | -69, 0, -136 | 1 | 4 | 0 | - |
| Flat twin (boxer) | exhaust port | -64, 102, -44 | 2 | 5 | 38 | rheinhart_cruiser |
| Flat twin (boxer) | intake | -63, 117, 22 | 1 | 5 | 17 | rheinhart_cruiser |
| Flat twin (boxer) | oil filter | 177, 74, -38 | 1 | 5 | 25 | rheinhart_cruiser |
| Flat twin (boxer) | rocker cover | -20, 0, 0 | 2 | 5 | 0 | - |
| Flat twin (boxer) | side case | -57, 126, 2 | 2 | 5 | 8 | - |
| Flat twin (boxer) | sump | -46, 0, -152 | 1 | 5 | 23 | rheinhart_cruiser |
| Inline triple | alternator cover | 0, 208, 0 | 1 | 3 | 0 | - |
| Inline triple | clutch cover | 0, 217, 0 | 2 | 3 | 0 | - |
| Inline triple | exhaust port | -28, 74, -101 | 3 | 3 | 0 | - |
| Inline triple | intake | 58, 74, 90 | 3 | 3 | 0 | - |
| Inline triple | oil filter | 162, 74, -52 | 1 | 3 | 0 | - |
| Inline triple | rocker cover | 130, 0, 4 | 1 | 3 | 0 | - |
| Inline triple | side case | -82, 186, 14 | 2 | 3 | 0 | - |
| Inline triple | sump | -48, 0, -136 | 1 | 3 | 0 | - |
| Inline four | alternator cover | 0, 208, 0 | 1 | 6 | 0 | - |
| Inline four | clutch cover | 0, 217, 0 | 2 | 6 | 0 | - |
| Inline four | exhaust port | 43, 83, -68 | 4 | 6 | 2 | - |
| Inline four | intake | 54, 83, 86 | 4 | 6 | 0 | - |
| Inline four | oil filter | 162, 74, -52 | 1 | 6 | 0 | - |
| Inline four | rocker cover | 122, 0, 3 | 1 | 6 | 0 | - |
| Inline four | side case | -82, 186, 14 | 2 | 6 | 0 | - |
| Inline four | sump | -48, 0, -136 | 1 | 6 | 0 | - |

Joints no bike of the type has (the type's parts don't fit there): Flat twin (boxer) clutch cover; Flat twin (boxer) alternator cover; Flat twin (boxer) cam caps; Inline triple cam caps; Inline four cam caps
