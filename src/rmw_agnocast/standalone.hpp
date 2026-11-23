#pragma once
#include <poll.h>
#include <rmw/error_handling.h>
#include <rmw/event.h>
#include <rmw/init.h>
#include <rmw/rmw.h>
#include <sys/eventfd.h>

#include <atomic>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

namespace standalone
{
inline constexpr char id[] = "rmw_agnocast";
struct Context
{
  std::mutex mutex;
  std::condition_variable changed;
  std::mutex signals_mutex;
  std::list<int> signals;
  void notify()
  {
    changed.notify_all();
    std::lock_guard lock(signals_mutex);
    for (int fd : signals) {
      while (eventfd_write(fd, 1) == -1 && errno == EINTR) {
      }
    }
  }
  bool stopped = false;
};
struct Guard
{
  Context * context;
  bool triggered = false;
};
inline Context * state(const rmw_context_t * ctx)
{
  return ctx && ctx->implementation_identifier == id ? reinterpret_cast<Context *>(ctx->impl)
                                                     : nullptr;
}
bool ready(void * subscription);
void append_routes(void * subscription, std::vector<pollfd> & descriptors);
void acquire_routes(void * subscription, const pollfd * descriptors, size_t count);
bool rpc_ready(void * endpoint);
rmw_publisher_t * rpc_writer(const rmw_node_t *, const char *, const rmw_qos_profile_t &);
rmw_subscription_t * rpc_reader(const rmw_node_t *, const char *, const rmw_qos_profile_t &);

}  // namespace standalone
