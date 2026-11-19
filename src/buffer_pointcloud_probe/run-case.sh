#!/usr/bin/env bash
# Run in a sourced Lyrical workspace on an unused ROS domain.
set -euo pipefail
backend=${1:?cuda or shmem}
mode=${2:-native}
[[ $backend == cuda || $backend == shmem ]]
[[ $mode == native || $mode == mixed ]]
export RMW_IMPLEMENTATION=rmw_zenoh_cpp
probe="$(ros2 pkg prefix buffer_pointcloud_probe)/lib/buffer_pointcloud_probe/probe"
router="$(ros2 pkg prefix rmw_zenoh_cpp)/lib/rmw_zenoh_cpp/rmw_zenohd"
children=()
cleanup() {
  for pid in "${children[@]}"; do kill "$pid" 2>/dev/null || true; done
  wait || true
}
trap cleanup EXIT
"$router" &
children+=("$!")
sleep 1
kill -0 "${children[0]}"
peers=1
if [[ $mode == mixed ]]; then
  peers=2
  "$probe" sub cpu "$peers" &
  children+=("$!")
fi
"$probe" sub "$backend" "$peers" &
children+=("$!")
"$probe" pub "$backend" "$peers"
for pid in "${children[@]:1}"; do wait "$pid"; done
