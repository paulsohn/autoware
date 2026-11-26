// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware/behavior_path_sampling_planner_module/manager.hpp>
#include <autoware_test_utils/autoware_test_utils.hpp>

#include <gtest/gtest.h>
#include <lanelet2_core/LaneletMap.h>

#include <cmath>
#include <memory>
#include <vector>

TEST(SamplingPlannerIntegration, GeneratesSampledPathAndReusesIt)
{
  namespace planner = autoware::behavior_path_planner;
  namespace fixtures = autoware::test_utils;
  rclcpp::NodeOptions options;
  const auto test_share = ament_index_cpp::get_package_share_directory("autoware_test_utils");
  const auto planner_share =
    ament_index_cpp::get_package_share_directory("autoware_behavior_path_planner");
  const auto scene_share =
    ament_index_cpp::get_package_share_directory("autoware_behavior_path_sampling_planner_module");
  fixtures::updateNodeOptions(
    options, {test_share + "/config/test_common.param.yaml",
              test_share + "/config/test_nearest_search.param.yaml",
              test_share + "/config/test_vehicle_info.param.yaml",
              planner_share + "/config/behavior_path_planner.param.yaml",
              planner_share + "/config/drivable_area_expansion.param.yaml",
              planner_share + "/config/scene_module_manager.param.yaml",
              scene_share + "/config/sampling_planner.param.yaml"});
  options.append_parameter_override("sampling.target_lengths", std::vector<double>{10.0});
  options.append_parameter_override(
    "sampling.target_lateral_positions", std::vector<double>{-1.0, 0.0, 1.0});
  options.append_parameter_override(
    "sampling.frenet.target_lateral_velocities", std::vector<double>{0.0});
  auto node = std::make_shared<rclcpp::Node>("sampling_scene", options);
  auto data = std::make_shared<planner::PlannerData>();
  data->init_parameters(*node);
  lanelet::Lanelet lane(
    100,
    lanelet::LineString3d(
      101, {lanelet::Point3d(102, 0.0, 5.0, 0.0), lanelet::Point3d(103, 100.0, 5.0, 0.0)}),
    lanelet::LineString3d(
      104, {lanelet::Point3d(105, 0.0, -5.0, 0.0), lanelet::Point3d(106, 100.0, -5.0, 0.0)}));
  lane.attributes()["subtype"] = "road";
  lane.attributes()["location"] = "urban";
  lane.attributes()["one_way"] = "yes";
  auto map = std::make_shared<lanelet::LaneletMap>();
  map->add(lane);
  data->route_handler->setMap(map);
  autoware_planning_msgs::msg::LaneletRoute route;
  route.header.frame_id = "map";
  route.start_pose.orientation.w = route.goal_pose.orientation.w = 1.0;
  route.start_pose.position.x = 10.0;
  route.goal_pose.position.x = 90.0;
  route.segments.push_back(fixtures::createLaneletSegment(lane.id()));
  data->route_handler->setRoute(route);
  planner::PathWithLaneId reference;
  reference.header.frame_id = "map";
  for (int i = 0; i <= 100; ++i) {
    autoware_internal_planning_msgs::msg::PathPointWithLaneId point;
    point.point.pose.position.x = i;
    point.point.pose.orientation.w = 1.0;
    point.point.longitudinal_velocity_mps = 2.0;
    point.lane_ids.push_back(lane.id());
    reference.points.push_back(point);
  }
  nav_msgs::msg::Odometry odometry;
  odometry.pose.pose = route.start_pose;
  odometry.twist.twist.linear.x = 2.0;
  data->self_odometry = std::make_shared<nav_msgs::msg::Odometry>(odometry);
  data->dynamic_object = std::make_shared<autoware_perception_msgs::msg::PredictedObjects>();
  *data->prev_output_path = reference;
  planner::SamplingPlannerModuleManager manager;
  manager.init(node.get());
  auto scene_owner = manager.createNewSceneModuleInstance();
  auto * scene = dynamic_cast<planner::SamplingPlannerModule *>(scene_owner.get());
  ASSERT_NE(scene, nullptr);
  scene->setData(data);
  planner::BehaviorModuleOutput previous;
  previous.path = reference;
  previous.reference_path = reference;
  scene->setPreviousModuleOutput(previous);
  EXPECT_TRUE(scene->isExecutionReady());
  const auto result = scene->plan();
  ASSERT_GT(result.path.points.size(), 2U);
  EXPECT_GT(
    std::hypot(
      result.path.points.front().point.pose.position.x -
        reference.points.front().point.pose.position.x,
      result.path.points.front().point.pose.position.y -
        reference.points.front().point.pose.position.y),
    0.1);
  for (const auto & point : result.path.points) {
    EXPECT_TRUE(std::isfinite(point.point.pose.position.x));
    EXPECT_TRUE(std::isfinite(point.point.pose.position.y));
    EXPECT_TRUE(std::isfinite(point.point.longitudinal_velocity_mps));
    EXPECT_FALSE(point.lane_ids.empty());
  }
  const auto reused = scene->plan();
  EXPECT_GT(reused.path.points.size(), 2U);
  previous.reference_path.points.clear();
  scene->setPreviousModuleOutput(previous);
  EXPECT_TRUE(scene->plan().path.points.empty());
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const int result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
