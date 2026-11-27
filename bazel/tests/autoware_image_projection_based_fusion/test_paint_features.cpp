// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/agnocast_wrapper/runtime.hpp>
#include <autoware/agnocast_wrapper/tf2.hpp>
#include <autoware/image_projection_based_fusion/pointpainting_fusion/voxel_generator.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

TEST(PointPaintingFeatures, DetectionClassesDoNotSetPaintChannelCount)
{
  namespace centerpoint = autoware::lidar_centerpoint;
  centerpoint::CenterPointConfig config(
    5, 7, 2, 4, {-1., -1., -1., 1., 1., 1.}, {1., 1., 2.}, 1, 12, {10.F},
    std::vector<float>(5, .1F), .3F, std::vector<double>(5, .1), false);
  autoware::image_projection_based_fusion::VoxelGenerator generator(
    centerpoint::DensificationParam("base_link", 0), config);
  autoware::agnocast_wrapper::Buffer buffer(std::make_shared<rclcpp::Clock>(RCL_ROS_TIME));
  sensor_msgs::msg::PointCloud2 input;
  input.header.frame_id = "base_link";
  input.header.stamp.sec = 42;
  input.height = 1;
  input.width = 2;
  input.point_step = 20;
  input.row_step = 40;
  for (const auto & name : {"x", "y", "z", "intensity", "CLASS"}) {
    sensor_msgs::msg::PointField field;
    field.name = name;
    field.offset = input.fields.size() * sizeof(float);
    field.datatype = sensor_msgs::msg::PointField::FLOAT32;
    field.count = 1;
    input.fields.push_back(field);
  }
  const std::array<float, 10> points{.25F, .5F, 0.F, 50.F, 1.F, .75F, .5F, 0.F, 60.F, 6.F};
  input.data.resize(sizeof(points));
  std::memcpy(input.data.data(), points.data(), sizeof(points));
  ASSERT_TRUE(generator.enqueuePointCloud(input, buffer));
  std::vector<float> features(14, -7.F);
  std::size_t count = 0;
  ASSERT_NO_THROW(count = generator.generateSweepPoints(features));
  ASSERT_EQ(count, 2U);
  EXPECT_EQ(
    features,
    (std::vector<float>{.25F, .5F, 0.F, 0.F, 1.F, 0.F, 0.F, .75F, .5F, 0.F, 0.F, 0.F, 1.F, 1.F}));
}

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  autoware::agnocast_wrapper::init(argc, argv, false);
  const int result = RUN_ALL_TESTS();
  autoware::agnocast_wrapper::shutdown();
  return result;
}
