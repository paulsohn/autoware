// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/trajectory_adapter/trajectory_adapter_wrapper.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;
namespace adapter = autoware::trajectory_adapter;
using Scored = autoware_internal_planning_msgs::msg::ScoredCandidateTrajectories;
Scored candidates()
{
  Scored msg;
  for (int i = 1; i <= 2; ++i) {
    autoware_internal_planning_msgs::msg::ScoredCandidateTrajectory scored;
    scored.score = i == 1 ? .3f : .9f;
    auto & c = scored.candidate_trajectory;
    c.generator_id.uuid[0] = i;
    c.header.frame_id = "map";
    c.header.stamp.sec = 123;
    c.points.resize(1);
    c.points[0].pose.position.x = i * 10;
    c.turn_indicators_command.command = i;
    msg.scored_candidate_trajectories.push_back(scored);
    autoware_internal_planning_msgs::msg::GeneratorInfo info;
    info.generator_id = c.generator_id;
    info.generator_name.data = i == 1 ? "first" : "second";
    msg.generator_info.push_back(info);
  }
  return msg;
}
TEST(TrajectoryAdapter, RejectsEmpty)
{
  adapter::TrajectoryAdapter a;
  auto result = a.process(Scored{});
  ASSERT_FALSE(result);
  EXPECT_FALSE(result.error().empty());
}
TEST(TrajectoryAdapter, PreservesBestCandidate)
{
  adapter::TrajectoryAdapter a;
  auto input = candidates();
  auto result = a.process(input);
  ASSERT_TRUE(result);
  const auto & best = input.scored_candidate_trajectories[1].candidate_trajectory;
  EXPECT_EQ(result->trajectory.header, best.header);
  EXPECT_EQ(result->trajectory.points, best.points);
  EXPECT_EQ(result->turn_indicators, best.turn_indicators_command);
  EXPECT_EQ(result->best_generator_name, "second");
  EXPECT_FLOAT_EQ(result->best_score, .9);
}
TEST(TrajectoryAdapter, StableTieAndUnknownGenerator)
{
  adapter::TrajectoryAdapter a;
  auto input = candidates();
  input.scored_candidate_trajectories[0].score = .9;
  input.generator_info.clear();
  auto result = a.process(input);
  ASSERT_TRUE(result);
  EXPECT_DOUBLE_EQ(result->trajectory.points[0].pose.position.x, 10.);
  EXPECT_EQ(result->best_generator_name, "NOT FOUND");
}
TEST(TrajectoryAdapter, WrapperResultAndLatencyPublication)
{
  auto node = std::make_shared<wrapper::Node>("adapter");
  auto peer = std::make_shared<wrapper::Node>("adapter_peer");
  EXPECT_THROW(adapter::TrajectoryAdapterWrapper(*node, nullptr), std::runtime_error);
  adapter::TrajectoryAdapterWrapper w(*node, std::make_shared<autoware_utils_debug::TimeKeeper>());
  EXPECT_FALSE(w.get_trajectory(Scored{}));
  using Value = autoware_internal_debug_msgs::msg::Float64Stamped;
  std::promise<Value> promise;
  auto output = promise.get_future();
  bool done = false;
  auto sub = peer->create_subscription<Value>(
    "/adapter/debug/planning_component_latency_s", 10,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Value) & msg) {
      if (!done) {
        done = true;
        promise.set_value(*msg);
      }
    });
  auto exercise = [&] {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (output.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < end) {
      auto input = candidates();
      for (auto & c : input.scored_candidate_trajectories)
        c.candidate_trajectory.header.stamp = node->now() - rclcpp::Duration::from_seconds(.2);
      auto result = w.get_trajectory(input);
      ASSERT_TRUE(result);
      EXPECT_EQ(result->best_generator_name, "second");
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
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
  ASSERT_EQ(output.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto msg = output.get();
  EXPECT_GE(msg.data, .19);
  EXPECT_LT(msg.data, 1.);
  EXPECT_GT(msg.stamp.sec, 0);
}
int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  int result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
