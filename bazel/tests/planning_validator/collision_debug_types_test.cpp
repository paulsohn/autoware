// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/planning_validator_intersection_collision_checker/types.hpp>
#include <structs.hpp>

#include <gtest/gtest.h>

#include <type_traits>

TEST(CollisionCheckerPlugins, DebugTypesHaveDistinctIdentities)
{
  using autoware::planning_validator::DebugData;
  using autoware::planning_validator::RearCollisionDebugData;
  static_assert(!std::is_same_v<DebugData, RearCollisionDebugData>);
  DebugData intersection;
  RearCollisionDebugData rear;
  rear.pointcloud_objects.emplace_back();
  EXPECT_EQ(rear.pointcloud_objects.size(), 1U);
  EXPECT_TRUE(intersection.ego_lanelets.turn_lanelets.empty());
}
