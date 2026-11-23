// Diagnostic interposer for the single-threaded 60-frame Path benchmark.
#include <dlfcn.h>
#include <rcl/rcl.h>
#include <time.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
struct Row
{
  int64_t sequence, published, queued, wait_done, take_begin, take_done, steady;
};
Row rows[60];
size_t count;
thread_local int64_t wait_done;
int64_t now(clockid_t clock)
{
  timespec t{};
  clock_gettime(clock, &t);
  return t.tv_sec * 1000000000LL + t.tv_nsec;
}
__attribute__((destructor)) void save()
{
  if (!count) return;
  auto * f = std::fopen(std::getenv("PATH_TIMING_FILE"), "wx");
  if (!f) std::abort();
  for (size_t i = 0; i < count; ++i) {
    const auto & r = rows[i];
    std::fprintf(
      f, "%ld,%ld,%ld,%ld,%ld,%ld,%ld\n", r.sequence, r.published, r.queued, r.wait_done,
      r.take_begin, r.take_done, r.steady);
  }
  std::fclose(f);
}
}  // namespace
extern "C" rcl_ret_t rcl_wait(rcl_wait_set_t * set, int64_t timeout)
{
  static auto real = reinterpret_cast<decltype(&rcl_wait)>(dlsym(RTLD_NEXT, "rcl_wait"));
  auto ret = real(set, timeout);
  wait_done = now(CLOCK_REALTIME);
  return ret;
}
extern "C" rcl_ret_t rcl_take_loaned_message(
  const rcl_subscription_t * sub, void ** message, rmw_message_info_t * info,
  rmw_subscription_allocation_t * allocation)
{
  static auto real = reinterpret_cast<decltype(&rcl_take_loaned_message)>(
    dlsym(RTLD_NEXT, "rcl_take_loaned_message"));
  auto begin = now(CLOCK_REALTIME);
  auto ret = real(sub, message, info, allocation);
  auto done = now(CLOCK_REALTIME);
  auto steady = now(CLOCK_MONOTONIC);
  if (
    ret == RCL_RET_OK && info &&
    std::strcmp(rcl_subscription_get_topic_name(sub), "/path_latency") == 0) {
    if (count >= 60) std::abort();
    rows[count++] = {
      static_cast<int64_t>(info->publication_sequence_number),
      info->source_timestamp,
      info->received_timestamp,
      wait_done,
      begin,
      done,
      steady};
  }
  return ret;
}
