// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "localization_error_monitor.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;
void check_ellipse(double radius, uint8_t level)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
    {rclcpp::Parameter("scale", 1.), rclcpp::Parameter("warn_ellipse_size", 1.),
     rclcpp::Parameter("error_ellipse_size", 2.),
     rclcpp::Parameter("warn_ellipse_size_lateral_direction", 1.),
     rclcpp::Parameter("error_ellipse_size_lateral_direction", 2.)});
  auto node =
    std::make_shared<autoware::localization_error_monitor::LocalizationErrorMonitor>(options);
  auto peer = std::make_shared<wrapper::Node>("error_peer");
  using Odom = nav_msgs::msg::Odometry;
  using Marker = visualization_msgs::msg::Marker;
  using Diag = diagnostic_msgs::msg::DiagnosticArray;
  auto pub = peer->create_publisher<Odom>("input/odom", 1);
  std::promise<Marker> marker;
  auto m = marker.get_future();
  bool got_m = false;
  std::promise<Diag> diag;
  auto d = diag.get_future();
  bool got_d = false;
  auto sub = peer->create_subscription<Marker>(
    "debug/ellipse_marker", rclcpp::QoS(1).transient_local(),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Marker) & p) {
      if (!got_m) {
        got_m = true;
        marker.set_value(*p);
      }
    });
  auto ds = peer->create_subscription<Diag>(
    "/diagnostics", 10, [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Diag) & p) {
      if (!got_d && !p->status.empty()) {
        got_d = true;
        diag.set_value(*p);
      }
    });
  Odom input;
  input.header.frame_id = "map";
  input.header.stamp.sec = 123;
  input.pose.pose.orientation.w = 1.;
  input.pose.pose.position.x = 10.;
  input.pose.pose.position.y = 20.;
  input.pose.covariance[0] = input.pose.covariance[7] = radius * radius;
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while ((m.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready ||
            d.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) &&
           std::chrono::steady_clock::now() < end) {
      pub->publish(input);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    executor.cancel();
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
  ASSERT_EQ(m.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  ASSERT_EQ(d.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  auto output = m.get();
  EXPECT_EQ(output.header, input.header);
  EXPECT_DOUBLE_EQ(output.pose.position.x, 10.);
  EXPECT_DOUBLE_EQ(output.pose.position.y, 20.);
  EXPECT_NEAR(output.scale.x, 2 * radius, 1e-8);
  EXPECT_NEAR(output.scale.y, 2 * radius, 1e-8);
  EXPECT_EQ(output.ns, "error_ellipse");
  const auto status = d.get().status;
  ASSERT_EQ(status.size(), 1U);
  EXPECT_EQ(status[0].level, level);
  bool long_axis = false, lateral = false;
  for (const auto & kv : status[0].values) {
    if (kv.key == "localization_error_ellipse") {
      long_axis = true;
      EXPECT_NEAR(std::stod(kv.value), radius, 1e-6);
    }
    if (kv.key == "localization_error_ellipse_lateral_direction") {
      lateral = true;
      EXPECT_NEAR(std::stod(kv.value), radius, 1e-6);
    }
  }
  EXPECT_TRUE(long_axis);
  EXPECT_TRUE(lateral);
}
TEST(ErrorMonitorTransport, Healthy)
{
  check_ellipse(.5, diagnostic_msgs::msg::DiagnosticStatus::OK);
}
TEST(ErrorMonitorTransport, Warning)
{
  check_ellipse(1.5, diagnostic_msgs::msg::DiagnosticStatus::WARN);
}
TEST(ErrorMonitorTransport, Error)
{
  check_ellipse(2.5, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
}
int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  int result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
