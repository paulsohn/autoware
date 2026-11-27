// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/lidar_frnet/ros_utils.hpp>
#include <autoware/lidar_frnet/utils.hpp>

#include <gtest/gtest.h>

namespace fr = autoware::lidar_frnet;

TEST(FRNetTypes, RoundTripsSupportedLayoutsAndConversionPolicy)
{
  for (const auto format :
       {fr::CloudFormat::XYZI, fr::CloudFormat::XYZIRC, fr::CloudFormat::XYZIRADRT,
        fr::CloudFormat::XYZIRCAEDT}) {
    const auto layout = fr::ros_utils::generateFilteredPointCloudLayout(format);
    EXPECT_EQ(fr::ros_utils::detectCloudFormat(layout.fields), format);
    EXPECT_EQ(fr::parse_cloud_format_string(fr::to_string(format)), format);
    EXPECT_TRUE(fr::can_convert_format(format, fr::CloudFormat::XYZI));
    EXPECT_TRUE(fr::can_convert_format(format, format));
    EXPECT_GT(layout.point_step, 0U);
  }
  EXPECT_EQ(fr::parse_cloud_format_string("XYZIRC"), fr::CloudFormat::XYZIRC);
  EXPECT_EQ(fr::parse_cloud_format_string("invalid"), fr::CloudFormat::UNKNOWN);
  EXPECT_FALSE(fr::can_convert_format(fr::CloudFormat::XYZI, fr::CloudFormat::XYZIRC));
}

TEST(FRNetTypes, RejectsInvalidPaletteAndFilterClass)
{
  using Params = fr::utils::PostprocessingParams;
  EXPECT_THROW(Params::make_palette({"road"}, {255, 0}), std::runtime_error);
  EXPECT_THROW(Params::make_palette({"road"}, {256, 0, 0}), std::runtime_error);
  EXPECT_THROW(Params::make_filter_class_indices({"road"}, {"car"}), std::runtime_error);
  EXPECT_EQ(Params::make_filter_class_indices({"road", "car"}, {"car"}), std::vector<uint32_t>{1});
}
