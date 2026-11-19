// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <NvInfer.h>
#include <cuda_runtime_api.h>
#include <dlfcn.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <string>

#ifndef _LIBCPP_VERSION
#error "The plugin boundary test must remain a native libc++ consumer"
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

void * load()
{
  static void * library = [] {
    const char * path = PLUGIN_TEST_LIBRARY;
    return path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : nullptr;
  }();
  return library;
}

TEST(TensorRTPlugins, LoadsAllElevenCreatorsAcrossTheCxxAbiBoundary)
{
  auto library = load();
  ASSERT_NE(library, nullptr) << dlerror();
  using GetCreators = nvinfer1::IPluginCreatorInterface * const * (*)(std::int32_t &);
  auto get_creators = reinterpret_cast<GetCreators>(dlsym(library, "getCreators"));
  ASSERT_NE(get_creators, nullptr);
  std::int32_t count = 0;
  auto creators = get_creators(count);
  ASSERT_EQ(count, 11);
  std::set<std::string> names;
  for (int i = 0; i < count; ++i) {
    const auto info = creators[i]->getInterfaceInfo();
    ASSERT_STREQ(info.kind, "PLUGIN CREATOR_V3ONE");
    ASSERT_EQ(info.major, 1);
    auto creator = static_cast<nvinfer1::IPluginCreatorV3One *>(creators[i]);
    ASSERT_NE(creator, nullptr);
    EXPECT_NE(creator->getFieldNames(), nullptr);
    EXPECT_STREQ(creator->getPluginVersion(), "1");
    names.insert(creator->getPluginName());
  }
  const std::set<std::string> expected{
    "Argsort",
    "QuickCumsumCuda",
    "GetIndicePairsImplicitGemm",
    "GetIndicePairs",
    "ImplicitGemm",
    "IndiceConv",
    "MultiScaleDeformableAttentionPlugin",
    "RotatePlugin",
    "SegmentCSR",
    "SelectAndPadPlugin",
    "CustomUnique"};
  EXPECT_EQ(names, expected);
}

TEST(TensorRTPlugins, BuildsSerializesRestoresAndExecutesArgsort)
{
  ASSERT_NE(load(), nullptr) << dlerror();
  auto registry = getPluginRegistry();
  auto raw_creator = registry->getCreator("Argsort", "1", "");
  ASSERT_NE(raw_creator, nullptr);
  ASSERT_STREQ(raw_creator->getInterfaceInfo().kind, "PLUGIN CREATOR_V3ONE");
  ASSERT_EQ(raw_creator->getInterfaceInfo().major, 1);
  auto creator = static_cast<nvinfer1::IPluginCreatorV3One *>(raw_creator);
  nvinfer1::PluginFieldCollection fields{};
  std::unique_ptr<nvinfer1::IPluginV3> plugin(
    creator->createPlugin("sort", &fields, nvinfer1::TensorRTPhase::kBUILD));
  ASSERT_NE(plugin, nullptr);
  std::unique_ptr<nvinfer1::IBuilder> builder(nvinfer1::createInferBuilder(logger));
  ASSERT_NE(builder, nullptr);
  std::unique_ptr<nvinfer1::INetworkDefinition> network(builder->createNetworkV2(0));
  std::unique_ptr<nvinfer1::IBuilderConfig> config(builder->createBuilderConfig());
  auto input = network->addInput("values", nvinfer1::DataType::kINT64, nvinfer1::Dims{1, {4}});
  ASSERT_NE(input, nullptr);
  nvinfer1::ITensor * inputs[] = {input};
  auto layer = network->addPluginV3(inputs, 1, nullptr, 0, *plugin);
  ASSERT_NE(layer, nullptr);
  layer->getOutput(0)->setName("order");
  network->markOutput(*layer->getOutput(0));
  std::unique_ptr<nvinfer1::IHostMemory> plan(builder->buildSerializedNetwork(*network, *config));
  ASSERT_NE(plan, nullptr);
  std::unique_ptr<nvinfer1::IRuntime> runtime(nvinfer1::createInferRuntime(logger));
  std::unique_ptr<nvinfer1::ICudaEngine> engine(
    runtime->deserializeCudaEngine(plan->data(), plan->size()));
  ASSERT_NE(engine, nullptr);
  std::unique_ptr<nvinfer1::IExecutionContext> context(engine->createExecutionContext());
  ASSERT_NE(context, nullptr);
  std::int64_t * device_input = nullptr;
  std::int64_t * device_output = nullptr;
  ASSERT_EQ(
    cudaMalloc(reinterpret_cast<void **>(&device_input), 4 * sizeof(std::int64_t)), cudaSuccess);
  ASSERT_EQ(
    cudaMalloc(reinterpret_cast<void **>(&device_output), 4 * sizeof(std::int64_t)), cudaSuccess);
  const std::array<std::int64_t, 4> values{30, 10, 40, 20};
  std::array<std::int64_t, 4> result{};
  EXPECT_EQ(
    cudaMemcpy(device_input, values.data(), sizeof(values), cudaMemcpyHostToDevice), cudaSuccess);
  EXPECT_TRUE(context->setTensorAddress("values", device_input));
  EXPECT_TRUE(context->setTensorAddress("order", device_output));
  cudaStream_t stream;
  ASSERT_EQ(cudaStreamCreate(&stream), cudaSuccess);
  EXPECT_TRUE(context->enqueueV3(stream));
  EXPECT_EQ(cudaStreamSynchronize(stream), cudaSuccess);
  EXPECT_EQ(
    cudaMemcpy(result.data(), device_output, sizeof(result), cudaMemcpyDeviceToHost), cudaSuccess);
  EXPECT_EQ(result, (std::array<std::int64_t, 4>{1, 3, 0, 2}));
  EXPECT_EQ(cudaStreamDestroy(stream), cudaSuccess);
  EXPECT_EQ(cudaFree(device_output), cudaSuccess);
  EXPECT_EQ(cudaFree(device_input), cudaSuccess);
}
}  // namespace
