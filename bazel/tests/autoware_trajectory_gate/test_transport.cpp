// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <trajectory_gate.hpp>

#include <diagnostic_msgs/msg/diagnostic_array.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;
using Trajectory = autoware_planning_msgs::msg::Trajectory;
using Source = tier4_system_msgs::msg::TrajectorySourceStatus;
using Change = tier4_system_msgs::srv::ChangeTrajectorySource;
TEST(TrajectoryGate, SelectionInvalidationAndTimeout)
{
  rclcpp::NodeOptions o;
  o.parameter_overrides(
    {rclcpp::Parameter("source_ids", std::vector<int64_t>{101, 102}),
     rclcpp::Parameter("source.101.name", "main"), rclcpp::Parameter("source.102.name", "mrm"),
     rclcpp::Parameter("trajectory_warn_duration", .15),
     rclcpp::Parameter("trajectory_error_duration", .3)});
  auto node = std::make_shared<autoware::trajectory_gate::TrajectoryGate>(o);
  auto peer = std::make_shared<wrapper::Node>("gate_peer");
  auto main = peer->create_publisher<Trajectory>("/trajectory_gate/inputs/main/trajectory", 10);
  auto mrm = peer->create_publisher<Trajectory>("/trajectory_gate/inputs/mrm/trajectory", 10);
  auto client = peer->create_client<Change>("/trajectory_gate/source/change");
  std::atomic<uint32_t> source{99999};
  std::atomic<int> got_main{0}, got_mrm{0}, main_level{-1}, mrm_level{-1};
  auto status = peer->create_subscription<Source>(
    "/trajectory_gate/source/status", rclcpp::QoS(1).transient_local(),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Source) & msg) { source.store(msg->source); });
  auto sub = peer->create_subscription<Trajectory>(
    "/trajectory_gate/output/trajectory", 10,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Trajectory) & msg) {
      EXPECT_EQ(msg->header.stamp.sec, 123);
      ASSERT_EQ(msg->points.size(), 1U);
      if (msg->header.frame_id == "main") {
        ++got_main;
        EXPECT_EQ(msg->points[0].pose.position.x, 1.);
      } else if (msg->header.frame_id == "mrm") {
        ++got_mrm;
        EXPECT_EQ(msg->points[0].pose.position.x, 2.);
      } else
        ADD_FAILURE() << msg->header.frame_id;
    });
  auto diag = peer->create_subscription<diagnostic_msgs::msg::DiagnosticArray>(
    "/diagnostics", 10,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(diagnostic_msgs::msg::DiagnosticArray) & msg) {
      for (const auto & s : msg->status) {
        if (s.name.find("main") != std::string::npos) main_level.store(s.level);
        if (s.name.find("mrm") != std::string::npos) mrm_level.store(s.level);
      }
    });
  auto pump = [&](auto pred, int ms = 3000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
      if (pred()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    return pred();
  };
  auto publish = [&] {
    auto a = ALLOCATE_OUTPUT_MESSAGE_UNIQUE(main);
    a->header.frame_id = "main";
    a->header.stamp.sec = 123;
    a->points.resize(1);
    a->points[0].pose.position.x = 1.;
    main->publish(std::move(a));
    auto b = ALLOCATE_OUTPUT_MESSAGE_UNIQUE(mrm);
    b->header.frame_id = "mrm";
    b->header.stamp.sec = 123;
    b->points.resize(1);
    b->points[0].pose.position.x = 2.;
    mrm->publish(std::move(b));
  };
  auto exercise = [&] {
    ASSERT_TRUE(pump([&] { return source.load() == 0 && client->service_is_ready(); }));
    auto select = [&](uint32_t id) {
      auto r = std::make_shared<Change::Request>();
      r->source = id;
      auto f = client->async_send_request(r);
      bool ready =
        pump([&] { return f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; });
      EXPECT_TRUE(ready);
      return ready && f.get()->status.success;
    };
    EXPECT_TRUE(select(101));
    ASSERT_TRUE(pump([&] { return source.load() == 101; }));
    ASSERT_TRUE(pump([&] {
      publish();
      return got_main.load() > 0 && main_level.load() == 0 && mrm_level.load() == 0;
    }));
    EXPECT_EQ(got_mrm.load(), 0);
    EXPECT_TRUE(select(102));
    ASSERT_TRUE(pump([&] { return source.load() == 102; }));
    ASSERT_TRUE(pump([&] {
      publish();
      return got_mrm.load() > 0;
    }));
    EXPECT_FALSE(select(999));
    ASSERT_TRUE(pump([&] { return source.load() == 0; }));
    pump([] { return false; }, 150);
    auto before = got_main.load() + got_mrm.load();
    pump(
      [&] {
        publish();
        return false;
      },
      150);
    EXPECT_EQ(got_main.load() + got_mrm.load(), before);
    ASSERT_TRUE(pump([&] { return main_level.load() == 2 && mrm_level.load() == 2; }));
  };
  auto run = [&](auto & e) {
    std::jthread t([&] { e.spin(); });
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
  int r = RUN_ALL_TESTS();
  wrapper::shutdown();
  return r;
}
