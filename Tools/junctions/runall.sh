#!/bin/bash
# runall.sh outdir binary [cache]: the junction check over every ride in rides.txt
out=$1; mkdir -p $out; cp $2 $out/world_ride.bin; cache=${3:-/home/user/wcache}
while read n a b c d; do
  WORLD_RIDE_JCHECK=1 WORLD_RIDE_JCHECK_DUMP=$out/$n $out/world_ride.bin $cache $a $b $c $d 2>&1 | grep jcheck > $out/$n.txt
  echo "$n $(tail -1 $out/$n.txt)"
done < ${RIDES:-/home/user/jn/rides.txt}
