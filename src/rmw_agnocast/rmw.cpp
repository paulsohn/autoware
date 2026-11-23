#include "filter.hpp"
#include "graph.hpp"

#include <agnocast/node/agnocast_node.hpp>
#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>
#include <rclcpp/typesupport_helpers.hpp>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

#include <std_msgs/msg/string.hpp>

#include <rosidl_runtime_c/type_description/type_description__struct.h>
#include <sys/random.h>

#include <cstdio>
#include <deque>
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
    qos.deadline.sec || qos.deadline.nsec || qos.liveliness_lease_duration.sec ||
    qos.liveliness_lease_duration.nsec ||
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
  std::shared_ptr<rcpputils::SharedLibrary> library;
  const rosidl_message_type_support_t * native_ts;
  const MessageMembers * members;
  rmw_qos_profile_t qos;
  std::shared_ptr<agnocast::Node> node;
  Endpoint(
    const rmw_node_t * owner, const rosidl_message_type_support_t * ts, const char * name,
    const rmw_qos_profile_t & requested, bool rpc)
  : topic(name), application_ts(ts), owner_node(owner), native_ts(ts), qos(requested)
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
    if (has_buffer(*members)) throw std::runtime_error("standalone Buffer layout not implemented");
    if (const char * error = resolve_qos(qos)) throw std::runtime_error(error);
    type = std::string(members->message_namespace_) + "/" + members->message_name_;
    for (auto pos = type.find("::"); pos != std::string::npos; pos = type.find("::"))
      type.replace(pos, 2, "/");
    route = std::string(rpc ? "/_rmw_agnocast/rpc2/d" : "/_rmw_agnocast/v3/d") +
            std::to_string(owner->context->actual_domain_id) + topic + "/" + type;
    static std::once_flag once;
    std::call_once(once, [] { agnocast::init(0, nullptr); });
    rclcpp::NodeOptions options;
    options.use_global_arguments(false).start_parameter_services(false).use_clock_thread(false);
    node = std::make_shared<agnocast::Node>(owner->name, owner->namespace_, options);
  }
  ~Endpoint()
  {
    for (auto & entry : events) {
      auto & event = *entry.second;
      std::lock_guard lock(event.callback_mutex);
      event.callback = nullptr;
    }
  }
  auto ros_qos() const
  {
    auto native = qos;
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

  auto borrow()
  {
    auto loan = native.borrow_loaned_message(members->size_of_ + sizeof(SampleHeader));
    try {
      members->init_function(loan.get(), rosidl_runtime_cpp::MessageInitialization::ALL);
    } catch (...) {
      native.cancel_message(std::move(loan), [](void * p) { ::operator delete(p); });
      throw;
    }
    return loan;
  }
  void destroy(void * p)
  {
    members->fini_function(p);
    ::operator delete(p);
  }
  void cancel(agnocast::ipc_shared_ptr<void> && loan)
  {
    native.cancel_message(std::move(loan), [this](void * p) { destroy(p); });
  }
  void send(agnocast::ipc_shared_ptr<void> && loan)
  {
    std::lock_guard lock(send_mutex);
    SampleHeader header{};
    std::memcpy(header.gid, gid.data, sizeof(header.gid));
    header.sequence = ++sequence;
    header.stamp = system_ns();
    auto duration = rmw_time_total_nsec(qos.lifespan);
    if (duration) {
      auto now = monotonic_ns();
      if (duration < INT64_MAX - now) header.expiry = now + duration;
    }
    std::memcpy(static_cast<char *>(loan.get()) + members->size_of_, &header, sizeof(header));
    native.publish(std::move(loan), [this](void * p) { destroy(p); });
  }
  ~Writer()
  {
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
    if (getrandom(gid.data, sizeof(gid.data), 0) != sizeof(gid.data))
      throw std::runtime_error("native publisher ID unavailable");
    gid.implementation_identifier = id;
    if (!rpc) lease.announce(owner, 'P', topic, type, ts->get_type_hash_func(ts), &qos, &gid);
    handle = {id, this, topic.c_str(), options, !library};
  }
};
struct Reader : Endpoint
{
  Context * context;
  rmw_subscription_t handle{};
  uint64_t sequence{};
  // Native subscriptions must still exist when queued and held loans release their references.
  std::vector<std::shared_ptr<agnocast::Subscription<void>>> routes;
  std::deque<QueuedSample> pending;
  Filter filter;
  std::recursive_mutex callback_mutex;
  rmw_event_callback_t callback{};
  const void * callback_data{};
  size_t unreported{};

  std::map<const void *, Loan> held;
  agnocast::AgnocastOnlySingleThreadedExecutor executor;
  std::thread thread;
  Reader(
    const rmw_node_t * owner, const rosidl_message_type_support_t * ts, const char * name,
    const rmw_qos_profile_t & q, const rmw_subscription_options_t & options, bool rpc = false)
  : Endpoint(owner, ts, name, q, rpc), context(state(owner->context))
  {
    if (options.content_filter_options) filter = Filter(*members, *options.content_filter_options);
    auto attach = [&](const std::string & suffix) {
      agnocast::SubscriptionOptions opt;
      opt.ignore_local_publications = options.ignore_local_publications;
      routes.push_back(
        std::make_shared<agnocast::Subscription<void>>(
          node.get(), route + suffix, type, ros_qos(),
          [this](Loan sample) {
            {
              std::lock_guard lock(context->mutex);
              SampleHeader header;
              std::memcpy(
                &header, static_cast<const char *>(sample.get()) + members->size_of_,
                sizeof(header));
              if (header.expiry && monotonic_ns() >= header.expiry) return;
              if (filter.matches && !filter.matches(sample.get())) return;
              if (pending.size() == qos.depth) pending.pop_front();
              pending.push_back({std::move(sample), header, system_ns(), ++sequence});
            }
            context->changed.notify_all();
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
    handle.can_loan_messages = !library;
    handle.is_cft_enabled = bool(filter.matches);
    handle.is_cft_supported = true;
    if (!rpc) lease.announce(owner, 'S', topic, type, ts->get_type_hash_func(ts), &qos);
    executor.add_node(node);
    thread = std::thread([this] { executor.spin(); });
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
    thread.join();
  }
};
}  // namespace
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
  Reader & r, void * msg, void ** loan, bool * taken, rmw_message_info_t * info)
{
  *taken = false;
  if (!r.ready()) return RMW_RET_OK;
  auto & sample = r.pending.front().loan;
  try {
    if (r.library) {
      if (loan) return RMW_RET_UNSUPPORTED;
      rclcpp::SerializedMessage scratch;
      auto & bytes = scratch.get_rcl_serialized_message();
      auto ret = rmw_serialize(sample.get(), r.native_ts, &bytes);
      if (ret == RMW_RET_OK) ret = rmw_deserialize(&bytes, r.application_ts, msg);
      if (ret != RMW_RET_OK) return ret;
    } else if (msg)
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
  const rmw_subscription_t * sub, void * msg, void ** loan, bool * taken, rmw_message_info_t * info)
{
  if (!sub || sub->implementation_identifier != standalone::id || (!msg && !loan) || !taken)
    return RMW_RET_INVALID_ARGUMENT;
  auto & r = *static_cast<Reader *>(sub->data);
  std::lock_guard lock(r.context->mutex);
  return take_locked(r, msg, loan, taken, info);
}
rmw_ret_t rmw_take_sequence(
  const rmw_subscription_t * sub, size_t count, rmw_message_sequence_t * messages,
  rmw_message_info_sequence_t * infos, size_t * taken, rmw_subscription_allocation_t *)
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
  std::lock_guard lock(reader.context->mutex);
  *taken = 0;
  while (*taken < count && reader.ready()) {
    bool received;
    auto result =
      take_locked(reader, messages->data[*taken], nullptr, &received, &infos->data[*taken]);
    if (result != RMW_RET_OK) return result;
    if (!received) break;
    ++*taken;
    messages->size = infos->size = *taken;
  }
  return RMW_RET_OK;
}
rmw_ret_t rmw_take_with_info(
  const rmw_subscription_t * s, void * m, bool * t, rmw_message_info_t * i,
  rmw_subscription_allocation_t *)
{
  return take(s, m, nullptr, t, i);
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
  if (w.library) return RMW_RET_UNSUPPORTED;
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
  *count = static_cast<Writer *>(p->data)->native.get_subscription_count();
  return RMW_RET_OK;
}
rmw_ret_t rmw_subscription_count_matched_publishers(const rmw_subscription_t * s, size_t * count)
{
  if (!s || s->implementation_identifier != standalone::id || !count)
    return RMW_RET_INVALID_ARGUMENT;
  *count = 0;
  for (auto & route : static_cast<Reader *>(s->data)->routes)
    *count += route->get_publisher_count();
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
  std::lock_guard lock(reader.context->mutex);
  *taken = false;
  if (!reader.ready()) return RMW_RET_OK;
  auto & sample = reader.pending.front();
  auto ret = rmw_serialize(sample.loan.get(), reader.native_ts, msg);
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
      observe_event(ep.owner_node, event);
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
