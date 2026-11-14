// Copyright 2026 Taeseung Sohn
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <autoware/node/node.hpp>
#include <rclcpp/rclcpp.hpp>

#include <std_msgs/msg/string.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

TEST(AutowareNodePubSub, ExchangesMessage)
{
  rclcpp::init(0, nullptr);

  struct ShutdownGuard
  {
    ~ShutdownGuard() { rclcpp::shutdown(); }
  } shutdown_guard;

  const auto publisher_node = std::make_shared<autoware::node::Node>("publisher");
  const auto subscriber_node = std::make_shared<autoware::node::Node>("subscriber");

  std::optional<std::string> received;
  const auto subscription = subscriber_node->create_subscription<std_msgs::msg::String>(
    "autoware_node_pubsub", rclcpp::QoS(1).reliable(),
    [&received](const std_msgs::msg::String & message) { received = message.data; });
  const auto publisher = publisher_node->create_publisher<std_msgs::msg::String>(
    "autoware_node_pubsub", rclcpp::QoS(1).reliable());

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(publisher_node);
  executor.add_node(subscriber_node);

  std_msgs::msg::String message;
  message.data = "autoware-node";

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!received && std::chrono::steady_clock::now() < deadline) {
    publisher->publish(message);
    executor.spin_some();
  }

  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(*received, message.data);
}
