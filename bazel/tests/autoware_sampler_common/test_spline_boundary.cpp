// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <autoware_sampler_common/transform/spline_transform.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

TEST(SamplerSpline, DerivativesAtFirstAndLastKnots)
{
  using autoware::sampler_common::transform::Spline;
  const std::vector<double> knots{2.0, 4.0, 6.0, 8.0};
  const Spline linear(knots, {7.0, 13.0, 19.0, 25.0});
  for (const double query : {2.0, 2.001, 4.0, 6.0, 7.999, 8.0}) {
    EXPECT_NEAR(linear.velocity(query, knots), 3.0, 1e-8);
    EXPECT_NEAR(linear.acceleration(query, knots), 0.0, 1e-8);
  }
}

TEST(SamplerSpline, FiniteHeadingAndCurvatureAtReferenceOrigin)
{
  using autoware::sampler_common::transform::Spline2D;
  const Spline2D straight({0.0, 10.0, 20.0, 30.0}, {0.0, 0.0, 0.0, 0.0});
  for (const double s : {0.0, 0.001, 10.0, 30.0}) {
    EXPECT_NEAR(straight.yaw(s), 0.0, 1e-8);
    EXPECT_NEAR(straight.curvature(s), 0.0, 1e-8);
  }
  const Spline2D curved({0.0, 2.0, 4.0, 6.0}, {0.0, 1.0, 3.0, 6.0});
  EXPECT_TRUE(std::isfinite(curved.yaw(0.0)));
  EXPECT_TRUE(std::isfinite(curved.curvature(0.0)));
  EXPECT_NEAR(curved.yaw(0.0), curved.yaw(1e-7), 1e-6);
  EXPECT_NEAR(curved.curvature(0.0), curved.curvature(1e-7), 1e-6);
}
