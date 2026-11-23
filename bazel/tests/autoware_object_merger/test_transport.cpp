// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/object_merger/object_association_merger_node.hpp>
#include <autoware/object_merger/object_fusion_merger_node.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;
using Message = autoware_perception_msgs::msg::DetectedObjects;
using Object = autoware_perception_msgs::msg::DetectedObject;
Object object(double x, double probability, double covariance)
{
  Object obj;
  obj.existence_probability = probability;
  obj.classification.resize(1);
  obj.classification[0].label = 1;
  obj.classification[0].probability = 1.0;
  obj.shape.type = 0;
  obj.shape.dimensions.x = 4;
  obj.shape.dimensions.y = 2;
  obj.shape.dimensions.z = 1;
  obj.kinematics.pose_with_covariance.pose.position.x = x;
  obj.kinematics.pose_with_covariance.pose.orientation.w = 1;
  obj.kinematics.has_position_covariance = true;
  obj.kinematics.orientation_availability = 2;
  obj.kinematics.pose_with_covariance.covariance[0] = covariance;
  obj.kinematics.pose_with_covariance.covariance[7] = covariance;
  obj.kinematics.pose_with_covariance.covariance[14] = covariance;
  obj.kinematics.has_twist = true;
  obj.kinematics.has_twist_covariance = true;
  obj.kinematics.twist_with_covariance.twist.linear.x = 2;
  obj.kinematics.twist_with_covariance.covariance[0] = covariance;
  return obj;
}
class ObjectMerge : public testing::TestWithParam<int>
{
};
TEST_P(ObjectMerge, AssociatesAndPreservesPayload)
{
  const bool fusion = GetParam() == 4;
  rclcpp::NodeOptions options;
  if (fusion) {
    options.arguments({"--ros-args", "--params-file", std::getenv("FUSION_PARAMS")});
  } else {
    options.arguments(
      {"--ros-args", "--params-file", std::getenv("ASSOCIATION_PARAMS"), "--params-file",
       std::getenv("MATRIX_PARAMS"), "--params-file", std::getenv("OVERLAP_PARAMS")});
    options.parameter_overrides(
      {{"priority_mode", GetParam()}, {"remove_overlapped_unknown_objects", false}});
  }
  std::shared_ptr<wrapper::Node> node;
  if (fusion)
    node = std::make_shared<autoware::object_merger::ObjectFusionMergerNode>(options);
  else
    node = std::make_shared<autoware::object_merger::ObjectAssociationMergerNode>(options);
  auto peer = std::make_shared<wrapper::Node>("object_merge_peer");
  auto pub0 = peer->create_publisher<Message>(fusion ? "/input/main_objects" : "/input/object0", 1);
  auto pub1 = peer->create_publisher<Message>(fusion ? "/input/sub_objects" : "/input/object1", 1);
  std::promise<Message> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->create_subscription<Message>(
    fusion ? "/output/objects" : "/output/object", rclcpp::QoS(1),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Message) & msg) {
      if (!delivered && msg->objects.size() == (fusion ? 2U : 3U)) {
        delivered = true;
        received.set_value(*msg);
      }
    });
  std::promise<Message> unmatched_received;
  auto unmatched_result = unmatched_received.get_future();
  bool unmatched_delivered = false;
  auto unmatched_sub = peer->create_subscription<Message>(
    "/output/other_objects", rclcpp::QoS(1),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Message) & msg) {
      if (!unmatched_delivered && msg->objects.size() == 1U) {
        unmatched_delivered = true;
        unmatched_received.set_value(*msg);
      }
    });
  Message a;
  a.header.frame_id = "base_link";
  a.header.stamp.sec = 123;
  a.header.stamp.nanosec = 456;
  a.objects = {object(0, .6, 4), object(30, .7, 2)};
  Message b;
  b.header = a.header;
  b.objects = {object(1.5, .9, 1), object(50, .8, 3)};
  b.objects[0].shape.dimensions.x = 2.0;
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while ((result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready ||
            (fusion && unmatched_result.wait_for(std::chrono::milliseconds(0)) !=
                         std::future_status::ready)) &&
           std::chrono::steady_clock::now() < deadline) {
      pub0->publish(a);
      pub1->publish(b);
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
  EXPECT_EQ(output.header, a.header);
  if (fusion) {
    ASSERT_EQ(output.objects.size(), 2U);
    EXPECT_NEAR(output.objects[0].kinematics.pose_with_covariance.pose.position.x, .25, 1e-3);
    EXPECT_NEAR(output.objects[0].shape.dimensions.x, 4.5, 1e-3);
    EXPECT_FLOAT_EQ(output.objects[0].existence_probability, .6);
    EXPECT_EQ(output.objects[0].classification, a.objects[0].classification);
    EXPECT_EQ(output.objects[1], a.objects[1]);
    ASSERT_EQ(unmatched_result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
    const auto unmatched = unmatched_result.get();
    EXPECT_EQ(unmatched.header, b.header);
    EXPECT_EQ(unmatched.objects[0], b.objects[1]);
  } else {
    ASSERT_EQ(output.objects.size(), 3U);
    EXPECT_EQ(
      output.objects[0], (GetParam() == 1 || GetParam() == 2) ? b.objects[0] : a.objects[0]);
    EXPECT_EQ(output.objects[1], a.objects[1]);
    EXPECT_EQ(output.objects[2], b.objects[1]);
  }
}
INSTANTIATE_TEST_SUITE_P(PriorityAndFusion, ObjectMerge, testing::Values(0, 1, 2, 3, 4));
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
