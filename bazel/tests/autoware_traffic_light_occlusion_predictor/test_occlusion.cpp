// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "node.hpp"

#include <autoware/lanelet2_utils/conversion.hpp>
#include <autoware_lanelet2_extension/regulatory_elements/autoware_traffic_light.hpp>

#include <gtest/gtest.h>
#include <lanelet2_core/LaneletMap.h>

#include <chrono>
#include <future>
#include <thread>
using Array = tier4_perception_msgs::msg::TrafficLightArray;
using Rois = tier4_perception_msgs::msg::TrafficLightRoiArray;
using Element = tier4_perception_msgs::msg::TrafficLightElement;
using Roi = tier4_perception_msgs::msg::TrafficLightRoi;
sensor_msgs::msg::CameraInfo camera()
{
  sensor_msgs::msg::CameraInfo info;
  info.header.frame_id = "map";
  info.header.stamp.sec = 123;
  info.width = 640;
  info.height = 480;
  info.k = {400, 0, 320, 0, 400, 240, 0, 0, 1};
  info.p = {400, 0, 320, 0, 0, 400, 240, 0, 0, 0, 1, 0};
  info.r = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  info.distortion_model = "plumb_bob";
  info.d = {0, 0, 0, 0, 0};
  return info;
}
sensor_msgs::msg::PointCloud2 cloud(float depth)
{
  pcl::PointCloud<pcl::PointXYZ> points;
  for (int x = -25; x <= 25; ++x)
    for (int y = -25; y <= 25; ++y) points.emplace_back(x * .02, y * .02, depth);
  sensor_msgs::msg::PointCloud2 out;
  pcl::toROSMsg(points, out);
  out.header = camera().header;
  return out;
}
Roi roi(int id, int x, bool pedestrian = false)
{
  Roi out;
  out.traffic_light_id = id;
  out.traffic_light_type = pedestrian ? Roi::PEDESTRIAN_TRAFFIC_LIGHT : Roi::CAR_TRAFFIC_LIGHT;
  out.roi.x_offset = x;
  out.roi.y_offset = 220;
  out.roi.width = out.roi.height = 40;
  return out;
}
TEST(OcclusionGeometry, ForegroundBackgroundAndStateReset)
{
  auto node = std::make_shared<rclcpp::Node>("occlusion_geometry");
  tf2_ros::Buffer buffer(node->get_clock());
  autoware::traffic_light::CloudOcclusionPredictor predictor(node.get(), 50, .15, .08);
  auto info = std::make_shared<sensor_msgs::msg::CameraInfo>(camera());
  auto regions = std::make_shared<Rois>();
  regions->rois.push_back(roi(10, 300));
  for (float depth : {10.f, 35.f, 10.f}) {
    std::vector<int> ratios;
    predictor.predict(
      info, regions, std::make_shared<sensor_msgs::msg::PointCloud2>(cloud(depth)), buffer,
      {{10, tf2::Vector3(0, 0, 20)}}, ratios);
    ASSERT_EQ(ratios.size(), 1U);
    EXPECT_EQ(ratios[0], depth == 10 ? 100 : 0);
  }
}
TEST(OcclusionGeometry, MaximumDistanceExcludesForeground)
{
  auto node = std::make_shared<rclcpp::Node>("occlusion_range");
  tf2_ros::Buffer buffer(node->get_clock());
  autoware::traffic_light::CloudOcclusionPredictor predictor(node.get(), 5, .15, .08);
  auto regions = std::make_shared<Rois>();
  regions->rois.push_back(roi(10, 300));
  std::vector<int> ratios;
  predictor.predict(
    std::make_shared<sensor_msgs::msg::CameraInfo>(camera()), regions,
    std::make_shared<sensor_msgs::msg::PointCloud2>(cloud(10)), buffer,
    {{10, tf2::Vector3(0, 0, 20)}}, ratios);
  ASSERT_EQ(ratios.size(), 1U);
  EXPECT_EQ(ratios[0], 0);
}
autoware_map_msgs::msg::LaneletMapBin map_fixture()
{
  using namespace lanelet;
  utils::registerId(1000);
  auto line = [](double x0, double y0, double x1, double y1, double z) {
    return LineString3d(
      utils::getId(), {Point3d(utils::getId(), x0, y0, z), Point3d(utils::getId(), x1, y1, z)});
  };
  Lanelet road(utils::getId(), line(0, 2, 30, 2, 0), line(0, -2, 30, -2, 0));
  road.attributes()["subtype"] = "road";
  LineString3d car(10, {Point3d(utils::getId(), -.5, 0, 20), Point3d(utils::getId(), .5, 0, 20)});
  car.attributes()["height"] = "0";
  LineString3d ped(20, {Point3d(utils::getId(), 4.5, 0, 20), Point3d(utils::getId(), 5.5, 0, 20)});
  ped.attributes()["height"] = "0";
  road.addRegulatoryElement(
    lanelet::autoware::AutowareTrafficLight::make(1000, {}, LineStringsOrPolygons3d{car, ped}));
  LaneletMapConstPtr map{utils::createMap(Lanelets{road}).release()};
  return ::autoware::experimental::lanelet2_utils::to_autoware_map_msgs(map);
}
TEST(OcclusionTransport, PairsBothCategoriesAndMasksOnlyOccludedSignal)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
    {rclcpp::Parameter("azimuth_occlusion_resolution_deg", .15),
     rclcpp::Parameter("elevation_occlusion_resolution_deg", .08),
     rclcpp::Parameter("max_valid_pt_dist", 50.), rclcpp::Parameter("max_image_cloud_delay", .5),
     rclcpp::Parameter("max_wait_t", .05), rclcpp::Parameter("max_occlusion_ratio", 50)});
  auto node =
    std::make_shared<autoware::traffic_light::TrafficLightOcclusionPredictorNode>(options);
  auto peer = std::make_shared<rclcpp::Node>("occlusion_peer");
  const std::string prefix = "/traffic_light_occlusion_predictor_node/input/";
  auto map = peer->create_publisher<autoware_map_msgs::msg::LaneletMapBin>(
    prefix + "vector_map", rclcpp::QoS(1).transient_local());
  auto car = peer->create_publisher<Array>(prefix + "car/traffic_signals", rclcpp::SensorDataQoS());
  auto ped =
    peer->create_publisher<Array>(prefix + "pedestrian/traffic_signals", rclcpp::SensorDataQoS());
  auto regions = peer->create_publisher<Rois>(prefix + "rois", rclcpp::SensorDataQoS());
  auto info = peer->create_publisher<sensor_msgs::msg::CameraInfo>(
    prefix + "camera_info", rclcpp::SensorDataQoS());
  auto points = peer->create_publisher<sensor_msgs::msg::PointCloud2>(
    prefix + "cloud", rclcpp::SensorDataQoS());
  std::promise<Array> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->create_subscription<Array>(
    "/traffic_light_occlusion_predictor_node/output/traffic_signals", rclcpp::QoS(1),
    [&](Array::ConstSharedPtr msg) {
      if (!delivered) {
        delivered = true;
        received.set_value(*msg);
      }
    });
  Array a;
  a.header = camera().header;
  a.signals.resize(1);
  a.signals[0].traffic_light_id = 10;
  a.signals[0].traffic_light_type = Roi::CAR_TRAFFIC_LIGHT;
  a.signals[0].elements.resize(1);
  a.signals[0].elements[0].color = Element::GREEN;
  a.signals[0].elements[0].shape = Element::CIRCLE;
  a.signals[0].elements[0].status = Element::SOLID_ON;
  a.signals[0].elements[0].confidence = .9;
  Array b = a;
  b.signals[0].traffic_light_id = 20;
  b.signals[0].traffic_light_type = Roi::PEDESTRIAN_TRAFFIC_LIGHT;
  Rois rois;
  rois.header = a.header;
  rois.rois = {roi(10, 300), roi(20, 400, true)};
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(peer);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while ((car->get_subscription_count() == 0 || ped->get_subscription_count() == 0 ||
          regions->get_subscription_count() < 2 || info->get_subscription_count() < 2 ||
          points->get_subscription_count() < 2 || map->get_subscription_count() == 0 ||
          sub->get_publisher_count() == 0) &&
         std::chrono::steady_clock::now() < deadline) {
    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  map->publish(map_fixture());
  const auto settle = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
  while (std::chrono::steady_clock::now() < settle) {
    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  regions->publish(rois);
  info->publish(camera());
  points->publish(cloud(10));
  car->publish(a);
  ped->publish(b);
  while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
         std::chrono::steady_clock::now() < deadline) {
    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = result.get();
  EXPECT_EQ(output.header, a.header);
  ASSERT_EQ(output.signals.size(), 2U);
  bool found_car = false, found_ped = false;
  for (const auto & signal : output.signals) {
    if (signal.traffic_light_id == 10) {
      ASSERT_EQ(signal.elements.size(), 1U);
      EXPECT_EQ(signal.elements[0].color, Element::UNKNOWN);
      EXPECT_EQ(signal.elements[0].confidence, 0);
      found_car = true;
    } else {
      EXPECT_EQ(signal, b.signals[0]);
      found_ped = true;
    }
  }
  EXPECT_TRUE(found_car);
  EXPECT_TRUE(found_ped);
}
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
