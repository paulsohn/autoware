// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <autoware_path_sampler/path_generation.hpp>
#include <autoware_path_sampler/prepare_inputs.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
const autoware::sampler_common::transform::Spline2D reference(
  {0.0, 10.0, 20.0, 30.0, 40.0}, {0.0, 0.0, 0.0, 0.0, 0.0});

Parameters parameters()
{
  Parameters p;
  p.sampling.resolution = 0.5;
  p.sampling.target_lengths = {10.0};
  p.sampling.target_lateral_positions = {-1.0, 0.0, 1.0};
  p.sampling.frenet.target_lateral_velocities = {0.0};
  p.sampling.frenet.target_lateral_accelerations = {0.0};
  p.sampling.bezier = {2, 0.3, 1.7, 2, 0.0, 1.0};
  return p;
}

void expectFinitePath(const autoware::sampler_common::Path & path)
{
  ASSERT_GT(path.points.size(), 2U);
  ASSERT_EQ(path.points.size(), path.yaws.size());
  ASSERT_EQ(path.points.size(), path.curvatures.size());
  ASSERT_EQ(path.points.size(), path.lengths.size());
  EXPECT_GT(path.points.back().x(), 9.0);
  for (size_t i = 0; i < path.points.size(); ++i) {
    EXPECT_TRUE(std::isfinite(path.points[i].x()));
    EXPECT_TRUE(std::isfinite(path.points[i].y()));
    EXPECT_TRUE(std::isfinite(path.yaws[i]));
    EXPECT_TRUE(std::isfinite(path.curvatures[i]));
    if (i) EXPECT_GT(path.lengths[i], path.lengths[i - 1]);
  }
}
}  // namespace

TEST(PathSampler, FrenetAndBezierFamilies)
{
  autoware::sampler_common::State initial{};
  auto p = parameters();
  p.sampling.enable_frenet = true;
  auto paths = autoware::path_sampler::generateCandidatePaths(initial, reference, 0.0, p);
  ASSERT_EQ(paths.size(), 3U);
  for (size_t i = 0; i < paths.size(); ++i) {
    expectFinitePath(paths[i]);
    EXPECT_NEAR(paths[i].points.back().y(), p.sampling.target_lateral_positions[i], 1e-8);
  }
  p.sampling.enable_frenet = false;
  p.sampling.enable_bezier = true;
  paths = autoware::path_sampler::generateCandidatePaths(initial, reference, 0.0, p);
  ASSERT_FALSE(paths.empty());
  for (const auto & path : paths) {
    expectFinitePath(path);
    EXPECT_NEAR(path.points.front().x(), 0.0, 1e-8);
    EXPECT_NEAR(path.points.back().y(), 0.0, 1e-8);
  }
  p.sampling.enable_frenet = true;
  const auto both = autoware::path_sampler::generateCandidatePaths(initial, reference, 0.0, p);
  EXPECT_EQ(both.size(), paths.size() + 3U);
  p.sampling.enable_frenet = p.sampling.enable_bezier = false;
  EXPECT_TRUE(autoware::path_sampler::generateCandidatePaths(initial, reference, 0.0, p).empty());
}

TEST(PathSampler, ReuseLengthAndDrivableLateralTargets)
{
  autoware::sampler_common::State initial{};
  auto p = parameters();
  auto samples = autoware::path_sampler::prepareSamplingParameters(initial, 4.0, reference, p);
  ASSERT_EQ(samples.parameters.size(), 3U);
  EXPECT_NEAR(samples.parameters.front().target_state.position.s, 6.0, 1e-8);
  p.sampling.nb_target_lateral_positions = 3;
  p.constraints.ego_width = 2.0;
  p.constraints.drivable_polygons = {
    {{{0.0, -3.0}, {40.0, -3.0}, {40.0, 3.0}, {0.0, 3.0}, {0.0, -3.0}}}};
  samples = autoware::path_sampler::prepareSamplingParameters(initial, 0.0, reference, p);
  ASSERT_EQ(samples.parameters.size(), 5U);
  EXPECT_NEAR(samples.parameters[2].target_state.position.d, -2.0, 1e-8);
  EXPECT_NEAR(samples.parameters[3].target_state.position.d, 0.0, 1e-8);
  EXPECT_NEAR(samples.parameters[4].target_state.position.d, 2.0, 1e-8);
}
