#pragma once
#include <rclcpp/rclcpp.hpp>

#include <autoware_planning_msgs/msg/trajectory.hpp>

namespace probe
{
using Message = autoware_planning_msgs::msg::Trajectory;
void attach(rclcpp::Node & node, bool writer);
void detach();
unsigned returns();
}  // namespace probe
