#!/usr/bin/env bash
set -e
w=build/rmw-agnocast
mode=${1:?use stock or patched}
source build/agnocast-lyrical/install/local_setup.bash
export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21
export XML_CATALOG_FILES=/ws/build/agnocast-lyrical/catalog.xml
args=(--executor sequential --cmake-args -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG')
case "$mode" in
  stock)
    colcon --log-base "$w/log" build --base-paths src/core/autoware_msgs/{autoware_common_msgs,autoware_planning_msgs} \
      --build-base "$w/msg-obj" --install-base "$w/msg-install" "${args[@]}"
    ;;
  patched) source "$w/install/local_setup.bash" ;;
  *) exit 2 ;;
esac
source "$w/msg-install/local_setup.bash"
colcon --log-base "$w/log" build --base-paths src/agnocast_rmw_loan_probe \
  --build-base "$w/$mode-obj" --install-base "$w/$mode-install" "${args[@]}"
