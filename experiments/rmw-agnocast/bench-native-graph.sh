#!/usr/bin/env bash
# Run from the colcon worktree with LYRICAL_ROOT set to its installed Lyrical root.
set -eu
out=${1:?fresh result directory}
mkdir "$out"
export BUFFER_RUNTIME_ONLY=1 SYNC_SHORT_WAIT=1
unset BENCH_NEAR_DEPTH SYNC_ISOLATION SYNC_STALL_NEAR BENCH_STALL_THIS_CROP SYNC_REGIONS SYNC_DOUBLE_RATE SYNC_AUDIT SYNC_SINK_TRACE SYNC_EXTENDED SYNC_TOP_JITTER SYNC_SLOW_SINK
failed=0
rounds=(1 2)
if [[ ${2:-} == confirmation ]]; then rounds=(3 4 5 6); fi
if [[ ${2:-} == regions || ${2:-} == regions-fast ]]; then rounds=(1 2 3 4 5 6); fi
if [[ ${2:-} == isolation ]]; then rounds=(1 2 3 4); fi
if [[ ${2:-} == isolation-repeat ]]; then rounds=(5 6 7 8); fi
for round in "${rounds[@]}"; do
  modes=(native-default native stock cyclone zenoh)
  conditions=(nominal overflow)
  if [[ $round == 2 ]]; then modes=(zenoh cyclone stock native native-default); conditions=(overflow nominal); fi
  if ((round > 2)); then
    conditions=(nominal)
    modes=(native stock)
    if ((round % 2 == 0)); then modes=(stock native); fi
  fi
  if [[ ${2:-} == regions ]]; then
    if ((round <= 2)); then
      modes=(native stock cyclone zenoh)
      conditions=(control regions jitter)
      if ((round == 2)); then modes=(zenoh cyclone stock native); conditions=(jitter regions control); fi
    else
      conditions=(regions)
    fi
  fi
  if [[ ${2:-} == regions-fast ]]; then
    conditions=(fast)
    if ((round == 1)); then modes=(native stock cyclone zenoh); fi
    if ((round == 2)); then modes=(zenoh cyclone stock native); fi
  fi
  if [[ ${2:-} == isolation || ${2:-} == isolation-repeat ]]; then
    if ((round == 1)); then modes=(native stock cyclone zenoh); fi
    if ((round == 2)); then modes=(zenoh cyclone stock native); fi
    conditions=(healthy slow-near)
    if ((round % 2 == 0)); then conditions=(slow-near healthy); fi
  fi
  if [[ ${2:-} == depth ]]; then
    modes=(native stock cyclone zenoh)
    conditions=(5 8 16)
    if ((round == 2)); then modes=(zenoh cyclone stock native); conditions=(16 8 5); fi
    export BENCH_BUSY_NEAR=1
  fi
  for mode in "${modes[@]}"; do
    launch_mode=$mode
    if [[ $mode == native-default ]]; then
      launch_mode=native
      unset SYNC_SHORT_WAIT
    else
      export SYNC_SHORT_WAIT=1
    fi
    for condition in "${conditions[@]}"; do
      if [[ $mode == native-default && $condition == overflow ]]; then continue; fi
      unset SYNC_STALL_NEAR
      if [[ $condition == healthy || $condition == slow-near ]]; then export SYNC_DOUBLE_RATE=1 SYNC_ISOLATION=1; fi
      unset SYNC_REGIONS SYNC_EXTENDED SYNC_TOP_JITTER SYNC_SLOW_SINK
      if [[ $condition == slow-near ]]; then export SYNC_STALL_NEAR=1; fi
      if [[ $condition == healthy || $condition == slow-near ]]; then export SYNC_REGIONS=1; fi
      if [[ $condition == regions || $condition == jitter ]]; then export SYNC_REGIONS=1; fi
      if [[ $condition == fast ]]; then export SYNC_REGIONS=1 SYNC_DOUBLE_RATE=1; fi
      if [[ $condition == jitter ]]; then export SYNC_TOP_JITTER=1; fi
      if [[ $condition == overflow ]]; then export SYNC_EXTENDED=1 SYNC_TOP_JITTER=1 SYNC_SLOW_SINK=1; fi
      log="$out/$round-$mode-$condition.log"
      if [[ ${2:-} == depth ]]; then
        export BENCH_NEAR_DEPTH=$condition SYNC_STALL_NEAR=1 SYNC_REGIONS=1 SYNC_ISOLATION=1 SYNC_DOUBLE_RATE=1
        mkdir -p "$out/depth-$condition"
        log="$out/depth-$condition/$round-$mode-slow-near.log"
      fi
      if ! experiments/buffer/run-lyrical.sh timeout -k 5 65 bash -ec '
        TIMEFORMAT="graph_user_s=%U graph_sys_s=%S"
        time bash experiments/rmw-agnocast/run-pipeline.sh "$1"
        scope=$(sed -n "s,^0::,,p" /proc/self/cgroup)
        case "$scope" in */aw-buffer-*.scope) sed "s/^/scope_peak_bytes=/" "/sys/fs/cgroup$scope/memory.peak";; *) exit 2;; esac
      ' bash "$launch_mode" >"$log" 2>&1; then
        echo "$round $mode $condition failed" >&2
        failed=1
      fi
    done
  done
done
exit "$failed"
