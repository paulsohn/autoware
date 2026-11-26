// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware/trajectory_selector/trajectory_selector_node.hpp>
#include <autoware_test_utils/autoware_test_utils.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace wrapper = autoware::agnocast_wrapper;
using Candidates = autoware_internal_planning_msgs::msg::CandidateTrajectories;
using Scored = autoware_internal_planning_msgs::msg::ScoredCandidateTrajectories;
using Map = autoware_map_msgs::msg::LaneletMapBin;
using Odometry = nav_msgs::msg::Odometry;
using Acceleration = geometry_msgs::msg::AccelWithCovarianceStamped;
using Objects = autoware_perception_msgs::msg::PredictedObjects;
using Trajectory = autoware_planning_msgs::msg::Trajectory;
using Indicators = autoware_vehicle_msgs::msg::TurnIndicatorsCommand;

TEST(TrajectorySelector, ValidatesRanksAdaptsAndPublishesWithRealPlugins)
{
  rclcpp::NodeOptions options;
  const auto share = ament_index_cpp::get_package_share_directory("autoware_test_utils");
  options.arguments(
    {"--ros-args", "--params-file", share + "/config/test_vehicle_info.param.yaml"});
  options.append_parameter_override(
    "filter_names", std::vector<std::string>{"safety::VehicleConstraintFilter"});
  auto node = std::make_shared<autoware::trajectory_selector::TrajectorySelectorNode>(options);
  auto peer = std::make_shared<wrapper::Node>("selector_peer");
  auto map_pub = peer->create_publisher<Map>(
    "/trajectory_selector_node/input/lanelet2_map", rclcpp::QoS{1}.transient_local());
  auto map = autoware::test_utils::makeMapBinMsg("autoware_test_utils", "lanelet2_map.osm");
  auto odo_pub = peer->create_publisher<Odometry>("/trajectory_selector_node/input/odometry", 10);
  auto accel_pub =
    peer->create_publisher<Acceleration>("/trajectory_selector_node/input/acceleration", 10);
  auto objects_pub = peer->create_publisher<Objects>("/trajectory_selector_node/input/objects", 10);
  auto input_pub = peer->create_publisher<Candidates>(
    "/trajectory_selector_node/input/trajectories_generative", 10);
  std::atomic<bool> got_scores{false}, got_trajectory{false}, got_indicators{false};
  auto scores = peer->create_subscription<Scored>(
    "/trajectory_selector_node/output/scored_trajectories", 10,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Scored) & msg) {
      ASSERT_EQ(msg->scored_candidate_trajectories.size(), 1u);
      const auto & selected = msg->scored_candidate_trajectories[0];
      EXPECT_EQ(selected.candidate_trajectory.generator_id.uuid[0], 1);
      EXPECT_FLOAT_EQ(selected.score, 1.f);
      ASSERT_EQ(msg->generator_info.size(), 1u);
      EXPECT_EQ(msg->generator_info[0].generator_name.data, "DiffusionPlanner_safe");
      got_scores = true;
    });
  auto output = peer->create_subscription<Trajectory>(
    "/trajectory_selector_node/output/trajectory", 10,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Trajectory) & msg) {
      EXPECT_EQ(msg->header.frame_id, "map");
      EXPECT_GT(msg->header.stamp.sec, 0);
      ASSERT_EQ(msg->points.size(), 5u);
      for (int i = 0; i < 5; ++i) {
        EXPECT_DOUBLE_EQ(msg->points[i].pose.position.x, i * 5.);
        EXPECT_FLOAT_EQ(msg->points[i].longitudinal_velocity_mps, 5.f);
      }
      got_trajectory = true;
    });
  auto indicators = peer->create_subscription<Indicators>(
    "/trajectory_selector_node/output/turn_indicators", 10,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Indicators) & msg) {
      EXPECT_EQ(msg->command, Indicators::ENABLE_LEFT);
      EXPECT_GT(msg->stamp.sec, 0);
      got_indicators = true;
    });
  auto exercise = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
      map_pub->publish(map);
      Odometry odometry;
      odometry.pose.pose.orientation.w = 1.;
      odo_pub->publish(odometry);
      accel_pub->publish(Acceleration{});
      objects_pub->publish(Objects{});
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      Candidates input;
      for (int id = 1; id <= 2; ++id) {
        autoware_internal_planning_msgs::msg::CandidateTrajectory candidate;
        candidate.generator_id.uuid[0] = id;
        candidate.header.frame_id = "map";
        candidate.header.stamp = peer->now();
        candidate.turn_indicators_command.command = Indicators::ENABLE_LEFT;
        for (int i = 0; i < 5; ++i) {
          autoware_planning_msgs::msg::TrajectoryPoint p;
          p.pose.position.x = i * 5.;
          p.pose.orientation.w = 1.;
          p.longitudinal_velocity_mps = id == 1 ? 5. : 30.;
          p.time_from_start.sec = i;
          candidate.points.push_back(p);
        }
        input.candidate_trajectories.push_back(candidate);
        autoware_internal_planning_msgs::msg::GeneratorInfo info;
        info.generator_id = candidate.generator_id;
        info.generator_name.data = id == 1 ? "DiffusionPlanner_safe" : "DiffusionPlanner_fast";
        input.generator_info.push_back(info);
      }
      input_pub->publish(input);
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    } while (!(got_scores && got_trajectory && got_indicators) &&
             std::chrono::steady_clock::now() < deadline);
    EXPECT_TRUE(got_scores);
    EXPECT_TRUE(got_trajectory);
    EXPECT_TRUE(got_indicators);
  };
  auto run = [&](auto & e) {
    std::jthread thread([&] { e.spin(); });
    try {
      exercise();
    } catch (...) {
      e.cancel();
      throw;
    }
    e.cancel();
  };
  if (wrapper::use_agnocast()) {
    EXPECT_FALSE(rclcpp::ok());
    agnocast::AgnocastOnlySingleThreadedExecutor e;
    e.add_node(node->get_agnocast_node());
    e.add_node(peer->get_agnocast_node());
    run(e);
  } else {
    rclcpp::executors::SingleThreadedExecutor e;
    e.add_node(node->get_rclcpp_node());
    e.add_node(peer->get_rclcpp_node());
    run(e);
  }
}
int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const int result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
