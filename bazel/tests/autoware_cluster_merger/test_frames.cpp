// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <cluster_merger_node.hpp>
#include <tf2_ros/static_transform_broadcaster.hpp>

#include <sensor_msgs/point_cloud2_iterator.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <future>
#include <thread>

using tier4_perception_msgs::msg::DetectedObjectsWithFeature;
using tier4_perception_msgs::msg::DetectedObjectWithFeature;

class ClusterMergerFrames : public testing::TestWithParam<bool>
{
};

TEST_P(ClusterMergerFrames, PreservesOutputFrameAndFeaturePayload)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides({{"output_frame_id", "base_link"}});
  options.arguments(
    {"--ros-args", "-r", "input/cluster0:=/merge_test/first", "-r",
     "input/cluster1:=/merge_test/second", "-r", "output/clusters:=/merge_test/output"});
  auto merger = std::make_shared<autoware::cluster_merger::ClusterMergerNode>(options);
  auto peer = std::make_shared<rclcpp::Node>("cluster_merger_test_peer");
  tf2_ros::StaticTransformBroadcaster broadcaster(*peer);
  geometry_msgs::msg::TransformStamped transform;
  transform.header.frame_id = "base_link";
  transform.child_frame_id = "cluster_sensor";
  transform.transform.translation.x = 10.0;
  transform.transform.translation.y = 20.0;
  transform.transform.rotation.z = std::sqrt(0.5);
  transform.transform.rotation.w = std::sqrt(0.5);
  broadcaster.sendTransform(transform);

  auto first_pub = peer->create_publisher<DetectedObjectsWithFeature>("/merge_test/first", 1);
  auto second_pub = peer->create_publisher<DetectedObjectsWithFeature>("/merge_test/second", 1);
  std::promise<DetectedObjectsWithFeature> received;
  auto result = received.get_future();
  bool delivered = false;
  auto subscription = peer->create_subscription<DetectedObjectsWithFeature>(
    "/merge_test/output", 10, [&](DetectedObjectsWithFeature::ConstSharedPtr message) {
      if (!delivered) {
        delivered = true;
        received.set_value(*message);
      }
    });

  DetectedObjectsWithFeature first;
  first.header.frame_id = GetParam() ? "cluster_sensor" : "base_link";
  first.header.stamp.sec = 42;
  first.header.stamp.nanosec = 123;
  DetectedObjectWithFeature feature;
  feature.object.existence_probability = 0.75f;
  feature.object.classification.resize(1);
  feature.object.classification.front().label = 1;
  feature.object.kinematics.pose_with_covariance.pose.position.x = 1.0;
  feature.object.kinematics.pose_with_covariance.pose.position.y = 2.0;
  feature.object.kinematics.pose_with_covariance.pose.orientation.w = 1.0;
  auto & cluster = feature.feature.cluster;
  cluster.header = first.header;
  sensor_msgs::PointCloud2Modifier modifier(cluster);
  modifier.setPointCloud2Fields(
    4, "x", 1, sensor_msgs::msg::PointField::FLOAT32, "y", 1, sensor_msgs::msg::PointField::FLOAT32,
    "z", 1, sensor_msgs::msg::PointField::FLOAT32, "intensity", 1,
    sensor_msgs::msg::PointField::FLOAT32);
  modifier.resize(1);
  *sensor_msgs::PointCloud2Iterator<float>(cluster, "x") = 1.0f;
  *sensor_msgs::PointCloud2Iterator<float>(cluster, "y") = 2.0f;
  *sensor_msgs::PointCloud2Iterator<float>(cluster, "z") = 0.5f;
  *sensor_msgs::PointCloud2Iterator<float>(cluster, "intensity") = 17.0f;
  first.feature_objects.push_back(feature);
  DetectedObjectsWithFeature second = first;
  second.header.frame_id = "base_link";
  second.feature_objects.front().feature.cluster.header = second.header;
  second.feature_objects.front().object.classification.front().label = 2;

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(merger);
  executor.add_node(peer);
  std::jthread spin([&] { executor.spin(); });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
         std::chrono::steady_clock::now() < deadline) {
    first_pub->publish(first);
    second_pub->publish(second);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  executor.cancel();
  spin.join();
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = result.get();
  ASSERT_EQ(output.feature_objects.size(), 2U);
  EXPECT_EQ(output.header.frame_id, "base_link");
  EXPECT_EQ(output.header.stamp, first.header.stamp);
  const auto & transformed = output.feature_objects.front();
  EXPECT_NEAR(
    transformed.object.kinematics.pose_with_covariance.pose.position.x, GetParam() ? 8.0 : 1.0,
    1e-6);
  EXPECT_NEAR(
    transformed.object.kinematics.pose_with_covariance.pose.position.y, GetParam() ? 21.0 : 2.0,
    1e-6);
  EXPECT_EQ(transformed.feature.cluster.header.frame_id, "base_link");
  EXPECT_FLOAT_EQ(transformed.object.existence_probability, 0.75f);
  EXPECT_EQ(transformed.object.classification.front().label, 1U);
  EXPECT_EQ(output.feature_objects.back().object.classification.front().label, 2U);
  auto cloud = transformed.feature.cluster;
  EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2Iterator<float>(cloud, "x"), GetParam() ? 8.0f : 1.0f);
  EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2Iterator<float>(cloud, "y"), GetParam() ? 21.0f : 2.0f);
  EXPECT_FLOAT_EQ(*sensor_msgs::PointCloud2Iterator<float>(cloud, "intensity"), 17.0f);
}

INSTANTIATE_TEST_SUITE_P(InputFrames, ClusterMergerFrames, testing::Bool());

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const int result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
