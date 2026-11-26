// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/trajectory_modifier/acados_interface.hpp>

#include <gtest/gtest.h>

#include <cmath>

TEST(TemporalMPC, TracksStraightMotionWithGeneratedSolver)
{
  temporal_mpt::AcadosInterface solver;
  solver.set_parameters_all_stages({1.25, 1.25});
  const std::array<double, temporal_mpt::NX> initial = {0., 0., 0., 5.};
  solver.set_warm_start(initial, {0., 0.});
  for (size_t stage = 0; stage < temporal_mpt::N; ++stage) {
    solver.set_stage_reference(static_cast<int>(stage), {stage * .5, 0., 0., 5., 0., 0.});
  }
  solver.set_terminal_reference({temporal_mpt::N * .5, 0., 0., 5.});
  const auto solution = solver.get_control(initial);
  ASSERT_EQ(solution.status, 0) << solution.info;
  for (const auto & state : solution.xtraj) {
    for (double value : state) EXPECT_TRUE(std::isfinite(value));
    EXPECT_NEAR(state[1], 0., .01);
    EXPECT_NEAR(state[3], 5., .05);
  }
  for (const auto & control : solution.utraj) {
    for (double value : control) EXPECT_TRUE(std::isfinite(value));
  }
  EXPECT_NEAR(solution.xtraj.front()[0], 0., 1e-6);
  EXPECT_NEAR(solution.xtraj.back()[0], temporal_mpt::N * .5, .05);
}
