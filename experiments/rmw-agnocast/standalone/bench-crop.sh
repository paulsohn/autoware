#!/usr/bin/env bash
set -eu
out=${1:?fresh output directory}
bash "$(dirname "$0")/check-universe-o3.sh" crop "${2:-}"
mkdir "$out"
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)" BUFFER_RUNTIME_ONLY=1
rounds=(1 2)
if [[ ${2:-} == --ownership || ${2:-} == --ownership-cie ]]; then rounds=(1 2 3 4); fi
for round in "${rounds[@]}"; do
  modes=(agnocast standalone cyclone zenoh)
  if [[ $round == 2 ]]; then modes=(zenoh cyclone standalone agnocast); fi
  if [[ ${2:-} == --ownership || ${2:-} == --ownership-cie ]]; then
    case $round in
      1) modes=(agnocast standalone view loan);;
      2) modes=(standalone loan agnocast view);;
      3) modes=(loan view standalone agnocast);;
      4) modes=(view agnocast loan standalone);;
    esac
  fi
  for mode in "${modes[@]}"; do
    isolate=()
    if [[ $mode == standalone || $mode == view || $mode == loan ]]; then
      isolate=(bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp
        --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws)
    fi
    experiments/buffer/run-lyrical.sh "${isolate[@]}" timeout -k 5 40 bash -ec '
      source build/agnocast-lyrical/install/local_setup.bash
      source build/standalone/o3-install/local_setup.bash
      source build/standalone/release-path/install/local_setup.bash
      export ROS_DOMAIN_ID=173
      export BENCH_HEAPHOOK=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
      arm=$1
      opts=()
      if [[ $1 == view || $1 == loan || $1 == standalone ]]; then
        arm=standalone
        if [[ $3 == --ownership || $3 == --ownership-cie ]]; then opts+=(--typed-input); fi
        if [[ $3 == --ownership-cie ]]; then
          source build/standalone/release-cie/install/local_setup.bash
          opts+=(--standalone-cie /ws/build/agnocast-lyrical/clang-obj/universe_crop_benchmark/crop_standalone_cie)
        fi
        if [[ $1 == view ]]; then opts+=(--host-view); fi
        if [[ $1 == loan ]]; then opts+=(--host-loan); export ROS_DISABLE_LOANED_MESSAGES=0; fi
        source build/standalone/poll-install/local_setup.bash
        export RMW_IMPLEMENTATION=rmw_agnocast
        test ! -e /opt/ros/lyrical/lib/libddsc.so
        test "$(ros2 pkg prefix rmw_agnocast)" = /ws/build/standalone/poll-install/rmw_agnocast
      fi
      python3 src/universe_crop_benchmark/run_case.py "$arm" "$2.csv" "${opts[@]}" --resources "$2-resources.csv"
    ' bash "$mode" "$out/$mode-$round" "${2:-}" >"$out/$mode-$round.log" 2>&1 || { tail -25 "$out/$mode-$round.log"; exit 1; }
    if [[ ${2:-} == --ownership-cie && $mode != agnocast ]]; then
      grep -q 'CIE benchmark announcement KeepLast(16)' "$out/$mode-$round.log"
    fi
  done
done
