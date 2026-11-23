// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "ros_fixture.hpp"

#include <radar_scan_to_pointcloud2_node.hpp>

#include <radar_msgs/msg/radar_scan.hpp>
#include <sensor_msgs/point_cloud2_iterator.hpp>

#include <cmath>

class RadarScanRosTest : public RadarRosTest
{
protected:
  void start(bool amplitude)
  {
    rclcpp::NodeOptions options;
    options.parameter_overrides(
      {{"publish_amplitude_pointcloud", amplitude},
       {"publish_doppler_pointcloud", true},
       {"max_queue_size", 5}});
    converter = attach(
      std::make_shared<autoware::radar_scan_to_pointcloud2::RadarScanToPointcloud2Node>(options));
    peer = attach(std::make_shared<rclcpp::Node>("radar_scan_peer"));
    amplitude_sub = peer->create_subscription<sensor_msgs::msg::PointCloud2>(
      "/radar_scan_to_pointcloud2/output/amplitude_pointcloud", rclcpp::SensorDataQoS(),
      [&](sensor_msgs::msg::PointCloud2::ConstSharedPtr msg) { amp = msg; });
    doppler_sub = peer->create_subscription<sensor_msgs::msg::PointCloud2>(
      "/radar_scan_to_pointcloud2/output/doppler_pointcloud", rclcpp::SensorDataQoS(),
      [&](sensor_msgs::msg::PointCloud2::ConstSharedPtr msg) { doppler = msg; });
    pub = peer->create_publisher<radar_msgs::msg::RadarScan>(
      "/radar_scan_to_pointcloud2/input/radar", rclcpp::SensorDataQoS());
    input.header.frame_id = "radar";
    input.header.stamp = peer->now();
    radar_msgs::msg::RadarReturn point;
    point.range = 10;
    point.amplitude = 42.5;
    point.doppler_velocity = -3.25;
    input.returns = {point};
  }
  std::shared_ptr<autoware::radar_scan_to_pointcloud2::RadarScanToPointcloud2Node> converter;
  std::shared_ptr<rclcpp::Node> peer;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr amplitude_sub, doppler_sub;
  rclcpp::Publisher<radar_msgs::msg::RadarScan>::SharedPtr pub;
  sensor_msgs::msg::PointCloud2::ConstSharedPtr amp, doppler;
  radar_msgs::msg::RadarScan input;
};
TEST_F(RadarScanRosTest, ConvertsGeometryBothIntensitiesAndHeader)
{
  start(true);
  auto angled = input.returns.front();
  angled.azimuth = std::atan2(3.0F, 4.0F);
  angled.elevation = std::asin(0.6F);
  input.returns.push_back(angled);
  ASSERT_TRUE(await([&] { return amp && doppler; }, [&] { pub->publish(input); }));
  for (const auto & cloud : {amp, doppler}) {
    EXPECT_EQ(cloud->header, input.header);
    ASSERT_EQ(cloud->width, 2U);
    EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2ConstIterator<float>(*cloud, "x"), 10.0F);
    EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2ConstIterator<float>(*cloud, "y"), 0.0F);
    EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2ConstIterator<float>(*cloud, "z"), 0.0F);
    sensor_msgs::PointCloud2ConstIterator<float> x(*cloud, "x"), y(*cloud, "y"), z(*cloud, "z");
    EXPECT_NEAR(*++x, 6.4F, 1.0e-4F);
    EXPECT_NEAR(*++y, 4.8F, 1.0e-4F);
    EXPECT_NEAR(*++z, 6.0F, 1.0e-4F);
  }
  EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2ConstIterator<float>(*amp, "intensity"), 42.5F);
  EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2ConstIterator<float>(*doppler, "intensity"), -3.25F);
}
TEST_F(RadarScanRosTest, PublicationParameterCanEnableAmplitude)
{
  start(false);
  ASSERT_TRUE(await([&] { return doppler != nullptr; }, [&] { pub->publish(input); }));
  EXPECT_FALSE(await(
    [&] { return amp != nullptr; }, [&] { pub->publish(input); }, std::chrono::milliseconds(250)));
  ASSERT_TRUE(
    converter->set_parameter(rclcpp::Parameter("publish_amplitude_pointcloud", true)).successful);
  ASSERT_TRUE(await([&] { return amp != nullptr; }, [&] { pub->publish(input); }));
}
