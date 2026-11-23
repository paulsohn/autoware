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
}  // namespace standalone
