// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/time_utils/stopwatch.hpp>
#include <autoware/time_utils/time_utils.hpp>

#include <gtest/gtest.h>

#include <thread>

using namespace std::chrono_literals;
using autoware::time_utils::interpolate;

TEST(TimeUtils, ClampsInterpolationToEndpoints)
{
  EXPECT_EQ(interpolate(1s, 5s, -1), 1s);
  EXPECT_EQ(interpolate(1s, 5s, 0), 1s);
  EXPECT_EQ(interpolate(1s, 5s, 1), 5s);
  EXPECT_EQ(interpolate(1s, 5s, 2), 5s);
}

TEST(TimeUtils, InterpolatesDescendingAndNegativeTimes)
{
  EXPECT_EQ(interpolate(5s, 1s, 0.25), 4s);
  EXPECT_EQ(interpolate(-4s, 4s, 0.25), -2s);
  EXPECT_EQ(interpolate(-2s, -2s, 0.5), -2s);
}

TEST(TimeUtils, PreservesNanosecondResolutionWithDurationTruncation)
{
  EXPECT_EQ(interpolate(1ns, 5ns, 0.5), 3ns);
  EXPECT_EQ(interpolate(0ns, 3ns, 0.5), 1ns);
}

TEST(TimeUtils, StopwatchMeasuresUnitsAndRestarts)
{
  // Consume the namespace published by this snapshot without moving its API.
  autoware::common::autoware::time_utils::Stopwatch watch;
  std::this_thread::sleep_for(2ms);
  const auto first = watch.measure<std::chrono::nanoseconds>();
  const auto milliseconds = watch.measure<std::chrono::milliseconds>();
  const auto last = watch.measure<std::chrono::nanoseconds>();
  EXPECT_GE(first, 1ms);
  EXPECT_GE(milliseconds, std::chrono::duration_cast<std::chrono::milliseconds>(first));
  EXPECT_LE(milliseconds, std::chrono::duration_cast<std::chrono::milliseconds>(last));
  const auto before_restart = std::chrono::high_resolution_clock::now();
  watch.restart();
  const auto restarted = watch.measure<std::chrono::nanoseconds>();
  const auto after_restart = std::chrono::high_resolution_clock::now();
  EXPECT_GE(restarted, 0ns);
  EXPECT_LE(restarted, after_restart - before_restart);
}
