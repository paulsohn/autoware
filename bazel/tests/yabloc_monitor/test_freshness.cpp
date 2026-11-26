// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "yabloc_monitor_core.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <thread>
TEST(YablocMonitor, MissingFreshAndExpiredPose)
{
  rclcpp::init(0, nullptr);
  rclcpp::NodeOptions opts;
  opts.parameter_overrides({rclcpp::Parameter("availability/timestamp_tolerance", .5)});
  auto node = std::make_shared<YabLocMonitor>(opts);
  auto peer = std::make_shared<rclcpp::Node>("monitor_peer");
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(peer);
  auto pub = peer->create_publisher<geometry_msgs::msg::PoseStamped>(
    "/yabloc_monitor/input/yabloc_pose", 10);
  std::optional<diagnostic_msgs::msg::DiagnosticStatus> status;
  auto sub = peer->create_subscription<diagnostic_msgs::msg::DiagnosticArray>(
    "/diagnostics", 10, [&](diagnostic_msgs::msg::DiagnosticArray::ConstSharedPtr msg) {
      for (const auto & s : msg->status)
        if (s.name.find("yabloc_status") != std::string::npos) status = s;
    });
  auto pump = [&](auto pred, int ms = 3000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
      executor.spin_some();
      if (pred()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    return pred();
  };
  EXPECT_TRUE(
    pump([&] { return status && status->level == diagnostic_msgs::msg::DiagnosticStatus::ERROR; }));
  geometry_msgs::msg::PoseStamped pose;
  pose.header.stamp = peer->now();
  pub->publish(pose);
  EXPECT_TRUE(
    pump([&] { return status && status->level == diagnostic_msgs::msg::DiagnosticStatus::OK; }));
  if (status) {
    bool available = false;
    for (const auto & kv : status->values)
      if (kv.key == "Availability") {
        available = true;
        EXPECT_EQ(kv.value, "OK");
      }
    EXPECT_TRUE(available);
  }
  EXPECT_TRUE(
    pump([&] { return status && status->level == diagnostic_msgs::msg::DiagnosticStatus::ERROR; }));
  rclcpp::shutdown();
}
