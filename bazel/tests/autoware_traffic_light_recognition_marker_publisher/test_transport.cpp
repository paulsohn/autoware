// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "traffic_light_recognition_marker_publisher.hpp"

#include <gtest/gtest.h>
#include <lanelet2_core/LaneletMap.h>

#include <chrono>
#include <optional>
#include <thread>

using Node =
  autoware::traffic_light_recognition_marker_publisher::TrafficLightRecognitionMarkerPublisher;
using Signals = autoware_perception_msgs::msg::TrafficLightGroupArray;
using Element = autoware_perception_msgs::msg::TrafficLightElement;
using Markers = visualization_msgs::msg::MarkerArray;
constexpr lanelet::Id group_id = (1LL << 34) + 1000;

autoware_map_msgs::msg::LaneletMapBin marker_map(lanelet::Id id)
{
  using namespace lanelet;
  utils::registerId(group_id + 10000);
  auto line = [](double x0, double y0, double x1, double y1) {
    return LineString3d(
      utils::getId(), {Point3d(utils::getId(), x0, y0, 0), Point3d(utils::getId(), x1, y1, 0)});
  };
  Lanelet road(utils::getId(), line(0, 2, 30, 2), line(0, -2, 30, -2));
  road.attributes()["subtype"] = "road";
  LineString3d a(10, {Point3d(utils::getId(), 20, 1, 3), Point3d(utils::getId(), 20, -1, 3)});
  LineString3d b(20, {Point3d(utils::getId(), 21, 1, 4), Point3d(utils::getId(), 21, -1, 4)});
  road.addRegulatoryElement(
    lanelet::autoware::AutowareTrafficLight::make(id, {}, LineStringsOrPolygons3d{a, b}));
  LaneletMapConstPtr map{utils::createMap(Lanelets{road}).release()};
  return ::autoware::experimental::lanelet2_utils::to_autoware_map_msgs(map);
}
Signals observations(lanelet::Id id)
{
  Signals s;
  s.traffic_light_groups.resize(2);
  auto & group = s.traffic_light_groups[0];
  group.traffic_light_group_id = id;
  Element e;
  e.color = Element::RED;
  e.shape = Element::CIRCLE;
  e.confidence = .9;
  group.elements.push_back(e);
  e.color = Element::GREEN;
  e.shape = Element::LEFT_ARROW;
  group.elements.push_back(e);
  s.traffic_light_groups[1].traffic_light_group_id = 999;
  s.traffic_light_groups[1].elements.push_back(e);
  return s;
}
class MarkerTransport : public ::testing::Test
{
protected:
  std::shared_ptr<Node> node;
  std::shared_ptr<rclcpp::Node> peer;
  rclcpp::executors::SingleThreadedExecutor executor;
  rclcpp::Publisher<autoware_map_msgs::msg::LaneletMapBin>::SharedPtr map;
  rclcpp::Publisher<Signals>::SharedPtr signals;
  rclcpp::Subscription<Markers>::SharedPtr sub;
  std::optional<Markers> received;
  template <class Predicate>
  bool until(Predicate done, std::chrono::milliseconds duration = std::chrono::seconds(3))
  {
    const auto end = std::chrono::steady_clock::now() + duration;
    do {
      executor.spin_some();
      if (done()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    return done();
  }
  void SetUp() override
  {
    node = std::make_shared<Node>(rclcpp::NodeOptions{});
    peer = std::make_shared<rclcpp::Node>("marker_peer");
    const std::string prefix = "/traffic_light_recognition_marker_publisher/";
    map = peer->create_publisher<autoware_map_msgs::msg::LaneletMapBin>(
      prefix + "input/lanelet2_map", rclcpp::QoS(1).transient_local());
    signals = peer->create_publisher<Signals>(prefix + "input/traffic_signals", 1);
    sub = peer->create_subscription<Markers>(
      prefix + "output/marker", 1, [this](Markers::ConstSharedPtr msg) { received = *msg; });
    executor.add_node(node);
    executor.add_node(peer);
    ASSERT_TRUE(until([&] {
      return map->get_subscription_count() > 0 && signals->get_subscription_count() > 0 &&
             sub->get_publisher_count() > 0;
    }));
  }
  void load(lanelet::Id id)
  {
    map->publish(marker_map(id));
    until([] { return false; }, std::chrono::milliseconds(100));
  }
  void send(lanelet::Id id)
  {
    received.reset();
    signals->publish(observations(id));
    ASSERT_TRUE(until([&] { return received.has_value(); }));
  }
};
TEST_F(MarkerTransport, SignalsBeforeMapAreSuppressed)
{
  signals->publish(observations(group_id));
  EXPECT_FALSE(until([&] { return received.has_value(); }, std::chrono::milliseconds(200)));
}
TEST_F(MarkerTransport, GroupIdentityProducesTextAtEveryHousing)
{
  load(group_id);
  send(group_id);
  ASSERT_TRUE(received);
  ASSERT_EQ(received->markers.size(), 4U);
  for (size_t i = 0; i < 4; ++i) {
    const auto & m = received->markers[i];
    EXPECT_EQ(m.header.frame_id, "map");
    EXPECT_EQ(m.id, i);
    EXPECT_EQ(m.type, visualization_msgs::msg::Marker::TEXT_VIEW_FACING);
    EXPECT_EQ(m.action, visualization_msgs::msg::Marker::ADD);
    EXPECT_EQ(m.ns, "traffic_light_color");
    EXPECT_DOUBLE_EQ(m.pose.position.x, i < 2 ? 20. : 21.);
    EXPECT_DOUBLE_EQ(m.pose.position.y, 0.);
    EXPECT_DOUBLE_EQ(m.pose.position.z, (i < 2 ? 4. : 5.) + (i % 2) * .5);
    EXPECT_DOUBLE_EQ(m.scale.z, .5);
    EXPECT_EQ(m.lifetime.sec, 1);
    EXPECT_EQ(m.text, i % 2 ? "LEFT_ARROW" : "RED");
    EXPECT_FLOAT_EQ(m.color.r, 1);
    EXPECT_FLOAT_EQ(m.color.g, i % 2 ? 1 : 0);
    EXPECT_FLOAT_EQ(m.color.b, i % 2 ? 1 : 0);
    EXPECT_FLOAT_EQ(m.color.a, .999);
  }
}
TEST_F(MarkerTransport, ReplacementMapDropsOldGroupGeometry)
{
  load(group_id);
  send(group_id);
  ASSERT_TRUE(received);
  ASSERT_EQ(received->markers.size(), 4U);
  load(group_id + 1);
  send(group_id);
  ASSERT_TRUE(received);
  EXPECT_TRUE(received->markers.empty());
  send(group_id + 1);
  ASSERT_TRUE(received);
  EXPECT_EQ(received->markers.size(), 4U);
}
int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
