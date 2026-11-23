// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "lanelet_filter/detected_object_lanelet_filter.hpp"
#include "lanelet_filter/tracked_object_lanelet_filter.hpp"
#include "test_lanelet_utils.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;
template <class Message, class Node>
void check_lanelet(const char * params)
{
  rclcpp::NodeOptions options;
  options.arguments({"--ros-args", "--params-file", std::getenv(params)});
  options.append_parameter_override("filter_target_label.CAR", true);
  auto node = std::make_shared<Node>(options);
  auto peer = std::make_shared<wrapper::Node>("lanelet_validation_peer");
  auto map = peer->create_publisher<autoware_map_msgs::msg::LaneletMapBin>(
    "/input/vector_map", rclcpp::QoS(1).transient_local());
  auto pub = peer->template create_publisher<Message>("/input/object", 1);
  std::promise<Message> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->template create_subscription<Message>(
    "/output/object", rclcpp::QoS(1), [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Message) & msg) {
      if (!delivered && msg->objects.size() == 1) {
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
    obj.classification.resize(1);
    obj.classification[0].label = 1;
    obj.classification[0].probability = .9;
    obj.kinematics.pose_with_covariance.pose.position.x = 10;
    obj.kinematics.pose_with_covariance.pose.position.y = i ? 10 : 0;
    obj.kinematics.pose_with_covariance.pose.orientation.w = 1;
    obj.kinematics.pose_with_covariance.covariance[0] = 2 + i;
    obj.shape.type = 0;
    obj.shape.dimensions.x = 1;
    obj.shape.dimensions.y = 1;
    obj.shape.dimensions.z = 1;
    if constexpr (requires { obj.object_id; }) obj.object_id.uuid[0] = i + 1;
  }
  const auto fixture = createSimpleLaneletMapMsg();
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      map->publish(fixture);
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
  EXPECT_EQ(output.objects[0], input.objects[0]);
}
TEST(LaneletValidationTransport, Detected)
{
  check_lanelet<
    autoware_perception_msgs::msg::DetectedObjects,
    autoware::detected_object_validation::lanelet_filter::DetectedObjectLaneletFilterNode>(
    "DETECTED_LANELET_PARAMS");
}
TEST(LaneletValidationTransport, Tracked)
{
  check_lanelet<
    autoware_perception_msgs::msg::TrackedObjects,
    autoware::detected_object_validation::lanelet_filter::TrackedObjectLaneletFilterNode>(
    "TRACKED_LANELET_PARAMS");
}
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
