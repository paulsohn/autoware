// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "low_intensity_cluster_filter_node.hpp"

#include <agnocast/agnocast_callback_isolated_executor.hpp>
#include <autoware/point_types/types.hpp>
#include <point_cloud_msg_wrapper/point_cloud_msg_wrapper.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;
using Message = tier4_perception_msgs::msg::DetectedObjectsWithFeature;
TEST(RaindropTransport, FiltersOnlyLowConfidenceLowIntensityTargetsInsideRange)
{
  rclcpp::NodeOptions options;
  options.arguments({"--ros-args", "--params-file", std::getenv("RAINDROP_PARAMS")});
  auto node =
    std::make_shared<autoware::low_intensity_cluster_filter::LowIntensityClusterFilter>(options);
  auto peer = std::make_shared<rclcpp::Node>("raindrop_peer");
  auto pub =
    AUTOWARE_CREATE_PUBLISHER2_ON_NODE(Message, peer.get(), "/input/objects", rclcpp::QoS(1));
  std::promise<Message> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = AUTOWARE_CREATE_SUBSCRIPTION_ON_NODE(
    Message, peer.get(), "/output/objects", rclcpp::QoS(1),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Message) & msg) {
      if (!delivered) {
        delivered = true;
        received.set_value(*msg);
      }
    },
    AUTOWARE_SUBSCRIPTION_OPTIONS{});
  Message input;
  input.header.frame_id = "base_link";
  input.header.stamp.sec = 123;
  input.header.stamp.nanosec = 456;
  input.feature_objects.resize(5);
  for (size_t i = 0; i < 5; ++i) {
    auto & feature = input.feature_objects[i];
    auto & obj = feature.object;
    obj.classification.resize(1);
    obj.classification[0].label = i == 3 ? 1 : 0;
    obj.classification[0].probability = .9;
    obj.existence_probability = i == 2 ? .9 : .1;
    obj.kinematics.pose_with_covariance.pose.position.x = i == 4 ? 70 : 10;
    obj.kinematics.pose_with_covariance.pose.orientation.w = 1;
    obj.kinematics.pose_with_covariance.covariance[0] = i + 1;
    feature.feature.roi.x_offset = i;
    using Point = autoware::point_types::PointXYZIRC;
    point_cloud_msg_wrapper::PointCloud2Modifier<Point, autoware::point_types::PointXYZIRCGenerator>
      cloud(feature.feature.cluster, "base_link");
    cloud.resize(16);
    for (size_t p = 0; p < 16; ++p) {
      cloud[p].x = 10;
      cloud[p].y = 0;
      cloud[p].z = 0;
      cloud[p].intensity = i == 1 ? 5 : 0;
    }
  }
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
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = result.get();
  EXPECT_EQ(output.header, input.header);
  ASSERT_EQ(output.feature_objects.size(), 4U);
  for (size_t i = 0; i < 4; ++i) EXPECT_EQ(output.feature_objects[i], input.feature_objects[i + 1]);
}
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, false);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
