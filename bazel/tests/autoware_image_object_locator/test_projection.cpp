// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "bbox_object_locator_node.hpp"

#include <tf2_ros/static_transform_broadcaster.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <thread>
using Input = tier4_perception_msgs::msg::DetectedObjectsWithFeature;
using Output = autoware_perception_msgs::msg::DetectedObjects;
class ImageLocator : public testing::TestWithParam<bool>
{
};
TEST_P(ImageLocator, ProjectsAndFiltersCameraRois)
{
  rclcpp::NodeOptions options;
  options.arguments({"--ros-args", "--params-file", std::getenv("LOCATOR_PARAMS")});
  options.parameter_overrides(
    {{"rois_ids", std::vector<int64_t>{0}},
     {"roi_truncation_validation.enabled", std::vector<bool>{true}},
     {"roi_truncation_validation.remove_truncated", std::vector<bool>{GetParam()}},
     {"roi_truncation_validation.image_border_truncation_horizontal_margin_"
      "ratio",
      0.05},
     {"roi_truncation_validation.image_border_truncation_vertical_margin_"
      "ratio",
      0.05}});
  auto node = std::make_shared<autoware::image_object_locator::BboxObjectLocatorNode>(options);
  auto peer = std::make_shared<rclcpp::Node>("image_locator_peer");
  tf2_ros::StaticTransformBroadcaster broadcaster(peer);
  geometry_msgs::msg::TransformStamped tf;
  tf.header.frame_id = "base_link";
  tf.child_frame_id = "locator_optical";
  tf.transform.translation.x = 1.0;
  tf.transform.translation.y = 2.0;
  tf.transform.translation.z = 2.0;
  tf.transform.rotation.x = -0.5;
  tf.transform.rotation.y = 0.5;
  tf.transform.rotation.z = -0.5;
  tf.transform.rotation.w = 0.5;
  broadcaster.sendTransform(tf);
  auto camera = peer->create_publisher<sensor_msgs::msg::CameraInfo>(
    "/sensing/camera/camera0/camera_info", rclcpp::SensorDataQoS());
  auto pub = peer->create_publisher<Input>(
    "/perception/object_recognition/detection/rois0", rclcpp::SensorDataQoS());
  std::promise<Output> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->create_subscription<Output>(
    "/roi_3d_projector/rois0/objects", 1, [&](Output::ConstSharedPtr msg) {
      if (!delivered && !msg->objects.empty()) {
        delivered = true;
        received.set_value(*msg);
      }
    });
  sensor_msgs::msg::CameraInfo info;
  info.header.frame_id = tf.child_frame_id;
  info.width = 640;
  info.height = 480;
  info.k = {500, 0, 320, 0, 500, 240, 0, 0, 1};
  info.d = {0, 0, 0, 0, 0};
  Input input;
  input.header.frame_id = tf.child_frame_id;
  input.header.stamp.sec = 123;
  input.header.stamp.nanosec = 456;
  input.feature_objects.resize(5);
  for (auto & feature : input.feature_objects) {
    feature.object.existence_probability = 0.9;
    feature.object.classification.resize(1);
    feature.object.classification[0].label = 7;  // PEDESTRIAN.
    feature.object.classification[0].probability = 0.95;
    feature.feature.roi.x_offset = 400;
    feature.feature.roi.y_offset = 300;
    feature.feature.roi.width = 100;
    feature.feature.roi.height = 100;
  }
  input.feature_objects[1].object.existence_probability = 0.1;  // Confidence rejection.
  input.feature_objects[2].object.classification[0].label = 1;  // Disabled CAR.
  input.feature_objects[3].feature.roi.y_offset = 180;          // Ground range > 10 m.
  input.feature_objects[4].feature.roi.x_offset = 0;            // Border truncation.
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(peer);
  std::jthread spin([&] { executor.spin(); });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
         std::chrono::steady_clock::now() < deadline) {
    camera->publish(info);
    pub->publish(input);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  executor.cancel();
  spin.join();
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = result.get();
  EXPECT_EQ(output.header.frame_id, "base_link");
  EXPECT_EQ(output.header.stamp, input.header.stamp);
  ASSERT_EQ(output.objects.size(), GetParam() ? 1U : 2U);
  const auto & object = output.objects.front();
  EXPECT_EQ(object.classification, input.feature_objects[0].object.classification);
  EXPECT_FLOAT_EQ(object.existence_probability, 0.9);
  EXPECT_EQ(object.shape.type, autoware_perception_msgs::msg::Shape::CYLINDER);
  EXPECT_NEAR(object.shape.dimensions.x, 0.8, 1e-5);
  EXPECT_NEAR(object.shape.dimensions.z, 1.25, 1e-5);
  EXPECT_NEAR(object.kinematics.pose_with_covariance.pose.position.x, 7.875, 1e-5);
  EXPECT_NEAR(object.kinematics.pose_with_covariance.pose.position.y, 0.375, 1e-5);
  EXPECT_NEAR(object.kinematics.pose_with_covariance.pose.position.z, 0.625, 1e-5);
  const auto & covariance = object.kinematics.pose_with_covariance.covariance;
  EXPECT_TRUE(object.kinematics.has_position_covariance);
  EXPECT_GT(covariance[0], 0.0);
  EXPECT_GT(covariance[7], 0.0);
  EXPECT_NEAR(covariance[1], covariance[6], 1e-12);
  EXPECT_GT(covariance[0] * covariance[7] - covariance[1] * covariance[6], 0.0);
}
INSTANTIATE_TEST_SUITE_P(TruncationPolicy, ImageLocator, testing::Bool());
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
