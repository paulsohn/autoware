// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware/trajectory_modifier/trajectory_modifier.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace wrapper = autoware::agnocast_wrapper;
namespace modifier = autoware::trajectory_modifier;
using Candidates = autoware_internal_planning_msgs::msg::CandidateTrajectories;
using Trajectory = autoware_planning_msgs::msg::Trajectory;
using Odometry = nav_msgs::msg::Odometry;
using Acceleration = geometry_msgs::msg::AccelWithCovarianceStamped;

TEST(TrajectoryModifier, LoadsAllFourteenIndexedPlugins)
{
  pluginlib::ClassLoader<modifier::plugin::TrajectoryModifierPluginBase> loader(
    "autoware_trajectory_modifier",
    "autoware::trajectory_modifier::plugin::TrajectoryModifierPluginBase");
  const auto classes = loader.getDeclaredClasses();
  EXPECT_EQ(classes.size(), 14u);
  for (const auto & name : classes) {
    auto plugin = loader.createSharedInstance(name);
    ASSERT_NE(plugin, nullptr) << name;
  }
}
TEST(TrajectoryModifier, StopPluginProcessesEveryCandidateAndPublishesSelection)
{
  rclcpp::NodeOptions options;
  const auto share = ament_index_cpp::get_package_share_directory("autoware_test_utils");
  options.arguments(
    {"--ros-args", "--params-file", share + "/config/test_vehicle_info.param.yaml"});
  options.append_parameter_override(
    "plugin_names",
    std::vector<std::string>{"autoware::trajectory_modifier::plugin::StopPointFixer"});
  options.append_parameter_override("stop_point_fixer.force_stop_close_stopped_trajectories", true);
  options.append_parameter_override("stop_point_fixer.min_distance_threshold", 1.);
  auto node = std::make_shared<modifier::TrajectoryModifier>(options);
  auto peer = std::make_shared<wrapper::Node>("modifier_peer");
  Candidates input;
  for (int id = 1; id <= 2; ++id) {
    autoware_internal_planning_msgs::msg::CandidateTrajectory c;
    c.generator_id.uuid[0] = id;
    c.header.frame_id = "map";
    c.header.stamp.sec = 123;
    for (int i = 0; i < 4; ++i) {
      autoware_planning_msgs::msg::TrajectoryPoint p;
      p.pose.position.x = id == 1 ? i * .1 : i + 10.;
      p.pose.orientation.w = 1.;
      p.longitudinal_velocity_mps = 5.;
      p.time_from_start.sec = i;
      c.points.push_back(p);
    }
    input.candidate_trajectories.push_back(c);
  }
  std::atomic<bool> got_array{false}, got_selected{false};
  auto output = peer->create_subscription<Candidates>(
    "/trajectory_modifier/output/trajectories", 10,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Candidates) & msg) {
      ASSERT_EQ(msg->candidate_trajectories.size(), 2u);
      const auto & stopped = msg->candidate_trajectories[0];
      EXPECT_EQ(stopped.header, input.candidate_trajectories[0].header);
      EXPECT_EQ(stopped.generator_id, input.candidate_trajectories[0].generator_id);
      ASSERT_GE(stopped.points.size(), 3u);
      for (const auto & p : stopped.points) {
        EXPECT_FLOAT_EQ(p.longitudinal_velocity_mps, 0.f);
        EXPECT_NEAR(p.pose.position.x, 0., .0021);
      }
      EXPECT_EQ(msg->candidate_trajectories[1], input.candidate_trajectories[1]);
      got_array = true;
    });
  auto selection = peer->create_subscription<Trajectory>(
    "/trajectory_modifier/output/trajectory", 10,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Trajectory) & msg) {
      EXPECT_EQ(msg->header, input.candidate_trajectories[0].header);
      ASSERT_GE(msg->points.size(), 3u);
      for (const auto & p : msg->points) EXPECT_FLOAT_EQ(p.longitudinal_velocity_mps, 0.f);
      got_selected = true;
    });
  auto trajectories =
    peer->create_publisher<Candidates>("/trajectory_modifier/input/trajectories", 10);
  auto odometry = peer->create_publisher<Odometry>("/trajectory_modifier/input/odometry", 10);
  auto acceleration =
    peer->create_publisher<Acceleration>("/trajectory_modifier/input/acceleration", 10);
  auto exercise = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
      Odometry odo;
      odo.pose.pose.orientation.w = 1.;
      odometry->publish(odo);
      acceleration->publish(Acceleration{});
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      trajectories->publish(input);
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    } while (!(got_array && got_selected) && std::chrono::steady_clock::now() < deadline);
    EXPECT_TRUE(got_array);
    EXPECT_TRUE(got_selected);
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
