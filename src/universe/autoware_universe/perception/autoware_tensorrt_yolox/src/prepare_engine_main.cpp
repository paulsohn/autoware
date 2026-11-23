// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <ament_index_cpp/get_package_share_path.hpp>
#include <autoware/tensorrt_yolox/engine_preparation.hpp>

#include <cuda_runtime_api.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

int main(int argc, char ** argv)
{
  try {
    std::map<std::string, std::string> options{
      {"--model", ""},
      {"--model-package", ""},
      {"--engine", ""},
      {"--precision", "fp16"},
      {"--calibration-algorithm", "Entropy"},
      {"--calibration-images", ""},
      {"--clip-value", "6"},
      {"--builder-optimization-level", "3"},
      {"--gpu-id", "0"}};
    for (int i = 1; i < argc; ++i) {
      const std::string key = argv[i];
      if (key == "--help") {
        std::cout << "prepare_engine --model FILE --engine FILE [--model-package PACKAGE]\n"
                     "  [--precision fp32|fp16|int8] [--calibration-algorithm "
                     "Entropy|MinMax|Legacy|Percentile]\n"
                     "  [--calibration-images LIST] [--clip-value NUMBER]\n"
                     "  [--builder-optimization-level 0..5] [--gpu-id ID]\n";
        return 0;
      }
      if (!options.count(key) || i + 1 == argc) {
        throw std::invalid_argument("Unknown option or missing value: " + key);
      }
      options[key] = argv[++i];
    }
    if (options["--model"].empty() || options["--engine"].empty()) {
      throw std::invalid_argument("--model and --engine are required; use --help");
    }
    auto integer = [&](const std::string & key) {
      std::size_t used = 0;
      const auto value = std::stoll(options.at(key), &used);
      if (used != options.at(key).size()) throw std::invalid_argument("Invalid integer: " + key);
      return value;
    };
    std::size_t used = 0;
    const auto clip = std::stod(options["--clip-value"], &used);
    if (used != options["--clip-value"].size() || !std::isfinite(clip))
      throw std::invalid_argument("Invalid clip value");
    const auto level = integer("--builder-optimization-level");
    if (level < 0 || level > 5)
      throw std::invalid_argument("Builder optimization level must be 0..5");
    const auto gpu = integer("--gpu-id");
    if (gpu < 0 || gpu > std::numeric_limits<int>::max())
      throw std::invalid_argument("Invalid GPU ID");
    if (cudaSetDevice(static_cast<int>(gpu)) != cudaSuccess)
      throw std::runtime_error("GPU unavailable");
    std::filesystem::path model = options["--model"];
    if (!options["--model-package"].empty()) {
      if (model.is_absolute())
        throw std::invalid_argument("Indexed model filenames must be relative");
      model = ament_index_cpp::get_package_share_path(options["--model-package"]) / model;
    }
    const autoware::tensorrt_common::TrtCommonConfig config(
      model.string(), options["--precision"], options["--engine"]);
    const autoware::tensorrt_common::CalibrationConfig calibration(
      options["--calibration-algorithm"], false, false, clip);
    const auto engine = autoware::tensorrt_yolox::createYoloXEngine(
      config, calibration, options["--calibration-images"], 1.0, level);
    std::cout << "Prepared engine: " << options["--engine"] << '\n';
    return 0;
  } catch (const std::exception & error) {
    std::cerr << "Engine preparation failed: " << error.what() << '\n';
    return 1;
  }
}
