// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/tracking_object_merger/decorative_tracker_merger_node.hpp>
#include <autoware/tracking_object_merger/utils/tracker_state.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;
namespace merger = autoware::tracking_object_merger;
using Message = autoware_perception_msgs::msg::TrackedObjects;
using Object = autoware_perception_msgs::msg::TrackedObject;
Object object(uint8_t id, double x, double velocity, double covariance)
{
  Object obj;
  obj.object_id.uuid[0] = id;
  obj.existence_probability = .9;
  obj.classification.resize(1);
  obj.classification[0].label = 1;
  obj.classification[0].probability = 1;
  obj.shape.type = 0;
  obj.shape.dimensions.x = 4;
  obj.shape.dimensions.y = 2;
  obj.shape.dimensions.z = 1;
  obj.kinematics.pose_with_covariance.pose.position.x = x;
  obj.kinematics.pose_with_covariance.pose.orientation.w = 1;
  obj.kinematics.pose_with_covariance.covariance[0] = covariance;
  obj.kinematics.pose_with_covariance.covariance[7] = covariance;
  obj.kinematics.twist_with_covariance.twist.linear.x = velocity;
  obj.kinematics.twist_with_covariance.covariance[0] = covariance;
  return obj;
}
TEST(TrackerState, PreservesIdentityAndSelectsSensorFields)
{
  const auto lidar = object(11, 10, 3, 2);
  const auto radar = object(22, 11, 9, 5);
  merger::TrackerState state(merger::MEASUREMENT_STATE::LIDAR, rclcpp::Time(123, 0), lidar);
  ASSERT_TRUE(state.updateState(merger::MEASUREMENT_STATE::RADAR, rclcpp::Time(123, 0), radar));
  const auto fused = state.getObject();
  EXPECT_EQ(fused.object_id, lidar.object_id);
  EXPECT_EQ(fused.kinematics.pose_with_covariance, lidar.kinematics.pose_with_covariance);
  auto expected_twist = lidar.kinematics.twist_with_covariance;
  expected_twist.twist.linear.x = 9.0;
  EXPECT_EQ(fused.kinematics.twist_with_covariance, expected_twist);
  const auto weighted = merger::merger_utils::objectKinematicsVXMerger(
    lidar, radar, merger::merger_utils::MergePolicy::FUSION);
  EXPECT_NEAR(weighted.twist_with_covariance.twist.linear.x, 33.0 / 7.0, 1e-9);
  EXPECT_NEAR(weighted.twist_with_covariance.covariance[0], 10.0 / 7.0, 1e-9);
  EXPECT_TRUE(state.hasUUID(merger::MEASUREMENT_STATE::RADAR, radar.object_id));
  ASSERT_TRUE(state.predict(rclcpp::Time(123, 500000000)));
  EXPECT_NEAR(state.getObject().kinematics.pose_with_covariance.pose.position.x, 14.5, 1e-9);
  EXPECT_FALSE(state.updateState(merger::MEASUREMENT_STATE::RADAR, rclcpp::Time(122, 0), radar));
  EXPECT_FALSE(state.isValid(rclcpp::Time(126, 0)));
  for (int i = 0; i < 8; ++i) state.updateWithoutSensor(rclcpp::Time(123, 500000000));
  EXPECT_FALSE(state.canPublish());
  EXPECT_FALSE(state.isValid(rclcpp::Time(123, 500000000)));
}
TEST(TrackingMergerTransport, MergesRadarVelocityAndKeepsLidarIdentity)
{
  rclcpp::NodeOptions options;
  options.arguments(
    {"--ros-args", "--params-file", std::getenv("TRACKING_PARAMS"), "--params-file",
     std::getenv("TRACKING_MATRIX")});
  options.parameter_overrides({{"merge_frame_id", "base_link"}});
  auto node = std::make_shared<merger::DecorativeTrackerMergerNode>(options);
  auto peer = std::make_shared<wrapper::Node>("tracking_merger_peer");
  auto pub0 = peer->create_publisher<Message>("/input/main_object", 1);
  auto pub1 = peer->create_publisher<Message>("/input/sub_object", 1);
  std::promise<Message> seed_received, fused_received;
  auto seed = seed_received.get_future();
  auto fused = fused_received.get_future();
  bool seeded = false, delivered = false;
  auto sub = peer->create_subscription<Message>(
    "/output/object", rclcpp::QoS(1), [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Message) & msg) {
      if (!seeded && msg->objects.size() == 1U && msg->objects[0].object_id.uuid[0] == 11) {
        seeded = true;
        seed_received.set_value(*msg);
      }
      if (
        !delivered && msg->objects.size() == 2U && msg->objects[0].object_id.uuid[0] == 11 &&
        msg->objects[0].kinematics.twist_with_covariance.twist.linear.x == 9) {
        delivered = true;
        fused_received.set_value(*msg);
      }
    });
  Message a;
  a.header.frame_id = "base_link";
  a.header.stamp.sec = 123;
  a.header.stamp.nanosec = 456;
  a.objects = {object(11, 10, 3, 2)};
  Message b;
  b.header = a.header;
  b.objects = {object(22, 10.1, 9, 5), object(33, 50, 4, 3)};
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (fused.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      if (seed.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        pub1->publish(b);
      pub0->publish(a);
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
  ASSERT_EQ(seed.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  ASSERT_EQ(fused.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = fused.get();
  EXPECT_EQ(output.header, a.header);
  ASSERT_EQ(output.objects.size(), 2U);
  const auto & obj = output.objects[0];
  EXPECT_EQ(obj.object_id, a.objects[0].object_id);
  EXPECT_EQ(obj.classification, a.objects[0].classification);
  EXPECT_EQ(obj.shape, a.objects[0].shape);
  EXPECT_EQ(obj.kinematics.pose_with_covariance, a.objects[0].kinematics.pose_with_covariance);
  auto expected_twist = a.objects[0].kinematics.twist_with_covariance;
  expected_twist.twist.linear.x = 9.0;
  EXPECT_EQ(obj.kinematics.twist_with_covariance, expected_twist);
  EXPECT_EQ(output.objects[1].object_id, b.objects[1].object_id);
  EXPECT_EQ(output.objects[1].kinematics, b.objects[1].kinematics);
}
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
