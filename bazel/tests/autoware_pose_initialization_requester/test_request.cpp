// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "pose_initialization_requester.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
TEST(InitializationRequester, RequestsAutoOnlyWhileUninitialized)
{
  using State = autoware::component_interface_specs::localization::InitializationState;
  using Init = autoware::component_interface_specs::localization::Initialize;
  rclcpp::init(0, nullptr);
  auto node =
    std::make_shared<autoware::pose_initialization_requester::PoseInitializationRequester>(
      rclcpp::NodeOptions{});
  auto peer = std::make_shared<rclcpp::Node>("initialization_peer");
  std::atomic<int> calls = 0;
  std::atomic<bool> auto_empty = true;
  auto service = peer->create_service<Init::Service>(
    Init::name, [&](Init::Service::Request::SharedPtr req, Init::Service::Response::SharedPtr res) {
      if (req->method != Init::Service::Request::AUTO || !req->pose_with_covariance.empty())
        auto_empty = false;
      ++calls;
      res->status.success = true;
    });
  auto state =
    peer->create_publisher<State::Message>(State::name, rclcpp::QoS(1).transient_local());
  rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions{}, 3);
  executor.add_node(node);
  executor.add_node(peer);
  std::jthread spin([&] { executor.spin(); });
  auto wait = [&](auto pred, int ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!pred() && std::chrono::steady_clock::now() < end)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return pred();
  };
  const bool discovered = wait([&] { return state->get_subscription_count() > 0; }, 2000);
  EXPECT_TRUE(discovered);
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  EXPECT_EQ(calls.load(), 0);
  State::Message s;
  s.state = State::Message::UNINITIALIZED;
  state->publish(s);
  EXPECT_TRUE(wait([&] { return calls.load() >= 1; }, 2500));
  EXPECT_TRUE(auto_empty.load());
  s.state = State::Message::INITIALIZED;
  state->publish(s);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  const int before = calls.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  EXPECT_EQ(calls.load(), before);
  executor.cancel();
  spin.join();
  rclcpp::shutdown();
}
