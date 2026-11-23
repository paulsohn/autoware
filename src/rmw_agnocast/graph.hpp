#pragma once
#include "events.hpp"

#include <string>
#include <thread>

namespace standalone
{
struct Lease
{
  int fd = -1;
  std::string path;
  void announce(
    const rmw_node_t * node, char kind, const std::string & name = {},
    const std::string & type = {}, const rosidl_type_hash_t * hash = nullptr,
    const rmw_qos_profile_t * qos = nullptr, const rmw_gid_t * gid = nullptr);
  ~Lease();
};
struct GraphWatch
{
  int events = -1, stop = -1;
  std::atomic<bool> stopping{false};
  std::thread worker;
  std::mutex mutex;
  std::vector<std::weak_ptr<Event>> listeners;
  void start(const rmw_node_t * node, rmw_guard_condition_t * guard);
  ~GraphWatch();
};
}  // namespace standalone
