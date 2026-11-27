// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/cuda_utils/cuda_unique_ptr.hpp>
#include <autoware/image_projection_based_fusion/camera_projection.hpp>
#include <autoware/image_projection_based_fusion/pointpainting_fusion/preprocess_kernel.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace fusion = autoware::image_projection_based_fusion;

sensor_msgs::msg::CameraInfo camera(double focal)
{
  sensor_msgs::msg::CameraInfo info;
  info.width = 64;
  info.height = 48;
  info.distortion_model = "plumb_bob";
  info.d = std::vector<double>(5, 0.);
  info.k = {focal, 0., 32., 0., focal, 24., 0., 0., 1.};
  info.r = {1., 0., 0., 0., 1., 0., 0., 0., 1.};
  info.p = {focal, 0., 32., 0., 0., focal, 24., 0., 0., 0., 1., 0.};
  return info;
}

TEST(FusionProjection, CalibrationsAndProjectionModes)
{
  for (const double focal : {40., 80.}) {
    for (const bool raw : {false, true}) {
      for (const bool approximate : {false, true}) {
        fusion::CameraProjection projector(camera(focal), 1.F, 1.F, raw, approximate);
        projector.initialize();
        Eigen::Vector2d pixel;
        ASSERT_TRUE(projector.calcImageProjectedPoint(cv::Point3d(1., 0., 10.), pixel));
        EXPECT_NEAR(pixel.x(), 32. + focal / 10., approximate && raw ? 1. : 1e-6);
        EXPECT_NEAR(pixel.y(), 24., approximate && raw ? 1. : 1e-6);
        EXPECT_FALSE(projector.isOutsideFOV(0.F, 0.F, 10.F));
        EXPECT_TRUE(projector.isOutsideFOV(100.F, 0.F, 10.F));
      }
    }
  }
}

TEST(FusionProjection, RejectsInvalidGrid)
{
  EXPECT_THROW(fusion::CameraProjection(camera(40.), 0.F, 1.F, false, false), std::runtime_error);
  EXPECT_THROW(fusion::CameraProjection(camera(40.), 1.F, -1.F, false, false), std::runtime_error);
}

TEST(PointPaintingCuda, CropsAndPreservesPaintedFeatures)
{
  const std::vector<float> points{.25F, .25F, 0.F, 0.F, 1.F,  0.F, 0.F, .5F, .5F, 0.F, 0.F,
                                  0.F,  1.F,  0.F, 3.F, .25F, 0.F, 0.F, 0.F, 0.F, 1.F};
  auto input = autoware::cuda_utils::make_unique<float[]>(points.size());
  auto mask = autoware::cuda_utils::make_unique<unsigned int[]>(4);
  auto voxels = autoware::cuda_utils::make_unique<float[]>(4 * 32 * 7);
  auto voxel_features = autoware::cuda_utils::make_unique<float[]>(4 * 32 * 7);
  auto counts = autoware::cuda_utils::make_unique<float[]>(4);
  auto coords = autoware::cuda_utils::make_unique<int[]>(4 * 3);
  auto number = autoware::cuda_utils::make_unique<unsigned int[]>(1);
  auto features = autoware::cuda_utils::make_unique<float[]>(4 * 32 * 12);
  ASSERT_EQ(
    cudaMemcpy(input.get(), points.data(), points.size() * sizeof(float), cudaMemcpyHostToDevice),
    cudaSuccess);
  ASSERT_EQ(cudaMemset(mask.get(), 0, 4 * sizeof(unsigned int)), cudaSuccess);
  ASSERT_EQ(cudaMemset(number.get(), 0, sizeof(unsigned int)), cudaSuccess);
  ASSERT_EQ(
    fusion::generateVoxels_random_launch(
      input.get(), 3, 0.F, 2.F, 0.F, 2.F, -1.F, 1.F, 1.F, 1.F, 2.F, 2, 2, mask.get(), voxels.get(),
      nullptr),
    cudaSuccess);
  ASSERT_EQ(
    fusion::generateBaseFeatures_launch(
      mask.get(), voxels.get(), 2, 2, 4, number.get(), voxel_features.get(), counts.get(),
      coords.get(), nullptr),
    cudaSuccess);
  ASSERT_EQ(
    fusion::generateFeatures_launch(
      voxel_features.get(), counts.get(), coords.get(), number.get(), 4, 1.F, 1.F, 2.F, 0.F, 0.F,
      -1.F, features.get(), 12, nullptr),
    cudaSuccess);
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
  unsigned int pillars = 0;
  float count = 0.F;
  std::vector<float> output(32 * 12);
  ASSERT_EQ(
    cudaMemcpy(&pillars, number.get(), sizeof(pillars), cudaMemcpyDeviceToHost), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(&count, counts.get(), sizeof(count), cudaMemcpyDeviceToHost), cudaSuccess);
  ASSERT_EQ(
    cudaMemcpy(
      output.data(), features.get(), output.size() * sizeof(float), cudaMemcpyDeviceToHost),
    cudaSuccess);
  EXPECT_EQ(pillars, 1U);
  ASSERT_FLOAT_EQ(count, 2.F);
  for (int point = 0; point < 2; ++point) {
    const auto offset = point * 12;
    const float x = output[offset];
    ASSERT_TRUE(x == .25F || x == .5F);
    EXPECT_FLOAT_EQ(output[offset + 1], x);
    EXPECT_FLOAT_EQ(output[offset + 4], x == .25F ? 1.F : 0.F);
    EXPECT_FLOAT_EQ(output[offset + 5], x == .5F ? 1.F : 0.F);
    EXPECT_FLOAT_EQ(output[offset + 6], 0.F);
    EXPECT_FLOAT_EQ(output[offset + 7], x - .375F);
    EXPECT_FLOAT_EQ(output[offset + 10], x - .5F);
  }
  EXPECT_TRUE(std::all_of(output.begin() + 24, output.end(), [](float x) { return x == 0.F; }));
}
