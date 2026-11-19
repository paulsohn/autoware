#!/usr/bin/env bash
set -e
w=build/agnocast-lyrical
export ENABLE_AGNOCAST=1
export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21
export CXXFLAGS="--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15"
source "$w/install/local_setup.bash"
opts=(--build-base "$w/clang-obj" --install-base "$w/install" --executor sequential
      --cmake-args "-DCMAKE_CXX_FLAGS=$CXXFLAGS" -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG')
colcon --log-base "$w/log" build --base-paths "$w/point_cloud_msg_wrapper" "${opts[@]}" -DBUILD_TESTING=OFF
source "$w/install/local_setup.bash"
colcon --log-base "$w/log" build --base-paths src/universe/external/managed_transform_buffer/managed_transform_buffer "${opts[@]}" -DBUILD_TESTING=ON
source "$w/install/local_setup.bash"
colcon --log-base "$w/log" build --base-paths src/universe_crop_benchmark "${opts[@]}" -DBUILD_TESTING=OFF
