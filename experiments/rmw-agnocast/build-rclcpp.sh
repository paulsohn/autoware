#!/usr/bin/env bash
set -e
w=build/rmw-agnocast
export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21
colcon --log-base "$w/log" build --base-paths src/rclcpp/rclcpp \
  --build-base "$w/obj" --install-base "$w/install" --executor sequential \
  --cmake-args -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 \
  -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG'
