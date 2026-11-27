// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#ifndef SPCONV_POINT2VOXEL_BRIDGE_H_
#define SPCONV_POINT2VOXEL_BRIDGE_H_
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SpconvTensorView3D
{
  void * data;
  int64_t shape[3];
  int64_t stride[3];
  int32_t rank;
  int32_t dtype;
  int32_t device;
} SpconvTensorView3D;

typedef struct SpconvPoint2Voxel3D
{
  // points, voxels, indices, counts, hash, point indices, point voxel IDs.
  SpconvTensorView3D tensors[7];
  float voxel_size[3];
  int32_t grid_size[3];
  int64_t grid_stride[3];
  float range[6];
  int32_t clear_voxels;
  int32_t empty_mean;
  uintptr_t stream;
} SpconvPoint2Voxel3D;

// Buffers remain caller-owned. Exceptions become a nonzero status and error text.
int spconv_point2voxel3d(
  const SpconvPoint2Voxel3D * request, int64_t * voxel_count, char * error, size_t error_capacity);

#ifdef __cplusplus
}
#endif
#endif
