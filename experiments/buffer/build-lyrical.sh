#!/usr/bin/env bash
set -e
export PATH=/usr/local/cuda/bin:$PATH
build() {
  colcon build --executor sequential --base-paths "$@"
  source install/local_setup.bash
}
build src/rosidl_buffer_backends/cuda_buffer_backend --packages-up-to cuda_buffer_backend --cmake-args -DBUILD_TESTING=OFF
build src/shmem_buffer_backend --packages-up-to shmem_buffer_backend --cmake-args -DBUILD_TESTING=OFF
build src/core/autoware_internal_msgs/{autoware_internal_msgs,autoware_internal_debug_msgs} --cmake-args -DBUILD_TESTING=OFF
build src/buffer_pointcloud_probe
for backend in cuda shmem; do
  for mode in native mixed; do bash src/buffer_pointcloud_probe/run-case.sh "$backend" "$mode"; done
done
p=src/crop_pipeline_benchmark
opts=(-DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG --param=ggc-min-expand=20 --param=ggc-min-heapsize=32768')
unset WRAPPER_TWO_EDGE
build "$p" --cmake-args "${opts[@]}"
ctest --test-dir build/crop_pipeline_benchmark -j1 --output-on-failure
bash experiments/buffer/bench-lyrical.sh
export WRAPPER_TWO_EDGE=1
colcon build --build-base build/two/obj --install-base build/two/install --base-paths "$p" --executor sequential --cmake-args "${opts[@]}"
source build/two/install/local_setup.bash
ctest --test-dir build/two/obj/crop_pipeline_benchmark -j1 --output-on-failure
bash experiments/buffer/bench-lyrical.sh two-edge
