// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>
#include <autoware/agnocast_wrapper/runtime.hpp>
#include <autoware/point_types/types.hpp>
#include <autoware/pointcloud_preprocessor/crop_box_filter/crop_box_filter_node.hpp>
#include <autoware/pointcloud_preprocessor/utility/geometry.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>

#include <pcl_msgs/msg/point_indices.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

TEST(PolygonRemoval, PreservesBoundaryAndPointsAboveHeightLimit)
{
  namespace utils = autoware::pointcloud_preprocessor::utils;
  PolygonCgal polygon{{0, 0}, {2, 0}, {2, 2}, {0, 2}};
  pcl::PointCloud<pcl::PointXYZ> input;
  input.emplace_back(1, 1, 0);
  input.emplace_back(2, 1, 0);
  input.emplace_back(3, 1, 0);
  input.emplace_back(1, 1, 2);
  pcl::PointCloud<pcl::PointXYZ> output;
  utils::remove_polygon_cgal_from_cloud(input, polygon, output, 1.0f);
  ASSERT_EQ(output.size(), 3u);
  EXPECT_FLOAT_EQ(output[0].x, 2.0f);
  EXPECT_FLOAT_EQ(output[1].x, 3.0f);
  EXPECT_FLOAT_EQ(output[2].z, 2.0f);
  geometry_msgs::msg::Polygon invalid;
  PolygonCgal converted;
  EXPECT_THROW(utils::to_cgal_polygon(invalid, converted), std::length_error);
}

class CropBoxTransport : public testing::TestWithParam<int>
{
};

TEST_P(CropBoxTransport, PreservesAcceptedPointFieldsAndTimestamp)
{
  namespace wrapper = autoware::agnocast_wrapper;
  using Cloud = sensor_msgs::msg::PointCloud2;
  using autoware::point_types::PointXYZIRC;
  using autoware::point_types::PointXYZIRCGenerator;
  rclcpp::NodeOptions options;
  options.arguments(
    {"--ros-args", "-r", "input:=/crop_test/input", "-r", "output:=/crop_test/output", "-r",
     "indices:=/crop_test/indices"});
  options.parameter_overrides(
    {{"use_indices", GetParam() != 0},
     {"approximate_sync", GetParam() == 2},
     {"input_frame", "base_link"},
     {"output_frame", "base_link"},
     {"min_x", -1.0},
     {"min_y", -1.0},
     {"min_z", -1.0},
     {"max_x", 1.0},
     {"max_y", 1.0},
     {"max_z", 1.0},
     {"negative", false},
     {"processing_time_threshold_sec", 1.0}});
  auto filter =
    std::make_shared<autoware::pointcloud_preprocessor::CropBoxFilterComponent>(options);
  auto peer = std::make_shared<wrapper::Node>("crop_test_peer");
  auto publisher = peer->create_publisher<Cloud>("/crop_test/input", rclcpp::SensorDataQoS());
  auto indices_publisher = peer->create_publisher<pcl_msgs::msg::PointIndices>(
    "/crop_test/indices", rclcpp::SensorDataQoS());
  std::promise<Cloud> received;
  auto result = received.get_future();
  bool delivered = false;
  auto subscription = peer->create_subscription<Cloud>(
    "/crop_test/output", rclcpp::SensorDataQoS(),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Cloud) & cloud) {
      if (!delivered) {
        delivered = true;
        received.set_value(*cloud);
      }
    });
  Cloud input;
  point_cloud_msg_wrapper::PointCloud2Modifier<PointXYZIRC, PointXYZIRCGenerator> modifier(
    input, "base_link");
  modifier.resize(2);
  modifier[0] = PointXYZIRC{0.5f, 0.25f, 0.0f, 42, 1, 7};
  modifier[1] = PointXYZIRC{5.0f, 0.0f, 0.0f, 99, 2, 8};
  input.header.stamp.sec = 123;
  input.header.stamp.nanosec = 456;

  pcl_msgs::msg::PointIndices indices;
  indices.header = input.header;
  indices.indices = {0};
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    if (GetParam() != 0) {
      for (int i = 0; i < 5; ++i) {
        publisher->publish(input);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      EXPECT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      publisher->publish(input);
      if (GetParam() != 0) indices_publisher->publish(indices);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    executor.cancel();
  };
  if (wrapper::use_agnocast()) {
    EXPECT_FALSE(rclcpp::ok());
    agnocast::AgnocastOnlySingleThreadedExecutor executor;
    executor.add_node(filter->get_agnocast_node());
    executor.add_node(peer->get_agnocast_node());
    exchange(executor);
  } else {
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(filter->get_rclcpp_node());
    executor.add_node(peer->get_rclcpp_node());
    exchange(executor);
  }
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = result.get();
  ASSERT_EQ(output.width * output.height, 1u);
  EXPECT_EQ(output.header, input.header);
  EXPECT_EQ(output.fields, input.fields);
  EXPECT_EQ(output.point_step, input.point_step);
  EXPECT_EQ(
    output.data, std::vector<uint8_t>(input.data.begin(), input.data.begin() + input.point_step));
}

INSTANTIATE_TEST_SUITE_P(SubscriptionPaths, CropBoxTransport, testing::Values(0, 1, 2));

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  autoware::agnocast_wrapper::init(argc, argv, true);
  const int result = RUN_ALL_TESTS();
  autoware::agnocast_wrapper::shutdown();
  return result;
}
