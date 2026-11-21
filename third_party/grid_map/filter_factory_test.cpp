// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <filters/filter_base.hpp>
#include <grid_map_core/GridMap.hpp>
#include <pluginlib/class_loader.hpp>

#include <gtest/gtest.h>

TEST(GridMapFilter, LoadsIndexedOriginalFactory)
{
  pluginlib::ClassLoader<filters::FilterBase<grid_map::GridMap>> loader(
    "filters", "filters::FilterBase<grid_map::GridMap>");
  const auto filter = loader.createSharedInstance("gridMapCv/InpaintFilter");
  ASSERT_TRUE(filter);
}
