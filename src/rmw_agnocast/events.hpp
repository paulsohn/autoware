#pragma once
#include "standalone.hpp"

#include <rmw/events_statuses/events_statuses.h>

#include <map>
#include <set>
#include <string>
#include <vector>
namespace standalone
{
struct Peer
{
  std::string name, type, gid;
  char kind;
  rmw_qos_profile_t qos;
};
struct Event
{
  Context * context;
  std::string name, type;
  bool publisher;
  rmw_qos_profile_t qos;
  rmw_event_type_t kind;
  std::set<std::string> peers;
  size_t total{}, added{}, removed{}, unreported{};
  rmw_qos_policy_kind_t policy{RMW_QOS_POLICY_INVALID};
  bool pending{};
  std::recursive_mutex callback_mutex;
  rmw_event_callback_t callback{};
  const void * data{};
  void update(const std::vector<Peer> &);
};
bool event_ready(const void * handle);
void observe_event(const rmw_node_t *, const std::shared_ptr<Event> &);
}  // namespace standalone
