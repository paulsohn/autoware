#pragma once

#include "filter.hpp"
#include "graph.hpp"
#include "host_cloud.hpp"

#include <agnocast/node/agnocast_node.hpp>
#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>
#include <rcpputils/shared_library.hpp>

#include <deque>
#include <map>
#include <thread>

namespace standalone
{
namespace endpoints __attribute__((visibility("hidden")))
{
using Loan = agnocast::ipc_shared_ptr<const void>;
using rosidl_typesupport_introspection_cpp::MessageMembers;

const char * resolve_qos(rmw_qos_profile_t & qos);

struct Endpoint
{
  std::string topic, type, route;
  const rosidl_message_type_support_t * application_ts;
  const rmw_node_t * owner_node;
  std::map<rmw_event_type_t, std::shared_ptr<Event>> events;
  std::mutex events_mutex;
  Lease lease;
  std::shared_ptr<Deadline> deadline;
  bool internal;
  bool host_cloud{};
  bool cdr_wire{};
  bool typed_wire() const { return !host_cloud && !cdr_wire; }
  size_t wire_size() const
  {
    return host_cloud ? sizeof(HostCloud)
           : cdr_wire ? sizeof(std::vector<uint8_t>)
                      : members->size_of_;
  }
  void configure_deadline(bool publisher);
  std::shared_ptr<rcpputils::SharedLibrary> library;
  const rosidl_message_type_support_t * native_ts;
  const MessageMembers * members;
  rmw_qos_profile_t qos;
  const int64_t deadline_period;
  std::shared_ptr<agnocast::Node> node;
  Endpoint(
    const rmw_node_t * owner, const rosidl_message_type_support_t * ts, const char * name,
    const rmw_qos_profile_t & requested, bool rpc);
  ~Endpoint();
  rclcpp::QoS ros_qos() const;
};
struct SampleHeader
{
  uint8_t gid[RMW_GID_STORAGE_SIZE];
  int64_t offered_deadline;
  int64_t expiry;
  int64_t stamp;
  uint64_t sequence;
};
struct QueuedSample
{
  Loan loan;
  SampleHeader header;
  int64_t received;
  uint64_t sequence;
  rmw_message_info_t info() const
  {
    rmw_message_info_t out{};
    out.source_timestamp = header.stamp;
    out.received_timestamp = received;
    out.publication_sequence_number = header.sequence;
    out.reception_sequence_number = sequence;
    out.publisher_gid.implementation_identifier = id;
    std::memcpy(out.publisher_gid.data, header.gid, sizeof(header.gid));
    return out;
  }
};
struct Writer : Endpoint
{
  agnocast::TypeErasedPublisher native;
  rmw_gid_t gid{};
  std::mutex send_mutex;
  uint64_t sequence{};
  rmw_publisher_t handle{};
  std::mutex mutex;
  std::map<void *, agnocast::ipc_shared_ptr<void>> borrowed;

  bool cloud_loans{};
  size_t published_loans{}, private_copies{};
  uintptr_t pool_begin = 0, pool_end = 0;
  // Linux maps this writer's native loan and its shared vector storage in one VMA.
  bool owns_payload(const std::vector<uint8_t> & bytes, const void * loan);
  agnocast::ipc_shared_ptr<void> borrow();
  void publish_bytes(const rmw_serialized_message_t & bytes);
  void publish_cloud(const Cloud & cloud);
  void destroy(void * p);
  void cancel(agnocast::ipc_shared_ptr<void> && loan);
  void send(agnocast::ipc_shared_ptr<void> && loan);
  ~Writer();
  Writer(
    const rmw_node_t * owner, const rosidl_message_type_support_t * ts, const char * name,
    const rmw_qos_profile_t & q, const rmw_publisher_options_t & options, bool rpc = false);
};
class PollSubscription : public agnocast::Subscription<void>
{
public:
  using agnocast::Subscription<void>::Subscription;
  int fd() const { return notify_eventfd_; }
};

class PollExecutor : public agnocast::AgnocastOnlySingleThreadedExecutor
{
public:
  void acquire_ready(const std::pair<uint32_t, agnocast::CallbackInfo> & registration);
  void prepare();
  void acquire();
};

struct Reader : Endpoint
{
  Context * context;
  rmw_subscription_t handle{};
  uint64_t sequence{};
  // Native subscriptions must still exist when queued and held loans release their references.
  std::vector<std::shared_ptr<PollSubscription>> routes;
  std::vector<std::pair<uint32_t, agnocast::CallbackInfo>> ready_routes;
  std::deque<QueuedSample> pending;
  Filter filter;
  std::recursive_mutex callback_mutex;
  rmw_event_callback_t callback{};
  const void * callback_data{};
  size_t unreported{};

  std::map<const void *, Loan> held;
  PollExecutor executor;
  bool direct{};
  bool host_view{};
  struct ViewOwner
  {
    std::shared_ptr<agnocast::Node> node;
    decltype(routes) registrations;
    Loan sample;  // Release before unregistering the native subscription.
  };
  std::mutex acquire_mutex;
  std::thread thread;
  Reader(
    const rmw_node_t * owner, const rosidl_message_type_support_t * ts, const char * name,
    const rmw_qos_profile_t & q, const rmw_subscription_options_t & options, bool rpc = false);
  void acquire();
  void before_take();
  bool ready();
  ~Reader();
};
}  // namespace endpoints
}  // namespace standalone
