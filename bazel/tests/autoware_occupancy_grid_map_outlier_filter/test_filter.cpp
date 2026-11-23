// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <occupancy_grid_map_outlier_filter_node.hpp>
#include <tf2_ros/static_transform_broadcaster.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <future>
#include <set>
#include <thread>

class OccupancyOutlierTransport : public testing::TestWithParam<bool>
{
};

TEST_P(OccupancyOutlierTransport, SynchronizesMapAndPreservesSelectedPoints)
{
  using Cloud = sensor_msgs::msg::PointCloud2;
  using Grid = nav_msgs::msg::OccupancyGrid;
  rclcpp::NodeOptions options;
  options.arguments(
    {"--ros-args", "--params-file", std::getenv("FILTER_PARAMS"), "-r",
     "~/input/pointcloud:=/outlier_test/cloud", "-r",
     "~/input/occupancy_grid_map:=/outlier_test/map", "-r",
     "~/output/pointcloud:=/outlier_test/output"});
  options.parameter_overrides(
    {{"use_radius_search_2d_filter", GetParam()},
     {"radius_search_2d_filter.search_radius", 0.5},
     {"radius_search_2d_filter.min_points_and_distance_ratio", 1.0},
     {"radius_search_2d_filter.min_points", 2},
     {"radius_search_2d_filter.max_points", 2}});
  auto filter = std::make_shared<
    autoware::occupancy_grid_map_outlier_filter::OccupancyGridMapOutlierFilterComponent>(options);
  auto peer = std::make_shared<rclcpp::Node>("occupancy_outlier_test_peer");
  tf2_ros::StaticTransformBroadcaster broadcaster(*peer);
  geometry_msgs::msg::TransformStamped transform;
  transform.header.frame_id = "map";
  transform.child_frame_id = "base_link";
  transform.transform.translation.x = 1.0;
  transform.transform.translation.y = -1.0;
  transform.transform.rotation.w = 1.0;
  broadcaster.sendTransform(transform);
  auto cloud_pub = peer->create_publisher<Cloud>("/outlier_test/cloud", rclcpp::SensorDataQoS());
  auto map_pub = peer->create_publisher<Grid>("/outlier_test/map", rclcpp::QoS(1));
  std::promise<Cloud> received;
  auto result = received.get_future();
  bool delivered = false;
  auto subscription = peer->create_subscription<Cloud>(
    "/outlier_test/output", 10, [&](Cloud::ConstSharedPtr message) {
      if (!delivered) {
        delivered = true;
        received.set_value(*message);
      }
    });
  Cloud input;
  input.header.frame_id = "base_link";
  input.header.stamp.sec = 42;
  input.header.stamp.nanosec = 123;
  sensor_msgs::PointCloud2Modifier modifier(input);
  modifier.setPointCloud2Fields(
    4, "x", 1, sensor_msgs::msg::PointField::FLOAT32, "y", 1, sensor_msgs::msg::PointField::FLOAT32,
    "z", 1, sensor_msgs::msg::PointField::FLOAT32, "intensity", 1,
    sensor_msgs::msg::PointField::FLOAT32);
  modifier.resize(6);
  const std::array<std::array<float, 2>, 6> positions{
    {{2.0f, 2.0f}, {4.0f, 4.0f}, {4.1f, 4.0f}, {8.0f, 8.0f}, {25.0f, 25.0f}, {-2.0f, 2.0f}}};
  sensor_msgs::PointCloud2Iterator<float> x(input, "x"), y(input, "y"), z(input, "z"),
    intensity(input, "intensity");
  for (size_t i = 0; i < positions.size(); ++i, ++x, ++y, ++z, ++intensity) {
    *x = positions[i][0];
    *y = positions[i][1];
    *z = 0.1f;
    *intensity = static_cast<float>(i + 1);
  }
  input.is_dense = true;
  Grid map;
  map.header = input.header;
  map.header.frame_id = "map";
  map.header.stamp.sec = 43;
  map.info.resolution = 1.0f;
  map.info.width = 10;
  map.info.height = 10;
  map.info.origin.orientation.w = 1.0;
  map.data.assign(100, 0);
  map.data[1 * 10 + 3] = 100;

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(filter);
  executor.add_node(peer);
  std::jthread spin([&] { executor.spin(); });
  const auto discovery_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while ((cloud_pub->get_subscription_count() == 0 || map_pub->get_subscription_count() == 0) &&
         std::chrono::steady_clock::now() < discovery_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_GT(cloud_pub->get_subscription_count(), 0U);
  EXPECT_GT(map_pub->get_subscription_count(), 0U);
  // Mismatched stamps must not trigger ExactTime delivery.
  for (int i = 0; i < 5; ++i) {
    cloud_pub->publish(input);
    map_pub->publish(map);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  EXPECT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
  map.header.stamp = input.header.stamp;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
         std::chrono::steady_clock::now() < deadline) {
    cloud_pub->publish(input);
    map_pub->publish(map);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  executor.cancel();
  spin.join();
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  auto output = result.get();
  EXPECT_EQ(output.header, input.header);
  EXPECT_EQ(output.fields, input.fields);
  EXPECT_EQ(output.point_step, input.point_step);
  std::set<int> retained;
  sensor_msgs::PointCloud2Iterator<float> ox(output, "x"), oy(output, "y"), oz(output, "z"),
    oi(output, "intensity");
  for (; oi != oi.end(); ++ox, ++oy, ++oz, ++oi) {
    const int id = static_cast<int>(*oi);
    ASSERT_GE(id, 1);
    ASSERT_LE(id, 6);
    retained.insert(id);
    EXPECT_NEAR(*ox, positions[id - 1][0], 1e-5);
    EXPECT_NEAR(*oy, positions[id - 1][1], 1e-5);
    EXPECT_FLOAT_EQ(*oz, 0.1f);
  }
  EXPECT_EQ(output.width * output.height, GetParam() ? 5U : 3U);
  EXPECT_EQ(retained, GetParam() ? (std::set<int>{1, 2, 3, 5, 6}) : (std::set<int>{1, 5, 6}));
}

INSTANTIATE_TEST_SUITE_P(RadiusModes, OccupancyOutlierTransport, testing::Bool());

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const int result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
