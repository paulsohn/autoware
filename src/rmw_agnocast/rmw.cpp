#include "endpoints.hpp"
#include "host_cloud_view.hpp"

#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <std_msgs/msg/string.hpp>

#include <rmw/features.h>
#include <rmw/get_network_flow_endpoints.h>

namespace
{
using namespace standalone;
using namespace standalone::endpoints;
using namespace rosidl_typesupport_introspection_cpp;

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
}  // namespace

rmw_publisher_t * rmw_create_publisher(
  const rmw_node_t * n, const rosidl_message_type_support_t * t, const char * name,
  const rmw_qos_profile_t * q, const rmw_publisher_options_t * o)
{
  if (
    !n || n->implementation_identifier != standalone::id || !t || !name || !q || !o ||
    (!q->avoid_ros_namespace_conventions && !valid_ros_topic(name)))
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
  if (
    !n || n->implementation_identifier != standalone::id || !t || !name || !q || !o ||
    (!q->avoid_ros_namespace_conventions && !valid_ros_topic(name)))
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
namespace
{
template <class Native, class Handle>
rmw_ret_t destroy_endpoint(rmw_node_t * node, Handle * handle)
{
  if (!node || !handle) {
    RMW_SET_ERROR_MSG("endpoint destruction requires a node and an endpoint");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (
    node->implementation_identifier != standalone::id ||
    handle->implementation_identifier != standalone::id) {
    RMW_SET_ERROR_MSG("endpoint or node belongs to a different middleware implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  auto * endpoint = static_cast<Native *>(handle->data);
  if (!endpoint || endpoint->owner_node != node) {
    RMW_SET_ERROR_MSG("node does not own this endpoint");
    return RMW_RET_INVALID_ARGUMENT;
  }
  delete endpoint;
  return RMW_RET_OK;
}
}  // namespace

rmw_ret_t rmw_destroy_publisher(rmw_node_t * node, rmw_publisher_t * publisher)
{
  return destroy_endpoint<Writer>(node, publisher);
}

rmw_ret_t rmw_destroy_subscription(rmw_node_t * node, rmw_subscription_t * subscription)
{
  return destroy_endpoint<Reader>(node, subscription);
}
rmw_ret_t rmw_publish(const rmw_publisher_t * p, const void * msg, rmw_publisher_allocation_t *)
{
  if (auto ret = check_initialized_handle(p); ret != RMW_RET_OK) return ret;
  if (!msg) return RMW_RET_INVALID_ARGUMENT;
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
  if (auto ret = check_initialized_handle(sub); ret != RMW_RET_OK) return ret;
  if (loan && !sub->can_loan_messages) return RMW_RET_UNSUPPORTED;
  if ((!msg && !loan) || !taken) return RMW_RET_INVALID_ARGUMENT;
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
  if (!i) return RMW_RET_INVALID_ARGUMENT;
  return take(s, m, nullptr, t, i, allocation);
}
rmw_ret_t rmw_take_loaned_message_with_info(
  const rmw_subscription_t * s, void ** m, bool * t, rmw_message_info_t * i,
  rmw_subscription_allocation_t *)
{
  if (!i) return RMW_RET_INVALID_ARGUMENT;
  return take(s, nullptr, m, t, i);
}
rmw_ret_t rmw_take_loaned_message(
  const rmw_subscription_t * s, void ** m, bool * t, rmw_subscription_allocation_t *)
{
  return take(s, nullptr, m, t, nullptr);
}
rmw_ret_t rmw_return_loaned_message_from_subscription(const rmw_subscription_t * s, void * msg)
{
  if (auto ret = check_initialized_handle(s); ret != RMW_RET_OK) return ret;
  if (!s->can_loan_messages) return RMW_RET_UNSUPPORTED;
  if (!msg) return RMW_RET_INVALID_ARGUMENT;
  auto & r = *static_cast<Reader *>(s->data);
  std::lock_guard lock(r.context->mutex);
  return r.held.erase(msg) ? RMW_RET_OK : RMW_RET_INVALID_ARGUMENT;
}
rmw_ret_t rmw_borrow_loaned_message(
  const rmw_publisher_t * p, const rosidl_message_type_support_t * ts, void ** msg)
{
  if (auto ret = check_initialized_handle(p); ret != RMW_RET_OK) return ret;
  if (!ts || !msg || *msg) return RMW_RET_INVALID_ARGUMENT;
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
  if (auto ret = check_initialized_handle(p); ret != RMW_RET_OK) return ret;
  if (!p->can_loan_messages) return RMW_RET_UNSUPPORTED;
  if (!msg) return RMW_RET_INVALID_ARGUMENT;
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
  return take(s, m, nullptr, t, nullptr, a);
}
rmw_ret_t rmw_get_gid_for_publisher(const rmw_publisher_t * p, rmw_gid_t * gid)
{
  if (auto ret = check_initialized_handle(p); ret != RMW_RET_OK) return ret;
  if (!gid) return RMW_RET_INVALID_ARGUMENT;
  *gid = static_cast<Writer *>(p->data)->gid;
  return RMW_RET_OK;
}
rmw_ret_t rmw_compare_gids_equal(const rmw_gid_t * a, const rmw_gid_t * b, bool * equal)
{
  if (!equal) return RMW_RET_INVALID_ARGUMENT;
  if (auto ret = check_initialized_handle(a); ret != RMW_RET_OK) return ret;
  if (auto ret = check_initialized_handle(b); ret != RMW_RET_OK) return ret;
  *equal = std::memcmp(a->data, b->data, sizeof(a->data)) == 0;
  return RMW_RET_OK;
}
rmw_ret_t rmw_publisher_get_actual_qos(const rmw_publisher_t * p, rmw_qos_profile_t * q)
{
  if (auto ret = check_initialized_handle(p); ret != RMW_RET_OK) return ret;
  if (!q) return RMW_RET_INVALID_ARGUMENT;
  *q = static_cast<Writer *>(p->data)->qos;
  return RMW_RET_OK;
}
rmw_ret_t rmw_subscription_get_actual_qos(const rmw_subscription_t * s, rmw_qos_profile_t * q)
{
  if (auto ret = check_initialized_handle(s); ret != RMW_RET_OK) return ret;
  if (!q) return RMW_RET_INVALID_ARGUMENT;
  *q = static_cast<Reader *>(s->data)->qos;
  return RMW_RET_OK;
}
rmw_ret_t rmw_publisher_count_matched_subscriptions(const rmw_publisher_t * p, size_t * count)
{
  if (auto ret = check_initialized_handle(p); ret != RMW_RET_OK) return ret;
  if (!count) return RMW_RET_INVALID_ARGUMENT;
  auto & w = *static_cast<Writer *>(p->data);
  if (!w.internal) return count_compatible(w.owner_node, w.topic, w.type, w.qos, true, count);
  *count = w.native.get_subscription_count();
  return RMW_RET_OK;
}
rmw_ret_t rmw_subscription_count_matched_publishers(const rmw_subscription_t * s, size_t * count)
{
  if (auto ret = check_initialized_handle(s); ret != RMW_RET_OK) return ret;
  if (!count) return RMW_RET_INVALID_ARGUMENT;
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
  resolve_defaults(publisher);
  resolve_defaults(subscription);
  const bool mismatch = publisher.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT &&
                        subscription.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  *compatibility = RMW_QOS_COMPATIBILITY_OK;
  if (reason_size) reason[0] = '\0';
  size_t used = 0;
  auto report =
    [&](const char * error, rmw_qos_compatibility_type_t severity = RMW_QOS_COMPATIBILITY_ERROR) {
      if (*compatibility != RMW_QOS_COMPATIBILITY_ERROR) *compatibility = severity;
      if (used < reason_size) {
        const auto n =
          std::snprintf(reason + used, reason_size - used, "%s%s", used ? "; " : "", error);
        if (n > 0) used += static_cast<size_t>(n);
      }
    };
  auto concrete_kind = [](auto value) {
    return value == RMW_QOS_POLICY_LIVELINESS_AUTOMATIC ||
           value == RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC;
  };
  for (const auto & profile : {publisher, subscription}) {
    if (
      !concrete_kind(profile.liveliness) ||
      (profile.reliability != RMW_QOS_POLICY_RELIABILITY_RELIABLE &&
       profile.reliability != RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT) ||
      (profile.durability != RMW_QOS_POLICY_DURABILITY_VOLATILE &&
       profile.durability != RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL)) {
      report("endpoint policy is unresolved", RMW_QOS_COMPATIBILITY_WARNING);
      break;
    }
  }
  if (
    concrete_kind(publisher.liveliness) && concrete_kind(subscription.liveliness) &&
    publisher.liveliness < subscription.liveliness)
    report("offered liveliness kind cannot satisfy the request");
  if (
    deadline_ns(publisher.liveliness_lease_duration) >
    deadline_ns(subscription.liveliness_lease_duration))
    report("offered liveliness lease exceeds requested lease");
  if (deadline_ns(publisher.deadline) > deadline_ns(subscription.deadline))
    report("offered deadline exceeds requested deadline");
  if (mismatch) report("best-effort publisher cannot satisfy reliable subscription");
  if (
    publisher.durability == RMW_QOS_POLICY_DURABILITY_VOLATILE &&
    subscription.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL)
    report("volatile publisher cannot satisfy transient-local subscription");
  return RMW_RET_OK;
}

rmw_ret_t rmw_publish_serialized_message(
  const rmw_publisher_t * pub, const rmw_serialized_message_t * msg, rmw_publisher_allocation_t *)
{
  if (auto ret = check_initialized_handle(pub); ret != RMW_RET_OK) return ret;
  if (!msg) return RMW_RET_INVALID_ARGUMENT;
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
  if (auto ret = check_initialized_handle(sub); ret != RMW_RET_OK) return ret;
  if (!msg || !taken || !info) return RMW_RET_INVALID_ARGUMENT;
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
  if (auto ret = check_initialized_handle(sub); ret != RMW_RET_OK) return ret;
  if (!options) return RMW_RET_INVALID_ARGUMENT;
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
  if (auto ret = check_initialized_handle(sub); ret != RMW_RET_OK) return ret;
  if (!a || !options) return RMW_RET_INVALID_ARGUMENT;
  auto & r = *static_cast<Reader *>(sub->data);
  try {
    std::lock_guard lock(r.context->mutex);
    if (!r.filter.matches) {
      RMW_SET_ERROR_MSG("subscription has no active content filter");
      return RMW_RET_ERROR;
    }
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
      if (!deadline_event(kind) && kind != RMW_EVENT_MESSAGE_LOST)
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
