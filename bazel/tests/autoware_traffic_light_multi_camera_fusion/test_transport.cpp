// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "traffic_light_multi_camera_fusion_node.hpp"

#include <autoware/lanelet2_utils/conversion.hpp>
#include <autoware_lanelet2_extension/regulatory_elements/autoware_traffic_light.hpp>

#include <gtest/gtest.h>
#include <lanelet2_core/LaneletMap.h>

#include <chrono>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;
using Camera = sensor_msgs::msg::CameraInfo;
using Rois = tier4_perception_msgs::msg::TrafficLightRoiArray;
using Signals = tier4_perception_msgs::msg::TrafficLightArray;
using Output = autoware_perception_msgs::msg::TrafficLightGroupArray;
using Diagnostics = diagnostic_msgs::msg::DiagnosticArray;
using Element = tier4_perception_msgs::msg::TrafficLightElement;
autoware_map_msgs::msg::LaneletMapBin fusion_map()
{
  using namespace lanelet;
  utils::registerId(1000);
  auto line = [](double x0, double y0, double x1, double y1) {
    return LineString3d(
      utils::getId(), {Point3d(utils::getId(), x0, y0, 0), Point3d(utils::getId(), x1, y1, 0)});
  };
  Lanelet road(utils::getId(), line(0, 2, 30, 2), line(0, -2, 30, -2));
  road.attributes()["subtype"] = "road";
  LineString3d a(10, {Point3d(utils::getId(), 20, .5, 3.5), Point3d(utils::getId(), 20, -.5, 3.5)});
  a.attributes()["height"] = "1";
  a.attributes()["subtype"] = "red_yellow_green";
  LineString3d b(20, {Point3d(utils::getId(), 21, .5, 3.5), Point3d(utils::getId(), 21, -.5, 3.5)});
  b.attributes() = a.attributes();
  road.addRegulatoryElement(
    lanelet::autoware::AutowareTrafficLight::make(1000, {}, LineStringsOrPolygons3d{a, b}));
  LaneletMapConstPtr map{utils::createMap(Lanelets{road}).release()};
  return ::autoware::experimental::lanelet2_utils::to_autoware_map_msgs(map);
}
void check_fusion(bool approximate)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
    {rclcpp::Parameter("camera_namespaces", std::vector<std::string>{"camera0", "camera1"}),
     rclcpp::Parameter("approximate_sync", approximate), rclcpp::Parameter("message_lifespan", 1.),
     rclcpp::Parameter("prior_log_odds", 0.),
     rclcpp::Parameter("signal_consistency_check.enable", true),
     rclcpp::Parameter("signal_consistency_check.publish_partial_matched_signal", false),
     rclcpp::Parameter("map_based_signal_filter.enable", false)});
  auto node = std::make_shared<autoware::traffic_light::MultiCameraFusionNode>(options);
  auto peer = std::make_shared<wrapper::Node>("fusion_peer");
  auto map = peer->create_publisher<autoware_map_msgs::msg::LaneletMapBin>(
    "/traffic_light_multi_camera_fusion/input/vector_map", rclcpp::QoS(1).transient_local());
  decltype(peer->create_publisher<Camera>("", rclcpp::SensorDataQoS())) camera[2];
  decltype(peer->create_publisher<Rois>("", rclcpp::QoS(1))) rois[2];
  decltype(peer->create_publisher<Signals>("", rclcpp::QoS(1))) signals[2];
  for (int i = 0; i < 2; ++i) {
    const auto prefix = "/camera" + std::to_string(i);
    camera[i] = peer->create_publisher<Camera>(prefix + "/camera_info", rclcpp::SensorDataQoS());
    rois[i] = peer->create_publisher<Rois>(prefix + "/detection/rois", 1);
    signals[i] = peer->create_publisher<Signals>(prefix + "/classification/traffic_signals", 1);
  }
  std::promise<Output> out;
  auto output = out.get_future();
  bool got_output = false;
  std::promise<Diagnostics> diag;
  auto diagnostics = diag.get_future();
  bool got_diag = false;
  auto sub = peer->create_subscription<Output>(
    "/traffic_light_multi_camera_fusion/output/traffic_signals", rclcpp::QoS(1),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Output) & msg) {
      if (
        !got_output && msg->traffic_light_groups.size() == 1 &&
        msg->traffic_light_groups[0].elements.size() == 1 &&
        msg->traffic_light_groups[0].elements[0].color ==
          autoware_perception_msgs::msg::TrafficLightElement::UNKNOWN) {
        got_output = true;
        out.set_value(*msg);
      }
    });
  auto ds = peer->create_subscription<Diagnostics>(
    "/diagnostics", rclcpp::QoS(10),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Diagnostics) & msg) {
      if (!got_diag)
        for (const auto & status : msg->status)
          if (status.level == diagnostic_msgs::msg::DiagnosticStatus::WARN)
            for (const auto & kv : status.values)
              if (kv.key == "1000") {
                got_diag = true;
                diag.set_value(*msg);
                return;
              }
    });
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    map->publish(fusion_map());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    int frame = 0;
    while ((output.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready ||
            diagnostics.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) &&
           std::chrono::steady_clock::now() < deadline) {
      for (int i = 0; i < 2; ++i) {
        Camera info;
        info.header.frame_id = "camera" + std::to_string(i);
        info.header.stamp.sec = 123 + frame / 20;
        info.header.stamp.nanosec = (frame % 20) * 50000000;
        info.width = 1920;
        info.height = 1080;
        Rois regions;
        regions.header = info.header;
        if (approximate) regions.header.stamp.nanosec += 1000000;
        regions.rois.resize(1);
        regions.rois[0].traffic_light_id = i ? 20 : 10;
        regions.rois[0].traffic_light_type = 0;
        regions.rois[0].roi.x_offset = regions.rois[0].roi.y_offset = 100;
        regions.rois[0].roi.width = regions.rois[0].roi.height = 50;
        Signals observations;
        observations.header = info.header;
        if (approximate) observations.header.stamp.nanosec += 2000000;
        observations.signals.resize(1);
        observations.signals[0].traffic_light_id = i ? 20 : 10;
        observations.signals[0].elements.resize(1);
        auto & element = observations.signals[0].elements[0];
        element.color = i ? Element::GREEN : Element::RED;
        element.shape = Element::CIRCLE;
        element.status = Element::SOLID_ON;
        element.confidence = .9;
        camera[i]->publish(info);
        rois[i]->publish(regions);
        signals[i]->publish(observations);
      }
      ++frame;
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
  ASSERT_EQ(output.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  ASSERT_EQ(diagnostics.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto result = output.get();
  EXPECT_GE(result.stamp.sec, 123);
  EXPECT_EQ(result.stamp.nanosec % 50000000, 0U);
  ASSERT_EQ(result.traffic_light_groups.size(), 1U);
  EXPECT_EQ(result.traffic_light_groups[0].traffic_light_group_id, 1000);
  EXPECT_EQ(
    result.traffic_light_groups[0].elements[0].shape,
    autoware_perception_msgs::msg::TrafficLightElement::UNKNOWN);
  bool conflict = false;
  for (const auto & status : diagnostics.get().status)
    for (const auto & kv : status.values)
      if (kv.key == "1000") {
        EXPECT_EQ(
          kv.value,
          std::to_string(static_cast<int>(autoware::traffic_light::ConflictType::CONFLICT)));
        conflict = true;
      }
  EXPECT_TRUE(conflict);
}
TEST(FusionTransport, ExactSynchronizationPublishesConflictAndDiagnostics)
{
  check_fusion(false);
}
TEST(FusionTransport, ApproximateSynchronizationPublishesConflictAndDiagnostics)
{
  check_fusion(true);
}
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
