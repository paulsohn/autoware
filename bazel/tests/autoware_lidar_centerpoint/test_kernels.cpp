// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/lidar_centerpoint/cuda_utils.hpp>
#include <autoware/lidar_centerpoint/network/scatter_kernel.hpp>
#include <autoware/lidar_centerpoint/postprocess/postprocess_kernel.hpp>
#include <autoware/lidar_centerpoint/preprocess/preprocess_kernel.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace cp = autoware::lidar_centerpoint;
namespace
{
cp::CenterPointConfig configuration(int features = 4)
{
  return cp::CenterPointConfig(
    1, features, 8, 4, {0., 0., -1., 4., 4., 1.}, {1., 1., 2.}, 1, features == 4 ? 9 : 10, {100.F},
    {.5F}, .5F, {0.}, false);
}

template <typename T>
auto device(const std::vector<T> & values)
{
  auto result = cuda::make_unique<T[]>(values.size());
  CHECK_CUDA_ERROR(
    cudaMemcpy(result.get(), values.data(), values.size() * sizeof(T), cudaMemcpyHostToDevice));
  return result;
}

template <typename T>
std::vector<T> host(const T * pointer, std::size_t count)
{
  std::vector<T> result(count);
  CHECK_CUDA_ERROR(cudaMemcpy(result.data(), pointer, count * sizeof(T), cudaMemcpyDeviceToHost));
  return result;
}
}  // namespace

TEST(CenterPointGpu, SweepTransformPreservesIntensityAndTime)
{
  cudaStream_t stream = cudaStreamLegacy;
  for (int features : {4, 5}) {
    const auto config = configuration(features);
    cp::PreprocessCuda preprocessing(config, stream);
    cp::InputPointType point{};
    point.x = 1.F;
    point.y = 2.F;
    point.intensity = 17;
    auto input = device(std::vector<cp::InputPointType>{point});
    auto transform =
      device(std::vector<float>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, .5F, .25F, 0, 1});
    auto output = cuda::make_unique<float[]>(features);
    ASSERT_EQ(
      preprocessing.generateSweepPoints_launch(input.get(), 1, .25F, transform.get(), output.get()),
      cudaSuccess);
    const auto values = host(output.get(), features);
    EXPECT_FLOAT_EQ(values[0], 1.5F);
    EXPECT_FLOAT_EQ(values[1], 2.25F);
    EXPECT_FLOAT_EQ(values[2], 0.F);
    EXPECT_FLOAT_EQ(values.back(), .25F);
    if (features == 5) EXPECT_FLOAT_EQ(values[3], 17.F);
  }
}

TEST(CenterPointGpu, VoxelPackingRetainsPointsAndRejectsOutOfRange)
{
  cudaStream_t stream = cudaStreamLegacy;
  const auto config = configuration();
  cp::PreprocessCuda preprocessing(config, stream);
  auto input =
    device(std::vector<float>{.25F, .25F, 0.F, .1F, .5F, .5F, 0.F, .2F, 10.F, 10.F, 0.F, .3F});
  auto mask = device(std::vector<unsigned int>(16, 0));
  auto voxels = device(std::vector<float>(16 * 32 * 4, 0.F));
  auto count = device(std::vector<unsigned int>{0});
  auto packed = device(std::vector<float>(4 * 32 * 4, 0.F));
  auto sizes = device(std::vector<float>(4, 0.F));
  auto coordinates = device(std::vector<int>(4 * 3, 0));
  ASSERT_EQ(
    preprocessing.generateVoxels_random_launch(input.get(), 3, mask.get(), voxels.get()),
    cudaSuccess);
  ASSERT_EQ(
    preprocessing.generateBaseFeatures_launch(
      mask.get(), voxels.get(), count.get(), packed.get(), sizes.get(), coordinates.get()),
    cudaSuccess);
  ASSERT_EQ(host(count.get(), 1).front(), 1U);
  EXPECT_FLOAT_EQ(host(sizes.get(), 1).front(), 2.F);
  EXPECT_EQ(host(coordinates.get(), 3), (std::vector<int>{0, 0, 0}));
  const auto points = host(packed.get(), 8);
  std::vector<float> positions{points[0], points[4]};
  std::sort(positions.begin(), positions.end());
  EXPECT_EQ(positions, (std::vector<float>{.25F, .5F}));
  auto features = device(std::vector<float>(4 * 32 * 9, 0.F));
  ASSERT_EQ(
    preprocessing.generateFeatures_launch(
      packed.get(), sizes.get(), coordinates.get(), count.get(), features.get()),
    cudaSuccess);
  for (const auto value : host(features.get(), 32 * 9)) EXPECT_TRUE(std::isfinite(value));
}

TEST(CenterPointGpu, ScatterPlacesFeaturesInDeclaredCells)
{
  auto input = device(std::vector<float>{1.F, 2.F, 3.F, 4.F});
  auto coordinates = device(std::vector<int>{0, 1, 2, 0, 2, 1});
  auto count = device(std::vector<unsigned int>{2});
  auto output = device(std::vector<float>(2 * 4 * 4, 0.F));
  ASSERT_EQ(
    cp::scatterFeatures_launch(
      input.get(), coordinates.get(), count.get(), 2, 2, 4, 4, output.get(), cudaStreamLegacy),
    cudaSuccess);
  const auto values = host(output.get(), 32);
  EXPECT_FLOAT_EQ(values[6], 1.F);
  EXPECT_FLOAT_EQ(values[16 + 6], 2.F);
  EXPECT_FLOAT_EQ(values[9], 3.F);
  EXPECT_FLOAT_EQ(values[16 + 9], 4.F);
}

TEST(CenterPointGpu, DecodesConfidentBoxAndRejectsEmptyHeatmap)
{
  cudaStream_t stream = cudaStreamLegacy;
  const auto config = configuration();
  cp::PostProcessCUDA postprocess(config, stream);
  std::vector<float> heat(16, -100.F);
  auto heatmap = device(heat);
  auto offsets = device(std::vector<float>(32, 0.F));
  auto heights = device(std::vector<float>(16, 0.F));
  auto dimensions = device(std::vector<float>(48, 0.F));
  std::vector<float> rotations(32, 0.F);
  rotations[16 + 6] = 1.F;
  auto rotation = device(rotations);
  auto velocity = device(std::vector<float>(32, 0.F));
  std::vector<cp::Box3D> boxes;
  auto decode = [&] {
    return postprocess.generateDetectedBoxes3D_launch(
      heatmap.get(), offsets.get(), heights.get(), dimensions.get(), rotation.get(), velocity.get(),
      boxes, stream);
  };
  ASSERT_EQ(decode(), cudaSuccess);
  EXPECT_TRUE(boxes.empty());
  heat[6] = 10.F;
  CHECK_CUDA_ERROR(
    cudaMemcpy(heatmap.get(), heat.data(), heat.size() * sizeof(float), cudaMemcpyHostToDevice));
  ASSERT_EQ(decode(), cudaSuccess);
  ASSERT_EQ(boxes.size(), 1U);
  EXPECT_EQ(boxes.front().label, 0);
  EXPECT_GT(boxes.front().score, .99F);
  EXPECT_FLOAT_EQ(boxes.front().x, 2.F);
  EXPECT_FLOAT_EQ(boxes.front().y, 1.F);
  EXPECT_FLOAT_EQ(boxes.front().length, 1.F);
  EXPECT_FLOAT_EQ(boxes.front().width, 1.F);
  EXPECT_FLOAT_EQ(boxes.front().height, 1.F);
}
