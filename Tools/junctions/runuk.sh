#!/bin/bash
# runuk.sh outdir binary workers: the junction check round every place in sample.txt (JCHECK_AT), workers in parallel, each its own cache
out=$1; bin=$2; nw=${3:-3}; mkdir -p $out; cp $bin $out/world_ride.bin
here=$(dirname $0)
work() { w=$1; i=0; while read n la lo r; do
  if [ $((i % nw)) -eq $w ]; then
    lb=$(python3 -c "print($la+0.01)")
    env WORLD_RIDE_JCHECK=1 WORLD_RIDE_JCHECK_AT=$la,$lo,$r ${DUMPW:+WORLD_RIDE_JCHECK_WORST=$DUMPW WORLD_RIDE_JCHECK_DUMP=$out/$n} $out/world_ride.bin /home/user/wcache$w $la $lo $lb $lo < /dev/null 2>&1 | grep jcheck > $out/$n.txt
    echo "$n $(tail -1 $out/$n.txt)"
  fi; i=$((i+1)); done < ${SAMPLE:-$here/sample.txt}; }
for w in $(seq 0 $((nw-1))); do work $w & done; wait
