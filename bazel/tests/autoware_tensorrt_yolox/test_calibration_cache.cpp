// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/tensorrt_yolox/calibrator.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
namespace fs = std::filesystem;
using namespace autoware::tensorrt_yolox;

class CalibrationFiles : public testing::Test
{
protected:
  void SetUp() override
  {
    int count = 0;
    ASSERT_EQ(cudaGetDeviceCount(&count), cudaSuccess);
    ASSERT_GT(count, 0);
    directory = fs::path(std::getenv("TEST_TMPDIR")) /
                testing::UnitTest::GetInstance()->current_test_info()->name();
    fs::create_directories(directory / "model");
    fs::create_directories(directory / "cache");
    provided = directory / "model/input.table";
    output = directory / "cache/output.table";
    std::ofstream(provided) << "supplied calibration bytes";
    fs::permissions(provided, fs::perms::owner_read);
    fs::permissions(provided.parent_path(), fs::perms::owner_read | fs::perms::owner_exec);
    std::ofstream denied(provided);
    ASSERT_FALSE(denied.is_open());
  }

  void TearDown() override
  {
    if (directory.empty()) return;
    fs::permissions(provided.parent_path(), fs::perms::owner_all);
    fs::remove_all(directory);
  }

  static std::string read(const fs::path & path)
  {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  void check_calibration(nvinfer1::IInt8Calibrator & calibrator)
  {
    std::size_t size = 0;
    const auto bytes = calibrator.readCalibrationCache(size);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(std::string(static_cast<const char *>(bytes), size), read(provided));
    const std::string generated = "generated calibration bytes";
    calibrator.writeCalibrationCache(generated.data(), generated.size());
    EXPECT_EQ(read(output), generated);
    EXPECT_EQ(read(provided), "supplied calibration bytes");
    const auto cached = calibrator.readCalibrationCache(size);
    ASSERT_NE(cached, nullptr);
    EXPECT_EQ(std::string(static_cast<const char *>(cached), size), generated);
  }

  ImageStream stream{1, nvinfer1::Dims4{1, 3, 2, 2}, {}};
  fs::path directory, provided, output;
};

TEST_F(CalibrationFiles, EntropyPrefersGeneratedCacheAndPreservesProvidedInput)
{
  Int8EntropyCalibrator calibrator(stream, output, 1.0, true, provided);
  check_calibration(calibrator);
}

TEST_F(CalibrationFiles, MinMaxPrefersGeneratedCacheAndPreservesProvidedInput)
{
  Int8MinMaxCalibrator calibrator(stream, output, 1.0, true, provided);
  check_calibration(calibrator);
}

TEST_F(CalibrationFiles, LegacySeparatesCalibrationAndHistogramInputsFromOutputs)
{
  const auto histogram = directory / "cache/output.histogram";
  Int8LegacyCalibrator calibrator(
    stream, output, histogram, 1.0, true, 0.999999, 0.999999, provided, provided);
  check_calibration(calibrator);
  std::size_t size = 0;
  const auto bytes = calibrator.readHistogramCache(size);
  ASSERT_NE(bytes, nullptr);
  EXPECT_EQ(std::string(static_cast<const char *>(bytes), size), read(provided));
  const std::string generated = "generated histogram bytes";
  calibrator.writeHistogramCache(generated.data(), generated.size());
  EXPECT_EQ(read(histogram), generated);
  EXPECT_EQ(read(provided), "supplied calibration bytes");
  const auto cached = calibrator.readHistogramCache(size);
  ASSERT_NE(cached, nullptr);
  EXPECT_EQ(std::string(static_cast<const char *>(cached), size), generated);
}

TEST_F(CalibrationFiles, MissingCachesAndDisabledReadsReturnNoData)
{
  Int8MinMaxCalibrator missing(stream, output, 1.0, true, directory / "missing.table");
  std::size_t size = 99;
  EXPECT_EQ(missing.readCalibrationCache(size), nullptr);
  EXPECT_EQ(size, 0U);
  Int8EntropyCalibrator disabled(stream, output, 1.0, false, provided);
  size = 99;
  EXPECT_EQ(disabled.readCalibrationCache(size), nullptr);
  EXPECT_EQ(size, 0U);
}
}  // namespace
