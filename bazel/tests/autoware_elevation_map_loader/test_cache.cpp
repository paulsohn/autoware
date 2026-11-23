// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "elevation_map_loader_node.hpp"

#include <grid_map_ros/GridMapRosConverter.hpp>

#include <gtest/gtest.h>
#include <pcl_conversions/pcl_conversions.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <thread>

class ElevationLoader : public testing::TestWithParam<bool>
{
};
TEST_P(ElevationLoader, GeneratesAndReloadsIndexedElevationCache)
{
  const auto directory =
    std::filesystem::path(std::getenv("TEST_TMPDIR")) / (GetParam() ? "inpaint" : "raw");
  std::filesystem::create_directories(directory);
  auto run = [&](float height, bool cached) {
    rclcpp::NodeOptions options;
    options.parameter_overrides(
      {{"param_file_path", std::string(std::getenv("ELEVATION_PARAMS"))},
       {"use_sequential_load", false},
       {"use_lane_filter", false},
       {"use_inpaint", GetParam()},
       {"elevation_map_directory", directory.string()}});
    auto node = std::make_shared<autoware::elevation_map_loader::ElevationMapLoaderNode>(options);
    auto peer = std::make_shared<rclcpp::Node>("elevation_peer");
    auto hash = peer->create_publisher<tier4_external_api_msgs::msg::MapHash>(
      "/api/autoware/get/map/info/hash", rclcpp::QoS(1).transient_local());
    auto pub = peer->create_publisher<sensor_msgs::msg::PointCloud2>(
      "/input/pointcloud_map", rclcpp::QoS(1).transient_local());
    std::promise<grid_map_msgs::msg::GridMap> received;
    auto result = received.get_future();
    bool delivered = false;
    auto sub = peer->create_subscription<grid_map_msgs::msg::GridMap>(
      "/output/elevation_map", rclcpp::QoS(1).transient_local(),
      [&](grid_map_msgs::msg::GridMap::ConstSharedPtr msg) {
        if (!delivered) {
          delivered = true;
          received.set_value(*msg);
        }
      });
    pcl::PointCloud<pcl::PointXYZ> plane;
    for (int x = 0; x < 60; ++x)
      for (int y = 0; y < 60; ++y) plane.push_back(pcl::PointXYZ(x * .03F, y * .03F, height));
    sensor_msgs::msg::PointCloud2 cloud;
    pcl::toROSMsg(plane, cloud);
    cloud.header.frame_id = "map";
    tier4_external_api_msgs::msg::MapHash id;
    id.pcd = "fixture";
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(peer);
    std::jthread spin([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      hash->publish(id);
      pub->publish(cloud);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    executor.cancel();
    spin.join();
    ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
    const auto message = result.get();
    EXPECT_EQ(message.header.frame_id, "map");
    grid_map::GridMap map;
    ASSERT_TRUE(grid_map::GridMapRosConverter::fromMessage(message, map));
    ASSERT_TRUE(map.exists("elevation"));
    size_t count = 0;
    for (grid_map::GridMapIterator it(map); !it.isPastEnd(); ++it) {
      const auto z = map.at("elevation", *it);
      if (std::isfinite(z)) {
        ++count;
        EXPECT_NEAR(z, cached ? 3.0 : height, 0.02);
      }
    }
    EXPECT_GT(count, 10U);
  };
  run(3.0F, false);
  ASSERT_TRUE(std::filesystem::exists(directory / "fixture" / "metadata.yaml"));
  // A different cloud height proves the second node reads the saved map.
  run(7.0F, true);
}
INSTANTIATE_TEST_SUITE_P(Inpainting, ElevationLoader, testing::Bool());
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
