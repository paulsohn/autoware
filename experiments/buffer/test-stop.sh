#!/usr/bin/env bash
set -e
p=src/core/autoware_core/localization/autoware_stop_filter
export AMENT_CPPCHECK_ALLOW_SLOW_VERSIONS=1 PATH="$PWD/build/lint-bin:$PATH"
colcon build --base-paths "$p" --executor sequential --cmake-args -DBUILD_TESTING=ON
source install/local_setup.bash
colcon test --base-paths "$p" --executor sequential --return-code-on-test-failure
colcon test-result --test-result-base build/autoware_stop_filter
for mode in standalone component; do python3 src/buffer_pointcloud_probe/check_stop.py "$mode"; done
