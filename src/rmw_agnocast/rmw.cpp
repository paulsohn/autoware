#include <agnocast/node/agnocast_node.hpp>
#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>

#include <autoware_planning_msgs/msg/trajectory.hpp>

#include <dlfcn.h>
#include <rmw/error_handling.h>
#include <rmw/rmw.h>

#include <deque>
#include <map>
#include <thread>

namespace
{
using Message = autoware_planning_msgs::msg::Trajectory;
using Loan = agnocast::ipc_shared_ptr<const Message>;
template <class F>
F cyclone(const char * symbol)
{
  static void * lib = dlopen("librmw_cyclonedds_cpp.so", RTLD_LOCAL | RTLD_NOW);
  auto fn = reinterpret_cast<F>(dlsym(lib, symbol));
  if (!lib || !fn) std::terminate();
  return fn;
}
#define CYCLONE(name) cyclone<decltype(&name)>(#name)
bool selected(const rosidl_message_type_support_t * ts, const char * topic)
{
  const char * enabled = std::getenv("AGNOCAST_RMW_TOPIC");
  return enabled && std::strcmp(enabled, topic) == 0 &&
         ts == rosidl_typesupport_cpp::get_message_type_support_handle<Message>();
}
rmw_ret_t error(const char * message)
{
  RMW_SET_ERROR_MSG(message);
  return RMW_RET_ERROR;
}
auto native_node(const rmw_node_t * owner)
{
  // Supplies Agnocast node interfaces only. ROS graph entities are owned by RCL.
  static std::once_flag once;
  std::call_once(once, [] { agnocast::init(0, nullptr); });
  rclcpp::NodeOptions opts;
  opts.use_global_arguments(false).start_parameter_services(false).use_clock_thread(false);
  return std::make_shared<agnocast::Node>(owner->name, owner->namespace_, opts);
}
struct Writer
{
  std::shared_ptr<agnocast::Node> node;
  agnocast::Publisher<Message> pub;
  std::mutex mutex;
  std::map<void *, agnocast::ipc_shared_ptr<Message>> borrowed;
  Writer(const rmw_node_t * owner, const char * topic, const rmw_qos_profile_t & qos)
  : node(native_node(owner)),
    pub(
      node.get(), topic, rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(qos), qos), {},
      agnocast::PublisherRole::AgnocastOnly)
  {
  }
};
struct Reader
{
  std::shared_ptr<agnocast::Node> node;
  using Guard = std::unique_ptr<rmw_guard_condition_t, decltype(&rmw_destroy_guard_condition)>;
  Guard guard;
  std::mutex mutex;
  std::deque<Loan> pending;
  std::map<void *, Loan> held;
  agnocast::Subscription<Message>::SharedPtr sub;
  agnocast::AgnocastOnlySingleThreadedExecutor exec;
  std::thread thread;
  Reader(const rmw_node_t * owner, const char * topic, const rmw_qos_profile_t & qos)
  : node(native_node(owner)),
    guard(CYCLONE(rmw_create_guard_condition)(owner->context), CYCLONE(rmw_destroy_guard_condition))
  {
    if (!guard) throw std::runtime_error("native wait guard creation failed");
    sub = std::make_shared<agnocast::Subscription<Message>>(
      node.get(), topic, rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(qos), qos),
      [this, depth = qos.depth](Loan sample) {
        std::lock_guard lock(mutex);
        if (pending.size() == depth) pending.pop_front();
        pending.emplace_back(std::move(sample));
        if (CYCLONE(rmw_trigger_guard_condition)(guard.get()) != RMW_RET_OK) std::terminate();
      },
      agnocast::SubscriptionOptions{}, agnocast::SubscriptionRole::AgnocastOnly);
    exec.add_node(node);
    thread = std::thread([this] { exec.spin(); });
  }
  ~Reader()
  {
    exec.cancel();
    if (thread.joinable()) thread.join();
    // Release kernel references before unregistering the native subscriber.
    held.clear();
    pending.clear();
  }
  bool ready()
  {
    std::lock_guard lock(mutex);
    return !pending.empty();
  }
};
std::mutex registry;
std::map<const void *, std::shared_ptr<Reader>> readers;
std::map<const void *, std::shared_ptr<Writer>> writers;
template <class T>
auto lookup(const T & map, const void * key) -> typename T::mapped_type
{
  std::lock_guard lock(registry);
  auto it = map.find(key);
  return it == map.end() ? nullptr : it->second;
}
bool supported(const rmw_qos_profile_t * qos)
{
  return qos->history == RMW_QOS_POLICY_HISTORY_KEEP_LAST && qos->depth > 0 &&
         qos->durability == RMW_QOS_POLICY_DURABILITY_VOLATILE;
}
}  // namespace
extern "C" const char * rmw_get_implementation_identifier()
{
  return "rmw_agnocast";
}
extern "C" rmw_publisher_t * rmw_create_publisher(
  const rmw_node_t * node, const rosidl_message_type_support_t * ts, const char * topic,
  const rmw_qos_profile_t * qos, const rmw_publisher_options_t * options)
{
  auto fn = CYCLONE(rmw_create_publisher);
  if (!selected(ts, topic)) return fn(node, ts, topic, qos, options);
  if (!supported(qos)) {
    error("native trajectory requires volatile KeepLast");
    return nullptr;
  }
  try {
    auto state = std::make_shared<Writer>(node, topic, *qos);
    auto pub = fn(node, ts, topic, qos, options);
    if (!pub) return nullptr;
    pub->can_loan_messages = true;
    std::lock_guard lock(registry);
    writers.emplace(pub->data, std::move(state));
    return pub;
  } catch (const std::exception & e) {
    error(e.what());
    return nullptr;
  }
}
extern "C" rmw_subscription_t * rmw_create_subscription(
  const rmw_node_t * node, const rosidl_message_type_support_t * ts, const char * topic,
  const rmw_qos_profile_t * qos, const rmw_subscription_options_t * options)
{
  auto fn = CYCLONE(rmw_create_subscription);
  if (!selected(ts, topic)) return fn(node, ts, topic, qos, options);
  if (!supported(qos)) {
    error("native trajectory requires volatile KeepLast");
    return nullptr;
  }
  try {
    auto state = std::make_shared<Reader>(node, topic, *qos);
    auto sub = fn(node, ts, topic, qos, options);
    if (!sub) return nullptr;
    sub->can_loan_messages = true;
    std::lock_guard lock(registry);
    readers.emplace(sub->data, std::move(state));
    return sub;
  } catch (const std::exception & e) {
    error(e.what());
    return nullptr;
  }
}
extern "C" rmw_ret_t rmw_destroy_subscription(rmw_node_t * node, rmw_subscription_t * sub)
{
  {
    std::lock_guard lock(registry);
    readers.erase(sub->data);
  }
  return CYCLONE(rmw_destroy_subscription)(node, sub);
}
extern "C" rmw_ret_t rmw_destroy_publisher(rmw_node_t * node, rmw_publisher_t * pub)
{
  {
    std::lock_guard lock(registry);
    writers.erase(pub->data);
  }
  return CYCLONE(rmw_destroy_publisher)(node, pub);
}
extern "C" rmw_ret_t rmw_borrow_loaned_message(
  const rmw_publisher_t * pub, const rosidl_message_type_support_t * ts, void ** msg)
{
  auto w = lookup(writers, pub->data);
  if (!w) return CYCLONE(rmw_borrow_loaned_message)(pub, ts, msg);
  std::lock_guard lock(w->mutex);
  auto loan = w->pub.borrow_loaned_message();
  *msg = loan.get();
  w->borrowed.emplace(*msg, std::move(loan));
  return RMW_RET_OK;
}
extern "C" rmw_ret_t rmw_publish_loaned_message(
  const rmw_publisher_t * pub, void * msg, rmw_publisher_allocation_t * a)
{
  auto w = lookup(writers, pub->data);
  if (!w) return CYCLONE(rmw_publish_loaned_message)(pub, msg, a);
  std::lock_guard lock(w->mutex);
  auto it = w->borrowed.find(msg);
  if (it == w->borrowed.end()) return error("unknown publisher loan");
  w->pub.publish(std::move(it->second));
  w->borrowed.erase(it);
  return RMW_RET_OK;
}
extern "C" rmw_ret_t rmw_return_loaned_message_from_publisher(
  const rmw_publisher_t * pub, void * msg)
{
  auto w = lookup(writers, pub->data);
  if (!w) return CYCLONE(rmw_return_loaned_message_from_publisher)(pub, msg);
  std::lock_guard lock(w->mutex);
  return w->borrowed.erase(msg) ? RMW_RET_OK : error("unknown publisher loan");
}
extern "C" rmw_ret_t rmw_publish(
  const rmw_publisher_t * pub, const void * msg, rmw_publisher_allocation_t * a)
{
  auto w = lookup(writers, pub->data);
  if (!w) return CYCLONE(rmw_publish)(pub, msg, a);
  auto loan = w->pub.borrow_loaned_message();
  *loan = *static_cast<const Message *>(msg);
  w->pub.publish(std::move(loan));
  return RMW_RET_OK;
}
namespace
{
rmw_ret_t take(Reader & r, void * copy, void ** loan, bool * taken, rmw_message_info_t * info)
{
  std::lock_guard lock(r.mutex);
  *taken = !r.pending.empty();
  if (!*taken) return RMW_RET_OK;
  if (info) *info = {};
  auto & sample = r.pending.front();
  if (loan) {
    *loan = const_cast<Message *>(sample.get());
    r.held.emplace(*loan, std::move(sample));
  } else
    *static_cast<Message *>(copy) = *sample;
  r.pending.pop_front();
  return RMW_RET_OK;
}
}  // namespace
extern "C" rmw_ret_t rmw_take_loaned_message_with_info(
  const rmw_subscription_t * sub, void ** msg, bool * taken, rmw_message_info_t * info,
  rmw_subscription_allocation_t * a)
{
  auto r = lookup(readers, sub->data);
  return r ? take(*r, nullptr, msg, taken, info)
           : CYCLONE(rmw_take_loaned_message_with_info)(sub, msg, taken, info, a);
}
extern "C" rmw_ret_t rmw_take_loaned_message(
  const rmw_subscription_t * sub, void ** msg, bool * taken, rmw_subscription_allocation_t * a)
{
  auto r = lookup(readers, sub->data);
  return r ? take(*r, nullptr, msg, taken, nullptr)
           : CYCLONE(rmw_take_loaned_message)(sub, msg, taken, a);
}
extern "C" rmw_ret_t rmw_take_with_info(
  const rmw_subscription_t * sub, void * msg, bool * taken, rmw_message_info_t * info,
  rmw_subscription_allocation_t * a)
{
  auto r = lookup(readers, sub->data);
  return r ? take(*r, msg, nullptr, taken, info)
           : CYCLONE(rmw_take_with_info)(sub, msg, taken, info, a);
}
extern "C" rmw_ret_t rmw_take(
  const rmw_subscription_t * sub, void * msg, bool * taken, rmw_subscription_allocation_t * a)
{
  auto r = lookup(readers, sub->data);
  return r ? take(*r, msg, nullptr, taken, nullptr) : CYCLONE(rmw_take)(sub, msg, taken, a);
}
extern "C" rmw_ret_t rmw_return_loaned_message_from_subscription(
  const rmw_subscription_t * sub, void * msg)
{
  auto r = lookup(readers, sub->data);
  if (!r) return CYCLONE(rmw_return_loaned_message_from_subscription)(sub, msg);
  std::lock_guard lock(r->mutex);
  return r->held.erase(msg) ? RMW_RET_OK : error("unknown subscriber loan");
}
extern "C" rmw_ret_t rmw_wait(
  rmw_subscriptions_t * subs, rmw_guard_conditions_t * guards, rmw_services_t * services,
  rmw_clients_t * clients, rmw_events_t * events, rmw_wait_set_t * ws, const rmw_time_t * timeout)
{
  std::vector<std::pair<size_t, std::shared_ptr<Reader>>> native;
  if (subs)
    for (size_t i = 0; i < subs->subscriber_count; ++i) {
      auto r = lookup(readers, subs->subscribers[i]);
      if (r) native.emplace_back(i, std::move(r));
    }
  if (native.empty())
    return CYCLONE(rmw_wait)(subs, guards, services, clients, events, ws, timeout);
  std::vector<void *> guard_data;
  if (guards && guards->guard_condition_count)
    guard_data.assign(
      guards->guard_conditions, guards->guard_conditions + guards->guard_condition_count);
  bool queued = false;
  for (auto & [idx, r] : native) {
    guard_data.push_back(r->guard->data);
    queued |= r->ready();
  }
  rmw_guard_conditions_t combined{guard_data.size(), guard_data.data()};
  rmw_time_t zero{};
  // Native subscriptions have Cyclone graph handles. Preserve the pointer
  // on readiness even though the delegated wait clears empty DDS readers.
  std::vector<void *> originals;
  for (auto & [idx, r] : native) originals.push_back(subs->subscribers[idx]);
  auto ret =
    CYCLONE(rmw_wait)(subs, &combined, services, clients, events, ws, queued ? &zero : timeout);
  if (ret != RMW_RET_OK && ret != RMW_RET_TIMEOUT) return ret;
  if (guards && guards->guard_condition_count)
    std::copy_n(guard_data.begin(), guards->guard_condition_count, guards->guard_conditions);
  for (size_t i = 0; i < native.size(); ++i) {
    auto & [idx, r] = native[i];
    auto ready = r->ready();
    subs->subscribers[idx] = ready ? originals[i] : nullptr;
    if (ready) ret = RMW_RET_OK;
  }
  return ret;
}
