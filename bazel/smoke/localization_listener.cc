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

#include <cstdlib>
#include <memory>
#include <string>

class LocalizationListener : public rclcpp::Node
{
public:
  LocalizationListener() : Node("bazel_localization_listener")
  {
    subscription_ = create_subscription<autoware_localization_msgs::msg::KinematicState>(
      "bazel_localization_state", 10,
      [this](const autoware_localization_msgs::msg::KinematicState::ConstSharedPtr message) {
        const double x = message->pose_with_covariance.pose.position.x;
        const auto index = static_cast<std::size_t>(x);
        const std::string expected_child = "base_link_" + std::to_string(index);
        if (
          message->header.frame_id != "map" || message->child_frame_id != expected_child ||
          x != static_cast<double>(index)) {
          RCLCPP_ERROR(get_logger(), "Received inconsistent localization state");
          std::exit(1);
        }

        RCLCPP_INFO(
          get_logger(), "Received localization state '%s' at x=%.0f",
          message->child_frame_id.c_str(), x);
        if (++count_ == 5) {
          RCLCPP_INFO(get_logger(), "Received 5 localization states; behavior test passed");
          std::exit(0);
        }
      });
  }

private:
  std::size_t count_{0};
  rclcpp::Subscription<autoware_localization_msgs::msg::KinematicState>::SharedPtr subscription_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<LocalizationListener>());
  rclcpp::shutdown();
  return 0;
}
