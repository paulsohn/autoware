// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#ifndef BAZEL_TESTS_RADAR_ROS_FIXTURE_HPP_
#define BAZEL_TESTS_RADAR_ROS_FIXTURE_HPP_
#include <rclcpp/rclcpp.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

class RadarRosTest : public testing::Test
{
protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }

  template <class Node>
  std::shared_ptr<Node> attach(std::shared_ptr<Node> node)
  {
    executor_.add_node(node);
    nodes_.push_back(node);
    return node;
  }

  bool await(
    const std::function<bool()> & ready, const std::function<void()> & publish,
    std::chrono::milliseconds timeout = std::chrono::seconds(5))
  {
    const auto until = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < until) {
      publish();
      executor_.spin_some();
      if (ready()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return ready();
  }

  void TearDown() override
  {
    for (const auto & node : nodes_) executor_.remove_node(node);
    nodes_.clear();
  }

  std::vector<std::shared_ptr<rclcpp::Node>> nodes_;
  rclcpp::executors::SingleThreadedExecutor executor_;
};
#endif
