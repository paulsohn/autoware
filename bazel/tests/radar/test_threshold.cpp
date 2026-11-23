// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "ros_fixture.hpp"

#include <radar_threshold_filter_node.hpp>

#include <radar_msgs/msg/radar_scan.hpp>

TEST_F(RadarRosTest, FiltersReturnsAndAppliesUpdatedAmplitudeLimit)
{
  using radar_msgs::msg::RadarScan;
  rclcpp::NodeOptions options;
  options.parameter_overrides(
    {{"node_params.is_amplitude_filter", true},
     {"node_params.amplitude_min", -10.0},
     {"node_params.amplitude_max", 100.0},
     {"node_params.is_range_filter", true},
     {"node_params.range_min", 1.0},
     {"node_params.range_max", 200.0},
     {"node_params.is_azimuth_filter", true},
     {"node_params.azimuth_min", -1.0},
     {"node_params.azimuth_max", 1.0},
     {"node_params.is_z_filter", true},
     {"node_params.z_min", -1.0},
     {"node_params.z_max", 1.0},
     {"node_params.max_queue_size", 5}});
  auto filter =
    attach(std::make_shared<autoware::radar_threshold_filter::RadarThresholdFilterNode>(options));
  auto peer = attach(std::make_shared<rclcpp::Node>("radar_threshold_peer"));
  RadarScan::ConstSharedPtr output;
  auto sub = peer->create_subscription<RadarScan>(
    "/radar_threshold_filter/output/radar", rclcpp::SensorDataQoS(),
    [&](RadarScan::ConstSharedPtr msg) { output = msg; });
  auto pub = peer->create_publisher<RadarScan>(
    "/radar_threshold_filter/input/radar", rclcpp::SensorDataQoS());
  RadarScan input;
  input.header.frame_id = "radar";
  input.header.stamp = peer->now();
  radar_msgs::msg::RadarReturn good, high, boundary, near;
  good.range = 10;
  good.amplitude = 0;
  high = good;
  high.amplitude = 500;
  boundary = good;
  boundary.amplitude = 100;
  near = good;
  near.range = 0.5;
  input.returns = {good, high, boundary, near};
  ASSERT_TRUE(await([&] { return output != nullptr; }, [&] { pub->publish(input); }));
  EXPECT_EQ(output->header, input.header);
  ASSERT_EQ(output->returns.size(), 1U);
  EXPECT_EQ(output->returns[0].amplitude, 0);
  ASSERT_TRUE(
    filter->set_parameter(rclcpp::Parameter("node_params.amplitude_max", 1000.0)).successful);
  input.header.frame_id = "updated";
  ASSERT_TRUE(
    await([&] { return output->header.frame_id == "updated"; }, [&] { pub->publish(input); }));
  EXPECT_EQ(output->returns.size(), 3U);
}
