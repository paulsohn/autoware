// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "ros_fixture.hpp"

#include <radar_static_pointcloud_filter_node.hpp>
#include <tf2_ros/static_transform_broadcaster.hpp>

#include <cmath>

class RadarStaticRosTest : public RadarRosTest
{
protected:
  void start(double yaw)
  {
    rclcpp::NodeOptions options;
    options.parameter_overrides({{"doppler_velocity_sd", 0.5}, {"max_queue_size", 5}});
    filter = attach(
      std::make_shared<autoware::radar_static_pointcloud_filter::RadarStaticPointcloudFilterNode>(
        options));
    peer = attach(std::make_shared<rclcpp::Node>("radar_static_peer"));
    broadcaster = std::make_unique<tf2_ros::StaticTransformBroadcaster>(peer);
    geometry_msgs::msg::TransformStamped transform;
    transform.header.frame_id = "base";
    transform.child_frame_id = "radar";
    transform.header.stamp = peer->now();
    transform.transform.rotation.z = std::sin(yaw / 2);
    transform.transform.rotation.w = std::cos(yaw / 2);
    broadcaster->sendTransform(transform);
    static_sub = peer->create_subscription<radar_msgs::msg::RadarScan>(
      "/radar_static_pointcloud_filter/output/static_radar_scan", rclcpp::SensorDataQoS(),
      [&](radar_msgs::msg::RadarScan::ConstSharedPtr msg) { stationary = msg; });
    dynamic_sub = peer->create_subscription<radar_msgs::msg::RadarScan>(
      "/radar_static_pointcloud_filter/output/dynamic_radar_scan", rclcpp::SensorDataQoS(),
      [&](radar_msgs::msg::RadarScan::ConstSharedPtr msg) { moving = msg; });
    radar_pub = peer->create_publisher<radar_msgs::msg::RadarScan>(
      "/radar_static_pointcloud_filter/input/radar", rclcpp::SensorDataQoS());
    odom_pub = peer->create_publisher<nav_msgs::msg::Odometry>(
      "/radar_static_pointcloud_filter/input/odometry", rclcpp::SensorDataQoS());
    input.header.frame_id = "radar";
    odom.header.frame_id = "base";
    odom.twist.twist.linear.x = 5;
  }
  void publish()
  {
    input.header.stamp = peer->now();
    odom.header.stamp = input.header.stamp;
    radar_pub->publish(input);
    odom_pub->publish(odom);
  }
  std::shared_ptr<autoware::radar_static_pointcloud_filter::RadarStaticPointcloudFilterNode> filter;
  std::shared_ptr<rclcpp::Node> peer;
  std::unique_ptr<tf2_ros::StaticTransformBroadcaster> broadcaster;
  rclcpp::Subscription<radar_msgs::msg::RadarScan>::SharedPtr static_sub, dynamic_sub;
  rclcpp::Publisher<radar_msgs::msg::RadarScan>::SharedPtr radar_pub;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub;
  radar_msgs::msg::RadarScan input;
  nav_msgs::msg::Odometry odom;
  radar_msgs::msg::RadarScan::ConstSharedPtr stationary, moving;
};
TEST_F(RadarStaticRosTest, PartitionsReturnsUsingEgoVelocityAndPreservesHeader)
{
  start(0);
  radar_msgs::msg::RadarReturn fixed, mobile;
  fixed.range = 10;
  fixed.doppler_velocity = -5;
  mobile = fixed;
  mobile.doppler_velocity = 0;
  input.returns = {fixed, mobile};
  ASSERT_TRUE(await(
    [&] { return stationary && moving && stationary->header.stamp == moving->header.stamp; },
    [&] { publish(); }));
  ASSERT_EQ(stationary->returns.size(), 1U);
  ASSERT_EQ(moving->returns.size(), 1U);
  EXPECT_FLOAT_EQ(stationary->returns[0].doppler_velocity, -5);
  EXPECT_FLOAT_EQ(moving->returns[0].doppler_velocity, 0);
  EXPECT_EQ(stationary->header, moving->header);
  EXPECT_EQ(stationary->header.frame_id, "radar");
}
TEST_F(RadarStaticRosTest, RotatesSensorVelocityBeforeEgoCompensation)
{
  start(std::acos(-1.0) / 2);
  radar_msgs::msg::RadarReturn fixed;
  fixed.range = 10;
  fixed.azimuth = std::acos(-1.0) / 2;
  fixed.doppler_velocity = 5;
  input.returns = {fixed};
  ASSERT_TRUE(await(
    [&] { return stationary && moving && stationary->header.stamp == moving->header.stamp; },
    [&] { publish(); }));
  EXPECT_EQ(stationary->returns.size(), 1U);
  EXPECT_TRUE(moving->returns.empty());
}
TEST_F(RadarStaticRosTest, UpdatedVelocityToleranceChangesPartition)
{
  start(0);
  radar_msgs::msg::RadarReturn point;
  point.range = 10;
  point.doppler_velocity = -4;
  input.returns = {point};
  ASSERT_TRUE(await([&] { return moving && moving->returns.size() == 1; }, [&] { publish(); }));
  ASSERT_TRUE(filter->set_parameter(rclcpp::Parameter("doppler_velocity_sd", 2.0)).successful);
  ASSERT_TRUE(
    await([&] { return stationary && stationary->returns.size() == 1; }, [&] { publish(); }));
}
