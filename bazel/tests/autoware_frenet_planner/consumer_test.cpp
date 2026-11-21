// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <autoware_frenet_planner/frenet_planner.hpp>
#include <autoware_frenet_planner/polynomials.hpp>
#include <autoware_sampler_common/transform/spline_transform.hpp>
#include <gtest/gtest.h>

#include <cmath>

TEST(FrenetConsumer, PolynomialBoundaryConditions)
{
  const autoware::frenet_planner::Polynomial polynomial(1.0, 2.0, 0.5, 10.0, 3.0, 0.0, 4.0);
  EXPECT_NEAR(polynomial.position(0.0), 1.0, 1e-8);
  EXPECT_NEAR(polynomial.velocity(0.0), 2.0, 1e-8);
  EXPECT_NEAR(polynomial.acceleration(0.0), 0.5, 1e-8);
  EXPECT_NEAR(polynomial.position(4.0), 10.0, 1e-8);
  EXPECT_NEAR(polynomial.velocity(4.0), 3.0, 1e-8);
  EXPECT_NEAR(polynomial.acceleration(4.0), 0.0, 1e-8);
}

TEST(FrenetConsumer, LaneChangeTrajectoryInCartesianFrame)
{
  using namespace autoware::frenet_planner;
  const autoware::sampler_common::transform::Spline2D reference(
    {0.0, 10.0, 20.0, 30.0, 40.0}, {0.0, 0.0, 0.0, 0.0, 0.0});
  FrenetState initial;
  initial.longitudinal_velocity = 5.0;
  FrenetState target = initial;
  target.position = {20.0, 3.0};
  const SamplingParameters sampling{{SamplingParameter{4.0, target}}, 0.25};
  const auto trajectories = generateTrajectories(reference, initial, sampling);
  ASSERT_EQ(trajectories.size(), 1U);
  const auto & trajectory = trajectories.front();
  ASSERT_EQ(trajectory.points.size(), 17U);
  ASSERT_EQ(trajectory.times.size(), trajectory.points.size());
  ASSERT_EQ(trajectory.poses.size(), trajectory.points.size());
  ASSERT_EQ(trajectory.curvatures.size(), trajectory.points.size());
  ASSERT_EQ(trajectory.longitudinal_velocities.size(), trajectory.points.size());
  EXPECT_NEAR(trajectory.points.front().x(), 0.0, 1e-8);
  EXPECT_NEAR(trajectory.points.front().y(), 0.0, 1e-8);
  EXPECT_NEAR(trajectory.points.back().x(), 20.0, 1e-8);
  EXPECT_NEAR(trajectory.points.back().y(), 3.0, 1e-8);
  EXPECT_NEAR(std::hypot(trajectory.longitudinal_velocities.front(),
                         trajectory.lateral_velocities.front()), 5.0, 1e-8);
  EXPECT_NEAR(trajectory.longitudinal_velocities.back(), 5.0, 1e-8);
  for (size_t i = 0; i < trajectory.points.size(); ++i) {
    EXPECT_NEAR(trajectory.points[i].x(), trajectory.frenet_points[i].s, 1e-8);
    EXPECT_NEAR(trajectory.points[i].y(), trajectory.frenet_points[i].d, 1e-8);
    EXPECT_TRUE(std::isfinite(trajectory.curvatures[i]));
  }
}
