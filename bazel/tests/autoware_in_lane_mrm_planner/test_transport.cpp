// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware/agnocast_wrapper/node.hpp>
#include <autoware_test_utils/autoware_test_utils.hpp>
#include <class_loader/class_loader.hpp>
#include <rclcpp_components/node_factory.hpp>

#include <autoware_internal_debug_msgs/msg/float32_multi_array_stamped.hpp>
#include <autoware_perception_msgs/msg/predicted_objects.hpp>
#include <autoware_vehicle_msgs/msg/gear_command.hpp>
#include <autoware_vehicle_msgs/msg/hazard_lights_command.hpp>
#include <geometry_msgs/msg/accel_with_covariance_stamped.hpp>
#include <tier4_system_msgs/msg/in_lane_stop_trigger.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

namespace wrapper = autoware::agnocast_wrapper;
namespace fixtures = autoware::test_utils;
std::string component_path;

TEST(InLaneMrmTransport, StandbyLatchReleaseAndAuxiliaryCommands)
{
  auto loader = std::make_shared<class_loader::ClassLoader>(component_path);
  auto make_node = [&](const std::string & type, rclcpp::NodeOptions options) {
    auto factory = loader->createInstance<rclcpp_components::NodeFactory>(
      "rclcpp_components::NodeFactoryTemplate<autoware::in_lane_mrm_planner::" + type + ">");
    return factory->create_node_instance(options);
  };
  rclcpp::NodeOptions options;
  const auto share = ament_index_cpp::get_package_share_directory("autoware_in_lane_mrm_planner");
  const auto test_share = ament_index_cpp::get_package_share_directory("autoware_test_utils");
  fixtures::updateNodeOptions(
    options, {share + "/config/in_lane_mrm_planner.param.yaml",
              share + "/config/elastic_band_smoother.param.yaml",
              test_share + "/config/test_vehicle_info.param.yaml"});
  auto planner_instance = make_node("InLaneMrmPlannerNode", options);
  auto auxiliary_instance = make_node("MrmInLaneStopAuxiliaryCommandsNode", rclcpp::NodeOptions{});
  auto planner = std::static_pointer_cast<wrapper::Node>(planner_instance.get_node_instance());
  auto auxiliary = std::static_pointer_cast<wrapper::Node>(auxiliary_instance.get_node_instance());
  auto peer = std::make_shared<wrapper::Node>("in_lane_mrm_peer");
  using Map = autoware_map_msgs::msg::LaneletMapBin;
  using Route = autoware_planning_msgs::msg::LaneletRoute;
  using Odom = nav_msgs::msg::Odometry;
  using Accel = geometry_msgs::msg::AccelWithCovarianceStamped;
  using Objects = autoware_perception_msgs::msg::PredictedObjects;
  using Trigger = tier4_system_msgs::msg::InLaneStopTrigger;
  using Trajectory = autoware_planning_msgs::msg::Trajectory;
  using Debug = autoware_internal_debug_msgs::msg::Float32MultiArrayStamped;
  const std::string prefix = "/in_lane_mrm_planner";
  auto map_pub =
    peer->create_publisher<Map>(prefix + "/input/vector_map", rclcpp::QoS(1).transient_local());
  auto route_pub =
    peer->create_publisher<Route>(prefix + "/input/route", rclcpp::QoS(1).transient_local());
  auto odom_pub = peer->create_publisher<Odom>(prefix + "/input/kinematic_state", 1);
  auto accel_pub = peer->create_publisher<Accel>(prefix + "/input/acceleration", 1);
  auto objects_pub = peer->create_publisher<Objects>(prefix + "/input/objects", 1);
  auto trigger_pub =
    peer->create_publisher<Trigger>(prefix + "/input/trigger", rclcpp::QoS(1).transient_local());
  std::mutex mutex;
  Trajectory output;
  std::atomic<int> trajectories{0}, latched{-1}, profile{-1}, gears{0}, hazards{0};
  auto sub = peer->create_subscription<Trajectory>(
    prefix + "/output/trajectory", 1,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Trajectory) & msg) {
      std::scoped_lock lock(mutex);
      output = *msg;
      ++trajectories;
    });
  auto debug = peer->create_subscription<Debug>(
    prefix + "/debug/planner_status", 1, [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Debug) & msg) {
      if (msg->data.size() > 13) {
        latched = static_cast<int>(msg->data[2]);
        profile = static_cast<int>(msg->data[13]);
      }
    });
  using Gear = autoware_vehicle_msgs::msg::GearCommand;
  using Hazard = autoware_vehicle_msgs::msg::HazardLightsCommand;
  auto gear = peer->create_subscription<Gear>(
    "/mrm_in_lane_stop_auxiliary_commands/output/gear_command", 1,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Gear) & msg) {
      EXPECT_EQ(msg->command, Gear::DRIVE);
      ++gears;
    });
  auto hazard = peer->create_subscription<Hazard>(
    "/mrm_in_lane_stop_auxiliary_commands/output/hazard_lights_command", 1,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Hazard) & msg) {
      EXPECT_EQ(msg->command, Hazard::ENABLE);
      ++hazards;
    });
  auto map = fixtures::makeMapBinMsg();
  auto route = fixtures::makeBehaviorNormalRoute();
  Odom odometry;
  odometry.header.frame_id = "map";
  odometry.pose.pose = route.start_pose;
  odometry.twist.twist.linear.x = 2.0;
  Trigger trigger;
  trigger.profile = Trigger::PROFILE_MODERATE;
  auto send = [&](auto & publisher, const auto & msg) {
    auto allocated = ALLOCATE_OUTPUT_MESSAGE_UNIQUE(publisher);
    *allocated = msg;
    publisher->publish(std::move(allocated));
  };
  auto publish = [&] {
    send(map_pub, map);
    send(route_pub, route);
    send(odom_pub, odometry);
    send(accel_pub, Accel{});
    send(objects_pub, Objects{});
    send(trigger_pub, trigger);
  };
  auto wait = [&](auto condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
      if (condition()) return true;
      publish();
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return condition();
  };
  auto exercise = [&] {
    ASSERT_TRUE(wait([&] { return trajectories > 0 && latched == 0 && gears > 0 && hazards > 0; }));
    {
      std::scoped_lock lock(mutex);
      ASSERT_GT(output.points.size(), 2U);
      EXPECT_EQ(output.header.frame_id, "map");
      EXPECT_EQ(output.points.back().longitudinal_velocity_mps, 0.0F);
      for (const auto & p : output.points) {
        EXPECT_TRUE(std::isfinite(p.pose.position.x));
        EXPECT_TRUE(std::isfinite(p.pose.position.y));
        EXPECT_TRUE(std::isfinite(p.longitudinal_velocity_mps));
      }
    }
    trigger.trigger = true;
    ASSERT_TRUE(wait([&] { return latched == 1; }));
    trigger.profile = Trigger::PROFILE_EMERGENCY;
    ASSERT_TRUE(wait([&] { return latched == 1 && profile == Trigger::PROFILE_EMERGENCY; }));
    trigger.trigger = false;
    ASSERT_TRUE(wait([&] { return latched == 0; }));
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
    executor.add_node(planner->get_agnocast_node());
    executor.add_node(auxiliary->get_agnocast_node());
    executor.add_node(peer->get_agnocast_node());
    run(executor);
  } else {
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(planner->get_rclcpp_node());
    executor.add_node(auxiliary->get_rclcpp_node());
    executor.add_node(peer->get_rclcpp_node());
    run(executor);
  }
}

int main(int argc, char ** argv)
{
  component_path = argv[1];
  --argc;
  ++argv;
  ::testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const int result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
