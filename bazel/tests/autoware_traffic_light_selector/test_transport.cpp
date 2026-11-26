// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "traffic_light_selector_node.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>
using tier4_perception_msgs::msg::DetectedObjectsWithFeature;
using tier4_perception_msgs::msg::TrafficLightRoiArray;
TEST(SelectorTransport, FourSynchronizedStreamsRetainSelectedRegionsAndIdentity)
{
  auto node =
    std::make_shared<autoware::traffic_light::TrafficLightSelectorNode>(rclcpp::NodeOptions{});
  auto peer = std::make_shared<rclcpp::Node>("roi_selector_peer");
  auto detected = peer->create_publisher<DetectedObjectsWithFeature>("input/detected_rois", 1);
  auto rough = peer->create_publisher<TrafficLightRoiArray>("input/rough_rois", 1);
  auto expected = peer->create_publisher<TrafficLightRoiArray>("input/expect_rois", 1);
  auto camera = peer->create_publisher<sensor_msgs::msg::CameraInfo>(
    "input/camera_info", rclcpp::SensorDataQoS());
  std::promise<TrafficLightRoiArray> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->create_subscription<TrafficLightRoiArray>(
    "output/traffic_rois", rclcpp::QoS(1), [&](TrafficLightRoiArray::ConstSharedPtr msg) {
      if (!delivered) {
        delivered = true;
        received.set_value(*msg);
      }
    });
  DetectedObjectsWithFeature detections;
  detections.header.frame_id = "camera";
  detections.header.stamp.sec = 123;
  detections.header.stamp.nanosec = 456;
  TrafficLightRoiArray roughs, expectations;
  roughs.header = expectations.header = detections.header;
  for (int i = 0; i < 2; ++i) {
    tier4_perception_msgs::msg::DetectedObjectWithFeature feature;
    feature.feature.roi.x_offset = 110 + i * 200;
    feature.feature.roi.y_offset = 105;
    feature.feature.roi.width = feature.feature.roi.height = 40;
    detections.feature_objects.push_back(feature);
    tier4_perception_msgs::msg::TrafficLightRoi roi;
    roi.traffic_light_id = 123 + i;
    roi.traffic_light_type =
      i ? decltype(roi)::PEDESTRIAN_TRAFFIC_LIGHT : decltype(roi)::CAR_TRAFFIC_LIGHT;
    roi.roi.x_offset = 100 + i * 200;
    roi.roi.y_offset = 100;
    roi.roi.width = roi.roi.height = 40;
    expectations.rois.push_back(roi);
    roi.roi.x_offset = 50 + i * 200;
    roi.roi.y_offset = 50;
    roi.roi.width = roi.roi.height = 200;
    roughs.rois.push_back(roi);
  }
  sensor_msgs::msg::CameraInfo info;
  info.header = detections.header;
  info.width = 1280;
  info.height = 720;
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(peer);
  {
    std::jthread spin([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      detected->publish(detections);
      rough->publish(roughs);
      expected->publish(expectations);
      camera->publish(info);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    executor.cancel();
  }
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = result.get();
  EXPECT_EQ(output.header, detections.header);
  ASSERT_EQ(output.rois.size(), 2U);
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(output.rois[i].traffic_light_id, expectations.rois[i].traffic_light_id);
    EXPECT_EQ(output.rois[i].traffic_light_type, expectations.rois[i].traffic_light_type);
    EXPECT_EQ(output.rois[i].roi, detections.feature_objects[i].feature.roi);
  }
}
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
