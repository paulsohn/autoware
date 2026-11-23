// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>
#include <autoware/agnocast_wrapper/runtime.hpp>
#include <autoware/agnocast_wrapper/tf2.hpp>
#include <autoware/imu_corrector/gyro_bias_estimator.hpp>
#include <autoware/imu_corrector/imu_corrector_core.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <future>
#include <thread>

TEST(ImuCorrectionTransport, AppliesBiasAndTransformsIntoOutputFrame)
{
  namespace wrapper = autoware::agnocast_wrapper;
  using Imu = sensor_msgs::msg::Imu;
  rclcpp::NodeOptions options;
  options.arguments(
    {"--ros-args", "-r", "input:=/imu_test/input", "-r", "output:=/imu_test/output"});
  options.parameter_overrides(
    {{"angular_velocity_offset_x", 1.0},
     {"angular_velocity_offset_y", 0.5},
     {"angular_velocity_offset_z", -1.0},
     {"angular_velocity_stddev_xx", 0.1},
     {"angular_velocity_stddev_yy", 0.2},
     {"angular_velocity_stddev_zz", 0.3},
     {"acceleration_stddev", 0.5}});
  auto corrector = std::make_shared<autoware::imu_corrector::ImuCorrector>(options);
  auto peer = std::make_shared<wrapper::Node>("imu_test_peer");
  wrapper::StaticTransformBroadcaster broadcaster(*peer);
  geometry_msgs::msg::TransformStamped transform;
  transform.header.frame_id = "base_link";
  transform.child_frame_id = "imu_test_sensor";
  transform.transform.rotation.z = std::sqrt(0.5);
  transform.transform.rotation.w = std::sqrt(0.5);
  broadcaster.sendTransform(transform);

  auto publisher = peer->create_publisher<Imu>("/imu_test/input", rclcpp::QoS(1));
  std::promise<Imu> received;
  auto result = received.get_future();
  bool delivered = false;
  auto subscription = peer->create_subscription<Imu>(
    "/imu_test/output", rclcpp::QoS(10),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Imu) & message) {
      if (!delivered) {
        delivered = true;
        received.set_value(*message);
      }
    });
  Imu input;
  input.header.frame_id = "imu_test_sensor";
  input.header.stamp.sec = 123;
  input.header.stamp.nanosec = 456;
  input.angular_velocity.x = 3.0;
  input.angular_velocity.y = 2.0;
  input.angular_velocity.z = 1.0;
  input.linear_acceleration.x = 1.0;

  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      publisher->publish(input);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    executor.cancel();
  };
  if (wrapper::use_agnocast()) {
    EXPECT_FALSE(rclcpp::ok());
    agnocast::AgnocastOnlySingleThreadedExecutor executor;
    executor.add_node(corrector->get_agnocast_node());
    executor.add_node(peer->get_agnocast_node());
    exchange(executor);
  } else {
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(corrector->get_rclcpp_node());
    executor.add_node(peer->get_rclcpp_node());
    exchange(executor);
  }
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = result.get();
  EXPECT_EQ(output.header.frame_id, "base_link");
  EXPECT_EQ(output.header.stamp, input.header.stamp);
  EXPECT_NEAR(output.angular_velocity.x, -1.5, 1e-9);
  EXPECT_NEAR(output.angular_velocity.y, 2.0, 1e-9);
  EXPECT_NEAR(output.angular_velocity.z, 2.0, 1e-9);
  EXPECT_NEAR(output.linear_acceleration.x, 0.0, 1e-9);
  EXPECT_NEAR(output.linear_acceleration.y, 1.0, 1e-9);
  for (int i : {0, 4, 8}) {
    EXPECT_NEAR(output.angular_velocity_covariance[i], 0.09, 1e-12);
    EXPECT_NEAR(output.linear_acceleration_covariance[i], 0.25, 1e-12);
  }
}

namespace autoware::imu_corrector
{
// Use the existing upstream friend seam to isolate the estimator's two TF directions.
class GyroBiasEstimatorTest
{
public:
  static void check_sensor_and_bias_frames()
  {
    namespace wrapper = autoware::agnocast_wrapper;
    rclcpp::NodeOptions options;
    options.arguments(
      {"--ros-args", "--params-file", std::getenv("IMU_CORRECTOR_PARAMS"), "--params-file",
       std::getenv("GYRO_BIAS_PARAMS"), "--params-file", std::getenv("GYRO_SCALE_PARAMS")});
    auto estimator = std::make_shared<GyroBiasEstimator>(options);
    estimator->timer_->cancel();
    auto peer = std::make_shared<wrapper::Node>("gyro_frame_test_peer");
    wrapper::StaticTransformBroadcaster broadcaster(*peer);
    geometry_msgs::msg::TransformStamped transform;
    transform.header.frame_id = "base_link";
    transform.child_frame_id = "gyro_test_sensor";
    transform.transform.rotation.z = std::sqrt(0.5);
    transform.transform.rotation.w = std::sqrt(0.5);
    broadcaster.sendTransform(transform);
    auto receive_tf = [&](auto & executor) {
      std::jthread spin([&] { executor.spin(); });
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
      while (
        !estimator->transform_listener_->get_latest_transform("base_link", "gyro_test_sensor") &&
        std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      executor.cancel();
    };
    if (wrapper::use_agnocast()) {
      EXPECT_FALSE(rclcpp::ok());
      agnocast::AgnocastOnlySingleThreadedExecutor executor;
      executor.add_node(estimator->get_agnocast_node());
      executor.add_node(peer->get_agnocast_node());
      receive_tf(executor);
    } else {
      rclcpp::executors::SingleThreadedExecutor executor;
      executor.add_node(estimator->get_rclcpp_node());
      executor.add_node(peer->get_rclcpp_node());
      receive_tf(executor);
    }
    ASSERT_TRUE(
      estimator->transform_listener_->get_latest_transform("base_link", "gyro_test_sensor"));
    for (int nanosec : {250000000, 500000000}) {
      auto input = std::make_shared<sensor_msgs::msg::Imu>();
      input->header.frame_id = "gyro_test_sensor";
      input->header.stamp.sec = 1;
      input->header.stamp.nanosec = nanosec;
      input->angular_velocity.x = 0.1;
      input->angular_velocity.y = 0.2;
      input->angular_velocity.z = 0.3;
      std::shared_ptr<const sensor_msgs::msg::Imu> constant = input;
      AUTOWARE_MESSAGE_CONST_SHARED_PTR(sensor_msgs::msg::Imu) wrapped{std::move(constant)};
      estimator->callback_imu(wrapped);
    }
    ASSERT_EQ(estimator->gyro_all_.size(), 2U);
    for (const auto & gyro : estimator->gyro_all_) {
      EXPECT_NEAR(gyro.vector.x, -0.2, 1e-9);
      EXPECT_NEAR(gyro.vector.y, 0.1, 1e-9);
      EXPECT_NEAR(gyro.vector.z, 0.3, 1e-9);
    }
    // Supply known base-frame samples so the inverse check cannot cancel a forward error.
    for (auto & gyro : estimator->gyro_all_) {
      gyro.vector.x = -0.2;
      gyro.vector.y = 0.1;
      gyro.vector.z = 0.3;
    }
    for (int sec : {1, 2}) {
      geometry_msgs::msg::PoseStamped pose;
      pose.header.stamp.sec = sec;
      pose.pose.orientation.w = 1.0;
      estimator->pose_buf_.push_back(pose);
    }
    estimator->timer_callback();
    ASSERT_TRUE(estimator->gyro_bias_.has_value());
    EXPECT_NEAR(estimator->gyro_bias_->x, 0.1, 1e-9);
    EXPECT_NEAR(estimator->gyro_bias_->y, 0.2, 1e-9);
    EXPECT_NEAR(estimator->gyro_bias_->z, 0.3, 1e-9);
  }
};
}  // namespace autoware::imu_corrector

TEST(ImuEstimatorFrames, RotatesMeasurementsToBaseAndBiasBackToSensor)
{
  autoware::imu_corrector::GyroBiasEstimatorTest::check_sensor_and_bias_frames();
}

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  autoware::agnocast_wrapper::init(argc, argv, true);
  const int result = RUN_ALL_TESTS();
  autoware::agnocast_wrapper::shutdown();
  return result;
}
