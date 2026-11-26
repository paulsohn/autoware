// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/trajectory_validator/trajectory_validator_wrapper.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace wrapper = autoware::agnocast_wrapper;
namespace validator_api = autoware::trajectory_validator;
using Reports = autoware_trajectory_validator::msg::ValidationReportArray;
using Risk = autoware_trajectory_validator::msg::RiskLevel;
using Candidates = autoware_internal_planning_msgs::msg::CandidateTrajectories;

Candidates candidates()
{
  Candidates msg;
  for (int id = 1; id <= 2; ++id) {
    autoware_internal_planning_msgs::msg::CandidateTrajectory c;
    c.generator_id.uuid[0] = id;
    c.header.frame_id = "map";
    c.header.stamp.sec = 123;
    for (int i = 0; i < 4; ++i) {
      autoware_planning_msgs::msg::TrajectoryPoint point;
      point.pose.position.x = i * 5;
      point.pose.orientation.w = 1.;
      point.longitudinal_velocity_mps = id == 1 ? 5. : 20.;
      point.time_from_start.sec = i;
      c.points.push_back(point);
    }
    msg.candidate_trajectories.push_back(c);
    autoware_internal_planning_msgs::msg::GeneratorInfo info;
    info.generator_id = c.generator_id;
    info.generator_name.data = id == 1 ? "safe" : "overspeed";
    msg.generator_info.push_back(info);
  }
  return msg;
}
TEST(TrajectoryValidator, LoadsEveryIndexedPlugin)
{
  pluginlib::ClassLoader<validator_api::plugin::ValidatorInterface> loader(
    "autoware_trajectory_validator", "autoware::trajectory_validator::plugin::ValidatorInterface");
  const auto classes = loader.getDeclaredClasses();
  EXPECT_EQ(classes.size(), 4u);
  for (const auto & name : classes) {
    auto plugin = loader.createSharedInstance(name);
    ASSERT_NE(plugin, nullptr) << name;
    EXPECT_FALSE(plugin->get_name().empty());
    EXPECT_FALSE(plugin->is_shadow_mode());
  }
}
void check_wrapper(bool shadow)
{
  const std::string name = shadow ? "validator_shadow" : "validator_active";
  rclcpp::NodeOptions options;
  options.parameter_overrides({rclcpp::Parameter(
    shadow ? "shadow_mode_filter_names" : "filter_names",
    std::vector<std::string>{"safety::VehicleConstraintFilter"})});
  auto node = std::make_shared<wrapper::Node>(name, options);
  auto peer = std::make_shared<wrapper::Node>(name + "_peer");
  autoware::vehicle_info_utils::VehicleInfo vehicle;
  vehicle.wheel_base_m = 2.5;
  EXPECT_THROW(
    validator_api::TrajectoryValidatorWrapper(
      *node, node->get_node_parameters_interface(), vehicle, nullptr),
    std::runtime_error);
  validator_api::TrajectoryValidatorWrapper w(
    *node, node->get_node_parameters_interface(), vehicle,
    std::make_shared<autoware_utils_debug::TimeKeeper>());
  validator_api::ValidatorContext context;
  context.odometry = std::make_shared<nav_msgs::msg::Odometry>();
  std::atomic<bool> received{false};
  auto sub = peer->create_subscription<Reports>(
    "/" + name + "/debug/validation_reports", 10,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Reports) & msg) {
      if (msg->reports.size() != 2) return;
      EXPECT_EQ(msg->reports[0].generator_name, "safe");
      EXPECT_EQ(msg->reports[1].generator_name, "overspeed");
      EXPECT_EQ(msg->reports[1].risk.level, shadow ? Risk::SAFE : Risk::HIGH_CAUTION);
      received = true;
    });
  auto exercise = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    do {
      const auto report = w.validate_trajectories(candidates(), context);
      ASSERT_EQ(report.evaluation_tables.size(), 2u);
      ASSERT_EQ(report.evaluation_tables[0].plugin_evaluations.size(), 1u);
      EXPECT_TRUE(report.evaluation_tables[0].all_feasible());
      EXPECT_FALSE(report.evaluation_tables[1].all_feasible());
      EXPECT_EQ(report.evaluation_tables[1].plugin_evaluations[0].is_shadow_mode, shadow);
      EXPECT_EQ(report.num_feasible_trajectories, 1u);
      ASSERT_EQ(report.valid_trajectories.candidate_trajectories.size(), shadow ? 2u : 1u);
      EXPECT_EQ(report.valid_trajectories.candidate_trajectories[0].generator_id.uuid[0], 1);
      ASSERT_EQ(report.validation_reports.size(), 2u);
      EXPECT_EQ(report.validation_reports[1].trajectory_stamp.sec, 123);
      EXPECT_EQ(report.validation_reports[1].generator_id.uuid[0], 2);
      ASSERT_FALSE(report.validation_reports[1].metrics.empty());
      EXPECT_EQ(report.validation_reports[1].risk.level, shadow ? Risk::SAFE : Risk::HIGH_CAUTION);
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    } while (!received && std::chrono::steady_clock::now() < deadline);
    EXPECT_TRUE(received);
  };
  auto run = [&](auto & executor) {
    std::jthread thread([&] { executor.spin(); });
    try {
      exercise();
    } catch (...) {
      executor.cancel();
      throw;
    }
    executor.cancel();
  };
  if (wrapper::use_agnocast()) {
    EXPECT_FALSE(rclcpp::ok());
    agnocast::AgnocastOnlySingleThreadedExecutor executor;
    executor.add_node(node->get_agnocast_node());
    executor.add_node(peer->get_agnocast_node());
    run(executor);
  } else {
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node->get_rclcpp_node());
    executor.add_node(peer->get_rclcpp_node());
    run(executor);
  }
}
TEST(TrajectoryValidator, ActiveFilterAndPublishedReport)
{
  check_wrapper(false);
}
TEST(TrajectoryValidator, ShadowFilterReportsWithoutRejection)
{
  check_wrapper(true);
}
int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const int result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
