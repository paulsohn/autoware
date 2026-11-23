// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "detected_object_feature_remover_node.hpp"

#include <gtest/gtest.h>
#include <pcl_conversions/pcl_conversions.h>

#include <chrono>
#include <future>
#include <thread>

namespace wrapper = autoware::agnocast_wrapper;
using Input = tier4_perception_msgs::msg::DetectedObjectsWithFeature;
using Output = autoware_perception_msgs::msg::DetectedObjects;

class FeatureRemoval : public testing::TestWithParam<bool>
{
};

TEST_P(FeatureRemoval, PreservesMetadataAndConvertsOnlyWhenRequested)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides({{"run_convex_hull_conversion", GetParam()}});
  auto node =
    std::make_shared<autoware::detected_object_feature_remover::DetectedObjectFeatureRemover>(
      options);
  EXPECT_EQ(node->get_parameter("run_convex_hull_conversion").as_bool(), GetParam());
  auto peer = std::make_shared<wrapper::Node>("feature_removal_peer");
  auto pub = peer->create_publisher<Input>("/detected_object_feature_remover/input", 1);
  std::promise<Output> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->create_subscription<Output>(
    "/detected_object_feature_remover/output", rclcpp::QoS(1),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Output) & msg) {
      if (!delivered) {
        delivered = true;
        received.set_value(*msg);
      }
    });
  Input input;
  input.header.frame_id = "base_link";
  input.header.stamp.sec = 123;
  input.header.stamp.nanosec = 456;
  input.feature_objects.resize(1);
  auto & feature = input.feature_objects.front();
  auto & object = feature.object;
  object.existence_probability = 0.8;
  object.classification.resize(1);
  object.classification.front().label = 1;
  object.classification.front().probability = 0.9;
  object.kinematics.pose_with_covariance.covariance[0] = 2.5;
  object.kinematics.twist_with_covariance.twist.linear.x = 3.0;
  object.kinematics.pose_with_covariance.pose.orientation.w = 1.0;
  pcl::PointCloud<pcl::PointXYZ> cloud;
  for (float x : {1.0F, 3.0F}) {
    for (float y : {2.0F, 4.0F}) {
      cloud.push_back(pcl::PointXYZ(x, y, 1.0F));
      cloud.push_back(pcl::PointXYZ(x, y, 3.0F));
    }
  }
  pcl::toROSMsg(cloud, feature.feature.cluster);
  feature.feature.roi.width = 123;
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      pub->publish(input);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    executor.cancel();
  };
  if (wrapper::use_agnocast()) {
    EXPECT_FALSE(rclcpp::ok());
    agnocast::AgnocastOnlySingleThreadedExecutor executor;
    executor.add_node(node->get_agnocast_node());
    executor.add_node(peer->get_agnocast_node());
    exchange(executor);
  } else {
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node->get_rclcpp_node());
    executor.add_node(peer->get_rclcpp_node());
    exchange(executor);
  }
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = result.get();
  EXPECT_EQ(output.header, input.header);
  ASSERT_EQ(output.objects.size(), 1U);
  auto expected = object;
  if (GetParam()) {
    const auto & converted = output.objects.front();
    EXPECT_EQ(converted.shape.type, autoware_perception_msgs::msg::Shape::POLYGON);
    ASSERT_EQ(converted.shape.footprint.points.size(), 4U);
    EXPECT_FLOAT_EQ(converted.shape.dimensions.z, 2.0);
    EXPECT_FLOAT_EQ(converted.kinematics.pose_with_covariance.pose.position.x, 2.0);
    EXPECT_FLOAT_EQ(converted.kinematics.pose_with_covariance.pose.position.y, 3.0);
    EXPECT_FLOAT_EQ(converted.kinematics.pose_with_covariance.pose.position.z, 2.0);
    expected.shape = converted.shape;
    expected.kinematics.pose_with_covariance.pose = converted.kinematics.pose_with_covariance.pose;
  }
  EXPECT_EQ(output.objects.front(), expected);
}
INSTANTIATE_TEST_SUITE_P(ConversionModes, FeatureRemoval, testing::Bool());

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
