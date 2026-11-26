// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/pose_instability_detector/pose_instability_detector.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;
void check_motion(bool jump)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
    {rclcpp::Parameter("timer_period", .05), rclcpp::Parameter("heading_velocity_maximum", 16.667),
     rclcpp::Parameter("heading_velocity_scale_factor_tolerance", 3.),
     rclcpp::Parameter("angular_velocity_maximum", .523),
     rclcpp::Parameter("angular_velocity_scale_factor_tolerance", .2),
     rclcpp::Parameter("angular_velocity_bias_tolerance", .00698),
     rclcpp::Parameter("pose_estimator_longitudinal_tolerance", .11),
     rclcpp::Parameter("pose_estimator_lateral_tolerance", .11),
     rclcpp::Parameter("pose_estimator_vertical_tolerance", .5),
     rclcpp::Parameter("pose_estimator_angular_tolerance", .0175),
     rclcpp::Parameter("enable_validation.position_x", true),
     rclcpp::Parameter("enable_validation.position_y", true),
     rclcpp::Parameter("enable_validation.position_z", true),
     rclcpp::Parameter("enable_validation.angle_x", false),
     rclcpp::Parameter("enable_validation.angle_y", false),
     rclcpp::Parameter("enable_validation.angle_z", true)});
  auto node =
    std::make_shared<autoware::pose_instability_detector::PoseInstabilityDetector>(options);
  auto peer = std::make_shared<wrapper::Node>("instability_peer");
  using Odom = nav_msgs::msg::Odometry;
  using Twist = geometry_msgs::msg::TwistWithCovarianceStamped;
  using Diag = diagnostic_msgs::msg::DiagnosticArray;
  using Pose = geometry_msgs::msg::PoseStamped;
  auto odom = peer->create_publisher<Odom>("/pose_instability_detector/input/odometry", 10);
  auto twist = peer->create_publisher<Twist>("/pose_instability_detector/input/twist", 10);
  std::promise<Diag> dp;
  auto d = dp.get_future();
  bool gd = false;
  std::promise<Pose> pp;
  auto p = pp.get_future();
  bool gp = false;
  auto sub = peer->create_subscription<Diag>(
    "/diagnostics", 10, [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Diag) & m) {
      if (!gd && !m->status.empty() && m->header.stamp.sec == 101) {
        gd = true;
        dp.set_value(*m);
      }
    });
  auto ps = peer->create_subscription<Pose>(
    "/pose_instability_detector/debug/diff_pose", 10,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Pose) & m) {
      if (!gp && m->header.stamp.sec == 101) {
        gp = true;
        pp.set_value(*m);
      }
    });
  Odom first;
  first.header.stamp.sec = 100;
  first.header.frame_id = "map";
  first.pose.pose.orientation.w = 1.;
  first.pose.pose.position.x = 10.;
  Twist t;
  t.header.stamp.sec = 100;
  t.twist.twist.linear.x = 1.;
  auto exchange = [&](auto & e) {
    std::jthread spin([&] { e.spin(); });
    for (int i = 0; i < 5; ++i) {
      odom->publish(first);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    twist->publish(t);
    auto next = first;
    next.header.stamp.sec = 101;
    next.pose.pose.position.x = jump ? 15. : 11.;
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while ((d.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready ||
            p.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) &&
           std::chrono::steady_clock::now() < end) {
      odom->publish(next);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    e.cancel();
  };
  if (wrapper::use_agnocast()) {
    EXPECT_FALSE(rclcpp::ok());
    agnocast::AgnocastOnlySingleThreadedExecutor e;
    e.add_node(node->get_agnocast_node());
    e.add_node(peer->get_agnocast_node());
    exchange(e);
  } else {
    rclcpp::executors::SingleThreadedExecutor e;
    e.add_node(node->get_rclcpp_node());
    e.add_node(peer->get_rclcpp_node());
    exchange(e);
  }
  ASSERT_EQ(d.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  ASSERT_EQ(p.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto msg = d.get();
  ASSERT_EQ(msg.status.size(), 1U);
  EXPECT_EQ(
    msg.status[0].level, jump ? diagnostic_msgs::msg::DiagnosticStatus::ERROR
                              : diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(msg.status[0].name, "localization: pose_instability_detector");
  const auto diff = p.get();
  EXPECT_EQ(diff.header.frame_id, "base_link");
  EXPECT_NEAR(std::abs(diff.pose.position.x), jump ? 4. : 0., 1e-6);
  EXPECT_NEAR(diff.pose.position.y, 0., 1e-6);
}
TEST(InstabilityTransport, ConsistentMotion)
{
  check_motion(false);
}
TEST(InstabilityTransport, PoseJump)
{
  check_motion(true);
}
int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  int r = RUN_ALL_TESTS();
  wrapper::shutdown();
  return r;
}
