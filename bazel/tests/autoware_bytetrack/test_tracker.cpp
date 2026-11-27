// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/bytetrack/bytetrack.hpp>

#include <gtest/gtest.h>

using autoware::bytetrack::ByteTrack;
using autoware::bytetrack::Object;
using autoware::bytetrack::ObjectArray;

Object detection(int x, float score = .9F, int label = 1)
{
  Object object{};
  object.x_offset = x;
  object.y_offset = 20;
  object.width = 40;
  object.height = 30;
  object.score = score;
  object.type = label;
  return object;
}

TEST(ByteTrack, EmptyAndDistinctIdentities)
{
  ByteTrack tracker;
  ObjectArray input;
  EXPECT_TRUE(tracker.update_tracker(input).empty());
  input = {detection(20), detection(160)};
  auto output = tracker.update_tracker(input);
  ASSERT_EQ(output.size(), 2U);
  EXPECT_NE(output[0].track_id, output[1].track_id);
  EXPECT_NE(output[0].unique_id, output[1].unique_id);
  EXPECT_FALSE(output[0].unique_id.is_nil());
  EXPECT_FALSE(output[1].unique_id.is_nil());
}

TEST(ByteTrack, LowScoreAssociationKeepsIdentity)
{
  ByteTrack tracker;
  ObjectArray input{detection(20)};
  const auto initial = tracker.update_tracker(input);
  ASSERT_EQ(initial.size(), 1U);
  for (int x : {22, 24, 26}) {
    input = {detection(x, .3F)};
    auto output = tracker.update_tracker(input);
    ASSERT_EQ(output.size(), 1U);
    EXPECT_EQ(output[0].track_id, initial[0].track_id);
    EXPECT_EQ(output[0].unique_id, initial[0].unique_id);
    EXPECT_GT(output[0].width, 0);
    EXPECT_GT(output[0].height, 0);
  }
}

TEST(ByteTrack, LostTrackReactivatesWithinBuffer)
{
  ByteTrack tracker(3);
  ObjectArray input{detection(20)};
  const auto initial = tracker.update_tracker(input);
  ASSERT_EQ(initial.size(), 1U);
  input.clear();
  EXPECT_TRUE(tracker.update_tracker(input).empty());
  input = {detection(22)};
  const auto output = tracker.update_tracker(input);
  ASSERT_EQ(output.size(), 1U);
  EXPECT_EQ(output[0].unique_id, initial[0].unique_id);
}

TEST(ByteTrack, ExpiredTrackGetsNewIdentity)
{
  ByteTrack tracker(3);
  ObjectArray input{detection(20)};
  const auto initial = tracker.update_tracker(input);
  ASSERT_EQ(initial.size(), 1U);
  input.clear();
  for (int i = 0; i < 8; ++i) EXPECT_TRUE(tracker.update_tracker(input).empty());
  input = {detection(20)};
  auto output = tracker.update_tracker(input);
  if (output.empty()) output = tracker.update_tracker(input);
  ASSERT_EQ(output.size(), 1U);
  EXPECT_NE(output[0].track_id, initial[0].track_id);
  EXPECT_NE(output[0].unique_id, initial[0].unique_id);
}

TEST(ByteTrack, ClassificationChangesOnlyWithStrongerEvidence)
{
  ByteTrack tracker;
  ObjectArray input{detection(20)};
  const auto initial = tracker.update_tracker(input);
  ASSERT_EQ(initial.size(), 1U);
  input = {detection(20, .3F, 2)};
  auto output = tracker.update_tracker(input);
  ASSERT_EQ(output.size(), 1U);
  EXPECT_EQ(output[0].type, 1);
  input = {detection(20, .99F, 2)};
  output = tracker.update_tracker(input);
  ASSERT_EQ(output.size(), 1U);
  EXPECT_EQ(output[0].type, 2);
  EXPECT_EQ(output[0].unique_id, initial[0].unique_id);
}
