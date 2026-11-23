#!/usr/bin/env bash
# Inside the memory-guarded Lyrical runtime with the qualified standalone CIE installed.
set -e
source build/migration-guide/install/local_setup.bash
source build/standalone/o3-install/local_setup.bash
source build/standalone/release-path/install/local_setup.bash
source build/standalone/release-cie/install/local_setup.bash
export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21 RMW_IMPLEMENTATION=rmw_zenoh_cpp XML_CATALOG_FILES=/ws/build/agnocast-lyrical/catalog.xml
obj=build/standalone/event-cie
cmake -S src/callback_isolated_executor/callback_isolated_executor -B "$obj" \
  -DCIE_EXPERIMENT_EVENT_WORKER=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15
cmake --build "$obj" --target callback_isolated_executor -j1
