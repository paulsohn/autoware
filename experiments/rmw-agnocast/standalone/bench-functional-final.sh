#!/usr/bin/env bash
# Inside the stripped runtime, from the worktree root.
set -e
for test in pressure point path trajectory; do
  bash experiments/rmw-agnocast/run-loadable.sh --standalone "$test"
done
source build/agnocast-lyrical/install/local_setup.bash
source build/migration-guide/install/local_setup.bash
source build/standalone/o3-install/local_setup.bash
source build/standalone/release-path/install/local_setup.bash
source build/standalone/poll-install/local_setup.bash
export RMW_IMPLEMENTATION=rmw_agnocast ROS_DOMAIN_ID=173
export LD_PRELOAD=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
test ! -e /opt/ros/lyrical/lib/libddsc.so
talker=build/migration-guide/project-install/examples_rclcpp_minimal_publisher/lib/examples_rclcpp_minimal_publisher/publisher_member_function
python3 experiments/rmw-agnocast/standalone/check-stock.py "$talker"
for mode in basic ready reused drained; do
  export AGNOCAST_RMW_POLL_TOPIC=/poll_check_topic
  unset AGNOCAST_RMW_READY_ROUTE AGNOCAST_RMW_REUSE_WAIT_FD
  if [[ $mode != basic ]]; then export AGNOCAST_RMW_READY_ROUTE=1; fi
  if [[ $mode == reused ]]; then export AGNOCAST_RMW_REUSE_WAIT_FD=1; fi
  if [[ $mode == drained ]]; then export AGNOCAST_RMW_REUSE_WAIT_FD=drain; fi
  timeout -k 2 12 build/standalone/poll-probe/poll_check
done
