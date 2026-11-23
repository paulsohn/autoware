#!/usr/bin/env bash
set -eu
out=${1:?fresh output directory}
mkdir "$out"
bash experiments/rmw-agnocast/standalone/check-universe-o3.sh lidar --cie-regression
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)" BUFFER_RUNTIME_ONLY=1 SYNC_SHORT_WAIT=1
condition=${2:-saturation}
rounds=(1 2)
case "$condition" in
  saturation)
    rounds=(1 2 3 4)
    export SYNC_DOUBLE_RATE=1 SYNC_REGIONS=1 SYNC_STALL_NEAR=1 SYNC_ISOLATION=1
    export BENCH_NEAR_DEPTH=8 BENCH_COMPETITOR=1 BENCH_SECOND_TIMER=1 BENCH_NEAR_CPUS=${BENCH_NEAR_CPUS:-2,3,4};;
  extended) export SYNC_EXTENDED=1 SYNC_TOP_JITTER=1;;
  overflow) export SYNC_EXTENDED=1 SYNC_TOP_JITTER=1 SYNC_SLOW_SINK=1;;
  jitter) export SYNC_REGIONS=1 SYNC_TOP_JITTER=1;;
  regions) export SYNC_REGIONS=1;;
  regions-fast) export SYNC_REGIONS=1 SYNC_DOUBLE_RATE=1;;
  slow|busy|depth-*|fresh-*)
    export SYNC_DOUBLE_RATE=1 SYNC_REGIONS=1 SYNC_STALL_NEAR=1 SYNC_ISOLATION=1
    case $condition in
      busy|depth-*|fresh-*) export BENCH_BUSY_NEAR=1;;
    esac
    case $condition in
      depth-*) export BENCH_NEAR_DEPTH=${condition#depth-};;
      fresh-*) export BENCH_NEAR_DEPTH=8 BENCH_NEAR_POLICY=${condition#fresh-};;
    esac;;
  *) echo "Unknown pressure condition $condition" >&2; exit 2;;
esac
export SYNC_NEAR_CIE_EXECUTABLE=/ws/build/agnocast-lyrical/clang-obj/universe_crop_benchmark/crop_standalone_cie
failed=0
for round in "${rounds[@]}"; do
  case $round in
    1) modes=(native standalone cyclone zenoh);;
    2) modes=(zenoh cyclone standalone native);;
    3) modes=(standalone native);;
    4) modes=(native standalone);;
  esac
  for mode in "${modes[@]}"; do
    unset SYNC_CIE_EXECUTABLE SYNC_TYPED_CROP_CONTROL SYNC_STANDALONE_OWNERSHIP
    if [[ $mode != native ]]; then
      export SYNC_TYPED_CROP_CONTROL=1 SYNC_CROP_COMPONENT_PREFIX=/ws/build/agnocast-lyrical/install/universe_crop_benchmark
    fi
    command=(env UNIVERSE_RELEASE_CONTROLS=1 bash experiments/rmw-agnocast/run-pipeline.sh "$mode")
    if [[ $mode == standalone ]]; then
      export SYNC_CIE_EXECUTABLE=/ws/build/universe-pipeline/obj/universe_sync_benchmark/sync_standalone_cie
      export SYNC_STANDALONE_OWNERSHIP=crop-view
      command=(bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws bash experiments/rmw-agnocast/standalone/run-universe.sh)
    fi
    log="$out/$mode-$round.log"
    experiments/buffer/run-lyrical.sh bash -c 'TIMEFORMAT="graph_cpu %U %S"; time timeout -k 8 65 "$@"' bash "${command[@]}" >"$log" 2>&1 || { tail -25 "$log"; failed=1; continue; }
    if [[ $condition == overflow || $condition == extended ]]; then
      grep -q 'extended expected=' "$log" || failed=1
    else
      grep -Eq 'verified [0-9]+ synchronized clouds and metadata' "$log" || failed=1
    fi
    if [[ $condition == saturation ]]; then
      grep -q 'competitor_cpu_ns=' "$log" || failed=1
      grep -q 'second_cpu_ns=' "$log" || failed=1
    fi
  done
done
exit "$failed"
