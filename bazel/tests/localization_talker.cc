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

#include "autoware_localization_msgs/msg/kinematic_state.hpp"

#include <chrono>
#include <memory>
#include <string>

class LocalizationTalker : public rclcpp::Node
{
public:
  LocalizationTalker() : Node("bazel_localization_talker")
  {
    publisher_ = create_publisher<autoware_localization_msgs::msg::KinematicState>(
      "bazel_localization_state", 10);
    timer_ = create_wall_timer(std::chrono::milliseconds(250), [this]() {
      autoware_localization_msgs::msg::KinematicState message;
      message.header.stamp = now();
      message.header.frame_id = "map";
      message.child_frame_id = "base_link_" + std::to_string(count_);
      message.pose_with_covariance.pose.position.x = static_cast<double>(count_);
      RCLCPP_INFO(get_logger(), "Publishing localization state %zu", count_);
      ++count_;
      publisher_->publish(message);
    });
  }

private:
  std::size_t count_{0};
  rclcpp::Publisher<autoware_localization_msgs::msg::KinematicState>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<LocalizationTalker>());
  rclcpp::shutdown();
  return 0;
}
