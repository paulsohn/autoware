// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware/mission_planner_universe/default_planner.hpp>
#include <autoware_test_utils/autoware_test_utils.hpp>
#include <autoware_vehicle_info_utils/vehicle_info_utils.hpp>

#include <geometry_msgs/msg/pose.hpp>

#include <gtest/gtest.h>
#include <pcl/PCLPointCloud2.h>
#include <pcl/io/pcd_io.h>
#include <yaml-cpp/yaml.h>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
std::string scenario_path;
geometry_msgs::msg::Pose pose(const YAML::Node & values)
{
  geometry_msgs::msg::Pose result;
  result.position.x = values[0].as<double>();
  result.position.y = values[1].as<double>();
  result.position.z = values[2].as<double>();
  const auto yaw = values[3].as<double>();
  result.orientation.z = std::sin(yaw / 2);
  result.orientation.w = std::cos(yaw / 2);
  return result;
}

TEST(PlanningMap, IndexedScenarioIsRoutable)
{
  const auto scenario = YAML::LoadFile(scenario_path);
  const auto map_share =
    ament_index_cpp::get_package_share_directory(scenario["map_package"].as<std::string>());
  const auto vehicle_share = ament_index_cpp::get_package_share_directory(
    scenario["vehicle_model"].as<std::string>() + "_description");
  const auto vehicle =
    YAML::LoadFile(vehicle_share + "/config/vehicle_info.param.yaml")["/**"]["ros__parameters"];
  const auto info = autoware::vehicle_info_utils::createVehicleInfo(
    vehicle["wheel_radius"].as<double>(), vehicle["wheel_width"].as<double>(),
    vehicle["wheel_base"].as<double>(), vehicle["wheel_tread"].as<double>(),
    vehicle["front_overhang"].as<double>(), vehicle["rear_overhang"].as<double>(),
    vehicle["left_overhang"].as<double>(), vehicle["right_overhang"].as<double>(),
    vehicle["vehicle_height"].as<double>(), vehicle["max_steer_angle"].as<double>());
  autoware::mission_planner_universe::lanelet2::DefaultPlannerParameters params{};
  params.goal_angle_threshold_deg = 45.0;
  params.check_footprint_inside_lanes = true;
  autoware::mission_planner_universe::lanelet2::DefaultPlanner planner(params, info);
  const auto map = autoware::test_utils::make_map_bin_msg(map_share + "/lanelet2_map.osm");
  ASSERT_FALSE(map.data.empty());
  planner.set_map(map);
  ASSERT_TRUE(planner.ready());
  const auto start = pose(scenario["initial_pose"]);
  const auto goal = pose(scenario["goal_pose"]);
  const auto result = planner.plan({start, goal});
  ASSERT_FALSE(result.route.segments.empty()) << result.warning_message.value_or("");
  std::vector<int64_t> preferred;
  for (const auto & segment : result.route.segments)
    preferred.push_back(segment.preferred_primitive.id);
  EXPECT_EQ(preferred, scenario["preferred_lanelets"].as<std::vector<int64_t>>());
  EXPECT_DOUBLE_EQ(result.route.goal_pose.position.x, goal.position.x);
  EXPECT_DOUBLE_EQ(result.route.goal_pose.position.y, goal.position.y);
  EXPECT_GT(
    std::hypot(goal.position.x - start.position.x, goal.position.y - start.position.y), 15.0);
  std::cout << "Preferred lanelets:";
  for (const auto & segment : result.route.segments) {
    std::cout << " " << segment.preferred_primitive.id;
  }
  std::cout << std::endl;
}
TEST(PlanningMap, PointcloudLoadsWithPcl)
{
  const auto scenario = YAML::LoadFile(scenario_path);
  const auto share =
    ament_index_cpp::get_package_share_directory(scenario["map_package"].as<std::string>());
  pcl::PCLPointCloud2 cloud;
  ASSERT_EQ(pcl::io::loadPCDFile(share + "/pointcloud_map.pcd", cloud), 0);
  EXPECT_EQ(cloud.width, 1757841U);
  EXPECT_EQ(cloud.height, 1U);
  EXPECT_EQ(cloud.data.size(), 1757841U * 16U);
  ASSERT_EQ(cloud.fields.size(), 4U);
  EXPECT_EQ(cloud.fields.front().name, "x");
  EXPECT_EQ(cloud.fields.back().name, "rgb");
}
}  // namespace

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  if (argc != 2) return 2;
  scenario_path = argv[1];
  return RUN_ALL_TESTS();
}
