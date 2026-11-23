// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "ros_fixture.hpp"

#include <node.hpp>

namespace
{
using namespace autoware::radar_fusion_to_detected_object;

RadarFusionToDetectedObject::Param parameters()
{
  RadarFusionToDetectedObject::Param p;
  p.threshold_yaw_diff = 0.35;
  p.velocity_weight_min_distance = 1;
  p.threshold_probability = 0.35;
  return p;
}

DetectedObject object(double x = 0, float confidence = 0.9)
{
  DetectedObject o;
  o.existence_probability = confidence;
  o.kinematics.pose_with_covariance.pose.position.x = x;
  o.kinematics.pose_with_covariance.pose.orientation.w = 1;
  o.shape.dimensions.x = 10;
  o.shape.dimensions.y = 4;
  autoware_perception_msgs::msg::ObjectClassification c;
  c.probability = 1;
  o.classification = {c};
  return o;
}

class FusionWeightTest : public testing::TestWithParam<int>
{
};
TEST_P(FusionWeightTest, SelectsAndNormalizesVelocityEstimate)
{
  auto p = parameters();
  p.velocity_weight_min_distance = 0;
  switch (GetParam()) {
    case 0:
      p.velocity_weight_min_distance = 2;
      break;
    case 1:
      p.velocity_weight_average = 2;
      break;
    case 2:
      p.velocity_weight_median = 2;
      break;
    case 3:
      p.velocity_weight_target_value_average = 2;
      break;
    case 4:
      p.velocity_weight_target_value_top = 2;
      break;
    default:
      break;
  }
  RadarFusionToDetectedObject fusion(rclcpp::get_logger("fusion_weights"));
  fusion.setParam(p);
  auto objects = std::make_shared<DetectedObjects>();
  objects->objects = {object()};
  auto radars = std::make_shared<std::vector<RadarFusionToDetectedObject::RadarInput>>();
  const double velocities[] = {2, 4, 10};
  for (int i = 0; i < 3; ++i) {
    RadarFusionToDetectedObject::RadarInput r;
    r.pose_with_covariance.pose.position.x = i;
    r.twist_with_covariance.twist.linear.x = velocities[i];
    r.twist_with_covariance.covariance[0] = 9;
    r.target_value = 1 << i;
    radars->push_back(r);
  }
  const auto out = fusion.update({radars, objects});
  ASSERT_EQ(out.objects.objects.size(), 1U);
  const double expected[] = {2, 16.0 / 3, 4, 50.0 / 7, 10, 2};
  const auto & k = out.objects.objects.front().kinematics;
  EXPECT_NEAR(k.twist_with_covariance.twist.linear.x, expected[GetParam()], 1e-6);
  EXPECT_DOUBLE_EQ(k.twist_with_covariance.covariance[0], 9);
  EXPECT_TRUE(k.has_twist);
  EXPECT_TRUE(k.has_twist_covariance);
}
INSTANTIATE_TEST_SUITE_P(Estimators, FusionWeightTest, testing::Range(0, 6));

class FusionRosTest : public RadarRosTest
{
protected:
  void SetUp() override
  {
    rclcpp::NodeOptions options;
    options.parameter_overrides(
      {{"node_params.update_rate_hz", 10.0},
       {"core_params.bounding_box_margin", 0.0},
       {"core_params.split_threshold_velocity", 5.0},
       {"core_params.threshold_yaw_diff", 0.35},
       {"core_params.velocity_weight_min_distance", 1.0},
       {"core_params.velocity_weight_average", 0.0},
       {"core_params.velocity_weight_median", 0.0},
       {"core_params.velocity_weight_target_value_average", 0.0},
       {"core_params.velocity_weight_target_value_top", 0.0},
       {"core_params.convert_doppler_to_twist", false},
       {"core_params.threshold_probability", 0.35},
       {"core_params.compensate_probability", false}});
    node = attach(std::make_shared<RadarObjectFusionToDetectedObjectNode>(options));
    peer = attach(std::make_shared<rclcpp::Node>("fusion_peer"));
    const std::string prefix = "/radar_object_fusion_to_detected_object/";
    sub = peer->create_subscription<DetectedObjects>(
      prefix + "output/objects", 1, [&](DetectedObjects::ConstSharedPtr m) { output = m; });
    debug_sub = peer->create_subscription<DetectedObjects>(
      prefix + "debug/low_confidence_objects", 1,
      [&](DetectedObjects::ConstSharedPtr m) { debug = m; });
    objects_pub = peer->create_publisher<DetectedObjects>(prefix + "input/objects", 1);
    radars_pub = peer->create_publisher<DetectedObjects>(prefix + "input/radars", 1);
    objects.header.frame_id = radars.header.frame_id = "base_link";
    objects.header.stamp = radars.header.stamp = peer->now();
  }
  void publish()
  {
    objects_pub->publish(objects);
    radars_pub->publish(radars);
  }
  rclcpp::Node::SharedPtr node, peer;
  DetectedObjects objects, radars;
  DetectedObjects::ConstSharedPtr output, debug;
  rclcpp::Publisher<DetectedObjects>::SharedPtr objects_pub, radars_pub;
  rclcpp::Subscription<DetectedObjects>::SharedPtr sub, debug_sub;
};

TEST_F(FusionRosTest, FusesMatchedRadarAndSeparatesUnmatchedLowConfidence)
{
  objects.objects = {object(0, 0.2), object(30), object(100, 0.2)};
  auto radar = object();
  radar.kinematics.twist_with_covariance.twist.linear.x = 5;
  radar.kinematics.twist_with_covariance.covariance[0] = 2;
  radars.objects = {radar};
  ASSERT_TRUE(await([&] { return output && debug; }, [&] { publish(); }));
  ASSERT_EQ(output->objects.size(), 2U);
  ASSERT_EQ(debug->objects.size(), 1U);
  EXPECT_EQ(output->header, objects.header);
  EXPECT_DOUBLE_EQ(output->objects[0].kinematics.twist_with_covariance.twist.linear.x, 5);
  EXPECT_DOUBLE_EQ(output->objects[0].kinematics.twist_with_covariance.covariance[0], 2);
  EXPECT_DOUBLE_EQ(debug->objects[0].kinematics.pose_with_covariance.pose.position.x, 100);
  ASSERT_TRUE(
    node->set_parameter(rclcpp::Parameter("core_params.compensate_probability", true)).successful);
  ASSERT_TRUE(
    await([&] { return output->objects[0].existence_probability >= 0.35F; }, [&] { publish(); }));
}

TEST_F(FusionRosTest, PassesThroughWithoutRadarAndRejectsDifferentFrames)
{
  objects.objects = {object(0, 0.1)};
  radars.header.frame_id = "wrong_frame";
  EXPECT_FALSE(
    await([&] { return output != nullptr; }, [&] { publish(); }, std::chrono::milliseconds(250)));
  radars.header.frame_id = objects.header.frame_id;
  objects.header.stamp = radars.header.stamp = peer->now();
  ASSERT_TRUE(await([&] { return output != nullptr; }, [&] { publish(); }));
  EXPECT_EQ(*output, objects);
}
}  // namespace
