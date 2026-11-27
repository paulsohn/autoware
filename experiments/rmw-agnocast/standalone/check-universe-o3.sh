#!/usr/bin/env bash
# Reject old colcon fixture overlays before collecting samples.
set -eu
check() {
  local flags="$1/CMakeFiles/$2.dir/flags.make"
  if ! awk '/^CXX_FLAGS =/ {
    for (i = 3; i <= NF; ++i) if ($i ~ /^-O([0-9]+|g|s|z|fast)?$/) opt = $i
  } END {exit opt != "-O3"}' "$flags"; then
    echo "Timing requires -O3. Check or rebuild $flags" >&2
    exit 1
  fi
}
if [[ $1 == perception ]]; then
  check build/perception-replay/obj/universe_detection_replay cluster
  check build/perception-replay/obj/universe_detection_replay replay_cluster
  check build/tracker-replay/node-obj/autoware_multi_object_tracker autoware_multi_object_tracker
  check build/tracker-replay/node-obj/autoware_multi_object_tracker multi_object_tracker_node
fi
if [[ $1 == path ]]; then
  check build/standalone/release-cie/probe path_benchmark
  check build/standalone/release-cie/obj/callback_isolated_executor callback_isolated_executor
fi
if [[ ${2:-} == --cie-regression ]]; then
  check build/universe-pipeline/obj/universe_sync_benchmark sync_standalone_cie
fi
if [[ ${2:-} == --event-* ]]; then check build/standalone/event-cie callback_isolated_executor; fi
if [[ $1 == crop || ${2:-} == --cie-regression || ${2:-} == --crop-inputs || ${2:-} == --crop-repeat || ${2:-} == --stages || ${2:-} == --count || ${2:-} == --live-count ]]; then
  pkg=build/agnocast-lyrical/clang-obj/universe_crop_benchmark
  if [[ ${2:-} == --branched-cie || ${2:-} == --final-cie || ${2:-} == --ownership-cie || ${2:-} == --scope-* || ${2:-} == --route-* || ${2:-} == --source-* || ${2:-} == --reader-route-* || ${2:-} == --event-* || ${2:-} == --hybrid-* || ${2:-} == --input-loan-* ]]; then check "$pkg" crop_standalone_cie; fi
  check "$pkg" universe_crop
  if [[ $1 == crop ]]; then
    for target in crop endpoint universe_endpoint; do check "$pkg" "$target"; done
  fi
fi
if [[ $1 == lidar ]]; then
  for variant in universe-pipeline universe-pipeline-native; do
    obj="build/$variant/obj"
    check "$obj/universe_crop_benchmark" universe_crop
    for target in universe_sync sync_fixture concatenate_and_time_sync_node; do
      check "$obj/universe_sync_benchmark" "$target"
    done
  done
  check build/universe-pipeline-native/obj/universe_crop_benchmark native_crop_container
fi
if [[ -n ${SYNC_REHEARSAL_WORKSPACE:-} ]]; then
  rehearsal=${SYNC_REHEARSAL_WORKSPACE#/ws/}
  check "$rehearsal/build/rmw_agnocast" rmw_agnocast
  check "$rehearsal/build/rclcpp" rclcpp
  check "$rehearsal/build/agnocastlib" agnocast
else
  check build/standalone/poll-obj/rmw_agnocast rmw_agnocast
fi
check build/standalone/o3-obj/agnocastlib agnocast
check build/standalone/release-path/obj/rclcpp rclcpp
