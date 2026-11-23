#pragma once
#include <rmw/error_handling.h>
#include <rmw/event.h>
#include <rmw/init.h>
#include <rmw/rmw.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace standalone
{
inline constexpr char id[] = "rmw_agnocast";
struct Context
{
  std::mutex mutex;
  std::condition_variable changed;
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
bool rpc_ready(void * endpoint);
rmw_publisher_t * rpc_writer(const rmw_node_t *, const char *, const rmw_qos_profile_t &);
rmw_subscription_t * rpc_reader(const rmw_node_t *, const char *, const rmw_qos_profile_t &);

}  // namespace standalone
