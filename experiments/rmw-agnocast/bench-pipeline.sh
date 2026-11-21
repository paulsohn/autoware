#!/usr/bin/env bash
set -eu
out=${1:?fresh result directory}
mkdir "$out"
export BUFFER_RUNTIME_ONLY=1
for round in 1 2; do
  modes=(plain stock runner cyclone zenoh)
  if [[ $round == 2 ]]; then modes=(zenoh cyclone runner stock plain); fi
  for mode in "${modes[@]}"; do
    log="$out/$round-$mode.log"
    experiments/buffer/run-lyrical.sh timeout -k 5 65 bash -c '
      TIMEFORMAT="graph_user_s=%U graph_sys_s=%S"
      time bash experiments/rmw-agnocast/run-pipeline.sh "$1"
    ' bash "$mode" >"$log" 2>&1
    grep -q 'verified 24 synchronized clouds and metadata' "$log"
  done
done
python3 experiments/rmw-agnocast/summarize-pipeline.py "$out"
