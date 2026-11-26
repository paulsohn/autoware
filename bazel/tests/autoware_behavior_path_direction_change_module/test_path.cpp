// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/behavior_path_planner/test_utils.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <optional>

TEST(DirectionChangeIntegration, ProducesFinitePathAndHandlesInvalidContext)
{
  namespace planner = autoware::behavior_path_planner;
  auto manager = planner::generateTestManager();
  auto node = planner::generateNode(
    {"direction_change"}, {"autoware::behavior_path_planner::DirectionChangeModuleManager"});
  using Path = autoware_internal_planning_msgs::msg::PathWithLaneId;
  std::optional<Path> path;
  manager->subscribeOutput<Path>(
    "behavior_path_planner/output/path", [&](const Path::ConstSharedPtr msg) { path = *msg; });
  planner::publishMandatoryTopics(manager, node);
  manager->testWithBehaviorNormalRoute(node, "behavior_path_planner/input/route");
  ASSERT_TRUE(path.has_value());
  EXPECT_EQ(path->header.frame_id, "map");
  ASSERT_FALSE(path->points.empty());
  for (const auto & p : path->points) {
    EXPECT_TRUE(std::isfinite(p.point.pose.position.x));
    EXPECT_TRUE(std::isfinite(p.point.pose.position.y));
    EXPECT_TRUE(std::isfinite(p.point.longitudinal_velocity_mps));
  }
  EXPECT_NO_THROW(manager->testWithAbnormalRoute(node, "behavior_path_planner/input/route"));
  EXPECT_NO_THROW(manager->testWithOffTrackOdometry(node, "behavior_path_planner/input/odometry"));
}
int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const int result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
