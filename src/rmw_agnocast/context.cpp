#include "graph.hpp"

#include <agnocast/agnocast_utils.hpp>

#include <rcutils/logging.h>
#include <rcutils/strdup.h>
#include <rmw/domain_id.h>
#include <sys/eventfd.h>

#include <chrono>
#include <cstring>
#include <new>
#include <string>
using namespace standalone;

const char * rmw_get_implementation_identifier()
{
  return id;
}

rmw_ret_t rmw_set_log_severity(rmw_log_severity_t severity)
{
  switch (severity) {
    case RMW_LOG_SEVERITY_DEBUG:
    case RMW_LOG_SEVERITY_INFO:
    case RMW_LOG_SEVERITY_WARN:
    case RMW_LOG_SEVERITY_ERROR:
    case RMW_LOG_SEVERITY_FATAL:
      return rcutils_logging_set_logger_level("Agnocast", severity) == RCUTILS_RET_OK
               ? RMW_RET_OK
               : RMW_RET_ERROR;
    default:
      return RMW_RET_INVALID_ARGUMENT;
  }
}

namespace
{
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
}  // namespace

rmw_ret_t rmw_init_options_init(rmw_init_options_t * o, rcutils_allocator_t allocator)
{
  if (!o || o->implementation_identifier || !rcutils_allocator_is_valid(&allocator))
    return RMW_RET_INVALID_ARGUMENT;
  o->allocator = allocator;
  o->domain_id = RMW_DEFAULT_DOMAIN_ID;
  o->security_options = rmw_get_default_security_options();
  auto ret = rmw_discovery_options_init(&o->discovery_options, 0, &o->allocator);
  if (ret == RMW_RET_OK) o->implementation_identifier = id;
  return ret;
}

rmw_ret_t rmw_init_options_fini(rmw_init_options_t * o)
{
  if (auto ret = check_initialized_handle(o); ret != RMW_RET_OK) return ret;
  o->allocator.deallocate(o->enclave, o->allocator.state);
  auto ret = rmw_discovery_options_fini(&o->discovery_options);
  auto sec = rmw_security_options_fini(&o->security_options, &o->allocator);
  *o = {};
  return ret == RMW_RET_OK ? sec : ret;
}

rmw_ret_t rmw_init_options_copy(const rmw_init_options_t * src, rmw_init_options_t * dst)
{
  if (!dst || dst->implementation_identifier) return RMW_RET_INVALID_ARGUMENT;
  if (auto ret = check_initialized_handle(src); ret != RMW_RET_OK) return ret;
  rmw_init_options_t o{};
  o.implementation_identifier = id;
  o.instance_id = src->instance_id;
  o.domain_id = src->domain_id;
  o.allocator = src->allocator;
  auto ret =
    rmw_discovery_options_copy(&src->discovery_options, &o.allocator, &o.discovery_options);
  if (ret != RMW_RET_OK) return ret;
  ret = rmw_security_options_copy(&src->security_options, &o.allocator, &o.security_options);
  if (ret == RMW_RET_OK && src->enclave && !(o.enclave = rcutils_strdup(src->enclave, o.allocator)))
    ret = RMW_RET_BAD_ALLOC;
  if (ret != RMW_RET_OK) {
    (void)rmw_init_options_fini(&o);
    return ret;
  }
  *dst = o;
  return RMW_RET_OK;
}

rmw_ret_t rmw_init(const rmw_init_options_t * o, rmw_context_t * ctx)
{
  if (auto ret = check_initialized_handle(o); ret != RMW_RET_OK) return ret;
  if (!o->enclave || !ctx || ctx->impl) return RMW_RET_INVALID_ARGUMENT;
  if (agnocast::agnocast_fd < 0) {
    RMW_SET_ERROR_MSG(
      "native transport unavailable: preload the matching Agnocast heap hook and check "
      "/dev/agnocast");
    return RMW_RET_ERROR;
  }
  const auto native_domain = agnocast::get_registered_domain_id();
  if (native_domain == UINT32_MAX) {
    RMW_SET_ERROR_MSG("native process has no registered Agnocast application domain");
    return RMW_RET_ERROR;
  }
  const auto domain = o->domain_id == RMW_DEFAULT_DOMAIN_ID ? native_domain : o->domain_id;
  if (domain != native_domain) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "rmw_agnocast: requested ROS domain %zu, but this process is registered with Agnocast in "
      "domain %u. Start a separate process with ROS_DOMAIN_ID=%zu.", domain, native_domain, domain);
    return RMW_RET_UNSUPPORTED;
  }
  if (o->security_options.enforce_security) return RMW_RET_UNSUPPORTED;
  auto local = std::unique_ptr<Context>(new (std::nothrow) Context);
  if (!local) return RMW_RET_BAD_ALLOC;
  auto ret = rmw_init_options_copy(o, &ctx->options);
  if (ret != RMW_RET_OK) return ret;
  if (!join_gateway(*local, domain)) {
    (void)rmw_init_options_fini(&ctx->options);
    return RMW_RET_ERROR;
  }
  ctx->implementation_identifier = id;
  ctx->instance_id = o->instance_id;
  ctx->actual_domain_id = domain;
  ctx->impl = reinterpret_cast<rmw_context_impl_t *>(local.release());
  return RMW_RET_OK;
}

rmw_ret_t rmw_shutdown(rmw_context_t * ctx)
{
  if (auto ret = check_initialized_handle(ctx); ret != RMW_RET_OK) return ret;
  auto s = state(ctx);
  if (!s) return RMW_RET_INVALID_ARGUMENT;
  {
    std::lock_guard lock(s->mutex);
    s->stopped = true;
  }
  s->notify();
  return RMW_RET_OK;
}
rmw_ret_t rmw_context_fini(rmw_context_t * ctx)
{
  if (auto ret = check_initialized_handle(ctx); ret != RMW_RET_OK) return ret;
  auto s = state(ctx);
  if (!s || !s->stopped) return RMW_RET_INVALID_ARGUMENT;
  delete s;
  auto ret = rmw_init_options_fini(&ctx->options);
  *ctx = {};
  return ret;
}

rmw_guard_condition_t * rmw_create_guard_condition(rmw_context_t * ctx)
{
  if (!state(ctx)) return nullptr;
  auto guard = std::unique_ptr<Guard>(new (std::nothrow) Guard{state(ctx)});
  if (!guard) return nullptr;
  auto handle = new (std::nothrow) rmw_guard_condition_t{id, guard.get(), ctx};
  if (handle) guard.release();
  return handle;
}
rmw_ret_t rmw_destroy_guard_condition(rmw_guard_condition_t * guard)
{
  if (!guard || guard->implementation_identifier != id) return RMW_RET_INVALID_ARGUMENT;
  delete static_cast<Guard *>(guard->data);
  delete guard;
  return RMW_RET_OK;
}
rmw_ret_t rmw_trigger_guard_condition(const rmw_guard_condition_t * handle)
{
  if (!handle || handle->implementation_identifier != id) return RMW_RET_INVALID_ARGUMENT;
  auto guard = static_cast<Guard *>(handle->data);
  {
    std::lock_guard lock(guard->context->mutex);
    guard->triggered = true;
  }
  guard->context->notify();
  return RMW_RET_OK;
}
struct LocalNode
{
  std::string name, ns;
  std::unique_ptr<rmw_guard_condition_t, decltype(&rmw_destroy_guard_condition)> graph;
  Lease lease;
  GraphCache cache;
  GraphWatch watch;
};
standalone::GraphCache & standalone::graph_cache(const rmw_node_t * node)
{
  return static_cast<LocalNode *>(node->data)->cache;
}
rmw_node_t * rmw_create_node(rmw_context_t * ctx, const char * name, const char * ns)
{
  if (!state(ctx) || !name || !ns) return nullptr;
  try {
    auto node = std::unique_ptr<LocalNode>(
      new LocalNode{name, ns, {rmw_create_guard_condition(ctx), rmw_destroy_guard_condition}});
    if (!node->graph) return nullptr;
    auto handle = std::unique_ptr<rmw_node_t>(
      new rmw_node_t{id, node.get(), node->name.c_str(), node->ns.c_str(), ctx});
    node->lease.announce(handle.get(), 'N');
    node->watch.start(handle.get(), node->graph.get());
    node.release();
    return handle.release();
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return nullptr;
  }
}
rmw_ret_t rmw_destroy_node(rmw_node_t * node)
{
  if (!node || node->implementation_identifier != id) return RMW_RET_INVALID_ARGUMENT;
  delete static_cast<LocalNode *>(node->data);
  delete node;
  return RMW_RET_OK;
}
const rmw_guard_condition_t * rmw_node_get_graph_guard_condition(const rmw_node_t * node)
{
  return node && node->implementation_identifier == id
           ? static_cast<LocalNode *>(node->data)->graph.get()
           : nullptr;
}

namespace
{
struct WaitSignal
{
  Context & context;
  int fd = -1;

  int attach()
  {
    if (fd >= 0) return fd;
    std::lock_guard lock(context.signals_mutex);
    // Reserve the list entry first so an allocation failure cannot leak a descriptor.
    context.signals.push_back(-1);
    fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0)
      context.signals.pop_back();
    else
      context.signals.back() = fd;
    return fd;
  }
  ~WaitSignal()
  {
    if (fd < 0) return;
    std::lock_guard lock(context.signals_mutex);
    context.signals.remove(fd);
    close(fd);
  }
};

struct LocalWaitSet : rmw_wait_set_t
{
  WaitSignal signal;
  bool reuse = false;
  bool drain = false;
  explicit LocalWaitSet(Context * ctx) : rmw_wait_set_t{id, nullptr, ctx}, signal{*ctx}
  {
    if (auto value = std::getenv("AGNOCAST_RMW_REUSE_WAIT_FD")) {
      drain = std::string_view(value) == "drain";
      reuse = drain || std::string_view(value) == "1";
    }
  }
};
}  // namespace

rmw_wait_set_t * rmw_create_wait_set(rmw_context_t * ctx, size_t)
{
  if (!state(ctx)) return nullptr;
  return new (std::nothrow) LocalWaitSet(state(ctx));
}
rmw_ret_t rmw_destroy_wait_set(rmw_wait_set_t * set)
{
  if (auto ret = check_initialized_handle(set); ret != RMW_RET_OK) return ret;
  delete static_cast<LocalWaitSet *>(set);
  return RMW_RET_OK;
}
rmw_ret_t rmw_wait(
  rmw_subscriptions_t * subs, rmw_guard_conditions_t * guards, rmw_services_t * services,
  rmw_clients_t * clients, rmw_events_t * events, rmw_wait_set_t * set, const rmw_time_t * timeout)
{
  if (auto ret = check_initialized_handle(set); ret != RMW_RET_OK) return ret;

  auto s = static_cast<Context *>(set->data);
  std::unique_lock lock(s->mutex);
  auto active = [&] {
    if (s->stopped) return true;
    if (guards)
      for (size_t i = 0; i < guards->guard_condition_count; ++i)
        if (auto g = static_cast<Guard *>(guards->guard_conditions[i]); g && g->triggered)
          return true;
    if (events)
      for (size_t i = 0; i < events->event_count; ++i)
        if (event_ready(events->events[i])) return true;
    if (services)
      for (size_t i = 0; i < services->service_count; ++i)
        if (services->services[i] && rpc_ready(services->services[i])) return true;
    if (clients)
      for (size_t i = 0; i < clients->client_count; ++i)
        if (clients->clients[i] && rpc_ready(clients->clients[i])) return true;
    if (subs)
      for (size_t i = 0; i < subs->subscriber_count; ++i)
        if (subs->subscribers[i] && ready(subs->subscribers[i])) return true;
    return false;
  };
  bool awake = true;
  std::vector<pollfd> descriptors;
  std::vector<std::pair<size_t, void *>> starts;
  try {
    if (subs)
      for (size_t i = 0; i < subs->subscriber_count; ++i) {
        auto sub = subs->subscribers[i];
        if (!sub) continue;
        auto start = descriptors.size();
        append_routes(sub, descriptors);
        if (descriptors.size() > start) starts.emplace_back(start, sub);
      }
    if (!starts.empty()) {
      descriptors.push_back({-1, POLLIN, 0});
      auto & local = *static_cast<LocalWaitSet *>(set);
      WaitSignal transient{*s};
      int fd = (local.reuse ? local.signal : transient).attach();
      if (fd < 0) {
        RMW_SET_ERROR_MSG("wait eventfd failed");
        return RMW_RET_ERROR;
      }
      if (local.drain) {
        // Notifications are hints. Recheck every readiness predicate under the context lock
        // before blocking, including state changes represented by a token removed here.
        eventfd_t previous;
        int ret;
        do {
          ret = eventfd_read(fd, &previous);
        } while (ret < 0 && errno == EINTR);
        if (ret < 0 && errno != EAGAIN) {
          RMW_SET_ERROR_MSG("cannot clear wait eventfd");
          return RMW_RET_ERROR;
        }
      }
      descriptors.back().fd = fd;
      using Clock = std::chrono::steady_clock;
      auto start = Clock::now();
      auto end = Clock::time_point::max();
      if (
        timeout &&
        timeout->sec < static_cast<uint64_t>(
                         std::chrono::duration_cast<std::chrono::seconds>(end - start).count()))
        end = start + std::chrono::seconds(timeout->sec) + std::chrono::nanoseconds(timeout->nsec);
      while (true) {
        const bool already_ready = active();
        timespec ts{};
        if (!already_ready && timeout) {
          auto remaining = std::max(Clock::duration::zero(), end - Clock::now());
          auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
          ts = {ns / 1000000000, ns % 1000000000};
        }
        lock.unlock();
        int n = ppoll(
          descriptors.data(), descriptors.size(), timeout || already_ready ? &ts : nullptr,
          nullptr);
        if (n < 0 && errno != EINTR) {
          RMW_SET_ERROR_MSG("native ppoll failed");
          return RMW_RET_ERROR;
        }
        if (n > 0) {
          for (auto & d : descriptors)
            if (d.revents & (POLLERR | POLLHUP | POLLNVAL)) {
              RMW_SET_ERROR_MSG("invalid native wait descriptor");
              return RMW_RET_ERROR;
            }
          if (descriptors.back().revents & POLLIN) {
            eventfd_t count;
            while (eventfd_read(fd, &count) < 0 && errno == EINTR) {
            }
          }
          for (size_t i = 0; i < starts.size(); ++i) {
            size_t stop = i + 1 < starts.size() ? starts[i + 1].first : descriptors.size() - 1;
            auto first = starts[i].first;
            acquire_routes(starts[i].second, descriptors.data() + first, stop - first);
          }
        }
        lock.lock();
        awake = active();
        if (awake || (timeout && Clock::now() >= end)) break;
      }
    } else if (timeout)
      awake = s->changed.wait_for(
        lock, std::chrono::seconds(timeout->sec) + std::chrono::nanoseconds(timeout->nsec), active);
    else
      s->changed.wait(lock, active);
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("native wait allocation failed");
    return RMW_RET_BAD_ALLOC;
  } catch (const std::exception & error) {
    RMW_SET_ERROR_MSG(error.what());
    return RMW_RET_ERROR;
  }
  if (guards)
    for (size_t i = 0; i < guards->guard_condition_count; ++i) {
      auto g = static_cast<Guard *>(guards->guard_conditions[i]);
      if (!g || !g->triggered)
        guards->guard_conditions[i] = nullptr;
      else
        g->triggered = false;
    }
  if (subs)
    for (size_t i = 0; i < subs->subscriber_count; ++i)
      if (subs->subscribers[i] && !ready(subs->subscribers[i])) subs->subscribers[i] = nullptr;
  if (services)
    for (size_t i = 0; i < services->service_count; ++i)
      if (services->services[i] && !rpc_ready(services->services[i]))
        services->services[i] = nullptr;
  if (clients)
    for (size_t i = 0; i < clients->client_count; ++i)
      if (clients->clients[i] && !rpc_ready(clients->clients[i])) clients->clients[i] = nullptr;
  if (events)
    for (size_t i = 0; i < events->event_count; ++i)
      if (!event_ready(events->events[i])) events->events[i] = nullptr;
  return awake ? RMW_RET_OK : RMW_RET_TIMEOUT;
}

void standalone::observe_event(const rmw_node_t * node, const std::shared_ptr<Event> & event)
{
  auto & watch = static_cast<LocalNode *>(node->data)->watch;
  {
    std::lock_guard lock(watch.mutex);
    watch.listeners.push_back(event);
  }
  eventfd_write(watch.stop, 1);
}
