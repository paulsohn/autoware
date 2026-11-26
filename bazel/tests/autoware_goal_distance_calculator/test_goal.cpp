// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/goal_distance_calculator/goal_distance_calculator_node.hpp>
#include <tf2_ros/transform_broadcaster.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <thread>
namespace goal = autoware::goal_distance_calculator;
TEST(GoalDistance, RotatedReferenceFrame)
{
  goal::Input input;
  auto pose = std::make_shared<geometry_msgs::msg::PoseStamped>();
  auto route = std::make_shared<autoware_planning_msgs::msg::LaneletRoute>();
  route->goal_pose.position.x = 10;
  route->goal_pose.position.y = 20;
  route->goal_pose.orientation.z = std::sqrt(.5);
  route->goal_pose.orientation.w = std::sqrt(.5);
  pose->pose.position.x = 12;
  pose->pose.position.y = 23;
  pose->pose.orientation.w = 1;
  input.current_pose = pose;
  input.route = route;
  auto result = goal::GoalDistanceCalculator::update(input).goal_deviation;
  EXPECT_NEAR(result.lateral, -2., 1e-6);
  EXPECT_NEAR(result.longitudinal, 3., 1e-6);
  EXPECT_NEAR(result.yaw, -M_PI_2, 1e-6);
}
class GoalNode : public ::testing::Test
{
protected:
  void SetUp() override { rclcpp::init(0, nullptr); }
  void TearDown() override { rclcpp::shutdown(); }
  void check(bool stale)
  {
    auto peer = std::make_shared<rclcpp::Node>("goal_peer");
    tf2_ros::TransformBroadcaster tf(peer);
    std::jthread broadcast([&](std::stop_token stop) {
      while (!stop.stop_requested()) {
        geometry_msgs::msg::TransformStamped t;
        t.header.frame_id = "map";
        t.child_frame_id = "base_link";
        t.header.stamp = peer->now() - rclcpp::Duration::from_seconds(stale ? 3. : 0.);
        t.transform.rotation.w = 1;
        t.transform.translation.x = 3;
        t.transform.translation.y = 2;
        tf.sendTransform(t);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
      }
    });
    rclcpp::NodeOptions o;
    o.parameter_overrides(
      {rclcpp::Parameter("update_rate", 20.), rclcpp::Parameter("oneshot", false)});
    auto node = std::make_shared<goal::GoalDistanceCalculatorNode>(o);
    auto route = peer->create_publisher<autoware_planning_msgs::msg::LaneletRoute>(
      "/planning/mission_planning/route", rclcpp::QoS(1).transient_local());
    using Value = autoware_internal_debug_msgs::msg::Float64Stamped;
    std::optional<double> lateral, longitudinal, yaw;
    auto a = peer->create_subscription<Value>(
      "/goal_distance_calculator/deviation/lateral", 10,
      [&](Value::ConstSharedPtr m) { lateral = m->data; });
    auto b = peer->create_subscription<Value>(
      "/goal_distance_calculator/deviation/longitudinal", 10,
      [&](Value::ConstSharedPtr m) { longitudinal = m->data; });
    auto c = peer->create_subscription<Value>(
      "/goal_distance_calculator/deviation/yaw", 10,
      [&](Value::ConstSharedPtr m) { yaw = m->data; });
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(peer);
    executor.add_node(node);
    autoware_planning_msgs::msg::LaneletRoute r;
    r.goal_pose.orientation.w = 1;
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(stale ? 500 : 3000);
    while (std::chrono::steady_clock::now() < end && !(lateral && longitudinal && yaw)) {
      route->publish(r);
      executor.spin_some();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (stale) {
      EXPECT_FALSE(lateral || longitudinal || yaw);
    } else {
      ASSERT_TRUE(lateral && longitudinal && yaw);
      EXPECT_NEAR(*lateral, 2., 1e-6);
      EXPECT_NEAR(*longitudinal, 3., 1e-6);
      EXPECT_NEAR(*yaw, 0., 1e-6);
    }
    broadcast.request_stop();
    broadcast.join();
    executor.remove_node(node);
    executor.remove_node(peer);
  }
};
TEST_F(GoalNode, PublishesLiveRouteDeviation)
{
  check(false);
}
TEST_F(GoalNode, RejectsStalePose)
{
  check(true);
}
