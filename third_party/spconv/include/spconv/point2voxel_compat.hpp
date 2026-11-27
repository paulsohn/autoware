// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <spconv/point2voxel_bridge.h>
#include <spconvlib/spconv/csrc/sparse/all/ops3d/Point2Voxel.h>

#include <array>

#ifdef _LIBCPP_VERSION
#include <algorithm>
#include <array>
#include <stdexcept>

namespace spconvlib::spconv::csrc::sparse::all::ops3d
{
// Keep TensorView ownership in each ABI; only borrowed tensor descriptions cross.
inline std::tuple<tv::Tensor, tv::Tensor, tv::Tensor> Point2Voxel::point_to_voxel_hash_static(
  tv::Tensor points, tv::Tensor voxels, tv::Tensor indices, tv::Tensor num_per_voxel,
  tv::Tensor hashdata, tv::Tensor point_indice_data, tv::Tensor points_voxel_id,
  std::array<float, 3> vsize, std::array<int, 3> grid_size, std::array<int64_t, 3> grid_stride,
  std::array<float, 6> coors_range, bool clear_voxels, bool empty_mean, std::uintptr_t stream_int)
{
  SpconvPoint2Voxel3D request{};
  std::array<tv::Tensor, 7> tensors{
    points, voxels, indices, num_per_voxel, hashdata, point_indice_data, points_voxel_id};
  for (std::size_t i = 0; i < tensors.size(); ++i) {
    auto & tensor = tensors[i];
    auto & view = request.tensors[i];
    if (tensor.ndim() < 1 || tensor.ndim() > 3)
      throw std::invalid_argument("Expected tensor rank 1 to 3");
    view.data = tensor.raw_data(false);
    view.rank = tensor.ndim();
    view.dtype = tensor.dtype();
    view.device = tensor.device();
    for (int d = 0; d < view.rank; ++d) {
      view.shape[d] = tensor.dim(d);
      view.stride[d] = tensor.stride(d);
    }
  }
  std::copy(vsize.begin(), vsize.end(), request.voxel_size);
  std::copy(grid_size.begin(), grid_size.end(), request.grid_size);
  std::copy(grid_stride.begin(), grid_stride.end(), request.grid_stride);
  std::copy(coors_range.begin(), coors_range.end(), request.range);
  request.clear_voxels = clear_voxels;
  request.empty_mean = empty_mean;
  request.stream = stream_int;
  int64_t count = 0;
  char error[512]{};
  if (spconv_point2voxel3d(&request, &count, error, sizeof(error)) != 0)
    throw std::runtime_error(error);
  return std::make_tuple(
    voxels.slice_first_axis(0, count), indices.slice_first_axis(0, count),
    num_per_voxel.slice_first_axis(0, count));
}
}  // namespace spconvlib::spconv::csrc::sparse::all::ops3d
#endif
