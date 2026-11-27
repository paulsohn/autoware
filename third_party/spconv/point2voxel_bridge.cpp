// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <spconv/point2voxel_bridge.h>
#include <spconvlib/spconv/csrc/sparse/all/ops3d/Point2Voxel.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <stdexcept>

extern "C" int spconv_point2voxel3d(
  const SpconvPoint2Voxel3D * request, int64_t * voxel_count, char * error, size_t error_capacity)
{
  try {
    if (!request || !voxel_count)
      throw std::invalid_argument("Missing voxelization request/result");
    std::array<tv::Tensor, 7> tensors;
    for (std::size_t i = 0; i < tensors.size(); ++i) {
      const auto & view = request->tensors[i];
      if (view.rank < 1 || view.rank > 3)
        throw std::invalid_argument("Expected tensor rank 1 to 3");
      tensors[i] = tv::from_blob(
        view.data, tv::TensorShape(view.shape, view.shape + view.rank),
        tv::TensorShape(view.stride, view.stride + view.rank), static_cast<tv::DType>(view.dtype),
        view.device);
    }
    std::array<float, 3> voxel_size;
    std::array<int, 3> grid_size;
    std::array<int64_t, 3> grid_stride;
    std::array<float, 6> range;
    std::copy_n(request->voxel_size, 3, voxel_size.begin());
    std::copy_n(request->grid_size, 3, grid_size.begin());
    std::copy_n(request->grid_stride, 3, grid_stride.begin());
    std::copy_n(request->range, 6, range.begin());
    using Voxelizer = spconvlib::spconv::csrc::sparse::all::ops3d::Point2Voxel;
    const auto result = Voxelizer::point_to_voxel_hash_static(
      tensors[0], tensors[1], tensors[2], tensors[3], tensors[4], tensors[5], tensors[6],
      voxel_size, grid_size, grid_stride, range, request->clear_voxels != 0,
      request->empty_mean != 0, request->stream);
    *voxel_count = std::get<0>(result).dim(0);
    return 0;
  } catch (const std::exception & failure) {
    if (error && error_capacity) std::snprintf(error, error_capacity, "%s", failure.what());
    return 1;
  } catch (...) {
    if (error && error_capacity)
      std::snprintf(error, error_capacity, "Unknown sparse SDK exception");
    return 1;
  }
}
