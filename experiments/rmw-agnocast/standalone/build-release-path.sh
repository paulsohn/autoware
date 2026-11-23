#!/usr/bin/env bash
# Run inside the guarded Lyrical shell, with the two o3-install transport libraries available.
set -e
source build/migration-guide/install/local_setup.bash
source build/standalone/o3-install/local_setup.bash
export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21 RMW_IMPLEMENTATION=rmw_zenoh_cpp
args=(-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DRMW_IMPLEMENTATION=rmw_zenoh_cpp
      -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15)
w=build/standalone/release-path
colcon --log-base "$w/log" build --build-base "$w/obj" --install-base "$w/install" --paths src/rclcpp/rclcpp --executor sequential --cmake-args "${args[@]}"
source "$w/install/local_setup.bash"
cmake -S src/agnocast_rmw_loan_probe -B "$w/probe" "${args[@]}"
cmake --build "$w/probe" --target path_benchmark -j1
