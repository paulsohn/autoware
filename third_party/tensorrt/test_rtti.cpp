// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <NvInfer.h>
#include <gtest/gtest.h>

#include <iostream>
#include <memory>

#ifndef _LIBCPP_VERSION
#error "This regression must exercise the libc++ consumer of the GNU-built SDK"
#endif

namespace
{
class Logger : public nvinfer1::ILogger
{
  void log(Severity severity, const char * message) noexcept override
  {
    if (severity <= Severity::kWARNING) std::cerr << message << '\n';
  }
} logger;

TEST(TensorRTRuntime, CastsLayersReturnedByTheVendorSdk)
{
  std::unique_ptr<nvinfer1::IBuilder> builder(nvinfer1::createInferBuilder(logger));
  ASSERT_NE(builder, nullptr);
  std::unique_ptr<nvinfer1::INetworkDefinition> network(builder->createNetworkV2(0));
  ASSERT_NE(network, nullptr);
  auto input = network->addInput("input", nvinfer1::DataType::kFLOAT, nvinfer1::Dims4{1, 1, 2, 2});
  ASSERT_NE(input, nullptr);
  const float weight = 1.0F;
  auto convolution = network->addConvolutionNd(
    *input, 1, nvinfer1::DimsHW{1, 1}, {nvinfer1::DataType::kFLOAT, &weight, 1},
    {nvinfer1::DataType::kFLOAT, nullptr, 0});
  ASSERT_NE(convolution, nullptr);
  auto pooling = network->addPoolingNd(
    *convolution->getOutput(0), nvinfer1::PoolingType::kAVERAGE, nvinfer1::DimsHW{2, 2});
  ASSERT_NE(pooling, nullptr);
  EXPECT_EQ(dynamic_cast<nvinfer1::IConvolutionLayer *>(network->getLayer(0)), convolution);
  EXPECT_EQ(dynamic_cast<nvinfer1::IPoolingLayer *>(network->getLayer(1)), pooling);
}
}  // namespace
