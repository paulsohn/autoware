#include "events.hpp"

#include <algorithm>
#include <utility>
using namespace standalone;
namespace
{
bool is_match(rmw_event_type_t k)
{
  return k == RMW_EVENT_SUBSCRIPTION_MATCHED || k == RMW_EVENT_PUBLICATION_MATCHED ||
         k == RMW_EVENT_LIVELINESS_CHANGED;
}
bool is_type(rmw_event_type_t k)
{
  return k == RMW_EVENT_SUBSCRIPTION_INCOMPATIBLE_TYPE ||
         k == RMW_EVENT_PUBLISHER_INCOMPATIBLE_TYPE;
}
}  // namespace
bool rmw_event_type_is_supported(rmw_event_type_t k)
{
  return deadline_event(k) || is_match(k) || is_type(k) ||
         k == RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE || k == RMW_EVENT_OFFERED_QOS_INCOMPATIBLE;
}
void Event::update(
  const std::vector<Peer> & graph, std::chrono::steady_clock::time_point snapshot_start)
{
  std::set<std::string> current;
  auto last = RMW_QOS_POLICY_INVALID;
  for (const auto & r : graph) {
    if (r.kind != (publisher ? 'S' : 'P') || r.name != name) continue;
    auto & offer = publisher ? qos : r.qos;
    auto & request = publisher ? r.qos : qos;
    auto reason = RMW_QOS_POLICY_INVALID;
    if (
      offer.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT &&
      request.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE)
      reason = RMW_QOS_POLICY_RELIABILITY;
    if (
      offer.durability == RMW_QOS_POLICY_DURABILITY_VOLATILE &&
      request.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL)
      reason = RMW_QOS_POLICY_DURABILITY;
    if (deadline_ns(offer.deadline) > deadline_ns(request.deadline))
      reason = RMW_QOS_POLICY_DEADLINE;
    if (kind == RMW_EVENT_REQUESTED_DEADLINE_MISSED) {
      if (type == r.type && reason == RMW_QOS_POLICY_INVALID) return;
      continue;
    }
    bool relevant = is_type(kind)
                      ? type != r.type
                      : (type == r.type && is_match(kind) == (reason == RMW_QOS_POLICY_INVALID));
    if (relevant) {
      current.insert(r.gid);
      if (!peers.count(r.gid)) last = reason;
    }
  }
  if (kind == RMW_EVENT_REQUESTED_DEADLINE_MISSED) {
    if (auto timer = requested_timer.lock()) timer->disconnect(snapshot_start);
    return;
  }
  size_t joins = 0, leaves = 0;
  for (auto & gid : current) joins += !peers.count(gid);
  if (is_match(kind))
    for (auto & gid : peers) leaves += !current.count(gid);
  {
    std::lock_guard lock(context->mutex);
    peers.swap(current);
    if (!joins && !leaves) return;
    total += joins;
    added += joins;
    removed += leaves;
    pending = true;
    if (joins) policy = last;
  }
  context->changed.notify_all();
  std::lock_guard lock(callback_mutex);
  if (callback)
    callback(data, joins + leaves);
  else
    unreported += joins + leaves;
}
bool standalone::event_ready(const void * opaque)
{
  auto handle = static_cast<const rmw_event_t *>(opaque);
  return handle && handle->implementation_identifier == id &&
         static_cast<Event *>(handle->data)->pending;
}
rmw_ret_t rmw_take_event(const rmw_event_t * handle, void * out, bool * taken)
{
  if (!handle || handle->implementation_identifier != id || !out || !taken)
    return RMW_RET_INVALID_ARGUMENT;
  auto & e = *static_cast<Event *>(handle->data);
  std::lock_guard lock(e.context->mutex);
  *taken = e.pending;
  if (!*taken) return RMW_RET_OK;
  int32_t change = static_cast<int32_t>(e.added) - static_cast<int32_t>(e.removed);
  if (e.kind == RMW_EVENT_OFFERED_DEADLINE_MISSED)
    *static_cast<rmw_offered_deadline_missed_status_t *>(out) = {
      static_cast<int32_t>(e.total), static_cast<int32_t>(e.added)};
  else if (e.kind == RMW_EVENT_REQUESTED_DEADLINE_MISSED)
    *static_cast<rmw_requested_deadline_missed_status_t *>(out) = {
      static_cast<int32_t>(e.total), static_cast<int32_t>(e.added)};
  else if (e.kind == RMW_EVENT_LIVELINESS_CHANGED) {
    auto & status = *static_cast<rmw_liveliness_changed_status_t *>(out);
    status = {};
    status.alive_count = e.peers.size();
    status.alive_count_change = change;
  } else if (is_match(e.kind))
    *static_cast<rmw_matched_status_t *>(out) = {e.total, e.added, e.peers.size(), change};
  else if (is_type(e.kind))
    *static_cast<rmw_incompatible_type_status_t *>(out) = {
      static_cast<int32_t>(e.total), static_cast<int32_t>(e.added)};
  else
    *static_cast<rmw_qos_incompatible_event_status_t *>(out) = {
      static_cast<int32_t>(e.total), static_cast<int32_t>(e.added), e.policy};
  e.pending = false;
  e.added = e.removed = 0;
  return RMW_RET_OK;
}
rmw_ret_t rmw_event_set_callback(rmw_event_t * handle, rmw_event_callback_t cb, const void * data)
{
  if (!handle || handle->implementation_identifier != id) return RMW_RET_INVALID_ARGUMENT;
  auto & e = *static_cast<Event *>(handle->data);
  std::lock_guard lock(e.callback_mutex);
  e.callback = cb;
  e.data = data;
  if (cb && e.unreported) {
    auto n = e.unreported;
    e.unreported = 0;
    cb(data, n);
  }
  return RMW_RET_OK;
}

bool standalone::deadline_event(rmw_event_type_t kind)
{
  return kind == RMW_EVENT_OFFERED_DEADLINE_MISSED || kind == RMW_EVENT_REQUESTED_DEADLINE_MISSED;
}
Deadline::Deadline(std::shared_ptr<Event> status)
: event(std::move(status)), period(deadline_ns(event->qos.deadline)), worker([this] { run(); })
{
}
Deadline::~Deadline()
{
  {
    std::lock_guard lock(mutex);
    stopping = true;
  }
  changed.notify_one();
  worker.join();
}
void Deadline::advance(Clock::time_point now)
{
  if (next > now) return;
  auto elapsed = now - next;
  misses += 1 + elapsed / period;
  auto remaining = period - elapsed % period;
  next = remaining < Clock::time_point::max() - now ? now + remaining : Clock::time_point::max();
}
void Deadline::sample()
{
  std::lock_guard lock(mutex);
  auto now = Clock::now();
  advance(now);
  last_sample = now;
  next = period < Clock::time_point::max() - now ? now + period : Clock::time_point::max();
  changed.notify_one();
}
void Deadline::disconnect(Clock::time_point snapshot_start)
{
  std::lock_guard lock(mutex);
  if (last_sample > snapshot_start) return;
  next = Clock::time_point::max();
  changed.notify_one();
}
void Deadline::run()
{
  std::unique_lock lock(mutex);
  while (!stopping) {
    advance(Clock::now());
    if (!misses) {
      if (next == Clock::time_point::max())
        changed.wait(lock);
      else
        changed.wait_until(lock, next);
      continue;
    }
    auto count = std::exchange(misses, 0);
    lock.unlock();
    {
      std::lock_guard guard(event->context->mutex);
      event->added += count;
      event->total += count;
      event->pending = true;
    }
    event->context->changed.notify_all();
    {
      std::lock_guard guard(event->callback_mutex);
      if (event->callback)
        event->callback(event->data, count);
      else
        event->unreported += count;
    }
    lock.lock();
  }
}
