#include "standalone.hpp"

#include <agnocast/node/agnocast_node.hpp>
#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>
#include <rcl_interfaces/msg/detail/log__rosidl_typesupport_introspection_cpp.hpp>
#include <rcl_interfaces/msg/log.hpp>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

#include <rcl_interfaces/msg/detail/log__rosidl_typesupport_introspection_c.h>
#include <rcl_interfaces/msg/detail/log__struct.h>
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
  bool c_log = false;
  const MessageMembers * members;
  rmw_qos_profile_t qos;
  std::shared_ptr<agnocast::Node> node;
  Endpoint(
    const rmw_node_t * owner, const rosidl_message_type_support_t * ts, const char * name,
    const rmw_qos_profile_t & requested)
  : topic(name), qos(requested)
  {
    auto intro = get_message_typesupport_handle(ts, "rosidl_typesupport_introspection_cpp");
    if (!intro) {
      rmw_reset_error();
      auto c = get_message_typesupport_handle(ts, "rosidl_typesupport_introspection_c");
      if (
        !c || c != ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(
                     rosidl_typesupport_introspection_c, rcl_interfaces, msg, Log)()) {
        throw std::runtime_error("native endpoint requires C++ introspection or a C Log publisher");
      }
      c_log = true;
      intro = ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(
        rosidl_typesupport_introspection_cpp, rcl_interfaces, msg, Log)();
    }
    members = static_cast<const MessageMembers *>(intro->data);
    if (has_buffer(*members)) throw std::runtime_error("standalone Buffer layout not implemented");
    if (const char * error = resolve_qos(qos)) throw std::runtime_error(error);
    type = std::string(members->message_namespace_) + "/" + members->message_name_;
    for (auto pos = type.find("::"); pos != std::string::npos; pos = type.find("::"))
      type.replace(pos, 2, "/");
    route =
      "/_rmw_agnocast/v2/d" + std::to_string(owner->context->actual_domain_id) + topic + "/" + type;
    static std::once_flag once;
    std::call_once(once, [] { agnocast::init(0, nullptr); });
    rclcpp::NodeOptions options;
    options.use_global_arguments(false).start_parameter_services(false).use_clock_thread(false);
    node = std::make_shared<agnocast::Node>(owner->name, owner->namespace_, options);
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
struct Writer : Endpoint
{
  agnocast::TypeErasedPublisher native;
  rmw_gid_t gid{};
  rmw_publisher_t handle{};
  std::mutex mutex;
  std::map<void *, agnocast::ipc_shared_ptr<void>> borrowed;

  auto borrow()
  {
    auto loan =
      native.borrow_loaned_message(members->size_of_ + sizeof(gid.data) + sizeof(int64_t));
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
    std::memcpy(static_cast<char *>(loan.get()) + members->size_of_, gid.data, sizeof(gid.data));
    const auto duration = rmw_time_total_nsec(qos.lifespan);
    int64_t expires = 0;
    if (duration) {
      const auto now = monotonic_ns();
      if (duration < INT64_MAX - now) expires = now + duration;
    }
    std::memcpy(
      static_cast<char *>(loan.get()) + members->size_of_ + sizeof(gid.data), &expires,
      sizeof(expires));
    native.publish(std::move(loan), [this](void * p) { destroy(p); });
  }
  ~Writer()
  {
    for (auto & entry : borrowed) cancel(std::move(entry.second));
  }
  Writer(
    const rmw_node_t * owner, const rosidl_message_type_support_t * ts, const char * name,
    const rmw_qos_profile_t & q, const rmw_publisher_options_t & options)
  : Endpoint(owner, ts, name, q),
    native(
      node.get(),
      route + (qos.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE ? "/r" : "/b") +
        (qos.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL ? "/t" : "/v"),
      type, ros_qos(), {}, agnocast::PublisherRole::AgnocastOnly)
  {
    if (getrandom(gid.data, sizeof(gid.data), 0) != sizeof(gid.data))
      throw std::runtime_error("native publisher ID unavailable");
    gid.implementation_identifier = id;
    handle = {id, this, topic.c_str(), options, !c_log};
  }
};
struct Reader : Endpoint
{
  Context * context;
  rmw_subscription_t handle{};
  std::deque<Loan> pending;
  std::map<const void *, Loan> held;
  std::vector<std::shared_ptr<agnocast::Subscription<void>>> routes;
  agnocast::AgnocastOnlySingleThreadedExecutor executor;
  std::thread thread;
  Reader(
    const rmw_node_t * owner, const rosidl_message_type_support_t * ts, const char * name,
    const rmw_qos_profile_t & q, const rmw_subscription_options_t & options)
  : Endpoint(owner, ts, name, q), context(state(owner->context))
  {
    if (c_log) throw std::runtime_error("C Log subscriptions require a C destination adapter");
    auto attach = [&](const std::string & suffix) {
      agnocast::SubscriptionOptions opt;
      opt.ignore_local_publications = options.ignore_local_publications;
      routes.push_back(
        std::make_shared<agnocast::Subscription<void>>(
          node.get(), route + suffix, type, ros_qos(),
          [this](Loan sample) {
            {
              std::lock_guard lock(context->mutex);
              if (expired(sample)) return;
              if (pending.size() == qos.depth) pending.pop_front();
              pending.push_back(std::move(sample));
            }
            context->changed.notify_all();
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
    handle.can_loan_messages = true;
    executor.add_node(node);
    thread = std::thread([this] { executor.spin(); });
  }
  bool expired(const Loan & sample) const
  {
    int64_t expiry;
    std::memcpy(
      &expiry,
      static_cast<const char *>(sample.get()) + members->size_of_ + sizeof(rmw_gid_t::data),
      sizeof(expiry));
    return expiry && monotonic_ns() >= expiry;
  }
  bool ready()
  {
    while (!pending.empty() && expired(pending.front())) pending.pop_front();
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
      if (w.c_log) {
        const auto & src = *static_cast<const rcl_interfaces__msg__Log *>(msg);
        auto & dst = *static_cast<rcl_interfaces::msg::Log *>(loan.get());
        dst.stamp.sec = src.stamp.sec;
        dst.stamp.nanosec = src.stamp.nanosec;
        dst.level = src.level;
        dst.line = src.line;
        dst.name.assign(src.name.data, src.name.size);
        dst.msg.assign(src.msg.data, src.msg.size);
        dst.file.assign(src.file.data, src.file.size);
        dst.function.assign(src.function.data, src.function.size);
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
  auto & sample = r.pending.front();
  try {
    if (msg) copy_message(*r.members, sample.get(), msg);
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return RMW_RET_ERROR;
  }
  if (info) {
    *info = {};
    info->publisher_gid.implementation_identifier = standalone::id;
    std::memcpy(
      info->publisher_gid.data, static_cast<const char *>(sample.get()) + r.members->size_of_,
      sizeof(info->publisher_gid.data));
  }
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
  if (w.c_log) return RMW_RET_UNSUPPORTED;
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
