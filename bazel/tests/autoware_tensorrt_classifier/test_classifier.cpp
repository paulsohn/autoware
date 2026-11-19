// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/cuda_utils/cuda_unique_ptr.hpp>
#include <autoware/cuda_utils/stream_unique_ptr.hpp>
#include <autoware/tensorrt_classifier/tensorrt_classifier.hpp>

#include <autoware/tensorrt_classifier/preprocess.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
using autoware::tensorrt_classifier::TrtClassifier;

class Classifier : public testing::TestWithParam<bool>
{
protected:
  void SetUp() override
  {
    directory_ = std::filesystem::path(std::getenv("TEST_TMPDIR")) / (GetParam() ? "cuda" : "cpu");
    std::filesystem::create_directories(directory_);
    path_ = directory_ / "channel_means.onnx";
    std::filesystem::copy_file(
      CLASSIFIER_ONNX_PATH, path_, std::filesystem::copy_options::overwrite_existing);
  }
  void TearDown() override { std::filesystem::remove_all(directory_); }
  std::filesystem::path directory_, path_;
};

TEST_P(Classifier, BatchedInferenceAndCachedEngine)
{
  const std::vector<cv::Mat> images{
    cv::Mat(2, 2, CV_8UC3, cv::Scalar(20, 40, 60)), cv::Mat(2, 2, CV_8UC3, cv::Scalar(90, 40, 10))};
  for (int pass = 0; pass < 2; ++pass) {
    TrtClassifier classifier(
      path_, "fp32", {0, 0, 0}, {2, 4, 5}, "",
      autoware::tensorrt_common::CalibrationConfig("MinMax", false, false, 0.0), GetParam());
    EXPECT_EQ(classifier.getBatchSize(), 2);
    if (GetParam()) {
      classifier.initPreprocessBuffer(2, 2);
      classifier.initPreprocessBuffer(4, 4);
    }
    std::vector<int> classes{99};
    std::vector<float> probabilities{99};
    ASSERT_TRUE(classifier.doInference(images, classes, probabilities));
    ASSERT_EQ(classes, (std::vector<int>{2, 0}));
    ASSERT_EQ(probabilities.size(), 2U);
    EXPECT_NEAR(probabilities[0], 12.0F, 1e-4F);
    EXPECT_NEAR(probabilities[1], 45.0F, 1e-4F);
    ASSERT_TRUE(classifier.doInference(images, classes, probabilities));
    EXPECT_EQ(classes.size(), 2U);
    EXPECT_EQ(probabilities.size(), 2U);
  }
}

TEST_P(Classifier, ResizeAndZeroLetterbox)
{
  TrtClassifier classifier(
    path_, "fp32", {0, 0, 0}, {2, 4, 5}, "",
    autoware::tensorrt_common::CalibrationConfig("MinMax", false, false, 0.0), GetParam());
  const std::vector<cv::Mat> images{
    cv::Mat(2, 4, CV_8UC3, cv::Scalar(20, 40, 60)), cv::Mat(2, 4, CV_8UC3, cv::Scalar(90, 40, 10))};
  std::vector<int> classes;
  std::vector<float> probabilities;
  ASSERT_TRUE(classifier.doInference(images, classes, probabilities));
  EXPECT_EQ(classes, (std::vector<int>{2, 0}));
  ASSERT_EQ(probabilities.size(), 2U);
  EXPECT_NEAR(probabilities[0], 6.0F, 1e-4F);
  EXPECT_NEAR(probabilities[1], 22.5F, 1e-4F);
}

INSTANTIATE_TEST_SUITE_P(Preallocation, Classifier, testing::Bool());

TEST(ClassifierGpu, BatchedRgbNormalizationAndZeroLetterbox)
{
  using autoware::cuda_utils::make_unique;
  auto stream = autoware::cuda_utils::makeCudaStream();
  std::vector<unsigned char> input(2 * 2 * 2 * 3);
  for (int i = 0; i < 8; ++i) {
    input[i * 3] = i < 4 ? 20 : 90;
    input[i * 3 + 1] = 40;
    input[i * 3 + 2] = i < 4 ? 60 : 10;
  }
  auto source = make_unique<unsigned char[]>(input.size());
  auto output = make_unique<float[]>(2 * 3 * 4 * 2);
  ASSERT_EQ(
    cudaMemcpy(source.get(), input.data(), input.size(), cudaMemcpyHostToDevice), cudaSuccess);
  resize_bilinear_letterbox_nhwc_to_nchw32_batch_gpu(
    output.get(), source.get(), 2, 4, 3, 2, 2, 3, 2, 1.0F, *stream);
  ASSERT_EQ(cudaStreamSynchronize(*stream), cudaSuccess);
  std::vector<float> result(48);
  ASSERT_EQ(
    cudaMemcpy(result.data(), output.get(), result.size() * sizeof(float), cudaMemcpyDeviceToHost),
    cudaSuccess);
  const float mean[] = {123.675F, 116.28F, 103.53F};
  const float scale[] = {58.395F, 57.12F, 57.375F};
  const float pixels[2][3] = {{60, 40, 20}, {10, 40, 90}};
  for (int b = 0; b < 2; ++b) {
    for (int c = 0; c < 3; ++c) {
      for (int i = 0; i < 8; ++i) {
        const float expected = ((i < 4 ? pixels[b][c] : 0.0F) - mean[c]) / scale[c];
        EXPECT_NEAR(result[b * 24 + c * 8 + i], expected, 1e-5F);
      }
    }
  }
}
}  // namespace
