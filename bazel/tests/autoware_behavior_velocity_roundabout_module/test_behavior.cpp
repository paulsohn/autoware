// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include "decision_result.hpp"
#include "object_manager.hpp"

#include <gtest/gtest.h>
#include <lanelet2_core/primitives/Polygon.h>

#include <string>

namespace
{
namespace behavior = autoware::behavior_velocity_planner;
using UUID = unique_identifier_msgs::msg::UUID;

TEST(RoundaboutObjects, IdentityUsesEveryUuidByte)
{
  behavior::ObjectInfoManager manager;
  UUID original;
  const auto first = manager.registerObject(original, true);
  EXPECT_EQ(manager.registerObject(original, false), first);
  for (size_t i = 0; i < original.uuid.size(); ++i) {
    auto other = original;
    other.uuid[i] = 1;
    EXPECT_NE(manager.registerObject(other, false), first);
  }
  EXPECT_EQ(manager.getObjectsMap().size(), 17U);
  ASSERT_EQ(manager.attentionObjects().size(), 1U);
  EXPECT_EQ(manager.attentionObjects().front(), first);
  manager.clearObjects();
  EXPECT_TRUE(manager.getObjectsMap().empty());
  EXPECT_TRUE(manager.attentionObjects().empty());
}

TEST(RoundaboutObjects, ObservedSpeedAndSafetyIntervalReset)
{
  behavior::ObjectInfo object(UUID{});
  autoware_perception_msgs::msg::PredictedObject observation;
  observation.kinematics.initial_twist_with_covariance.twist.linear.x = 3.5;
  object.initialize(observation, std::nullopt);
  EXPECT_DOUBLE_EQ(object.observed_velocity(), 3.5);
  EXPECT_FALSE(object.estimated_past_position(1.0));
  behavior::CollisionInterval interval;
  interval.interval_position = {2, 4};
  interval.interval_time = {1.0, 2.0};
  object.update_safety(interval, std::nullopt);
  ASSERT_TRUE(object.unsafe_info());
  EXPECT_EQ(object.unsafe_info()->interval_position.first, 2U);
  object.initialize(observation, std::nullopt);
  EXPECT_FALSE(object.unsafe_info());
}

TEST(RoundaboutDecision, ReportsStateAndActivation)
{
  const auto error =
    behavior::formatDecisionResult(behavior::InternalError{"missing route"}, false);
  EXPECT_NE(error.find("InternalError because missing route"), std::string::npos);
  EXPECT_NE(error.find("activated_ = 0"), std::string::npos);
  const auto safe = behavior::formatDecisionResult(behavior::Safe{2, 3}, true);
  EXPECT_NE(safe.find("Safe:"), std::string::npos);
  EXPECT_NE(safe.find("activated_ = 1"), std::string::npos);
  EXPECT_NE(
    behavior::formatDecisionResult(behavior::CollisionStop{2, 3}, false).find("CollisionStop:"),
    std::string::npos);
  const auto passed =
    behavior::formatDecisionResult(behavior::OverPassJudge{"safe", "evasive"}, true);
  EXPECT_NE(passed.find("safety_report:safe"), std::string::npos);
  EXPECT_NE(passed.find("evasive_report:evasive"), std::string::npos);
}

TEST(RoundaboutCollision, PredictsCrossingAndRejectsDisjointPath)
{
  autoware_perception_msgs::msg::PredictedPath path;
  path.time_step.sec = 0;
  path.time_step.nanosec = 500000000;
  for (int x = 0; x <= 10; ++x) {
    geometry_msgs::msg::Pose pose;
    pose.position.x = x;
    pose.orientation.w = 1;
    path.path.push_back(pose);
  }
  autoware_perception_msgs::msg::Shape shape;
  shape.type = autoware_perception_msgs::msg::Shape::BOUNDING_BOX;
  shape.dimensions.x = 0.2;
  shape.dimensions.y = 0.2;
  shape.dimensions.z = 1;
  const lanelet::BasicPolygon2d lane{{4, -2}, {6, -2}, {6, 2}, {4, 2}};
  const auto interval = behavior::findPassageInterval(path, shape, lane, std::nullopt);
  ASSERT_TRUE(interval);
  EXPECT_EQ(interval->interval_position.first, 3U);
  EXPECT_EQ(interval->interval_position.second, 7U);
  EXPECT_DOUBLE_EQ(interval->interval_time.first, 1.5);
  EXPECT_DOUBLE_EQ(interval->interval_time.second, 3.5);
  for (auto & pose : path.path) {
    pose.position.y = 5;
  }
  EXPECT_FALSE(behavior::findPassageInterval(path, shape, lane, std::nullopt));
  path.path.clear();
  EXPECT_FALSE(behavior::findPassageInterval(path, shape, lane, std::nullopt));
}
}  // namespace
