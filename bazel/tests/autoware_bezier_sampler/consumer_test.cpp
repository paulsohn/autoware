// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <autoware_bezier_sampler/bezier_sampling.hpp>

#include <gtest/gtest.h>

#include <cmath>

TEST(BezierConsumer, BoundaryStatesAndSampling)
{
  const Eigen::Vector2d start{0.0, 0.0}, goal{10.0, 2.0};
  const Eigen::Vector2d tangent{5.0, 0.0}, zero{0.0, 0.0};
  const auto curve = autoware::bezier_sampler::generate(start, goal, tangent, zero, tangent, zero);
  EXPECT_TRUE(curve.value(0.0).isApprox(start));
  EXPECT_TRUE(curve.valueM(1.0).isApprox(goal));
  EXPECT_TRUE(curve.velocity(0.0).isApprox(tangent));
  EXPECT_TRUE(curve.velocity(1.0).isApprox(tangent));
  EXPECT_NEAR(curve.acceleration(0.0).norm(), 0.0, 1e-10);
  EXPECT_NEAR(curve.acceleration(1.0).norm(), 0.0, 1e-10);
  const auto points = curve.cartesianWithHeading(9);
  ASSERT_EQ(points.size(), 9U);
  EXPECT_NEAR(points.front().x(), 0.0, 1e-10);
  EXPECT_NEAR(points.back().x(), 10.0, 1e-10);
  for (int i = 0; i <= 8; ++i) {
    const double t = i / 8.0;
    EXPECT_TRUE(curve.value(t).isApprox(curve.valueM(t)));
    EXPECT_TRUE(std::isfinite(curve.curvature(t)));
  }
}

TEST(BezierConsumer, CandidateFamily)
{
  autoware::sampler_common::State start{}, goal{};
  goal.pose = {10.0, 2.0};
  const auto curves = autoware::bezier_sampler::sample(
    start, goal, autoware::bezier_sampler::SamplingParameters{2, 0.5, 1.0, 2, 0.0, 0.5});
  ASSERT_EQ(curves.size(), 8U);
  for (const auto & curve : curves) {
    EXPECT_NEAR(curve.valueM(0.0).norm(), 0.0, 1e-10);
    EXPECT_TRUE(curve.valueM(1.0).isApprox(Eigen::Vector2d{10.0, 2.0}));
    EXPECT_NEAR(curve.heading(0.0), 0.0, 1e-10);
    EXPECT_NEAR(curve.heading(1.0), 0.0, 1e-10);
  }
}
