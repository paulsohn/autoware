#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace lidar_trace
{
inline bool enabled()
{
  static const bool active = std::getenv("SYNC_STAGE_TRACE") != nullptr;
  return active;
}
inline void mark(const char * stage, int64_t stamp)
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  std::fprintf(stderr, "lidar_stage,%s,%lld,%lld\n", stage, static_cast<long long>(stamp),
    static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()));
}
}
