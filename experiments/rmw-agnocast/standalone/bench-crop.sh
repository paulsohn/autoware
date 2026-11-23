#!/usr/bin/env bash
set -eu
out=${1:?fresh output directory}
mkdir "$out"
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)" BUFFER_RUNTIME_ONLY=1
for round in 1 2; do
  modes=(agnocast standalone cyclone zenoh)
  if [[ $round == 2 ]]; then modes=(zenoh cyclone standalone agnocast); fi
  for mode in "${modes[@]}"; do
    isolate=()
    if [[ $mode == standalone ]]; then
      isolate=(bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp
        --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws)
    fi
    experiments/buffer/run-lyrical.sh "${isolate[@]}" timeout -k 5 40 bash -ec '
      source build/agnocast-lyrical/install/local_setup.bash
      source build/standalone/o3-install/local_setup.bash
      source build/standalone/release-path/install/local_setup.bash
      export ROS_DOMAIN_ID=173
      export BENCH_HEAPHOOK=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
      if [[ $1 == standalone ]]; then
        source build/standalone/poll-install/local_setup.bash
        export RMW_IMPLEMENTATION=rmw_agnocast
        test ! -e /opt/ros/lyrical/lib/libddsc.so
        test "$(ros2 pkg prefix rmw_agnocast)" = /ws/build/standalone/poll-install/rmw_agnocast
      fi
      python3 src/universe_crop_benchmark/run_case.py "$1" "$2.csv" --resources "$2-resources.csv"
    ' bash "$mode" "$out/$mode-$round" >"$out/$mode-$round.log" 2>&1 || { tail -25 "$out/$mode-$round.log"; exit 1; }
  done
done
