#!/usr/bin/env bash
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/rmw-agnocast/load-install/local_setup.bash
source build/universe-sync/install/local_setup.bash
export ENABLE_AGNOCAST=0 CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21
w=build/universe-pipeline
packages=(src/universe_crop_benchmark src/universe_sync_benchmark)
view=ON
if [[ ${1:-} == plain ]]; then
  source "$w/install/local_setup.bash"
  packages=(src/universe_sync_benchmark)
  w=build/universe-pipeline-plain
  view=OFF
fi
colcon --log-base "$w/log" build --base-paths "${packages[@]}" \
  --build-base "$w/obj" --install-base "$w/install" --executor sequential \
  --cmake-args -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 \
  -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG' \
  -DBUILD_TESTING=OFF -DUNIVERSE_BUFFER_MATRIX=OFF -DSYNC_NATIVE=OFF -DAUTOWARE_ENABLE_AGNOCAST_HOST_VIEW="$view"
