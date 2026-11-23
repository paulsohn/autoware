#pragma once
#include "standalone.hpp"

#include <rmw/events_statuses/events_statuses.h>

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>
namespace standalone
{
struct Peer
{
  std::string name, type, gid;
  char kind;
  rmw_qos_profile_t qos;
};
struct Deadline;
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
  std::weak_ptr<Deadline> requested_timer;
  void update(const std::vector<Peer> &, std::chrono::steady_clock::time_point snapshot_start);
};
inline int64_t deadline_ns(rmw_time_t time)
{
  auto ns = rmw_time_total_nsec(time);
  return ns ? ns : INT64_MAX;
}
bool deadline_event(rmw_event_type_t kind);
struct Deadline
{
  using Clock = std::chrono::steady_clock;
  std::shared_ptr<Event> event;
  explicit Deadline(std::shared_ptr<Event> status);
  ~Deadline();
  void sample();
  void disconnect(Clock::time_point snapshot_start);

private:
  void run();
  void advance(Clock::time_point now);
  const std::chrono::nanoseconds period;
  Clock::time_point next = Clock::time_point::max();
  Clock::time_point last_sample{};
  size_t misses = 0;
  bool stopping = false;
  std::mutex mutex;
  std::condition_variable changed;
  std::thread worker;
};
bool event_ready(const void * handle);
void observe_event(const rmw_node_t *, const std::shared_ptr<Event> &);
}  // namespace standalone
