#!/usr/bin/env bash
# Inside the guarded Lyrical root with the existing colcon build directories.
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/migration-guide/install/local_setup.bash
source build/perception-replay/install/local_setup.bash
source build/tracker-replay/install/local_setup.bash
source build/standalone/o3-install/local_setup.bash
source build/standalone/release-path/install/local_setup.bash
source build/standalone/release-cie/install/local_setup.bash
export RMW_IMPLEMENTATION=rmw_zenoh_cpp
if [[ $1 == path ]]; then
  cmake --build build/standalone/release-cie/probe --target path_benchmark -j1
  exit
fi
for obj in build/perception-replay/obj/universe_detection_replay build/tracker-replay/node-obj/autoware_multi_object_tracker; do
  src=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$obj/CMakeCache.txt")
  cmake -S "$src" -B "$obj" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG"
  cmake --build "$obj" -j1
  cmake --install "$obj"
done
