// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <Eigen/Core>

#include <cuda_runtime.h>

__global__ void eigen_expression(float * output)
{
  Eigen::Matrix3f matrix = Eigen::Matrix3f::Identity();
  Eigen::Vector3f vector(1.F, 2.F, 3.F);
  const Eigen::Matrix3f scaled = 2.F * matrix;
  const Eigen::Vector3f result = scaled * vector + vector;
  for (int i = 0; i < 3; ++i) output[i] = result[i];
}

extern "C" int eigen_cuda_values(float * output)
{
  float * device = nullptr;
  auto status = cudaMalloc(&device, 3 * sizeof(float));
  if (status != cudaSuccess) return status;
  eigen_expression<<<1, 1>>>(device);
  status = cudaGetLastError();
  if (status == cudaSuccess) {
    status = cudaMemcpy(output, device, 3 * sizeof(float), cudaMemcpyDeviceToHost);
  }
  const auto released = cudaFree(device);
  return status == cudaSuccess ? released : status;
}
