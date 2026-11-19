#include "probe.hpp"

#include <pluginlib/class_list_macros.hpp>
#include <rosidl_buffer_backend/buffer_backend.hpp>

#include <std_msgs/msg/u_int64.hpp>

namespace probe
{
namespace
{
std::atomic_bool reclaimed{false};
std::atomic_bool rejected{false};
Reader * local_reader = nullptr;  // Set before spinning, cleared after shutdown.
class Storage final : public rosidl::BufferImplBase<uint8_t>
{
public:
  explicit Storage(agnocast::ipc_shared_ptr<const Bytes> handle) : loan(std::move(handle)) {}
  std::string get_backend_type() const override { return "agnocast_probe"; }
  size_t size() const override { return loan->size(); }
  std::unique_ptr<rosidl::BufferImplBase<uint8_t>> to_cpu() const override
  {
    auto cpu = std::make_unique<rosidl::CpuBufferImpl<uint8_t>>();
    cpu->get_storage() = *loan;
    return cpu;
  }
  std::unique_ptr<rosidl::BufferImplBase<uint8_t>> clone() const override { return to_cpu(); }
  agnocast::ipc_shared_ptr<const Bytes> loan;
};
}  // namespace
Bytes::~Bytes()
{
  if (!empty() && front() == 1) reclaimed.store(true);
}
bool first_reclaimed()
{
  return reclaimed.load();
}
std::string topic()
{
  auto name = std::getenv("AGNOCAST_BUFFER_PROBE_TOPIC");
  return name ? name : "";
}
void attach(Reader * reader)
{
  local_reader = reader;
}
Buffer wrap(agnocast::ipc_shared_ptr<const Bytes> loan)
{
  if (!loan) throw std::runtime_error("missing native loan");
  return Buffer(std::make_unique<Storage>(std::move(loan)));
}
std::span<const uint8_t> view(const Buffer & buffer)
{
  return *dynamic_cast<const Storage &>(*buffer.get_impl()).loan;
}
static std::unique_ptr<Storage> import(int64_t expected)
{
  if (!local_reader) throw std::runtime_error("no native probe reader");
  auto loan = local_reader->take();
  if (!loan || loan.get_entry_id() != expected) {
    rejected.store(true);
    throw std::runtime_error("descriptor generation unavailable");
  }
  return std::make_unique<Storage>(std::move(loan));
}
bool stale_rejected()
{
  return rejected.load();
}

class Backend final : public rosidl::BufferBackend
{
  using Descriptor = std_msgs::msg::UInt64;

public:
  std::string get_backend_type() const override { return "agnocast_probe"; }
  std::string get_backend_metadata() const override { return topic(); }
  std::pair<bool, std::vector<std::set<uint32_t>>> on_discovering_endpoint(
    const rmw_topic_endpoint_info_t &, const std::vector<rmw_topic_endpoint_info_t> &,
    const std::unordered_map<std::string, std::string> & backends) override
  {
    auto it = backends.find(get_backend_type());
    return {it != backends.end() && !topic().empty() && it->second == topic(), {}};
  }
  const rosidl_message_type_support_t * get_descriptor_type_support() const override
  {
    return rosidl_typesupport_cpp::get_message_type_support_handle<Descriptor>();
  }
  std::shared_ptr<void> create_empty_descriptor() const override
  {
    return std::make_shared<Descriptor>();
  }
  std::shared_ptr<void> create_descriptor_with_endpoint(
    const void * impl, const rmw_topic_endpoint_info_t &) const override
  {
    auto storage =
      dynamic_cast<const Storage *>(static_cast<const rosidl::BufferImplBase<uint8_t> *>(impl));
    if (!storage) return nullptr;
    auto desc = std::make_shared<Descriptor>();
    desc->data = storage->loan.get_entry_id();
    return desc;
  }
  std::unique_ptr<void, void (*)(void *)> from_descriptor_with_endpoint(
    const void * descriptor, const rmw_topic_endpoint_info_t &) const override
  {
    auto storage = import(static_cast<const Descriptor *>(descriptor)->data);
    return {storage.release(), [](void * p) { delete static_cast<Storage *>(p); }};
  }
};
}  // namespace probe
PLUGINLIB_EXPORT_CLASS(probe::Backend, rosidl::BufferBackend)
