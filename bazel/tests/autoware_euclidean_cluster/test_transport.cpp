// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#if CLUSTER_KIND == 0
#include "euclidean_cluster_node.hpp"
#elif CLUSTER_KIND == 1
#include "voxel_grid_based_euclidean_cluster_node.hpp"
#else
#include "label_based_euclidean_cluster_node.hpp"
#endif

#include <agnocast/agnocast_callback_isolated_executor.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware/agnocast_wrapper/autoware_agnocast_wrapper.hpp>
#include <autoware/point_types/memory.hpp>
#include <autoware/point_types/types.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>
#include <thread>
#include <vector>
namespace wrapper = autoware::agnocast_wrapper;
#if CLUSTER_KIND == 2
using Node = autoware::euclidean_cluster::LabelBasedEuclideanClusterNode;
using Output = autoware_perception_msgs::msg::DetectedObjects;
#else
using Node = autoware::euclidean_cluster::CLUSTER_CLASS;
using Output = tier4_perception_msgs::msg::DetectedObjectsWithFeature;
#endif
TEST(ClusterTransport, SeparatesNonemptyClustersAndPreservesHeader)
{
  rclcpp::NodeOptions options;
  options.arguments(
    {"--ros-args", "--params-file",
     ament_index_cpp::get_package_share_directory("autoware_euclidean_cluster") +
       "/config/" CLUSTER_PARAMS});
#if CLUSTER_KIND == 2
  options.append_parameter_override("min_cluster_size_m", 0.0);
#endif
  auto node = std::make_shared<Node>(options);
#if CLUSTER_KIND == 2
  auto peer = std::make_shared<wrapper::Node>("semantic_cluster_peer");
  auto pub = peer->create_publisher<sensor_msgs::msg::PointCloud2>(
    "input/pointcloud", rclcpp::SensorDataQoS());
#else
  auto peer = std::make_shared<rclcpp::Node>("cluster_peer");
  auto pub =
    peer->create_publisher<sensor_msgs::msg::PointCloud2>("input", rclcpp::SensorDataQoS());
#endif
  std::promise<Output> received;
  auto result = received.get_future();
  bool delivered = false;
#if CLUSTER_KIND == 0
  using OutputPtr = Output::ConstSharedPtr;
#else
  using OutputPtr = AUTOWARE_MESSAGE_CONST_SHARED_PTR(Output);
#endif
  auto callback = [&](const OutputPtr & msg) {
    if (!delivered) {
      delivered = true;
      received.set_value(*msg);
    }
  };
#if CLUSTER_KIND == 2
  auto sub = peer->create_subscription<Output>("output/objects", rclcpp::QoS(1), callback);
#elif CLUSTER_KIND == 1
  auto sub = AUTOWARE_CREATE_SUBSCRIPTION_ON_NODE(
    Output, peer.get(), "output", rclcpp::QoS(1), callback, AUTOWARE_SUBSCRIPTION_OPTIONS{});
#else
  auto sub = peer->create_subscription<Output>("output", rclcpp::QoS(1), callback);
#endif
  sensor_msgs::msg::PointCloud2 input;
#if CLUSTER_KIND == 2
  using Point = autoware::point_types::PointXYZCPE;
  input.fields = autoware::point_types::create_fields_point_xyzcpe();
  input.point_step = sizeof(Point);
  input.width = 32;
  input.height = 1;
  input.row_step = input.width * input.point_step;
  input.is_dense = true;
  input.data.resize(input.row_step);
  for (int i = 0; i < 32; ++i) {
    Point p;
    p.x = 5 + (i / 16) * 10 + (i % 4) * .05;
    p.y = ((i % 16) / 4) * .05;
    p.z = (i % 2) * .1;
    p.class_id = i < 16 ? 0 : 1;
    p.probability = .9;
    std::memcpy(input.data.data() + i * sizeof(Point), &p, sizeof(Point));
  }
#else
  pcl::PointCloud<pcl::PointXYZ> cloud;
  for (int i = 0; i < 32; ++i)
    cloud.emplace_back(5 + (i / 16) * 10 + (i % 4) * .05, ((i % 16) / 4) * .05, (i % 2) * .1);
  pcl::toROSMsg(cloud, input);
#endif
  input.header.frame_id = "map";
  input.header.stamp.sec = 123;
  input.header.stamp.nanosec = 456;
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      pub->publish(input);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    executor.cancel();
  };
#if CLUSTER_KIND == 2
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
#else
  if (wrapper::use_agnocast()) {
    EXPECT_TRUE(rclcpp::ok());
    agnocast::CallbackIsolatedAgnocastExecutor executor;
    executor.add_node(node);
    executor.add_node(peer);
    exchange(executor);
  } else {
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(peer);
    exchange(executor);
  }
#endif
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = result.get();
  EXPECT_EQ(output.header, input.header);
#if CLUSTER_KIND == 2
  ASSERT_EQ(output.objects.size(), 2U);
  std::vector<int> labels;
  for (const auto & o : output.objects) {
    ASSERT_FALSE(o.classification.empty());
    labels.push_back(o.classification.front().label);
    EXPECT_GT(o.shape.footprint.points.size(), 2U);
    EXPECT_NEAR(o.classification.front().probability, .9, 1e-5);
  }
  std::sort(labels.begin(), labels.end());
  EXPECT_EQ(labels, (std::vector<int>{1, 2}));
#else
  ASSERT_EQ(output.feature_objects.size(), 2U);
  size_t total = 0;
  std::vector<float> centres;
  for (const auto & f : output.feature_objects) {
    pcl::PointCloud<pcl::PointXYZ> points;
    pcl::fromROSMsg(f.feature.cluster, points);
    ASSERT_EQ(points.size(), 16U);
    float mean = 0;
    for (const auto & p : points) mean += p.x;
    centres.push_back(mean / points.size());
    total += points.size();
  }
  EXPECT_EQ(total, 32U);
  std::sort(centres.begin(), centres.end());
  EXPECT_NEAR(centres[0], 5.075, 1e-4);
  EXPECT_NEAR(centres[1], 15.075, 1e-4);
#endif
}
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, CLUSTER_KIND == 2);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
