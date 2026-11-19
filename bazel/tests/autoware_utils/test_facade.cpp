// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware_utils/autoware_utils.hpp>

#include <type_traits>

static_assert(std::is_same_v<autoware_utils::StopWatch<>, autoware_utils_system::StopWatch<>>);

int main()
{
  autoware_utils::StopWatch<> watch;
  return watch.toc() < 0;
}
