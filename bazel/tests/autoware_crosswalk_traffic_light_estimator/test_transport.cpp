// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "node.hpp"

#include <autoware/lanelet2_utils/conversion.hpp>

#include <gtest/gtest.h>
#include <lanelet2_core/primitives/BasicRegulatoryElements.h>

#include <chrono>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;
using Message = autoware_perception_msgs::msg::TrafficLightGroupArray;
using Element = autoware_perception_msgs::msg::TrafficLightElement;
autoware_map_msgs::msg::LaneletMapBin crossing_map()
{
  using namespace lanelet;
  auto line = [](double x0, double y0, double x1, double y1) {
    return LineString3d(
      utils::getId(), {Point3d(utils::getId(), x0, y0, 0), Point3d(utils::getId(), x1, y1, 0)});
  };
  Lanelet road(utils::getId(), line(0, 4, 20, 4), line(0, 0, 20, 0));
  road.attributes()["type"] = "lanelet";
  road.attributes()["subtype"] = "road";
  road.attributes()["turn_direction"] = "straight";
  Lanelet crosswalk(utils::getId(), line(8, -6, 8, 6), line(12, -6, 12, 6));
  crosswalk.attributes()["type"] = "lanelet";
  crosswalk.attributes()["subtype"] = "crosswalk";
  road.addRegulatoryElement(
    TrafficLight::make(100, {}, LineStringsOrPolygons3d{line(10, 5, 11, 5)}));
  crosswalk.addRegulatoryElement(
    TrafficLight::make(200, {}, LineStringsOrPolygons3d{line(13, 2, 14, 2)}));
  LaneletMapConstPtr map{utils::createMap(Lanelets{road, crosswalk}).release()};
  return ::autoware::experimental::lanelet2_utils::to_autoware_map_msgs(map);
}
TEST(CrosswalkTransport, GreenConflictProducesRedCrosswalkAndRetainsVehicle)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
    {rclcpp::Parameter("use_last_detect_color", true),
     rclcpp::Parameter("use_pedestrian_signal_detect", false),
     rclcpp::Parameter("last_detect_color_hold_time", 2.),
     rclcpp::Parameter("last_colors_hold_time", 1.)});
  auto node = std::make_shared<
    autoware::crosswalk_traffic_light_estimator::CrosswalkTrafficLightEstimatorNode>(options);
  auto peer = std::make_shared<wrapper::Node>("crosswalk_peer");
  auto map = peer->create_publisher<autoware_map_msgs::msg::LaneletMapBin>(
    "/crosswalk_traffic_light_estimator/input/vector_map", rclcpp::QoS(1).transient_local());
  auto pub = peer->create_publisher<Message>(
    "/crosswalk_traffic_light_estimator/input/classified/traffic_signals", 1);
  std::promise<Message> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->create_subscription<Message>(
    "/crosswalk_traffic_light_estimator/output/traffic_signals", rclcpp::QoS(1),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Message) & msg) {
      if (!delivered) {
        delivered = true;
        received.set_value(*msg);
      }
    });
  Message input;
  input.stamp.sec = 123;
  input.stamp.nanosec = 456;
  input.traffic_light_groups.resize(1);
  auto & group = input.traffic_light_groups[0];
  group.traffic_light_group_id = 100;
  group.elements.resize(1);
  group.elements[0].color = Element::GREEN;
  group.elements[0].shape = Element::CIRCLE;
  group.elements[0].status = Element::SOLID_ON;
  group.elements[0].confidence = .9;
  auto fixture = crossing_map();
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      map->publish(fixture);
      pub->publish(input);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    executor.cancel();
  };
  if (wrapper::use_agnocast()) {
    EXPECT_FALSE(rclcpp::ok());
    agnocast::AgnocastOnlySingleThreadedExecutor executor;
    executor.add_node(node->get_agnocast_node());
    executor.add_node(peer->get_agnocast_node());
    exchange(executor);
  } else {
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node->get_rclcpp_node());
    executor.add_node(peer->get_rclcpp_node());
    exchange(executor);
  }
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = result.get();
  EXPECT_EQ(output.stamp, input.stamp);
  ASSERT_EQ(output.traffic_light_groups.size(), 2U);
  bool found = false;
  for (const auto & out : output.traffic_light_groups) {
    if (out.traffic_light_group_id == 100)
      EXPECT_EQ(out, group);
    else {
      EXPECT_EQ(out.traffic_light_group_id, 200);
      ASSERT_FALSE(out.elements.empty());
      EXPECT_EQ(out.elements[0].color, Element::RED);
      found = true;
    }
  }
  EXPECT_TRUE(found);
}
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
