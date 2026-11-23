// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
// SDK-only builder diagnostic: no Autoware implementation, ROS or ament lookup.
#include <opencv2/dnn.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <NvInfer.h>
#include <NvInferPlugin.h>
#include <NvOnnxParser.h>
#include <cuda_runtime_api.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
class Logger : public nvinfer1::ILogger
{
public:
  std::atomic_bool myelin_error{false};
  void log(Severity severity, const char * message) noexcept override
  {
    if (std::strstr(message, "MyelinCheckException")) myelin_error = true;
    if (severity <= Severity::kWARNING) {
      std::fputs(message, stderr);
      std::fputc('\n', stderr);
    }
  }
};

class MinMax : public nvinfer1::IInt8MinMaxCalibrator
{
public:
  explicit MinMax(nvinfer1::Dims dims)
  {
    auto image = cv::imread(CALIBRATION_IMAGE_PATH, cv::IMREAD_COLOR);
    if (image.empty()) throw std::runtime_error("Calibration image unavailable");
    const float scale = std::min(float(dims.d[3]) / image.cols, float(dims.d[2]) / image.rows);
    cv::Mat resized;
    cv::resize(
      image, resized, cv::Size(image.cols * scale, image.rows * scale), 0, 0, cv::INTER_CUBIC);
    cv::copyMakeBorder(
      resized, resized, 0, dims.d[2] - resized.rows, 0, dims.d[3] - resized.cols,
      cv::BORDER_CONSTANT, {114, 114, 114});
    auto blob = cv::dnn::blobFromImages(
      std::vector<cv::Mat>{resized}, 1.0, cv::Size(), cv::Scalar(), false, false, CV_32F);
    if (cudaMalloc(&device_, blob.total() * sizeof(float)) != cudaSuccess) {
      throw std::runtime_error("Calibration device allocation failed");
    }
    if (
      cudaMemcpy(
        device_, blob.ptr<float>(), blob.total() * sizeof(float), cudaMemcpyHostToDevice) !=
      cudaSuccess) {
      cudaFree(device_);
      throw std::runtime_error("Calibration upload failed");
    }
  }
  ~MinMax() override { cudaFree(device_); }
  int getBatchSize() const noexcept override { return 1; }
  bool getBatch(void * bindings[], const char *[], int count) noexcept override
  {
    if (used_ || count != 1) return false;
    bindings[0] = device_;
    used_ = true;
    return true;
  }
  const void * readCalibrationCache(std::size_t & size) noexcept override
  {
    size = 0;
    return nullptr;
  }
  void writeCalibrationCache(const void * bytes, std::size_t size) noexcept override
  {
    const auto begin = static_cast<const char *>(bytes);
    table.assign(begin, begin + size);
  }
  void * device() const { return device_; }
  std::vector<char> table;

private:
  void * device_{nullptr};
  bool used_{false};
};

class SdkCompiler : public testing::Test
{
protected:
  void SetUp() override
  {
    int count = 0;
    ASSERT_EQ(cudaGetDeviceCount(&count), cudaSuccess);
    ASSERT_GT(count, 0);
  }

  void build(bool int8, bool wrapper_flags, bool expected_success, int optimization = 3)
  {
    Logger logger;
    ASSERT_TRUE(initLibNvInferPlugins(&logger, ""));
    std::unique_ptr<nvinfer1::IBuilder> builder(nvinfer1::createInferBuilder(logger));
    ASSERT_NE(builder, nullptr);
    std::unique_ptr<nvinfer1::INetworkDefinition> network(builder->createNetworkV2(0));
    ASSERT_NE(network, nullptr);
    std::unique_ptr<nvonnxparser::IParser> parser(nvonnxparser::createParser(*network, logger));
    ASSERT_NE(parser, nullptr);
    ASSERT_TRUE(
      parser->parseFromFile(TRAFFIC_LIGHT_ONNX_PATH, int(nvinfer1::ILogger::Severity::kERROR)));
    ASSERT_EQ(network->getNbInputs(), 1);
    const auto input = network->getInput(0);
    const auto shape = input->getDimensions();
    ASSERT_EQ(shape.nbDims, 4);
    ASSERT_EQ(shape.d[0], 1);
    ASSERT_EQ(shape.d[1], 3);
    ASSERT_EQ(shape.d[2], 960);
    ASSERT_EQ(shape.d[3], 960);
    std::unique_ptr<nvinfer1::IBuilderConfig> config(builder->createBuilderConfig());
    ASSERT_NE(config, nullptr);
    config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE, 1ULL << 30U);
    config->setBuilderOptimizationLevel(optimization);
    if (optimization == 0) {
      config->setProfilingVerbosity(nvinfer1::ProfilingVerbosity::kDETAILED);
    }
    config->setFlag(nvinfer1::BuilderFlag::kFP16);
    auto profile = builder->createOptimizationProfile();
    ASSERT_NE(profile, nullptr);
    for (auto selector :
         {nvinfer1::OptProfileSelector::kMIN, nvinfer1::OptProfileSelector::kOPT,
          nvinfer1::OptProfileSelector::kMAX}) {
      ASSERT_TRUE(profile->setDimensions(input->getName(), selector, shape));
    }
    ASSERT_EQ(config->addOptimizationProfile(profile), 0);
    auto calibration = std::make_unique<MinMax>(shape);
    if (int8) {
      config->setFlag(nvinfer1::BuilderFlag::kINT8);
      config->setInt8Calibrator(calibration.get());
      if (wrapper_flags) {
        config->setFlag(nvinfer1::BuilderFlag::kPREFER_PRECISION_CONSTRAINTS);
        ASSERT_TRUE(input->setDynamicRange(0.0F, 255.0F));
      }
    }
    std::unique_ptr<nvinfer1::IHostMemory> plan(builder->buildSerializedNetwork(*network, *config));
    const auto prefix = std::filesystem::path(std::getenv("TEST_UNDECLARED_OUTPUTS_DIR")) /
                        testing::UnitTest::GetInstance()->current_test_info()->name();
    if (int8) {
      ASSERT_FALSE(calibration->table.empty());
      std::ofstream(prefix.string() + ".calibration.table", std::ios::binary)
        .write(calibration->table.data(), calibration->table.size());
    }
    RecordProperty("engine_built", bool(plan));
    RecordProperty("myelin_error", logger.myelin_error.load());
    RecordProperty("optimization_level", optimization);
    std::fprintf(
      stderr, "SDK diagnostic: int8=%d wrapper_flags=%d optimization=%d built=%d myelin=%d\n", int8,
      wrapper_flags, optimization, bool(plan), logger.myelin_error.load());
    EXPECT_EQ(bool(plan), expected_success);
    if (!plan) EXPECT_TRUE(logger.myelin_error);
    if (plan) {
      std::unique_ptr<nvinfer1::IRuntime> runtime(nvinfer1::createInferRuntime(logger));
      ASSERT_NE(runtime, nullptr);
      std::unique_ptr<nvinfer1::ICudaEngine> engine(
        runtime->deserializeCudaEngine(plan->data(), plan->size()));
      ASSERT_NE(engine, nullptr);
      ASSERT_EQ(engine->getNbIOTensors(), 2);
      const char * output_name = engine->getIOTensorName(1);
      ASSERT_EQ(engine->getTensorDataType(input->getName()), nvinfer1::DataType::kFLOAT);
      ASSERT_EQ(engine->getTensorDataType(output_name), nvinfer1::DataType::kFLOAT);
      const auto output_shape = engine->getTensorShape(output_name);
      ASSERT_GT(output_shape.nbDims, 0);
      ASSERT_EQ(output_shape.d[output_shape.nbDims - 1], 8);
      std::size_t count = 1;
      for (int i = 0; i < output_shape.nbDims; ++i) {
        ASSERT_GT(output_shape.d[i], 0);
        count *= output_shape.d[i];
      }
      float * raw_output = nullptr;
      ASSERT_EQ(
        cudaMalloc(reinterpret_cast<void **>(&raw_output), count * sizeof(float)), cudaSuccess);
      std::unique_ptr<float, decltype(&cudaFree)> device_output(raw_output, cudaFree);
      std::unique_ptr<nvinfer1::IExecutionContext> context(engine->createExecutionContext());
      ASSERT_NE(context, nullptr);
      ASSERT_TRUE(context->setTensorAddress(input->getName(), calibration->device()));
      ASSERT_TRUE(context->setTensorAddress(output_name, device_output.get()));
      struct Stream
      {
        cudaStream_t value{};
        ~Stream()
        {
          if (value) cudaStreamDestroy(value);
        }
      } stream;
      ASSERT_EQ(cudaStreamCreate(&stream.value), cudaSuccess);
      ASSERT_TRUE(context->enqueueV3(stream.value));
      ASSERT_EQ(cudaStreamSynchronize(stream.value), cudaSuccess);
      std::vector<float> values(count);
      ASSERT_EQ(
        cudaMemcpy(
          values.data(), device_output.get(), count * sizeof(float), cudaMemcpyDeviceToHost),
        cudaSuccess);
      ASSERT_TRUE(
        std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); }));
      float peak = 0;
      for (std::size_t i = 0; i < count; i += 8) {
        // Class 0 is background; only traffic-light classes 1 and 2 are usable.
        peak = std::max(peak, values[i + 4] * std::max(values[i + 6], values[i + 7]));
      }
      RecordProperty("peak_traffic_light_confidence", peak);
      std::fprintf(stderr, "SDK diagnostic: peak traffic-light confidence=%f\n", peak);
      EXPECT_GT(peak, 0.35F);
      if (optimization == 0) {
        std::unique_ptr<nvinfer1::IEngineInspector> inspector(engine->createEngineInspector());
        ASSERT_NE(inspector, nullptr);
        const auto layout =
          inspector->getEngineInformation(nvinfer1::LayerInformationFormat::kJSON);
        ASSERT_NE(layout, nullptr);
        std::ofstream(prefix.string() + ".layers.json") << layout;
      }
      std::ofstream(prefix.string() + ".engine", std::ios::binary)
        .write(static_cast<const char *>(plan->data()), plan->size());
    }
  }
};

TEST_F(SdkCompiler, Fp16Control)
{
  build(false, false, true);
}
TEST_F(SdkCompiler, MatchingInt8Profile)
{
  build(true, true, false);
}
TEST_F(SdkCompiler, ReducedInt8Flags)
{
  build(true, false, false);
}
TEST_F(SdkCompiler, LowerOptimizationLevel)
{
  build(true, true, true, 0);
}
}  // namespace
