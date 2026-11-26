#pragma once
#include <poll.h>
#include <rmw/error_handling.h>
#include <rmw/event.h>
#include <rmw/init.h>
#include <rmw/rmw.h>
#include <rmw/validate_full_topic_name.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace standalone
{
inline constexpr char id[] = "rmw_agnocast";
template <class Handle>
rmw_ret_t check_initialized_handle(const Handle * handle)
{
  if (!handle || !handle->implementation_identifier) {
    RMW_SET_ERROR_MSG("expected an initialized RMW handle");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (handle->implementation_identifier != id) {
    RMW_SET_ERROR_MSG("handle belongs to a different middleware implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  return RMW_RET_OK;
}

inline bool valid_ros_topic(const char * name)
{
  int result;
  if (rmw_validate_full_topic_name(name, &result, nullptr) != RMW_RET_OK) return false;
  if (result != RMW_TOPIC_VALID) {
    RMW_SET_ERROR_MSG(rmw_full_topic_name_validation_result_string(result));
    return false;
  }
  return true;
}
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
  int gateway_lease = -1;
  int gateway_stop = -1;
  std::thread gateway_monitor;
  ~Context();
  bool stopped = false;
};
bool join_gateway(Context & context, size_t domain);
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
