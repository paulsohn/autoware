#!/usr/bin/env bash
set -eu
out=${1:?fresh output directory}
bash "$(dirname "$0")/check-universe-o3.sh" lidar "${2:-}"
mkdir "$out"
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)" BUFFER_RUNTIME_ONLY=1 SYNC_SHORT_WAIT=1
rounds=(1 2)
if [[ ${2:-} == --crop-inputs || ${2:-} == --stages ]]; then rounds=(1 2 3 4); fi
if [[ ${2:-} == --crop-repeat ]]; then rounds=(5 6 7 8); fi
for round in "${rounds[@]}"; do
  modes=(native standalone poll cyclone zenoh)
  if [[ $round == 2 ]]; then modes=(zenoh cyclone poll standalone native); fi
  if [[ ${2:-} == --ownership ]]; then
    modes=(native standalone view loan cyclone zenoh)
    if [[ $round == 2 ]]; then modes=(zenoh cyclone loan view standalone native); fi
  fi
  if [[ ${2:-} == --crop-inputs ]]; then
    case $round in
      1) modes=(native crop-copy crop-view cyclone zenoh);;
      2) modes=(zenoh cyclone crop-view crop-copy native);;
      3) modes=(crop-view crop-copy native);;
      4) modes=(native crop-copy crop-view);;
    esac
  fi
  if [[ ${2:-} == --crop-repeat ]]; then
    modes=(crop-copy crop-view)
    if [[ $round == 6 || $round == 7 ]]; then modes=(crop-view crop-copy); fi
  fi
  if [[ ${2:-} == --stages ]]; then
    case $round in
      1) modes=(native crop-view cyclone zenoh crop-view-trace cyclone-trace);;
      2) modes=(cyclone-trace crop-view-trace zenoh cyclone crop-view native);;
      3) modes=(cyclone crop-view native cyclone-trace crop-view-trace);;
      4) modes=(crop-view-trace cyclone-trace native crop-view cyclone);;
    esac
  fi
  if [[ ${2:-} == --count ]]; then
    modes=(crop-view-count crop-view-reuse cyclone-count cyclone-reuse)
    if [[ $round == 2 ]]; then modes=(cyclone-reuse cyclone-count crop-view-reuse crop-view-count); fi
  fi
  if [[ ${2:-} == --live-count ]]; then
    modes=(crop-view-count cyclone-count)
    if [[ $round == 2 ]]; then modes=(cyclone-count crop-view-count); fi
  fi
  for mode in "${modes[@]}"; do
    log="$out/$mode-$round.log"
    unset AGNOCAST_RMW_POLL_TOPIC SYNC_STANDALONE_OWNERSHIP SYNC_STAGE_TRACE SYNC_TYPED_CROP_CONTROL
    unset SYNC_COUNT_PROBE SYNC_COUNT_REUSE
    if [[ ${2:-} == --count || ${2:-} == --live-count ]]; then
      export SYNC_COUNT_PROBE=/ws/build/standalone/crop-count.so
      if [[ $mode == *-reuse ]]; then export SYNC_COUNT_REUSE=1; fi
      mode=${mode%-*}
    fi
    if [[ $mode == *-trace ]]; then export SYNC_STAGE_TRACE=1; mode=${mode%-trace}; fi
    if [[ ( ${2:-} == --stages || ${2:-} == --count || ${2:-} == --live-count ) && $mode != native ]]; then
      export SYNC_TYPED_CROP_CONTROL=1
      export SYNC_CROP_COMPONENT_PREFIX=/ws/build/agnocast-lyrical/install/universe_crop_benchmark
    fi
    if [[ $mode == crop-* ]]; then
      export SYNC_STANDALONE_OWNERSHIP=$mode
      export SYNC_CROP_COMPONENT_PREFIX=/ws/build/agnocast-lyrical/install/universe_crop_benchmark
    fi
    if [[ $mode == crop-* || $mode == standalone || $mode == poll || $mode == view || $mode == loan ]]; then
      if [[ $mode == view || $mode == loan ]]; then export SYNC_STANDALONE_OWNERSHIP=$mode; fi
      if [[ $mode == poll ]]; then export AGNOCAST_RMW_POLL_TOPIC=/output; fi
      command=(bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp
        --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws timeout -k 8 65
        bash experiments/rmw-agnocast/standalone/run-universe.sh)
    else
      command=(env UNIVERSE_RELEASE_CONTROLS=1 timeout -k 8 65 bash experiments/rmw-agnocast/run-pipeline.sh "$mode")
    fi
    if ! experiments/buffer/run-lyrical.sh "${command[@]}" >"$log" 2>&1; then tail -30 "$log"; exit 1; fi
    if [[ $mode == view || $mode == loan ]]; then grep -q 'sync_input views=60 copies=0' "$log"; fi
    if [[ $mode == crop-* ]]; then
      grep -q 'sync_input views=60 copies=0' "$log"
      grep -Fq "$SYNC_CROP_COMPONENT_PREFIX/lib/libuniverse_crop.so" "$log"
      grep -q 'verified 24 synchronized clouds and metadata' "$log"
    fi
    if [[ $mode == loan ]]; then grep -q 'host-cloud publisher loans=24 private_copies=0' "$log"; fi
    if [[ ( ${2:-} == --stages || ${2:-} == --count || ${2:-} == --live-count ) && $mode != native ]]; then
      grep -Fq "$SYNC_CROP_COMPONENT_PREFIX/lib/libuniverse_crop.so" "$log"
      grep -q 'verified 24 synchronized clouds and metadata' "$log"
    fi
    if [[ ${2:-} == --count || ${2:-} == --live-count ]]; then test "$(grep -c 'crop_count,' "$log")" = 60; fi
    test "$(grep -c 'sample,[0-9]' "$log")" = 24
  done
done
