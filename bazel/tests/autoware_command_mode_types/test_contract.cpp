// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware_command_mode_types/modes.hpp>
#include <autoware_command_mode_types/sources.hpp>

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <type_traits>

namespace modes = autoware::command_mode_types::modes;
namespace sources = autoware::command_mode_types::sources;

TEST(CommandModeTypes, ModeIdsAreDistinctAndRespectTheReservedDomain)
{
  static_assert(std::is_same_v<decltype(modes::autonomous), const uint16_t>);
  constexpr std::array ids = {
    modes::manual, modes::stop,           modes::autonomous,       modes::local,
    modes::remote, modes::emergency_stop, modes::comfortable_stop, modes::pull_over};
  EXPECT_EQ(modes::unknown, 0);
  EXPECT_EQ((std::set<uint16_t>(ids.begin(), ids.end()).size()), ids.size());
  for (const auto id : ids) EXPECT_GT(id, 9);
}

TEST(CommandModeTypes, SourceIdsAreDistinctAndRespectBuiltinReservations)
{
  static_assert(std::is_same_v<decltype(sources::main), const uint16_t>);
  constexpr std::array ids = {
    sources::stop, sources::main, sources::local, sources::remote, sources::emergency_stop};
  EXPECT_EQ(sources::unknown, 0);
  EXPECT_EQ(sources::builtin, 1);
  EXPECT_EQ((std::set<uint16_t>(ids.begin(), ids.end()).size()), ids.size());
  for (const auto id : ids) EXPECT_GT(id, 9);
}
