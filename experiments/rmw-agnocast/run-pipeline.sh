#!/usr/bin/env bash
# Inside run-lyrical.sh. The launch service client uses the delegated ROS middleware.
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/rmw-agnocast/install/local_setup.bash
source build/rmw-agnocast/load-install/local_setup.bash
source build/universe-sync/install/local_setup.bash
source build/universe-pipeline/install/local_setup.bash
mode=${1:?plain, stock, runner, cyclone or zenoh}
if [[ $mode == plain ]]; then source build/universe-pipeline-plain/install/local_setup.bash; fi
if [[ $mode == native ]]; then source build/universe-pipeline-native/install/local_setup.bash; fi
dir=$(mktemp -d)
export ENABLE_AGNOCAST=0 ROS_LOG_DIR="$dir/roslog"
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
if [[ $mode == zenoh ]]; then
  export RMW_IMPLEMENTATION=rmw_zenoh_cpp
  /opt/ros/lyrical/lib/rmw_zenoh_cpp/rmw_zenohd &
  router=$!
  trap 'kill "$router" 2>/dev/null || true; wait "$router" || true' EXIT
  sleep 1
  kill -0 "$router"
fi
ros2 launch universe_sync_benchmark three_lidar.launch.py mode:="$mode" fixture_directory:="$dir"
