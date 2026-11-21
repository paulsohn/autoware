#!/usr/bin/env bash
set -eu
cd "$(git rev-parse --show-toplevel)"
export LYRICAL_ROOT=${LYRICAL_ROOT:-$(cat build/agnocast-lyrical/universe-root)}
base=build/gpu-edge
mkdir -p "$base"
pre=src/universe/autoware_universe/sensing/autoware_cuda_pointcloud_preprocessor
experiments/buffer/run-limited.sh "${CUDA_HOME:-/usr/local/cuda}/bin/nvcc" \
  -arch="${GPU_ARCH:-sm_120}" -std=c++17 -I"$pre/include" \
  -Isrc/core/cuda/autoware_cuda/autoware_cuda_utils/include \
  -c "$pre/src/cuda_concatenate_data/cuda_combine_cloud_handler_kernel.cu" -o "$base/transform.o"
experiments/buffer/run-lyrical.sh colcon --log-base "$base/log" build --executor sequential \
  --base-paths src/core/cuda/negotiated src/universe_gpu_edge_benchmark \
  --packages-up-to universe_gpu_edge_benchmark --build-base "$base/obj" --install-base "$base/install" \
  --cmake-args -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g0' \
  -DCUDAToolkit_ROOT=/usr/local/cuda -DKERNEL_OBJECT=/ws/build/gpu-edge/transform.o
