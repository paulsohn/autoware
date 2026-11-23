#!/usr/bin/env bash
set -e
source build/migration-guide/install/local_setup.bash
source build/standalone/o3-install/local_setup.bash
source build/standalone/release-path/install/local_setup.bash
export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21 RMW_IMPLEMENTATION=rmw_zenoh_cpp
colcon --log-base build/standalone/poll-log build --build-base build/standalone/poll-obj \
  --install-base build/standalone/poll-install --paths src/rmw_agnocast --executor sequential \
  --cmake-args -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG" -DBUILD_TESTING=OFF -DRMW_IMPLEMENTATION=rmw_zenoh_cpp \
  -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15
