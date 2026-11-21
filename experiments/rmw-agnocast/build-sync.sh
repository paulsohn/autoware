#!/usr/bin/env bash
set -e
source build/agnocast-lyrical/install/local_setup.bash
w=build/universe-sync
export ENABLE_AGNOCAST=0 CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21
colcon --log-base "$w/log" build --base-paths src/core/autoware_msgs/autoware_sensing_msgs src/universe_sync_benchmark \
  --build-base "$w/obj" --install-base "$w/install" --executor sequential \
  --cmake-args -DBUILD_TESTING=OFF -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 \
  -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG'
