// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <autoware/perception_online_evaluator/metrics/detection_count.hpp>
#include <autoware/perception_online_evaluator/metrics_calculator.hpp>
#include <rclcpp/rclcpp.hpp>
#include <tf2_ros/buffer.hpp>
#include <tf2_ros/transform_listener.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

namespace
{
namespace diagnostics = autoware::perception_diagnostics;
using autoware_perception_msgs::msg::ObjectClassification;
using autoware_perception_msgs::msg::PredictedObject;
using autoware_perception_msgs::msg::PredictedObjects;

class TimeWindowTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    rclcpp::init(0, nullptr);
    clock = std::make_shared<rclcpp::Clock>(RCL_ROS_TIME);
    buffer = std::make_unique<tf2_ros::Buffer>(clock);
    listener = std::make_unique<tf2_ros::TransformListener>(*buffer);
    parameters = std::make_shared<diagnostics::Parameters>();
    parameters->prediction_time_horizons = {5.0};
    parameters->smoothing_window_size = 1;
    parameters->stopped_velocity_threshold = 0.1;
    parameters->detection_radius_list = {100.0};
    parameters->detection_height_list = {10.0};
    parameters->detection_count_purge_seconds = 10.0;
    parameters->objects_count_window_seconds = 10.0;
    auto & car = parameters->object_parameters[ObjectClassification::CAR];
    car.check_lateral_deviation = true;
    car.check_total_objects_count = true;
    car.check_average_objects_count = true;
    car.check_interval_average_objects_count = true;
  }

  void TearDown() override
  {
    listener.reset();
    buffer.reset();
    clock.reset();
    rclcpp::shutdown();
  }

  PredictedObjects frame(int64_t nanoseconds, uint8_t count = 1)
  {
    PredictedObjects message;
    message.header.frame_id = "base_link";
    message.header.stamp = rclcpp::Time(nanoseconds, RCL_ROS_TIME);
    for (uint8_t i = 0; i < count; ++i) {
      PredictedObject object;
      object.object_id.uuid[0] = i + 1;
      ObjectClassification classification;
      classification.label = ObjectClassification::CAR;
      classification.probability = 1.0;
      object.classification = {classification};
      object.kinematics.initial_pose_with_covariance.pose.orientation.w = 1.0;
      object.kinematics.initial_pose_with_covariance.pose.position.x = nanoseconds / 1.0e9;
      object.kinematics.initial_twist_with_covariance.twist.linear.x = 1.0;
      message.objects.push_back(object);
    }
    return message;
  }

  std::shared_ptr<diagnostics::Parameters> parameters;
  std::shared_ptr<rclcpp::Clock> clock;
  std::unique_ptr<tf2_ros::Buffer> buffer;
  std::unique_ptr<tf2_ros::TransformListener> listener;
};

TEST_F(TimeWindowTest, CountsIncludeTheWindowBoundaryAndPurgeOnlyOlderFrames)
{
  diagnostics::metrics::DetectionCounter counter(parameters);
  counter.addObjects(frame(0), *buffer);
  EXPECT_DOUBLE_EQ(
    counter.getAverageCount(10.0).at(ObjectClassification::CAR).at("r100.00_h10.00"), 1.0);
  counter.addObjects(frame(10000000000LL, 2), *buffer);
  EXPECT_DOUBLE_EQ(
    counter.getAverageCount(10.0).at(ObjectClassification::CAR).at("r100.00_h10.00"), 1.5);
  counter.addObjects(frame(10000000001LL, 3), *buffer);
  EXPECT_DOUBLE_EQ(
    counter.getAverageCount(10.0).at(ObjectClassification::CAR).at("r100.00_h10.00"), 2.5);
  EXPECT_DOUBLE_EQ(
    counter.getOverallAverageCount().at(ObjectClassification::CAR).at("r100.00_h10.00"), 2.5);
  counter.addObjects(frame(20000000001LL), *buffer);
  EXPECT_DOUBLE_EQ(
    counter.getAverageCount(10.0).at(ObjectClassification::CAR).at("r100.00_h10.00"), 2.0);
}

TEST_F(TimeWindowTest, PredictionWaitsForItsHorizonAndRetainsTheHistoryBoundary)
{
  diagnostics::MetricsCalculator calculator(parameters);
  calculator.setPredictedObjects(frame(0), *buffer);
  EXPECT_FALSE(calculator.calculate(diagnostics::Metric::lateral_deviation).has_value());
  calculator.setPredictedObjects(frame(5000000000LL), *buffer);
  EXPECT_TRUE(calculator.calculate(diagnostics::Metric::lateral_deviation).has_value());
  calculator.setPredictedObjects(frame(10000000000LL), *buffer);
  auto history = calculator.getHistoryPathMap();
  ASSERT_EQ(history.size(), 1U);
  ASSERT_EQ(history.begin()->second.first.size(), 3U);
  EXPECT_DOUBLE_EQ(history.begin()->second.first.front().position.x, 0.0);
  calculator.setPredictedObjects(frame(10000000001LL), *buffer);
  history = calculator.getHistoryPathMap();
  ASSERT_EQ(history.size(), 1U);
  ASSERT_FALSE(history.begin()->second.first.empty());
  EXPECT_DOUBLE_EQ(history.begin()->second.first.front().position.x, 5.0);
}
}  // namespace
