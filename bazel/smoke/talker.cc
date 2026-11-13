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

#include <chrono>
#include <memory>
#include <string>

class Talker : public rclcpp::Node
{
public:
  Talker() : Node("bazel_smoke_talker")
  {
    publisher_ = create_publisher<std_msgs::msg::String>("bazel_smoke", 10);
    timer_ = create_wall_timer(std::chrono::milliseconds(250), [this]() {
      std_msgs::msg::String message;
      message.data = "hello from bazel " + std::to_string(count_++);
      RCLCPP_INFO(get_logger(), "Publishing: '%s'", message.data.c_str());
      publisher_->publish(message);
    });
  }

private:
  std::size_t count_{0};
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<Talker>());
  rclcpp::shutdown();
  return 0;
}
