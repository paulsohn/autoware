#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <thread>

namespace crop_benchmark
{
template<class Node>
void stall(Node & node)
{
  static const bool stall = std::getenv("BENCH_STALL_THIS_CROP") != nullptr;
  if (stall) {
    static unsigned callbacks = 0;  // One crop in the selected branch process.
    ++callbacks;
    if (callbacks == 5) std::fprintf(stderr, "near_stall_start_ns=%ld\n", static_cast<long>(node.now().nanoseconds()));
    if (callbacks >= 5 && callbacks <= 10) {
      const auto duration = std::chrono::milliseconds(150);
      if (std::getenv("BENCH_BUSY_NEAR")) {
        timespec before{}, after{};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &before)) std::abort();
        const auto until = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < until) {}
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &after)) std::abort();
        const long cpu_ns = (after.tv_sec - before.tv_sec) * 1000000000L + after.tv_nsec - before.tv_nsec;
        std::fprintf(stderr, "near_busy_cpu_ns=%ld\n", cpu_ns);
      } else {
        std::this_thread::sleep_for(duration);
      }
    }
    if (callbacks == 10) std::fprintf(stderr, "near_stall_end_ns=%ld\n", static_cast<long>(node.now().nanoseconds()));
  }
}
}  // namespace crop_benchmark
