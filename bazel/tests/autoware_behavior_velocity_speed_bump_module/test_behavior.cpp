// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include "util.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace
{
namespace behavior = autoware::behavior_velocity_planner::speed_bump;
using Point = autoware_internal_planning_msgs::msg::PathPointWithLaneId;
using Trajectory = autoware::experimental::trajectory::Trajectory<Point>;
const lanelet::BasicPolygon2d polygon{{4, -1}, {6, -1}, {6, 1}, {4, 1}};

Trajectory path(double start, double end)
{
  std::vector<Point> points(11);
  for (size_t i = 0; i < points.size(); ++i) {
    points[i].point.pose.position.x = start + (end - start) * i / 10;
    points[i].point.pose.orientation.w = 1;
    points[i].point.longitudinal_velocity_mps = 3;
  }
  return Trajectory::Builder{}.build(points).value();
}

TEST(SpeedBump, CrossingReportsEntryAndExit)
{
  const auto result = behavior::getPathIntersectionWithSpeedBumpPolygon(path(0, 10), polygon, 2);
  ASSERT_TRUE(result.first_intersection_s);
  ASSERT_TRUE(result.second_intersection_s);
  EXPECT_NEAR(*result.first_intersection_s, 4, 1e-6);
  EXPECT_NEAR(*result.second_intersection_s, 6, 1e-6);
  EXPECT_FALSE(behavior::isNoRelation(result));
}

TEST(SpeedBump, EnteringAndExitingAreDistinguished)
{
  const auto entering = behavior::getPathIntersectionWithSpeedBumpPolygon(path(0, 5), polygon, 2);
  ASSERT_TRUE(entering.first_intersection_s);
  EXPECT_NEAR(*entering.first_intersection_s, 4, 1e-6);
  EXPECT_FALSE(entering.second_intersection_s);
  const auto exiting = behavior::getPathIntersectionWithSpeedBumpPolygon(path(5, 10), polygon, 2);
  EXPECT_FALSE(exiting.first_intersection_s);
  ASSERT_TRUE(exiting.second_intersection_s);
  EXPECT_NEAR(*exiting.second_intersection_s, 1, 1e-6);
}

TEST(SpeedBump, ContainedPathHasRelationWithoutBoundaryCrossing)
{
  const auto result = behavior::getPathIntersectionWithSpeedBumpPolygon(path(4.2, 5.8), polygon, 2);
  EXPECT_TRUE(result.is_path_inside_of_polygon);
  EXPECT_FALSE(result.first_intersection_s);
  EXPECT_FALSE(result.second_intersection_s);
  EXPECT_FALSE(behavior::isNoRelation(result));
}

TEST(SpeedBump, DisjointPathHasNoRelation)
{
  const auto result = behavior::getPathIntersectionWithSpeedBumpPolygon(path(8, 18), polygon, 2);
  EXPECT_TRUE(behavior::isNoRelation(result));
}

TEST(SpeedBump, HeightInterpolatesAndClampsSlowDownSpeed)
{
  geometry_msgs::msg::Point32 low, high;
  low.x = 0.05F;
  low.y = 2.78F;
  high.x = 0.30F;
  high.y = 1.39F;
  using autoware::behavior_velocity_planner::calcSlowDownSpeed;
  EXPECT_NEAR(calcSlowDownSpeed(low, high, 0.175F), 2.085F, 1e-5);
  EXPECT_FLOAT_EQ(calcSlowDownSpeed(low, high, 0.0F), low.y);
  EXPECT_FLOAT_EQ(calcSlowDownSpeed(low, high, 0.5F), high.y);
}
}  // namespace
