// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/camera_streampetr/network/camera_ego_mask.hpp>
#include <autoware/camera_streampetr/network/memory.cuh>
#include <autoware/camera_streampetr/network/preprocess.hpp>
#include <autoware/camera_streampetr/postprocess/postprocess_kernel.hpp>
#include <autoware/cuda_utils/cuda_unique_ptr.hpp>

#include <cuda_runtime_api.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace petr = autoware::camera_streampetr;
using autoware::cuda_utils::make_unique;

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

TEST(StreamPetrCuda, BgrResizeNormalizeCropAndCameraOffset)
{
  auto image = upload<std::uint8_t>({1, 11, 21, 2, 12, 22, 3, 13, 23, 4, 14, 24});
  auto mean = upload<float>({20, 10, 0});
  auto stddev = upload<float>({2, 2, 2});
  auto output = upload<float>(std::vector<float>(9, -100));
  ASSERT_EQ(petr::convert_bgr_to_rgb_launch(image.get(), 2, 2, nullptr), cudaSuccess);
  ASSERT_EQ(
    petr::resize_and_extract_roi_launch(
      image.get(), output.get(), 3, 2, 2, 2, 2, 2, 1, 0, 1, mean.get(), stddev.get(), nullptr),
    cudaSuccess);
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
  EXPECT_EQ(download(output.get(), 9), (std::vector<float>{-100, -100, -100, 1, 2, 1, 2, 1, 2}));
  ASSERT_EQ(
    petr::resize_and_extract_roi_launch(
      image.get(), output.get(), 0, 2, 2, 1, 1, 1, 1, 0, 0, mean.get(), stddev.get(), nullptr),
    cudaSuccess);
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
  EXPECT_EQ(download(output.get(), 3), (std::vector<float>{1.25F, 1.25F, 1.25F}));
}

TEST(StreamPetrCuda, EgoMaskParsingRasterAndRgbFill)
{
  auto polygons = petr::parse_polygons_yaml_text(
    "polygons: [{points: [0, 0, 0.5, 0, 0.5, 0.5, 0, 0.5], normalized: true}]");
  auto mask = petr::build_ego_mask_raster(polygons, 8, 8);
  ASSERT_EQ(polygons.size(), 1U);
  EXPECT_TRUE(polygons.front().normalized);
  ASSERT_EQ(mask.size(), 64U);
  EXPECT_EQ(mask[1 * 8 + 1], 255);
  EXPECT_EQ(mask[7 * 8 + 7], 0);
  EXPECT_THROW(petr::parse_polygons_yaml_text("polygons: [[0, 0, 1, 1]]"), std::runtime_error);
  EXPECT_THROW(petr::parse_polygons_yaml_text("polygons: {}"), std::runtime_error);
  EXPECT_THROW(
    petr::parse_polygons_yaml_text("polygons: [[0, 0, 1, 0, 1, 1]]\npolygons_normalized: []"),
    std::runtime_error);
  auto image = upload<std::uint8_t>(std::vector<std::uint8_t>(64 * 3, 99));
  auto device_mask = upload(mask);
  ASSERT_EQ(
    petr::apply_ego_mask_launch(image.get(), device_mask.get(), 8, 8, 10, 20, 30, nullptr),
    cudaSuccess);
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
  auto rgb = download(image.get(), 64 * 3);
  EXPECT_EQ(
    (std::vector<std::uint8_t>{rgb[27], rgb[28], rgb[29]}),
    (std::vector<std::uint8_t>{10, 20, 30}));
  EXPECT_EQ(rgb.back(), 99);
}

TEST(StreamPetrCuda, TemporalTimestampsRebaseAcrossFrames)
{
  cudaStream_t stream;
  ASSERT_EQ(cudaStreamCreate(&stream), cudaSuccess);
  auto pre = upload<float>({0, 0});
  auto post = upload<float>({0, 1, 2, 3});
  Memory memory{};
  memory.init(stream, 2, 4);
  memory.pre_buf = pre.get();
  memory.post_buf = post.get();
  memory.step_post(10.F);
  memory.step_pre(12.F);
  ASSERT_EQ(cudaStreamSynchronize(stream), cudaSuccess);
  EXPECT_EQ(download(pre.get(), 2), (std::vector<float>{2, 3}));
  ASSERT_EQ(
    cudaMemcpyAsync(post.get(), pre.get(), 2 * sizeof(float), cudaMemcpyDeviceToDevice, stream),
    cudaSuccess);
  memory.step_post(12.F);
  memory.step_pre(15.F);
  ASSERT_EQ(cudaStreamSynchronize(stream), cudaSuccess);
  EXPECT_EQ(download(pre.get(), 2), (std::vector<float>{5, 6}));
  ASSERT_EQ(cudaFreeAsync(memory.mem_buf, stream), cudaSuccess);
  ASSERT_EQ(cudaStreamDestroy(stream), cudaSuccess);
}

TEST(StreamPetrCuda, DetectionDecodeRangeAndCircleSuppression)
{
  cudaStream_t stream = nullptr;
  petr::PostProcessingConfig config(2, .5F, {.5, .5}, {.3, .3}, 4, {-10, -10, -10, 10, 10, 10});
  petr::PostprocessCuda postprocess(config, stream);
  auto scores = upload<float>({8, 7, 6, 9, -8, -8, -8, -8});
  std::vector<float> boxes(10 * 4, 0.F);
  boxes[0] = 0;
  boxes[1] = .1F;
  boxes[2] = 2;
  boxes[3] = 20;
  for (int i = 0; i < 4; ++i) boxes[7 * 4 + i] = 1.F;
  auto coordinates = upload(boxes);
  std::vector<petr::Box3D> result;
  ASSERT_EQ(
    postprocess.generate_detected_boxes3d_launch(scores.get(), coordinates.get(), result, stream),
    cudaSuccess);
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
  ASSERT_EQ(result.size(), 2U);
  EXPECT_FLOAT_EQ(result[0].x, 0.F);
  EXPECT_FLOAT_EQ(result[1].x, 2.F);
  EXPECT_GT(result[0].score, result[1].score);
  EXPECT_EQ(result[0].label, 0);
  EXPECT_FLOAT_EQ(result[0].width, 1.F);
  EXPECT_FLOAT_EQ(result[0].yaw, 0.F);
}
