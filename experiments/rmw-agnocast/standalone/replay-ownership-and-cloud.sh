#!/usr/bin/env bash
# Run inside the stripped Lyrical shell from standalone/README.md.
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/universe-sync/install/local_setup.bash
source build/rmw-agnocast/msg-install/local_setup.bash
source build/migration-guide/install/local_setup.bash
source build/standalone/qualified-install/local_setup.bash
export RMW_IMPLEMENTATION=rmw_agnocast ROS_DOMAIN_ID=173
test ! -e /opt/ros/lyrical/lib/libddsc.so
test ! -e /opt/ros/lyrical/lib/librmw_cyclonedds_cpp.so
test "$(ros2 pkg prefix rmw_agnocast)" = /ws/build/standalone/qualified-install/rmw_agnocast
probe=/ws/build/rmw-agnocast/load-install/agnocast_rmw_loan_probe/lib/agnocast_rmw_loan_probe/loadable_probe
hook=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
ulimit -c 0
for type in trajectory point path; do
  for mode in loan copy; do
    (
      cd "$(mktemp -d)"
      disabled=0
      if [[ $mode == copy ]]; then disabled=1; fi
      timeout -k 2 18 env LD_PRELOAD="$hook" ROS_DISABLE_LOANED_MESSAGES="$disabled" "$probe" reader "$mode" "$type" &
      reader=$!
      trap 'kill "$reader" 2>/dev/null || true; wait "$reader" || true' EXIT
      timeout -k 2 18 env LD_PRELOAD="$hook" ROS_DISABLE_LOANED_MESSAGES=0 "$probe" writer "$mode" "$type"
      wait "$reader"
      trap - EXIT
    )
  done
done
sync=$(ros2 pkg prefix universe_sync_benchmark)/lib/universe_sync_benchmark/concatenate_and_time_sync_node
config=src/universe/autoware_universe/sensing/autoware_pointcloud_preprocessor/config/concatenate_and_time_sync_node.param.yaml
exec timeout -k 2 15 env LD_PRELOAD="$hook" "$sync" --ros-args --params-file "$config" -p 'input_topics:=[/sync/left,/sync/right,/sync/top]' -p is_motion_compensated:=false -p publish_synchronized_pointcloud:=false -p timeout_sec:=0.04
