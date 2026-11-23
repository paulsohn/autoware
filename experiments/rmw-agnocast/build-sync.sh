#!/usr/bin/env bash
set -e
source build/agnocast-lyrical/install/local_setup.bash
if [[ ${AUTOWARE_ENABLE_AGNOCAST_HOST_VIEW:-ON} == ON ]]; then
  source build/rmw-agnocast/load-install/local_setup.bash
fi
w=build/universe-sync
export ENABLE_AGNOCAST=0
if [[ ${SYNC_NATIVE:-OFF} == ON ]]; then
  w=build/universe-sync-native
  export ENABLE_AGNOCAST=1
fi
export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21
colcon --log-base "$w/log" build --base-paths src/core/autoware_msgs/autoware_sensing_msgs src/universe_sync_benchmark \
  --build-base "$w/obj" --install-base "$w/install" --executor sequential \
  --cmake-args -DSYNC_NATIVE="${SYNC_NATIVE:-OFF}" -DAUTOWARE_ENABLE_AGNOCAST_HOST_VIEW="${AUTOWARE_ENABLE_AGNOCAST_HOST_VIEW:-ON}" -DBUILD_TESTING=OFF -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 \
  -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG'
