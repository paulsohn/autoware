// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <spconv/point2voxel_compat.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

TEST(Point2VoxelCompatibility, BorrowedViewsSurviveRepeatedVoxelization)
{
  using Voxelizer = spconvlib::spconv::csrc::sparse::all::ops3d::Point2Voxel;
  float input[]{.1F, .2F, .3F, 0.F, .4F, .5F, .6F, 0.F, -1.1F, -.2F, -.3F, 0.F};
  auto points = tv::from_blob(input, {3, 4}, tv::float32).cuda();
  auto voxels = tv::empty({4, 4, 4}, tv::float32, 0);
  auto indices = tv::empty({4, 3}, tv::int32, 0);
  auto counts = tv::empty({4}, tv::int32, 0);
  auto hash = tv::empty({8}, tv::custom128, 0);
  auto point_indices = tv::empty({4}, tv::int64, 0);
  auto voxel_ids = tv::empty({4}, tv::int64, 0);
  for (int repeat = 0; repeat < 2; ++repeat) {
    auto result = Voxelizer::point_to_voxel_hash_static(
      points, voxels, indices, counts, hash, point_indices, voxel_ids, {1.F, 1.F, 1.F}, {4, 4, 4},
      {16, 4, 1}, {-2.F, -2.F, -2.F, 2.F, 2.F, 2.F}, true, false, 0);
    EXPECT_EQ(std::get<0>(result).dim(0), 2);
    auto host_counts = std::get<2>(result).cpu();
    const auto * sizes = host_counts.data<std::int32_t>();
    EXPECT_EQ(sizes[0] + sizes[1], 3);
  }
  // The SDK rejects integer point records. Its GNU exception returns through C status
  // and becomes an exception owned by the libc++ caller.
  auto invalid_points = tv::empty({3, 4}, tv::int64, 0);
  EXPECT_THROW(
    Voxelizer::point_to_voxel_hash_static(
      invalid_points, voxels, indices, counts, hash, point_indices, voxel_ids, {1.F, 1.F, 1.F},
      {4, 4, 4}, {16, 4, 1}, {-2.F, -2.F, -2.F, 2.F, 2.F, 2.F}, true, false, 0),
    std::runtime_error);
}

TEST(Point2VoxelCompatibility, InvalidDescriptorReturnsAnError)
{
  SpconvPoint2Voxel3D request{};
  request.tensors[0].rank = 4;
  int64_t count = -1;
  char error[128]{};
  EXPECT_NE(spconv_point2voxel3d(&request, &count, error, sizeof(error)), 0);
  EXPECT_NE(std::string(error).find("rank"), std::string::npos);
  EXPECT_EQ(count, -1);
}
