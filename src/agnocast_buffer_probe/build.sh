#!/usr/bin/env bash
set -e
w=build/agnocast-lyrical
source "$w/install/local_setup.bash"
export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21
colcon --log-base "$w/log" build --base-paths src/agnocast_buffer_probe \
  --build-base "$w/probe-obj" --install-base "$w/probe-install" --executor sequential \
  --cmake-args -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 \
  -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG'
