// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "ros_fixture.hpp"

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <radar_object_tracker_node.hpp>

#include <tuple>

namespace
{
using autoware_perception_msgs::msg::DetectedObjects;
using autoware_perception_msgs::msg::TrackedObjects;

class TrackerRosTest : public RadarRosTest,
                       public testing::WithParamInterface<std::tuple<std::string, bool>>
{
};

TEST_P(TrackerRosTest, AssociatesBothObjectsAndExpiresTracks)
{
  const auto share = ament_index_cpp::get_package_share_directory("autoware_radar_object_tracker");
  rclcpp::NodeOptions options;
  options.arguments(
    {"--ros-args", "--params-file", share + "/config/radar_object_tracker.param.yaml",
     "--params-file", share + "/config/data_association_matrix.param.yaml", "--params-file",
     share + "/config/default_tracker.param.yaml"});
  options.parameter_overrides(
    {{"tracking_config_directory", share + "/config/tracking/"},
     {"world_frame_id", "base_link"},
     {"enable_delay_compensation", std::get<1>(GetParam())},
     {"publish_rate", 40.0},
     {"tracker_lifetime", 0.3},
     {"measurement_count_threshold", 2},
     {"use_distance_based_noise_filtering", false},
     {"use_map_based_noise_filtering", false},
     {"car_tracker", std::get<0>(GetParam())}});
  auto tracker =
    attach(std::make_shared<autoware::radar_object_tracker::RadarObjectTrackerNode>(options));
  auto peer = attach(std::make_shared<rclcpp::Node>("tracker_peer"));
  TrackedObjects::ConstSharedPtr output;
  auto sub = peer->create_subscription<TrackedObjects>(
    "output", 1, [&](TrackedObjects::ConstSharedPtr m) { output = m; });
  auto pub = peer->create_publisher<DetectedObjects>("input", 1);
  DetectedObjects input;
  input.header.frame_id = "base_link";
  for (double x : {100.0, 120.0}) {
    autoware_perception_msgs::msg::DetectedObject o;
    o.existence_probability = 1;
    o.kinematics.pose_with_covariance.pose.position.x = x;
    o.kinematics.pose_with_covariance.pose.orientation.w = 1;
    o.kinematics.pose_with_covariance.covariance[0] = 1;
    o.kinematics.pose_with_covariance.covariance[7] = 1;
    o.kinematics.orientation_availability =
      autoware_perception_msgs::msg::DetectedObjectKinematics::AVAILABLE;
    o.shape.type = autoware_perception_msgs::msg::Shape::BOUNDING_BOX;
    o.shape.dimensions.x = 4;
    o.shape.dimensions.y = 2;
    o.shape.dimensions.z = 1.5;
    autoware_perception_msgs::msg::ObjectClassification c;
    c.label = c.CAR;
    c.probability = 1;
    o.classification = {c};
    input.objects.push_back(o);
  }
  auto publish = [&] {
    input.header.stamp = peer->now();
    pub->publish(input);
  };
  ASSERT_TRUE(await([&] { return output && output->objects.size() == 2; }, publish));
  EXPECT_EQ(output->header.frame_id, "base_link");
  const auto first = output->objects[0].object_id;
  const auto second = output->objects[1].object_id;
  EXPECT_NE(first, second);
  const auto stamp = output->header.stamp;
  input.objects[0].kinematics.pose_with_covariance.pose.position.x += 0.5;
  ASSERT_TRUE(await(
    [&] {
      return output->header.stamp != stamp && output->objects.size() == 2 &&
             output->objects[0].kinematics.pose_with_covariance.pose.position.x > 100.05;
    },
    publish));
  EXPECT_EQ(output->objects[0].object_id, first);
  EXPECT_EQ(output->objects[1].object_id, second);
  EXPECT_NEAR(output->objects[0].kinematics.pose_with_covariance.pose.position.x, 100.5, 1);
  input.objects.clear();
  ASSERT_TRUE(await([&] { return output->objects.empty(); }, publish));
}
INSTANTIATE_TEST_SUITE_P(
  MotionModels, TrackerRosTest,
  testing::Combine(
    testing::Values(
      std::string("linear_motion_tracker"), std::string("constant_turn_rate_motion_tracker")),
    testing::Bool()));
}  // namespace
