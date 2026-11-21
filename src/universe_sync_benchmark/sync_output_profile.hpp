#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace sync_output_profile
{
// The blocking executor builds and publishes each group on the same thread.
inline thread_local int64_t reserve_ns{}, append_ns{};
inline int64_t now()
{
  static const bool enabled = std::getenv("SYNC_OUTPUT_PROFILE") != nullptr;
  if (!enabled) return 0;
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
           std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void report(unsigned bytes, int64_t start)
{
  const auto publish_ns = now() - start;
  std::fprintf(stderr, "sync_output bytes=%u reserve_ns=%ld append_ns=%ld publish_ns=%ld\n",
    bytes, long(reserve_ns), long(append_ns), long(publish_ns));
}
}
