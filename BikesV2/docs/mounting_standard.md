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

## Checks the pipeline will run

For every frame in a family: deck placed inside the ranges, tunnel clear. For
every tank, seat and tail: mounts on the deck within 1 mm, overlaps present.
For every combination: no gap, no clipping, nothing hanging.
