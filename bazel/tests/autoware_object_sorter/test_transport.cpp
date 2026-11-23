// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "detected_object_sorter_node.hpp"
#include "tracked_object_sorter_node.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;

template <class Message, class Node>
void check_sorter(bool distance)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
    {{"range_calc_frame_id", "base_link"},
     {"range_calc_offset.x", 1.0},
     {"range_calc_offset.y", 0.0},
     {"range_thresholding_mode", distance ? "distance" : "rectangle"},
     {"sort_target.UNKNOWN.publish", true},
     {"sort_target.UNKNOWN.min_velocity_threshold", 1.0},
     {"sort_target.UNKNOWN.range_threshold.min_distance", 2.0},
     {"sort_target.UNKNOWN.range_threshold.max_distance", 10.0},
     {"sort_target.UNKNOWN.range_threshold.min_x", 2.0},
     {"sort_target.UNKNOWN.range_threshold.max_x", 10.0},
     {"sort_target.UNKNOWN.range_threshold.min_y", -1.0},
     {"sort_target.UNKNOWN.range_threshold.max_y", 1.0},
     {"sort_target.CAR.publish", false},
     {"sort_target.CAR.min_velocity_threshold", 0.0},
     {"sort_target.CAR.range_threshold.min_distance", 0.0},
     {"sort_target.CAR.range_threshold.max_distance", 100.0},
     {"sort_target.CAR.range_threshold.min_x", -100.0},
     {"sort_target.CAR.range_threshold.max_x", 100.0},
     {"sort_target.CAR.range_threshold.min_y", -100.0},
     {"sort_target.CAR.range_threshold.max_y", 100.0}});
  auto node = std::make_shared<Node>(options);
  auto peer = std::make_shared<wrapper::Node>("sorter_peer");
  wrapper::StaticTransformBroadcaster broadcaster(*peer);
  geometry_msgs::msg::TransformStamped transform;
  transform.header.frame_id = "base_link";
  transform.child_frame_id = "sorter_sensor";
  transform.transform.translation.x = 5.0;
  transform.transform.rotation.w = 1.0;
  broadcaster.sendTransform(transform);
  const auto base = std::string("/") + node->get_name();
  auto pub = peer->template create_publisher<Message>(base + "/input/objects", 1);
  std::promise<Message> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->template create_subscription<Message>(
    base + "/output/objects", rclcpp::QoS(1),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Message) & msg) {
      if (!delivered) {
        delivered = true;
        received.set_value(*msg);
      }
    });
  Message input;
  input.header.frame_id = "sorter_sensor";
  input.header.stamp.sec = 123;
  input.header.stamp.nanosec = 456;
  input.objects.resize(5);
  for (size_t i = 0; i < input.objects.size(); ++i) {
    auto & object = input.objects[i];
    object.classification.resize(1);
    object.classification[0].label = 0;
    object.classification[0].probability = 0.9;
    object.kinematics.pose_with_covariance.pose.orientation.w = 1.0;
    object.kinematics.pose_with_covariance.covariance[0] = 2.0 + i;
    object.kinematics.twist_with_covariance.twist.linear.x = 2.0;
    if constexpr (requires { object.object_id; }) {
      object.object_id.uuid[0] = i + 1;
    }
  }
  // Sensor x=-1 maps to base x=4: passes only if the actual TF arrives.
  input.objects[0].kinematics.pose_with_covariance.pose.position.x = -1.0;
  input.objects[1].kinematics.pose_with_covariance.pose.position.x = 20.0;
  input.objects[2].kinematics.twist_with_covariance.twist.linear.x = 0.5;
  input.objects[3].classification[0].label = 1;   // Disabled class.
  input.objects[4].classification[0].label = 99;  // UNKNOWN fallback, preserve ordering.
  input.objects[4].kinematics.pose_with_covariance.pose.position.x = 3.0;
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
  ASSERT_EQ(output.objects.size(), 2U);
  EXPECT_EQ(output.objects[0], input.objects[0]);
  EXPECT_EQ(output.objects[1], input.objects[4]);
}
TEST(ObjectSorterTransport, DetectedDistance)
{
  check_sorter<
    autoware_perception_msgs::msg::DetectedObjects,
    autoware::object_sorter::DetectedObjectSorterNode>(true);
}
TEST(ObjectSorterTransport, TrackedRectangle)
{
  check_sorter<
    autoware_perception_msgs::msg::TrackedObjects,
    autoware::object_sorter::TrackedObjectSorterNode>(false);
}
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
