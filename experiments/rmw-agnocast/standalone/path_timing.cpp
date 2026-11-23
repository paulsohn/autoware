// Diagnostic interposer for the single-threaded 60-frame Path benchmark.
#include <dlfcn.h>
#include <poll.h>
#include <pthread.h>
#include <rcl/rcl.h>
#include <time.h>

#include <atomic>
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
#ifdef PATH_WAIT_DETAIL
struct WaitTimes
{
  int64_t wake, rmw_done;
};
thread_local bool in_rmw;
thread_local WaitTimes current_wait{};
WaitTimes waits[60];
struct PollCounts
{
  unsigned calls{}, context_only{};
};
thread_local PollCounts since_take{};
PollCounts poll_counts[60];
#endif
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
#ifdef PATH_WAIT_DETAIL
  if (auto * path = std::getenv("PATH_POLL_COUNTS")) {
    f = std::fopen(path, "wx");
    if (!f) std::abort();
    for (size_t i = 0; i < count; ++i)
      std::fprintf(f, "%zu,%u,%u\n", i, poll_counts[i].calls, poll_counts[i].context_only);
    std::fclose(f);
  }
  if (auto * path = std::getenv("PATH_WAIT_TIMES")) {
    f = std::fopen(path, "wx");
    if (!f) std::abort();
    for (size_t i = 0; i < count; ++i)
      std::fprintf(f, "%ld,%ld\n", waits[i].wake, waits[i].rmw_done);
    std::fclose(f);
  }
#endif
}
}  // namespace
#ifdef PATH_WAIT_DETAIL
// Record the blocking primitive on the thread executing the current ROS wait set.
extern "C" int pthread_cond_wait(pthread_cond_t * condition, pthread_mutex_t * mutex)
{
  static auto real =
    reinterpret_cast<decltype(&pthread_cond_wait)>(dlsym(RTLD_NEXT, "pthread_cond_wait"));
  auto ret = real(condition, mutex);
  if (in_rmw && ret == 0) current_wait.wake = now(CLOCK_REALTIME);
  return ret;
}
extern "C" int ppoll(pollfd * fds, nfds_t count, const timespec * timeout, const sigset_t * mask)
{
  static auto real = reinterpret_cast<decltype(&ppoll)>(dlsym(RTLD_NEXT, "ppoll"));
  auto ret = real(fds, count, timeout, mask);
  if (in_rmw) {
    if (ret > 0) current_wait.wake = now(CLOCK_REALTIME);
    ++since_take.calls;
    // The standalone mixed wait appends the context eventfd after all native routes.
    if (count && ret == 1 && (fds[count - 1].revents & POLLIN)) ++since_take.context_only;
  }
  return ret;
}
extern "C" rmw_ret_t rmw_wait(
  rmw_subscriptions_t * subs, rmw_guard_conditions_t * guards, rmw_services_t * services,
  rmw_clients_t * clients, rmw_events_t * events, rmw_wait_set_t * set, const rmw_time_t * timeout)
{
  static auto real = reinterpret_cast<decltype(&rmw_wait)>(dlsym(RTLD_NEXT, "rmw_wait"));
  in_rmw = true;
  current_wait = {};
  auto ret = real(subs, guards, services, clients, events, set, timeout);
  current_wait.rmw_done = now(CLOCK_REALTIME);
  in_rmw = false;
  return ret;
}
#endif
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
#ifdef PATH_WAIT_DETAIL
    waits[count] = current_wait;
    poll_counts[count] = since_take;
    since_take = {};
#endif
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

// Native receive runs on a different thread from the CIE callback in the default RMW.
namespace
{
struct LoanTime
{
  uint32_t wake_id, loan_id;
  int64_t entry;
  uintptr_t address;
  int64_t wake, acquired;
};
LoanTime loans[1024];
std::atomic<size_t> loan_count{};
thread_local uint32_t wake_id;
thread_local int64_t wake_time;
__attribute__((destructor)) void save_loans()
{
  if (!loan_count) return;
  auto * f = std::fopen(std::getenv("PATH_LOAN_TIMES"), "wx");
  if (!f) std::abort();
  for (size_t i = 0; i < loan_count; ++i) {
    const auto & r = loans[i];
    std::fprintf(
      f, "%u,%u,%ld,%lu,%ld,%ld\n", r.wake_id, r.loan_id, r.entry, r.address, r.wake, r.acquired);
  }
  std::fclose(f);
}
}  // namespace
extern "C" void path_timing_wake(uint32_t id)
{
  wake_time = now(CLOCK_MONOTONIC);
  wake_id = id;
}
extern "C" void path_timing_loan(uint32_t id, int64_t entry, uintptr_t address)
{
  auto acquired = now(CLOCK_MONOTONIC);
  auto i = loan_count.fetch_add(1, std::memory_order_relaxed);
  if (i >= sizeof(loans) / sizeof(loans[0])) std::abort();
  loans[i] = {wake_id, id, entry, address, wake_time, acquired};
}
