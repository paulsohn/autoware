// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include "util.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace
{
namespace behavior = autoware::behavior_velocity_planner::no_drivable_lane;
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

TEST(NoDrivableLane, CrossingReportsEntryAndExit)
{
  const auto result =
    behavior::getPathIntersectionWithNoDrivableLanePolygon(path(0, 10), polygon, 2);
  ASSERT_TRUE(result.first_intersection_s);
  ASSERT_TRUE(result.second_intersection_s);
  EXPECT_NEAR(*result.first_intersection_s, 4, 1e-6);
  EXPECT_NEAR(*result.second_intersection_s, 6, 1e-6);
  EXPECT_FALSE(result.is_path_inside_of_polygon);
  EXPECT_FALSE(result.is_first_path_point_inside_polygon);
}

TEST(NoDrivableLane, EnteringReportsOnlyEntry)
{
  const auto result =
    behavior::getPathIntersectionWithNoDrivableLanePolygon(path(0, 5), polygon, 2);
  ASSERT_TRUE(result.first_intersection_s);
  EXPECT_NEAR(*result.first_intersection_s, 4, 1e-6);
  EXPECT_FALSE(result.second_intersection_s);
}

TEST(NoDrivableLane, ExitingReportsOnlyExit)
{
  const auto result =
    behavior::getPathIntersectionWithNoDrivableLanePolygon(path(5, 10), polygon, 2);
  EXPECT_TRUE(result.is_first_path_point_inside_polygon);
  EXPECT_FALSE(result.first_intersection_s);
  ASSERT_TRUE(result.second_intersection_s);
  EXPECT_NEAR(*result.second_intersection_s, 1, 1e-6);
}

TEST(NoDrivableLane, ContainedPathIsRecognized)
{
  const auto result =
    behavior::getPathIntersectionWithNoDrivableLanePolygon(path(4.2, 5.8), polygon, 2);
  EXPECT_TRUE(result.is_path_inside_of_polygon);
  EXPECT_TRUE(result.is_first_path_point_inside_polygon);
  EXPECT_FALSE(result.first_intersection_s);
  EXPECT_FALSE(result.second_intersection_s);
}

TEST(NoDrivableLane, DisjointPathHasNoIntersection)
{
  const auto result =
    behavior::getPathIntersectionWithNoDrivableLanePolygon(path(8, 18), polygon, 2);
  EXPECT_FALSE(result.is_path_inside_of_polygon);
  EXPECT_FALSE(result.is_first_path_point_inside_polygon);
  EXPECT_FALSE(result.first_intersection_s);
  EXPECT_FALSE(result.second_intersection_s);
}
}  // namespace
