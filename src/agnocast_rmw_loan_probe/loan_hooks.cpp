#include "loan_hooks.hpp"

#include <agnocast/agnocast_publisher.hpp>
#include <agnocast/agnocast_subscription.hpp>

#include <dlfcn.h>
#include <rmw/rmw.h>

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>

namespace
{
using namespace probe;
using Writer = agnocast::Publisher<Message>;
using Reader = agnocast::TakeSubscription<Message>;
std::unique_ptr<Writer> writer;
std::unique_ptr<Reader> reader;
agnocast::ipc_shared_ptr<Message> outgoing;
std::map<const void *, agnocast::ipc_shared_ptr<const Message>> incoming;
std::mutex incoming_mutex;
std::atomic_uint return_count{0};
bool selected(const char * name)
{
  return name && std::strcmp(name, "/trajectory_loan_probe") == 0;
}
template <typename F>
F next(const char * symbol)
{
  auto fn = reinterpret_cast<F>(dlsym(RTLD_NEXT, symbol));
  if (!fn) std::abort();
  return fn;
}
}  // namespace
namespace probe
{
void attach(rclcpp::Node & node, bool publishing)
{
  const char * name = std::getenv("NATIVE_PROBE_TOPIC");
  if (!name) throw std::runtime_error("missing native topic");
  if (publishing) {
    writer = std::make_unique<Writer>(
      &node, name, rclcpp::QoS(4), agnocast::PublisherOptions{},
      agnocast::PublisherRole::AgnocastOnly);
  } else {
    reader = std::make_unique<Reader>(
      &node, name, rclcpp::QoS(4), agnocast::SubscriptionOptions{},
      agnocast::SubscriptionRole::AgnocastOnly);
  }
}
void detach()
{
  reader.reset();
  writer.reset();
}
unsigned returns()
{
  return return_count.load();
}
}  // namespace probe

// These test-only ELF interpositions exercise the real rcl/rclcpp loan path.
// The selected topic uses Cyclone solely for the wait set and empty wakeup samples.
extern "C" rmw_publisher_t * rmw_create_publisher(
  const rmw_node_t * node, const rosidl_message_type_support_t * ts, const char * name,
  const rmw_qos_profile_t * qos, const rmw_publisher_options_t * opts)
{
  auto * pub = next<decltype(&rmw_create_publisher)>(__func__)(node, ts, name, qos, opts);
  if (pub && selected(name)) pub->can_loan_messages = true;
  return pub;
}
extern "C" rmw_subscription_t * rmw_create_subscription(
  const rmw_node_t * node, const rosidl_message_type_support_t * ts, const char * name,
  const rmw_qos_profile_t * qos, const rmw_subscription_options_t * opts)
{
  auto * sub = next<decltype(&rmw_create_subscription)>(__func__)(node, ts, name, qos, opts);
  if (sub && selected(name)) sub->can_loan_messages = true;
  return sub;
}
extern "C" rmw_ret_t rmw_borrow_loaned_message(
  const rmw_publisher_t * pub, const rosidl_message_type_support_t * ts, void ** msg)
{
  if (!selected(pub->topic_name))
    return next<decltype(&rmw_borrow_loaned_message)>(__func__)(pub, ts, msg);
  if (!writer || outgoing) return RMW_RET_ERROR;
  outgoing = writer->borrow_loaned_message();
  *msg = outgoing.get();
  return RMW_RET_OK;
}
extern "C" rmw_ret_t rmw_return_loaned_message_from_publisher(
  const rmw_publisher_t * pub, void * msg)
{
  if (!selected(pub->topic_name))
    return next<decltype(&rmw_return_loaned_message_from_publisher)>(__func__)(pub, msg);
  if (!outgoing || msg != outgoing.get()) return RMW_RET_ERROR;
  outgoing.reset();
  return RMW_RET_OK;
}
extern "C" rmw_ret_t rmw_publish_loaned_message(
  const rmw_publisher_t * pub, void * msg, rmw_publisher_allocation_t * allocation)
{
  if (!selected(pub->topic_name))
    return next<decltype(&rmw_publish_loaned_message)>(__func__)(pub, msg, allocation);
  if (!writer || !outgoing || msg != outgoing.get()) return RMW_RET_ERROR;
  writer->publish(std::move(outgoing));
  Message wake;
  return rmw_publish(pub, &wake, allocation);
}
extern "C" rmw_ret_t rmw_take_loaned_message_with_info(
  const rmw_subscription_t * sub, void ** msg, bool * taken, rmw_message_info_t * info,
  rmw_subscription_allocation_t * allocation)
{
  if (!selected(sub->topic_name))
    return next<decltype(&rmw_take_loaned_message_with_info)>(__func__)(
      sub, msg, taken, info, allocation);
  Message wake;
  auto ret = rmw_take_with_info(sub, &wake, taken, info, allocation);
  if (ret != RMW_RET_OK || !*taken) return ret;
  if (!reader || !wake.points.empty()) return RMW_RET_ERROR;
  std::lock_guard lock(incoming_mutex);
  auto loan = reader->take();
  if (!loan) return RMW_RET_ERROR;
  *msg = const_cast<Message *>(loan.get());
  if (!incoming.emplace(*msg, std::move(loan)).second) return RMW_RET_ERROR;
  return RMW_RET_OK;
}
extern "C" rmw_ret_t rmw_return_loaned_message_from_subscription(
  const rmw_subscription_t * sub, void * msg)
{
  if (!selected(sub->topic_name))
    return next<decltype(&rmw_return_loaned_message_from_subscription)>(__func__)(sub, msg);
  std::lock_guard lock(incoming_mutex);
  if (!incoming.erase(msg)) return RMW_RET_ERROR;
  ++return_count;
  return RMW_RET_OK;
}
