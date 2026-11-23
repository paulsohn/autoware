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
if [[ -n ${SYNC_TYPED_CROP_CONTROL:-} ]]; then
  deps=$(ldd "$SYNC_CROP_COMPONENT_PREFIX/lib/libuniverse_crop.so")
  grep -Fq 'librclcpp.so => /ws/build/standalone/release-path/install/rclcpp/lib/' <<< "$deps"
  grep -Fq 'libagnocast.so => /ws/build/standalone/o3-install/agnocastlib/lib/' <<< "$deps"
  grep -q 'not found' <<< "$deps" && exit 1
fi
export LD_PRELOAD=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
ulimit -c 0
if [[ ${1:-} == --ownership ]]; then
  probe=/ws/build/rmw-agnocast/load-install/agnocast_rmw_loan_probe/lib/agnocast_rmw_loan_probe/loadable_probe
  export AGNOCAST_RMW_HOST_VIEW=1 AGNOCAST_VIEW_ADDRESS_FILE=view-address ROS_DISABLE_LOANED_MESSAGES=0
  for mode in view loan; do
    cd "$(mktemp -d)"
    if [[ $mode == loan ]]; then export AGNOCAST_RMW_HOST_LOAN=1; fi
    "$probe" reader host-cloud &
    reader=$!
    trap 'kill "$reader" 2>/dev/null || true' EXIT
    "$probe" writer host-cloud
    wait "$reader"
    trap - EXIT
  done
  exit 0
fi
if [[ ${1:-} == --check ]]; then
  python3 experiments/rmw-agnocast/standalone/check-matches.py
  python3 experiments/rmw-agnocast/standalone/check-cloud.py
  exec bash experiments/rmw-agnocast/standalone/check.sh build/standalone/qualified-probe/topic_check
fi
dir=$(mktemp -d)
export ROS_LOG_DIR="$dir/log"
exec ros2 launch /ws/src/universe_sync_benchmark/launch/three_lidar.launch.py mode:=cyclone fixture_directory:="$dir"
