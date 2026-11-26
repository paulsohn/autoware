// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include "scene.hpp"

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware_test_utils/autoware_test_utils.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

TEST(TemplateScene, LeavesTrajectoryAndVisualizationUnchanged)
{
  rclcpp::init(0, nullptr);
  {
    const auto share = ament_index_cpp::get_package_share_directory("autoware_test_utils");
    const auto common =
      ament_index_cpp::get_package_share_directory("autoware_behavior_velocity_planner_common");
    rclcpp::NodeOptions options;
    autoware::test_utils::updateNodeOptions(
      options, {share + "/config/test_vehicle_info.param.yaml",
                common + "/config/behavior_velocity_planner_common.param.yaml"});
    rclcpp::Node node("template_scene_peer", options);
    const autoware::behavior_velocity_planner::PlannerData data(node);
    autoware::behavior_velocity_planner::TemplateModule scene(
      42, node.get_logger(), node.get_clock(), nullptr, nullptr);
    using Point = autoware_internal_planning_msgs::msg::PathPointWithLaneId;
    std::vector<Point> points(11);
    for (size_t i = 0; i < points.size(); ++i) {
      points[i].point.pose.position.x = static_cast<double>(i);
      points[i].point.pose.orientation.w = 1;
      points[i].point.longitudinal_velocity_mps = 3;
      points[i].lane_ids = {42};
    }
    using Trajectory = autoware::behavior_velocity_planner::experimental::Trajectory;
    auto trajectory = Trajectory::Builder{}.build(points).value();
    const auto before = trajectory.restore();
    EXPECT_FALSE(scene.modifyPathVelocity(trajectory, {}, {}, data));
    EXPECT_EQ(trajectory.restore(), before);
    EXPECT_TRUE(scene.createDebugMarkerArray().markers.empty());
    EXPECT_TRUE(scene.createVirtualWalls().empty());
    EXPECT_EQ(scene.getModuleId(), 42);
  }
  rclcpp::shutdown();
}
