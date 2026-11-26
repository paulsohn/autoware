#!/usr/bin/env bash
# From the Lyrical build environment; the optional Debian directory is for offline runs.
set -e
out=$(realpath "${1:?supply staged workspace}")
repo=${2:?supply path to rmw_implementation git checkout}
repo=$(realpath "$repo")
# All rehearsal workspaces in this checkout use one native test domain.
exec 9>"build/rehearsal-conformance.lock"
flock --exclusive 9
pinned=b2083abba8c13900acf55f1dbdb8447757ff2b75
git -C "$repo" cat-file -e "$pinned^{commit}"
mkdir -p "$out/upstream-src"
set -o pipefail
git -C "$repo" archive "$pinned" test_rmw_implementation | tar -x -C "$out/upstream-src"
source /opt/ros/lyrical/setup.bash
source "$out/install/local_setup.bash"
if [[ -n ${REHEARSAL_TEST_DEBS:-} ]]; then
  deps="$out/test-dependencies"
  mkdir -p "$deps"
  sha256sum "$REHEARSAL_TEST_DEBS"/*.deb > "$out/test-dependencies.sha256"
  for deb in "$REHEARSAL_TEST_DEBS"/*.deb; do dpkg-deb -x "$deb" "$deps"; done
  rosprefix="$deps/opt/ros/lyrical"
  export AMENT_PREFIX_PATH="$AMENT_PREFIX_PATH:$rosprefix"
  export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH:$rosprefix:$rosprefix/opt/mimick_vendor:$deps/usr"
  export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$rosprefix/lib:$rosprefix/opt/mimick_vendor/lib:$deps/usr/lib/x86_64-linux-gnu"
fi
export MAKEFLAGS=-j1 CMAKE_BUILD_PARALLEL_LEVEL=1 PYTHON_CPU_COUNT=1 CC=clang-21 CXX=clang++-21
export XML_CATALOG_FILES="$out/schema/catalog.xml"
export RMW_IMPLEMENTATIONS=rmw_agnocast RMW_IMPLEMENTATION=rmw_agnocast
flags=(-DRMW_IMPLEMENTATION=rmw_agnocast -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
cmake -S "$out/upstream-src/test_rmw_implementation" -B "$out/conformance-rmw" "${flags[@]}"
cmake --build "$out/conformance-rmw" --parallel 1
# Reuse this rehearsal's own rclcpp objects. Keep the installed app overlay intact.
cmake -S "$out/src/rclcpp/rclcpp" -B "$out/build/rclcpp" "${flags[@]}"
cmake --build "$out/build/rclcpp" --parallel 1 --target test_loaned_message test_subscription test_executor test_node_interfaces__node_graph
ulimit -c 0
export ROS_DOMAIN_ID=177
python3 experiments/rmw-agnocast/rehearsal/gtest.py "$out" "$REHEARSAL_HOOK" "${3:-$out/conformance-results}"
