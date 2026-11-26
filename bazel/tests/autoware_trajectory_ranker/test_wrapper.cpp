// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/trajectory_ranker/trajectory_ranker_wrapper.hpp>

#include <gtest/gtest.h>

namespace wrapper = autoware::agnocast_wrapper;
namespace ranker = autoware::trajectory_ranker;
using Risk = autoware_trajectory_validator::msg::RiskLevel;

TEST(TrajectoryRanker, LoadsEveryIndexedMetric)
{
  pluginlib::ClassLoader<ranker::metrics::MetricInterface> loader(
    "autoware_trajectory_ranker", "autoware::trajectory_ranker::metrics::MetricInterface");
  const auto classes = loader.getDeclaredClasses();
  EXPECT_EQ(classes.size(), 6u);
  for (const auto & name : classes) {
    auto plugin = loader.createSharedInstance(name);
    ASSERT_NE(plugin, nullptr) << name;
  }
}
TEST(TrajectoryRanker, WrapperScoringMetadataAndParameterUpdates)
{
  auto node = std::make_shared<wrapper::Node>("ranker");
  if (wrapper::use_agnocast()) EXPECT_FALSE(rclcpp::ok());
  autoware::vehicle_info_utils::VehicleInfo vehicle;
  vehicle.wheel_base_m = 2.5;
  EXPECT_THROW(
    ranker::TrajectoryRankerWrapper(*node, node->get_node_parameters_interface(), vehicle, nullptr),
    std::runtime_error);
  ranker::TrajectoryRankerWrapper w(
    *node, node->get_node_parameters_interface(), vehicle,
    std::make_shared<autoware_utils_debug::TimeKeeper>());
  ranker::RankerContext context;
  EXPECT_TRUE(w.rank_trajectories({}, context).scored_candidate_trajectories.empty());
  ranker::RankerInputTrajectories input;
  for (int id = 1; id <= 2; ++id) {
    ranker::RankerInputTrajectory t;
    t.source = id == 1 ? ranker::TrajectorySource::DIFFUSION_PLANNER
                       : ranker::TrajectorySource::BACKUP_PLANNER_GO;
    t.risk_level = id == 1 ? Risk::DANGER : Risk::SAFE;
    auto & c = t.candidate_trajectory;
    c.header.frame_id = "map";
    c.header.stamp.sec = 123;
    c.generator_id.uuid[0] = id;
    c.points.resize(4);
    for (int i = 0; i < 4; ++i) {
      c.points[i].pose.position.x = i * 5.;
      c.points[i].pose.orientation.w = 1.;
      c.points[i].longitudinal_velocity_mps = 5.;
      c.points[i].time_from_start.sec = i;
    }
    input.push_back(t);
    autoware_internal_planning_msgs::msg::GeneratorInfo info;
    info.generator_id = c.generator_id;
    info.generator_name.data = id == 1 ? "risky_primary" : "safe_backup";
    context.generator_info.push_back(info);
  }
  auto output = w.rank_trajectories(input, context);
  ASSERT_EQ(output.scored_candidate_trajectories.size(), 2u);
  EXPECT_EQ(output.generator_info, context.generator_info);
  EXPECT_EQ(
    output.scored_candidate_trajectories[0].candidate_trajectory, input[0].candidate_trajectory);
  EXPECT_LT(
    output.scored_candidate_trajectories[0].score, output.scored_candidate_trajectories[1].score);
  EXPECT_NEAR(output.scored_candidate_trajectories[0].score, 1. - 20. / 26., 1e-6);
  EXPECT_NEAR(output.scored_candidate_trajectories[1].score, 1. - 2.25 / 26., 1e-6);
  const auto updated = node->set_parameter(rclcpp::Parameter("safety.enable", false));
  ASSERT_TRUE(updated.successful) << updated.reason;
  output = w.rank_trajectories(input, context);
  ASSERT_EQ(output.scored_candidate_trajectories.size(), 2u);
  EXPECT_FLOAT_EQ(output.scored_candidate_trajectories[0].score, 1.f);
  EXPECT_GT(
    output.scored_candidate_trajectories[0].score, output.scored_candidate_trajectories[1].score);
}
int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const int result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
