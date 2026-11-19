// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>

#include <tier4_perception_msgs/msg/detected_objects_with_feature.hpp>

#include <gtest/gtest.h>

TEST(Tier4Perception, FeatureRoundtrip)
{
  using Message = tier4_perception_msgs::msg::DetectedObjectsWithFeature;
  Message source;
  source.header.frame_id = "camera";
  source.header.stamp.sec = 42;
  source.feature_objects.resize(1);
  auto & feature = source.feature_objects[0];
  feature.feature.roi.x_offset = 17;
  feature.feature.roi.width = 23;
  feature.object.existence_probability = 0.75F;
  feature.object.classification.resize(1);
  feature.object.classification[0].label = 1;
  rclcpp::Serialization<Message> serializer;
  rclcpp::SerializedMessage wire;
  serializer.serialize_message(&source, &wire);
  Message restored;
  serializer.deserialize_message(&wire, &restored);
  EXPECT_EQ(source, restored);
}
