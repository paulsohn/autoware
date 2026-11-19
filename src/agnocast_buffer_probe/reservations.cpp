#include "reservations.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace probe
{
namespace
{
struct Socket
{
  int fd;
  ~Socket()
  {
    if (fd >= 0) close(fd);
  }
};
std::string path(pid_t pid)
{
  // The harness runs all participants in one mode-0700 temporary directory.
  return "agnocast_reserve_" + std::to_string(pid);
}
sockaddr_un address(pid_t pid)
{
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  path(pid).copy(addr.sun_path, sizeof(addr.sun_path) - 1);
  return addr;
}
bool deadline(int fd)
{
  timeval t{0, 500000};
  return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t)) == 0 &&
         setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(t)) == 0;
}
}  // namespace
Reservations::Reservations(std::shared_ptr<Reader> reader)
: reader_(std::move(reader)), listener_(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0))
{
  auto addr = address(getpid());
  if (
    listener_ < 0 || bind(listener_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ||
    listen(listener_, 8)) {
    if (listener_ >= 0) close(listener_);
    throw std::runtime_error("could not start loan reservation listener");
  }
  worker_ = std::thread(&Reservations::serve, this);
}
Reservations::~Reservations()
{
  stop_ = true;
  shutdown(listener_, SHUT_RDWR);
  worker_.join();
  close(listener_);
  unlink(path(getpid()).c_str());
}
bool Reservations::request(pid_t receiver, int64_t entry)
{
  Socket s{socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)};
  auto addr = address(receiver);
  if (
    s.fd < 0 || !deadline(s.fd) || connect(s.fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)))
    return false;
  uint8_t accepted{};
  return send(s.fd, &entry, sizeof(entry), MSG_NOSIGNAL) == sizeof(entry) &&
         recv(s.fd, &accepted, sizeof(accepted), 0) == sizeof(accepted) && accepted == 1;
}
void Reservations::serve()
{
  while (!stop_) {
    Socket peer{accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC)};
    if (peer.fd < 0) {
      if (errno == EINTR) continue;
      break;
    }
    int64_t entry{};
    if (!deadline(peer.fd) || recv(peer.fd, &entry, sizeof(entry), MSG_TRUNC) != sizeof(entry))
      continue;
    uint8_t ok = reserve(entry);
    send(peer.fd, &ok, sizeof(ok), MSG_NOSIGNAL);
  }
}
bool Reservations::reserve(int64_t entry)
{
  std::lock_guard lock(mutex_);
  if (pending_.contains(entry)) return true;
  constexpr size_t byte_limit = 4 * 1024 * 1024;
  if (pending_.size() >= 16) return false;
  auto loan = reader_->take();
  if (!loan || loan.get_entry_id() != entry || loan->size() > byte_limit - pending_bytes_)
    return false;
  auto bytes = loan->size();
  pending_.emplace(entry, std::move(loan));
  pending_bytes_ += bytes;
  return true;
}
agnocast::ipc_shared_ptr<const Bytes> Reservations::consume(int64_t entry)
{
  std::lock_guard lock(mutex_);
  auto found = pending_.find(entry);
  if (found == pending_.end()) throw std::runtime_error("no reservation for descriptor");
  // This fixture has one ordered stream. Older unconsumed offers have been
  // skipped by the ROS queue when it takes this entry.
  for (auto it = pending_.begin(); it != found;) {
    pending_bytes_ -= it->second->size();
    it = pending_.erase(it);
  }
  pending_bytes_ -= found->second->size();
  auto loan = std::move(found->second);
  pending_.erase(found);
  return loan;
}
}  // namespace probe
