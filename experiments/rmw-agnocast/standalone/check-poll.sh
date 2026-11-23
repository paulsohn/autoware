#!/usr/bin/env bash
# Run in the guarded Lyrical shell with the Cyclone-stripped distro mounted.
set -e
source build/migration-guide/install/local_setup.bash
source build/standalone/o3-install/local_setup.bash
source build/standalone/release-path/install/local_setup.bash
source build/standalone/poll-install/local_setup.bash
export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21 RMW_IMPLEMENTATION=rmw_zenoh_cpp
dir=build/standalone/poll-probe
cmake -S experiments/rmw-agnocast/standalone -B "$dir" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15
cmake --build "$dir" --target poll_check -j1
export RMW_IMPLEMENTATION=rmw_agnocast ROS_DOMAIN_ID=176
test "$(ros2 pkg prefix rmw_agnocast)" = /ws/build/standalone/poll-install/rmw_agnocast
test ! -e /opt/ros/lyrical/lib/libddsc.so
test ! -e /opt/ros/lyrical/lib/librmw_cyclonedds_cpp.so
export LD_PRELOAD=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
export AGNOCAST_RMW_POLL_TOPIC=/poll_check_topic
timeout -k 2 12 "$dir/poll_check"
unset AGNOCAST_RMW_POLL_TOPIC
bash experiments/rmw-agnocast/standalone/check.sh build/standalone/qualified-probe/topic_check
