// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <common/control_command_filter.hpp>

#include <gtest/gtest.h>

#include <cmath>

TEST(CommandGateFilter, PreservesSymmetricSteeringLimitAtHalfPi)
{
  using autoware::control_command_gate::Control;
  using autoware::control_command_gate::VehicleCmdFilter;
  using autoware::control_command_gate::VehicleCmdFilterParam;
  VehicleCmdFilterParam params{};
  params.reference_speed_points = {0.0};
  params.lon_acc_lim_for_lon_vel = params.lon_jerk_lim_for_lon_acc =
    params.lat_acc_lim_for_steer_cmd = params.lat_jerk_lim_for_steer_cmd = params.steer_cmd_lim =
      params.steer_rate_lim_for_steer_cmd = params.steer_cmd_diff_lim_from_current_steer = {2.0};
  VehicleCmdFilter filter;
  filter.setParam(params);
  for (const float direction : {-1.0F, 1.0F}) {
    Control command;
    command.lateral.steering_tire_angle = direction * 2.0F;
    filter.limitLateralSteer(command);
    EXPECT_FLOAT_EQ(command.lateral.steering_tire_angle, direction * std::acos(0.0F));
  }
}
