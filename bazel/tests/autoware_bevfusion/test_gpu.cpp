// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/bevfusion/camera/camera_preprocess.hpp>
#include <autoware/bevfusion/postprocess/circle_nms_kernel.hpp>
#include <autoware/bevfusion/preprocess/preprocess_kernel.hpp>
#include <autoware/cuda_utils/cuda_unique_ptr.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace bev = autoware::bevfusion;
using autoware::cuda_utils::make_unique;

bev::BEVFusionConfig config(bool intensity = false)
{
  return bev::BEVFusionConfig(
    1, "", "", "", "", 1, 8, 4, {1, 4, 8}, {-2.F, -2.F, -2.F, 2.F, 2.F, 2.F}, {1.F, 1.F, 1.F},
    {1.F, 4.F, 1.F}, {-2.F, 2.F, 1.F}, {-2.F, 2.F, 1.F}, {-2.F, 2.F, 4.F}, 1, 4, 4, 1.F, 1.F, 4, 4,
    1, 1, 3, 4, 3, .5F, {.3}, {.1F}, {4.F}, intensity);
}

template <class T>
auto upload(const std::vector<T> & values)
{
  auto device = make_unique<T[]>(values.size());
  if (
    cudaMemcpy(device.get(), values.data(), values.size() * sizeof(T), cudaMemcpyHostToDevice) !=
    cudaSuccess)
    throw std::runtime_error("CUDA upload failed");
  return device;
}

template <class T>
std::vector<T> download(const T * device, std::size_t size)
{
  std::vector<T> values(size);
  if (cudaMemcpy(values.data(), device, size * sizeof(T), cudaMemcpyDeviceToHost) != cudaSuccess)
    throw std::runtime_error("CUDA download failed");
  return values;
}

TEST(BevFusionCuda, SweepCoordinatesIntensityAndTime)
{
  const std::vector<bev::InputPointType> input{{.5F, -.5F, 1.F, 42, 1, 7}};
  auto points = upload(input);
  auto transform = upload<float>({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 2, 3, 1});
  auto output = make_unique<float[]>(5);
  for (bool intensity : {false, true}) {
    auto cfg = config(intensity);
    bev::PreprocessCuda preprocess(cfg, nullptr, false);
    ASSERT_EQ(
      preprocess.generateSweepPoints_launch(points.get(), 1, .25F, transform.get(), output.get()),
      cudaSuccess);
    ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
    const auto values = download(output.get(), cfg.num_point_feature_size_);
    EXPECT_EQ(
      values, intensity ? (std::vector<float>{1.5F, 1.5F, 4.F, 42.F, .25F})
                        : (std::vector<float>{1.5F, 1.5F, 4.F, .25F}));
  }
}

TEST(BevFusionCuda, SparseVoxelCoordinatesCountsAndCrop)
{
  auto cfg = config();
  bev::PreprocessCuda preprocess(cfg, nullptr, true);
  auto points = upload<float>(
    {.1F, .2F, .3F, 0.F, .4F, .5F, .6F, .1F, -1.1F, -.2F, -.3F, .2F, 3.F, 0.F, 0.F, .3F});
  auto voxels = make_unique<float[]>(8 * 4 * 4);
  auto coords = make_unique<std::int32_t[]>(8 * 3);
  auto counts = make_unique<std::int32_t[]>(8);
  for (int repeat = 0; repeat < 2; ++repeat) {
    const auto size =
      preprocess.generateVoxels(points.get(), 4, voxels.get(), coords.get(), counts.get());
    ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
    ASSERT_EQ(size, 2U);
    const auto positions = download(coords.get(), 6);
    const auto numbers = download(counts.get(), 2);
    int total = 0;
    for (std::size_t i = 0; i < 2; ++i) {
      const std::array<std::int32_t, 3> xyz{
        positions[3 * i + 2], positions[3 * i + 1], positions[3 * i]};
      EXPECT_TRUE(
        (xyz == std::array<std::int32_t, 3>{2, 2, 2}) ||
        (xyz == std::array<std::int32_t, 3>{0, 1, 1}));
      EXPECT_EQ(numbers[i], xyz[0] == 2 ? 2 : 1);
      total += numbers[i];
    }
    EXPECT_EQ(total, 3);
  }
}

TEST(BevFusionCuda, CameraRemapAndPlanarCrop)
{
  bev::CameraPreprocess preprocess(nullptr, 0);
  auto rgb = upload<std::uint8_t>({1, 11, 21, 2, 12, 22, 3, 13, 23, 4, 14, 24});
  auto output = make_unique<std::uint8_t[]>(12);
  auto x = upload<float>({0.F, 1.F, 0.F, -1.F});
  auto y = upload<float>({0.F, 0.F, 1.F, 1.F});
  ASSERT_EQ(
    preprocess.remap_launch(rgb.get(), output.get(), 2, 2, 2, 2, x.get(), y.get()), cudaSuccess);
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
  EXPECT_EQ(
    download(output.get(), 12),
    (std::vector<std::uint8_t>{1, 11, 21, 2, 12, 22, 3, 13, 23, 0, 0, 0}));
  ASSERT_EQ(
    preprocess.resizeAndExtractRoi_launch(rgb.get(), output.get(), 2, 2, 2, 2, 2, 1, 0, 1),
    cudaSuccess);
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
  EXPECT_EQ(download(output.get(), 6), (std::vector<std::uint8_t>{2, 4, 12, 14, 22, 24}));
}

TEST(BevFusionCuda, CircleSuppressionKeepsSeparatedBoxes)
{
  auto cfg = config();
  bev::CircleNMS nms(cfg, nullptr);
  std::vector<bev::Box3D> boxes(3);
  boxes[0].x = 0.F;
  boxes[1].x = .1F;
  boxes[2].x = 2.F;
  boxes[0].score = .9F;
  boxes[1].score = .8F;
  boxes[2].score = .7F;
  auto device = upload(boxes);
  EXPECT_EQ(nms.circleNMS(device.get(), nullptr), (std::vector<std::uint8_t>{1, 0, 1}));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
}
