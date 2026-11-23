#include "endpoints.hpp"

#include <agnocast/agnocast_executor.hpp>
#include <rclcpp/typesupport_helpers.hpp>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>

#include <rosidl_runtime_c/type_description/type_description__struct.h>
#include <sys/random.h>

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace standalone::endpoints
{
namespace
{
using namespace rosidl_typesupport_introspection_cpp;

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
}  // namespace

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

void Endpoint::configure_deadline(bool publisher)
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

Endpoint::Endpoint(
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
  // Buffer backends belong to the publishing process. Transport nested layouts with
  // their generated serializer; PointCloud2 retains its specialized host representation.
  cdr_wire = !host_cloud && has_buffer(*members);
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

Endpoint::~Endpoint()
{
  deadline.reset();
  for (auto & entry : events) {
    auto & event = *entry.second;
    std::lock_guard lock(event.callback_mutex);
    event.callback = nullptr;
  }
}

rclcpp::QoS Endpoint::ros_qos() const
{
  auto native = qos;
  native.deadline = {};
  native.lifespan = {};  // The RMW enforces publisher lifespan at receive and take.
  return rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(native), native);
}

bool Writer::owns_payload(const std::vector<uint8_t> & bytes, const void * loan)
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

agnocast::ipc_shared_ptr<void> Writer::borrow()
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

void Writer::publish_bytes(const rmw_serialized_message_t & bytes)
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

void Writer::publish_cloud(const Cloud & cloud)
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

void Writer::destroy(void * p)
{
  if (cdr_wire)
    std::destroy_at(static_cast<std::vector<uint8_t> *>(p));
  else if (host_cloud)
    static_cast<HostCloud *>(p)->~HostCloud();
  else
    members->fini_function(p);
  ::operator delete(p);
}

void Writer::cancel(agnocast::ipc_shared_ptr<void> && loan)
{
  native.cancel_message(std::move(loan), [this](void * p) {
    members->fini_function(p);
    ::operator delete(p);
  });
}

void Writer::send(agnocast::ipc_shared_ptr<void> && loan)
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

Writer::~Writer()
{
  if (published_loans)
    std::fprintf(
      stderr, "host-cloud publisher loans=%zu private_copies=%zu\n", published_loans,
      private_copies);
  for (auto & entry : borrowed) cancel(std::move(entry.second));
}

Writer::Writer(
  const rmw_node_t * owner, const rosidl_message_type_support_t * ts, const char * name,
  const rmw_qos_profile_t & q, const rmw_publisher_options_t & options, bool rpc)
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

void PollExecutor::acquire_ready(const std::pair<uint32_t, agnocast::CallbackInfo> & registration)
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

void PollExecutor::prepare()
{
  add_callback_groups_from_nodes_associated_to_executor();
  epoll_manager_->prepare_epoll(
    [this](const auto & group) { return is_callback_group_associated(group); });
}

void PollExecutor::acquire()
{
  agnocast::AgnocastExecutable executable;
  if (get_next_agnocast_executable(executable, 0)) {
    do {
      execute_agnocast_executable(executable);
    } while (get_next_ready_agnocast_executable(executable));
  }
}

Reader::Reader(
  const rmw_node_t * owner, const rosidl_message_type_support_t * ts, const char * name,
  const rmw_qos_profile_t & q, const rmw_subscription_options_t & options, bool rpc)
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

void Reader::acquire()
{
  std::lock_guard lock(acquire_mutex);
  executor.acquire();
}

void Reader::before_take()
{
  if (!direct) return;
  {
    std::lock_guard lock(context->mutex);
    if (ready()) return;
  }
  acquire();
}

bool Reader::ready()
{
  while (!pending.empty() && pending.front().header.expiry &&
         monotonic_ns() >= pending.front().header.expiry)
    pending.pop_front();
  return !pending.empty();
}

Reader::~Reader()
{
  executor.cancel();
  if (thread.joinable()) thread.join();
}
}  // namespace standalone::endpoints

using namespace standalone::endpoints;

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
