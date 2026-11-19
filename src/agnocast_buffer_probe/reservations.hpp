#pragma once
#include "probe.hpp"

#include <atomic>
#include <map>
#include <mutex>
#include <thread>

namespace probe
{
// Single ordered writer and one ROS subscription per receiver process.
class Reservations
{
public:
  explicit Reservations(std::shared_ptr<Reader> reader);
  ~Reservations();
  agnocast::ipc_shared_ptr<const Bytes> consume(int64_t entry);
  static bool request(pid_t receiver, int64_t entry);

private:
  void serve();
  bool reserve(int64_t entry);
  std::shared_ptr<Reader> reader_;
  int listener_;
  std::atomic_bool stop_{false};
  std::thread worker_;
  std::mutex mutex_;
  std::map<int64_t, agnocast::ipc_shared_ptr<const Bytes>> pending_;
  size_t pending_bytes_{0};
};
}  // namespace probe
