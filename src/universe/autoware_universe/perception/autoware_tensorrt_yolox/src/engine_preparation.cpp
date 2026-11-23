// Copyright 2023 TIER IV, Inc.
// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/tensorrt_yolox/calibrator.hpp>
#include <autoware/tensorrt_yolox/engine_preparation.hpp>
#include <autoware/tensorrt_yolox/label.hpp>

#include <stdexcept>
#include <utility>

namespace autoware::tensorrt_yolox
{
using namespace autoware::tensorrt_common;
std::unique_ptr<TrtConvCalib> createYoloXEngine(
  const TrtCommonConfig & trt_config, const CalibrationConfig & calib_config,
  std::string calibration_image_list_path, const double norm_factor_,
  const int64_t builder_optimization_level)
{
  if (builder_optimization_level < 0 || builder_optimization_level > 5) {
    throw std::invalid_argument("builder_optimization_level must be between 0 and 5");
  }
  auto trt_common_ = std::make_unique<TrtConvCalib>(trt_config);

  const auto network_input_dims = trt_common_->getTensorShape(0);
  if (
    network_input_dims.nbDims != 4 || network_input_dims.d[0] <= 0 ||
    network_input_dims.d[1] != 3 || network_input_dims.d[2] <= 0 || network_input_dims.d[3] <= 0) {
    throw std::invalid_argument("YOLOX requires a static positive NCHW input with three channels");
  }
  const auto batch_size_ = network_input_dims.d[0];
  const auto input_channel = network_input_dims.d[1];
  const auto input_height = network_input_dims.d[2];
  const auto input_width = network_input_dims.d[3];

  auto profile_dims_ptr = std::make_unique<std::vector<autoware::tensorrt_common::ProfileDims>>();

  std::vector<autoware::tensorrt_common::ProfileDims> profile_dims{
    autoware::tensorrt_common::ProfileDims(
      0, {4, {batch_size_, input_channel, input_height, input_width}},
      {4, {batch_size_, input_channel, input_height, input_width}},
      {4, {batch_size_, input_channel, input_height, input_width}})};
  *profile_dims_ptr = profile_dims;

  if (!trt_config.allow_engine_build) {
    if (!trt_common_->setup(std::move(profile_dims_ptr))) {
      throw std::runtime_error("Prepared YOLOX engine profile mismatch");
    }
    return trt_common_;
  }
  trt_common_->getBuilderConfig()->setBuilderOptimizationLevel(
    static_cast<int32_t>(builder_optimization_level));

  if (trt_config.precision == "int8") {
    // Supplied tables stay with the model; generated tables follow the engine cache.
    fs::path calibration_table{trt_config.engine_path};
    std::string ext = "";
    if (calib_config.calib_type_str == "Entropy") {
      ext = "EntropyV2-";
    } else if (
      calib_config.calib_type_str == "Legacy" || calib_config.calib_type_str == "Percentile") {
      ext = "Legacy-";
    } else {
      ext = "MinMax-";
    }

    ext += "calibration.table";
    calibration_table.replace_extension(ext);
    fs::path calibration_input{trt_config.onnx_path};
    calibration_input.replace_extension(ext);
    fs::path histogram_table{trt_config.engine_path};
    ext = "histogram.table";
    histogram_table.replace_extension(ext);
    fs::path histogram_input{trt_config.onnx_path};
    histogram_input.replace_extension(ext);

    if (calib_config.clip_value <= 0.0) {
      if (
        calibration_image_list_path.empty() && !fs::exists(calibration_table) &&
        !fs::exists(calibration_input) && !fs::exists(trt_config.engine_path)) {
        throw std::runtime_error(
          "INT8 requires calibration images, a supplied/cached calibration table, "
          "an existing engine, or a positive clip_value.");
      }
    } else {
      // if clip value is larger than zero, calibration file is not needed
      calibration_image_list_path = "";
    }

    std::vector<std::string> calibration_images;
    if (calibration_image_list_path != "") {
      calibration_images = load_image_list(calibration_image_list_path);
    }
    tensorrt_yolox::ImageStream stream(batch_size_, network_input_dims, calibration_images);
    std::unique_ptr<nvinfer1::IInt8Calibrator> calibrator;
    if (calib_config.calib_type_str == "Entropy") {
      calibrator.reset(new tensorrt_yolox::Int8EntropyCalibrator(
        stream, calibration_table, norm_factor_, true, calibration_input));

    } else if (
      calib_config.calib_type_str == "Legacy" || calib_config.calib_type_str == "Percentile") {
      const double quantile = 0.999999;
      const double cutoff = 0.999999;
      calibrator.reset(new tensorrt_yolox::Int8LegacyCalibrator(
        stream, calibration_table, histogram_table, norm_factor_, true, quantile, cutoff,
        calibration_input, histogram_input));
    } else {
      calibrator.reset(new tensorrt_yolox::Int8MinMaxCalibrator(
        stream, calibration_table, norm_factor_, true, calibration_input));
    }
    if (!trt_common_->setupWithCalibrator(
          std::move(calibrator), calib_config, std::move((profile_dims_ptr)))) {
      throw std::runtime_error("Failed to setup TensorRT engine with calibrator");
    }
  } else {
    if (!trt_common_->setup(std::move(profile_dims_ptr))) {
      throw std::runtime_error("Failed to setup TensorRT engine");
    }
  }

  return trt_common_;
}
}  // namespace autoware::tensorrt_yolox
