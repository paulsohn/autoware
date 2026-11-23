// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "obstacle_pointcloud/obstacle_pointcloud_validator.hpp"
#include "occupancy_grid_map/occupancy_grid_map_validator.hpp"

#include <gtest/gtest.h>
#include <pcl_conversions/pcl_conversions.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <thread>
using Objects = autoware_perception_msgs::msg::DetectedObjects;
class ObjectValidation : public testing::TestWithParam<int>
{
};
TEST_P(ObjectValidation, ValidatesNonemptyObjectsAndPreservesRetainedPayload)
{
  const int mode = GetParam();
  rclcpp::NodeOptions options;
  options.arguments(
    {"--ros-args", "--params-file",
     std::getenv(mode == 0 ? "OCCUPANCY_PARAMS" : "OBSTACLE_PARAMS")});
  options.append_parameter_override("using_2d_validator", mode == 1);
  std::shared_ptr<rclcpp::Node> node;
  if (mode == 0)
    node = std::make_shared<
      autoware::detected_object_validation::occupancy_grid_map::OccupancyGridBasedValidator>(
      options);
  else
    node = std::make_shared<
      autoware::detected_object_validation::obstacle_pointcloud::ObstaclePointCloudBasedValidator>(
      options);
  auto peer = std::make_shared<rclcpp::Node>("validation_peer");
  const auto base = std::string("/") + node->get_name();
  auto objects = peer->create_publisher<Objects>(base + "/input/detected_objects", 1);
  auto grid_pub =
    peer->create_publisher<nav_msgs::msg::OccupancyGrid>(base + "/input/occupancy_grid_map", 1);
  auto cloud_pub = peer->create_publisher<sensor_msgs::msg::PointCloud2>(
    base + "/input/obstacle_pointcloud", rclcpp::SensorDataQoS());
  std::promise<Objects> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->create_subscription<Objects>(
    base + "/output/objects", 1, [&](Objects::ConstSharedPtr msg) {
      if (!delivered) {
        delivered = true;
        received.set_value(*msg);
      }
    });
  Objects input;
  input.header.frame_id = "map";
  input.header.stamp.sec = 123;
  input.header.stamp.nanosec = 456;
  input.objects.resize(3);
  for (size_t i = 0; i < 3; ++i) {
    auto & obj = input.objects[i];
    obj.classification.resize(1);
    obj.classification[0].label = 1;
    obj.classification[0].probability = .9;
    obj.existence_probability = .8;
    obj.kinematics.pose_with_covariance.pose.orientation.w = 1;
    obj.kinematics.pose_with_covariance.covariance[0] = 2 + i;
    obj.kinematics.pose_with_covariance.pose.position.x = i ? -3 : 3;
    obj.kinematics.pose_with_covariance.pose.position.z = 1;
    obj.shape.type = 0;
    obj.shape.dimensions.x = 2;
    obj.shape.dimensions.y = 2;
    obj.shape.dimensions.z = 2;
  }
  if (mode == 0)
    input.objects[2].classification[0].label = 7;
  else
    input.objects[2].kinematics.pose_with_covariance.pose.position.x = 80;
  nav_msgs::msg::OccupancyGrid grid;
  grid.header = input.header;
  grid.info.resolution = 1;
  grid.info.width = 20;
  grid.info.height = 20;
  grid.info.origin.position.x = -10;
  grid.info.origin.position.y = -10;
  grid.info.origin.orientation.w = 1;
  grid.data.resize(400);
  for (size_t y = 0; y < 20; ++y)
    for (size_t x = 0; x < 20; ++x) grid.data[y * 20 + x] = x >= 10 ? 100 : 0;
  pcl::PointCloud<pcl::PointXYZ> points;
  for (int i = 0; i < 24; ++i) points.push_back(pcl::PointXYZ(2.7F + .02F * i, .02F * (i % 3), 1));
  sensor_msgs::msg::PointCloud2 cloud;
  pcl::toROSMsg(points, cloud);
  cloud.header = input.header;
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(peer);
  std::jthread spin([&] { executor.spin(); });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
         std::chrono::steady_clock::now() < deadline) {
    objects->publish(input);
    if (mode == 0)
      grid_pub->publish(grid);
    else
      cloud_pub->publish(cloud);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  executor.cancel();
  spin.join();
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = result.get();
  EXPECT_EQ(output.header, input.header);
  ASSERT_EQ(output.objects.size(), 2U);
  EXPECT_EQ(output.objects[0], input.objects[0]);
  EXPECT_EQ(output.objects[1], input.objects[2]);
}
INSTANTIATE_TEST_SUITE_P(OccupancyAndPointcloud, ObjectValidation, testing::Range(0, 3));
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
