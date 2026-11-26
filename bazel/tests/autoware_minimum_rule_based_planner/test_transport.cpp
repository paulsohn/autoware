// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware/agnocast_wrapper/node.hpp>
#include <autoware_test_utils/autoware_test_utils.hpp>
#include <class_loader/class_loader.hpp>
#include <plugin_interface.hpp>
#include <pluginlib/class_loader.hpp>
#include <rclcpp_components/node_factory.hpp>

#include <autoware_internal_planning_msgs/msg/candidate_trajectories.hpp>
#include <autoware_perception_msgs/msg/predicted_objects.hpp>
#include <geometry_msgs/msg/accel_with_covariance_stamped.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

namespace wrapper = autoware::agnocast_wrapper;
std::string component_path;

TEST(MinimumRulePlanner, CandidateIdentityStopAndRecovery)
{
  namespace fixtures = autoware::test_utils;
  rclcpp::NodeOptions options;
  const auto share =
    ament_index_cpp::get_package_share_directory("autoware_minimum_rule_based_planner");
  const auto test_share = ament_index_cpp::get_package_share_directory("autoware_test_utils");
  const auto core_share = ament_index_cpp::get_package_share_directory("autoware_core_planning");
  fixtures::updateNodeOptions(
    options, {test_share + "/config/test_vehicle_info.param.yaml",
              test_share + "/config/test_nearest_search.param.yaml",
              core_share + "/config/common.param.yaml",
              share + "/config/minimum_rule_based_planner.param.yaml",
              share + "/config/trajectory_optimizer_plugins/elastic_band_smoother.param.yaml",
              share + "/config/velocity_smoother/jerk_filtered_smoother.param.yaml"});
  auto loader = std::make_shared<class_loader::ClassLoader>(component_path);
  auto factory = loader->createInstance<rclcpp_components::NodeFactory>(
    "rclcpp_components::NodeFactoryTemplate<autoware::minimum_rule_based_planner::"
    "MinimumRuleBasedPlannerNode>");
  auto instance = factory->create_node_instance(options);
  auto planner = std::static_pointer_cast<wrapper::Node>(instance.get_node_instance());
  auto peer = std::make_shared<wrapper::Node>("minimum_rule_planner_peer");
  {
    pluginlib::ClassLoader<autoware::minimum_rule_based_planner::plugin::PluginInterface> plugins(
      "autoware_minimum_rule_based_planner",
      "autoware::minimum_rule_based_planner::plugin::PluginInterface");
    for (const auto * name : {"ObstacleStop", "SurroundObstacleStop"}) {
      auto plugin = plugins.createSharedInstance(
        std::string("autoware::minimum_rule_based_planner::plugin::") + name);
      ASSERT_NE(plugin, nullptr);
    }
  }
  using Map = autoware_map_msgs::msg::LaneletMapBin;
  using Route = autoware_planning_msgs::msg::LaneletRoute;
  using Odom = nav_msgs::msg::Odometry;
  using Accel = geometry_msgs::msg::AccelWithCovarianceStamped;
  using Objects = autoware_perception_msgs::msg::PredictedObjects;
  using Candidates = autoware_internal_planning_msgs::msg::CandidateTrajectories;
  const std::string prefix = "/minimum_rule_based_planner_node";
  auto map_pub =
    peer->create_publisher<Map>(prefix + "/input/vector_map", rclcpp::QoS(1).transient_local());
  auto route_pub =
    peer->create_publisher<Route>(prefix + "/input/route", rclcpp::QoS(1).transient_local());
  auto odom_pub = peer->create_publisher<Odom>(prefix + "/input/odometry", 1);
  auto accel_pub = peer->create_publisher<Accel>(prefix + "/input/acceleration", 1);
  auto objects_pub = peer->create_publisher<Objects>(prefix + "/input/objects", 1);
  std::mutex mutex;
  Candidates output;
  std::atomic<int> received{0}, optimizer_debug{0};
  using Trajectory = autoware_planning_msgs::msg::Trajectory;
  auto optimizer_sub = peer->create_subscription<Trajectory>(
    prefix + "/debug/optimizer/eb_smoother/trajectory", 1,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Trajectory) & msg) {
      EXPECT_FALSE(msg->points.empty());
      EXPECT_EQ(msg->header.frame_id, "map");
      ++optimizer_debug;
    });
  auto sub = peer->create_subscription<Candidates>(
    prefix + "/output/candidate_trajectories", 1,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Candidates) & msg) {
      std::scoped_lock lock(mutex);
      output = *msg;
      ++received;
    });
  auto map = fixtures::makeMapBinMsg();
  auto route = fixtures::makeBehaviorNormalRoute();
  Odom odom;
  odom.header.frame_id = "map";
  odom.pose.pose = route.start_pose;
  odom.twist.twist.linear.x = 2.0;
  Objects objects;
  objects.header.frame_id = "map";
  auto send = [&](auto & publisher, const auto & msg) {
    auto allocated = ALLOCATE_OUTPUT_MESSAGE_UNIQUE(publisher);
    *allocated = msg;
    publisher->publish(std::move(allocated));
  };
  auto wait = [&](auto condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
      if (condition()) return true;
      objects.header.stamp = peer->get_clock()->now();
      send(map_pub, map);
      send(route_pub, route);
      send(odom_pub, odom);
      send(accel_pub, Accel{});
      send(objects_pub, objects);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return condition();
  };
  auto forward_begin = [&](const auto & points) {
    return std::min_element(points.begin(), points.end(), [&](const auto & a, const auto & b) {
      return std::hypot(
               a.pose.position.x - odom.pose.pose.position.x,
               a.pose.position.y - odom.pose.pose.position.y) <
             std::hypot(
               b.pose.position.x - odom.pose.pose.position.x,
               b.pose.position.y - odom.pose.pose.position.y);
    });
  };
  auto moving = [&] {
    std::scoped_lock lock(mutex);
    if (
      output.candidate_trajectories.size() != 1 || output.candidate_trajectories[0].points.empty())
      return false;
    return forward_begin(output.candidate_trajectories[0].points)->longitudinal_velocity_mps > 0.1;
  };
  auto stopped = [&] {
    std::scoped_lock lock(mutex);
    if (
      output.candidate_trajectories.size() != 1 || output.candidate_trajectories[0].points.empty())
      return false;
    const auto & points = output.candidate_trajectories[0].points;
    return std::all_of(forward_begin(points), points.end(), [](const auto & p) {
      return std::isfinite(p.longitudinal_velocity_mps) &&
             std::abs(p.longitudinal_velocity_mps) < 0.01;
    });
  };
  auto exercise = [&] {
    ASSERT_TRUE(wait([&] { return moving() && optimizer_debug > 0; }));
    {
      std::scoped_lock lock(mutex);
      ASSERT_EQ(output.candidate_trajectories.size(), 1U);
      ASSERT_EQ(output.generator_info.size(), 1U);
      EXPECT_EQ(output.generator_info[0].generator_name.data, "MinimumRuleBasedPlanner");
      EXPECT_EQ(
        output.generator_info[0].generator_id, output.candidate_trajectories[0].generator_id);
      const auto & points = output.candidate_trajectories[0].points;
      ASSERT_GT(points.size(), 20U);
      for (const auto & p : points) {
        EXPECT_TRUE(std::isfinite(p.pose.position.x));
        EXPECT_TRUE(std::isfinite(p.pose.position.y));
        EXPECT_TRUE(std::isfinite(p.longitudinal_velocity_mps));
      }
      const auto closest =
        std::min_element(points.begin(), points.end(), [&](const auto & a, const auto & b) {
          return std::hypot(
                   a.pose.position.x - odom.pose.pose.position.x,
                   a.pose.position.y - odom.pose.pose.position.y) <
                 std::hypot(
                   b.pose.position.x - odom.pose.pose.position.x,
                   b.pose.position.y - odom.pose.pose.position.y);
        });
      const auto index =
        std::min<size_t>(std::distance(points.begin(), closest) + 16, points.size() - 1);
      autoware_perception_msgs::msg::PredictedObject obstacle;
      obstacle.object_id.uuid[0] = 42;
      obstacle.existence_probability = 1.0;
      obstacle.kinematics.initial_pose_with_covariance.pose = points[index].pose;
      obstacle.shape.type = autoware_perception_msgs::msg::Shape::BOUNDING_BOX;
      obstacle.shape.dimensions.x = obstacle.shape.dimensions.y = obstacle.shape.dimensions.z = 2.0;
      autoware_perception_msgs::msg::ObjectClassification classification;
      classification.label = classification.CAR;
      classification.probability = 1.0;
      obstacle.classification.push_back(classification);
      objects.objects.push_back(obstacle);
    }
    const auto before = received.load();
    ASSERT_TRUE(wait([&] { return received > before + 2 && stopped(); }));
    objects.objects.clear();
    ASSERT_TRUE(wait(moving));
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
    executor.add_node(peer->get_agnocast_node());
    run(executor);
  } else {
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(planner->get_rclcpp_node());
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
