// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "vehicle_cmd_filter.hpp"

#include <gtest/gtest.h>

TEST(SteeringLimit, PreservesFloatHalfPiBoundary)
{
  using autoware::vehicle_cmd_gate::VehicleCmdFilter;
  using autoware::vehicle_cmd_gate::VehicleCmdFilterParam;
  VehicleCmdFilterParam parameters{};
  parameters.wheel_base = 2.8;
  parameters.vel_lim = 20.0;
  parameters.reference_speed_points = {0.0, 10.0};
  parameters.lon_acc_lim_for_lon_vel = {1.0, 1.0};
  parameters.lon_jerk_lim_for_lon_acc = {1.0, 1.0};
  parameters.lat_acc_lim_for_steer_cmd = {1.0, 1.0};
  parameters.lat_jerk_lim_for_steer_cmd = {1.0, 1.0};
  parameters.steer_cmd_lim = {3.0, 3.0};
  parameters.steer_rate_lim_for_steer_cmd = {1.0, 1.0};
  parameters.steer_cmd_diff_lim_from_current_steer = {1.0, 1.0};
  parameters.lat_jerk_lim_for_steer_rate = 1.0;
  VehicleCmdFilter filter;
  filter.setParam(parameters);
  filter.setCurrentSpeed(5.0);
  constexpr float half_pi = 1.57079632679489661923F;
  for (float angle : {-4.0F, 4.0F, -1.0F, 1.0F}) {
    autoware_control_msgs::msg::Control command;
    command.lateral.steering_tire_angle = angle;
    command.longitudinal.velocity = 7.0F;
    filter.limitLateralSteer(command);
    const float expected = angle < -half_pi ? -half_pi : angle > half_pi ? half_pi : angle;
    EXPECT_FLOAT_EQ(command.lateral.steering_tire_angle, expected);
    EXPECT_FLOAT_EQ(command.longitudinal.velocity, 7.0F);
  }
}
