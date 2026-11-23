#!/usr/bin/env bash
set -e
w=build/rmw-agnocast
source build/agnocast-lyrical/install/local_setup.bash
source "$w/install/local_setup.bash"
source "$w/msg-install/local_setup.bash"
export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21
colcon --log-base "$w/log" build --base-paths src/rmw_agnocast src/agnocast_rmw_loan_probe \
 --build-base "$w/load-obj" --install-base "$w/load-install" --executor sequential \
 --cmake-args -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 \
 -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG'
