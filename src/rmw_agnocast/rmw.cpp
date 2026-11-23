#include "filter.hpp"
#include "graph.hpp"
#include "host_cloud.hpp"
#include "host_cloud_view.hpp"

#include <agnocast/agnocast_executor.hpp>
#include <agnocast/node/agnocast_node.hpp>
#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>
#include <rclcpp/typesupport_helpers.hpp>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

#include <std_msgs/msg/string.hpp>

#include <rmw/features.h>
#include <rmw/get_network_flow_endpoints.h>
#include <rosidl_runtime_c/type_description/type_description__struct.h>
#include <sys/random.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <thread>

namespace
{
using namespace standalone;
using namespace rosidl_typesupport_introspection_cpp;
using Loan = agnocast::ipc_shared_ptr<const void>;

void copy_message(const MessageMembers & members, const void * from, void * to)
{
  for (size_t i = 0; i < members.member_count_; ++i) {
    auto & f = members.members_[i];
    const void * src = static_cast<const char *>(from) + f.offset_;
    void * dst = static_cast<char *>(to) + f.offset_;
    if (f.is_array_) {
      size_t n = f.size_function(src);
      if (f.resize_function) f.resize_function(dst, n);
      for (size_t j = 0; j < n; ++j) {
        if (f.type_id_ == ROS_TYPE_BOOLEAN) {
          bool v;
          f.fetch_function(src, j, &v);
          f.assign_function(dst, j, &v);
        } else {
          f.assign_function(dst, j, f.get_const_function(src, j));
        }
      }
      continue;
    }
    switch (f.type_id_) {
      case ROS_TYPE_MESSAGE:
        copy_message(*static_cast<const MessageMembers *>(f.members_->data), src, dst);
        break;
      case ROS_TYPE_STRING:
        *static_cast<std::string *>(dst) = *static_cast<const std::string *>(src);
        break;
      case ROS_TYPE_WSTRING:
        *static_cast<std::u16string *>(dst) = *static_cast<const std::u16string *>(src);
        break;
      case ROS_TYPE_LONG_DOUBLE:
        std::memcpy(dst, src, sizeof(long double));
        break;
      case ROS_TYPE_DOUBLE:
      case ROS_TYPE_INT64:
      case ROS_TYPE_UINT64:
        std::memcpy(dst, src, 8);
        break;
      case ROS_TYPE_FLOAT:
      case ROS_TYPE_INT32:
      case ROS_TYPE_UINT32:
        std::memcpy(dst, src, 4);
        break;
      case ROS_TYPE_WCHAR:
      case ROS_TYPE_INT16:
      case ROS_TYPE_UINT16:
        std::memcpy(dst, src, 2);
        break;
      case ROS_TYPE_BOOLEAN:
      case ROS_TYPE_CHAR:
      case ROS_TYPE_OCTET:
      case ROS_TYPE_INT8:
      case ROS_TYPE_UINT8:
        std::memcpy(dst, src, 1);
        break;
      default:
        throw std::runtime_error("unsupported native field");
    }
  }
}
// Build the copy schedule once. Only primitive subobjects are copied, never
// message padding, container control blocks or C++ object headers.
struct CopyPlan
{
  const MessageMembers * type;
  struct Span
  {
    size_t offset, bytes;
  };
  std::vector<Span> spans;
  bool append(const MessageMembers & members, size_t base)
  {
    for (size_t i = 0; i < members.member_count_; ++i) {
      const auto & f = members.members_[i];
      if (f.is_rosidl_buffer_ || (f.is_array_ && (!f.array_size_ || f.is_upper_bound_)))
        return false;
      size_t offset = base + f.offset_, count = f.is_array_ ? f.array_size_ : 1;
      if (f.type_id_ == ROS_TYPE_MESSAGE) {
        const auto & nested = *static_cast<const MessageMembers *>(f.members_->data);
        for (size_t n = 0; n < count; ++n)
          if (!append(nested, offset + n * nested.size_of_)) return false;
        continue;
      }
      size_t width;
      switch (f.type_id_) {
        case ROS_TYPE_BOOLEAN:
          width = sizeof(bool);
          break;
        case ROS_TYPE_CHAR:
        case ROS_TYPE_OCTET:
        case ROS_TYPE_INT8:
        case ROS_TYPE_UINT8:
          width = 1;
          break;
        case ROS_TYPE_WCHAR:
        case ROS_TYPE_INT16:
        case ROS_TYPE_UINT16:
          width = 2;
          break;
        case ROS_TYPE_FLOAT:
        case ROS_TYPE_INT32:
        case ROS_TYPE_UINT32:
          width = 4;
          break;
        case ROS_TYPE_DOUBLE:
        case ROS_TYPE_INT64:
        case ROS_TYPE_UINT64:
          width = 8;
          break;
        case ROS_TYPE_LONG_DOUBLE:
          width = sizeof(long double);
          break;
        default:
          return false;
      }
      spans.push_back({offset, count * width});
    }
    return true;
  }
  void copy(const void * source, void * dest) const
  {
    for (auto span : spans)
      std::memcpy(
        static_cast<char *>(dest) + span.offset, static_cast<const char *>(source) + span.offset,
        span.bytes);
  }
};
bool has_buffer(const MessageMembers & m)
{
  for (size_t i = 0; i < m.member_count_; ++i) {
    auto & f = m.members_[i];
    if (
      f.is_rosidl_buffer_ || (f.type_id_ == ROS_TYPE_MESSAGE &&
                              has_buffer(*static_cast<const MessageMembers *>(f.members_->data))))
      return true;
  }
  return false;
}
const char * resolve_qos(rmw_qos_profile_t & qos)
{
  if (qos.history == RMW_QOS_POLICY_HISTORY_SYSTEM_DEFAULT) {
    qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
    if (!qos.depth) qos.depth = 10;
  }
  if (qos.reliability == RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT)
    qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  if (qos.durability == RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT)
    qos.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
  if (
    (qos.reliability != RMW_QOS_POLICY_RELIABILITY_RELIABLE &&
     qos.reliability != RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT) ||
    qos.liveliness_lease_duration.sec || qos.liveliness_lease_duration.nsec ||
    (qos.liveliness != RMW_QOS_POLICY_LIVELINESS_AUTOMATIC &&
     qos.liveliness != RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT) ||
    qos.avoid_ros_namespace_conventions)
    return "unsupported native endpoint QoS";
  if (
    !qos.depth || qos.history != RMW_QOS_POLICY_HISTORY_KEEP_LAST ||
    (qos.durability != RMW_QOS_POLICY_DURABILITY_VOLATILE &&
     qos.durability != RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL))
    return "standalone endpoint requires supported durability and KeepLast with positive depth";
  return nullptr;
}
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
  void configure_deadline(bool publisher)
  {
    if (deadline_period == INT64_MAX) return;
    if (internal || members->has_any_key_member_)
      throw std::invalid_argument("finite deadlines require unkeyed topic endpoints");
    auto kind = publisher ? RMW_EVENT_OFFERED_DEADLINE_MISSED : RMW_EVENT_REQUESTED_DEADLINE_MISSED;
    auto event = std::shared_ptr<Event>(
      new Event{state(owner_node->context), topic, type, publisher, qos, kind});
    events.emplace(kind, event);
    deadline = std::make_shared<Deadline>(event);
    if (!publisher) {
      event->requested_timer = deadline;
      observe_event(owner_node, event);
    }
  }
  std::shared_ptr<rcpputils::SharedLibrary> library;
  const rosidl_message_type_support_t * native_ts;
  const MessageMembers * members;
  rmw_qos_profile_t qos;
  const int64_t deadline_period;
  std::shared_ptr<agnocast::Node> node;
  Endpoint(
    const rmw_node_t * owner, const rosidl_message_type_support_t * ts, const char * name,
    const rmw_qos_profile_t & requested, bool rpc)
  : topic(name),
    application_ts(ts),
    owner_node(owner),
    internal(rpc),
    native_ts(ts),
    qos(requested),
    deadline_period(deadline_ns(requested.deadline))
  {
    auto intro = get_message_typesupport_handle(ts, "rosidl_typesupport_introspection_cpp");
    if (!intro) {
      rmw_reset_error();
      auto c = get_message_typesupport_handle(ts, "rosidl_typesupport_introspection_c");
      if (!c || !ts->get_type_description_func)
        throw std::runtime_error("missing generated type support");
      const auto * desc = ts->get_type_description_func(ts);
      if (!desc) throw std::runtime_error("no message description");
      const auto type_name = desc->type_description.type_name.data;
      library = rclcpp::get_typesupport_library(type_name, "rosidl_typesupport_cpp");
      native_ts =
        rclcpp::get_message_typesupport_handle(type_name, "rosidl_typesupport_cpp", *library);
      intro = get_message_typesupport_handle(native_ts, "rosidl_typesupport_introspection_cpp");
      if (!intro) throw std::runtime_error("canonical C++ introspection unavailable");
    }
    members = static_cast<const MessageMembers *>(intro->data);
    host_cloud = std::string_view(members->message_namespace_) == "sensor_msgs::msg" &&
                 std::string_view(members->message_name_) == "PointCloud2";
    cdr_wire = !host_cloud && has_buffer(*members) &&
               std::string_view(members->message_namespace_) == "autoware_sensing_msgs::msg" &&
               std::string_view(members->message_name_) == "ConcatenatedPointCloudInfo";
    if (typed_wire() && has_buffer(*members))
      throw std::runtime_error(
        std::string("unsupported Buffer wire type: ") + members->message_namespace_ + "/" +
        members->message_name_);
    if (const char * error = resolve_qos(qos)) throw std::runtime_error(error);
    type = std::string(members->message_namespace_) + "/" + members->message_name_;
    for (auto pos = type.find("::"); pos != std::string::npos; pos = type.find("::"))
      type.replace(pos, 2, "/");
    route = std::string(rpc ? "/_rmw_agnocast/rpc3/d" : "/_rmw_agnocast/v4/d") +
            std::to_string(owner->context->actual_domain_id) + topic + "/" + type;
    if (cdr_wire) route += "/cdr_v1";
    if (host_cloud) route += "/cpu_cloud_v1";
    static std::once_flag once;
    std::call_once(once, [] { agnocast::init(0, nullptr); });
    rclcpp::NodeOptions options;
    options.use_global_arguments(false).start_parameter_services(false).use_clock_thread(false);
    node = std::make_shared<agnocast::Node>(owner->name, owner->namespace_, options);
  }
  ~Endpoint()
  {
    deadline.reset();
    for (auto & entry : events) {
      auto & event = *entry.second;
      std::lock_guard lock(event.callback_mutex);
      event.callback = nullptr;
    }
  }
  auto ros_qos() const
  {
    auto native = qos;
    native.deadline = {};
    native.lifespan = {};  // The RMW enforces publisher lifespan at receive and take.
    return rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(native), native);
  }
};
int64_t monotonic_ns()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
           std::chrono::steady_clock::now().time_since_epoch())
    .count();
}
int64_t system_ns()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
           std::chrono::system_clock::now().time_since_epoch())
    .count();
}
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
  bool owns_payload(const std::vector<uint8_t> & bytes, const void * loan)
  {
    if (!pool_end) {
      std::ifstream maps("/proc/self/maps");
      for (std::string line; std::getline(maps, line);) {
        unsigned long begin, end;
        char mode[5];
        if (
          std::sscanf(line.c_str(), "%lx-%lx %4s", &begin, &end, mode) == 3 &&
          std::string_view(mode) == "rw-s" && begin <= reinterpret_cast<uintptr_t>(loan) &&
          reinterpret_cast<uintptr_t>(loan) < end) {
          pool_begin = begin;
          pool_end = end;
          break;
        }
      }
    }
    if (bytes.empty()) return !bytes.capacity();
    const auto ptr = reinterpret_cast<uintptr_t>(bytes.data());
    return pool_begin <= ptr && ptr < pool_end && bytes.capacity() <= pool_end - ptr;
  }
  auto borrow()
  {
    auto loan =
      native.borrow_loaned_message(std::max(members->size_of_, wire_size()) + sizeof(SampleHeader));
    try {
      members->init_function(loan.get(), rosidl_runtime_cpp::MessageInitialization::ALL);
    } catch (...) {
      native.cancel_message(std::move(loan), [](void * p) { ::operator delete(p); });
      throw;
    }
    return loan;
  }
  void publish_bytes(const rmw_serialized_message_t & bytes)
  {
    auto loan = native.borrow_loaned_message(wire_size() + sizeof(SampleHeader));
    try {
      new (loan.get()) std::vector<uint8_t>(bytes.buffer, bytes.buffer + bytes.buffer_length);
    } catch (...) {
      native.cancel_message(std::move(loan), [](void * p) { ::operator delete(p); });
      throw;
    }
    send(std::move(loan));
  }
  void publish_cloud(const Cloud & cloud)
  {
    if (cloud.data.get_backend_type() != "cpu")
      throw std::runtime_error("standalone cloud transport currently requires CPU data");
    auto loan = native.borrow_loaned_message(wire_size() + sizeof(SampleHeader));
    try {
      auto wire = new (loan.get()) HostCloud(cloud);
      if (const char * path = std::getenv("AGNOCAST_VIEW_ADDRESS_FILE"))
        std::ofstream(path) << reinterpret_cast<uintptr_t>(wire->bytes.data());
    } catch (...) {
      native.cancel_message(std::move(loan), [](void * p) { ::operator delete(p); });
      throw;
    }
    send(std::move(loan));
  }
  void destroy(void * p)
  {
    if (cdr_wire)
      std::destroy_at(static_cast<std::vector<uint8_t> *>(p));
    else if (host_cloud)
      static_cast<HostCloud *>(p)->~HostCloud();
    else
      members->fini_function(p);
    ::operator delete(p);
  }
  void cancel(agnocast::ipc_shared_ptr<void> && loan)
  {
    native.cancel_message(std::move(loan), [this](void * p) {
      members->fini_function(p);
      ::operator delete(p);
    });
  }
  void send(agnocast::ipc_shared_ptr<void> && loan)
  {
    std::lock_guard lock(send_mutex);
    SampleHeader header{};
    std::memcpy(header.gid, gid.data, sizeof(header.gid));
    header.offered_deadline = deadline_period;
    header.sequence = ++sequence;
    header.stamp = system_ns();
    auto duration = rmw_time_total_nsec(qos.lifespan);
    if (duration) {
      auto now = monotonic_ns();
      if (duration < INT64_MAX - now) header.expiry = now + duration;
    }
    std::memcpy(static_cast<char *>(loan.get()) + wire_size(), &header, sizeof(header));
    native.publish(std::move(loan), [this](void * p) { destroy(p); });
    if (deadline) deadline->sample();
  }
  ~Writer()
  {
    if (published_loans)
      std::fprintf(
        stderr, "host-cloud publisher loans=%zu private_copies=%zu\n", published_loans,
        private_copies);
    for (auto & entry : borrowed) cancel(std::move(entry.second));
  }
  Writer(
    const rmw_node_t * owner, const rosidl_message_type_support_t * ts, const char * name,
    const rmw_qos_profile_t & q, const rmw_publisher_options_t & options, bool rpc = false)
  : Endpoint(owner, ts, name, q, rpc),
    native(
      node.get(),
      route + (qos.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE ? "/r" : "/b") +
        (qos.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL ? "/t" : "/v"),
      type, ros_qos(), {}, agnocast::PublisherRole::AgnocastOnly)
  {
    configure_deadline(true);
    if (getrandom(gid.data, sizeof(gid.data), 0) != sizeof(gid.data))
      throw std::runtime_error("native publisher ID unavailable");
    gid.implementation_identifier = id;
    if (!rpc) lease.announce(owner, 'P', topic, type, ts->get_type_hash_func(ts), &qos, &gid);
    cloud_loans = host_cloud && !library && std::getenv("AGNOCAST_RMW_HOST_LOAN");
    handle = {id, this, topic.c_str(), options, (!library && typed_wire()) || cloud_loans};
  }
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
  void acquire_ready(const std::pair<uint32_t, agnocast::CallbackInfo> & registration)
  {
    const auto & [id, info] = registration;
    eventfd_t count;
    while (eventfd_read(info.notify_eventfd, &count) < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN) return;  // A concurrent take already drained this notification.
      throw std::runtime_error("cannot read ready Agnocast route");
    }
    agnocast::receive_and_execute_message(
      id, my_pid_, info, ready_agnocast_executables_mutex_, ready_agnocast_executables_);
    // A kernel receive may schedule a continuation for a coalesced burst.
    agnocast::AgnocastExecutable next;
    while (get_next_ready_agnocast_executable(next)) execute_agnocast_executable(next);
  }
  void prepare()
  {
    add_callback_groups_from_nodes_associated_to_executor();
    epoll_manager_->prepare_epoll(
      [this](const auto & group) { return is_callback_group_associated(group); });
  }
  void acquire()
  {
    agnocast::AgnocastExecutable executable;
    if (get_next_agnocast_executable(executable, 0)) {
      do {
        execute_agnocast_executable(executable);
      } while (get_next_ready_agnocast_executable(executable));
    }
  }
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
    const rmw_qos_profile_t & q, const rmw_subscription_options_t & options, bool rpc = false)
  : Endpoint(owner, ts, name, q, rpc), context(state(owner->context))
  {
    configure_deadline(false);
    const std::string backends =
      options.acceptable_buffer_backends ? options.acceptable_buffer_backends : "";
    host_view = host_cloud && !library && std::getenv("AGNOCAST_RMW_HOST_VIEW") &&
                (backends == "any" ||
                 ("," + backends + ",").find(",agnocast_host_view,") != std::string::npos);
    if (
      !typed_wire() && options.content_filter_options &&
      options.content_filter_options->filter_expression &&
      *options.content_filter_options->filter_expression)
      throw std::runtime_error("cloud filters unsupported");
    if (options.content_filter_options) filter = Filter(*members, *options.content_filter_options);
    auto attach = [&](const std::string & suffix) {
      agnocast::SubscriptionOptions opt;
      opt.ignore_local_publications = options.ignore_local_publications;
      routes.push_back(
        std::make_shared<PollSubscription>(
          node.get(), route + suffix, type, ros_qos(),
          [this](Loan sample) {
            {
              std::lock_guard lock(context->mutex);
              SampleHeader header;
              std::memcpy(
                &header, static_cast<const char *>(sample.get()) + wire_size(), sizeof(header));
              if (header.offered_deadline > deadline_period) return;
              if (header.expiry && monotonic_ns() >= header.expiry) return;
              if (filter.matches && !filter.matches(sample.get())) return;
              if (deadline) deadline->sample();
              if (pending.size() == qos.depth) pending.pop_front();
              pending.push_back({std::move(sample), header, system_ns(), ++sequence});
            }
            context->notify();
            std::lock_guard callback_lock(callback_mutex);
            if (callback)
              callback(callback_data, 1);
            else
              ++unreported;
          },
          opt, agnocast::SubscriptionRole::AgnocastOnly));
    };
    auto attach_durabilities = [&](const std::string & reliability) {
      attach(reliability + "/t");
      if (qos.durability == RMW_QOS_POLICY_DURABILITY_VOLATILE) attach(reliability + "/v");
    };
    attach_durabilities("/r");
    if (qos.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT) attach_durabilities("/b");
    handle.implementation_identifier = id;
    handle.data = this;
    handle.topic_name = topic.c_str();
    handle.options = options;
    handle.can_loan_messages = !library && typed_wire();
    handle.is_cft_enabled = bool(filter.matches);
    handle.is_cft_supported = typed_wire();
    if (!rpc) lease.announce(owner, 'S', topic, type, ts->get_type_hash_func(ts), &qos);
    executor.add_node(node);
    const char * selected = std::getenv("AGNOCAST_RMW_POLL_TOPIC");
    direct = !rpc && selected && topic == selected;
    if (direct) {
      executor.prepare();
      const char * exact_route = std::getenv("AGNOCAST_RMW_READY_ROUTE");
      if (exact_route && std::string_view(exact_route) == "1") {
        // Each registration belongs to this reader's private native node. Its callback only
        // admits a loan to the RMW queue. ROS callback-group dispatch stays with rclcpp.
        std::lock_guard lock(agnocast::id2_callback_info_mtx);
        for (const auto & route : routes) {
          const auto & registry = agnocast::id2_callback_info;
          auto it = std::find_if(registry.begin(), registry.end(), [&](const auto & entry) {
            return entry.second.notify_eventfd == route->fd();
          });
          if (it == registry.end()) throw std::runtime_error("missing Agnocast route registration");
          ready_routes.push_back(*it);
        }
      }
    } else
      thread = std::thread([this] { executor.spin(); });
  }
  void acquire()
  {
    std::lock_guard lock(acquire_mutex);
    executor.acquire();
  }
  void before_take()
  {
    if (!direct) return;
    {
      std::lock_guard lock(context->mutex);
      if (ready()) return;
    }
    acquire();
  }
  bool ready()
  {
    while (!pending.empty() && pending.front().header.expiry &&
           monotonic_ns() >= pending.front().header.expiry)
      pending.pop_front();
    return !pending.empty();
  }
  ~Reader()
  {
    executor.cancel();
    if (thread.joinable()) thread.join();
  }
};
}  // namespace
void standalone::append_routes(void * sub, std::vector<pollfd> & descriptors)
{
  auto & reader = *static_cast<Reader *>(sub);
  if (reader.direct)
    for (auto & route : reader.routes) descriptors.push_back({route->fd(), POLLIN, 0});
}
void standalone::acquire_routes(void * sub, const pollfd * descriptors, size_t count)
{
  auto & reader = *static_cast<Reader *>(sub);
  for (size_t i = 0; i < count; ++i) {
    if (!(descriptors[i].revents & POLLIN)) continue;
    std::lock_guard lock(reader.acquire_mutex);
    if (reader.ready_routes.empty()) {
      reader.executor.acquire();
      return;
    }
    reader.executor.acquire_ready(reader.ready_routes[i]);
  }
}
bool standalone::ready(void * sub)
{
  return static_cast<Reader *>(sub)->ready();
}
rmw_publisher_t * rmw_create_publisher(
  const rmw_node_t * n, const rosidl_message_type_support_t * t, const char * name,
  const rmw_qos_profile_t * q, const rmw_publisher_options_t * o)
{
  if (!n || n->implementation_identifier != standalone::id || !t || !name || !q || !o)
    return nullptr;
  if (
    o->require_unique_network_flow_endpoints ==
    RMW_UNIQUE_NETWORK_FLOW_ENDPOINTS_STRICTLY_REQUIRED) {
    RMW_SET_ERROR_MSG("native transport has no network flow");
    return nullptr;
  }
  try {
    return &(new Writer(n, t, name, *q, *o))->handle;
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return nullptr;
  }
}
rmw_subscription_t * rmw_create_subscription(
  const rmw_node_t * n, const rosidl_message_type_support_t * t, const char * name,
  const rmw_qos_profile_t * q, const rmw_subscription_options_t * o)
{
  if (!n || n->implementation_identifier != standalone::id || !t || !name || !q || !o)
    return nullptr;
  if (
    o->require_unique_network_flow_endpoints ==
    RMW_UNIQUE_NETWORK_FLOW_ENDPOINTS_STRICTLY_REQUIRED) {
    RMW_SET_ERROR_MSG("native transport has no network flow");
    return nullptr;
  }
  try {
    return &(new Reader(n, t, name, *q, *o))->handle;
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return nullptr;
  }
}
rmw_ret_t rmw_destroy_publisher(rmw_node_t *, rmw_publisher_t * p)
{
  if (!p || p->implementation_identifier != standalone::id) return RMW_RET_INVALID_ARGUMENT;
  delete static_cast<Writer *>(p->data);
  return RMW_RET_OK;
}
rmw_ret_t rmw_destroy_subscription(rmw_node_t *, rmw_subscription_t * s)
{
  if (!s || s->implementation_identifier != standalone::id) return RMW_RET_INVALID_ARGUMENT;
  delete static_cast<Reader *>(s->data);
  return RMW_RET_OK;
}
rmw_ret_t rmw_publish(const rmw_publisher_t * p, const void * msg, rmw_publisher_allocation_t *)
{
  if (!p || p->implementation_identifier != standalone::id || !msg) return RMW_RET_INVALID_ARGUMENT;
  auto & w = *static_cast<Writer *>(p->data);
  try {
    if (w.cdr_wire) {
      rclcpp::SerializedMessage scratch;
      auto & bytes = scratch.get_rcl_serialized_message();
      auto ret = rmw_serialize(msg, w.application_ts, &bytes);
      if (ret != RMW_RET_OK) return ret;
      w.publish_bytes(bytes);
      return RMW_RET_OK;
    }
    if (w.host_cloud) {
      if (!w.library)
        w.publish_cloud(*static_cast<const Cloud *>(msg));
      else {
        rclcpp::SerializedMessage scratch;
        auto & bytes = scratch.get_rcl_serialized_message();
        Cloud cloud;
        auto ret = rmw_serialize(msg, w.application_ts, &bytes);
        if (ret == RMW_RET_OK) ret = rmw_deserialize(&bytes, w.native_ts, &cloud);
        if (ret != RMW_RET_OK) return ret;
        w.publish_cloud(cloud);
      }
      return RMW_RET_OK;
    }
    auto loan = w.borrow();
    try {
      if (w.library) {
        rclcpp::SerializedMessage scratch;
        auto & bytes = scratch.get_rcl_serialized_message();
        auto ret = rmw_serialize(msg, w.application_ts, &bytes);
        if (ret == RMW_RET_OK) ret = rmw_deserialize(&bytes, w.native_ts, loan.get());
        if (ret != RMW_RET_OK) {
          w.cancel(std::move(loan));
          return ret;
        }
      } else {
        copy_message(*w.members, msg, loan.get());
      }
    } catch (...) {
      w.cancel(std::move(loan));
      throw;
    }
    w.send(std::move(loan));
    return RMW_RET_OK;
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return RMW_RET_ERROR;
  }
}
// The caller holds the context mutex for the entire take (including a batch).
static rmw_ret_t take_locked(
  Reader & r, void * msg, void ** loan, bool * taken, rmw_message_info_t * info,
  const rmw_subscription_allocation_t * allocation = nullptr)
{
  const CopyPlan * plan = nullptr;
  if (allocation) {
    if (allocation->implementation_identifier != standalone::id)
      return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
    plan = static_cast<const CopyPlan *>(allocation->data);
    if (!plan || plan->type != r.members || r.library) return RMW_RET_INVALID_ARGUMENT;
  }
  *taken = false;
  if (!r.ready()) return RMW_RET_OK;
  auto & sample = r.pending.front().loan;
  try {
    if (r.cdr_wire) {
      if (loan) return RMW_RET_UNSUPPORTED;
      const auto & bytes = *static_cast<const std::vector<uint8_t> *>(sample.get());
      rmw_serialized_message_t serialized{};
      serialized.buffer = const_cast<uint8_t *>(bytes.data());
      serialized.buffer_length = serialized.buffer_capacity = bytes.size();
      auto ret = rmw_deserialize(&serialized, r.application_ts, msg);
      if (ret != RMW_RET_OK) return ret;
    } else if (r.host_cloud) {
      if (loan) return RMW_RET_UNSUPPORTED;
      Cloud temporary;
      auto & cloud = r.library ? temporary : *static_cast<Cloud *>(msg);
      auto & wire = *static_cast<const HostCloud *>(sample.get());
      wire.restore_metadata(cloud);
      if (r.host_view) {
        auto owner =
          std::make_shared<Reader::ViewOwner>(Reader::ViewOwner{r.node, r.routes, sample});
        std::shared_ptr<const std::vector<uint8_t>> bytes(owner, &wire.bytes);
        cloud.data =
          rosidl::Buffer<uint8_t>(std::make_unique<rmw_agnocast::HostCloudView>(std::move(bytes)));
      } else
        cloud.data = wire.bytes;
      if (r.library) {
        rclcpp::SerializedMessage scratch;
        auto & bytes = scratch.get_rcl_serialized_message();
        auto ret = rmw_serialize(&cloud, r.native_ts, &bytes);
        if (ret == RMW_RET_OK) ret = rmw_deserialize(&bytes, r.application_ts, msg);
        if (ret != RMW_RET_OK) return ret;
      }
    } else if (r.library) {
      if (loan) return RMW_RET_UNSUPPORTED;
      rclcpp::SerializedMessage scratch;
      auto & bytes = scratch.get_rcl_serialized_message();
      auto ret = rmw_serialize(sample.get(), r.native_ts, &bytes);
      if (ret == RMW_RET_OK) ret = rmw_deserialize(&bytes, r.application_ts, msg);
      if (ret != RMW_RET_OK) return ret;
    } else if (plan && msg)
      plan->copy(sample.get(), msg);
    else if (msg)
      copy_message(*r.members, sample.get(), msg);
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return RMW_RET_ERROR;
  }
  if (info) *info = r.pending.front().info();
  if (loan) {
    const void * ptr = sample.get();
    try {
      r.held.try_emplace(ptr, std::move(sample));
    } catch (const std::bad_alloc &) {
      return RMW_RET_BAD_ALLOC;
    }
    *loan = const_cast<void *>(ptr);
  }
  r.pending.pop_front();
  *taken = true;
  return RMW_RET_OK;
}
static rmw_ret_t take(
  const rmw_subscription_t * sub, void * msg, void ** loan, bool * taken, rmw_message_info_t * info,
  rmw_subscription_allocation_t * allocation = nullptr)
{
  if (!sub || sub->implementation_identifier != standalone::id || (!msg && !loan) || !taken)
    return RMW_RET_INVALID_ARGUMENT;
  auto & r = *static_cast<Reader *>(sub->data);
  r.before_take();
  std::lock_guard lock(r.context->mutex);
  return take_locked(r, msg, loan, taken, info, allocation);
}
rmw_ret_t rmw_init_subscription_allocation(
  const rosidl_message_type_support_t * ts, const rosidl_runtime_c__Sequence__bound *,
  rmw_subscription_allocation_t * allocation)
{
  if (!ts || !allocation || allocation->data || allocation->implementation_identifier)
    return RMW_RET_INVALID_ARGUMENT;
  auto intro = get_message_typesupport_handle(ts, "rosidl_typesupport_introspection_cpp");
  if (!intro) {
    RMW_SET_ERROR_MSG("subscription allocation requires C++ introspection");
    return RMW_RET_UNSUPPORTED;
  }
  try {
    auto plan = std::make_unique<CopyPlan>();
    plan->type = static_cast<const MessageMembers *>(intro->data);
    if (!plan->append(*plan->type, 0)) {
      RMW_SET_ERROR_MSG("subscription allocation requires fixed-size fields");
      return RMW_RET_UNSUPPORTED;
    }
    *allocation = {standalone::id, plan.release()};
    return RMW_RET_OK;
  } catch (const std::bad_alloc &) {
    return RMW_RET_BAD_ALLOC;
  }
}
rmw_ret_t rmw_fini_subscription_allocation(rmw_subscription_allocation_t * a)
{
  if (!a || !a->data) return RMW_RET_INVALID_ARGUMENT;
  if (a->implementation_identifier != standalone::id) return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  delete static_cast<CopyPlan *>(a->data);
  *a = {};
  return RMW_RET_OK;
}
rmw_ret_t rmw_take_sequence(
  const rmw_subscription_t * sub, size_t count, rmw_message_sequence_t * messages,
  rmw_message_info_sequence_t * infos, size_t * taken, rmw_subscription_allocation_t * allocation)
{
  if (
    !sub || !taken || !messages || !infos || !count || messages->capacity < count ||
    infos->capacity < count || !messages->data || !infos->data) {
    RMW_SET_ERROR_MSG("invalid batch take arguments or insufficient capacity");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (sub->implementation_identifier != standalone::id) {
    RMW_SET_ERROR_MSG("subscription belongs to another middleware");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  for (size_t i = 0; i < count; ++i) {
    if (!messages->data[i]) {
      RMW_SET_ERROR_MSG("batch take requires initialized destination messages");
      return RMW_RET_INVALID_ARGUMENT;
    }
  }
  auto & reader = *static_cast<Reader *>(sub->data);
  reader.before_take();
  std::lock_guard lock(reader.context->mutex);
  *taken = 0;
  while (*taken < count) {
    bool received;
    auto result = take_locked(
      reader, messages->data[*taken], nullptr, &received, &infos->data[*taken], allocation);
    if (result != RMW_RET_OK) return result;
    if (!received) break;
    ++*taken;
    messages->size = infos->size = *taken;
  }
  return RMW_RET_OK;
}
rmw_ret_t rmw_take_with_info(
  const rmw_subscription_t * s, void * m, bool * t, rmw_message_info_t * i,
  rmw_subscription_allocation_t * allocation)
{
  return take(s, m, nullptr, t, i, allocation);
}
rmw_ret_t rmw_take_loaned_message_with_info(
  const rmw_subscription_t * s, void ** m, bool * t, rmw_message_info_t * i,
  rmw_subscription_allocation_t *)
{
  return take(s, nullptr, m, t, i);
}
rmw_ret_t rmw_take_loaned_message(
  const rmw_subscription_t * s, void ** m, bool * t, rmw_subscription_allocation_t *)
{
  return take(s, nullptr, m, t, nullptr);
}
rmw_ret_t rmw_return_loaned_message_from_subscription(const rmw_subscription_t * s, void * msg)
{
  if (!s || s->implementation_identifier != standalone::id || !msg) return RMW_RET_INVALID_ARGUMENT;
  auto & r = *static_cast<Reader *>(s->data);
  std::lock_guard lock(r.context->mutex);
  return r.held.erase(msg) ? RMW_RET_OK : RMW_RET_INVALID_ARGUMENT;
}
rmw_ret_t rmw_borrow_loaned_message(
  const rmw_publisher_t * p, const rosidl_message_type_support_t *, void ** msg)
{
  if (!p || p->implementation_identifier != standalone::id || !msg || *msg)
    return RMW_RET_INVALID_ARGUMENT;
  auto & w = *static_cast<Writer *>(p->data);
  if (!p->can_loan_messages) return RMW_RET_UNSUPPORTED;
  std::lock_guard lock(w.mutex);
  try {
    auto loan = w.borrow();
    void * ptr = loan.get();
    try {
      w.borrowed.try_emplace(ptr, std::move(loan));
    } catch (...) {
      w.cancel(std::move(loan));
      throw;
    }
    *msg = ptr;
    return RMW_RET_OK;
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return RMW_RET_ERROR;
  }
}
static rmw_ret_t finish_loan(const rmw_publisher_t * p, void * msg, bool publish)
{
  if (!p || p->implementation_identifier != standalone::id || !msg) return RMW_RET_INVALID_ARGUMENT;
  auto & w = *static_cast<Writer *>(p->data);
  std::lock_guard lock(w.mutex);
  auto it = w.borrowed.find(msg);
  if (it == w.borrowed.end()) return RMW_RET_INVALID_ARGUMENT;
  if (publish && w.cloud_loans) {
    auto cloud = static_cast<Cloud *>(msg);
    try {
      if (cloud->data.get_backend_type() != "cpu")
        throw std::runtime_error("publisher loan requires CPU cloud");
      const bool shared = w.owns_payload(cloud->data, msg);
      HostCloud wire = shared ? HostCloud(std::move(*cloud)) : HostCloud(*cloud);
      static_assert(std::is_nothrow_move_constructible_v<HostCloud>);
      std::destroy_at(cloud);
      new (msg) HostCloud(std::move(wire));
      if (!shared) ++w.private_copies;
      ++w.published_loans;
    } catch (const std::exception & e) {
      RMW_SET_ERROR_MSG(e.what());
      return RMW_RET_ERROR;
    }
  }
  if (publish)
    w.send(std::move(it->second));
  else
    w.cancel(std::move(it->second));
  w.borrowed.erase(it);
  return RMW_RET_OK;
}
rmw_ret_t rmw_publish_loaned_message(
  const rmw_publisher_t * p, void * msg, rmw_publisher_allocation_t *)
{
  return finish_loan(p, msg, true);
}
rmw_ret_t rmw_return_loaned_message_from_publisher(const rmw_publisher_t * p, void * msg)
{
  return finish_loan(p, msg, false);
}
rmw_ret_t rmw_take(
  const rmw_subscription_t * s, void * m, bool * t, rmw_subscription_allocation_t * a)
{
  return rmw_take_with_info(s, m, t, nullptr, a);
}
rmw_ret_t rmw_get_gid_for_publisher(const rmw_publisher_t * p, rmw_gid_t * gid)
{
  if (!p || p->implementation_identifier != standalone::id || !gid) return RMW_RET_INVALID_ARGUMENT;
  *gid = static_cast<Writer *>(p->data)->gid;
  return RMW_RET_OK;
}
rmw_ret_t rmw_compare_gids_equal(const rmw_gid_t * a, const rmw_gid_t * b, bool * equal)
{
  if (
    !a || !b || !equal || a->implementation_identifier != standalone::id ||
    b->implementation_identifier != standalone::id)
    return RMW_RET_INVALID_ARGUMENT;
  *equal = std::memcmp(a->data, b->data, sizeof(a->data)) == 0;
  return RMW_RET_OK;
}
rmw_ret_t rmw_publisher_get_actual_qos(const rmw_publisher_t * p, rmw_qos_profile_t * q)
{
  if (!p || p->implementation_identifier != standalone::id || !q) return RMW_RET_INVALID_ARGUMENT;
  *q = static_cast<Writer *>(p->data)->qos;
  return RMW_RET_OK;
}
rmw_ret_t rmw_subscription_get_actual_qos(const rmw_subscription_t * s, rmw_qos_profile_t * q)
{
  if (!s || s->implementation_identifier != standalone::id || !q) return RMW_RET_INVALID_ARGUMENT;
  *q = static_cast<Reader *>(s->data)->qos;
  return RMW_RET_OK;
}
rmw_ret_t rmw_publisher_count_matched_subscriptions(const rmw_publisher_t * p, size_t * count)
{
  if (!p || p->implementation_identifier != standalone::id || !count)
    return RMW_RET_INVALID_ARGUMENT;
  auto & w = *static_cast<Writer *>(p->data);
  if (!w.internal) return count_compatible(w.owner_node, w.topic, w.type, w.qos, true, count);
  *count = w.native.get_subscription_count();
  return RMW_RET_OK;
}
rmw_ret_t rmw_subscription_count_matched_publishers(const rmw_subscription_t * s, size_t * count)
{
  if (!s || s->implementation_identifier != standalone::id || !count)
    return RMW_RET_INVALID_ARGUMENT;
  *count = 0;
  auto & r = *static_cast<Reader *>(s->data);
  if (!r.internal) return count_compatible(r.owner_node, r.topic, r.type, r.qos, false, count);
  for (auto & route : r.routes) *count += route->get_publisher_count();
  return RMW_RET_OK;
}

rmw_ret_t rmw_qos_profile_check_compatible(
  rmw_qos_profile_t publisher, rmw_qos_profile_t subscription,
  rmw_qos_compatibility_type_t * compatibility, char * reason, size_t reason_size)
{
  if (!compatibility || (!reason && reason_size)) {
    RMW_SET_ERROR_MSG("invalid QoS compatibility output");
    return RMW_RET_INVALID_ARGUMENT;
  }
  const char * publisher_error = resolve_qos(publisher);
  const char * subscription_error = resolve_qos(subscription);
  const bool mismatch = publisher.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT &&
                        subscription.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  *compatibility = RMW_QOS_COMPATIBILITY_OK;
  if (reason_size) reason[0] = '\0';
  size_t used = 0;
  auto report = [&](const char * label, const char * error) {
    if (!error) return;
    *compatibility = RMW_QOS_COMPATIBILITY_ERROR;
    if (used < reason_size) {
      const auto n =
        std::snprintf(reason + used, reason_size - used, "%s%s%s", used ? "; " : "", label, error);
      if (n > 0) used += static_cast<size_t>(n);
    }
  };
  report("publisher: ", publisher_error);
  report("subscription: ", subscription_error);
  if (deadline_ns(publisher.deadline) > deadline_ns(subscription.deadline))
    report("", "offered deadline exceeds requested deadline");
  if (mismatch) report("", "best-effort publisher cannot satisfy reliable subscription");
  if (
    publisher.durability == RMW_QOS_POLICY_DURABILITY_VOLATILE &&
    subscription.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL)
    report("", "volatile publisher cannot satisfy transient-local subscription");
  return RMW_RET_OK;
}

rmw_ret_t rmw_publish_serialized_message(
  const rmw_publisher_t * pub, const rmw_serialized_message_t * msg, rmw_publisher_allocation_t *)
{
  if (!pub || pub->implementation_identifier != standalone::id || !msg)
    return RMW_RET_INVALID_ARGUMENT;
  auto & writer = *static_cast<Writer *>(pub->data);
  try {
    if (writer.cdr_wire) {
      if (!msg->buffer || !msg->buffer_length || msg->buffer_length > msg->buffer_capacity)
        return RMW_RET_INVALID_ARGUMENT;
      writer.publish_bytes(*msg);
      return RMW_RET_OK;
    }
    if (writer.host_cloud) {
      Cloud cloud;
      auto ret = rmw_deserialize(msg, writer.native_ts, &cloud);
      if (ret != RMW_RET_OK) return ret;
      writer.publish_cloud(cloud);
      return RMW_RET_OK;
    }
    auto loan = writer.borrow();
    auto ts = writer.native_ts;
    auto ret = rmw_deserialize(msg, ts, loan.get());
    if (ret != RMW_RET_OK) {
      writer.cancel(std::move(loan));
      return ret;
    }
    writer.send(std::move(loan));
    return RMW_RET_OK;
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return RMW_RET_ERROR;
  }
}
rmw_ret_t rmw_take_serialized_message_with_info(
  const rmw_subscription_t * sub, rmw_serialized_message_t * msg, bool * taken,
  rmw_message_info_t * info, rmw_subscription_allocation_t *)
{
  if (!sub || sub->implementation_identifier != standalone::id || !msg || !taken || !info)
    return RMW_RET_INVALID_ARGUMENT;
  auto & reader = *static_cast<Reader *>(sub->data);
  reader.before_take();
  std::lock_guard lock(reader.context->mutex);
  *taken = false;
  if (!reader.ready()) return RMW_RET_OK;
  auto & sample = reader.pending.front();
  rmw_ret_t ret;
  try {
    if (reader.cdr_wire) {
      const auto & bytes = *static_cast<const std::vector<uint8_t> *>(sample.loan.get());
      if (msg->buffer_capacity < bytes.size()) {
        ret = rmw_serialized_message_resize(msg, bytes.size());
        if (ret != RMW_RET_OK) return ret;
      }
      std::memcpy(msg->buffer, bytes.data(), bytes.size());
      msg->buffer_length = bytes.size();
      ret = RMW_RET_OK;
    } else if (reader.host_cloud) {
      Cloud cloud;
      auto & wire = *static_cast<const HostCloud *>(sample.loan.get());
      wire.restore_metadata(cloud);
      cloud.data = wire.bytes;
      ret = rmw_serialize(&cloud, reader.native_ts, msg);
    } else
      ret = rmw_serialize(sample.loan.get(), reader.native_ts, msg);
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return RMW_RET_ERROR;
  }
  if (ret != RMW_RET_OK) return ret;
  *info = sample.info();
  reader.pending.pop_front();
  *taken = true;
  return RMW_RET_OK;
}
rmw_ret_t rmw_take_serialized_message(
  const rmw_subscription_t * sub, rmw_serialized_message_t * msg, bool * taken,
  rmw_subscription_allocation_t * allocation)
{
  rmw_message_info_t info{};
  return rmw_take_serialized_message_with_info(sub, msg, taken, &info, allocation);
}

rmw_publisher_t * standalone::rpc_writer(
  const rmw_node_t * node, const char * name, const rmw_qos_profile_t & qos)
{
  return &(new Writer(
             node, rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>(),
             name, qos, rmw_get_default_publisher_options(), true))
            ->handle;
}
rmw_subscription_t * standalone::rpc_reader(
  const rmw_node_t * node, const char * name, const rmw_qos_profile_t & qos)
{
  return &(new Reader(
             node, rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>(),
             name, qos, rmw_get_default_subscription_options(), true))
            ->handle;
}

rmw_ret_t rmw_subscription_set_on_new_message_callback(
  rmw_subscription_t * sub, rmw_event_callback_t callback, const void * data)
{
  if (!sub || sub->implementation_identifier != standalone::id) return RMW_RET_INVALID_ARGUMENT;
  auto & r = *static_cast<Reader *>(sub->data);
  if (r.direct && callback) {
    RMW_SET_ERROR_MSG("experimental directly polled topic requires a wait-set executor");
    return RMW_RET_UNSUPPORTED;
  }
  std::lock_guard lock(r.callback_mutex);
  r.callback = callback;
  r.callback_data = data;
  if (callback && r.unreported) {
    auto n = std::exchange(r.unreported, size_t{});
    callback(data, n);
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_subscription_set_content_filter(
  rmw_subscription_t * sub, const rmw_subscription_content_filter_options_t * options)
{
  if (!sub || sub->implementation_identifier != standalone::id || !options)
    return RMW_RET_INVALID_ARGUMENT;
  auto & r = *static_cast<Reader *>(sub->data);
  try {
    if (!r.typed_wire()) return RMW_RET_UNSUPPORTED;
    Filter next(*r.members, *options);
    std::lock_guard lock(r.context->mutex);
    r.filter = std::move(next);
    sub->is_cft_enabled = bool(r.filter.matches);
    return RMW_RET_OK;
  } catch (const std::bad_alloc &) {
    return RMW_RET_BAD_ALLOC;
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return RMW_RET_INVALID_ARGUMENT;
  }
}
rmw_ret_t rmw_subscription_get_content_filter(
  const rmw_subscription_t * sub, rcutils_allocator_t * a,
  rmw_subscription_content_filter_options_t * options)
{
  if (!sub || sub->implementation_identifier != standalone::id || !a || !options)
    return RMW_RET_INVALID_ARGUMENT;
  auto & r = *static_cast<Reader *>(sub->data);
  try {
    std::lock_guard lock(r.context->mutex);
    return r.filter.get(a, options);
  } catch (const std::bad_alloc &) {
    return RMW_RET_BAD_ALLOC;
  }
}

namespace
{
rmw_ret_t init_event(rmw_event_t * handle, Endpoint & ep, bool publisher, rmw_event_type_t kind)
{
  if (!rmw_event_type_is_supported(kind) || publisher != (kind >= RMW_EVENT_LIVELINESS_LOST))
    return RMW_RET_UNSUPPORTED;
  try {
    std::lock_guard lock(ep.events_mutex);
    auto it = ep.events.find(kind);
    if (it == ep.events.end()) {
      auto event = std::shared_ptr<Event>(
        new Event{state(ep.owner_node->context), ep.topic, ep.type, publisher, ep.qos, kind});
      if (!deadline_event(kind)) observe_event(ep.owner_node, event);
      it = ep.events.emplace(kind, event).first;
    }
    *handle = {standalone::id, it->second.get(), kind};
    return RMW_RET_OK;
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return RMW_RET_ERROR;
  }
}
}  // namespace
rmw_ret_t rmw_publisher_event_init(rmw_event_t * h, const rmw_publisher_t * p, rmw_event_type_t e)
{
  if (!h || !p || p->implementation_identifier != standalone::id) return RMW_RET_INVALID_ARGUMENT;
  return init_event(h, *static_cast<Writer *>(p->data), true, e);
}
rmw_ret_t rmw_subscription_event_init(
  rmw_event_t * h, const rmw_subscription_t * s, rmw_event_type_t e)
{
  if (!h || !s || s->implementation_identifier != standalone::id) return RMW_RET_INVALID_ARGUMENT;
  return init_event(h, *static_cast<Reader *>(s->data), false, e);
}

rmw_ret_t rmw_publisher_assert_liveliness(const rmw_publisher_t * p)
{
  if (!p) return RMW_RET_INVALID_ARGUMENT;
  if (p->implementation_identifier != standalone::id) return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  // Accepted native publishers use automatic liveliness with no finite lease.
  return RMW_RET_OK;
}
rmw_ret_t rmw_publisher_wait_for_all_acked(const rmw_publisher_t * p, rmw_time_t)
{
  if (!p) return RMW_RET_INVALID_ARGUMENT;
  if (p->implementation_identifier != standalone::id) return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  if (static_cast<Writer *>(p->data)->qos.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT)
    return RMW_RET_OK;
  RMW_SET_ERROR_MSG("native publish completion has no per-reader acknowledgement");
  return RMW_RET_UNSUPPORTED;
}

bool rmw_feature_supported(rmw_feature_t feature)
{
  return feature == RMW_FEATURE_MESSAGE_INFO_PUBLICATION_SEQUENCE_NUMBER ||
         feature == RMW_FEATURE_MESSAGE_INFO_RECEPTION_SEQUENCE_NUMBER;
}

rmw_ret_t rmw_publisher_get_network_flow_endpoints(
  const rmw_publisher_t * publisher, rcutils_allocator_t * allocator,
  rmw_network_flow_endpoint_array_t * network_flow_endpoint_array)
{
  if (
    !publisher || publisher->implementation_identifier != standalone::id || !allocator ||
    !rcutils_allocator_is_valid(allocator) || !network_flow_endpoint_array)
    return RMW_RET_INVALID_ARGUMENT;
  auto ret = rmw_network_flow_endpoint_array_check_zero(network_flow_endpoint_array);
  return ret == RMW_RET_OK
           ? rmw_network_flow_endpoint_array_init(network_flow_endpoint_array, 0, allocator)
           : ret;
}

rmw_ret_t rmw_subscription_get_network_flow_endpoints(
  const rmw_subscription_t * subscription, rcutils_allocator_t * allocator,
  rmw_network_flow_endpoint_array_t * network_flow_endpoint_array)
{
  if (
    !subscription || subscription->implementation_identifier != standalone::id || !allocator ||
    !rcutils_allocator_is_valid(allocator) || !network_flow_endpoint_array)
    return RMW_RET_INVALID_ARGUMENT;
  auto ret = rmw_network_flow_endpoint_array_check_zero(network_flow_endpoint_array);
  return ret == RMW_RET_OK
           ? rmw_network_flow_endpoint_array_init(network_flow_endpoint_array, 0, allocator)
           : ret;
}
