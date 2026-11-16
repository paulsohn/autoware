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

#include "rclcpp/rclcpp.hpp"

#include "std_msgs/msg/string.hpp"

#include <cstdlib>
#include <memory>

class Listener : public rclcpp::Node
{
public:
  Listener() : Node("bazel_smoke_listener")
  {
    subscription_ = create_subscription<std_msgs::msg::String>(
      "bazel_smoke", 10, [this](const std_msgs::msg::String::ConstSharedPtr message) {
        RCLCPP_INFO(get_logger(), "Received: '%s'", message->data.c_str());
        if (++count_ == 5) {
          RCLCPP_INFO(get_logger(), "Received 5 messages; smoke test passed");
          std::exit(0);
        }
      });
  }

private:
  std::size_t count_{0};
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<Listener>());
  rclcpp::shutdown();
  return 0;
}
