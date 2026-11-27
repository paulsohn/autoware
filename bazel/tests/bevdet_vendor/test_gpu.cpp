// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/cuda_utils/cuda_unique_ptr.hpp>

#include <bevdet.h>
#include <cpu_jpegdecoder.h>
#include <gtest/gtest.h>
#include <iou3d_nms.h>
#include <jpeglib.h>
#include <precision_config.h>
#include <preprocess.h>

#include <cstdlib>
#include <vector>

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

TEST(BevDetVendor, RgbInterleavedToBgrPlanar)
{
  auto image = upload<unsigned char>({1, 11, 21, 2, 12, 22, 3, 13, 23, 4, 14, 24});
  auto output = make_unique<unsigned char[]>(12);
  convert_RGBHWC_to_BGRCHW(image.get(), output.get(), 3, 2, 2);
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
  EXPECT_EQ(
    download(output.get(), 12),
    (std::vector<unsigned char>{21, 22, 23, 24, 11, 12, 13, 14, 1, 2, 3, 4}));
}

TEST(BevDetVendor, CpuJpegDecodeAndDeviceLayout)
{
  jpeg_compress_struct compressor{};
  jpeg_error_mgr errors{};
  compressor.err = jpeg_std_error(&errors);
  jpeg_create_compress(&compressor);
  unsigned char * bytes = nullptr;
  unsigned long length = 0;
  jpeg_mem_dest(&compressor, &bytes, &length);
  compressor.image_width = 2;
  compressor.image_height = 2;
  compressor.input_components = 3;
  compressor.in_color_space = JCS_RGB;
  jpeg_set_defaults(&compressor);
  jpeg_set_quality(&compressor, 100, true);
  jpeg_start_compress(&compressor, true);
  std::vector<unsigned char> rgb(12);
  for (int i = 0; i < 4; ++i) {
    rgb[i * 3] = 40;
    rgb[i * 3 + 1] = 80;
    rgb[i * 3 + 2] = 120;
  }
  while (compressor.next_scanline < compressor.image_height) {
    JSAMPROW row = rgb.data() + compressor.next_scanline * 6;
    jpeg_write_scanlines(&compressor, &row, 1);
  }
  jpeg_finish_compress(&compressor);
  std::vector<char> jpeg(bytes, bytes + length);
  jpeg_destroy_compress(&compressor);
  std::free(bytes);
  auto output = make_unique<unsigned char[]>(12);
  ASSERT_EQ(decode_cpu({jpeg}, output.get(), 2, 2), EXIT_SUCCESS);
  auto decoded = download(output.get(), 12);
  for (int i = 0; i < 12; ++i) EXPECT_NEAR(decoded[i], i < 4 ? 120 : (i < 8 ? 80 : 40), 2);
}

TEST(BevDetVendor, TemporalRingPoseAndReset)
{
  adjFrame history(2, 2 * sizeof(float));
  EXPECT_FALSE(history.havingBuffer(0));
  auto first = upload<float>({1, 2});
  auto second = upload<float>({3, 4});
  Eigen::Quaternion<float> rotation = Eigen::Quaternion<float>::Identity();
  history.saveFrameBuffer(first.get(), "scene", rotation, Eigen::Translation3f(1, 2, 3));
  EXPECT_TRUE(history.havingBuffer(1));
  EXPECT_EQ(
    download(static_cast<const float *>(history.getFrameBuffer(1)), 2), (std::vector<float>{1, 2}));
  history.saveFrameBuffer(second.get(), "scene", rotation, Eigen::Translation3f(4, 5, 6));
  EXPECT_EQ(
    download(static_cast<const float *>(history.getFrameBuffer(0)), 2), (std::vector<float>{3, 4}));
  EXPECT_EQ(
    download(static_cast<const float *>(history.getFrameBuffer(1)), 2), (std::vector<float>{1, 2}));
  Eigen::Quaternion<float> actual_rotation;
  Eigen::Translation3f actual_translation;
  history.getEgo2Global(0, actual_rotation, actual_translation);
  EXPECT_TRUE(actual_rotation.isApprox(rotation));
  EXPECT_TRUE(actual_translation.vector().isApprox(Eigen::Vector3f(4, 5, 6)));
  history.reset();
  EXPECT_FALSE(history.havingBuffer(0));
  history.saveFrameBuffer(first.get(), "new_scene", rotation, Eigen::Translation3f(0, 0, 0));
  EXPECT_EQ(history.lastScenesToken(), "new_scene");
  EXPECT_EQ(
    download(static_cast<const float *>(history.getFrameBuffer(1)), 2), (std::vector<float>{1, 2}));
}

TEST(BevDetVendor, RotatedSuppression)
{
  Iou3dNmsCuda nms(3, 1, .2F);
  auto boxes = upload<float>(
    {0, 0, 0, 2, 2, 2, 0, 0, 0, .1F, 0, 0, 2, 2, 2, 0, 0, 0, 5, 0, 0, 2, 2, 2, 0, 0, 0});
  auto indices = upload<int>({0, 1, 2});
  long kept[3] = {-1, -1, -1};
  ASSERT_EQ(nms.DoIou3dNms(3, boxes.get(), indices.get(), kept), 2);
  EXPECT_EQ(kept[0], 0);
  EXPECT_EQ(kept[1], 2);
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
}

TEST(BevDetVendor, BothPrecisionsAndAllPluginCreators)
{
  bevdet_precision::setPrecision("fp32");
  EXPECT_FALSE(bevdet_precision::isFloat16());
  bevdet_precision::setPrecision("fp16");
  EXPECT_TRUE(bevdet_precision::isFloat16());
  auto * registry = getPluginRegistry();
  ASSERT_NE(registry, nullptr);
  for (const auto * name : {"Preprocess", "BEVPool", "AlignBEV", "GatherBEV"}) {
    EXPECT_NE(registry->getCreator(name, "1", ""), nullptr) << name;
  }
}
