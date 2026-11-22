#include "host_cloud_view.hpp"
#include "host_cloud.hpp"
#include <agnocast/agnocast_executor.hpp>
#include <agnocast/node/agnocast_node.hpp>
#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>

#include <dlfcn.h>
#include <poll.h>
#include <rmw/error_handling.h>
#include <rmw/rmw.h>
#include <sys/eventfd.h>

#include <condition_variable>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <map>
#include <string_view>
#include <thread>

namespace
{
// Each role traces a single endpoint on its executor thread in the gap fixture.
struct HopTrace {
  const char * path;
  struct Event { const char * name; int64_t stamp, time; };
  std::vector<Event> events;
  explicit HopTrace(const char * env) : path(std::getenv(env)) {
    if (path) events.reserve(512);
  }
  void mark(const char * name, int64_t stamp = 0) {
    if (path && events.size() < events.capacity()) {
      const auto now = std::chrono::system_clock::now().time_since_epoch();
      events.push_back({name, stamp, std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()});
    }
  }
  ~HopTrace() {
    if (!path) return;
    std::ofstream out(path);
    for (const auto & e : events) out << e.name << ',' << e.stamp << ',' << e.time << '\n';
  }
};
HopTrace & hop_reader() { static thread_local HopTrace trace("BENCH_HOP_READER"); return trace; }
int64_t cloud_stamp(const Cloud::_header_type & h) {
  return static_cast<int64_t>(h.stamp.sec) * 1000000000 + h.stamp.nanosec;
}
using Members = rosidl_typesupport_introspection_cpp::MessageMembers;
using Loan = agnocast::ipc_shared_ptr<const void>;
template <class F>
F cyclone(const char * symbol)
{
  static void * lib = dlopen("librmw_cyclonedds_cpp.so", RTLD_LOCAL | RTLD_NOW);
  auto fn = reinterpret_cast<F>(dlsym(lib, symbol));
  if (!lib || !fn) std::terminate();
  return fn;
}
#define CYCLONE(name) cyclone<decltype(&name)>(#name)
// Separate diagnostic run: time only the Cyclone codec calls on Buffer topics.
template <class F>
rmw_ret_t measure_codec(const char * operation, F && call)
{
  static const bool enabled = std::getenv("AGNOCAST_RMW_TRACE_CODEC") != nullptr;
  if (!enabled) return call();
  auto start = std::chrono::steady_clock::now();
  auto ret = call();
  auto elapsed =
    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
      .count();
  const char * role = std::getenv("BENCH_ROLE");
  std::fprintf(
    stderr, "codec_ns %s %s %lld\n", role ? role : "unknown", operation,
    static_cast<long long>(elapsed));
  return ret;
}
bool listed(std::string_view names, char delimiter, std::string_view value)
{
  while (!names.empty()) {
    auto sep = names.find(delimiter);
    if (names.substr(0, sep) == value) return true;
    if (sep == names.npos) break;
    names.remove_prefix(sep + 1);
  }
  return false;
}
bool selected(const char * topic, const char * direction)
{
  const char * enabled = std::getenv(direction);
  if (!enabled) enabled = std::getenv("AGNOCAST_RMW_TOPIC");
  return enabled && topic && listed(enabled, ':', topic);
}
bool contains_buffer(const Members & members)
{
  for (size_t i = 0; i < members.member_count_; ++i) {
    const auto & field = members.members_[i];
    if (field.is_rosidl_buffer_) return true;
    if (
      field.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE &&
      contains_buffer(*static_cast<const Members *>(field.members_->data)))
      return true;
  }
  return false;
}
struct Type
{
  const rosidl_message_type_support_t * ts;
  const Members * members;
  bool has_buffer;
  bool host_cloud = false;
  bool legacy = [] {
    const char * flag = std::getenv("AGNOCAST_RMW_LEGACY_WIRE");
    return flag && std::strcmp(flag, "1") == 0;
  }();
  std::string name;
  std::string route(const char * topic, bool reliable) const
  {
    if (legacy) return native_topic(topic);
    const char * layout = host_cloud ? "host_cloud" : has_buffer ? "cdr" : "object";
    return std::string(topic) + "/_rmw_agnocast_v2/" + name + "/" + layout +
           (reliable ? "/reliable" : "/best_effort");
  }
  size_t wire_size(size_t payload) const
  {
    if (legacy) return payload;
    if (payload > SIZE_MAX - RMW_GID_STORAGE_SIZE) throw std::bad_alloc();
    return payload + RMW_GID_STORAGE_SIZE;
  }
  size_t payload_size(const void * sample) const
  {
    if (host_cloud) return sizeof(HostCloud);
    if (has_buffer) return sizeof(size_t) + *static_cast<const size_t *>(sample);
    return members->size_of_;
  }
  std::string native_topic(const char * topic) const
  {
    if (host_cloud) return std::string(topic) + "/_rmw_agnocast_host_cloud_v1";
    if (!has_buffer) return topic;
    return std::string(topic) + "/_rmw_agnocast_cdr/" + name;
  }
  explicit Type(const rosidl_message_type_support_t * handle) : ts(handle)
  {
    auto intro = get_message_typesupport_handle(handle, "rosidl_typesupport_introspection_cpp");
    if (!intro) throw std::runtime_error("native topics require C++ introspection");
    members = static_cast<const Members *>(intro->data);
    has_buffer = contains_buffer(*members);
    name = members->message_namespace_;
    for (size_t pos; (pos = name.find("::")) != std::string::npos;) name.replace(pos, 2, "/");
    name += "/" + std::string(members->message_name_);
    host_cloud =
      has_buffer && name == "sensor_msgs/msg/PointCloud2" && std::getenv("AGNOCAST_RMW_HOST_CLOUD");
    if (has_buffer && !host_cloud && !std::getenv("AGNOCAST_RMW_BUFFER_CDR"))
      throw std::runtime_error("native object loans cannot expose a foreign Buffer implementation");
  }
  void destroy(void * p) const
  {
    members->fini_function(p);
    ::operator delete(p);
  }
  rmw_ret_t copy(const void * from, void * to) const
  {
    rclcpp::SerializedMessage bytes;
    auto & raw = bytes.get_rcl_serialized_message();
    auto ret = CYCLONE(rmw_serialize)(from, ts, &raw);
    return ret == RMW_RET_OK ? CYCLONE(rmw_deserialize)(&raw, ts, to) : ret;
  }
};
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
  Type type;
  agnocast::TypeErasedPublisher pub;
  std::mutex mutex;
  std::map<void *, agnocast::ipc_shared_ptr<void>> borrowed;
  rmw_gid_t gid{};
  void stamp(void * payload) const
  {
    if (!type.legacy) std::memcpy(static_cast<uint8_t *>(payload) + type.payload_size(payload),
                                  gid.data, sizeof(gid.data));
  }
  Writer(
    const rmw_node_t * owner, const char * topic, const rmw_qos_profile_t & qos,
    const rosidl_message_type_support_t * ts)
  : node(native_node(owner)),
    type(ts),
    pub(
      node.get(), type.route(topic, qos.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE), type.name,
      rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(qos), qos), {},
      agnocast::PublisherRole::AgnocastOnly)
  {
  }
  const bool host_loan = type.host_cloud && std::getenv("AGNOCAST_RMW_HOST_LOAN");
  size_t published_loans = 0, private_copies = 0;
  size_t backend_materializations = 0, extra_host_copies = 0;
  uintptr_t pool_begin = 0, pool_end = 0;
  // Linux maps this writer's native loan and its shared vector storage in one VMA.
  bool owns_payload(const std::vector<uint8_t> & bytes, const void * loan)
  {
    if (!pool_end) {
      std::ifstream maps("/proc/self/maps");
      for (std::string line; std::getline(maps, line); ) {
        unsigned long begin, end;
        char mode[5];
        if (std::sscanf(line.c_str(), "%lx-%lx %4s", &begin, &end, mode) == 3 &&
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
  // Plugins may return a vector allocated before the shared allocation scope.
  // A reader in another process must only see bytes owned by this writer's pool.
  void ensure_shared_bytes(HostCloud & wire, const void * loan)
  {
    if (!owns_payload(wire.bytes, loan)) {
      std::vector<uint8_t> shared(wire.bytes);
      if (!owns_payload(shared, loan)) throw std::runtime_error("fallback payload outside writer pool");
      wire.bytes.swap(shared);
      ++extra_host_copies;
    }
    ++backend_materializations;
  }
  ~Writer()
  {
    for (auto & entry : borrowed) cancel(std::move(entry.second));
    if (backend_materializations) std::fprintf(stderr, "host-cloud materializations=%zu extra_host_copies=%zu\n",
      backend_materializations, extra_host_copies);
    if (host_loan) std::fprintf(stderr, "host-cloud publisher loans=%zu private_copies=%zu\n", published_loans, private_copies);
  }
  auto borrow()
  {
    auto size = host_loan ? std::max(sizeof(HostCloud), sizeof(Cloud)) : type.members->size_of_;
    auto loan = pub.borrow_loaned_message(type.wire_size(size));
    try {
      type.members->init_function(loan.get(), rosidl_runtime_cpp::MessageInitialization::ALL);
    } catch (...) {
      pub.cancel_message(std::move(loan), [](void * p) { ::operator delete(p); });
      throw;
    }
    return loan;
  }
  void publish(agnocast::ipc_shared_ptr<void> && loan)
  {
    stamp(loan.get());
    pub.publish(std::move(loan), [this](void * p) { type.destroy(p); });
  }
  void cancel(agnocast::ipc_shared_ptr<void> && loan)
  {
    pub.cancel_message(std::move(loan), [this](void * p) { type.destroy(p); });
  }
};
// Bounded, single-reader diagnostic. All row access is under the reader mutex;
// the benchmark supplies one private output file per reader process.
struct HandoffTrace
{
  static int64_t now()
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
  }
  struct DirectTimes
  {
    int64_t wake{}, interrupted{}, drained{}, helper_return{}, synchronized{};
  };
  struct Row
  {
    int64_t id;
    uintptr_t address;
    int64_t native, signal, signalled, wait_start, dds_return, ready, take_enter, taken;
    bool blocking;
    DirectTimes direct{};
  };
  std::vector<Row> rows;
  std::string path;
  explicit HandoffTrace(const char * file) : path(file) { rows.reserve(1024); }
  Row * find(const Loan & loan)
  {
    auto id = loan.get_entry_id();
    auto it = std::find_if(rows.rbegin(), rows.rend(), [&](const Row & row) {
      return row.id == id && row.address == reinterpret_cast<uintptr_t>(loan.get());
    });
    return it == rows.rend() ? nullptr : &*it;
  }
  ~HandoffTrace()
  {
    std::ofstream out(path);
    for (const auto & r : rows) {
      out << r.id << ' ' << r.address << ' ' << r.native << ' ' << r.signal << ' ' << r.signalled
          << ' ' << r.wait_start << ' ' << r.dds_return << ' ' << r.ready << ' ' << r.take_enter
          << ' ' << r.taken << ' ' << r.blocking;
      if (r.direct.wake)
        out << ' ' << r.direct.wake << ' ' << r.direct.interrupted << ' ' << r.direct.drained
            << ' ' << r.direct.helper_return << ' ' << r.direct.synchronized;
      out << '\n';
    }
  }
};
bool direct_wait_enabled()
{
  static const bool enabled = [] {
    const char * mode = std::getenv("AGNOCAST_RMW_DIRECT_WAIT");
    return mode && std::strcmp(mode, "1") == 0;
  }();
  return enabled;
}
bool ready_fd_enabled()
{
  static const bool enabled = [] {
    const char * flag = std::getenv("AGNOCAST_RMW_READY_FD");
    return flag && std::strcmp(flag, "1") == 0;
  }();
  return enabled;
}
class NativeSubscription : public agnocast::Subscription<void>
{
public:
  using agnocast::Subscription<void>::Subscription;
  int notification_fd() const { return notify_eventfd_; }
};
class NativeExecutor : public agnocast::AgnocastOnlySingleThreadedExecutor
{
public:
  void prepare()
  {
    add_callback_groups_from_nodes_associated_to_executor();
    epoll_manager_->prepare_epoll(
      [this](const auto & group) { return is_callback_group_associated(group); });
  }
  void receive_known(uint32_t callback_id)
  {
    agnocast::SubscriptionEventHandler handler(my_pid_, &ready_agnocast_executables_mutex_, &ready_agnocast_executables_);
    handler.handle(callback_id);
    agnocast::AgnocastExecutable executable;
    while (get_next_ready_agnocast_executable(executable)) execute_agnocast_executable(executable);
  }
  void receive()
  {
    agnocast::AgnocastExecutable executable;
    // Consume one notification, including continuation batches for coalesced writes.
    if (get_next_agnocast_executable(executable, 0)) {
      do {
        execute_agnocast_executable(executable);
      } while (get_next_ready_agnocast_executable(executable));
    }
  }
};
struct Reader : std::enable_shared_from_this<Reader>
{
  std::shared_ptr<agnocast::Node> node;
  using Guard = std::unique_ptr<rmw_guard_condition_t, decltype(&rmw_destroy_guard_condition)>;
  Type type;
  bool host_view = false;
  Guard guard;
  std::mutex mutex;
  std::unique_ptr<HandoffTrace> trace;
  std::deque<Loan> pending;
  std::map<void *, Loan> held;
  std::vector<std::shared_ptr<NativeSubscription>> routes;
  std::mutex receive_mutex;
  std::vector<uint32_t> route_ids;
  size_t known_dispatches = 0;
  NativeExecutor exec;
  std::thread thread;
  Reader(
    const rmw_node_t * owner, const char * topic, const rmw_qos_profile_t & qos,
    const rosidl_message_type_support_t * ts)
  : node(native_node(owner)),
    type(ts),
    guard(
      direct_wait_enabled() ? nullptr : CYCLONE(rmw_create_guard_condition)(owner->context),
      CYCLONE(rmw_destroy_guard_condition))
  {
    if (!direct_wait_enabled()) {
      if (!guard) throw std::runtime_error("native wait guard creation failed");
      if (const char * file = std::getenv("AGNOCAST_RMW_HANDOFF_FILE"))
        trace = std::make_unique<HandoffTrace>(file);
    }
    if (direct_wait_enabled()) {
      if (const char * file = std::getenv("AGNOCAST_RMW_DIRECT_TRACE"))
        trace = std::make_unique<HandoffTrace>(file);
    }
    auto subscribe = [&](bool reliable) {
      routes.push_back(std::make_shared<NativeSubscription>(
        node.get(), type.route(topic, reliable), type.name,
        rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(qos), qos),
        [this, depth = qos.depth](Loan sample) {
          if (hop_reader().path && type.host_cloud)
            hop_reader().mark("admit", cloud_stamp(static_cast<const HostCloud *>(sample.get())->header));
          auto entry = trace ? HandoffTrace::now() : 0;
          std::lock_guard lock(mutex);
          if (pending.size() == depth) {
            if (trace && type.host_cloud) {
              const auto & oldest = pending.front();
              std::fprintf(stderr, "queue_evict id=%ld stamp=%ld depth=%zu\n",
                static_cast<long>(oldest.get_entry_id()),
                static_cast<long>(cloud_stamp(static_cast<const HostCloud *>(oldest.get())->header)), pending.size());
            }
            pending.pop_front();
          }
          pending.emplace_back(std::move(sample));
          HandoffTrace::Row * row = nullptr;
          if (trace && trace->rows.size() < trace->rows.capacity()) {
            const auto & loan = pending.back();
            row = &trace->rows.emplace_back(
              HandoffTrace::Row{loan.get_entry_id(), reinterpret_cast<uintptr_t>(loan.get()), entry});
          }
          if (direct_wait_enabled()) return;
          if (row) row->signal = HandoffTrace::now();
          if (CYCLONE(rmw_trigger_guard_condition)(guard.get()) != RMW_RET_OK) std::terminate();
          if (row) row->signalled = HandoffTrace::now();
        },
        agnocast::SubscriptionOptions{}, agnocast::SubscriptionRole::AgnocastOnly));
    };
    subscribe(true);
    if (!type.legacy && qos.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT) subscribe(false);
    if (direct_wait_enabled() && ready_fd_enabled()) {
      std::lock_guard lock(agnocast::id2_callback_info_mtx);
      for (const auto & route : routes) {
        auto id = std::find_if(agnocast::id2_callback_info.begin(), agnocast::id2_callback_info.end(),
          [&](const auto & item) { return item.second.notify_eventfd == route->notification_fd(); });
        if (id == agnocast::id2_callback_info.end()) throw std::runtime_error("missing Agnocast route ID");
        route_ids.push_back(id->first);
      }
    }
    exec.add_node(node);
    if (direct_wait_enabled())
      exec.prepare();
    else
      thread = std::thread([this] { exec.spin(); });
  }
  ~Reader()
  {
    if (known_dispatches) std::fprintf(stderr, "ready_fd dispatches=%zu\n", known_dispatches);
    exec.cancel();
    if (thread.joinable()) thread.join();
    // Release kernel references before unregistering the native subscriber.
    held.clear();
    pending.clear();
  }
  void receive_polled(const pollfd * descriptors)
  {
    if (route_ids.empty()) return receive_direct();
    std::lock_guard lock(receive_mutex);
    for (size_t i = 0; i < route_ids.size(); ++i) {
      if (descriptors[i].revents & POLLIN) {
        exec.receive_known(route_ids[i]);
        ++known_dispatches;
      }
    }
  }
  void receive_direct()
  {
    std::lock_guard lock(receive_mutex);
    exec.receive();
  }
  bool ready(int64_t start = 0, int64_t returned = 0, bool blocking = false,
             const HandoffTrace::DirectTimes * direct = nullptr)
  {
    std::lock_guard lock(mutex);
    if ((start || direct) && !pending.empty()) {
      if (auto row = trace->find(pending.front())) {
        if (direct) row->direct = *direct;
        row->wait_start = start;
        row->dds_return = returned;
        row->blocking = blocking;
        row->ready = HandoffTrace::now();
      }
    }
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
  if (!selected(topic, "AGNOCAST_RMW_PUBLISH_TOPICS")) return fn(node, ts, topic, qos, options);
  if (!supported(qos)) {
    error("native topic requires volatile KeepLast");
    return nullptr;
  }
  try {
    auto pub = fn(node, ts, topic, qos, options);
    if (!pub) return nullptr;
    auto cleanup = [node](rmw_publisher_t * p) { CYCLONE(rmw_destroy_publisher)(const_cast<rmw_node_t *>(node), p); };
    std::unique_ptr<rmw_publisher_t, decltype(cleanup)> handle(pub, cleanup);
    rmw_qos_profile_t actual{};
    if (CYCLONE(rmw_publisher_get_actual_qos)(pub, &actual) != RMW_RET_OK) return nullptr;
    auto native_qos = *qos;
    native_qos.reliability = actual.reliability;
    auto state = std::make_shared<Writer>(node, topic, native_qos, ts);
    if (CYCLONE(rmw_get_gid_for_publisher)(pub, &state->gid) != RMW_RET_OK) return nullptr;
    pub->can_loan_messages = !state->type.has_buffer || state->host_loan;
    if (state->type.has_buffer)
      std::fprintf(
        stderr, "rmw_agnocast %s writer %s\n", state->type.host_cloud ? "host-cloud" : "CDR",
        topic);
    std::lock_guard lock(registry);
    writers.emplace(pub->data, std::move(state));
    return handle.release();
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
  if (!selected(topic, "AGNOCAST_RMW_SUBSCRIBE_TOPICS")) return fn(node, ts, topic, qos, options);
  if (!supported(qos)) {
    error("native topic requires volatile KeepLast");
    return nullptr;
  }
  try {
    auto sub = fn(node, ts, topic, qos, options);
    if (!sub) return nullptr;
    auto cleanup = [node](rmw_subscription_t * p) { CYCLONE(rmw_destroy_subscription)(const_cast<rmw_node_t *>(node), p); };
    std::unique_ptr<rmw_subscription_t, decltype(cleanup)> handle(sub, cleanup);
    rmw_qos_profile_t actual{};
    if (CYCLONE(rmw_subscription_get_actual_qos)(sub, &actual) != RMW_RET_OK) return nullptr;
    auto native_qos = *qos;
    native_qos.reliability = actual.reliability;
    auto state = std::make_shared<Reader>(node, topic, native_qos, ts);
    const std::string_view backends = options && options->acceptable_buffer_backends
      ? options->acceptable_buffer_backends : "";
    state->host_view = std::getenv("AGNOCAST_RMW_HOST_VIEW") &&
      (backends == "any" || listed(backends, ',', "agnocast_host_view"));
    sub->can_loan_messages = !state->type.has_buffer;
    if (state->type.has_buffer)
      std::fprintf(
        stderr, "rmw_agnocast %s reader %s\n", state->type.host_cloud ? "host-cloud" : "CDR",
        topic);
    std::lock_guard lock(registry);
    readers.emplace(sub->data, std::move(state));
    return handle.release();
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
  if (w->type.has_buffer && !w->host_loan) return RMW_RET_UNSUPPORTED;
  try {
    auto loan = w->borrow();
    try {
      auto [entry, inserted] = w->borrowed.try_emplace(loan.get(), std::move(loan));
      if (!inserted) throw std::runtime_error("duplicate publisher loan");
      *msg = entry->first;
    } catch (...) {
      w->cancel(std::move(loan));
      throw;
    }
  } catch (const std::exception & e) {
    return error(e.what());
  }
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
  if (w->host_loan) {
    auto * cloud = static_cast<Cloud *>(msg);
    static thread_local HopTrace trace("BENCH_HOP_WRITER");
    const auto stamp = trace.path ? cloud_stamp(cloud->header) : 0;
    trace.mark("publish_entry", stamp);
    const bool cpu = cloud->data.get_backend_type() == "cpu";
    const bool shared = cpu && w->owns_payload(cloud->data, msg);
    try {
      HostCloud wire = shared ? HostCloud(std::move(*cloud)) : HostCloud(*cloud);
      if (!cpu) w->ensure_shared_bytes(wire, msg);
      std::destroy_at(cloud);
      static_assert(std::is_nothrow_move_constructible_v<HostCloud>);
      new (msg) HostCloud(std::move(wire));
    } catch (const std::exception & e) {
      w->cancel(std::move(it->second));
      w->borrowed.erase(it);
      return error(e.what());
    }
    if (cpu && !shared) ++w->private_copies;
    w->stamp(msg);
    trace.mark("handoff", stamp);
    w->pub.publish(std::move(it->second), [](void * p) {
      auto * cloud = static_cast<HostCloud *>(p);
      static const bool audit = std::getenv("AGNOCAST_RMW_AUDIT_RELEASE") != nullptr;
      const auto stamp = audit ? cloud_stamp(cloud->header) : 0;
      std::destroy_at(cloud);
      ::operator delete(p);
      if (audit) std::fprintf(stderr, "host_payload_reclaimed=%ld\n", long(stamp));
    });
    trace.mark("publish_return", stamp);
    ++w->published_loans;
  } else {
    w->publish(std::move(it->second));
  }
  w->borrowed.erase(it);
  return RMW_RET_OK;
}
extern "C" rmw_ret_t rmw_return_loaned_message_from_publisher(
  const rmw_publisher_t * pub, void * msg)
{
  auto w = lookup(writers, pub->data);
  if (!w) return CYCLONE(rmw_return_loaned_message_from_publisher)(pub, msg);
  std::lock_guard lock(w->mutex);
  auto it = w->borrowed.find(msg);
  if (it == w->borrowed.end()) return error("unknown publisher loan");
  w->cancel(std::move(it->second));
  w->borrowed.erase(it);
  return RMW_RET_OK;
}
extern "C" rmw_ret_t rmw_publish(
  const rmw_publisher_t * pub, const void * msg, rmw_publisher_allocation_t * a)
{
  auto w = lookup(writers, pub->data);
  if (!w) return CYCLONE(rmw_publish)(pub, msg, a);
  if (w->type.host_cloud) {
    try {
      const auto & cloud = *static_cast<const Cloud *>(msg);
      auto loan = w->pub.borrow_loaned_message(w->type.wire_size(sizeof(HostCloud)));
      try {
        HostCloud wire(cloud);
        if (cloud.data.get_backend_type() != "cpu") {
          std::lock_guard lock(w->mutex);
          w->ensure_shared_bytes(wire, loan.get());
        }
        new (loan.get()) HostCloud(std::move(wire));
      } catch (...) {
        w->pub.cancel_message(std::move(loan), [](void * p) { ::operator delete(p); });
        throw;
      }
      if (const char * path = std::getenv("AGNOCAST_VIEW_ADDRESS_FILE")) {
        std::ofstream(path) << reinterpret_cast<uintptr_t>(static_cast<HostCloud *>(loan.get())->bytes.data());
      }
      w->stamp(loan.get());
      w->pub.publish(std::move(loan), [](void * p) {
        static_cast<HostCloud *>(p)->~HostCloud();
        ::operator delete(p);
      });
      return RMW_RET_OK;
    } catch (const std::exception & e) {
      return error(e.what());
    }
  }
  if (w->type.has_buffer) {
    rclcpp::SerializedMessage bytes;
    auto & raw = bytes.get_rcl_serialized_message();
    auto ret =
      measure_codec("serialize", [&] { return CYCLONE(rmw_serialize)(msg, w->type.ts, &raw); });
    if (ret != RMW_RET_OK) return ret;
    auto loan = w->pub.borrow_loaned_message(w->type.wire_size(sizeof(size_t) + raw.buffer_length));
    auto length = new (loan.get()) size_t(raw.buffer_length);
    std::memcpy(length + 1, raw.buffer, *length);
    w->stamp(loan.get());
    w->pub.publish(std::move(loan), [](void * p) { ::operator delete(p); });
    return RMW_RET_OK;
  }
  auto loan = w->borrow();
  auto ret = w->type.copy(msg, loan.get());
  if (ret != RMW_RET_OK) {
    w->cancel(std::move(loan));
    return ret;
  }
  w->publish(std::move(loan));
  return RMW_RET_OK;
}
namespace
{
rmw_ret_t take(
  Reader & r, void * copy, void ** loan, bool * taken, rmw_message_info_t * info,
  rmw_serialized_message_t * serialized = nullptr)
{
  if (direct_wait_enabled() && !r.ready()) r.receive_direct();
  auto entry = r.trace ? HandoffTrace::now() : 0;
  std::lock_guard lock(r.mutex);
  *taken = !r.pending.empty();
  if (!*taken) return RMW_RET_OK;
  if (info) *info = {};
  auto & sample = r.pending.front();
  if (info && !r.type.legacy) {
    static const char * const implementation = CYCLONE(rmw_get_implementation_identifier)();
    info->publisher_gid.implementation_identifier = implementation;
    std::memcpy(info->publisher_gid.data,
      static_cast<const uint8_t *>(sample.get()) + r.type.payload_size(sample.get()),
      sizeof(info->publisher_gid.data));
  }
  if (r.type.has_buffer) {
    if (loan) {
      *taken = false;
      return RMW_RET_UNSUPPORTED;
    }
    if (r.type.host_cloud) {
      rmw_ret_t ret = RMW_RET_OK;
      try {
        const auto & cloud = *static_cast<const HostCloud *>(sample.get());
        hop_reader().mark("take", cloud_stamp(cloud.header));
        if (serialized) {
          Cloud local;
          cloud.restore_metadata(local);
          local.data = cloud.bytes;
          ret = CYCLONE(rmw_serialize)(&local, r.type.ts, serialized);
        } else {
          auto & msg = *static_cast<Cloud *>(copy);
          cloud.restore_metadata(msg);
          if (r.host_view) {
            // Member destruction order returns the loan before unregistering the reader.
            struct Held {
              std::shared_ptr<Reader> reader;
              Loan loan;
            };
            auto held = std::make_shared<Held>(Held{r.shared_from_this(), std::move(sample)});
            std::shared_ptr<const std::vector<uint8_t>> bytes(held, &cloud.bytes);
            msg.data = rosidl::Buffer<uint8_t>(std::make_unique<rmw_agnocast::HostCloudView>(std::move(bytes)));
          } else {
            msg.data = cloud.bytes;
          }
        }
      } catch (const std::exception & e) {
        ret = error(e.what());
      }
      if (ret == RMW_RET_OK && copy && !serialized) hop_reader().mark("restored", cloud_stamp(static_cast<Cloud *>(copy)->header));
      if (ret != RMW_RET_OK) *taken = false;
      auto row = r.trace && !r.host_view ? r.trace->find(sample) : nullptr;
      r.pending.pop_front();
      if (row) {
        row->take_enter = entry;
        row->taken = HandoffTrace::now();
      }
      return ret;
    }
    const auto length = static_cast<const size_t *>(sample.get());
    auto payload = reinterpret_cast<const uint8_t *>(length + 1);
    rmw_ret_t ret = RMW_RET_OK;
    if (serialized) {
      if (
        serialized->buffer_capacity < *length &&
        rmw_serialized_message_resize(serialized, *length) != RCUTILS_RET_OK)
        ret = error("unable to resize serialized sample");
      if (ret == RMW_RET_OK) {
        std::memcpy(serialized->buffer, payload, *length);
        serialized->buffer_length = *length;
      }
    } else {
      auto raw = rmw_get_zero_initialized_serialized_message();
      raw.buffer = const_cast<uint8_t *>(payload);
      raw.buffer_capacity = raw.buffer_length = *length;
      ret = measure_codec(
        "deserialize", [&] { return CYCLONE(rmw_deserialize)(&raw, r.type.ts, copy); });
    }
    if (ret != RMW_RET_OK) *taken = false;
    r.pending.pop_front();
    return ret;
  }
  auto row = r.trace ? r.trace->find(sample) : nullptr;
  if (loan) {
    *loan = const_cast<void *>(sample.get());
    r.held.emplace(*loan, std::move(sample));
  } else {
    auto ret = serialized ? CYCLONE(rmw_serialize)(sample.get(), r.type.ts, serialized)
                          : r.type.copy(sample.get(), copy);
    if (ret != RMW_RET_OK) {
      *taken = false;
      return ret;
    }
  }
  r.pending.pop_front();
  if (row) {
    row->take_enter = entry;
    row->taken = HandoffTrace::now();
  }
  return RMW_RET_OK;
}
}  // namespace
extern "C" rmw_ret_t rmw_take_serialized_message_with_info(
  const rmw_subscription_t * sub, rmw_serialized_message_t * msg, bool * taken,
  rmw_message_info_t * info, rmw_subscription_allocation_t * a)
{
  auto r = lookup(readers, sub->data);
  return r ? take(*r, nullptr, nullptr, taken, info, msg)
           : CYCLONE(rmw_take_serialized_message_with_info)(sub, msg, taken, info, a);
}
extern "C" rmw_ret_t rmw_take_serialized_message(
  const rmw_subscription_t * sub, rmw_serialized_message_t * msg, bool * taken,
  rmw_subscription_allocation_t * a)
{
  auto r = lookup(readers, sub->data);
  return r ? take(*r, nullptr, nullptr, taken, nullptr, msg)
           : CYCLONE(rmw_take_serialized_message)(sub, msg, taken, a);
}
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
  hop_reader().mark("rmw_entry");
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
namespace
{
// Cyclone's public rmw_wait has no pollable descriptor. Its helper operates
// only for the lifetime of each blocking wait; DDS entity pointers are never
// retained after rmw_wait returns. Native payloads stay on the caller thread.
thread_local bool fixed_probe = false;
class DirectWaitSet
{
  Reader::Guard interrupt;
  int done_fd;
  std::mutex mutex;
  std::condition_variable cv;
  std::function<void()> job;
  bool stopping = false;
  std::thread worker;

  rmw_context_t * context;
  rmw_wait_set_t * fixed_set = nullptr;
  using Lists = std::array<std::vector<void *>, 5>;
  Lists fixed_inputs, fixed_output;
  rmw_ret_t fixed_result{};
  bool armed = false;
  unsigned arms = 0, completions = 0, native_wakes = 0, both = 0, interrupts = 0, native_interrupts = 0;

  void serve()
  {
    std::unique_lock lock(mutex);
    for (;;) {
      cv.wait(lock, [this] { return stopping || bool(job); });
      if (stopping) return;
      auto call = std::move(job);
      job = {};
      lock.unlock();
      call();
      lock.lock();
      int status;
      do {
        status = eventfd_write(done_fd, 1);
      } while (status < 0 && errno == EINTR);
      if (status < 0) std::terminate();
    }
  }

public:
  explicit DirectWaitSet(rmw_context_t * context)
  : interrupt(CYCLONE(rmw_create_guard_condition)(context), CYCLONE(rmw_destroy_guard_condition)),
    done_fd(-1), context(context)
  {
    if (!interrupt) throw std::runtime_error("could not create DDS interrupt guard");
    done_fd = eventfd(0, EFD_CLOEXEC);
    if (done_fd < 0) throw std::runtime_error("could not create DDS notification fd");
  }
  void stop_fixed()
  {
    if (!fixed_set) return;
    {
      std::lock_guard lock(mutex);
      stopping = true;
    }
    ++interrupts;
    if (CYCLONE(rmw_trigger_guard_condition)(interrupt.get()) != RMW_RET_OK) std::terminate();
    cv.notify_one();
    if (worker.joinable()) worker.join();
    CYCLONE(rmw_destroy_wait_set)(fixed_set);
    fixed_set = nullptr;
    // A shutdown guard can leave a completion posted while the executor is idle.
    pollfd done{done_fd, POLLIN, 0};
    int rc;
    do {
      rc = poll(&done, 1, 0);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) std::terminate();
    if (rc > 0) {
      eventfd_t count;
      while (eventfd_read(done_fd, &count) < 0)
        if (errno != EINTR) std::terminate();
    }
    armed = stopping = false;
    job = {};
    fixed_inputs = {};
    fixed_output = {};
    std::fprintf(
      stderr,
      "fixed_wait arms=%u completions=%u native=%u both=%u native_interrupts=%u "
      "stop_interrupts=%u\n",
      arms, completions, native_wakes, both, native_interrupts, interrupts);
    arms = completions = native_wakes = both = native_interrupts = interrupts = 0;
  }
  ~DirectWaitSet()
  {
    stop_fixed();
    {
      std::lock_guard lock(mutex);
      stopping = true;
    }
    cv.notify_one();
    if (worker.joinable()) worker.join();
    close(done_fd);
  }
  rmw_ret_t wait(
    const std::vector<std::pair<size_t, std::shared_ptr<Reader>>> & native,
    rmw_subscriptions_t * subs, rmw_guard_conditions_t * guards, rmw_services_t * services,
    rmw_clients_t * clients, rmw_events_t * events, rmw_wait_set_t * ws, const rmw_time_t * timeout)
  {
    std::vector<void *> guard_data;
    if (guards && guards->guard_condition_count)
      guard_data.assign(
        guards->guard_conditions, guards->guard_conditions + guards->guard_condition_count);
    guard_data.push_back(interrupt->data);
    rmw_guard_conditions_t combined{guard_data.size(), guard_data.data()};
    std::vector<pollfd> fds{{done_fd, POLLIN, 0}};
    std::vector<size_t> fd_starts;
    std::vector<void *> original;
    bool queued = false;
    bool tracing = false;
    HandoffTrace::DirectTimes times;
    for (auto & [idx, r] : native) {
      original.push_back(subs->subscribers[idx]);
      fd_starts.push_back(fds.size());
      for (const auto & route : r->routes) fds.push_back({route->notification_fd(), POLLIN, 0});
      tracing |= bool(r->trace);
      queued |= r->ready();
    }
    fd_starts.push_back(fds.size());
    auto events_for = [&](size_t i) {
      short events = 0;
      for (size_t fd = fd_starts[i]; fd < fd_starts[i + 1]; ++fd) events |= fds[fd].revents;
      return events;
    };
    // Only delegated readers participate in the Cyclone wait. The native
    // shadow graph entities can match external writers without native data.
    std::vector<size_t> indices;
    std::vector<void *> delegated;
    size_t n = 0;
    for (size_t i = 0; i < subs->subscriber_count; ++i) {
      if (n < native.size() && native[n].first == i) {
        ++n;
        continue;
      }
      indices.push_back(i);
      delegated.push_back(subs->subscribers[i]);
    }
    rmw_subscriptions_t dds_subs{delegated.size(), delegated.data()};
    rmw_ret_t result = RMW_RET_ERROR;
    rmw_time_t zero{};
    auto dds_wait = [&](const rmw_time_t * t) {
      result = CYCLONE(rmw_wait)(&dds_subs, &combined, services, clients, events, ws, t);
    };
    // Diagnostic only: delegated entities cannot wake this native-only poll.
    static const bool native_only = [] {
      const char * flag = std::getenv("AGNOCAST_RMW_PROBE_NATIVE_ONLY");
      return flag && std::strcmp(flag, "1") == 0;
    }();
    if (fixed_probe) {
      using View = std::pair<void **, size_t>;
      std::array<View, 5> views{
        View{delegated.data(), delegated.size()}, View{guard_data.data(), guard_data.size()},
        services ? View{services->services, services->service_count} : View{},
        clients ? View{clients->clients, clients->client_count} : View{},
        events ? View{events->events, events->event_count} : View{}};
      if (!fixed_set) {
        native_interrupts = 0;  // Begin accounting after any ordinary mixed-wait warmup.
        fixed_set = CYCLONE(rmw_create_wait_set)(context, 0);
        if (!fixed_set) return error("fixed DDS wait set creation failed");
        for (size_t i = 0; i < views.size(); ++i) {
          auto [p, n] = views[i];
          if (n) fixed_inputs[i].assign(p, p + n);
        }
      }
      for (size_t i = 0; i < views.size(); ++i) {
        auto [p, n] = views[i];
        if (
          n != fixed_inputs[i].size() ||
          !std::equal(fixed_inputs[i].begin(), fixed_inputs[i].end(), p)) {
          std::fprintf(
            stderr, "fixed topology list=%zu old=%zu new=%zu wakes=%u permutation=%d\n", i,
            fixed_inputs[i].size(), n, native_wakes,
            int(
              n == fixed_inputs[i].size() &&
              std::is_permutation(fixed_inputs[i].begin(), fixed_inputs[i].end(), p)));
          return error("fixed DDS topology changed");
        }
      }
      if (!armed) {
        fixed_output = fixed_inputs;
        std::lock_guard lock(mutex);
        if (!worker.joinable()) worker = std::thread([this] { serve(); });
        job = [this] {
          auto & v = fixed_output;
          rmw_subscriptions_t s{v[0].size(), v[0].data()};
          rmw_guard_conditions_t g{v[1].size(), v[1].data()};
          rmw_services_t sv{v[2].size(), v[2].data()};
          rmw_clients_t c{v[3].size(), v[3].data()};
          rmw_events_t e{v[4].size(), v[4].data()};
          fixed_result = CYCLONE(rmw_wait)(&s, &g, &sv, &c, &e, fixed_set, nullptr);
        };
        armed = true;
        ++arms;
        cv.notify_one();
      }
      timespec duration{};
      if (timeout && !queued) {
        duration.tv_sec = timeout->sec;
        duration.tv_nsec = timeout->nsec;
      }
      int rc = ppoll(fds.data(), fds.size(), timeout || queued ? &duration : nullptr, nullptr);
      if (rc < 0 && errno != EINTR) return error("fixed mixed poll failed");
      bool dds_ready = fds.front().revents & POLLIN;
      bool native_ready =
        std::any_of(fds.begin() + 1, fds.end(), [](auto fd) { return fd.revents & POLLIN; });
      if (native_ready) {
        auto & trace = hop_reader();
        trace.mark("poll");
        static const char * probe = std::getenv("BENCH_POLL_WORK");
        if (trace.path && probe) {
          const bool work = std::strcmp(probe, "work") == 0;
          if (!work && std::strcmp(probe, "empty") != 0) std::terminate();
          timespec start{}, end{};
          if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start)) std::terminate();
          uint64_t value = 1;
          for (int i = 0; i < (work ? 4096 : 0); ++i) {
            value ^= value >> 17;
            value *= UINT64_C(0x9e3779b97f4a7c15);
            asm volatile("" : "+r"(value));
          }
          if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end)) std::terminate();
          if (value != (work ? UINT64_C(11663699708554462312) : 1)) std::terminate();
          trace.mark("poll_work_end", (end.tv_sec - start.tv_sec) * INT64_C(1000000000)
            + end.tv_nsec - start.tv_nsec);
        }
      }
      native_wakes += native_ready;
      both += native_ready && dds_ready;
      if (dds_ready) {
        eventfd_t count;
        std::lock_guard lock(mutex);
        if (eventfd_read(done_fd, &count)) return error("fixed completion read failed");

        result = fixed_result;
        armed = false;
        ++completions;
      } else {
        result = RMW_RET_TIMEOUT;
      }
      for (size_t i = 0; i < views.size(); ++i) {
        auto [p, n] = views[i];
        if (!n) continue;
        if (dds_ready)
          std::copy_n(fixed_output[i].begin(), n, p);
        else
          std::fill_n(p, n, nullptr);
      }
    } else if (native_only) {
      timespec duration{};
      if (timeout && !queued) {
        duration.tv_sec = timeout->sec;
        duration.tv_nsec = timeout->nsec;
      }
      int rc =
        ppoll(fds.data() + 1, fds.size() - 1, timeout || queued ? &duration : nullptr, nullptr);
      if (rc < 0 && errno != EINTR) return error("native-only poll failed");
      std::fill(delegated.begin(), delegated.end(), nullptr);
      std::fill(guard_data.begin(), guard_data.end(), nullptr);
      if (services) std::fill_n(services->services, services->service_count, nullptr);
      if (clients) std::fill_n(clients->clients, clients->client_count, nullptr);
      if (events) std::fill_n(events->events, events->event_count, nullptr);
      result = RMW_RET_TIMEOUT;
    } else if (queued || (timeout && !timeout->sec && !timeout->nsec)) {
      dds_wait(&zero);
      if (poll(fds.data() + 1, fds.size() - 1, 0) < 0 && errno != EINTR)
        return error("native poll failed");
    } else {
      {
        std::lock_guard lock(mutex);
        if (!worker.joinable()) worker = std::thread([this] { serve(); });
        job = [&] {
          dds_wait(timeout);
          if (tracing) times.helper_return = HandoffTrace::now();
        };
      }
      cv.notify_one();
      int rc;
      do {
        rc = poll(fds.data(), fds.size(), -1);
      } while (rc < 0 && errno == EINTR);
      if (tracing) times.wake = HandoffTrace::now();
      if (rc < 0 || !(fds.front().revents & POLLIN)) {
        ++native_interrupts;
        if (CYCLONE(rmw_trigger_guard_condition)(interrupt.get()) != RMW_RET_OK) std::terminate();
      }
      if (tracing) times.interrupted = HandoffTrace::now();
      // Acquire the loan while the helper is returning its borrowed DDS handles.
      std::exception_ptr failure;
      try {
        for (size_t i = 0; i < native.size(); ++i)
          if (events_for(i) & POLLIN) native[i].second->receive_polled(&fds[fd_starts[i]]);
      } catch (...) {
        failure = std::current_exception();
      }
      if (tracing) times.drained = HandoffTrace::now();
      eventfd_t count;
      int status;
      do {
        status = eventfd_read(done_fd, &count);
      } while (status < 0 && errno == EINTR);
      if (status < 0) std::terminate();
      {
        std::lock_guard sync(mutex);
      }  // synchronize with completion in serve()
      if (tracing) times.synchronized = HandoffTrace::now();
      if (failure) std::rethrow_exception(failure);
      if (rc < 0) return error("mixed wait poll failed");
    }
    if (result != RMW_RET_OK && result != RMW_RET_TIMEOUT)
      return error("delegated DDS wait failed");
    for (size_t i = 0; i < indices.size(); ++i) subs->subscribers[indices[i]] = delegated[i];
    if (guards && guards->guard_condition_count)
      std::copy_n(guard_data.begin(), guards->guard_condition_count, guards->guard_conditions);
    for (size_t i = 0; i < native.size(); ++i) {
      auto & [idx, r] = native[i];
      if (events_for(i) & (POLLERR | POLLHUP | POLLNVAL))
        return error("native notification fd failed");
      if ((fixed_probe || native_only || queued || (timeout && !timeout->sec && !timeout->nsec)) && (events_for(i) & POLLIN))
        r->receive_polled(&fds[fd_starts[i]]);
      auto timing = r->trace && times.wake && (events_for(i) & POLLIN) ? &times : nullptr;
      if (r->ready(0, 0, false, timing)) {
        if (events_for(i) & POLLIN) hop_reader().mark("ready");
        subs->subscribers[idx] = original[i];
        result = RMW_RET_OK;
      } else
        subs->subscribers[idx] = nullptr;
    }
    return result;
  }
};
thread_local std::weak_ptr<DirectWaitSet> fixed_owner;
std::map<const void *, std::shared_ptr<DirectWaitSet>> direct_waitsets;
}  // namespace
// The single-threaded probe owns all registered entities until it ends this scope.
extern "C" void agnocast_fixed_probe_scope(bool begin)
{
  fixed_probe = begin;
  if (!begin) {
    if (auto state = fixed_owner.lock()) state->stop_fixed();
    fixed_owner.reset();
  }
}
extern "C" rmw_wait_set_t * rmw_create_wait_set(rmw_context_t * context, size_t max_conditions)
{
  auto ws = CYCLONE(rmw_create_wait_set)(context, max_conditions);
  if (ws && direct_wait_enabled()) {
    try {
      auto state = std::make_shared<DirectWaitSet>(context);
      std::lock_guard lock(registry);
      direct_waitsets.emplace(ws, std::move(state));
    } catch (const std::exception & e) {
      CYCLONE(rmw_destroy_wait_set)(ws);
      error(e.what());
      return nullptr;
    }
  }
  return ws;
}
extern "C" rmw_ret_t rmw_destroy_wait_set(rmw_wait_set_t * ws)
{
  if (direct_wait_enabled()) {
    std::lock_guard lock(registry);
    direct_waitsets.erase(ws);
  }
  return CYCLONE(rmw_destroy_wait_set)(ws);
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
  if (direct_wait_enabled()) {
    auto state = lookup(direct_waitsets, ws);
    if (!state) return error("missing direct wait set");
    if (fixed_probe) {
      if (auto prior = fixed_owner.lock(); prior && prior != state) return error("fixed executor changed");
      fixed_owner = state;
    }
    try {
      return state->wait(native, subs, guards, services, clients, events, ws, timeout);
    } catch (const std::exception & e) {
      return error(e.what());
    }
  }
  std::vector<void *> guard_data;
  if (guards && guards->guard_condition_count)
    guard_data.assign(
      guards->guard_conditions, guards->guard_conditions + guards->guard_condition_count);
  bool tracing = false;
  bool queued = false;
  for (auto & [idx, r] : native) {
    guard_data.push_back(r->guard->data);
    tracing |= bool(r->trace);
    queued |= r->ready();
  }
  rmw_guard_conditions_t combined{guard_data.size(), guard_data.data()};
  rmw_time_t zero{};
  // Native subscriptions have Cyclone graph handles. Preserve the pointer
  // on readiness even though the delegated wait clears empty DDS readers.
  std::vector<void *> originals;
  for (auto & [idx, r] : native) originals.push_back(subs->subscribers[idx]);
  auto start = tracing ? HandoffTrace::now() : 0;
  auto ret =
    CYCLONE(rmw_wait)(subs, &combined, services, clients, events, ws, queued ? &zero : timeout);
  auto returned = tracing ? HandoffTrace::now() : 0;
  if (ret != RMW_RET_OK && ret != RMW_RET_TIMEOUT) return ret;
  if (guards && guards->guard_condition_count)
    std::copy_n(guard_data.begin(), guards->guard_condition_count, guards->guard_conditions);
  for (size_t i = 0; i < native.size(); ++i) {
    auto & [idx, r] = native[i];
    auto ready = r->ready(
      r->trace ? start : 0, returned, !queued && (!timeout || timeout->sec || timeout->nsec));
    subs->subscribers[idx] = ready ? originals[i] : nullptr;
    if (ready) ret = RMW_RET_OK;
  }
  return ret;
}
