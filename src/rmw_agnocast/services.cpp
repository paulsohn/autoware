#include "graph.hpp"

#include <rclcpp/serialized_message.hpp>

#include <std_msgs/msg/string.hpp>

#include <rosidl_runtime_c/type_description/type_description__struct.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>

namespace
{
using namespace standalone;
std::string hex(const uint8_t * bytes, size_t size)
{
  constexpr char digits[] = "0123456789abcdef";
  std::string str;
  for (size_t i = 0; i < size; ++i) {
    str += digits[bytes[i] >> 4];
    str += digits[bytes[i] & 15];
  }
  return str;
}
struct Rpc
{
  const rmw_node_t * node;
  std::string name, path;
  Lease lease;
  const rosidl_service_type_support_t * ts;
  rmw_qos_profile_t qos;
  rmw_subscription_t * reader{};
  rmw_publisher_t * writer{};
  rmw_gid_t gid{};
  std::atomic<int64_t> next{1};
  std::mutex mutex;
  std::map<std::string, rmw_publisher_t *> replies;
  Rpc(
    const rmw_node_t * n, const char * service, const rosidl_service_type_support_t * support,
    const rmw_qos_profile_t & q)
  : node(n), name(service), ts(support), qos(q)
  {
    if (
      !ts->request_typesupport || !ts->response_typesupport || !ts->get_type_hash_func ||
      !ts->get_type_description_func)
      throw std::runtime_error("service needs request/response typesupport and a type hash");
    auto hash = ts->get_type_hash_func(ts);
    if (!hash || !hash->version) throw std::runtime_error("service type has no hash");
    path = name + "/h" + hex(hash->value, sizeof(hash->value));
  }
  ~Rpc()
  {
    auto n = const_cast<rmw_node_t *>(node);
    if (reader) (void)rmw_destroy_subscription(n, reader);
    if (writer) (void)rmw_destroy_publisher(n, writer);
    for (auto & reply : replies) (void)rmw_destroy_publisher(n, reply.second);
  }
};
template <class Handle>
Rpc * rpc(const Handle * handle)
{
  return handle && handle->implementation_identifier == id ? static_cast<Rpc *>(handle->data)
                                                           : nullptr;
}
template <class Handle>
Handle * create(
  const rmw_node_t * n, const rosidl_service_type_support_t * ts, const char * name,
  const rmw_qos_profile_t * qos)
{
  if (!n || n->implementation_identifier != id || !ts || !name || !qos) return nullptr;
  try {
    auto ep = std::make_unique<Rpc>(n, name, ts, *qos);
    if constexpr (std::is_same_v<Handle, rmw_client_t>) {
      ep->writer = rpc_writer(n, (ep->path + "/request").c_str(), *qos);
      if (rmw_get_gid_for_publisher(ep->writer, &ep->gid) != RMW_RET_OK) return nullptr;
      ep->reader =
        rpc_reader(n, (ep->path + "/c" + hex(ep->gid.data, sizeof(ep->gid.data))).c_str(), *qos);
    } else
      ep->reader = rpc_reader(n, (ep->path + "/request").c_str(), *qos);
    const auto * desc = ts->get_type_description_func(ts);
    rmw_qos_profile_t actual{};
    if (!desc || rmw_subscription_get_actual_qos(ep->reader, &actual) != RMW_RET_OK) return nullptr;
    ep->lease.announce(
      n, std::is_same_v<Handle, rmw_client_t> ? 'C' : 'V', name,
      desc->type_description.type_name.data, ts->get_type_hash_func(ts), &actual,
      ep->writer ? &ep->gid : nullptr);
    auto handle = new Handle{id, ep.get(), ep->name.c_str()};
    ep.release();
    return handle;
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return nullptr;
  }
}
template <class H>
rmw_ret_t destroy(rmw_node_t * n, H * h)
{
  auto ep = rpc(h);
  if (!ep || ep->node != n) return RMW_RET_INVALID_ARGUMENT;
  delete ep;
  delete h;
  return RMW_RET_OK;
}
int64_t wall_ns()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
           std::chrono::system_clock::now().time_since_epoch())
    .count();
}
struct Header
{
  rmw_request_id_t request;
  int64_t stamp;
};
rmw_ret_t send(
  rmw_publisher_t * pub, const rosidl_message_type_support_t * ts, const rmw_request_id_t & id,
  const void * msg)
{
  try {
    rclcpp::SerializedMessage bytes;
    auto & data = bytes.get_rcl_serialized_message();
    auto ret = rmw_serialize(msg, ts, &data);
    if (ret != RMW_RET_OK) return ret;
    Header header{id, wall_ns()};
    std_msgs::msg::String frame;
    frame.data.assign(reinterpret_cast<const char *>(&header), sizeof(header));
    frame.data.append(reinterpret_cast<const char *>(data.buffer), data.buffer_length);
    return rmw_publish(pub, &frame, nullptr);
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return RMW_RET_ERROR;
  }
}
rmw_ret_t receive(
  Rpc * ep, const rosidl_message_type_support_t * ts, rmw_service_info_t * info, void * msg,
  bool * taken)
{
  if (!ep || !info || !msg || !taken) return RMW_RET_INVALID_ARGUMENT;
  std_msgs::msg::String frame;
  auto ret = rmw_take(ep->reader, &frame, taken, nullptr);
  if (ret != RMW_RET_OK || !*taken) return ret;
  *taken = false;
  if (frame.data.size() < sizeof(Header)) {
    RMW_SET_ERROR_MSG("invalid service frame");
    return RMW_RET_ERROR;
  }
  Header header;
  std::memcpy(&header, frame.data.data(), sizeof(header));
  rmw_serialized_message_t bytes{};
  bytes.buffer = reinterpret_cast<uint8_t *>(frame.data.data() + sizeof(header));
  bytes.buffer_length = bytes.buffer_capacity = frame.data.size() - sizeof(header);
  ret = rmw_deserialize(&bytes, ts, msg);
  if (ret == RMW_RET_OK) {
    info->request_id = header.request;
    info->source_timestamp = header.stamp;
    info->received_timestamp = wall_ns();
    *taken = true;
  }
  return ret;
}
}  // namespace
bool standalone::rpc_ready(void * ep)
{
  return ready(static_cast<Rpc *>(ep)->reader->data);
}
rmw_service_t * rmw_create_service(
  const rmw_node_t * n, const rosidl_service_type_support_t * t, const char * s,
  const rmw_qos_profile_t * q)
{
  return create<rmw_service_t>(n, t, s, q);
}
rmw_client_t * rmw_create_client(
  const rmw_node_t * n, const rosidl_service_type_support_t * t, const char * s,
  const rmw_qos_profile_t * q)
{
  return create<rmw_client_t>(n, t, s, q);
}
rmw_ret_t rmw_destroy_service(rmw_node_t * n, rmw_service_t * h)
{
  return destroy(n, h);
}
rmw_ret_t rmw_destroy_client(rmw_node_t * n, rmw_client_t * h)
{
  return destroy(n, h);
}
rmw_ret_t rmw_take_request(
  const rmw_service_t * h, rmw_service_info_t * info, void * msg, bool * taken)
{
  auto ep = rpc(h);
  return ep ? receive(ep, ep->ts->request_typesupport, info, msg, taken) : RMW_RET_INVALID_ARGUMENT;
}
rmw_ret_t rmw_take_response(
  const rmw_client_t * h, rmw_service_info_t * info, void * msg, bool * taken)
{
  auto ep = rpc(h);
  return ep ? receive(ep, ep->ts->response_typesupport, info, msg, taken)
            : RMW_RET_INVALID_ARGUMENT;
}
rmw_ret_t rmw_send_request(const rmw_client_t * h, const void * msg, int64_t * seq)
{
  auto ep = rpc(h);
  if (!ep || !msg || !seq) return RMW_RET_INVALID_ARGUMENT;
  rmw_request_id_t id{};
  std::memcpy(id.writer_guid, ep->gid.data, sizeof(id.writer_guid));
  id.sequence_number = ep->next++;
  auto ret = send(ep->writer, ep->ts->request_typesupport, id, msg);
  if (ret == RMW_RET_OK) *seq = id.sequence_number;
  return ret;
}
rmw_ret_t rmw_send_response(const rmw_service_t * h, rmw_request_id_t * id, void * msg)
{
  auto ep = rpc(h);
  if (!ep || !msg || !id) return RMW_RET_INVALID_ARGUMENT;
  try {
    std::lock_guard lock(ep->mutex);
    auto key = hex(id->writer_guid, sizeof(id->writer_guid));
    auto it = ep->replies.find(key);
    if (it == ep->replies.end()) {
      for (auto old = ep->replies.begin(); old != ep->replies.end();) {
        size_t matched;
        if (
          rmw_publisher_count_matched_subscriptions(old->second, &matched) == RMW_RET_OK &&
          !matched) {
          (void)rmw_destroy_publisher(const_cast<rmw_node_t *>(ep->node), old->second);
          old = ep->replies.erase(old);
        } else
          ++old;
      }
      it = ep->replies.emplace(key, nullptr).first;
      try {
        it->second = rpc_writer(ep->node, (ep->path + "/c" + key).c_str(), ep->qos);
      } catch (...) {
        ep->replies.erase(it);
        throw;
      }
    }
    return send(it->second, ep->ts->response_typesupport, *id, msg);
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return RMW_RET_ERROR;
  }
}
rmw_ret_t rmw_service_server_is_available(
  const rmw_node_t * n, const rmw_client_t * h, bool * available)
{
  auto ep = rpc(h);
  if (!ep || ep->node != n || !available) return RMW_RET_INVALID_ARGUMENT;
  size_t matched;
  auto ret = rmw_publisher_count_matched_subscriptions(ep->writer, &matched);
  if (ret == RMW_RET_OK) *available = matched > 0;
  return ret;
}
rmw_ret_t rmw_get_gid_for_client(const rmw_client_t * h, rmw_gid_t * gid)
{
  auto ep = rpc(h);
  if (!ep || !gid) return RMW_RET_INVALID_ARGUMENT;
  *gid = ep->gid;
  return RMW_RET_OK;
}
rmw_ret_t rmw_client_request_publisher_get_actual_qos(const rmw_client_t * h, rmw_qos_profile_t * q)
{
  auto ep = rpc(h);
  return ep ? rmw_subscription_get_actual_qos(ep->reader, q) : RMW_RET_INVALID_ARGUMENT;
}
rmw_ret_t rmw_client_response_subscription_get_actual_qos(
  const rmw_client_t * h, rmw_qos_profile_t * q)
{
  auto ep = rpc(h);
  return ep ? rmw_subscription_get_actual_qos(ep->reader, q) : RMW_RET_INVALID_ARGUMENT;
}
rmw_ret_t rmw_service_request_subscription_get_actual_qos(
  const rmw_service_t * h, rmw_qos_profile_t * q)
{
  auto ep = rpc(h);
  return ep ? rmw_subscription_get_actual_qos(ep->reader, q) : RMW_RET_INVALID_ARGUMENT;
}
rmw_ret_t rmw_service_response_publisher_get_actual_qos(
  const rmw_service_t * h, rmw_qos_profile_t * q)
{
  auto ep = rpc(h);
  return ep ? rmw_subscription_get_actual_qos(ep->reader, q) : RMW_RET_INVALID_ARGUMENT;
}
rmw_ret_t rmw_client_set_on_new_response_callback(
  rmw_client_t * h, rmw_event_callback_t callback, const void * data)
{
  auto ep = rpc(h);
  return ep ? rmw_subscription_set_on_new_message_callback(ep->reader, callback, data)
            : RMW_RET_INVALID_ARGUMENT;
}
rmw_ret_t rmw_service_set_on_new_request_callback(
  rmw_service_t * h, rmw_event_callback_t callback, const void * data)
{
  auto ep = rpc(h);
  return ep ? rmw_subscription_set_on_new_message_callback(ep->reader, callback, data)
            : RMW_RET_INVALID_ARGUMENT;
}
