// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "traffic_light_map_visualizer/traffic_light_visualizer_node.hpp"

#include <autoware/lanelet2_utils/conversion.hpp>
#include <autoware_lanelet2_extension/regulatory_elements/autoware_traffic_light.hpp>

#include <gtest/gtest.h>
#include <lanelet2_core/LaneletMap.h>

#include <chrono>
#include <future>
#include <thread>

namespace wrapper = autoware::agnocast_wrapper;
using Signals = autoware_perception_msgs::msg::TrafficLightGroupArray;
using Element = autoware_perception_msgs::msg::TrafficLightElement;
using Markers = visualization_msgs::msg::MarkerArray;
autoware_map_msgs::msg::LaneletMapBin bulb_map()
{
  using namespace lanelet;
  utils::registerId(1000);
  auto line = [](double y) {
    return LineString3d(
      utils::getId(), {Point3d(utils::getId(), 0, y, 0), Point3d(utils::getId(), 30, y, 0)});
  };
  Lanelet road(utils::getId(), line(2), line(-2));
  road.attributes()["subtype"] = "road";
  LineString3d housing(10, {Point3d(utils::getId(), 20, 0, 4), Point3d(utils::getId(), 23, 0, 4)});
  Point3d red(11, 20, 0, 5), green(12, 21, 0, 5), amber(13, 22, 0, 5);
  red.attributes()["color"] = "red";
  green.attributes()["color"] = "green";
  amber.attributes()["color"] = "yellow";
  LineString3d bulbs(utils::getId(), {red, green, amber});
  bulbs.attributes()["traffic_light_id"] = "10";
  road.addRegulatoryElement(lanelet::autoware::AutowareTrafficLight::make(
    1000, {}, LineStringsOrPolygons3d{housing}, {}, {bulbs}));
  LaneletMapConstPtr map{utils::createMap(Lanelets{road}).release()};
  return ::autoware::experimental::lanelet2_utils::to_autoware_map_msgs(map);
}
TEST(MapVisualizerTransport, OnlyObservedBulbsProduceColoredSpheres)
{
  auto node =
    std::make_shared<autoware::traffic_light::TrafficLightMapVisualizerNode>(rclcpp::NodeOptions{});
  auto peer = std::make_shared<wrapper::Node>("map_visualizer_peer");
  const std::string prefix = "/traffic_light_map_visualizer_node/";
  auto map = peer->create_publisher<autoware_map_msgs::msg::LaneletMapBin>(
    prefix + "input/vector_map", rclcpp::QoS(1).transient_local());
  auto signals = peer->create_publisher<Signals>(prefix + "input/tl_state", 1);
  std::promise<Markers> promise;
  auto result = promise.get_future();
  bool received = false;
  auto sub = peer->create_subscription<Markers>(
    prefix + "output/traffic_light", 1,
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Markers) & msg) {
      if (!received && msg->markers.size() == 2) {
        received = true;
        promise.set_value(*msg);
      }
    });
  Signals observations;
  observations.traffic_light_groups.resize(2);
  observations.traffic_light_groups[0].traffic_light_group_id = 1000;
  for (auto color : {Element::RED, Element::GREEN}) {
    Element e;
    e.color = color;
    e.shape = Element::CIRCLE;
    e.confidence = .9;
    observations.traffic_light_groups[0].elements.push_back(e);
  }
  observations.traffic_light_groups[1] = observations.traffic_light_groups[0];
  observations.traffic_light_groups[1].traffic_light_group_id = 999;
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    map->publish(bulb_map());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      signals->publish(observations);
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
  const auto markers = result.get();
  ASSERT_EQ(markers.markers.size(), 2U);
  bool red = false, green = false;
  for (const auto & m : markers.markers) {
    EXPECT_EQ(m.header.frame_id, "map");
    EXPECT_GT(m.header.stamp.sec, 0);
    EXPECT_EQ(m.type, visualization_msgs::msg::Marker::SPHERE);
    EXPECT_EQ(m.ns, "traffic_light");
    EXPECT_TRUE(m.frame_locked);
    EXPECT_EQ(m.lifetime.nanosec, 200000000U);
    EXPECT_NEAR(m.scale.x, .3, 1e-6);
    EXPECT_DOUBLE_EQ(m.pose.orientation.w, 1.);
    EXPECT_DOUBLE_EQ(m.pose.position.y, 0.);
    EXPECT_DOUBLE_EQ(m.pose.position.z, 5.);
    EXPECT_FLOAT_EQ(m.color.b, 0.);
    EXPECT_FLOAT_EQ(m.color.a, .999);
    if (m.id == 11) {
      red = true;
      EXPECT_DOUBLE_EQ(m.pose.position.x, 20.);
      EXPECT_FLOAT_EQ(m.color.r, 1.);
      EXPECT_FLOAT_EQ(m.color.g, 0.);
    } else if (m.id == 12) {
      green = true;
      EXPECT_DOUBLE_EQ(m.pose.position.x, 21.);
      EXPECT_FLOAT_EQ(m.color.r, 0.);
      EXPECT_FLOAT_EQ(m.color.g, 1.);
    } else
      ADD_FAILURE() << "Unexpected bulb " << m.id;
  }
  EXPECT_TRUE(red);
  EXPECT_TRUE(green);
}
int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const int result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
