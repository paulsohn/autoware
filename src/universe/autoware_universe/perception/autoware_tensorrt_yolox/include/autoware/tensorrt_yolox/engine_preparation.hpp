// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#ifndef AUTOWARE__TENSORRT_YOLOX__ENGINE_PREPARATION_HPP_
#define AUTOWARE__TENSORRT_YOLOX__ENGINE_PREPARATION_HPP_

#include <autoware/tensorrt_common/tensorrt_conv_calib.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace autoware::tensorrt_yolox
{
// Shared model setup; prepared-only mode never creates calibration/build state.
std::unique_ptr<autoware::tensorrt_common::TrtConvCalib> createYoloXEngine(
  const autoware::tensorrt_common::TrtCommonConfig & config,
  const autoware::tensorrt_common::CalibrationConfig & calibration,
  std::string calibration_images = "", double norm_factor = 1.0,
  int64_t builder_optimization_level = 3);
}  // namespace autoware::tensorrt_yolox
#endif
