// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/lanelet2_utils/conversion.hpp>
#include <main.hpp>
#include <ros_interface.hpp>

#include <gtest/gtest.h>
#include <lanelet2_core/LaneletMap.h>
#include <lanelet2_core/utility/Utilities.h>

#include <chrono>
#include <future>
#include <thread>
namespace path = autoware::path_distance_calculator;
namespace wrapper = autoware::agnocast_wrapper;
using Map = autoware_map_msgs::msg::LaneletMapBin;
using Route = autoware_planning_msgs::msg::LaneletRoute;
struct RouteFixture
{
  Map map;
  Route route;
  RouteFixture()
  {
    auto lm = std::make_shared<lanelet::LaneletMap>();
    using lanelet::utils::getId;
    for (int i = 0; i < 3; ++i) {
      lanelet::LineString3d left(
        getId(),
        {lanelet::Point3d(getId(), 10 * i, 2, 0), lanelet::Point3d(getId(), 10 * i + 10, 2, 0)});
      lanelet::LineString3d right(
        getId(),
        {lanelet::Point3d(getId(), 10 * i, -2, 0), lanelet::Point3d(getId(), 10 * i + 10, -2, 0)});
      lanelet::Lanelet lane(getId(), left, right);
      lane.attributes()["subtype"] = "road";
      lm->add(lane);
      autoware_planning_msgs::msg::LaneletSegment s;
      s.preferred_primitive.id = lane.id();
      route.segments.push_back(s);
    }
    map = autoware::experimental::lanelet2_utils::to_autoware_map_msgs(lm);
    route.goal_pose.position.x = 27;
    route.goal_pose.orientation.w = 1;
  }
  geometry_msgs::msg::Pose pose(double x)
  {
    geometry_msgs::msg::Pose p;
    p.position.x = x;
    p.orientation.w = 1;
    return p;
  }
};
TEST(PathDistance, PlannedSequenceAndClamping)
{
  RouteFixture f;
  path::RouteDistanceCalculator c;
  EXPECT_FALSE(c.calculate_remaining_distance(f.pose(2)));
  c.set_map(f.map);
  c.set_route(f.route);
  ASSERT_TRUE(c.calculate_remaining_distance(f.pose(2)));
  EXPECT_NEAR(*c.calculate_remaining_distance(f.pose(2)), 25., 1e-6);
  EXPECT_NEAR(*c.calculate_remaining_distance(f.pose(12)), 15., 1e-6);
  EXPECT_NEAR(*c.calculate_remaining_distance(f.pose(29)), 0., 1e-6);
}
TEST(PathDistance, RouteReadinessAndInvalidation)
{
  RouteFixture f;
  path::RouteDistanceCalculator c;
  c.set_route(f.route);
  c.set_map(f.map);
  EXPECT_FALSE(c.calculate_remaining_distance(f.pose(2)));
  c.set_route(f.route);
  ASSERT_TRUE(c.calculate_remaining_distance(f.pose(2)));
  auto invalid = f.route;
  invalid.segments.front().preferred_primitive.id = -999;
  c.set_route(invalid);
  EXPECT_FALSE(c.calculate_remaining_distance(f.pose(2)));
  c.set_route(Route{});
  EXPECT_FALSE(c.calculate_remaining_distance(f.pose(2)));
  c.set_route(f.route);
  EXPECT_TRUE(c.calculate_remaining_distance(f.pose(2)));
}
TEST(PathDistance, TimerPublishesMappedRemainingDistance)
{
  RouteFixture f;
  auto peer = std::make_shared<wrapper::Node>("path_peer");
  auto node = std::make_shared<path::PathDistanceCalculator>(rclcpp::NodeOptions{});
  wrapper::TransformBroadcaster tf(*peer);
  geometry_msgs::msg::TransformStamped transform;
  transform.header.frame_id = "map";
  transform.child_frame_id = "base_link";
  transform.header.stamp.sec = 123;
  transform.transform.translation.x = 2;
  transform.transform.rotation.w = 1;
  auto map = peer->create_publisher<Map>(
    "/path_distance_calculator/input/map", rclcpp::QoS(1).transient_local());
  auto route = peer->create_publisher<Route>(
    "/path_distance_calculator/input/route", rclcpp::QoS(1).transient_local());
  using Value = autoware_internal_debug_msgs::msg::Float64Stamped;
  std::promise<Value> promise;
  auto result = promise.get_future();
  bool done = false;
  auto sub = peer->create_subscription<Value>(
    "/path_distance_calculator/output/distance", 10,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Value) & m) {
      if (!done) {
        done = true;
        promise.set_value(*m);
      }
    });
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < end) {
      tf.sendTransform(transform);
      auto m = ALLOCATE_OUTPUT_MESSAGE_UNIQUE(map);
      *m = f.map;
      map->publish(std::move(m));
      auto r = ALLOCATE_OUTPUT_MESSAGE_UNIQUE(route);
      *r = f.route;
      route->publish(std::move(r));
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    executor.cancel();
  };
  if (wrapper::use_agnocast()) {
    EXPECT_FALSE(rclcpp::ok());
    agnocast::AgnocastOnlySingleThreadedExecutor e;
    e.add_node(node->get_agnocast_node());
    e.add_node(peer->get_agnocast_node());
    exchange(e);
  } else {
    rclcpp::executors::SingleThreadedExecutor e;
    e.add_node(node->get_rclcpp_node());
    e.add_node(peer->get_rclcpp_node());
    exchange(e);
  }
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  auto output = result.get();
  EXPECT_NEAR(output.data, 25., 1e-6);
  EXPECT_EQ(output.stamp.sec, 123);
}
int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  int r = RUN_ALL_TESTS();
  wrapper::shutdown();
  return r;
}
