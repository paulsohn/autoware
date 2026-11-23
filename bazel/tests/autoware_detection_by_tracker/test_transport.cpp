// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "detection_by_tracker_node.hpp"

#include <agnocast/agnocast_callback_isolated_executor.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <tf2/utils.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;
using Featured = tier4_perception_msgs::msg::DetectedObjectsWithFeature;
using Detected = autoware_perception_msgs::msg::DetectedObjects;
using Tracked = autoware_perception_msgs::msg::TrackedObjects;
TEST(TrackerHandler, SelectsNearestSampleAndExtrapolatesPose)
{
  autoware::detection_by_tracker::TrackerHandler handler;
  Tracked out;
  EXPECT_FALSE(handler.estimateTrackedObjects(rclcpp::Time(101, 500000000, RCL_ROS_TIME), out));
  auto old = std::make_shared<Tracked>();
  old->header.frame_id = "map";
  old->header.stamp.sec = 100;
  old->objects.resize(1);
  auto & object = old->objects[0];
  object.object_id.uuid[0] = 42;
  object.classification.resize(1);
  object.classification[0].label = 1;
  object.existence_probability = .9;
  object.shape.dimensions.x = 4.4;
  object.kinematics.pose_with_covariance.pose.orientation.w = 1;
  object.kinematics.twist_with_covariance.twist.linear.x = 2;
  object.kinematics.twist_with_covariance.twist.angular.z = .1;
  handler.onTrackedObjects(old);
  auto recent = std::make_shared<Tracked>(*old);
  recent->header.stamp.sec = 102;
  recent->objects[0].kinematics.pose_with_covariance.pose.position.x = 10;
  handler.onTrackedObjects(recent);
  ASSERT_TRUE(handler.estimateTrackedObjects(rclcpp::Time(101, 500000000, RCL_ROS_TIME), out));
  ASSERT_EQ(out.objects.size(), 1U);
  EXPECT_EQ(out.header.frame_id, "map");
  EXPECT_EQ(out.header.stamp.sec, 101);
  EXPECT_EQ(out.header.stamp.nanosec, 500000000U);
  EXPECT_NEAR(out.objects[0].kinematics.pose_with_covariance.pose.position.x, 9, 1e-5);
  EXPECT_NEAR(
    tf2::getYaw(out.objects[0].kinematics.pose_with_covariance.pose.orientation), -.05, 1e-5);
  EXPECT_EQ(out.objects[0].object_id, object.object_id);
  EXPECT_EQ(out.objects[0].classification, object.classification);
  EXPECT_EQ(out.objects[0].shape, object.shape);
}
void check_detection(int mode)
{
  rclcpp::NodeOptions options;
  options.arguments(
    {"--ros-args", "--params-file",
     ament_index_cpp::get_package_share_directory("autoware_detection_by_tracker") +
       "/config/detection_by_tracker.param.yaml"});
  auto node = std::make_shared<autoware::detection_by_tracker::DetectionByTracker>(options);
  auto peer = std::make_shared<rclcpp::Node>("tracker_detection_peer");
  auto tracks = peer->create_publisher<Tracked>("/detection_by_tracker/input/tracked_objects", 1);
  auto input_pub = AUTOWARE_CREATE_PUBLISHER2_ON_NODE(
    Featured, peer.get(), "/detection_by_tracker/input/initial_objects", rclcpp::QoS(1));
  std::promise<Detected> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->create_subscription<Detected>(
    "/detection_by_tracker/output", rclcpp::QoS(1), [&](Detected::ConstSharedPtr msg) {
      if (!delivered && (mode == 0 || !msg->objects.empty())) {
        delivered = true;
        received.set_value(*msg);
      }
    });
  Tracked tracker;
  tracker.header.frame_id = "map";
  tracker.header.stamp.sec = 123;
  tracker.header.stamp.nanosec = 456;
  tracker.objects.resize(1);
  auto & t = tracker.objects[0];
  t.classification.resize(1);
  t.classification[0].label = 1;
  t.classification[0].probability = .9;
  t.existence_probability = .9;
  t.kinematics.pose_with_covariance.pose.position.x = mode == 3 ? 20 : 10;
  t.kinematics.pose_with_covariance.pose.position.z = .75;
  t.kinematics.pose_with_covariance.pose.orientation.w = 1;
  t.shape.type = 0;
  t.shape.dimensions.x = 4.4;
  t.shape.dimensions.y = 1.8;
  t.shape.dimensions.z = 1.5;
  Featured input;
  input.header = tracker.header;
  auto box = [&](double centre, double length) {
    pcl::PointCloud<pcl::PointXYZ> cloud;
    for (int x = 0; x <= 22; ++x)
      for (int y = 0; y <= 9; ++y)
        for (int z = 0; z < 2; ++z)
          cloud.emplace_back(centre - length / 2 + length * x / 22, -.9 + .2 * y, z * 1.5);
    return cloud;
  };
  for (int i = 0; i < (mode == 1 ? 2 : 1); ++i) {
    tier4_perception_msgs::msg::DetectedObjectWithFeature f;
    f.object.shape = t.shape;
    f.object.shape.dimensions.x = mode == 1 ? 2.2 : 14.4;
    f.object.kinematics.pose_with_covariance.pose = t.kinematics.pose_with_covariance.pose;
    f.object.kinematics.pose_with_covariance.pose.position.x = mode == 1 ? 8.9 + 2.2 * i : 15;
    f.object.classification = t.classification;
    auto cloud = mode == 1 ? box(8.9 + 2.2 * i, 2.2) : box(10, 4.4);
    if (mode != 1) cloud += box(20, 4.4);
    pcl::toROSMsg(cloud, f.feature.cluster);
    f.feature.cluster.header = input.header;
    input.feature_objects.push_back(f);
  }
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      if (mode) tracks->publish(tracker);
      input_pub->publish(input);
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
  if (!mode) {
    EXPECT_TRUE(output.objects.empty());
    return;
  }
  ASSERT_EQ(output.objects.size(), 1U);
  const auto & out = output.objects[0];
  EXPECT_EQ(out.classification, t.classification);
  EXPECT_GT(out.existence_probability, .8);
  EXPECT_NEAR(out.kinematics.pose_with_covariance.pose.position.x, mode == 3 ? 20 : 10, .15);
  EXPECT_NEAR(out.kinematics.pose_with_covariance.pose.position.y, 0, .15);
  EXPECT_NEAR(out.shape.dimensions.x, 4.4, .2);
  EXPECT_NEAR(out.shape.dimensions.y, 1.8, .2);
  EXPECT_NEAR(out.shape.dimensions.z, 1.5, .1);
}
TEST(TrackerDetectionTransport, NoTrackerPublishesStampedEmpty)
{
  check_detection(0);
}
TEST(TrackerDetectionTransport, MergesOversegmentedClusters)
{
  check_detection(1);
}
TEST(TrackerDetectionTransport, DividesUndersegmentedCluster)
{
  check_detection(2);
  check_detection(3);
}
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, false);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
