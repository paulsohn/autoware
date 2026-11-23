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

TEST(ManagedTransformBackend, EigenResultsRemainRigidForRoundedQuaternion)
{
  namespace wrapper = autoware::agnocast_wrapper;
  wrapper::Node node("managed_transform_rotation_publisher");
  wrapper::StaticTransformBroadcaster broadcaster(node);
  geometry_msgs::msg::TransformStamped transform;
  transform.header.frame_id = "rotation_map";
  transform.child_frame_id = "rotation_sensor";
  transform.transform.translation.x = 1;
  transform.transform.translation.y = 2;
  transform.transform.translation.z = 3;
  transform.transform.rotation.x = 0.278;
  transform.transform.rotation.y = 0.717;
  transform.transform.rotation.z = 0.441;
  transform.transform.rotation.w = 0.453;
  broadcaster.sendTransform(transform);
  const Eigen::Matrix3d expected =
    Eigen::Quaterniond(0.453, 0.278, 0.717, 0.441).normalized().toRotationMatrix();
  managed_transform_buffer::ManagedTransformBuffer buffer;
  const auto time = tf2::TimePointZero;
  const auto timeout = std::chrono::seconds(5);
  const auto matrix_f =
    buffer.getTransform<Eigen::Matrix4f>("rotation_map", "rotation_sensor", time, timeout);
  const auto matrix_d =
    buffer.getTransform<Eigen::Matrix4d>("rotation_map", "rotation_sensor", time, timeout);
  const auto affine_f =
    buffer.getTransform<Eigen::Affine3f>("rotation_map", "rotation_sensor", time, timeout);
  const auto affine_d =
    buffer.getTransform<Eigen::Affine3d>("rotation_map", "rotation_sensor", time, timeout);
  ASSERT_TRUE(matrix_f && matrix_d && affine_f && affine_d);
  auto check = [&](const auto & matrix, double tolerance) {
    EXPECT_LT(
      (matrix.template block<3, 3>(0, 0).template cast<double>() - expected).norm(), tolerance);
    EXPECT_NEAR(matrix(0, 3), 1.0, tolerance);
    EXPECT_NEAR(matrix(1, 3), 2.0, tolerance);
    EXPECT_NEAR(matrix(2, 3), 3.0, tolerance);
  };
  check(*matrix_f, 1e-6);
  check(*matrix_d, 1e-12);
  check(affine_f->matrix(), 1e-6);
  check(affine_d->matrix(), 1e-12);
}

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  autoware::agnocast_wrapper::init(argc, argv, true);
  const int result = RUN_ALL_TESTS();
  autoware::agnocast_wrapper::shutdown();
  return result;
}
