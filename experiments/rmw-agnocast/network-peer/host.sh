#!/usr/bin/env bash
# Inside the guarded host-side Lyrical root.
set -e
mode=$1
out=$(realpath "$2")
shift 2
export CYCLONEDDS_URI=file://"$out/../host.xml"
source build/agnocast-lyrical/install/local_setup.bash
source build/rmw-agnocast/install/local_setup.bash
source build/rmw-agnocast/load-install/local_setup.bash
source build/universe-sync/install/local_setup.bash
source build/universe-pipeline/install/local_setup.bash
export ROS_LOG_DIR="$out/ros"
if [[ $mode == cli ]]; then exec ros2 "$@"; fi
if [[ $mode == verify ]]; then exec python3 experiments/rmw-agnocast/network-peer/readback.py "$out" "$@"; fi
bin=$(ros2 pkg prefix agnocast_rmw_loan_probe)/lib/agnocast_rmw_loan_probe
hook=$(ros2 pkg prefix agnocastlib)/lib/libagnocast_heaphook.so
native=(env LD_PRELOAD="$hook" RMW_IMPLEMENTATION=rmw_agnocast AGNOCAST_RMW_HOST_CLOUD=1 AGNOCAST_BRIDGE_MODE=OFF AGNOCAST_NO_DISCOVERY_AGENT=1)
relay=
finish() {
  if [[ -n $relay ]]; then
    awk -v hz="$(getconf CLK_TCK)" '{print ($14+$15)/hz}' "/proc/$relay/stat" >relay-cpu-seconds
    kill -INT "$relay" 2>/dev/null || true
    wait "$relay"
  fi
}
trap finish EXIT
cd "$out"
if [[ $mode == ingress ]]; then
  "${native[@]}" "$bin/route_bridge" r2a auto /peer/poses /local/poses >relay.log 2>&1 &
  relay=$!
  "${native[@]}" AGNOCAST_RMW_SUBSCRIBE_TOPICS=/local/poses "$bin/type_fallback_peer" reader poses /local/poses
  exit
fi
if [[ $mode != unrouted ]]; then
  "${native[@]}" AGNOCAST_BRIDGE_DEMAND=1 AGNOCAST_BRIDGE_PROFILE="$out/forward.csv" "$bin/route_bridge" a2r cloud /output /dds/synchronized >relay.log 2>&1 &
  relay=$!
fi
SYNC_ROUTE_PILOT=1 SYNC_RECORDER_LIVE=1 SYNC_AUDIT=1 ENABLE_AGNOCAST=0 ros2 launch universe_sync_benchmark three_lidar.launch.py mode:=stock fixture_directory:="$out" >graph.log 2>&1
