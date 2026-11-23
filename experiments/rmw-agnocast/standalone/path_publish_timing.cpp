// Writer-side diagnostic for the single-publisher, 60-frame Path fixture.
#include <agnocast/agnocast_ioctl.hpp>
#include <dlfcn.h>
#include <time.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
struct PublishTime
{
  int64_t entry;
  uint64_t address;
  int64_t begin_ns, end_ns;
};
std::array<PublishTime, 60> samples;
size_t count;

int64_t now()
{
  timespec stamp{};
  clock_gettime(CLOCK_MONOTONIC, &stamp);
  return stamp.tv_sec * 1000000000LL + stamp.tv_nsec;
}

__attribute__((destructor)) void save()
{
  if (!count) return;
  auto * file = std::fopen(std::getenv("PATH_PUBLISH_TIMES"), "wx");
  if (!file) std::abort();
  for (size_t i = 0; i < count; ++i) {
    const auto & row = samples[i];
    std::fprintf(file, "%ld,%lu,%ld,%ld\n", row.entry, row.address, row.begin_ns, row.end_ns);
  }
  std::fclose(file);
}
}  // namespace

namespace agnocast
{
ioctl_publish_msg_args publish_core(
  const void * handle, const std::string & topic, topic_local_id_t publisher_id, uint64_t address)
{
  static auto real = [] {
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void *>(&publish_core), &info) || !info.dli_sname) std::abort();
    auto fn = reinterpret_cast<decltype(&publish_core)>(dlsym(RTLD_NEXT, info.dli_sname));
    if (!fn) std::abort();
    return fn;
  }();
  if (topic != "/path_latency" && topic.find("/path_latency/") == std::string::npos)
    return real(handle, topic, publisher_id, address);
  auto begin = now();
  auto result = real(handle, topic, publisher_id, address);
  auto end = now();
  if (count >= samples.size()) std::abort();
  samples[count++] = {result.ret_entry_id, address, begin, end};
  return result;
}
}  // namespace agnocast
