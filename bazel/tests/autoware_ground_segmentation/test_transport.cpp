// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>
#include <autoware/agnocast_wrapper/runtime.hpp>
#include <autoware/point_types/types.hpp>
#include <scan_ground_filter/node.hpp>

#include <sensor_msgs/point_cloud2_iterator.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <set>
#include <thread>

class GroundFilterTransport : public testing::TestWithParam<bool>
{
};

TEST_P(GroundFilterTransport, RemovesFlatGroundAndPreservesObstaclePayload)
{
  namespace wrapper = autoware::agnocast_wrapper;
  using Cloud = sensor_msgs::msg::PointCloud2;
  rclcpp::NodeOptions options;
  options.arguments(
    {"--ros-args", "--params-file", std::getenv("SCAN_PARAMS"), "-r", "input:=/ground_test/input",
     "-r", "output:=/ground_test/output"});
  options.parameter_overrides(
    {{"elevation_grid_mode", GetParam()},
     {"input_frame", "base_link"},
     {"output_frame", "base_link"},
     {"wheel_radius", 0.39},
     {"wheel_width", 0.42},
     {"wheel_base", 2.74},
     {"wheel_tread", 1.63},
     {"front_overhang", 1.0},
     {"rear_overhang", 1.03},
     {"left_overhang", 0.1},
     {"right_overhang", 0.1},
     {"vehicle_height", 2.5},
     {"max_steer_angle", 0.7}});
  auto filter = std::make_shared<autoware::ground_segmentation::ScanGroundFilterComponent>(options);
  EXPECT_EQ(filter->get_parameter("elevation_grid_mode").as_bool(), GetParam());
  auto peer = std::make_shared<wrapper::Node>("scan_filter_test_peer");
  auto publisher = peer->create_publisher<Cloud>("/ground_test/input", rclcpp::SensorDataQoS());
  std::promise<Cloud> received;
  auto result = received.get_future();
  bool delivered = false;
  auto subscription = peer->create_subscription<Cloud>(
    "/ground_test/output", rclcpp::SensorDataQoS(),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Cloud) & message) {
      if (!delivered) {
        delivered = true;
        received.set_value(*message);
      }
    });
  Cloud input;
  input.header.frame_id = "base_link";
  using autoware::point_types::PointXYZIRC;
  using autoware::point_types::PointXYZIRCGenerator;
  point_cloud_msg_wrapper::PointCloud2Modifier<PointXYZIRC, PointXYZIRCGenerator> modifier(
    input, "base_link");
  modifier.resize(34);
  input.header.stamp.sec = 42;
  input.header.stamp.nanosec = 123;
  for (int i = 0; i < 34; ++i) {
    modifier[i] = PointXYZIRC{
      i < 32 ? 2.0f + i * 0.5f : (i == 32 ? 8.1f : 10.1f),
      0.0f,
      i < 32 ? 0.0f : (i == 32 ? 1.0f : 1.5f),
      static_cast<std::uint8_t>(i < 32 ? 10 + i : 200 + i - 32),
      1,
      static_cast<std::uint16_t>(i)};
  }
  input.is_dense = true;
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      publisher->publish(input);
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
  auto output = result.get();
  EXPECT_EQ(output.header, input.header);
  EXPECT_EQ(output.fields, input.fields);
  EXPECT_EQ(output.point_step, input.point_step);
  ASSERT_EQ(output.width * output.height, 2U);
  std::set<std::uint8_t> retained;
  for (sensor_msgs::PointCloud2Iterator<std::uint8_t> value(output, "intensity");
       value != value.end(); ++value) {
    retained.insert(*value);
  }
  EXPECT_EQ(retained, (std::set<std::uint8_t>{200, 201}));
}

INSTANTIATE_TEST_SUITE_P(GridModes, GroundFilterTransport, testing::Bool());

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  autoware::agnocast_wrapper::init(argc, argv, true);
  const int result = RUN_ALL_TESTS();
  autoware::agnocast_wrapper::shutdown();
  return result;
}
