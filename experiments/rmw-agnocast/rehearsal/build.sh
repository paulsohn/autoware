#!/usr/bin/env bash
# Inside a clean Lyrical userspace. OUT is a fresh directory with pinned source inputs.
set -e
out=$(realpath "${1:?supply the staged workspace}")
source /opt/ros/lyrical/setup.bash
export CC=clang-21 CXX=clang++-21 CMAKE_BUILD_PARALLEL_LEVEL=1 MAKEFLAGS=-j1 PYTHON_CPU_COUNT=1
export XML_CATALOG_FILES="$out/schema/catalog.xml" RMW_IMPLEMENTATION=rmw_zenoh_cpp
cd "$out"
pkgs=(rclcpp agnocast_cie_config_msgs agnocast_cie_thread_configurator agnocastlib rmw_agnocast
      rmw_agnocast_discovery_msgs agnocast_cyclone_gateway
      examples_rclcpp_minimal_publisher examples_rclcpp_minimal_subscriber)
colcon build --base-paths src --packages-select "${pkgs[@]}" --executor sequential \
    --cmake-args -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15
source install/local_setup.bash
for p in "${pkgs[@]}"; do test "$(ros2 pkg prefix "$p")" = "$out/install/$p"; done
