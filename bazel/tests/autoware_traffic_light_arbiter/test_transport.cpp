// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/lanelet2_utils/conversion.hpp>
#include <autoware/traffic_light_arbiter/traffic_light_arbiter_node.hpp>

#include <gtest/gtest.h>
#include <lanelet2_core/LaneletMap.h>
#include <lanelet2_core/primitives/BasicRegulatoryElements.h>

#include <chrono>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;
using Message = autoware_perception_msgs::msg::TrafficLightGroupArray;
using Element = autoware_perception_msgs::msg::TrafficLightElement;
autoware_map_msgs::msg::LaneletMapBin map_fixture()
{
  using namespace lanelet;
  LineString3d left(
    utils::getId(), {Point3d(utils::getId(), 0, 0), Point3d(utils::getId(), 10, 0)});
  LineString3d right(
    utils::getId(), {Point3d(utils::getId(), 0, 2), Point3d(utils::getId(), 10, 2)});
  Lanelet road(utils::getId(), left, right);
  road.attributes()[AttributeName::Subtype] = AttributeValueString::Road;
  road.attributes()[AttributeName::Type] = AttributeValueString::Lanelet;
  LineString3d bulb(
    utils::getId(), {Point3d(utils::getId(), 5, 0, 5), Point3d(utils::getId(), 6, 0, 5)});
  bulb.attributes()[AttributeName::Type] = AttributeValueString::TrafficLight;
  road.addRegulatoryElement(TrafficLight::make(1001, {}, LineStringsOrPolygons3d{bulb}));
  LaneletMapConstPtr map{utils::createMap(Lanelets{road}).release()};
  return ::autoware::experimental::lanelet2_utils::to_autoware_map_msgs(map);
}
void check_arbiter(bool matching)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
    {rclcpp::Parameter("external_delay_tolerance", 5.),
     rclcpp::Parameter("external_time_tolerance", 5.),
     rclcpp::Parameter("perception_time_tolerance", 1.),
     rclcpp::Parameter("source_priority", "external"),
     rclcpp::Parameter("enable_signal_matching", matching)});
  auto node = std::make_shared<autoware::traffic_light::TrafficLightArbiterNode>(options);
  auto peer = std::make_shared<wrapper::Node>("arbiter_peer");
  auto map = peer->create_publisher<autoware_map_msgs::msg::LaneletMapBin>(
    "/traffic_light_arbiter/sub/vector_map", rclcpp::QoS(1).transient_local());
  auto perception =
    peer->create_publisher<Message>("/traffic_light_arbiter/sub/perception_traffic_signals", 1);
  auto external =
    peer->create_publisher<Message>("/traffic_light_arbiter/sub/external_traffic_signals", 1);
  std::promise<Message> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->create_subscription<Message>(
    "/traffic_light_arbiter/pub/traffic_signals", rclcpp::QoS(1),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Message) & msg) {
      if (
        !delivered && msg->traffic_light_groups.size() == 1 &&
        msg->traffic_light_groups[0].elements.size() == 1 &&
        msg->traffic_light_groups[0].elements[0].color ==
          (matching ? Element::UNKNOWN : Element::RED)) {
        delivered = true;
        received.set_value(*msg);
      }
    });
  Message a;
  a.stamp = node->now();
  a.traffic_light_groups.resize(1);
  a.traffic_light_groups[0].traffic_light_group_id = 1001;
  a.traffic_light_groups[0].elements.resize(1);
  auto & e = a.traffic_light_groups[0].elements[0];
  e.color = Element::GREEN;
  e.shape = Element::CIRCLE;
  e.status = Element::SOLID_ON;
  e.confidence = .9;
  Message b = a;
  b.traffic_light_groups[0].elements[0].color = Element::RED;
  b.traffic_light_groups[0].elements[0].confidence = .1;
  auto fixture = map_fixture();
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      map->publish(fixture);
      perception->publish(a);
      external->publish(b);
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
  EXPECT_EQ(output.stamp, b.stamp);
  ASSERT_EQ(output.traffic_light_groups.size(), 1U);
  EXPECT_EQ(output.traffic_light_groups[0].traffic_light_group_id, 1001);
  if (!matching) EXPECT_EQ(output.traffic_light_groups[0], b.traffic_light_groups[0]);
}
TEST(ArbiterTransport, ExternalPriorityBeatsConfidence)
{
  check_arbiter(false);
}
TEST(ArbiterTransport, SignalMismatchProducesUnknown)
{
  check_arbiter(true);
}
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
