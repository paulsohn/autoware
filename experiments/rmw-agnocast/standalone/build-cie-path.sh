#!/usr/bin/env bash
set -e
source build/migration-guide/install/local_setup.bash
source build/standalone/o3-install/local_setup.bash
source build/standalone/release-path/install/local_setup.bash
export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21 RMW_IMPLEMENTATION=rmw_zenoh_cpp XML_CATALOG_FILES=/ws/build/agnocast-lyrical/catalog.xml
w=build/standalone/release-cie
args=(-DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG" -DBUILD_TESTING=OFF -DRMW_IMPLEMENTATION=rmw_zenoh_cpp -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
colcon --log-base "$w/log" build --build-base "$w/obj" --install-base "$w/install" --paths src/callback_isolated_executor/{cie_config_msgs,cie_thread_configurator,callback_isolated_executor} --executor sequential --cmake-args "${args[@]}"
source "$w/install/local_setup.bash"
cmake -S src/agnocast_rmw_loan_probe -B "$w/probe" -DPATH_BENCH_CIE=ON "${args[@]}"
cmake --build "$w/probe" --target path_benchmark -j1
