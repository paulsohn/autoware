#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("autoware_bazel_smoke_listener");
  auto sub = node->create_subscription<std_msgs::msg::String>(
    "chatter", 10, [&](const std_msgs::msg::String & msg) {
      RCLCPP_INFO(node->get_logger(), "I heard: %s", msg.data.c_str());
    });
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
