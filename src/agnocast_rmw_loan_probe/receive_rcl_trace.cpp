// Loaded only in the first crop process of the opt-in receive profile.
#include <rcl/subscription.h>
#include <rcl/wait.h>
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <sched.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>

namespace {
struct Trace {
  struct Event {const char * name; int64_t ns; int cpu;};
  std::array<Event, 4096> events{};
  std::atomic<size_t> count{0};
  ~Trace() {
    const char * path = std::getenv("BENCH_RCL_EVENTS");
    if (!path) return;
    std::ofstream out(path);
    for (size_t i = 0; i < std::min(count.load(), events.size()); ++i)
      out << events[i].name << ',' << events[i].ns << ',' << events[i].cpu << '\n';
  }
};
void mark(const char * name, int cpu = -1) {
  static Trace trace;
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  auto i = trace.count.fetch_add(1, std::memory_order_relaxed);
  if (i < trace.events.size())
    trace.events[i] = {name, std::chrono::duration_cast<std::chrono::nanoseconds>(now).count(), cpu};
}
}

extern "C" rcl_ret_t rcl_wait(rcl_wait_set_t * set, int64_t timeout) {
  static auto next = reinterpret_cast<decltype(&rcl_wait)>(dlsym(RTLD_NEXT, "rcl_wait"));
  if (!next) std::abort();
  auto ret = next(set, timeout);
  if (ret == RCL_RET_OK) {
    static const bool locate = std::getenv("BENCH_RECEIVE_CPU") != nullptr;
    mark("rcl_wait_return", locate ? sched_getcpu() : -1);
  }
  return ret;
}
extern "C" rcl_ret_t rcl_take(const rcl_subscription_t * sub, void * message,
  rmw_message_info_t * info, rmw_subscription_allocation_t * alloc) {
  static auto next = reinterpret_cast<decltype(&rcl_take)>(dlsym(RTLD_NEXT, "rcl_take"));
  if (!next) std::abort();
  mark("rcl_take_entry");
  return next(sub, message, info, alloc);
}
extern "C" bool rcl_subscription_can_loan_messages(const rcl_subscription_t * sub) {
  static auto next = reinterpret_cast<decltype(&rcl_subscription_can_loan_messages)>(
    dlsym(RTLD_NEXT, "rcl_subscription_can_loan_messages"));
  if (!next) std::abort();
  mark("loan_check_in");
  const bool value = next(sub);
  mark("loan_check_out");
  return value;
}

#ifdef BENCH_EXECUTOR_SPLIT
// The diagnostic interposer targets the pinned Lyrical x86-64 C++ ABI.
#include <rclcpp/executor.hpp>
bool rclcpp::Executor::get_next_ready_executable(AnyExecutable & executable) {
  using Next = bool (*)(Executor *, AnyExecutable &);
  static auto next = reinterpret_cast<Next>(dlsym(RTLD_NEXT,
    "_ZN6rclcpp8Executor25get_next_ready_executableERNS_13AnyExecutableE"));
  if (!next) std::abort();
  mark("select_in");
  bool ready = next(this, executable);
  if (ready && executable.subscription) mark("selected_sub");
  return ready;
}
void rclcpp::Executor::execute_subscription(const rclcpp::SubscriptionBase::SharedPtr & sub) {
  static auto next = reinterpret_cast<decltype(&execute_subscription)>(dlsym(RTLD_NEXT,
    "_ZN6rclcpp8Executor20execute_subscriptionERKSt10shared_ptrINS_16SubscriptionBaseEE"));
  if (!next) std::abort();
  mark("execute_sub");
  next(sub);
}
#endif
