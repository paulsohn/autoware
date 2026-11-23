#include "events.hpp"

#include <algorithm>
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
  return is_match(k) || is_type(k) || k == RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE ||
         k == RMW_EVENT_OFFERED_QOS_INCOMPATIBLE;
}
void Event::update(const std::vector<Peer> & graph)
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
    bool relevant = is_type(kind)
                      ? type != r.type
                      : (type == r.type && is_match(kind) == (reason == RMW_QOS_POLICY_INVALID));
    if (relevant) {
      current.insert(r.gid);
      if (!peers.count(r.gid)) last = reason;
    }
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
  if (e.kind == RMW_EVENT_LIVELINESS_CHANGED) {
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
