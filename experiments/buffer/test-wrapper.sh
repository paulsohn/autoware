#!/usr/bin/env bash
set -e
w=src/core/autoware_core/common/autoware_agnocast_wrapper
mkdir -p build/version-include/rclcpp build/lint-bin
ln -sf /opt/ros/lyrical/include/rclcpp/rclcpp/version.h build/version-include/rclcpp/version.h
printf '%s\n' '#!/bin/sh' 'exec /usr/bin/cppcheck --library=googletest "$@"' >build/lint-bin/cppcheck
chmod +x build/lint-bin/cppcheck
export AMENT_CPPCHECK_ALLOW_SLOW_VERSIONS=1 PATH="$PWD/build/lint-bin:$PATH"
colcon build --base-paths "$w" --executor sequential --cmake-args -DBUILD_TESTING=ON -Dament_cmake_cppcheck_ADDITIONAL_INCLUDE_DIRS="$PWD/build/version-include"
source install/local_setup.bash
colcon test --base-paths "$w" --executor sequential --return-code-on-test-failure
colcon test-result --test-result-base build/autoware_agnocast_wrapper
