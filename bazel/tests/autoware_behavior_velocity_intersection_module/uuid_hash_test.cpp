// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/behavior_velocity_intersection_module/object_manager.hpp>

#include <gtest/gtest.h>

#include <unordered_map>

TEST(IntersectionObjectIdentity, AllUuidBytesParticipateInLookup)
{
  unique_identifier_msgs::msg::UUID base;
  for (size_t i = 0; i < base.uuid.size(); ++i) {
    base.uuid[i] = static_cast<uint8_t>(i * 13);
  }
  std::unordered_map<unique_identifier_msgs::msg::UUID, int> objects;
  objects.emplace(base, 42);
  const auto copy = base;
  EXPECT_EQ(objects.at(copy), 42);
  for (size_t i = 0; i < base.uuid.size(); ++i) {
    auto different = base;
    different.uuid[i] ^= 1;
    EXPECT_EQ(objects.find(different), objects.end());
    EXPECT_TRUE(objects.emplace(different, static_cast<int>(i)).second);
  }
  EXPECT_EQ(objects.size(), 17U);
}
