// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/universe_utils/geometry/geometry.hpp>
#include <autoware_utils_geometry/geometry.hpp>

#include <gtest/gtest.h>

TEST(UniverseCoreGeometry, SharedStampedPoseConversion)
{
  geometry_msgs::msg::PoseStamped pose;
  pose.header.frame_id = "map";
  pose.header.stamp.sec = 7;
  pose.header.stamp.nanosec = 12300000;
  pose.pose.position.x = 1.0;
  pose.pose.position.y = -2.0;
  pose.pose.position.z = 3.0;
  pose.pose.orientation = autoware::universe_utils::createQuaternionFromRPY(0.1, 0.2, 0.3);

  tf2::Stamped<tf2::Transform> converted;
  tf2::fromMsg(pose, converted);
  EXPECT_EQ(converted.frame_id_, pose.header.frame_id);
  EXPECT_EQ(converted.stamp_, tf2_ros::fromMsg(pose.header.stamp));
  EXPECT_DOUBLE_EQ(converted.getOrigin().x(), pose.pose.position.x);
  EXPECT_DOUBLE_EQ(converted.getOrigin().y(), pose.pose.position.y);
  EXPECT_DOUBLE_EQ(converted.getOrigin().z(), pose.pose.position.z);
  EXPECT_NEAR(converted.getRotation().x(), pose.pose.orientation.x, 1e-12);
  EXPECT_NEAR(converted.getRotation().y(), pose.pose.orientation.y, 1e-12);
  EXPECT_NEAR(converted.getRotation().z(), pose.pose.orientation.z, 1e-12);
  EXPECT_NEAR(converted.getRotation().w(), pose.pose.orientation.w, 1e-12);

  const auto legacy_rpy = autoware::universe_utils::getRPY(pose.pose);
  const auto core_rpy = autoware_utils_geometry::get_rpy(pose.pose);
  EXPECT_DOUBLE_EQ(legacy_rpy.x, core_rpy.x);
  EXPECT_DOUBLE_EQ(legacy_rpy.y, core_rpy.y);
  EXPECT_DOUBLE_EQ(legacy_rpy.z, core_rpy.z);
}
