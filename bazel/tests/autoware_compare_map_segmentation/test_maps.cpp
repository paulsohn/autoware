// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "compare_elevation_map_filter/node.hpp"
#include "distance_based_compare_map_filter/node.hpp"
#include "lanelet_elevation_filter/node.hpp"
#include "voxel_based_approximate_compare_map_filter/node.hpp"
#include "voxel_based_compare_map_filter/node.hpp"
#include "voxel_distance_based_compare_map_filter/node.hpp"

#include <autoware/lanelet2_utils/conversion.hpp>
#include <autoware/point_types/types.hpp>
#include <grid_map_ros/GridMapRosConverter.hpp>
#include <point_cloud_msg_wrapper/point_cloud_msg_wrapper.hpp>

#include <sensor_msgs/point_cloud2_iterator.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <thread>
namespace cm = autoware::compare_map_segmentation;
using Cloud = sensor_msgs::msg::PointCloud2;
using Point = autoware::point_types::PointXYZIRC;
Cloud cloud(std::initializer_list<std::array<float, 3>> xyz)
{
  Cloud msg;
  point_cloud_msg_wrapper::PointCloud2Modifier<Point, autoware::point_types::PointXYZIRCGenerator>
    m(msg, "map");
  m.resize(xyz.size());
  size_t i = 0;
  for (auto p : xyz) {
    m[i].x = p[0];
    m[i].y = p[1];
    m[i].z = p[2];
    m[i].intensity = static_cast<uint8_t>(i + 1);
    ++i;
  }
  msg.is_dense = true;
  msg.header.stamp.sec = 123;
  msg.header.stamp.nanosec = 456;
  return msg;
}
class MapFilters : public testing::TestWithParam<int>
{
};
TEST_P(MapFilters, FiltersNonemptyCloudAgainstDeclaredMap)
{
  const bool dynamic = GetParam() >= 6;
  const int mode = dynamic ? GetParam() - 6 : GetParam();
  rclcpp::NodeOptions options;
  if (mode < 4) {
    const char * params[] = {
      "DISTANCE_PARAMS", "APPROX_PARAMS", "VOXEL_PARAMS", "VOXEL_DISTANCE_PARAMS"};
    options.arguments({"--ros-args", "--params-file", std::getenv(params[mode])});
  }
  options.append_parameter_override("use_dynamic_map_loading", dynamic);
  options.append_parameter_override("input_frame", "map");
  options.append_parameter_override("map_layer_name", "elevation");
  options.append_parameter_override("map_frame", "map");
  options.append_parameter_override("height_diff_thresh", .15);
  options.append_parameter_override("height_threshold", .5);
  options.append_parameter_override("require_map_coverage", true);
  options.append_parameter_override("extension_count", 2);
  options.append_parameter_override(
    "cache_directory", std::string(std::getenv("TEST_TMPDIR")) + "/lanelet");
  auto peer = std::make_shared<rclcpp::Node>("compare_map_peer");
  using Service = autoware_map_msgs::srv::GetDifferentialPointCloudMap;
  std::atomic<int> requests{0};
  auto service = peer->create_service<Service>(
    "/map_loader_service",
    [&](Service::Request::SharedPtr request, Service::Response::SharedPtr response) {
      ++requests;
      response->header.frame_id = "map";
      if (request->cached_ids.empty()) {
        autoware_map_msgs::msg::PointCloudMapCellWithID cell;
        cell.cell_id = "fixture";
        cell.pointcloud = cloud({{0, 0, 0}, {1, 1, 0}});
        cell.metadata.min_x = -5;
        cell.metadata.min_y = -5;
        cell.metadata.max_x = 5;
        cell.metadata.max_y = 5;
        response->new_pointcloud_with_ids.push_back(cell);
      }
    });
  auto odom = peer->create_publisher<nav_msgs::msg::Odometry>("/kinematic_state", 1);
  std::shared_ptr<rclcpp::Node> node;
  switch (mode) {
    case 0:
      node = std::make_shared<cm::DistanceBasedCompareMapFilterComponent>(options);
      break;
    case 1:
      node = std::make_shared<cm::VoxelBasedApproximateCompareMapFilterComponent>(options);
      break;
    case 2:
      node = std::make_shared<cm::VoxelBasedCompareMapFilterComponent>(options);
      break;
    case 3:
      node = std::make_shared<cm::VoxelDistanceBasedCompareMapFilterComponent>(options);
      break;
    case 4:
      node = std::make_shared<cm::CompareElevationMapFilterComponent>(options);
      break;
    default:
      node = std::make_shared<cm::LaneletElevationFilterComponent>(options);
      break;
  }
  auto raw = peer->create_publisher<Cloud>("/map", rclcpp::QoS(1).transient_local());
  auto elev = peer->create_publisher<grid_map_msgs::msg::GridMap>(
    "/input/elevation_map", rclcpp::QoS(1).transient_local());
  auto lane = peer->create_publisher<autoware_map_msgs::msg::LaneletMapBin>(
    std::string("/") + node->get_name() + "/input/lanelet_map", rclcpp::QoS(1).transient_local());
  raw->publish(cloud({{0, 0, 0}, {1, 1, 0}}));
  grid_map::GridMap map({"elevation"});
  map.setFrameId("map");
  map.setGeometry(grid_map::Length(10, 10), 1.0);
  map["elevation"].setZero();
  elev->publish(*grid_map::GridMapRosConverter::toMessage(map));
  auto lanes = std::make_shared<lanelet::LaneletMap>();
  auto point = [](double x, double y) {
    return lanelet::Point3d(lanelet::utils::getId(), x, y, 0);
  };
  lanelet::LineString3d left(lanelet::utils::getId(), {point(-5, 2), point(5, 2)});
  lanelet::LineString3d right(lanelet::utils::getId(), {point(-5, -2), point(5, -2)});
  lanes->add(lanelet::Lanelet(lanelet::utils::getId(), left, right));
  lane->publish(autoware::experimental::lanelet2_utils::to_autoware_map_msgs(lanes));
  auto pub = peer->create_publisher<Cloud>("/input", rclcpp::SensorDataQoS());
  std::promise<Cloud> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->create_subscription<Cloud>(
    "/output", rclcpp::SensorDataQoS(), [&](Cloud::ConstSharedPtr msg) {
      if (!delivered && msg->width == (dynamic ? 4U : mode < 4 ? 2U : 1U)) {
        delivered = true;
        received.set_value(*msg);
      }
    });
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(peer);
  std::jthread spin([&] { executor.spin(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const auto input = dynamic
                       ? cloud({{.02F, .02F, 0}, {1, 1, 2}, {50, 0, 0}, {4.9F, 0, 1}, {.6F, 0, 0}})
                       : cloud({{.02F, .02F, 0}, {1, 1, 2}, {50, 0, 0}});
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
         std::chrono::steady_clock::now() < deadline) {
    if (dynamic) odom->publish(nav_msgs::msg::Odometry{});
    pub->publish(input);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  executor.cancel();
  spin.join();
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  if (dynamic) EXPECT_GT(requests.load(), 0);
  const auto output = result.get();
  EXPECT_EQ(output.header, input.header);
  sensor_msgs::PointCloud2ConstIterator<float> x(output, "x"), z(output, "z");
  if (mode < 5) {
    EXPECT_NEAR(*x, 1, 1e-6);
    EXPECT_NEAR(*z, 2, 1e-6);
  } else {
    EXPECT_NEAR(*x, .02, 1e-6);
    EXPECT_NEAR(*z, 0, 1e-6);
  }
  if (mode < 4) {
    ++x;
    EXPECT_NEAR(*x, 50, 1e-6);
    EXPECT_EQ(output.fields, input.fields);
    if (dynamic) {
      ++x;
      EXPECT_NEAR(*x, 4.9, 1e-6);
      ++x;
      EXPECT_NEAR(*x, .6, 1e-6);
    }
  }
}
INSTANTIATE_TEST_SUITE_P(AllComponents, MapFilters, testing::Range(0, 10));
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
