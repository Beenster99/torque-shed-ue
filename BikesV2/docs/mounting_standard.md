# The hidden mounting standard (version 1, to be proven in Phase 1)

The standard is what lets a tank and seat from one brand bolt to a frame from
another, inside one family. It lives where it is never seen: under the tank and
under the seat.

## The deck

Each family has one **deck**: a line in the side view with fixed mounts along it.
All numbers are in `data/families.json` under `deck`.

- **Nose point.** Where the seat nose meets the back of the tank. Each frame
  places it inside the family's `nose_x` and `nose_z` ranges.
- **Tank section.** Runs forward from the nose for `tank_span`. Two mounts: a
  rear tab 20 mm ahead of the nose and a front pair of rubber bobbins at the far
  end. Fixed to the main frame. Its pitch must be inside `tank_pitch`.
- **Tunnel.** Under the tank the frame must stay inside a tunnel `tunnel_w` wide
  and `tunnel_h` tall above the deck line. Every tank keeps its underside clear
  of that tunnel. This is what lets a trellis, a spar or a backbone share tanks.
- **Seat section.** Runs back from the nose. Mounts at the nose (hook), at
  `seat_solo` and at `seat_dual`, on two rails `rail_spacing` apart. It belongs
  to the bolt-on subframe and may pitch inside `seat_pitch`, hinged at the nose.
- **Tail mounts.** Tails use the `seat_solo` and `seat_dual` mounts.

## Joint rules

1. **Overlap, never butt.** The seat nose sits over the back of the tank by at
   least 25 mm. The tail tucks under the seat by at least 30 mm. Side panels
   tuck under the tank's lower edge.
2. **Pairs.** A tank and its seat are designed together. So are a tank and its
   fairing. A pair is fitted as one in the shop; a part of a pair may still be
   fitted with another brand's partner, and the overlap rule keeps it gap free.
3. **Engines follow layout.** A frame is drawn around one engine layout and
   takes any engine of that layout in the family.
4. **Shared pool.** Wheels, tyres, forks, swingarms, bars, levers, mirrors,
   lights and exhaust cans mount on family-wide standards (axle sizes, yoke
   spacing, bar clamp, pivot width) set in Phase 1.

## Seat pan (added in Phase 0 after the first mixed builds)

Tail lines differ between brands, so a seat from one brand does not sit exactly
on another brand's tail. Every seat therefore carries a hidden pan 40 mm deep
under its base, drawn behind the tail. A seat may float up to 30 mm above a
foreign tail (the pan still reaches into it) or sink up to 35 mm into it. The
first two nakeds mix both ways inside these limits (worst gap 28 mm, worst
sink 30 mm).

Each model file declares its deck: `"deck": {"nose": [x, z], "tank_pitch": deg,
"seat_pitch": deg}`. `foundry/mix.py` seats a donor's tank, seat or tail on a
host by matching noses and turning by the pitch difference.

## Checks the pipeline runs (foundry/checks.py: deck, and the mix matrix)

For every frame in a family: deck placed inside the ranges, tunnel clear. For
every tank, seat and tail: mounts on the deck within 1 mm, overlaps present.
For every combination: no gap, no clipping, nothing hanging.

## Joint rule 4: a bench seat and its tail are one unit

A long flat bench (the Hartwell's) cannot sit on a tail that kicks up behind a short seat, and a short
seat leaves daylight over a flat bench tail. A model whose seat and tail are designed as one piece says
so with `"seat_unit": true` in its deck. In a mixed build, whenever such a seat moves to another frame,
or another brand's seat moves onto such a frame, the tail and its undertray travel with the seat. Both
still mount on the same subframe rails, so nothing else changes. `foundry/mix.py` applies this by itself
and the checks run on the result.
