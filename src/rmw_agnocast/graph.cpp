#include "graph.hpp"

#include <fcntl.h>
#include <poll.h>
#include <rcutils/logging_macros.h>
#include <rcutils/strdup.h>
#include <rmw/get_node_info_and_types.h>
#include <rmw/get_service_endpoint_info.h>
#include <rmw/get_service_names_and_types.h>
#include <rmw/get_topic_endpoint_info.h>
#include <rmw/get_topic_names_and_types.h>
#include <rmw/validate_full_topic_name.h>
#include <rmw/validate_namespace.h>
#include <rmw/validate_node_name.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace
{
using namespace standalone;
namespace fs = std::filesystem;
std::string directory(const rmw_node_t * node)
{
  struct stat ns{};
  if (stat("/proc/self/ns/ipc", &ns)) throw std::runtime_error("cannot identify IPC namespace");
  return "/dev/shm/rmw_agnocast_graph_v1_" + std::to_string(geteuid()) + "_" +
         std::to_string(ns.st_ino) + "_" + std::to_string(node->context->actual_domain_id);
}
struct Metadata
{
  char kind{};
  rosidl_type_hash_t hash{};
  uint8_t gid[RMW_GID_STORAGE_SIZE]{};
  rmw_qos_profile_t qos{};
};
struct Record
{
  Metadata meta;
  std::string node, ns, enclave, name, type;
};
std::vector<Record> snapshot(const rmw_node_t * node)
{
  std::vector<Record> out;
  for (const auto & file : fs::directory_iterator(directory(node))) {
    if (file.path().extension() != ".live") continue;
    int fd = open(file.path().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
      if (errno == ENOENT) continue;
      throw std::runtime_error("cannot open native graph record");
    }
    struct stat st{};
    if (
      fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < static_cast<off_t>(sizeof(Metadata)) ||
      st.st_size > 65536) {
      close(fd);
      continue;
    }
    if (!flock(fd, LOCK_EX | LOCK_NB)) {
      unlink(file.path().c_str());
      close(fd);
      continue;
    }
    if (errno != EWOULDBLOCK) {
      close(fd);
      throw std::runtime_error("cannot inspect graph lease");
    }
    std::string bytes;
    try {
      bytes.resize(st.st_size);
    } catch (...) {
      close(fd);
      throw;
    }
    auto n = pread(fd, bytes.data(), bytes.size(), 0);
    close(fd);
    if (n != st.st_size) continue;
    Record r;
    std::memcpy(&r.meta, bytes.data(), sizeof(Metadata));
    std::istringstream fields(bytes.substr(sizeof(Metadata)));
    if (
      fields >> std::quoted(r.node) >> std::quoted(r.ns) >> std::quoted(r.enclave) >>
      std::quoted(r.name) >> std::quoted(r.type))
      out.push_back(std::move(r));
  }
  return out;
}
template <class F>
rmw_ret_t inspect(const rmw_node_t * node, F f)
{
  if (!node) return RMW_RET_INVALID_ARGUMENT;
  if (node->implementation_identifier != id) return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  try {
    return f(snapshot(node));
  } catch (const std::bad_alloc &) {
    return RMW_RET_BAD_ALLOC;
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return RMW_RET_ERROR;
  }
}
rmw_ret_t strings(
  const std::vector<std::string> & values, rcutils_allocator_t * a, rcutils_string_array_t * out)
{
  if (!a || !out || out->data || out->size) return RMW_RET_INVALID_ARGUMENT;
  auto ret = rcutils_string_array_init(out, values.size(), a);
  if (ret != RCUTILS_RET_OK) return RMW_RET_BAD_ALLOC;
  for (size_t i = 0; i < values.size(); ++i)
    if (!(out->data[i] = rcutils_strdup(values[i].c_str(), *a))) {
      (void)rcutils_string_array_fini(out);
      return RMW_RET_BAD_ALLOC;
    }
  return RMW_RET_OK;
}
rmw_ret_t names(
  const rmw_node_t * n, rcutils_allocator_t * a, const char * node, const char * ns,
  const char * kinds, rmw_names_and_types_t * out)
{
  if (
    !a || !rcutils_allocator_is_valid(a) || !out ||
    rmw_names_and_types_check_zero(out) != RMW_RET_OK)
    return RMW_RET_INVALID_ARGUMENT;
  if (node) {
    int result = 0;
    if (
      !ns || rmw_validate_node_name(node, &result, nullptr) != RMW_RET_OK ||
      result != RMW_NODE_NAME_VALID || rmw_validate_namespace(ns, &result, nullptr) != RMW_RET_OK ||
      result != RMW_NAMESPACE_VALID)
      return RMW_RET_INVALID_ARGUMENT;
  }
  return inspect(n, [&](const auto & rows) -> rmw_ret_t {
    std::map<std::string, std::set<std::string>> result;
    bool found = !node;
    for (auto & r : rows) {
      if (node && (r.node != node || r.ns != ns)) continue;
      found |= r.meta.kind == 'N';
      if (std::strchr(kinds, r.meta.kind)) result[r.name].insert(r.type);
    }
    if (!found) return RMW_RET_NODE_NAME_NON_EXISTENT;
    auto ret = rmw_names_and_types_init(out, result.size(), a);
    if (ret != RMW_RET_OK) return ret;
    size_t i = 0;
    for (const auto & entry : result) {
      out->names.data[i] = rcutils_strdup(entry.first.c_str(), *a);
      ret = out->names.data[i]
              ? strings({entry.second.begin(), entry.second.end()}, a, &out->types[i])
              : RMW_RET_BAD_ALLOC;
      if (ret != RMW_RET_OK) {
        (void)rmw_names_and_types_fini(out);
        return ret;
      }
      ++i;
    }
    return RMW_RET_OK;
  });
}
rmw_ret_t count(const rmw_node_t * node, const char * name, char kind, size_t * out)
{
  int valid = 0;
  if (
    !name || !out || rmw_validate_full_topic_name(name, &valid, nullptr) != RMW_RET_OK ||
    valid != RMW_TOPIC_VALID)
    return RMW_RET_INVALID_ARGUMENT;
  return inspect(node, [&](const auto & rows) {
    *out = 0;
    for (auto & r : rows)
      if (r.meta.kind == kind && r.name == name) ++*out;
    return RMW_RET_OK;
  });
}
rmw_ret_t nodes(
  const rmw_node_t * node, rcutils_string_array_t * names, rcutils_string_array_t * namespaces,
  rcutils_string_array_t * enclaves)
{
  if (!names || !namespaces) return RMW_RET_INVALID_ARGUMENT;
  return inspect(node, [&](const auto & rows) -> rmw_ret_t {
    auto alloc = rcutils_get_default_allocator();
    std::vector<std::string> n, s, e;
    for (auto & r : rows)
      if (r.meta.kind == 'N') {
        n.push_back(r.node);
        s.push_back(r.ns);
        e.push_back(r.enclave);
      }
    auto ret = strings(n, &alloc, names);
    if (ret != RMW_RET_OK) return ret;
    ret = strings(s, &alloc, namespaces);
    if (ret != RMW_RET_OK) {
      (void)rcutils_string_array_fini(names);
      return ret;
    }
    if (enclaves && (ret = strings(e, &alloc, enclaves)) != RMW_RET_OK) {
      (void)rcutils_string_array_fini(names);
      (void)rcutils_string_array_fini(namespaces);
    }
    return ret;
  });
}
}  // namespace
void standalone::Lease::announce(
  const rmw_node_t * node, char kind, const std::string & name, const std::string & type,
  const rosidl_type_hash_t * hash, const rmw_qos_profile_t * qos, const rmw_gid_t * gid)
{
  auto dir = directory(node);
  if (mkdir(dir.c_str(), 0700) && errno != EEXIST)
    throw std::runtime_error("cannot create graph directory");
  struct stat st{};
  if (
    lstat(dir.c_str(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() ||
    (st.st_mode & 0077))
    throw std::runtime_error("unsafe graph directory permissions");
  Metadata m{};
  m.kind = kind;
  if (hash) m.hash = *hash;
  if (qos) m.qos = *qos;
  if (gid)
    std::memcpy(m.gid, gid->data, sizeof(m.gid));
  else if (getrandom(m.gid, sizeof(m.gid), 0) != sizeof(m.gid))
    throw std::runtime_error("cannot generate endpoint id");
  std::ostringstream fields;
  fields << std::quoted(node->name) << ' ' << std::quoted(node->namespace_) << ' '
         << std::quoted(node->context->options.enclave) << ' ' << std::quoted(name) << ' '
         << std::quoted(type);
  auto bytes = std::string(reinterpret_cast<char *>(&m), sizeof(m)) + fields.str();
  path = dir + "/entity-XXXXXX";
  fd = mkostemp(path.data(), O_CLOEXEC);
  if (fd < 0) {
    path.clear();
    throw std::runtime_error("cannot create graph lease");
  }
  if (
    flock(fd, LOCK_EX) ||
    write(fd, bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size()))
    throw std::runtime_error("cannot write graph lease");
  auto dest = path + ".live";
  if (rename(path.c_str(), dest.c_str())) throw std::runtime_error("cannot publish graph record");
  path.swap(dest);
}
standalone::Lease::~Lease()
{
  if (fd >= 0) {
    unlink(path.c_str());
    close(fd);
  }
}
void standalone::GraphWatch::start(const rmw_node_t * node, rmw_guard_condition_t * guard)
{
  events = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  stop = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (
    events < 0 || stop < 0 ||
    inotify_add_watch(events, directory(node).c_str(), IN_MOVED_TO | IN_DELETE | IN_CLOSE_WRITE) <
      0)
    throw std::runtime_error("cannot watch local graph");
  worker = std::thread([this, guard, node] {
    pollfd fds[] = {{events, POLLIN, 0}, {stop, POLLIN, 0}};
    char buf[4096];
    while (true) {
      int ret = poll(fds, 2, -1);
      if (ret < 0 && errno == EINTR) continue;
      if (ret < 0 || stopping) return;
      if (fds[1].revents) {
        eventfd_t value;
        eventfd_read(stop, &value);
      }
      try {
        std::vector<std::shared_ptr<Event>> live;
        {
          std::lock_guard lock(mutex);
          for (auto it = listeners.begin(); it != listeners.end();) {
            if (auto e = it->lock()) {
              live.push_back(std::move(e));
              ++it;
            } else
              it = listeners.erase(it);
          }
        }
        if (!live.empty()) {
          auto snapshot_start = std::chrono::steady_clock::now();
          std::vector<Peer> peers;
          for (auto & r : snapshot(node))
            if (r.meta.kind == 'P' || r.meta.kind == 'S')
              peers.push_back(
                {r.name, r.type,
                 std::string(reinterpret_cast<const char *>(r.meta.gid), sizeof(r.meta.gid)),
                 r.meta.kind, r.meta.qos});
          for (auto & e : live) e->update(peers, snapshot_start);
        }
      } catch (const std::exception & e) {
        RCUTILS_LOG_ERROR_NAMED("Agnocast", "graph update failed: %s", e.what());
      }
      if (read(events, buf, sizeof(buf)) > 0) (void)rmw_trigger_guard_condition(guard);
    }
  });
}
standalone::GraphWatch::~GraphWatch()
{
  if (worker.joinable()) {
    stopping = true;
    eventfd_write(stop, 1);
    worker.join();
  }
  if (events >= 0) close(events);
  if (stop >= 0) close(stop);
}
rmw_ret_t rmw_get_subscriber_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, bool no_demangle, rmw_names_and_types_t * topic_names_and_types)
{
  return (!node_name || !node_namespace)
           ? RMW_RET_INVALID_ARGUMENT
           : names(node, allocator, node_name, node_namespace, "S", topic_names_and_types);
}
rmw_ret_t rmw_get_publisher_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, bool no_demangle, rmw_names_and_types_t * topic_names_and_types)
{
  return (!node_name || !node_namespace)
           ? RMW_RET_INVALID_ARGUMENT
           : names(node, allocator, node_name, node_namespace, "P", topic_names_and_types);
}
rmw_ret_t rmw_get_service_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, rmw_names_and_types_t * service_names_and_types)
{
  return (!node_name || !node_namespace)
           ? RMW_RET_INVALID_ARGUMENT
           : names(node, allocator, node_name, node_namespace, "V", service_names_and_types);
}
rmw_ret_t rmw_get_client_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, rmw_names_and_types_t * service_names_and_types)
{
  return (!node_name || !node_namespace)
           ? RMW_RET_INVALID_ARGUMENT
           : names(node, allocator, node_name, node_namespace, "C", service_names_and_types);
}
rmw_ret_t rmw_get_service_names_and_types(
  const rmw_node_t * node, rcutils_allocator_t * allocator,
  rmw_names_and_types_t * service_names_and_types)
{
  return names(node, allocator, nullptr, nullptr, "VC", service_names_and_types);
}
rmw_ret_t rmw_get_topic_names_and_types(
  const rmw_node_t * node, rcutils_allocator_t * allocator, bool no_demangle,
  rmw_names_and_types_t * topic_names_and_types)
{
  return names(node, allocator, nullptr, nullptr, "PS", topic_names_and_types);
}
rmw_ret_t rmw_get_node_names(
  const rmw_node_t * node, rcutils_string_array_t * node_names,
  rcutils_string_array_t * node_namespaces)
{
  return nodes(node, node_names, node_namespaces, nullptr);
}
rmw_ret_t rmw_get_node_names_with_enclaves(
  const rmw_node_t * node, rcutils_string_array_t * node_names,
  rcutils_string_array_t * node_namespaces, rcutils_string_array_t * enclaves)
{
  if (!enclaves) return RMW_RET_INVALID_ARGUMENT;
  return nodes(node, node_names, node_namespaces, enclaves);
}
rmw_ret_t rmw_count_publishers(const rmw_node_t * node, const char * topic_name, size_t * count)
{
  return ::count(node, topic_name, 'P', count);
}
rmw_ret_t rmw_count_subscribers(const rmw_node_t * node, const char * topic_name, size_t * count)
{
  return ::count(node, topic_name, 'S', count);
}
rmw_ret_t rmw_count_clients(const rmw_node_t * node, const char * service_name, size_t * count)
{
  return ::count(node, service_name, 'C', count);
}
rmw_ret_t rmw_count_services(const rmw_node_t * node, const char * service_name, size_t * count)
{
  return ::count(node, service_name, 'V', count);
}
namespace
{
template <class Array>
rmw_ret_t endpoint_info(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * name, char kind,
  Array * out)
{
  if (!name || !out || !allocator || !rcutils_allocator_is_valid(allocator))
    return RMW_RET_INVALID_ARGUMENT;
  constexpr bool service = std::is_same_v<Array, rmw_service_endpoint_info_array_t>;
  if constexpr (service) {
    if (rmw_service_endpoint_info_array_check_zero(out) != RMW_RET_OK)
      return RMW_RET_INVALID_ARGUMENT;
  } else if (rmw_topic_endpoint_info_array_check_zero(out) != RMW_RET_OK)
    return RMW_RET_INVALID_ARGUMENT;
  return inspect(node, [&](const auto & rows) -> rmw_ret_t {
    size_t n = 0;
    for (auto & row : rows)
      if (row.meta.kind == kind && row.name == name) ++n;
    rmw_ret_t ret;
    if constexpr (service)
      ret = rmw_service_endpoint_info_array_init_with_size(out, n, allocator);
    else
      ret = rmw_topic_endpoint_info_array_init_with_size(out, n, allocator);
    if (ret != RMW_RET_OK) return ret;
    size_t i = 0;
    for (auto & row : rows) {
      if (row.meta.kind != kind || row.name != name) continue;
      auto & entry = out->info_array[i++];
      entry.node_name = rcutils_strdup(row.node.c_str(), *allocator);
      entry.node_namespace = rcutils_strdup(row.ns.c_str(), *allocator);
      if (!entry.node_name || !entry.node_namespace) {
        ret = RMW_RET_BAD_ALLOC;
        break;
      }
      if constexpr (service) {
        entry.service_type = rcutils_strdup(row.type.c_str(), *allocator);
        if (!entry.service_type) {
          ret = RMW_RET_BAD_ALLOC;
          break;
        }
        entry.service_type_hash = row.meta.hash;
        entry.endpoint_type = kind == 'V' ? RMW_ENDPOINT_SERVER : RMW_ENDPOINT_CLIENT;
        entry.endpoint_count = 1;
        ret = rmw_service_endpoint_info_set_gids(
          &entry, row.meta.gid, 1, sizeof(row.meta.gid), allocator);
        if (ret != RMW_RET_OK) break;
        ret = rmw_service_endpoint_info_set_qos_profiles(&entry, &row.meta.qos, 1, allocator);
        if (ret != RMW_RET_OK) break;
      } else {
        entry.topic_type = rcutils_strdup(row.type.c_str(), *allocator);
        if (!entry.topic_type) {
          ret = RMW_RET_BAD_ALLOC;
          break;
        }
        entry.topic_type_hash = row.meta.hash;
        entry.qos_profile = row.meta.qos;
        entry.endpoint_type = kind == 'P' ? RMW_ENDPOINT_PUBLISHER : RMW_ENDPOINT_SUBSCRIPTION;
        std::memcpy(entry.endpoint_gid, row.meta.gid, sizeof(entry.endpoint_gid));
      }
    }
    if (ret != RMW_RET_OK) {
      if constexpr (service)
        (void)rmw_service_endpoint_info_array_fini(out, allocator);
      else
        (void)rmw_topic_endpoint_info_array_fini(out, allocator);
    }
    return ret;
  });
}
}  // namespace
rmw_ret_t rmw_get_clients_info_by_service(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * service_name,
  bool no_mangle, rmw_service_endpoint_info_array_t * clients_info)
{
  return endpoint_info(node, allocator, service_name, 'C', clients_info);
}
rmw_ret_t rmw_get_servers_info_by_service(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * service_name,
  bool no_mangle, rmw_service_endpoint_info_array_t * servers_info)
{
  return endpoint_info(node, allocator, service_name, 'V', servers_info);
}
rmw_ret_t rmw_get_publishers_info_by_topic(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * topic_name, bool no_mangle,
  rmw_topic_endpoint_info_array_t * publishers_info)
{
  return endpoint_info(node, allocator, topic_name, 'P', publishers_info);
}
rmw_ret_t rmw_get_subscriptions_info_by_topic(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * topic_name, bool no_mangle,
  rmw_topic_endpoint_info_array_t * subscriptions_info)
{
  return endpoint_info(node, allocator, topic_name, 'S', subscriptions_info);
}

rmw_ret_t standalone::count_compatible(
  const rmw_node_t * node, const std::string & topic, const std::string & type,
  const rmw_qos_profile_t & qos, bool publisher, size_t * count)
{
  return inspect(node, [&](const auto & rows) {
    *count = 0;
    for (const auto & row : rows) {
      if (row.name != topic || row.type != type || row.meta.kind != (publisher ? 'S' : 'P'))
        continue;
      rmw_qos_compatibility_type_t compatible;
      auto ret = rmw_qos_profile_check_compatible(
        publisher ? qos : row.meta.qos, publisher ? row.meta.qos : qos, &compatible, nullptr, 0);
      if (ret != RMW_RET_OK) return ret;
      if (compatible == RMW_QOS_COMPATIBILITY_OK) ++*count;
    }
    return static_cast<rmw_ret_t>(RMW_RET_OK);
  });
}
