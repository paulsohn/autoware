#!/usr/bin/env bash
# Inside the stripped Lyrical runtime, with the matched Release overlays.
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/rmw-agnocast/load-install/local_setup.bash
source build/universe-sync/install/local_setup.bash
source build/universe-pipeline/install/local_setup.bash
source build/migration-guide/install/local_setup.bash
source build/standalone/o3-install/local_setup.bash
source build/standalone/release-path/install/local_setup.bash
source build/standalone/poll-install/local_setup.bash
export RMW_IMPLEMENTATION=rmw_agnocast ROS_DOMAIN_ID=173 ENABLE_AGNOCAST=0
test ! -e /opt/ros/lyrical/lib/libddsc.so
test "$(ros2 pkg prefix rmw_agnocast)" = /ws/build/standalone/poll-install/rmw_agnocast
export LD_PRELOAD=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
ulimit -c 0
if [[ ${1:-} == --check ]]; then
  python3 experiments/rmw-agnocast/standalone/check-cloud.py
  exec bash experiments/rmw-agnocast/standalone/check.sh build/standalone/qualified-probe/topic_check
fi
dir=$(mktemp -d)
export ROS_LOG_DIR="$dir/log"
exec ros2 launch universe_sync_benchmark three_lidar.launch.py mode:=cyclone fixture_directory:="$dir"
