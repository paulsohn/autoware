// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "ros_fixture.hpp"

#include <autoware/simple_object_merger/simple_detected_object_merger_node.hpp>
#include <tf2_ros/static_transform_broadcaster.hpp>

namespace
{
using autoware::simple_object_merger::SimpleDetectedObjectMergerNode;
using autoware_perception_msgs::msg::DetectedObject;
using autoware_perception_msgs::msg::DetectedObjects;

rclcpp::NodeOptions options(const std::vector<std::string> & topics)
{
  rclcpp::NodeOptions o;
  o.parameter_overrides(
    {{"update_rate_hz", 40.0},
     {"new_frame_id", "base_link"},
     {"timeout_threshold", 0.5},
     {"input_topics", topics}});
  return o;
}

TEST_F(RadarRosTest, DetectedTimerExcludesStaleInputAndAppliesLiveThreshold)
{
  auto merger =
    std::make_shared<SimpleDetectedObjectMergerNode>(options({"input/a", "input/b", "input/c"}));
  attach(merger->get_rclcpp_node());
  auto peer = attach(std::make_shared<rclcpp::Node>("merger_timer_peer"));
  DetectedObjects::ConstSharedPtr output;
  auto sub = peer->create_subscription<DetectedObjects>(
    "/simple_object_merger/output/objects", 1,
    [&](DetectedObjects::ConstSharedPtr m) { output = m; });
  std::vector<rclcpp::Publisher<DetectedObjects>::SharedPtr> pubs;
  for (const auto * name : {"input/a", "input/b", "input/c"})
    pubs.push_back(peer->create_publisher<DetectedObjects>(name, 1));
  auto publish = [&] {
    for (size_t i = 0; i < 3; ++i) {
      DetectedObjects m;
      m.header.frame_id = "base_link";
      m.header.stamp = peer->now() - rclcpp::Duration::from_seconds(i == 1 ? 2 : 0);
      m.objects.resize(i + 1);
      pubs[i]->publish(m);
    }
  };
  ASSERT_TRUE(await([&] { return output && output->objects.size() == 4; }, publish));
  EXPECT_EQ(output->header.frame_id, "base_link");
  ASSERT_TRUE(merger->set_parameter(rclcpp::Parameter("timeout_threshold", 10.0)).successful);
  ASSERT_TRUE(await([&] { return output && output->objects.size() == 6; }, publish));
}

TEST_F(RadarRosTest, DetectedSynchronizedMergeTransformsInputGeometry)
{
  auto merger = std::make_shared<SimpleDetectedObjectMergerNode>(options({"input/a", "input/b"}));
  attach(merger->get_rclcpp_node());
  auto peer = attach(std::make_shared<rclcpp::Node>("merger_tf_peer"));
  tf2_ros::StaticTransformBroadcaster broadcaster(peer);
  geometry_msgs::msg::TransformStamped tf;
  tf.header.stamp = peer->now();
  tf.header.frame_id = "base_link";
  tf.child_frame_id = "radar";
  tf.transform.rotation.w = 1;
  tf.transform.translation.x = 5;
  broadcaster.sendTransform(tf);
  DetectedObjects::ConstSharedPtr output;
  auto sub = peer->create_subscription<DetectedObjects>(
    "/simple_object_merger/output/objects", 1,
    [&](DetectedObjects::ConstSharedPtr m) { output = m; });
  auto a = peer->create_publisher<DetectedObjects>("input/a", 1);
  auto b = peer->create_publisher<DetectedObjects>("input/b", 1);
  DetectedObjects first, second;
  first.header.frame_id = "radar";
  second.header.frame_id = "base_link";
  first.header.stamp = second.header.stamp = peer->now();
  DetectedObject object;
  object.kinematics.pose_with_covariance.pose.position.x = 10;
  object.kinematics.pose_with_covariance.pose.orientation.w = 1;
  first.objects = second.objects = {object};
  ASSERT_TRUE(await(
    [&] { return output && output->objects.size() == 2; },
    [&] {
      a->publish(first);
      b->publish(second);
    }));
  EXPECT_EQ(output->header.frame_id, "base_link");
  EXPECT_EQ(output->header.stamp, first.header.stamp);
  EXPECT_DOUBLE_EQ(output->objects[0].kinematics.pose_with_covariance.pose.position.x, 15);
  EXPECT_DOUBLE_EQ(output->objects[1].kinematics.pose_with_covariance.pose.position.x, 10);
}
}  // namespace
