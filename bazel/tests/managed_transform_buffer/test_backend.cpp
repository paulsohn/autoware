// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <autoware/agnocast_wrapper/node.hpp>
#include <autoware/agnocast_wrapper/runtime.hpp>
#include <autoware/agnocast_wrapper/tf2.hpp>
#include <managed_transform_buffer/managed_transform_buffer.hpp>

#include <gtest/gtest.h>

#include <chrono>

TEST(ManagedTransformBackend, ReceivesStaticTransformWithSelectedContext)
{
  namespace wrapper = autoware::agnocast_wrapper;
  EXPECT_EQ(rclcpp::ok(), !wrapper::use_agnocast());
  wrapper::Node node("managed_transform_backend_publisher");
  wrapper::StaticTransformBroadcaster broadcaster(node);
  geometry_msgs::msg::TransformStamped transform;
  transform.header.frame_id = "backend_map";
  transform.child_frame_id = "backend_lidar";
  transform.transform.translation.x = 3.5;
  transform.transform.rotation.w = 1.0;
  broadcaster.sendTransform(transform);

  managed_transform_buffer::ManagedTransformBuffer buffer;
  const auto result = buffer.getTransform<geometry_msgs::msg::TransformStamped>(
    "backend_map", "backend_lidar", tf2::TimePointZero, std::chrono::seconds(5));
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->transform.translation.x, 3.5);
  const auto inverse = buffer.getTransform<geometry_msgs::msg::TransformStamped>(
    "backend_lidar", "backend_map", tf2::TimePointZero, std::chrono::seconds(5));
  ASSERT_TRUE(inverse.has_value());
  EXPECT_DOUBLE_EQ(inverse->transform.translation.x, -3.5);
}

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  autoware::agnocast_wrapper::init(argc, argv, true);
  const int result = RUN_ALL_TESTS();
  autoware::agnocast_wrapper::shutdown();
  return result;
}
