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
if [[ ${2:-} == --cie-regression ]]; then
  check build/universe-pipeline/obj/universe_sync_benchmark sync_standalone_cie
fi
if [[ $1 == crop || ${2:-} == --cie-regression || ${2:-} == --crop-inputs || ${2:-} == --crop-repeat || ${2:-} == --stages || ${2:-} == --count || ${2:-} == --live-count ]]; then
  pkg=build/agnocast-lyrical/clang-obj/universe_crop_benchmark
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
check build/standalone/poll-obj/rmw_agnocast rmw_agnocast
check build/standalone/o3-obj/agnocastlib agnocast
check build/standalone/release-path/obj/rclcpp rclcpp
