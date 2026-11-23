// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/cuda_utils/cuda_unique_ptr.hpp>
#include <autoware/cuda_utils/stream_unique_ptr.hpp>
#include <autoware/tensorrt_common/tensorrt_common.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using autoware::tensorrt_common::ProfileDims;
using autoware::tensorrt_common::TrtCommon;
using autoware::tensorrt_common::TrtCommonConfig;
const nvinfer1::Dims shape{2, {1, 4}};

class RuntimeCache : public testing::Test
{
protected:
  void SetUp() override
  {
    int count = 0;
    ASSERT_EQ(cudaGetDeviceCount(&count), cudaSuccess);
    ASSERT_GT(count, 0) << "This runtime gate requires a CUDA device";
    directory = fs::path(std::getenv("TEST_TMPDIR")) /
                testing::UnitTest::GetInstance()->current_test_info()->name();
    fs::create_directories(directory / "models");
    fs::create_directories(directory / "cache");
    model = directory / "models/dummy.onnx";
    fs::copy_file(DUMMY_ONNX_PATH, model, fs::copy_options::overwrite_existing);
    fs::permissions(model, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
    fs::permissions(directory / "models", fs::perms::owner_read | fs::perms::owner_exec);
    engine = directory / "cache/dummy.engine";
  }

  void TearDown() override
  {
    if (directory.empty()) return;
    fs::permissions(directory / "models", fs::perms::owner_all);
    fs::permissions(directory / "cache", fs::perms::owner_all);
    fs::remove_all(directory);
  }

  std::unique_ptr<TrtCommon> make() const
  {
    return std::make_unique<TrtCommon>(TrtCommonConfig(model, "fp32", engine, 1ULL << 26U));
  }

  static bool setup(TrtCommon & trt)
  {
    auto profiles = std::make_unique<std::vector<ProfileDims>>();
    for (const char * name : {"required_a", "required_b", "optional_present"}) {
      profiles->emplace_back(name, shape, shape, shape);
    }
    return trt.setup(std::move(profiles));
  }

  static void infer(TrtCommon & trt, float base = 16.0F)
  {
    using autoware::cuda_utils::make_unique;
    auto stream = autoware::cuda_utils::makeCudaStream();
    const std::array<float, 12> values{1, 2, 3, 4, 5, 5, 5, 5, 10, 10, 10, 10};
    auto input = make_unique<float[]>(values.size());
    auto output = make_unique<float[]>(4);
    ASSERT_EQ(
      cudaMemcpy(input.get(), values.data(), sizeof(values), cudaMemcpyHostToDevice), cudaSuccess);
    int offset = 0;
    for (const char * name : {"required_a", "required_b", "optional_present"}) {
      ASSERT_TRUE(trt.setInputShape(name, shape));
      ASSERT_TRUE(trt.setTensorAddress(name, input.get() + offset));
      offset += 4;
    }
    ASSERT_TRUE(trt.setTensorAddress("sum", output.get()));
    ASSERT_TRUE(trt.enqueueV3(*stream));
    ASSERT_EQ(cudaStreamSynchronize(*stream), cudaSuccess);
    std::array<float, 4> actual{};
    ASSERT_EQ(
      cudaMemcpy(actual.data(), output.get(), sizeof(actual), cudaMemcpyDeviceToHost), cudaSuccess);
    for (int i = 0; i < 4; ++i) EXPECT_FLOAT_EQ(actual[i], base + i);
  }

  fs::path directory, model, engine;
};

TEST_F(RuntimeCache, JsonExportPreservesConfiguredEnginePath)
{
  auto trt = make();
  ASSERT_TRUE(setup(*trt));
  EXPECT_EQ(trt->getTrtCommonConfig()->engine_path, engine);
  infer(*trt);
}

TEST_F(RuntimeCache, ReadOnlyModelAndSeparateCacheCanBeReused)
{
  {
    std::ofstream forbidden(model.parent_path() / "cannot_write");
    ASSERT_FALSE(forbidden.is_open());
    auto trt = make();
    ASSERT_TRUE(setup(*trt));
    infer(*trt);
  }
  const auto modified = fs::last_write_time(engine);
  const auto bytes = fs::file_size(engine);
  {
    auto trt = make();
    ASSERT_TRUE(setup(*trt));
    infer(*trt);
  }
  EXPECT_EQ(fs::last_write_time(engine), modified);
  EXPECT_EQ(fs::file_size(engine), bytes);
  EXPECT_EQ(
    std::distance(fs::directory_iterator(model.parent_path()), fs::directory_iterator()), 1);
}

TEST_F(RuntimeCache, MissingCacheDirectoryIsRejected)
{
  engine = directory / "missing/dummy.engine";
  auto trt = make();
  EXPECT_FALSE(setup(*trt));
  EXPECT_FALSE(fs::exists(engine));
}

TEST_F(RuntimeCache, UnwritableCacheIsRejected)
{
  fs::permissions(engine.parent_path(), fs::perms::owner_read | fs::perms::owner_exec);
  auto trt = make();
  EXPECT_FALSE(setup(*trt));
  EXPECT_FALSE(fs::exists(engine));
}

TEST_F(RuntimeCache, TruncatedCachedPlanIsRebuilt)
{
  std::ofstream(engine, std::ios::binary) << "bad";
  auto trt = make();
  ASSERT_TRUE(setup(*trt));
  EXPECT_GT(fs::file_size(engine), 27U);
  infer(*trt);
}

TEST_F(RuntimeCache, CorruptCachedBodyIsRejected)
{
  {
    auto trt = make();
    ASSERT_TRUE(setup(*trt));
  }
  fs::resize_file(engine, 27);
  auto trt = make();
  EXPECT_FALSE(setup(*trt));
}

TEST_F(RuntimeCache, ChangedShapeProfileRebuildsTheCache)
{
  {
    auto trt = make();
    ASSERT_TRUE(setup(*trt));
  }
  const auto changed = fs::last_write_time(engine);
  auto trt = make();
  auto profiles = std::make_unique<std::vector<ProfileDims>>();
  const nvinfer1::Dims larger{2, {2, 4}};
  for (const char * name : {"required_a", "required_b", "optional_present"}) {
    profiles->emplace_back(name, shape, larger, larger);
  }
  ASSERT_TRUE(trt->setup(std::move(profiles)));
  EXPECT_NE(fs::last_write_time(engine), changed);
  infer(*trt);
}

TEST_F(RuntimeCache, ModelRevisionUsesAnExplicitNewCacheKey)
{
  {
    auto trt = make();
    ASSERT_TRUE(setup(*trt));
    infer(*trt);
  }
  const auto first_engine = engine;
  const auto changed = fs::last_write_time(first_engine);
  std::ifstream input(model, std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(input)), {});
  const auto add = data.find("Add");
  ASSERT_NE(add, std::string::npos);
  data.replace(add, 3, "Sub");
  fs::permissions(model, fs::perms::owner_read | fs::perms::owner_write);
  std::ofstream(model, std::ios::binary).write(data.data(), data.size());
  fs::permissions(model, fs::perms::owner_read);
  {
    // Same-shaped model contents are not part of the loader's validation contract.
    auto trt = make();
    ASSERT_TRUE(setup(*trt));
    infer(*trt);
  }
  engine = directory / "cache/dummy-revision2.engine";
  {
    auto trt = make();
    ASSERT_TRUE(setup(*trt));
    infer(*trt, 6.0F);
  }
  EXPECT_EQ(fs::last_write_time(first_engine), changed);
  EXPECT_TRUE(fs::exists(engine));
}
TEST_F(RuntimeCache, PreparedEngineLoadsWithoutOnnxOrBuilder)
{
  {
    auto trt = make();
    ASSERT_TRUE(setup(*trt));
  }
  const auto changed = fs::last_write_time(engine);
  fs::permissions(model.parent_path(), fs::perms::owner_all);
  fs::remove(model);
  TrtCommon trt(TrtCommonConfig(model, "fp32", engine, 1ULL << 26U, -1, false, false));
  EXPECT_EQ(trt.getBuilderConfig(), nullptr);
  EXPECT_EQ(trt.getNetwork(), nullptr);
  EXPECT_EQ(trt.getNbIOTensors(), 4);
  EXPECT_STREQ(trt.getIOTensorName(3), "sum");
  EXPECT_EQ(trt.getIOTensorName(-1), nullptr);
  EXPECT_EQ(trt.getInputDims(0).d[1], 4);
  EXPECT_EQ(trt.getOutputDims(0).d[1], 4);
  EXPECT_TRUE(setup(trt));
  infer(trt);
  EXPECT_EQ(trt.getBuilderConfig(), nullptr);
  EXPECT_EQ(trt.getNetwork(), nullptr);
  EXPECT_EQ(fs::last_write_time(engine), changed);
}

TEST_F(RuntimeCache, PreparedOnlyRejectsMissingAndCorruptEnginesWithoutGenerating)
{
  const auto config = TrtCommonConfig(model, "fp32", engine, 1ULL << 26U, -1, false, false);
  EXPECT_THROW({ TrtCommon trt(config); }, std::runtime_error);
  EXPECT_FALSE(fs::exists(engine));
  std::ofstream(engine) << "bad";
  EXPECT_THROW({ TrtCommon trt(config); }, std::runtime_error);
  EXPECT_EQ(fs::file_size(engine), 3U);
}

TEST_F(RuntimeCache, PreparedOnlyRejectsProfileMismatchWithoutRebuilding)
{
  {
    auto trt = make();
    ASSERT_TRUE(setup(*trt));
  }
  const auto changed = fs::last_write_time(engine);
  TrtCommon trt(TrtCommonConfig(model, "fp32", engine, 1ULL << 26U, -1, false, false));
  auto profiles = std::make_unique<std::vector<ProfileDims>>();
  const nvinfer1::Dims larger{2, {2, 4}};
  for (const char * name : {"required_a", "required_b", "optional_present"}) {
    profiles->emplace_back(name, shape, larger, larger);
  }
  EXPECT_FALSE(trt.setup(std::move(profiles)));
  EXPECT_EQ(fs::last_write_time(engine), changed);
  EXPECT_EQ(trt.getBuilderConfig(), nullptr);
}

}  // namespace
