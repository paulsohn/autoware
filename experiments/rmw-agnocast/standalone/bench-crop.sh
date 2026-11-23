#!/usr/bin/env bash
set -eu
out=${1:?fresh output directory}
bash "$(dirname "$0")/check-universe-o3.sh" crop "${2:-}"
mkdir "$out"
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)" BUFFER_RUNTIME_ONLY=1
batch=${2:-}
cie=0
case "$batch" in --ownership-cie|--scope-*|--route-*|--source-*|--reader-route-*|--event-*|--hybrid-*|--input-loan-*) cie=1;; esac
unset BENCH_CROP_PROFILE CROP_READER_TIMING CROP_SOURCE_TIMING AGNOCAST_RMW_POLL_TOPIC AGNOCAST_RMW_READY_ROUTE AGNOCAST_RMW_REUSE_WAIT_FD
rounds=(1 2)
if [[ ${2:-} == --ownership || ${2:-} == --ownership-cie ]]; then rounds=(1 2 3 4); fi
if [[ ${2:-} == --scope-cie || $batch == --hybrid-cie || $batch == --input-loan-cie ]]; then rounds=(1 2 3 4); fi
if [[ ${2:-} == --route-cie || ${2:-} == --source-cie || $batch == --reader-route-cie || $batch == --event-cie ]]; then rounds=(1 2 3 4 5 6); fi
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
  if [[ $batch == --scope-* ]]; then
    case $round in
      1) modes=(agnocast view scoped loan);;
      2) modes=(loan scoped view agnocast);;
      3) modes=(view loan agnocast scoped);;
      4) modes=(scoped agnocast loan view);;
    esac
  fi
  if [[ ${2:-} == --route-* || ${2:-} == --source-* || $batch == --reader-route-* || $batch == --event-* ]]; then
    tuned=direct
    if [[ ${2:-} == --source-* ]]; then tuned=source; fi
    if [[ $batch == --reader-route-* ]]; then tuned=cropdirect; fi
    if [[ $batch == --event-* ]]; then tuned=event; fi
    case $round in
      1) modes=(agnocast view "$tuned");;
      2) modes=("$tuned" view agnocast);;
      3) modes=(view agnocast "$tuned");;
      4) modes=("$tuned" agnocast view);;
      5) modes=(agnocast "$tuned" view);;
      6) modes=(view "$tuned" agnocast);;
    esac
  fi
  if [[ $batch == --hybrid-* || $batch == --input-loan-* ]]; then
    single=cropdirect
    other=scoped
    if [[ $batch == --input-loan-* ]]; then single=sourcedirect; other=source; fi
    case $round in
      1) modes=(agnocast hybrid "$single" "$other");;
      2) modes=(hybrid "$other" agnocast "$single");;
      3) modes=("$other" "$single" hybrid agnocast);;
      4) modes=("$single" agnocast "$other" hybrid);;
    esac
  fi
  if [[ $batch == --source-reader-trace || $batch == --reader-route-trace ]]; then export CROP_READER_TIMING=1; fi
  if [[ $batch == --source-trace || $batch == --source-reader-trace || $batch == --reader-route-trace ]]; then export CROP_SOURCE_TIMING=1; fi
  for mode in "${modes[@]}"; do
    if [[ $batch == *-trace ]]; then export BENCH_CROP_PROFILE="/ws/$out/$mode-$round.profile"; fi
    isolate=()
    transport=$mode
    if [[ $mode != agnocast && $mode != cyclone && $mode != zenoh ]]; then
      transport=standalone
      isolate=(bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp
        --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws)
    fi
    experiments/buffer/run-lyrical.sh "${isolate[@]}" timeout -k 5 40 bash -ec '
      source build/agnocast-lyrical/install/local_setup.bash
      source build/standalone/o3-install/local_setup.bash
      source build/standalone/release-path/install/local_setup.bash
      export ROS_DOMAIN_ID=173
      export BENCH_HEAPHOOK=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
      arm=$4
      opts=()
      if [[ $arm == standalone ]]; then
        if [[ $3 == --ownership || $3 == --ownership-cie ]]; then opts+=(--typed-input); fi
        if (( $5 )); then
          source build/standalone/release-cie/install/local_setup.bash
          opts+=(--standalone-cie /ws/build/agnocast-lyrical/clang-obj/universe_crop_benchmark/crop_standalone_cie)
        fi
        if [[ $1 == source || $1 == sourcedirect ]]; then opts+=(--source-loan-only); export ROS_DISABLE_LOANED_MESSAGES=0; fi
        if [[ $1 == event ]]; then opts+=(--host-view --cie-worker-library /ws/build/standalone/event-cie/libcallback_isolated_executor.so); fi
        if [[ $1 == cropdirect ]]; then opts+=(--host-view --direct-crop-wait); fi
        if [[ $1 == direct ]]; then opts+=(--host-view --direct-cloud-waits); fi
        if [[ $1 == view ]]; then opts+=(--host-view); fi
        if [[ $1 == hybrid || $1 == sourcedirect ]]; then opts+=(--direct-crop-wait); fi
        if [[ $1 == scoped || $1 == hybrid ]]; then opts+=(--payload-scope); export ROS_DISABLE_LOANED_MESSAGES=0; fi
        if [[ $1 == loan ]]; then opts+=(--host-loan); export ROS_DISABLE_LOANED_MESSAGES=0; fi
        source build/standalone/poll-install/local_setup.bash
        export RMW_IMPLEMENTATION=rmw_agnocast
        test ! -e /opt/ros/lyrical/lib/libddsc.so
        test "$(ros2 pkg prefix rmw_agnocast)" = /ws/build/standalone/poll-install/rmw_agnocast
      fi
      python3 src/universe_crop_benchmark/run_case.py "$arm" "$2.csv" "${opts[@]}" --resources "$2-resources.csv"
    ' bash "$mode" "$out/$mode-$round" "$batch" "$transport" "$cie" >"$out/$mode-$round.log" 2>&1 || { tail -25 "$out/$mode-$round.log"; exit 1; }
    if [[ $cie == 1 && $transport == standalone ]]; then
      grep -q 'CIE benchmark announcement KeepLast(16)' "$out/$mode-$round.log"
    fi
  done
done
