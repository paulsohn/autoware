// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/lidar_transfusion/postprocess/postprocess_kernel.hpp>
#include <autoware/lidar_transfusion/preprocess/preprocess_kernel.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

namespace tfm = autoware::lidar_transfusion;
namespace
{
tfm::TransfusionConfig configuration()
{
  return tfm::TransfusionConfig(
    1024, {1, 2, 4}, {0., 0., -1., 16., 16., 1.}, {1., 1., 2.}, 2, .5F, {0., 0., 0., 0., 0.}, .5F);
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

TEST(TransFusionGpu, SweepPreservesXYZIntensityAndTime)
{
  cudaStream_t stream = cudaStreamLegacy;
  tfm::PreprocessCuda preprocessing(configuration(), stream);
  std::vector<uint8_t> raw(16, 0);
  const float coordinates[3] = {1.F, 2.F, 0.F};
  std::memcpy(raw.data(), coordinates, sizeof(coordinates));
  raw[12] = 17;
  auto input = device(raw);
  auto transform = device(std::vector<float>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, .5F, .25F, 0, 1});
  auto output = cuda::make_unique<float[]>(5);
  ASSERT_EQ(
    preprocessing.generateSweepPoints_launch(
      input.get(), 1, 16, .25F, transform.get(), output.get()),
    cudaSuccess);
  EXPECT_EQ(host(output.get(), 5), (std::vector<float>{1.5F, 2.25F, 0.F, 17.F, .25F}));
}

TEST(TransFusionGpu, ShufflePreservesEveryValidPointAcrossPaddingAndBlockBoundary)
{
  cudaStream_t stream = cudaStreamLegacy;
  tfm::PreprocessCuda preprocessing(configuration(), stream);
  constexpr std::size_t size = 513;
  std::vector<float> points(size * 5, 999.F);
  std::fill(points.begin(), points.begin() + 5, 1.F);
  std::fill(points.begin() + 5, points.begin() + 10, 2.F);
  std::vector<unsigned int> order(size);
  std::iota(order.begin(), order.end(), 0);
  std::swap(order[0], order[size - 1]);
  auto input = device(points);
  auto indices = device(order);
  auto output = device(std::vector<float>(size * 5, -7.F));
  ASSERT_EQ(
    preprocessing.shufflePoints_launch(input.get(), indices.get(), output.get(), 2, size, 0),
    cudaSuccess);
  const auto shuffled = host(output.get(), size * 5);
  std::size_t invalid_padding = 0;
  for (std::size_t i = 0; i < size; ++i) {
    for (std::size_t feature = 0; feature < 5; ++feature) {
      if (i == 1)
        EXPECT_FLOAT_EQ(shuffled[i * 5 + feature], 2.F);
      else if (i == size - 1)
        EXPECT_FLOAT_EQ(shuffled[i * 5 + feature], 1.F);
      else if (!std::isinf(shuffled[i * 5 + feature]))
        ++invalid_padding;
    }
  }
  EXPECT_EQ(invalid_padding, 0U);
}

TEST(TransFusionGpu, VoxelsRejectOutOfRangeAndPreservePointFeatures)
{
  cudaStream_t stream = cudaStreamLegacy;
  const auto config = configuration();
  tfm::PreprocessCuda preprocessing(config, stream);
  auto input = device(
    std::vector<float>{
      .25F, .25F, 0.F, 10.F, .1F, .5F, .5F, 0.F, 20.F, .2F, 50.F, 50.F, 0.F, 30.F, .3F});
  auto count = device(std::vector<unsigned int>{0});
  auto features = device(std::vector<float>(4 * 20 * 5, 0.F));
  auto sizes = device(std::vector<unsigned int>(4, 0));
  auto coordinates = device(std::vector<unsigned int>(4 * 4, 0));
  preprocessing.generateVoxels(
    input.get(), 3, count.get(), features.get(), sizes.get(), coordinates.get());
  ASSERT_EQ(host(count.get(), 1).front(), 1U);
  EXPECT_EQ(host(sizes.get(), 1).front(), 2U);
  const auto result = host(features.get(), 10);
  std::vector<float> positions{result[0], result[5]};
  std::sort(positions.begin(), positions.end());
  EXPECT_EQ(positions, (std::vector<float>{.25F, .5F}));
  for (int i = 0; i < 2; ++i) {
    EXPECT_FLOAT_EQ(result[i * 5 + 3], result[i * 5] == .25F ? 10.F : 20.F);
    EXPECT_FLOAT_EQ(result[i * 5 + 4], result[i * 5] == .25F ? .1F : .2F);
  }
}

TEST(TransFusionGpu, DecodesKnownProposalAndRejectsLowScores)
{
  cudaStream_t stream = cudaStreamLegacy;
  tfm::PostprocessCuda postprocess(configuration(), stream);
  std::vector<float> scores(10, 0.F);
  auto classes = device(scores);
  std::vector<float> coordinates(12, 0.F);
  coordinates[0] = .25F;
  coordinates[2] = .5F;
  auto boxes = device(coordinates);
  auto rotations = device(std::vector<float>{0.F, 0.F, 1.F, 0.F});
  std::vector<tfm::Box3D> result;
  auto decode = [&] {
    return postprocess.generateDetectedBoxes3D_launch(
      classes.get(), boxes.get(), rotations.get(), result, stream);
  };
  ASSERT_EQ(decode(), cudaSuccess);
  EXPECT_TRUE(result.empty());
  scores[0] = .9F;
  CHECK_CUDA_ERROR(cudaMemcpy(
    classes.get(), scores.data(), scores.size() * sizeof(float), cudaMemcpyHostToDevice));
  ASSERT_EQ(decode(), cudaSuccess);
  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result.front().label, 0);
  EXPECT_FLOAT_EQ(result.front().score, .9F);
  EXPECT_FLOAT_EQ(result.front().x, 1.F);
  EXPECT_FLOAT_EQ(result.front().y, 2.F);
  EXPECT_FLOAT_EQ(result.front().length, 1.F);
  EXPECT_FLOAT_EQ(result.front().width, 1.F);
  EXPECT_FLOAT_EQ(result.front().height, 1.F);
}
