// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/point_types/types.hpp>
#include <autoware/pointcloud_preprocessor/downsample_filter/faster_voxel_grid_downsample_filter.hpp>

#include <sensor_msgs/point_cloud2_iterator.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <memory>

TEST(FastVoxelFilter, RejectsNonfiniteCoordinatesBeforeComputingCentroids)
{
  using autoware::point_types::PointXYZIRC;
  using autoware::point_types::PointXYZIRCGenerator;
  auto input = std::make_shared<sensor_msgs::msg::PointCloud2>();
  point_cloud_msg_wrapper::PointCloud2Modifier<PointXYZIRC, PointXYZIRCGenerator> points(
    *input, "base_link");
  points.resize(5);
  points[0] = PointXYZIRC{0.25f, 0.25f, 0.25f, 10, 1, 1};
  points[1] = PointXYZIRC{0.75f, 0.25f, 0.25f, 30, 1, 1};
  points[2] = PointXYZIRC{std::numeric_limits<float>::quiet_NaN(), 0, 0, 0, 0, 0};
  points[3] = PointXYZIRC{0, std::numeric_limits<float>::infinity(), 0, 0, 0, 0};
  points[4] = PointXYZIRC{0, 0, std::numeric_limits<float>::quiet_NaN(), 0, 0, 0};
  input->is_dense = false;
  autoware::pointcloud_preprocessor::FasterVoxelGridDownsampleFilter filter;
  filter.set_voxel_size(1, 1, 1);
  sensor_msgs::msg::PointCloud2 output;
  filter.filter(input, output, {}, rclcpp::get_logger("fast_voxel_regression"));
  ASSERT_EQ(output.width * output.height, 1u);
  EXPECT_TRUE(output.is_dense);
  EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2ConstIterator<float>(output, "x"), 0.5f);
  EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2ConstIterator<float>(output, "y"), 0.25f);
  EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2ConstIterator<float>(output, "z"), 0.25f);
  EXPECT_EQ(*sensor_msgs::PointCloud2ConstIterator<uint8_t>(output, "intensity"), 20);
}

TEST(FastVoxelFilter, TransformsCentroidWithoutUsingIntensityAsHomogeneousCoordinate)
{
  using autoware::point_types::PointXYZIRC;
  using autoware::point_types::PointXYZIRCGenerator;
  auto input = std::make_shared<sensor_msgs::msg::PointCloud2>();
  point_cloud_msg_wrapper::PointCloud2Modifier<PointXYZIRC, PointXYZIRCGenerator> points(
    *input, "lidar");
  points.resize(2);
  points[0] = PointXYZIRC{0.25f, 0.25f, 0.25f, 10, 1, 1};
  points[1] = PointXYZIRC{0.75f, 0.25f, 0.25f, 30, 1, 1};
  autoware::pointcloud_preprocessor::FasterVoxelGridDownsampleFilter filter;
  filter.set_voxel_size(1, 1, 1);
  autoware::pointcloud_preprocessor::TransformInfo transform;
  transform.need_transform = true;
  transform.eigen_transform(0, 0) = 0;
  transform.eigen_transform(0, 1) = -1;
  transform.eigen_transform(1, 0) = 1;
  transform.eigen_transform(1, 1) = 0;
  transform.eigen_transform(0, 3) = 10;
  transform.eigen_transform(1, 3) = 20;
  transform.eigen_transform(2, 3) = 30;
  sensor_msgs::msg::PointCloud2 output;
  filter.filter(input, output, transform, rclcpp::get_logger("fast_voxel_regression"));
  ASSERT_EQ(output.width * output.height, 1u);
  EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2ConstIterator<float>(output, "x"), 9.75f);
  EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2ConstIterator<float>(output, "y"), 20.5f);
  EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2ConstIterator<float>(output, "z"), 30.25f);
  EXPECT_EQ(*sensor_msgs::PointCloud2ConstIterator<uint8_t>(output, "intensity"), 20);
}
