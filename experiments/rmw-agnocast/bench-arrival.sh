#!/usr/bin/env bash
# Run from the colcon worktree, after building the pipeline fixture.
set -eu
out=${1:?fresh results directory}
mkdir "$out"
export BUFFER_RUNTIME_ONLY=1
unset SYNC_AUDIT
for round in 1 2; do
  modes=(stock cyclone zenoh)
  conditions=(baseline jitter)
  if [[ $round == 2 ]]; then modes=(zenoh cyclone stock); conditions=(jitter baseline); fi
  for mode in "${modes[@]}"; do
    for condition in "${conditions[@]}"; do
      unset SYNC_TOP_JITTER
      if [[ $condition == jitter ]]; then export SYNC_TOP_JITTER=1; fi
      experiments/buffer/run-lyrical.sh timeout -k 5 65 bash -ec '
        TIMEFORMAT="graph_user_s=%U graph_sys_s=%S"
        time bash experiments/rmw-agnocast/run-pipeline.sh "$1"
        scope=$(sed -n "s,^0::,,p" /proc/self/cgroup)
        case "$scope" in */aw-buffer-*.scope) cat "/sys/fs/cgroup$scope/memory.peak" | sed "s/^/scope_peak_bytes=/" ;; *) exit 2 ;; esac
      ' bash "$mode" >"$out/$round-$mode-$condition.log" 2>&1
    done
  done
done
python3 experiments/rmw-agnocast/summarize-arrival.py "$out"
