#!/usr/bin/env bash
# Inside the guarded Lyrical root with the cluster replay prerequisites installed.
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/perception-replay/install/local_setup.bash
ws=build/tracker-replay
common=(--executor sequential --install-base "$ws/install" --cmake-args -DBUILD_TESTING=OFF
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
# muSSP clears CMAKE_CXX_FLAGS, including the GCC installation hint Clang needs.
mkdir -p "$ws/src/muSSP"
cp -a src/universe/external/muSSP/. "$ws/src/muSSP/"
patch -d "$ws/src/muSSP" -p1 < experiments/rmw-agnocast/mussp-parent.patch
CC=/usr/bin/gcc CXX=/usr/bin/g++ colcon build --base-paths "$ws/src/muSSP" \
  --build-base "$ws/mussp-obj" "${common[@]}"
source "$ws/install/local_setup.bash"
export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21
flags=(-DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 -Wno-error=unknown-warning-option"
       "-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG")
colcon build --base-paths src/core/autoware_utils/{autoware_utils_system,autoware_utils_debug,autoware_utils_math,autoware_utils_geometry} \
  src/core/autoware_msgs/{autoware_common_msgs,autoware_vehicle_msgs,autoware_planning_msgs} \
  src/core/autoware_internal_msgs/autoware_internal_planning_msgs \
  --build-base "$ws/obj" "${common[@]}" "${flags[@]}" -DCMAKE_CXX_STANDARD=17
source "$ws/install/local_setup.bash"
pkg=autoware_multi_object_tracker
work="$ws/src/$pkg"
mkdir -p "$work"
cp -a "src/universe/autoware_universe/perception/$pkg/." "$work/"
patch -d "$work" -p1 < experiments/rmw-agnocast/tracker-lyrical.patch
if [[ ${REPLAY_CONSUME_STAMP:-1} == 1 ]]; then
  patch -d "$work" -p1 < experiments/rmw-agnocast/tracker-consume-stamp.patch
fi
colcon build --base-paths "$work" --build-base "$ws/node-obj" "${common[@]}" "${flags[@]}"
