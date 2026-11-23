// Fixed-graph diagnostic. Only the three explicitly named debug publishers may reuse a zero.
#include <rcl/publisher.h>
#include <dlfcn.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" rcl_ret_t rcl_publisher_get_subscription_count(const rcl_publisher_t * pub, size_t * count)
{
  static auto real = reinterpret_cast<decltype(&rcl_publisher_get_subscription_count)>(
    dlsym(RTLD_NEXT, "rcl_publisher_get_subscription_count"));
  const char * topic = rcl_publisher_get_topic_name(pub);
  const char * topics[] = {"/sync/left/debug/published_time", "/sync/right/debug/published_time",
                           "/sync/top/debug/published_time"};
  // The fixture container runs all three crop callbacks on its one executor thread.
  static thread_local unsigned calls[3]{};
  for (unsigned i = 0; topic && i < 3; ++i) {
    if (std::strcmp(topic, topics[i])) continue;
    auto n = ++calls[i];
    static const bool reuse = std::getenv("SYNC_COUNT_REUSE") != nullptr;
    bool query = !reuse || n <= 2 || n == (i == 2 ? 12u : 24u);
    auto start = std::chrono::steady_clock::now();
    if (query) {
      auto ret = real(pub, count);
      if (ret != RCL_RET_OK || *count != 0) std::abort();
    } else *count = 0;
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - start).count();
    std::fprintf(stderr, "crop_count,%u,%u,%d,%ld\n", i, n, int(query), long(ns));
    return RCL_RET_OK;
  }
  return real(pub, count);
}
