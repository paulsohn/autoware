#!/usr/bin/env bash
# Run from the colcon worktree, after building the pipeline fixture.
set -eu
out=${1:?fresh results directory}
mkdir "$out"
export BUFFER_RUNTIME_ONLY=1
variant=${2:-jitter}
case "$variant" in slow|jitter|combined) ;; *) exit 2 ;; esac
control=baseline
if [[ $variant == combined ]]; then control=jitter; fi
unset SYNC_AUDIT SYNC_SINK_TRACE
for round in 1 2; do
  modes=(stock cyclone zenoh)
  conditions=("$control" "$variant")
  if [[ $round == 2 ]]; then modes=(zenoh cyclone stock); conditions=("$variant" "$control"); fi
  for mode in "${modes[@]}"; do
    for condition in "${conditions[@]}"; do
      unset SYNC_TOP_JITTER SYNC_SLOW_SINK
      if [[ $condition == slow || $condition == combined ]]; then export SYNC_SLOW_SINK=1; fi
      if [[ $condition == jitter || $condition == combined ]]; then export SYNC_TOP_JITTER=1; fi
      experiments/buffer/run-lyrical.sh timeout -k 5 65 bash -ec '
        TIMEFORMAT="graph_user_s=%U graph_sys_s=%S"
        time bash experiments/rmw-agnocast/run-pipeline.sh "$1"
        scope=$(sed -n "s,^0::,,p" /proc/self/cgroup)
        case "$scope" in */aw-buffer-*.scope) cat "/sys/fs/cgroup$scope/memory.peak" | sed "s/^/scope_peak_bytes=/" ;; *) exit 2 ;; esac
      ' bash "$mode" >"$out/$round-$mode-$condition.log" 2>&1
    done
  done
done
python3 experiments/rmw-agnocast/summarize-arrival.py "$out" "$variant"
