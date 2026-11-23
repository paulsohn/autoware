// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/predicted_path_postprocessor/node.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <thread>
#include <tuple>
using Message = autoware_perception_msgs::msg::PredictedObjects;
class PostprocessorTransport : public testing::TestWithParam<std::tuple<std::string, bool>>
{
};
TEST_P(PostprocessorTransport, ComposesProcessorsAndPreservesObjectMetadata)
{
  rclcpp::NodeOptions options;
  options.arguments({"--ros-args", "--params-file", std::getenv("POSTPROCESSOR_PARAMS")});
  options.parameter_overrides(
    {{"debug", std::get<1>(GetParam())},
     {"refine_by_speed.interpolation", std::get<0>(GetParam())},
     {"refine_penetration_by_static_objects.interpolation", std::get<0>(GetParam())}});
  auto node =
    std::make_shared<autoware::predicted_path_postprocessor::PredictedPathPostprocessorNode>(
      options);
  auto peer = std::make_shared<rclcpp::Node>("postprocessor_peer");
  const auto base = std::string("/") + node->get_name();
  auto pub = peer->create_publisher<Message>(base + "/input/objects", 1);
  std::promise<Message> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->create_subscription<Message>(
    base + "/output/objects", 1, [&](Message::ConstSharedPtr msg) {
      if (!delivered) {
        delivered = true;
        received.set_value(*msg);
      }
    });
  Message input;
  input.header.frame_id = "map";
  input.header.stamp.sec = 123;
  input.header.stamp.nanosec = 456;
  input.objects.resize(2);
  for (size_t i = 0; i < 2; ++i) {
    auto & obj = input.objects[i];
    obj.object_id.uuid[0] = i + 1;
    obj.existence_probability = .9;
    obj.classification.resize(1);
    obj.classification[0].label = 1;
    obj.classification[0].probability = 1;
    obj.shape.type = 0;
    obj.shape.dimensions.x = 1;
    obj.shape.dimensions.y = 1;
    obj.shape.dimensions.z = 1;
    obj.kinematics.initial_pose_with_covariance.pose.position.x = i * 50;
    obj.kinematics.initial_pose_with_covariance.pose.orientation.w = 1;
    obj.kinematics.initial_pose_with_covariance.covariance[0] = 2.0;
    obj.kinematics.initial_twist_with_covariance.twist.linear.x = i ? 10 : .5;
    autoware_perception_msgs::msg::PredictedPath path;
    path.time_step = rclcpp::Duration::from_seconds(1);
    path.confidence = .8;
    for (int k = 0; k < 6; ++k) {
      geometry_msgs::msg::Pose pose;
      pose.position.x = i * 50 + 2 * k;
      pose.orientation.w = 1;
      path.path.push_back(pose);
    }
    obj.kinematics.predicted_paths.push_back(path);
  }
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(peer);
  std::jthread spin([&] { executor.spin(); });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
         std::chrono::steady_clock::now() < deadline) {
    pub->publish(input);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  executor.cancel();
  spin.join();
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = result.get();
  EXPECT_EQ(output.header, input.header);
  ASSERT_EQ(output.objects.size(), 2U);
  EXPECT_EQ(output.objects[1], input.objects[1]);
  auto expected = input.objects[0];
  ASSERT_EQ(output.objects[0].kinematics.predicted_paths.size(), 1U);
  const auto & path = output.objects[0].kinematics.predicted_paths.front();
  ASSERT_EQ(path.path.size(), 6U);
  EXPECT_EQ(path.time_step, expected.kinematics.predicted_paths.front().time_step);
  EXPECT_FLOAT_EQ(path.confidence, .8);
  for (size_t k = 0; k < path.path.size(); ++k) {
    EXPECT_NEAR(path.path[k].position.x, k * .5, 1e-8);
    EXPECT_NEAR(path.path[k].position.y, 0, 1e-8);
    EXPECT_NEAR(path.path[k].orientation.w, 1, 1e-8);
  }
  expected.kinematics.predicted_paths = output.objects[0].kinematics.predicted_paths;
  EXPECT_EQ(output.objects[0], expected);
}
INSTANTIATE_TEST_SUITE_P(
  InterpolationAndReports, PostprocessorTransport,
  testing::Combine(testing::Values("linear", "spline", "spline_by_akima"), testing::Bool()));
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
