// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "ros_fixture.hpp"

#include <radar_tracks_msgs_converter_node.hpp>
#include <tf2_ros/static_transform_broadcaster.hpp>

#include <cmath>

namespace
{
using autoware_perception_msgs::msg::DetectedObjects;
using autoware_perception_msgs::msg::TrackedObjects;
using radar_msgs::msg::RadarTracks;

class TracksTest : public RadarRosTest
{
protected:
  void SetUp() override
  {
    rclcpp::NodeOptions options;
    options.parameter_overrides(
      {{"update_rate_hz", 40.0},
       {"new_frame_id", "base_link"},
       {"use_twist_compensation", true},
       {"use_twist_yaw_compensation", false},
       {"static_object_speed_threshold", 1.0}});
    node =
      attach(std::make_shared<autoware::radar_tracks_msgs_converter::RadarTracksMsgsConverterNode>(
        options));
    peer = attach(std::make_shared<rclcpp::Node>("tracks_peer"));
    tracked_sub = peer->create_subscription<TrackedObjects>(
      "/radar_tracks_msgs_converter/output/radar_tracked_objects", 1,
      [&](TrackedObjects::ConstSharedPtr m) { tracked = m; });
    detected_sub = peer->create_subscription<DetectedObjects>(
      "/radar_tracks_msgs_converter/output/radar_detected_objects", 1,
      [&](DetectedObjects::ConstSharedPtr m) { detected = m; });
    radar_pub = peer->create_publisher<RadarTracks>(
      "/radar_tracks_msgs_converter/input/radar_objects", rclcpp::SensorDataQoS());
    odom_pub = peer->create_publisher<nav_msgs::msg::Odometry>(
      "/radar_tracks_msgs_converter/input/odometry", 1);
    input.header.frame_id = "radar";
    input.header.stamp = peer->now();
    radar_msgs::msg::RadarTrack track;
    track.uuid.uuid[0] = 42;
    track.position.x = 10;
    track.velocity.x = -3;
    track.size.x = 4;
    track.size.y = 2;
    track.size.z = 1;
    track.classification = 32001;
    track.position_covariance = {1, 2, 3, 4, 5, 6};
    track.velocity_covariance = {7, 8, 9, 10, 11, 12};
    track.acceleration_covariance = {13, 14, 15, 16, 17, 18};
    input.tracks = {track};
    odometry.twist.twist.linear.x = 4;
  }
  void transform(double yaw = 0)
  {
    broadcaster = std::make_unique<tf2_ros::StaticTransformBroadcaster>(peer);
    geometry_msgs::msg::TransformStamped tf;
    tf.header.stamp = peer->now();
    tf.header.frame_id = "base_link";
    tf.child_frame_id = "radar";
    tf.transform.translation.x = 1;
    tf.transform.translation.y = 2;
    tf.transform.rotation.z = std::sin(yaw / 2);
    tf.transform.rotation.w = std::cos(yaw / 2);
    broadcaster->sendTransform(tf);
  }
  bool receive(size_t count)
  {
    return await(
      [&] {
        return tracked && detected && tracked->objects.size() == count &&
               detected->objects.size() == count;
      },
      [&] {
        radar_pub->publish(input);
        odom_pub->publish(odometry);
      });
  }
  rclcpp::Node::SharedPtr node, peer;
  TrackedObjects::ConstSharedPtr tracked;
  DetectedObjects::ConstSharedPtr detected;
  rclcpp::Subscription<TrackedObjects>::SharedPtr tracked_sub;
  rclcpp::Subscription<DetectedObjects>::SharedPtr detected_sub;
  rclcpp::Publisher<RadarTracks>::SharedPtr radar_pub;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub;
  std::unique_ptr<tf2_ros::StaticTransformBroadcaster> broadcaster;
  RadarTracks input;
  nav_msgs::msg::Odometry odometry;
};

TEST_F(TracksTest, ConvertsIdentityGeometryCovarianceAndBothOutputs)
{
  transform();
  ASSERT_TRUE(receive(1));
  EXPECT_EQ(tracked->header.frame_id, "base_link");
  EXPECT_EQ(tracked->header.stamp, input.header.stamp);
  const auto object = tracked->objects.front();
  EXPECT_EQ(object.object_id, input.tracks.front().uuid);
  EXPECT_FLOAT_EQ(object.existence_probability, 1);
  EXPECT_DOUBLE_EQ(object.kinematics.pose_with_covariance.pose.position.x, 11);
  EXPECT_DOUBLE_EQ(object.kinematics.pose_with_covariance.pose.position.y, 2);
  EXPECT_EQ(object.shape.dimensions, input.tracks.front().size);
  EXPECT_EQ(
    object.classification.front().label, autoware_perception_msgs::msg::ObjectClassification::CAR);
  const auto & p = object.kinematics.pose_with_covariance.covariance;
  EXPECT_DOUBLE_EQ(p[0], 1);
  EXPECT_DOUBLE_EQ(p[1], 2);
  EXPECT_DOUBLE_EQ(p[6], 2);
  EXPECT_DOUBLE_EQ(p[7], 4);
  EXPECT_DOUBLE_EQ(p[14], 6);
  EXPECT_DOUBLE_EQ(object.kinematics.twist_with_covariance.covariance[7], 10);
  EXPECT_DOUBLE_EQ(object.kinematics.acceleration_with_covariance.covariance[14], 18);
  ASSERT_TRUE(await(
    [&] { return tracked->objects.front().kinematics.twist_with_covariance.twist.linear.x == 1; },
    [&] {
      radar_pub->publish(input);
      odom_pub->publish(odometry);
    }));
  EXPECT_EQ(detected->header, tracked->header);
  EXPECT_TRUE(detected->objects.front().kinematics.has_twist);
  EXPECT_TRUE(detected->objects.front().kinematics.has_twist_covariance);
  EXPECT_EQ(detected->objects.front().classification, object.classification);
  EXPECT_EQ(detected->objects.front().shape, object.shape);
}

TEST_F(TracksTest, RotatesVelocityAndAppliesLiveCompensationSetting)
{
  transform(M_PI / 2);
  ASSERT_TRUE(node->set_parameter(rclcpp::Parameter("use_twist_compensation", false)).successful);
  ASSERT_TRUE(receive(1));
  auto pose = tracked->objects.front().kinematics.pose_with_covariance.pose;
  EXPECT_NEAR(pose.position.x, 1, 1e-6);
  EXPECT_NEAR(pose.position.y, 12, 1e-6);
  EXPECT_NEAR(pose.orientation.z, -std::sqrt(0.5), 1e-6);
  EXPECT_NEAR(tracked->objects.front().kinematics.twist_with_covariance.twist.linear.x, 3, 1e-6);
  ASSERT_TRUE(node->set_parameter(rclcpp::Parameter("use_twist_compensation", true)).successful);
  ASSERT_TRUE(await(
    [&] {
      return tracked && !tracked->objects.empty() &&
             std::abs(
               tracked->objects.front().kinematics.twist_with_covariance.twist.linear.x - 5) < 1e-6;
    },
    [&] {
      radar_pub->publish(input);
      odom_pub->publish(odometry);
    }));
}

TEST_F(TracksTest, MissingTransformProducesEmptyMessagesAndClassificationRecovers)
{
  ASSERT_TRUE(receive(0));
  transform();
  auto track = input.tracks.front();
  input.tracks.clear();
  for (int code = 32000; code <= 32007; ++code) {
    track.classification = code;
    input.tracks.push_back(track);
  }
  track.classification = 123;
  input.tracks.push_back(track);
  ASSERT_TRUE(receive(9));
  using C = autoware_perception_msgs::msg::ObjectClassification;
  const std::vector<uint8_t> labels = {C::UNKNOWN, C::CAR,        C::TRUCK,
                                       C::BUS,     C::TRAILER,    C::MOTORCYCLE,
                                       C::BICYCLE, C::PEDESTRIAN, C::UNKNOWN};
  for (size_t i = 0; i < labels.size(); ++i) {
    EXPECT_EQ(tracked->objects[i].classification.front().label, labels[i]);
    EXPECT_EQ(detected->objects[i].classification.front().label, labels[i]);
  }
}
}  // namespace
