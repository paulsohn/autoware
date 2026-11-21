#!/usr/bin/env bash
set -e
out=${1:?fresh output directory}
mkdir "$out"
export SYNC_LARGE=1 BUFFER_RUNTIME_ONLY=1
for batch in large repeat; do
  modes=(cyclone cdr host fixed zenoh)
  if [[ $batch == repeat ]]; then modes=(zenoh fixed host cdr cyclone); fi
  for mode in "${modes[@]}"; do
    log="$out/$batch-$mode.log"
    status=0
    experiments/buffer/run-lyrical.sh timeout -k 4 45 bash -c '
      TIMEFORMAT="graph_user_s=%U graph_sys_s=%S"
      time bash experiments/rmw-agnocast/run-sync.sh "$1"
    ' bash "$mode" > "$log" 2>&1 || status=$?
    echo "exit_status=$status" >> "$log"
    if ((status)); then tail -n 12 "$log"; exit "$status"; fi
  done
done
python3 experiments/rmw-agnocast/summarize-sync.py "$out"
