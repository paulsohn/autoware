// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/cuda_utils/cuda_unique_ptr.hpp>
#include <autoware/lidar_frnet/postprocess_kernel.hpp>
#include <autoware/lidar_frnet/preprocess_kernel.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace fr = autoware::lidar_frnet;
namespace cu = autoware::cuda_utils;
namespace
{
template <typename T>
auto device(const std::vector<T> & values)
{
  auto pointer = cu::make_unique<T[]>(values.size());
  CHECK_CUDA_ERROR(
    cudaMemcpy(pointer.get(), values.data(), values.size() * sizeof(T), cudaMemcpyHostToDevice));
  return pointer;
}

template <typename T>
std::vector<T> host(const T * pointer, size_t count)
{
  std::vector<T> result(count);
  CHECK_CUDA_ERROR(cudaMemcpy(result.data(), pointer, count * sizeof(T), cudaMemcpyDeviceToHost));
  return result;
}
}  // namespace

TEST(FRNetGpu, ProjectsPointsAndRemovesEgoBox)
{
  fr::utils::NetworkParams parameters(
    {"road", "car"}, {1, 2, 4}, {1, 2, 4}, 15., -25., 16, 8, 32, 8, true,
    {-1.F, -1.F, -1.F, 1.F, 1.F, 1.F});
  fr::PreprocessCuda preprocessing(parameters, cudaStreamLegacy);
  fr::CloudPointTypeXYZIRC first{}, second{};
  first.x = .5F;
  first.intensity = 10;
  second.y = 5.F;
  second.intensity = 20;
  second.channel = 7;
  auto input = device(std::vector{first, second});
  auto count = device(std::vector<uint32_t>{0});
  auto points = device(std::vector<float>(8, 0.F));
  auto coordinates = device(std::vector<int64_t>(6, 0));
  auto keys = device(std::vector<int64_t>(2, 0));
  auto indexes = device(std::vector<uint32_t>(32 * 8, 0));
  auto projections = device(std::vector<uint64_t>(32 * 8, 0));
  auto compact = cu::make_unique<fr::CloudPointTypeXYZIRC[]>(2);
  ASSERT_EQ(
    preprocessing.projectPoints_launch(
      input.get(), 2, fr::CloudFormat::XYZIRC, count.get(), points.get(), coordinates.get(),
      keys.get(), indexes.get(), projections.get(), compact.get()),
    cudaSuccess);
  ASSERT_EQ(host(count.get(), 1).front(), 1U);
  const auto result = host(points.get(), 4);
  EXPECT_FLOAT_EQ(result[0], 0.F);
  EXPECT_FLOAT_EQ(result[1], 5.F);
  EXPECT_FLOAT_EQ(result[3], 20.F);
  EXPECT_EQ(host(compact.get(), 1).front().channel, 7);
  const auto coors = host(coordinates.get(), 3);
  EXPECT_EQ(coors[0], 0);
  EXPECT_GE(coors[1], 0);
  EXPECT_LT(coors[1], 8);
  EXPECT_EQ(coors[2], 4);
}

TEST(FRNetGpu, LabelsColorsAndFiltersPredictionsPreservingPointFields)
{
  const fr::utils::PostprocessingParams parameters(
    .5, {"road"}, "xyzirc", {}, {"road", "car"}, {255, 0, 0, 0, 255, 0});
  fr::PostprocessCuda postprocessing(parameters, cudaStreamLegacy);
  fr::CloudPointTypeXYZIRC first{}, second{};
  first.x = 5.F;
  first.intensity = 10;
  second.x = 6.F;
  second.intensity = 20;
  second.return_type = 2;
  second.channel = 7;
  auto compact = device(std::vector{first, second});
  auto points = device(std::vector<float>{5.F, 0, 0, 10.F, 6.F, 0, 0, 20.F});
  auto predictions = device(std::vector<float>{.9F, .1F, .1F, .9F});
  auto count = device(std::vector<uint32_t>{0});
  auto segmentation = cu::make_unique<fr::OutputSegmentationPointType[]>(2);
  auto visualization = cu::make_unique<fr::OutputVisualizationPointType[]>(2);
  auto filtered = cu::make_unique<fr::CloudPointTypeXYZIRC[]>(2);
  ASSERT_EQ(
    postprocessing.fillCloud_launch(
      points.get(), compact.get(), 2, predictions.get(), 2, fr::CloudFormat::XYZIRC,
      fr::CloudFormat::XYZIRC, fr::utils::ActiveComm(true, true, true), count.get(),
      segmentation.get(), visualization.get(), filtered.get()),
    cudaSuccess);
  const auto labels = host(segmentation.get(), 2);
  EXPECT_EQ(labels[0].class_id, 0);
  EXPECT_EQ(labels[1].class_id, 1);
  EXPECT_FLOAT_EQ(labels[0].probability, .9F);
  EXPECT_FLOAT_EQ(labels[1].x, 6.F);
  const auto colors = host(visualization.get(), 2);
  uint32_t color;
  std::memcpy(&color, &colors[0].rgb, sizeof(color));
  EXPECT_EQ(color, 0xff0000U);
  std::memcpy(&color, &colors[1].rgb, sizeof(color));
  EXPECT_EQ(color, 0x00ff00U);
  ASSERT_EQ(host(count.get(), 1).front(), 1U);
  const auto kept = host(filtered.get(), 1).front();
  EXPECT_FLOAT_EQ(kept.x, 6.F);
  EXPECT_EQ(kept.intensity, 20);
  EXPECT_EQ(kept.return_type, 2);
  EXPECT_EQ(kept.channel, 7);
}
