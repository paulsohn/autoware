#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <sched.h>
#include <rclcpp/callback_group.hpp>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <thread>

namespace crop_benchmark
{
// Build the timer before spin so callback-isolated executors see its group.
template<class Node, class Callback>
void add_competitor(Node & node, Callback & process)
{
  auto count = std::make_shared<std::atomic<unsigned>>(0);
  auto make_timer = [&](bool second) {
    auto group = node.create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    auto timer = node.create_wall_timer(std::chrono::milliseconds(5),
      [count, &node, second, done = std::make_shared<bool>(false)]() {
        if (*done || count->load() < 5) return;
        *done = true;
        if (std::getenv("BENCH_SECOND_TIMER")) {
          cpu_set_t allowed;
          if (sched_getaffinity(0, sizeof(allowed), &allowed)) std::abort();
          for (int i = 0; i < CPU_SETSIZE; ++i) {
            if (CPU_ISSET(i, &allowed)) std::fprintf(stderr, "timer_%d_allowed_cpu=%d\n", int(second), i);
          }
        }
        timespec before{}, after{};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &before)) std::abort();
        const char * label = second ? "second_stall" : "near_stall";
        std::fprintf(stderr, "%s_start_ns=%ld\n", label, long(node.now().nanoseconds()));
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
        while (std::chrono::steady_clock::now() < end) {}
        std::fprintf(stderr, "%s_end_ns=%ld\n", label, long(node.now().nanoseconds()));
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &after)) std::abort();
        std::fprintf(stderr, "%s_cpu_ns=%ld\n", second ? "second" : "competitor",
          (after.tv_sec - before.tv_sec) * 1000000000L + after.tv_nsec - before.tv_nsec);
      }, group);
    return std::make_pair(group, timer);
  };
  auto first = make_timer(false);
  auto second = std::getenv("BENCH_SECOND_TIMER") ? make_timer(true) : first;
  process = [previous = process, first, second, count, &node](auto msg) {
    std::fprintf(stderr, "near_entry_ns=%ld\n", long(node.now().nanoseconds()));
    ++*count;
    previous(msg);
  };
}

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
